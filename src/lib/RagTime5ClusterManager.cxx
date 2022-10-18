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

#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stack>

#include "MWAWDebug.hxx"
#include "MWAWPosition.hxx"

#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"

#include "RagTime5ClusterManager.hxx"

/** Internal: the structures of a RagTime5ClusterManager */
namespace RagTime5ClusterManagerInternal
{
//! cluster information
struct ClusterInformation {
  //! constructor
  ClusterInformation()
    : m_type(-1)
    , m_fileType(-1)
    , m_name("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, ClusterInformation const &info)
  {
    switch (info.m_type) {
    case 0:
      o << "root,";
      break;
    case 0x1:
      o << "layout,";
      break;
    // case 0xe: mainTextZone? either a graphic zone or a text zone
    case 0x42:
      o << "colPat,";
      break;
    case 0x80:
      o << "style,";
      break;
    case 0x104:
      o << "pipeline,";
      break;
    case 0x10000:
      o << "gObjProp,";
      break;
    case 0x20000:
      o << "formulaDef,";
      break;
    case 0x20001:
      o << "formulaPos,";
      break;
    case 0x30000:
      o << "unkC_A,";
      break;
    case 0x30001:
      o << "unkC_B,";
      break;
    case 0x30002:
      o << "unkC_C,";
      break;
    case 0x30003:
      o << "unkC_D,";
      break;
    case 0x40000:
      o << "picture,";
      break;
    case 0x40001:
      o << "graphic,";
      break;
    case 0x40002:
      o << "spreadsheet,";
      break;
    case 0x40003:
      o << "text,";
      break;
    case 0x40004:
      o << "chart,";
      break;
    case 0x40005:
      o << "button,";
      break;
    case 0x40006:
      o << "sound,";
      break;
    case 0x40007:
      o << "group[zones],";
      break;
    default:
      if (info.m_fileType>=0)
        o << "typ=" << std::hex << info.m_fileType << std::dec << ",";
    }
    if ((info.m_fileType&8)==0) o << "auto[delete],";
    if (info.m_fileType&0x20) o << "visible[selected],";
    if (info.m_fileType&0x4000) o << "tear[on],";
    if (info.m_fileType&0x8000) o << "lock,";
    if (!info.m_name.empty())
      o << info.m_name.cstr() << ",";
    return o;
  }
  //! the cluster type
  int m_type;
  //! the cluster file type
  int m_fileType;
  //! the cluster name
  librevenge::RVNGString m_name;
};
//! Internal: the state of a RagTime5ClusterManager
struct State {
  //! constructor
  State()
    : m_idToClusterInfoMap()
    , m_idToClusterMap()
    , m_rootIdList()
  {
  }
  //! map id to cluster information map
  std::map<int, ClusterInformation> m_idToClusterInfoMap;
  //! map id to cluster map
  std::map<int, std::shared_ptr<RagTime5ClusterManager::Cluster> > m_idToClusterMap;
  //! the root id list
  std::vector<int> m_rootIdList;
};
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5ClusterManager::RagTime5ClusterManager(RagTime5Document &doc)
  : m_state(new RagTime5ClusterManagerInternal::State)
  , m_document(doc)
  , m_structManager(m_document.getStructManager())
{
}

RagTime5ClusterManager::~RagTime5ClusterManager()
{
}

RagTime5ClusterManager::Cluster::~Cluster()
{
}

RagTime5ClusterManager::ClusterParser::~ClusterParser()
{
}

RagTime5ClusterManager::ClusterRoot::~ClusterRoot()
{
}

RagTime5ClusterManager::Cluster::Type RagTime5ClusterManager::getClusterType(int zId) const
{
  if (m_state->m_idToClusterMap.find(zId) == m_state->m_idToClusterMap.end() ||
      !m_state->m_idToClusterMap.find(zId)->second) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::getClusterType: can not find cluster type for zone %d\n", zId));
    return RagTime5ClusterManager::Cluster::C_Unknown;
  }
  return m_state->m_idToClusterMap.find(zId)->second->m_type;
}

////////////////////////////////////////////////////////////
// read basic structures
////////////////////////////////////////////////////////////
bool RagTime5ClusterManager::readFieldHeader(RagTime5Zone &zone, long endPos, std::string const &headerName, long &endDataPos, long expectedLVal)
{
  MWAWInputStreamPtr input=zone.getInput();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;

  f << headerName << ":";
  long lVal, sz;
  bool ok=true;
  if (pos>=endPos || !RagTime5StructManager::readCompressedLong(input, endPos, lVal) ||
      !RagTime5StructManager::readCompressedLong(input,endPos,sz) || sz <= 7 || input->tell()+sz>endPos) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readFieldHeader: can not read the main item\n"));
    f << "###";
    ok=false;
  }
  else {
    if (lVal!=expectedLVal)
      f << "f0=" << lVal << ",";
    f << "sz=" << sz << ",";
    endDataPos=input->tell()+sz;
  }
  if (!headerName.empty()) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return ok;
}

std::string RagTime5ClusterManager::getClusterDebugName(int id)
{
  if (!id) return "";
  std::stringstream s;
  s << "data" << id << "A";
  if (m_state->m_idToClusterInfoMap.find(id)!=m_state->m_idToClusterInfoMap.end())
    s << "[" << m_state->m_idToClusterInfoMap.find(id)->second << "]";
  return s.str();
}

void RagTime5ClusterManager::setClusterName(int id, librevenge::RVNGString const &name)
{
  if (!id) return;
  auto it=m_state->m_idToClusterInfoMap.find(id);
  if (it==m_state->m_idToClusterInfoMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::setClusterName: can not find cluster %d\n", id));
    return;
  }
  else if (!it->second.m_name.empty()) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::setClusterName: cluster %d already has a name\n", id));
    return;
  }
  it->second.m_name=name;
}

////////////////////////////////////////////////////////////
// link to cluster
////////////////////////////////////////////////////////////
bool RagTime5ClusterManager::readClusterMainList(RagTime5ClusterManager::ClusterRoot &root, std::vector<int> &lists, std::vector<int> const &clusterIdList)
{
  std::map<int, librevenge::RVNGString> idToNameMap;
  if (!root.m_listClusterName.empty())
    m_document.readUnicodeStringList(root.m_listClusterName, idToNameMap);
  if (!root.m_listClusterLink[0].empty()) {
    std::vector<long> unknList;
    m_document.readLongList(root.m_listClusterLink[0], unknList);
  }
  auto zone=m_document.getDataZone(root.m_listClusterId);
  if (!zone || zone->getKindLastPart(zone->m_kinds[1].empty())!="ItemData" ||
      zone->m_entry.length()<24 || (zone->m_entry.length()%8)) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterMainList: the item list seems bad\n"));
    return false;
  }
  MWAWEntry &entry=zone->m_entry;
  zone->m_isParsed=true;
  MWAWInputStreamPtr input=zone->getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  input->setReadInverted(!zone->m_hiLoEndian);

  libmwaw::DebugFile &ascFile=zone->ascii();
  libmwaw::DebugStream f;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  auto N=int(entry.length()/8);
  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    f.str("");
    if (i==0)
      f << "Entries(RootClustMain)[" << *zone << "]:";
    else
      f << "RootClustMain-" << i+1 << ":";
    librevenge::RVNGString name("");
    if (idToNameMap.find(i+1)!=idToNameMap.end()) {
      name=idToNameMap.find(i+1)->second;
      f << name.cstr() << ",";
    }
    std::vector<int> listIds;
    if (!m_structManager->readDataIdList(input, 1, listIds)) {
      input->seek(pos+8, librevenge::RVNG_SEEK_SET);
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    if (listIds[0]==0) {
      input->seek(pos+8, librevenge::RVNG_SEEK_SET);
      ascFile.addPos(pos);
      ascFile.addNote("_");
      continue;
    }
    f << "data" << listIds[0] << "A,";
    m_state->m_rootIdList.push_back(listIds[0]);
    auto val=static_cast<int>(input->readULong(2)); // the type
    if (val) f << "type=" << std::hex << val << std::dec << ",";
    RagTime5ClusterManagerInternal::ClusterInformation info;
    info.m_fileType=val;
    info.m_name=name;
    m_state->m_idToClusterInfoMap[listIds[0]]=info;
    lists.push_back(listIds[0]);
    val=static_cast<int>(input->readLong(2)); // always 0?
    if (val) f << "#f1=" << val << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  input->setReadInverted(false);

  // update the cluster info zone
  for (auto cId : clusterIdList) {
    auto data=m_document.getDataZone(cId);
    if (!data) continue;
    if (m_state->m_idToClusterInfoMap.find(cId)==m_state->m_idToClusterInfoMap.end()) {
      RagTime5ClusterManagerInternal::ClusterInformation info;
      info.m_fileType=getClusterFileType(*data);
      info.m_type=getClusterType(*data, info.m_fileType);
      m_state->m_idToClusterInfoMap[cId]=info;
      continue;
    }
    auto &info=m_state->m_idToClusterInfoMap.find(cId)->second;
    info.m_type=getClusterType(*data, info.m_fileType);
  }
  return true;
}

bool RagTime5ClusterManager::readUnknownClusterC(Link const &link)
{
  if (link.m_ids.size()!=4) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readUnknownClusterC: call with bad ids\n"));
    return false;
  }
  for (size_t i=0; i<4; ++i) {
    if (!link.m_ids[i]) continue;
    auto data=m_document.getDataZone(link.m_ids[i]);
    if (!data || data->m_isParsed || data->getKindLastPart(data->m_kinds[1].empty())!="Cluster") {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readUnknownClusterC: the child cluster id %d seems bad\n", link.m_ids[i]));
      continue;
    }
    m_document.readClusterZone(*data, 0x30000+int(i));
  }
  return true;
}

////////////////////////////////////////////////////////////
// main cluster fonction
////////////////////////////////////////////////////////////
bool RagTime5ClusterManager::readCluster(RagTime5Zone &zone, RagTime5ClusterManager::ClusterParser &parser, bool warnForUnparsed)
{
  MWAWEntry &entry=zone.m_entry;
  if (entry.length()<13) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: the zone %d seems bad\n", zone.m_ids[0]));
    return false;
  }
  auto cluster=parser.getCluster();
  if (!cluster) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: oops, the cluster is not defined\n"));
    return false;
  }
  cluster->m_hiLoEndian=parser.m_hiLoEndian=zone.m_hiLoEndian;
  cluster->m_zoneId=zone.m_ids[0];

  MWAWInputStreamPtr input=zone.getInput();
  long endPos=entry.end();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f.str("");
  f << "Entries(" << parser.getZoneName() << ")[" << zone << "]:";
  if (m_state->m_idToClusterInfoMap.find(zone.m_ids[0])!=m_state->m_idToClusterInfoMap.end() &&
      !m_state->m_idToClusterInfoMap.find(zone.m_ids[0])->second.m_name.empty()) {
    cluster->m_name=m_state->m_idToClusterInfoMap.find(zone.m_ids[0])->second.m_name;
    f << cluster->m_name.cstr() << ",";
  }
  for (int i=0; i<4; ++i) { // f0=f1=0, f2=1, f3=small number
    static int const expected[]= {0,0,1,0};
    auto val=static_cast<int>(input->readLong(2));
    if (val!=expected[i]) f << "f" << i << "=" << val << ",";
  }
  zone.m_isParsed=true;
  libmwaw::DebugFile &ascFile=zone.ascii();
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(endPos);
  ascFile.addNote("_");

  // first create the list of zone to parse
  parser.m_dataId=-1;
  std::map<int, MWAWEntry> idToEntryMap;
  std::set<int> toParseSet;
  MWAWEntry zEntry;
  while (!input->isEnd()) {
    long pos=input->tell();
    if (pos>=endPos) break;
    long endDataPos;
    ++parser.m_dataId; // update dataId
    if (!readFieldHeader(zone, endPos, parser.getZoneName(parser.m_dataId), endDataPos) ||
        !input->checkPosition(endDataPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    zEntry.setId(parser.m_dataId);
    zEntry.setBegin(input->tell());
    zEntry.setEnd(endDataPos);
    idToEntryMap[parser.m_dataId]=zEntry;
    toParseSet.insert(parser.m_dataId);
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
  }

  long pos=input->tell();
  if (pos!=endPos) {
    f.str("");
    f << parser.getZoneName() << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  while (!toParseSet.empty()) {
    int id=parser.getNewZoneToParse();
    if (id>=0 && toParseSet.find(id)==toParseSet.end()) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: zone %d is not valid, reset to basic method\n", id));
      id=-1;
    }
    if (id<0)
      id=*(toParseSet.begin());
    toParseSet.erase(id);

    auto it=idToEntryMap.find(id);
    if (it==idToEntryMap.end()) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: can not find some id=%d\n", id));
      continue;
    }
    parser.m_dataId=id;
    parser.m_link=Link();
    parser.startZone();

    pos=it->second.begin();
    long endDataPos=it->second.end();
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << parser.getZoneName(parser.m_dataId) << "-A:";
    if (!zone.m_hiLoEndian) f << "lohi,";
    long fSz;
    if (!RagTime5StructManager::readCompressedLong(input, endDataPos, fSz) || fSz<6 || input->tell()+fSz>endDataPos) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: can not read item A\n"));
      f << "###fSz";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());

      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
      continue;
    }
    long debSubDataPos=input->tell();
    long endSubDataPos=debSubDataPos+fSz;
    auto fl=static_cast<int>(input->readULong(2)); // [01][13][0139b]
    auto N=static_cast<int>(input->readLong(4));
    if (!parser.parseZone(input, fSz, N, fl, f) && warnForUnparsed) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: find an unparsed zone\n"));
      f << "###";
    }

    if (input->tell()!=endSubDataPos) {
      ascFile.addDelimiter(input->tell(),'|');
      input->seek(endSubDataPos, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    int m=-1;
    while (!input->isEnd()) {
      pos=input->tell();
      if (pos+4>endDataPos)
        break;
      ++m;

      RagTime5StructManager::Field field;
      if (!m_structManager->readField(input, endDataPos, ascFile, field)) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      f.str("");
      f << parser.getZoneName(parser.m_dataId,m) << ":";
      if (!parser.parseField(field, m, f)) {
        if (warnForUnparsed) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: find an unparsed field\n"));
          f << "###";
        }
        f << field;
      }
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    pos=input->tell();
    if (pos!=endDataPos) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: find some extra data\n"));
      f.str("");
      f << parser.getZoneName(parser.m_dataId) <<  ":" << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    parser.endZone();
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
  }

  input->setReadInverted(false);

  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool RagTime5ClusterManager::sendClusterMainList()
{
  MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(200,200), librevenge::RVNG_POINT);
  pos.m_anchorTo=MWAWPosition::Char;
  for (auto id : m_state->m_rootIdList) {
    if (!id) continue;
    if (m_state->m_idToClusterMap.find(id) == m_state->m_idToClusterMap.end() ||
        !m_state->m_idToClusterMap.find(id)->second) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::sendClusterMainList: can not find cluster type for zone %d\n", id));
      continue;
    }
    auto cluster=m_state->m_idToClusterMap.find(id)->second;
    if (cluster->m_isSent) continue;
    auto type=cluster->m_type;
    if (type==Cluster::C_ChartZone|| type==Cluster::C_GraphicZone || type==Cluster::C_PictureZone ||
        type==Cluster::C_SpreadsheetZone || type==Cluster::C_TextZone)
      m_document.send(id, MWAWListenerPtr(), pos);
  }
  return true;
}

////////////////////////////////////////////////////////////
// pattern cluster implementation
////////////////////////////////////////////////////////////
std::string RagTime5ClusterManager::ClusterParser::getClusterDebugName(int id)
{
  return m_parser.getClusterDebugName(id);
}

bool RagTime5ClusterManager::ClusterParser::readLinkHeader(MWAWInputStreamPtr &input, long fSz, Link &link, long(&values)[4], std::string &msg)
{
  if (fSz<28)
    return false;
  long pos=input->tell();
  std::stringstream s;
  link.m_fileType[0]=input->readULong(4);
  bool shortFixed=link.m_fileType[0]==0x3c052 ||
                  (fSz<30 && (link.m_fileType[0]==0x34800||link.m_fileType[0]==0x35800||link.m_fileType[0]==0x3e800));
  if (shortFixed) {
    link.m_type=RagTime5ClusterManager::Link::L_LongList;
    link.m_fieldSize=4;
  }
  else if (fSz<30) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (link.m_fileType[0])
    s << "type1=" << RagTime5ClusterManager::printType(link.m_fileType[0]) << ",";
  values[0]=long(input->readULong(4));
  if (values[0])
    s << "f0=" << std::hex << values[0] << std::dec << ",";
  for (int i=1; i<3; ++i) { // always 0?
    values[i]=input->readLong(2);
    if (values[i]) s << "f" << i << "=" << values[i] << ",";
  }
  values[3]=long(input->readULong(4));
  if (values[3]) s << "f3=" << std::hex << values[3] << std::dec << ",";
  link.m_fileType[1]=input->readULong(2);
  bool done=false;
  if (!shortFixed) {
    link.m_fieldSize=static_cast<int>(input->readULong(2));
    if (link.m_fieldSize==0 || link.m_fieldSize==1 || link.m_fieldSize==0x100) {
      if (fSz<32) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        return false;
      }
      input->seek(-2, librevenge::RVNG_SEEK_CUR);
      if (!RagTime5StructManager::readDataIdList(input, 2, link.m_ids) || !link.m_ids[1]) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        return false;
      }
      link.m_fieldSize=0;
      link.m_type= RagTime5ClusterManager::Link::L_List;
      done=true;
    }
    else if ((link.m_fieldSize%2)!=0 || link.m_fieldSize>=0x100) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
  }
  if (!done && !RagTime5StructManager::readDataIdList(input, 1, link.m_ids)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if ((link.m_ids[0]==0 && (link.m_fileType[1]&0x20)==0 && link.m_N) ||
      (link.m_ids[0] && (link.m_fileType[1]&0x20)!=0)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  link.m_fileType[1] &=0xFFDF;

  msg=s.str();
  return true;
}

namespace RagTime5ClusterManagerInternal
{

//
//! low level: parser of color pattern cluster : zone 0x8042
//
struct ColPatCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  explicit ColPatCParser(RagTime5ClusterManager &parser)
    : ClusterParser(parser, 0x8042, "ClustColPat")
    , m_cluster(new RagTime5ClusterManager::Cluster(RagTime5ClusterManager::Cluster::C_ColorPattern))
  {
  }
  //! destructor
  ~ColPatCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    /*
      v5-v6.2 find only the header zone
      v6.5 find only header zone following by a link field (fSz=30)
     */
    if ((m_dataId==0&&flag!=0x30) || (m_dataId==1&&flag!=0x10) || m_dataId>=2)
      f << "fl=" << std::hex << flag << std::dec << ",";

    if (N==-5) {
      int val;
      if (m_dataId || (fSz!=82 && fSz!=86)) {
        f << "###data,";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseZone: find unexpected field\n"));
        return false;
      }
      for (int i=0; i<2; ++i) { // always 0?
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+1 << "=" << val << ",";
      }
      auto type=input->readULong(4);
      if (type!=0x16a8042) f << "#fileType=" << RagTime5ClusterManager::printType(type) << ",";
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+3 << "=" << val << ",";
      }

      for (int wh=0; wh<2; ++wh) {
        long actPos=input->tell();
        RagTime5ClusterManager::Link link;
        f << "link" << wh << "=[";
        val=static_cast<int>(input->readLong(2));
        if (val!= 0x10)
          f << "f0=" << val << ",";
        link.m_N=static_cast<int>(input->readLong(4));
        link.m_fileType[1]=input->readULong(4);
        if ((wh==0 && link.m_fileType[1]!=0x84040) ||
            (wh==1 && link.m_fileType[1]!=0x16de842))
          f << "#fileType=" << RagTime5ClusterManager::printType(link.m_fileType[1]) << ",";
        for (int i=0; i<7; ++i) {
          val=static_cast<int>(input->readLong(2)); // always 0?
          if (val) f << "f" << i+2 << "=" << val << ",";
        }
        link.m_fieldSize=static_cast<int>(input->readULong(2));
        std::vector<int> listIds;
        if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseZone: can not read the data id\n"));
          f << "##link=" << link << "],";
          input->seek(actPos+30, librevenge::RVNG_SEEK_SET);
          continue;
        }
        if (listIds[0]) {
          link.m_ids.push_back(listIds[0]);
          m_cluster->m_linksList.push_back(link);
        }
        f << link;
        f << "],";
      }
      std::vector<int> listIds;
      if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
        f << "##clusterIds";
        return true;
      }
      if (listIds[0]) {
        m_cluster->m_clusterIdsList.push_back(listIds[0]);
        f << "clusterId1=" << getClusterDebugName(listIds[0]) << ",";
      }
      if (fSz==82)
        return true;
      val=static_cast<int>(input->readLong(4));
      if (val!=2)
        f << "g0=" << val << ",";
      return true;
    }

    if (N<=0 || m_dataId!=1) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseZone: find unexpected header N\n"));
      f << "###N=" << N << ",";
      return false;
    }
    if (fSz!=30) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseZone: find unexpected data size\n"));
      f << "###fSz=" << fSz << ",";
      return false;
    }

    std::string mess;
    RagTime5ClusterManager::Link link;
    link.m_N=N;
    long linkValues[4]; // f0=2b|2d|85|93
    if (readLinkHeader(input,fSz,link,linkValues,mess) && link.m_fieldSize==10) {
      if (link.m_fileType[1]!=0x40)
        f << "###fileType1=" << std::hex << link.m_fileType[1] << std::dec << ",";
      f << link << "," << mess;
      if (!link.empty())
        m_cluster->m_linksList.push_back(link);
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseZone: can not read a link\n"));
      f << "###link" << link << ",";
    }
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (m_dataId==0 && field.m_type==RagTime5StructManager::Field::T_FieldList && (field.m_fileType==0x16be055 || field.m_fileType==0x16be065)) {
      f << "unk" << (field.m_fileType==0x16be055 ? "0" : "1") << "=";
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0xcf817) {
          f << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseField: find unexpected color/pattern child field\n"));
        f << "#[" << child << "],";
      }
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::ColPatCParser::parseField: find unexpected sub field\n"));
      f << "#" << field;
    }
    return true;
  }
protected:
  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> m_cluster;
};

ColPatCParser::~ColPatCParser()
{
}

//
//! try to read a root cluster: 4001
//
struct RootCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_formulaLink=0, F_clusterList, F_functionName=F_clusterList+3, F_docInfo, F_filename,
         F_nextId, F_settings, F_settingsRoot=F_settings+3, F_unknRootA, F_unknRootC=F_unknRootA+3, F_unknRootD, F_unknUnicodeD, F_unknUnicodeE
       };
  //! constructor
  explicit RootCParser(RagTime5ClusterManager &parser)
    : ClusterParser(parser, 0, "ClustRoot")
    , m_cluster(new RagTime5ClusterManager::ClusterRoot)
    , m_what(-1)
    , m_linkId(-1)
    , m_fieldName("")

    , m_expectedIdToType()
    , m_idStack()
  {
  }
  //! destructor
  ~RootCParser() final;
  //! set a data id type
  void setExpectedType(int id, int type)
  {
    m_expectedIdToType[id]=type;
    m_idStack.push(id);
  }
  /** returns to new zone to parse. -1: means no preference, 0: means first zone, ... */
  int getNewZoneToParse() final
  {
    if (m_idStack.empty())
      return -1;
    int id=m_idStack.top();
    m_idStack.pop();
    return id;
  }
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    if (m_dataId==0) {
      if (m_cluster->m_dataLink.empty())
        m_cluster->m_dataLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::endZone: oops the main link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
    }
    else if (m_what==3)
      m_cluster->m_graphicTypeLink=m_link;
    else {
      bool ok=true;
      switch (m_linkId) {
      case 0:
        ok=m_cluster->m_listClusterName.empty();
        m_cluster->m_listClusterName=RagTime5ClusterManager::NameLink(m_link);
        break;
      case 1:
        ok=m_cluster->m_docInfoLink.empty();
        m_cluster->m_docInfoLink=m_link;
        break;
      case 2:
        ok=m_cluster->m_linkUnknown.empty();
        m_cluster->m_linkUnknown=m_link;
        break;
      case 3:
        m_cluster->m_settingLinks.push_back(m_link);
        break;
      case 4:
        ok=m_cluster->m_functionNameLink.empty();
        m_cluster->m_functionNameLink=m_link;
        break;
      case 5:
      case 6:
      case 7:
        ok=m_cluster->m_listClusterLink[m_linkId-5].empty();
        m_cluster->m_listClusterLink[m_linkId-5]=m_link;
        break;
      case 8:
        ok=m_cluster->m_listUnicodeLink.empty();
        m_cluster->m_listUnicodeLink=m_link;
        break;
      default:
        m_cluster->m_linksList.push_back(m_link);
      }
      if (!ok) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::endZone: oops  link %d is already set\n", m_linkId));
      }
    }
  }

  //! parse the header zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_what=m_linkId=-1;
    m_fieldName="";
    if (m_dataId==0)
      return parseHeaderZone(input, fSz, N, flag, f);
    if (isANameHeader(N)) {
      auto const &it=m_expectedIdToType.find(m_dataId);
      if (it==m_expectedIdToType.end() || it->second!=F_filename)  {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseZone: expected n seems bad\n"));
        f << "###expected,";
      }
      else
        f << "[F" << m_dataId << "]";
      f << "fileName,";
      m_fieldName="filename";
      m_what=1;
      return true;
    }
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseZone: expected N value\n"));
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
    switch (m_what) {
    case 0: // main
      if (field.m_type==RagTime5StructManager::Field::T_ZoneId && field.m_fileType==0x14510b7) {
        if (field.m_longValue[0]) {
          m_cluster->m_styleClusterIds[7]=static_cast<int>(field.m_longValue[0]);
          f << "col/pattern[id]=dataA" << field.m_longValue[0] << ",";
        }
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x3c057) {
        for (auto const &id : field.m_longList)
          f << "unkn0=" << id << ",";  // small number between 8 and 10
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x1451025) {
        f << "decal=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017) {
            // can be very long, seems to contain more 0 than 1
            f << "unkn1="<<child.m_extra << ",";
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected decal child[main]\n"));
          f << "###[" << child << "],";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected child[main]\n"));
      f << "###" << field << ",";
      break;
    case 1: // filename
      if (field.m_type==RagTime5StructManager::Field::T_Unicode && field.m_fileType==0xc8042) {
        m_cluster->m_fileName=field.m_string.cstr();
        f << field.m_string.cstr();
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected filename field\n"));
      f << "###" << field << ",";
      break;
    case 2: // list
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else if (val>1000)
            f << std::hex << val << std::dec << ",";
          else
            f << val << ",";
        }
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected list link field\n"));
      f << "###" << field << ",";
      break;
    case 3: // graph type
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14eb015) {
        f << "decal=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList) {
              if (val==0)
                f << "_,";
              else if (val>1000)
                f << std::hex << val << std::dec << ",";
              else
                f << val << ",";
            }
            m_link.m_longList=child.m_longList;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected decal child[graphType]\n"));
          f << "###[" << child << "],";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected graph type field\n"));
      f << "###" << field << ",";
      break;
    case 4:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x154f017) {
        f << "values=["; // find 1,1,2
        for (auto val : field.m_longList) f << val << ",";
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected child[formulaLink]\n"));
      f << "###" << field << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected field\n"));
      f << "###" << field << ",";
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
    if (flag!=0x10) f << "fl=" << std::hex << flag << std::dec << ",";
    m_link.m_N=N;
    switch (expected) {
    case F_docInfo:
    case F_formulaLink:
    case F_unknRootA:
    case F_unknRootA+1:
    case F_unknRootA+2:
    case F_clusterList:
    case F_clusterList+1:
    case F_clusterList+2:
    case F_functionName:
    case F_settings:
    case F_settings+1:
    case F_settings+2:
    case F_unknRootC:
    case F_unknUnicodeD:
    case F_unknUnicodeE: {
      std::string mess;
      long linkValues[4];
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        return true;
      }

      m_what=2;
      f << m_link << "," << mess;
      long expectedFileType1=0, expectedFieldSize=0;
      if ((expected==F_settings||expected==F_unknRootA) && m_link.m_fileType[0]==0x3e800)
        m_link.m_name= expected==F_unknRootA ? "unknRootA0" : "settingList0";
      else if ((expected==F_settings+1 || expected==F_clusterList+1) && m_link.m_fileType[0]==0x35800)
        m_link.m_name= expected==F_settings+1 ? "settingList1" : "nameIdToPos";
      else if (expected==F_settings+2 && m_link.m_fileType[0]==0x47040) {
        m_linkId=3;
        m_link.m_name="settings";
      }
      else if ((expected==F_formulaLink||expected==F_unknRootA+2) && fSz==30) {
        expectedFileType1=0;
        expectedFieldSize=4;
        m_linkId=expected==F_formulaLink ? 6 : 7;
        m_link.m_name=expected==F_formulaLink?"formulaLink" : "unknRootA2";
      }
      else if (expected==F_unknRootA+1 && m_link.m_fileType[0]==0x35800) {
        m_link.m_name="unknRootA1";
      }
      else if (expected==F_clusterList && fSz==32) {
        if (linkValues[0]!=0x7d01a) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: unexpected type for zone[name]\n"));
          f << "##fileType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        }
        m_linkId=0;
        expectedFileType1=0x200;
        m_link.m_name="names[cluster]";
      }
      else if (expected==F_clusterList+2 && fSz==30) {
        if (linkValues[0]) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: bad expected linkValues for cluster list id\n"));
          f << "##linkValues,";
        }
        if ((m_link.m_fileType[1]&0xFFD7)!=0x40 || m_link.m_fieldSize!=8) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: find odd definition for cluster list id\n"));
          f << "##[" << std::hex << m_link.m_fileType[1] << std::dec << ":" << m_link.m_fieldSize << "],";
        }
        m_cluster->m_listClusterId=m_link.m_ids[0];
        m_link=RagTime5ClusterManager::Link();
        m_link.m_name="clusterList";
      }
      else if (expected==F_functionName && fSz==32) {
        m_linkId=4;
        m_link.m_name="functionName";
      }
      else if (expected==F_docInfo && fSz==32) {
        m_linkId=1;
        expectedFileType1=0x8010;
        m_link.m_name="docInfo";
      }
      else if (expected==F_unknRootC && fSz==32) {
        m_linkId=2;
        expectedFileType1=0xc010;
        m_link.m_name="rootUnknC"; // a list, but never find any data
      }
      else if (expected==F_unknUnicodeD && fSz==32) {
        m_what=2;
        m_linkId=8;
        expectedFileType1=0x310;
        m_link.m_name="rootUnicodeLst";
      }
      else if (expected==F_unknUnicodeE && fSz==32) {
        // checkme an unicode string ?
        m_what=2;
        expectedFileType1=0x200;
        m_link.m_name="rootUnicodeLst";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        f << m_link.m_name << ",";
        m_fieldName=m_link.m_name;
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_settingsRoot: {
      if (fSz<38) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: unexpected data of size for setting root\n"));
        f << "##fSz,";
        return true;
      }
      m_fieldName="settings[root]";
      f << "settings[root],";
      int val=static_cast<int>(input->readULong(4));
      if (val!=0x47040) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: unexpected data of for setting root\n"));
        f << "##fileType=" << std::hex << val << std::dec << ",";
      }
      for (int i=0; i<6; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if ((val&0xFFD7)!=0x10) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: unexpected fileType1 for setting roots\n"));
        f << "##fileType1=" << std::hex << val << std::dec << ",";
      }
      f << "ids=[";
      for (int i=0; i<3; ++i) { // small int, often with f1=f0+1, f2=f1+1
        val=static_cast<int>(input->readULong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        setExpectedType(val-1,F_settings+i);
        f << "F" << val-1 << ",";
      }
      f << "],";
      val=static_cast<int>(input->readULong(2)); // always 0?
      if (val) f << "f0=" << val << ",";
      return true;
    }
    case F_nextId:
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected zone\n"));
      f << "###";
      break;
    }
    if (fSz<4) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseField: find unexpected short zone\n"));
      f << "###fSz";
      return true;
    }
    // linked data size=26|32|52|78
    int val=static_cast<int>(input->readULong(4)); // small number or 0: related to the number in cluster list?
    if (val) {
      setExpectedType(val-1,F_nextId);
      f << "next[id]=F" << val-1 << ",";
    }
    switch (fSz) {
    case 26:
      m_fieldName="graphPrefs";
      m_link.m_fileType[0]=input->readULong(4);
      if (m_link.m_fileType[0]!=0x14b4042) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: unexpected type for block1a\n"));
        f << "##fileType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
      }
      for (int i=0; i<6; ++i) {
        val=static_cast<int>(input->readLong(2));
        int const expectedV[]= {0,0,0,0,12,0};
        if (val==expectedV[i])
          continue;
        if (i==2)
          f << "grid[start]=" << val << ",";
        else if (i==4)
          f << "grid[sep]=" << val << ",";
        else
          f << "f" << i << "=" << val << ",";
      }
      break;
    case 30:
      val=static_cast<int>(input->readULong(4));
      if (val==0x15e5042) {
        // first near n=9, second near n=15 with no other data
        // no auxilliar data expected
        m_fieldName="unknDataD";
        for (int i=0; i<4; ++i) { // f0, f3: small number
          val=static_cast<int>(input->readULong(4));
          if (!val) continue;
          if (i==3) {
            setExpectedType(val-1,F_unknUnicodeD);
            f << "unicode=F" << val-1 << ",";
          }
          else
            f << "f" << i << "=" << val << ",";
        }
        break;
      }
      f << "###fUnknD";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: the field unknown d bad\n"));
      break;
    case 52:
      m_what=3;
      m_fieldName="graphTypes";
      if (N!=1) f << "##N=" << N << ",";
      m_link.m_fileType[0]=input->readULong(4);
      if (m_link.m_fileType[0]!=0x14e6042) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone[graph]: find unexpected fileType\n"));
        f << "###fileType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
      }
      for (int i=0; i<14; ++i) { // g1=0-2, g2=10[size?], g4=1-8[N], g13=30
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      if (RagTime5StructManager::readDataIdList(input, 2, m_link.m_ids) && m_link.m_ids[1]) {
        m_link.m_fileType[1]=0x30;
        m_link.m_fieldSize=16;
      }
      val=static_cast<int>(input->readLong(2));
      if (val) // small number
        f << "h0=" << val << ",";
      break;
    case 78: {
      m_what=4;
      m_fieldName="formulaLink";
      if (N!=1) f << "##N=" << N << ",";
      auto type=input->readULong(4);
      if (type!=0x154a042) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: find odd type for fSz=78\n"));
        f << "##[" << RagTime5ClusterManager::printType(type) << ":" << m_link.m_fieldSize << "],";
      }
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      std::vector<int> listIds;
      long actPos=input->tell();
      if (!RagTime5StructManager::readDataIdList(input, 2, listIds)) {
        f << "###fieldId,";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: can not read field ids\n"));
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      else if (listIds[0] || listIds[1]) { // formuladef and formulapos
        RagTime5ClusterManager::Link formulaLink;
        formulaLink.m_type= RagTime5ClusterManager::Link::L_ClusterLink;
        formulaLink.m_ids=listIds;
        m_cluster->m_formulaLink=formulaLink;
        f << "buttons," << formulaLink << ",";
      }
      val=static_cast<int>(input->readULong(4));
      if (val) {
        setExpectedType(val-1,F_formulaLink);
        f << "clusterLink=F" << val-1 << ",";
      }
      for (int i=0; i<4; ++i) { // always 0
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << "f" << i+2 << "=" << val << ",";
      }
      for (int i=0; i<2; ++i) { // always 1,0
        val=static_cast<int>(input->readULong(1));
        if (val!=1-i)
          f << "fl" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(2));
      if (val!=100)
        f << "f6=" << val << ",";
      f << "marg?=[";
      for (int i=0; i<2; ++i) {
        actPos=input->tell();
        double res;
        bool isNan;
        if (input->readDouble8(res, isNan)) // typically 0.01
          f << res << ",";
        else {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: can not read a double\n"));
          f << "##double,";
          input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
        }
        f << ",";
      }
      f << "],";
      listIds.clear();
      actPos=input->tell();
      if (!RagTime5StructManager::readDataIdList(input, 4, listIds)) {
        f << "###clusterCId,";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: can not read clusterC ids\n"));
        input->seek(actPos+16, librevenge::RVNG_SEEK_SET);
      }
      else if (listIds[0] || listIds[1] || listIds[2] || listIds[3]) {
        RagTime5ClusterManager::Link fieldLink;
        fieldLink.m_type= RagTime5ClusterManager::Link::L_UnknownClusterC;
        fieldLink.m_ids=listIds;
        m_cluster->m_linksList.push_back(fieldLink);
        f << fieldLink << ",";
      }

      val=static_cast<int>(input->readULong(4));
      if (val) {
        setExpectedType(val-1,F_functionName);
        f << "functionName=F" << val-1 << ",";
      }
      break;
    }
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseDataZone: find unexpected data field\n"));
      f << "###N=" << N << ",fSz=" << fSz << ",";
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
    if (N!=-2 || m_dataId!=0 || (fSz!=215 && fSz!=220)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    m_what=0;
    auto val=static_cast<int>(input->readLong(4)); // 8|9|a
    if (val) {
      setExpectedType(val-1,F_nextId);
      f << "next[id]=F" << val-1 << ",";
    }
    for (int i=0; i<4; ++i) { // f2=0-7, f3=1|3
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+2 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(4)); // 7|8
    setExpectedType(val-1,F_filename);
    f << "filename=F" << val-1 << ",";
    std::vector<int> listIds;
    long actPos=input->tell();
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds) || !listIds[0]) {
      f << "###cluster[child],";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseHeaderZone: can not find the cluster's child\n"));
      input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
    }
    else { // link to unknown cluster zone
      m_cluster->m_clusterIds[0]=listIds[0];
      f << "unknClustB=data" << listIds[0] << "A,";
    }
    for (int i=0; i<18; ++i) { // always g0=g11=g16=16, other 0 ?
      val=static_cast<int>(input->readLong((i==12 || i==13 || i==14) ? 4 : 2));
      if (!val) continue;
      if (i>=12 && i<=14) {
        setExpectedType(val-1,F_unknRootA+(i-12));
        f << "unknRootA" << i-12 << "=F" << val-1 << ",";
      }
      else
        f << "g" << i << "=" << val << ",";
    }
    auto type=input->readULong(4);
    if (type!=0x3c052)
      f << "#fileType=" << RagTime5ClusterManager::printType(type) << ",";
    for (int i=0; i<9; ++i) { // always h6=6
      val=static_cast<int>(input->readLong(2));
      if (val) f << "h" << i << "=" << val << ",";
    }
    for (int i=0; i<3; ++i) { // can be 1,11,10
      val=static_cast<int>(input->readULong(1));
      if (val)
        f << "fl" << i << "=" << std::hex << val << std::dec << ",";
    }
    if (fSz==220) {
      for (int i=0; i<2; ++i) { // h10=1, h11=16
        val=static_cast<int>(input->readLong(2));
        if (val) f << "h" << i+9 << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(1));
      if (val) f << "h11=" << val << ",";
    }
    val=static_cast<int>(input->readLong(4)); // e-5a
    if (val) f << "N2=" << val << ",";
    for (int i=0; i<9; ++i) { // j8=18
      val=static_cast<int>(input->readLong(2));
      if (val) f << "j" << i << "=" << val << ",";
    }
    for (int i=0; i<3; ++i) {
      val=static_cast<int>(input->readLong(4));
      setExpectedType(val-1,F_clusterList+i);
      if (val!=i+2)
        f << "cluster" << i << "=[F" << val-1 << "],";
    }
    actPos=input->tell();
    listIds.clear();
    if (!RagTime5StructManager::readDataIdList(input, 4, listIds)) {
      f << "###style[child],";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseHeaderZone: can not find the style's child\n"));
      input->seek(actPos+16, librevenge::RVNG_SEEK_SET);
    }
    else {
      for (size_t i=0; i<4; ++i) {
        if (listIds[i]==0) continue;
        m_cluster->m_styleClusterIds[i]=listIds[i];
        static char const *wh[]= { "graph", "units", "units2", "text" };
        f << wh[i] << "Style=data" << listIds[i] << "A,";
      }
    }
    val=static_cast<int>(input->readLong(4)); // always 5?
    if (val) {
      setExpectedType(val-1,F_settingsRoot);
      f << "settings[root]=F" << val-1 << ",";
    }
    actPos=input->tell();
    listIds.clear();
    if (!RagTime5StructManager::readDataIdList(input, 3, listIds)) {
      f << "###style[child],";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseHeaderZone: can not find the style2's child\n"));
      input->seek(actPos+12, librevenge::RVNG_SEEK_SET);
    }
    else {
      for (size_t i=0; i<3; ++i) {
        if (listIds[i]==0) continue;
        m_cluster->m_styleClusterIds[i+4]=listIds[i];
        static char const *wh[]= { "format", "#unk", "graphColor" };
        f << wh[i] << "Style=data" << listIds[i] << "A,";
      }
    }
    for (int i=0; i<6; ++i) { // k6=0|6, k7=0|7
      val=static_cast<int>(input->readULong(4)); // maybe some dim
      static int const expected[]= {0xc000, 0x2665, 0xc000, 0x2665, 0xc000, 0xc000};
      if (val==expected[i]) continue;
      f << "k" << i << "=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<3; ++i) {
      val=static_cast<int>(input->readULong(4)); // maybe some dim
      if (!val) continue;
      int const what[]= { F_docInfo, F_unknRootC, F_unknRootD };
      setExpectedType(val-1,what[i]);
      if (i==2) {
        f << "##";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseHeaderZone: find unknown root D node\n"));
      }
      char const *wh[]= {"docInfo", "unknRootC", "unknRootD" };
      f << wh[i] << "=F" << val-1 << ",";
    }
    for (int i=0; i<2; ++i) { // l0=0|1|2, l1=0|1
      val=static_cast<int>(input->readLong(2)); // 0|1|2
      if (val)
        f << "l" << i <<"=" << val << ",";
    }
    // a very big number
    f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2)); // 0|1|2
      if (val)
        f << "l" << i+2 <<"=" << val << ",";
    }
    val=static_cast<int>(input->readULong(4)); // maybe some dim
    if (val) {
      setExpectedType(val-1,F_unknUnicodeE);
      f << "unknUnicodeE=F" << val-1 << ",";
    }
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2)); // 0|1|2
      if (val)
        f << "l" << i+4 <<"=" << val << ",";
    }
    return true;
  }
  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::ClusterRoot> m_cluster;
  //! a index to know which field is parsed :  0: main, 1: filename, 2: list, 3: graph type, 4: fieldList
  int m_what;
  //! the link id : 0: zone[names], 1: field5=doc[info]?, 2: field6, 3: settings, 4: function names, 5: cluster[list], 6: a def cluster list, 7: a list of unicode string?
  int m_linkId;
  //! the actual field name
  std::string m_fieldName;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
};

RootCParser::~RootCParser()
{
}

//
//! try to read a basic root child cluster: either fielddef or fieldpos or a first internal child of the root (unknown) or another child
//
struct RootChildCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  RootChildCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustCRoot_BAD")
    , m_cluster(new RagTime5ClusterManager::Cluster(RagTime5ClusterManager::Cluster::C_Unknown))
  {
    switch (type) {
    case 0x10000:
      m_name="ClustGObjProp";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ClusterGProp;
      break;
    case 0x20000:
      m_name="ClustFormula_Def";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_FormulaDef;
      break;
    case 0x20001:
      m_name="ClustFormula_Pos";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_FormulaPos;
      break;
    case 0x30000:
      m_name="ClustUnkC_A";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ClusterC;
      break;
    case 0x30001:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootChildCParser::RootChildCParser: find zone ClustUnkC_B\n"));
      m_name="ClustUnkC_B";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ClusterC;
      break;
    case 0x30002:
      m_name="ClustUnkC_C";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ClusterC;
      break;
    case 0x30003:
      m_name="ClustUnkC_D";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ClusterC;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootChildCParser::RootChildCParser: find unknown type\n"));
      break;
    }
  }
  //! destructor
  ~RootChildCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    if ((m_dataId==0 && flag!=0x30) || (m_dataId==1 && flag !=0x30))
      f << "fl=" << std::hex << flag << std::dec << ",";
    bool ok=false;
    unsigned long expectedFileType1=0;
    switch (m_type) {
    case 0x10000:
      ok=m_dataId==0 && fSz==32;
      expectedFileType1=0x4010;
      break;
    case 0x20000:
      ok=m_dataId==0 && fSz==41;
      expectedFileType1=0x1010;
      break;
    case 0x20001:
      ok=m_dataId==0 && fSz==32;
      expectedFileType1=0x1010;
      break;
    case 0x30000:
      ok=m_dataId==0 && fSz==34;
      expectedFileType1=0x50;
      break;
    case 0x30002:
      if (m_dataId==0 && fSz==40) {
        ok=true;
        expectedFileType1=0x8010;
      }
      else if (m_dataId==1 && fSz==30) {
        ok=true;
        expectedFileType1=0x50;
      }
      break;
    case 0x30003:
      ok=m_dataId==0 && fSz==32;
      expectedFileType1=0x310;
      break;
    default:
      break;
    }
    if (N<=0 || !ok) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootChildCParser::parseZone: find unexpected header\n"));
      f << "###type" << std::hex << N << std::dec;
      return true;
    }

    int val;
    m_link.m_N=N;
    long linkValues[4]; // for type=0x30002, f0=3c|60, for fixed size f0=54, other 0
    std::string mess;
    if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootChildCParser::parseZone: can not read the link\n"));
      f << "###link";
      return true;
    }
    m_link.m_fileType[0]=static_cast<unsigned long>(m_type < 0x30000 ? m_type : m_type-0x30000);
    f << m_link << "," << mess;
    if (expectedFileType1>0 && (m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
      f << "###fileType1,";
    }

    if (m_type==0x20000) {
      std::vector<int> listIds;
      bool hasCluster=false;
      if (RagTime5StructManager::readDataIdList(input, 1, listIds) && listIds[0]) {
        m_cluster->m_clusterIdsList.push_back(listIds[0]);
        f << "sheet=" << getClusterDebugName(listIds[0]) << ",";
        hasCluster=true;
      }
      val=static_cast<int>(input->readLong(1));
      if ((hasCluster && val!=1) || (!hasCluster && val))
        f << "#hasCluster=" << val << ",";
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
    }
    else if (m_type==0x30000) {
      for (int i=0; i<2; ++i) { // find 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
    }
    else if (m_type==0x30002) {
      for (int i=0; i<2; ++i) { // find 0
        val=static_cast<int>(input->readLong(4));
        if (val) f << "g" << i << "=" << val << ",";
      }
    }
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (m_dataId==0 && field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
      f << "pos=[";
      for (auto val : field.m_longList)
        f << val << ",";
      f << "],";
      m_link.m_longList=field.m_longList;
    }
    else if (m_dataId==0 && field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017)
      // pos find 2|4|8
      // def find f801|000f00
      f << "unkn="<<field.m_extra << ",";
    else {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootChildCParser::parseField: find unexpected sub field\n"));
      f << "#" << field;
    }
    return true;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    if (m_dataId==0)
      m_cluster->m_dataLink=m_link;
    else
      m_cluster->m_linksList.push_back(m_link);
  }
protected:
  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> m_cluster;
};

RootChildCParser::~RootChildCParser()
{
}

//
//! low level: parser of group cluster : zone 4010
//
struct GroupCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  explicit GroupCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustGroup")
    , m_cluster(new RagTime5ClusterManager::Cluster(RagTime5ClusterManager::Cluster::C_GroupZone))
    , m_fieldName("")
  {
  }
  //! destructor
  ~GroupCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! try to parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    f << "fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="";

    int val;
    if (N!=-5) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: unexpected header\n"));
      f << "##N=" << N << ",";
      return true;
    }
    if (fSz!=50 || m_dataId!=0) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: find unknown block\n"));
      f << "###unknown,";
      return true;
    }

    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val!=m_type) {
      f << "###type=" << std::hex << val << std::dec << ",";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: the field format seems bad\n"));
    }
    for (int i=0; i<4; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    int fl1=static_cast<int>(input->readLong(2));
    if (fl1!=0x10)
      f << "fl1=" << std::hex << fl1 << std::dec << ",";
    m_link.m_N=int(input->readLong(4));
    std::string mess;
    long linkValues[4];
    if (!readLinkHeader(input, 28, m_link, linkValues, mess)) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: can not read the int link\n"));
      f << "###";
    }
    f << m_link << mess;
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    switch (m_dataId) {
    case 0: {
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x17db015) {
        f << "ids=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList)
              f << val << ",";
            m_link.m_longList=child.m_longList;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: find unexpected child[main]\n"));
          f << "###[" << child << "],";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: find unexpected field[main]\n"));
      f << "###" << field;
      break;
    }
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::parseZone: find unexpected list link field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    if (m_dataId==0) {
      if (m_cluster->m_dataLink.empty())
        m_cluster->m_dataLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::GroupCParser::endZone: oops the main link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
    }
  }
protected:
  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> m_cluster;
  //! the actual field name
  std::string m_fieldName;
};

GroupCParser::~GroupCParser()
{
}

//! the sound cluster ( 2/a/4002/400a zone)
struct ClusterSound final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterSound()
    : Cluster(C_Sound)
  {
  }
  //! destructor
  ~ClusterSound() final;
};

ClusterSound::~ClusterSound()
{
}

//
//! low level: parser of sound cluster : zone 2,a,4002,400a
//
struct SoundCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_nextId, F_parentList };
  //! constructor
  SoundCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustSound")
    , m_cluster(new ClusterSound)
    , m_fieldName("")

    , m_expectedIdToType()
    , m_idStack()
  {
    m_cluster->m_type=RagTime5ClusterManager::Cluster::C_Sound;
  }
  //! destructor
  ~SoundCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }

  //! return the sound cluster
  std::shared_ptr<ClusterSound> getSoundCluster()
  {
    return m_cluster;
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
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    if (N==-5)
      return parseHeaderZone(input, fSz, N, flag, f);

    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected!=-1)
      f << "[F" << m_dataId << "]";
    if (flag!=0x10)
      f << "fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="";
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseZone: find unexpected data block\n"));
      f << "###N=" << N << ",";
      return true;
    }
    m_link.m_N=N;
    long linkValues[4];
    std::string mess;
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseZone: find unexpected field[%d]\n", m_dataId));
      f << "###";
    }

    switch (fSz) {
    case 36:
      f << "parentListA,";
      if (!readLinkHeader(input,fSz,m_link,linkValues,mess)) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseZone: can not read the link\n"));
        f << "###link,";
        return true;
      }
      if ((m_link.m_fileType[1]&0xFFD7)!= 0x10) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::RootCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1,";
      }
      setExpectedType(m_dataId, F_parentList);
      m_fieldName=m_link.m_name="parentList";
      f << m_link << "," << mess;
      for (int i=0; i<2; ++i) { // g0: small number between 38 and 64, g1: 0|-1
        int val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseZone: find unknown size[%ld]\n", fSz));
      f << "###fSz=" << fSz << ",";
      break;
    }
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (expected) {
    case F_parentList:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2 (can be first data)
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_nextId:
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseField: find unexpected field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    // script size 38
    f << "header,fl=" << std::hex << flag << std::dec << ",";
    if (N!=-5 || m_dataId!=0 || fSz!=38) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    int val;
    m_fieldName="main";
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(int(val-1), F_nextId); // either next[id] or parentList
      f << "next[id]=F" << val-1 << ",";
    }
    for (int i=0; i<6; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    std::string code(""); // find betr
    for (int i=0; i<4; ++i) code+=char(input->readULong(1));
    if (!code.empty()) f << code << ",";
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
    return true;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected==F_parentList)
      m_cluster->m_parentLink=m_link;
    else {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::SoundCParser::parseHeaderZone: unexpected link\n"));
      m_cluster->m_linksList.push_back(m_link);
    }
  }

protected:
  //! the current cluster
  std::shared_ptr<ClusterSound> m_cluster;
  //! the actual field name
  std::string m_fieldName;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
};

SoundCParser::~SoundCParser()
{
}

//
//! low level: parser of style cluster : zone 480
//
struct StyleCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  explicit StyleCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustStyle")
    , m_cluster(new RagTime5ClusterManager::Cluster(RagTime5ClusterManager::Cluster::C_Unknown))
    , m_fieldName("")
  {
  }
  //! destructor
  ~StyleCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! try to parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    f << "fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="";

    int val;
    if (N!=-5) {
      if (N<0 || m_dataId==0 || (fSz!=28 && fSz!=32 && fSz!=36)) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: unexpected header\n"));
        f << "##N=" << N << ",";
        return true;
      }
      m_link.m_N=N;
      if (fSz==28 || fSz==32) { // n=2,3 with fSz=28, type=0x3e800, can have no data
        if ((fSz==28 && m_dataId!=2 && m_dataId!=3) || (fSz==32 && m_dataId!=4))  {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: dataId seems bad\n"));
          f << "##n=" << m_dataId << ",";
        }
        long linkValues[4];
        std::string mess;
        if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: link seems bad\n"));
          f << "###link,";
          return true;
        }
        f << m_link << "," << mess;
        if (m_link.m_fileType[0]==0x35800)
          m_fieldName="unicodeList1";
        else if (m_link.m_fileType[0]==0x3e800)
          m_fieldName="unicodeList0";
        else if (fSz==32) {
          m_fieldName="unicodeNames";
          m_cluster->m_nameLink.m_N=m_link.m_N;
          m_cluster->m_nameLink.m_ids=m_link.m_ids;
        }
        else {
          f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: the field 2,3 type seems bad\n"));
          return true;
        }
        m_link.m_name=m_fieldName;
        if (m_dataId>=2 && m_dataId<=3)
          m_cluster->m_nameLink.m_posToNamesLinks[m_dataId-2]=m_link;
        if ((m_link.m_fileType[1]&0xFFD7)!=(fSz==28 ? 0 : 0x200)) {
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
          f << "###fileType1,";
        }
        if (!m_fieldName.empty())
          f << m_fieldName << ",";
        return true;
      }
      if (m_dataId!=1) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: dataId seems bad\n"));
        f << "##n=" << m_dataId << ",";
      }
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      m_link.m_fileType[0]=input->readULong(4);
      if (m_link.m_fileType[0]!=0x7d01a) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: the field 1 type seems bad\n"));
      }
      for (int i=0; i<4; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+2 << "=" << val << ",";
      }
      m_link.m_fileType[1]=input->readULong(2);
      if (m_link.m_fileType[1]!=0x10 && m_link.m_fileType[1]!=0x18) {
        f << "###fType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: the field 1 type1 seems bad\n"));
      }
      for (int i=0; i<3; ++i) { // always 3,4,5 ?
        val=static_cast<int>(input->readLong(4));
        if (val!=i+3) f << "g" << i << "=" << val << ",";
      }
      return true;
    }
    if ((fSz!=22 && fSz!=58 && fSz!=64 && fSz!=66 && fSz!=68) || m_dataId!=0) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: find unknown block\n"));
      f << "###unknown,";
      return true;
    }

    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val!=m_type) {
      f << "###type=" << std::hex << val << std::dec << ",";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: the field format seems bad\n"));
    }
    m_link.m_N=val;
    for (int i=0; i< (fSz==22 ? 4 : 13); ++i) { // g3=2, g4=10, g6 and g8 2 small int
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      if (i==6)
        f << "N=" << val << ",";
      else
        f << "g" << i << "=" << val << ",";
    }
    if (fSz==22)
      return true;
    m_link.m_fileType[0]=input->readULong(4);
    if (m_link.m_fileType[0] != 0x01473857 && m_link.m_fileType[0] != 0x0146e827) {
      f << "###fileType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: the field type seems bad\n"));
    }
    m_link.m_fileType[1]=input->readULong(2); // c018|c030|c038 or type ?
    if (!RagTime5StructManager::readDataIdList(input, 2, m_link.m_ids) || m_link.m_ids[1]==0) {
      f << "###noData,";
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseZone: can not find any data\n"));
    }
    m_link.m_type=RagTime5ClusterManager::Link::L_FieldsList;
    if (fSz==58) {
      if (m_link.m_fileType[0] == 0x0146e827) {
        m_link.m_name=m_fieldName="formats";
        m_cluster->m_type=RagTime5ClusterManager::Cluster::C_FormatStyles;
      }
      else {
        m_link.m_name=m_fieldName="units";
        m_cluster->m_type=RagTime5ClusterManager::Cluster::C_UnitStyles;
      }
    }
    else if (fSz==64) {
      m_link.m_name=m_fieldName="graphColor";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ColorStyles;
    }
    else if (fSz==66) {
      m_link.m_name=m_fieldName="textStyle";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_TextStyles;
    }
    else {
      m_link.m_name=m_fieldName="graphStyle";
      m_cluster->m_type=RagTime5ClusterManager::Cluster::C_GraphicStyles;
    }
    f << m_link << ",";
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    switch (m_dataId) {
    case 0: {
      unsigned long expectedVal=m_cluster->m_type==RagTime5ClusterManager::Cluster::C_FormatStyles ? 0x146e815 : 0x1473815;
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==expectedVal) {
        f << "decal=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList)
              f << val << ",";
            m_link.m_longList=child.m_longList;
            continue;
          }
          if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017) {
            // a list of small int 0104|0110|22f8ffff7f3f
            f << "unkn0=" << child.m_longValue[0] << child.m_extra << ",";
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected child[main]\n"));
          f << "###[" << child << "],";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected field[main]\n"));
      f << "###" << field;
      break;
    }
    case 1:
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2 (can be first data)
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected field[zone1]\n"));
      f << "###" << field;
      break;
    case 2:
    case 3:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "data=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else if (static_cast<int>(val)==static_cast<int>(0x80000000))
            f << "inf,";
          else
            f << val << ",";
        }
        f << "],";
        m_cluster->m_nameLink.m_posToNames[m_dataId-2]=field.m_longList;
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected field[zone23–\n"));
      f << "###" << field;
      break;
    case 4:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "data=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_cluster->m_nameLink.m_decalList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2 (can be first data)
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected unicode field\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    if (m_dataId==0) {
      if (m_cluster->m_dataLink.empty())
        m_cluster->m_dataLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ClusterManagerInternal::StyleCParser::endZone: oops the main link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
    }
  }
protected:
  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> m_cluster;
  //! the actual field name
  std::string m_fieldName;
};

StyleCParser::~StyleCParser()
{
}

//
//! low level: parser of unknown cluster
//
struct UnknownCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  UnknownCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustUnknown")
    , m_cluster(new RagTime5ClusterManager::Cluster(RagTime5ClusterManager::Cluster::C_Unknown))
  {
    if (type==-1)
      return;
  }
  //! destructor
  ~UnknownCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
protected:

  //! the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> m_cluster;
};

UnknownCParser::~UnknownCParser()
{
}

}

bool RagTime5ClusterManager::getClusterBasicHeaderInfo(RagTime5Zone &zone, long &N, long &fSz, long &debHeaderPos)
{
  MWAWEntry const &entry=zone.m_entry;
  if (entry.length()<13) return false;
  MWAWInputStreamPtr input=zone.getInput();
  long endPos=entry.end();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(entry.begin()+8, librevenge::RVNG_SEEK_SET);
  long endDataPos;
  if (!readFieldHeader(zone, endPos, "", endDataPos) || !RagTime5StructManager::readCompressedLong(input, endDataPos, fSz) ||
      fSz<6 || input->tell()+fSz>endDataPos) {
    input->setReadInverted(false);
    return false;
  }
  input->seek(2, librevenge::RVNG_SEEK_CUR); // skip flag
  N=static_cast<int>(input->readLong(4));
  debHeaderPos=input->tell();
  input->setReadInverted(false);
  return true;
}

int RagTime5ClusterManager::getClusterType(RagTime5Zone &zone, int fileType)
{
  if (fileType==-1)
    fileType=getClusterFileType(zone);
  if (fileType==-1)
    return -1;
  switch (fileType&0xfff3fd7) {
  case 0: // root
    return 0;
  case 1: // layout
    return 1;
  // case 0x2: button/sound
  // case 0x3: text/spreadsheet/picture
  case 0x10:
    return 0x40007;
  case 0x42: // color pattern
  case 0x142: // v6 file
    return 0x42;
  case 0x104: // pipeline
  case 0x204:
    return 0x104;
  case 0x480: // style
  case 0x4c0:
    return 0x80;
  case 0x10000: // first child of root
  case 0x20000: // formula def
  case 0x20001: // formula pos
  case 0x30000: // 0th element of child list root
  case 0x30001: // 1th element of child list root, never seen
  case 0x30002: // 2th element of child list root
  case 0x30003: // 3th element of child list root
  case 0x40000: // picture cluster
  case 0x40001: // graphic cluster
  case 0x40002: // spreadsheet cluster
  case 0x40003: // text cluster
  case 0x40004: // chart
  case 0x40005: // button
  case 0x40006: // sound
  case 0x40007: // group
    return fileType;
  default:
    break;
  }
  long N, fSz, debDataPos;
  if (!getClusterBasicHeaderInfo(zone, N, fSz, debDataPos) || N!=-5)
    return -1;

  if ((fileType&0xfff1fd7)!=2 && (fileType&0xfff1fd7)!=3 && fileType!=0xe && fileType!=0x16a) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::getClusterType: unexpected filetype=%x\n", (unsigned)fileType));
  }
  switch (fSz) {
  case 38: // sound cluster
    return 0x40006;
  case 50: // group
    return 0x40007;
  case 74: // button cluster
    return 0x40005;
  case 64: // movie cluster
  case 104:
  case 109: // picture cluster
    return 0x40000;
  case 118: // graphic cluster
    return 0x40001;
  case 134: // spreadsheet cluster
    return 0x40002;
  case 135:
  case 140:
  case 143:
  case 208:
  case 212:
  case 213:
  case 216: // text cluster
    return 0x40003;
  case 331:
  case 339: // chart cluster
    return 0x40004;
  default: // unknown
    break;
  }
  return -1;
}

int RagTime5ClusterManager::getClusterFileType(RagTime5Zone &zone)
{
  long N, fSz, debDataPos;
  if (!getClusterBasicHeaderInfo(zone, N, fSz, debDataPos))
    return -1;
  int res=-1;

  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  switch (N) {
  case -2:
    res=0;
    break;
  case -5:
    input->seek(debDataPos+6, librevenge::RVNG_SEEK_SET); // skip id, ...
    res=static_cast<int>(input->readULong(2));
    break;
  default:
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::getClusterFileType: unexpected N value\n"));
      break;
    }
    if (fSz==0x20) {
      input->seek(debDataPos+16, librevenge::RVNG_SEEK_SET);
      auto fieldType=static_cast<int>(input->readULong(2));
      if ((fieldType&0xFFD7)==0x1010)
        res=0x20001;
      else if ((fieldType&0xFFD7)==0x310)
        res=0x30003;
      else if ((fieldType&0xFFD7)==0x4010)
        res=0x10000;
      else {
        MWAW_DEBUG_MSG(("RagTime5ClusterManager::getClusterFileType: unexpected field type %x\n", unsigned(fieldType)));
      }
    }
    else if (fSz==0x22)
      res=0x30000;
    else if (fSz==0x28)
      res=0x30002;
    else if (fSz==0x29)
      res=0x20000;
    else {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::getClusterFileType: unexpected fSz=%ld\n", fSz));
    }
    break;
  }
  input->setReadInverted(false);
  return res;
}

bool RagTime5ClusterManager::readClusterGObjProperties(RagTime5Zone &zone)
{
  MWAWEntry entry=zone.m_entry;
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  MWAWInputStreamPtr input=zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian);
  long debPos=entry.begin();
  long endPos=entry.end();
  zone.m_isParsed=true;
  f.str("");
  f << "Entries(ClustCGObjProp)[" << zone << "]:";

  input->seek(debPos, librevenge::RVNG_SEEK_SET);
  if (input->readULong(4)==0x5a610600) {  // rare, 3 can be good in one file and 1 bad, so...
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterGObjProperties: endian seems bad, reverts it\n"));
    input->setReadInverted(zone.m_hiLoEndian);
    f << "##badEndian,";
  }
  ascFile.addPos(debPos);
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(endPos);
  ascFile.addNote("_");

  input->seek(debPos, librevenge::RVNG_SEEK_SET);
  RagTime5StructManager::GObjPropFieldParser parser("ClustCGObjProp");
  m_document.readStructData(zone, endPos, 0, -1, parser, librevenge::RVNGString("ClustCGObjProp"));

  long pos=input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterGObjProperties: find extra data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("ClustCGObjProp:##extra");
  }
  input->setReadInverted(false);
  return true;
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5ClusterManager::readRootCluster(RagTime5Zone &zone)
{
  RagTime5ClusterManagerInternal::RootCParser parser(*this);
  if (readCluster(zone, parser))
    return parser.getCluster();
  return std::shared_ptr<Cluster>();
}

bool RagTime5ClusterManager::readCluster(RagTime5Zone &zone, std::shared_ptr<RagTime5ClusterManager::Cluster> &cluster, int zoneType)
{
  cluster.reset();
  int zType=-1;
  if (m_state->m_idToClusterInfoMap.find(zone.m_ids[0])!=m_state->m_idToClusterInfoMap.end()) {
    auto const &info=m_state->m_idToClusterInfoMap.find(zone.m_ids[0])->second;
    zoneType=info.m_fileType;
    zType=info.m_type;
  }
  if (zoneType==-1)
    zoneType=getClusterFileType(zone);
  if (zType==-1)
    zType=getClusterType(zone, zoneType);

  std::shared_ptr<ClusterParser> parser;
  switch (zType) {
  case 0:
    cluster=readRootCluster(zone);
    break;
  case 0x1:
    cluster=m_document.readLayoutCluster(zone, zoneType);
    break;
  case 0x42:
    parser.reset(new RagTime5ClusterManagerInternal::ColPatCParser(*this));
    break;
  case 0x80:
    parser.reset(new RagTime5ClusterManagerInternal::StyleCParser(*this, zoneType));
    break;
  case 0x104:
    cluster=m_document.readPipelineCluster(zone, zoneType);
    break;
  case 0x10000: // first child of root
  case 0x20000: // field def
  case 0x20001: // field pos
  case 0x30000: // 0th element of child list root
  case 0x30001: // 1th element of child list root, never seen
  case 0x30002: // 2th element of child list root
  case 0x30003: // 3th element of child list root
    parser.reset(new RagTime5ClusterManagerInternal::RootChildCParser(*this, zType));
    break;
  case 0x40000:
    cluster=m_document.readPictureCluster(zone, zoneType);
    break;
  case 0x40001:
    cluster=m_document.readGraphicCluster(zone, zoneType);
    break;
  case 0x40002:
    cluster=m_document.readSpreadsheetCluster(zone, zoneType);
    break;
  case 0x40003:
    cluster=m_document.readTextCluster(zone, zoneType);
    break;
  case 0x40004:
    cluster=m_document.readChartCluster(zone, zoneType);
    break;
  case 0x40005:
    cluster=m_document.readButtonCluster(zone, zoneType);
    break;
  case 0x40006: {
    RagTime5ClusterManagerInternal::SoundCParser soundParser(*this, zoneType);
    if (readCluster(zone, soundParser) && soundParser.getSoundCluster()) {
      auto sound=soundParser.getSoundCluster();
      std::vector<RagTime5StructManager::ZoneLink> listCluster;
      m_document.readClusterLinkList(sound->m_parentLink, listCluster, "SoundClustLst");
      cluster=soundParser.getCluster();
    }
    break;
  }
  case 0x40007: {
    RagTime5ClusterManagerInternal::GroupCParser groupParser(*this, zoneType);
    if (readCluster(zone, groupParser) && groupParser.getCluster()) {
      cluster=groupParser.getCluster();
      // each group is associated with a list of id, maybe the list of ids corresponding to a type?
      if (!cluster->m_dataLink.empty()) {
        cluster->m_dataLink.m_name="groupUnknownLst";
        std::vector<long> listIds;
        m_document.readLongList(cluster->m_dataLink, listIds);
      }
    }
    break;
  }
  default:
    if (!zone.m_entry.valid()) { // rare, but can append; maybe some deleted cluster
      cluster.reset(new Cluster(Cluster::C_Empty));
      cluster->m_hiLoEndian=zone.m_hiLoEndian;
      break;
    }
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: can not find cluster type, use default parser\n"));
    parser.reset(new RagTime5ClusterManagerInternal::UnknownCParser(*this, zoneType));
    break;
  }
  bool ok=cluster.get() != nullptr;
  if (!ok && parser) {
    ok=readCluster(zone, *parser) && parser->getCluster();
    cluster=parser->getCluster();
  }
  else if (!ok) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: can not find any parser\n"));
  }
  if (!ok)
    return false;

  // check the level2 child
  libmwaw::DebugFile &mainAscii=m_document.ascii();
  for (auto cIt : zone.m_childIdToZoneMap) {
    auto child=cIt.second;
    if (!child) continue;
    child->m_isParsed=true;
    switch (cIt.first) {
    case 8:
      if (child->m_variableD[0] || (child->m_variableD[1]<=0&&cluster->m_type!=Cluster::C_Empty) || child->m_entry.valid()) {
        MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: refCount seems odd\n"));
        mainAscii.addPos(child->m_defPosition);
        mainAscii.addNote("Cluster[child]###");
      }
      break;
    default:
      if (child->m_entry.valid() && readClusterGObjProperties(*child))
        break;
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: find unknown child zone\n"));
      mainAscii.addPos(child->m_defPosition);
      mainAscii.addNote("Cluster[child]###");
      break;
    }
  }

  if (m_state->m_idToClusterMap.find(zone.m_ids[0])!=m_state->m_idToClusterMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readCluster: a cluster for zone %d already exists\n", zone.m_ids[0]));
  }
  else
    m_state->m_idToClusterMap[zone.m_ids[0]]=cluster;
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
