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
 * Parser to convert spreadsheet Jazz document and some database
 *
 */
#ifndef JAZZ_SS_PARSER
#  define JAZZ_SS_PARSER

#include <string>
#include <vector>

#include "MWAWCell.hxx"
#include "MWAWDebug.hxx"

#include "MWAWParser.hxx"

namespace JazzSSParserInternal
{
struct State;

class SubDocument;
}

/** \brief the main class to read a Jazz spreadsheet v1 (Lotus) document and some databases.
 *
 * \note need more files to be sure to treat all documents
 *
 * \note a database is stored as a spreadsheet in the form
 *       [A][B]
 *       [C][empty]
 * where [B] corresponds to the report's definitions and
 *       [C] corresponds to the database's contents
 */
class JazzSSParser final : public MWAWSpreadsheetParser
{
  friend class JazzSSParserInternal::SubDocument;
public:
  //! constructor
  JazzSSParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~JazzSSParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  //! the main parse function
  void parse(librevenge::RVNGSpreadsheetInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGSpreadsheetInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! sends the spreadsheet
  bool sendSpreadsheet();

  //
  // low level
  //
  //////////////////////// spread sheet //////////////////////////////

  //! reads a query/name/range... data (zone 11)
  bool readZone11(long endPos);
  //! reads a cell content data
  bool readCell(int id, long endPos);
  //! reads sheet size
  bool readSheetSize(long endPos);
  //! read the basic document zones (zone 15): selection, columns' width, default font, preferences...
  bool readDocument(long endPos);

  /* reads a cell */
  bool readCell(MWAWVec2i actPos, MWAWCellContent::FormulaInstruction &instr);
  /* reads a formula */
  bool readFormula(long endPos, MWAWVec2i const &pos,
                   std::vector<MWAWCellContent::FormulaInstruction> &formula,
                   std::string &error);

protected:

  //
  // data
  //

  //! the state
  std::shared_ptr<JazzSSParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
