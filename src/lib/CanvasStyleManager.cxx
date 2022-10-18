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

#include "CanvasParser.hxx"

#include "CanvasStyleManager.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a CanvasStyleManager */
namespace CanvasStyleManagerInternal
{

////////////////////////////////////////
//! Internal: the state of a CanvasStyleManager
struct State {
  //! constructor
  State()
    : m_input()

    , m_colors()
    , m_patterns()
  {
  }

  //! the main input
  MWAWInputStreamPtr m_input;

  //! the colors
  std::vector<MWAWColor> m_colors;
  //! the patterns
  std::vector<MWAWGraphicStyle::Pattern> m_patterns;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
CanvasStyleManager::CanvasStyleManager(CanvasParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new CanvasStyleManagerInternal::State)
  , m_mainParser(&parser)
{
}

CanvasStyleManager::~CanvasStyleManager()
{ }

int CanvasStyleManager::version() const
{
  return m_parserState->m_version;
}

void CanvasStyleManager::setInput(MWAWInputStreamPtr &input)
{
  m_state->m_input=input;
}

MWAWInputStreamPtr &CanvasStyleManager::getInput()
{
  return m_state->m_input;
}

bool CanvasStyleManager::get(int index, MWAWColor &color) const
{
  if (index<0 || index>=int(m_state->m_colors.size())) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::getColor: can find color with index=%d\n", index));
    return false;
  }
  color=m_state->m_colors[size_t(index)];
  return true;
}

bool CanvasStyleManager::get(int index, MWAWGraphicStyle::Pattern &pattern) const
{
  if (index<0 || index>=int(m_state->m_patterns.size())) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::getPattern: can find pattern with index=%d\n", index));
    return false;
  }
  pattern=m_state->m_patterns[size_t(index)];
  return true;
}

std::vector<MWAWColor> const &CanvasStyleManager::getColorsList() const
{
  return m_state->m_colors;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

bool CanvasStyleManager::readArrows()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+180)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readArrows: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Arrow):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int u=0; u<6; ++u) {
    pos=input->tell();
    f.str("");
    f << "Arrow-" << u << ":";
    for (int i=0; i<2; ++i) { // 0
      int val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    MWAWGraphicStyle::Arrow arrow;
    std::string extra;
    if (readArrow(arrow, extra))
      f << "arrow=[" << arrow << extra << "],";
    else
      f << "###";
    input->seek(pos+30, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool CanvasStyleManager::readArrow(MWAWGraphicStyle::Arrow &arrow, std::string &extra)
{
  arrow=MWAWGraphicStyle::Arrow::plain();
  extra="";
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+26)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readArrow: file is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  int type=int(input->readULong(2));
  if (type&0x100) f << "hasBar,";
  if (type&0x200) f << "use[surf,color],";
  // type&0x800 rare ?
  if (type&0x1000) f << "circle,";
  f << "type=" << (type&3) << ",";
  if (type&0xECFC)
    f << "fl=" << std::hex << (type&0xECFC) << std::dec << ",";
  int dim[2];
  for (auto &d: dim) d=int(input->readLong(2));
  f << "pt0=" << MWAWVec2i(dim[1], dim[0]) << ",";
  int val=int(input->readULong(2));
  if (val!=3) f << "f2=" << val << ",";
  val=int(input->readULong(4));
  if (val!=0x20000)
    f << "scale=" << double(val)/65536 << ",";
  for (int i=0; i<7; ++i) {
    int const expected[]= {90,1,0,2,0,3,12};
    val=int(input->readLong(2));
    if (val!=expected[i])
      f << "g" << i << "=" << val << ",";
  }
  switch (type&0xfdff) {
  case 1:
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,33)), "m10 0l-10 30 l10 3 l10 -3z", false);
    break;
  case 2:
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,30)), "m10 0l-10 30h20z", false);
    break;
  case 3:
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1580)),
                                  "M1013 1491l118 89-567-1580-564 1580 114-85 136-68 148-46 161-17 161 13 153 46z", false);
    break;
  case 0x100:
    arrow=MWAWGraphicStyle::Arrow(10, MWAWBox2i(MWAWVec2i(-100,0),MWAWVec2i(100, 30)),
                                  "M 0,0 L -100,0 -100,30 100,30 100,0 0,0 Z", false);
    break;
  case 0x101:
    arrow=MWAWGraphicStyle::Arrow(10, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(40,38)), "m20 0l-20 0 l0 4 l20 0 l-10 30 l10 3 l10 -3 l-10 -30 l20 0 l0 -4z", false);
    break;
  case 0x102:
    arrow=MWAWGraphicStyle::Arrow(10, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(40,35)), "m20 0l-20 0 l0 4 l20 0 l-10 30 l20 0 l-10 -30 l20 0 l0 -4z", false);
    break;
  case 0x1000:
    arrow=MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1131)), "M462 1118l-102-29-102-51-93-72-72-93-51-102-29-102-13-105 13-102 29-106 51-102 72-89 93-72 102-50 102-34 106-9 101 9 106 34 98 50 93 72 72 89 51 102 29 106 13 102-13 105-29 102-51 102-72 93-93 72-98 51-106 29-101 13z", false);
    break;
  default:
    MWAW_DEBUG_MSG(("CanvasStyleManager::readArrow: find unexpected type\n"));
    f << "###";
    break;
  }
  extra=f.str();
  return true;
}

bool CanvasStyleManager::readColors(int numColors)
{
  if (!m_mainParser->decode(6*numColors)) {
    MWAW_DEBUG_MSG(("CanvasParser::readColors: can not decode the input\n"));
    return false;
  }
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  if (!input || numColors<1 || !input->checkPosition(pos+numColors*6)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readColors: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Color):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  unsigned char col[3];
  for (int i=0; i<numColors; ++i) {
    pos=input->tell();
    f.str("");
    f << "Color-" << i << ":";
    for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
    m_state->m_colors.push_back(MWAWColor(col[0], col[1], col[2]));
    f << m_state->m_colors.back() << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool CanvasStyleManager::readDashes(int numDashes, bool user)
{
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  int const dataSize=user ? 60 : 58;
  if (!user) {
    if (!m_mainParser->decode(2+numDashes*dataSize)) {
      MWAW_DEBUG_MSG(("CanvasParser::readDashes: can not decode the input\n"));
      return false;
    }
  }
  if (!input || numDashes<1 || !input->checkPosition(pos+(user?0:2)+numDashes*dataSize)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readDashes: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Dash):";
  int val;
  if (!user) {
    val=int(input->readULong(2));
    if (val)
      f << "f0=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i=0; i<numDashes; ++i) {
    pos=input->tell();
    f.str("");
    f << "Dash-" << i << (user ? "U" : "") << ":";
    int N=int(input->readULong(2));
    if (N<=0 || N>12) {
      MWAW_DEBUG_MSG(("CanvasStyleManager::readDashes: the number of dashes seems bad\n"));
      f << "###N=" << N << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    f << "dashes=[";
    for (int p=0; p<N; ++p) f << float(input->readLong(4))/65536.f << ",";
    f << "],";
    input->seek(pos+50, librevenge::RVNG_SEEK_SET);
    for (int j=0; j<(user ? 5 : 4); ++j) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << j+1 << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool CanvasStyleManager::readGradient(MWAWEntry const &entry, MWAWGraphicStyle::Gradient &gradient)
{
  auto input=getInput();
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Gradient):";

  if (entry.length()<126 || !input || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readGradient: can not find the gradient data\n"));
    if (input)
      input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    f << "###";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }

  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  MWAWColor colors[2];
  int val;
  for (int st=0; st<2; ++st) {
    long pos=input->tell();
    f.str("");
    f << "Gradient-col" << st << ":";
    val=int(input->readULong(2));
    if (val!=0x8000) f << "fl=" << std::hex << val << std::dec << ",";
    for (int wh=0; wh<2; ++wh) {
      unsigned char col[3];
      for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
      MWAWColor color(col[0],col[1],col[2]) ;
      if (wh==0) colors[st]=color;
      f << "c" << wh << "=" << color << ",";
    }
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+46, librevenge::RVNG_SEEK_SET);
    f << "],";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  long pos=input->tell();
  f.str("");
  f << "Gradient-data:";
  int method=int(input->readLong(2));
  f << "method=" << method << ","; // TB, BT, LR, RL, circ, ellip, rect, directional, shape
  f << "using=" << input->readLong(2) << ","; // palette, rgb, dithered
  val=int(input->readLong(2)); // center, mouse
  bool useCenterPoint=val==2;
  if (val!=1 && val!=2)
    f << "##center[flag]=" << val << ",";
  int rate=int(input->readLong(2));
  if (rate!=1)
    f << "rate=" << rate << ","; // constant, dual, accelerating
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  int dir[2];
  for (auto &d : dir) d=int(input->readLong(2));
  if (method==8)
    f << "dir=" << MWAWVec2i(dir[0],dir[1]) << ",";
  int center[2];
  for (auto &d : center) d=int(input->readLong(2));
  if (useCenterPoint) {
    f << "center=" << MWAWVec2i(center[0],center[1]) << ",";
    gradient.m_percentCenter=MWAWVec2f(float(center[0])/100,float(center[1])/100);
  }
  switch (method) {
  case 1:
  case 2:
  case 3:
  case 4: {
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Linear;
    int const angles[]= {90, 270, 0, 180};
    gradient.m_angle=float(angles[method-1]+90);
    break;
  }
  case 5:
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Radial;
    break;
  case 6:
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Ellipsoid;
    break;
  case 7:
  case 9: // shape
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Square;
    break;
  case 8:
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Linear;
    if (dir[0]<0 || dir[0]>0 || dir[1]<0 || dir[1]>0) // checkme: this is probably bad
      gradient.m_angle=float(std::atan2(dir[1],dir[0])*180/M_PI)+180;
    break;
  default:
    gradient.m_type=MWAWGraphicStyle::Gradient::G_Linear;
    MWAW_DEBUG_MSG(("CanvasStyleManager::readGradient: unknown method=%d\n", method));
    f << "##method,";
    break;
  }
  if (rate==2) {
    gradient.m_stopList.resize(3);
    for (size_t i=0; i<3; ++i)
      gradient.m_stopList[i]=MWAWGraphicStyle::Gradient::Stop(float(i)/2,colors[i<2 ? i : 0]);
  }
  else {
    gradient.m_stopList.resize(2);
    for (size_t i=0; i<2; ++i)
      gradient.m_stopList[i]=MWAWGraphicStyle::Gradient::Stop(float(i),colors[1-i]);
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasStyleManager::readPatterns(int numPatterns)
{
  if (!m_mainParser->decode(8*numPatterns)) {
    MWAW_DEBUG_MSG(("CanvasParser::readPatterns: can not decode the input\n"));
    return false;
  }
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  if (!input || !input->checkPosition(pos+8*numPatterns)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readPatterns: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Pattern):";
  int N=int(input->readULong(2));
  if (N!=120)
    f << "f0=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->m_patterns.resize(size_t(numPatterns));
  for (size_t i=0; i<size_t(numPatterns)-1; ++i) {
    pos=input->tell();
    f.str("");
    f << "Pattern-" << i << ":";
    auto &pat = m_state->m_patterns[i];
    pat.m_dim=MWAWVec2i(8,8);
    pat.m_data.resize(8);
    for (auto &c : pat.m_data) c=(unsigned char) input->readULong(1);
    f << pat << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  ascFile.addPos(input->tell());
  ascFile.addNote("Pattern-end:");
  // in general some 0 but sometimes junk?
  input->seek(6, librevenge::RVNG_SEEK_CUR);
  return true;
}

bool CanvasStyleManager::readPenSize()
{
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  if (!input || !input->checkPosition(pos+20)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readPenSize: file is too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(PenSize):sz=[";
  for (int i=0; i<10; ++i)
    f << double(input->readULong(2))/256. << ",";
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool CanvasStyleManager::readFonts(int numFonts)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (numFonts<=0 || !input->checkPosition(pos+132*numFonts)) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readFonts: zone seems too short\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(Font):";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  bool const isWindows=m_mainParser->isWindowsFile();
  auto fontConverter=m_parserState->m_fontConverter;
  std::string const family=isWindows ? "CP1252" : "";
  for (int fo=0; fo<numFonts; ++fo) {
    pos=input->tell();
    f.str("");
    f << "Font-" << fo << ":";
    int id=int(input->readULong(2));
    f << "id=" << id << ",";
    int val=int(input->readLong(2)); // 0
    if (val) f << "f0=" << val << ",";
    int dSz=int(input->readULong(1));
    if (dSz>=127) {
      MWAW_DEBUG_MSG(("CanvasStyleManager::readFonts: can not read a name\n"));
      f << "###name";
    }
    else {
      std::string name;
      for (int s=0; s<dSz; ++s) name+=char(input->readULong(1));
      if (!name.empty())
        fontConverter->setCorrespondance(isWindows ? fo+1 : id, name, family);
      f << name << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+132, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// Windows resource
//
////////////////////////////////////////////////////////////
bool CanvasStyleManager::readColorValues(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<32*16) {
    MWAW_DEBUG_MSG(("CanvasStyleManager::readColorValues: the zone seems too small\n"));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries(CVal)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  int N=int(entry.length()/16); // normally 256
  unsigned char col[4];
  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "CVal-" << i << ":";
    int val=int(input->readULong(2));
    if (val!=0x8000) f << "f0=" << std::hex << val << std::dec << ",";
    for (int c=0; c<3; ++c) col[c]=(unsigned char)(input->readULong(2)>>8);
    f << MWAWColor(col[0], col[1], col[2]) << ",";
    for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8); // CMYK?
    f << "col2=" << MWAWColor(col[0], col[1], col[2], col[3]) << ",";
    input->seek(pos+16,librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
