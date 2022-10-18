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

#ifndef STYLE_PARSER
#  define STYLE_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace StyleParserInternal
{
struct Picture;
struct State;
}

/** \brief the main class to read a Style file
 */
class StyleParser final : public MWAWTextParser
{
public:
  //! constructor
  StyleParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~StyleParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! adds a new page
  void newPage(int number);

protected:
  //! finds the different objects zones
  bool createZones();
  //! tries to read the main entry map
  bool readTypeEntryMap();
  //! tries to read the background color: bgcl
  bool readBackgroundColor(MWAWEntry const &entry);
  //! tries to read the format: cfor (v1.9)
  bool readFormats(MWAWEntry const &entry);
  //! tries to read the extra property zone: xprc
  bool readExtraProperties(MWAWEntry const &entry);
  //! tries to read the font table: fntb (v1.6)
  bool readFontNames(MWAWEntry const &entry);
  /** tries to read the font correspondance zone: font, contains the
      font id, the position in fntb and many unknown datas (v1.6).*/
  bool readFontCorr(MWAWEntry const &entry);
  //! tries to read the style zone: styl
  bool readStyleTable(MWAWEntry const &entry);
  //! tries to read the margins zone: marg
  bool readMargins(MWAWEntry const &entry);
  //! tries to read the printer info zone: prec
  bool readPrintInfo(MWAWEntry const &entry);
  //! tries to read the plc zone: runa or para
  bool readPLCs(MWAWEntry const &entry, bool para);
  //! tries to read the pictures zone: soup
  bool readPictures(MWAWEntry const &entry);
  //! tries to read the rule zone: rule
  bool readRules(MWAWEntry const &entry);
  //! tries to read the stat zone: stat
  bool readStat(MWAWEntry const &entry);
  //! tries to read the tab width zone : tabw
  bool readTabWidth(MWAWEntry const &entry);
  //! tries to read the version zone: vers
  bool readVersion(MWAWEntry const &entry);

  /** compute the number of page of a zone*/
  int computeNumPages(MWAWEntry const &entry, bool unicodeChar) const;

  /** try to send the main text*/
  bool sendText(MWAWEntry const &entry, bool unicodeChar);
  //! try to send a picture knowing the char position
  bool sendPicture(StyleParserInternal::Picture const &pict);

  //
  // data
  //
  //! the state
  std::shared_ptr<StyleParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
