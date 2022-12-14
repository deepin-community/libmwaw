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

#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWSection.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "WriterPlsParser.hxx"

/** Internal: the structures of a WriterPlsParser */
namespace WriterPlsParserInternal
{
//! Page informations
struct PageInfo {
  PageInfo()
    : m_firstLine(0)
    , m_height(0)
    , m_heightFromBegin(0)
  {
    std::memset(m_unknown, 0, sizeof(m_unknown));
  }
  friend std::ostream &operator<<(std::ostream &o, PageInfo const &p)
  {
    o << "firstLine=" << p.m_firstLine
      << ", height=" << p.m_height
      << ", height[fromStart]=" << p.m_heightFromBegin;
    if (p.m_unknown[0] != 1) o << ", unkn0=" << p.m_unknown[0];
    if (p.m_unknown[1]) o << ", unkn1=" << p.m_unknown[1];
    return o;
  }

  int m_firstLine;
  int m_unknown[2]; // 1 0
  int m_height, m_heightFromBegin;
};

//! Column informations
struct ColumnInfo {
  ColumnInfo()
    : m_firstLine(0)
    , m_height(0)
    , m_col(0)
    , m_numCol(1)
  {
    std::memset(m_unknown, 0, sizeof(m_unknown));
  }
  friend std::ostream &operator<<(std::ostream &o, ColumnInfo const &c)
  {
    o << "firstLine=" << c.m_firstLine
      << ", col=" << c.m_col << "/" << c.m_numCol
      << ", height=" << c.m_height
      << ", dim?=" << c.m_unknown[3];
    if (c.m_unknown[0]) o << ", unkn0=" << c.m_unknown[0];
    if (c.m_unknown[1] != 1) o << ", unkn1=" << c.m_unknown[1];
    if (c.m_unknown[2]) o << ", unkn2=" << c.m_unknown[2];
    return o;
  }

  int m_firstLine;
  int m_unknown[4]; // 0 1 0
  int m_height, m_col, m_numCol;
};

//! Column informations in a table
struct ColumnTableInfo {
  ColumnTableInfo()
    : m_height(0)
    , m_numData(0)
    , m_flags(0)
  {
    for (auto &col : m_colX) col=0;
    for (auto &text : m_textX) text=0;
  }
  friend std::ostream &operator<<(std::ostream &o, ColumnTableInfo const &c)
  {
    o << "height=" << c.m_height
      << ", numData=" << c.m_numData
      << ", colX=" <<  c.m_colX[0] << "<->" << c.m_colX[1]
      << ", textX=" <<  c.m_textX[0] << "<->" << c.m_textX[1];
    if (c.m_textX[0] !=  c.m_textX[2])
      o << ", textX[begin?]=" << c.m_textX[2];
    if (c.m_flags) o << ", flags=" << c.m_flags;
    return o;
  }

  int m_height;
  int m_numData;
  int m_colX[2]; // x : begin and end pos
  int m_textX[3]; // x : begin|end|begin pos
  int m_flags; // 0
};

//! Paragraph informations
struct ParagraphInfo {
  ParagraphInfo()
    : m_pos(0)
    , m_type(-2)
    , m_height(0)
    , m_height2(0)
    , m_width(0)
    , m_numLines(0)
    , m_linesHeight()
    , m_unknowns()
  {
    for (auto &fl : m_flags) fl=0;
  }
  int getType() const
  {
    if (m_type >= 8) return (m_type & 0x7);
    return m_type;
  }
  friend std::ostream &operator<<(std::ostream &o, ParagraphInfo const &p)
  {
    int type = p.m_type;
    bool typeFlag = false;
    if (type >= 8) {
      typeFlag = true;
      type &= 7;
    }
    switch (type) {
    case 0:
      o << "text";
      break;
    case 1:
      o << "section";
      break;
    case 2:
      o << "text2";
      break;
    case 3:
      o << "colBreak";
      break;
    case 4:
      o << "graphics";
      break;
    case 5:
      o << "table";
      break;

    case -1:
      o << "empty";
      break;
    case -2:
      break;
    default:
      o << "type=" << type;
      break;
    }
    if (typeFlag) o << "[in table],";
    else o << ",";

    if (p.m_pos) o << "pos=" << std::hex << p.m_pos << std::dec << ",";
    o << "h=" << p.m_height << ",";
    if (p.m_height2 != p.m_height) o << "h[next]=" << p.m_height2 << ",";
    if (p.m_width) o << "w=" << p.m_width << ",";
    if (type == 5) {
      o << "numCols=" << p.m_numLines << ",";
      if (!p.m_linesHeight.empty()) {
        o << "numDataByCols=[";
        for (auto i : p.m_linesHeight)
          o << i << ",";
        o << "],";
      }
    }
    else {
      if (p.m_numLines) o << "numLines=" << p.m_numLines << ",";
      if (!p.m_linesHeight.empty()) {
        o << "lineH=[";
        for (auto i : p.m_linesHeight)
          o << i << ",";
        o << "],";
      }
    }
    for (int i= 0; i < 6; i++) {
      if (!p.m_flags[i]) continue;
      o << "f" << i << "=" << std::hex << p.m_flags[i] << std::dec << ",";
    }
    if (p.m_unknowns.size()) {
      o << "unkn=[";
      for (auto unk : p.m_unknowns) {
        if (unk)
          o << unk << ",";
        else
          o << "_,";
      }
      o << "],";
    }
    return o;
  }

  long m_pos;
  int m_type;

  int m_height, m_height2, m_width, m_numLines;
  std::vector<int> m_linesHeight;
  int m_flags[6];
  std::vector<int> m_unknowns;
};

//! Windows informations
struct WindowsInfo {
  struct Zone {
    Zone()
      : m_number(0)
      , m_size(0)
      , m_width(0)
    {
      for (auto &unk : m_unknown) unk = 0;
    }
    bool empty() const
    {
      return m_number==0 && m_size==0;
    }
    friend std::ostream &operator<<(std::ostream &o, Zone const &z)
    {
      o << "N=" << z.m_number << ", sz=" << std::hex << z.m_size << std::dec;
      o << ", w=" << z.m_width;
      for (int i = 0; i < 3; i++) {
        if (!z.m_unknown[i]) continue;
        o << ", f" << i << "=" << z.m_unknown[i];
      }
      return o;
    }
    int m_number;
    int m_size;
    int m_width;
    int m_unknown[3];
  };

  friend std::ostream &operator<<(std::ostream &o, WindowsInfo const &w);

  WindowsInfo()
    : m_pageDim()
    , m_headerY(0)
    , m_footerY(0)
    , m_pages()
    , m_columns()
    , m_paragraphs()
  {
  }

  bool dimensionInvalid() const
  {
    return (m_pageDim.x() < 0 || m_pageDim.y() < 0 ||
            m_headerY < 0 || m_footerY < 0 ||
            m_headerY+m_footerY > m_pageDim.y());
  }

  bool getColumnLimitsFor(int line, std::vector<int> &listPos);

  MWAWVec2i m_pageDim;
  int m_headerY, m_footerY;
  std::vector<PageInfo> m_pages;
  std::vector<ColumnInfo> m_columns;
  std::vector<ParagraphInfo> m_paragraphs;

  // ????, pages, columns, parag, ???, ???, ???
  Zone m_zone[7];
};

bool WindowsInfo::getColumnLimitsFor(int line, std::vector<int> &listPos)
{
  listPos.resize(0);

  size_t numColumns = m_columns.size();
  size_t firstColumn = 0; // initialized to make clang analyzer happier
  int numCols = 0;
  for (size_t i = 0; i < numColumns; i++) {
    if (m_columns[i].m_firstLine == line+2) {
      numCols=m_columns[i].m_numCol;
      firstColumn = i;
      if (numCols > int(unsigned(numColumns - firstColumn)))
        numCols = int(unsigned(numColumns - firstColumn));
      if (numCols <= 1 || m_columns[i].m_col != 1) return false;
      break;
    }
    if (m_columns[i].m_firstLine > line+2)
      return true;
  }
  if (numCols <= 1)
    return true;

  size_t numPara = m_paragraphs.size();
  listPos.resize(size_t(numCols));
  for (size_t i = 0; i < size_t(numCols); i++) {
    ColumnInfo const &colInfo =  m_columns[firstColumn++];
    int l = colInfo.m_firstLine-1;
    if (l < 0 || l >= static_cast<int>(numPara)) {
      MWAW_DEBUG_MSG(("WindowsInfo::getColumnLimitsFor: pb with line position\n"));
      return false;
    }
    if (i && m_paragraphs[size_t(l)].getType() != 3) {
      MWAW_DEBUG_MSG(("WindowsInfo::getColumnLimitsFor: can not find cols break\n"));
      return false;
    }

    listPos[i] = (i == 0) ? l-1 : l;
  }
  return true;
}

std::ostream &operator<<(std::ostream &o, WindowsInfo const &w)
{
  if (w.m_pageDim.x() || w.m_pageDim.y())
    o << "pagesDim=" << w.m_pageDim << ",";
  if (w.m_headerY) o << "header[Height]=" << w.m_headerY << ",";
  if (w.m_footerY) o << "footer[Height]=" << w.m_footerY << ",";
  for (int i = 0; i < 7; i++) {
    if (w.m_zone[i].empty()) continue;
    switch (i) {
    case 1:
      o << "zonePages";
      break;
    case 2:
      o << "zoneCols?";
      break;
    case 3:
      o << "zoneParag";
      break;
    default:
      o << "unkZone" << i;
      break;
    }
    o << "=[" << w.m_zone[i] << "], ";
  }
  return o;
}

////////////////////////////////////////
/** Internal: class to store the font properties */
struct Font {
  Font()
    : m_font()
    , m_firstChar(0)
  {
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Font const &f)
  {
    if (f.m_firstChar) o << "firstChar=" << f.m_firstChar << ",";
    return o;
  }
  //! the font
  MWAWFont m_font;
  //! the first character
  int m_firstChar;
};

////////////////////////////////////////
/** Internal: class to store the line  properties */
struct Line {
  Line()
    : m_firstChar(0)
    , m_height(0)
    , m_width(0)
    , m_maxFontSize(0)
  {
    for (auto &fl : m_flags) fl=0;
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Line const &l)
  {
    if (l.m_firstChar) o << "firstChar=" << l.m_firstChar << ",";
    o << "height=" << l.m_height << ", width=" << l.m_width;
    for (int i = 0; i < 4; i++) {
      if (!l.m_flags[i]) continue;
      o << ", lF" << i << "=" << std::hex << l.m_flags[i] << std::dec;
    }
    return o;
  }
  //! the first character
  int m_firstChar;
  int m_height/** the height */, m_width /** the width */;
  int m_maxFontSize; /** the maximum font size */
  //! some flag
  int m_flags[4]; // flags[0]  a small number : a,b,c
};

////////////////////////////////////////
/** Internal: class to store the Graphic properties */
struct GraphicInfo {
  GraphicInfo()
    : m_width(0)
    , m_graphicWidth(0)
  {
    for (auto &fl : m_flags) fl=0;
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, GraphicInfo const &g)
  {
    o << "width=" << g.m_graphicWidth << ", width[line]=" << g.m_width;
    for (int i = 0; i < 6; i++) { // m_flags[6] seems to be junk
      if (!g.m_flags[i]) continue;
      o << ", gF" << i << "=" << std::hex << g.m_flags[i] << std::dec;
    }
    return o;
  }
  //! the first character
  int m_width/** the line width */, m_graphicWidth /** the graphic width */;
  //! some flag
  int m_flags[7];
};

////////////////////////////////////////
/** Internal: class to store the Section properties */
struct SectionInfo {
  SectionInfo()
    : m_numCol(0)
  {
    for (auto &dim : m_dim) dim = 0;
    for (auto &fl : m_flags) fl = 0;
  }

  bool empty() const
  {
    if (m_numCol) return false;
    for (auto dim : m_dim)
      if (dim) return false;
    for (auto fl : m_flags)
      if (fl) return false;
    return true;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, SectionInfo const &s)
  {
    if (s.m_numCol) o << "numCols?=" << s.m_numCol << ",";
    o << "dim?=[";
    for (auto dim : s.m_dim)
      o << dim << ",";
    o << "],";
    for (int i = 0; i < 4; i++) {
      if (!s.m_flags[i]) continue;
      o << ", sF" << i << "=" << std::hex << s.m_flags[i] << std::dec;
    }
    return o;
  }
  //! the number of columns(?)
  int m_numCol;
  //! unknown dimension
  int m_dim[3];
  //! some flag
  int m_flags[4];
};

////////////////////////////////////////
/** Internal: class to store the beginning of all paragraph data */
struct ParagraphData {
  //! Constructor
  ParagraphData()
    : m_type(-1)
    , m_typeFlag(0)
    , m_height(0)
    , m_width(0)
    , m_unknown(0)
    , m_text("")
    , m_fonts()
    , m_endPos(0)
  {
    for (auto &indent : m_indent) indent=0;
    for (auto &numData : m_numData) numData=0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ParagraphData const &p)
  {
    switch (p.m_type) {
    case 0:
      o << "text";
      break;
    case 1:
      o << "section";
      break; // checkme find only one time before a colbreak
    case 2:
      o << "text2";
      break;  // what is the difference with 1
    case 3:
      o << "colBreak";
      break;  // find only one time to change column
    case 4:
      o << "graphic";
      break;
    case 5:
      o << "table";
      break;
    default:
      o << "type=" << p.m_type;
      break;
    }
    switch (p.m_typeFlag) {
    case 0:
      break;
    case 0x80:
      o << "[in table]";
      break;
    default:
      o << "[" << std::hex << p.m_typeFlag << std::dec << "],";
    }
    o << ",";

    o << "height=" << p.m_height << ",";
    o << "witdh=" << p.m_width << ",";
    if (p.m_indent[0]) o << "indent[left]=" << p.m_indent[0] << ",";
    if (p.m_indent[1] != p.m_indent[0])
      o << "indent[firstPos]=" << p.m_indent[1] << ",";
    if (p.m_text.length()) o << "text='" << p.m_text << "',";
    if (p.m_type==5) o << "numData[total]=" << p.m_unknown << ",";
    else o << "unkn=" << p.m_unknown << ","; /* in text2: often 1, but can be 5|13|25|29 */
    return o;
  }

  int m_type, m_typeFlag;
  int m_height, m_width;
  int m_indent[2]; // left indent and ?
  int m_unknown;

  std::string m_text;
  std::vector<Font> m_fonts;

  long m_endPos; // end of the data ( except if there is auxilliary data )
  int m_numData[2]; // number of data[1] ( always font?), data[2]
};

////////////////////////////////////////
//! Internal: the state of a WriterPlsParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  //! the information ( 0: main, 1: header, 2: footer)
  WindowsInfo m_windows[3];

  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

////////////////////////////////////////
//! Internal: the subdocument of a WriterPlsParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(WriterPlsParser &pars, MWAWInputStreamPtr const &input, int zoneId)
    : MWAWSubDocument(&pars, input, MWAWEntry()), m_id(zoneId)
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
    if (m_id != sDoc->m_id) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the subdocument id
  int m_id;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("WriterPlsParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (m_id != 1 && m_id != 2) {
    MWAW_DEBUG_MSG(("WriterPlsParserInternal::SubDocument::parse: unknown zone\n"));
    return;
  }
  auto *parser=dynamic_cast<WriterPlsParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("WriterPlsParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendWindow(m_id);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
WriterPlsParser::WriterPlsParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

WriterPlsParser::~WriterPlsParser()
{
}

void WriterPlsParser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new WriterPlsParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// position and height
////////////////////////////////////////////////////////////
double WriterPlsParser::getTextHeight() const
{
  return getPageSpan().getPageLength()-m_state->m_headerHeight/72.0-m_state->m_footerHeight/72.0;
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void WriterPlsParser::newPage(int number)
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
void WriterPlsParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());

    checkHeader(nullptr);
    ok = createZones();
    ascii().addPos(getInput()->tell());
    ascii().addNote("_");
    if (ok) {
      createDocument(docInterface);
      sendWindow(0);
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("WriterPlsParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void WriterPlsParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("WriterPlsParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  for (int i = 1; i < 3; i++) {
    if (m_state->m_windows[i].m_paragraphs.size() == 0)
      continue;
    MWAWHeaderFooter hF((i==1) ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    hF.m_subDocument.reset(new WriterPlsParserInternal::SubDocument(*this, getInput(), i));
    ps.setHeaderFooter(hF);
  }

  m_state->m_numPages = int(m_state->m_windows[0].m_pages.size());
  ps.setPageSpan(m_state->m_numPages+1);
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
bool WriterPlsParser::createZones()
{
  if (!readWindowsInfo(0) || !readPrintInfo())
    return false;
  for (int st = 1; st < 4; st++) {
    bool ok = true;
    switch (st) {
    case 1:
      ok = m_state->m_headerHeight > 0;
      break;
    case 2:
      ok = m_state->m_footerHeight > 0;
      break;
    default:
      break;
    }
    if (!ok) continue;
    if (st !=3 && !readWindowsInfo(st))
      return false;
    if (!readWindowsZone(st==3 ? 0 : st))
      return (st==3);
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
bool WriterPlsParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = WriterPlsParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  int const headerSize=2;
  if (!input->checkPosition(headerSize)) {
    MWAW_DEBUG_MSG(("WriterPlsParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(2) != 0x110)
    return false;
  ascii().addPos(0);
  ascii().addNote("FileHeader");

  if (!readWindowsInfo(0) || !readPrintInfo())
    return false;
  input->seek(2,librevenge::RVNG_SEEK_SET);
  if (header)
    header->reset(MWAWDocument::MWAW_T_WRITERPLUS, 1);

  return true;
}

bool WriterPlsParser::readWindowsInfo(int zone)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsInfo:the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();

  long debPos = input->tell();
  if (!input->checkPosition(debPos+0xf4)) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsInfo: file is too short\n"));
    return false;
  }

  WriterPlsParserInternal::WindowsInfo info;
  libmwaw::DebugStream f;
  f << "Entries(WindowsZone)";
  switch (zone) {
  case 0:
    break;
  case 1:
    f << "[Header]";
    break;
  case 2:
    f << "[Footer]";
    break;
  default:
    f << "[Unknown]";
    break;
  }
  f << ":";
  for (int i = 0; i < 2; i++) {
    auto val = static_cast<int>(input->readLong(1));
    f << "f" << i << "=" << val << ",";
  }
  f << "unkn=" << input->readLong(2);

  long pos;
  for (auto &i : info.m_zone) {
    WriterPlsParserInternal::WindowsInfo::Zone infoZone;
    infoZone.m_unknown[0] = static_cast<int>(input->readULong(1));
    infoZone.m_width = static_cast<int>(input->readULong(2));
    infoZone.m_unknown[1] = static_cast<int>(input->readULong(1));
    infoZone.m_unknown[2] = static_cast<int>(input->readULong(2));
    infoZone.m_size = static_cast<int>(input->readULong(2));
    infoZone.m_number = static_cast<int>(input->readULong(2));
    i = infoZone;
  }
  f << "," << info;

  ascii().addPos(debPos);
  ascii().addNote(f.str().c_str());

  pos = input->tell();
  ascii().addPos(pos);
  ascii().addNote("WindowsZone(A-1)");
  ascii().addPos(pos+12);
  ascii().addNote("WindowsZone(A-2)");
  ascii().addPos(pos+30);
  ascii().addNote("WindowsZone(A-3)");
  ascii().addPos(pos+60);
  ascii().addNote("WindowsZone(A-4)");
  ascii().addPos(pos+60+14);
  ascii().addNote("WindowsZone(A-5)");
  ascii().addPos(pos+60+14*2);
  ascii().addNote("WindowsZone(A-6)");

  pos = debPos+0xc2;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f.str("");
  f << "WindowsZone(A-7):";
  auto val = static_cast<int>(input->readLong(2));
  if (val) f << "unkn=" << val << ",";
  auto width = static_cast<int>(input->readLong(2));
  info.m_footerY = static_cast<int>(input->readLong(2));
  info.m_headerY = static_cast<int>(input->readLong(2));
  auto height = static_cast<int>(input->readLong(2));
  info.m_pageDim = MWAWVec2i(width, height);
  f << "page=" << info.m_pageDim << ",";
  if (info.m_headerY)
    f << "header[height]=" << info.m_headerY << ",";
  if (info.m_footerY)
    f << "footer[height]=" << info.m_footerY << ",";
  for (int i = 0; i < 3; i++) // always 17 12 0 left|right ?
    f << "f" << i << "=" << static_cast<int>(input->readLong(2)) << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (info.dimensionInvalid())
    return false;
  if (zone == 0) {
    m_state->m_headerHeight = info.m_headerY;
    m_state->m_footerHeight = info.m_footerY;
  }
  pos = input->tell();
  f.str("");
  f << "WindowsZone(B):";
  int dim[4];
  for (auto &d : dim) d = static_cast<int>(input->readLong(2));
  f << "dim(?)=" << dim[1] << "x" << dim[0] << "-" << dim[3] << "x" << dim[2] << ",";
  for (int i = 0; i < 2; i++) {
    auto fl = static_cast<int>(input->readLong(1)); // almost always 0 except some time 1
    if (fl) f << "fl" << i << "=" << fl << ",";
  }
  for (int i = 0; i < 6; i++) {
    int values[3];
    values[0] = static_cast<int>(input->readULong(1));
    values[1] = static_cast<int>(input->readLong(2));
    values[2] = static_cast<int>(input->readULong(1));
    if (values[0] == 0 && values[1] == 0 && values[2] == 0) continue;
    f << "f" << i << "=[" << values[0] << ", w=" << values[1]
      << ", " << std::hex << values[2] << std::dec << "],";
  }

  m_state->m_windows[zone]=info;

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read all the windows zone info
////////////////////////////////////////////////////////////
bool WriterPlsParser::readWindowsZone(int zone)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsZone:the zone seems bad\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  auto &wInfo = m_state->m_windows[zone];

  libmwaw::DebugStream f;
  for (int wh=1; wh < 7; wh++) {
    auto const &z = wInfo.m_zone[wh];
    int length = z.m_size;
    if (!length) continue;

    long pos = input->tell();
    input->seek(length, librevenge::RVNG_SEEK_CUR);
    if (long(input->tell()) != pos+length) {
      MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsZone: zone is too short\n"));
      return false;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool ok = false;
    switch (wh) {
    case 1:
      ok=readPageInfo(zone);
      break;
    case 2:
      ok=readColInfo(zone);
      break;
    case 3:
      // need to get next block
      ok = readParagraphInfo(zone);
      if (!ok) return false;
      break;
    default:
      break;
    }
    if (ok) continue;

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (z.m_number && (length % z.m_number) == 0) {
      int dataSz = length / z.m_number;
      for (int i = 0; i < z.m_number; i++) {
        f.str("");
        f << "Entries(Zone" << wh << ")-" << i << ":";
        ascii().addPos(input->tell());
        ascii().addNote(f.str().c_str());
        input->seek(dataSz, librevenge::RVNG_SEEK_CUR);
      }
    }
    else {
      f.str("");
      f << "Entries(Zone" << wh << "):";
      ascii().addPos(input->tell());
      ascii().addNote(f.str().c_str());
      input->seek(length, librevenge::RVNG_SEEK_CUR);
    }
  }

  for (int i = int(wInfo.m_paragraphs.size())-1; i >= 0; i--) {
    auto const &pInfo = wInfo.m_paragraphs[size_t(i)];
    if (!pInfo.m_pos)	continue;

    input->seek(pInfo.m_pos, librevenge::RVNG_SEEK_SET);
    auto length = long(input->readULong(2));
    auto length2 = long(input->readULong(2));
    long endPos = pInfo.m_pos+4+length+length2;
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    if (long(input->tell()) != endPos) {
      MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsZone: data zone is too short\n"));
      return false;
    }
    switch (pInfo.getType()) {
    case 4:
      length = long(input->readULong(4));
      input->seek(length, librevenge::RVNG_SEEK_CUR);
      if (long(input->tell()) != endPos+length+4) {
        MWAW_DEBUG_MSG(("WriterPlsParser::readWindowsZone: graphics zone is too short\n"));
        return false;
      }
      break;
    default: // 0,1,2,3, 5 : ok, other ?
      break;
    }
    return true;
  }
  return true;
}

////////////////////////////////////////////////////////////
// send the windows zone info
////////////////////////////////////////////////////////////
bool WriterPlsParser::sendWindow(int zone, MWAWVec2i limits)
{
  MWAWTextListenerPtr listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: can not find a listener\n"));
    return false;
  }
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow:the zone seems bad\n"));
    return false;
  }
  auto &wInfo = m_state->m_windows[zone];

  bool sendAll = limits[0] < 0;

  auto maxPages = int(wInfo.m_pages.size());
  if (maxPages == 0 || zone || !sendAll) maxPages = 1;

  int actParag = 0;
  int actCol = 0, numCols = 0;
  for (int pg = 0; pg < maxPages; pg++) {
    int endParag = 0;
    if (!sendAll) {
      actParag = limits[0];
      endParag = limits[1];
      if (endParag <= actParag) {
        MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: pb2 with limits\n"));
        return true;
      }
    }
    else {
      if (zone == 0) {
        newPage(pg+1);
        actCol = numCols ? 1 : 0;
      }
      if (pg == maxPages-1 || wInfo.m_pages.size() == 0)
        endParag = int(wInfo.m_paragraphs.size());
      else {
        endParag = wInfo.m_pages[size_t(pg)+1].m_firstLine-1;
        if (endParag == -1 || endParag < actParag) {
          MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: pb with page zone\n"));
          continue;
        }
      }
    }
    if (endParag > int(wInfo.m_paragraphs.size())) {
      MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: pb with limits\n"));
      endParag = int(wInfo.m_paragraphs.size());
    }

    for (int i = actParag; i < endParag; i++) {
      auto const &pInfo = wInfo.m_paragraphs[size_t(i)];
      if (!pInfo.m_pos) {
        readText(pInfo);
        continue;
      }
      bool ok = true;
      switch (pInfo.getType()) {
      case 3: // col break: seems similar to an entry data (with a text zone which does not contain any character)
        if (numCols) {
          if (actCol >numCols) {
            MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: pb with col break\n"));
          }
          else {
            actCol++;
            listener->insertBreak(MWAWTextListener::ColumnBreak);
          }
        }
        MWAW_FALLTHROUGH;
      case 0:
      case 2:
        ok = readText(pInfo);
        break;
      case 1: {
        MWAWSection section;
        bool canCreateSection = sendAll && zone == 0 && actCol == numCols;
        if (findSection(zone, MWAWVec2i(i, endParag), section)) {
          if (!canCreateSection) {
            if (section.numColumns()>1) {
              MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: find a section in auxilliary zone\n"));
            }
          }
          else {
            if (listener->isSectionOpened())
              listener->closeSection();
            listener->openSection(section);
            numCols = listener->getSection().numColumns();
            if (numCols<=1) numCols=0;
            actCol = numCols ? 1 : 0;
            canCreateSection = false;
          }
        }

        ok = readSection(pInfo, canCreateSection);
        break;
      }
      case 4:
        ok = readGraphic(pInfo);
        break;
      case 5:
        if (pInfo.m_numLines + i <= endParag) {
          if ((ok = readTable(pInfo))) {
            listener->openTableRow(float(pInfo.m_height), librevenge::RVNG_POINT);

            for (size_t j = 0; j < pInfo.m_linesHeight.size(); j++) {
              int numData = pInfo.m_linesHeight[j];
              MWAWCell cell;
              cell.setPosition(MWAWVec2i(int(j), 0));
              listener->openTableCell(cell);
              sendWindow(zone, MWAWVec2i(i+1, i+1+numData));
              i += numData;
              listener->closeTableCell();
            }

            listener->closeTableRow();
            listener->closeTable();
          }
        }
        else {
          MWAW_DEBUG_MSG(("WriterPlsParser::sendWindow: table across a page\n"));
        }
        break;
      default:
        ok = readUnknown(pInfo);
        break;
      }
      if (!ok) {
        libmwaw::DebugStream f;
        f << "Entries(Unknown):" << pInfo;
        ascii().addPos(pInfo.m_pos);
        ascii().addNote(f.str().c_str());
      }
    }
    actParag = endParag;
  }
  return true;
}


/*
 * find the column size which correspond to a limit
 *
 * Note: complex because we need to read the file in order to find the limit
 */
bool WriterPlsParser::findSection(int zone, MWAWVec2i limits, MWAWSection &sec)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::findSection:the zone seems bad\n"));
    return false;
  }
  auto &wInfo = m_state->m_windows[zone];

  sec=MWAWSection();
  std::vector<int> listPos;
  if (!wInfo.getColumnLimitsFor(limits[0], listPos))
    return false;

  size_t numPos = listPos.size();
  if (!numPos)
    return true;
  if (listPos[numPos-1] >= limits[1]) {
    MWAW_DEBUG_MSG(("WriterPlsParser::findSection: columns across a page\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  int totalSize = 0;
  for (auto &line : listPos) {
    long pos = wInfo.m_paragraphs[size_t(line)].m_pos;
    if (!pos) {
      MWAW_DEBUG_MSG(("WriterPlsParser::findSection: bad data pos\n"));
      return false;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (input->readLong(2)) {
      MWAW_DEBUG_MSG(("WriterPlsParser::findSection: find a text size\n"));
      return false;
    }
    input->seek(8, librevenge::RVNG_SEEK_CUR); // sz2 and type, h, indent
    auto val = static_cast<int>(input->readLong(2));
    if (val <= 0 || long(input->tell()) != pos + 12) {
      MWAW_DEBUG_MSG(("WriterPlsParser::findSection: file is too short\n"));
      return false;
    }
    totalSize += val;
    MWAWSection::Column col;
    col.m_width=val;
    col.m_widthUnit=librevenge::RVNG_POINT;
    sec.m_columns.push_back(col);
  }
  if (sec.m_columns.size()==1)
    sec.m_columns.resize(0);
  if (totalSize >= int(72.*getPageWidth())) {
    MWAW_DEBUG_MSG(("WriterPlsParser::findSection: total size is too big\n"));
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////
// read all the windows zone info
////////////////////////////////////////////////////////////
bool WriterPlsParser::readPageInfo(int zone)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readPageInfo:the zone seems bad\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  auto &wInfo = m_state->m_windows[zone];
  int numPages = wInfo.m_zone[1].m_number;
  if (wInfo.m_zone[1].m_size != numPages * 10) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readPageInfo: odd page size\n"));
    return false;
  }

  int actNumLine = 0;
  auto maxHeight = int(72.*getTextHeight()+20.);
  if (maxHeight < 1000) maxHeight = 1000;
  int prevTotalHeight = 0;

  for (int page = 0; page < numPages; page++) {
    long pos = input->tell();
    WriterPlsParserInternal::PageInfo pInfo;
    pInfo.m_firstLine = static_cast<int>(input->readLong(2));
    if ((page == 0 && pInfo.m_firstLine != 1) || pInfo.m_firstLine < actNumLine)
      return false;
    actNumLine=pInfo.m_firstLine;
    for (auto &unkn : pInfo.m_unknown) unkn = static_cast<int>(input->readLong(2));
    pInfo.m_heightFromBegin = static_cast<int>(input->readULong(2));
    if (pInfo.m_heightFromBegin < prevTotalHeight) return false;
    prevTotalHeight = pInfo.m_heightFromBegin;
    pInfo.m_height = static_cast<int>(input->readULong(2));
    if (pInfo.m_height > maxHeight) return false;

    wInfo.m_pages.push_back(pInfo);
    f.str("");
    f << "Entries(PageInfo)-"<< page+1 << ":" << pInfo;

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  return true;
}

////////////////////////////////////////////////////////////
// read a windows paragraph info
////////////////////////////////////////////////////////////
MWAWParagraph WriterPlsParser::getParagraph(WriterPlsParserInternal::ParagraphData const &data)
{
  MWAWParagraph para;

  para.m_marginsUnit=librevenge::RVNG_POINT;
  // decrease a little left indent to avoid some page width pb
  double left=double(data.m_indent[0])-20.-72.*getPageSpan().getMarginLeft();
  if (left > 0)
    para.m_margins[1]=left;
  para.m_margins[0]=double(data.m_indent[1]-data.m_indent[0]);
  if (getTextListener() && getTextListener()->getSection().numColumns() > 1)
    return para; // too dangerous to set the paragraph width in this case...
  double right=getPageWidth()*72.-double(data.m_width);
  if (right > 0)
    para.m_margins[2]=right;
  return para;
}

bool WriterPlsParser::readParagraphInfo(int zone)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readParagraphInfo:the zone seems bad\n"));
    return false;
  }

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  WriterPlsParserInternal::WindowsInfo &wInfo = m_state->m_windows[zone];
  int numPara = wInfo.m_zone[3].m_number;
  long endPos = long(input->tell()) + wInfo.m_zone[3].m_size;

  int para = 0;
  while (para <= numPara) {
    long pos = input->tell();
    if (pos == endPos) break;
    if (pos > endPos) return false;
    WriterPlsParserInternal::ParagraphInfo pInfo;

    f.str("");
    f << "Entries(ParaInfo)-"<< para+1 << ":";
    auto wh = static_cast<int>(input->readLong(1));
    if ((wh%2) == 0) {
      if (wh < 4) return false;
      for (int i = 0; i < (wh-4)/2; i++)
        pInfo.m_unknowns.push_back(static_cast<int>(input->readULong(2)));
      pInfo.m_type = -1;
      pInfo.m_numLines = static_cast<int>(input->readULong(1)); // probably numLine
      pInfo.m_height = static_cast<int>(input->readULong(2));
      f << pInfo;
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    para++;
    pInfo.m_flags[0] = (wh>>1);
    pInfo.m_flags[1] = static_cast<int>(input->readULong(1)); // almost always 0
    pInfo.m_type = static_cast<int>(input->readULong(1));
    pInfo.m_numLines = static_cast<int>(input->readULong(1)); // or numColumns if type==5
    pInfo.m_height = static_cast<int>(input->readULong(2));
    pInfo.m_pos = long(input->readULong(4));
    pInfo.m_flags[2] = static_cast<int>(input->readULong(1)); // almost always 0
    pInfo.m_width = static_cast<int>(input->readULong(2));
    for (int i = 3; i < 5; i++)
      pInfo.m_flags[i] = static_cast<int>(input->readULong(1));
    if (pInfo.m_numLines!=1) {
      for (int i = 0; i < pInfo.m_numLines; i++)
        pInfo.m_linesHeight.push_back(static_cast<int>(input->readULong(1)));
    }
    pInfo.m_height2 = static_cast<int>(input->readULong(1));
    wInfo.m_paragraphs.push_back(pInfo);
    f << pInfo;

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

  }

  return true;
}

////////////////////////////////////////////////////////////
// read all the windows col info ?
////////////////////////////////////////////////////////////
bool WriterPlsParser::readColInfo(int zone)
{
  if (zone<0 || zone>=3) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readColInfo:the zone seems bad\n"));
    return false;
  }

  libmwaw::DebugStream f;

  WriterPlsParserInternal::WindowsInfo &wInfo = m_state->m_windows[zone];
  int numCols = wInfo.m_zone[2].m_number;
  if (wInfo.m_zone[2].m_size != numCols * 16) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readColInfo: odd col size\n"));
    return false;
  }

  MWAWInputStreamPtr input = getInput();
  for (int col = 0; col < numCols; col++) {
    long pos = input->tell();
    WriterPlsParserInternal::ColumnInfo cInfo;
    cInfo.m_col = static_cast<int>(input->readLong(2));
    cInfo.m_unknown[0] = static_cast<int>(input->readLong(2));
    cInfo.m_numCol = static_cast<int>(input->readLong(2));
    cInfo.m_firstLine = static_cast<int>(input->readLong(2));
    for (int i = 1; i < 4; i++)
      cInfo.m_unknown[i] = static_cast<int>(input->readLong(2));
    cInfo.m_height = static_cast<int>(input->readLong(2));
    wInfo.m_columns.push_back(cInfo);

    f.str("");
    f << "Entries(ColInfo):" << cInfo;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  return true;
}

bool WriterPlsParser::readText(WriterPlsParserInternal::ParagraphInfo const &info)
{
  WriterPlsParserInternal::ParagraphData data;
  std::vector<WriterPlsParserInternal::Line> lines;
  if (!info.m_pos) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readText: pb with pos\n"));
    return false;
  }

  if (!readParagraphData(info, true, data))
    return false;

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  f.str("");
  f << "Paragraph" << data.m_type << "(II):";

  int numLines = data.m_numData[1];
  if (!readLines(info, numLines, lines)) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readText: pb with the lines\n"));
    lines.resize(0);
    input->seek(pos+numLines*16, librevenge::RVNG_SEEK_SET);
    f << "###lines,";
  }
  for (int i = 0; i < numLines; i++)
    f << "line" << i << "=[" << lines[size_t(i)] << "],";

  if (long(input->tell()) != data.m_endPos) {
    ascii().addDelimiter(input->tell(), '|');
    input->seek(data.m_endPos, librevenge::RVNG_SEEK_SET);
    f << "#endPos,";
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(input->tell());
  ascii().addNote("_");

  if (!getTextListener())
    return true;
  std::string const &text = data.m_text;
  auto const &fonts = data.m_fonts;
  auto numChars = long(text.length());
  size_t actFont = 0, numFonts = fonts.size();
  int actLine = 0;
  numLines=int(lines.size());
  MWAWParagraph para=getParagraph(data);

  if (numLines == 0 && info.m_height > 0) {
    para.setInterline(info.m_height, librevenge::RVNG_POINT);
    getTextListener()->setParagraph(para);
  }
  for (long c = 0; c < numChars; c++) {
    if (actFont < numFonts && c ==  fonts[actFont].m_firstChar)
      getTextListener()->setFont(fonts[actFont++].m_font);
    if (actLine < numLines && c == lines[size_t(actLine)].m_firstChar) {
      if (actLine) getTextListener()->insertEOL();
      if (numLines == 1 && info.m_height > lines[0].m_height) {
        para.setInterline(info.m_height, librevenge::RVNG_POINT);
        getTextListener()->setParagraph(para);
      }
      else if (lines[size_t(actLine)].m_height) {
        para.setInterline(lines[size_t(actLine)].m_height, librevenge::RVNG_POINT);
        getTextListener()->setParagraph(para);
      }
      actLine++;
    }

    auto ch = static_cast<unsigned char>(text[size_t(c)]);
    if (ch == 0x9)
      getTextListener()->insertTab();
    else
      getTextListener()->insertCharacter(ch);
  }
  if (info.getType() != 3)
    getTextListener()->insertEOL();

  return true;
}

bool WriterPlsParser::readSection(WriterPlsParserInternal::ParagraphInfo const &info, bool mainBlock)
{
  WriterPlsParserInternal::ParagraphData data;

  if (!info.m_pos) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readSection: can not find the beginning pos\n"));
    return false;
  }

  if (!readParagraphData(info, true, data))
    return false;

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  f.str("");
  f << "Paragraph" << data.m_type << "(II):";

  int numData = data.m_numData[1];
  if (numData != 1) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readSection: unexpected num of data: %d \n", numData));
  }

  std::vector<WriterPlsParserInternal::SectionInfo> sections;
  for (int i = 0; i < numData; i++) {
    WriterPlsParserInternal::SectionInfo section;
    for (int j = 0; j < 2; j++)
      section.m_flags[j] = static_cast<int>(input->readLong(2));
    section.m_numCol = static_cast<int>(input->readLong(2)); // checkme
    for (auto &dim : section.m_dim) dim = static_cast<int>(input->readLong(2));
    for (int j = 2; j < 4; j++)
      section.m_flags[j] = static_cast<int>(input->readLong(2));
    sections.push_back(section);
    if (!section.empty())
      f << "section" << i << "=[" << section << "],";
  }

  if (long(input->tell()) != data.m_endPos) {
    ascii().addDelimiter(input->tell(), '|');
    input->seek(data.m_endPos, librevenge::RVNG_SEEK_SET);
    f << "#endPos,";
  }

  if (getTextListener() && mainBlock) {
    if (!getTextListener()->isSectionOpened())
      getTextListener()->openSection(MWAWSection());
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(input->tell());
  ascii().addNote("_");
  return true;
}

bool WriterPlsParser::readTable(WriterPlsParserInternal::ParagraphInfo const &info)
{
  WriterPlsParserInternal::ParagraphData data;

  if (!info.m_pos) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readTable: can not find the beginning pos\n"));
    return false;
  }

  if (!readParagraphData(info, true, data))
    return false;

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  f.str("");
  f << "Paragraph" << data.m_type << "(II):";

  int numData = data.m_numData[1];
  if (numData <= 1) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readTable: unexpected num of data: %d \n", numData));
  }

  std::vector<WriterPlsParserInternal::ColumnTableInfo> columns;
  for (int i = 0; i < numData; i++) {
    WriterPlsParserInternal::ColumnTableInfo cols;
    cols.m_height = static_cast<int>(input->readLong(2));
    for (auto &colX : cols.m_colX) colX = static_cast<int>(input->readLong(2));
    cols.m_numData = static_cast<int>(input->readLong(2));
    cols.m_flags  = static_cast<int>(input->readLong(2));
    for (auto &textX : cols.m_textX) textX = static_cast<int>(input->readLong(2));

    columns.push_back(cols);
    f << "col" << i << "=[" << cols << "],";
  }

  if (getTextListener()) {
    std::vector<float> colSize(static_cast<size_t>(numData));
    for (int i = 0; i < numData; i++) {
      auto const &cols = columns[size_t(i)];
      colSize[size_t(i)] = float(cols.m_colX[1]-cols.m_colX[0]);
    }
    MWAWTable table(MWAWTable::TableDimBit);
    table.setColsSize(colSize);
    // use the same function than getParagraph to respect alignment
    int left=columns.empty() ? 0 : columns[0].m_colX[0]-20-int(72.*getPageSpan().getMarginLeft());
    if (left)
      table.setAlignment(MWAWTable::Left, float(left));
    getTextListener()->openTable(table);
  }

  if (long(input->tell()) != data.m_endPos) {
    ascii().addDelimiter(input->tell(), '|');
    input->seek(data.m_endPos, librevenge::RVNG_SEEK_SET);
    f << "#endPos,";
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(input->tell());
  ascii().addNote("_");

  return true;
}

bool WriterPlsParser::readGraphic(WriterPlsParserInternal::ParagraphInfo const &info)
{
  WriterPlsParserInternal::ParagraphData data;

  if (!info.m_pos) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: can not find the beginning pos\n"));
    return false;
  }

  if (!readParagraphData(info, true, data))
    return false;

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  f.str("");
  f << "Paragraph" << data.m_type << "(II):";

  int numData = data.m_numData[1];
  if (numData != 1) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: unexpected num of data: %d \n", numData));
  }

  std::vector<WriterPlsParserInternal::GraphicInfo> graphicsInfos;
  for (int i = 0; i < numData; i++) {
    WriterPlsParserInternal::GraphicInfo gInfo;
    gInfo.m_flags[0] = static_cast<int>(input->readLong(1));
    gInfo.m_width = static_cast<int>(input->readLong(2));
    gInfo.m_flags[1] = static_cast<int>(input->readULong(1)); //
    gInfo.m_graphicWidth = static_cast<int>(input->readLong(2)); // total width
    for (int j = 2; j < 7; j++)
      gInfo.m_flags[j] = static_cast<int>(input->readLong(2));
    f << "data" << i << "=[" << gInfo << "],";
    graphicsInfos.push_back(gInfo);
  }
  if (long(input->tell()) != data.m_endPos) {
    ascii().addDelimiter(input->tell(), '|');
    input->seek(data.m_endPos, librevenge::RVNG_SEEK_SET);
    f << "#endPos,";
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // read the graphic:
  pos = input->tell();
  auto length = long(input->readULong(4));
  if (!length) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: find a zero size graphics\n"));
    ascii().addPos(pos);
    ascii().addNote("Entries(Graphic):#sz=0");
    return true;
  }
  long endPos = pos+4+length;
  input->seek(length, librevenge::RVNG_SEEK_CUR);
  if (long(input->tell()) != endPos) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: file is too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  f.str("");
  f << "Paragraph" << data.m_type << "(III):";

  MWAWBox2f box;
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  auto res = MWAWPictData::check(input, static_cast<int>(length), box);
  if (res == MWAWPict::MWAW_R_BAD) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: can not find the picture\n"));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  MWAWVec2f actualSize(0,0), naturalSize(actualSize);
  if (box.size().x() > 0 && box.size().y()  > 0) {
    if (actualSize.x() <= 0 || actualSize.y() <= 0) actualSize = box.size();
    naturalSize = box.size();
  }
  else {
    MWAW_DEBUG_MSG(("WriterPlsParser::readGraphic: can not find the picture size\n"));
    actualSize = MWAWVec2f(100,100);
  }

  MWAWPosition pictPos=MWAWPosition(MWAWVec2f(0,0),actualSize, librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Char);
  pictPos.setNaturalSize(naturalSize);
  f << pictPos;

  // get the picture
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, static_cast<int>(length)));
  if (getTextListener()) {
    auto para=getTextListener()->getParagraph();
    para.setInterline(info.m_height, librevenge::RVNG_POINT);
    getTextListener()->setParagraph(para);
    MWAWEmbeddedObject picture;
    if (pict && pict->getBinary(picture))
      getTextListener()->insertPicture(pictPos, picture);
    getTextListener()->insertEOL();
    para.setInterline(1.0, librevenge::RVNG_PERCENT);
    getTextListener()->setParagraph(para);
  }
  if (pict)
    ascii().skipZone(pos+4, pos+4+length-1);

  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(endPos);
  ascii().addNote("_");

  return true;
}

////////////////////////////////////////////////////////////
// read a paragraph
////////////////////////////////////////////////////////////
bool WriterPlsParser::readUnknown(WriterPlsParserInternal::ParagraphInfo const &info)
{
  WriterPlsParserInternal::ParagraphData data;
  if (!readParagraphData(info, true, data))
    return false;

  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  f.str("");
  f << "Paragraph" << data.m_type << "(II):";

  int numData = data.m_numData[1];
  for (int i = 0; i < numData; i++) {
    f << "data" << i << "=[";
    for (int j = 0; j < 8; j++) {
      auto val = static_cast<int>(input->readLong(2));
      if (!val) f << "_,";
      else f << val << ",";
    }
    f << "],";
  }
  if (long(input->tell()) != data.m_endPos) {
    ascii().addDelimiter(input->tell(), '|');
    input->seek(data.m_endPos, librevenge::RVNG_SEEK_SET);
    f << "#";
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(input->tell());
  ascii().addNote("_");

  return true;
}

////////////////////////////////////////////////////////////
// read the beginning of a paragraph data
////////////////////////////////////////////////////////////
bool WriterPlsParser::readParagraphData(WriterPlsParserInternal::ParagraphInfo const &info, bool hasFonts,
                                        WriterPlsParserInternal::ParagraphData &data)
{
  libmwaw::DebugStream f;

  MWAWInputStreamPtr input = getInput();
  long pos = info.m_pos;
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  data = WriterPlsParserInternal::ParagraphData();
  auto textLength = static_cast<int>(input->readLong(2));
  auto length2 = static_cast<int>(input->readLong(2));
  data.m_endPos = pos+4+textLength+length2;

  if (textLength < 0 || length2 < 0 || !input->checkPosition(data.m_endPos)) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readParagraphData:  paragraph is too short\n"));
    return false;
  }
  if (textLength) {
    std::string &text = data.m_text;
    for (int i = 0; i < textLength; i++) {
      auto c = char(input->readULong(1));
      if (c == '\0') return false;
      text += c;
    }
  }
  auto type = static_cast<int>(input->readULong(2));
  data.m_type = (type & 7);
  data.m_typeFlag = (type & 0xFFF8);

  f << "Entries(Paragraph" << data.m_type << "):";

  // format type
  if (info.m_type != data.m_type + (data.m_typeFlag!=0 ? 8 : 0)) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readParagraph: I find an unexpected type\n"));
    f << "#diffType=" << info.m_type << ",";
  }

  data.m_height = static_cast<int>(input->readLong(2));
  data.m_indent[0] = static_cast<int>(input->readLong(2)); // left indent ?
  data.m_width = static_cast<int>(input->readLong(2));
  data.m_indent[1] = static_cast<int>(input->readLong(2)); // first pos indent ?
  data.m_unknown = static_cast<int>(input->readLong(2));

  for (auto &numData : data.m_numData) numData = static_cast<int>(input->readLong(2));

  auto &fonts = data.m_fonts;
  if (hasFonts) {
    long actPos = input->tell();
    if (data.m_numData[0]<0 || !input->checkPosition(actPos+data.m_numData[0]*16)) {
      MWAW_DEBUG_MSG(("WriterPlsParser::readParagraph: pb reading the number of fonts\n"));
      f << "###numFonts=" << data.m_numData[0] << ",";
    }
    else if (!readFonts(data.m_numData[0], data.m_type, fonts)) {
      MWAW_DEBUG_MSG(("WriterPlsParser::readParagraph: pb with the fonts\n"));
      input->seek(actPos+data.m_numData[0]*16, librevenge::RVNG_SEEK_SET);
    }
  }

  f << data;
  for (size_t i = 0; i < fonts.size(); i++) {
    f << "font" << i << "=[";
#ifdef DEBUG
    f << fonts[i].m_font.getDebugString(getFontConverter());
#endif
    f << fonts[i] << "],";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// read a series of fonts
////////////////////////////////////////////////////////////
bool WriterPlsParser::readFonts
(int nFonts, int type, std::vector<WriterPlsParserInternal::Font> &fonts)
{
  fonts.resize(0);
  MWAWInputStreamPtr input = getInput();
  bool hasFontExtra = true;
  switch (type) {
  case 0: // find in these case junk in the last part of font
  case 2:
  case 4:
    hasFontExtra = false;
    break;
  default:
    break;
  }
  int actPos = 0;
  libmwaw::DebugStream f;
  for (int i = 0; i < nFonts; i++) {
    if (!input->checkPosition(input->tell()+16)) {
      MWAW_DEBUG_MSG(("WriterPlsParser::readFonts: the zone seems too short\n"));
      break;
    }
    WriterPlsParserInternal::Font fInfo;
    f.str("");
    auto val = static_cast<int>(input->readLong(2)); // 65|315
    if (val) f << "dim?=" << val << ",";
    for (int j = 0; j < 3; j++) { // always 0: a color ?
      val = static_cast<int>(input->readLong(1));
      if (val) f << "f" << j << "=" << val << ",";
    }
    MWAWFont &font = fInfo.m_font;
    font.setId(static_cast<int>(input->readULong(1)));
    auto flag = static_cast<int>(input->readULong(1));
    uint32_t flags = 0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x60)
      f << "#fl=" << std::hex << (flag&0x60) << std::dec << ",";
    if (flag&0x80) f << "fl80,"; // frequent, find on complete line,

    flag= static_cast<int>(input->readULong(1));
    if (flag&2) font.set(MWAWFont::Script::super100());
    if (flag&4) font.set(MWAWFont::Script::sub100());
    if (flag&0x10) f << "flA10,";// also frequent, find on complete line
    if (flag&0xE9) f << "#flA=" << std::hex << (flag&0xE9) << std::dec << ",";
    font.setFlags(flags);
    val = static_cast<int>(input->readLong(1));// always 0
    if (val)
      f << "#g0=" << val << ",";
    font.setSize(float(input->readLong(1)));
    fInfo.m_firstChar = actPos;
    auto nChar = static_cast<int>(input->readULong(2));
    actPos += nChar;
    if (!hasFontExtra)
      input->seek(4, librevenge::RVNG_SEEK_CUR);
    else { // always 0
      for (int j = 0; j < 2; j++) {
        val = static_cast<int>(input->readLong(2));
        if (val) f << "g" << j+1 << "=" << val << ",";
      }
    }
    font.m_extra+=f.str();
    fonts.push_back(fInfo);
  }

  return true;
}

////////////////////////////////////////////////////////////
// read a series of lines
////////////////////////////////////////////////////////////
bool WriterPlsParser::readLines
(WriterPlsParserInternal::ParagraphInfo const &/*info*/,
 int nLines, std::vector<WriterPlsParserInternal::Line> &lines)
{
  lines.resize(0);
  MWAWInputStreamPtr input = getInput();

  int actPos = 0;
  for (int i = 0; i < nLines; i++) {
    WriterPlsParserInternal::Line lInfo;
    lInfo.m_height = static_cast<int>(input->readLong(2));
    lInfo.m_maxFontSize = static_cast<int>(input->readLong(2)); // checkMe
    lInfo.m_width = static_cast<int>(input->readLong(2));
    auto nChar = static_cast<int>(input->readLong(2));
    lInfo.m_firstChar = actPos;
    actPos += nChar;
    /*
    	 f0 always 0
    	 f1 almost always 0, if not 1
    	 f2 almost always 0, if not 2, 3, 4, c
    	 f3 almost always 0, if not 200, 400, 6465, 7600, dfc, e03, e04, e06 : junk?
     */
    for (auto &fl : lInfo.m_flags) fl = static_cast<int>(input->readLong(2));
    lines.push_back(lInfo);
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool WriterPlsParser::readPrintInfo()
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

  input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != pos+0x78) {
    MWAW_DEBUG_MSG(("WriterPlsParser::readPrintInfo: file is too short\n"));
    return false;
  }

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
