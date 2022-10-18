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
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWList.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWSubDocument.hxx"

#include "BeagleWksStructManager.hxx"

#include "BeagleWksDBParser.hxx"

/** Internal: the structures of a BeagleWksDBParser */
namespace BeagleWksDBParserInternal
{
//! Internal: the cell of a BeagleWksDBParser
struct Cell final : public MWAWCell {
  //! the cell type
  enum Type { Text, Number, Date, Time, Picture, Formula, Memo, Unknown };
  //! constructor
  explicit Cell(MWAWVec2i pos=MWAWVec2i(0,0))
    : MWAWCell()
    , m_type(Unknown)
    , m_name("")
    , m_content()
    , m_formula(-1)
    , m_pictureId(-1)
    , m_isEmpty(false)
  {
    setPosition(pos);
  }
  Cell(Cell const &)=default;
  //! destructor
  ~Cell() final;
  //! returns true if the field has no content
  bool empty() const
  {
    return m_content.empty() && (m_type!=Picture || m_pictureId<=0);
  }
  //! the cell type
  Type m_type;
  //! the field name
  std::string m_name;
  //! the cell content
  MWAWCellContent m_content;
  //! the formula id
  int m_formula;
  //! picture id
  int m_pictureId;
  //! flag to know if the cell is empty
  bool m_isEmpty;
};

Cell::~Cell()
{
}

//! Internal: the spreadsheet of a BeagleWksDBParser
struct Database {
  //! constructor
  Database()
    : m_numFields(0)
    , m_fields()
    , m_records()
    , m_memos()
    , m_name("Sheet0")
  {
  }
  //! convert the m_widthCols, m_heightRows in a vector of of point size
  static std::vector<float> convertInPoint(std::vector<int> const &list, float defSize)
  {
    size_t numElt = list.size();
    std::vector<float> res;
    res.resize(numElt);
    for (size_t i = 0; i < numElt; i++) {
      if (list[i] < 0) res[i] = defSize;
      else res[i] = float(list[i]);
    }
    return res;
  }
  //! update a field with the record data
  void updateWithContent(Cell &cell, MWAWVec2i const &pos, MWAWCellContent const &content) const;
  //! the number of rows
  int m_numFields;
  //! the list of fields
  std::vector<Cell> m_fields;
  //! the list of not empty cells (one list by row )
  std::vector<std::vector<MWAWCellContent> > m_records;
  /** the list of memo strings entry */
  std::vector<MWAWEntry> m_memos;
  //! the database name
  std::string m_name;
};

void Database::updateWithContent(Cell &cell, MWAWVec2i const &pos, MWAWCellContent const &content) const
{
  cell.setPosition(pos);
  switch (cell.m_type) {
  case Cell::Formula:
    cell.m_content.m_contentType=MWAWCellContent::C_FORMULA;
    break;
  case Cell::Number:
  case Cell::Date:
  case Cell::Time:
  case Cell::Text:
    cell.m_content=content;
    break;
  case Cell::Memo: {
    if (!content.isValueSet()) break;
    auto id=int(0.1+content.m_value);
    if (id<1 || id>static_cast<int>(m_memos.size())) {
      MWAW_DEBUG_MSG(("BeagleWksDBParserInternal::Database::updateWithContent: can not retrieve the memo content\n"));
      break;
    }
    cell.m_content.m_contentType=MWAWCellContent::C_TEXT;
    cell.m_content.m_textEntry=m_memos[size_t(id-1)];
    break;
  }
  case Cell::Picture:
    cell.m_pictureId=int(0.1+content.m_value);
    break;
#if !defined(__clang__)
  default:
#endif
  case Cell::Unknown:
    break;
  }
  auto const &format=cell.getFormat();
  // change the reference date from 1/1/1904 to 1/1/1900
  if (format.m_format==MWAWCell::F_DATE && cell.m_content.isValueSet())
    cell.m_content.setValue(cell.m_content.m_value+1460.);
  // and try to update the 1D formula in 2D
  auto &formula=cell.m_content.m_formula;
  for (auto &instr : formula) {
    if (instr.m_type==MWAWCellContent::FormulaInstruction::F_Cell) {
      --instr.m_position[0][0];
      instr.m_position[0][1]=pos[1];
    }
    else if (instr.m_type==MWAWCellContent::FormulaInstruction::F_CellList) {
      --instr.m_position[0][0];
      --instr.m_position[1][0];
      instr.m_position[0][1]=instr.m_position[1][1]=pos[1];
    }
  }
}

////////////////////////////////////////
//! Internal: the state of a BeagleWksDBParser
struct State {
  //! constructor
  State()
    : m_databaseBegin(-1)
    , m_database()
    , m_typeEntryMap()
    , m_actPage(0)
    , m_numPages(0)
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }
  /** the database begin position */
  long m_databaseBegin;
  /** the database */
  Database m_database;
  /** the type entry map */
  std::multimap<std::string, MWAWEntry> m_typeEntryMap;
  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

////////////////////////////////////////
//! Internal: the subdocument of a BeagleWksDBParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(BeagleWksDBParser &pars, MWAWInputStreamPtr const &input, MWAWEntry const &entry)
    : MWAWSubDocument(&pars, input, entry)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    return MWAWSubDocument::operator!=(doc);
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("BeagleWksDBParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<BeagleWksDBParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("BeagleWksDBParserInternal::SubDocument::parse: can not find the parser\n"));
    return;
  }
  long pos = m_input->tell();
  listener->setFont(MWAWFont(3,12)); // fixme
  parser->sendText(m_zone, true);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
BeagleWksDBParser::BeagleWksDBParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWSpreadsheetParser(input, rsrcParser, header)
  , m_state()
  , m_structureManager()
{
  init();
}

BeagleWksDBParser::~BeagleWksDBParser()
{
}

void BeagleWksDBParser::init()
{
  resetSpreadsheetListener();
  setAsciiName("main-1");

  m_state.reset(new BeagleWksDBParserInternal::State);
  m_structureManager.reset(new BeagleWksStructManager(getParserState()));

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

MWAWInputStreamPtr BeagleWksDBParser::rsrcInput()
{
  return getRSRCParser()->getInput();
}

libmwaw::DebugFile &BeagleWksDBParser::rsrcAscii()
{
  return getRSRCParser()->ascii();
}

////////////////////////////////////////////////////////////
// position and height
////////////////////////////////////////////////////////////
MWAWVec2f BeagleWksDBParser::getPageLeftTop() const
{
  return MWAWVec2f(float(getPageSpan().getMarginLeft()),
                   float(getPageSpan().getMarginTop()+m_state->m_headerHeight/72.0));
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void BeagleWksDBParser::parse(librevenge::RVNGSpreadsheetInterface *docInterface)
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
      sendDatabase();
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetSpreadsheetListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void BeagleWksDBParser::createDocument(librevenge::RVNGSpreadsheetInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getSpreadsheetListener()) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  int numPages = 1;
  m_state->m_numPages = numPages;

  MWAWEntry header, footer;
  m_structureManager->getHeaderFooterEntries(header,footer);
  std::vector<MWAWPageSpan> pageList;
  MWAWPageSpan ps(getPageSpan());
  if (header.valid()) {
    std::shared_ptr<BeagleWksDBParserInternal::SubDocument> subDoc
    (new BeagleWksDBParserInternal::SubDocument(*this, getInput(), header));
    MWAWHeaderFooter hf(MWAWHeaderFooter::HEADER, MWAWHeaderFooter::ALL);
    hf.m_subDocument=subDoc;
    ps.setHeaderFooter(hf);
  }
  if (footer.valid()) {
    std::shared_ptr<BeagleWksDBParserInternal::SubDocument> subDoc
    (new BeagleWksDBParserInternal::SubDocument(*this, getInput(), footer));
    MWAWHeaderFooter hf(MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    hf.m_subDocument=subDoc;
    ps.setHeaderFooter(hf);
  }
  ps.setPageSpan(numPages);
  pageList.push_back(ps);

  MWAWSpreadsheetListenerPtr listen(new MWAWSpreadsheetListener(*getParserState(), pageList, documentInterface));
  setSpreadsheetListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::createZones()
{
  readRSRCZones();
  MWAWInputStreamPtr input = getInput();
  if (input->seek(66, librevenge::RVNG_SEEK_SET) || !readPrintInfo())
    return false;
  long pos = input->tell();
  if (!input->checkPosition(pos+70)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: the file can not contains Zones\n"));
    return false;
  }

  // now read the list of zones
  libmwaw::DebugStream f;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(Zones):";
  for (int i=0; i<7; ++i) { // checkme: at least 2 zones, maybe 7
    MWAWEntry entry;
    entry.setBegin(input->readLong(4));
    entry.setLength(input->readLong(4));
    entry.setId(static_cast<int>(input->readLong(2)));
    if (entry.length()==0) continue;
    entry.setType(i==1?"Frame":"Unknown");
    f << entry.type() << "[" << entry.id() << "]="
      << std::hex << entry.begin() << "<->" << entry.end() << ",";
    if (!entry.valid() || !input->checkPosition(entry.end())) {
      f << "###";
      if (i<2) {
        MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not read the header zone, stop\n"));
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        return false;
      }
      MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not zones entry %d\n",i));
      continue;
    }
    m_state->m_typeEntryMap.insert
    (std::multimap<std::string, MWAWEntry>::value_type(entry.type(),entry));
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // now parse the different zones
  auto it=m_state->m_typeEntryMap.find("FontNames");
  if (it!=m_state->m_typeEntryMap.end())
    m_structureManager->readFontNames(it->second);
  it=m_state->m_typeEntryMap.find("Frame");
  if (it!=m_state->m_typeEntryMap.end())
    m_structureManager->readFrame(it->second);

  // now parse the different zones
  for (auto eIt : m_state->m_typeEntryMap) {
    MWAWEntry const &entry=eIt.second;
    if (entry.isParsed())
      continue;
    f.str("");
    f << "Entries(" << entry.type() << ")[" << entry.id() << "]:";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }

  input->seek(m_state->m_databaseBegin, librevenge::RVNG_SEEK_SET);
  pos = input->tell();
  if (!m_structureManager->readDocumentInfo())
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  pos = input->tell();
  if (!m_structureManager->readDocumentPreferences())
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  if (!readDatabase())
    return m_state->m_database.m_records.size()!=0;
  pos=input->tell();
  auto N=static_cast<int>(input->readULong(2));
  if (N==0) {
    ascii().addPos(pos);
    ascii().addNote("_");
  }
  else {
    // unsure about the parsing of this zone: find N=2+000602000d02
    f.str("");
    f << "Entries(UnknZone0):";
    if (!input->checkPosition(pos+2+3*N)) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not read UnkZone0\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return true;
    }
    f << "unkn0=[";
    for (int i=0; i< N; ++i)
      f << input->readLong(2) << ",";
    f << "],";
    f << "unkn1=[";
    for (int i=0; i< N; ++i)
      f << input->readLong(1) << ",";
    f << "],";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  pos=input->tell();
  N=static_cast<int>(input->readULong(2));
  if (N==0) {
    ascii().addPos(pos);
    ascii().addNote("_");
  }
  else {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: find data in UnkZone1\n"));
    f.str("");
    f << "Entries(UnknZone1):###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }

  pos=input->tell();
  f.str("");
  f << "Entries(Memo):";
  auto dSz=static_cast<long>(input->readULong(2));
  long endPos=pos+2+dSz;
  N=static_cast<int>(input->readULong(2));
  if (dSz<2+2*long(N) || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: find data in UnkZone1\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Memo-"<< i << ":";
    auto val=static_cast<int>(input->readLong(1));
    if (val) f << "f0=" << val << ",";
    auto sSz=static_cast<int>(input->readULong(1));
    if (pos+2+sSz>endPos) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not read a memo\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return true;
    }
    MWAWEntry memo;
    memo.setBegin(input->tell());
    memo.setLength(sSz);
    m_state->m_database.m_memos.push_back(memo);
    std::string text("");
    for (int c=0; c<sSz; ++c) text+=char(input->readULong(1));
    f << text << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Entries(UnknZone2):";
  dSz=long(input->readULong(2));
  endPos=pos+6+dSz;
  auto val=static_cast<int>(input->readULong(2));
  if ((dSz%2) || val!=0xeb || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not read zone2\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }
  N=static_cast<int>(input->readULong(2));
  if (N) f << "N=" << N << ",";
  if (dSz==2*N) {
    f << "unkn=[";
    for (int i=0; i<N; ++i)
      f << input->readLong(2) << ",";
    f << "],";
  }
  else
    f << "###,";
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  /* now the formula zone */
  auto &fields=m_state->m_database.m_fields;
  int n=0;
  for (auto &field : fields) {
    ++n;
    if (field.m_type!=BeagleWksDBParserInternal::Cell::Formula)
      continue;
    pos=input->tell();
    f.str("");
    f << "Entries(Formula):";
    auto id=static_cast<int>(input->readLong(2));
    if (id!=n-1) f << "#id=" << id << ",";
    dSz=static_cast<int>(input->readULong(2));
    endPos=pos+4+dSz;
    if (dSz==0 || !input->checkPosition(pos+4+dSz)) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::createZones: can not read a formula\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return true;
    }
    std::vector<MWAWCellContent::FormulaInstruction> formula;
    std::string error("");
    if (m_structureManager->readFormula(endPos, MWAWVec2i(id,9), formula, error)) {
      field.m_content.m_formula = formula;
      field.m_content.m_contentType = MWAWCellContent::C_FORMULA;
    }
    else
      f << "###";
    for (auto const &fo : formula)
      f << fo;
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  /*
    now find
    000000000001000100000000 or
    000200000003000100020000
   */
  ascii().addPos(input->tell());
  ascii().addNote("Entries(ZoneEnd)");
  return true;
}

bool BeagleWksDBParser::readRSRCZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser)
    return true;

  auto &entryMap = rsrcParser->getEntriesMap();
  // the 1 zone
  char const *zNames[] = {"wPos", "DMPF" };
  for (int z = 0; z < 2; ++z) {
    auto it = entryMap.lower_bound(zNames[z]);
    while (it != entryMap.end()) {
      if (it->first != zNames[z])
        break;
      MWAWEntry const &entry = it++->second;
      switch (z) {
      case 0: // 1001
        m_structureManager->readwPos(entry);
        break;
      case 1: // find in one file with id=4661 6a1f 4057
        m_structureManager->readFontStyle(entry);
        break;
      /* find also
         - edpt: see sendPicture
         - DMPP: the paragraph style
         - sect and alis: position?, alis=filesystem alias(dir, filename, path...)
      */
      default:
        break;
      }
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+0x70))
    return false;

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
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readPrintInfo: file is too short\n"));
    return false;
  }
  ascii().addPos(input->tell());

  return true;
}

////////////////////////////////////////////////////////////
// database
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::readDatabase()
{
  if (!readFields() || !readLayouts()) return false;

  MWAWInputStreamPtr &input= getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+6)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readDatabase: can not find the database header\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(DbRow):";
  int val;
  for (int i=0; i<2; ++i) { // f0=0|1
    val =static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  val =static_cast<int>(input->readLong(2));
  if (val!=7) f << "f2=" << val << ",";
  auto N=static_cast<int>(input->readLong(2));
  f << "N=" << N << ",";
  val =static_cast<int>(input->readLong(2));
  if (val) f << "f3=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<=N; ++i) {
    if (!readRow()) return false;
  }

  pos=input->tell();
  f.str("");
  f << "Entries(DbFld):";
  val =static_cast<int>(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  val =static_cast<int>(input->readLong(2));
  if (val) f << "f1=" << val << ",";
  auto dSz =static_cast<int>(input->readULong(2));
  f << "dSz=" << dSz << ",";
  if (dSz<14 || dSz>(input->size()-pos-8)/(N+1)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readDatabase: can not find the database field format\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  auto &database=m_state->m_database;
  size_t numFields=database.m_fields.size();
  if (N+1>static_cast<int>(numFields)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readDatabase: the number of field seems too small, must increase them\n"));
    database.m_fields.resize(size_t(N+1));
  }
  std::string extra("");
  for (int i=0; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << "DbFld" << i << ":";
    val=static_cast<int>(input->readULong(2));
    if (val!=1) f << "f0=" << val << ",";
    val=static_cast<int>(input->readLong(2));
    if (val!=0x4b) f << "f1=" << val << ",";
    // checkme: maybe we must use the read id here...
    auto &field=database.m_fields[size_t(i)];
    if (readFormat(field, extra)) {
      f << extra << ",";
      if (dSz>14) { // find also dSz=18 which add c0000034 here
        ascii().addDelimiter(input->tell(),'|');
        input->seek(pos+dSz-2, librevenge::RVNG_SEEK_SET);
        ascii().addDelimiter(input->tell(),'|');
      }
      val=static_cast<int>(input->readLong(2));
      if (val!=i) f << "#id=" << val << ",";
    }
    input->seek(pos+dSz, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

/* read a cell format

   \note: this function is very simillar to BeagleWksSSParser readCellSheet, maybe, we can merge them
 */
bool BeagleWksDBParser::readFormat(MWAWCell &cell, std::string &extra)
{
  MWAWInputStreamPtr &input= getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+8)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readFormat: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  MWAWFont font;
  MWAWCell::Format format;
  auto val=static_cast<int>(input->readULong(1));
  if (val) f << "f0=" << std::hex << val << std::dec << ",";
  val=static_cast<int>(input->readLong(1));
  if (val>0)
    font.setSize(float(val));
  val=static_cast<int>(input->readLong(2));
  if (val>=0)
    font.setId(val);
  auto flag=static_cast<int>(input->readULong(1));
  uint32_t flags=0;
  if (flag&0x8) flags |= MWAWFont::boldBit;
  if (flag&0x10) flags |= MWAWFont::italicBit;
  if (flag&0x20) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x40) flags |= MWAWFont::embossBit;
  if (flag&0x80) flags |= MWAWFont::shadowBit;
  font.setFlags(flags);
  f << font.getDebugString(getParserState()->m_fontConverter);
  if (flag&7) f << "flags=" << (flag&7) << ",";

  auto form=static_cast<int>(input->readULong(1));
  if (form) {
    if (form & 0x10)
      format.m_thousandHasSeparator=true;
    switch (form>>5) {
    case 0: // generic (or picture, memo, etc)
      break;
    case 1:
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_CURRENCY;
      break;
    case 2:
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_PERCENT;
      break;
    case 3:
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_SCIENTIFIC;
      break;
    case 4:
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_DECIMAL;
      break;
    case 5:
      format.m_format=MWAWCell::F_DATE;
      format.m_DTFormat="%m/%d/%y";
      break;
    case 6:
      switch (form & 0x7) {
      case 0:
        format.m_format=MWAWCell::F_DATE;
        format.m_DTFormat="%b %d, %Y";
        break;
      case 1:
        format.m_format=MWAWCell::F_DATE;
        format.m_DTFormat="%B %d, %Y";
        break;
      case 2:
        format.m_format=MWAWCell::F_DATE;
        format.m_DTFormat="%a, %b %d, %Y";
        break;
      case 3:
        format.m_format=MWAWCell::F_DATE;
        format.m_DTFormat="%A, %B %d, %Y";
        break;
      case 4:
        format.m_format=MWAWCell::F_TIME;
        format.m_DTFormat="%I:%M %p";
        break;
      case 5:
        format.m_format=MWAWCell::F_TIME;
        format.m_DTFormat="%I:%M:%S %p";
        break;
      case 6:
        format.m_format=MWAWCell::F_TIME;
        format.m_DTFormat="%H:%M";
        break;
      case 7:
        format.m_format=MWAWCell::F_TIME;
        format.m_DTFormat="%H:%M:%S";
        break;
      default:
        break;
      }
      form &= 0x8;
      break;
    default:
      f << "#form=7:";
      break;
    }
    if (form & 0xf)
      format.m_digits=(form & 0xf);
    f << format;
  }

  val=static_cast<int>(input->readULong(1));
  if (val&0xF0) f << "col?="<<std::hex << (val>>4) << std::dec << ",";
  if (val&0xf) f << "bord?="<<std::hex << (val&0xF) << std::dec << ",";

  val=static_cast<int>(input->readULong(1));
  switch ((val>>5)&7) {
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
  case 3: // default
    break;
  case 4:
    cell.setHAlignment(MWAWCell::HALIGN_LEFT);
    f << "filled,";
    break;
  default:
    f << "#align=" << ((val>>5)&7) << ",";
    break;
  }
  cell.setFormat(format);
  // checkme, this flags seem important, but I am not sure of their meanings
  if (val&0x10)
    cell.setFont(font);
  val &= 0xF;
  if (val!=0x3) f << "flags2=" << std::hex << val << std::dec << ",";
  extra=f.str();
  return true;
}


////////////////////////////////////////////////////////////
// read the row data
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::readRow()
{
  MWAWInputStreamPtr &input= getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;

  auto id=static_cast<int>(input->readLong(2));
  f << "DbRow" << id << ":";
  auto val=static_cast<int>(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  auto dSz=long(input->readULong(2));
  long endPos=pos+6+dSz;
  if (dSz<18 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readRow: can not find the database row %d\n", id));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  val=static_cast<int>(input->readLong(2)); // 0|78|86
  if (val) f << "f1=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  if (val!=-1) f << "f2=" << val << ",";
  f << "fl?=[" << std::hex;
  for (int i=0; i<4; ++i) {
    val=static_cast<int>(input->readULong(2));
    if (val) f << val << ",";
    else f << "_,";
  }
  f << std::dec << "],";
  // now a format
  f << "fId=" << input->readULong(2) << ",";
  f << "fSz=" << input->readULong(2) << ",";
  val=static_cast<int>(input->readULong(2));
  if (val!=dSz) f << "#dSz1=" << val << ",";

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  auto &database=m_state->m_database;
  database.m_records.resize(database.m_records.size()+1);
  auto &records=database.m_records.back();
  int fd=0;
  for (auto const &field : database.m_fields) {
    pos=input->tell();
    if (pos>=endPos) break;
    f.str("");
    f << "DbRow" << id << "-" << fd++ << ":";
    auto fSz=static_cast<int>(input->readULong(1));
    if (fSz==0xFF) {
      ascii().addPos(pos);
      ascii().addNote("_");
      break;
    }
    if (pos+fSz+2>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("BeagleWksDBParser::readRow: file size seems bad\n"));
      break;
    }
    val=static_cast<int>(input->readULong(1));
    if (val!=0x20) f << "fl=" << std::hex << val << std::dec << ",";
    MWAWCellContent content;
    if (fSz && fSz<8) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::readRow: find some very short field\n"));
      f << "###sz=" << fSz << ",";
    }
    else if (fSz) {
      for (int i=0; i<4; ++i) {
        val=static_cast<int>(input->readULong(2));
        if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
      }
      switch (field.m_type) {
      case BeagleWksDBParserInternal::Cell::Formula:
        if (fSz>20) break;
        val=static_cast<int>(input->readULong(2));
        if (val) f << "g0=" << std::hex << val << std::dec << ",";
        MWAW_FALLTHROUGH;
      case BeagleWksDBParserInternal::Cell::Memo: // memoId
      case BeagleWksDBParserInternal::Cell::Picture: // pictId
      case BeagleWksDBParserInternal::Cell::Number:
      case BeagleWksDBParserInternal::Cell::Date:
      case BeagleWksDBParserInternal::Cell::Time: {
        // will be changed by sendDatabase for memo, formula, ...
        content.m_contentType=MWAWCellContent::C_NUMBER;
        if (input->tell()+10>endPos) {
          MWAW_DEBUG_MSG(("BeagleWksDBParser::readRow: can not read some field\n"));
          f << "###";
          break;
        }
        double value;
        bool isNan;
        if (!input->readDouble10(value,isNan)) {
          f << "#" << value << ",";
          break;
        }
        content.setValue(value);
        f << value << ",";
        break;
      }
      case BeagleWksDBParserInternal::Cell::Text: {
        content.m_contentType=MWAWCellContent::C_TEXT;
        content.m_textEntry.setBegin(input->tell());
        content.m_textEntry.setEnd(pos+fSz+2);
        std::string text("");
        while (input->tell()<pos+fSz+2) text+=char(input->readULong(1));
        f << text << ",";
      }
      break;
      case BeagleWksDBParserInternal::Cell::Unknown:
#if !defined(__clang__)
      default:
#endif
        f << "type=" << static_cast<int>(field.m_type) << ",";
        break;
      }
    }
    records.push_back(content);
    if ((fSz%2)) ++fSz;
    input->seek(pos+fSz+2, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  pos=input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readRow: find some extra data\n"));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote("DbRow:#end");
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the fields
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::readFields()
{
  MWAWInputStreamPtr &input= getInput();
  auto &database=m_state->m_database;
  long pos=input->tell();
  if (!input->checkPosition(pos+6)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readFields: can not find the field zone\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Field):";
  auto val=static_cast<int>(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  if (val!=0x2c) // may a type
    f << "f1=" << val << ",";
  database.m_numFields=static_cast<int>(input->readULong(2));
  f << "num[fields]=" << database.m_numFields << ",";
  if (!input->checkPosition(pos+database.m_numFields*64)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readFields: can not find the fields zone\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int fld=0; fld<database.m_numFields; ++fld) {
    pos=input->tell();
    f.str("");
    f << "Field-" << fld << ":";
    BeagleWksDBParserInternal::Cell field;
    auto dSz=long(input->readULong(2));
    long endPos=pos+4+dSz;
    if (dSz<0x3c || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::readFields: can not read a field\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    auto id=static_cast<int>(input->readLong(2));
    if (id) f << "id=" << id << ",";
    auto sSz=static_cast<int>(input->readULong(1));
    if (sSz+1>dSz) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::readFields: can not read a field\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    for (int i=0; i<sSz; ++i) field.m_name += char(input->readULong(1));
    f << "\"" << field.m_name << "\",";
    ascii().addDelimiter(input->tell(),'|');
    input->seek(endPos-10, librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');

    MWAWCell::Format format;
    auto &content=field.m_content;
    auto type=static_cast<int>(input->readLong(1));
    switch (type) {
    case 0:
      field.m_type = BeagleWksDBParserInternal::Cell::Text;
      format.m_format=MWAWCell::F_TEXT;
      content.m_contentType=MWAWCellContent::C_TEXT;
      f << "text,";
      break;
    case 1:
      field.m_type = BeagleWksDBParserInternal::Cell::Number;
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_GENERIC;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      f << "number,";
      break;
    case 2:
      field.m_type = BeagleWksDBParserInternal::Cell::Date;
      format.m_format=MWAWCell::F_DATE;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      f << "date,";
      break;
    case 3:
      field.m_type = BeagleWksDBParserInternal::Cell::Time;
      format.m_format=MWAWCell::F_TIME;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      f << "time,";
      break;
    case 4:
      field.m_type = BeagleWksDBParserInternal::Cell::Picture;
      f << "picture,";
      break;
    case 5:
      field.m_type = BeagleWksDBParserInternal::Cell::Formula;
      format.m_format=MWAWCell::F_NUMBER;
      format.m_numberFormat=MWAWCell::F_NUMBER_GENERIC;
      content.m_contentType=MWAWCellContent::C_FORMULA;
      f << "formula,";
      break;
    case 6:
      field.m_type = BeagleWksDBParserInternal::Cell::Memo;
      format.m_format=MWAWCell::F_TEXT;
      content.m_contentType=MWAWCellContent::C_TEXT;
      f << "memo,";
      break;
    default:
      f << "#type=" << type << ",";
      break;
    }
    f << "form?=" << std::hex << input->readULong(1) << std::dec << ",";
    f << "id2=" << std::hex << input->readULong(4) << std::dec << ",";
    val=static_cast<int>(input->readLong(2)); // 0|-1
    if (val!=-1) f << "g0=" << val << ",";
    f << "g1=" << input->readLong(2) << ",";
    database.m_fields.push_back(field);

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the layouts
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::readLayouts()
{
  MWAWInputStreamPtr &input= getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+6)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can not find the layout zone\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Layout):";
  auto val=static_cast<int>(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  if (val!=0x29) // may a type
    f << "f1=" << val << ",";
  auto numLayouts=static_cast<int>(input->readULong(2));
  f << "num[layout]=" << numLayouts << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int layout=0; layout<numLayouts; ++layout) {
    if (!readLayout(layout)) return false;
  }
  return true;
}

bool BeagleWksDBParser::readLayout(int id)
{
  MWAWInputStreamPtr &input= getInput();
  libmwaw::DebugStream f;
  f << "Layout-" << id << "[A]:";

  long pos=input->tell();
  auto readId=static_cast<int>(input->readULong(1));
  auto dSz=long(input->readULong(2));
  long endPos=pos+1+dSz;
  if (dSz<100 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can find a layout\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  if (readId!=id) f << "#id=" << readId << ",";
  auto val=static_cast<int>(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  val=static_cast<int>(input->readULong(1));
  if (val!=readId) f << "#id1=" << val << ",";
  auto sSz=static_cast<int>(input->readULong(1));
  if (sSz>30) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can find layout string\n"));
    f << "###sSz=" << sSz << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  std::string name("");
  for (int i=0; i<sSz; ++i) name+=char(input->readULong(1));
  f << name << ",";
  input->seek(pos+37, librevenge::RVNG_SEEK_SET);
  f << "ids=[" << std::hex;
  for (int i=0; i<3; ++i) f << input->readULong(4) << ",";
  f << std::dec << "],";
  val=static_cast<int>(input->readLong(2)); // small number
  f << "N=" << val << ",";
  for (int i=0; i<6; ++i) { // f4=0|78, f5=0|68
    static int const expected[]= {0x100, 0, 0, 0, 0, 0xffff };
    val = static_cast<int>(input->readULong(2));
    if (val!=expected[i])
      f << "f" << i+2 << "=" << std::hex << val << std::dec << ",";
  }
  f << "g0=" << std::hex << input->readULong(4) << std::dec << ",";
  f << "id2s=[" << std::hex;
  for (int i=0; i<4; ++i) {
    val=static_cast<int>(input->readULong(i==2 ? 2 : 4));
    if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << std::dec << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[B]:";
  double margins[4];
  f << "margins=[";
  for (double &margin : margins) {
    margin=double(input->readLong(4))/72.;
    f << margin << ",";
  }
  f << "],";
  for (int i=0; i<3; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  f << "id=" << std::hex << input->readULong(4) << std::dec << ",";
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[C]:";
  for (int i=0; i<8; i++) {
    val=static_cast<int>(input->readLong(1));
    if (val==1) f << "fl" << i << ",";
    else if (val) f << "fl" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  //--- now some big unknown zones
  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[C0]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+244, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[C1]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+178, librevenge::RVNG_SEEK_SET);

  /* in some positions there seems be some blocks with size 12
     so let's try this decomposition ( which clearly does not works )..
   */
  for (int i=0; i<87; ++i) {
    pos=input->tell();
    f.str("");
    f << "Layout-" << id << "[C2:" << i << "]:";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[C3]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(pos+1420, librevenge::RVNG_SEEK_SET);

  //--- end of unknown zone
  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[D]:";
  auto N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (!input->checkPosition(pos+2+2*N)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can find zone D\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  if (N) {
    f << "lists=[";
    for (int i=0; i<N; ++i) f << input->readLong(2) << ",";
    f << "],";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[fields]:";
  val=static_cast<int>(input->readULong(2));
  auto type=static_cast<int>(input->readULong(2));
  N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (type!=0x5a || !input->checkPosition(pos+6+36*N)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can find field zone \n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  BeagleWksDBParserInternal::Cell field;
  for (int i=0; i<N; i++) {
    pos=input->tell();
    f.str("");
    f << "Layout-" << id << "[field" << i << "]:";
    id=static_cast<int>(input->readLong(2));
    if (id!=i) f << "id[field]=" << val << ",";
    val=static_cast<int>(input->readLong(2));
    if (val!=0x4b) f << "f0=" << val << ",";
    for (int j=0; j<2; ++j) { // j=0: field name, j=1: field content
      std::string extra("");
      if (!readFormat(field, extra)) {
        f << "###";
        break;
      }
      f << "field" << j << "=[" << extra << "],";
    }
    for (int j=0; j<2; ++j) {
      int dim[4];
      for (auto &k : dim) k=static_cast<int>(input->readLong(2));
      f << "box" << j << "=" << dim[1] << "x" << dim[0] << "<->" << dim[3] << "x" << dim[2] << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+36, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  f.str("");
  f << "Layout-" << id << "[list]:";
  dSz=static_cast<int>(input->readULong(2));
  type=static_cast<int>(input->readLong(2));
  N=static_cast<int>(input->readULong(2));
  endPos=pos+6+dSz;
  if (2*N>dSz || type!=0x75 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::readLayouts: can read zone F\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  if (val!=0x75) f << "f0=" << val << ",";
  f << "lists=[";
  for (int i=0; i<N; ++i) f << input->readLong(2) << ",";
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::sendPageFrames()
{
  auto const &frameMap = m_structureManager->getIdFrameMap();
  for (auto it : frameMap)
    sendFrame(it.second);
  return true;
}

bool BeagleWksDBParser::sendFrame(BeagleWksStructManager::Frame const &frame)
{
  MWAWPosition fPos(MWAWVec2f(0,0), frame.m_dim, librevenge::RVNG_POINT);

  fPos.setPagePos(frame.m_page > 0 ? frame.m_page : 1, frame.m_origin);
  fPos.setRelativePosition(MWAWPosition::Page);
  fPos.m_wrapping = frame.m_wrap==0 ? MWAWPosition::WNone : MWAWPosition::WDynamic;

  auto style=MWAWGraphicStyle::emptyStyle();
  style.setBorders(frame.m_bordersSet, frame.m_border);
  return sendPicture(frame.m_pictId, fPos, true, style);
}

// read/send picture (edtp resource)
bool BeagleWksDBParser::sendPicture
(int pId, MWAWPosition const &pictPos, bool readEDTP, MWAWGraphicStyle const &style)
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::sendPicture: can not find the listener\n"));
    return false;
  }
  auto rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("BeagleWksDBParser::sendPicture: need access to resource fork to retrieve picture content\n"));
      first=false;
    }
    return true;
  }

  librevenge::RVNGBinaryData data;
  if (!m_structureManager->readPicture(pId, data, readEDTP))
    return false;

  listener->insertPicture(pictPos, MWAWEmbeddedObject(data, "image/pict"), style);
  return true;
}

bool BeagleWksDBParser::sendText(MWAWEntry const &entry, bool /*headerFooter*/)
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::sendText: can not find the listener\n"));
    return false;
  }
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::sendText: can not find the entry\n"));
    return false;
  }

  MWAWInputStreamPtr &input= getInput();
  long endPos=entry.end();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    long pos=input->tell();
    if (pos>=endPos) break;
    auto c = static_cast<unsigned char>(input->readULong(1));
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      listener->insertEOL();
      break;
    default:
      listener->insertCharacter(static_cast<unsigned char>(c));
      break;
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
bool BeagleWksDBParser::sendDatabase()
{
  MWAWSpreadsheetListenerPtr listener=getSpreadsheetListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::sendDatabase: I can not find the listener\n"));
    return false;
  }
  MWAWInputStreamPtr input=getInput();
  auto const &database=m_state->m_database;
  auto const &fields = database.m_fields;
  size_t numFields=fields.size();
  auto const &records=database.m_records;
  size_t numRecords=records.size();
  // fixme: use first layout colWidth here
  listener->openSheet(std::vector<float>(1,76), librevenge::RVNG_POINT, std::vector<int>(1,static_cast<int>(numRecords)), "Sheet0");
  for (size_t r=0; r<numRecords; ++r) {
    std::vector<MWAWCellContent> const &row=records[r];
    listener->openSheetRow(12, librevenge::RVNG_POINT);
    for (size_t c=0; c<row.size(); ++c) {
      if (c>=numFields) break;
      auto field=fields[c];
      database.updateWithContent(field, MWAWVec2i(int(c),int(r)), row[c]);
      if (field.empty()) continue;

      auto const &content=field.m_content;
      listener->openSheetCell(field, content);
      if (content.m_contentType==MWAWCellContent::C_TEXT && content.m_textEntry.valid()) {
        listener->setFont(field.getFont());
        input->seek(content.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
        while (!input->isEnd() && input->tell()<content.m_textEntry.end()) {
          auto ch=static_cast<unsigned char>(input->readULong(1));
          if (ch==0xd)
            listener->insertEOL();
          else if (ch<30) {
            MWAW_DEBUG_MSG(("BeagleWksDBParser::sendDatabase: find some odd character\n"));
            break;
          }
          else
            listener->insertCharacter(ch);
        }
      }
      else if (field.m_type==BeagleWksDBParserInternal::Cell::Picture && field.m_pictureId>=0) {
        std::string cellName("Sheet0.");
        cellName+=MWAWCell::getBasicCellName(field.position()+MWAWVec2i(1,1));
        MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(76,12), librevenge::RVNG_POINT); // changeme
        position.setAnchorToCell(librevenge::RVNGString(cellName.c_str()));
        sendPicture(field.m_pictureId, position, false);
      }
      listener->closeSheetCell();
    }
    listener->closeSheetRow();
  }
  listener->closeSheet();
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
bool BeagleWksDBParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = BeagleWksDBParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(66))
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";

  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (input->readLong(2)!=0x4257 || input->readLong(2)!=0x6b73 ||
      input->readLong(2)!=0x4257 || input->readLong(2)!=0x6462 ||
      input->readLong(2)!=0x4257 || input->readLong(2)!=0x6462) {
    return false;
  }
  for (int i=0; i < 9; ++i) { // f2=f6=1 other 0
    auto val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  setVersion(1);

  if (header)
    header->reset(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_DATABASE);

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "FileHeader-II:";
  m_state->m_databaseBegin=input->readLong(4);
  if (!input->checkPosition(m_state->m_databaseBegin)) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::checkHeader: can not read the database position\n"));
    return false;
  }
  f << "database[ptr]=" << std::hex << m_state->m_databaseBegin << std::dec << ",";
  for (int i=0; i < 11; ++i) { // f2=0x50c|58c|5ac f3=f5=9
    long val=input->readLong(2);
    if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  MWAWEntry entry;
  entry.setBegin(input->readLong(4));
  entry.setLength(input->readLong(4));
  entry.setId(static_cast<int>(input->readLong(2))); // in fact nFonts
  entry.setType("FontNames");
  f << "fontNames[ptr]=" << std::hex << entry.begin() << "<->" << entry.end()
    << std::dec << ",nFonts=" << entry.id() << ",";
  if (entry.length() && (!entry.valid() || !input->checkPosition(entry.end()))) {
    MWAW_DEBUG_MSG(("BeagleWksDBParser::checkHeader: can not read the font names position\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }

  m_state->m_typeEntryMap.insert
  (std::multimap<std::string, MWAWEntry>::value_type(entry.type(),entry));
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (strict && !readPrintInfo())
    return false;
  ascii().addPos(66);
  ascii().addNote("_");

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
