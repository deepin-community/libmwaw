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
 * Parser to Claris Works text document ( spreadsheet part )
 *
 */
#ifndef CLARIS_WKS_SPREADSHEET
#  define CLARIS_WKS_SPREADSHEET

#include <list>
#include <string>
#include <vector>

#include "MWAWEntry.hxx"
#include "MWAWSubDocument.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

#include "ClarisWksStruct.hxx"

namespace ClarisWksSpreadsheetInternal
{
struct Spreadsheet;
struct Field;
struct State;
}

class ClarisWksDocument;
class ClarisWksParser;

/** \brief the main class to read the text part of Claris Works file
 *
 *
 *
 */
class ClarisWksSpreadsheet
{
  friend class ClarisWksParser;

public:
  //! constructor
  explicit ClarisWksSpreadsheet(ClarisWksDocument &document);
  //! destructor
  virtual ~ClarisWksSpreadsheet();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

  //! reads the zone Text DSET
  std::shared_ptr<ClarisWksStruct::DSET> readSpreadsheetZone
  (ClarisWksStruct::DSET const &zone, MWAWEntry const &entry, bool &complete);
  //! check if we can send a spreadsheet as graphic
  bool canSendSpreadsheetAsGraphic(int) const
  {
    return false;
  }
  //! sends the zone data to the listener (if it exists )
  bool sendSpreadsheet(int number, MWAWListenerPtr listener);
  //! sends the zone data to the listener (if it exists )
  bool sendSpreadsheetAsTable(int number, MWAWListenerPtr listener);

protected:
  //
  // Intermediate level
  //

  /** try to read the first spreadsheet zone */
  bool readZone1(ClarisWksSpreadsheetInternal::Spreadsheet &sheet);
  /** try to read the row height zone */
  bool readRowHeightZone(ClarisWksSpreadsheetInternal::Spreadsheet &sheet);

  //
  // low level
  //

private:
  ClarisWksSpreadsheet(ClarisWksSpreadsheet const &orig) = delete;
  ClarisWksSpreadsheet &operator=(ClarisWksSpreadsheet const &orig) = delete;

protected:
  //
  // data
  //

  //! the document
  ClarisWksDocument &m_document;

  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<ClarisWksSpreadsheetInternal::State> m_state;

  //! the main parser;
  MWAWParser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
