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
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWStringStream.hxx"

#include "RagTime5Chart.hxx"
#include "RagTime5ClusterManager.hxx"
#include "RagTime5Formula.hxx"
#include "RagTime5Graph.hxx"
#include "RagTime5Layout.hxx"
#include "RagTime5Pipeline.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5StyleManager.hxx"
#include "RagTime5Spreadsheet.hxx"
#include "RagTime5Text.hxx"

#include "RagTime5Document.hxx"

/** Internal: the structures of a RagTime5Document */
namespace RagTime5DocumentInternal
{
//! Internal: the helper to read doc info parse
struct DocInfoFieldParser final : public RagTime5StructManager::FieldParser {
  //! constructor
  explicit DocInfoFieldParser(RagTime5Document &doc)
    : RagTime5StructManager::FieldParser("DocInfo")
    , m_document(doc)
  {
  }
  //! destructor
  ~DocInfoFieldParser() final;
  //! parse a field
  bool parseField(RagTime5StructManager::Field &field, RagTime5Zone &zone, int /*n*/, libmwaw::DebugStream &f) final
  {
    if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x1f7827) {
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0x32040 && child.m_entry.valid()) {
          f << child;

          long actPos=zone.getInput()->tell();
          m_document.readDocInfoClusterData(zone, child.m_entry);
          zone.getInput()->seek(actPos, librevenge::RVNG_SEEK_SET);
          return true;
        }
        MWAW_DEBUG_MSG(("RagTime5DocumentInternal::DocInfoFieldParser::parseField: find some unknown mainData block\n"));
        f << "##mainData=" << child << ",";
      }
    }
    else
      f << field;
    return true;
  }

protected:
  //! the main parser
  RagTime5Document &m_document;
};

DocInfoFieldParser::~DocInfoFieldParser()
{
}

//! Internal: the helper to read index + unicode string for a RagTime5Document
struct IndexUnicodeParser final : public RagTime5StructManager::DataParser {
  //! constructor
  IndexUnicodeParser(RagTime5Document &, bool readIndex, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_readIndex(readIndex)
    , m_idToStringMap()
    , m_indicesMap()
  {
  }
  //! destructor
  ~IndexUnicodeParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    int id=n;
    if (m_readIndex) {
      if (endPos-pos<4) {
        MWAW_DEBUG_MSG(("RagTime5DocumentInternal::IndexUnicodeParser::parse: bad data size\n"));
        return false;
      }
      id=static_cast<int>(input->readULong(4));
      f << "id=" << id << ",";
    }
    else if (!m_indicesMap.empty()) {
      auto it = m_indicesMap.find(n);
      if (it != m_indicesMap.end())
        id=it->second;
      else
        id=0;
    }
    librevenge::RVNGString str("");
    if (endPos==input->tell())
      ;
    else if (!RagTime5StructManager::readUnicodeString(input, endPos, str))
      f << "###";
    f << "\"" << str.cstr() << "\",";
    m_idToStringMap[id]=str;
    return true;
  }

  //! a flag to know if we need to read the index
  bool m_readIndex;
  //! the data
  std::map<int, librevenge::RVNGString> m_idToStringMap;
  //! the map n to index if given
  std::map<int,int> m_indicesMap;
};

IndexUnicodeParser::~IndexUnicodeParser()
{
}

//! Internal: the helper to read a clustList
struct ClustListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  ClustListParser(RagTime5ClusterManager &clusterManager, int fieldSize, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_fieldSize(fieldSize)
    , m_linkList()
    , m_idToNameMap()
    , m_clusterManager(clusterManager)
  {
    if (m_fieldSize<4) {
      MWAW_DEBUG_MSG(("RagTime5DocumentInternal::ClustListParser: bad field size\n"));
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
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (m_idToNameMap.find(n)!=m_idToNameMap.end())
      f << m_idToNameMap.find(n)->second.cstr() << ",";
    if (endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5DocumentInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5DocumentInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    RagTime5StructManager::ZoneLink link;
    link.m_dataId=listIds[0];
    if (listIds[0])
      // a e,2003,200b, ... cluster
      f << getClusterDebugName(listIds[0]) << ",";
    if (m_fieldSize>=10) {
      link.m_subZoneId[0]=long(input->readULong(4));
      link.m_subZoneId[1]=long(input->readLong(2));
    }
    f << link;
    m_linkList.push_back(link);
    return true;
  }

  //! the field size
  int m_fieldSize;
  //! the list of read cluster
  std::vector<RagTime5StructManager::ZoneLink> m_linkList;
  //! the name
  std::map<int, librevenge::RVNGString> m_idToNameMap;
private:
  //! the main zone manager
  RagTime5ClusterManager &m_clusterManager;
  //! copy constructor, not implemented
  ClustListParser(ClustListParser &orig);
  //! copy operator, not ximplemented
  ClustListParser &operator=(ClustListParser &orig);
};

ClustListParser::~ClustListParser()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Document
struct State {
  //! constructor
  State()
    : m_version(5)
    , m_zonesEntry()
    , m_zonesList()
    , m_zoneIdToTypeMap()
    , m_zoneInfo()
    , m_mainClusterId(0)
    , m_mainTypeId(0)
    , m_buttonFormulaLink()
    , m_dataIdZoneMap()
    , m_pageZonesIdMap()
    , m_sendZoneSet()
    , m_hasLayout(false)
    , m_numPages(0)
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }

  //! the document version
  int m_version;
  //! the main zone entry
  MWAWEntry m_zonesEntry;
  //! the zone list
  std::vector<std::shared_ptr<RagTime5Zone> > m_zonesList;
  //! a map id to type string
  std::map<int, std::string> m_zoneIdToTypeMap;
  //! the zone info zone (ie. the first zone)
  std::shared_ptr<RagTime5Zone> m_zoneInfo;
  //! the main cluster id
  int m_mainClusterId;
  //! the main type id
  int m_mainTypeId;
  //! the buttons formula link
  RagTime5ClusterManager::Link m_buttonFormulaLink;
  //! a map: data id->entry (datafork)
  std::map<int, std::shared_ptr<RagTime5Zone> > m_dataIdZoneMap;
  //! a map: page->main zone id
  std::map<int, std::vector<int> > m_pageZonesIdMap;
  //! a set used to avoid looping when sending zone
  std::set<int> m_sendZoneSet;
  //! a flag to know if the file has some layout
  bool m_hasLayout;
  int m_numPages /** the number of page of the final document */;

  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Document::RagTime5Document(MWAWParser &parser)
  : m_parser(&parser)
  , m_parserState(parser.getParserState())
  , m_state()
  , m_chartParser()
  , m_formulaParser()
  , m_graphParser()
  , m_layoutParser()
  , m_pipelineParser()
  , m_spreadsheetParser()
  , m_textParser()
  , m_clusterManager()
  , m_structManager()
  , m_styleManager()

  , m_newPage(nullptr)
  , m_sendFootnote(nullptr)
{
  init();
}

RagTime5Document::~RagTime5Document()
{
}

void RagTime5Document::init()
{
  m_structManager.reset(new RagTime5StructManager(*this));
  m_clusterManager.reset(new RagTime5ClusterManager(*this));
  m_styleManager.reset(new RagTime5StyleManager(*this));

  m_chartParser.reset(new RagTime5Chart(*this));
  m_formulaParser.reset(new RagTime5Formula(*this));
  m_graphParser.reset(new RagTime5Graph(*this));
  m_layoutParser.reset(new RagTime5Layout(*this));
  m_pipelineParser.reset(new RagTime5Pipeline(*this));
  m_spreadsheetParser.reset(new RagTime5Spreadsheet(*this));
  m_textParser.reset(new RagTime5Text(*this));

  m_state.reset(new RagTime5DocumentInternal::State);
}

librevenge::RVNGPropertyList RagTime5Document::getDocumentMetaData() const
{
  return librevenge::RVNGPropertyList();
}

int RagTime5Document::version() const
{
  return m_state->m_version;
}

void RagTime5Document::setVersion(int vers)
{
  m_state->m_version=vers;
}

int RagTime5Document::numPages() const
{
  if (m_state->m_numPages<=0) {
    if (m_parserState->m_kind==MWAWDocument::MWAW_K_SPREADSHEET)
      m_state->m_numPages=1;
    else {
      int nPages=m_layoutParser->numPages();
      if (nPages<=0)
        nPages=1;
      else
        m_state->m_hasLayout=true;
      m_state->m_numPages=nPages;
    }
  }
  return m_state->m_numPages;
}

void RagTime5Document::updatePageSpanList(std::vector<MWAWPageSpan> &spanList)
{
  MWAWPageSpan ps(m_parser->getPageSpan());
  ps.setPageSpan(numPages());
  spanList.push_back(ps);
}

bool RagTime5Document::sendButtonZoneAsText(MWAWListenerPtr listener, int buttonId)
{
  return m_graphParser->sendButtonZoneAsText(listener, buttonId);
}

std::shared_ptr<RagTime5ClusterManager> RagTime5Document::getClusterManager()
{
  return m_clusterManager;
}

std::shared_ptr<RagTime5StructManager> RagTime5Document::getStructManager()
{
  return m_structManager;
}

std::shared_ptr<RagTime5StyleManager> RagTime5Document::getStyleManager()
{
  return m_styleManager;
}

std::shared_ptr<RagTime5Formula> RagTime5Document::getFormulaParser()
{
  return m_formulaParser;
}

std::shared_ptr<RagTime5Graph> RagTime5Document::getGraphParser()
{
  return m_graphParser;
}

std::shared_ptr<RagTime5Spreadsheet> RagTime5Document::getSpreadsheetParser()
{
  return m_spreadsheetParser;
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readButtonCluster(RagTime5Zone &zone, int zoneType)
{
  return m_graphParser->readButtonCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readChartCluster(RagTime5Zone &zone, int zoneType)
{
  return m_chartParser->readChartCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readGraphicCluster(RagTime5Zone &zone, int zoneType)
{
  return m_graphParser->readGraphicCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readLayoutCluster(RagTime5Zone &zone, int zoneType)
{
  return m_layoutParser->readLayoutCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readPipelineCluster(RagTime5Zone &zone, int zoneType)
{
  return m_pipelineParser->readPipelineCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readPictureCluster(RagTime5Zone &zone, int zoneType)
{
  return m_graphParser->readPictureCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readSpreadsheetCluster(RagTime5Zone &zone, int zoneType)
{
  return m_spreadsheetParser->readSpreadsheetCluster(zone, zoneType);
}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Document::readTextCluster(RagTime5Zone &zone, int zoneType)
{
  return m_textParser->readTextCluster(zone, zoneType);
}

std::shared_ptr<RagTime5Zone> RagTime5Document::getDataZone(int dataId) const
{
  if (m_state->m_dataIdZoneMap.find(dataId)==m_state->m_dataIdZoneMap.end())
    return std::shared_ptr<RagTime5Zone>();
  return m_state->m_dataIdZoneMap.find(dataId)->second;
}

RagTime5ClusterManager::Cluster::Type RagTime5Document::getClusterType(int zId) const
{
  return m_clusterManager->getClusterType(zId);
}

RagTime5ClusterManager::Cluster::Type RagTime5Document::getPipelineContainerType(int pipelineId) const
{
  return m_pipelineParser->getContainerType(pipelineId);
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void RagTime5Document::newPage(int number, bool softBreak)
{
  if (!m_parser || !m_newPage) {
    MWAW_DEBUG_MSG(("RagTime5Document::newPage: can not find newPage callback\n"));
    return;
  }
  (m_parser->*m_newPage)(number, softBreak);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

bool RagTime5Document::createZones()
{
  int const vers=version();
  if (vers<5) {
    MWAW_DEBUG_MSG(("RagTime5Document::createZones: must not be called for v%d document\n", vers));
    return false;
  }

  if (m_state->m_zonesList.empty()) {
    if (!findZones(m_state->m_zonesEntry))
      return false;
    ascii().addPos(m_state->m_zonesEntry.end());
    ascii().addNote("FileHeader-End");
  }

  if (m_state->m_zonesList.size()<20) {
    // even an empty file seems to have almost ~80 zones, so...
    MWAW_DEBUG_MSG(("RagTime5Document::createZones: the zone list seems too short\n"));
    return false;
  }
  // we need to find the string's zones and update the map zoneId to string data
  m_state->m_zoneInfo=m_state->m_zonesList[0];
  if (!findZonesKind())
    return false;
  // now, we can update all the zones: kinds, input, ...
  for (size_t i=1; i<m_state->m_zonesList.size(); ++i)
    updateZone(m_state->m_zonesList[i]);

  if (!useMainZoneInfoData()) return false;

  // now, parse the formula in spreadsheet and in button
  m_spreadsheetParser->parseSpreadsheetFormulas();
  if (!m_state->m_buttonFormulaLink.empty())
    m_formulaParser->readFormulaClusters(m_state->m_buttonFormulaLink, -1);

  // check for unread clusters
  for (auto zone : m_state->m_zonesList) {
    if (!zone || zone->m_isParsed || zone->getKindLastPart(zone->m_kinds[1].empty())!="Cluster")
      continue;
    if (zone->m_entry.valid()) {
      MWAW_DEBUG_MSG(("RagTime5Document::createZones: find unparsed cluster zone %d\n", zone->m_ids[0]));
    }
    readClusterZone(*zone);
  }
  // now read the screen rep list zone: CHECKME: can we remove this check, now ?
  for (auto zone : m_state->m_zonesList) {
    if (!zone || zone->m_isParsed || (!zone->m_entry.valid()&&zone->m_variableD[0]!=1) || zone->getKindLastPart(zone->m_kinds[1].empty())!="ScreenRepList")
      continue;
    m_graphParser->readPictureList(*zone);
  }

  return true;
}

bool RagTime5Document::findZonesKind()
{
  libmwaw::DebugStream f;
  if (!m_state->m_zoneIdToTypeMap.empty())
    return true;
  for (size_t i=1; i<m_state->m_zonesList.size(); ++i) {
    if (!m_state->m_zonesList[i])
      continue;
    auto &zone=*m_state->m_zonesList[i];
    // id=0 correspond to the file header already read, so ignored it
    if (zone.m_ids[0]==0 && zone.m_level==1) {
      zone.m_isParsed=true;
      continue;
    }

    std::string what("");
    if (zone.m_idsFlag[1]!=0 || (zone.m_ids[1]!=23 && zone.m_ids[1]!=24) || zone.m_ids[2]!=21)
      continue;
    // normally a string, update the zone input(always uncompressed) and read the zone
    if (!updateZoneInput(zone) || !readString(zone, what) || what.empty())
      continue;
    if (m_state->m_zoneIdToTypeMap.find(zone.m_ids[0])!=m_state->m_zoneIdToTypeMap.end()) {
      MWAW_DEBUG_MSG(("RagTime5Document::findZonesKind: a type with id=%d already exists\n", zone.m_ids[0]));
    }
    else {
      m_state->m_zoneIdToTypeMap[zone.m_ids[0]]=what;
      f.str("");
      f << what << ",";
      ascii().addPos(zone.m_defPosition);
      ascii().addNote(f.str().c_str());
    }
  }
  return true;
}

bool RagTime5Document::parseMainZoneInfoData(RagTime5Zone const &zoneInfo)
{
  if (zoneInfo.m_isParsed)
    return true;

  zoneInfo.m_isParsed=true;
  for (auto it : zoneInfo.m_childIdToZoneMap) {
    std::shared_ptr<RagTime5Zone> zone=it.second;
    if (!zone) continue;
    zone->m_isParsed=true;
    switch (it.first) {
    case 3: // alway with gd=[1,_]
      if (zone->m_variableD[0]==1 && zone->m_variableD[1]) {
        MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: find a zone 3\n"));
        ascii().addPos(zone->m_defPosition);
        ascii().addNote("###");
      }
      break;
    case 4: // list of zones limits, safe to ignore
    case 5: // file limits, safe to ignore
      break;
    case 6: // alway with gd=[_,_]
      if (zone->m_variableD[1]) {
        MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: find a zone 6\n"));
        ascii().addPos(zone->m_defPosition);
        ascii().addNote("###");
      }
      break;
    case 10: { // the type zone
      if (zone->m_variableD[0]!=1) {
        MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: the type zone seems bads\n"));
        break;
      }
      m_state->m_mainTypeId=zone->m_variableD[1];
      break;
    }
    case 11:
      if (zone->m_variableD[0]!=1) {
        MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: the main cluster zone seems bads\n"));
        break;
      }
      m_state->m_mainClusterId=zone->m_variableD[1];
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: find unknown main zone %d\n", it.first));
      ascii().addPos(zone->m_defPosition);
      ascii().addNote("###");
      break;
    }
  }
  if (!m_state->m_mainClusterId) {
    MWAW_DEBUG_MSG(("RagTime5Document::parseMainZoneInfoData: can not find the cluster id try 13\n"));
    m_state->m_mainClusterId=13;
  }
  return true;
}

bool RagTime5Document::useMainZoneInfoData()
{
  if (!m_state->m_zoneInfo || m_state->m_zoneInfo->m_ids[0]!=1) {
    MWAW_DEBUG_MSG(("RagTime5Document::useMainZoneInfoData: can not find the zone information zone, impossible to continue\n"));
    return false;
  }
  parseMainZoneInfoData(*m_state->m_zoneInfo);

  // the type id
  if (m_state->m_mainTypeId) {
    auto dZone=getDataZone(m_state->m_mainTypeId);
    if (!dZone || !dZone->m_entry.valid()) {
      MWAW_DEBUG_MSG(("RagTime5Document::useMainZoneInfoData: can not find the type zone\n"));
    }
    else {
      if (dZone->getKindLastPart()!="ItemData" || !m_structManager->readTypeDefinitions(*dZone)) {
        MWAW_DEBUG_MSG(("RagTime5Document::useMainZoneInfoData: unexpected list of block type\n"));
      }
    }
  }
  // the main cluster
  auto dZone=getDataZone(m_state->m_mainClusterId);
  if (!dZone) {
    MWAW_DEBUG_MSG(("RagTime5Document::useMainZoneInfoData: can not find the main cluster zone\n"));
    return true;
  }
  dZone->m_extra+="main,";
  if (dZone->getKindLastPart(dZone->m_kinds[1].empty())!="Cluster" || !readClusterZone(*dZone, 0)) {
    MWAW_DEBUG_MSG(("RagTime5Document::useMainZoneInfoData: unexpected main cluster zone type\n"));
  }
  return true;
}

bool RagTime5Document::readZoneData(RagTime5Zone &zone)
{
  if (!zone.m_entry.valid()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: can not find the entry\n"));
    return false;
  }
  libmwaw::DebugStream f;
  int usedId=zone.m_kinds[1].empty() ? 0 : 1;
  std::string actType=zone.getKindLastPart(usedId==0);

  std::string kind=zone.getKindLastPart();
  // the "RagTime" string
  if (kind=="CodeName") {
    std::string what;
    if (zone.m_kinds[1]!="BESoftware:7BitASCII:Type" || !readString(zone, what)) {
      MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: can not read codename for zone %d\n", zone.m_ids[0]));
      zone.m_isParsed=true;
      f << "Entries(CodeName)[" << zone << "]:###";
      libmwaw::DebugFile &ascFile=zone.ascii();
      ascFile.addPos(zone.m_entry.begin());
      ascFile.addNote(f.str().c_str());
    }
    for (auto it : zone.m_childIdToZoneMap) {
      std::shared_ptr<RagTime5Zone> child=it.second;
      if (!child || child->m_isParsed) continue;
      if (child->getKindLastPart()=="DocuVersion" && readDocumentVersion(*child))
        continue;
      if (child->getKindLastPart()=="7BitASCII") {
        child->m_isParsed=true;
        ascii().addPos(child->m_defPosition);
        ascii().addNote("codeName[type]");
        continue;
      }
      MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find unknown child for codename for zone %d\n", zone.m_ids[0]));
      ascii().addPos(child->m_defPosition);
      ascii().addNote("###unkCodeName");
    }
    return true;
  }
  //
  // first test for picture data
  //

  // checkme: find how we can retrieve the next data without parsing unparsed data
  if (kind=="ScreenRepMatchData" || kind=="ScreenRepMatchDataColor") {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find unexpected %s for zone %d\n", kind.c_str(), zone.m_ids[0]));
    return m_graphParser->readPictureMatch(zone, kind=="ScreenRepMatchDataColor");
  }
  if (kind=="DocuVersion") {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find unexpected docuVersion\n"));
    return readDocumentVersion(zone);
  }
  if (kind=="Thumbnail")
    return m_graphParser->readPictureData(zone);
  if (m_graphParser->readPictureData(zone)) {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find some unparsed picture %d\n", zone.m_ids[0]));
    ascii().addPos(zone.m_defPosition);
    ascii().addNote("###unparsed");
    return true;
  }
  if (kind=="ScriptComment" || kind=="ScriptName") {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find unexpected %s\n", kind.c_str()));
    return readScriptComment(zone);
  }
  std::string name("");
  if (kind=="OSAScript" || kind=="TCubics") {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find unexpected %s\n", kind.c_str()));
    name=kind;
  }
  else if (kind=="ItemData" || kind=="Unicode") {
    actType=zone.getKindLastPart(zone.m_kinds[1].empty());
    if (actType=="Unicode" || kind=="Unicode") {
      // hilo/lohi is not always set, so this can cause problem....
      if (readUnicodeString(zone))
        return true;
      MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: can not read a unicode zone %d\n", zone.m_ids[0]));
      f << "Entries(StringUnicode)[" << zone << "]:###";
      zone.m_isParsed=true;
      libmwaw::DebugFile &ascFile=zone.ascii();
      ascFile.addPos(zone.m_entry.begin());
      ascFile.addNote(f.str().c_str());
      return true;
    }
    if (zone.m_entry.length()==164 && zone.m_level==1)
      name="ZoneUnkn0";
    else {
      name="ItemDta";
      // checkme: often Data22 is not parsed, but there can be others
      MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find a unparsed %s zone %d\n", zone.m_level==1 ? "data" : "main", zone.m_ids[0]));
    }
  }
  else {
    MWAW_DEBUG_MSG(("RagTime5Document::readZoneData: find a unknown type for zone=%d\n", zone.m_ids[0]));
    name="UnknownZone";
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  f << "Entries(" << name << "):" << zone;
  zone.m_isParsed=true;
  ascFile.addPos(zone.m_entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(zone.m_entry.end());
  ascFile.addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// parse the different zones
////////////////////////////////////////////////////////////
bool RagTime5Document::readString(RagTime5Zone &zone, std::string &text)
{
  if (!zone.m_entry.valid()) return false;
  MWAWInputStreamPtr input=zone.getInput();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(StringZone)[" << zone << "]:";
  input->seek(zone.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  text="";
  for (long i=0; i<zone.m_entry.length(); ++i) {
    auto c=char(input->readULong(1));
    if (c==0 && i+1==zone.m_entry.length()) break;
    if (c<0x1f)
      return false;
    text+=c;
  }
  f << "\"" << text << "\",";
  if (input->tell()!=zone.m_entry.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readString: find extra data\n"));
    f << "###";
    ascFile.addDelimiter(input->tell(),'|');
  }
  zone.m_isParsed=true;
  ascFile.addPos(zone.m_entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(zone.m_entry.end());
  ascFile.addNote("_");
  return true;
}

bool RagTime5Document::readUnicodeString(RagTime5Zone &zone, std::string const &what)
{
  if (zone.m_entry.length()==0) return true;
  MWAWInputStreamPtr input=zone.getInput();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  if (what.empty())
    f << "Entries(StringUnicode)[" << zone << "]:";
  else
    f << "Entries(" << what << ")[" << zone << "]:";
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(zone.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  librevenge::RVNGString string;
  if (!m_structManager->readUnicodeString(input, zone.m_entry.end(), string))
    f << "###";
  else
    f << string.cstr();
  zone.m_isParsed=true;
  ascFile.addPos(zone.m_entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(zone.m_entry.end());
  ascFile.addNote("_");
  input->setReadInverted(false);
  return true;
}

bool RagTime5Document::readUnicodeStringList(RagTime5ClusterManager::NameLink const &nameLink, std::map<int, librevenge::RVNGString> &idToStringMap)
{
  RagTime5DocumentInternal::IndexUnicodeParser dataParser(*this, false, "UnicodeNames");
  std::vector<long> posToNames[2];
  for (int i=0; i<2; ++i) {
    if (!nameLink.m_posToNames[i].empty())
      posToNames[i]=nameLink.m_posToNames[i];
    else if (!nameLink.m_posToNamesLinks[i].empty())
      readLongList(nameLink.m_posToNamesLinks[i], posToNames[i]);
  }
  long numPosToNames=long(posToNames[1].size());
  for (auto const &id : posToNames[0]) {
    if (id>=0 && id<numPosToNames)
      dataParser.m_indicesMap[int(posToNames[1][size_t(id)])]=int(id);
  }
  RagTime5ClusterManager::Link link;
  link.m_ids=nameLink.m_ids;
  link.m_longList=nameLink.m_decalList;
  if (!readListZone(link, dataParser))
    return false;
  idToStringMap=dataParser.m_idToStringMap;
  return true;
}

bool RagTime5Document::readLongListWithSize(int dataId, int fSz, std::vector<long> &listPosition, std::string const &zoneName)
{
  listPosition.clear();
  if (!dataId || fSz<=0 || fSz>4)
    return false;

  auto zone=getDataZone(dataId);
  if (!zone || !zone->m_entry.valid() || (zone->m_entry.length()%fSz) ||
      zone->getKindLastPart(zone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Document::readLongListWithSize: the zone %d seems bad\n", dataId));
    return false;
  }
  MWAWEntry entry=zone->m_entry;
  MWAWInputStreamPtr input=zone->getInput();
  input->setReadInverted(!zone->m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  zone->m_isParsed=true;
  libmwaw::DebugStream f;

  if (!zoneName.empty()) {
    std::string zName(zoneName);
    if (zName[0]>='a'&&zName[0]<='z')
      zName[0]=char(zName[0]+'A'-'a');
    f << "Entries(" << zName << ")[" << *zone << "]:";
  }
  else
    f << "Entries(ListLong" << fSz << ")[" << *zone << "]:";
  auto N=int(entry.length()/fSz);
  for (int i=0; i<N; ++i) {
    long ptr=input->readLong(fSz);
    listPosition.push_back(ptr);
    if (ptr==-2147483648) // 80000000
      f << "inf,";
    else if (ptr)
      f << ptr << ",";
    else
      f << "_,";
  }
  input->setReadInverted(false);
  zone->ascii().addPos(entry.begin());
  zone->ascii().addNote(f.str().c_str());
  zone->ascii().addPos(entry.end());
  zone->ascii().addNote("_");
  return true;
}

bool RagTime5Document::readLongList(RagTime5ClusterManager::Link const &link, std::vector<long> &list)
{
  if (!link.m_ids.empty() && link.m_ids[0] &&
      readLongListWithSize(link.m_ids[0], link.m_fieldSize, list, link.m_name))
    return true;
  list=link.m_longList;
  return !list.empty();
}

bool RagTime5Document::readPositions(int posId, std::vector<long> &listPosition)
{
  return readLongListWithSize(posId, 4, listPosition, "Positions");
}

////////////////////////////////////////////////////////////
// Cluster
////////////////////////////////////////////////////////////
bool RagTime5Document::readClusterRootData(RagTime5ClusterManager::ClusterRoot &cluster)
{
  // first read the list of child cluster and update the list of cluster for the cluster manager
  std::vector<int> listClusters;
  for (auto zone : m_state->m_zonesList) {
    if (!zone || zone->m_isParsed || !zone->m_entry.valid() || zone->getKindLastPart(zone->m_kinds[1].empty())!="Cluster")
      continue;
    listClusters.push_back(zone->m_ids[0]);
  }

  if (cluster.m_listClusterId==0) {
    MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterRootData: cluster list id is not set, try zone id+1\n"));
    cluster.m_listClusterId=cluster.m_zoneId+1;
  }
  std::vector<int> listChilds;
  m_clusterManager->readClusterMainList(cluster, listChilds, listClusters);
  std::set<int> seens;
  // the list of graphic type
  if (!cluster.m_graphicTypeLink.empty() && m_graphParser->readGraphicTypes(cluster.m_graphicTypeLink)) {
    if (cluster.m_graphicTypeLink.m_ids.size()>2 && cluster.m_graphicTypeLink.m_ids[1])
      seens.insert(cluster.m_graphicTypeLink.m_ids[1]);
  }
  // the different styles ( beginning with colors, then graphic styles and text styles )
  for (int i=0; i<8; ++i) {
    int const order[]= {7, 6, 1, 2, 0, 4, 3, 5};
    int cId=cluster.m_styleClusterIds[order[i]];
    if (!cId) continue;

    int const wh[]= {0x480, 0x480, 0x480, 0x480, 0x480, -1, 0x480, 0x8042};
    auto dZone= getDataZone(cId);
    if (!dZone || dZone->getKindLastPart(dZone->m_kinds[1].empty())!="Cluster" || !readClusterZone(*dZone, wh[order[i]])) {
      MWAW_DEBUG_MSG(("RagTime5Document::readClusterRootData: can not find cluster style zone %d\n", cId));
      continue;
    }
    seens.insert(cId);
  }
  // the formula def cluster list
  if (!cluster.m_listClusterLink[1].empty()) {
    RagTime5DocumentInternal::ClustListParser parser(*m_clusterManager.get(), 4, "FormulaList");
    readFixedSizeZone(cluster.m_listClusterLink[1], parser);
    // TODO: read the field cluster's data here
  }
  // list of style
  if (!cluster.m_listClusterLink[2].empty()) {
    RagTime5DocumentInternal::ClustListParser parser(*m_clusterManager.get(), 4, "RootUnknALst2");
    readFixedSizeZone(cluster.m_listClusterLink[2], parser);
  }
  // now the main cluster list
  for (int i=0; i<1; ++i) {
    int cId=cluster.m_clusterIds[i];
    if (cId==0) continue;
    auto data=getDataZone(cId);
    if (!data || !data->m_entry.valid() || data->getKindLastPart(data->m_kinds[1].empty())!="Cluster") {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterRootData: the cluster zone %d seems bad\n", cId));
      continue;
    }
    int const wh[]= {0x10000};
    if (readClusterZone(*data, wh[i]))
      seens.insert(cId);
  }
  if (!cluster.m_functionNameLink.empty())
    m_formulaParser->readFunctionNames(cluster.m_functionNameLink);
  m_state->m_buttonFormulaLink=cluster.m_formulaLink;
  for (auto const &lnk : cluster.m_settingLinks) {
    if (lnk.empty()) continue;
    RagTime5StructManager::FieldParser defaultParser("Settings");
    readStructZone(lnk, defaultParser, 0);
  }
  if (!cluster.m_docInfoLink.empty()) {
    RagTime5DocumentInternal::DocInfoFieldParser parser(*this);
    readStructZone(cluster.m_docInfoLink, parser, 18);
  }
  if (!cluster.m_listUnicodeLink.empty()) {
    RagTime5DocumentInternal::IndexUnicodeParser parser(*this, true, "RootUnicodeLst");
    readListZone(cluster.m_listUnicodeLink, parser);
  }

  // unknown link
  if (!cluster.m_linkUnknown.empty()) { // find always an empty list
    RagTime5StructManager::DataParser parser("RootUnknC");
    readListZone(cluster.m_linkUnknown, parser);
  }
  // now read the not parsed childs
  for (auto cId : listChilds) {
    if (cId==0 || seens.find(cId)!=seens.end())
      continue;
    auto dZone= getDataZone(cId);
    if (!dZone || dZone->getKindLastPart(dZone->m_kinds[1].empty())!="Cluster" || !readClusterZone(*dZone)) {
      MWAW_DEBUG_MSG(("RagTime5Document::readClusterRootData: can not find cluster zone %d\n", cId));
      continue;
    }
    seens.insert(cId);
  }

  for (auto const &link : cluster.m_linksList) {
    if (link.m_type==RagTime5ClusterManager::Link::L_List) {
      readListZone(link);
      continue;
    }
    else if (link.m_type==RagTime5ClusterManager::Link::L_LongList) {
      std::vector<long> list;
      readLongList(link, list);
      continue;
    }
    else if (link.m_type==RagTime5ClusterManager::Link::L_UnknownClusterC) {
      m_clusterManager->readUnknownClusterC(link);
      continue;
    }

    if (link.empty()) continue;
    std::shared_ptr<RagTime5Zone> data=getDataZone(link.m_ids[0]);
    if (!data || data->m_isParsed) {
      MWAW_DEBUG_MSG(("RagTime5Document::readClusterRootData: can not find data zone %d\n", link.m_ids[0]));
      continue;
    }
    data->m_hiLoEndian=cluster.m_hiLoEndian;
    if (link.m_fieldSize==0 && !data->m_entry.valid())
      continue;
    switch (link.m_type) {
    case RagTime5ClusterManager::Link::L_FieldsList:
    case RagTime5ClusterManager::Link::L_List:
    case RagTime5ClusterManager::Link::L_LongList:
    case RagTime5ClusterManager::Link::L_UnicodeList:
    case RagTime5ClusterManager::Link::L_UnknownClusterC:
      break;
    case RagTime5ClusterManager::Link::L_ClusterLink: {
      std::vector<RagTime5StructManager::ZoneLink> links;
      readClusterLinkList(*data, link, links);
      break;
    }
    case RagTime5ClusterManager::Link::L_Unknown:
#if !defined(__clang__)
    default:
#endif
      readFixedSizeZone(link, "");
      break;
    }
  }

  return true;
}

bool RagTime5Document::readChildList(RagTime5ClusterManager::Link const &link, std::vector<RagTime5StructManager::ZoneLink> &childList, bool findN)
{
  if (link.m_ids.empty())
    return true;
  auto dataZone=getDataZone(link.m_ids[0]);
  if (!dataZone || dataZone->m_entry.length()<=0) // ok, empty list
    return true;
  if (!dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Document::readChildList: the child zone %d seems bad\n",
                    link.m_ids[0]));
    return false;
  }
  if (findN) {
    if (dataZone->m_entry.length()%12) {
      MWAW_DEBUG_MSG(("RagTime5Document::readChildList: can not compute the number of child for zone %d\n",
                      link.m_ids[0]));
      return false;
    }
    auto finalLink=link;
    finalLink.m_N=int(dataZone->m_entry.length()/12);
    if (!readClusterLinkList(*dataZone, finalLink, childList))
      return false;
  }
  else if (!readClusterLinkList(*dataZone, link, childList))
    return false;
  checkClusterList(childList);
  return true;
}

bool RagTime5Document::checkClusterList(std::vector<int> const &list)
{
  bool ok=true;
  for (auto cId : list) {
    if (cId==0) continue;
    auto data=getDataZone(cId);
    if (!data || !data->m_entry.valid() || data->getKindLastPart(data->m_kinds[1].empty())!="Cluster") {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::checkClusterList: the cluster zone %d seems bad\n", cId));
      ok=false;
    }
  }
  return ok;
}

bool RagTime5Document::checkClusterList(std::vector<RagTime5StructManager::ZoneLink> const &list)
{
  bool ok=true;
  for (auto const &lnk : list) {
    int cId=lnk.m_dataId;
    if (cId==0) continue;
    auto data=getDataZone(cId);
    if (!data || !data->m_entry.valid() || data->getKindLastPart(data->m_kinds[1].empty())!="Cluster") {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::checkClusterList: the cluster zone %d seems bad\n", cId));
      ok=false;
    }
  }
  return ok;
}

bool RagTime5Document::readClusterZone(RagTime5Zone &zone, int zoneType)
{
  std::shared_ptr<RagTime5ClusterManager::Cluster> cluster;
  if (!m_clusterManager->readCluster(zone, cluster, zoneType) || !cluster)
    return false;
  checkClusterList(cluster->m_clusterIdsList);

  switch (cluster->m_type) {
  // main zone
  case RagTime5ClusterManager::Cluster::C_ButtonZone:
  case RagTime5ClusterManager::Cluster::C_ChartZone:
  case RagTime5ClusterManager::Cluster::C_FormulaDef:
  case RagTime5ClusterManager::Cluster::C_FormulaPos:
  case RagTime5ClusterManager::Cluster::C_GraphicZone:
  case RagTime5ClusterManager::Cluster::C_GroupZone:
  case RagTime5ClusterManager::Cluster::C_Layout:
  case RagTime5ClusterManager::Cluster::C_PictureZone:
  case RagTime5ClusterManager::Cluster::C_Pipeline:
  case RagTime5ClusterManager::Cluster::C_SpreadsheetZone:
  case RagTime5ClusterManager::Cluster::C_Sound:
  case RagTime5ClusterManager::Cluster::C_TextZone: // parsing already done
    return true;
  case RagTime5ClusterManager::Cluster::C_ClusterGProp:
    return readClusterGProp(*cluster);
  case RagTime5ClusterManager::Cluster::C_ClusterC:
    return readUnknownClusterCData(*cluster);
  case RagTime5ClusterManager::Cluster::C_ColorPattern:
    return m_graphParser->readColorPatternZone(*cluster);
  case RagTime5ClusterManager::Cluster::C_Root: {
    auto *root=dynamic_cast<RagTime5ClusterManager::ClusterRoot *>(cluster.get());
    if (!root) {
      MWAW_DEBUG_MSG(("RagTime5ClusterManager::readClusterZone: can not find the root pointer\n"));
      return false;
    }
    readClusterRootData(*root);
    return true;
  }

  // style
  case RagTime5ClusterManager::Cluster::C_FormatStyles:
    return m_styleManager->readFormats(*cluster);
  case RagTime5ClusterManager::Cluster::C_ColorStyles:
    return m_styleManager->readGraphicColors(*cluster);
  case RagTime5ClusterManager::Cluster::C_GraphicStyles:
    return m_styleManager->readGraphicStyles(*cluster);
  case RagTime5ClusterManager::Cluster::C_TextStyles:
    return m_styleManager->readTextStyles(*cluster);
  case RagTime5ClusterManager::Cluster::C_UnitStyles: {
    RagTime5StructManager::FieldParser defaultParser("Units");
    return readStructZone(cluster->m_dataLink, defaultParser, 14, &cluster->m_nameLink);
  }

  case RagTime5ClusterManager::Cluster::C_Empty:
  case RagTime5ClusterManager::Cluster::C_Unknown:
#if !defined(__clang__)
  default:
#endif
    break;
  }

  if (!cluster->m_nameLink.empty()) {
    std::map<int, librevenge::RVNGString> idToStringMap;
    readUnicodeStringList(cluster->m_nameLink, idToStringMap);
  }

  for (auto const &link : cluster->m_linksList) {
    if (link.m_type==RagTime5ClusterManager::Link::L_List)
      readListZone(link);
    else
      readFixedSizeZone(link, "");
  }
  return true;
}

bool RagTime5Document::readClusterLinkList
(RagTime5ClusterManager::Link const &link, std::vector<RagTime5StructManager::ZoneLink> &list, std::string const &name)
{
  RagTime5DocumentInternal::ClustListParser parser(*m_clusterManager.get(), 10, !name.empty() ? name : link.getZoneName());
  if (!link.empty())
    readListZone(link, parser);
  list=parser.m_linkList;
  checkClusterList(list);
  return true;
}

bool RagTime5Document::readClusterLinkList(RagTime5Zone &zone, RagTime5ClusterManager::Link const &link,
    std::vector<RagTime5StructManager::ZoneLink> &listLinks)
{
  listLinks.clear();
  if (!zone.m_entry.valid()) {
    if (link.m_N && link.m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5Document::readClusterLinkList: can not find data zone %d\n", link.m_ids[0]));
    }
    return false;
  }

  MWAWInputStreamPtr input=zone.getInput();
  bool const hiLo=zone.m_hiLoEndian;
  input->setReadInverted(!hiLo);
  input->seek(zone.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  zone.m_isParsed=true;

  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  std::string zoneName=link.m_name.empty() ? "ClustLink" : link.m_name;
  zoneName[0]=char(std::toupper((unsigned char) zoneName[0]));
  f << "Entries(" << zoneName << ")[" << zone << "]:";
  if (link.m_N*link.m_fieldSize>zone.m_entry.length() || link.m_N*link.m_fieldSize < 0 ||
      link.m_N>zone.m_entry.length() || link.m_fieldSize!=12) {
    MWAW_DEBUG_MSG(("RagTime5Document::readClusterLinkList: bad fieldSize/N for zone %d\n", link.m_ids[0]));
    f << "###";
    ascFile.addPos(zone.m_entry.begin());
    ascFile.addNote(f.str().c_str());
    return true;
  }
  ascFile.addPos(zone.m_entry.begin());
  ascFile.addNote(f.str().c_str());

  listLinks.resize(size_t(link.m_N)+1);
  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    f << zoneName << "-" << i+1 << ":";
    RagTime5StructManager::ZoneLink cLink;

    std::vector<int> listIds;
    if (!m_structManager->readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5Document::readClusterLinkList: a link seems bad\n"));
      f << "###id,";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos+12, librevenge::RVNG_SEEK_SET);
      continue;
    }
    else if (listIds[0]==0) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(pos+12, librevenge::RVNG_SEEK_SET);
      continue;
    }

    cLink.m_dataId=listIds[0];
    f << m_clusterManager->getClusterDebugName(listIds[0]) << ",";
    cLink.m_subZoneId[0]=long(input->readULong(4)); // 0 or 80000000 and a small int
    cLink.m_subZoneId[1]=long(input->readLong(4)); // small int
    f << cLink;
    listLinks[size_t(i+1)]=cLink;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=zone.m_entry.end()) {
    f.str("");
    f << zoneName << ":end";
    ascFile.addPos(input->tell());
    ascFile.addNote(f.str().c_str());
  }

  return true;
}

////////////////////////////////////////////////////////////
// structured zone
////////////////////////////////////////////////////////////
bool RagTime5Document::readDocInfoClusterData(RagTime5Zone &zone, MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()<160) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: the entry does not seems valid\n"));
    return false;
  }
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  MWAWInputStreamPtr input=zone.getInput();
  long pos=entry.begin();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  f << "DocInfo[dataA]:";
  // checkme the field data seems always in hilo endian...
  bool actEndian=input->readInverted();
  input->setReadInverted(false);

  auto val=static_cast<int>(input->readULong(2)); // always 0
  if (val) f << "f0=" << val;
  auto dataSz=long(input->readULong(4));
  if (pos+dataSz>entry.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: the main data size seems bad\n"));
    f << "###dSz=" << dataSz << ",";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    input->setReadInverted(actEndian);
    return true;
  }
  for (int i=0; i<2; ++i) { // f1=2
    val=static_cast<int>(input->readULong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto sSz=static_cast<int>(input->readULong(1));
  long actPos=input->tell();
  if (sSz>25) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: the dataA string size seems bad\n"));
    f << "###sSz=" << sSz << ",";
    sSz=0;
  }
  std::string text("");
  for (int i=0; i<sSz; ++i) text += char(input->readULong(1));
  f << text << ",";
  input->seek(actPos+25, librevenge::RVNG_SEEK_SET);
  f << "IDS=["; // maybe some char
  for (int i=0; i<7; ++i) { // _, ?, ?, ?, 0, 0|4, ?
    val=static_cast<int>(input->readULong(2));
    if (val) f << std::hex << val << std::dec << ",";
    else f << "_,";
  }
  f << "],";
  sSz=static_cast<int>(input->readULong(1));
  actPos=input->tell();
  if (sSz>62) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: the dataA string2 size seems bad\n"));
    f << "###sSz2=" << sSz << ",";
    sSz=0;
  }
  text=("");
  for (int i=0; i<sSz; ++i) text += char(input->readULong(1));
  f << text << ",";
  input->seek(actPos+63, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo[dataB]:";
  f << "IDS=["; // maybe some char
  for (int i=0; i<8; ++i) {
    val=static_cast<int>(input->readULong(2));
    if (val) f << std::hex << val << std::dec << ",";
    else f << "_,";
  }
  f << "],";
  for (int i=0; i<11; ++i) { // f0=-1|2|6, f1=-1|2|4, f3=0|17|21,
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  val=static_cast<int>(input->readLong(1)); // 0
  if (val) f << "f11=" << val << ",";
  sSz=static_cast<int>(input->readULong(1));
  if (sSz>64||pos+sSz+4>entry.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: the string size for dataB data seems bad\n"));
    f << "###sSz3=" << sSz << ",";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    input->setReadInverted(actEndian);
    return true;
  }
  text=("");
  for (int i=0; i<sSz; ++i) text += char(input->readULong(1));
  f << text << ",";
  if ((sSz%2)==1)
    input->seek(1, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo[dataC]:";
  if (input->readLong(2)!=1 || (val=static_cast<int>(input->readLong(2)))<=0 || (val%4) || pos+6+val>entry.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: oops something is bad[dataC]\n"));
    f << "###val=" << val << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    input->setReadInverted(actEndian);
    return true;
  }
  int N=val/4;
  f << "list=[";
  for (int i=0; i<N; ++i) {
    val=static_cast<int>(input->readLong(4));
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  val=static_cast<int>(input->readLong(2)); // always 2
  if (val!=2) f << "f0=" << val << ",";
  sSz=static_cast<int>(input->readULong(2));
  if (input->tell()+sSz+4>entry.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocInfoClusterData: string size seems bad[dataC]\n"));
    f << "###sSz=" << sSz << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
    input->setReadInverted(actEndian);
    return true;
  }
  text=("");
  for (int i=0; i<sSz; ++i) text += char(input->readULong(1));
  f << text << ",";
  if ((sSz%2)==1)
    input->seek(1, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo[dataD]:";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  input->setReadInverted(actEndian);
  return true;
}

bool RagTime5Document::readScriptComment(RagTime5Zone &zone)
{
  if (!zone.m_entry.valid() ||
      zone.getKindLastPart(zone.m_kinds[1].empty())!="Unicode") {
    zone.addErrorInDebugFile("ScriptComment");
    MWAW_DEBUG_MSG(("RagTime5Document::readScriptComment: the script comment zone %d seems bad\n", zone.m_ids[0]));
    return true;
  }
  readUnicodeString(zone, "ScriptComment");
  libmwaw::DebugStream f;
  for (auto it : zone.m_childIdToZoneMap) {
    auto child=it.second;
    if (!child || child->m_isParsed) continue;
    child->m_isParsed=true;
    switch (it.first) {
    case 3:  // find one time with no data
      if (child->m_entry.valid()) {
        MWAW_DEBUG_MSG(("RagTime5Document::readScriptComment: find data with child3\n"));
        libmwaw::DebugFile &ascFile=child->ascii();
        f.str("");
        f << "ScriptComment[" << *child << "child3]:";
        ascFile.addPos(child->m_entry.begin());
        ascFile.addNote(f.str().c_str());
        ascFile.addPos(child->m_entry.end());
        ascFile.addNote("_");
      }
      break;
    case 8:
      ascii().addPos(child->m_defPosition);
      ascii().addNote("scriptComment[refCount]");
      break;
    default: {
      std::string kind=child->getKindLastPart();
      if (kind=="Unicode") { // the script name
        child->m_hiLoEndian=zone.m_hiLoEndian;
        readUnicodeString(*child, "ScriptNameData");
        break;
      }
      if (kind=="32Bit") {
        if (child->m_variableD[0]!=0 || child->m_variableD[1]!=1) { // do not show in meny
          MWAW_DEBUG_MSG(("RagTime5Document::readScriptComment: find unknown flag\n"));
          ascii().addPos(child->m_defPosition);
          ascii().addNote("scriptData[showInMenu]:###");
        }
        if (child->m_entry.valid()) {
          libmwaw::DebugFile &ascFile=child->ascii();
          f.str("");
          f << "Entries(ScriptData)[" << *child << "]:###";
          MWAW_DEBUG_MSG(("RagTime5Document::readScriptComment: find unknown script data\n"));
          ascFile.addPos(child->m_entry.begin());
          ascFile.addNote(f.str().c_str());
          ascFile.addPos(child->m_entry.end());
          ascFile.addNote("_");
        }
        break;
      }
      if (kind=="OSAScript") {
        if (child->m_entry.valid()) {
          libmwaw::DebugFile &ascFile=child->ascii();
          f.str("");
          f << "Entries(OSAScript)[" << *child << "]:";
          ascFile.addPos(child->m_entry.begin());
          ascFile.addNote(f.str().c_str());
          ascFile.addPos(child->m_entry.end());
          ascFile.addNote("_");
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5Document::readScriptComment: find unknown child zone\n"));
      child->addErrorInDebugFile("ScriptComment");
      break;
    }
    }
  }
  return true;
}

bool RagTime5Document::readClusterGProp(RagTime5ClusterManager::Cluster &cluster)
{
  RagTime5ClusterManager::Link const &link=cluster.m_dataLink;
  if (link.m_ids.size()<2 || !link.m_ids[1]) {
    MWAW_DEBUG_MSG(("RagTime5Document::readClusterGProp: can not find the main data\n"));
    return false;
  }
  // probably a cluster with only on field, so...
  RagTime5StructManager::GObjPropFieldParser defaultParser("RootGObjProp");
  if (!readStructZone(link, defaultParser, 8, &cluster.m_nameLink)) {
    auto dataZone=getDataZone(link.m_ids[1]);
    if (dataZone)
      dataZone->addErrorInDebugFile("RootGObjProp");
    MWAW_DEBUG_MSG(("RagTime5Document::readClusterGProp: unexpected type for zone %d\n", link.m_ids[1]));
  }

  for (auto const &lnk : cluster.m_linksList) {
    MWAW_DEBUG_MSG(("RagTime5Document::readClusterGProp: find extra data\n"));
    RagTime5StructManager::DataParser defParser("UnknBUnknown2");
    readFixedSizeZone(lnk, defParser);
  }

  return true;
}

bool RagTime5Document::readUnknownClusterCData(RagTime5ClusterManager::Cluster &cluster)
{
  RagTime5ClusterManager::Link const &link=cluster.m_dataLink;
  if (link.m_ids.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Document::readUnknownClusterCData: can not find the main data\n"));
    return false;
  }
  std::stringstream s;
  s << "UnknC_" << char('A'+link.m_fileType[0]) << "_";
  std::string zoneName=s.str();

  if (link.m_type==RagTime5ClusterManager::Link::L_List) {
    if (link.m_fileType[1]==0x310) {
      // find id=8,"Rechenblatt 1": spreadsheet name ?
      RagTime5DocumentInternal::IndexUnicodeParser parser(*this, true, zoneName+"0");
      readListZone(link, parser);
    }
    else {
      RagTime5StructManager::DataParser parser(zoneName+"0");
      readListZone(link, parser);
    }
  }
  else {
    RagTime5StructManager::DataParser defaultParser(zoneName+"0");
    readFixedSizeZone(link, defaultParser);
  }
  for (auto const &lnk : cluster.m_linksList) {
    RagTime5StructManager::DataParser parser(zoneName+"1");
    readFixedSizeZone(lnk, parser);
  }

  return true;
}

bool RagTime5Document::readListZone(RagTime5ClusterManager::Link const &link)
{
  RagTime5StructManager::DataParser parser(link.getZoneName());
  return readListZone(link, parser);
}

bool RagTime5Document::readListZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::DataParser &parser)
{
  if (link.m_ids.size()<2 || !link.m_ids[1])
    return false;

  std::vector<long> decal;
  if (link.m_ids[0])
    readPositions(link.m_ids[0], decal);
  if (decal.empty())
    decal=link.m_longList;

  int const dataId=link.m_ids[1];
  auto dataZone=getDataZone(dataId);
  auto N=int(decal.size());

  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData" || N<=1) {
    if (N==1 && dataZone && !dataZone->m_entry.valid()) {
      // a zone with 0 zone is ok...
      dataZone->m_isParsed=true;
      libmwaw::DebugStream f;
      f << "[" << parser.getZoneName() << "]";
      ascii().addPos(dataZone->m_defPosition);
      ascii().addNote(f.str().c_str());
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5Document::readListZone: the data zone %d seems bad\n", dataId));
    return false;
  }

  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(" << parser.getZoneName() << ")[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  long debPos=entry.begin();
  long endPos=entry.end();

  for (int i=0; i<N-1; ++i) {
    long pos=decal[size_t(i)], lastPos=decal[size_t(i+1)];
    if (pos==lastPos) continue;
    if (pos<0 || pos>lastPos || debPos+lastPos>endPos) {
      MWAW_DEBUG_MSG(("RagTime5Document::readListZone: can not read the data zone %d-%d seems bad\n", dataId, i));
      continue;
    }
    input->seek(debPos+pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << parser.getZoneName(i+1) << ":";
    if (!parser.parseData(input, debPos+lastPos, *dataZone, i+1, f))
      f << "###";
    ascFile.addPos(debPos+pos);
    ascFile.addNote(f.str().c_str());
    ascFile.addPos(debPos+lastPos);
    ascFile.addNote("_");
  }

  input->setReadInverted(false);
  return true;
}

bool RagTime5Document::readFixedSizeZone(RagTime5ClusterManager::Link const &link, std::string const &name)
{
  RagTime5StructManager::DataParser parser(name.empty() ? link.getZoneName() : name);
  return readFixedSizeZone(link, parser);
}

bool RagTime5Document::readFixedSizeZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::DataParser &parser)
{
  if (link.m_ids.empty() || !link.m_ids[0])
    return false;

  int const dataId=link.m_ids[0];
  auto dataZone=getDataZone(dataId);

  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData" ||
      link.m_fieldSize<=0 || link.m_N>dataZone->m_entry.length()/link.m_fieldSize ||
      link.m_N>dataZone->m_entry.length() || link.m_N<0) {
    if ((link.m_N==0 || link.m_fieldSize==0) && dataZone && !dataZone->m_entry.valid()) {
      // a zone with 0 zone is ok...
      dataZone->m_isParsed=true;
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5Document::readFixedSizeZone: the data zone %d seems bad\n", dataId));
    if (dataZone) dataZone->addErrorInDebugFile(parser.getZoneName());
    return false;
  }

  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(" << parser.getZoneName() << ")[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();

  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    f << parser.getZoneName(i+1) << ":";
    if (!parser.parseData(input, pos+link.m_fieldSize, *dataZone, i+1, f))
      f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+link.m_fieldSize, librevenge::RVNG_SEEK_SET);
  }
  long pos=input->tell();
  if (pos!=endPos) {
    f.str("");
    f << parser.getZoneName() << ":#end";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  input->setReadInverted(false);
  return true;
}

bool RagTime5Document::readStructZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::FieldParser &parser, int headerSz, RagTime5ClusterManager::NameLink *nameLink)
{
  if (link.m_ids.size()<2 || !link.m_ids[1])
    return false;

  std::map<int, librevenge::RVNGString> idToNameMap;
  if (nameLink && !nameLink->empty()) {
    readUnicodeStringList(*nameLink, idToNameMap);
    *nameLink=RagTime5ClusterManager::NameLink();
  }
  std::vector<long> decal;
  if (link.m_ids[0])
    readPositions(link.m_ids[0], decal);
  if (decal.empty())
    decal=link.m_longList;
  int const dataId=link.m_ids[1];
  auto dataZone=getDataZone(dataId);
  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
    if (decal.size()==1) {
      // a zone with 0 zone is ok...
      if (dataZone)
        dataZone->m_isParsed=true;
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5Document::readStructZone: the data zone %d seems bad\n", dataId));
    return false;
  }
  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(" << parser.getZoneName() << ")[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  auto N=int(decal.size());
  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  long debPos=entry.begin();
  long endPos=entry.end();
  if (N==0) {
    MWAW_DEBUG_MSG(("RagTime5Document::readStructZone: can not find decal list for zone %d, let try to continue\n", dataId));
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    int n=0;
    while (input->tell()+8 < endPos) {
      long pos=input->tell();
      int id=++n;
      librevenge::RVNGString name("");
      if (idToNameMap.find(id)!=idToNameMap.end())
        name=idToNameMap.find(id)->second;
      if (!readStructData(*dataZone, endPos, id, headerSz, parser, name)) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
    }
    if (input->tell()!=endPos) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("RagTime5Document::readStructZone: can not read some block\n"));
        first=false;
      }
      ascFile.addPos(debPos);
      ascFile.addNote("###");
    }
  }
  else {
    for (int i=0; i<N-1; ++i) {
      long pos=decal[size_t(i)];
      long nextPos=decal[size_t(i+1)];
      if (pos<0 || debPos+pos>endPos) {
        MWAW_DEBUG_MSG(("RagTime5Document::readStructZone: can not read the data zone %d-%d seems bad\n", dataId, i));
        continue;
      }
      librevenge::RVNGString name("");
      if (idToNameMap.find(i+1)!=idToNameMap.end())
        name=idToNameMap.find(i+1)->second;
      input->seek(debPos+pos, librevenge::RVNG_SEEK_SET);
      readStructData(*dataZone, debPos+nextPos, i+1, headerSz, parser, name);
      if (input->tell()!=debPos+nextPos) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5Document::readStructZone: can not read some block\n"));
          first=false;
        }
        ascFile.addPos(debPos+pos);
        ascFile.addNote("###");
      }
    }
  }
  return true;
}

bool RagTime5Document::readStructData(RagTime5Zone &zone, long endPos, int n, int headerSz,
                                      RagTime5StructManager::FieldParser &parser, librevenge::RVNGString const &dataName)
{
  MWAWInputStreamPtr input=zone.getInput();
  long pos=input->tell();
  if ((headerSz && pos+headerSz>endPos) || (headerSz==0 && pos+5>endPos)) return false;
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  std::string const zoneName=parser.getZoneName(n);
  int m=0;
  if (headerSz>0) {
    f << zoneName << "[A]:";
    if (!dataName.empty()) f << dataName.cstr() << ",";
    int val;
    if (headerSz==14) {
      val=static_cast<int>(input->readLong(4));
      if (val!=1) f << "numUsed=" << val << ",";
      f << "f1=" << std::hex << input->readULong(2) << std::dec << ",";
      val=static_cast<int>(input->readLong(2)); // sometimes form an increasing sequence but not always
      if (val!=n) f << "id=" << val << ",";

      RagTime5StructManager::Field field;
      field.m_fileType=input->readULong(4);
      field.m_type=RagTime5StructManager::Field::T_Long;
      field.m_longValue[0]=input->readLong(2);
      parser.parseHeaderField(field, zone, n, f);
    }
    else if (headerSz==8) {
      val=static_cast<int>(input->readLong(2));
      if (val!=1) f << "numUsed=" << val << ",";
      val=static_cast<int>(input->readLong(2)); // sometimes form an increasing sequence but not always
      if (val!=n) f << "id=" << val << ",";
      f << "type=" << std::hex << input->readULong(4) << std::dec << ","; // 0 or 01458042
    }
    else if (headerSz==18) { // docinfo header
      val=static_cast<int>(input->readLong(4)); // 1 or 3
      if (val!=1) f << "numUsed?=" << val << ",";
      val=static_cast<int>(input->readLong(4)); // always 0
      if (val) f << "f0=" << val << ",";
      f << "ID=" << std::hex << input->readULong(4) << ","; // a big number
      val=static_cast<int>(input->readLong(4));
      if (val!=0x1f6817) // doc info type
        f << "type=" << std::hex << val << std::dec << ",";
      val=static_cast<int>(input->readLong(2)); // always 0
      if (val) f << "f1=" << val << ",";
      input->seek(pos+headerSz, librevenge::RVNG_SEEK_SET);
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5Document::readStructData: find unknown header size\n"));
      f << "###hSz";
      input->seek(pos+headerSz, librevenge::RVNG_SEEK_SET);
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  pos=input->tell();
  if (parser.m_regroupFields) {
    f.str("");
    f << zoneName << "[B]:";
    if (headerSz==0 && !dataName.empty()) f << dataName.cstr() << ",";
  }
  while (!input->isEnd()) {
    long actPos=input->tell();
    if (actPos>=endPos) break;

    if (!parser.m_regroupFields) {
      f.str("");
      f << zoneName << "[B" << ++m << "]:";
      if (m==1 && headerSz==0 && !dataName.empty()) f << dataName.cstr() << ",";
    }
    RagTime5StructManager::Field field;
    if (!m_structManager->readField(input, endPos, ascFile, field, headerSz ? 0 : endPos-actPos)) {
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
    if (!parser.parseField(field, zone, n, f))
      f << "#" << field;
    if (!parser.m_regroupFields) {
      ascFile.addPos(actPos);
      ascFile.addNote(f.str().c_str());
    }
  }
  if (parser.m_regroupFields && pos!=input->tell()) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

////////////////////////////////////////////////////////////
// zone unpack/create ascii file, ...
////////////////////////////////////////////////////////////
bool RagTime5Document::updateZone(std::shared_ptr<RagTime5Zone> &zone)
{
  if (!zone || zone->m_isInitialised || zone->m_isParsed)
    return true;

  zone->m_isInitialised=true;
  // update the kinds of this zone
  for (int j=1; j<3; ++j) {
    if (!zone->m_ids[j]) continue;
    if (m_state->m_zoneIdToTypeMap.find(zone->m_ids[j])==m_state->m_zoneIdToTypeMap.end()) {
      // the main zone seems to point to a cluster id...
      if (zone->m_ids[0]<=6) continue;
      MWAW_DEBUG_MSG(("RagTime5Document::updateZone: can not find the type for %d:%d\n", zone->m_ids[0],j));
      ascii().addPos(zone->m_defPosition);
      ascii().addNote("###type,");
    }
    else {
      zone->m_kinds[j-1]=m_state->m_zoneIdToTypeMap.find(zone->m_ids[j])->second;
      libmwaw::DebugStream f;
      f << zone->m_kinds[j-1] << ",";
      ascii().addPos(zone->m_defPosition);
      ascii().addNote(f.str().c_str());
    }
  }

  // update the zone input
  if (!zone->m_entriesList.empty() && !updateZoneInput(*zone))
    return false;

  // check for pack zones and unpack them
  int usedId=zone->m_kinds[1].empty() ? 0 : 1;
  std::string actType=zone->getKindLastPart(usedId==0);
  if (actType=="Pack") {
    if (zone->m_entry.valid() && !unpackZone(*zone)) {
      MWAW_DEBUG_MSG(("RagTime5Document::updateZone: can not unpack the zone %d\n", zone->m_ids[0]));
      libmwaw::DebugStream f;
      libmwaw::DebugFile &ascFile=zone->ascii();
      f << "Entries(BADPACK)[" << zone << "]:###" << zone->m_kinds[usedId];
      ascFile.addPos(zone->m_entry.begin());
      ascFile.addNote(f.str().c_str());
      zone->m_entry=MWAWEntry();
    }
    size_t length=zone->m_kinds[usedId].size();
    if (length>5)
      zone->m_kinds[usedId].resize(length-5);
    else
      zone->m_kinds[usedId]="";
  }

  // check hilo flag
  usedId=zone->m_kinds[1].empty() ? 0 : 1;
  actType=zone->getKindLastPart(usedId==0);
  if (actType=="HiLo" || actType=="LoHi") {
    zone->m_hiLoEndian=actType=="HiLo";
    size_t length=zone->m_kinds[usedId].size();
    if (length>5)
      zone->m_kinds[usedId].resize(length-5);
    else
      zone->m_kinds[usedId]="";
  }
  // update the zone kind
  std::string kind=zone->getKindLastPart();
  if (kind=="Type") {
    size_t length=zone->m_kinds[0].size();
    if (length>5)
      zone->m_kinds[0].resize(length-5);
    else
      zone->m_kinds[0]="";
    zone->m_extra += "type,";
  }

  return true;
}
bool RagTime5Document::updateZoneInput(RagTime5Zone &zone)
{
  if (zone.getInput() || zone.m_entriesList.empty())
    return true;
  std::stringstream s;
  s << "Zone" << std::hex << zone.m_entriesList[0].begin() << std::dec;
  zone.setAsciiFileName(s.str());

  MWAWInputStreamPtr input = m_parser->getInput();
  if (zone.m_entriesList.size()==1) {
    zone.setInput(input);
    zone.m_entry=zone.m_entriesList[0];
    return true;
  }

  libmwaw::DebugStream f;
  f << "Entries(" << zone.getZoneName() << "):";
  std::shared_ptr<MWAWStringStream> newStream;
  int n=0;
  for (auto const &entry : zone.m_entriesList) {
    if (!entry.valid() || !input->checkPosition(entry.end())) {
      MWAW_DEBUG_MSG(("RagTime5Document::updateZoneInput: can not read some data\n"));
      f << "###";
      ascii().addPos(entry.begin());
      ascii().addNote(f.str().c_str());
      return false;
    }
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

    unsigned long read;
    const unsigned char *dt = input->read(static_cast<unsigned long>(entry.length()), read);
    if (!dt || long(read) != entry.length()) {
      MWAW_DEBUG_MSG(("RagTime5Document::updateZoneInput: can not read some data\n"));
      f << "###";
      ascii().addPos(entry.begin());
      ascii().addNote(f.str().c_str());
      return false;
    }
    ascii().skipZone(entry.begin(), entry.end()-1);
    if (n++==0)
      newStream.reset(new MWAWStringStream(dt, static_cast<unsigned int>(entry.length())));
    else
      newStream->append(dt, static_cast<unsigned int>(entry.length()));
  }

  MWAWInputStreamPtr newInput(new MWAWInputStream(newStream, false));
  zone.setInput(newInput);
  zone.m_entry.setBegin(0);
  zone.m_entry.setLength(newInput->size());

  return true;
}

bool RagTime5Document::unpackZone(RagTime5Zone &zone, MWAWEntry const &entry, std::vector<unsigned char> &data)
{
  if (!entry.valid())
    return false;

  MWAWInputStreamPtr input=zone.getInput();
  long pos=entry.begin(), endPos=entry.end();
  if (entry.length()<4 || !input || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: the input seems bad\n"));
    return false;
  }

  bool actEndian=input->readInverted();
  input->setReadInverted(false);
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  data.resize(0);
  auto sz=static_cast<unsigned long>(input->readULong(4));
  if (sz==0) {
    input->setReadInverted(actEndian);
    return true;
  }
  auto flag=int(sz>>24);
  sz &= 0xFFFFFF;
  if ((flag&0xf) || (flag&0xf0)==0 || !(sz&0xFFFFFF)) {
    input->setReadInverted(actEndian);
    return false;
  }

  int nBytesRead=0, szField=9;
  unsigned int read=0;
  size_t mapPos=0;
  data.reserve(size_t(sz));
  std::vector<std::vector<unsigned char> > mapToString;
  mapToString.reserve(size_t(entry.length()-6));
  bool ok=false;
  while (!input->isEnd()) {
    if (static_cast<int>(mapPos)==(1<<szField)-0x102)
      ++szField;
    if (input->tell()>=endPos) {
      MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: oops can not find last data\n"));
      ok=false;
      break;
    }
    do {
      read = (read<<8)+static_cast<unsigned int>(input->readULong(1));
      nBytesRead+=8;
    }
    while (nBytesRead<szField);
    unsigned int val=(read >> (nBytesRead-szField));
    nBytesRead-=szField;
    read &= ((1<<nBytesRead)-1);

    if (val<0x100) {
      auto c=static_cast<unsigned char>(val);
      data.push_back(c);
      if (mapPos>= mapToString.size())
        mapToString.resize(mapPos+1);
      mapToString[mapPos++]=std::vector<unsigned char>(1,c);
      continue;
    }
    if (val==0x100) { // begin
      if (!data.empty()) {
        // data are reset when mapPos=3835, so it is ok
        mapPos=0;
        mapToString.resize(0);
        szField=9;
      }
      continue;
    }
    if (val==0x101) {
      ok=read==0;
      if (!ok) {
        MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: find 0x101 in bad position\n"));
      }
      break;
    }
    auto readPos=size_t(val-0x102);
    if (readPos >= mapToString.size()) {
      MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: find bad position\n"));
      ok = false;
      break;
    }
    std::vector<unsigned char> final=mapToString[readPos++];
    if (readPos==mapToString.size())
      final.push_back(final[0]);
    else
      final.push_back(mapToString[readPos][0]);
    data.insert(data.end(), final.begin(), final.end());
    if (mapPos>= mapToString.size())
      mapToString.resize(mapPos+1);
    mapToString[mapPos++]=final;
  }

  if (ok && data.size()!=size_t(sz)) {
    MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: oops the data file is bad\n"));
    ok=false;
  }
  if (!ok) {
    MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: stop with mapPos=%ld and totalSize=%ld/%ld\n", long(mapPos), long(data.size()), long(sz)));
  }
  input->setReadInverted(actEndian);
  return ok;
}

bool RagTime5Document::unpackZone(RagTime5Zone &zone)
{
  if (!zone.m_entry.valid())
    return false;

  std::vector<unsigned char> newData;
  if (!unpackZone(zone, zone.m_entry, newData))
    return false;
  long pos=zone.m_entry.begin(), endPos=zone.m_entry.end();
  MWAWInputStreamPtr input=zone.getInput();
  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("RagTime5Document::unpackZone: find some extra data\n"));
    return false;
  }
  if (newData.empty()) {
    // empty zone
    zone.ascii().addPos(pos);
    zone.ascii().addNote("_");
    zone.m_entry.setLength(0);
    zone.m_extra += "packed,";
    return true;
  }

  if (input.get()==m_parser->getInput().get())
    ascii().skipZone(pos, endPos-1);

  std::shared_ptr<MWAWStringStream> newStream(new MWAWStringStream(&newData[0], static_cast<unsigned int>(newData.size())));
  MWAWInputStreamPtr newInput(new MWAWInputStream(newStream, false));
  zone.setInput(newInput);
  zone.m_entry.setBegin(0);
  zone.m_entry.setLength(newInput->size());
  zone.m_extra += "packed,";
  return true;
}

////////////////////////////////////////////////////////////
// read the different zones
////////////////////////////////////////////////////////////
bool RagTime5Document::readDocumentVersion(RagTime5Zone &zone)
{
  MWAWInputStreamPtr input = zone.getInput();
  MWAWEntry &entry=zone.m_entry;

  zone.m_isParsed=true;
  ascii().addPos(zone.m_defPosition);
  ascii().addNote("doc[version],");

  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "Entries(DocVersion):";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  if ((entry.length())%6!=2) {
    MWAW_DEBUG_MSG(("RagTime5Document::readDocumentVersion: the entry size seem bads\n"));
    f << "###";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return true;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  auto val=static_cast<int>(input->readLong(1)); // find 2-4
  f << "f0=" << val << ",";
  val=static_cast<int>(input->readLong(1)); // always 0
  if (val)
    f << "f1=" << val << ",";
  auto N=int(entry.length()/6);
  for (int i=0; i<N; ++i) {
    // v0: last used version, v1: first used version, ... ?
    f << "v" << i << "=" << input->readLong(1);
    val = static_cast<int>(input->readULong(1));
    if (val)
      f << "." << val;
    val = static_cast<int>(input->readULong(1)); // 20|60|80
    if (val != 0x80)
      f << ":" << std::hex << val << std::dec;
    for (int j=0; j<3; ++j) { // often 0 or small number
      val = static_cast<int>(input->readULong(1));
      if (val)
        f << ":" << val << "[" << j << "]";
    }
    f << ",";
  }
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// find the different zones in a OLE1 struct files
////////////////////////////////////////////////////////////
bool RagTime5Document::findZones(MWAWEntry const &entry)
{
  libmwaw::DebugStream f;
  MWAWInputStreamPtr input = m_parser->getInput();
  long pos=entry.begin();
  if (!input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("RagTime5Document::findZones: main entry seems too bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }

  int n=0;
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  std::shared_ptr<RagTime5Zone> actualZone, actualChildZone; // the actual zone
  while (!input->isEnd()) {
    pos=input->tell();
    if (pos>=entry.end()) break;
    auto level=static_cast<int>(input->readULong(1));
    if (level==0x18) {
      while (input->tell()<entry.end()) {
        if (input->readULong(1)==0xFF)
          continue;
        input->seek(-1, librevenge::RVNG_SEEK_CUR);
        break;
      }
      ascii().addPos(pos);
      ascii().addNote("_");
      continue;
    }
    f.str("");
    // create a new zone, set the default input and default ascii file
    std::shared_ptr<RagTime5Zone> zone(new RagTime5Zone(input, ascii()));
    zone->m_defPosition=pos;
    zone->m_level=level;
    // level=3: 0001, 59-78 + sometimes g4=[_,1]
    if (pos+4>entry.end() || level < 1 || level > 3) {
      zone->m_extra=f.str();
      if (n++==0)
        f << "Entries(Zones)[1]:";
      else
        f << "Zones-" << n << ":";
      f << *zone << "###";
      MWAW_DEBUG_MSG(("RagTime5Document::findZones: find unknown level\n"));
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      break;
    }
    for (int i=0; i<4-level; ++i) {
      zone->m_idsFlag[i]=static_cast<int>(input->readULong(2)); // alway 0/1?
      zone->m_ids[i]=static_cast<int>(input->readULong(2));
    }
    bool ok=true;
    do {
      auto type2=static_cast<int>(input->readULong(1));
      switch (type2) {
      case 4: // always 0, 1
      case 0xa: // always 0, 0: never seens in v5 but frequent in v6
      case 0xb: { // find in some pc file
        ok = input->tell()+4+(type2==4 ? 1 : 0)<=entry.end();
        if (!ok) break;
        int data[2];
        for (int &i : data)
          i=static_cast<int>(input->readULong(2));
        if (type2==4) {
          if (data[0]==0 && data[1]==1)
            f << "selected,";
          else if (data[0]==0)
            f << "#selected=" << data[1] << ",";
          else
            f << "#selected=[" << data[0] << "," << data[1] << "],";
        }
        else
          f << "g" << std::hex << type2 << std::dec << "=[" << data[0] << "," << data[1] << "],";
        break;
      }
      case 5:
      case 6: { // 6 entry followed by other data
        ok = input->tell()+8+(type2==6 ? 1 : 0)<=entry.end();
        if (!ok) break;
        MWAWEntry zEntry;
        zEntry.setBegin(long(input->readULong(4)));
        zEntry.setLength(long(input->readULong(4)));
        zone->m_entriesList.push_back(zEntry);
        break;
      }
      case 9:
        ok=input->tell()<=entry.end();
        break;
      case 0xd: // always 0 || c000
        ok = input->tell()+4<=entry.end();
        if (!ok) break;
        for (int &i : zone->m_variableD)
          i=static_cast<int>(input->readULong(2));
        break;
      case 0x18:
        while (input->tell()<entry.end()) {
          if (input->readULong(1)==0xFF)
            continue;
          input->seek(-1, librevenge::RVNG_SEEK_CUR);
          break;
        }
        ok=input->tell()+1<entry.end();
        break;
      default:
        ok=false;
        MWAW_DEBUG_MSG(("RagTime5Document::findZones: find unknown type2=%d\n", type2));
        f << "type2=" << type2 << ",";
        break;
      }
      if (!ok || (type2&1) || (type2==0xa))
        break;
    }
    while (1);
    switch (zone->m_level) {
    case 1:
      actualZone=zone;
      actualChildZone.reset();
      break;
    case 2:
      if (!actualZone || actualZone->m_childIdToZoneMap.find(zone->m_ids[0])!=actualZone->m_childIdToZoneMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5Document::findZones: can not add child to a zone %d\n", zone->m_ids[0]));
        f << "##badChild";
      }
      else {
        zone->m_parentName=actualZone->getZoneName();
        actualZone->m_childIdToZoneMap[zone->m_ids[0]]=zone;
      }
      actualChildZone=zone;
      break;
    case 3:
      if (!actualChildZone || actualChildZone->m_childIdToZoneMap.find(zone->m_ids[0])!=actualChildZone->m_childIdToZoneMap.end()) {
        // checkme: can happen in 6.0 files after a jpeg picture with level 1, ...
        MWAW_DEBUG_MSG(("RagTime5Document::findZones: can not add child to a zone %d\n", zone->m_ids[0]));
        f << "#noparent";
      }
      else {
        zone->m_parentName=actualChildZone->getZoneName();
        actualChildZone->m_childIdToZoneMap[zone->m_ids[0]]=zone;
      }
      break;
    default:
      break;
    }

    // store 1 level zone (expect the first one which is the main info zone)
    if (!m_state->m_zonesList.empty() && zone->m_level==1) {
      if (m_state->m_dataIdZoneMap.find(zone->m_ids[0])!=m_state->m_dataIdZoneMap.end()) {
        MWAW_DEBUG_MSG(("RagTime5Document::findZonesKind: data zone with id=%d already exists\n", zone->m_ids[0]));
      }
      else
        m_state->m_dataIdZoneMap[zone->m_ids[0]]=zone;
    }

    m_state->m_zonesList.push_back(zone);
    zone->m_extra=f.str();
    f.str("");
    if (n++==0)
      f << "Entries(Zones)[1]:";
    else
      f << "Zones-" << n << ":";
    f << *zone;


    if (!ok) {
      MWAW_DEBUG_MSG(("RagTime5Document::findZones: find unknown data\n"));
      f << "###";
      if (input->tell()!=pos)
        ascii().addDelimiter(input->tell(),'|');
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      break;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// color map
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool RagTime5Document::checkIsSpreadsheet()
{
  if (m_state->m_zonesList.empty() && !findZones(m_state->m_zonesEntry))
    return false;
  if (m_state->m_zonesList.size()<20)
    return false;
  if (!m_state->m_zonesList[0] || !findZonesKind())
    return false;
  if (!parseMainZoneInfoData(*m_state->m_zonesList[0]))
    return false;

  auto dZone=getDataZone(m_state->m_mainClusterId);
  if (!dZone)
    return false;
  updateZone(dZone);
  std::shared_ptr<RagTime5ClusterManager::Cluster> cluster=m_clusterManager->readRootCluster(*dZone);
  if (!cluster)
    return false;
  auto root=std::dynamic_pointer_cast<RagTime5ClusterManager::ClusterRoot>(cluster);
  if (!root || !root->m_listClusterId)
    return false;
  auto lZone=getDataZone(root->m_listClusterId);
  if (!lZone)
    return false;
  updateZone(lZone);
  if (!lZone || lZone->getKindLastPart(lZone->m_kinds[1].empty())!="ItemData" ||
      lZone->m_entry.length()<24 || (lZone->m_entry.length()%8))
    return false;

  MWAWEntry const &entry=lZone->m_entry;
  MWAWInputStreamPtr input=lZone->getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  input->setReadInverted(!lZone->m_hiLoEndian);
  auto N=int(entry.length()/8);
  bool firstFound=false;
  // look a file which begins by a spreadsheet and which has no layout, no other spreadsheet, ...
  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    std::vector<int> listIds;
    if (!m_structManager->readDataIdList(input, 1, listIds) || listIds.empty() || listIds[0]==0) {
      input->seek(pos+8, librevenge::RVNG_SEEK_SET);
      continue;
    }
    auto val=static_cast<int>(input->readULong(2)); // the type
    input->seek(2, librevenge::RVNG_SEEK_CUR);
    bool needCheck=false;
    switch ((val&0xfff3fd7)) {
    case 0: // root
    case 2: // script
    case 0x42: // color pattern
    case 0x104: // pipeline
    case 0x204: // pipeline
    case 0x480: // style
      break;
    case 1: // layout
      return false;
    default:
      needCheck=true;
      break;
    }
    if (!needCheck) continue;
    auto clustZone=getDataZone(listIds[0]);
    if (!clustZone)
      return false;
    updateZone(clustZone);
    int type=m_clusterManager->getClusterType(*clustZone, val);
    if (type==1) // a layout
      return false;
    if ((type&0x40000)==0x40000) { // a shape
      if (!firstFound) {
        if (type!=0x40002) return false; // first is not a spreadsheet
        firstFound=true;
      }
      else if (type==0x40002) // too many spreadsheets
        return false;
    }
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
  }
  if (!firstFound)
    return false;
  return true;
}

bool RagTime5Document::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = RagTime5DocumentInternal::State();

  MWAWInputStreamPtr input = m_parser->getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  if (!input->checkPosition(32)) {
    MWAW_DEBUG_MSG(("RagTime5Document::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(4)!=0x43232b44 || input->readULong(4)!=0xa4434da5
      || input->readULong(4)!=0x486472d7)
    return false;
  int val;
  for (int i=0; i<3; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val!=i) f << "f" << i << "=" << val << ",";
  }
  val=static_cast<int>(input->readLong(2)); // always 0?
  if (val) f << "f3=" << val << ",";
  m_state->m_zonesEntry.setBegin(long(input->readULong(4)));
  m_state->m_zonesEntry.setLength(long(input->readULong(4)));
  if (m_state->m_zonesEntry.length()<137 ||
      !input->checkPosition(m_state->m_zonesEntry.begin()+137))
    return false;
  if (strict && !input->checkPosition(m_state->m_zonesEntry.end()))
    return false;
  val=static_cast<int>(input->readLong(1));
  if (val==1)
    f << "compacted,";
  else if (val)
    f << "g0=" << val << ",";
  val=static_cast<int>(input->readLong(1));
  setVersion(5);
  switch (val) {
  case 0:
    f << "vers=5,";
    break;
  case 4:
    f << "vers=6.5,";
    setVersion(6);
    break;
  default:
    f << "#vers=" << val << ",";
    break;
  }
  for (int i=0; i<2; ++i) {
    val=static_cast<int>(input->readLong(1));
    if (val) f << "g" << i+1 << "=" << val << ",";
  }
  // ok, we can finish initialization
  if (header) {
    bool isSpreadsheet=checkIsSpreadsheet();
    header->reset(MWAWDocument::MWAW_T_RAGTIME, version(), isSpreadsheet ? MWAWDocument::MWAW_K_SPREADSHEET : MWAWDocument::MWAW_K_TEXT);
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  ascii().addPos(input->tell());
  ascii().addNote("_");

  return true;
}

////////////////////////////////////////////////////////////
// send data to the listener
////////////////////////////////////////////////////////////

bool RagTime5Document::sendZones(MWAWListenerPtr listener)
{
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Document::sendZones: can not find the listener\n"));
    return false;
  }
  if (m_state->m_hasLayout)
    m_layoutParser->sendPageContents();
  else {
    MWAW_DEBUG_MSG(("RagTime5Document::sendZones: no layout, try to send the main zones\n"));
    m_clusterManager->sendClusterMainList();
  }
  return true;
}

bool RagTime5Document::sendSpreadsheet(MWAWListenerPtr listener)
{
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Document::sendSpreadsheet: can not find the listener\n"));
    return false;
  }
  std::vector<int> sheetIds=m_spreadsheetParser->getSheetIdList();
  if (sheetIds.size()!=1) {
    MWAW_DEBUG_MSG(("RagTime5Document::sendSpreadsheet: Oops, %d spreadsheets exist\n", int(sheetIds.size())));
    return false;
  }
  return send(sheetIds[0], listener, MWAWPosition());
}

bool RagTime5Document::send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId, int cellId, double totalWidth)
{
  if (m_state->m_sendZoneSet.find(zoneId)!=m_state->m_sendZoneSet.end()) {
    MWAW_DEBUG_MSG(("RagTime5Document::send: argh zone %d is already in the sent set\n", zoneId));
    return false;
  }

  m_state->m_sendZoneSet.insert(zoneId);
  auto type=m_clusterManager->getClusterType(zoneId);
  bool ok=false;
  if (type==RagTime5ClusterManager::Cluster::C_ButtonZone || type==RagTime5ClusterManager::Cluster::C_GraphicZone || type==RagTime5ClusterManager::Cluster::C_PictureZone)
    ok=m_graphParser->send(zoneId, listener, pos);
  else if (type==RagTime5ClusterManager::Cluster::C_TextZone)
    ok=m_textParser->send(zoneId, listener, partId, cellId, totalWidth);
  else if (type==RagTime5ClusterManager::Cluster::C_SpreadsheetZone)
    ok=m_spreadsheetParser->send(zoneId, listener, pos, partId);
  else if (type==RagTime5ClusterManager::Cluster::C_Pipeline)
    ok=m_pipelineParser->send(zoneId, listener, pos, partId, totalWidth);
  m_state->m_sendZoneSet.erase(zoneId);
  if (ok)
    return true;
  static bool first=true;
  if (first) {
    MWAW_DEBUG_MSG(("RagTime5Document::send: not fully implemented\n"));
    first=false;
  }
  return false;
}

void RagTime5Document::flushExtra(MWAWListenerPtr listener, bool onlyCheck)
{
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Document::flushExtra: can not find the listener\n"));
    return;
  }
  m_textParser->flushExtra(onlyCheck);
  m_graphParser->flushExtra(onlyCheck);
  m_spreadsheetParser->flushExtra(onlyCheck);

  // look for unparsed data
  int notRead=0;
  for (auto zone : m_state->m_zonesList) {
    if (!zone || zone->m_isParsed || !zone->m_entry.valid())
      continue;
    ascii().addPos(zone->m_defPosition);
    ascii().addNote("[notParsed]");
    readZoneData(*zone);
    ++notRead;
  }
  if (notRead) {
    MWAW_DEBUG_MSG(("RagTime5Document::flushExtra: find %d/%d unparsed data\n", notRead, static_cast<int>(m_state->m_zonesList.size())));
  }
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
