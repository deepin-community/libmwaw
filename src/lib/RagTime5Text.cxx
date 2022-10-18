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

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWListener.hxx"
#include "MWAWGraphicEncoder.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSection.hxx"
#include "MWAWSpreadsheetEncoder.hxx"
#include "MWAWSpreadsheetListener.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5ClusterManager.hxx"
#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5StyleManager.hxx"

#include "RagTime5Text.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Text */
namespace RagTime5TextInternal
{
//! a PLC of a RagTime5Text
struct PLC {
  //! constructor
  PLC()
    : m_position(-1)
    , m_fileType(0)
    , m_value(-1)
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, PLC const &plc)
  {
    if (plc.m_fileType==0) {
      o << "free[next]";
      if (plc.m_position>0)
        o << "=PLC" << plc.m_position;
      o << ",";
      return o;
    }
    if (plc.m_position>=0) o << "pos=" << plc.m_position << ",";
    switch (plc.m_fileType) {
    case 0:
      break;
    case 0x1001:
      o << "para,";
      break;
    case 0x1801: // soft?
      o << "line[beg],";
      break;
    case 0x3001:
      o << "index[end],";
      break;
    // 0x4001: related to footnote?
    case 0x5001:
      o << "char,";
      break;
    case 0x7001:
      o << "index[beg],";
      break;
    default:
      if (plc.m_fileType&0xfe) o << "#";
      o << "type=" << std::hex << plc.m_fileType << std::dec << ",";
    }
    if (plc.m_value!=-1) o << "f0=" << plc.m_value << ",";
    return o;
  }
  //! the position in the text
  int m_position;
  //! the file type
  int m_fileType;
  //! an unknown value
  int m_value;
};

//! a small struct use to define a block of a RagTime5Text
struct Block {
  //! constructor
  Block()
    : m_id(0)
    , m_subId(0)
    , m_dimension()
    , m_extra("")
  {
    for (auto &plc : m_plc) plc=0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Block const &block)
  {
    o << "id=" << block.m_id << ",";
    if (block.m_subId) o << "id[sub]=" << block.m_subId << ",";
    o << "PLC" << block.m_plc[0] << "<->" << block.m_plc[1] << ",";
    o << block.m_dimension << ",";
    o << block.m_extra;
    return o;
  }
  //! the block id
  int m_id;
  //! the block sub id
  int m_subId;
  //! the block dimension
  MWAWBox2f m_dimension;
  //! the list of zone plc (first-end)
  int m_plc[2];
  //! extra data
  std::string m_extra;
};

//! a small struct used to store link plc data: footnote, index, ...
struct LinkPLC {
  //! constructor
  LinkPLC()
    : m_what(0)
    , m_type(0)
    , m_positions(-1,-1)
    , m_id(0)
    , m_dimensions()
    , m_footnotePositions()
  {
  }
  //! the plc type 0:attachment, 1:item(list item), 2:unknown, 3:index, 4:formula(page number, ...), 5:footnote
  int m_what;
  //! the file type
  int m_type;
  //! the position in the text
  MWAWVec2i m_positions;
  //! an identifier
  int m_id;
  //! the attachment box
  MWAWVec2f m_dimensions;
  //! the footnote data
  MWAWVec2i m_footnotePositions;
};

//! low level: the text cluster of a RagTime5Text
struct ClusterText final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterText()
    : RagTime5ClusterManager::Cluster(C_TextZone)
    , m_contentLink()
    , m_plcDefLink()
    , m_plcDefFreeBegin(0)
    , m_plcDefNumFree(-1)
    , m_plcToStyleLink()
    , m_blockCellToPlcLink()
    , m_separatorLink()
    , m_footnoteLink()
    , m_indexLink()
    , m_textIntListLink()
    , m_unknownLinks1()

    , m_blockList()
    , m_blockCellList()
    , m_childList()
    , m_PLCList()
    , m_separators()
    , m_posToStyleIdMap()
    , m_linkPLCList()
    , m_posToLinkIdMap()
  {
  }
  //! destructor
  ~ClusterText() final;
  //! the main content
  RagTime5ClusterManager::Link m_contentLink;
  //! the plc definition link
  RagTime5ClusterManager::Link m_plcDefLink;
  //! the plc first free block in the plc definition list
  int m_plcDefFreeBegin;
  //! the number of free block in the plc definition list
  int m_plcDefNumFree;
  //! the plc to text style link
  RagTime5ClusterManager::Link m_plcToStyleLink;
  //! the blockCell to plc link
  RagTime5ClusterManager::Link m_blockCellToPlcLink;
  //! the word/separator link
  RagTime5ClusterManager::Link m_separatorLink;
  //! the footnote link
  RagTime5ClusterManager::Link m_footnoteLink;
  //! the index link
  RagTime5ClusterManager::Link m_indexLink;
  //! the list of link zone
  RagTime5ClusterManager::Link m_linkDefs[5];
  //! list of a int link with size 2(only v6.6)
  RagTime5ClusterManager::Link m_textIntListLink;
  //! list of unkndata1 links
  std::vector<RagTime5ClusterManager::Link> m_unknownLinks1;
  //! list of unknown link: the three unkndata+2-3 links and the header link3 link
  RagTime5ClusterManager::Link m_unknownLink[3];

  // final data

  //! list of block (defined in header)
  std::vector<std::vector<Block> > m_blockList;
  //! list of block (defined in blockCell list)
  std::vector<Block> m_blockCellList;
  //! list of child
  std::vector<RagTime5StructManager::ZoneLink> m_childList;
  //! the PLC list
  std::vector<PLC> m_PLCList;
  //! the separators
  std::vector<int> m_separators;
  //! position to plc map
  std::multimap<int, int> m_posToStyleIdMap;
  //! the link plc list
  std::vector<LinkPLC> m_linkPLCList;
  //! position to link data map
  std::multimap<int, size_t> m_posToLinkIdMap;
};

ClusterText::~ClusterText()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Text
struct State {
  //! constructor
  State()
    : m_numPages(0)
    , m_idTextMap()
    , m_uniqueIndexId(0)
  {
  }
  //! the number of pages
  int m_numPages;
  //! map data id to text zone
  std::map<int, std::shared_ptr<ClusterText> > m_idTextMap;
  //! an int used to create unique index id
  int m_uniqueIndexId;
};

//! Internal: the subdocument of a RagTime5Text
class SubDocument final : public MWAWSubDocument
{
public:
  // constructor
  SubDocument(RagTime5Text &parser, MWAWInputStreamPtr const &input,
              RagTime5TextInternal::ClusterText &cluster, RagTime5Zone &dataZone, size_t firstChar, size_t lastChar)
    : MWAWSubDocument(&parser.m_document.getMainParser(), input, MWAWEntry())
    , m_ragtimeParser(parser)
    , m_cluster(cluster)
    , m_dataZone(dataZone)
    , m_firstChar(firstChar)
    , m_lastChar(lastChar)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the main parser
  RagTime5Text &m_ragtimeParser;
  //! the cluster
  ClusterText &m_cluster;
  //! the data zone
  RagTime5Zone &m_dataZone;
  //! the first char
  size_t m_firstChar;
  //! the last char
  size_t m_lastChar;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("RagTime5TextInternal::SubDocument::parse: no listener\n"));
    return;
  }

  long pos = m_input->tell();
  m_ragtimeParser.send(m_cluster, m_dataZone, listener, m_firstChar, m_lastChar, true, -1);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (&m_ragtimeParser != &sDoc->m_ragtimeParser) return true;
  if (&m_cluster != &sDoc->m_cluster) return true;
  if (&m_dataZone != &sDoc->m_dataZone) return true;
  if (m_firstChar != sDoc->m_firstChar) return true;
  if (m_lastChar != sDoc->m_lastChar) return true;
  return false;
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Text::RagTime5Text(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_styleManager(m_document.getStyleManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5TextInternal::State)
{
}

RagTime5Text::~RagTime5Text()
{ }

int RagTime5Text::version() const
{
  return m_parserState->m_version;
}

int RagTime5Text::numPages() const
{
  // TODO IMPLEMENT ME
  MWAW_DEBUG_MSG(("RagTime5Text::numPages: is not implemented\n"));
  return 0;
}

bool RagTime5Text::send(int zoneId, MWAWListenerPtr listener, int partId, int cellId, double totalWidth)
{
  auto it=m_state->m_idTextMap.find(zoneId);
  if (it==m_state->m_idTextMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find zone %d\n", zoneId));
    return false;
  }
  return send(*it->second, listener, partId, cellId, totalWidth);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// text separator position
////////////////////////////////////////////////////////////
bool RagTime5Text::readTextSeparators(RagTime5Zone &zone, std::vector<int> &separators)
{
  if (!zone.m_entry.valid() || zone.getKindLastPart(zone.m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Text::readTextSeparators: can not find the text position zone\n"));
    return false;
  }

  zone.m_isParsed=true;
  MWAWEntry entry=zone.m_entry;
  MWAWInputStreamPtr input=zone.getInput();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(TextSep)[" << zone << "]:";

  input->seek(zone.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  separators.resize(size_t(2*entry.length()));

  int lastSeen=0, numSeen=0;
  for (long i=0; i<entry.length(); ++i) {
    auto c=static_cast<int>(input->readULong(1));
    for (int j=0; j<2; ++j) {
      int v=(j==0 ? (c>>4) : c)&0xf;
      if (v!=lastSeen) {
        if (numSeen==1) f << lastSeen << ",";
        else if (numSeen) f << lastSeen << "x" << numSeen << ",";
        numSeen=0;
        lastSeen=v;
      }
      ++numSeen;
      separators[size_t(2*i+j)]=v;
    }
  }
  if (numSeen==1) f << lastSeen << ",";
  else if (numSeen) f << lastSeen << "x" << numSeen << ",";

  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// link/list definition
////////////////////////////////////////////////////////////
bool RagTime5Text::readLinkZones(RagTime5TextInternal::ClusterText &cluster, RagTime5ClusterManager::Link const &link, int what)
{
  if (link.m_ids.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: can not find the first zone id\n"));
    return false;
  }
  if (what!=1 && link.m_ids.size()>=3 && link.m_ids[2]) {
    MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: find unexpected link2\n"));
  }
  else if (link.m_ids.size()>=3 && link.m_ids[2]) {
    std::vector<long> decal;
    if (link.m_ids[1])
      m_document.readPositions(link.m_ids[1], decal);
    if (decal.empty())
      decal=link.m_longList;
    int const dataId=link.m_ids[2];
    auto dataZone=m_document.getDataZone(dataId);
    if (!dataZone || !dataZone->m_entry.valid() ||
        dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
      if (decal.size()==1) {
        // a graphic zone with 0 zone is ok...
        dataZone->m_isParsed=true;
      }
      MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: the data zone %d seems bad\n", dataId));
    }
    else {
      MWAWEntry entry=dataZone->m_entry;
      dataZone->m_isParsed=true;

      libmwaw::DebugFile &ascFile=dataZone->ascii();
      libmwaw::DebugStream f;
      f << "Entries(" << link.m_name << "Def)[" << *dataZone << "]:";
      ascFile.addPos(entry.end());
      ascFile.addNote("_");

      if (decal.size() <= 1) {
        MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: can not find position for the data zone %d\n", dataId));
        f << "###";
        ascFile.addPos(entry.begin());
        ascFile.addNote(f.str().c_str());
      }
      else {
        auto N=int(decal.size());
        MWAWInputStreamPtr input=dataZone->getInput();
        input->setReadInverted(!cluster.m_hiLoEndian); // checkme maybe zone

        ascFile.addPos(entry.begin());
        ascFile.addNote(f.str().c_str());

        for (int i=0; i<N-1; ++i) {
          long pos=decal[size_t(i)], nextPos=decal[size_t(i+1)];
          if (pos==nextPos) continue;
          if (pos<0 || pos>entry.length()) {
            MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: can not read the data zone %d-%d seems bad\n", dataId, i));
            continue;
          }
          f.str("");
          f << link.m_name << "Def-" << i+1 << ":";
          librevenge::RVNGString string;
          input->seek(pos+entry.begin(), librevenge::RVNG_SEEK_SET);
          if (nextPos>entry.length() || !m_structManager->readUnicodeString(input, entry.begin()+nextPos, string)) {
            MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: can not read a string\n"));
            f << "###";
          }
          else if (!string.empty() && string.cstr()[0]=='\0')
            f << "\"" << string.cstr()+1 << "\",";
          else
            f << "\"" << string.cstr() << "\",";
          ascFile.addPos(entry.begin()+pos);
          ascFile.addNote(f.str().c_str());
        }
      }
    }
  }
  // ok no list
  if (!link.m_ids[0])
    return true;
  auto dataZone=m_document.getDataZone(link.m_ids[0]);
  if (!dataZone || dataZone->getKindLastPart()!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: can not find the first zone %d\n", link.m_ids[0]));
    return false;
  }

  // ok no list
  if (!dataZone->m_entry.valid())
    return true;

  MWAWInputStreamPtr input=dataZone->getInput();
  bool const hiLo=dataZone->m_hiLoEndian;
  input->setReadInverted(!hiLo);
  input->seek(dataZone->m_entry.begin(), librevenge::RVNG_SEEK_SET);
  dataZone->m_isParsed=true;

  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  ascFile.addPos(dataZone->m_entry.end());
  ascFile.addNote("_");
  int const expectedSize[]= {32, 14/* or 24 in 6.5*/, 16, 12, 16, 24};
  // CHANGEME: find where the version is stored and use it to decide if the fieldSize is ok or not
  if (what<0 || what>=6 || (link.m_fieldSize!=expectedSize[what] && (what!=1 || link.m_fieldSize!=24))) {
    MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: find unexpected size for zone %d\n", link.m_ids[0]));
    f << "###";
    ascFile.addPos(dataZone->m_entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }
  if (link.m_fieldSize<12 || dataZone->m_entry.length()/link.m_fieldSize<link.m_N || link.m_N<=0) {
    MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: the position zone %d seems bad\n", dataZone->m_ids[0]));
    f << "Entries(" << link.m_name << ")[" << *dataZone << "]:" << link << "###,";
    ascFile.addPos(dataZone->m_entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }
  size_t numPLC=cluster.m_PLCList.size();
  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    if (i==0)
      f << "Entries(" << link.m_name << "Pos)[" << *dataZone << "]:";
    else
      f << link.m_name << "Pos-" << i << ":";
    size_t ids[2];
    for (auto &id : ids) id=size_t(input->readULong(4));
    if (ids[0] && ids[1]) {
      bool ok=true;
      RagTime5TextInternal::LinkPLC linkPLC;
      linkPLC.m_what=what;
      for (int j=0; j<2; ++j) {
        if (ids[j]>numPLC) {
          MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: a plc position in zone %d seems bad\n", dataZone->m_ids[0]));
          f << "###PLC" << ids[j];
          ok=false;
          continue;
        }
        linkPLC.m_positions[j]=cluster.m_PLCList[ids[j]-1].m_position;
        f << linkPLC.m_positions[j];
        if (j==0)
          f << "<->";
        else
          f << ",";
      }
      if (ok && linkPLC.m_positions[0]>linkPLC.m_positions[1]) {
        MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: the plc orders in zone %d seems bad\n", dataZone->m_ids[0]));
        f << "###";
        ok=false;
      }
      int val=int(input->readLong(2)); // always 0?
      if (val) f << "f0=" << val << ",";
      linkPLC.m_type=int(input->readLong(2));
      f << "type=" << linkPLC.m_type << ","; // 8: graph/footnote/index, 10: ref1 and ref4, 14: ref3
      if (link.m_fieldSize==16) {
        linkPLC.m_id=int(input->readLong(4));
        if (what==4)
          f << "FD" << linkPLC.m_id << ",";
        else
          f << "id=" << linkPLC.m_id << ",";
      }
      else if (what==3) // index
        linkPLC.m_id=++m_state->m_uniqueIndexId;
      else if (what==5) { // footnote
        for (auto &id : ids) id=size_t(input->readULong(4));
        for (int j=0; j<2; ++j) {
          if (ids[j]>numPLC) {
            MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: a plc position in zone %d seems bad\n", dataZone->m_ids[0]));
            f << "###PLC" << ids[j];
            ok=false;
            continue;
          }
          linkPLC.m_footnotePositions[j]=cluster.m_PLCList[ids[j]-1].m_position;
          f << linkPLC.m_footnotePositions[j];
          if (j==0)
            f << "<->";
          else
            f << ",";
        }
        if (ok && linkPLC.m_footnotePositions[0]>linkPLC.m_footnotePositions[1]) {
          MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: the plc orders in zone %d seems bad\n", dataZone->m_ids[0]));
          f << "###";
          ok=false;
        }
        linkPLC.m_id=int(input->readLong(4));
        f << "id=" << linkPLC.m_id << ",";
      }
      else if (what==0) { // attachment
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(4))/65536.f;
        linkPLC.m_dimensions=MWAWVec2f(dim[0],dim[1]);
        f << "dim=" << linkPLC.m_dimensions << ",";
        linkPLC.m_id=int(input->readLong(4));
        f << "id=" << linkPLC.m_id << ",";
      }
      if (input->tell()!=pos+link.m_fieldSize)
        ascFile.addDelimiter(input->tell(),'|');
      if (ok) {
        size_t id=cluster.m_linkPLCList.size();
        cluster.m_linkPLCList.push_back(linkPLC);
        cluster.m_posToLinkIdMap.insert(std::pair<int,size_t>(linkPLC.m_positions[0],id));
        if (linkPLC.m_positions[0]!=linkPLC.m_positions[1])
          cluster.m_posToLinkIdMap.insert(std::pair<int,size_t>(linkPLC.m_positions[1],id));
      }
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+link.m_fieldSize, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()<dataZone->m_entry.end()) {
    f.str("");
    f << link.m_name << "Pos-:end";
    // check me: the size seems always a multiple of 16, so maybe reserved data...
    if (dataZone->m_entry.length()%link.m_fieldSize) {
      f << "###";
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("RagTime5Text::readLinkZones: find some extra data\n"));
        first=false;
      }
    }
    ascFile.addPos(input->tell());
    ascFile.addNote(f.str().c_str());
  }
  return true;
}


////////////////////////////////////////////////////////////
// PLC
////////////////////////////////////////////////////////////
bool RagTime5Text::readPLC(RagTime5TextInternal::ClusterText &cluster, int zoneId)
{
  auto zone=m_document.getDataZone(zoneId);
  if (!zone || !zone->m_entry.valid() || (zone->m_entry.length()%6) ||
      zone->getKindLastPart(zone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Text::readPLC: the entry of zone %d seems bad\n", zoneId));
    return false;
  }
  MWAWEntry entry=zone->m_entry;
  MWAWInputStreamPtr input=zone->getInput();
  bool const hiLo=cluster.m_hiLoEndian;
  input->setReadInverted(!hiLo);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugFile &ascFile=zone->ascii();
  libmwaw::DebugStream f;
  zone->m_isParsed=true;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  auto N=size_t(entry.length()/6);
  f << "Entries(TextPLCDef)[" << *zone << "]:";
  // first check the free list
  int freeId=cluster.m_plcDefFreeBegin;
  std::set<int> listFreeIds;
  bool ok=true;
  for (int i=0; i<cluster.m_plcDefNumFree; ++i) {
    if (freeId<=0 || freeId>int(N) || listFreeIds.find(freeId)!=listFreeIds.end()) {
      MWAW_DEBUG_MSG(("RagTime5Text::readPLC: find a bad freeId=%d\n", freeId));
      ok=false;
      break;
    }
    listFreeIds.insert(freeId);
    input->seek(entry.begin()+(freeId-1)*6, librevenge::RVNG_SEEK_SET);
    freeId=static_cast<int>(input->readLong(4));
  }
  if (ok && freeId) {
    MWAW_DEBUG_MSG(("RagTime5Text::readPLC: last free Id=%d seems bad\n", freeId));
  }
  if (!ok) {
    listFreeIds.clear();
    f << "###badFreeList,";
  }
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  cluster.m_PLCList.resize(N);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  for (size_t i=0; i<N; ++i) {
    long pos=input->tell();
    if (listFreeIds.find(int(i+1))!=listFreeIds.end()) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(6, librevenge::RVNG_SEEK_CUR);
      continue;
    }
    f.str("");
    f << "TextPLCDef-PLC" << i+1 << ":";
    RagTime5TextInternal::PLC plc;
    if (hiLo) {
      plc.m_fileType=static_cast<int>(input->readULong(2));
      plc.m_position=static_cast<int>(input->readULong(2));
      plc.m_value=static_cast<int>(input->readLong(2));
    }
    else {
      plc.m_value=static_cast<int>(input->readLong(2));
      plc.m_position=static_cast<int>(input->readULong(2));
      plc.m_fileType=static_cast<int>(input->readULong(2));
    }

    f << plc;
    cluster.m_PLCList[i]=plc;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  input->setReadInverted(false);
  return true;
}

bool RagTime5Text::readPLCToCharStyle(RagTime5TextInternal::ClusterText &cluster)
{
  if (cluster.m_plcToStyleLink.m_ids.empty())
    return true;
  int const zoneId=cluster.m_plcToStyleLink.m_ids[0];
  if (!zoneId)
    return false;

  auto zone=m_document.getDataZone(zoneId);
  if (!zone || !zone->m_entry.valid() || (zone->m_entry.length()%6) ||
      zone->getKindLastPart(zone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Text::readPLCToCharStyle: the entry of zone %d seems bad\n", zoneId));
    return false;
  }
  MWAWEntry entry=zone->m_entry;
  MWAWInputStreamPtr input=zone->getInput();
  input->setReadInverted(!cluster.m_hiLoEndian); // checkme: can also be zone->m_hiLoEndian
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugFile &ascFile=zone->ascii();
  libmwaw::DebugStream f;
  zone->m_isParsed=true;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  f << "Entries(TextPLCToCStyle)[" << *zone << "]:";

  auto N=int(entry.length()/6);
  if (N>cluster.m_plcToStyleLink.m_N) // rare but can happens
    N=cluster.m_plcToStyleLink.m_N;
  else if (N<cluster.m_plcToStyleLink.m_N) {
    MWAW_DEBUG_MSG(("RagTime5Text::readPLCToCharStyle: N value seems too short\n"));
    f << "##N=" << N << ",";
  }
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  size_t numPLC=cluster.m_PLCList.size();
  long lastFindPos=-1;
  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "TextPLCToCStyle-" << i << ":";
    auto id=size_t(input->readULong(4));
    auto styleId=static_cast<int>(input->readULong(2));
    f << "PLC" << id;
    if (id==0 || id>numPLC) {
      MWAW_DEBUG_MSG(("RagTime5Text::readPLCToCharStyle: find bad PLC id\n"));
      f << "###";
    }
    else {
      auto const plc=cluster.m_PLCList[size_t(id-1)];
      if ((i==0 && plc.m_position!=0) || (i && plc.m_position<lastFindPos)) {
        MWAW_DEBUG_MSG(("RagTime5Text::readPLCToCharStyle: the PLC position seems bad\n"));
        f << "###";
      }
      else
        cluster.m_posToStyleIdMap.insert(std::multimap<int, int>::value_type(plc.m_position, styleId));
      lastFindPos=plc.m_position;
      f << "[" << plc << "]";
    }
    f << "->TS" << styleId << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    ascFile.addPos(input->tell());
    ascFile.addNote("TextPLCToCStyle:#extra");
  }
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// interface send function
////////////////////////////////////////////////////////////

void RagTime5Text::flushExtra(bool onlyCheck)
{
  for (auto it : m_state->m_idTextMap) {
    if (!it.second || it.second->m_isSent)
      continue;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Text::flushExtra: find some unsent zones: %d...\n", it.first));
      first=false;
    }
    if (!onlyCheck)
      send(*it.second, MWAWListenerPtr());
  }
}

bool RagTime5Text::send(RagTime5TextInternal::ClusterText &cluster, RagTime5Zone &dataZone, MWAWListenerPtr listener, size_t firstChar, size_t lastChar,
                        bool isLastZone, double totalWidth)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the listener\n"));
    return false;
  }
  if (cluster.m_separators.empty()) {
    std::shared_ptr<RagTime5Zone> sepZone;
    int cId=!cluster.m_separatorLink.m_ids.empty() ? cluster.m_separatorLink.m_ids[0] : -1;
    if (cId>0)
      sepZone=m_document.getDataZone(cId);
    std::vector<int> separators;
    if (!sepZone) {
      MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the text separator zone %d\n", cId));
    }
    else
      readTextSeparators(*sepZone, cluster.m_separators);
  }
  auto input=dataZone.getInput();
  auto const &entry=dataZone.m_entry;
  auto &ascFile=dataZone.ascii();
  if (!input || long(firstChar)>=entry.length()/2 || firstChar>=cluster.m_separators.size()) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the text\n"));
    return false;
  }
  if (long(lastChar)>entry.length()/2 || lastChar>cluster.m_separators.size()) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: last char seems to big\n"));
    lastChar=std::min<size_t>(size_t(entry.length()/2),cluster.m_separators.size());
  }
  input->seek(entry.begin()+2*long(firstChar), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "TextUnicode:";
  bool newLine=true;
  auto numLinks=cluster.m_linkPLCList.size();
  for (size_t i=firstChar; i<lastChar; ++i) {
    auto plcIt=cluster.m_posToStyleIdMap.lower_bound(int(i));
    while (plcIt!=cluster.m_posToStyleIdMap.end() && plcIt->first==static_cast<int>(i)) {
      int const styleId=plcIt++->second;
      f << "[TS" << styleId << "]";
      MWAWFont font;
      MWAWParagraph para;
      MWAWSection section;
      if (!m_styleManager->updateTextStyles(styleId, font, para, section, totalWidth)) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: the style seems bad\n"));
        f << "###";
      }
      else {
        if (newLine && listener->canOpenSectionAddBreak() && section!=listener->getSection()) {
          if (listener->isSectionOpened())
            listener->closeSection();
          listener->openSection(section);
        }
        listener->setParagraph(para);
        listener->setFont(font);
      }
    }

    switch (cluster.m_separators[i]) {
    case 0: // none
    case 2: // sign separator: .,/-(x)
    case 3: // word separator
    case 4: // potential hyphenate
      break;
    default: // find also 1 and 7:link?, 8, 12
      f << "[m" << cluster.m_separators[i] << "]";
    }
    newLine=false;
    auto linkIt=cluster.m_posToLinkIdMap.find(int(i));
    while (linkIt!=cluster.m_posToLinkIdMap.end() && linkIt->first==int(i)) {
      if (linkIt->second>=numLinks) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: find a bad link\n"));
        ++linkIt;
        continue;
      }
      auto const &plc=cluster.m_linkPLCList[linkIt++->second];
      if (plc.m_what==3 && plc.m_positions[0]!=plc.m_positions[1]) { // index
        MWAWField field(int(i)==plc.m_positions[0] ? MWAWField::BookmarkStart : MWAWField::BookmarkEnd);
        std::stringstream s;
        s << "Index" << plc.m_id;
        field.m_data=s.str();
        listener->insertField(field);
      }
      // TODO when m_what==4, check if we can retrieve the formula, to decide if this is a pagenumber, ...
    }
    auto unicode=uint32_t(input->readULong(2));
    switch (unicode) {
    case 0:
      f << "###[0]";
      break;
    case 9:
      listener->insertTab();
      f << "\t";
      break;
    case 0xb:
    case 0xd:
      if (i+1==lastChar && !isLastZone && unicode==0xd)
        break;
      newLine=unicode==0xd;
      listener->insertEOL(unicode==0xb);
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      pos=input->tell();
      f.str("");
      f << "TextUnicode:";
      break;
    case 0xe820: // attachment
    case 0xe824: { // footnote
      linkIt=cluster.m_posToLinkIdMap.find(int(i));
      bool find=false;
      int expectedType=unicode==0xe820 ? 0 : 5;
      while (linkIt!=cluster.m_posToLinkIdMap.end() && linkIt->first==int(i)) {
        if (linkIt->second>=numLinks) {
          MWAW_DEBUG_MSG(("RagTime5Text::send: find a bad link\n"));
          ++linkIt;
          continue;
        }
        auto const &plc=cluster.m_linkPLCList[linkIt++->second];
        if (plc.m_what!=expectedType) continue;
        find=true;
        if (unicode==0xe824) {
          if (plc.m_footnotePositions[0]>0 && plc.m_footnotePositions[0]<plc.m_footnotePositions[1]) {
            // add a note as comment (we are in a textbox)
            std::shared_ptr<MWAWSubDocument> doc=std::make_shared<RagTime5TextInternal::SubDocument>(*this, input, cluster, dataZone, plc.m_footnotePositions[0], plc.m_footnotePositions[1]);
            listener->insertComment(doc);
          }
        }
        else {
          if (plc.m_id<0 || plc.m_id>=int(cluster.m_childList.size())) {
            MWAW_DEBUG_MSG(("RagTime5Text::send: oops, unknown child %d\n", plc.m_id));
          }
          else {
            auto lnk=cluster.m_childList[size_t(plc.m_id)];
            auto lnkType=m_document.getClusterManager()->getClusterType(lnk.m_dataId);

            MWAWPosition position(MWAWVec2f(0,0), plc.m_dimensions, librevenge::RVNG_POINT);
            position.setRelativePosition(MWAWPosition::CharBaseLine, MWAWPosition::XLeft, MWAWPosition::YCenter);
            if (lnkType==RagTime5ClusterManager::Cluster::C_Unknown) {
              MWAW_DEBUG_MSG(("RagTime5Text::send: oops, child has no dataId\n"));
            }
            else if (lnkType==RagTime5ClusterManager::Cluster::C_PictureZone)
              m_document.send(lnk.m_dataId, listener, position);
            else if (lnkType==RagTime5ClusterManager::Cluster::C_SpreadsheetZone) {
              // let try to create a graphic object to represent the content
              MWAWBox2f box(MWAWVec2f(0,0), position.size());
              MWAWSpreadsheetEncoder spreadsheetEncoder;
              MWAWSpreadsheetListenerPtr spreadsheetListener(new MWAWSpreadsheetListener(*m_parserState, box, &spreadsheetEncoder));
              spreadsheetListener->startDocument();
              MWAWPosition spreadsheetPos;
              spreadsheetPos.m_anchorTo = MWAWPosition::Page;
              m_document.send(lnk.m_dataId, spreadsheetListener, spreadsheetPos);
              spreadsheetListener->endDocument();

              MWAWEmbeddedObject picture;
              if (spreadsheetEncoder.getBinaryResult(picture))
                listener->insertPicture(position, picture);
            }
            else {
              // let try to create a graphic object to represent the content
              MWAWBox2f box(MWAWVec2f(0,0), position.size());
              MWAWGraphicEncoder graphicEncoder;
              MWAWGraphicListenerPtr graphicListener(new MWAWGraphicListener(*m_parserState, box, &graphicEncoder));
              graphicListener->startDocument();
              MWAWPosition graphicPos;
              graphicPos.m_anchorTo = MWAWPosition::Page;
              m_document.send(lnk.m_dataId, graphicListener, graphicPos);
              graphicListener->endDocument();

              MWAWEmbeddedObject picture;
              if (graphicEncoder.getBinaryResult(picture))
                listener->insertPicture(position, picture);
            }
          }
        }
        break;
      }

      if (!find) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the corresponding link\n"));
      }
      f << "[" << std::hex << unicode << std::dec << "]";
      break;
    }
    case 0xe834: // end sub zone<
    case 0xe835: // end zone
      f << "[" << std::hex << unicode << std::dec << "]";
      break;
    default:
      if (unicode<=0x1f) {
        MWAW_DEBUG_MSG(("RagTime5Text::send:  find an odd char %x\n", static_cast<unsigned int>(unicode)));
        f << "[#" << std::hex << unicode << std::dec << "]";
        break;
      }
      listener->insertUnicode(unicode);
      if (unicode<0x80)
        f << char(unicode);
      else
        f << "[" << std::hex << unicode << std::dec << "]";
      break;
    }
  }
  if (pos!=input->tell()||firstChar==lastChar) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool RagTime5Text::send(RagTime5TextInternal::ClusterText &cluster, MWAWListenerPtr listener, int blockId, int cellId, double totalWidth)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the listener\n"));
    return false;
  }
  cluster.m_isSent=true;
  std::vector<RagTime5TextInternal::Block> const *blockZones=nullptr;
  std::vector<RagTime5TextInternal::Block> blockCell;
  if (cellId>0 && cellId<=static_cast<int>(cluster.m_blockCellList.size())) {
    blockCell.push_back(cluster.m_blockCellList[size_t(cellId-1)]);
    blockZones=&blockCell;
  }
  else if (cellId>0) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the block %d in zone %d\n", cellId, cluster.m_zoneId));
  }
  else if (blockId>0 && blockId<=static_cast<int>(cluster.m_blockList.size()) && !cluster.m_blockList[size_t(blockId-1)].empty())
    blockZones=&cluster.m_blockList[size_t(blockId-1)];
  else if (blockId>0) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the block %d in zone %d\n", blockId, cluster.m_zoneId));
  }

  std::shared_ptr<RagTime5Zone> dataZone;
  int cId=!cluster.m_contentLink.m_ids.empty() ? cluster.m_contentLink.m_ids[0] : -1;
  if (cId>0)
    dataZone=m_document.getDataZone(cId);
  else
    dataZone.reset();
  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="Unicode") {
    MWAW_DEBUG_MSG(("RagTime5Text::send: can not find the text contents zone %d\n", cId));
    return false;
  }

  dataZone->m_isParsed=true;
  if (dataZone->m_entry.length()==0) return true;

  MWAWInputStreamPtr input=dataZone->getInput();
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(TextUnicode)[" << *dataZone << "]:";
  if (dataZone->m_entry.length()%2) {
    MWAW_DEBUG_MSG(("RagTime5Text::send: bad length for zone %d\n", cId));
    f << "###";
    ascFile.addPos(dataZone->m_entry.begin());
    ascFile.addNote(f.str().c_str());
    ascFile.addPos(dataZone->m_entry.end());
    ascFile.addNote("_");
    return false;
  }
  input->setReadInverted(!cluster.m_hiLoEndian);
  input->seek(dataZone->m_entry.end()-2, librevenge::RVNG_SEEK_SET);
  if (input->readULong(2)==0xd00) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Text::send: must change some hiLo\n"));
      first=false;
    }
    f << "###hiLo,";
    input->setReadInverted(cluster.m_hiLoEndian);
  }

  auto N=size_t(dataZone->m_entry.length()/2);
  size_t numZones=blockZones ? blockZones->size() : 1;
  ascFile.addPos(dataZone->m_entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(dataZone->m_entry.end());
  ascFile.addNote("_");
  for (size_t z=0; z<numZones; ++z) {
    RagTime5TextInternal::Block block;
    bool checkBlock=false;
    if (blockZones) {
      block=(*blockZones)[z];
      checkBlock=true;
    }
    else if (blockId<0) {
      if (-blockId>static_cast<int>(cluster.m_blockCellList.size())) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: can not find blockCell %d zone\n", -blockId));
        return true;
      }
      block=cluster.m_blockCellList[size_t(-blockId-1)];
      if (block.m_plc[0]==0 && block.m_plc[1]==0)
        return true;
      checkBlock=true;
    }

    size_t firstChar=0, lastChar=N;
    if (checkBlock) {
      bool ok=true;
      for (int i=0; i<2; ++i) {
        if (block.m_plc[i]==0) continue;
        if (block.m_plc[i]<0 || block.m_plc[i]>static_cast<int>(cluster.m_PLCList.size())) {
          MWAW_DEBUG_MSG(("RagTime5Text::send: find bad plc id for block %d\n", cluster.m_zoneId));
          ok=false;
          continue;
        }
        if (i==0)
          firstChar=size_t(cluster.m_PLCList[size_t(block.m_plc[i]-1)].m_position);
        else
          lastChar=size_t(cluster.m_PLCList[size_t(block.m_plc[i]-1)].m_position);
      }
      if (lastChar<firstChar) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: find bad plc positions for block %d\n", cluster.m_zoneId));
        continue;
      }
      if (!ok) continue;
      if (lastChar>N) {
        MWAW_DEBUG_MSG(("RagTime5Text::send: last char seems too big for block %d\n", cluster.m_zoneId));
        lastChar=N;
      }

      auto plcIt=cluster.m_posToStyleIdMap.upper_bound(int(firstChar));
      MWAWFont font;
      MWAWParagraph para;
      MWAWSection section;
      if ((plcIt==cluster.m_posToStyleIdMap.end() || plcIt->first>static_cast<int>(firstChar)) &&
          plcIt !=cluster.m_posToStyleIdMap.begin() && m_styleManager->updateTextStyles((--plcIt)->second, font, para, section, totalWidth)) {
        if (listener->canOpenSectionAddBreak() && section!=listener->getSection()) {
          if (listener->isSectionOpened())
            listener->closeSection();
          listener->openSection(section);
        }
        listener->setParagraph(para);
        listener->setFont(font);
      }
    }

    send(cluster, *dataZone, listener, firstChar, lastChar, z+1==numZones, totalWidth);
  }
  ascFile.addPos(dataZone->m_entry.end());
  ascFile.addNote("_");
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
// cluster parser
////////////////////////////////////////////////////////////

namespace RagTime5TextInternal
{


//! Internal: the helper to read a clustList
struct ClustListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  ClustListParser(RagTime5ClusterManager &clusterManager, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_clusterList()
    , m_clusterManager(clusterManager)
  {
  }
  //! destructor
  ~ClustListParser() final;
  //! returns a name which can be used to debugging
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }

  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=10 && fSz!=12 && fSz!=14) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }

    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    if (listIds[0]) {
      m_clusterList.push_back(listIds[0]);
      f << getClusterDebugName(listIds[0]) << ",";
    }
    if (fSz==12 || fSz==14) {
      unsigned long lVal=input->readULong(4); // c00..small number
      f << "f0=" << (lVal&0x3fffffff);
      if ((lVal&0xc0000000)==0xc0000000) f << "*";
      else if (lVal&0xc0000000) f << ":" << (lVal>>30);
      f << ",";
    }
    int num=fSz==12 ? 2 : 3;
    for (int i=0; i<num; ++i) { // f3=1 if fSz==14, f1=0x200, f2=1 if fSz==12
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    return true;
  }

  //! the list of read cluster
  std::vector<int> m_clusterList;
private:
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

//! Internal: the helper to read a block 2 list
struct BlockCellListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  BlockCellListParser()
    : RagTime5StructManager::DataParser("TextBlockCell")
    , m_blockList()
  {
  }
  //! destructor
  ~BlockCellListParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=20) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::BlockCellListParser::parse: bad data size\n"));
      return false;
    }

    Block block;
    for (int &i : block.m_plc) i=static_cast<int>(input->readLong(4));
    if (block.m_plc[0]==0 && block.m_plc[1]==0) {
      f << "empty,";
      m_blockList.push_back(block);
      return true;
    }
    f << "PLC" << block.m_plc[0] << "<->" << block.m_plc[1] << ",";
    libmwaw::DebugStream f2;
    auto val=static_cast<int>(input->readULong(2));
    if (val) f2 << "fl=" << std::hex << val << std::dec << ",";
    for (int i=0; i<2; ++i) { // f0=a|1e, f1=1-e, f2=[02][145]
      val=static_cast<int>(input->readLong(2));
      if (val) f2 << "f" << i << "=" << val << ",";
    }
    auto fl=input->readULong(2);
    if (fl)
      f2 << "fl=" << std::hex << fl << std::dec << ",";
    for (int i=0; i<4; ++i) { // f3=1-30, f6=1-5c
      val=static_cast<int>(input->readLong(1));
      if (val) f2 << "f" << i+3 << "=" << val << ",";
    }
    f << f2.str();
    block.m_extra=f2.str();
    m_blockList.push_back(block);
    return true;
  }

  //! the list of block
  std::vector<Block> m_blockList;
private:
  //! copy constructor, not implemented
  BlockCellListParser(BlockCellListParser &orig) = delete;
  //! copy operator, not implemented
  BlockCellListParser &operator=(BlockCellListParser &orig) = delete;
};

BlockCellListParser::~BlockCellListParser()
{
}

//
//! low level: parser of text cluster
//
struct TextCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_block, F_indexList, F_footnote, F_linkDefs=F_footnote+2, F_parentLink=F_linkDefs+5, F_nextId, F_plc, F_plcToStyle, F_text, F_textDefs, F_textRoot, F_textList, F_unknLongs=F_textList+3, F_unknData };

  //! constructor
  TextCParser(RagTime5ClusterManager &parser, int type, libmwaw::DebugFile &ascii)
    : ClusterParser(parser, type, "ClustText")
    , m_cluster(new ClusterText)
    , m_expectedIdToType()
    , m_NToBlockIdMap()
    , m_fieldName("")
    , m_asciiFile(ascii)
  {
  }
  //! destructor
  ~TextCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the text cluster
  std::shared_ptr<ClusterText> getTextCluster()
  {
    return m_cluster;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (expected) {
    case F_linkDefs:
    case F_linkDefs+1:
    case F_linkDefs+2:
    case F_linkDefs+3:
    case F_linkDefs+4:
      if (m_cluster->m_linkDefs[expected-F_linkDefs].empty())
        m_cluster->m_linkDefs[expected-F_linkDefs]=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: parent link pos %d is already set\n", expected));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_footnote:
      if (m_cluster->m_footnoteLink.empty())
        m_cluster->m_footnoteLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: footnote link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_indexList:
      if (m_cluster->m_indexLink.empty())
        m_cluster->m_indexLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: index link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_parentLink:
      if (m_cluster->m_parentLink.empty())
        m_cluster->m_parentLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: parent link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_plcToStyle:
      if (m_cluster->m_plcToStyleLink.empty())
        m_cluster->m_plcToStyleLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: link plcToTextStyle is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_plc:
      if (m_cluster->m_plcDefLink.empty())
        m_cluster->m_plcDefLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: link plcDef is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_textList+2:
      if (m_cluster->m_textIntListLink.empty())
        m_cluster->m_textIntListLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: link text int list is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case F_unknData+1:
      m_cluster->m_unknownLinks1.push_back(m_link);
      break;
    case F_unknData+2:
    case F_unknData+3:
      if (m_cluster->m_unknownLink[expected-F_unknData-2].empty())
        m_cluster->m_unknownLink[expected-F_unknData-2]=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::endZone: unknown link %d is already set\n", expected));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    default:
      m_cluster->m_linksList.push_back(m_link);
      break;
    }
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_fieldName="";
    if (m_dataId==0)
      return parseHeaderZone(input,fSz,N,flag,f);

    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected!=-1)
      f << "[F" << m_dataId << "]";
    /*
       normally the header is followed by num[zones] or less but sometimes block zone happens after other zones,
       so just test also fSz.
     */
    if (expected==F_block || fSz==80) {
      return parseZoneBlock(input,fSz,N,flag,f);
    }
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: expected N value\n"));
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
    if (m_dataId==0) {
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x15e0825) {
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xd7842) {
            if ((child.m_longList.size()%3)!=0) {
              MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: block def child seen bad\n"));
              f << "###blockDef[sz]=" << child.m_longList.size() << ",";
            }
            size_t N=child.m_longList.size()/3;
            m_cluster->m_blockList.resize(N);
            f << "blockDef=[";
            for (size_t b=0; b<N; ++b) {
              if (child.m_longList[3*b]==0) {
                f << "_,";
                continue;
              }
              auto id=int(child.m_longList[3*b]-1);
              m_expectedIdToType[id]=F_block;
              if (m_NToBlockIdMap.find(id)!=m_NToBlockIdMap.end()) {
                MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: block pos is already set\n"));
                f << "#";
              }
              else
                m_NToBlockIdMap[id]=b;
              f << "F" << child.m_longList[3*b]-1;
              for (size_t j=1; j<3; ++j) {
                if (child.m_longList[3*b+j]) f << ":" << child.m_longList[3*b+j];
                else f << ":_";
              }
              f << ",";
            }
            f << "],";
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected unkn child[header]\n"));
          f << "####[" << child << "],";
        }
      }
      else if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x3c057) {
        for (auto const &id : field.m_longList) {
          if (m_dataId==0 && id) {
            m_expectedIdToType[int(id-1)]=F_unknData;
            f << "unknData=F" << id-1 << ",";
          }
          else
            f << "unkn0=" << id << ",";
        }
      }
      // extended header
      else if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x15f9015) {
        f << "unknExt=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017) {
            f << "unkn="<<child.m_extra << ",";
            continue;
          }
          if (child.m_type==RagTime5StructManager::Field::T_FieldList && child.m_fileType==0x15f6815) {
            for (auto const &child2 : child.m_fieldList) {
              if (child2.m_type==RagTime5StructManager::Field::T_Unstructured && child2.m_fileType==0xce017) {
                f << "unkn15f6815="<<child2.m_extra << ",";
                continue;
              }
              MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected unkn child2[header]\n"));
              f << "###"<<child2.m_extra << ",";
            }
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected unkn child[header]\n"));
          f << "####[" << child << "],";
        }
        f << "],";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected header field\n"));
        f << "###" << field;
      }
      return true;
    }
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (expected) {
    case F_plc:
      if (field.m_type==RagTime5StructManager::Field::T_2Long && field.m_fileType==0x15e3017) {
        f << "unk=" << field.m_longValue[0] << "x" << field.m_longValue[1] << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected plc link field\n"));
      f << "###" << field;
      break;
    case F_linkDefs:
    case F_linkDefs+1:
    case F_linkDefs+2:
    case F_linkDefs+3:
    case F_linkDefs+4:
      if (field.m_type==RagTime5StructManager::Field::T_FieldList &&
          (field.m_fileType==0x15f4815 /* v5?*/ || field.m_fileType==0x160f815 /* v6? */)) {
        f << "decal=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList)
              f << val << ",";
            m_link.m_longList=child.m_longList;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected decal child[linkDefs]\n"));
          f << "#[" << child << "],";
        }
        f << "],";
        break;
      }
      else if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x15f4015) {
        f << "id=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017) {
            f << "unkn0=" << field.m_longValue[0] << field.m_extra; // id to ?
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected id child[linkDefs]\n"));
          f << "#[" << child << "],";
        }
        f << "],";
        break;
      }
      else if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        f << "unkn0=" << field.m_longValue[0] << field.m_extra; // id to a fSz=0x31 zone
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected child[linkDefs]\n"));
      f << "###" << field;
      break;
    case F_indexList:
    case F_parentLink:
    case F_textList:
    case F_textList+1:
    case F_unknData+2:
    case F_unknData+3:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // 1,2
        f << "unkn=" << field.m_longValue[0] <<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected child[clustList]\n"));
      f << "###" << field;
      break;
    case F_textDefs: // list of id
    case F_unknLongs:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) {
        f << "unkn=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else
            f << val << ",";
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected child[textDefs]\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseField: find unexpected field %d\n", expected));
      f << "###" << field;
      break;
    }
    return true;
  }
protected:
  //! parse a data block
  bool parseDataZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "fl=" << std::hex << flag << std::dec << ",";
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;

    m_link.m_N=N;
    int val;
    long linkValues[4];
    std::string mess("");

    switch (expected) {
    case F_footnote:
    case F_linkDefs+2:
    case F_linkDefs+3:
    case F_linkDefs+4:
    case F_parentLink:
    case F_plc:
    case F_plcToStyle:
    case F_textDefs:
    case F_textList:
    case F_textList+1:
    case F_textList+2:
    case F_unknData+1:
    case F_unknData+2: // related to column
    case F_unknData+3: { // related to colum ?
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Text::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=0, expectedFieldSize=0;
      if (expected==F_parentLink && fSz>=36) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected file type0\n"));
          f << "###type0";
        }
        expectedFileType1=0x10;
        m_link.m_name="textParentLst";
        m_link.m_type=RagTime5ClusterManager::Link::L_ClusterLink;
        for (int i=0; i<2; ++i) { // small value between 3e and 74 some data id ?
          val=static_cast<int>(input->readLong(2));
          if (val) f << "g" << i << "=" << val << ",";
        }
      }
      else if (((expected>=F_linkDefs+2 && expected<=F_linkDefs+4) || expected==F_footnote) && fSz>=39) {
        if (linkValues[3]!=0x15f3817) { // fSz=39
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected link type\n"));
          f << "###values3";
        }
        // fieldSize=12|16
        expectedFileType1=expected==F_footnote ? 0x43 : 0x50;
        if (expected==F_footnote)
          m_link.m_name="footnote";
        else
          m_link.m_name=expected==F_linkDefs+2 ? "linkDef2" : expected==F_linkDefs+3 ? "linkIndex" : "linkField";
        val=static_cast<int>(input->readLong(4)); // 1|8
        if (val) f << "g0=" << val << ",";
        for (int i=0; i<3; ++i) { // g1=language[0:US,7:UK,9:croatian...], g2=1, linkIndex: g2=9
          val=static_cast<int>(input->readLong(i==2 ? 1 : 2));
          if (!val)
            continue;
          f << "g" << i+1 << "=" << val << ",";
        }
      }
      else if (expected==F_plc && fSz>=52) {
        if (m_link.m_fileType[0]!=0 || m_link.m_fieldSize!=6) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected plc file type\n"));
          f << "###plc";
        }
        expectedFileType1=0;
        m_link.m_name="plc";
        for (int i=0; i<5; ++i) { // g2=0 maybe an 2xint other small number
          val=static_cast<int>(input->readLong(4));
          switch (i) {
          case 0:
            if (m_cluster->m_plcDefFreeBegin) {
              MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: the plc root is already set\n"));
              f << "###";
            }
            else
              m_cluster->m_plcDefFreeBegin=val;
            f << "free[rootId]=" << val << ",";
            break;
          case 4:
            if (m_cluster->m_plcDefNumFree<0)
              m_cluster->m_plcDefNumFree=val;
            f << "free[num]=" << val << ",";
            break;
          default:
            if (val) f << "g" << i << "=" << val << ",";
            break;
          }
        }
        val=static_cast<int>(input->readLong(2)); // always 1
        if (val!=1)
          f << "g5=" << val << ",";
      }
      else if (expected==F_plcToStyle && fSz==34) {
        if (m_link.m_fieldSize!=6 || linkValues[3]!=0x15e4817) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected file type1\n"));
          f << "###type";
        }
        m_link.m_name="plcToCStyle";
        expectedFileType1=0x47;
        val=static_cast<int>(input->readLong(4)); // always 1
        if (val!=1) f << "g0=" << val << ",";
      }
      else if (expected==F_textDefs && m_link.m_fileType[0]==0x3c052 && fSz>=41) { // fSz==41|46
        m_link.m_name="textDefs";
        expectedFileType1=0x40;
        val=static_cast<int>(input->readLong(1)); // always 1
        if (val!=1) f << "g0=" << val << ",";
        if (fSz>41) {
          f << "#extra,";
          input->seek(fSz-41, librevenge::RVNG_SEEK_CUR);
        }
        // first g2, g3 are ids to textZone and plc
        for (int i=0; i<3; ++i) { // g1=1, g3=g2+1
          val=static_cast<int>(input->readLong(4));
          if (!val)
            continue;
          if (i==1) {
            m_expectedIdToType[int(val-1)]=F_text;
            f << "textZone=F" << val-1 << ",";
          }
          else if (i==2) {
            m_expectedIdToType[int(val-1)]=F_plc;
            f << "plc=F" << val-1 << ",";
          }
          else
            f << "g" << i+1 << "=" << val << ",";
        }
        // in unkn0, id to textZone
      }
      else if (expected==F_unknData+1 && fSz>=39) {
        expectedFileType1=0x47;
        m_link.m_name="unknData1";
        for (int i=0; i<3; ++i) { // g0=probably previous
          val=static_cast<int>(input->readLong(i==2 ? 1 : 4));
          if (val) f << "g" << i << "=" << val << ",";
        }
      }
      else if (expected==F_unknData+2 && fSz==32) {
        expectedFileType1=0x210;
        m_link.m_name="TextUnknData2";
      }
      else if (expected==F_unknData+3 && fSz==32) {
        expectedFileType1=0x10;
        m_link.m_name="TextUnknData3";
      }
      // v6.5
      else if (expected==F_textList && m_link.m_fileType[0]==0x3e800)
        m_link.m_name="textList0";
      else if (expected==F_textList+1 && m_link.m_fileType[0]==0x35800)
        m_link.m_name="textList1";
      else if (expected==F_textList+2 && m_link.m_fileType[0]==0x45080) {
        m_link.m_name="textListInt";
        expectedFieldSize=2;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        m_fieldName=m_link.m_name;
        f << m_link.m_name << ",";
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_linkDefs: // fSz=69 attachment
    case F_linkDefs+1: { // fSz=71
      m_fieldName=(expected==F_linkDefs ? "attachmentLink" : "itemLink");
      f << m_fieldName << ",";
      if (fSz<69) {
        f << "##fSz,";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected size\n"));
        return true;
      }
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: can not read the link position field\n"));
        f << "###link";
        return true;
      }
      if (linkValues[3]==0x15f3817) {
        if ((m_link.m_fileType[1]&0xFFF7)!=0x43 && (m_link.m_fileType[1]&0xFFF7)!=0x50) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: fileType1 seems odd\n"));
          f << "###fileType1,";
        }
        m_link.m_name="linkDef";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected field\n"));
        m_fieldName="##unknown";
      }
      f << m_link << "," << mess;
      for (int i=0; i<2; ++i) { // g0=1, g1=2,b,c
        val=static_cast<int>(input->readLong(4));
        if (val) f << "g" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(1)); // always 0
      if (val) f << "g2=" << val << ",";
      val=static_cast<int>(input->readLong(2)); // always 0
      if (val!=0x10) f << "g3=" << val << ",";
      val=static_cast<int>(input->readLong(4)); // 1,3, 5
      if (val) f << "g3=" << val << ",";
      RagTime5ClusterManager::Link link2;
      mess="";
      if (!readLinkHeader(input, fSz, link2, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: can not read the link second field\n"));
        f << "###link2";
        return true;
      }
      if (fSz==69 && link2.m_fieldSize==12)
        m_cluster->m_childLink=link2;
      else if (fSz==71 && link2.m_ids.size()==2) {
        // FIXME: store directly the field pos and set link2 as main link
        m_link.m_ids.push_back(link2.m_ids[0]);
        m_link.m_ids.push_back(link2.m_ids[1]);
      }
      else if (!link2.empty()) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: can not find the second link field\n"));
        f << "###";
        m_cluster->m_linksList.push_back(link2);
      }
      f << "link2=[" << link2 << "]," << mess;
      return true;
    }
    case F_text: {
      f << "textZone,";
      if (fSz<28) {
        f << "##fSz,";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected size\n"));
        return true;
      }
      val=static_cast<int>(input->readULong(2));
      if (val!=0x10) {
        f << "##fType=" << std::hex << val << std::dec << ",";
      }
      m_fieldName="textZone";
      val=static_cast<int>(input->readULong(2));
      if (val!=4) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: the first value\n"));
        f << "##f0=" << val << ",";
      }
      val=static_cast<int>(input->readLong(2)); // always 0?
      if (val) f << "f1=" << val << ",";
      val=static_cast<int>(input->readLong(2)); // always f?
      if (val!=15) f << "f2=" << val << ",";
      std::vector<int> listIds;
      if (RagTime5StructManager::readDataIdList(input, 1, listIds) && listIds[0]) {
        if (!m_cluster->m_separatorLink.m_ids.empty()) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: oops the text separator is already set\n"));
          f << "###";
        }
        m_cluster->m_separatorLink.m_ids.push_back(static_cast<int>(listIds[0]));
        f << "textSep=data" << listIds[0] << "A,";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: can not read the text separator\n"));
        f << "##textSeparator,";
      }
      m_link.m_N=static_cast<int>(input->readULong(4));
      val=static_cast<int>(input->readLong(1)); // always 0?
      if (val) f << "f3=" << val << ",";
      listIds.clear();
      if (RagTime5StructManager::readDataIdList(input, 1, listIds) && listIds[0]) {
        if (!m_cluster->m_contentLink.m_ids.empty()) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: oops the text content is already set\n"));
          f << "###";
        }
        m_cluster->m_contentLink.m_ids.push_back(listIds[0]);
        f << "content=data" << listIds[0] << "A,";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: can not read the text content\n"));
        f << "##textContent,";
      }
      val=static_cast<int>(input->readLong(1)); // always 1?
      if (val) f << "f4=" << val << ",";
      f << m_link;
      return true;
    }
    case F_footnote+1: { // checkme, seens rarely with no data...
      f << "footnote1,";
      if (fSz<106) {
        f << "###fSz,";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      for (int i=0; i<8; ++i) { // f1=1, f2=17, f4=2048,
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      for (int i=0; i<6; ++i) { //some dim ? dim0=dim4=712,dim1=dim5=532,dim3=517,
        val=static_cast<int>(input->readLong(4));
        if (val) f << "dim" << i << "=" << val << ",";
      }
      for (int i=0; i<7; ++i) { // f2=2
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      for (int i=0; i<2; ++i) { //some dim ? dim6=121,dim7=74
        val=static_cast<int>(input->readLong(4));
        if (val) f << "dim" << i+6 << "=" << val << ",";
      }
      for (int i=0; i<9; ++i) { // h7=1,h8=1,
        val=static_cast<int>(input->readLong(2));
        if (val) f << "h" << i << "=" << val << ",";
      }
      // then 02050000000e4000020500000205000000000000
      return true;
    }
    case F_unknData: { // checkme, seens rarely with no data...
      if (fSz<49) {
        f << "###fSz,";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << "unknData0,";
      for (int i=0; i<6; ++i) { // f3=1, f4=1c
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto type=input->readULong(4);
      if (type!=0x15e0842) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: fileType0 seems odd\n"));
        f << "###fileType0=" << RagTime5Text::printType(type) << ",";
      }
      for (int i=0; i<4; ++i) { // f6=1, f7=1|2
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+6 << "=" << val << ",";
      }
      for (int i=0; i<3; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (!val) continue;
        if (i==0) {
          m_expectedIdToType[int(val-1)]=F_unknData+1;
          f << "unknData1=F" << val-1 << ",";
        }
        else
          f << "g" << i << "=" << val << ",";
      }
      for (int i=0; i<3; ++i) { // g3: big number, g5=2|3
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i+3 << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(1)); // always 0?
      if (val) f << "g7=" << val << ",";
      return true;
    }
    case F_block:
    case F_nextId:
    case F_indexList:
    case F_textRoot:
    case F_unknLongs:
    default:
      break;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::PictCParser::parseDataZone: find unexpected field\n"));
      f << "###field,";
    }
    switch (fSz) {
    case 29: // unknLong0
    case 44: { // indexlist
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Text::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: the expected field[%d] seems bad\n", m_dataId));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=0, expectedFieldSize=0;
      if (fSz==44 && linkValues[0]==0x1484017) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unexpected file type0\n"));
          f << "###type0";
        }
        expectedFileType1=0x10;
        m_link.m_name="textIndexData";
        m_expectedIdToType[m_dataId]=F_indexList;
        for (int i=0; i<2; ++i) { // g0=1
          val=static_cast<int>(input->readLong(2));
          if (val) f << "g" << i << "=" << val << ",";
        }
        for (int i=0; i<2; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (!val) continue;
          m_expectedIdToType[int(val-1)]=F_unknData+2+i;
          f << "unknData" << i+2 << "=F" << val-1 << ",";
        }
      }
      else if (fSz==29 && m_link.m_fileType[0]==0x3c052) { // v5-v6.5
        m_expectedIdToType[m_dataId]=F_unknLongs;
        m_link.m_name="unknLongs0";
        expectedFileType1=0x50;
        val=int(input->readLong(1));
        if (val!=1) f << "g0=" << val << ",";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: unknow field[%d]\n", m_dataId));
        f << "###field,";
      }
      if (linkValues[2]) {
        m_expectedIdToType[int(linkValues[2]-1)]=F_nextId;
        f << "nextId=F" << linkValues[2]-1 << ",";
      }
      if (!m_link.m_name.empty()) {
        m_fieldName=m_link.m_name;
        f << m_link.m_name << ",";
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case 36: { // v6.6
      m_fieldName="textList[root]";
      m_expectedIdToType[m_dataId]=F_textRoot;
      f << m_fieldName << ",";
      val=static_cast<int>(input->readLong(4));
      if (val) f << "#f0=" << val << ",";
      val=static_cast<int>(input->readLong(4));
      if (val!=0x17db042) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: find unexpected type0\n"));
        f << "#fileType0=" << std::hex << val << std::dec << ",";
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (val) f << "f" << i+1 << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if ((val&0xFFD7)!=0x10) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZone: find unexpected type1[fSz36]\n"));
        f << "#fileType1=" << std::hex << val << std::dec << ",";
      }
      f << "ids=[";
      for (int i=0; i<3; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        m_expectedIdToType[val-1]=F_textList+i;
        f << "F" << val-1 << ",";
      }
      f << "],";
      return true;
    }
    default:
      break;
    }
    f << "###fSz=" << fSz;
    MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseDataZone: find unexpected field size\n"));
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header, fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    if (N!=-5 || m_dataId!=0 || (fSz!=135 && fSz!=140 && fSz!=143 && fSz!=208 && fSz!=212 && fSz!=213 && fSz!=216)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    bool hasData1=(fSz==140||fSz==213);
    int numData2=(fSz==143||fSz==216) ? 2 : fSz==212 ? 1 : 0;
    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (m_type>0 && val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    // f2,f3 are also some ids to listClust and to zone:longs2
    for (int i=0; i<2; ++i) { // f2=9-5d, f3=0
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==0) {
        m_expectedIdToType[int(val-1)]=F_parentLink;
        f << "textParentLst=F" << val-1 << ",";
      }
      else {
        m_expectedIdToType[int(val-1)]=F_nextId;
        f << "nextId=F" << val-1 << ",";
      }
    }
    val=static_cast<int>(input->readLong(1)); // 0|1
    if (val)
      f << "fl=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val&1)
      f << "area[widest,only],";
    if (val&8)
      f << "noShift[baseline,start],";
    if (val&0x10)
      f << "recalculate[demand],";
    if (val&0x1000)
      f << "vertical[writing],";
    val&=0xefe6;
    if (val) // [08]0[08][049]
      f << "fl2=" << std::hex << val << std::dec << ",";
    val=static_cast<int>(input->readLong(1)); // 1|1d
    if ((val&1)==0) f << "hyphen[end],";
    if (val&2) f << "column[balanced],";
    if ((val&4)==0) f << "space[between,para,sum],";
    if ((val&8)==0) f << "footnote[endComponent],";
    if (val&0x10) f << "footnote[number,restart],";
    if (val&0x20) f << "footnote[symbol,cycle],";
    val &= 0xc0;
    if (val)
      f << "fl3=" << std::hex << val << std::dec << ",";
    val=static_cast<int>(input->readULong(2)); // alway 10
    if (val!=0x10)
      f << "f4=" << val << ",";
    int numZones=static_cast<int>(input->readLong(4));
    if (numZones)
      f << "num[zones]=" << numZones << ",";
    for (int i=0; i<11; ++i) { // g8=40|60
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "g" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(1)); // always 1
    if (val!=1)
      f << "fl4=" << val << ",";
    if (hasData1) {
      for (int i=0; i<5; ++i) { // unsure find only 0 here
        val=static_cast<int>(input->readLong(1));
        if (val)
          f << "flA" << i << "=" << val << ",";
      }
    }

    for (int i=0; i<2; ++i) { // always 1,2 checkme id?
      val=static_cast<int>(input->readLong(4));
      if (val!=i+1)
        f << "h" << i << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) { // always 0,4
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      if (i==1)
        f << "column[line,style]=" << val << ","; // 5: contain border, ...
      else
        f << "h" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<4; ++i) { // always h4=3, h5=id to plcToCStyle and zone:longs2
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==1) {
        m_expectedIdToType[int(val-1)]=F_plcToStyle;
        f << "plcToStyle=F" << val-1 << ",";
      }
      else if (i==2) {
        m_expectedIdToType[int(val-1)]=F_textDefs;
        f << "textDefs=F" << val-1 << ",";
      }
      else if (i==3) {
        m_expectedIdToType[int(val-1)]=F_footnote;
        f << "footnote=F" << val-1 << ",";
      }
      else
        f << "h" << i+4 << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) {  // always 1,4
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      if (i==1)
        f << "footnote[sep,style]=" << val << ",";
      else
        f << "h" << i+8 << "=" << val << ",";
    }
    auto sepLen=input->readULong(4);
    if (sepLen!=0x5555)
      f << "footnote[len,separator]=" << 100*double(sepLen)/double(0x10000) << "%,";
    sepLen=input->readULong(4);
    if (sepLen!=0x18000)
      f << "footnote[margins,vert]=" << 100*double(sepLen)/double(0x10000) << "%,";
    for (int i=0; i<5; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      f << "j" << i << "=" << val << ",";
    }
    for (int i=0; i<5; ++i) { // j5=0|5, j6=0|5,
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      m_expectedIdToType[int(val-1)]=F_linkDefs+i;
      char const *what[]= {"attachLink", "itemLink", "linkDef2", "indexLink", "fieldLink"};
      f << what[i] << "=F" << val-1 << ",";
    }
    f << "IDS=[";
    for (int i=0; i<2; ++i) // unsure, junk
      f << std::hex << input->readULong(4) << std::dec << ",";
    f << "],";
    val=static_cast<int>(input->readULong(2)); // c00|cef
    if (val)
      f << "fl5=" << std::hex << val << std::dec << ",";
    for (int i=0; i<numData2; ++i) { // always 0
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==1) {
        m_expectedIdToType[int(val-1)]=F_linkDefs+1; // checkme: another link with fSz=47
        f << "linkDef1[bis]=F" << val-1 << ",";
      }
      else
        f << "k" << i << "=" << val << ",";
    }
    if (fSz<=143)
      return true;

    f << "link2=[";
    long linkValues[4];
    std::string mess("");
    val=static_cast<int>(input->readULong(2));
    if (val!=0x10) f << "fl=" << std::hex << val << std::dec << ",";
    RagTime5ClusterManager::Link link2;
    link2.m_N=static_cast<int>(input->readLong(4));
    mess="";
    if (!readLinkHeader(input, fSz, link2, linkValues, mess)) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: can not read the second link\n"));
      f << "###link2";
      return true;
    }
    if (linkValues[3]==0x15f3817 && link2.m_fieldSize==20)
      m_cluster->m_blockCellToPlcLink=link2;
    else {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: blockCell to plc link\n"));
      f << "###";
    }
    f << link2 << "," << mess;
    for (int i=0; i<2; ++i) { // always 1 and 4
      val=static_cast<int>(input->readLong(4));
      if (val) f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(1)); // always 1
    if (val) f << "f2=" << val << ",";
    f << "],";

    f << "link3=[";
    val=static_cast<int>(input->readULong(2));
    if (val!=0x10) f << "fl=" << std::hex << val << std::dec << ",";
    RagTime5ClusterManager::Link link3;
    link3.m_N=static_cast<int>(input->readLong(4));
    mess="";
    if (!readLinkHeader(input, fSz, link3, linkValues, mess)) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: can not read the third link\n"));
      f << "###link3";
      return true;
    }
    if (link3.m_fieldSize==12)
      m_cluster->m_unknownLink[2]=link3;
    else {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: third link seems bad\n"));
      f << "###";
    }
    f << link3 << "," << mess;
    f << "],";

    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseHeaderZone: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    if (listIds[0]) {
      m_cluster->m_clusterIdsList.push_back(listIds[0]);
      f << "cluster=" << getClusterDebugName(listIds[0]) << ",";
    }
    return true;
  }
  //! parse a zone block
  bool parseZoneBlock(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    if (N<0 || fSz!=80) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZoneBlock: find unexpected main field\n"));
      return false;
    }
    RagTime5TextInternal::Block block;
    m_fieldName="block";
    std::string debugHeader=f.str();
    f.str("");
    if (N!=1) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZoneBlock: zone N seems badA\n"));
      f << "#N=" << N << ",";
    }
    auto val=static_cast<int>(input->readULong(2)); // always 0?
    if (val) f << "f0=" << val << ",";
    block.m_id=static_cast<int>(input->readULong(2));
    val=static_cast<int>(input->readULong(2)); //[04][01248a][01][23]
    if (val) f << "fl=" << std::hex << val << std::dec << ",";
    block.m_subId=static_cast<int>(input->readULong(2));
    val=static_cast<int>(input->readULong(2)); //f1=0|3ffe
    if (val) f << "f1=" << val << ",";
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    block.m_dimension=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3]));
    for (float &i : dim) i=float(input->readLong(4))/65536.f;
    MWAWBox2f box2(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3]));
    if (block.m_dimension!=box2)
      f << "boxA=" << box2 << ",";
    int nextId=0;
    for (int i=0; i<4; ++i) { // g1=0|2, g3=9|7
      val=static_cast<int>(input->readLong(i<2 ? 4 : 2));
      if (!val) continue;
      switch (i) {
      case 0: // prev
        m_expectedIdToType[int(val-1)]=F_block;
        f << "prev=F" << val-1 << ",";
        break;
      case 1: // next
        m_expectedIdToType[int(val-1)]=F_block;
        f << "next=F" << val-1 << ",";
        nextId=val;
        break;
      case 3:
        m_expectedIdToType[int(val-1)]=F_footnote+1;
        f << "footnote1=F" << val-1 << ",";
        break;
      default:
        f << "g" << i << "=" << val << ",";
        break;
      }
    }
    for (int &i : block.m_plc) i=static_cast<int>(input->readULong(4));
    for (int i=0; i<6; ++i) { // h1=h2=0|-1
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      f << "h" << i << "=" << val << ",";
    }
    block.m_extra=f.str();
    f.str("");
    f << debugHeader << "block,fl=" << std::hex << flag << std::dec << "," << block;

    if (m_NToBlockIdMap.find(m_dataId)==m_NToBlockIdMap.end()) {
      MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZoneBlock: unknown block for N=%d\n", m_dataId));
      f << "###unknown,";
    }
    else {
      m_cluster->m_blockList[m_NToBlockIdMap.find(m_dataId)->second].push_back(block);
      if (nextId && m_NToBlockIdMap.find(nextId)!=m_NToBlockIdMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5TextInternal::TextCParser::parseZoneBlock: next id block for N=%d is already set\n", nextId));
        f << "###nextId,";
      }
      else if (nextId)
        m_NToBlockIdMap[nextId-1]=m_NToBlockIdMap.find(m_dataId)->second;
    }
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterText> m_cluster;
  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the field pos to block map
  std::map<int, size_t> m_NToBlockIdMap;
  //! the actual field name
  std::string m_fieldName;
  //! the ascii file
  libmwaw::DebugFile &m_asciiFile;
private:
  //! copy constructor (not implemented)
  TextCParser(TextCParser const &orig) = delete;
  //! copy operator (not implemented)
  TextCParser &operator=(TextCParser const &orig) = delete;
};

TextCParser::~TextCParser()
{
}

}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Text::readTextCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Text::readTextCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5TextInternal::TextCParser parser(*clusterManager, zoneType, zone.ascii());
  if (!clusterManager->readCluster(zone, parser) || !parser.getTextCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Text::readTextCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  auto cluster=parser.getTextCluster();
  if (m_state->m_idTextMap.find(zone.m_ids[0])!=m_state->m_idTextMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Text::readTextCluster: oops text zone %d is already stored\n", zone.m_ids[0]));
  }
  else
    m_state->m_idTextMap[zone.m_ids[0]]=cluster;
  m_document.checkClusterList(cluster->m_clusterIdsList);

  if (!cluster->m_dataLink.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Text::readTextCluster: oops do not know how to read the dataLink\n"));
  }

  // the text<->separator zone cluster->m_separatorLink.m_ids[0] will be parsed when we send the cluster
  // the textzone cluster->m_contentLink.m_ids[0] will be parsed when we send the cluster

  if (!cluster->m_plcDefLink.m_ids.empty())
    readPLC(*cluster, cluster->m_plcDefLink.m_ids[0]);
  readPLCToCharStyle(*cluster); // read cluster->m_plcToStyleLink
  if (!cluster->m_blockCellToPlcLink.empty()) {
    RagTime5TextInternal::BlockCellListParser blockCellParser;
    m_document.readFixedSizeZone(cluster->m_blockCellToPlcLink, blockCellParser);
    cluster->m_blockCellList=blockCellParser.m_blockList;
  }
  for (auto const &link : cluster->m_unknownLinks1)
    m_document.readFixedSizeZone(link, "TextUnkn0");
  if (!cluster->m_unknownLink[0].empty()) { // some unicode string related to index ?
    std::map<int, librevenge::RVNGString> idToStringMap;
    m_document.readUnicodeStringList(RagTime5ClusterManager::NameLink(cluster->m_unknownLink[0]), idToStringMap);
  }
  if (!cluster->m_unknownLink[1].empty()) // related to column/section ?
    m_document.readListZone(cluster->m_unknownLink[1]);
  if (!cluster->m_unknownLink[2].empty())
    m_document.readFixedSizeZone(cluster->m_unknownLink[2], "TextUnkn3");
  // parent zones:  graphic or pipeline, ...
  if (!cluster->m_parentLink.empty()) {
    RagTime5TextInternal::ClustListParser linkParser(*clusterManager, "TextParentLst");
    m_document.readListZone(cluster->m_parentLink, linkParser);
    m_document.checkClusterList(linkParser.m_clusterList);
  }
  if (!cluster->m_indexLink.empty())
    m_document.readListZone(cluster->m_indexLink);
  if (!cluster->m_childLink.empty()) {
    cluster->m_childLink.m_name="TextChildLst";
    m_document.readChildList(cluster->m_childLink, cluster->m_childList, true);
  }
  for (int i=0; i<5; ++i) {
    // 0: attachement, pos sz=32, id, dim, ??
    // 1: item: list type in unicode pos sz=12 or v6 sz=24
    // 2: maybe end doc or section, pos sz=16
    // 3: index, pos sz=12
    // 4: pos sz=16
    auto &lnk = cluster->m_linkDefs[i];
    if (lnk.empty()) continue;
    std::stringstream s;
    if (i==0)
      lnk.m_name="TextLinkAttach";
    else if (i==1)
      lnk.m_name="TextLinkItem";
    else if (i==3)
      lnk.m_name="TextLinkIndex";
    else if (i==4)
      lnk.m_name="TextLinkFormula";
    else {
      s << "TextLink" << i;
      lnk.m_name=s.str();
    }
    readLinkZones(*cluster, lnk, i);
  }
  if (!cluster->m_footnoteLink.empty()) {
    cluster->m_footnoteLink.m_name="TextLinkFootnote";
    readLinkZones(*cluster, cluster->m_footnoteLink, 5);
  }
  if (!cluster->m_textIntListLink.empty()) { // only v6
    std::vector<long> intList;
    cluster->m_textIntListLink.m_name="TextListInt";
    m_document.readLongList(cluster->m_textIntListLink, intList);
  }
  for (auto const &link : cluster->m_linksList) {
    if (link.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(link);
      continue;
    }
    std::stringstream s;
    s << "Text_Data" << link.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(link.m_name.empty() ? s.str() : link.m_name);
    m_document.readFixedSizeZone(link, defaultParser);
  }
  return cluster;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
