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
 * Parser to Canvas text document ( style part )
 *
 */
#ifndef CANVAS_STYLE_MANAGER
#  define CANVAS_STYLE_MANAGER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

namespace CanvasStyleManagerInternal
{
struct State;
}

class CanvasGraph;
class CanvasParser;

/** \brief the main class to read the style part of Canvas file
 *
 *
 *
 */
class CanvasStyleManager
{
  friend class CanvasGraph;
  friend class CanvasParser;

public:
  //! constructor
  explicit CanvasStyleManager(CanvasParser &parser);
  //! destructor
  virtual ~CanvasStyleManager();

  /** returns the file version */
  int version() const;

protected:
  /** store the actual input */
  void setInput(MWAWInputStreamPtr &input);
  /** returns the current input */
  MWAWInputStreamPtr &getInput();

  // interface

  //! try to retrieve a color from the color index
  bool get(int index, MWAWColor &color) const;
  //! returns the list of colors
  std::vector<MWAWColor> const &getColorsList() const;
  //! try to retrieve a pattern from the pattern index
  bool get(int index, MWAWGraphicStyle::Pattern &pattern) const;

  //
  // Intermediate level
  //

  //! read the arrow shapes
  bool readArrows();
  //! read an arrow shape
  bool readArrow(MWAWGraphicStyle::Arrow &arrow, std::string &extra);
  //! try to read the colors list
  bool readColors(int numColors);
  //! try to read the dash list
  bool readDashes(int numDashes, bool user=false);
  //! try to read a gradient (ObFL)
  bool readGradient(MWAWEntry const &entry, MWAWGraphicStyle::Gradient &gradient);
  //! try to read the pattern list
  bool readPatterns(int numPatterns);
  //! read the pen size
  bool readPenSize();
  //! try to read the spray
  bool readSprays();

  //! try to read the fonts names
  bool readFonts(int numFonts);

  //
  // Windows RSRC
  //

  //! read the Windows CVal RSRC: v3 (a list of color)
  bool readColorValues(MWAWEntry const &entry);

  //
  // Low level
  //

private:
  CanvasStyleManager(CanvasStyleManager const &orig) = delete;
  CanvasStyleManager &operator=(CanvasStyleManager const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<CanvasStyleManagerInternal::State> m_state;

  //! the main parser;
  CanvasParser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
