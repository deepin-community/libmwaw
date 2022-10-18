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

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWParser.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "Canvas5Parser.hxx"

#include "Canvas5Graph.hxx"
#include "Canvas5Image.hxx"
#include "Canvas5Structure.hxx"
#include "Canvas5StyleManager.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a Canvas5Graph */
namespace Canvas5GraphInternal
{
//! Internal: the section data
struct SectionData {
  //! constructor
  SectionData()
    : m_numColumns(1)
    , m_bdBox()
  {
  }
  //! the number of columns
  int m_numColumns;
  //! the bounding box
  MWAWBox2f m_bdBox;
};

//! Internal: the shape data
struct ShapeData {
  //! constructor
  ShapeData()
    : m_inMainZone(true)
    , m_type(0)
    , m_stream()
    , m_streamReverted(false)
    , m_entry()
    , m_vertices()
    , m_children()
    , m_macoId()

    , m_grid(1,1)
    , m_ngonType(4)

    , m_gdeType(0)
    , m_sections()
  {
    for (auto &l : m_local) l=0;
    for (auto &i : m_ids) i=0;
    for (auto &s : m_shapeIds) s=0;
    for (auto &s : m_specials) s=0;
    for (auto &e : m_cweb) e=MWAWEntry();
    for (auto &d : m_doubleValues) d=0;
  }
  //! returns the data stream
  Canvas5Structure::Stream &getStream() const
  {
    if (!m_stream || !m_stream->input()) {
      MWAW_DEBUG_MSG(("Canvas5GraphInternal::ShapeData::getStream: no input stream\n"));
      throw libmwaw::ParseException();
    }
    m_stream->input()->setReadInverted(m_streamReverted);
    return *m_stream;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ShapeData const &s)
  {
    for (int i=0; i< int(MWAW_N_ELEMENTS(s.m_local)); ++i) {
      if (s.m_local[i]) o << "l" << i << "=" << s.m_local[i] << ",";
    }
    for (int i=0; i< int(MWAW_N_ELEMENTS(s.m_ids)); ++i) {
      if (!s.m_ids[i]) continue;
      char const *wh[]= {"TL", "Mat", "Str"};
      o << wh[i] << s.m_ids[i] << ",";
    }
    for (int i=0; i< int(MWAW_N_ELEMENTS(s.m_shapeIds)); ++i) {
      if (!s.m_shapeIds[i]) continue;
      char const *wh[]= {"child", "parent", "shape1", "shape2"};
      o << wh[i] << "=S" << s.m_shapeIds[i] << ",";
    }
    return o;
  }
  //! a flag to know if the shape is in the main zone or in Vkfl
  bool m_inMainZone;
  //! the shape type
  unsigned m_type;
  //! the data stream
  std::shared_ptr<Canvas5Structure::Stream> m_stream;
  //! a flag to know the stream endian
  bool m_streamReverted;
  //! the shape data entry
  MWAWEntry m_entry;
  //! the local variable
  int m_local[2];
  //! the text link, matrix, name id
  unsigned m_ids[3];
  //! the shape ids
  unsigned m_shapeIds[4];

  //! the shape vertices: line, ...
  std::vector<MWAWVec2f> m_vertices;
  //! the childs: group
  std::vector<unsigned> m_children;
  //! the macro Id: MACO
  std::vector<unsigned> m_macoId;

  // special

  //! the grid subdivision
  MWAWVec2i m_grid;
  //! some special values
  int m_specials[4];
  //! the buttons image entries
  MWAWEntry m_cweb[3];
  //! the n-polygon type: NGON
  int m_ngonType;
  //! the #Gde type
  int m_gdeType;
  //! the sections: #Gde
  std::vector<SectionData> m_sections;
  //! the arc angles or rect oval size: v9
  double m_doubleValues[4];
};

//! Internal: the shape of a Canvas5Graph
struct Shape {
  //! constructor
  Shape()
    : m_type(-1)
    , m_id(0)
    , m_initialBox()
    , m_bdbox()
    , m_pos()
    , m_sent(false)
  {
    for (auto &f : m_flags) f=0;
    for (auto &v : m_values) v=0;
  }

  //! returns the type name
  std::string getTypeName() const
  {
    static std::map<int, std::string> const s_typeName= {
      {2,"text"},
      {3,"line"},
      {4,"rect"},
      {5,"rectOval"},
      {6,"oval"},
      {7,"arc"},
      {9,"polyline"},
      {10,"spline"},
      {52,"special"},
      {99,"group"},
      {100,"none"}
    };
    auto const &it=s_typeName.find(m_type);
    if (it!=s_typeName.end())
      return it->second;
    std::stringstream s;
    s << "Type" << m_type << "A";
    return s.str();
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Shape const &s)
  {
    o << s.getTypeName() << ",";
    o << s.m_bdbox << ",";
    if (s.m_bdbox!=s.m_initialBox)
      o << "bdbox[orig]=" << s.m_initialBox << ",";
    if (s.m_type!=100 && s.m_pos>=0)
      o << "data=" << std::hex << s.m_pos << std::dec << ",";
    if (s.m_flags[0]) {
      if (s.m_flags[0]&1) o << "locked,";
      if (s.m_flags[0]&4) o << "noPrint,";
      if (s.m_flags[0]&0x200) o << "spread[trap],";
      if (s.m_flags[0]&0x400) o << "overPrint,";
      if (s.m_flags[0]&0x800) o << "trap[choke],";
      int val=(s.m_flags[0]&0xf1fa);
      if (val)
        o << "fl=" << std::hex << val << std::dec << ",";
    }
    if (s.m_flags[1]) {
      if (s.m_flags[1]&1) o << "parent,";
      if (s.m_flags[1]&2) o << "shape1,";
      if (s.m_flags[1]&4) o << "shape2,";
      if (s.m_flags[1]&8) o << "rot,";
      int val=(s.m_flags[1]&0xfff9);
      if (val)
        o << "fl1=" << std::hex << val << std::dec << ",";
    }
    if (s.m_flags[2]) {
      if ((s.m_flags[2]&0x1)==0) o << "no[size],";
      if (s.m_flags[2]&0x4) o << "txtPlc[id],";
      if (s.m_flags[2]&0x10) o << "mat,";
      if (s.m_flags[2]&0x20) o << "type,";
      if (s.m_flags[2]&0x80) o << "shape[id],";
      if (s.m_flags[2]&0x200) o << "loc1,";
      if (s.m_flags[2]&0x400) o << "loc2,";
      if (s.m_flags[2]&0x800) o << "name,";
      int val=(s.m_flags[2]&0xf1ca);
      if (val)
        o << "fl2=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<int(MWAW_N_ELEMENTS(s.m_values)); ++i) {
      int val=s.m_values[i];
      if (val==0) continue;
      char const *wh[]= {nullptr, "col[surf]=Co", "col[line]=Co", "stroke=St"};
      if (wh[i])
        o << wh[i] << val << ",";
      else
        o << "f" << i << "=" << val << ",";
    }
    return o;
  }
  //! the shape type
  int m_type;
  //! the shape id
  int m_id;
  //! the original box
  MWAWBox2f m_initialBox;
  //! the bounding box
  MWAWBox2f m_bdbox;
  //! the beginning position
  long m_pos;
  //! some unknown value
  int m_values[4];
  //! some unknown flag
  int m_flags[3];
  //! a flag to know if the shape is already send
  mutable bool m_sent;
};

//! Internal[low level]: a pseudo class to store the data corresponding to a shape
struct PseudoShape {
  //! constructor
  PseudoShape()
    : m_shape()
    , m_data()
  {
  }

  //! the shape
  Shape m_shape;
  //! the data shape
  ShapeData m_data;
};

////////////////////////////////////////
//! Internal: the state of a Canvas5Graph
struct State {
  //! constructor
  State()
    : m_dataStream()
    , m_dataStreamReverted(false)

    , m_shapeZones()
    , m_idToShapeMap()
    , m_posToShapeDataMap()
    , m_idToMatrices()

    , m_sendIdSet()
    , m_sendAGIFIdSet()
    , m_sendMACOIdSet()
  {
  }

  //! the data shape stream
  std::shared_ptr<Canvas5Structure::Stream> m_dataStream;
  //! a flag to retrieved the data shape entry
  bool m_dataStreamReverted;

  //! the shape data zones
  std::vector<MWAWEntry> m_shapeZones;
  //! the map id to shape
  std::map<int, Shape> m_idToShapeMap;
  //! the map id to shape data
  std::map<long, ShapeData> m_posToShapeDataMap;
  //! the map id to matrices
  std::map<int, std::array<std::array<double,9>, 2> > m_idToMatrices;

  //! the list of current send shape id (used to avoid loop)
  std::set<int> m_sendIdSet;
  //! the list of current send GIF id (used to avoid loop)
  std::set<int> m_sendAGIFIdSet;
  //! the list of current send macro id (used to avoid loop)
  std::set<std::vector<unsigned> > m_sendMACOIdSet;
};

////////////////////////////////////////
//! Internal: the subdocument of a Canvas5Graph
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor from a zoneId
  SubDocument(Canvas5Graph &parser, MWAWInputStreamPtr const &input, Shape const &shape, ShapeData const &data)
    : MWAWSubDocument(parser.m_mainParser, input, MWAWEntry())
    , m_graphParser(parser)
    , m_shape(&shape)
    , m_data(&data)
    , m_measure()
    , m_font()
  {
  }
  //! constructor from string
  SubDocument(Canvas5Graph &parser, MWAWInputStreamPtr const &input, librevenge::RVNGString const &measure, MWAWFont const &font)
    : MWAWSubDocument(parser.m_mainParser, input, MWAWEntry())
    , m_graphParser(parser)
    , m_shape(nullptr)
    , m_data(nullptr)
    , m_measure(measure)
    , m_font(font)
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
    if (&m_graphParser != &sDoc->m_graphParser) return true;
    if (m_shape != sDoc->m_shape) return true;
    if (m_data != sDoc->m_data) return true;
    if (m_measure != sDoc->m_measure) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the graph parser
  Canvas5Graph &m_graphParser;
  //! the shape
  Shape const *m_shape;
  //! the shape data
  ShapeData const *m_data;
  //! the measure
  librevenge::RVNGString m_measure;
  //! the font
  MWAWFont m_font;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("Canvas5GraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (!m_shape || !m_data) {
    if (m_measure.empty()) {
      MWAW_DEBUG_MSG(("Canvas5GraphInternal::SubDocument::parse: can not find the measure\n"));
      return;
    }
    listener->setFont(m_font);
    MWAWParagraph para;
    para.m_justify = MWAWParagraph::JustificationCenter;
    listener->setParagraph(para);
    listener->insertUnicodeString(m_measure);
    return;
  }
  long pos = m_input ? m_input->tell() : 0;
  m_graphParser.sendText(listener, *m_shape, *m_data);
  if (m_input) m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
Canvas5Graph::Canvas5Graph(Canvas5Parser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new Canvas5GraphInternal::State)
  , m_mainParser(&parser)
  , m_imageParser(parser.m_imageParser)
  , m_styleManager(parser.m_styleManager)
{
}

Canvas5Graph::~Canvas5Graph()
{
}

int Canvas5Graph::version() const
{
  return m_parserState->m_version;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

bool Canvas5Graph::readMatrices(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readMatrices: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readMatrices: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = stream->ascii();
  pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Matrix):";
  if (version()>=9) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return m_mainParser->readArray9(stream, "Matrix",
    [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
      auto lInput=lStream->input();
      libmwaw::DebugFile &asciiFile = lStream->ascii();
      libmwaw::DebugStream lF;
      lF << "Mat" << item.m_id << ",";
      if (item.m_length!=144) {
        MWAW_DEBUG_MSG(("Canvas5Graph::readMatrices: a matrix is too short\n"));
        lF << "###";
        asciiFile.addPos(item.m_pos);
        asciiFile.addNote(lF.str().c_str());
      }
      lInput->seek(-4, librevenge::RVNG_SEEK_CUR);
      std::array<std::array<double,9>, 2> matrices;
      for (size_t st=0; st<2; ++st) {
        lF << "mat" << st << "=[";
        auto &matrix = matrices[st];
        for (auto &d : matrix) {
          d=m_mainParser->readDouble(*lStream, 8);
          lF << d << ",";
        }
        lF << "],";
      }
      m_state->m_idToMatrices[item.m_id]=matrices;
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote(lF.str().c_str());
    });
  }
  int val=int(input->readLong(4));
  if (val!=-1)
    f << "f0=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (!m_mainParser->readUsed(*stream, "Matrix")) // size=144
    return false;
  return m_mainParser->readExtendedHeader(stream, 0x48, "Matrix",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
    auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Mat" << item.m_id << ",";
    std::array<std::array<double,9>, 2> matrices;
    for (size_t st=0; st<2; ++st) {
      lF << "mat" << st << "=[";
      auto &matrix = matrices[st];
      for (auto &d : matrix) {
        d=double(lInput->readLong(4))/65536.;
        lF << d << ",";
      }
      lF << "],";
    }
    m_state->m_idToMatrices[item.m_id]=matrices;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  });
}

////////////////////////////////////////////////////////////
// shapes
////////////////////////////////////////////////////////////

bool Canvas5Graph::findShapeDataZones(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  m_state->m_dataStream=stream;
  auto input=stream->input();
  m_state->m_dataStreamReverted=input->readInverted();
  auto &ascFile=stream->ascii();
  long pos=input->tell();
  int len=int(input->readULong(4));
  if ((len%20)!=0 || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::findShapeDataZones: can not find zone 1\n"));
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote("Entries(DataShap):");

  libmwaw::DebugStream f;
  int N=len/20;
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "DataShap-" << i << ":";
    f << "id=" << input->readULong(4) << ",";
    f << "f0=" << input->readULong(4) << ","; // small number
    f << "sz=" << input->readULong(4) << ",";
    for (int j=0; j<4; ++j) { // 0
      int val=int(input->readLong(2));
      if (val)
        f << "f" << j+1 << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "DataShap-A" << i << ":";
    long zLen=long(input->readULong(4));
    if (pos+4+zLen<pos+4 || !input->checkPosition(pos+4+zLen)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::findShapeDataZones: can not find a zone 1 length\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    MWAWEntry entry;
    entry.setBegin(pos+4);
    entry.setLength(zLen);
    m_state->m_shapeZones.push_back(entry);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+4+zLen, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool Canvas5Graph::readShapes(Canvas5Structure::Stream &stream, int numShapes)
{
  auto input=stream.input();
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapes: can not find the input\n"));
    return false;
  }
  long len=long(input->readULong(4));
  long endPos=pos+4+len;
  int const vers=version();
  int const dataSize=vers<9 ? 60 : 96;
  if (endPos<pos+4 || len<dataSize*numShapes || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapes: can not determine the zone length\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream.ascii();
  libmwaw::DebugStream f;
  f << "Entries(Shape):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  bool reverted=input->readInverted();
  for (int i=0; i<numShapes; ++i) {
    pos=input->tell();
    if (i<1) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(pos+dataSize, librevenge::RVNG_SEEK_SET);
      continue;
    }
    Canvas5GraphInternal::Shape shape;
    f.str("");
    f << "Shape-S" << i << ":";
    float fDim[4];
    for (auto &d : fDim) d=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
    if (vers<9)
      shape.m_initialBox=MWAWBox2f(MWAWVec2f(fDim[1], fDim[0]),MWAWVec2f(fDim[3], fDim[2]));
    else
      shape.m_initialBox=MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]),MWAWVec2f(fDim[2], fDim[3]));
    for (auto &d : fDim) d=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
    if (vers<9)
      shape.m_bdbox=MWAWBox2f(MWAWVec2f(fDim[1], fDim[0]),MWAWVec2f(fDim[3], fDim[2]));
    else
      shape.m_bdbox=MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]),MWAWVec2f(fDim[2], fDim[3]));
    unsigned block=unsigned(input->readULong(2));
    shape.m_pos=int((block<<16)|input->readULong(2));
    if (shape.m_pos==int(0xFFFFFFFF)) shape.m_pos=-1;
    shape.m_type=int(input->readULong(1));
    if (shape.m_type==100) { // none
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(pos+dataSize, librevenge::RVNG_SEEK_SET);
      continue;
    }
    f << "id=" << std::hex << input->readULong(4) << std::dec << ",";
    shape.m_values[0]=int(input->readULong(1));
    for (int j=0; j<3; ++j)
      shape.m_flags[j]=int(input->readULong(2));
    if (reverted) std::swap(shape.m_flags[1],shape.m_flags[2]);
    if (shape.m_flags[1]&0x60) {
      f << "##fl,";
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapes: find some unknown flags\n"));
    }
    for (int j=0; j<3; ++j) // small number, maybe parent id, child id, ???
      shape.m_values[j+1]=int(input->readLong(4));
    f << shape << ",";
    shape.m_id=i;
    m_state->m_idToShapeMap[i]=shape;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+dataSize, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()<endPos)
    ascFile.skipZone(input->tell(), endPos-1);
  if (&stream!=m_state->m_dataStream.get()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapes: oops, the shape data stream seems bad\n"));
  }
  else {
    for (auto &it : m_state->m_idToShapeMap) {
      if (it.second.m_pos>=0)
        readShapeData(it.first, it.second);
    }
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool Canvas5Graph::readShapeData(int id, Canvas5GraphInternal::Shape const &shape)
{
  auto &stream=m_state->m_dataStream;
  if (shape.m_pos < 0 || !stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: shape id=%d has not a valid position\n", id));
    return false;
  }
  if ((shape.m_flags[1]&0x419f)==0 && (shape.m_flags[2]&0xfff)==0 && shape.m_type>=4 && shape.m_type<=7)
    return true; // sometimes m_pos is set even if there is no data
  size_t bl=size_t(shape.m_pos>>16);
  long pos=shape.m_pos&0xffff;
  if (bl>=m_state->m_shapeZones.size() || pos+4 > m_state->m_shapeZones[bl].length()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: can not find the block corresponding to shape id=%d\n", id));
    return false;
  }

  if (m_state->m_posToShapeDataMap.find(shape.m_pos)!=m_state->m_posToShapeDataMap.end())
    return true;
  auto const &entry=m_state->m_shapeZones[bl];
  auto input=stream->input();
  int const vers=version();
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "DataShap-S" << id << ":";

  input->seek(entry.begin()+pos, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  int val=int(input->readULong(4));
  if (val!=id) f << "dup2=" << val << ",";
  f << shape.getTypeName() << ",";

  m_state->m_posToShapeDataMap[shape.m_pos]=Canvas5GraphInternal::ShapeData();
  Canvas5GraphInternal::ShapeData &data=m_state->m_posToShapeDataMap.find(shape.m_pos)->second;
  long len=(shape.m_flags[2]&0x1) ? long(input->readULong(4)) : 0;
  if (shape.m_flags[2]&0x2) f << "f2=" << long(input->readULong(4)) << ","; // find id=1???
  if (shape.m_flags[2]&0x4) data.m_ids[0]=unsigned(input->readULong(4));
  if (shape.m_flags[2]&0x8) f << "f8=" << long(input->readULong(4)) << ","; // never seen this one
  if (shape.m_flags[2]&0x10) data.m_ids[1]=unsigned(input->readULong(4));
  if (shape.m_flags[2]&0x20) data.m_type = unsigned(input->readULong(4));
  if (shape.m_flags[2]&0x40) f << "f40=" << long(input->readULong(4)) << ","; // never seen this one
  if (shape.m_flags[2]&0x80) data.m_shapeIds[0]=unsigned(input->readULong(4)); // checkme: replaced?
  if (shape.m_flags[2]&0x100) f << "f100=" << long(input->readULong(4)) << ","; // never seen this one
  if (shape.m_flags[2]&0x200) data.m_local[0]=int(input->readULong(4)); // text: id?, rectOval: roundX, arc angl1
  if (shape.m_flags[2]&0x400) data.m_local[1]=int(input->readULong(4)); // roundY, angl2
  if (shape.m_flags[2]&0x800) data.m_ids[2]=unsigned(input->readULong(4));
  if (shape.m_flags[1]&0x1) data.m_shapeIds[1]=unsigned(input->readULong(4));
  if (shape.m_flags[1]&0x2) data.m_shapeIds[2]=unsigned(input->readULong(4)); // child?
  if (shape.m_flags[1]&0x4) data.m_shapeIds[3]=unsigned(input->readULong(4)); // often simillar to shape1 ?
  if (shape.m_flags[1]&0x8) f << "g8=" << long(input->readULong(4)) << ","; // rotation?
  if (shape.m_flags[1]&0x10) f << "g10=" << long(input->readULong(4)) << ","; // ?
  // checkme: we need probably also test for f1&0x20 and f1&0x40
  if (shape.m_flags[1]&0x80) f << "g80=" << long(input->readULong(4)) << ","; // ?
  if (shape.m_flags[1]&0x100) f << "link[id]=" << long(input->readULong(4)) << ","; //  checkme: appear in v6 with id=3200
  if (shape.m_flags[1]&0x4000) f << "Xobd" << long(input->readULong(4)) << ","; // appear in v6, related to object data base

  if (data.m_type) f << "type=" << Canvas5Structure::getString(data.m_type) << ",";
  f << data;

  long actPos=input->tell();
  long endPos=actPos+len;
  if (endPos<actPos || endPos>entry.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: oops, bad length for shape id=%d\n", id));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  if (len && shape.m_type!=2)
    ascFile.addDelimiter(input->tell(),'|');

  input->pushLimit(entry.end());

  data.m_stream=stream;
  data.m_streamReverted=input->readInverted();
  data.m_entry.setBegin(input->tell());
  data.m_entry.setLength(len);

  switch (shape.m_type) {
  case 2: // with type="TXT " or "TxtU"
    // will be parsed by sendText
    break;
  case 3: {
    if (len<(vers<9 ? 16 : 32)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: unexpected size for a line\n"));
      f << "###";
      break;
    }
    f << "pts=[";
    for (int i=0; i<2; ++i) {
      float fDim[2];
      for (auto &d : fDim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers>=9)
        data.m_vertices.push_back(MWAWVec2f(fDim[0], fDim[1]));
      else
        data.m_vertices.push_back(MWAWVec2f(fDim[1], fDim[0]));
      f << data.m_vertices.back() << ",";
    }
    f << "];";
    break;
  }
  case 4: // rect
  case 5: // rectOval
  case 6: // oval
  case 7: // arc
    if (vers>=9 && (shape.m_type==5 || shape.m_type==7) && len==16) {
      f << (shape.m_type==5 ? "round" : "angle") << "=";
      for (int i=0; i<2; ++i) {
        data.m_doubleValues[i]=m_mainParser->readDouble(*stream, 8);
        f << data.m_doubleValues[i] << (i==0 ? "x" : ",");
      }
      break;
    }
    if (len) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: find unexpected length\n"));
      f << "###";
      break;
    }
    if (shape.m_type==5)
      f << "round=" << float(data.m_local[0])/65536.f << "x"  << float(data.m_local[1])/65536.f << ",";
    else if (shape.m_type==7)
      f << "angle=" << float(data.m_local[0])/65536.f << "->"  << float(data.m_local[1])/65536.f << ",";
    break;
  case 9: // polyline
  case 10: { // spline
    if (len<8) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: unexpected size for a polyline/spline\n"));
      f << "###";
      break;
    }
    if (vers<9)
      input->seek(4, librevenge::RVNG_SEEK_CUR);
    int N=m_mainParser->readInteger(*stream, vers<9 ? 4 : 8);
    f << "N=" << N << ",";
    if (vers>=9)
      input->seek(8, librevenge::RVNG_SEEK_CUR);
    int const fieldSize=vers<9 ? 4 : 8;
    if (4+fieldSize+2*fieldSize*N<4+fieldSize || (len-4-fieldSize)/(2*fieldSize)<N || 4+fieldSize+2*fieldSize*N>len) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData[polyline/spline]: can not read N\n"));
      f << "###";
      break;
    }
    f << "pts=[";
    for (int i=0; i<N; ++i) {
      float fDim[2];
      for (auto &d : fDim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers<9)
        data.m_vertices.push_back(MWAWVec2f(fDim[1], fDim[0]));
      else
        data.m_vertices.push_back(MWAWVec2f(fDim[0], fDim[1]));
      f << data.m_vertices.back() << ",";
    }
    f << "],";
    break;
  }
  case 52: { // special
    std::string extra;
    if (!readSpecialData(stream, len, data, extra))
      f << "###";
    f << extra;
    break;
  }
  case 20: // master elements ?
  case 99: { // group
    if (len<4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: unexpected size for a group\n"));
      f << "###";
      break;
    }
    int N=int(input->readULong(4));
    f << "N=" << N << ",";
    if (4+4*N<4 || 4+4*N>len) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData[group]: can not read N\n"));
      f << "###";
      break;
    }
    f << "id=[";
    for (int i=0; i<N; ++i) {
      data.m_children.push_back(unsigned(input->readULong(4)));
      f << "S" << data.m_children.back() << ",";
    }
    f << "],";
    break;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: unexpected type\n"));
    f << "###";
    break;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (len && input->tell()!=endPos)
    ascFile.addDelimiter(input->tell(),'|');
  input->popLimit();

  return true;
}

std::shared_ptr<Canvas5GraphInternal::PseudoShape> Canvas5Graph::readSpecialData(std::shared_ptr<Canvas5Structure::Stream> stream, long len, unsigned type, MWAWBox2f const &box, std::string &extra)
{
  if (!stream)
    return nullptr;
  auto input=stream->input();

  auto res=std::make_shared<Canvas5GraphInternal::PseudoShape>();
  Canvas5GraphInternal::ShapeData &data=res->m_data;
  data.m_inMainZone=false;
  data.m_type=type;

  data.m_stream=stream;
  data.m_streamReverted=input->readInverted();
  data.m_entry.setBegin(input->tell());
  data.m_entry.setLength(len);
  if (!readSpecialData(stream, len, data, extra))
    return nullptr;
  auto &shape=res->m_shape;
  shape.m_type=52;
  shape.m_initialBox=shape.m_bdbox=box;
  return res;
}

bool Canvas5Graph::readSpecialData(std::shared_ptr<Canvas5Structure::Stream> stream, long len, Canvas5GraphInternal::ShapeData &data, std::string &extra)
{
  if (!stream)
    return false;
  auto input=stream->input();
  int const vers=version();
  libmwaw::DebugStream f;
  int val;
  switch (data.m_type) {
  case 0x43756265: // Cube
    if (len<(vers<9 ? 64 : 128)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: can not find the cube points\n"));
      return false;
    }
    for (int i=0; i<8; ++i) { // front face, back face
      float pts[2];
      for (auto &c : pts) c=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      if (vers>=9)
        std::swap(pts[0],pts[1]);
      data.m_vertices.push_back(MWAWVec2f(pts[1],pts[0]));
      f << data.m_vertices.back() << ",";
    }
    break;
  case 0x43765465: // CvTe, will be read when we create the shape
  case 0x44494d4e: // DIMN, will be read when we create the shape
    break;
  case 0x4e474f4e: { // NGON
    if (len<(vers<9 ? 56 : 72)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readShapeData: can not find the NGON data\n"));
      return false;
    }
    val=int(input->readULong(2));
    if (val&0x100)
      f << "smooth,";
    val&=0xfeff;
    if (val)
      f << "fl=" << std::hex << val << std::hex << ",";
    if (vers<9) {
      for (int i=0; i<5; ++i) {
        val=int(input->readULong(2));
        int const expected[]= {1,0x255, 0x6ae0, 0x2440, 0x1404 };
        if (val!=expected[i]) f << "f" << i << "=" << val << ",";
      }
      for (int i=0; i<5; ++i) {
        val=int(input->readULong(4));
        int const expected[]= {0x22e5140, 0x2232300, 0x2556af0, 0x23718c2, 0xec634 };
        if (val!=expected[i]) f << "f" << i+6 << "=" << val << ",";
      }
    }
    else {
      for (int i=0; i<7; ++i) {
        val=int(input->readULong(2));
        int const expected[]= {0x3884, 0xbfff, 0xdc80,0,0x20, 0xa000, 0xb430};
        if (val!=expected[i]) f << "f" << i << "=" << val << ",";
      }
      for (int i=0; i<8; ++i) {
        val=int(input->readULong(2));
        if (val) f << "f" << i+8 << "=" << val << ",";
      }
    }
    data.m_doubleValues[0]=m_mainParser->readDouble(*stream,vers<9 ? 4 : 8);
    f << "rad[min]=" << data.m_doubleValues[1] << ",";
    f << "angles=[";
    for (int i=0; i<2; ++i) { // 2 angles: pt0, min
      data.m_doubleValues[i+1]=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
      f << data.m_doubleValues[i+1] << ",";
    }
    f << "],";
    data.m_ngonType=int(input->readULong(4));
    if (data.m_ngonType!=4) f << "type=" << data.m_ngonType << ",";
    data.m_specials[0]=int(input->readLong(2));
    f << "N=" << data.m_specials[0] << ",";
    for (int i=0; i<(vers<9 ? 3 : 5); ++i) {
      val=int(input->readULong(2));
      int const expected[]= {vers<9 ? 0x207 : 0x3830, 0, 0, 0, 0};
      if (val!=expected[i]) f << "g" << i << "=" << val << ",";
    }
    break;
  }
  case 0x65666665: // effe: will be read by sendEffect
  case 0x45787472: // Extr: will be read by sendExtrude
    break;
  case 0x4772644d: { // GrdL
    if (len<4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a grid\n"));
      return false;
    }
    int subdiv[2];
    for (auto &d : subdiv) d=int(input->readULong(2));
    data.m_grid=MWAWVec2i(subdiv[0], subdiv[1]);
    f << "grid=" << data.m_grid << ",";
    break;
  }
  case 0x43436972: // CCir
  case 0x53504952: { // SPIR
    if (len<4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a ccircle/spiral\n"));
      return false;
    }
    for (int i=0; i<2; ++i) data.m_specials[i]=int(input->readLong(2));
    f << "N=" << data.m_specials[0] << ",";
    if (data.m_specials[1]) f << "space[between]=" << data.m_specials[1] << ","; // 0: equidistant (only used by CCir)
    break;
  }
  case 0x4d41434f: { // MACO: object from macros, never sent ?
    if (len<(vers<9 ? 92 : 128)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[MACO]: unexpected size\n"));
      return false;
    }
    val=int(input->readLong(4));
    if (val!=2)
      f << "f0=" << val << ",";
    if (vers>=9)
      input->seek(4, librevenge::RVNG_SEEK_CUR);
    float dim[4];
    for (auto &d : dim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
    if (vers<9)
      f << "box=" << MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2])) << ",";
    else
      f << "box=" << MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3])) << ",";
    for (int i=0; i<(vers<9 ? 13 : 17); ++i) { // f5=0|1
      val=int(input->readLong(4));
      if (val)
        f << "f" << i+1 << "=" << val << ",";
    }

    std::string sMaco;
    m_imageParser->readMacroIndent(*stream, data.m_macoId, sMaco);
    f << "id=[" << sMaco << "],";
    break;
  }
  case 0x4f4c6e6b : { // OLnk
    if (len<56) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a link\n"));
      return false;
    }
    f << "pts=[";
    for (int i=0; i<4; ++i) {
      float fDim[2];
      for (auto &d : fDim) d=float(input->readLong(4))/65536.f;
      data.m_vertices.push_back(MWAWVec2f(fDim[1], fDim[0]));
      f << data.m_vertices.back() << ",";
    }
    f << "],";
    for (int i=0; i<3; ++i) { // f0=small number, f1=0|5
      val=int(input->readLong(4));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    val=int(input->readLong(2)); // find 4,5,7
    if (val) f << "f3=" << val << ",";
    break;
  }
  case 0x706f626a:  // pobj
    if (len<8) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a pobj\n"));
      return false;
    }
    for (int i=0; i<2; ++i)
      data.m_specials[i]=int(input->readULong(4));
    if (data.m_specials[0])
      f << "B" << data.m_specials[1] << ":" << data.m_specials[0] << ",";
    else
      f << "B" << data.m_specials[1] << ",";
    break;
  case 0x54585420: // TEXT, only in Vkfl, will be read when we create the shape
    if (data.m_inMainZone) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected text in main zone\n"));
      return false;
    }
    break;
  case 0x41474946:  // AGIF: appear in v6
    if (len<12) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a AGIF\n"));
      return false;
    }
    for (int i=0; i<3; ++i)
      data.m_specials[i]=int(input->readULong(4));
    if (data.m_specials[0]!=1)
      f << "AG" << data.m_specials[1] << ":" << data.m_specials[0];
    else
      f << "AG" << data.m_specials[1];
    if (data.m_specials[2]!=1)
      f << "[" << data.m_specials[2] << "]";
    f << ",";
    break;
  case 0x43574542: { // CWEB: a button with 3 potential shapes (and sound)
    // checkme: find 8 times in two files, but with the same content...
    //          this zone is clearly related with RsrcWEBE (unparsed)
    if (len<40) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[CWEB]: unexpected size\n"));
      return false;
    }
    long pos=input->tell();
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(4));
      int const expected[]= {0x1261998, 1};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
    f << "lengths=[";
    std::vector<long> lengths;
    for (int i=0; i<8; ++i) { // 0-2: image, 3-5: sound, 6-7: unsure ???
      long len1=long(input->readLong(4));
      if (!len1 && i>=5) break;
      lengths.push_back(len1);
      f << std::hex << len1 << std::dec << ",";
    }
    f << "],";
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);

    libmwaw::DebugFile &ascFile = stream->ascii();
    long endPos=pos+len;
    for (size_t i=0; i<6; ++i) {
      if (i>=lengths.size())
        break;
      long l=lengths[i];
      if (l==0) continue;
      pos=input->tell();
      if (l < 0 || pos+l<pos || pos+l>endPos) {
        extra=f.str();
        ascFile.addPos(input->tell());
        ascFile.addNote("DataShap[CWEB]:###");
        return false;
      }
      if (i<3) { // image
        data.m_cweb[i].setBegin(pos);
        data.m_cweb[i].setLength(l);
      }
      else {
        // look like a basic snd file: see https://en.wikipedia.org/wiki/Au_file_format
        ascFile.addPos(pos);
        ascFile.addNote("DataShap[CWEB,snd]:##");
      }
      input->seek(pos+l, librevenge::RVNG_SEEK_SET);
    }
    if (input->tell()!=endPos) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[CWEB]: find extra data\n"));
      ascFile.addPos(input->tell());
      ascFile.addNote("DataShap:special,CWEB:###");
    }
    break;
  }
  case 0x516b546d:  // QkTm: appear in v6
    if (len!=4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a QkTm\n"));
      return false;
    }
    data.m_specials[0]=int(input->readULong(4));
    f << "QK" << data.m_specials[0] << ",";
    break;
  case 0x23476465: { // #Gde: text with column and section, appear in v6
    if (len<28) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[#Gde]: unexpected size\n"));
      return false;
    }
    auto fl=input->readULong(4);
    if (fl!=0x1771) f << "fl=" << std::hex << fl << std::dec << ",";
    data.m_gdeType=int(input->readULong(4));
    if (data.m_gdeType<=0 || data.m_gdeType>=4 || (data.m_gdeType==1 && len!=(vers<9 ? 52: 60))) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[#Gde]: unexpected type0\n"));
      f << "###type0=" << data.m_gdeType << "," << len << ",";
      extra=f.str();
      return false;
    }
    char const *wh[]= {"type1","type2","section/column"};
    f << wh[data.m_gdeType-1] << ",";
    switch (data.m_gdeType) {
    case 0: {
      float dims[2];
      for (auto &d : dims) d=float(m_mainParser->readDouble(*stream, 4));
      f << "dim=" << MWAWVec2f(dims[0], dims[1]) << ","; // 36x36 or 50x50
      for (auto &d : dims) d=float(m_mainParser->readDouble(*stream, 4));
      f << "unk=" << MWAWVec2f(dims[0], dims[1]) << ","; // 0.25x0.25..
      f << "unk1=" << std::hex << input->readULong(4) << std::dec << ",";
      break;
    }
    case 1: {
      for (int i=0; i<3; ++i) { // f0=0|1, f1=f2=0|1
        val=int(input->readULong(4));
        int const expected[]= {0, 1, 1};
        if (val!=expected[i]) f << "f" << i << "=" << val << ",";
      }
      if (vers>=9)
        input->seek(4, librevenge::RVNG_SEEK_CUR);

      float dims[2];
      for (auto &d : dims) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
      f << "dim=" << MWAWVec2f(dims[1], dims[0]) << ","; // 36x36 or 50x50
      if (vers<9)
        f << "unk0=" << std::hex << input->readULong(4) << std::dec << ",";
      for (auto &d : dims) d=float(m_mainParser->readDouble(*stream, 8));
      f << "unk=" << MWAWVec2f(dims[0], dims[1]) << ","; // 0.25, 0.25 or 1x1 except when unkn0=0
      f << "unk1=" << std::hex << input->readULong(4) << std::dec << ",";
      break;
    }
    case 2: {
      val=int(input->readULong(4)); // 0|1
      if (val) f << "f0=" << val << ",";
      int N=int(input->readULong(4));
      f << "N=" << N << ",";
      unsigned const headerSz=vers<9 ? 28 : 36;
      unsigned const dataSz=vers<9 ? 8 : 16;
      if (N<0 || int((len-headerSz)/dataSz)<N || len!=long(dataSz*unsigned(N)+headerSz)) {
        MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[#Gde]: can not read the N value\n"));
        f << "###";
        extra=f.str();
        return false;
      }
      f << "unk=[";
      for (int i=0; i<N; ++i) {
        val=int(input->readLong(4)); // 1|2
        if (vers>=9)
          input->seek(4, librevenge::RVNG_SEEK_CUR);
        f << m_mainParser->readDouble(*stream, vers<9 ? 4 : 8) << ":" << val << ",";
      }
      f << "],";
      f << "unk1=[";
      for (int i=0; i<(vers<9 ? 3 : 5); ++i)
        f << float(input->readLong(4))/65536 << ",";
      f << "],";
      break;
    }
    case 3:
    default: {
      int N=int(input->readLong(4));
      f << "N=" << N << ",";
      int const dataSz=vers<9 ? 100 : 120;
      if (N<0 || (len-28)/dataSz<N) {
        MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[#Gde]: can not read the N value\n"));
        f << "###";
        extra=f.str();
        return false;
      }
      libmwaw::DebugStream f2;
      libmwaw::DebugFile &ascFile = stream->ascii();
      for (int i=0; i<N; ++i) {
        long pos=input->tell();
        f2.str("");
        f2 << "DataShap[#Gde-S" << i << ":]";
        Canvas5GraphInternal::SectionData section;
        for (int j=0; j<4; ++j) {
          val=int(input->readLong(4));
          if (val)
            f2 << "f" << j << "=" << val << ",";
        }
        float dim[4];
        for (auto &d : dim) d=float(m_mainParser->readDouble(*stream, vers<9 ? 4 : 8));
        if (vers<9)
          section.m_bdBox=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
        else
          section.m_bdBox=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
        f2 << "box=" << section.m_bdBox << ",";
        if (vers>=9) {
          f2 << "unkn=" << m_mainParser->readDouble(*stream, 8) << ","; // 2
          val=int(input->readLong(4));
          if (val)
            f2 << "f2=" << val << ",";
        }
        long actPos=input->tell();
        std::string name;
        for (int j=0; j<28; ++j) { // checkme what is the bigger length
          char c=char(input->readULong(1));
          if (!c)
            break;
          name+=c;
        }
        f2 << name << ",";
        input->seek(actPos+28, librevenge::RVNG_SEEK_SET);
        for (int j=0; j<(vers<9 ? 7 : 6); ++j) { // f11=0|1
          val=int(input->readLong(4));
          if (val==(j<3 ? 1 : 0)) continue;
          if (j==0)
            f2 << "writing[mode]=" << val << ","; // 2: means first rigth columns then toward left
          else
            f2 << "f" << j+5 << "=" << val << ",";
        }
        if (vers<9) {
          val=int(input->readLong(4));
          if (val!=0x20000)
            f2 << "g0=" << float(val)/65536;
        }
        section.m_numColumns=int(input->readLong(4));
        f2 << "num[columns]=" << section.m_numColumns << ",";
        f2 << "id=" << input->readLong(4) << ",";
        data.m_sections.push_back(section);

        ascFile.addPos(pos);
        ascFile.addNote(f2.str().c_str());
        input->seek(pos+dataSz, librevenge::RVNG_SEEK_SET);
      }

      long pos=input->tell();
      f2.str("");
      f2 << "DataShap[#Gde-columns]:";
      if (vers<9) {
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(4))/65536;
        f2 << "orig=" << MWAWVec2f(dim[1],dim[0]) << ",";
      }
      else
        input->seek(8, librevenge::RVNG_SEEK_CUR);
      int N0=int(input->readLong(4));
      f2 << "num[columns]=" << N0 << ",";
      int const data1Sz=vers<9 ? 8 : 16;
      if (N0<0 || 28+dataSz*N+data1Sz*N0<len || (len-dataSz*N-28)/data1Sz<N0) {
        MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData[#Gde]: can not read the N0 value\n"));
        f2 << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f2.str().c_str());
        extra=f.str();
        return false;
      }
      f2 << "pos=[";
      for (int i=0; i<=2*N0; ++i)
        f2 << m_mainParser->readDouble(*stream, vers<9 ? 4 : 8) << ",";
      f2 << "],";
      ascFile.addPos(pos);
      ascFile.addNote(f2.str().c_str());
      break;
    }
    }
    break;
  }
  case 0x416e4766: // AnGf: appear in v7, will be parsed when we send data
    break;
  case 0x70636567:  // pceg: appear in v7
    if (len<8) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a pceg\n"));
      return false;
    }
    val=int(input->readULong(4));
    if (val!=0x3251999)
      f << "f0=" << std::hex << val << std::dec << ",";
    data.m_specials[1]=int(input->readULong(4));
    f << "PC" << data.m_specials[1] << ",";
    break;
  case 0x54656368: // Tech: appear in v7, will be parsed when we send data
    break;

  case 0x72656750: // regP: registration mark, appear in v8
    if (len<16) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected size for a regP\n"));
      return false;
    }
    for (int i=0; i<4; ++i) {
      val=int(input->readLong(4));
      int const expected[]= {0x7df, 0x700, 1, 1};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5Graph::readSpecialData: unexpected special %s\n", Canvas5Structure::getString(data.m_type).c_str()));
    return false;
  }
  extra=f.str();
  return true;
}

////////////////////////////////////////////////////////////

bool Canvas5Graph::readDeR3(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5StyleManager::StyleList &styles)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  int const vers=version();
  int const headerSize=vers<9 ? 124 : 160;
  if (!input->checkPosition(pos+headerSize)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: the zone is too short 1\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  int val;
  f << "Entries(DeR3):";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val!=(i==0 ? 2 : 0))
      f << "f" << i << "=" << val << ",";
  }
  unsigned name=unsigned(input->readULong(4));
  if (name!=0x44655233) { // "DeR3"
    MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: unexcepted header\n"));
    return false;
  }
  int nLines=0;
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(2));
    if (val==0) continue;
    if (i==2) {
      nLines=val;
      f << "n[lines]=" << val << ",";
    }
    else
      f << "f" << i+2 << "=" << val << ",";
  }
  unsigned long lengths[7], totalLength=0;
  f << "len=[";
  for (auto &l : lengths) {
    l=input->readULong(4);
    if (totalLength+l<totalLength) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: bad lengths\n"));
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    totalLength+=l;
    if (l)
      f << l << ",";
    else
      f << "_,";
    if (long(l)<0) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: a length is bad\n"));
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
  }
  f << "],";
  for (int i=0; i<(vers<9 ? 2 : 6); ++i) { // g0=0|12
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) { // dim0~=1 or dim2=0-100
    double dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (dVal<1 || dVal>1)
      f << "dim" << i << "=" << dVal << ",";
  }
  int nIntervs=int(input->readLong(4));
  if (nIntervs) f << "n[interv]=" << nIntervs << ",";
  if (nIntervs<0 || (int(lengths[3])<nIntervs*12)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: bad number of tabulations\n"));
    f << "###";
    nIntervs=0;
  }
  val=int(input->readLong(4));
  if (val!=10) f << "g2=" << val << ",";
  for (int i=0; i<30; ++i) { // g2=1-6, g3=-1, g28=0-466
    val=int(input->readLong(2));
    if (val) f << "g" << i+3 << "=" << val << ",";
  }
  int const widthSize=vers<9 ? 4 : 8;
  int const tabSize=vers<9 ? 12 : 24;
  if (pos+headerSize+long(totalLength)<pos+headerSize || !input->checkPosition(pos+headerSize+long(totalLength)) ||
      int(lengths[0])<4*(nLines+1) || int(lengths[1])<2*(nLines+1) || int(lengths[2])<widthSize*nLines ||
      (lengths[3]%12)!=0 || (int(lengths[4])%tabSize)!=0) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3: bad lengths\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+headerSize, librevenge::RVNG_SEEK_SET);

  if (lengths[0]) {
    pos=input->tell();
    f.str("");
    f << "DeR3-line:numChar=[";
    for (int i=0; i<=nLines; ++i) f << input->readULong(4) << ",";
    f << "],";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+long(lengths[0]), librevenge::RVNG_SEEK_SET);
  }

  if (lengths[1]) {
    pos=input->tell();
    f.str("");
    f << "DeR3-flags:fl=[";
    for (int i=0; i<=nLines; ++i) f << std::hex << input->readULong(2) << std::dec << ",";
    f << "],";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+long(lengths[1]), librevenge::RVNG_SEEK_SET);
  }

  if (lengths[2]) {
    pos=input->tell();
    f.str("");
    f << "DeR3-widths:w=[";
    for (int i=0; i<nLines; ++i) f << m_mainParser->readDouble(*stream, vers<9 ? 4 : 8) << ",";
    f << "],";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+long(lengths[2]), librevenge::RVNG_SEEK_SET);
  }

  long endPos=input->tell()+long(lengths[3]);
  for (int i=0; i<int(lengths[3]/12); ++i) {
    if (i>=nIntervs)
      break;
    pos=input->tell();
    f.str("");
    f << "DeR3-int" << i << ":";
    f << "len=" << float(input->readLong(4))/65536.f << ",";
    f << "type=" << input->readLong(2) << ",";
    val=int(input->readLong(2));
    if (val) f << "f1=" << val << ",";
    f << "pos=" << float(input->readLong(4))/65536.f << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=endPos) {
    ascFile.addPos(input->tell());
    ascFile.addNote("_");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  for (int i=0; i<int(lengths[4])/tabSize; ++i) {
    pos=input->tell();
    f.str("");
    f << "DeR3-tab" << i << ":";
    f << "pos=" << m_mainParser->readDouble(*stream, vers<9 ? 4: 8) << ",";
    f << "type=" << input->readLong(2) << ",";
    for (int j=0; j<(vers<9 ? 3 : 7); ++j) {
      val=int(input->readLong(2));
      if (val) f << "f" << j << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+tabSize, librevenge::RVNG_SEEK_SET);
  }

  if (lengths[5]) {
    pos=input->tell();
    f.str("");
    f<< "DeR3-A";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+long(lengths[5]), librevenge::RVNG_SEEK_SET);
  }

  if (!lengths[6]) return true;

  pos=input->tell();
  endPos=pos+long(lengths[6]);
  f.str("");
  f<< "Entries(ParaStyl),DeR3-style:";

  int N[2];
  f << "N=[";
  for (auto &n : N) {
    n=int(input->readULong(4));
    f << n << ",";
  }
  f << "],";
  f << "len=" << input->readULong(4) << ",";
  f << "max[tabs,sz]=" << int(input->readULong(4)) << ",";
  int styleSize=vers<9 ? 128 : 224;
  if (lengths[6]<40 || N[0]<0 || N[1]<0 || int(lengths[6]-40)/styleSize<N[0] ||
      long(N[0])*styleSize+40<40 || long(N[0])*styleSize+40>long(lengths[6])) {
    MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3[G]: bad N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->seek(pos+(vers<9 ? 28 : 32), librevenge::RVNG_SEEK_SET);
  styles.m_paragraphs.resize(size_t(N[0]));
  for (int i=0; i<N[0]; ++i) {
    pos=input->tell();
    f.str("");
    f<< "ParaStyl-E" << i+1 << ":";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    m_styleManager->readParaStyle(stream, i+1, &styles);
    input->seek(pos+styleSize, librevenge::RVNG_SEEK_SET);
  }

  for (int t=0; t<N[1]; ++t) {
    pos=input->tell();
    f.str("");
    f << "DeR3-Tab" << t+1 << ":";
    val=int(input->readULong(4));
    if (val!=1)
      f << "used=" << val << ",";
    int len=int(input->readULong(4));
    if (len<0 || pos+16+len > endPos) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3[G]: bad tab size\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      return true;
    }
    for (int i=0; i<4; ++i) { // 0
      val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    double dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (dVal<36 || dVal>36)
      f << "dim?=" << dVal << ",";
    int n=int(input->readULong(2));
    if (n) f << "N=" << n << ",";
    if (n<0 || len < (vers<9 ? 0 : 24) + tabSize*n) {
      MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3[G]: the num of tab seems bad\n"));
      f << "###";
      n=0;
    }
    for (int i=0; i<(vers<9 ? 3 : 7); ++i) { // 0
      val=int(input->readLong(2));
      if (val) f << "f" << i+4 << "=" << val << ",";
    }
    f << "tabs=[";
    std::vector<MWAWTabStop> tabs;
    tabs.resize(size_t(n));
    for (size_t i=0; i<size_t(n); ++i) {
      MWAWTabStop &tab=tabs[i];
      tab.m_position=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8)/72;
      int type=int(input->readULong(2));
      switch (type) {
      case 0: // left
        break;
      case 1:
        tab.m_alignment=MWAWTabStop::CENTER;
        break;
      case 2:
        tab.m_alignment=MWAWTabStop::RIGHT;
        break;
      case 3:
        tab.m_alignment=MWAWTabStop::DECIMAL;
        tab.m_decimalCharacter=',';
        break;
      case 4:
        tab.m_alignment=MWAWTabStop::DECIMAL;
        tab.m_decimalCharacter='\'';
        break;
      default:
        MWAW_DEBUG_MSG(("Canvas5Graph::readDeR3[G]: unknown tab type\n"));
        f << "###type=" << val << ",";
        break;
      }
      f << tab;
      for (int j=0; j<(vers<9 ? 3 : 7); ++j) { // v9: f2 is probably related to leader char but 0x30 mean 'a' ?
        val=int(input->readLong(2));
        if (val) f << ":f" << j << "=" << val << ",";
      }
      f << ",";
    }
    f << "],";
    if (!tabs.empty()) {
      for (auto &paraId : styles.m_paragraphs) {
        if (paraId.second==t+1)
          paraId.first.m_tabs=tabs;
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+16+len, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  if (pos!=endPos) { // checkme: list of 0
    ascFile.addPos(pos);
    ascFile.addNote("_");
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
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
bool Canvas5Graph::sendShape(int sId)
{
  return sendShape(sId, LocalState());
}

bool Canvas5Graph::sendShape(int sId, Canvas5Graph::LocalState const &local)
{
  auto const &it = m_state->m_idToShapeMap.find(sId);
  if (it==m_state->m_idToShapeMap.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendShape: can not find the shape %d\n", sId));
    return false;
  }
  if (m_state->m_sendIdSet.find(sId)!=m_state->m_sendIdSet.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendShape: loop detected for shape %d\n", sId));
    return false;
  }
  m_state->m_sendIdSet.insert(sId);
  bool res=send(it->second, local);
  m_state->m_sendIdSet.erase(sId);
  return res;
}

void Canvas5Graph::send(MWAWListenerPtr listener, MWAWGraphicShape const &shape, MWAWTransformation const &transform,
                        MWAWGraphicStyle const &style)
{
  if (!listener)
    return;
  MWAWGraphicShape fShape=shape;
  if (!transform.isIdentity())
    fShape=fShape.transform(transform);
  auto shapeBox=fShape.getBdBox();
  MWAWPosition pos(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  listener->insertShape(pos, fShape, style);
}

void Canvas5Graph::send(MWAWListenerPtr listener, librevenge::RVNGString const &text, MWAWVec2f const &center,
                        MWAWTransformation const &transform, MWAWFont const &font, bool addFrame)
{
  if (!listener || text.empty())
    return;

  MWAWPosition measurePos(center-MWAWVec2f(30,6), MWAWVec2f(60,12), librevenge::RVNG_POINT);
  measurePos.m_anchorTo = MWAWPosition::Page;
  std::shared_ptr<MWAWSubDocument> doc(new Canvas5GraphInternal::SubDocument(*this, MWAWInputStreamPtr(), text, font));

  MWAWGraphicStyle measureStyle;
  measureStyle.m_lineWidth=addFrame ? 1 : 0;
  measureStyle.setSurfaceColor(MWAWColor::white());

  MWAWTransformation transf;
  float rotation=0;
  MWAWVec2f shearing;
  if (!transform.isIdentity() && transform.decompose(rotation,shearing,transf,center)) {
    auto shapeBox=transf*MWAWBox2f(center-MWAWVec2f(30,6),center+MWAWVec2f(30,6));
    measurePos.setOrigin(shapeBox[0]);
    measurePos.setSize(shapeBox[1]-shapeBox[0]);
    measureStyle.m_rotate=-rotation;
  }
  listener->insertTextBox(measurePos, doc, measureStyle);
}

bool Canvas5Graph::send(Canvas5GraphInternal::Shape const &shape, Canvas5Graph::LocalState const &lTransform)
{
  auto listener=m_parserState->m_graphicListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("Canvas5Graph::send[shape]: can not find the listener\n"));
    return false;
  }
  int const vers=version();
  auto const &shapeIt=m_state->m_posToShapeDataMap.find(shape.m_pos);
  bool hasShapeId=shapeIt!=m_state->m_posToShapeDataMap.end();
  MWAWPosition pos(shape.m_initialBox[0], shape.m_initialBox.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  LocalState local(pos, lTransform.m_style);
  local.m_transform=lTransform.m_transform;
  if (hasShapeId && shapeIt->second.m_ids[1]) {
    auto const &matIt=m_state->m_idToMatrices.find(int(shapeIt->second.m_ids[1]));
    if (matIt==m_state->m_idToMatrices.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[shape]: can not find the matrix %d\n", int(shapeIt->second.m_ids[1])));
    }
    else
      local.multiplyMatrix(matIt->second[0]);
  }
  if (shape.m_values[1])
    m_styleManager->updateSurfaceColor(shape.m_values[1], local.m_style);
  if (shape.m_values[2])
    m_styleManager->updateLineColor(shape.m_values[2], local.m_style);
  int numLines=1;
  if (shape.m_values[3])
    m_styleManager->updateLineStyle(shape.m_values[3], local.m_style, numLines);
  MWAWGraphicShape finalShape;
  switch (shape.m_type) {
  case 2: {
    if (!hasShapeId || !shapeIt->second.m_stream) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[text]: can not find the text zone\n"));
      return false;
    }
    local.m_style.m_lineWidth=0;
    std::shared_ptr<MWAWSubDocument> doc(new Canvas5GraphInternal::SubDocument(*this, shapeIt->second.getStream().input(), shape, shapeIt->second));
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,shape.m_initialBox.center())) {
      MWAWBox2f box=transf*shape.m_initialBox;
      pos.setOrigin(box[0]);
      pos.setSize(box[1]-box[0]);
      MWAWGraphicStyle style(local.m_style);
      style.m_rotate=-rotation;
      listener->insertTextBox(pos, doc, style);
    }
    else
      listener->insertTextBox(pos, doc, local.m_style);
    return true;
  }
  case 3: {
    auto const &data=shapeIt->second;
    if (data.m_vertices.size()==2)
      finalShape=MWAWGraphicShape::line(data.m_vertices[0], data.m_vertices[1]);
    else
      finalShape=MWAWGraphicShape::line(shape.m_initialBox[0], shape.m_initialBox[1]);
    break;
  }
  case 4:
    finalShape=MWAWGraphicShape::rectangle(shape.m_initialBox);
    break;
  case 5:
    if (!hasShapeId) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[rectOval]: can not find the oval size\n"));
      return false;
    }
    if (vers<9)
      finalShape=MWAWGraphicShape::rectangle
                 (shape.m_initialBox, 1/65536.f*MWAWVec2f(float(shapeIt->second.m_local[0])/2,float(shapeIt->second.m_local[1])/2));
    else
      finalShape=MWAWGraphicShape::rectangle
                 (shape.m_initialBox, MWAWVec2f(float(shapeIt->second.m_doubleValues[0])/2,float(shapeIt->second.m_doubleValues[1])/2));
    break;
  case 6:
    finalShape=MWAWGraphicShape::circle(shape.m_initialBox);
    break;
  case 7: { // arc
    if (!hasShapeId) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[arc]: can not find the angle\n"));
      return false;
    }
    auto const &data=shapeIt->second;
    float angles[]= {vers<9 ? float(data.m_local[0])/65536 : float(180/M_PI *data.m_doubleValues[1]),
                     vers<9 ? float(data.m_local[1])/65536 : float(180/ M_PI *data.m_doubleValues[0])
                    };
    int angle[2] = { int(90-angles[0]-angles[1]), int(90-angles[1]) };
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
    MWAWBox2f circleBox=shape.m_initialBox;
    // we have the shape box, we need to reconstruct the circle box
    if (maxVal[0]>minVal[0] && maxVal[1]>minVal[1]) {
      float scaling[2]= { (shape.m_initialBox[1][0]-shape.m_initialBox[0][0])/(maxVal[0]-minVal[0]),
                          (shape.m_initialBox[1][1]-shape.m_initialBox[0][1])/(maxVal[1]-minVal[1])
                        };
      float constant[2]= { shape.m_initialBox[0][0]-minVal[0] *scaling[0], shape.m_initialBox[0][1]-minVal[1] *scaling[1]};
      circleBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                          MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
    }
    finalShape = MWAWGraphicShape::pie(shape.m_initialBox, circleBox, MWAWVec2f(float(angle[0]), float(angle[1])));
    break;
  }
  case 9: { // polyline
    if (!hasShapeId || shapeIt->second.m_vertices.size()<2) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[spline]: can not find the point\n"));
      return false;
    }
    if (local.m_style.hasSurface())
      finalShape=MWAWGraphicShape::polygon(shape.m_initialBox);
    else
      finalShape=MWAWGraphicShape::polyline(shape.m_initialBox);
    finalShape.m_vertices=shapeIt->second.m_vertices;
    break;
  }
  case 10: { // spline
    if (!hasShapeId || shapeIt->second.m_vertices.size()<2 || (shapeIt->second.m_vertices.size()%4)!=0) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[spline]: can not find the point\n"));
      return false;
    }
    finalShape=MWAWGraphicShape::path(shape.m_initialBox);
    std::vector<MWAWGraphicShape::PathData> &path=finalShape.m_path;
    path.push_back(MWAWGraphicShape::PathData('M', shapeIt->second.m_vertices[0]));
    for (size_t p=3; p < shapeIt->second.m_vertices.size(); p+=4) {
      if (p>=4 && shapeIt->second.m_vertices[p-4]!=shapeIt->second.m_vertices[p-3])
        path.push_back(MWAWGraphicShape::PathData('M', shapeIt->second.m_vertices[p-3]));
      bool hasFirstC=shapeIt->second.m_vertices[p-3]!=shapeIt->second.m_vertices[p-2];
      bool hasSecondC=shapeIt->second.m_vertices[p-1]!=shapeIt->second.m_vertices[p];
      if (!hasFirstC && !hasSecondC)
        path.push_back(MWAWGraphicShape::PathData('L', shapeIt->second.m_vertices[p]));
      else
        path.push_back(MWAWGraphicShape::PathData('C', shapeIt->second.m_vertices[p], shapeIt->second.m_vertices[p-2], shapeIt->second.m_vertices[p-1]));
    }
    if (local.m_style.hasSurface())
      path.push_back(MWAWGraphicShape::PathData('Z'));
    break;
  }
  case 52:
    if (!hasShapeId) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[special]: can not find the special data\n"));
      return false;
    }
    if (numLines!=1) {
      // even if this is possible, using mutiple line on special give really weird results
      MWAW_DEBUG_MSG(("Canvas5Graph::send[special]: find a special with multi lines\n"));
      m_styleManager->updateLineStyle(shape.m_values[3], local.m_style, numLines, 0);
    }
    return sendSpecial(listener, shape, shapeIt->second, local);
  case 20:
  case 99:
    if (!hasShapeId) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[group]: can not find the child shape\n"));
      return false;
    }
    if (shapeIt->second.m_children.empty())
      return true;
    if (shape.m_type==99)
      local.m_style=MWAWGraphicStyle::emptyStyle();
    listener->openGroup(pos);
    for (auto cId : shapeIt->second.m_children)
      sendShape(int(cId), local);
    listener->closeGroup();
    return true;
  default:
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("Canvas5Graph::send[shape]: sorry, not implemented[%d]\n", shape.m_type));
      first=false;
    }
    return false;
  }
  if (!local.m_transform.isIdentity()) {
    finalShape=finalShape.transform(local.m_transform);
    MWAWBox2f shapeBox=finalShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
  }
  if (shape.m_values[3]==0 || numLines==1) {
    listener->insertShape(pos, finalShape, local.m_style);
    return true;
  }
  listener->openGroup(pos);
  auto style=local.m_style;
  style.m_lineWidth=0;
  listener->insertShape(pos, finalShape, style);
  style=MWAWGraphicStyle::emptyStyle();
  if (shape.m_values[2])
    m_styleManager->updateLineColor(shape.m_values[2], style);
  auto path=finalShape.getPath(true);
  for (int l=0; l<numLines; ++l) {
    float offset;
    m_styleManager->updateLineStyle(shape.m_values[3], style, numLines, l, &offset);
    MWAWBox2f decalBox;
    auto decalPath=MWAWGraphicShape::offsetVertices(path, offset, decalBox);
    auto decalShape=MWAWGraphicShape::path(decalBox);
    decalShape.m_path=decalPath;
    pos=MWAWPosition(decalBox[0], decalBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->insertShape(pos, decalShape, style);
  }
  listener->closeGroup();
  return true;
}

bool Canvas5Graph::sendSpecial(MWAWListenerPtr listener, Canvas5GraphInternal::PseudoShape const &pseudoShape, LocalState const &local)
{
  return sendSpecial(listener, pseudoShape.m_shape, pseudoShape.m_data, local);
}

bool Canvas5Graph::sendSpecial(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                               Canvas5Graph::LocalState const &local)
{
  if (!data.m_stream)
    return false;
  if (!listener) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: can not find the listener\n"));
    return false;
  }
  int const vers=version();
  auto &stream=data.getStream();
  MWAWGraphicShape fShape;
  auto const &box=shape.m_initialBox;
  switch (data.m_type) {
  case 0x43436972: { // CCir
    if (data.m_specials[0]<=0 || data.m_specials[0]>20) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, the number of circles seems bad\n"));
      return false;
    }
    listener->openGroup(local.m_position);
    MWAWVec2f center=0.5f*(box[0]+box[1]);
    MWAWVec2f diag=0.5f*box.size();
    for (int i=0; i<data.m_specials[0]; ++i) {
      MWAWVec2f newDiag;
      if (data.m_specials[1]<=0)
        newDiag=float(data.m_specials[0]-i)/float(data.m_specials[0])*diag;
      else {
        newDiag=diag-float(data.m_specials[1]*i)*MWAWVec2f(1,1);
        for (int c=0; c<2; ++c) {
          if (newDiag[c]<0)
            newDiag[c]=0;
        }
      }
      fShape=MWAWGraphicShape::circle(MWAWBox2f(center-newDiag, center+newDiag));
      send(listener, fShape, local.m_transform, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 0x43756265: { // Cube
    if (data.m_vertices.size()!=8) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: can not find the cube vertices\n"));
      return false;
    }
    int const faces[]= {
      0, 2, 6, 4, // X==0
      1, 3, 7, 5, // X==1
      0, 1, 5, 4, // Y==0
      2, 3, 7, 6, // Y==1
      0, 1, 3, 2, // Z==0
      4, 5, 7, 6, // Z==1
    };
    listener->openGroup(local.m_position);
    fShape.m_type=local.m_style.hasSurface() ? MWAWGraphicShape::Polygon : MWAWGraphicShape::Polyline;
    MWAWVec2f const dir=box[1]-box[0];
    MWAWVec2f const dirs[]= {data.m_vertices[1]-data.m_vertices[0],
                             data.m_vertices[2]-data.m_vertices[0],
                             data.m_vertices[4]-data.m_vertices[0]
                            };
    int wh=(dirs[0][0]*dirs[2][1]-dirs[0][1]*dirs[2][0]>0) ? 0 : 1;
    wh+=(dirs[1][0]*dirs[2][1]-dirs[1][1]*dirs[2][0]>0) ? 0 : 2;
    if (dirs[0][0]*dirs[1][1]-dirs[0][1]*dirs[1][0]>0 && (wh==0 || wh==3)) wh=3-wh;

    for (int f=0; f<3; ++f) {
      size_t face;
      switch (f) {
      case 0:
        face=4;
        break;
      case 1:
        face = (wh==0 || wh==1) ? 2 : 3;
        break;
      default:
        face = (wh==0 || wh==2) ? 1 : 0;
        break;
      }

      MWAWBox2f shapeBox;
      fShape.m_vertices.resize(4);
      for (size_t p=0; p<4; ++p) {
        MWAWVec2f const &pt=data.m_vertices[size_t(faces[4*face+p])];
        fShape.m_vertices[p]=box[0]+MWAWVec2f(pt[0]*dir[0], pt[1]*dir[1]);
      }
      fShape.m_bdBox=shapeBox;
      send(listener, fShape, local.m_transform, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 0x43765465: // CvTe
    return sendCurveText(listener, shape, data, local);
  case 0x44494d4e: // DIMN
    if (vers<9)
      return sendDimension(listener, shape, data, local);
    return sendDimension9(listener, shape, data, local);
  case 0x65666665: // eff
    return sendEffect(listener, shape, data, local);
  case 0x45787472: // Extr: extrude
    return sendExtrude(listener, shape, data, local);
  case 0x4772644d: { // GrdL
    listener->openGroup(local.m_position);
    if (data.m_grid[0]<=0 || data.m_grid[1]<=0 ||
        data.m_grid[0]>100 || data.m_grid[1]>100) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial[grid]: can not find the number of rows/columns\n"));
      return false;
    }
    MWAWVec2f dim((box[1][0]-box[0][0])/float(data.m_grid[0]),
                  (box[1][1]-box[0][1])/float(data.m_grid[1]));
    for (int i=0; i<=data.m_grid[0]; ++i) {
      float X=box[0][0]+float(i)*dim[0];
      fShape=MWAWGraphicShape::line(MWAWVec2f(X,box[0][1]), MWAWVec2f(X,box[1][1]));
      send(listener, fShape, local.m_transform, local.m_style);
    }
    for (int j=0; j<=data.m_grid[1]; ++j) {
      float Y=box[0][1]+float(j)*dim[1];
      fShape=MWAWGraphicShape::line(MWAWVec2f(box[0][0],Y), MWAWVec2f(box[1][0],Y));
      send(listener, fShape, local.m_transform, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 0x4e474f4e: { // NGON
    if (data.m_specials[0]<=2 || data.m_specials[0]>=50) { // find N=33
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, the number of ngon seems bad\n"));
      return false;
    }
    int type=data.m_ngonType;
    if (type<0 || type>5) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, the ngon's seems bad, assume 4\n"));
      type=4;
    }
    bool needGroup=type!=0 && type!=3 && type!=4;
    if (needGroup)
      listener->openGroup(local.m_position);
    MWAWVec2f const center=0.5f*(box[0]+box[1]);
    MWAWVec2f const diag=0.5f*box.size();
    double const angles[]= {M_PI/2-data.m_doubleValues[2], M_PI/2-M_PI/data.m_specials[0]};
    double const step=-2*M_PI/data.m_specials[0];
    float const rad=float(data.m_doubleValues[0]);
    if (type==0 || type==1 || type==5) { // border
      fShape=MWAWGraphicShape::polygon(box);
      for (int i=0; i<data.m_specials[0]; ++i) {
        float const angle1=float(angles[0]+i*step);
        fShape.m_vertices.push_back(center+MWAWVec2f(std::cos(angle1)*diag[0], -std::sin(angle1)*diag[1]));
      }
      send(listener, fShape, local.m_transform, local.m_style);
    }
    if (type==1 || type==4) { // outside star
      fShape=MWAWGraphicShape::polygon(box);
      for (int i=0; i<data.m_specials[0]; ++i) {
        float const angle1=float(angles[0]+i*step);
        fShape.m_vertices.push_back(center+MWAWVec2f(std::cos(angle1)*diag[0], -std::sin(angle1)*diag[1]));
        float const angle2=float(angles[1]+i*step);
        fShape.m_vertices.push_back(center+MWAWVec2f(rad*std::cos(angle2)*diag[0], -rad*std::sin(angle2)*diag[1]));
      }
      send(listener, fShape, local.m_transform, local.m_style);
    }
    if (type==3) { // inside star
      fShape=MWAWGraphicShape::polygon(box);
      int id=0;
      for (int i=0; i<data.m_specials[0]; ++i) {
        float const angle1=float(angles[0]+id*step);
        fShape.m_vertices.push_back(center+MWAWVec2f(std::cos(angle1)*diag[0], -std::sin(angle1)*diag[1]));
        id+=(data.m_specials[0]-1+i%2)/2;
      }
      send(listener, fShape, local.m_transform, local.m_style);
    }
    if (type==2 || type==5) { // list of segment: center->outside points
      for (int i=0; i<data.m_specials[0]; ++i) {
        float const angle1=float(angles[0]+i*step);
        fShape=MWAWGraphicShape::line(center, center+MWAWVec2f(std::cos(angle1)*diag[0], -std::sin(angle1)*diag[1]));
        send(listener, fShape, local.m_transform, local.m_style);
      }
    }
    if (needGroup)
      listener->closeGroup();
    break;
  }
  case 0x4f4c6e6b : { // OLnk
    if (data.m_vertices.size()<2) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, can not find the link extremities\n"));
      return false;
    }
    fShape=MWAWGraphicShape::line(data.m_vertices[0], data.m_vertices[1]);
    send(listener, fShape, local.m_transform, local.m_style);
    break;
  }
  case 0x4d41434f: { // MACO
    auto sIt=m_state->m_sendMACOIdSet.find(data.m_macoId);
    if (sIt!=m_state->m_sendMACOIdSet.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry find a loop sending MACO\n"));
      break;
    }
    auto maco=m_imageParser->getMACO(data.m_macoId);
    if (!maco)
      break;

    m_state->m_sendMACOIdSet.insert(data.m_macoId);
    listener->openGroup(local.m_position);
    m_imageParser->send(maco, listener, shape.m_initialBox, local.m_transform);
    listener->closeGroup();
    m_state->m_sendMACOIdSet.erase(data.m_macoId);
    break;
  }
  case 0x706f626a: { // pobj
    MWAWEmbeddedObject bitmap;
    if (!m_imageParser->getBitmap(data.m_specials[1], bitmap))
      return false;
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,shape.m_initialBox.center())) {
      MWAWBox2f shapeBox=transf*shape.m_initialBox;
      MWAWPosition pos(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      MWAWGraphicStyle style(local.m_style);
      style.m_rotate=-rotation;
      listener->insertPicture(pos, bitmap, style);
    }
    else
      listener->insertPicture(local.m_position, bitmap, local.m_style);
    break;
  }
  case 0x53504952: { // SPIR
    if (data.m_specials[0]<=0) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, the number of spirals seems bad\n"));
      return false;
    }
    fShape=MWAWGraphicShape::path(box);
    auto const center = box.center();
    auto const dir = 0.5f/4/float(data.m_specials[0]) * box.size();
    std::vector<MWAWGraphicShape::PathData> &path=fShape.m_path;
    auto pt=center;
    path.push_back(MWAWGraphicShape::PathData('M', center));
    for (int i=1; i<=4*data.m_specials[0]; ++i) {
      auto nextPt=center;
      nextPt[(i&1)] += ((i%4)<2 ? 1 : -1)*float(i)*dir[(i&1)];
      MWAWVec2f l;
      l[1-(i&1)]=pt[1-(i&1)];
      l[(i&1)]=nextPt[(i&1)];
      path.push_back(MWAWGraphicShape::PathData('Q', nextPt, l));
      pt=nextPt;
    }
    send(listener, fShape, local.m_transform, local.m_style);
    break;
  }
  case 0x43574542: { // CWEB: ie a button with 3 state
    auto input=stream.input();
    bool send=false;
    for (auto const &e : data.m_cweb) {
      if (!e.valid()) continue;
      input->seek(e.begin(), librevenge::RVNG_SEEK_SET);
      input->pushLimit(e.end());
      bool ok=true;
      std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
      if (!m_imageParser->readVKFL(data.m_stream, e.length(), image)) {
        auto &ascFile=stream.ascii();
        ok=false;
        ascFile.addPos(e.begin());
        ascFile.addNote("DataShap:special,image:###");
      }
      else if (!send) {
        send=true;
        listener->openGroup(local.m_position);
        m_imageParser->send(image, listener, shape.m_initialBox, local.m_transform);
        listener->closeGroup();
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecialData[button]: send only the first picture (instead of the three state pictures)\n"));
          first=false;
        }
      }
      input->popLimit();
      if (!ok)
        continue;
#ifndef DEBUG
      break;
#endif
    }
    break;
  }
  case 0x54585420: { // TXT : only in Vkfl
    if (data.m_inMainZone) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecialData: unexpected text in main zone\n"));
      return false;
    }
    auto lStyle=local.m_style;
    lStyle.m_lineWidth=0;
    std::shared_ptr<MWAWSubDocument> doc(new Canvas5GraphInternal::SubDocument(*this, stream.input(), shape, data));
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,shape.m_initialBox.center())) {
      MWAWBox2f shapeBox=transf*shape.m_initialBox;
      MWAWPosition pos(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      lStyle.m_rotate=-rotation;
      listener->insertTextBox(pos, doc, lStyle);
    }
    else
      listener->insertTextBox(local.m_position, doc, lStyle);
    break;
  }
  case 0x41474946: { // AGIF appear in v6
    auto sIt=m_state->m_sendAGIFIdSet.find(data.m_specials[1]);
    if (sIt!=m_state->m_sendAGIFIdSet.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry find a loop sending AGIF\n"));
      break;
    }
    auto gif=m_imageParser->getGIF(data.m_specials[1]);
    if (!gif)
      break;

    m_state->m_sendAGIFIdSet.insert(data.m_specials[1]);
    listener->openGroup(local.m_position);
    m_imageParser->send(gif, listener, shape.m_initialBox, local.m_transform);
    listener->closeGroup();
    m_state->m_sendAGIFIdSet.erase(data.m_specials[1]);
    break;
  }
  case 0x516b546d: { // QkTm
    MWAWEmbeddedObject movie;
    if (!m_imageParser->getQuickTime(data.m_specials[0], movie))
      return false;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial[QkTm]: this file contains movie, there will be probably illisible\n"));
      first=false;
    }
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,shape.m_initialBox.center())) {
      MWAWBox2f shapeBox=transf*shape.m_initialBox;
      MWAWPosition pos(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      MWAWGraphicStyle style(local.m_style);
      style.m_rotate=-rotation;
      listener->insertPicture(pos, movie, style);
    }
    else
      listener->insertPicture(local.m_position, movie, local.m_style);
    break;
  }
  case 0x23476465: { // #Gde: appear in v6
    if (data.m_gdeType!=3)
      return true;
    for (auto const &sect : data.m_sections) {
      // add only a basic frame, todo: add also a frame to separate column
      fShape=MWAWGraphicShape::rectangle(sect.m_bdBox);
      MWAWGraphicStyle basicStyle;
      basicStyle.m_lineColor=MWAWColor(127,127,255);
      basicStyle.m_lineWidth=0.5f;
      send(listener, fShape, local.m_transform, basicStyle);
    }
    break;
  }
  case 0x416e4766: // AnGf: appear in v7
    return sendGIF(listener, shape, data, local);
  case 0x54656368: // Tech: appear in v7
    return sendTechnical(listener, shape, data, local);
  case 0x494d534c: { // SIML: a slice mask?, appear in v8
    fShape=MWAWGraphicShape::rectangle(shape.m_initialBox);
    MWAWGraphicStyle basicStyle;
    basicStyle.m_lineColor=MWAWColor(250,128,114);
    basicStyle.m_lineWidth=0.5f;
    basicStyle.setSurfaceColor(MWAWColor(255,255,0),0.5);
    send(listener, fShape, local.m_transform, basicStyle);
    break;
  }
  case 0x72656750: { // regP: appear in v8
    listener->openGroup(local.m_position);
    MWAWBox2f const &shapeBox=shape.m_initialBox;
    auto const center=shapeBox.center();
    MWAWGraphicStyle basicStyle;
    for (int i=0; i<3; ++i) {
      switch (i) {
      case 0: {
        MWAWBox2f cBox=shapeBox;
        cBox.resizeFromCenter(0.5f*shapeBox.size());
        fShape=MWAWGraphicShape::circle(cBox);
        break;
      }
      case 1:
        fShape=MWAWGraphicShape::line(MWAWVec2f(shapeBox[0][0],center[1]), MWAWVec2f(shapeBox[1][0],center[1]));
        break;
      case 2:
      default:
        fShape=MWAWGraphicShape::line(MWAWVec2f(center[0],shapeBox[0][1]), MWAWVec2f(center[0],shapeBox[1][1]));
        break;
      }
      send(listener, fShape, local.m_transform, basicStyle);
    }
    listener->closeGroup();
    break;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5Graph::sendSpecial: sorry, sending %s is not implemented\n", Canvas5Structure::getString(data.m_type).c_str()));
    return false;
  }
  return true;
}

bool Canvas5Graph::sendText(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &/*shape*/, Canvas5GraphInternal::ShapeData const &data)
{
  if (!data.m_stream)
    return false;

  if (!listener) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  int const vers=version();
  libmwaw::DebugStream f;
  libmwaw::DebugFile &ascFile = stream.ascii();
  auto input=stream.input();
  auto entry=data.m_entry;
  MWAWEntry fontEntry;
  if (!data.m_inMainZone) {
    if (!entry.valid() || entry.length()<16 || !input->checkPosition(entry.end())) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendText: can not find the text entry\n"));
      return false;
    }

    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    long pos=input->tell();
    f << "Text[zones]:";
    MWAWEntry childs[2];
    for (auto &c : childs) {
      c.setBegin(entry.begin()+input->readLong(4));
      c.setLength(input->readLong(4));
      if (c.begin()<entry.begin() || c.end()>entry.end()) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendText: can not find the main child entry\n"));
        f << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return false;
      }
      f << std::hex << c.begin() << "<->" << c.end() << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    entry=childs[0];
    fontEntry=childs[1];
    ascFile.addPos(fontEntry.begin());
    ascFile.addNote("Text[fonts]:");
  }
  if (!entry.valid() || entry.length()<20+5*4 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: can not find the text entry\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  long pos=input->tell();
  f.str("");
  f << "Entries(Text):";
  int val;
  for (int i=0; i<7; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {1,0,0xc,0,0,0,1};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  val=int(input->readULong(2));
  if (val!=0x7071) f << "fl=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(2));
  MWAWParagraph para;
  switch (val) {
  case -1:
    para.m_justify=MWAWParagraph::JustificationRight;
    f << "right,";
    break;
  case 0: // left
    break;
  case 1:
    para.m_justify=MWAWParagraph::JustificationCenter;
    f << "center,";
    break;
  case 4:
    para.m_justify=MWAWParagraph::JustificationFull;
    f << "justify,";
    break;
  default:
    f << "#align=" << val << ",";
  }
  val=int(input->readLong(2));
  if (val) f << "f7=" << val << ",";
  unsigned long lengths[5], totalLength=0;
  f << "len=[";
  for (auto &l : lengths) {
    l=input->readULong(4);
    if (totalLength+l<totalLength) {
      f << "###";
      MWAW_DEBUG_MSG(("Canvas5Graph::sendText: bad lengths\n"));
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    totalLength+=l;
    if (l)
      f << l << ",";
    else
      f << "_,";
  }
  f << "],";
  if (pos+24+5*4+long(totalLength)<pos+24+20 || pos+24+5*4+long(totalLength)>=entry.end()) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: bad lengths\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  MWAWEntry textEntry;
  textEntry.setBegin(pos);
  textEntry.setLength(int(lengths[0]));
  input->seek((lengths[0]&1) ? long(lengths[0])+1 : long(lengths[0]), librevenge::RVNG_SEEK_CUR);

  if (lengths[1]) {
    ascFile.addPos(input->tell());
    ascFile.addNote("Text-Unkn:");
    input->seek(long(lengths[1]), librevenge::RVNG_SEEK_CUR);
  }
  bool ok=true;
  if (lengths[2]<8) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: length 2 seems too short\n"));
    ok=false;
  }

  std::map<int,int> posToFontIdMap;
  if (ok) {
    pos=input->tell();
    f.str("");
    f << "Text-plc:";
    int N0=int(input->readLong(2));
    if (N0!=1) f << "f0=" << N0 << ",";
    int N=int(input->readULong(2));
    f << "numPLC=" << N << ",";
    if (int(lengths[2])<20+N0*8) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendText: length 2 seems bad\n"));
      f << "###";
    }
    else {
      for (int i=0; i<8; ++i) { // 0
        val=int(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      f << "plcs=[";
      for (int i=0; i<N0; ++i) {
        int posi=int(input->readULong(4));
        int id=int(input->readULong(2));
        val=int(input->readLong(2)); // almost always 0, but find 4447|a0 ?
        f << posi << ":" << id;
        if (val) f << "[" << val << "]";
        f << ",";
        posToFontIdMap[posi]=id;
      }
      f << input->readULong(4);
      f << "],";
    }
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+long(lengths[2]), librevenge::RVNG_SEEK_SET);
  }

  int const styleSz=vers<9 ? 60 : 96;
  if (ok && (int(lengths[4])%styleSz)!=0) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: length 4 seems bads\n"));
    ok=false;
  }

  Canvas5StyleManager::StyleList styles;
  if (ok) {
    int N=int(lengths[4])/styleSz;
    styles.m_fonts.resize(size_t(N));
    for (int n=0; n<N; ++n) {
      pos=input->tell();
      m_styleManager->readCharStyle(stream, n, styles.m_fonts[size_t(n)], data.m_inMainZone);
      input->seek(pos+styleSz, librevenge::RVNG_SEEK_SET);
    }
  }

  if (ok && (lengths[3]%16)) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendText: length 3 seems bads\n"));
    ok=false;
  }

  if (ok) {
    int N=int(lengths[3]/16);
    for (int n=0; n<N; ++n) {
      // unsure maybe
      // before v9 n*[2 double:int4 + 32 bytes]
      // in v9 n*[2 double + 32 bytes] + n*[2 int]
      pos=input->tell();
      f.str("");
      f << "Text-A" << n << ":";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    }

    pos=input->tell();
    if (!readDeR3(data.m_stream, styles)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      ok=false;
    }
  }

  pos=input->tell();
  input->seek(textEntry.begin(), librevenge::RVNG_SEEK_SET);
  f.str("");
  f << "Text-text:";
  listener->setParagraph(para);

  int linkId=0;
  bool linkIsOpen=false;
  for (int n=0; n<int(lengths[0]); ++n) {
    auto it=posToFontIdMap.find(n);
    if (it!=posToFontIdMap.end()) {
      if (it->second<0 || it->second>=int(styles.m_fonts.size())) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendText: can not read find the font=%d\n", it->second));
      }
      else {
        auto const &font=styles.m_fonts[size_t(it->second)];
        if (font.m_paragraphId>0 && size_t(font.m_paragraphId)<styles.m_paragraphs.size())
          listener->setParagraph(styles.m_paragraphs[size_t(font.m_paragraphId)].first);
        listener->setFont(font.m_font);
        if (font.m_linkId!=linkId) {
          if (linkIsOpen) {
            listener->closeLink();
            linkIsOpen=false;
          }
          linkId=font.m_linkId;
          if (linkId) {
            auto ref=m_mainParser->getTextLink(linkId);
            if (!ref.empty()) {
              MWAWLink link;
              link.m_HRef=ref.cstr();
              listener->openLink(link);
              linkIsOpen=true;
            }
          }
        }
      }
    }
    unsigned char c=(unsigned char)(input->readULong(1));
    f << c;
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      if (linkIsOpen) {
        listener->closeLink();
        linkIsOpen=false;
      }
      listener->insertEOL();
      break;
    default:
      if (c<=0x1f) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendText: find unexpected char=%x\n", (unsigned int)(c)));
      }
      else
        listener->insertCharacter(c);
    }
  }
  if (linkIsOpen)
    listener->closeLink();
  ascFile.addPos(textEntry.begin());
  ascFile.addNote(f.str().c_str());
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return ok;
}

bool Canvas5Graph::sendEffect(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape,
                              Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendEffect: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  libmwaw::DebugFile &ascFile = stream.ascii();
  if (!entry.valid() || entry.length()<8 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendEffect: can not find the number of zone\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Effect):";
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(i==0 ? 4 : 2));
    if (val!=1-i) f << "f" << i << "=" << val << ",";
  }
  int N=int(input->readULong(2));
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (N>2)
    listener->openGroup(local.m_position);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Effect-" << i << ":";
    long dLen=input->readLong(4);
    f << "sz=" << dLen << ",";
    long endPos=pos+4+dLen;
    if (endPos<pos+4 || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendEffect: the length seems bad\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
    if (m_imageParser->readVKFL(data.m_stream, dLen, image) && image)
      m_imageParser->send(image, listener, shape.m_initialBox, local.m_transform);
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  if (N>2)
    listener->closeGroup();
  return true;
}

bool Canvas5Graph::sendExtrude(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape,
                               Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendExtrude: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  libmwaw::DebugFile &ascFile = stream.ascii();
  if (!entry.valid() || entry.length()<1000+48 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendExtrude: can not find the text entry\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Extrude):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  int val;
  for (int i=0; i<25; ++i) {
    // 0: width
    // 1: height
    // 2: width[far]
    // 12: 0, w, h, w2
    // 13: w/2, h
    //

    pos=input->tell();
    f.str("");
    f << "Extrude-" << i << ":";
    for (int j=0; j<5; ++j) {
      val=int(input->readULong(1));
      input->seek(-1, librevenge::RVNG_SEEK_CUR);
      if (val==0) {
        for (int k=0; k<2; ++k) {
          val=int(input->readLong(4));
          if (val) f << "f" << 2*j+k << "=" << val << ",";
        }
      }
      else {
        double value;
        bool isNAN;
        if (!m_mainParser->readDouble(stream, value, isNAN)) {
          f << "###";
          input->seek(pos+8*(j+1), librevenge::RVNG_SEEK_SET);
        }
        else
          f << "g" << j << "=" << value << ",";
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);
  }
  pos=input->tell();
  f.str("");
  f << "Extrude-A:";
  int N=0;
  for (int i=0; i<12; ++i) {
    val=int(input->readLong(4));
    if (!val) continue;
    if (i==4) {
      N=val;
      f << "N=" << N << ",";
    }
    else
      f << "f" << i << "=" << val << ",";
  }
  if (N<2 || 1048+N*24<1048 || 1048+N*24>entry.length()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendExtrude: the number of points seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  MWAWPosition const &position=local.m_position;
  MWAWVec2f origin=position.origin()+0.5*position.size();
  MWAWVec2f dir=0.5*position.size();
  bool ok=true;

  std::vector<MWAWVec2f> pts;
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Extrude-B" << i << ":";
    float coords[]= {0,0};
    for (int j=0; j<2; ++j) {
      double value;
      bool isNAN;
      if (!m_mainParser->readDouble(stream, value, isNAN) || value<-2 || value>2) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendExtrude: can not read a coordinate\n"));
        f << "###";
        input->seek(pos+8*(j+1), librevenge::RVNG_SEEK_SET);
        ok=false;
      }
      else {
        coords[j]=float(value);
        f << "g" << j << "=" << value << ",";
      }
    }
    pts.push_back(origin+MWAWVec2f(coords[0]*dir[0], coords[1]*dir[1]));
    for (int j=0; j<2; ++j) {
      val=int(input->readLong(4));
      if (!val) continue;
      f << "f" << i << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+24, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()<entry.end()) {
    ascFile.addPos(input->tell());
    ascFile.addNote("Extrude-End:");
  }
  if (!ok)
    return false;

  // FIXME: sometimes there is multiple contours in this list of points ...
  static bool first=true;
  if (first) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendExtrude: sorry, sending extrude's shape is not reliable\n"));
    first=false;
  }
  auto fShape=MWAWGraphicShape::polygon(shape.m_initialBox);
  fShape.m_vertices=pts;
  send(listener, fShape, local.m_transform, local.m_style);
  return true;
}

bool Canvas5Graph::sendGIF(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape,
                           Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendGIF: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  libmwaw::DebugFile &ascFile = stream.ascii();
  if (!entry.valid() || entry.length()<104 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendGIF: can not find the number of zone\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(ANGF):";
  int val=int(input->readULong(4));
  if (val) f << "id=" << std::hex << val << std::dec << ",";
  auto len=input->readLong(4);
  if (104+len<104 || 104+len>entry.length()) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Graph::sendGIF: can not find the GIF length\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(pos+8+80, librevenge::RVNG_SEEK_SET);
  int dim[2];
  for (auto &d : dim)
    d = int(input->readLong(4));
  f << "dim=" << MWAWVec2i(dim[0],dim[1]) << ",";
  val=int(input->readLong(4));
  if (val!=1)
    f << "f0=" << val << ",";
  val=int(input->readLong(4));
  if (val!=4)
    f << "f0=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!len) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendGIF: can not find the GIF picture\n"));
  }
  else {
    pos=input->tell();
    librevenge::RVNGBinaryData gif;
    if (!input->readDataBlock(len, gif)) {
      MWAW_DEBUG_MSG(("Canvas5Image::sendGIF: oops can not retrieve the gif\n"));
      ascFile.addPos(pos);
      ascFile.addNote("ANGF:###");
      return false;
    }

    ascFile.skipZone(pos, pos+len-1);
#ifdef DEBUG_WITH_FILES
    std::stringstream s;
    static int index=0;
    s << "gif" << ++index << ".gif";
    libmwaw::Debug::dumpFile(gif, s.str().c_str());
#endif

    MWAWEmbeddedObject obj(gif, "image/gif");
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,shape.m_initialBox.center())) {
      MWAWBox2f shapeBox=transf*shape.m_initialBox;
      MWAWPosition posi(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      posi.m_anchorTo = MWAWPosition::Page;
      MWAWGraphicStyle style(local.m_style);
      style.m_rotate=-rotation;
      listener->insertPicture(posi, obj, style);
    }
    else
      listener->insertPicture(local.m_position, obj, local.m_style);
  }

  while (!input->tell()+4<entry.end()) {
    // find 4 blocks with size 28
    pos=input->tell();
    len=input->readLong(4);
    if (pos+len<pos+4 || pos+len>entry.end()) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote("ANGF-Dt:");
    input->seek(pos+len, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  if (pos!=entry.end()) {
    MWAW_DEBUG_MSG(("Canvas5Image::sendGIF: find extra data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("ANGF[extra]:###");
  }
  return true;
}

bool Canvas5Graph::sendTechnical(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape,
                                 Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  libmwaw::DebugFile &ascFile = stream.ascii();
  if (!entry.valid() || entry.length()<8 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not find the number of zone\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Technical):";
  int N[2];
  f << "N=[";
  for (auto &n : N) {
    n=m_mainParser->readInteger(stream, 8);
    f << n << ",";
  }

  bool isGroupOpened=false;
  if (N[0]>1) {
    isGroupOpened=true;
    listener->openGroup(local.m_position);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  for (int poly=0; poly<N[0]; ++poly) {
    f.str("");
    pos=input->tell();
    f << "Technical-T" << poly << ":";
    if (pos+8 > data.m_entry.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a spline\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      if (isGroupOpened)
        listener->closeGroup();
      return false;
    }
    int N1[2]; // id, num pt
    f << "N=[";
    for (auto &n : N1) {
      double value;
      bool isNan;
      if (!m_mainParser->readDouble(stream, value, isNan)) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a generic number\n"));
        f << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        if (isGroupOpened)
          listener->closeGroup();
        return false;
      }
      n=int(value+0.2);
      f << n << ",";
    }
    f << "],";
    if (N1[1]<0 || (data.m_entry.end()-pos-8)/16<N1[1] || pos+8+16*N1[1] > data.m_entry.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a sub shape\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      if (isGroupOpened)
        listener->closeGroup();
      return false;
    }

    f << "pts=[";
    std::vector<MWAWVec2f> points;
    for (int p=0; p<N1[1]; ++p) {
      double coord[2];
      for (auto &c : coord) {
        bool isNan;
        long actPos=input->tell();
        if (!m_mainParser->readDouble(stream, c, isNan)) {
          MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a number\n"));
          f << "###";
          input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
          c=0;
        }
      }
      points.push_back(MWAWVec2f(float(coord[1]), float(coord[0])));
      f << points.back() << ",";
    }
    f << "],";

    auto const orig=shape.m_initialBox[0];
    auto const dir=shape.m_initialBox.size();
    for (auto &p : points)
      p=orig+MWAWVec2f(p[0]*dir[0],p[1]*dir[1]);
    if (points.size()<4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: the spline seems bad\n"));
      f << "###";
    }
    else {
      auto finalShape=MWAWGraphicShape::path(shape.m_initialBox);
      std::vector<MWAWGraphicShape::PathData> &path=finalShape.m_path;
      path.push_back(MWAWGraphicShape::PathData('M', points[0]));
      for (size_t p=3; p < points.size(); p+=4) {
        if (p>=4 && points[p-4]!=points[p-3])
          path.push_back(MWAWGraphicShape::PathData('M', points[p-3]));
        bool hasFirstC=points[p-3]!=points[p-2];
        bool hasSecondC=points[p-1]!=points[p];
        if (!hasFirstC && !hasSecondC)
          path.push_back(MWAWGraphicShape::PathData('L', points[p]));
        else
          path.push_back(MWAWGraphicShape::PathData('C', points[p], points[p-2], points[p-1]));
      }
      if (local.m_style.hasSurface())
        path.push_back(MWAWGraphicShape::PathData('Z'));
      send(listener, finalShape, local.m_transform, local.m_style);
    }

    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  if (isGroupOpened)
    listener->closeGroup();

  pos=input->tell();
  f.str("");
  f << "Technical-A:";
  if (pos+16>entry.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read the last part\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  for (int i=0; i<6; ++i) {
    int val=int(input->readLong(2));
    int const expected[]= {0,0,0,0,0x6ef0,1};
    if (val==expected[i])
      continue;
    if (i==3)
      f << "fl=" << std::hex << val << std::dec << ","; // 5X01
    else
      f << "f" << i << "=" << val << ",";
  }
  int n=int(input->readULong(4));
  f << "N=" << n << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i=0; i<n; ++i) {
    pos=input->tell();
    f.str("");
    f << "Technical-A" << i << ":";
    if (pos+12>entry.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a type block\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    unsigned type=unsigned(input->readULong(4));
    f << Canvas5Structure::getString(type) << ",";
    int val=int(input->readLong(4));
    if (val)
      f << "id=" << val << ",";
    long len=input->readLong(4);
    long endPos=pos+12+len;
    if (endPos<pos+12 || endPos>entry.end()) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: can not read a type block\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    switch (type) {
    case 0x42507473: { //BPts copy of main zone of points but store with int
      libmwaw::DebugStream f2;
      while (input->tell()+8<endPos) {
        long actPos=input->tell();
        f2.str("");
        f2 << "Technical-bPts:";
        f2 << "id=" << input->readLong(4) << ",";
        int nbPts=int(input->readLong(4));
        f2 << "N=" << nbPts << ",";
        if (nbPts<4 || (endPos-actPos-8)/8 < nbPts) {
          input->seek(actPos, librevenge::RVNG_SEEK_SET);
          break;
        }
        f2 << "pts=[";
        for (int j=0; j<nbPts; ++j) {
          float coord[2];
          for (auto &c : coord) c=float(input->readLong(4))/65536;
          f2 << MWAWVec2f(coord[0],coord[1]) << ",";
        }
        f2 << "],";
        ascFile.addPos(actPos);
        ascFile.addNote(f2.str().c_str());
      }
      if (input->tell()!=endPos) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[bPts]: can not read some data\n"));
        ascFile.addPos(input->tell());
        ascFile.addNote("Technical-bPts:###");
      }
      break;
    }
    case 0x4374726c: // Ctrl
      if (long(int(len/4))*4!=len || (len%4)!=0) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[Ctrl]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "val=["; // [4] or [1,2,3]
      for (int j=0; j<int(len)/4; ++j)
        f << input->readLong(4) << ",";
      f << "],";
      break;
    case 0x44697263: // Dirc
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[Dirc]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "f0=" << input->readLong(4) << ","; // 4
      break;
    case 0x53686450: // ShdP
      if (long(int(len/4))*4!=len || (len%4)!=0) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[ShdP]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "val=["; // [2] or [2,3]
      for (int j=0; j<int(len)/4; ++j)
        f << input->readLong(4) << ",";
      f << "],";
      break;
    case 0x53796d6d: // Symm
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[Symm]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "f0=" << input->readLong(4) << ","; // 0
      break;
    case 0x54787450: // TxtP
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[TxtP]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "f0=" << input->readLong(4) << ","; // 1
      break;
    case 0x57547874: // WTxt
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[WTxt]: unexpected length\n"));
        f << "###";
        break;
      }
      f << "f0=" << input->readLong(4) << ","; // 0
      break;
    case 0x6b696e64: // kind
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical[kind]: unexpected length\n"));
        f << "###";
        break;
      }
      f << input->readLong(4) << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("Canvas5Graph::sendTechnical: unexpected type=%s\n", Canvas5Structure::getString(type).c_str()));
      f << "###";
      break;
    }
    if (input->tell()!=endPos) {
      ascFile.addDelimiter(input->tell(),'|');
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  return true;
}

bool Canvas5Graph::sendCurveText(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &/*shape*/,
                                 Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendCurveText: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  int const vers=version();
  auto const &entry=data.m_entry;
  libmwaw::DebugFile &ascFile = stream.ascii();
  int const headerSz=vers<9 ? 176 : 344;
  if (!entry.valid() || entry.length()<headerSz || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendCurveText: can not find the text entry\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(CurveTxt):";

  input->seek(pos+(vers<9 ? 24 : 40), librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  f << "unk=[";
  MWAWVec2f origin=local.m_position.origin();
  for (int p=0; p<9; ++p) { // pt7: origin bdbox max, pt8 origin bdbox min
    float dim[2];
    for (auto &d : dim) d=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
    if (vers>=9)
      std::swap(dim[0],dim[1]);
    f << MWAWVec2f(dim[1],dim[0]) << ",";
    if (p==8) origin=MWAWVec2f(dim[1],dim[0]); // checkme: not always fine
  }
  f << "],";
  int N=int(input->readULong(2)), val;
  f << "N=" << N << ",";
  for (int i=0; i<4; ++i) { // g0: current style ?
    val=int(input->readLong(2));
    if (val!=(i==0 ? 1 : 0))
      f << "g" << i << "=" << val << ",";
  }
  int nFonts=int(input->readULong(2));
  f << "nFonts=" << nFonts << ",";
  int const fontSize=vers<9 ? 72 : 120;
  int const textSize=vers<9 ? 60 : 112;
  if (headerSz+nFonts*fontSize+N*textSize<0 || headerSz+nFonts*fontSize+N*textSize>entry.length()) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Graph::sendCurveText: N seems bad\n"));
    return false;
  }
  input->seek(pos+headerSz, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::vector<Canvas5StyleManager::CharStyle> fonts;
  fonts.resize(size_t(nFonts));
  for (size_t i=0; i<size_t(nFonts); ++i) {
    pos=input->tell();
    f.str("");
    f << "CurveTxt-F" << i+1 << ":";
    for (int j=0; j<(vers<9 ? 2 : 4); ++j) { // f0: small number
      val=int(input->readLong(4));
      if (val) f << "f" << j << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    m_styleManager->readCharStyle(stream, int(i+1), fonts[i]);
    for (int j=0; j<(vers<9 ? 2 : 4); ++j) {
      val=int(input->readLong(2));
      if (val) f << "f" << j+4 << "=" << val << ",";
    }
  }

  if (N>1)
    listener->openGroup(local.m_position);
  auto fontConverter=m_parserState->m_fontConverter;
  MWAWGraphicStyle charStyle=MWAWGraphicStyle::emptyStyle();
  MWAWPosition charPos(local.m_position);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "CurveTxt-" << i << ":";
    int fId=int(input->readULong(2));
    f << "F" << fId+1 << ",";
    MWAWFont font;
    if (fId>=0 && fId<int(nFonts))
      font=fonts[size_t(fId)].m_font;
    else {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendCurveText: find unknow fonts\n"));
      f << "###";
    }
    librevenge::RVNGString text;
    char c=char(input->readULong(1));
    int unicode = fontConverter->unicode(font.id(), (unsigned char)(c));
    if (unicode == -1) {
      if (c < 0x20) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendCurveText: Find odd char %x\n", static_cast<unsigned int>(c)));
      }
      else
        text.append(c);
    }
    else
      libmwaw::appendUnicode(uint32_t(unicode), text);
    if (!text.empty())
      f << text.cstr() << ",";
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    val=int(input->readULong(4));
    if (val!=0x17c94)
      f << "f0=" << val << ",";
    float angle=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
    f << "angle=" << angle << ",";
    MWAWVec2f points[5];
    f << "pts=[";
    for (auto &pt : points) { // decal, then a box?
      float pts[2];
      for (auto &p : pts) p=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
      if (vers<9)
        pt= MWAWVec2f(pts[1],pts[0]);
      else
        pt= MWAWVec2f(pts[0],pts[1]);
      f << pt << ",";
    }
    f << "],";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+textSize, librevenge::RVNG_SEEK_SET);

    if (text.empty()) continue;
    std::shared_ptr<MWAWSubDocument> doc(new Canvas5GraphInternal::SubDocument(*this, input, text, font));

    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    if (!local.m_transform.isIdentity() && local.m_transform.decompose(rotation,shearing,transf,origin+0.5*points[2]+0.5*points[3])) {
      MWAWBox2f shapeBox=transf*MWAWBox2f(origin+points[2],origin+points[3]);
      charPos.setOrigin(shapeBox[0]);
      charPos.setSize(shapeBox[1]-shapeBox[0]);
      charStyle.m_rotate=-angle-rotation;
    }
    else {
      charPos.setOrigin(origin+points[2]);
      charPos.setSize(points[3]-points[2]);
      charStyle.m_rotate=-angle;
    }
    listener->insertTextBox(charPos, doc, charStyle);
  }
  if (N>1)
    listener->closeGroup();

  pos=input->tell();
  f.str("");
  f << "CurveTxt-End:";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5Graph::sendDimension(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape,
                                 Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream || version()>=9) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: can not find the shape enntry\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream.ascii();
  libmwaw::DebugStream f;
  f << "Entries(Dimension):";
  if (entry.length()<420) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: the data seens too short\n"));
    f << "###sz";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int type=int(input->readLong(2)); // 1-15
  if (type) f << "type=" << type << ",";
  int val=int(input->readLong(2)); // 0
  if (val) f << "f0=" << val << ",";
  f << "points=[";
  std::vector<MWAWVec2f> pts;
  for (int i=0; i<18; ++i) {
    float dims[2];
    // fract type: between -2 and 2
    for (auto &d : dims) d=4*float(input->readLong(4))/65536.f/65536.f;
    pts.push_back(MWAWVec2f(dims[1],dims[0]));
    f << pts.back() << ",";
  }
  f << "],";
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  long posi=input->tell();
  f.str("");
  f << "Dimension[data1]:";
  input->seek(posi+40, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  bool arrowInside=true;
  bool hasFrame=false;
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    int expected[]= {1,0,0};
    if (val==expected[i]) continue;
    char const *wh[]= {"arrows[inside]", "text[centered]", "frame[text]"};
    if (val==0) {
      if (i==0) arrowInside=false;
      f << wh[i] << "=off,";
    }
    else if (val==1) {
      if (i==2) hasFrame=true;
      f << wh[i] << "=on,";
    }
    else
      f << "###" << wh[i] << "=" << val << ",";
  }
  for (int i=0; i<5; ++i) {
    val=int(input->readLong(2));
    int const expected[]= { 1, 1, 1, 0, 3};
    if (val==expected[i]) continue;
    char const *wh[]= {
      "leader",  // none, left, right, automatic
      nullptr,
      "display[text]", // hori, hori/90, aligned, above, below
      "what", // 1: line, 3: arc?
      "precision", // X, X.X, X.XX, X.XXX, X.XXXX, X X/X
    };
    if (i==3 && val==3)
      f << "print[angle],";
    else if (wh[i])
      f << wh[i] << "=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  f << "tolerances=[";
  for (int i=0; i<3; ++i) f << float(input->readLong(4))/65536.f << ",";
  f << "],";
  val=int(input->readLong(2));
  if (val!=1)
    f << "f6=" << val << ",";
  librevenge::RVNGString format;
  long actPos=input->tell();
  if (m_mainParser->readString(stream, format, 19))
    f << "unit=" << format.cstr() << ",";
  else {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: can not read the format's name\n"));
    f << "###format,";
    input->seek(actPos+20, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addDelimiter(input->tell(), '|');
  input->seek(posi+162, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  posi=input->tell();
  f.str("");
  f << "Dimension-format:";
  input->seek(posi+22, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(), '|');
  if (m_mainParser->readString(stream, format, 19))
    f << "name=" << format.cstr() << ",";
  else {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: can not read the format's name\n"));
    f << "###format,";
  }
  input->seek(posi+22+20, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(), '|');
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  MWAWVec2f bDir=shape.m_initialBox.size();
  for (auto &pt : pts)
    pt=shape.m_initialBox[0]+MWAWVec2f(pt[0]*bDir[0], pt[1]*bDir[1]);

  MWAWGraphicStyle style=local.m_style;
  listener->openGroup(local.m_position);

  MWAWGraphicShape fShape;
  MWAWBox2f shapeBox;

  MWAWVec2f textOrigin;
  librevenge::RVNGString text;
  if (type==12) { // a sector instead of a line
    // circle between pts[0], pts[1]->pts[2]
    float angles[2];
    for (size_t i=0; i<2; ++i) {
      MWAWVec2f dir=pts[i+1]-pts[0];
      angles[i]=180*std::atan2(-dir[1],dir[0])/float(M_PI);
    }
    if (std::isnan(angles[0]) || std::isnan(angles[1])) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension: can not compute the sector angles\n"));
    }
    else {
      if (angles[1]<angles[0])
        std::swap(angles[0],angles[1]);
      MWAWVec2f dir=pts[5]-pts[0];
      float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]);
      MWAWBox2f circleBox(pts[0]-len*MWAWVec2f(1,1), pts[0]+len*MWAWVec2f(1,1));
      for (int st=0; st<2; ++st) {
        float angle[2];
        if (arrowInside) {
          if (st==1)
            break;
          angle[0]=angles[0];
          angle[1]=angles[1];
        }
        else if (st==0) {
          angle[0]=angles[0]-10;
          angle[1]=angles[0];
        }
        else {
          angle[0]=angles[1];
          angle[1]=angles[1]+10;
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
        MWAWBox2f arcBox=circleBox;
        // we have the shape box, we need to reconstruct the circle box
        if (maxVal[0]>minVal[0] && maxVal[1]>minVal[1]) {
          float scaling[2]= { (circleBox[1][0]-circleBox[0][0])/(maxVal[0]-minVal[0]),
                              (circleBox[1][1]-circleBox[0][1])/(maxVal[1]-minVal[1])
                            };
          for (auto &s : scaling) {
            if (s>1e7)
              s=100;
            else if (s<-1e7)
              s=-100;
          }
          float constant[2]= { circleBox[0][0]-minVal[0] *scaling[0], circleBox[0][1]-minVal[1] *scaling[1]};
          arcBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                           MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
        }
        style.setSurfaceColor(MWAWColor::white(), 0);
        style.m_arrows[st]=arrowInside ? MWAWGraphicStyle::Arrow::plain(): MWAWGraphicStyle::Arrow();
        style.m_arrows[1-st]=MWAWGraphicStyle::Arrow::plain();

        fShape = MWAWGraphicShape::arc(arcBox, circleBox, MWAWVec2f(float(angle[0]), float(angle[1])));
        send(listener, fShape, local.m_transform, style);
      }
    }
    // TODO: use format for unit, ...
    textOrigin=pts[9];
    std::stringstream s;
    s << std::setprecision(0) << std::fixed << angles[1]-angles[0] << " ";
    text=s.str().c_str();
    libmwaw::appendUnicode(0xb0, text);
  }
  else if (type>12 && type<=14) { // radius/diameter inside an circle/ellipse
    size_t orig=type==13 ? 0 : 4;
    fShape=MWAWGraphicShape::line(pts[orig],pts[3]);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
    send(listener, fShape, local.m_transform, style);

    fShape=MWAWGraphicShape::line(pts[1],pts[3]);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    send(listener, fShape, local.m_transform, style);

    textOrigin=pts[1];
    // TODO: use format for unit, ...
    MWAWVec2f lineSz=pts[orig]-pts[3];
    std::stringstream s;
    s << std::setprecision(0) << std::fixed << std::sqrt(lineSz[0]*lineSz[0]+lineSz[1]*lineSz[1]) << " pt";
    text=s.str().c_str();
  }
  else if (type==15) { // four segments, no text
    for (size_t i=0; i<4; ++i) {
      fShape=MWAWGraphicShape::line(pts[1],pts[i+14]);
      send(listener, fShape, local.m_transform, style);
    }
  }
  else {
    for (size_t i=0; i<2; ++i) {
      size_t const limits[]= {4,6, 7,9 }; // outside1, outside2
      fShape=MWAWGraphicShape::line(pts[limits[2*i]],pts[limits[2*i+1]]);
      send(listener, fShape, local.m_transform, style);
    }

    if (arrowInside) {
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
      fShape=MWAWGraphicShape::line(pts[5],pts[8]);
      send(listener, fShape, local.m_transform, style);
    }
    else {
      style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
      for (size_t i=0; i<2; ++i) {
        size_t const limits[]= {5,10, 8,11 }; // arrows1, arrows2
        fShape=MWAWGraphicShape::line(pts[limits[2*i]],pts[limits[2*i+1]]);
        send(listener, fShape, local.m_transform, style);
      }
    }

    // sometimes there is also a line to rely pts[5/8] and the text

    textOrigin=pts[3];
    // TODO: use format for unit, ...
    MWAWVec2f lineSz=pts[5]-pts[8];
    std::stringstream s;
    s << std::setprecision(0) << std::fixed << std::sqrt(lineSz[0]*lineSz[0]+lineSz[1]*lineSz[1]) << " pt";
    text=s.str().c_str();
  }
  // TODO: use local style to define the text's color...
  if (!text.empty())
    send(listener, text, textOrigin, local.m_transform, MWAWFont(3,10), hasFrame);

  listener->closeGroup();
  return true;
}

namespace Canvas5GraphInternal
{
bool intersect(MWAWVec2f const &A, MWAWVec2f const &dirA, MWAWVec2f const &B, MWAWVec2f const &dirB, MWAWVec2f &pt)
{
  float cross=dirA[0]*dirB[1]-dirA[1]*dirB[0];
  if (cross>-1e-9f && cross<1e-9f) return false;
  MWAWVec2f AB=B-A;
  float alpha=(AB[0]*dirB[1]-AB[1]*dirB[0])/cross;
  pt=A+alpha*dirA;
  return true;
}

std::vector<MWAWVec2f> intersect(MWAWBox2f const &box, MWAWVec2f const &pt, MWAWVec2f const &dir)
{
  std::vector<MWAWVec2f> res;
  for (int d=0; d<2; ++d) {
    for (int wh=0; wh<2; ++wh) {
      MWAWVec2f pts[]= {box[0],box[1]};
      pts[1-wh][1-d]=pts[wh][1-d];
      MWAWVec2f AB=pts[1]-pts[0];
      float cross=AB[0]*dir[1]-AB[1]*dir[0];
      if (cross>-1e-9f && cross<1e-9f) continue;
      MWAWVec2f AO=pt-pts[0];
      float alpha=(AO[0]*dir[1]-AO[1]*dir[0])/cross;
      if (alpha<-1e-9f || alpha>1+1e-9f) continue;
      if (alpha<0)
        alpha=0;
      else if (alpha>1)
        alpha=1;
      res.push_back((1-alpha)*pts[0]+alpha*pts[1]);
    }
  }
  for (size_t i=0; i<res.size(); ++i) {
    for (size_t j=i+1; j<res.size(); ++j) {
      MWAWVec2f diff=res[j]-res[i];
      if (diff[0]*diff[0]+diff[1]*diff[1]>1e-8f)
        continue;
      std::swap(res[j],res.back());
      res.resize(res.size()-1);
      --j;
    }
  }
  if (res.size()!=2) {
    MWAW_DEBUG_MSG(("Canvas5GraphInternal::intersect:: find %d intersections\n", int(res.size())));
    return std::vector<MWAWVec2f>();
  }
  return res;
}
}
bool Canvas5Graph::sendDimension9(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &/*shape*/,
                                  Canvas5GraphInternal::ShapeData const &data, Canvas5Graph::LocalState const &local)
{
  if (!listener || !data.m_stream || version()<9) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: can not find the listener\n"));
    return false;
  }
  auto &stream=data.getStream();
  auto input=stream.input();
  auto const &entry=data.m_entry;
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: can not find the shape enntry\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream.ascii();
  libmwaw::DebugStream f;
  f << "Entries(Dimension):";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long zoneSize=input->readLong(4);
  if (zoneSize<0x796 || zoneSize>entry.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: can not read the zone size\n"));
    f << "###sz";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int val=int(input->readLong(4)); // 4
  if (val!=4) f << "f0=" << val << ",";
  int type=int(input->readLong(1));
  if (type) f << "type=" << type << ",";
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  val=int(input->readLong(4)); // 1
  if (val!=1) f << "f1=" << val << ",";
  MWAWBox2f bdbox;
  for (int i=0; i<2; ++i) {
    float dims[2];
    // fract type: between -2 and 2
    for (auto &d : dims) d=float(m_mainParser->readDouble(stream,8));
    ascFile.addDelimiter(input->tell(),'|');
    if (i==0)
      bdbox.setMin(MWAWVec2f(dims[0],dims[1]));
    else
      bdbox.setMax(MWAWVec2f(dims[0],dims[1]));
  }
  f << "box=" << bdbox << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(4));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  long posi=input->tell();
  int N=0;
  bool finishedWithN=type!=4 && type!=7;
  if (finishedWithN) {
    input->seek(entry.end()-4, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(input->tell(),'|');
    N=int(input->readULong(4));
    f << "N=" << N << ",";
    input->seek(posi, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  for (int i=0; i<2; ++i) {
    posi=input->tell();
    f.str("");
    f << "Dimension[" << (i==0 ? "prefix" : "suffix") << "]:";
    std::string name;
    for (int j=0; j<64; ++j) {
      char c=char(input->readULong(1));
      if (!c)
        break;
      name+=c;
    }
    if (!name.empty())
      f << name << ",";
    input->seek(posi+64, librevenge::RVNG_SEEK_SET);
    val=int(input->readLong(4));
    if (val)
      f << "f0=" << val << ",";
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
  }
  for (int i=0; i<12; ++i) {
    posi=input->tell();
    f.str("");
    f << "Dimension[data" << i << "]:";
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
    input->seek(posi+(i<11 ? 128 : 112), librevenge::RVNG_SEEK_SET);
  }

  posi=input->tell();
  f.str("");
  f << "Dimension[format]:";
  int arrowType=3;
  for (int i=0; i<8; ++i) { // f5=f6=3, f7=2
    val=int(input->readLong(4));
    int const expected[]= {0,0,0,0,0, 3,3,2};
    if (val==expected[i]) continue;
    if (i==5) {
      arrowType=val;
      f << "arrow=" << val << ","; // 0-3: none, inside, outside, auto
    }
    else if (i==7)
      f << "witness[line]=" << val << ","; // 0-2: none, short, long
    else
      f << "f" << i << "=" << val << ",";
  }
  MWAWFont font;
  f << "font=[";
  font.setSize(float(m_mainParser->readDouble(stream, 8)));
  val=int(input->readULong(4)); // 0
  uint32_t flags = 0;
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  if (val&0x80) font.setStrikeOutStyle(MWAWFont::Line::Simple);
  font.setFlags(flags);
  val &= 0xffffff60;
  if (val)
    f << "flag=" << std::hex << val << std::dec << ",";
  std::string name;
  for (int i=0; i<32; ++i) {
    char c=char(input->readULong(1));
    if (!c)
      break;
    name+=c;
  }
  auto fontConverter=m_parserState->m_fontConverter;
  std::string const family=m_mainParser->isWindowsFile() ? "CP1252" : "";
  if (!name.empty())
    font.setId(fontConverter->getId(name, family));
  f << font.getDebugString(fontConverter) << ",";
  f << "],";
  input->seek(posi+76, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  posi=input->tell();
  f.str("");
  f << "Dimension[formatA]:";
  for (int i=0; i<7; ++i) { // f2=-1|3, f4=1,
    val=int(input->readLong(4));
    if (!val)
      continue;
    if (i==2) // -1-20
      f << "dimension=" << val << ",";
    else if (i==5) {
      if (val==1)
        f << "use[secondary],";
      else
        f << "use[secondary]=" << val << ",";
    }
    else if (i==6) // 0-3
      f << "tolerance=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  f << "unkn=["; // 0.05*3
  for (int i=0; i<3; ++i)
    f << m_mainParser->readDouble(stream, 8) << ",";
  f << "],";
  val=int(input->readLong(4)); // 0
  if (val)
    f << "f10=" << val << ",";
  f << "unkn1=" << m_mainParser->readDouble(stream, 8) << ","; // 18
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(4));
    if (val==(i==0 ? 0 : 2)) continue;
    if (i==1)
      f << "digits=" << val << ",";
    else
      f << "g" << i << "=" << val << ",";
  }
  f << "displ[scaling]=" << m_mainParser->readDouble(stream, 8) << ","; // 1
  for (int i=0; i<12; ++i) {
    val=int(input->readLong(4));
    if (val)
      f << "g" << i+2 << "=" << val << ",";
  }
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  posi=input->tell();
  f.str("");
  f << "Dimension[last,type=" << type << "]:";
  if (type<1 || type>11) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: unknown type\n"));
    f << "###type=" << type << ",";
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  std::vector<MWAWVec2f> pts;
  f << "unkn=[";
  for (int i=0; i<((type==11||type==10) ? 2 : type==7 ? 3 : 4); ++i) {
    float dim[2];
    for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
    pts.push_back(MWAWVec2f(dim[0], dim[1]));
    f << pts.back() << ",";
  }
  f << "],";

  long remain=entry.end()-input->tell()-(finishedWithN ? 4 : 0);
  switch (type) {
  case 1: // simple
    if (remain<0) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[1]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    break;
  case 2: // array 1d <->|<->
  case 3: { // array 1D
    if (N<0 || remain/64<N+1 || remain<64*(N+1)+(type==2 ? 4 : 0)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[1]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    f << "unk2=[";
    for (int i=0; i<=N; ++i) {
      f << "[";
      for (int j=0; j<4; ++j) {
        float dim[2];
        for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
        pts.push_back(MWAWVec2f(dim[0], dim[1]));
        f << pts.back() << ",";
      }
      f << "],";
    }
    f << "],";
    ascFile.addDelimiter(input->tell(),'|');
    if (type==2) {
      input->seek(entry.end()-8, librevenge::RVNG_SEEK_SET);
      int direction=int(input->readULong(4));
      f << "dir=" << direction << ","; // 1: hori, 2: verti, 0: all?
      if (direction<0 || direction>2) {
        MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[2]: unexpected direction\n"));
        f << "###";
      }
    }
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    break;
  }
  case 4: { // perpendicular line ...
    if (remain<4 || (remain%16)!=4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[4]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    val=int(input->readLong(4));
    if (val)
      f << "f0=" << val << ",";
    int n=int(remain/16);
    f << "unk2=[";
    for (int i=0; i<n; ++i) { // then the line
      float dim[2];
      for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
      pts.push_back(MWAWVec2f(dim[0], dim[1]));
      f << pts.back() << ",";
    }
    f << "],";
    break;
  }
  // case 5: side object (no data)
  case 6: { // arc
    if (remain!=48) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[6]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    f << "unk2=[";
    for (int i=0; i<3; ++i) {
      float dim[2];
      for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
      pts.push_back(MWAWVec2f(dim[0], dim[1]));
      f << pts.back() << ",";
    }
    f << "],";
    break;
  }
  case 7: // radius
    if (remain!=40) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[9]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    input->seek(6, librevenge::RVNG_SEEK_CUR);
    f << "unk2=[";
    for (int i=0; i<2; ++i) {
      float dim[2];
      for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
      pts.push_back(MWAWVec2f(dim[0], dim[1]));
      f << pts.back() << ",";
    }
    f << "],";
    val=int(input->readLong(2));
    if (val)
      f << "f0=" << val << ",";
    break;
  // case 8: diameter(no data)
  case 9: // cross in circle
    if (remain!=20) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[9]: can not read the last part\n"));
      f << "###";
      ascFile.addPos(posi);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    float dim[2];
    for (auto &p : dim) p=float(m_mainParser->readDouble(stream, 8));
    pts.push_back(MWAWVec2f(dim[0], dim[1]));
    f << "unkn2=" << pts.back() << ","; // center?
    val=int(input->readLong(4));
    if (val)
      f << "f0=" << val << ",";
    break;
  // case 10: inside area
  // case 11: outside area
  default:
    break;
  }
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  if (input->tell()+(finishedWithN ? 4 : 0)<entry.end()) {
    MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: find extra data\n", type));
    posi=input->tell();
    f.str("");
    f << "Dimension[end]:type=" << type << ",";
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
  }

  MWAWGraphicShape fShape;
  MWAWBox2f shapeBox;
  MWAWGraphicStyle style=local.m_style;
  MWAWPosition pos;
  pos.m_anchorTo = MWAWPosition::Page;

  listener->openGroup(local.m_position);
  switch (type) {
  case 1:
  case 2:
  case 3:
  case 4:
  case 5: {
    if (type>=2 && type<=3 && pts.size()<12) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems to small\n", type));
      break;
    }
    else if (type==4 && pts.size()<6) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems to small\n", type));
      break;
    }
    int num=(type==1 || type==4 || type==5) ? 1 : int(pts.size()-8)/4;
    for (int n=0; n<num; ++n) {
      if (n>0) {
        static bool first=true;
        if (first) {
          first=false;
          MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: oops, do not know how to retrieve multi-dimension type=%d\n", type));
        }
#if 0
        style.m_surfaceOpacity=0;
        fShape=MWAWGraphicShape::rectangle(bdbox);
        send(listener, fShape, local.m_transform, style);
#endif
        break;
      }
      MWAWVec2f v=pts[3]-pts[2];
      MWAWVec2f dir(v[1],-v[0]);
      // first border
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
      for (size_t i=0; i<2; ++i) {
        std::vector<MWAWVec2f> points=Canvas5GraphInternal::intersect(bdbox, pts[i], dir);
        if (points.size()!=2) continue;
        fShape=MWAWGraphicShape::line(points[0],points[1]);
        send(listener, fShape, local.m_transform, style);
      }
      MWAWVec2f points[2];
      bool ok=true;
      for (size_t j=0; j<2; ++j) {
        if (Canvas5GraphInternal::intersect(pts[2],v, pts[j], dir, points[j]))
          continue;
        ok=true;
        break;
      }
      if (!ok) continue;
      // now the main arrow
      MWAWVec2f u=pts[1]-pts[0];
      bool outside=arrowType==2 || (arrowType!=1 && u[0]*u[0]+u[1]*u[1]<50*50);
      if (outside) {
        std::vector<MWAWVec2f> points2=Canvas5GraphInternal::intersect(bdbox, points[0], points[1]-points[0]);
        if (points2.size()==2) {
          MWAWVec2f dir0=points[1]-points[0];
          MWAWVec2f dir1=points2[1]-points2[0];
          if (dir0[0]*dir1[0]+dir0[1]*dir1[1]<0)
            std::swap(points2[0],points2[1]);
          if (arrowType!=0)
            style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
          for (size_t i=0; i<2; ++i) {
            fShape=MWAWGraphicShape::line(points2[i],points[i]);
            send(listener, fShape, local.m_transform, style);
          }
        }
      }
      else {
        if (arrowType!=0)
          style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
        fShape=MWAWGraphicShape::line(points[0],points[1]);
        send(listener, fShape, local.m_transform, style);
      }

      // and the text
      std::stringstream s;
      s << std::setprecision(0) << std::fixed << std::sqrt(u[0]*u[0]+u[1]*u[1]) << " pt";
      librevenge::RVNGString text=s.str().c_str();

      MWAWVec2f textOrigin=0.5f*(points[0]+points[1]);
      send(listener, text, textOrigin, local.m_transform, font, false);
    }
    break;
  }
  case 6: {
    if (pts.size()!=7) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems bad\n", type));
      break;
    }
    MWAWVec2f orig;
    if (!Canvas5GraphInternal::intersect(pts[0],pts[1]-pts[0], pts[3],pts[3]-pts[2],orig)) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, can not find the origin\n", type));
      break;
    }
    float angles[2];
    for (size_t i=0; i<2; ++i) {
      MWAWVec2f dir=pts[1+2*i]-orig;
      angles[i]=std::atan2(-dir[1],dir[0]);
    }

    if (std::isnan(angles[0]) || std::isnan(angles[1])) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[6]: can not compute the sector angle\n"));
    }
    else {
      MWAWVec2f dir=pts[5]-orig;
      float radius=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]);
      std::swap(angles[0],angles[1]);
      MWAWBox2f circleBox(MWAWVec2f(orig[0]-radius, orig[1]-radius),MWAWVec2f(orig[0]+radius, orig[1]+radius));

      // we must compute the arc box
      float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
      int limitAngle[2];
      for (int i = 0; i < 2; ++i)
        limitAngle[i] = (angles[i] < 0) ? int(2*angles[i]/float(M_PI))-1 : int(2*angles[i]/float(M_PI));
      for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; ++bord) {
        float ang = (bord == limitAngle[0]) ? float(angles[0]) :
                    (bord == limitAngle[1]+1) ? float(angles[1]) : float(M_PI/2*bord);
        float actVal[2] = { std::cos(ang), -std::sin(ang)};
        if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
        else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
        if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
        else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
      }
      MWAWBox2f arcBox(MWAWVec2f(orig[0]+minVal[0]*radius, orig[1]+minVal[1]*radius),MWAWVec2f(orig[0]+maxVal[0]*radius, orig[1]+maxVal[1]*radius));
      fShape = MWAWGraphicShape::pie(arcBox, circleBox, MWAWVec2f(float(180/M_PI)*angles[0], float(180/M_PI)*angles[1]));
      send(listener, fShape, local.m_transform, style);
    }
    // and the text
    std::stringstream s;
    s << std::setprecision(2) << std::fixed << float(180/M_PI)*(angles[1]-angles[0]) << " ";
    librevenge::RVNGString text=s.str().c_str();

    send(listener, text, pts[5], local.m_transform, font, false);
    break;
  }
  case 7: {
    if (pts.size()!=5) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems bad\n", type));
      break;
    }
    fShape=MWAWGraphicShape::line(pts[1],pts[2]);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    send(listener, fShape, local.m_transform, style);
    fShape=MWAWGraphicShape::line(pts[0],pts[1]);
    if (arrowType!=0)
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
    send(listener, fShape, local.m_transform, style);
    // and the text
    std::stringstream s;
    MWAWVec2f dir=pts[2]-pts[0];
    s << std::setprecision(0) << std::fixed << std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]) << " pt";
    librevenge::RVNGString text=s.str().c_str();
    send(listener, text, pts[1], local.m_transform, font, false);
    break;
  }
  case 8: {
    if (pts.size()!=4) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems bad\n", type));
      break;
    }
    fShape=MWAWGraphicShape::line(pts[0],pts[1]);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    send(listener, fShape, local.m_transform, style);
    fShape=MWAWGraphicShape::line(pts[0],pts[2]);
    if (arrowType!=0)
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
    send(listener, fShape, local.m_transform, style);
    // and the text
    std::stringstream s;
    MWAWVec2f dir=pts[2]-pts[0];
    s << std::setprecision(0) << std::fixed << std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]) << " pt";
    librevenge::RVNGString text=s.str().c_str();
    send(listener, text, pts[1], local.m_transform, font, false);
    break;
  }
  case 9: // 0 is the center
    if (pts.size()!=5) {
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9[%d]: sorry, the number of points seems bad\n", type));
      break;
    }
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    fShape=MWAWGraphicShape::line(pts[1],pts[2]);
    send(listener, fShape, local.m_transform, style);
    fShape=MWAWGraphicShape::line(pts[3],pts[4]);
    send(listener, fShape, local.m_transform, style);
    break;
  case 10:
  case 11: {
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: sorry, sending area dimension of type=%d is not implemented\n", type));
    }
    break;
  }
  default:
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("Canvas5Graph::sendDimension9: sorry, sending dimension of type=%d is not implemented\n", type));
    }
    break;
  }
  listener->closeGroup();
  return true;
}

////////////////////////////////////////////////////////////
// auxilliary structure
////////////////////////////////////////////////////////////

void Canvas5Graph::LocalState::multiplyMatrix(std::array<double,9> const &mat)
{
  if (mat[8]>=-1e-3 && mat[8]<=1e-3) {
    // checkme: this seems possible, unsure what this means ?
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("Canvas5Graph::LocalState::multiplyMatrix: find some matrix with mat[3][3]=0\n"));
    }
  }
  if (mat[2]<-1e-3 || mat[2]>1e-3 || mat[5]<-1e-3 || mat[5]>1e-3) {
    MWAW_DEBUG_MSG(("Canvas5Graph::LocalState::multiplyMatrix: projection will be ignored\n"));
    return;
  }
  m_transform*=MWAWTransformation(MWAWVec3f((float)mat[0],(float)mat[3],(float)mat[6]), MWAWVec3f((float)mat[1],(float)mat[4],(float)mat[7]));
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
