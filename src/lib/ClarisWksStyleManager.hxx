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
 * Class used to read main style by AppleWorks/ClarisWorks parser
 *
 */
#ifndef CLARIS_WKS_STYLE_MANAGER
#  define CLARIS_WKS_STYLE_MANAGER

#include <iostream>
#include <string>
#include <vector>

#include "libmwaw_internal.hxx"

#include "MWAWCell.hxx"
#include "MWAWDebug.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWInputStream.hxx"

class ClarisWksDocument;

namespace ClarisWksStyleManagerInternal
{
struct State;
}

//! a structure to store the style list and the lookup zone
class ClarisWksStyleManager
{
public:
  struct CellFormat;
  struct KSEN;
  struct Style;
public:
  //! constructor
  ClarisWksStyleManager(MWAWParserStatePtr const &parserState, ClarisWksDocument *document=nullptr);
  //! destructor
  ~ClarisWksStyleManager();

  //! reads a color map zone ( v4-v6)
  bool readColorList(MWAWEntry const &entry);
  //! reads a pattern map zone ( v2)
  bool readPatternList(long endPos=-1);
  //! reads a gradient map zone ( v2)
  bool readGradientList(long endPos=-1);
  /** try to read the styles definition (in v4-6) */
  bool readStyles(MWAWEntry const &entry);
  /** read the font name style zone (method to store font names in v2/v3 files) */
  bool readFontNames();
  //! update a style using a gradiant id
  bool updateGradient(int grad, MWAWGraphicStyle &style) const;
  //! update a style using a wall paper id
  bool updateWallPaper(int wall, MWAWGraphicStyle &style) const;

  //! return a mac font id corresponding to a local id
  int getFontId(int localId) const;
  //! return the color which corresponds to an id (if possible)
  bool getColor(int id, MWAWColor &col) const;
  //! return the pattern which corresponds to an id.
  bool getPattern(int id, MWAWGraphicStyle::Pattern &pattern, float &percent) const;
  //! return the name corresponding to a styleId
  bool getRulerName(int id, std::string &name) const;

  //! return the style corresponding to a styleId
  bool get(int styleId, Style &style) const;
  //! return the font corresponding to a fontId
  bool get(int fontId, MWAWFont &font) const;
  //! return the cell format corresponding to a cellFormatId
  bool get(int formatId, CellFormat &format) const;
  //! return the ksen style corresponding to a ksenId
  bool get(int ksenId, KSEN &ksen) const;
  //! return the graphic style corresponding to a graphicId
  bool get(int graphId, MWAWGraphicStyle &graph) const;

  //! try to read a named font
  bool readFont(int id, int fontSize, MWAWFont &font);
  //! try to read a font
  bool readFontAndPos(int id, int &posC, MWAWFont &font);

protected:
  //! return the file version
  int version() const;

  /** try to read a STYL_ subzone (in v4-6) */
  bool readGenStyle(int id);

  //! try to read the style definition zone
  bool readStylesDef(int N, int fSz);
  //! try to read the lookup zone
  bool readLookUp(int N, int fSz);

  /* read the STYL CELL sequence */
  bool readCellStyles(int N, int fSz);
  /** read the font name style zone (in the style zones) */
  bool readFontNames(int N, int fSz);
  /** read a GraphicStyle sequence */
  bool readGraphStyles(int N, int fSz);
  //! read a KSEN sequence
  bool readKSEN(int N, int fSz);
  /** read a STYL Name sequence */
  bool readStyleNames(int N, int fSz);
  /** read a STYL_CHAR Font sequence */
  bool readStyleFonts(int N, int fSz);

protected:
  //! the document
  ClarisWksDocument *m_document;
  //! the parser state
  MWAWParserStatePtr m_parserState;
  //! the state
  std::shared_ptr<ClarisWksStyleManagerInternal::State> m_state;

private:
  ClarisWksStyleManager(ClarisWksStyleManager const &orig) = delete;
  ClarisWksStyleManager &operator=(ClarisWksStyleManager const &orig) = delete;

public:
  //! the CELL structure a structure related to number/date format
  struct CellFormat final : public MWAWCell::Format {
    //! constructor from cell
    explicit CellFormat(MWAWCell::Format const &format=MWAWCell::Format())
      : MWAWCell::Format(format)
      , m_hAlign(MWAWCell::HALIGN_DEFAULT)
      , m_fileFormat(-1)
      , m_borders(0)
      , m_wrap(false)
      , m_extra("")
    {
    }
    CellFormat(CellFormat const &)=default;
    CellFormat &operator=(CellFormat const &)=default;
    CellFormat &operator=(CellFormat &&)=default;
    //! destructor
    ~CellFormat() final;
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, CellFormat const &form);
    //! the cell alignment : by default nothing
    MWAWCell::HorizontalAlignment m_hAlign;
    //! the field format: number, string, currency, ..
    int m_fileFormat;
    //! the borders
    int m_borders;
    //! true if the cell content is wrapped
    bool m_wrap;
    //! extra data
    std::string m_extra;
  };

  //! the KSEN structure a structure related to paragraph and cell style
  struct KSEN {
    //! constructor
    KSEN()
      : m_valign(0)
      , m_lineType(MWAWBorder::Simple)
      , m_lineRepeat(MWAWBorder::Single)
      , m_lines(0)
      , m_extra("")
    {
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, KSEN const &ksen);
    //! the vertical alignment
    int m_valign;
    //! the line type
    MWAWBorder::Style m_lineType;
    //! the line repetition
    MWAWBorder::Type m_lineRepeat;
    //! an int used to add some oblique line ( or cross )
    int m_lines;
    //! extra data
    std::string m_extra;
  };

  //! the structure to store the style in a ClarisWksStyleManager
  struct Style {
    //! constructor
    Style()
      : m_fontId(-1)
      , m_cellFormatId(-1)
      , m_rulerId(-1)
      , m_rulerPId(-1)
      , m_nameId(-1)
      , m_ksenId(-1), m_graphicId(-1)
      , m_localStyleId(-1)
      , m_styleId(-1)
      , m_extra("")
    {
    }

    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Style const &style);

    //! the char
    int m_fontId;
    //! the formatId
    int m_cellFormatId;
    //! the ruler
    int m_rulerId;
    //! the ruler parent id ( or maybe the style parent)
    int m_rulerPId;
    //! the style name id
    int m_nameId;
    //! the ksen id
    int m_ksenId;
    //! the graphic (checkme)
    int m_graphicId;
    //! a local style id
    int m_localStyleId;
    //! the style id
    int m_styleId;
    //! extra data
    std::string m_extra;
  };
};

#endif
