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
#include <sstream>
#include <stack>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "WingzParser.hxx"

#include "WingzGraph.hxx"

/** Internal: the structures of a WingzGraph */
namespace WingzGraphInternal
{
//! Internal a Graphic of a WingzGraph
struct Graphic {
  //! the constructor
  Graphic()
    : m_type(-1)
    , m_order(-1)
    , m_position()
    , m_relativePosition()
    , m_style(MWAWGraphicStyle::emptyStyle())
    , m_vertices()
    , m_children()

    , m_textType(-1)
    , m_textEntry()
    , m_fontList()
    , m_posToFontId()
    , m_paragraph()

    , m_flag(0)
  {
    for (auto &angle : m_angles) angle=0;
  }
  //! the file type
  int m_type;
  //! the order
  int m_order;
  //! the cell
  MWAWBox2i m_position;
  //! the relative position (% of cell)
  MWAWBox2f m_relativePosition;
  //! the graphic style
  MWAWGraphicStyle m_style;
  //! the angles: for arc
  float m_angles[2];
  //! the vertices list: poly (% of box)
  std::vector<MWAWVec2f> m_vertices;
  //! the children: group
  std::vector<std::shared_ptr<Graphic> > m_children;
  //! the data : if picture 0: data, if textbox/button 0:button, 1:title
  MWAWEntry m_entry[2];
  //! the name/title basic font
  MWAWFont m_font[2];

  // textbox
  //! the textbox type
  int m_textType;
  //! the textbox entry
  MWAWEntry m_textEntry;
  //! list of fonts: textbox
  std::vector<MWAWFont> m_fontList;
  //! map pos to fontId
  std::map<int, size_t> m_posToFontId;
  //! the paragraph: textbox
  MWAWParagraph m_paragraph;

  //! some flag (depending of type)
  int m_flag;
};

////////////////////////////////////////
//! Internal: the state of a WingzGraph
struct State {
  //! constructor
  State()
    : m_patternList()
    , m_pictureList()
    , m_groupStack()
    , m_inGroupDepth(0)
  {
  }
  //! init the pattern list
  void initPatterns(int vers);
  //! add a new graphic
  void addGraphic(std::shared_ptr<WingzGraphInternal::Graphic> graphic)
  {
    if (!m_groupStack.empty() && m_groupStack.top())
      m_groupStack.top()->m_children.push_back(graphic);
    else
      m_pictureList.push_back(graphic);
  }
  //! the patterns list
  std::vector<MWAWGraphicStyle::Pattern> m_patternList;
  //! the list of picture
  std::vector<std::shared_ptr<Graphic> > m_pictureList;
  //! the group stack
  std::stack<std::shared_ptr<Graphic> > m_groupStack;
  //! the group actual depth
  int m_inGroupDepth;
};

void State::initPatterns(int vers)
{
  if (!m_patternList.empty())
    return;
  static uint16_t const patternsWingz[] = {
    0x0,0x0,0x0,0x0/*none*/, 0xffff,0xffff,0xffff,0xffff, 0xfffb,0xffbf,0xfffb,0xffbf, 0xff77,0xffdd,0xff77,0xffdd,
    0x4411,0x4411,0x4411,0x4411, 0xfffb,0xfffb,0xfffb,0xfffb, 0x3333,0x3333,0x3333,0x3333, 0xfcf9,0xf3e7,0xcf9f,0x3f8e,
    0x1111,0x1111,0x1111,0x1111, 0x1881,0xb136,0x0660,0x631b, 0x2004,0x8010,0x0108,0x4002, 0x1010,0x1010,0x1010,0x01ff,
    0x0101,0x01ff,0x1010,0x10ff,
    0x0001,0x0010,0x0001,0x0010, 0x8040,0x2000,0x0001,0x0204, 0x7088,0x0505,0x0588,0x7002, 0xc7ab,0x11ba,0x7cba,0x91eb,
    0x1010,0x3844,0x8283,0x4428, 0x8142,0x2424,0x2424,0x1800, 0x007e,0x7e62,0x6262,0x7e00, 0x0000,0x0000,0x0000,0x0000,
    0x0000,0x0010,0x0000,0x0001, 0x0001,0x0010,0x0001,0x0010, 0x0044,0x0011,0x0044,0x0011, 0x0011,0x0011,0x0011,0x0011,
    0x00ff,0x00ff,0x00ff,0x00ff,
    0x1122,0x4488,0x1122,0x4488, 0x000f,0x000f,0x000f,0x000f, 0x1020,0x4080,0x1020,0x4080, 0x4000,0x40aa,0x4000,0x4000,
    0x4040,0x40ff,0x4040,0x4040, 0x1028,0x4482,0x0102,0x0408, 0x0814,0x2241,0x8800,0xaa00, 0x40a0,0x0000,0x040a,0x0000,
    0x8004,0x040a,0x1221,0xa030, 0xa141,0x221a,0x0808,0x1422, 0x0102,0x0408,0x102a,0x66ff, 0x62e3,0xe3dd,0x263e,0x3edd,
    0x0502,0x0002,0x058a,0x558a
  };
  static uint16_t const patternsResolve[] = {
    0x0, 0x0, 0x0, 0x0, 0xffff, 0xffff, 0xffff, 0xffff, 0x7fff, 0xffff, 0xf7ff, 0xffff, 0x7fff, 0xf7ff, 0x7fff, 0xf7ff,
    0xffee, 0xffbb, 0xffee, 0xffbb, 0x77dd, 0x77dd, 0x77dd, 0x77dd, 0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x8822, 0x8822, 0x8822, 0x8822,
    0xaa00, 0xaa00, 0xaa00, 0xaa00, 0xaa00, 0x4400, 0xaa00, 0x1100, 0x8800, 0xaa00, 0x8800, 0xaa00, 0x8800, 0x2200, 0x8800, 0x2200,
    0x8000, 0x800, 0x8000, 0x800, 0x0, 0x11, 0x0, 0x11, 0x8000, 0x0, 0x800, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xeedd, 0xbb77, 0xeedd, 0xbb77, 0x3366, 0xcc99, 0x3366, 0xcc99, 0x1122, 0x4488, 0x1122, 0x4488, 0x8307, 0xe1c, 0x3870, 0xe0c1,
    0x306, 0xc18, 0x3060, 0xc081, 0x102, 0x408, 0x1020, 0x4080, 0xffff, 0x0, 0x0, 0x0, 0xff00, 0x0, 0x0, 0x0,
    0x77bb, 0xddee, 0x77bb, 0xddee, 0x99cc, 0x6633, 0x99cc, 0x6633, 0x8844, 0x2211, 0x8844, 0x2211, 0xe070, 0x381c, 0xe07, 0x83c1,
    0xc060, 0x3018, 0xc06, 0x381, 0x8040, 0x2010, 0x804, 0x201, 0xc0c0, 0xc0c0, 0xc0c0, 0xc0c0, 0x8080, 0x8080, 0x8080, 0x8080,
    0xffaa, 0xffaa, 0xffaa, 0xffaa, 0xe4e4, 0xe4e4, 0xe4e4, 0xe4e4, 0xffff, 0xff00, 0xff, 0x0, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,
    0xff00, 0xff00, 0xff00, 0xff00, 0xff00, 0x0, 0xff00, 0x0, 0x8888, 0x8888, 0x8888, 0x8888, 0xff80, 0x8080, 0x8080, 0x8080,
    0x4ecf, 0xfce4, 0x473f, 0xf372, 0x6006, 0x36b1, 0x8118, 0x1b63, 0x2004, 0x4002, 0x1080, 0x801, 0x9060, 0x609, 0x9060, 0x609,
    0x8814, 0x2241, 0x8800, 0xaa00, 0x2050, 0x8888, 0x8888, 0x502, 0xaa00, 0x8000, 0x8800, 0x8000, 0x2040, 0x8000, 0x804, 0x200,
    0xf0f0, 0xf0f0, 0xf0f, 0xf0f, 0x77, 0x7777, 0x77, 0x7777, 0xff88, 0x8888, 0xff88, 0x8888, 0xaa44, 0xaa11, 0xaa44, 0xaa11,
    0x8244, 0x2810, 0x2844, 0x8201, 0x8080, 0x413e, 0x808, 0x14e3, 0x8142, 0x2418, 0x1020, 0x4080, 0x40a0, 0x0, 0x40a, 0x0,
    0x7789, 0x8f8f, 0x7798, 0xf8f8, 0xf1f8, 0x6cc6, 0x8f1f, 0x3663, 0xbf00, 0xbfbf, 0xb0b0, 0xb0b0, 0xff80, 0x8080, 0xff08, 0x808,
    0x1020, 0x54aa, 0xff02, 0x408, 0x8, 0x142a, 0x552a, 0x1408, 0x55a0, 0x4040, 0x550a, 0x404, 0x8244, 0x3944, 0x8201, 0x101
  };
  MWAWGraphicStyle::Pattern pat;
  pat.m_dim=MWAWVec2i(8,8);
  pat.m_data.resize(8);
  pat.m_colors[0]=MWAWColor::white();
  pat.m_colors[1]=MWAWColor::black();
  uint16_t const *patPtr=vers==2 ? patternsWingz : patternsResolve;
  for (int i=0; i<(vers==2 ? 39 : 64); ++i) {
    for (size_t j=0; j<8; j+=2, ++patPtr) {
      pat.m_data[j]=uint8_t((*patPtr)>>8);
      pat.m_data[j+1]=uint8_t((*patPtr)&0xFF);
    }
    m_patternList.push_back(pat);
  }
}

////////////////////////////////////////
//! Internal: the subdocument of a WingzGraph
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(WingzGraph &pars, MWAWInputStreamPtr const &input, Graphic const &graph)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_graphParser(pars)
    , m_graphic(graph)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the graph parser */
  WingzGraph &m_graphParser;
  /** the graphic */
  Graphic const &m_graphic;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("WingzGraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  long pos = m_input->tell();
  m_graphParser.sendText(m_graphic);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (&m_graphParser != &sDoc->m_graphParser) return true;
  if (&m_graphic != &sDoc->m_graphic) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
WingzGraph::WingzGraph(WingzParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new WingzGraphInternal::State)
  , m_mainParser(&parser)
{
}

WingzGraph::~WingzGraph()
{ }

int WingzGraph::version() const
{
  return m_parserState->m_version;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read a graphic zone
////////////////////////////////////////////////////////////
bool WingzGraph::readGraphic()
{
  auto input = m_parserState->m_input;
  auto &ascFile = m_parserState->m_asciiFile;
  long pos = input->tell();
  if (!input->checkPosition(pos+60)) {
    MWAW_DEBUG_MSG(("WingzGraph::readGraphic: the header seems bad\n"));
    return false;
  }
  auto graphic=std::make_shared<WingzGraphInternal::Graphic>();
  auto type=static_cast<int>(input->readULong(1));
  if (type!=0xe) return false;
  auto fl=static_cast<int>(input->readULong(1));
  auto dSz=static_cast<int>(input->readULong(2));
  int id= (fl==0) ? 0 : static_cast<int>(input->readULong(2));
  libmwaw::DebugStream f;
  f << "Entries(Graphic):";
  if (fl!=0x80) f << "fl=" << std::hex << fl << std::dec << ",";
  if (id) f << "id=" << id << ",";
  long actPos=input->tell();
  auto nSz=static_cast<int>(input->readULong(1));
  if (nSz>15) {
    MWAW_DEBUG_MSG(("WingzGraph::readGraphic: the graphic title seems bad\n"));
    f << "#nSz=" << nSz << ",";
  }
  else if (nSz) {
    std::string name("");
    for (int i=0; i<nSz; ++i) name+=char(input->readULong(1));
    f << name << ",";
  }
  input->seek(actPos+16, librevenge::RVNG_SEEK_SET);
  graphic->m_order=static_cast<int>(input->readULong(2));
  f << "order=" << graphic->m_order << ",";
  auto val=static_cast<int>(input->readULong(2)); // always 0
  if (val) f << "f1=" << val << ",";
  // the position seem to be stored as cell + % of the cell width...
  float decal[4];
  for (auto &d : decal) d=static_cast<float>(input->readULong(1))/255.f;
  graphic->m_relativePosition=MWAWBox2f(MWAWVec2f(decal[2],decal[0]),MWAWVec2f(decal[3],decal[1]));
  int dim[4]; // the cells which included the picture
  for (auto &d : dim) d=static_cast<int>(input->readULong(2));
  graphic->m_position=MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  f << "dim=" << dim[0] << ":" << decal[2] << "x"
    << dim[1] << ":" << decal[0] << "<->"
    << dim[2] << ":" << decal[3] << "x"
    << dim[3] << ":" << decal[1] << ",";
  type=graphic->m_type=static_cast<int>(input->readULong(2));
  val=static_cast<int>(input->readULong(2)); // always 0
  if (val) f << "f2=" << val << ",";

  long endPos=pos+(version()==1 ? 4 : 8)+dSz;
  long dataPos=input->tell();
  if (type==0 || type==2) {
    for (int i=0; i<2; ++i) { // name, title
      auto sSz=static_cast<int>(input->readULong(1));
      if (!input->checkPosition(input->tell()+sSz+1)) {
        MWAW_DEBUG_MSG(("WingzGraph::readGraphic: can not find the textbox name%d\n", i));
        return false;
      }
      if (!sSz)
        continue;
      graphic->m_entry[i].setBegin(input->tell());
      graphic->m_entry[i].setLength(sSz);
      std::string name("");
      for (int c=0; c<sSz; ++c) name+=char(input->readULong(1));
      f << name << ",";
    }
    auto hasMacro=static_cast<int>(input->readLong(1));
    if (hasMacro==1) {
      f << "macro,";
      if (!m_mainParser->readMacro()) return false;
    }
    else if (hasMacro) {
      f << "###macro=" << hasMacro << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: can not find the textbox type\n"));
      return false;
    }
  }
  if (type==0 || type==2 || (type>=5 && type<=9)) {
    bool canHaveShadow=true;
    if (type>=5 && type<=9) {
      static int const expectedSize[]= {0x38, 0x3c, 0x34, 0x40, 0x40 };
      if (!input->checkPosition(endPos) || dSz < expectedSize[type-5]) {
        MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find bad size for shape\n"));
        return false;
      }
      canHaveShadow=type==8;
    }
    else if (!input->checkPosition(input->tell()+30)) {
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find bad size for text/button\n"));
      return false;
    }
    int patId;
    MWAWColor color;
    MWAWGraphicStyle::Pattern pattern;
    readPattern(pattern, patId);
    if (patId) {
      if (pattern.getUniqueColor(color)) {
        graphic->m_style.setSurfaceColor(color);
        if (!color.isWhite())
          f << "surf[col]=" << color << ",";
      }
      else {
        f << "surf=" << pattern << ",";
        graphic->m_style.setPattern(pattern);
      }
    }
    else if (!patId)
      f << "surf[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "f0=" << val << ",";
    if (canHaveShadow) {
      graphic->m_flag=val;
      readColor(color, patId);
      if (patId) {
        if (graphic->m_flag&2)
          graphic->m_style.setShadowColor(color);
        f << "shadow[col]=" << color << ",";
      }
      val=int(input->readLong(1));
      if (val) f << "f1=" << val << ",";
    }
    readColor(color, patId);
    bool hasLine=true;
    if (patId && !color.isBlack()) {
      f << "line[col]=" << color << ",";
      graphic->m_style.m_lineColor=color;
    }
    else if (!patId) {
      hasLine=false;
      f << "line[col]=none,";
    }
    val=int(input->readLong(1));
    if (val!=1) f << "f2=" << val << ",";

    val=static_cast<int>(input->readLong(2));
    if (hasLine) graphic->m_style.m_lineWidth=float(val)/20.f;
    if (val!=5) f << "line[w]=" << float(val)/20.f << ",";
    if (canHaveShadow) {
      for (int i=0; i<2; ++i) dim[i]=int(input->readLong(2));
      if (dim[0]!=20 || dim[1]!=20) {
        graphic->m_style.m_shadowOffset=MWAWVec2f(float(dim[0])/20.f,float(dim[1])/20.f);
        f << "shadow[pos]=" << graphic->m_style.m_shadowOffset << ",";
      }
    }
  }
  switch (type) {
  case 0: // button
  case 2: { // textbox
    f << "TextZone,g0=" << std::hex << dSz << std::dec << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return readTextZone(graphic);
  }
  case 4:
    f << "Chart,";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return readChartData(graphic);
  case 5:
  case 6:
  case 7:
  case 8:
  case 9: {
    static int const expectedSize[]= {0x38, 0x3c, 0x34, 0x40, 0x40 };
    if (!input->checkPosition(endPos) || dSz < expectedSize[type-5]) {
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find bad size for shape\n"));
      return false;
    }
    static char const *what[]= {"line", "arc", "circle", "rectangle", "poly" };
    f << what[type-5] << ",";
    switch (type) {
    case 5: {
      int arrowWidth=static_cast<int>(input->readLong(2));
      if (arrowWidth!=0x21c) f << "arrow[size]=" << double(arrowWidth)/20. << ","; // default 27 point
      val=static_cast<int>(input->readULong(2));
      if (val&0x40) {
        f << "start[arrow],";
        graphic->m_style.m_arrows[0]=
          MWAWGraphicStyle::Arrow(float(arrowWidth)/20.f, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(3000,3000)),
                                  "M1500 0l1500 3000h-3000zM1500 447l-1176 2353h2353z", false);
      }
      if (val&0x80) {
        f << "start[end],";
        graphic->m_style.m_arrows[1]=
          MWAWGraphicStyle::Arrow(float(arrowWidth)/20.f, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(3000,3000)),
                                  "M1500 0l1500 3000h-3000zM1500 447l-1176 2353h2353z", false);
      }
      graphic->m_flag=(val&3);
      if (graphic->m_flag&3) f << "rot=" << (graphic->m_flag&3) << ",";
      val &= 0xff3c;
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      break;
    }
    case 6: {
      for (int i=0; i<2; ++i) { // 0|3fff
        val=int(input->readULong(2));
        if (!val) continue;
        if (val==0x3fff)
          f << "h" << i << "*,";
        else
          f << "h" << i << "=" << val << ",";
      }
      for (auto &angle : graphic->m_angles) angle=float(input->readLong(2))/10.f;
      f << "angles=" << MWAWVec2f(graphic->m_angles[0],graphic->m_angles[1]) << ",";
      break;
    }
    case 7:
      break;
    case 8: {
      break;
    }
    case 9: {
      val=static_cast<int>(input->readULong(2));
      if (val&1) f << "closed,";
      if (val&2) f << "smooth,";
      graphic->m_flag=val;
      int arrowWidth=static_cast<int>(input->readLong(2));
      if (arrowWidth!=0x21c) f << "arrow[size]=" << double(arrowWidth)/20. << ","; // default 27 point
      if (val&0x40) {
        f << "start[arrow],";
        graphic->m_style.m_arrows[0]=
          MWAWGraphicStyle::Arrow(float(arrowWidth)/20.f, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(3000,3000)),
                                  "M1500 0l1500 3000h-3000zM1500 447l-1176 2353h2353z", false);
      }
      if (val&0x80) {
        f << "start[end],";
        graphic->m_style.m_arrows[0]=
          MWAWGraphicStyle::Arrow(float(arrowWidth)/20.f, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(3000,3000)),
                                  "M1500 0l1500 3000h-3000zM1500 447l-1176 2353h2353z", false);
      }
      val &= 0xff3c;
      if (val) f << "h0=" << val << ",";
      int nbPt=int(input->readULong(2));
      f << "nbPt=" << nbPt << ",";
      if (input->tell()+nbPt*4>endPos) {
        f << "###";
        break;
      }
      f << "pts=[";
      for (int i=0; i<nbPt; ++i) {
        float pts[2];
        for (auto &pt : pts) pt=float(input->readULong(2))/float(0x3fff);
        graphic->m_vertices.push_back(MWAWVec2f(pts[0],pts[1]));
        f << graphic->m_vertices.back() << ",";
      }
      f << "],";
      break;
    }
    default:
      break;
    }
    break;
  }
  case 0xa: {
    if (!input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find bad size for picture\n"));
      return false;
    }
    f << "picture,";
    auto pSz=long(input->readULong(2));
    for (int i=0; i<2; ++i) { // g0=0, g1=2
      val=static_cast<int>(input->readULong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    if (!pSz || !input->checkPosition(dataPos+6+pSz)) {
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: can not find the picture data\n"));
      return false;
    }
    graphic->m_entry[0].setBegin(dataPos+6);
    graphic->m_entry[0].setLength(pSz);
#ifdef DEBUG_WITH_FILES
    ascFile.skipZone(dataPos+6, dataPos+6+pSz-1);
    librevenge::RVNGBinaryData file;
    input->seek(dataPos+6, librevenge::RVNG_SEEK_SET);
    input->readDataBlock(pSz, file);
    static int volatile pictName = 0;
    libmwaw::DebugStream f2;
    f2.str("");
    f2 << "PICT-" << ++pictName;
    libmwaw::Debug::dumpFile(file, f2.str().c_str());
#endif
    input->seek(dataPos+6+pSz, librevenge::RVNG_SEEK_SET);
    break;
  }
  case 0xb: {
    if (!input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find bad size for group\n"));
      return false;
    }
    f << "group,";
    break;
  }
  default:
    MWAW_DEBUG_MSG(("WingzGraph::readGraphic: find some unknown type %d\n", type));
    f << "#typ=" << type << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  m_state->addGraphic(graphic);
  if (graphic->m_type==0xb)
    m_state->m_groupStack.push(graphic);
  if (input->tell()!=pos && input->tell()!=endPos)
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool WingzGraph::readEndGroup()
{
  auto input = m_parserState->m_input;
  auto &ascFile = m_parserState->m_asciiFile;
  long pos = input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("WingzGraph::readEndGroup: the header seems bad\n"));
    return false;
  }
  auto type=static_cast<int>(input->readULong(1));
  if (type!=0xf) return false;
  auto fl=static_cast<int>(input->readULong(1));
  auto dSz=static_cast<int>(input->readULong(2));
  int id= (fl==0) ? 0 : static_cast<int>(input->readULong(2));
  libmwaw::DebugStream f;
  f << "Entries(Group)[end]:";
  if (fl!=0x80) f << "fl=" << std::hex << fl << std::dec << ",";
  if (id) f << "id=" << id << ",";
  if (!input->checkPosition(input->tell()+dSz)) {
    MWAW_DEBUG_MSG(("WingzGraph::readEndGroup: the header seems bad\n"));
    return false;
  }
  if (dSz) {
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(dSz, librevenge::RVNG_SEEK_CUR);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (m_state->m_groupStack.empty()) {
    MWAW_DEBUG_MSG(("WingzGraph::readEndGroup: can not found the group beginning\n"));
  }
  else
    m_state->m_groupStack.pop();
  return true;
}

////////////////////////////////////////////////////////////
// text box
////////////////////////////////////////////////////////////
bool WingzGraph::readTextZone(std::shared_ptr<WingzGraphInternal::Graphic> graphic)
{
  auto input = m_parserState->m_input;
  auto &ascFile = m_parserState->m_asciiFile;
  long pos = input->tell();
  if (!input->checkPosition(pos+18)) {
    MWAW_DEBUG_MSG(("WingzGraph::readTextZone: the zone seems too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(TextZone):";
  int patId;
  MWAWColor color;
  readColor(color, patId);
  if (patId)
    f << "col[unkn]=" << color << ",";
  int val=int(input->readULong(1));
  if (val!=1) f << "f0=" << val << ",";
  auto fontConverter=m_parserState->m_fontConverter;
  for (int i=0; i<2; ++i) { // actual font and generic font ?
    MWAWFont font;
    f << "font" << i << "=[";
    unsigned char colors[3];
    for (auto &c : colors) c=static_cast<unsigned char>(input->readULong(1));
    font.setColor(MWAWColor(colors[0],colors[1],colors[2]));
    val=int(input->readLong(1)); // 0
    if (val) f << "f0=" << val << ",";
    font.setSize(float(input->readULong(1)));
    auto flag=static_cast<int>(input->readULong(1));
    uint32_t flags=0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x60)
      f << "#font[flag]=" << std::hex << (flag&0x60) << std::dec << ",";
    font.setFlags(flags);
    auto sSz=static_cast<int>(input->readULong(1));
    if (!sSz || !input->checkPosition(input->tell()+4+sSz)) {
      MWAW_DEBUG_MSG(("WingzGraph::readTextZone: can not determine the string zone %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    std::string name("");
    for (int j=0; j<sSz; ++j)
      name+=char(input->readLong(1));
    font.setId(fontConverter->getId(name));
    f << font.getDebugString(fontConverter);
    f << "],";
    graphic->m_font[i]=font;
  }
  for (int i=0; i<3; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val) f << "g" << i << "=" << val;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "TextZone-A:";
  graphic->m_textType=static_cast<int>(input->readLong(1));
  m_state->addGraphic(graphic);
  bool ok=true;
  switch (graphic->m_textType) {
  case 0:
    f << "button,";
    val=static_cast<int>(input->readLong(1));
    if (val!=3) f << "f0=" << val << ",";
    val=static_cast<int>(input->readLong(1));
    if (val==0) f << "noContent,";
    else if (val!=1) f << "#content=" << val << ",";
    val=static_cast<int>(input->readLong(1));
    if (val==1) f << "title,";
    else if (val) f << "#title=" << val << ",";
    val=static_cast<int>(input->readULong(1));
    if (val) f << "h[content]=" << val << ",";
    val=static_cast<int>(input->readULong(1));
    if (val) f << "h[title]=" << val << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  case 1:
    ok=input->checkPosition(pos+60);
    if (!ok) break;
    f << "text,";
    break;
  case 5: {
    ok=input->checkPosition(pos+53);
    if (!ok) break;
    f << "wheel,";
    for (int i=0; i<5; ++i) {
      val=int(input->readLong(1));
      int const expected[]= {3,0,0,0,0};
      if (val!=expected[i]) f << "f" << i << "=" << val << ",";
    }
    bool isNan;
    double value;
    f << "val=[";
    for (int i=0; i<5; ++i) { // val, minVal, maxVal, incr?, maxVal-minVal?
      if (!input->readDoubleReverted8(value,isNan)) {
        f << "###,";
        input->seek(pos+6+8*(i+1), librevenge::RVNG_SEEK_SET);
      }
      else
        f << value << ",";
    }
    f << "],";
    for (int i=0; i<3; ++i) { // small number
      val=int(input->readLong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+53, librevenge::RVNG_SEEK_SET);
    break;
  }
  case 6: {
    ok=input->checkPosition(pos+40);
    if (!ok) break;
    f << "button[wheel],";
    for (int i=0; i<5; ++i) {
      val=int(input->readLong(1));
      int const expected[]= {3,0,0,0,0};
      if (val!=expected[i]) f << "f" << i << "=" << val << ",";
    }
    bool isNan;
    double value;
    f << "val=[";
    for (int i=0; i<4; ++i) { // val?, min, max, increment?
      if (!input->readDoubleReverted8(value,isNan)) {
        f << "###,";
        input->seek(pos+6+8*(i+1), librevenge::RVNG_SEEK_SET);
      }
      else
        f << value << ",";
    }
    f << "],";
    val=int(input->readLong(2)); // 0
    if (val) f << "f5=" << val << ",";
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);
    break;
  }
  default:
    MWAW_DEBUG_MSG(("WingzGraph::readTextZone: find unknown type %d\n", graphic->m_textType));
    f << "###type=" << graphic->m_textType;
    ok=false;
    break;
  }

  if (!ok || graphic->m_textType!=1) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    if (ok) return true;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return m_mainParser->findNextZone(0xe) && input->tell()>pos;
  }
  val=static_cast<int>(input->readLong(1));
  if (val!=3) f<<"f0=" << val << ",";
  auto &para=graphic->m_paragraph;
  for (int i=0; i<5; ++i) {
    val=static_cast<int>(input->readULong(2));
    if (i==2 && (val>>12)) { // 1 left, 2 center, 3 right
      switch ((val>>12)&3) {
      case 0:
      default:
        f << "#align=0,";
        break;
      case 1: // left
        break;
      case 2:
        para.m_justify = MWAWParagraph::JustificationCenter;
        f << "center,";
        break;
      case 3:
        para.m_justify = MWAWParagraph::JustificationRight;
        f << "right,";
        break;
      }
      val &= 0xCFFF;
    }
    if (val) f<<"f" << i+1 << "=" << std::hex << val << std::dec << ",";
  }
  val=static_cast<int>(input->readULong(4));
  auto textSize=static_cast<int>(input->readLong(4));
  if (val!=textSize)
    f << "selection="<<val << ",";
  val=static_cast<int>(input->readLong(2)); // 1|7
  if (val!=1) f << "g0=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  if (val) f << "g1=" << val << ",";
  for (int i=0; i< 2; ++i) { // g2=0|1, g3=4|6, g3=64 -> scroll bar, g3&1 -> cell note?
    val=static_cast<int>(input->readULong(1));
    static int const expected[]= {0,0x40};
    if (val!=expected[i])
      f << "g" << i+2 << "=" << std::hex << val << std::dec << ",";
  }
  auto numFonts=static_cast<int>(input->readLong(2));
  if (numFonts!=1) f << "numFonts=" << numFonts << ",";
  val=static_cast<int>(input->readLong(2));
  if (val) f << "h0=" << val << ",";
  auto numPos=static_cast<int>(input->readULong(2));
  if (numPos!=1) f << "numPos=" << numPos << ",";
  for (int i=0; i<14; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (!val) continue;
    if (i==3) f << "marg[top]="  << double(val)/20. << ",";
    else if (i==4) f << "marg[bottom]="  << double(val)/20. << ",";
    else if (i==7) f << "tabs[repeat]=" << double(val)/20. << ",";
    else
      f << "h" << i+1 << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  if (textSize<0 || pos+textSize<pos || !input->checkPosition(pos+textSize)) {
    MWAW_DEBUG_MSG(("WingzGraph::readTextZone: the text zone seems bad\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  f << "TextZone[text]:";
  graphic->m_textEntry.setBegin(input->tell());
  graphic->m_textEntry.setLength(textSize);
  std::string text("");
  for (int i=0; i< textSize; ++i) text+=char(input->readULong(1));
  f << text;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  if (!input->checkPosition(pos+numFonts*7)) {
    MWAW_DEBUG_MSG(("WingzGraph::readTextZone: the fonts zone seems bad\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  f << "TextZone[fonts]:";
  for (int i=0; i<numFonts; ++i) {
    f << "font" << i << "=[";
    MWAWFont font;
    unsigned char colors[3];
    for (auto &c : colors) c=static_cast<unsigned char>(input->readULong(1));
    font.setColor(MWAWColor(colors[0],colors[1],colors[2]));
    val=int(input->readLong(1)); // 0
    if (val) f << "f0=" << val << ",";
    font.setSize(float(input->readULong(1)));
    auto flag=static_cast<int>(input->readULong(1));
    uint32_t flags=0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x60)
      f << "#font[flag]=" << std::hex << (flag&0x60) << std::dec << ",";
    font.setFlags(flags);
    auto sSz=static_cast<int>(input->readULong(1));
    if (!sSz || !input->checkPosition(input->tell()+sSz)) {
      MWAW_DEBUG_MSG(("WingzGraph::readTextZone: can not determine the string zone %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    std::string name("");
    for (int j=0; j<sSz; ++j)
      name+=char(input->readLong(1));
    font.setId(fontConverter->getId(name));
    f << font.getDebugString(fontConverter);
    f << "],";
    graphic->m_fontList.push_back(font);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  if (!input->checkPosition(pos+16+numPos*6)) {
    MWAW_DEBUG_MSG(("WingzGraph::readTextZone: the last zone seems bad\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f.str("");
  f << "TextZone-B:";
  double extraLeading=0;
  para.m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<7; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val==0) continue;
    switch (i) {
    case 2:
      para.m_margins[1]=double(val)/20.;
      f << "marg[left]=" << double(val)/20. << ",";
      break;
    case 3:
      para.m_margins[2]=double(val)/20.;
      f << "marg[right]=" << double(val)/20. << ",";
      break;
    case 4:
      para.m_margins[0]=double(val)/20.;
      f << "para[indent]=" << double(val)/20. << ",";
      break;
    case 5:
      extraLeading=double(val)/20;
      f << "height[leading]=" << extraLeading << ",";
      break;
    default:
      f << "f" << i << "=" << val << ",";
      break;
    }
  }
  val=static_cast<int>(input->readLong(1));
  switch (val) {
  case 1: // normal
    break;
  case 2:
    para.setInterline(2, librevenge::RVNG_PERCENT);
    f << "interline=200%,";
    break;
  case 3:
    para.setInterline(1.5, librevenge::RVNG_PERCENT);
    f << "interline=150%,";
    break;
  case 4:
    f << "interline=fixed,";
    break;
  case 5:
    para.m_spacings[1]=extraLeading/72.;
    f << "interline=extra[leading],";
    break;
  default:
    f << "#interline=" << val << ",";
    break;
  }
  val=static_cast<int>(input->readLong(1)); // 1|2
  if (val!=1) f << "f8=" << val << ",";
  int lastPos=0;
  f << "pos=[";
  for (int i=0; i<numPos; ++i) {
    auto newPos=static_cast<int>(input->readULong(4));
    auto ft=static_cast<int>(input->readULong(2));
    if ((i==0 && newPos!=0) || (i && (newPos<lastPos || newPos>textSize)) || (ft>numFonts)) {
      MWAW_DEBUG_MSG(("WingzGraph::readTextZone: the position zone seems bad\n"));
      f << "##";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (ft<numFonts)
      graphic->m_posToFontId[newPos]=size_t(ft);
    f << std::hex << newPos << std::dec << ":" << ft << ",";
  }
  f << "],";

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// color/pattern
////////////////////////////////////////////////////////////
bool WingzGraph::readPattern(MWAWGraphicStyle::Pattern &pattern, int &patId)
{
  auto input = m_parserState->m_input;
  long pos = input->tell();
  if (!input->checkPosition(pos+7)) {
    MWAW_DEBUG_MSG(("WingzGraph::readPattern: the zone seems to short\n"));
    return false;
  }
  MWAWColor colors[2];
  unsigned char col[3];
  for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
  colors[0]=MWAWColor(col[0],col[1],col[2]);
  patId=int(input->readULong(1));
  for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
  colors[1]=MWAWColor(col[0],col[1],col[2]);
  if (m_state->m_patternList.empty())
    m_state->initPatterns(version());
  if (patId>=0 && patId<int(m_state->m_patternList.size()))
    pattern=m_state->m_patternList[size_t(patId)];
  else
    pattern=m_state->m_patternList[0];
  for (int i=0; i<2; ++i)
    pattern.m_colors[i]=colors[1-i];
  return true;
}

bool WingzGraph::readColor(MWAWColor &color, int &patId)
{
  MWAWGraphicStyle::Pattern pat;
  if (!readPattern(pat, patId))
    return false;
  pat.getAverageColor(color);
  return true;
}

////////////////////////////////////////////////////////////
// chart
////////////////////////////////////////////////////////////
bool WingzGraph::readChartData(std::shared_ptr<WingzGraphInternal::Graphic>)
{
  auto input = m_parserState->m_input;
  auto &ascFile = m_parserState->m_asciiFile;
  long pos = input->tell(), debPos=pos;
  libmwaw::DebugStream f;
  f << "Entries(Chart):";
  auto val=static_cast<int>(input->readLong(2));
  f << "f0=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  f << "f1=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (val>0)
    return true;
  if (!input->checkPosition(pos+866)) {
    MWAW_DEBUG_MSG(("WingzGraph::readChartData: the zone seems to short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  MWAWColor color;
  int patId;
  bool ok=true;
  for (int i=0; i<6; ++i) {
    pos=input->tell();
    f.str("");
    char const *wh[]= {"title", "footnote", "background", "plotArea", "serie,label", "interior"};
    f << "Chart[" << wh[i] << "]:";
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId && !color.isWhite())
      f << "surf[col]=" << color << ",";
    else if (!patId)
      f << "surf[col]=none,";
    if (i==5) {
      val=int(input->readLong(1));
      if (val!=1) f << "f0=" << val << ",";
    }
    else {
      val=int(input->readLong(1)); // shadow type 4: none, 5: 2d, 6: 3d
      if (val!=4) f << "shadow[type]=" << val << ",";
      if (!readColor(color, patId)) {
        ok=false;
        break;
      }
      if (patId && !color.isBlack())
        f << "shadow[col]=" << color << ",";
      else if (!patId)
        f << "shadow[col]=none,";
      val=int(input->readULong(1)); // related to shadow
      if (val!=0xff) f << "f1=" << std::hex << val << std::dec << ",";
    }
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId && !color.isBlack())
      f << "line[col]=" << color << ",";
    else if (!patId)
      f << "line[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "h0=" << val << ",";
    val=int(input->readULong(1));
    if (val!=5) f << "line[w]=" << float(val)/20.f << ",";
    val=int(input->readLong(1));
    if (val) f << "h1=" << val << ",";
    if (i!=5) {
      int dim[2];
      for (auto &d : dim) d=int(input->readULong(2));
      if (dim[0]!=20 || dim[1]!=20) f << "shadow[pos]=" << 0.05f * MWAWVec2f(float(dim[0]), float(dim[1])) << ",";
    }
    if (i<2) {
      int cell[4];
      for (auto &d : cell) d=int(input->readLong(2));
      if (cell[1]>=0)
        f << MWAWBox2i(MWAWVec2i(cell[0],cell[1]),MWAWVec2i(cell[2],cell[3])) << ",";
      ascFile.addDelimiter(input->tell(),'|');
      input->seek(pos+42, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  if (!ok) {
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  pos=input->tell();
  f.str("");
  f << "Chart-A5:";
  for (int j=0; j<7; ++j) { // 0 and 1 : the table's data
    int cell[4];
    for (auto &d : cell) d=int(input->readLong(2));
    if (cell[1]>=0)
      f << "ce" << j << "=" << MWAWBox2i(MWAWVec2i(cell[0],cell[1]),MWAWVec2i(cell[2],cell[3])) << ",";
  }
  for (int j=0; j<6; ++j) { // f0=0|1,f3=1|2|b|e|13:type?,f5=1-4,b-c|10|1b|23|40|47
    val=int(input->readLong(2));
    int const expected[]= {0,0xf0,0,0,0,0};
    if (j==3)
      /* 0: bar, 1: line, 2:layer, 3:step, 4: bar/line
         5: bar 3d, 6: line 3d, 7: layer 3d, 8: step 3d, 9: bar/line 3d
         10:pie, 11: pie 3d, 12:High-Low, 14: XY, 16: scatter,
         17: polar, 18:  wireframe, 19: contour, 20: surface
       */
      f << "type=" << val << ",";
    else if (val!=expected[j])
      f << "f" << j << "=" << val << ",";
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  pos+=70;

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f.str("");
  f << "Chart-header:";
  auto numSeries=static_cast<int>(input->readULong(2));
  f << "numSerie=" << numSeries << ",";
  long endPos=debPos+866+73*numSeries;
  if (!input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("WingzGraph::readChartData: the zone seems to short\n"));
    ok=input->checkPosition(debPos+866);
    if (ok) {
      input->seek(debPos+866, librevenge::RVNG_SEEK_SET);
      ok=m_mainParser->findNextZone(0xe);
    }
    if (!ok) {
      MWAW_DEBUG_MSG(("WingzGraph::readChartData: can not find the next zone\n"));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    numSeries=0;
    endPos=input->tell();
    input->seek(pos+2, librevenge::RVNG_SEEK_SET);
  }
  for (int i=0; i<3; ++i) { // f1: some id, f2&0xff: subtype
    val=int(input->readULong(2));
    if (val)
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  readColor(color, patId);
  if (patId && !color.isWhite())
    f << "surf[col]=" << color << ",";
  else if (!patId)
    f << "surf[col]=none,";
  val=int(input->readLong(1)); // shadow type 4: none, 5: 2d, 6: 3d
  if (val!=4) f << "shadow[type]=" << val << ",";
  readColor(color, patId);
  if (patId && !color.isBlack())
    f << "shadow[col]=" << color << ",";
  else if (!patId)
    f << "shadow[col]=none,";
  val=int(input->readULong(1)); // related to shadow
  if (val!=0xff)
    f << "f4=" << std::hex << val << std::dec << ",";
  readColor(color, patId);
  if (patId && !color.isBlack())
    f << "line[col]=" << color << ",";
  else if (!patId)
    f << "line[col]=none,";
  val=int(input->readLong(1));
  if (val!=1) f << "g0=" << val << ",";
  val=int(input->readULong(1));
  if (val!=5) f << "line[w]=" << float(val)/20.f << ",";
  val=int(input->readLong(1));
  if (val) f << "g1=" << val << ",";
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(2));
  if (dim[0]!=20 || dim[1]!=20) f << "shadow[pos]=" << 0.05f * MWAWVec2f(float(dim[0]), float(dim[1])) << ",";
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(18, librevenge::RVNG_SEEK_CUR);
  ascFile.addDelimiter(input->tell(),'|');
  val=int(input->readLong(1));
  if (val!=1)
    f << "g2=" << val << ",";
  val=int(input->readLong(1));
  if (val!=1)
    f << "g3=" << val << ",";
  for (int i=0; i<11; ++i) {
    val=int(input->readLong(1));
    if (val)
      f << "h" << i << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos+=80;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<4; ++i) {
    pos=input->tell();
    f.str("");
    char const *wh[]= {"axisX", "axisZ", "axisY", "B3"};
    f << "Chart[" << wh[i] << "]:";
    val=int(input->readULong(2));
    if (val!=0x4024) f << "fl=" << std::hex << val << std::dec << ",";
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId && !color.isWhite())
      f << "surf[col]=" << color << ",";
    else if (!patId)
      f << "surf[col]=none,";
    val=int(input->readLong(1)); // shadow type 4: none, 5: 2d, 6: 3d
    if (val!=4) f << "shadow[type]=" << val << ",";
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId && !color.isBlack())
      f << "shadow[col]=" << color << ",";
    else if (!patId)
      f << "shadow[col]=none,";
    val=int(input->readULong(1)); // related to shadow
    if (val!=0xff) f << "f1=" << std::hex << val << std::dec << ",";
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId && !color.isBlack())
      f << "line[col]=" << color << ",";
    else if (!patId)
      f << "line[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "h0=" << val << ",";
    val=int(input->readULong(1));
    if (val!=5) f << "line[w]=" << float(val)/20.f << ",";
    val=int(input->readLong(1));
    if (val) f << "h1=" << val << ",";
    for (auto &d : dim) d=int(input->readULong(2));
    if (dim[0]!=20 || dim[1]!=20) f << "shadow[pos]=" << 0.05f * MWAWVec2f(float(dim[0]), float(dim[1])) << ",";
    int cell[4];
    for (auto &d : cell) d=int(input->readLong(2));
    if (cell[1]>=0)
      f << MWAWBox2i(MWAWVec2i(cell[0],cell[1]),MWAWVec2i(cell[2],cell[3])) << ",";
    for (int j=0; j<2; ++j) { // h2=1 for axisY
      int const expected[]= {0,2};
      val=int(input->readLong(2));
      if (val!=expected[j]) f << "h" << j+2 << "=" << val << ",";
    }
    for (int k=0; k<2; ++k) { // k=0, real axis line color
      std::string what(k==0 ? "line2" : "unkn");
      if (!readColor(color, patId)) {
        ok=false;
        break;
      }
      if (patId && !color.isBlack())
        f << what << "[col]=" << color << ",";
      else if (!patId)
        f << what << "[col]=none,";
      val=int(input->readLong(1));
      if (val!=1) f << what << "[f0]=" << val << ",";
      val=int(input->readULong(1));
      if (val!=5) f << what << "[w]=" << float(val)/20.f << ",";
      val=int(input->readLong(1));
      if (val) f << what << "[f1]=" << val << ",";
    }
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    if (patId)
      f << "unkn2[col]=" << color << ",";
    val=int(input->readLong(1));
    if (val!=1) f << "l0=" << val << ",";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    input->seek(pos+113, librevenge::RVNG_SEEK_SET);
  }
  if (!ok) {
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  pos=input->tell();
  f.str("");
  f << "Chart-B5:";
  for (int i=0; i<5; ++i) {
    int const expected[]= {0x1e, 0x1e, 0x32, 0x32, 0x109};
    val=int(input->readLong(2));
    if (val==expected[i]) continue;
    char const *wh[] = {"f0", "f1", "x[vanish,3d]", "y[vanish,3d]", "distance[3d]"};
    f << wh[i] << "=" << val << ",";
  }
  for (int i=0; i<4; ++i) {
    if (!readColor(color, patId)) {
      ok=false;
      break;
    }
    char const *wh[] = {"top", "side", "shadow", "line"};
    if (patId && ((i<2 && !color.isWhite()) || (i>=2 && !color.isBlack())))
      f << wh[i] << "[3d,col]=" << color << ",";
    else if (!patId)
      f << wh[i] << "[3d,col]=none,";
    val=int(input->readULong(1));
    if (i<2) {
      if (val!=4)
        f << "f" << i+2 << "=" << val << ",";
    }
    else if (i==2) {
      if (val!=0x4b) // val something like ~2*tint in percent
        f << "shadow[tint]=" << val << ",";
    }
    else if (val!=1)
      f << "f" << i+2 << "=" << val << ",";
  }
  val=int(input->readULong(1));
  if (val!=5) f << "line[w,3d]=" << float(val)/20.f << ",";
  if (!ok) {
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  pos+=68;
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  for (int i=0; i<numSeries; ++i) {
    pos=input->tell();
    f.str("");
    f << "Chart-Serie" << i << ":";

    input->seek(pos+70, librevenge::RVNG_SEEK_SET);
    val=int(input->readLong(1));
    if (val!=2) {
      if (val) {
        f << "###type=" << val << ",";
        MWAW_DEBUG_MSG(("WingzGraph::readChartData: find unexpected serie type\n"));
      }
      f << "_,";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos+73, librevenge::RVNG_SEEK_SET);
      continue;
    }

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (!readColor(color, patId))
      break;
    if (patId && !color.isWhite())
      f << "surf[col]=" << color << ",";
    else if (!patId)
      f << "surf[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "f0=" << val << ",";

    if (!readColor(color, patId))
      break;
    if (patId)
      f << "shadow[col]=" << color << ",";

    val=int(input->readLong(1));
    if (val) f << "f1=" << val << ",";
    if (!readColor(color, patId))
      break;
    if (patId && !color.isBlack())
      f << "line[col]=" << color << ",";
    else if (!patId)
      f << "line[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "f2=" << val << ",";
    val=int(input->readLong(2));
    if (val!=40) f << "f3=" << val << ",";
    if (!readColor(color, patId))
      break;
    if (patId && !color.isBlack())
      f << "unkn[col]=" << color << ",";
    else if (!patId)
      f << "unkn[col]=none,";
    val=int(input->readLong(1));
    if (val!=1) f << "g0=" << val << ",";
    val=int(input->readULong(1));
    if (val!=5) f << "line[w]=" << float(val)/20.f << ",";
    val=int(input->readLong(1));
    if (val) f << "g1=" << val << ",";
    for (int j=0; j<3; ++j) { // 0: data, 1: label?
      int cell[4];
      for (auto &c : cell) c=int(input->readLong(2));
      if (cell[1]>=0)
        f << "cells" << j << "=" << MWAWBox2i(MWAWVec2i(cell[0],cell[1]),MWAWVec2i(cell[2],cell[3])) << ",";
    }
    for (int j=0; j<5; ++j) { // h1=serie[id] ?
      val=int(input->readLong(2));
      if (val)
        f << "h" << j << "=" << val << ",";
    }
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+73, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("WingzGraph::readChartData: find some extra data\n"));
    ascFile.addPos(input->tell());
    ascFile.addNote("Chart-end:###");
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool WingzGraph::sendGraphic(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WingzGraph::sendGraphic: listener is not set\n"));
    return false;
  }
  MWAWPosition fPos;
  if (!m_state->m_inGroupDepth) {
    MWAWVec2f begPos=m_mainParser->getPosition(graphic.m_position[0], graphic.m_relativePosition[0]);
    MWAWVec2f endPos=m_mainParser->getPosition(graphic.m_position[1], graphic.m_relativePosition[1]);
    fPos=MWAWPosition(begPos, endPos-begPos, librevenge::RVNG_POINT);
  }
  else { // special case relative to the group box
    auto const &orig=pos.origin();
    auto const &size=pos.size();
    MWAWVec2f begPos(orig[0]+size[0]*float(graphic.m_position[0][0])/float(0x3fff),
                     orig[1]+size[1]*float(graphic.m_position[0][1])/float(0x3fff));
    MWAWVec2f endPos(orig[0]+size[0]*float(graphic.m_position[1][0])/float(0x3fff),
                     orig[1]+size[1]*float(graphic.m_position[1][1])/float(0x3fff));
    fPos=MWAWPosition(begPos, endPos-begPos, librevenge::RVNG_POINT);
  }
  fPos.m_anchorTo=MWAWPosition::Page;
  fPos.setOrder(graphic.m_order);
  switch (graphic.m_type) {
  case 0:
  case 2: {
    std::shared_ptr<MWAWSubDocument> doc(new WingzGraphInternal::SubDocument(*this, m_parserState->m_input, graphic));
    listener->insertTextBox(fPos, doc, graphic.m_style);
    return true;
  }
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
    return sendShape(graphic, fPos);
  case 0xa:
    return sendPicture(graphic, fPos);
  case 0xb: // group
    listener->openGroup(pos);
    ++m_state->m_inGroupDepth;
    for (auto &c : graphic.m_children) {
      if (c) sendGraphic(*c, fPos);
    }
    --m_state->m_inGroupDepth;
    listener->closeGroup();
    return true;
  default:
    break;
  }
  static bool first=true;
  if (first) {
    MWAW_DEBUG_MSG(("WingzGraph::sendGraphic: oops, unsure how to send some graphic[%d]\n", graphic.m_type));
    first=false;
  }
  return false;
}

bool WingzGraph::sendPicture(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WingzGraph::sendPicture: listener is not set\n"));
    return false;
  }
  if (!graphic.m_entry[0].valid()) {
    MWAW_DEBUG_MSG(("WingzGraph::sendPicture: can not find the picture\n"));
    return false;
  }
  auto input = m_parserState->m_input;
  long actPos=input->tell();
  librevenge::RVNGBinaryData file;
  input->seek(graphic.m_entry[0].begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(graphic.m_entry[0].length(), file);
  MWAWEmbeddedObject object(file);
  listener->insertPicture(pos, object);
  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool WingzGraph::sendShape(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WingzGraph::sendShape: listener is not set\n"));
    return false;
  }
  auto const &orig=pos.origin();
  auto end=orig+pos.size();
  MWAWGraphicShape shape;
  switch (graphic.m_type) {
  case 5:
    switch (graphic.m_flag&3) {
    case 1:
      shape=MWAWGraphicShape::line(MWAWVec2f(end[0],orig[1]),MWAWVec2f(orig[0],end[1]));
      break;
    case 2:
      shape=MWAWGraphicShape::line(MWAWVec2f(orig[0],end[1]),MWAWVec2f(end[0],orig[1]));
      break;
    case 3:
      shape=MWAWGraphicShape::line(end,orig);
      break;
    case 0:
    default:
      shape=MWAWGraphicShape::line(orig,end);
      break;
    }
    break;
  case 6: {
    float angle[2] = { graphic.m_angles[0], graphic.m_angles[1] };
    if (angle[0] > angle[1]) std::swap(angle[0],angle[1]);
    while (angle[1] > 360) {
      angle[0]-=360;
      angle[1]-=360;
    }
    while (angle[0] < -360) {
      angle[0]+=360;
      angle[1]+=360;
    }

    MWAWBox2f box(orig,end);
    // we must compute the real bd box
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; i++)
      limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; bord++) {
      float ang = (bord == limitAngle[0]) ? float(angle[0]) :
                  (bord == limitAngle[1]+1) ? float(angle[1]) : float(90 * bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { std::cos(ang), -std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    MWAWBox2f circleBox(orig,end);
    // we have the shape box, we need to reconstruct the circle box
    if (maxVal[0]>minVal[0] && maxVal[1]>minVal[1]) {
      float scaling[2]= { (box[1][0]-box[0][0])/(maxVal[0]-minVal[0]),
                          (box[1][1]-box[0][1])/(maxVal[1]-minVal[1])
                        };
      float constant[2]= { box[0][0]-minVal[0] *scaling[0], box[0][1]-minVal[1] *scaling[1]};
      circleBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                          MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
    }
    if (graphic.m_style.hasSurface())
      shape = MWAWGraphicShape::pie(box, circleBox, MWAWVec2f(float(angle[0]),float(angle[1])));
    else
      shape = MWAWGraphicShape::arc(box, circleBox, MWAWVec2f(float(angle[0]),float(angle[1])));
    break;
  }
  case 7:
    shape=MWAWGraphicShape::circle(MWAWBox2f(orig,end));
    break;
  case 8:
    if (graphic.m_flag&0x20)
      shape=MWAWGraphicShape::rectangle(MWAWBox2f(orig,end), 0.2f*pos.size());
    else
      shape=MWAWGraphicShape::rectangle(MWAWBox2f(orig,end));
    break;
  case 9: {
    if (graphic.m_vertices.empty()) {
      MWAW_DEBUG_MSG(("WingzGraph::sendPageGraphics: oops, can not find any vertices\n"));
      return false;
    }
    auto size=pos.size();
    if (graphic.m_flag&2) { // smooth
      shape.m_bdBox=MWAWBox2f(orig,end);
      shape.m_type=MWAWGraphicShape::Path;
      shape.m_path.push_back(MWAWGraphicShape::PathData('M', MWAWVec2f(orig[0]+graphic.m_vertices[0][0]*size[0],orig[1]+graphic.m_vertices[0][1]*size[1])));
      for (size_t i=1; i+1<graphic.m_vertices.size(); ++i) {
        MWAWVec2f pt=MWAWVec2f(orig[0]+graphic.m_vertices[i][0]*size[0],
                               orig[1]+graphic.m_vertices[i][1]*size[1]);
        MWAWVec2f dir=graphic.m_vertices[i+1]-graphic.m_vertices[i-1];
        shape.m_path.push_back(MWAWGraphicShape::PathData('S', pt, pt-0.1f*MWAWVec2f(dir[0]*size[0],dir[1]*size[1])));
      }
      if (graphic.m_vertices.size()>1)
        shape.m_path.push_back(MWAWGraphicShape::PathData('L', MWAWVec2f(orig[0]+graphic.m_vertices.back()[0]*size[0],orig[1]+graphic.m_vertices.back()[1]*size[1])));
      if (graphic.m_flag&1)
        shape.m_path.push_back(MWAWGraphicShape::PathData('Z'));
      break;
    }
    if (graphic.m_flag&1)
      shape=MWAWGraphicShape::polygon(MWAWBox2f(orig,end));
    else
      shape=MWAWGraphicShape::polyline(MWAWBox2f(orig,end));
    for (auto const &pt : graphic.m_vertices)
      shape.m_vertices.push_back(MWAWVec2f(orig[0]+pt[0]*size[0],orig[1]+pt[1]*size[1]));
    break;
  }
  default:
    shape=MWAWGraphicShape::rectangle(MWAWBox2f(orig,end));
    break;
  }
  listener->insertShape(pos, shape, graphic.m_style);
  return true;
}

bool WingzGraph::sendText(WingzGraphInternal::Graphic const &graphic)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WingzGraph::sendText: listener is not set\n"));
    return false;
  }
  auto input=m_parserState->m_input;

  bool first=true;
  for (int i=0; i<2; ++i) {
    if (!graphic.m_entry[i].valid()) continue;
    if (!first) listener->insertEOL();
    listener->setFont(graphic.m_font[graphic.m_textType==1 ? 0 : 1]);
    input->seek(graphic.m_entry[i].begin(), librevenge::RVNG_SEEK_SET);
    for (long l=graphic.m_entry[i].length(); l>0; --l) {
      auto c=static_cast<unsigned char>(input->readULong(1));
      if (c==0x9)
        listener->insertTab();
      else if (c==0xd)
        listener->insertEOL();
      else
        listener->insertCharacter(c);
    }
    first=false;
  }

  if (!graphic.m_textEntry.valid())
    return true;
  if (!first) listener->insertEOL();
  listener->setParagraph(graphic.m_paragraph);
  input->seek(graphic.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
  for (long l=0; l<graphic.m_textEntry.length(); ++l) {
    if (graphic.m_posToFontId.find(int(l))!=graphic.m_posToFontId.end()) {
      auto fId=graphic.m_posToFontId.find(int(l))->second;
      if (fId<graphic.m_fontList.size())
        listener->setFont(graphic.m_fontList[fId]);
    }
    auto c=static_cast<unsigned char>(input->readULong(1));
    if (c==0x9)
      listener->insertTab();
    else if (c==0xd)
      listener->insertEOL();
    else
      listener->insertCharacter(c);
  }
  return true;
}

bool WingzGraph::sendPageGraphics()
{
  if (!m_state->m_groupStack.empty()) {
    MWAW_DEBUG_MSG(("WingzGraph::sendPageGraphics: oops, some groups are not closed\n"));
  }
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WingzGraph::sendPageGraphics: listener is not set\n"));
    return false;
  }
  MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(0,0), librevenge::RVNG_POINT);
  pos.m_anchorTo=MWAWPosition::Page;

  for (auto &graph : m_state->m_pictureList) {
    if (graph) sendGraphic(*graph, pos);
  }
  return true;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
