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

/*
 * Class to read/store the text font and paragraph styles
 */

#ifndef MS_WRD_TEXT_STYLES
#  define MS_WRD_TEXT_STYLES

#include <iostream>
#include <map>
#include <string>

#include "libmwaw_internal.hxx"

#include "MWAWEntry.hxx"
#include "MWAWParagraph.hxx"

class MsWrdParser;
class MsWrdText;

namespace MsWrdStruct
{
struct Font;
struct Paragraph;
struct Section;
}

namespace MsWrdTextStylesInternal
{
struct State;
}

/** \brief the main class to read/store the text font, paragraph, section stylesread */
class MsWrdTextStyles
{
  friend class MsWrdText;
public:
  enum ZoneType { TextZone, TextStructZone, StyleZone, InParagraphDefinition };
  //! constructor
  explicit MsWrdTextStyles(MsWrdText &textParser);
  //! destructor
  virtual ~MsWrdTextStyles();

  /** returns the file version */
  int version() const;

protected:
  // font:
  //! returns the default font
  MWAWFont const &getDefaultFont() const;
  //! return a font corresponding to an index
  bool getFont(ZoneType type, int id, MsWrdStruct::Font &actFont);

  /* send a font */
  void setProperty(MsWrdStruct::Font const &font);
  /** try to read a font.
      \param font : the read font,
      \param type : the zone in which the font is read */
  bool readFont(MsWrdStruct::Font &font, ZoneType type);

  // pararagraph:
  //! return a paragraph corresponding to an index
  bool getParagraph(ZoneType type, int id, MsWrdStruct::Paragraph &para);
  //! try to read a paragraph
  bool readParagraph(MsWrdStruct::Paragraph &para, int dataSz=-1);
  //! send a default paragraph
  void sendDefaultParagraph();

  //! read the main char/paragraph plc list
  bool readPLCList(MsWrdEntry &entry);
  //! read the paragraphs at the beginning of the text structure zone
  bool readTextStructList(MsWrdEntry &entry);
  /** read the property modifier (2 bytes last bytes of text struct ).
      Returns a textstruct parag id or -1 (PRM) */
  int readPropertyModifier(bool &complex, std::string &extra);
  //! read the char/paragraph plc : type=0: char, type=1: parag
  bool readPLC(MsWrdEntry &entry, int type, MWAWVec2<long> const &fileLimit);

  // section:
  //! return a section corresponding to an index
  bool getSection(ZoneType type, int id, MsWrdStruct::Section &section);
  //! return a paragraph corresponding to the section
  bool getSectionParagraph(ZoneType type, int id, MsWrdStruct::Paragraph &para);
  //! return a font corresponding to the section
  bool getSectionFont(ZoneType type, int id, MsWrdStruct::Font &font);
  //! read the text section
  bool readSection(MsWrdEntry &entry, std::vector<long> &cLimits);
  //! try to send a section
  bool sendSection(int id, int textStructId);

  //! try to read the section data
  bool readSection(MsWrdStruct::Section &section, long pos);
  //! send section properties
  void setProperty(MsWrdStruct::Section const &sec);

  // style
  //! try to read the styles zone
  bool readStyles(MsWrdEntry &entry);
  //! try to read the styles hierachy
  bool readStylesHierarchy(MsWrdEntry &entry, int N, std::vector<int> &orig);
  //! try to read the styles names and fill the number of "named" styles...
  bool readStylesNames(MsWrdEntry const &zone, int N, int &Nnamed);
  //! try to read the styles fonts
  bool readStylesFont(MsWrdEntry &zone, int N, std::vector<int> const &previous,
                      std::vector<int> const &order);
  //! try to read the styles fonts
  bool readStylesParagraph(MsWrdEntry &zone, int N, std::vector<int> const &previous,
                           std::vector<int> const &order);
  //! returns the style id to next style id map
  std::map<int,int> const &getNextStyleMap() const;
  //! try to reorder the styles to find a good order
  static std::vector<int> orderStyles(std::vector<int> const &previous);

private:
  MsWrdTextStyles(MsWrdTextStyles const &orig) = delete;
  MsWrdTextStyles &operator=(MsWrdTextStyles const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<MsWrdTextStylesInternal::State> m_state;

  //! the main parser;
  MsWrdParser *m_mainParser;

  //! the text parser;
  MsWrdText *m_textParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
