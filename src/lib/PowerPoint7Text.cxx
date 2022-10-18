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
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWParser.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPresentationListener.hxx"

#include "PowerPoint7Struct.hxx"

#include "PowerPoint7Text.hxx"
#include "PowerPoint7Parser.hxx"

/** Internal: the structures of a PowerPoint7Text */
namespace PowerPoint7TextInternal
{
//! Internal: a ruler of a PowerPoint7Text
struct Ruler {
  //! constructor
  Ruler()
    : m_paragraph()
  {
    for (auto &margin : m_margins) margin=0;
  }
  //! returns a paragraph corresponding to a level
  void updateParagraph(int level)
  {
    if (level<0 || level>4) {
      MWAW_DEBUG_MSG(("PowerPoint7TextInternal::Ruler::updateParagraph: the level %d seems bad\n", level));
      level=0;
    }
    m_paragraph.m_marginsUnit=librevenge::RVNG_POINT;
    m_paragraph.m_margins[0]=(double(m_margins[2*level+1])-m_margins[2*level+0])/8;
    m_paragraph.m_margins[1]=double(m_margins[2*level+0])/8;
  }
  //! the paragraph
  MWAWParagraph m_paragraph;
  //! the left/first margins * 5 (0: normal, 1-4: level)
  int m_margins[10];
};

//! Internal: a text zone of a PowerPoint7Text
struct TextZone {
  //! constructor
  TextZone()
    : m_textEntry()
    , m_rulerId(-1)
    , m_posToFontMap()
    , m_posToRulerMap()
    , m_posToFieldFormatMap()
  {
  }
  //! returns true if the zone contain no text
  bool isEmpty() const
  {
    return !m_textEntry.valid();
  }
  //! the list of text zone
  MWAWEntry m_textEntry;
  //! the ruler identifier
  int m_rulerId;
  //! a map position to font
  std::map<long,MWAWFont> m_posToFontMap;
  //! a map position to ruler
  std::map<long,Ruler> m_posToRulerMap;
  //! a map position to format
  std::map<long,int> m_posToFieldFormatMap;
};

////////////////////////////////////////
//! Internal: the state of a PowerPoint7Text
struct State {
  //! constructor
  State()
    : m_fontFamily("CP1252")
    , m_fileIdFontIdMap()
    , m_idToRulerMap()
    , m_fieldIdToFormatIdMap()
    , m_textZoneList()
  { }
  //! the basic pc font family if known
  std::string m_fontFamily;
  //! a local id to final id font map
  std::map<int,int> m_fileIdFontIdMap;
  //! a local id to ruler id  map
  std::map<int,Ruler> m_idToRulerMap;
  //! a field id to format id map
  std::map<int,int> m_fieldIdToFormatIdMap;
  //! the list of text zone
  std::vector<std::shared_ptr<TextZone> > m_textZoneList;
};
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint7Text::PowerPoint7Text(PowerPoint7Parser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new PowerPoint7TextInternal::State)
  , m_mainParser(&parser)
{
}

PowerPoint7Text::~PowerPoint7Text()
{ }

int PowerPoint7Text::version() const
{
  return m_parserState->m_version;
}

void PowerPoint7Text::setFontFamily(std::string const &family)
{
  m_state->m_fontFamily=family;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool PowerPoint7Text::readFontCollection(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2005) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontCollection: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(FontDef)[collection," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  std::string fName;
  int id;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2017: {
      done=m_mainParser->readIdentifier(level+1,endPos,id,"FontDef");
      if (!done || fName.empty()) break;
      if (m_state->m_fileIdFontIdMap.find(id)!=m_state->m_fileIdFontIdMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFontCollection: can not define font %d\n", id));
        break;
      }
      int fId;
      if (fName=="Monotype Sorts" || fName=="Wingdings")
        fId=m_parserState->m_fontConverter->getId(fName);
      else {
        std::string finalName("_");
        finalName+=fName;
        fId=m_parserState->m_fontConverter->getId(finalName, m_state->m_fontFamily);
      }
      m_state->m_fileIdFontIdMap[id]=fId;
      fName="";
      break;
    }
    case 2018:
      done=m_mainParser->readZoneNoData(level+1,endPos,"FontDef","id,end");
      break;
    case 4022: // StyleTextProp11Atom
      done=readFontContainer(level+1,endPos,fName);
      break;
    case 4042: // after readFontContainer
      done=m_mainParser->readZoneNoData(level+1,endPos,"FontDef","flags"); // some flags?
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFontCollection: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontCollection: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("FontDef:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Text::readTextZoneContainer(int level, long lastPos, PowerPoint7TextInternal::TextZone &zone)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2028) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZoneContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(TextZone)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2030:
      done=readTextZone(level+1,endPos,zone);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZoneContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZoneContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("TextZone:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Text::readTextZone(int level, long lastPos, PowerPoint7TextInternal::TextZone &zone)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2030) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long endPos=pos+16+header.m_dataSize;
  f << "Entries(TextZone)[" << level << "]:" << header;
  int val;
  switch (header.m_values[3]) {
  case 47: {
    if (header.m_dataSize%44) {
      MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: unexpected data size for zone=47\n"));
      f << "###,";
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    auto N=int(header.m_dataSize/44);
    long actC=0;
    int defaultId=m_parserState->m_fontConverter->getId(m_state->m_fontFamily.c_str());
    for (int fo=0; fo<N; ++fo) {
      pos=input->tell();
      f.str("");
      f << "TextZone-FS" << fo << "[font]:";
      auto nC=long(input->readULong(4));
      f << "nC=" << nC << ",";
      for (int i=0; i<6; ++i) {
        val=int(input->readLong(2));
        int const expected[]= {0xfe2, 0, 0, 0, 0x18, 0};
        if (val!=expected[i])
          f << "f" << i << "=" << val << ",";
      }
      f << "fl=[";
      for (int i=0; i<4; ++i) {
        val=int(input->readULong(1));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      auto fontId=int(input->readULong(2));
      MWAWFont font;
      if (m_state->m_fileIdFontIdMap.find(fontId)!=m_state->m_fileIdFontIdMap.end())
        font.setId(m_state->m_fileIdFontIdMap.find(fontId)->second);
      else {
        font.setId(defaultId);
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: can not find font %d\n", fontId));
        f << "fId=###" << "F" << fontId << ",";
      }
      val=int(input->readLong(2));
      if (val!=2) f << "f6=" << val << ",";
      val=int(input->readULong(2));
      if (val) font.setSize(float(val));
      val=int(input->readLong(2));
      if (val) f << "f7=" << val << ",";
      auto flag=int(input->readULong(2));
      uint32_t flags=0;
      if (flag&0x1) flags |= MWAWFont::boldBit;
      if (flag&0x2) flags |= MWAWFont::italicBit;
      if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
      if (flag&0x8) flags |= MWAWFont::embossBit;
      if (flag&0x10) flags |= MWAWFont::shadowBit;
      if (flag&0x80) f << "fl4,";
      if (flag&0x200) flags |= MWAWFont::embossBit;
      // flag&0x400 super, flag&0x800 sub
      flag&=0xF160;
      if (flag) f << "##flag=" << std::hex << flag << std::dec << ",";// find 4
      font.setFlags(flags);
      unsigned char col[4];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
      MWAWColor color=MWAWColor::black();
      if (col[3]==0xfe)
        color=MWAWColor(col[0],col[1],col[2]);
      else if (!m_mainParser->getColor(col[3],color)) {
        f << "##color[id]=" << int(col[3]) << ",";
      }
      if (!color.isBlack())
        font.setColor(color);
      val=int(input->readLong(2)); // 12|47|62
      if (val) f << "g0=" << val << ",";
      val=int(input->readLong(4));
      if (val) font.set(MWAWFont::Script(float(val),librevenge::RVNG_PERCENT,58));
      for (int i=0; i<2; ++i) { // 0
        val=int(input->readLong(2));
        if (val) f << "g" << i+1 << "=" << val << ",";
      }
      f << font.getDebugString(m_parserState->m_fontConverter);

      if (zone.m_posToFontMap.find(actC)!=zone.m_posToFontMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: a font already exists for pos=%ld\n", actC));
        f << "###,";
      }
      else
        zone.m_posToFontMap[actC]=font;
      actC+=nC;
      input->seek(pos+44, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    return true;
  }
  case 48: {
    if (header.m_dataSize%72) {
      MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: unexpected data size for zone=48\n"));
      f << "###,";
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    auto N=int(header.m_dataSize/72);
    int defaultId=m_parserState->m_fontConverter->getId(m_state->m_fontFamily.c_str());
    PowerPoint7TextInternal::Ruler defaultRuler;
    if (m_state->m_idToRulerMap.find(zone.m_rulerId)!=m_state->m_idToRulerMap.end())
      defaultRuler=m_state->m_idToRulerMap.find(zone.m_rulerId)->second;
    else {
      f << "##R" << zone.m_rulerId << ",";
      MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: can not find ruler %d\n", zone.m_rulerId));
    }
    long actC=0;
    for (int r=0; r<N; ++r) {
      pos=input->tell();
      f.str("");
      f << "TextZone-R" << r << "[ruler]:";
      auto nC=int(input->readULong(4));
      f << "nChar=" << nC << ",";
      auto ruler=defaultRuler;
      MWAWParagraph &para=ruler.m_paragraph;
      for (int i=0; i<6; ++i) {
        val=int(input->readLong(2));
        int const expected[]= {0xfe3, 0, 0, 0, 0x34, 0};
        if (val!=expected[i])
          f << "f" << i << "=" << val << ",";
      }
      f << "fl=[";
      for (int i=0; i<4; ++i) {
        val=int(input->readULong(1));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      // bullet
      char bulletChar=0;
      MWAWColor bulletColor;
      MWAWFont bulletFont;
      bulletFont.setId(defaultId);
      auto bulletFlags=int(input->readULong(2));
      if (bulletFlags&1) {
        f << "useBullet[";
        if (bulletFlags&4) f << "font,";
        if (bulletFlags&0x10) f << "color,";
        if (bulletFlags&0x40) f << "size,";
        f << "],";
        if (bulletFlags&0xffaa) f << "bullet[flag]=" << std::hex << (bulletFlags&0xffaa) << std::dec << ",";
      }
      unsigned char col[4];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
      if (col[3]==0xfe)
        bulletColor=MWAWColor(col[0],col[1],col[2]);
      else if ((bulletFlags&0x11)==0x11 && col[3]!=0xfd && !m_mainParser->getColor(col[3],bulletColor)) {
        f << "##color[id]=" << int(col[3]) << ",";
      }
      if (!bulletColor.isBlack()) {
        bulletFont.setColor(bulletColor);
        f << "bullet[color]=" << bulletColor << ",";
      }
      for (int i=0; i<9; ++i) { // f8=-1|2-4,f10=small number
        val=int(input->readLong(2));
        int const expected[]= {0,-1,2,100,0,0,0,0,0};
        if (val==expected[i]) continue;
        switch (i) {
        case 0:
          if ((bulletFlags&1)==0) break;
          bulletChar=char(val);
          f << "bullet[char]=" << bulletChar << ",";
          break;
        case 1:
          if ((bulletFlags&5)!=5) break;
          if (m_state->m_fileIdFontIdMap.find(val)!=m_state->m_fileIdFontIdMap.end())
            bulletFont.setId(m_state->m_fileIdFontIdMap.find(val)->second);
          else {
            MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: can not find font %d\n", val));
            f << "###";
          }
          f << "fId[bullet]=F" << val << ",";
          break;
        case 3:
          if ((bulletFlags&0x41)!=0x41) break;
          f << "bullet[size]=" << val << "%,";
          bulletFont.setSize(float(val)/100.f, true);
          break;
        default:
          f << "f" << i+8 << "=" << val << ",";
          break;
        }
      }

      auto levl=int(input->readULong(1));
      if (levl) f << "level=" << levl << ",";
      ruler.updateParagraph(levl);
      if (bulletChar) {
        para.m_listLevelIndex=(levl>=0 && levl<=4) ? levl+1 : 1;
        para.m_listLevel=MWAWListLevel();
        para.m_listLevel->m_type=MWAWListLevel::BULLET;
        para.m_listLevel->m_spanId=m_parserState->m_fontManager->getId(bulletFont);
        int unicode=m_parserState->m_fontConverter->unicode(bulletFont.id(), static_cast<unsigned char>(bulletChar));
        libmwaw::appendUnicode(unicode==-1 ? 0x2022 : uint32_t(unicode), para.m_listLevel->m_bullet);
      }
      val=int(input->readULong(1)); // 0|1|10|30|33 junk?
      if (val) f << "fl3=" << std::hex << val << std::dec << ",";
      for (int i=0; i<3; ++i) {
        val=int(input->readLong(2));
        if (i==1) {
          switch (val) {
          case 0: // left
            break;
          case 1:
            para.m_justify=MWAWParagraph::JustificationCenter;
            f << "center,";
            break;
          case 2:
            para.m_justify=MWAWParagraph::JustificationRight;
            f << "right,";
            break;
          case 3:
            para.m_justify=MWAWParagraph::JustificationFull;
            f << "justify,";
            break;
          default:
            MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: find unknown justifcation\n"));
            f << "##justify=" << val << ",";
          }
        }
        else if (val)
          f << "g" << i << "=" << val << ",";
      }
      for (int i=0; i<3; ++i) {
        val=int(input->readLong(4));
        char const *wh[]= {"interline","before","after"};
        if (val<0) {
          f << wh[i] << "=" << -int64_t(val) << "pt,";
          if (val + std::numeric_limits<int>::max() < 0) {
            f << "###";
            MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: find bad spacing\n"));
          }
          else if (i==0)
            para.setInterline(-val, librevenge::RVNG_POINT);
          else
            para.m_spacings[i]=-double(val)/72.;
        }
        else if ((i==0 && val!=100) || (i!=0 && val)) {
          if (i==0)
            para.setInterline(double(val)/100., librevenge::RVNG_PERCENT);
          else
            para.m_spacings[i]=double(val)/100.*24./72.; // percent assume font=24
          f << wh[i] << "=" << val << "%,";
        }
      }
      for (int i=0; i<4; ++i) {
        val=int(input->readLong(2));
        if (val!=(i==3 ? 1 : 0))
          f << "g" << i+3 << "=" << val << ",";
      }
      if (zone.m_posToRulerMap.find(actC)!=zone.m_posToRulerMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: a ruler already exists for pos=%ld\n", actC));
        f << "###,";
      }
      else
        zone.m_posToRulerMap[actC]=ruler;
      actC+=nC;
      input->seek(pos+72, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    return true;
  }
  case 49: {
    if (header.m_dataSize%24) {
      MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: unexpected data size for zone=49\n"));
      f << "###,";
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    auto N=int(header.m_dataSize/24);
    long actC=0;
    for (int fl=0; fl<N; ++fl) {
      pos=input->tell();
      f.str("");
      f << "TextZone-F" << fl << "[field]:";
      auto nC=int(input->readULong(4));
      f << "nChar=" << nC << ",";
      for (int i=0; i<6; ++i) {
        val=int(input->readLong(2));
        int const expected[]= {0xfe1, 0, 0, 0, 0x4, 0};
        if (val!=expected[i])
          f << "f" << i << "=" << val << ",";
      }
      f << "fl=[";
      for (int i=0; i<4; ++i) {
        val=int(input->readULong(1));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      val=int(input->readLong(4));
      if (val!=-1 && m_state->m_fieldIdToFormatIdMap.find(val)!=m_state->m_fieldIdToFormatIdMap.end()) {
        int format=m_state->m_fieldIdToFormatIdMap.find(val)->second;
        f << "FS" << format << ",";
        if (zone.m_posToFieldFormatMap.find(actC)!=zone.m_posToFieldFormatMap.end()) {
          MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: a fieldFormat already exists for pos=%ld\n", actC));
          f << "###,";
        }
        else
          zone.m_posToFieldFormatMap[actC]=format;
      }
      else if (val!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: can not find format\n"));
        f << "##FS=" << val << ",";
      }
      actC+=nC;
      input->seek(pos+24, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    return true;
  }
  case 53: {
    if (zone.m_textEntry.valid()) {
      MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: oops a text entry is already defined\n"));
      f << "##duplicated,";
    }
    else {
      zone.m_textEntry.setBegin(input->tell());
      zone.m_textEntry.setLength(header.m_dataSize);
    }
    std::string text;
    for (long i=0; i<header.m_dataSize; ++i)
      text+=char(input->readULong(1));
    f << text << ",";
    break;
  }
  default:
    if (!header.m_dataSize)
      break;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextZone: unexpected data for zone=%d\n", header.m_values[3]));
    f << "###,";
    break;
  }
  if (input->tell()!=endPos)
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PowerPoint7Text::readTextMasterProp(int level, long lastPos, int &tId)
{
  tId=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4002) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterProp: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(TextMasterProp)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  int rId=-1;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4003:
      done=readTextMasterPropAtom(level+1,endPos);
      break;
    case 4021:
      done=readRulerSetId(level+1,endPos,rId);
      break;
    case 4051:
      done=readExternalHyperlinkAtom(level+1,endPos);
      break;
    case 4055:
      done=readExternalHyperlinkData(level+1,endPos);
      break;
    case 4064: {
      if (tId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterProp: the text id is already set\n"));
      }
      done=readZone4064(level+1,endPos,rId,tId);
      break;
    }
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterProp: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterProp: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("MasterTextPropAtom:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (tId==-1) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterProp: can not find Zone4064\n"));
  }
  return true;
}

bool PowerPoint7Text::readTextMasterPropAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4003) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterPropAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(TextMasterProp)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x24) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readTextMasterPropAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  int val;
  f << "unkn=[";
  for (int i=0; i<6; ++i) { // f0=0-6
    val=int(input->readULong(2));
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  for (int i=0; i<4; ++i) { // f0=-32768|-1680
    val=int(input->readLong(2));
    int const expected[]= {-32768,-1,0,0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  f << "box=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Text::readRulerSetId(int level, long lastPos,int &rId)
{
  rId=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4021) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerSetId: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Ruler)[setId," << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerSetId: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    rId=int(input->readULong(4));
    f << "R" << rId << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readFontContainer(int level, long lastPos, std::string &fName)
{
  fName="";
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4022) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(FontDef)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4023:
      done=readFont(level+1,endPos,fName);
      break;
    case 4024:
      done=readFontEmbedded(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFontContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("FontDef:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (fName.empty()) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontContainer: can not find the font name\n"));
  }
  return true;
}

bool PowerPoint7Text::readFont(int level, long lastPos, std::string &fName)
{
  fName="";
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4023) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFont: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(FontDef)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x3c) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFont: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  for (int i=0; i<14; ++i) { // f8=109|2bc,f10=0|ff|100,f11=0|200,f13=XX00
    auto val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<32; ++i) {
    auto c=char(input->readULong(1));
    if (!c) break;
    fName+=c;
  }
  f << fName << ",";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}
bool PowerPoint7Text::readFontEmbedded(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4024) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontEmbedded: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(FontDef)[embedded," << level << "]:" << header;
  static bool first=true;
  if (first) {
    first=false;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFontEmbedded: reading embedded font is not implemented\n"));
  }
  if (header.m_dataSize)
    ascFile.addDelimiter(pos+16,'|');
  input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readExternalHyperlinkAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4051) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlinkAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalHyperlink)[atom," << level << "]:" << header;
  if (header.m_dataSize!=12) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlinkAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<6; ++i) { // f0=1,f4=0|16
      auto val=int(input->readULong(2));
      if (val)
        f << "f"<< i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readExternalHyperlinkData(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4055) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlinkData: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalHyperlink)[data," << level << "]:" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlinkData: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) {
      long val=input->readLong(4);
      if (val)
        f << "id"<< i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readZone4064(int level, long lastPos, int rId, int &tId)
{
  tId=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4064) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4064: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone4064B)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  std::shared_ptr<PowerPoint7TextInternal::TextZone> zone(new PowerPoint7TextInternal::TextZone);
  zone->m_rulerId=rId;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2028:
      done=readTextZoneContainer(level+1,endPos,*zone);
      break;
    case 2030:
      done=readTextZone(level+1,endPos,*zone);
      break;
    case 4013:
      done=m_mainParser->readZoneNoData(level+1,endPos,"Zone4064B","flags");
      break;
    case 4066:
      done=readZone4066(level+1,endPos);
      break;
    case 4067:
      done=readZone4067(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4064: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4064: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Zone4064B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (!zone->isEmpty()) {
    tId=int(m_state->m_textZoneList.size());
    m_state->m_textZoneList.push_back(zone);
  }
  else
    tId=-2;
  return true;
}

bool PowerPoint7Text::readZone4066(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4066) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4066: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone4066B)[" << level << "]:" << header;
  if (header.m_dataSize!=0x18) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4066: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  int val;
  for (int i=0; i<6; ++i) { // f0=id?, f1=2, f2=fSz, f4=1|200
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) { // fl1=100|300, fl2=6200: color?
    val=int(input->readULong(2));
    if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<4; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << i+4 << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readZone4067(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4067) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4067: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone4067B)[" << level << "]:" << header;
  if (header.m_dataSize!=0x34) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readZone4067: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(1));
    if (val)
      f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<11; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0,-768,0x95,-1,2,100,0,0,0,0,0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<14; ++i) {
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readExternalHyperlink9(int level, long lastPos, int &tId)
{
  tId=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4068) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlink9: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalHyperlnk9)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  int rId=-1;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4003:
      done=readTextMasterPropAtom(level+1,endPos);
      break;
    case 4021:
      done=readRulerSetId(level+1,endPos,rId);
      break;
    case 4064:
      if (tId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlink9: find a duplicated Zone4064\n"));
      }
      done=readZone4064(level+1,endPos,rId,tId);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlink9: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlink9: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("ExternalHyperlink9:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (tId==-1) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readExternalHyperlink9: can not find Zone4064\n"));
  }
  return true;
}

bool PowerPoint7Text::readRulerList(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4016) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Ruler)[list," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  PowerPoint7TextInternal::Ruler ruler;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2017: {
      int id;
      done=m_mainParser->readIdentifier(level+1,endPos,id,"Ruler");
      if (!done) break;
      if (m_state->m_idToRulerMap.find(id)!=m_state->m_idToRulerMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerList: find dupplicated id\n"));
      }
      else
        m_state->m_idToRulerMap[id]=ruler;
      ruler=PowerPoint7TextInternal::Ruler();
      break;
    }
    case 2018:
      done=m_mainParser->readZoneNoData(level+1,endPos,"Ruler","id,end");
      break;
    case 4043:
      done=m_mainParser->readZoneNoData(level+1,endPos,"Ruler","flags"); // flags?
      break;
    case 4069:
      done=readRulerContainer(level+1,endPos,ruler);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerList: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerList: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Ruler:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Text::readRuler(int level, long lastPos, PowerPoint7TextInternal::Ruler &ruler)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4019) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRuler: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Ruler)[" << level << "]:" << header;
  if (header.m_dataSize!=0x34) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRuler: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  f << "margins=[";
  for (auto &margin : ruler.m_margins) {
    margin=int(input->readLong(4));
    f << margin << ",";
  }
  f << "],";
  for (int i=0; i<6; ++i) {
    auto val=int(input->readLong(2));
    int const expected[]= {3,0,576,0,0,0};
    if (val==expected[i]) continue;
    if (i==4)
      f << "num[ruler]=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Text::readRulerContainer(int level, long lastPos, PowerPoint7TextInternal::Ruler &ruler)
{
  ruler=PowerPoint7TextInternal::Ruler();
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4069) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Ruler)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4019:
      done=readRuler(level+1,endPos,ruler);
      break;
    case 4070:
      done=readRulerTabs(level+1,endPos,ruler);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Ruler:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Text::readRulerTabs(int level, long lastPos, PowerPoint7TextInternal::Ruler &ruler)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4070) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerTabs: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Ruler)[tabs," << level << "]:" << header;
  if (header.m_dataSize%8) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerTabs: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  auto N=int(header.m_dataSize/8);
  f << "tabs=[";
  for (int i=0; i<N; ++i) {
    MWAWTabStop tab;
    tab.m_position=double(input->readLong(4))/8./72.;
    auto val=int(input->readLong(4));
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
      MWAW_DEBUG_MSG(("PowerPoint7Text::readRulerTabs: find unknown tab position\n"));
      f << "##tab" << i << "=" << val << ",";
      break;
    }
    f << tab << ",";
    ruler.m_paragraph.m_tabs->push_back(tab);
  }
  f << "],";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Text::readFieldList(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2027) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Field)[list" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  int format=-1;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2017: {
      int id;
      done=m_mainParser->readIdentifier(level+1,endPos,id,"Field");
      if (!done || format==-1) break;
      if (m_state->m_fieldIdToFormatIdMap.find(id)!=m_state->m_fieldIdToFormatIdMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldList: can not store field %d\n", id));
      }
      else
        m_state->m_fieldIdToFormatIdMap[id]=format;
      format=-1;
      break;
    }
    case 2018:
      done=m_mainParser->readZoneNoData(level+1,endPos,"Field","id,end");
      break;
    case 4056:
      if (format!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldList: find unused format %d\n", format));
      }
      done=readFieldDef(level+1,endPos,format);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldList: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldList: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Field:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Text::readFieldDef(int level, long lastPos, int &format)
{
  format=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4056) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldDef: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Field)[def," << level << "]:" << header;
  if (header.m_dataSize!=2) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::readFieldDef: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    format=int(input->readULong(2));
    if (format>=0 && format<=16) {
      char const *wh[]= {
        "title,", "%m/%d/%y", "%A, %d %B, %Y", "%d %B, %Y", "%B %d, %Y",
        "%d-%b-%y","%B, %y","%m-%y","%m/%d/%y %H:%M","%m/%d/%y %I:%M:%S %p",
        "%H:%M", "%H:%M:%S", "%I:%M %p", "%I:%M:%S %p", "header",
        "footer", "page[number]"
      };
      f << wh[format] << ",";
    }
    else
      f << "##FS" << format << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool PowerPoint7Text::sendText(int textId)
{
  MWAWListenerPtr listener=m_parserState->m_presentationListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::sendText: can not find the listener\n"));
    return false;
  }
  if (textId<0 || textId>=int(m_state->m_textZoneList.size()) || !m_state->m_textZoneList[size_t(textId)]) {
    MWAW_DEBUG_MSG(("PowerPoint7Text::sendText: can not find the text zone %d\n", textId));
    return false;
  }
  PowerPoint7TextInternal::TextZone const &zone=*m_state->m_textZoneList[size_t(textId)];
  if (!zone.m_textEntry.valid())
    return true;
  MWAWInputStreamPtr input=m_parserState->m_input;
  input->seek(zone.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=zone.m_textEntry.end();
  int fId=m_parserState->m_fontConverter->getId(m_state->m_fontFamily.c_str());
  MWAWFont font;
  font.setId(fId);
  listener->setFont(font);
  long actPosC=0;
  while (!input->isEnd()) {
    if (input->tell()>=endPos) break;
    auto c=static_cast<unsigned char>(input->readULong(1));
    if (zone.m_posToRulerMap.find(actPosC)!=zone.m_posToRulerMap.end())
      listener->setParagraph(zone.m_posToRulerMap.find(actPosC)->second.m_paragraph);
    if (zone.m_posToFontMap.find(actPosC)!=zone.m_posToFontMap.end())
      listener->setFont(zone.m_posToFontMap.find(actPosC)->second);
    if (zone.m_posToFieldFormatMap.find(actPosC)!=zone.m_posToFieldFormatMap.end()) {
      if (c!='*') {
        MWAW_DEBUG_MSG(("PowerPoint7Text::sendText: find odd character for char %d\n", int(c)));
      }
      int format=zone.m_posToFieldFormatMap.find(actPosC)->second;
      if (format==16)
        listener->insertField(MWAWField(MWAWField::PageNumber));
      else if (format>=1 && format<=13) {
        char const *wh[]= {
          "", "%m/%d/%y", "%A, %d %B, %Y", "%d %B, %Y", "%B %d, %Y",
          "%d-%b-%y","%B, %y","%m-%y","%m/%d/%y %H:%M","%m/%d/%y %I:%M:%S %p",
          "%H:%M", "%H:%M:%S", "%I:%M %p", "%I:%M:%S %p"
        };
        MWAWField field(format<=9 ? MWAWField::Date : MWAWField::Time);
        field.m_DTFormat = wh[format];
        listener->insertField(field);
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint7Text::sendText: unsure how to insert format %d\n", format));
        listener->insertCharacter('#');
      }
      ++actPosC;
      continue;
    }
    ++actPosC;
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
    default:
      listener->insertCharacter(c);
      break;
    }
  }
  return true;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
