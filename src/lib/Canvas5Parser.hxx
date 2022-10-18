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

#ifndef CANVAS5_PARSER
#  define CANVAS5_PARSER

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

namespace Canvas5ParserInternal
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

/** \brief the main class to read a Canvas 5-10 files (and probably some not password protected Windows 11 files)
 *
 */
class Canvas5Parser final : public MWAWGraphicParser
{
  friend class Canvas5Graph;
  friend class Canvas5Image;
  friend class Canvas5StyleManager;
public:
  //! constructor
  Canvas5Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~Canvas5Parser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGDrawingInterface *documentInterface);

  //
  // interface
  //

  //! returns true if the file is a windows file
  bool isWindowsFile() const;
  //! returns the link corresponding to a text id
  librevenge::RVNGString getTextLink(int textLinkId) const;

protected:
  //! finds the different objects zones
  bool createZones();
  //! try to read the first big block
  bool readMainBlock(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the first big block: v9
  bool readMainBlock9(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the third big block: a list of resource?, font, ...
  bool readFileRSRCs(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the SI200 zone: v6
  bool readSI200(Canvas5Structure::Stream &stream);
  //! try to read the last block: some pathes, ...
  bool readFileDesc(Canvas5Structure::Stream &stream);

  // Intermediate level

  //
  // first block
  //

  //! try to read the file header
  bool readFileHeader(std::shared_ptr<Canvas5Structure::Stream> stream);

  //! read the document settings
  bool readDocumentSettings(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the different layers
  bool readLayers(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the list of slides
  bool readSlides(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the text links
  bool readTextLinks(std::shared_ptr<Canvas5Structure::Stream> stream);

  //
  // second block
  //

  //
  // third block
  //

  //! try to read a printer rsrc
  bool readPrinterRsrc(Canvas5Structure::Stream &stream);
  //! try to read the OLnk rsrc block: v6
  bool readOLnkRsrc(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the object database: XOBD v6
  bool readObjectDBRsrc(std::shared_ptr<Canvas5Structure::Stream> stream);

  //
  // Mac RSRC
  //
  //! read the RSRC 0 pnot zone
  bool readPnot(Canvas5Structure::Stream &stream, MWAWEntry const &entry);
  //! read the RSRC Pict zone
  bool readPicture(Canvas5Structure::Stream &stream, MWAWEntry const &entry);

  //
  // Windows RSRC
  //

  //
  // send data to the listener
  //

  //! try to send a page/slide
  bool send(Canvas5ParserInternal::Slide const &slide);
  //! try to send a layer
  bool send(Canvas5ParserInternal::Layer const &layer);

  //
  // low level
  //

  //! try to read a pascal string in the data fork or a Pascal/C string depending on the file type
  bool readString(Canvas5Structure::Stream &stream, librevenge::RVNGString &string, int maxSize, bool canBeCString=false);
  //! try to read a double 8
  bool readDouble(Canvas5Structure::Stream &stream, double &val, bool &isNaN) const;
  //! try to read a float: either a double: fieldSize=8 or a int32 (divided by 65536)
  double readDouble(Canvas5Structure::Stream &stream, int fieldSize) const;
  //! try to read a int: either a cast a double: fieldSize=8 or a int32/int16
  int readInteger(Canvas5Structure::Stream &stream, int fieldSize) const;

  //! a structure used to store the item data of a Canvas5Parser
  struct Item {
    //! constructor
    Item()
      : m_id(-1)
      , m_type(unsigned(-1))
      , m_length(0)
      , m_pos(-1)
      , m_decal(0)
    {
    }

    //! the identifier
    int m_id;
    //! the type (if known)
    unsigned m_type;
    //! the data length (from current position)
    long m_length;
    //! the data beginning position (may be before the current position: v9)
    long m_pos;
    //! the decal position
    int m_decal;
  };
  //! a function used to parse the data of a index map/a extended header
  typedef std::function<void(std::shared_ptr<Canvas5Structure::Stream>, Item const &, std::string const &)> DataFunction;
  //! the default function to parse the data of a index map/a extended header
  static void defDataFunction(std::shared_ptr<Canvas5Structure::Stream>, Item const &, std::string const &) {}
  //! the default function to parse a string
  static void stringDataFunction(std::shared_ptr<Canvas5Structure::Stream> stream, Item const &item, std::string const &what);

  //! try to read a data header, ie. N fields with a given size
  bool readDataHeader(Canvas5Structure::Stream &stream, int expectedSize, int &N);
  /** try to read an extended data header, ie. N0 is expected to be value

      \note the function func is called on each entry excepted the first one
   */
  bool readExtendedHeader(std::shared_ptr<Canvas5Structure::Stream> stream, int expectedValue, std::string const &what, DataFunction const &func);

  //! try to read the used list
  bool readUsed(Canvas5Structure::Stream &stream, std::string const &what);
  //! try to read the defined list
  bool readDefined(Canvas5Structure::Stream &stream, std::vector<bool> &defined, std::string const &what);

  //! try to read a index map
  bool readIndexMap(std::shared_ptr<Canvas5Structure::Stream> stream, std::string const &what, DataFunction const &func=&Canvas5Parser::defDataFunction);

  ////////////////////////////////////////////////////////////
  // v9 new structures
  ////////////////////////////////////////////////////////////

  //! try to read an array: v9
  bool readArray9(std::shared_ptr<Canvas5Structure::Stream> stream, std::string const &what,
                  DataFunction const &func=&Canvas5Parser::defDataFunction);
  //! try to read an array item header: v9
  bool readItemHeader9(Canvas5Structure::Stream &stream, int &id, int &used);

  //! try to return a tag, type:0 means begin, type:1 means end: v9
  bool getTAG9(Canvas5Structure::Stream &stream, std::string &tag, int &type);
  /** try to check is the following is a tag: v9 */
  bool checkTAG9(Canvas5Structure::Stream &stream, std::string const &tag, int type);

  //! try to decode the input stream
  static MWAWInputStreamPtr decode(MWAWInputStreamPtr input, int version);

  //
  // data
  //
  //! the state
  std::shared_ptr<Canvas5ParserInternal::State> m_state;
  //! the graph parser
  std::shared_ptr<Canvas5Graph> m_graphParser;
  //! the image parser
  std::shared_ptr<Canvas5Image> m_imageParser;
  //! the style manager
  std::shared_ptr<Canvas5StyleManager> m_styleManager;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
