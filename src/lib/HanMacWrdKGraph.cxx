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

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicEncoder.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWTextListener.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "HanMacWrdKParser.hxx"

#include "HanMacWrdKGraph.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a HanMacWrdKGraph */
namespace HanMacWrdKGraphInternal
{
////////////////////////////////////////
//! Internal: the frame header of a HanMacWrdKGraph
struct Frame {
  //! constructor
  Frame()
    : m_type(-1)
    , m_fileId(-1)
    , m_fileSubId(-1)
    , m_id(-1)
    , m_page(0)
    , m_pos()
    , m_baseline(0.f)
    , m_posFlags(0)
    , m_style()
    , m_borderType(0)
    , m_inGroup(false)
    , m_parsed(false)
    , m_extra("")
  {
  }
  Frame(Frame const &)=default;
  //! destructor
  virtual ~Frame();
  //! return the frame bdbox
  MWAWBox2f getBdBox() const
  {
    MWAWVec2f minPt(m_pos[0][0], m_pos[0][1]);
    MWAWVec2f maxPt(m_pos[1][0], m_pos[1][1]);
    for (int c=0; c<2; ++c) {
      if (m_pos.size()[c]>=0) continue;
      minPt[c]=m_pos[1][c];
      maxPt[c]=m_pos[0][c];
    }
    return MWAWBox2f(minPt,maxPt);
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Frame const &grph);
  //! the graph type
  int m_type;
  //! the file id
  long m_fileId;
  //! the file sub id
  long m_fileSubId;
  //! the local id
  int m_id;
  //! the page
  int m_page;
  //! the position
  MWAWBox2f m_pos;
  //! the baseline
  float m_baseline;
  //! the graph anchor flags
  int m_posFlags;
  //! the style
  MWAWGraphicStyle m_style;
  //! the border type
  int m_borderType;
  //! the border default size (before using width), 0 means Top, other unknown
  MWAWVec2f m_borders[4];
  //! true if the frame is a child of a group
  bool m_inGroup;
  //! true if we have send the data
  mutable bool m_parsed;
  //! an extra string
  std::string m_extra;
};

Frame::~Frame()
{
}

std::ostream &operator<<(std::ostream &o, Frame const &grph)
{
  switch (grph.m_type) {
  case 0: // main text
    break;
  case 1:
    o << "header,";
    break;
  case 2:
    o << "footer,";
    break;
  case 3:
    o << "footnote[frame],";
    break;
  case 4:
    o << "textbox,";
    break;
  case 6:
    o << "picture,";
    break;
  case 8:
    o << "basicGraphic,";
    break;
  case 9:
    o << "table,";
    break;
  case 10:
    o << "comments,"; // memo
    break;
  case 11:
    o << "group";
    break;
  default:
    o << "#type=" << grph.m_type << ",";
    break;
  case -1:
    break;
  }
  if (grph.m_fileId > 0)
    o << "fileId="  << std::hex << grph.m_fileId << std::dec << ",";
  if (grph.m_id>0)
    o << "id=" << grph.m_id << ",";
  if (grph.m_page) o << "page=" << grph.m_page+1  << ",";
  o << "pos=" << grph.m_pos << ",";
  if (grph.m_baseline < 0 || grph.m_baseline>0) o << "baseline=" << grph.m_baseline << ",";
  if (grph.m_inGroup) o <<  "inGroup,";
  int flag = grph.m_posFlags;
  if (flag & 4) o << "wrap=around,"; // else overlap
  if (flag & 0x40) o << "lock,";
  if (!(flag & 0x80)) o << "transparent,"; // else opaque
  if (flag & 0x39) o << "posFlags=" << std::hex << (flag & 0x39) << std::dec << ",";
  o << "style=[" << grph.m_style << "],";
  if (grph.m_borderType) o << "bord[type]=" << grph.m_borderType << ",";
  for (int i = 0; i < 4; ++i) {
    if (grph.m_borders[i].x() > 0 || grph.m_borders[i].y() > 0)
      o << "border" << i << "=" << grph.m_borders[i] << ",";
  }
  o << grph.m_extra;
  return o;
}

////////////////////////////////////////
//! Internal: the geometrical graph of a HanMacWrdKGraph
struct ShapeGraph final : public Frame {
  //! constructor
  explicit ShapeGraph(Frame const &orig)
    : Frame(orig)
    , m_shape()
  {
  }
  //! destructor
  ~ShapeGraph() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ShapeGraph const &graph)
  {
    o << graph.print() << ",";
    o << static_cast<Frame const &>(graph);
    return o;
  }
  //! return the current style
  MWAWGraphicStyle getStyle() const
  {
    MWAWGraphicStyle style(m_style);
    if (m_shape.m_type!=MWAWGraphicShape::Line)
      style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow();
    return style;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    s << m_shape;
    return s.str();
  }

  //! the shape m_shape
  MWAWGraphicShape m_shape;
};

ShapeGraph::~ShapeGraph()
{
}

////////////////////////////////////////
//! Internal: the footnote of a HanMacWrdKGraph
struct FootnoteFrame final : public Frame {
  //! constructor
  explicit FootnoteFrame(Frame const &orig)
    : Frame(orig)
    , m_textFileId(-1)
    , m_textFileSubId(0)
  {
  }
  //! destructor
  ~FootnoteFrame() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, FootnoteFrame const &ftn)
  {
    o << ftn.print();
    o << static_cast<Frame const &>(ftn);
    return o;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_textFileId>0)
      s << "textFileId=" << std::hex << m_textFileId << "[" << m_textFileSubId << std::dec << "],";
    return s.str();
  }
  //! the text file id
  long m_textFileId;
  //! the text file subId
  long m_textFileSubId;

};

FootnoteFrame::~FootnoteFrame()
{
}

////////////////////////////////////////
//! Internal: the group of a HanMacWrdKGraph
struct Group final : public Frame {
  struct Child;
  //! constructor
  explicit Group(Frame const &orig)
    : Frame(orig)
    , m_childsList()
  {
  }
  //! destructor
  ~Group() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Group const &group)
  {
    o << group.print();
    o << static_cast<Frame const &>(group);
    return o;
  }
  //! print local data
  std::string print() const;
  //! the list of child
  std::vector<Child> m_childsList;
  //! struct to store child data in HanMacWrdKGraphInternal::Group
  struct Child {
    //! constructor
    Child()
      : m_fileId(-1)
    {
      for (auto &val : m_values) val=0;
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Child const &ch)
    {
      if (ch.m_fileId > 0)
        o << "fileId="  << std::hex << ch.m_fileId << std::dec << ",";
      for (int i = 0; i < 2; ++i) {
        if (ch.m_values[i] == 0)
          continue;
        o << "f" << i << "=" << ch.m_values[i] << ",";
      }
      return o;
    }
    //! the child id
    long m_fileId;
    //! two values
    int m_values[2];
  };
};

Group::~Group()
{
}

std::string Group::print() const
{
  std::stringstream s;
  for (size_t i = 0; i < m_childsList.size(); ++i)
    s << "chld" << i << "=[" << m_childsList[i] << "],";
  return s.str();
}

////////////////////////////////////////
//! Internal: the picture of a HanMacWrdKGraph
struct PictureFrame final : public Frame {
  //! constructor
  explicit PictureFrame(Frame const &orig)
    : Frame(orig)
    , m_pictureType(0)
    , m_dim(0,0)
    , m_borderDim(0,0)
  {
    for (auto &val : m_values) val=0;
  }
  //! destructor
  ~PictureFrame() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, PictureFrame const &picture)
  {
    o << picture.print();
    o << static_cast<Frame const &>(picture);
    return o;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_pictureType) s << "type?=" << m_pictureType << ",";
    if (m_dim[0] || m_dim[1])
      s << "dim?=" << m_dim << ",";
    if (m_borderDim[0] > 0 || m_borderDim[1] > 0)
      s << "borderDim?=" << m_borderDim << ",";
    for (int i = 0; i < 7; ++i) {
      if (m_values[i]) s << "f" << i << "=" << m_values[i];
    }
    return s.str();
  }

  //! a type
  int m_pictureType;
  //! a dim?
  MWAWVec2i m_dim;
  //! the border dim?
  MWAWVec2f m_borderDim;
  //! some unknown int
  int m_values[7];
};

PictureFrame::~PictureFrame()
{
}

////////////////////////////////////////
//! a table cell in a table in HanMacWrdKGraph
struct TableCell final : public MWAWCell {
  //! constructor
  TableCell()
    : MWAWCell()
    , m_id(-1)
    , m_fileId(-1)
    , m_flags(0)
    , m_extra("")
  {
  }
  //! call when the content of a cell must be send
  bool sendContent(MWAWListenerPtr listener, MWAWTable &table) final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, TableCell const &cell);
  //! the cell id ( corresponding to the last data in the main zones list )
  long m_id;
  //! the file id
  long m_fileId;
  //! the cell data
  int m_flags;
  //! extra data
  std::string m_extra;
};

std::ostream &operator<<(std::ostream &o, TableCell const &cell)
{
  o << static_cast<MWAWCell const &>(cell);
  if (cell.m_flags&0x10) o << "lock,";
  if (cell.m_flags&0xFFE2)
    o << "linesFlags=" << std::hex << (cell.m_flags&0xFFE2) << std::dec << ",";
  if (cell.m_id > 0)
    o << "cellId="  << std::hex << cell.m_id << std::dec << ",";
  if (cell.m_fileId > 0)
    o << "fileId="  << std::hex << cell.m_fileId << std::dec << ",";
  o << cell.m_extra;
  return o;
}

////////////////////////////////////////
//! Internal: the table of a HanMacWrdKGraph
struct Table final : public Frame, public MWAWTable {
  //! constructor
  Table(Frame const &orig, HanMacWrdKGraph &parser)
    : Frame(orig)
    , MWAWTable(MWAWTable::CellPositionBit|MWAWTable::SizeBit)
    , m_parser(&parser)
    , m_rows(0)
    , m_columns(0)
    , m_numCells(0)
    , m_textFileId(-1)
  {
  }
  //! destructor
  ~Table() final;
  //! return the i^th table cell
  TableCell *get(int i)
  {
    auto cell=MWAWTable::get(i);
    return static_cast<TableCell *>(cell.get());
  }
  //! send a text zone
  bool sendText(long textId, long id) const
  {
    return m_parser->sendText(textId, id);
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Table const &table)
  {
    o << table.print();
    o << static_cast<Frame const &>(table);
    return o;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_rows)
      s << "nRows="  << m_rows << ",";
    if (m_columns)
      s << "nColumns="  << m_columns << ",";
    if (m_numCells)
      s << "nCells="  << m_numCells << ",";
    if (m_textFileId>0)
      s << "textFileId=" << std::hex << m_textFileId << std::dec << ",";
    return s.str();
  }
  //! the graph parser
  HanMacWrdKGraph *m_parser;
  //! the number of row
  int m_rows;
  //! the number of columns
  int m_columns;
  //! the number of cells
  int m_numCells;
  //! the text file id
  long m_textFileId;
private:
  Table(Table const &orig) = delete;
  Table &operator=(Table const &orig) = delete;
};

Table::~Table()
{
}

////////////////////////////////////////
//! Internal: the textbox of a HanMacWrdKGraph
struct TextBox final : public Frame {
  //! constructor
  TextBox(Frame const &orig, bool isComment)
    : Frame(orig)
    , m_commentBox(isComment)
    , m_textFileId(-1)
    , m_linkedIdList()
    , m_isLinked(false)
  {
    for (auto &d : m_dim) d = 0;
  }
  //! destructor
  ~TextBox() final;
  //! returns true if the box is linked to other textbox
  bool isLinked() const
  {
    return !m_linkedIdList.empty() || m_isLinked;
  }
  //! add property to frame extra values
  void addTo(MWAWGraphicStyle &style) const
  {
    if (m_type == 10) {
      MWAWBorder border;
      border.m_width=double(m_style.m_lineWidth);
      border.m_color = m_style.m_lineColor;
      style.setBorders(libmwaw::LeftBit|libmwaw::BottomBit|libmwaw::RightBit, border);
      border.m_width=double(m_borders[0][1]*m_style.m_lineWidth);
      style.setBorders(libmwaw::TopBit, border);
    }
    else if (m_style.hasLine()) {
      MWAWBorder border;
      border.m_width=double(m_style.m_lineWidth);
      border.m_color=m_style.m_lineColor;
      switch (m_borderType) {
      case 0: // solid
        break;
      case 1:
        border.m_type = MWAWBorder::Double;
        break;
      case 2:
        border.m_type = MWAWBorder::Double;
        border.m_widthsList.resize(3,1.);
        border.m_widthsList[0]=2.0;
        break;
      case 3:
        border.m_type = MWAWBorder::Double;
        border.m_widthsList.resize(3,1.);
        border.m_widthsList[2]=2.0;
        break;
      default:
        MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::TextBox::addTo: unexpected type\n"));
        break;
      }
      style.setBorders(15, border);
    }
    // now the link
    if (m_type==4 && m_isLinked) {
      librevenge::RVNGString fName;
      fName.sprintf("Frame%ld", m_fileId);
      style.m_frameName=fName.cstr();
    }
    if (m_type==4 && !m_linkedIdList.empty()) {
      librevenge::RVNGString fName;
      fName.sprintf("Frame%ld", m_linkedIdList[0]);
      style.m_frameNextName=fName.cstr();
    }
    if (m_style.hasSurfaceColor())
      style.setBackgroundColor(m_style.m_surfaceColor);
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, TextBox const &textbox)
  {
    o << textbox.print();
    o << static_cast<Frame const &>(textbox);
    return o;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_dim[0] > 0 || m_dim[1] > 0)
      s << "commentsDim2=" << m_dim[0] << "x" << m_dim[1] << ",";
    if (m_textFileId>0)
      s << "textFileId=" << std::hex << m_textFileId << std::dec << ",";
    if (!m_linkedIdList.empty()) {
      s << "link[to]=[";
      for (auto &id : m_linkedIdList)
        s << std::hex << id << std::dec << ",";
      s << "],";
    }
    return s.str();
  }

  //! a flag to know if this is a comment textbox
  bool m_commentBox;
  //! the text file id
  long m_textFileId;
  //! two auxilliary dim for memo textbox
  float m_dim[2];
  //! the list of linked remaining textbox id
  std::vector<long> m_linkedIdList;
  //! a flag to know if this textbox is linked to a previous box
  bool m_isLinked;
};

TextBox::~TextBox()
{
}

bool TableCell::sendContent(MWAWListenerPtr, MWAWTable &table)
{
  if (m_id < 0)
    return true;
  return static_cast<Table &>(table).sendText(m_fileId, m_id);
}

////////////////////////////////////////
//! Internal: the picture of a HanMacWrdKGraph
struct Picture {
  //! constructor
  explicit Picture(std::shared_ptr<HanMacWrdKZone> const &zone)
    : m_zone(zone)
    , m_fileId(-1)
    , m_fileSubId(-1)
    , m_parsed(false)
    , m_extra("")
  {
    for (auto &pos : m_pos) pos=0;
  }
  //! destructor
  ~Picture()
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Picture const &pict)
  {
    if (pict.m_fileId >= 0)
      o << "fileId="  << std::hex << pict.m_fileId << std::dec << ",";
    o << pict.m_extra;
    return o;
  }
  //! the main zone
  std::shared_ptr<HanMacWrdKZone> m_zone;
  //! the first and last position of the picture data in the zone
  long m_pos[2];
  //! the file id
  long m_fileId;
  //! the file subid
  long m_fileSubId;
  //! a flag to know if the picture was send to the receiver
  mutable bool m_parsed;
  //! extra data
  std::string m_extra;
};

////////////////////////////////////////
//! Internal: the pattern of a HanMacWrdKGraph
struct Pattern final : public MWAWGraphicStyle::Pattern {
  //! constructor ( 4 int by patterns )
  explicit Pattern(uint16_t const *pat=nullptr)
    : MWAWGraphicStyle::Pattern()
    , m_percent(0)
  {
    if (!pat) return;
    m_colors[0]=MWAWColor::white();
    m_colors[1]=MWAWColor::black();
    m_dim=MWAWVec2i(8,8);
    m_data.resize(8);
    for (size_t i=0; i < 4; ++i) {
      uint16_t val=pat[i];
      m_data[2*i]=static_cast<unsigned char>(val>>8);
      m_data[2*i+1]=static_cast<unsigned char>(val&0xFF);
    }
    int numOnes=0;
    for (size_t j=0; j < 8; ++j) {
      auto val=static_cast<uint8_t>(m_data[j]);
      for (int b=0; b < 8; b++) {
        if (val&1) ++numOnes;
        val = uint8_t(val>>1);
      }
    }
    m_percent=float(numOnes)/64.f;
  }
  Pattern(Pattern const &)=default;
  Pattern &operator=(Pattern const &)=default;
  Pattern &operator=(Pattern &&)=default;
  //! destructor
  ~Pattern() final;
  //! the percentage
  float m_percent;
};

Pattern::~Pattern()
{
}

////////////////////////////////////////
//! Internal: the state of a HanMacWrdKGraph
struct State {
  //! constructor
  State()
    : m_numPages(0)
    , m_framesMap()
    , m_picturesMap()
    , m_colorList()
    , m_patternList()
  {
  }
  //! returns a color correspond to an id
  bool getColor(int id, MWAWColor &col)
  {
    initColors();
    if (id < 0 || id >= int(m_colorList.size())) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::State::getColor: can not find color %d\n", id));
      return false;
    }
    col = m_colorList[size_t(id)];
    return true;
  }
  //! returns a pattern correspond to an id
  bool getPattern(int id, Pattern &pattern)
  {
    initPatterns();
    if (id < 0 || id >= int(m_patternList.size())) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::State::getPattern: can not find pattern %d\n", id));
      return false;
    }
    pattern = m_patternList[size_t(id)];
    return true;
  }

  //! returns a color corresponding to a pattern and a color
  static MWAWColor getColor(MWAWColor col, float pattern)
  {
    return MWAWColor::barycenter(pattern,col,1.f-pattern,MWAWColor::white());
  }

  //! init the color list
  void initColors();
  //! init the patterns' list
  void initPatterns();

  int m_numPages /* the number of pages */;
  //! a map fileId -> frame
  std::multimap<long, std::shared_ptr<Frame> > m_framesMap;
  //! a map fileId -> picture
  std::map<long, std::shared_ptr<Picture> > m_picturesMap;
  //! a list colorId -> color
  std::vector<MWAWColor> m_colorList;
  //! the patterns list
  std::vector<Pattern> m_patternList;
};

void State::initPatterns()
{
  if (m_patternList.size()) return;
  static uint16_t const s_pattern[4*64] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0xffff, 0x7fff, 0xffff, 0xf7ff, 0xffff, 0x7fff, 0xf7ff, 0x7fff, 0xf7ff,
    0xffee, 0xffbb, 0xffee, 0xffbb, 0x77dd, 0x77dd, 0x77dd, 0x77dd, 0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x8822, 0x8822, 0x8822, 0x8822,
    0xaa00, 0xaa00, 0xaa00, 0xaa00, 0xaa00, 0x4400, 0xaa00, 0x1100, 0x8800, 0xaa00, 0x8800, 0xaa00, 0x8800, 0x2200, 0x8800, 0x2200,
    0x8000, 0x0800, 0x8000, 0x0800, 0x8800, 0x0000, 0x8800, 0x0000, 0x8000, 0x0000, 0x0800, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
    0xeedd, 0xbb77, 0xeedd, 0xbb77, 0x3366, 0xcc99, 0x3366, 0xcc99, 0x1122, 0x4488, 0x1122, 0x4488, 0x8307, 0x0e1c, 0x3870, 0xe0c1,
    0x0306, 0x0c18, 0x3060, 0xc081, 0x0102, 0x0408, 0x1020, 0x4080, 0xffff, 0x0000, 0x0000, 0x0000, 0xff00, 0x0000, 0x0000, 0x0000,
    0x77bb, 0xddee, 0x77bb, 0xddee, 0x99cc, 0x6633, 0x99cc, 0x6633, 0x8844, 0x2211, 0x8844, 0x2211, 0xe070, 0x381c, 0x0e07, 0x83c1,
    0xc060, 0x3018, 0x0c06, 0x0381, 0x8040, 0x2010, 0x0804, 0x0201, 0xc0c0, 0xc0c0, 0xc0c0, 0xc0c0, 0x8080, 0x8080, 0x8080, 0x8080,
    0xffaa, 0xffaa, 0xffaa, 0xffaa, 0xe4e4, 0xe4e4, 0xe4e4, 0xe4e4, 0xffff, 0xff00, 0x00ff, 0x0000, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,
    0xff00, 0xff00, 0xff00, 0xff00, 0xff00, 0x0000, 0xff00, 0x0000, 0x8888, 0x8888, 0x8888, 0x8888, 0xff80, 0x8080, 0x8080, 0x8080,
    0x4ecf, 0xfce4, 0x473f, 0xf372, 0x6006, 0x36b1, 0x8118, 0x1b63, 0x2004, 0x4002, 0x1080, 0x0801, 0x9060, 0x0609, 0x9060, 0x0609,
    0x8814, 0x2241, 0x8800, 0xaa00, 0x2050, 0x8888, 0x8888, 0x0502, 0xaa00, 0x8000, 0x8800, 0x8000, 0x2040, 0x8000, 0x0804, 0x0200,
    0xf0f0, 0xf0f0, 0x0f0f, 0x0f0f, 0x0077, 0x7777, 0x0077, 0x7777, 0xff88, 0x8888, 0xff88, 0x8888, 0xaa44, 0xaa11, 0xaa44, 0xaa11,
    0x8244, 0x2810, 0x2844, 0x8201, 0x8080, 0x413e, 0x0808, 0x14e3, 0x8142, 0x2418, 0x1020, 0x4080, 0x40a0, 0x0000, 0x040a, 0x0000,
    0x7789, 0x8f8f, 0x7798, 0xf8f8, 0xf1f8, 0x6cc6, 0x8f1f, 0x3663, 0xbf00, 0xbfbf, 0xb0b0, 0xb0b0, 0xff80, 0x8080, 0xff08, 0x0808,
    0x1020, 0x54aa, 0xff02, 0x0408, 0x0008, 0x142a, 0x552a, 0x1408, 0x55a0, 0x4040, 0x550a, 0x0404, 0x8244, 0x3944, 0x8201, 0x0101
  };

  m_patternList.resize(64);
  for (size_t i=0; i < 64; ++i)
    m_patternList[i] = Pattern(&s_pattern[i*4]);
}

void State::initColors()
{
  if (m_colorList.size()) return;
  uint32_t const defCol[256] = {
    0x000000, 0xffffff, 0xffffcc, 0xffff99, 0xffff66, 0xffff33, 0xffff00, 0xffccff,
    0xffcccc, 0xffcc99, 0xffcc66, 0xffcc33, 0xffcc00, 0xff99ff, 0xff99cc, 0xff9999,
    0xff9966, 0xff9933, 0xff9900, 0xff66ff, 0xff66cc, 0xff6699, 0xff6666, 0xff6633,
    0xff6600, 0xff33ff, 0xff33cc, 0xff3399, 0xff3366, 0xff3333, 0xff3300, 0xff00ff,
    0xff00cc, 0xff0099, 0xff0066, 0xff0033, 0xff0000, 0xccffff, 0xccffcc, 0xccff99,
    0xccff66, 0xccff33, 0xccff00, 0xccccff, 0xcccccc, 0xcccc99, 0xcccc66, 0xcccc33,
    0xcccc00, 0xcc99ff, 0xcc99cc, 0xcc9999, 0xcc9966, 0xcc9933, 0xcc9900, 0xcc66ff,
    0xcc66cc, 0xcc6699, 0xcc6666, 0xcc6633, 0xcc6600, 0xcc33ff, 0xcc33cc, 0xcc3399,
    0xcc3366, 0xcc3333, 0xcc3300, 0xcc00ff, 0xcc00cc, 0xcc0099, 0xcc0066, 0xcc0033,
    0xcc0000, 0x99ffff, 0x99ffcc, 0x99ff99, 0x99ff66, 0x99ff33, 0x99ff00, 0x99ccff,
    0x99cccc, 0x99cc99, 0x99cc66, 0x99cc33, 0x99cc00, 0x9999ff, 0x9999cc, 0x999999,
    0x999966, 0x999933, 0x999900, 0x9966ff, 0x9966cc, 0x996699, 0x996666, 0x996633,
    0x996600, 0x9933ff, 0x9933cc, 0x993399, 0x993366, 0x993333, 0x993300, 0x9900ff,
    0x9900cc, 0x990099, 0x990066, 0x990033, 0x990000, 0x66ffff, 0x66ffcc, 0x66ff99,
    0x66ff66, 0x66ff33, 0x66ff00, 0x66ccff, 0x66cccc, 0x66cc99, 0x66cc66, 0x66cc33,
    0x66cc00, 0x6699ff, 0x6699cc, 0x669999, 0x669966, 0x669933, 0x669900, 0x6666ff,
    0x6666cc, 0x666699, 0x666666, 0x666633, 0x666600, 0x6633ff, 0x6633cc, 0x663399,
    0x663366, 0x663333, 0x663300, 0x6600ff, 0x6600cc, 0x660099, 0x660066, 0x660033,
    0x660000, 0x33ffff, 0x33ffcc, 0x33ff99, 0x33ff66, 0x33ff33, 0x33ff00, 0x33ccff,
    0x33cccc, 0x33cc99, 0x33cc66, 0x33cc33, 0x33cc00, 0x3399ff, 0x3399cc, 0x339999,
    0x339966, 0x339933, 0x339900, 0x3366ff, 0x3366cc, 0x336699, 0x336666, 0x336633,
    0x336600, 0x3333ff, 0x3333cc, 0x333399, 0x333366, 0x333333, 0x333300, 0x3300ff,
    0x3300cc, 0x330099, 0x330066, 0x330033, 0x330000, 0x00ffff, 0x00ffcc, 0x00ff99,
    0x00ff66, 0x00ff33, 0x00ff00, 0x00ccff, 0x00cccc, 0x00cc99, 0x00cc66, 0x00cc33,
    0x00cc00, 0x0099ff, 0x0099cc, 0x009999, 0x009966, 0x009933, 0x009900, 0x0066ff,
    0x0066cc, 0x006699, 0x006666, 0x006633, 0x006600, 0x0033ff, 0x0033cc, 0x003399,
    0x003366, 0x003333, 0x003300, 0x0000ff, 0x0000cc, 0x000099, 0x000066, 0x000033,
    0xee0000, 0xdd0000, 0xbb0000, 0xaa0000, 0x880000, 0x770000, 0x550000, 0x440000,
    0x220000, 0x110000, 0x00ee00, 0x00dd00, 0x00bb00, 0x00aa00, 0x008800, 0x007700,
    0x005500, 0x004400, 0x002200, 0x001100, 0x0000ee, 0x0000dd, 0x0000bb, 0x0000aa,
    0x000088, 0x000077, 0x000055, 0x000044, 0x000022, 0x000011, 0xeeeeee, 0xdddddd,
    0xbbbbbb, 0xaaaaaa, 0x888888, 0x777777, 0x555555, 0x444444, 0x222222, 0x111111,
  };
  m_colorList.resize(256);
  for (size_t i = 0; i < 256; ++i)
    m_colorList[i] = defCol[i];
}

////////////////////////////////////////
//! Internal: the subdocument of a HanMacWrdKGraph
class SubDocument final : public MWAWSubDocument
{
public:
  //! the document type
  enum Type { Picture, FrameInFrame, Group, Text, UnformattedTable, EmptyPicture };
  //! constructor
  SubDocument(HanMacWrdKGraph &pars, MWAWInputStreamPtr const &input, Type type, long id, long subId=0)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_graphParser(&pars)
    , m_type(type)
    , m_id(id)
    , m_subId(subId)
    , m_pos()
  {
  }

  //! constructor
  SubDocument(HanMacWrdKGraph &pars, MWAWInputStreamPtr const &input, MWAWPosition const &pos, Type type, long id, long subId=0)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_graphParser(&pars)
    , m_type(type)
    , m_id(id)
    , m_subId(subId)
    , m_pos(pos)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the graph parser */
  HanMacWrdKGraph *m_graphParser;
  //! the zone type
  Type m_type;
  //! the zone id
  long m_id;
  //! the zone subId ( for table cell )
  long m_subId;
  //! the position in a frame
  MWAWPosition m_pos;

private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (!m_graphParser) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  if (listener->getType()==MWAWListener::Graphic) {
    if (m_type!=Text) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::SubDocument::parse: unexpected type\n"));
      return;
    }
    m_graphParser->sendText(m_id, m_subId, listener);
  }
  else {
    switch (m_type) {
    case FrameInFrame:
      m_graphParser->sendFrame(m_id, m_pos);
      break;
    case Group:
      m_graphParser->sendGroup(m_id, m_pos);
      break;
    case Picture:
      m_graphParser->sendPicture(m_id, m_pos);
      break;
    case UnformattedTable:
      m_graphParser->sendTableUnformatted(m_id);
      break;
    case Text:
      m_graphParser->sendText(m_id, m_subId);
      break;
    case EmptyPicture:
      m_graphParser->sendEmptyPicture(m_pos);
      break;
#if !defined(__clang__)
    default:
      MWAW_DEBUG_MSG(("HanMacWrdKGraphInternal::SubDocument::parse: send type %d is not implemented\n", m_type));
      break;
#endif
    }
  }
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_graphParser != sDoc->m_graphParser) return true;
  if (m_type != sDoc->m_type) return true;
  if (m_id != sDoc->m_id) return true;
  if (m_subId != sDoc->m_subId) return true;
  if (m_pos != sDoc->m_pos) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
HanMacWrdKGraph::HanMacWrdKGraph(HanMacWrdKParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new HanMacWrdKGraphInternal::State)
  , m_mainParser(&parser)
{
}

HanMacWrdKGraph::~HanMacWrdKGraph()
{ }

int HanMacWrdKGraph::version() const
{
  return m_parserState->m_version;
}

bool HanMacWrdKGraph::getColor(int colId, int patternId, MWAWColor &color) const
{
  if (patternId==0) // ie. the empty pattern
    return false;
  if (!m_state->getColor(colId, color)) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::getColor: can not find color for id=%d\n", colId));
    return false;
  }
  HanMacWrdKGraphInternal::Pattern pattern;
  if (!m_state->getPattern(patternId, pattern)) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::getColor: can not find pattern for id=%d\n", patternId));
    return false;
  }
  color = m_state->getColor(color, pattern.m_percent);
  return true;
}

int HanMacWrdKGraph::numPages() const
{
  if (m_state->m_numPages)
    return m_state->m_numPages;
  int nPages = 0;
  for (auto fIt : m_state->m_framesMap) {
    if (!fIt.second) continue;
    int page = fIt.second->m_page+1;
    if (page <= nPages) continue;
    if (page >= nPages+100) continue; // a pb ?
    nPages = page;
  }
  m_state->m_numPages = nPages;
  return nPages;
}

bool HanMacWrdKGraph::sendText(long textId, long id, MWAWListenerPtr const &listener)
{
  return m_mainParser->sendText(textId, id, listener);
}

std::map<long,int> HanMacWrdKGraph::getTextFrameInformations() const
{
  std::map<long,int> mapIdType;
  for (auto fIt : m_state->m_framesMap) {
    if (!fIt.second) continue;
    auto const &frame = *fIt.second;
    std::vector<long> listId;

    if (frame.m_type!=3 && frame.m_type!=4 && frame.m_type != 9 && frame.m_type!=10)
      continue;
    switch (frame.m_type) {
    case 3:
      listId.push_back(static_cast<HanMacWrdKGraphInternal::FootnoteFrame const &>(frame).m_textFileId);
      break;
    case 4:
    case 10:
      listId.push_back(static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame).m_textFileId);
      break;
    case 9: {
      auto &table = const_cast<HanMacWrdKGraphInternal::Table &>
                    (static_cast<HanMacWrdKGraphInternal::Table const &>(frame));
      for (int c=0; c < table.numCells(); ++c) {
        if (table.get(c))
          listId.push_back(table.get(c)->m_fileId);
      }
      break;
    }
    default:
      break;
    }

    for (auto zId : listId) {
      if (mapIdType.find(zId) == mapIdType.end())
        mapIdType[zId] = frame.m_type;
      else if (mapIdType.find(zId)->second != frame.m_type) {
        MWAW_DEBUG_MSG(("HanMacWrdKGraph::getTextFrameInformations: id %lx already set\n", static_cast<long unsigned int>(zId)));
      }
    }
  }
  return mapIdType;
}
////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

// a small zone: related to frame pos + data?
bool HanMacWrdKGraph::readFrames(std::shared_ptr<HanMacWrdKZone> zone)
{
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readFrames: called without any zone\n"));
    return false;
  }

  long dataSz = zone->length();
  if (dataSz < 70) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readFrames: the zone seems too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = zone->m_input;
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  zone->m_parsed = true;
  long pos=0;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  long val;
  HanMacWrdKGraphInternal::Frame graph;
  graph.m_type = static_cast<int>(input->readULong(1));
  val = long(input->readULong(1));
  if (val) f << "#f0=" << std::hex << val << std::dec << ",";
  graph.m_posFlags = static_cast<int>(input->readULong(1));
  if (graph.m_posFlags&2) graph.m_inGroup=true;
  val = long(input->readULong(1));
  if (val) f << "#f1=" << std::hex << val << std::dec << ",";
  graph.m_page  = static_cast<int>(input->readLong(2));
  float dim[4];
  for (float &i : dim)
    i = float(input->readLong(4))/65536.f;
  graph.m_pos = MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));

  for (auto &border : graph.m_borders) {  // border size, 0=Top, other unknown
    float bd[2];
    for (auto &j : bd) j = float(input->readLong(4))/65536.f;
    border = MWAWVec2f(bd[0],bd[1]);
  }
  MWAWGraphicStyle &style = graph.m_style;
  style.m_lineWidth = float(input->readLong(4))/65536.f;
  graph.m_borderType= static_cast<int>(input->readULong(2));
  if (val) f << "#g0=" << val << ","; // border type?
  for (int i = 0; i < 2; ++i) {
    auto color = static_cast<int>(input->readULong(2));
    MWAWColor col;
    if (!m_state->getColor(color, col)) {
      f << "#color[" << i << "]=" << color << ", pat="<<input->readULong(2) << ",";
      continue;
    }
    auto pattern = static_cast<int>(input->readULong(2));
    if (pattern==0) {
      if (i==0)
        style.m_lineOpacity=0;
      else
        style.m_surfaceOpacity=0;
      continue;
    }
    HanMacWrdKGraphInternal::Pattern pat;
    if (m_state->getPattern(pattern, pat)) {
      pat.m_colors[1]=col;
      if (!pat.getUniqueColor(col)) {
        pat.getAverageColor(col);
        if (i) style.setPattern(pat);
      }
    }
    else
      f << "#pattern[" << i << "]=" << pattern << ",";
    if (i==0)
      style.m_lineColor=col;
    else
      style.setSurfaceColor(col,1);
  }
  graph.m_id=static_cast<int>(input->readLong(2));
  graph.m_baseline = float(input->readLong(4))/65536.f;
  for (int i = 1; i < 3; ++i) {
    val = long(input->readULong(2));
    if (val) f << "#g" << i << "=" << val << ",";
  }

  graph.m_extra=f.str();
  f.str("");
  f << zone->name() << "(A):PTR=" << std::hex << zone->fileBeginPos() << std::dec << "," << graph;
  graph.m_fileId = zone->m_id;
  graph.m_fileSubId = zone->m_subId;

  asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  std::shared_ptr<HanMacWrdKGraphInternal::Frame> frame;
  switch (graph.m_type) {
  case 3:
    frame = readFootnoteFrame(zone, graph);
    break;
  case 4:
  case 10:
    frame = readTextBox(zone, graph,graph.m_type==10);
    break;
  case 6:
    frame = readPictureFrame(zone, graph);
    break;
  case 8:
    frame = readShapeGraph(zone, graph);
    break;
  case 9:
    frame = readTable(zone, graph);
    break;
  case 11:
    frame = readGroup(zone, graph);
    break;
  default:
    break;
  }
  if (frame)
    m_state->m_framesMap.insert
    (std::multimap<long, std::shared_ptr<HanMacWrdKGraphInternal::Frame> >::value_type
     (zone->m_id, frame));
  return true;
}

// read a picture
bool HanMacWrdKGraph::readPicture(std::shared_ptr<HanMacWrdKZone> zone)
{
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: called without any zone\n"));
    return false;
  }

  long dataSz = zone->length();
  if (dataSz < 86) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: the zone seems too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = zone->m_input;
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  zone->m_parsed = true;

  std::shared_ptr<HanMacWrdKGraphInternal::Picture> picture(new HanMacWrdKGraphInternal::Picture(zone));

  long pos=0;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  picture->m_fileId = long(input->readULong(4));
  for (int i=0; i < 39; ++i) {
    long val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto pictSz = long(input->readULong(4));
  if (pictSz < 0 || pictSz+86 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: problem reading the picture size\n"));
    return false;
  }
  picture->m_pos[0] = input->tell();
  picture->m_pos[1] = picture->m_pos[0]+pictSz;
  picture->m_extra = f.str();
  long fId = picture->m_fileId;
  if (!fId) fId = zone->m_id;
  picture->m_fileSubId = zone->m_subId;
  if (m_state->m_picturesMap.find(fId) != m_state->m_picturesMap.end())
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: oops I already find a picture for %lx\n", static_cast<long unsigned int>(fId)));
  else
    m_state->m_picturesMap[fId] = picture;

  f.str("");
  f << zone->name() << ":PTR=" << std::hex << zone->fileBeginPos() << std::dec << "," << *picture;
  f << "pictSz=" << pictSz << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  asciiFile.skipZone(picture->m_pos[0], picture->m_pos[1]-1);

  return true;
}

////////////////////////////////////////////////////////////
// send data to a listener
////////////////////////////////////////////////////////////
bool HanMacWrdKGraph::sendPicture(long pictId, MWAWPosition const &pos)
{
  if (!m_parserState->m_textListener) return true;
  auto pIt = m_state->m_picturesMap.find(pictId);

  if (pIt == m_state->m_picturesMap.end() || !pIt->second) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendPicture: can not find the picture %lx\n", static_cast<long unsigned int>(pictId)));
    return false;
  }
  sendPicture(*pIt->second, pos);
  return true;
}

bool HanMacWrdKGraph::sendPicture(HanMacWrdKGraphInternal::Picture const &picture, MWAWPosition const &pos)
{
#ifdef DEBUG_WITH_FILES
  bool firstTime = picture.m_parsed == false;
#endif
  picture.m_parsed = true;
  if (!m_parserState->m_textListener) return true;

  if (!picture.m_zone || picture.m_pos[0] >= picture.m_pos[1]) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendPicture: can not find the picture\n"));
    return false;
  }

  MWAWInputStreamPtr input = picture.m_zone->m_input;

  librevenge::RVNGBinaryData data;
  input->seek(picture.m_pos[0], librevenge::RVNG_SEEK_SET);
  input->readDataBlock(picture.m_pos[1]-picture.m_pos[0], data);
#ifdef DEBUG_WITH_FILES
  if (firstTime) {
    libmwaw::DebugStream f;
    static int volatile pictName = 0;
    f << "Pict" << ++pictName << ".pct";
    libmwaw::Debug::dumpFile(data, f.str().c_str());
  }
#endif
  m_parserState->m_textListener->insertPicture(pos, MWAWEmbeddedObject(data, "image/pict"));

  return true;
}

bool HanMacWrdKGraph::sendFrame(long frameId, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  auto fIt=m_state->m_framesMap.find(frameId);
  if (fIt == m_state->m_framesMap.end() || !fIt->second) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendFrame: can not find frame %lx\n", static_cast<long unsigned int>(frameId)));
    return false;
  }
  auto pos(lPos);
  if (pos.size()[0]<=0 || pos.size()[1]<=0)
    pos.setSize(fIt->second->m_pos.size());
  return sendFrame(*fIt->second, pos);
}

bool HanMacWrdKGraph::sendFrame(HanMacWrdKGraphInternal::Frame const &frame, MWAWPosition const &lPos)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) return true;

  auto pos(lPos);
  frame.m_parsed = true;
  MWAWInputStreamPtr &input= m_parserState->m_input;
  switch (frame.m_type) {
  case 3: {
    auto const &ftnote=static_cast<HanMacWrdKGraphInternal::FootnoteFrame const &>(frame);
    MWAWSubDocumentPtr subdoc
    (new HanMacWrdKGraphInternal::SubDocument(*this, input, HanMacWrdKGraphInternal::SubDocument::Text,
        ftnote.m_textFileId, ftnote.m_textFileSubId));
    listener->insertNote(MWAWNote(MWAWNote::FootNote),subdoc);
    break;
  }
  case 4:
    // fixme: check also for border
    if (frame.m_style.hasPattern()) {
      auto const &textbox=static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame);
      if (!textbox.isLinked() && m_mainParser->canSendTextAsGraphic(textbox.m_textFileId,0)) {
        textbox.m_parsed=true;
        MWAWSubDocumentPtr subdoc
        (new HanMacWrdKGraphInternal::SubDocument(*this, input, HanMacWrdKGraphInternal::SubDocument::Text, textbox.m_textFileId));
        MWAWBox2f box(MWAWVec2f(0,0),pos.size());
        MWAWGraphicEncoder graphicEncoder;
        MWAWGraphicListener graphicListener(*m_parserState, box, &graphicEncoder);
        graphicListener.startDocument();
        MWAWPosition textPos(box[0], box.size(), librevenge::RVNG_POINT);
        textPos.m_anchorTo=MWAWPosition::Page;
        graphicListener.insertTextBox(textPos, subdoc, textbox.m_style);
        graphicListener.endDocument();
        MWAWEmbeddedObject picture;
        if (!graphicEncoder.getBinaryResult(picture))
          return false;
        listener->insertPicture(pos, picture);
        return true;
      }
    }
    MWAW_FALLTHROUGH;
  case 10:
    return sendTextBox(static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame), pos);
  case 6: {
    auto const &pict = static_cast<HanMacWrdKGraphInternal::PictureFrame const &>(frame);
    if (pict.m_fileId==0) {
      if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
        pos.setSize(pict.getBdBox().size());

      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));

      MWAWSubDocumentPtr subdoc
      (new HanMacWrdKGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdKGraphInternal::SubDocument::EmptyPicture, pict.m_fileId));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    return sendPictureFrame(pict, pos);
  }
  case 8:
    return sendShapeGraph(static_cast<HanMacWrdKGraphInternal::ShapeGraph const &>(frame), pos);
  case 9: {
    auto &table = const_cast<HanMacWrdKGraphInternal::Table &>
                  (static_cast<HanMacWrdKGraphInternal::Table const &>(frame));
    if (!table.updateTable()) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendFrame: can not find the table structure\n"));
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdKGraphInternal::SubDocument
       (*this, input, HanMacWrdKGraphInternal::SubDocument::UnformattedTable, frame.m_fileId));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    if (pos.m_anchorTo==MWAWPosition::Page ||
        (pos.m_anchorTo!=MWAWPosition::Frame && table.hasExtraLines())) {
      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));

      MWAWSubDocumentPtr subdoc
      (new HanMacWrdKGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdKGraphInternal::SubDocument::FrameInFrame, frame.m_fileId));
      pos.setSize(MWAWVec2f(-0.01f,-0.01f)); // autosize
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    if (table.sendTable(listener,pos.m_anchorTo==MWAWPosition::Frame))
      return true;
    return table.sendAsText(listener);
  }
  case 11: {
    auto const &group=static_cast<HanMacWrdKGraphInternal::Group const &>(frame);
    if ((pos.m_anchorTo==MWAWPosition::Char || pos.m_anchorTo==MWAWPosition::CharBaseLine) && !canCreateGraphic(group)) {
      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdKGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdKGraphInternal::SubDocument::Group, frame.m_fileId));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    sendGroup(group, pos);
    break;
  }
  default:
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendFrame: sending type %d is not implemented\n", frame.m_type));
    break;
  }
  return false;
}

bool HanMacWrdKGraph::sendEmptyPicture(MWAWPosition const &pos)
{
  if (!m_parserState->m_textListener)
    return true;
  MWAWVec2f pictSz = pos.size();
  std::shared_ptr<MWAWPict> pict;
  MWAWPosition pictPos(MWAWVec2f(0,0), pictSz, librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Frame);
  pictPos.setOrder(-1);

  MWAWBox2f box=MWAWBox2f(MWAWVec2f(0,0),pictSz);
  MWAWPosition shapePos(MWAWVec2f(0,0),pictSz, librevenge::RVNG_POINT);
  shapePos.m_anchorTo=MWAWPosition::Page;
  MWAWGraphicEncoder graphicEncoder;
  MWAWGraphicListener graphicListener(*m_parserState, box, &graphicEncoder);
  graphicListener.startDocument();
  MWAWGraphicStyle defStyle;
  graphicListener.insertShape(shapePos, MWAWGraphicShape::rectangle(box), defStyle);
  graphicListener.insertShape(shapePos, MWAWGraphicShape::line(box[0],box[1]), defStyle);
  graphicListener.insertShape(shapePos, MWAWGraphicShape::line(MWAWVec2f(0,pictSz[1]), MWAWVec2f(pictSz[0],0)), defStyle);
  graphicListener.endDocument();
  MWAWEmbeddedObject picture;
  if (!graphicEncoder.getBinaryResult(picture)) return false;
  m_parserState->m_textListener->insertPicture(pictPos, picture);
  return true;
}

bool HanMacWrdKGraph::sendPictureFrame(HanMacWrdKGraphInternal::PictureFrame const &pict, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  auto pos(lPos);
  if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(pict.getBdBox().size());
  //fixme: check if we have border
  sendPicture(pict.m_fileId, pos);
  return true;
}

bool HanMacWrdKGraph::sendTextBox(HanMacWrdKGraphInternal::TextBox const &textbox, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  MWAWVec2f textboxSz = textbox.getBdBox().size();
  auto pos(lPos);
  if (textbox.m_type==10) {
    if (textbox.m_dim[0] > textboxSz[0]) textboxSz[0]=textbox.m_dim[0];
    if (textbox.m_dim[1] > textboxSz[1]) textboxSz[1]=textbox.m_dim[1];
    pos.setSize(textboxSz);
    pos.setOrder(100); // put note in front
  }
  else if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(textboxSz);

  MWAWGraphicStyle style;
  textbox.addTo(style);
  MWAWSubDocumentPtr subdoc;
  if (!textbox.m_isLinked)
    subdoc.reset(new HanMacWrdKGraphInternal::SubDocument(*this, m_parserState->m_input, HanMacWrdKGraphInternal::SubDocument::Text, textbox.m_textFileId));
  m_parserState->m_textListener->insertTextBox(pos, subdoc, style);

  return true;
}

bool HanMacWrdKGraph::sendShapeGraph(HanMacWrdKGraphInternal::ShapeGraph const &pict, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  auto pos(lPos);
  if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(pict.getBdBox().size());
  pos.setOrigin(pos.origin());
  pos.setSize(pos.size()+MWAWVec2f(4,4));
  m_parserState->m_textListener->insertShape(pos,pict.m_shape,pict.getStyle());
  return true;
}

// ----- table
bool HanMacWrdKGraph::sendTableUnformatted(long fId)
{
  if (!m_parserState->m_textListener)
    return true;
  auto fIt = m_state->m_framesMap.find(fId);
  if (fIt == m_state->m_framesMap.end() || !fIt->second || fIt->second->m_type != 9) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendTableUnformatted: can not find table %lx\n", static_cast<long unsigned int>(fId)));
    return false;
  }
  auto &table = static_cast<HanMacWrdKGraphInternal::Table &>(*fIt->second);
  return table.sendAsText(m_parserState->m_textListener);
}

////////////////////////////////////////////////////////////
// low level
////////////////////////////////////////////////////////////

// try to read a small graphic
std::shared_ptr<HanMacWrdKGraphInternal::ShapeGraph> HanMacWrdKGraph::readShapeGraph(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header)
{
  std::shared_ptr<HanMacWrdKGraphInternal::ShapeGraph> graph;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readShapeGraph: called without any zone\n"));
    return graph;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  if (pos+26 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readShapeGraph: the zone seems too short\n"));
    return graph;
  }

  graph.reset(new HanMacWrdKGraphInternal::ShapeGraph(header));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  auto graphType = static_cast<int>(input->readLong(1));
  long val;
  bool ok = true;
  MWAWBox2f bdbox=graph->m_pos;
  MWAWGraphicShape &shape=graph->m_shape;
  shape = MWAWGraphicShape();
  shape.m_bdBox = shape.m_formBox = bdbox;
  switch (graphType) {
  case 0:
  case 3: { // lines
    if (pos+28 > dataSz) {
      f << "###";
      ok = false;
      break;
    }
    shape.m_type=MWAWGraphicShape::Line;
    auto arrowFlags=static_cast<int>(input->readULong(1));
    if (arrowFlags&1) graph->m_style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
    if (arrowFlags&2) graph->m_style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
    if (arrowFlags&0xFC) f << "#arrowsFl=" << (arrowFlags & 0xFC) << ",";
    for (int i = 0; i < 5; ++i) { // always 0
      val = input->readLong(2);
      if (val) f << "f" << i << "=" << val << ",";
    }
    float coord[2];
    for (int pt = 0; pt < 2; ++pt) {
      for (auto &c : coord) c = float(input->readLong(4))/65536.f;
      MWAWVec2f vertex=MWAWVec2f(coord[1],coord[0]);
      shape.m_vertices.push_back(vertex);
    }
    break;
  }
  case 1: // rectangle
  case 2: // circle
    shape.m_type = graphType==1?
                   MWAWGraphicShape::Rectangle : MWAWGraphicShape::Circle;
    for (int i = 0; i < 13; ++i) { // always 0
      val = input->readLong(2);
      if (val) f << "f" << i << "=" << val << ",";
    }
    break;
  case 4: { // rectOval
    if (pos+28 > dataSz) {
      f << "###";
      ok = false;
      break;
    }
    for (int i = 0; i < 4; ++i) {
      val = input->readLong(i ? 2 : 1);
      if (val) f << "f" << i << "=" << val << ",";
    }
    shape.m_type=MWAWGraphicShape::Rectangle;
    float cornerDim = float(input->readLong(4))/65536.f;
    for (int c=0; c < 2; ++c) {
      if (2.f*cornerDim <= bdbox.size()[c])
        shape.m_cornerWidth[c]=cornerDim;
      else
        shape.m_cornerWidth[c]=bdbox.size()[c]/2.0f;
    }
    for (int i = 0; i < 8; ++i) {
      val = input->readLong(i);
      if (val) f << "g" << i << "=" << val << ",";
    }
    break;
  }
  case 5: { // arc
    val = input->readLong(2);
    if (val) f << "f0=" << val << ",";
    auto transf = static_cast<int>(input->readULong(1));
    float angles[2];
    if (transf>=0 && transf <= 3) {
      int decal = (transf%2) ? 4-transf : transf;
      angles[0] = float(-90*decal);
      angles[1] = float(90-90*decal);
    }
    else {
      f << "#transf=" << transf << ",";
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::readShapeGraph: find unexpected transformation for arc\n"));
      ok = false;
      break;
    }

    // we must compute the real bd box: first the box on the unit circle
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; ++i)
      limitAngle[i] = (angles[i] < 0) ? int(angles[i]/90.f)-1 : int(angles[i]/90.f);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; ++bord) {
      float ang = (bord == limitAngle[0]) ? float(angles[0]) :
                  (bord == limitAngle[1]+1) ? float(angles[1]) : float(90 * bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { std::cos(ang), -std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    float factor[2]= {bdbox.size()[0]/(maxVal[0]>minVal[0]?maxVal[0]-minVal[0]:0.f),
                      bdbox.size()[1]/(maxVal[1]>minVal[1]?maxVal[1]-minVal[1]:0.f)
                     };
    float delta[2]= {bdbox[0][0]-minVal[0] *factor[0],bdbox[0][1]-minVal[1] *factor[1]};
    shape.m_formBox=MWAWBox2f(MWAWVec2f(delta[0]-factor[0],delta[1]-factor[1]),
                              MWAWVec2f(delta[0]+factor[0],delta[1]+factor[1]));
    shape.m_type=MWAWGraphicShape::Pie;
    shape.m_arcAngles=MWAWVec2f(angles[0],angles[1]);
    for (int i = 0; i < 12; ++i) { // always 0
      val = input->readLong(2);
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    break;
  }
  case 6: { // poly
    for (int i = 0; i < 5; ++i) { // find f3=0|1, other 0
      val = input->readLong(1);
      if (val) f << "f" << i << "=" << val << ",";
    }
    auto numPt = static_cast<int>(input->readLong(2));
    if (numPt < 0 || 28+8*numPt > dataSz) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::readShapeGraph: find unexpected number of points\n"));
      f << "#pt=" << numPt << ",";
      ok = false;
      break;
    }
    for (int i = 0; i < 10; ++i) { //always 0
      val = input->readLong(2);
      if (val) f << "g" << i << "=" << val << ",";
    }
    shape.m_type=MWAWGraphicShape::Polygon;
    MWAWVec2f minPt(0,0);
    for (int i = 0; i < numPt; ++i) {
      float dim[2];
      for (auto &d : dim) d = float(input->readLong(4))/65536.f;
      MWAWVec2f vertex=MWAWVec2f(dim[1], dim[0])+bdbox[0];
      shape.m_vertices.push_back(vertex);
    }
    break;
  }
  default:
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readShapeGraph: find unexpected graphic subType\n"));
    f << "###type=" << graphType << ",";
    ok = false;
    break;
  }

  std::string extra = f.str();
  graph->m_extra += extra;

  f.str("");
  f << "FrameDef(graphData):" << graph->print() << extra;

  asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (!ok)
    graph.reset();
  return graph;
}

// try to read the group data
std::shared_ptr<HanMacWrdKGraphInternal::Group> HanMacWrdKGraph::readGroup(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header)
{
  std::shared_ptr<HanMacWrdKGraphInternal::Group> group;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readGroup: called without any zone\n"));
    return group;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  if (pos+2 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readGroup: the zone seems too short\n"));
    return group;
  }
  auto N = static_cast<int>(input->readULong(2));
  if (pos+2+8*N > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readGroup: can not read N\n"));
    return group;
  }
  group.reset(new HanMacWrdKGraphInternal::Group(header));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  for (int i = 0; i < N; ++i) {
    HanMacWrdKGraphInternal::Group::Child child;
    child.m_fileId = long(input->readULong(4));
    for (auto &val : child.m_values) // f0=4|6|8, f1=0
      val = static_cast<int>(input->readLong(2));
    group->m_childsList.push_back(child);
  }
  f << "FrameDef(groupData):" << group->print();

  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return group;
}

// try to read a picture frame
std::shared_ptr<HanMacWrdKGraphInternal::PictureFrame> HanMacWrdKGraph::readPictureFrame(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header)
{
  std::shared_ptr<HanMacWrdKGraphInternal::PictureFrame> picture;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: called without any zone\n"));
    return picture;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  if (pos+32 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readPicture: the zone seems too short\n"));
    return picture;
  }

  picture.reset(new HanMacWrdKGraphInternal::PictureFrame(header));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  picture->m_pictureType = static_cast<int>(input->readLong(2)); // 0 or 4 : or maybe wrapping
  for (int i = 0; i < 5; ++i) // always 0
    picture->m_values[i] = static_cast<int>(input->readLong(2));
  float bDim[2];
  for (auto &d : bDim) d = float(input->readLong(4))/65536.f;
  picture->m_borderDim = MWAWVec2f(bDim[0], bDim[1]);
  for (int i = 5; i < 7; ++i) // always 0
    picture->m_values[i] = static_cast<int>(input->readLong(2));
  int dim[2]; //0,0 or 3e,3c
  for (auto &d : dim) d = static_cast<int>(input->readLong(2));
  picture->m_dim  = MWAWVec2i(dim[0], dim[1]);
  picture->m_fileId = long(input->readULong(4));

  f << "FrameDef(pictureData):";
  if (picture->m_fileId)
    f << "fId=" << std::hex << picture->m_fileId << std::dec << ",";
  f << picture->print();
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return picture;
}

// try to read the footnote data
std::shared_ptr<HanMacWrdKGraphInternal::FootnoteFrame> HanMacWrdKGraph::readFootnoteFrame(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header)
{
  std::shared_ptr<HanMacWrdKGraphInternal::FootnoteFrame> ftn;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readFootnoteFrame: called without any zone\n"));
    return ftn;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  if (pos+24 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readFootnoteFrame: the zone seems too short\n"));
    return ftn;
  }

  ftn.reset(new HanMacWrdKGraphInternal::FootnoteFrame(header));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  for (int i = 0; i < 9; ++i) { // always 0?
    long val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  ftn->m_textFileSubId = long(input->readULong(2));
  ftn->m_textFileId = long(input->readULong(4));
  std::string extra = f.str();
  ftn->m_extra += extra;

  f.str("");
  f << "FrameDef(footnoteData):" << ftn->print() << extra;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return ftn;
}

// try to read the textbox data
std::shared_ptr<HanMacWrdKGraphInternal::TextBox> HanMacWrdKGraph::readTextBox(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header, bool isMemo)
{
  std::shared_ptr<HanMacWrdKGraphInternal::TextBox> textbox;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTextBox: called without any zone\n"));
    return textbox;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  int expectedSize = isMemo ? 20 : 12;
  if (pos+expectedSize > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTextBox: the zone seems too short\n"));
    return textbox;
  }

  textbox.reset(new HanMacWrdKGraphInternal::TextBox(header, isMemo));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f;
  for (int i = 0; i < 3; ++i) { // 0|1,0|1,0|1,numtextbox linked
    auto val = static_cast<int>(input->readLong(1));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto numLinks=static_cast<int>(input->readLong(1));
  if (numLinks!=(isMemo ? 0 : 1)) f << "numLinks=" << numLinks << ",";
  auto fChar=long(input->readULong(4));
  if (fChar) f << "first[char]=" << fChar << ",";
  textbox->m_textFileId = long(input->readULong(4));
  if (isMemo) { // checkme
    for (int i = 0; i < 2; ++i)
      textbox->m_dim[1-i] = float(input->readLong(4))/65536.f;
  }
  else if (numLinks>1 && pos+12+4*(numLinks-1) <= dataSz) {
    for (int l=1; l<numLinks; ++l)
      textbox->m_linkedIdList.push_back(input->readLong(4));
  }
  textbox->m_extra=f.str();
  f.str("");
  f << "FrameDef(textboxData):";
  f << "fId=" << std::hex << textbox->m_textFileId << std::dec << "," << textbox->print();
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return textbox;
}

// try to read the table data
std::shared_ptr<HanMacWrdKGraphInternal::Table> HanMacWrdKGraph::readTable(std::shared_ptr<HanMacWrdKZone> zone, HanMacWrdKGraphInternal::Frame const &header)
{
  std::shared_ptr<HanMacWrdKGraphInternal::Table> table;
  if (!zone) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTable: called without any zone\n"));
    return table;
  }

  MWAWInputStreamPtr input = zone->m_input;
  long dataSz = zone->length();
  long pos = input->tell();
  if (pos+20 > dataSz) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTable: the zone seems too short\n"));
    return table;
  }

  table.reset(new HanMacWrdKGraphInternal::Table(header, *this));
  libmwaw::DebugFile &asciiFile = zone->ascii();
  libmwaw::DebugStream f, f2;
  long val;
  for (int i = 0; i < 4; ++i) {
    val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  table->m_textFileId = long(input->readULong(4));
  table->m_rows = static_cast<int>(input->readLong(2));
  table->m_columns = static_cast<int>(input->readLong(2));
  table->m_numCells = static_cast<int>(input->readLong(2));

  val = input->readLong(2);
  if (val) f << "f4=" << val << ",";
  std::string extra = f.str();
  table->m_extra += extra;

  f.str("");
  f << "FrameDef(tableData):";
  f << "fId=" << std::hex << table->m_textFileId << std::dec << ","
    << table->print() << extra;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < table->m_numCells; ++i) {
    if (input->isEnd()) break;
    pos = input->tell();
    f.str("");
    if (pos+80 > dataSz) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTable: can not read cell %d\n", i));
      f <<  "FrameDef(tableCell-" << i << "):###";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      break;
    }
    std::shared_ptr<HanMacWrdKGraphInternal::TableCell> cell(new HanMacWrdKGraphInternal::TableCell);
    int posi[2];
    for (auto &p : posi) p = static_cast<int>(input->readLong(2));
    cell->setPosition(MWAWVec2i(posi[1],posi[0]));
    int span[2];
    for (auto &s : span) s = static_cast<int>(input->readLong(2));
    if (span[0]>=1 && span[1]>=1)
      cell->setNumSpannedCells(MWAWVec2i(span[1], span[0]));
    else {
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::readTable: can not read cell span\n"));
      f << "##span=" << span[1] << "x" << span[0] << ",";
    }
    float dim[2];
    for (auto &d : dim) d = float(input->readLong(4))/65536.f;
    cell->setBdSize(MWAWVec2f(dim[0], dim[1]));

    auto color = static_cast<int>(input->readULong(2));
    MWAWColor backCol = MWAWColor::white();
    if (!m_state->getColor(color, backCol))
      f << "#backcolor=" << color << ",";
    auto pattern = static_cast<int>(input->readULong(2));
    HanMacWrdKGraphInternal::Pattern pat;
    if (pattern && m_state->getPattern(pattern, pat))
      cell->setBackgroundColor(m_state->getColor(backCol, pat.m_percent));
    else if (pattern)
      f << "#backPattern=" << pattern << ",";

    cell->m_flags = static_cast<int>(input->readULong(2));
    if (cell->m_flags&1) cell->setVAlignment(MWAWCell::VALIGN_CENTER);
    switch ((cell->m_flags>>2) & 3) {
    case 1:
      cell->setExtraLine(MWAWCell::E_Line1);
      break;
    case 2:
      cell->setExtraLine(MWAWCell::E_Line2);
      break;
    case 3:
      cell->setExtraLine(MWAWCell::E_Cross);
      break;
    case 0: // none
    default:
      break;
    }
    val = input->readLong(2);
    if (val) f << "f2=" << val << ",";

    static char const *what[] = {"T", "L", "B", "R"};
    static int const which[] = { libmwaw::TopBit, libmwaw::LeftBit, libmwaw::BottomBit, libmwaw::RightBit };
    for (int b = 0; b < 4; ++b) { // find _,4000,_,_,1,_, and 1,_,_,_,1,_,
      f2.str("");
      MWAWBorder border;
      border.m_width=double(input->readLong(4))/65536.;
      auto type = int(input->readLong(2));
      switch (type) {
      case 0: // solid
        break;
      case 1:
        border.m_type = MWAWBorder::Double;
        break;
      case 2:
        border.m_type = MWAWBorder::Double;
        border.m_widthsList.resize(3,1.);
        border.m_widthsList[0]=2.0;
        break;
      case 3:
        border.m_type = MWAWBorder::Double;
        border.m_widthsList.resize(3,1.);
        border.m_widthsList[2]=2.0;
        break;
      default:
        f2 << "#style=" << type << ",";
        break;
      }
      color = static_cast<int>(input->readULong(2));
      MWAWColor col = MWAWColor::black();
      if (!m_state->getColor(color, col))
        f2 << "#color=" << color << ",";
      pattern = static_cast<int>(input->readULong(2));
      if (pattern==0) border.m_style=MWAWBorder::None;
      else {
        if (!m_state->getPattern(pattern, pat)) {
          f2 << "#pattern=" << pattern << ",";
          border.m_color = col;
        }
        else
          border.m_color = m_state->getColor(col, pat.m_percent);
      }
      val = long(input->readULong(2));
      if (val) f2 << "unkn=" << val << ",";

      cell->setBorders(which[b],border);
      if (cell->hasExtraLine() && b==3) {
        // extra line border seems to correspond to the right border
        MWAWBorder extraL;
        extraL.m_width=border.m_width;
        extraL.m_color=border.m_color;
        cell->setExtraLine(cell->extraLine(), extraL);
      }
      if (f2.str().length())
        f << "bord" << what[b] << "=[" << f2.str() << "],";
    }
    cell->m_fileId = long(input->readULong(4));
    cell->m_id = long(input->readULong(4));
    cell->m_extra = f.str();
    table->add(cell);

    f.str("");
    f <<  "FrameDef(tableCell-" << i << "):" << *cell;

    asciiFile.addDelimiter(input->tell(),'|');
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+80, librevenge::RVNG_SEEK_SET);
  }

  return table;
}

////////////////////////////////////////////////////////////
// group
////////////////////////////////////////////////////////////
bool HanMacWrdKGraph::sendGroup(long groupId, MWAWPosition const &pos)
{
  if (!m_parserState->m_textListener) return true;
  auto fIt=m_state->m_framesMap.find(groupId);
  if (fIt == m_state->m_framesMap.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroup: can not find group %lx\n", static_cast<long unsigned int>(groupId)));
    return false;
  }
  auto frame=fIt->second;
  if (!frame || frame->m_type!=11) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroup: %lx seems bad\n", static_cast<long unsigned int>(groupId)));
    return false;
  }
  return sendGroup(static_cast<HanMacWrdKGraphInternal::Group const &>(*frame), pos);
}

bool HanMacWrdKGraph::sendGroup(HanMacWrdKGraphInternal::Group const &group, MWAWPosition const &pos)
{
  group.m_parsed=true;
  sendGroupChild(group,pos);
  return true;
}

bool HanMacWrdKGraph::canCreateGraphic(HanMacWrdKGraphInternal::Group const &group)
{
  int page = group.m_page;
  for (auto const &child :group.m_childsList) {
    long fId=child.m_fileId;
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end() || fIt->first!=fId || !fIt->second)
      continue;
    auto const &frame=*fIt->second;
    if (frame.m_page!=page) return false;
    switch (frame.m_type) {
    case 4: {
      auto const &text=static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame);
      if (text.isLinked() || !m_mainParser->canSendTextAsGraphic(text.m_textFileId,0))
        return false;
      break;
    }
    case 8: // shape
      break;
    case 11:
      if (!canCreateGraphic(static_cast<HanMacWrdKGraphInternal::Group const &>(frame)))
        return false;
      break;
    default:
      return false;
    }
  }
  return true;
}

void HanMacWrdKGraph::sendGroup(HanMacWrdKGraphInternal::Group const &group, MWAWGraphicListenerPtr &listener)
{
  if (!listener) return;
  group.m_parsed=true;
  MWAWInputStreamPtr &input= m_parserState->m_input;
  for (auto const &child : group.m_childsList) {
    long fId=child.m_fileId;
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end() || fIt->first!=fId || !fIt->second)
      continue;
    auto const &frame=*fIt->second;
    MWAWBox2f box=frame.getBdBox();
    MWAWPosition pictPos(box[0], box.size(), librevenge::RVNG_POINT);
    pictPos.m_anchorTo=MWAWPosition::Page;
    switch (frame.m_type) {
    case 4: {
      frame.m_parsed=true;
      auto const &textbox=static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame);
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdKGraphInternal::SubDocument(*this, input, HanMacWrdKGraphInternal::SubDocument::Text, textbox.m_textFileId));
      listener->insertTextBox(pictPos, subdoc, textbox.m_style);
      break;
    }
    case 8: {
      frame.m_parsed=true;
      auto const &shape=static_cast<HanMacWrdKGraphInternal::ShapeGraph const &>(frame);
      listener->insertShape(pictPos, shape.m_shape, shape.getStyle());
      break;
    }
    case 11:
      sendGroup(static_cast<HanMacWrdKGraphInternal::Group const &>(frame), listener);
      break;
    default:
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroup: unexpected type %d\n", frame.m_type));
      break;
    }
  }
}

void HanMacWrdKGraph::sendGroupChild(HanMacWrdKGraphInternal::Group const &group, MWAWPosition const &pos)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroupChild: can not find the listeners\n"));
    return;
  }
  size_t numChilds=group.m_childsList.size(), childNotSent=0;
  if (!numChilds) return;

  int numDataToMerge=0;
  MWAWBox2f partialBdBox;
  MWAWPosition partialPos(pos);
  MWAWInputStreamPtr &input= m_parserState->m_input;
  for (size_t c=0; c<numChilds; ++c) {
    long fId=group.m_childsList[c].m_fileId;
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end() || fIt->first!=fId || !fIt->second) {
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroupChild: can not find child %lx\n", static_cast<long unsigned int>(fId)));
      continue;
    }
    HanMacWrdKGraphInternal::Frame const &frame=*fIt->second;
    bool canMerge=false;
    if (frame.m_page==group.m_page) {
      switch (frame.m_type) {
      case 4: {
        auto const &text=static_cast<HanMacWrdKGraphInternal::TextBox const &>(frame);
        canMerge=!text.isLinked()&&m_mainParser->canSendTextAsGraphic(text.m_textFileId,0);
        break;
      }
      case 8: // shape
        canMerge = true;
        break;
      case 11:
        canMerge = canCreateGraphic(static_cast<HanMacWrdKGraphInternal::Group const &>(frame));
        break;
      default:
        break;
      }
    }
    bool isLast=false;
    if (canMerge) {
      MWAWBox2f box=frame.getBdBox();
      if (numDataToMerge == 0)
        partialBdBox=box;
      else
        partialBdBox=partialBdBox.getUnion(box);
      ++numDataToMerge;
      if (c+1 < numChilds)
        continue;
      isLast=true;
    }

    if (numDataToMerge>1) {
      partialBdBox.extend(3);
      MWAWGraphicEncoder graphicEncoder;
      MWAWGraphicListenerPtr graphicListener(new MWAWGraphicListener(*m_parserState, partialBdBox, &graphicEncoder));
      graphicListener->startDocument();
      size_t lastChild = isLast ? c : c-1;
      for (size_t ch=childNotSent; ch <= lastChild; ++ch) {
        long localFId=group.m_childsList[ch].m_fileId;
        fIt=m_state->m_framesMap.find(localFId);
        if (fIt == m_state->m_framesMap.end() || fIt->first!=localFId || !fIt->second)
          continue;
        HanMacWrdKGraphInternal::Frame const &child=*fIt->second;
        MWAWBox2f box=child.getBdBox();
        MWAWPosition pictPos(box[0], box.size(), librevenge::RVNG_POINT);
        pictPos.m_anchorTo=MWAWPosition::Page;
        switch (child.m_type) {
        case 4: {
          child.m_parsed=true;
          auto const &textbox=static_cast<HanMacWrdKGraphInternal::TextBox const &>(child);
          MWAWSubDocumentPtr subdoc
          (new HanMacWrdKGraphInternal::SubDocument(*this, input, HanMacWrdKGraphInternal::SubDocument::Text, textbox.m_textFileId));
          graphicListener->insertTextBox(pictPos, subdoc, textbox.m_style);
          break;
        }
        case 8: {
          child.m_parsed=true;
          auto const &shape=static_cast<HanMacWrdKGraphInternal::ShapeGraph const &>(child);
          graphicListener->insertShape(pictPos, shape.m_shape, shape.getStyle());
          break;
        }
        case 11:
          sendGroup(static_cast<HanMacWrdKGraphInternal::Group const &>(child), graphicListener);
          break;
        default:
          MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroupChild: unexpected type %d\n", child.m_type));
          break;
        }
      }
      graphicListener->endDocument();
      MWAWEmbeddedObject picture;
      if (graphicEncoder.getBinaryResult(picture)) {
        partialPos.setOrigin(pos.origin()+partialBdBox[0]-group.m_pos[0]);
        partialPos.setSize(partialBdBox.size());
        listener->insertPicture(partialPos, picture);
        if (isLast)
          break;
        childNotSent=c;
      }
    }

    // time to send back the data
    for (; childNotSent <= c; ++childNotSent) {
      long localFId=group.m_childsList[childNotSent].m_fileId;
      fIt=m_state->m_framesMap.find(localFId);
      if (fIt != m_state->m_framesMap.end() && fIt->first==localFId && fIt->second) {
        auto const &childFrame=*fIt->second;
        MWAWPosition fPos(pos);
        fPos.setOrigin(childFrame.m_pos[0]-group.m_pos[0]+pos.origin());
        fPos.setSize(childFrame.m_pos.size());
        sendFrame(childFrame, fPos);
        continue;
      }
      MWAW_DEBUG_MSG(("HanMacWrdKGraph::sendGroupChild: can not find child %lx\n", static_cast<long unsigned int>(localFId)));
    }
    numDataToMerge=0;
  }
}

void HanMacWrdKGraph::prepareStructures()
{
  for (auto fIt : m_state->m_framesMap) {
    if (!fIt.second) continue;
    HanMacWrdKGraphInternal::Frame &frame = *fIt.second;
    if (frame.m_type==11 && !frame.m_inGroup) {
      std::multimap<long, long> seens;
      checkGroupStructures(fIt.first, frame.m_fileSubId, seens, false);
    }
    if (frame.m_type==4) {
      auto &text=static_cast<HanMacWrdKGraphInternal::TextBox &>(frame);
      size_t numLink=text.m_linkedIdList.size();
      for (size_t l=0; l < numLink; ++l) {
        auto fIt2=m_state->m_framesMap.find(text.m_linkedIdList[l]);
        if (fIt2==m_state->m_framesMap.end() || fIt2->first!=text.m_linkedIdList[l] || !fIt2->second || fIt2->second->m_type!=4) {
          MWAW_DEBUG_MSG(("HanMacWrdKGraph::prepareStructures: can not find frame %lx\n", static_cast<long unsigned int>(text.m_linkedIdList[l])));
          text.m_linkedIdList.resize(l);
          break;
        }
        auto &follow=static_cast<HanMacWrdKGraphInternal::TextBox &>(*fIt2->second);
        follow.m_isLinked=true;
        if (l+1!=numLink)
          follow.m_linkedIdList.push_back(text.m_linkedIdList[l+1]);
      }
    }
  }
}

bool HanMacWrdKGraph::checkGroupStructures(long fileId, long fileSubId, std::multimap<long, long> &seens, bool inGroup)
{
  auto it=seens.lower_bound(fileId);
  while (it!=seens.end() && it->first==fileId) {
    if (it->second != fileSubId)
      continue;
    MWAW_DEBUG_MSG(("HanMacWrdKGraph::checkGroupStructures: zone %ld[%ld] already find\n", fileId, fileSubId));
    return false;
  }
  seens.insert(std::multimap<long, long>::value_type(fileId, fileSubId));
  auto fIt = m_state->m_framesMap.lower_bound(fileId);
  for (; fIt != m_state->m_framesMap.end(); ++fIt) {
    if (fIt->first!=fileId) break;
    if (!fIt->second) continue;
    auto &frame = *fIt->second;
    frame.m_inGroup=inGroup;
    if (frame.m_fileSubId != fileSubId) continue;
    if (frame.m_type==11) {
      auto &group=static_cast<HanMacWrdKGraphInternal::Group &>(frame);
      for (size_t c=0; c < group.m_childsList.size(); ++c) {
        if (checkGroupStructures(group.m_childsList[c].m_fileId, 0, seens, true))
          continue;
        group.m_childsList.resize(c);
        break;
      }
    }
    return true;
  }
  MWAW_DEBUG_MSG(("HanMacWrdKGraph::checkGroupStructures: can not find zone %ld[%ld]\n", fileId, fileSubId));
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool HanMacWrdKGraph::sendPageGraphics(std::vector<long> const &doNotSendIds)
{
  std::set<long> notSend;
  for (auto &id : doNotSendIds)
    notSend.insert(id);
  for (auto fIt : m_state->m_framesMap) {
    if (notSend.find(fIt.first) != notSend.end() || !fIt.second) continue;
    auto const &frame = *fIt.second;
    if (frame.m_parsed || frame.m_type==3 || frame.m_inGroup)
      continue;
    MWAWPosition pos(frame.m_pos[0],frame.m_pos.size(),librevenge::RVNG_POINT);
    pos.setRelativePosition(MWAWPosition::Page);
    pos.setPage(frame.m_page+1);
    sendFrame(frame, pos);
  }
  return true;
}

void HanMacWrdKGraph::flushExtra()
{
  for (auto fIt : m_state->m_framesMap) {
    if (!fIt.second) continue;
    auto const &frame = *fIt.second;
    if (frame.m_parsed || frame.m_type==3)
      continue;
    MWAWPosition pos(MWAWVec2f(0,0),MWAWVec2f(0,0),librevenge::RVNG_POINT);
    pos.setRelativePosition(MWAWPosition::Char);
    sendFrame(frame, pos);
  }
  for (auto pIt : m_state->m_picturesMap) {
    if (!pIt.second) continue;
    auto const &picture = *pIt.second;
    if (picture.m_parsed)
      continue;
    MWAWPosition pos(MWAWVec2f(0,0),MWAWVec2f(100,100),librevenge::RVNG_POINT);
    pos.setRelativePosition(MWAWPosition::Char);
    sendPicture(picture, pos);
  }
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
