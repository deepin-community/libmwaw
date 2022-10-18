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

#ifndef READYSETGO_PARSER
#  define READYSETGO_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace ReadySetGoParserInternal
{
struct Layout;
struct Shape;
struct State;

class SubDocument;
}

struct MWAWTabStop;

/** \brief the main class to read a ReadySetGo 1.0,2.1,3.0,4.0,4.5 file
 *
 */
class ReadySetGoParser final : public MWAWGraphicParser
{
  friend class ReadySetGoParserInternal::SubDocument;
public:
  //! constructor
  ReadySetGoParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~ReadySetGoParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGDrawingInterface *documentInterface);
  //! try to send a shape
  bool send(ReadySetGoParserInternal::Shape const &shape);
  //! try to send the text corresponding to a shape
  bool sendText(ReadySetGoParserInternal::Shape const &shape);

  //! try to update the textboxes link: v3
  bool updateTextBoxLinks();

protected:
  //! finds the different objects zones
  bool createZones();
  //! try to read the document header: v3
  bool readDocument();
  //! try to read the layout list: v3
  bool readLayoutsList();
  //! try to read an unknown list of IDs: v3
  bool readIdsList();
  //! try to read the list of style block: v4
  bool readStyles();
  //! try to read the glossary list: v4
  bool readGlossary();
  //! try to read the list of font block: unsure, name + data?, v5
  bool readFontsBlock();

  //! try to read a shape: v1
  bool readShapeV1();
  //! try to read a shape: v2
  bool readShapeV2(ReadySetGoParserInternal::Layout &layout);
  //! try to read a shape: v3
  bool readShapeV3(ReadySetGoParserInternal::Layout &layout, bool &last);
  //! try to read a style: v3
  bool readStyle(MWAWFont &font, MWAWParagraph &para, int *cPos=nullptr);
  //! try to read a list of tabulations: v1-2
  bool readTabulationsV1(std::vector<MWAWTabStop> &tabulations, std::string &extra);
  //! try to read a list of tabulations: v3
  bool readTabulations(std::vector<MWAWTabStop> &tabs, long len=-1, int *cPos=nullptr);

  // Intermediate level

  //! try to read the print info zone
  bool readPrintInfo();

  //
  // low level
  //

  //
  // data
  //
  //! the state
  std::shared_ptr<ReadySetGoParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
