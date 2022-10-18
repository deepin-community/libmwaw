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
 * Parser to PowerPoint 95 document ( text part )
 *
 */
#ifndef POWER_POINT7_TEXT
#  define POWER_POINT7_TEXT

#include <set>
#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

namespace PowerPoint7TextInternal
{
struct Ruler;
struct TextZone;

struct State;
}

class PowerPoint7Parser;

/** \brief the main class to read the text part of a PowerPoint 95 file
 *
 *
 *
 */
class PowerPoint7Text
{
  friend class PowerPoint7Parser;

public:
  //! constructor
  explicit PowerPoint7Text(PowerPoint7Parser &parser);
  //! destructor
  virtual ~PowerPoint7Text();

  /** returns the file version */
  int version() const;
  /** sets the default font family */
  void setFontFamily(std::string const &family);
  //! try to send the text content
  bool sendText(int textId);

protected:
  //! try to read the font collection zone 2005
  bool readFontCollection(int level, long endPos);
  //! try to read the field definition list zone 2027
  bool readFieldList(int level, long endPos);
  //! try to read a text zone container, zone 2028
  bool readTextZoneContainer(int level, long endPos, PowerPoint7TextInternal::TextZone &zone);
  //! try to read a text zone 2030
  bool readTextZone(int level, long endPos, PowerPoint7TextInternal::TextZone &zone);

  //! try to read the master text prop atom zone 4002
  bool readTextMasterProp(int level, long endPos, int &textId);
  //! try to read the text master prop atom zone 4003
  bool readTextMasterPropAtom(int level, long endPos);
  //! try to read the list of rulers zone 4016
  bool readRulerList(int level, long endPos);
  //! try to read the ruler margins zone 4019
  bool readRuler(int level, long endPos, PowerPoint7TextInternal::Ruler &ruler);
  //! try to read the ruler set id zone 4021
  bool readRulerSetId(int level, long endPos, int &id);
  //! try to read the font container zone 4022
  bool readFontContainer(int level, long endPos, std::string &fName);
  //! try to read the font entity atom zone 4023
  bool readFont(int level, long endPos, std::string &fName);
  //! try to read an embedded font container zone 4024
  bool readFontEmbedded(int level, long endPos);
  //! try to read the external hyper link atom zone: 4051
  bool readExternalHyperlinkAtom(int level, long endPos);
  //! try to read the external hyper link atom zone: 4055
  bool readExternalHyperlinkData(int level, long endPos);
  //! try to read the field definition zone 4056
  bool readFieldDef(int level, long endPos, int &format);
  //! try to read the zone 4064: child of MasterTextPropAtom,ExternalHyperlink9
  bool readZone4064(int level, long endPos,int rId,int &textId);
  //! try to read the zone 4066: child of 4064
  bool readZone4066(int level, long endPos);
  //! try to read the zone 4067: child of 4064
  bool readZone4067(int level, long endPos);
  //! try to read the external hyper link zone: 4068
  bool readExternalHyperlink9(int level, long endPos, int &tId);
  //! try to read the ruler container zone: 4069
  bool readRulerContainer(int level, long endPos, PowerPoint7TextInternal::Ruler &ruler);
  //! try to read the tab list zone: 4070
  bool readRulerTabs(int level, long endPos, PowerPoint7TextInternal::Ruler &ruler);

  //
  // Intermediate level
  //

  //
  // low level
  //

private:
  PowerPoint7Text(PowerPoint7Text const &orig) = delete;
  PowerPoint7Text &operator=(PowerPoint7Text const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<PowerPoint7TextInternal::State> m_state;

  //! the main parser;
  PowerPoint7Parser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
