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

#include "CanvasParser.hxx"

#include "CanvasGraph.hxx"
#include "CanvasStyleManager.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a CanvasGraph */
namespace CanvasGraphInternal
{
//! Internal: the shape of a CanvasGraph
struct Shape {
  //! constructor
  Shape()
    : m_type(-1)
    , m_box()
    , m_rotation(0)
    , m_penSize(1,1)
    , m_mode(8)
    , m_patterns{1,1}
    , m_dash(1)
    , m_dashWidth()
    , m_hatchGradChild(-1)
    , m_values{0,0}
    , m_colors{MWAWColor::black(), MWAWColor::white()}
    , m_points()
    , m_child(-1)
    , m_origChild(-1)
    , m_childs()
    , m_align(0)
    , m_bitmapType(0)
    , m_arrow(MWAWGraphicStyle::Arrow::plain())
    , m_specialType()
    , m_entry()
    , m_sent(false)
  {
  }

  //! returns the type name
  std::string getTypeName() const
  {
    if (m_type==52 && !m_specialType.empty()) {
      std::stringstream s;
      s << "SPEC" << m_specialType;
      return s.str();
    }
    static std::map<int, std::string> const s_typeName= {
      {2,"text"},
      {3,"line"},
      {4,"rect"},
      {5,"rectOval"},
      {6,"oval"},
      {7,"arc"},
      {9,"polyline"},
      {10,"spline"},
      {18,"picture"},
      {52,"special"},
      {55,"bitmap"}, // in v3.5 indexed
      {56,"polydata"},
      {59,"emptyV3"}, // in v3
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

  //! try to return the special type
  int getSpecialId() const
  {
    static std::map<std::string, int> const s_specialId= {
      {"CCir", 9}, // concentric circle
      {"Cube", 0}, // front/back face coord in m_points
      {"DIMN", 1}, // a dimension with measure
      {"Enve", 8}, // enveloppe
      {"grid", 2}, // num subdivision in values[0], values[1]
      {"HATC", 7}, // hatch
      {"ObFl", 3}, // gradient
      {"OLnk", 10}, // object link
      {"Paln", 4}, // line, poly, spline with big border
      {"QkTm", 5}, // checkme a quickTime film video in data?
      {"regP", 6}, // a target arrow
    };
    auto const &it=s_specialId.find(m_specialType);
    if (it!=s_specialId.end())
      return it->second;
    return -1;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Shape const &s)
  {
    o << s.getTypeName() << ",";
    o << s.m_box << ",";
    if (s.m_rotation)
      o << "rot[transf]=" << s.m_rotation << ",";
    if (s.m_penSize!=MWAWVec2f(1,1))
      o << "pen[size]=" << s.m_penSize << ",";
    if (s.m_colors[0]!=MWAWColor::black())
      o << "col[line]=" << s.m_colors[0] << ",";
    if (s.m_colors[1]!=MWAWColor::white())
      o << "col[surf]=" << s.m_colors[1] << ",";
    switch (s.m_mode) {
    case 8: // copy
      break;
    case 9:
      o << "xor,";
      break;
    default:
      o << "mode=" << s.m_mode << ",";
      break;
    }
    for (int i=0; i<2; ++i) {
      if (s.m_patterns[i]==1) continue;
      o << "patt[" << (i==0 ? "line" : "surf") << "]=" << s.m_patterns[i] << ",";
    }
    if (!s.m_dashWidth.empty()) {
      o << "dash=[";
      for (auto w : s.m_dashWidth) o << w << ",";
      o << "],";
    }
    else if (s.m_dash!=1)
      o << "dash=" << s.m_dash << ",";
    if (s.m_hatchGradChild>0)
      o << "child[hatch/grad]=S" << s.m_hatchGradChild << ",";
    if (!s.m_points.empty()) {
      o << "pts=[";
      for (auto const &pt: s.m_points)
        o << pt << ",";
      o << "],";
    }
    if (s.m_values[0]) {
      switch (s.m_type) {
      case 3:
        o << "dir=" << s.m_values[0] << ",";
        break;
      case 5:
        o << "rad[H]=" << s.m_values[0] << ",";
        break;
      case 6: // rotation angle
      case 7:
        o << "angle0=" <<  s.m_values[0] << ",";
        break;
      case 55: // 1-8-32
        o << "depth=" << s.m_values[0] << ",";
        break;
      default:
        o << "f0=" <<  s.m_values[0] << ",";
        break;
      }
    }
    if (s.m_values[1]) {
      switch (s.m_type) {
      case 3:
        o << "arrow=" << s.m_values[1] << ",";
        break;
      case 5:
        o << "rad[V]=" << s.m_values[1] << ",";
        break;
      case 7:
        o << "angle1=" <<  s.m_values[1] << ",";
        break;
      default:
        o << "f1=" <<  s.m_values[1] << ",";
        break;
      }
    }
    if (s.m_child>0)
      o << "child=S" << s.m_child << ",";
    if (s.m_origChild>0)
      o << "child[orig]=S" << s.m_origChild << ",";
    if (s.m_entry.valid())
      o << "data=" << std::hex << s.m_entry.begin() << "<->" << s.m_entry.end() << std::dec << ",";
    if (s.m_align)
      o << "align=" << s.m_align << ",";
    if (s.m_bitmapType)
      o << "bitmap[type]=" << s.m_bitmapType << ",";

    return o;
  }
  //! the shape type
  int m_type;
  //! the bounding box
  MWAWBox2f m_box;
  //! the transformed child rotation
  int m_rotation;
  //! the pen size
  MWAWVec2f m_penSize;
  //! the copy mode (8: copy, 9: xor)
  int m_mode;
  //! the line, surface pattern
  int m_patterns[2];
  //! the line dash
  int m_dash;
  //! the dash array: a sequence of (fullsize, emptysize) v3.5
  std::vector<float> m_dashWidth;
  //! the hatch or gradient child
  int m_hatchGradChild;
  //! the values
  int m_values[2];
  //! the color
  MWAWColor m_colors[2];
  //! the points: line, ...
  std::vector<MWAWVec2f> m_points;
  //! the main child (all)
  int m_child;
  //! the child before the transformation
  int m_origChild;
  //! the childs (group 99)
  std::vector<int> m_childs;
  //! the text alignment: 0:left, 1:center, ...
  int m_align;
  //! the bitmap type
  int m_bitmapType;
  //! the line/arc arrow
  MWAWGraphicStyle::Arrow m_arrow;
  //! the special type
  std::string m_specialType;
  //! the data zone
  MWAWEntry m_entry;
  //! a flag to know if the shape is already send
  mutable bool m_sent;
};

//! Internal: the local state of a CanvasGraph
struct LocalTransform {
  //! default constructor
  LocalTransform(MWAWPosition const &pos, MWAWGraphicStyle const &style)
    : m_position(pos)
    , m_style(style)
  {
  }
  MWAWPosition m_position;
  MWAWGraphicStyle m_style;
};

//! Internal: given a list of vertices, an indice and an offset computes a new point
MWAWVec2f getOffsetPoint(std::vector<MWAWVec2f> const &vertices, size_t id, float offset)
{
  if (vertices.size()<=1 || id>=vertices.size()) {
    MWAW_DEBUG_MSG(("CanvasGraphInternal::getOffsetPoints: bad index=%d\n",int(id)));
    return vertices.empty() ? MWAWVec2f(0,0) : vertices[0];
  }
  MWAWVec2f dirs[]= {MWAWVec2f(0,0), MWAWVec2f(0,0)};
  float scales[]= {0,0};
  for (size_t d=0; d<2; ++d) {
    if ((d==0 && id==0) || (d==1 && id+1==vertices.size()))
      continue;
    dirs[d]=vertices[id+(d==0 ? 0 : 1)]-vertices[id-(d==0 ? 1 : 0)];
    float len=dirs[d][0]*dirs[d][0]+dirs[d][1]*dirs[d][1];
    if (len<=0) continue;
    scales[d]=offset/std::sqrt(len);
  }
  MWAWVec2f const &pt=vertices[id];
  MWAWVec2f pts[]= {pt+MWAWVec2f(-scales[0]*dirs[0][1], scales[0]*dirs[0][0]),
                    pt+MWAWVec2f(-scales[1]*dirs[1][1], scales[1]*dirs[1][0])
                   };

  float const epsilon=1e-6f;
  float cr=dirs[0][0]*dirs[1][1]-dirs[0][1]*dirs[1][0];
  if (cr>-epsilon && cr<epsilon)
    return pts[id==0 ? 1 : 0];
  // M=P0+u*d0, M=P1+v*d1, P0P1=u*d0-v*d1, P0P1^d1=u*d0^d1
  MWAWVec2f P0P1=pts[1]-pts[0];
  float u=(P0P1[0]*dirs[1][1]-P0P1[1]*dirs[1][0])/cr;
  return pts[0]+u*dirs[0];
}

//! Internal: try to smooth a list of points
std::vector<MWAWVec2f> smoothPoints(std::vector<MWAWVec2f> const &vertices)
{
  std::vector<MWAWVec2f> res;
  size_t N=vertices.size();
  if (N<=1)
    return res;

  res.push_back(vertices[0]);
  for (size_t j=1; j+1<N; ++j) {
    MWAWVec2f dir=vertices[j+1]-vertices[j-1];
    MWAWVec2f AB=vertices[j]-vertices[j-1];
    float len2=(dir[0]*dir[0]+dir[1]*dir[1]);
    float cr=AB[0]*dir[1]-AB[1]*dir[0];
    float offset=cr/3/(len2>0 ? len2 : 1);
    res.push_back(vertices[j]+offset*MWAWVec2f(-dir[1],dir[0]));
  }
  res.push_back(vertices.back());
  return res;
}

////////////////////////////////////////
//! Internal: the state of a CanvasGraph
struct State {
  //! constructor
  State()
    : m_input()

    , m_idToGradientMap()
    , m_idToShapeMap()
  {
  }

  //! the main input
  MWAWInputStreamPtr m_input;

  //! the map id to gradient
  std::map<int, MWAWGraphicStyle::Gradient> m_idToGradientMap;
  //! the map id to shape
  std::map<int, Shape> m_idToShapeMap;
};

////////////////////////////////////////
//! Internal: the subdocument of a CanvasGraph
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor from a zoneId
  SubDocument(CanvasGraph &parser, MWAWInputStreamPtr const &input, int zoneId)
    : MWAWSubDocument(parser.m_mainParser, input, MWAWEntry())
    , m_graphParser(parser)
    , m_id(zoneId)
    , m_measure()
  {
  }
  //! constructor from string
  SubDocument(CanvasGraph &parser, MWAWInputStreamPtr const &input, librevenge::RVNGString const &measure)
    : MWAWSubDocument(parser.m_mainParser, input, MWAWEntry())
    , m_graphParser(parser)
    , m_id(-1)
    , m_measure(measure)
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
    if (m_id != sDoc->m_id) return true;
    if (m_measure != sDoc->m_measure) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the graph parser
  CanvasGraph &m_graphParser;
  //! the subdocument id
  int m_id;
  //! the measure
  librevenge::RVNGString m_measure;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("CanvasGraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (m_id<0) {
    if (m_measure.empty()) {
      MWAW_DEBUG_MSG(("CanvasGraphInternal::SubDocument::parse: can not find the measure\n"));
      return;
    }
    listener->setFont(MWAWFont(3,10));
    MWAWParagraph para;
    para.m_justify = MWAWParagraph::JustificationCenter;
    listener->setParagraph(para);
    listener->insertUnicodeString(m_measure);
    return;
  }
  long pos = m_input->tell();
  m_graphParser.sendText(m_id);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
CanvasGraph::CanvasGraph(CanvasParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new CanvasGraphInternal::State)
  , m_mainParser(&parser)
  , m_styleManager(parser.m_styleManager)
{
}

CanvasGraph::~CanvasGraph()
{
}

int CanvasGraph::version() const
{
  return m_parserState->m_version;
}

void CanvasGraph::setInput(MWAWInputStreamPtr &input)
{
  m_state->m_input=input;
}

MWAWInputStreamPtr &CanvasGraph::getInput()
{
  return m_state->m_input;
}

bool CanvasGraph::sendShape(int id)
{
  auto const it=m_state->m_idToShapeMap.find(id);
  if (id<=0 || it==m_state->m_idToShapeMap.end()) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendShape: can not find shape %d\n", id));
    return false;
  }
  return send(it->second);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// shapes
////////////////////////////////////////////////////////////

bool CanvasGraph::readShapes(int numShapes, unsigned long shapeLength, unsigned long dataLength)
{
  if (long(shapeLength)<0 || !m_mainParser->decode(long(shapeLength)) ||
      (long(dataLength)<0 || !m_mainParser->decode(long(dataLength)))) {
    MWAW_DEBUG_MSG(("CanvasGraph::readShapes: can not decode the input\n"));
    return false;
  }
  bool const isWindows=m_mainParser->isWindowsFile();
  MWAWInputStreamPtr input = getInput();
  long pos=input ? input->tell() : 0;
  long endPos=pos+long(shapeLength);
  // checkme:
  // on Windows, I found 4 extra bits after each 65532 bits
  //             I supposed that these shapes are stored in blocks of 65536 bits on Windows, ...
  //             (this probably implies that data blocks with size >65536 are managed differently :-~)
  long extraCheckSumSz=isWindows ? 4*(numShapes/762) : 0;
  if (!input->checkPosition(endPos+long(dataLength)) || (long(shapeLength)-extraCheckSumSz)/86 < numShapes) {
    MWAW_DEBUG_MSG(("CanvasGraph::readShapes: zone seems too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Shape):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  MWAWEntry dataZone;
  dataZone.setBegin(endPos);
  dataZone.setLength(long(dataLength));
  std::vector<MWAWEntry> dataZonesList;
  if (!isWindows)
    dataZonesList.push_back(dataZone);
  else {
    input->seek(endPos, librevenge::RVNG_SEEK_SET);

    long finalEnd=dataZone.end();
    for (int i=0; i<int(dataLength/16); ++i) {
      long actPos=input->tell();
      f.str("");
      f << "Shape-Dt" << i << ":";
      f << input->readULong(4) << ",";
      f << input->readULong(4) << ",";
      auto len=input->readULong(4);
      f << "len=" << len << ",";

      dataZone.setBegin(finalEnd);
      dataZone.setLength(long(len));
      dataZonesList.push_back(dataZone);

      if (len) {
        if (!m_mainParser->decode(long(len))) {
          MWAW_DEBUG_MSG(("CanvasGraph::readShapes: can not decode a data zone\n"));
          return false;
        }
        ascFile.addPos(finalEnd);
        ascFile.addNote("_");
        finalEnd += len;
        ascFile.addPos(finalEnd);
        ascFile.addNote("_");
      }
      ascFile.addDelimiter(input->tell(), '|');
      ascFile.addPos(actPos);
      ascFile.addNote(f.str().c_str());
      input->seek(actPos+16, librevenge::RVNG_SEEK_SET);
    }
  }

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<numShapes; ++i) {
    if (isWindows && i>0 && (i%762)==0) {
      ascFile.addPos(input->tell());
      ascFile.addNote("_");
      input->seek(4, librevenge::RVNG_SEEK_CUR);
    }
    pos=input->tell();
    readShape(i, dataZonesList);
    input->seek(pos+86, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=endPos) {
    ascFile.addPos(input->tell());
    ascFile.addNote("Shape-End:");
  }

  ascFile.addPos(dataZone.begin());
  ascFile.addNote("Shape-Data:");
  if (!dataZonesList.empty())
    input->seek(dataZonesList.back().end(), librevenge::RVNG_SEEK_SET);

  return true;
}

bool CanvasGraph::readShape(int n, std::vector<MWAWEntry> const &dataZonesList)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+86)) {
    MWAW_DEBUG_MSG(("CanvasGraph::readShape: zone seems too short\n"));
    return false;
  }
  bool const isWindows=m_mainParser->isWindowsFile();
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  int const vers=version();
  int val;
  float dim[4];
  for (auto &d : dim) d=float(input->readLong(2));
  int type=int(input->readULong(1));

  if (type==59 || type==100) {
    input->seek(pos+86, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  m_state->m_idToShapeMap[n]=CanvasGraphInternal::Shape();
  CanvasGraphInternal::Shape &shape=m_state->m_idToShapeMap.find(n)->second;
  shape.m_type=type;
  float penSize[2];
  for (auto &p :  penSize) p=float(input->readULong(1));
  for (auto &p :  penSize) p+=float(input->readULong(1))/256;
  shape.m_penSize=MWAWVec2f(penSize[0], penSize[1]);
  shape.m_mode=int(input->readULong(1));
  for (auto &pat : shape.m_patterns) pat=int(input->readULong(1));
  val=int(input->readULong(2));
  bool hasDash=false;
  if (val&0x1000)
    hasDash=true;
  if (val&0x8000)
    f << "locked,";
  val&=0x6fff;
  if (val) // 0 or 1
    f << "fl=" << std::hex << val << std::dec << ",";
  MWAWEntry data;
  long begPos=input->readLong(4);
  size_t dataId=0;
  if (isWindows && (begPos>>16)) {
    dataId=size_t(begPos>>16);
    begPos=(begPos&0xffff);
  }
  data.setBegin(begPos);
  data.setLength(input->readLong(4));
  if (n>0 && data.valid()) {
    if (dataId<dataZonesList.size() && data.end()<=dataZonesList[dataId].length()) {
      shape.m_entry.setBegin(dataZonesList[dataId].begin()+data.begin());
      shape.m_entry.setLength(data.length());
    }
    else if ((dim[0]<0 || dim[0]>0) && (dim[1]<0 || dim[1]>0)) { // dim[0|1]==0 is a symptom of a junk zone
      MWAW_DEBUG_MSG(("CanvasGraph::readShape: the zone data seems bad\n"));
      f << "###data=" << std::hex << data.begin() << "<->" << data.end() << std::dec << "[" << dataId << "],";
    }
  }
  for (auto &v : shape.m_values) v=int(input->readLong(2));
  if (vers==2 || !hasDash)
    shape.m_bitmapType=int(input->readLong(2)); // 0|1
  else
    shape.m_dash=int(input->readULong(2));
  val=int(input->readULong(2));
  if (val)
    f << "parent?[id]=" << val << ",";
  val=int(input->readULong(2));
  if (val)
    f << "next?[id]=" << val << ",";
  val=int(input->readULong(2));
  if (vers>2)
    shape.m_hatchGradChild=val;
  else if (val)
    f << "unkn=" << val << ",";
  shape.m_origChild=int(input->readULong(2));
  val=int(input->readULong(2)); // 0
  if (val) f << "g0=" << val << ",";
  shape.m_child=int(input->readULong(2));
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(pos+46, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  if (type==2) {
    val=int(input->readULong(2));
    if (val)
      f << "N[C]=" << val << ",";
    val=int(input->readULong(2)); // 0
    if (val) f << "g1=" << val << ",";
    if (vers==2) {
      shape.m_align=int(input->readULong(1)); // 0: left, 1: center
      if (shape.m_align) f << "align=" << shape.m_align << ",";
      input->seek(1, librevenge::RVNG_SEEK_CUR);
    }
    else {
      val=int(input->readULong(2)); // 0
      if (val) f << "g2=" << val << ",";
    }
  }
  for (int st=type==2 ? 1 : 0; st<2; ++st) {
    unsigned char col[3];
    for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
    shape.m_colors[st]=MWAWColor(col[0],col[1],col[2]);
  }
  shape.m_rotation=int(input->readULong(2));
  val=int(input->readULong(2));
  if (val) f << "h1=" << val << ",";
  for (auto &d : dim) d+=float(input->readULong(2))/65536.f;
  if (isWindows)
    shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  else
    shape.m_box=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
  if (type==52) {
    for (int i=0; i<6; ++i) { // 0
      val=int(input->readULong(2));
      if (!val) continue;
      if (i==5)
        f << "prev[hatch/grad]=S" << val << ",";
      else
        f << "h" << 2+i << "=" << val << ",";
    }
    std::string what;
    for (int i=0; i<4; ++i) what+=char(input->readULong(1));
    shape.m_specialType=what;
  }
  auto const extra=f.str();
  f.str("");
  f << "Shape-" << n << ":" << shape << extra;
  if (input->tell()!=pos+86)
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (n>0 && shape.m_entry.valid()) {
    shape.m_entry.setId(n);
    readShapeData(shape);
  }

  input->seek(pos+86, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasGraph::readShapeData(CanvasGraphInternal::Shape &shape)
{
  bool const isWindows=m_mainParser->isWindowsFile();
  MWAWInputStreamPtr input = getInput();
  int expectedSize=shape.m_type==2 ? 47 : shape.m_type==3 ? 46 : shape.m_type==7 ? 48 : shape.m_type==99 ? 2 : 0;
  auto const &entry=shape.m_entry;
  if (!entry.valid() || !input->checkPosition(entry.end()) || (expectedSize!=0 && entry.length()<expectedSize)) {
    MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: zone %d seems bad\n", entry.id()));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Shape-" << entry.id() << "[data," << shape.getTypeName() << "]:";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();
  ascFile.addPos(endPos);
  ascFile.addNote("_");
  switch (shape.m_type) {
  case 2: // a text zone, will be read by sendText
    break;
  case 3: { // line
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2));
    for (auto &d : dim) d+=float(input->readLong(2))/65536;
    for (int st=0; st<4; st+=2) {
      if (isWindows)
        shape.m_points.push_back(MWAWVec2f(dim[st], dim[st+1]));
      else
        shape.m_points.push_back(MWAWVec2f(dim[st+1], dim[st]));
      f << shape.m_points.back() << (st==0 ? "<->" : ",");
    }

    std::string extra;
    if (m_styleManager->readArrow(shape.m_arrow, extra))
      f << "arrow=[" << shape.m_arrow << extra << "],";
    else
      f << "###";
    input->seek(entry.begin()+16+26, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<2; ++i) {
      int val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    if (entry.length()<46+2) break;  // v3.5
    int N=int(input->readULong(2));
    if (entry.length()<46+2+4*N && isWindows) { // rare but may happen if the file is converted
      input->seek(-2, librevenge::RVNG_SEEK_CUR);
      input->setReadInverted(false);
      N=int(input->readULong(2));
    }
    if (entry.length()<46+2+4*N) {
      f << "###N=" << N << ",";
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: the number of dashes in zone %d seems bad\n", entry.id()));
      if (isWindows)
        input->setReadInverted(true);
      break;
    }
    f << "dash=[";
    for (int i=0; i<N; ++i) {
      shape.m_dashWidth.push_back(float(input->readULong(4))/65536.f);
      f << shape.m_dashWidth.back() << ",";
    }
    f << "],";
    if (isWindows)
      input->setReadInverted(true);
    break;
  }
  case 4:
  case 5: // a BW bitmap, will be read by getBitmapBW
    break;
  case 7: { // arc
    std::string extra;
    if (m_styleManager->readArrow(shape.m_arrow, extra))
      f << "arrow=[" << shape.m_arrow << extra << "],";
    else
      f << "###";
    input->seek(entry.begin()+26, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<3; ++i) { // 0
      int val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2));
    for (auto &d : dim) d+=float(input->readLong(2))/65536;
    f << "dim=" << MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2])); // checkme probably junk
    break;
  }
  case 9:
  case 10: { // polygone
    if (entry.length()<8) {
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: the entry seems too short\n"));
      f << "####";
      break;
    }
    for (int i=0; i<2; ++i) { // small numbers
      int val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    int N=int(input->readULong(4));
    if (N<0 || 1+N>entry.length()/8 || (shape.m_type==10 && (N%2)!=0)) {
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the number of points of a polyline\n"));
      f << "###N=" << N << ",";
    }
    else {
      f << "N=" << N << ",";
      f << "pts=[";
      for (int i=0; i<N; ++i) {
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(4))/65536;
        shape.m_points.push_back(MWAWVec2f(dim[1], dim[0]));
        f << shape.m_points.back() << ",";
      }
      f << "],";
    }
    break;
  }
  case 18: // a picture, will be read by getPicture
    break;
  case 52: { // special
    int specialId=shape.getSpecialId();
    switch (specialId) {
    case 0: // cube
      if (entry.length()<64) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the cube points\n"));
        f << "###sz";
        break;
      }
      for (int i=0; i<8; ++i) { // front face, back face
        float pts[2];
        for (auto &c : pts) c=float(input->readULong(4))/65536.f;
        shape.m_points.push_back(MWAWVec2f(pts[1],pts[0]));
        f << shape.m_points.back() << ",";
      }
      break;
    case 1: // DIMN, will be read when we create the shape
      break;
    case 3: { // ObFL : gradient
      MWAWGraphicStyle::Gradient grad;
      if (!m_styleManager->readGradient(entry, grad)) {
        f << "###sz";
        break;
      }
      if (m_state->m_idToGradientMap.find(entry.id()) != m_state->m_idToGradientMap.end()) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: the gradient %d already exists\n", entry.id()));
      }
      else
        m_state->m_idToGradientMap[entry.id()]=grad;

      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
      return true;
    }
    case 4: // Paln: will be read when we send the data
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
      return true;
    case 5: // QkTm: a QuickTime movie? must be read when we send the data
      break;
    case 7: { // hatch
      if (entry.length()<78) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the hatch data\n"));
        f << "###sz";
        break;
      }
      long pos=input->tell();
      float dim[2];
      for (int i=0; i<2; ++i) {
        for (float &d : dim) d=float(input->readLong(4))/65536.f;
        f <<  "dir" << i << "=" << MWAWVec2f(dim[0],dim[1]) << ",";
      }
      // CHECKME: normally, there is also some dash properties, where ?
      librevenge::RVNGString text;
      if (m_mainParser->readString(text, 60)) // find nothing
        f << text.cstr() << ",";
      else
        f << "###string,";
      input->seek(pos+76, librevenge::RVNG_SEEK_SET);
      ascFile.addDelimiter(input->tell(),'|');
      int N=int(input->readULong(2));
      f << "N=" << N << ",";
      if (N<=0 || entry.length()<78+8*N) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the number of hatch\n"));
        f << "###sz";
        break;
      }
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());

      pos=input->tell();
      f.str("");
      f << "Shape-" << entry.id() << "[points," << shape.getTypeName() << "]:";
      for (int i=0; i<2*N; ++i) {
        for (float &d : dim) d=float(input->readLong(4))/65536.f;
        shape.m_points.push_back(MWAWVec2f(dim[0],dim[1]));
        f << shape.m_points.back() << ((i%2)==0 ? "<->" : ",");
      }
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return true;
    }
    case 8: { // Enve
      if (entry.length()<24) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: the enveloppe zone seems bad\n"));
        f << "###sz";
        break;
      }
      for (int i=0; i<2; ++i) { // 0
        int val=int(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      int N=int(input->readULong(4));
      if (N<2 || (entry.length()-8)/8<N || 8+N*8>entry.length()) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: the number of points seems bad\n"));
        f << "###N=" << N << ",";
        break;
      }
      f << "points=[";
      for (int i=0; i<N; ++i) {
        float dim[2];
        for (float &d : dim) d=float(input->readLong(4))/65536.f;
        shape.m_points.push_back(MWAWVec2f(dim[1],dim[0]));
        f << shape.m_points.back() << ",";
      }
      f << "],";
      if (input->tell()!=entry.end())
        ascFile.addDelimiter(input->tell(),'|');
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
      return true;
    }
    case 10: { // OLnk
      long pos=input->tell();
      if (entry.length()==10) { // special child of DIMN used to keep the relation between 2 lines?
        int val=int(input->readULong(2));
        if (val!=1)
          f << "f0=" << val << ",";
        for (int i=0; i<2; ++i) {
          shape.m_childs.push_back(int(input->readULong(2)));
          f << "child" << i << "=S" << shape.m_childs.back() << ",";
        }
        ascFile.addDelimiter(input->tell(),'|');
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return true;
      }
      if (entry.length()<136) {
        MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the line connector data\n"));
        f << "###sz";
        break;
      }
      MWAWVec2f pts[4];
      for (int i=0; i<4; ++i) {
        float dim[2];
        for (float &d : dim) d=float(input->readLong(4))/65536.f;
        pts[i]=MWAWVec2f(dim[1],dim[0]);
        f << pts[i] << ((i%2)==0 ? "<->" : ",");
      }
      for (int i=0; i<3; ++i) { // f0=small number
        int val=int(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      int type=int(input->readLong(2)); // normally 0-4, 4 is the stair connector
      f << "type=" << type << ",";
      if (type!=4)
        shape.m_points= {pts[0],pts[1]};
      else {
        float c=(pts[0][0]+pts[1][0])/2;
        shape.m_points= {pts[0], MWAWVec2f(c,pts[0][1]), MWAWVec2f(c,pts[1][1]), pts[1]};
      }
      ascFile.addDelimiter(input->tell(),'|');
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return true;
    }
    default:
      f << "###";
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: reading data of a special %d shape is not implemented\n", specialId));
      break;
    }
    break;
  }
  case 55: // will be read by getBitmap
    break;
  case 56: { // bdbox, points
    int N=int(input->readULong(2));
    if (N>entry.length()) // can happen when a file is converted between mac and windows
      N=(N>>8)|((N&0xff)<<8);
    if (N<4 || N>entry.length() || (N%4)!=2) {
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the number of points of a polydata\n"));
      f << "###N=" << N << ",";
      break;
    }
    N/=4;
    if (isWindows)
      input->setReadInverted(false);
    f << "pts=[";
    for (int i=0; i<N; ++i) {
      float dim[2];
      for (auto &d : dim) d=float(input->readLong(2));
      MWAWVec2f pt(dim[1], dim[0]);
      if (i>=2)
        shape.m_points.push_back(pt);
      f << pt << ",";
    }
    f << "],";
    if (isWindows)
      input->setReadInverted(true);
    break;
  }
  case 99: { // group
    int N=int(input->readULong(2));
    if (2+2*N>entry.length()) {
      MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: can not find the number of childs\n"));
      f << "###N=" << N << ",";
      break;
    }
    f << "childs=[";
    for (int i=0; i<N; ++i) {
      shape.m_childs.push_back(int(input->readULong(2)));
      f << "S" << shape.m_childs.back() << ",";
    }
    f << "],";
    break;
  }
  default:
    f << "###type,";
    MWAW_DEBUG_MSG(("CanvasGraph::readShapeData: unexpected type=%d\n", shape.m_type));
    break;
  }
  if (input->tell()!=entry.begin() && input->tell()!=entry.end())
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  return true;
}

bool CanvasGraph::getBitmap(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj)
{
  bool const isWindows=m_mainParser->isWindowsFile();
  if (!isWindows) {
    if (shape.m_values[0]==1) // normally, must not happen...
      return getBitmapBW(shape, obj);
    if (shape.m_values[0]<=0) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: unexpected depth=%d\n", shape.m_values[0]));
      return false;
    }
  }

  MWAWInputStreamPtr input = getInput();
  if (!input || !shape.m_entry.valid() || !input->checkPosition(shape.m_entry.end())) {
    MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: the entry size seems bad\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(BitmapCol):";
  input->seek(shape.m_entry.begin(), librevenge::RVNG_SEEK_SET);

  int nBitsByPixel=0;
  MWAWVec2i dim;
  int width;
  std::vector<MWAWColor> colors;
  if (!isWindows) {
    nBitsByPixel=shape.m_values[0];
    dim=MWAWBox2i(shape.m_box).size();
    int scale=std::abs(shape.m_bitmapType);
    dim *= scale;
    width=(nBitsByPixel*dim[0]+7)/8;
    if (width&1) ++width;
    if (width*dim[1]!=shape.m_entry.length()) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: unexpected size\n"));
      f << "###" << width << "x" << dim[1] << "!=" << shape.m_entry.length();
      ascFile.addPos(shape.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }
  }
  else {
    long headerSize=int(input->readULong(4));
    width=int(input->readULong(2));
    f << "w=" << width << ",";
    int val=int(input->readLong(2)); // 0|-1
    if (val) f << "f0=" << val << ",";
    int numColors=int(input->readULong(2));
    if (numColors==2)
      return getBitmapBW(shape, obj);
    val=int(input->readLong(2)); // 0|-1
    if (val) f << "f1=" << val << ",";
    val=int(input->readULong(4)); // probably a local data size, ie. size before colors
    if (val!=0x28) f << "f2=" << val << ",";
    int dims[2];
    for (auto &d:dims) d=int(input->readLong(4));
    dim=MWAWVec2i(dims[0],dims[1]);
    f << "dim=" << dim << ",";
    val=int(input->readLong(2));
    if (val!=1) f << "f2=" << val << ",";
    nBitsByPixel=int(input->readLong(2));
    if (nBitsByPixel!=8)
      f << "num[bits/pixel]=" << nBitsByPixel << ",";
    if (val<=0) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: unexpected depth\n"));
      f << "###";
      ascFile.addPos(shape.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }
    if (dim[0]<=0 || dim[1]<=0 || width<(dim[0]*nBitsByPixel+7)/8 ||
        headerSize<52+4*numColors || width*dim[1]+headerSize!=shape.m_entry.length()) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: unexpected size\n"));
      f << "###";
      ascFile.addPos(shape.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }
    ascFile.addDelimiter(input->tell(), '|');
    input->seek(shape.m_entry.begin()+52, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(input->tell(), '|');
    for (int i=0; i<numColors; ++i) {
      unsigned char col[4];
      for (auto &c : col) c=(unsigned char)(input->readULong(1));
      colors.push_back(MWAWColor(col[2],col[1],col[0]));
      f << colors.back() << ",";
    }
    input->seek(shape.m_entry.begin()+headerSize, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addPos(shape.m_entry.begin());
  ascFile.addNote(f.str().c_str());

  if (nBitsByPixel!=4 && nBitsByPixel!=8 && nBitsByPixel!=24 && nBitsByPixel!=32) {
    MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: find unexpected depth=%d\n", nBitsByPixel));
    return false;
  }
  if (nBitsByPixel==4 || nBitsByPixel==8) {
    std::vector<MWAWColor> const &fColors= isWindows ? colors : m_styleManager->getColorsList();
    int numColors=int(fColors.size());
    if (numColors<2) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: can not find the picture colors\n"));
      return false;
    }

    MWAWPictBitmapIndexed pict(dim);
    pict.setColors(fColors);
    for (int y=0; y<dim[1]; ++y) {
      long pos=input->tell();
      f.str("");
      f << "BitmapCol" << y << "]:";
      for (int w=0; w<dim[0];) {
        int value=int(input->readULong(1));
        for (int st=0; st<2; ++st) {
          if (w>=dim[0]) break;
          int val;
          if (nBitsByPixel==8) {
            if (st==1)
              break;
            val=value;
          }
          else
            val=(st==0) ? (value>>4) : (value&0xf);
          if (val>numColors) {
            static bool first=true;
            if (first) {
              MWAW_DEBUG_MSG(("CanvasGraph::getBitmap: find unexpected indices\n"));
              first=false;
            }
            pict.set(w, isWindows ? dim[1]-1-y : y, 0);
          }
          else
            pict.set(w, isWindows ? dim[1]-1-y : y, val);
          ++w;
        }
      }
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos+width, librevenge::RVNG_SEEK_SET);
    }
    return pict.getBinary(obj);
  }

  MWAWPictBitmapColor pict(dim, nBitsByPixel==32);
  for (int y=0; y<dim[1]; ++y) {
    long pos=input->tell();
    f.str("");
    f << "BitmapCol" << y << "]:";
    unsigned char cols[4]= {0,0,0,0};
    for (int w=0; w<dim[0]; ++w) {
      for (int c=0; c<nBitsByPixel/8; ++c) cols[c]=(unsigned char)(input->readULong(1));
      if (nBitsByPixel==32)
        pict.set(w, isWindows ? dim[1]-1-y : y, MWAWColor(cols[1], cols[2], cols[3], (unsigned char)(255-cols[0])));
      else
        pict.set(w, isWindows ? dim[1]-1-y : y,
                 isWindows ? MWAWColor(cols[2], cols[1], cols[0]) : MWAWColor(cols[0], cols[1], cols[2]));
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+width, librevenge::RVNG_SEEK_SET);
  }
  return pict.getBinary(obj);
}

bool CanvasGraph::getBitmapBW(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj)
{
  bool const isWindows=m_mainParser->isWindowsFile();
  MWAWInputStreamPtr input = getInput();
  if (!input || !shape.m_entry.valid() || !input->checkPosition(shape.m_entry.end()) ||
      (isWindows&&shape.m_entry.length()<60)) {
    MWAW_DEBUG_MSG(("CanvasGraph::getBitmapBW: the entry size seems bad\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(BitmapBW):";
  MWAWVec2i dim;
  int width=0;
  std::vector<MWAWColor> colors;

  input->seek(shape.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  if (!isWindows) {
    dim=MWAWBox2i(shape.m_box).size();
    int scale=std::abs(shape.m_bitmapType);
    dim *= scale;
    width=(dim[0]+7)/8;
    if (width&1) ++width;
    if (width*dim[1]!=shape.m_entry.length()) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmapBW: unexpected size\n"));
      f << "###" << width << "x" << dim[1] << "!=" << shape.m_entry.length();
      ascFile.addPos(shape.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }
    colors.push_back(shape.m_colors[0]);
    colors.push_back(MWAWColor::white());
  }
  else {
    long headerSize=int(input->readULong(4));
    if (headerSize!=60)
      f << "header[size]=" << headerSize << ",";
    width=int(input->readULong(2));
    f << "w=" << width << ",";
    int val=int(input->readLong(2)); // 0|-1
    if (val) f << "f0=" << val << ",";
    int numColors=int(input->readULong(2));
    if (numColors!=2) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmapBW: the number of colors seems bad\n"));
      f << "##num[colors]=" << numColors << ",";
    }
    val=int(input->readLong(2)); // 0|-1
    if (val) f << "f1=" << val << ",";
    val=int(input->readULong(4)); // probably a local data size, ie. size before colors
    if (val!=0x28)
      f << "f2=" << val << ",";
    int dims[2];
    for (auto &d:dims) d=int(input->readLong(4));
    dim=MWAWVec2i(dims[0],dims[1]);
    f << "dim=" << dim << ",";
    for (int i=0; i<2; ++i) { // f1=numPlanes?, f2=num bits by pixel
      val=int(input->readLong(2));
      if (val!=1)
        f << "f" << i+3 << "=" << val << ",";
    }
    if (dim[0]<=0 || dim[1]<=0 || width<dim[0]/8 ||
        headerSize<60 || width*dim[1]+headerSize!=shape.m_entry.length()) {
      MWAW_DEBUG_MSG(("CanvasGraph::getBitmapBW: unexpected size\n"));
      f << "###";
      ascFile.addPos(shape.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }
    ascFile.addDelimiter(input->tell(), '|');
    input->seek(shape.m_entry.begin()+52, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(input->tell(), '|');
    colors.resize(2);
    for (size_t i=0; i<2; ++i) {
      unsigned char col[4];
      for (auto &c : col) c=(unsigned char)(input->readULong(1));
      colors[1-i]=MWAWColor(col[0],col[1],col[2]);
      f << colors[1-i] << ",";
    }
    input->seek(shape.m_entry.begin()+headerSize, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addPos(shape.m_entry.begin());
  ascFile.addNote(f.str().c_str());

  MWAWPictBitmapIndexed pict(dim);
  pict.setColors(colors);
  for (int y=0; y<dim[1]; ++y) {
    long pos=input->tell();
    f.str("");
    f << "BitmapBW" << y << "]:";
    int x=0;
    for (int w=0; w<width; ++w) {
      int val=int(input->readULong(1));
      for (int v=0, depl=0x80; v<8; ++v, depl>>=1) {
        if (x>=dim[0])
          break;
        pict.set(x++,isWindows ? dim[1]-1-y : y, (val&depl) ? 0 : 1);
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+width, librevenge::RVNG_SEEK_SET);
  }
  return pict.getBinary(obj);
}

bool CanvasGraph::readFileBitmap(long length)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input ? input->tell() : 0;
  long endPos=pos+length;
  if (!input || !input->checkPosition(endPos) || length<40) {
    MWAW_DEBUG_MSG(("CanvasGraph::readFileBitmap: the zone seems to short\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(FileBitmap):";

  long headerSize=long(input->readULong(4));
  int dims[2];
  for (auto &d : dims) d=int(input->readULong(4));
  MWAWVec2i dim(dims[0], dims[1]);
  f << "dim=" << dim << ",";
  if (dim[0]<=0 || dim[1]<=0 || length<=0 || headerSize<40) {
    MWAW_DEBUG_MSG(("CanvasGraph::readFileBitmap: can not read the bitmap definition\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    if (length<=0 || !input->checkPosition(endPos))
      return false;
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }

  int val=int(input->readLong(2));
  if (val!=1) f << "type?=" << val << ",";
  int nBytes=int(input->readULong(2));
  if (nBytes==4)
    f << "n[bytes]=4,";
  else if (nBytes!=8) {
    MWAW_DEBUG_MSG(("CanvasGraph::readFileBitmap: unknown number of bytes\n"));
    f << "###n[bytes]=" << nBytes << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  int const width=nBytes==4 ? 4*((dim[0]/2+3)/4) : 4*((dim[0]+3)/4); // align to 4 bytes
  int const numColors=nBytes==4 ? 16 : 256;
  if (length<headerSize+4*numColors+width*dim[1]) {
    MWAW_DEBUG_MSG(("CanvasGraph::readFileBitmap: can not read the bitmap definition\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  ascFile.addDelimiter(input->tell(), '|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+4+headerSize, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  std::vector<MWAWColor> colors;
  colors.reserve(size_t(numColors));
  for (int i=0; i<numColors; ++i) {
    unsigned char col[4];
    for (auto &c : col) c=(unsigned char)(input->readULong(1));
    colors.push_back(MWAWColor(col[0], col[1], col[2], (unsigned char)(255-col[3])));
  }
  MWAWPictBitmapIndexed pict(dim);
  pict.setColors(colors);
  for (int y=0; y<dim[1]; ++y) {
    long bPos=input->tell();
    if (nBytes==4) {
      for (int w=0; w<dim[0]; w+=2) {
        val=int(input->readULong(1));
        pict.set(w, y, (val>>4));
        if (w+1<dim[0])
          pict.set(w+1, y, (val&0xf));
      }
    }
    else {
      for (int w=0; w<dim[0]; ++w) {
        val=int(input->readULong(1));
        pict.set(w, y, val);
      }
    }
    input->seek(bPos+width, librevenge::RVNG_SEEK_SET);
  }
  ascFile.skipZone(pos, endPos-1);
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
#ifdef DEBUG_WITH_FILES
  MWAWEmbeddedObject obj;
  if (pict.getBinary(obj) && !obj.m_dataList.empty())
    libmwaw::Debug::dumpFile(obj.m_dataList[0], "file.png");
#endif
  return true;
}

bool CanvasGraph::getPicture(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !shape.m_entry.valid() || !input->checkPosition(shape.m_entry.end())) {
    MWAW_DEBUG_MSG(("CanvasGraph::getPicture: the entry size seems bad\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  input->seek(shape.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  MWAWBox2f box;
  int dSz=int(shape.m_entry.length());
  auto res = MWAWPictData::check(input, dSz, box);
  if (res == MWAWPict::MWAW_R_BAD) {
    MWAW_DEBUG_MSG(("CanvasGraph::getPicture:: can not find the picture\n"));
    ascFile.addPos(shape.m_entry.begin());
    ascFile.addNote("Entries(Picture):###");
    return false;
  }
  input->seek(shape.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  std::shared_ptr<MWAWPict> thePict(MWAWPictData::get(input, dSz));
  bool ok=thePict && thePict->getBinary(obj);
#ifdef DEBUG_WITH_FILES
  librevenge::RVNGBinaryData file;
  input->seek(shape.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(dSz, file);
  static int volatile pictName = 0;
  libmwaw::DebugStream s;
  s << "PICT-" << ++pictName << ".pct";
  libmwaw::Debug::dumpFile(file, s.str().c_str());
  if (!ok) {
    ascFile.addPos(shape.m_entry.begin());
    ascFile.addNote("Entries(Picture):###");
  }
  else
    ascFile.skipZone(shape.m_entry.begin(), shape.m_entry.begin()-1+dSz);
#endif
  return ok;
}
////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

void CanvasGraph::markSent(int id)
{
  if (id<=0) return;
#ifdef DEBUG
  auto const it=m_state->m_idToShapeMap.find(id);
  if (it==m_state->m_idToShapeMap.end() || it->second.m_sent) {
    MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find shape %d\n", id));
    return;
  }
  auto const &shape=it->second;
  shape.m_sent=true;
  markSent(shape.m_child);
  markSent(shape.m_origChild);
  for (auto const &cId : shape.m_childs)
    markSent(cId);
#endif
}

void CanvasGraph::checkUnsent() const
{
#ifdef DEBUG
  bool first=true;
  for (auto const &it : m_state->m_idToShapeMap) {
    if (it.first<3 || it.second.m_sent || it.second.m_type==100)
      continue;
    if (first) {
      first=false;
      std::cerr << "Find unsent graphs:";
    }
    std::cerr << it.first << ":" << it.second.m_type << ",";
  }
  if (!first)
    std::cerr << "\n";
#endif
}

////////////////////////////////////////////////////////////
// send data to the listener
////////////////////////////////////////////////////////////

void CanvasGraph::update(CanvasGraphInternal::Shape const &shape, MWAWGraphicStyle &style) const
{
  style.m_lineWidth=(shape.m_penSize[0]+shape.m_penSize[1])/2;
  for (int st=0; st<2; ++st) {
    // no need to compute surface style
    if (st==1 && shape.m_type==3)
      break;
    if (shape.m_patterns[st]==0) {
      // no color
      if (st==0)
        style.m_lineWidth=0;
      continue;
    }
    if (st==0) {
      if (!shape.m_dashWidth.empty())
        style.m_lineDashWidth=shape.m_dashWidth;
      else if (shape.m_dash!=1) {
        switch (shape.m_dash) {
        case 2: // 4-4
        case 3: // 4-2
        case 4: // 8-2
          style.m_lineDashWidth.resize(2);
          style.m_lineDashWidth[0]=(shape.m_dash==3 ? 8 : 4);
          style.m_lineDashWidth[1]=(shape.m_dash==2 ? 4 : 2);
          break;
        case 5: // 8-1,2-1
        case 6: // 8-2,4-2
          style.m_lineDashWidth.resize(4);
          style.m_lineDashWidth[0]=8;
          style.m_lineDashWidth[1]=(shape.m_dash==5 ? 1 : 2);
          style.m_lineDashWidth[2]=(shape.m_dash==5 ? 2 : 4);
          style.m_lineDashWidth[3]=(shape.m_dash==5 ? 1 : 2);
          break;
        case 7:
          style.m_lineDashWidth= {8,1,2,1,2,1};
          break;
        default:
          MWAW_DEBUG_MSG(("CanvasGraph::update[style]: unknown dash style=%d\n", shape.m_dash));
          break;
        }
      }
    }
    if (shape.m_patterns[st]<155) {
      MWAWGraphicStyle::Pattern pat;
      if (!m_styleManager->get(shape.m_patterns[st]-1, pat)) {
        MWAW_DEBUG_MSG(("CanvasGraph::update[style]: can not find patterns %d\n", shape.m_patterns[st]));
      }
      else {
        for (int i=0; i<2; ++i)
          pat.m_colors[1-i]=shape.m_colors[i];
        if (st==0)
          pat.getAverageColor(style.m_lineColor);
        else
          style.setPattern(pat);
      }
    }
    else {
      float percent=float(255-shape.m_patterns[st])/100.f;
      MWAWColor finalColor=MWAWColor::barycenter(percent, shape.m_colors[1], 1.f-percent, shape.m_colors[0]);
      if (st==0)
        style.m_lineColor=finalColor;
      else
        style.setSurfaceColor(finalColor);
    }
  }
  if (shape.m_type==3) {
    // TODO: find where the arrow are stored in the arc's shape
    int fl=shape.m_values[1];
    if (fl&1) {
      style.m_arrows[1]=shape.m_arrow;
      style.m_arrows[1].m_width*=float(style.m_lineWidth);
    }
    if (fl&2) {
      style.m_arrows[0]=shape.m_arrow;
      style.m_arrows[0].m_width*=float(style.m_lineWidth);
    }
  }
}

bool CanvasGraph::send(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const *local)
{
  MWAWGraphicListenerPtr listener=m_parserState->m_graphicListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find the listener\n"));
    return false;
  }
  if (shape.m_sent) {
    MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: find an already sent shape\n"));
    return false;
  }
  shape.m_sent=true;

  int const vers=version();
  MWAWPosition pos=local ? local->m_position : MWAWPosition(shape.m_box[0], shape.m_box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;

  if (shape.m_type==99) {
    if (shape.m_childs.size()>1)
      listener->openGroup(pos);
    for (auto const id : shape.m_childs) {
      auto const it=m_state->m_idToShapeMap.find(id);
      if (it==m_state->m_idToShapeMap.end()) {
        MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find shape %d\n", id));
        continue;
      }
      send(it->second);
    }
    if (shape.m_childs.size()>1)
      listener->closeGroup();
    return true;
  }

  MWAWGraphicStyle style;
  if (local)
    style=local->m_style;
  else
    update(shape, style);
  style.m_rotate=-float(shape.m_rotation);
  int hatchGradChild=shape.m_hatchGradChild;
  if (hatchGradChild>0 && shape.getSpecialId()==-1) { // look for a gradient
    int cChild=hatchGradChild;
    std::set<int> found;
    while (cChild>0) {
      if (found.find(cChild)!=found.end()) {
        MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: find loop in hatch/grad child\n"));
        break;
      }
      found.insert(cChild);
      auto const it=m_state->m_idToShapeMap.find(cChild);
      if (it==m_state->m_idToShapeMap.end()) {
        MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find hatch/grad child=%d\n", cChild));
        break;
      }
      int cId=it->second.getSpecialId();
      if (cId==3) { // gradient
        auto const &gIt=m_state->m_idToGradientMap.find(cChild);
        if (gIt!=m_state->m_idToGradientMap.end())
          style.m_gradient=gIt->second;
        else {
          MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find gradient=%d\n", cChild));
        }
        break;
      }
      cChild=it->second.m_hatchGradChild;
    }
  }

  CanvasGraphInternal::LocalTransform lTransform(pos, style);
  // first check if we need to use the original shape
  if (shape.m_origChild) {
    auto const it=m_state->m_idToShapeMap.find(shape.m_origChild);
    if (it==m_state->m_idToShapeMap.end()) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find the original child\n"));
    }
    // check if original contain text or if original is a bitmap
    // TODO: do we need to use original if the child is a group?
    else if (it->second.m_type==2 || (it->second.m_type>=4 && it->second.m_type<=5 && it->second.m_entry.valid())) {
      send(it->second, &lTransform);
      // TODO: if the form is skewed, distorted, we need to retrieve the shape.m_child to draw the original shape in the shape.m_child :-~
      markSent(shape.m_child);
      return true;
    }
    else if (shape.m_type!=18)
      markSent(shape.m_origChild);
  }
  // now look if the shape has a more precise child, if yes, use it
  if (shape.m_child) {
    auto const it=m_state->m_idToShapeMap.find(shape.m_child);
    if (it==m_state->m_idToShapeMap.end()) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find a child\n"));
    }
    else {
      send(it->second, &lTransform);
      return true;
    }
  }

  bool isSent=false;
  MWAWGraphicShape finalShape;
  switch (shape.m_type) {
  case 2: {
    std::shared_ptr<MWAWSubDocument> doc(new CanvasGraphInternal::SubDocument(*this, getInput(), shape.m_entry.id()));
    listener->insertTextBox(pos, doc, style);
    isSent=true;
    break;
  }
  case 3: // line
    if (shape.m_points.size()!=2) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: oops can not find the line's points\n"));
      return false;
    }
    finalShape=MWAWGraphicShape::line(shape.m_points[0], shape.m_points[1]);
    if (shape.m_values[1]&4) { // measure
      listener->openGroup(pos);
      listener->insertShape(pos, finalShape, style);

      MWAWVec2f lineSz=pos.size();
      MWAWVec2f center=pos.origin() + 0.5f*lineSz;
      MWAWPosition measurePos(pos);
      measurePos.setOrigin(center-MWAWVec2f(30,6));
      measurePos.setSize(MWAWVec2f(60,12));
      measurePos.setOrder(pos.order()+1);
      std::stringstream s;
      s << std::setprecision(0) << std::fixed << std::sqrt(lineSz[0]*lineSz[0]+lineSz[1]*lineSz[1]) << " pt";
      std::shared_ptr<MWAWSubDocument> doc(new CanvasGraphInternal::SubDocument(*this, getInput(), librevenge::RVNGString(s.str().c_str())));
      MWAWGraphicStyle measureStyle;
      measureStyle.m_lineWidth=0;
      measureStyle.setSurfaceColor(MWAWColor::white());
      listener->insertTextBox(measurePos, doc, measureStyle);

      listener->closeGroup();
      isSent=true;
    }
    break;
  case 4: { // rect
    MWAWEmbeddedObject obj;
    if (shape.m_entry.valid() && getBitmapBW(shape, obj)) {
      listener->insertPicture(pos, obj, style);
      isSent=true;
      break;
    }
    finalShape=MWAWGraphicShape::rectangle(shape.m_box);
    break;
  }
  case 5: { // rectOval
    MWAWEmbeddedObject obj;
    finalShape=MWAWGraphicShape::rectangle(shape.m_box, MWAWVec2f(float(shape.m_values[0])/2.f, float(shape.m_values[1])/2.f));
    if (shape.m_entry.valid() && getBitmapBW(shape, obj)) {
      if (style.hasSurface())
        listener->insertShape(pos, finalShape, style);
      listener->insertPicture(pos, obj, style);
      isSent=true;
    }
    break;
  }
  case 6: // circle
    finalShape=MWAWGraphicShape::circle(shape.m_box);
    break;
  case 7: { // arc
    int angle[2] = { int(90-shape.m_values[0]-shape.m_values[1]), int(90-shape.m_values[0]) };
    if (shape.m_values[1]<0) {
      angle[0]=int(90-shape.m_values[0]);
      angle[1]=int(90-shape.m_values[0]-shape.m_values[1]);
    }
    else if (shape.m_values[1]==360)
      angle[0]=int(90-shape.m_values[0]-359);
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
      float constant[2]= { shape.m_box[0][0]-minVal[0] *scaling[0], shape.m_box[0][1]-minVal[1] *scaling[1]};
      circleBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                          MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
    }
    finalShape = MWAWGraphicShape::pie(shape.m_box, circleBox, MWAWVec2f(float(angle[0]), float(angle[1])));
    break;
  }
  case 9: // polyline
  case 56: // polydata
    if (shape.m_points.size()<2) {
      // I find sometimes only one point, probably safe to ignore
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: oops can not find the polyline's points for shape\n"));
      return false;
    }
    if (style.hasSurface())
      finalShape=MWAWGraphicShape::polygon(shape.m_box);
    else
      finalShape=MWAWGraphicShape::polyline(shape.m_box);
    finalShape.m_vertices=shape.m_points;
    break;
  case 10: { // spline
    if (shape.m_points.size()<2 || (shape.m_points.size()%(vers==2 ? 2 : 4))!=0) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: oops can not find the spline's points\n"));
      return false;
    }
    finalShape=MWAWGraphicShape::path(shape.m_box);
    std::vector<MWAWGraphicShape::PathData> &path=finalShape.m_path;
    path.push_back(MWAWGraphicShape::PathData('M', shape.m_points[0]));

    if (vers==2) {
      for (size_t p=2; p < shape.m_points.size(); p+=2) {
        bool hasFirstC=shape.m_points[p-1]!=shape.m_points[p-2];
        bool hasSecondC=shape.m_points[p]!=shape.m_points[p+1];
        if (!hasFirstC && !hasSecondC)
          path.push_back(MWAWGraphicShape::PathData('L', shape.m_points[p]));
        else
          path.push_back(MWAWGraphicShape::PathData('C', shape.m_points[p], shape.m_points[p-1], shape.m_points[p+1]));
      }
    }
    else { // each extremity is dupplicated, so we have 0-1-2-3 4-5-6-7 with P3=P4(almost alway), ...
      for (size_t p=3; p < shape.m_points.size(); p+=4) {
        if (p>=4 && shape.m_points[p-4]!=shape.m_points[p-3])
          path.push_back(MWAWGraphicShape::PathData('M', shape.m_points[p-3]));
        bool hasFirstC=shape.m_points[p-3]!=shape.m_points[p-2];
        bool hasSecondC=shape.m_points[p-1]!=shape.m_points[p];
        if (!hasFirstC && !hasSecondC)
          path.push_back(MWAWGraphicShape::PathData('L', shape.m_points[p]));
        else
          path.push_back(MWAWGraphicShape::PathData('C', shape.m_points[p], shape.m_points[p-2], shape.m_points[p-1]));
      }
    }
    if (style.hasSurface())
      path.push_back(MWAWGraphicShape::PathData('Z'));
    break;
  }
  case 18: {
    if (shape.m_origChild==0 && shape.m_entry.valid()) {
      MWAWEmbeddedObject obj;
      if (getPicture(shape, obj)) {
        listener->insertPicture(pos, obj, style);
        isSent=true;
        break;
      }
    }
    auto const it=m_state->m_idToShapeMap.find(shape.m_origChild);
    if (shape.m_origChild<=0 || it==m_state->m_idToShapeMap.end()) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find picture container child=%d\n", shape.m_origChild));
      return false;
    }
    send(it->second, &lTransform);
    isSent=true;
    break;
  }
  case 52:
    sendSpecial(shape, lTransform);
    isSent=true;
    break;
  case 55: {
    MWAWEmbeddedObject obj;
    if (shape.m_entry.valid() && getBitmap(shape, obj)) {
      listener->insertPicture(pos, obj, style);
      isSent=true;
      break;
    }
    return false;
  }
  default:
    MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: unknown type=%d\n", shape.m_type));
    finalShape=MWAWGraphicShape::rectangle(shape.m_box);
    break;
  }
  if (!isSent)
    listener->insertShape(pos, finalShape, style);
  if (hatchGradChild>0) {
    auto const it=m_state->m_idToShapeMap.find(hatchGradChild);
    if (it==m_state->m_idToShapeMap.end()) {
      MWAW_DEBUG_MSG(("CanvasGraph::send[shape]: can not find hatch/grad child=%d\n", hatchGradChild));
      return false;
    }
    send(it->second);
  }

  return true;
}

bool CanvasGraph::sendDimension(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local)
{
  MWAWGraphicListenerPtr listener=m_parserState->m_graphicListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: can not find the listener\n"));
    return false;
  }

  auto const &entry=shape.m_entry;
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: sorry, can not find the data\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  if (entry.length()<384) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: the data seens too short\n"));
    f << "###sz";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }

  auto input=getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int type=int(input->readLong(2)); // 4-15
  if (type) f << "type=" << type << ",";
  f << "points=[";
  std::vector<MWAWVec2f> pts;
  for (int i=0; i<18; ++i) {
    float dims[2];
    // fract type: between -2 and 2
    for (auto &d : dims) d=4*float(input->readLong(4))/65536.f/65536.f;
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addDelimiter(input->tell()-4,',');
    pts.push_back(MWAWVec2f(dims[1],dims[0]));
    f << pts.back() << ",";
  }
  f << "],";
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  input->seek(entry.begin()+146, librevenge::RVNG_SEEK_SET);

  long posi=input->tell();
  f.str("");
  f << "Shape-" << entry.id() << "[data1," << shape.getTypeName() << "]:";
  input->seek(posi+36, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  bool arrowInside=true;
  bool hasFrame=false;
  int val;
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
  for (int i=0; i<6; ++i) {
    val=int(input->readLong(2));
    int const expected[]= { 1, 1, 1, 0, 3, 1};
    if (val==expected[i]) continue;
    char const *wh[]= {
      "leader",  // none, left, right, automatic
      nullptr,
      "display[text]", // hori, hori/90, aligned, above, below
      "what", // 1: line, 3: arc?
      "precision", // X, X.X, X.XX, X.XXX, X.XXXX, X X/X
      "tolerance", // none, one, two, limit
    };
    if (i==3 && val==3)
      f << "print[angle],";
    else if (wh[i])
      f << wh[i] << "=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  f << "tolerances=[";
  for (int i=0; i<2; ++i) f << float(input->readLong(4))/65536.f << ",";
  f << "],";
  val=int(input->readLong(2));
  if (val!=1)
    f << "f6=" << val << ",";
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());
  input->seek(posi+64, librevenge::RVNG_SEEK_SET);

  posi=input->tell();
  f.str("");
  f << "Shape-" << entry.id() << "[format," << shape.getTypeName() << "]:";
  librevenge::RVNGString format;
  if (m_mainParser->readString(format, 19))
    f << "name=" << format.cstr() << ",";
  else {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: can not read the format's name\n"));
    f << "###format,";
  }
  input->seek(posi+20, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<13; ++i) { // f2=48|4b|6b, f4=0|1
    val=int(input->readLong(2));
    int const expected[]= {1, 0, 0x48/* a flag to know what is changed*/, 0, 0, /* scale in=>scale out, 100, 100*/ 1, 0, 1, 0, 100, 0, 100, 0};
    if (val==expected[i])
      continue;
    if (i==4) {
      if (val==1)
        f << "custom[unit],";
      else
        f << "###custom[unit]=" << val << ",";
    }
    else
      f << "f" << i << "=" << val << ",";
  }
  f << "margins?=[";
  for (int i=0; i<4; ++i) f << float(input->readLong(4))/65536.f << ",";
  f << "],";
  f << "margins2?=[";
  for (int i=0; i<4; ++i) f << float(input->readLong(4))/65536.f << ",";
  f << "],";
  for (int i=0; i<6; ++i) { // g5=1
    val=int(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());
  input->seek(posi+90, librevenge::RVNG_SEEK_SET);

  posi=input->tell();
  f.str("");
  f << "Shape-" << entry.id() << "[data3," << shape.getTypeName() << "]:";
  librevenge::RVNGString name;
  if (m_mainParser->readString(name, 19))
    f << "encoding=" << name.cstr() << ",";
  else {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: can not read the encoding\n"));
    f << "###encoding,";
  }
  input->seek(posi+20, librevenge::RVNG_SEEK_SET);
  if (m_mainParser->readString(name, 63))
    f << "style=" << name.cstr() << ",";
  else {
    MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: can not read the style name\n"));
    f << "###style,";
  }

  ascFile.addPos(posi);
  ascFile.addNote(f.str().c_str());

  MWAWVec2f bDir=shape.m_box.size();
  for (auto &pt : pts)
    pt=shape.m_box[0]+MWAWVec2f(pt[0]*bDir[0], pt[1]*bDir[1]);

  MWAWGraphicStyle style=local.m_style;
  MWAWPosition pos;
  pos.m_anchorTo = MWAWPosition::Page;

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
      MWAW_DEBUG_MSG(("CanvasGraph::sendDimension: can not read compute the sector angles\n"));
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
          float constant[2]= { circleBox[0][0]-minVal[0] *scaling[0], circleBox[0][1]-minVal[1] *scaling[1]};
          arcBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                           MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
        }
        style.setSurfaceColor(MWAWColor::white(), 0);
        style.m_arrows[st]=arrowInside ? MWAWGraphicStyle::Arrow::plain(): MWAWGraphicStyle::Arrow();
        style.m_arrows[1-st]=MWAWGraphicStyle::Arrow::plain();

        fShape = MWAWGraphicShape::arc(arcBox, circleBox, MWAWVec2f(float(angle[0]), float(angle[1])));
        shapeBox=fShape.getBdBox();
        pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
        listener->insertShape(pos, fShape, style);
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
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
    listener->insertShape(pos, fShape, style);

    fShape=MWAWGraphicShape::line(pts[1],pts[3]);
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    listener->insertShape(pos, fShape, style);

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
      shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, style);
    }
  }
  else {
    for (size_t i=0; i<2; ++i) {
      size_t const limits[]= {4,6, 7,9 }; // outside1, outside2
      fShape=MWAWGraphicShape::line(pts[limits[2*i]],pts[limits[2*i+1]]);
      shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, style);
    }

    if (arrowInside) {
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
      fShape=MWAWGraphicShape::line(pts[5],pts[8]);
      shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, style);
    }
    else {
      style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
      for (size_t i=0; i<2; ++i) {
        size_t const limits[]= {5,10, 8,11 }; // arrows1, arrows2
        fShape=MWAWGraphicShape::line(pts[limits[2*i]],pts[limits[2*i+1]]);
        shapeBox=fShape.getBdBox();
        pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
        listener->insertShape(pos, fShape, style);
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

  if (!text.empty()) {
    // TODO: use local style to define the text's color...
    style=MWAWGraphicStyle();
    MWAWPosition measurePos(pos);
    measurePos.m_anchorTo = MWAWPosition::Page;
    measurePos.setOrigin(textOrigin-MWAWVec2f(30,6));
    measurePos.setSize(MWAWVec2f(60,12));
    std::shared_ptr<MWAWSubDocument> doc(new CanvasGraphInternal::SubDocument(*this, getInput(), text));
    MWAWGraphicStyle measureStyle;
    measureStyle.m_lineWidth=hasFrame ? 1 : 0;
    measureStyle.setSurfaceColor(MWAWColor::white());
    listener->insertTextBox(measurePos, doc, measureStyle);
  }
  listener->closeGroup();
  return true;
}

bool CanvasGraph::sendMultiLines(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local)
{
  MWAWGraphicListenerPtr listener=m_parserState->m_graphicListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find the listener\n"));
    return false;
  }

  auto const &entry=shape.m_entry;
  auto input=getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: sorry, can not find the data\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  bool const isWindows=m_mainParser->isWindowsFile();
  int const headerSize=24+(isWindows ? 2 : 0);
  long const lineSize=isWindows ? 182 : 174;
  if (entry.length()<headerSize+lineSize) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: the data seens too short\n"));
    f << "###sz";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int numLines=0, numPoints=0, type=0;
  bool addEndCaps=false;
  int val;
  if (isWindows) {
    val=int(input->readLong(2));
    if (val) f << "f0=" << val << ","; // find 2
  }
  for (int i=0; i<6; ++i) {
    val=int(input->readLong(2));
    if (val==0)
      continue;
    char const *wh[]= {"num[points]", "type" /*17,18,20*/, "num[lines]", "equidistant", "identical", "end[caps]"};
    if (wh[i]==nullptr)
      f << "f" << i+1 << "=" << val << ",";
    else if (i==0) {
      numPoints=val;
      f << "num[pts]=" << val << ",";
    }
    else if (i==1) {
      type=val;
      if (val!=17)
        f << "type=" << val << ",";
    }
    else if (i==2) {
      numLines=val;
      f << "num[lines]=" << val << ",";
    }
    else {
      if (val==1) {
        if (i==5) addEndCaps=true;
        f << wh[i] << ",";
      }
      else
        f << "#" << wh[i] << "=" << val << ",";
    }
  }
  float dim[4];
  for (float &d : dim) d=float(input->readLong(4))/65536.f;
  MWAWVec2f pts[]= {MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2])};
  f <<  MWAWBox2f(pts[0], pts[1]) << ",";
  if (numLines<=0 || entry.length()<headerSize+numLines*lineSize) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find the paln lines\n"));
    f << "###lines";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  std::vector<float> offsets;
  std::vector<MWAWGraphicStyle> styles;
  styles.resize(size_t(numLines));
  for (int i=0; i<numLines; ++i) {
    auto &style=styles[size_t(i)];
    long posi=input->tell();
    f.str("");
    f << "Shape-" << entry.id() << "[line" << i << "," << shape.getTypeName() << "]:";
    val=int(input->readLong(2));
    int offsetSign=1;
    if (val==-1 || val==1) {
      offsetSign=-1;
      f << "offsetNeg,";
    }
    else if (val) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: unknown offset sign\n"));
      f << "###offsetSign=" << val << ",";
    }
    int pattern=int(input->readLong(2));
    if (pattern!=1) f << "pat=" << pattern << ",";
    float w[2]= {1,1};
    if (isWindows) {
      for (auto &we : w) we=float(input->readULong(4))/65536.f;
      f << "w=" << w[0] << "x" << w[1] << ",";
    }
    else {
      w[0]=float(input->readLong(2)); // horizontal width
      if (w[0]<1 || w[0]>1)
        f << "w[hori]=" << w[0] << ",";
    }
    float offset=float(input->readULong(4))/65536.f;
    if (offset>0 || offset<0) f << "offset=" << offset << ",";
    offsets.push_back(float(offsetSign)*offset);
    val=int(input->readULong(2));
    if (val!=0x8000)
      f << "fl=" << std::hex << val << std::dec << ",";
    MWAWColor colors[2]; // front back color
    for (int st=0; st<2; ++st) {
      unsigned char col[3];
      for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
      MWAWColor color(col[0],col[1],col[2]);
      if (st==0)
        colors[0]=color;
      if (color!=(st==0 ? MWAWColor::black() : MWAWColor::white()))
        f << "col" << st << "=" << color << ",";
    }
    for (int j=0; j<3; ++j) {
      val=int(input->readULong(2));
      int const expected[]= {0, 0x7c, 0xa5};
      if (val!=expected[j]) f << "f" << j+2 << "=" << val << ",";
    }
    for (int j=0; j<9; ++j) {
      val=int(input->readULong(2));
      if (val) f << "g" << j << "=" << val << ",";
    }
    for (int j=0; j<3; ++j) {
      val=int(input->readULong(j==2 ? 2 : 4));
      int const expected[]= {0x184508, 0x1844f8, 0x8018};
      if (val!=expected[j]) f << "id" << j << "=" << std::hex << val << std::dec << ",";
    }
    for (int st=0; st<2; ++st) {
      unsigned char col[3];
      for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
      MWAWColor color(col[0],col[1],col[2]) ;
      if (st==0) colors[1]=color;
      if (color!=(st==1 ? MWAWColor::black() : MWAWColor::white()))
        f << "col" << st+2 << "=" << color << ",";
    }
    for (int j=0; j<2; ++j) {
      val=int(input->readLong(2));
      if (val) f << "h" << j << "=" << val << ",";
    }
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());

    posi=input->tell();
    f.str("");
    f << "Shape-" << entry.id() << "[line" << i << "A," << shape.getTypeName() << "]:";
    val=int(input->readLong(2));
    if (val!=2) f << "f0=" << val << ",";
    for (int j=0; j<6; ++j) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << j+1 << "=" << val << ",";
    }
    for (int j=0; j< 7; ++j) {
      val=int(input->readLong(2));
      int const expected[]= {1, 0, 0, 0, 1, 1, 0};
      if (val!=expected[j])
        f << "f" << j+7 << "=" << val << ",";
    }
    float iDim[4];
    for (auto &d : iDim) d=isWindows ? float(input->readLong(4))/65536.f : float(input->readLong(2));
    f << MWAWBox2f(MWAWVec2f(iDim[1],iDim[0]),MWAWVec2f(iDim[3],iDim[2])) << ",";
    int N=int(input->readULong(2));
    if (N>12) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find the number of dashes\n"));
      f << "###dash=" << N << ",";
    }
    else if (N) {
      f << "dash=[";
      for (int d=0; d<N; ++d) {
        style.m_lineDashWidth.push_back(float(input->readLong(4))/65536);
        f << style.m_lineDashWidth.back() << ",";
      }
      f << "],";
    }
    ascFile.addDelimiter(input->tell(),'|');
    if (!isWindows) {
      input->seek(posi+94, librevenge::RVNG_SEEK_SET);
      ascFile.addDelimiter(input->tell(),'|');
      w[1]=float(input->readLong(2));
      if (w[1]<1 || w[1]>1) f << "w[vert]=" << w[1] << ",";
      for (int j=0; j<2; ++j) {
        val=int(input->readLong(2));
        if (val)
          f << "g" << j << "=" << val << ",";
      }
    }
    input->seek(posi+100+(isWindows ? 2 : 0), librevenge::RVNG_SEEK_SET);
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
    // time to update the style
    style.m_lineWidth=(w[0]+w[1])/2;
    if (pattern<155) {
      MWAWGraphicStyle::Pattern pat;
      if (!m_styleManager->get(pattern-1, pat)) {
        MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find patterns %d\n", pattern));
      }
      else {
        for (int j=0; j<2; ++j)
          pat.m_colors[1-j]=colors[j];
        pat.getAverageColor(style.m_lineColor);
      }
    }
    else {
      float percent=float(255-pattern)/100.f;
      MWAWColor finalColor=MWAWColor::barycenter(percent, colors[1], 1.f-percent, colors[0]);
      style.m_lineColor=finalColor;
    }
  }

  long posi=input->tell();
  std::vector<MWAWVec2f> points;
  if (posi!=entry.end()) {
    f.str("");
    f << "Shape-" << entry.id() << "[points," << shape.getTypeName() << "]:";
    if (posi+numPoints*8 <= entry.end()) {
      for (int j=0; j<numPoints; ++j) {
        float pt[2];
        for (auto &p : pt) p=float(input->readLong(4))/65536;
        points.push_back(MWAWVec2f(pt[1],pt[0]));
        f << points.back() << ",";
      }
    }
    else {
      MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find retrieve some points=%d\n", numPoints));
      f << "##N=" << numPoints << ",";
    }
    ascFile.addPos(posi);
    ascFile.addNote(f.str().c_str());
  }
  auto N=points.size();
  bool ok=type==17 ? N==0 : N>=2;
  if (!ok) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendMultiLines: can not find points for type=%d\n", type));
    return false;
  }
  if (N==0) {
    points= {pts[0], pts[1]};
    N=2;
  }

  // times to draw the shapes

  MWAWPosition pos;
  pos.m_anchorTo = MWAWPosition::Page;
  listener->openGroup(local.m_position);
  std::vector<MWAWVec2f> originals;
  MWAWGraphicShape fShape;
  MWAWVec2f bDir=shape.m_box.size();
  for (auto const &pt : points)
    originals.push_back(shape.m_box[0]+MWAWVec2f(pt[0]*bDir[0], pt[1]*bDir[1]));
  bool hasSurface=local.m_style.hasSurface();
  if (hasSurface && numLines>=2 && type!=20) {
    // first draw the surface
    fShape=MWAWGraphicShape::polygon(shape.m_box);
    for (size_t p=0; p<N; ++p)
      fShape.m_vertices.push_back
      (CanvasGraphInternal::getOffsetPoint(originals, p, offsets[0]));
    for (size_t p=N; p>0; --p)
      fShape.m_vertices.push_back
      (CanvasGraphInternal::getOffsetPoint(originals, p-1, offsets[size_t(numLines-1)]));
    auto shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    auto style=local.m_style;
    style.m_lineWidth=0;
    listener->insertShape(pos, fShape, style);
  }
  // now draw the line
  fShape=type==20 ? MWAWGraphicShape::path(shape.m_box) :
         MWAWGraphicShape::polyline(shape.m_box);
  for (int l=0; l<numLines; ++l) {
    fShape.m_vertices.clear();
    for (size_t p=0; p<N; ++p)
      fShape.m_vertices.push_back
      (CanvasGraphInternal::getOffsetPoint(originals, p, offsets[size_t(l)]));
    if (type==20) {
      // recreate the spline (fixme: do that correctly)
      fShape.m_path.clear();
      std::vector<MWAWGraphicShape::PathData> &path=fShape.m_path;
      std::vector<MWAWVec2f> newPoints=CanvasGraphInternal::smoothPoints(fShape.m_vertices);
      path.push_back(MWAWGraphicShape::PathData('M', newPoints[0]));
      for (size_t j=1; j<newPoints.size(); ++j) {
        MWAWVec2f dir=newPoints[j+1==newPoints.size() ? j : j+1]-newPoints[j-1];
        fShape.m_path.push_back(MWAWGraphicShape::PathData('S', newPoints[j], newPoints[j]-0.1f*dir));
      }
    }
    auto shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, styles[size_t(l)]);
  }
  // close the borders
  fShape=MWAWGraphicShape::polyline(shape.m_box);
  if (numLines>=2 && addEndCaps) {
    for (int bo=0; bo<2; ++bo) {
      fShape.m_vertices.clear();
      size_t wh=bo==0 ? 0 : N-1;
      for (int w=0; w<2; ++w)
        fShape.m_vertices.push_back
        (CanvasGraphInternal::getOffsetPoint(originals, wh, offsets[w==0 ? 0 : size_t(numLines-1)]));
      auto shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, styles[bo==1 ? 0 : size_t(numLines-1)]);
    }
  }
  listener->closeGroup();
  return true;
}

bool CanvasGraph::sendSpecial(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local)
{
  MWAWGraphicListenerPtr listener=m_parserState->m_graphicListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: can not find the listener\n"));
    return false;
  }
  int id=shape.getSpecialId();
  auto const &box=shape.m_box;
  MWAWGraphicShape fShape;
  MWAWPosition pos;
  pos.m_anchorTo = MWAWPosition::Page;
  switch (id) {
  case 0: { // cube
    if (shape.m_points.size()!=8) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: can not find the cube points\n"));
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
    MWAWVec2f const dir=shape.m_box[1]-shape.m_box[0];
    MWAWVec2f const dirs[]= {shape.m_points[1]-shape.m_points[0],
                             shape.m_points[2]-shape.m_points[0],
                             shape.m_points[4]-shape.m_points[0]
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
        MWAWVec2f const &pt=shape.m_points[size_t(faces[4*face+p])];
        fShape.m_vertices[p]=shape.m_box[0]+MWAWVec2f(pt[0]*dir[0], pt[1]*dir[1]);
      }
      fShape.m_bdBox=shapeBox;

      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 1:
    return sendDimension(shape, local);
  case 2: { // grid
    listener->openGroup(local.m_position);
    if (shape.m_values[0]<=0 || shape.m_values[1]<=0 ||
        shape.m_values[0]>100 || shape.m_values[1]>100) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial[grid]: can not find the number of rows/columns\n"));
      return false;
    }
    MWAWVec2f dim((box[1][0]-box[0][0])/float(shape.m_values[0]),
                  (box[1][1]-box[0][1])/float(shape.m_values[1]));
    for (int i=0; i<=shape.m_values[0]; ++i) {
      float X=box[0][0]+float(i)*dim[0];
      fShape=MWAWGraphicShape::line(MWAWVec2f(X,box[0][1]), MWAWVec2f(X,box[1][1]));
      MWAWBox2f shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, local.m_style);
    }
    for (int j=0; j<=shape.m_values[1]; ++j) {
      float Y=box[0][1]+float(j)*dim[1];
      fShape=MWAWGraphicShape::line(MWAWVec2f(box[0][0],Y), MWAWVec2f(box[1][0],Y));
      MWAWBox2f shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 3: // ObFl: done
    break;
  case 4: // Paln
    return sendMultiLines(shape, local);
  case 5: { // QkTm
    if (shape.m_entry.valid()) {
      // TODO replace this code when we find how to read data
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial[QkTm]: sorry, reading QkTm data is not implemented\n"));
    }
    listener->openGroup(local.m_position);
    // box
    fShape=MWAWGraphicShape::rectangle(box);
    MWAWBox2f shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, local.m_style);
    // diag1 line
    fShape=MWAWGraphicShape::line(box[0], box[1]);
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, local.m_style);
    // diag2 line
    fShape=MWAWGraphicShape::line(MWAWVec2f(box[0][0], box[1][1]), MWAWVec2f(box[1][0], box[0][1]));
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, local.m_style);
    listener->closeGroup();
    break;
  }
  case 6: { // regP
    MWAWGraphicStyle style;
    listener->openGroup(local.m_position);
    MWAWVec2f center=0.5f*(box[0]+box[1]);
    // H line
    fShape=MWAWGraphicShape::line(MWAWVec2f(box[0][0], center[1]), MWAWVec2f(box[1][0], center[1]));
    MWAWBox2f shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, style);
    // V line
    fShape=MWAWGraphicShape::line(MWAWVec2f(center[0], box[0][1]), MWAWVec2f(center[0], box[1][1]));
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, style);
    // circle
    style.m_lineWidth=2;
    MWAWVec2f delta=0.2*(box[1]-box[0]);
    fShape=MWAWGraphicShape::circle(MWAWBox2f(box[0]+delta,box[1]-delta));
    shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, style);

    listener->closeGroup();
    break;
  }
  case 7: { // hatch
    if (shape.m_points.empty() || (shape.m_points.size()%2)) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: sorry, can not find the hatch line\n"));
      break;
    }
    MWAWGraphicStyle style;
    listener->openGroup(local.m_position);
    for (size_t p=0; p+1<shape.m_points.size(); p+=2) {
      fShape=MWAWGraphicShape::line(shape.m_points[p], shape.m_points[p+1]);
      MWAWBox2f shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 8: // Enve, looks like a secondary representation, ...
    break;
  case 9: { // CCir
    if (shape.m_values[0]<=0 || shape.m_values[0]>20) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: sorry, the number of circles seems bad\n"));
      break;
    }
    listener->openGroup(local.m_position);
    MWAWVec2f center=0.5f*(box[0]+box[1]);
    MWAWVec2f diag=0.5f*box.size();
    for (int i=0; i<shape.m_values[0]; ++i) {
      MWAWVec2f newDiag;
      if (shape.m_values[1]<=0)
        newDiag=float(shape.m_values[0]-i)/float(shape.m_values[0])*diag;
      else {
        newDiag=diag-float(shape.m_values[1]*i)*MWAWVec2f(1,1);
        for (int c=0; c<2; ++c) {
          if (newDiag[c]<0)
            newDiag[c]=0;
        }
      }
      fShape=MWAWGraphicShape::circle(MWAWBox2f(center-newDiag, center+newDiag));
      MWAWBox2f shapeBox=fShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, fShape, local.m_style);
    }
    listener->closeGroup();
    break;
  }
  case 10: { // OLnk
    if (!shape.m_childs.empty()) // child of a DIMN node, safe to ignore...
      break;
    if (shape.m_points.size()<2) {
      MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: sorry, can not find the connector points\n"));
      break;
    }
    fShape=MWAWGraphicShape::polyline(box);
    fShape.m_vertices=shape.m_points;
    MWAWBox2f shapeBox=fShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    listener->insertShape(pos, fShape, local.m_style);
    break;
  }
  default:
    MWAW_DEBUG_MSG(("CanvasGraph::sendSpecial: sorry, sending type=%d is not implemented\n", id));
    break;
  }
  return true;
}

bool CanvasGraph::sendText(int zId)
{
  auto const it=m_state->m_idToShapeMap.find(zId);
  if (it==m_state->m_idToShapeMap.end()) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not find shape %d\n", zId));
    return false;
  }
  return sendText(it->second);
}

bool CanvasGraph::sendText(CanvasGraphInternal::Shape const &shape)
{
  auto input=getInput();
  MWAWGraphicListenerPtr listener=m_parserState->m_graphicListener;
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not find the listener\n"));
    return false;
  }
  auto const &entry=shape.m_entry;
  int const vers=version();
  if (shape.m_type!=2 || !entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: unexpected type for a text shape=%d\n", shape.m_type));
    return false;
  }
  MWAWParagraph para;
  if (vers==2) {
    switch (shape.m_align) {
    case 0: // left
      break;
    case 1:
      para.m_justify=MWAWParagraph::JustificationCenter;
      break;
    case 2:
      para.m_justify=MWAWParagraph::JustificationRight;
      break;
    default:
      MWAW_DEBUG_MSG(("CanvasGraph::sendText: find align=%d\n", shape.m_align));
      break;
    }
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Text)[S" << entry.id() << "]:";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();
  for (int i=0; i<9; ++i) {
    int val=int(input->readLong(2));
    int const expected[]= {3,0,12,0,0,0,-2,0,0};
    if (val==expected[i]) continue;
    if (i==8 && vers>=3) {
      switch (val) {
      case 0: // left
        break;
      case 1:
        f << "center,";
        para.m_justify=MWAWParagraph::JustificationCenter;
        break;
      case -1:
        f << "right,";
        para.m_justify=MWAWParagraph::JustificationRight;
        break;
      case 4:
        f << "full,";
        para.m_justify=MWAWParagraph::JustificationFull;
        break;
      default:
        MWAW_DEBUG_MSG(("CanvasGraph::sendText: find align=%d\n", val));
        f << "##align=" << val << ",";
        break;
      }
    }
    f << "f" << i << "=" << val << ",";
  }
  long dims[4];
  dims[0]=int(input->readULong(4));
  f << "N[char]=" << dims[0] << ",";
  int val=int(input->readULong(2));
  if (val&1)
    f << "sym[hor],";
  if (val&2)
    f << "sym[ver],";
  if (val&0xfffc)
    f << "sym?=" << std::hex << (val&0xfffc) << std::dec << ",";
  val=int(input->readLong(2)); // 0
  if (val) f << "rot=" << val << ",";
  f << "dim=[";
  for (int i=1; i<4; ++i) {
    dims[i]=int(input->readULong(4));
    if (dims[i])
      f << dims[i] << ",";
    else
      f << "_,";
  }
  f << "],";
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "TextA:";
  if (47+dims[0]>entry.length()) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not find the text\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::string text;
  long textPos=input->tell();
  for (int i=0; i<dims[0]; ++i) text+=char(input->readULong(1));
  f << text;
  if (vers>=3 && (dims[0]&1)) input->seek(1, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Text[plc-header]:";
  bool ok=true;
  if (dims[1]<16 || pos+dims[1]>endPos) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not read the plc zone\n"));
    ok=false;
  }
  int N[2]= {0,0};
  if (ok) {
    for (int i=0; i<2; ++i) {
      N[i]=int(input->readULong(2));
      f << "N" << i << "=" << N[i] << ",";
    }
    f << "ids=[";
    for (int i=0; i<4; ++i)
      f << std::hex << input->readULong(4) << std::dec << ",";
    f << "],";
  }
  int const fontSz=vers==2 ? 18 : 50;
  if (ok && (20+(N[0]+1)*(vers==2 ? 4 : 6)>dims[1] || input->tell()+(N[0]+1)*4+N[1]*fontSz>entry.end())) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not find the format size\n"));
    f << "###";
    ok=false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::map<int,int> posToFontIdMap;
  std::vector<MWAWFont> fonts;
  if (ok) {
    pos=input->tell();
    f.str("");
    f << "Text-plc:";
    for (int i=0; i<N[0]+1; ++i) {
      int cPos=int(input->readULong(vers==2 ? 2 : 4));
      int fId=int(input->readULong(2));
      f << cPos << ":F" << fId << ",";
      posToFontIdMap[cPos]=fId;
    }
    if (dims[1]!=20+(N[0]+1)*4) {
      ascFile.addDelimiter(input->tell(),'|');
      input->seek(pos+dims[1]-20, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  if (ok && fontSz*N[1]>dims[3]) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not find the font\n"));
    f << "###";
    ok=false;
  }
  if (ok) {
    long endFontPos=input->tell()+dims[3];
    auto fontConverter=m_parserState->m_fontConverter;
    fonts.resize(size_t(N[1]));
    for (size_t i=0; i<size_t(N[1]); ++i) {
      pos=input->tell();
      MWAWFont &font=fonts[i];
      f.str("");
      val=int(input->readLong(2));
      if (val!=1)
        f << "used=" << val << ",";
      f << "dims?=["; // related to h?
      for (int j=0; j<2; ++j) f << std::hex << input->readULong(2) << std::dec << ",";
      f << "],";
      font.setId(int(input->readULong(2)));
      int fl=int(input->readULong(1));
      uint32_t flags = 0;
      if (fl&0x1) flags |= MWAWFont::boldBit;
      if (fl&0x2) flags |= MWAWFont::italicBit;
      if (fl&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
      if (fl&0x8) flags |= MWAWFont::embossBit;
      if (fl&0x10) flags |= MWAWFont::shadowBit;
      if (fl&0xe0) f << "fl=" << std::hex << (fl&0xe0) << std::dec << ",";
      val=int(input->readULong(1));
      if (val) f << "fl1=" << std::hex << val << std::dec;
      font.setSize(float(input->readULong(2)));
      unsigned char col[3];
      for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
      font.setColor(MWAWColor(col[0],col[1], col[2]));
      if (fontSz>=50) {
        for (int j=0; j<10; ++j) {
          val=int(input->readLong(2));
          int const expected=(j>=2 && j<=5) ? 1 : 0;
          if (val==expected) continue;
          if (j==0) { // normally between -2 and 2
            if (val>0 && val<6)
              font.setDeltaLetterSpacing(1+float(val)*0.3f, librevenge::RVNG_PERCENT);
            else if (val>=-6 && val<0)
              font.setDeltaLetterSpacing(float(val)/2, librevenge::RVNG_POINT);
            else {
              MWAW_DEBUG_MSG(("CanvasGraph::sendText: unknown delta spacing\n"));
              f << "##delta[spacing]=" << val << ",";
            }
          }
          else if (j==6)
            font.set(MWAWFont::Script(float(val),librevenge::RVNG_POINT));
          else if (j==9) {
            if (val&1)
              flags |= MWAWFont::smallCapsBit;
            if (val&2)
              flags |= MWAWFont::uppercaseBit;
            if (val&4)
              flags |= MWAWFont::lowercaseBit;
            if (val&8)
              flags |= MWAWFont::initialcaseBit;
            val &= 0xFFF0;
            if (val) {
              MWAW_DEBUG_MSG(("CanvasGraph::sendText: unknown small caps bits\n"));
              f << "##smallCaps=" << val << ",";
            }
          }
          else
            f << "f" << j << "=" << val << ",";
        }
      }
      font.setFlags(flags);
      std::string const extra=f.str();
      f.str("");
      f << "Text-F" << i << ":" << font.getDebugString(fontConverter) << extra;
      if (input->tell()!=pos+fontSz)
        ascFile.addDelimiter(input->tell(),'|');
      input->seek(pos+fontSz, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    input->seek(endFontPos, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  std::vector<float> lineHeights;
  if (dims[2]<4 || pos+dims[2]>endPos) {
    MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not read the line zone\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Shape-data,text###");
    ok=false;
  }
  else if (ok) {
    f.str("");
    f << "Text-line:";
    bool useDouble=vers>2;
    if (vers==2) { // v2.0 use float, v2.1 double?
      input->seek(2, librevenge::RVNG_SEEK_CUR);
      useDouble=input->readULong(2)==0;
      input->seek(-4, librevenge::RVNG_SEEK_CUR);
    }
    if (!useDouble) {
      for (int i=0; i<int(dims[2]/4); ++i) {
        lineHeights.push_back(float(input->readULong(2)));
        f << lineHeights.back() << "<->" << input->readULong(2) << ","; // max height, min height?
      }
    }
    else {
      int num=int(dims[2]/8);
      for (int i=0; i<num-1; ++i) {
        lineHeights.push_back(float(input->readULong(4))/65536);
        f << lineHeights.back() << "<->" << float(input->readULong(4))/65536 << ","; // max height, min height?
      }
    }
    if (input->tell() != pos+dims[2])
      ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+dims[2], librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  size_t numLines=0;
  bool newLine=true;
  // time to send the text
  long actPos=input->tell();
  input->seek(textPos, librevenge::RVNG_SEEK_SET);
  listener->setParagraph(para);
  for (int i=0; i<dims[0]; ++i) {
    if (newLine) {
      if (numLines<lineHeights.size() && lineHeights[numLines]>0)
        para.setInterline(lineHeights[numLines]>100 ? 100 : lineHeights[numLines], librevenge::RVNG_POINT);
      else
        para.setInterline(1, librevenge::RVNG_PERCENT);
      listener->setParagraph(para);
      newLine=false;
    }
    auto it=posToFontIdMap.find(i);
    if (it!=posToFontIdMap.end()) {
      if (it->second<0 || it->second>=int(fonts.size())) {
        MWAW_DEBUG_MSG(("CanvasGraph::sendText: can not read find the font=%d\n", it->second));
      }
      else
        listener->setFont(fonts[size_t(it->second)]);
    }
    unsigned char c=(unsigned char)(input->readULong(1));
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      if (i+1==dims[0])
        break;
      listener->insertEOL();
      newLine=true;
      break;
    default:
      if (c<=0x1f) {
        MWAW_DEBUG_MSG(("CanvasGraph::sendText: find unexpected char=%x\n", (unsigned int)(c)));
      }
      else
        listener->insertCharacter(c);
    }
  }
  if (!ok)
    return false;
  input->seek(actPos, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  if (pos!=endPos) { // v2 empty (or 1 char), v3 a DeR2 zone, v2.1 ?
    f.str("");
    f << "Text-end:";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
