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
 * Parser to Canvas text document ( graphic part )
 *
 */
#ifndef CANVAS_GRAPH
#  define CANVAS_GRAPH

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

namespace CanvasGraphInternal
{
struct LocalTransform;
struct Shape;
struct State;

class SubDocument;
}

class CanvasParser;
class CanvasStyleManager;

/** \brief the main class to read the graphic part of Canvas file
 *
 *
 *
 */
class CanvasGraph
{
  friend class CanvasGraphInternal::SubDocument;
  friend class CanvasParser;

public:
  //! constructor
  explicit CanvasGraph(CanvasParser &parser);
  //! destructor
  virtual ~CanvasGraph();

  /** returns the file version */
  int version() const;

protected:
  /** store the actual input */
  void setInput(MWAWInputStreamPtr &input);
  /** returns the current input */
  MWAWInputStreamPtr &getInput();

  // interface with main parser

  //! tries to send a shape with id
  bool sendShape(int id);

  //
  // Intermediate level
  //

  //! try to read the shapes: in fact, the compression header + the list of shapes
  bool readShapes(int numShapes, unsigned long shapeLength, unsigned long dataLength);
  //! try to read a shape: to do
  bool readShape(int n, std::vector<MWAWEntry> const &dataZonesList);
  //! try to read the shapes data
  bool readShapeData(CanvasGraphInternal::Shape &shape);

  //! tries to read a bitmap stored in the rectangle's data
  bool getBitmapBW(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj);
  //! tries to read the color bitmap stored in 55's shape: v3.5
  bool getBitmap(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj);
  //! tries to read the file bitmap: windows v3.5
  bool readFileBitmap(long length);
  //! tries to read a picture stored in the picture's data
  bool getPicture(CanvasGraphInternal::Shape const &shape, MWAWEmbeddedObject &obj);

  //
  // send data to the listener
  //

  //! updates the style corresponding to a shape
  void update(CanvasGraphInternal::Shape const &shape, MWAWGraphicStyle &style) const;
  //! tries to send a shape
  bool send(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const *local=nullptr);
  //! tries the dimension line's special shape: DIMN
  bool sendDimension(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local);
  //! tries the multiligne's special shape: Palm
  bool sendMultiLines(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local);
  //! tries to send the special content
  bool sendSpecial(CanvasGraphInternal::Shape const &shape, CanvasGraphInternal::LocalTransform const &local);
  //! tries to send the text of a text's shape
  bool sendText(CanvasGraphInternal::Shape const &shape);
  //! tries to send the text of a text's shape given a zone id
  bool sendText(int zId);

  //
  // Low level
  //

  //! mark the id's shape as read in debug mode
  void markSent(int id);
  //! look for unsent shapes in debug mode
  void checkUnsent() const;

private:
  CanvasGraph(CanvasGraph const &orig) = delete;
  CanvasGraph &operator=(CanvasGraph const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<CanvasGraphInternal::State> m_state;

  //! the main parser;
  CanvasParser *m_mainParser;
  //! the style manager
  std::shared_ptr<CanvasStyleManager> m_styleManager;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
