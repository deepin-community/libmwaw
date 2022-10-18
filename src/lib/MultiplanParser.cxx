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
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWSubDocument.hxx"

#include "MultiplanParser.hxx"

/** Internal: the structures of a MultiplanParser */
namespace MultiplanParserInternal
{
////////////////////////////////////////
//! Internal: the state of a MultiplanParser
struct State {
  //! constructor
  State()
    : m_font()
    , m_maximumCell()
    , m_columnPositions()
    , m_cellPositions()
    , m_cellPositionsSet()
    , m_posToLinkMap()
    , m_posToNameMap()
    , m_posToSharedDataSeen()
  {
  }
  //! returns the column width in point
  std::vector<float> getColumnsWidth() const;
  //! the default font
  MWAWFont m_font;
  //! the maximumCell
  MWAWVec2i m_maximumCell;
  //! the columns begin position in point
  std::vector<int> m_columnPositions;
  //! the header/footer/printer message entries
  MWAWEntry m_hfpEntries[3];
  //! the positions of each cell: a vector for each row
  std::vector<std::vector<int> > m_cellPositions;
  //! the list of all position (use for checking)
  std::set<int> m_cellPositionsSet;
  //! the different main spreadsheet zones
  MWAWEntry m_entries[9];
  //! the list of link instruction
  std::map<int, MWAWCellContent::FormulaInstruction> m_posToLinkMap;
  //! the map name's pos to name's cell instruction
  std::map<int, MWAWCellContent::FormulaInstruction> m_posToNameMap;
  //! a set a shared data already seen
  std::set<int> m_posToSharedDataSeen;
};

std::vector<float> State::getColumnsWidth() const
{
  std::vector<float> res;
  bool first=true;
  int lastPos=0;
  float const defWidth=64.f;
  for (auto p : m_columnPositions) {
    if (first) {
      first=false;
      continue;
    }
    if (p<lastPos)
      res.push_back(defWidth);
    else
      res.push_back(float(p-lastPos));
    lastPos=p;
  }
  if (res.size()<64) res.resize(64, defWidth);
  return res;
}

////////////////////////////////////////
//! Internal: the subdocument of a MultiplanParserInternal
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(MultiplanParser &parser, MWAWInputStreamPtr const &input, MWAWEntry const &entry)
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
  MultiplanParser &m_multiParser;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("MultiplanParser::SubDocument::parse: no listener\n"));
    return;
  }

  long pos = m_input->tell();
  m_multiParser.sendText(m_zone);
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
MultiplanParser::MultiplanParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWSpreadsheetParser(input, rsrcParser, header)
  , m_state(new MultiplanParserInternal::State)
{
  setAsciiName("main-1");
  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

MultiplanParser::~MultiplanParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MultiplanParser::parse(librevenge::RVNGSpreadsheetInterface *docInterface)
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
    MWAW_DEBUG_MSG(("MultiplanParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  ascii().reset();
  resetSpreadsheetListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MultiplanParser::createDocument(librevenge::RVNGSpreadsheetInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getSpreadsheetListener()) {
    MWAW_DEBUG_MSG(("MultiplanParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  for (int i=0; i<2; ++i) {
    if (!m_state->m_hfpEntries[i].valid()) continue;
    MWAWHeaderFooter header(i==0 ? MWAWHeaderFooter::HEADER :  MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    header.m_subDocument.reset
    (new MultiplanParserInternal::SubDocument
     (*this, getInput(), m_state->m_hfpEntries[i]));
    ps.setHeaderFooter(header);
  }
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
bool MultiplanParser::createZones()
{
  if (!readPrinterMessage() || !readZoneB()) return false;
  if (!readColumnsPos() || !readPrinterInfo()) return false;
  if (!readHeaderFooter() || !readZoneC()) return false;
  if (!readZonesList()) return false;
  MWAWInputStreamPtr input = getInput();
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("MultiplanParser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Unknown):###extra");
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// spreadsheet
////////////////////////////////////////////////////////////
bool MultiplanParser::readHeaderFooter()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+2*256)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readHeaderFooter: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  for (int i=0; i<2; ++i) {
    pos = input->tell();
    f.str("");
    f << "Entries(HF)[" << (i==0 ? "header" : "footer") << "]:";
    int sSz=int(input->readULong(1));
    m_state->m_hfpEntries[i].setBegin(pos+1);
    m_state->m_hfpEntries[i].setLength(sSz);
    std::string name;
    for (int c=0; c<sSz; ++c) name+=char(input->readULong(1));
    f << name;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+256, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool MultiplanParser::readPrinterInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+0xbc)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readPrinterInfo: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(PrinterInfo):";
  int val=int(input->readULong(2));
  if (val!=0x7fff) f << "f0=" << val << ",";
  val=int(input->readULong(2));
  if (val) f << "f1=" << val << ",";
  f << "left[margin]=" << input->readULong(1) << ",";
  f << "width=" << input->readULong(1) << ",";
  f << "right[margin]=" << input->readULong(1) << ",";
  f << "length=" << input->readULong(1) << ",";
  // then 0 and a string?
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+130, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "PrinterInfo[II]:";
  f << "row[pbBreak]=[";
  for (int i=0; i<32; ++i) {
    val=int(input->readULong(1));
    if (!val) continue;
    for (int d=0, depl=1; d<8; ++d, depl<<=1) {
      if (val&depl) f << i*8+d << ",";
    }
  }
  f << "],";
  f << "col[pbBreak]=[";
  for (int i=0; i<8; ++i) {
    val=int(input->readULong(1));
    if (!val) continue;
    for (int d=0, depl=1; d<8; ++d, depl<<=1) {
      if (val&depl) f << i*8+d << ",";
    }
  }
  f << "],";
  for (int i=0; i<7; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x48,0x48,0x36,0x36,1,1,0};
    if (val==expected[i]) continue;
    if (i==4) {
      if (val==0)
        f << "print[col,row,number]=no,";
      else
        f << "##print[col,row,number]=" << val << ",";
    }
    else
      f << "g" << i << "=" << val << ",";
  }
  m_state->m_font.setId(int(input->readULong(2)));
  m_state->m_font.setSize(float(input->readULong(2)));
  f << "font=[" << m_state->m_font.getDebugString(getFontConverter()) << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+58, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readPrinterMessage()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+256)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readPrinterMessage: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(HF)[printerMessage]:";
  int sSz=int(input->readULong(1));
  m_state->m_hfpEntries[2].setBegin(pos+1);
  m_state->m_hfpEntries[2].setLength(sSz);
  std::string name;
  for (int c=0; c<sSz; ++c) name+=char(input->readULong(1));
  f << name;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+256, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readColumnsPos()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+256)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readColumnsPos: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(ColPos):pos=[";
  for (int i=0; i<64; ++i) {
    m_state->m_columnPositions.push_back(int(input->readULong(2)));
    f << m_state->m_columnPositions.back() << ",";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool MultiplanParser::readZonesList()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+20)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readZonesList: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(ZonesList):";
  int lastPos=0;
  f << "zones=[";
  for (int i=0, w=0; i<10; ++i) {
    int newPos=int(input->readULong(2));
    if (i==6) newPos+=lastPos; // length
    if (i==7) {
      lastPos=newPos;
      continue;
    }
    if (newPos>lastPos) {
      if (!input->checkPosition(pos+20+newPos)) {
        MWAW_DEBUG_MSG(("MultiplanParser::readZonesList: find a bad position"));
        f << "###";
      }
      else {
        m_state->m_entries[w].setBegin(pos+20+lastPos);
        m_state->m_entries[w].setEnd(pos+20+newPos);
      }
      f << std::hex << lastPos << "<->" << newPos << std::dec << ",";
      lastPos=newPos;
    }
    else
      f << "_,";
    ++w;
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<9; ++i) {
    if (!m_state->m_entries[i].valid()) continue;
    bool ok=false;
    std::string name;
    switch (i) {
    case 1:
      ok=readZone1(m_state->m_entries[i]);
      break;
    case 3:
      ok=readCellDataPosition(m_state->m_entries[i]);
      break;
    case 4:
      name="Link";
      break;
    case 5:
      name="Link";
      break;
    case 6:
      name="DataCell";
      break;
    case 7: // the data are normally read in zone 6
      name="SharedData";
      break;
    case 8:
      name="Names";
      break;
    default:
      ok=false;
      break;
    }
    if (ok)
      continue;
    f.str("");
    if (!name.empty())
      f << "Entries(" << name << "):";
    else
      f << "Entries(Zone" << i << "):";
    ascii().addPos(m_state->m_entries[i].begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(m_state->m_entries[i].end());
    ascii().addNote("_");
    input->seek(m_state->m_entries[i].end(), librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool MultiplanParser::readZone1(MWAWEntry const &entry)
{
  if (entry.length()%30) {
    MWAW_DEBUG_MSG(("MultiplanParser::readZone1: the zone size seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Zone1):";
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  int N=int(entry.length()/30);
  for (int i=0; i<N; ++i) {
    /* find something like that
       0000000000fb01d80012001c00fb01d800000000000d0007ffe4ffeec000
       00000000005500550012001c005500550000000000040001ffe4ffeec000
    */
    long pos=input->tell();
    f.str("");
    f << "Zone1-" << i << ":";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+30, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool MultiplanParser::readCellDataPosition(MWAWEntry const &entry)
{
  if (m_state->m_maximumCell[0]<=0 || m_state->m_maximumCell[1]<=0 || entry.length()/m_state->m_maximumCell[0]/2<m_state->m_maximumCell[1]) {
    MWAW_DEBUG_MSG(("MultiplanParser::readCellDataPosition: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(DataPos):";
  m_state->m_cellPositions.resize(size_t(m_state->m_maximumCell[0]));
  auto &posSet=m_state->m_cellPositionsSet;
  for (int i=0; i<m_state->m_maximumCell[0]; ++i) {
    f << "[" << std::hex;
    auto &cellPos=m_state->m_cellPositions[size_t(i)];
    for (int j=0; j<m_state->m_maximumCell[1]; ++j) {
      cellPos.push_back(int(input->readLong(2)));
      posSet.insert(cellPos.back());
      if (cellPos.back())
        f << cellPos.back() << ",";
      else
        f << "_,";
    }
    f << std::dec << "],";
  }
  if (input->tell()!=entry.end()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readCellDataPosition: find extra data\n"));
    f << "###extra";
    ascii().addDelimiter(input->tell(),'|');
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  return true;
}

bool MultiplanParser::readLink(int pos, MWAWCellContent::FormulaInstruction &instr)
{
  auto it=m_state->m_posToLinkMap.find(pos);
  if (it!=m_state->m_posToLinkMap.end()) {
    instr=it->second;
    return true;
  }
  auto const &entry=m_state->m_entries[4];
  if (!entry.valid() || pos<0 || pos+12>entry.length()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readLink: the pos %d seems bad\n", pos));
    return false;
  }
  auto input = getInput();
  long actPos=input->tell();
  long begPos=entry.begin()+pos;
  input->seek(begPos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Link-" << std::hex << pos << std::dec << "[pos]:";
  int dSz=int(input->readULong(1));
  if (dSz<0 || pos+12+dSz > entry.end()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readLink: the pos %d seems bad\n", pos));
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  int type=int(input->readULong(1));
  f << "type=" << type << ",";
  int lPos=int(input->readULong(2));
  if (!readLinkFilename(lPos, instr))
    f << "###";
  f << "pos=" << std::hex << lPos << std::dec << ",";
  int val;
  for (int j=0; j<2; ++j) {
    val=int(input->readULong(1));
    int const expected[]= {0x1a,0x1a};
    if (val!=expected[j]) f << "f" << j+2 << "=" << val << ",";
  }
  for (int j=0; j<3; ++j) {  // f4=1|821
    val=int(input->readULong(2));
    if (val)
      f << "f" << j+4 << "=" << std::hex << val << std::dec << ",";
  }
  bool ok=false;
  switch (type) {
  case 0: {
    ok=true;
    auto fontConverter=getFontConverter();
    auto const fId=m_state->m_font.id();
    librevenge::RVNGString name=instr.m_fileName;
    name.append(':');
    for (int i=0; i<dSz; ++i) {
      auto ch=static_cast<unsigned char>(input->readULong(1));
      int unicode = fontConverter->unicode(fId, static_cast<unsigned char>(ch));
      if (unicode!=-1)
        libmwaw::appendUnicode(uint32_t(unicode), name);
      else if (ch==0x9 || ch > 0x1f)
        libmwaw::appendUnicode(static_cast<uint32_t>(ch), name);
      else {
        f << "##";
        MWAW_DEBUG_MSG(("MultiplanParser::readLink: name seems bad\n"));
      }
    }
    instr.m_type=instr.F_Text;
    instr.m_content=name.cstr();
    break;
  }
  case 1: {
    if (dSz<4)
      break;
    ok=true;
    int rows[2], cols[2];
    for (auto &r : rows) r=int(input->readULong(1));
    for (auto &c : cols) c=int(input->readULong(1));
    for (int j=0; j<2; ++j) {
      instr.m_position[j]=MWAWVec2i(cols[j],rows[j]);
      instr.m_positionRelative[j]=MWAWVec2b(false,false);
    }
    instr.m_type=instr.m_position[0]==instr.m_position[1] ? instr.F_Cell : instr.F_CellList;
    f << instr << ",";
    break;
  }
  default:
    MWAW_DEBUG_MSG(("MultiplanParser::readLink: find unknown type %d\n", type));
    break;
  }
  if (!ok) {
    MWAW_DEBUG_MSG(("MultiplanParser::readLink: can not read link at pos %d\n", pos));
    f << "###";
  }
  else
    m_state->m_posToLinkMap[pos]=instr;
  ascii().addPos(begPos);
  ascii().addNote(f.str().c_str());
  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  return ok;
}

bool MultiplanParser::readLinkFilename(int pos, MWAWCellContent::FormulaInstruction &instr)
{
  MWAWInputStreamPtr input = getInput();
  auto const &entry=m_state->m_entries[5];
  if (!entry.valid() || pos<0 || pos+10>entry.length() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("MultiplanParser::readLinkFilename: the pos %d seems bad\n", pos));
    return false;
  }
  long actPos=input->tell();
  long begPos=entry.begin()+pos;
  input->seek(begPos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Link-" << std::hex << pos << std::dec << ":";
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(2));
    if (val!=1-i) f << "f" << i << "=" << val << ",";
  }
  f << "unkn=" << std::hex << input->readULong(4) << std::dec << ","; // d66aa996 | d66aab1e maybe dirId, fileId
  int dSz=int(input->readULong(1));
  if (begPos+9+dSz>entry.end()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readLinkFilename: the pos %d seems bad\n", pos));
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  librevenge::RVNGString filename;
  auto fontConverter=getFontConverter();
  auto const fId=m_state->m_font.id();
  for (int i=0; i<dSz; ++i) {
    auto ch=static_cast<unsigned char>(input->readULong(1));
    int unicode = fontConverter->unicode(fId, static_cast<unsigned char>(ch));
    if (unicode!=-1)
      libmwaw::appendUnicode(uint32_t(unicode), filename);
    else if (ch==0x9 || ch > 0x1f)
      libmwaw::appendUnicode(static_cast<uint32_t>(ch), filename);
    else {
      f << "##";
      MWAW_DEBUG_MSG(("MultiplanParser::readLinkFilename: dir seems bad\n"));
    }
  }
  instr.m_fileName=filename;
  f << instr.m_fileName.cstr() << ",";
  instr.m_sheet[0]="Sheet0";
  ascii().addPos(begPos);
  ascii().addNote(f.str().c_str());
  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readSharedData(int pos, int cellType, MWAWVec2i const &cellPos, MWAWCellContent &content)
{
  auto const &entry=m_state->m_entries[7];
  MWAWInputStreamPtr input = getInput();
  if (!entry.valid() || pos<0 || pos+3>entry.length() || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("MultiplanParser::readSharedData: the pos %d seems bad\n", pos));
    return false;
  }
  long actPos=input->tell();
  long begPos=entry.begin()+pos;
  input->seek(begPos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "SharedData-" << std::hex << pos << std::dec << ":";
  int type=int(input->readULong(2));
  f << "type=" << (type&3) << ",";
  int N=(type/4);
  if (N!=2) f << "used=" << N << ",";
  int dSz=int(input->readULong(1));
  long endPos=begPos+3+dSz;
  if (endPos>entry.end()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readSharedData: the pos %d seems bad\n", pos));
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  bool ok=true;
  switch (type&3) {
  case 0:
    switch (cellType&3) {
    case 0: {
      double value;
      if (dSz!=8 || !readDouble(value))
        ok=false;
      else {
        content.m_contentType=content.C_NUMBER;
        content.setValue(value);
        f << value << ",";
      }
      break;
    }
    case 1: {
      content.m_contentType=content.C_TEXT;
      content.m_textEntry.setBegin(input->tell());
      content.m_textEntry.setLength(dSz);
      std::string name;
      for (int c=0; c<dSz; ++c) name+=char(input->readULong(1));
      f << name << ",";
      break;
    }
    case 2:
      if (dSz!=8)
        ok=false;
      else {
        f << "Nan" << input->readULong(1) << ",";
        input->seek(7, librevenge::RVNG_SEEK_CUR);
        content.m_contentType=content.C_NUMBER;
        content.setValue(std::nan(""));
      }
      break;
    case 3:
    default:
      if (dSz!=8)
        ok=false;
      else {
        int val=int(input->readULong(1));
        content.m_contentType=content.C_NUMBER;
        content.setValue(val);
        if (val==0)
          f << "false,";
        else if (val==1)
          f << "true,";
        else
          f << "##bool=" << val << ",";
        input->seek(7, librevenge::RVNG_SEEK_CUR);
      }
      break;
    }
    break;
  case 1: {
    std::string err;
    if (!readFormula(cellPos, content.m_formula, endPos, err))
      f << "###";
    else
      content.m_contentType=content.C_FORMULA;
    for (auto const &fo : content.m_formula) f << fo;
    f << ",";
    f << err;
    break;
  }
  default:
    ok=false;
    break;
  }
  if (!ok) {
    MWAW_DEBUG_MSG(("MultiplanParser::readSharedData: can not read data for the pos %d\n", pos));
    f << "###";
  }
  if (m_state->m_posToSharedDataSeen.find(pos)==m_state->m_posToSharedDataSeen.end()) {
    m_state->m_posToSharedDataSeen.insert(pos);
    if (input->tell()!=endPos)
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(begPos);
    ascii().addNote(f.str().c_str());
  }
  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readName(int pos, MWAWCellContent::FormulaInstruction &instruction)
{
  auto it=m_state->m_posToNameMap.find(pos);
  if (it!=m_state->m_posToNameMap.end()) {
    instruction=it->second;
    return true;
  }
  auto const &entry=m_state->m_entries[8]; // the named entry
  if (!entry.valid() || pos<0 || pos+10>=entry.length()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readName: the pos %d seeems bad\n", pos));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long actPos=input->tell();
  long begPos=entry.begin()+pos;
  input->seek(begPos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Names-" << std::hex << pos << std::dec << ":";
  int val=int(input->readULong(1));
  int dSz=(val>>3);
  if (dSz<=0 || begPos+10+dSz>entry.end()) {
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("MultiplanParser::readName: the pos %d seeems bad\n", pos));
    return false;
  }
  if (val&3) f << "f0=" << val << ",";
  val=int(input->readULong(1)); // 40|60
  if (val) f << "f1=" << std::hex << val << std::dec << ",";
  int rows[2];
  for (auto &r : rows) r=int(input->readULong(1));
  val=int(input->readULong(2));
  int cols[2]= {(val>>10),(val>>4)&0x3f};
  for (int i=0; i<2; ++i) {
    instruction.m_position[i]=MWAWVec2i(cols[i], rows[i]);
    instruction.m_positionRelative[i]=MWAWVec2b(false,false);
  }
  instruction.m_type=instruction.m_position[0]==instruction.m_position[1] ? instruction.F_Cell : instruction.F_CellList;
  f << instruction << ",";
  m_state->m_posToNameMap[pos]=instruction;
  if (val&0xf) f << "f2=" << (val&0xf) << ","; // 0|2
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << i+2 << "=" << val << ",";
  }
  std::string name;
  for (int c=0; c<dSz; ++c) name+=char(input->readULong(1));
  f << name << ",";
  ascii().addPos(begPos);
  ascii().addNote(f.str().c_str());

  input->seek(actPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readZoneB()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+82)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readZoneB: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(ZoneB):";
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(2));
  m_state->m_maximumCell=MWAWVec2i(dim[0],dim[1]);
  f << "cell[max]=" << m_state->m_maximumCell << ",";
  int val;
  for (int i=0; i<7; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0,0,0x7fff,0x47,0xc,0x1e7,0x10a};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<15; ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "ZoneB[II]:";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(1));
    if (val!=1-i) f << "f" << i << "=" << val << ",";
  }
  int dim4[4];
  for (auto &d : dim4) d=int(input->readULong(1));
  f << "selection=" << MWAWBox2i(MWAWVec2i(dim4[0],dim4[1]),MWAWVec2i(dim4[2],dim4[3])) << ",";
  for (int i=0; i<19; ++i) { // 0
    val=int(input->readLong(1));
    if (val) f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(pos+82, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MultiplanParser::readZoneC()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+22)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readZoneC: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(ZoneC):";
  int val;
  f << "unkn=[";
  for (int i=0; i<4; ++i) { // small number
    val=int(input->readLong(2));
    if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  val=int(input->readLong(2));
  if (val==1)
    f << "protected,";
  else if (val)
    f << "protected=#" << val << ",";
  val=int(input->readULong(2));
  if (val) f << "passwd[crypted]=" << std::hex << val << std::dec << ",";
  for (int i=0; i<5; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0,0,0,2,1};
    if (val!=expected[i]) f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// double
////////////////////////////////////////////////////////////
bool MultiplanParser::readDouble(double &value)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  value=0;
  if (!input->checkPosition(input->tell()+8)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readDouble: the zone is too short\n"));
    return false;
  }
  int exponant=int(input->readULong(1));
  double sign=1;
  if (exponant&0x80) {
    exponant&=0x7f;
    sign=-1;
  }
  bool ok=true;
  double factor=1;
  for (int i=0; i<7; ++i) {
    int val=int(input->readULong(1));
    for (int d=0; d<2; ++d) {
      int v= d==0 ? (val>>4) : (val&0xf);
      if (v>=10) {
        MWAW_DEBUG_MSG(("MultiplanParser::readDouble: oops find a bad digits\n"));
        ok=false;
        break;
      }
      factor/=10.;
      value+=factor*v;
    }
    if (!ok) break;
  }
  value *= sign*std::pow(10.,exponant-0x40);
  input->seek(pos+8, librevenge::RVNG_SEEK_SET);
  return ok;
}

////////////////////////////////////////////////////////////
// formula
////////////////////////////////////////////////////////////
namespace MultiplanParserInternal
{
struct Functions {
  char const *m_name;
  int m_arity;
};

static Functions const s_listOperators[] = {
  // 0
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 1
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 2
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { ":", 2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { ":", 2}, { "", -2}, { "", -2},
  // 3
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 4
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { ":", 2}, { "", -2}, { "", -2},
  // 5
  { "&", 2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 6
  { "<", 2}, { "", -2}, { "<=", 2}, { "", -2},
  { "=", 2}, { "", -2}, { ">=", 2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 7
  { ">", 2}, { "", -2}, { "<>", 2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  // 8
  { "", -2}, { "", -2}, { "+", 2}, { "", -2},
  { "-", 2}, { "", -2}, { "*", 2}, { "", -2},
  { "/", 2}, { "", -2}, { "^", 2}, { "", -2},
  { "", -2}, { "", -2}, { "-", 1}, { "", -2},
  // 9
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
  { "%", 1}, { "", -2}, { "", -2}, { "", -2},
  { "", -2}, { "", -2}, { "", -2}, { "", -2},
};

static char const *s_listFunctions[]= {
  // 0
  "Count", "If", "IsNA", "IsError",
  "Sum", "Average", "Min", "Max",
  "Row", "Column", "NA", "NPV",
  "Stdev", "Dollar", "Fixed", "Sin",
  // 1
  "Cos", "Tan", "Atan", "Pi",
  "Sqrt", "Exp", "Ln", "Log",
  "Abs", "Int", "Sign", "Round",
  "Lookup", "Index", "Rept", "Mid",
  // 2
  "Length", "Value", "True", "False",
  "And", "Or", "Not", "Mod",
  "IterCnt", "Delta", "PV", "FV",
  "NPer", "PMT", "Rate", "MIRR",
  // 3
  "Irr", nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr,
};
}

bool MultiplanParser::readFormula(MWAWVec2i const &cellPos, std::vector<MWAWCellContent::FormulaInstruction> &formula, long endPos, std::string &error)
{
  formula.clear();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MultiplanParser::readFormula: bad position\n"));
    error="badPos###";
    return false;
  }
  std::vector<std::vector<MWAWCellContent::FormulaInstruction> > stack;
  auto const &listOperators=MultiplanParserInternal::s_listOperators;
  int const numOperators=int(MWAW_N_ELEMENTS(listOperators));
  auto const &listFunctions=MultiplanParserInternal::s_listFunctions;
  int const numFunctions=int(MWAW_N_ELEMENTS(listFunctions));
  bool ok=true;
  int closeDelayed=0;
  bool checkForClose=false;
  while (input->tell()<=endPos) {
    long pos=input->tell();
    int wh=pos==endPos ? -1 : int(input->readULong(1));
    bool needCloseParenthesis=closeDelayed && (checkForClose || pos==endPos);
    ok=true;
    if (closeDelayed && !needCloseParenthesis && wh!=0x3c)
      needCloseParenthesis=wh>=numOperators || listOperators[wh].m_arity!=2;
    while (needCloseParenthesis && closeDelayed>0) {
      auto len=stack.size();
      if (len<2) {
        error="##closedParenthesis,";
        ok=false;
        break;
      }
      auto &dParenthesisFunc=stack[len-2];
      if (dParenthesisFunc.size()!=1 || dParenthesisFunc[0].m_type!=dParenthesisFunc[0].F_Operator ||
          dParenthesisFunc[0].m_content!="(") {
        error="##closedParenthesis,";
        ok=false;
        break;
      }
      dParenthesisFunc.insert(dParenthesisFunc.end(),stack.back().begin(), stack.back().end());
      MWAWCellContent::FormulaInstruction instr;
      instr.m_type=instr.F_Operator;
      instr.m_content=")";
      dParenthesisFunc.push_back(instr);
      stack.resize(len-1);
      --closeDelayed;
    }
    if (!ok || pos==endPos)
      break;
    int arity=0;
    MWAWCellContent::FormulaInstruction instr;
    ok=false;
    bool noneInstr=false, closeFunction=false;
    switch (wh) {
    case 0:
      if (pos+3>endPos || !readLink(int(input->readULong(2)), instr))
        break;
      ok=true;
      break;
    case 0x12: {
      if (pos+2>endPos)
        break;
      ok=true;
      instr.m_type=instr.F_Function;
      int id=int(input->readULong(1));
      if (id<numFunctions && listFunctions[id])
        instr.m_content=listFunctions[id];
      else {
        std::stringstream s;
        s << "Funct" << std::hex << id << std::dec;
        instr.m_content=s.str();
      }
      std::vector<MWAWCellContent::FormulaInstruction> child;
      child.push_back(instr);
      stack.push_back(child);
      instr.m_type=instr.F_Operator;
      instr.m_content="(";
      break;
    }
    case 0x51:
    case 0x71:
    case 0x91:
    case 0xd1:
    case 0xf1:
      closeFunction=ok=true;
      break;
    case 0x1c: // use before %
    case 0x1e: // use for <> A 1e B "code <>"
    case 0x34: // use for <=,>= ... A 34 B "code <=,..."
    case 0x36: // use before -unary
      noneInstr=ok=true;
      break;
    case 0x3a:
      ok=true;
      instr.m_type=instr.F_Operator;
      instr.m_content=";";
      break;
    case 0x3c:
      noneInstr=ok=true;
      ++closeDelayed;
      break;
    case 0x3e:
      ok=true;
      instr.m_type=instr.F_Operator;
      instr.m_content="(";
      break;
    case 0x56: {
      int dSz=int(input->readULong(1));
      if (pos+2+dSz>endPos)
        break;
      instr.m_type=instr.F_Text;
      auto fontConverter=getFontConverter();
      auto const fId=m_state->m_font.id();
      librevenge::RVNGString content;
      for (int i=0; i<dSz; ++i) {
        auto ch=static_cast<unsigned char>(input->readULong(1));
        int unicode = fontConverter->unicode(fId, static_cast<unsigned char>(ch));
        if (unicode!=-1)
          libmwaw::appendUnicode(uint32_t(unicode), content);
        else if (ch==0x9 || ch > 0x1f)
          libmwaw::appendUnicode(static_cast<uint32_t>(ch), content);
        else {
          MWAW_DEBUG_MSG(("MultiplanParser::readFormula: content seen bad seems bad\n"));
          error="##content";
        }
      }
      instr.m_content=content.cstr();
      ok=true;
      break;
    }
    case 0x21: // double
    case 0xe1:
    case 0x8f: // simple
    case 0xef:
      if (pos+3>endPos)
        break;
      instr.m_type=instr.F_Cell;
      instr.m_positionRelative[0]=MWAWVec2b(false,false);
      instr.m_position[0][1]=int(input->readULong(1));
      instr.m_position[0][0]=int(input->readULong(1));
      ok=(instr.m_position[0][0]<63) && (instr.m_position[0][1]<255);
      if (!ok) {
        error="###RorC";
        MWAW_DEBUG_MSG(("MultiplanParser::readFormula: find only row/column reference\n"));
      }
      break;
    case 0x29: // example C2 R1
      MWAW_DEBUG_MSG(("MultiplanParser::readFormula: find union operator\n"));
      error="###union";
      ok=false;
      break;
    case 0x37: // use for list cell
    case 0x53:
    case 0x73:
    case 0x93: // basic cell
    case 0xf3: { // difference ?
      if (pos+3>endPos)
        break;
      instr.m_type=instr.F_Cell;
      instr.m_positionRelative[0]=MWAWVec2b(true,true);
      int val=int(input->readULong(2));
      auto &newPos=instr.m_position[0];
      if (val&0x80)
        newPos[1]=cellPos[1]-(val>>8);
      else
        newPos[1]=cellPos[1]+(val>>8);
      if (val&0x40)
        newPos[0]=cellPos[0]-(val&0x3f);
      else
        newPos[0]=cellPos[0]+(val&0x3f);
      ok=newPos[0]>=0 && newPos[1]>=0;
      break;
    }
    case 0x94:
      if (pos+9>endPos || !readDouble(instr.m_doubleValue))
        break;
      instr.m_type=instr.F_Double;
      ok=true;
      break;
    case 0xf5:
      if (pos+3>endPos || !readName(int(input->readULong(2)), instr))
        break;
      ok=true;
      break;
    default:
      if (wh<numOperators && listOperators[wh].m_arity!=-2) {
        instr.m_content=listOperators[wh].m_name;
        instr.m_type=instr.F_Function;
        arity=listOperators[wh].m_arity;
      }
      if (instr.m_content.empty()) {
        MWAW_DEBUG_MSG(("MultiplanParser::readFormula: find unknown type %x\n", wh));
        std::stringstream s;
        s << "##unkn[func]=" << std::hex << wh << std::dec << ",";
        error=s.str();
        break;
      }
      ok=true;
      break;
    }
    if (!ok) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    checkForClose=!noneInstr && closeDelayed>0;
    if (noneInstr) continue;
    if (closeFunction) {
      ok=false;
      if (stack.empty()) {
        error="##closed,";
        break;
      }
      auto it=stack.end();
      --it;
      for (; it!=stack.begin(); --it) {
        if (it->size()!=1) continue;
        auto const &dInstr=(*it)[0];
        if (dInstr.m_type!=dInstr.F_Operator || dInstr.m_content!="(") continue;
        auto fIt=it;
        --fIt;
        auto &functionStack=*fIt;
        if (functionStack.size()!=1 || functionStack[0].m_type!=functionStack[0].F_Function) continue;
        ok=true;
        for (; it!=stack.end(); ++it)
          functionStack.insert(functionStack.end(), it->begin(), it->end());
        ++fIt;
        stack.erase(fIt, stack.end());
        break;
      }
      if (!ok) {
        error="##closed";
        break;
      }
      instr.m_type=instr.F_Operator;
      instr.m_content=")";
      stack.back().push_back(instr);
      continue;
    }
    std::vector<MWAWCellContent::FormulaInstruction> child;
    if (instr.m_type!=MWAWCellContent::FormulaInstruction::F_Function) {
      child.push_back(instr);
      stack.push_back(child);
      continue;
    }
    size_t numElt = stack.size();
    if (static_cast<int>(numElt) < arity) {
      std::stringstream s;
      s << instr.m_content << "[##" << arity << "]";
      error=s.str();
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      ok=false;
      break;
    }
    if (arity==1) {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      if (instr.m_content=="%")
        stack[numElt-1].push_back(instr);
      else
        stack[numElt-1].insert(stack[numElt-1].begin(), instr);
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
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    break;
  }
  long pos=input->tell();
  if (pos!=endPos || !ok || closeDelayed || stack.size()!=1 || stack[0].empty()) {
    MWAW_DEBUG_MSG(("MultiplanParser::readFormula: can not read a formula\n"));
    ascii().addDelimiter(pos, '|');
    input->seek(endPos, librevenge::RVNG_SEEK_SET);

    std::stringstream s;
    if (!error.empty())
      s << error;
    else
      s << "##unknownError";
    s << "[";
    for (auto const &i : stack) {
      for (auto const &j : i)
        s << j << ",";
    }
    s << "],";
    error=s.str();
    return true;
  }
  formula=stack[0];
  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool MultiplanParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = MultiplanParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  if (!input->checkPosition(0x778)) {
    MWAW_DEBUG_MSG(("MultiplanParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(2)!=0x11ab || input->readULong(2)!=0 ||
      input->readULong(2)!=0x13e8 || input->readULong(2)!=0)
    return false;
  libmwaw::DebugStream f;
  f << "FileHeader:";
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  if (strict) {
    // read the last zone list position and check that it corresponds to a valid position
    input->seek(0x758, librevenge::RVNG_SEEK_SET);
    int val=int(input->readULong(2));
    if (val<0x3c || !input->checkPosition(0x75a+val)) {
      MWAW_DEBUG_MSG(("MultiplanParser::checkHeader: can not find last spreadsheet position\n"));
      return false;
    }
  }
  /* checkme: MsMultiplan DOS begins by a list of 8 potential filename
     linked to this files (with length 0x1f) maybe something like
     that... */
  input->seek(0x30,librevenge::RVNG_SEEK_SET);
  ascii().addPos(0x30);
  ascii().addNote("Entries(ZoneA):");
  for (int i=0; i<4; ++i) {
    long pos=input->tell();
    f.str("");
    f << "ZoneA" << i << ":";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+0x80,librevenge::RVNG_SEEK_SET);
  }
  long pos=input->tell();
  ascii().addPos(pos);
  ascii().addNote("ZoneA4");
  input->seek(0x272,librevenge::RVNG_SEEK_SET);
  if (header)
    header->reset(MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN, 1, MWAWDocument::MWAW_K_SPREADSHEET);
  return true;
}

////////////////////////////////////////////////////////////
// send spreadsheet
////////////////////////////////////////////////////////////
bool MultiplanParser::sendText(MWAWEntry const &entry)
{
  auto listener=getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendText: can not find the listener\n"));
    return false;
  }
  listener->setFont(m_state->m_font);
  auto input=getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  for (long l=0; l<entry.length(); ++l) {
    if (input->isEnd()) {
      MWAW_DEBUG_MSG(("MultiplanParser::sendText: oops, can not read a character\n"));
      break;
    }
    auto c=static_cast<unsigned char>(input->readULong(1));
    if (c==0x9)
      listener->insertTab();
    else if (c==0xa || c==0xd)
      listener->insertEOL();
    else
      listener->insertCharacter(c);
  }
  return true;
}

bool MultiplanParser::sendCell(MWAWVec2i const &cellPos, int p)
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendCell: I can not find the listener\n"));
    return false;
  }
  auto const &entry=m_state->m_entries[6];
  if (p<=0 || p>entry.length()) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendCell: unexpected position %d\n", p));
    return false;
  }
  MWAWCell cell;
  MWAWCellContent content;
  MWAWCell::Format format;
  cell.setPosition(cellPos);
  cell.setFont(m_state->m_font);
  libmwaw::DebugStream f;
  f << "DataCell[C" << cellPos[0]+1 << "R" << cellPos[1]+1 << "]:";
  long pos=entry.begin()+p;
  auto it=m_state->m_cellPositionsSet.find(p);
  ++it;
  long endPos=it!=m_state->m_cellPositionsSet.end() ? entry.begin()+*it : entry.end();
  auto input=getInput();
  if (endPos-pos<4 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendCell: a cell %d seems to short\n", p));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  int formSize=int(input->readULong(1));
  if (formSize) f << "form[size]=" << std::hex << formSize << std::dec << ",";
  int val=int(input->readULong(1));
  int digits=(val&0xf);
  if (digits) f << "decimal=" << digits << ",";
  int form=(val>>4)&7;
  format.m_numberFormat=MWAWCell::F_NUMBER_GENERIC;
  switch (form) {
  case 2:
    format.m_numberFormat=MWAWCell::F_NUMBER_SCIENTIFIC;
    format.m_digits=digits;
    f << "scientific,";
    break;
  case 3:
    format.m_numberFormat=MWAWCell::F_NUMBER_DECIMAL;
    format.m_digits=digits;
    f << "decimal,";
    break;
  case 4: // default
    break;
  case 5:
    format.m_numberFormat=MWAWCell::F_NUMBER_CURRENCY;
    format.m_digits=digits;
    f << "currency,";
    break;
  case 6: // a bar
    f << "bar,";
    break;
  case 7:
    format.m_numberFormat=MWAWCell::F_NUMBER_PERCENT;
    format.m_digits=digits;
    f << "percent,";
    break;
  default:
    f << "format=" << form << ",";
    break;
  }
  cell.setProtected((val&0x80)!=0);
  if ((val&0x80)==0) f << "no[protection],";
  val=int(input->readULong(1));
  int align=(val>>2)&7;
  switch (align) {
  case 1:
    cell.setHAlignment(cell.HALIGN_CENTER);
    f << "center,";
    break;
  case 0: // default
  case 2: // generic
    break;
  case 3:
    cell.setHAlignment(cell.HALIGN_LEFT);
    f << "left,";
    break;
  case 4:
    cell.setHAlignment(cell.HALIGN_RIGHT);
    f << "right,";
    break;
  default:
    f << "#align=" << (align) << ",";
    break;
  }
  switch (val&3) {
  case 0:
    f << "double,";
    format.m_format=MWAWCell::F_NUMBER;
    content.m_contentType=content.C_NUMBER;
    break;
  case 1:
    format.m_format=MWAWCell::F_TEXT;
    content.m_contentType=content.C_TEXT;
    f << "text,";
    break;
  case 2:
    format.m_format=MWAWCell::F_NUMBER;
    content.m_contentType=content.C_NUMBER;
    f << "nan,";
    break;
  case 3: // or nothing
    format.m_format=MWAWCell::F_BOOLEAN;
    content.m_contentType=content.C_NUMBER;
    f << "bool,";
    break;
  default: // impossible
    break;
  }
  cell.setFormat(format);
  if ((val&0x20)==0)
    f << "no20[f1],";
  if (val&0x40)
    f << "shared,";
  int type=(val&0xe3);
  if (val&0x80) f << "80[f1],";
  int dSz=int(input->readULong(1));
  if (endPos<pos+4+dSz) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendCell: a cell seems to short\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  if ((type&0x3)==0 && dSz==8) {
    double value;
    if (!readDouble(value))
      f << "###";
    else
      content.setValue(value);
    f << value << ",";
  }
  else if ((type&0x3)==1 && dSz && pos+4+dSz+((type&0x40) ? 2 : 0) <= endPos) {
    content.m_textEntry.setBegin(input->tell());
    content.m_textEntry.setLength(dSz);
    std::string name;
    for (int c=0; c<dSz; ++c) name+=char(input->readULong(1));
    f << name << ",";
  }
  else if ((type&0x3)==2 && dSz==8) {
    content.setValue(std::nan(""));
    f << "Nan" << input->readULong(1) << ",";
    input->seek(7, librevenge::RVNG_SEEK_CUR);
  }
  else if ((type&0x3)==3 && dSz==8) {
    val=int(input->readULong(1));
    content.setValue(val);
    if (val==0)
      f << "false,";
    else if (val==1)
      f << "true,";
    else
      f << "##bool=" << val << ",";
    input->seek(7, librevenge::RVNG_SEEK_CUR);
  }
  if ((type&0x40) && input->tell()+2<=endPos && (formSize==0 || formSize==2)) {
    if ((input->tell()-pos)%2)
      input->seek(1, librevenge::RVNG_SEEK_CUR);
    int nPos=int(input->readULong(2));
    if (!readSharedData(nPos, type, cellPos, content))
      f << "###";
    f << "sharedData-" << std::hex << nPos << std::dec << ",";
  }
  else if (!(type&0x40) && formSize && input->tell()+formSize<=endPos) {
    auto endFPos=input->tell()+formSize;
    std::string err;
    if (!readFormula(cellPos, content.m_formula, endFPos, err)) {
      ascii().addDelimiter(input->tell(),'|');
      f << "###";
    }
    else
      content.m_contentType=content.C_FORMULA;

    for (auto const &fo : content.m_formula) f << fo;
    f << ",";
    f << err;
    input->seek(endFPos, librevenge::RVNG_SEEK_SET);
  }
  else if (formSize) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendCell: can not read a formula\n"));
    f << "###form";
  }
  listener->openSheetCell(cell, content);
  if (content.m_textEntry.valid()) {
    listener->setFont(cell.getFont());
    input->seek(content.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
    while (!input->isEnd() && input->tell()<content.m_textEntry.end()) {
      auto c=static_cast<unsigned char>(input->readULong(1));
      if (c==0x9)
        listener->insertTab();
      else if (c==0xa || c==0xd)
        listener->insertEOL();
      else
        listener->insertCharacter(c);
    }
  }
  listener->closeSheetCell();
  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool MultiplanParser::sendSpreadsheet()
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MultiplanParser::sendSpreadsheet: I can not find the listener\n"));
    return false;
  }
  listener->openSheet(m_state->getColumnsWidth(), librevenge::RVNG_POINT, std::vector<int>(), "Sheet0");
  auto const &dataEntry=m_state->m_entries[6];
  m_state->m_cellPositionsSet.insert(int(dataEntry.length()));
  for (size_t r=0; r<m_state->m_cellPositions.size(); ++r) {
    auto const &row = m_state->m_cellPositions[r];
    listener->openSheetRow(-16.f, librevenge::RVNG_POINT);
    for (size_t col=0; col<row.size(); ++col) {
      auto p=row[col];
      if (p<0 || p>dataEntry.length()) {
        MWAW_DEBUG_MSG(("MultiplanParser::sendSpreadsheet: find some bad data\n"));
        continue;
      }
      if (!p) continue;
      MWAWVec2i cellPos(static_cast<int>(col), static_cast<int>(r));
      sendCell(cellPos, p);
    }
    listener->closeSheetRow();
  }
  listener->closeSheet();
  return true;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
