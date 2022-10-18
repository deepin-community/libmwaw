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
 * Parser to Canvas 5/6 drawing document ( image part )
 */
#ifndef CANVAS5_IMAGE
#  define CANVAS5_IMAGE

#include <string>
#include <utility>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

namespace Canvas5Structure
{
struct Stream;
}

namespace Canvas5ImageInternal
{
struct VKFLImage;
struct VKFLShape;

struct State;
}

class Canvas5Parser;
class Canvas5StyleManager;

/** \brief the main class to read/convert the image or movie inside of
 * Canvas 5-11 files
 *
 * \note Canvas can define many vectorized images in a drawing
 *   document (to store arrow, symbol, bitmap, macros, ...). There
 *   are stored in a "compressed" form: a series of continuous structures
 *   which are often similar to the drawing document structures.
 */
class Canvas5Image
{
  friend class Canvas5Parser;

public:
  //! constructor
  explicit Canvas5Image(Canvas5Parser &parser);
  //! destructor
  virtual ~Canvas5Image();

  /** returns the file version */
  int version() const;

  // interface with other parser

  //! try to read the AGIF rsrc block: a list of vectorised image v6
  bool readAGIFRsrc(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the macros rsrc blocks: a list of vectorised image
  bool readMACORsrc(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the QkTm rsrc block: a list of media? v6
  bool readQkTmRsrc(Canvas5Structure::Stream &stream);
  /** try to read an unknown vectorized graphic format used to store symbol, texture and arrow
   */
  bool readVKFL(std::shared_ptr<Canvas5Structure::Stream> stream, long len, std::shared_ptr<Canvas5ImageInternal::VKFLImage> &image);

  //! try to read the second big block: the list of bitmap
  bool readImages(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the list of bitmap: v9
  bool readImages9(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to retrieve a bitmap given a bitmapId
  bool getBitmap(int bitmapId, MWAWEmbeddedObject &object);
  //! try to retrieve a gif given a GIF id
  std::shared_ptr<Canvas5ImageInternal::VKFLImage> getGIF(int gifId);
  //! try to read a macro indent (low level)
  bool readMacroIndent(Canvas5Structure::Stream &stream, std::vector<unsigned> &id, std::string &extra);
  //! try to retrieve a macros image given a MACO id
  std::shared_ptr<Canvas5ImageInternal::VKFLImage> getMACO(std::vector<unsigned> const &id);
  //! try to retrieve a quicktime given a quicktimeId
  bool getQuickTime(int quicktimeId, MWAWEmbeddedObject &object);
  //! try to retrieve an arrow from a VKFL image
  bool getArrow(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWGraphicStyle::Arrow &arrow) const;
  //! try to retrieve an texture from a VKFL image
  bool getTexture(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWEmbeddedObject &texture, MWAWVec2i &textureDim, MWAWColor &averageColor) const;
  //! try to send a image where box is the image bdbox before applying transformation
  bool send(std::shared_ptr<Canvas5ImageInternal::VKFLImage> image, MWAWListenerPtr listener,
            MWAWBox2f const &box, MWAWTransformation const &transformation) const;

protected:
  //
  // Low level
  //
  //! return the style manager
  std::shared_ptr<Canvas5StyleManager> getStyleManager() const;
  //! try to read a shape in an image
  bool readVKFLShape(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image);
  //! try to read a shape header in an image
  bool readVKFLShapeMainData(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image, Canvas5ImageInternal::VKFLShape &shape,
                             MWAWEntry const &data);
  //! try to read a external data corresponding to some shapes in an image
  bool readVKFLShapeOtherData(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5ImageInternal::VKFLImage &image,
                              std::tuple<MWAWEntry, unsigned, long> const &dataTypePos,
                              std::vector<long> &childFieldPos, int subId);

  //! try to send a shape
  bool send(Canvas5ImageInternal::VKFLImage const &image, size_t &shapeId, MWAWListenerPtr listener,
            MWAWGraphicStyle const &style, MWAWTransformation const &transformation) const;
private:
  Canvas5Image(Canvas5Image const &orig) = delete;
  Canvas5Image &operator=(Canvas5Image const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<Canvas5ImageInternal::State> m_state;

  //! the main parser;
  Canvas5Parser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
