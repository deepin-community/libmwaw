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
#include <sstream>
#include <stack>
#include <utility>

#include <librevenge/librevenge.h>

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

#include "CricketDrawParser.hxx"

/** Internal: the structures of a CricketDrawParser */
namespace CricketDrawParserInternal
{
/** small structure of CricketDrawParserInternal used to stored
    a shape
*/
struct Shape {
  //! the different shape type
  enum Type {
    Basic, Grate, Group, GroupEnd, Picture, StarBurst, Textbox, Unknown
  };
  //! constructor
  Shape()
    : m_id(-1)
    , m_type(Unknown)
    , m_shape()
    , m_translation()
    , m_rotation(0)
    , m_shear(0)
    , m_style(MWAWGraphicStyle::emptyStyle())
    , m_vertices()
    , m_closed(false)
    , m_locked(false)
    , m_smoothed(false)
    , m_corners(0,0)
    , m_angles(0,0)
    , m_grateN(0)
    , m_grateType(0)
    , m_text()
    , m_paragraph()
    , m_textColor(MWAWColor::black())
    , m_bitmap()
    , m_bitmapDimension()
    , m_bitmapRowSize()
    , m_bitmapScale(100)
    , m_isSent(false)
  {
    for (auto &fl : m_flip) fl=false;
    for (auto &id : m_ids) id=0;
    for (auto &group : m_groupIds) group=0;
    for (auto &angle : m_starBustAngles) angle=0;
  }
  //! returns the current transformation
  MWAWTransformation getTransformation(MWAWTransformation const &transform) const
  {
    auto transformation=transform*MWAWTransformation::translation(m_translation);
    MWAWVec2f const &center=m_box[0].center();
    if (m_shear<0 || m_shear>0) {
      auto angl=float(double(m_shear)*M_PI/180);
      auto cosA=float(std::cos(angl));
      auto sinA=float(std::sin(angl));
      transformation *=
        MWAWTransformation(MWAWVec3f(1,sinA,-sinA*center[1]),MWAWVec3f(0,cosA,center[1]-cosA*center[1]));
    }
    if (m_rotation<0||m_rotation>0)
      transformation *= MWAWTransformation::rotation(m_rotation,center);
    static bool first=true;
    if (first && (m_flip[0]||m_flip[1])) {
      first=false;
      MWAW_DEBUG_MSG(("CricketDrawParserInternal::Shape::getTransformation: oops flipping is not implemented\n"));
    }
    return transformation;
  }
  //! the shape id
  int m_id;
  //! the shape type
  Type m_type;
  //! the ids
  long m_ids[2];
  //! the shape
  MWAWGraphicShape m_shape;
  //! the main box (before translation and after translation)
  MWAWBox2f m_box[2];
  //! the translation
  MWAWVec2f m_translation;
  //! the rotation angle
  float m_rotation;
  //! the shear angle
  float m_shear;
  //! two bool to indicated we need to flip the shape or not
  bool m_flip[2];
  //! the style
  MWAWGraphicStyle m_style;
  //! the list of point
  std::vector<MWAWVec2f> m_vertices;
  //! flag to know if the shape is closed
  bool m_closed;
  //! flag to know if the shape is locked
  bool m_locked;
  //! flag to know if the shape is smoothed
  bool m_smoothed;
  //! the rectOval corner size
  MWAWVec2f m_corners;
  //! the arc limits
  MWAWVec2i m_angles;
  //! the grate number
  int m_grateN;
  //! the grate type
  int m_grateType;
  //! the starbust angle: min, max, delta
  int m_starBustAngles[3];
  //! some unknown group id
  long m_groupIds[2];
  //! the text entry
  MWAWEntry m_text;
  //! the paragraph style
  MWAWParagraph m_paragraph;
  //! the text color
  MWAWColor m_textColor;
  //! the bitmap entry
  MWAWEntry m_bitmap;
  //! the bitmap dimension
  MWAWBox2i m_bitmapDimension;
  //! the bitmap row size
  int m_bitmapRowSize;
  //! the bitmap scaling
  int m_bitmapScale;
  //! flag to know if a shape is already sent
  mutable bool m_isSent;
};

////////////////////////////////////////
//! Internal: the state of a CricketDrawParser
struct State {
  //! constructor
  State()
    : m_dashList()
    , m_shapeList()
  {
  }
  //! returns the quickdraw color corresponding to some id
  static bool getColor(int id, int intensity, MWAWColor &col);
  //! returns a dash corresponding to some id
  bool getDash(int id, std::vector<float> &dash)
  {
    if (m_dashList.empty())
      initDashs();
    if (id<1 || size_t(id)>m_dashList.size()) {
      MWAW_DEBUG_MSG(("CricketDrawParserInternal::State::getDash: unknown dahs %d\n", id));
      return false;
    }
    dash=m_dashList[size_t(id-1)];
    return true;
  }

protected:
  //! init the dashs list
  void initDashs();
  //! the list of dash
  std::vector< std::vector<float> > m_dashList;
public:
  //! the list of shape
  std::vector<Shape> m_shapeList;
};

bool State::getColor(int id, int intensity, MWAWColor &col)
{
  switch (id) {
  case 30:
    col = MWAWColor::white();
    break; // white
  case 33:
    col = MWAWColor::black();
    break; // black
  case 69:
    col = MWAWColor(255,255,0);
    break; // yellow
  case 137:
    col = MWAWColor(255,0,255);
    break; // magenta
  case 205:
    col = MWAWColor(255,0,0);
    break; // red
  case 273:
    col = MWAWColor(0,255,255);
    break; // cyan
  case 341:
    col = MWAWColor(0,255,0);
    break; // green
  case 409:
    col = MWAWColor(0,0,255);
    break; // blue
  default:
    MWAW_DEBUG_MSG(("CricketDrawParserInternal::State::getColor: unknown color %d\n", id));
    return false;
  }
  col=MWAWColor::barycenter(float(intensity)/100.f, col, float(100-intensity)/100.f, MWAWColor::white());
  return true;
}

void State::initDashs()
{
  if (!m_dashList.empty()) return;
  std::vector<float> dash;
  // 1 solid
  dash.push_back(270);
  m_dashList.push_back(dash);
  // 2: 36x9 9x9 9x9
  dash[0]=36;
  for (int i=0; i<5; ++i) dash.push_back(9);
  m_dashList.push_back(dash);
  // 3:36x9 9x9
  dash.resize(4);
  m_dashList.push_back(dash);
  // 4: 36x18
  dash.resize(2);
  dash[1]=18;
  m_dashList.push_back(dash);
  // 5:27x9
  dash[0]=27;
  dash[1]=9;
  m_dashList.push_back(dash);
  // 6:18x18
  dash[0]=18;
  dash[1]=18;
  m_dashList.push_back(dash);
  // 7:9x27
  dash[0]=9;
  dash[1]=27;
  m_dashList.push_back(dash);
  // 8:5x32
  dash[0]=5;
  dash[1]=32;
  m_dashList.push_back(dash);
  // 9:3x18
  dash[0]=3;
  dash[1]=18;
  m_dashList.push_back(dash);
  // 10:3x3
  dash[0]=3;
  dash[1]=3;
  m_dashList.push_back(dash);
}

////////////////////////////////////////
//! Internal: the subdocument of a CricketDrawParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(CricketDrawParser &pars, MWAWInputStreamPtr const &input, int zoneId)
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
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("CricketDrawParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<CricketDrawParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("CricketDrawParserInternal::SubDocument::parse: no parser\n"));
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
CricketDrawParser::CricketDrawParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new CricketDrawParserInternal::State);

  getPageSpan().setMargins(0.1);
}

CricketDrawParser::~CricketDrawParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void CricketDrawParser::parse(librevenge::RVNGDrawingInterface *docInterface)
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
      sendAll();
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("CricketDrawParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void CricketDrawParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("CricketDrawParser::createDocument: listener already exist\n"));
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
bool CricketDrawParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!readPrintInfo())
    input->seek(pos,librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  const int vers=version();
  libmwaw::DebugStream f;
  f << "Entries(Unknown):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  // vers<=2 first block in 0xa0?
  input->seek(0x200, librevenge::RVNG_SEEK_SET);
  int const decal=vers<=2 ? 39 : 63;
  while (!input->isEnd()) {
    if (readShape())
      continue;
    pos=input->tell();
    if (input->tell()==pos)
      input->seek(pos+decal+1,librevenge::RVNG_SEEK_SET);
    while (!input->isEnd()) {
      long actPos=input->tell();
      auto val=static_cast<int>(input->readULong(4));
      if ((val&0xFFFFFF)==0x640021) {
        input->seek(actPos+1-decal, librevenge::RVNG_SEEK_SET);
        break;
      }
      if ((val>>8)==0x640021) {
        input->seek(actPos-decal, librevenge::RVNG_SEEK_SET);
        break;
      }
      if ((val&0xFFFF)==0x6400)
        input->seek(-2, librevenge::RVNG_SEEK_CUR);
      else if ((val&0xFF)==0x64)
        input->seek(-3, librevenge::RVNG_SEEK_CUR);
    }
    if (input->tell()!=pos) {
      ascii().addPos(pos);
      ascii().addNote("Entries(Unknown):");
    }
  }
  return true;
}

bool CricketDrawParser::readShape()
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  const int vers=version();
  int expectedSize=vers>2 ? 192 : 180;
  if (!input->checkPosition(pos+expectedSize))
    return false;
  libmwaw::DebugStream f;
  f << "Entries(Shape):";
  CricketDrawParserInternal::Shape shape;
  f << "IDS=[";
  for (auto &id : shape.m_ids) {
    id=long(input->readULong(4));
    if (id)
      f << std::hex << id << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  int val,type=0;
  if (vers>2) {
    for (int st=0; st<2; ++st) {
      float dim[4];
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      shape.m_box[st]=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
      f << "box" << st << "=" << shape.m_box[st] << ",";
    }
    for (int i=0; i<6; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  else {
    val=static_cast<int>(input->readULong(2));
    if (val&0x100) f << "selected,";
    val &=0xFEFF;
    if (val) f << "f0=" << std::hex << val << std::dec << ",";
    for (int i=0; i<2; ++i) { // f2=0|1
      val=static_cast<int>(input->readLong(2));
      if (i==0) {
        if (val&0x100) f << "locked,";
        shape.m_locked=true;
        val&=0xFEFF;
      }
      if (val) f << "f" << i+2 << "=" << val << ",";
    }
  }
  type=static_cast<int>(input->readLong(2));
  if (type<0 || type>0x10) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  switch (type) {
  case 0:
    shape.m_type=CricketDrawParserInternal::Shape::Group;
    f << "group,";
    break;
  case 1:
    shape.m_type=CricketDrawParserInternal::Shape::Textbox;
    f << "textbox,";
    break;
  case 2:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "line,";
    break;
  case 3:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "rect,";
    break;
  case 4:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "rectOval,";
    break;
  case 5:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "circle,";
    break;
  case 6:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "arc,";
    break;
  case 7:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    shape.m_closed=true;
    f << "diamond,";
    break;
  case 8:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "poly,";
    break;
  case 9:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "spline,";
    break;
  case 10: // list of line
    shape.m_type=CricketDrawParserInternal::Shape::Grate;
    f << "grate,";
    break;
  case 11:
    shape.m_type=CricketDrawParserInternal::Shape::StarBurst;
    f << "starburst,";
    break;
  case 12:
    shape.m_type=CricketDrawParserInternal::Shape::Basic;
    f << "bezier,";
    break;
  case 13:
    shape.m_type=CricketDrawParserInternal::Shape::Picture;
    f << "picture,";
    break;
  case 14:
    shape.m_type=CricketDrawParserInternal::Shape::GroupEnd;
    f << "endgroup,";
    break;
  default:
    f << "type=" << type << ",";
    break;
  }
  if (vers<=2) {
    val=static_cast<int>(input->readLong(2)); // always 0
    if (val&1) {
      shape.m_smoothed=true;
      f << "smooth,";
    }
    val &=0xFFFE;
    if (val) f << "f4=" << val << ",";
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    shape.m_box[0]=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << "dim=" << shape.m_box[0] << ",";
  }
  else {
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f6=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val&2) f << "selected,";
    if (val&8) f << "locked,";
    val &= 0xFFF5;
    if (val) f << "f7=" << std::hex << val << std::dec << ",";
  }
  MWAWGraphicStyle &style=shape.m_style;
  f << "line=[";
  style.m_lineWidth=float(input->readLong(4))/65536.f;
  f << "width=" << style.m_lineWidth << ",";
  val=static_cast<int>(input->readULong(1));
  if (val==0) { // checkme
    style.m_lineWidth=0;
    f << "none,";
  }
  else if (val!=1) {
    m_state->getDash(val, style.m_lineDashWidth);
    f << "dash=" << val << ",";
  }
  auto intensity=static_cast<int>(input->readULong(1));
  if (intensity!=100)
    f << "intensity=" << intensity << ",";
  val=static_cast<int>(input->readULong(2));
  if (!m_state->getColor(val, intensity, style.m_lineColor))
    f << "##color=" << val << ",";
  else if (!style.m_lineColor.isBlack())
    f << "color=" << style.m_lineColor << ",";
  f << "],";
  for (int i=0; i<2; ++i) { // 0
    val=static_cast<int>(input->readLong(2));
    if (val) f << "g" << i+4 << "=" << val << ",";
  }
  val=static_cast<int>(input->readULong(2)); // 0|f02a
  if (val) f << "g2=" << std::hex << val << std::dec << ",";
  f << "surf=[";
  intensity=static_cast<int>(input->readULong(1));
  if (intensity) f << "intensity=" << intensity << ",";
  val=static_cast<int>(input->readULong(1));
  if (val!=14) f << "f0=" << val <<  ",";
  val=static_cast<int>(input->readULong(2));
  MWAWColor col;
  if (!m_state->getColor(val, intensity,col))
    f << "##color=" << val << ",";
  else {
    style.setSurfaceColor(col);
    shape.m_textColor=col;
    if (!col.isWhite())
      f << "color=" << col << ",";
  }
  f << "],";
  val=static_cast<int>(input->readULong(2));
  if (val!=0x1bb9)
    f << "g3=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) { // 0
    val=static_cast<int>(input->readLong(2));
    if (val) f << "g" << i+3 << "=" << val << ",";
  }

  val=static_cast<int>(input->readULong(1));
  if (val==1) {
    f << "shadow=[";
    int totalIntensity=0;
    for (int i=0; i<3; ++i) {
      val=static_cast<int>(input->readULong(1));
      totalIntensity+=val;
      int const expected[] = {0x64,0x19,0x4b};
      if (val==expected[i]) continue;
      char const *wh[]= {"begin","end","borders"};
      f << wh[i] << "[intensities]=" << val << ",";
    }
    float decal[2];
    for (auto &d : decal) d=float(input->readLong(4))/65536.f;
    style.m_shadowOffset=MWAWVec2f(decal[1],decal[0]);
    f << "decal=" << style.m_shadowOffset << ",";
    val=static_cast<int>(input->readLong(2));
    if (!m_state->getColor(val, totalIntensity/3, col))
      f << "###color=" << val << ",";
    else {
      f << "color=" << col << ",";
      style.setShadowColor(col);
    }
    f << "],";
  }
  else {
    if (val) {
      MWAW_DEBUG_MSG(("CricketDrawParser::readShape: unknown shadow type\n"));
      f << "##shadow[type]=" << val << ",";
    }
    input->seek(13, librevenge::RVNG_SEEK_CUR);
  }
  for (int i=0; i<3; ++i) { // fl
    val=static_cast<int>(input->readULong(2));
    if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  f.str("");
  f << "Shape-A[" << type << "]:";
  pos=input->tell();
  expectedSize-=(vers<=2 ? 78 : 102);
  auto fountainType=static_cast<int>(input->readLong(1));
  switch (fountainType) {
  case 0:
    break;
  case 1:
    f << "fountain[type]=linear,";
    break;
  case 2:
    f << "fountain[type]=log,";
    break;
  case 3:
    f << "fountain[type]=radial,";
    break;
  default:
    MWAW_DEBUG_MSG(("CricketDrawParser::readShape: unknown fountain type\n"));
    f << "##fountain[type]=" << fountainType << ",";
    fountainType=0;
    break;
  }
  if (fountainType) {
    f << "fountain=[";
    int intensityVal[2];
    for (int i=0; i<2; ++i) intensityVal[1-i]=static_cast<int>(input->readULong(1));
    if (intensityVal[0]) f << "beg[intensity]=" << intensityVal[0] << ",";
    if (intensityVal[1]!=100) f << "end[intensity]=" << intensityVal[1] << ",";
    val=static_cast<int>(input->readLong(1)); // always 0?
    if (val) f << "f0=" << val << ",";
    auto angle=static_cast<int>(input->readLong(2));
    if (angle) f << "angle=" << angle << ",";
    float dim[2];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    MWAWVec2f center(dim[1],dim[0]);
    if (center!=MWAWVec2f(0,0)) f << "center=" << center << ",";
    val=static_cast<int>(input->readLong(2));
    if (!m_state->getColor(val, 100, col))
      f << "###color=" << val << ",";
    else {
      if (!col.isBlack())
        f << "color=" << col << ",";
      // set the gradient
      auto &finalGrad=style.m_gradient;
      if (fountainType==3) {
        finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Radial;
        finalGrad.m_percentCenter=MWAWVec2f(0.5f,0.5f); // fixme: use center and bdbox here
      }
      else {
        finalGrad.m_type= MWAWGraphicStyle::Gradient::G_Linear;
        finalGrad.m_angle=float(angle+90);
      }
      finalGrad.m_stopList.resize(2);
      for (size_t i=0; i<2; ++i)
        finalGrad.m_stopList[i]
          =MWAWGraphicStyle::Gradient::Stop(float(i),MWAWColor::barycenter(float(intensityVal[i])/100.f, col, float(100-intensityVal[i])/100.f, MWAWColor::white()));
    }
    f << "],";
  }
  else
    input->seek(15, librevenge::RVNG_SEEK_CUR);
  for (int i=0; i<3; ++i) { // fl
    val=static_cast<int>(input->readULong(2));
    if (val) f << "fl" << i+3 << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<2; ++i) { // 0
    val=static_cast<int>(input->readLong(2));
    if (i==0 && (val&0x300)) {
      if (val&0x100) {
        style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
        f << "arrow[beg],";
      }
      if (val&0x200) {
        style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
        f << "arrow[end],";
      }
      val &= 0xFCFF;
    }
    if (val) f << "f" << i+7 << "=" << val << ",";
  }
  float dim[2];
  for (auto &d : dim) d=float(input->readLong(4))/65536.f;
  shape.m_translation=MWAWVec2f(dim[1],dim[0]);
  f << "orig=" << shape.m_translation << ",";
  val=static_cast<int>(input->readLong(4));
  if (val) {
    shape.m_rotation=float(val)/65536.f;
    f << "rotate=" << shape.m_rotation << ",";
  }
  val=static_cast<int>(input->readLong(4));
  if (val) {
    shape.m_shear=float(val)/65536.f;
    f << "shear[angle]=" << shape.m_shear << ",";
  }
  val=static_cast<int>(input->readULong(1));
  if (val&1) {
    shape.m_flip[1]=true;
    f << "flipY,";
  }
  if (val&2) {
    shape.m_flip[0]=true;
    f << "flipX,";
  }
  val &=0xFC;
  if (val) f << "g0=" << val << ",";
  val=static_cast<int>(input->readULong(1)); // small number
  if (val) f << "g1=" << val << ",";
  int const numData=vers<=2 ? 9 : 5;
  for (int i=0; i<numData; ++i) {
    val=static_cast<int>(input->readLong(2));
    int const expected[]= {0,0x2d,0x48,0,0,0,0,0,0};
    if (val!=expected[i])
      f << "g" << i+2 << "=" << val << ",";
  }
  auto dataSize=long(input->readULong(4));
  if (dataSize<0 || !input->checkPosition(pos+expectedSize+dataSize)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (dataSize)
    f << "dataSize=" << dataSize << ",";
  long bitmapSize=0;
  switch (type) {
  case 4: { // rectOval
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    shape.m_corners=MWAWVec2f(dim[1],dim[0]);
    f << "corners=" << shape.m_corners << ",";
    break;
  }
  case 6: { // arc
    val=static_cast<int>(input->readULong(2));
    if (val) // f | 2f0c
      f << "h0=" << std::hex << val << std::dec << ",";
    int angles[2];
    for (auto &angle : angles) angle=static_cast<int>(input->readLong(2));
    shape.m_angles=MWAWVec2i(angles[0], angles[1]);
    f << "angles=" << shape.m_angles << ",";
    // then 4 small int
    break;
  }
  case 10: // grate
    shape.m_grateN=static_cast<int>(input->readLong(2));
    f << "grate[number]=" << shape.m_grateN << ",";
    shape.m_grateType=static_cast<int>(input->readLong(2));
    switch (shape.m_grateType) {
    case 0: // uniform
      break;
    case 1:
      f << "grad[type]=log,";
      break;
    case 2:
      f << "grad[type]=radial,";
      break;
    default:
      f << "#grad[type]=" << shape.m_grateType << ",";
    }
    break;
  case 11: //starbust
    for (auto &angle : shape.m_starBustAngles) angle=static_cast<int>(input->readLong(2));
    if (shape.m_starBustAngles[0]) f << "starbust[beg]=" << shape.m_starBustAngles[0] << ",";
    if (shape.m_starBustAngles[1]!=160) f << "starbust[end]=" << shape.m_starBustAngles[1] << ",";
    if (shape.m_starBustAngles[2]!=10) f << "starbust[delta]=" << shape.m_starBustAngles[2] << ",";
    break;
  case 13: {
    for (int i=0; i<2; ++i)
      f << "flA" << i << "=" << std::hex << input->readULong(2) << std::dec << ",";
    shape.m_bitmapRowSize=static_cast<int>(input->readULong(2));
    f << "bitmap[rowSize]=" << shape.m_bitmapRowSize << ","; // 4|8|e
    int dimInt[4];
    for (auto &d : dimInt) d=static_cast<int>(input->readULong(2));
    shape.m_bitmapDimension=MWAWBox2i(MWAWVec2i(dimInt[1],dimInt[0]),MWAWVec2i(dimInt[3],dimInt[2]));
    f << "dim=" << shape.m_bitmapDimension << ",";
    bitmapSize=long(input->readULong(4));
    if (bitmapSize<0 || !input->checkPosition(pos+expectedSize+dataSize+bitmapSize)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("CricketDrawParser::readShape: can not read bitmap size\n"));
      return false;
    }
    shape.m_bitmapScale=static_cast<int>(input->readLong(2));
    if (shape.m_bitmapScale!=100) f << "scaling=" << shape.m_bitmapScale << "%";
    break;
  }
  case 14: // endgroup
    val=static_cast<int>(input->readLong(2)); // always 0
    if (val) f << "h0=" << val << ",";
    val=static_cast<int>(input->readLong(2));
    if (val) f << "N=" << val << ",";
    ascii().addDelimiter(input->tell(),'|');
    input->seek(16, librevenge::RVNG_SEEK_CUR);
    ascii().addDelimiter(input->tell(),'|');
    f << "IDS=[";
    for (auto &id : shape.m_groupIds) {
      id=long(input->readULong(4));
      f << std::hex << id << std::dec << ",";
    }
    f << "],";
    break;
  default:
    break;
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+expectedSize, librevenge::RVNG_SEEK_SET);
  if (dataSize) {
    pos=input->tell();
    f.str("");
    f << "Shape-data[" << type << "]:";
    bool ok=true;
    switch (type) {
    case 2:
      if (dataSize!=16) {
        ok=false;
        break;
      }
      f << "pts=[";
      for (int pt=0; pt<2; ++pt) {
        float coord[2];
        for (auto &c : coord) c=float(input->readLong(4))/65536.f;
        shape.m_vertices.push_back(MWAWVec2f(coord[1],coord[0]));
        f << shape.m_vertices.back() << ",";
      }
      f << "],";
      shape.m_shape=MWAWGraphicShape::line(shape.m_vertices[0], shape.m_vertices[1]);
      break;
    case 1:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 10:
    case 11:
    case 12:
    case 13:
      if (dataSize!=32) {
        ok=false;
        break;
      }
      f << "pts=[";
      for (int pt=0; pt<4; ++pt) {
        float coord[2];
        for (auto &c : coord) c=float(input->readLong(4))/65536.f;
        shape.m_vertices.push_back(MWAWVec2f(coord[1],coord[0]));
        f << shape.m_vertices.back() << ",";
      }
      f << "],";
      switch (type) {
      case 3:
      case 4:
        shape.m_shape=MWAWGraphicShape::rectangle(MWAWBox2f(shape.m_vertices[0], shape.m_vertices[2]), shape.m_corners);
        break;
      case 5:
        shape.m_shape=MWAWGraphicShape::circle(MWAWBox2f(shape.m_vertices[0], shape.m_vertices[2]));
        break;
      case 6: {
        MWAWBox2f box(shape.m_vertices[0], shape.m_vertices[2]);
        int angle[2] = { shape.m_angles[0], shape.m_angles[1] };
        if (box.min()[1]>box.max()[1]) {
          std::swap(box.min()[1],box.max()[1]);
          angle[0]=180-shape.m_angles[1];
          angle[1]=180-shape.m_angles[0];
        }
        if (angle[1]<=angle[0])
          std::swap(angle[0],angle[1]);
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
        MWAWBox2f circleBox(box);
        // we have the shape box, we need to reconstruct the circle box
        if (maxVal[0]>minVal[0] && maxVal[1]>minVal[1]) {
          float scaling[2]= { (box[1][0]-box[0][0])/(maxVal[0]-minVal[0]),
                              (box[1][1]-box[0][1])/(maxVal[1]-minVal[1])
                            };
          float constant[2]= { box[0][0]-minVal[0] *scaling[0], box[0][1]-minVal[1] *scaling[1]};
          circleBox=MWAWBox2f(MWAWVec2f(constant[0]-scaling[0], constant[1]-scaling[1]),
                              MWAWVec2f(constant[0]+scaling[0], constant[1]+scaling[1]));
        }
        shape.m_shape = MWAWGraphicShape::arc(box, circleBox, MWAWVec2f(float(angle[0]),float(angle[1])));
        break;
      }
      case 7: // diamond
        shape.m_shape.m_type=MWAWGraphicShape::Polygon;
        shape.m_shape.m_bdBox=MWAWBox2f(shape.m_vertices[0], shape.m_vertices[2]);
        shape.m_shape.m_vertices=shape.m_vertices;
        break;
      case 12: // small bezier curve: 4 points
        shape.m_shape.m_type=MWAWGraphicShape::Path;
        shape.m_shape.m_bdBox=MWAWBox2f(shape.m_box[0][0], shape.m_box[0][1]);
        shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('M', shape.m_vertices[0]));
        shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('C', shape.m_vertices[3],shape.m_vertices[1],shape.m_vertices[2]));
        break;
      default:
        break;
      }
      break;
    case 8:
    case 9:
      if (dataSize%8) {
        ok=false;
        break;
      }
      f << "N=" << (dataSize/8) << ",";
      f << "pts=[";
      for (long pt=0; pt<(dataSize/8); ++pt) {
        float coord[2];
        for (auto &c : coord) c=float(input->readLong(4))/65536.f;
        shape.m_vertices.push_back(MWAWVec2f(coord[1],coord[0]));
        f << shape.m_vertices.back() << ",";
      }
      f << "],";
      if (!dataSize) break;
      shape.m_shape.m_bdBox=MWAWBox2f(shape.m_box[0][0], shape.m_box[0][1]);
      if (type==8 && !shape.m_smoothed) {
        shape.m_shape.m_type=MWAWGraphicShape::Polygon;
        shape.m_shape.m_vertices=shape.m_vertices;
      }
      else if (type==8) {
        shape.m_shape.m_type=MWAWGraphicShape::Path;
        shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('M', shape.m_vertices[0]));
        for (size_t i=0; i+1<shape.m_vertices.size(); ++i)
          shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('Q', 0.5f*(shape.m_vertices[i]+shape.m_vertices[i+1]),
                                         shape.m_vertices[i]));
        shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('T', shape.m_vertices.back()));
      }
      else if (type==9) {
        if ((shape.m_vertices.size()%2)==0) {
          MWAW_DEBUG_MSG(("CricketDrawParser::readShape: find uneven number of point, ignore last one\n"));
          f << "###odd";
        }
        shape.m_shape.m_type=MWAWGraphicShape::Path;
        shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('M', shape.m_vertices[0]));
        for (size_t i=1; i+1<shape.m_vertices.size(); i+=2)
          shape.m_shape.m_path.push_back(MWAWGraphicShape::PathData('Q', shape.m_vertices[i+1], shape.m_vertices[i]));
      }
      break;
    default:
      ok=false;
      break;
    }
    if (!ok) {
      MWAW_DEBUG_MSG(("CricketDrawParser::readShape: find unexpected data size for type %d\n", type));
      f << "###";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+dataSize, librevenge::RVNG_SEEK_SET);
  }
  if (type==1) {
    pos=input->tell();
    auto sSz=static_cast<int>(input->readULong(2));
    if (!input->checkPosition(pos+2+sSz+(sSz%2)+38)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    f.str("");
    f << "Shape-text:";
    shape.m_text.setBegin(pos+2);
    shape.m_text.setLength(sSz);
    if (sSz%2) ++sSz;
    input->seek(pos+2+sSz, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    pos=input->tell();
    f.str("");
    f << "Shape-textPLC:";
    for (int i=0; i<6; ++i) { // f5 probably related to line height
      val=static_cast<int>(input->readLong(2));
      int const expected[]= {6,0,0,0x15,4,0x2e};
      if (val!=expected[i]) f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(1));
    switch (val) {
    case 0: // left
      break;
    case 1:
      f << "right,";
      shape.m_paragraph.m_justify=MWAWParagraph::JustificationRight;
      break;
    case 2:
      f << "center,";
      shape.m_paragraph.m_justify=MWAWParagraph::JustificationCenter;
      break;
    case 3:
      f << "justify=all,";
      shape.m_paragraph.m_justify=MWAWParagraph::JustificationFull;
      break;
    default:
      MWAW_DEBUG_MSG(("CricketDrawParser::readShape: find unexpected align\n"));
      f << "###align=" << val << ",";
      break;
    }
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f6=" << val << ",";
    val=static_cast<int>(input->readLong(1));
    switch (val) {
    case 0: // normal
      break;
    case 1:
      f << "interline=150%,";
      shape.m_paragraph.setInterline(1.5, librevenge::RVNG_PERCENT);
      break;
    case 2:
      f << "interline=200%,";
      shape.m_paragraph.setInterline(2., librevenge::RVNG_PERCENT);
      break;
    default:
      MWAW_DEBUG_MSG(("CricketDrawParser::readShape: find unexpected align\n"));
      f << "###interline=" << val << ",";
      break;
    }

    ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+38, librevenge::RVNG_SEEK_SET);
  }
  if (bitmapSize) {
    pos=input->tell();
    shape.m_bitmap.setBegin(pos);
    shape.m_bitmap.setLength(bitmapSize);
    f.str("");
    f << "Entries(Bitmap):";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+bitmapSize, librevenge::RVNG_SEEK_SET);
  }
  shape.m_id=static_cast<int>(m_state->m_shapeList.size());
  m_state->m_shapeList.push_back(shape);
  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool CricketDrawParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = CricketDrawParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(0x200))
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  auto vers=static_cast<int>(input->readULong(2));
  if (vers<=0 || vers>4)
    return false;
  f << "vers=" << vers << ",";
  auto sSz=static_cast<int>(input->readULong(1));
  if (sSz<6||sSz>8) return false;
  std::string date;
  int numSlash=0;
  for (int i=0; i<sSz; ++i) {
    auto c=char(input->readULong(1));
    date+=c;
    if (c=='/')
      ++numSlash;
    else if (c<'0' || c>'9')
      return false;
  }
  if (numSlash!=2) return false;
  f << "vers[date]=" << date << ",";
  input->seek(12, librevenge::RVNG_SEEK_SET);
  auto val=static_cast<int>(input->readULong(2)); // always 0?
  if (val)
    f << "f0=" << val << ",";
  int dim[4];
  for (auto &d : dim) d=static_cast<int>(input->readLong(2));
  f << "dim=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]), MWAWVec2i(dim[2],dim[3])) << ",";
  if (strict && !readPrintInfo()) { // check if empty
    input->seek(22,librevenge::RVNG_SEEK_SET);
    for (int i=0; i<4; ++i) {
      if (input->readLong(4)) return false;
    }
    return false;
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_CRICKETDRAW, vers, MWAWDocument::MWAW_K_DRAW);
  input->seek(22,librevenge::RVNG_SEEK_SET);

  return true;
}

////////////////////////////////////////////////////////////
// try to read the print info zone
////////////////////////////////////////////////////////////
bool CricketDrawParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  long endPos=pos+120;
  if (!input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("CricketDrawParser::readPrintInfo: file seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo):";
  libmwaw::PrinterInfo info;
  if (!info.read(input)) {
    MWAW_DEBUG_MSG(("CricketDrawParser::readPrintInfo: can not read print info\n"));
    return false;
  }
  f << info;
  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }

  // define margin from print info
  MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
  MWAWVec2i rBotMargin=info.paper().size() - info.page().size();

  // move margin left | top
  int decalX = lTopMargin.x() > 14 ? lTopMargin.x()-14 : 0;
  int decalY = lTopMargin.y() > 14 ? lTopMargin.y()-14 : 0;
  lTopMargin -= MWAWVec2i(decalX, decalY);
  rBotMargin += MWAWVec2i(decalX, decalY);

  // decrease right | bottom
  int rightMarg = rBotMargin.x() -50;
  if (rightMarg < 0) rightMarg=0;
  int botMarg = rBotMargin.y() -50;
  if (botMarg < 0) botMarg=0;

  getPageSpan().setMarginTop(lTopMargin.y()/72.0);
  getPageSpan().setMarginBottom(botMarg/72.0);
  getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
  getPageSpan().setMarginRight(rightMarg/72.0);
  getPageSpan().setFormLength(paperSize.y()/72.);
  getPageSpan().setFormWidth(paperSize.x()/72.);

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool CricketDrawParser::sendAll()
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendAll: can not find the listener\n"));
    return false;
  }
  std::stack<MWAWTransformation> transformationStack;
  MWAWVec2f LT(float(getPageSpan().getMarginLeft())*72.f, float(getPageSpan().getMarginTop())*72.f);
  transformationStack.push(MWAWTransformation::translation(LT));
  for (size_t i=0; i<m_state->m_shapeList.size(); ++i) {
    auto const &shape=m_state->m_shapeList[i];
    if (shape.m_type==CricketDrawParserInternal::Shape::GroupEnd) {
      MWAWTransformation transformation=shape.getTransformation(transformationStack.top());
      transformationStack.push(transformation);
      MWAWBox2f box=transformation*shape.m_box[0];
      MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      listener->openGroup(pos);
      continue;
    }
    if (shape.m_type==CricketDrawParserInternal::Shape::Group) {
      if (transformationStack.size()>1) {
        transformationStack.pop();
        listener->closeGroup();
      }
      else if (i+1!=m_state->m_shapeList.size()) {
        MWAW_DEBUG_MSG(("CricketDrawParser::sendAll: can not find the group end shape\n"));
      }
      continue;
    }
    send(shape, transformationStack.top());
  }
  if (transformationStack.size()>1) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendAll: find some unclosed group\n"));
    for (size_t i=1; i<transformationStack.size(); ++i)
      listener->closeGroup();
  }
  return true;
}

bool CricketDrawParser::send(CricketDrawParserInternal::Shape const &shape, MWAWTransformation const &transform)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CricketDrawParser::send: can not find the listener\n"));
    return false;
  }
  if (shape.m_isSent) {
    MWAW_DEBUG_MSG(("CricketDrawParser::send: the shape is already sent\n"));
    return false;
  }
  shape.m_isSent=true;
  MWAWTransformation transformation=shape.getTransformation(transform);
  switch (shape.m_type) {
  case CricketDrawParserInternal::Shape::Basic: {
    MWAWGraphicShape finalShape=shape.m_shape.transform(transformation);
    MWAWBox2f box=finalShape.getBdBox();
    MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    pos.setOrder(static_cast<int>(m_state->m_shapeList.size())-shape.m_id);
    listener->insertShape(pos, finalShape, shape.m_style);
    break;
  }
  case CricketDrawParserInternal::Shape::Textbox: {
    MWAWGraphicStyle style(MWAWGraphicStyle::emptyStyle());
    MWAWTransformation transf;
    float rotation=0;
    MWAWVec2f shearing;
    MWAWBox2f box=transformation*shape.m_box[0];
    if (transformation.decompose(rotation,shearing,transf,shape.m_box[0].center())) {
      box=transf*shape.m_box[0];
      style.m_rotate=-rotation;
    }
    MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    pos.setOrder(static_cast<int>(m_state->m_shapeList.size())-shape.m_id);
    for (int i=0; i<2; ++i) style.m_flip[i]=shape.m_flip[i];
    std::shared_ptr<MWAWSubDocument> doc(new CricketDrawParserInternal::SubDocument(*this, getInput(), shape.m_id));

    listener->insertTextBox(pos, doc, style);
    break;
  }
  case CricketDrawParserInternal::Shape::Grate: {
    if (shape.m_grateN<=0) {
      MWAW_DEBUG_MSG(("CricketDrawParser::send: the number of line seems bad\n"));
      break;
    }
    if (shape.m_grateType<0||shape.m_grateType>2) {
      MWAW_DEBUG_MSG(("CricketDrawParser::send: sorry unexpected grate type\n"));
      break;
    }
    MWAWBox2f box=MWAWBox2f(shape.m_box[0][0], shape.m_box[0][1]);
    MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
    MWAWVec2f center=0.5f*(box[0]+box[1]);
    MWAWVec2f dir1=(shape.m_grateType==2) ? (box[1]-box[0]) : MWAWVec2f(box[1][0]-box[0][0],0);
    MWAWVec2f dir2(0, box[1][1]-box[0][1]);
    float decal;
    for (int i=0; i<shape.m_grateN; ++i) {
      if (i==0)
        decal=0;
      else if (i+1==shape.m_grateN)
        decal=1;
      else if (shape.m_grateType==1)
        decal=float(std::log(i+1)/std::log(shape.m_grateN));
      else
        decal=float(i)/float(shape.m_grateN-1);
      MWAWGraphicShape line;
      if (shape.m_grateType==2) // fixme: normally the lines are (portion of) circles and not ellipsis
        line=MWAWGraphicShape::circle(MWAWBox2f(center-0.5f*(1.f-decal)*dir1, center+0.5f*(1.f-decal)*dir1));
      else
        line=MWAWGraphicShape::line(box[0]+(1.f-decal)*dir2, box[0]+(1.f-decal)*dir2+dir1);
      line = line.transform(transformation);
      MWAWBox2f lineBox=line.getBdBox();
      pos=MWAWPosition(lineBox[0], lineBox.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      pos.setOrder(static_cast<int>(m_state->m_shapeList.size())-shape.m_id);
      listener->insertShape(pos, line, shape.m_style);
    }
    listener->closeGroup();
    break;
  }
  case CricketDrawParserInternal::Shape::StarBurst: {
    if (shape.m_starBustAngles[0]>shape.m_starBustAngles[1] || shape.m_starBustAngles[2]<=0) {
      MWAW_DEBUG_MSG(("CricketDrawParser::send: the star burst angles seems bad\n"));
      break;
    }
    MWAWBox2f box=MWAWBox2f(shape.m_box[0][0], shape.m_box[0][1]);
    MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
    MWAWVec2f center=0.5f*(box[0]+box[1]);
    MWAWVec2f dir(0.5f*(box[1][0]-box[0][0]), 0.5f*(box[1][1]-box[0][1]));
    for (int angle=shape.m_starBustAngles[0]; angle<=shape.m_starBustAngles[1]; angle+=shape.m_starBustAngles[2]) {
      float angl=float(M_PI)/180.f*float(angle);
      MWAWGraphicShape line=MWAWGraphicShape::line(center,center+MWAWVec2f(std::cos(angl)*dir[0],-std::sin(angl)*dir[1]));
      line = line.transform(transformation);
      MWAWBox2f lineBox=line.getBdBox();
      pos=MWAWPosition(lineBox[0], lineBox.size(), librevenge::RVNG_POINT);
      pos.m_anchorTo = MWAWPosition::Page;
      pos.setOrder(static_cast<int>(m_state->m_shapeList.size())-shape.m_id);
      listener->insertShape(pos, line, shape.m_style);
    }
    listener->closeGroup();
    break;
  }
  case CricketDrawParserInternal::Shape::Picture:
    return sendBitmap(shape, transform);
  case CricketDrawParserInternal::Shape::Group:
  case CricketDrawParserInternal::Shape::GroupEnd:
  case CricketDrawParserInternal::Shape::Unknown:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  return true;
}

bool CricketDrawParser::sendText(int zId)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendText: can not find the listener\n"));
    return false;
  }
  if (zId<0||zId>=static_cast<int>(m_state->m_shapeList.size()) ||
      m_state->m_shapeList[size_t(zId)].m_type != CricketDrawParserInternal::Shape::Textbox) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendText: can not find the text shape\n"));
    return false;
  }
  auto const &shape=m_state->m_shapeList[size_t(zId)];
  if (!shape.m_text.valid())
    return true;

  listener->setParagraph(shape.m_paragraph);
  MWAWFont font(3,12);
  font.setColor(shape.m_textColor);
  listener->setFont(font);
  MWAWInputStreamPtr input=getInput();
  input->seek(shape.m_text.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Shape[text]:";
  long endPos=shape.m_text.end();
  while (!input->isEnd()) {
    if (input->tell()>=shape.m_text.end())
      break;
    auto c = char(input->readULong(1));
    if (c==0) {
      MWAW_DEBUG_MSG(("CricketDrawParser::sendText: find char 0\n"));
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
  ascii().addPos(shape.m_text.begin());
  ascii().addNote(f.str().c_str());
  return true;
}

bool CricketDrawParser::sendBitmap(CricketDrawParserInternal::Shape const &bitmap, MWAWTransformation const &transform)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendBitmap: can not find the listener\n"));
    return false;
  }
  MWAWVec2i dim=bitmap.m_bitmapDimension.size();
  if (!bitmap.m_bitmap.valid() || bitmap.m_bitmapRowSize<=0 ||
      8*bitmap.m_bitmapRowSize<dim[0] ||
      dim[0] <= 0 || dim[1] <= 0 ||
      dim[1]>bitmap.m_bitmap.length()/bitmap.m_bitmapRowSize) {
    MWAW_DEBUG_MSG(("CricketDrawParser::sendBitmap: oops, the bitmap dimension seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  input->seek(bitmap.m_bitmap.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  // a bitmap is composed of 720 rows of (72x8bytes)
  std::shared_ptr<MWAWPictBitmapIndexed> pict;
  pict.reset(new MWAWPictBitmapIndexed(dim));
  std::vector<MWAWColor> colors(2);
  colors[0]=MWAWColor::white();
  colors[1]=bitmap.m_textColor;
  pict->setColors(colors);

  for (int r=0; r<dim[1]; ++r) {
    long rowPos=input->tell();
    f.str("");
    f << "Entries(Bitmap)-" << r << ":";
    int col=0;
    while (col<dim[0]) {
      auto color=static_cast<int>(input->readULong(1));
      for (int b=7; b>=0; --b) {
        if (col>=dim[0])
          break;
        pict->set(col++, r, (color>>b)&1);
      }
    }
    input->seek(rowPos+bitmap.m_bitmapRowSize, librevenge::RVNG_SEEK_SET);
    ascii().addPos(rowPos);
    ascii().addNote(f.str().c_str());
  }

  MWAWEmbeddedObject picture;
  if (!pict->getBinary(picture)) return false;

  MWAWTransformation transformation=bitmap.getTransformation(transform);
  MWAWBox2f box=transformation*bitmap.m_box[0];
  MWAWTransformation transf;
  float rotation=0;
  MWAWVec2f shearing;
  MWAWGraphicStyle style(MWAWGraphicStyle::emptyStyle());
  if (transformation.decompose(rotation,shearing,transf,bitmap.m_box[0].center())) {
    box=transf*bitmap.m_box[0];
    style.m_rotate=-rotation;
  }

  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  pos.setOrder(static_cast<int>(m_state->m_shapeList.size())-bitmap.m_id);
  listener->insertPicture(pos, picture, style);

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
