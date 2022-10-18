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
 * Parser to convert some Script Writer 1.3 text document
 *
 */
#ifndef SCRIPT_WRITER_PARSER
#  define SCRIPT_WRITER_PARSER

#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace ScriptWriterParserInternal
{
struct Frame;
struct HFData;
struct Page;
struct State;

class SubDocument;
}

/** \brief the main class to read a Script Writer 1.3 file
 *
 *
 *
 */
class ScriptWriterParser final : public MWAWTextParser
{
  friend class ScriptWriterParserInternal::SubDocument;

public:
  //! constructor
  ScriptWriterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~ScriptWriterParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! try to read the document header
  bool readDocument();
  //! try to read the print information
  bool readPrintInfo();

  //! try to read a frame entry
  bool readFrame(ScriptWriterParserInternal::Frame &frame);

  //! try to read the character style
  bool readCharStyle(ScriptWriterParserInternal::Page &page, int column);
  //! try to read  page
  bool readPage(ScriptWriterParserInternal::Page &page);
  //! try to read the main text zone
  bool readTextZone();
  //! try to read the hf style
  bool readHFStyle(std::map<int,MWAWFont> &posToFontMap);

  //! try to read a picture entry
  bool readPicture(MWAWEntry &entry);
  //! try to read a font
  bool readFont(MWAWFont &font);

  //
  // send data
  //

  //! try to send a header/footer
  bool send(ScriptWriterParserInternal::HFData const &hf);
  //! try to send a frame text
  bool sendText(ScriptWriterParserInternal::Frame const &frame);
  //! try to send the main text zone
  bool sendMainZone();
  //! try to send a page text
  bool sendText(ScriptWriterParserInternal::Page const &page);
protected:
  //
  // data
  //

  //! the state
  std::shared_ptr<ScriptWriterParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
