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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include <librevenge/librevenge.h>

#include "MWAWPresentationListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWSubDocument.hxx"

#include "PowerPoint3Parser.hxx"

#include "PowerPoint3OLE.hxx"

/** Internal: the structures of a PowerPoint3Parser */
namespace PowerPoint3ParserInternal
{
int swapUInt8(int v);
int swapBool8(int v);
int swapBool4UInt4(int v);
int swapUInt4Bool4(int v);
////////////////////////////////////////
//! swap an uint8_t by 4 offset
int swapUInt8(int v)
{
  return ((v>>4)|(v<<4)) & 0xFF;
}
//! swap a list of 8 bool
int swapBool8(int v)
{
  int val=0;
  for (int b=0, d1=1, d2=0x80; b<4; ++b, d1<<=1, d2>>=1) {
    if (v&d1) val|=d2;
    if (v&d2) val|=d1;
  }
  return val;
}
//! swap a list of 4bool and a int4
int swapBool4UInt4(int v)
{
  int val=0;
  for (int b=0, d1=1, d2=0x80; b<4; ++b, d1<<=1, d2>>=1) {
    if (v&d2) val|=d1;
  }
  val|=((v&3)<<6)|((v&0x0c)<<2);
  return val;
}
//! swap a list of a int4 and 4bool
int swapUInt4Bool4(int v)
{
  int val=0;
  for (int b=0, d1=1, d2=0x80; b<4; ++b, d1<<=1, d2>>=1) {
    if (v&d1) val|=d2;
  }
  val|=((v&0x30)>>2)|((v&0xc0)>>6);
  return val;
}

////////////////////////////////////////
//! Internal: virtual field parser of a PowerPoint3Parser
struct FieldParser {
  //! the constructor
  FieldParser(int fSize, std::string const &debugName)
    : m_fieldSize(fSize)
    , m_name(debugName)
  {
  }
  //! destructor
  virtual ~FieldParser();
  //! virtual function used to parse a field
  virtual bool parse(int id, MWAWInputStreamPtr &input, libmwaw::DebugFile &ascFile)=0;
  //! the field size
  int m_fieldSize;
  //! the debug name
  std::string m_name;
};

FieldParser::~FieldParser()
{
}
////////////////////////////////////////
//! Internal: a basic zone id parser of a PowerPoint3Parser
struct ListZoneIdParser final : public FieldParser {
  //! the constructor
  ListZoneIdParser(int numZones, std::string const &debugName)
    : FieldParser(4, debugName)
    , m_numZones(numZones)
    , m_fieldIdToZoneIdMap()
  {
  }
  //! virtual function used to parse a field
  bool parse(int id, MWAWInputStreamPtr &input, libmwaw::DebugFile &ascFile) final;
  //! the number of zones
  int m_numZones;
  //! map field id to zone id
  std::map<int,int> m_fieldIdToZoneIdMap;
};

bool ListZoneIdParser::parse(int id, MWAWInputStreamPtr &input, libmwaw::DebugFile &ascFile)
{
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << m_name << "[" << id << "]:pos,";
  auto zId=int(input->readULong(4));
  f << "Z" << zId;
  if (zId>=0 && zId<m_numZones)
    m_fieldIdToZoneIdMap[id]=zId;
  else {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::ListZoneIdParser::parse: find bad zone Z%d\n", zId));
    f << "###";
  }
  ascFile.addPos(pos-2);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////
//! Internal: a font name parser of a PowerPoint3Parser
struct FontNameFieldParser final : public FieldParser {
  //! the constructor
  explicit FontNameFieldParser(int numZones)
    : FieldParser(64, "FontName")
    , m_numZones(numZones)
    , m_idToNameMap()
    , m_childList()
  {
  }
  //! virtual function used to parse a field
  bool parse(int id, MWAWInputStreamPtr &input, libmwaw::DebugFile &ascFile) final;
  //! the number of zones
  int m_numZones;
  //! map file id to font name
  std::map<int, std::string> m_idToNameMap;
  //! the child list
  std::vector<int> m_childList;
};

bool FontNameFieldParser::parse(int id, MWAWInputStreamPtr &input, libmwaw::DebugFile &ascFile)
{
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << m_name << "[" << id << "]:";
  int val;
  for (int i=0; i<5; ++i) { // f4=400|700
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";;
  }
  val=int(input->readLong(1)); // 0|1|-1
  if (val) f << "f5=" << val << ",";
  for (int i=0; i<3; ++i) { // f7=0|2
    val=int(input->readLong(2));
    if (val) f << "f" << i+6 << "=" << val << ",";
  }
  val=int(input->readLong(1)); // [012][07]
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  std::string name;
  for (int i=0; i<32; ++i) {
    auto c=char(input->readULong(1));
    if (!c) break;
    name+=c;
  }
  f << name << ",";
  if (!name.empty()) m_idToNameMap[id]=name;
  input->seek(pos+50, librevenge::RVNG_SEEK_SET);
  auto zId=int(input->readULong(4));
  if (zId)
    f << "Z" << zId << ",";
  if (zId>0 && zId<m_numZones)
    m_childList.push_back(zId);
  else if (zId) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::ListZoneIdParser::parse: find bad zone Z%d\n", zId));
    f << "###";
  }
  for (int i=0; i<5; ++i) { // g0=0|1, g1,g2=small number g3=2048, g4=0
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  ascFile.addPos(pos-2);
  ascFile.addNote(f.str().c_str());
  return true;
}

//! Internal: a ruler
struct Ruler {
  //! constructor
  Ruler()
    : m_paragraph()
  {
    for (auto &margin : m_margins) margin=0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Ruler const &ruler)
  {
    o << "margins=[";
    for (int i=0; i<5; ++i)
      o << double(ruler.m_margins[2*i+1]-ruler.m_margins[2*i])/8 << ":" << double(ruler.m_margins[2*i])/8 << ",";
    o << "],";
    o << ruler.m_paragraph;
    return o;
  }
  //! returns a paragraph corresponding to a level
  void updateParagraph(MWAWParagraph &para, int level) const
  {
    if (level<0 || level>4) {
      MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::Ruler::updateParagraph: the level %d seems bad\n", level));
      level=0;
    }
    para.m_marginsUnit=librevenge::RVNG_POINT;
    para.m_margins[0]=double(m_margins[2*level+1]-m_margins[2*level+0])/8;
    para.m_margins[1]=double(m_margins[2*level+0])/8;
    para.m_tabs=m_paragraph.m_tabs;
  }
  //! the paragraph
  MWAWParagraph m_paragraph;
  //! the left/first margins * 5 (0: normal, 1-4: level)
  int m_margins[10];
};
//! Internal: a frame of a PowerPoint3Parser
struct Frame {
  //! constructor
  Frame()
    : m_type(-1)
    , m_formatId(-1)
    , m_dimension()
    , m_shapeId(-1)
    , m_customShapeId(-1)
    , m_customTransformation(0)
    , m_polygonId(-1)
    , m_pictureId(-1)
    , m_textId(-1)
    , m_mainTextBox(false)
    , m_style()
    , m_isSent(false)
  {
    for (auto &angle : m_angles) angle=0;
    for (auto &groupChild : m_groupChild) groupChild=-1;
  }
  //! the type: 0:line, 1:rect, 2: textbox, ...
  int m_type;
  //! the format id
  int m_formatId;
  //! the dimension
  MWAWBox2i m_dimension;
  //! the shape id: 1: oval, 2: rectOval, 3:rectangle
  int m_shapeId;
  //! the custom shape id
  int m_customShapeId;
  //! the custom transformation: 1:rot90, 2:rot180, 4:flipX
  int m_customTransformation;
  //! the polygon id
  int m_polygonId;
  //! the picture id
  int m_pictureId;
  //! the textzone id
  int m_textId;
  //! flag to know if the textbox is a place holder
  bool m_mainTextBox;
  //! the arc angles
  float m_angles[2];
  //! the group limit
  int m_groupChild[2];
  //! the style
  MWAWGraphicStyle m_style;
  //! flag to know if a frame is sent
  mutable bool m_isSent;
};
//! Internal: a polygon of a PowerPoint3Parser
struct Polygon {
  //! constructor
  Polygon()
    : m_type(0)
    , m_box()
    , m_vertices()
  {
  }
  //! update the shape
  bool updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const;
  //! the polygon type
  int m_type;
  //! the bdbox
  MWAWBox2i m_box;
  //! the list of points
  std::vector<MWAWVec2f> m_vertices;
};

bool Polygon::updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const
{
  if (m_vertices.empty()) return false;
  MWAWBox2f actBox(m_vertices[0],m_vertices[0]);
  for (size_t i=1; i<m_vertices.size(); ++i) actBox=actBox.getUnion(MWAWBox2f(m_vertices[i],m_vertices[i]));
  float factor[2], decal[2];
  for (int i=0; i<2; ++i) {
    if (actBox.size()[i]<0||actBox.size()[i]>0)
      factor[i]=finalBox.size()[i]/actBox.size()[i];
    else
      factor[i]=1.f;
    decal[i]=finalBox[0][i]-factor[i]*actBox[0][i];
  }
  shape.m_type = MWAWGraphicShape::Polygon;
  for (auto const &pt : m_vertices)
    shape.m_vertices.push_back(MWAWVec2f(decal[0]+factor[0]*pt[0], decal[1]+factor[1]*pt[1]));
  if (m_type==1) shape.m_vertices.push_back(shape.m_vertices[0]);
  return true;
}

//! a scheme of a PowerPoint3Parser
struct Scheme {
  //! the color: back, foreground, accents
  MWAWColor m_colors[8];
};
//! Internal: the third zone defining a slide of a PowerPoint3Parser
struct SlideFormat {
  //! constructor
  SlideFormat()
    : m_margins(0,0)
    , m_gradientOffset(0)
    , m_shadowOffset(0,0)
  {
  }
  //! the left/right and top/bottom margins
  MWAWVec2i m_margins;
  //! the color gradient offset: -10 means black, 10 means white
  int m_gradientOffset;
  //! the shadow offset
  MWAWVec2i m_shadowOffset;
};

//! Internal: a text zone of a PowerPoint3Parser
struct TextZone {
  //! constructor
  TextZone()
    : m_rulerId(-1)
    , m_box()
    , m_text()
    , m_fonts()
    , m_rulers()
    , m_centered(false)
    , m_wrapText(false)
    , m_adjustSize(false)
  {
  }
  //! return true if the zone has no text
  bool empty() const
  {
    return !m_text.valid();
  }
  //! the ruler id
  int m_rulerId;
  //! the bdbox
  MWAWBox2i m_box;
  //! the text entry
  MWAWEntry m_text;
  //! the fonts entry
  MWAWEntry m_fonts;
  //! the ruler entry
  MWAWEntry m_rulers;
  //! force horizontal centered
  bool m_centered;
  //! wrap the text
  bool m_wrapText;
  //! adjust the textbox size
  bool m_adjustSize;
};

//! Internal: a slide of a PowerPoint3Parser
struct SlideContent {
  //! constructor
  SlideContent()
    : m_useMasterPage(false)
    , m_numMainZones(0)
    , m_textZone()
    , m_frameList()
    , m_formatList()
    , m_polygonList()
    , m_schemeId(-1)
  {
    for (auto &id : m_mainZoneIds) id=-1;
  }
  //! return true if the zone has text
  bool hasText() const
  {
    for (auto const &zone : m_textZone) {
      if (!zone.empty()) return true;
    }
    return false;
  }
  //! a flag to know if we need to use the master page
  bool m_useMasterPage;
  //! the number of title/body zones
  int m_numMainZones;
  //! the title/body position
  int m_mainZoneIds[2];
  //! the textzone
  std::vector<TextZone> m_textZone;
  //! the list of frames
  std::vector<Frame> m_frameList;
  //! the format list
  std::vector<SlideFormat> m_formatList;
  //! the list of polygons
  std::vector<Polygon> m_polygonList;
  //! the scheme id
  int m_schemeId;
};
//! Internal: a slide of a PowerPoint3Parser
struct Slide {
  //! constructor
  Slide()
  {
    for (auto &id : m_contentIds) id=-1;
  }
  //! the slide content ids: slide and not
  int m_contentIds[2];
};

////////////////////////////////////////
//! Internal: the state of a PowerPoint3Parser
struct State {
  //! constructor
  State()
    : m_isMacFile(true)
    , m_fontFamily("CP1252")
    , m_oleParser()
    , m_zoneListBegin(0)
    , m_zonesList()
    , m_slidesIdList()
    , m_idToSlideMap()
    , m_idToSlideContentMap()
    , m_idToSchemeMap()
    , m_pictIdToZoneIdMap()
    , m_idToPictureContentMap()
    , m_origin(0,0)
    , m_idToUserColorMap()
    , m_idToFontIdMap()
    , m_idToRulerMap()
    , m_monoTypeFontId(-1)
    , m_badEntry()
  {
    for (auto &id : m_printInfoIds) id=-1;
    for (auto &id : m_zoneIds) id=-1;
  }
  //! try to return a zone
  MWAWEntry const &getZoneEntry(int id) const
  {
    if (id==-1) return m_badEntry;
    if (id<0||size_t(id)>=m_zonesList.size()) {
      MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::State::getZone: can find entry with id=%d\n", id));
      return m_badEntry;
    }
    return m_zonesList[size_t(id)];
  }
  //! try to return a pattern
  bool getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const;
  //! returns a custom shape corresponding to an id
  static bool getCustomShape(int id, MWAWGraphicShape &shape);
  //! flag to know if the file is a mac file or a pc file
  bool m_isMacFile;
  //! the basic pc font family if known
  std::string m_fontFamily;
  //! the ole parser
  std::shared_ptr<PowerPoint3OLE> m_oleParser;
  //! the begin position of the list of zones
  long m_zoneListBegin;
  //! the list of zone entries
  std::vector<MWAWEntry> m_zonesList;
  //! the main list of slides id
  std::vector<int> m_slidesIdList;
  //! a map zoneId to slide
  std::map<int, Slide> m_idToSlideMap;
  //! a map zoneId to slide content
  std::map<int, SlideContent> m_idToSlideContentMap;
  //! a map between schemeId and scheme
  std::map<int, Scheme> m_idToSchemeMap;
  //! a map pictId to picture zone
  std::map<int, int> m_pictIdToZoneIdMap;
  //! a map zoneId to picture object
  std::map<int, MWAWEmbeddedObject> m_idToPictureContentMap;
  //! the origin
  MWAWVec2i m_origin;
  //! a map between colorId and user color
  std::map<int, MWAWColor> m_idToUserColorMap;
  //! a map between file id and font id
  std::map<int, int> m_idToFontIdMap;
  //! a map between id and paragraph
  std::map<int, Ruler> m_idToRulerMap;
  //! the printInfo id
  int m_printInfoIds[2];
  //! the sequential zones id
  int m_zoneIds[13];
  //! the monotype font id
  int m_monoTypeFontId;
  //! an entry used by getZoneEntry if it does not find the zone
  MWAWEntry m_badEntry;
};

bool State::getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const
{
  // normally between 1 and 32 but find a pattern resource with 38 patterns
  if (id<=0 || id>=39) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::State::getPattern: unknown id=%d\n", id));
    return false;
  }
  static uint16_t const values[] = {
    0xffff, 0xffff, 0xffff, 0xffff, 0x0, 0x0, 0x0, 0x0,
    0xddff, 0x77ff, 0xddff, 0x77ff, 0x8000, 0x800, 0x8000, 0x800,
    0xdd77, 0xdd77, 0xdd77, 0xdd77, 0x8800, 0x2200, 0x8800, 0x2200,
    0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x8822, 0x8822, 0x8822, 0x8822,
    0x8844, 0x2211, 0x8844, 0x2211, 0x1122, 0x4488, 0x1122, 0x4488,
    0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xff00, 0xff00, 0xff00, 0xff00,
    0x81c0, 0x6030, 0x180c, 0x603, 0x8103, 0x60c, 0x1830, 0x60c0,
    0x8888, 0x8888, 0x8888, 0x8888, 0xff00, 0x0, 0xff00, 0x0,
    0xb130, 0x31b, 0xd8c0, 0xc8d, 0x8010, 0x220, 0x108, 0x4004,
    0xff80, 0x8080, 0x8080, 0x8080, 0xff88, 0x8888, 0xff88, 0x8888,
    0xff80, 0x8080, 0xff08, 0x808, 0xeedd, 0xbb77, 0xeedd, 0xbb77,
    0x7fff, 0xffff, 0xf7ff, 0xffff, 0x88, 0x4422, 0x1100, 0x0,
    0x11, 0x2244, 0x8800, 0x0, 0x8080, 0x8080, 0x808, 0x808, 0xf000,
    0x0, 0xf00, 0x0, 0x8142, 0x2418, 0x8142, 0x2418,
    0x8000, 0x2200, 0x800, 0x2200, 0x1038, 0x7cfe, 0x7c38, 0x1000,
    0x102, 0x408, 0x1824, 0x4281, 0xc1e0, 0x7038, 0x1c0e, 0x783,
    0x8307, 0xe1c, 0x3870, 0xe0c1, 0xcccc, 0xcccc, 0xcccc, 0xcccc,
    0xffff, 0x0, 0xffff, 0x0, 0xf0f0, 0xf0f0, 0xf0f, 0xf0f,
    0x6699, 0x9966, 0x6699, 0x9966, 0x8142, 0x2418, 0x1824, 0x4281,
  };
  pattern.m_dim=MWAWVec2i(8,8);
  uint16_t const *ptr=&values[4*(id-1)];
  pattern.m_data.resize(8);
  for (size_t i=0; i < 4; ++i, ++ptr) {
    pattern.m_data[2*i]=static_cast<unsigned char>((*ptr)>>8);
    pattern.m_data[2*i+1]=static_cast<unsigned char>((*ptr)&0xff);
  }
  return true;
}

bool State::getCustomShape(int id, MWAWGraphicShape &shape)
{
  int N=4;
  double const *vertices=nullptr;
  switch (id) {
  case 0: {
    static double const v[]= {0.5,1, 1,0.5, 0.5,0, 0,0.5 };
    vertices=v;
    break;
  }
  case 1: {
    N=3;
    static double const v[]= {0,1, 1,1, 0.5,0};
    vertices=v;
    break;
  }
  case 2: {
    N=3;
    static double const v[]= {0,1, 1,1, 0,0};
    vertices=v;
    break;
  }
  case 3: {
    static double const v[]= {0,1, 0.7,1, 1,0, 0.3,0 };
    vertices=v;
    break;
  }
  case 4: {
    static double const v[]= {0,1, 0.3,0, 0.7,0, 1,1 };
    vertices=v;
    break;
  }
  case 5: {
    N=6;
    static double const v[]= {0,0.5, 0.2,1, 0.8,1, 1,0.5, 0.8,0, 0.2,0};
    vertices=v;
    break;
  }
  case 6: {
    N=8;
    static double const v[]= {0,0.3, 0,0.7, 0.3,1, 0.7,1, 1,0.7, 1,0.3, 0.7,0, 0.3,0};
    vertices=v;
    break;
  }
  case 7: {
    N=12;
    static double const v[]= {0,0.2, 0,0.8, 0.2,0.8, 0.2,1,
                              0.8,1, 0.8,0.8, 1,0.8, 1,0.2,
                              0.8,0.2, 0.8,0, 0.2,0, 0.2,0.2
                             };
    vertices=v;
    break;
  }
  case 8: {
    N=10;
    static double const v[]= {0.5,0, 0.383,0.383, 0,0.383, 0.3112,0.62,
                              0.1943,1, 0.5,0.78, 0.8056,1, 0.688,0.62,
                              1,0.3822, 0.6167,0.3822,
                             };
    vertices=v;
    break;
  }
  case 9: {
    N=7;
    static double const v[]= {0,0.333, 0,0.666, 0.7,0.666, 0.7,1,
                              1,0.5, 0.7,0, 0.7,0.333
                             };
    vertices=v;
    break;
  }
  case 10: {
    N=7;
    static double const v[]= {0,0.2, 0,0.8, 0.7,0.8, 0.7,1,
                              1,0.5, 0.7,0, 0.7,0.2
                             };
    vertices=v;
    break;
  }
  case 11: {
    N=5;
    static double const v[]= {0,0, 0,1, 0.7,1, 1,0.5, 0.7,0};
    vertices=v;
    break;
  }
  case 12: {
    N=12;
    static double const v[]= {0,1, 0.8,1, 1,0.8, 1,0,
                              0.8,0.2, 0.8,1, 0.8,0.2, 0,0.2,
                              0.2,0., 1,0, 0.2,0, 0,0.2
                             };

    vertices=v;
    break;
  }
  case 13: {
    N=11;
    static double const v[]= {0,0.1, 0,0.8, 0.1,0.9, 0.2,0.9,
                              0.1,1, 0.3,0.9, 0.9,0.9, 1,0.8,
                              1,0.1, 0.9,0, 0.1,0
                             };
    vertices=v;
    break;
  }
  case 14: {
    N=24;
    static double const v[]= { 0.5,0, 0.55,0.286, 0.7465,0.07, 0.656,0.342,
                               0.935,0.251, 0.7186,0.4465, 1,0.5, 0.7186,0.5535,
                               0.935,0.75, 0.6558,0.66558, 0.7465,0.9349, 0.558,0.7186,
                               0.495,1, 0.44,0.7186, 0.2511,0.935, 0.3418,0.6627,
                               0.063,0.7535, 0.279,0.558, 0,0.502, 0.279,0.4465,
                               0.063,0.2511, 0.3418,0.3418, 0.2511,0.069, 0.4395,0.286
                             };
    vertices=v;
    break;
  }
  default:
    break;
  }
  if (N<=0 || !vertices) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::State::getCustomShape: unknown id %d\n", id));
    return false;
  }
  shape.m_type = MWAWGraphicShape::Polygon;
  shape.m_vertices.resize(size_t(N+1));
  for (int i=0; i<N; ++i)
    shape.m_vertices[size_t(i)]=MWAWVec2f(float(vertices[2*i]),float(vertices[2*i+1]));
  shape.m_vertices[size_t(N)]=MWAWVec2f(float(vertices[0]),float(vertices[1]));
  return true;
}

////////////////////////////////////////
//! Internal: the subdocument of a PowerPointParser
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor for text
  SubDocument(PowerPoint3Parser &pars, MWAWInputStreamPtr const &input, SlideContent const *slide, int tId, bool mainZone, bool master)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_slide(slide)
    , m_textId(tId)
    , m_mainTextBox(mainZone)
    , m_isMaster(master)
  {
  }
  //! constructor for note
  SubDocument(PowerPoint3Parser &pars, MWAWInputStreamPtr const &input, SlideContent const *slide)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_slide(slide)
    , m_textId(-1)
    , m_mainTextBox(false)
    , m_isMaster(false)
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
    if (m_slide != sDoc->m_slide) return true;
    if (m_textId != sDoc->m_textId) return true;
    if (m_mainTextBox != sDoc->m_mainTextBox) return true;
    if (m_isMaster != sDoc->m_isMaster) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the slide
  SlideContent const *m_slide;
  //! the text id
  int m_textId;
  //! flag to know if we send the title or the body
  bool m_mainTextBox;
  //! flag to know if we send a master text zone
  bool m_isMaster;
private:
  SubDocument(SubDocument const &) = delete;
  SubDocument &operator=(SubDocument const &) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<PowerPoint3Parser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  if (!m_slide) {
    MWAW_DEBUG_MSG(("PowerPoint3ParserInternal::SubDocument::parse: no slide zone\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendText(*m_slide, m_textId, m_mainTextBox, m_isMaster);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint3Parser::PowerPoint3Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWPresentationParser(input, rsrcParser, header)
  , m_state(new PowerPoint3ParserInternal::State)
{
  setAsciiName("main-1");
}

PowerPoint3Parser::~PowerPoint3Parser()
{
}

bool PowerPoint3Parser::getColor(int colorId, int schemeId, MWAWColor &color) const
{
  // if scheme is defined, we must use it for 0<=colorId<8
  if (schemeId>=0 && colorId>=0 && colorId<8 && m_state->m_idToSchemeMap.find(schemeId)!=m_state->m_idToSchemeMap.end()) {
    color=m_state->m_idToSchemeMap.find(schemeId)->second.m_colors[colorId];
    return true;
  }
  if (m_state->m_idToUserColorMap.find(colorId)!=m_state->m_idToUserColorMap.end()) {
    color=m_state->m_idToUserColorMap.find(colorId)->second;
    return true;
  }
  if (schemeId!=0) { // seems to happens in the master slide
    MWAW_DEBUG_MSG(("PowerPoint3Parser::getColor: can not find color=%d in scheme=%d\n", colorId, schemeId));
  }
  return false;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void PowerPoint3Parser::parse(librevenge::RVNGPresentationInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendSlides();
    }

#ifdef DEBUG
    checkForUnparsedZones();
    if (m_state->m_oleParser)
      m_state->m_oleParser->checkForUnparsedStream();
#endif
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetPresentationListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void PowerPoint3Parser::createDocument(librevenge::RVNGPresentationInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getPresentationListener()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::createDocument: listener already exist\n"));
    return;
  }

  std::vector<MWAWPageSpan> pageList;
  for (auto id : m_state->m_slidesIdList) {
    MWAWPageSpan ps(getPageSpan());
    if (m_state->m_idToSlideMap.find(id)!=m_state->m_idToSlideMap.end()) {
      int cId=m_state->m_idToSlideMap.find(id)->second.m_contentIds[0];
      if (m_state->m_idToSlideContentMap.find(cId)!=m_state->m_idToSlideContentMap.end()) {
        auto const &slide=m_state->m_idToSlideContentMap.find(cId)->second;
        if (slide.m_useMasterPage && m_state->m_zoneIds[2]>=0)
          ps.setMasterPageName(librevenge::RVNGString("Master"));
        MWAWColor backColor;
        if (getColor(0, slide.m_schemeId, backColor))
          ps.setBackgroundColor(backColor);
      }
    }
    pageList.push_back(ps);
  }

  //
  MWAWPresentationListenerPtr listen(new MWAWPresentationListener(*getParserState(), pageList, documentInterface));
  setPresentationListener(listen);
  if (m_state->m_oleParser) {
    librevenge::RVNGPropertyList metaData;
    m_state->m_oleParser->updateMetaData(metaData);
    listen->setDocumentMetaData(metaData);
  }
  listen->startDocument();
}


////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

// create the different zones
bool PowerPoint3Parser::createZones()
{
  MWAWInputStreamPtr input=getInput();
  if (!input) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::createZones: can not find the main input\n"));
    return false;
  }
  bool const isMacFile=m_state->m_isMacFile;

  std::shared_ptr<PowerPoint3OLE> oleParser;
  if (input->isStructured()) {
    MWAWInputStreamPtr mainOle=input->getSubStreamByName("PP40");
    if (!mainOle) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::createZones: can not find the main ole\n"));
      return false;
    }
    int fId=3;
    if (!isMacFile)
      // create a temporary font, to use a CP1252 encoding
      fId=getFontConverter()->getId("CP1252");
    oleParser.reset(new PowerPoint3OLE(input, version(), getFontConverter(), fId));
    oleParser->parse();
    int encoding=oleParser->getFontEncoding();
    if (!isMacFile && encoding>=1250 && encoding<=1258) {
      std::stringstream s;
      s << "CP" << encoding;
      m_state->m_fontFamily=s.str();
    }
    getParserState()->m_input=input=mainOle;
    input->setReadInverted(!isMacFile);
  }
  // create the asciiFile
  ascii().setStream(input);
  ascii().open(asciiName());
  if (!checkHeader(nullptr)) return false;
  m_state->m_oleParser=oleParser;
  int docInfo;
  if (!readListZones(docInfo)) return false;
  size_t numZones=m_state->m_zonesList.size();
  if (docInfo<0 || docInfo>=int(numZones) || !readDocInfo(m_state->m_zonesList[size_t(docInfo)])) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::createZones: can not find the document info zone\n"));
    return false;
  }
  // first try to read the font name and scheme
  libmwaw::DebugStream f;
  for (int w=0; w<3; ++w) {
    int id=w==0 ? 11 : w==1 ? 7 : 8;
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_zoneIds[id]);
    if (!entry.valid()) continue;
    if (w==2) {
      readColorZone(entry);
      continue;
    }
    if (w==0 && !isMacFile) {
      PowerPoint3ParserInternal::FontNameFieldParser parser(static_cast<int>(numZones));
      readStructList(entry, parser);
      for (auto it : parser.m_idToNameMap) {
        m_state->m_idToFontIdMap[it.first]=it.first;
        /* FIXME: by default, we force the family to be CP1252,
         but we may want to use the file/font encoding */
        getFontConverter()->setCorrespondance(it.first, it.second, it.second=="Monotype Sorts" || it.second=="Wingdings" ? "" : m_state->m_fontFamily);
      }
      for (auto cId : parser.m_childList) {
        MWAWEntry const &cEntry=m_state->getZoneEntry(cId);
        if (!cEntry.valid() || cEntry.isParsed()) continue;
        cEntry.setParsed(true);
        f.str("");
        f << "Entries(FontDef)" << "[Z" << cEntry.id() << "]:";
        ascii().addPos(cEntry.begin());
        ascii().addNote(f.str().c_str());
        ascii().addPos(cEntry.end());
        ascii().addNote("_");
      }
      continue;
    }
    PowerPoint3ParserInternal::ListZoneIdParser parser(int(numZones), w==0 ? "FontName" : "Scheme");
    if (!readStructList(entry, parser)) continue;
    if (w==0) {
      readFontNamesList(parser.m_fieldIdToZoneIdMap);
      continue;
    }
    for (auto it : parser.m_fieldIdToZoneIdMap) {
      MWAWEntry const &cEntry=m_state->getZoneEntry(it.second);
      if (!cEntry.valid() || cEntry.isParsed()) continue;
      readScheme(cEntry, it.first);
    }
  }
  for (int i=0; i<13; ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_zoneIds[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    bool done=true;
    switch (i) {
    case 0:
      readDocRoot(entry);
      break;
    case 1:
    case 2: // master
    case 3: {
      PowerPoint3ParserInternal::Slide slide;
      readSlide(entry, slide, i);
      break;
    }
    case 5:
      readPictureMain(entry);
      break;
    case 6: {
      PowerPoint3ParserInternal::ListZoneIdParser parser(int(numZones), "Ruler");
      if (readStructList(entry, parser)) {
        for (auto it : parser.m_fieldIdToZoneIdMap) {
          MWAWEntry const &cEntry=m_state->getZoneEntry(it.second);
          if (!cEntry.valid() || cEntry.isParsed()) continue;
          readRuler(cEntry, it.first);
        }
      }
      break;
    }
    case 9:
      readZone9(entry);
      break;
    case 10:
      readZone10(entry);
      break;
    default:
      done=false;
    }
    if (done) continue;
    entry.setParsed(true);
    f.str("");
    f << "Entries(Zone" << i << "A)[Z" << entry.id() << "]:";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }
  for (auto id : m_state->m_slidesIdList) {
    MWAWEntry const &entry=m_state->getZoneEntry(id);
    if (!entry.valid() || entry.isParsed()) continue;
    PowerPoint3ParserInternal::Slide slide;
    readSlide(entry, slide, -1);
  }
  for (int i=0; i<2; ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_printInfoIds[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    if (isMacFile && i==0)
      readPrintInfo(entry);
    else {
      entry.setParsed(true);
      f.str("");
      f << "Entries(PrintInfo" << i << ")[Z" << entry.id() << "]:";
      ascii().addPos(entry.begin());
      ascii().addNote(f.str().c_str());
      ascii().addPos(entry.end());
      ascii().addNote("_");
    }
  }
  return !m_state->m_slidesIdList.empty();
}

bool PowerPoint3Parser::readListZones(int &docInfoId)
{
  docInfoId=-1;
  MWAWInputStreamPtr input=getInput();
  libmwaw::DebugStream f;
  f << "Entries(ListZones):";
  long pos=input->tell();
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (!input->checkPosition(m_state->m_zoneListBegin+N*8)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readListZones: the number of zones seems bad\n"));
    f << "###zone";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  auto val=int(input->readULong(2)); // always 4
  if (val!=4) f << "f0=" << val << ",";
  auto endPos=long(input->readULong(4));
  if (!input->checkPosition(endPos) || input->checkPosition(endPos+1)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readListZones: the endPos seems bad\n"));
    f << "###endPos=" << std::hex << endPos << std::dec << ",";
  }
  val=int(input->readULong(2)); // find a|10
  if (val) f << "f1=" << val << ",";
  docInfoId=int(input->readULong(2));
  if (docInfoId) f << "docInfo=Z" << docInfoId << ",";
  if (input->tell()!=m_state->m_zoneListBegin)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(m_state->m_zoneListBegin, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "ListZones:zones=[";
  m_state->m_zonesList.resize(size_t(N));
  std::set<long> posList;
  for (int i=0; i<N; ++i) {
    unsigned long length=input->readULong(4);
    auto begin=long(input->readULong(4));
    if (length&0x80000000) {
      f << "*";
      length&=0x7FFFFFFF;
    }
    if (length&0x40000000) {
      f << "@";
      length&=0xBFFFFFFF;
    }
    if (length==0) {
      f << "_,";
      continue;
    }
    if (begin+long(length)<=begin || !input->checkPosition(begin+long(length))) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readListZones: a zone seems bad\n"));
      f << std::hex << begin << ":" << begin+long(length) << std::dec << "###,";
      continue;
    }
    MWAWEntry &zone=m_state->m_zonesList[size_t(i)];
    zone.setBegin(begin);
    zone.setLength(long(length));
    zone.setId(i);
    posList.insert(begin);
    posList.insert(zone.end());
    f << std::hex << begin << ":" << begin+long(length) << std::dec << ",";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  // check that the zones do not overlap
  for (size_t i=0; i<m_state->m_zonesList.size(); ++i) {
    MWAWEntry &zone=m_state->m_zonesList[size_t(i)];
    if (!zone.valid()) continue;
    auto it=posList.find(zone.begin());
    bool ok=it!=posList.end();
    if (ok) {
      if (++it==posList.end() || *it!=zone.end())
        ok=false;
    }
    if (ok) continue;
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readListZones: the zone %d overlaps with other zones\n", int(i)));
    m_state->m_zonesList[size_t(i)]=MWAWEntry();
  }
  ascii().addPos(input->tell());
  ascii().addNote("_");
  return true;
}

void PowerPoint3Parser::checkForUnparsedZones()
{
  // check if there remains some unparsed zone
  for (auto const &entry : m_state->m_zonesList) {
    if (!entry.valid() || entry.isParsed()) continue;
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("PowerPoint3Parser::checkForUnparsedZones: find some unknown zone\n"));
    }
    libmwaw::DebugStream f;
    f << "Entries(UnknZone)[Z" << entry.id() << "]:";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }
}

////////////////////////////////////////////////////////////
// try to read the different zones
////////////////////////////////////////////////////////////
bool PowerPoint3Parser::readDocInfo(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input=getInput();
  int const vers=version();
  if (entry.length()!=(vers<=3 ? 142 : 146)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: the entry %d seems bad\n", entry.id()));
    return false;
  }
  entry.setParsed(true);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(DocInfo)[Z" << entry.id() << "]:";
  int val;
  auto numZones=int(m_state->m_zonesList.size());
  f << "unkn=[";
  for (int i=0; i<4; ++i) { // list of 0 or big number (multiple of 12?)
    val=int(input->readLong(2));
    if (val) f << float(val)/12.f << ",";
    else f << "_,";
  }
  f << "],";
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(2));
  if (!m_state->m_isMacFile) {
    std::swap(dim[0],dim[1]);
    std::swap(dim[2],dim[3]);
  }
  MWAWBox2i pageBox(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
  f << "dim[page]=" << pageBox << ",";
  for (auto &d : dim) d=int(input->readLong(2));
  if (!m_state->m_isMacFile) {
    std::swap(dim[0],dim[1]);
    std::swap(dim[2],dim[3]);
  }
  MWAWBox2i paperBox=MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  f << "dim[paper]=" << paperBox << ",";
  MWAWVec2i paperSize = paperBox.size();
  MWAWVec2i pageSize = pageBox.size();
  // basic check
  if (pageSize.x()+pageSize.y() > paperSize.x()+paperSize.y()) {
    // checkme: is page/paper order inverted in mac file?
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: swap page and paper box\n"));
    std::swap(pageBox, paperBox);
    std::swap(pageSize, paperSize);
    f << "##paper/page,";
  }
  if (pageSize.x() > paperSize.x() || pageSize.y() > paperSize.y()) {
    // checkme: rare happened on one file found on internet, related to page orientation?
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: swap paper x/y\n"));
    paperBox=MWAWBox2i(MWAWVec2i(paperBox[0][1],paperBox[0][0]),MWAWVec2i(paperBox[1][1],paperBox[1][0]));
    paperSize = paperBox.size();
    f << "##paperXY,";
  }
  m_state->m_origin=-1*paperBox[0];
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0 ||
      pageSize.x() > paperSize.x() || pageSize.y() > paperSize.y()) {
    f << "###,";
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: the page dimension seems bad\n"));
  }
  else {
    // checkme, maybe better to define a slide with pageSize and no margins
    getPageSpan().setFormOrientation(MWAWPageSpan::PORTRAIT);
    if (pageBox[0][1]>=paperBox[0][1])
      getPageSpan().setMarginTop(double(pageBox[0][1]-paperBox[0][1])/576.0);
    if (pageBox[1][1]<=paperBox[1][1])
      getPageSpan().setMarginBottom(double(paperBox[1][1]-pageBox[1][1])/576.0);
    if (pageBox[0][0]>=paperBox[0][0])
      getPageSpan().setMarginLeft(double(pageBox[0][0]-paperBox[0][0])/576.0);
    if (pageBox[1][0]<=paperBox[1][0])
      getPageSpan().setMarginRight(double(paperBox[1][0]-pageBox[1][0])/576.0);
    getPageSpan().setFormLength(double(paperSize.y())/576.0);
    getPageSpan().setFormWidth(double(paperSize.x())/576.0);
  }
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
  val=int(input->readLong(2));
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  for (int i=0; i<5; ++i) {
    m_state->m_zoneIds[i]=int(input->readULong(4));
    f << "zone[id" << i << "]=Z" <<  m_state->m_zoneIds[i] << ",";
    if (m_state->m_zoneIds[i]>=numZones) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: the zoneId %d seems bad\n", m_state->m_zoneIds[i]));
      f << "###";
      m_state->m_zoneIds[i]=-1;
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo-2:";
  for (int i=0; i<2; ++i) dim[i]=int(input->readLong(2));
  f << "dim?=" << MWAWVec2i(dim[1],dim[0]) << ",";
  val=int(input->readLong(2)); // 1,2,8
  if (val) f << "f0=" << val << ",";
  val=int(input->readULong(2)); // big number
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  f << "unk=[";
  for (int i=0; i<5; ++i) { // 1,2,3,2|3|4,0|3|4
    val=int(input->readLong(2));
    f << val << ",";
  }
  f << "],";
  for (int i=0; i<2; ++i) { // f1=big number, f2=0
    val=int(input->readULong(1));
    if (val) f << "fl" << i+1 << "=" << std::hex << val << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo-3:";
  f << "zones=[";
  for (int i=0; i<8; ++i) {
    // 0: picture zones, 1: picture pos?, 2: some style?,
    long id=input->readLong(4);
    if (id==0 || id==-1)
      f << "_,";
    else if (id>0 && id<numZones) {
      f << "Z" << id << ",";
      m_state->m_zoneIds[i+5]=int(id);
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: find odd zone\n"));
      f << "###" << id << ",";
    }
  }
  f << "],";
  for (auto &d : dim) d=int(input->readULong(2));
  f << "page=" << MWAWVec2i(dim[0],dim[1]) << ",";
  f << "dim?=" << MWAWVec2i(dim[3],dim[2]) << ","; // frame, slide dim?
  for (int i=0; i<2; ++i) { // f2=1, f3=0
    val=int(input->readLong(2));
    if (val!=1-i)
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) {
    m_state->m_printInfoIds[i]= int(input->readLong(4));
    if (m_state->m_printInfoIds[i]==-1) continue;
    f << "printInfo[id" << i << "]=Z" << m_state->m_printInfoIds[i] << ",";
    if (m_state->m_printInfoIds[i]>=numZones) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocInfo: the printInfoId %d seems bad\n", m_state->m_printInfoIds[i]));
      f << "###";
      m_state->m_printInfoIds[i]=-1;
    }
  }
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(i<2 ? 4 : 2));
    int const expected[]= {10000, 7500, -2};
    if (val!=expected[i])
      f << "g" << i << "=" << val << ",";
  }
  if (vers<=3) {
    for (int i=0; i<2; ++i) { // two big number
      val=int(input->readULong(2));
      if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
    }
    val=int(input->readLong(2)); // 3|4
    if (val) f << "g3=" << val << ",";
  }
  else {
    for (int i=0; i<5; ++i) { // three big number
      val=int(input->readULong(2));
      if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  return true;
}

bool PowerPoint3Parser::readPrintInfo(MWAWEntry const &entry)
{
  if (entry.length() != 0x78) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readPrintInfo: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  // print info
  libmwaw::PrinterInfo info;
  if (!info.read(input)) return false;
  f << "Entries(PrintInfo)[Z"<< entry.id() << "]:" << info;

  // this is the final paper, so let ignore this
  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) return false;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(entry.end());
  ascii().addNote("_");

  return true;
}

bool PowerPoint3Parser::readDocRoot(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()!=22) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocRoot: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(DocRoot)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<2; ++i) { // fl0=[359][4c], fl1=0|c
    val=int(input->readULong(1));
    if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<2; ++i) { // big numbers, maybe a int32t
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto numZones=int(m_state->m_zonesList.size());
  val=int(input->readULong(4));
  int child=-1;
  if (val) {
    if (val>=0 && val<numZones) {
      f << "slideList[id]=Z" << val << ",";
      child=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocRoot: find some bad child\n"));
      f << "slideList[id]=##Z" << val << ",";
    }
  }
  for (int i=0; i<2; ++i) { // f2=1 (actual slide?)
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==1)
      f << "num[slides]=" << val << ",";
    else
      f << "f2=" << val << ",";
  }
  val=int(input->readULong(4)); // 257-298
  if (val) f << "f3=" << val << ",";
  for (int i=0; i<2; ++i) { // big numbers, maybe a int32t
    val=int(input->readLong(2));
    if (val) f << "f" << i+4 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  MWAWEntry const &cEntry=m_state->getZoneEntry(child);
  if (cEntry.valid() && !cEntry.isParsed())
    readSlidesList(cEntry);
  else {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readDocRoot: can not find the slide list...\n"));
    return false;
  }
  return true;
}

bool PowerPoint3Parser::readScheme(MWAWEntry const &entry, int id)
{
  int const vers=version();
  if (!entry.valid() || entry.length()<(vers<=3 ? 94 : 118)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readScheme: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  PowerPoint3ParserInternal::Scheme scheme;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Scheme)[Z"<< entry.id() << "]:S" << id << ",";
  int val;
  val=int(input->readLong(2)); // -2|-6
  if (val!=-2) f << "f0=" << val << ",";
  if (vers>=4) {
    for (int i=0; i<12; ++i) {
      val=int(input->readLong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
  }
  for (int i=0; i<14; ++i) { // f2=X, f3=X|0, f4,f5,f6:small number, f7=0|1|101, f8=0|1, f9=0|e1c,f10=0|3715,f11=0|80
    val=int(input->readLong(2));
    int const expected[]= {0,0,0,100,100,100, 0x101, 0, 0, 0, 0, 0, 7, 0};
    if (val!=expected[i])
      f << "f" << i+1 << "=" << val << ",";
  }
  f << "colors=[";
  for (auto &color : scheme.m_colors) {
    val=int(input->readULong(2));
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    color=MWAWColor(col[0],col[1],col[2]);
    f << color << ":" << val << ",";
  }
  f << "],";
  if (m_state->m_idToSchemeMap.find(id)!=m_state->m_idToSchemeMap.end()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readScheme: oops, scheme S%d is already defined\n", id));
  }
  else
    m_state->m_idToSchemeMap[id]=scheme;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readScheme: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Scheme:##extra");
  }
  return true;
}

bool PowerPoint3Parser::readSlidesList(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%16)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidesList: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(SlideList)[Z"<< entry.id() << "]:";
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if ((N+1)*16>int(entry.length())) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidesList: the number of slides seems bad\n"));
    f << "###";
    N=int(entry.length()/16)-1;
  }
  auto val=int(input->readLong(4)); // always 10, headerSz?
  if (val!=10) f << "f0=" << val << ",";
  for (int i=0; i<5; ++i) { // f1=1
    val=int(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  auto const numZones=int(m_state->m_zonesList.size());
  m_state->m_slidesIdList.resize(size_t(N), -1);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "SlideList-" << i << ":";
    for (int j=0; j<3; ++j) { // f0=0-3
      val=int(input->readLong(2));
      if (val) f << "f" << j << "=" << val << ",";
    }
    for (int j=0; j<2; ++j) { // fl0=0|1|81
      val=int(input->readULong(1));
      if (val) f << "fl" << j << "=" << std::hex << val << std::dec << ",";
    }
    for (int j=0; j<2; ++j) { // always 0
      val=int(input->readLong(2));
      if (val) f << "f" << j+2 << "=" << val << ",";
    }
    val=int(input->readULong(4));
    if (val>=0 && val<numZones) {
      f << "slide[id]=Z" << val << ",";
      m_state->m_slidesIdList[size_t(i)]=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidesList: find some bad child\n"));
      f << "slide[id]=##Z" << val << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidesList: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("SlideList-extra:###");
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readSlide(MWAWEntry const &entry, PowerPoint3ParserInternal::Slide &slide, int zId)
{
  int const vers=version();
  if (!entry.valid() || entry.length()!=(vers<=3 ? 32 : 34)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlide: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  if (zId>=0)
    f << "Entries(SlideMain)[Z"<< entry.id() << "," << zId << "]:";
  else
    f << "Entries(SlideMain)[Z"<< entry.id() << "]:";
  auto val=int(input->readULong(4));
  if (val) f << "id=" << std::hex << val << std::dec << ",";
  auto const numZones=int(m_state->m_zonesList.size());
  int childA=-1;
  for (size_t i=0; i<3; ++i) {
    val=int(input->readULong(4));
    if (i && val==0) continue;
    char const *wh[]= {"transition[id]", "slide[id]", "note[id]"};
    if (val>=0 && val<numZones) {
      f << wh[i] << "=Z" << val << ",";
      if (i==0)
        childA=val;
      else
        slide.m_contentIds[i-1]=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlide: find some bad child\n"));
      f << wh[i] << "=##Z" << val << ",";
    }
  }
  f << "ids=[";
  for (int i=0; i<3; ++i) // first two big numbers, last 1|2a3d
    f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  for (int i=0; i<2; ++i) { // f0=0(for mac),-1(for pc), f1=0
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  m_state->m_idToSlideMap[entry.id()]=slide;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  for (size_t i=0; i<3; ++i) {
    int id=i==0 ? childA : slide.m_contentIds[i-1];
    MWAWEntry const &cEntry=m_state->getZoneEntry(id);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    if (i==0)
      readSlideTransition(cEntry);
    else {
      PowerPoint3ParserInternal::SlideContent content;
      if (readSlideContent(cEntry, content))
        m_state->m_idToSlideContentMap[id]=content;
    }
  }
  return true;
}

bool PowerPoint3Parser::readSlideTransition(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()!=24) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlideTransition: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(SlideTransition)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {m_state->m_isMacFile ? 0 : 0x4b4e, 0x554e};
    if (val==expected[i]) continue;
    if (i==0)
      f << "effect=" << val << ",";
    else
      f << "id" << i << "=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readLong(2));
  switch (val) {
  case 0:
    f << "effect[slow],";
    break;
  case 1:
    f << "effect[medium],";
    break;
  case 2: // fast
    break;
  default:
    f << "effect=##" << val << ",";
    break;
  }
  val=int(input->readULong(2)); // small number
  if (val) f  << "f0=" << val << ",";
  val=int(input->readLong(4));
  if (val!=-1) f << "adv[time]=" << double(val)/1000. << "s,";
  val=int(input->readLong(2));
  if (val) f << "f1=" << val << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(1));
    int const expected[]= {7, 2};
    if (val!=expected[i]) f << "f" << i+2 << "=" << val << ",";
  }
  for (int i=0; i<3; ++i) { // g0=0|c
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  val=int(input->readULong(1)); // 0|3f
  if (val) f << "g3=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readSlideContent(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &slide)
{
  if (!entry.valid() || entry.length()!=38) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlideContent: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(SlideContent)[Z"<< entry.id() << "]:";
  int val;
  auto numZones=int(m_state->m_zonesList.size());
  std::vector<int> listChild;
  listChild.resize(4, -1);
  for (size_t i=0; i<2; ++i) {
    val=int(input->readULong(4));
    if (!val) continue;
    f << (i==0 ? "text[id]" : "frame[id]") << "=Z" << val << ",";
    if (val>=0 && val<numZones)
      listChild[i]=val;
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlideContent: find some bad child\n"));
      f << "###";
    }
  }
  f << "num[shapes]=" << input->readLong(2) << ",";
  f << "act[shape]=" << input->readLong(2) << ",";
  f << "frames[ids]=["; // 0: ?, 1: title, 2: body
  slide.m_numMainZones=0;
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    if (val==-1) {
      f << "_,";
      continue;
    }
    if (i>=1) slide.m_mainZoneIds[i-1]=val;
    f << val << ",";
  }
  f << "],";
  val=int(input->readULong(1));
  f << "useMaster[";
  if (m_state->m_isMacFile) {
    if (val&0x80) {
      slide.m_useMasterPage=true;
      f << "content,";
    }
    if (val&0x40)
      f << "schemeStyle,";
    if (val&0x20)
      f << "titleStyle,";
    if (val&0x10)
      f << "bodyStyle,";
    val &= 0xF;
  }
  else {
    if (val&1)  {
      slide.m_useMasterPage=true;
      f << "content,";
    }
    val &= 0xFE;
  }
  f << "],";
  if (val)
    f << "fl=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(1));
  if (val) f << "fl1=" << std::hex << val << std::dec << ",";
  slide.m_schemeId=int(input->readULong(2));
  if (slide.m_schemeId) f << "scheme=S" << slide.m_schemeId << ",";
  val=int(input->readULong(1));
  if (val) f << "fl2=" << std::hex << val << std::dec << ",";
  for (int i=0; i<7; ++i) {
    val=int(input->readULong(1));
    if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (size_t i=2; i<4; ++i) {
    val=int(input->readULong(4));
    if (!val) continue;
    f << (i==2 ? "format[id]" : "poly[id]") << "=Z" << val << ",";
    if (val>=0 && val<numZones)
      listChild[i]=val;
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlideContent: find some bad child\n"));
      f << "###";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  for (size_t i=0; i<4; ++i) {
    size_t orders[]= {0, 2, 1, 3};
    size_t const id=orders[i];
    MWAWEntry const &cEntry=m_state->getZoneEntry(listChild[id]);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    if (id==0)
      readTextZone(cEntry, slide);
    else if (id==1)
      readFramesList(cEntry, slide);
    else if (id==2)
      readSlideFormats(cEntry, slide.m_formatList);
    else
      readSlidePolygons(cEntry, slide.m_polygonList);
  }
  return true;
}

bool PowerPoint3Parser::readSlideFormats(MWAWEntry const &entry, std::vector<PowerPoint3ParserInternal::SlideFormat> &formatList)
{
  int const vers=version();
  int const dSz = vers<=3 ? 20 : 26;
  if (!entry.valid() || (entry.length()%dSz)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlideFormats: the zone Z%d seems bad\n", entry.id()));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(SlideFormat)[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  bool const isMacFile=m_state->m_isMacFile;
  auto N=size_t(entry.length()/dSz);
  formatList.resize(N);
  for (size_t i=0; i<N; ++i) {
    PowerPoint3ParserInternal::SlideFormat &format=formatList[i];
    pos=input->tell();
    f.str("");
    f << "SlideFormat-" << i << ":";
    int val;
    for (int j=0; j<2; ++j) { // fl0=[1-c]0
      val=int(input->readULong(1));
      if (!isMacFile) val=PowerPoint3ParserInternal::swapBool8(val);
      if (val) f << "fl" << j << "=" << std::hex << val << std::dec << ",";
    }
    val=int(input->readULong(4));
    if (val) f << "id=" << std::hex << val << std::dec << ",";
    int dim[2];
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) std::swap(dim[0],dim[1]);
    f << "dim0=" << MWAWVec2i(dim[1], dim[0]) << ",";
    format.m_gradientOffset=int(input->readLong(2));
    if (format.m_gradientOffset) f << "grad[col,offset]=" << format.m_gradientOffset << ",";
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) std::swap(dim[0],dim[1]);
    format.m_margins=MWAWVec2i(dim[1], dim[0]);
    f << "box[margins]=" << format.m_margins << ",";
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) std::swap(dim[0],dim[1]);
    format.m_shadowOffset=MWAWVec2i(dim[1], dim[0]);
    if (format.m_shadowOffset!=MWAWVec2i(0,0))
      f << "shadow[offset]=" << format.m_shadowOffset << ",";
    if (input->tell()!=pos+dSz) {
      ascii().addDelimiter(input->tell(),'|');
      input->seek(pos+dSz, librevenge::RVNG_SEEK_SET);
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readSlidePolygons(MWAWEntry const &entry, std::vector<PowerPoint3ParserInternal::Polygon> &polyList)
{
  if (!entry.valid() || entry.length()<12) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidePolygons: the zone Z%d seems bad\n", entry.id()));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(SlidePolygon)[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  int n=0;
  bool const isMacFile=m_state->m_isMacFile;
  while (input->tell()<entry.end()+12) {
    pos=input->tell();
    f.str("");
    f << "SlidePolygon-" << n++ << ":";
    auto N=int(input->readULong(2));
    if (pos+4+(N+2)*4 > entry.end()) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    polyList.push_back(PowerPoint3ParserInternal::Polygon());
    auto &poly=polyList.back();
    f << "N=" << N << ",";
    poly.m_type=int(input->readULong(2));
    f << "type=" << poly.m_type << ",";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) {
      std::swap(dim[0],dim[1]);
      std::swap(dim[2],dim[3]);
    }
    poly.m_box=MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2]));
    f << "box=" << poly.m_box << ",";
    f << "pts=[";
    for (int pt=0; pt<N; ++pt) {
      for (int i=0; i<2; ++i) dim[i]=int(input->readLong(2));
      if (!isMacFile) std::swap(dim[0],dim[1]);
      poly.m_vertices.push_back(MWAWVec2f(float(dim[1])/8.f, float(dim[0])/8.f));
      f << poly.m_vertices.back() << ",";
    }
    f << "],";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readSlidePolygons: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("SlidePolygon:###extra");
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readFramesList(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &content)
{
  if (!entry.valid() || (entry.length()%32)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: the zone seems bad\n"));
    return false;
  }
  int const vers=version();
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Frames)[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  auto N=size_t(entry.length()/32);
  bool const isMacFile=m_state->m_isMacFile;
  content.m_frameList.resize(N);
  int format=0, poly=0, text=0;
  int showTypes[3]= {0,0,0}; // surf, frame, shadow
  // surf, surf[back], frame, frame[back], shadow
  MWAWColor colors[5]= {MWAWColor::black(),MWAWColor::white(),MWAWColor::white(),MWAWColor::black(),MWAWColor::black()};
  int patterns[2]= {0,0};
  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Frames-" << i << ":";
    auto &frame=content.m_frameList[i];
    MWAWGraphicStyle &style=frame.m_style;
    if (vers<=3) {
      auto type=int(input->readULong(1));
      if (!isMacFile) type=PowerPoint3ParserInternal::swapUInt8(type);
      frame.m_type=(type>>4)&7;
      showTypes[0]=type&0x7;
      type&=0x88;
      if (type) f << "fl0=" << std::hex << type << std::dec << ",";
      auto type2=int(input->readULong(1));
      if (!isMacFile) type2=PowerPoint3ParserInternal::swapUInt8(type2);
      showTypes[2]=type2&3;
      showTypes[1]=(type2>>4)&3;
      type2&=0xCC;
      if (type2) f << "fl1=" << std::hex << type2 << std::dec << ",";
    }
    else {
      auto type=int(input->readULong(2));
      int dashId=0;
      if (isMacFile) {
        frame.m_type=(type>>13)&7;
        showTypes[0]=(type>>10)&0x7;
        dashId=(type>>6)&7;
        showTypes[2]=(type>>4)&3;
        type &= 0x20F;
      }
      else {
        frame.m_type=(type&7);
        showTypes[0]=(type>>3)&0x7;
        dashId=(type>>6)&7;
        showTypes[2]=(type>>10)&3;
        type&=0xF200;
      }
      showTypes[1]=1;
      switch (dashId) {
      case 0: // none
      case 1: // normal
      case 2: // unsure
        showTypes[1]=dashId;
        break;
      case 3:
        style.m_lineDashWidth.resize(2,1);
        f << "dot,";
        break;
      case 4:
        style.m_lineDashWidth.resize(2,2);
        f << "dot[2x2],";
        break;
      case 5:
        style.m_lineDashWidth.resize(2,4);
        f << "dot[4x2],";
        break;
      case 6:
        style.m_lineDashWidth.resize(4,2);
        style.m_lineDashWidth[2]=1;
        f << "dot[4,4,1,4],";
        break;
      default:
        f << "##dashId=" << dashId << ",";
      }
      if (type) f << "fl0=" << std::hex << type << std::dec << ",";
    }
    switch (frame.m_type) {
    case 0:
      f << "line,";
      break;
    case 0x1: // link to data?
      f << "gen,";
      break;
    case 0x2:
      frame.m_polygonId=poly++;
      f << "poly,";
      break;
    case 0x3:
      f << "arc,";
      break;
    case 0x4:
      f << "group,";
      break;
    default:
      f << "type=" << frame.m_type << ",";
      break;
    }
    switch (showTypes[0]) {
    case 0: // none
      break;
    case 1:
      f << "filled,";
      break;
    case 2:
      f << "opaque,";
      break;
    case 3:
      f << "pattern,";
      break;
    case 4:
      f << "gradient,";
      break;
    default:
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unknown surface style\n"));
      f << "###surf[type]=" << showTypes[0] << ",";
      break;
    }
    switch (showTypes[1]) {
    case 0:
      f << "no[line],";
      break;
    case 2:
      f << "line[pattern],";
      break;
    case 3:
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unknown line style\n"));
      f << "##line[type2]=3,";
      break;
    default:
      break;
    }
    if (showTypes[2]&1) f << "shadow,";
    if (showTypes[2]&2) f << "emboss,";
    bool hasPicture=false;
    auto val=int(input->readULong(1));
    if (!isMacFile) val=PowerPoint3ParserInternal::swapBool8(val);
    if (val&1)
      f << "basic,";
    if (val&2)
      f << "wrap[text],";
    if (val&4)
      f << "adjust[textbox],";
    if (val&0x8) {
      style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
      f << "arrow[start],";
    }
    if (val&0x10) {
      style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
      f << "arrow[end],";
    }
    PowerPoint3ParserInternal::SlideFormat *sFormat=nullptr;
    if (val&0x20) {
      f << "has[format],";
      if (format<0||format>=int(content.m_formatList.size())) {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: can not find the slide format's\n"));
        f << "###,";
      }
      else {
        sFormat=&content.m_formatList[size_t(format)];
        frame.m_formatId=format++;
      }
    }
    if (val&0x40) {
      f << "has[pict],";
      hasPicture=true;
    }
    if (val&0x80) {
      f << "has[text],";
      int mainId=-1;
      for (int j=0; j<2; ++j) {
        if (int(i)!=content.m_mainZoneIds[j]) continue;
        mainId = (j==1 && content.m_mainZoneIds[0]==-1) ? 0 : j;
      }
      if (mainId!=-1) {
        frame.m_textId=mainId;
        frame.m_mainTextBox=true;
        ++content.m_numMainZones;
      }
      else
        frame.m_textId=text++;
    }
    val=int(input->readULong(1));
    if (isMacFile) val=PowerPoint3ParserInternal::swapUInt4Bool4(val);
    switch ((val>>2)&3) {
    case 0: // top
      break;
    case 1:
      f << "center[h],";
      break;
    case 2:
      f << "bottom[h],";
      break;
    default:
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: unknown vertical\n"));
      f << "##vert=3,";
    }
    if (!(val&0x2)) f << "centered,";
    val &=0xF1;
    if (val) f << "fl3=" << std::hex << val << std::dec << ",";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) {
      std::swap(dim[0],dim[1]);
      std::swap(dim[2],dim[3]);
    }
    frame.m_dimension=MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2]));
    f << "dim=" << frame.m_dimension << ",";
    int lineType=1;
    for (int w=0; w<2; ++w) {
      f << (w==0 ? "surf" : "line") << "=[";
      if (w==1) {
        val=int(input->readULong(1));
        if (val>=0 && val<=10) {
          if (val!=1) {
            char const *wh[]= {"none", "w=1", "w=2","w=4", "w=8", "w=16", "w=32",
                               "double", "double1x2", "double2x1", "triple1x2x1"
                              };
            f << wh[val] << ",";
          }
          lineType=val;
        }
        else {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unexpected line type\n"));
          f << "##line=" << val << ",";
        }
      }
      patterns[w]=int(input->readULong(1));
      if (patterns[w]) f << "pat=" << patterns[w] << ",";
      for (int j=0; j<2; ++j) {
        val=int(input->readULong(1));
        int cId=2*w+j;
        if (getColor(val, content.m_schemeId, colors[cId])) {
          if (((cId%3)!=0 && !colors[cId].isBlack()) || ((cId%3)==0 && !colors[cId].isWhite()))
            f << colors[cId] << ",";
          else
            f << "_,";
        }
        else
          f << "###col=" << val << ",";
      }
      f << "],";
    }

    if (lineType && showTypes[1]) {
      MWAWColor color=colors[2];
      if (showTypes[1]==2) {
        MWAWGraphicStyle::Pattern pattern;
        if (m_state->getPattern(patterns[1], pattern)) {
          pattern.m_colors[0]=colors[2];
          pattern.m_colors[1]=colors[3];
          pattern.getAverageColor(color);
        }
      }
      float const lWidth[]= {0, 1, 2, 3, 6, 12, 16, 3, 4, 4, 6};
      style.m_lineWidth=lWidth[lineType];
      style.m_lineColor=color;
      MWAWBorder border;
      border.m_width=double(lWidth[lineType]);
      border.m_color=color;
      switch (lineType) {
      case 7:
        border.m_type=MWAWBorder::Double;
        break;
      case 8:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        break;
      case 9:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      case 10:
        border.m_type=MWAWBorder::Triple;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      default:
        break;
      }
      style.setBorders(0xF, border);
    }
    else
      style.m_lineWidth=0;
    if (showTypes[0]==4) {
      int gradId=(patterns[0]>>4);
      int subId=(patterns[0]&0xF);
      MWAWColor auxColor(0,0,0);
      if (sFormat) {
        if (sFormat->m_gradientOffset<0)
          auxColor=MWAWColor::barycenter(float(-sFormat->m_gradientOffset)/10.f,MWAWColor::black(), float(10+sFormat->m_gradientOffset)/10.f, colors[0]);
        else if (sFormat->m_gradientOffset>0)
          auxColor=MWAWColor::barycenter(float(sFormat->m_gradientOffset)/10.f,MWAWColor::white(), float(10-sFormat->m_gradientOffset)/10.f, colors[0]);
        else
          auxColor=colors[0];
      }
      auto &finalGrad=style.m_gradient;
      finalGrad.m_stopList.resize(0);
      if (gradId>=1 && gradId<=4) {
        if (subId<2) {
          finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Linear;
          for (int c=0; c < 2; ++c)
            finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), (c==subId)  ? colors[0] : auxColor));
        }
        else {
          finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Axial;
          for (int c=0; c < 3; ++c)
            finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c)/2.f, ((c%2)==(subId%2)) ? colors[0] : auxColor));
        }
        float angles[]= {0,90,45,315};
        finalGrad.m_angle=angles[gradId-1];
      }
      else if (gradId==5) {
        finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Rectangular;
        for (int c=0; c < 2; ++c)
          finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), c==0 ? colors[0] : auxColor));
        finalGrad.m_percentCenter=MWAWVec2f(float(subId&1),float(subId<2 ? 0 : 1));
      }
      else if (gradId==7) {
        finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Rectangular;
        for (int c=0; c < 2; ++c)
          finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), ((c%2)==(subId%2))  ? colors[0] : auxColor));
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unknown gradient\n"));
        style.setSurfaceColor(colors[0]);
      }
    }
    else if (showTypes[0]==3) {
      MWAWGraphicStyle::Pattern pattern;
      if (m_state->getPattern(patterns[0], pattern)) {
        pattern.m_colors[0]=colors[1];
        pattern.m_colors[1]=colors[0];
        MWAWColor color;
        if (pattern.getUniqueColor(color))
          style.setSurfaceColor(color);
        else
          style.setPattern(pattern);
      }
    }
    else if (showTypes[0]==1)
      style.setSurfaceColor(colors[0]);
    else if (showTypes[0]==2) {
      MWAWColor bgColor;
      if (getColor(0, content.m_schemeId, bgColor))
        style.setSurfaceColor(bgColor);
    }
    f << "shadow=[";
    val=int(input->readULong(1));
    if (getColor(val, content.m_schemeId, colors[4])) {
      if (!colors[4].isBlack())
        f << colors[4] << ",";
    }
    else
      f << "###col=" << val << ":S" << content.m_schemeId << ",";
    val=int(input->readULong(4)); // some big number probably an id
    if (val) f <<  std::hex << val << std::dec << ",";
    f << "],";
    if (showTypes[2]) {
      style.setShadowColor(colors[4]);
      style.m_shadowOffset=MWAWVec2f(6,6);
    }
    switch (frame.m_type) {
    case 1:
      val=int(input->readLong(2));
      if (val<0) {
        frame.m_shapeId=-val;
        if (frame.m_shapeId<=3) {
          char const *wh[]= {"oval", "rect[oval]", "rect"};
          f << wh[frame.m_shapeId-1] << ",";
        }
        else {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unexpected type\n"));
          f << "###type=" << frame.m_shapeId << ",";
        }
      }
      else {
        frame.m_customShapeId=(val&0xFF);
        frame.m_customTransformation=(val>>12);
        f <<  "custom=" << frame.m_customShapeId << ",";
        if (frame.m_customTransformation&1) f << "rot90,";
        if (frame.m_customTransformation&2) f << "rot180,";
        if (frame.m_customTransformation&4) f << "flipX,";
        val=((val&0x8f00)>>8);
        if (val) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find unexpected transformation\n"));
          f << "##trans=" << std::hex << val << std::dec << "]";
        }
        f << ",";
      }
      val=int(input->readLong(2));
      if (val) f << "f0=" << val << ",";
      if (hasPicture) {
        frame.m_pictureId=int(input->readULong(2));
        f << "pict[id]=" << frame.m_pictureId << ",";
      }
      break;
    case 3:
      for (auto &angle : frame.m_angles) angle=float(input->readLong(2))/16.f;
      f << "angles=" << frame.m_angles[0] << "<->" << frame.m_angles[0]+frame.m_angles[1] << ",";
      for (int j=0; j<4; ++j) { // f0=0|1
        val=int(input->readLong(1));
        if (val) f << "f" << j << "=" << val << ",";
      }
      break;
    case 4:
      for (auto &id : frame.m_groupChild) id=int(input->readULong(4));
      f << "child=" << frame.m_groupChild[0] << "<->" << frame.m_groupChild[1] << ",";
      if (frame.m_groupChild[0]<0 || frame.m_groupChild[1]>=int(N) || frame.m_groupChild[0]>frame.m_groupChild[1]) {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readFramesList: find bad group child\n"));
        f << "###";
        frame.m_groupChild[0]=frame.m_groupChild[1]=-1;
      }
      break;
    // line, poly, probably no other data or flag
    default:
      break;
    }
    if (input->tell()!=pos+32)
      ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+32, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readTextZone(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &content)
{
  if (!entry.valid() || entry.end()<14) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  long const endPos = entry.end();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(TextZone)[Z"<< entry.id() << "]:";
  bool const isMacFile=m_state->m_isMacFile;
  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(4));
    if (val) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(1));
    if (!val) continue;
    if (!isMacFile) val=PowerPoint3ParserInternal::swapBool8(val);
    f << "fl" << i << "=" << std::hex << val << std::dec <<",";
  }
  val=int(input->readULong(4));
  if (val) f << "f2=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(endPos);
  ascii().addNote("_");
  int const schemeId=content.m_schemeId;
  while (input->tell()+58<endPos) {
    PowerPoint3ParserInternal::TextZone tZone;
    pos=input->tell();
    f.str("");
    f << "TextZone-A:";
    tZone.m_rulerId=int(input->readLong(2));
    if (tZone.m_rulerId) f << "ruler=" << tZone.m_rulerId << ",";
    for (int i=0; i<2; ++i) { // fl0 small number
      val=int(input->readULong(1));
      if (!val) continue;
      if (isMacFile) val=PowerPoint3ParserInternal::swapBool4UInt4(val);
      if (i==0) {
        switch ((val>>6)&3) {
        case 0: // top
          break;
        case 1:
          f << "center[h],";
          break;
        case 2:
          f << "bottom[h],";
          break;
        default:
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: unknown vertical\n"));
          f << "##vert=3,";
        }
        if (!(val&0x20)) {
          tZone.m_centered=true;
          f << "centered,";
        }
        if (val&0x2) {
          tZone.m_wrapText=true;
          f << "wrap[text],";
        }
        if (val&0x1) {
          tZone.m_adjustSize=true;
          f << "adjust[textbox],";
        }
        if (val&0x8)
          f << "basic,";
        val&=0x14;
      }
      if (val)
        f << "fl" << i << "=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) { // f1 small number
      val=int(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    int dim[4];
    for (int &d : dim) d=int(input->readLong(2));
    if (!isMacFile) {
      std::swap(dim[0],dim[1]);
      std::swap(dim[2],dim[3]);
    }
    tZone.m_box=MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
    f << "dim=" << tZone.m_box << ",";
    MWAWFont font;
    if (readFont(font, schemeId))
      f << "font=[" << font.getDebugString(getFontConverter()) << "],";
    else {
      f << "###font,";
      input->seek(pos+16+12, librevenge::RVNG_SEEK_SET);
    }
    MWAWParagraph para;
    PowerPoint3ParserInternal::Ruler ruler;
    if (readParagraph(para, ruler, schemeId))
      f << "ruler=[" << para << "],";
    else  {
      f << "###para,";
      input->seek(pos+16+12+24, librevenge::RVNG_SEEK_SET);
    }
    val=int(input->readLong(2));
    if (val) f << "f3=" << val << ",";
    auto sSz=int(input->readULong(4));
    if (sSz<0 || endPos-pos-58-8-10<sSz || pos+58+sSz+8+10>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    tZone.m_text.setBegin(input->tell());
    tZone.m_text.setLength(sSz);
    if (sSz) {
      pos=input->tell();
      f.str("");
      f << "TextZone-text:";
      std::string text;
      for (int c=0; c<sSz; ++c) text+=char(input->readULong(1));
      f << text;
      if (sSz&1)
        input->seek(1, librevenge::RVNG_SEEK_CUR);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }

    pos=input->tell();
    f.str("");
    f << "TextZone:font,";
    val=int(input->readLong(2));
    if (val!=0xc) f << "f0=" << val << ",";
    val=int(input->readULong(4));
    if (val!=sSz) f << "#N=" << val << ",";
    auto fSz=int(input->readULong(2));
    if (fSz!=16) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: field size seems bad\n"));
      f << "#fSz=" << fSz << ",";
    }
    auto N=int(input->readULong(4));
    f << "N=" << N << ",";
    if (fSz<4 || N<0 || (endPos-pos)/fSz<N || pos+12+fSz*N+8>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    if (fSz==16) {
      tZone.m_fonts.setBegin(input->tell());
      tZone.m_fonts.setLength(N*fSz);
      input->seek(tZone.m_fonts.end(), librevenge::RVNG_SEEK_SET);
    }
    else {
      for (int i=0; i<N; ++i) {
        pos=input->tell();
        f.str("");
        f << "TextZone-F" << i << ":";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        input->seek(pos+fSz, librevenge::RVNG_SEEK_SET);
      }
    }

    pos=input->tell();
    f.str("");
    f << "TextZone-R:";
    val=int(input->readLong(2));
    if (val!=0x18) f << "f0=" << val << ",";
    val=int(input->readULong(4));
    if (val!=sSz) f << "#N=" << val << ",";
    fSz=int(input->readULong(2));
    if (fSz!=28) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: field size seems bad\n"));
      f << "#fSz=" << fSz << ",";
    }
    N=int(input->readULong(4));
    f << "N=" << N << ",";
    if (fSz<4 || N<0 || (endPos-pos)/fSz<N || pos+12+fSz*N>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    if (fSz==28) {
      tZone.m_rulers.setBegin(input->tell());
      tZone.m_rulers.setLength(N*fSz);
      input->seek(tZone.m_rulers.end(), librevenge::RVNG_SEEK_SET);
    }
    else {
      long cPos=0;
      for (int i=0; i<N; ++i) {
        pos=input->tell();
        f.str("");
        auto cLen=int(input->readULong(4));
        f << "TextZone-R" << i << "[" << cPos << "->" << cPos+cLen << "]:";
        cPos+=cLen;
        if (cPos>sSz) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: the cLen seems bad\n"));
          f << "###";
        }
        if (input->tell()!=pos+fSz)
          ascii().addDelimiter(input->tell(),'|');
        input->seek(pos+fSz, librevenge::RVNG_SEEK_SET);
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
      }
    }
    content.m_textZone.push_back(tZone);
  }
  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readTextZone: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("TextZone:###extra");
  }
  return true;
}

bool PowerPoint3Parser::readStructList(MWAWEntry const &entry, PowerPoint3ParserInternal::FieldParser &parser)
{
  bool useInt16 = (!m_state->m_isMacFile && version()<=3);
  int const headerSize= useInt16 ? 16 : 18;
  if (!entry.valid() || entry.length()<headerSize) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readStructList: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(" << parser.m_name << ")[Z"<< entry.id() << "]:list,";
  auto val=int(input->readULong(2));
  if (val!=0x8001)
    f << "f0=" << std::hex << val << std::dec << ",";
  f << "id=" << std::hex << input->readULong(useInt16 ? 2 : 4) << std::dec << ",";
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x7fff, 0};
    if (val!=expected[i])
      f << "f" << i+1 << "=" << val << ",";
  }
  auto const fieldSize=int(input->readULong(2));
  if (N>(entry.length()-headerSize)/(2+fieldSize)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readStructList: the N value seems bad\n"));
    f << "###";
    N=int((entry.length()-headerSize)/(2+fieldSize));
  }
  f << "id2=" << std::hex << input->readULong(4) << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  std::vector<int> listChild;
  listChild.resize(size_t(N), -1);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    val=int(input->readLong(2));
    if (val && fieldSize==parser.m_fieldSize && parser.parse(i, input, ascii())) {
      input->seek(pos+2+fieldSize, librevenge::RVNG_SEEK_SET);
      continue;
    }
    else if (val) {
      f << parser.m_name << "-" << val << ":";
      ascii().addDelimiter(input->tell(),'|');
    }
    else
      f << "_,";
    input->seek(pos+2+fieldSize, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    f.str("");
    f << parser.m_name << ":##extra";
    ascii().addPos(input->tell());
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readPicturesList(std::map<int,int> const &pIdtoZIdMap)
{
  for (auto it : pIdtoZIdMap) {
    MWAWEntry const &cEntry=m_state->getZoneEntry(it.second);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    readPictureDefinition(cEntry, it.first);
  }
  return true;
}

bool PowerPoint3Parser::readPictureDefinition(MWAWEntry const &entry, int pId)
{
  if (!entry.valid() || entry.length()<24) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureDefinition: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  ascii().addPos(entry.end());
  ascii().addNote("_");
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Picture)[Z"<< entry.id() << "-" << pId << "]:def,";
  auto val=int(input->readULong(2)); // 0|4|90|b4|fc|120|144
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  auto id=int(input->readULong(2));
  if (id)
    f << "id=" << std::hex << id << std::dec << ",";
  f << "ole,";
  val=int(input->readULong(2));
  if (val)
    f << "id2=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(4));
  if (val)
    f << "id3=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(2)); // small number
  if (val) f << "f0=" << val << ",";
  val=int(input->readULong(4));
  if (val)
    f << "id4=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  auto numZones=int(m_state->m_zonesList.size());
  int childs[2]= {-1,-1};
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(4));
    if (val>=0 && val<numZones) {
      f << "child" << i << "[id]=Z" << val << ",";
      childs[i]=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureDefinition: find some bad child\n"));
      f << "child" << i << "[id]=##Z" << val << ",";
    }
    if (entry.length()==24) break;
  }
  if (entry.length()==122) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    pos=input->tell();
    f.str("");
    f << "PictureA:ole,";
    for (int i=0; i<2; ++i) { // f0=8(windows)|10(mac)
      val=int(input->readULong(1));
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) { // mine, program
      long actPos=input->tell();
      int sSz=32;
      if (m_state->m_isMacFile) { // Mac : pascal, Windows : C string
        sSz=int(input->readULong(1));
        if (sSz>31) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureDefinition: the string size seems bad\n"));
          f << "##sSz=" << sSz << ",";
          sSz=31;
        }
      }
      std::string name;
      for (int c=0; c<sSz; ++c) {
        auto ch=char(input->readULong(1));
        if (ch==0) break;
        name+=ch;
      }
      f << "str" << i << "=" << name << ",";
      input->seek(actPos+32, librevenge::RVNG_SEEK_SET);
    }
    ascii().addDelimiter(input->tell(),'|');
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<2; ++i) {
    MWAWEntry const &cEntry=m_state->getZoneEntry(childs[i]);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    if (i==0) {
      MWAWEmbeddedObject data;
      readPictureContent(cEntry, data);
      if (!data.isEmpty())
        m_state->m_idToPictureContentMap.insert(std::map<int, MWAWEmbeddedObject>::value_type(entry.id(),data));
    }
    else {
      cEntry.setParsed(true);
      f.str("");
      f << "Entries(PictData)[Z" << cEntry.id() << "-B]:";
      ascii().addPos(cEntry.begin());
      ascii().addNote(f.str().c_str());
      ascii().addPos(cEntry.end());
      ascii().addNote("_");
    }
  }
  return true;
}

bool PowerPoint3Parser::readPictureContent(MWAWEntry const &entry, MWAWEmbeddedObject &pict)
{
  bool isMacFile=m_state->m_isMacFile;
  int const vers=version();
  if (!entry.valid() || entry.length()!=(vers>=4 ? 60 : isMacFile ? 50 : 48)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureContent: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(PictData)[Z"<< entry.id() << "-A]:def,";
  auto numZones=int(m_state->m_zonesList.size());
  auto val=int(input->readLong(2)); // mac: 5, windows: 0
  if (val) f << "f0=" << val << ",";
  int childs[2]= {-1,-1};
  if (isMacFile) {
    std::string rsrcName;
    for (int i=0; i<4; ++i) rsrcName+=char(input->readULong(1));
    f << rsrcName << ",";
    f << "id=" << std::hex << input->readULong(4) << std::dec << ",";
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(4));
      if (val==-1) continue;
      if (val>=0 && val<numZones) {
        f << "child" << i << "[id]=Z" << val << ",";
        childs[i]=val;
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureContent: find some bad child\n"));
        f << "child" << i << "[id]=##Z" << val << ",";
      }
    }
  }
  else {
    for (int i=0; i<2; ++i) { // f1=0-4
      val=int(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=int(input->readULong(vers>=4 ? 4 : 2));
    if (val!=0x262a) f << "id=" << std::hex << val << std::dec << ",";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    f << "dim=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(2));
      if (val==-1) continue;
      if (val>=0 && val<numZones) {
        f << "child" << i << "[id]=Z" << val << ",";
        childs[i]=val;
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureContent: find some bad child\n"));
        f << "child" << i << "[id]=##Z" << val << ",";
      }
    }
  }
  for (int i=0; i<(isMacFile ? 16 : 14); ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  for (int i=0; i<2; ++i) { // first is the picture, second maybe a bitmap
    MWAWEntry const &cEntry=m_state->getZoneEntry(childs[i]);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    cEntry.setParsed(true);
    if (i==0) {
      input->seek(cEntry.begin(), librevenge::RVNG_SEEK_SET);
      ascii().skipZone(pos, cEntry.end()-1);
      librevenge::RVNGBinaryData file;
      input->seek(cEntry.begin(), librevenge::RVNG_SEEK_SET);
      input->readDataBlock(cEntry.length(), file);
      pict.add(file);
#ifdef DEBUG_WITH_FILES
      static int volatile pictName = 0;
      f.str("");
      f << "PICT-" << ++pictName << ".pct";
      libmwaw::Debug::dumpFile(file, f.str().c_str());
#endif
      ascii().addPos(cEntry.end());
      ascii().addNote("_");
      continue;
    }
    f.str("");
    f << "Entries(PictData)[Z" << cEntry.id() << "-D]:";
    ascii().addPos(cEntry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(cEntry.end());
    ascii().addNote("_");
  }
  return true;
}

bool PowerPoint3Parser::readFont(MWAWFont &font, int schemeId)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+12)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readFont: the zone seems bad\n"));
    return false;
  }
  font=MWAWFont();
  libmwaw::DebugStream f;
  auto val=int(input->readLong(2));
  if (val>=0 && m_state->m_idToFontIdMap.find(val)!= m_state->m_idToFontIdMap.end())
    font.setId(m_state->m_idToFontIdMap.find(val)->second);
  else if (val>=0) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readFont: can not find the font id\n"));
    f << "##id=" << val << ",";
  }
  val=int(input->readLong(2));
  if (val>0) font.setSize(float(val));
  auto flag = int(input->readULong(2));
  uint32_t flags=0;
  if (flag&0x1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x8) flags |= MWAWFont::embossBit;
  if (flag&0x10) flags |= MWAWFont::shadowBit;
  if (flag&0xFFE0) f << "##flag=" << std::hex << (flag>>5) << std::dec << ",";
  font.setFlags(flags);
  auto col=int(input->readLong(2));
  MWAWColor color;
  if (col>=0 && getColor(col, schemeId, color))
    font.setColor(color);
  else if (col>=0)
    f << "###col=" << col << ":S" << schemeId << ",";
  val=int(input->readLong(2));
  if (val) font.set(MWAWFont::Script(float(val),librevenge::RVNG_PERCENT,58));
  font.m_extra=f.str();
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PowerPoint3Parser::readFontNamesList(std::map<int,int> const &pIdtoZIdMap)
{
  for (auto it : pIdtoZIdMap) {
    MWAWEntry const &cEntry=m_state->getZoneEntry(it.second);
    if (!cEntry.valid() || cEntry.isParsed()) continue;
    readFontName(cEntry, it.first);
  }
  return true;
}

bool PowerPoint3Parser::readFontName(MWAWEntry const &entry, int id)
{
  if (!entry.valid() || entry.length()!=12) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readFontName: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "FontName[Z"<< entry.id() << "-" << id << "]:def,";
  auto numZones=int(m_state->m_zonesList.size());
  auto val=int(input->readULong(4));
  int child=-1;
  if (val) {
    if (val>=0 && val<numZones) {
      f << "name[id]=Z" << val << ",";
      child=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readFontName: find some bad child\n"));
      f << "name[id]=##Z" << val << ",";
    }
  }
  for (int i=0; i<4; ++i) { // f3=0..255
    val=int(input->readULong(2));
    if (!val) continue;
    if (i==2) {
      if (val!=0xFFFF)
        f << "fId=" << val << ",";
      else
        f << "fId*,";
    }
    else
      f << "f" << i << "=" << val <<",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  MWAWEntry const &cEntry=m_state->getZoneEntry(child);
  if (cEntry.valid()) {
    cEntry.setParsed(true);
    input->seek(cEntry.begin(), librevenge::RVNG_SEEK_SET);
    pos=input->tell();
    f.str("");
    f << "FontName[Z" << child << "-" << id << "]:";
    auto sSz=int(input->readULong(1));
    if (sSz+1>cEntry.length()) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readFontName: the string size seems bad\n"));
      f << "###sSz=" << sSz << ",";
    }
    else {
      std::string name;
      for (int i=0; i<sSz; ++i) name+=char(input->readULong(1));
      f << name << ",";
      if (!name.empty())
        m_state->m_idToFontIdMap[id]=getFontConverter()->getId(name);
    }
    if (input->tell()!=cEntry.end())
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    ascii().addPos(cEntry.end());
    ascii().addNote("_");
  }
  else {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readFontName: can not find the font name\n"));
  }
  return true;
}

bool PowerPoint3Parser::readParagraph(MWAWParagraph &para, PowerPoint3ParserInternal::Ruler const &ruler, int schemeId)
{
  bool isMacFile=m_state->m_isMacFile;
  para=MWAWParagraph();
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+24)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readParagraph: the zone seems bad\n"));
    ruler.updateParagraph(para, 0);
    return false;
  }
  libmwaw::DebugStream f;
  f << "fl=[";
  bool hasBullet=false;
  int val;
  char bulletChar=0;
  MWAWFont bulletFont;
  if (!isMacFile) {
    if (m_state->m_monoTypeFontId<0)
      m_state->m_monoTypeFontId=getFontConverter()->getId("Monotype Sorts");
    bulletFont.setId(m_state->m_monoTypeFontId);
  }

  for (int j=0; j<4; ++j) { // ?:swap?, 1|23|ff, 6e|95, 0
    val=int(input->readULong(1));
    switch (j) {
    case 1:
      if (hasBullet) {
        MWAWColor color;
        if (val!=255 && getColor(val, schemeId, color)) {
          bulletFont.setColor(color);
          if (!color.isBlack())
            f << "bullet[color]=" << color << ",";
        }
        else if (val!=255) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readParagraph: can not read a color\n"));
          f << "bullet[color]=C" << val << ",";
        }
      }
      break;
    case 2:
      if (hasBullet && val!=255) {
        bulletChar=char(val);
        f << "bullet=" << bulletChar << ",";
      }
      break;
    case 0:
      if (!m_state->m_isMacFile) {
        if (val&1) {
          hasBullet=true;
          f << "bullet[has],";
        }
        val &= 0xfe;
      }
      else {
        if (val&0x40) {
          hasBullet=true;
          f << "bullet[has],";
        }
        val &= 0xbf;
      }
      MWAW_FALLTHROUGH;
    default:
      if (val)
        f << std::hex << val << std::dec << ",";
      else
        f << "_,";
    }
  }
  f << "],";
  f << "unkn=[";
  int level=0;
  for (int j=0; j<10; ++j) { // 0-6, 75|100, 0, 0, 1-4, 0-2, 40-160, 30-60, 0, 0-d
    val=int(input->readLong(2));
    switch (j) {
    case 0:
      if (hasBullet && val >=0) {
        if (m_state->m_idToFontIdMap.find(val)!= m_state->m_idToFontIdMap.end()) {
          bulletFont.setId(m_state->m_idToFontIdMap.find(val)->second);
          f << "bullet[font]=F" << val << ",";
        }
        else {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::readParagraph: can not read a font\n"));
          f << "###bullet[font]=F" << val << ",";
        }
      }
      break;
    case 1:
      if (hasBullet) {
        if (val!=100)
          f << "bullet[size]=" << val << "%,";
        bulletFont.setSize(float(val)/100.f, true);
      }
      break;
    case 4:
      level=val&0xFF;
      ruler.updateParagraph(para, level);
      if (level) f << "level=" << level << ",";
      if (val&0xFF00) f << "level[high]=" << (val>>8) << ",";
      break;
    case 5:
      switch (val) {
      case 0: // left
        break;
      case 1:
        para.m_justify=MWAWParagraph::JustificationCenter;
        break;
      case 2:
        para.m_justify=MWAWParagraph::JustificationRight;
        break;
      case 3:
        para.m_justify=MWAWParagraph::JustificationFull;
        break;
      default:
        MWAW_DEBUG_MSG(("PowerPoint3Parser::readParagraph: find unknown justifcation\n"));
        f << "##justify=" << val << ",";
      }
      break;
    case 6:
      if (val<0)
        para.setInterline(-val, librevenge::RVNG_POINT);
      else if (val>0)
        para.setInterline(double(val)/100., librevenge::RVNG_PERCENT);
      break;
    case 7:
    case 8:
      if (val<0)
        para.m_spacings[j-6]=double(-val)/72.;
      else if (val>0)
        para.m_spacings[j-6]=double(val)/100.*24./72.; // percent assume font=24
      break;
    default:
      if (val)
        f << val << ",";
      else
        f << "_,";
    }
  }
  f << "],";
  if (hasBullet && bulletChar && getMainListener()) {
    para.m_listLevelIndex=level+1;
    para.m_listLevel=MWAWListLevel();
    para.m_listLevel->m_type=MWAWListLevel::BULLET;
    para.m_listLevel->m_spanId=getFontManager()->getId(bulletFont);
    int unicode=getFontConverter()->unicode(bulletFont.id(), static_cast<unsigned char>(bulletChar));
    libmwaw::appendUnicode(unicode==-1 ? 0x2022 : uint32_t(unicode), para.m_listLevel->m_bullet);
  }
  para.m_extra=f.str();
  input->seek(pos+24, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PowerPoint3Parser::readRuler(MWAWEntry const &entry, int pId)
{
  if (!entry.valid() || entry.length()<26) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readRuler: the zone seems bad\n"));
    return false;
  }
  PowerPoint3ParserInternal::Ruler ruler;
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  ascii().addPos(entry.end());
  ascii().addNote("_");
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Ruler)[Z"<< entry.id() << "-R" << pId << "]:,";
  for (auto &margin : ruler.m_margins) margin=int(input->readLong(2));
  int val;
  for (int i=0; i<2; ++i) { // f0=1-3, f1=223,240, 242
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto N=int(input->readULong(2));
  if (26+4*N>entry.length()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readRuler: the N value seems bad\n"));
    f << "###N=" << N << ",";
    N=0;
  }
  for (int i=0; i<N; ++i) {
    MWAWTabStop tab;
    tab.m_position=double(input->readLong(2))/8./72.;
    val=int(input->readLong(2));
    switch (val) {
    case 0:
      tab.m_alignment=MWAWTabStop::DECIMAL;
      break;
    case 1:
      tab.m_alignment=MWAWTabStop::RIGHT;
      break;
    case 2:
      tab.m_alignment=MWAWTabStop::CENTER;
      break;
    case 3: // left
      break;
    default:
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readRuler: find unknown tab position\n"));
      f << "##tab" << i << "=" << val << ",";
      break;
    }
    ruler.m_paragraph.m_tabs->push_back(tab);
  }
  f << ruler << ",";
  if (m_state->m_idToRulerMap.find(pId)==m_state->m_idToRulerMap.end())
    m_state->m_idToRulerMap[pId]=ruler;
  else {
    f << "###dup,";
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readRuler: paragraph %d already exists\n", pId));
  }
  if (input->tell()!=entry.end()) {
    f << "#extra,";
    ascii().addDelimiter(input->tell(),'|');
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint3Parser::readPictureMain(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()!=16) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureMain: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(PictMain)[Z"<< entry.id() << "]:";
  auto numZones=int(m_state->m_zonesList.size());
  auto val=int(input->readULong(4));
  int child=-1;
  if (val) {
    if (val>=0 && val<numZones) {
      f << "picture[id]=Z" << val << ",";
      child=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readPictureMain: find some bad child\n"));
      f << "picture[id]=##Z" << val << ",";
    }
  }
  f << "ids=[";
  for (int i=0; i<3; ++i)
    f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  MWAWEntry const &cEntry=m_state->getZoneEntry(child);
  if (cEntry.valid()) {
    PowerPoint3ParserInternal::ListZoneIdParser parser(numZones, "Picture");
    if (readStructList(cEntry, parser)) {
      m_state->m_pictIdToZoneIdMap=parser.m_fieldIdToZoneIdMap;
      readPicturesList(parser.m_fieldIdToZoneIdMap);
    }
  }
  return true;
}

bool PowerPoint3Parser::readColors(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%8)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readColors: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Color)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<3; ++i) { // can be big numbers
    val=int(input->readULong(2));
    if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (8+(N+1)*8 != int(entry.length())) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readColors: the N value seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  // cmyk picker 32-33-34-35
  for (int i=0; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Color-C" << i << ":";
    val=int(input->readLong(2));
    if (val) {
      unsigned char col[3];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
      MWAWColor color(col[0],col[1],col[2]);
      m_state->m_idToUserColorMap[i]=color;
      f << color << ",";
    }
    else
      f << "_,";
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool PowerPoint3Parser::readColorZone(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()<48) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readColorZone: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Color)[Z"<< entry.id() << "]:menu,";
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (48+2*N!=entry.length()) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readColorZone: the N number seems bad\n"));
    f << "###";
    N=int(entry.length()/2)-24;
  }
  auto val=int(input->readLong(2)); // 8-a
  if (val) f << "f0=" << val << ",";
  auto numZones=int(m_state->m_zonesList.size());
  val=int(input->readULong(4));
  int child=-1;
  if (val) {
    if (val>=0 && val<numZones) {
      f << "child[id]=Z" << val << ",";
      child=val;
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::readColorZone: find some bad child\n"));
      f << "child[id]=##Z" << val << ",";
    }
  }
  //unsure, look like a list of flags?
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+46, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Color-A:menu,used=[";
  for (int i=0; i<N; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  val=int(input->readULong(2)); // big number
  if (val) f << "f0=" << std::hex << val << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(entry.end());
  ascii().addNote("_");
  MWAWEntry const &cEntry=m_state->getZoneEntry(child);
  if (cEntry.valid() && !cEntry.isParsed())
    readColors(cEntry);
  return true;
}

bool PowerPoint3Parser::readZone9(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()!=34) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readZone9: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Zone9)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<5; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {1,1,0,0,0};
    if (val!=expected[i]) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<8; ++i) {
    val=int(input->readLong(1));
    int const expected[]= {0,1,0,0,1,0,2,0};
    if (val!=expected[i]) f << "fl" << i << "=" << val << ",";
  }
  for (int i=0; i<5; ++i) { // g0=4|2000
    val=int(input->readLong(2));
    int const expected[]= {0,48,48,0,0};
    if (val!=expected[i]) f << "g" << i << "=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readULong(1)); // 1|80
  if (val) f << "fl8=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) { // h1=0|3000
    val=int(input->readULong(2));
    if (val) f << "h" << i << "=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readLong(1)); // 0
  if (val) f << "fl9=" << std::hex << val << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint3Parser::readZone10(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%206)!=12) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readZone10: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Zone10)[Z"<< entry.id() << "]:";
  f << "ids=[";
  for (int i=0; i<3; ++i)
    f << std::hex << input->readLong(4) << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  auto N=int(entry.length()/206);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Zone10A[" << i << "]:";
    auto val=int(input->readLong(2)); // -1|0-5
    if (val) f << "f0=" << val << ",";
    for (int k=0; k<2; ++k) { // fl0=[347c][23cd]
      val=int(input->readULong(1));
      if (val) f << "fl" << k << "=" << std::hex << val << std::dec << ",";
    }
    val=int(input->readULong(2)); // 8001|801f
    if (val!=0x801f) f << "fl2=" << std::hex << val << std::dec << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    for (int j=0; j<5; ++j) {
      pos=input->tell();
      f.str("");
      f << "Zone10B[" << i << "-" << j << "]:";
      for (int k=0; k<6; ++k) { // f0=[0-3] fontid?, f2=0|1, f3=1|3
        val=int(input->readLong(2));
        if (!val) continue;
        if (k==1) f << "font[sz]=" << val << ",";
        else f << "f" << k << "=" << val << ",";
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    for (int j=0; j<5; ++j) {
      pos=input->tell();
      f.str("");
      f << "Zone10C[" << i << "-" << j << "]:";
      for (int k=0; k<4; ++k) { // fl0=0|1|15|40|54, fl1=1|ff, fl2=[9-d][058b]
        val=int(input->readULong(1));
        if (val) f << "fl" << k << "=" << std::hex << val << std::dec << ",";
      }
      for (int k=0; k<12; ++k) { // f0=0-2, f1=100, f4=0-4, f5=0|1, f6=90|100, f7=0|20|30|40, f9=0|1
        val=int(input->readLong(2));
        if (val) f << "f" << k << "=" << val << ",";
      }
      input->seek(pos+28, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}
////////////////////////////////////////////////////////////
// try to send data
////////////////////////////////////////////////////////////
bool PowerPoint3Parser::sendText(PowerPoint3ParserInternal::SlideContent const &content, int tId, bool mainText, bool master)
{
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::sendText: can not find the listener\n"));
    return false;
  }
  auto numTZones=int(content.m_textZone.size());
  if (mainText) tId+=numTZones-content.m_numMainZones;
  int minTId=tId, maxTId=tId;
  if (tId==-1) {
    minTId=0;
    maxTId=numTZones-1;
  }
  else if (tId<0 || tId>=numTZones) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::sendText: oops the textId=%d seems bad\n", tId));
    return false;
  }
  int const schemeId=content.m_schemeId;
  for (int id=minTId; id<=maxTId; ++id) {
    auto const &textZone=content.m_textZone[size_t(id)];
    MWAWInputStreamPtr input = getInput();
    long pos;

    // paragraph
    PowerPoint3ParserInternal::Ruler ruler;
    if (m_state->m_idToRulerMap.find(textZone.m_rulerId)!=m_state->m_idToRulerMap.end())
      ruler=m_state->m_idToRulerMap.find(textZone.m_rulerId)->second;
    else {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::sendText: can not find paragraph %d\n", textZone.m_rulerId));
    }

    MWAWEntry const &rEntry=textZone.m_rulers;
    int N=(rEntry.length()%28)==0 ? int(rEntry.length()/28) : 0;
    libmwaw::DebugStream f;
    std::map<int, MWAWParagraph> posToParagraphMap;
    input->seek(rEntry.begin(), librevenge::RVNG_SEEK_SET);
    int cPos=0;
    for (int i=0; i<N; ++i) {
      pos=input->tell();
      f.str("");
      f << "TextZone-R[" << i << "]:";
      auto cLen=int(input->readULong(4));
      f << "pos=" << cPos << "<->" << long(cPos)+cLen << ",";
      MWAWParagraph para;
      if (readParagraph(para, ruler, schemeId)) {
        if (posToParagraphMap.find(cPos)!=posToParagraphMap.end()) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::sendText: oops, find duplicated position\n"));
          f << "##dup,";
        }
        f << para;
      }
      else
        f << "###";
      posToParagraphMap[cPos]=para;
      if (cLen < 0 || cLen > textZone.m_text.length() - cPos) // there can't be any para that long
        break;
      cPos+=cLen;
      input->seek(pos+28, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }

    // fonts
    MWAWEntry const &fEntry=textZone.m_fonts;
    N=(fEntry.length()%16)==0 ? int(fEntry.length()/16) : 0;
    std::map<int, MWAWFont> posToFontMap;
    input->seek(fEntry.begin(), librevenge::RVNG_SEEK_SET);
    cPos=0;
    for (int i=0; i<N; ++i) {
      pos=input->tell();
      f.str("");
      f << "TextZone-F[" << i << "]:";
      auto cLen=int(input->readULong(4));
      f << "pos=" << cPos << "<->" << long(cPos)+cLen << ",";
      MWAWFont font;
      if (readFont(font, schemeId)) {
        if (posToFontMap.find(cPos)!=posToFontMap.end()) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::sendText: oops, find duplicated position\n"));
          f << "##dup,";
        }
        else
          posToFontMap[cPos]=font;
        if (cLen < 0 || cLen > textZone.m_text.length() - cPos) // there can't be any span that long
          break;
        cPos+=cLen;
        f << font.getDebugString(getFontConverter());
      }
      else
        f << "###";
      input->seek(pos+16, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }

    if (id!=minTId) listener->insertEOL();
    MWAWEntry const &tEntry=textZone.m_text;
    input->seek(tEntry.begin(), librevenge::RVNG_SEEK_SET);
    for (int i=0; i<int(tEntry.length()); ++i) {
      if (posToParagraphMap.find(i)!=posToParagraphMap.end())
        listener->setParagraph(posToParagraphMap.find(i)->second);
      if (posToFontMap.find(i)!=posToFontMap.end())
        listener->setFont(posToFontMap.find(i)->second);
      auto c=static_cast<unsigned char>(input->readULong(1));
      switch (c) {
      case 0x9:
        listener->insertTab();
        break;
      case 0xb:
      case 0xd:
        listener->insertEOL(c==0xb);
        break;
      case 0x11: // command key
        listener->insertUnicode(0x2318);
        break;
      // special, if dupplicated, this is a field
      case '/': // date
      case ':': // time
      case '#': { // page number
        pos=input->tell();
        if (master && i+1<int(tEntry.length()) && char(input->readULong(1))==char(c)) {
          ++i;
          listener->insertField(MWAWField(c=='#' ? MWAWField::PageNumber : c=='/' ? MWAWField::Date : MWAWField::Time));
        }
        else {
          input->seek(pos, librevenge::RVNG_SEEK_SET);
          listener->insertCharacter(c);
        }
        break;
      }
      default:
        listener->insertCharacter(c);
        break;
      }
    }
  }
  return true;
}

void PowerPoint3Parser::sendSlides()
{
  MWAWPresentationListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::sendSlides: can not find the listener\n"));
    return;
  }
  if (m_state->m_slidesIdList.empty())
    return;

  // first send the master page
  if (m_state->m_zoneIds[2]>=0) {
    MWAWPageSpan ps(getPageSpan());
    ps.setMasterPageName(librevenge::RVNGString("Master"));
    if (!listener->openMasterPage(ps)) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::sendSlides: can not create the master page\n"));
    }
    else {
      int id=m_state->m_zoneIds[2];
      if (m_state->m_idToSlideMap.find(id)!=m_state->m_idToSlideMap.end()) {
        int cId=m_state->m_idToSlideMap.find(id)->second.m_contentIds[0];
        if (m_state->m_idToSlideContentMap.find(cId)!=m_state->m_idToSlideContentMap.end())
          sendSlide(m_state->m_idToSlideContentMap.find(cId)->second, true);
      }
      listener->closeMasterPage();
    }
  }

  for (size_t i=0; i<m_state->m_slidesIdList.size(); ++i) {
    if (i>0)
      listener->insertBreak(MWAWListener::PageBreak);
    int id=m_state->m_slidesIdList[i];
    if (m_state->m_idToSlideMap.find(id)==m_state->m_idToSlideMap.end())
      continue;
    int cId=m_state->m_idToSlideMap.find(id)->second.m_contentIds[0];
    if (m_state->m_idToSlideContentMap.find(cId)==m_state->m_idToSlideContentMap.end())
      continue;
    sendSlide(m_state->m_idToSlideContentMap.find(cId)->second, false);
    // now try to send the note
    cId=m_state->m_idToSlideMap.find(id)->second.m_contentIds[1];
    if (m_state->m_idToSlideContentMap.find(cId)==m_state->m_idToSlideContentMap.end())
      continue;
    auto const &note=m_state->m_idToSlideContentMap.find(cId)->second;
    if (!note.hasText()) continue;
    MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(200,200), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    MWAWSubDocumentPtr doc(new PowerPoint3ParserInternal::SubDocument(*this, getInput(), &note));
    listener->insertSlideNote(pos, doc);
  }
}

bool PowerPoint3Parser::sendSlide(PowerPoint3ParserInternal::SlideContent const &slide, bool master)
{
  MWAWPresentationListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::sendSlide: can not find the listener\n"));
    return false;
  }
  // first is title, better to remove it in the master slide
  for (size_t f=0; f<slide.m_frameList.size(); ++f) {
    if (slide.m_frameList[f].m_isSent) continue;
    if (master && slide.m_frameList[f].m_mainTextBox) continue;
    std::set<int> seen;
    seen.insert(int(f));
    sendFrame(slide.m_frameList[f], slide, master, seen);
  }
  return true;
}

bool PowerPoint3Parser::sendFrame(PowerPoint3ParserInternal::Frame const &frame, PowerPoint3ParserInternal::SlideContent const &content, bool master, std::set<int> &seen)
{
  frame.m_isSent=true;
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: can not find the listener\n"));
    return false;
  }

  MWAWBox2f fBox(1.f/8.f*MWAWVec2f(frame.m_dimension[0]+m_state->m_origin),
                 1.f/8.f*MWAWVec2f(frame.m_dimension[1]+m_state->m_origin));
  if (frame.m_textId>=0) {
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    MWAWSubDocumentPtr subdoc(new PowerPoint3ParserInternal::SubDocument(*this, getInput(), &content, frame.m_textId, frame.m_mainTextBox, master));
    listener->insertTextBox(pos, subdoc, frame.m_style);
    return true;
  }
  if (frame.m_pictureId>=0) {
    if (m_state->m_pictIdToZoneIdMap.find(frame.m_pictureId)==m_state->m_pictIdToZoneIdMap.end()) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: can not find the picture %d\n", frame.m_pictureId));
      return false;
    }
    int zId=m_state->m_pictIdToZoneIdMap.find(frame.m_pictureId)->second;
    if (m_state->m_idToPictureContentMap.find(zId)==m_state->m_idToPictureContentMap.end()) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: can not find the picture %d in Z%d\n", frame.m_pictureId, zId));
      return false;
    }
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->insertPicture(pos, m_state->m_idToPictureContentMap.find(zId)->second);
    return true;
  }
  MWAWGraphicShape shape;
  switch (frame.m_type) {
  case 0:
    shape=MWAWGraphicShape::line(fBox[0], fBox[1]);
    break;
  case 1: {
    if (frame.m_customShapeId>=0) {
      if (!m_state->getCustomShape(frame.m_customShapeId, shape))
        return false;
      if (frame.m_customTransformation&3)
        shape=shape.rotate(-90.f*float(frame.m_customTransformation&3), MWAWVec2f(0.5f,0.5f));
      if (frame.m_customTransformation&4) {
        shape.translate(MWAWVec2f(-0.5f,-0.5f));
        shape.scale(MWAWVec2f(-1,1));
        shape.translate(MWAWVec2f(0.5f,0.5f));
      }
      shape.scale(fBox.size());
      shape.translate(fBox[0]);
      break;
    }
    switch (frame.m_shapeId) {
    case 1:
      shape=MWAWGraphicShape::circle(fBox);
      break;
    case 2:
      shape=MWAWGraphicShape::rectangle(fBox, MWAWVec2f(3,3));
      break;
    case 3:
      shape=MWAWGraphicShape::rectangle(fBox);
      break;
    default:
      return false;
    }
    break;
  }
  case 2: {
    if (frame.m_polygonId<0||frame.m_polygonId>=int(content.m_polygonList.size())) {
      MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: can not find the polygon %d\n", frame.m_polygonId));
      return false;
    }
    auto const &poly=content.m_polygonList[size_t(frame.m_polygonId)];
    if (!poly.updateShape(fBox, shape)) return false;
    break;
  }
  case 3: {
    float angle[2] = { frame.m_angles[0], frame.m_angles[0]+frame.m_angles[1] };
    if (angle[1]<angle[0])
      std::swap(angle[0],angle[1]);
    if (angle[1]>360) {
      int numLoop=int(angle[1]/360)-1;
      angle[0]-=float(numLoop*360);
      angle[1]-=float(numLoop*360);
      while (angle[1] > 360) {
        angle[0]-=360;
        angle[1]-=360;
      }
    }
    if (angle[0] < -360) {
      int numLoop=int(angle[0]/360)+1;
      angle[0]-=float(numLoop*360);
      angle[1]-=float(numLoop*360);
      while (angle[0] < -360) {
        angle[0]+=360;
        angle[1]+=360;
      }
    }
    MWAWVec2f center = fBox.center();
    MWAWVec2f axis = 0.5f*MWAWVec2f(fBox.size());
    // we must compute the real bd box
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; i++)
      limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; bord++) {
      float ang = (bord == limitAngle[0]) ? angle[0] :
                  (bord == limitAngle[1]+1) ? angle[1] : 90 * float(bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { axis[0] *std::cos(ang), -axis[1] *std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    MWAWBox2f realBox(MWAWVec2f(center[0]+minVal[0],center[1]+minVal[1]),
                      MWAWVec2f(center[0]+maxVal[0],center[1]+maxVal[1]));
    shape = MWAWGraphicShape::pie(realBox, fBox, MWAWVec2f(float(angle[0]),float(angle[1])));
    break;
  }
  case 4: {
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
    auto numGroups=int(content.m_frameList.size());
    // check if the group child list is not broken
    bool ok=true;
    for (auto id : seen) {
      if (frame.m_groupChild[0]<=id && id<=frame.m_groupChild[1]) {
        MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: oops the child list seems broken\n"));
        ok=false;
        break;
      }
    }
    if (ok) {
      for (int i=frame.m_groupChild[0]; i<=frame.m_groupChild[1]; ++i) {
        if (i<0||i>=numGroups || seen.find(i)!=seen.end()) {
          MWAW_DEBUG_MSG(("PowerPoint3Parser::sendFrame: group %d seens bad\n", i));
          continue;
        }
        seen.insert(i);
        sendFrame(content.m_frameList[size_t(i)], content, master, seen);
        seen.erase(i);
      }
    }
    listener->closeGroup();
    return true;
  }
  default:
    shape=MWAWGraphicShape::rectangle(fBox);
    break;
  }
  MWAWBox2f box=shape.getBdBox();
  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  listener->insertShape(pos, shape, frame.m_style);

  return true;
}
////////////////////////////////////////////////////////////
// Low level
////////////////////////////////////////////////////////////

// read the header
bool PowerPoint3Parser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = PowerPoint3ParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;
  if (input->isStructured()) {
    input=input->getSubStreamByName("PP40");
    if (!input)
      return false;
  }
  libmwaw::DebugStream f;
  if (!input->checkPosition(24+8)) {
    MWAW_DEBUG_MSG(("PowerPoint3Parser::checkHeader: file is too short\n"));
    return false;
  }
  long pos = 0;
  input->setReadInverted(false);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  unsigned long signature=input->readULong(4);
  if (signature==0xeddead0b) {
    input->setReadInverted(true);
    m_state->m_isMacFile=false;
  }
  else if (signature!=0xbaddeed)
    return false;
  f << "FileHeader:";
  auto vers=int(input->readLong(4));
  if (vers!=3 && vers!=4) return false;
  m_state->m_zoneListBegin=long(input->readULong(4));
  if (m_state->m_zoneListBegin<24 || !input->checkPosition(m_state->m_zoneListBegin))
    return false;
  f << "zone[begin]=" << std::hex << m_state->m_zoneListBegin << std::dec << ",";

  if (strict) {
    input->seek(12, librevenge::RVNG_SEEK_SET);
    auto val=int(input->readULong(2));
    if (!input->checkPosition(m_state->m_zoneListBegin+val*8))
      return false;
  }
  input->seek(12, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_POWERPOINT, vers, MWAWDocument::MWAW_K_PRESENTATION);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
