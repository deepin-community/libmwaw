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

#ifndef SCOOP_PARSER
#  define SCOOP_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace ScoopParserInternal
{
struct FrameZone;
struct Paragraph;
struct Shape;
struct TextZone;

struct State;

class SubDocument;
}

/** \brief the main class to read a Scoop v1 file
 *
 */
class ScoopParser final : public MWAWGraphicParser
{
  friend class ScoopParserInternal::SubDocument;
public:
  //! constructor
  ScoopParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~ScoopParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGDrawingInterface *documentInterface);

protected:
  //! finds the different objects zones
  bool createZones();

  // Intermediate level

  //! try to read the print info zone
  bool readPrintInfo();
  //! try to the header zone
  bool readHeader();

  /** try to read the data associated to a text zone: its name, its
      content, the list of shape which displayed this text */
  bool readTextZone(ScoopParserInternal::TextZone &tZone);
  //! try to read a frame paragraph
  bool readTextZoneParagraph(ScoopParserInternal::Paragraph &para, int id);

  //! try to read a font style
  bool readFont(MWAWFont &font);
  //! try to read a paragraph style
  bool readParagraph(MWAWParagraph &para, bool define=false);
  //! try to read a text zone
  bool readText(MWAWEntry &entry, std::string const &what);

  //! try to read a list of shape
  bool readShapesList(std::vector<ScoopParserInternal::Shape> &shapes);
  //! try to read a shape
  bool readShape(ScoopParserInternal::Shape &shape, int id);
  //! try to read a bitmap
  bool readBitmap(MWAWEntry const &entry, MWAWEmbeddedObject &object, bool compressed=true);

  //
  // low level
  //

  //
  // data
  //

  //! try to send a shape
  bool send(ScoopParserInternal::Shape const &shape, MWAWVec2i const &decal);
  //! try to send the text of a text zone
  bool sendText(long tZoneId, int subZone);
  //! try to send the text corresponding to a paragraph
  bool sendText(ScoopParserInternal::Paragraph const &paragraph);

  //! the state
  std::shared_ptr<ScoopParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
