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

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "FreeHandParser.hxx"

/** Internal: the structures of a FreeHandParser */
namespace FreeHandParserInternal
{
//! the different zone type
enum ZoneType { Z_Unknown, Z_Color, Z_ColorGroup, Z_Dash, Z_DashGroup, Z_Data, Z_Fill, Z_FillGroup, Z_Group,
                Z_LineStyle, Z_LineStyleGroup, Z_Note, Z_Picture, Z_PictureName, Z_String, Z_Shape, Z_StyleGroup
              };

/** struct which defines the screen parameters in FreeHandParserInternal */
struct ScreenMode {
  //! constructor
  ScreenMode()
    : m_function(0)
    , m_angle(0)
    , m_lineByInch(0)
    , m_value(0)
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ScreenMode const &screen)
  {
    switch (screen.m_function) {
    case 0: // unset
    case -1: // default
      break;
    case 1:
      o << "function=round,";
      break;
    case 2:
      o << "function=line,";
      break;
    default:
      MWAW_DEBUG_MSG(("FreeHandParserInternal::operator<<(ScreenMode): find unexpected screen function\n"));
      o << "function=###" << screen.m_function << ",";
      break;
    }
    if (screen.m_angle < 0 || screen.m_angle>0)
      o << "angle=" << screen.m_angle << ",";
    if (screen.m_lineByInch==0xFFFF)
      o << "lineByInch*,";
    else
      o << "lineByInch=" << screen.m_lineByInch << ",";
    if (screen.m_value)
      o << "unkn0=" << screen.m_value << ",";
    return o;
  }

  //! the  function
  int m_function;
  //! the  angle
  float m_angle;
  //! the  line/inch
  int m_lineByInch;
  //! unknow value
  int m_value;
};

/** small structure of FreeHandParserInternal used to stored
    a shape header
*/
struct ShapeHeader {
  //! constructor
  ShapeHeader()
    : m_size(0)
    , m_type(0)
    , m_note("")
    , m_dataId(0)
    , m_layerId(-1)
    , m_screen()
    , m_extra("")
  {
    for (auto &value : m_values) value=0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ShapeHeader const &shape)
  {
    if (shape.m_layerId>=0) o << "layer=" << shape.m_layerId << ",";
    if (shape.m_dataId) o << "data=Z" << shape.m_dataId << ",";
    if (!shape.m_note.empty()) o << "note=\"" << shape.m_note << "\",";
    if (shape.m_screen.isSet()) o << "screen=[" << *shape.m_screen << "],";
    for (int i=0; i<3; ++i) {
      if (!shape.m_values[i])
        continue;
      int val=shape.m_values[i];
      if (i==1 && (val&1)) {
        o << "locked,";
        val&=0xFFFE;
      }
      if (val)
        o << "unkn" << i << "=" << val << ",";
    }
    if (shape.m_values[3]) // always a 1005 zone ?
      o << "unknZone=Z" << shape.m_values[3] << ",";
    o << shape.m_extra;
    return o;
  }
  //! a field related to the zone size
  long m_size;
  //! the zone type
  int m_type;
  //! the note
  std::string m_note;
  //! the data id (used to store a note, ...)
  int m_dataId;
  //! the layer id
  int m_layerId;
  //! the screen mode
  MWAWVariable<ScreenMode> m_screen;
  //! the unknown values
  int m_values[4];
  //! extra data
  std::string m_extra;
};

/** small structure of FreeHandParserInternal used to stored
    a fill style
*/
struct FillStyle {
  //! constructor
  FillStyle()
    : m_type(MWAWGraphicStyle::Gradient::G_None)
    , m_pattern()
    , m_angle(0)
    , m_logarithm(false)
  {
    for (auto &colorId : m_colorId) colorId=0;
  }
  //! the gradient type
  MWAWGraphicStyle::Gradient::Type m_type;
  //! the color id
  int m_colorId[2];
  //! the pattern
  MWAWGraphicStyle::Pattern m_pattern;
  //! the angle
  float m_angle;
  //! flag to know if a flag has logarithmic scale
  bool m_logarithm;
};

/** small structure of FreeHandParserInternal used to stored
    a line style
*/
struct LineStyle {
  //! constructor
  LineStyle()
    : m_width(1)
    , m_colorId(0)
    , m_dashId(0)
    , m_pattern()
    , m_miterLimit(0)
    , m_cap(MWAWGraphicStyle::C_Butt)
    , m_join(MWAWGraphicStyle::J_Miter)
  {
  }
  //! the line width
  float m_width;
  //! the color id
  int m_colorId;
  //! the dash id
  int m_dashId;
  //! the pattern
  MWAWGraphicStyle::Pattern m_pattern;
  //! the miter limit
  float m_miterLimit;
  //! the line cap
  MWAWGraphicStyle::LineCap m_cap;
  //! the line join
  MWAWGraphicStyle::LineJoin m_join;
};
/** small structure of FreeHandParserInternal used to stored
    a style header
*/
struct StyleHeader {
  //! constructor
  StyleHeader()
    : m_size(0)
    , m_type(0)
    , m_labelId(0)
    , m_screen()
    , m_unknownValue(0)
    , m_extra("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, StyleHeader const &style)
  {
    if (style.m_labelId) o << "label=Z" << style.m_labelId << ",";
    if (style.m_screen.isSet()) o << "screen=[" << *style.m_screen << "],";
    if (style.m_unknownValue)
      o << "unkn0=" << style.m_unknownValue << ",";
    o << style.m_extra;
    return o;
  }
  //! a field related to the zone size
  long m_size;
  //! the zone type
  int m_type;
  //! the label id
  int m_labelId;
  //! the screen mode
  MWAWVariable<ScreenMode> m_screen;
  //! the first unknown value
  int m_unknownValue;
  //! extra data
  std::string m_extra;
};

/** small structure of FreeHandParserInternal used to stored a shape
*/
struct Shape {
  //! the different type
  enum Type { Line, Rectangle, Ellipse, Path, BackgroundPicture, Picture, Group, JoinGroup, Unknown };

  //! constructor
  Shape()
    : m_id(0)
    , m_type(Unknown)
    , m_layerId(-1)
    , m_lineId(0)
    , m_fillId(0)
    , m_transformation()
    , m_box()
    , m_corner()
    , m_vertices()
    , m_closed(false)
    , m_evenOdd(false)
    , m_joinDistance(0)
    , m_childs()
    , m_picture()
    , m_dataId(0)
    , m_isSent(false)
  {
  }

  //! try to returns a shape and position
  bool updateShape(MWAWGraphicShape &shape) const
  {
    if (m_type==Line || m_type==Rectangle || m_type==Ellipse) {
      MWAWBox2f box=m_box;
      if (m_type==Line)
        shape=MWAWGraphicShape::line(box[0],box[1]);
      else if (m_type==Rectangle)
        shape=MWAWGraphicShape::rectangle(box, m_corner);
      else
        shape=MWAWGraphicShape::circle(box);
      return true;
    }
    if (m_type!=Path || m_vertices.empty()) return false;
    if (m_vertices.size()<6) {
      // probably an aborted spline, transform in a point
      MWAWVec2f pt=m_vertices[0];
      shape=MWAWGraphicShape::line(pt,pt);
      return true;
    }
    shape.m_type=MWAWGraphicShape::Polygon;
    MWAWBox2f box;
    bool needSpline=false;
    for (size_t i=0; i+2<m_vertices.size(); i+=3) {
      MWAWVec2f pt=m_vertices[i];
      if (i==0)
        box=MWAWBox2f(pt,pt);
      else
        box=box.getUnion(MWAWBox2f(pt,pt));
      if (!needSpline && (m_vertices[i]!=m_vertices[i+1] || m_vertices[i]!=m_vertices[i+2]))
        needSpline=true;
      shape.m_vertices.push_back(pt);
    }
    shape.m_bdBox=box;
    if (m_closed)
      shape.m_vertices.push_back(shape.m_vertices.front());
    if (!needSpline)
      return true;

    MWAWVec2f prevPoint, pt1;
    bool hasPrevPoint = false;
    shape.m_type=MWAWGraphicShape::Path;
    shape.m_vertices.clear();
    for (size_t i=0; i+2<m_vertices.size()+3; i+=3) {
      bool end=i+2>=m_vertices.size();
      if (end) {
        if (!m_closed)
          break;
        if (end && !hasPrevPoint && m_vertices[0]==m_vertices[1]) {
          shape.m_path.push_back(MWAWGraphicShape::PathData('Z'));
          break;
        }
        i=0;
      }
      MWAWVec2f pt=m_vertices[i];
      pt1 = m_vertices[i+1];
      char type = hasPrevPoint ? 'C' : i==0 ? 'M' : (m_vertices[i]!=m_vertices[i+1]) ? 'S' : 'L';
      shape.m_path.push_back(MWAWGraphicShape::PathData(type, pt, hasPrevPoint ? prevPoint : pt1, pt1));
      hasPrevPoint = m_vertices[i]!=m_vertices[i+2];
      if (hasPrevPoint)
        prevPoint=m_vertices[i+2];
      if (end)
        break;
    }
    return true;
  }
  //! the zone id
  int m_id;
  //! the type
  Type m_type;
  //! the layer
  int m_layerId;
  //! the line id
  int m_lineId;
  //! the fill id
  int m_fillId;

  //! the transformation
  MWAWTransformation m_transformation;
  //! the main box (for line, rectangle, ellipse)
  MWAWBox2f m_box;
  //! the corner size
  MWAWVec2f m_corner;
  //! the list of point for path: 3 Vec2f defining each point
  std::vector<MWAWVec2f> m_vertices;
  //! a flag to know if a path is closed
  bool m_closed;
  //! a flag to know how path intersection are defined
  bool m_evenOdd;
  //! the join distance
  float m_joinDistance;
  //! the list of child (for group and join group )
  std::vector<int> m_childs;
  //! the picture entry
  MWAWEntry m_picture;
  //! the id of a the picture date
  int m_dataId;
  //! flag to known if a shape is sent
  mutable bool m_isSent;
};

/** structure of FreeHandParserInternal used to stored a font */
struct Font {
  //! constructor
  Font()
    : m_font()
    , m_nameId(0)
    , m_colorId(0)
  {
  }
  //! the font
  MWAWFont m_font;
  //! the font name id
  int m_nameId;
  //! the font color id
  int m_colorId;
};

/** structure of FreeHandParserInternal used to stored a textbox */
struct Textbox {
  //! constructor
  explicit Textbox(int id)
    : m_id(id)
    , m_layerId(-1)
    , m_box()
    , m_transformation()
    , m_spacings(0,0)
    , m_scalings(1,1)
    , m_baseline(0)
    , m_justify(MWAWParagraph::JustificationLeft)
    , m_text()
    , m_posToFontMap()
    , m_isSent(false)
  {
  }
  //! the textbox id
  int m_id;
  //! the layer id
  int m_layerId;
  //! the main box
  MWAWBox2f m_box;
  //! the transformation
  MWAWTransformation m_transformation;
  //! the letter/word spacing
  MWAWVec2f m_spacings;
  //! the horizontal/vertical scalings
  MWAWVec2f m_scalings;
  //! the baseline
  float m_baseline;
  //! the paragraph justification
  MWAWParagraph::Justification m_justify;
  //! the text data
  MWAWEntry m_text;
  //! map char pos to font
  std::map<int,Font> m_posToFontMap;
  //! flag to known if a shape is sent
  mutable bool m_isSent;
};

////////////////////////////////////////
//! Internal: the state of a FreeHandParser
struct State {
  //! constructor
  State()
    : m_mainGroupId(0)
    , m_transform()
    , m_zIdToTypeMap()
    , m_zIdToColorMap()
    , m_zIdToDashMap()
    , m_zIdToDataMap()
    , m_zIdToFillStyleMap()
    , m_zIdToLineStyleMap()
    , m_zIdToStringMap()
    , m_zIdToPostscriptMap()
    , m_zIdToShapeMap()
    , m_zIdToTextboxMap()
    , m_actualLayer(-1)
    , m_sendIdSet()
    , m_sendLayerSet()
  {
  }
  //! try to return a zone type
  ZoneType getZoneType(int id) const
  {
    if (m_zIdToTypeMap.find(id)==m_zIdToTypeMap.end())
      return Z_Unknown;
    return m_zIdToTypeMap.find(id)->second;
  }
  //! try to add a id
  bool addZoneId(int id, ZoneType zoneType)
  {
    if (m_zIdToTypeMap.find(id)!=m_zIdToTypeMap.end())
      return m_zIdToTypeMap.find(id)->second==zoneType;
    m_zIdToTypeMap[id]=zoneType;
    return true;
  }
  //! try to update the fill style
  bool updateFillStyle(int zId, MWAWGraphicStyle &style) const;
  //! try to update the line style
  bool updateLineStyle(int zId, MWAWGraphicStyle &style) const;
  //! try to update the group layer id, return 0 or the new layer id
  int updateGroupLayerId(int zId, std::set<int> &seen);
  //! the main group id
  int m_mainGroupId;
  //! the main transformation
  MWAWTransformation m_transform;
  //! the list of id seen
  std::map<int, ZoneType> m_zIdToTypeMap;
  //! the list zoneId to color
  std::map<int, MWAWColor> m_zIdToColorMap;
  //! the list zoneId to dash
  std::map<int, std::vector<float> > m_zIdToDashMap;
  //! the list zoneId to data map
  std::map<int, MWAWEntry> m_zIdToDataMap;
  //! the list zoneId to fillStyle
  std::map<int, FillStyle> m_zIdToFillStyleMap;
  //! the list zoneId to lineStyle
  std::map<int, LineStyle> m_zIdToLineStyleMap;
  //! the list zoneId to string
  std::map<int, std::string> m_zIdToStringMap;
  //! the list zoneId to postscrip code
  std::map<int, std::string> m_zIdToPostscriptMap;
  //! the list zoneId to shape
  std::map<int, Shape> m_zIdToShapeMap;
  //! the list zoneId to textbox
  std::map<int, Textbox> m_zIdToTextboxMap;
  //! the actual layer
  int m_actualLayer;
  //! a set of send id used to avoid potential loop
  std::set<int> m_sendIdSet;
  //! a set of create layer to avoid dupplicating layer
  std::set<int> m_sendLayerSet;
};

bool State::updateFillStyle(int zId, MWAWGraphicStyle &style) const
{
  if (!zId) return true;
  // can be a simple color
  if (m_zIdToColorMap.find(zId)!=m_zIdToColorMap.end()) {
    style.setSurfaceColor(m_zIdToColorMap.find(zId)->second);
    return true;
  }
  if (m_zIdToFillStyleMap.find(zId)==m_zIdToFillStyleMap.end()) {
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("FreeHandParserInternal::State::updateFillStyle: can not find style %d\n", zId));
    }
    return false;
  }
  FillStyle const &fill=m_zIdToFillStyleMap.find(zId)->second;
  int numColors=fill.m_type==MWAWGraphicStyle::Gradient::G_None ? 1 : 2;
  MWAWColor colors[2];
  for (int i=0; i<numColors; ++i) {
    if (fill.m_colorId[i]==0) {
      colors[i]=MWAWColor::white();
      continue;
    }
    if (m_zIdToColorMap.find(fill.m_colorId[i])==m_zIdToColorMap.end()) {
      MWAW_DEBUG_MSG(("FreeHandParserInternal::State::updateFillStyle: can not find some color %d\n", fill.m_colorId[i]));
      return false;
    }
    colors[i]=m_zIdToColorMap.find(fill.m_colorId[i])->second;
  }
  if (!fill.m_pattern.empty()) {
    auto pat=fill.m_pattern;
    pat.m_colors[0]=MWAWColor::white();
    pat.m_colors[1]=colors[0];
    style.setPattern(pat);
    return true;
  }
  if (fill.m_type==MWAWGraphicStyle::Gradient::G_None) {
    style.setSurfaceColor(colors[0]);
    return true;
  }
  auto &finalGrad=style.m_gradient;
  finalGrad.m_type = fill.m_type;
  finalGrad.m_angle = 270.f-fill.m_angle;
  finalGrad.m_stopList.resize(2);
  for (size_t i=0; i<2; ++i)
    finalGrad.m_stopList[i]=MWAWGraphicStyle::Gradient::Stop(float(i), colors[i]);
  return true;
}

bool State::updateLineStyle(int zId, MWAWGraphicStyle &style) const
{
  if (!zId) {
    style.m_lineWidth=0;
    return true;
  }
  if (m_zIdToLineStyleMap.find(zId)==m_zIdToLineStyleMap.end()) {
    MWAW_DEBUG_MSG(("FreeHandParserInternal::State::updateLineStyle: can not find style %d\n", zId));
    style.m_lineWidth=1;
    return false;
  }
  LineStyle const &line=m_zIdToLineStyleMap.find(zId)->second;
  style.m_lineWidth=line.m_width;
  MWAWColor color=MWAWColor::white();
  if (line.m_colorId) {
    if (m_zIdToColorMap.find(line.m_colorId)==m_zIdToColorMap.end()) {
      static bool first=true;
      if (first) {
        first=false;
        MWAW_DEBUG_MSG(("FreeHandParserInternal::State::updateLineStyle: can not find some color %d\n", line.m_colorId));
      }
    }
    else
      color=m_zIdToColorMap.find(line.m_colorId)->second;
  }
  if (!line.m_pattern.empty()) {
    auto pat=line.m_pattern;
    // CHECKME
    pat.m_colors[0]=MWAWColor::white();
    pat.m_colors[1]=color;
    pat.getAverageColor(style.m_lineColor);
  }
  else
    style.m_lineColor=color;
  if (line.m_dashId) {
    if (m_zIdToDashMap.find(line.m_dashId)==m_zIdToDashMap.end()) {
      MWAW_DEBUG_MSG(("FreeHandParserInternal::State::updateLineStyle: can not find dash %d\n", line.m_dashId));
    }
    else if (m_zIdToDashMap.find(line.m_dashId)->second.size()>1)
      style.m_lineDashWidth=m_zIdToDashMap.find(line.m_dashId)->second;
  }
  style.m_lineCap=line.m_cap;
  style.m_lineJoin=line.m_join;
  return true;
}

int State::updateGroupLayerId(int zId, std::set<int> &seen)
{
  if (m_zIdToTextboxMap.find(zId)!=m_zIdToTextboxMap.end())
    return m_zIdToTextboxMap.find(zId)->second.m_layerId;
  if (m_zIdToShapeMap.find(zId)==m_zIdToShapeMap.end())
    return -1;
  Shape &shape=m_zIdToShapeMap.find(zId)->second;
  if (seen.find(zId)!=seen.end() || (shape.m_type!=Shape::Group && shape.m_type!=Shape::JoinGroup))
    return shape.m_layerId;
  int layerId=-1;
  seen.insert(zId);
  bool first=true;
  for (auto &child : shape.m_childs) {
    int newLayerId=updateGroupLayerId(child, seen);
    if (newLayerId==-1 || (!first && layerId!=newLayerId))
      layerId=-1;
    else
      layerId=newLayerId;
    first=false;
  }
  shape.m_layerId=layerId;
  seen.erase(zId);
  return layerId;
}

////////////////////////////////////////
//! Internal: the subdocument of a FreeHandParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(FreeHandParser &pars, MWAWInputStreamPtr const &input, int zoneId)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_id(zoneId) {}

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    if (MWAWSubDocument::operator!=(doc)) return true;
    auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
    if (!sDoc) return true;
    if (m_id != sDoc->m_id) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the subdocument id
  int m_id;
private:
  SubDocument(SubDocument const &orig);
  SubDocument &operator=(SubDocument const &orig);
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("FreeHandParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<FreeHandParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("FreeHandParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  parser->sendText(m_id);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
FreeHandParser::FreeHandParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

FreeHandParser::~FreeHandParser()
{
}

void FreeHandParser::init()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new FreeHandParserInternal::State);

  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void FreeHandParser::parse(librevenge::RVNGDrawingInterface *docInterface)
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
      sendZone(m_state->m_mainGroupId, m_state->m_transform);
#ifdef DEBUG
      flushExtra();
#endif
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("FreeHandParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void FreeHandParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("FreeHandParser::createDocument: listener already exist\n"));
    return;
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
bool FreeHandParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  long pos;
  libmwaw::DebugStream f;
  bool readSome=false;
  int zId=1;
  int const vers=version();
  if (vers==2) {
    pos=input->tell();
    libmwaw::PrinterInfo info;
    if (!info.read(input)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      if (input->readULong(4)==0) { // null print info is ok
        ascii().addPos(pos);
        ascii().addNote("_");
        input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
      }
      else
        input->seek(pos, librevenge::RVNG_SEEK_SET);
    }
    else {
      f.str("");
      f << "Entries(PrintInfo):" << info;
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
  }
  while (!input->isEnd()) {
    pos=input->tell();
    while ((vers==1 && readZoneV1(zId)) || (vers==2 && readZoneV2(zId))) {
      readSome=true;
      pos=input->tell();
      if (zId) ++zId;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (!input->checkPosition(pos+5))
      break;
    // ok try to continue
    bool ok=true;
    zId=0;
    while (!input->isEnd()) {
      auto val=static_cast<unsigned long>(input->readULong(4));
      if (input->isEnd()) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        ok=false;
        break;
      }
      if (!val || (val&0xFF00)) {
        input->seek(-1, librevenge::RVNG_SEEK_CUR);
        continue;
      }
      if (val&0xFF0000) {
        input->seek(-2, librevenge::RVNG_SEEK_CUR);
        continue;
      }
      if (val&0xFF000000) {
        input->seek(-3, librevenge::RVNG_SEEK_CUR);
        continue;
      }
      input->seek(-4, librevenge::RVNG_SEEK_CUR);
      long actPos=input->tell();
      if ((vers==1 && readZoneV1(zId)) || (vers==2 && readZoneV2(zId))) {
        if (pos!=actPos) {
          MWAW_DEBUG_MSG(("FreeHandParser::createZones: find some unexpected data\n"));
          ascii().addPos(pos);
          ascii().addNote("Entries(Unknown):###");
        }
        break;
      }
      input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
      continue;
    }
    if (!ok) break;
  }
  pos=input->tell();
  f.str("");
  f << "Entries(End):";
  if (input->readLong(4)!=-1) {
    MWAW_DEBUG_MSG(("FreeHandParser::createZones: find unexpected end data\n"));
    f << "###";
  }
  if (readSome && m_state->m_mainGroupId) {
    std::set<int> seen;
    m_state->updateGroupLayerId(m_state->m_mainGroupId, seen);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return readSome;
}

bool FreeHandParser::readZoneV1(int zId)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+5))
    return false;
  auto val=static_cast<int>(input->readULong(4));
  if (val < 0 || (static_cast<unsigned long>(val)&0xFF000000)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  auto type=static_cast<int>(input->readULong(2));
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  switch (type) {
  // does type=1 exist ?
  case 2:
    return readStyleGroup(zId);
  case 3:
    return readStringZone(zId);

  // 4001-4002
  case 0xfa1:
    return readRootGroup(zId);
  case 0xfa2:
    return readGroupV1(zId);

  // 4101-4104
  case 0x1005:
    return readTransformGroup(zId);
  case 0x1006:
    return readTextboxV1(zId);
  case 0x1007:
    return readBackgroundPicture(zId);
  case 0x1008:
    return readJoinGroup(zId);

  // 4202-4204
  // does type=0x1069 exist ?
  case 0x106a:
  case 0x106b:
  case 0x106c:
    return readColor(zId);

  // 4301-4305
  case 0x10cd:
    return readFillStyle(zId);
  case 0x10ce:
    return readLineStyle(zId);
  case 0x10cf:
    return readPostscriptStyle(zId);
  case 0x10d0:
  case 0x10d1:
    return readFillStyle(zId);

  // 4401-4405
  case 0x1131: // rectangle
  case 0x1132: // ellipse
  // does type=0x1133 exist ?
  case 0x1134: // spline
  case 0x1135: // line
    return readShape(zId);

  // 4501
  case 0x1195:
    return readDash(zId);
  default:
    break;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return false;
}

bool FreeHandParser::readZoneV2(int zId)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+5))
    return false;
  auto val=static_cast<int>(input->readULong(4));
  if (val < 0 || (static_cast<unsigned long>(val)&0xFF000000)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  auto type=static_cast<int>(input->readULong(2));
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  switch (type) {
  case 5: // style group
    return readStyleGroup(zId);
  case 6:
    return readStringZone(zId);
  case 0x1389:
    return readRootGroup(zId);
  case 0x138a:
    return readGroupV2(zId);
  case 0x138b:
    return readDataZone(zId);
  case 0x13ed:
    return readTransformGroup(zId);
  case 0x13ee:
    return readTextboxV2(zId);
  case 0x13f0:
    return readJoinGroup(zId);
  case 0x13f8:
    return readPictureZone(zId);
  case 0x1452: // basic
  case 0x1453: // tint
  case 0x1454: // cmyk
  case 0x1455: // pantome ?
    return readColor(zId);
  case 0x14b5: // basic
    return readFillStyle(zId);
  case 0x14b6: // line style
    return readLineStyle(zId);
  case 0x14b7: // gradient linear
  case 0x14b8: // radial
    return readFillStyle(zId);
  case 0x14c9: // line, always follow 14d3
  case 0x14ca: // surf, always follow 14d4
    return readPostscriptStyle(zId);
  case 0x14d3: // pattern
    return readFillStyle(zId);
  case 0x14d4: // pattern line style
    return readLineStyle(zId);
  case 0x14dd: // tile style
    return readFillStyle(zId);
  case 0x1519: // rectangle
  case 0x151a: // ellipse
    return readShape(zId);
  case 0x151c:
    return readShape(zId);
  case 0x151d: // line
    return readShape(zId);
  case 0x157d:
    return readDash(zId);
  default:
    break;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return false;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool FreeHandParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = FreeHandParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(128))
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  auto signature=long(input->readULong(4));
  int vers=1, val;
  if (signature==0x61636633) {
    val=static_cast<int>(input->readULong(2)); // the subversion?
    if (strict && val>=9) return false;
    if (val!=5)
      f << "f0=" << val << ",";
  }
  else if (signature==0x46484432) {
    if (!input->checkPosition(256))
      return false;
    vers=2;
    val=static_cast<int>(input->readULong(2)); // the subversion?
    if (strict && val>20) return false;
    if (val!=9)
      f << "f0=" << val << ",";
  }
  else
    return false;
  val=static_cast<int>(input->readULong(2));
  if (val!=100) f << "f1=" << val << ",";
  float dim[8];
  for (auto &d : dim) d=float(input->readLong(2))/10.f;
  f << "page[sz]=" << MWAWVec2f(dim[0],dim[1]) << ",";
  f << "paper[sz]=" << MWAWVec2f(dim[2],dim[3]) << ",";
  if (dim[4]>0 || dim[5]>0)
    f << "unkn[sz]=" << MWAWVec2f(dim[4],dim[5]) << ",";
  f << "margins=" << MWAWVec2f(dim[6],dim[7]) << ",";
  if (vers>1) {
    ascii().addDelimiter(input->tell(),'|');
    input->seek(30, librevenge::RVNG_SEEK_CUR);
    for (int i=0; i<3; ++i) {
      val=static_cast<int>(input->readULong(2));
      if (!val) continue;
      // checkme: odd
      if (i==0 && (val&0x20) && dim[0]>dim[1]) {
        f << "landscape,";
        getPageSpan().setFormOrientation(MWAWPageSpan::LANDSCAPE);
        for (int j=0; j<4; ++j) {
          if (j==1) continue;
          std::swap(dim[2*j],dim[2*j+1]);
        }
        val &= 0xFFDF;
      }
      if (val)
        f << i+2 << "=" << std::hex << val << std::dec << ",";
    }
  }
  else {
    for (int i=0; i<2; ++i) { // f2=1|2|a
      val=static_cast<int>(input->readULong(2));
      if (!val) continue;
      if (i==1) {
        if (val&1) {
          f << "landscape,";
          getPageSpan().setFormOrientation(MWAWPageSpan::LANDSCAPE);
          for (int j=0; j<4; ++j)
            std::swap(dim[2*j],dim[2*j+1]);
        }
        if (val&2) f << "crop[mark],";
        if (val&4) f << "center[mark],";
        if (val&8) f << "separation[name],";
        if (val&0x10) f << "file[name&date],";
        if (val&0x40) f << "include[processColor],";
        if (val&0x80) f << "display[quality]=better,";
        if (val&0x100) f << "print[quality]=better,";
        val &= 0xFE20;
      }
      if (val)
        f << "f" << i+2 << "=" << std::hex << val << std::dec << ",";
    }
  }
  if (dim[2]>0 && dim[3]>0) {
    getPageSpan().setFormLength(double(dim[2])/72.);
    getPageSpan().setFormWidth(double(dim[3])/72.);
    if (dim[0]+dim[6]<=dim[2]) {
      getPageSpan().setMarginBottom(double(dim[6])/72.0);
      getPageSpan().setMarginTop(double(dim[2]-dim[0]-dim[6])/72.0);
    }
    else {
      MWAW_DEBUG_MSG(("FreeHandParser::checkHeader: the vertical margins seems bad\n"));
      if (dim[0]<=dim[2]) {
        getPageSpan().setMarginBottom(double(dim[2]-dim[0])/2.0/72.0);
        getPageSpan().setMarginTop(double(dim[2]-dim[0])/2.0/72.0);
      }
    }
    if (dim[1]+dim[7]<=dim[3]) {
      getPageSpan().setMarginRight(double(dim[7])/72.0);
      getPageSpan().setMarginLeft(double(dim[3]-dim[1]-dim[7])/72.0);
    }
    else {
      MWAW_DEBUG_MSG(("FreeHandParser::checkHeader: the horizontal margins seems bad\n"));
      if (dim[1]<=dim[3]) {
        getPageSpan().setMarginLeft(double(dim[3]-dim[1])/2.0/72.0);
        getPageSpan().setMarginRight(double(dim[3]-dim[1])/2.0/72.0);
      }
    }
  }
  else {
    if (strict)
      return false;
    MWAW_DEBUG_MSG(("FreeHandParser::checkHeader: the paper size seems bad\n"));
  }
  // transform orig from page content LeftBot -> origin form page LeftTop
  m_state->m_transform=
    MWAWTransformation::translation(72.f*MWAWVec2f(float(getPageSpan().getMarginLeft()),float(getPageSpan().getPageLength()+getPageSpan().getMarginTop()))) *
    MWAWTransformation::scale(MWAWVec2f(1,-1));
  if (vers==1) {
    val=static_cast<int>(input->readULong(4));
    switch ((val>>29)) {
    case 0: // point
      break;
    case 1:
      f << "unit=picas,";
      break;
    case 2:
      f << "unit=inches,";
      break;
    case 3:
      f << "unit=decimal[inches],";
      break;
    case 4:
      f << "unit=millimeters,";
      break;
    default:
      MWAW_DEBUG_MSG(("FreeHandParser::checkHeader: find unknown unit\n"));
      f << "##units=" << ((val>>29)&7) << ",";
    }
    val &= 0x1FFF;
    if (val) f << "grid[size]=" << float(val)/65536.f/10.f << ",";
    for (int i=0; i<4; ++i) { // f4=0|200
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i+4 << "=" << std::hex << val << std::dec << ",";
    }
  }

  for (int i=0; i<2; ++i) {
    // checkme no sure what are limit
    long actPos=input->tell();
    auto sSz=static_cast<int>(input->readULong(1));
    if (sSz>31) {
      if (strict) return false;
      MWAW_DEBUG_MSG(("FreeHandParser::checkHeader: string size %d seems bad\n", i));
      f << "##sSz,";
      sSz=0;
    }
    std::string name;
    for (int s=0; s<sSz; ++s) name+=char(input->readULong(1));
    if (!name.empty()) {
      char const *wh[]= {"printer", "paper"};
      f << wh[i] << "=" << name << ",";
    }
    input->seek(actPos+32, librevenge::RVNG_SEEK_SET);
  }
  if (vers==1) {
    for (int i=0; i<5; ++i) { // g0=0|41, g1=0|3-7, g3=0|20, g4=0|b4
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "g" << i << "=" << std::hex << val << std::dec << ",";
    }
    // big number
    f << "unkn=" << std::hex << input->readULong(4) << std::dec << ",";
    for (int i=0; i<5; ++i) { // always 0
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "h" << i << "=" << val << ",";
    }
  }
  else {
    ascii().addDelimiter(input->tell(),'|');
    f << "unkn=" << std::hex << input->readULong(4) << std::dec << ",";
    // always 0
    for (int i=0; i<64; ++i) {
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "h" << i << "=" << val << ",";
    }
    // check for printer info or null printer info
    if (strict) {
      libmwaw::PrinterInfo info;
      if (!info.read(input)) {
        input->seek(256, librevenge::RVNG_SEEK_SET);
        if (input->readULong(4))
          return false;
      }
    }
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_FREEHAND, vers, MWAWDocument::MWAW_K_DRAW);
  input->seek(vers==1 ? 128 : 256, librevenge::RVNG_SEEK_SET);

  return true;
}

////////////////////////////////////////////////////////////
// try to read the zone
////////////////////////////////////////////////////////////
bool FreeHandParser::readRootGroup(int zId)
{
  if ((zId && zId!=1) || !m_state->m_zIdToTypeMap.empty())
    return false;
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  int const vers=version();
  if (!input->checkPosition(pos+(vers==1 ? 24 : 34)))
    return false;
  f.str("");
  f << "Entries(Root):";
  auto dSz=static_cast<int>(input->readULong(4)); // probably related to size
  auto opCode=static_cast<int>(input->readULong(2));
  if ((vers==1 && opCode !=0xfa1) || (vers>1 && opCode != 0x1389))
    return false;
  if (vers>1) dSz-=4;
  if (dSz!=0x34) {
    MWAW_DEBUG_MSG(("FreeHandParser::readRootGroup: find unexpected zone size\n"));
    f << "#sz?=" << dSz << ",";
  }
  if (vers==1) {
    for (int i=0; i<2; ++i) { // always 0 ?
      auto val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      MWAW_DEBUG_MSG(("FreeHandParser::readRootGroup: find unknown zone %d\n", i));
      f << "#f" << i << "=" << val << ",";
    }
  }
  auto id=static_cast<int>(input->readLong(2));
  if (id) {
    m_state->m_mainGroupId=id;
    m_state->addZoneId(id, FreeHandParserInternal::Z_Group);
    f << "main=Z" << id << ",";
  }
  if (vers==1) {
    for (int i=0; i<6; ++i) {
      id=static_cast<int>(input->readLong(2));
      if (!id) continue;
      // the first group is a style group, but I never find any child, so...
      FreeHandParserInternal::ZoneType type[6]= {
        FreeHandParserInternal::Z_StyleGroup, FreeHandParserInternal::Z_FillGroup, FreeHandParserInternal::Z_LineStyleGroup,
        FreeHandParserInternal::Z_ColorGroup, FreeHandParserInternal::Z_DashGroup, FreeHandParserInternal::Z_ColorGroup
      };
      if (!m_state->addZoneId(id, type[i])) {
        MWAW_DEBUG_MSG(("FreeHandParser::readRootGroup: find dupplicated id\n"));
        f << "###";
      }
      char const *wh[6]= { "groupStyle0", "fillStyle", "lineStyle", "colStyle", "dashStyle", "colStyle2" };
      f << wh[i] << "=Z" << id << ",";
    }
  }
  else {
    // at least 8, maybe more
    for (int i=0; i<8; ++i) {
      id=static_cast<int>(input->readLong(2));
      if (!id) continue;
      FreeHandParserInternal::ZoneType type[8]= {
        FreeHandParserInternal::Z_ColorGroup, FreeHandParserInternal::Z_FillGroup, FreeHandParserInternal::Z_LineStyleGroup,
        FreeHandParserInternal::Z_StyleGroup, FreeHandParserInternal::Z_FillGroup, FreeHandParserInternal::Z_LineStyleGroup,
        FreeHandParserInternal::Z_DashGroup, FreeHandParserInternal::Z_ColorGroup,
      };
      if (!m_state->addZoneId(id, type[i])) {
        MWAW_DEBUG_MSG(("FreeHandParser::readRootGroup: find dupplicated id\n"));
        f << "###";
      }
      char const *wh[8]= { "colStyle", "fillStyle", "lineStyle", "groupStyle3", "fillStyle[unamed]", "lineStyle[unamed]", "dashStyle", "colStyle2"};
      f << wh[i] << "=Z" << id << ",";
    }
    for (int i=0; i<5; ++i) {
      auto val=static_cast<int>(input->readULong(2));
      if (!val) continue;
      MWAW_DEBUG_MSG(("FreeHandParser::readRootGroup: find unknown group id\n"));
      f << "###Z" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool FreeHandParser::readGroupV1(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+20))
    return false;
  f.str("");
  if (zId)
    f << "Entries(Group)[Z" << zId << "]:";
  else
    f << "Entries(Group):";
  if (zId) {
    auto type=m_state->getZoneType(zId);
    if (type!=FreeHandParserInternal::Z_Group && type!=FreeHandParserInternal::Z_Shape) {
      MWAW_DEBUG_MSG(("FreeHandParser::readGroupV1: find unexpected zone type for zone %d\n", zId));
    }
  }

  auto dSz=static_cast<int>(input->readULong(4)); // probably related to size
  f << "sz=" << dSz << ",";
  if (input->readULong(2)!=0xfa2) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  FreeHandParserInternal::Shape res;
  res.m_id=zId;
  res.m_type=FreeHandParserInternal::Shape::Group;
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+18, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  auto N=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(pos+20+2*N)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "childs=[";
  for (int i=0; i<N; ++i) {
    auto id=static_cast<int>(input->readULong(2));
    if (!m_state->addZoneId(id, FreeHandParserInternal::Z_Shape)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    res.m_childs.push_back(id);
    f << "Z" << id << ",";
  }
  f <<"],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readGroupV2(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+20))
    return false;
  f.str("");
  if (zId)
    f << "Entries(Group)[Z" << zId << "]:";
  else
    f << "Entries(Group):";
  if (zId) {
    auto type=m_state->getZoneType(zId);
    if (type!=FreeHandParserInternal::Z_Group && type!=FreeHandParserInternal::Z_Shape) {
      MWAW_DEBUG_MSG(("FreeHandParser::readGroupV2: find unexpected zone type for zone %d\n", zId));
    }
  }

  auto dSz=static_cast<int>(input->readULong(4)); // probably related to size
  f << "sz=" << dSz << ",";
  if (input->readULong(2)!=0x138a) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  FreeHandParserInternal::Shape res;
  res.m_id=zId;
  res.m_type=FreeHandParserInternal::Shape::Group;
  for (int i=0; i<2; ++i) { // always 0
    auto val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto sSz=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(input->tell()+sSz+8)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().addDelimiter(input->tell(),'|');
  /* find
     00000000000000000000000000000000
     000f000a000000000000000000000000a89d800180017fff7fff7fff7fff7fff7fff7fff7fff7fff7fff4ead7fff80017fff52807fff7fff7fff60144ead8fff670a
   */
  input->seek(sSz, librevenge::RVNG_SEEK_CUR);
  ascii().addDelimiter(input->tell(),'|');
  float dim[2];
  for (auto &d : dim) d=float(input->readLong(2))/10.f;
  if (MWAWVec2f(dim[0],dim[1])!=MWAWVec2f(0,0))
    f << "dim?=" << MWAWVec2f(dim[0],dim[1]) << ",";
  auto val=static_cast<int>(input->readLong(2));
  if (val) f << "f2=" << val << ",";

  auto N=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(input->tell()+2*N)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "childs=[";
  for (int i=0; i<N; ++i) {
    auto id=static_cast<int>(input->readULong(2));
    if (!m_state->addZoneId(id, FreeHandParserInternal::Z_Shape)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    res.m_childs.push_back(id);
    f << "Z" << id << ",";
  }
  f <<"],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readJoinGroup(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  int const vers=version();
  if (!readShapeHeader(shape)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if ((vers==1 && shape.m_type!=0x1008) || (vers>1 && shape.m_type!=0x13f0)
      || !input->checkPosition(input->tell()+8)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  if (zId)
    f << "Entries(JoinGrp)[Z" << zId << "]:" << shape;
  else
    f << "Entries(JoinGrp):" << shape;
  if (zId) {
    auto type=m_state->getZoneType(zId);
    if (type!=FreeHandParserInternal::Z_Shape) {
      MWAW_DEBUG_MSG(("FreeHandParser::readJoinGroup: find unexpected zone type for zone %d\n", zId));
    }
  }
  FreeHandParserInternal::Shape res;
  res.m_id=zId;
  res.m_layerId=shape.m_layerId;
  res.m_type=FreeHandParserInternal::Shape::JoinGroup;
  if (shape.m_size!=0x24) f << "sz?=" << shape.m_size << ",";
  res.m_joinDistance=float(input->readLong(4))/65536.f;
  if (res.m_joinDistance<0 || res.m_joinDistance>0)
    f << "dist=" << res.m_joinDistance << ",";
  f << "childs=[";
  for (int i=0; i<2; ++i) {
    auto id=static_cast<int>(input->readULong(2));
    if (!m_state->addZoneId(id, FreeHandParserInternal::Z_Shape)) {
      MWAW_DEBUG_MSG(("FreeHandParser::readJoinGroup: find unexpected child id\n"));
      f << "###";
    }
    res.m_childs.push_back(id);
    f << "Z" << id << ",";
  }
  f <<"],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  static bool first=true;
  if (first) {
    MWAW_DEBUG_MSG(("FreeHandParser::readJoinGroup: Ooops, sending text on path is unimplemented\n"));
    first=false;
  }
  return true;
}

bool FreeHandParser::readTransformGroup(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  int const vers=version();
  if ((vers==1 && shape.m_type!=0x1005) || (vers>1 && shape.m_type!=0x13ed) || !input->checkPosition(input->tell()+30)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  if (zId)
    f << "Entries(TransformGrp)[Z" << zId << "]:" << shape;
  else
    f << "Entries(TransformGrp):" << shape;
  if (shape.m_size!=0x38) f << "sz?=" << shape.m_size << ",";
  if (zId) {
    auto zType=m_state->getZoneType(zId);
    if (zType!=FreeHandParserInternal::Z_Group && zType!=FreeHandParserInternal::Z_Shape) {
      MWAW_DEBUG_MSG(("FreeHandParser::readTransformGroup: find unexpected zone type for zone %d\n", zId));
    }
  }
  FreeHandParserInternal::Shape res;
  res.m_id=zId;
  res.m_layerId=shape.m_layerId;
  res.m_type=FreeHandParserInternal::Shape::Group;
  auto id=static_cast<int>(input->readULong(2));
  if (!m_state->addZoneId(id, FreeHandParserInternal::Z_Group)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTransformGroup: find unexpected child id\n"));
    f << "###";
  }
  f << "child=Z" << id << ",";
  res.m_childs.push_back(id);
  auto val=static_cast<int>(input->readULong(2)); // always 0
  if (val) f << "f0=" << val << ",";
  f << "flags=" << std::hex << input->readULong(2) << std::dec << ",";
  f << "rot=[";
  float dim[6];
  for (int i=0; i<4; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  f << "trans=[";
  for (int i=0; i<2; ++i) {
    dim[i+4]=float(input->readLong(4))/65536.f/10.f;
    f << dim[i+4] << ",";
  }
  f << "],";
  res.m_transformation=MWAWTransformation(MWAWVec3f(dim[0],dim[2],dim[4]),MWAWVec3f(dim[1],dim[3],dim[5]));

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readStyleGroup(int zId)
{
  int const vers=version();
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+(vers>1 ? 12 : 16)))
    return false;
  f.str("");
  if (zId)
    f << "Entries(StyleGrp)[Z" << zId << "]:";
  else
    f << "Entries(StyleGrp):";
  auto cType=FreeHandParserInternal::Z_Unknown;
  bool checkDSize=true;
  if (zId) {
    auto zType=m_state->getZoneType(zId);
    checkDSize=false;
    if (zType==FreeHandParserInternal::Z_ColorGroup)
      cType=FreeHandParserInternal::Z_Color;
    else if (zType==FreeHandParserInternal::Z_DashGroup)
      cType=FreeHandParserInternal::Z_Dash;
    else if (zType==FreeHandParserInternal::Z_FillGroup)
      cType=FreeHandParserInternal::Z_Fill;
    else if (zType==FreeHandParserInternal::Z_LineStyleGroup)
      cType=FreeHandParserInternal::Z_LineStyle;
    else if (zType!=FreeHandParserInternal::Z_StyleGroup) {
      checkDSize=true;
      MWAW_DEBUG_MSG(("FreeHandParser::readStyleGroup: find unexpected zone type for zone %d\n", zId));
    }
  }
  auto dSz=static_cast<int>(input->readULong(4)); // probably related to size
  f << "sz?=" << dSz << ",";
  auto opCode=static_cast<int>(input->readULong(2));
  if ((vers==1 && opCode!=2) || (vers>1 && opCode!=5))
    return false;
  if (vers==1) {
    for (int i=0; i<2; ++i) { // always f0=0,f1=16 ?
      auto val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
  }
  auto N=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(input->tell()+4+2*N) ||
      (vers==1 && checkDSize && N!=(dSz-16)/2) ||
      (vers>1 && checkDSize && N!=(dSz-12)/2))
    return false;
  for (int i=0; i<2; ++i) { // always 0?
    auto val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i+2 << "=" << val << ",";
  }
  f << "childs=[";
  for (int i=0; i<N; ++i) {
    auto id=static_cast<int>(input->readULong(2));
    if (!m_state->addZoneId(id, cType)) {
      if (checkDSize) return false;
      MWAW_DEBUG_MSG(("FreeHandParser::readStyleGroup: find unexpected child zone %d\n", id));
      f << "###";
    }
    f << "Z" << id << ",";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool FreeHandParser::readStringZone(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  auto dSz=long(input->readULong(4));
  auto opCode=static_cast<int>(input->readLong(2));
  int const vers=version();
  // v1: opcode=3, v2: opcode=6
  if (vers==2) {
    dSz-=4;
    opCode-=3;
  }
  if (opCode!=3 || dSz<3 || !input->checkPosition(pos+dSz+2)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  if (zId)
    f << "Entries(String)[Z" << zId << "]:";
  else
    f << "Entries(String):";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_String) {
    MWAW_DEBUG_MSG(("FreeHandParser::readStringZone: find unexpected zone type for zone %d\n", zId));
  }
  auto sSz=int(input->readULong(1));
  if (sSz+5>dSz || (!zId && sSz+6<dSz)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  std::string name;
  for (int i=0; i<sSz; ++i) name += char(input->readULong(1));
  f << name << ",";
  if (zId && m_state->m_zIdToStringMap.find(zId)==m_state->m_zIdToStringMap.end())
    m_state->m_zIdToStringMap[zId]=name;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+dSz+2, librevenge::RVNG_SEEK_SET);
  return true;
}

bool FreeHandParser::readShapeHeader(FreeHandParserInternal::ShapeHeader &shape)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  long pos=input->tell();
  int const vers=version();
  if (!input->checkPosition(pos+(vers==1 ? 20 : 18))) return false;
  shape.m_size=long(input->readULong(4));
  shape.m_type=static_cast<int>(input->readULong(2));
  if (vers>1) {
    shape.m_dataId=static_cast<int>(input->readULong(2));
    if (shape.m_dataId && !m_state->addZoneId(shape.m_dataId, FreeHandParserInternal::Z_Note)) {
      MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: find unexpected data id\n"));
      f << "###dataId";
    }
    shape.m_values[0]=static_cast<int>(input->readLong(2)); // always 0
    shape.m_layerId=static_cast<int>(input->readULong(2));
    shape.m_values[1]=static_cast<int>(input->readLong(2)); // always 0
    // now to multiple of 256 ???
    f << "unkn=[" << float(input->readLong(2))/256.f << "," << float(input->readLong(2))/256.f << "],";
    shape.m_extra=f.str();
    return true;
  }
  // always 0, if not we may have a problem...
  shape.m_values[0]=static_cast<int>(input->readLong(2));
  auto dataSz=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(pos+14+dataSz)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (dataSz) {
    auto sSz=static_cast<int>(input->readULong(1));
    if (sSz==dataSz-1) {
      for (int i=0; i<sSz; ++i)
        shape.m_note+=char(input->readULong(1));
    }
    else {
      MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: find unexpected special size\n"));
      f << "##specialSize=" << dataSz << ",";
      input->seek(dataSz-1, librevenge::RVNG_SEEK_CUR);
    }
  }
  shape.m_layerId=static_cast<int>(input->readULong(2));
  /* val1,val2: always 0, if not we may have a problem...
     val3: sometimes a 1005 zone
   */
  for (int i=0; i<3; ++i)
    shape.m_values[i+1]=static_cast<int>(input->readLong(2));
  if (shape.m_values[3]) {
    if (!m_state->addZoneId(shape.m_values[3], FreeHandParserInternal::Z_Shape)) {
      MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: find unexpected shape id\n"));
      f << "###shapeId";
    }
  }
  // the special zone
  dataSz=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(input->tell()+dataSz)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (dataSz==8) {
    long actPos=input->tell();
    if (!readScreenMode(*shape.m_screen)) {
      MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: can not read screen mode\n"));
      f << "##screenMode,";
      input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
    }
  }
  else if (dataSz) {
    MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: find unexpected special size\n"));
    f << "##specialSize=" << dataSz << ",";
    input->seek(dataSz, librevenge::RVNG_SEEK_CUR);
  }
  shape.m_extra=f.str();
  return true;
}

bool FreeHandParser::readScreenMode(FreeHandParserInternal::ScreenMode &screen)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+8)) return false;
  screen.m_function=static_cast<int>(input->readLong(2));
  screen.m_angle=float(input->readLong(2))/10.f;
  screen.m_lineByInch=static_cast<int>(input->readULong(2));
  screen.m_value=static_cast<int>(input->readLong(2)); // always 0?
  return true;
}

bool FreeHandParser::readStyleHeader(FreeHandParserInternal::StyleHeader &style)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  long pos=input->tell();
  if (!input->checkPosition(pos+12)) return false;
  style.m_size=long(input->readULong(4));
  style.m_type=static_cast<int>(input->readULong(2));
  if (version()==1) {
    // always 0, if not we may have a problem...
    style.m_unknownValue=static_cast<int>(input->readLong(2));
    auto dataSz=static_cast<int>(input->readULong(2));
    if (!input->checkPosition(pos+12+dataSz)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (dataSz==8) {
      long actPos=input->tell();
      if (!readScreenMode(*style.m_screen)) {
        MWAW_DEBUG_MSG(("FreeHandParser::readStyleHeader: can not read screen mode\n"));
        f << "##screenMode,";
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
    }
    else if (dataSz) {
      MWAW_DEBUG_MSG(("FreeHandParser::readStyleHeader: find unexpected special size\n"));
      f << "##specialSize=" << dataSz << ",";
      input->seek(dataSz, librevenge::RVNG_SEEK_CUR);
    }
  }
  auto id=static_cast<int>(input->readULong(2));
  if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_String)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  else
    style.m_labelId=id;
  return true;
}

bool FreeHandParser::readColor(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  f.str("");
  if (zId)
    f << "Entries(Color)[Z" << zId << "]:";
  else
    f << "Entries(Color):";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Color) {
    MWAW_DEBUG_MSG(("FreeHandParser::readColor: find unexpected zone type for zone %d\n", zId));
  }
  FreeHandParserInternal::StyleHeader zone;
  if (!readStyleHeader(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  int expectedSize=0;
  int const vers=version();
  switch (zone.m_type) {
  case 0x106a:
  case 0x1452:
    f << "color,";
    expectedSize=12;
    if ((vers==1 && zone.m_size!=0x1c) || (vers>1 && zone.m_size!=0x1e))
      f << "#sz?=" << zone.m_size << ",";
    break;
  case 0x106b:
    f << "tint,";
    expectedSize=4;
    if (zone.m_size!=0x16)
      f << "#sz?=" << zone.m_size << ",";
    break;
  case 0x1453:
    f << "tint,";
    expectedSize=10;
    if (zone.m_size!=0x1e)
      f << "#sz?=" << zone.m_size << ",";
    break;
  case 0x106c:
    f << "cmyk,";
    expectedSize=8;
    if (zone.m_size!=0x18)
      f << "#sz?=" << zone.m_size << ",";
    break;
  case 0x1454:
    f << "cmyk,";
    expectedSize=14;
    if (zone.m_size!=0x20)
      f << "#sz?=" << zone.m_size << ",";
    break;
  case 0x1455:
    f << "pantome?,";
    expectedSize=22;
    if (zone.m_size!=0x28)
      f << "#sz?=" << zone.m_size << ",";
    break;
  default:
    break;
  }
  long endPos=input->tell()+expectedSize;
  if (expectedSize==0 || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << zone;
  MWAWColor color;
  if (zone.m_type==0x106a || zone.m_type==0x1452) {
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    color=MWAWColor(col[0],col[1],col[2]);
    f << color << ",";
    auto val=static_cast<int>(input->readLong(2));
    if (val) f << "id=" << val << ",";
    val=static_cast<int>(input->readLong(2)); // flag or big number
    if (val) f << "f0=" << val << ",";
    val=static_cast<int>(input->readLong(2)); // always 1
    if (val!=1) f << "f1=" << val << ",";
  }
  else if (zone.m_type==0x106b || zone.m_type==0x1453) {
    if (zone.m_type==0x1453) {
      unsigned char col[3];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
      color=MWAWColor(col[0],col[1],col[2]);
      f << color << ",";
    }
    auto cId=static_cast<int>(input->readULong(2));
    if (cId && !m_state->addZoneId(cId, FreeHandParserInternal::Z_Color)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    f << "main[color]=Z" << cId << ",";
    MWAWColor mainColor(MWAWColor::white());
    if (cId && m_state->m_zIdToColorMap.find(cId)!=m_state->m_zIdToColorMap.end())
      mainColor=m_state->m_zIdToColorMap.find(cId)->second;
    else if (cId) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("FreeHandParser::readColor: can not find some main color\n"));
        first=false;
      }
    }
    float tint=float(input->readULong(2))/65535.f;
    if (zone.m_type==0x106b)
      color=MWAWColor::barycenter(tint, mainColor, 1-tint, MWAWColor::white());
    f << "percent=" << tint << ",";
  }
  else if (zone.m_type==0x1455) {
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    color=MWAWColor(col[0],col[1],col[2]);
    f << color << ",";
    // what is that ?
    for (int i=0; i<8; ++i) { // f0=0|1a5|1f7,f2=0|451e,f4=0|f5c|1c28,f5=147a|2e14|828f,f6=1c2, f7=1
      auto val=static_cast<int>(input->readULong(2));
      if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
    }
  }
  else {
    if (zone.m_type==0x1454) {
      unsigned char col[3];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
      color=MWAWColor(col[0],col[1],col[2]);
      f << color << ",";
    }
    unsigned char col[4];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    if (zone.m_type==0x106c) {
      color=MWAWColor::colorFromCMYK(col[1],col[2],col[3],col[0]);
      f << color << ",";
    }
  }
  if (zId && m_state->m_zIdToColorMap.find(zId)==m_state->m_zIdToColorMap.end())
    m_state->m_zIdToColorMap[zId]=color;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool FreeHandParser::readDash(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  f.str("");
  if (zId)
    f << "Entries(Dash)[Z" << zId << "]:";
  else
    f << "Entries(Dash):";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Dash) {
    MWAW_DEBUG_MSG(("FreeHandParser::readDash: find unexpected zone type for zone %d\n", zId));
  }
  FreeHandParserInternal::StyleHeader zone;
  if (!readStyleHeader(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  int const vers=version();
  if (zone.m_size < 12 || (vers==1 && zone.m_type!=0x1195) || (vers>1 && zone.m_type!=0x157d)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long endPos;
  if (vers==1)
    endPos=pos+2+zone.m_size;
  else {
    endPos=pos-2+zone.m_size;
    for (int i=0; i<2; ++i) { // 0
      auto val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
  }
  f << zone;
  auto N=static_cast<int>(input->readLong(2));
  if (endPos!=input->tell()+2*N || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "dash=[";
  std::vector<float> dashes;
  for (int j=0; j<N; ++j) {
    dashes.push_back(float(input->readLong(2))/10.f);
    f << dashes.back() << ",";
  }
  f << "],";
  if (zId && m_state->m_zIdToDashMap.find(zId)==m_state->m_zIdToDashMap.end())
    m_state->m_zIdToDashMap[zId]=dashes;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool FreeHandParser::readFillStyle(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+13))
    return false;
  f.str("");
  if (zId)
    f << "Entries(FillStyle)[Z" << zId << "]:";
  else
    f << "Entries(FillStyle):";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Fill) {
    MWAW_DEBUG_MSG(("FreeHandParser::readFillStyle: find unexpected zone type for zone %d\n", zId));
  }
  FreeHandParserInternal::StyleHeader zone;
  if (!readStyleHeader(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  FreeHandParserInternal::FillStyle style;
  int expectedSize=0;
  int const vers=version();
  switch (zone.m_type) {
  case 0x10cd:
    f << "basic,";
    if (zone.m_size!=0x12) f << "sz?=" << zone.m_size << ",";
    expectedSize=3;
    break;
  case 0x10d0:
    f << "gradient,";
    style.m_type=MWAWGraphicStyle::Gradient::G_Linear;
    if (zone.m_size!=0x18) f << "sz?=" << zone.m_size << ",";
    expectedSize=8;
    break;
  case 0x10d1:
    f << "radial,";
    style.m_type=MWAWGraphicStyle::Gradient::G_Radial;
    if (zone.m_size!=0x14) f << "sz?=" << zone.m_size << ",";
    expectedSize=4;
    break;
  case 0x14b5:
    f << "basic,";
    if (zone.m_size!=0x16) f << "sz?=" << zone.m_size << ",";
    expectedSize=8;
    break;
  case 0x14b7:
    f << "gradient,";
    style.m_type=MWAWGraphicStyle::Gradient::G_Linear;
    if (zone.m_size!=0x1c) f << "sz?=" << zone.m_size << ",";
    expectedSize=12;
    break;
  case 0x14b8:
    f << "radial,";
    style.m_type=MWAWGraphicStyle::Gradient::G_Radial;
    if (zone.m_size!=0x1e) f << "sz?=" << zone.m_size << ",";
    expectedSize=14;
    break;
  case 0x14d3:
    f << "pattern,";
    if (zone.m_size!=0x1c) f << "sz?=" << zone.m_size << ",";
    expectedSize=14;
    break;
  case 0x14dd:
    f << "tiled,";
    if (zone.m_size!=0x44) f << "sz?=" << zone.m_size << ",";
    expectedSize=54;
    break;
  default:
    break;
  }
  long endPos=input->tell()+expectedSize;
  if (expectedSize==0 || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << zone;
  if (vers>1) {
    for (int i=0; i<2; ++i) { // always 0
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  auto id=static_cast<int>(input->readULong(2));
  if (zone.m_type==0x14dd) {
    if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Group)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    else if (id) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("FreeHandParser::readFillStyle: retrieving tiled style is not implemented\n"));
        first=false;
      }
      f << "group=Z" << id << ",";
    }
    for (int i=0; i<4; ++i) { // always 0
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+2 << "=" << val << ",";
    }
    f << "scale=" << float(input->readLong(4))/65536.f << "x" << float(input->readLong(4))/65536.f << ",";
    f << "decal=" << float(input->readLong(2))/10.f << "x" << float(input->readLong(2))/10.f << ",";
    f << "angle=" << float(input->readLong(2))/10.f << ",";
    f << "fl=" << std::hex << input->readULong(2) << std::dec << ","; // 39|49
    f << "rot=[";
    for (int i=0; i<4; ++i)
      f << float(input->readLong(4))/65536.f << ",";
    f << "],";
    f << "trans=[";
    for (int i=0; i<2; ++i)
      f << float(input->readLong(4))/65536.f/10.f << ",";
    f << "],";
  }
  else if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  else if (id) {
    style.m_colorId[0]=id;
    f << "color=Z" << id << ",";
  }
  if (zone.m_type==0x10d0 || zone.m_type==0x10d1 || zone.m_type==0x14b7 || zone.m_type==0x14b8) {
    id=static_cast<int>(input->readULong(2));
    if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    else if (id) {
      style.m_colorId[1]=id;
      f << "color2=Z" << id << ",";
    }
  }
  if (zone.m_type==0x10d0 || zone.m_type==0x14b7) {
    style.m_angle = float(input->readULong(2))/10.f;
    f << "angle=" << style.m_angle << ",";
    auto val=static_cast<int>(input->readULong(vers==1 ? 1 : 2));
    switch (val) {
    case 1:
      f << "linear,";
      break;
    case 2:
      style.m_logarithm=true;
      f << "logarithm,";
      break;
    default:
      MWAW_DEBUG_MSG(("FreeHandParser::readFillStyle: find unexpected gradient type\n"));
      f << "#gradient[type]=" << val << ",";
    }
  }
  else if (zone.m_type==0x14b8) {
    for (int i=0; i<3; ++i) { // always 0
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
  }
  else if (zone.m_type==0x14d3) {
    MWAWGraphicStyle::Pattern pattern;
    pattern.m_colors[0]=MWAWColor::white();
    pattern.m_colors[1]=MWAWColor::black();
    pattern.m_dim=MWAWVec2i(8,8);
    pattern.m_data.resize(8);
    for (auto &data : pattern.m_data)
      data=static_cast<uint8_t>(input->readULong(1));
    style.m_pattern=pattern;
    f << pattern;
  }
  if ((vers==1 && zone.m_type!=0x10d1) || (vers>1 && zone.m_type==0x14b5)) {
    auto val=static_cast<int>(input->readULong(vers==1 ? 1 : 2)); // always 0
    if (val==1)
      f << "overprint,";
    else
      f << "g0=" << val << ",";
  }
  if (zId && m_state->m_zIdToFillStyleMap.find(zId)==m_state->m_zIdToFillStyleMap.end())
    m_state->m_zIdToFillStyleMap[zId]=style;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool FreeHandParser::readLineStyle(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+13))
    return false;
  f.str("");
  if (zId)
    f << "Entries(LinStyle)[Z" << zId << "]:";
  else
    f << "Entries(LinStyle):";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_LineStyle) {
    MWAW_DEBUG_MSG(("FreeHandParser::readLineStyle: find unexpected zone type for zone %d\n", zId));
  }
  FreeHandParserInternal::StyleHeader zone;
  int const vers=version();
  if (!readStyleHeader(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << zone;
  long endPos=0;
  bool ok=false;
  switch (zone.m_type) {
  case 0x10ce:
    ok=(vers==1);
    endPos=input->tell()+12;
    if (zone.m_size!=0x1c) f << "sz?=" << zone.m_size << ",";
    break;
  case 0x14b6:
    ok=(vers>1);
    endPos=input->tell()+18;
    if (zone.m_size!=0x22) f << "sz?=" << zone.m_size << ",";
    break;
  case 0x14d4:
    f << "pattern,";
    ok=(vers>1);
    endPos=input->tell()+22;
    if (zone.m_size!=0x24) f << "sz?=" << zone.m_size << ",";
    break;
  default:
    ok=false;
    break;
  }
  if (!ok || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (vers>1) {
    for (int i=0; i<2; ++i) { // 0
      auto val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
  }
  FreeHandParserInternal::LineStyle style;
  auto id=static_cast<int>(input->readULong(2));
  if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  else if (id) {
    style.m_colorId=id;
    f << "color=Z" << id << ",";
  }
  if (zone.m_type==0x14d4) {
    MWAWGraphicStyle::Pattern pattern;
    pattern.m_colors[0]=MWAWColor::white();
    pattern.m_colors[1]=MWAWColor::black();
    pattern.m_dim=MWAWVec2i(8,8);
    pattern.m_data.resize(8);
    for (auto &data : pattern.m_data)
      data=static_cast<uint8_t>(input->readULong(1));
    style.m_pattern=pattern;
    f << pattern;
  }
  else {
    id=static_cast<int>(input->readULong(2));
    if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Dash)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    else if (id) {
      style.m_dashId=id;
      f << "dash=Z" << id << ",";
    }
  }
  // probably cosecant(miter/2)
  float value=float(input->readLong(4))/65536.f;
  if (value<=-1||value>=1) {
    style.m_miterLimit=float(360./M_PI)*float(std::asin(1/value));
    f << "miter[limit]=" << style.m_miterLimit << ",";
  }
  else if (value<0||value>0) {
    f << "##miter[limit]=2*asin(" << 1/value << "),";
  }
  else
    f << "miter[limit]*,";
  style.m_width=vers==1 ? float(input->readLong(2))/10.0f  : float(input->readLong(4))/65536.f/10.0f;
  f << "width=" << style.m_width << ",";
  if (zone.m_type!=0x14d4) {
    auto val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0: // default
      break;
    case 1:
      style.m_join=MWAWGraphicStyle::J_Bevel;
      f << "join=bevel,";
      break;
    case 2:
      style.m_join=MWAWGraphicStyle::J_Round;
      f << "join=round,";
      break;
    default:
      MWAW_DEBUG_MSG(("FreeHandParser::readLineStyle: find unknown join\n"));
      f << "#join=" << val << ",";
    }
    val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0: // default
      break;
    case 1:
      style.m_cap=MWAWGraphicStyle::C_Round;
      f << "cap=round,";
      break;
    case 2:
      style.m_cap=MWAWGraphicStyle::C_Square;
      f << "cap=square,";
      break;
    default:
      MWAW_DEBUG_MSG(("FreeHandParser::readLineStyle: find unknown cap\n"));
      f << "#cap=" << val << ",";
    }
  }
  if (zId && m_state->m_zIdToLineStyleMap.find(zId)==m_state->m_zIdToLineStyleMap.end())
    m_state->m_zIdToLineStyleMap[zId]=style;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool FreeHandParser::readPostscriptStyle(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  if (!input->checkPosition(pos+12))
    return false;
  f.str("");
  if (zId)
    f << "Entries(Postscript)[Z" << zId << "]:";
  else
    f << "Entries(Postscript):";
  if (zId) {
    auto type=m_state->getZoneType(zId);
    if (type!=FreeHandParserInternal::Z_Fill && type!=FreeHandParserInternal::Z_LineStyle) {
      MWAW_DEBUG_MSG(("FreeHandParser::readPostscriptStyle: find unexpected zone type for zone %d\n", zId));
    }
  }
  FreeHandParserInternal::StyleHeader zone;
  if (!readStyleHeader(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << zone;
  long endPos;
  int sSz;
  if (version()==1) {
    if (zone.m_type!=0x10cf) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (zone.m_size!=0x12) f << "sz?=" << zone.m_size << ",";
    sSz=static_cast<int>(input->readULong(1));
    endPos=input->tell()+sSz;
  }
  else {
    bool ok=true;
    if (zone.m_type==0x14c9)
      f << "surf,";
    else if (zone.m_type==0x14ca)
      f << "line,";
    else
      ok=false;
    if (!ok || zone.m_size<16) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    for (int i=0; i<2; ++i) { // always 0?
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    endPos=pos+zone.m_size-4;
    sSz=int(zone.m_size-16);
  }
  if (!input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  std::string text;
  for (int i=0; i<sSz; ++i) text+=char(input->readULong(1));
  if (!text.empty())
    f << "ps=\"" << text << "\",";
  if (zId && m_state->m_zIdToPostscriptMap.find(zId)==m_state->m_zIdToPostscriptMap.end())
    m_state->m_zIdToPostscriptMap[zId]=text;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool FreeHandParser::readBackgroundPicture(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape) || shape.m_type!=0x1007 || !input->checkPosition(input->tell()+32)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  FreeHandParserInternal::Shape res;
  res.m_type=FreeHandParserInternal::Shape::BackgroundPicture;
  res.m_layerId=shape.m_layerId;
  if (zId)
    f << "Entries(BackgroundPicture)[Z" << zId << "]:" << shape;
  else
    f << "Entries(BackgroundPicture):" << shape;
  for (int i=0; i<14; ++i) { // f1=29|39, f2=1, f8=1, f10=0|-5, f12=109|113|118
    auto val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto picSize=long(input->readLong(4));
  res.m_picture.setBegin(input->tell());
  res.m_picture.setLength(picSize);
  if (picSize<0 || !input->checkPosition(res.m_picture.end())) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().skipZone(res.m_picture.begin(), res.m_picture.end()-1);
  input->seek(picSize, librevenge::RVNG_SEEK_CUR);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readPictureZone(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape) || shape.m_type!=0x13f8 || !input->checkPosition(input->tell()+58)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long endPos=input->tell()+58;
  FreeHandParserInternal::Shape res;
  res.m_type=FreeHandParserInternal::Shape::Picture;
  res.m_layerId=shape.m_layerId;
  if (zId)
    f << "Entries(Picture)[Z" << zId << "]:" << shape;
  else
    f << "Entries(Picture):" << shape;
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Shape) {
    MWAW_DEBUG_MSG(("FreeHandParser::readPictureZone: find unexpected zone type for zone %d\n", zId));
  }
  for (int i=0; i<2; ++i) {
    auto id=static_cast<int>(input->readULong(2));
    if (!id) continue;
    if (!m_state->addZoneId(id, i==0 ? FreeHandParserInternal::Z_Picture : FreeHandParserInternal::Z_PictureName)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (i==0) {
      res.m_dataId=id;
      f << "data=Z" << id << ",";
    }
    else
      f << "name=Z" << id << ",";
  }
  auto val=static_cast<int>(input->readLong(2)); // 0
  if (val) f << "f0=" << val << ",";
  float dim[6];
  for (int i=0; i<2; ++i) dim[i]=float(input->readLong(2))/10.f;
  f << "dim=" << MWAWVec2f(dim[1],dim[0]) << ",";
  // checkme: why are the coord inverted ?
  res.m_box=MWAWBox2f(MWAWVec2f(0,0), MWAWVec2f(dim[1],dim[0]));
  for (int i=0; i<2; ++i) { // 0?
    val=static_cast<int>(input->readLong(2)); // 0
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  f << "flags=" << std::hex << input->readULong(2) << std::dec << ",";
  f << "rot=[";
  for (int i=0; i<4; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  f << "trans=[";
  for (int i=0; i<2; ++i) {
    dim[i+4]=float(input->readLong(4))/65536.f/10.f;
    f << dim[i+4] << ",";
  }
  f << "],";
  res.m_transformation=MWAWTransformation(MWAWVec3f(dim[0],dim[2],dim[4]),MWAWVec3f(dim[1],dim[3],dim[5]));
  auto id=static_cast<int>(input->readULong(2));
  if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readPictureZone: find unexpected colorId\n"));
    f << "###colorId,";
  }
  else if (id)
    f << "color=Z" << id << ",";
  for (int i=0; i<2; ++i) {
    int iDim[4];
    for (auto &d : iDim) d=static_cast<int>(input->readLong(2));
    f << "box" << i << "=" << MWAWBox2i(MWAWVec2i(iDim[0],iDim[1]),MWAWVec2i(iDim[2],iDim[3])) << ",";
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readShape(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  bool canHaveMatrix=true, hasDimension=true;
  int dataSize=-1;
  FreeHandParserInternal::Shape res;
  res.m_layerId=shape.m_layerId;
  const int vers=version();
  switch (shape.m_type) {
  case 0x1131:
  case 0x1519:
    if (zId)
      f << "Entries(Rectangle)[Z" << zId << "]:" << shape;
    else
      f << "Entries(Rectangle):" << shape;
    dataSize=4;
    res.m_type=FreeHandParserInternal::Shape::Rectangle;
    break;
  case 0x1132:
  case 0x151a:
    if (zId)
      f << "Entries(Circle)[Z" << zId << "]:" << shape;
    else
      f << "Entries(Circle):" << shape;
    dataSize=0;
    res.m_type=FreeHandParserInternal::Shape::Ellipse;
    break;
  case 0x1134:
  case 0x151c:
    if (zId)
      f << "Entries(Spline)[Z" << zId << "]:" << shape;
    else
      f << "Entries(Spline):" << shape;
    dataSize=4;
    hasDimension=canHaveMatrix=false;
    res.m_type=FreeHandParserInternal::Shape::Path;
    break;
  case 0x1135:
  case 0x151d:
    if (zId)
      f << "Entries(Line)[Z" << zId << "]:" << shape;
    else
      f << "Entries(Line):" << shape;
    dataSize=0;
    canHaveMatrix=false;
    res.m_type=FreeHandParserInternal::Shape::Line;
    break;
  default:
    break;
  }
  if (dataSize<0 || !input->checkPosition(input->tell()+4+(vers>1 ? 6 : 0)+(hasDimension ? 8 : 0)+(canHaveMatrix ? 4 : 0)+dataSize)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "sz?=" << shape.m_size << ",";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Shape) {
    MWAW_DEBUG_MSG(("FreeHandParser::readShape: find unexpected zone type for zone %d\n", zId));
  }
  if (vers>1) {
    auto id=static_cast<int>(input->readULong(2));
    if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Group)) {
      MWAW_DEBUG_MSG(("FreeHandParser::readShapeHeader: find unexpected group id\n"));
      f << "###groupId,";
    }
    else if (id) {
      f << "group=Z" << id << ",";
      res.m_childs.push_back(id);
    }
  }
  auto id=static_cast<int>(input->readLong(2)); // always 0?
  if (id &&  !m_state->addZoneId(id, FreeHandParserInternal::Z_Fill)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readShape: find a bad color\n"));
    f << "###";
  }
  if (id) f << "fill=Z" << id << ",";
  res.m_fillId=id;
  id=static_cast<int>(input->readULong(2));
  if (id &&  !m_state->addZoneId(id, FreeHandParserInternal::Z_LineStyle)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readShape: find a bad style\n"));
    f << "###";
  }
  if (id) f << "line[style]=Z" << id << ",";
  res.m_lineId=id;
  if (vers>1) {
    for (int i=0; i<2; ++i) { // always 0?
      auto val=static_cast<int>(input->readULong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  if (hasDimension) {
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    res.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
    f << "rect=" << res.m_box << ",";
  }
  int dSz=canHaveMatrix ? static_cast<int>(input->readULong(4)) : 0;
  if (dSz<0 || !input->checkPosition(input->tell()+dSz+dataSize)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (dSz==0x1a) {
    f << "flags=" << std::hex << input->readULong(2) << std::dec << ",";
    float dim[6];
    f << "rot=[";
    for (int i=0; i<4; ++i) {
      dim[i]=float(input->readLong(4))/65536.f;
      f << dim[i] << ",";
    }
    f << "],";
    f << "trans=[";
    for (int i=0; i<2; ++i) {
      dim[i+4]=float(input->readLong(4))/65536.f/10.f;
      f << dim[i+4] << ",";
    }
    f << "],";
    res.m_transformation=MWAWTransformation(MWAWVec3f(dim[0],dim[2],dim[4]),MWAWVec3f(dim[1],dim[3],dim[5]));
  }
  else if (dSz) {
    MWAW_DEBUG_MSG(("FreeHandParser::readShape: find unknown matrix size\n"));
    f << "###matrix,";
    input->seek(dSz, librevenge::RVNG_SEEK_CUR);
  }
  if (shape.m_type==0x1131 || shape.m_type==0x1519) {
    float dim[2];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    res.m_corner=MWAWVec2f(dim[0],dim[1]);
    if (res.m_corner!=MWAWVec2f(0,0))
      f << "corner=" << res.m_corner << ",";
  }
  if (shape.m_type==0x1134 || shape.m_type==0x151c) {
    auto val=static_cast<int>(input->readULong(2));
    if (val&1) {
      res.m_closed=true;
      f << "closed,";
    }
    if (val&2) {
      res.m_evenOdd=true;
      f << "even/odd,";
    }
    val &= 0xFFFC;
    // find also 4
    if (val) f << "fl=" << std::hex << val << std::dec << ",";
    auto nPt=static_cast<int>(input->readULong(2));
    f << "N=" << nPt << ",";
    if (!input->checkPosition(input->tell()+16*nPt)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    for (int i=0; i<nPt; ++i) {
      pos=input->tell();
      f.str("");
      f << "Spline-" << i << ":";
      val=static_cast<int>(input->readULong(2));
      switch (val) {
      case 0: // corner
        break;
      case 1:
        f << "connector,";
        break;
      case 2:
        f << "curve,";
        break;
      default:
        // find also 0xf0
        MWAW_DEBUG_MSG(("FreeHandParser::readShape: find unknown point type\n"));
        f << "#type=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if (val&0x100) f << "no[autoCurvature],";
      val &= 0xFEFF;
      // find unknow [01][4|9|b]
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      MWAWVec2f coord[3];
      for (auto &pt : coord) {
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(2))/10.f;
        pt=MWAWVec2f(dim[0], dim[1]);
        res.m_vertices.push_back(pt);
      }
      if (coord[0]==coord[1] && coord[0]==coord[2])
        f << coord[0] << ",";
      else
        f << "pts=[" << coord[0] << "," << coord[1] << "," << coord[2] << "],";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
  }
  else {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (zId && m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end())
    m_state->m_zIdToShapeMap.insert(std::map<int,FreeHandParserInternal::Shape>::value_type(zId,res));
  return true;
}

bool FreeHandParser::readTextboxV1(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape) || shape.m_type!=0x1006 || !input->checkPosition(input->tell()+4+8+54)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (zId)
    f << "Entries(Textbox)[Z" << zId << "]:" << shape;
  else
    f << "Entries(Textbox):" << shape;
  FreeHandParserInternal::Textbox textbox(zId);
  textbox.m_layerId=shape.m_layerId;
  f << "sz?=" << shape.m_size << ",";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Shape) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find unexpected zone type for zone %d\n", zId));
  }
  auto val=static_cast<int>(input->readLong(2)); // always 0?
  if (val) f << "f0=" << val << ",";
  auto nbPt=static_cast<int>(input->readULong(2)); // nbPt=4*textSz
  f << "N=" << nbPt << ",";
  long actPos=input->tell();
  if ((nbPt%2) || !input->checkPosition(actPos+3*(nbPt/2)+8+54)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  // probably nbPt/2 float (size of each char)+ nbPt/2 bytes (flag?)
  ascii().skipZone(actPos, actPos+3*(nbPt/2)-1);

  actPos+=3*(nbPt/2);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  f.str("");
  f << "Textbox-A:";
  for (int i=0; i<3; ++i) { // f0=0|2|-82|-123|-225, f1=0|-41|-82|-123|-151, f2=0
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto sSz=static_cast<int>(input->readULong(2));
  if (!input->checkPosition(input->tell()+sSz+54)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "text[sz]=" << sSz << ",";
  ascii().addPos(actPos);
  ascii().addNote(f.str().c_str());

  textbox.m_text.setBegin(input->tell());
  textbox.m_text.setLength(sSz);
  input->seek(textbox.m_text.end(), librevenge::RVNG_SEEK_SET);

  actPos=input->tell();
  f.str("");
  f << "Textbox-B:";
  float dim[6];
  for (int i=0; i<4; ++i) dim[i]=float(input->readLong(2))/10.f;
  textbox.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  f << "dim=" << textbox.m_box << ",";
  f << "flags=" << std::hex << input->readULong(2) << std::dec << ",";
  f << "rot=[";
  for (int i=0; i<4; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  f << "trans=[";
  for (int i=0; i<2; ++i) {
    dim[i+4]=float(input->readLong(4))/65536.f/10.f;
    f << dim[i+4] << ",";
  }
  f << "],";
  textbox.m_transformation=MWAWTransformation(MWAWVec3f(dim[0],dim[2],dim[4]),MWAWVec3f(dim[1],dim[3],dim[5]));
  f << "spacing=["; // letter and word
  for (int i=0; i<2; ++i) {
    dim[i]=float(input->readLong(4))/65536.f/10.f;
    f << dim[i] << ",";
  }
  f << "],";
  textbox.m_spacings=MWAWVec2f(dim[0],dim[1]);
  f << "scaling=[";
  for (int i=0; i<2; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  textbox.m_scalings=MWAWVec2f(dim[0],dim[1]);
  val=static_cast<int>(input->readLong(1)); // 0|1|2
  if (val) f << "f0=" << val << ",";
  val=static_cast<int>(input->readLong(1));
  switch (val) {
  case 0: // left
    break;
  case 1:
    f << "right,";
    textbox.m_justify=MWAWParagraph::JustificationRight;
    break;
  case 2:
    f << "center,";
    textbox.m_justify=MWAWParagraph::JustificationCenter;
    break;
  case 3:
    f << "justify=all,";
    textbox.m_justify=MWAWParagraph::JustificationFull;
    break;
  default:
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find unexpected align\n"));
    f << "###align=" << val << ",";
    break;
  }
  auto nPLC=static_cast<int>(input->readULong(2));
  f << "NPLC=" << nPLC << ",";
  if (!input->checkPosition(input->tell()+18*nPLC)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().addPos(actPos);
  ascii().addNote(f.str().c_str());
  for (int plc=0; plc<nPLC; ++plc) {
    actPos=input->tell();
    f.str("");
    f << "Textbox-PLC" << plc << ":";
    FreeHandParserInternal::Font font;
    if (plc+1!=nPLC) {
      auto id=static_cast<int>(input->readULong(2));
      if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_String)) {
        MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find bad font name\n"));
        f << "###";
      }
      font.m_nameId=id;
      if (id)
        f << "font[name]=Z" << id << ",";
      id=static_cast<int>(input->readULong(2));
      if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
        MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find bad color\n"));
        f << "###";
      }
      if (id)
        f << "color=Z" << id << ",";
      font.m_colorId=id;
      float sz=float(input->readLong(4))/65536.f;
      font.m_font.setSize(sz);
      f << "font[sz]=" << sz << ",";
      val=static_cast<int>(input->readLong(4));
      switch (val) { // useme
      case -2: // solid
        break;
      case -1:
        f << "leading=auto,";
        break;
      default:
        f << "leading=" << float(val)/65536.f << ",";
      }
    }
    else
      input->seek(12, librevenge::RVNG_SEEK_CUR);
    auto cPos=static_cast<int>(input->readULong(2));
    f << "pos=" << cPos << ",";
    uint32_t flags=0;
    val=static_cast<int>(input->readULong(2));
    if (val & 1) {
      flags |= MWAWFont::boldBit;
      f << "bold,";
    }
    if (val & 2) {
      flags |= MWAWFont::italicBit;
      f << "italic,";
    }
    val &= 0xFFFC;
    if (val && plc+1!=nPLC) {
      MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find unknown font flag1\n"));
      f << "##flag1=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readULong(2));
    switch (val) {
    case 1: // solid
      break;
    case 2:
      flags |= MWAWFont::boldBit;
      f << "heavy,";
      break;
    case 3:
      flags |= MWAWFont::italicBit;
      f << "oblique,";
      break;
    case 4:
      flags |= MWAWFont::outlineBit;
      f << "outline,";
      break;
    case 5:
      flags |= MWAWFont::shadowBit;
      f << "shadow,";
      break;
    default:
      if (plc+1==nPLC) break;
      MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV1: find unknown font flag2\n"));
      f << "##flag2=" << val << ",";
      break;
    }
    font.m_font.setFlags(flags);
    textbox.m_posToFontMap[cPos]=font;
    ascii().addPos(actPos);
    ascii().addNote(f.str().c_str());
  }
  if (zId && m_state->m_zIdToTextboxMap.find(zId)==m_state->m_zIdToTextboxMap.end())
    m_state->m_zIdToTextboxMap.insert(std::map<int,FreeHandParserInternal::Textbox>::value_type(zId,textbox));

  return true;
}

bool FreeHandParser::readTextboxV2(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos=input->tell();
  FreeHandParserInternal::ShapeHeader shape;
  if (!readShapeHeader(shape) || shape.m_type!=0x13ee || !input->checkPosition(input->tell()+66)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (zId)
    f << "Entries(Textbox)[Z" << zId << "]:" << shape;
  else
    f << "Entries(Textbox):" << shape;
  FreeHandParserInternal::Textbox textbox(zId);
  textbox.m_layerId=shape.m_layerId;
  f << "sz?=" << shape.m_size << ",";
  if (zId && m_state->getZoneType(zId)!=FreeHandParserInternal::Z_Shape) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find unexpected zone type for zone %d\n", zId));
  }
  for (int i=0; i<6; ++i) { // f3=0|2, f4=0|1
    auto val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto val=static_cast<int>(input->readULong(2)); // 1a|7b|c3|eb|f4|fb|114|1af|7d00|7d02
  if (val) f << "f6=" << val << ",";
  float dim[6];
  for (int i=0; i<4; ++i) dim[i]=float(input->readLong(2))/10.f;
  textbox.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  f << "dim=" << textbox.m_box << ",";
  val=static_cast<int>(input->readULong(2)); // 0
  if (val) f << "f7=" << val << ",";
  f << "flags=" << std::hex << input->readULong(2) << std::dec << ",";
  f << "rot=[";
  for (int i=0; i<4; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  f << "trans=[";
  for (int i=0; i<2; ++i) {
    dim[i+4]=float(input->readLong(4))/65536.f/10.f;
    f << dim[i+4] << ",";
  }
  f << "],";
  textbox.m_transformation=MWAWTransformation(MWAWVec3f(dim[0],dim[2],dim[4]),MWAWVec3f(dim[1],dim[3],dim[5]));
  val=static_cast<int>(input->readLong(1));
  switch (val) {
  case 0: // left
    break;
  case 1:
    f << "center,";
    textbox.m_justify=MWAWParagraph::JustificationCenter;
    break;
  case 2:
    f << "right,";
    textbox.m_justify=MWAWParagraph::JustificationRight;
    break;
  case 3:
    f << "justify=all,";
    textbox.m_justify=MWAWParagraph::JustificationFull;
    break;
  default:
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find unexpected align\n"));
    f << "###align=" << val << ",";
    break;
  }
  ascii().addDelimiter(input->tell(), '|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(11, librevenge::RVNG_SEEK_CUR);

  pos=input->tell();
  f.str("");
  f << "Textbox-A:";
  auto dSz=static_cast<int>(input->readULong(2));
  auto sSz=static_cast<int>(input->readULong(2));
  long endPos=pos+dSz-18-80;
  if (dSz-18-80<58 || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "text[sz]=" << sSz << ",";
  val=static_cast<int>(input->readULong(2));
  if (val!=sSz) f << "text[pos]=" << val << ",";
  val=static_cast<int>(input->readULong(2)); // always 7ffd ?
  if (val!=0x7ffd) f << "f0=" << val << ",";
  for (int i=0; i<2; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  FreeHandParserInternal::Font font;
  auto id=static_cast<int>(input->readULong(2));
  if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_String)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find bad font name\n"));
    f << "###";
  }
  font.m_nameId=id;
  if (id)
    f << "font[name]=Z" << id << ",";
  float sz=float(input->readLong(4))/65536.f;
  font.m_font.setSize(sz);
  f << "font[sz]=" << sz << ",";
  val=static_cast<int>(input->readULong(4));
  // use me
  if (val==static_cast<int>(0xFFFE0000))
    f << "leading=auto,";
  else if (val!=static_cast<int>(0xFFFF0000)) // no solid
    f << "leading=" << float(val)/65536.f << ",";
  val=static_cast<int>(input->readLong(2));
  if (val) f << "f4=" << val << ",";
  uint32_t flags=0;
  val=static_cast<int>(input->readULong(2));
  if (val & 1) {
    flags |= MWAWFont::boldBit;
    f << "bold,";
  }
  if (val & 2) {
    flags |= MWAWFont::italicBit;
    f << "italic,";
  }
  val &= 0xFFFC;
  if (val) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find unknown font flag1\n"));
    f << "##flag1=" << std::hex << val << std::dec << ",";
  }
  id=static_cast<int>(input->readULong(2));
  if (id && !m_state->addZoneId(id, FreeHandParserInternal::Z_Color)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find bad color\n"));
    f << "###";
  }
  if (id)
    f << "color=Z" << id << ",";
  font.m_colorId=id;
  val=static_cast<int>(input->readLong(2)); // 0
  if (val) f << "f6=" << val << ",";
  auto special=static_cast<int>(input->readLong(2));
  int specialData[6];
  for (int i=0; i<6; ++i) specialData[i]=static_cast<int>(input->readULong(i>=4 ? 1 : 2));
  if (!m_state->addZoneId(specialData[0], FreeHandParserInternal::Z_Color)) {
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find bad text color\n"));
    f << "###";
  }
  if (specialData[0])
    f << "col2=Z" << specialData[0] << ",";
  switch (special) {
  case 1: // solid
    break;
  case 2:
    flags |= MWAWFont::boldBit;
    f << "heavy,";
    break;
  case 3:
    flags |= MWAWFont::italicBit;
    f << "oblique,";
    break;
  case 4:
    flags |= MWAWFont::outlineBit;
    f << "outline,";
    break;
  case 5:
    flags |= MWAWFont::shadowBit;
    f << "shadow,";
    break;
  case 6:
    f << "fillAndStroke,";
    if (specialData[1]||specialData[2]) {
      f << "stroke[w]=" << float((specialData[1]<<16)+specialData[2])/65536.f << ",";
      specialData[1]=specialData[2]=0;
    }
    if (specialData[3]&0x100) f << "fill[set],";
    if (specialData[3]&0x1) f << "fill[overprint],";
    specialData[3]&=0xfefe;
    for (int i=0; i<2; ++i) {
      if (!specialData[4+i]) continue;

      char const *wh[]= {"stroke[set]", "stroke[overprint]"};
      f << wh[i] << "=" << specialData[4+i] << ",";
      specialData[4+i]=0;
    }
    break;
  case 0x79:
    f << "char,";
    for (int i=1; i<5; ++i) {
      if (!specialData[i]) continue;
      char const *wh[]= {"", "fill[sz]", "line[spacing]", "stroke[width]", "has[stroke]"};
      if (i==3)
        f << wh[i] << "=" << float(specialData[i])/10.f << ",";
      else
        f << wh[i] << "=" << specialData[i] << ",";
      specialData[i]=0;
    }
    break;
  case 0x7a:
    f << "zoom,";
    for (int i=1; i<4; ++i) {
      if (!specialData[i]) continue;
      char const *wh[]= {"", "zoom[horOffset]", "zoom[verOffset]", "zoom[%]"};
      f << wh[i] << "=" << specialData[i] << ",";
      specialData[i]=0;
    }
    break;
  default:
    MWAW_DEBUG_MSG(("FreeHandParser::readTextboxV2: find unknown font flag2\n"));
    f << "##flag2=" << special << ",";
    break;
  }
  for (int i=1; i<6; ++i) {
    if (specialData[i])
      f << "#special" << i << "=" << specialData[i] << ",";
  }
  font.m_font.setFlags(flags);
  textbox.m_posToFontMap[0]=font;
  f << "spacing=["; // letter and word
  for (int i=0; i<2; ++i) {
    dim[i]=float(input->readLong(4))/65536.f;
    f << dim[i] << ",";
  }
  f << "],";
  textbox.m_spacings=MWAWVec2f(dim[0],dim[1]);
  textbox.m_scalings[0]=float(input->readLong(4))/65536.f;
  f << "scalings[hor]=" << textbox.m_scalings[0] << ",";
  textbox.m_baseline=float(input->readLong(4))/65536.f;
  if (textbox.m_baseline<0||textbox.m_baseline>0)
    f << "baseline=" << textbox.m_baseline << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  /* CHECKME: find some blocks here : [12bytes]* followed by [unkn]* and [22bytes]*
     for instance
     00290003007a9ce0000ffed1
     002a00190069046000100000
     0012000...
     001a0000000000c9f280000ac3de00033c22000f0000
     00290000000000690460000ac3de00033c22000f0000
     004d0000000000e2f160000ac3de00033c2200120000
     006000000000007a6860000ac3de00033c22000f0000
   */
  if (endPos!=input->tell()) {
    pos=input->tell();
    f.str("");
    f << "Textbox-B:";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  textbox.m_text.setBegin(input->tell());
  textbox.m_text.setLength(sSz);
  input->seek(textbox.m_text.end(), librevenge::RVNG_SEEK_SET);

  if (zId && m_state->m_zIdToTextboxMap.find(zId)==m_state->m_zIdToTextboxMap.end())
    m_state->m_zIdToTextboxMap.insert(std::map<int,FreeHandParserInternal::Textbox>::value_type(zId,textbox));

  return true;
}

bool FreeHandParser::readDataZone(int zId)
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  long pos=input->tell();
  if (!input->checkPosition(pos+10)) return false;
  auto dSz=long(input->readULong(4));
  auto opCode=static_cast<int>(input->readULong(2));
  auto dataSize=long(input->readULong(4));
  long endPos=pos+10+dataSize;
  if (opCode!=0x138b || dataSize<0 || !input->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  auto type=zId ? m_state->getZoneType(zId) : FreeHandParserInternal::Z_Unknown;
  if (type==FreeHandParserInternal::Z_Note) {
    f << "Entries(Note)[Z" << zId << "]:";
    if (dataSize) {
      auto sSz=static_cast<int>(input->readULong(1));
      if (sSz+1>dataSize) {
        MWAW_DEBUG_MSG(("FreeHandParser::readDataZone: can not read the note size\n"));
        f << "##sSz";
      }
      else {
        std::string note;
        for (int i=0; i<sSz; ++i) note+=char(input->readULong(1));
        f << note;
      }
    }
  }
  else if (type==FreeHandParserInternal::Z_PictureName) {
    f << "Picture[name][Z" << zId << "]:";
    if (dataSize<6) {
      MWAW_DEBUG_MSG(("FreeHandParser::readDataZone: can not read the picture name zone\n"));
      f << "##sSz";
    }
    else {
      auto val=static_cast<int>(input->readLong(4)); // disk id?
      if (val) f << "f0=" << std::hex << val << std::dec << ",";
      for (int i=0; i<2; ++i) { // disk name ?, file name
        auto sSz=static_cast<int>(input->readULong(1));
        if (input->tell()+sSz>endPos) {
          MWAW_DEBUG_MSG(("FreeHandParser::readDataZone: can not read some string\n"));
          f << "##sSz";
          break;
        }
        std::string name;
        for (int c=0; c<sSz; ++c) name+=char(input->readULong(1));
        f << name << ",";
      }
    }
  }
  else if (type==FreeHandParserInternal::Z_Picture) {
    f << "Picture[data][Z" << zId << "]:";
    if (dataSize) {
      MWAWEntry entry;
      entry.setBegin(input->tell());
      entry.setLength(dataSize);
      if (zId && m_state->m_zIdToDataMap.find(zId)==m_state->m_zIdToDataMap.end())
        m_state->m_zIdToDataMap[zId]=entry;
      ascii().skipZone(entry.begin(), entry.end()-1);
    }
  }
  else {
    MWAW_DEBUG_MSG(("FreeHandParser::readDataZone: find unknown zone\n"));
    if (zId)
      f << "Entries(DataZone)[Z" << zId << "]:";
    else
      f << "Entries(DataZone):";
    if (dSz!=dataSize+5)
      f << "sz?=" << dSz << ",";
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool FreeHandParser::openLayer(int zId)
{
  if (zId<0 || m_state->m_actualLayer>=0 || m_state->m_sendLayerSet.find(zId)!=m_state->m_sendLayerSet.end())
    return false;
  if (!getGraphicListener()) {
    MWAW_DEBUG_MSG(("FreeHandParser::openLayer: can not find the listener\n"));
    return false;
  }
  m_state->m_sendLayerSet.insert(zId);
  librevenge::RVNGString layer;
  layer.sprintf("%d", zId);
  if (!getGraphicListener()->openLayer(layer))
    return false;
  m_state->m_actualLayer=zId;
  return true;
}

void FreeHandParser::closeLayer()
{
  if (m_state->m_actualLayer<0)
    return;
  getGraphicListener()->closeLayer();
  m_state->m_actualLayer=-1;
}

bool FreeHandParser::sendZone(int zId, MWAWTransformation const &transform)
{
  if (!getGraphicListener()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendZone: can not find the listener\n"));
    return false;
  }
  if (m_state->m_zIdToTextboxMap.find(zId)!=m_state->m_zIdToTextboxMap.end())
    return sendTextbox(m_state->m_zIdToTextboxMap.find(zId)->second, transform);
  if (m_state->m_zIdToShapeMap.find(zId)==m_state->m_zIdToShapeMap.end()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendZone: can not find the zone %d\n", zId));
    return false;
  }
  auto const &shape=m_state->m_zIdToShapeMap.find(zId)->second;
  shape.m_isSent=true;
  if (shape.m_type==FreeHandParserInternal::Shape::Group ||
      shape.m_type==FreeHandParserInternal::Shape::JoinGroup)
    return sendGroup(shape, transform);
  if (shape.m_type==FreeHandParserInternal::Shape::Picture)
    return sendPicture(shape, transform);
  if (shape.m_type==FreeHandParserInternal::Shape::BackgroundPicture)
    return sendBackgroundPicture(shape, transform);
  if (shape.m_type!=FreeHandParserInternal::Shape::Unknown)
    return sendShape(shape, transform);
  return false;
}

bool FreeHandParser::sendGroup(FreeHandParserInternal::Shape const &group, MWAWTransformation const &transform)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendGroup: can not find the listener\n"));
    return false;
  }
  if (group.m_childs.empty()) return true;
  if (m_state->m_sendIdSet.find(group.m_id)!=m_state->m_sendIdSet.end()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendGroup: sorry the zone %d is already sent\n", group.m_id));
    return false;
  }
  m_state->m_sendIdSet.insert(group.m_id);
  MWAWTransformation transf=transform*group.m_transformation;
  bool createGroup=group.m_childs.size()>1 && group.m_id!=m_state->m_mainGroupId;
  // TODO check for join group
  bool newLayer=openLayer(group.m_layerId);
  if (createGroup) {
    MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(0,0), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
  }
  bool checkLayer=m_state->m_actualLayer==-1;
  int actualLayerId=-1;
  for (int cId : group.m_childs) {
    if (checkLayer) {
      int newLayerId=-1;
      if (m_state->m_zIdToTextboxMap.find(cId)!=m_state->m_zIdToTextboxMap.end())
        newLayerId=m_state->m_zIdToTextboxMap.find(cId)->second.m_layerId;
      else if (m_state->m_zIdToShapeMap.find(cId)!=m_state->m_zIdToShapeMap.end())
        newLayerId=m_state->m_zIdToShapeMap.find(cId)->second.m_layerId;
      if (newLayerId!=actualLayerId) {
        if (actualLayerId>=0)
          closeLayer();
        if (openLayer(newLayerId))
          actualLayerId=newLayerId;
        else
          actualLayerId=-1;
      }
    }
    sendZone(cId, transf);
  }
  if (actualLayerId>=0) closeLayer();
  if (createGroup)
    listener->closeGroup();
  m_state->m_sendIdSet.erase(group.m_id);
  if (newLayer) closeLayer();
  return true;
}

bool FreeHandParser::sendBackgroundPicture(FreeHandParserInternal::Shape const &picture, MWAWTransformation const &)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendBackgroundPicture: can not find the listener\n"));
    return false;
  }
  if (!picture.m_picture.valid()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendBackgroundPicture: can not find the background picture\n"));
    return false;
  }
  MWAWInputStreamPtr input=getInput();
  input->seek(picture.m_picture.begin(), librevenge::RVNG_SEEK_SET);
  librevenge::RVNGBinaryData data;
  if (!input->readDataBlock(picture.m_picture.length(), data) || data.empty()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendBackgroundPicture: oops the picture is empty\n"));
    return false;
  }
#ifdef DEBUG_WITH_FILES
  static int volatile pictName = 0;
  libmwaw::DebugStream f;
  f << "PICT-" << ++pictName << ".pct";
  libmwaw::Debug::dumpFile(data, f.str().c_str());
#endif
  MWAWPosition pos(MWAWVec2f(float(getPageSpan().getMarginLeft()), float(getPageSpan().getMarginTop())),
                   MWAWVec2f(float(getPageSpan().getPageWidth()), float(getPageSpan().getPageLength())), librevenge::RVNG_INCH);
  pos.m_anchorTo = MWAWPosition::Page;
  pos.setOrder(-1);
  MWAWEmbeddedObject pict(data);
  listener->insertPicture(pos, pict);
  return true;
}

bool FreeHandParser::sendPicture(FreeHandParserInternal::Shape const &picture, MWAWTransformation const &transform)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendPicture: can not find the listener\n"));
    return false;
  }
  if (m_state->m_zIdToDataMap.find(picture.m_dataId)==m_state->m_zIdToDataMap.end() ||
      !m_state->m_zIdToDataMap.find(picture.m_dataId)->second.valid()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendPicture: can not find the  picture\n"));
    return false;
  }
  MWAWInputStreamPtr input=getInput();
  MWAWEntry entry=m_state->m_zIdToDataMap.find(picture.m_dataId)->second;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  librevenge::RVNGBinaryData data;
  if (!input->readDataBlock(entry.length(), data) || data.empty()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendPicture: oops the picture is empty\n"));
    return false;
  }
#ifdef DEBUG_WITH_FILES
  static int volatile pictName = 0;
  libmwaw::DebugStream f;
  f << "PICT-" << ++pictName << ".pct";
  libmwaw::Debug::dumpFile(data, f.str().c_str());
#endif
  MWAWGraphicStyle style=MWAWGraphicStyle::emptyStyle();
  MWAWTransformation finalTransformation=transform*picture.m_transformation;
  MWAWTransformation transf;
  float rotation=0;
  MWAWBox2f box;
  if (decomposeMatrix(finalTransformation,rotation,transf,picture.m_box.center())) {
    box=transf*picture.m_box;
    style.m_rotate=rotation;
  }
  else
    box=finalTransformation*picture.m_box;
  for (int c=0; c<2; ++c) {
    if (box.min()[c]>box.max()[c])
      std::swap(box.min()[c],box.max()[c]);
  }
  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  MWAWEmbeddedObject pict(data);
  listener->insertPicture(pos, pict, style);
  return true;
}

bool FreeHandParser::sendShape(FreeHandParserInternal::Shape const &shape, MWAWTransformation const &transform)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendShape: can not find the listener\n"));
    return false;
  }
  MWAWGraphicStyle style;
  m_state->updateLineStyle(shape.m_lineId, style);
  if (shape.m_type!=FreeHandParserInternal::Shape::Line &&
      (shape.m_type!=FreeHandParserInternal::Shape::Path || shape.m_closed))
    m_state->updateFillStyle(shape.m_fillId, style);
  MWAWTransformation finalTransformation=transform*shape.m_transformation;
  MWAWGraphicShape res;
  if (shape.updateShape(res)) {
    res=res.transform(finalTransformation);
    MWAWPosition pos(res.m_bdBox[0], res.m_bdBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->insertShape(pos, res, style);
    return true;
  }
  MWAW_DEBUG_MSG(("FreeHandParser::sendShape: found some unexpected shape\n"));
  return false;
}

bool FreeHandParser::sendTextbox(FreeHandParserInternal::Textbox const &textbox, MWAWTransformation const &transform)
{
  textbox.m_isSent=true;
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendTextbox: can not find the listener\n"));
    return false;
  }
  MWAWGraphicStyle style(MWAWGraphicStyle::emptyStyle());
  MWAWTransformation finalTransformation=transform*textbox.m_transformation;
  MWAWTransformation transf;
  float rotation=0;
  MWAWBox2f box;
  if (decomposeMatrix(finalTransformation,rotation,transf,textbox.m_box.center())) {
    box=transf*textbox.m_box;
    style.m_rotate=rotation;
  }
  else
    box=finalTransformation*textbox.m_box;
  for (int c=0; c<2; ++c) {
    if (box.min()[c]>box.max()[c])
      std::swap(box.min()[c],box.max()[c]);
  }
  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  std::shared_ptr<MWAWSubDocument> doc(new FreeHandParserInternal::SubDocument(*this, getInput(), textbox.m_id));
  listener->insertTextBox(pos, doc, style);
  return false;
}

bool FreeHandParser::sendText(int zId)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendText: can not find the listener\n"));
    return false;
  }
  if (m_state->m_zIdToTextboxMap.find(zId)==m_state->m_zIdToTextboxMap.end()) {
    MWAW_DEBUG_MSG(("FreeHandParser::sendText: can not find the text shape\n"));
    return false;
  }
  auto &textbox=m_state->m_zIdToTextboxMap.find(zId)->second;
  MWAWParagraph para;
  para.m_justify = textbox.m_justify;
  listener->setParagraph(para);
  if (!textbox.m_text.valid())
    return true;
  MWAWInputStreamPtr input=getInput();
  input->seek(textbox.m_text.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Textbox[text]:";
  long endPos=textbox.m_text.end();
  int cPos=0;
  float deltaSpacing=textbox.m_spacings[0];
  while (!input->isEnd()) {
    if (input->tell()>=endPos)
      break;
    if (textbox.m_posToFontMap.find(cPos)!=textbox.m_posToFontMap.end()) {
      auto &font=textbox.m_posToFontMap.find(cPos)->second;
      if (font.m_nameId && m_state->m_zIdToStringMap.find(font.m_nameId)!= m_state->m_zIdToStringMap.end())
        font.m_font.setId(getParserState()->m_fontConverter->getId(m_state->m_zIdToStringMap.find(font.m_nameId)->second));
      // color
      if (font.m_colorId && m_state->m_zIdToColorMap.find(font.m_colorId) != m_state->m_zIdToColorMap.end())
        font.m_font.setColor(m_state->m_zIdToColorMap.find(font.m_colorId)->second);
      // spacing
      font.m_font.setDeltaLetterSpacing(deltaSpacing, librevenge::RVNG_POINT);
      // streching
      bool needStreching=false;
      if (textbox.m_scalings[1]<1||textbox.m_scalings[1]>1) {
        font.m_font.setSize(font.m_font.size()*textbox.m_scalings[1]);
        needStreching=true;
      }
      if ((needStreching || textbox.m_scalings[0]<1 || textbox.m_scalings[0]>1) && textbox.m_scalings[1]>0)
        font.m_font.setWidthStreching(textbox.m_scalings[0]/textbox.m_scalings[1]);
      listener->setFont(font.m_font);
      f << "[F]";
    }
    ++cPos;
    auto c = char(input->readULong(1));
    if (c==0) {
      MWAW_DEBUG_MSG(("FreeHandParser::sendText: find char 0\n"));
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
      listener->insertCharacter(static_cast<unsigned char>(c), input, endPos);
      break;
    }
  }
  ascii().addPos(textbox.m_text.begin());
  ascii().addNote(f.str().c_str());

  return true;
}

void FreeHandParser::flushExtra()
{
  bool first=true;
  for (auto it : m_state->m_zIdToShapeMap) {
    if (it.second.m_isSent) continue;
    if (first) {
      MWAW_DEBUG_MSG(("FreeHandParser::flushExtra: find some unused shape: %d\n", it.first));
      first=false;
    }
    sendZone(it.first, m_state->m_transform);
  }
  first=true;
  for (auto it : m_state->m_zIdToTextboxMap) {
    if (it.second.m_isSent) continue;
    if (first) {
      MWAW_DEBUG_MSG(("FreeHandParser::flushExtra: find some unsed textbox %d\n", it.first));
      first=false;
    }
    sendZone(it.first, m_state->m_transform);
  }
}

bool FreeHandParser::decomposeMatrix(MWAWTransformation const &matrix, float &rot, MWAWTransformation &transform, MWAWVec2f const &origCenter)
{
  // FIXME: this assumes that there is no skewing, ...
  MWAWVec3f const &yRow=matrix[1];
  if (matrix.isIdentity() || (yRow[0]>=0 && yRow[0]<=0)) return false;
  rot=std::atan2(yRow[0],-yRow[1]);
  rot *= float(180/M_PI);
  transform=MWAWTransformation::rotation(-rot, matrix * origCenter) * matrix;
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
