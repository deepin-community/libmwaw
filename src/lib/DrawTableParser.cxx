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
#include "MWAWRSRCParser.hxx"
#include "MWAWStringStream.hxx"
#include "MWAWSubDocument.hxx"

#include "DrawTableParser.hxx"

/** Internal: the structures of a DrawTableParser */
namespace DrawTableParserInternal
{

////////////////////////////////////////
//! Internal: the state of a DrawTableParser
struct State {
  //! constructor
  State()
    : m_openedGroup(0)
    , m_patternList()
    , m_maxDim(0,0)
  {
  }

  //! try to return a color
  MWAWColor getColor(int id) const;
  //! try to init a patterns
  bool getPattern(int id, MWAWGraphicStyle::Pattern &pat) const;
  //! init the patterns list
  void initPatterns();
  //! the number of opened groups
  int m_openedGroup;
  //! the patterns list
  std::vector<MWAWGraphicStyle::Pattern> m_patternList;
  //! the max dimensions
  MWAWVec2f m_maxDim;
};

MWAWColor State::getColor(int id) const
{
  if (id<0 || id>=8) {
    MWAW_DEBUG_MSG(("DrawTableParserInternal::State::getColor: unknown color %d\n", id));
    return MWAWColor::white();
  }
  MWAWColor const colors[]= {
    MWAWColor::white(), MWAWColor::black(), MWAWColor(255,0,0), MWAWColor(0,255,0),
    MWAWColor(0,0,255), MWAWColor(0,255,255), MWAWColor(255,0,255), MWAWColor(255,255,0)
  };
  return colors[id];
}

bool State::getPattern(int id, MWAWGraphicStyle::Pattern &pat) const
{
  if (m_patternList.empty())
    const_cast<State *>(this)->initPatterns();
  if (id<0 || id>=int(m_patternList.size())) {
    MWAW_DEBUG_MSG(("DrawTableParserInternal::State::getPattern: unknown pattern %d\n", id));
    return false;
  }
  pat=m_patternList[size_t(id)];
  return true;
}

void State::initPatterns()
{
  if (!m_patternList.empty()) return;
  static uint16_t const patterns[]= {
    0x0, 0x0, 0x0, 0x0, 0xffff, 0xffff, 0xffff, 0xffff, 0x77dd, 0x77dd, 0x77dd, 0x77dd, 0xaa55, 0xaa55, 0xaa55, 0xaa55,
    0x8822, 0x8822, 0x8822, 0x8822, 0x8800, 0x2200, 0x8800, 0x2200, 0x8000, 0x800, 0x8000, 0x800, 0x8000, 0x0, 0x800, 0x0,
    0x8080, 0x413e, 0x808, 0x14e3, 0xff80, 0x8080, 0xff08, 0x808, 0x8142, 0x2418, 0x8142, 0x2418, 0x8040, 0x2010, 0x804, 0x201,
    0xe070, 0x381c, 0xe07, 0x83c1, 0x77bb, 0xddee, 0x77bb, 0xddee, 0x8844, 0x2211, 0x8844, 0x2211, 0x99cc, 0x6633, 0x99cc, 0x6633,
    0x2040, 0x8000, 0x804, 0x200, 0xff00, 0xff00, 0xff00, 0xff00, 0xff00, 0x0, 0xff00, 0x0, 0xcc00, 0x0, 0x3300, 0x0,
    0xf0f0, 0xf0f0, 0xf0f, 0xf0f, 0xff88, 0x8888, 0xff88, 0x8888, 0xaa44, 0xaa11, 0xaa44, 0xaa11, 0x102, 0x408, 0x1020, 0x4080,
    0x8307, 0xe1c, 0x3870, 0xe0c1, 0xeedd, 0xbb77, 0xeedd, 0xbb77, 0x1122, 0x4488, 0x1122, 0x4488, 0x3366, 0xcc99, 0x3366, 0xcc99,
    0x40a0, 0x0, 0x40a, 0x0, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0x8888, 0x8888, 0x8888, 0x8888, 0x101, 0x1010, 0x101, 0x1010,
    0x8, 0x142a, 0x552a, 0x1408, 0xff80, 0x8080, 0x8080, 0x8080, 0x8244, 0x2810, 0x2844, 0x8201, 0x0, 0x0, 0x0, 0x0,
    0x8000, 0x0, 0x0, 0x0, 0x8000, 0x0, 0x800, 0x0, 0x8800, 0x0, 0x800, 0x0, 0x8800, 0x0, 0x8800, 0x0,
    0x8800, 0x2000, 0x8800, 0x0, 0x8800, 0x2000, 0x8800, 0x200, 0x8800, 0x2200, 0x8800, 0x2200, 0xa800, 0x2200, 0x8a00, 0x2200,
    0xaa00, 0x2200, 0xaa00, 0x2200, 0xaa00, 0xa200, 0xaa00, 0x2a00, 0xaa00, 0xaa00, 0xaa00, 0xaa00, 0xaa40, 0xaa00, 0xaa04, 0xaa00,
    0xaa44, 0xaa00, 0xaa44, 0xaa00, 0xaa44, 0xaa10, 0xaa44, 0xaa01, 0xaa44, 0xaa11, 0xaa44, 0xaa11, 0xaa54, 0xaa11, 0xaa45, 0xaa11,
    0xaa55, 0xaa11, 0xaa55, 0xaa11, 0xaa55, 0xaa51, 0xaa55, 0xaa15, 0xaa55, 0xaa55, 0xaa55, 0xaa55, 0xea55, 0xaa55, 0xae55, 0xaa55,
    0xee55, 0xaa55, 0xee55, 0xaa55, 0xee55, 0xba55, 0xee55, 0xab55, 0xee55, 0xbb55, 0xee55, 0xbb55, 0xfe55, 0xbb55, 0xef55, 0xbb55,
    0xff55, 0xbb55, 0xff55, 0xbb55, 0xff55, 0xfb55, 0xff55, 0xbf55, 0xff55, 0xff55, 0xff55, 0xff55, 0xffd5, 0xff55, 0xff5d, 0xff55,
    0xffdd, 0xff55, 0xffdd, 0xff55, 0xffdd, 0xff75, 0xffdd, 0xff57, 0xffdd, 0xff77, 0xffdd, 0xff77, 0xfffd, 0xff77, 0xffdf, 0xff77,
    0xffff, 0xff77, 0xffff, 0xff77, 0xffff, 0xfff7, 0xffff, 0xff7f, 0xffff, 0xffff, 0xffff, 0xffff, 0x81c3, 0x8100, 0x183c, 0x1800,
    0xffff, 0x0, 0xffff, 0x0, 0x1122, 0x2211, 0x1188, 0x8811, 0xbb00, 0x0, 0xee00, 0x0, 0xa55a, 0xa545, 0x45ba, 0x45ba,
    0x82c7, 0x10, 0x287c, 0x1, 0xe7db, 0x9966, 0x6699, 0xdbe7, 0x66, 0x6f0f, 0x3e78, 0x7b33, 0xefef, 0xffef, 0xefef, 0x28ef,
    0x7fd, 0x1b0e, 0x6672, 0x5272, 0xdb66, 0xbddb, 0xbd66, 0xdbbd, 0x525, 0x7525, 0x525, 0x5525, 0xff00, 0x0, 0x4024, 0xa850,
    0xcccc, 0xcccc, 0xcccc, 0xcccc, 0xbfb0, 0xb0b0, 0xb0bf, 0xbf, 0x6600, 0x99, 0x9900, 0x66, 0x1010, 0x1010, 0x1028, 0xc628,
    0xe0a0, 0xe000, 0xe0a, 0xe00, 0xebeb, 0xebeb, 0xebeb, 0xebeb, 0xc366, 0x3c66, 0xc366, 0x3c66, 0x8004, 0x2211, 0x8004, 0x2211,
    0xcf4d, 0xca4d, 0xca4d, 0xcf00, 0x83c6, 0x6c38, 0x180c, 0x603, 0xff, 0x80be, 0xa2aa, 0x2aeb, 0x6dab, 0xd729, 0xd7ab, 0x6dfe,
    0xaabf, 0xa0bf, 0xaafb, 0xafb, 0x1010, 0x10, 0x1010, 0xd710, 0x18, 0x187e, 0x7e18, 0x1800, 0x82a, 0x1463, 0x142a, 0x880,
    0x2418, 0x8142, 0x4281, 0x1824, 0xffff, 0xffff, 0xff, 0xff, 0xcc06, 0x3318, 0xcc60, 0x3381, 0x447c, 0x4483, 0x3844, 0xc744,
    0x2808, 0x3000, 0x0, 0x6080, 0x7e3c, 0x99c3, 0xe7c3, 0x993c, 0x220e, 0x8838, 0x22e0, 0x8883, 0x80be, 0xa2aa, 0xaaba, 0x82fe,
    0x40, 0x5c5c, 0x5c40, 0x7e00, 0xaa55, 0xaa55, 0x0, 0x0, 0x0, 0x0, 0x40e0, 0x4040, 0x81c, 0x3e7f, 0xf7e3, 0xc180,
    0x3e7f, 0x7f7f, 0x7f7f, 0x3e80, 0xeaee, 0xeaee, 0xeaee, 0xeaee, 0x10, 0x1, 0x20, 0x4, 0xf68e, 0x7efd, 0xc3bf, 0x7ff8,
    0x8888, 0x8888, 0x8877, 0x22dd, 0x3800, 0x3800, 0x3800, 0x3800, 0xaaaa, 0x5555, 0xaaaa, 0x5555, 0xc0c0, 0xc0ff, 0xffc0, 0xc0c0,
    0xff80, 0x8183, 0x878f, 0x9fbf, 0xa050, 0xa050, 0xa050, 0xa050, 0xd0a0, 0xd0a0, 0xd0bc, 0xf2e1, 0x8310, 0x55, 0x10, 0x8393,
    0xff00, 0x7755, 0xdd00, 0xff00, 0x182, 0x7c54, 0x7c54, 0x7c82, 0x10, 0x10fe, 0x7c38, 0x6c44, 0x2874, 0xeac5, 0x83c5, 0xea74,
    0x288, 0x75d8, 0xa8d8, 0x7588, 0x0, 0xaaaa, 0xaa00, 0x0, 0xcccc, 0x3333, 0xcccc, 0x3333, 0x24e7, 0x7e, 0x427e, 0xe7,
    0x7f1f, 0xdfc7, 0xf7f1, 0xfd7c, 0x4182, 0x50a, 0x1428, 0x50a0, 0x8894, 0x2249, 0x8800, 0xaa00, 0x300, 0x6066, 0x600, 0x3033,
    0x7744, 0x5c50, 0x7705, 0x1d11, 0xe3dd, 0x3eba, 0x3edd, 0xa3eb, 0x1c1c, 0x14e3, 0xc1e3, 0x141c, 0x2449, 0x9224, 0x9249, 0x2492,
    0xe724, 0xbd81, 0x7e42, 0xdb18, 0xe000, 0x3800, 0xe00, 0x8300, 0x60, 0x908c, 0x43e0, 0x0
  };
  MWAWGraphicStyle::Pattern pat;
  pat.m_dim=MWAWVec2i(8,8);
  pat.m_data.resize(8);
  pat.m_colors[0]=MWAWColor::white();
  pat.m_colors[1]=MWAWColor::black();
  m_patternList.push_back(pat); // none pattern

  int numPatterns=int(MWAW_N_ELEMENTS(patterns))/4;
  uint16_t const *patPtr=patterns;
  for (int i=0; i<numPatterns; ++i) {
    for (size_t j=0; j<8; j+=2, ++patPtr) {
      pat.m_data[j]=uint8_t((*patPtr)>>8);
      pat.m_data[j+1]=uint8_t((*patPtr)&0xFF);
    }
    m_patternList.push_back(pat);
  }
}

////////////////////////////////////////
//! Internal: the subdocument of a DrawTableParser
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor from a zoneId
  SubDocument(DrawTableParser &parser, MWAWInputStreamPtr const &input, MWAWEntry const &entry, MWAWFont const &font, MWAWParagraph const &para)
    : MWAWSubDocument(&parser, input, entry)
    , m_font(font)
    , m_para(para)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the font style
  MWAWFont m_font;
  //! the paragraph style
  MWAWParagraph m_para;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("DrawTableParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  listener->setFont(m_font);
  listener->setParagraph(m_para);
  if (!m_input || !m_zone.valid() || !m_input->checkPosition(m_zone.end()))
    return;
  long pos = m_input->tell();
  m_input->seek(m_zone.begin(), librevenge::RVNG_SEEK_SET);
  while (m_input->tell()<m_zone.end() && !m_input->isEnd()) {
    unsigned char c=(unsigned char)(m_input->readULong(1));
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      if (m_input->tell()<m_zone.end())
        listener->insertEOL();
      break;
    default:
      if (c<=0x1f) {
        MWAW_DEBUG_MSG(("DrawTableParserInternal::SubDocument::parse: find unexpected char=%x\n", (unsigned int)(c)));
      }
      else
        listener->insertCharacter(c);
    }
  }
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
DrawTableParser::DrawTableParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new DrawTableParserInternal::State);

  getPageSpan().setMargins(0.1);
}

DrawTableParser::~DrawTableParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void DrawTableParser::parse(librevenge::RVNGDrawingInterface *docInterface)
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
      sendShapes();
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("DrawTableParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void DrawTableParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("DrawTableParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  // check if the document has multiple pages, if yes increase the page
  if (ps.getFormLength()>0 && 1.02*ps.getFormLength()*72<m_state->m_maxDim[0]) {
    int numPageY=int(m_state->m_maxDim[0]/ps.getFormLength()/72)+1;
    MWAW_DEBUG_MSG(("DrawTableParser::createDocument: increase Y pages to %d\n", numPageY));
    ps.setFormLength(ps.getFormLength()*(numPageY>10 ? 10 : numPageY));
  }
  if (ps.getFormWidth()>0 && 1.02*ps.getFormWidth()*72<m_state->m_maxDim[1]) {
    int numPageX=int(m_state->m_maxDim[1]/ps.getFormWidth()/72)+1;
    MWAW_DEBUG_MSG(("DrawTableParser::createDocument: increase X pages to %d\n", numPageX));
    ps.setFormWidth(ps.getFormWidth()*(numPageX>10 ? 10 : numPageX));
  }
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
bool DrawTableParser::createZones()
{
  auto input=getInput();
  if (!input) return false;

  if (!readPrefs() || !readPrintInfo() || !readFonts())
    return false;

  long pos=input->tell();
  if (!computeMaxDimension())
    return false;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool DrawTableParser::computeMaxDimension()
{
  auto input=getInput();
  int numShapes=0;
  while (input->checkPosition(input->tell()+10)) {
    long pos=input->tell();
    int val=int(input->readULong(2));
    if (val==0)
      continue;
    if (val!=6)
      break;
    int type=int(input->readULong(2));
    if (type<=1 || type>=10)
      break;
    input->seek(2, librevenge::RVNG_SEEK_CUR); // flags
    int headerSz=int(input->readULong(4));
    long endPos=pos+10+headerSz;
    if (!input->checkPosition(endPos))
      break;
    long dimPos=0;
    switch (type) {
    case 2:
    case 3:
      dimPos=endPos-8;
      break;
    case 4:
      dimPos=pos+22;
      break;
    case 7:
    case 9:
      dimPos=endPos-10;
      break;
    case 8:
      dimPos=endPos-16;
      break;
    default:
      break;
    }
    if (dimPos>=pos+10 && dimPos+8<=endPos) {
      input->seek(dimPos, librevenge::RVNG_SEEK_SET);
      for (int pt=0; pt<2; ++pt) {
        if (type==4 && pt==1)
          break;
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(2))/10.f;
        for (int i=0; i<2; ++i) {
          if (dim[i]>m_state->m_maxDim[i])
            m_state->m_maxDim[i]=dim[i];
        }
      }
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    int const numData[]= {
      0, 0, 0, 0, 0,
      2, 0, 1, 2, 1
    };
    bool ok=true;
    for (int i=0; i<numData[type]; ++i) {
      pos=input->tell();
      int dSz=int(input->readULong(2));
      if (!input->checkPosition(pos+2+dSz)) {
        ok=false;
        break;
      }
      if (type==5 && i==0 && (dSz%4)==0) {
        for (int pt=0; pt<(dSz/4); ++pt) {
          float dim[2];
          for (auto &d : dim) d=float(input->readLong(2))/10.f;
          for (int j=0; j<2; ++j) {
            if (dim[j]>m_state->m_maxDim[j])
              m_state->m_maxDim[j]=dim[j];
          }
        }
      }
      input->seek(pos+2+dSz, librevenge::RVNG_SEEK_SET);
    }
    if (!ok)
      break;
    ++numShapes;
  }
  return numShapes>0;
}

bool DrawTableParser::readFonts()
{
  auto input=getInput();
  libmwaw::DebugStream f;
  auto fontConverter=getFontConverter();
  while (input->checkPosition(input->tell()+6)) {
    long pos=input->tell();
    if (input->readULong(2)!=2) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return true;
    }
    f.str("");
    f << "Entries(Font):";
    int dataSz[2];
    for (auto &d : dataSz) d=int(input->readULong(2));
    if (dataSz[0]>dataSz[1])
      std::swap(dataSz[0],dataSz[1]);
    if (!input->checkPosition(pos+6+dataSz[1])) {
      MWAW_DEBUG_MSG(("DrawTableParser::readFonts: zone seems too short\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    int type=int(input->readULong(2));
    bool ok=false;
    switch (type) {
    case 0:
      ok=true;
      break;
    case 1: {
      if (dataSz[0]<3) {
        MWAW_DEBUG_MSG(("DrawTableParser::readFonts: the data size seems to short\n"));
        break;
      }
      int id=int(input->readULong(2));
      f << "id=" << id << ",";
      int dSz=int(input->readULong(1));
      if (3+dSz>dataSz[0])
        break;
      ok=true;
      std::string name;
      for (int s=0; s<dSz; ++s) name+=char(input->readULong(1));
      if (!name.empty())
        fontConverter->setCorrespondance(id, name);
      f << name << ",";
      break;
    }
    default:
      f << "type=" << type << ",";
      MWAW_DEBUG_MSG(("DrawTableParser::readFonts: unknown type\n"));
      break;
    }
    if (!ok)
      f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+6+dataSz[1], librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool DrawTableParser::readPrintInfo()
{
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo):";
  auto sz = long(input->readULong(2));
  if (sz < 0x78 || !input->checkPosition(pos+2+sz)) {
    MWAW_DEBUG_MSG(("DrawTableParser::readPrintInfo: can not find the print info zone\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  // print info
  libmwaw::PrinterInfo info;
  if (!info.read(input))
    f << "###";
  else {
    f << info;
    MWAWVec2i paperSize = info.paper().size();
    MWAWVec2i pageSize = info.page().size();
    if (pageSize.x() > 0 && pageSize.y() > 0 && paperSize.x() > 0 && paperSize.y() > 0) {
      MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
      MWAWVec2i rBotMargin=info.paper().pos(1) - info.page().pos(1);

      getPageSpan().setMarginTop(lTopMargin.y()<0 ? 0 : lTopMargin.y()/72.0);
      getPageSpan().setMarginBottom(rBotMargin.y()<0 ? 0 : rBotMargin.y()/72.0);
      getPageSpan().setMarginLeft(lTopMargin.x()<0 ? 0 : lTopMargin.x()/72.0);
      getPageSpan().setMarginRight(rBotMargin.y()<0 ? 0 : rBotMargin.y()/72.0);
      getPageSpan().setFormLength(paperSize.y()/72.);
      getPageSpan().setFormWidth(paperSize.x()/72.);
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+2+sz, librevenge::RVNG_SEEK_SET);
  return true;
}

bool DrawTableParser::readPrefs()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+172)) {
    MWAW_DEBUG_MSG(("DrawTableParser::readPrefs: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  int val;
  f << "Entries(Prefs):";
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(2));
    int const expected[]= { 0 /* or 1*/, 4 /* 2-4*/, 3, 0xc };
    if (val==expected[i]) continue;
    if (i==2)
      f << "font[id]=" << val << ",";
    else if (i==3)
      f << "font[sz]=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<3; ++i) { // f4=0|1
    val=int(input->readULong(2));
    if (!val) continue;
    if (i==0)
      f << "font[flags]=" << std::hex << val << std::dec << ",";
    else if (i==1) {
      if (val&0xff00) f << "align=" << (val>>8) << ",";
      if (val&0xff) f << "interline=" << float(2+(val&0xff))/2.f << ",";
    }
    else
      f << "f" << i+4 << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) { // always 101: an int or two boolean ?
    val=int(input->readULong(1));
    if (val==1) continue;
    if (i==1)
      f << "font[color]=" << val << ",";
    else
      f << "fl" << i << "=" << val << ",";
  }
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(2));
    int const expected[]= { 10, 1, 2, 1 };
    if (val!=expected[i]) f << "f" << i+7 << "=" << val << ",";
  }
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(2));
  if (dim[1]&3) {
    f << "penSize=" << (dim[0]&0x7fff) << "/" << (dim[1]>>2);
    if ((dim[1]&3)!=1) f << "[" << (dim[1]&3) << "]";
    if (dim[0]&0x8000) f << "_dec"; // decimal or frac
    f << ",";
  }
  for (int st=0; st<2; ++st) {
    f << (st==0 ? "line" : "surf") << "[style]=[";
    val=int(input->readULong(1));
    if (val==0)
      f << "none,";
    else if (val!=2-st)
      f << "pat=" << val << ",";
    for (int i=0; i<2; ++i) { // 0: back, 1: front
      val=int(input->readULong(1));
      if (val!=i)
        f << "col" << i << "=" << val << ",";
    }
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    f << "],";
  }
  for (int i=0; i<13; ++i) { // g0=6|15, g4=0|1, g6=g10=1
    val=int(input->readULong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  float fDim[2];
  for (auto &d : fDim) d=float(input->readLong(2))/10.f;
  f << "dim=" << MWAWVec2f(fDim[1], fDim[0]) << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+66, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Prefs-1:";
  for (int i=0; i<9; ++i) {
    val=int(input->readULong(2));
    int const expected[]= { 0 /* or 1*/, 0, 1, 1, 0 /* or 1*/, 0, 1, 0, 2 };
    if (val==expected[i]) continue;
    if (i==5 && val==1)
      f << "spline,";
    else
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<10; ++i) { // g1=0|1
    val=int(input->readULong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  val=int(input->readULong(2)); // 0|6|c6e
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(2)); // 0-2
  if (val) f << "h0=" << val << ",";
  for (auto &d : fDim) d=float(input->readLong(2))/10.f;
  f << "dim=" << MWAWVec2f(fDim[1], fDim[0]) << ","; // unsure two times the same number
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(pos+46, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Prefs-2:";
  for (int i=0; i<30; ++i) { // f8=0|1 f10=1
    val=int(input->readULong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool DrawTableParser::checkHeader(MWAWHeader *header, bool strict)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(316))
    return false;

  libmwaw::DebugStream f;
  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (input->readULong(2)!=0xc || input->readULong(2)!=0x1357)
    return false;
  f << "FileHeader:";
  int const vers=1;
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_DRAWINGTABLE, vers, MWAWDocument::MWAW_K_DRAW);
  for (int i=0; i<6; ++i) { // checkme: f0=f2 some file's version?
    int const expected[]= {0x13 /* or 14*/, 0, 0x13 /* or 14*/, 2, 2, 0xac};
    int val=int(input->readLong(2));
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  if (strict) {
    long pos=input->tell();
    input->seek(0xbc, librevenge::RVNG_SEEK_SET);
    if (!readPrintInfo()) return false;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool DrawTableParser::sendShapes()
{
  auto input=getInput();
  auto listener=getGraphicListener();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("DrawTableParser::sendShapes: can not find the listener\n"));
    return false;
  }

  while (input->checkPosition(input->tell()+2)) {
    long pos=input->tell();
    if (sendShape())
      continue;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (m_state->m_openedGroup) {
    MWAW_DEBUG_MSG(("DrawTableParser::sendShapes: find unclosed group\n"));
  }
  while (m_state->m_openedGroup-->0)
    listener->closeGroup();
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("DrawTableParser::sendShapes: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Extra):###");
  }
  return true;
}

bool DrawTableParser::sendShape()
{
  auto input=getInput();
  auto listener=getGraphicListener();
  if (!input || !listener)
    return false;
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Shape):";
  if (!input->checkPosition(pos+2))
    return false;
  int type=int(input->readULong(2));
  if (type==0) { // end of shapes' list or end of group
    if (m_state->m_openedGroup>0) {
      --m_state->m_openedGroup;
      listener->closeGroup();
    }
    ascii().addPos(pos);
    ascii().addNote("_");
    return true;
  }
  if (type!=6 || !input->checkPosition(pos+10))
    return false;

  type=int(input->readULong(2));
  char const *wh[]= {
    nullptr, nullptr, "line", "rect", "arc",
    "poly", "group", "bitmap", "epsf", "text"
  };
  int const numData[]= {
    0, 0, 0, 0, 0,
    2, 0, 1, 2, 1
  };
  std::string what;
  int nbData=0;
  if (type<10 && wh[type]) {
    what=wh[type];
    nbData=numData[type];
  }
  else {
    MWAW_DEBUG_MSG(("DrawTableParser::sendShape: find unknown shape %d\n", type));
    f << "###";

    std::stringstream s;
    s << "Type" << type;
    what=s.str();
  }
  f << what << ",";

  int val=int(input->readULong(2));
  if (val&1) f << "selected,";
  if (val&0x40) f << "group,";
  if (val&0x80) f << "locked,";
  val&=0xFF3E;
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(2)); // 0
  if (val) f << "f0=" << val << ",";
  int headerSz=int(input->readULong(2));
  long endPos=pos+10+headerSz;
  if (!input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("DrawTableParser::sendShape: the zone seems too short\n"));
    f << "###hSz=" << headerSz << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  MWAWGraphicStyle style=MWAWGraphicStyle::emptyStyle();
  if (headerSz>=8 && type!=9) {
    int dim[2];
    for (auto &d : dim) d=int(input->readULong(2));
    if (dim[1]&3) {
      int fDim[]= {(dim[0]&0x7fff), (dim[1]>>2)};
      if (fDim[1]) style.m_lineWidth=72*float(fDim[0])/float(fDim[1]);
      f << "penSize=" << fDim[0] << "/" << fDim[1];
      if ((dim[1]&3)!=1) f << "[" << (dim[1]&3) << "]"; // main pt LT, C, BT
      if (dim[0]&0x8000) f << "_dec"; // decimal or frac
      f << ",";
    }
    for (int st=0; st<2; ++st) {
      if (st==1 && (type==2 || headerSz<12)) break;
      f << (st==0 ? "line" : "surface") << "[style]=[";
      val=int(input->readULong(1));
      if (val==0)
        f << "none,";
      else if (val!=2)
        f << "pat=" << val << ",";
      bool ok=true;
      MWAWGraphicStyle::Pattern pat;
      ok=val!=0 && m_state->getPattern(val, pat);
      for (int i=0; i<2; ++i) { // 0: back, 1: front
        val=int(input->readULong(1));
        pat.m_colors[i]=m_state->getColor(val);
        if (val!=i)
          f << "col" << i << "=" << val << ",";
      }
      if (st==0 && type==2) {
        val=int(input->readULong(1));
        if (val) f << "arrow=" << val << ",";
        if (val&1) style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
        if (val&2) style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
      }
      else
        input->seek(1, librevenge::RVNG_SEEK_CUR);
      f << "],";
      if (st==0) {
        if (!ok)
          style.m_lineWidth=0;
        else
          pat.getAverageColor(style.m_lineColor);
      }
      else if (ok)
        style.setPattern(pat);
    }
  }
  MWAWGraphicShape shape;
  MWAWBox2f shapeBox;
  int numPolyPoints=0;
  int polyType=0;
  MWAWBox2i bitmapBox;
  MWAWFont font;
  MWAWParagraph para;

  switch (type) {
  case 2: {
    if (headerSz!=18) break;
    val=int(input->readULong(1));
    if (val!=1) f << "f1=" << val << ",";
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    shape=MWAWGraphicShape::line(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2])) << ",";
    shapeBox=shape.getBdBox();
    break;
  }
  case 3: {
    if (headerSz!=28) break;
    int fl=int(input->readULong(2));
    if (fl==1)
      f << "round/oval,";
    else if (fl)
      f << "##f1=" << fl << ",";
    int round=int(input->readULong(2));
    if (round)
      f << "roundSz=" << round << ",";
    for (int i=0; i<2; ++i) { // 0,0|-1
      val=int(input->readLong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    shapeBox=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << shapeBox << ",";
    if (round || fl!=1)
      shape=MWAWGraphicShape::rectangle(shapeBox, MWAWVec2f(float(round),float(round)));
    else
      shape=MWAWGraphicShape::circle(shapeBox);
    break;
  }
  case 4: {
    if (headerSz!=26) break;
    MWAWVec2f pts[2];
    for (auto &pt : pts) {
      float dim[2];
      for (auto &d : dim) d=float(input->readLong(2))/10.f;
      pt=MWAWVec2f(dim[1],dim[0]);
    }
    shapeBox=MWAWBox2f(pts[0]-pts[1],pts[0]+pts[1]);
    f << shapeBox << ",";
    val=int(input->readLong(2));
    if (val)
      f << "f1=" << val << ",";
    int fileAngles[2];
    for (auto &angl : fileAngles) angl=int(input->readLong(2));
    f << "angle=" << fileAngles[0] << "->" << fileAngles[0]+fileAngles[1] << ",";
    int angle[2] = { int(90-fileAngles[0]-fileAngles[1]), int(90-fileAngles[0]) };
    if (angle[1]<angle[0])
      std::swap(angle[0],angle[1]);
    if (angle[1]>360) {
      int numLoop=int(angle[1]/360)-1;
      angle[0]-=int(numLoop*360);
      angle[1]-=int(numLoop*360);
      while (angle[1] > 360) {
        angle[0]-=360;
        angle[1]-=360;
      }
    }
    if (angle[0] < -360) {
      int numLoop=int(angle[0]/360)+1;
      angle[0]-=int(numLoop*360);
      angle[1]-=int(numLoop*360);
      while (angle[0] < -360) {
        angle[0]+=360;
        angle[1]+=360;
      }
    }
    MWAWVec2f center = shapeBox.center();
    MWAWVec2f axis = 0.5f*MWAWVec2f(shapeBox.size());
    // we must compute the real bd box
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; i++)
      limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; bord++) {
      float ang = (bord == limitAngle[0]) ? float(angle[0]) :
                  (bord == limitAngle[1]+1) ? float(angle[1]) : 90 * float(bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { axis[0] *std::cos(ang), -axis[1] *std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    MWAWBox2f realBox(MWAWVec2f(center[0]+minVal[0],center[1]+minVal[1]),
                      MWAWVec2f(center[0]+maxVal[0],center[1]+maxVal[1]));
    if (style.hasSurface())
      shape = MWAWGraphicShape::pie(realBox, shapeBox, MWAWVec2f(float(angle[0]),float(angle[1])));
    else
      shape = MWAWGraphicShape::arc(realBox, shapeBox, MWAWVec2f(float(angle[0]),float(angle[1])));
    shapeBox=realBox;
    break;
  }
  case 5:
    if (headerSz!=20) break;
    for (int i=0; i<2; ++i) { // 0,0
      val=int(input->readLong(2));
      if (val)
        f << "f" << i+1 << "=" << val << ",";
    }
    numPolyPoints=int(input->readLong(2));
    f << "num[pts]=" << numPolyPoints << ",";
    polyType=int(input->readLong(2));
    f << "type=" << polyType << ","; // (type&1) means closed
    if (polyType==0) {
      if (!style.hasSurface())
        shape=MWAWGraphicShape::polyline(shapeBox);
      else
        shape=MWAWGraphicShape::polygon(shapeBox);
    }
    else if (polyType==1)
      shape=MWAWGraphicShape::polygon(shapeBox);
    else if (polyType==2 || polyType==3)
      shape=MWAWGraphicShape::path(shapeBox);
    else {
      MWAW_DEBUG_MSG(("DrawTableParser::sendShape: unknown polygon type\n"));
      f << "###";
      shape=MWAWGraphicShape::polyline(shapeBox);
      polyType=0;
    }
    break;
  case 7: {
    if (headerSz!=42) break;
    val=int(input->readLong(2));
    style.m_rotate=float(val);
    if (val) f << "rot=" << val << ",";
    val=int(input->readLong(2)); // 0
    if (val)
      f << "f1=" << val << ",";
    for (int i=0; i<2; ++i) {
      int dim[4];
      for (auto &d : dim) d=int(input->readLong(2));
      MWAWBox2i box(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
      if (i==0)
        bitmapBox=box;
      f << "dim" << i << "=" << box << ",";
    }
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    shapeBox=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << shapeBox << ",";
    val=int(input->readLong(2)); // 0
    if (val)
      f << "f3=" << val << ",";
    break;
  }
  case 8: {
    if (headerSz!=40) break;
    val=int(input->readLong(2));
    style.m_rotate=float(val);
    if (val) f << "rot=" << val << ",";
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    shapeBox=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << shapeBox << ",";
    val=int(input->readLong(2)); // 3
    if (val)
      f << "f2=" << val << ",";
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    f << "orig=" << MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2])) << ",";
    for (int i=0; i<2; ++i)
      f << "sz" << i << "=" << std::hex << input->readULong(4) << std::dec << ",";
    break;
  }
  case 9: {
    if (headerSz!=24) break;
    // font
    font.setId(int(input->readULong(2)));
    font.setSize(float(input->readLong(2)));
    uint32_t flags=0;
    val=int(input->readULong(2));
    if (val&0x1) flags |= MWAWFont::boldBit;
    if (val&0x2) flags |= MWAWFont::italicBit;
    if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (val&0x8) flags |= MWAWFont::embossBit;
    if (val&0x10) flags |= MWAWFont::shadowBit;
    font.setFlags(flags);
    f << font.getDebugString(getFontConverter());
    val&=0xffe0;
    if (val) f << "font[fl]=" << std::hex << val << std::dec << ",";
    // paragraph
    val=int(input->readULong(1));
    switch (val&3) {
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
    case 3:
    default:
      MWAW_DEBUG_MSG(("DrawTableParser::sendShape: find align=3\n"));
      f << "###align=3,";
      break;
    }
    if (val&0xfc) f << "#para[align]=" << (val>>2) << ",";
    val=int(input->readULong(1));
    switch (val&3) {
    case 0: // 1 line
      break;
    case 1:
      para.setInterline(1.5, librevenge::RVNG_PERCENT);
      f << "interline=150%,";
      break;
    case 2:
      para.setInterline(2, librevenge::RVNG_PERCENT);
      f << "interline=200%,";
      break;
    default:
      MWAW_DEBUG_MSG(("DrawTableParser::sendShape: find unknown interline\n"));
      f << "#interline3,";
    }
    if (val&0xfc) f << "#interline=" << (val>>2) << ",";
    val=int(input->readLong(2));
    if (val) f << "f3=" << val << ",";
    val=int(input->readULong(1));
    if (val!=1) f << "f4=" << val << ",";
    val=int(input->readULong(1));
    if (val!=1) {
      MWAWColor color=m_state->getColor(val);
      font.setColor(color);
      f << "text[color]=" << val << ",";
    }
    val=int(input->readLong(2));
    style.m_rotate=float(val);
    if (val) f << "rot=" << val << ",";
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2))/10.f;
    shapeBox=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    f << shapeBox << ",";
    f << "N=" << input->readULong(2) << ",";
    break;
  }
  default:
    break;
  }
  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  MWAWPosition position(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
  position.m_anchorTo = MWAWPosition::Page;
  if (type>=2 && type<=4)
    listener->insertShape(position, shape, style);
  else if (type==6 && m_state->m_openedGroup>=0) {
    ++m_state->m_openedGroup;
    listener->openGroup(position);
  }
  for (int i=0; i<nbData; ++i) {
    pos=input->tell();
    // checkme: find how blocks with size>=65536 are stored
    int dSz=int(input->readULong(2));
    f.str("");
    f << "Shape-" << i << "[" << what << "]:";
    if (!input->checkPosition(pos+2+dSz)) {
      MWAW_DEBUG_MSG(("DrawTableParser::sendShape: bad size for zone %d\n", i));
      f << "###dSz=" << dSz << ",";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (type==5) {
      if (i==0 && (dSz%4)==0) {
        f << "pts=[";
        std::vector<MWAWVec2f> points;
        for (int j=0; j<(dSz>>2); ++j) {
          float dim[2];
          for (auto &d : dim) d=float(input->readLong(2))/10.f;
          points.push_back(MWAWVec2f(dim[1],dim[0]));
          f << points.back() << ",";
        }
        f << "],";
        if (polyType&1 && points.size()>1)
          points.push_back(points[0]);
        if (polyType==0 || polyType==1)
          shape.m_vertices=points;
        else if (!points.empty()) {
          shape.m_path.push_back(MWAWGraphicShape::PathData('M', points[0]));
          for (size_t j=1; j+1<points.size(); ++j) {
            MWAWVec2f dir=points[j+1]-points[j-1];
            shape.m_path.push_back(MWAWGraphicShape::PathData('S', points[j], points[j]-0.1f*dir));
          }
          if (polyType==3)
            shape.m_path.push_back(MWAWGraphicShape::PathData('Z'));
        }
        listener->insertShape(position, shape, style);
      }
      else if (i==1 && dSz==4) {
        for (int j=0; j<2; ++j) { // 0,0
          val=int(input->readLong(2));
          if (val)
            f << "f" << j << "=" << val << ",";
        }
      }
      else {
        MWAW_DEBUG_MSG(("DrawTableParser::sendShape: can not read poly's zone %d\n", i));
        f << "###";
      }
    }
    else if (type==7) {
      MWAWEmbeddedObject obj;
      if (getBitmap(bitmapBox, obj, pos+2+dSz)) {
        listener->insertPicture(position, obj);
        input->seek(pos+2+dSz, librevenge::RVNG_SEEK_SET);
        continue;
      }
      f << "###";
    }
    else if (type==8) {
      if (i==0) { // normally must be a apple picture
        MWAWBox2f box;
        auto res = MWAWPictData::check(input, dSz, box);
        if (res == MWAWPict::MWAW_R_BAD) {
          MWAW_DEBUG_MSG(("DrawTableParser::sendShape:: can not find the picture\n"));
        }
        else {
          input->seek(pos+2, librevenge::RVNG_SEEK_SET);
          std::shared_ptr<MWAWPict> thePict(MWAWPictData::get(input, dSz));
          if (thePict) {
            MWAWEmbeddedObject picture;
            if (thePict->getBinary(picture))
              listener->insertPicture(position, picture);
          }
        }
      }
#ifdef DEBUG_WITH_FILES
      librevenge::RVNGBinaryData file;
      input->seek(pos+2, librevenge::RVNG_SEEK_SET);
      input->readDataBlock(dSz, file);
      static int volatile pictName = 0;
      libmwaw::DebugStream s;
      s << "PICT-" << ++pictName << (i==0 ? ".pct" : ".eps");
      libmwaw::Debug::dumpFile(file, s.str().c_str());
      ascii().skipZone(pos, pos+1+dSz);
#endif
      input->seek(pos+2+dSz, librevenge::RVNG_SEEK_SET);
      continue;
    }
    else if (type==9) {
      std::string text;
      for (int j=0; j<dSz; ++j) text+=char(input->readULong(1));
      f << text << ",";
      MWAWEntry entry;
      entry.setBegin(pos+2);
      entry.setLength(dSz);
      std::shared_ptr<MWAWSubDocument> doc(new DrawTableParserInternal::SubDocument(*this, getInput(), entry, font, para));
      listener->insertTextBox(position, doc, style);
    }
    if (input->tell()!=pos+2 && input->tell()!=pos+2+dSz) {
      f << "###extra";
      MWAW_DEBUG_MSG(("DrawTableParser::sendShape: find extra data in zone %d\n", i));
      ascii().addDelimiter(input->tell(),'|');
    }
    input->seek(pos+2+dSz, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool DrawTableParser::getBitmap(MWAWBox2i const &box, MWAWEmbeddedObject &obj, long endPos)
{
  auto input=getInput();
  long pos=input->tell();
  long dSz=endPos-pos;
  int Y=box.size()[1];
  if (Y==0 || dSz<0 || (dSz%Y)!=0) {
    MWAW_DEBUG_MSG(("DrawTableParser::getBitmap: unexpected bitmap size\n"));
    return false;
  }

  long width=dSz/Y;
  int X=box.size()[0];
  if (8*width<X) {
    MWAW_DEBUG_MSG(("DrawTableParser::getBitmap: unexpected bitmap size\n"));
    return false;
  }

  obj=MWAWEmbeddedObject();
  ascii().addPos(pos-2);
  ascii().addNote("Entries(Bitmap)");

  MWAWPictBitmapIndexed pict(box.size());
  std::vector<MWAWColor> colors;
  colors.push_back(MWAWColor::black());
  colors.push_back(MWAWColor::white());
  pict.setColors(colors);

  libmwaw::DebugStream f;
  for (int y=0; y<Y; ++y) {
    pos=input->tell();
    f.str("");
    f << "Bitmap-" << y << ":";

    int x=0;
    for (int w=0; w<width; ++w) {
      int val=int(input->readULong(1));
      for (int v=0, depl=0x80; v<8; ++v, depl>>=1) {
        if (x>=X)
          break;
        pict.set(x++,y, (val&depl) ? 0 : 1);
      }
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+width, librevenge::RVNG_SEEK_SET);
  }
  return pict.getBinary(obj);
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
