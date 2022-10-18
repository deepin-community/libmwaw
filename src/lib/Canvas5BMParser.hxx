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

#ifndef CANVAS5_BM_PARSER
#  define CANVAS5_BM_PARSER

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

#include "MWAWParser.hxx"

namespace Canvas5BMParserInternal
{
struct Layer;
struct Slide;

struct State;
}

class Canvas5Graph;
class Canvas5Image;
class Canvas5StyleManager;

namespace Canvas5Structure
{
struct Stream;
}

/** \brief the main class to read a mac Canvas 5/6 bitmap file: .cvi
 *
 */
class Canvas5BMParser final : public MWAWGraphicParser
{
  friend class Canvas5Graph;
  friend class Canvas5Image;
  friend class Canvas5StyleManager;
public:
  //! constructor
  Canvas5BMParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~Canvas5BMParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  bool createDocument(librevenge::RVNGDrawingInterface *documentInterface);

  //
  // interface
  //

  //! returns true if the file is a windows file
  bool isWindowsFile() const;

protected:
  //! finds the different objects zones
  bool createZones();

  // Intermediate level

  //! try to read the file header
  bool readFileHeader(Canvas5Structure::Stream &stream);

  //
  // send data to the listener
  //


  //
  // low level
  //

  //
  // data
  //
  //! the state
  std::shared_ptr<Canvas5BMParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
