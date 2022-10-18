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

#ifndef MWAW_PRESENTATION_LISTENER_H
#define MWAW_PRESENTATION_LISTENER_H

#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWGraphicStyle.hxx"

#include "MWAWListener.hxx"

class MWAWGraphicShape;

namespace MWAWPresentationListenerInternal
{
struct GraphicState;
struct State;
}

/** This class contains code needed to write a presention document.

    \note All units are specified in librevenge::RVNG_POINT
 */
class MWAWPresentationListener final : public MWAWListener
{
public:
  /** constructor */
  MWAWPresentationListener(MWAWParserState &parserState, std::vector<MWAWPageSpan> const &pageList, librevenge::RVNGPresentationInterface *documentInterface);
  /** destructor */
  ~MWAWPresentationListener() final;

  /** returns the listener type */
  Type getType() const final
  {
    return Presentation;
  }

  /** sets the documents metadata */
  void setDocumentMetaData(librevenge::RVNGPropertyList const &metadata) final;
  /** sets the documents language */
  void setDocumentLanguage(std::string const &locale) final;
  /** starts a new document */
  void startDocument() final;
  /** ends the actual document */
  void endDocument(bool delayed=true) final;

  // ------ general information --------
  /** returns true if a text zone is opened */
  bool canWriteText() const final;
  /** returns true if a document is opened */
  bool isDocumentStarted() const final;

  /** function called to add a subdocument and modify the origin*/
  void handleSubDocument(MWAWVec2f const &orig, MWAWSubDocumentPtr const &subDocument, libmwaw::SubDocumentType subDocumentType);
  /** function called to add a subdocument */
  void handleSubDocument(MWAWSubDocumentPtr const &subDocument, libmwaw::SubDocumentType subDocumentType) final
  {
    handleSubDocument(MWAWVec2f(0,0), subDocument, subDocumentType);
  }
  /** returns try if a subdocument is open  */
  bool isSubDocumentOpened(libmwaw::SubDocumentType &subdocType) const final;
  /** store the position and the style (which will be needed further to insert a textbox or a table with openTable) */
  bool openFrame(MWAWPosition const &pos, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle()) final;
  /** close a frame */
  void closeFrame() final;
  /** open a group */
  bool openGroup(MWAWPosition const &pos) final;
  /** close a group */
  void closeGroup() final;
  /** open a layer */
  bool openLayer(librevenge::RVNGString const &name);
  /** close a layer */
  void closeLayer();

  // ------ page --------
  /** opens a master page */
  bool openMasterPage(MWAWPageSpan &masterPage);
  /** close a master page */
  void closeMasterPage()
  {
    _closePageSpan(true);
  }
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
  /** adds a picture with potential various representationin given position */
  void insertPicture(MWAWPosition const &pos, MWAWEmbeddedObject const &picture,
                     MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle()) final;
  /** adds a shape picture in given position */
  void insertShape(MWAWPosition const &pos, MWAWGraphicShape const &shape, MWAWGraphicStyle const &style) final;
  /** adds a textbox in given position */
  void insertTextBox(MWAWPosition const &pos, MWAWSubDocumentPtr const &subDocument, MWAWGraphicStyle const &style) final;
  /** adds a group: ie. next insertion will be done relative to this bdbox[0] position */
  void insertGroup(MWAWBox2f const &bdbox, MWAWSubDocumentPtr const &subDocument);
  /** insert a slide note */
  void insertSlideNote(MWAWPosition const &pos, MWAWSubDocumentPtr &subDocument);
  /** insert a note

   \note as RVNGPresentationInterface does not accept note, note can only be inserted in a text zone (and are inserted between --) */
  void insertNote(MWAWNote const &note, MWAWSubDocumentPtr &subDocument) final;
  /** adds comment

   \note as RVNGPresentationInterface does not accept comment, comment can only be inserted in a text zone (and are inserted between --) */
  void insertComment(MWAWSubDocumentPtr &subDocument) final;

  // ------- table -----------------

  /** adds a table in given position */
  void insertTable(MWAWPosition const &pos, MWAWTable &table, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());
  /** open a table (using the last parameters of openFrame for the position ) */
  void openTable(MWAWTable const &table) final;
  /** open a table in a given position */
  void openTable(MWAWPosition const &pos, MWAWTable const &table, MWAWGraphicStyle const &style);
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
  bool closeSection() final
  {
    return false;
  }
  //! inserts a break type: ColumBreak, PageBreak, ..
  void insertBreak(BreakType breakType) final;

protected:
  //! does open a new page (low level)
  void _openPageSpan(bool sendHeaderFooters=true);
  //! does close a page (low level)
  void _closePageSpan(bool masterPage=false);

  void _startSubDocument();
  void _endSubDocument();

  /** adds in propList the frame parameters.

   \note if there is some gradient, first draw a rectangle to print the gradient and them update propList */
  void _handleFrameParameters(librevenge::RVNGPropertyList &propList, MWAWPosition const &pos, MWAWGraphicStyle const &style);

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

  /** creates a new parsing state (copy of the actual state)
   *
   * \return the old one */
  std::shared_ptr<MWAWPresentationListenerInternal::State> _pushParsingState();
  //! resets the previous parsing state
  void _popParsingState();

protected:
  //! the actual global state
  std::shared_ptr<MWAWPresentationListenerInternal::GraphicState> m_ds;
  //! the actual local parse state
  std::shared_ptr<MWAWPresentationListenerInternal::State> m_ps;
  //! stack of local state
  std::vector<std::shared_ptr<MWAWPresentationListenerInternal::State> > m_psStack;
  //! the parser state
  MWAWParserState &m_parserState;
  //! the document interface
  librevenge::RVNGPresentationInterface *m_documentInterface;

private:
  MWAWPresentationListener(const MWAWPresentationListener &) = delete;
  MWAWPresentationListener &operator=(const MWAWPresentationListener &) = delete;
};

#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
