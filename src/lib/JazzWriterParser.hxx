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

#ifndef JAZZ_WRITER_PARSER
#  define JAZZ_WRITER_PARSER

#include <set>
#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace JazzWriterParserInternal
{
class SubDocument;
struct State;
}

/** \brief the main class to read a Jazz(Lotus) word file
 */
class JazzWriterParser final : public MWAWTextParser
{
  friend class JazzWriterParserInternal::SubDocument;
public:
  //! constructor
  JazzWriterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~JazzWriterParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! tries to read the WPPD zones
  bool readParagraph(MWAWEntry const &entry);
  //! tries to read the WSCR zones
  bool readPLC(MWAWEntry const &entry);
  //! tries to read the WDoc zones
  bool readZone(MWAWEntry const &entry);

  //! try to send a zone
  bool sendZone(unsigned zId);
  //! try to send a paragraph
  bool sendParagraph(unsigned pId);
  //! try to send a plc
  bool sendPLC(unsigned pId);
  //! try to retrieve a picture
  bool getPicture(unsigned pId, MWAWEmbeddedObject &obj);

protected:
  //! finds the different objects zones
  bool createZones();
  /** checks that the main zone exists (and its potential header/footer) and that there are valid.

      Enumerates also the number of characters in each zones and check that the data fork contains enough characters.
   */
  bool checkZones();
  //! check that a paragraph, its style and its followings exist (and do not create loops) and update the number of characters
  bool checkParagraphs(unsigned id, long &num, std::set<unsigned> &seens) const;
  //! retrieve the number of characters corresponding to a PLC
  bool countCharactersInPLC(unsigned plcId, long &n);

  //! return the input input
  MWAWInputStreamPtr rsrcInput();
  //! a DebugFile used to write what we recognize when we parse the document in rsrc
  libmwaw::DebugFile &rsrcAscii();

  //
  // data
  //

  //! try to read a C-string
  bool readString(MWAWInputStreamPtr input, librevenge::RVNGString &string, long endPos);

  //! the state
  std::shared_ptr<JazzWriterParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
