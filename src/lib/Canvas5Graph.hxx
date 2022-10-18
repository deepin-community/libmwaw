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
 * Parser to Canvas v5-v11 text document ( graphic part )
 *
 */
#ifndef CANVAS5_GRAPH
#  define CANVAS5_GRAPH

#include <string>
#include <utility>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

#include "Canvas5StyleManager.hxx"

class MWAWFont;
class MWAWParagraph;

namespace Canvas5Structure
{
struct Stream;
}

namespace Canvas5GraphInternal
{
struct Shape;
struct ShapeData;
struct State;

struct PseudoShape;

class SubDocument;
}

class Canvas5Image;

class Canvas5Parser;

/** \brief the main class to read the graphic part of Canvas v5-v11 file
 *
 *
 *
 */
class Canvas5Graph
{
  friend class Canvas5GraphInternal::SubDocument;
  friend class Canvas5Image;
  friend class Canvas5Parser;

public:
  //! constructor
  explicit Canvas5Graph(Canvas5Parser &parser);
  //! destructor
  virtual ~Canvas5Graph();

  /** returns the file version */
  int version() const;

  //! Internal: the local state of a Canvas5Graph
  struct LocalState {
    //! default constructor
    LocalState(MWAWPosition const &pos=MWAWPosition(), MWAWGraphicStyle const &style=MWAWGraphicStyle::emptyStyle())
      : m_position(pos)
      , m_style(style)
      , m_transform()
    {
    }
    //! set the matrix transform
    void multiplyMatrix(std::array<double,9> const &mat);
    //! the shape position position
    MWAWPosition m_position;
    //! the shape style
    MWAWGraphicStyle m_style;
    //! the shape transformation
    MWAWTransformation m_transform;
  };


protected:

  // interface with main parser

  //! try to send a shape
  bool sendShape(int sId);

  //
  // Intermediate level
  //

  //! try to find the list of data's shape zones
  bool findShapeDataZones(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read a shape data
  bool readShapeData(int id, Canvas5GraphInternal::Shape const &shape);
  //! try to read a special shape data
  bool readSpecialData(std::shared_ptr<Canvas5Structure::Stream> stream, long len, Canvas5GraphInternal::ShapeData &data, std::string &extra);
  //! try to read a special shape data (internal helper to understand vkfl structure)
  std::shared_ptr<Canvas5GraphInternal::PseudoShape> readSpecialData(std::shared_ptr<Canvas5Structure::Stream> stream, long len, unsigned type, MWAWBox2f const &box, std::string &extra);
  //! try to read the different shapes
  bool readShapes(Canvas5Structure::Stream &stream, int numShapes);

  //! try to read the different matrix
  bool readMatrices(std::shared_ptr<Canvas5Structure::Stream> stream);

  //! try to read a complementary styles zone: DeR3
  bool readDeR3(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5StyleManager::StyleList &styles);

  //
  // send data to the listener
  //

  //! try to send a shape with a transformation
  bool sendShape(int sId, LocalState const &local);
  //! try to send a shape with a transformation
  bool send(Canvas5GraphInternal::Shape const &shape, LocalState const &local);
  //! try to send a special shape
  bool sendSpecial(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                   LocalState const &local);
  //! try to send a special shape
  bool sendSpecial(MWAWListenerPtr listener, Canvas5GraphInternal::PseudoShape const &pseudoShape, LocalState const &local);

  //! try to send a text zone
  bool sendText(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data);

  //! try to send a curve's text zone: CvTe
  bool sendCurveText(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                     LocalState const &local);
  //! tries to send the dimension line's special shape: DIMN
  bool sendDimension(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                     LocalState const &local);
  //! tries to send the dimension line's special shape: DIMN: v9
  bool sendDimension9(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                      LocalState const &local);
  //! tries to send the effect's special shape: effe
  bool sendEffect(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                  LocalState const &local);
  //! tries to send the extrude's special shape: Extr (pretty basic)
  bool sendExtrude(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                   LocalState const &local);
  //! tries to send the technical shape: Tech (v7)
  bool sendTechnical(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
                     LocalState const &local);
  //! tries to send the gif's shape: AnGf (v7)
  bool sendGIF(MWAWListenerPtr listener, Canvas5GraphInternal::Shape const &shape, Canvas5GraphInternal::ShapeData const &data,
               LocalState const &local);

  //
  // Low level
  //

  //! tries to send a basic shape ( applying a transformation if need)
  void send(MWAWListenerPtr listener, MWAWGraphicShape const &shape, MWAWTransformation const &transform,
            MWAWGraphicStyle const &style);
  //! tries to send a measure ( applying a transformation if need)
  void send(MWAWListenerPtr listener, librevenge::RVNGString const &text, MWAWVec2f const &center,
            MWAWTransformation const &transform, MWAWFont const &font, bool addFrame);

private:
  Canvas5Graph(Canvas5Graph const &orig) = delete;
  Canvas5Graph &operator=(Canvas5Graph const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<Canvas5GraphInternal::State> m_state;

  //! the main parser;
  Canvas5Parser *m_mainParser;
  //! the image parser
  std::shared_ptr<Canvas5Image> m_imageParser;
  //! the style manager
  std::shared_ptr<Canvas5StyleManager> m_styleManager;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
