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

#ifndef ACTA_PARSER
#  define ACTA_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace ActaParserInternal
{
class SubDocument;
struct State;
}

class ActaText;

/** \brief the main class to read a Acta file
 */
class ActaParser final : public MWAWTextParser
{
  friend class ActaParserInternal::SubDocument;
  friend class ActaText;
public:
  //! constructor
  ActaParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~ActaParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! inits all internal variables
  void init();

  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! returns the page left top point ( in inches)
  MWAWVec2f getPageLeftTop() const;
  //! adds a new page
  void newPage(int number);

  // interface with the text parser

  //! returns a list to be used in the text
  std::shared_ptr<MWAWList> getMainList();

protected:
  //! finds the different objects zones
  bool createZones();

  //! read the resource fork zone
  bool readRSRCZones();

  //! read the end file data zones
  bool readEndDataV3();

  //! sends the header/footer data
  void sendHeaderFooter();

  //! read a PrintInfo block ( PSET resource block )
  bool readPrintInfo(MWAWEntry const &entry);

  //! read the window position ( WSIZ resource block )
  bool readWindowPos(MWAWEntry const &entry);

  //! read the label type
  bool readLabel(MWAWEntry const &entry);

  //! read the Header/Footer property (QHDR block)
  bool readHFProperties(MWAWEntry const &entry);

  //! read the QOPT resource block (small print change )
  bool readOption(MWAWEntry const &entry);

  //
  // low level
  //

  //! return the input input
  MWAWInputStreamPtr rsrcInput();

  //! a DebugFile used to write what we recognize when we parse the document in rsrc
  libmwaw::DebugFile &rsrcAscii();

  //
  // data
  //

  //! the state
  std::shared_ptr<ActaParserInternal::State> m_state;

  //! the text parser
  std::shared_ptr<ActaText> m_textParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
