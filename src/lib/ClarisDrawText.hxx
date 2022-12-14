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
 * Parser to ClarisDraw text part
 *
 */
#ifndef CLARIS_DRAW_TEXT
#  define CLARIS_DRAW_TEXT

#include <list>
#include <string>
#include <vector>

#include "libmwaw_internal.hxx"

#include "MWAWEntry.hxx"
#include "MWAWSubDocument.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

#include "ClarisWksStruct.hxx"

namespace ClarisDrawTextInternal
{
struct Paragraph;
struct DSET;
struct State;
}

class ClarisDrawStyleManager;
class ClarisDrawParser;

/** \brief the main class to read the text part of ClarisDraw file
 *
 *
 *
 */
class ClarisDrawText
{
  friend class ClarisDrawParser;

public:
  //! constructor
  explicit ClarisDrawText(ClarisDrawParser &parser);
  //! destructor
  virtual ~ClarisDrawText();

  /** returns the file version */
  int version() const;
  /** resets the current state */
  void resetState();
  /** returns the number of pages */
  int numPages() const;

  //! reads the zone Text DSET
  std::shared_ptr<ClarisWksStruct::DSET> readDSETZone(ClarisWksStruct::DSET const &zone, MWAWEntry const &entry);
protected:
  //! sends the zone data
  bool sendZone(int number, int subZone=-1);
  //! sends the data which have not yet been sent to the listener
  void flushExtra();

  //
  // low level
  //

  //! try to read a font
  bool readFont(int id, int &posC, MWAWFont &font);

  /** read the rulers block which is present at the beginning of the text */
  bool readParagraphs();

  /** the definition of ruler :
      present at the beginning of the text in the first version of Claris Works : v1-2,
      present in the STYL entries in v4-v6 files */
  bool readParagraph(int id=-1);
  //! try to read the paragraph
  bool readParagraphs(MWAWEntry const &entry, ClarisDrawTextInternal::DSET &zone);

  //! try to read a font sequence
  bool readFonts(MWAWEntry const &entry, ClarisDrawTextInternal::DSET &zone);

  //! try to the token zone)
  bool readTokens(MWAWEntry const &entry, ClarisDrawTextInternal::DSET &zone);

  //! try to read the text zone size
  bool readTextZoneSize(MWAWEntry const &entry, ClarisDrawTextInternal::DSET &zone);

  //! send the text zone to the listener
  bool sendText(ClarisDrawTextInternal::DSET const &zone, int subZone);

private:
  ClarisDrawText(ClarisDrawText const &orig) = delete;
  ClarisDrawText &operator=(ClarisDrawText const &orig) = delete;

protected:
  //
  // data
  //

  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<ClarisDrawTextInternal::State> m_state;

  //! the main parser;
  ClarisDrawParser *m_mainParser;
  //! the style manager
  std::shared_ptr<ClarisDrawStyleManager> m_styleManager;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
