/* -*- Mode: C++; c-default-style: "k&r"; indent-tabs-mode: nil; tab-width: 2; c-basic-offset: 2 -*- */

/* libmwaw
* Version: MPL 2.0 / LGPLv2+
*
* The contents of this file are subject to the Mozilla Public License Version
* 2.0 (the "License"); you may not use this file except in compliance with
* the License or as specified alternatively below. You may obtain a copy of
* the License at http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
* for the specific language governing rights and limitations under the
* License.
*
* Major Contributor(s):
* Copyright (C) 2002 William Lachance (wrlach@gmail.com)
* Copyright (C) 2002,2004 Marc Maurer (uwog@uwog.net)
* Copyright (C) 2004-2006 Fridrich Strba (fridrich.strba@bluewin.ch)
* Copyright (C) 2006, 2007 Andrew Ziem
* Copyright (C) 2011, 2012 Alonso Laurent (alonso@loria.fr)
*
*
* All Rights Reserved.
*
* For minor contributions see the git repository.
*
* Alternatively, the contents of this file may be used under the terms of
* the GNU Lesser General Public License Version 2 or later (the "LGPLv2+"),
* in which case the provisions of the LGPLv2+ are applicable
* instead of those above.
*/

#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "CorelPainterParser.hxx"

/** Internal: the structures of a CorelPainterParser */
namespace CorelPainterParserInternal
{
////////////////////////////////////////////////////////////
//! Internal: a node of a CorelPainterParser
struct Node {
  //! constructor
  Node()
  {
    for (auto &v: m_values) v=0;
  }
  //! the child
  std::shared_ptr<Node> m_childs[2];
  //! the values
  int m_values[2];
};
////////////////////////////////////////////////////////////
//! Internal: a zone header of a CorelPainterParser
struct ZoneHeader {
  //! constructor
  ZoneHeader()
    : m_isMainZone(false)
    , m_dimension()
    , m_origin()
    , m_pixelByInch(0)
    , m_numTreeNodes(0)
    , m_tree()
    , m_bitmapPos(0)
    , m_rsrcDataPos(0)
    , m_nextPos(0)
    , m_rsrcMap()
  {
    for (auto &f : m_flags) f=0;
  }
  //! check if it is a picture header
  bool isBitmap() const
  {
    if (m_dimension[0]<=2 || m_dimension[1]<=2) return false;
    long endPos=m_rsrcDataPos>0 ? m_rsrcDataPos : m_nextPos;
    if (m_bitmapPos>=endPos) return false;
    if ((m_flags[1]&1) && m_bitmapPos+4*long(m_dimension[0])*long(m_dimension[1])>endPos)
      return false;
    return true;
  }
  //! a flag to know if this is the main picture
  bool m_isMainZone;
  //! the bitmap dimension
  MWAWVec2i m_dimension;
  //! the bitmap origin
  MWAWVec2i m_origin;
  //! number of pixel by inch
  int m_pixelByInch;
  /// the number of Huffman node
  int m_numTreeNodes;
  /// the Huffman tree
  std::shared_ptr<Node> m_tree;
  /// the bitmap position
  long m_bitmapPos;
  //! the resource data position
  long m_rsrcDataPos;
  //! the next zone position
  long m_nextPos;
  //! the main zone flags
  int m_flags[2];
  //! the different rsrc zone (v7)
  std::map<std::string, MWAWEntry> m_rsrcMap;
};

////////////////////////////////////////
//! Internal: the state of a CorelPainterParser
struct State {
  //! constructor
  State()
    : m_zoneList()
    , m_pixelByInch(0)
  {
  }
  //! the main zone header
  std::vector<ZoneHeader> m_zoneList;
  //! number of pixel by inch
  int m_pixelByInch;
};

////////////////////////////////////////
//! Internal: the subdocument of a CorelPainterParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(CorelPainterParser &pars, MWAWInputStreamPtr const &input, MWAWEntry const &entry, MWAWEntry const &unicodeEntry)
    : MWAWSubDocument(&pars, input, entry)
    , m_unicodeEntry(unicodeEntry)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    if (MWAWSubDocument::operator!=(doc)) return true;
    auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
    if (!sDoc) return true;
    return m_unicodeEntry!=sDoc->m_unicodeEntry;
  }
  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

private:
  //! the unicode entry
  MWAWEntry m_unicodeEntry;

  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("CorelPainterParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<CorelPainterParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("CorelPainterParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  parser->sendText(m_zone, m_unicodeEntry);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
CorelPainterParser::CorelPainterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

CorelPainterParser::~CorelPainterParser()
{
}

void CorelPainterParser::init()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new CorelPainterParserInternal::State);

  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void CorelPainterParser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      for (auto const &z : m_state->m_zoneList) {
        if (z.isBitmap())
          sendBitmap(z);
        else
          sendZone(z);
      }
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("CorelPainterParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void CorelPainterParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::createDocument: listener already exist\n"));
    return;
  }
  if (m_state->m_zoneList.empty()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::createDocument: can not find any zone\n"));
    return;
  }
  auto const &zone=m_state->m_zoneList[0];
  int const &pixelByInch=m_state->m_pixelByInch;
  if (pixelByInch>0 && pixelByInch<0xFFFF) { // time to update the page dimension
    getPageSpan().setFormWidth(0.2+double(zone.m_dimension[0])/double(pixelByInch));
    getPageSpan().setFormLength(0.2+double(zone.m_dimension[1])/double(pixelByInch));
  }
  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  std::vector<MWAWPageSpan> pageList(1,ps);
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool CorelPainterParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  input->seek(0, librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    long pos=input->tell();
    CorelPainterParserInternal::ZoneHeader zone;
    if (!readZoneHeader(zone) || input->tell()>zone.m_nextPos) {
      MWAW_DEBUG_MSG(("CorelPainterParser::createZones: find extra data\n"));
      ascii().addPos(pos);
      ascii().addNote("Entries(UnknownD):###extra");
      break;
    }
    if (zone.m_rsrcDataPos>0)
      readResourcesList(zone);
    m_state->m_zoneList.push_back(zone);
    input->seek(zone.m_nextPos, librevenge::RVNG_SEEK_SET);
  }
  if (m_state->m_zoneList.empty() || !m_state->m_zoneList[0].isBitmap()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::createZones: oops the first zone is not a picture\n"));
    return false;
  }

  return true;
}

std::shared_ptr<MWAWPict> CorelPainterParser::readBitmap(CorelPainterParserInternal::ZoneHeader const &zone)
{
  MWAWInputStreamPtr input = getInput();
  auto const &dim=zone.m_dimension;
  long endPos= zone.m_rsrcDataPos>0 ? zone.m_rsrcDataPos : zone.m_nextPos;
  if (dim[0]<2 || dim[1]<2 || input->tell()>=endPos) return nullptr;
  // in the main zone, the alpha channel is used to store the selected
  // zone, so we must not retrieve it....
  auto bitmap=std::make_shared<MWAWPictBitmapColor>(MWAWVec2i(dim[0],dim[1]), !zone.m_isMainZone);
  std::vector<MWAWColor> listColor;
  if (!zone.m_numTreeNodes) { // uncompressed
    libmwaw::DebugStream f;
    listColor.resize(size_t(dim[0]));
    for (int i=0; i<dim[1]; ++i) {
      long pos=input->tell();
      f.str("");
      f << "BitmapRow[unc]:";
      if (pos+4*dim[0]>endPos) {
        MWAW_DEBUG_MSG(("CorelPainterParser::readBitmap: can not read some row\n"));
        f << "###";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        return nullptr;
      }
      for (size_t c=0; c<size_t(dim[0]); ++c) {
        unsigned char data[4];
        for (auto &d : data) d=static_cast<unsigned char>(input->readULong(1));
        listColor[c]=MWAWColor(data[1],data[2],data[3],data[0]);
      }
      bitmap->setRow(i, listColor.data());
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
  }
  else { // compressed
    std::vector<unsigned char> previousValues;
    previousValues.resize(size_t(4*dim[0]),0);
    for (int i=0; i<dim[1]; ++i) {
      long pos=input->tell();
      if (!readBitmapRow(zone, listColor, previousValues) || input->tell()>endPos) {
        MWAW_DEBUG_MSG(("CorelPainterParser::readBitmap: can not read some row\n"));
        ascii().addPos(pos);
        ascii().addNote("Entries(UnknownB):###extra");
        return nullptr;
      }
      bitmap->setRow(i, listColor.data());
    }
  }
  return bitmap;
}

bool CorelPainterParser::readBitmapRow(CorelPainterParserInternal::ZoneHeader const &zone,
                                       std::vector<MWAWColor> &colorList, std::vector<unsigned char> &previousValues)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  int const &dim=zone.m_dimension[0];
  if (!input->checkPosition(pos+4) || dim<=0) return false;
  libmwaw::DebugStream f;
  f << "Entries(BitmapRow):";
  int type=int(input->readLong(1)); // 0,2
  if (type==0)
    f << "huffman,";
  else if (type!=2) f << "##type=" << type << ",";
  unsigned char firstData=static_cast<unsigned char>(input->readULong(1));
  if (firstData) f << "d0=" << std::hex << int(firstData) << std::dec << ",";
  int sz=int(input->readULong(2));
  long endPos=pos+sz;
  if (sz<4 || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  bool ok=true;
  std::vector<unsigned char> listColorData;
  int expectedNumData=4*dim;
  listColorData.reserve(size_t(expectedNumData));
  listColorData.push_back(firstData);
  switch (type) {
  case 0: { // use Huffman tree
    f << "*[";
    int buffer, numBitsInBuffer=0;
    for (int i=0; i<expectedNumData-1; ++i) {
      int value;
      if (!decompressData(zone, endPos, value, buffer, numBitsInBuffer)) {
        MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: oops, problem when decompressing the data\n"));
        ok=false;
        f << "###";
        break;
      }
      if (i<10)
        f << std::hex << value << std::dec << ",";
      listColorData.push_back(static_cast<unsigned char>(value));
    }
    f << "...],";
    break;
  }
  // case 1: never seems in v1.2, maybe exists in v1.0 or v1.1 ?
  case 2: { // basic compression: 0:(n+1) following values, 1:(n+1)*val1
    while (input->tell()<endPos && listColorData.size() < size_t(expectedNumData)) {
      long actPos=input->tell();
      int subType=int(input->readULong(1));
      if (subType==0) {
        int dSz=int(input->readULong(1));
        long lastPos=actPos+3+dSz;
        if (lastPos>endPos) {
          input->seek(actPos, librevenge::RVNG_SEEK_SET);
          break;
        }
        f << "0[";
        for (int i=0; i<dSz+1; ++i) {
          listColorData.push_back(static_cast<unsigned char>(input->readULong(1)));
          if (i<3)
            f << std::hex << int(listColorData.back()) << std::dec << ",";
          else if (i==3)
            f << "...";
        }
        f << "],";
      }
      else if (subType==1) {
        if (actPos+3>endPos) {
          MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: can not read the color data\n"));
          input->seek(actPos, librevenge::RVNG_SEEK_SET);
          break;
        }
        int nData=int(input->readULong(1));
        unsigned char value=static_cast<unsigned char>(input->readULong(1));
        f << "1[" << std::hex << int(value) << std::dec << "x" << (nData+1) << "],";
        for (int i=0; i<nData+1; ++i) listColorData.push_back(value);
      }
      else {
        input->seek(actPos, librevenge::RVNG_SEEK_SET);
        MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: unknown sub type %d\n", subType));
        ok=false;
        f << "###subType=" << subType;
        break;
      }
    }
    break;
  }
  default:
    MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: unknown type %d\n", type));
    ok=false;
    break;
  }
  if (ok && listColorData.size() != size_t(expectedNumData)) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: bad number of data\n"));
    f << "###numData,";
    ok=false;
  }
  if (input->tell()!=endPos && input->tell()+1!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  if (ok && previousValues.size()!=listColorData.size()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readBitmapRow: oops bad previous values\n"));
    f << "###prevValues,";
    ok=false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  if (!ok) return false;
  // before compressing a row, a difference to the previous row is done,
  // then in this row, a difference "value less previous value" is done.
  //
  // so ...
  unsigned char actCol=0;
  for (size_t i=0; i<listColorData.size(); ++i) {
    actCol = static_cast<unsigned char>(actCol+listColorData[i]);
    previousValues[i] = static_cast<unsigned char>(previousValues[i]+actCol);
  }

  colorList.resize(size_t(dim));
  for (size_t i=0; i<size_t(dim); ++i)
    colorList[i]=MWAWColor(previousValues[i+size_t(dim)], previousValues[i+2*size_t(dim)],previousValues[i+3*size_t(dim)],static_cast<unsigned char>(255-previousValues[i]));
  return true;
}

bool CorelPainterParser::decompressData(CorelPainterParserInternal::ZoneHeader const &zone,
                                        long endPos, int &value, int &buffer, int &numBitsInBuffer)
{
  if (!zone.m_tree) {
    MWAW_DEBUG_MSG(("CorelPainterParser::decompressData: can not find the main tree node\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  auto node=zone.m_tree;
  while (!input->isEnd()) {
    if (numBitsInBuffer<=0) {
      if (input->tell()>=endPos) break;
      buffer=int(input->readULong(1));
      numBitsInBuffer=8;
    }
    int c=(buffer>>(--numBitsInBuffer))&1;
    if (node->m_childs[c]) {
      node=node->m_childs[c];
      continue;
    }
    value=node->m_values[c];
    return true;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return false;
}

bool CorelPainterParser::readDouble(double &res)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readDouble: can not read a double\n"));
    return false;
  }
  auto exp=int(input->readULong(1));
  int fVal=int(input->readULong(1));
  exp=(exp<<1)+(fVal>>7);
  double mantisse=(fVal&0x7f)/128., factor=1./128./256.;
  for (int j=0; j<2; ++j, factor/=256)
    mantisse+=double(input->readULong(1))*factor;
  if (exp==0 && mantisse<=0) { // initialized ?
    res=0;
    return true;
  }
  int sign = 1;
  if (exp & 0x100) {
    exp &= 0xff;
    sign = -1;
  }
  exp -= 0x7f;
  res = std::ldexp(1+mantisse, exp);
  if (sign == -1)
    res *= -1.;
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
MWAWPosition CorelPainterParser::getZonePosition(CorelPainterParserInternal::ZoneHeader const &zone) const
{
  MWAWPageSpan const &page=getPageSpan();
  int pixelByInch=zone.m_pixelByInch;
  if (pixelByInch<=0 || pixelByInch>=0xffff) pixelByInch=m_state->m_pixelByInch;
  if (pixelByInch<=0 || pixelByInch>=0xffff) {
    MWAW_DEBUG_MSG(("CorelPainterParser::updatePosition: can not find the number of pixel by inch\n"));
    pixelByInch=1;
  }
  MWAWPosition pos(MWAWVec2f(float(page.getMarginLeft()),float(page.getMarginRight()))
                   +1.f/float(pixelByInch)*MWAWVec2f(zone.m_origin),
                   1.f/float(pixelByInch)*MWAWVec2f(zone.m_dimension), librevenge::RVNG_INCH);
  pos.setRelativePosition(MWAWPosition::Page);
  pos.m_wrapping = MWAWPosition::WNone;
  return pos;
}

bool CorelPainterParser::sendBitmap(CorelPainterParserInternal::ZoneHeader const &zone)
{
  if (!zone.isBitmap()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendBitmap: oops, the zone is not a bitmap\n"));
    return false;
  }
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendBitmap: can not find the listener\n"));
    return false;
  }

  auto input=getInput();
  input->seek(zone.m_bitmapPos, librevenge::RVNG_SEEK_SET);
  auto bitmap=readBitmap(zone);
  if (!bitmap) return false;

  // let finish to read the zone
  if (zone.m_rsrcDataPos>0 &&input->tell()<zone.m_rsrcDataPos) {
    ascii().addPos(input->tell());
    ascii().addNote("Entries(UnknownB):");
    input->seek(zone.m_rsrcDataPos, librevenge::RVNG_SEEK_SET);
  }
  // send the bitmap
  MWAWEmbeddedObject picture;
  if (!bitmap->getBinary(picture)) return false;

#ifdef DEBUG_WITH_FILES
  if (!picture.m_dataList.empty() && !picture.m_dataList[0].empty()) {
    static int volatile pictName = 0;
    libmwaw::DebugStream f;
    f << "Pict-" << ++pictName << ".png";
    libmwaw::Debug::dumpFile(picture.m_dataList[0], f.str().c_str());
  }
#endif

  listener->insertPicture(getZonePosition(zone), picture);
  return true;
}

bool CorelPainterParser::sendZone(CorelPainterParserInternal::ZoneHeader const &zone)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendZone: can not find the listener\n"));
    return false;
  }

  auto it=zone.m_rsrcMap.find("TEXT");
  auto unicodeIt=zone.m_rsrcMap.find("utxt");
  if (it!=zone.m_rsrcMap.end()) {
    it->second.setParsed(true);
    auto position=getZonePosition(zone);
    position.setSize(MWAWVec2f(-0.1f,-0.1f));
    std::shared_ptr<MWAWSubDocument> doc
    (new CorelPainterParserInternal::SubDocument(*this, getInput(), it->second, unicodeIt!=zone.m_rsrcMap.end() ? unicodeIt->second : MWAWEntry()));
    listener->insertTextBox(position, doc, MWAWGraphicStyle::emptyStyle());
    return true;
  }

  MWAWInputStreamPtr input = getInput();
  if (zone.isBitmap() || zone.m_rsrcDataPos!=0 || zone.m_bitmapPos+2 >= zone.m_nextPos) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendZone: oops, unexpected zone\n"));
    return false;
  }

  input->seek(zone.m_bitmapPos,librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  // CHECKME: do we need to check the zone flags, ie find a poly with zone.m_flags[1]=4[13]10
  MWAWGraphicShape shape;
  MWAWGraphicStyle style;
  if (readPolygon(zone.m_nextPos, shape, style)) {
    // we must rescale the shape
    int pixelByInch=zone.m_pixelByInch;
    if (pixelByInch<=0 || pixelByInch>=0xffff) pixelByInch=m_state->m_pixelByInch;
    if (pixelByInch>0 && pixelByInch<0xff00) { // must be enough to avoid special case
      float const factor=72.f/float(pixelByInch);
      shape.scale(MWAWVec2f(factor,factor));
    }
    listener->insertShape(getZonePosition(zone), shape, style);
  }
  else {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendZone: sending not spline zone is not implemented\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=zone.m_nextPos) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendZone: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Shape):###extra");
  }

  return false;
}

bool CorelPainterParser::sendText(MWAWEntry const &entry, MWAWEntry const &unicodeEntry)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendText: can not find the listener\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length()<140) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendText: bad entry\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(TextZone):";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int val=int(input->readLong(2));
  if (val!=4) f << "f0=" << val << ",";
  int dSz=int(input->readULong(2));
  if (dSz<140 || dSz>entry.length()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendText: unexpected data size value\n"));
    f << "###dSz=" << dSz << ",";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return false;
  }
  else if (dSz!=entry.length())
    f << "header[size]=" << dSz << ",";
  int sizes[2];
  for (auto &s : sizes) s=int(input->readULong(2));
  if (sizes[0]+sizes[1]+140>dSz) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendText: unexpected size value\n"));
    f << "###text[size]=" << sizes[0] << ",";
    f << "###font[size]=" << sizes[1] << ",";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return false;
  }
  f << "text[size]=" << sizes[0] << ",";
  f << "fontname[size]=" << sizes[1] << ",";
  MWAWFont font;
  MWAWParagraph para;
  double value;
  for (int i=0; i<2; ++i) { // value0=0.5?
    readDouble(value);
    if (i==0) font.setSize(float(value));
    if (value<0||value>0) f << (i==0 ? "font[size]" : "value0") << "=" << value << ",";
  }
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0, 0, 0, 0};
    if (val!=expected[i]) f << "f" << i+2 << "=" << std::hex << val << std::dec << ",";
  }
  double opacity=1;
  for (int i=0; i<5; ++i) {
    readDouble(value);
    double const expectedValue[]= {0,1,1,0, 0};
    if (value<expectedValue[i]||value>expectedValue[i]) {
      char const *what[]= {"traking", "leading", "opacity", "blur", "direction[rad]"};
      if (i==2) opacity=value;
      f << what[i] << "=" << value << ",";
    }
  }
  int dim[2]; // begin of first line position?
  for (auto &d : dim) d=int(input->readLong(2));
  f << "pos?=" << MWAWVec2i(dim[1],dim[0]) << ",";
  readDouble(value);
  if (value<1 || value>1) f << "value1=" << value << ",";
  for (int i=0; i<11; ++i) { // g6=0|40, g9=0|100,
    val=int(input->readULong(2));
    int const expected[]= {0, 0, 0, 0, 4, 4,
                           0, 0, 0, 0, 1
                          };
    if (i==9) {
      uint32_t fontFlags=0;
      switch ((val>>8)&3) {
      default:
      case 0:
        break;
      case 1:
        f << "shadow,";
        fontFlags |= MWAWFont::shadowBit;
        break;
      case 2:
        f << "engraved,";
        fontFlags |= MWAWFont::engraveBit;
        break;
      case 3:
        f << "##shadow=3,";
        break;
      }
      font.setFlags(fontFlags);
      val &= 0xfcff;
    }
    if (val!=expected[i]) f << "g" << i << "=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readULong(1));
  switch (val) {
  case 0: // left
    break;
  case 1:
    para.m_justify = MWAWParagraph::JustificationCenter;
    f << "align=center,";
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationRight;
    f << "align=right,";
    break;
  default:
    f << "###align=" << val << ",";
    break;
  }
  input->seek(1, librevenge::RVNG_SEEK_CUR); // unused
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());

  long begTextPos=input->tell();
  input->seek(sizes[0], librevenge::RVNG_SEEK_CUR);

  long pos=input->tell();
  f.str("");
  f << "TextZone-II:";
  std::string fontName;
  for (int i=0; i<sizes[1]; ++i) fontName+=char(input->readULong(1));
  f << fontName << ",";
  if (!fontName.empty()) font.setId(getFontConverter()->getId(fontName));
  val=int(input->readULong(2));
  if (val) f << "f0=" << val << ",";
  MWAWColor color(uint32_t(input->readULong(4))>>8);
  if (!color.isBlack()) f << "col=" << color << ",";
  if (opacity>=0 && opacity<1) {
    uint32_t opValue=uint32_t(opacity*255);
    font.setColor(MWAWColor((color.value()&0xffffff)|(opValue<<24)));
  }
  else
    font.setColor(color);
  MWAWColor shadowColor(uint32_t(input->readULong(4)));
  if (!shadowColor.isBlack()) f << "col[shadow]=" << shadowColor << ",";
  for (int i=0; i<5; ++i) { // f2=0|1, f3=1
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==0)
      f << "curve[type]=" << val << ","; // useme
    else
      f << "f" << i+1 << "=" << val << ",";
  }
  for (int i=0; i<6; ++i) {
    readDouble(value);
    double const expected[]= {0, 70, -70, 130, -70, 200};
    if (value<expected[i]||value>expected[i]) {
      if (i==0)
        f << "centering=" << value << ",";
      else
        f << "val" << i << "=" << value << ",";
    }
  }
  for (int i=0; i<6; ++i) { // 0
    val=int(input->readLong(2));
    if (val==0) continue;
    if (i==4)
      f << "composite[method]=" << val << ",";
    else
      f << "g" << i << "=" << val << ",";
  }
  float cDim[2];
  for (auto &d : cDim) {
    readDouble(value);
    d=float(value);
  }
  MWAWVec2f corner(cDim[0],cDim[1]); // checkme
  if (corner!=MWAWVec2f(4,4)) f << "corner?=" << corner << ",";

  if (input->tell()!=entry.end() && input->tell()+1!=entry.end()) {
    MWAW_DEBUG_MSG(("CorelPainterParser::sendText: find extra data\n"));
    f << "###extra,";
    if (input->tell()!=pos)
      ascii().addDelimiter(input->tell(),'|');
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // time to send the data
  input->seek(begTextPos, librevenge::RVNG_SEEK_SET);
  listener->setFont(font);
  listener->setParagraph(para);
  f.str("");
  f << "TextZone[txt]:";
  if (unicodeEntry.valid()) {
    for (int i=0; i<sizes[0]; ++i) {
      auto c=char(input->readULong(1));
      if (c==0)
        f << "#[0]";
      else
        f << c;
    }
    ascii().addPos(begTextPos);
    ascii().addNote(f.str().c_str());

    f.str("");
    f << "Rsrc[utxt]:";
    input->seek(unicodeEntry.begin(), librevenge::RVNG_SEEK_SET);
    while (input->tell() < unicodeEntry.end()) {
      auto c=static_cast<unsigned char>(input->readULong(1));
      if (c&0x80) {
        // send one unicode character (to be sure to create consistant utf8 string)
        unsigned char outbuf[9];
        int i=0;
        while ((c&0x40) && i<7 && input->tell() < unicodeEntry.end()) {
          outbuf[i++] = c;
          f << "#[" << std::hex << int(c) << std::dec << "]";
          c=static_cast<unsigned char>(input->readULong(1));
        }
        f << "#[" << std::hex << int(c) << std::dec << "]";
        outbuf[i++] = c;
        outbuf[i++] = 0;
        librevenge::RVNGString unicodeString;
        unicodeString.append(reinterpret_cast<char const *>(outbuf));
        listener->insertUnicodeString(unicodeString);
        continue;
      }
      if (c==0) break;
      f << c;
      switch (c) {
      case 9:
        listener->insertTab();
        break;
      case 0xd:
        listener->insertEOL();
        break;
      default:
        listener->insertCharacter(c);
        break;
      }
    }
    ascii().addPos(unicodeEntry.begin());
    ascii().addNote(f.str().c_str());
    return true;
  }
  else {
    for (int i=0; i<sizes[0]; ++i) {
      auto c=char(input->readULong(1));
      if (c==0) {
        MWAW_DEBUG_MSG(("CorelPainterParser::sendText: find char 0\n"));
        f << "#[0]";
        continue;
      }
      f << c;
      switch (c) {
      case 9:
        listener->insertTab();
        break;
      case 0xd:
        listener->insertEOL();
        break;
      default:
        listener->insertCharacter(static_cast<unsigned char>(c));
        break;
      }
    }
  }
  ascii().addPos(begTextPos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read the header
//
// there exists also the movie files
//   00000003003c0030000000020000001400001680 (then list of colors)
// maybe 0 numFrame dimY dimX 0 2[some format?] 14: begin of data 1680: end of data?
////////////////////////////////////////////////////////////
bool CorelPainterParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = CorelPainterParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  input->seek(0,librevenge::RVNG_SEEK_SET);
  MWAWDocument::Type type=MWAWDocument::MWAW_T_CORELPAINTER;
  int const vers=1;

  CorelPainterParserInternal::ZoneHeader zone;
  if (!readZoneHeader(zone) || !zone.isBitmap())
    return false;
  if (strict) {
    auto const &flags=zone.m_flags[1];
    auto const &numTree = zone.m_numTreeNodes;
    bool uncompressed=flags&1;
    if ((uncompressed && numTree!=0) || (!uncompressed && numTree==0)) return false;
  }
  m_state->m_pixelByInch=zone.m_pixelByInch;
  setVersion(vers);
  if (header)
    header->reset(type, vers, MWAWDocument::MWAW_K_PAINT);
  return true;
}

std::shared_ptr<CorelPainterParserInternal::Node> CorelPainterParser::readCompressionTree(long endPos, int numNodes)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (numNodes==0) return nullptr;
  if (numNodes<=0 || endPos<pos+4*numNodes) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readCompressionTree: the number of nodes seems bad\n"));
    return nullptr;
  }
  libmwaw::DebugStream f;
  f << "Entries(Compression):";
  std::vector<std::shared_ptr<CorelPainterParserInternal::Node> > nodesList;
  nodesList.resize(size_t(numNodes));
  nodesList[0]=std::make_shared<CorelPainterParserInternal::Node>();
  for (size_t i=0; i<size_t(numNodes); ++i) {
    auto &node=nodesList[i];
    if (!node) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readCompressionTree: can not find node %d\n", int(i)));
      return nullptr;
    }
    for (int c=0; c<2; ++c) {
      int val=int(input->readULong(2));
      if (val&0x8000) {
        node->m_values[c]=(val&0xff);
        f << std::hex << node->m_values[c] << std::dec;
      }
      else {
        int id=(val/4);
        if (id>=numNodes || nodesList[size_t(id)]) {
          MWAW_DEBUG_MSG(("CorelPainterParser::readCompressionTree: problem with id=%d\n", int(id)));
          return nullptr;
        }
        nodesList[size_t(id)]=node->m_childs[c]=std::make_shared<CorelPainterParserInternal::Node>();
        f << "N" << id;
      }
      if (c==0) f << "-";
    }
    f << ",";
    nodesList.push_back(node);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return nodesList[0];
}

bool CorelPainterParser::readResourcesList(CorelPainterParserInternal::ZoneHeader &zone)
{
  MWAWInputStreamPtr input = getInput();
  if (zone.m_rsrcDataPos<=0 || !input->checkPosition(zone.m_rsrcDataPos) ||
      zone.m_rsrcDataPos>=zone.m_nextPos)
    return false;

  libmwaw::DebugStream f;
  input->seek(zone.m_rsrcDataPos, librevenge::RVNG_SEEK_SET);
  long &endPos = zone.m_nextPos;
  while (input->tell()+4<=endPos && !input->isEnd()) {
    long pos=input->tell();
    int sz=int(input->readULong(4));
    if (sz==0) {
      ascii().addPos(pos);
      ascii().addNote("_");
      return true;
    }
    long endRsrcPos=pos+sz;
    if (sz<16 || endRsrcPos>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    f.str("");
    f << "Entries(Rsrc):";
    std::string type;
    for (int i=0; i<4; ++i) type+=char(input->readULong(1));
    f << type << ",";
    long dSizes[2];
    for (auto &d : dSizes) d=long(input->readULong(4));
    if (dSizes[0]<18 || dSizes[1]<0 || dSizes[0]+dSizes[1]>sz) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readResourcesList: the sizes seems bad\n"));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    MWAWEntry entry;
    entry.setType(type);
    entry.setBegin(pos+dSizes[0]);
    entry.setEnd(endRsrcPos);
    if (dSizes[0]>18) f << "header[sz]=" << dSizes[0] << ",";
    if (dSizes[1]>0) {
      if (zone.m_rsrcMap.find(type)!=zone.m_rsrcMap.end()) {
        f << "##duplicated,";
        MWAW_DEBUG_MSG(("CorelPainterParser::readResourcesList: a entry with the same names already exist\n"));
      }
      else
        zone.m_rsrcMap[type]=entry;
      f << "data[sz]=" << dSizes[1] << ",";
    }

    int nameSz=int(input->readULong(2));
    if (18+nameSz>dSizes[0]) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readResourcesList: the name size seems bad\n"));
      f << "###name[sz]=" << nameSz << ",";
    }
    else if (nameSz) {
      // PCOL => "Paper Color", FSKT => "Friskets", ANNO => "Annotations", NOTE => "Note Text"
      int pSz=int(input->readULong(1));
      if (pSz+1<=nameSz) {
        std::string text;
        for (int c=0; c<pSz; ++c) text+=char(input->readULong(1));
        f << text << ",";
      }
    }

    if (input->tell()!=pos+dSizes[0] && input->tell()+1!=pos+dSizes[0]) {
      f << "##extra,";
      MWAW_DEBUG_MSG(("CorelPainterParser::readResourcesList: find extra header data\n"));
      ascii().addDelimiter(input->tell(),'|');
    }
    input->seek(pos+dSizes[0], librevenge::RVNG_SEEK_SET);
    if (entry.valid())
      readResource(entry);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endRsrcPos, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()<endPos) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readResourcesList: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Rsrc:##extra");
  }

  return true;
}

bool CorelPainterParser::readResource(MWAWEntry &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("CorelPainterParser::readResource: bad entry\n"));
    return false;
  }
  if (entry.isParsed() || entry.type()=="TEXT" || entry.type()=="utxt") return true;
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Rsrc[" << entry.type() << "]:";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  // find also ANNO,GUID with sz=0
  int val;
  if (entry.length()==2 && entry.type()=="TXGL") { // text extra? v8
    val=int(input->readLong(2));
    if (val!=1) f << "f0=" << val << ",";
  }
  else if (entry.length()==4 && entry.type()=="PCOL") {
    MWAWColor col(uint32_t(input->readULong(4)));
    if (!col.isWhite()) f << "bgColor=" << col << ",";
  }
  else if (entry.length()==8 && entry.type()=="MOSA") {
    for (int i=0; i<4; ++i) { // f0=-2
      val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  else if (entry.length()==12 && entry.type()=="WRAP") {
    for (int i=0; i<6; ++i) { // 0
      val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  else if (entry.length()==14 && entry.type()=="RULR") {
    for (int i=0; i<7; ++i) { // f0=0|100, f1=2, f6=f0=0|100
      val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  else if (entry.length()>=8 && entry.type()=="FSKT") {
    std::string type;
    for (int i=0; i<4; ++i) type+=char(input->readULong(1));
    if (type!="FSKT") f << "type2=" << type << ",";
    val=int(input->readULong(4));
    if (val) f << "f0=" << val << ",";
    if (val==1 && entry.length()>=24) {
      val=int(input->readULong(4));
      if (val!=0xc) f << "f1=" << val << ",";
      long N=long(input->readULong(4));
      long hSize=long(input->readULong(4));
      if (hSize<24 || 4*N<0 || hSize+8*N>entry.length()) {
        MWAW_DEBUG_MSG(("CorelPainterParser::readResource: unsure how to read a frisket zone\n"));
        f << "###,";
      }
      else {
        for (int i=0; i<2; ++i) { // fl0=0|4a80
          val=int(input->readULong(2));
          if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
        }
        if (hSize>24) {
          // fl0=0: no data
          // fl0=4a80: 000000000000ffff00000000000000040652656374203900 + 15 points, related to polygon?
          ascii().addDelimiter(input->tell(),'|');
        }
        input->seek(entry.begin()+hSize, librevenge::RVNG_SEEK_SET);
        if (N) {
          long pos=input->tell();
          libmwaw::DebugStream f2;
          f2 << "Rsrc[FSKT-pt]:";
          for (long i=0; i<N; ++i) {
            float dim[2];
            for (auto &d : dim) d=float(input->readLong(4))/float(65536);
            f2 << MWAWVec2f(dim[0],dim[1]) << ",";
          }
          ascii().addPos(pos);
          ascii().addNote(f2.str().c_str());
        }
      }
    }
  }
  else if (entry.length()==256 && entry.type()=="NOTE") {
    int noteSz=int(input->readULong(1));
    std::string note;
    for (int i=0; i<noteSz; ++i) note+=char(input->readULong(1));
    f << note << ",";
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
  }
  else if (entry.length()==544 && entry.type()=="IPAR") {
    for (int i=0; i<16; ++i) { // f2=0|10,f5=0|100,f14=1,f15=-1
      val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    int sSz=int(input->readULong(1));
    if (sSz && sSz<=31) {
      std::string name;
      for (int i=0; i<sSz; ++i) name+=char(input->readULong(1));
      f << name << ",";
    }
    else if (sSz) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readResource: can not read a name\n"));
      f << "##sSz=" << sSz << ",";
    }
    input->seek(entry.begin()+64, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<240; ++i) { // 0
      val=int(input->readLong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
  }
  // v18
  else if (entry.length()==6 && entry.type()=="CSPR") {
    for (int i=0; i<3; ++i) { // f1=3f00
      val=int(input->readULong(2));
      if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
    }
  }
  else if (entry.length()==2 && entry.type()=="CSTI") { // 0
    val=int(input->readLong(2));
    if (val) f << "f0=" << val << ",";
  }
#ifdef DEBUG_WITH_FILES
  // v10
  else if (entry.length()>8 && entry.type()=="RETP") { // Painter-X small preview bitmap (one by file)
    int dim[4];
    for (auto &d : dim) d=int(input->readULong(2));
    f << "dim=" << MWAWVec2i(dim[1],dim[0]) << "x" << MWAWVec2i(dim[3],dim[2]) << ",";
    if (entry.length()!=8+4*long(dim[2])*long(dim[3])) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readResource: bitmap size seem bad\n"));
      f << "##";
      ascii().addPos(entry.begin());
      ascii().addNote(f.str().c_str());
      return true;
    }

    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());

    MWAWPictBitmapColor bitmap(MWAWVec2i(dim[3],dim[2]), false);
    std::vector<MWAWColor> listColor;
    listColor.resize(size_t(dim[3]));
    for (int i=0; i<dim[2]; ++i) {
      long pos=input->tell();
      f.str("");
      f << "Rsrc[RETP-" << i << ":";
      if (pos+4*dim[2]>entry.end()) {
        MWAW_DEBUG_MSG(("CorelPainterParser::readResource: can not read some row\n"));
        f << "###";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        return false;
      }
      for (size_t c=0; c<size_t(dim[3]); ++c) {
        unsigned char data[4];
        for (auto &d : data) d=static_cast<unsigned char>(input->readULong(1));
        listColor[c]=MWAWColor(data[1],data[2],data[3],data[0]);
      }
      bitmap.setRow(i, listColor.data());
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    MWAWEmbeddedObject picture;
    if (bitmap.getBinary(picture) && !picture.m_dataList.empty() && !picture.m_dataList[0].empty()) {
      static int volatile pictName = 0;
      libmwaw::DebugStream f2;
      f2 << "Preview" << ++pictName << ".ppm";
      libmwaw::Debug::dumpFile(picture.m_dataList[0], f2.str().c_str());
    }
    return true;
  }
  else if (entry.type()=="PRFL" && entry.length()>0) {
    // look like a standard .icc file
    MWAW_DEBUG_MSG(("CorelPainterParser::readResouce: this file contains a color profile, unimplemented\n"));
    librevenge::RVNGBinaryData file;
    input->readDataBlock(entry.length(), file);
    libmwaw::Debug::dumpFile(file, "profile.icc");
    ascii().skipZone(entry.begin(), entry.end()-1);
    return true;
  }
  // v18
  else if (entry.type()=="TJPG" && entry.length()>0) {
    librevenge::RVNGBinaryData file;
    input->readDataBlock(entry.length(), file);
    static int volatile pictName = 0;
    libmwaw::DebugStream f2;
    f2 << "Pict" << ++pictName << ".jpg";
    libmwaw::Debug::dumpFile(file, f2.str().c_str());
    ascii().skipZone(entry.begin(), entry.end()-1);
    return true;
  }
  else if (entry.type()=="FSPG" && entry.length()>22) { // paper texture bitamp
    val=int(input->readLong(2));
    if (val!=2) f << "f0=" << val << ",";
    int dim[2];
    for (auto &d : dim) d=int(input->readULong(2));
    f << "dim=" << MWAWVec2i(dim[0], dim[1]) << ",";
    for (int i=0; i<8; ++i) { // f2=1[number of plane?], f8=22?
      val=int(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    if (dim[0]<=0 || dim[1]<=0 || (entry.length()-22)/dim[0]!=dim[1]) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readResouce[FSPG]: can not read the color paper\n"));
      f << "###";
    }
    else {
      MWAWPictBitmapIndexed bitmap(MWAWVec2i(dim[0],dim[1]));
      std::vector<MWAWColor> indexes;
      indexes.reserve(256);
      for (int i=0; i<=255; ++i) indexes.push_back(MWAWColor(static_cast<unsigned char>(i),static_cast<unsigned char>(i),static_cast<unsigned char>(i)));
      bitmap.setColors(indexes);
      bool ok=true;
      for (int r=0; r<dim[1]; ++r) {
        unsigned long numReads;
        uint8_t const *values=input->read(size_t(dim[0]), numReads);
        if (!values || numReads!=static_cast<unsigned long>(dim[0])) {
          MWAW_DEBUG_MSG(("CorelPainterParser::readResouce[FSPG]: can not read row %d\n", r));
          f << "###";
          ok=false;
          break;
        }
        bitmap.setRow(r, reinterpret_cast<unsigned char const *>(values));
      }
      MWAWEmbeddedObject picture;
      if (ok && bitmap.getBinary(picture) && !picture.isEmpty() && !picture.m_dataList[0].empty()) {
        static int volatile pictName = 0;
        libmwaw::DebugStream f2;
        f2 << "PaperTexture" << ++pictName << ".png";
        libmwaw::Debug::dumpFile(picture.m_dataList[0], f2.str().c_str());
        ascii().skipZone(entry.begin(), entry.end()-1);
        return true;
      }
    }
  }
  // else if (entry.type()=="APSF" && entry.length()>=1096) some preferences' file? probably safe to ignore...
#endif
  if (input->tell()!=entry.end()) {
    f << "#extra,";
    if (input->tell()!=entry.begin())
      ascii().addDelimiter(input->tell(),'|');
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  return true;
}

bool CorelPainterParser::readZoneHeader(CorelPainterParserInternal::ZoneHeader &zone)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  long pos=input->tell();
  if (!input->checkPosition(pos+64) || input->readULong(2)!=2) return false;
  f << "Entries(ZoneHeader):";

  int headerSize=64;
  zone.m_isMainZone=pos==0;
  zone.m_flags[0]=int(input->readULong(2)); // 2000: in painter v6 file
  if (zone.m_flags[0]&0x2000) f << "extra[pict],";
  if (zone.m_flags[0]&0xdfff) f << "fl0=" << std::hex << (zone.m_flags[0]&0xdfff) << std::dec << ",";
  int dim[2];
  for (auto &d : dim) d=int(input->readLong(2));
  zone.m_dimension=MWAWVec2i(dim[1],dim[0]);
  if (dim[0] || dim[1]) f << "dim=" << zone.m_dimension << ",";
  zone.m_flags[1]=int(input->readULong(2)); // main 2, subzone 510(bitmap?)|4110
  if (zone.m_flags[1]&1) f << "uncompressed,";
  if (zone.m_flags[1]&2) {
    f << "has[order],";
    headerSize+=256;
    if (!input->checkPosition(pos+headerSize)) return false;
  }
  // zone.m_flags[1]&0x10 local shape?
  // (zone.m_flags[1]>>16): 0 main picture, 5: floater with IPAR&FSKT, 7: floater with IPAR, 41: shape?
  if (zone.m_flags[1]&0xfffc)
    f << "fl1=" << std::hex << (zone.m_flags[1]&0xfffc) << std::dec << ",";
  int val;
  for (int i=0; i<2; ++i) { // checkme: f2=num of ordering?
    val=int(input->readULong(2));
    int const expected[] = {7,0x100};
    if (val!=expected[i]) f << "f" << i+1 << "=" << val << ",";
  }
  auto &bitmapPos=zone.m_bitmapPos;
  bitmapPos=long(input->readULong(4));
  if (bitmapPos<headerSize || !input->checkPosition(pos+bitmapPos)) return false;
  bitmapPos+=pos;
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(2));
    if (val) f << "f" << i+3 << "=" << val << ",";
  }
  val=int(input->readULong(4));
  if (val && pos+val!=bitmapPos) f << "bitmap[pos2]=" << val << ","; // use me
  zone.m_pixelByInch=int(input->readULong(2));
  if (zone.m_pixelByInch == 0xFFFF)
    f << "pixel[inch]=inherited,";
  else if (zone.m_pixelByInch)
    f << "pixel[inch]=" << zone.m_pixelByInch << ",";
  val=int(input->readULong(2));
  if (val) f << "f7=" << std::hex << val << std::dec << ",";
  auto &numTree=zone.m_numTreeNodes;
  numTree=int(input->readULong(2));
  if (numTree>=256 || pos+headerSize+4*numTree > bitmapPos) return false;
  for (auto &d : dim) d=int(input->readLong(2));
  zone.m_origin=MWAWVec2i(dim[1],dim[0]);
  if (dim[0] || dim[1])
    f << "orig=" << zone.m_origin << ",";
  long lVal=int(input->readULong(4));
  if (lVal==0x3fe66666)
    f << "main,";
  else if (lVal)
    f << "zone[type]=" << std::hex << lVal << std::dec << ",";
  for (int i=0; i<4; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  long prevPos=bitmapPos;
  for (int z=0; z<2; ++z) {
    long newPos=long(input->readULong(4));
    if (newPos<=0) continue;
    newPos+=pos;
    if (input->checkPosition(newPos) && newPos>=prevPos) {
      if (z==0)
        zone.m_rsrcDataPos=newPos;
      else
        zone.m_nextPos=newPos;
      prevPos=newPos;
    }
    else {
      MWAW_DEBUG_MSG(("CorelPainterParser::readZoneHeader: zone pos%d seems bad\n", z));
      f << "###";
    }
    f << (z==0 ? "rsrc" : "next") << "[pos]=" << std::hex << newPos << std::dec << ",";
  }
  if (zone.m_nextPos==0) zone.m_nextPos=input->size();
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "g" << i+2 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  if (zone.m_flags[1]&2) { // read the ordering
    pos=input->tell();
    f.str("");
    f << "Entries(Ordering):[";
    for (int i=0; i<256; ++i) {
      val=int(input->readULong(1));
      if (val!=i)
        f << val << ",";
      else
        f << "_,";
    }
    f << "],";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  if (numTree>0) {
    zone.m_tree=readCompressionTree(bitmapPos, numTree);
    if (!zone.m_tree) return false;
  }
  if (input->tell()<bitmapPos) {
    // before v10 flag&2000 => a zone of 40, v18 => a zone of 48
    long const extraSize=bitmapPos-input->tell();
    if ((zone.m_flags[0]&0x2000) && (extraSize==40 || extraSize==48)) {
      // 40 char, like 00070000000004640000000000000000000000000000000000000000000000000000000000000000
      // 48 char, like 00070000000004ec00000000000000000000000000000000000000000000000000000000000000000000000000000000
      ascii().addPos(input->tell());
      ascii().addNote("ZoneHeader[pict,extra]:");
    }
    else {
      MWAW_DEBUG_MSG(("CorelPainterParser::readZoneHeader: find extra data\n"));
      ascii().addPos(input->tell());
      ascii().addNote("ZoneHeader:###extra");
    }
  }
  return true;
}

bool CorelPainterParser::readPolygon(long endPos, MWAWGraphicShape &shape, MWAWGraphicStyle &style)
{
  auto input=getInput();
  MWAWGraphicStyle insideStyle;
  for (int st=0; st<2; ++st) {
    long pos=input->tell();
    if (pos+0x6c>endPos)
      return false;
    int dSz=int(input->readULong(2));
    if (dSz!=0x6c)
      return false;
    libmwaw::DebugStream f;
    f << "Entries(Polygon):";
    MWAWGraphicStyle &styl=st==0 ? style : insideStyle;
    int val;
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(2));
      if (val!=2-2*i) f << "f" << i << "=" << val << ",";
    }
    int flags=int(input->readULong(2));
    if (flags&1) f << "has[insidePoly],";
    if (flags&0xfffe) f << "fl=" << std::hex << (flags&0xfffe) << std::dec << ",";
    for (int i=0; i<2; ++i) { // f2=2, f3=2
      val=int(input->readLong(2));
      if (val!=2-i) f << "f" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) { // fl0=1, fl1=0|1
      val=int(input->readLong(1));
      if (val!=1-i) f << "fl" << i << "=" << val << ",";
    }
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    f << "box=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
    val=int(input->readLong(2)); // 0
    if (val) f << "f4=" << val << ",";
    int flags2=int(input->readULong(2));
    if (flags2&1) f << "line,";
    if (flags2&2) f << "has[surface],";
    if (flags2&4) f << "has[stroke],";
    if ((flags2&8)==0) {
      styl.m_fillRuleEvenOdd=true;
      f << "fill[evenOdd],";
    }
    if (flags2&0xfff0) f << "flags=" << std::hex << (flags2&0xfff0) << std::dec << ",";
    std::string name;
    int pSz=int(input->readULong(1));
    if (pSz>32) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readPolygon: can not read a name\n"));
      f << "##pSz=" << pSz << ",";
      pSz=0;
    }
    for (int i=0; i<pSz; ++i) {
      char c=char(input->readULong(1));
      if (!c) break;
      name+=c;
    }
    f << "name=" << name << ","; // shape name or corresponding letter
    input->seek(pos+64, librevenge::RVNG_SEEK_SET);
    int type=int(input->readLong(2));
    switch (type) {
    case 100:
      f << "spline,";
      break;
    case 101:
      f << "rect,";
      break;
    case 102:
      f << "oval,";
      break;
    default:
      MWAW_DEBUG_MSG(("CorelPainterParser::readPolygon: find unknown type=%d\n",type));
      f << "###type=" << type << ",";
      break;
    }
    val=int(input->readULong(2));
    switch ((val>>12)) {
    case 0: // butt
      break;
    case 1:
      styl.m_lineCap=MWAWGraphicStyle::C_Round;
      f << "line[cap]=round,";
      break;
    case 2:
      styl.m_lineCap=MWAWGraphicStyle::C_Square;
      f << "line[cap]=square,";
      break;
    default:
      f << "#line[cap]=" << (val>>12) << ",";
      break;
    }
    switch ((val>>8)&0xf) {
    case 0: // miter
      break;
    case 1:
      styl.m_lineJoin=MWAWGraphicStyle::J_Round;
      f << "line[join]=round,";
      break;
    case 2:
      styl.m_lineJoin=MWAWGraphicStyle::J_Bevel;
      f << "line[join]=bevel,";
      break;
    default:
      f << "#line[join]=" << ((val>>8)&0xf) << ",";
      break;
    }
    if (val&0xff)
      f << "f5=" << (val&0xff) << ",";
    MWAWColor colors[2];
    for (int i=0; i<2; ++i) {
      colors[i]=MWAWColor(uint32_t(input->readULong(4)));
      if ((i==0 && !colors[0].isWhite()) || (i==1 && !colors[1].isBlack()))
        f << "col[" << (i==0 ? "surface" : "stroke") << "]=" << colors[i] << ",";
    }
    val=int(input->readULong(4));
    styl.m_lineWidth=float(val)/float(65536);
    if (val!=0x30000) f << "stroke[w]=" << styl.m_lineWidth << ",";
    val=int(input->readULong(4));
    if (val!=0x70000) f << "mitter[limit]=" << float(val)/float(65536) << ",";
    val=int(input->readLong(2)); // 1 ou 40
    if (val!=40) f << "flatness=" << val << ",";
    int N=int(input->readULong(2));
    f << "N=" << N << ",";
    float fDim[4];
    for (auto &d : fDim) d=float(input->readLong(4))/65536.f;
    MWAWBox2f box(MWAWVec2f(fDim[0],fDim[1]),MWAWVec2f(fDim[2],fDim[3]));
    f << "box[float]=" << box << ",";
    float opacity[2];
    for (int i=0; i<2; ++i) {
      opacity[i]=float(input->readULong(2))/float(65535);
      if (opacity[i]<1.f)
        f << "opacity[" << (i==0 ? "surface" : "stroke") << "=" << opacity[i] << ",";
    }
    if (flags2&2)
      styl.setSurfaceColor(colors[0],opacity[0]);
    if (flags2&4) {
      styl.m_lineColor=colors[1];
      styl.m_lineOpacity=opacity[1];
    }
    else
      styl.m_lineWidth=0;
    if (pos+dSz+40*(N+1)+2<pos+dSz+2 || pos+dSz+40*(N+1)+2>endPos) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readPolygon: the number of point seems bad\n"));
      f << "###N";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+dSz, librevenge::RVNG_SEEK_SET);
      return false;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+dSz, librevenge::RVNG_SEEK_SET);
    if (st==0) shape=MWAWGraphicShape::path(box);

    // checkme: this must work for simple shape, for most complex
    // shape, we need to understand what ptype codes
    std::vector<MWAWVec2f> vertices;
    vertices.reserve(3*size_t(N+1));
    for (int i=0; i<=N; ++i) {
      pos=input->tell();
      f.str("");
      f << "Polygon:";
      int pType=int(input->readULong(2)); // find 0|1|11|a0
      if (pType==0) // end of poly
        f << "_,";
      else if (pType==1 || pType==0x11) // 1 normal, 11: pt0=pt1
        f << "point,";
      else // a0: line
        f << "point" << std::hex << pType << std::dec << ",";
      if (pType || i!=N) {
        val=int(input->readLong(2)); // 0
        if (val) f << "f0=" << val << ",";
        f << "pts=[";
        for (int pt=0; pt<3; ++pt) {
          float fPos[2];
          for (auto &d : fPos) d=float(input->readLong(4))/65536.f;
          if (pType==0 && pt==0 && (fPos[0]<fDim[0] || fPos[0]>fDim[2] || fPos[1]<fDim[1] || fPos[1]>fDim[3]))
            break;
          vertices.push_back(MWAWVec2f(fPos[0],fPos[1]));
          f << vertices.back() << ",";
        }
        f << "],";
        // then junk?
      }
      if ((i==N || pType==0) && !vertices.empty()) {
        // TODO use point type to recreate the path
        shape.m_path.push_back(MWAWGraphicShape::PathData('M', vertices[0]));
        size_t numPts=vertices.size()/3;
        for (size_t pt=1; pt<numPts; ++pt) {
          if (vertices[3*pt-3]==vertices[3*pt-2] && vertices[3*pt-1]==vertices[3*pt])
            shape.m_path.push_back(MWAWGraphicShape::PathData('L', vertices[3*pt]));
          else
            shape.m_path.push_back(MWAWGraphicShape::PathData('C', vertices[3*pt], vertices[3*pt-2], vertices[3*pt-1]));
        }
        if (i<=1 && (numPts==1 || (numPts==2 && vertices[0]==vertices[3]))) // line special case
          shape.m_path.push_back(MWAWGraphicShape::PathData('L', vertices[2]));
        else if (numPts>2 && vertices[0]==vertices[3*numPts-3])
          shape.m_path.push_back(MWAWGraphicShape::PathData('Z'));
        vertices.clear();
      }

      ascii().addDelimiter(input->tell(),'|');
      input->seek(pos+40, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    pos=input->tell();
    if (pos+2>endPos) {
      MWAW_DEBUG_MSG(("CorelPainterParser::readPolygon: can not find the end marker\n"));
      return false;
    }
    f.str("");
    f << "Polygon[end]:";
    val=int(input->readULong(2));
    if (val!=0) f << "f0=" << val << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    if (st==0 && (flags&1)==0)
      break;
  }

  return true;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
