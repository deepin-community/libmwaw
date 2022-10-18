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
 * Parser to convert spreadsheet Multiplan document
 *
 */
#ifndef MULTIPLAN_PARSER
#  define MULTIPLAN_PARSER

#include <vector>

#include "MWAWCell.hxx"
#include "MWAWDebug.hxx"

#include "MWAWParser.hxx"

namespace MultiplanParserInternal
{
struct State;

class SubDocument;
}

/** \brief the main class to read a Multiplan document
 *
 * \note need more files to be sure to treat all documents
 *
 */
class MultiplanParser final : public MWAWSpreadsheetParser
{
  friend class MultiplanParserInternal::SubDocument;
public:
  //! constructor
  MultiplanParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~MultiplanParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  //! the main parse function
  void parse(librevenge::RVNGSpreadsheetInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGSpreadsheetInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! read the columns limits pos
  bool readColumnsPos();
  //! read the header/footer strings, ...
  bool readHeaderFooter();
  //! read the printer information
  bool readPrinterInfo();
  //! read the printer message
  bool readPrinterMessage();

  //! read the spreadsheet zone list
  bool readZonesList();
  //! read an unknown zone: two blocks of 60 bytes
  bool readZone1(MWAWEntry const &entry);
  //! read the cell data position
  bool readCellDataPosition(MWAWEntry const &entry);
  //! read a link given a position
  bool readLink(int pos, MWAWCellContent::FormulaInstruction &instruction);
  //! read a link filename
  bool readLinkFilename(int pos, MWAWCellContent::FormulaInstruction &instruction);
  //! read a shared data
  bool readSharedData(int pos, int cellType, MWAWVec2i const &cellPos, MWAWCellContent &content);
  //! reads a name and returns the cell's instruction
  bool readName(int pos, MWAWCellContent::FormulaInstruction &instruction);
  //! read an unknown zone
  bool readZoneB();
  //! read an unknown zone
  bool readZoneC();

  //! try to send the main spreadsheet
  bool sendSpreadsheet();
  //! try to send a text zone
  bool sendText(MWAWEntry const &entry);
  //! try to send a cell
  bool sendCell(MWAWVec2i const &cellPos, int pos);
protected:
  //! try to read a double value
  bool readDouble(double &value);
  //! try to read a formula
  bool readFormula(MWAWVec2i const &cellPos, std::vector<MWAWCellContent::FormulaInstruction> &formula, long endPos, std::string &extra);

  //
  // data
  //

  //! the state
  std::shared_ptr<MultiplanParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
