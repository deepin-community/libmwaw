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
#include <map>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWListener.hxx"
#include "MWAWParser.hxx"
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWTable.hxx"

#include "ClarisWksDbaseContent.hxx"
#include "ClarisWksDocument.hxx"
#include "ClarisWksStruct.hxx"
#include "ClarisWksStyleManager.hxx"

#include "ClarisWksSpreadsheet.hxx"

/** Internal: the structures of a ClarisWksSpreadsheet */
namespace ClarisWksSpreadsheetInternal
{
//! Internal the spreadsheet
struct Spreadsheet final : public ClarisWksStruct::DSET {
  // constructor
  explicit Spreadsheet(ClarisWksStruct::DSET const &dset = ClarisWksStruct::DSET()) :
    ClarisWksStruct::DSET(dset), m_colWidths(), m_rowHeightMap(), m_content()
  {
  }
  //! destructor
  ~Spreadsheet() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Spreadsheet const &doc)
  {
    o << static_cast<ClarisWksStruct::DSET const &>(doc);
    return o;
  }
  //! returns the row size in point
  float getRowHeight(int row) const
  {
    if (m_rowHeightMap.find(row)!=m_rowHeightMap.end())
      return float(m_rowHeightMap.find(row)->second);
    return 14;
  }
  //! returns the height of a row in point and updated repeated row
  float getRowHeight(int row, int &numRepeated) const
  {
    int res=14;
    if (m_rowHeightMap.empty()) {
      numRepeated=1000;
      return float(res);
    }
    auto it=m_rowHeightMap.lower_bound(row);
    if (it==m_rowHeightMap.end()) {
      numRepeated=1000;
      return float(res);
    }
    numRepeated=1;
    if (it->first==row)
      res=it++->second;
    int lastRow=row;
    while (it!=m_rowHeightMap.end()) {
      int nRow=it->first;
      int nextH=it++->second;

      if (nRow!=lastRow+1) {
        if (res!=14)
          break;
        else
          numRepeated+=(nRow-(lastRow+1));
      }
      if (nRow==row)
        continue;
      numRepeated=(nRow-row);
      if (nextH!=res)
        break;
      ++numRepeated;
      lastRow=nRow;
    }
    return float(res);
  }

  //! the columns width
  std::vector<int> m_colWidths;
  //! a map row to height
  std::map<int, int> m_rowHeightMap;
  //! the data
  std::shared_ptr<ClarisWksDbaseContent> m_content;
};

Spreadsheet::~Spreadsheet()
{
}

//! Internal: the state of a ClarisWksSpreadsheet
struct State {
  //! constructor
  State()
    : m_spreadsheetMap()
  {
  }
  //! a map zoneId to spreadsheet
  std::map<int, std::shared_ptr<Spreadsheet> > m_spreadsheetMap;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
ClarisWksSpreadsheet::ClarisWksSpreadsheet(ClarisWksDocument &document)
  : m_document(document)
  , m_parserState(document.m_parserState)
  , m_state(new ClarisWksSpreadsheetInternal::State)
  , m_mainParser(&document.getMainParser())
{
}

ClarisWksSpreadsheet::~ClarisWksSpreadsheet()
{ }

int ClarisWksSpreadsheet::version() const
{
  return m_parserState->m_version;
}

// fixme
int ClarisWksSpreadsheet::numPages() const
{
  return 1;
}
////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// a document part
////////////////////////////////////////////////////////////
std::shared_ptr<ClarisWksStruct::DSET> ClarisWksSpreadsheet::readSpreadsheetZone
(ClarisWksStruct::DSET const &zone, MWAWEntry const &entry, bool &complete)
{
  complete = false;
  if (!entry.valid() || zone.m_fileType != 2 || entry.length() < 256)
    return std::shared_ptr<ClarisWksStruct::DSET>();
  long pos = entry.begin();
  MWAWInputStreamPtr &input= m_parserState->m_input;
  input->seek(pos+8+16, librevenge::RVNG_SEEK_SET); // avoid header+8 generic number
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  std::shared_ptr<ClarisWksSpreadsheetInternal::Spreadsheet>
  sheet(new ClarisWksSpreadsheetInternal::Spreadsheet(zone));

  f << "Entries(SpreadsheetDef):" << *sheet << ",";
  ascFile.addDelimiter(input->tell(), '|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  // read the last part
  long data0Length = zone.m_dataSz;
  long N = zone.m_numData;
  if (entry.length() -8-12 != data0Length*N + zone.m_headerSz) {
    if (data0Length == 0 && N) {
      MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: can not find definition size\n"));
      input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
      return std::shared_ptr<ClarisWksStruct::DSET>();
    }

    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: unexpected size for zone definition, try to continue\n"));
  }
  int debColSize = 0;
  int vers = version();
  switch (vers) {
  case 1:
    debColSize = 72;
    break;
  case 2:
  case 3: // checkme...
  case 4:
  case 5:
    debColSize = 76;
    break;
  case 6:
    debColSize = 72;
    break;
  default:
    break;
  }

  sheet->m_colWidths.resize(0);
  sheet->m_colWidths.resize(256,36);
  if (debColSize) {
    pos = entry.begin()+debColSize;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Entries(SpreadsheetCol):width,";
    for (size_t i = 0; i < 256; ++i) {
      auto w=static_cast<int>(input->readULong(1));
      sheet->m_colWidths[i]=w;
      if (w!=36) // default
        f << "w" << i << "=" << w << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    ascFile.addPos(input->tell());
    ascFile.addNote("SpreadsheetDef-A");
  }

  long dataEnd = entry.end()-N*data0Length;
  int numLast = version()==6 ? 4 : 0;
  if (long(input->tell()) + data0Length + numLast <= dataEnd) {
    ascFile.addPos(dataEnd-data0Length-numLast);
    ascFile.addNote("SpreadsheetDef-_");
    if (numLast) {
      ascFile.addPos(dataEnd-numLast);
      ascFile.addNote("SpreadsheetDef-extra");
    }
  }
  input->seek(dataEnd, librevenge::RVNG_SEEK_SET);

  for (long i = 0; i < N; i++) {
    pos = input->tell();

    f.str("");
    f << "SpreadsheetDef-" << i;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+data0Length, librevenge::RVNG_SEEK_SET);
  }

  input->seek(entry.end(), librevenge::RVNG_SEEK_SET);

  if (m_state->m_spreadsheetMap.find(sheet->m_id) != m_state->m_spreadsheetMap.end()) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: zone %d already exists!!!\n", sheet->m_id));
  }
  else
    m_state->m_spreadsheetMap[sheet->m_id] = sheet;

  sheet->m_otherChilds.push_back(sheet->m_id+1);
  pos = input->tell();

  bool ok = readZone1(*sheet);
  if (ok) {
    pos = input->tell();
    ok = ClarisWksStruct::readStructZone(*m_parserState, "SpreadsheetZone2", false);
  }
  if (ok) {
    pos = input->tell();
    std::shared_ptr<ClarisWksDbaseContent> content(new ClarisWksDbaseContent(m_document, true));
    ok = content->readContent();
    if (ok) sheet->m_content=content;
  }
  if (ok) {
    pos = input->tell();
    if (!readRowHeightZone(*sheet)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      ok = ClarisWksStruct::readStructZone(*m_parserState, "SpreadsheetRowHeight", false);
    }
  }
  if (ok && vers <= 2) { // field with size 0xa in v2
    pos = input->tell();
    ok = ClarisWksStruct::readStructZone(*m_parserState, "SpreadsheetUnkn1", false);
  }
  /* checkme: now a sequence of 5/6 lists: when filed the first two zones are a list of cell,
   while the last 2 lists contains only 4 numbers */
  while (ok) {
    pos=input->tell();
    auto sz=long(input->readULong(4));
    if (!input->checkPosition(pos+4+sz)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    // empty or list of 2*uint16_t ?
    if (!sz) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(SpreadsheetListCell):_");
      continue;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    std::vector<MWAWVec2i> res;
    ok = m_document.readStructCellZone("SpreadsheetListCell", false, res);
    if (ok) continue;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    ok = ClarisWksStruct::readStructZone(*m_parserState, "SpreadsheetUnkn2", false);
    if (ok) {
      MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: find unexpected Unkn2 zone\n"));
    }
  }
  if (ok) {
    pos=input->tell();
    auto sz=long(input->readULong(4));
    if (input->checkPosition(pos+4+sz)) {
      input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: find some extra block\n"));
      ascFile.addNote("Entries(SpreadsheetEnd):###");
    }
    else
      input->seek(pos, librevenge::RVNG_SEEK_SET);
  }

  if (!ok) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readSpreadsheetZone: find a bad block\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Entries(SpreadsheetEnd):###");
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  }
  return sheet;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool ClarisWksSpreadsheet::readZone1(ClarisWksSpreadsheetInternal::Spreadsheet &/*sheet*/)
{
  MWAWInputStreamPtr &input= m_parserState->m_input;
  long pos = input->tell();
  auto sz = long(input->readULong(4));
  long endPos = pos+4+sz;
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != endPos) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readZone1: spreadsheet\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  if (sz == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("Nop");
    return true;
  }
  int fSize = 0;
  switch (version()) {
  case 4:
  case 5:
    fSize = 4;
    break;
  case 6:
    fSize = 6;
    break;
  default:
    break;
  }
  if (!fSize) {
    ascFile.addPos(pos);
    ascFile.addNote("Entries(SpreadsheetZone1)");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  long numElts = sz/fSize;
  if (numElts *fSize != sz) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readZone1: unexpected size\n"));
    return false;
  }

  ascFile.addPos(pos);
  ascFile.addNote("Entries(SpreadsheetZone1)");

  libmwaw::DebugStream f;
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  for (long i = 0; i < numElts; i++) {
    pos = input->tell();

    f.str("");
    f << "SpreadsheetZone1-" << i << ":";
    f << "row?=" << input->readLong(2) << ",";
    f << "col?=" << input->readLong(2) << ",";
    if (fSize == 6) {
      auto val = static_cast<int>(input->readLong(2));
      if (val != -1)
        f << "#unkn=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+fSize, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool ClarisWksSpreadsheet::readRowHeightZone(ClarisWksSpreadsheetInternal::Spreadsheet &sheet)
{
  MWAWInputStreamPtr &input= m_parserState->m_input;
  long pos = input->tell();
  ClarisWksStruct::Struct header;
  if (!header.readHeader(input,false)) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readRowHeightZone: can not read the header\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  if (header.m_size==0) {
    ascFile.addPos(pos);
    ascFile.addNote("Nop");
    return true;
  }
  long endPos=pos+4+header.m_size;
  f << "Entries(SpreadsheetRowHeight):" << header;
  if (header.m_dataSize!=4) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::readRowHeightZone: unexpected size for fieldSize\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  if (header.m_headerSize) {
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(header.m_headerSize, librevenge::RVNG_SEEK_CUR);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (long i = 0; i < header.m_numData; i++) {
    pos = input->tell();

    f.str("");
    f << "SpreadsheetRowHeightZone-" << i << ":";
    auto row=static_cast<int>(input->readLong(2));
    auto h=static_cast<int>(input->readLong(2));
    sheet.m_rowHeightMap[row]=h;
    f << "row=" << row << ", height=" << h << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool ClarisWksSpreadsheet::sendSpreadsheet(int zId, MWAWListenerPtr listener)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheet: called without any listener\n"));
    return false;
  }
  if (listener->getType()!=MWAWListener::Spreadsheet ||
      (m_parserState->m_kind==MWAWDocument::MWAW_K_SPREADSHEET && zId!=1))
    return sendSpreadsheetAsTable(zId, listener);

  auto *sheetListener=static_cast<MWAWSpreadsheetListener *>(listener.get());
  auto it=m_state->m_spreadsheetMap.find(zId);
  if (it == m_state->m_spreadsheetMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheet: can not find zone %d!!!\n", zId));
    return false;
  }
  auto &sheet=*it->second;
  MWAWVec2i minData, maxData;
  if (!sheet.m_content || !sheet.m_content->getExtrema(minData,maxData)) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheet: can not find content\n"));
    return false;
  }
  if (m_parserState->m_kind==MWAWDocument::MWAW_K_SPREADSHEET && zId==1)
    minData=MWAWVec2i(0,0);
  std::vector<float> colSize(size_t(maxData[0]-minData[0]+1),72);
  for (int c=minData[0], fC=0; c <= maxData[0]; ++c, ++fC) {
    if (c>=0 && c < int(sheet.m_colWidths.size()))
      colSize[size_t(fC)]=2.0f*float(sheet.m_colWidths[size_t(c)]);
  }
  sheetListener->openSheet(colSize, librevenge::RVNG_POINT);
  MWAWInputStreamPtr &input= m_parserState->m_input;
  std::vector<int> rowsPos, colsPos;
  if (!sheet.m_content->getRecordList(rowsPos)) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheet: can not find the record position\n"));
    sheetListener->closeSheet();
    return true;
  }
  bool recomputeCellPosition=(minData!=MWAWVec2i(0,0));
  int prevRow = minData[1]-1;
  for (int r : rowsPos) {
    int fR=r-minData[1];
    if (r>prevRow+1) {
      while (r > prevRow+1) {
        int numRepeat;
        auto h=float(sheet.getRowHeight(prevRow+1, numRepeat));
        if (r<prevRow+1+numRepeat)
          numRepeat=r-1-prevRow;
        sheetListener->openSheetRow(h, librevenge::RVNG_POINT, numRepeat);
        sheetListener->closeSheetRow();
        prevRow+=numRepeat;
      }
    }
    sheetListener->openSheetRow(float(sheet.getRowHeight(r)), librevenge::RVNG_POINT);
    prevRow=r;
    colsPos.clear();
    if (!sheet.m_content->getColumnList(r, colsPos)) {
      MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheet: can not find the columns for row=%d\n", r));
      sheetListener->closeSheetRow();
      continue;
    }
    for (auto c : colsPos) {
      ClarisWksDbaseContent::Record rec;
      if (!sheet.m_content->get(MWAWVec2i(c,r),rec)) continue;
      MWAWCell cell;
      cell.setPosition(MWAWVec2i(c-minData[0],fR));
      cell.setFormat(rec.m_format);
      cell.setHAlignment(rec.m_hAlign);
      cell.setFont(rec.m_font);
      if (recomputeCellPosition)
        rec.updateFormulaCells(minData);
      // change the reference date from 1/1/1904 to 1/1/1900
      if (rec.m_format.m_format==MWAWCell::F_DATE && rec.m_content.isValueSet())
        rec.m_content.setValue(rec.m_content.m_value+1460);
      if (rec.m_borders) {
        int wh=0;
        for (int b=0, bit=1; b < 4; ++b, bit*=2) {
          if ((rec.m_borders&bit)==0) continue;
          static int const what[] = {libmwaw::LeftBit, libmwaw::TopBit, libmwaw::RightBit, libmwaw::BottomBit};
          wh |= what[b];
        }
        cell.setBorders(wh, MWAWBorder());
      }
      if (!rec.m_backgroundColor.isWhite())
        cell.setBackgroundColor(rec.m_backgroundColor);
      sheetListener->openSheetCell(cell, rec.m_content);
      if (rec.m_content.m_textEntry.valid()) {
        long fPos = input->tell();
        input->seek(rec.m_content.m_textEntry.begin(), librevenge::RVNG_SEEK_SET);
        long endPos = rec.m_content.m_textEntry.end();
        sheetListener->setFont(rec.m_font);
        while (!input->isEnd() && input->tell() < endPos) {
          auto ch=static_cast<unsigned char>(input->readULong(1));
          if (ch==0xd || ch==0xa)
            sheetListener->insertEOL();
          else
            sheetListener->insertCharacter(ch, input, endPos);
        }
        input->seek(fPos,librevenge::RVNG_SEEK_SET);
      }
      sheetListener->closeSheetCell();
    }
    sheetListener->closeSheetRow();
  }
  sheetListener->closeSheet();
  return true;
}

bool ClarisWksSpreadsheet::sendSpreadsheetAsTable(int zId, MWAWListenerPtr listener)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheetAsTable: called without any listener\n"));
    return false;
  }
  auto it=m_state->m_spreadsheetMap.find(zId);
  if (it == m_state->m_spreadsheetMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheetAsTable: can not find zone %d!!!\n", zId));
    return false;
  }
  auto &sheet=*it->second;
  MWAWVec2i minData, maxData;
  if (!sheet.m_content || !sheet.m_content->getExtrema(minData,maxData)) {
    MWAW_DEBUG_MSG(("ClarisWksSpreadsheet::sendSpreadsheetAsTable: can not find content\n"));
    return false;
  }
  std::vector<float> colSize(size_t(maxData[0]-minData[0]+1),72);
  for (int c=minData[0], fC=0; c <= maxData[0]; ++c, ++fC) {
    if (c>=0 && c < int(sheet.m_colWidths.size()))
      colSize[size_t(fC)]=2.0f*float(sheet.m_colWidths[size_t(c)]);
  }
  MWAWTable table(MWAWTable::TableDimBit);
  table.setColsSize(colSize);
  listener->openTable(table);
  for (int r=minData[1], fR=0; r <= maxData[1]; ++r, ++fR) {
    if (sheet.m_rowHeightMap.find(r)!=sheet.m_rowHeightMap.end())
      listener->openTableRow(float(sheet.m_rowHeightMap.find(r)->second), librevenge::RVNG_POINT);
    else
      listener->openTableRow(14.f, librevenge::RVNG_POINT);
    for (int c=minData[0], fC=0; c <= maxData[0]; ++c, ++fC) {
      MWAWCell cell;
      cell.setPosition(MWAWVec2i(fC,fR));
      cell.setVAlignment(MWAWCell::VALIGN_BOTTOM); // always ?
      listener->openTableCell(cell);
      sheet.m_content->send(MWAWVec2i(c, r));
      listener->closeTableCell();
    }
    listener->closeTableRow();
  }
  listener->closeTable();
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
