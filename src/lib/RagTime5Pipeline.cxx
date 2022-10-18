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

#include "RagTime5Pipeline.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Pipeline */
namespace RagTime5PipelineInternal
{
//! the pipeline cluster ( 4001 zone)
struct ClusterPipeline final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterPipeline()
    : Cluster(C_Pipeline)
    , m_dataId(0)
    , m_masterId(0)
    , m_layoutId(0)
    , m_data2Link()
  {
  }
  //! destructor
  ~ClusterPipeline() final;
  //! the data id
  int m_dataId;
  //! the master id
  int m_masterId;
  //! the layout id
  int m_layoutId;
  //! the second data link(rare)
  RagTime5ClusterManager::Link m_data2Link;
};

ClusterPipeline::~ClusterPipeline()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Pipeline
struct State {
  //! constructor
  State()
    : m_idPipelineMap()
  {
  }
  //! map data id to text zone
  std::map<int, std::shared_ptr<ClusterPipeline> > m_idPipelineMap;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Pipeline::RagTime5Pipeline(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5PipelineInternal::State)
{
}

RagTime5Pipeline::~RagTime5Pipeline()
{
}

int RagTime5Pipeline::version() const
{
  return m_parserState->m_version;
}

bool RagTime5Pipeline::send(int pipelineId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId, double totalWidth)
{
  if (m_state->m_idPipelineMap.find(pipelineId)==m_state->m_idPipelineMap.end() ||
      !m_state->m_idPipelineMap.find(pipelineId)->second) {
    MWAW_DEBUG_MSG(("RagTime5Pipeline::send: can not find container for pipeline %d\n", pipelineId));
    return false;
  }
  int dataId=m_state->m_idPipelineMap.find(pipelineId)->second->m_dataId;
  if (dataId==0)
    return true;
  return m_document.send(dataId, listener, pos, partId, 0, totalWidth);
}

RagTime5ClusterManager::Cluster::Type  RagTime5Pipeline::getContainerType(int pipelineId) const
{
  if (m_state->m_idPipelineMap.find(pipelineId)==m_state->m_idPipelineMap.end() ||
      !m_state->m_idPipelineMap.find(pipelineId)->second) {
    MWAW_DEBUG_MSG(("RagTime5Pipeline::getContainerType: can not find container for pipeline %d\n", pipelineId));
    return RagTime5ClusterManager::Cluster::C_Unknown;
  }
  int dataId=m_state->m_idPipelineMap.find(pipelineId)->second->m_dataId;
  if (dataId==0) // rare, but can happens
    return RagTime5ClusterManager::Cluster::C_Unknown;
  return m_document.getClusterType(dataId);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// interface send function
////////////////////////////////////////////////////////////
void RagTime5Pipeline::flushExtra()
{
  MWAW_DEBUG_MSG(("RagTime5Pipeline::flushExtra: not implemented\n"));
}

////////////////////////////////////////////////////////////
// cluster parser
////////////////////////////////////////////////////////////

namespace RagTime5PipelineInternal
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
    if (m_fieldSize<56) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::ClustListParser: bad field size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~ClustListParser() final;
  //! return the cluster name
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }
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
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    // find only cluster with one field
    long pos=input->tell();
    if (endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    RagTime5StructManager::ZoneLink link;
    link.m_dataId=listIds[0];
    if (listIds[0])
      f << getClusterDebugName(listIds[0]) << ",";
    link.m_subZoneId[0]=long(input->readULong(4)); // subId[3]
    f << link;
    float dim[2];
    for (auto &d : dim) d=float(input->readULong(4))/65536.f;
    f << "dim=" << MWAWVec2f(dim[0],dim[1]) << ",";
    int val;
    f << "unkn=[";
    for (int i=0; i<8; ++i) {
      val=static_cast<int>(input->readLong(2));
      if (val) f << val << ",";
      else f << "_,";
    }
    f << "],";
    for (int i=0; i<12; ++i) { // always 0
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
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

//! Internal: the helper to read a unknown
struct UnknownParser final : public RagTime5StructManager::DataParser {
  //! constructor
  UnknownParser(int fieldSize, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_fieldSize(fieldSize)
  {
    if (m_fieldSize<12) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::UnknownParser: bad field size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~UnknownParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::UnknownParser::parse: bad data size\n"));
      return false;
    }
    for (int i=0; i<6; ++i) { // f0=1, f1=5|6, f2=1|2, f5=0|1
      auto val=static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    return true;
  }

  //! the field size
  int m_fieldSize;
private:
  //! copy constructor, not implemented
  UnknownParser(UnknownParser &orig) = delete;
  //! copy operator, not implemented
  UnknownParser &operator=(UnknownParser &orig) = delete;
};

UnknownParser::~UnknownParser()
{
}

//
//! try to read a pipeline cluster: 104,204,4104, 4204
//
struct PipelineCParser final : public RagTime5ClusterManager::ClusterParser {
  //! constructor
  PipelineCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustPipeline")
    , m_cluster(new ClusterPipeline)
  {
  }
  //! destructor
  ~PipelineCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the pipeline cluster
  std::shared_ptr<ClusterPipeline> getPipelineCluster()
  {
    return m_cluster;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x146c015) {
      f << "unkn0=[";
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017) { // find 2
          f << child << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseField: find unexpected child\n"));
        f << "##[" << child << "],";
      }
      f << "],";
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseField: find unknow field\n"));
      f << "##[" << field << "],";
    }
    return true;
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    if (flag!=0x31)
      f << "fl=" << std::hex << flag << std::dec << ",";
    if (m_dataId || N!=-5) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: find unexpected header\n"));
      f << "###type" << std::hex << N << std::dec;
      return true;
    }
    if (fSz!=76 && fSz!=110) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: find unexpected file size\n"));
      f << "###fSz=" << fSz << ",";
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
    if (val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: the zone type seems odd\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 0?
    if (val) f << "f4=" << val << ",";
    for (int i=0; i<7; ++i) { // g1, g2, g3 small int other 0
      val=static_cast<int>(input->readLong(4));
      if (i==2)
        m_link.m_N=val;
      else if (val) f << "g" << i << "=" << val << ",";
    }
    m_link.m_fileType[1]=input->readULong(2);
    m_link.m_fieldSize=static_cast<int>(input->readULong(2));

    std::vector<int> listIds;
    long actPos=input->tell();
    if (!RagTime5StructManager::readDataIdList(input, 2, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: can not read the first list id\n"));
      f << "##listIds,";
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
    }
    else {
      if (listIds[0]) {
        m_link.m_ids.push_back(listIds[0]);
        m_cluster->m_parentLink=m_link;
        f << "parent[list]=data" << listIds[0] << "A,";
      }
      if (listIds[1]) { // the object corresponding to the pipeline
        m_cluster->m_dataId=listIds[1];
        f << "data[id]=" << getClusterDebugName(listIds[1]) << ",";
      }
    }
    unsigned long ulVal=input->readULong(4);
    if (ulVal) {
      f << "h0=" << (ulVal&0x7FFFFFFF);
      if (ulVal&0x80000000) f << "[h],";
      else f << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 1?
    if (val!=1) f << "h1=" << val << ",";
    listIds.clear();
    if (!RagTime5StructManager::readDataIdList(input, 2, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: can not read the cluster list id\n"));
      f << "##listClusterIds,";
      return true;
    }
    if (listIds[0]) { // find some master layout and some master pipeline
      m_cluster->m_masterId=listIds[0];
      f << "id[master]=" << getClusterDebugName(listIds[0]) << ",";
    }
    if (listIds[1]) { // find always layout
      m_cluster->m_layoutId=listIds[1];
      f << "id[layout]=" << getClusterDebugName(listIds[1]) << ",";
    }
    val=static_cast<int>(input->readULong(2)); // 2[08a][01]
    f << "fl=" << std::hex << val << std::dec << ",";
    for (int i=0; i<2; ++i) { // h2=0|4|a, h3=small number
      val=static_cast<int>(input->readLong(2));
      if (val) f << "h" << i+2 << "=" << val << ",";
    }
    if (fSz==76) return true;

    for (int i=0; i<7; ++i) { // g1, g2, g3 small int other 0
      val=static_cast<int>(input->readLong(i==0 ? 2 : 4));
      if (i==2)
        m_link.m_N=val;
      else if (val) f << "g" << i << "=" << val << ",";
    }
    m_link.m_fileType[1]=input->readULong(2);
    m_link.m_fieldSize=static_cast<int>(input->readULong(2));

    listIds.clear();
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5PipelineInternal::PipelineCParser::parseZone: can not read the second list id\n"));
      f << "##listIds2,";
      return true;
    }
    if (listIds[0]) {
      m_link.m_ids.clear();
      m_link.m_ids.push_back(listIds[0]);
      m_cluster->m_data2Link=m_link;
      f << "data2=data" << listIds[0] << "A,";
    }
    return true;
  }
protected:
  //! the current cluster
  std::shared_ptr<ClusterPipeline> m_cluster;
};

PipelineCParser::~PipelineCParser()
{
}

}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Pipeline::readPipelineCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Pipeline::readPipelineCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5PipelineInternal::PipelineCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getPipelineCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Pipeline::readPipelineCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }

  auto cluster=parser.getPipelineCluster();
  if (cluster->m_parentLink.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readClusterPipelineData: can not find the parent zone\n"));
  }
  else {
    RagTime5PipelineInternal::ClustListParser linkParser(*clusterManager, cluster->m_parentLink.m_fieldSize, "PipelineParent");
    m_document.readFixedSizeZone(cluster->m_parentLink, linkParser);
    m_document.checkClusterList(linkParser.m_linkList);
  }

  if (!cluster->m_data2Link.empty()) {
    RagTime5PipelineInternal::UnknownParser data2Parser(cluster->m_data2Link.m_fieldSize, "PipelineUnknown");
    m_document.readFixedSizeZone(cluster->m_data2Link, data2Parser);
  }

  if (m_state->m_idPipelineMap.find(zone.m_ids[0])!=m_state->m_idPipelineMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Pipeline::readPipelineCluster: cluster %d already exists\n", zone.m_ids[0]));
  }
  else
    m_state->m_idPipelineMap[zone.m_ids[0]]=cluster;
  return cluster;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
