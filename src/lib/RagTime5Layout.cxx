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
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5ClusterManager.hxx"
#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5StyleManager.hxx"

#include "RagTime5Layout.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Layout */
namespace RagTime5LayoutInternal
{
//! the layout cluster ( 4001 zone)
struct ClusterLayout final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterLayout()
    : Cluster(C_Layout)
    , m_pageList()
    , m_pageNameLink()
    , m_pipelineLink()
    , m_pageDataLink()
    , m_childIdSet()
    , m_numChild(0)
    , m_numMasterChild(0)
  {
  }
  //! destructor
  ~ClusterLayout() final;

  //! a zone of in a layout
  struct Zone {
    //! constructor
    Zone()
      : m_mainId(0)
      , m_masterId(0)
      , m_dimension()
    {
    }
    //! the main zone id
    int m_mainId;
    //! the master zone id or 0
    int m_masterId;
    //! the dimension
    MWAWVec2f m_dimension;
  };

  //! list of zone's
  std::vector<Zone> m_pageList;
  //! the name link for page?
  RagTime5ClusterManager::NameLink m_pageNameLink;
  //! link to a pipeline cluster list
  RagTime5ClusterManager::Link m_pipelineLink;
  //! link to  a zone of fieldSize 8(unknown)
  RagTime5ClusterManager::Link m_pageDataLink;
  //! list of child id
  std::set<int> m_childIdSet;
  //! the number of classic child
  int m_numChild;
  //! the number of master child
  int m_numMasterChild;
};

ClusterLayout::~ClusterLayout()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Layout
struct State {
  //! constructor
  State()
    : m_numPages(-1)
    , m_idLayoutMap()
    , m_masterIdSet()
    , m_layoutIdToSendList()
  {
  }
  //! the number of pages
  int m_numPages;
  //! map data id to text zone
  std::map<int, std::shared_ptr<ClusterLayout> > m_idLayoutMap;
  //! the list of master id
  std::set<int> m_masterIdSet;
  //! the list of layout to send
  std::vector<int> m_layoutIdToSendList;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Layout::RagTime5Layout(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5LayoutInternal::State)
{
}

RagTime5Layout::~RagTime5Layout()
{
}

int RagTime5Layout::version() const
{
  return m_parserState->m_version;
}

int RagTime5Layout::numPages() const
{
  if (m_state->m_numPages<0)
    const_cast<RagTime5Layout *>(this)->updateLayouts();
  return m_state->m_numPages;
}

bool RagTime5Layout::sendPageContents()
{
  int page=0;
  for (size_t i=0; i<m_state->m_layoutIdToSendList.size(); ++i) {
    int lId=m_state->m_layoutIdToSendList[i];
    if (m_state->m_idLayoutMap.find(lId)==m_state->m_idLayoutMap.end() || !m_state->m_idLayoutMap.find(lId)->second) {
      MWAW_DEBUG_MSG(("RagTime5Layout::sendPageContents: can not find layout %d\n", lId));
      continue;
    }
    auto &layout=*m_state->m_idLayoutMap.find(lId)->second;
    layout.m_isSent=true;
    for (auto &zone : layout.m_pageList) {
      MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(100,100), librevenge::RVNG_POINT);
      position.m_anchorTo=MWAWPosition::Page;
      position.setPage(++page);
      if (zone.m_masterId) {
        if (m_state->m_idLayoutMap.find(zone.m_masterId)==m_state->m_idLayoutMap.end() || !m_state->m_idLayoutMap.find(zone.m_masterId)->second) {
          MWAW_DEBUG_MSG(("RagTime5Layout::sendPageContents: can not find layout %d\n", zone.m_masterId));
        }
        else {
          auto &master=*m_state->m_idLayoutMap.find(zone.m_masterId)->second;
          int cId=0;
          size_t n=layout.m_pageList.size();
          if (master.m_pageList.size()==1)
            cId=master.m_pageList[0].m_mainId;
          else if (n<master.m_pageList.size())
            cId=master.m_pageList[n].m_mainId;
          if (cId)
            m_document.send(cId, MWAWListenerPtr(), position);
        }
      }
      if (zone.m_mainId)
        m_document.send(zone.m_mainId, MWAWListenerPtr(), position);
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
void RagTime5Layout::updateLayouts()
{
  for (auto it : m_state->m_idLayoutMap) {
    if (!it.second) continue;
    updateLayout(*it.second);
  }
  // look for no master layout
  int nPages=0;
  std::multimap<int,int> numZonesToLayout;
  for (auto it : m_state->m_idLayoutMap) {
    if (!it.second || it.second->m_pageList.empty() || m_state->m_masterIdSet.find(it.first)!=m_state->m_masterIdSet.end()) {
      if (it.second)
        it.second->m_isSent=true;
      continue;
    }
    numZonesToLayout.insert(std::multimap<int,int>::value_type(it.second->m_numChild,it.first));
    nPages+=static_cast<int>(it.second->m_pageList.size());
  }
  m_state->m_numPages=nPages;

  for (auto lIt=numZonesToLayout.rbegin(); lIt!=numZonesToLayout.rend(); ++lIt)
    m_state->m_layoutIdToSendList.push_back(lIt->second);
}

void RagTime5Layout::updateLayout(RagTime5LayoutInternal::ClusterLayout &layout)
{
  int numChild=0, numMasterChild=0;
  for (auto &zone : layout.m_pageList) {
    if (zone.m_mainId) {
      if (m_document.getClusterType(zone.m_mainId)==RagTime5ClusterManager::Cluster::C_GraphicZone)
        ++numChild;
      else {
        MWAW_DEBUG_MSG(("RagTime5Layout::updateLayout: find unexpected type for cluster %d\n", zone.m_mainId));
        zone.m_mainId=0;
      }
    }
    if (zone.m_masterId) {
      if (m_document.getClusterType(zone.m_masterId)==RagTime5ClusterManager::Cluster::C_Layout) {
        m_state->m_masterIdSet.insert(zone.m_masterId);
        ++numMasterChild;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5Layout::updateLayout: find unexpected type for cluster %d\n", zone.m_masterId));
        zone.m_masterId=0;
      }
    }
  }
  layout.m_numChild=numChild;
  layout.m_numMasterChild=numMasterChild;
}


////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// interface send function
////////////////////////////////////////////////////////////
void RagTime5Layout::flushExtra()
{
  MWAW_DEBUG_MSG(("RagTime5Layout::flushExtra: not implemented\n"));
}

bool RagTime5Layout::send(RagTime5LayoutInternal::ClusterLayout &/*cluster*/, MWAWListenerPtr listener, int /*page*/)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Layout::send: can not find the listener\n"));
    return false;
  }
  static bool first=true;
  if (first) {
    first=false;
    MWAW_DEBUG_MSG(("RagTime5Layout::send: sorry not implemented\n"));
  }
  return true;
}

////////////////////////////////////////////////////////////
// cluster parser
////////////////////////////////////////////////////////////

namespace RagTime5LayoutInternal
{
//! Internal: the helper to read a clustList
struct ClustListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  ClustListParser(RagTime5ClusterManager &clusterManager, int fieldSize, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_fieldSize(fieldSize)
    , m_linkList()
    , m_clusterManager(clusterManager)
  {
    if (m_fieldSize<4) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::ClustListParser: bad field size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~ClustListParser() final;
  //! returns the not null list dataId list
  std::vector<int> getIdList() const
  {
    std::vector<int> res;
    for (auto const &lnk : m_linkList) {
      if (lnk.m_dataId>0)
        res.push_back(lnk.m_dataId);
    }
    return res;
  }
  //! return the cluster name
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }

  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    RagTime5StructManager::ZoneLink link;
    link.m_dataId=listIds[0];
    if (listIds[0])
      f << getClusterDebugName(listIds[0]) << ",";
    f << link;
    m_linkList.push_back(link);
    return true;
  }

  //! the field size
  int m_fieldSize;
  //! the list of read cluster
  std::vector<RagTime5StructManager::ZoneLink> m_linkList;
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

//! Internal: the helper to read a extra page data
struct PageDataParser final : public RagTime5StructManager::DataParser {
  //! constructor
  PageDataParser(int fieldSize, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_fieldSize(fieldSize)
  {
    if (m_fieldSize<8) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::PageDataParser: bad field size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~PageDataParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::PageDataParser::parse: bad data size\n"));
      return false;
    }
    auto val=input->readLong(4);
    f << "id1=" << val << ",";
    for (int i=0; i<2; ++i) { // f0=0|4|8|a, f1: the page number?
      val=input->readLong(2);
      if (val) f << "f" << i << "=" << val << ",";
    }
    return true;
  }

  //! the field size
  int m_fieldSize;
private:
  //! copy constructor, not implemented
  PageDataParser(PageDataParser &orig) = delete;
  //! copy operator, not implemented
  PageDataParser &operator=(PageDataParser &orig) = delete;
};

PageDataParser::~PageDataParser()
{
}

//
//! low level: parser of layout cluster
//
struct LayoutCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_page, F_pageData0, F_pageData1, F_pageData2, F_pipeline, F_name, F_nextId=F_name+3, F_settingsDef, F_settings};
  //! constructor
  LayoutCParser(RagTime5ClusterManager &parser, int zoneType)
    : ClusterParser(parser, zoneType, "ClustLayout"), m_cluster(new ClusterLayout)
    , m_numPages(0)
    , m_what(-1)
    , m_linkId(-1)
    , m_fieldName("")

    , m_actualZone()

    , m_expectedIdToType()
    , m_idStack()
  {
  }
  //! destructor
  ~LayoutCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the current layout cluster
  std::shared_ptr<ClusterLayout> getLayoutCluster()
  {
    return m_cluster;
  }
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
  //! start a new zone
  void startZone() final
  {
    if (m_what<=0)
      ++m_what;
    else if (m_what==1) {
      if (m_dataId>=m_numPages+1)
        ++m_what;
      m_actualZone=ClusterLayout::Zone();
    }
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    switch (m_linkId) {
    case 0:
      m_cluster->m_pageDataLink=m_link;
      break;
    case 1:
      m_cluster->m_pipelineLink=m_link;
      break;
    case 2:
      m_cluster->m_settingLinks.push_back(m_link);
      break;
    case 3:
      if (m_cluster->m_pageNameLink.empty())
        m_cluster->m_pageNameLink=RagTime5ClusterManager::NameLink(m_link);
      else {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::endZone: oops the name link is already set\n"));
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
    m_linkId=-1;
    m_link.m_N=N;
    if (m_dataId==0)
      return parseHeaderZone(input,fSz,N,flag,f);
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected!=-1)
      f << "[F" << m_dataId << "]";
    int val;
    switch (expected) {
    case F_name:
    case F_name+1:
    case F_name+2:
    case F_pageData0:
    case F_pipeline:
    case F_settings:
    case F_settings+1:
    case F_settings+2: {
      std::string mess;
      long linkValues[4];
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Layout::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      m_what=4;
      long expectedFileType1=0;
      if ((expected==F_name || expected==F_settings) && m_link.m_fileType[0]==0x3e800)
        m_link.m_name=expected==F_settings ? "settingsList0" : "unicodeList0";
      else if ((expected==F_name+1 || expected==F_settings+1) && m_link.m_fileType[0]==0x35800)
        m_link.m_name=expected==F_settings+1 ? "settingsList1" : "unicodeList1";
      else if (expected==F_name+2 && m_link.m_fileType[0]==0) { // fSz==32
        expectedFileType1=0x200;
        m_linkId=3;
        m_link.m_name="unicodeNames";
      }
      else if (expected==F_settings+2 && m_link.m_fileType[0]==0x47040) {
        m_linkId=2;
        m_link.m_name="settings";
      }
      else if (expected==F_pageData0 && m_link.m_fileType[0]==0x14b9800) { // fSz=30
        m_linkId=0;
        m_what=3;
        m_link.m_name="layoutPageData0";
        expectedFileType1=0x10;
      }
      else if (expected==F_pipeline && m_link.m_fileType[0]==0) { // fSz=30
        m_linkId=1;
        m_link.m_name="pipeline";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        m_fieldName=m_link.m_name;
        f << m_link.m_name << ",";
      }
      if (expected==F_name||expected==F_name+1)
        m_cluster->m_pageNameLink.m_posToNamesLinks[expected-F_name]=m_link;
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      return true;
    }
    case F_page:
      return parsePageZone(input,fSz,N,flag,f);
    case F_pageData1: {
      if (fSz<54) {
        f << "###fSz,";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      m_fieldName="layoutPageData1";
      f << m_fieldName << ",";
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      float dim[2];
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      f << "sz=" << MWAWVec2f(dim[0],dim[1]) << ",";

      std::vector<int> listIds;
      long actPos=input->tell();
      if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
        f << "###cluster1,";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: can not read cluster block[fSz=54]\n"));
        input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
      }
      else if (listIds[0]) { // link to the new cluster e zone ? (can be also 0)
        m_cluster->m_clusterIdsList.push_back(listIds[0]);
        f << "cluster0=" << getClusterDebugName(listIds[0]) << ",";
      }
      for (int i=0; i<7; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+2 << "=" << val << ",";
      }
      for (int i=0; i<9; ++i) { // g0=1, g1=1-7, g2=0-d, g3=0-1, g8=2-8
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      return true;
    }
    case F_pageData2: { // unsure
      if (fSz<60) {
        f << "###fSz,";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      m_fieldName="data2";
      m_what=5;
      for (int i=0; i<4; ++i) { // f3=1
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(4)); // 0/72cc/b840
      if (val) f << "f4=" << std::hex << val << std::dec << ",";
      auto type=input->readULong(4);
      if (type!=0x14b6842) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected filetype[fSz=60]\n"));
        f << "#fileType1=" << RagTime5Layout::printType(type) << ",";
      }
      for (int i=0; i<7; ++i) { // g4=g6=1
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      type=input->readULong(4);
      if (type!=0x35800) { // maybe a link here
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected filetype2[fSz=60]\n"));
        f << "#fileType2=" << RagTime5Layout::printType(type) << ",";
      }
      for (int i=0; i<4; ++i) { // 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "h" << i << "=" << val << ",";
      }
      type=input->readULong(4);
      if (type!=0x14b4817) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected filetype3[fSz=60]\n"));
        f << "#fileType3=" << RagTime5Layout::printType(type) << ",";
      }
      for (int i=0; i<4; ++i) { // h4=47
        val=static_cast<int>(input->readLong(2));
        if (val) f << "h" << i+4 << "=" << val << ",";
      }
      return true;
    }
    case F_settingsDef: // ok, but there are some that we do not find automatically
      if (fSz!=38) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: unexpected settings def\n"));
        f << "###,";
      }
      break;
    case F_nextId:
    default:
      break;
    }
    m_what=2;

    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: N value seems bad\n"));
      f << "###N=" << N << ",";
      return true;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: unexpected field\n"));
      f << "###";
    }
    switch (fSz) {
    case 29: {
      // checkme find where is stored next[id]
      std::string mess;
      long linkValues[4];
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Layout::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the field fSz28... type seems bad\n"));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=0;
      if (m_link.m_fileType[0]==0x3c052) {
        m_what=6;
        m_fieldName="list:longs2";
        expectedFileType1=0x50;
        // use 0cf042 to find data2
      }
      else {
        f << "###fType=" << RagTime5Layout::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: the field fSz%d... type seems bad\n", int(fSz)));
        return true;
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      break;
    }
    case 36: { // follow when it exists the fSz=30 zone, no auxilliar data
      m_fieldName="page[name]";
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto type=input->readULong(4);
      if (type!=0x7d01a) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected filetype[fSz=36]\n"));
        f << "###fileType=" << RagTime5Layout::printType(type) << ",";
      }
      val=static_cast<int>(input->readLong(4)); // 0 or small number
      if (val) {
        // TODO: find which expected type refers to next[id], can be list:longs2, settings[def], ...
        setExpectedType(val-1,F_nextId);
        f << "next[id]=F" << (val-1) << ",";
      }
      for (int i=0; i<3; ++i) { // f4=10
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+2 << "=" << val << ",";
      }
      f << "ids=[";
      for (int i=0; i<3; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        setExpectedType(val-1,F_name+i);
        f << "F" << val-1 << ",";
      }
      f << "],";
      break;
    }
    case 38: { // in 1 or 2 exemplar, no auxilliar data
      m_fieldName="settings[Def]";
      auto type=input->readULong(4);
      if (type!=0x47040) {
        MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected type[fSz=38]\n"));
        f << "##fileType=" << RagTime5Layout::printType(type) << ",";
      }
      type=input->readULong(4); // 0|1c07007|1492042
      if (type) f << "fileType1=" << RagTime5Layout::printType(type) << ",";
      val=static_cast<int>(input->readLong(4));
      if (val) {
        setExpectedType(val-1,F_nextId);
        f << "next[id]=F" << (val-1) << ",";
      }
      for (int i=0; i<3; ++i) { // f1=0|8, f4=10
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      f << "ids=[";
      for (int i=0; i<3; ++i) { // small increasing sequence
        val=static_cast<int>(input->readLong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        setExpectedType(val-1,F_settings+i);
        f << "F" << val-1 << ",";
      }
      f << "],";
      val=static_cast<int>(input->readLong(2)); // always 0
      if (val) f << "f5=" << val << ",";
      break;
    }
    default:
      f << "###fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseZone: find unexpected file size\n"));
      break;
    }
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    switch (m_what) {
    case 0: // main
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14b5815) {
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xcf042) {
            f << "ids[page]=[";
            for (auto val : child.m_longList) {
              if (val==0)
                f << "_,";
              else
                f << "F" << val-1 << ",";
            }
            // read the page in file order
            for (auto pIt=child.m_longList.rbegin(); pIt!=child.m_longList.rend(); ++pIt) {
              auto const &val=*pIt;
              if (val)
                setExpectedType(int(val-1),F_page);
            }
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected main field\n"));
          f << "###[" << child << "],";
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected main field\n"));
      f << "###" << field;
      break;
    case 3: // page data
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // rare, always 2
        f << "unkn="<< field.m_extra << ",";
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Long && field.m_fileType==0xcf817) {
        // a small value between 2a and 61
        f << "f0="<<field.m_longValue[0] << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected data0 field\n"));
      f << "###" << field;
      break;
    case 4: // list
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
        if (expected==F_name||expected==F_name+1)
          m_cluster->m_pageNameLink.m_posToNames[expected-F_name]=field.m_longList;
        else
          m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) { // with 3c052
        f << "unkn=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else
            f << val << ",";
        }
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected list field\n"));
      f << "###" << field;
      break;
    case 5: // fSz=60
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14b4815) {
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            f << "unkn=[";
            for (auto val : child.m_longList) {
              if (val==0)
                f << "_,";
              else
                f << val << ",";
            }
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected data2 field\n"));
          f << "###[" << child << "],";
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected data2 field\n"));
      f << "###" << field;
      break;
    case 6: // second list
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) { // with 3c052
        f << "unkn=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else {
            setExpectedType(int(val-1),F_pageData2);
            f << "data2=F" << val-1 << ",";
          }
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected list:long2 field\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseField: find unexpected sub field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
protected:
  //! parse a page
  bool parsePageZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    if (N<0 || m_dataId==0 || m_dataId>m_numPages || (fSz!=50 && fSz!=66)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parsePageZone: find unexpected main field\n"));
      return false;
    }
    f << "page, fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="page";
    if (N!=1) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parsePageZone: zone N seems badA\n"));
      f << "#N=" << N << ",";
    }
    int val;
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    float dim[2];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    m_actualZone.m_dimension=MWAWVec2f(dim[0],dim[1]);
    f << "sz=" << MWAWVec2f(dim[0],dim[1]) << ",";
    std::vector<int> listIds;
    long actPos=input->tell();
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      f << "###cluster0,";
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parsePageZone: can not read first cluster page\n"));
      input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
    }
    else if (listIds[0]) { // link to a cluster main zone
      m_actualZone.m_mainId=listIds[0];
      m_cluster->m_childIdSet.insert(listIds[0]);
      f << "cluster0=" << getClusterDebugName(listIds[0]) << ",";
    }
    for (int i=0; i<2; ++i) { // always 0: another item?
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    listIds.clear();
    actPos=input->tell();
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      f << "###cluster1,";
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parsePageZone: can not read second cluster page\n"));
      input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
    }
    else if (listIds[0]) { // link to the master layout
      m_actualZone.m_masterId=listIds[0];
      m_cluster->m_childIdSet.insert(listIds[0]);
      f << "cluster1=" << getClusterDebugName(listIds[0]) << ",";
    }
    for (int i=0; i<2; ++i) { // either 0,0 or 1,small number (but does not seems a data id )
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i+4 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(4)); // alwas 1?
    if (val!=1) f << "f6=" << val << ",";
    f << "unkn=[";
    for (int i=0; i<4; ++i) { // small number: hasData0?, hasSetting?, hasPageName?, ?
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << val << ",";
      else
        f << "_,";
    }
    f << "],";
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(val-1,F_settingsDef);
      f << "settingsDef=F" << val-1 << ",";
    }
    if (fSz==66) { // master page
      f << "master,";
      for (int i=0; i<2; ++i) { // 0: main, 1: master?
        val=static_cast<int>(input->readLong(4));
        if (!val) continue;
        setExpectedType(val-1,F_pageData1);
        f << "pageData1[" << i << "]=F" << val-1 << ",";
      }
      for (int i=0; i<2; ++i) { // fl0=0|1
        val=static_cast<int>(input->readLong(1));
        if (!val) continue;
        if (i==0) // 1: specified number, 2:index counted from start, 3: from end, 4: formula
          f << "usage=" << val << ",";
        else
          f << "fl" << i << "=" << val << ",";
      }
      for (int i=0; i<3; ++i) { // g4=0|1, g6=g4
        val=static_cast<int>(input->readLong(2));
        if (!val) continue;
        if (i==2)
          f << "formula=f" << val << ",";
        else
          f << "g" << i+4 << "=" << val << ",";
      }
    }
    m_cluster->m_pageList.push_back(m_actualZone);
    m_actualZone=ClusterLayout::Zone();
    return true;
  }

  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header,fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    if (N!=-5 || m_dataId!=0 || (fSz!=123 && fSz!=127 && fSz!=128 && fSz!=132)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseHeaderZone: find unexpected main field\n"));
      return false;
    }
    int val;
    for (int i=0; i<2; ++i) {
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2)); // small number, unsure what it is
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) { // f0=0, f1=4-6
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==1) {
        setExpectedType(val-1,F_nextId);
        f << "next[id]=F" << val-1 << ",";
      }
      else
        f << "f" << i+2 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 16
    if (val!=16)
      f << "f4=" << val << ",";
    m_numPages=static_cast<int>(input->readLong(4));
    if (m_numPages!=1)
      f << "num[pages]=" << m_numPages << ",";
    auto fileType=input->readULong(4);
    if (fileType!=0x14b6052) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseHeaderZone: find unexpected filetype\n"));
      f << "#fileType0=" << RagTime5Layout::printType(fileType) << ",";
    }
    for (int i=0; i<9; ++i) { // f11=0x60,
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i+5 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(1));
    if (val!=1) f << "fl=" << val << ",";
    if (fSz==128 || fSz==132) {
      for (int i=0; i<5; ++i) { // unsure find only 0 here
        val=static_cast<int>(input->readLong(1));
        if (val)
          f << "flA" << i << "=" << val << ",";
      }
    }
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(val-1,F_pageData0);
      f << "pageData0=F" << val-1 << ",";
    }
    long actPos=input->tell();
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5LayoutInternal::LayoutCParser::parseHeaderZone: can not read first cluster frame\n"));
      f << "##badCluster,";
      input->seek(actPos+4, librevenge::RVNG_SEEK_SET);
    }
    else if (listIds[0]) { // find link to a named frame cluster
      m_cluster->m_clusterIdsList.push_back(listIds[0]);
      f << "clusterId1=" << getClusterDebugName(listIds[0]) << ",";
    }
    for (int i=0; i<2; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "g" << i+1 << "=" << val << ",";
    }
    float dim[4];
    for (int i=0; i<2; ++i) dim[i]=float(input->readLong(4))/65536.f;
    MWAWVec2f frameSize(dim[0],dim[1]);
    f << "sz=" << frameSize << ",";
    for (int i=0; i<2; ++i) dim[i]=float(input->readLong(4))/65536.f;
    if (MWAWVec2f(dim[0],dim[1])!=frameSize)
      f << "sz2=" << MWAWVec2f(dim[0],dim[1]) << ",";
    auto fl=input->readULong(2);
    // checkme, only ok for mac file ?
    if (fl&1) f << "side[double],";
    if (fl&4) f << "show[grid],";
    if (fl&8) f << "tear[all,page],";
    fl &= 0xfff2;
    if (fl) f << "flB=" << std::hex << fl << std::dec << ",";
    for (int i=0; i<8; ++i) {
      val=static_cast<int>(input->readLong(i==3 ? 4 : 2));
      int const expected[]= {0,0,0,0,1,0,1,1};
      if (val==expected[i]) continue;
      if (i==0)
        f << "first[page]=" << val+1 << ",";
      else if (i==3) {
        setExpectedType(val-1,F_pipeline);
        f << "pipeline=F" << val-1 << ",";
      }
      else if (i==4)
        f << "tear[from]=" << val << ",";
      else if (i==6)
        f << "tear[to]=" << val << ",";
      else if (i==7)
        f << "page[number,format]=" << val << ",";
      else
        f << "g" << i+2 << "=" << val << ",";
    }
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    f << "dim=" << MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3])) << ",";
    for (int i=0; i<4; ++i) { // h3=0|1|3|6
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "h" << i << "=" << val << ",";
    }
    if (fSz==127 || fSz==132) {
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "h" << i+3 << "=" << val << ",";
      }
    }
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterLayout> m_cluster;
  //! the number of pages
  int m_numPages;
  //! a index to know which field is parsed :  0: main, 1:list of pages, 2: unknown, 3:data0, 4:list, 5: unknown, 6: list:longs2
  int m_what;
  //! the link id : 0: listItem, 1: pipeline, 2: settinglinks, 3: namelink,
  int m_linkId;
  //! the actual field name
  std::string m_fieldName;

  //! the actual zone
  ClusterLayout::Zone m_actualZone;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
};

LayoutCParser::~LayoutCParser()
{
}

}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Layout::readLayoutCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Layout::readLayoutCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5LayoutInternal::LayoutCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getLayoutCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Layout::readLayoutCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }

  auto cluster=parser.getLayoutCluster();

  std::map<int, librevenge::RVNGString> idToNameMap;
  if (!cluster->m_pageNameLink.empty()) {
    m_document.readUnicodeStringList(cluster->m_pageNameLink, idToNameMap);
    // update the cluster name
    for (size_t i=0; i<cluster->m_pageList.size(); ++i) {
      auto it=idToNameMap.find(int(i+1));
      if (it==idToNameMap.end() || it->second.empty()) continue;
      auto const &child=cluster->m_pageList[i];
      if (!child.m_mainId) continue;
      clusterManager->setClusterName(child.m_mainId, it->second);
    }
  }

  if (!cluster->m_pageDataLink.empty()) { // related to page
    RagTime5LayoutInternal::PageDataParser pageParser(cluster->m_pageDataLink.m_fieldSize, "LayoutPage0");
    m_document.readFixedSizeZone(cluster->m_pageDataLink, pageParser);
  }
  if (!cluster->m_pipelineLink.empty() && cluster->m_pipelineLink.m_ids.size()==1) {
    if (cluster->m_pipelineLink.m_fieldSize==4) {
      RagTime5LayoutInternal::ClustListParser listParser(*clusterManager, 4, "LayoutPipeline");
      m_document.readFixedSizeZone(cluster->m_pipelineLink, listParser);
      m_document.checkClusterList(listParser.getIdList());
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5Layout::readClusterLayoutData: find unexpected field size for pipeline data\n"));
      m_document.readFixedSizeZone(cluster->m_pipelineLink, "LayoutPipelineBAD");
    }
  }
  // can have some setting
  for (auto const &lnk : cluster->m_settingLinks) {
    if (lnk.empty()) continue;
    RagTime5StructManager::FieldParser defaultParser("Settings");
    m_document.readStructZone(lnk, defaultParser, 0);
  }

  for (auto const &lnk : cluster->m_linksList) {
    if (lnk.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(lnk);
      continue;
    }
    std::stringstream s;
    s << "Layout_Data" << lnk.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(s.str());
    m_document.readFixedSizeZone(lnk, defaultParser);
  }

  if (m_state->m_idLayoutMap.find(zone.m_ids[0])!=m_state->m_idLayoutMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Layout::readLayoutCluster: cluster %d already exists\n", zone.m_ids[0]));
  }
  else
    m_state->m_idLayoutMap[zone.m_ids[0]]=cluster;
  return cluster;
}


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
