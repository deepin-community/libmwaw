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
#include <stack>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFont.hxx"
#include "MWAWGraphicEncoder.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSection.hxx"
#include "MWAWSpreadsheetEncoder.hxx"
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5ClusterManager.hxx"
#include "RagTime5Document.hxx"
#include "RagTime5Formula.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5StyleManager.hxx"

#include "RagTime5Spreadsheet.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Spreadsheet */
namespace RagTime5SpreadsheetInternal
{

//! internal: a structure used to store a value in a cell in RagTime5SpreadsheetInternal
struct CellValue {
  //! constructor
  CellValue()
    : m_type(0)
    , m_id(0)
    , m_long(0)
    , m_double(0)
    , m_text("")
    , m_formulaId(0)
    , m_extra("")
  {
  }
  //! update the cell's content
  void update(MWAWCell &cell, MWAWCellContent &content) const
  {
    MWAWCell::Format format=cell.getFormat();
    switch (m_type) {
    case 4:
      format.m_format=MWAWCell::F_NUMBER;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      content.setValue(m_double);
      break;
    case 5:
      format.m_format=MWAWCell::F_DATE;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      content.setValue(m_double+1460);
      break;
    case 6:
      format.m_format=MWAWCell::F_TIME;
      content.m_contentType=MWAWCellContent::C_NUMBER;
      content.setValue(m_double);
      break;
    case 7:
      format.m_format=MWAWCell::F_TEXT;
      content.m_contentType=MWAWCellContent::C_TEXT;
      break;
    default:
      break;
    }
    cell.setFormat(format);
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, CellValue const &cell)
  {
    switch (cell.m_type) {
    case 0: // empty
      break;
    case 1: // cell is masqued because previous cell text is too width, ...
      o << "masked=" << cell.m_id << ",";
      break;
    case 2: // checkme
      o << "nan=" << std::hex << cell.m_long << std::dec << ",";
      break;
    case 4:
      o << "number=" << cell.m_double << ",";
      break;
    case 5:
      o << "date=" << cell.m_double << ",";
      break;
    case 6:
      o << "time=" << cell.m_double << ",";
      break;
    case 7:
      o << "text=\"" << cell.m_text.cstr() << "\",";
      break;
    case 8: // simple text zone
      o << "textZone=" << (cell.m_id&0xFFFFFF) << "[" << (cell.m_id>>24) << "],";
      break;
    case 9:
      o << "zone[id]=" << (cell.m_id&0xFFFFFF) << ":" << (cell.m_id>>24) << ",";
      break;
    case 0xa:
      o << "pict[id]=" << cell.m_id << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellValue::operator<<: unknown type\n"));
      o << "##type=" << cell.m_type << ",";
    }
    if (cell.m_formulaId)
      o << "formulaDefFD-" << cell.m_formulaId << ",";
    o << cell.m_extra;
    return o;
  }
  //! the cell type
  int m_type;
  //! a id value
  unsigned long m_id;
  //! a long value
  long m_long;
  //! a double value
  double m_double;
  //! the text
  librevenge::RVNGString m_text;
  //! the formula id
  int m_formulaId;
  //! extra data
  std::string m_extra;
};

//! a struct to store what a cell contains
struct CellContent {
  //! enum to define the id position
  enum IdPosition { Value=0, Union, GraphicStyle, TextStyle,
                    BorderPrevVStyle, BorderNextVStyle, BorderPrevHStyle, BorderNextHStyle
                  };

  //! constructor
  CellContent(MWAWVec2i const &pos, int plane)
    : m_position(pos)
    , m_plane(plane)
    , m_isMerged(false)
  {
    for (auto &id : m_id) id=-1;
  }
  //! returns true if the cell is merged
  bool isMergedCell() const
  {
    return m_isMerged;
  }
  //! sets the cell content
  void setContent(int id, int contentId)
  {
    if (id<0 || id>=8) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellContent: called with bad id=%d\n", id));
    }
    else
      m_id[id]=contentId;
  }
  //! returns true if the cell has no id or is merged
  bool hasContent() const
  {
    if (m_isMerged) return false;
    for (auto id : m_id) {
      if (id>=0)
        return true;
    }
    return false;
  }
  //! small operator<<
  friend std::ostream &operator<<(std::ostream &o, CellContent const &cell)
  {
    if (cell.m_isMerged) o << "_[" << cell.m_position << "]";
    else if (!cell.hasContent()) o << "*";
    else {
      o << "[";
      for (int i=0; i<8; ++i) {
        if (cell.m_id[i]<0) continue;
        static char const *wh[]= {"V", "U", "G", "T", "bv", "BV", "bh", "BH"};
        o << wh[i] << cell.m_id[i] << ",";
      }
      o << "]";
    }
    return o;
  }
  /** the cell position

   \note if the cell is an merged cell, this corresponds to the first cell*/
  MWAWVec2i m_position;
  //! the cell plane
  int m_plane;
  //! a flag to know if the cell is merged
  bool m_isMerged;
  //! the list of id
  int m_id[8];
};

//! a border style PLC
struct BorderPLC {
  //! constructor
  BorderPLC()
    : m_values(6,0)
  {
  }
  //! constructor
  explicit BorderPLC(std::vector<int> const &values) : m_values(values)
  {
    if (values.size()==6)
      return;
    MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::BorderPLC::BorderPLC: bad value size\n"));
    m_values.resize(6,0);
  }
  //! returns true if the cell is a merged cell
  bool isMergedBorder() const
  {
    return m_values.size()==6 && (m_values[5]&0x300)==0x300;
  }
  //! returns the graphic style border id corresponding to this cell
  int getBorderGraphicStyleId(bool prevCell) const
  {
    return m_values[prevCell ? 0 : 2];
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, BorderPLC const &plc)
  {
    for (size_t i=0; i<6; ++i) {
      int val=plc.m_values[i];
      if (!val) continue;
      switch (i) {
      case 0:
        o << "GS" << val << "[prevCell],";
        break;
      case 2:
        o << "GS" << val << "[nextCell],";
        break;
      case 5:
        if ((val&0x300)==0x300) {
          o << "none[merged],";
          val&=0xFCFF;
        }
        // find also 2
        if (val)
          o << "fl=" << std::hex << val << std::dec << ",";
        break;
      default: // always 0?
        o << "f" << i << "=" << val << ",";
        break;
      }
    }
    return o;
  }
  //! operator==
  bool operator==(BorderPLC const &plc) const
  {
    for (size_t i=0; i<6; ++i) {
      if (m_values[i]!=plc.m_values[i])
        return false;
    }
    return true;
  }
  //! operator!=
  bool operator!=(BorderPLC const &plc) const
  {
    return !(*this==plc);
  }
  //! the values
  std::vector<int> m_values;
};

//! a graphic style PLC
struct GraphicPLC {
  //! constructor
  GraphicPLC()
    : m_graphStyleId(0)
    , m_unknownId(0)
  {
  }
  //! constructor
  explicit GraphicPLC(std::vector<int> const &values)
    : m_graphStyleId(0)
    , m_unknownId(0)
  {
    if (values.size()!=2) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::GraphicPLC::GraphicPLC: bad value size\n"));
      return;
    }
    m_unknownId=values[0];
    m_graphStyleId=values[1];
  }
  //! returns the graphic id
  int getGraphicStyleId() const
  {
    return m_graphStyleId;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, GraphicPLC const &plc)
  {
    if (plc.m_graphStyleId) o << "GS" << plc.m_graphStyleId << ",";
    if (plc.m_unknownId) o << "#unkn=" << plc.m_unknownId << ",";
    return o;
  }
  //! operator==
  bool operator==(GraphicPLC const &plc) const
  {
    return m_graphStyleId==plc.m_graphStyleId && m_unknownId==plc.m_unknownId;
  }
  //! operator!=
  bool operator!=(GraphicPLC const &plc) const
  {
    return !(*this==plc);
  }
  //! the graph style
  int m_graphStyleId;
  //! unknown id (always 0)
  int m_unknownId;
};

//! a text style PLC
struct TextPLC {
  //! constructor
  TextPLC()
    : m_textStyleId(0)
    , m_formatId(0)
    , m_flags(0)
  {
  }
  //! constructor
  explicit TextPLC(std::vector<int> const &values)
    : m_textStyleId(0)
    , m_formatId(0)
    , m_flags(0)
  {
    if (values.size()!=3) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::TextPLC::TextPLC: bad value size\n"));
      return;
    }
    m_textStyleId=values[0];
    m_formatId=values[1];
    m_flags=values[2];
  }
  //! returns the alignement
  MWAWCell::HorizontalAlignment getHorizontalAlignment() const
  {
    switch (m_flags&3) {
    case 1:
      return MWAWCell::HALIGN_LEFT;
    case 2:
      return MWAWCell::HALIGN_CENTER;
    case 3:
      return MWAWCell::HALIGN_RIGHT;
    default:
    case 0: // none
      break;
    }
    return MWAWCell::HALIGN_DEFAULT;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, TextPLC const &plc)
  {
    if (plc.m_textStyleId) o << "TS" << plc.m_textStyleId << ",";
    if (plc.m_formatId) o << "Fo" << plc.m_formatId << ",";
    auto orientationFlags=plc.m_flags>>16;
    if (orientationFlags&3) o << "orientation=" << (orientationFlags&3) << ","; // 0:LR, BT, RL, TB USEME
    if (orientationFlags&0x1c) o << "vert[just]=" << ((orientationFlags&0x1c)>>2) << ","; // 0: standart, 1:T, 2:C, 3:bottom, 4:baseline
    if (orientationFlags&0x20) o << "tategaki,";
    if (orientationFlags&0xffc0) o << "##orient=" << (orientationFlags&0xffc0) << ",";
    auto fl=plc.m_flags&0xffff;
    if (fl) {
      switch (fl&3) {
      case 0: // none
        break;
      case 1:
        o << "align=left,";
        break;
      case 2:
        o << "align=center,";
        break;
      case 3:
        o << "align=right,";
        break;
      default:
        break;
      }
      if (fl&0x180)
        o << "protection=" << ((fl&0x180)>>7) << ",";
      if (fl&0x400)
        o << "no[print],";
      if (fl&0x800)
        o << "no[screen],";
      if (fl&0x1000)
        o << "zero[hide],";
      if (fl&0x2000)
        o << "precision[use,format],";
      if (fl&0x4000)
        o << "formula[preserved],";
      // fl&0x10 related to multitext?
      fl &= 0x82bc;
      if (fl) // find [04][03][0348][08]
        o << "fl=" << std::hex << fl << std::dec << ",";
    }
    return o;
  }
  //! operator==
  bool operator==(TextPLC const &plc) const
  {
    return m_textStyleId==plc.m_textStyleId && m_formatId==plc.m_formatId && m_flags==plc.m_flags;
  }
  //! operator!=
  bool operator!=(TextPLC const &plc) const
  {
    return !(*this==plc);
  }
  //! the text style
  int m_textStyleId;
  //! the format style
  int m_formatId;
  //! low: text flag, high: orientation flags, tategaki, ...
  int m_flags;
};

//! internal: a structure used to store a sheet in RagTime5SpreadsheetInternal
struct Sheet {
  /** a row: a list of cell map */
  struct Row {
    //! constructor
    Row(MWAWVec2i const &row, int plane)
      : m_rows(row)
      , m_columnsToDataMap()
    {
      // create the spreadsheet zone
      m_columnsToDataMap.insert(std::map<MWAWVec2i, CellContent>::value_type(MWAWVec2i(0,15999),CellContent(MWAWVec2i(0,m_rows[0]), plane)));
    }
    //! returns the rows
    MWAWVec2i const &getRows() const
    {
      return m_rows;
    }
    //! returns true if the row is empty
    bool isEmpty() const
    {
      for (auto it : m_columnsToDataMap) {
        if (it.second.hasContent())
          return false;
      }
      return true;
    }
    //! split columns if needed, so that we can insert cells correspond to the cols interval
    void splitColumns(MWAWVec2i const &cols)
    {
      // first find the first columns block
      auto it=m_columnsToDataMap.lower_bound(MWAWVec2i(-1,cols[0]));
      if (it==m_columnsToDataMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Row::splitColumns: argh can not find any column for %d-%d\n", cols[0], cols[1]));
        return;
      }
      while (it!=m_columnsToDataMap.end()) {
        MWAWVec2i contentCols=it->first;
        if (cols[1]<contentCols[0])
          return;
        if (cols[0]<=contentCols[0] && contentCols[1]<=cols[1]) { // no need to split the cell
          ++it;
          continue;
        }
        CellContent content=it->second;
        int breakPos=(cols[0]>contentCols[0] && cols[0]<=contentCols[1]) ? 0 :
                     (cols[1]>=contentCols[0] && cols[1]<contentCols[1]) ? 1 : -1;
        if (breakPos==-1) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Row::splitColumns: argh can not find break pos %d-%d\n", cols[0], cols[1]));
          return;
        }
        m_columnsToDataMap.erase(contentCols);
        int newMinCol=cols[breakPos]+(breakPos==0?0:1);
        m_columnsToDataMap.insert
        (std::map<MWAWVec2i, CellContent>::value_type(MWAWVec2i(contentCols[0], newMinCol-1),content));
        // check if we need to update the cell position
        if (!content.m_isMerged && content.m_id[CellContent::Union]==-1)
          content.m_position[0]=newMinCol;
        else
          content.m_isMerged=true;
        it=m_columnsToDataMap.insert
           (std::map<MWAWVec2i, CellContent>::value_type(MWAWVec2i(newMinCol, contentCols[1]),content)).first;
      }
    }
    //! update the cells content type
    void update(MWAWVec2i const &cols, int id, int contentId, MWAWVec2i const &beginCellPos, std::set<MWAWVec2i> &unsetCell)
    {
      splitColumns(cols);
      auto it=m_columnsToDataMap.lower_bound(MWAWVec2i(-1,cols[0]));
      if (it==m_columnsToDataMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Row::update: argh can not find any column for %d-%d\n", cols[0], cols[1]));
        return;
      }
      while (it!=m_columnsToDataMap.end()) {
        MWAWVec2i const cPos=it->first;
        if (cPos[0]>cols[1]) break;
        if (cPos[0]<cols[0]||cPos[1]>cols[1]) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Row::update: argh can insert some columns for %d-%d\n", cols[0], cols[1]));
          break;
        }
        CellContent &content=(it++)->second;
        if (content.m_isMerged) {
          unsetCell.insert(content.m_position);
          continue;
        }
        content.setContent(id, contentId);
        if (id==CellContent::Union && content.m_position!=beginCellPos) {
          content.m_position=beginCellPos;
          content.m_isMerged=true;
        }
      }
    }
    //! reset each row's cell position to new row position
    void resetMinRow(int row)
    {
      for (auto &it : m_columnsToDataMap) {
        if (it.second.m_id[CellContent::Union]==-1)
          it.second.m_position[1]=row;
        else
          it.second.m_isMerged=true;
      }
    }
    friend std::ostream &operator<<(std::ostream &o, Row const &row)
    {
      for (auto it : row.m_columnsToDataMap)
        o << it.first << ":" << it.second << ",";
      return o;
    }
    //! the rows (min-max)
    MWAWVec2i m_rows;
    //! the map columns to data
    std::map<MWAWVec2i, CellContent> m_columnsToDataMap;
  };

  /** a plane: a list of rows map */
  struct Plane {
    //! constructor
    explicit Plane(int plane)
      : m_plane(plane)
      , m_rowsToDataMap()
      , m_unitedCellMap()
    {
      // create the spreadsheet zone
      m_rowsToDataMap.insert(std::map<MWAWVec2i, Row>::value_type(MWAWVec2i(0,15999),Row(MWAWVec2i(0,15999), plane)));
    }
    //! returns the plane
    int getPlane() const
    {
      return m_plane;
    }
    //! returns true if the row is empty
    bool isEmpty() const
    {
      for (auto it : m_rowsToDataMap) {
        if (!it.second.isEmpty())
          return false;
      }
      return true;
    }

    //! returns the span value corresponding to an id
    MWAWVec2i getSpan(MWAWVec2i const &position) const
    {
      auto it=m_unitedCellMap.find(position);
      if (it!=m_unitedCellMap.end())
        return MWAWVec2i(it->second[0]-it->first[0]+1, it->second[1]-it->first[1]+1);
      return MWAWVec2i(1,1);
    }

    //! split rows if needed, so that we can insert cells correspond to the rows interval
    void splitRows(MWAWVec2i const &rows)
    {
      // first find the first rows block
      auto it=m_rowsToDataMap.lower_bound(MWAWVec2i(-1,rows[0]));
      if (it==m_rowsToDataMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::splitRows: argh can not find any row for %d-%d\n", rows[0], rows[1]));
        return;
      }
      while (it!=m_rowsToDataMap.end()) {
        MWAWVec2i contentRows=it->first;
        if (rows[1]<contentRows[0])
          return;
        if (rows[0]<=contentRows[0] && contentRows[1]<=rows[1]) { // no need to split the cell
          ++it;
          continue;
        }
        Row rContent=it->second;
        int breakPos=(rows[0]>contentRows[0] && rows[0]<=contentRows[1]) ? 0 :
                     (rows[1]>=contentRows[0] && rows[1]<contentRows[1]) ? 1 : -1;
        if (breakPos==-1) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::splitRows: argh can not find break pos %d-%d\n", rows[0], rows[1]));
          return;
        }
        int newMinRow=rows[breakPos]+(breakPos==0?0:1);
        m_rowsToDataMap.erase(contentRows);
        rContent.m_rows=MWAWVec2i(contentRows[0], newMinRow-1);
        m_rowsToDataMap.insert(std::map<MWAWVec2i, Row>::value_type(rContent.m_rows,rContent));

        rContent.resetMinRow(newMinRow);
        rContent.m_rows=MWAWVec2i(newMinRow, contentRows[1]);
        it=m_rowsToDataMap.insert(std::map<MWAWVec2i, Row>::value_type(rContent.m_rows,rContent)).first;
      }
    }
    //! update the cells content type
    void update(Sheet const &sheet, MWAWBox2i const &box, int id, int contentId)
    {
      MWAWVec2i rows(box[0][1], box[1][1]), cols(box[0][0], box[1][0]);
      splitRows(rows);
      auto it=m_rowsToDataMap.lower_bound(MWAWVec2i(-1,rows[0]));
      if (it==m_rowsToDataMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::update: argh can not find any rows for %d-%d\n", rows[0], rows[1]));
        return;
      }
      std::set<MWAWVec2i> unsetCell;
      while (it!=m_rowsToDataMap.end()) {
        MWAWVec2i const rPos=it->first;
        if (rPos[0]>rows[1]) break;
        if (rPos[0]<rows[0]||rPos[1]>rows[1]) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::update: argh can insert some rows for %d-%d\n", rows[0], rows[1]));
          break;
        }
        it->second.update(cols, id, contentId, box[0], unsetCell);
        ++it;
      }
      if (unsetCell.empty() || id==CellContent::GraphicStyle || id==CellContent::TextStyle ||
          id==CellContent::BorderPrevHStyle || id==CellContent::BorderPrevVStyle)
        return;
      // sometimes merged cell have value with no content
      if (id==CellContent::Value && contentId>0 && contentId<=static_cast<int>(sheet.m_valuesList.size()) &&
          sheet.m_valuesList[size_t(contentId-1)].m_type==0)
        return;
      // if the cells have been merged, we need to affect the next border to the original cell
      if (id==CellContent::BorderNextHStyle || id==CellContent::BorderNextVStyle) {
        for (auto cellPos : unsetCell) {
          it=m_rowsToDataMap.lower_bound(MWAWVec2i(-1,cellPos[1]));
          if (it==m_rowsToDataMap.end() || it->first[0]!=cellPos[1]) {
            MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::update: argh can not find a cell to set border: %dx%d\n", cellPos[0], cellPos[1]));
            continue;
          }
          Row &row=it->second;
          auto rIt=row.m_columnsToDataMap.lower_bound(MWAWVec2i(-1,cellPos[0]));
          if (rIt==row.m_columnsToDataMap.end() || rIt->first[0]!=cellPos[0] || rIt->second.m_id[CellContent::Union]<0) {
            MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::update: argh can not find a cell to set border: %dx%d(II)\n", cellPos[0], cellPos[1]));
            continue;
          }
          rIt->second.setContent(id,contentId);
        }
        return;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::Plane::update: argh can not set some cell for id=%d\n", id));
    }

    friend std::ostream &operator<<(std::ostream &o, Plane const &plane)
    {
      if (plane.m_rowsToDataMap.empty()) return o;
      for (auto it : plane.m_rowsToDataMap)
        o << "\t" << it.first << "[" << plane.m_plane << "]:" << it.second << "\n";
      return o;
    }

    //! the plane
    int m_plane;
    //! the map rows to data
    std::map<MWAWVec2i, Row> m_rowsToDataMap;
    //! the list of united cell: map from TL cell to RB cell
    std::map<MWAWVec2i, MWAWVec2i> m_unitedCellMap;
  };

  //! constructor
  Sheet()
    : m_name("")
    , m_textboxZoneId(0)
    , m_colWidthDef(56)
    , m_colWidthsMap()
    , m_rowHeightDef(13)
    , m_rowHeightsMap()
    , m_blockToCellRefMap()
    , m_valueToCellRefMap()
    , m_refToCellRefMap()
    , m_formulaLink()
    , m_idToFormula()
    , m_valuesList()
    , m_planesList()
    , m_graphicPLCList()
    , m_defGraphicPLC()
    , m_textPLCList()
    , m_defTextPLC()
    , m_defaultFont(16, 12)
    , m_defaultParagraph()
    , m_childList()
    , m_isSent(false)
  {
  }
  //! returns a name corresponding to a plane
  librevenge::RVNGString getName(int plane) const
  {
    if (plane==1)
      return m_name;
    librevenge::RVNGString name=m_name, suffix;
    suffix.sprintf("_%d", plane);
    name.append(suffix);
    return name;
  }
  //! increase the number of planes if need
  void increasePlaneSizeIfNeeded(int newPlane)
  {
    if (newPlane<0) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::increasePlaneSizeIfNeeded: called with bad id=%d\n", newPlane));
      return;
    }
    for (auto plane=static_cast<int>(m_planesList.size()); plane<newPlane; ++plane) {
      if (plane>=100) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::increasePlaneSizeIfNeeded: sorry, 100 planes is the arbitrary maximum\n"));
          first=true;
        }
        return;
      }
      m_planesList.push_back(Plane(plane+1));
    }
  }
  //! returns the number of planes
  int getNumPlanes() const
  {
    int numPlanes=0;
    for (size_t plane=m_planesList.size(); plane>=1; --plane) {
      if (!m_planesList[plane-1].isEmpty())
        return static_cast<int>(plane);
    }
    return numPlanes;
  }

  //! stores a plc
  void setPLCValues(MWAWVec3i const &minPos, MWAWVec3i const &maxPos, int plcType, int plcId)
  {
    if (plcType==RagTime5SpreadsheetInternal::CellContent::Value && plcId) {
      if (minPos[2]==maxPos[2]) {
        MWAWCellContent::FormulaInstruction cells;
        cells.m_type=minPos==maxPos ? MWAWCellContent::FormulaInstruction::F_Cell :
                     MWAWCellContent::FormulaInstruction::F_CellList;
        cells.m_position[0]=MWAWVec2i(minPos[0],minPos[1]);
        cells.m_position[1]=MWAWVec2i(maxPos[0],maxPos[1]);
        cells.m_sheet[0]=getName(minPos[2]);
        cells.m_sheet[1]=getName(maxPos[2]);
        m_valueToCellRefMap[plcId]=cells;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::setPLCValues: storing value on multipleplane is not implemented\n"));
      }
    }
    increasePlaneSizeIfNeeded(maxPos[2]);
    MWAWBox2i box(MWAWVec2i(minPos[0],minPos[1]),MWAWVec2i(maxPos[0],maxPos[1]));
    for (int plane=minPos[2]-1; plane<=maxPos[2]-1; ++plane) {
      if (plane<0 || plane>=static_cast<int>(m_planesList.size())) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::setPLCValues: plane %d seems bad\n", plane));
          first=false;
        }
        continue;
      }
      Plane &planeData=m_planesList[size_t(plane)];
      planeData.update(*this, box, plcType, plcId);
    }
  }

  //! stores an union of cells
  void setMergedCells(MWAWVec3i const &minPos, MWAWVec3i const &maxPos)
  {
    increasePlaneSizeIfNeeded(maxPos[2]);
    MWAWBox2i box(MWAWVec2i(minPos[0],minPos[1]),MWAWVec2i(maxPos[0],maxPos[1]));
    for (int plane=minPos[2]-1; plane<=maxPos[2]-1; ++plane) {
      if (plane<0 || plane>=static_cast<int>(m_planesList.size())) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::setMergedCells: plane %d seems bad\n", plane));
        continue;
      }
      Plane &planeData=m_planesList[size_t(plane)];
      planeData.m_unitedCellMap[box[0]]=box[1];
      planeData.update(*this, box, CellContent::Union, 1);
      if (minPos[1]!=maxPos[1]) // split the row to avoid sending merged cells
        planeData.splitRows(MWAWVec2i(minPos[1],minPos[1]));
    }
  }

  //! returns the row height in point
  float getRowHeight(int row) const
  {
    auto rIt=m_rowHeightsMap.lower_bound(MWAWVec2i(-1,row));
    if (rIt==m_rowHeightsMap.end() || rIt->first[0]>row || rIt->first[1]<row)
      return -m_rowHeightDef;
    return rIt->second;
  }
  //! sets the row heights
  void setRowsHeight(MWAWVec2i const &rows, float height)
  {
    if (rows[0]<0 || rows[1]<rows[0]) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::setRowsHeight: the rows %dx%d seems bad\n", rows[0], rows[1]));
      return;
    }
    m_rowHeightDef=m_rowHeightsMap[rows]=height;
  }
  //! returns the col width in point
  float getColWidth(int col) const
  {
    auto rIt=m_colWidthsMap.lower_bound(MWAWVec2i(-1,col));
    if (rIt==m_colWidthsMap.end() || rIt->first[0]>col || rIt->first[1]<col)
      return m_colWidthDef;
    return rIt->second;
  }
  //! returns the col width dimension in point
  std::vector<float> getColumnWidths(std::vector<int> &repeated) const
  {
    std::vector<float> widths;
    repeated.clear();
    int actPos=0;
    for (auto it : m_colWidthsMap) {
      int lastPos=it.first[1];
      if (lastPos<actPos) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::getColumnWidths: the position %d seems bad\n", lastPos));
        continue;
      }
      widths.push_back(it.second);
      repeated.push_back(lastPos+1-actPos);
      actPos=lastPos;
    }
    return widths;
  }
  //! sets the row widths
  void setColsWidth(MWAWVec2i const &cols, float width)
  {
    if (cols[0]<0 || cols[1]<cols[0]) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::setColsWidth: the cols %dx%d seems bad\n", cols[0], cols[1]));
      return;
    }
    m_colWidthDef=m_colWidthsMap[cols]=width;
  }
  //! returns the cell dimension
  MWAWVec2f getCellDimensions(MWAWVec2i const &position, int plane) const
  {
    MWAWVec2i maxPos=getBottomRightCell(position, plane);
    float width=0;
    for (int c=position[0]; c<maxPos[0]; ++c)
      width+=getColWidth(c);
    float height=0;
    for (int r=position[1]; r<maxPos[1]; ++r) {
      float h=getRowHeight(r);
      height+=h<0 ? -h : h;
    }
    return MWAWVec2f(width, height);
  }
  //! returns the bottom right cell
  MWAWVec2i getBottomRightCell(MWAWVec2i const &position, int plane) const
  {
    return position+getSpan(position, plane);
  }
  //! returns the span value corresponding to an id
  MWAWVec2i getSpan(MWAWVec2i const &position, int plane) const
  {
    if (plane<=0 || plane>static_cast<int>(m_planesList.size()))
      return MWAWVec2i(1,1);
    return m_planesList[size_t(plane-1)].getSpan(position);
  }

  //! returns the graphic id
  int getGraphicStyleId(int id) const
  {
    if (id<0 || id>=static_cast<int>(m_graphicPLCList.size())) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::getGraphicStyleId: unknown id=%d\n", id));
      return -1;
    }
    return m_graphicPLCList[size_t(id)].getGraphicStyleId();
  }
  //! retrieves the text plc
  bool getTextPLC(int id, TextPLC &plc) const
  {
    if (id<0 || id>=static_cast<int>(m_textPLCList.size())) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::Sheet::getTextPLC: unknown id=%d\n", id));
      return false;
    }
    plc=m_textPLCList[size_t(id)];
    return true;
  }
  //! the sheet name
  librevenge::RVNGString m_name;
  //! the text zone id
  int m_textboxZoneId;
  //! the default col width in point
  float m_colWidthDef;
  //! the col widths: map for cols positions to width in points
  std::map<MWAWVec2i, float> m_colWidthsMap;
  //! the default row height in point
  float m_rowHeightDef;
  //! the row heights: map from rows positions to height in points
  std::map<MWAWVec2i, float> m_rowHeightsMap;
  //! the list of block id to ref position
  std::map<int, MWAWCellContent::FormulaInstruction> m_blockToCellRefMap;
  //! the list of value id to ref position
  std::map<int, MWAWCellContent::FormulaInstruction> m_valueToCellRefMap;
  //! the list of ref id to ref position
  std::map<int, MWAWCellContent::FormulaInstruction> m_refToCellRefMap;
  //! the formula link
  RagTime5ClusterManager::Link m_formulaLink;
  //! all the formula
  std::map<int, std::vector<MWAWCellContent::FormulaInstruction> > m_idToFormula;
  //! the list of values
  std::vector<CellValue> m_valuesList;
  //! the list of planes
  std::vector<Plane> m_planesList;
  //! the graph plc
  std::vector<GraphicPLC> m_graphicPLCList;
  //! the default graphic plc
  GraphicPLC m_defGraphicPLC;
  //! the text plc
  std::vector<TextPLC> m_textPLCList;
  //! the default text plc
  TextPLC m_defTextPLC;
  //! the default border plc (vertical and horizontal)
  BorderPLC m_defBordersPLC[2];
  //! the default font (Palatino, 12)
  MWAWFont m_defaultFont;
  //! the default paragraph
  MWAWParagraph m_defaultParagraph;
  //! the list of child zone: picture, button, ...
  std::vector<RagTime5StructManager::ZoneLink> m_childList;
  //! a flag to know if the sheet has been sent
  bool m_isSent;
};

//
// parser
//

//! Internal: the helper to read a clustList
struct ClustListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  ClustListParser(RagTime5ClusterManager &clusterManager, int fieldSize, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_clusterList()
    , m_fieldSize(fieldSize)
    , m_clusterManager(clusterManager)
  {
    if (fieldSize!=24 && fieldSize!=60) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ClustListParser::ClustListParser: bad data size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~ClustListParser() final;
  //! return a name which can be used for debugging
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }

  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (!m_fieldSize || endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }

    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    if (listIds[0]) {
      m_clusterList.push_back(listIds[0]);
      // a e,2003,200b, ... cluster
      f << getClusterDebugName(listIds[0]) << ",";
    }
    unsigned long lVal=input->readULong(4); // c00..small number
    if ((lVal&0xc0000000)==0xc0000000)
      f << "f0=" << (lVal&0x3fffffff) << ",";
    else
      f << "f0*" << lVal << ",";
    if (m_fieldSize==24) {
      for (int i=0; i<8; ++i) { // f1=0|1, f2=f3=f4=0, f5=0|c, f6=0|d|e
        auto val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      return true;
    }
    auto val=static_cast<int>(input->readLong(4)); // small int
    if (val) f << "f0=" << val << ",";
    for (int i=0; i<3; ++i) {
      float dim[4];
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      MWAWBox2f box(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3]));
      if (box!=MWAWBox2f(MWAWVec2f(0,0),MWAWVec2f(0,0)))
        f << "dim" << i << "=" << box << ",";
    }
    return true;
  }

  //! the list of read cluster
  std::vector<int> m_clusterList;
private:
  //! the field size
  int m_fieldSize;
  //! the main zone manager
  RagTime5ClusterManager &m_clusterManager;
  //! copy constructor, not implemented
  ClustListParser(ClustListParser &orig) = delete;
  //! copy operator, not implemented
  ClustListParser &operator=(ClustListParser &orig) = delete;
};

ClustListParser::~ClustListParser()
{
}

//! Internal: the helper to read a cell values
struct ValuesParser final : public RagTime5StructManager::DataParser {
  //! constructor
  explicit ValuesParser(Sheet &sheet)
    : RagTime5StructManager::DataParser("SheetValue")
    , m_sheet(sheet)
  {
  }
  //! destructor
  ~ValuesParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &debStream) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz<2) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: bad data size\n"));
      return false;
    }
    libmwaw::DebugStream f;
    RagTime5SpreadsheetInternal::CellValue cell;
    auto type=static_cast<int>(input->readULong(2));
    bool hasIndex[3]= {(type&0x40)!=0, (type&0x80)!=0, (type&0x2000)!=0};
    if (type&0x4E30)
      f << "fl" << std::hex << (type&0x4E30) << std::dec << ",";
    cell.m_type=(type&0x910F);
    bool ok=true;
    switch (cell.m_type) {
    case 0: // empty
      break;
    case 1:
    case 0xa: // componentId(pict)
      if (fSz<4) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: find bad size for long\n"));
        f << "###fSz[long],";
        ok=false;
        break;
      }
      cell.m_id=input->readULong(4);
      break;
    case 2: // find 800b88[47]8, so ?
      if (fSz<4) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: find bad size for long\n"));
        f << "###fSz[long],";
        ok=false;
        break;
      }
      cell.m_long=long(input->readULong(4));
      break;
    case 4: // number
    case 5: // date
    case 6: { // time
      if (fSz<10) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: find bad size for double\n"));
        f << cell << "###fSz[double],";
        ok=false;
        break;
      }
      bool isNan;
      if (!input->readDouble8(cell.m_double, isNan)) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: can not read a double\n"));
        f << "###double,";
        ok=false;
      }
      break;
    }
    case 7: {
      for (int i=0; i<3; ++i) {
        if (!hasIndex[i]) continue;
        if (input->tell()+4>endPos) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: can not read index\n"));
          f << "###index[string],";
          ok=false;
          break;
        }
        hasIndex[i]=false;
        auto val=static_cast<int>(input->readLong(4));
        if (!val) continue;
        if (i==0) // checkme
          cell.m_formulaId=val;
        else
          f << "f" << i << "=" << val << ",";
      }
      if (!ok)
        break;
      if (!RagTime5StructManager::readUnicodeString(input, endPos, cell.m_text)) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: can not read a string\n"));
        f << "###string,";
        ok=false;
      }
      break;
    }
    case 8:
    case 9: { // multiligne text zone
      if (fSz<4) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: find bad size for long2\n"));
        f << "###fSz[long],";
        ok=false;
        break;
      }
      cell.m_id=input->readULong(4);
      break;
    }
    default:
      break;
    }
    for (int i=0; ok && i<3; ++i) {
      if (!hasIndex[i]) continue;
      if (input->tell()+4>endPos) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: can not read an index\n"));
        f << "###index,";
        break;
      }
      auto val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==0)
        cell.m_formulaId=val;
      else
        f << "f" << i << "=" << val << ",";
    }
    if (ok && input->tell()!=endPos) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: find extra data\n"));
      f << "###extra,";
    }
    cell.m_extra=f.str();
    if (n<=0) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::ValuesParser::parse: n value:%d seems bad\n", n));
    }
    else {
      if (n>static_cast<int>(m_sheet.m_valuesList.size()))
        m_sheet.m_valuesList.resize(size_t(n));
      m_sheet.m_valuesList[size_t(n-1)]=cell;
    }
    debStream << "V" << n << "," << cell;
    return true;
  }
  //! the actual sheet
  Sheet &m_sheet;
};

ValuesParser::~ValuesParser()
{
}

//! Internal: the helper to read a list of cell to paragraph/char/... data
struct CellPLCParser final : public RagTime5StructManager::DataParser {
  //! constructor
  CellPLCParser(Sheet &sheet, int which, int fieldSize, std::map<MWAWVec2i,int> const &numRowByPlanes)
    : RagTime5StructManager::DataParser(fieldSize==6 ? "SheetGrphPLC" : fieldSize==10 ? "SheetTxtPLC" : "SheetBordPLC")
    , m_which(which)
    , m_fieldSize(fieldSize)
    , m_row(0)
    , m_planes(1,1)
    , m_sheet(sheet)
    , m_numRowByPlanes(numRowByPlanes)
    , m_numRemainingRows(-1)
  {
    static int const expectedSize[]= {6,10,14,14};
    if (m_which<0 || m_which>=4 || m_fieldSize!=expectedSize[m_which]) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellPLCParser::CellPLCParser: bad field size\n"));
      m_which=-1;
    }
    if (!m_numRowByPlanes.empty()) {
      m_planes=m_numRowByPlanes.begin()->first;
      m_numRemainingRows=m_numRowByPlanes.begin()->second;
    }
  }
  //! destructor
  ~CellPLCParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &zone, int n, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz<2 || (fSz%m_fieldSize)!=4) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellPLCParser::parse: bad data size\n"));
      return false;
    }
    int const maxRow = m_fieldSize==14 ? 16001 : 16000;
    auto numRow=static_cast<int>(input->readULong(2));
    MWAWVec2i planes=m_planes;
    MWAWVec2i rows(m_row, m_row+numRow-1);

    f << "R" << m_row+1;
    if (numRow!=1)
      f << "-" << m_row+numRow;
    f << ",";
    f << "planes=" << planes << ",";
    m_row += numRow;
    if (--m_numRemainingRows==0) {
      auto it=m_numRowByPlanes.find(m_planes);
      if (it!=m_numRowByPlanes.end() && (++it)!=m_numRowByPlanes.end()) {
        m_planes=it->first;
        m_numRemainingRows=it->second;
        m_row=0;
      }
    }
    if (m_numRemainingRows<0 && m_row>=maxRow) {
      // by default, suppose that we add one planes
      m_planes=MWAWVec2i(m_planes[1]+1,m_planes[1]+1);
      m_row=0;
    }
    auto N=static_cast<int>(input->readLong(2));
    f << "N=" << N << ",";
    if (fSz!=4+m_fieldSize*N || (m_fieldSize && (fSz-4)/m_fieldSize<N)) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellPLCParser::parse: N seems bad\n"));
      f << "###";
      return true;
    }
    libmwaw::DebugStream f1;
    libmwaw::DebugFile &ascii=zone.ascii();
    int col=0;
    std::vector<int> values;
    size_t const numValues=m_which==1 ? 3 : size_t(m_fieldSize-2)/2;
    values.resize(numValues);

    /* we limit the number of planes to the current number of planes
       or 20 to avoid creating 16000 sheets, just because the user has
       reset some style in all planes */
    int const maxPlanes=m_sheet.m_planesList.size()>20 ? int(m_sheet.m_planesList.size()+1) : 20;
    for (int i=0; i<N; ++i) {
      pos=input->tell();
      f1.str("");
      f1 << m_name << "-" << n << "-A" << i << ":";
      auto numCol=static_cast<int>(input->readLong(2));
      f1 << "C" << col+1;
      if (numCol!=1)
        f1 << "-"  << col+numCol;
      f1 << ",";
      MWAWVec2i cols(col, col+numCol-1);
      col+=numCol;
      if (m_which==1) {
        for (size_t j=0; j<3; ++j) values[j]=int(input->readULong(j==2 ? 4 : 2));
      }
      else {
        for (auto &val : values) val=static_cast<int>(input->readLong(2));
      }
      switch (m_which) {
      case 0: {
        GraphicPLC plc(values);
        if (plc!=m_sheet.m_defGraphicPLC) {
          f1 << plc;
          for (int plane=planes[0]; plane<=planes[1] && plane<=maxPlanes; ++plane)
            m_sheet.setPLCValues(MWAWVec3i(cols[0],rows[0],plane), MWAWVec3i(cols[1],rows[1],plane),
                                 RagTime5SpreadsheetInternal::CellContent::GraphicStyle, static_cast<int>(m_sheet.m_graphicPLCList.size()));
          m_sheet.m_graphicPLCList.push_back(plc);
        }
        else
          f1 << "def,";
        break;
      }
      case 1: {
        TextPLC plc(values);
        if (plc!=m_sheet.m_defTextPLC) {
          f1 << plc;
          for (int plane=planes[0]; plane<=planes[1] && plane<=maxPlanes; ++plane)
            m_sheet.setPLCValues(MWAWVec3i(cols[0],rows[0],plane), MWAWVec3i(cols[1],rows[1],plane),
                                 RagTime5SpreadsheetInternal::CellContent::TextStyle, static_cast<int>(m_sheet.m_textPLCList.size()));
          m_sheet.m_textPLCList.push_back(plc);
        }
        else
          f1 << "def,";
        break;
      }
      case 2: // vertical border
      case 3: { // horizontal border
        BorderPLC plc(values);
        if (plc!=m_sheet.m_defBordersPLC[m_which-2]) {
          f1 << plc;
          if (plc.isMergedBorder())
            break;
          int bordersId[2]= {plc.getBorderGraphicStyleId(true), plc.getBorderGraphicStyleId(false) };
          for (int wh=0; wh<2; ++wh) {
            if (bordersId[wh]<=0) continue;
            MWAWVec2i finalRows=rows;
            if (wh==0) {
              if (finalRows[0]>0)
                --finalRows[0];
              --finalRows[1];
            }
            else {
              if (finalRows[1]==160000)
                --finalRows[1];
            }
            if (finalRows[0]>finalRows[1])
              continue;
            for (int plane=planes[0]; plane<=planes[1] && plane<=maxPlanes; ++plane) {
              if (m_which==2) // time to invert rows and columns
                m_sheet.setPLCValues(MWAWVec3i(finalRows[0],cols[0],plane), MWAWVec3i(finalRows[1],cols[1],plane),
                                     RagTime5SpreadsheetInternal::CellContent::BorderPrevVStyle+(1-wh), bordersId[wh]);
              else
                m_sheet.setPLCValues(MWAWVec3i(cols[0],finalRows[0],plane), MWAWVec3i(cols[1],finalRows[1],plane),
                                     RagTime5SpreadsheetInternal::CellContent::BorderPrevHStyle+(1-wh), bordersId[wh]);
            }
          }
        }
        else
          f1 << "def,";
        break;
      }
      default:
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::CellPLCParser::parse: find unknown PLC\n"));
        for (size_t j=0; j<values.size(); ++j) {
          if (values[j])
            f1 << "f" << j << "=" << values[j] << ",";
        }
        break;
      }
      input->seek(pos+m_fieldSize, librevenge::RVNG_SEEK_SET);
      ascii.addPos(pos);
      ascii.addNote(f1.str().c_str());
    }
    return true;
  }
  //! the type: 0=graphicStyles, 1=textStyles, 2=vertical border, 3=horizontal border
  int m_which;
  //! the field size
  int m_fieldSize;
  //! the actual row
  int m_row;
  //! the actual plane set
  MWAWVec2i m_planes;
  //! the actual sheet
  Sheet &m_sheet;
  //! the number of row by planes
  std::map<MWAWVec2i,int> const m_numRowByPlanes;
  //! the remaining row in the planes
  int m_numRemainingRows;
private:
  //! copy constructor, not implemented
  CellPLCParser(CellPLCParser &orig) = delete;
  //! copy operator, not implemented
  CellPLCParser &operator=(CellPLCParser &orig) = delete;
};

CellPLCParser::~CellPLCParser()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Spreadsheet
struct State {
  //! constructor
  State()
    : m_idSheetMap()
    , m_namesSet()
    , m_newSheetId(0)
  {
  }
  //! returns a new sheet name
  librevenge::RVNGString getNewSheetName(librevenge::RVNGString const &fileName) const
  {
    auto it=m_namesSet.find(fileName);
    if (!fileName.empty() && it==m_namesSet.end()) {
      m_namesSet.insert(fileName);
      return fileName;
    }
    auto name=fileName;
    if (name.empty())
      name="Sheet";
    while (true) {
      librevenge::RVNGString suffix, finalName(name);
      suffix.sprintf(" %d", ++m_newSheetId);
      finalName.append(suffix);
      it=m_namesSet.find(finalName);
      if (it==m_namesSet.end()) {
        m_namesSet.insert(finalName);
        return finalName;
      }
    }
  }
  //! map data id to sheet zone
  std::map<int, std::shared_ptr<Sheet> > m_idSheetMap;
protected:
  //! the set of name already used
  mutable std::set<librevenge::RVNGString> m_namesSet;
  //! a int uses to generate unique sheet id
  mutable int m_newSheetId;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Spreadsheet::RagTime5Spreadsheet(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_styleManager(m_document.getStyleManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5SpreadsheetInternal::State)
{
}

RagTime5Spreadsheet::~RagTime5Spreadsheet()
{
}

int RagTime5Spreadsheet::version() const
{
  return m_parserState->m_version;
}

int RagTime5Spreadsheet::numPages() const
{
  return m_state->m_idSheetMap.empty() ? 0 : 1;
}

std::vector<int> RagTime5Spreadsheet::getSheetIdList() const
{
  std::vector<int> res;
  for (auto const &it : m_state->m_idSheetMap)
    res.push_back(it.first);
  return res;
}

bool RagTime5Spreadsheet::getFormulaRef(int sheetId, int refId, MWAWCellContent::FormulaInstruction &instruction) const
{
  auto it=m_state->m_idSheetMap.find(sheetId);
  if (it==m_state->m_idSheetMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::getFormulaRef: can not find sheet=%d\n", sheetId));
    return false;
  }
  auto const &sheet=*it->second;
  auto const &rIt=sheet.m_refToCellRefMap.find(refId);
  if (rIt==sheet.m_refToCellRefMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::getFormulaRef: can not find ref %d in sheet=%d\n", refId, sheetId));
    return false;
  }
  instruction=rIt->second;
  return true;
}

void RagTime5Spreadsheet::parseSpreadsheetFormulas()
{
  for (auto const &it : m_state->m_idSheetMap) {
    if (!it.second) continue;
    auto const &sheet=*it.second;
    if (!sheet.m_formulaLink.empty())
      m_document.getFormulaParser()->readFormulaClusters(sheet.m_formulaLink, it.first);
  }
}

void RagTime5Spreadsheet::storeFormula(int sheetId, std::map<int, std::vector<MWAWCellContent::FormulaInstruction> > const &idToFormula)
{
  auto it=m_state->m_idSheetMap.find(sheetId);
  if (it==m_state->m_idSheetMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::storeFormula: can not find sheet=%d\n", sheetId));
    return;
  }
  it->second->m_idToFormula=idToFormula;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool RagTime5Spreadsheet::readSheetDimensions(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link)
{
  MWAWEntry const &entry=zone.m_entry;
  if (!entry.valid() || link.m_fieldSize!=24 || link.m_fieldSize*link.m_N>entry.length()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSheetDimensions: the zone seems bad\n"));
    if (entry.valid()) {
      libmwaw::DebugFile &ascFile=zone.ascii();
      libmwaw::DebugStream f;
      f << "Entries(SheetDims)[" << zone << "]:###";
      zone.m_isParsed=true;
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
    }
    return false;
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(SheetDims)[" << zone << "]:";
  zone.m_isParsed=true;
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();

  int actCPos=0;
  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "SheetDims-" << i+1 << ":";
    long newCPos=input->readLong(4);
    float value=float(input->readLong(4))/65536.f;
    if (newCPos<0 || newCPos>32000) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSheetDimensions: find unexpected newCPos\n"));
      f << "###newCPos=" << newCPos << ",";
    }
    else if (newCPos==0)
      f << "empty,";
    else if (newCPos<=16000) { // between 1 and 16000
      MWAWVec2i cols(actCPos, int(newCPos-1));
      sheet.setColsWidth(cols, value);
      f << "C" << cols << ",";
    }
    else { // between 16001 and 32000
      MWAWVec2i rows(actCPos-16000, int(newCPos-16000-1));
      sheet.setRowsHeight(rows, value);
      f << "R" << rows << ",";
    }
    f << "dim=" << value << ",";
    float dim[2]; // padding, useme
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    if (dim[0]>0||dim[1]>0) f << "padding[beg,end]=" << dim[0] << "x" << dim[1] << ",";
    auto val=long(input->readULong(4));
    if (val) f << "content[width]=" << double(val)/double(0x10000) << ",";
    auto fl=input->readULong(2); // 0|1
    if (fl&1)
      f << "automatic,";
    if (fl&2)
      f << "ignore[orientation],";
    if (fl&0x20)
      f << "hidden,";
    fl&=0xffdc;
    if (fl)
      f << "fl=" << std::hex << fl << std::dec << ",";
    val=input->readLong(2);
    if (val)
      f << "f1=" << val << ",";
    if (newCPos>0 && newCPos<=32000)
      actCPos=int(newCPos);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  long pos=input->tell();
  if (pos!=endPos) {
    // no rare
    ascFile.addPos(pos);
    ascFile.addNote("SheetDims:end");
  }
  input->setReadInverted(false);
  return true;
}

bool RagTime5Spreadsheet::readCellBlocks(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link, bool isUnion)
{
  MWAWEntry const &entry=zone.m_entry;
  std::string const wh(isUnion ? "SheetUnion" : "SheetRefBlock");
  if (!entry.valid() || (link.m_fieldSize!=22&&link.m_fieldSize!=24) || link.m_fieldSize*link.m_N>entry.length()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellBlocks: the zone seems bad\n"));
    if (entry.valid()) {
      libmwaw::DebugFile &ascFile=zone.ascii();
      libmwaw::DebugStream f;
      f << "Entries(" << wh << ")[" << zone << "]:###";
      zone.m_isParsed=true;
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
    }
    return false;
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(" << wh << ")[" << zone << "]:";
  zone.m_isParsed=true;
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();
  int const numEnd=link.m_fieldSize==22 ? 1 : 2;

  for (int i=0; i<link.m_N; ++i) {
    // checkme: it is possible that the list of valid block is maintained in the SheetRefPos
    long pos=input->tell();
    f.str("");
    if (isUnion)
      f << wh << "-" << i+1 << ":";
    else
      f << wh << "-RB" << i+1 << ":";
    auto val=static_cast<int>(input->readLong(2)); // always 0?
    auto type=static_cast<int>(input->readLong(2));
    if (val==0 && type==0) { // no data,
      f << "_";
      input->seek(pos+link.m_fieldSize, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    if (val) f << "f0=" << val << ",";
    if (type!=1) f << "type=" << type << ",";
    int dim[4], plane[2];
    for (auto &d : dim) d=int(input->readULong(2));
    for (auto &p : plane) p=int(input->readULong(2));
    if (dim[0]==0 && dim[1]==0 && dim[2]==0 && dim[3]==0) {
      f << "_";
      input->seek(pos+link.m_fieldSize, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    if (plane[1]<=plane[0]) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellBlocks: the block planes seem bad\n"));
      f << "###plane,";
    }
    else if (isUnion && (dim[2]>=dim[0] || dim[3]>=dim[1])) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellBlocks: the block seems bad\n"));
      f << "###";
    }
    else if (isUnion && (dim[0]!=dim[2]+1 || dim[1]!=dim[3]+1))
      sheet.setMergedCells(MWAWVec3i(dim[2]-1,dim[3]-1,plane[0]),MWAWVec3i(dim[0]-2,dim[1]-2,plane[1]-1));
    else if (!isUnion) {
      MWAWCellContent::FormulaInstruction cells;
      cells.m_position[0]=MWAWVec2i(dim[2]-1,dim[3]-1);
      cells.m_position[1]=MWAWVec2i(dim[0]-2,dim[1]-2);
      if (dim[2]==32700 && dim[0]==0) { // complete column
        cells.m_position[0][0]=cells.m_position[1][0]=-1;
        cells.m_type=MWAWCellContent::FormulaInstruction::F_CellList;
      }
      else if (dim[3]==32700 && dim[1]==0) { // complete row
        cells.m_position[0][1]=cells.m_position[1][1]=-1;
        cells.m_type=MWAWCellContent::FormulaInstruction::F_CellList;
      }
      else
        cells.m_type=cells.m_position[0]==cells.m_position[1] ? MWAWCellContent::FormulaInstruction::F_Cell :
                     MWAWCellContent::FormulaInstruction::F_CellList;
      cells.m_sheet[0]=sheet.getName(plane[0]);
      cells.m_sheet[1]=sheet.getName(plane[1]-1);
      sheet.m_blockToCellRefMap[i+1]=cells;
    }

    // 1 means first line, 16001 means last line
    f << "cells?=" << MWAWBox2i(MWAWVec2i(dim[2],dim[3]),MWAWVec2i(dim[0],dim[1])) << ",";
    if (plane[1]!=plane[0]+1)
      f << "planes=" << plane[0] << "<->" << plane[1]-1 << ",";
    else if (plane[0]!=1)
      f << "plane=" << plane[0] << ",";
    unsigned long zoneId=input->readULong(4);
    if (zoneId==0x2000000) // normal no zone
      ;
    else if ((zoneId>>24)==2)
      f << "zone[id]=" << (zoneId&0xFFFFFF) << ",";
    else { // find also (zoneId>>24)==42
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellBlocks: the zone id seems bad\n"));
      f << "###zoneId=" << std::hex << zoneId << std::dec << ",";
    }
    for (int j=0; j<numEnd; ++j) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << j+3 << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  long pos=input->tell();
  if (pos!=endPos) {
    // extra data seems rare, but possible...
    f.str("");
    f << wh << ":end";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  input->setReadInverted(false);
  return true;
}

bool RagTime5Spreadsheet::readValuesTree(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone,
    RagTime5ClusterManager::Link const &link, int rootId, MWAWVec3i const &maxPos)
{
  MWAWEntry const &entry=zone.m_entry;
  if (!entry.valid() || link.m_fieldSize!=8 || link.m_fieldSize*link.m_N>entry.length()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: the zone seems bad\n"));
    if (entry.valid()) {
      libmwaw::DebugFile &ascFile=zone.ascii();
      libmwaw::DebugStream f;
      f << "Entries(SheetVTree)[" << zone << "]:###";
      zone.m_isParsed=true;
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
    }
    return false;
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(SheetVTree)[" << zone << "]:";
  zone.m_isParsed=true;
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  /* a binary tree, all leaf have the same level. Each node shares
     format coord-leftChild-type-rightChild where:
     - coord=0..2,
     - type=0(internal node) or type=11(leaf),
     - left(resp. right) child corresponds to 0(resp. 1) value,
     - bits defining each coord are stored in reversed order(to make
       the tree grows easily if needed)
   */
  std::stack<int> idStack;
  std::stack<MWAWVec3i> coordStack;
  std::set<int> idSeenSet;
  idStack.push(rootId);
  coordStack.push(MWAWVec3i(0,0,0));
  while (!idStack.empty()) {
    int id=idStack.top();
    MWAWVec3i coord=coordStack.top();
    idStack.pop();
    coordStack.pop();
    if (idSeenSet.find(id)!=idSeenSet.end() || id<=0 || id>link.m_N) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: id %d is bad\n", id));
      continue;
    }
    idSeenSet.insert(id);
    long pos=entry.begin()+8*(id-1);
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    f.str("");
    f << "SheetVTree-VT" << id << ":";
    if (id==rootId) f << "root,";

    int child[2];
    unsigned long value=input->readULong(4);
    auto actCoord=int(value>>24);
    child[0]=int(value&0xFFFFFF);
    value=input->readULong(4);
    auto type=int(value>>24);
    child[1]=int(value&0xFFFFFF);

    if (actCoord<0 || actCoord>2) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: coord %d seems bad\n", actCoord));
      f << "###coord=" << actCoord << ",";
    }
    else if (type!=0 && type!=0x11) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: unknown type %d\n", type));
      f << "###type=" << type << ",";
    }
    else {
      coord[actCoord]*=2;
      for (int i=0; i<2; ++i) {
        int cId=child[i];
        if (!cId) {
          f << "_,";
          continue;
        }
        if (i==1) ++coord[actCoord];
        if (type==0x11) {
          f << "V" << cId << "[C" << coord << "],";
          if (coord[0]<=0 || coord[0]>maxPos[0] || coord[1]<=0 || coord[1]>maxPos[1] || coord[2]<=0 || coord[2]>maxPos[2]) {
            MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: the final pos seems bad\n"));
            f << "###pos,";
          }
          else
            sheet.setPLCValues(coord-MWAWVec3i(1,1,0),coord-MWAWVec3i(1,1,0), RagTime5SpreadsheetInternal::CellContent::Value, cId);
          continue;
        }
        if (cId>=1 && cId<=link.m_N && idSeenSet.find(cId)==idSeenSet.end()) {
          idStack.push(cId);
          coordStack.push(coord);
        }
        else {
          MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readValuesTree: find bad child\n"));
          f << "###";
        }
        f << "VT" << cId << ",";
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

#ifdef DEBUG_WITH_FILES
  auto it=idSeenSet.begin();
  for (int i=1; i<=link.m_N; ++i) {
    if (it!=idSeenSet.end() && *it==i) {
      ++it;
      continue;
    }
    ascFile.addPos(entry.begin()+(i-1)*8);
    ascFile.addNote("SheetVTree:_");
  }
#endif

  if (link.m_N*8!=entry.length()) { // no frequent, but can happens
    ascFile.addPos(entry.begin()+link.m_N*8);
    ascFile.addNote("SheetVTree:end");
  }
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////
bool RagTime5Spreadsheet::readCellRefPos(RagTime5SpreadsheetInternal::Sheet &sheet, RagTime5Zone &zone, RagTime5ClusterManager::Link const &link)
{
  MWAWEntry const &entry=zone.m_entry;
  if (!entry.valid() || link.m_fieldSize!=10 || link.m_fieldSize*link.m_N>entry.length()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellRefPos: the zone seems bad\n"));
    if (entry.valid()) {
      libmwaw::DebugFile &ascFile=zone.ascii();
      libmwaw::DebugStream f;
      f << "Entries(SheetRefPos)[" << zone << "]:###";
      zone.m_isParsed=true;
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
    }
    return false;
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(SheetRefPos)[" << zone << "]:";
  zone.m_isParsed=true;
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();

  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "SheetRefPos-RP" << i+1 << ":";
    auto used=input->readULong(4);
    auto fl=input->readULong(2); // [0145][08][0-f][0-f]
    auto id2=input->readULong(4);
    if (used) {
      if (used!=1) f << "used=" << used << ",";
      // fl&0x40: 1 coord
      // fl&0x80: 2 coord
      if (fl & 0xff00) f << "fl=" << std::hex << (fl&0xff00) << std::dec << "],";
      auto const &refMap=(fl&0x80)==0 ? sheet.m_valueToCellRefMap : sheet.m_blockToCellRefMap;
      auto it=refMap.find(int(id2));
      if (it!=refMap.end()) {
        auto instr=it->second;
        instr.m_positionRelative[0][0]=(fl&1)==0;
        instr.m_positionRelative[0][1]=(fl&2)==0;
        // fl&4: plane[0]
        instr.m_positionRelative[1][0]=(fl&8)==0;
        instr.m_positionRelative[1][1]=(fl&0x10)==0;
        // fl&0x20: plane[1]
        sheet.m_refToCellRefMap[i+1]=instr;
        f << instr;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellRefPos: can not find a ref\n"));
        f << "##" << ((fl&0x80)==0 ? "V" : "RB") << id2 << ",";
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  long pos=input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readCellRefPos: find extra data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("SheetRefPos:end###");
  }
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
// interface send function
////////////////////////////////////////////////////////////
bool RagTime5Spreadsheet::send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId)
{
  auto it=m_state->m_idSheetMap.find(zoneId);
  if (it==m_state->m_idSheetMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: can not find sheet for zone %d\n", zoneId));
    return false;
  }
  return send(*it->second, listener, pos, partId);
}

bool RagTime5Spreadsheet::send(RagTime5SpreadsheetInternal::Sheet &sheet, MWAWListenerPtr listener, MWAWPosition const &position, int partId)
{
  sheet.m_isSent=true;

  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: can not find the listener\n"));
    return false;
  }

  if (partId>1) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: send partial sheet is not implemented, send all sheets\n"));
      first=false;
    }
  }
  // time to update default value
  MWAWSection section;
  if (sheet.m_defTextPLC.m_textStyleId)
    m_styleManager->updateTextStyles(sheet.m_defTextPLC.m_textStyleId,sheet.m_defaultFont,sheet.m_defaultParagraph, section);

  MWAWSpreadsheetListenerPtr sheetListener=std::dynamic_pointer_cast<MWAWSpreadsheetListener>(listener);
  MWAWSpreadsheetEncoder spreadsheetEncoder;
  bool localListener=!sheetListener;
  if (localListener) {
    MWAWBox2f box=MWAWBox2f(MWAWVec2f(0,0), position.size());
    sheetListener=std::make_shared<MWAWSpreadsheetListener>(*m_parserState, box, &spreadsheetEncoder);
    sheetListener->startDocument();
  }
  for (int plane=1; plane<=sheet.getNumPlanes(); ++plane) {
    if (plane>static_cast<int>(sheet.m_planesList.size()))
      break;
    auto const &data=sheet.m_planesList[size_t(plane-1)];
    if (data.isEmpty()) continue;
    std::vector<int> repeatedWidths;
    std::vector<float> colWidths=sheet.getColumnWidths(repeatedWidths);
    sheetListener->openSheet(colWidths, librevenge::RVNG_POINT, repeatedWidths, sheet.getName(plane).cstr());
    int actRow=-1;
    for (auto rIt : data.m_rowsToDataMap) {
      if (rIt.second.isEmpty()) continue;

      MWAWVec2i rowPos=rIt.first;
      auto const row=rIt.second;
      if (rowPos[0]>actRow+1) {
        // must not happen, so suppose all row have same size
        sheetListener->openSheetRow(sheet.getRowHeight(actRow+1), librevenge::RVNG_POINT, rowPos[0]-actRow-1);
        sheetListener->closeSheetRow();
      }

      while (rowPos[0]<=rowPos[1]) {
        // we must check that all row have the same height, if not, we must send them separatly
        MWAWVec2i blockRow=rowPos;
        auto hIt=sheet.m_rowHeightsMap.lower_bound(MWAWVec2i(-1,blockRow[0]));
        if (hIt!=sheet.m_rowHeightsMap.end() && hIt->first[0]>=blockRow[0] && hIt->first[1]<blockRow[1])
          blockRow[1]=hIt->first[1];
        // ok let send the current block
        sheetListener->openSheetRow(sheet.getRowHeight(blockRow[0]), librevenge::RVNG_POINT, blockRow[1]-blockRow[0]+1);
        actRow=blockRow[1];
        for (auto cIt : row.m_columnsToDataMap) {
          RagTime5SpreadsheetInternal::CellContent const &cContent=cIt.second;
          if (cContent.isMergedCell()) continue;

          bool isUnion=cContent.m_id[RagTime5SpreadsheetInternal::CellContent::Union]>=0;
          send(sheet, plane, cContent, isUnion ? 1 : cIt.first[1]-cIt.first[0]+1, sheetListener);
        }
        sheetListener->closeSheetRow();
        rowPos[0]=blockRow[1]+1;
      }
    }
    sheetListener->closeSheet();
  }
  if (localListener) {
    sheetListener->endDocument();

    MWAWEmbeddedObject object;
    if (spreadsheetEncoder.getBinaryResult(object))
      listener->insertPicture(position, object);
  }
  return true;
}

bool RagTime5Spreadsheet::send
(RagTime5SpreadsheetInternal::Sheet &sheet, int plane, RagTime5SpreadsheetInternal::CellContent const &cContent, int numRepeated,
 MWAWSpreadsheetListenerPtr &listener)
{
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: can not find the listener\n"));
    return false;
  }

  MWAWCell cell;
  MWAWCellContent content;
  cell.setPosition(cContent.m_position);
  cell.setVAlignment(MWAWCell::VALIGN_CENTER);
  // the span
  if (cContent.m_id[RagTime5SpreadsheetInternal::CellContent::Union]>=0)
    cell.setNumSpannedCells(sheet.getSpan(cContent.m_position, cContent.m_plane));
  // graphic style: background color
  int graphicId=cContent.m_id[RagTime5SpreadsheetInternal::CellContent::GraphicStyle];
  if (graphicId>=0)
    graphicId=sheet.getGraphicStyleId(graphicId);
  else
    graphicId=sheet.m_defGraphicPLC.getGraphicStyleId();
  MWAWColor color;
  if (graphicId>0 && m_styleManager->getCellBackgroundColor(graphicId, color))
    cell.setBackgroundColor(color);
  // the border
  for (int i=0; i<4; ++i) {
    int const static wh[]= {RagTime5SpreadsheetInternal::CellContent::BorderPrevVStyle,
                            RagTime5SpreadsheetInternal::CellContent::BorderNextVStyle,
                            RagTime5SpreadsheetInternal::CellContent::BorderPrevHStyle,
                            RagTime5SpreadsheetInternal::CellContent::BorderNextHStyle
                           };
    int bId=cContent.m_id[wh[i]];
    if (bId<0) bId=sheet.m_defBordersPLC[i<2 ? 0 : 1].getBorderGraphicStyleId((i%2)==1);
    if (bId<=0) continue;
    MWAWBorder border;
    if (!m_styleManager->getCellBorder(bId, border))
      continue;
    int const final[]= {libmwaw::LeftBit, libmwaw::RightBit, libmwaw::TopBit, libmwaw::BottomBit};
    cell.setBorders(final[i], border);
  }
  // the value
  RagTime5SpreadsheetInternal::CellValue value;
  if (cContent.m_id[RagTime5SpreadsheetInternal::CellContent::Value]>0 &&
      cContent.m_id[RagTime5SpreadsheetInternal::CellContent::Value]<=static_cast<int>(sheet.m_valuesList.size()))
    value=sheet.m_valuesList[size_t(cContent.m_id[RagTime5SpreadsheetInternal::CellContent::Value]-1)];
  value.update(cell, content);
  if (value.m_formulaId) {
    auto fIt=sheet.m_idToFormula.find(value.m_formulaId);
    if (fIt==sheet.m_idToFormula.end() || fIt->second.empty()) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: can not retrieve some formula\n"));
        first=false;
      }
    }
    else {
      content.m_contentType=MWAWCellContent::C_FORMULA;
      content.m_formula=fIt->second;
      // try to remove uneeded sheet name
      auto sheetName=sheet.getName(plane);
      for (auto &instr : content.m_formula) {
        if (instr.m_type==instr.F_Cell && instr.m_sheet[0]==sheetName)
          instr.m_sheet[0].clear();
        else if (instr.m_type==instr.F_CellList && instr.m_sheet[0]==sheetName && instr.m_sheet[1]==sheetName) {
          instr.m_sheet[0].clear();
          instr.m_sheet[1].clear();
        }
      }
    }
  }
  // the number/text/format plc
  RagTime5SpreadsheetInternal::TextPLC plc;
  MWAWFont font(sheet.m_defaultFont);
  MWAWParagraph para(sheet.m_defaultParagraph);
  MWAWSection section;
  if (cContent.m_id[RagTime5SpreadsheetInternal::CellContent::TextStyle]>=0)
    sheet.getTextPLC(cContent.m_id[RagTime5SpreadsheetInternal::CellContent::TextStyle], plc);
  if (plc.m_formatId)
    m_styleManager->updateCellFormat(plc.m_formatId, cell);
  if (plc.m_textStyleId)
    m_styleManager->updateTextStyles(plc.m_textStyleId,font,para,section);
  auto align=plc.getHorizontalAlignment();
  if (align!=MWAWCell::HALIGN_DEFAULT)
    cell.setHAlignment(align);
  auto rot=(plc.m_flags>>16)&3;
  if (rot) // fixme, if rot=1|3, we must also swap H/V alignement
    cell.setRotation(rot*90);
  cell.setFont(font);
  listener->openSheetCell(cell, content, numRepeated);
  if (value.m_type==7) { // small text zone
    listener->setFont(font);
    listener->setParagraph(para);
    listener->insertUnicodeString(value.m_text);
  }
  else if (value.m_type==8 || value.m_type==9) // big text zone
    m_document.send(sheet.m_textboxZoneId, listener, MWAWPosition(), 0, static_cast<int>(value.m_id&0xFFFFFF));
  else if (value.m_type==0xa) { // a zone
    if (value.m_id>=static_cast<unsigned long>(sheet.m_childList.size()) ||
        sheet.m_childList[size_t(value.m_id)].m_dataId==0) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::send: the child list seems bad\n"));
    }
    else {
      auto cellName=sheet.getName(cContent.m_plane);
      cellName.append('.');
      cellName.append(MWAWCell::getBasicCellName(sheet.getBottomRightCell(cContent.m_position,cContent.m_plane)).c_str());
      MWAWPosition position(MWAWVec2f(0,0), sheet.getCellDimensions(cContent.m_position, cContent.m_plane), librevenge::RVNG_POINT);
      position.setAnchorToCell(cellName);
      auto const &link=sheet.m_childList[size_t(value.m_id)];

      auto type=m_document.getClusterType(link.m_dataId);
      if (type==RagTime5ClusterManager::Cluster::C_PictureZone)
        m_document.send(link.m_dataId, listener, position, int(link.getSubZoneId(0)&0xFFFFFF));
      else if (type==RagTime5ClusterManager::Cluster::C_ButtonZone)
        m_document.sendButtonZoneAsText(listener, link.m_dataId);
      else {
        // let try to create a graphic object to represent the content
        MWAWBox2f box(MWAWVec2f(0,0), position.size());
        MWAWGraphicEncoder graphicEncoder;
        MWAWGraphicListenerPtr graphicListener(new MWAWGraphicListener(*m_parserState, box, &graphicEncoder));
        graphicListener->startDocument();
        MWAWPosition graphicPos;
        graphicPos.m_anchorTo = MWAWPosition::Page;
        m_document.send(link.m_dataId, graphicListener, graphicPos, int(link.getSubZoneId(0)&0xFFFFFF));
        graphicListener->endDocument();

        MWAWEmbeddedObject picture;
        if (graphicEncoder.getBinaryResult(picture))
          listener->insertPicture(position, picture);
      }
    }
  }
  listener->closeSheetCell();
  return true;
}

void RagTime5Spreadsheet::flushExtra(bool onlyCheck)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::flushExtra: can not find a listener\n"));
    return;
  }
  MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(100,100), librevenge::RVNG_POINT);
  position.m_anchorTo=MWAWPosition::Char;
  for (auto it : m_state->m_idSheetMap) {
    if (!it.second || it.second->m_isSent) continue;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::flushExtra: find some unsent spreadsheets %d, ...\n", it.first));
      first=false;
    }
    if (!onlyCheck)
      send(*it.second, listener, position);
  }
}

////////////////////////////////////////////////////////////
// cluster parser
////////////////////////////////////////////////////////////

namespace RagTime5SpreadsheetInternal
{

//! low level: the spreadsheet cluster data
struct ClusterSpreadsheet final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterSpreadsheet()
    : RagTime5ClusterManager::Cluster(C_SpreadsheetZone)
    , m_dimensionLink()
    , m_valuesLink()
    , m_valuesTreeLink()
    , m_valuesTreeRoot(0)
    , m_valuesMaxPos(0,0,0)
    , m_graphPLCLink()
    , m_graphPLCNumRowByPlanesMap()
    , m_textPLCLink()
    , m_textPLCNumRowByPlanesMap()
  {
  }
  //! destructor
  ~ClusterSpreadsheet() final;
  //! the dimension link
  RagTime5ClusterManager::Link m_dimensionLink;
  //! the list of values link
  RagTime5ClusterManager::Link m_valuesLink;
  //! the value tree link
  RagTime5ClusterManager::Link m_valuesTreeLink;
  //! the value tree root
  int m_valuesTreeRoot;
  //! the maximum values position
  MWAWVec3i m_valuesMaxPos;
  //! the graph PLC link
  RagTime5ClusterManager::Link m_graphPLCLink;
  //! the list of row graph plc by planes
  std::map<MWAWVec2i,int> m_graphPLCNumRowByPlanesMap;
  //! the text PLC link
  RagTime5ClusterManager::Link m_textPLCLink;
  //! the list of row text plc by planes
  std::map<MWAWVec2i,int> m_textPLCNumRowByPlanesMap;
  //! the border PLC link(vertical then horizontal)
  RagTime5ClusterManager::Link m_borderPLCLink[2];
  //! the list of row border plc by planes
  std::map<MWAWVec2i,int> m_borderPLCNumRowByPlanesMap[2];
  //! the reference block/cell union/reference pos link
  RagTime5ClusterManager::Link m_blockLinks[3];
};

ClusterSpreadsheet::~ClusterSpreadsheet()
{
}

//
//! low level: parser of main spreadsheet cluster
//
struct SpreadsheetCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_borderRoot, F_borderH, F_borderV, F_cellsTree, F_cellsTreeValue, F_cellsUnion, F_dims, F_graphPLC, F_name, F_nameRoot=F_name+3, F_sheetList, F_nextId=F_sheetList+3, F_parentList, F_childList, F_refBlock, F_refPos, F_textPLC, F_unknA, F_unknARoot };
  //! constructor
  SpreadsheetCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustSheet")
    , m_cluster(new ClusterSpreadsheet)
    , m_sheet(new Sheet)
    , m_fieldName("")
    , m_defaultPLCValues()
    , m_PLCNumRowByPlanesMap()

    , m_expectedIdToType()
    , m_idStack()
  {
  }
  //! destructor
  ~SpreadsheetCParser() final;
  //! return the spreadsheet cluster
  std::shared_ptr<ClusterSpreadsheet> getSpreadsheetCluster()
  {
    return m_cluster;
  }
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the spreadsheet
  std::shared_ptr<Sheet> getSpreadsheet()
  {
    return m_sheet;
  }
  //! set a data id type
  void setExpectedType(int id, int type)
  {
    m_expectedIdToType[id]=type;
    m_idStack.push(id);
  }
  /** returns to new zone to parse. */
  int getNewZoneToParse() final
  {
    if (m_idStack.empty())
      return -1;
    int id=m_idStack.top();
    m_idStack.pop();
    return id;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (expected) {
    case F_cellsTree:
      if (m_cluster->m_valuesTreeLink.empty())
        m_cluster->m_valuesTreeLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the values tree link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_cellsTreeValue:
      if (m_cluster->m_valuesLink.empty())
        m_cluster->m_valuesLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the values link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_dims:
      if (m_cluster->m_dimensionLink.empty())
        m_cluster->m_dimensionLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the dimension link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_name:
      if (m_cluster->m_nameLink.empty())
        m_cluster->m_nameLink=RagTime5ClusterManager::NameLink(m_link);
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the name link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_name+1:
    case F_name+2:
    case F_sheetList:
    case F_sheetList+1:
    case F_sheetList+2:
      m_cluster->m_linksList.push_back(m_link);
      break;
    case F_childList:
      if (m_cluster->m_childLink.empty())
        m_cluster->m_childLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the picture cluster link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_parentList:// parent link (graphic or pipeline)
      if (m_cluster->m_parentLink.empty())
        m_cluster->m_parentLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the picture cluster link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_cellsUnion:
    case F_refBlock:
      if (expected==F_refBlock && m_cluster->m_blockLinks[0].empty())
        m_cluster->m_blockLinks[0]=m_link;
      else if (expected==F_cellsUnion && m_cluster->m_blockLinks[1].empty())
        m_cluster->m_blockLinks[1]=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the two block links are already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_refPos:
      if (m_cluster->m_blockLinks[2].empty())
        m_cluster->m_blockLinks[2]=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops the last block links is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_borderH:
    case F_borderV:
    case F_graphPLC:
    case F_textPLC:
      if (m_link.m_fieldSize==6 && expected==F_graphPLC && m_cluster->m_graphPLCLink.empty()) {
        m_cluster->m_graphPLCLink=m_link;
        m_cluster->m_graphPLCNumRowByPlanesMap=m_PLCNumRowByPlanesMap;
        m_sheet->m_defGraphicPLC=GraphicPLC(m_defaultPLCValues);
      }
      else if (m_link.m_fieldSize==10 && expected==F_textPLC && m_cluster->m_textPLCLink.empty()) {
        m_cluster->m_textPLCLink=m_link;
        m_cluster->m_textPLCNumRowByPlanesMap=m_PLCNumRowByPlanesMap;
        m_sheet->m_defTextPLC=TextPLC(m_defaultPLCValues);
      }
      else if (m_link.m_fieldSize==14 && expected==F_borderH && m_cluster->m_borderPLCLink[1].empty()) { // horizontal
        m_cluster->m_borderPLCLink[1]=m_link;
        m_cluster->m_borderPLCNumRowByPlanesMap[1]=m_PLCNumRowByPlanesMap;
        m_sheet->m_defBordersPLC[1]=BorderPLC(m_defaultPLCValues);
      }
      else if (m_link.m_fieldSize==14 && expected==F_borderV && m_cluster->m_borderPLCLink[0].empty()) { // vertical
        m_cluster->m_borderPLCLink[0]=m_link;
        m_cluster->m_borderPLCNumRowByPlanesMap[0]=m_PLCNumRowByPlanesMap;
        m_sheet->m_defBordersPLC[0]=BorderPLC(m_defaultPLCValues);
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops find unexpected PLC link with size %d\n",
                        m_link.m_fieldSize));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::endZone: oops find unexpected link for field %d\n",
                      m_dataId));
      m_cluster->m_linksList.push_back(m_link);
      break;
    }
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_fieldName="";
    if (N==-5)
      return parseHeaderZone(input,fSz,N,flag,f);
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseZone: expected N value\n"));
      f << "###N=" << N << ",";
      return true;
    }
    return parseDataZone(input, fSz, N, flag, f);
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (expected) {
    case F_borderH:
    case F_borderV:
    case F_graphPLC:
    case F_textPLC:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        return true;
      }
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x1671845) {
        f << "nData[byPlane]=["; // find only 0|1000X,[3e7f|3e80]0001: probably a selection
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            int actPlanes=1;
            for (auto val : child.m_longList) {
              auto nPlanes=static_cast<int>(val>>16);
              int nData=(val&0xFFFF);
              m_PLCNumRowByPlanesMap[MWAWVec2i(actPlanes,actPlanes+nPlanes-1)]=nData;
              actPlanes+=nPlanes;
              f << nData;
              if (nPlanes!=1)
                f << "[" << nPlanes << "]";
              f << ",";
            }
            if (actPlanes!=16001) {
              MWAW_DEBUG_MSG(("RagTime5GraphInternal::SpreadsheetCParser::parseField: the number of planes seems bad\n"));
              f << "###";
              m_PLCNumRowByPlanesMap.clear();
            }
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::SpreadsheetCParser::parseField: find unexpected child[fSz=91]\n"));
          f << "##[" << child << "],";
        }
        f << "],";
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x1671817) {
        f << "default=[";
        for (auto val : field.m_longList) {
          m_defaultPLCValues.push_back(int(val));
          f << val << ",";
        }
        if (expected==F_textPLC && m_defaultPLCValues.size()==4) { // FIXME this assume hilo
          m_defaultPLCValues[2]=(m_defaultPLCValues[2]<<16)+m_defaultPLCValues[3];
          m_defaultPLCValues.resize(3);
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_name:
    case F_name+1:
    case F_name+2:
    case F_sheetList:
    case F_sheetList+1:
    case F_sheetList+2:
    case F_parentList:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        return true;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_cellsTreeValue:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        return true;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_cellsUnion:
    case F_refBlock:
    case F_refPos:
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_unknARoot:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) {
        f << "ids=[";
        for (auto val : field.m_longList) {
          if (val==0) {
            f << "_,";
            continue;
          }
          setExpectedType(int(val-1), F_unknA);
          f << "F" << val-1 << ",";
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
protected:
  //! parse a data block
  bool parseDataZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected!=-1)
      f << "[F" << m_dataId << "]";
    if (flag!=0x10)
      f << "fl=" << std::hex << flag << std::dec << ",";

    long pos=input->tell();
    int val;
    long linkValues[4];
    std::string mess;
    m_link.m_N=N;
    switch (expected) {
    case F_cellsTreeValue:
    case F_cellsUnion:
    case F_dims:
    case F_name:
    case F_name+1:
    case F_name+2:
    case F_sheetList:
    case F_sheetList+1:
    case F_sheetList+2:
    case F_parentList:
    case F_childList:
    case F_refBlock:
    case F_refPos: {
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=-1, expectedFieldSize=0;
      if (expected==F_name && fSz==32) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] fileType0 seems odd\n", expected));
          f << "###fileType0=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        }
        expectedFileType1=0x200;
        m_link.m_type=RagTime5ClusterManager::Link::L_UnicodeList;
        m_link.m_name="unicode";
      }
      else if ((expected==F_name+1 || expected==F_name+2 || expected==F_sheetList+1) && m_link.m_fileType[0]==0x35800)
        m_link.m_name=expected==F_name+1 ? "unicodeList1" : expected==F_name+2 ? "unicodeList2" : "sheetList1";
      else if (expected==F_sheetList && m_link.m_fileType[0]==0x3e800)
        m_link.m_name="sheetList0";
      else if (expected==F_sheetList+2 && m_link.m_fileType[0]==0x45080) {
        expectedFieldSize=2;
        m_link.m_name="sheetListInt";
      }
      else if (expected==F_dims && fSz==34) {
        expectedFileType1=0x40;
        expectedFieldSize=24;
        m_link.m_name="dims";
        val=static_cast<int>(input->readULong(4));
        if (val==32000) // always 0|32000
          f << "num[data32000],";
        else if (val)
          f << "num[data]=" << val << ",";
      }
      else if (expected==F_cellsTreeValue && fSz==34) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] fileType0 seems odd\n", expected));
          f << "###fileType0=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        }
        expectedFileType1=0x10;
        m_link.m_name="cells[tree,values]";
        val=static_cast<int>(input->readULong(2));
        if (val) // some dataA ?
          f << "f0=" << val << ",";
      }
      else if ((expected==F_cellsUnion || expected==F_refBlock) && fSz==34) {
        if (m_link.m_fieldSize!=0x16 && m_link.m_fieldSize!=0x18) { // 5: 0x16, 6:0x18
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] fieldSize seems odd\n", expected));
          f << "###fieldSize=" << m_link.m_fieldSize << ",";
        }
        expectedFileType1=0x50;
        m_link.m_name=expected==F_cellsUnion ? "cells[union]" : "ref[block]";
        val=static_cast<int>(input->readULong(4)); // always 1
        if (val!=1)
          f << "g0=" << val << ",";
      }
      else if (expected==F_parentList && fSz==36) { // parent link: graphic or pipeline link
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] fileType0 seems odd\n", expected));
          f << "###fileType0=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        }
        expectedFileType1=0x10;
        m_link.m_name="parentList";
        f << "interval=";
        for (int i=0; i<2; ++i)
          f << input->readULong(2) << (i==0 ? "->" : ",");
      }
      else if (expected==F_childList && fSz==30) {
        expectedFieldSize=12;
        expectedFileType1=0xd0;
        m_link.m_name="sheetChildLst";
        m_link.m_type=RagTime5ClusterManager::Link::L_ClusterLink;
      }
      else if (expected==F_refPos && fSz==34) {
        expectedFileType1=0x50;
        expectedFieldSize=10;
        m_link.m_name="ref[pos]";
        val=static_cast<int>(input->readULong(4)); // always 1
        if (val!=1)
          f << "g0=" << val << ",";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        f << m_link.m_name << ",";
        m_fieldName=m_link.m_name;
      }
      if (expectedFileType1>=0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_borderRoot:
      if (fSz<16) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected size\n"));
        f << "###fSz";
        return true;
      }
      m_fieldName="border[root]";
      f << m_fieldName << ",";
      for (int i=0; i<2; ++i) { // either 0,0 or g1=g0+1
        val=static_cast<int>(input->readLong(4));
        if (!val) continue;
        setExpectedType(val-1, i==0 ? F_borderV : F_borderH);
        f << "border" << (i==0 ? "V": "H")  << "=F" << val-1 << ",";
      }
      val=static_cast<int>(input->readLong(2)); // 4 or 8
      if (val!=4) f << "g2=" << val << ",";
      return true;
    case F_borderH:
    case F_borderV:
    case F_graphPLC: // fSz=71
    case F_textPLC: {
      if (fSz<69) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected size\n"));
        f << "###fSz";
        return true;
      }
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: can not read link for fSz69...\n"));
        input->seek(pos+26, librevenge::RVNG_SEEK_SET);
        f << "###link,";
      }
      else  {
        if ((m_link.m_fileType[1]&0xFFD7)!=0x8000) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType1 seems odd[fSz69...]\n"));
          f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
        }
        f << m_link << ",";
      }
      val=static_cast<int>(input->readLong(4)); // always 1
      if (val!=1)
        f << "g0=" << val << ",";
      m_link.m_fieldSize=static_cast<int>(input->readLong(2));
      val=static_cast<int>(input->readULong(2));
      m_defaultPLCValues.clear();
      m_PLCNumRowByPlanesMap.clear();
      if (val==0x3e81 && (expected==F_borderH || expected==F_borderV) && m_link.m_fieldSize==14) {
        m_fieldName= "border[PLC]";
        m_fieldName+=expected==F_borderV ? "[vert]" : "[hori]";
        m_link.m_name=m_fieldName;
      }
      else if (val==0x3e80 && ((expected==F_graphPLC && m_link.m_fieldSize==6) || (expected==F_textPLC && m_link.m_fieldSize==10)))
        m_link.m_name=m_fieldName=expected==F_graphPLC ? "graph[PLC]" : "text[PLC]";
      else {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType2 seems odd[fSz69...]\n"));
        f << "###fileType2=" << std::hex << val << std::dec << ",";
      }
      f << m_fieldName << ",";
      val=static_cast<int>(input->readLong(2)); // always 1
      if (val!=1)
        f << "g2=" << val << ",";
      val=static_cast<int>(input->readLong(4)); // 1-2
      if (val!=2)
        f << "g3=" << val << ",";
      auto type=input->readULong(4);
      if (type!=0x34800) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType3 seems odd[fSz69...]\n"));
        f << "###fileType3=" << RagTime5Spreadsheet::printType(type) << ",";
      }
      for (int i=0; i<9; ++i) { // h6=32
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "h" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(1)); // always 1
      if (val!=1)
        f << "h9=" << val << ",";
      if (fSz==71) { // graph[PLC]
        val=static_cast<int>(input->readLong(2)); // always 0
        if (val)
          f << "h10=" << val << ",";
      }
      return true;
    }
    case F_cellsTree: {
      if (fSz<58) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected size\n"));
        f << "###fSz";
        return true;
      }
      m_fieldName="cells[tree]";
      f << m_fieldName << ",";
      f << "root=VT" << N << ",";
      if (m_cluster->m_valuesTreeRoot) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the value tree root is already set\n"));
        f << "###";
      }
      else
        m_cluster->m_valuesTreeRoot=N;
      val=static_cast<int>(input->readLong(2)); // always 1
      if (val!=1) f << "g0=" << val << ",";
      m_link.m_N=static_cast<int>(input->readLong(4));
      long actPos=input->tell();
      m_fieldName=m_link.m_name="VTree";
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: can not read link for fSz58\n"));
        input->seek(actPos+30, librevenge::RVNG_SEEK_SET);
        f << "###link,";
      }
      else {
        f << m_link << "," << mess;
        m_link.m_fileType[0]=0;
        if ((m_link.m_fileType[1]&0xFFD7)!=0x40) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType1 seems odd[fSz58]\n"));
          f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
        }
      }
      val=static_cast<int>(input->readLong(4));
      if (val) {
        setExpectedType(val-1, F_cellsTreeValue);
        f << "cells[tree,val]=F" << val-1 << ",";
      }
      f << "num=[";
      for (int i=0; i<4; ++i) {
        val=static_cast<int>(input->readULong(2));
        if (!val)
          f << "_,";
        else
          f << val << ",";
      }
      f << "],";

      int dim[3];
      for (auto &d : dim) d=static_cast<int>(input->readULong(2));
      MWAWVec3i maxCell(dim[0],dim[1],dim[2]);
      if (m_cluster->m_valuesTreeLink.empty())
        m_cluster->m_valuesMaxPos=MWAWVec3i(dim[0],dim[1],dim[2]);
      f << "cell[max]=" << maxCell << ",";
      val=static_cast<int>(input->readLong(4)); // always 1?
      if (val!=1)
        f << "g2=" << val << ",";
      return true;
    }
    case F_unknA: {
      m_fieldName="unknA";
      f << m_fieldName << ",";
      if (fSz<68) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseField: find unexpected size\n"));
        f << "###fSz";
        return true;
      }
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      for (int i=0; i<2; ++i) { // f2=1, f3=0|a big number
        val=static_cast<int>(input->readLong(4));
        if (val)
          f << "f" << i+2 << "=" << val << ",";
      }
      auto type=input->readULong(4);
      if (type!=0x1646042) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType0 seems odd[fSz68]\n"));
        f << "###fileType0=" << RagTime5Spreadsheet::printType(type) << ",";
      }
      for (int i=0; i<4; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i+4 << "=" << val << ",";
      }
      f << "num0=[";
      for (int i=0; i<3; ++i) { // small number
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << "],";
      val=static_cast<int>(input->readULong(4)); // always 1
      if (val!=1) f << "f8=" << val << ",";
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i+9 << "=" << val << ",";
      }
      f << "num1=[";
      for (int i=0; i<10; ++i) { // find X,_,_,X,X,_,_,X,X,_ where X are some small numbers
        val=static_cast<int>(input->readLong(1));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << "],";
      f << "num2=[";
      for (int i=0; i<7; ++i) { // first always 0, other some ints
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << "],";
      return true;
    }
    default:
      break;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: find unexpected field[%d]\n", m_dataId));
      f << "###";
    }
    switch (fSz) {
    case 29: { // unknARoot
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Spreadsheet::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the field type seems bad\n"));
        return true;
      }
      long expectedFileType1=0;
      if (m_link.m_fileType[0]==0x3c052) {
        m_link.m_fileType[0]=0;
        if (linkValues[0]!=0x1454877) {
          MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: find unexpected linkValue[0]\n"));
          f << "#lValues0,";
        }
        expectedFileType1=0x50;
        m_fieldName="unknA[root]";
        m_expectedIdToType[m_dataId]=F_unknARoot;
        if (linkValues[2]) {
          setExpectedType(int(linkValues[2]-1), F_nextId);
          f << "next[id]=F" << linkValues[2]-1 << ",";
        }
      }
      else {
        f << "###fType=" << m_link << ",";
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the field fSz28 type seems bad\n"));
        return true;
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      f << m_link << "," << mess;
      val=static_cast<int>(input->readLong(1));
      if (val!=1) // always 1
        f << "g0=" << val << ",";
      break;
    }
    case 36: {
      // probably related to named cell
      m_expectedIdToType[m_dataId]=F_nameRoot;
      auto type=input->readULong(4);
      auto type1=input->readULong(4);
      if ((type==0x35800 && type1==0x1454857) || (type==0 && type1==0x17db042)) {
        m_fieldName=type1==0x1454857? "name[root]" : "sheetList[root]";
        f << m_fieldName << ",";
        for (int i=0; i<2; ++i) { // g1=1669817
          val=static_cast<int>(input->readLong(4));
          if (val) f << "g" << i << "=" << std::hex << val << std::dec << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << "fileType1=" << std::hex << val << std::dec << ",";
        // increasing sequence
        f << "ids=[";
        for (int i=0; i<3; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (!val) {
            f << "_,";
            continue;
          }
          setExpectedType(val-1, type1==0x1454857 ? F_name+i : F_sheetList+i);
          f << "F" << val-1 << ",";
        }
        f << "],";
      }
      else {
        f << "###fType=" << RagTime5Spreadsheet::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: the field type seems bad\n"));
      }
      return true;
    }
    default:
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseDataZone: find unexpected file size\n"));
      f << "###fSz=" << fSz << ",";
      break;
    }
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header, fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    if (N!=-5 || m_dataId!=0 || fSz != 134) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (m_type>0 && val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    val=int(input->readULong(4));
    if (val) {
      setExpectedType(val-1, F_parentList);
      f << "parent[list]=F" << val-1 << ",";
    }
    val=int(input->readULong(4));
    if (val) {
      setExpectedType(val-1, F_nextId);
      f << "next[id]=F" << val-1 << ",";
    }
    for (int i=0; i<5; ++i) {
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) { // fl0=0|1, fl1=0|1
      val=static_cast<int>(input->readULong(1));
      if (val)
        f << "fl" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readULong(2)); // [02][08]0[12c]
    if (val&2)
      f << "cell[border,draw,hori],";
    if (val&4)
      f << "nogrid[hori],";
    if (val&8)
      f << "nogrid[vert],";
    if (val&0x40)
      f << "grid[print,hori],";
    if (val&0x20)
      f << "recalculate[demand],";
    if (val&0x80)
      f << "grid[print,vert],";
    if (val&0x400)
      f << "fixed[widths,heights]";
    if (val&0x8000)
      f << "space[between,para,sum],";
    val &= 0x7b11;
    if (val) f << "fl2=" << std::hex << val << std::dec << ",";

    // first read dims, tree cells, ...
    int ids[6];
    for (int i=0; i<6; ++i) {
      val=static_cast<int>(input->readULong(4));
      ids[i]=val;
      if (!val) continue;
      char const *what[]= { "dims", "tree[cells]", "text[PLC]", "graph[PLC]", "root[unkn]", "refBlock"};
      f << what[i] << "=F" << val-1 << ",";
    }

    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 2, listIds)) {
      f << "##field,";
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseHeaderZone: can not read the field definitions\n"));
      return true;
    }
    else if (listIds[0] || listIds[1]) { // formuladef and formulapos
      m_sheet->m_formulaLink.m_type=RagTime5ClusterManager::Link::L_ClusterLink;
      m_sheet->m_formulaLink.m_ids=listIds;
      f << "formula[" << m_sheet->m_formulaLink << "],";
    }
    val=static_cast<int>(input->readULong(4));
    if (val) {
      setExpectedType(val-1, F_cellsUnion);
      f << "union[cells]=F" << val-1 << ",";
    }
    val=static_cast<int>(input->readULong(2)); // 10|110
    if (val) f << "fl3=" << std::hex << val << std::dec << ",";
    for (int i=0; i<2; ++i) { // 0
      val=static_cast<int>(input->readULong(2)); // 0
      if (val) f << "h" << i+2 << "=" << std::hex << val << std::dec << ",";
    }
    auto type=input->readULong(4);
    if (type!=0x34800)
      f << "#type1=" << RagTime5Spreadsheet::printType(type) << ",";
    for (int i=0; i<9; ++i) { // h10=32
      val=static_cast<int>(input->readLong(2));
      if (val) f << "h" << i+4 << "=" << val << ",";
    }
    val=static_cast<int>(input->readULong(2)); // always 1
    if (val!=1)
      f << "num[planes]=" << val << ",";
    val=static_cast<int>(input->readULong(4)); // always 1
    if (val!=1)
      f << "l1=" << val << ",";
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      f << "##text,";
      MWAW_DEBUG_MSG(("RagTime5SpreadsheetInternal::SpreadsheetCParser::parseHeaderZone: can not read the text zone\n"));
      return true;
    }
    else if (listIds[0]) { // text zone
      m_sheet->m_textboxZoneId=listIds[0];
      m_cluster->m_clusterIdsList.push_back(listIds[0]);
      f << "clusterId[text]=" << getClusterDebugName(listIds[0]) << ",";
    }
    for (int i=0; i<3; ++i) { // l1=0|7-9, l3=0-8-9
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==0) {
        setExpectedType(val-1, F_childList);
        f << "childList=F" << val-1 << ",";
      }
      else if (i==2) {
        setExpectedType(val-1, F_refPos);
        f << "refPos=F" << val-1 << ",";
      }
      else
        f << "l" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<6; ++i) { // l4=3|5, l5=0|1
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      if (i==1)
        f << "num[title,vert]=" << val << ",";
      else if (i==2)
        f << "num[title,hori]=" << val << ",";
      else
        f << "l" << i+5 << "=" << val << ",";
    }

    // time to mark the main ids type
    for (int i=5; i>=0; --i) {
      int id=ids[i];
      if (!id) continue;
      int const wh[]= { F_dims, F_cellsTree, F_textPLC, F_graphPLC, F_borderRoot, F_refBlock };
      setExpectedType(id-1, wh[i]);
    }
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterSpreadsheet> m_cluster;
  //! the sheet
  std::shared_ptr<Sheet> m_sheet;
  //! the actual field name
  std::string m_fieldName;
  //! the default plc values
  std::vector<int> m_defaultPLCValues;
  //! the list of row plc by planes
  std::map<MWAWVec2i,int> m_PLCNumRowByPlanesMap;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
private:
  //! copy constructor (not implemented)
  SpreadsheetCParser(SpreadsheetCParser const &orig) = delete;
  //! copy operator (not implemented)
  SpreadsheetCParser &operator=(SpreadsheetCParser const &orig) = delete;
};

SpreadsheetCParser::~SpreadsheetCParser()
{
}

}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Spreadsheet::readSpreadsheetCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSpreadsheetCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5SpreadsheetInternal::SpreadsheetCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getSpreadsheetCluster() || !parser.getSpreadsheet()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSpreadsheetCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  auto cluster=parser.getSpreadsheetCluster();
  m_document.checkClusterList(cluster->m_clusterIdsList);

  if (!cluster->m_dataLink.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSpreadsheetCluster: oops do not how to parse the main data\n"));
  }
  auto sheet=parser.getSpreadsheet();
  sheet->m_name=m_state->getNewSheetName(cluster->m_name);

  // values
  if (!cluster->m_valuesLink.empty()) {
    RagTime5SpreadsheetInternal::ValuesParser valuesParser(*sheet);
    m_document.readListZone(cluster->m_valuesLink, valuesParser);
  }
  /* dimensions, cell blocks and finally the value tree

     note: it is important to read the merged cell before the value list and other PLC*/
  for (int w=0; w<5; ++w) {
    RagTime5ClusterManager::Link link=w==0 ? cluster->m_dimensionLink :
                                      w==1 ? cluster->m_valuesTreeLink : cluster->m_blockLinks[w-2];
    if (link.m_ids.empty())
      continue;
    int cId=link.m_ids[0];
    std::shared_ptr<RagTime5Zone> dataZone=m_document.getDataZone(cId);
    if (!dataZone || !dataZone->m_entry.valid() ||
        dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
      if (dataZone && dataZone->getKindLastPart()=="ItemData" && link.m_N==0)
        continue;
      MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSpreadsheetCluster: the %dth data zone %d seems bad\n", w, cId));
      continue;
    }
    if (w==0)
      readSheetDimensions(*sheet, *dataZone, link);
    else if (w==1)
      readValuesTree(*sheet, *dataZone, link, cluster->m_valuesTreeRoot, cluster->m_valuesMaxPos);
    else if (w==2 || w==3) // 2: formula block, 3: union block
      readCellBlocks(*sheet, *dataZone, link, w==3);
    else // 4: ref positions to value pos
      readCellRefPos(*sheet, *dataZone, link);
  }

  // PLC
  // note: formula links can only be read after all spreadsheet are read
  for (int i=0; i<4; ++i) {
    auto link = i==0 ? cluster->m_graphPLCLink : i==1 ? cluster->m_textPLCLink : cluster->m_borderPLCLink[i-2];
    if (link.empty())
      continue;
    auto const &numRowByPlanes=i==0 ? cluster->m_graphPLCNumRowByPlanesMap : i==1 ? cluster->m_textPLCNumRowByPlanesMap :
                               cluster->m_borderPLCNumRowByPlanesMap[i-2];
    RagTime5SpreadsheetInternal::CellPLCParser plcParser(*sheet, i, link.m_fieldSize, numRowByPlanes);
    m_document.readListZone(link, plcParser);
  }

  // pictures list
  m_document.readChildList(cluster->m_childLink, sheet->m_childList);
  // parent zones:  graphic or pipeline
  if (!cluster->m_parentLink.empty()) {
    RagTime5SpreadsheetInternal::ClustListParser linkParser(*clusterManager, 24, "SheetParentLst");
    m_document.readListZone(cluster->m_parentLink, linkParser);
    m_document.checkClusterList(linkParser.m_clusterList);
  }

  if (!cluster->m_nameLink.empty()) {
    std::map<int, librevenge::RVNGString> idToStringMap;
    m_document.readUnicodeStringList(cluster->m_nameLink, idToStringMap);
  }

  for (auto const &lnk : cluster->m_linksList) {
    if (lnk.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(lnk);
      continue;
    }
    std::stringstream s;
    s << "Sheet_data" << lnk.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(lnk.m_name.empty() ? s.str() : lnk.m_name);
    m_document.readFixedSizeZone(lnk, defaultParser);
  }

  if (m_state->m_idSheetMap.find(zone.m_ids[0])!=m_state->m_idSheetMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Spreadsheet::readSpreadsheetCluster: the sheet %d already exists\n",
                    zone.m_ids[0]));
  }
  else
    m_state->m_idSheetMap[zone.m_ids[0]]=sheet;
  return cluster;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
