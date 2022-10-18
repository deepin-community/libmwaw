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

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSection.hxx"
#include "MWAWTable.hxx"

#include "MacWrtProStructures.hxx"
#include "MacWrtProParser.hxx"

/** Internal: the structures of a MacWrtProStructures */
namespace MacWrtProStructuresInternal
{
//! Internal: the graphic structure
struct Graphic {
  enum Type { UNKNOWN, GRAPHIC, TEXT, NOTE };
  //! the constructor
  explicit Graphic(int vers)
    : m_version(vers)
    , m_type(-1)
    , m_contentType(UNKNOWN)
    , m_fileBlock(0)
    , m_id(-1)
    , m_attachment(false)
    , m_page(-1)
    , m_box()
    , m_textPos(0)
      // II
    , m_textboxType(0)
    , m_headerFooterFlag(0)
    , m_column(1)
    , m_colSeparator(0)
    , m_lastFlag(0)
      // 1 or 1.5
    , m_baseline(0.)
    , m_surfaceColor(MWAWColor::white())
    , m_lineBorder()
    , m_isHeader(false)
    , m_row(0)
    , m_col(0)
    , m_textboxCellType(0)
      // all
    , m_extra("")
    , m_send(false)
  {
    for (auto &w : m_borderWList) w= 0;
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Graphic const &bl)
  {
    if (bl.m_version==0) {
      switch (std::abs(bl.m_type)) {
      case 3:
        o << "textbox,";
        break;
      default:
        o << "type=" << bl.m_type << ",";
        break;
      }
      switch (bl.m_textboxType) {
      case 1: // a header zone
        o << "header,";
        break;
      case 2: // a footer zone
        o << "footer,";
        break;
      case 3: // a footnote zone
        o << "footnote,";
        break;
      case 0:
        break;
      default:
        MWAW_DEBUG_MSG(("MacWrtProStructures::Graphic::operator<<: find unknown textbox type\n"));
        o << "##fram[type]=" << bl.m_textboxType << ",";
        break;
      }
      switch (bl.m_headerFooterFlag) {
      case 1:
        o << "left[page],";
        break;
      case 2:
        o << "right[page],";
        break;
      case 3:
        o << "###page=" << bl.m_headerFooterFlag << ",";
        break;
      case 0:
      default: // all
        break;
      }
      if (bl.m_textPos) o << "textPos=" << bl.m_textPos << ",";
      if (bl.m_type<0) o << "background,";
    }
    else {
      switch (bl.m_contentType) {
      case GRAPHIC:
        o << "graphic,";
        if (bl.m_type != 8) {
          MWAW_DEBUG_MSG(("MacWrtProStructuresInternal::Graphic::operator<< unknown type\n"));
          o << "#type=" << bl.m_type << ",";
        }
        break;
      case NOTE:
        o << "note";
        break;
      case TEXT:
        o << "text";
        switch (bl.m_type) {
        case 3:
          o << "[table]";
          break;
        case 4:
          o << "[textbox/cell/note]";
          break;
        case 5:
          if (bl.m_textPos) o << "[pageBreak:" << bl.m_textPos << "]";
          break;
        case 6:
          if (bl.m_isHeader)
            o << "[header]";
          else
            o << "[footer]";
          break;
        case 7:
          o << "[footnote]";
          break;
        case 8:
          o << "[empty frame]";
          break;
        default:
          // v2: -1: can be the document zone, but not always
          MWAW_DEBUG_MSG(("MacWrtProStructuresInternal::Graphic::operator<< unknown type\n"));
          o << "[#" << bl.m_type << "]";
          break;
        }
        o << ",";
        break;
      case UNKNOWN:
#if !defined(__clang__)
      default:
#endif
        break;
      }
    }
    if (bl.m_column>1) {
      o << "col[num]=" << bl.m_column << ",";
      o << "col[sep]=" << bl.m_colSeparator << ",";
    }
    if (bl.m_id >= 0) o << "id=" << bl.m_id << ",";
    o << "box=" << bl.m_box << ",";
    static char const *wh[] = { "L", "R", "T", "B" };
    if (bl.hasSameBorders()) {
      if (bl.m_borderWList[0] > 0)
        o << "bord[width]=" << bl.m_borderWList[0] << ",";
    }
    else {
      for (int i = 0; i < 4; ++i) {
        if (bl.m_borderWList[i] <= 0)
          continue;
        o << "bord" << wh[i] << "[width]=" << bl.m_borderWList[i] << ",";
      }
    }
    if (bl.m_contentType==TEXT && bl.m_type==4) {
      for (int i = 0; i < 4; ++i)
        o << "bord" << wh[i] << "[cell]=[" << bl.m_borderCellList[i] << "],";
    }
    if (bl.m_baseline < 0 || bl.m_baseline >0) o << "baseline=" << bl.m_baseline << ",";
    if (!bl.m_surfaceColor.isWhite())
      o << "col=" << bl.m_surfaceColor << ",";
    if (!bl.m_lineBorder.isEmpty())
      o << "line=" << bl.m_lineBorder << ",";
    if (bl.m_fileBlock > 0) o << "block=" << std::hex << bl.m_fileBlock << std::dec << ",";
    if (bl.m_extra.length())
      o << bl.m_extra << ",";
    return o;
  }
  //! update the style to include frame style
  void fillFrame(MWAWGraphicStyle &style) const
  {
    if (!m_surfaceColor.isWhite())
      style.setBackgroundColor(m_surfaceColor);
    if (!hasBorders())
      return;
    for (int w=0; w < 4; ++w) {
      MWAWBorder border(m_lineBorder);
      border.m_width=m_borderWList[w]; // ok also for setAll
      if (border.isEmpty())
        continue;
      static int const wh[] = { libmwaw::LeftBit, libmwaw::RightBit, libmwaw::TopBit, libmwaw::BottomBit};
      style.setBorders(wh[w], border);
    }
  }

  //! returns true is this is a graphic zone
  bool isGraphic() const
  {
    return m_fileBlock > 0 && m_contentType == GRAPHIC;
  }
  //! returns true is this is a text zone (or a not)
  bool isText() const
  {
    return m_fileBlock > 0 && (m_contentType == TEXT || m_contentType == NOTE);
  }
  //! returns true is this is a table zone
  bool isTable() const
  {
    return m_fileBlock <= 0 && m_type == 3;
  }
  bool hasSameBorders() const
  {
    for (int i=1; i < 4; ++i) {
      if (m_borderWList[i] > m_borderWList[0] ||
          m_borderWList[i] < m_borderWList[0])
        return false;
    }
    return true;
  }
  bool hasBorders() const
  {
    if (m_lineBorder.m_color.isWhite() || m_lineBorder.isEmpty())
      return false;
    for (auto w : m_borderWList) {
      if (w > 0)
        return true;
    }
    return false;
  }

  MWAWPosition getPosition() const
  {
    MWAWPosition res;
    if (m_attachment) {
      res = MWAWPosition(MWAWVec2f(0,0), m_box.size(), librevenge::RVNG_POINT);
      res.setRelativePosition(MWAWPosition::Char, MWAWPosition::XLeft, getRelativeYPos());
    }
    else {
      res = MWAWPosition(m_box.min(), m_box.size(), librevenge::RVNG_POINT);
      res.setRelativePosition(MWAWPosition::Page);
      res.setPage(m_page);
      res.m_wrapping = m_contentType==NOTE ? MWAWPosition::WRunThrough :
                       MWAWPosition::WDynamic;
    }
    return res;
  }

  MWAWPosition::YPos getRelativeYPos() const
  {
    float height = m_box.size()[1];
    if (m_baseline < 0.25f*height) return MWAWPosition::YBottom;
    if (m_baseline < 0.75f*height) return MWAWPosition::YCenter;
    return MWAWPosition::YTop;
  }
  bool contains(MWAWBox2f const &box) const
  {
    return box[0][0] >= m_box[0][0] && box[0][1] >= m_box[0][1] &&
           box[1][0] <= m_box[1][0] && box[1][1] <= m_box[1][1];
  }
  bool intersects(MWAWBox2f const &box) const
  {
    if (box[0][0] >= m_box[1][0] || box[0][1] >= m_box[1][1] ||
        box[1][0] <= m_box[0][0] || box[1][1] <= m_box[1][1])
      return false;
    if (m_box[0][0] >= box[1][0] || m_box[0][1] >= box[1][1] ||
        m_box[1][0] <= box[0][0] || m_box[1][1] <= box[1][1])
      return false;
    return true;
  }

  //! the version
  int m_version;

  //! the type
  int m_type;

  //! the type 1.0 or 1.5
  Type m_contentType;

  //! the file block id
  int m_fileBlock;

  //! the graphic id
  int m_id;

  //! true if this is an attachment 1.0 or 1.5
  bool m_attachment;

  //! the page (if absolute)
  int m_page;

  //! the bdbox
  MWAWBox2f m_box;

  /** filled for pagebreak pos */
  int m_textPos;

  // vII

  //! the header footer type
  int m_textboxType;
  //! the header footer flag
  int m_headerFooterFlag;

  //! the number of columns
  int m_column;
  //! the columns separator
  float m_colSeparator;


  /** the last flag */
  int m_lastFlag;

  //
  //    v1.0 or v.15
  //

  //! the borders width
  double m_borderWList[4];

  //! the cell borders
  MWAWBorder m_borderCellList[4];

  //! the baseline ( in point 0=bottom aligned)
  float m_baseline;

  //! the background color
  MWAWColor m_surfaceColor;

  //! the line border
  MWAWBorder m_lineBorder;

  /** filled for header/footer */
  bool m_isHeader;

  /** number of row, filled for table */
  int m_row;

  /** number of columns, filled for table */
  int m_col;

  /** filled for textbox : 0: unknown/textbox, 1: cell, 2: textbox(opened)*/
  int m_textboxCellType;

  //! extra data
  std::string m_extra;

  //! true if we have send the data
  bool m_send;
};

//! Internal: a page
struct Page {
  //! the constructor
  Page()
    : m_page(-1)
    , m_graphicsList()
    , m_extra("")
    , m_send(false)
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Page const &bl)
  {
    if (bl.m_page > 0) o << "page=" << bl.m_page << ",";
    if (bl.m_extra.length())
      o << bl.m_extra << ",";
    return o;
  }
  //! the page (if absolute)
  int m_page;
  //! the graphic list
  std::vector<std::shared_ptr<Graphic> > m_graphicsList;
  //! extra data
  std::string m_extra;

  //! true if we have send the data
  bool m_send;
};

////////////////////////////////////////
//! Internal: the fonts
struct Font {
  //! the constructor
  Font()
    : m_font()
    , m_flags(0)
    , m_token(-1)
  {
    for (auto &val : m_values) val = 0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Font const &font)
  {
    if (font.m_flags) o << "flags=" << std::hex << font.m_flags << std::dec << ",";
    for (int i = 0; i < 5; ++i) {
      if (!font.m_values[i]) continue;
      o << "f" << i << "=" << font.m_values[i] << ",";
    }
    switch (font.m_token) {
    case -1:
      break;
    default:
      o << "token=" << font.m_token << ",";
      break;
    }
    return o;
  }

  //! the font
  MWAWFont m_font;
  //! some unknown flag
  int m_flags;
  //! the token type(checkme)
  int m_token;
  //! unknown values
  int m_values[5];
};

////////////////////////////////////////
/** Internal: class to store the paragraph properties */
struct Paragraph final : public MWAWParagraph {
  //! Constructor
  Paragraph()
    :  m_value(0)
  {
  }
  Paragraph(Paragraph const &)=default;
  Paragraph &operator=(Paragraph const &)=default;
  Paragraph &operator=(Paragraph &&)=default;
  //! destructor
  ~Paragraph() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Paragraph const &ind)
  {
    o << static_cast<MWAWParagraph const &>(ind);
    if (ind.m_value) o << "unkn=" << ind.m_value << ",";
    return o;
  }
  //! a unknown value
  int m_value;
};

Paragraph::~Paragraph()
{
}

////////////////////////////////////////
//! Internal: the cell of a MacWrtProStructure
struct Cell final : public MWAWCell {
  //! constructor
  Cell(MacWrtProStructures &parser, Graphic *graphic)
    : MWAWCell()
    , m_parser(parser)
    , m_graphicId(0)
  {
    if (!graphic) return;
    setBdBox(MWAWBox2f(graphic->m_box.min(), graphic->m_box.max()-MWAWVec2f(1,1)));
    setBackgroundColor(graphic->m_surfaceColor);
    m_graphicId = graphic->m_id;
    for (int b=0; b<4; ++b) {
      int const wh[4] = { libmwaw::LeftBit, libmwaw::RightBit,
                          libmwaw::TopBit, libmwaw::BottomBit
                        };
      setBorders(wh[b], graphic->m_borderCellList[b]);
    }
  }
  //! destructor
  ~Cell() final;
  //! send the content
  bool sendContent(MWAWListenerPtr listener, MWAWTable &) final
  {
    if (m_graphicId > 0)
      m_parser.send(m_graphicId);
    else if (listener) // try to avoid empty cell
      listener->insertChar(' ');
    return true;
  }

  //! the text parser
  MacWrtProStructures &m_parser;
  //! the graphic id
  int m_graphicId;
};

Cell::~Cell()
{
}

////////////////////////////////////////
////////////////////////////////////////
struct Table final : public MWAWTable {
  //! constructor
  Table() : MWAWTable()
  {
  }
  //! destructor
  ~Table() final;
  //! return a cell corresponding to id
  Cell *get(int id)
  {
    if (id < 0 || id >= numCells()) {
      MWAW_DEBUG_MSG(("MacWrtProStructuresInternal::Table::get: cell %d does not exists\n",id));
      return nullptr;
    }
    return static_cast<Cell *>(MWAWTable::get(id).get());
  }
};

Table::~Table()
{
}

////////////////////////////////////////
//! Internal: the section of a MacWrtProStructures
struct Section {
  enum StartType { S_Line, S_Page, S_PageLeft, S_PageRight };

  //! constructor
  Section()
    : m_start(S_Page)
    , m_colsPos()
    , m_textLength(0)
    , m_extra("")
  {
    for (auto &id : m_headerIds) id=0;
    for (auto &id : m_footerIds) id=0;
  }
  //! returns a MWAWSection
  MWAWSection getSection() const
  {
    MWAWSection sec;
    size_t numCols=m_colsPos.size()/2;
    if (numCols <= 1)
      return sec;
    sec.m_columns.resize(numCols);
    float prevPos=0;
    for (size_t c=0; c < numCols; ++c) {
      sec.m_columns[c].m_width = double(m_colsPos[2*c+1]-prevPos);
      prevPos = m_colsPos[2*c+1];
      sec.m_columns[c].m_widthUnit = librevenge::RVNG_POINT;
      sec.m_columns[c].m_margins[libmwaw::Right] =
        double(m_colsPos[2*c+1]-m_colsPos[2*c])/72.;
    }
    return sec;
  }
  //! return the number of columns
  int numColumns() const
  {
    auto numCols=int(m_colsPos.size()/2);
    return numCols ? numCols : 1;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Section const &sec)
  {
    switch (sec.m_start) {
    case S_Line:
      o << "newLine,";
      break;
    case S_Page:
      break;
    case S_PageLeft:
      o << "newPage[left],";
      break;
    case S_PageRight:
      o << "newPage[right],";
      break;
#if !defined(__clang__)
    default:
      break;
#endif
    }
    auto nColumns = size_t(sec.numColumns());
    if (nColumns != 1) {
      o << "nCols=" << nColumns << ",";
      o << "colsPos=[";
      for (size_t i = 0; i < 2*nColumns; i+=2)
        o << sec.m_colsPos[i] << ":" << sec.m_colsPos[i+1] << ",";
      o << "],";
    }
    if (sec.m_headerIds[0]) o << "sec.headerId=" << sec.m_headerIds[0] << ",";
    if (sec.m_headerIds[0]!=sec.m_headerIds[1]) o << "sec.headerId1=" << sec.m_headerIds[0] << ",";
    if (sec.m_footerIds[0]) o << "sec.footerId=" << sec.m_footerIds[0] << ",";
    if (sec.m_footerIds[0]!=sec.m_footerIds[1]) o << "sec.footerId1=" << sec.m_footerIds[0] << ",";
    if (sec.m_textLength) o << "nChar=" << sec.m_textLength << ",";
    if (sec.m_extra.length()) o << sec.m_extra;
    return o;
  }
  //! the way to start the new section
  StartType m_start;
  //! the columns position ( series of end columns <-> new column begin)
  std::vector<float> m_colsPos;
  //! the header graphic ids
  int m_headerIds[2];
  //! the footerer graphic ids
  int m_footerIds[2];
  //! the number of character in the sections
  long m_textLength;
  //! extra data
  std::string m_extra;
};

////////////////////////////////////////
//! Internal: the state of a MacWrtProStructures
struct State {
  //! constructor
  State()
    : m_version(-1)
    , m_numPages(1)
    , m_inputData()
    , m_fontsList()
    , m_paragraphsList()
    , m_pagesList()
    , m_sectionsList()
    , m_graphicsList()
    , m_tablesMap()
    , m_idGraphicMap()
    , m_headersMap()
    , m_footersMap()
    , m_graphicsSendSet()
  {
  }

  //! try to set the line properties of a border
  static bool updateLineType(int lineType, MWAWBorder &border)
  {
    switch (lineType) {
    case 2:
      border.m_type=MWAWBorder::Double;
      border.m_widthsList.resize(3,2.);
      border.m_widthsList[1]=1.0;
      break;
    case 3:
      border.m_type=MWAWBorder::Double;
      border.m_widthsList.resize(3,1.);
      border.m_widthsList[2]=2.0;
      break;
    case 4:
      border.m_type=MWAWBorder::Double;
      border.m_widthsList.resize(3,1.);
      border.m_widthsList[0]=2.0;
      break;
    case 1: // solid
      break;
    default:
      return false;
    }
    return true;
  }

  //! the file version
  int m_version;

  //! the number of pages
  int m_numPages;

  //! the input data
  librevenge::RVNGBinaryData m_inputData;

  //! the list of fonts
  std::vector<Font> m_fontsList;
  //! the list of paragraph
  std::vector<Paragraph> m_paragraphsList;
  //! the pages list (for MWII)
  std::vector<Page> m_pagesList;
  //! the list of section
  std::vector<Section> m_sectionsList;
  //! the list of graphic
  std::vector<std::shared_ptr<Graphic> > m_graphicsList;
  //! a map graphic id -> table
  std::map<int, std::shared_ptr<Table> > m_tablesMap;
  //! a map graphic id -> graphic
  std::map<int, std::shared_ptr<Graphic> > m_idGraphicMap;
  //! a map page -> header id
  std::map<int, int> m_headersMap;
  //! a map page -> footer id
  std::map<int, int> m_footersMap;
  //! a list of graphic use to avoid potential loop in bad file
  std::set<MWAWVec2i> m_graphicsSendSet;
};

}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MacWrtProStructures::MacWrtProStructures(MacWrtProParser &parser)
  : m_parserState(parser.getParserState())
  , m_mainParser(parser)
  , m_state()
  , m_asciiName()
{
  init();
}

MacWrtProStructures::~MacWrtProStructures()
{
}

void MacWrtProStructures::init()
{
  m_state.reset(new MacWrtProStructuresInternal::State);
}

int MacWrtProStructures::version() const
{
  if (m_state->m_version < 0)
    m_state->m_version = m_parserState->m_version;
  return m_state->m_version;
}

MWAWTextListenerPtr &MacWrtProStructures::getTextListener()
{
  return m_parserState->m_textListener;
}

int MacWrtProStructures::numPages() const
{
  return m_state->m_numPages;
}

////////////////////////////////////////////////////////////
// update a page span
void MacWrtProStructures::updatePageSpan(int page, bool hasTitlePage, MWAWPageSpan &ps)
{
  if (version()==0) {
    // title page has no header/footer
    if (hasTitlePage && page==0) {
      ps.setPageSpan(1);
      return;
    }
    // hf is defined for all pages excepted the title page
    int index=0;
    for (size_t i = 0; i < m_state->m_pagesList.size(); ++i) {
      if (i>=2) break;
      auto const &pge = m_state->m_pagesList[i];
      for (auto const &graphic : pge.m_graphicsList) {
        if (graphic->m_textboxType<1 || graphic->m_textboxType>2) continue;
        m_state->m_idGraphicMap[++index]=graphic;
        MWAWHeaderFooter hf(graphic->m_textboxType==1 ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER,
                            graphic->m_headerFooterFlag==1 ? MWAWHeaderFooter::EVEN : graphic->m_headerFooterFlag==2 ? MWAWHeaderFooter::ODD : MWAWHeaderFooter::ALL);
        hf.m_subDocument=m_mainParser.getSubDocument(index);
        ps.setHeaderFooter(hf);
      }
    }
    ps.setPageSpan(m_state->m_numPages>page ? m_state->m_numPages-page : 100);
    return;
  }
  ++page;
  int numSimilar[2]= {1,1};
  for (int st=0; st<2; ++st) {
    auto const &map=st==0 ? m_state->m_headersMap : m_state->m_footersMap;
    auto it=map.lower_bound(page);
    if (it==map.end()) {
      if (m_state->m_numPages>page)
        numSimilar[st]=m_state->m_numPages-page+1;
      continue;
    }
    if (it->first!=page) {
      numSimilar[st]=it->first-page;
      continue;
    }
    int id=it->second;
    while (++it!=map.end() && it->second==id)
      numSimilar[st]++;
    if (!id) continue;
    MWAWHeaderFooter hf(st==0 ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    hf.m_subDocument=m_mainParser.getSubDocument(id);
    ps.setHeaderFooter(hf);
  }
  ps.setPageSpan(std::min<int>(numSimilar[0],numSimilar[1]));
}

////////////////////////////////////////////////////////////
// try to return the color/pattern, set the line properties
bool MacWrtProStructures::getColor(int colId, MWAWColor &color) const
{
  if (version()==0) {
    // MWII: 2:red 4: blue, ..
    switch (colId) {
    case 0:
      color = 0xFFFFFF;
      break;
    case 1:
      color = 0;
      break;
    case 2:
      color = 0xFF0000;
      break;
    case 3:
      color = 0x00FF00;
      break;
    case 4:
      color = 0x0000FF;
      break;
    case 5:
      color = 0x00FFFF;
      break; // cyan
    case 6:
      color = 0xFF00FF;
      break; // magenta
    case 7:
      color = 0xFFFF00;
      break; // yellow
    default:
      MWAW_DEBUG_MSG(("MacWrtProStructures::getColor: unknown color %d\n", colId));
      return false;
    }
  }
  else {
    /* 0: white, 38: yellow, 44: magenta, 36: red, 41: cyan, 39: green, 42: blue
       checkme: this probably corresponds to the following 81 gray/color palette...
    */
    uint32_t const colorMap[] = {
      0xFFFFFF, 0x0, 0x222222, 0x444444, 0x666666, 0x888888, 0xaaaaaa, 0xcccccc, 0xeeeeee,
      0x440000, 0x663300, 0x996600, 0x002200, 0x003333, 0x003399, 0x000055, 0x330066, 0x660066,
      0x770000, 0x993300, 0xcc9900, 0x004400, 0x336666, 0x0033ff, 0x000077, 0x660099, 0x990066,
      0xaa0000, 0xcc3300, 0xffcc00, 0x006600, 0x006666, 0x0066ff, 0x0000aa, 0x663399, 0xcc0099,
      0xdd0000, 0xff3300, 0xffff00, 0x008800, 0x009999, 0x0099ff, 0x0000dd, 0x9900cc, 0xff0099,
      0xff3333, 0xff6600, 0xffff33, 0x00ee00, 0x00cccc, 0x00ccff, 0x3366ff, 0x9933ff, 0xff33cc,
      0xff6666, 0xff6633, 0xffff66, 0x66ff66, 0x66cccc, 0x66ffff, 0x3399ff, 0x9966ff, 0xff66ff,
      0xff9999, 0xff9966, 0xffff99, 0x99ff99, 0x66ffcc, 0x99ffff, 0x66ccff, 0x9999ff, 0xff99ff,
      0xffcccc, 0xffcc99, 0xffffcc, 0xccffcc, 0x99ffcc, 0xccffff, 0x99ccff, 0xccccff, 0xffccff
    };
    if (colId < 0 || colId >= 81) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::getColor: unknown color %d\n", colId));
      return false;
    }
    color = colorMap[colId];
  }
  return true;
}

bool MacWrtProStructures::getPattern(int patId, float &patternPercent) const
{
  patternPercent=1.0f;
  if (version()==0) // not implemented
    return false;
  static float const defPercentPattern[64] = {
    0.0f, 1.0f, 0.968750f, 0.93750f, 0.8750f, 0.750f, 0.50f, 0.250f,
    0.250f, 0.18750f, 0.18750f, 0.1250f, 0.06250f, 0.06250f, 0.031250f, 0.015625f,
    0.750f, 0.50f, 0.250f, 0.3750f, 0.250f, 0.1250f, 0.250f, 0.1250f,
    0.750f, 0.50f, 0.250f, 0.3750f, 0.250f, 0.1250f, 0.250f, 0.1250f,
    0.750f, 0.50f, 0.50f, 0.50f, 0.50f, 0.250f, 0.250f, 0.234375f,
    0.6250f, 0.3750f, 0.1250f, 0.250f, 0.218750f, 0.218750f, 0.1250f, 0.093750f,
    0.50f, 0.56250f, 0.43750f, 0.3750f, 0.218750f, 0.281250f, 0.18750f, 0.093750f,
    0.593750f, 0.56250f, 0.515625f, 0.343750f, 0.31250f, 0.250f, 0.250f, 0.234375f,
  };
  if (patId <= 0 || patId>64) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::getPattern: unknown pattern %d\n", patId));
    return false;
  }
  patternPercent=defPercentPattern[patId-1];
  return true;
}

bool MacWrtProStructures::getColor(int colId, int patId, MWAWColor &color) const
{
  if (!getColor(colId, color))
    return false;
  if (patId==0)
    return true;
  float percent;
  if (!getPattern(patId,percent))
    return false;
  color=MWAWColor::barycenter(percent,color,1.f-percent,MWAWColor::white());
  return true;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool MacWrtProStructures::createZones(std::shared_ptr<MWAWStream> &stream, int numPages)
{
  if (!stream)
    return false;
  if (version() == 0)
    return createZonesII(stream, numPages);

  auto &input=stream->m_input;
  auto &ascFile=stream->m_ascii;
  long pos = input->tell();

  bool ok = readStyles(stream) && readCharStyles(stream);
  if (ok) {
    pos = input->tell();
    if (!readSelection(stream)) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(Selection):#");
      input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    }
  }

  if (ok) {
    pos = input->tell();
    ok = readFontsName(stream);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(FontsName):#");
    }
  }
  if (ok) {
    pos = input->tell();
    ok = readStructB(stream);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(StructB):#");
    }
  }
  if (ok) {
    pos = input->tell();
    ok = readFontsDef(stream);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(FontsDef):#");
    }
  }
  if (ok) {
    pos = input->tell();
    ok = readParagraphs(stream);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(ParaZone):#");
    }
  }
  for (int st = 0; st < 2; ++st) {
    if (!ok) break;
    pos = input->tell();
    std::vector<MacWrtProStructuresInternal::Section> sections;
    ok = readSections(stream, sections);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(Sections):#");
      break;
    }
    if (st == 0) continue;
    m_state->m_sectionsList = sections;
  }
  if (ok) {
    pos = input->tell();
    libmwaw::DebugStream f;
    f << "Entries(UserName):";
    // username,
    std::string res;
    for (int i = 0; i < 2; ++i) {
      ok = readString(input, res);
      if (!ok) {
        f << "#" ;
        break;
      }
      f << "'" << res << "',";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  if (ok) {
    pos = input->tell();
    ok = readGraphicsList(stream, numPages);
    if (!ok) {
      ascFile.addPos(pos);
      ascFile.addNote("Entries(Graphic):#");
    }
  }

  pos = input->tell();
  ascFile.addPos(pos);
  ascFile.addNote("Entries(End)");

  // ok, now we can build the structures
  buildPageStructures();
  buildTableStructures();

  return true;
}

bool MacWrtProStructures::createZonesII(std::shared_ptr<MWAWStream> &stream, int numPages)
{
  if (!stream) return false;
  if (version()) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::createZonesII: must be called for a MacWriteII file\n"));
    return false;
  }
  libmwaw::DebugStream f;
  auto &input=stream->m_input;
  auto &ascFile=stream->m_ascii;
  long pos;
  bool ok = readFontsName(stream);
  if (ok) {
    pos = input->tell();
    long val = long(input->readULong(4));
    if (val) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::createZonesII: argh!!! find data after the fonts name zone. Trying to continue.\n"));
      f.str("");
      // in QuarkXPress color
      f << "Entries(Color):#" << std::hex << val << std::dec;

      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    else {
      ascFile.addPos(pos);
      ascFile.addNote("_");
    }
    ok = readCharStyles(stream);
  }
  if (ok)
    ok = readFontsDef(stream);
  if (ok)
    ok = readParagraphs(stream);
  // FIXME: this code is bad, look for XPressGraph::readPagesListII which is very simillar
  if (ok)
    readPagesListII(stream, numPages);

  pos = input->tell();
  if (input->checkPosition(pos+256)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::createZonesII: oops, probable problem when reading the pages...\n"));
  }
  ascFile.addPos(pos);
  ascFile.addNote("Entries(Page)[End]:");

  int nPages = numPages;
  int nFootnotes=0;
  for (auto const &pages : m_state->m_pagesList) {
    bool mainBlock=true;
    for (auto const &graphic : pages.m_graphicsList) {
      if (graphic->m_fileBlock>0) {
        m_mainParser.parseDataZone(graphic->m_fileBlock, 0);
        if (mainBlock)
          nPages += m_mainParser.findNumHardBreaks(graphic->m_fileBlock);
      }
      if (graphic->m_textboxType==3) // footnote
        m_state->m_idGraphicMap[--nFootnotes]=graphic;
      mainBlock=false;
    }
  }
  m_state->m_numPages = nPages;

  return true;
}

////////////////////////////////////////////////////////////
// try to find the main text zone and sent it
bool MacWrtProStructures::sendMainZone()
{
  int vers = version();
  if (vers==0) {
    if (m_state->m_pagesList.size()>=3) {
      auto const &page=m_state->m_pagesList[2];
      if (!page.m_graphicsList.empty()) {
        m_state->m_idGraphicMap[0]=page.m_graphicsList[0];
        return send(0, true);
      }
    }
  }
  else {
    for (size_t i = 0; i < m_state->m_graphicsList.size(); ++i) {
      auto graphic = m_state->m_graphicsList[i];
      if (!graphic->isText() || graphic->m_send) continue;
      if (vers == 1 && graphic->m_type != 5)
        continue;
      return send(graphic->m_id, true);
    }
  }
  //ok the main zone can be empty
  std::shared_ptr<MacWrtProStructures> THIS
  (this, MWAW_shared_ptr_noop_deleter<MacWrtProStructures>());
  MacWrtProStructuresListenerState listenerState(THIS, true, vers);
  return true;
}

////////////////////////////////////////////////////////////
// try to find the header and the pages break
void MacWrtProStructures::buildPageStructures()
{
  // first find the pages break
  std::set<long> set;
  int actPage = 0;
  for (auto graphic : m_state->m_graphicsList) {
    graphic->m_page = actPage ? actPage : 1; // mainly ok
    if (graphic->m_type != 5)
      continue;
    actPage++;
    set.insert(graphic->m_textPos);
  }
  long actSectPos = 0;
  for (auto &sec : m_state->m_sectionsList) {
    if (sec.m_start != sec.S_Line) set.insert(actSectPos);
    actSectPos += sec.m_textLength;
  }
  std::vector<int> pagesBreak;
  pagesBreak.assign(set.begin(), set.end());

  // now associates the header/footer to each pages
  int nPages = m_state->m_numPages = int(pagesBreak.size());
  int actPagePos = 0;
  actPage = 0;
  actSectPos = 0;
  for (auto &sec : m_state->m_sectionsList) {
    std::vector<int> listPages;
    actSectPos += sec.m_textLength;
    while (actPagePos < actSectPos) {
      listPages.push_back(actPage);
      if (actPage >= nPages-1 || pagesBreak[size_t(actPage+1)] > actSectPos)
        break;
      actPagePos=pagesBreak[size_t(++actPage)];
    }
    int headerId = 0, footerId = 0;
    for (int k = 0; k < 2; ++k) {
      if (sec.m_headerIds[k])
        headerId = sec.m_headerIds[k];
      if (sec.m_footerIds[k])
        footerId = sec.m_footerIds[k];
    }
    if (!headerId && !footerId) continue;
    for (auto p : listPages) {
      ++p;
      if (headerId && m_state->m_headersMap.find(p) == m_state->m_headersMap.end())
        m_state->m_headersMap[p] = headerId;
      if (footerId)
        m_state->m_footersMap[p] = footerId;
    }
  }
  // finally mark the attachment
  auto const &listCalled = m_mainParser.getGraphicIdCalledByToken();
  for (auto id : listCalled) {
    if (m_state->m_idGraphicMap.find(id) == m_state->m_idGraphicMap.end()) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::buildPageStructures: can not find attachment block %d...\n",
                      id));
      continue;
    }
    auto graphic = m_state->m_idGraphicMap.find(id)->second;
    graphic->m_attachment = true;
  }
}

////////////////////////////////////////////////////////////
// try to find the main text zone and sent it
void MacWrtProStructures::buildTableStructures()
{
  size_t numGraphics = m_state->m_graphicsList.size();
  for (size_t i = 0; i < numGraphics; ++i) {
    if (m_state->m_graphicsList[i]->m_type != 3)
      continue;
    auto table = m_state->m_graphicsList[i];
    std::vector<std::shared_ptr<MacWrtProStructuresInternal::Graphic> > graphicList;
    size_t j = i+1;
    for (; j < numGraphics; ++j) {
      auto cell = m_state->m_graphicsList[j];
      if (cell->m_type != 4)
        break;
      if (!table->contains(cell->m_box))
        break;
      bool ok = true;
      for (auto graphic : graphicList) {
        if (cell->intersects(graphic->m_box)) {
          ok = false;
          break;
        }
      }
      if (!ok)
        break;
      graphicList.push_back(cell);
    }
    if (j-1 >= i) i = j-1;

    size_t numCells = graphicList.size();
    bool ok = numCells > 1;
    if (!ok && numCells == 1)
      ok = table->m_col == 1 && table->m_row == 1;
    if (!ok) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::buildTableStructures: find a table with %ld cells : ignored...\n", long(numCells)));
      continue;
    }

    std::shared_ptr<MacWrtProStructuresInternal::Table> newTable(new MacWrtProStructuresInternal::Table);
    for (auto graphic : graphicList) {
      graphic->m_send = true;
      graphic->m_attachment = true;
      graphic->m_textboxCellType=1;
      newTable->add(std::shared_ptr<MacWrtProStructuresInternal::Cell>
                    (new MacWrtProStructuresInternal::Cell(*this, graphic.get())));
    }
    m_state->m_tablesMap[table->m_id]=newTable;
  }
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read the font names
bool MacWrtProStructures::readFontsName(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  int vers = version();
  long endPos = pos+4+sz;
  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readFontsName: file is too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(FontsName):";
  auto N=static_cast<int>(input->readULong(2));
  if (3*N+2 > sz) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readFontsName: can not read the number of fonts\n"));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    f << "#";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  for (int ft = 0; ft < N; ++ft) {
    auto fId = static_cast<int>(input->readLong(2));
    f << "[id=" << fId << ",";
    for (int st = 0; st < 2; ++st) {
      auto sSz = static_cast<int>(input->readULong(1));
      if (long(input->tell())+sSz > endPos) {
        MWAW_DEBUG_MSG(("MacWrtProStructures::readFontsName: can not read the %d font\n", ft));
        f << "#";
        break;
      }
      std::string name("");
      for (int i = 0; i < sSz; ++i)
        name += char(input->readULong(1));
      if (name.length()) {
        if (st == 0)
          m_parserState->m_fontConverter->setCorrespondance(fId, name);
        f << name << ",";
      }
      if (vers)
        break;
    }
    f << "],";
  }

  if (long(input->tell()) != endPos)
    ascFile.addDelimiter(input->tell(),'|');
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// read the character properties
bool MacWrtProStructures::readFontsDef(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  long endPos = pos+4+sz;
  int expectedSize = version()==0 ? 10 : 20;
  if ((sz%expectedSize) != 0 || !stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readFontsDef: find an odd value for sz\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  f << "Entries(FontsDef):";
  auto N = int(sz/expectedSize);
  f << "N=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->m_fontsList.resize(0);
  for (int n = 0; n < N; ++n) {
    pos = input->tell();
    MacWrtProStructuresInternal::Font font;
    if (!readFont(stream, font)) {
      ascFile.addPos(pos);
      ascFile.addNote("FontsDef-#");
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      return true;
    }
    m_state->m_fontsList.push_back(font);
    f.str("");
    f << "FontsDef-C" << n << ":";
    f << font.m_font.getDebugString(m_parserState->m_fontConverter) << font << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool MacWrtProStructures::readFont(std::shared_ptr<MWAWStream> &stream, MacWrtProStructuresInternal::Font &font)
{
  auto &input = stream->m_input;
  long pos = input->tell();
  int vers = version();
  libmwaw::DebugStream f;
  font = MacWrtProStructuresInternal::Font();
  font.m_values[0] = static_cast<int>(input->readLong(2)); // 1, 3 or 6
  auto val = static_cast<int>(input->readULong(2));
  if (val != 0xFFFF)
    font.m_font.setId(val);
  val = static_cast<int>(input->readULong(2));
  if (val != 0xFFFF)
    font.m_font.setSize(float(val)/4.f);
  if (vers >= 1)
    font.m_values[1] = static_cast<int>(input->readLong(2));
  auto flag = long(input->readULong(2));
  uint32_t flags=0;
  if (flag&0x1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x8) flags |= MWAWFont::embossBit;
  if (flag&0x10) flags |= MWAWFont::shadowBit;
  if (flag&0x20) font.m_font.set(MWAWFont::Script(40,librevenge::RVNG_PERCENT));
  if (flag&0x40) font.m_font.set(MWAWFont::Script(-40,librevenge::RVNG_PERCENT));
  if (flag&0x100) font.m_font.set(MWAWFont::Script::super());
  if (flag&0x200) font.m_font.setStrikeOutStyle(MWAWFont::Line::Simple);
  if (flag&0x400) flags |= MWAWFont::uppercaseBit;
  if (flag&0x800) flags |= MWAWFont::smallCapsBit;
  if (flag&0x1000) font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x2000) {
    font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.m_font.setUnderlineType(MWAWFont::Line::Double);
  }
  if (flag&0x4000) flags |= MWAWFont::lowercaseBit;
  font.m_flags = (flag&0x8080L);

  auto color = static_cast<int>(input->readULong(1));
  MWAWColor col;
  if (color != 1 && getColor(color, col))
    font.m_font.setColor(col);
  else if (color != 1)
    f << "#colId=" << color << ",";
  val = static_cast<int>(input->readULong(1)); // always 0x64 (unused?)
  if (val != 0x64) font.m_values[2] = val;
  if (vers == 1) {
    auto lang = static_cast<int>(input->readLong(2));
    switch (lang) {
    case 0:
      font.m_font.setLanguage("en_US");
      break;
    case 2:
      font.m_font.setLanguage("en_GB");
      break;
    case 3:
      font.m_font.setLanguage("de");
      break;
    default:
      f << "#lang=" << lang << ",";
      break;
    }
    font.m_token = static_cast<int>(input->readLong(2));
    auto spacings = static_cast<int>(input->readLong(2));
    if (spacings) {
      if (spacings < -50 || spacings > 100) {
        MWAW_DEBUG_MSG(("MacWrtProStructures::readFont: character spacings seems odd\n"));
        f << "#spacings=" << spacings << "%,";
        spacings = spacings < 0 ? -50 : 100;
      }
      float fSz = font.m_font.size();
      if (fSz <= 0) {
        MWAW_DEBUG_MSG(("MacWrtProStructures::readFont: expand called without fSize, assume 12pt\n"));
        fSz = 12;
      }
      font.m_font.setDeltaLetterSpacing(fSz*float(spacings)/100.f);
    }
    for (int i = 4; i < 5; ++i)
      font.m_values[i] = static_cast<int>(input->readLong(2));
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  font.m_font.setFlags(flags);
  font.m_font.m_extra = f.str();

  return true;
}

////////////////////////////////////////////////////////////
// read a paragraph and a list of paragraph
bool MacWrtProStructures::readParagraphs(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;
  int dataSz = version()==0 ? 202 : 192;

  auto sz = long(input->readULong(4));
  if (sz == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  long endPos = pos+sz;
  if ((sz%dataSz) != 0 || !stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readParagraphs: find an odd value for sz\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  f << "Entries(ParaZone):";
  auto N = int(sz/dataSz);
  f << "N=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->m_paragraphsList.resize(0);
  for (int n = 0; n < N; ++n) {
    pos = input->tell();
    auto val = static_cast<int>(input->readLong(2));
    f.str("");
    f << "Entries(Paragraph)[" << n << "]:";
    if (val) f << "used?="<<val <<",";
    MacWrtProStructuresInternal::Paragraph para;
    if (!readParagraph(stream, para)) {
      f << "#";
      m_state->m_paragraphsList.push_back(MacWrtProStructuresInternal::Paragraph());
      input->seek(pos+dataSz, librevenge::RVNG_SEEK_SET);
    }
    else {
      f << para;
      m_state->m_paragraphsList.push_back(para);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool MacWrtProStructures::readParagraph(std::shared_ptr<MWAWStream> &stream, MacWrtProStructuresInternal::Paragraph &para)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  libmwaw::DebugStream f;
  int vers = version();
  long pos = input->tell(), endPos = pos+(vers == 0 ? 200: 190);
  para = MacWrtProStructuresInternal::Paragraph();

  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readParagraph: file is too short\n"));
    return false;
  }
  int val, just = 0;
  if (vers == 0) {
    just = static_cast<int>(input->readULong(2));
    val = static_cast<int>(input->readLong(2));
    if (val) f << "unkn=" << val << ",";
  }
  para.m_margins[1] = double(input->readLong(4))/72.0/65536.;
  para.m_margins[0] = double(input->readLong(4))/72.0/65536.;
  para.m_margins[2] = double(input->readLong(4))/72.0/65536.;


  float spacings[3];
  for (auto &spacing : spacings) spacing = float(input->readLong(4))/65536.f;
  for (int i = 0; i < 3; ++i) {
    int dim = vers==0 ? static_cast<int>(input->readLong(4)) : static_cast<int>(input->readULong(1));
    bool inPoint = true;
    bool ok = true;
    switch (dim) {
    case 0: // point
      ok = spacings[i] < 721 && (i || spacings[0] > 0);
      spacings[i]/=72.f;
      break;
    case -1:
    case 0xFF: // percent
      ok = (spacings[i] >= 0 && spacings[i]<46);
      if (i==0) spacings[i]+=1.0f;
      inPoint=false;
      break;
    default:
      f << "#inter[dim]=" << std::hex << dim << std::dec << ",";
      ok = spacings[i] < 721 && (i || spacings[0] > 0);
      spacings[i]/=72.f;
      break;
    }
    if (ok) {
      if (i == 0 && inPoint) {
        if (spacings[0] > 0)
          para.setInterline(double(spacings[0]), librevenge::RVNG_INCH, MWAWParagraph::AtLeast);
        else if (spacings[0] < 0) f << "interline=" << spacings[0] << ",";
        continue;
      }
      para.m_spacings[i] = double(spacings[i]);
      if (inPoint && spacings[i] > 1) {
        MWAW_DEBUG_MSG(("MacWrtProStructures::readParagraph: spacings looks big decreasing it\n"));
        f << "#prevSpacings" << i << "=" << spacings[i] << ",";
        para.m_spacings[i] = 1.0;
      }
      else if (!inPoint && i && (spacings[i]<0 || spacings[i]>0)) {
        if (i==1) f << "spaceBef";
        else f  << "spaceAft";
        f << "=" << spacings[i] << "%,";
        /** seems difficult to set bottom a percentage of the line unit,
            so do the strict minimum... */
        *(para.m_spacings[i]) *= 10./72.;
      }
    }
    else
      f << "#spacings" << i << ",";
  }

  if (vers==1) {
    just = static_cast<int>(input->readULong(1));
    input->seek(pos+28, librevenge::RVNG_SEEK_SET);
  }
  else {
    ascFile.addDelimiter(input->tell(),'|');
  }
  /* Note: when no extra tab the justification,
           if there is a extra tab, this corresponds to the extra tab alignment :-~ */
  switch (just & 0x3) {
  case 0:
    break;
  case 1:
    para.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationRight;
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    break;
  default:
    break;
  }
  if (just & 0x40)
    para.m_breakStatus = MWAWParagraph::NoBreakWithNextBit;
  if (just & 0x80)
    para.m_breakStatus = para.m_breakStatus.get()|MWAWParagraph::NoBreakBit;
  if (just&0x3C) f << "#justify=" << std::hex << (just&0x3C) << std::dec << ",";
  for (int i = 0; i < 20; ++i) {
    pos = input->tell();
    MWAWTabStop newTab;
    auto type = static_cast<int>(input->readULong(1));
    switch (type & 3) {
    case 0:
      break;
    case 1:
      newTab.m_alignment = MWAWTabStop::CENTER;
      break;
    case 2:
      newTab.m_alignment = MWAWTabStop::RIGHT;
      break;
    case 3:
      newTab.m_alignment = MWAWTabStop::DECIMAL;
      break;
    default:
      break;
    }
    if (type & 0xfc) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readParagraph: tab type is odd\n"));
      f << "tabs" << i << "[#type]=" << std::hex << (type & 0xFc) << std::dec << ",";
    }
    auto leader = static_cast<int>(input->readULong(1));
    if (leader != 0x20)
      newTab.m_leaderCharacter = static_cast<uint16_t>(leader);
    unsigned long tabPos = input->readULong(4);
    if (tabPos == 0xFFFFFFFFL) { // no more tab
      ascFile.addDelimiter(pos,'|');
      break;
    }
    newTab.m_position = double(tabPos)/72./65536.;
    auto decimalChar = static_cast<int>(input->readULong(1));
    if (decimalChar && decimalChar != '.')
      newTab.m_decimalCharacter=static_cast<uint16_t>(decimalChar);
    val = static_cast<int>(input->readLong(1)); // always 0?
    if (val)
      f << "tab" << i << "[#unkn=" << std::hex << val << std::dec << "],";
    para.m_tabs->push_back(newTab);
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
  }

  if (vers==1) {
    input->seek(endPos-2, librevenge::RVNG_SEEK_SET);
    para.m_value = static_cast<int>(input->readLong(2));
  }
  para.m_extra=f.str();

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read the styles ( character )
bool MacWrtProStructures::readCharStyles(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;
  int vers = version();

  int N;
  int expectedSz = 0x42;
  if (version() == 1) {
    auto sz = long(input->readULong(4));
    if ((sz%0x42) != 0) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readCharStyles: find an odd value for sz=%ld\n",sz));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    N = int(sz/0x42);
  }
  else {
    N = static_cast<int>(input->readULong(2));
    expectedSz = 0x2a;
  }

  if (N == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  long actPos = input->tell();
  long endPos = actPos+long(N)*expectedSz;

  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readCharStyles: file is too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(CharStyles):N=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i = 0; i < N; ++i) {
    pos = input->tell();
    f.str("");
    f << "CharStyles-" << i << ":";
    auto sSz = static_cast<int>(input->readULong(1));
    if (sSz > 31) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readCharStyles: string size seems odd\n"));
      sSz = 31;
      f << "#";
    }
    std::string name("");
    for (int c = 0; c < sSz; ++c)
      name += char(input->readULong(1));
    f << name << ",";
    input->seek(pos+32, librevenge::RVNG_SEEK_SET);

    if (vers == 1) {
      auto val = static_cast<int>(input->readLong(2));
      if (val) f << "unkn0=" << val << ",";
      val = static_cast<int>(input->readLong(2));
      if (val != -1) f << "unkn1=" << val << ",";
      f << "date=" << MacWrtProParser::convertDateToDebugString(unsigned(input->readULong(4))) << ","; // unsure
      val = static_cast<int>(input->readLong(2)); // small number between 0 and 2 (nextId?)
      if (val) f << "f0=" << val << ",";
      for (int j = 1; j < 5; ++j) { // [-1,0,1], [0,1 or ee], 0, 0
        val = static_cast<int>(input->readLong(1));
        if (val) f << "f" << j <<"=" << val << ",";
      }
    }
    MacWrtProStructuresInternal::Font font;
    if (!readFont(stream, font)) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readCharStyles: can not read the font\n"));
      f << "###";
    }
    else
      f << font.m_font.getDebugString(m_parserState->m_fontConverter) << font << ",";

    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    input->seek(pos+expectedSz, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the styles ( paragraph + font)
bool MacWrtProStructures::readStyles(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;
  auto sz = long(input->readULong(4));
  if ((sz%0x106) != 0) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readStyles: find an odd value for sz=%ld\n",sz));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  auto N = int(sz/0x106);

  if (N==0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }

  f << "Entries(Style):";
  f << "N=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i = 0; i < N; ++i) {
    pos = input->tell();
    if (!readStyle(stream, i)) {
      f.str("");
      f << "#Style-" << i << ":";
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
  }
  ascFile.addPos(input->tell());
  ascFile.addNote("_");

  return true;
}

bool MacWrtProStructures::readStyle(std::shared_ptr<MWAWStream> &stream, int styleId)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long debPos = input->tell(), pos = debPos;
  libmwaw::DebugStream f;
  // checkme something is odd here
  long dataSz = 0x106;
  long endPos = pos+dataSz;
  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readStyle: file is too short\n"));
    return false;
  }
  f << "Style-" << styleId << ":";
  auto strlen = static_cast<int>(input->readULong(1));
  if (!strlen || strlen > 31) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readStyle: style name length seems bad!!\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  std::string name("");
  for (int i = 0; i < strlen; ++i) // default
    name+=char(input->readULong(1));
  f << name << ",";
  input->seek(pos+32, librevenge::RVNG_SEEK_SET); // probably end of name

  int val;
  for (int i = 0; i < 3; ++i) { // 0 | [0,1,-1] | numTabs or idStyle?
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  f << "date=" << MacWrtProParser::convertDateToDebugString(unsigned(input->readULong(4))) << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();

  f.str("");
  f << "Entries(Paragraph)[" << styleId << "]:";
  MacWrtProStructuresInternal::Paragraph para;
  if (!readParagraph(stream, para)) {
    f << "#";
    input->seek(pos+190, librevenge::RVNG_SEEK_SET);
  }
  else
    f << para;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();
  f.str("");
  f << "Style-" << styleId << "(II):";
  val = static_cast<int>(input->readLong(2));
  if (val != -1) f << "nextId?=" << val << ",";
  val = static_cast<int>(input->readLong(1)); // -1 0 or 1
  if (val) f << "f0=" << val << ",";
  for (int i = 1; i < 4; ++i) { // 0, then 0|1
    val = static_cast<int>(input->readLong(i==3?1:2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();
  MacWrtProStructuresInternal::Font font;
  if (!readFont(stream, font)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readStyle: end of style seems bad\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Style:end###");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }

  f.str("");
  f << "FontsDef:";
  f << font.m_font.getDebugString(m_parserState->m_fontConverter) << font << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();

  f.str("");
  f << "Style-" << styleId << "(end):";
  val = static_cast<int>(input->readLong(2));
  if (val!=-1) f << "unkn=" << val << ",";

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read the pages
bool MacWrtProStructures::readPagesListII(std::shared_ptr<MWAWStream> const &stream, int numPages)
{
  auto input=stream->m_input;
  long pos = input->tell();
  if (!stream->checkPosition(pos+50)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readPagesListII: can not find the page zone\n"));
    return false;
  }
  m_state->m_pagesList.resize(2+size_t(numPages));
  for (size_t p=0; p<2+size_t(numPages); ++p) {
    pos=input->tell();
    if (!readPageII(stream, int(p), m_state->m_pagesList[p])) {
      m_state->m_pagesList.resize(p);
      input->seek(pos,librevenge::RVNG_SEEK_SET);
      break;
    }
  }
  return true;
}

bool MacWrtProStructures::readPageII(std::shared_ptr<MWAWStream> const &stream, int wh, MacWrtProStructuresInternal::Page &page)
{
  auto input=stream->m_input;
  auto &ascFile=stream->m_ascii;
  long pos = input->tell();
  if (!stream->checkPosition(pos+12+66)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readPageII: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  std::string name;
  for (int i=0; i<4; ++i) {
    auto c=char(input->readULong(1));
    if (!c) break;
    name+=c;
  }
  if (!name.empty()) f << "name=" << name << ",";
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);

  page=MacWrtProStructuresInternal::Page();
  page.m_page = static_cast<int>(input->readLong(2));
  long val = input->readLong(1); // always -1 ?
  if (val>=1 && val<=5) {
    char const *what[]= {nullptr, "num", "Roman", "roman", "Alpha", "alpha"};
    f << "format=" << what[val] << ",";
  }
  else if (val != -1) f << "##format=" << val << ",";
  val = long(input->readULong(1)); // 0|80
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  for (int i = 0; i < 2; ++i) {
    int const expected[]= {0,1};
    val = input->readLong(2);
    if (val != expected[i]) f << "f" << i << "=" << val << ",";
  }
  page.m_extra=f.str();
  f.str("");
  f << "Entries(Page)[" << wh << "]:" << page;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  int k=100*wh;
  while (!input->isEnd()) {
    pos=input->tell();
    auto graphic=std::make_shared<MacWrtProStructuresInternal::Graphic>(0);
    if (!readGraphicII(stream, ++k, page.m_graphicsList.empty(), *graphic)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    page.m_graphicsList.push_back(graphic);
    if (graphic->m_lastFlag<0 || graphic->m_lastFlag>=2)
      break;
  }

  return true;
}

bool MacWrtProStructures::readGraphicII(std::shared_ptr<MWAWStream> const &stream, int wh, bool mainBlock, MacWrtProStructuresInternal::Graphic &graphic)
{
  auto input=stream->m_input;
  auto &ascFile=stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;
  if (!stream->checkPosition(pos+76))
    return false;
  auto type = static_cast<int>(input->readLong(1)); // fd or 3
  long val;
  if (type<=-0x10 || type>=0x10) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long const expectedWidth[]= {
    -1, -1, -1, 76,
      -1, -1, -1, -1,
      -1, -1, -1, -1,
      -1, -1, -1, -1
    };
  long const len = expectedWidth[std::abs(type)];
  if (len==-1) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphicII: unknown block %d\n", type));
    ascFile.addPos(pos);
    ascFile.addNote("Entries(Graphic):###");
    return false;
  }
  long endPos = pos+len;
  if (!stream->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  graphic=MacWrtProStructuresInternal::Graphic(0);
  graphic.m_type=type;
  type=std::abs(type);
  val = long(input->readULong(1)); // 0, 6a, 78, f2, fa : type ?
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  val = long(input->readULong(1));
  if (!mainBlock) {
    graphic.m_textboxType=int(val>>6);
    graphic.m_headerFooterFlag=int((val>>4)&3);
    val &= 0xf;
  }
  if (val) f << "f0=" << std::hex << val << std::dec << ",";
  f << "unkn0=[";
  for (int i = 0; i < 6; ++i) {
    val = long(input->readULong(i==0 ? 1 : 2));
    if (val==0) f << "_,";
    else f << std::hex << val << std::dec << ",";
  }
  f << "],";
  f << "unkn1=[";
  for (int i = 0; i < 3; ++i) { // big number ptr?, junk
    auto lVal = long(input->readULong(4));
    if (lVal==0) f << "_,";
    else f << std::hex << lVal << std::dec << ",";
  }
  f << "],";
  graphic.m_fileBlock = static_cast<int>(input->readULong(2));
  float dim[4];
  for (auto &d : dim) d = float(input->readLong(2));
  graphic.m_box = MWAWBox2f(MWAWVec2f(dim[1],dim[0]), MWAWVec2f(dim[3],dim[2]));
  if (dim[0]>dim[2] || dim[1]>dim[3] || (dim[2]<=0 && dim[3]<=0)) {
    // bad box or (0,0,0,0) is clearly an indice of a problem
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    // 8000*4, probably the box decimal position ; unsure if 0x8000 means decal 0.5 pt or 0 pt, ...
    val = long(input->readULong(2));
    if (val != 0x8000) f << "g" << i+2 << "=" << float(val)/float(0x8000) << ",";
  }
  graphic.m_textPos = int(input->readULong(1))<<16;
  graphic.m_textPos += int(input->readULong(2));
  if (graphic.m_textPos) {
    // ok this is a soft page break block
    graphic.m_page = graphic.m_fileBlock;
    graphic.m_fileBlock = 0;
  }
  val = long(input->readULong(1));
  if (val) f << "g6=" << std::hex << val << std::dec << ",";
  f << "unkn=[";
  for (int i = 0; i < 3; ++i) { // often unkn[0]=unkn1[0] or unkn1[1], excepted when unkn1[0]==unkn1[1]==unkn1[2]
    auto lVal = long(input->readULong(4));
    if (lVal==0) f << "_,";
    else f << std::hex << lVal << std::dec << ",";
  }
  f << "],";
  graphic.m_column=int(input->readULong(1));
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  graphic.m_colSeparator=float(input->readULong(4))/float(0x10000);
  val=int(input->readLong(2)); // 0
  if (val) f << "h0=" << val << ",";
  val=int(input->readLong(2));
  if (val) f << "nextPage=" << val+1 << ",";
  auto ID=input->readULong(4); // 0 or big number if pageBreak
  if (ID) f << "ID=" << std::hex << ID << std::dec << ",";
  val=int(input->readLong(1)); // 0|ff
  if (val!=-1) f << "k1=" << val << ",";
  if (graphic.m_type==-3) endPos+=12;
  if (val!=0)
    endPos+=3;
  if (!stream->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  if (input->tell()!=endPos-1)
    ascFile.addDelimiter(input->tell(),'|');
  input->seek(endPos-1, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  graphic.m_lastFlag=int(input->readLong(1));
  if (graphic.m_lastFlag<0 || graphic.m_lastFlag>2) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphicII: unknown last value\n"));
    f << "###isLast=" << graphic.m_lastFlag << ",";
  }

  graphic.m_extra=f.str();
  f.str("");
  f << "Entries(Graphic)[" << wh << "]:" << graphic;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// read the list of blocks
bool MacWrtProStructures::readGraphicsList(std::shared_ptr<MWAWStream> &stream, int numPages)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  long endPos = pos+45;
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != endPos) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphicsList: file is too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(Graphic):";
  auto rev= static_cast<int>(input->readLong(4)); // 1 or 3
  f << "revision=" << rev << ",";
  auto uVal = input->readULong(4);
  if (uVal) f << "revision[min]=" << double(uVal)/60. << "',";
  long val;
  for (int i = 0; i < 4; ++i) { // [0|81|ff][0|03|33|63|ff][0|ff][0|ff]
    val = long(input->readULong(1));
    if (val) f << "flA" << i << "=" << std::hex << val << std::dec << ",";
  }
  val = input->readLong(4); // 0, 2, 46, 1479
  if (val) f << "f1=" << val << ",";
  for (int i = 0; i < 4; ++i) { // [0|1][0|74][0][0|4]
    val = long(input->readULong(1));
    if (val) f << "flB" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i = 2; i < 4; ++i) { // [0|72] [0|a]
    val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  val = long(input->readULong(4));
  if (val) f << "date=" << MacWrtProParser::convertDateToDebugString(unsigned(val)) << ",";

  std::string str;
  if (!readString(input, str))
    return false;
  if (str.length()) f << "dir='" << str << "',";
  val = input->readLong(2);
  if (val) f << "f4=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos = input->tell();
  if (!stream->checkPosition(pos+6)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphicsList: can not find the block zone\n"));
    return false;
  }
  f.str("");
  f << "Graphic-end:";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(6, librevenge::RVNG_SEEK_CUR);

  int actPage=0;
  while (actPage<=numPages) {
    pos=input->tell();
    auto graphic = readGraphic(stream);
    if (!graphic) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      if (stream->checkPosition(pos+2) && input->readULong(2)==0x7fff) {
        f.str("");
        f << "Graphic-Pg" << actPage++ << ",";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        continue;
      }
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    m_state->m_graphicsList.push_back(graphic);
    if (m_state->m_idGraphicMap.find(graphic->m_id) != m_state->m_idGraphicMap.end()) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphicsList: graphic %d already exists\n", graphic->m_id));
    }
    else
      m_state->m_idGraphicMap[graphic->m_id] = graphic;
    if (graphic->isGraphic() || graphic->isText())
      m_mainParser.parseDataZone(graphic->m_fileBlock, graphic->isGraphic() ? 1 : 0);
  }
  return true;
}

std::shared_ptr<MacWrtProStructuresInternal::Graphic> MacWrtProStructures::readGraphic(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;
  int type=int(input->readLong(2));
  if (type<-3 || type>2) { // normally -2..1
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return std::shared_ptr<MacWrtProStructuresInternal::Graphic>();
  }
  f << "type=" << type << ",";
  auto sz = long(input->readULong(4));
  // pat2*3?, dim[pt*65536], border[pt*65536], ?, [0|10|1c], 0, graphic?
  if (sz < 0x40) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return std::shared_ptr<MacWrtProStructuresInternal::Graphic>();
  }

  long endPos = pos+sz+6;
  if (!stream->checkPosition(endPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return std::shared_ptr<MacWrtProStructuresInternal::Graphic>();
  }

  auto graphic=std::make_shared<MacWrtProStructuresInternal::Graphic>(1);
  long val;
  f << "pat?=[" << std::hex;
  for (int i = 0; i < 2; ++i)
    f << input->readULong(2) << ",";
  f << std::dec << "],";
  graphic->m_type = static_cast<int>(input->readULong(2));
  float dim[4];
  for (auto &d : dim) d = float(input->readLong(4))/65536.f;
  graphic->m_box = MWAWBox2f(MWAWVec2f(dim[1],dim[0]), MWAWVec2f(dim[3],dim[2]));

  static int const wh[4] = { libmwaw::Top, libmwaw::Left, libmwaw::Bottom, libmwaw::Right };
  for (auto what : wh)
    graphic->m_borderWList[what]=double(input->readLong(4))/65536.;

  /* 4: pagebreak,
     5: text
     1: floating, 7: none(wrapping/attachment), b: attachment
     0/a: table ?
  */
  for (int i = 0; i < 2; ++i) {
    val = long(input->readULong(2));
    if (val)
      f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  val = input->readLong(2);
  if (val) f << "f0=" << val << ",";
  graphic->m_fileBlock = static_cast<int>(input->readLong(2));
  graphic->m_id = static_cast<int>(input->readLong(2));
  val = static_cast<int>(input->readLong(2)); // almost always 4 ( one time 0)
  if (val!=4)
    f << "bordOffset=" << val << ",";
  for (int i = 2; i < 7; ++i) {
    /* always 0, except f3=-1 (in one file),
       and in other file f4=1,f5=1,f6=1, */
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  graphic->m_baseline = float(input->readLong(4))/65536.f;
  auto colorId = static_cast<int>(input->readLong(2));
  auto patId=static_cast<int>(input->readLong(2));
  MWAWColor color(MWAWColor::white());
  if (getColor(colorId, patId, color))
    graphic->m_surfaceColor = color;
  else
    f << "#colorId=" << colorId << ", #patId=" << patId << ",";

  colorId = static_cast<int>(input->readLong(2));
  patId=static_cast<int>(input->readLong(2));
  if (getColor(colorId, patId, color))
    graphic->m_lineBorder.m_color = color;
  else
    f << "line[#colorId=" << colorId << ", #patId[line]=" << patId << "],";
  val = input->readLong(2);
  static double const w[9]= {0,0.5,1,2,4,6,8,10,12};
  if (val>0&&val<10)
    graphic->m_lineBorder.m_width = w[val-1];
  else
    f << "#lineWidth=" << val << ",";
  val = input->readLong(2);
  if (!m_state->updateLineType(static_cast<int>(val), graphic->m_lineBorder))
    f << "#line[type]=" << val << ",";
  auto contentType = static_cast<int>(input->readULong(1));
  switch (contentType) {
  case 0:
    graphic->m_contentType = MacWrtProStructuresInternal::Graphic::TEXT;
    break;
  case 1:
    graphic->m_contentType = MacWrtProStructuresInternal::Graphic::GRAPHIC;
    break;
  default:
    MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphic: find unknown block content type\n"));
    f << "#contentType=" << contentType << ",";
    break;
  }

  bool isNote = false;
  if (graphic->m_type==4 && sz == 0xa0) {
    // this can be a note, let check
    isNote=true;
    static double const expectedWidth[4] = {5,5,19,5};
    for (int i=0; i < 4; ++i) {
      if (graphic->m_borderWList[i]<expectedWidth[i] ||
          graphic->m_borderWList[i]>expectedWidth[i]) {
        isNote = false;
        break;
      }
    }
  }
  if (isNote) {
    long actPos = input->tell();
    ascFile.addDelimiter(pos+118,'|');
    input->seek(pos+118, librevenge::RVNG_SEEK_SET);
    val = input->readLong(2);
    isNote = val==0 || val==0x100;
    if (isNote) {
      float dim2[4];
      for (auto &d : dim2) {
        d = float(input->readLong(4))/65536.f;
        if (!val && (d<0||d>0)) {
          isNote = false;
          break;
        }
      }
      if (isNote && val) {
        // ok, reset the box only if it is bigger
        if (dim2[3]-dim2[1]>dim[3]-dim[1] && dim2[2]-dim2[0]>dim[2]-dim[0])
          graphic->m_box=MWAWBox2f(MWAWVec2f(dim2[1],dim2[0]),MWAWVec2f(dim2[3],dim2[2]));
      }
    }
    if (isNote) {
      graphic->m_contentType = MacWrtProStructuresInternal::Graphic::NOTE;
      // ok reset the border and the line color to gray
      for (int i = 0; i < 4; ++i) {
        if (i!=libmwaw::Top)
          graphic->m_borderWList[i]=1;
      }
      graphic->m_lineBorder=MWAWBorder();
      graphic->m_lineBorder.m_color=MWAWColor(128,128,128);

      if (val)
        f << "note[closed],";
      else
        f << "note,";
    }
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
  }
  else if (graphic->m_type==4 && sz == 0x9a) {
    long actPos = input->tell();
    ascFile.addDelimiter(pos+110,'|');
    input->seek(pos+110, librevenge::RVNG_SEEK_SET);
    libmwaw::DebugStream f2;
    for (int i=0; i<4; ++i) {
      MWAWBorder border;
      colorId = static_cast<int>(input->readLong(2));
      patId=static_cast<int>(input->readLong(2));
      f2.str("");
      if (getColor(colorId, patId, color))
        border.m_color=color;
      else
        f2 << "#colorId=" << colorId << ", #patId=" << patId << ",";
      val= input->readLong(2);
      if (val > 0 && val < 10)
        border.m_width=w[val-1];
      else
        f2 << "#w[line]=" << val << ",";
      val= input->readLong(2);
      if (!m_state->updateLineType(static_cast<int>(val), border))
        f2 << "#border[type]=" << val << ",";
      val=input->readLong(2);
      if (int(val)!=i)
        f2 << "#id=" << val << ",";
      border.m_extra = f2.str();
      graphic->m_borderCellList[wh[i]]=border;
    }
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
  }

  graphic->m_extra = f.str();

  f.str("");
  f << "Graphic-B" << m_state->m_graphicsList.size() << ":" << *graphic;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (long(input->tell()) != endPos)
    ascFile.addDelimiter(input->tell(), '|');

  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  // ok now read the end of the header
  pos = input->tell();
  if (stream->checkPosition(pos+6)) {
    f.str("");
    f << "Graphic-data-B" << m_state->m_graphicsList.size()<< "[" << graphic->m_type << "]:";
    switch (graphic->m_type) {
    case 3: { // table
      graphic->m_row = static_cast<int>(input->readLong(2));
      graphic->m_col = static_cast<int>(input->readLong(2));
      f << "numRow=" << graphic->m_row << ",";
      f << "numCol=" << graphic->m_col << ",";
      break;
    }
    case 4: { // cell/textbox : no sure it contain data?
      val =  input->readLong(2); // always 0 ?
      if (val) f << "f0=" << val << ",";
      val = long(input->readULong(2)); // [0|10|1e|10c0|1cc0|a78a|a7a6|d0c0|dcc0]
      if (val) f << "fl?=" << std::hex << val << std::dec << ",";
      break;
    }
    case 5: { // text or ?
      bool emptyBlock = graphic->m_fileBlock <= 0;
      val = long(input->readULong(2));  // always 0 ?
      if (emptyBlock) {
        if (val & 0xFF00)
          f << "#f0=" << val << ",";
        graphic->m_textPos=int(((val&0xFF)<<16) | static_cast<int>(input->readULong(2)));
        f << "posC=" << graphic->m_textPos << ",";
      }
      else if (val) f << "f0=" << val << ",";
      val = long(input->readULong(2)); // 30c0[normal], 20c0|0[empty]
      f << "fl?=" << std::hex << val << ",";
      break;
    }
    case 6: {
      for (int i = 0; i < 4; ++i) { // [10|d0],40, 0, 0
        val = long(input->readULong(1));
        f << "f" << i << "=" << val << ",";
      }
      val =  input->readLong(1);
      switch (val) {
      case 1:
        f << "header,";
        graphic->m_isHeader = true;
        break;
      case 2:
        f << "footer,";
        graphic->m_isHeader = false;
        break;
      default:
        MWAW_DEBUG_MSG(("MacWrtProStructures::readGraphic: find unknown header/footer type\n"));
        f << "#type=" << val << ",";
        break;
      }
      val =  input->readLong(1); // alway 1 ?
      if (val != 1) f << "f4=" << val << ",";
      break;
    }
    case 7: { // footnote: something here ?
      for (int i = 0; i < 3; ++i) { // 0, 0, [0|4000]
        val = long(input->readULong(2));
        f << "f" << i << "=" << std::hex << val << std::dec << ",";
      }
      break;
    }
    case 8:
      break; // graphic: clearly nothing
    default:
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+6, librevenge::RVNG_SEEK_SET);
  }

  return graphic;
}

////////////////////////////////////////////////////////////
// read the column information zone : checkme
bool MacWrtProStructures::readSections(std::shared_ptr<MWAWStream> &stream, std::vector<MacWrtProStructuresInternal::Section> &sections)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz == 0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  long endPos = pos+4+sz;
  if ((sz%0xd8)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readSections: find an odd value for sz\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Entries(Sections)#");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readSections: section is outside of the input\n"));
    return true;
  }

  auto N = int(sz/0xd8);
  f << "Entries(Section):";
  f << "N=" << N;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int n = 0; n < N; ++n) {
    MacWrtProStructuresInternal::Section sec;
    pos = input->tell();
    f.str("");
    sec.m_textLength = long(input->readULong(4));
    long val =  input->readLong(4); // almost always 0 or a dim?
    if (val) f << "dim?=" << float(val)/65536.f << ",";
    auto startWay = static_cast<int>(input->readLong(2));
    switch (startWay) {
    case 1:
      sec.m_start = sec.S_Line;
      break;
    case 2:
      sec.m_start = sec.S_Page;
      break;
    case 3:
      sec.m_start = sec.S_PageLeft;
      break;
    case 4:
      sec.m_start = sec.S_PageRight;
      break;
    default:
      MWAW_DEBUG_MSG(("MacWrtProStructures::readSections: find an odd value for start\n"));
      f << "#start=" << startWay << ",";
    }
    val = input->readLong(2);
    if (val)
      f << "f0=" << val << ",";
    // a flag ? and noused ?
    for (int i = 0; i < 2; ++i) {
      val = long(input->readULong(1));
      if (val == 0xFF) f << "fl" << i<< "=true,";
      else if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
    }

    for (int st = 0; st < 2; ++st) {
      val = input->readLong(2); // alway 1 ?
      if (val != 1) f << "f" << 1+st << "=" << val << ",";
      // another flag ?
      val = long(input->readULong(1));
      if (val) f << "fl" << st+2 << "=" << std::hex << val << std::dec << ",";
    }
    auto numColumns = static_cast<int>(input->readLong(2));
    if (numColumns < 1 || numColumns > 20) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::readSections: bad number of columns\n"));
      f << "#nCol=" << numColumns << ",";
      numColumns = 1;
    }
    val = input->readLong(2); // find: 3, c, 24
    if (val) f << "f3=" << val << ",";
    for (int i = 4; i < 7; ++i) { // always 0 ?
      val = input->readLong(2);
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    long actPos = input->tell();
    for (int c = 0; c < 2*numColumns; ++c)
      sec.m_colsPos.push_back(float(input->readLong(4))/65536.f);
    input->seek(actPos+20*8+4, librevenge::RVNG_SEEK_SET);
    // 5 flags ( 1+unused?)
    for (int i = 0; i < 6; ++i) {
      val = long(input->readULong(1));
      if ((i!=5 && val!=1) || (i==5 && val))
        f << "g" << i << "=" << val << ",";
    }
    for (int st = 0; st < 2; ++st) { // pair, unpair?
      for (int i = 0; i < 2; ++i) { // header/footer
        val = input->readLong(2);
        if (val)
          f << "#h" << 2*st+i << "=" << val << ",";

        val = input->readLong(2);
        if (i==0) sec.m_headerIds[st] = static_cast<int>(val);
        else sec.m_footerIds[st] = static_cast<int>(val);
      }
    }
    sec.m_extra=f.str();
    sections.push_back(sec);

    f.str("");
    f << "Section" << "-" << n << ":" << sec;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+0xd8, librevenge::RVNG_SEEK_SET);
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read the selection zone
bool MacWrtProStructures::readSelection(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  long endPos = pos+14;
  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readSelection: file is too short\n"));
    return false;
  }
  f << "Entries(Selection):";
  auto val = static_cast<int>(input->readLong(2));
  f << "f0=" << val << ","; // zone?
  val = static_cast<int>(input->readLong(4)); // -1, 0 or 8 : zone type?
  if (val == -1 || val == 0) { // checkme: none ?
    f << "*";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+6, librevenge::RVNG_SEEK_SET);
    return true;
  }
  if (val!=8) f << "f1=" << val << ",";
  f << "char=";
  for (int i = 0; i < 2; ++i) {
    f << input->readULong(4);
    if (i==0) f << "x";
    else f << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read a string
bool MacWrtProStructures::readString(MWAWInputStreamPtr input, std::string &res)
{
  res="";
  long pos = input->tell();
  auto sz = static_cast<int>(input->readLong(2));
  if (sz == 0) return true;
  if (sz < 0) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("MacWrtProStructures::readString: odd value for size\n"));
    return false;
  }
  if (!input->checkPosition(pos+sz+2)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("MacWrtProStructures::readString: file is too short\n"));
    return false;
  }
  for (int i= 0; i < sz; ++i) {
    auto c = char(input->readULong(1));
    if (c) {
      res+=c;
      continue;
    }
    if (i==sz-1) break;

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("MacWrtProStructures::readString: find odd character in string\n"));
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////
// read an unknown zone
bool MacWrtProStructures::readStructB(std::shared_ptr<MWAWStream> &stream)
{
  auto &input = stream->m_input;
  auto &ascFile = stream->m_ascii;
  long pos = input->tell();
  libmwaw::DebugStream f;

  auto N = static_cast<int>(input->readULong(2));
  if (N==0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
    return true;
  }
  f << "Entries(StructB):N=" << N << ",";

  // CHECKME: find N=2 only one time ( and across a checksum zone ...)
  long endPos = pos+N*10+6;
  if (!stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::readZonB: file is too short\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  auto val = static_cast<int>(input->readULong(2));
  if (val != 0x2af8)
    f << "f0=" << std::hex << val << std::dec << ",";
  val = static_cast<int>(input->readULong(2));
  if (val) f << "f1=" << val << ",";

  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int n = 0; n < N; ++n) {
    pos = input->tell();
    f.str("");
    f << "StructB" << "-" << n;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+10, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
// check if a block is sent
bool MacWrtProStructures::isSent(int graphicId)
{
  if (m_state->m_idGraphicMap.find(graphicId) == m_state->m_idGraphicMap.end()) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::isSent: can not find the block %d\n", graphicId));
    return true;
  }
  return m_state->m_idGraphicMap.find(graphicId)->second->m_send;
}

////////////////////////////////////////////////////////////
// send a block
bool MacWrtProStructures::send(int graphicId, bool mainZone)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (version()==0) {
    if (m_state->m_idGraphicMap.find(graphicId)==m_state->m_idGraphicMap.end()) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::send: can not find the graphic %d\n", graphicId));
      return false;
    }
    MWAWVec2i const gId(graphicId, 0);
    if (m_state->m_graphicsSendSet.find(gId)!=m_state->m_graphicsSendSet.end()) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::send: oops find a loop for %d\n", graphicId));
      return false;
    }

    m_state->m_graphicsSendSet.insert(gId);
    auto graphic=m_state->m_idGraphicMap.find(graphicId)->second;
    graphic->m_send = true;
    if (graphic->m_fileBlock>0)
      m_mainParser.sendTextZone(graphic->m_fileBlock, mainZone);
    m_state->m_graphicsSendSet.insert(gId);

    return true;
  }
  if (m_state->m_idGraphicMap.find(graphicId) == m_state->m_idGraphicMap.end()) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::send: can not find the block %d\n", graphicId));
    return false;
  }
  auto graphic = m_state->m_idGraphicMap.find(graphicId)->second;
  MWAWVec2i graphicIdMain(graphicId, (mainZone ? 1 : 0) + (graphic->m_textboxCellType ? 2 : 0)
                          +(!graphic->m_attachment ? 4 : 0));
  if (m_state->m_graphicsSendSet.find(graphicIdMain)!=m_state->m_graphicsSendSet.end()) {
    MWAW_DEBUG_MSG(("MacWrtProStructures::send: oops find a loop for %d\n", graphicId));
    return false;
  }
  m_state->m_graphicsSendSet.insert(graphicIdMain);
  graphic->m_send = true;
  if (graphic->m_type == 4 && graphic->m_textboxCellType == 0) {
    graphic->m_textboxCellType = 2;
    MWAWGraphicStyle style;
    graphic->fillFrame(style);
    m_mainParser.sendTextBoxZone(graphicId, graphic->getPosition(), style);
    graphic->m_textboxCellType = 0;
  }
  else if (graphic->isText())
    m_mainParser.sendTextZone(graphic->m_fileBlock, mainZone);
  else if (graphic->isGraphic()) {
    MWAWGraphicStyle style;
    graphic->fillFrame(style);
    m_mainParser.sendPictureZone(graphic->m_fileBlock, graphic->getPosition(), style);
  }
  else if (graphic->m_type == 3) {
    if (m_state->m_tablesMap.find(graphicId) == m_state->m_tablesMap.end()) {
      MWAW_DEBUG_MSG(("MacWrtProStructures::send: can not find table with id=%d\n", graphicId));
    }
    else {
      bool needTextBox = listener && !graphic->m_attachment && graphic->m_textboxCellType == 0;
      if (needTextBox) {
        graphic->m_textboxCellType = 2;
        m_mainParser.sendTextBoxZone(graphicId, graphic->getPosition());
      }
      else {
        auto table = m_state->m_tablesMap.find(graphicId)->second;
        if (!table->sendTable(listener))
          table->sendAsText(listener);
        graphic->m_textboxCellType = 0;
      }
    }
  }
  else if (graphic->m_type == 4 || graphic->m_type == 6) {
    // probably ok, can be an empty cell, textbox, header/footer ..
    if (listener) listener->insertChar(' ');
  }
  else if (graphic->m_type == 8) {   // empty frame
    MWAWGraphicStyle style;
    graphic->fillFrame(style);
    m_mainParser.sendEmptyFrameZone(graphic->getPosition(), style);
  }
  else {
    MWAW_DEBUG_MSG(("MacWrtProStructures::send: can not send block with type=%d\n", graphic->m_type));
  }
  m_state->m_graphicsSendSet.erase(graphicIdMain);
  return true;
}

////////////////////////////////////////////////////////////
// send the not sent data
void MacWrtProStructures::flushExtra()
{
  int vers = version();
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (listener && listener->isSectionOpened()) {
    listener->closeSection();
    listener->openSection(MWAWSection());
  }
  if (version()==0) {
#ifdef DEBUG
    bool const checkHFs=true;
#else
    bool const checkHFs=false;
#endif
    for (size_t i=checkHFs ? 0 : 2; i<m_state->m_pagesList.size(); ++i) {
      auto const &page = m_state->m_pagesList[i];
      for (auto const &graphic : page.m_graphicsList) {
        if (graphic->m_send || graphic->m_fileBlock<=0 || graphic->m_textPos>0) continue;
        int const id=1000;
        m_state->m_idGraphicMap[id]=graphic;
        send(id);
      }
    }
  }
  else {
    // first send the text
    for (size_t i = 0; i < m_state->m_graphicsList.size(); ++i) {
      auto graphic = m_state->m_graphicsList[i];
      if (graphic->m_send)
        continue;
      if (graphic->m_type == 6) {
        /* Fixme: macwritepro can have one header/footer by page and one by default.
           For the moment, we only print the first one :-~ */
        MWAW_DEBUG_MSG(("MacWrtProStructures::flushExtra: find some header/footer\n"));
        continue;
      }
      int id = vers == 0 ? int(i) : graphic->m_id;
      if (graphic->isText()) {
        // force to non floating position
        graphic->m_attachment = true;
        send(id);
        if (listener) listener->insertEOL();
      }
      else if (graphic->m_type == 3) {
        // force to non floating position
        graphic->m_attachment = true;
        send(id);
      }
    }
    // then send graphic
    for (auto graphic : m_state->m_graphicsList) {
      if (graphic->m_send)
        continue;
      if (graphic->isGraphic()) {
        // force to non floating position
        graphic->m_attachment = true;
        send(graphic->m_id);
      }
    }
  }
}

////////////////////////////////////////////////////////////
// interface with the listener
MacWrtProStructuresListenerState::MacWrtProStructuresListenerState(std::shared_ptr<MacWrtProStructures> const &structures, bool mainZone, int version)
  : m_isMainZone(mainZone)
  , m_version(version)
  , m_actPage(0)
  , m_actTab(0)
  , m_numTab(0)
  , m_section(0)
  , m_numCols(1)
  , m_newPageDone(false)
  , m_structures(structures)
{
  if (!m_structures) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::MacWrtProStructuresListenerState can not find structures parser\n"));
    return;
  }
  if (mainZone) {
    newPage();
    sendSection(0);
  }
}

MacWrtProStructuresListenerState::~MacWrtProStructuresListenerState()
{
}

bool MacWrtProStructuresListenerState::isSent(int graphicId)
{
  if (!m_structures) return false;
  return m_structures->isSent(graphicId);
}

bool MacWrtProStructuresListenerState::send(int graphicId)
{
  m_newPageDone = false;
  if (!m_structures) return false;
  int oldNumTab=m_numTab;
  bool ok=m_structures->send(graphicId);
  m_numTab=oldNumTab;
  return ok;
}

void MacWrtProStructuresListenerState::insertSoftPageBreak()
{
  if (m_newPageDone) return;
  newPage(true);
}

bool MacWrtProStructuresListenerState::newPage(bool softBreak)
{
  if (!m_structures || !m_isMainZone) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::newPage: can not create a new page\n"));
    return false;
  }

  // first send all the floating data
  if (m_actPage == 0) {
    for (auto graphic : m_structures->m_state->m_graphicsList) {
      if (graphic->m_send || graphic->m_attachment) continue;
      if (graphic->m_type != 3 && graphic->m_type != 4 && graphic->m_type != 8) continue;
      m_structures->send(graphic->m_id);
    }
  }

  m_structures->m_mainParser.newPage(++m_actPage, softBreak);
  m_actTab=0;
  m_newPageDone = true;
  double colSep;
  if (m_version==0 && m_structures->m_mainParser.numColumns(colSep)>1 && m_actPage>1) {
    if (!softBreak || (m_actPage==2&&m_structures->m_mainParser.hasTitlePage())) {
      MWAWTextListenerPtr listener=m_structures->getTextListener();
      if (listener->isSectionOpened())
        listener->closeSection();
      sendSection(++m_section);
    }
  }
  return true;
}

std::vector<int> MacWrtProStructuresListenerState::getPageBreaksPos() const
{
  std::vector<int> res;
  if (!m_structures || !m_isMainZone) return res;
  if (m_version==0) {
    for (size_t i=2 ; i<m_structures->m_state->m_pagesList.size(); ++i) {
      for (auto const &graphic : m_structures->m_state->m_pagesList[i].m_graphicsList) {
        if (graphic->m_textboxType!=0)
          continue;
        if (graphic->m_textPos) res.push_back(graphic->m_textPos);
      }
    }
  }
  else {
    for (auto graphic : m_structures->m_state->m_graphicsList) {
      if (graphic->m_type != 5) continue;
      if (graphic->m_textPos) res.push_back(graphic->m_textPos);
    }
  }
  return res;
}

// ----------- character function ---------------------
void MacWrtProStructuresListenerState::sendChar(char c)
{
  if (!m_structures) return;
  bool newPageDone = m_newPageDone;
  m_newPageDone = false;
  MWAWTextListenerPtr listener=m_structures->getTextListener();
  if (!listener) return;
  switch (c) {
  case 0:
    break; // ignore
  case 3: // footnote ok
  case 4: // figure ok
  case 5: // hyphen ok
    break;
  case 7:
    if (m_version==0) {
      m_actTab = 0;
      listener->insertEOL(true);
    }
    else {
      MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendChar: Find odd char 0x7\n"));
    }
    break;
  case 0x9:
    if ((m_actTab++ < m_numTab) || m_actTab!=1)
      listener->insertTab();
    else // this case appears in list, 2.\tItem..., we do not always want a tab here
      listener->insertChar(' ');
    break;
  case 0xa:
    m_actTab = 0;
    if (newPageDone) break;
    listener->insertEOL();
    break; // soft break
  case 0xd:
    m_actTab = 0;
    if (newPageDone) break;
    listener->insertEOL();
    break;
  case 0xc:
    m_actTab = 0;
    if (m_isMainZone) newPage();
    break;
  case 0xb: // add a columnbreak
    m_actTab = 0;
    if (m_isMainZone) {
      if (m_numCols <= 1) newPage();
      else if (listener)
        listener->insertBreak(MWAWTextListener::ColumnBreak);
    }
    break;
  case 0xe:
    m_actTab = 0;
    if (!m_isMainZone) break;

    // create a new section here
    if (listener->isSectionOpened())
      listener->closeSection();
    sendSection(++m_section);
    break;
  case 2: // for MWII
  case 0x15:
  case 0x17:
  case 0x1a:
    break;
  case 0x1f: // some hyphen
    break;
  /* 0x10 and 0x13 : bad character which can happen in conversion */
  default:
    listener->insertCharacter(static_cast<unsigned char>(c));
    break;
  }
}

// ----------- font function ---------------------
bool MacWrtProStructuresListenerState::sendFont(int id)
{
  if (!m_structures) return false;
  if (!m_structures->getTextListener()) return true;
  if (id < 0 || id >= int(m_structures->m_state->m_fontsList.size())) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendFont: can not find font %d\n", id));
    return false;
  }
  m_structures->getTextListener()->setFont(m_structures->m_state->m_fontsList[size_t(id)].m_font);

  return true;
}

// ----------- paragraph function ---------------------
bool MacWrtProStructuresListenerState::sendParagraph(int id)
{
  if (!m_structures) return false;
  if (!m_structures->getTextListener()) return true;
  if (id < 0 || id >= int(m_structures->m_state->m_paragraphsList.size())) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendParagraph: can not find paragraph %d\n", id));
    return false;
  }
  auto const &para=m_structures->m_state->m_paragraphsList[size_t(id)];
  m_structures->getTextListener()->setParagraph(para);
  m_numTab = int(para.m_tabs->size());
  return true;
}

// ----------- section function ---------------------
void MacWrtProStructuresListenerState::sendSection(int nSection)
{
  if (!m_structures) return;
  MWAWTextListenerPtr listener=m_structures->getTextListener();
  if (!listener) return;
  if (listener->isSectionOpened()) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendSection: a section is already opened\n"));
    listener->closeSection();
  }
  if (m_version==0) {
    double colSep=0.16666;
    m_numCols = (nSection==0 && m_structures->m_mainParser.hasTitlePage()) ? 1 : m_structures->m_mainParser.numColumns(colSep);
    if (m_numCols > 10) {
      MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendSection: num columns is to big, reset to 1\n"));
      m_numCols = 1;
    }
    MWAWSection sec;
    if (m_numCols>1)
      sec.setColumns(m_numCols, (m_structures->m_mainParser.getPageWidth()-colSep*double(m_numCols-1))/double(m_numCols), librevenge::RVNG_INCH, colSep);
    listener->openSection(sec);
    return;
  }

  if (nSection >= int(m_structures->m_state->m_sectionsList.size())) {
    MWAW_DEBUG_MSG(("MacWrtProStructuresListenerState::sendSection: can not find section %d\n", nSection));
    return;
  }
  auto const &section = m_structures->m_state->m_sectionsList[size_t(nSection)];
  if (nSection && section.m_start != section.S_Line) newPage();

  listener->openSection(section.getSection());
  m_numCols = listener->getSection().numColumns();
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
