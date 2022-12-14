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
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "ZWrtText.hxx"

#include "ZWrtParser.hxx"

/** Internal: the structures of a ZWrtParser */
namespace ZWrtParserInternal
{
////////////////////////////////////////
//! Internal: the state of a ZWrtParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)
    , m_headerUsed(true)
    , m_footerUsed(true)
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  //! true if the header is used
  bool m_headerUsed;
  //! true if the footer is used
  bool m_footerUsed;
  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

////////////////////////////////////////
//! Internal: the subdocument of a ZWrtParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(ZWrtParser &pars, MWAWInputStreamPtr const &input, bool header)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_isHeader(header)
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
    if (m_isHeader != sDoc->m_isHeader) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! true if we need to send the parser
  int m_isHeader;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("ZWrtParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<ZWrtParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("ZWrtParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  parser->sendHeaderFooter(m_isHeader);
}
}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
ZWrtParser::ZWrtParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
  , m_textParser()
{
  init();
}

ZWrtParser::~ZWrtParser()
{
}

void ZWrtParser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new ZWrtParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);

  m_textParser.reset(new ZWrtText(*this));
}

MWAWInputStreamPtr ZWrtParser::rsrcInput()
{
  return getRSRCParser()->getInput();
}

libmwaw::DebugFile &ZWrtParser::rsrcAscii()
{
  return getRSRCParser()->ascii();
}

////////////////////////////////////////////////////////////
// position and height
////////////////////////////////////////////////////////////
MWAWVec2f ZWrtParser::getPageLeftTop() const
{
  return MWAWVec2f(float(getPageSpan().getMarginLeft()),
                   float(getPageSpan().getMarginTop()+m_state->m_headerHeight/72.0));
}

////////////////////////////////////////////////////////////
// interface with the text parser
////////////////////////////////////////////////////////////
bool ZWrtParser::sendHeaderFooter(bool header)
{
  MWAWInputStreamPtr rsrc = rsrcInput();
  long rsrcPos = rsrc->tell();
  m_textParser->sendHeaderFooter(header);
  rsrc->seek(rsrcPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void ZWrtParser::newPage(int number)
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
void ZWrtParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !getRSRCParser() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      m_textParser->sendMainText();
#ifdef DEBUG
      m_textParser->flushExtra();
#endif
    }
  }
  catch (...) {
    MWAW_DEBUG_MSG(("ZWrtParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void ZWrtParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("ZWrtParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  int numPages = 1;
  if (m_textParser->numPages() > numPages)
    numPages = m_textParser->numPages();
  m_state->m_numPages = numPages;

  MWAWPageSpan ps(getPageSpan());
  if (m_state->m_headerUsed && m_textParser->hasHeaderFooter(true)) {
    MWAWHeaderFooter header(MWAWHeaderFooter::HEADER, MWAWHeaderFooter::ALL);
    header.m_subDocument.reset(new ZWrtParserInternal::SubDocument(*this, getInput(), true));
    ps.setHeaderFooter(header);
  }
  if (m_state->m_footerUsed && m_textParser->hasHeaderFooter(false)) {
    MWAWHeaderFooter footer(MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    footer.m_subDocument.reset(new ZWrtParserInternal::SubDocument(*this, getInput(), false));
    ps.setHeaderFooter(footer);
  }
  ps.setPageSpan(m_state->m_numPages+1);
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
bool ZWrtParser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    MWAW_DEBUG_MSG(("ZWrtParser::createZones: can not find the entry map\n"));
    return false;
  }
  auto &entryMap = rsrcParser->getEntriesMap();

  // the 128 zones
  char const *zNames[] = {"BBAR", "HTML", "PRIN", "RANG", "WPOS", "PGPT"};
  for (int z = 0; z < 6; z++) {
    auto it = entryMap.lower_bound(zNames[z]);
    while (it != entryMap.end()) {
      if (it->first != zNames[z])
        break;
      MWAWEntry const &entry = it++->second;
      bool done=true;
      switch (z) {
      case 0:
        done=readBarState(entry);
        break;
      case 1:
        done=readHTMLPref(entry);
        break;
      case 2:
        done=readPrintInfo(entry);
        break;
      case 3:
        done=readSectionRange(entry);
        break;
      case 4:
        done=readWindowPos(entry);
        break;
      case 5:
        done=readCPRT(entry);
        break;
      default:
        done=false;
        break;
      }
      if (done || !entry.valid()) continue;
      readUnknownZone(entry);
    }
  }

  // 1001 and following
  char const *sNames[] = {"CPOS", "SLEN"};
  for (int z = 0; z < 2; z++) {
    auto it = entryMap.lower_bound(sNames[z]);
    while (it != entryMap.end()) {
      if (it->first != sNames[z])
        break;
      MWAWEntry const &entry = it++->second;
      bool done=true;
      switch (z) {
      case 0:
        done=readCPos(entry);
        break;
      case 1:
        done=readSLen(entry);
        break;
      default:
        done = false;
      }
      if (done || !entry.valid()) continue;
      readUnknownZone(entry);
    }
  }
  if (!m_textParser->createZones())
    return false;
  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool ZWrtParser::readPrintInfo(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readPrintInfo: the entry is bad\n"));
    return false;
  }

  if (entry.id()!=128) {
    MWAW_DEBUG_MSG(("ZWrtParser::readPrintInfo: the entry id is odd\n"));
  }
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields)) {
    MWAW_DEBUG_MSG(("ZWrtParser::readPrintInfo: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  size_t numFields = fields.size();
  if (numFields < 6) {
    MWAW_DEBUG_MSG(("ZWrtParser::readPrintInfo: the fields list seems very short\n"));
  }
  bool boolVal;
  float floatVal;
  int intVal;
  std::string strVal;
  int margins[4]= {0,0,0,0};
  bool marginsOk=true;
  for (size_t ff = 0; ff < numFields; ff++) {
    ZWField const &field = fields[ff];
    bool done = false;
    switch (ff) {
    case 0: // T
    case 1: // B
    case 2: // L
    case 3: // R
      done = field.getInt(input, intVal);
      if (!done) {
        marginsOk = false;
        break;
      }
      margins[ff]=intVal;
      break;
    case 4: // 2,-2,-4
      done = field.getInt(input, intVal);
      if (!done||!intVal)
        break;
      f << "autoResize=" << intVal << ",";
      break;
    case 5: //1.2
      done = field.getFloat(input, floatVal);
      if (!done)
        break;
      f << "lineSpacing=" << floatVal << ",";
      break;
    case 6:
    case 7: // always set
    case 8: // always set
      done = field.getBool(input, boolVal);
      if (!done)
        break;
      if (!boolVal)
        continue;
      switch (ff) { // checkme: does not seems to works in all case...
      case 6:
        f << "sectionAddNewPage,";
        break;
      case 7:
        f << "useHeader,";
        break;
      case 8:
        f << "useFooter,";
        break;
      default:
        f << "#f" << ff << "Set,";
        break;
      }
      break;
    default:
      break;
    }
    if (done)
      continue;
    if (fields[ff].getDebugString(input, strVal))
      f << "#f" << ff << "=\"" << strVal << "\",";
    else
      f << "#f" << ff << ",";
  }
  if (marginsOk) {
    getPageSpan().setMarginTop(double(margins[0])/72.0);
    getPageSpan().setMarginBottom(double(margins[1])/72.0);
    getPageSpan().setMarginLeft(double(margins[2])/72.0);
    getPageSpan().setMarginRight(double(margins[3])/72.0);
  }
  f << "margins=(" << margins[2] << "x" << margins[0] << "<->" << margins[3] << "x" << margins[1] << "),";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

// read the print info xml data
bool ZWrtParser::readCPRT(MWAWEntry const &entry)
{
  if (entry.length() < 0x10) {
    MWAW_DEBUG_MSG(("ZWrtParser::readCPRT: data seems to short\n"));
    return false;
  }

  MWAWInputStreamPtr input = rsrcInput();
  long pos = entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
#ifdef DEBUG_WITH_FILES
  libmwaw::DebugFile &ascFile = rsrcAscii();
  librevenge::RVNGBinaryData file;
  input->readDataBlock(entry.length(), file);

  static int volatile cprtName = 0;
  libmwaw::DebugStream f;
  f << "CPRT" << ++cprtName << ".plist";
  libmwaw::Debug::dumpFile(file, f.str().c_str());

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  ascFile.skipZone(entry.begin(),entry.end()-1);
#endif
  return true;
}

////////////////////////////////////////////////////////////
// read the bar state/windows pos, ...
////////////////////////////////////////////////////////////
bool ZWrtParser::readBarState(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readBarState: the entry is bad\n"));
    return false;
  }

  if (entry.id()!=128) {
    MWAW_DEBUG_MSG(("ZWrtParser::readBarState: the entry id is odd\n"));
  }
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields) || !fields.size()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readBarState: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::string res("");
  if (fields[0].getString(input, res))
    f << "set=" << res << ",";
  else
    f << "#set,";
  size_t numFields = fields.size();
  if (numFields > 1) {
    MWAW_DEBUG_MSG(("ZWrtParser::readBarState: find extra fields\n"));
  }
  for (size_t ff = 1; ff < numFields; ff++) {
    if (fields[ff].getDebugString(input, res))
      f << "#f" << ff << "=\"" << res << "\",";
    else
      f << "#f" << ff << ",";
  }

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool ZWrtParser::readHTMLPref(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readHTMLPref: the entry is bad\n"));
    return false;
  }

  if (entry.id()!=128) {
    MWAW_DEBUG_MSG(("ZWrtParser::readHTMLPref: the entry id is odd\n"));
  }
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields)) {
    MWAW_DEBUG_MSG(("ZWrtParser::readHTMLPref: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  size_t numFields = fields.size();
  if (numFields < 4) {
    MWAW_DEBUG_MSG(("ZWrtParser::readHTMLPref: the fields list seems very short\n"));
  }
  std::string strVal;
  bool boolVal;
  for (size_t ff = 0; ff < numFields; ff++) {
    ZWField const &field = fields[ff];
    bool done = false;
    switch (ff) {
    case 0:
    case 1:
    case 2: // always true?
    case 3: // true if name?
      done = field.getBool(input, boolVal);
      if (!done || !boolVal)
        break;
      f << "f" << ff << "Set,";
      break;
    case 4: // find one time sidebar
      done = field.getString(input, strVal);
      if (!done||!strVal.length())
        break;
      f << "name=" << strVal << ",";
      break;
    default:
      break;
    }
    if (done)
      continue;
    if (fields[ff].getDebugString(input, strVal))
      f << "#f" << ff << "=\"" << strVal << "\",";
    else
      f << "#f" << ff << ",";
  }
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool ZWrtParser::readSectionRange(MWAWEntry const &entry)
{
  long pos = entry.begin();
  if (pos <= 0) {
    MWAW_DEBUG_MSG(("ZWrtParser::readSectionRange: the entry is bad\n"));
    return false;
  }

  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  if (entry.length() <= 0) {
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  pos -= 4;
  std::string name("");
  int num=0;
  while (!input->isEnd()) {
    bool done=input->tell()>=entry.end();
    char c = done ? char(0xa) : char(input->readULong(1));
    if (c==0) {
      MWAW_DEBUG_MSG(("ZWrtParser::readSectionRange: find a 0 char\n"));
      name +="##[0]";
      continue;
    }
    if (c!=0xa) {
      name += c;
      continue;
    }

    f << name;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    pos = input->tell();
    name = "";
    f.str("");
    f << entry.type() << "-" << ++num << ":";
    if (done)
      break;
  }
  if (name.length()) {
    f << name;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool ZWrtParser::readWindowPos(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readWindowPos: the entry is bad\n"));
    return false;
  }

  if (entry.id()!=128) {
    MWAW_DEBUG_MSG(("ZWrtParser::readWindowPos: the entry id is odd\n"));
  }
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields)) {
    MWAW_DEBUG_MSG(("ZWrtParser::readWindowPos: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  size_t numFields = fields.size();
  if (numFields < 6) {
    MWAW_DEBUG_MSG(("ZWrtParser::readWindowPos: the fields list seems very short\n"));
  }
  std::string strVal;
  int intVal;
  int dim[4]= {0,0,0,0};
  for (size_t ff = 0; ff < numFields; ff++) {
    ZWField const &field = fields[ff];
    bool done = false;
    switch (ff) {
    case 0:
    case 1:
    case 2:
    case 3:
      done = field.getInt(input, intVal);
      if (!done)
        break;
      dim[ff]=intVal;
      break;
    case 4: // 137|139|144
    case 5: // 0|3|9|12 ( actual section ?)
      done = field.getInt(input, intVal);
      if (!done||!intVal)
        break;
      f << "f" << ff << "=" << intVal << ",";
      break;
    default:
      break;
    }
    if (done)
      continue;
    if (fields[ff].getDebugString(input, strVal))
      f << "#f" << ff << "=\"" << strVal << "\",";
    else
      f << "#f" << ff << ",";
  }
  f << "pos=(" << dim[0] << "x" << dim[1] << "<->" << dim[2] << "x" << dim[3] << "),";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// read a section zone ...
////////////////////////////////////////////////////////////

// read a cursor position in a section ?
bool ZWrtParser::readCPos(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readCPos: the entry is bad\n"));
    return false;
  }

  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields) || !fields.size()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readCPos: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int intVal;
  size_t ff=0;
  if (fields[ff++].getInt(input, intVal)) {
    if (intVal)
      f << "cPos=" << intVal << ",";
  }
  else {
    MWAW_DEBUG_MSG(("ZWrtParser::readCPos: can not read cursor pos\n"));
    ff = 0;
  }
  size_t numFields = fields.size();
  if (numFields > 1) {
    MWAW_DEBUG_MSG(("ZWrtParser::readCPos: find extra fields\n"));
  }
  std::string res;
  for (; ff < numFields; ff++) {
    if (fields[ff].getDebugString(input, res))
      f << "#f" << ff << "=\"" << res << "\",";
    else
      f << "#f" << ff << ",";
  }

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

// read a cursor position in a section ?
bool ZWrtParser::readSLen(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readSLen: the entry is bad\n"));
    return false;
  }

  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields) || !fields.size()) {
    MWAW_DEBUG_MSG(("ZWrtParser::readSLen: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int intVal;
  size_t ff=0;
  if (fields[ff++].getInt(input, intVal)) {
    if (intVal)
      f << "len?=" << intVal << ",";
  }
  else {
    MWAW_DEBUG_MSG(("ZWrtParser::readSLen: can not read cursor pos\n"));
    ff = 0;
  }
  size_t numFields = fields.size();
  if (numFields > 1) {
    MWAW_DEBUG_MSG(("ZWrtParser::readSLen: find extra fields\n"));
  }
  std::string res;
  for (; ff < numFields; ff++) {
    if (fields[ff].getDebugString(input, res))
      f << "#f" << ff << "=\"" << res << "\",";
    else
      f << "#f" << ff << ",";
  }

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// read a generic zone ...
////////////////////////////////////////////////////////////
bool ZWrtParser::readUnknownZone(MWAWEntry const &entry)
{
  if (entry.begin() <= 0) {
    MWAW_DEBUG_MSG(("ZWrtParser::readUnknownZone: the entry is bad\n"));
    return false;
  }

  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  long pos = entry.begin();

  libmwaw::DebugStream f;
  f << "Entries(" << entry.type() << ")[" << entry << "]:";
  entry.setParsed(true);

  std::vector<ZWField> fields;
  if (!getFieldList(entry, fields)) {
    MWAW_DEBUG_MSG(("ZWrtParser::readUnknownZone: can not get fields list\n"));
    f << "###";
    ascFile.addPos(pos-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::string res("");
  size_t numFields = fields.size();
  for (size_t ff = 0; ff < numFields; ff++) {
    if (fields[ff].getDebugString(input, res))
      f << "f" << ff << "=\"" << res << "\",";
    else
      f << "#f" << ff << ",";
  }

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
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
bool ZWrtParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = ZWrtParserInternal::State();
  if (!getRSRCParser())
    return false;
  // check if the RANG section exists
  MWAWEntry entry = getRSRCParser()->getEntry("RANG", 128);
  if (entry.begin()<=0) { // length can be 0, so ...
    MWAW_DEBUG_MSG(("ZWrtParser::checkHeader: can not find the RANG[128] resource\n"));
    return false;
  }
  if (getInput()->hasDataFork() && getInput()->size()>0) {
    MWAW_DEBUG_MSG(("ZWrtParser::checkHeader: find some data fork\n"));
    if (strict)
      return false;
  }
  if (header)
    header->reset(MWAWDocument::MWAW_T_ZWRITE, 1);

  return true;
}

////////////////////////////////////////////////////////////
// read a list of field
////////////////////////////////////////////////////////////
bool ZWrtParser::getFieldList(MWAWEntry const &entry, std::vector<ZWField> &list)
{
  list.resize(0);
  MWAWInputStreamPtr input = rsrcInput();
  long pos=entry.begin();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    long actPos=input->tell();
    long done = actPos>=entry.end();
    char c= done ? '\t' : char(input->readULong(1));
    if (c=='\t') {
      ZWField field;
      field.m_pos.setBegin(pos);
      field.m_pos.setEnd(actPos);
      pos = actPos+1;
      list.push_back(field);
      if (done)
        return true;
      continue;
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
// field function
////////////////////////////////////////////////////////////
bool ZWField::getString(MWAWInputStreamPtr &input, std::string &str) const
{
  str="";
  if (!m_pos.valid())
    return true;
  input->seek(m_pos.begin(), librevenge::RVNG_SEEK_SET);
  while (!input->isEnd() && input->tell()!=m_pos.end()) {
    auto c=char(input->readULong(1));
    if (c==0) {
      MWAW_DEBUG_MSG(("ZWField::getString::readFieldString: find a zero entry\n"));
      str += "##[0]";
      continue;
    }
    str += c;
  }
  return true;
}

bool ZWField::getDebugString(MWAWInputStreamPtr &input, std::string &str) const
{
  str="";
  if (!m_pos.valid())
    return true;
  input->seek(m_pos.begin(), librevenge::RVNG_SEEK_SET);
  std::stringstream ss;
  while (!input->isEnd() && input->tell()!=m_pos.end()) {
    auto c=char(input->readULong(1));
    if (static_cast<unsigned char>(c)<=0x1f && c!= 0x9)
      ss << "##[" << std::hex << int(c) << std::dec << "]";
    else
      ss << c;
  }
  str = ss.str();
  return true;
}

bool ZWField::getBool(MWAWInputStreamPtr &input, bool &val) const
{
  val = false;
  if (m_pos.length()==0 && m_pos.begin()>0)
    return true;
  std::string str;
  if (!getString(input,str) || str.length() != 1) {
    MWAW_DEBUG_MSG(("ZWField::getBool: can not read field\n"));
    return false;
  }
  if (str[0]=='T')
    val = true;
  else if (str[0]=='F')
    val = false;
  else {
    MWAW_DEBUG_MSG(("ZWField::getBool: find unexpected char %x\n", static_cast<unsigned int>(str[0])));
    return false;
  }
  return true;
}

bool ZWField::getInt(MWAWInputStreamPtr &input, int &val) const
{
  val = 0;
  std::string str;
  if (!getString(input,str) || str.length() == 0) {
    MWAW_DEBUG_MSG(("ZWField::getInt: can not read field\n"));
    return false;
  }
  int sign = 1;
  size_t numChar = str.length();
  size_t p = 0;
  if (str[0]=='-') {
    sign = -1;
    p++;
  }
  while (p < numChar) {
    char c = str[p++];
    if (c>='0' && c <= '9') {
      val = 10*val+(c-'0');
      continue;
    }
    MWAW_DEBUG_MSG(("ZWField::getInt: find unexpected char %x\n", static_cast<unsigned int>(c)));
    val *= sign;
    return false;
  }
  val *= sign;
  return true;
}

bool ZWField::getFloat(MWAWInputStreamPtr &input, float &val) const
{
  val = 0;
  std::string str;
  if (!getString(input,str) || str.length() == 0) {
    MWAW_DEBUG_MSG(("ZWField::getFloat: can not read field\n"));
    return false;
  }
  std::stringstream ss;
  ss << str;
  ss >> val;
  return !(!ss);
}

bool ZWField::getIntList(MWAWInputStreamPtr &input, std::vector<int> &list) const
{
  list.resize(0);
  std::string str;
  if (!getString(input,str) || str.length() == 0) {
    MWAW_DEBUG_MSG(("ZWField::getIntList: can not read field\n"));
    return false;
  }
  int sign = 1, val=0;
  size_t numChar = str.length();
  size_t p = 0;
  while (p <= numChar) {
    if (p==numChar) {
      list.push_back(sign*val);
      break;
    }
    char c = str[p++];
    if (c==',') {
      list.push_back(sign*val);
      val = 0;
      sign = 1;
      continue;
    }
    if (c=='-') {
      if (val != 0 || sign != 1) {
        MWAW_DEBUG_MSG(("ZWField::getIntList: find a int inside a word\n"));
        return list.size();
      }
      sign = -1;
    }
    if (c>='0' && c <= '9') {
      val = 10*val+(c-'0');
      continue;
    }
    MWAW_DEBUG_MSG(("ZWField::getIntList: find unexpected char %x\n", static_cast<unsigned int>(c)));
    return list.size();
  }
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
