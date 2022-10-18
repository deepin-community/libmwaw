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

/** \file MWAWSpreadsheetListener.hxx
 * Defines MWAWSpreadsheetListener: the libmwaw spreadsheet processor listener
 *
 * \note this class is the only class which does the interface with
 * the librevenge::RVNGSpreadsheetInterface
 */
#ifndef MWAW_SPREADSHEET_LISTENER_H
#define MWAW_SPREADSHEET_LISTENER_H

#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWListener.hxx"

class MWAWCell;
class MWAWCellContent;
class MWAWChart;
class MWAWGraphicStyle;
class MWAWGraphicShape;
class MWAWTable;

namespace MWAWSpreadsheetListenerInternal
{
struct DocumentState;
struct State;
}

/** This class contents the main functions needed to create a spreadsheet processing Document */
class MWAWSpreadsheetListener final : public MWAWListener
{
public:
  /** constructor */
  MWAWSpreadsheetListener(MWAWParserState &parserState, std::vector<MWAWPageSpan> const &pageList, librevenge::RVNGSpreadsheetInterface *documentInterface);
  /** simplified constructor (can be used for a embedded spreadsheet with one page).

   \note the box coordinates must be given in point.*/
  MWAWSpreadsheetListener(MWAWParserState &parserState, MWAWBox2f const &box, librevenge::RVNGSpreadsheetInterface *documentInterface);
  /** destructor */
  ~MWAWSpreadsheetListener() final;

  /** returns the listener type */
  Type getType() const final
  {
    return Spreadsheet;
  }

  /** sets the documents metadata */
  void setDocumentMetaData(librevenge::RVNGPropertyList const &metadata) final;
  /** sets the documents language */
  void setDocumentLanguage(std::string const &locale) final;

  /** starts the document */
  void startDocument() final;
  /** ends the document */
  void endDocument(bool sendDelayedSubDoc=true) final;
  /** returns true if a document is opened */
  bool isDocumentStarted() const final;

  /** function called to add a subdocument */
  void handleSubDocument(MWAWSubDocumentPtr const &subDocument, libmwaw::SubDocumentType subDocumentType) final;
  /** returns try if a subdocument is open  */
  bool isSubDocumentOpened(libmwaw::SubDocumentType &subdocType) const final;
  /** tries to open a frame */
  bool openFrame(MWAWPosition const &pos, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle()) final;
  /** tries to close a frame */
  void closeFrame() final;
  /** open a group (not implemented) */
  bool openGroup(MWAWPosition const &pos) final;
  /** close a group (not implemented) */
  void closeGroup() final;

  /** returns true if we can add text data */
  bool canWriteText() const final;

  // ------ page --------
  /** returns true if a page is opened */
  bool isPageSpanOpened() const final;
  /** returns the current page span

  \note this forces the opening of a new page if no page is opened.*/
  MWAWPageSpan const &getPageSpan() final;

  // ------ header/footer --------
  /** insert a header */
  bool insertHeader(MWAWSubDocumentPtr const &subDocument, librevenge::RVNGPropertyList const &extras) final;
  /** insert a footer */
  bool insertFooter(MWAWSubDocumentPtr const &subDocument, librevenge::RVNGPropertyList const &extras) final;
  /** returns true if the header/footer is open */
  bool isHeaderFooterOpened() const final;

  // ------- sheet -----------------
  /** opens a sheet

      \note if defWidth is positive, add 1000 columns with size defWidth
   */
  void openSheet(std::vector<float> const &colWidth, librevenge::RVNGUnit unit,
                 std::vector<int> const &repeatColWidthNumber=std::vector<int>(), std::string const &name="");
  /** closes this sheet */
  void closeSheet();
  /** open a row with given height ( if h < 0.0, set min-row-height = -h )*/
  void openSheetRow(float h, librevenge::RVNGUnit unit, int numRepeated=1);
  /** closes this row */
  void closeSheetRow();
  /** open a cell */
  void openSheetCell(MWAWCell const &cell, MWAWCellContent const &content, int numRepeated=1);
  /** close a cell */
  void closeSheetCell();

  // ------- chart -----------------
  /** adds a chart in given position */
  void insertChart(MWAWPosition const &pos, MWAWChart &chart,
                   MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());

  // ------ text data -----------

  //! adds a basic character, ..
  void insertChar(uint8_t character) final;
  /** insert a character using the font converter to find the utf8
      character */
  void insertCharacter(unsigned char c) final;
  /** insert a character using the font converter to find the utf8
      character and if needed, input to read extra character.

      \return the number of extra character read
   */
  int insertCharacter(unsigned char c, MWAWInputStreamPtr &input, long endPos=-1) final;
  /** adds an unicode character.
   *  By convention if \a character=0xfffd(undef), no character is added */
  void insertUnicode(uint32_t character) final;
  //! adds a unicode string
  void insertUnicodeString(librevenge::RVNGString const &str) final;

  //! adds a tab
  void insertTab() final;
  //! adds an end of line ( by default an hard one)
  void insertEOL(bool softBreak=false) final;

  // ------ text format -----------
  //! sets the font
  void setFont(MWAWFont const &font) final;
  //! returns the actual font
  MWAWFont const &getFont() const final;

  // ------ paragraph format -----------
  //! returns true if a paragraph or a list is opened
  bool isParagraphOpened() const final;
  //! sets the paragraph
  void setParagraph(MWAWParagraph const &paragraph) final;
  //! returns the actual paragraph
  MWAWParagraph const &getParagraph() const final;

  // ------- fields ----------------
  //! adds a field type
  void insertField(MWAWField const &field) final;

  // ------- link ----------------
  //! open a link
  void openLink(MWAWLink const &link) final;
  //! close a link
  void closeLink() final;

  // ------- subdocument -----------------
  /** insert a note */
  void insertNote(MWAWNote const &note, MWAWSubDocumentPtr &subDocument) final;

  /** adds comment */
  void insertComment(MWAWSubDocumentPtr &subDocument) final;

  /** adds a picture with potential various representationin given position */
  void insertPicture(MWAWPosition const &pos, MWAWEmbeddedObject const &picture,
                     MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle()) final;
  /** adds a shape picture in given position */
  void insertShape(MWAWPosition const &pos, MWAWGraphicShape const &shape,
                   MWAWGraphicStyle const &style) final;
  /** adds a textbox in given position */
  void insertTextBox(MWAWPosition const &pos, MWAWSubDocumentPtr const &subDocument,
                     MWAWGraphicStyle const &frameStyle=MWAWGraphicStyle::emptyStyle()) final;

  // ------- table -----------------
  /** adds a table in given position */
  void insertTable(MWAWPosition const &pos, MWAWTable &table, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());
  /** open a table */
  void openTable(MWAWTable const &table) final;
  /** closes this table */
  void closeTable() final;
  /** open a row with given height ( if h < 0.0, set min-row-height = -h )*/
  void openTableRow(float h, librevenge::RVNGUnit unit, bool headerRow=false) final;
  /** closes this row */
  void closeTableRow() final;
  /** open a cell */
  void openTableCell(MWAWCell const &cell) final;
  /** close a cell */
  void closeTableCell() final;
  /** add empty cell */
  void addEmptyTableCell(MWAWVec2i const &pos, MWAWVec2i span=MWAWVec2i(1,1)) final;

  // ------- section ---------------
  /** returns true if we can add open a section, add page break, ... */
  bool canOpenSectionAddBreak() const final
  {
    return false;
  }
  //! returns true if a section is opened
  bool isSectionOpened() const final
  {
    return false;
  }
  //! returns the actual section
  MWAWSection const &getSection() const final;
  //! open a section if possible
  bool openSection(MWAWSection const &section) final;
  //! close a section
  bool closeSection() final;
  //! inserts a break type: ColumBreak, PageBreak, ..
  void insertBreak(BreakType breakType) final;

protected:
  //! does open a new page (low level)
  void _openPageSpan(bool sendHeaderFooters=true);
  //! does close a page (low level)
  void _closePageSpan();

  void _startSubDocument();
  void _endSubDocument();

  void _handleFrameParameters(librevenge::RVNGPropertyList &propList, MWAWPosition const &pos);

  void _openParagraph();
  void _closeParagraph();
  void _resetParagraphState(const bool isListElement=false);

  /** open a list level */
  void _openListElement();
  /** close a list level */
  void _closeListElement();
  /** update the list so that it corresponds to the actual level */
  void _changeList();
  /** low level: find a list id which corresponds to actual list and a change of level.

  \note called when the list id is not set
  */
  int _getListId() const;

  void _openSpan();
  void _closeSpan();

  void _flushText();
  void _flushDeferredTabs();

  /** creates a new parsing state (copy of the actual state)
   *
   * \return the old one */
  std::shared_ptr<MWAWSpreadsheetListenerInternal::State> _pushParsingState();
  //! resets the previous parsing state
  void _popParsingState();

protected:
  //! the main parse state
  std::shared_ptr<MWAWSpreadsheetListenerInternal::DocumentState> m_ds;
  //! the actual local parse state
  std::shared_ptr<MWAWSpreadsheetListenerInternal::State> m_ps;
  //! stack of local state
  std::vector<std::shared_ptr<MWAWSpreadsheetListenerInternal::State> > m_psStack;
  //! the parser state
  MWAWParserState &m_parserState;
  //! the document interface
  librevenge::RVNGSpreadsheetInterface *m_documentInterface;

private:
  //! copy constructor (unimplemented)
  MWAWSpreadsheetListener(const MWAWSpreadsheetListener &);
  //! operator= (unimplemented)
  MWAWSpreadsheetListener &operator=(const MWAWSpreadsheetListener &);
};

#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
