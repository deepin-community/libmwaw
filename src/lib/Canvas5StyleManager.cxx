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
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWParser.hxx"

#include "Canvas5Image.hxx"
#include "Canvas5Parser.hxx"
#include "Canvas5Structure.hxx"

#include "Canvas5StyleManager.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a Canvas5StyleManager */
namespace Canvas5StyleManagerInternal
{

////////////////////////////////////////
//! Internal: the color style of a Canvas5StyleManager
struct ColorStyle {
  //! constructor
  ColorStyle()
    : m_type(1)
    , m_nameId(0)
    , m_color()
    , m_gradient()
    , m_hatch()
    , m_textureDim(0,0)
    , m_texture()
  {
  }
  friend std::ostream &operator<<(std::ostream &o, ColorStyle const &color)
  {
    if (color.m_type==0)
      o << "transparent,";
    else if (color.m_type==1 && color.m_color.isSet())
      o << *color.m_color << ",";
    else
      o << "type=" << Canvas5Structure::getString(color.m_type) << ",";
    if (color.m_nameId)
      o << "N" << color.m_nameId << ",";
    return o;
  }
  //! the type
  unsigned m_type;
  //! the name id
  int m_nameId;
  //! the basic color
  MWAWVariable<MWAWColor> m_color;
  //! the gradient
  MWAWGraphicStyle::Gradient m_gradient;
  //! the hatch
  MWAWGraphicStyle::Hatch m_hatch;
  //! the texture dimension
  MWAWVec2i m_textureDim;
  //! the embedded objet (texture)
  MWAWEmbeddedObject m_texture;
};

////////////////////////////////////////
//! Internal: the pen style of a Canvas5StyleManager
struct PenStyle {
  /// a line of a Canvas5StyleManager pen style
  struct Line {
    //! constructor
    Line()
      : m_size(1,1)
      , m_offset(0)
      , m_color(MWAWColor::black())
    {
    }
    //! the line width
    MWAWVec2f m_size;
    //! the offset
    float m_offset;
    //! the line color
    MWAWVariable<MWAWColor> m_color;
  };
  //! constructor
  PenStyle()
    : m_type(1)
    , m_size(1,1)
    , m_usePenColor(true)
    , m_lines()
  {
    for (auto &c : m_colors) c=MWAWColor::black();
  }
  friend std::ostream &operator<<(std::ostream &o, PenStyle const &pen)
  {
    if (pen.m_type!=1)
      o << "type=" << Canvas5Structure::getString(pen.m_type) << ",";
    if (pen.m_size!=MWAWVec2f(1,1))
      o << "size=" << pen.m_size << ",";
    return o;
  }
  //! the type
  unsigned m_type;
  //! the pen size
  MWAWVec2f m_size;
  //! the neo color
  MWAWVariable<MWAWColor> m_colors[2];
  //! use pen ink
  bool m_usePenColor;
  //! the plin lines
  std::vector<Line> m_lines;
};

////////////////////////////////////////
//! Internal: the stroke style of a Canvas5StyleManager
struct Stroke {
  //! constructor
  Stroke()
    : m_type(1)
    , m_penId(0)
    , m_dashId(0)
  {
    for (auto &id : m_arrowId) id=0;
  }
  friend std::ostream &operator<<(std::ostream &o, Stroke const &stroke)
  {
    if (stroke.m_type!=1)
      o << "type=" << Canvas5Structure::getString(stroke.m_type) << ",";
    if (stroke.m_penId)
      o << "Pe" << stroke.m_penId << ",";
    if (stroke.m_dashId)
      o << "Da" << stroke.m_dashId << ",";
    for (int i=0; i<2; ++i) {
      if (!stroke.m_arrowId[i]) continue;
      o << (i==0 ? "beg" : "end") << "=Ar" << stroke.m_arrowId[i] << ",";
    }
    return o;
  }
  //! the type
  unsigned m_type;
  //! the pen id
  int m_penId;
  //! the dash id
  int m_dashId;
  //! the arrow id (beg/end)
  int m_arrowId[2];
};

////////////////////////////////////////
//! Internal: the state of a Canvas5StyleManager
struct State {
  //! constructor
  State()
    : m_idToArrow()
    , m_idToColor()
    , m_idToPen()
    , m_idToDash()
    , m_idToStroke()
  {
  }

  //! the id to arrow map
  std::map<int, MWAWGraphicStyle::Arrow> m_idToArrow;
  //! the id to color style map
  std::map<int, std::shared_ptr<ColorStyle> > m_idToColor;
  //! the id to pen style map
  std::map<int, std::shared_ptr<PenStyle> > m_idToPen;
  //! the id to dash map
  std::map<int, std::vector<float> > m_idToDash;
  //! the id to stroke style map
  std::map<int, Stroke> m_idToStroke;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
Canvas5StyleManager::Canvas5StyleManager(Canvas5Parser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new Canvas5StyleManagerInternal::State)
  , m_mainParser(&parser)
{
}

Canvas5StyleManager::~Canvas5StyleManager()
{ }

int Canvas5StyleManager::version() const
{
  return m_parserState->m_version;
}

std::shared_ptr<Canvas5Image> Canvas5StyleManager::getImageParser() const
{
  return m_mainParser->m_imageParser;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

bool Canvas5StyleManager::readColor(Canvas5Structure::Stream &stream, MWAWVariable<MWAWColor> &color, std::string &extra)
{
  color.setSet(false);

  auto input=stream.input();
  long pos=input->tell();
  extra="";
  if (!input->checkPosition(pos+24)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readColor: file is to short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  for (int i=0; i<6; ++i) {
    int val=(i>0 && i<5) ? int(input->readULong(2)) : int(input->readLong(2));
    if (val==(i==5 ? -1 : 0)) continue;
    if (i>0 && i<5)
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  unsigned char cols[4];
  for (auto &c : cols) c=(unsigned char)(input->readULong(2)>>8);
  // cmyk, gray, rgb , sepp, pton, trum, toyo
  unsigned name=unsigned(input->readULong(4));
  f << Canvas5Structure::getString(name) << ",";
  if (name==0x67726179) // gray
    color=MWAWColor(cols[0],cols[0],cols[0]);
  else if (name==0x72676220) // rgb
    color=MWAWColor(cols[0],cols[1],cols[2],(unsigned char)(255-cols[3]));
  else {
    if (name==0x70746f6e) { // pton
      f << "##";
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readColor: this file contains pantom color, there will ne be converted correctly\n"));
        first=false;
      }
    }
    color=MWAWColor::colorFromCMYK(cols[0],cols[1],cols[2],cols[3]);
  }
  f << *color << ",";
  extra=f.str();
  return true;
}

bool Canvas5StyleManager::readGradient(std::shared_ptr<Canvas5Structure::Stream> stream, long len,
                                       MWAWGraphicStyle::Gradient &gradient)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;

  f << "Entries(ObFl):";
  int vers=version();
  long headerLength=vers==5 ? 56 : vers<9 ? 80+0x300 : 912;
  unsigned dataSize=vers==5 ? 28 : 60;
  if (len<headerLength || !input->checkPosition(pos+len)) {
    if (vers>5 && input->checkPosition(pos+len) && len>=56) {
      // find some v5 gradient in v6 files converted from v5, so let's us try to revert to v5
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: this does not look as a v6 gradient, try to read a v5 gradient\n"));
      f << "#v5,";
      vers=5;
      headerLength=56;
      dataSize=28;
    }
    else {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: unexpected length\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
  }

  int type;
  size_t N;
  if (vers==5) {
    N=size_t(input->readULong(4));
    f << "N=" << N << ",";
    type=int(input->readLong(4));
    f << "pts=[";
    MWAWVec2f pts[3];
    for (auto &pt : pts) {
      float fDim[2];
      for (auto &d : fDim) d=float(input->readLong(4))/65536;
      pt=MWAWVec2f(fDim[1],fDim[0]);
      f << pt << ",";
    }
    f << "],";
    if (pts[0]!=pts[1]) {
      MWAWVec2f dir=pts[1]-pts[0];
      gradient.m_angle=90-180*std::atan2(dir[1],dir[0])/float(M_PI);
    }
    float fDim[4];
    for (auto &d : fDim) d=float(input->readLong(4))/65536;
    MWAWBox2f box=MWAWBox2f(MWAWVec2f(fDim[1],fDim[0]),MWAWVec2f(fDim[3],fDim[2]));
    gradient.m_percentCenter=box.center();
    f << "box=" << box << ",";
    int val=int(input->readULong(4));
    if (val==1)
      f << "rainbow,";
    else if (val) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: find unknown rainbow type\n"));
      f << "##rainbow=" << val << ",";
    }
    val=int(input->readULong(2));
    if (val==1)
      f << "rainbow[inverted],";
    else if (val)
      f << "#rainbow[inverted]=" << val << ",";
    val=int(input->readULong(1));
    if (val)
      f << "h0=" << val << ",";
    val=int(input->readULong(1));
    if (val!=0x13)
      f << "h1=" << val << ",";
  }
  else {
    for (int i=0; i<2; ++i) {
      int val=int(input->readLong(4));
      int const expected[]= {vers<9 ? 0xdf : 0xfa, vers<9 ? 0x600 : 0x900};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
    type=int(input->readLong(4));
    auto fl=input->readULong(4);
    if (fl&1)
      f << "rainbow,";
    if ((fl&0x100)==0)
      f << "rainbow[inverted],";
    fl&=0xFFFFFEFE;
    if (fl!=0x1000)
      f << "fl=" << std::hex << fl << std::dec << ",";
    MWAWVec2f pts[2];
    for (auto &pt : pts) {
      float coords[2];
      for (auto &c : coords) c=float(m_mainParser->readDouble(*stream,vers<9 ? 4 : 8));
      pt=MWAWVec2f(coords[1],coords[0]);
    }
    if (type<=2) {
      MWAWVec2f dir=pts[1]-pts[0];
      gradient.m_angle=90-180*std::atan2(dir[1],dir[0])/float(M_PI);
      if (std::isnan(gradient.m_angle)) {
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: can not compute the gradient angle\n"));
        f << "###angle,";
        gradient.m_angle=0;
      }
      else if (gradient.m_angle<0 || gradient.m_angle>0)
        f << "angle=" << gradient.m_angle << ",";
    }
    else {
      MWAWBox2f box=MWAWBox2f(pts[0], pts[1]);
      gradient.m_percentCenter=box.center();
      f << "box=" << box << ",";
    }
    N=unsigned(input->readULong(4));
    f << "N=" << N << ",";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(input->tell()+44);
    ascFile.addNote("ObFl[unkn]:");
  }
  if (long(N)<0 || (len-headerLength)/long(dataSize)<long(N) || headerLength+long(N)*long(dataSize)<headerLength || len<headerLength+long(N*dataSize)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: can not read N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  if (type>=1 && type<=5) {
    char const *wh[]= {"radial", "directional", "shape", "rectangular",
                       "elliptical"
                      };
    MWAWGraphicStyle::Gradient::Type const types[]= {
      MWAWGraphicStyle::Gradient::G_Radial, MWAWGraphicStyle::Gradient::G_Linear,
      MWAWGraphicStyle::Gradient::G_Rectangular, MWAWGraphicStyle::Gradient::G_Rectangular,
      MWAWGraphicStyle::Gradient::G_Ellipsoid
    };
    gradient.m_type=types[type-1];
    f << wh[type-1] << ",";
  }
  else {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readGradient: find unknown type\n"));
    f << "###type=" << type << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->seek(pos+headerLength, librevenge::RVNG_SEEK_SET);
  gradient.m_stopList.resize(size_t(N));
  for (size_t i=0; i<size_t(N); ++i) {
    long actPos=input->tell();
    f.str("");
    f << "ObFl[stop" << i << "]:";
    MWAWGraphicStyle::Gradient::Stop &stop=gradient.m_stopList[size_t(N)-1-i];
    stop.m_offset=1-float(input->readLong(4))/100;
    f << "pos=" << stop.m_offset << ",";
    std::string extra;
    MWAWVariable<MWAWColor> stopColor;
    if (!readColor(*stream, stopColor, extra))
      input->seek(actPos+4+24, librevenge::RVNG_SEEK_SET);
    else if (stopColor.isSet())
      stop.m_color=*stopColor;
    f << extra;
    if (vers>5) {
      std::string name;
      for (int j=0; j<32; ++j) {
        char c=char(input->readULong(1));
        if (!c)
          break;
        name+=c;
      }
      f << name << ",";
      input->seek(actPos+4+24+32, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(actPos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool Canvas5StyleManager::readHatch(std::shared_ptr<Canvas5Structure::Stream> stream, long len,
                                    MWAWGraphicStyle::Hatch &hatch, MWAWVariable<MWAWColor> &backColor)
{
  hatch=MWAWGraphicStyle::Hatch();
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  int const vers=version();
  int headerSz=vers<9 ? 8 : 12;
  int const dataSz=vers<9 ? 104 : 192;
  f << "Entries(Hatch):";
  if (len<headerSz+dataSz) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readHatch: unexpected length\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  unsigned sz=unsigned(input->readULong(4));
  f << "first[sz]=" << sz << ",";
  int N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (int(sz)<0 || headerSz+long(sz)>len || (len-long(sz)-headerSz)/dataSz<N ||
      headerSz+int(sz+unsigned(dataSz)*unsigned(N))<headerSz+dataSz ||
      len<headerSz+long(sz+unsigned(dataSz)*unsigned(N))) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readHatch: the number of line seems bad\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int val;
  if (vers<9) {
    val=int(input->readULong(2));
    if (val!=0xf6f6) f << "fl=" << std::hex << val << std::dec << ",";
  }
  else {
    for (int i=0; i<3; ++i) { // f2=0|1
      val=int(input->readLong(2));
      if (!val) continue;
      f << "f" << i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (sz) {
    pos=input->tell();
    f.str("");
    f << "Hatch[color,back]:";
    unsigned type=unsigned(input->readULong(4));
    if (sz>4) {
      auto bgColor=readColorStyle(stream, type, sz-4);
      if (!bgColor)
        f << "###";
      else {
        backColor=bgColor->m_color;
        f << *bgColor;
        ascFile.addPos(pos+4);
        ascFile.addNote(f.str().c_str());
      }
    }
    else if (type!=0)
      f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+sz, librevenge::RVNG_SEEK_SET);
  }
  if (N<=0)
    return true;

  hatch.m_type= N==1 ? MWAWGraphicStyle::Hatch::H_Single :
                N==2 ? MWAWGraphicStyle::Hatch::H_Double : MWAWGraphicStyle::Hatch::H_Triple;
  float offset=0;
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Hatch-L" << i << ":";
    double w=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (w<0 || w>0)
      f << "w=" << w << ",";
    double angle=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (angle<0 || angle>0)
      f << "angle=" << angle << ",";
    if (i==0)
      hatch.m_rotation=90-float(angle);
    double offs=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    offset+=float(offs);
    f << "offset=" << offs << ",";
    double orig=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (orig<0 || orig>0)
      f << "orig=" << orig << ",";
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+dataSz-24, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << "Hatch-C" << i << ":";
    std::string extra;
    MWAWVariable<MWAWColor> col;
    if (!readColor(*stream, col, extra))
      input->seek(pos+24, librevenge::RVNG_SEEK_SET);
    else if (col.isSet())
      hatch.m_color=*col;
    f << extra;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  hatch.m_distance=offset/float(N)/72;
  return true;
}

bool Canvas5StyleManager::readArrows(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readArrows: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Arrow):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (!m_mainParser->readUsed(*stream, "Arrow")) // checkme: probably a type
    return false;

  if (!m_mainParser->readIndexMap(stream, "Arrow",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    lF << "Arrow" << item.m_id << ",";
    MWAWGraphicStyle::Arrow arrow;
    if (!readArrow(lStream, arrow, 1, item.m_length))
      lF << "###";
    else
      m_state->m_idToArrow[item.m_id]=arrow;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;
  return true;
}

bool Canvas5StyleManager::readArrow(std::shared_ptr<Canvas5Structure::Stream> stream, MWAWGraphicStyle::Arrow &arrow, unsigned, long len)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readArrow: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  int const vers=version();
  int const headerLen=vers<9 ? 24 : 88;
  if (len<headerLen || !input->checkPosition(pos+headerLen)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readArrow: file is to short\n"));
    return false;
  }
  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;
  long dataLen=long(input->readULong(4));
  if (dataLen==len) dataLen=len-headerLen; // can happen sometimes
  if (pos+headerLen+dataLen<pos+headerLen || headerLen+dataLen>len) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readArrow: can not read the arrow's data size\n"));
    return false;
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+headerLen, librevenge::RVNG_SEEK_SET);

  if (headerLen+dataLen!=len) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readArrow: find unknown length data\n"));
    ascFile.addPos(pos+headerLen+dataLen);
    ascFile.addNote("Arrow-End:###");
  }

  if (dataLen==0) {
    arrow=MWAWGraphicStyle::Arrow();
    return true;
  }
  std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
  if (!getImageParser()->readVKFL(stream, dataLen, image) || !image
      || !getImageParser()->getArrow(image, arrow))
    arrow=MWAWGraphicStyle::Arrow::plain();

  return true;
}

bool Canvas5StyleManager::readInks(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Color):type,";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::map<int, std::pair<unsigned, int> > idToTypeNameMap;
  if (!m_mainParser->readExtendedHeader(stream, 8, "Color",
  [&idToTypeNameMap](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    lF << "Co" << item.m_id << "[type]:";
    Canvas5StyleManagerInternal::ColorStyle color;
    unsigned type=unsigned(lInput->readULong(4)); // 1, TXUR: texture, ObFl, htch: hatch, vkfl: symbol
    if (type!=1)
      lF << "type=" << Canvas5Structure::getString(type) << ",";
    int nameId=int(lInput->readLong(4));
    if (nameId)
      lF << "id[name]=" << nameId << ",";
    idToTypeNameMap[item.m_id]=std::make_pair(type,nameId);
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!m_mainParser->readIndexMap(stream, "Color",
  [this, &idToTypeNameMap](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    lF << "Co" << item.m_id << ",";
    unsigned type=1;
    int nameId=0;
    auto const &it=idToTypeNameMap.find(item.m_id);
    if (it!=idToTypeNameMap.end())
      std::tie(type,nameId)=it->second;
    auto color=readColorStyle(lStream, type, item.m_length);
    if (color) {
      color->m_nameId=nameId;
      m_state->m_idToColor[item.m_id]=color;
    }
    else
      lF << "###";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!m_mainParser->readUsed(*stream, "Color"))
    return false;

  std::multimap<int,int> nameIdToColor;
  for (auto &it : m_state->m_idToColor) {
    if (it.second && it.second->m_nameId)
      nameIdToColor.insert(std::make_pair(it.second->m_nameId, it.first));
  }

  pos=input->tell();
  f.str("");
  f << "Color:names";
  int N;
  if (!m_mainParser->readDataHeader(*stream, 4, N)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readInks: can not read the last zone N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  f << "id=[";
  for (int i=0; i<N; ++i) f << input->readLong(4) << ",";
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return m_mainParser->readIndexMap(stream, "Color",
  [&nameIdToColor](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
    auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    std::string name;
    for (int i=0; i<int(item.m_length); ++i) {
      char c=char(lInput->readULong(1));
      if (c==0)
        break;
      name+=c;
    }
    lF << name << ",";
    auto it = nameIdToColor.find(item.m_id);
    lF << "[";
    while (it!=nameIdToColor.end() && it->first==item.m_id) {
      lF << "Co" << it->second << ",";
      ++it;
    }
    lF << "],";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  });
}


bool Canvas5StyleManager::readInks9(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  auto &ascFile=stream->ascii();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Color)[list]:";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!m_mainParser->readArray9(stream, "Color",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    if (item.m_length<8) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readInks9: can not find the color type\n"));
      lF << "###";
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote(lF.str().c_str());
      return;
    }
    lF << "Co" << item.m_id << ",";
    int lVal=int(lInput->readLong(4));
    if (lVal!=item.m_id)
      lF << "#g0=" << lVal << ",";
    long len2=long(lInput->readULong(4));
    auto color=readColorStyle(lStream, item.m_type, std::min(item.m_length-8,len2));
    if (color)
      m_state->m_idToColor[item.m_id]=color;
    else
      lF << "###";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;
  if (!m_mainParser->readArray9(stream, "Color[name]", &Canvas5Parser::stringDataFunction))
    return false;
  pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readInks9: can not find the array block\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Color-End###");
    return false;
  }
  pos=input->tell();
  f.str("");
  f << "Color-End:";
  int val=int(input->readLong(4));
  if (val!=-1) // checkme: maybe another length
    f << "f0=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

std::shared_ptr<Canvas5StyleManagerInternal::ColorStyle> Canvas5StyleManager::readColorStyle(std::shared_ptr<Canvas5Structure::Stream> stream, unsigned type, long len)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readColorStyle: can not find the input\n"));
    return nullptr;
  }
  auto input=stream->input();
  long pos=input->tell();
  if (len<0 || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readColorStyle: the zone seems too short\n"));
    return nullptr;
  }
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  if (version()>=9)
    f << "Color:";
  auto color=std::make_shared<Canvas5StyleManagerInternal::ColorStyle>();
  color->m_type=type;
  switch (type) {
  case 0:
    if (len==24) { // gray?
      std::string extra;
      if (!readColor(*stream, color->m_color, extra)) {
        color->m_color.setSet(false);
        f << "##";
      }
      f << extra << ",";
      break;
    }
    if (len!=4) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readColorStyle: unexpected length\n"));
      f << "###";
      break;
    }
    color->m_color=MWAWColor(0,0,0,0);
    for (int i=0; i<2; ++i) {
      int val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    break;
  case 1: {
    if (len<24) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readColorStyle: unexpected length\n"));
      f << "###";
      break;
    }
    std::string extra;
    if (!readColor(*stream, color->m_color, extra)) {
      color->m_color.setSet(false);
      f << "##";
    }
    f << extra << ",";
    if (len==24 || version()<=5)
      break;
    // normally only in v6
    std::string name;
    for (int i=0; i<len-24; ++i) {
      char c=char(input->readULong(1));
      if (!c)
        break;
      name+=c;
    }
    f << name << ",";
    break;
  }
  case 0x68746368: { // htch
    color->m_color=MWAWColor(0,0,0,0);
    color->m_color.setSet(false);
    if (!readHatch(stream, len, color->m_hatch, color->m_color))
      f << "###";
    break;
  }
  case 0x4f62466c: // ObFl
    color->m_color.setSet(false);
    f << "ObFl,";
    if (!readGradient(stream, len, color->m_gradient))
      f << "###";
    break;
  case 0x50415453: // PATS: v9
  case 0x54585552: { // TXUR
    MWAWVariable<MWAWColor> bgColor;
    auto image=readSymbol(stream, len, bgColor);
    MWAWColor avgColor;
    color->m_color.setSet(false);
    if (!image || !getImageParser()->getTexture(image, color->m_texture, color->m_textureDim, avgColor))
      f << "###";
    else
      color->m_color=avgColor;
    break;
  }
  case 0x766b666c: { // vkfl
    color->m_color=MWAWColor(0,0,0,0);
    color->m_color.setSet(false);
    if (!readSymbol(stream, len, color->m_color))
      f << "###";
    break;
  }
  default: {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readColorStyle: can not read some complex ink color\n"));
      first=false;
    }
    f << "type=" << Canvas5Structure::getString(color->m_type) << "##";
    color->m_color.setSet(false);
    break;
  }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return color;
}

std::shared_ptr<Canvas5ImageInternal::VKFLImage> Canvas5StyleManager::readSymbol
(std::shared_ptr<Canvas5Structure::Stream> stream, long len, MWAWVariable<MWAWColor> &backColor)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readSymbol: bad input\n"));
    return nullptr;
  }
  auto input=stream->input();
  long pos=input->tell();
  int const vers=version();
  int const headerLen=vers<9 ? 36 : 56;
  if (len<headerLen || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readSymbol: the zone seems too short\n"));
    return nullptr;
  }
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Symbol):";
  for (int i=0; i<5; ++i) {
    double value=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (value<=0 && value>=0)
      continue;
    char const *wh[]= { "deplX", "deplY", "stagger", "rotation", "scale" };
    f << wh[i] << "=" << value << ",";
  }
  long sz=input->readLong(4);
  f << "sz=" << sz << ",";
  long endSize=long(input->readULong(4));
  if (endSize)
    f << "sz[end]=" << endSize << ",";
  long endPos=pos+headerLen+sz;
  if (sz<0 || endSize<0 || headerLen+sz+endSize<0 || headerLen+sz+endSize>len) {
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readSymbol: can not read the symbox sz\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return nullptr;
  }
  int val;
  for (int i=0; i<2; ++i) { // g0=0|2e2e, g1=0|c3|e6e6
    val=int(input->readLong(2));
    if (val)
      f << "g" << i << "=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readLong(1));
  if (val!=1) f << "type?=" << val << ",";
  input->seek(3, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::shared_ptr<Canvas5ImageInternal::VKFLImage> image;
  if (sz>0)
    getImageParser()->readVKFL(stream, sz, image);

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  if (!endSize)
    return image;

  f.str("");
  f << "Symbol-End:";
  if (endSize>4) {
    unsigned type=unsigned(input->readULong(4));
    f << Canvas5Structure::getString(type) << ",";
    auto endColor=readColorStyle(stream, type, endSize-4);
    if (!endColor)
      f << "###";
    else {
      backColor=endColor->m_color;
      ascFile.addPos(endPos+4);
      ascFile.addNote(f.str().c_str());
    }
  }
  ascFile.addPos(endPos);
  ascFile.addNote(f.str().c_str());

  return image;
}

bool Canvas5StyleManager::readDashes(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Dash):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!m_mainParser->readUsed(*stream, "Dash")) // checkme: probably a type list
    return false;

  return m_mainParser->readExtendedHeader(stream, 64, "Dash",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
    auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Da" << item.m_id << ",";
    std::vector<float> dashes;
    if (!readDash(*lStream, dashes, 1, item.m_length))
      lF << "###";
    else
      m_state->m_idToDash[item.m_id]=dashes;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  });
}

bool Canvas5StyleManager::readDash(Canvas5Structure::Stream &stream, std::vector<float> &dashes, unsigned /*type*/, long len)
{
  dashes.clear();
  MWAWInputStreamPtr input = stream.input();
  long pos=input->tell();
  int const vers=version();
  int expectedSize=vers<9 ? 64 : 136;
  if (len<expectedSize || !input->checkPosition(pos+expectedSize)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readDash: the zone seems too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream.ascii();
  libmwaw::DebugStream f;
  f << "Entries(Dash):";
  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int N;
  bool inverted=input->readInverted();
  if (vers<9) {
    N=int(input->readLong(2));
    val=int(input->readLong(2));
    if (val!=1) f << "f2=" << val << ",";
  }
  else {
    input->seek(pos+124, librevenge::RVNG_SEEK_SET);
    N=int(input->readLong(2)); // checkme: maybe 4
    for (int i=0; i<5; ++i) {
      val=int(input->readLong(2));
      if (val!=(i==0 ? 1 : 0)) f << "f" << i+1 << "=" << val << ",";
    }
    input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  }
  f << "N=" << N << ",";
  if (N>14) {
    if (N>0 && (N%512)==0 && (N>>8)<14) { // look for potential inversion and N even
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readDash: endian seems inverted\n"));
      input->setReadInverted(!inverted);
      N=(N>>8);
      f << "#N=" << N << ",";
    }
    else {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readDash: N seems bad\n"));
      f << "###";
      N=0;
    }
  }
  f << "dash=[";
  for (int i=0; i<N; ++i) {
    double value=m_mainParser->readDouble(stream, vers<9 ? 4 : 8);
    dashes.push_back(float(value));
    f << value << ",";
  }
  f << "],";
  input->setReadInverted(inverted);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5StyleManager::readFonts(std::shared_ptr<Canvas5Structure::Stream> stream, int numFonts)
{
  if (!stream || !stream->input())
    return false;
  MWAWInputStreamPtr input = stream->input();
  long pos=input->tell();
  if (numFonts<=0 || !input->checkPosition(pos+136*numFonts)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readFonts: zone seems too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Font):N=" << numFonts << ",";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());

  auto fontConverter=m_parserState->m_fontConverter;
  std::string const family=m_mainParser->isWindowsFile() ? "CP1252" : "";
  for (int fo=0; fo<numFonts; ++fo) {
    pos=input->tell();
    f.str("");
    f << "Font-F" << fo << ":";
    int id=int(input->readULong(2));
    f << "id=" << id << ",";
    for (int i=0; i<3; ++i) {
      int val=int(input->readLong(2));
      if (val!=(i==0 ? 4 : 0)) f << "f" << i << "=" << val << ",";
    }
    int dSz=int(input->readULong(1));
    if (dSz>=127) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readFonts: can not read a name\n"));
      f << "###name";
    }
    else {
      std::string name;
      for (int s=0; s<dSz; ++s) name+=char(input->readULong(1));
      if (!name.empty())
        fontConverter->setCorrespondance(fo+1, name, family);
      f << name << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+136, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool Canvas5StyleManager::readFormats(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readFormats: can not find the input\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+7*44+4)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readFormats: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(Format):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int fo=0; fo<7; ++fo) {
    pos=input->tell();
    f.str("");
    f << "Format-" << fo << ":";
    int val=int(input->readLong(2));
    if (val) // small integer
      f << "f0=" << val << ",";
    val=int(input->readLong(2)); // 1|a|c|10
    f << "f1=" << val << ",";
    for (int i=0; i<2; ++i) { // f2=0|1, f3=0|3|10
      val=int(input->readLong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<4; ++i) {
      val=int(input->readULong(4));
      if (val!=0x10000)
        f << "dim" << i << "=" << double(val)/double(0x10000) << ",";
    }
    int len=int(input->readULong(1));
    if (len<=19) {
      std::string text;
      for (int i=0; i<len; ++i) text+=char(input->readULong(1));
      f << "name=" << text << ",";
    }
    else {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readFormats: can not read the format name\n"));
      f << "###name,";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+44, librevenge::RVNG_SEEK_SET);
    if (fo!=0) continue;

    pos=input->tell();
    f.str("");
    f << "Format-unk:";
    for (int i=0; i<2; ++i) { // f0: small number
      val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool Canvas5StyleManager::readPenSize(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenSize: can not find the input\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  if (!input->checkPosition(pos+20)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenSize: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "Entries(PenSize):sz=[";
  for (int i=0; i<10; ++i)
    f << double(input->readULong(2))/256. << ",";
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5StyleManager::readPenStyles(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();

  libmwaw::DebugStream f;
  libmwaw::DebugFile &ascFile = stream->ascii();
  f << "Entries(PenStyl):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::map<int, unsigned> idToTypeMap;
  if (!m_mainParser->readExtendedHeader(stream, 4, "PenStyl",
  [&idToTypeMap](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Pe" << item.m_id << ",";
    unsigned type=unsigned(lInput->readULong(4));
    lF << "type=" << Canvas5Structure::getString(type) << ",";
    idToTypeMap[item.m_id]=type;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!m_mainParser->readIndexMap(stream, "PenStyl",
  [this, &idToTypeMap](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    auto &asciiFile=lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Pe" << item.m_id << ",";
    unsigned type=1;
    auto it=idToTypeMap.find(item.m_id);
    if (it!=idToTypeMap.end()) type=it->second;
    auto style=readPenStyle(*lStream, type, item.m_length);
    if (!style)
      lF << "###";
    else
      m_state->m_idToPen[item.m_id]=style;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;
  return m_mainParser->readUsed(*stream, "PenStyl");
}

std::shared_ptr<Canvas5StyleManagerInternal::PenStyle> Canvas5StyleManager::readPenStyle(Canvas5Structure::Stream &stream, unsigned type, long len)
{
  auto input=stream.input();
  long pos=input->tell();

  libmwaw::DebugStream f;
  libmwaw::DebugFile &ascFile = stream.ascii();
  f << "Entries(PenStyl):";

  auto style=std::make_shared<Canvas5StyleManagerInternal::PenStyle>();
  style->m_type=type;
  int const vers=version();
  switch (type) {
  // case 0: find one time with size=0x70 and inverted endian, so unsure
  case 1: {
    int expectedLength=vers<9 ? 32 : 0x70;
    if (len<expectedLength) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: find unexpected size for type 1\n"));
      return nullptr;
    }
    float widths[2];
    for (auto &w : widths) {
      if (vers<9) {
        w=float(input->readLong(4))/65536.f;
        continue;
      }
      double dVal;
      bool isNan;
      if (m_mainParser->readDouble(stream, dVal, isNan))
        w=float(dVal);
      else {
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: can not read a width\n"));
        w=0;
        f << "###bad value,";
      }
    }
    style->m_size=MWAWVec2f(widths[0],widths[1]);
    ascFile.addDelimiter(input->tell(),'|');
    break;
  }
  case 0x706c696e: { // plin
    int headerLen=vers==5 ? 16 : vers<9 ? 60 : 64;
    int dataLen=vers==5 ? 128 : vers<9 ? 164 : 328;
    if (len<headerLen) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: find unexpected size for plin\n"));
      return nullptr;
    }
    if (vers>5) {
      // checkme: look like ObFl structure
      for (int i=0; i<2; ++i) {
        int lVal=int(input->readLong(4));
        int const expected[]= {vers<9 ? 0xfa : 0xdf, vers<9 ? 0x600 : 0x700};
        if (lVal!=expected[i])
          f << "f" << i << "=" << lVal << ",";
      }
    }
    int N=m_mainParser->readInteger(stream, vers==5 ? 4 : vers<9 ? 2 : 8);

    f << "plin,N=" << N << ",";
    if (N<0 || (len-headerLen)/dataLen<N) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: find unexpected value of N for plin\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return nullptr;
    }
    style->m_usePenColor=false;
    for (int i=0; i<2; ++i) {
      int val=int(input->readLong(1));
      if (!val) continue;
      char const *wh[]= {"equidistant", "usePenLine" };
      if (val==1) {
        f << wh[i] << ",";
        if (i==1)
          style->m_usePenColor=true;
      }
      else
        f << wh[i] << "=" << val << ",";
    }
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+headerLen, librevenge::RVNG_SEEK_SET);
    libmwaw::DebugStream f2;
    std::vector<float> offsets;
    float sumOffsets=0;
    for (int i=0; i<N; ++i) {
      long actPos=input->tell();
      f2.str("");
      f2 << "PenStyl,Pe[plin" << i << "]:";
      Canvas5StyleManagerInternal::PenStyle::Line line;
      std::string extra;
      if (!readColor(stream, line.m_color, extra)) {
        f2 << "##";
        input->seek(actPos+24, librevenge::RVNG_SEEK_SET);
      }
      f2 << extra;
      if (vers>5) {
        std::string name;
        for (int j=0; j<32; ++j) {
          char c=char(input->readULong(1));
          if (!c)
            break;
          name+=c;
        }
        f2 << name << ",";
        input->seek(actPos+24+32, librevenge::RVNG_SEEK_SET);
      }
      float width[2];
      for (auto &w : width) w=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
      line.m_size=MWAWVec2f(width[0],width[1]);
      f2 << "w=" << line.m_size << ",";
      for (int j=0; j<46; ++j) { // 0
        int val=int(input->readLong(2));
        if (val) f2 << "g" << j << "=" << val << ",";
      }
      if (vers==5) {
        offsets.push_back(float(input->readULong(4))/65536);
        sumOffsets+=offsets.back();
        f2 << "decal=" << offsets.back() << ",";
      }
      else {
        input->seek(actPos+dataLen-(vers<9 ? 8 : 16), librevenge::RVNG_SEEK_SET);
        offsets.push_back(float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8)));
        sumOffsets+=offsets.back();
        f2 << "decal=" << offsets.back() << ",";
        for (int j=0; j<(vers<9 ? 1 : 2); ++j) {
          int val=int(input->readLong(4));
          if (val) f2 << "h" << j << "=" << val << ",";
        }
      }
      style->m_lines.push_back(line);
      ascFile.addPos(actPos);
      ascFile.addNote(f2.str().c_str());
    }
    float actualOffset=sumOffsets/2;
    for (size_t i=0; i<std::min(style->m_lines.size(),offsets.size()); ++i) {
      style->m_lines[i].m_offset=actualOffset;
      actualOffset-=offsets[i];
    }
    ascFile.addDelimiter(input->tell(),'|');
    break;
  }
  case 0x766e656f: { // vneo
    int headerLen=vers==5 ? 68 : vers<9 ? 184 : 236;
    if (len<headerLen) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: the vneo zone seems too short\n"));
      return nullptr;
    }
    if (vers!=5) { // checkme: look like ObFl structure
      for (int i=0; i<3; ++i) {
        int lVal=int(input->readLong(4));
        int const expected[]= {vers<9 ? 0xdf : 0xadf, vers<9 ? 0x600 : 0xa00, 0};
        if (lVal!=expected[i])
          f << "f" << i << "=" << lVal << ",";
      }
      if (vers>=9) {
        int lVal=int(input->readULong(4));
        if (lVal) f << "type=" << Canvas5Structure::getString(unsigned(lVal)) << ",";
      }
    }
    float w=float(m_mainParser->readDouble(stream, vers<9 ? 4 : 8));
    f << "w=" << w << ",";
    style->m_size=MWAWVec2f(w,w);
    if (vers>=9) {
      ascFile.addDelimiter(input->tell(), '|');
      input->seek(16, librevenge::RVNG_SEEK_CUR); // 01040004000000000000000100000000
      ascFile.addDelimiter(input->tell(), '|');
      f << "values=[";
      for (int i=0; i<3; ++i) // unkn, width, angle
        f << m_mainParser->readDouble(stream, 8) << ",";
      f << "],";
      ascFile.addDelimiter(input->tell(), '|');
      input->seek(52, librevenge::RVNG_SEEK_CUR);
      ascFile.addDelimiter(input->tell(), '|');
    }
    for (int i=0; i<2; ++i) {
      std::string extra;
      long actPos=input->tell();
      if (!readColor(stream, style->m_colors[i], extra)) {
        f << "##";
        input->seek(actPos+24, librevenge::RVNG_SEEK_SET);
      }
      f << "col" << i << "=[" << extra << "],";
      if (vers>5) {
        std::string name;
        for (int j=0; j<32; ++j) {
          char c=char(input->readULong(1));
          if (!c)
            break;
          name+=c;
        }
        f << name << ",";
        input->seek(actPos+24+32, librevenge::RVNG_SEEK_SET);
      }
    }
    if (vers>=9)
      break;
    int val=int(input->readLong(2));
    if (val)
      f << "f0=" << val << ",";
    val=int(input->readULong(2));
    if (val&0x100)
      f << "axial,";
    val &= 0xfeff;
    if (val!=0xdd)
      f << "fl=" << std::hex << val << std::dec << ",";
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(4));
      if (!val) continue;
      if (i==0)
        f << "corner=" << val << ","; // 0: none, 1: round, 2: square
      else
        f << "join=" << val << ","; // 0: mitter, 1: round, 2:bevel
    }
    f << "angle=" << float(input->readLong(4))/65536 << "rad,";
    if (vers!=5)
      ascFile.addDelimiter(input->tell(),'|');
    break;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readPenStyle: find unexpected type=%s\n", Canvas5Structure::getString(type).c_str()));
    return nullptr;
  }
  f << *style;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return style;
}

bool Canvas5StyleManager::readStrokes(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();

  libmwaw::DebugStream f;
  libmwaw::DebugFile &ascFile = stream->ascii();
  f << "Entries(Stroke):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (!m_mainParser->readUsed(*stream, "Stroke"))
    return false;
  if (!m_mainParser->readExtendedHeader(stream, 20, "Stroke",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "St" << item.m_id << ",";
    Canvas5StyleManagerInternal::Stroke style;
    style.m_type=unsigned(lInput->readULong(4));
    style.m_penId=int(lInput->readLong(4));
    style.m_dashId=int(lInput->readLong(4));
    for (int i=0; i<2; ++i)
      style.m_arrowId[i]=int(lInput->readLong(4));
    lF << style;
    m_state->m_idToStroke[item.m_id]=style;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  return true;
}

////////////////////////////////////////////////////////////
// styles
////////////////////////////////////////////////////////////
bool Canvas5StyleManager::readCharStyle(Canvas5Structure::Stream &stream, int id, CharStyle &font,
                                        bool useFileColors)
{
  auto input=stream.input();
  libmwaw::DebugFile &ascFile = stream.ascii();
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  if (id<0)
    f << "Entries(CharStyl):";
  else
    f << "CharStyl-" << id << ":";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  font.m_paragraphId=font.m_linkId=0;
  int const sz=vers<9 ? 60 : 96;
  if (!input->checkPosition(pos+sz)) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readCharStyle: the zone is too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f.str("");
  int val=int(input->readLong(2));
  if (val!=1) f << "used=" << val << ",";
  f << "h=[";
  for (int i=0; i<2; ++i)
    f << input->readLong(2) << ",";
  f << "],";
  int fId=int(input->readULong(2));
  font.m_font.setId(fId);
  val=int(input->readULong(1));
  uint32_t flags = 0;
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  if (val&0x80) font.m_font.setStrikeOutStyle(MWAWFont::Line::Simple);
  if (val&0x60) f << "fl=" << std::hex << (val&0x60) << std::dec << ",";
  val=int(input->readULong(1));
  if (val) f << "fl1=" << std::hex << val << std::dec;
  if (vers<9)
    font.m_font.setSize(float(input->readULong(2)));
  else {
    for (int i=0; i<3; ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    font.m_font.setSize(float(m_mainParser->readDouble(stream, 8)));
  }
  val=int(input->readLong(4));
  if (val) {
    // CHECKME: inside a Vkfl image, colorId and linkId are a negative number,
    //          I do not know how to retrieve the color/link using this number
    if (!useFileColors)
      f << "##COLOR=" << val << ",";
    else {
      auto it=m_state->m_idToColor.find(val);
      if (it!=m_state->m_idToColor.end() && it->second && it->second->m_color.isSet())
        font.m_font.setColor(*it->second->m_color);
      else
        f << "#Co1" << val << ",";
    }
  }
  for (int i=0; i<2; ++i) { //
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==0) {
      f << "E" << val << ",";
      font.m_paragraphId=val;
    }
    else
      f << "f" << i << "=" << val << ",";
  }
  float stretchs[]= {1,1};
  if (vers<9) {
    val=int(input->readLong(4));
    if ((val>=-60*65536 && val<0) || (val>0 && val<60*65536)) // is 30 pt is big enough ?
      font.m_font.setDeltaLetterSpacing(float(val)/2/65536, librevenge::RVNG_POINT);
    else if (val) {
      MWAW_DEBUG_MSG(("Canvas5Style::readCharStyle: unknown delta spacing\n"));
      f << "##delta[spacing]=" << val/65536 << ",";
    }

    int lVals[4];
    for (auto &l : lVals) l=int(input->readLong(2));
    for (int i=0; i<2; ++i) {
      if (lVals[i]==lVals[i+2])
        continue;
      f << "scaling[" << (i==0 ? "hori" : "verti") << "]=" << lVals[i] << "/" << lVals[i+2] << ",";
      if (lVals[i]<=0 || lVals[i+2]<=0) {
        MWAW_DEBUG_MSG(("Canvas5Style::readCharStyle: invalid scaling\n"));
        f << "###";
      }
      else
        stretchs[i]=float(lVals[i])/float(lVals[i+2]);
    }
    val=int(input->readLong(4));
    if (val)
      font.m_font.set(MWAWFont::Script(float(val)/65536,librevenge::RVNG_POINT));
  }
  else {
    for (int i=0; i<4; ++i) {
      double dVal=m_mainParser->readDouble(stream, 8);
      double const expected=i==0 || i==3 ? 0 : 1;
      if (dVal<=expected && dVal>=expected)
        continue;
      if (i==0)
        font.m_font.setDeltaLetterSpacing(float(dVal), librevenge::RVNG_POINT);
      else if (i==3)
        font.m_font.set(MWAWFont::Script(float(dVal), librevenge::RVNG_POINT));
      else {
        stretchs[i-1]=float(dVal);
        f << "scaling[" << (i==1 ? "hori" : "verti") << "]=" << dVal << ",";
      }
    }
  }
  if (stretchs[1]>1-1e-4f && stretchs[1]<1+1e-4f) {
    if (stretchs[0]<1-1e-4f || stretchs[0]>1+1e-4f)
      font.m_font.setWidthStreching(stretchs[0]);
  }
  else {
    font.m_font.setSize(font.m_font.size()*stretchs[1]);
    font.m_font.setWidthStreching(stretchs[0]/stretchs[1]);
  }
  val=int(input->readLong(2));
  if (val) f << "h0=" << std::hex << val << std::dec << ","; // 4 or 8, 4 a link?
  val=int(input->readULong(2));
  if (val&1)
    flags |= MWAWFont::smallCapsBit;
  if (val&2)
    flags |= MWAWFont::uppercaseBit;
  if (val&4)
    flags |= MWAWFont::lowercaseBit;
  if (val&8)
    flags |= MWAWFont::initialcaseBit;
  if (val&0x200)
    f << "spread,";
  if (val&0x800)
    f << "overprint,";
  val &= 0xF5F0;
  if (val) {
    MWAW_DEBUG_MSG(("Canvas5Style::readCharStyle: unknown small caps bits\n"));
    f << "##smallCaps=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<4; ++i) { // h2=0|1 a link id?
    val=int(input->readLong(4));
    if (!val) continue;
    if (i==0) {
      if (!useFileColors)
        f << "###LINK=" << val << ",";
      else {
        font.m_linkId=val;
        f << "link[id]=Tl" << val << ",";
      }
    }
    else if (i==3 && font.m_paragraphId==0) { // checkme: there is two color: the para color and the font color
      if (!useFileColors)
        f << "###COLOR=" << val << ",";
      else {
        auto it=m_state->m_idToColor.find(val);
        if (it!=m_state->m_idToColor.end() && it->second && it->second->m_color.isSet())
          font.m_font.setColor(*it->second->m_color);
        else
          f << "#Co2" << val << ",";
      }
    }
    else
      f << "h" << i+1 << "=" << val << ",";
  }
  for (int i=0; i<(vers<9 ? 2 : 6); ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    f << "h" << i+5 << "=" << val << ",";
  }

  font.m_font.setFlags(flags);
  std::string const extra=f.str();
  f.str("");
  f << font.m_font.getDebugString(m_parserState->m_fontConverter) << "," << extra;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5StyleManager::readCharStyles(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  libmwaw::DebugFile &ascFile = stream->ascii();
  int const vers=version();
  ascFile.addPos(input->tell());
  ascFile.addNote("Entries(CharStyl):");
  if (vers>=9) {
    return m_mainParser->readArray9(stream, "CharStyl",
    [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
      auto lInput=lStream->input();
      long endPos=lInput->tell()+item.m_length;
      lInput->seek(-4, librevenge::RVNG_SEEK_CUR);
      CharStyle font;
      if (!readCharStyle(*lStream, item.m_id, font))
        return;

      long pos=lInput->tell();
      libmwaw::DebugStream f;
      libmwaw::DebugFile &asciiFile = lStream->ascii();
      f << what << "-" << item.m_id << "[A]:";
      if (pos+44>endPos) {
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readCharStyles: the zone seems too short\n"));
        f << "###";
        asciiFile.addPos(pos);
        asciiFile.addNote(f.str().c_str());
        return;
      }
      for (int i=0; i<4; ++i) {
        int val=int(lInput->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());

      pos=lInput->tell();
      f.str("");
      f << what << "-" << item.m_id << "[name]:";
      std::string name;
      for (int i=0; i<32; ++i) {
        char c=char(lInput->readLong(1));
        if (c==0) break;
        name+=c;
      }
      f << name << ",";
      lInput->seek(pos+32, librevenge::RVNG_SEEK_SET);
      int val=int(lInput->readLong(4)); // unsure can be an extended size
      if (val)
        f << "f0=" << val << ",";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
    });
  }
  if (!m_mainParser->readExtendedHeader(stream, 0x64, "CharStyl",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
  CharStyle font;
  if (!readCharStyle(*lStream, item.m_id, font))
      return;
    auto lInput=lStream->input();
    long pos=lInput->tell();
    libmwaw::DebugStream f;
    libmwaw::DebugFile &asciiFile = lStream->ascii();

    f << what << "-" << item.m_id << "[A]:";
    for (int i=0; i<4; ++i) {
      int val=int(lInput->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    pos=lInput->tell();
    f.str("");
    f << what << "-" << item.m_id << "[name]:";
    std::string name;
    for (int i=0; i<32; ++i) {
      char c=char(lInput->readLong(1));
      if (c==0) break;
      name+=c;
    }
    f << name << ",";
    lInput->seek(pos+32, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }))
  return false;
  if (!m_mainParser->readIndexMap(stream, "CharStyl"))
    return false;
  std::vector<bool> defined;
  if (!m_mainParser->readDefined(*stream, defined, "CharStyl"))
    return false;
  if (!m_mainParser->readUsed(*stream, "CharStyl"))
    return false;
  return m_mainParser->readExtendedHeader(stream, 8, "CharStyl[data2]", &Canvas5Parser::defDataFunction);
}

bool Canvas5StyleManager::readParaStyle(std::shared_ptr<Canvas5Structure::Stream> stream, int id, Canvas5StyleManager::StyleList *styles)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  libmwaw::DebugFile &ascFile = stream->ascii();
  int const vers=version();

  long pos=input->tell();
  libmwaw::DebugStream f;
  if (id<0)
    f << "Entries(ParaStyl):";

  if (!input->checkPosition(pos+(vers<9 ? 128 : 224))) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readParaStyle: the zone is too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::pair<MWAWParagraph,int> *paraId=nullptr;
  if (styles && id>=0) {
    if (styles->m_paragraphs.size()<=size_t(id))
      styles->m_paragraphs.resize(size_t(id+1));
    paraId=&styles->m_paragraphs[size_t(id)];
    paraId->second=0;
  }
  MWAWParagraph *para=paraId ? &paraId->first : nullptr;
  int val=int(input->readLong(4));
  if (val!=1)
    f << "used=" << val << ",";
  val=int(input->readLong(4));
  if (val) {
    if (paraId) paraId->second=val;
    f << "Tab" << val << ",";
  }
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==0) {
      switch (val) {
      case -1:
        if (para) para->m_justify=MWAWParagraph::JustificationRight;
        f << "align=right,";
        break;
      // 0: left
      case 1:
        if (para) para->m_justify=MWAWParagraph::JustificationCenter;
        f << "align=center,";
        break;
      // 2: ?
      case 4:
        if (para) para->m_justify=MWAWParagraph::JustificationFull;
        f << "align=justify,";
        break;
      default:
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readParaStyle: find unexpected align\n"));
        f << "##align=" << val << ",";
        break;
      }
    }
    else
      f << "f" << i << "=" << val << ",";
  }
  if (vers>=9) // align?
    input->seek(4, librevenge::RVNG_SEEK_CUR);
  double dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
  if (dVal>0) {
    f << "interline=" << dVal << "pt,";
    if (para)
      para->setInterline(dVal, librevenge::RVNG_POINT);
  }
  dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
  if (dVal>0 && (dVal<1 || dVal>1)) {
    f << "interline=" << dVal << ",";
    if (para)
      para->setInterline(dVal, librevenge::RVNG_PERCENT);
  }
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  if (para) para->m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<3; ++i) {
    dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (dVal<=0 && dVal>=0) continue;
    char const *wh[]= {"first", "left", "right"};
    f << wh[i] << "[marg]=" << dVal << ",";
    if (para)
      para->m_margins[i]=dVal;
  }
  if (para)
    para->m_margins[0]=*(para->m_margins[0])-*(para->m_margins[1]);
  for (int i=0; i<2; ++i) {
    dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (dVal<=0 && dVal>=0) continue;
    f << "space[" << (i==0 ? "before" : "after") << "]=" << dVal << ",";
    if (para)
      para->m_spacings[i+1]=dVal/72;
  }
  val=int(input->readULong(4));
  if (val) f << "g8=" << val << ",";
  dVal=m_mainParser->readDouble(*stream, 4);
  if (dVal<0 || dVal>0) f << "wrap[object]=" << dVal << ","; // useme: unit in point
  for (int i=0; i<(vers<9 ? 2 : 8); ++i) { // g9=0|2
    val=int(input->readULong(2));
    if (!val) continue;
    f << "g" << i+9 << "=" << val << ",";
  }
  if (vers>=9) {
    dVal=m_mainParser->readDouble(*stream, 8);
    if (dVal<0 || dVal>0)
      f << "unkn=" << dVal << ",";
  }
  int dropChar=int(input->readULong(2));
  int dropLine=int(input->readULong(2));
  if (dropChar>0 && dropLine>1) {
    if (para) {
      para->m_dropNumCharacters=dropChar;
      para->m_dropNumLines=dropLine;
    }
    f << "drop=" << dropChar << "[l=" << dropLine << "],";
  }
  if (vers>=9)
    input->seek(4, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  MWAWFont *font=nullptr;
  if (styles) {
    for (auto &fId : styles->m_fonts) {
      if (fId.m_paragraphId==id)
        font=&fId.m_font;
    }
  }

  return readStyleEnd(stream, font, para);
}

bool Canvas5StyleManager::readStyleEnd(std::shared_ptr<Canvas5Structure::Stream> stream, MWAWFont *font, MWAWParagraph *para)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile = stream->ascii();
  libmwaw::DebugStream f;
  f << "ParaStyl[A]:";

  int const vers=version();
  if (!input->checkPosition(pos+(vers<9 ? 64 : 104))) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::readStyleEnd: the zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  double dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
  if (dVal>0)
    f << "justify[last,width]=" << 100-dVal << "%,";
  for (int i=0; i<6; ++i) {
    dVal=m_mainParser->readDouble(*stream, vers<9 ? 4 : 8);
    if (dVal<=0 && dVal>=0) continue;
    char const *wh[]= {"spacing[word,min]", "spacing[word]", "spacing[word,max]", "spacing[min]", "spacing", "spacing[max]"};
    if (i==4 && font)
      font->setDeltaLetterSpacing(1+float(dVal), librevenge::RVNG_PERCENT);
    f << wh[i] << "=" << 100+100*dVal << "%,";
  }
  int val;
  f << "hyphen=[";
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {3,2,6,3}; // after word, before word, smallest word, consecutive limite
    if (val!=expected[i])
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  for (int i=0; i<4; ++i) { // g2=0|-1, g3=0|-1|0x140
    val=i==2 ? int(input->readULong(2)) : int(input->readLong(2));
    if (i==2) {
      int flags=0;
      if ((val&2)==0)
        f << "no[hyphen],";
      if ((val&4)==0)
        f << "skip[cap],";
      if ((val&0x200)==0)
        f << "orphan,";
      if ((val&0x400)==0) {
        flags|=MWAWParagraph::NoBreakBit;
        f << "keep[alllines],";
      }
      if ((val&0x800)==0) {
        flags|=MWAWParagraph::NoBreakWithNextBit;
        f << "keep[with,next],";
      }
      if (flags&&para)
        para->m_breakStatus=flags;
      val&=0xf1f9;
      if (val)
        f << "g2=" << std::hex << val << std::dec << ",";
      continue;
    }
    if (!val) continue;
    f << "g" << i << "=" << val << ",";
  }
  if (version()<9) {
    for (int i=0; i<10; ++i) { // 0
      val=int(input->readLong(2));
      if (!val)
        continue;
      if (i==2) { // checkme, something is bad here
        if (val!=100)
          f << "min[line,width]=" << 100-val << ",";
      }
      else if (i==3)
        f << "para[orphan]=" << val << ",";
      else if (i==4)
        f << "para[window]=" << val << ",";
      else
        f << "h" << i << "=" << val << ",";
    }
  }
  else {
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+104, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5StyleManager::readParaStyles(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  libmwaw::DebugFile &ascFile = stream->ascii();
  ascFile.addPos(input->tell());
  ascFile.addNote("Entries(ParaStyl):");

  if (version()>=9) {
    return m_mainParser->readArray9(stream, "ParaStyl",
    [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
      libmwaw::DebugStream f;
      auto &asciiFile=lStream->ascii();
      auto lInput=lStream->input();
      long endPos=lInput->tell()+item.m_length;
      lInput->seek(-4, librevenge::RVNG_SEEK_CUR);
      long pos=lInput->tell();
      f << what << "-" << item.m_id << ":";

      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      if (!readParaStyle(lStream, item.m_id))
        return;

      pos=lInput->tell();
      f.str("");
      f << what << "-" << item.m_id << "[B]:";
      if (pos+44>endPos) {
        MWAW_DEBUG_MSG(("Canvas5StyleManager::readParaStyles: the zone seems too short\n"));
        f << "###";
        asciiFile.addPos(pos);
        asciiFile.addNote(f.str().c_str());
        return;
      }
      for (int i=0; i<4; ++i) { // f1=0|1, f3=0|1
        int val=int(lInput->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());

      pos=lInput->tell();
      f.str("");
      f << what << "-" << item.m_id << "[name]:";
      std::string name;
      for (int i=0; i<32; ++i) {
        char c=char(lInput->readLong(1));
        if (c==0) break;
        name+=c;
      }
      f << name << ",";
      lInput->seek(pos+32, librevenge::RVNG_SEEK_SET);
      int val=int(lInput->readLong(4)); // unsure can be an extended size
      if (val)
        f << "f0=" << val << ",";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
    });
  }

  if (!m_mainParser->readUsed(*stream, "ParaStyl"))
    return false;

  if (!m_mainParser->readExtendedHeader(stream, 0x114, "ParaStyl", &Canvas5Parser::stringDataFunction)) // string:256 + 5xlong?
    return false;

  if (!m_mainParser->readExtendedHeader(stream, 0xa8, "ParaStyl",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
  if (!readParaStyle(lStream, item.m_id))
      return;
    auto lInput=lStream->input();
    long pos=lInput->tell();
    libmwaw::DebugStream f;
    auto &asciiFile=lStream->ascii();
    f << what << "-" << item.m_id << "[B]:";
    for (int i=0; i<4; ++i) { // f1=0|1, f3=0|1
      int val=int(lInput->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    pos=lInput->tell();
    f.str("");
    f << what << "-" << item.m_id << "[name]:";
    std::string name;
    for (int i=0; i<32; ++i) {
      char c=char(lInput->readLong(1));
      if (c==0) break;
      name+=c;
    }
    f << name << ",";
    lInput->seek(pos+32, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }))
  return false;
  if (!m_mainParser->readIndexMap(stream, "ParaStyl"))
    return false;
  std::vector<bool> defined;
  return m_mainParser->readDefined(*stream, defined, "ParaStyl");
}

bool Canvas5StyleManager::readFrameStyles9(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;
  auto input=stream->input();
  auto &ascFile=stream->ascii();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(FrameStyl):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!m_mainParser->readArray9(stream, "FrameStyl[stroke]",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "St" << item.m_id << ",";
    if (item.m_length!=20) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readFrameStyles9[stroke]: unexpected lengths\n"));
      lF << "###";
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote(lF.str().c_str());
      return;
    }
    Canvas5StyleManagerInternal::Stroke style;
    style.m_type=item.m_type;
    style.m_penId=int(lInput->readLong(4));
    style.m_dashId=int(lInput->readLong(4));
    for (int i=0; i<2; ++i)
      style.m_arrowId[i]=int(lInput->readLong(4));
    int val=int(lInput->readLong(4));
    if (val) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readFrameStyles9[stroke]: find extra value\n"));
      lF << "###f0=" << val << ",";
    }
    lF << style;
    m_state->m_idToStroke[item.m_id]=style;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!m_mainParser->readArray9(stream, "FrameStyl[pen]",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    auto &asciiFile=lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Pe" << item.m_id << ",";
    if (item.m_decal!=4 || item.m_length<4) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::readFrameStyles9[pen]: the zone seems bad\n"));
      lF << "###";
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote(lF.str().c_str());
      return;
    }
    lInput->seek(4, librevenge::RVNG_SEEK_CUR);
    auto style=readPenStyle(*lStream, item.m_type, item.m_length-4);
    if (!style)
      lF << "###";
    else
      m_state->m_idToPen[item.m_id]=style;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!m_mainParser->readArray9(stream, "FrameStyl[arrow]",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
  auto lInput=lStream->input();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();
    lF << "Arrow" << item.m_id << ",";
    MWAWGraphicStyle::Arrow arrow;
    if (!readArrow(lStream, arrow, 1, item.m_length))
      lF << "###";
    else
      m_state->m_idToArrow[item.m_id]=arrow;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  return m_mainParser->readArray9(stream, "FrameStyl[dash]",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
    auto lInput=lStream->input();
    libmwaw::DebugFile &asciiFile = lStream->ascii();
    libmwaw::DebugStream lF;
    lF << "Da" << item.m_id << ",";
    std::vector<float> dashes;
    if (!readDash(*lStream, dashes, 1, item.m_length))
      lF << "###";
    else
      m_state->m_idToDash[item.m_id]=dashes;
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  });
}

////////////////////////////////////////////////////////////
//
// Windows resource
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool Canvas5StyleManager::updateLineColor(int cId, MWAWGraphicStyle &style)
{
  auto it=m_state->m_idToColor.find(cId);
  if (it==m_state->m_idToColor.end() || !it->second) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineColor: can not find color %d\n", cId));
    return false;
  }
  return updateLineColor(*it->second, style);
}

bool Canvas5StyleManager::updateLineColor(Canvas5StyleManagerInternal::ColorStyle const &color, MWAWGraphicStyle &style)
{
  switch (color.m_type) {
  case 0: // checkme
    style.m_lineOpacity=0;
    break;
  case 1:
    if (!color.m_color.isSet()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineColor: can not find color\n"));
      break;
    }
    style.m_lineColor=*color.m_color;
    if (color.m_color->getAlpha()!=255)
      style.m_lineOpacity=float(color.m_color->getAlpha())/255;
    break;
  case 0x50415453: // pats
  case 0x54585552: // texture
    if (color.m_color.isSet())
      style.m_lineColor=*color.m_color;
    break;
  case 0x68746368: // hatch
  case 0x766b666c: // vkfl
    if (color.m_color.isSet())
      style.m_lineColor=*color.m_color;
    break;
  case 0x4f62466c: // ObFl
    if (color.m_gradient.hasGradient())
      color.m_gradient.getAverageColor(style.m_lineColor);
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineColor: can not send type=%s\n", Canvas5Structure::getString(color.m_type).c_str()));
    break;
  }
  return true;
}

bool Canvas5StyleManager::updateSurfaceColor(int cId, MWAWGraphicStyle &style)
{
  auto it=m_state->m_idToColor.find(cId);
  if (it==m_state->m_idToColor.end() || !it->second) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find color %d\n", cId));
    return false;
  }
  return updateSurfaceColor(*it->second, style);
}

bool Canvas5StyleManager::updateSurfaceColor(Canvas5StyleManagerInternal::ColorStyle const &color, MWAWGraphicStyle &style)
{
  switch (color.m_type) {
  case 0:
    style.m_surfaceOpacity=0;
    break;
  case 1:
    if (color.m_color.isSet())
      style.setSurfaceColor(*color.m_color, float(color.m_color->getAlpha())/255);
    else {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find the color\n"));
      return false;
    }
    break;
  case 0x50415453: // pats
  case 0x54585552: // txur
    if (color.m_texture.isEmpty()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find the texture\n"));
      return false;
    }
    style.setPattern(MWAWGraphicStyle::Pattern(color.m_textureDim, color.m_texture, *color.m_color));
    break;
  case 0x4f62466c:
    if (!color.m_gradient.hasGradient()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find the gradient\n"));
      return false;
    }
    style.m_gradient=color.m_gradient;
    break;
  case 0x68746368:
    if (!color.m_hatch.hasHatch()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find the hatch\n"));
      return false;
    }
    style.m_hatch=color.m_hatch;
    if (color.m_color.isSet())
      style.setSurfaceColor(*color.m_color, float(color.m_color->getAlpha())/255);
    break;
  case 0x766b666c: { // vkfl
    if (color.m_color.isSet()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not find the symbol color\n"));
      return false;
    }
    style.setSurfaceColor(*color.m_color, float(color.m_color->getAlpha())/255);
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: symbol color are replaced by background color\n"));
      first=false;
    }
    break;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateSurfaceColor: can not send type=%s\n", Canvas5Structure::getString(color.m_type).c_str()));
    break;
  }
  return true;
}

bool Canvas5StyleManager::updateLine(Canvas5StyleManagerInternal::PenStyle const &pen, MWAWGraphicStyle &style, int &numLines, int lineId, float *offset)
{
  numLines=1;
  if (offset) *offset=0;
  style.m_lineWidth=0;

  switch (pen.m_type) {
  case 1:
    style.m_lineWidth=(pen.m_size[0]+pen.m_size[1])/2;
    break;
  case 0x766e656f: { // vneo
    style.m_lineWidth=(pen.m_size[0]+pen.m_size[1])/2;
    // fixme: normally a gradient, let's replace it by it barycenters color...
    style.m_lineColor=MWAWColor::barycenter(0.5, *pen.m_colors[0], 0.5, *pen.m_colors[1]);
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLine: replace line gradient with their average color\n"));
      first=false;
    }
    break;
  }
  case 0x706c696e: // plin
    numLines=int(pen.m_lines.size());
    if ((lineId>=0 && lineId<numLines) || (numLines==1 && lineId<0)) {
      auto const &line = pen.m_lines[size_t(lineId)];
      style.m_lineWidth=(line.m_size[0]+line.m_size[1])/2;
      style.m_lineColor=*line.m_color;
      if (offset) *offset=line.m_offset;
    }
    else if (lineId>=0) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLine: can not find the line with: %d\n", lineId));
      return false;
    }
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineStyle: can not send pen with type %s\n", Canvas5Structure::getString(pen.m_type).c_str()));
    return false;
  }
  return true;
}

bool Canvas5StyleManager::updateLineStyle(int sId, MWAWGraphicStyle &style, int &numLines, int lineId, float *offset)
{
  numLines=1;
  if (offset) *offset=0;
  auto it=m_state->m_idToStroke.find(sId);
  if (it==m_state->m_idToStroke.end()) {
    MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineStyle: can not find stroke %d\n", sId));
    return false;
  }
  auto const &stroke=it->second;
  style.m_lineWidth=0;
  if (stroke.m_penId) {
    auto pIt=m_state->m_idToPen.find(stroke.m_penId);
    if (pIt==m_state->m_idToPen.end() || !pIt->second) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineStyle: can not find pen %d\n", stroke.m_penId));
    }
    else
      updateLine(*pIt->second, style, numLines, lineId, offset);
  }
  if (stroke.m_dashId) {
    auto dIt=m_state->m_idToDash.find(stroke.m_dashId);
    if (dIt==m_state->m_idToDash.end()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineStyle: can not find dash %d\n", stroke.m_dashId));
    }
    else
      style.m_lineDashWidth=dIt->second;
  }
  for (int i=0; i<2; ++i) {
    if (!stroke.m_arrowId[i])
      continue;
    auto dIt=m_state->m_idToArrow.find(stroke.m_arrowId[i]);
    if (dIt==m_state->m_idToArrow.end()) {
      MWAW_DEBUG_MSG(("Canvas5StyleManager::updateLineStyle: can not find arrow %d\n", stroke.m_arrowId[i]));
    }
    else
      style.m_arrows[i]=dIt->second;
  }
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
