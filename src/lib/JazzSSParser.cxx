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
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWSubDocument.hxx"

#include "JazzSSParser.hxx"

/** Internal: the structures of a JazzSSParser */
namespace JazzSSParserInternal
{
//! Internal: the cell of a JazzSSParser
struct Cell final : public MWAWCell {
  //! constructor
  explicit Cell()
    : MWAWCell()
    , m_content()
  {
  }
  Cell(Cell const &)=default;
  //! destructor
  ~Cell() final;
  //! the cell content
  MWAWCellContent m_content;
};

Cell::~Cell()
{
}
////////////////////////////////////////
//! Internal: the state of a JazzSSParser
struct State {
  //! constructor
  State() :
    m_isDatabase(false),
    m_dimensions(-1,-1),
    m_font(),
    m_widths(),
    m_posToCells()
  {
  }
  //! true if the file is a database file
  bool m_isDatabase;
  //! the sheet dimensions
  MWAWVec2i m_dimensions;
  //! the cell default font
  MWAWFont m_font;
  //! the columns width
  std::vector<float> m_widths;
  //! map of cells sorted by columns
  std::map<MWAWVec2i, Cell> m_posToCells;
};

////////////////////////////////////////
//! Internal: the subdocument of a JazzSSParserInternal
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(JazzSSParser &parser, MWAWInputStreamPtr const &input, MWAWEntry const &entry)
    : MWAWSubDocument(&parser, input, entry)
    , m_multiParser(parser)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the main parser */
  JazzSSParser &m_multiParser;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("JazzSSParser::SubDocument::parse: no listener\n"));
    return;
  }

  long pos = m_input->tell();
  //m_multiParser.sendText(m_zone);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (&m_multiParser != &sDoc->m_multiParser) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
JazzSSParser::JazzSSParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWSpreadsheetParser(input, rsrcParser, header)
  , m_state(new JazzSSParserInternal::State)
{
  setAsciiName("main-1");
  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

JazzSSParser::~JazzSSParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void JazzSSParser::parse(librevenge::RVNGSpreadsheetInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendSpreadsheet();
    }
  }
  catch (...) {
    MWAW_DEBUG_MSG(("JazzSSParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  ascii().reset();
  resetSpreadsheetListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void JazzSSParser::createDocument(librevenge::RVNGSpreadsheetInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getSpreadsheetListener()) {
    MWAW_DEBUG_MSG(("JazzSSParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  std::vector<MWAWPageSpan> pageList(1,ps);
  //
  MWAWSpreadsheetListenerPtr listen(new MWAWSpreadsheetListener(*getParserState(), pageList, documentInterface));
  setSpreadsheetListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool JazzSSParser::createZones()
{
#ifdef DEBUG_WITH_FILES
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  // normally contains a string:256 JAZZ 01.000,1
  if (rsrcParser)
    rsrcParser->getEntriesMap();
#endif
  MWAWInputStreamPtr input = getInput();
  input->seek(6, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  while (!input->isEnd()) {
    long pos=input->tell();
    f.str("");
    if (!input->checkPosition(pos+4))
      break;
    int id=int(input->readULong(2));
    f << "Entries(Zone" << id << "A):";
    int len=int(input->readULong(2));
    long endPos=pos+4+len;
    if (!input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    bool isParsed = false, done=false;
    switch (id) {
    // 0 version (already done)
    case 1:
      f.str("");
      f << "Entries(End):";
      if (len!=0)
        f << "###";
      else
        done=true;
      break;
    case 6:
      isParsed = readSheetSize(endPos);
      break;
    case 0xc: // blank cell
    case 0xd: // integer cell
    case 0xe: // floating cell
    case 0xf: // label cell
    case 0x10: // formula cell
      isParsed = readCell(id, endPos);
      break;
    case 0x11:
      isParsed = readZone11(endPos);
      break;
    case 0x12:
    case 0x13:
      if (len%2) {
        MWAW_DEBUG_MSG(("JazzSSParser::createZones: unexpected size for page break\n"));
        f << "###";
        break;
      }
      f.str("");
      f << "Entries(" << (id==0x12 ? "ColBreak" : "RowBreak") << "):";
      f << "br=[";
      for (int i=0; i<len/2; ++i) f << input->readLong(2) << ",";
      f << "],";
      break;
    case 0x15:
      if (len!=0x126 && len!=0x16c) {
        MWAW_DEBUG_MSG(("JazzSSParser::createZones: unexpected size for document\n"));
        f << "###";
        break;
      }
      isParsed=readDocument(endPos);
      break;
    default:
      break;
    }
    if (len && input->tell()!=endPos)
      ascii().addDelimiter(input->tell(),'|');
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    if (!isParsed) {
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    if (done)
      break;
  }
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("JazzSSParser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(BAD):###");
  }
  return !m_state->m_widths.empty() && !m_state->m_posToCells.empty();
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// spreadsheet
////////////////////////////////////////////////////////////
bool JazzSSParser::readZone11(long endPos)
{
  auto input=getInput();
  long pos=input->tell();
  long len=endPos-pos;
  if (len<28) {
    MWAW_DEBUG_MSG(("JazzSSParser::createZones: unexpected size for name cells\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f.str("");
  f << "Entries(Zone11):";
  std::string name;
  for (int i=0; i<16; ++i) {
    char c=char(input->readULong(1));
    if (c==0)
      break;
    name+=c;
  }
  f << name << ",";
  input->seek(pos+4+16, librevenge::RVNG_SEEK_SET);
  int val=int(input->readLong(2));
  switch (val) {
  case 0:
    f << "name,";
    break;
  case 7:
    f << "sort,";
    break;
  case 8:
    f << "distribution,";
    break;
  case 9:
    f << "table,";
    break;
  default:
    f << "f0=" << val << ",";
    break;
  }
  int dim[4];
  for (auto &d : dim) d=int(input->readULong(2));
  f << "range=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  if (len==28) {
    val=int(input->readLong(2));
    if (val!=4) f << "f1=" << val << ",";
  }
  ascii().addPos(pos-4);
  ascii().addNote(f.str().c_str());
  return true;
}

bool JazzSSParser::readDocument(long endPos)
{
  auto input=getInput();
  long pos = input->tell();
  if (endPos-pos != 0x126 && endPos-pos != 0x16c) {
    MWAW_DEBUG_MSG(("JazzSSParser::readDocument: block is too short\n"));
    return false;
  }
  m_state->m_isDatabase=endPos-pos==0x16c;
  libmwaw::DebugStream f;
  f << "Entries(Document):";
  int val;
  if (!m_state->m_isDatabase) {
    for (int i=0; i<4; ++i) {
      val=int(input->readULong(2));
      int const expected[]= {0,0x100,0,0x7150};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
    int dim[2], dim2[2];
    for (auto &d : dim) d=int(input->readULong(2));
    for (auto &d : dim2) d=int(input->readULong(2));
    if (dim[0]!=dim2[0] || dim[1]!=dim2[1])
      f << "select=" << MWAWVec2i(dim[0],dim[1]) << "<->" << MWAWVec2i(dim2[0],dim2[1]) << ",";
    else
      f << "select=" << MWAWVec2i(dim[0],dim[1]) << ",";
    for (auto &d : dim2) d=int(input->readULong(2));
    if (dim[0]!=dim2[0] || dim[1]!=dim2[1])
      f << "pos="  << MWAWVec2i(dim2[0],dim2[1]) << ",";
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(2));
      int const expected[]= {0,5};
      if (val!=expected[i])
        f << "f" << i+4 << "=" << val << ",";
    }
  }
  int defWidth=int(input->readULong(1)); // width in number of char
  if (defWidth!=7) f << "w[def]=" << defWidth << ",";
  ascii().addPos(pos-4);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-width:";
  std::vector<int> colWidths;
  colWidths.resize(255);
  for (size_t i=0; i<256; ++i) { // w0=0, w1 correspond to the width of column A, ...
    val=int(input->readULong(1));
    if (i)
      colWidths[i-1]=val ? val : defWidth;
    if (val) f << "w" << i << "=" << val << ",";
  }
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-A:";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(1));
    if (val==-1) continue;
    char const *wh[]= {"border","grid"};
    if (val==0) f << "hide[" << wh[i] << "],";
    else f << "#show[" << wh[i] << "]=" << val << ",";
  }
  MWAWFont &font=m_state->m_font;
  font.setId(int(input->readULong(2)));
  float fSz=float(input->readULong(2));
  font.setSize(fSz);
  val=int(input->readULong(1));
  unsigned flags=0;
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  val &= 0xe0;
  font.setFlags(flags);
  f << "font=[";
  f << font.getDebugString(getFontConverter());
  if (val)
    f << "fl=" << std::hex << val << std::dec << ",";
  f << "],";
  val=int(input->readULong(1));
  if (val&0x8)
    f << "show[formula],";
  val&=0xf7;
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(2));
    if (val) f << "g" << i+1 << "=" << val << ",";
  }
  // time to update the columns width
  m_state->m_widths.clear();
  m_state->m_widths.reserve(255);
  for (auto &w : colWidths) m_state->m_widths.push_back(float(w)*fSz);

  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(), '|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool JazzSSParser::readCell(int id, long endPos)
{
  auto input=getInput();
  long pos = input->tell();
  if (endPos-pos < 6) {
    MWAW_DEBUG_MSG(("JazzSSParser::readCell: block is too short\n"));
    return false;
  }
  JazzSSParserInternal::Cell cell;
  MWAWCell::Format format;
  MWAWCellContent &content=cell.m_content;
  libmwaw::DebugStream f;
  f << "Entries(Cell):";
  int val=int(input->readULong(2));
  if (val&0x8000)
    f << "locked,";
  int type=(val>>12)&7;
  int digits=(val>>8)&0xf;
  format.m_digits=digits;
  switch (type) {
  case 0:
    f << "fixed,";
    format.m_format=MWAWCell::F_NUMBER;
    format.m_numberFormat=MWAWCell::F_NUMBER_DECIMAL;
    break;
  case 1:
    f << "scientific,";
    format.m_format=MWAWCell::F_NUMBER;
    format.m_numberFormat=MWAWCell::F_NUMBER_SCIENTIFIC;
    break;
  case 2:
    f << "currency,";
    format.m_format=MWAWCell::F_NUMBER;
    format.m_numberFormat=MWAWCell::F_NUMBER_CURRENCY;
    break;
  case 3:
    f << "percent,";
    format.m_format=MWAWCell::F_NUMBER;
    format.m_numberFormat=MWAWCell::F_NUMBER_PERCENT;
    break;
  case 4:
    f << "thousand,";
    format.m_format=MWAWCell::F_NUMBER;
    format.m_thousandHasSeparator=true;
    break;
  case 5:
    f << "text,";
    format.m_format=MWAWCell::F_TEXT;
    switch (digits&3) {
    case 0:
      cell.setHAlignment(MWAWCell::HALIGN_LEFT);
      f << "left,";
      break;
    case 1:
      cell.setHAlignment(MWAWCell::HALIGN_RIGHT);
      f << "right,";
      break;
    case 2:
      cell.setHAlignment(MWAWCell::HALIGN_CENTER);
      f << "center,";
      break;
    default:
    case 3: {
      static bool first=true;
      if (first) {
        first=false;
        MWAW_DEBUG_MSG(("JazzSSParser::readCell: repeated text is ignored\n"));
      }
      f << "#repeat,";
      break;
    }
    }
    if (digits&4)
      f << "extend[cell],";
    if (digits&8) {
      MWAW_DEBUG_MSG(("JazzSSParser::readCell: unknown text align8\n"));
      f << "##align8,";
    }
    digits=2;
    break;
  case 7:
    switch (digits) {
    case 0: // a bar 0 empty, 10? full
      f << "bar,";
      format.m_format=MWAWCell::F_NUMBER;
      break;
    case 1:
      f << "number[general],";
      format.m_format=MWAWCell::F_NUMBER;
      break;
    case 2:
    case 3:
    case 4:
    case 5: {
      f << "date,";
      format.m_format=MWAWCell::F_DATE;
      char const *wh[]= {"%d-%b-%y", "%d-%b", "%b-%y", "%m/%d/%y"};
      format.m_DTFormat=wh[digits-2];
      f << format.m_DTFormat << ",";
      break;
    }
    case 7:
    case 8:
    case 9:
    case 10: {
      f << "time,";
      format.m_format=MWAWCell::F_TIME;
      char const *wh[]= {"%I:%M:%S %p", "%I:%M %p", "%H:%M:%S", "%H:%M"};
      format.m_DTFormat=wh[digits-7];
      f << format.m_DTFormat << ",";
      break;
    }
    case 11:
      f << "text[formula],";
      break;
    case 15:
      f << "general,";
      break;
    default:
      MWAW_DEBUG_MSG(("JazzSSParser::readCell: unknown format=7\n"));
      f << "##type1=" << digits << ",";
      break;
    }
    break;
  default:
    MWAW_DEBUG_MSG(("JazzSSParser::readCell: unknown format=6\n"));
    f << "##type=" << type << ",";
    break;
  }
  if (type!=7 && digits!=2)
    f << "digits=" << digits << ",";

  if (val&1)
    f << "formula[text],";
  if (val&2)
    f << "check[entry],";
  MWAWFont font=m_state->m_font;
  if (val&4) {
    font.setFlags(font.flags()|MWAWFont::hiddenBit);
    f << "hide,";
  }
  int iFormat=val&0xf9;
  if (iFormat!=0x80)
    f << "format=" << std::hex << iFormat << std::dec << ",";
  int cellPos[2];
  for (auto &p : cellPos) p=int(input->readULong(2));
  f << "C" << cellPos[0] << "R" << cellPos[1] << ",";
  MWAWVec2i const cPos(cellPos[0],cellPos[1]);
  cell.setPosition(cPos);
  cell.setFont(font);
  cell.setFormat(format);

  bool ok=false;
  long dataSz=endPos-input->tell();
  switch (id) {
  case 12: // empty
    f << "empty,";
    ok=dataSz==0;
    content.m_contentType=MWAWCellContent::C_NONE;
    break;
  case 13: {
    f << "int,";
    if (dataSz!=2)
      break;
    val=int(input->readLong(2));
    f << "val=" << val << ",";
    content.m_contentType=content.C_NUMBER;
    content.setValue(double(val));
    ok=true;
    break;
  }
  case 14: {
    if (dataSz!=10)
      break;
    f << "double,";
    ok=true;
    double res;
    bool isNan;
    content.m_contentType=content.C_NUMBER;
    if (!input->readDouble10(res, isNan)) {
      MWAW_DEBUG_MSG(("JazzSSParser::readCell: can not read a double\n"));
      f << "###nan,";
    }
    else {
      content.setValue(res);
      f << "val=" << res << ",";
    }
    break;
  }
  case 15: {
    if (dataSz<1)
      break;
    int sz=int(input->readULong(1));
    if (sz+1>dataSz)
      break;
    f << "text,";
    ok=true;
    content.m_contentType=content.C_TEXT;
    content.m_textEntry.setBegin(input->tell());
    content.m_textEntry.setLength(sz);
    std::string text;
    for (int i=0; i<sz; ++i) text+=char(input->readULong(1));
    f << text;
    break;
  }
  case 16: {
    f << "formula,";
    if (iFormat&1) {
      int sz=int(input->readULong(1));
      if (1+sz+2>dataSz)
        break;
      content.m_contentType=content.C_TEXT;
      content.m_textEntry.setBegin(input->tell());
      content.m_textEntry.setLength(sz);
      std::string res;
      for (int i=0; i<sz; ++i) res+=char(input->readULong(1));
      f << res << ",";
    }
    else {
      if (dataSz<12)
        break;
      content.m_contentType=content.C_NUMBER;
      double res;
      bool isNan;
      if (!input->readDouble10(res, isNan)) {
        // can be ok if the result is nan
        static bool first=true;
        if (first) {
          first=false;
          MWAW_DEBUG_MSG(("JazzSSParser::readCell: can not read some double\n"));
        }
        f << "#nan,";
      }
      else {
        content.setValue(double(res));
        f << "val=" << res << ",";
      }
      input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    }
    ok=true;
    std::string error;
    if (!readFormula(endPos, cPos, content.m_formula, error))
      f << "###";
    else
      content.m_contentType=content.C_FORMULA;
    for (auto const &instr : content.m_formula) f << instr << ",";
    if (!error.empty()) f << error;
    break;
  }
  default:
    break;
  }
  if (!ok) {
    f << "###";
  }
  if (m_state->m_posToCells.find(cPos)!=m_state->m_posToCells.end()) {
    MWAW_DEBUG_MSG(("JazzSSParser::readCell: find a dupplicated cell\n"));
    f << "##dupplicated";
  }
  else if (cPos[0]>=0x100 || cPos[1]>=0x2000) {
    MWAW_DEBUG_MSG(("JazzSSParser::readCell: the cell position seems bad\n"));
    f << "##dupplicated";
  }
  else
    m_state->m_posToCells[cPos]=cell;
  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(), '|');
  ascii().addPos(pos-4);
  ascii().addNote(f.str().c_str());

  return true;
}

bool JazzSSParser::readSheetSize(long endPos)
{
  auto input=getInput();
  long pos = input->tell();
  if (endPos-pos < 6) {
    MWAW_DEBUG_MSG(("JazzSSParser::readSheetSize: block is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(SheetSize):";
  input->seek(2, librevenge::RVNG_SEEK_CUR); // clearly junk
  int dims[2];
  for (auto &d : dims) d=int(input->readLong(2));
  f << "dims=" << MWAWVec2i(dims[0], dims[1]) << ",";
  ascii().addPos(pos-4);
  ascii().addNote(f.str().c_str());

  // empty spreadsheet
  if (dims[0]==-1 && dims[1]==-1) return true;
  if (dims[0] < 0 || dims[1] < 0) return false;

  m_state->m_dimensions=MWAWVec2i(dims[0], dims[1]);
  return true;
}

////////////////////////////////////////////////////////////
// formula
////////////////////////////////////////////////////////////
namespace JazzSSParserInternal
{
struct Functions {
  char const *m_name;
  int m_arity;
};

static Functions const s_listFunctions[] = {
  { "", 0} /*SPEC: number*/, {"", 0}/*SPEC: cell*/, {"", 0}/*SPEC: cells*/, {"=", 1} /*SPEC: end of formula*/,
  { "(", 1} /* SPEC: () */, {"", 0}/*SPEC: number*/, { "", -2} /*SPEC: text*/, {"", -2}/*unused*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/, { "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,

  // 10-1f
  { "-", 1}, { "+", 2}, { "-", 2}, { "*", 2},
  { "/", 2}, { "^", 2}, { "=", 2}, { "<>", 2},
  { "<=", 2}, { ">=", 2}, { "<", 2}, { ">", 2},
  { "And", 2}, { "Or", 2}, { "Not", 1}, { "+", 1},

  // 20-2f
  { "&", 2}, { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/, { "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,

  // 30-3f
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/, { "Repeat", 2},
  { "Replace", 4}, { "Left", 2}, { "Right", 2}, { "Lower", 1},
  { "Upper", 1}, { "Proper", 1}, { "Clean", 1}, { "", -2} /*UNKN*/,
  { "Trim", 1}, { "Exact", 2}, { "CellPointer", 1}, { "IsBlank", 1},

  // 40-4f
  { "NA", 0}, { "Err", 0}, { "Abs", 1}, { "Int", 1},
  { "Sqrt", 1}, { "Log", 1}, { "Ln", 1}, { "Pi", 0},
  { "Sin", 1}, { "Cos", 1}, { "Tan", 1}, { "Atan2", 2},
  { "Atan", 1}, { "Asin", 1}, { "Acos", 1}, { "Exp", 1},

  // 50-5f
  { "Mod", 2}, { "Choose", -1}, { "IsNa", 1}, { "IsErr", 1},
  { "False", 0}, { "True", 0}, { "Rand", 0}, { "Date", 3},
  { "Now", 0}, { "PMT", 3}, { "PV", 3}, { "FV", 3},
  { "If", 3}, { "Day", 1}, { "Month", 1}, { "Year", 1},

  // 60-6f
  { "Round", 2}, { "Time", 3}, { "Hour", 1}, { "Minute", 1},
  { "Second", 1}, { "IsNumber", 1}, { "IsString", 1}, { "Length", 1},
  { "Value", 1}, { "Fixed", 2}, { "SubStr", 3}, { "Char", 1},
  { "Code", 1}, { "Find", 3}, { "DateValue", 1}, { "", -2} /*UNKN*/,

  // 70-7f
  { "", -2} /*UNKN*/,{ "IsRef", 1}, { "CpySgn", 2}, { "Scale", 2},
  { "Ln1", 1}, { "Exp2", 1}, { "Exp1", 1}, { "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/, { "TimeValue", 1}, { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,

  // 80-8f
  { "Sum", -1}, { "Avg", -1}, { "Count", -1}, { "Min", -1},
  { "Max", -1}, { "VLookUp", 3}, { "NPV", 2}, { "Var", -1},
  { "Std", -1}, { "IRR", 2}, { "HLookUp", 3}, { "DSum", 3},
  { "DAvg", 3}, { "DCount", 3}, { "DMin", 3}, { "DMax", 3},

  // 90-9f
  { "DVar", 3}, { "DStd", 3}, { "Index", 3}, { "Cols", 1},
  { "Rows", 1}, { "N", 1}, { "S", 1}, { "", -2} /*UNKN*/,
  { "Cell", 2}, { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2},

  // a0-af
  { "FCount", 1}, { "FSum", 1}, { "FAVG", 1}, { "FMin", 1},
  { "FMax", 1}, { "FStd", 1}, { "FVar", 1}, { "FPage", 0},
  { "FPrev", 0}, { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2},
  { "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2} /*UNKN*/,{ "", -2},
};
}

bool JazzSSParser::readCell(MWAWVec2i actPos, MWAWCellContent::FormulaInstruction &instr)
{
  instr=MWAWCellContent::FormulaInstruction();
  instr.m_type=MWAWCellContent::FormulaInstruction::F_Cell;
  bool ok = true;
  int pos[2];
  bool absolute[2] = { true, true};
  auto input=getInput();
  for (int dim = 0; dim < 2; dim++) {
    auto val = int(input->readULong(2));
    if ((val & 0x8000) == 0); // absolue value ?
    else {
      // relative
      int const maxVal=0x2000;
      val &= (2*maxVal-1);
      if (val & maxVal) val = val - 2*maxVal;
      if (val+actPos[dim]>=maxVal) val-=maxVal;
      val += actPos[dim];
      absolute[dim] = false;
    }
    pos[dim] = val;
  }
  if (pos[0] < 0 || pos[1] < 0) {
    if (ok) {
      MWAW_DEBUG_MSG(("JazzSSParser::readCell: can not read cell position\n"));
    }
    return false;
  }
  if (pos[0]>=0x100) pos[0]%=0x100;
  instr.m_position[0]=MWAWVec2i(pos[0],pos[1]);
  instr.m_positionRelative[0]=MWAWVec2b(!absolute[0],!absolute[1]);
  return ok;
}

bool JazzSSParser::readFormula(long endPos, MWAWVec2i const &position,
                               std::vector<MWAWCellContent::FormulaInstruction> &formula, std::string &error)
{
  formula.resize(0);
  error = "";
  auto input=getInput();
  long pos = input->tell();
  if (endPos - pos < 2) return false;
  auto sz = int(input->readULong(2));
  if (endPos-pos-2 != sz || !input->checkPosition(endPos)) return false;

  std::stringstream f;
  std::vector<std::vector<MWAWCellContent::FormulaInstruction> > stack;
  bool ok = true;
  while (long(input->tell()) != endPos) {
    pos = input->tell();
    if (pos > endPos) return false;
    auto wh = int(input->readULong(1));
    int arity = 0;
    MWAWCellContent::FormulaInstruction instr;
    switch (wh) {
    case 0x0: {
      double val;
      bool isNaN;
      if (endPos-pos<10 || !input->readDouble10(val, isNaN)) {
        f.str("");
        f << "###number";
        error=f.str();
        ok = false;
        break;
      }
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Double;
      instr.m_doubleValue=val;
      break;
    }
    case 0x1: {
      if (endPos-pos<7) {
        f.str("");
        f << "###cell short";
        error=f.str();
        ok = false;
        break;
      }
      ok = readCell(position, instr);
      int val=int(input->readULong(2));
      if (val) {
        MWAW_DEBUG_MSG(("JazzSSParser::readFormula: oops find a sheet val=%d\n", val));
        f << "##sheet=" << val << ",";
      }
      break;
    }
    case 0x2: {
      if (endPos-pos< 1+10 || !readCell(position, instr)) {
        f.str("");
        f << "###list cell short";
        error=f.str();
        ok = false;
        break;
      }
      MWAWCellContent::FormulaInstruction instr2;
      if (!readCell(position, instr2)) {
        ok = false;
        f.str("");
        f << "###list cell short(2)";
        error=f.str();
        break;
      }
      instr.m_type=MWAWCellContent::FormulaInstruction::F_CellList;
      instr.m_position[1]=instr2.m_position[0];
      instr.m_positionRelative[1]=instr2.m_positionRelative[0];
      int val=int(input->readULong(2));
      if (val) {
        MWAW_DEBUG_MSG(("JazzSSParser::readFormula: oops find a sheet val=%d\n", val));
        f << "##sheet=" << val << ",";
      }
      break;
    }
    case 0x5:
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Long;
      instr.m_longValue=double(input->readLong(2));
      break;
    case 0x6: {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
      int sSz=int(input->readULong(1));
      if (input->tell()+sSz > endPos) {
        ok=false;
        break;
      }
      for (int i=0; i<sSz; ++i) {
        auto c = char(input->readULong(1));
        if (c==0) break;
        instr.m_content += c;
      }
      break;
    }
    default:
      if (wh >= 0xb0 || JazzSSParserInternal::s_listFunctions[wh].m_arity == -2) {
        f.str("");
        f << "##Funct" << std::hex << wh;
        error=f.str();
        ok = false;
        break;
      }
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Function;
      instr.m_content=JazzSSParserInternal::s_listFunctions[wh].m_name;
      ok=!instr.m_content.empty();
      arity = JazzSSParserInternal::s_listFunctions[wh].m_arity;
      if (arity == -1) arity = int(input->readLong(1));
      break;
    }

    if (!ok) break;
    std::vector<MWAWCellContent::FormulaInstruction> child;
    if (instr.m_type!=MWAWCellContent::FormulaInstruction::F_Function) {
      child.push_back(instr);
      stack.push_back(child);
      continue;
    }
    size_t numElt = stack.size();
    if (int(numElt) < arity) {
      f.str("");
      f << instr.m_content << "[##" << arity << "]";
      error=f.str();
      ok = false;
      break;
    }
    if ((instr.m_content[0] >= 'A' && instr.m_content[0] <= 'Z') || instr.m_content[0] == '(') {
      if (instr.m_content[0] != '(')
        child.push_back(instr);

      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      instr.m_content="(";
      child.push_back(instr);
      for (int i = 0; i < arity; i++) {
        if (i) {
          instr.m_content=";";
          child.push_back(instr);
        }
        auto const &node=stack[size_t(int(numElt)-arity+i)];
        child.insert(child.end(), node.begin(), node.end());
      }
      instr.m_content=")";
      child.push_back(instr);

      stack.resize(size_t(int(numElt)-arity+1));
      stack[size_t(int(numElt)-arity)] = child;
      continue;
    }
    if (arity==1) {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      stack[numElt-1].insert(stack[numElt-1].begin(), instr);
      if (wh==3)
        break;
      continue;
    }
    if (arity==2) {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      stack[numElt-2].push_back(instr);
      stack[numElt-2].insert(stack[numElt-2].end(), stack[numElt-1].begin(), stack[numElt-1].end());
      stack.resize(numElt-1);
      continue;
    }
    ok=false;
    error = "### unexpected arity";
    break;
  }

  if (!ok) ;
  else if (stack.size()==1 && stack[0].size()>1 && stack[0][0].m_content=="=") {
    formula.insert(formula.begin(),stack[0].begin()+1,stack[0].end());
    if (input->tell()!=endPos) {
      MWAW_DEBUG_MSG(("JazzSSParser::readFormula: find some extra data\n"));
      error="##extra data";
      ascii().addDelimiter(input->tell(),'#');
    }
    return true;
  }
  else
    error = "###stack problem";

  static bool first = true;
  if (first) {
    MWAW_DEBUG_MSG(("JazzSSParser::readFormula: I can not read some formula\n"));
    first = false;
  }

  f.str("");
  for (auto const &i : stack) {
    for (auto const &j : i)
      f << j << ",";
  }
  f << error << "###";
  error = f.str();
  return false;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool JazzSSParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = JazzSSParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  if (!input->checkPosition(46+0x100)) {
    MWAW_DEBUG_MSG(("JazzSSParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(2)!=0 || input->readULong(2)!=2)
    return false;
  libmwaw::DebugStream f;
  f << "FileHeader:";
  int val=int(input->readLong(2));
  if (val!=0xb)
    f << "vers=" << val << ",";
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  if (strict) {
    bool ok=false;
    for (int i=0; i<20; ++i) {
      long pos=input->tell();
      if (!input->checkPosition(pos+4)) {
        MWAW_DEBUG_MSG(("JazzSSParser::checkHeader: file is too short\n"));
        return false;
      }
      int id=int(input->readULong(2));
      int len=int(input->readULong(2));
      if (!input->checkPosition(pos+4+len))
        return false;
      if ((id==6 && len==6) || (id==0x11 && (len==0x1c||len==0x1e)) || (id==0x15 && (len==0x126||len==0x16c))) {
        ok=true;
        break;
      }
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
    }
    if (!ok) {
      MWAW_DEBUG_MSG(("JazzSSParser::checkHeader: can not find any expected zone\n"));
      return false;
    }
  }
  if (header)
    header->reset(MWAWDocument::MWAW_T_JAZZLOTUS, 1, MWAWDocument::MWAW_K_SPREADSHEET);
  return true;
}

////////////////////////////////////////////////////////////
// send spreadsheet
////////////////////////////////////////////////////////////
bool JazzSSParser::sendSpreadsheet()
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  MWAWInputStreamPtr &input= getInput();
  if (!listener) {
    MWAW_DEBUG_MSG(("JazzSSParser::sendSpreadsheet: I can not find the listener\n"));
    return false;
  }
  listener->openSheet(m_state->m_widths, librevenge::RVNG_POINT);

  int prevRow = -1;
  for (auto const &it : m_state->m_posToCells) {
    auto const &cell=it.second;
    if (cell.position()[1]>prevRow) {
      if (prevRow != -1) listener->closeSheetRow();
      int numRepeat=cell.position()[1]-1-prevRow;
      if (numRepeat) {
        listener->openSheetRow(0, librevenge::RVNG_POINT, numRepeat);
        listener->closeSheetRow();
      }
      listener->openSheetRow(0, librevenge::RVNG_POINT);
      prevRow=cell.position()[1];
    }
    listener->openSheetCell(cell, cell.m_content);
    if (cell.m_content.m_textEntry.valid()) {
      listener->setFont(cell.isFontSet() ? cell.getFont() : m_state->m_font);
      input->seek(cell.m_content.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
      while (!input->isEnd() && input->tell()<cell.m_content.m_textEntry.end()) {
        auto c=static_cast<unsigned char>(input->readULong(1));
        if (c==0xd)
          listener->insertEOL();
        else
          listener->insertCharacter(c);
      }
    }
    listener->closeSheetCell();
  }
  if (prevRow!=-1) listener->closeSheetRow();
  listener->closeSheet();
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
