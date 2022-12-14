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

/* Inspired of TN-012-Disk-Based-MW-Format.txt */

#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "MacWrtParser.hxx"

/** Internal: the structures of a MacWrtParser */
namespace MacWrtParserInternal
{

//! Document header
struct FileHeader {
  FileHeader()
    : m_hideFirstPageHeaderFooter(false)
    , m_startNumberPage(1)
    , m_freeListPos(0)
    , m_freeListLength(0)
    , m_freeListAllocated(0)
    , m_dataPos(0)
  {
    for (auto &num : m_numParagraphs) num = 0;
  }

  friend std::ostream &operator<<(std::ostream &o, FileHeader const &header);

  //! the number of lines : text, header footer
  int m_numParagraphs[3];
  //! true if the first page header/footer must be draw
  bool m_hideFirstPageHeaderFooter;
  //! the first number page
  int m_startNumberPage;
  //! free list start position
  long m_freeListPos;
  //! free list length
  long m_freeListLength;
  //! free list allocated
  long m_freeListAllocated;
  //! the begin of data ( if version == 3)
  long m_dataPos;
};

std::ostream &operator<<(std::ostream &o, FileHeader const &header)
{
  for (int i=0; i < 3; i++) {
    if (!header.m_numParagraphs[i]) continue;
    o << "numParagraph";
    if (i==1) o << "[header]";
    else if (i==2) o << "[footer]";
    o << "=" << header.m_numParagraphs[i] << ",";
  }
  if (header.m_hideFirstPageHeaderFooter)
    o << "noHeaderFooter[FirstPage],";
  if (header.m_startNumberPage != 1)
    o << "firstPageNumber=" << header.m_startNumberPage << ",";
  if (header.m_freeListPos) {
    o << "FreeList=" << std::hex
      << header.m_freeListPos
      << "[" << header.m_freeListLength << "+" << header.m_freeListAllocated << "],"
      << std::dec << ",";
  }
  if (header.m_dataPos)
    o << "DataPos="  << std::hex << header.m_dataPos << std::dec << ",";

  return o;
}

////////////////////////////////////////
//! the paragraph... information
struct Information {
  /** the different type */
  enum Type { TEXT, RULER, GRAPHIC, PAGEBREAK, UNKNOWN };

  //! constructor
  Information()
    : m_type(UNKNOWN)
    , m_compressed(false)
    , m_pos()
    , m_height(0)
    , m_justify(MWAWParagraph::JustificationLeft)
    , m_justifySet(false)
    , m_data()
    , m_font()
  {
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Information const &info);

  //! the type
  Type m_type;

  //! a flag to know if the text data are compressed
  bool m_compressed;

  //! top left position
  MWAWPosition m_pos;

  //! the paragraph height
  int m_height;

  //! paragraph justification : MWAW_PARAGRAPH_JUSTIFICATION*
  MWAWParagraph::Justification m_justify;

  //! true if the justification must be used
  bool m_justifySet;
  //! the position in the file
  MWAWEntry m_data;

  //! the font
  MWAWFont m_font;
};

std::ostream &operator<<(std::ostream &o, Information const &info)
{
  switch (info.m_type) {
  case Information::TEXT:
    o << "text";
    if (info.m_compressed) o << "[compressed]";
    o << ",";
    break;
  case Information::RULER:
    o << "indent,";
    break;
  case Information::GRAPHIC:
    o << "graphics,";
    break;
  case Information::PAGEBREAK:
    o << "pageBreak,";
    break;
  case Information::UNKNOWN:
#if !defined(__clang__)
  default:
#endif
    o << "###unknownType,";
    break;
  }
  o << info.m_pos << ",";
  if (info.m_height) o << "height=" << info.m_height << ",";

  if (info.m_justifySet) {
    switch (info.m_justify) {
    case MWAWParagraph::JustificationLeft:
      o << "left[justify],";
      break;
    case MWAWParagraph::JustificationCenter:
      o << "center[justify],";
      break;
    case MWAWParagraph::JustificationRight:
      o << "right[justify],";
      break;
    case MWAWParagraph::JustificationFull:
      o << "full[justify],";
      break;
    case MWAWParagraph::JustificationFullAllLines:
      o << "fullAllLines[justify],";
      break;
#if !defined(__clang__)
    default:
      o << "###unknown[justify],";
      break;
#endif
    }
  }
  if (info.m_data.begin() > 0)
    o << std::hex << "data=[" << info.m_data.begin() << "-" << info.m_data.end() << "]," << std::dec;
  return o;
}

////////////////////////////////////////
//! the windows structure
struct WindowsInfo {
  WindowsInfo()
    : m_startSel()
    , m_endSel()
    , m_posTopY(0)
    , m_informations()
    , m_firstParagLine()
    , m_linesHeight()
    , m_pageNumber()
    , m_date()
    , m_time()
  {
  }

  /** small function used to recognized empty header or footer */
  bool isEmpty() const
  {
    if (m_informations.size() == 0) return true;
    if (m_pageNumber.x() >= 0 || m_date.x() >= 0 || m_time.x() >= 0)
      return false;
    if (m_informations.size() > 2) return false;
    for (auto const &info : m_informations) {
      switch (info.m_type) {
      case Information::GRAPHIC:
        return false;
      case Information::TEXT:
        if (info.m_data.length() != 10)
          return false;
        // empty line : ok
        break;
      case Information::RULER:
      case Information::PAGEBREAK:
      case Information::UNKNOWN:
#if !defined(__clang__)
      default:
#endif
        break;
      }
    }
    return true;
  }

  friend std::ostream &operator<<(std::ostream &o, WindowsInfo const &w);

  MWAWVec2i m_startSel, m_endSel; // start end selection (parag, char)
  int m_posTopY;
  std::vector<Information> m_informations;
  std::vector<int> m_firstParagLine, m_linesHeight;
  MWAWVec2i m_pageNumber, m_date, m_time;
};

std::ostream &operator<<(std::ostream &o, WindowsInfo const &w)
{
  o << "sel=[" << w.m_startSel << "-" << w.m_endSel << "],";
  if (w.m_posTopY) o << "windowsY=" << w.m_posTopY << ",";
  o << "pageNumberPos=" << w.m_pageNumber << ",";
  o << "datePos=" << w.m_date << ",";
  o << "timePos=" << w.m_time << ",";
  return o;
}

////////////////////////////////////////
//! Internal: the state of a MacWrtParser
struct State {
  //! constructor
  State()
    : m_compressCorr(" etnroaisdlhcfp")
    , m_actPage(0)
    , m_numPages(0)
    , m_fileHeader()
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }

  //! the correspondance between int compressed and char : must be 15 character
  std::string m_compressCorr;

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  //! the header
  FileHeader m_fileHeader;

  //! the information of main document, header, footer
  WindowsInfo m_windows[3];

  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

////////////////////////////////////////
//! Internal: the subdocument of a MacWrtParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(MacWrtParser &pars, MWAWInputStreamPtr const &input, int zoneId)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_id(zoneId)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the subdocument id
  int m_id;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("MacWrtParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (m_id != 1 && m_id != 2) {
    MWAW_DEBUG_MSG(("MacWrtParserInternal::SubDocument::parse: unknown zone\n"));
    return;
  }
  auto *parser=dynamic_cast<MacWrtParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("MacWrtParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendWindow(m_id);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_id != sDoc->m_id) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MacWrtParser::MacWrtParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

MacWrtParser::~MacWrtParser()
{
}

void MacWrtParser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new MacWrtParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void MacWrtParser::newPage(int number)
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
void MacWrtParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    if (getRSRCParser()) {
      MWAWEntry corrEntry = getRSRCParser()->getEntry("STR ", 700);
      std::string corrString("");
      if (corrEntry.valid() && getRSRCParser()->parseSTR(corrEntry, corrString)) {
        if (corrString.length() != 15) {
          MWAW_DEBUG_MSG(("MacWrtParser::parse: resource correspondance string seems bad\n"));
        }
        else
          m_state->m_compressCorr = corrString;
      }
    }
    ok = (version() <= 3) ? createZonesV3() : createZones();
    if (ok) {
      createDocument(docInterface);
      sendWindow(0);
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MacWrtParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MacWrtParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("MacWrtParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  for (int i = 1; i < 3; i++) {
    if (m_state->m_windows[i].isEmpty()) {
#ifdef DEBUG
      sendWindow(i); // force the parsing
#endif
      continue;
    }
    MWAWHeaderFooter hF((i==1) ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    hF.m_subDocument.reset(new MacWrtParserInternal::SubDocument(*this, getInput(), i));
    ps.setHeaderFooter(hF);
  }

  std::vector<MWAWPageSpan> pageList;
  if (m_state->m_fileHeader.m_hideFirstPageHeaderFooter) {
    pageList.push_back(getPageSpan());
    ps.setPageSpan(m_state->m_numPages);
  }
  else
    ps.setPageSpan(m_state->m_numPages+1);
  if (ps.getPageSpan())
    pageList.push_back(ps);
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
bool MacWrtParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();

  if (!readPrintInfo()) {
    // bad sign, but we can try to recover
    ascii().addPos(pos);
    ascii().addNote("###PrintInfo");
    input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  for (int i = 0; i < 3; i++) {
    if (readWindowsInfo(i))
      continue;
    if (i == 2) return false; // problem on the main zone, better quit

    // reset state
    m_state->m_windows[2-i] = MacWrtParserInternal::WindowsInfo();
    int const windowsSize = 46;

    // and try to continue
    input->seek(pos+(i+1)*windowsSize, librevenge::RVNG_SEEK_SET);
  }

#ifdef DEBUG
  checkFreeList();
#endif

  // ok, we can find calculate the number of pages and the header and the footer height
  for (int i = 1; i < 3; i++) {
    auto const &info = m_state->m_windows[i];
    if (info.isEmpty()) // avoid reserving space for empty header/footer
      continue;
    int height = 0;
    for (auto const &inf : info.m_informations)
      height+=inf.m_height;
    if (i == 1) m_state->m_headerHeight = height;
    else m_state->m_footerHeight = height;
  }
  int numPages = 0;
  auto const &mainInfo = m_state->m_windows[0];
  for (auto &info : mainInfo.m_informations) {
    if (info.m_pos.page() > numPages)
      numPages = info.m_pos.page();
  }
  m_state->m_numPages = numPages+1;

  return true;
}

bool MacWrtParser::createZonesV3()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();

  if (!readPrintInfo()) {
    // bad sign, but we can try to recover
    ascii().addPos(pos);
    ascii().addNote("###PrintInfo");
    input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  for (int i = 0; i < 3; i++) {
    if (readWindowsInfo(i))
      continue;
    if (i == 2) return false; // problem on the main zone, better quit

    // reset state
    m_state->m_windows[2-i] = MacWrtParserInternal::WindowsInfo();
    int const windowsSize = 34;

    // and try to continue
    input->seek(pos+(i+1)*windowsSize, librevenge::RVNG_SEEK_SET);
  }

  auto const &header = m_state->m_fileHeader;

  for (int i = 0; i < 3; i++) {
    if (!readInformationsV3
        (header.m_numParagraphs[i], m_state->m_windows[i].m_informations))
      return false;
  }
  if (int(input->tell()) != header.m_dataPos) {
    MWAW_DEBUG_MSG(("MacWrtParser::createZonesV3: pb with dataPos\n"));
    ascii().addPos(input->tell());
    ascii().addNote("###FileHeader");

    // posibility to do very bad thing from here, so we stop
    if (int(input->tell()) > header.m_dataPos)
      return false;

    // and try to continue
    input->seek(header.m_dataPos, librevenge::RVNG_SEEK_SET);
    if (int(input->tell()) != header.m_dataPos)
      return false;
  }
  for (int z = 0; z < 3; z++) {
    int numParag = header.m_numParagraphs[z];
    auto &wInfo = m_state->m_windows[z];
    for (int p = 0; p < numParag; p++) {
      pos = input->tell();
      auto type = static_cast<int>(input->readLong(2));
      auto sz = static_cast<int>(input->readLong(2));
      input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
      if (sz < 0 || long(input->tell()) !=  pos+4+sz) {
        MWAW_DEBUG_MSG(("MacWrtParser::createZonesV3: pb with dataZone\n"));
        return (p != 0);
      }
      MWAWEntry entry;
      entry.setBegin(pos+4);
      entry.setLength(sz);
      if (int(wInfo.m_informations.size()) <= p)
        continue;
      wInfo.m_informations[size_t(p)].m_data = entry;
      auto newType = MacWrtParserInternal::Information::UNKNOWN;

      switch ((type & 0x7)) {
      case 0:
        newType=MacWrtParserInternal::Information::RULER;
        break;
      case 1:
        newType=MacWrtParserInternal::Information::TEXT;
        break;
      case 2:
        newType=MacWrtParserInternal::Information::PAGEBREAK;
        break;
      default:
        break;
      }
      if (newType != wInfo.m_informations[size_t(p)].m_type) {
        MWAW_DEBUG_MSG(("MacWrtParser::createZonesV3: types are inconstant\n"));
        if (newType != MacWrtParserInternal::Information::UNKNOWN)
          wInfo.m_informations[size_t(p)].m_type = newType;
      }
    }
  }
  if (!input->isEnd()) {
    ascii().addPos(input->tell());
    ascii().addNote("Entries(END)");
  }

  int numPages = 0;
  auto const &mainInfo = m_state->m_windows[0];
  for (auto const &info : mainInfo.m_informations) {
    if (info.m_pos.page() > numPages)
      numPages = info.m_pos.page();
  }
  m_state->m_numPages = numPages+1;
  return true;
}

bool MacWrtParser::sendWindow(int zone)
{
  if (zone < 0 || zone >= 3) {
    MWAW_DEBUG_MSG(("MacWrtParser::sendWindow: invalid zone %d\n", zone));
    return false;
  }

  auto const &info = m_state->m_windows[zone];
  size_t numInfo = info.m_informations.size();
  auto numPara = int(info.m_firstParagLine.size());

  if (version() <= 3 && zone == 0)
    newPage(1);
  for (size_t i=0; i < numInfo; i++) {
    if (zone == 0)
      newPage(info.m_informations[i].m_pos.page()+1);
    switch (info.m_informations[i].m_type) {
    case MacWrtParserInternal::Information::TEXT:
      if (!zone || info.m_informations[i].m_data.length() != 10) {
        std::vector<int> lineHeight;
        if (int(i) < numPara) {
          int firstLine = info.m_firstParagLine[i];
          int lastLine = (int(i+1) < numPara) ?  info.m_firstParagLine[i+1] : int(info.m_linesHeight.size());
          for (int line = firstLine; line < lastLine; line++)
            lineHeight.push_back(info.m_linesHeight[size_t(line)]);
        }
        readText(info.m_informations[i], lineHeight);
      }
      break;
    case MacWrtParserInternal::Information::RULER:
      readParagraph(info.m_informations[i]);
      break;
    case MacWrtParserInternal::Information::GRAPHIC:
      readGraphic(info.m_informations[i]);
      break;
    case MacWrtParserInternal::Information::PAGEBREAK:
      readPageBreak(info.m_informations[i]);
      if (zone == 0 && version() <= 3)
        newPage(info.m_informations[i].m_pos.page()+2);
      break;
    case MacWrtParserInternal::Information::UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      break;
    }
  }
  if (getTextListener() && zone) {
    // FIXME: try to insert field in the good place
    if (info.m_pageNumber.x() >= 0 && info.m_pageNumber.y() >= 0)
      getTextListener()->insertField(MWAWField(MWAWField::PageNumber));
    if (info.m_date.x() >= 0 && info.m_date.y() >= 0)
      getTextListener()->insertField(MWAWField(MWAWField::Date));
    if (info.m_time.x() >= 0 && info.m_time.y() >= 0)
      getTextListener()->insertField(MWAWField(MWAWField::Time));
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
bool MacWrtParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = MacWrtParserInternal::State();
  MacWrtParserInternal::FileHeader fHeader = m_state->m_fileHeader;

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  int headerSize=40;
  if (!input->checkPosition(headerSize)) {
    MWAW_DEBUG_MSG(("MacWrtParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);

  auto vers = static_cast<int>(input->readULong(2));
  setVersion(vers);

  std::string vName("");

  switch (vers) {
  case 3:
    vName="v1.0-2.2";
    break;
  case 6: // version 4.5 ( also version 5.01 of Claris MacWrite )
    vName="v4.5-5.01";
    break;
  default:
    MWAW_DEBUG_MSG(("MacWrtParser::checkHeader: unknown version\n"));
    return false;
  }
  if (vName.empty()) {
    MWAW_DEBUG_MSG(("Maybe a MacWrite file unknown version(%d)\n", vers));
  }
  else {
    MWAW_DEBUG_MSG(("MacWrite file %s\n", vName.c_str()));
  }

  f << "FileHeader: vers=" << vers << ",";

  if (vers <= 3) fHeader.m_dataPos = static_cast<int>(input->readULong(2));

  for (int &numParagraph : fHeader.m_numParagraphs) {
    auto numParag = static_cast<int>(input->readLong(2));
    numParagraph = numParag;
    if (numParag < 0) {
      MWAW_DEBUG_MSG(("MacWrtParser::checkHeader: numParagraphs is negative : %d\n",
                      numParag));
      return false;
    }
  }

  if (vers <= 3) {
    input->seek(6, librevenge::RVNG_SEEK_CUR); // unknown
    if (input->readLong(1)) f << "hasFooter(?);";
    if (input->readLong(1)) f << "hasHeader(?),";
    fHeader.m_startNumberPage = static_cast<int>(input->readLong(2));
    headerSize=20;
  }
  else {
    fHeader.m_hideFirstPageHeaderFooter = (input->readULong(1)==0xFF);

    input->seek(7, librevenge::RVNG_SEEK_CUR); // unused + 4 display flags + active doc
    fHeader.m_startNumberPage = static_cast<int>(input->readLong(2));
    fHeader.m_freeListPos = long(input->readULong(4));
    fHeader.m_freeListLength = static_cast<int>(input->readULong(2));
    fHeader.m_freeListAllocated = static_cast<int>(input->readULong(2));
    // 14 unused
  }
  f << fHeader;

  //
  input->seek(headerSize, librevenge::RVNG_SEEK_SET);
  if (!readPrintInfo()) {
    input->seek(headerSize, librevenge::RVNG_SEEK_SET);
    if (input->readLong(2)) return false;  // allow iPrVersion to be zero
    input->seek(headerSize+0x78, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<3; ++i)
      if (!readWindowsInfo(i) && i==2) return false;
  }
  if (!input->checkPosition(vers <= 3 ? fHeader.m_dataPos : fHeader.m_freeListPos))
    return false;

  input->seek(headerSize, librevenge::RVNG_SEEK_SET);
  m_state->m_fileHeader = fHeader;

  // ok, we can finish initialization
  if (header)
    header->reset(MWAWDocument::MWAW_T_MACWRITE, version());

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  ascii().addPos(headerSize);

  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool MacWrtParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  libmwaw::DebugStream f;
  // print info
  libmwaw::PrinterInfo info;
  if (!info.read(input)) return false;
  f << "Entries(PrintInfo):"<< info;

  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) return false;

  // define margin from print info
  MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
  MWAWVec2i rBotMargin=info.paper().pos(1) - info.page().pos(1);

  // move margin left | top
  int decalX = lTopMargin.x() > 14 ? lTopMargin.x()-14 : 0;
  int decalY = lTopMargin.y() > 14 ? lTopMargin.y()-14 : 0;
  lTopMargin -= MWAWVec2i(decalX, decalY);
  rBotMargin += MWAWVec2i(decalX, decalY);

  // decrease right | bottom
  int rightMarg = rBotMargin.x() -10;
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
  input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != pos+0x78) {
    MWAW_DEBUG_MSG(("MacWrtParser::readPrintInfo: file is too short\n"));
    return false;
  }
  ascii().addPos(input->tell());

  return true;
}

////////////////////////////////////////////////////////////
// read the windows info
////////////////////////////////////////////////////////////
bool MacWrtParser::readWindowsInfo(int wh)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  int windowsSize = version() <= 3 ? 34 : 46;

  input->seek(pos+windowsSize, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) !=pos+windowsSize) {
    MWAW_DEBUG_MSG(("MacWrtParser::readWindowsInfo: file is too short\n"));
    return false;
  }

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Windows)";
  switch (wh) {
  case 0:
    f << "[Footer]";
    break;
  case 1:
    f << "[Header]";
    break;
  case 2:
    break;
  default:
    MWAW_DEBUG_MSG(("MacWrtParser::readWindowsInfo: called with bad which=%d\n",wh));
    return false;
  }

  int which = 2-wh;
  auto &info = m_state->m_windows[which];
  f << ": ";

  MWAWEntry informations;
  MWAWEntry lineHeightEntry;

  for (int i = 0; i < 2; i++) {
    auto x = static_cast<int>(input->readLong(2));
    auto y = static_cast<int>(input->readLong(2));
    if (i == 0) info.m_startSel = MWAWVec2i(x,y);
    else info.m_endSel = MWAWVec2i(x,y);
  }

  if (version() <= 3) {
    for (int i = 0; i < 2; i++) {
      auto val = static_cast<int>(input->readLong(2));
      if (val) f << "unkn" << i << "=" << val << ",";
    }
  }
  else {
    info.m_posTopY = static_cast<int>(input->readLong(2));
    input->seek(2,librevenge::RVNG_SEEK_CUR); // need to redraw
    informations.setBegin(long(input->readULong(4)));
    informations.setLength(long(input->readULong(2)));
    informations.setId(which);

    lineHeightEntry.setBegin(long(input->readULong(4)));
    lineHeightEntry.setLength(long(input->readULong(2)));
    lineHeightEntry.setId(which);

    f << std::hex
      << "lineHeight=[" << lineHeightEntry.begin() << "-" << lineHeightEntry.end() << "],"
      << "informations=[" << informations.begin() << "-" << informations.end() << "],"
      << std::dec;
  }
  for (int i = 0; i < 3; i++) {
    auto x = static_cast<int>(input->readLong(2));
    auto y = static_cast<int>(input->readLong(2));
    if (i == 0) info.m_pageNumber = MWAWVec2i(x,y);
    else if (i == 1) info.m_date = MWAWVec2i(x,y);
    else info.m_time = MWAWVec2i(x,y);
  }
  f << info;
  bool ok=true;
  if (version() <= 3) {
    input->seek(6,librevenge::RVNG_SEEK_CUR); // unknown flags: ff ff ff ff ff 00
    f << "actFont=" << input->readLong(1) << ",";
    for (int i= 0; i < 2; i++) {
      auto val = static_cast<int>(input->readULong(1));
      if (val==255) f << "f" << i << "=true,";
    }
    f << "flg=" << input->readLong(1);
  }
  else {
    input->seek(4,librevenge::RVNG_SEEK_CUR); // unused
    if (input->readULong(1) == 0xFF) f << "redrawOval,";
    if (input->readULong(1) == 0xFF) f << "lastOvalUpdate,";
    f << "actStyle=" << input->readLong(2) << ",";
    f << "actFont=" << input->readLong(2);

    if (!readLinesHeight(lineHeightEntry, info.m_firstParagLine, info.m_linesHeight)) {
      // ok, try to continue without lineHeight
      info.m_firstParagLine.resize(0);
      info.m_linesHeight.resize(0);
    }
    ok = readInformations(informations, info.m_informations);
    if (!ok) info.m_informations.resize(0);
  }

  input->seek(pos+windowsSize, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(input->tell());

  return ok;
}

////////////////////////////////////////////////////////////
// read the lines height
////////////////////////////////////////////////////////////
bool MacWrtParser::readLinesHeight(MWAWEntry const &entry, std::vector<int> &firstParagLine, std::vector<int> &linesHeight)
{
  firstParagLine.resize(0);
  linesHeight.resize(0);

  if (!entry.valid()) return false;

  MWAWInputStreamPtr input = getInput();

  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readLinesHeight: file is too short\n"));
    return false;
  }

  long pos = entry.begin(), endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  int numParag=0;
  while (input->tell() != endPos) {
    pos = input->tell();
    auto sz = static_cast<int>(input->readULong(2));
    if (pos+sz+2 > endPos) {
      MWAW_DEBUG_MSG(("MacWrtParser::readLinesHeight: find odd line\n"));
      ascii().addPos(pos);
      ascii().addNote("Entries(LineHeight):###");
      return false;
    }

    firstParagLine.push_back(int(linesHeight.size()));
    int actHeight = 0;
    bool heightOk = false;
    f.str("");
    f << "Entries(LineHeight)[" << entry.id() << "-" << ++numParag << "]:";
    for (int c = 0; c < sz; c++) {
      auto val = static_cast<int>(input->readULong(1));
      if (val & 0x80) {
        val &= 0x7f;
        if (!heightOk || val==0) {
          MWAW_DEBUG_MSG(("MacWrtParser::readLinesHeight: find factor without height \n"));
          return false;
        }

        for (int i = 0; i < val-1; i++)
          linesHeight.push_back(actHeight);
        if (val != 0x7f) heightOk = false;
        f << "x" << val;
        continue;
      }
      actHeight = val;
      linesHeight.push_back(actHeight);
      heightOk = true;
      if (c) f << ",";
      f << actHeight;
    }

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    if ((sz%2)==1) sz++;
    input->seek(pos+sz+2, librevenge::RVNG_SEEK_SET);
  }
  firstParagLine.push_back(int(linesHeight.size()));

  ascii().addPos(endPos);
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// read the entries
////////////////////////////////////////////////////////////
bool MacWrtParser::readInformationsV3(int numEntries, std::vector<MacWrtParserInternal::Information> &informations)
{
  informations.resize(0);

  if (numEntries < 0) return false;
  if (numEntries == 0) return true;

  MWAWInputStreamPtr input = getInput();

  libmwaw::DebugStream f;
  for (int i = 0; i < numEntries; i++) {
    long pos = input->tell();
    MacWrtParserInternal::Information info;
    f.str("");
    f << "Entries(Information)[" << i+1 << "]:";
    auto height = static_cast<int>(input->readLong(2));
    info.m_height = height;
    if (info.m_height < 0) {
      info.m_height = 0;
      info.m_type = MacWrtParserInternal::Information::PAGEBREAK;
    }
    else if (info.m_height > 0)
      info.m_type = MacWrtParserInternal::Information::TEXT;
    else
      info.m_type = MacWrtParserInternal::Information::RULER;

    auto y = static_cast<int>(input->readLong(2));
    info.m_pos=MWAWPosition(MWAWVec2f(0,float(y)), MWAWVec2f(0, float(height)), librevenge::RVNG_POINT);
    info.m_pos.setPage(static_cast<int>(input->readLong(1)));
    f << info;
    informations.push_back(info);

    f << "unkn1=" << std::hex << input->readULong(2) << std::dec << ",";
    f << "unkn2=" << std::hex << input->readULong(1) << std::dec << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(input->tell());
  ascii().addNote("_");

  return true;
}

////////////////////////////////////////////////////////////
// read the entries
////////////////////////////////////////////////////////////
bool MacWrtParser::readInformations(MWAWEntry const &entry, std::vector<MacWrtParserInternal::Information> &informations)
{
  informations.resize(0);

  if (!entry.valid()) return false;

  MWAWInputStreamPtr input = getInput();

  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readInformations: file is too short\n"));
    return false;
  }

  long pos = entry.begin(), endPos = entry.end();
  if ((endPos-pos)%16) {
    MWAW_DEBUG_MSG(("MacWrtParser::readInformations: entry size is odd\n"));
    return false;
  }
  auto numEntries = int((endPos-pos)/16);
  libmwaw::DebugStream f;

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  for (int i = 0; i < numEntries; i++) {
    pos = input->tell();

    f.str("");
    f << "Entries(Information)[" << entry.id() << "-" << i+1 << "]:";
    MacWrtParserInternal::Information info;
    auto height = static_cast<int>(input->readLong(2));
    if (height < 0) {
      info.m_type = MacWrtParserInternal::Information::GRAPHIC;
      height *= -1;
    }
    else if (height == 0)
      info.m_type = MacWrtParserInternal::Information::RULER;
    else
      info.m_type = MacWrtParserInternal::Information::TEXT;
    info.m_height = height;

    auto y = static_cast<int>(input->readLong(2));
    auto page = static_cast<int>(input->readULong(1));
    input->seek(3, librevenge::RVNG_SEEK_CUR); // unused
    info.m_pos = MWAWPosition(MWAWVec2f(0,float(y)), MWAWVec2f(0, float(height)), librevenge::RVNG_POINT);
    info.m_pos.setPage(page);

    auto paragStatus = static_cast<int>(input->readULong(1));
    switch (paragStatus & 0x3) {
    case 0:
      info.m_justify = MWAWParagraph::JustificationLeft;
      break;
    case 1:
      info.m_justify = MWAWParagraph::JustificationCenter;
      break;
    case 2:
      info.m_justify = MWAWParagraph::JustificationRight;
      break;
    case 3:
      info.m_justify = MWAWParagraph::JustificationFull;
      break;
    default:
      break;
    }
    info.m_compressed = (paragStatus & 0x8);
    info.m_justifySet = (paragStatus & 0x20);

    // other bits used internally
    auto highPos = static_cast<unsigned int>(input->readULong(1));
    info.m_data.setBegin(long(highPos<<16)+long(input->readULong(2)));
    info.m_data.setLength(long(input->readULong(2)));

    auto paragFormat = static_cast<int>(input->readULong(2));
    uint32_t flags = 0;
    // bit 1 = plain
    if (paragFormat&0x2) flags |= MWAWFont::boldBit;
    if (paragFormat&0x4) flags |= MWAWFont::italicBit;
    if (paragFormat&0x8) info.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (paragFormat&0x10) flags |= MWAWFont::embossBit;
    if (paragFormat&0x20) flags |= MWAWFont::shadowBit;
    if (paragFormat&0x40)
      info.m_font.set(MWAWFont::Script::super100());
    if (paragFormat&0x80)
      info.m_font.set(MWAWFont::Script::sub100());
    info.m_font.setFlags(flags);

    int fontSize = 0;
    switch ((paragFormat >> 8) & 7) {
    case 0:
      break;
    case 1:
      fontSize=9;
      break;
    case 2:
      fontSize=10;
      break;
    case 3:
      fontSize=12;
      break;
    case 4:
      fontSize=14;
      break;
    case 5:
      fontSize=18;
      break;
    case 6:
      fontSize=14;
      break;
    default: // rare, but can appears on some empty lines
      MWAW_DEBUG_MSG(("MacWrtParser::readInformations: unknown font size=7\n"));
      f << "##fSize=7,";
    }
    if (fontSize) info.m_font.setSize(float(fontSize));
    if ((paragFormat >> 11)&0x1F) info.m_font.setId((paragFormat >> 11)&0x1F);

    informations.push_back(info);
    f << info;
#ifdef DEBUG
    f << "font=[" << info.m_font.getDebugString(getFontConverter()) << "]";
#endif

    input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  ascii().addPos(endPos);
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// read a text
////////////////////////////////////////////////////////////
bool MacWrtParser::readText(MacWrtParserInternal::Information const &info,
                            std::vector<int> const &lineHeight)
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("MacWrtParser::readText: can not find the listener\n"));
    return false;
  }
  MWAWEntry const &entry = info.m_data;
  if (!entry.valid()) return false;

  MWAWInputStreamPtr input = getInput();
  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readText: file is too short\n"));
    return false;
  }

  long pos = entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Entries(Text):";

  auto numChar = static_cast<int>(input->readULong(2));
  std::string text("");
  if (!info.m_compressed) {
    if (numChar+2 >= entry.length()) {
      MWAW_DEBUG_MSG(("MacWrtParser::readText: text is too long\n"));
      return false;
    }
    for (int i = 0; i < numChar; i++)
      text += char(input->readULong(1));
  }
  else {
    std::string const &compressCorr = m_state->m_compressCorr;

    int actualChar = 0;
    bool actualCharSet = false;

    for (int i = 0; i < numChar; i++) {
      int highByte = 0;
      for (int st = 0; st < 3; st++) {
        int actVal;
        if (!actualCharSet) {
          if (long(input->tell()) >= entry.end()) {
            MWAW_DEBUG_MSG(("MacWrtParser::readText: text is too long\n"));
            return false;
          }
          actualChar = static_cast<int>(input->readULong(1));
          actVal = (actualChar >> 4);
        }
        else
          actVal = (actualChar & 0xf);
        actualCharSet = !actualCharSet;
        if (st == 0) {
          if (actVal == 0xf) continue;
          text += compressCorr[size_t(actVal)];
          break;
        }
        if (st == 1) { // high bytes
          highByte = (actVal<<4);
          continue;
        }
        text += char(highByte | actVal);
      }
    }
  }
  f << "'" << text << "'";

  long actPos = input->tell();
  if ((actPos-pos)%2==1) {
    input->seek(1,librevenge::RVNG_SEEK_CUR);
    actPos++;
  }

  auto formatSize = static_cast<int>(input->readULong(2));
  if ((formatSize%6)!=0 || actPos+2+formatSize > entry.end()) {
    MWAW_DEBUG_MSG(("MacWrtParser::readText: format is too long\n"));
    return false;
  }
  int numFormat = formatSize/6;

  std::vector<int> listPos;
  std::vector<MWAWFont> listFonts;

  for (int i = 0; i < numFormat; i++) {
    auto tPos = static_cast<int>(input->readULong(2));

    MWAWFont font;
    font.setSize(float(input->readULong(1)));
    auto flag = static_cast<int>(input->readULong(1));
    uint32_t flags = 0;
    // bit 1 = plain
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x20) font.set(MWAWFont::Script::super100());
    if (flag&0x40) font.set(MWAWFont::Script::sub100());
    font.setFlags(flags);
    font.setId(static_cast<int>(input->readULong(2)));
    listPos.push_back(tPos);
    listFonts.push_back(font);
    f << ",f" << i << "=[pos=" << tPos;
#ifdef DEBUG
    f << ",font=[" << font.getDebugString(getFontConverter()) << "]";
#endif
    f << "]";
  }

  std::vector<int> const *lHeight = &lineHeight;
  int totalHeight = info.m_height;
  std::vector<int> textLineHeight;
  if (version() <= 3) {
    std::vector<int> fParagLines;
    pos = input->tell();
    MWAWEntry hEntry;
    hEntry.setBegin(pos);
    hEntry.setEnd(entry.end());

    if (readLinesHeight(hEntry, fParagLines, textLineHeight)) {
      lHeight = &textLineHeight;
      totalHeight = 0;
      for (auto height : textLineHeight)
        totalHeight+=height;
    }
    else
      input->seek(pos, librevenge::RVNG_SEEK_SET);
  }
  if (long(input->tell()) != entry.end()) {
    f << "#badend";
    ascii().addDelimiter(input->tell(), '|');
  }

  if (getTextListener()) {
    MWAWParagraph para=getTextListener()->getParagraph();
    if (totalHeight && lHeight->size()) // fixme find a way to associate the good size to each line
      para.setInterline(totalHeight/double(lHeight->size()), librevenge::RVNG_POINT);
    else
      para.setInterline(1.2, librevenge::RVNG_PERCENT);
    if (info.m_justifySet)
      para.m_justify=info.m_justify;
    getTextListener()->setParagraph(para);

    if (!numFormat || listPos[0] != 0)
      getTextListener()->setFont(info.m_font);

    int actFormat = 0;
    numChar = int(text.length());
    for (int i = 0; i < numChar; i++) {
      if (actFormat < numFormat && i == listPos[size_t(actFormat)]) {
        getTextListener()->setFont(listFonts[size_t(actFormat)]);
        actFormat++;
      }
      auto c = static_cast<unsigned char>(text[size_t(i)]);
      if (c == 0x9)
        getTextListener()->insertTab();
      else if (c == 0xd)
        getTextListener()->insertEOL();
      else if (c==0x11) // command key (found in some files)
        getTextListener()->insertUnicode(0x2318);
      else if (c==0x14) // apple logo: check me
        getTextListener()->insertUnicode(0xf8ff);
      else if (c<0x1f) {
        // MacWrite allows to add "invalid" characters in the text
        // (and do not display them), this does not imply that the
        // file is invalid...
        MWAW_DEBUG_MSG(("MacWrtParser::readText: find bad character %d at pos=0x%lx\n", int(c), version()<=3 ? entry.begin()-4 : entry.begin()));
        f << "###[" << int(c) << "]";
      }
      else
        getTextListener()->insertCharacter(c);
    }
  }

  ascii().addPos(version()<=3 ? entry.begin()-4 : entry.begin());
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read a paragraph
////////////////////////////////////////////////////////////
bool MacWrtParser::readParagraph(MacWrtParserInternal::Information const &info)
{
  MWAWEntry const &entry = info.m_data;
  if (!entry.valid()) return false;
  if (entry.length() != 34) {
    MWAW_DEBUG_MSG(("MacWrtParser::readParagraph: size is odd\n"));
    return false;
  }

  MWAWParagraph parag;
  MWAWInputStreamPtr input = getInput();

  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readParagraph: file is too short\n"));
    return false;
  }

  long pos = entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Entries(Paragraph):";

  parag.m_margins[1] = double(input->readLong(2))/80.;
  parag.m_margins[2] = double(input->readLong(2))/80.;
  auto justify = static_cast<int>(input->readLong(1));
  switch (justify) {
  case 0:
    parag.m_justify = MWAWParagraph::JustificationLeft;
    break;
  case 1:
    parag.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 2:
    parag.m_justify = MWAWParagraph::JustificationRight;
    break;
  case 3:
    parag.m_justify = MWAWParagraph::JustificationFull;
    break;
  default:
    f << "##justify=" << justify << ",";
    break;
  }
  auto numTabs = static_cast<int>(input->readLong(1));
  if (numTabs < 0 || numTabs > 10) {
    f << "##numTabs=" << numTabs << ",";
    numTabs = 0;
  }
  auto highspacing = static_cast<int>(input->readULong(1));
  if (highspacing==0x80) // 6 by line
    parag.setInterline(12, librevenge::RVNG_POINT);
  else if (highspacing) {
    f << "##highSpacing=" << std::hex << highspacing << std::dec << ",";
    MWAW_DEBUG_MSG(("MacWrtParser::readParagraph: high spacing bit set=%d\n", highspacing));
  }
  auto spacing = static_cast<int>(input->readLong(1));
  if (spacing < 0)
    f << "#interline=" << 1.+spacing/2.0 << ",";
  else if (spacing)
    parag.setInterline(1.+spacing/2.0, librevenge::RVNG_PERCENT);
  parag.m_margins[0] = double(input->readLong(2))/80.;

  parag.m_tabs->resize(size_t(numTabs));
  for (size_t i = 0; i < size_t(numTabs); i++) {
    auto numPixel = static_cast<int>(input->readLong(2));
    auto align = MWAWTabStop::LEFT;
    if (numPixel < 0) {
      align = MWAWTabStop::DECIMAL;
      numPixel *= -1;
    }
    (*parag.m_tabs)[i].m_alignment = align;
    (*parag.m_tabs)[i].m_position = numPixel/72.0;
  }
  *(parag.m_margins[0]) -= parag.m_margins[1].get();
  if (parag.m_margins[2].get() > 0.0)
    parag.m_margins[2]=getPageWidth()-parag.m_margins[2].get()-1.0;
  if (parag.m_margins[2].get() < 0) parag.m_margins[2] = 0;
  f << parag;

  if (getTextListener())
    getTextListener()->setParagraph(parag);
  ascii().addPos(version()<=3 ? pos-4 : pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read the page break
////////////////////////////////////////////////////////////
bool MacWrtParser::readPageBreak(MacWrtParserInternal::Information const &info)
{
  MWAWEntry const &entry = info.m_data;
  if (!entry.valid()) return false;
  if (entry.length() != 21) {
    MWAW_DEBUG_MSG(("MacWrtParser::readPageBreak: size is odd\n"));
    return false;
  }

  MWAWParagraph parag;
  MWAWInputStreamPtr input = getInput();

  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readPageBreak: file is too short\n"));
    return false;
  }

  long pos = entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;

  f << "Entries(PageBreak):";
  for (int i = 0; i < 2; i++) {
    auto val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  int dim[2]= {0,0};
  for (auto &d : dim) d = static_cast<int>(input->readLong(2));
  f << "pageSize(?)=" << dim[0] << "x" << dim[1] << ",";
  f << "unk=" << input->readLong(2) << ","; // find 0xd

  // find MAGICPIC
  std::string name("");
  for (int i = 0; i < 8; i++)
    name += char(input->readULong(1));
  f << name << ",";
  // then I find 1101ff: end of quickdraw pict1 ?
  ascii().addPos(version()<=3 ? pos-4 : pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read a graphic
////////////////////////////////////////////////////////////
bool MacWrtParser::readGraphic(MacWrtParserInternal::Information const &info)
{
  MWAWEntry const &entry = info.m_data;
  if (!entry.valid()) return false;

  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("MacWrtParser::readGraphic: file is too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();

  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != entry.end()-1) {
    MWAW_DEBUG_MSG(("MacWrtParser::readGraphic: file is too short\n"));
    return false;
  }
  long pos = entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  int dim[4];
  for (auto &d : dim) d = static_cast<int>(input->readLong(2));
  if (dim[2] < dim[0] || dim[3] < dim[1]) {
    MWAW_DEBUG_MSG(("MacWrtParser::readGraphic: bdbox is bad\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Graphic):";

  MWAWBox2f box;
  auto res = MWAWPictData::check(input, int(entry.length()-8), box);
  if (res == MWAWPict::MWAW_R_BAD) {
    MWAW_DEBUG_MSG(("MacWrtParser::readGraphic: can not find the picture\n"));
    return false;
  }


  MWAWVec2f actualSize(float(dim[3]-dim[1]), float(dim[2]-dim[0])), naturalSize(actualSize);
  if (box.size().x() > 0 && box.size().y()  > 0) naturalSize = box.size();
  MWAWPosition pictPos=MWAWPosition(MWAWVec2f(0,0),actualSize, librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Char);
  pictPos.setNaturalSize(naturalSize);
  f << pictPos;

  // get the picture
  input->seek(pos+8, librevenge::RVNG_SEEK_SET);

  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(entry.length()-8)));
  if (pict) {
    if (getTextListener()) {
      MWAWParagraph para=getTextListener()->getParagraph();
      para.setInterline(1.0, librevenge::RVNG_PERCENT);
      getTextListener()->setParagraph(para);

      MWAWEmbeddedObject picture;
      if (pict->getBinary(picture) && !picture.m_dataList.empty() && !isMagicPic(picture.m_dataList[0]))
        getTextListener()->insertPicture(pictPos, picture);
      getTextListener()->insertEOL();
#ifdef DEBUG_WITH_FILES
      if (!picture.m_dataList.empty()) {
        static int volatile pictName = 0;
        libmwaw::DebugStream f2;
        f2 << "PICT-" << ++pictName;
        libmwaw::Debug::dumpFile(picture.m_dataList[0], f2.str().c_str());
        ascii().skipZone(pos+8, entry.end()-1);
      }
#endif
    }
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool MacWrtParser::isMagicPic(librevenge::RVNGBinaryData const &dt)
{
  if (dt.size() != 526)
    return false;
  static char const *header="MAGICPIC";
  unsigned char const *dtBuf = dt.getDataBuffer()+514;
  for (int i=0; i < 8; i++)
    if (*(dtBuf++)!=header[i])
      return false;
  return true;
}

////////////////////////////////////////////////////////////
// read the free list
////////////////////////////////////////////////////////////
bool MacWrtParser::checkFreeList()
{
  if (version() <= 3)
    return true;
  MWAWInputStreamPtr input = getInput();
  long pos = m_state->m_fileHeader.m_freeListPos;
  if (!input->checkPosition(pos+m_state->m_fileHeader.m_freeListLength)) {
    MWAW_DEBUG_MSG(("MacWrtParser::checkFreeList: zone is too short\n"));
    return false;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  int N=int(m_state->m_fileHeader.m_freeListLength/8);
  for (int n=0; n<N; ++n) {
    pos=input->tell();
    auto freePos = long(input->readULong(4));
    auto sz = long(input->readULong(4));

    f.str("");
    f << "Entries(FreeList)[" << n << "]:" << std::hex << freePos << "-" << sz << std::dec;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    // if the file end by a free zone, pos+sz can be greater than the file size
    if (!input->checkPosition(freePos+1)) {
      if (!input->checkPosition(freePos)) {
        MWAW_DEBUG_MSG(("MacWrtParser::checkFreeList: bad free block: \n"));
        return false;
      }
      continue;
    }
    f.str("");
    f << "Entries(FreeBlock)[" << n << "]:";
    ascii().addPos(freePos);
    ascii().addNote(f.str().c_str());
  }
  if (m_state->m_fileHeader.m_freeListLength!=m_state->m_fileHeader.m_freeListAllocated) {
    ascii().addPos(input->tell());
    ascii().addNote("Entries(FreeList)[end]:");
  }
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
