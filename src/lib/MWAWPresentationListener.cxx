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

#include <cstring>
#include <iomanip>
#include <sstream>
#include <time.h>

#include <librevenge/librevenge.h>

#include "libmwaw/libmwaw.hxx"
#include "libmwaw_internal.hxx"

#include "MWAWCell.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicEncoder.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWList.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWParser.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSection.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "MWAWPresentationListener.hxx"

//! Internal and low level namespace to define the states of MWAWPresentationListener
namespace MWAWPresentationListenerInternal
{
/** the global graphic state of MWAWPresentationListener */
struct GraphicState {
  //! constructor
  explicit GraphicState(std::vector<MWAWPageSpan> const &pageList)
    : m_pageList(pageList)
    , m_metaData()
    , m_isDocumentStarted(false)
    , m_isPageSpanOpened(false)
    , m_isMasterPageSpanOpened(false)
    , m_isAtLeastOnePageOpened(false)
    , m_isHeaderFooterStarted(false)
    , m_pageSpan()
    , m_sentListMarkers()
    , m_subDocuments()
    , m_section()
  {
  }
  GraphicState &operator=(GraphicState const &)=default;
  GraphicState &operator=(GraphicState &&)=default;
  //! destructor
  ~GraphicState()
  {
  }
  //! the pages definition
  std::vector<MWAWPageSpan> m_pageList;
  //! the document meta data
  librevenge::RVNGPropertyList m_metaData;
  /** a flag to know if the document is open */
  bool m_isDocumentStarted;
  //! true if a page is open
  bool m_isPageSpanOpened;
  //! true if a masterpage is open
  bool m_isMasterPageSpanOpened;
  /** true if the first page has been open */
  bool m_isAtLeastOnePageOpened;
  /** a flag to know if the header footer is started */
  bool m_isHeaderFooterStarted;
  ///! the current page span
  MWAWPageSpan m_pageSpan;
  /// the list of marker corresponding to sent list
  std::vector<int> m_sentListMarkers;
  //! the list of actual subdocument
  std::vector<MWAWSubDocumentPtr> m_subDocuments;
  //! empty section used to return a section in getSection
  MWAWSection m_section;
};

/** the state of a MWAWPresentationListener */
struct State {
  //! constructor
  State();
  //! destructor
  ~State() { }

//! returns true if we are in a text zone, ie. either in a textbox or a table cell
  bool isInTextZone() const
  {
    return m_inNote || m_inLink || m_isTextBoxOpened || m_isTableCellOpened;
  }
  //! the origin position
  MWAWVec2f m_origin;
  //! a buffer to stored the text
  librevenge::RVNGString m_textBuffer;

  //! the font
  MWAWFont m_font;
  //! the paragraph
  MWAWParagraph m_paragraph;
  //! the list of list
  std::shared_ptr<MWAWList> m_list;

  //! a flag to know if openFrame was called
  bool m_isFrameOpened;
  //! the frame position
  MWAWPosition m_framePosition;
  //! the frame style
  MWAWGraphicStyle m_frameStyle;

  //! a flag to know if we are in a textbox
  bool m_isTextBoxOpened;
  //! a flag to know if openGroup was called
  bool m_isGroupOpened;
  //! a flag to know if openLayer was called
  bool m_isLayerOpened;
  bool m_isSpanOpened;
  bool m_isParagraphOpened;
  bool m_isListElementOpened;

  bool m_firstParagraphInPageSpan;

  std::vector<bool> m_listOrderedLevels; //! a stack used to know what is open

  bool m_isTableOpened;
  bool m_isTableRowOpened;
  bool m_isTableColumnOpened;
  bool m_isTableCellOpened;

  unsigned m_currentPage;
  int m_numPagesRemainingInSpan;
  int m_currentPageNumber;

  bool m_inLink;
  bool m_inNote;
  bool m_inSubDocument;
  libmwaw::SubDocumentType m_subDocumentType;

private:
  State(const State &) = delete;
  State &operator=(const State &) = delete;
};

State::State()
  : m_origin(0,0)
  , m_textBuffer("")
  , m_font(20,12)/* default time 12 */
  , m_paragraph()
  , m_list()
  , m_isFrameOpened(false)
  , m_framePosition()
  , m_frameStyle()
  , m_isTextBoxOpened(false)
  , m_isGroupOpened(false)
  , m_isLayerOpened(false)
  , m_isSpanOpened(false)
  , m_isParagraphOpened(false)
  , m_isListElementOpened(false)
  , m_firstParagraphInPageSpan(true)
  , m_listOrderedLevels()
  , m_isTableOpened(false)
  , m_isTableRowOpened(false)
  , m_isTableColumnOpened(false)
  , m_isTableCellOpened(false)
  , m_currentPage(0)
  , m_numPagesRemainingInSpan(0)
  , m_currentPageNumber(1)
  , m_inLink(false)
  , m_inNote(false)
  , m_inSubDocument(false)
  , m_subDocumentType(libmwaw::DOC_NONE)
{
}
}

MWAWPresentationListener::MWAWPresentationListener(MWAWParserState &parserState, std::vector<MWAWPageSpan> const &pageList, librevenge::RVNGPresentationInterface *documentInterface)
  : MWAWListener()
  , m_ds(new MWAWPresentationListenerInternal::GraphicState(pageList))
  , m_ps(new MWAWPresentationListenerInternal::State)
  , m_psStack()
  , m_parserState(parserState)
  , m_documentInterface(documentInterface)
{
}

MWAWPresentationListener::~MWAWPresentationListener()
{
}

///////////////////
// text data
///////////////////
void MWAWPresentationListener::insertChar(uint8_t character)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: called outside a text zone\n"));
    return;
  }
  if (character >= 0x80) {
    MWAWPresentationListener::insertUnicode(character);
    return;
  }
  if (!m_ps->m_isSpanOpened) _openSpan();
  m_ps->m_textBuffer.append(char(character));
}

void MWAWPresentationListener::insertCharacter(unsigned char c)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: called outside a text zone\n"));
    return;
  }
  int unicode = m_parserState.m_fontConverter->unicode(m_ps->m_font.id(), c);
  if (unicode == -1) {
    if (c < 0x20) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: Find odd char %x\n", static_cast<unsigned int>(c)));
    }
    else
      MWAWPresentationListener::insertChar(static_cast<uint8_t>(c));
  }
  else
    MWAWPresentationListener::insertUnicode(static_cast<uint32_t>(unicode));
}

int MWAWPresentationListener::insertCharacter(unsigned char c, MWAWInputStreamPtr &input, long endPos)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: called outside a text zone\n"));
    return 0;
  }
  if (!input || !m_parserState.m_fontConverter) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: input or font converter does not exist!!!!\n"));
    return 0;
  }
  long debPos=input->tell();
  int fId = m_ps->m_font.id();
  int unicode = endPos==debPos ?
                m_parserState.m_fontConverter->unicode(fId, c) :
                m_parserState.m_fontConverter->unicode(fId, c, input);

  long pos=input->tell();
  if (endPos > 0 && pos > endPos) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: problem reading a character\n"));
    pos = debPos;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    unicode = m_parserState.m_fontConverter->unicode(fId, c);
  }
  if (unicode == -1) {
    if (c < 0x20) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::insertCharacter: Find odd char %x\n", static_cast<unsigned int>(c)));
    }
    else
      MWAWPresentationListener::insertChar(static_cast<uint8_t>(c));
  }
  else
    MWAWPresentationListener::insertUnicode(static_cast<uint32_t>(unicode));

  return int(pos-debPos);
}

void MWAWPresentationListener::insertUnicode(uint32_t val)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertUnicode: called outside a text zone\n"));
    return;
  }
  // undef character, we skip it
  if (val == 0xfffd) return;

  if (!m_ps->m_isSpanOpened) _openSpan();
  libmwaw::appendUnicode(val, m_ps->m_textBuffer);
}

void MWAWPresentationListener::insertUnicodeString(librevenge::RVNGString const &str)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertUnicodeString: called outside a text zone\n"));
    return;
  }
  if (!m_ps->m_isSpanOpened) _openSpan();
  m_ps->m_textBuffer.append(str);
}

void MWAWPresentationListener::insertEOL(bool soft)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertEOL: called outside a text zone\n"));
    return;
  }
  if (!m_ps->m_isParagraphOpened && !m_ps->m_isListElementOpened)
    _openSpan();
  if (soft) {
    _flushText();
    m_documentInterface->insertLineBreak();
  }
  else if (m_ps->m_isParagraphOpened)
    _closeParagraph();

  // sub/superscript must not survive a new line
  m_ps->m_font.set(MWAWFont::Script());
}

void MWAWPresentationListener::insertTab()
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertTab: called outside a text zone\n"));
    return;
  }
  if (!m_ps->m_isSpanOpened) _openSpan();
  _flushText();
  m_documentInterface->insertTab();
}

///////////////////
// font/paragraph function
///////////////////
void MWAWPresentationListener::setFont(MWAWFont const &font)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::setFont: called outside a text zone\n"));
    return;
  }
  if (font == m_ps->m_font) return;

  // check if id and size are defined, if not used the previous fields
  MWAWFont finalFont(font);
  if (font.id() == -1)
    finalFont.setId(m_ps->m_font.id());
  if (font.size() <= 0)
    finalFont.setSize(m_ps->m_font.size());
  if (finalFont == m_ps->m_font) return;

  _closeSpan();
  m_ps->m_font = finalFont;
}

MWAWFont const &MWAWPresentationListener::getFont() const
{
  return m_ps->m_font;
}

bool MWAWPresentationListener::isParagraphOpened() const
{
  return m_ps->m_isParagraphOpened;
}

void MWAWPresentationListener::setParagraph(MWAWParagraph const &para)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::setParagraph: called outside a text zone\n"));
    return;
  }
  if (para==m_ps->m_paragraph) return;

  m_ps->m_paragraph=para;
}

MWAWParagraph const &MWAWPresentationListener::getParagraph() const
{
  return m_ps->m_paragraph;
}

///////////////////
// field/link :
///////////////////
void MWAWPresentationListener::insertField(MWAWField const &field)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::setParagraph: called outside a text zone\n"));
    return;
  }
  librevenge::RVNGPropertyList propList;
  if (field.addTo(propList)) {
    _flushText();
    _openSpan();
    m_documentInterface->insertField(propList);
    return;
  }
  librevenge::RVNGString text=field.getString();
  if (!text.empty())
    MWAWPresentationListener::insertUnicodeString(text);
  else {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertField: must not be called with type=%d\n", int(field.m_type)));
  }
}

void MWAWPresentationListener::openLink(MWAWLink const &link)
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openLink: called outside a textbox\n"));
    return;
  }
  if (m_ps->m_inLink) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openLink: called inside a link\n"));
    return;
  }
  if (!m_ps->m_isSpanOpened) _openSpan();
  librevenge::RVNGPropertyList propList;
  link.addTo(propList);
  m_documentInterface->openLink(propList);
  _pushParsingState();
  m_ps->m_inLink=true;
  // we do not want any close open paragraph in a link
  m_ps->m_isParagraphOpened=true;
}

void MWAWPresentationListener::closeLink()
{
  if (!m_ps->m_inLink) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeLink: closed outside a link\n"));
    return;
  }
  _flushText();
  m_documentInterface->closeLink();
  _popParsingState();
}

///////////////////
// document
///////////////////
void MWAWPresentationListener::startDocument()
{
  if (m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::startDocument: the document is already started\n"));
    return;
  }
  m_ds->m_isDocumentStarted=true;
  m_documentInterface->startDocument(librevenge::RVNGPropertyList());
  m_documentInterface->setDocumentMetaData(m_ds->m_metaData);
}

void MWAWPresentationListener::endDocument(bool /*delayed*/)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::endDocument: the document is not started\n"));
    return;
  }
  if (!m_ds->m_isAtLeastOnePageOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::endDocument: no data have been send\n"));
    _openPageSpan();
  }
  if (m_ds->m_isPageSpanOpened)
    _closePageSpan(m_ds->m_isMasterPageSpanOpened);
  m_documentInterface->endDocument();
  m_ds->m_isDocumentStarted=false;
  *m_ds=MWAWPresentationListenerInternal::GraphicState(std::vector<MWAWPageSpan>());
}

///////////////////
// document
///////////////////
void MWAWPresentationListener::setDocumentMetaData(librevenge::RVNGPropertyList const &meta)
{
  librevenge::RVNGPropertyList::Iter i(meta);
  for (i.rewind(); i.next();)
    m_ds->m_metaData.insert(i.key(), i()->getStr());
}

void MWAWPresentationListener::setDocumentLanguage(std::string const &locale)
{
  if (!locale.length()) return;
  m_ds->m_metaData.insert("librevenge:language", locale.c_str());
}

bool MWAWPresentationListener::isDocumentStarted() const
{
  return m_ds->m_isDocumentStarted;
}

bool MWAWPresentationListener::canWriteText() const
{
  return m_ds->m_isPageSpanOpened && m_ps->isInTextZone();
}

///////////////////
// page
///////////////////
bool MWAWPresentationListener::isPageSpanOpened() const
{
  return m_ds->m_isPageSpanOpened;
}

MWAWPageSpan const &MWAWPresentationListener::getPageSpan()
{
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  return m_ds->m_pageSpan;
}

bool MWAWPresentationListener::openMasterPage(MWAWPageSpan &masterPage)
{
  if (m_ds->m_isMasterPageSpanOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openMasterPage: a master page is already opened\n"));
    return false;
  }
  if (!m_ds->m_isDocumentStarted)
    startDocument();
  if (m_ds->m_isPageSpanOpened)
    _closePageSpan();

  librevenge::RVNGPropertyList propList;
  masterPage.getPageProperty(propList, true);
  propList.insert("svg:width",72.*masterPage.getFormWidth(), librevenge::RVNG_POINT);
  propList.insert("svg:height",72.*masterPage.getFormLength(), librevenge::RVNG_POINT);

  m_documentInterface->startMasterSlide(propList);
  m_ds->m_isPageSpanOpened = m_ds->m_isMasterPageSpanOpened = true;

  // checkme: can we send some header/footer if some exists
  return true;
}

void MWAWPresentationListener::_openPageSpan(bool sendHeaderFooters)
{
  if (m_ds->m_isPageSpanOpened)
    return;

  if (!m_ds->m_isDocumentStarted)
    startDocument();

  if (m_ds->m_pageList.size()==0) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_openPageSpan: can not find any page\n"));
    throw libmwaw::ParseException();
  }
  m_ds->m_isAtLeastOnePageOpened=true;
  unsigned actPage = 0;
  auto it = m_ds->m_pageList.begin();
  m_ps->m_currentPage++;
  while (true) {
    actPage+=static_cast<unsigned>(it->getPageSpan());
    if (actPage >= m_ps->m_currentPage) break;
    if (++it == m_ds->m_pageList.end()) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::_openPageSpan: can not find current page, use the previous one\n"));
      --it;
      break;
    }
  }

  MWAWPageSpan &currentPage = *it;
  librevenge::RVNGPropertyList propList;
  currentPage.getPageProperty(propList, true);
  propList.insert("librevenge:is-last-page-span", ++it == m_ds->m_pageList.end());
  // now add data for embedded graph
  propList.insert("svg:x",double(m_ps->m_origin.x()), librevenge::RVNG_POINT);
  propList.insert("svg:y",double(m_ps->m_origin.y()), librevenge::RVNG_POINT);
  propList.insert("svg:width",72.*currentPage.getFormWidth(), librevenge::RVNG_POINT);
  propList.insert("svg:height",72.*currentPage.getFormLength(), librevenge::RVNG_POINT);
  propList.insert("librevenge:enforce-frame",true);

  if (!m_ds->m_isPageSpanOpened)
    m_documentInterface->startSlide(propList);
  m_ds->m_isPageSpanOpened = true;
  m_ds->m_pageSpan = currentPage;

  // we insert the header footer
  if (sendHeaderFooters)
    currentPage.sendHeaderFooters(this, (m_ps->m_currentPage%2) ? MWAWHeaderFooter::EVEN : MWAWHeaderFooter::ODD);

  // first paragraph in span (necessary for resetting page number)
  m_ps->m_firstParagraphInPageSpan = true;
  m_ps->m_numPagesRemainingInSpan = (currentPage.getPageSpan() - 1);
}

void MWAWPresentationListener::_closePageSpan(bool masterPage)
{
  if (!m_ds->m_isPageSpanOpened)
    return;

  if (masterPage && !m_ds->m_isMasterPageSpanOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::endDocument:no master page are opened\n"));
    return;
  }
  if (!masterPage && m_ds->m_isMasterPageSpanOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::endDocument:a master page are opened\n"));
    return;
  }
  if (m_ps->m_inSubDocument) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::endDocument: we are in a sub document\n"));
    _endSubDocument();
    _popParsingState();
  }
  if (m_ps->m_isTableOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_closePageSpan: we are in a table zone\n"));
    closeTable();
  }
  if (m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_closePageSpan: we are in a text zone\n"));
    if (m_ps->m_isParagraphOpened)
      _closeParagraph();
    m_ps->m_paragraph.m_listLevelIndex = 0;
    _changeList(); // flush the list exterior
  }
  m_ds->m_isPageSpanOpened = m_ds->m_isMasterPageSpanOpened = false;
  if (masterPage)
    m_documentInterface->endMasterSlide();
  else
    m_documentInterface->endSlide();
}

///////////////////
// paragraph
///////////////////
void MWAWPresentationListener::_openParagraph()
{
  if (m_ps->m_inNote || (m_ps->m_isTableOpened && !m_ps->m_isTableCellOpened))
    return;
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_openParagraph: called outsize a text zone\n"));
    return;
  }
  if (m_ps->m_isParagraphOpened || m_ps->m_isListElementOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_openParagraph: a paragraph (or a list) is already opened"));
    return;
  }

  librevenge::RVNGPropertyList propList;
  m_ps->m_paragraph.addTo(propList, m_ps->m_isTableCellOpened);
  m_documentInterface->openParagraph(propList);

  _resetParagraphState();
  m_ps->m_firstParagraphInPageSpan = false;
}

void MWAWPresentationListener::_closeParagraph()
{
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_closeParagraph: called outsize a text zone\n"));
    return;
  }
  if (m_ps->m_inLink) return;
  if (m_ps->m_isListElementOpened) {
    _closeListElement();
    return;
  }

  if (m_ps->m_isParagraphOpened) {
    if (m_ps->m_isSpanOpened)
      _closeSpan();
    m_documentInterface->closeParagraph();
  }

  m_ps->m_isParagraphOpened = false;
  m_ps->m_paragraph.m_listLevelIndex = 0;
}

void MWAWPresentationListener::_resetParagraphState(const bool isListElement)
{
  m_ps->m_isListElementOpened = isListElement;
  m_ps->m_isParagraphOpened = true;
}

///////////////////
// list
///////////////////
void MWAWPresentationListener::_openListElement()
{
  if (m_ps->m_inNote || (m_ps->m_isTableOpened && !m_ps->m_isTableCellOpened))
    return;
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_openListElement: called outsize a text zone\n"));
    return;
  }
  if (m_ps->m_isParagraphOpened || m_ps->m_isListElementOpened)
    return;

  librevenge::RVNGPropertyList propList;
  m_ps->m_paragraph.addTo(propList,m_ps->m_isTableOpened);

  // check if we must change the start value
  int startValue=m_ps->m_paragraph.m_listStartValue.get();
  if (startValue > 0 && m_ps->m_list && m_ps->m_list->getStartValueForNextElement() != startValue) {
    propList.insert("text:start-value", startValue);
    m_ps->m_list->setStartValueForNextElement(startValue);
  }

  if (m_ps->m_list) m_ps->m_list->openElement();
  m_documentInterface->openListElement(propList);
  _resetParagraphState(true);
}

void MWAWPresentationListener::_closeListElement()
{
  if (m_ps->m_isListElementOpened) {
    if (m_ps->m_isSpanOpened)
      _closeSpan();

    if (m_ps->m_list) m_ps->m_list->closeElement();
    m_documentInterface->closeListElement();
  }

  m_ps->m_isListElementOpened = m_ps->m_isParagraphOpened = false;
}

int MWAWPresentationListener::_getListId() const
{
  auto newLevel= size_t(m_ps->m_paragraph.m_listLevelIndex.get());
  if (newLevel == 0) return -1;
  int newListId = m_ps->m_paragraph.m_listId.get();
  if (newListId > 0) return newListId;
  static bool first = true;
  if (first) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_getListId: the list id is not set, try to find a new one\n"));
    first = false;
  }
  auto list=m_parserState.m_listManager->getNewList(m_ps->m_list, int(newLevel), *m_ps->m_paragraph.m_listLevel);
  if (!list) return -1;
  return list->getId();
}

void MWAWPresentationListener::_changeList()
{
  if (m_ps->m_inNote || !m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_changeList: called outsize a text zone\n"));
    return;
  }
  if (m_ps->m_isParagraphOpened)
    _closeParagraph();

  size_t actualLevel = m_ps->m_listOrderedLevels.size();
  auto newLevel= size_t(m_ps->m_paragraph.m_listLevelIndex.get());
  if (newLevel>100) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_changeList: find level=%d, set it to 100\n", static_cast<int>(newLevel)));
    newLevel=100;
  }
  int newListId = newLevel>0 ? _getListId() : -1;
  bool changeList = newLevel &&
                    (m_ps->m_list && m_ps->m_list->getId()!=newListId);
  size_t minLevel = changeList ? 0 : newLevel;
  while (actualLevel > minLevel) {
    if (m_ps->m_listOrderedLevels[--actualLevel])
      m_documentInterface->closeOrderedListLevel();
    else
      m_documentInterface->closeUnorderedListLevel();
  }

  if (newLevel) {
    std::shared_ptr<MWAWList> theList;

    theList=m_parserState.m_listManager->getList(newListId);
    if (!theList) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::_changeList: can not find any list\n"));
      m_ps->m_listOrderedLevels.resize(actualLevel);
      return;
    }
    m_parserState.m_listManager->needToSend(newListId, m_ds->m_sentListMarkers);
    m_ps->m_list = theList;
    m_ps->m_list->setLevel(static_cast<int>(newLevel));
  }

  m_ps->m_listOrderedLevels.resize(newLevel, false);
  if (actualLevel == newLevel) return;

  librevenge::RVNGPropertyList propList;
  propList.insert("librevenge:list-id", m_ps->m_list->getId());
  for (size_t i=actualLevel+1; i<= newLevel; i++) {
    bool ordered = m_ps->m_list->isNumeric(int(i));
    m_ps->m_listOrderedLevels[i-1] = ordered;

    librevenge::RVNGPropertyList level;
    m_ps->m_list->addTo(int(i), level, m_parserState.m_fontManager);
    if (ordered)
      m_documentInterface->openOrderedListLevel(level);
    else
      m_documentInterface->openUnorderedListLevel(level);
  }
}

///////////////////
// span
///////////////////
void MWAWPresentationListener::_openSpan()
{
  if (m_ps->m_isTableOpened && !m_ps->m_isTableCellOpened)
    return;
  if (m_ps->m_inLink)
    return;
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_openSpan: called outsize a text zone\n"));
    return;
  }
  if (m_ps->m_isSpanOpened)
    return;

  if (!m_ps->m_isParagraphOpened && !m_ps->m_isListElementOpened) {
    _changeList();
    if (*m_ps->m_paragraph.m_listLevelIndex == 0)
      _openParagraph();
    else
      _openListElement();
  }

  librevenge::RVNGPropertyList propList;
  m_ps->m_font.addTo(propList, m_parserState.m_fontConverter);

  m_documentInterface->openSpan(propList);

  m_ps->m_isSpanOpened = true;
}

void MWAWPresentationListener::_closeSpan()
{
  if (m_ps->m_isTableOpened && !m_ps->m_isTableCellOpened)
    return;
  if (!m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_closeSpan: called outsize a text zone\n"));
    return;
  }
  if (!m_ps->m_isSpanOpened)
    return;

  _flushText();
  m_documentInterface->closeSpan();
  m_ps->m_isSpanOpened = false;
}

///////////////////
// text (send data)
///////////////////
void MWAWPresentationListener::_flushText()
{
  if (m_ps->m_textBuffer.len() == 0) return;

  // when some many ' ' follows each other, call insertSpace
  librevenge::RVNGString tmpText("");
  int numConsecutiveSpaces = 0;
  librevenge::RVNGString::Iter i(m_ps->m_textBuffer);
  for (i.rewind(); i.next();) {
    if (*(i()) == 0x20) // this test is compatible with unicode format
      numConsecutiveSpaces++;
    else
      numConsecutiveSpaces = 0;

    if (numConsecutiveSpaces > 1) {
      if (tmpText.len() > 0) {
        m_documentInterface->insertText(tmpText);
        tmpText.clear();
      }
      m_documentInterface->insertSpace();
    }
    else
      tmpText.append(i());
  }
  m_documentInterface->insertText(tmpText);
  m_ps->m_textBuffer.clear();
}

///////////////////
// header/footer
///////////////////
bool MWAWPresentationListener::isHeaderFooterOpened() const
{
  return m_ds->m_isHeaderFooterStarted;
}

bool MWAWPresentationListener::insertHeader(MWAWSubDocumentPtr const &subDocument, librevenge::RVNGPropertyList const &extras)
{
  if (m_ds->m_isHeaderFooterStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertHeader: Oops a header/footer is already opened\n"));
    return false;
  }
  // we do not have any header interface, so mimick it by creating a textbox
  MWAWPosition pos(MWAWVec2f(20,20), MWAWVec2f(-20,-10), librevenge::RVNG_POINT); // fixme
  pos.m_anchorTo=MWAWPosition::Page;
  if (!openFrame(pos))
    return false;
  librevenge::RVNGPropertyList propList(extras);
  _handleFrameParameters(propList, pos, MWAWGraphicStyle::emptyStyle());

  m_documentInterface->startTextObject(propList);
  handleSubDocument(pos.origin(), subDocument, libmwaw::DOC_HEADER_FOOTER);
  m_documentInterface->endTextObject();
  closeFrame();
  return true;
}

bool MWAWPresentationListener::insertFooter(MWAWSubDocumentPtr const &subDocument, librevenge::RVNGPropertyList const &extras)
{
  if (m_ds->m_isHeaderFooterStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertFooter: Oops a header/footer is already opened\n"));
    return false;
  }
  MWAW_DEBUG_MSG(("MWAWPresentationListener::insertFooter: inserting footer is very experimental\n"));

  // we do not have any header interface, so mimick it by creating a textbox
  MWAWPageSpan page(getPageSpan()); // fixme
  MWAWPosition pos(MWAWVec2f(20,72.f*float(page.getFormLength())-40.f), MWAWVec2f(-20,-10), librevenge::RVNG_POINT);
  pos.m_anchorTo=MWAWPosition::Page;
  if (!openFrame(pos))
    return false;
  librevenge::RVNGPropertyList propList(extras);
  _handleFrameParameters(propList, pos, MWAWGraphicStyle::emptyStyle());

  m_documentInterface->startTextObject(propList);
  handleSubDocument(pos.origin(), subDocument, libmwaw::DOC_HEADER_FOOTER);
  m_documentInterface->endTextObject();
  closeFrame();
  return true;
}

///////////////////
// section
///////////////////
MWAWSection const &MWAWPresentationListener::getSection() const
{
  MWAW_DEBUG_MSG(("MWAWPresentationListener::getSection: must not be called\n"));
  return m_ds->m_section;
}

bool MWAWPresentationListener::openSection(MWAWSection const &)
{
  MWAW_DEBUG_MSG(("MWAWPresentationListener::openSection: must not be called\n"));
  return false;
}

void MWAWPresentationListener::insertBreak(BreakType breakType)
{
  if (m_ps->m_inSubDocument)
    return;

  switch (breakType) {
  case ColumnBreak:
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertBreak: must not be called on column\n"));
    break;
  case SoftPageBreak:
  case PageBreak:
    if (m_ds->m_isMasterPageSpanOpened) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::insertBreak: can not insert a page break in master page definition\n"));
      break;
    }
    if (!m_ds->m_isPageSpanOpened)
      _openPageSpan();
    _closePageSpan();
    break;
#if !defined(__clang__)
  default:
    break;
#endif
  }
}

///////////////////
// note/comment ( we inserted them as text between -- -- )
///////////////////
void MWAWPresentationListener::insertComment(MWAWSubDocumentPtr &subDocument)
{
  if (!canWriteText() || m_ps->m_inNote) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertComment try to insert recursively or outside a text zone\n"));
    return;
  }
  // first check that a paragraph is already open
  if (!m_ps->m_isParagraphOpened && !m_ps->m_isListElementOpened)
    _openParagraph();
  insertChar(' ');
  insertUnicode(0x2014); // -
  insertChar(' ');
  handleSubDocument(subDocument, libmwaw::DOC_COMMENT_ANNOTATION);
  insertChar(' ');
  insertUnicode(0x2014); // -
  insertChar(' ');
}

void MWAWPresentationListener::insertNote(MWAWNote const &, MWAWSubDocumentPtr &subDocument)
{
  if (!canWriteText() || m_ps->m_inNote) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertNote try to insert recursively or outside a text zone\n"));
    return;
  }
  // first check that a paragraph is already open
  if (!m_ps->m_isParagraphOpened && !m_ps->m_isListElementOpened)
    _openParagraph();
  insertChar(' ');
  insertUnicode(0x2014); // -
  insertChar(' ');
  handleSubDocument(subDocument, libmwaw::DOC_NOTE);
  insertChar(' ');
  insertUnicode(0x2014); // -
  insertChar(' ');
}

///////////////////
// picture/textbox
///////////////////

void MWAWPresentationListener::insertShape
(MWAWPosition const &pos, MWAWGraphicShape const &shape, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertShape: the document is not started\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  if (m_ps->m_isFrameOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertShape: a frame is already open\n"));
    return;
  }

  librevenge::RVNGPropertyList list, shapePList;
  style.addTo(list, shape.getType()==MWAWGraphicShape::Line);
  m_documentInterface->setStyle(list);
  switch (shape.addTo(1.f/pos.getInvUnitScale(librevenge::RVNG_POINT)*pos.origin()-m_ps->m_origin, style.hasSurface(), shapePList)) {
  case MWAWGraphicShape::C_Ellipse:
    m_documentInterface->drawEllipse(shapePList);
    break;
  case MWAWGraphicShape::C_Path:
    m_documentInterface->drawPath(shapePList);
    break;
  case MWAWGraphicShape::C_Polyline:
    m_documentInterface->drawPolyline(shapePList);
    break;
  case MWAWGraphicShape::C_Polygon:
    m_documentInterface->drawPolygon(shapePList);
    break;
  case MWAWGraphicShape::C_Rectangle:
    m_documentInterface->drawRectangle(shapePList);
    break;
  case MWAWGraphicShape::C_Bad:
    break;
#if !defined(__clang__)
  default:
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertShape: unexpected shape\n"));
    break;
#endif
  }
}

void MWAWPresentationListener::insertPicture(MWAWPosition const &pos, MWAWEmbeddedObject const &picture, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertPicture: the document is not started\n"));
    return;
  }
  if (m_ps->m_isFrameOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertPicture: a frame is already open\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  librevenge::RVNGPropertyList list;
  style.addTo(list);
  m_documentInterface->setStyle(list);

  list.clear();
  _handleFrameParameters(list, pos, style);
  float rotate = style.m_rotate;
  if (style.m_flip[0]&&style.m_flip[1]) rotate += 180.f;
  if (rotate<0||rotate>0) {
    list.insert("librevenge:rotate", double(rotate), librevenge::RVNG_GENERIC);
    float pointFactor =1.f/pos.getInvUnitScale(librevenge::RVNG_POINT);
    MWAWVec2f size=pointFactor*pos.size();
    if (size[0]<0) size[0]=-size[0];
    if (size[1]<0) size[1]=-size[1];
    MWAWVec2f center=pointFactor*pos.origin()-m_ps->m_origin+0.5f*size;
    list.insert("librevenge:rotate-cx",double(center[0]), librevenge::RVNG_POINT);
    list.insert("librevenge:rotate-cy",double(center[1]), librevenge::RVNG_POINT);
  }
  if (picture.addTo(list))
    m_documentInterface->drawGraphicObject(list);
}

void MWAWPresentationListener::insertTextBox
(MWAWPosition const &pos, MWAWSubDocumentPtr const &subDocument, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertTextBox: the document is not started\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  float pointFactor =1.f/pos.getInvUnitScale(librevenge::RVNG_POINT);
  if (m_ps->m_isTextBoxOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertTextBox: can not insert a textbox in a textbox\n"));
    handleSubDocument(pointFactor*pos.origin(), subDocument, libmwaw::DOC_TEXT_BOX);
    return;
  }
  if (!openFrame(pos))
    return;
  librevenge::RVNGPropertyList propList;
  _handleFrameParameters(propList, pos, style);
  float rotate = style.m_rotate;
  // flip does not works on text, so we ignore it...
  if (style.m_flip[0]&&style.m_flip[1]) rotate += 180.f;
  if (rotate<0||rotate>0) {
    propList.insert("librevenge:rotate", double(rotate), librevenge::RVNG_GENERIC);
    MWAWVec2f size=pointFactor*pos.size();
    if (size[0]<0) size[0]=-size[0];
    if (size[1]<0) size[1]=-size[1];
    MWAWVec2f center=pointFactor*pos.origin()-m_ps->m_origin+0.5f*size;
    propList.insert("librevenge:rotate-cx", double(center[0]), librevenge::RVNG_POINT);
    propList.insert("librevenge:rotate-cy", double(center[1]), librevenge::RVNG_POINT);
  }
  m_documentInterface->startTextObject(propList);
  handleSubDocument(pointFactor*pos.origin(), subDocument, libmwaw::DOC_TEXT_BOX);
  m_documentInterface->endTextObject();
  closeFrame();
}

void MWAWPresentationListener::insertSlideNote(MWAWPosition const &pos, MWAWSubDocumentPtr &subDocument)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertSlideNote: the document is not started\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  float pointFactor =1.f/pos.getInvUnitScale(librevenge::RVNG_POINT);
  if (m_ps->m_isTextBoxOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertSlideNote: can not insert a textbox in a textbox\n"));
    handleSubDocument(pointFactor*pos.origin(), subDocument, libmwaw::DOC_TEXT_BOX);
    return;
  }
  if (!openFrame(pos))
    return;
  librevenge::RVNGPropertyList propList;
  _handleFrameParameters(propList, pos, MWAWGraphicStyle::emptyStyle());
  m_documentInterface->startNotes(propList);
  handleSubDocument(pointFactor*pos.origin(), subDocument, libmwaw::DOC_TEXT_BOX);
  m_documentInterface->endNotes();
  closeFrame();
}

void MWAWPresentationListener::insertGroup(MWAWBox2f const &bdbox, MWAWSubDocumentPtr const &subDocument)
{
  if (!m_ds->m_isDocumentStarted || m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertGroup: can not insert a group\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  handleSubDocument(bdbox[0], subDocument, libmwaw::DOC_GRAPHIC_GROUP);
}

///////////////////
// table
///////////////////
void MWAWPresentationListener::insertTable
(MWAWPosition const &pos, MWAWTable &table, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted || m_ps->m_inSubDocument) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertTable insert a table in a subdocument is not implemented\n"));
    return;
  }
  if (!openFrame(pos, style)) return;

  _pushParsingState();
  _startSubDocument();
  m_ps->m_subDocumentType = libmwaw::DOC_TABLE;

  std::shared_ptr<MWAWListener> listen(this, MWAW_shared_ptr_noop_deleter<MWAWPresentationListener>());
  try {
    table.sendTable(listen);
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::insertTable exception catched \n"));
  }
  _endSubDocument();
  _popParsingState();

  closeFrame();
}

void MWAWPresentationListener::openTable(MWAWTable const &table)
{
  if (!m_ps->m_isFrameOpened) {
    if (m_ps->m_isTextBoxOpened) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::openTable: must not be called inside a textbox\n"));
      MWAWPosition pos(m_ps->m_origin, MWAWVec2f(400,100), librevenge::RVNG_POINT);
      pos.m_anchorTo=MWAWPosition::Page;
      openTable(pos, table, MWAWGraphicStyle::emptyStyle());
      return;
    }
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTable: called outside openFrame\n"));
    return;
  }
  openTable(m_ps->m_framePosition, table, m_ps->m_frameStyle);
}

void MWAWPresentationListener::openTable(MWAWPosition const &pos, MWAWTable const &table, MWAWGraphicStyle const &style)
{
  if (!m_ps->m_isFrameOpened || m_ps->m_isTableOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTable: no frame is already open...\n"));
    return;
  }

  if (m_ps->m_isParagraphOpened)
    _closeParagraph();

  librevenge::RVNGPropertyList propList;
  // default value: which can be redefined by table
  propList.insert("table:align", "left");
  propList.insert("fo:margin-left", *m_ps->m_paragraph.m_margins[1], *m_ps->m_paragraph.m_marginsUnit);
  _pushParsingState();
  _startSubDocument();
  m_ps->m_subDocumentType = libmwaw::DOC_TABLE;

  _handleFrameParameters(propList, pos, style);
  table.addTablePropertiesTo(propList);
  m_documentInterface->startTableObject(propList);
  m_ps->m_isTableOpened = true;
}

void MWAWPresentationListener::closeTable()
{
  if (!m_ps->m_isTableOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeTable: called with m_isTableOpened=false\n"));
    return;
  }

  m_ps->m_isTableOpened = false;
  _endSubDocument();
  m_documentInterface->endTableObject();

  _popParsingState();
}

void MWAWPresentationListener::openTableRow(float h, librevenge::RVNGUnit unit, bool headerRow)
{
  if (m_ps->m_isTableRowOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTableRow: called with m_isTableRowOpened=true\n"));
    return;
  }
  if (!m_ps->m_isTableOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTableRow: called with m_isTableOpened=false\n"));
    return;
  }
  librevenge::RVNGPropertyList propList;
  propList.insert("librevenge:is-header-row", headerRow);

  if (h > 0)
    propList.insert("style:row-height", double(h), unit);
  else if (h < 0)
    propList.insert("style:min-row-height", double(-h), unit);
  m_documentInterface->openTableRow(propList);
  m_ps->m_isTableRowOpened = true;
}

void MWAWPresentationListener::closeTableRow()
{
  if (!m_ps->m_isTableRowOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeTableRow: called with m_isTableRowOpened=false\n"));
    return;
  }
  m_ps->m_isTableRowOpened = false;
  m_documentInterface->closeTableRow();
}

void MWAWPresentationListener::addEmptyTableCell(MWAWVec2i const &pos, MWAWVec2i span)
{
  if (!m_ps->m_isTableRowOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::addEmptyTableCell: called with m_isTableRowOpened=false\n"));
    return;
  }
  if (m_ps->m_isTableCellOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::addEmptyTableCell: called with m_isTableCellOpened=true\n"));
    closeTableCell();
  }
  librevenge::RVNGPropertyList propList;
  propList.insert("librevenge:column", pos[0]);
  propList.insert("librevenge:row", pos[1]);
  propList.insert("table:number-columns-spanned", span[0]);
  propList.insert("table:number-rows-spanned", span[1]);
  m_documentInterface->openTableCell(propList);
  m_documentInterface->closeTableCell();
}

void MWAWPresentationListener::openTableCell(MWAWCell const &cell)
{
  if (!m_ps->m_isTableRowOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTableCell: called with m_isTableRowOpened=false\n"));
    return;
  }
  if (m_ps->m_isTableCellOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openTableCell: called with m_isTableCellOpened=true\n"));
    closeTableCell();
  }

  librevenge::RVNGPropertyList propList;
  cell.addTo(propList, m_parserState.m_fontConverter);
  m_ps->m_isTableCellOpened = true;
  m_documentInterface->openTableCell(propList);
}

void MWAWPresentationListener::closeTableCell()
{
  if (!m_ps->m_isTableCellOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeTableCell: called with m_isTableCellOpened=false\n"));
    return;
  }

  _closeParagraph();
  m_ps->m_paragraph.m_listLevelIndex=0;
  _changeList(); // flush the list exterior

  m_documentInterface->closeTableCell();
  m_ps->m_isTableCellOpened = false;
}

///////////////////
// frame/group
///////////////////
bool MWAWPresentationListener::openFrame(MWAWPosition const &pos, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openFrame: the document is not started\n"));
    return false;
  }
  if (m_ps->m_isTableOpened && !m_ps->m_isTableCellOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openFrame: called in table but cell is not opened\n"));
    return false;
  }
  if (m_ps->m_isFrameOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openFrame: called but a frame is already opened\n"));
    return false;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  m_ps->m_isFrameOpened = true;
  m_ps->m_framePosition=pos;
  m_ps->m_frameStyle=style;
  return true;
}

void MWAWPresentationListener::closeFrame()
{
  if (!m_ps->m_isFrameOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeFrame: called but no frame is already opened\n"));
    return;
  }
  m_ps->m_isFrameOpened = false;
}

bool  MWAWPresentationListener::openGroup(MWAWPosition const &pos)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openGroup: the document is not started\n"));
    return false;
  }
  if (m_ps->m_isTableOpened || m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openGroup: called in table or in a text zone\n"));
    return false;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();

  librevenge::RVNGPropertyList propList;
  _handleFrameParameters(propList, pos, MWAWGraphicStyle::emptyStyle());

  _pushParsingState();
  _startSubDocument();
  m_ps->m_isGroupOpened = true;

  m_documentInterface->openGroup(propList);

  return true;
}

void  MWAWPresentationListener::closeGroup()
{
  if (!m_ps->m_isGroupOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeGroup: called but no group is already opened\n"));
    return;
  }
  _endSubDocument();
  _popParsingState();
  m_documentInterface->closeGroup();
}

bool MWAWPresentationListener::openLayer(librevenge::RVNGString const &layerName)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openLayer: the document is not started\n"));
    return false;
  }
  if (m_ps->m_isTableOpened || m_ps->isInTextZone()) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openLayer: called in table or in a text zone\n"));
    return false;
  }
  if (m_ps->m_isLayerOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::openLayer: called but layer is already opened\n"));
    return false;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();

  _pushParsingState();
  _startSubDocument();
  m_ps->m_isLayerOpened = true;

  librevenge::RVNGPropertyList propList;
  propList.insert("draw:layer", layerName);
  m_documentInterface->startLayer(propList);
  return true;
}

void  MWAWPresentationListener::closeLayer()
{
  if (!m_ps->m_isLayerOpened) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::closeLayer: called but no layer is already opened\n"));
    return;
  }
  m_documentInterface->endLayer();
  _endSubDocument();
  _popParsingState();
}

void MWAWPresentationListener::_handleFrameParameters(librevenge::RVNGPropertyList &list, MWAWPosition const &pos, MWAWGraphicStyle const &style)
{
  if (!m_ds->m_isDocumentStarted)
    return;

  librevenge::RVNGUnit unit = pos.unit();
  float pointFactor = pos.getInvUnitScale(librevenge::RVNG_POINT);
  float inchFactor=pos.getInvUnitScale(librevenge::RVNG_INCH);
  // first compute the origin ( in given unit and in point)
  MWAWVec2f origin = pos.origin()-pointFactor*m_ps->m_origin;
  MWAWVec2f originPt = (1.f/pointFactor)*pos.origin()-m_ps->m_origin;
  MWAWVec2f size = pos.size();
  // checkme: do we still need to do that ?
  if (style.hasGradient(true)) {
    if (style.m_rotate<0 || style.m_rotate>0) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::_handleFrameParameters: rotation is not implemented\n"));
    }
    // ok, first send a background rectangle
    librevenge::RVNGPropertyList rectList;
    m_documentInterface->setStyle(rectList);
    rectList.clear();
    rectList.insert("svg:x",double(originPt[0]), librevenge::RVNG_POINT);
    rectList.insert("svg:y",double(originPt[1]), librevenge::RVNG_POINT);
    rectList.insert("svg:width",size.x()>0 ? double(size.x()) : double(-size.x()), unit);
    rectList.insert("svg:height",size.y()>0 ? double(size.y()) : double(-size.y()), unit);
    m_documentInterface->drawRectangle(rectList);

    list.insert("draw:stroke", "none");
    list.insert("draw:fill", "none");
  }
  else
    style.addTo(list);

  list.insert("svg:x", double(originPt[0]), librevenge::RVNG_POINT);
  list.insert("svg:y", double(originPt[1]), librevenge::RVNG_POINT);
  if (size.x()>0)
    list.insert("svg:width", double(size.x()), unit);
  else if (size.x()<0)
    list.insert("fo:min-width", double(-size.x()), unit);
  if (size.y()>0)
    list.insert("svg:height", double(size.y()), unit);
  else if (size.y()<0)
    list.insert("fo:min-height", double(-size.y()), unit);
  if (pos.order() > 0)
    list.insert("draw:z-index", pos.order());
  if (pos.naturalSize().x() > 4*pointFactor && pos.naturalSize().y() > 4*pointFactor) {
    list.insert("librevenge:naturalWidth", double(pos.naturalSize().x()), pos.unit());
    list.insert("librevenge:naturalHeight", double(pos.naturalSize().y()), pos.unit());
  }
  MWAWVec2f TLClip = (1.f/pointFactor)*pos.leftTopClipping();
  MWAWVec2f RBClip = (1.f/pointFactor)*pos.rightBottomClipping();
  if (TLClip[0] > 0 || TLClip[1] > 0 || RBClip[0] > 0 || RBClip[1] > 0) {
    // in ODF1.2 we need to separate the value with ,
    std::stringstream s;
    s << "rect(" << TLClip[1] << "pt " << RBClip[0] << "pt "
      <<  RBClip[1] << "pt " << TLClip[0] << "pt)";
    list.insert("fo:clip", s.str().c_str());
  }

  if (pos.m_wrapping ==  MWAWPosition::WDynamic)
    list.insert("style:wrap", "dynamic");
  else if (pos.m_wrapping ==  MWAWPosition::WBackground) {
    list.insert("style:wrap", "run-through");
    list.insert("style:run-through", "background");
  }
  else if (pos.m_wrapping ==  MWAWPosition::WForeground) {
    list.insert("style:wrap", "run-through");
    list.insert("style:run-through", "foreground");
  }
  else if (pos.m_wrapping ==  MWAWPosition::WParallel) {
    list.insert("style:wrap", "parallel");
    list.insert("style:run-through", "foreground");
  }
  else if (pos.m_wrapping ==  MWAWPosition::WRunThrough)
    list.insert("style:wrap", "run-through");
  else
    list.insert("style:wrap", "none");
  if (pos.m_anchorTo != MWAWPosition::Page) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_handleFrameParameters: only page anchor is implemented\n"));
  }
  else {
    double w = m_ds->m_pageSpan.getFormWidth();
    double h = m_ds->m_pageSpan.getFormLength();
    w *= double(inchFactor);
    h *= double(inchFactor);
    double newPosition;
    switch (pos.m_yPos) {
    case MWAWPosition::YFull:
      list.insert("svg:height", double(h), unit);
      MWAW_FALLTHROUGH;
    case MWAWPosition::YTop:
      if (origin[1] < 0 || origin[1] > 0) {
        list.insert("style:vertical-pos", "from-top");
        newPosition = double(origin[1]);
        if (newPosition > h -double(pos.size()[1]))
          newPosition = h - double(pos.size()[1]);
        list.insert("svg:y", double(newPosition), unit);
      }
      else
        list.insert("style:vertical-pos", "top");
      break;
    case MWAWPosition::YCenter:
      if (origin[1] < 0 || origin[1] > 0) {
        list.insert("style:vertical-pos", "from-top");
        newPosition = (h - double(pos.size()[1]))/2.0;
        if (newPosition > h -double(pos.size()[1])) newPosition = h - double(pos.size()[1]);
        list.insert("svg:y", double(newPosition), unit);
      }
      else
        list.insert("style:vertical-pos", "middle");
      break;
    case MWAWPosition::YBottom:
      if (origin[1] < 0 || origin[1] > 0) {
        list.insert("style:vertical-pos", "from-top");
        newPosition = h - double(pos.size()[1])-double(origin[1]);
        if (newPosition > h -double(pos.size()[1])) newPosition = h -double(pos.size()[1]);
        else if (newPosition < 0) newPosition = 0;
        list.insert("svg:y", double(newPosition), unit);
      }
      else
        list.insert("style:vertical-pos", "bottom");
      break;
#if !defined(__clang__)
    default:
      break;
#endif
    }

    switch (pos.m_xPos) {
    case MWAWPosition::XFull:
      list.insert("svg:width", double(w), unit);
      MWAW_FALLTHROUGH;
    case MWAWPosition::XLeft:
      if (origin[0] < 0 || origin[0] > 0) {
        list.insert("style:horizontal-pos", "from-left");
        list.insert("svg:x", double(origin[0]), unit);
      }
      else
        list.insert("style:horizontal-pos", "left");
      break;
    case MWAWPosition::XRight:
      if (origin[0] < 0 || origin[0] > 0) {
        list.insert("style:horizontal-pos", "from-left");
        list.insert("svg:x",w - double(pos.size()[0]) + double(origin[0]), unit);
      }
      else
        list.insert("style:horizontal-pos", "right");
      break;
    case MWAWPosition::XCenter:
      if (origin[0] < 0 || origin[0] > 0) {
        list.insert("style:horizontal-pos", "from-left");
        list.insert("svg:x", (w - double(pos.size()[0]))/2. + double(origin[0]), unit);
      }
      else
        list.insert("style:horizontal-pos", "center");
      break;
#if !defined(__clang__)
    default:
      break;
#endif
    }

  }
  float const padding = 0; // fillme
  list.insert("fo:padding-top", double(padding), librevenge::RVNG_POINT);
  list.insert("fo:padding-bottom", double(padding), librevenge::RVNG_POINT);
  list.insert("fo:padding-left", double(padding), librevenge::RVNG_POINT);
  list.insert("fo:padding-right", double(padding), librevenge::RVNG_POINT);
}

///////////////////
// subdocument
///////////////////
void MWAWPresentationListener::handleSubDocument(MWAWVec2f const &orig, MWAWSubDocumentPtr const &subDocument, libmwaw::SubDocumentType subDocumentType)
{
  if (!m_ds->m_isDocumentStarted) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::handleSubDocument: the graphic is not started\n"));
    return;
  }
  if (!m_ds->m_isPageSpanOpened)
    _openPageSpan();
  MWAWVec2f actOrigin=m_ps->m_origin;
  _pushParsingState();
  m_ps->m_origin=actOrigin-orig;
  _startSubDocument();
  m_ps->m_subDocumentType = subDocumentType;

  m_ps->m_list.reset();
  if (subDocumentType==libmwaw::DOC_TEXT_BOX)
    m_ps->m_isTextBoxOpened=true;
  else if (subDocumentType==libmwaw::DOC_HEADER_FOOTER) {
    m_ps->m_isTextBoxOpened=true;
    m_ds->m_isHeaderFooterStarted = true;
  }
  else if (subDocumentType==libmwaw::DOC_COMMENT_ANNOTATION || subDocumentType==libmwaw::DOC_NOTE)
    m_ps->m_inNote=true;
  // Check whether the document is calling itself
  bool sendDoc = true;
  for (auto doc : m_ds->m_subDocuments) {
    if (!subDocument)
      break;
    if (!doc) continue;
    if (*subDocument == *doc) {
      MWAW_DEBUG_MSG(("MWAWPresentationListener::handleSubDocument: recursif call, stop...\n"));
      sendDoc = false;
      break;
    }
  }
  if (sendDoc) {
    if (subDocument) {
      m_ds->m_subDocuments.push_back(subDocument);
      std::shared_ptr<MWAWListener> listen(this, MWAW_shared_ptr_noop_deleter<MWAWListener>());
      try {
        subDocument->parse(listen, subDocumentType);
      }
      catch (...) {
        MWAW_DEBUG_MSG(("MWAWPresentationListener::handleSubDocument exception catched\n"));
      }
      m_ds->m_subDocuments.pop_back();
    }
  }

  _endSubDocument();
  _popParsingState();

  if (subDocumentType==libmwaw::DOC_HEADER_FOOTER)
    m_ds->m_isHeaderFooterStarted = false;
}

bool MWAWPresentationListener::isSubDocumentOpened(libmwaw::SubDocumentType &subdocType) const
{
  if (!m_ds->m_isDocumentStarted || !m_ps->m_inSubDocument)
    return false;
  subdocType = m_ps->m_subDocumentType;
  return true;
}

void MWAWPresentationListener::_startSubDocument()
{
  if (!m_ds->m_isDocumentStarted) return;
  m_ps->m_inSubDocument = true;
}

void MWAWPresentationListener::_endSubDocument()
{
  if (!m_ds->m_isDocumentStarted) return;
  if (m_ps->m_isTableOpened)
    closeTable();
  if (m_ps->m_isParagraphOpened)
    _closeParagraph();
  if (m_ps->isInTextZone()) {
    m_ps->m_paragraph.m_listLevelIndex=0;
    _changeList(); // flush the list exterior
  }
}

///////////////////
// others
///////////////////

// ---------- state stack ------------------
std::shared_ptr<MWAWPresentationListenerInternal::State> MWAWPresentationListener::_pushParsingState()
{
  auto actual = m_ps;
  m_psStack.push_back(actual);
  m_ps.reset(new MWAWPresentationListenerInternal::State);
  m_ps->m_origin=actual->m_origin;
  return actual;
}

void MWAWPresentationListener::_popParsingState()
{
  if (m_psStack.size()==0) {
    MWAW_DEBUG_MSG(("MWAWPresentationListener::_popParsingState: psStack is empty()\n"));
    throw libmwaw::ParseException();
  }
  m_ps = m_psStack.back();
  m_psStack.pop_back();
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
