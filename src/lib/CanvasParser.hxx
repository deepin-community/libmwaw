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

#ifndef CANVAS_PARSER
#  define CANVAS_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

#include "MWAWParser.hxx"

namespace CanvasParserInternal
{
struct Layer;
struct State;
}

class CanvasGraph;
class CanvasStyleManager;

/** \brief the main class to read a Canvas 2 and 3 files
 *
 */
class CanvasParser final : public MWAWGraphicParser
{
  friend class CanvasGraph;
  friend class CanvasStyleManager;
public:
  //! constructor
  CanvasParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~CanvasParser() final;

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

protected:
  //! finds the different objects zones
  bool createZones();
  //! returns the current input
  MWAWInputStreamPtr &getInput();


  // Intermediate level

  //! read the file header: list of unknown numbers
  bool readFileHeader();
  //! read the document header
  bool readDocumentHeader();
  //! try to read the brush
  bool readBrushes();
  //! read the grid: or a list which begins by a grid
  bool readGrids();
  //! try to read the layers
  bool readLayers();
  //! read the macro names
  bool readMacroNames();
  //! read the formats' zone, mainly an unit's conversion table
  bool readFormats();
  //! try to read the spray
  bool readSprays();
  //! try to read the views
  bool readViews();

  //
  // last v3 zones
  //

  //! try to read the end zone: v3
  bool readEndV3();

  //
  // Mac RSRC
  //

  //! read the RSRC HeAd(a copy of file header) zone: v3
  bool readRSRCFileHeader(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile);
  //! read the print info zone
  bool readPrintInfo(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile);
  //! read the RSRC LPol zone: v3
  bool readLPOL(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile);
  //! read the RSRC user zone: v3
  bool readUsers(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile);
  //! read the RSRC Windows zone: v3
  bool readWindows(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile);

  //
  // Windows RSRC
  //

  //! read the resource file which ends the file
  bool readRSRCWindowsFile();
  //! read the Windows CNam RSRC: v3 (always 256 0's bytes)
  bool readCNam(MWAWEntry const &entry);
  //! read the Windows CSet RSRC: v3 (contains the string Default)
  bool readCSet(MWAWEntry const &entry);
  //! read the Windows DevM RSRC: v3 (main data of the printer device)
  bool readPrinterDev(MWAWEntry const &entry);
  //! read the Windows Page RSRC: v3
  bool readPage(MWAWEntry const &entry);
  //! read the Windows PSST RSRC: v3 (the printer, device name, ...)
  bool readPrinterSST(MWAWEntry const &entry);

  //! read the first unknown zone
  bool readUnknownZoneHeader();
  //! read an unknown zone: contains a layer name, some font id,sz, ...
  bool readUnknownZone0();
  //! read an unknown zone
  bool readUnknownZone1();
  //! read an unknown zone
  bool readUnknownZone2();
  //! read an unknown zone
  bool readUnknownZone3();
  //! read an unknown zone: the last zone of a v2 files
  bool readUnknownZone4();

  //
  // send data to the listener
  //

  //! tries to send a layer
  bool send(CanvasParserInternal::Layer const &layer);

  //
  // low level
  //

  //! try to read a pascal string in the data fork or a Pascal/C string depending on the file type
  bool readString(librevenge::RVNGString &string, int maxSize, bool canBeCString=false);
  //! try to read a pascal string in the data fork or the resource fork
  bool readString(MWAWInputStreamPtr input, librevenge::RVNGString &string, int maxSize, bool canBeCString=false);

  //! try to decode some data: length==-1 means decode end of input
  bool decode(long length);

  //
  // data
  //
  //! the state
  std::shared_ptr<CanvasParserInternal::State> m_state;
  //! the graph parser
  std::shared_ptr<CanvasGraph> m_graphParser;
  //! the style manager
  std::shared_ptr<CanvasStyleManager> m_styleManager;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
