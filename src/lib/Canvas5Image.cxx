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

#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stack>

#include <librevenge/librevenge.h>

#ifdef DEBUG_WITH_FILES
#  define DEBUG_CANVAS_VKFL
#endif

#ifdef DEBUG_CANVAS_VKFL
#  include "MWAWGraphicEncoder.hxx"
#endif
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWListener.hxx"
#include "MWAWParser.hxx"

#include "Canvas5Parser.hxx"

#include "Canvas5Graph.hxx"
#include "Canvas5Image.hxx"
#include "Canvas5Structure.hxx"
#include "Canvas5StyleManager.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a Canvas5Image */
namespace Canvas5ImageInternal
{
//! Internal: the stroke style of a Canvas5Image
struct Stroke {
  //! constructor
  Stroke()
    : m_type(1)
    , m_penPos(-1)
    , m_dashPos(-1)
  {
    for (auto &id : m_arrowPos) id=-1;
  }
  //! the type
  unsigned m_type;
  //! the pen id
  long m_penPos;
  //! the dash id
  long m_dashPos;
  //! the arrow id (beg/end)
  long m_arrowPos[2];
};

////////////////////////////////////////
//! Internal: the internal shape of a Canvas5Image
struct VKFLShape {
  //! constructor
  VKFLShape()
    : m_type(-1)
    , m_subType(0)
    , m_box()
    , m_idToDataPos()
    , m_style()

    , m_vertices()
    , m_bitmap()
    , m_bitmapColor(MWAWColor::white())

    , m_special()
    , m_macoImage()
  {
    for (auto &v : m_locals) v=0;
  }
  static std::string getTypeName(int type)
  {
    switch (type) {
    case -1:
      return "";
    case 1:
      return "poly";
    case 2:
      return "spline";
    case 6:
      return "rect";
    case 7:
      return "circle";
    case 8:
      return "rectOval";
    case 9:
      return "line";
    case 10:
      return "arc";
    case 11:
      return "group";
    case 12: // a group for ???
      return "group1";
    case 14:
      return "special";
    default:
      return Canvas5Structure::getString(unsigned(type));
    }
  }
  //! the type
  int m_type;
  //! the sub type
  unsigned m_subType;
  //! the dimension
  MWAWBox2f m_box;
  //! the map id(type) to data pos in the main zone
  std::map<int, long> m_idToDataPos;
  //! the graphic style
  MWAWGraphicStyle m_style;

  //! the vertices: spline, poly, ...
  std::vector<MWAWVec2f> m_vertices;
  //! the local values : arc=>angles, rectOval=>oval size
  float m_locals[2];
  //! the bitmap
  MWAWEmbeddedObject m_bitmap;
  //! the bitmap color
  MWAWColor m_bitmapColor;
  //! a graph pseudo box: special
  std::shared_ptr<Canvas5GraphInternal::PseudoShape> m_special;
  //! a macro image : special
  std::shared_ptr<VKFLImage> m_macoImage;
};

//! Internal: the internal image of a Canvas5Image
struct VKFLImage {
  //! constructor
  VKFLImage()
    : m_data()
    , m_shapes()

    , m_posToTypesMap()

    , m_posToArrowMap()
    , m_posToColorMap()
    , m_posToDashMap()
    , m_posToMatrixMap()
    , m_posToPenMap()
    , m_posToStrokeMap()
  {
    for (auto &box : m_boxes) box=MWAWBox2f();
    for (auto &mat : m_matrices) mat=std::array<double,9>();
  }
  //! the data entry
  MWAWEntry m_data[2];
  //! the list of shape
  std::vector<VKFLShape> m_shapes;
  //! the dimensions
  MWAWBox2f m_boxes[2];
  //! the transformations
  std::array<double,9> m_matrices[2];

  //! the map pos to type and sub type
  std::map<long, std::pair<unsigned, unsigned> > m_posToTypesMap;

  //! the position to arrow map
  std::map<long, MWAWGraphicStyle::Arrow> m_posToArrowMap;
  //! the position to color map
  std::map<long, std::shared_ptr<Canvas5StyleManagerInternal::ColorStyle> > m_posToColorMap;
  //! the position to dash map
  std::map<long, std::vector<float> > m_posToDashMap;
  //! the position to matrix map
  std::map<long, std::array<double,9> > m_posToMatrixMap;
  //! the position to pen map
  std::map<long, std::shared_ptr<Canvas5StyleManagerInternal::PenStyle> > m_posToPenMap;
  //! the position to stroke map
  std::map<long, Stroke> m_posToStrokeMap;
};

////////////////////////////////////////
//! Internal: the state of a Canvas5Image
struct State {
  //! constructor
  State()
    : m_idToObject()
    , m_idToGIF()
    , m_idToMACO()
    , m_idToQkTm()
  {
  }

  //! the map id to bitmap
  std::map<int, MWAWEmbeddedObject> m_idToObject;
  //! the map id to git
  std::map<int, std::shared_ptr<VKFLImage> > m_idToGIF;
  //! the map id to maco
  std::map<std::vector<unsigned>, std::shared_ptr<VKFLImage> > m_idToMACO;
  //! the map id to quicktime
  std::map<int, librevenge::RVNGBinaryData> m_idToQkTm;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
Canvas5Image::Canvas5Image(Canvas5Parser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new Canvas5ImageInternal::State)
  , m_mainParser(&parser)
{
}

Canvas5Image::~Canvas5Image()
{
}

int Canvas5Image::version() const
{
  return m_parserState->m_version;
}

std::shared_ptr<Canvas5StyleManager> Canvas5Image::getStyleManager() const
{
  return m_mainParser->m_styleManager;
}

bool Canvas5Image::getBitmap(int bitmapId, MWAWEmbeddedObject &object)
{
  auto const &pIt=m_state->m_idToObject.find(bitmapId);
  if (pIt==m_state->m_idToObject.end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::getBitmap: can not find bitmap %d\n", bitmapId));
    return false;
  }
  object=pIt->second;
  return true;
}

std::shared_ptr<Canvas5ImageInternal::VKFLImage> Canvas5Image::getGIF(int gifId)
{
  auto const &pIt=m_state->m_idToGIF.find(gifId);
  if (pIt==m_state->m_idToGIF.end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::getGIF: can not find GIF %d\n", gifId));
    return nullptr;
  }
  return pIt->second;
}

std::shared_ptr<Canvas5ImageInternal::VKFLImage> Canvas5Image::getMACO(std::vector<unsigned> const &macoId)
{
  auto const &pIt=m_state->m_idToMACO.find(macoId);
  if (pIt==m_state->m_idToMACO.end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::getMACO: can not find a MACO picture\n"));
    return nullptr;
  }
  return pIt->second;
}

bool Canvas5Image::getQuickTime(int quicktimeId, MWAWEmbeddedObject &object)
{
  object=MWAWEmbeddedObject();
  auto const &qIt=m_state->m_idToQkTm.find(quicktimeId);
  if (qIt==m_state->m_idToQkTm.end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::getBitmap: can not find quicktime %d\n", quicktimeId));
    return false;
  }
  object.add(qIt->second, "video/quicktime");
  return true;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// bitmap
////////////////////////////////////////////////////////////

bool Canvas5Image::readImages(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;
  auto input=stream->input();

  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readImages: the zone is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream->ascii();
  ascFile.addPos(pos);
  ascFile.addNote("Entries(BitmDef):");
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);

  std::vector<bool> defined;
  if (!m_mainParser->readDefined(*stream, defined, "BitmDef"))
    return false;

  // find list of 2bb73XXX, always multiple of 4 some auto ref ?
  std::vector<unsigned long> unknowns;
  if (!m_mainParser->readExtendedHeader(stream, 4, "BitmDef",
  [&unknowns](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &, std::string const &) {
  auto lInput=lStream->input();
    unknowns.push_back(lInput->readULong(4));
  }))
  return false;

  size_t w=0;
  int const vers=version();
  for (size_t i=0; i<defined.size(); ++i) {
    if (!defined[i]) continue;
    if (w>=unknowns.size())
      break;
    if (unknowns[w++]==0) continue;
    MWAWEmbeddedObject object;
    if (!Canvas5Structure::readBitmapDAD58Bim(*stream, vers, object))
      return false;
    m_state->m_idToObject[int(i+1)]=object;
  }
  return true;
}

bool Canvas5Image::readImages9(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;

  auto input=stream->input();
  auto &ascFile=stream->ascii();
  int const vers=version();
  long pos=input->tell();
  if (!input->checkPosition(pos+8)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readImages9: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Image):";
  int val=int(input->readLong(4));
  if (val!=3)
    f << "f0=" << val << ",";
  int N=int(input->readLong(4));
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Image-B" << i+1 << ":";
    std::string name;
    int type;
    if (!m_mainParser->getTAG9(*stream, name, type) || type!=0) {
      MWAW_DEBUG_MSG(("Canvas5Image::readImages9: can not find the image tag\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    f << "name=" << name << ",";
    if (!input->checkPosition(input->tell()+4)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readImages9: the zone seems too short\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    int id=int(input->readLong(4));
    f << "id=" << id << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    MWAWEmbeddedObject object;
    if (!Canvas5Structure::readBitmapDAD58Bim(*stream, vers, object))
      return false;
    if (m_state->m_idToObject.find(i+1) != m_state->m_idToObject.end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::readImages9: id=%d already exists\n", i+1));
    }
    else
      m_state->m_idToObject[i+1]=object;
    if (!m_mainParser->checkTAG9(*stream, name, 1)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readImages9: can not find the image tag\n"));
      ascFile.addPos(input->tell());
      ascFile.addNote("Image:###");
      return false;
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
// macros
////////////////////////////////////////////////////////////
bool Canvas5Image::readMacroIndent(Canvas5Structure::Stream &stream, std::vector<unsigned> &id, std::string &extra)
{
  id.clear();

  auto input=stream.input();
  long pos=input ? input->tell() : 0;

  if (!input || !input->checkPosition(pos+20)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readMacroIndent: can not read first MACO value\n"));
    extra="###";
    return false;
  }

  std::stringstream s;
  for (int k=0; k<8; ++k) { // f0,f1,f2: a date, f3,f4,f5: hour?
    unsigned val=unsigned(input->readULong(2));
    unsigned const expected[]= {1998,10,5,14,0,0,2,1100};

    if (k>=0 && k<=6)
      id.push_back(val);
    if (val==expected[k]) continue;
    if (k==7)
      s << "fl=" << std::hex << val << std::dec << ",";
    else
      s << "f" << k << "=" << val << ",";
  }
  int val=int(input->readLong(4));
  if (val)
    s << "id=" << val << ",";
  extra=s.str();
  return true;
}

bool Canvas5Image::readMACORsrc(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readMACORsrc: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input ? input->tell() : 0;
  int const vers=version();
  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Macros):";
  if (!input || !input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readMACORsrc: can not read first MACO value\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int val=int(input->readULong(4));
  if (val!=0x77cc) f << "f0=" << std::hex << val << std::dec << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (input->isEnd())
    return true;

  // 32: name, dim, last part of DataShap Maco, another dim
  std::map<int, std::vector<unsigned> > idToUniqueIdMap;
  std::set<std::vector<unsigned> > uniqueIdSet;
  if (!m_mainParser->readExtendedHeader(stream, vers>=9 ? 0x80 : 0x58, "Macros",
  [this, vers, &idToUniqueIdMap, &uniqueIdSet](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    long lPos=lInput->tell();
    libmwaw::DebugStream lF;
    std::string name;
    for (int i=0; i<32; ++i) {
      char c=char(lInput->readULong(1));
      if (!c)
        break;
      name+=c;
    }
    lF << name << ",";
    lInput->seek(lPos+32, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<(vers<9 ? 2 : 4); ++i) { // checkme is g0 related to endian ordering?
      int lVal=int(lInput->readLong(2));
      if (lVal)
        lF << "g" << i << "=" << lVal << ",";
    }
    if (vers>=9) {
      double dim[4];
      bool isNan;
      for (auto &d : dim) {
        long actPos=lInput->tell();
        if (m_mainParser->readDouble(*lStream, d, isNan)) continue;
        d=0;
        lF << "###";
        lInput->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      lF << "box=" << MWAWBox2f(MWAWVec2f((float) dim[0], (float) dim[1]),
                                MWAWVec2f((float) dim[2], (float) dim[3])) << ",";
      std::vector<unsigned> mId;
      std::string extra;

      if (!readMacroIndent(*lStream, mId, extra) || uniqueIdSet.find(mId)!=uniqueIdSet.end()) {
        MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: oops, find multiple unique id\n"));
        lF << "###";
        lInput->seek(lPos+32+8+32+20, librevenge::RVNG_SEEK_SET);
      }
      else {
        idToUniqueIdMap[item.m_id]=mId;
        uniqueIdSet.insert(mId);
      }
      int lVal=int(lInput->readLong(4));
      if (lVal)
        lF << "g4=" << lVal << ",";
      for (auto &d : dim) {
        long actPos=lInput->tell();
        if (m_mainParser->readDouble(*lStream, d, isNan)) continue;
        d=0;
        lF << "###";
        lInput->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      lF << "box2=" << MWAWBox2f(MWAWVec2f((float) dim[0], (float) dim[1]),
                                 MWAWVec2f((float) dim[2], (float) dim[3])) << ",";
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote(lF.str().c_str());
      return ;
    }
    float fDim[4];
    for (auto &d : fDim) d=float(lInput->readULong(4))/65536.f;
    lF << "box=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]), MWAWVec2f(fDim[2], fDim[3])) << ",";

    std::vector<unsigned> mId;
    std::string extra;

    if (!readMacroIndent(*lStream, mId, extra) || uniqueIdSet.find(mId)!=uniqueIdSet.end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: oops, find multiple unique id\n"));
      lF << "###";
      lInput->seek(lPos+32+20+20, librevenge::RVNG_SEEK_SET);
    }
    else {
      idToUniqueIdMap[item.m_id]=mId;
      uniqueIdSet.insert(mId);
    }
    for (auto &d : fDim) d=float(lInput->readULong(4))/65536.f;
    lF << "box2=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]), MWAWVec2f(fDim[2], fDim[3])) << ",";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  })) {
    return false;
  }
  if (input->isEnd())
    return true;;
  if (!m_mainParser->readIndexMap(stream, "Macros",
  [this, &idToUniqueIdMap](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
  if (!readVKFL(lStream, item.m_length, image)) return;
    auto it=idToUniqueIdMap.find(item.m_id);
    if (it==idToUniqueIdMap.end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: oops, can not find an unique id for %d\n", item.m_id));
    }
    else
      m_state->m_idToMACO[it->second]=image;
  })) { // vkfl
    MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: can not read the first data value\n"));
    return false;
  }
  if (input->isEnd())
    return true;
  if (!m_mainParser->readUsed(*stream, "Macros")) {
    MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: can not read the used value\n"));
    return false;
  }
  pos=input->tell();
  f.str("");
  f << "Macros-F:";
  int N;
  if (!m_mainParser->readDataHeader(*stream, 0x14,N)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readMACORsrc: can not read the last data value\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  for (int j=0; j<N; ++j) {
    pos=input->tell();
    f.str("");
    f << "Macros-F" << j << ":";
    std::vector<unsigned> mId;
    std::string extra;
    readMacroIndent(*stream, mId, extra);
    f << extra << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+0x14, librevenge::RVNG_SEEK_SET);
  }

  return true;
}


////////////////////////////////////////////////////////////
//
// VKFL
//
////////////////////////////////////////////////////////////

bool Canvas5Image::readAGIFRsrc(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "RsrcAGIF:";

  if (!input->checkPosition(pos+56)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: the zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  int val=int(input->readLong(4));
  if (val!=2)
    f << "f0=" << val << ",";
  int N=int(input->readULong(4));
  f << "N=" << N << ",";
  if (N<1) {
    MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: the N value seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int z=0; z<N; ++z) {
    pos=input->tell();
    f.str("");
    f << "RsrcAGIF" << z << ":";

    if (!input->checkPosition(pos+4)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: the zone %d seems too short\n", z));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    val=int(input->readLong(4));
    if (val==0) {
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    if (val!=1 || !input->checkPosition(pos+20)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: find unknown identifier for the sub zone %d\n", z));
      f << "###id=" << val << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    int N0=1;
    for (int i=0; i<4; ++i) {
      val=int(input->readLong(4));
      if (val==1) continue;
      if (i==1) {
        N0=val;
        f << "N[subZ]=" << N0 << ",";
      }
      else
        f << "f" << i << "=" << val << ",";
    }

    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    for (int s=0; s<N0; ++s) {
      pos=input->tell();
      f.str("");
      f << "RsrcAGIF" << z << "-" << s << ":";
      if (!input->checkPosition(pos+24)) {
        MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: the sub zone %d-%d seems too short\n", z, s));
        f << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return false;
      }
      val=int(input->readULong(4));
      if (val!=0)
        f << "unkn=" << std::hex << val << std::dec << ",";
      val=int(input->readULong(4));
      long len=long(input->readULong(4));
      if (val!=0x3e23d70a || pos+24+len<pos+24 || !input->checkPosition(pos+24+len)) {
        MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: the sub zone %d-%d seems bad\n", z, s));
        f << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return false;
      }
      if (len) {
        std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
        if (!readVKFL(stream, len, image)) {
          f  << "###";
          input->seek(pos+12+len, librevenge::RVNG_SEEK_SET);
        }
        else
          m_state->m_idToGIF[s]=image;
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());

        pos=input->tell();
        f.str("");
        f << "RsrcAGIF" << z << "-" << s << "[B]:";
      }
      for (int i=0; i<3; ++i) { // g2=1 means continue
        val=int(input->readLong(4));
        int const expected[]= {0,1,0};
        if (val!=expected[i])
          f << "g" << i << "=" << val << ",";
      }
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
  }
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("Canvas5Image::readAGIFRsrc: find extra data\n"));
    ascFile.addPos(input->tell());
    ascFile.addNote("RsrcAGIF-end:###extra");
  }
  return true;
}

bool Canvas5Image::readQkTmRsrc(Canvas5Structure::Stream &stream)
{
  auto input=stream.input();
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "RsrcQkTm:";

  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readQkTmRsrc: the zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  int N=int(input->readULong(4));
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int z=0; z<N; ++z) {
    pos=input->tell();
    f.str("");
    f << "RsrcQkTm-QK" << z+1 << ":";
    if (!input->checkPosition(pos+44)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readQkTmRsrc: the %d zone seems too short\n", z));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);
    long len=input->readLong(4);
    if (len<0 || pos+44+len<pos+44 || !input->checkPosition(pos+44+len)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readQkTmRsrc: the %d zone len seems bad\n", z));
      f << "###len=" << len << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    if (len) {
      librevenge::RVNGBinaryData movie;
      if (!input->readDataBlock(len, movie)) {
        MWAW_DEBUG_MSG(("Canvas5Image::readQkTmRsrc: oops can not retrieve the %d movie\n", z));
        f << "###";
      }
      else {
        m_state->m_idToQkTm[z+1]=movie;
#ifdef DEBUG_WITH_FILES
        std::stringstream s;
        static int index=0;
        s << "movie" << ++index << ".mov";
        libmwaw::Debug::dumpFile(movie, s.str().c_str());
#endif
      }
      ascFile.skipZone(pos+44, pos+44+len-1);
    }
    input->seek(pos+44+len, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool Canvas5Image::getArrow(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWGraphicStyle::Arrow &arrow) const
{
  arrow=MWAWGraphicStyle::Arrow::plain();
  if (!image) {
    MWAW_DEBUG_MSG(("Canvas5Image::getArrow: can not find the image\n"));
    return false;
  }
  std::vector<int> typeList;
  for (auto const &shape : image->m_shapes) {
    if (shape.m_type>=0 && shape.m_type!=11)
      typeList.push_back(shape.m_type);
  }
  if (typeList.size()==1) {
    // TODO: get the real shape and extract the path
    switch (typeList[0]) {
    case 1:
    case 2:
      arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,30)), "M1013 1491l118 89-567-1580-564 1580 114-85 136-68 148-46 161-17 161 13 153 46z", false);
      break;
    case 7:
      arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1131)), "M462 1118l-102-29-102-51-93-72-72-93-51-102-29-102-13-105 13-102 29-106 51-102 72-89 93-72 102-50 102-34 106-9 101 9 106 34 98 50 93 72 72 89 51 102 29 106 13 102-13 105-29 102-51 102-72 93-93 72-98 51-106 29-101 13z", false);
      break;
    case 10:
      arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,30)), "m10 0l-10 30h20z", false);
      break;
    default:
      MWAW_DEBUG_MSG(("Canvas5Image::readArrow: find unexpected arrow with type %d\n", typeList[0]));
      break;
    }
  }
  else if (typeList.size()==2 && typeList[0]==1 && typeList[1]==1)
    arrow=MWAWGraphicStyle::Arrow(10, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(40,35)), "m20 0l-20 0 l0 4 l20 0 l-10 30 l20 0 l-10 -30 l20 0 l0 -4z", false);
  else if (typeList.size()==2 && typeList[0]==2 && typeList[1]==2)
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,30)), "m0 3 h-20 v-3 h40 v3 h-20 l-10 30 h20z", false);
  else if (typeList.size()==3 && typeList[0]==10)
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,33)), "m10 0l-10 30 l10 3 l10 -3z", false);
  else {
    MWAW_DEBUG_MSG(("Canvas5Image::readArrow: find unexpected arrow with size=%d\n", int(typeList.size())));
  }
  return true;
}

bool Canvas5Image::getTexture(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWEmbeddedObject &texture, MWAWVec2i &textureDim, MWAWColor &averageColor) const
{
  if (!image) {
    MWAW_DEBUG_MSG(("Canvas5Image::getTexture: can not find the image\n"));
    return false;
  }
  bool bitmapFound=false;
  for (auto const &shape : image->m_shapes) {
    if (shape.m_type!=14 || shape.m_bitmap.isEmpty()) continue;
    if (bitmapFound) {
      MWAW_DEBUG_MSG(("Canvas5Image::getTexture: found multiple textures, return the first one\n"));
      return true;
    }
    bitmapFound=true;
    texture=shape.m_bitmap;
    textureDim=MWAWVec2i(shape.m_box.size());
    averageColor=shape.m_bitmapColor;
  }
  if (bitmapFound)
    return true;
  MWAW_DEBUG_MSG(("Canvas5Image::getTexture: can not find any texture\n"));
  return false;
}

bool Canvas5Image::readVKFL(std::shared_ptr<Canvas5Structure::Stream> stream, long len, std::shared_ptr<Canvas5ImageInternal::VKFLImage> &image)
{
  image.reset();
  if (!stream)
    return false;
  if (len==0)
    return true;
  auto input=stream->input();
  int const vers=version();
  long pos=input->tell();
  long endPos=pos+len;
  int const headerLen=vers<9 ? 180 : 288;
  if (len<headerLen || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: the zone seems too short\n"));
    return false;
  }

  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;

  f << "Entries(Vkfl):";
  int val=int(input->readLong(4));
  if (val!=256)
    f << "f0=" << val << ",";
  auto tBegin=long(input->readULong(4));
  auto tLen=long(input->readULong(4));
  f << "pos=" << tBegin << "<->" << tBegin+tLen << ",";
  if (tBegin+tLen<0 || tLen<36 || tBegin<headerLen || tBegin+tLen>len) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: can not read the data length length\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  if (tBegin+tLen<len) {
    ascFile.addPos(pos+tBegin+tLen);
    ascFile.addNote("Vkfl-[end]:###");
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: find extra data\n"));
  }

  image=std::make_shared<Canvas5ImageInternal::VKFLImage>();
  if (vers>=9)
    input->seek(4, librevenge::RVNG_SEEK_CUR);
  float dim[4];
  for (auto &d : dim) d=float(m_mainParser->readDouble(*stream,vers<9 ? 4 : 8));
  if (vers<9)
    image->m_boxes[0]=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
  else
    image->m_boxes[0]=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  f << "dim=" << image->m_boxes[0] << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(4));
    if (val!=1-i)
      f << "f" << i+2 << "=" << val << ",";
  }
  for (auto &d : dim) d=float(m_mainParser->readDouble(*stream,vers<9 ? 4 : 8));
  if (vers<9)
    image->m_boxes[1]=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
  else
    image->m_boxes[1]=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  if (image->m_boxes[0]!=image->m_boxes[1])
    f << "dim2=" << image->m_boxes[1] << ",";
  for (int st=0; st<2; ++st) {
    f << "mat" << st << "=[";
    for (auto &d : image->m_matrices[st]) {
      d=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
      f << d << ",";
    }
    f << "],";
  }
  for (int j=0; j<3; ++j) { // g1=54|6c
    val=int(input->readLong(4));
    if (val!=-1)
      f << "g" << j << "=" << val << ",";
  }
  int dDim[2]; // g1+48, g1+24
  for (auto &d : dDim) d=int(input->readLong(4));
  if (vers<9)
    f << "dim3=" << MWAWVec2i(dDim[1],dDim[0]) << ",";
  else
    f << "dim3=" << MWAWVec2i(dDim[0],dDim[1]) << ",";

  long firstBlockDecal=0;
  for (int j=0; j<5; ++j) {
    val=int(input->readLong(4));
    int const expected[]= {-1,-1,1,0,0};
    if (val==expected[j]) continue;
    if (j==3)
      firstBlockDecal=val;
    f << "g" << j+3 << "=" << val << ",";
  }

  f << "entries=[";
  for (auto &entry : image->m_data) {
    entry.setBegin(input->readLong(4)+pos);
    entry.setLength(input->readLong(4));
    f << std::hex << entry.begin() << ":" << entry.end() << std::dec << ",";
    if (entry.begin()<pos || entry.end()>pos+tBegin+tLen) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: unexpected subs size for an effect\n"));
      f << "###";
      entry.setLength(0);
    }
  }
  f << ",";

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::set<long> seen;
  long begPos=image->m_data[1].begin();
  if (image->m_data[1].valid() && firstBlockDecal>=0 && firstBlockDecal<=image->m_data[1].length()) {
    input->seek(begPos+firstBlockDecal, librevenge::RVNG_SEEK_SET);
    while (input->tell()<image->m_data[1].end()) {
      if (input->tell()<begPos || seen.find(input->tell())!=seen.end()) {
        MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: oops find a loop\n"));
        break;
      }
      seen.insert(input->tell());
      if (!readVKFLShape(stream, *image))
        break;
    }
  }
  else if (firstBlockDecal>=0) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: first block seems bad\n"));
  }

  if (!image->m_data[0].valid()) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: can not find any data0 zoone\n"));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }

  std::stack<std::pair<long,int> > dataStack;
  for (auto const &shape : image->m_shapes) {
    for (auto const &it : shape.m_idToDataPos) {
      pos=begPos+it.second;
      if (seen.find(pos)!=seen.end()) // already parsed or pb
        continue;
      seen.insert(pos);
      dataStack.push(std::make_pair(pos, it.first));
    }
  }

  while (!dataStack.empty()) {
    auto posId=dataStack.top();
    dataStack.pop();

    pos=posId.first;
    if (pos<begPos || pos+24>image->m_data[1].end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: can not find sub zone0[%lx]\n", posId.first));
      continue;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    if (posId.second>=0)
      f << "Vkfl-VK" << std::hex << pos-begPos << std::dec << "A[" << posId.second << "]:";
    else
      f << "Vkfl-VK" << std::hex << pos-begPos << std::dec << "A:";
    unsigned dataType=unsigned(input->readULong(4));
    auto unknPos=input->readULong(4);
    if (unknPos!=0xFFFFFFFF) {
      long childPos=begPos+long(unknPos);
      f << "unkn=VK" << std::hex << long(unknPos) << std::dec << ",";
      if (seen.find(childPos)==seen.end()) {
        seen.insert(childPos);
        dataStack.push(std::make_pair(childPos,-1));
      }
    }
    long dataLength[2];
    dataLength[0]=input->readLong(4);
    val=int(input->readULong(4));
    if (val) f << "f0=" << val << ",";
    unsigned dataSubType=unsigned(input->readULong(4));
    dataLength[1]=input->readLong(4);
    image->m_posToTypesMap[pos-begPos]=std::make_pair(dataType, dataSubType);
    if (dataLength[0]) {
      MWAWEntry data;
      data.setId(int(dataType));
      data.setBegin(image->m_data[0].begin()+dataLength[1]);
      data.setLength(dataLength[0]);
      f << "data=[" << Canvas5Structure::getString(dataType) << "-" << Canvas5Structure::getString(dataSubType) << ","
        << std::hex << data.begin() << "->" << data.end() << std::dec << "],";
      std::vector<long> childFieldPos;
      input->pushLimit(data.end());
      readVKFLShapeOtherData(stream, *image, std::make_tuple(data, dataSubType, pos-begPos),
                             childFieldPos, posId.second);
      input->popLimit();
      for (auto cPos : childFieldPos) {
        if (cPos<0) continue;
        long childPos=begPos+cPos;
        if (seen.find(childPos)==seen.end()) {
          seen.insert(childPos);
          dataStack.push(std::make_pair(childPos,-1));
        }
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+24, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos+24);
    ascFile.addNote("_");
  }

#ifdef DEBUG_CANVAS_VKFL
  MWAWGraphicEncoder graphicEncoder;
  MWAWBox2f pictBox=image->m_boxes[0]; // 0: image size, 1: image + translation
  pictBox.setMax(pictBox[1]+MWAWVec2f(100,100));
  auto graphicListener=std::make_shared<MWAWGraphicListener>(*m_parserState, pictBox, &graphicEncoder);
  graphicListener->startDocument();
  MWAWTransformation transf;
  auto const &mat=image->m_matrices[0];
  if (mat[2]<-1e-3 || mat[2]>1e-3 || mat[5]<-1e-3 || mat[5]>1e-3) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFL: image matrix will be ignored\n"));
  }
  else
    transf=MWAWTransformation(MWAWVec3f((float)mat[0],(float)mat[3],(float)mat[6]), MWAWVec3f((float)mat[1],(float)mat[4],(float)mat[7]));

  send(image, graphicListener, pictBox, transf);
  graphicListener->endDocument();
  MWAWEmbeddedObject picture;
  if (graphicEncoder.getBinaryResult(picture) && !picture.m_dataList.empty()) {
    static int vkflId=0;
    std::stringstream s;
    s << "Vkfl" << ++vkflId << ".odg";
    libmwaw::Debug::dumpFile(picture.m_dataList[0], s.str().c_str());
  }
#endif

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool Canvas5Image::readVKFLShape(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image)
{
  if (!stream || !image.m_data[1].valid()) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShape: the image data1 is not valid\n"));
    return false;
  }

  auto input=stream->input();
  int const vers=version();
  long pos=input->tell();
  long begPos=image.m_data[1].begin();
  long endPos=image.m_data[1].end();
  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;

  int id=1+int(image.m_shapes.size());
  f << "Vkfl-s" << id << ":";
  int const headerLen=vers<9 ? 44 : 64;
  if (pos+headerLen>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShape: the zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  image.m_shapes.push_back(Canvas5ImageInternal::VKFLShape());
  auto &shape=image.m_shapes.back();
  shape.m_type=int(input->readLong(4)); // find 1-14
  f << "type=" << Canvas5ImageInternal::VKFLShape::getTypeName(shape.m_type) << ",";
  if (vers>=9)
    input->seek(4, librevenge::RVNG_SEEK_CUR);
  float dim[4];
  for (auto &d : dim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
  if (vers<9)
    shape.m_box=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
  else
    shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  f << "dim=" << shape.m_box << ",";
  int val=int(input->readULong(4)); // 0|8000
  if (val) f << "fl0=" << std::hex << val << std::dec << ",";
  auto fl=input->readULong(4); // [347]|[01]ff
  if (fl)  f << "fl1=" << std::hex << fl << std::dec << ",";
  auto decal=input->readULong(4);
  long nextPos=decal==0xFFFFFFFF ? endPos : begPos+long(decal);
  if (nextPos<=begPos || (nextPos>=pos && nextPos<pos+44) || nextPos>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShape: the zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  long dataLength[2];
  dataLength[0]=input->readLong(4);
  shape.m_subType=unsigned(input->readULong(4));
  dataLength[1]=input->readLong(4);
  if (dataLength[0])
    f << "data=[" << Canvas5Structure::getString(shape.m_subType) << "," << std::hex << image.m_data[0].begin()+dataLength[1] << "->" << image.m_data[0].begin()+dataLength[0]+dataLength[1] << std::dec << "],";
  else if (shape.m_subType && shape.m_type==11)
    f << "N=" << shape.m_subType << ",";
  else if (shape.m_subType)
    f << "unkn=" << Canvas5Structure::getString(shape.m_subType) << ",";

  unsigned dec=1;
  for (int i=0; i<12; ++i, dec*=2) {
    if ((fl&dec)==0) continue;
    if (input->tell()+4>nextPos) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShape: the zone seems too short\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      break;
    }
    long decalData=long(input->readLong(4));
    if (decalData>=0) {
      shape.m_idToDataPos[i]=decalData;
      char const *wh[]= {"surfColor", "lineColor", "stroke", "matrix", "matrix1",
                         "matrix2", nullptr, nullptr, nullptr, "name"
                        };
      if (i<10 && wh[i])
        f << "beg[" << wh[i] << "]=VK" << std::hex << decalData << std::dec << ",";
      else
        f << "beg[t" << i << "]=VK" << std::hex << decalData << std::dec << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(input->tell());
  ascFile.addNote("_");

  if (dataLength[0]) {
    MWAWEntry data;
    data.setBegin(image.m_data[0].begin()+dataLength[1]);
    data.setLength(dataLength[0]);
    readVKFLShapeMainData(stream, image, shape, data);
  }

  input->seek(nextPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool Canvas5Image::readVKFLShapeMainData(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image, Canvas5ImageInternal::VKFLShape &shape, MWAWEntry const &data)
{
  if (!data.valid() || !stream)
    return true;

  auto input=stream->input();
  int const vers=version();
  if (!input || !input->checkPosition(data.end()) || data.begin()<image.m_data[0].begin() || data.end()>image.m_data[0].end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: the entry seems bad\n"));
    return false;
  }

  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "Vkfl-s" << image.m_shapes.size() << "M[" << Canvas5ImageInternal::VKFLShape::getTypeName(shape.m_type) << ":" << Canvas5Structure::getString(shape.m_subType) << "]:";
  input->seek(data.begin(), librevenge::RVNG_SEEK_SET);

  int val;
  switch (shape.m_type) {
  case 1: // poly?
  case 2: { // unsure a list of 16/20 points, many are similar
    if (data.length()<8) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData[poly]: the zone seems too short\n"));
      break;
    }
    int nPts;
    if (vers<9) {
      val=int(input->readULong(4)); // checkme: either N or a number less than N
      if (val)
        f << "f0=" << val << ",";
      nPts=int(input->readULong(4));
    }
    else
      nPts=m_mainParser->readInteger(*stream, 8);
    int const fieldSize=vers<9 ? 8 : 16;
    f << "N=" << nPts << ",";
    if (nPts<0 || 8+fieldSize*nPts<0 || nPts>(data.length()-8)/fieldSize || 8+fieldSize*nPts>data.length()) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData[poly]: can not read the number of points\n"));
      break;
    }
    f << "pts=[";
    shape.m_vertices.resize(size_t(nPts));
    for (auto &pt : shape.m_vertices) {
      float coord[2];
      for (auto &d : coord) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      pt=MWAWVec2f(coord[1], coord[0]);
      f << pt << ",";
    }
    f << "],";
    break;
  }
  // 6,7: rect, circle: no data
  case 8:
    if (data.length()!=(vers<9 ? 8 : 16)) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData[rectOval]: the zone seems too short\n"));
      break;
    }
    f << "oval[sz]=[";
    for (auto &v : shape.m_locals)
      v=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
    f << MWAWVec2f(shape.m_locals[1],shape.m_locals[0]) << ",";
    break;
  case 9: // line
    if (data.length()<(vers<9 ? 16 : 32)) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData[type9]: the zone seems too short\n"));
      break;
    }
    f << "pts=[";
    shape.m_vertices.resize(2);
    for (auto &pt : shape.m_vertices) {
      float coord[2];
      for (auto &d : coord) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers<9)
        pt=MWAWVec2f(coord[1], coord[0]);
      else
        pt=MWAWVec2f(coord[0], coord[1]);
      f << pt << ",";
    }
    f << "],";
    break;
  case 10:
    if (data.length()!=(vers<9 ? 8 : 16)) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData[arc]: the zone seems too short\n"));
      break;
    }
    for (auto &v : shape.m_locals) {
      v=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers>=9) v*=float(180/M_PI);
    }
    f << "angles=[" << shape.m_locals[0] << "," << shape.m_locals[1] << "],";
    break;
  // 11, 12:  no dat
  case 14: { // special
    switch (shape.m_subType) {
    case 0x706f626a: // special a pobj which contains a bitmap
      if (!Canvas5Structure::readBitmap(*stream, vers, shape.m_bitmap, &shape.m_bitmapColor)) {
        f << "###";
        MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: can not retrieve the bitmap\n"));
      }
      if (input->tell()!=data.end()) {
        ascFile.addPos(input->tell());
        ascFile.addNote("Vkfl-end");
      }
      break;
    case 0x8F909d96: { // special a bitmap in a mac/windows files
      bool readInverted=input->readInverted();
      input->setReadInverted(!readInverted);
      if (!Canvas5Structure::readBitmap(*stream, vers, shape.m_bitmap, &shape.m_bitmapColor)) {
        f << "###";
        MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: can not retrieve the bitmap\n"));
      }
      input->setReadInverted(readInverted);
      if (input->tell()!=data.end()) {
        ascFile.addPos(input->tell());
        ascFile.addNote("Vkfl-end");
      }
      break;
    }
    case 0x4d41434f: { // MACO
      if (data.length()<(vers<9 ? 96 : 116)) {
        f << "###";
        MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: can not retrieve the MACO vkfl\n"));
        break;
      }
      // see also Canvas5Image::readMACORsrc
      f << "unkn=" << std::hex << input->readULong(4) << std::dec << ",";
      std::string name;
      for (int i=0; i<32; ++i) {
        char c=char(input->readULong(1));
        if (!c)
          break;
        name+=c;
      }
      f << name << ",";
      input->seek(data.begin()+4+32, librevenge::RVNG_SEEK_SET);
      for (int k=0; k<2; ++k) {
        val=int(input->readLong(2));
        if (val!=(k==0 ? 256 : 0))
          f << "g" << k << "=" << val << ",";
      }
      if (vers>=9)
        input->seek(4, librevenge::RVNG_SEEK_CUR);
      float fDim[4];
      for (auto &d : fDim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers<9)
        f << "box=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]), MWAWVec2f(fDim[2], fDim[3])) << ",";
      else
        f << "box=" << MWAWBox2f(MWAWVec2f(fDim[1], fDim[0]), MWAWVec2f(fDim[3], fDim[2])) << ",";

      long actPos=input->tell();
      std::vector<unsigned> mId;
      std::string extra;
      readMacroIndent(*stream, mId, extra);
      f << "id=[" << extra << "],";
      input->seek(actPos+20, librevenge::RVNG_SEEK_SET);

      for (auto &d : fDim) d=float(input->readULong(4))/65536.f;
      f << "box2=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]), MWAWVec2f(fDim[2], fDim[3])) << ",";

      long imageLen=input->readLong(4);
      if (96+imageLen<96 || 96+imageLen>data.length()) {
        f << "###";
        MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: can not retrieve the vkfl length\n"));
        break;
      }
      if (!imageLen)
        break;
      if (!readVKFL(stream, imageLen, shape.m_macoImage))
        f << "###";
      break;
    }
    case 0x44494d4e: // MACO
    case 0x54656368: // Tech
      if (vers>=9) {
        if (data.length()<=4)
          break;
        val=int(input->readLong(4));
        if (val!=1) f << "f0=" << val << ",";
        std::string extra;
        shape.m_special=m_mainParser->m_graphParser->readSpecialData(stream, data.length()-4, shape.m_subType, shape.m_box, extra);
        if (!shape.m_special)
          f << "###";
        f << extra;
        break;
      }
      MWAW_FALLTHROUGH;
    case 0x54585420: {
      std::string extra;
      shape.m_special=m_mainParser->m_graphParser->readSpecialData(stream, data.length(), shape.m_subType, shape.m_box, extra);
      if (!shape.m_special)
        f << "###";
      f << extra;
      break;
    }
    default: {
      std::string extra;
      shape.m_special=m_mainParser->m_graphParser->readSpecialData(stream, data.length(), shape.m_subType, shape.m_box, extra);
      if (!shape.m_special)
        f << "###";
      f << extra;
      break;
    }
    }
    break;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeMainData: find unexpected data for type=%d\n", shape.m_type));
    f << "##";
    break;
  }

  ascFile.addPos(data.begin());
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5Image::readVKFLShapeOtherData
(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image,
 std::tuple<MWAWEntry, unsigned, long> const &dataTypePos,
 std::vector<long> &childFieldPos, int subId)
{
  if (!stream || !stream->input())
    return false;
  MWAWEntry data;
  unsigned subType;
  long idPos;
  std::tie(data, subType, idPos)=dataTypePos;
  auto input=stream->input();
  if (!input->checkPosition(data.end()) || data.begin()<image.m_data[0].begin() || data.end()>image.m_data[0].end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData: the entry seems bad\n"));
    return false;
  }

  int const vers=version();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "Vkfl-B" << Canvas5Structure::getString(unsigned(data.id())) << "-" << Canvas5Structure::getString(subType);
  if (subId>=0)
    f << "[" << subId << "]";
  f << ":";
  input->seek(data.begin(), librevenge::RVNG_SEEK_SET);
  switch (data.id()) {
  case 1: {
    f << "color,";
    auto color=getStyleManager()->readColorStyle(stream, subType, data.length());
    if (!color) {
      f << "###";
      break;
    }

    image.m_posToColorMap[idPos]=color;
    break;
  }
  case 2: {
    f << "stroke,";
    if (data.length()!=20) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData: can not read a style field\n"));
      f << "##";
      break;
    }
    Canvas5ImageInternal::Stroke stroke;
    stroke.m_type=unsigned(input->readULong(4));
    if (stroke.m_type!=1) f << "type=" << Canvas5Structure::getString(stroke.m_type) << ",";
    for (int i=0; i<4; ++i) {
      long cPos=input->readLong(4);
      if (cPos<0) continue;
      char const *wh[]= { "penId", "dashId", "arrow[beg]", "arrow[end]"};
      childFieldPos.push_back(cPos);
      switch (i) {
      case 0:
        stroke.m_penPos=cPos;
        break;
      case 1:
        stroke.m_dashPos=cPos;
        break;
      default:
        stroke.m_arrowPos[i-2]=cPos;
        break;
      }
      f << wh[i] << "=Vk" << std::hex << cPos << std::dec << ",";
    }
    image.m_posToStrokeMap[idPos]=stroke;
    break;
  }
  case 3: {
    f << "pen,";
    auto pen=getStyleManager()->readPenStyle(*stream, subType, data.length());
    if (!pen) {
      f << "###";
      break;
    }

    image.m_posToPenMap[idPos]=pen;
    break;
  }
  case 4: {
    f << "matrix,";
    if (data.length()!=(vers<9 ? 72 : 144)) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData: can not read a matrix field\n"));
      f << "##";
      break;
    }
    for (size_t st=0; st<2; ++st) {
      f << "mat" << st << "=[";
      std::array<double,9> matrix;
      for (auto &d : matrix) {
        d=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
        f << d << ",";
      }
      f << "],";
      if (st==0)
        image.m_posToMatrixMap[idPos]=matrix;
    }
    break;
  }
  case 5: {
    f << "arrow,";
    MWAWGraphicStyle::Arrow arrow;
    if (!getStyleManager()->readArrow(stream, arrow, subType, data.length())) {
      f << "###";
      break;
    }
    image.m_posToArrowMap[idPos]=arrow;
    break;
  }
  case 6: {
    f << "dashes,";
    std::vector<float> dashes;
    if (!getStyleManager()->readDash(*stream, dashes, subType, data.length())) {
      f << "###";
      break;
    }
    image.m_posToDashMap[idPos]=dashes;
    break;
  }
  case 8: {
    f << "styles,";
    switch (subType) {
    case 0x54585420: { // TXT
      Canvas5StyleManager::CharStyle font;
      if (!getStyleManager()->readCharStyle(*stream, 0, font, false)) {
        f << "###";
        break;
      }

      bool ok=true;
      libmwaw::DebugStream f2;
      while (true) {
        long pos=input->tell();
        f2.str("");
        f2 << "Vkfl-B8-TXT [B]:";
        if (!input->checkPosition(pos+4)) {
          MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData[8,TXT ]: zone seems too short\n"));
          f << "###";
          ascFile.addPos(pos);
          ascFile.addNote(f2.str().c_str());
          ok=false;
          break;
        }

        int N=int(input->readLong(2));
        int type=int(input->readLong(2));
        if (N==0) {
          ascFile.addPos(pos);
          ascFile.addNote(f2.str().c_str());
          break;
        }
        int expectedLength=type==2 ? 64 : 0;
        if (N<0 || expectedLength==0 || (input->size()-pos-4)/expectedLength<N || pos+4+expectedLength*N>input->size()) {
          MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData[8,TXT ]: can not read the number of data\n"));
          f << "###";
          ascFile.addPos(pos);
          ascFile.addNote(f2.str().c_str());
          ok=false;
          break;
        }
        ascFile.addPos(pos);
        ascFile.addNote(f2.str().c_str());

        for (int k=0; k<N; ++k) {
          pos=input->tell();
          if (!getStyleManager()->readStyleEnd(stream)) {
            ascFile.addPos(pos);
            ascFile.addNote("Vkfl-B8-TXT [B]###:");
          }
          input->seek(pos+64, librevenge::RVNG_SEEK_SET);
        }
      }
      if (!ok) break;
      ascFile.addPos(input->tell());
      ascFile.addNote("Vkfl-B8-TXT [C]");
      // 000000004e6f726d616c0000000000000000000000000000000000000000000000000000 + (color)*
      break;
    }
    // also 0x70636567: pceg which contains a text link
    default:
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData[8]: unknown subtype=%s\n", Canvas5Structure::getString(subType).c_str()));
      f << "###";
      break;
    }
    break;
  }
  case 10: {
    f << "name,";
    std::string name;
    for (int i=0; i<data.length(); ++i) {
      char c=char(input->readLong(1));
      if (!c) break;
      name+=c;
    }
    f << name << ",";
    break;
  }
  case 11: {
    long len=input->readLong(4);
    // v5-v8:36, v9:64
    if (len<36 || len>data.length()) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData[11]: can not read the header length\n"));
      f << "###";
      break;
    }
    // 0, pobj, MACO
    f << "type=" << Canvas5Structure::getString(unsigned(input->readULong(4))) << ",";
    if (len!=data.length()) { // only if type=0
      // N+1 block of size 56: v<9 or 96: v==9
      // XOBJ, 3e8, endian, ...
      ascFile.addPos(data.begin()+len);
      ascFile.addNote("Vkfl-B11-0[data]:");
    }
    break;
  }
  case 12: //  similar to some XObd data 2 block
    if (data.length()!=40) {
      MWAW_DEBUG_MSG(("Canvas5Image::readVKFLShapeOtherData: can not read a type12 field\n"));
      f << "##";
      break;
    }
    f << "unkn=["; // find [-62.4277,0,3,127.855,127.855,127.855,0,0,3,127.855]
    for (int i=0; i<10; ++i)
      f << float(input->readLong(4))/65536 << ",";
    f << "],";
    break;
  default:
    f << "##";
    break;
  }

  ascFile.addPos(data.begin());
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// send data to the listener
////////////////////////////////////////////////////////////

bool Canvas5Image::send(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWListenerPtr listener,
                        MWAWBox2f const &box, MWAWTransformation const &transformation) const
{
  if (!listener || !image) {
    MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the image or the listener\n"));
    return false;
  }

  size_t shapeId=0;
  MWAWGraphicStyle style=MWAWGraphicStyle::emptyStyle();
  float scale[2]= {1,1};
  for (int i=0; i<2; ++i) {
    if (image->m_boxes[1].size()[i]>0)
      scale[i]=box.size()[i]/image->m_boxes[1].size()[i];
  }
  MWAWTransformation lTransformation=transformation*
                                     MWAWTransformation(MWAWVec3f(scale[0],0,box[0][0]-scale[0]*image->m_boxes[1][0][0]), MWAWVec3f(0,scale[1],box[0][1]-scale[0]*image->m_boxes[1][0][1]));
  while (shapeId<image->m_shapes.size()) {
    if (!send(*image, shapeId, listener, style, lTransformation))
      return false;
  }
  return true;
}

bool Canvas5Image::send(Canvas5ImageInternal::VKFLImage const &image, size_t &shapeId, MWAWListenerPtr listener,
                        MWAWGraphicStyle const &/*style*/, MWAWTransformation const &transformation) const
{
  if (shapeId>=image.m_shapes.size()) {
    MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the shape %d\n", int(shapeId)));
    return false;
  }
  auto const &shape=image.m_shapes[shapeId++];
  MWAWGraphicStyle lStyle;
  auto sIt=shape.m_idToDataPos.find(0);
  if (sIt!=shape.m_idToDataPos.end()) {
    auto cIt=image.m_posToColorMap.find(sIt->second);
    if (cIt==image.m_posToColorMap.end() || !cIt->second) {
      MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the surface color %x\n", unsigned(sIt->second)));
    }
    else
      getStyleManager()->updateSurfaceColor(*cIt->second, lStyle);
  }
  sIt=shape.m_idToDataPos.find(1);
  if (sIt!=shape.m_idToDataPos.end()) {
    auto cIt=image.m_posToColorMap.find(sIt->second);
    if (cIt==image.m_posToColorMap.end() || !cIt->second) {
      MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the line color %x\n", unsigned(sIt->second)));
    }
    else
      getStyleManager()->updateLineColor(*cIt->second, lStyle);
  }
  lStyle.m_lineWidth=0;
  sIt=shape.m_idToDataPos.find(2);
  if (sIt!=shape.m_idToDataPos.end()) {
    auto cIt=image.m_posToStrokeMap.find(sIt->second);
    if (cIt==image.m_posToStrokeMap.end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the surface stroke %x\n", unsigned(sIt->second)));
    }
    else {
      auto const &stroke=cIt->second;
      if (stroke.m_penPos>=0) {
        auto pIt=image.m_posToPenMap.find(stroke.m_penPos);
        if (pIt==image.m_posToPenMap.end() || !pIt->second) {
          MWAW_DEBUG_MSG(("Canvas5Image::send: can not find pen %ld\n", stroke.m_penPos));
        }
        else {
          int numLines;
          getStyleManager()->updateLine(*pIt->second, lStyle, numLines, 0, nullptr);
        }
      }
      if (stroke.m_dashPos>=0) {
        auto dIt=image.m_posToDashMap.find(stroke.m_dashPos);
        if (dIt==image.m_posToDashMap.end()) {
          MWAW_DEBUG_MSG(("Canvas5Image::send: can not find dash %ld\n", stroke.m_dashPos));
        }
        else
          lStyle.m_lineDashWidth=dIt->second;
      }
      for (int i=0; i<2; ++i) {
        if (stroke.m_arrowPos[i]<0)
          continue;
        auto dIt=image.m_posToArrowMap.find(stroke.m_arrowPos[i]);
        if (dIt==image.m_posToArrowMap.end()) {
          MWAW_DEBUG_MSG(("Canvas5Image::send: can not find arrow %ld\n", stroke.m_arrowPos[i]));
        }
        else
          lStyle.m_arrows[i]=dIt->second;
      }
    }
  }
  auto lTransformation(transformation);
  for (int m=3; m<6; ++m) {
    sIt=shape.m_idToDataPos.find(m);
    if (sIt==shape.m_idToDataPos.end())
      continue;
    auto mIt=image.m_posToMatrixMap.find(sIt->second);
    if (mIt==image.m_posToMatrixMap.end()) {
      MWAW_DEBUG_MSG(("Canvas5Image::send: can not find the surface matrix %x\n", unsigned(sIt->second)));
      continue;
    }
    if (m!=3)
      continue;
    auto const &mat=mIt->second;
    if (mat[2]<-1e-3 || mat[2]>1e-3 || mat[5]<-1e-3 || mat[5]>1e-3) {
      MWAW_DEBUG_MSG(("Canvas5Image::send: image matrix will be ignored\n"));
    }
    else
      lTransformation*=MWAWTransformation(MWAWVec3f((float)mat[0],(float)mat[3],(float)mat[6]), MWAWVec3f((float)mat[1],(float)mat[4],(float)mat[7]));
  }
  MWAWGraphicShape fShape;
  switch (shape.m_type) {
  case 1:
    fShape=MWAWGraphicShape::polygon(shape.m_box);
    fShape.m_vertices=shape.m_vertices;
    break;
  case 2: {
    if (shape.m_vertices.size()<2 || (shape.m_vertices.size()%4)) {
      MWAW_DEBUG_MSG(("Canvas5Image::send[spline]: find bad N\n"));
      return true;
    }
    fShape=MWAWGraphicShape::path(shape.m_box);
    auto &path=fShape.m_path;
    path.push_back(MWAWGraphicShape::PathData('M', shape.m_vertices[0]));
    for (size_t p=3; p < shape.m_vertices.size(); p+=4) {
      if (p>=4 && shape.m_vertices[p-4]!=shape.m_vertices[p-3])
        path.push_back(MWAWGraphicShape::PathData('M', shape.m_vertices[p-3]));
      bool hasFirstC=shape.m_vertices[p-3]!=shape.m_vertices[p-2];
      bool hasSecondC=shape.m_vertices[p-1]!=shape.m_vertices[p];
      if (!hasFirstC && !hasSecondC)
        path.push_back(MWAWGraphicShape::PathData('L', shape.m_vertices[p]));
      else
        path.push_back(MWAWGraphicShape::PathData('C', shape.m_vertices[p], shape.m_vertices[p-2], shape.m_vertices[p-1]));
    }
    if (lStyle.hasSurface())
      path.push_back(MWAWGraphicShape::PathData('Z'));
    break;
  }
  case 6:
    fShape=MWAWGraphicShape::rectangle(shape.m_box);
    break;
  case 7:
    fShape=MWAWGraphicShape::circle(shape.m_box);
    break;
  case 8:
    fShape=MWAWGraphicShape::rectangle(shape.m_box, MWAWVec2f(shape.m_locals[0], shape.m_locals[1]));
    break;
  case 9: // checkme: maybe better to use shape.m_vertices[0-1] if it exists...
    if (shape.m_vertices.size()==2)
      fShape=MWAWGraphicShape::line(shape.m_vertices[0], shape.m_vertices[1]);
    else
      fShape=MWAWGraphicShape::line(shape.m_box[0], shape.m_box[1]);
    break;
  case 10: {
    float angles[]= {shape.m_locals[0], shape.m_locals[1]};
    int angle[2] = { int(90-angles[0]), int(90-angles[0]-angles[1]) };
    if (angles[0]<0)
      std::swap(angle[0],angle[1]);
    else if (angles[0]>=360)
      angle[0]-=359;
    if (angle[1]>360) {
      int numLoop=int(angle[1]/360)-1;
      angle[0]-=numLoop*360;
      angle[1]-=numLoop*360;
      while (angle[1] > 360) {
        angle[0]-=360;
        angle[1]-=360;
      }
    }
    if (angle[0] < -360) {
      int numLoop=int(angle[0]/360)+1;
      angle[0]-=numLoop*360;
      angle[1]-=numLoop*360;
      while (angle[0] < -360) {
        angle[0]+=360;
        angle[1]+=360;
      }
    }
    // we must compute the real bd box
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; ++i)
      limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; ++bord) {
      float ang = (bord == limitAngle[0]) ? float(angle[0]) :
                  (bord == limitAngle[1]+1) ? float(angle[1]) : float(90 * bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { std::cos(ang), -std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    MWAWBox2f circleBox=shape.m_box;
    // we have the shape box, we need to reconstruct the circle box
    if (maxVal[0]>minVal[0] && maxVal[1]>minVal[1]) {
      float scaling[2]= { (shape.m_box[1][0]-shape.m_box[0][0])/(maxVal[0]-minVal[0]),
                          (shape.m_box[1][1]-shape.m_box[0][1])/(maxVal[1]-minVal[1])
                        };
      for (auto &s : scaling) {
        if (s>1e7)
          s=100;
        else if (s<-1e7)
          s=-100;
      }
      float constant[2]= { shape.m_box[0][0]-minVal[0] *scaling[0], shape.m_box[0][1]-minVal[1] *scaling[1]};
      circleBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                          MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
    }
    fShape = MWAWGraphicShape::pie(shape.m_box, circleBox, MWAWVec2f(float(angle[0]), float(angle[1])));
    break;
  }
  case 11:
  case 12: {
    if (shapeId+size_t(shape.m_subType)<shapeId ||
        shapeId+size_t(shape.m_subType)>image.m_shapes.size()) {
      MWAW_DEBUG_MSG(("Canvas5Image::send[group]: find bad N=%d\n", int(shape.m_subType)));
      return true;
    }
    if (shape.m_subType<=1)
      return true;
    MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(100,100), librevenge::RVNG_POINT); // checkme shape box is not valid
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
    for (size_t i=0; i<size_t(shape.m_subType); ++i) {
      if (!send(image, shapeId, listener, lStyle, lTransformation))
        break;
    }
    listener->closeGroup();
    return true;
  }
  case 14:
    switch (shape.m_subType) {
    case 0x706f626a:
    case 0x8F909d96: {
      if (shape.m_bitmap.isEmpty()) {
        MWAW_DEBUG_MSG(("Canvas5Image::send[pobj]: can not find the bitmap\n"));
        return true;
      }

      MWAWTransformation transf;
      float rotation=0;
      MWAWVec2f shearing;
      if (!lTransformation.isIdentity() && lTransformation.decompose(rotation,shearing,transf,shape.m_box.center())) {
        MWAWBox2f shapeBox=transf*shape.m_box;
        MWAWPosition pos(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
        pos.m_anchorTo = MWAWPosition::Page;
        lStyle.m_rotate=-rotation;
        listener->insertPicture(pos, shape.m_bitmap, lStyle);
      }
      else {
        MWAWPosition pos(shape.m_box[0], shape.m_box.size(), librevenge::RVNG_POINT);
        pos.m_anchorTo = MWAWPosition::Page;
        listener->insertPicture(pos, shape.m_bitmap, lStyle);
      }
      return true;
    }
    case 0x4d41434f: { // MACO
      if (!shape.m_macoImage) {
        MWAW_DEBUG_MSG(("Canvas5Image::send[pobj]: can not find the macro imag\n"));
        return true;
      }
      return send(shape.m_macoImage, listener, shape.m_box, lTransformation);
    }
    default:
      if (!shape.m_special) {
        MWAW_DEBUG_MSG(("Canvas5Image::send[special]: can not find the special data\n"));
        return true;
      }
      else {
        Canvas5Graph::LocalState lState;
        lState.m_position=MWAWPosition(shape.m_box[0], shape.m_box.size(), librevenge::RVNG_POINT);
        lState.m_position.m_anchorTo = MWAWPosition::Page;
        lState.m_style=lStyle;
        lState.m_transform=lTransformation;
        return m_mainParser->m_graphParser->sendSpecial(listener, *shape.m_special, lState);
      }
      break;
    }
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5Image::send: sending type=%d is not implemented\n", shape.m_type));
    return true;
  }
  if (!lTransformation.isIdentity())
    fShape=fShape.transform(lTransformation);
  MWAWBox2f shapeBox=fShape.getBdBox();
  MWAWPosition sPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
  listener->insertShape(sPosition, fShape, lStyle);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
