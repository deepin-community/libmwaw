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

#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "StyleParser.hxx"

/** Internal: the structures of a StyleParser */
namespace StyleParserInternal
{
//! small structure used to store picture data in StyleParser
struct Picture {
  //! constructor
  Picture()
    : m_size(0,0)
    , m_entry()
  {
  }
  //! returns true if the picture is valid
  bool valid() const
  {
    return m_entry.valid();
  }
  //! the picture size in point
  MWAWVec2i m_size;
  //! the picture entry
  MWAWEntry m_entry;
};

//! small structure used to store a font and a picture
struct Font {
  //! constructor
  Font()
    : m_font()
    , m_picture()
  {
  }
  //! returns true if the picture is valid
  bool hasPicture() const
  {
    return m_picture.valid();
  }
  //! the font
  MWAWFont m_font;
  //! the picture
  Picture m_picture;
};

////////////////////////////////////////
//! Internal: the state of a StyleParser
struct State {
  //! constructor
  State()
    : m_entryMap()
    , m_unicodeChar(false)
    , m_textEntry()
    , m_backgroundColor(MWAWColor::white())
    , m_fontIdToFinalIdList()
    , m_fontList()
    , m_paragraphList()
    , m_posFontIdMap()
    , m_posParagraphIdMap()
    , m_posPictureMap()
    , m_actPage(0)
    , m_numPages(0)
  {
  }
  //! the map type to entry
  std::map<std::string, MWAWEntry> m_entryMap;
  //! true if the character are unicode character
  bool m_unicodeChar;
  //! the text entry
  MWAWEntry m_textEntry;
  //! the background color
  MWAWColor m_backgroundColor;
  //! list of font final id
  std::vector<int> m_fontIdToFinalIdList;
  //! the list of font style
  std::vector<Font> m_fontList;
  //! the list of paragraph style
  std::vector<MWAWParagraph> m_paragraphList;
  //! a map pos->font id
  std::map<long,int> m_posFontIdMap;
  //! a map pos->paragraphId
  std::map<long,int> m_posParagraphIdMap;
  //! a map pos->pictEntry
  std::map<long,Picture> m_posPictureMap;
  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
StyleParser::StyleParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state(new StyleParserInternal::State)
{
  getPageSpan().setMargins(0.1);
}

StyleParser::~StyleParser()
{
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void StyleParser::newPage(int number)
{
  if (number <= m_state->m_actPage || number > m_state->m_numPages)
    return;

  while (m_state->m_actPage < number) {
    m_state->m_actPage++;
    if (!getTextListener() || m_state->m_actPage == 1)
      continue;
    getTextListener()->insertBreak(MWAWTextListener::PageBreak);
  }
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void StyleParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open("main-1");
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendText(m_state->m_textEntry, m_state->m_unicodeChar);
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("StyleParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void StyleParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("StyleParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  int numPages=m_state->m_textEntry.valid() ? computeNumPages(m_state->m_textEntry, m_state->m_unicodeChar) : 1;
  m_state->m_numPages = numPages;
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(m_state->m_numPages+1);
  if (!m_state->m_backgroundColor.isWhite())
    ps.setBackgroundColor(m_state->m_backgroundColor);
  std::vector<MWAWPageSpan> pageList(1,ps);
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool StyleParser::createZones()
{
  if (!readTypeEntryMap())
    return false;
  libmwaw::DebugStream f;
  auto &entryMap = m_state->m_entryMap;

  // basic data
  auto it = entryMap.find("vers");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readVersion(entry);
  }
  it = entryMap.find("bgcl");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readBackgroundColor(entry);
  }
  it = entryMap.find("marg");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readMargins(entry);
  }
  it = entryMap.find("prec");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readPrintInfo(entry);
  }
  it = entryMap.find("stat");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readStat(entry);
  }
  it = entryMap.find("tabw");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readTabWidth(entry);
  }
  bool findTextEntry=false;
  it = entryMap.find("text");
  if (it != entryMap.end()) {
    findTextEntry=true;
    MWAWEntry const &entry = it++->second;
    entry.setParsed(true);
    m_state->m_unicodeChar=false;
    m_state->m_textEntry=entry;
  }
  it = entryMap.find("utxt");
  if (it != entryMap.end()) {
    findTextEntry=true;
    MWAWEntry const &entry = it++->second;
    entry.setParsed(true);
    m_state->m_unicodeChar=true;
    m_state->m_textEntry=entry;
  }

  // font

  //  v1.9
  it = entryMap.find("cfor");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readFormats(entry);
  }
  //   or v1.6
  it = entryMap.find("font");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readFontCorr(entry);
  }
  it = entryMap.find("fntb");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readFontNames(entry);
  }
  it = entryMap.find("styl");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readStyleTable(entry);
  }
  // v1.6 or v1.9
  it = entryMap.find("runa");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readPLCs(entry, false);
  }

  // para
  it = entryMap.find("rule");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readRules(entry);
  }
  it = entryMap.find("para");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readPLCs(entry, true);
  }

  // image
  it = entryMap.find("soup");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readPictures(entry);
  }

  // extra
  it = entryMap.find("xprc");
  if (it != entryMap.end()) {
    MWAWEntry const &entry = it++->second;
    readExtraProperties(entry);
  }

  // other
  for (auto iter : entryMap) {
    MWAWEntry const &entry=iter.second;
    if (entry.isParsed()) continue;
    f.str("");
    f << "Entries(" << entry.type() << "):";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }
  return m_state->m_textEntry.valid() || findTextEntry;
}

bool StyleParser::readTypeEntryMap()
{
  MWAWInputStreamPtr input = getInput();
  if (!input->checkPosition(16)) return false;
  input->seek(8, librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(ZoneList):";
  int val;
  auto N=static_cast<int>(input->readLong(2));
  f << "N=" << N+1 << ",";
  if (!input->checkPosition(16+16*(N+1))) return false;
  for (int i=0; i<3; ++i) { //  always 0
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  m_state->m_entryMap.clear();
  bool ok=false;
  for (int i=0; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << "ZoneList-" << i << ":";
    std::string wh("");
    for (int c=0; c<4; ++c) wh+=char(input->readULong(1));
    f << wh << ",";
    if (wh=="text" || wh=="utxt") ok=true;
    for (int j=0; j<2; ++j) {
      // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    MWAWEntry entry;
    entry.setType(wh);
    entry.setBegin(long(input->readLong(4)));
    entry.setLength(long(input->readLong(4)));
    f << std::hex << entry.begin() << "<->" << entry.end() << ",";
    if (!entry.length()) {
      // ok if the document is empty
      if (wh=="text" || wh=="utxt")
        m_state->m_entryMap[wh]=entry;
    }
    else if (entry.begin()<16+16*(N+1) || !input->checkPosition(entry.end()) ||
             m_state->m_entryMap.find(wh)!=m_state->m_entryMap.end()) {
      MWAW_DEBUG_MSG(("StyleParser::readTypeEntryMap: find some bad entry"));
      f << "###";
    }
    else
      m_state->m_entryMap[wh]=entry;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(input->tell());
  ascii().addNote("_");
  return ok && !m_state->m_entryMap.empty();
}

////////////////////////////////////////////////////////////
// font
////////////////////////////////////////////////////////////
bool StyleParser::readFontNames(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readFontNames: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Font)[names]:";
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();
  while (input->tell()<endPos) {
    long actPos=input->tell();
    auto dSz=static_cast<int>(input->readULong(1));
    if (!dSz || actPos+1+dSz > endPos) {
      MWAW_DEBUG_MSG(("StyleParser::readFontNames: can not read some entry\n"));
      ascii().addDelimiter(input->tell(),'|');
      f << "###";
      break;
    }
    std::string name("");
    for (int i=0; i<dSz; ++i) name+=char(input->readULong(1));
    f << name << ",";
    m_state->m_fontIdToFinalIdList.push_back(getFontConverter()->getId(name));
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readFontCorr(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readFontCorr: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Font)[correspondance]:";
  if (entry.length()%32) {
    MWAW_DEBUG_MSG(("StyleParser::readFontCorr: the entry size seems bad\n"));
    f << "###";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  auto N=int(entry.length()/30);
  for (int j=0; j<N; ++j) {
    long pos=input->tell();
    f.str("");
    f << "Font-" << j << ":";
    f << "id=" << input->readULong(2);
    for (int i=0; i<15; ++i) { // except f5, always 0
      auto val=static_cast<int>(input->readULong(2));
      if (!val)
        continue;
      if (i==5) // pos in fntb
        f << "pos=" << val << ",";
      else
        f << "f" << i << "=" << val << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+32, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool StyleParser::readStyleTable(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readStyleTable: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Style):";
  if (entry.length()%20) {
    MWAW_DEBUG_MSG(("StyleParser::readStyleTable: the entry size seems bad\n"));
    f << "###";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  auto N=int(entry.length()/20);
  m_state->m_fontList.clear();
  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    StyleParserInternal::Font font;
    f.str("");
    long used = input->readLong(4);
    int dim[2];
    for (auto &d : dim) d = static_cast<int>(input->readLong(2));
    f << "height?=" << dim[0] << ":" << dim[1] << ",";
    auto fId=static_cast<int>(input->readLong(2));
    if (fId < 0 && -fId<=static_cast<int>(m_state->m_fontIdToFinalIdList.size()))
      font.m_font.setId(m_state->m_fontIdToFinalIdList[size_t(-fId-1)]);
    else if (fId>0) // checkme, never seen
      font.m_font.setId(fId);
    else {
      MWAW_DEBUG_MSG(("StyleParser::readStyleTable: the font id seems bad\n"));
      f << "##fId=" << fId << ",";
    }
    auto flag=static_cast<int>(input->readULong(1));
    uint32_t flags = 0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x20) font.m_font.setDeltaLetterSpacing(-1.);
    if (flag&0x40) font.m_font.setDeltaLetterSpacing(1.);
    if (flag&0x80) f << "#flags=" << std::hex << (flag&0x80) << std::dec << ",";
    flag=static_cast<int>(input->readULong(1));
    if (flag) f << "#flags1=" << std::hex << flag << std::dec << ",";
    font.m_font.setSize(float(input->readULong(2)));
    font.m_font.setFlags(flags);
    unsigned char col[3];
    for (auto &c : col) c = static_cast<unsigned char>(input->readULong(2)>>8);
    font.m_font.setColor(MWAWColor(col[0],col[1],col[2]));
    font.m_font.m_extra=f.str();
    m_state->m_fontList.push_back(font);

    f.str("");
    f << "Style-" << i << ":";
    if (used!=1) f << "used?=" << used << ",";
#ifdef DEBUG
    f << ",font=[" << font.m_font.getDebugString(getFontConverter()) << "]";
#endif
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool StyleParser::readFormats(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length()<24) {
    MWAW_DEBUG_MSG(("StyleParser::readFormats: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  ascii().addPos(entry.end());
  ascii().addNote("_");
  libmwaw::DebugStream f;
  f << "Entries(Format):";
  int val;
  for (int i=0; i<6; ++i) { // f5=0|18
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  std::string marker("");
  for (int i=0; i<4; ++i) marker+=char(input->readULong(1));
  if (marker!="list") {
    MWAW_DEBUG_MSG(("StyleParser::readFormats: can not find the list marker\n"));
    f << "###";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return false;
  }
  auto N=static_cast<int>(input->readLong(4));
  f << "N=" << N << ",";
  for (int i=0; i<2; ++i) { // always 0
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }

  input->seek(entry.begin()+24, librevenge::RVNG_SEEK_SET);
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());

  for (int form=0; form<N; ++form) {
    long pos=input->tell();
    marker="";
    for (int c=0; c<4; ++c) marker+=char(input->readLong(1));
    long dSz=static_cast<int>(input->readLong(4));
    long endPos=pos+8+dSz;
    if (marker!="reco" || dSz<8 || endPos>entry.end()) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("StyleParser::readFormats: can not read some format\n"));
      break;
    }
    StyleParserInternal::Font font;
    f.str("");
    f << "Format-C" << form << ":";
    long N1=static_cast<int>(input->readULong(4));
    f << "N1=" << N1 << ",";
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    if (dSz<8+12*N1) {
      MWAW_DEBUG_MSG(("StyleParser::readFormats: N1 is bad\n"));
      f << "###N1,";
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      m_state->m_fontList.push_back(font);
      continue;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    std::string type("");
    for (long i=0; i<N1; ++i) {
      pos=input->tell();
      f.str("");
      f << "Format-C" << form << "-" << i << ":";
      marker=type="";
      for (int c=0; c<4; ++c) marker+=char(input->readULong(1));
      for (int c=0; c<4; ++c) type+=char(input->readULong(1));
      f << marker << "[" << type << "],";
      dSz=input->readLong(4);
      if (dSz<0 || pos+12+dSz>endPos) {
        MWAW_DEBUG_MSG(("StyleParser::readFormats: can not read a subformat\n"));
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      if (type=="enum" || type=="TEXT") {
        // font
        std::string string("");
        for (long c=0; c<dSz; ++c) string+=char(input->readULong(1));
        f << string;
        if (marker=="font")
          font.m_font.setId(getFontConverter()->getId(string));
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
      }
      else if (dSz==1 && type=="bool") {
        val=static_cast<int>(input->readLong(1));
        f << val;
        if (val<0 || val>1) {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: find some old bool value\n"));
          f << "###";
        }
        else if (val==0)
          ;
        else if (marker=="bold")
          font.m_font.setFlags(font.m_font.flags()|MWAWFont::boldBit);
        else if (marker=="cond")
          font.m_font.setDeltaLetterSpacing(-1.);
        else if (marker=="ital")
          font.m_font.setFlags(font.m_font.flags()|MWAWFont::italicBit);
        else if (marker=="outl")
          font.m_font.setFlags(font.m_font.flags()|MWAWFont::embossBit);
        else if (marker=="pexp")
          font.m_font.setDeltaLetterSpacing(1.);
        else if (marker=="shad")
          font.m_font.setFlags(font.m_font.flags()|MWAWFont::shadowBit);
        else if (marker=="strk")
          font.m_font.setStrikeOutStyle(MWAWFont::Line::Simple);
        else if (marker=="undl")
          font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
      }
      else if (dSz==2 && type=="shor") {
        val=static_cast<int>(input->readLong(2));
        f << val;
        if (marker=="fnt#") // fontId, ok to ignore
          ;
        else if (marker=="objb" || marker=="pptm") // CHECKME
          f << "##";
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
      }
      else if (dSz==4 && type=="long") {
        val=static_cast<int>(input->readLong(4));
        if (marker=="ptxe") // always 2000?
          f << float(val)/65536.f;
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###" << val;
        }
      }
      else if (dSz==4 && type=="fixd") {
        float value=float(input->readLong(4))/65536.f;
        f << value;
        if (marker=="ptsz")
          font.m_font.setSize(value);
        else if (marker=="xshf")
          font.m_font.set(MWAWFont::Script(value,librevenge::RVNG_POINT));
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
      }
      else if (dSz==4 && type=="QDpt") {
        // objs
        int dim[2];
        for (int &j : dim) j=static_cast<int>(input->readLong(2));
        font.m_picture.m_size=MWAWVec2i(dim[1],dim[0]);
        f << font.m_picture.m_size;
      }
      else if (dSz==6 && type=="cRGB") {
        unsigned char col[3];
        for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
        MWAWColor color(col[0],col[1],col[2]);
        f << color;
        if (marker=="colr")
          font.m_font.setColor(color);
        else if (marker=="pbcl")
          font.m_font.setBackgroundColor(color);
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
      }
      else if (type=="PICT" && dSz) {
        if (marker=="obj ") {
          font.m_picture.m_entry.setBegin(pos+12);
          font.m_picture.m_entry.setLength(dSz);
        }
        else {
          MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown marker\n"));
          f << "###";
        }
#ifdef DEBUG_WITH_FILES
        ascii().skipZone(pos+12, pos+12+dSz-1);
        librevenge::RVNGBinaryData file;
        input->seek(pos+12, librevenge::RVNG_SEEK_SET);
        input->readDataBlock(dSz, file);
        static int volatile pictName = 0;
        libmwaw::DebugStream f2;
        f2 << "PICT-" << ++pictName << ".pct";
        libmwaw::Debug::dumpFile(file, f2.str().c_str());
#endif
      }
      else {
        MWAW_DEBUG_MSG(("StyleParser::readFormats: unknown type\n"));
        f << "###type";
      }
      if (dSz%2) ++dSz;
      input->seek(pos+12+dSz, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    m_state->m_fontList.push_back(font);
    if (input->tell()==endPos) continue;

    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("StyleParser::readFormats: find extra data\n"));
    f.str("");
    f << "Format-" << form << ":###extra";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("StyleParser::readFormats: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Format:###extra");
  }
  return true;
}
////////////////////////////////////////////////////////////
// rules/plc
////////////////////////////////////////////////////////////
bool StyleParser::readRules(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length()<24) {
    MWAW_DEBUG_MSG(("StyleParser::readRules: the entry seems bad\n"));
    return false;
  }

  entry.setParsed(true);
  ascii().addPos(entry.end());
  ascii().addNote("_");

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Rule):";
  int val;
  for (int i=0; i<5; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  val=static_cast<int>(input->readLong(2));
  if (val!=24) // always 18?
    f << "f5=" << val << ",";
  std::string marker("");
  for (int i=0; i<4; ++i) marker+=char(input->readULong(1));
  if (marker!="list") {
    MWAW_DEBUG_MSG(("StyleParser::readRules: can not find the list marker\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  auto N=static_cast<int>(input->readLong(4));
  f << "N=" << N << ",";
  for (int i=0; i<2; ++i) { // always 0
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  m_state->m_paragraphList.clear();
  for (int r=0; r<N; ++r) {
    pos=input->tell();
    if (!input->checkPosition(pos+8))
      break;
    f.str("");
    f << "Rule-P" << r << ":";
    marker="";
    for (int i=0; i<4; ++i) marker+=char(input->readULong(1));
    long dSz=input->readLong(4);
    long endPos=pos+8+dSz;
    if (marker!="reco" || dSz<8 || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("StyleParser::readRules: can not read a rule\n"));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    long N1=static_cast<int>(input->readULong(4));
    f << "N1=" << N1 << ",";
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    MWAWParagraph para;
    if (dSz<8+12*N1) {
      MWAW_DEBUG_MSG(("StyleParser::readRules: N1 is bad\n"));
      f << "###N1,";
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      m_state->m_paragraphList.push_back(para);
      continue;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    std::string type("");
    for (long i=0; i<N1; ++i) {
      pos=input->tell();
      f.str("");
      f << "Rule-P" << r << "-" << i << ":";
      marker=type="";
      for (int c=0; c<4; ++c) marker+=char(input->readULong(1));
      for (int c=0; c<4; ++c) type+=char(input->readULong(1));
      f << marker << "[" << type << "],";
      dSz=input->readLong(4);
      if (dSz<0 || pos+12+dSz>endPos) {
        MWAW_DEBUG_MSG(("StyleParser::readRules: can not read a subrule\n"));
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      if (type=="enum") {
        std::string string("");
        for (long c=0; c<dSz; ++c) string+=char(input->readULong(1));
        f << string;
        if (marker=="pjst") {
          if (string=="cent")
            para.m_justify = MWAWParagraph::JustificationCenter;
          else if (string=="rght")
            para.m_justify = MWAWParagraph::JustificationRight;
          else if (string=="full")
            para.m_justify = MWAWParagraph::JustificationFull;
          else if (string!="left") {
            MWAW_DEBUG_MSG(("StyleParser::readRules: find unexpected align\n"));
            f << "###align,";
          }
        }
        else if (marker=="BBRD") { // bottom border
          if (string=="DTDL") {
            MWAWBorder border;
            border.m_style=MWAWBorder::Dot;
            para.m_borders.resize(libmwaw::Bottom+1);
            para.m_borders[libmwaw::Bottom]=border;
          }
          else if (string=="SLDL") {
            para.m_borders.resize(libmwaw::Bottom+1);
            para.m_borders[libmwaw::Bottom]=MWAWBorder();
          }
          else if (string=="THKL") {
            MWAWBorder border;
            border.m_width=2;
            para.m_borders.resize(libmwaw::Bottom+1);
            para.m_borders[libmwaw::Bottom]=border;
          }
          else {
            MWAW_DEBUG_MSG(("StyleParser::readRules: sorry, unknown bottom border\n"));
            f << "###";
          }
        }
        else {
          MWAW_DEBUG_MSG(("StyleParser::readRules: unexpected marker\n"));
          f << "###";
        }
      }
      else if (type=="fixd" && dSz==4) {
        // riin[right], lein[left], fidt[firstIndent], spaf[after], spbe[before]
        double value=double(input->readLong(4))/65536.;
        f << value;
        if (marker=="ledg")
          para.setInterline(1.0+value, librevenge::RVNG_PERCENT);
        else if (marker=="lein")
          para.m_margins[1]=value/72.;
        else if (marker=="riin")
          para.m_margins[2]=value/72.;
        else if (marker=="fidt")
          para.m_margins[0]=value/72.;
        else if (marker=="spbe")
          para.m_spacings[1]=value/72.;
        else if (marker=="spaf")
          para.m_spacings[2]=value/72.;
        else {
          MWAW_DEBUG_MSG(("StyleParser::readRules: unexpected marker\n"));
          f << "###";
        }
      }
      else {
        MWAW_DEBUG_MSG(("StyleParser::readRules: unknown type\n"));
        f << "###type";
      }
      if (dSz%2) ++dSz;
      input->seek(pos+12+dSz, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    m_state->m_paragraphList.push_back(para);
    if (input->tell()==endPos) continue;

    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("StyleParser::readRules: find extra data\n"));
    f.str("");
    f << "Rule-P" << r << ":###extra";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("StyleParser::readRules: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Rule:###extra");
  }
  return true;
}

bool StyleParser::readPLCs(MWAWEntry const &entry, bool para)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || (entry.length()%8)!=0) {
    MWAW_DEBUG_MSG(("StyleParser::readPLCs: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  if (!para)
    f << "Entries(PLC)[char]:";
  else
    f << "Entries(PLC)[para]:";
  std::map<long,int> &map=para ? m_state->m_posParagraphIdMap : m_state->m_posFontIdMap;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  char wh=para ? 'P' : 'S';
  auto N=int(entry.length()/8);
  for (int i=0; i<N; ++i) {
    auto pos=long(input->readULong(4));
    auto id=static_cast<int>(input->readLong(4));
    f << pos;
    if (id!=-1) f << ":" << wh << id;
    f << ",";
    if (map.find(pos)!=map.end()) {
      MWAW_DEBUG_MSG(("StyleParser::readPLCs: pos %ld already exists\n", pos));
      f << "###";
    }
    else
      map[pos]=id;
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// picture
////////////////////////////////////////////////////////////
bool StyleParser::readPictures(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readPictures: the entry seems bad\n"));
    return false;
  }

  entry.setParsed(true);
  ascii().addPos(entry.begin());
  ascii().addNote("Entries(Picture):");
  ascii().addPos(entry.end());
  ascii().addNote("_");
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  std::string type;
  while (input->tell()<entry.end()) {
    long pos=input->tell();
    if (!input->checkPosition(pos+24))
      break;
    libmwaw::DebugStream f;
    f << "Picture:";
    auto pictPos=long(input->readULong(4));
    if (pictPos) f << "pictPos=" << pictPos << ",";
    StyleParserInternal::Picture pict;
    type="";
    for (int i=0; i<4; ++i) type+=char(input->readULong(1));
    f << type << ",";
    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    long dSz=input->readLong(4);
    if (dSz<0 || !input->checkPosition(pos+24+dSz)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    int dim[2];
    for (auto &d : dim) d=static_cast<int>(input->readULong(2));
    pict.m_size = MWAWVec2i(dim[1],dim[0]);
    f << "sz=" << pict.m_size << ",";
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    if (!dSz) continue;

    pict.m_entry.setBegin(pos+24);
    pict.m_entry.setLength(dSz);
    if (m_state->m_posPictureMap.find(pictPos) != m_state->m_posPictureMap.end()) {
      MWAW_DEBUG_MSG(("StyleParser::readPictures: a picture already exist at %ld\n", pictPos));
    }
    else
      m_state->m_posPictureMap[pictPos]=pict;
#ifdef DEBUG_WITH_FILES
    ascii().skipZone(pos+24, pos+24+dSz-1);
    librevenge::RVNGBinaryData file;
    input->seek(pos+24, librevenge::RVNG_SEEK_SET);
    input->readDataBlock(dSz, file);
    static int volatile pictName = 0;
    libmwaw::DebugStream f2;
    f2 << "PICT-" << ++pictName << ".pct";
    libmwaw::Debug::dumpFile(file, f2.str().c_str());
#endif
    input->seek(pos+24+dSz, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("StyleParser::readPictures: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Picture:###extra");
  }
  return true;

}
////////////////////////////////////////////////////////////
// other
////////////////////////////////////////////////////////////
bool StyleParser::readExtraProperties(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length()<16) {
    MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  ascii().addPos(entry.end());
  ascii().addNote("_");
  libmwaw::DebugStream f;
  f << "Entries(XProp):";
  std::string marker("");
  for (int c=0; c<4; ++c) marker+=char(input->readULong(1));
  if (marker!="grow" && marker!="More") {
    MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: can not find main marker\n"));
    f << "###marker=" << marker << ",";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return true;
  }
  f << marker << ",";
  int val;
  for (int i=0; i<5; ++i) {
    val=static_cast<int>(input->readLong(2));
    static const int expected[]= {1,0,0x4000,0,0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  auto N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (16*(N+1)>entry.length()) {
    MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: can not read the number of entry\n"));
    f << "###";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return true;
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  std::vector<MWAWEntry> entryList;
  for (int x=0; x<N; ++x) {
    long pos=input->tell();
    f.str("");
    f << "XProp-" << x << ":";
    marker="";
    for (int c=0; c<4; ++c) marker+=char(input->readULong(1));
    f << marker << ",";
    int id=1;
    if (marker!="Info") {
      id=static_cast<int>(input->readLong(4));
      if (id!=1) f << "id=" << id << ",";
    }
    else {
      std::string type;
      for (int c=0; c<4; ++c) type+=char(input->readULong(1));
      f << type << ",";
    }
    for (int i=0; i<2; ++i) {
      val=static_cast<int>(input->readLong(2));
      static const int expected[]= {0x4000,0};
      if (val==expected[i])
        continue;
      f << "f" << i << "=" << val << ",";
    }
    long dPos=input->readLong(4);
    f << "pos=" << dPos << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    if (dPos>0 && 16*(N+1)+dPos<entry.length()) {
      MWAWEntry dEntry;
      dEntry.setType(marker);
      dEntry.setId(id);
      dEntry.setBegin(entry.begin()+16*(N+1)+dPos);
      entryList.push_back(dEntry);
    }
    else if (dPos) {
      ascii().addPos(pos);
      ascii().addNote("###");
      MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: dataPos seems bad\n"));
    }
  }

  long pos=input->tell();
  if (pos==entry.end() && entryList.empty())
    return true;
  auto dSize=long(input->readULong(4));
  f.str("");
  f << "XProp[dataSz]:sz=" << dSize << ",";
  if (dSize<0 || !input->checkPosition(pos+4+dSize)) {
    MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: can not read data size\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (auto const &dEntry : entryList) {
    f.str("");
    f << "XProp[" << dEntry.type() << "-" << dEntry.id() << "]:";
    input->seek(dEntry.begin(), librevenge::RVNG_SEEK_SET);
    dSize=long(input->readULong(4));
    long endPos=dEntry.begin()+4+dSize;
    if (dSize<0 || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: can not read a data size\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    if (dSize==1 && (dEntry.type()=="covr" || dEntry.type()=="cusg" ||
                     dEntry.type()=="ehnd" || dEntry.type()=="ornt" ||
                     dEntry.type()=="Coll" || dEntry.type()=="Dgok"))
      f << "val=" << input->readLong(1) << ",";
    else if (dSize==4 && (dEntry.type()=="copy" || dEntry.type()=="NTnt"|| dEntry.type()=="Ucpy"))
      f << "val=" << input->readLong(4) << ",";
    else if (dSize==6 && dEntry.type()=="Bkpr") {
      for (int i=0; i<2; ++i) { // fl0=fl1=1
        val=static_cast<int>(input->readLong(1));
        if (val) f << "fl" << i << "=" << val << ",";
      }
      f << "ids=["; // af84,4f8 | b291,de8c
      for (int i=0; i<2; ++i) f << std::hex << input->readULong(2) << std::dec << ",";
      f << "],";
    }
    else if (dSize==20 && dEntry.type()=="nupd") {
      for (int i=0; i<10; ++i) { // f5=f7=1, f8=-256, f9=1|168
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
    }
    else if (dSize>7 && dEntry.type()=="dprf") {
      for (int i=0; i<3; ++i) { // f0=-1, f2=635f
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto dSz=static_cast<int>(input->readULong(1));
      if (dSz+7<=dSize) {
        // printer name
        std::string text("");
        for (int i=0; i<dSz; ++i) text+=char(input->readULong(1));
        f << text << ",";
        // then 793530010cf85c010cf86001032fe80000000000000000010cf6180000004c01032fe8ffff
      }
      else
        f << "###dSz=" << dSz << ",";
    }
    else if (dSize>=1 && dEntry.type()=="psnt") { // find A4Small,
      auto dSz=static_cast<int>(input->readULong(1));
      if (dSz+1<=dSize) {
        std::string text("");
        for (int i=0; i<dSz; ++i) text+=char(input->readULong(1));
        f << text << ",";
      }
      else
        f << "###dSz=" << dSz << ",";
    }
    else if (dEntry.type()=="feat") { // find InputSlot,Upper,ManualFeed,False,PageRegion,A4Small
      while (input->tell()<endPos) {
        auto dSz=static_cast<int>(input->readULong(1));
        if (input->tell()+dSz>endPos) {
          MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: can not read a feat string\n"));
          f << "###dSz=" << dSz << ",";
          break;
        }
        std::string text("");
        for (int i=0; i<dSz; ++i) text+=char(input->readULong(1));
        f << text << ",";
      }
    }
    else if (dSize>=1 && dEntry.type()=="Info") {
      std::string text("");
      for (long i=0; i<dSize; ++i) text+=char(input->readULong(1));
      f << text << ",";
    }
    else if (dEntry.type()=="ppnf") {
      auto dSz=static_cast<int>(input->readULong(2));
      if (dSz+2<=dSize) { // A4Small
        std::string text("");
        for (int i=0; i<dSz; ++i) text+=char(input->readULong(1));
        f << text << ",";
        /* then 000000006c61fd03ae816000554c7803ae8180220002480bfc44d00000000003ae8190000e4734041fccb400000000fe0200000000000103ae81d0000000c003ae81b00062666400000023 */
      }
      else
        f << "###dSz=" << dSz << ",";
    }
    else {
      MWAW_DEBUG_MSG(("StyleParser::readExtraProperties: find unknown type\n"));
      f << "###unknown";
    }
    if (input->tell()!=endPos)
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(dEntry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(endPos);
    ascii().addNote("_");
  }
  return true;
}

bool StyleParser::readBackgroundColor(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readBackgroundColor: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(BgColor):";
  if (entry.length()!=6) {
    MWAW_DEBUG_MSG(("StyleParser::readBackgroundColor: the entry size seems bad\n"));
    f << "###";
  }
  else {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    m_state->m_backgroundColor=MWAWColor(col[0], col[1], col[2]);
    f << m_state->m_backgroundColor << ",";
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readMargins(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readMargins: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Margins):";
  if (entry.length()!=16) {
    MWAW_DEBUG_MSG(("StyleParser::readMargins: the entry size seems bad\n"));
    f << "###";
  }
  else {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    float margins[4];
    bool ok=true;
    f << "marg=[";
    for (auto &margin : margins) {
      margin=float(input->readULong(4))/72.0f/65536.0f;
      if (margin<0) {
        MWAW_DEBUG_MSG(("StyleParser::readMargins: some margin seems bad\n"));
        f << "###";
        ok = false;
      }
      f << margin << ",";
    }
    f << "],";
    if (ok) { // checkme: order
      getPageSpan().setMarginLeft(double(margins[0]));
      getPageSpan().setMarginTop(double(margins[1]));
      getPageSpan().setMarginRight(double(margins[2]));
      getPageSpan().setMarginBottom(double(margins[3]));
    }
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readPrintInfo(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length() < 0x78) {
    MWAW_DEBUG_MSG(("StyleParser::readPrintInfo: zone size is invalid\n"));
    return false;
  }

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo):";
  libmwaw::PrinterInfo info;
  if (!info.read(input)) {
    MWAW_DEBUG_MSG(("StyleParser::readPrintInfo: can not read print info\n"));
    return false;
  }
  entry.setParsed(true);
  f << info;

  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    MWAW_DEBUG_MSG(("StyleParser::readPrintInfo: the paper size seems bad\n"));
    f << "###";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }

  // define margin from print info
  MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
  MWAWVec2i rBotMargin=info.paper().size() - info.page().size();

  getPageSpan().setMarginTop(lTopMargin.y()/72.0);
  getPageSpan().setMarginBottom(rBotMargin.y()/72.0);
  getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
  getPageSpan().setMarginRight(rBotMargin.x()/72.0);
  getPageSpan().setFormLength(paperSize.y()/72.);
  getPageSpan().setFormWidth(paperSize.x()/72.);

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readStat(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readStat: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Stat):";
  if (entry.length()!=40) {
    MWAW_DEBUG_MSG(("StyleParser::readStat: the entry size seems bad\n"));
    f << "###";
  }
  else {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    f << "ids=[";
    for (int i=0; i<2; ++i) f << std::hex << input->readULong(2) << std::dec << ",";
    f << "],";
    int dim[4];
    for (auto &d : dim) d=static_cast<int>(input->readLong(2));
    f << "dim?=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    for (int i=0; i<14; ++i) { // f4=0|-1, f5=0|-2292, f13=0|1|3
      auto val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readTabWidth(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readTabWidth: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(TabWidth):";
  if (entry.length()!=4) {
    MWAW_DEBUG_MSG(("StyleParser::readTabWidth: the entry size seems bad\n"));
    f << "###";
  }
  else {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    f << "val=" << float(input->readLong(4))/72.f/65536.f;
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool StyleParser::readVersion(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("StyleParser::readVersion: the entry seems bad\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Version):";
  if (entry.length()!=4) {
    MWAW_DEBUG_MSG(("StyleParser::readVersion: the entry size seems bad\n"));
    f << "###";
  }
  else {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    // find 0x160 for version 1.6, 0x191 for version 1.9.1
    f << "vers=" << std::hex << input->readULong(2) << std::dec << ",";
    auto val=static_cast<int>(input->readULong(2));
    if (val!=0x8000)
      f << "f0=" << std::hex << val << std::dec << ",";
  }

  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
int StyleParser::computeNumPages(MWAWEntry const &entry, bool unicodeChar) const
{
  MWAWInputStreamPtr input = const_cast<StyleParser *>(this)->getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int nPages=1;

  int const cSize=unicodeChar ? 2 : 1;
  while (!input->isEnd() && input->tell()<entry.end()) {
    if (input->readLong(cSize)==0xc)
      nPages++;
  }
  return nPages;
}

bool StyleParser::sendText(MWAWEntry const &entry, bool unicodeChar)
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("StyleParser::sendText: can not find the listener\n"));
    return false;
  }
  if (!entry.valid()) {
    // ok no text
    return true;
  }
  MWAWInputStreamPtr input = getInput();
  long debPos=entry.begin();
  input->seek(debPos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Entries(TEXT):";
  getTextListener()->setFont(MWAWFont(3,12));

  int actPage=1;
  auto numFonts=static_cast<int>(m_state->m_fontList.size());
  auto numParagraphs=static_cast<int>(m_state->m_paragraphList.size());
  long endChar = entry.length(), endPos = entry.end();
  int const cSize=unicodeChar ? 2 : 1;
  for (long i=0; i <= endChar; i+=cSize) {
    bool isEnd = i==endChar;
    int c=isEnd ? static_cast<int>(0) : static_cast<int>(input->readULong(cSize));
    if (isEnd || c==0xd || c==0xc || c==0x2029) {
      ascii().addPos(debPos);
      ascii().addNote(f.str().c_str());
      debPos = input->tell();
      if (isEnd) break;
      f.str("");
      f << "TEXT:";
    }
    auto plcIt=m_state->m_posParagraphIdMap.find(i);
    if (plcIt != m_state->m_posParagraphIdMap.end()) {
      if (plcIt->second>=0 && plcIt->second<numParagraphs)
        getTextListener()->setParagraph(m_state->m_paragraphList[size_t(plcIt->second)]);
      f << "[Style:P" << plcIt->second << "]";
    }
    plcIt=m_state->m_posFontIdMap.find(i);
    if (plcIt != m_state->m_posFontIdMap.end()) {
      if (plcIt->second>=0 && plcIt->second<numFonts) {
        auto const &font=m_state->m_fontList[size_t(plcIt->second)];
        getTextListener()->setFont(font.m_font);
        if (font.hasPicture())
          sendPicture(font.m_picture);
      }
      f << "[Style:C" << plcIt->second << "]";
    }
    if (c>=0x100)
      f << "[" << std::hex << c << std::dec << "]";
    else if (c)
      f << char(c);
    if (c==0xc) {
      newPage(++actPage);
      continue;
    }
    if (c==0 && !unicodeChar && !isEnd) {
      // tex-edit accept control character, ...
      auto nextC=static_cast<unsigned char>(input->readULong(1));
      if (nextC < 0x20) {
        i++;
        getTextListener()->insertChar('^');
        getTextListener()->insertChar(uint8_t('@'+nextC));
        continue;
      }
      input->seek(-1, librevenge::RVNG_SEEK_CUR);
    }
    switch (c) {
    case 0x1:
      if (m_state->m_posPictureMap.find(i)==m_state->m_posPictureMap.end() ||
          !m_state->m_posPictureMap.find(i)->second.valid()) {
        MWAW_DEBUG_MSG(("StyleParser::sendPicture: can not find picture for id=%ld\n",i));
        f << "[##pict]";
        break;
      }
      sendPicture(m_state->m_posPictureMap.find(i)->second);
      break;
    case 0x9:
      getTextListener()->insertTab();
      break;
    case 0x2028:
      getTextListener()->insertEOL(true);
      break;
    case 0xd:
    case 0x2029:
      getTextListener()->insertEOL();
      break;
    case 0x11: // command key
      getTextListener()->insertUnicode(0x2318);
      break;
    case 0x14: // apple logo: check me
      getTextListener()->insertUnicode(0xf8ff);
      break;
    case 0xfffc: // image
      break;
    default:
      if (c < 0x20) f  << "##[" << std::hex << c << std::dec << "]";
      if (unicodeChar)
        getTextListener()->insertUnicode(uint32_t(c));
      else
        i += getTextListener()->insertCharacter(static_cast<unsigned char>(c), input, endPos);
      break;
    }
  }
  return true;
}

// the pictures
bool StyleParser::sendPicture(StyleParserInternal::Picture const &pict)
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("StyleParser::sendPicture: can not find the listener\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  librevenge::RVNGBinaryData data;
  long pos = input->tell();
  input->seek(pict.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(pict.m_entry.length(), data);
  input->seek(pos,librevenge::RVNG_SEEK_SET);

  auto dataSz=int(data.size());
  if (!dataSz)
    return false;
  MWAWPosition pictPos=MWAWPosition(MWAWVec2f(0,0),MWAWVec2f(pict.m_size), librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Char);
  MWAWEmbeddedObject picture(data);
  getTextListener()->insertPicture(pictPos, picture);
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool StyleParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = StyleParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(40))
    return false;
  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (input->readULong(2)!=0x4348 || input->readULong(2)!=0x4e4b ||
      input->readULong(2)!=0x100 || input->readULong(2)!=0)
    return false;
  if (strict && !readTypeEntryMap())
    return false;
  setVersion(1);
  if (header)
    header->reset(MWAWDocument::MWAW_T_STYLE, version());
  ascii().addPos(0);
  ascii().addNote("FileHeader:");
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
