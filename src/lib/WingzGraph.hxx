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
 * Parser to Claris Resolve/Wingz document ( graphic part )
 *
 */
#ifndef WINGZ_GRAPH
#  define WINGZ_GRAPH

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"

namespace WingzGraphInternal
{
struct Graphic;
struct State;
class SubDocument;
}

class WingzParser;

/** \brief the main class to read the graphic part of a Claris Resolve/Wingz file
 *
 *
 *
 */
class WingzGraph
{
  friend class WingzParser;
  friend class WingzGraphInternal::SubDocument;

public:
  //! constructor
  explicit WingzGraph(WingzParser &parser);
  //! destructor
  virtual ~WingzGraph();

  /** returns the file version */
  int version() const;

protected:
  //! try to send the page graphic
  bool sendPageGraphics();

  //
  // Intermediate level
  //

  //! read a graphic zone: 0xe
  bool readGraphic();
  //! read a end group zone: 0xf
  bool readEndGroup();
  //! read a text zone or a button zone ( some graphic zone)
  bool readTextZone(std::shared_ptr<WingzGraphInternal::Graphic> graphic);

  //! read a chart
  bool readChartData(std::shared_ptr<WingzGraphInternal::Graphic> graphic);
  //! read a pattern
  bool readPattern(MWAWGraphicStyle::Pattern &pattern, int &patId);
  //! read a color: front color, patId, background color
  bool readColor(MWAWColor &color, int &patId);

  //
  // send data
  //

  //! try to send a generic graphic
  bool sendGraphic(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos);
  //! try to send a shape graphic
  bool sendShape(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos);
  //! try to send a picture graphic
  bool sendPicture(WingzGraphInternal::Graphic const &graphic, MWAWPosition const &pos);
  //! try to send the content of a textbox/button
  bool sendText(WingzGraphInternal::Graphic const &graphic);
private:
  WingzGraph(WingzGraph const &orig) = delete;
  WingzGraph &operator=(WingzGraph const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<WingzGraphInternal::State> m_state;

  //! the main parser;
  WingzParser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
