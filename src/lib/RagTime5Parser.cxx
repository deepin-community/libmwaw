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

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWHeader.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"

#include "RagTime5Parser.hxx"

/** Internal: the structures of a RagTime5Parser */
namespace RagTime5ParserInternal
{

////////////////////////////////////////
//! Internal: the state of a RagTime5Parser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)
  {
  }

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;
};

////////////////////////////////////////
//! Internal: the subdocument of a RagTime5Parser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(RagTime5Parser &pars, MWAWInputStreamPtr const &input, int zoneId, MWAWPosition const &pos=MWAWPosition())
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_id(zoneId)
    , m_position(pos) {}

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    if (MWAWSubDocument::operator!=(doc)) return true;
    auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
    if (!sDoc) return true;
    if (m_id != sDoc->m_id) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the subdocument id
  int m_id;
  //! the subdocument position if defined
  MWAWPosition m_position;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("RagTime5ParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (m_id == -1) { // a number used to send linked frame
    listener->insertChar(' ');
    return;
  }
  if (m_id == 0) {
    MWAW_DEBUG_MSG(("RagTime5ParserInternal::SubDocument::parse: unknown zone\n"));
    return;
  }

  if (!m_parser) {
    MWAW_DEBUG_MSG(("RagTime5ParserInternal::SubDocument::parse: can not find the parser\n"));
    return;
  }
  MWAW_DEBUG_MSG(("RagTime5ParserInternal::SubDocument::parse: not implemented\n"));
  //static_cast<RagTime5Parser *>(m_parser)->m_document->sendZone(m_id, listener, m_position);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Parser::RagTime5Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
  , m_document()
{
  init();
}

RagTime5Parser::~RagTime5Parser()
{
}

void RagTime5Parser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new RagTime5ParserInternal::State);
  m_document.reset(new RagTime5Document(*this));
  m_document->m_newPage=static_cast<RagTime5Document::NewPage>(&RagTime5Parser::newPage);
  m_document->m_sendFootnote=static_cast<RagTime5Document::SendFootnote>(&RagTime5Parser::sendFootnote);
  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void RagTime5Parser::newPage(int number, bool soft)
{
  if (number <= m_state->m_actPage || number > m_state->m_numPages)
    return;

  while (m_state->m_actPage < number) {
    m_state->m_actPage++;
    if (!getTextListener() || m_state->m_actPage == 1)
      continue;
    if (soft)
      getTextListener()->insertBreak(MWAWTextListener::SoftPageBreak);
    else
      getTextListener()->insertBreak(MWAWTextListener::PageBreak);
  }
}

////////////////////////////////////////////////////////////
// interface with the different parser
////////////////////////////////////////////////////////////
void RagTime5Parser::sendFootnote(int zoneId)
{
  if (!getTextListener()) return;
  MWAWSubDocumentPtr subdoc(new RagTime5ParserInternal::SubDocument(*this, getInput(), zoneId));
  getTextListener()->insertNote(MWAWNote(MWAWNote::FootNote), subdoc);
}

bool RagTime5Parser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = RagTime5ParserInternal::State();
  if (!m_document->checkHeader(header, strict))
    return false;
  return getParserState()->m_kind==MWAWDocument::MWAW_K_TEXT ||
         getParserState()->m_kind==MWAWDocument::MWAW_K_SPREADSHEET;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void RagTime5Parser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());

    checkHeader(nullptr);
    ok = m_document->createZones();
    if (ok) {
      createDocument(docInterface);
      m_document->sendZones(getMainListener());
#ifdef DEBUG
      m_document->flushExtra(getMainListener());
#endif
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("RagTime5Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void RagTime5Parser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("RagTime5Parser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;
  m_state->m_numPages = m_document->numPages();

  // create the page list
  std::vector<MWAWPageSpan> pageList;
  m_document->updatePageSpanList(pageList);
  //
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->setDocumentMetaData(m_document->getDocumentMetaData());
  listen->startDocument();
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
