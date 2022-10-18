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
 * Parser to convert some WordMaker 1.0 text document
 *
 */
#ifndef WORD_MAKER_PARSER
#  define WORD_MAKER_PARSER

#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace WordMakerParserInternal
{
struct State;
struct Zone;

class SubDocument;
}

/** \brief the main class to read a WordMaker file
 *
 *
 *
 */
class WordMakerParser final : public MWAWTextParser
{
  friend class WordMakerParserInternal::SubDocument;

public:
  //! constructor
  WordMakerParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~WordMakerParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! adds a new page
  void newPage();

  //! try to send the content of a zone
  bool sendZone(WordMakerParserInternal::Zone const &zone);
  //! try to send a picture
  bool sendPicture(MWAWEntry const &entry);
protected:
  //
  // data
  //
  //! try to read a font and its beginning position
  bool readFont(long len, MWAWFont &font);
  //! try to read the fonts names
  bool readFontNames(long len);
  //! try to read a paragraph (and its default font)
  bool readParagraph(long len, MWAWParagraph &para, MWAWFont &font);
  //! try to read a picture (and its size)
  bool readPicture(long len, MWAWEmbeddedObject &object, MWAWBox2f &bdbox, int &page);
  //! try to read a list of tabulations
  bool readTabulations(long len, MWAWParagraph &para);
  //! try to read the printer information
  bool readPrintInfo(long len);

  //! the state
  std::shared_ptr<WordMakerParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
