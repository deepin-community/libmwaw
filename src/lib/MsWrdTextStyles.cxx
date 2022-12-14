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
#include <map>

#include "MWAWTextListener.hxx"
#include "MWAWSection.hxx"

#include "MsWrdParser.hxx"
#include "MsWrdStruct.hxx"
#include "MsWrdText.hxx"

#include "MsWrdTextStyles.hxx"

/** Internal: the structures of a MsWrdTextStyles */
namespace MsWrdTextStylesInternal
{
////////////////////////////////////////
//! Internal: the state of a MsWrdTextStylesInternal
struct State {
  //! constructor
  State()
    : m_version(-1)
    , m_defaultFont(2,12)
    , m_nextStyleMap()
    , m_fontList()
    , m_paragraphList()
    , m_sectionList()
    , m_textstructParagraphList()
    , m_styleFontMap()
    , m_styleParagraphMap()
  {
  }
  //! the file version
  int m_version;

  //! the default font ( NewYork 12pt)
  MWAWFont m_defaultFont;

  //! a map styleId to next styleId
  std::map<int,int> m_nextStyleMap;

  //! the list of fonts
  std::vector<MsWrdStruct::Font> m_fontList;

  //! the list of paragraph
  std::vector<MsWrdStruct::Paragraph> m_paragraphList;

  //! the list of section
  std::vector<MsWrdStruct::Section> m_sectionList;

  //! the list of paragraph in textstruct
  std::vector<MsWrdStruct::Paragraph> m_textstructParagraphList;

  //! the list of fonts in style
  std::map<int, MsWrdStruct::Font> m_styleFontMap;

  //! the list of paragraph in style
  std::map<int, MsWrdStruct::Paragraph> m_styleParagraphMap;
};
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MsWrdTextStyles::MsWrdTextStyles(MsWrdText &textParser)
  : m_parserState(textParser.getParserState())
  , m_state(new MsWrdTextStylesInternal::State)
  , m_mainParser(textParser.m_mainParser)
  , m_textParser(&textParser)
{
}

MsWrdTextStyles::~MsWrdTextStyles()
{ }

int MsWrdTextStyles::version() const
{
  if (m_state->m_version < 0)
    m_state->m_version = m_parserState->m_version;
  return m_state->m_version;
}

MWAWFont const &MsWrdTextStyles::getDefaultFont() const
{
  return m_state->m_defaultFont;
}

////////////////////////////////////////////////////////////
// try to read a font
////////////////////////////////////////////////////////////
bool MsWrdTextStyles::readFont(MsWrdStruct::Font &font, MsWrdTextStyles::ZoneType type)
{
  bool mainZone = type==TextZone;
  libmwaw::DebugStream f;

  MWAWInputStreamPtr &input= m_parserState->m_input;
  long pos = input->tell();
  auto sz = static_cast<int>(input->readULong(1));
  if (sz > 20 || sz == 3) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  if (sz == 0) return true;

  auto flag = static_cast<int>(input->readULong(1));
  uint32_t flags = font.m_font->flags();
  if (flag&0x80) flags ^= MWAWFont::boldBit;
  if (flag&0x40) flags ^= MWAWFont::italicBit;
  if (flag&0x20) {
    if (font.m_font->getStrikeOut().m_style==MWAWFont::Line::Simple)
      font.m_font->setStrikeOutStyle(MWAWFont::Line::None);
    else
      font.m_font->setStrikeOutStyle(MWAWFont::Line::Simple);
  }
  if (flag&0x10) flags ^= MWAWFont::outlineBit;
  if (flag&0x8) flags ^= MWAWFont::shadowBit;
  if (flag&0x4) flags ^= MWAWFont::smallCapsBit;
  if (flag&0x2) flags ^= MWAWFont::uppercaseBit;
  if (flag&0x1) flags ^= MWAWFont::hiddenBit;

  int what = 0;
  /*  01: horizontal decal, 2: vertical decal, 4; underline, 08: fSize,  10: set font, 20: font color, 40: ???(maybe reset)
  */
  if (sz >= 2) what = static_cast<int>(input->readULong(1));

  if (sz >= 4) {
    auto fId = static_cast<int>(input->readULong(2));
    if (fId) {
      if (mainZone && (what & 0x50)==0) f << "#fId,";
      font.m_font->setId(fId);
    }
    else if (what & 0x10) {
    }
    what &= 0xEF;
  }
  else if (what & 0x10) {
  }
  if (sz >= 5) {
    float fSz = float(input->readULong(1))/2.0f;
    if (fSz>0) {
      if (mainZone && (what & 0x48)==0) f << "#fSz,";
      font.m_font->setSize(fSz);
    }
    what &= 0xF7;
  }

  if (sz >= 6) {
    auto decal = static_cast<int>(input->readLong(1)); // unit point
    if (decal) {
      if (what & 0x2)
        font.m_font->set(MWAWFont::Script(float(decal)/2.f, librevenge::RVNG_POINT));
      else
        f << "#vDecal=" << decal;
    }
    what &= 0xFD;
  }
  if (sz >= 7) {
    auto decal = static_cast<int>(input->readLong(1)); // unit point > 0 -> expand < 0: condensed
    if (decal) {
      if ((what & 0x1) == 0) f << "#hDecal=" << decal <<",";
      else font.m_font->setDeltaLetterSpacing(float(decal)/16.0f);
    }
    what &= 0xFE;
  }

  if (sz >= 8) {
    auto val = static_cast<int>(input->readULong(1));
    if (val & 0xF0) {
      if (what & 0x20) {
        MWAWColor col;
        if (m_mainParser->getColor((val>>4),col))
          font.m_font->setColor(col);
        else
          f << "#fColor=" << (val>>4) << ",";
      }
      else
        f << "#fColor=" << (val>>4) << ",";
    }
    what &= 0xDF;

    if (val && (what & 0x4)) {
      auto style=MWAWFont::Line::Simple;
      switch ((val>>1)&0x7) {
      case 4:
        style=MWAWFont::Line::Dot;
        break;
      case 3:
        font.m_font->setUnderlineType(MWAWFont::Line::Double);
        break;
      case 2:
        font.m_font->setUnderlineWordFlag(true);
        break;
      case 1:
        break;
      default:
        f << "#underline=" << ((val>>1) &0x7) << ",";
        break;
      }
      if (font.m_font->getUnderline().m_style==style)
        style=MWAWFont::Line::None;
      font.m_font->setUnderlineStyle(style);
      what &= 0xFB;
    }
    else if (val & 0xe)
      f << "#underline?=" << ((val>>1) &0x7) << ",";
    if (val & 0xF1)
      f << "#underline[unkn]=" << std::hex << (val & 0xF1) << std::dec << ",";
  }
  if (what & 0x20) {
    font.m_font->setColor(MWAWColor::black());
    what &= 0xDF;
  }
  if (what & 0x4) {
    font.m_font->setUnderlineStyle(MWAWFont::Line::None);
    what &= 0xFB;
  }
  if (what & 0x2) {
    font.m_font->set(MWAWFont::Script(0, librevenge::RVNG_POINT));
    what &= 0xFD;
  }
  if (what & 0x1) {
    font.m_font->setDeltaLetterSpacing(0);
    what &= 0xFE;
  }
  font.m_unknown =what;
  font.m_font->setFlags(flags);

  bool ok = false;
  if (mainZone && sz >= 10 && sz <= 12) {
    auto wh = static_cast<int>(input->readULong(1));
    long pictPos = 0;
    for (int i = 10; i < 13; i++) {
      pictPos <<= 8;
      if (i <= sz) pictPos += input->readULong(1);
    }
    long actPos = input->tell();
    if (m_mainParser->checkPicturePos(pictPos, wh)) {
      ok = true;
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      font.m_picturePos = pictPos;
      f << "pictWh=" << wh << ",";
    }
    else
      input->seek(pos+1+8, librevenge::RVNG_SEEK_SET);
  }
  if (!ok && sz >= 9) {
    auto wh = static_cast<int>(input->readLong(1));
    switch (wh) {
    case -1:
      ok = true;
      break;
    case 0: // line height ?
      if (sz < 10) break;
      font.m_size=float(input->readULong(1))/2.f;
      ok = true;
      break;
    default:
      break;
    }
  }
  if (!ok && sz >= 9) {
    input->seek(pos+1+8, librevenge::RVNG_SEEK_SET);
    f << "#";
  }
  if (long(input->tell()) != pos+1+sz)
    m_parserState->m_asciiFile.addDelimiter(input->tell(), '|');

  input->seek(pos+1+sz, librevenge::RVNG_SEEK_SET);
  font.m_extra = f.str();
  return true;
}

bool MsWrdTextStyles::getFont(ZoneType type, int id, MsWrdStruct::Font &font)
{
  MsWrdStruct::Font *fFont = nullptr;
  switch (type) {
  case TextZone:
    if (id < 0 || id >= int(m_state->m_fontList.size()))
      break;
    fFont = &m_state->m_fontList[size_t(id)];
    break;
  case StyleZone:
    if (m_state->m_styleFontMap.find(id) == m_state->m_styleFontMap.end())
      break;
    fFont = &m_state->m_styleFontMap.find(id)->second;
    break;
  case TextStructZone:
  case InParagraphDefinition:
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("MsWrdTextStyles::getFont: do not know how to send this type of font\n"));
    return false;
  }
  if (!fFont) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::getFont: can not find font with %d[type=%d]\n", id, int(type)));
    return false;
  }
  int fId = font.m_font->id();
  float fSz = font.m_font->size();
  font = *fFont;
  if (font.m_font->id() < 0)
    font.m_font->setId(fId);
  if (font.m_font->size() <= 0)
    font.m_font->setSize(fSz);
  return true;
}

void MsWrdTextStyles::setProperty(MsWrdStruct::Font const &font)
{
  if (!m_parserState->m_textListener) return;
  MsWrdStruct::Font tmp = font;
  if (tmp.m_font->id() < 0) tmp.m_font->setId(m_state->m_defaultFont.id());
  if (tmp.m_font->size() <= 0) tmp.m_font->setSize(m_state->m_defaultFont.size());
  tmp.updateFontToFinalState();
  m_parserState->m_textListener->setFont(*tmp.m_font);
}

////////////////////////////////////////////////////////////
// read/send the paragraph zone
////////////////////////////////////////////////////////////
bool MsWrdTextStyles::getParagraph(ZoneType type, int id, MsWrdStruct::Paragraph &para)
{
  switch (type) {
  case TextZone:
    if (id < 0 || id >= int(m_state->m_paragraphList.size()))
      break;
    para = m_state->m_paragraphList[size_t(id)];
    return true;
  case StyleZone:
    if (m_state->m_styleParagraphMap.find(id) == m_state->m_styleParagraphMap.end())
      break;
    para = m_state->m_styleParagraphMap.find(id)->second;
    return true;
  case TextStructZone:
    if (id < 0 || id >= int(m_state->m_textstructParagraphList.size()))
      break;
    para = m_state->m_textstructParagraphList[size_t(id)];
    return true;
  case InParagraphDefinition:
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("MsWrdTextStyles::getParagraph: do not know how to send this type of font\n"));
    return false;
  }

  MWAW_DEBUG_MSG(("MsWrdTextStyles::getParagraph: can not find paragraph with %d[type=%d]\n", id, int(type)));
  return false;
}

void MsWrdTextStyles::sendDefaultParagraph()
{
  if (!m_parserState->m_textListener) return;
  m_parserState->m_textListener->setParagraph(MsWrdStruct::Paragraph(version()));
}

bool MsWrdTextStyles::readParagraph(MsWrdStruct::Paragraph &para, int dataSz)
{
  int sz;
  MWAWInputStreamPtr &input= m_parserState->m_input;
  if (dataSz >= 0)
    sz = dataSz;
  else
    sz = static_cast<int>(input->readULong(2));

  long pos = input->tell();
  long endPos = pos+sz;

  if (sz == 0) return true;
  if (!input->checkPosition(endPos)) return false;

  int const vers = version();
  libmwaw::DebugStream f;
  int numFont=0;
  while (long(input->tell()) < endPos) {
    long actPos = input->tell();
    /* 5-16: basic paragraph properties
       75-84: basic section properties
       other
     */
    if (para.read(input,endPos)) continue;
    input->seek(actPos, librevenge::RVNG_SEEK_SET);

    int wh = static_cast<int>(input->readULong(1)), val;
    if (vers <= 3 && wh >= 0x36 && wh <= 0x45) {
      // this section data has different meaning in v3 and after...
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
    bool done = false;
    long dSz = endPos-actPos;
    switch (wh) {
    case 0:
      done = (actPos+1==endPos||(dataSz==2 && actPos+2==endPos));
      break;
    case 0x38:
      if (dSz < 4) break;
      val = static_cast<int>(input->readLong(1));
      if (val != 2) f << "#shadType=" <<  val << ",";
      f << "shad=" << float(input->readLong(2))/100.f << "%,";
      done = true;
      break;
    case 0x3a: // checkme: maybe plain
      f << "f3a,";
      done = true;
      break;
    case 0x4d: {
      if (dSz < 2) break;
      val = static_cast<int>(input->readLong(1));
      para.m_modFont->m_font->set(MWAWFont::Script(float(val)/2.f,librevenge::RVNG_POINT));
      if (val==0) f << "pos[y]=0,";
      done = true;
      break;
    }
    case 0x3c: // bold
    case 0x3d: // italic?
    case 0x3e: // strikeout (chekme)
    case 0x3f: // outline (chekme)
    case 0x40: // shadow (chekme)
    case 0x41: // small caps (chekme)
    case 0x42: // all caps (chekme)
    case 0x43: // hidden (chekme)
    case 0x45: // underline
    case 0x4a: {
      if (dSz < 2) break;
      done = true;
      val = static_cast<int>(input->readULong(1));
      if (wh == 0x4a) {
        if (val > 4 && val < 40)
          para.m_modFont->m_font->setSize(float(val)/2.0f);
        else
          f << "#fSize=" << val << ",";
        break;
      }
      switch (wh) {
      case 0x3c:
      case 0x3d:
      case 0x3e:
      case 0x3f:
      case 0x40:
      case 0x41:
      case 0x42:
      case 0x43:
        para.m_modFont->m_flags[wh-0x3c]=val;
        break;
      case 0x45:
        para.m_modFont->m_flags[8]=val;
        break;
      default:
        break;
      }
      break;
    }
    case 0x44:
      if (dSz < 3) break;
      done = true;
      val = static_cast<int>(input->readULong(2));
      para.m_modFont->m_font->setId(val);
      break;
    case 0x2: // a small number between 0 and 4
    case 0x34: // 0 ( one time)
    case 0x39: // 0 ( one time)
    case 0x47: // 0 one time
    case 0x49: // 0 ( one time)
    case 0x4c: // 0, 6, -12
    case 0x5e: // 0
      if (dSz < 2) break;
      done = true;
      val = static_cast<int>(input->readLong(1));
      f << "f" << std::hex << wh << std::dec << "=" << val << ",";
      break;
    case 0x23: // alway 0 ?
      if (dSz < 3) break;
      done = true;
      val = static_cast<int>(input->readLong(2));
      f << "f" << std::hex << wh << std::dec << "=" << val << ",";
      break;
    case 0x9f: // two small number: table range?
      if (dSz < 3) break;
      done = true;
      f << "f" << std::hex << wh << "=[";
      for (int i = 0; i < 2; i++)
        f << input->readULong(1) << ",";
      f << std::dec << "],";
      break;
    case 0x50: // two small number
      if (dSz < 4) break;
      done = true;
      f << "f" << std::hex << wh << std::dec << "=[";
      f << input->readLong(1) << ",";
      f << input->readLong(2) << ",";
      f << "],";
      break;
    case 0x4f: // a small int and a pos?
      if (dSz < 4) break;
      done = true;
      f << "f" << std::hex << wh << std::dec << "=[";
      f << input->readLong(1) << ",";
      f << std::hex << input->readULong(2) << std::dec << "],";
      break;
    case 0x9e: // two small number + a pos?
      if (dSz < 5) break;
      done = true;
      f << "f" << std::hex << wh << std::dec << "=[";
      for (int i = 0; i < 2; i++)
        f << input->readLong(1) << ",";
      f << std::hex << input->readULong(2) << std::dec << "],";
      break;
    case 0x4e:
    case 0x53: { // same as 4e ( new format )
      // checkme: sometimes, we can have a list of font, do we need to
      // use only the last one?
      if (numFont++)
        f << "#font" << numFont-1 << "=["
          << para.m_font->m_font->getDebugString(m_parserState->m_fontConverter)
          << "," << *para.m_font << "],";
      done = true;
      para.m_font = MsWrdStruct::Font();
      if (!readFont(*para.m_font, InParagraphDefinition) || long(input->tell()) > endPos) {
        done = false;
        f << "#";
        break;
      }
      break;
    }
    case 0x5f: { // 4 index
      if (dSz < 10) break;
      done = true;
      sz = static_cast<int>(input->readULong(1));
      if (sz != 8) f << "#sz=" << sz << ",";
      f << "f5f=[";
      for (int i = 0; i < 4; i++) f << input->readLong(2) << ",";
      f << "],";
      break;
    }
    case 0x94: // checkme space between column divided by 2 (in table) ?
      if (dSz < 3) break;
      done = true;
      val = static_cast<int>(input->readLong(2));
      f << "colSep[table]=" << 2*val/1440. << ",";
      break;
    default:
      break;
    }
    if (!done) {
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
  }
  if (long(input->tell()) != endPos) {
    static bool first = true;
    if (first) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readParagraph: can not read end of paragraph\n"));
      first = false;
    }
    libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
    ascFile.addDelimiter(input->tell(),'|');
    f << "####";
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  para.m_extra += f.str();

  return true;
}

////////////////////////////////////////////////////////////
// read the char/parag plc
////////////////////////////////////////////////////////////
bool MsWrdTextStyles::readPLCList(MsWrdEntry &entry)
{
  if (entry.length() < 10 || (entry.length()%6) != 4) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLCList: the zone size seems odd\n"));
    return false;
  }
  long pos = entry.begin();
  entry.setParsed(true);
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << entry.type() << ":";
  auto N=int(entry.length()/6);
  std::vector<long> textPos; // limit of the text in the file
  textPos.resize(size_t(N)+1);
  for (auto &tPos : textPos) tPos = long(input->readULong(4));
  int const expectedSize = (version() <= 3) ? 0x80 : 0x200;
  for (int i = 0; i < N; i++) {
    if (!input->checkPosition(textPos[size_t(i)])) f << "#";

    auto defPos = long(input->readULong(2));
    f << std::hex << "[filePos?=" << textPos[size_t(i)] << ",dPos=" << defPos << std::dec << ",";
    f << "],";

    MsWrdEntry plc;
    plc.setType(entry.id() ? "ParagPLC" : "CharPLC");
    plc.setId(i);
    plc.setBegin(defPos*expectedSize);
    plc.setLength(expectedSize);
    if (!input->checkPosition(plc.end())) {
      f << "#PLC,";
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLCList: plc def is outside the file\n"));
    }
    else {
      long actPos = input->tell();
      MWAWVec2<long> fLimit(textPos[size_t(i)], textPos[size_t(i)+1]);
      readPLC(plc, entry.id(), fLimit);
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
    }
  }
  f << std::hex << "end?=" << textPos[size_t(N)] << ",";

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  return true;
}

bool MsWrdTextStyles::readPLC(MsWrdEntry &entry, int type, MWAWVec2<long> const &fLimit)
{
  int const vers = version();
  int const expectedSize = (vers <= 3) ? 0x80 : 0x200;
  int const posFactor = (vers <= 3) ? 1 : 2;
  if (entry.length() != expectedSize) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: the zone size seems odd\n"));
    return false;
  }
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  auto N=static_cast<int>(input->readULong(1));
  if (N==0 || 5*(N+1) > entry.length()) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: the number of plc seems odd\n"));
    return false;
  }

  long pos = entry.begin();
  entry.setParsed(true);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Entries("<< entry.type() << ")[" << entry.id() << "]:N=" << N << ",";

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  std::vector<long> filePos;
  filePos.resize(size_t(N)+1);
  for (auto &fPos : filePos) fPos = long(input->readULong(4));
  if (filePos[0] != fLimit[0]) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: bad first limit\n"));
    return false;
  }
  std::map<int, int> mapPosId;
  std::vector<int> decal;
  decal.resize(size_t(N));
  size_t numData = type == 0 ? m_state->m_fontList.size() :
                   m_state->m_paragraphList.size();
  auto plcType = type == 0 ? MsWrdText::PLC::Font : MsWrdText::PLC::Paragraph;
  auto &plcMap = m_textParser->getFilePLCMap();

  for (size_t i = 0; i < size_t(N); i++) {
    decal[i] = static_cast<int>(input->readULong(1));
    int id = -1;
    if (decal[i]) {
      if (mapPosId.find(decal[i]) != mapPosId.end())
        id = mapPosId.find(decal[i])->second;
      else {
        id = int(numData++);
        mapPosId[decal[i]] = id;

        long actPos = input->tell();
        libmwaw::DebugStream f2;
        f2 << entry.type() << "-";

        long dataPos = entry.begin()+posFactor*decal[i];
        if (type == 0) {
          input->seek(dataPos, librevenge::RVNG_SEEK_SET);
          f2 << "F" << id << ":";
          MsWrdStruct::Font font;
          if (!readFont(font, TextZone)) {
            font = MsWrdStruct::Font();
            f2 << "#";
          }
          else
            f2 << font.m_font->getDebugString(m_parserState->m_fontConverter) << font << ",";
          m_state->m_fontList.push_back(font);
        }
        else {
          MsWrdStruct::Paragraph para(vers);
          f2 << "P" << id << ":";

          input->seek(dataPos, librevenge::RVNG_SEEK_SET);
          auto sz = static_cast<int>(input->readLong(1));
          long endPos;
          if (vers <= 3) {
            sz++;
            endPos = dataPos+sz;
          }
          else
            endPos = dataPos+2*sz+1;
          if (sz < 4 || endPos > entry.end()) {
            MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: can not read plcSz\n"));
            f2 << "#";
          }
          else {
            auto stId = static_cast<int>(input->readLong(1));
            if (m_state->m_styleParagraphMap.find(stId)==m_state->m_styleParagraphMap.end()) {
              MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: can not find parent paragraph\n"));
              f2 << "#";
            }
            else
              para.m_styleId = stId;
            f2 << "sP" << stId << ",";
            if (vers > 3) {
              if (!para.m_info->read(input, endPos, vers)) {
                f2 << "###info,";
                input->seek(dataPos+2+6, librevenge::RVNG_SEEK_SET);
              }
              // osnole: do we need to check here if the paragraph is empty ?
              // ie. if (para.m_info->isEmpty()&&stId==0)
            }
            else {   // always 0 ?
              auto val = static_cast<int>(input->readLong(2));
              if (val) f << "g0=" << val << ",";
            }
            if (sz >= 4) {
              ascFile.addDelimiter(input->tell(),'|');
              if (readParagraph(para, int(endPos-input->tell()))) {
#ifdef DEBUG_WITH_FILES
                para.print(f2, m_parserState->m_fontConverter);
#endif
              }
              else {
                para = MsWrdStruct::Paragraph(vers);
                f2 << "#";
              }
            }
          }
          m_state->m_paragraphList.push_back(para);
        }
        input->seek(actPos, librevenge::RVNG_SEEK_SET);
        ascFile.addPos(dataPos);
        ascFile.addNote(f2.str().c_str());
      }
    }
    f << std::hex << filePos[i] << std::dec;
    MsWrdText::PLC plc(plcType, id);
    plcMap.insert(std::multimap<long,MsWrdText::PLC>::value_type
                  (filePos[type == 0 ? i : i+1], plc));
    if (id >= 0) {
      if (type==0) f << ":F" << id;
      else f << ":P" << id;
    }
    f << ",";
  }

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  if (filePos[size_t(N)] != fLimit[1]) {
    if (input->isEnd()) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readPLC: we are already at the end of the stream\n"));
      return false;
    }
    MsWrdEntry nextEntry(entry);
    nextEntry.setBegin(entry.begin()+expectedSize);
    MWAWVec2<long> newLimit(filePos[size_t(N)], fLimit[1]);
    readPLC(nextEntry,type,newLimit);
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the text structure
////////////////////////////////////////////////////////////
bool MsWrdTextStyles::readTextStructList(MsWrdEntry &entry)
{
  if (entry.length() < 19) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readTextStructList: the zone seems to short\n"));
    return false;
  }
  int const vers = version();
  long pos = entry.begin();
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  auto type = static_cast<int>(input->readLong(1));
  if (type != 1 && type != 2) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readTextStructList: find odd type %d\n", type));
    return false;
  }

  int num = 0;
  while (type == 1) {
    /* probably a paragraph definition. Fixme: create a function */
    auto length = static_cast<int>(input->readULong(2));
    long endPos = pos+3+length;
    if (endPos > entry.end()) {
      ascFile.addPos(pos);
      ascFile.addNote("TextStruct[paragraph]#");
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readTextStructList: zone(paragraph) is too big\n"));
      return false;
    }
    f.str("");
    f << "ParagPLC:tP" << num++<< "]:";
    MsWrdStruct::Paragraph para(vers);
    input->seek(-2,librevenge::RVNG_SEEK_CUR);
    if (readParagraph(para) && long(input->tell()) <= endPos) {
#ifdef DEBUG_WITH_FILES
      para.print(f, m_parserState->m_fontConverter);
#endif
    }
    else {
      para = MsWrdStruct::Paragraph(vers);
      f << "#";
    }
    m_state->m_textstructParagraphList.push_back(para);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);

    pos = input->tell();
    type = static_cast<int>(input->readULong(1));
    if (type == 2) break;
    if (type != 1) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readTextStructList: find odd type %d\n", type));
      return false;
    }
  }
  input->seek(-1,librevenge::RVNG_SEEK_CUR);
  return true;
}

int MsWrdTextStyles::readPropertyModifier(bool &complex, std::string &extra)
{
  MWAWInputStreamPtr &input= m_parserState->m_input;
  long pos = input->tell();
  auto c = static_cast<int>(input->readULong(1));
  complex = false;
  if (c&0x80) { // complex data, let get the id
    complex=true;
    return ((c&0x7F)<<8)|static_cast<int>(input->readULong(1));
  }
  if (c==0) {
    input->seek(pos+2, librevenge::RVNG_SEEK_SET);
    return -1;
  }
  int id = -1;
  libmwaw::DebugStream f;
  MsWrdStruct::Paragraph para(version());
  input->seek(-1, librevenge::RVNG_SEEK_CUR);
  if (readParagraph(para, 2)) {
    id = int(m_state->m_textstructParagraphList.size());
    m_state->m_textstructParagraphList.push_back(para);
#ifdef DEBUG_WITH_FILES
    f << "[";
    para.print(f, m_parserState->m_fontConverter);
    f << "]";
#endif
  }
  else {
    input->seek(pos+1, librevenge::RVNG_SEEK_SET);
    f << "#f" << std::hex << c << std::dec << "=" << static_cast<int>(input->readULong(1));
  }
  extra = f.str();
  input->seek(pos+2, librevenge::RVNG_SEEK_SET);
  return id;
}

////////////////////////////////////////////////////////////
// read/send the section zone
////////////////////////////////////////////////////////////
bool MsWrdTextStyles::getSection(ZoneType type, int id, MsWrdStruct::Section &section)
{
  switch (type) {
  case TextZone:
    if (id < 0 || id >= int(m_state->m_sectionList.size()))
      break;
    section = m_state->m_sectionList[size_t(id)];
    return true;
  case StyleZone:
  case TextStructZone:
  case InParagraphDefinition:
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("MsWrdTextStyles::getSection do not know how to get this type of section\n"));
    return false;
  }
  MWAW_DEBUG_MSG(("MsWrdTextStyles::getSection can not find this section\n"));
  return false;
}

bool MsWrdTextStyles::getSectionParagraph(ZoneType type, int id, MsWrdStruct::Paragraph &para)
{
  MsWrdStruct::Section sec;
  if (!getSection(type, id, sec)) return false;
  if (!sec.m_paragraphId.isSet()) return false;
  return getParagraph(StyleZone, *sec.m_paragraphId, para);
}

bool MsWrdTextStyles::getSectionFont(ZoneType type, int id, MsWrdStruct::Font &font)
{
  MsWrdStruct::Section sec;
  if (!getSection(type, id, sec)) return false;

  if (!sec.m_paragraphId.isSet()) return false;
  MsWrdStruct::Paragraph para(version());
  if (!getParagraph(StyleZone, *sec.m_paragraphId, para))
    return false;

  if (para.m_font.isSet())
    font = para.m_font.get();
  else
    return false;
  return true;
}


bool MsWrdTextStyles::readSection(MsWrdEntry &entry, std::vector<long> &cLimits)
{
  if (entry.length() < 14 || (entry.length()%10) != 4) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readSection: the zone size seems odd\n"));
    return false;
  }
  long pos = entry.begin();
  entry.setParsed(true);
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Section:";
  auto N=size_t(entry.length()/10);
  cLimits.resize(N+1);
  for (auto &limit : cLimits) limit = long(input->readULong(4));

  MsWrdText::PLC plc(MsWrdText::PLC::Section);
  auto &plcMap = m_textParser->getTextPLCMap();
  long textLength = m_textParser->getMainTextLength();
  for (size_t i = 0; i < N; i++) {
    MsWrdStruct::Section sec;
    sec.m_type = static_cast<int>(input->readULong(1)); // 0|40|80|C0
    sec.m_flag = static_cast<int>(input->readULong(1)); // number between 2 and 7
    sec.m_id = int(i);
    unsigned long filePos = input->readULong(4);
    if (textLength && cLimits[i] > textLength) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readSection: text positions is bad...\n"));
      f << "#";
    }
    else {
      plc.m_id = int(i);
      plcMap.insert(std::multimap<long,MsWrdText::PLC>::value_type(cLimits[i],plc));
    }
    f << std::hex << "cPos=" << cLimits[i] << ":[" << sec << ",";
    if (filePos != 0xFFFFFFFFL) {
      f << "pos=" << std::hex << filePos << std::dec << ",";
      long actPos = input->tell();
      readSection(sec,long(filePos));
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
    }
    f << "],";

    m_state->m_sectionList.push_back(sec);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  return true;
}

bool MsWrdTextStyles::readSection(MsWrdStruct::Section &sec, long debPos)
{
  MWAWInputStreamPtr &input= m_parserState->m_input;
  if (!input->checkPosition(debPos)) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readSection: can not find section data...\n"));
    return false;
  }
  int const vers = version();
  input->seek(debPos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  auto sz = static_cast<int>(input->readULong(1));
  long endPos = debPos+sz+1;
  if (sz < 1 || sz >= 255) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readSection: data section size seems bad...\n"));
    f << "Section-" << sec.m_id.get() << ":#" << sec;
    ascFile.addPos(debPos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  while (input->tell() < endPos) {
    long pos = input->tell();
    bool ok;
    if (vers <= 3)
      ok = sec.readV3(input, endPos);
    else
      ok = sec.read(input, endPos);
    if (ok) continue;
    f << "#";
    ascFile.addDelimiter(pos,'|');
    break;
  }
  f << "Section-S" << sec.m_id.get() << ":" << sec;
  ascFile.addPos(debPos);
  ascFile.addNote(f.str().c_str());

  ascFile.addPos(endPos);
  ascFile.addNote("_");
  return true;
}

void MsWrdTextStyles::setProperty(MsWrdStruct::Section const &sec)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) return;
  if (listener->isHeaderFooterOpened()) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::setProperty: can not open a section in header/footer\n"));
  }
  else {
    int numCols = sec.m_col.get();
    int actCols = listener->getSection().numColumns();
    if (numCols >= 1 && actCols > 1 && sec.m_colBreak.get()) {
      if (!listener->isSectionOpened()) {
        MWAW_DEBUG_MSG(("MsWrdTextStyles::setProperty: section is not opened\n"));
      }
      else
        listener->insertBreak(MWAWTextListener::ColumnBreak);
    }
    else {
      if (listener->isSectionOpened())
        listener->closeSection();
      listener->openSection(sec.getSection(m_mainParser->getPageWidth()));
    }
  }
}

bool MsWrdTextStyles::sendSection(int id, int textStructId)
{
  if (!m_parserState->m_textListener) return true;

  if (id < 0 || id >= int(m_state->m_sectionList.size())) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::sendText: can not find new section\n"));
    return false;
  }
  auto section=m_state->m_sectionList[size_t(id)];
  MsWrdStruct::Paragraph para(version());
  if (textStructId >= 0 &&
      getParagraph(MsWrdTextStyles::TextStructZone, textStructId, para) &&
      para.m_section.isSet()) {
    section.insert(*para.m_section);
  }
  setProperty(section);
  return true;
}

////////////////////////////////////////////////////////////
// read the styles
////////////////////////////////////////////////////////////
std::map<int,int> const &MsWrdTextStyles::getNextStyleMap() const
{
  return m_state->m_nextStyleMap;
}

bool MsWrdTextStyles::readStyles(MsWrdEntry &entry)
{
  if (entry.length() < 6) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readStyles: zone seems to short...\n"));
    return false;
  }
  m_state->m_styleFontMap.clear();
  m_state->m_styleParagraphMap.clear();
  m_state->m_nextStyleMap.clear();
  entry.setParsed(true);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  long pos = entry.begin();
  libmwaw::DebugStream f;
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << entry << ":";
  auto N = static_cast<int>(input->readLong(2));
  if (N) f << "N?=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  // first find the different zone
  long debPos[4];
  int overOk[3]= {0,30,100}; // name, font, paragraph
  for (int st = 0; st < 3; st++) {
    debPos[st] = input->tell();
    auto dataSz = static_cast<int>(input->readULong(2));
    long endPos = debPos[st]+dataSz;
    if (dataSz < 2+N || endPos > entry.end()+overOk[st]) {
      ascFile.addPos(pos);
      ascFile.addNote("###Styles(bad)");
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStyles: can not read styles(%d)...\n", st));
      return false;
    }
    if (endPos > entry.end()) {
      entry.setEnd(endPos+1);
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStyles(%d): size seems incoherent...\n", st));
      f.str("");
      f << "#sz=" << dataSz << ",";
      ascFile.addPos(debPos[st]);
      ascFile.addNote(f.str().c_str());
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  debPos[3] = input->tell();
  // read the styles parents
  std::vector<int> orig, order;
  if (readStylesHierarchy(entry, N, orig))
    order=orderStyles(orig);

  int N1=0;
  MsWrdEntry zone;
  zone.setBegin(debPos[0]);
  zone.setEnd(debPos[1]);
  if (!readStylesNames(zone, N, N1)) {
    N1=int(orig.size())-N;
    if (N1 < 0)
      return false;
  }
  // ok, repair orig, and order if need
  if (int(orig.size()) < N+N1)
    orig.resize(size_t(N+N1), -1000);
  if (int(order.size()) < N+N1) {
    for (auto i = int(order.size()); i < N+N1; i++)
      order.push_back(i);
  }
  zone.setBegin(debPos[1]);
  zone.setEnd(debPos[2]);
  readStylesFont(zone, N, orig, order);

  zone.setBegin(debPos[2]);
  zone.setEnd(debPos[3]);
  readStylesParagraph(zone, N, orig, order);
  return true;
}

bool MsWrdTextStyles::readStylesNames(MsWrdEntry const &zone, int N, int &Nnamed)
{
  long pos = zone.begin();
  MWAWInputStreamPtr &input= m_parserState->m_input;
  if (!zone.valid() || !input || !input->checkPosition(zone.end())) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesNames: zone(names) seems to short...\n"));
    return false;
  }
  input->seek(pos+2, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Styles(names):";
  int actN=0;
  while (long(input->tell()) < zone.end()) {
    auto sz = static_cast<int>(input->readULong(1));
    if (sz == 0) {
      f << "*";
      actN++;
      continue;
    }
    if (sz == 0xFF) {
      f << "_";
      actN++;
      continue;
    }
    pos = input->tell();
    if (pos+sz > zone.end()) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesNames: zone(names) seems to short...\n"));
      f << "#";
      ascFile.addNote(f.str().c_str());
      input->seek(pos-1, librevenge::RVNG_SEEK_SET);
      break;
    }
    std::string s("");
    for (int i = 0; i < sz; i++) s += char(input->readULong(1));
    f << "N" << actN-N << "=" ;
    f << s << ",";
    actN++;
  }
  Nnamed=actN-N;
  if (Nnamed < 0) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesNames: zone(names) seems to short: stop...\n"));
    f << "#";
  }
  ascFile.addPos(zone.begin());
  ascFile.addNote(f.str().c_str());
  return Nnamed >= 0;
}

bool MsWrdTextStyles::readStylesFont
(MsWrdEntry &zone, int N, std::vector<int> const &previous, std::vector<int> const &order)
{
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  long pos = zone.begin();
  ascFile.addPos(pos);
  ascFile.addNote("Styles(font):");

  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos+2, librevenge::RVNG_SEEK_SET);
  size_t numElt = order.size();
  std::vector<long> debPos;
  std::vector<int> dataSize;
  debPos.resize(numElt, 0);
  dataSize.resize(numElt, 0);
  for (size_t i = 0; i < numElt; i++) {
    pos = input->tell();
    debPos[i] = pos;
    int sz = dataSize[i] = static_cast<int>(input->readULong(1));
    if (sz == 0xFF)
      sz = 0;
    else if (pos+1+sz > zone.end()) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesFont: can not read a font\n"));
      if (i == 0)
        return false;
      numElt = i-1;
      break;
    }
    if (sz)
      input->seek(sz, librevenge::RVNG_SEEK_CUR);
    else {
      f.str("");
      f << "CharPLC(sF" << int(i)-N << "):";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
  }

  for (auto id : order) {
    if (id < 0 || id >= int(numElt)) continue;
    int prevId = previous[size_t(id)];
    MsWrdStruct::Font font;
    // osnola:what is the difference between dataSize[(size_t)id]=0|0xFF
    if (prevId >= 0 && m_state->m_styleFontMap.find(prevId-N) != m_state->m_styleFontMap.end())
      font = m_state->m_styleFontMap.find(prevId-N)->second;
    if (dataSize[size_t(id)] && dataSize[size_t(id)] != 0xFF) {
      input->seek(debPos[size_t(id)], librevenge::RVNG_SEEK_SET);

      f.str("");
      f << "CharPLC(sF" << id-int(N) << "):";
      if (!readFont(font, StyleZone)) f << "#";
      else if (id==int(N)) m_state->m_defaultFont=font.m_font.get();
      f << "font=[" << font.m_font->getDebugString(m_parserState->m_fontConverter) << font << "],";
      ascFile.addPos(debPos[size_t(id)]);
      ascFile.addNote(f.str().c_str());
    }
    m_state->m_styleFontMap.insert(std::map<int,MsWrdStruct::Font>::value_type(id-N,font));
  }
  return true;
}

bool MsWrdTextStyles::readStylesParagraph(MsWrdEntry &zone, int N, std::vector<int> const &previous,
    std::vector<int> const &order)
{
  int const vers=version();
  int minSz = vers <= 3 ? 3 : 7;
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  long pos = zone.begin();
  ascFile.addPos(pos);
  ascFile.addNote("Styles(paragraph):");

  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos+2, librevenge::RVNG_SEEK_SET);
  size_t numElt = order.size();
  std::vector<long> debPos;
  std::vector<int> dataSize;
  debPos.resize(numElt, 0);
  dataSize.resize(numElt, 0);
  for (size_t i = 0; i < numElt; i++) {
    pos = input->tell();
    debPos[i] = pos;
    int sz = dataSize[i] = static_cast<int>(input->readULong(1));
    if (sz != 0xFF && pos+1+sz > zone.end()) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesParagraph: can not read a paragraph\n"));
      if (i == 0)
        return false;
      numElt = i-1;
      break;
    }
    if (sz && sz != 0xFF)
      input->seek(sz, librevenge::RVNG_SEEK_CUR);
    else {
      f.str("");
      f << "ParagPLC(sP" << int(i)-N << "):";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
  }
  for (auto id : order) {
    if (id < 0 || id >= int(numElt)) continue;
    int prevId = previous[size_t(id)];
    MsWrdStruct::Paragraph para(vers);
    if (prevId >= 0 && m_state->m_styleParagraphMap.find(prevId-N) != m_state->m_styleParagraphMap.end())
      para = m_state->m_styleParagraphMap.find(prevId-N)->second;
    /** osnola: update the font style here or after reading data ? */
    if (m_state->m_styleFontMap.find(id-N) != m_state->m_styleFontMap.end())
      para.m_font = m_state->m_styleFontMap.find(id-N)->second;
    if (dataSize[size_t(id)] != 0xFF) {
      f.str("");
      f << "ParagPLC(sP" << id-N << "):";
      if (dataSize[size_t(id)] < minSz) {
        MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesParagraph: zone(paragraph) the id seems bad...\n"));
        f << "#";
      }
      else {
        input->seek(debPos[size_t(id)]+1, librevenge::RVNG_SEEK_SET);
        auto pId = static_cast<int>(input->readLong(1));
        if (id >= N && pId != id-N) {
          MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesParagraph: zone(paragraph) the id seems bad...\n"));
          f << "#id=" << pId << ",";
        }
        auto val = static_cast<int>(input->readLong(2)); // always 0?
        if (val) f << "g0=" << val << ",";

        if (vers > 3) {
          for (int j = 1; j < 3; j++) { // 0|90|c0,0|1
            val = static_cast<int>(input->readLong(2));
            if (val) f << "g" << j << "=" << std::hex << val << std::dec << ",";
          }
        }

        if (dataSize[size_t(id)] != minSz && !readParagraph(para, dataSize[size_t(id)]-minSz))
          f << "#";
#ifdef DEBUG_WITH_FILES
        para.print(f, m_parserState->m_fontConverter);
#endif
      }
      ascFile.addPos(debPos[size_t(id)]);
      ascFile.addNote(f.str().c_str());
    }
    para.m_modFont.setSet(false);
    m_state->m_styleParagraphMap.insert
    (std::map<int,MsWrdStruct::Paragraph>::value_type(id-N,para));
  }
  return true;
}

bool MsWrdTextStyles::readStylesHierarchy(MsWrdEntry &entry, int N, std::vector<int> &orig)
{
  MWAWInputStreamPtr &input= m_parserState->m_input;
  long pos = input->tell();
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  f << "Styles(hierarchy):";

  auto N2 = static_cast<int>(input->readULong(2));
  if (N2 < N) {
    MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesHierarchy: N seems too small...\n"));
    f << "#N=" << N2 << ",";
    ascFile.addPos(pos);
    ascFile.addNote("Styles(hierarchy):#"); // big problem
    return false;
  }
  if (pos+(N2+1)*2 > entry.end()) {
    if (N2>40) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::readStylesHierarchy: N seems very big...\n"));
      ascFile.addPos(pos);
      ascFile.addNote("Styles(hierarchy):#"); // big problem
    }
    f << "#";
  }
  orig.resize(0);
  orig.resize(size_t(N2), -1000);
  for (int i = 0; i < N2; i++) {
    auto v0 = static_cast<int>(input->readLong(1)); // often 0 or i-N
    auto v1 = static_cast<int>(input->readLong(1));
    f << "prev(sP"<< i-N << ")";
    if (v1 == -34) {
    }
    else if (v1 < -N || v1+N >= N2)
      f << "=###" << v1;
    else {
      orig[size_t(i)] = v1+N;
      f << "=sP" << v1;
    }
    if (v0 < -N || v0+N >= N2) {
      f << "[###next" << v0 << "]";
      m_state->m_nextStyleMap[i-N]=i-N;
    }
    else {
      m_state->m_nextStyleMap[i-N]=v0;
      if (v0==i-N)
        f << "*";
      else if (v0)
        f << "[next" << v0 << "]";
    }
    f << ",";
  }

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();
  if (pos < entry.end()) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
  }
  else if (pos > entry.end())
    entry.setEnd(pos);

  return true;
}

std::vector<int> MsWrdTextStyles::orderStyles(std::vector<int> const &previous)
{
  std::vector<int> order, numChild;
  size_t N = previous.size();
  numChild.resize(N, 0);
  for (auto id : previous) {
    if (id == -1000) continue;
    if (id < 0 || id >= int(N)) {
      MWAW_DEBUG_MSG(("MsWrdTextStyles::orderStyles: find a bad previous %d\n", id));
      continue;
    }
    numChild[size_t(id)]++;
  }
  order.resize(N);
  size_t numElt = 0;
  while (numElt < N) {
    bool read = false;
    for (size_t i = 0; i < N; i++) {
      if (numChild[i]) continue;
      order[N-(++numElt)]=int(i);
      if (previous[i] >= 0 && previous[i] < int(N))
        numChild[size_t(previous[i])]--;
      read = true;
      numChild[i]=-1;
    }
    if (read) continue;
    MWAW_DEBUG_MSG(("MsWrdTextStyles::orderStyles: find a loop, stop...\n"));
    for (size_t i = 0; i < N; i++) {
      if (numChild[i] != -1)
        order[N-(++numElt)]=int(i);
    }
    break;
  }
  return order;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
