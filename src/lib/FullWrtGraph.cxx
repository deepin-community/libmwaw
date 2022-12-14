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
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "FullWrtParser.hxx"
#include "FullWrtStruct.hxx"

#include "FullWrtGraph.hxx"

/** Internal: the structures of a FullWrtGraph */
namespace FullWrtGraphInternal
{
////////////////////////////////////////
//! Internal: the sidebar of a FullWrtGraph
struct SideBar final : public FullWrtStruct::ZoneHeader {
  //! constructor
  explicit SideBar(FullWrtStruct::ZoneHeader const &header): FullWrtStruct::ZoneHeader(header), m_box(), m_page(0), m_borderId(0), m_parsed(false)
  {
  }
  //! destructor
  ~SideBar() final;
  //! the position (in point)
  MWAWBox2f m_box;
  //! the page
  int m_page;
  //! the border id
  int m_borderId;
  //! a flag to know if the sidebar is send to the listener
  mutable bool m_parsed;
};

SideBar::~SideBar()
{
}

////////////////////////////////////////
//! Internal: the state of a FullWrtGraph
struct State {
  //! constructor
  State()
    : m_version(-1)
    , m_sidebarList()
    , m_graphicMap()
    , m_borderList()
    , m_numPages(-1) { }

  //! the file version
  mutable int m_version;
  //! the sidebar list
  std::vector<std::shared_ptr<SideBar> > m_sidebarList;
  //! zoneId -> graphic entry
  std::multimap<int, FullWrtStruct::EntryPtr > m_graphicMap;
  //! a list of border
  std::vector<FullWrtStruct::Border> m_borderList;
  int m_numPages /* the number of pages */;
};

////////////////////////////////////////
//! Internal: the subdocument of a FullWrtGraph
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor
  SubDocument(FullWrtGraph &pars, int id, MWAWColor fontColor)
    : MWAWSubDocument(pars.m_mainParser, MWAWInputStreamPtr(), MWAWEntry())
    , m_graphParser(&pars)
    , m_id(id)
    , m_fontColor(fontColor) {}

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the graph parser */
  FullWrtGraph *m_graphParser;
  //! the zone file id
  int m_id;
  //! the default font color
  MWAWColor m_fontColor;

private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("FullWrtGraphInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (!m_graphParser) {
    MWAW_DEBUG_MSG(("FullWrtGraphInternal::SubDocument::parse: no graph parser\n"));
    return;
  }
  m_graphParser->send(m_id, m_fontColor);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_graphParser != sDoc->m_graphParser) return true;
  if (m_id != sDoc->m_id) return true;
  if (m_fontColor != sDoc->m_fontColor) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
FullWrtGraph::FullWrtGraph(FullWrtParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new FullWrtGraphInternal::State)
  , m_mainParser(&parser)
{
}

FullWrtGraph::~FullWrtGraph()
{
}

int FullWrtGraph::version() const
{
  if (m_state->m_version < 0)
    m_state->m_version = m_parserState->m_version;
  return m_state->m_version;
}

int FullWrtGraph::numPages() const
{
  if (m_state->m_numPages > 0)
    return m_state->m_numPages;
  int nPage=0;
  for (auto sidebar : m_state->m_sidebarList) {
    if (!sidebar)
      continue;
    if (sidebar->m_page>nPage)
      nPage=sidebar->m_page;
  }
  return (m_state->m_numPages=nPage);
}

bool FullWrtGraph::getBorder(int bId, FullWrtStruct::Border &border) const
{
  if (bId < 0 || bId>= int(m_state->m_borderList.size())) {
    MWAW_DEBUG_MSG(("FullWrtGraph::getBorder: can not find border %d\n", bId));
    border=FullWrtStruct::Border();
    return false;
  }
  border=m_state->m_borderList[size_t(bId)];
  return true;
}

bool FullWrtGraph::send(int fileId, MWAWColor const &fontColor)
{
  return m_mainParser->send(fileId, fontColor);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// low level
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// border
bool FullWrtGraph::readBorderDocInfo(FullWrtStruct::EntryPtr zone)
{
  MWAWInputStreamPtr input = zone->m_input;
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;
  long pos = input->tell();
  if (input->readULong(4)!=0x626f7264 || input->readULong(1)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  long blckSz = input->readLong(4);
  long endData = pos+9+blckSz;
  auto num = static_cast<int>(input->readULong(2));
  int const fSz = 26;
  f << "Entries(Border):N=" << num << ",";
  if (blckSz < 2 || blckSz != 2 + num*fSz || endData > zone->end()) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readBorderDocInfo: problem reading the data block or the number of data\n"));
    f << "###";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    if (endData <= zone->end()) {
      input->seek(endData, librevenge::RVNG_SEEK_SET);
      return true;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  m_state->m_borderList.push_back(FullWrtStruct::Border());
  for (int i = 0; i < num; i++) {
    pos = input->tell();
    FullWrtStruct::Border mod;
    f.str("");
    f << "Border-B" << i << ":";
    if (!mod.read(zone, fSz))
      f << "###";
    else
      f << mod;
    m_state->m_borderList.push_back(mod);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+fSz, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

////////////////////////////////////////////////////////////
// side bar
std::shared_ptr<FullWrtStruct::ZoneHeader> FullWrtGraph::readSideBar(FullWrtStruct::EntryPtr zone, FullWrtStruct::ZoneHeader const &doc)
{
  std::shared_ptr<FullWrtGraphInternal::SideBar> sidebar;
  if (doc.m_type != 0x13 && doc.m_type != 0x14) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBar: find unexpected type\n"));
    return sidebar;
  }
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  sidebar.reset(new FullWrtGraphInternal::SideBar(doc));
  if (!sidebar->read(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    sidebar.reset();
    return sidebar;
  }

  int val;
  if (input->tell()+12 > zone->end()) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    sidebar.reset();
    return sidebar;
  }

  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;
  f << "Entries(SideBar):" << *sidebar;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i=0; i<3; ++i) {
    pos = input->tell();
    bool ok=false;
    switch (i) {
    case 0:
      ok=readSideBarPosition(zone, *sidebar);
      break;
    case 1:
      ok=readSideBarFormat(zone, *sidebar);
      break;
    case 2:
      ok=readSideBarUnknown(zone, *sidebar);
      break;
    default:
      break;
    }
    if (ok) continue;
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBar: pb reading the zone %d\n", i));
    f.str("");
    static char const *wh[]= {"position","format","unknown"};
    f << "SideBar[" << wh[i] << ":###";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return sidebar;
  }

  // checkme: can this exist for a sidebar ?
  val = int(input->readLong(1));
  if (val==1) {
    pos = input->tell();
    auto sz = long(input->readULong(4));
    if (sz && input->tell()+sz <= zone->end()) {
      f.str("");
      f << "SideBar[end]:";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      input->seek(sz, librevenge::RVNG_SEEK_CUR);
    }
    else {
      MWAW_DEBUG_MSG(("FullWrtGraph::readSideBar: find bad end data\n"));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
    }
  }
  else if (val) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBar: find bad end data(II)\n"));
  }
  m_state->m_sidebarList.push_back(sidebar);
  return sidebar;
}

bool FullWrtGraph::readSideBarPosition(FullWrtStruct::EntryPtr zone, FullWrtGraphInternal::SideBar &frame)
{
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz < 0 || pos+sz+4 > zone->end())
    return false;
  f << "SideBar[pos]:";

  if (sz < 28) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBarPosition: the size seems bad\n"));
    f << "###";
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return true;
  }

  int dim[4];
  for (auto &d : dim) d=int(input->readLong(2));
  frame.m_box=MWAWBox2f(MWAWVec2f(float(dim[1]),float(dim[0])),MWAWVec2f(float(dim[3]),float(dim[2])));
  f << "pos=" << frame.m_box << ",";
  auto val=int(input->readLong(2));
  if (val) f << "w[wrap]=" << val << "pt,";
  f << "ptr?=[" << std::hex;
  for (int i = 0; i < 2; i++) // two big number
    f << input->readULong(4) << ",";
  f << std::dec << "],";
  val = int(input->readLong(2)); // seems related to floating point position
  if (val) f << "unkn=" << std::hex << val << std::dec << ",";// 0|441|442|f91|16ac
  val = int(input->readLong(2)); // always 0?
  if (val) f << "f0=" << val << ",";
  frame.m_page = int(input->readLong(2));
  if (frame.m_page) f << "page=" << frame.m_page << ",";
  val = int(input->readLong(2)); // number of point in the left part
  if (val) f << "N[left]?=" << val << ",";
  auto N=static_cast<int>(input->readLong(2));
  if (N*4+28 > sz) {
    f << "#N=" << N << ",";
    N=0;
  }
  else
    f << "N=" << N << ",";

  /* probably first the left margin: (x_i,n): meaning to add n times
     a point at x, then the same thing for the right margins
     -16000/16000=no point (left/right)
  */
  f << "mask=[";
  for (int i = 0; i < N; i++) {
    auto x = int(input->readLong(2));
    auto n = int(input->readLong(2));
    f << x << ":" << n << ",";
  }
  f << "],";
  if (input->tell() != pos+4+sz) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return true;
}

bool FullWrtGraph::readSideBarFormat(FullWrtStruct::EntryPtr zone, FullWrtGraphInternal::SideBar &frame)
{
  int const vers=version();
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz < 0 || pos+sz+4 > zone->end())
    return false;
  f << "SideBar[format]:";
  if ((vers==1&&sz!=0x3a)||(vers==2&&sz!=0x38)) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBarFormat: the size seems bad\n"));
    f << "###";
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return true;
  }
  f << "PTR=" << std::hex << input->readLong(4) << std::dec << ",";
  auto N=static_cast<int>(input->readLong(1));
  int val;
  if (N) {
    f << "N=" << N << ",";
    val=static_cast<int>(input->readLong(1));
    if (val) f << "#f0=" << val << ",";
    /* now probably N*[unknData],
       find for N=1 005f000000b201d2001600f40f94020004100001009700000000000000000000
     */
  }
  input->seek(pos+42, librevenge::RVNG_SEEK_SET);
  float dim[2];
  for (auto &d : dim) d=float(input->readLong(4))/65536.f;
  f << "dim?=" << dim[1] << "x" << dim[0] << ",";
  val=static_cast<int>(input->readULong(2)); // another dim with a flag?
  if (val&0x8000) f << "f1[high],";
  if (val&0x7FFF) f << "f1=" << (val&0x7FFF) << ",";
  float w=float(input->readLong(4))/65536.f;
  f << "w[wrap]=" << w << "pt,";
  frame.m_borderId=static_cast<int>(input->readLong(2));
  if (frame.m_borderId)
    f << "B" << frame.m_borderId-1 << ",";
  if (vers==1) {
    val=static_cast<int>(input->readLong(2)); // 0|1|4|b|20|..f0
    if (val) f << "f2=" << val << ",";
  }
  val=static_cast<int>(input->readLong(2)); // always 0
  if (val)
    f << "f3=" << val << ",";

  if (input->tell() != pos+4+sz) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return true;
}

bool FullWrtGraph::readSideBarUnknown(FullWrtStruct::EntryPtr zone, FullWrtGraphInternal::SideBar &/*frame*/)
{
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;

  auto sz = long(input->readULong(4));
  if (sz < 0 || pos+sz+4 > zone->end())
    return false;
  f << "SideBar[unknown]:";
  if (sz!=0x30) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readSideBarUnknown: the size seems bad\n"));
    f << "###";
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return true;
  }
  auto val = static_cast<int>(input->readLong(2));
  if (val!=-1)
    f << "f0=" << val << ",";
  val = static_cast<int>(input->readLong(2));
  if (val!=1)
    f << "f1=" << val << ",";
  val = static_cast<int>(input->readULong(2)); // maybe a color?
  if (val)
    f << "f2=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) { // f3=1|2, f4=small numer 0..ff
    val = static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i+3 << "=" << val << ",";
  }
  for (int i=0; i<19; ++i) { // g0,g1,g17,g18: in form xyxy, other 0
    val = static_cast<int>(input->readULong(2));
    if (val)
      f << "g" << i << "=" << std::hex << val << std::dec << ",";
  }
  if (input->tell() != pos+4+sz) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  return true;
}

bool FullWrtGraph::sendSideBar(FullWrtGraphInternal::SideBar const &frame)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("FullWrtGraph::sendSideBar can not find the listener\n"));
    return true;
  }

  frame.m_parsed=true;
  MWAWPosition pos(frame.m_box[0]+72.f*m_mainParser->getPageLeftTop(),
                   frame.m_box.size(),librevenge::RVNG_POINT);
  pos.setPage(frame.m_page>0 ? frame.m_page : 1);
  pos.setRelativePosition(MWAWPosition::Page);
  pos.m_wrapping=(frame.m_wrapping==3) ?
                 MWAWPosition::WBackground : MWAWPosition::WDynamic;
  FullWrtStruct::Border border;
  MWAWGraphicStyle style;
  if (frame.m_borderId && getBorder(frame.m_borderId, border))
    border.addTo(style);
  MWAWSubDocumentPtr doc(new FullWrtGraphInternal::SubDocument(*this,frame.m_fileId,border.m_frontColor));
  listener->insertTextBox(pos, doc, style);
  return true;
}

////////////////////////////////////////////////////////////
// graphic: data +
std::shared_ptr<FullWrtStruct::ZoneHeader> FullWrtGraph::readGraphicData(FullWrtStruct::EntryPtr zone, FullWrtStruct::ZoneHeader &doc)
{
  std::shared_ptr<FullWrtStruct::ZoneHeader> graphData;
  if (doc.m_type != 0x15) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readGraphicData: find unexpected type\n"));
    return graphData;
  }
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  if (!doc.read(zone)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return graphData;
  }

  int const vers = version();
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;

  if (input->tell()+(vers==2?14:2) > zone->end()) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return graphData;
  }

  graphData.reset(new FullWrtStruct::ZoneHeader(doc));
  f.str("");
  f << "Entries(GraphData):" << doc;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (vers == 2) {
    pos = input->tell();
    f.str("");
    f << "GraphData[1]:";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    f << "box=" << dim[1] << "x" << dim[0] << "<->" << dim[3] << "x" << dim[2] << ",";
    for (int i = 0; i < 2; i++) { // always 0 ?
      auto val = int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << "c";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  f.str("");
  auto nextData = int(input->readULong(1));
  pos = input->tell();
  if (nextData==1) {
    f << "GraphData[2]:";
    auto sz = long(input->readULong(4));
    if (sz < 0 || pos+4+sz > zone->end()) {
      f << "#sz=" << sz << ",";
      input->seek(pos, librevenge::RVNG_SEEK_SET);
    }
    else if (sz) {   // a serie of doc id ( normally 1e )
      f << "docId[type1e?]=[";
      for (long i = 0; i < sz/2; i++) {
        auto id = int(input->readLong(2));
        std::string type=m_mainParser->getDocumentTypeName(id);
        if (type.empty())
          f << "#" << id << ",";
        else
          f << id << "[" << type << "],";
      }
      f << "],";
      input->seek(pos+4+sz, librevenge::RVNG_SEEK_SET);
    }
  }
  else if (nextData) f << "GraphData[2]:#" << nextData;

  input->seek(1, librevenge::RVNG_SEEK_CUR);
  if (f.str().length()) {
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  return graphData;
}

bool FullWrtGraph::readGraphic(FullWrtStruct::EntryPtr zone)
{
  int vers = version();

  MWAWInputStreamPtr input = zone->m_input;
  libmwaw::DebugFile &asciiFile = zone->getAsciiFile();
  libmwaw::DebugStream f;

  long pos = zone->begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  auto sz = long(input->readULong(4));
  int expectedSz = vers==1 ? 0x5c : 0x54;
  if (sz != expectedSz || pos+sz > zone->end()) return false;
  input->seek(sz, librevenge::RVNG_SEEK_CUR);
  f << "Entries(Graphic)";
  f << "|" << *zone << ":";
  if (zone->m_fileType >= 0)
    f << "type=" << std::hex << zone->m_fileType << std::dec << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  pos = input->tell();
  sz = long(input->readULong(4));
  if (!sz || pos+4+sz > zone->end()) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readGraphic: can not read graphic size\n"));
    return false;
  }
  f.str("");
  f << "Graphic:sz=" << std::hex << sz << std::dec << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  asciiFile.skipZone(pos+4, pos+4+sz-1);
  input->seek(sz, librevenge::RVNG_SEEK_CUR);

  m_state->m_graphicMap.insert
  (std::multimap<int, FullWrtStruct::EntryPtr >::value_type(zone->id(), zone));

  pos = input->tell();
  if (pos == zone->end())
    return true;

  sz = long(input->readULong(4));
  if (sz)
    input->seek(sz, librevenge::RVNG_SEEK_CUR);
  if (pos+4+sz!=zone->end()) {
    MWAW_DEBUG_MSG(("FullWrtGraph::readGraphic: end graphic seems odds\n"));
  }
  asciiFile.addPos(pos);
  asciiFile.addNote("Graphic-A");

  asciiFile.addPos(input->tell());
  asciiFile.addNote("_");

  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool FullWrtGraph::sendGraphic(int fId)
{
  auto it = m_state->m_graphicMap.find(fId);
  if (it == m_state->m_graphicMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("FullWrtGraph::sendGraphic: can not find graphic %d\n", fId));
    return false;
  }
  auto zone = it->second;
  MWAWInputStreamPtr input = zone->m_input;
  long pos = input->tell();
  bool ok=sendGraphic(zone);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return ok;
}

bool FullWrtGraph::sendGraphic(FullWrtStruct::EntryPtr zone)
{
  MWAWTextListenerPtr listener=m_parserState->m_textListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("FullWrtGraph::sendGraphic can not find the listener\n"));
    return true;
  }
  zone->setParsed(true);

  MWAWInputStreamPtr input = zone->m_input;

  long pos = zone->begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  auto sz = static_cast<int>(input->readULong(4));
  input->seek(sz, librevenge::RVNG_SEEK_CUR);

  // header
  pos = input->tell();
  sz = static_cast<int>(input->readULong(4));

#ifdef DEBUG_WITH_FILES
  if (1) {
    librevenge::RVNGBinaryData file;
    input->seek(pos+4, librevenge::RVNG_SEEK_SET);
    input->readDataBlock(sz, file);
    static int volatile pictName = 0;
    libmwaw::DebugStream f;
    f << "DATA-" << ++pictName;
    libmwaw::Debug::dumpFile(file, f.str().c_str());
  }
#endif

  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  MWAWBox2f box;
  auto res = MWAWPictData::check(input, sz, box);
  if (res == MWAWPict::MWAW_R_BAD) {
    MWAW_DEBUG_MSG(("FullWrtGraph::sendGraphic: can not find the picture\n"));
    return false;
  }

  MWAWVec2f actualSize, naturalSize;
  if (box.size().x() > 0 && box.size().y()  > 0) {
    actualSize = naturalSize = box.size();
  }
  else if (actualSize.x() <= 0 || actualSize.y() <= 0) {
    MWAW_DEBUG_MSG(("FullWrtGraph::sendGraphic: can not find the picture size\n"));
    actualSize = naturalSize = MWAWVec2f(100,100);
  }
  MWAWPosition pictPos=MWAWPosition(MWAWVec2f(0,0),actualSize, librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Char);
  pictPos.setNaturalSize(naturalSize);

  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, sz));
  if (pict) {
    MWAWEmbeddedObject picture;
    if (pict->getBinary(picture)) {
      listener->insertPicture(pictPos, picture);
      return true;
    }
  }

  return true;
}

bool FullWrtGraph::sendPageGraphics()
{
  for (auto const &frame : m_state->m_sidebarList) {
    if (!frame)
      continue;
    if (!frame->m_parsed)
      sendSideBar(*frame);
  }
  return true;
}

void FullWrtGraph::flushExtra()
{
  for (auto it : m_state->m_graphicMap) {
    FullWrtStruct::EntryPtr &zone = it.second;
    if (!zone || zone->isParsed())
      continue;
    sendGraphic(zone);
  }
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
