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

#include "libmwaw_internal.hxx"

#include "MaxWrtParser.hxx"

/** Internal: the structures of a MaxWrtParser */
namespace MaxWrtParserInternal
{
////////////////////////////////////////
//! Internal: the state of a MaxWrtParser
struct State {
  //! constructor
  State()
    : m_fontList()
    , m_posToPLCMap()
  {
  }
  //! the list of font
  std::vector<MWAWFont> m_fontList;
  //! a map character pos to font id
  std::map<int,int> m_posToPLCMap;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MaxWrtParser::MaxWrtParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state(new MaxWrtParserInternal::State)
{
  setAsciiName("main-1");
  // no margins ( ie. the document is a set of picture corresponding to each page )
  getPageSpan().setMargins(0.01);
}

MaxWrtParser::~MaxWrtParser()
{
}

MWAWInputStreamPtr MaxWrtParser::rsrcInput()
{
  return getRSRCParser()->getInput();
}

libmwaw::DebugFile &MaxWrtParser::rsrcAscii()
{
  return getRSRCParser()->ascii();
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MaxWrtParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !getRSRCParser() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendText();
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MaxWrtParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MaxWrtParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("MaxWrtParser::createDocument: listener already exist\n"));
    return;
  }

  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  std::vector<MWAWPageSpan> pageList(1,ps);
  //
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool MaxWrtParser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  auto &entryMap = rsrcParser->getEntriesMap();

  // the 1 zone
  auto it = entryMap.lower_bound("LnHe");
  while (it != entryMap.end()) {
    if (it->first != "LnHe")
      break;
    readLineHeight(it++->second);
  }
  it = entryMap.lower_bound("StTB");
  while (it != entryMap.end()) {
    if (it->first != "StTB")
      break;
    readStyles(it++->second);
  }
  it = entryMap.lower_bound("Styl");
  while (it != entryMap.end()) {
    if (it->first != "Styl")
      break;
    readStylePLC(it++->second);
  }
  return true;
}

bool MaxWrtParser::readLineHeight(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%4)) {
    MWAW_DEBUG_MSG(("MaxWrtParser::readLineHeight: the entry is bad\n"));
    return false;
  }
  entry.setParsed(true);
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(LineHeight)[" << entry.id() << "]:";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  auto N=int(entry.length()/4);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "LineHeight-" << i << ":";
    f << "height=" << input->readLong(2) << "x" << input->readLong(2) << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool MaxWrtParser::readStylePLC(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%4) || entry.length()<20) {
    MWAW_DEBUG_MSG(("MaxWrtParser::readStylePLC: the entry is bad\n"));
    return false;
  }
  entry.setParsed(true);
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(StylPLC)[" << entry.id() << "]:";
  f << "Num?=" << input->readULong(2) << "x" << input->readULong(2) << ",";
  f << "ids?=[" << std::hex << input->readULong(4) << "," << input->readULong(4) << std::dec << "],";
  f << "unkn=" << std::hex << input->readULong(4) << std::dec << ","; // c82e0000|aa55aa55|74
  f << "id2=" << std::hex << input->readULong(4) << std::dec << ",";
  auto N=int((entry.length()-20)/4);
  f << "plcs=[";
  for (int i=0; i<N; ++i) {
    auto cPos=static_cast<int>(input->readULong(2));
    auto zone=static_cast<int>(input->readLong(2));
    if (zone==-1)
      f << cPos << ":*,";
    else {
      m_state->m_posToPLCMap[cPos]=zone;
      f << cPos << ":PLC" << zone << ",";
    }
  }
  f << "],";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool MaxWrtParser::readStyles(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%18)) {
    MWAW_DEBUG_MSG(("MaxWrtParser::readStyles: the entry is bad\n"));
    return false;
  }
  entry.setParsed(true);
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(StylDef)[" << entry.id() << "]:";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  auto N=int(entry.length()/18);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "StylDef-PLC" << i << ":";
    auto val=static_cast<int>(input->readLong(2));
    if (val!=1) f << "used?=" << val << ",";
    val=static_cast<int>(input->readLong(2));
    if (val!=16) f << "f0=" << val << ",";
    MWAWFont font;
    font.setSize(float(input->readULong(2)));
    font.setId(static_cast<int>(input->readULong(2)));
    auto flag = static_cast<int>(input->readULong(1));
    uint32_t flags=0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x20) font.setDeltaLetterSpacing(-1);
    if (flag&0x40) font.setDeltaLetterSpacing(1);
    if (flag&0x80) f << "#flag0[0x80],";
    font.setFlags(flags);
    val=static_cast<int>(input->readULong(1));
    if (val) f << "#flag1=" << std::hex << val << std::dec << ",";
    auto finalSz=static_cast<int>(input->readULong(2));
    if (finalSz) font.setSize(float(finalSz));
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    font.setColor(MWAWColor(col[0],col[1],col[2]));
    f << font.getDebugString(getParserState()->m_fontConverter);
    if (static_cast<int>(m_state->m_fontList.size()) <= i)
      m_state->m_fontList.resize(size_t(i+1));
    if (font.id()==0) font.setId(3);
    m_state->m_fontList[size_t(i)]=font;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+18, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool MaxWrtParser::sendText()
{
  MWAWListenerPtr listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MaxWrtParser::sendText: can not find the listener\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  input->seek(0, librevenge::RVNG_SEEK_SET);
  long debPos=0;

  libmwaw::DebugStream f;
  f << "Entries(TEXT):";
  listener->setFont(MWAWFont(3,12));

  long endPos = input->size();
  for (int i=0; i < static_cast<int>(endPos); i++) {
    bool isEnd = input->isEnd();
    unsigned char c=isEnd ? static_cast<unsigned char>(0) : static_cast<unsigned char>(input->readULong(1));
    if (isEnd || c==0xd) {
      ascii().addPos(debPos);
      ascii().addNote(f.str().c_str());
      debPos = input->tell();
      if (isEnd) break;
      f.str("");
      f << "TEXT:";
    }
    auto plcIt=m_state->m_posToPLCMap.find(i);
    if (plcIt != m_state->m_posToPLCMap.end()) {
      int fontId=m_state->m_posToPLCMap.find(i)->second;
      f << "[PLC" << fontId << "]";
      if (fontId>=0 && fontId<static_cast<int>(m_state->m_fontList.size()))
        listener->setFont(m_state->m_fontList[size_t(fontId)]);
      else {
        MWAW_DEBUG_MSG(("MaxWrtParser::sendText: can not find a font\n"));
        f << "##";
      }
    }
    if (c)
      f << c;
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      listener->insertEOL();
      break;
    default:
      if (c < 0x20) f  << "##[" << std::hex << int(c) << std::dec << "]";
      i += listener->insertCharacter(c, input, endPos);
      break;
    }
  }
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
bool MaxWrtParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = MaxWrtParserInternal::State();
  /** no data fork, may be ok, but this means
      that the file contains no text, so... */
  MWAWInputStreamPtr input = getInput();
  if (!input || !getRSRCParser() || !input->hasDataFork())
    return false;
  // check that the style zone exists
  auto &entryMap = getRSRCParser()->getEntriesMap();
  if (entryMap.find("Styl") == entryMap.end())
    return false;
  if (header)
    header->reset(MWAWDocument::MWAW_T_MAXWRITE, version());

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
