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


#ifndef MAC_WRT_PRO_PARSER
#  define MAC_WRT_PRO_PARSER

#include <list>
#include <string>
#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace MacWrtProParserInternal
{
struct State;
struct TextZoneData;
struct TextZone;
struct Token;
struct Zone;
class SubDocument;
}

class MacWrtProStructures;
class MacWrtProStructuresListenerState;

/** \brief the main class to read a MacWrite II and MacWrite Pro file
 *
 *
 *
 */
class MacWrtProParser final : public MWAWTextParser
{
  friend class MacWrtProStructures;
  friend class MacWrtProStructuresListenerState;
  friend class MacWrtProParserInternal::SubDocument;

public:
  //! constructor
  MacWrtProParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~MacWrtProParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! inits all internal variables
  void init();

  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! returns a stream corresponding to a block
  std::shared_ptr<MWAWStream> getStreamForBlock(int bId);

  //! return the chain list of block ( used to get free blocks): MWPro only
  bool getFreeZoneList(std::vector<int> &blockLists);

  /** parse a data zone

  \note type=0 ( text entry), type = 1 ( graphic entry ), other unknown
  */
  bool parseDataZone(int blockId, int type);

  /** parse a text zone */
  bool parseTextZone(std::shared_ptr<MacWrtProParserInternal::Zone> zone);

  /** try to read the text block entries */
  bool readTextEntries(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                       std::vector<MWAWEntry> &res, int textLength);
  /** try to read the text id entries */
  bool readTextIds(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                   std::vector<MacWrtProParserInternal::TextZoneData> &res,
                   int textLength, int type);
  /** try to read the text token entries */
  bool readTextTokens(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                      std::vector<MacWrtProParserInternal::Token> &res,
                      int textLength);

  /** return the list of graphicid called by token. A hack to help
      structures to retrieve the page attachment */
  std::vector<int> const &getGraphicIdCalledByToken() const;

  //! returns the page height, ie. paper size less margin (in inches)
  float pageHeight() const;
  //! returns the document number of columns and the column separator width in inch( filed in MWII)
  int numColumns(double &sep) const;

  //! adds a new page
  void newPage(int number, bool softBreak=false);
  //! returns true if the document has a title page
  bool hasTitlePage() const;
  //! returns a subdocument corresponding to a blockid
  std::shared_ptr<MWAWSubDocument> getSubDocument(int blockId);

  //
  // interface with MacWrtProParserStructures
  //

  //! send a text box
  bool sendTextZone(int blockId, bool mainZone = false);

  //! compute the number of hard page break
  int findNumHardBreaks(int blockId);

  //! try to send a picture
  bool sendPictureZone(int blockId, MWAWPosition const &pictPos, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());

  //! send a textbox zone
  bool sendTextBoxZone(int blockId, MWAWPosition const &pos, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());
  //! try to send an empty zone (can exist in MacWrtPro1.5)
  bool sendEmptyFrameZone(MWAWPosition const &pos, MWAWGraphicStyle const &style);

  //
  // low level
  //

  //! read the print info zone
  bool readPrintInfo();

  //! try to read the doc header zone
  bool readDocHeader();

  //! try to send a picture
  bool sendPicture(std::shared_ptr<MacWrtProParserInternal::Zone> zone, MWAWPosition pictPos, MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle());

  //! try to send a text
  bool sendText(std::shared_ptr<MacWrtProParserInternal::TextZone> zone, bool mainZone = false);

  //! compute the number of hard page break
  int findNumHardBreaks(std::shared_ptr<MacWrtProParserInternal::TextZone> zone);

  //! convert a date in a string (for debugging)
  static std::string convertDateToDebugString(unsigned date);
  //! a debug function which can be used to save the unparsed block
  void checkUnparsed();

protected:
  //
  // data
  //
  //! the state
  std::shared_ptr<MacWrtProParserInternal::State> m_state;

  //! the structures parser
  std::shared_ptr<MacWrtProStructures> m_structures;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
