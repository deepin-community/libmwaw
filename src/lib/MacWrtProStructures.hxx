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

#ifndef MAC_WRT_PRO_STRUCTURES
#  define MAC_WRT_PRO_STRUCTURES

#include <string>
#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"
#include "MWAWStream.hxx"

class MacWrtProParser;

namespace MacWrtProParserInternal
{
class SubDocument;
}

namespace MacWrtProStructuresInternal
{
struct Graphic;
struct Page;
struct Cell;
struct Font;
struct Paragraph;
struct Section;
struct State;
class SubDocument;
}

class MacWrtProStructures;

/** \brief an interface to transmit the info of MacWrtProStructures to a listener
 */
class MacWrtProStructuresListenerState
{
public:
  //! the constructor
  MacWrtProStructuresListenerState(std::shared_ptr<MacWrtProStructures> const &structures, bool mainZone, int version);
  //! the destructor
  ~MacWrtProStructuresListenerState();

  //! returns true if the graphic is already sent ( or does not exists)
  bool isSent(int graphicId);
  //! try to send a graphic which corresponds to graphicid
  bool send(int graphicId);

  //! try to send the i^th section
  void sendSection(int numSection);
  //! try to send a character style
  bool sendFont(int id);
  //! try to send a paragraph
  bool sendParagraph(int id);
  //! send a character
  void sendChar(char c);

  //! returns the actual section
  int numSection() const
  {
    if (!m_isMainZone) {
      MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::numSection: not called in main zone\n"));
      return 0;
    }
    return m_section;
  }

  //! return a list of page break position ( as some page break are soft )
  std::vector<int> getPageBreaksPos() const;
  //! insert a page break ( if we are not on a new page )
  void insertSoftPageBreak();

protected:
  //! create a new page
  bool newPage(bool softBreak=false);

  // true if this is the mainZone
  bool m_isMainZone;
  // the file version
  int m_version;
  // the actual page
  int m_actPage;
  // the actual tab
  int m_actTab;
  // the number of tab
  int m_numTab;
  // the actual section ( if mainZone )
  int m_section;
  // the actual number of columns
  int m_numCols;
  // a flag to know if a new page has just been open
  bool m_newPageDone;
  // the main structure parser
  std::shared_ptr<MacWrtProStructures> m_structures;
};

/** \brief the main class to read the structures part of MacWrite Pro file
 *
 *
 *
 */
class MacWrtProStructures
{
  friend class MacWrtProParser;
  friend class MacWrtProParserInternal::SubDocument;
  friend struct MacWrtProStructuresInternal::Cell;
  friend class MacWrtProStructuresListenerState;
public:
  //! constructor
  explicit MacWrtProStructures(MacWrtProParser &mainParser);
  //! destructor
  virtual ~MacWrtProStructures();

  /** returns the file version.
   *
   * this version is only correct after the header is parsed */
  int version() const;

protected:
  //! inits all internal variables
  void init();

  //! finds the different objects zones
  bool createZones(std::shared_ptr<MWAWStream> &stream, int numPages);

  /** finds the different objects zones in a MacWriteII file

  \note: this function is called by createZones
   */
  bool createZonesII(std::shared_ptr<MWAWStream> &stream, int numPages);

  //! returns the number of pages
  int numPages() const;

  //! send the main zone
  bool sendMainZone();

  //! update the page span
  void updatePageSpan(int page, bool hasTitlePage, MWAWPageSpan &pageSpan);

  //! flush not send zones
  void flushExtra();

  //! look for pages structures
  void buildPageStructures();

  //! look for tables structures and if so, prepare data
  void buildTableStructures();

  //
  // low level
  //

  //! try to read the paragraph styles zone which begins at address 0x200
  bool readStyles(std::shared_ptr<MWAWStream> &stream);

  //! try to read a style
  bool readStyle(std::shared_ptr<MWAWStream> &stream, int styleId);

  //! try to read the character styles zone
  bool readCharStyles(std::shared_ptr<MWAWStream> &stream);

  //! try to read a list of paragraph
  bool readParagraphs(std::shared_ptr<MWAWStream> &stream);

  //! try to read a paragraph
  bool readParagraph(std::shared_ptr<MWAWStream> &stream, MacWrtProStructuresInternal::Paragraph &para);

  //! try to read the list of graphic entries: 1.0 1.5
  bool readGraphicsList(std::shared_ptr<MWAWStream> &stream, int nuumPages);
  //! try to read a graphic entry: 1.0, 1.5
  std::shared_ptr<MacWrtProStructuresInternal::Graphic> readGraphic(std::shared_ptr<MWAWStream> &stream);

  //! try to parse the list of page: II
  bool readPagesListII(std::shared_ptr<MWAWStream> const &stream, int numPages);
  //! try to read a page entry: II
  bool readPageII(std::shared_ptr<MWAWStream> const &stream, int wh, MacWrtProStructuresInternal::Page &page);
  //! try to read a graphic structure: II
  bool readGraphicII(std::shared_ptr<MWAWStream> const &stream, int id, bool mainGraphic, MacWrtProStructuresInternal::Graphic &graphic);

  //! try to read the fonts zone
  bool readFontsName(std::shared_ptr<MWAWStream> &stream);

  //! try to read the list of fonts
  bool readFontsDef(std::shared_ptr<MWAWStream> &stream);

  //! try to read a font
  bool readFont(std::shared_ptr<MWAWStream> &stream, MacWrtProStructuresInternal::Font &font);

  //! try to read the section info ?
  bool readSections(std::shared_ptr<MWAWStream> &stream, std::vector<MacWrtProStructuresInternal::Section> &sections);

  //! try to read a 16 bytes the zone which follow the char styles zone ( the selection?)
  bool readSelection(std::shared_ptr<MWAWStream> &stream);

  //! try to read a zone which follow the fonts zone(checkme)
  bool readStructB(std::shared_ptr<MWAWStream> &stream);

  //! try to read a string
  static bool readString(MWAWInputStreamPtr input, std::string &res);

  //! try to return the color corresponding to colId
  bool getColor(int colId, MWAWColor &color) const;

  //! try to return the pattern corresponding to patId
  bool getPattern(int patId, float &patternPercent) const;

  //! try to return the color corresponding to colId and patId
  bool getColor(int colId, int patId, MWAWColor &color) const;

  //! returns true if the graphic is already sent ( or does not exists)
  bool isSent(int graphicId);

  /** try to send a graphic which corresponds to graphicid

      note: graphicId=-noteId to send footnote in MW2
   */
  bool send(int graphicId, bool mainZone=false);

  //! returns the actual listener
  MWAWTextListenerPtr &getTextListener();

protected:
  //
  // data
  //

  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the main parser
  MacWrtProParser &m_mainParser;

  //! the state
  std::shared_ptr<MacWrtProStructuresInternal::State> m_state;

  //! the debug file name
  std::string m_asciiName;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
