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

#ifndef CORELPAINTER_PARSER
#  define CORELPAINTER_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

class MWAWPict;

namespace CorelPainterParserInternal
{
struct Node;
struct ZoneHeader;
struct State;

class SubDocument;
}

/** \brief the main class to read a Painter's file, actually, read
 *  Fractal Design Painter 1-4, MetaCreation v5-v6 and Corel Painter v7-v10 Mac files
 *  and Fractal Design Painter 3 Windows files.
 */
class CorelPainterParser final : public MWAWGraphicParser
{
public:
  friend class CorelPainterParserInternal::SubDocument;
  //! constructor
  CorelPainterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~CorelPainterParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  //! the main parser function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! inits all internal variables
  void init();

  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGDrawingInterface *documentInterface);
  //! try to send a bitmap zone (main bitmap or floater)
  bool sendBitmap(CorelPainterParserInternal::ZoneHeader const &zone);
  //! try to send a not bitmap zone
  bool sendZone(CorelPainterParserInternal::ZoneHeader const &zone);
protected:
  //! finds the different objects zones
  bool createZones();
  //! try to read the Hoffman tree
  std::shared_ptr<CorelPainterParserInternal::Node> readCompressionTree(long endPos, int numNodes);

  //! try to decompress a data
  bool decompressData(CorelPainterParserInternal::ZoneHeader const &zone, long endPos, int &value, int &buffer, int &numBitsInBuffer);
  // Intermediate level

  //! try to read the header zone
  bool readZoneHeader(CorelPainterParserInternal::ZoneHeader &zone);
  //! try to read a bitmap
  std::shared_ptr<MWAWPict> readBitmap(CorelPainterParserInternal::ZoneHeader const &zone);
  //! try to read a bitmap line
  bool readBitmapRow(CorelPainterParserInternal::ZoneHeader const &zone,
                     std::vector<MWAWColor> &colorList, std::vector<unsigned char> &previousValues);
  //! try to read the list of resource zone (in the data fork)
  bool readResourcesList(CorelPainterParserInternal::ZoneHeader &zone);
  //! try to parse the resource data
  bool readResource(MWAWEntry &entry);
  //! try to parse the text data
  bool sendText(MWAWEntry const &entry, MWAWEntry const &unicodeEntry);
  //! try to read a polygon data
  bool readPolygon(long endPos, MWAWGraphicShape &shape, MWAWGraphicStyle &style);

  //! update the position beforing sending a bitmap, shape, ...
  MWAWPosition getZonePosition(CorelPainterParserInternal::ZoneHeader const &zone) const;

  // low level
  //! try to read a double 1 bytes exponent, 3 mantisse
  bool readDouble(double &res);
  //
  // data
  //
  //! the state
  std::shared_ptr<CorelPainterParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
