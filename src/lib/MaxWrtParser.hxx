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

#ifndef MAXWRT_PARSER
#  define MAXWRT_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace MaxWrtParserInternal
{
struct State;
}

/** \brief the main class to read a MaxWrite file
 */
class MaxWrtParser final : public MWAWTextParser
{
public:
  //! constructor
  MaxWrtParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~MaxWrtParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:

  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

protected:
  //! finds the different objects zones
  bool createZones();
  /** try to send the main text*/
  bool sendText();

  // Intermediate level

  //! try to read a line height zone
  bool readLineHeight(MWAWEntry const &entry);
  //! try to read a style plc zone: Styl
  bool readStylePLC(MWAWEntry const &entry);
  //! try to read a styles zone: StTB
  bool readStyles(MWAWEntry const &entry);

  //! return the input input
  MWAWInputStreamPtr rsrcInput();

  //! a DebugFile used to write what we recognize when we parse the document in rsrc
  libmwaw::DebugFile &rsrcAscii();

  //
  // data
  //
  //! the state
  std::shared_ptr<MaxWrtParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
