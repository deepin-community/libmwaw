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
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"
#include "MWAWTextListener.hxx"

#include "HanMacWrdJParser.hxx"

#include "HanMacWrdJGraph.hxx"

/** Internal: the structures of a HanMacWrdJGraph */
namespace HanMacWrdJGraphInternal
{
////////////////////////////////////////
//! a cell format in HanMacWrdJGraph
struct CellFormat {
public:
  //! constructor
  CellFormat()
    : m_backColor(MWAWColor::white())
    , m_borders()
    , m_extra("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, CellFormat const &frmt)
  {
    if (!frmt.m_backColor.isWhite())
      o << "backColor=" << frmt.m_backColor << ",";
    char const *what[] = {"T", "L", "B", "R"};
    for (size_t b = 0; b < frmt.m_borders.size(); b++)
      o << "bord" << what[b] << "=[" << frmt.m_borders[b] << "],";
    o << frmt.m_extra;
    return o;
  }
  //! the background color
  MWAWColor m_backColor;
  //! the border: order defined by MWAWBorder::Pos
  std::vector<MWAWBorder> m_borders;
  //! extra data
  std::string m_extra;
};

////////////////////////////////////////
//! a table cell in a table in HanMacWrdJGraph
struct TableCell final : public MWAWCell {
  //! constructor
  explicit TableCell(long tId)
    : MWAWCell()
    , m_zId(0)
    , m_tId(tId)
    , m_cPos(-1)
    , m_fileId(0)
    , m_formatId(0)
    , m_flags(0)
    , m_extra("")
  {
  }
  //! use cell format to finish updating cell
  void update(CellFormat const &format);
  //! call when the content of a cell must be send
  bool sendContent(MWAWListenerPtr listener, MWAWTable &table) final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, TableCell const &cell);
  //! the cell zone id
  long m_zId;
  //! the cell text zone id
  long m_tId;
  //! the first character position in m_zId
  long m_cPos;
  //! the file id
  long m_fileId;
  //! the cell format id
  int m_formatId;
  //! the cell flags
  int m_flags;
  //! extra data
  std::string m_extra;

};

void TableCell::update(CellFormat const &format)
{
  setBackgroundColor(format.m_backColor);
  static int const wh[] = { libmwaw::LeftBit,  libmwaw::RightBit, libmwaw::TopBit, libmwaw::BottomBit};
  for (size_t b = 0; b < format.m_borders.size(); b++)
    setBorders(wh[b], format.m_borders[b]);
  if (hasExtraLine() && format.m_borders.size()>=2) {
    MWAWBorder extraL;
    extraL.m_width=format.m_borders[1].m_width;
    extraL.m_color=format.m_borders[1].m_color;
    setExtraLine(extraLine(), extraL);
  }
}

std::ostream &operator<<(std::ostream &o, TableCell const &cell)
{
  o << static_cast<MWAWCell const &>(cell);
  if (cell.m_flags&0x100) o << "justify[full],";
  if (cell.m_flags&0x800) o << "lock,";
  if (cell.m_flags&0x1000) o << "merge,";
  if (cell.m_flags&0x2000) o << "inactive,";
  if (cell.m_flags&0xC07F)
    o << "#linesFlags=" << std::hex << (cell.m_flags&0xC07F) << std::dec << ",";
  if (cell.m_zId > 0)
    o << "cellId="  << std::hex << cell.m_zId << std::dec << "[" << cell.m_cPos << "],";
  if (cell.m_formatId > 0)
    o << "formatId="  << std::hex << cell.m_formatId << std::dec << ",";
  o << cell.m_extra;
  return o;
}

////////////////////////////////////////
//! Internal: the table of a HanMacWrdJGraph
struct Table final : public MWAWTable {
  //! constructor
  explicit Table(HanMacWrdJGraph &parser)
    : MWAWTable(MWAWTable::CellPositionBit|MWAWTable::TableDimBit)
    , m_parser(&parser)
    , m_rows(1)
    , m_columns(1)
    , m_height(0)
    , m_textFileId(0)
    , m_formatsList()
  {
  }
  //! destructor
  ~Table() final;
  //! update all cells using the formats list
  void updateCells();
  //! send a text zone
  bool sendText(long id, long cPos) const
  {
    return m_parser->sendText(id, cPos);
  }
  //! the graph parser
  HanMacWrdJGraph *m_parser;
  //! the number of row
  int m_rows;
  //! the number of columns
  int m_columns;
  //! the table height
  int m_height;
  //! the text file id
  long m_textFileId;
  //! a list of cell format
  std::vector<CellFormat> m_formatsList;

private:
  Table(Table const &orig) = delete;
  Table &operator=(Table const &orig) = delete;
};

Table::~Table()
{
}

bool TableCell::sendContent(MWAWListenerPtr, MWAWTable &table)
{
  if (m_tId)
    return static_cast<Table &>(table).sendText(m_tId, m_cPos);
  return true;
}

void Table::updateCells()
{
  auto numFormats=static_cast<int>(m_formatsList.size());
  for (int c=0; c<numCells(); ++c) {
    if (!get(c)) continue;
    TableCell &cell=static_cast<TableCell &>(*get(c));
    if (cell.m_formatId < 0 || cell.m_formatId>=numFormats) {
      static bool first = true;
      if (first) {
        MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::Table::updateCells: can not find the format\n"));
        first = false;
      }
      continue;
    }
    cell.update(m_formatsList[size_t(cell.m_formatId)]);
  }
}

////////////////////////////////////////
//! a frame format in HanMacWrdJGraph
struct FrameFormat {
public:
  //! constructor
  FrameFormat()
    : m_style()
    , m_borderType(0)
  {
    m_style.m_lineWidth=0;
    for (auto &wrap : m_intWrap) wrap=1.0;
    for (auto &wrap : m_extWrap) wrap=1.0;
  }
  //! add property to frame extra values
  void addTo(MWAWGraphicStyle &style) const
  {
    if (m_style.hasLine()) {
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
        MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::FrameFormat::addTo: unexpected type\n"));
        break;
      }
      style.setBorders(15, border);
    }
    if (m_style.hasSurfaceColor())
      style.setBackgroundColor(m_style.m_surfaceColor);
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, FrameFormat const &frmt)
  {
    o << "style=[" << frmt.m_style << "],";
    if (frmt.m_borderType) o << "border[type]=" << frmt.m_borderType << ",";
    bool intDiff=false, extDiff=false;
    for (int i=1; i < 4; ++i) {
      if (frmt.m_intWrap[i]<frmt.m_intWrap[0] || frmt.m_intWrap[i]>frmt.m_intWrap[0])
        intDiff=true;
      if (frmt.m_extWrap[i]<frmt.m_extWrap[0] || frmt.m_extWrap[i]>frmt.m_extWrap[0])
        extDiff=true;
    }
    if (intDiff) {
      o << "dim/intWrap/border=[";
      for (double i : frmt.m_intWrap)
        o << i << ",";
      o << "],";
    }
    else
      o << "dim/intWrap/border=" << frmt.m_intWrap[0] << ",";
    if (extDiff) {
      o << "exterior[wrap]=[";
      for (auto &wrap : frmt.m_extWrap)
        o << wrap << ",";
      o << "],";
    }
    else
      o << "exterior[wrap]=" << frmt.m_extWrap[0] << ",";
    return o;
  }
  //! the graphic style
  MWAWGraphicStyle m_style;
  //! the border type
  int m_borderType;
  //! the interior wrap dim
  double m_intWrap[4];
  //! the exterior wrap dim
  double m_extWrap[4];
};

////////////////////////////////////////
//! Internal: the frame header of a HanMacWrdJGraph
struct Frame {
  //! constructor
  Frame()
    : m_type(-1)
    , m_fileId(-1)
    , m_id(-1)
    , m_formatId(0)
    , m_page(0)
    , m_pos()
    , m_baseline(0.f)
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
  //! returns true if the frame data are read
  virtual bool valid() const
  {
    return false;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Frame const &grph);
  //! the graph type
  int m_type;
  //! the file id
  long m_fileId;
  //! the local id
  int m_id;
  //! the format id
  int m_formatId;
  //! the page
  int m_page;
  //! the position
  MWAWBox2f m_pos;
  //! the baseline
  float m_baseline;
  //! true if this node is a group's child
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
  case 0: // text or column
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
  case 12:
    o << "footnote[sep],";
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
  if (grph.m_formatId > 0)
    o << "formatId=" << grph.m_formatId << ",";
  if (grph.m_page) o << "page=" << grph.m_page+1  << ",";
  o << "pos=" << grph.m_pos << ",";
  if (grph.m_baseline < 0 || grph.m_baseline>0) o << "baseline=" << grph.m_baseline << ",";
  o << grph.m_extra;
  return o;
}

////////////////////////////////////////
//! Internal: the comment frame of a HanMacWrdJGraph
struct CommentFrame final :  public Frame {
public:
  //! constructor
  explicit CommentFrame(Frame const &orig)
    : Frame(orig)
    , m_zId(0)
    , m_width(0)
    , m_cPos(0)
    , m_dim(0,0)
  {
  }
  //! destructor
  ~CommentFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_zId) s << "zId[TZone]=" << std::hex << m_zId << std::dec << ",";
    if (m_dim[0]>0 || m_dim[1] > 0)
      s << "auxi[dim]=" << m_dim << ",";
    if (m_width > 0)
      s << "width=" << m_width << ",";
    if (m_cPos)
      s << "cPos[first]=" << m_cPos << ",";
    return s.str();
  }
  //! the text id
  long m_zId;
  //! the zone width
  double m_width;
  //! the first char pos
  long m_cPos;
  //! the auxilliary dim
  MWAWVec2f m_dim;
};

CommentFrame::~CommentFrame()
{
}

////////////////////////////////////////
//! Internal: a group of a HanMacWrdJGraph
struct Group final :  public Frame {
public:
  //! constructor
  explicit Group(Frame const &orig)
    : Frame(orig)
    , m_zId(0)
    , m_childsList()
  {
  }
  //! destructor
  ~Group() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! the group id
  long m_zId;
  //! the child list
  std::vector<long> m_childsList;
};

Group::~Group()
{
}
////////////////////////////////////////
//! Internal: the picture frame of a HanMacWrdJGraph
struct PictureFrame final : public Frame {
public:
  //! constructor
  explicit PictureFrame(Frame const &orig)
    : Frame(orig)
    , m_entry()
    , m_zId(0)
    , m_dim(100,100)
    , m_scale(1,1)
  {
  }
  //! destructor
  ~PictureFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_zId) s << "zId=" << std::hex << m_zId << std::dec << ",";
    s << "dim[original]=" << m_dim << ",";
    s << "scale=" << m_scale << ",";
    return s.str();
  }
  //! the picture entry
  MWAWEntry m_entry;
  //! the picture id
  long m_zId;
  //! the picture size
  MWAWVec2i m_dim;
  //! the scale
  MWAWVec2f m_scale;
};

PictureFrame::~PictureFrame()
{
}

////////////////////////////////////////
//! Internal: a footnote separator of a HanMacWrdJGraph
struct SeparatorFrame final : public Frame {
public:
  //! constructor
  explicit SeparatorFrame(Frame const &orig) : Frame(orig)
  {
  }
  //! destructor
  ~SeparatorFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
};

SeparatorFrame::~SeparatorFrame()
{
}

////////////////////////////////////////
//! Internal: the table frame of a HanMacWrdJGraph
struct TableFrame final : public Frame {
public:
  //! constructor
  explicit TableFrame(Frame const &orig)
    : Frame(orig)
    , m_zId(0)
    , m_width(0)
    , m_length(0)
    , m_table()
  {
  }
  //! destructor
  ~TableFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_zId) s << "zId[TZone]=" << std::hex << m_zId << std::dec << ",";
    if (m_width > 0)
      s << "width=" << m_width << ",";
    if (m_length)
      s << "length[text?]=" << m_length << ",";
    return s.str();
  }
  //! the textzone id
  long m_zId;
  //! the zone width
  double m_width;
  //! related to text length?
  long m_length;
  //! the table
  std::shared_ptr<Table> m_table;
};

TableFrame::~TableFrame()
{
}

////////////////////////////////////////
//! Internal: the textbox frame of a HanMacWrdJGraph
struct TextboxFrame final : public Frame {
public:
  //! constructor
  explicit TextboxFrame(Frame const &orig)
    : Frame(orig)
    , m_zId(0)
    , m_width(0)
    , m_cPos(0)
    , m_linkToFId(0)
    , m_isLinked(false)
  {
  }
  //! destructor
  ~TextboxFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! returns true if the box is linked to other textbox
  bool isLinked() const
  {
    return m_linkToFId || m_isLinked;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_zId) s << "zId[TZone]=" << std::hex << m_zId << std::dec << ",";
    if (m_width > 0)
      s << "width=" << m_width << ",";
    if (m_cPos)
      s << "cPos[first]=" << m_cPos << ",";
    return s.str();
  }
  //! the text id
  long m_zId;
  //! the zone width
  double m_width;
  //! the first char pos
  long m_cPos;
  //! the next link zone
  long m_linkToFId;
  //! true if this zone is linked
  bool m_isLinked;
};

TextboxFrame::~TextboxFrame()
{
}

////////////////////////////////////////
//! Internal: the text frame (basic, header, footer, footnote) of a HanMacWrdJGraph
struct TextFrame final :  public Frame {
public:
  //! constructor
  explicit TextFrame(Frame const &orig) : Frame(orig), m_zId(0), m_width(0), m_cPos(0)
  {
  }
  //! destructor
  ~TextFrame() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    if (m_zId) s << "zId[TZone]=" << std::hex << m_zId << std::dec << ",";
    if (m_width > 0)
      s << "width=" << m_width << ",";
    if (m_cPos)
      s << "cPos[first]=" << m_cPos << ",";
    return s.str();
  }
  //! the text id
  long m_zId;
  //! the zone width
  double m_width;
  //! the first char pos
  long m_cPos;
};

TextFrame::~TextFrame()
{
}

////////////////////////////////////////
//! Internal: the geometrical graph of a HanMacWrdJGraph
struct ShapeGraph final : public Frame {
  //! constructor
  explicit ShapeGraph(Frame const &orig)
    : Frame(orig)
    , m_shape()
    , m_arrowsFlag(0)
  {
  }
  //! destructor
  ~ShapeGraph() final;
  //! returns true if the frame data are read
  bool valid() const final
  {
    return true;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ShapeGraph const &graph)
  {
    o << graph.print();
    o << static_cast<Frame const &>(graph);
    return o;
  }
  //! print local data
  std::string print() const
  {
    std::stringstream s;
    s << m_shape;
    if (m_arrowsFlag&1) s << "startArrow,";
    if (m_arrowsFlag&2) s << "endArrow,";
    return s.str();
  }

  //! the shape m_shape
  MWAWGraphicShape m_shape;
  //! the lines arrow flag
  int m_arrowsFlag;
};

ShapeGraph::~ShapeGraph()
{
}

////////////////////////////////////////
//! Internal: the pattern of a HanMacWrdJGraph
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
//! Internal: the state of a HanMacWrdJGraph
struct State {
  //! constructor
  State()
    : m_framesList()
    , m_framesMap()
    , m_frameFormatsList()
    , m_numPages(0)
    , m_colorList()
    , m_patternList()
    , m_defaultFormat() { }
  //! tries to find the lId the frame of a given type
  std::shared_ptr<Frame> findFrame(int type, int lId) const
  {
    int actId = 0;
    for (auto frame : m_framesList) {
      if (!frame || frame->m_type != type)
        continue;
      if (actId++==lId) {
        if (!frame->valid())
          break;
        return frame;
      }
    }
    return std::shared_ptr<Frame>();
  }
  //! returns the frame format corresponding to an id
  FrameFormat const &getFrameFormat(int id) const
  {
    if (id >= 0 && id < static_cast<int>(m_frameFormatsList.size()))
      return m_frameFormatsList[size_t(id)];
    MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::State::getFrameFormat: can not find format %d\n", id));
    return m_defaultFormat;
  }
  //! returns a color correspond to an id
  bool getColor(int id, MWAWColor &col)
  {
    initColors();
    if (id < 0 || id >= int(m_colorList.size())) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::State::getColor: can not find color %d\n", id));
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
      MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::State::getPattern: can not find pattern %d\n", id));
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
  //! init the pattenr list
  void initPatterns();

  /** the list of frames */
  std::vector<std::shared_ptr<Frame> > m_framesList;
  /** a map zId->frame pos in frames list */
  std::map<long, int> m_framesMap;
  /** the list of frame format */
  std::vector<FrameFormat> m_frameFormatsList;
  int m_numPages /* the number of pages */;
  //! a list colorId -> color
  std::vector<MWAWColor> m_colorList;
  //! a list patternId -> pattern
  std::vector<Pattern> m_patternList;
  //! empty format used to return a default format
  FrameFormat m_defaultFormat;
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
//! Internal: the subdocument of a HanMacWrdJGraph
class SubDocument final : public MWAWSubDocument
{
public:
  //! the document type
  enum Type { FrameInFrame, Group, Text, UnformattedTable, EmptyPicture };
  //! constructor
  SubDocument(HanMacWrdJGraph &pars, MWAWInputStreamPtr const &input, Type type, long id, long firstChar=0)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_graphParser(&pars)
    , m_type(type)
    , m_id(id)
    , m_firstChar(firstChar)
    , m_pos() {}

  //! constructor
  SubDocument(HanMacWrdJGraph &pars, MWAWInputStreamPtr const &input, MWAWPosition const &pos, Type type, long id, int firstChar=0)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_graphParser(&pars)
    , m_type(type)
    , m_id(id)
    , m_firstChar(firstChar)
    , m_pos(pos) {}

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the graph parser */
  HanMacWrdJGraph *m_graphParser;
  //! the zone type
  Type m_type;
  //! the zone id
  long m_id;
  //! the first char position
  long m_firstChar;
  //! the position in a frame
  MWAWPosition m_pos;

private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (!m_graphParser) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  if (listener->getType()==MWAWListener::Graphic) {
    if (m_type==Text)
      m_graphParser->sendText(m_id, m_firstChar, listener);
    else {
      MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::SubDocument::parse: send type %d is not implemented\n", m_type));
    }
  }
  else {
    switch (m_type) {
    case EmptyPicture:
      m_graphParser->sendEmptyPicture(m_pos);
      break;
    case Group:
      m_graphParser->sendGroup(m_id, m_pos);
      break;
    case FrameInFrame:
      m_graphParser->sendFrame(m_id, m_pos);
      break;
    case Text:
      m_graphParser->sendText(m_id, m_firstChar);
      break;
    case UnformattedTable:
      m_graphParser->sendTableUnformatted(m_id);
      break;
#if !defined(__clang__)
    default:
      MWAW_DEBUG_MSG(("HanMacWrdJGraphInternal::SubDocument::parse: send type %d is not implemented\n", m_type));
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
  if (m_firstChar != sDoc->m_firstChar) return true;
  if (m_pos != sDoc->m_pos) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
HanMacWrdJGraph::HanMacWrdJGraph(HanMacWrdJParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new HanMacWrdJGraphInternal::State)
  , m_mainParser(&parser)
{
}

HanMacWrdJGraph::~HanMacWrdJGraph()
{
}

int HanMacWrdJGraph::version() const
{
  return m_parserState->m_version;
}

bool HanMacWrdJGraph::getColor(int colId, int patternId, MWAWColor &color) const
{
  if (!m_state->getColor(colId, color)) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::getColor: can not find color for id=%d\n", colId));
    return false;
  }
  HanMacWrdJGraphInternal::Pattern pattern;
  if (!m_state->getPattern(patternId, pattern)) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::getColor: can not find pattern for id=%d\n", patternId));
    return false;
  }
  color = m_state->getColor(color, pattern.m_percent);
  return true;
}

int HanMacWrdJGraph::numPages() const
{
  if (m_state->m_numPages)
    return m_state->m_numPages;
  int nPages = 0;
  for (auto frame : m_state->m_framesList) {
    if (!frame || !frame->valid()) continue;
    int page = frame->m_page+1;
    if (page <= nPages) continue;
    if (page >= nPages+100) continue; // a pb ?
    nPages = page;
  }
  m_state->m_numPages = nPages;
  return nPages;
}

bool HanMacWrdJGraph::sendText(long textId, long fPos, MWAWListenerPtr const &listener)
{
  return m_mainParser->sendText(textId, fPos, listener);
}

std::map<long,int> HanMacWrdJGraph::getTextFrameInformations() const
{
  std::map<long,int> mapIdType;
  for (auto frame : m_state->m_framesList) {
    if (!frame || !frame->valid())
      continue;
    long zId=0;
    switch (frame->m_type) {
    case 0:
    case 1:
    case 2:
    case 3:
      zId=static_cast<HanMacWrdJGraphInternal::TextFrame const &>(*frame).m_zId;
      break;
    case 4:
      zId=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(*frame).m_zId;
      break;
    case 9:
      zId=static_cast<HanMacWrdJGraphInternal::TableFrame const &>(*frame).m_zId;
      break;
    case 10:
      zId=static_cast<HanMacWrdJGraphInternal::CommentFrame const &>(*frame).m_zId;
      break;
    default:
      break;
    }
    if (!zId) continue;
    if (mapIdType.find(zId) == mapIdType.end())
      mapIdType[zId] = frame->m_type;
    else if (mapIdType.find(zId)->second != frame->m_type) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::getTextFrameInformations: id %lx already set\n", static_cast<long unsigned int>(zId)));
    }
  }
  return mapIdType;
}

bool HanMacWrdJGraph::getFootnoteInformations(long &textZId, std::vector<long> &fPosList) const
{
  fPosList.clear();
  textZId = 0;
  for (auto frame : m_state->m_framesList) {
    if (!frame || !frame->valid() || frame->m_type != 3)
      continue;
    auto const &text=static_cast<HanMacWrdJGraphInternal::TextFrame const &>(*frame);
    if (textZId && text.m_zId != textZId) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: find different textIds\n"));
    }
    else if (!textZId)
      textZId = text.m_zId;
    fPosList.push_back(text.m_cPos);
  }
  return fPosList.size();
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool HanMacWrdJGraph::readFrames(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: called without any entry\n"));
    return false;
  }
  if (entry.length() <= 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: the entry seems too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(true);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize != 4 ||
      16+12+mainHeader.m_n*4 > mainHeader.m_length) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: can not read the header\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  long val;
  for (int i = 0; i < 2; ++i) {
    val = long(input->readULong(4));
    f << "id" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i = 0; i < 2; ++i) { // f0:small number, 0
    val = input->readLong(2);
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  f << "listIds=[";
  std::vector<long> lIds(size_t(mainHeader.m_n));
  for (int i = 0; i < mainHeader.m_n; ++i) {
    val = long(input->readULong(4));
    lIds[size_t(i)]=val;
    m_state->m_framesMap[val]=i;
    f << std::hex << val << std::dec << ",";
  }
  f << std::dec << "],";
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  // the data
  m_state->m_framesList.resize(size_t(mainHeader.m_n));
  for (int i = 0; i < mainHeader.m_n; ++i) {
    pos = input->tell();
    auto frame=readFrame(i);
    if (!frame) {
      f << "###";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      return false;
    }
    frame->m_fileId = lIds[size_t(i)];
    m_state->m_framesList[size_t(i)]=frame;
  }

  // normally there remains 2 block, ...

  // block 0
  pos = input->tell();
  f.str("");
  f << entry.name() << "-Format:";
  HanMacWrdJZoneHeader header(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=48) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: can not read auxilliary block A\n"));
    f << "###" << header;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long zoneEnd=pos+4+header.m_length;
  f << header;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < header.m_n; ++i) {
    HanMacWrdJGraphInternal::FrameFormat format;
    MWAWGraphicStyle &style=format.m_style;
    pos=input->tell();
    f.str("");
    val = input->readLong(2);
    if (val != -2)
      f << "f0=" << val << ",";
    val = long(input->readULong(2));
    if (val)
      f << "f1=" << std::hex << val << std::dec << ",";
    for (auto &wrap : format.m_intWrap) wrap = double(input->readLong(4))/65536.;
    for (auto &wrap : format.m_extWrap) wrap = double(input->readLong(4))/65536.;
    style.m_lineWidth= float(input->readLong(4))/65536.f;
    format.m_borderType= static_cast<int>(input->readULong(1));
    for (int j = 0; j < 2; j++) {
      auto color = static_cast<int>(input->readULong(1));
      MWAWColor col = j==0 ? MWAWColor::black() : MWAWColor::white();
      if (!m_state->getColor(color, col))
        f << "#color[" << j << "]=" << color << ",";
      auto pattern = static_cast<int>(input->readULong(1));
      if (pattern==0) {
        if (i==0) style.m_lineOpacity=0;
        else style.m_surfaceOpacity=0;
        continue;
      }
      HanMacWrdJGraphInternal::Pattern pat;
      if (m_state->getPattern(pattern, pat)) {
        pat.m_colors[1]=col;
        if (!pat.getUniqueColor(col)) {
          pat.getAverageColor(col);
          if (j) style.setPattern(pat);
        }
      }
      else
        f << "#pattern[" << j << "]=" << pattern << ",";
      if (j==0)
        style.m_lineColor=col;
      else
        style.setSurfaceColor(col,1);
    }
    for (int j = 0; j < 3; j++) { // always 0
      val = static_cast<int>(input->readULong(1));
      if (val) f << "g" << j << "=" << val << ",";
    }
    format.m_style.m_extra=f.str();
    m_state->m_frameFormatsList.push_back(format);
    f.str("");
    f << entry.name() << "-F" << i << ":" << format;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+48, librevenge::RVNG_SEEK_SET);
  }
  input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);

  // block B
  pos = input->tell();
  f.str("");
  f << entry.name() << "-B:";
  header=HanMacWrdJZoneHeader(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=8 ||
      16+2+header.m_n*8 > header.m_length) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: can not read auxilliary block B\n"));
    f << "###" << header;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  for (int i = 0; i < 2; ++i) { // f0=1|3|4=N?
    val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  f << "unk=[";
  for (int i = 0; i < header.m_n; ++i) {
    f << "[";
    for (int j = 0; j < 2; j++) { // always 0?
      val = input->readLong(2);
      if (val) f << val << ",";
      else f << "_,";
    }
    f << std::hex << input->readULong(4) << std::dec; // id
    f << "],";
  }
  zoneEnd=pos+4+header.m_length;
  f << header;
  input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  // and for each n, a list
  for (int i = 0; i < header.m_n; ++i) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-B" << i << ":";
    HanMacWrdJZoneHeader lHeader(false);
    if (!m_mainParser->readClassicHeader(lHeader,endPos) || lHeader.m_fieldSize!=4) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: can not read auxilliary block B%d\n",i));
      f << "###" << lHeader;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      return false;
    }
    f << "listId?=[" << std::hex;
    for (int j = 0; j < lHeader.m_n; j++) {
      val = long(input->readULong(4));
      f << val << ",";
    }
    f << std::dec << "],";

    zoneEnd=pos+4+lHeader.m_length;
    f << header;
    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  asciiFile.addPos(endPos);
  asciiFile.addNote("_");
  pos = input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrames: find unexpected end data\n"));
    f.str("");
    f << entry.name() << "###:";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }
  return true;
}

std::shared_ptr<HanMacWrdJGraphInternal::Frame> HanMacWrdJGraph::readFrame(int id)
{
  std::shared_ptr<HanMacWrdJGraphInternal::Frame> res;
  HanMacWrdJGraphInternal::Frame graph;
  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  auto len = long(input->readULong(4));
  long endPos = pos+4+len;
  if (len < 32 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrame: can not read the frame length\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return res;
  }

  auto fl = static_cast<int>(input->readULong(1));
  graph.m_type=(fl>>4);
  f << "f0=" << std::hex << (fl&0xf) << std::dec << ",";
  int val;
  /* fl0=[0|1|2|3|4|6|8|9|a|b|c][2|6], fl1=0|1|20|24,
     fl2=0|8|c|e|10|14|14|40|8a, fl3=0|10|80|c0 */
  for (int i = 1; i < 4; ++i) {
    val = static_cast<int>(input->readULong(1));
    if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  graph.m_page = static_cast<int>(input->readLong(2));
  graph.m_formatId = static_cast<int>(input->readULong(2));
  float dim[4];
  for (auto &d : dim) d = float(input->readLong(4))/65536.f;
  graph.m_pos = MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
  graph.m_id = static_cast<int>(input->readLong(2)); // check me
  val = static_cast<int>(input->readLong(2));
  if (val) f << "f1=" << val << ",";
  graph.m_baseline  = float(input->readLong(4))/65536.f;
  graph.m_extra = f.str();

  f.str("");
  f << "FrameDef-" << id << ":" << graph;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  switch (graph.m_type) {
  case 0: // text
  case 1: // header
  case 2: // footer
  case 3: // footnote
    res=readTextData(graph, endPos);
    break;
  case 4:
    res=readTextboxData(graph, endPos);
    break;
  case 6:
    res=readPictureData(graph, endPos);
    break;
  case 8:
    res=readShapeGraph(graph, endPos);
    break;
  case 9:
    res=readTableData(graph, endPos);
    break;
  case 10:
    res=readCommentData(graph, endPos);
    break;
  case 11:
    if (len < 36) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrame: can not read the group id\n"));
      break;
    }
    else {
      auto group = std::make_shared<HanMacWrdJGraphInternal::Group>(graph);
      res = group;
      pos =input->tell();
      group->m_zId = long(input->readULong(4));
      f.str("");
      f << "FrameDef-group:zId=" << std::hex << group->m_zId << std::dec << ",";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      break;
    }
  case 12:
    if (len < 52) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readFrame: can not read the footnote[sep] data\n"));
      break;
    }
    else {
      auto sep = std::make_shared<HanMacWrdJGraphInternal::SeparatorFrame>(graph);
      res = sep;
      pos =input->tell();
      f.str("");
      f << "FrameDef-footnote[sep];";
      for (int i = 0; i < 8; ++i) { // f0=256,f2=8,f4=2,f6=146
        val = static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      f << "zId=" << std::hex << long(input->readULong(4)) << std::dec << ",";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      break;
    }
  default:
    break;
  }
  if (!res)
    res.reset(new HanMacWrdJGraphInternal::Frame(graph));
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return res;
}

bool HanMacWrdJGraph::readGroupData(MWAWEntry const &entry, int actZone)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: called without any entry\n"));
    return false;
  }
  if (entry.length() == 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: find an empty zone\n"));
    entry.setParsed(true);
    return true;
  }
  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: the entry seems too short\n"));
    return false;
  }

  auto frame = m_state->findFrame(11, actZone);
  std::vector<long> dummyList;
  std::vector<long> *idsList=&dummyList;
  if (!frame) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: can not find group %d\n", actZone));
  }
  else {
    auto *group = static_cast<HanMacWrdJGraphInternal::Group *>(frame.get());
    idsList = &group->m_childsList;
  }

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(true);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=4) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: can not read an entry\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;
  f << "listId=[" << std::hex;
  idsList->resize(size_t(mainHeader.m_n), 0);
  for (int i = 0; i < mainHeader.m_n; ++i) {
    auto val = long(input->readULong(4));
    (*idsList)[size_t(i)]=val;
    f << val << ",";
  }
  f << std::dec << "],";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  if (pos!=endPos) {
    f.str("");
    f << entry.name() << "[last]:###";
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGroupData: find unexpected end of data\n"));
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  return true;
}

// try to read the graph data
bool HanMacWrdJGraph::readGraphData(MWAWEntry const &entry, int actZone)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: called without any entry\n"));
    return false;
  }
  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: the entry seems too short\n"));
    return false;
  }

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(false);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=8) {
    // sz=12 is ok, means no data
    if (entry.length() != 12) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: can not read an entry\n"));
      f << "###sz=" << mainHeader.m_length;
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;

  std::vector<MWAWVec2f> lVertices(size_t(mainHeader.m_n));
  f << "listPt=[";
  for (int i = 0; i < mainHeader.m_n; ++i) {
    float point[2];
    for (auto &pt : point) pt = float(input->readLong(4))/65536.f;
    MWAWVec2f pt(point[1], point[0]);
    lVertices[size_t(i)]=pt;
    f << pt << ",";
  }
  f << "],";

  auto frame = m_state->findFrame(8, actZone);
  if (!frame) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: can not find basic graph %d\n", actZone));
  }
  else {
    auto *graph = static_cast<HanMacWrdJGraphInternal::ShapeGraph *>(frame.get());
    if (graph->m_shape.m_type != MWAWGraphicShape::Polygon) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: basic graph %d is not a polygon\n", actZone));
    }
    else {
      graph->m_shape.m_vertices = lVertices;
      for (auto &vertex : graph->m_shape.m_vertices)
        vertex += graph->m_pos[0];
    }
  }

  asciiFile.addPos(entry.begin()+8);
  asciiFile.addNote(f.str().c_str());

  if (headerEnd!=endPos) {
    f.str("");
    f << entry.name() << "[last]:###";
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readGraphData: find unexpected end of data\n"));
    asciiFile.addPos(headerEnd);
    asciiFile.addNote(f.str().c_str());
  }

  return true;
}

// try to read the picture
bool HanMacWrdJGraph::readPicture(MWAWEntry const &entry, int actZone)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readPicture: called without any entry\n"));
    return false;
  }
  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readPicture: the entry seems too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  long pos = entry.begin()+8; // skip header
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  auto sz=long(input->readULong(4));
  if (sz+12 != entry.length()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readPicture: the entry sz seems bad\n"));
    return false;
  }
  f << "Picture:pictSz=" << sz;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  asciiFile.skipZone(entry.begin()+12, entry.end()-1);

  auto frame = m_state->findFrame(6, actZone);
  if (!frame) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readPicture: can not find picture %d\n", actZone));
  }
  else {
    auto *picture = static_cast<HanMacWrdJGraphInternal::PictureFrame *>(frame.get());
    picture->m_entry.setBegin(pos+4);
    picture->m_entry.setLength(sz);
  }

  return true;
}

// table
bool HanMacWrdJGraph::readTable(MWAWEntry const &entry, int actZone)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: called without any entry\n"));
    return false;
  }
  if (entry.length() == 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: find an empty zone\n"));
    entry.setParsed(true);
    return true;
  }
  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: the entry seems too short\n"));
    return false;
  }
  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(true);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=4 ||
      mainHeader.m_length < 16+12+4*mainHeader.m_n) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: can not read an entry\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;
  std::shared_ptr<HanMacWrdJGraphInternal::Table> table(new HanMacWrdJGraphInternal::Table(*this));

  long textId = 0;
  auto frame = m_state->findFrame(9, actZone);
  if (!frame || !frame->valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJTable::readTable: can not find basic table %d\n", actZone));
  }
  else {
    auto *tableFrame = static_cast<HanMacWrdJGraphInternal::TableFrame *>(frame.get());
    tableFrame->m_table = table;
    textId = tableFrame->m_zId;
  }

  table->m_rows = static_cast<int>(input->readULong(1));
  table->m_columns = static_cast<int>(input->readULong(1));
  f << "dim=" << table->m_rows << "x" << table->m_columns << ",";
  long val;
  for (int i = 0; i < 4; ++i) { // f0=4|5|7|8|9, f1=1|7|107, f2=3|4|5|6, f3=0
    val = long(input->readULong(2));
    if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  table->m_height = static_cast<int>(input->readLong(2));
  f << "h=" << table->m_height << ",";
  f << "listId=[" << std::hex;
  std::vector<long> listIds;
  for (int i = 0; i < mainHeader.m_n; ++i) {
    val = long(input->readULong(4));
    listIds.push_back(val);
    f << val << ",";
  }
  f << std::dec << "],";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }

  // first read the row
  for (int i = 0; i < mainHeader.m_n; ++i) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-row" << i << ":";
    HanMacWrdJZoneHeader header(false);
    if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=16) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: can not read zone %d\n", i));
      f << "###" << header;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      if (header.m_length<16 || pos+4+header.m_length>endPos)
        return false;
      input->seek(pos+4+header.m_length, librevenge::RVNG_SEEK_SET);
      continue;
    }
    long zoneEnd=pos+4+header.m_length;
    f << header;

    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    // the different cells in a row
    for (int j = 0; j < header.m_n; j++) {
      pos = input->tell();
      f.str("");
      std::shared_ptr<HanMacWrdJGraphInternal::TableCell> cell(new HanMacWrdJGraphInternal::TableCell(textId));
      cell->setPosition(MWAWVec2i(j,i));
      cell->m_cPos = long(input->readULong(4));
      cell->m_zId = long(input->readULong(4));
      cell->m_flags = static_cast<int>(input->readULong(2));
      if (cell->m_flags&0x80)
        cell->setVAlignment(MWAWCell::VALIGN_CENTER);
      switch ((cell->m_flags>>9)&3) {
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
      if (val) f << "#f0=" << val << ",";
      cell->m_formatId = static_cast<int>(input->readLong(2));
      int dim[2]; // for merge, inactive -> the other limit cell
      for (auto &d : dim) d=static_cast<int>(input->readULong(1));
      if (cell->m_flags & 0x1000) {
        if (dim[1]>=j&&dim[0]>=i)
          cell->setNumSpannedCells(MWAWVec2i(dim[1]+1-j,dim[0]+1-i));
        else {
          static bool first = true;
          if (first) {
            MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: can not determine the span\n"));
            first = false;
          }
          f << "##span=" << dim[1]+1-j << "x" << dim[0]+1-i << ",";
        }
      }
      cell->m_extra = f.str();
      // do not push the ignore cell
      if ((cell->m_flags&0x2000)==0)
        table->add(cell);
      f.str("");
      f << entry.name() << "-cell:" << cell;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    }

    if (input->tell() != zoneEnd) {
      asciiFile.addDelimiter(input->tell(),'|');
      input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    }
  }
  asciiFile.addPos(endPos);
  asciiFile.addNote("_");
  if (input->tell()==endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: can not find the 3 last blocks\n"));
    return true;
  }

  for (int i = 0; i < 2; ++i) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-" << (i==0 ? "rowY" : "colX") << ":";
    HanMacWrdJZoneHeader header(false);
    if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize != 4) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: can not read zone %d\n", i));
      f << "###" << header;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      if (header.m_length<16 || pos+4+header.m_length>endPos)
        return false;
      input->seek(pos+4+header.m_length, librevenge::RVNG_SEEK_SET);
      continue;
    }
    long zoneEnd=pos+4+header.m_length;
    f << header;

    f << "pos=[";
    float prevPos = 0.;
    std::vector<float> dim;
    for (int j = 0; j < header.m_n; j++) {
      float cPos = float(input->readULong(4))/65536.f;
      f << cPos << ",";
      if (j!=0)
        dim.push_back(cPos-prevPos);
      prevPos=cPos;
    }
    f << "],";
    if (i==0)
      table->setRowsSize(dim);
    else
      table->setColsSize(dim);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  }

  // finally the format
  readTableFormatsList(*table, endPos);
  table->updateCells();

  if (input->tell() != endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTable: find unexpected last block\n"));
    pos = input->tell();
    f.str("");
    f << entry.name() << "-###:";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  return true;
}

bool HanMacWrdJGraph::readTableFormatsList(HanMacWrdJGraphInternal::Table &table, long endPos)
{
  table.m_formatsList.clear();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f, f2;

  long pos = input->tell();
  f.str("");
  f << "Table-format:";
  HanMacWrdJZoneHeader header(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize != 40) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTableFormatsList: can not read format\n"));
    f << "###" << header;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long zoneEnd=pos+4+header.m_length;
  f << header;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  table.m_formatsList.resize(size_t(header.m_n));
  for (int i = 0; i < header.m_n; ++i) {
    HanMacWrdJGraphInternal::CellFormat format;
    pos = input->tell();
    f.str("");
    long val = input->readLong(2); // always -2
    if (val != -2)
      f << "f0=" << val << ",";
    val = long(input->readULong(2)); // 0|2004|51|1dd4
    if (val)
      f << "#f1=" << std::hex << val << std::dec << ",";

    int color, pattern;
    format.m_borders.resize(4);
    static char const *what[] = {"T", "L", "B", "R"};
    static size_t const which[] = { libmwaw::Top, libmwaw::Left, libmwaw::Bottom, libmwaw::Right };
    for (int b=0; b < 4; b++) {
      f2.str("");
      MWAWBorder border;
      border.m_width=double(input->readLong(4))/65536.;
      auto type = int(input->readLong(1));
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
      color = static_cast<int>(input->readULong(1));
      MWAWColor col = MWAWColor::black();
      if (!m_state->getColor(color, col))
        f2 << "#color=" << color << ",";
      pattern = static_cast<int>(input->readULong(1));
      HanMacWrdJGraphInternal::Pattern pat;
      if (pattern==0) border.m_style=MWAWBorder::None;
      else {
        if (!m_state->getPattern(pattern, pat)) {
          f2 << "#pattern=" << pattern << ",";
          border.m_color = col;
        }
        else
          border.m_color = m_state->getColor(col, pat.m_percent);
      }
      val = long(input->readULong(1));
      if (val) f2 << "unkn=" << val << ",";

      format.m_borders[which[b]] = border;
      if (f2.str().length())
        f << "bord" << what[b] << "=[" << f2.str() << "],";
    }
    color = static_cast<int>(input->readULong(1));
    MWAWColor backCol = MWAWColor::white();
    if (!m_state->getColor(color, backCol))
      f << "#backcolor=" << color << ",";
    pattern = static_cast<int>(input->readULong(1));
    HanMacWrdJGraphInternal::Pattern pat;
    if (!m_state->getPattern(pattern, pat))
      f << "#backPattern=" << pattern << ",";
    else
      format.m_backColor = m_state->getColor(backCol, pat.m_percent);
    format.m_extra = f.str();
    table.m_formatsList[size_t(i)]=format;
    f.str("");
    f << "Table-format" << i << ":" << format;
    asciiFile.addDelimiter(input->tell(),'|');
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+header.m_fieldSize, librevenge::RVNG_SEEK_SET);
  }
  input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  return true;
}


////////////////////////////////////////////////////////////
// send data to a listener
////////////////////////////////////////////////////////////

bool HanMacWrdJGraph::sendFrame(long frameId, MWAWPosition const &pos)
{
  if (!m_parserState->m_textListener) return true;

  auto fIt=m_state->m_framesMap.find(frameId);
  if (fIt == m_state->m_framesMap.end() || fIt->second < 0 || fIt->second >= int(m_state->m_framesList.size())) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: can not find frame %lx\n", static_cast<long unsigned int>(frameId)));
    return false;
  }
  auto frame = m_state->m_framesList[size_t(fIt->second)];
  if (!frame || !frame->valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: frame %lx is not initialized\n", static_cast<long unsigned int>(frameId)));
    return false;
  }
  return sendFrame(*frame, pos);
}

// --- basic shape
bool HanMacWrdJGraph::sendShapeGraph(HanMacWrdJGraphInternal::ShapeGraph const &pict, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  auto pos(lPos);
  if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(pict.getBdBox().size());

  auto const &format=m_state->getFrameFormat(pict.m_formatId);

  MWAWGraphicStyle style(format.m_style);;
  if (pict.m_shape.m_type==MWAWGraphicShape::Line) {
    if (pict.m_arrowsFlag&1) style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
    if (pict.m_arrowsFlag&2) style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
  }

  pos.setOrigin(pos.origin());
  pos.setSize(pos.size()+MWAWVec2f(4,4));
  m_parserState->m_textListener->insertShape(pos,pict.m_shape,style);
  return true;
}

// picture
bool HanMacWrdJGraph::sendPictureFrame(HanMacWrdJGraphInternal::PictureFrame const &pict, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
#ifdef DEBUG_WITH_FILES
  bool firstTime = pict.m_parsed == false;
#endif
  pict.m_parsed = true;
  auto pos(lPos);
  if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(pict.getBdBox().size());

  if (!pict.m_entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendPictureFrame: can not find picture data\n"));
    sendEmptyPicture(pos);
    return true;
  }
  //fixme: check if we have border

  MWAWInputStreamPtr input = m_parserState->m_input;
  long fPos = input->tell();
  input->seek(pict.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  librevenge::RVNGBinaryData data;
  input->readDataBlock(pict.m_entry.length(), data);
  input->seek(fPos, librevenge::RVNG_SEEK_SET);

#ifdef DEBUG_WITH_FILES
  if (firstTime) {
    libmwaw::DebugStream f;
    static int volatile pictName = 0;
    f << "Pict" << ++pictName << ".pct1";
    libmwaw::Debug::dumpFile(data, f.str().c_str());
  }
#endif

  m_parserState->m_textListener->insertPicture(pos, MWAWEmbeddedObject(data, "image/pict"));

  return true;
}

bool HanMacWrdJGraph::sendEmptyPicture(MWAWPosition const &pos)
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

// ----- comment box
bool HanMacWrdJGraph::sendComment(HanMacWrdJGraphInternal::CommentFrame const &comment, MWAWPosition const &lPos, librevenge::RVNGPropertyList const &extras)
{
  if (!m_parserState->m_textListener) return true;
  MWAWVec2f commentSz = comment.getBdBox().size();
  if (comment.m_dim[0] > commentSz[0]) commentSz[0]=comment.m_dim[0];
  if (comment.m_dim[1] > commentSz[1]) commentSz[1]=comment.m_dim[1];
  auto pos(lPos);
  pos.setSize(commentSz);

  librevenge::RVNGPropertyList pList(extras);

  auto const &format=m_state->getFrameFormat(comment.m_formatId);

  MWAWGraphicStyle style=format.m_style;
  MWAWBorder border;
  border.m_color=style.m_lineColor;
  border.m_width=double(style.m_lineWidth);
  style.setBorders(libmwaw::LeftBit|libmwaw::BottomBit|libmwaw::RightBit, border);

  border.m_width=20*double(style.m_lineWidth);
  style.setBorders(libmwaw::TopBit, border);

  if (style.hasSurfaceColor())
    style.setBackgroundColor(style.m_surfaceColor);

  MWAWSubDocumentPtr subdoc(new HanMacWrdJGraphInternal::SubDocument(*this, m_parserState->m_input, HanMacWrdJGraphInternal::SubDocument::Text, comment.m_zId));
  m_parserState->m_textListener->insertTextBox(pos, subdoc, style);

  return true;
}

// ----- textbox
bool HanMacWrdJGraph::sendTextbox(HanMacWrdJGraphInternal::TextboxFrame const &textbox, MWAWPosition const &lPos)
{
  if (!m_parserState->m_textListener) return true;
  auto pos(lPos);
  if (pos.size()[0] <= 0 || pos.size()[1] <= 0)
    pos.setSize(textbox.getBdBox().size());

  auto const &format=m_state->getFrameFormat(textbox.m_formatId);
  MWAWGraphicStyle style;
  format.addTo(style);
  MWAWSubDocumentPtr subdoc;
  if (!textbox.m_isLinked)
    subdoc.reset(new HanMacWrdJGraphInternal::SubDocument(*this, m_parserState->m_input, HanMacWrdJGraphInternal::SubDocument::Text, textbox.m_zId));
  else {
    librevenge::RVNGString fName;
    fName.sprintf("Frame%ld", textbox.m_fileId);
    style.m_frameName=fName.cstr();
  }
  if (textbox.m_linkToFId) {
    librevenge::RVNGString fName;
    fName.sprintf("Frame%ld", textbox.m_linkToFId);
    style.m_frameNextName=fName.cstr();
  }
  m_parserState->m_textListener->insertTextBox(pos, subdoc, style);

  return true;
}

// ----- table
bool HanMacWrdJGraph::sendTableUnformatted(long fId)
{
  if (!m_parserState->m_textListener)
    return true;
  auto fIt = m_state->m_framesMap.find(fId);
  if (fIt == m_state->m_framesMap.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendTableUnformatted: can not find the table frame %lx\n", static_cast<long unsigned int>(fId)));
    return false;
  }
  int id = fIt->second;
  if (id < 0 || id >= static_cast<int>(m_state->m_framesList.size()))
    return false;
  auto &frame = *m_state->m_framesList[size_t(id)];
  if (!frame.valid() || frame.m_type != 9) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendTableUnformatted: can not find the table frame %lx(II)\n", static_cast<long unsigned int>(fId)));
    return false;
  }
  auto &tableFrame = static_cast<HanMacWrdJGraphInternal::TableFrame &>(frame);
  if (!tableFrame.m_table) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendTableUnformatted: can not find the table\n"));
    return false;
  }
  tableFrame.m_table->sendAsText(m_parserState->m_textListener);
  return true;
}

////////////////////////////////////////////////////////////
// low level
////////////////////////////////////////////////////////////
bool HanMacWrdJGraph::sendFrame(HanMacWrdJGraphInternal::Frame const &frame, MWAWPosition const &lPos)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) return true;

  if (!frame.valid()) {
    frame.m_parsed = true;
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: called with invalid frame\n"));
    return false;
  }

  MWAWInputStreamPtr &input= m_parserState->m_input;
  auto pos(lPos);
  switch (frame.m_type) {
  case 4: {
    frame.m_parsed = true;
    auto const &format=m_state->getFrameFormat(frame.m_formatId);
    if (format.m_style.hasPattern()) {
      auto const &textbox=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame);
      if (!textbox.isLinked() && m_mainParser->canSendTextAsGraphic(textbox.m_zId,0)) {
        MWAWSubDocumentPtr subdoc
        (new HanMacWrdJGraphInternal::SubDocument(*this, input, HanMacWrdJGraphInternal::SubDocument::Text, textbox.m_zId));
        MWAWBox2f box(MWAWVec2f(0,0),pos.size());
        MWAWGraphicEncoder graphicEncoder;
        MWAWGraphicListener graphicListener(*m_parserState, box, &graphicEncoder);
        graphicListener.startDocument();
        MWAWPosition textPos(box[0], box.size(), librevenge::RVNG_POINT);
        textPos.m_anchorTo=MWAWPosition::Page;
        graphicListener.insertTextBox(textPos, subdoc, format.m_style);
        graphicListener.endDocument();
        MWAWEmbeddedObject picture;
        if (!graphicEncoder.getBinaryResult(picture))
          return false;
        listener->insertPicture(pos, picture);
        return true;
      }
    }
    return sendTextbox(static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame), pos);
  }
  case 6: {
    auto const &pict = static_cast<HanMacWrdJGraphInternal::PictureFrame const &>(frame);
    if (!pict.m_entry.valid()) {
      pos.setSize(pict.getBdBox().size());

      frame.m_parsed = true;
      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));

      MWAWSubDocumentPtr subdoc
      (new HanMacWrdJGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdJGraphInternal::SubDocument::EmptyPicture, 0));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    return sendPictureFrame(pict, pos);
  }
  case 8:
    frame.m_parsed = true;
    return sendShapeGraph(static_cast<HanMacWrdJGraphInternal::ShapeGraph const &>(frame), pos);
  case 9: {
    frame.m_parsed = true;
    auto const &tableFrame = static_cast<HanMacWrdJGraphInternal::TableFrame const &>(frame);
    if (!tableFrame.m_table) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: can not find the table\n"));
      return false;
    }
    auto &table = *tableFrame.m_table;

    if (!table.updateTable()) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: can not find the table structure\n"));
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdJGraphInternal::SubDocument
       (*this, input, HanMacWrdJGraphInternal::SubDocument::UnformattedTable, frame.m_fileId));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    if (pos.m_anchorTo==MWAWPosition::Page ||
        (pos.m_anchorTo!=MWAWPosition::Frame && table.hasExtraLines())) {
      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));

      MWAWSubDocumentPtr subdoc
      (new HanMacWrdJGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdJGraphInternal::SubDocument::FrameInFrame, frame.m_fileId));
      pos.setSize(MWAWVec2f(-0.01f,-0.01f)); // autosize
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    if (table.sendTable(listener, pos.m_anchorTo==MWAWPosition::Frame))
      return true;
    return table.sendAsText(listener);
  }
  case 10:
    frame.m_parsed = true;
    return sendComment(static_cast<HanMacWrdJGraphInternal::CommentFrame const &>(frame), pos);
  case 11: {
    auto const &group=static_cast<HanMacWrdJGraphInternal::Group const &>(frame);
    if ((pos.m_anchorTo==MWAWPosition::Char || pos.m_anchorTo==MWAWPosition::CharBaseLine) && !canCreateGraphic(group)) {
      MWAWPosition framePos(pos);
      framePos.m_anchorTo = MWAWPosition::Frame;
      framePos.setOrigin(MWAWVec2f(0,0));
      pos.setSize(group.getBdBox().size());
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdJGraphInternal::SubDocument
       (*this, input, framePos, HanMacWrdJGraphInternal::SubDocument::Group, group.m_fileId));
      listener->insertTextBox(pos, subdoc);
      return true;
    }
    sendGroup(group, pos);
    break;
  }
  default:
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendFrame: sending type %d is not implemented\n", frame.m_type));
    break;
  }
  frame.m_parsed = true;
  return false;
}

// try to read a basic comment zone
std::shared_ptr<HanMacWrdJGraphInternal::CommentFrame> HanMacWrdJGraph::readCommentData(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::CommentFrame> comment;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+40) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readCommentData: the zone seems too short\n"));
    return comment;
  }
  comment.reset(new HanMacWrdJGraphInternal::CommentFrame(header));
  comment->m_width = double(input->readLong(4))/65536.;
  long val = input->readLong(2); // small number between 1 and 0x17
  if (val!=1)
    f << "f0=" << val << ",";
  val = input->readLong(2);// always 0?
  if (val)
    f << "f1=" << val << ",";
  comment->m_cPos = long(input->readULong(4));
  val = long(input->readULong(4));
  f << "id0=" << std::hex << val << std::dec << ",";
  comment->m_zId = long(input->readULong(4));
  for (int i=0; i < 4; ++i) { // g2=8000 if close?
    val = input->readLong(2);
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  float dim[2];
  for (auto &d : dim) d = float(input->readLong(4))/65536.f;
  comment->m_dim=MWAWVec2f(dim[1],dim[0]);
  for (int i=0; i < 2; ++i) {
    val = input->readLong(2);
    if (val)
      f << "g" << i+4 << "=" << val << ",";
  }

  std::string extra=f.str();
  comment->m_extra += extra;
  f.str("");
  f << "FrameDef(Comment-data):" << comment->print() << extra;
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return comment;
}

// try to read a basic picture zone
std::shared_ptr<HanMacWrdJGraphInternal::PictureFrame> HanMacWrdJGraph::readPictureData(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::PictureFrame> picture;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+40) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readPictureData: the zone seems too short\n"));
    return picture;
  }
  picture.reset(new HanMacWrdJGraphInternal::PictureFrame(header));
  long val;
  for (int i=0; i < 2; ++i) { // always 0
    val = input->readLong(2);
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  float fDim[2]; // a small size, typically 1x1
  for (auto &d : fDim) d = float(input->readLong(4))/65536.f;
  picture->m_scale = MWAWVec2f(fDim[0], fDim[1]);
  picture->m_zId = long(input->readULong(4));
  for (int i = 0; i < 2; ++i) { // f2=0, f3=0|-1 : maybe front/back color?
    val = input->readLong(4);
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int dim[2];
  for (auto &d: dim) d = int(input->readLong(2));
  picture->m_dim=MWAWVec2i(dim[0],dim[1]); // checkme: xy
  for (int i = 0; i < 6; ++i) { // g2=8400
    val = long(input->readULong(2));
    if (val)
      f << "g" << i << "=" << std::hex << val << std::dec << ",";
  }
  std::string extra = f.str();
  picture->m_extra += extra;
  f.str("");
  f << "FrameDef(picture-data):" << picture->print() << extra;
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return picture;
}

// try to read a basic table zone
std::shared_ptr<HanMacWrdJGraphInternal::TableFrame> HanMacWrdJGraph::readTableData(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::TableFrame> table;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+28) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTableData: the zone seems too short\n"));
    return table;
  }
  table.reset(new HanMacWrdJGraphInternal::TableFrame(header));
  table->m_width = double(input->readLong(4))/65536.;
  long val = input->readLong(2); // small number between 1 and 3
  if (val!=1)
    f << "f0=" << val << ",";
  val = input->readLong(2);// always 0?
  if (val)
    f << "f1=" << val << ",";
  table->m_length = long(input->readULong(4));
  val = long(input->readULong(4));
  f << "id0=" << std::hex << val << std::dec << ",";
  table->m_zId = long(input->readULong(4));
  for (int i = 0; i < 2; ++i) {
    val = input->readLong(2);
    if (val) f << "f" << i+2 << "=" << val << ",";
  }
  val = long(input->readULong(4));
  f << "id1=" << std::hex << val << std::dec << ",";
  std::string extra=f.str();
  table->m_extra += extra;
  f.str("");
  f << "FrameDef(table-data):" << table->print() << extra;
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return table;
}

// try to read a basic text box zone
std::shared_ptr<HanMacWrdJGraphInternal::TextboxFrame> HanMacWrdJGraph::readTextboxData(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::TextboxFrame> textbox;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+24) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTextboxData: the zone seems too short\n"));
    return textbox;
  }
  textbox.reset(new HanMacWrdJGraphInternal::TextboxFrame(header));
  textbox->m_width = double(input->readLong(4))/65536.;
  long val = input->readLong(2); // small number between 1 and 0x17
  if (val!=1)
    f << "f0=" << val << ",";
  val = input->readLong(2);// always 0?
  if (val)
    f << "f1=" << val << ",";
  textbox->m_cPos = long(input->readULong(4));
  val = long(input->readULong(4));
  f << "id0=" << std::hex << val << std::dec << ",";
  textbox->m_zId = long(input->readULong(4));
  float dim = float(input->readLong(4))/65536.f; // a small negative number: 0, -4 or -6.5
  if (dim < 0 || dim > 0)
    f << "dim?=" << dim << ",";
  std::string extra=f.str();
  textbox->m_extra += extra;
  f.str("");
  f << "FrameDef(Textbox-data):" << textbox->print() << extra;
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return textbox;
}

// try to read a basic text zone
std::shared_ptr<HanMacWrdJGraphInternal::TextFrame> HanMacWrdJGraph::readTextData(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::TextFrame> text;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+20) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readTextData: the zone seems too short\n"));
    return text;
  }
  text.reset(new HanMacWrdJGraphInternal::TextFrame(header));
  text->m_width = double(input->readLong(4))/65536.;
  long val = input->readLong(2); // small number between 1 and 0x17
  if (val!=1)
    f << "f0=" << val << ",";
  val = input->readLong(2);// always 0?
  if (val)
    f << "f1=" << val << ",";
  text->m_cPos = long(input->readULong(4));
  val = long(input->readULong(4));
  f << "id0=" << std::hex << val << std::dec << ",";
  text->m_zId = long(input->readULong(4));

  std::string extra=f.str();
  text->m_extra += extra;
  f.str("");
  f << "FrameDef(Text-data):" << text->print() << extra;
  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return text;
}

// try to read a small graphic
std::shared_ptr<HanMacWrdJGraphInternal::ShapeGraph> HanMacWrdJGraph::readShapeGraph(HanMacWrdJGraphInternal::Frame const &header, long endPos)
{
  std::shared_ptr<HanMacWrdJGraphInternal::ShapeGraph> graph;

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;

  long pos = input->tell();
  if (endPos<pos+36) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::readShapeGraph: the zone seems too short\n"));
    return graph;
  }

  graph.reset(new HanMacWrdJGraphInternal::ShapeGraph(header));
  long val = static_cast<int>(input->readULong(1));
  auto graphType = static_cast<int>(val>>4);
  auto flag = int(val&0xf);
  bool isLine = graphType==0 || graphType==3;
  bool ok = graphType >= 0 && graphType < 7;
  MWAWBox2f bdbox=graph->m_pos;
  MWAWGraphicShape &shape=graph->m_shape;
  shape = MWAWGraphicShape();
  shape.m_bdBox = shape.m_formBox = bdbox;
  if (isLine) {
    graph->m_arrowsFlag = (flag>>2)&0x3;
    flag &= 0x3;
  }
  auto flag1 = static_cast<int>(input->readULong(1));
  float angles[2]= {0,0};
  if (graphType==5) { // arc
    auto transf = static_cast<int>((2*(flag&1)) | (flag1>>7));
    int decal = (transf%2) ? 4-transf : transf;
    angles[0] = float(-90*decal);
    angles[1] = float(90-90*decal);
    flag &= 0xe;
    flag1 &= 0x7f;
  }
  if (flag) f << "#fl0=" << std::hex << flag << std::dec << ",";
  if (flag1) f << "#fl1=" << std::hex << flag1 << std::dec << ",";
  val = input->readLong(2); // always 0
  if (val) f << "f0=" << val << ",";

  val = input->readLong(4);
  float cornerDim=0;
  if (graphType==4)
    cornerDim = float(val)/65536.f;
  else if (val)
    f << "#cornerDim=" << val << ",";
  if (isLine) {
    shape.m_type=MWAWGraphicShape::Line;
    float coord[2];
    for (int pt = 0; pt < 2; ++pt) {
      for (auto &c : coord) c = float(input->readLong(4))/65536.f;
      shape.m_vertices.push_back(MWAWVec2f(coord[1],coord[0]));
    }
  }
  else {
    switch (graphType) {
    // case 0: already treated in isLine block
    case 1:
      shape.m_type = MWAWGraphicShape::Rectangle;
      break;
    case 2:
      shape.m_type = MWAWGraphicShape::Circle;
      break;
    // case 3: already treated in isLine block
    case 4:
      shape.m_type = MWAWGraphicShape::Rectangle;
      for (int c=0; c < 2; ++c) {
        if (2.f*cornerDim <= bdbox.size()[c])
          shape.m_cornerWidth[c]=cornerDim;
        else
          shape.m_cornerWidth[c]=bdbox.size()[c]/2.0f;
      }
      break;
    case 5: {
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
      break;
    }
    case 6:
      shape.m_type = MWAWGraphicShape::Polygon;
      break;
    default:
      break;
    }
    for (int i = 0; i < 4; ++i) {
      val = input->readLong(4);
      if (val) f << "#coord" << i << "=" << val << ",";
    }
  }
  auto id = long(input->readULong(4));
  if (id) {
    if (graphType!=6)
      f << "#id0=" << std::hex << id << std::dec << ",";
    else
      f << "id[poly]=" << std::hex << id << std::dec << ",";
  }
  id = long(input->readULong(4));
  f << "id=" << std::hex << id << std::dec << ",";
  for (int i = 0; i < 2; ++i) { // always 1|0
    val = input->readLong(2);
    if (val) f << "g" << i << "=" << val << ",";
  }
  std::string extra = f.str();
  graph->m_extra += extra;

  f.str("");
  f << "FrameDef(basicGraphic-data):" << graph->print() << extra;

  if (input->tell() != endPos)
    asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (!ok)
    graph.reset();
  return graph;
}

////////////////////////////////////////////////////////////
// prepare data
////////////////////////////////////////////////////////////
void HanMacWrdJGraph::prepareStructures()
{
  std::multimap<long,size_t> textZoneFrameMap;
  auto numFrames = int(m_state->m_framesList.size());
  for (auto fIt : m_state->m_framesMap) {
    int id = fIt.second;
    if (id < 0 || id >= numFrames || !m_state->m_framesList[size_t(id)])
      continue;
    auto const &frame = *m_state->m_framesList[size_t(id)];
    if (!frame.valid() || frame.m_type!=4)
      continue;
    auto const &text = static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame);
    if (!text.m_zId) continue;
    textZoneFrameMap.insert(std::multimap<long,size_t>::value_type(text.m_zId, size_t(id)));
  }
  auto tbIt=textZoneFrameMap.begin();
  while (tbIt!=textZoneFrameMap.end()) {
    long textId=tbIt->first;
    std::map<long, HanMacWrdJGraphInternal::TextboxFrame *> nCharTextMap;
    bool ok=true;
    while (tbIt!=textZoneFrameMap.end() && tbIt->first==textId) {
      size_t id=tbIt++->second;
      auto &text = static_cast<HanMacWrdJGraphInternal::TextboxFrame &>(*m_state->m_framesList[size_t(id)]);
      if (nCharTextMap.find(text.m_cPos)!=nCharTextMap.end()) {
        MWAW_DEBUG_MSG(("HanMacWrdJGraph::prepareStructures: pos %ld already exist for textZone %lx\n",
                        text.m_cPos, static_cast<long unsigned int>(textId)));
        ok=false;
      }
      else
        nCharTextMap[text.m_cPos]=&text;
    }
    size_t numIds=nCharTextMap.size();
    if (!ok || numIds<=1) continue;
    HanMacWrdJGraphInternal::TextboxFrame *prevText=nullptr;
    for (auto ctIt : nCharTextMap) {
      HanMacWrdJGraphInternal::TextboxFrame *newText=ctIt.second;
      if (prevText) {
        prevText->m_linkToFId=newText->m_fileId;
        newText->m_isLinked=true;
      }
      prevText=newText;
    }
  }
  // now check that there is no loop
  for (auto fIt : m_state->m_framesMap) {
    int id = fIt.second;
    if (id < 0 || id >= numFrames || !m_state->m_framesList[size_t(id)])
      continue;
    auto const &frame = *m_state->m_framesList[size_t(id)];
    if (!frame.valid() || frame.m_inGroup || frame.m_type!=11)
      continue;
    std::set<long> seens;
    checkGroupStructures(fIt.first, seens, false);
  }
}

bool HanMacWrdJGraph::checkGroupStructures(long zId, std::set<long> &seens, bool inGroup)
{
  while (seens.find(zId)!=seens.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::checkGroupStructures: zone %ld already find\n", zId));
    return false;
  }
  seens.insert(zId);
  auto fIt= m_state->m_framesMap.find(zId);
  if (fIt==m_state->m_framesMap.end() || fIt->second < 0 ||
      fIt->second >= static_cast<int>(m_state->m_framesList.size()) || !m_state->m_framesList[size_t(fIt->second)]) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::checkGroupStructures: can not find zone %ld\n", zId));
    return false;
  }
  auto &frame = *m_state->m_framesList[size_t(fIt->second)];
  frame.m_inGroup=inGroup;
  if (!frame.valid() || frame.m_type!=11)
    return true;
  auto &group = static_cast<HanMacWrdJGraphInternal::Group &>(frame);
  for (size_t c=0; c < group.m_childsList.size(); ++c) {
    if (checkGroupStructures(group.m_childsList[c], seens, true))
      continue;
    group.m_childsList.resize(c);
    break;
  }
  return true;
}

////////////////////////////////////////////////////////////
// send group
////////////////////////////////////////////////////////////
bool HanMacWrdJGraph::sendGroup(long fId, MWAWPosition const &pos)
{
  if (!m_parserState->m_textListener)
    return true;
  auto fIt = m_state->m_framesMap.find(fId);
  if (fIt == m_state->m_framesMap.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroup: can not find table %lx\n", static_cast<long unsigned int>(fId)));
    return false;
  }
  int id = fIt->second;
  if (id < 0 || id >= static_cast<int>(m_state->m_framesList.size()))
    return false;
  auto &frame = *m_state->m_framesList[size_t(id)];
  if (!frame.valid() || frame.m_type != 11) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroup: can not find table %lx(II)\n", static_cast<long unsigned int>(fId)));
    return false;
  }
  return sendGroup(static_cast<HanMacWrdJGraphInternal::Group &>(frame), pos);
}

bool HanMacWrdJGraph::sendGroup(HanMacWrdJGraphInternal::Group const &group, MWAWPosition const &pos)
{
  group.m_parsed=true;
  sendGroupChild(group,pos);
  return true;
}

bool HanMacWrdJGraph::canCreateGraphic(HanMacWrdJGraphInternal::Group const &group)
{
  int page = group.m_page;
  auto numFrames = int(m_state->m_framesList.size());
  for (auto fId : group.m_childsList) {
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end() || fIt->second < 0 || fIt->second >= numFrames ||
        !m_state->m_framesList[size_t(fIt->second)])
      continue;
    auto const &frame=*m_state->m_framesList[size_t(fIt->second)];
    if (frame.m_page!=page) return false;
    switch (frame.m_type) {
    case 4: {
      auto const &text=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame);
      if (text.isLinked() || !m_mainParser->canSendTextAsGraphic(text.m_zId,0))
        return false;
      break;
    }
    case 8: // shape
      break;
    case 11:
      if (!canCreateGraphic(static_cast<HanMacWrdJGraphInternal::Group const &>(frame)))
        return false;
      break;
    default:
      return false;
    }
  }
  return true;
}

void HanMacWrdJGraph::sendGroup(HanMacWrdJGraphInternal::Group const &group, MWAWGraphicListenerPtr const &listener)
{
  if (!listener) return;
  group.m_parsed=true;
  MWAWInputStreamPtr &input= m_parserState->m_input;
  auto numFrames = int(m_state->m_framesList.size());
  for (auto fId : group.m_childsList) {
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end()  || fIt->second < 0 || fIt->second >= numFrames ||
        !m_state->m_framesList[size_t(fIt->second)])
      continue;
    auto const &frame=*m_state->m_framesList[size_t(fIt->second)];
    MWAWBox2f box=frame.getBdBox();
    auto const &format=m_state->getFrameFormat(frame.m_formatId);
    MWAWPosition pictPos(box[0], box.size(), librevenge::RVNG_POINT);
    pictPos.m_anchorTo=MWAWPosition::Page;
    switch (frame.m_type) {
    case 4: {
      frame.m_parsed=true;
      auto const &textbox=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame);
      MWAWSubDocumentPtr subdoc
      (new HanMacWrdJGraphInternal::SubDocument(*this, input, HanMacWrdJGraphInternal::SubDocument::Text, textbox.m_zId));
      listener->insertTextBox(pictPos, subdoc, format.m_style);
      break;
    }
    case 8: {
      frame.m_parsed=true;
      auto const &shape=static_cast<HanMacWrdJGraphInternal::ShapeGraph const &>(frame);
      MWAWGraphicStyle style(format.m_style);;
      if (shape.m_shape.m_type==MWAWGraphicShape::Line) {
        if (shape.m_arrowsFlag&1) style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
        if (shape.m_arrowsFlag&2) style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
      }
      listener->insertShape(pictPos, shape.m_shape, style);
      break;
    }
    case 11:
      sendGroup(static_cast<HanMacWrdJGraphInternal::Group const &>(frame), listener);
      break;
    default:
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroup: unexpected type %d\n", frame.m_type));
      break;
    }
  }
}

void HanMacWrdJGraph::sendGroupChild(HanMacWrdJGraphInternal::Group const &group, MWAWPosition const &pos)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroupChild: can not find the listeners\n"));
    return;
  }
  size_t numChilds=group.m_childsList.size(), childNotSent=0;
  if (!numChilds) return;

  int numDataToMerge=0;
  MWAWBox2f partialBdBox;
  MWAWPosition partialPos(pos);
  MWAWInputStreamPtr &input= m_parserState->m_input;
  auto numFrames = int(m_state->m_framesList.size());
  for (size_t c=0; c<numChilds; ++c) {
    long fId=group.m_childsList[c];
    auto fIt=m_state->m_framesMap.find(fId);
    if (fIt == m_state->m_framesMap.end()  || fIt->second < 0 || fIt->second >= numFrames ||
        !m_state->m_framesList[size_t(fIt->second)]) {
      MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroupChild: can not find child %lx\n", static_cast<long unsigned int>(fId)));
      continue;
    }
    auto const &frame=*m_state->m_framesList[size_t(fIt->second)];
    bool canMerge=false;
    if (frame.m_page==group.m_page) {
      switch (frame.m_type) {
      case 4: {
        auto const &text=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(frame);
        canMerge=!text.isLinked()&&m_mainParser->canSendTextAsGraphic(text.m_zId,0);
        break;
      }
      case 8: // shape
        canMerge = true;
        break;
      case 11:
        canMerge = canCreateGraphic(static_cast<HanMacWrdJGraphInternal::Group const &>(frame));
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
        long localFId=group.m_childsList[ch];
        fIt=m_state->m_framesMap.find(localFId);
        if (fIt == m_state->m_framesMap.end() || fIt->second < 0 || fIt->second >= numFrames ||
            !m_state->m_framesList[size_t(fIt->second)])
          continue;
        auto const &child=*m_state->m_framesList[size_t(fIt->second)];
        MWAWBox2f box=child.getBdBox();
        auto const &format=m_state->getFrameFormat(child.m_formatId);
        MWAWPosition pictPos(box[0], box.size(), librevenge::RVNG_POINT);
        pictPos.m_anchorTo=MWAWPosition::Page;
        switch (child.m_type) {
        case 4: {
          child.m_parsed=true;
          auto const &textbox=static_cast<HanMacWrdJGraphInternal::TextboxFrame const &>(child);
          MWAWSubDocumentPtr subdoc
          (new HanMacWrdJGraphInternal::SubDocument(*this, input, HanMacWrdJGraphInternal::SubDocument::Text, textbox.m_zId));
          graphicListener->insertTextBox(pictPos, subdoc, format.m_style);
          break;
        }
        case 8: {
          child.m_parsed=true;
          auto const &shape=static_cast<HanMacWrdJGraphInternal::ShapeGraph const &>(child);
          MWAWGraphicStyle style(format.m_style);
          if (shape.m_shape.m_type==MWAWGraphicShape::Line) {
            if (shape.m_arrowsFlag&1) style.m_arrows[0]=MWAWGraphicStyle::Arrow::plain();
            if (shape.m_arrowsFlag&2) style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
          }
          graphicListener->insertShape(pictPos, shape.m_shape, style);
          break;
        }
        case 11:
          sendGroup(static_cast<HanMacWrdJGraphInternal::Group const &>(child), graphicListener);
          break;
        default:
          MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroupChild: unexpected type %d\n", child.m_type));
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
      long localFId=group.m_childsList[childNotSent];
      fIt=m_state->m_framesMap.find(localFId);
      if (fIt == m_state->m_framesMap.end() || fIt->second < 0 || fIt->second >= numFrames ||
          !m_state->m_framesList[size_t(fIt->second)]) {
        MWAW_DEBUG_MSG(("HanMacWrdJGraph::sendGroup: can not find child %lx\n", static_cast<long unsigned int>(localFId)));
        continue;
      }
      auto const &childFrame=*m_state->m_framesList[size_t(fIt->second)];
      MWAWPosition fPos(pos);
      fPos.setOrigin(childFrame.m_pos[0]-group.m_pos[0]+pos.origin());
      fPos.setSize(childFrame.m_pos.size());
      sendFrame(childFrame, fPos);
    }
    numDataToMerge=0;
  }
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool HanMacWrdJGraph::sendPageGraphics(std::vector<long> const &doNotSendIds)
{
  if (!m_parserState->m_textListener)
    return true;
  std::set<long> notSend;
  for (auto id : doNotSendIds)
    notSend.insert(id);
  auto numFrames = int(m_state->m_framesList.size());
  for (auto fIt : m_state->m_framesMap) {
    int id = fIt.second;
    if (notSend.find(fIt.first) != notSend.end() || id < 0 || id >= numFrames ||
        !m_state->m_framesList[size_t(id)])
      continue;
    auto const &frame = *m_state->m_framesList[size_t(id)];
    if (!frame.valid() || frame.m_parsed || frame.m_inGroup)
      continue;
    if (frame.m_type <= 3 || frame.m_type == 12) continue;
    MWAWPosition pos(frame.m_pos[0],frame.m_pos.size(),librevenge::RVNG_POINT);
    pos.setRelativePosition(MWAWPosition::Page);
    pos.setPage(frame.m_page+1);
    sendFrame(frame, pos);
  }
  return true;
}

void HanMacWrdJGraph::flushExtra()
{
  if (!m_parserState->m_textListener)
    return;
  for (auto frame : m_state->m_framesList) {
    if (!frame || !frame->valid() || frame->m_parsed)
      continue;
    if (frame->m_type <= 3 || frame->m_type == 12) continue;
    MWAWPosition pos(MWAWVec2f(0,0),MWAWVec2f(0,0),librevenge::RVNG_POINT);
    pos.setRelativePosition(MWAWPosition::Char);
    sendFrame(*frame, pos);
  }
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
