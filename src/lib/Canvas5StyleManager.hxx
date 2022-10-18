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
 * Parser to Canvas v5-v11 text document ( style part )
 *
 */
#ifndef CANVAS5_STYLE_MANAGER
#  define CANVAS5_STYLE_MANAGER

#include <string>
#include <utility>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWFont.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"

class MWAWFont;

namespace Canvas5Structure
{
struct Stream;
}

namespace Canvas5StyleManagerInternal
{
struct ColorStyle;
struct PenStyle;

struct State;
}

namespace Canvas5ImageInternal
{
struct VKFLImage;
}

class Canvas5Graph;
class Canvas5Image;
class Canvas5Parser;

/** \brief the main class to read the style part of Canvas 5 file
 *
 *
 *
 */
class Canvas5StyleManager
{
  friend class Canvas5Graph;
  friend class Canvas5Parser;

public:
  //! a structure use to store a character style
  struct CharStyle {
    //! constructor
    CharStyle()
      : m_font()
      , m_paragraphId(0)
      , m_linkId(0)
    {
    }
    //! the font
    MWAWFont m_font;
    //! the paragraph id
    int m_paragraphId;
    //! the link id
    int m_linkId;
  };

  //! a structure use to store a list of styles
  struct StyleList {
    //! constructor
    StyleList()
      : m_fonts()
      , m_paragraphs()
    {
    }
    //! the fonts, the paragraph id and the link id
    std::vector<CharStyle> m_fonts;
    //! the paragraph list and the tab id
    std::vector<std::pair<MWAWParagraph, int> > m_paragraphs;
  };

  //! constructor
  explicit Canvas5StyleManager(Canvas5Parser &parser);
  //! destructor
  virtual ~Canvas5StyleManager();

  /** returns the file version */
  int version() const;
  /** returns the image parser */
  std::shared_ptr<Canvas5Image> getImageParser() const;

  //! try to read a color style
  std::shared_ptr<Canvas5StyleManagerInternal::ColorStyle> readColorStyle(std::shared_ptr<Canvas5Structure::Stream> stream, unsigned type, long len);
  //! try to update the line color given a color style
  bool updateLineColor(Canvas5StyleManagerInternal::ColorStyle const &color, MWAWGraphicStyle &style);
  //! try to update the surface color given a color style
  bool updateSurfaceColor(Canvas5StyleManagerInternal::ColorStyle const &color, MWAWGraphicStyle &style);

  //! try to read a pen style
  std::shared_ptr<Canvas5StyleManagerInternal::PenStyle> readPenStyle(Canvas5Structure::Stream &stream, unsigned type, long len);
  //! try to update the line color given a color style
  bool updateLine(Canvas5StyleManagerInternal::PenStyle const &pen, MWAWGraphicStyle &style, int &numLines, int lineId, float *offset);

  //! try to read an arrow
  bool readArrow(std::shared_ptr<Canvas5Structure::Stream> stream, MWAWGraphicStyle::Arrow &arrow, unsigned type, long len);
  //! try to read a character style, returns a font, a paragraph id and it potential link id
  bool readCharStyle(Canvas5Structure::Stream &stream, int id, CharStyle &fontIds, bool useFileColors=true);
  //! try to read a dash's array
  bool readDash(Canvas5Structure::Stream &stream, std::vector<float> &dashes, unsigned type, long len);
  //! try to read the second part of a style which contains minor paragraph styles, hyphen, ...
  bool readStyleEnd(std::shared_ptr<Canvas5Structure::Stream> stream, MWAWFont *font=nullptr, MWAWParagraph *para=nullptr);

protected:
  // interface

  /** try to update the line style given the stroke id and returns the number of lines(plin)

      \note in case of plin, after retrieving the number of lines, use lineId to define the line's style to set
      and give a offset pointer to retrieve this line offset
   */
  bool updateLineStyle(int sId, MWAWGraphicStyle &style, int &numLines, int lineId=-1, float *offset=nullptr);
  //! try to update the line color given the color id
  bool updateLineColor(int cId, MWAWGraphicStyle &style);
  //! try to update the surface color given the color id
  bool updateSurfaceColor(int cId, MWAWGraphicStyle &style);

  //
  // Intermediate level
  //

  //! try to read the arrows zones
  bool readArrows(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the character styles
  bool readCharStyles(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the ink color zones
  bool readInks(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the ink color zones: v9
  bool readInks9(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read a color: 12 unknown 4 components followed by a type
  bool readColor(Canvas5Structure::Stream &stream, MWAWVariable<MWAWColor> &color, std::string &extra);
  //! try to read the dashes
  bool readDashes(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the frame zones: stroke, pen style, arrow, dashes: v9
  bool readFrameStyles9(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the fonts names
  bool readFonts(std::shared_ptr<Canvas5Structure::Stream> stream, int numFonts);
  //! read the list of formats, mainly an unit's conversion table
  bool readFormats(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! read the pen size (header file)
  bool readPenSize(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! read the pen styles
  bool readPenStyles(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read the stroke styles
  bool readStrokes(std::shared_ptr<Canvas5Structure::Stream> stream);
  //! try to read a paragraph style
  bool readParaStyle(std::shared_ptr<Canvas5Structure::Stream> stream, int id, StyleList *styles=nullptr);
  //! try to read the paragraph styles
  bool readParaStyles(std::shared_ptr<Canvas5Structure::Stream> stream);

  //
  // Windows RSRC
  //

  //
  // Low level
  //

  //! try to read a gradient zone: OBFL
  bool readGradient(std::shared_ptr<Canvas5Structure::Stream> stream, long len, MWAWGraphicStyle::Gradient &gradient);
  //! try to read a hatch zone: htch
  bool readHatch(std::shared_ptr<Canvas5Structure::Stream> stream, long len, MWAWGraphicStyle::Hatch &hatch,
                 MWAWVariable<MWAWColor> &backColor);
  //! try to read a symbol zone: vkfl/TXUR
  std::shared_ptr<Canvas5ImageInternal::VKFLImage> readSymbol(std::shared_ptr<Canvas5Structure::Stream> stream, long len,
      MWAWVariable<MWAWColor> &backColor);

private:
  Canvas5StyleManager(Canvas5StyleManager const &orig) = delete;
  Canvas5StyleManager &operator=(Canvas5StyleManager const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<Canvas5StyleManagerInternal::State> m_state;

  //! the main parser;
  Canvas5Parser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
