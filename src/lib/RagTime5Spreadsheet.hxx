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

/*
 * Parser to RagTime 5-6 document ( spreadsheet part )
 *
 */
#ifndef RAGTIME5_SPREADSHEET
#  define RAGTIME5_SPREADSHEET

#include <string>
#include <map>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWCell.hxx"
#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

#include "RagTime5StructManager.hxx"
#include "RagTime5ClusterManager.hxx"

namespace RagTime5SpreadsheetInternal
{
struct CellContent;
struct Sheet;
struct State;

class SubDocument;
}

class RagTime5Document;
class RagTime5Formula;
class RagTime5StructManager;
class RagTime5StyleManager;
class RagTime5Zone;

/** \brief the main class to read the spreadsheet part of RagTime 56 file
 *
 *
 *
 */
class RagTime5Spreadsheet
{
  friend class RagTime5SpreadsheetInternal::SubDocument;
  friend class RagTime5Document;
  friend class RagTime5Formula;

public:
  //! constructor
  explicit RagTime5Spreadsheet(RagTime5Document &doc);
  //! destructor
  virtual ~RagTime5Spreadsheet();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

  //! returns a formula instruction corresponding to a sheetId and refId
  bool getFormulaRef(int sheetId, int refId, MWAWCellContent::FormulaInstruction &instruction) const;

protected:

  //
  // send data
  //

  //! try to send the cluster zone
  bool send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId=0);
  //! try to send the cluster zone
  bool send(RagTime5SpreadsheetInternal::Sheet &sheet,
            MWAWListenerPtr listener, MWAWPosition const &pos, int partId=0);
  //! try to send a cell
  bool send(RagTime5SpreadsheetInternal::Sheet &sheet, int plane,
            RagTime5SpreadsheetInternal::CellContent const &cell, int numRepeated,
            MWAWSpreadsheetListenerPtr &listener);
  //! return the sheet data id list
  std::vector<int> getSheetIdList() const;
  //! sends the data which have not yet been sent to the listener
  void flushExtra(bool onlyCheck=false);

  // interface with main parser

  //! try to read a spreadsheet cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> readSpreadsheetCluster(RagTime5Zone &zone, int zoneType);
  //! parses all the formula structure linked to a spreadsheet
  void parseSpreadsheetFormulas();
  //! store the formula corresponding to a sheetId
  void storeFormula(int sheetId, std::map<int, std::vector<MWAWCellContent::FormulaInstruction> > const &idToFormula);

  //
  // Intermediate level
  //

  //! try to read the spreadsheet dimensions
  bool readSheetDimensions(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link);
  //! try to read a spreadsheet tree of values
  bool readValuesTree(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link, int rootId, MWAWVec3i const &maxPos);
  //! try to read a spreadsheet referenced/union zones
  bool readCellBlocks(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link, bool unionBlock);
  //! try to read the cell ref to value zone
  bool readCellRefPos(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link);

  //
  // basic
  //

  //
  // low level
  //

public:
  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }

private:
  RagTime5Spreadsheet(RagTime5Spreadsheet const &orig) = delete;
  RagTime5Spreadsheet &operator=(RagTime5Spreadsheet const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser
  RagTime5Document &m_document;

  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
  //! the style manager
  std::shared_ptr<RagTime5StyleManager> m_styleManager;
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<RagTime5SpreadsheetInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
