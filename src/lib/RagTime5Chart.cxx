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

#include "MWAWFont.hxx"
#include "MWAWListener.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5ClusterManager.hxx"

#include "RagTime5Chart.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Chart */
namespace RagTime5ChartInternal
{
//! the setting zone
struct SettingZone {
  //! constructor
  SettingZone()
  {
  }
  /** three list of long: first zone of type?, second list pos to id?, main data link
      the 0 and 1 data zone are directly stored in link.m_longList is data are short, if not in the link
      the 2 zone only contains a link to the settings zone
   */
  RagTime5ClusterManager::Link m_listLinkId[3];
};

//! the unknown third chart zone
struct UnknownZone3 {
  //! constructor
  UnknownZone3()
  {
  }
  /** three list of long: first zone of type?, second list pos to id?, third list of flag
      the data zone are directly stored in link.m_longList is data are short, if not in the link
   */
  RagTime5ClusterManager::Link m_listLinkId[3];
};

//! the unknown ten chart zone
struct UnknownZone10 {
  //! constructor
  UnknownZone10()
  {
  }
  /** three list of long: first zone of type?, second list pos to id?, third list of sub zones
      the data zone are directly stored in link.m_longList is data are short, if not in the link
   */
  RagTime5ClusterManager::Link m_listLinkId[3];
};

//! structure to store chart information in RagTime5ChartInternal
struct Chart {
  //! constructor
  Chart()
    : m_numSeries(0)
    , m_settingZone()
    , m_zone3()
    , m_zone10()
  {
  }
  //! the number of series
  int m_numSeries;
  //! the setting zone
  SettingZone m_settingZone;
  //! the unknown zone3
  UnknownZone3 m_zone3;
  //! the unknown zone10
  UnknownZone3 m_zone10;
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
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ClustListParser::ClustListParser: bad data size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~ClustListParser() final;
  //! returns a cluster name
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }

  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (!m_fieldSize || endPos-pos!=m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }

    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ClustListParser::parse: can not read an cluster id\n"));
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
  ClustListParser(ClustListParser &orig);
  //! copy operator, not implemented
  ClustListParser &operator=(ClustListParser &orig);
};

ClustListParser::~ClustListParser()
{
}

//! Internal: the helper to read a double's cell double
struct DoubleParser final : public RagTime5StructManager::DataParser {
  //! constructor
  DoubleParser()
    : RagTime5StructManager::DataParser("ChartValueDouble")
  {
  }
  //! destructor
  ~DoubleParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::DoubleParser::parse: bad data size\n"));
      return false;
    }
    double res;
    bool isNan;
    if (input->readDouble8(res, isNan)) {
      f << res;
      return true;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (input->readULong(4)==0x7ff01fe0 && input->readULong(4)==0) {
      // some kind of nan ?
      f << "undef,";
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5ChartInternal::DoubleParser::parse: can not read a double\n"));
    f << "##double";
    return true;
  }
};

DoubleParser::~DoubleParser()
{
}

//! Internal: the helper to read a serieType's cell serieType
struct SerieTypeParser final : public RagTime5StructManager::DataParser {
  //! constructor
  SerieTypeParser()
    : RagTime5StructManager::DataParser("ChartSerieType")
  {
  }
  //! destructor
  ~SerieTypeParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::SerieTypeParser::parse: bad data size\n"));
      return false;
    }
    long val=static_cast<int>(input->readULong(4)); // always 1
    if (val!=1) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::SerieTypeParser::parse: find unknown f0 value\n"));
      f << "##f0=" << val << ",";
    }
    auto type=input->readULong(4);
    switch (type) { // todo: find meaning
    case 0x7d01a:
    case 0x16b481a:
    case 0x16b482a:
    case 0x16b48fa:
    case 0x16b601a:
      f << "type=" << RagTime5StructManager::printType(type) << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::SerieTypeParser::parse: find unknown type\n"));
      f << "##type=" << RagTime5StructManager::printType(type) << ",";
      break;
    }
    return true;
  }
};

SerieTypeParser::~SerieTypeParser()
{
}

//! Internal: the helper to read child text box value(title+label)
struct ChildTZoneParser final : public RagTime5StructManager::DataParser {
  //! constructor
  ChildTZoneParser()
    : RagTime5StructManager::DataParser("ChartValueTZone")
  {
  }
  //! destructor
  ~ChildTZoneParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=14) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChildTZoneParser::parse: bad data size\n"));
      return false;
    }
    for (int i=0; i<5; ++i) { // f0=403d, f5=1|8
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    unsigned long id= input->readULong(4);
    if ((id&0xfc000000) != 0x4000000) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChildTZoneParser::parse: textbox sub[id] seems bad\n"));
      f << "#partId[h]=" << (id>>26) << ",";
    }
    id &= 0x3ffffff;
    if (id) f << "subId=" << id << ",";
    return true;
  }
};

ChildTZoneParser::~ChildTZoneParser()
{
}

//! Internal: the helper to read a ZoneUnknown1's cell ZoneUnknown1
struct ZoneUnknown1Parser final : public RagTime5StructManager::DataParser {
  //! constructor
  ZoneUnknown1Parser()
    : RagTime5StructManager::DataParser("ChartUnknown1")
  {
  }
  //! destructor
  ~ZoneUnknown1Parser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=6) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ZoneUnknown1Parser::parse: bad data size\n"));
      return false;
    }
    for (int i=0; i<2; ++i) { // f0: constant in the zone 1|18, f1=10..25
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    auto val=static_cast<int>(input->readULong(2)); // always 0|4
    if (val!=4)
      f << "f2=" << val << ",";
    return true;
  }
};

ZoneUnknown1Parser::~ZoneUnknown1Parser()
{
}

//! Internal: the helper to read a ZoneUnknown3's cell ZoneUnknown3
struct ZoneUnknown3Parser final : public RagTime5StructManager::DataParser {
  //! constructor
  ZoneUnknown3Parser() : RagTime5StructManager::DataParser("ChartUnknown3")
  {
  }
  //! destructor
  ~ZoneUnknown3Parser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=32) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ZoneUnknown3Parser::parse: bad data size\n"));
      return false;
    }
    for (int i=0; i<16; ++i) { // f1=1, f3=800, f8=9
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    return true;
  }
};

ZoneUnknown3Parser::~ZoneUnknown3Parser()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Chart
struct State {
  //! constructor
  State()
    : m_numPages(0)
  {
  }
  //! the number of pages
  int m_numPages;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Chart::RagTime5Chart(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_styleManager(m_document.getStyleManager())
  , m_parserState(m_document.getParserState())
  , m_state(new RagTime5ChartInternal::State)
{
}

RagTime5Chart::~RagTime5Chart()
{ }

int RagTime5Chart::version() const
{
  return m_parserState->m_version;
}

int RagTime5Chart::numPages() const
{
  // TODO IMPLEMENT ME
  MWAW_DEBUG_MSG(("RagTime5Chart::numPages: is not implemented\n"));
  return 0;
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

void RagTime5Chart::flushExtra()
{
  MWAW_DEBUG_MSG(("RagTime5Chart::flushExtra: is not implemented\n"));
}

////////////////////////////////////////////////////////////
// cluster parser
////////////////////////////////////////////////////////////

namespace RagTime5ChartInternal
{
//! low level: the chart cluster data
struct ClusterChart final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterChart()
    : RagTime5ClusterManager::Cluster(C_ChartZone)
    , m_typesLink()
    , m_unknownLink1()
    , m_unknownLink3()
  {
  }
  //! destructor
  ~ClusterChart() final;
  //! some content zone: first a double zone, second link to sub text zone data
  std::vector<RagTime5ClusterManager::Link> m_valuesLink[2];
  //! list of type link
  RagTime5ClusterManager::Link m_typesLink;
  //! unknown link of size 6
  RagTime5ClusterManager::Link m_unknownLink1;
  //! unknown link of size 32
  RagTime5ClusterManager::Link m_unknownLink3;
};

ClusterChart::~ClusterChart()
{
}

//
//! low level: parser of chart cluster
//
struct ChartCParser final : public RagTime5ClusterManager::ClusterParser {
  //! the different field types
  enum Type {
    F_ParentLink, F_Prefs, F_Pref, F_Series, F_Series2, F_Serie, F_SerieTypes, F_Settings, F_Setting, F_Values, F_Values2, F_Value,
    F_DZone1, F_DZones3, F_DZone3, F_DZone5, F_DZone8, F_DZone9, F_DZones10, F_DZone10,
    F_DZoneF12, F_DZoneF70, F_DZoneF226,
    F_ChartList,
    F_UnknZone1, F_UnknZone2,
    F_Unknown
  };
  //! a small structure used to stored a field type
  struct ZoneType {
    //! constructor
    ZoneType()
      : m_type(F_Unknown)
      , m_id(-1)
    {
    }
    //! return the zone type name
    std::string getName() const
    {
      std::stringstream s;
      switch (m_type) {
      case F_ParentLink:
        s << "parent[list]";
        break;
      case F_Prefs: // unsure
        s << "pref[list]";
        break;
      case F_Pref: // unsure
        s << "pref";
        break;
      case F_Series:
        s << "serie[list1]";
        break;
      case F_Series2:
        s << "serie[list2]";
        break;
      case F_Serie:
        s << "serie";
        break;
      case F_SerieTypes:
        s << "serie[types]";
        break;
      case F_Settings:
        s << "setting[list]";
        break;
      case F_Setting:
        s << "setting";
        break;
      case F_ChartList:
        s << "charList";
        break;
      case F_Values:
        s << "value[list1]";
        break;
      case F_Values2: // column value size 18 or 22 or 24 ?
        s << "value[list2]";
        break;
      case F_Value:
        s << "value";
        break;
      case F_DZone1:
        s << "dZone1";
        break;
      case F_DZone5:
        s << "dZone5";
        break;
      case F_DZones3:
        s << "dZone3[list]";
        break;
      case F_DZone3:
        s << "dZone3";
        break;
      case F_DZone8:
        s << "dZone8";
        break;
      case F_DZone9:
        s << "dZone9";
        break;
      case F_DZones10:
        s << "dZone10[list]";
        break;
      case F_DZone10:
        s << "dZone10";
        break;
      case F_DZoneF12: // size 12 or 20
        s << "dZone12";
        break;
      case F_DZoneF70:
        s << "dZone70";
        break;
      case F_DZoneF226:
        s << "dZone226";
        break;
      case F_UnknZone1:
        s << "unkZone1";
        break;
      case F_UnknZone2:
        s << "unkZone2";
        break;
      case F_Unknown:
#if !defined(__clang__)
      default:
#endif
        s << "unknown";
        break;
      }
      if (m_id>=0) s << "[" << m_id << "]";
      return s.str();
    }
    //! the field type
    Type m_type;
    //! the field local id
    int m_id;
  };
  //! constructor
  ChartCParser(RagTime5ClusterManager &parser, int type, libmwaw::DebugFile &ascii)
    : ClusterParser(parser, type, "ClustChart")
    , m_cluster(new ClusterChart)
    , m_chart(new Chart)
    , m_what(-1)
    , m_linkId(-1)
    , m_fieldName("")
    , m_zoneType()
    , m_fieldIdToZoneTypeMap()
    , m_zoneToParseSet()
    , m_asciiFile(ascii)
  {
  }
  //! destructor
  ~ChartCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the current cluster
  std::shared_ptr<ClusterChart> getChartCluster()
  {
    return m_cluster;
  }
  //! return the chart
  std::shared_ptr<Chart> getChart()
  {
    return m_chart;
  }
  //! insert a new zone to be parsed
  void insertZoneToBeParsed(int id, ZoneType const &type, bool canBeDuplicated=false)
  {
    if (canBeDuplicated && m_fieldIdToZoneTypeMap.find(id)!=m_fieldIdToZoneTypeMap.end() &&
        m_fieldIdToZoneTypeMap.find(id)->second.m_type==type.m_type)
      return;
    if (id<0 || m_fieldIdToZoneTypeMap.find(id)!=m_fieldIdToZoneTypeMap.end()) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::insertZoneToParse: oops the zone %d seems bad\n", id));
      return;
    }
    m_fieldIdToZoneTypeMap[id]=type;
    m_zoneToParseSet.insert(id);
  }
  //! try to check the father type
  bool checkFatherType(int id, Type type) const
  {
    if (m_fieldIdToZoneTypeMap.find(id)==m_fieldIdToZoneTypeMap.end() ||
        m_fieldIdToZoneTypeMap.find(id)->second.m_type!=type) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::insertZoneToParse: can not check the father type for %d\n", id));
      return false;
    }
    return true;
  }
  /** returns to new zone to parse. -1: means no preference, 0: means first zone, ...
   */
  int getNewZoneToParse() final
  {
    if (m_zoneToParseSet.empty())
      return -1;
    int id=*(m_zoneToParseSet.begin());
    m_zoneToParseSet.erase(id);
    return id;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    switch (m_zoneType.m_type) {
    case F_Setting:
      if (m_zoneType.m_id<0 || m_zoneType.m_id>=3 ||
          !m_chart->m_settingZone.m_listLinkId[m_zoneType.m_id].empty()) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops setting%d link is already set\n", m_zoneType.m_id));
        m_cluster->m_linksList.push_back(m_link);
        return;
      }
      m_link.m_name=std::string("ChartSetting_")+char('0'+m_zoneType.m_id);
      m_chart->m_settingZone.m_listLinkId[m_zoneType.m_id]=m_link;
      return;
    case F_DZone3:
      if (m_zoneType.m_id<0 || m_zoneType.m_id>=3 ||
          !m_chart->m_zone3.m_listLinkId[m_zoneType.m_id].empty()) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops zone3%d link is already set\n", m_zoneType.m_id));
        m_cluster->m_linksList.push_back(m_link);
        return;
      }
      m_link.m_name=std::string("ChartDZone3_")+char('0'+m_zoneType.m_id);
      m_chart->m_zone3.m_listLinkId[m_zoneType.m_id]=m_link;
      return;
    case F_DZone10:
      if (m_zoneType.m_id<0 || m_zoneType.m_id>=2 ||
          !m_chart->m_zone10.m_listLinkId[m_zoneType.m_id].empty()) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops zone10%d link is already set\n", m_zoneType.m_id));
        m_cluster->m_linksList.push_back(m_link);
        return;
      }
      m_link.m_name=std::string("ChartDZone10_")+char('0'+m_zoneType.m_id);
      m_chart->m_zone10.m_listLinkId[m_zoneType.m_id]=m_link;
      return;
    case F_ParentLink:
      if (m_cluster->m_parentLink.empty())
        m_cluster->m_parentLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops parent link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      return;
    case F_SerieTypes:
      if (m_cluster->m_typesLink.empty())
        m_cluster->m_typesLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops serie types link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      return;
    case F_DZone5: // a link to some data but never fills in my file
      if (m_link.m_fieldSize>0) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: find unexpected not empty zone 5's link\n"));
      }
      m_cluster->m_linksList.push_back(m_link);
      return;
    case F_UnknZone1:
      if (m_cluster->m_unknownLink1.empty())
        m_cluster->m_unknownLink1=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops unknown1 link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      return;
    case F_DZoneF70:
      if (m_cluster->m_unknownLink3.empty())
        m_cluster->m_unknownLink3=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops unknown1 link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      return;
    case F_Pref:
    case F_Prefs:
    case F_Series:
    case F_Series2:
    case F_Settings:
    case F_Values:
    case F_DZone1:
    case F_DZones3:
    case F_DZone8:
    case F_DZone9:
    case F_DZones10:
    case F_DZoneF12:
    case F_Values2:
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::endZone: oops find unexpected link for zone %s\n", m_zoneType.getName().c_str()));
      m_cluster->m_linksList.push_back(m_link);
      return;
    case F_ChartList:
    case F_DZoneF226:
    case F_UnknZone2:
    case F_Serie:
    case F_Value:
    case F_Unknown:
#if !defined(__clang__)
    default:
#endif
      break;
    }

    switch (m_linkId) {
    case 1:
    case 2:
      m_cluster->m_valuesLink[m_linkId-1].push_back(m_link);
      break;
    default:
      m_cluster->m_linksList.push_back(m_link);
      break;
    }
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_linkId=-1;
    m_fieldName="";
    if (N==-5)
      return parseHeaderZone(input,fSz,N,flag,f);
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseZone: expected N value\n"));
      f << "###N=" << N << ",";
      return true;
    }
    m_what=1;
    return parseDataZone(input, fSz, N, flag, f);
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field const &field, int /*m*/, libmwaw::DebugStream &f) final
  {
    if (!m_fieldName.empty())
      f << m_fieldName << ",";

    switch (m_what) {
    case 0: // header
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x3c057) {
        f << "unkn0=[";
        for (auto const id : field.m_longList)
          f << id << ","; // 15
        f << "],";
        break;
      }
      break;
    case 2: // list link
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto &val : field.m_longList) {
          if (val>1000)
            f << std::hex << val << std::dec << ",";
          else
            f << val << ",";
        }
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (m_zoneType.m_type==F_Setting || m_zoneType.m_type==F_DZone3 ||
          m_zoneType.m_type==F_DZone5 || m_zoneType.m_type==F_DZone10) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected list link field\n"));
        f << "###" << field;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      if (m_zoneType.m_type==F_ParentLink) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected list link field\n"));
        f << "###" << field;
        break;
      }
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
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case 3:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) {
        bool listPos=false, canBeDupplicated=false;
        ZoneType zoneType;
        if (m_zoneType.m_type==F_Series || m_zoneType.m_type==F_Series2) {
          listPos=true;
          zoneType.m_type=F_Serie;
          canBeDupplicated=true;
        }
        else if (m_zoneType.m_type==F_Values) {
          listPos=true;
          zoneType.m_type=F_Value;
          canBeDupplicated=true;
        }
        else if (m_zoneType.m_type==F_DZone10 && m_zoneType.m_id==2) {
          listPos=true;
          zoneType.m_type=F_DZoneF226;
          canBeDupplicated=true;
        }
        else if (m_zoneType.m_type==F_Prefs) {
          listPos=true;
          zoneType.m_type=F_Pref;
        }
        if (listPos)
          f << "child=[";
        else
          f << "unkn=[";
        for (size_t j=0; j<field.m_longList.size(); ++j) {
          if (field.m_longList[j]==0)
            f << "_,";
          else if (listPos) {
            f << "F" << field.m_longList[j]-1 << ",";
            zoneType.m_id=int(j);
            insertZoneToBeParsed(int(field.m_longList[j])-1, zoneType,canBeDupplicated);
          }
          else
            f << field.m_longList[j] << ",";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected data4 field\n"));
      f << "###" << field;
      break;
    case 4: // UnknZone1
      if (field.m_type==RagTime5StructManager::Field::T_Long && field.m_fileType==0xcf817) {
        f << "unkn=" << field.m_longValue[0]; // 56|87
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected what=4 field\n"));
      f << "###" << field;
      break;
    case 5: // Pref
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x16c1825) {
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_FieldList && child.m_fileType==0x42040) {
            f << child;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::SpreadsheetCParser::parseField: find unexpected child[fSz=91]\n"));
          f << "##[" << child << "],";
          continue;
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected preferences field\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseField: find unexpected field\n"));
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
    m_zoneType=ZoneType();
    if (m_fieldIdToZoneTypeMap.find(m_dataId)!=m_fieldIdToZoneTypeMap.end())
      m_zoneType=m_fieldIdToZoneTypeMap.find(m_dataId)->second;
    long pos=input->tell();
    m_link.m_N=N;
    int val;
    long linkValues[4];
    std::string mess("");
    if (m_zoneType.m_type==F_Unknown)
      f << "@";
    else
      f << "[F" << m_dataId << "],";
    switch (m_zoneType.m_type) {
    case F_ParentLink:
      if (fSz!=36 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a link for parent list\n"));
        f << "###";
        break;
      }
      if ((m_link.m_fileType[1]&0xFFD7)!=0x10) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the fileType1 seems bad\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      // a cluster zone with field of size 60...
      m_link.m_name="ChartParentLst";
      m_what=2;
      f << m_link << "," << mess;
      // now 2 int
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      break;
    case F_Prefs:
      if (fSz==36) {
        val=static_cast<int>(input->readLong(4));
        if (val) f << "#f0=" << val << ",";
        val=static_cast<int>(input->readLong(4));
        if (val!=0x17db042) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseZone: find unexpected type0\n"));
          f << "#fileType0=" << std::hex << val << std::dec << ",";
        }
        for (int i=0; i<2; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (val) f << "f" << i+1 << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if ((val&0xFFD7)!=0x10) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseZone: find unexpected type1[fSz36]\n"));
          f << "#fileType1=" << std::hex << val << std::dec << ",";
        }
        ZoneType zoneType;
        zoneType.m_type=F_ChartList;
        f << "ids=[";
        for (int i=0; i<3; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (!val) {
            f << "_,";
            continue;
          }
          zoneType.m_id=i;
          insertZoneToBeParsed(val-1, zoneType);
          f << "F" << val-1 << ",";
        }
        f << "],";
        break;
      }
      if (fSz!=29 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a link for prefs list\n"));
        f << "###";
        break;
      }
      m_what=3;
      if (m_link.m_fileType[0]!=0x3c052) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: fileType0 seems odd for prefs list\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << std::dec << ",";
      }
      if ((m_link.m_fileType[1]&0xFFD7)!=0x50) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: fileType1 seems odd for prefs list\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      val=static_cast<int>(input->readLong(1)); // always 1
      if (val!=1)
        f << "f0=" << val << ",";
      break;
    case F_Series: {
      if (fSz!=35 && fSz!=40) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for series list1\n"));
        f << "###";
        break;
      }
      m_what=3;
      if (m_dataId!=4) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the zone id seems bad\n"));
        f << "##zoneId=" << m_dataId << ",";
      }
      auto type=input->readULong(4);
      if (type && type!=0x3c052) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone:fileType0 seems bad\n"));
        f << "##fileType0=" << RagTime5StructManager::printType(type) << ",";
      }
      for (int i=0; i<3; ++i) { // f2=0|16a88a7
        type=input->readULong(4);
        if (type) f << "f" << i << "=" << RagTime5StructManager::printType(type) << ",";
      }
      val=static_cast<int>(input->readULong(2)); // 67|6f
      if ((val&0xFFF7)!=0x67)
        f << "f3=" << val << ",";
      for (int i=0; i<3; ++i) { // always 0,0,0x100
        val=static_cast<int>(input->readULong(2));
        if (val) f << "f" << i+4 << "=" << val << ",";
      }
      if (fSz==40) {
        for (int i=0; i<5; ++i) { // always 0
          val=static_cast<int>(input->readULong(1));
          if (val) f << "f" << i+7 << "=" << val << ",";
        }
      }
      val=static_cast<int>(input->readULong(1)); // always 0
      if (val) f << "g0=" << val << ",";
      for (int i=0; i<2; ++i) { // 1,1-4
        val=static_cast<int>(input->readULong(2));
        if (val) f << "f" << i+4 << "=" << val << ",";
      }
      break;
    }
    case F_Series2: {
      if (fSz!=29 && fSz!=34) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for series list 2\n"));
        f << "###";
        break;
      }
      f << "father=A" << N-1 << ",";
      m_what=3;
      auto type=input->readULong(4);
      if (type!=0x16c2042) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: filetype0 seems bad\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(type) << ",";
      }
      for (int i=0; i<6; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if (val!=0x70) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the filetype1  seems bad\n"));
        f << "###fileType1=" << std::hex << val << std::dec << ",";
      }
      for (int i=0; i<5; ++i) { //g2=0|256
        val=static_cast<int>(input->readLong(fSz==29 ? 1 : 2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      break;
    }
    case F_Settings: { // second zone, no auxiliar data
      if (fSz!=38) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the settings size seems bad\n"));
        f << "###";
        break;
      }
      auto type=input->readULong(4);
      if (type!=0x47040) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone:fileType0 seems bad\n"));
        f << "##fileType0=" << RagTime5StructManager::printType(type) << ",";
      }
      for (int i=0; i<5; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }

      val=static_cast<int>(input->readLong(4)); // often 16
      if (val!=16) f << "unk=" << val << ",";
      ZoneType zoneType;
      zoneType.m_type=F_Setting;
      int listIds[3];
      for (auto &id : listIds) id=static_cast<int>(input->readLong(4));
      for (int i=0; i<3; ++i) {
        if (!listIds[i]) {
          f << "_,";
          continue;
        }
        f << "F" << listIds[i]-1 << ",";
        zoneType.m_id=i;
        insertZoneToBeParsed(listIds[i]-1, zoneType);
      }

      val=static_cast<int>(input->readLong(2)); // always 1
      if (val!=1)
        f << "f10=" << val << ",";
      break;
    }
    case F_DZones3:
    case F_DZones10: {
      if (fSz!=36) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the zone 3 seems odd\n"));
        f << "###";
        break;
      }
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      auto fType=input->readULong(4);
      if (fType && fType!=0x35800) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the file type 0 seems bad\n"));
        f << "##fileType0=" << RagTime5StructManager::printType(fType) << ",";
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readULong(4));
        if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
      }
      fType=input->readULong(4);
      unsigned long const expectedFileType=m_zoneType.m_type==F_DZones3 ? 0 : 0x16a88a7;
      if (fType!=expectedFileType) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the file type1 seems bad\n"));
        f << "##fileType1=" << RagTime5StructManager::printType(fType) << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if (val!=0x10)
        f << "f3=" << val << ",";
      ZoneType zoneType;
      zoneType.m_type= m_zoneType.m_type==F_DZones3 ? F_DZone3 : F_DZone10;
      for (int i=0; i<3; ++i) {
        val=static_cast<int>(input->readULong(4));
        if (!val)
          continue;
        f << "F" << val-1 << ",";
        zoneType.m_id=i;
        insertZoneToBeParsed(val-1, zoneType);
      }
      break;
    }
    case F_Values: {
      if (fSz!=29 && fSz!=34) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for values list 1\n"));
        f << "###";
        break;
      }
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read the values link\n"));
        f << "###link";
        break;
      }
      m_what=3;
      if (m_link.m_fileType[0]!=0x3c052) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected fieldType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      if ((m_link.m_fileType[1]&0xFFD7)!=0x40) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected fieldType1\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (fSz==34) {
        for (int i=0; i<3; ++i) { // f0=100
          val=static_cast<int>(input->readLong(2));
          if (val) f << "f" << i << "=" << val << ",";
        }
      }
      val=static_cast<int>(input->readLong(1)); // always 1
      if (val!=1)
        f << "f0=" << val << ",";
      f << m_link << "," << mess;
      break;
    }
    case F_Values2: { // 18, 22 or 24
      if (fSz<18) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for values list2\n"));
        f << "###";
        break;
      }
      if (!checkFatherType(N-1, F_Serie))
        f << "###";
      f << "father=A" << N-1 << ",";
      auto type=input->readULong(4);
      if (type!=0x7a4a9d && (type&0xFFF00E0)!=0x1fa0000) { // find 0x1fa601e, 0x1fa421c, 0x1faa21d
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: filetype0 seems bad\n"));
        f << "###";
      }
      f << "fileType0=" << RagTime5StructManager::printType(type) << ",";
      int numExtra=int(fSz-10)/4;
      f << "child=[";
      ZoneType zoneType;
      zoneType.m_type=F_Value;
      zoneType.m_id=m_zoneType.m_id;
      for (int i=0; i<numExtra; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        f << "F" << val-1 << ",";
        insertZoneToBeParsed(val-1, zoneType, true);
      }
      f << "],";
      if ((fSz%4)==0) {
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g0=" << val << ",";
      }
      break;
    }
    // simple data
    case F_Pref: {
      if (fSz!=30) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read pref link\n"));
        f << "###link";
        break;
      }
      m_what=5;
      for (int i=0; i<6; ++i) { // f3=1
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto type=input->readULong(4);
      if (type!=0x16a8842) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the filetype0 seems bad\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(type) << ",";
      }
      for (int i=0; i<4; ++i) { // g0=1
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      break;
    }
    case F_ChartList: {
      if (fSz!=28 && fSz!=30) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for chart list link\n"));
        f << "###";
        break;
      }
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read the chart list link\n"));
        f << "###link";
        break;
      }
      m_what=2;
      if ((m_zoneType.m_id==0 &&  m_link.m_fileType[0]!=0x3e800) ||
          (m_zoneType.m_id==1 &&  m_link.m_fileType[0]!=0x35800) ||
          (m_zoneType.m_id==2 &&  m_link.m_fileType[0]!=0x45080)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected fieldType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      if ((m_link.m_fileType[1]&0xFFD7)!=0) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected fieldType1\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      break;
    }
    case F_Serie: {
      if (fSz!=14 && fSz!=116) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected series field size\n"));
        f << "###";
        break;
      }
      for (int i=0; i<4; ++i) { // f2=0|1000
        val=static_cast<int>(input->readULong(2));
        static int const expected[]= {0,0,0,0x400};
        if (val!=expected[i]) f << "f" << i << "=" << std::hex << val << std::dec << ",";
      }
      if (fSz==14) { // empty
        f << "empty,";
        break;
      }
      for (int i=0; i<3; ++i) { // f4=1-23,
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+4 << "=" << val << ",";
      }
      auto zoneId=static_cast<int>(input->readLong(4));
      if (zoneId) {
        f << "F" << zoneId-1 << ",";
        ZoneType zoneType;
        zoneType.m_type=F_Values2;
        zoneType.m_id=m_zoneType.m_id;
        insertZoneToBeParsed(zoneId-1, zoneType);
      }
      for (int i=0; i<3; ++i) { // f4=1-23,
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+7 << "=" << val << ",";
      }
      f << "num=[";
      for (int i=0; i<5; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << "],";
      f << "fl=[";
      for (int i=0; i<6; ++i) { // 0|4,1,0|1|e|68,0|1|6|55,2,8
        val=static_cast<int>(input->readULong(1));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      double res;
      bool isNan;
      long actPos;
      f << "dim=";
      for (int i=0; i<2; ++i) { // always Xx0.2?
        actPos=input->tell();
        if (!input->readDouble8(res, isNan)) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a double\n"));
          f << "###double";
          input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
        }
        else
          f << res;
        f << (i==0 ? "x" : ",");
      }
      val=static_cast<int>(input->readLong(4));
      if (val) f << "f12=" << val << ",";
      actPos=input->tell();
      if (!input->readDouble8(res, isNan)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a double\n"));
        f << "###double,";
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      else
        f << "dim1=" << res << ","; // always 6?
      f << "fl2=[";
      for (int i=0; i<4; ++i) { // 0|10, 0|80, 8,0|20
        val=static_cast<int>(input->readULong(1));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      val=static_cast<int>(input->readLong(2)); // always 0
      if (val) f << "g0=" << val << ",";
      for (int i=0; i<2; ++i) {
        f << "unk" << i << "=[";
        for (int j=0; j<3; ++j) { // always _,_,1d8-218,[138]0
          val=static_cast<int>(input->readULong(j==1 ? 4 : 2));
          if (!val) {
            f << "_,";
            continue;
          }
          if (j==1 && m_zoneType.m_type==F_Serie) {
            f << "F" << val-1 << ",";
            if (i==0 && val==zoneId) continue;
            ZoneType zoneType;
            zoneType.m_type=i==0 ? F_Values2 :  F_Values;
            zoneType.m_id=m_zoneType.m_id;
            insertZoneToBeParsed(val-1, zoneType);
          }
          else
            f << std::hex << val << std::dec << ",";

        }
        f << "],";
      }
      val=static_cast<int>(input->readLong(4));
      if (val) f << "id=" << val << ",";
      val=static_cast<int>(input->readLong(4));
      if (val) {
        f << "unknZone2=F" << val-1 << ",";
        ZoneType zoneType;
        zoneType.m_type=F_UnknZone2;
        zoneType.m_id=m_zoneType.m_id;
        insertZoneToBeParsed(val-1, zoneType);
      }
      for (int i=0; i<6; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (!val) continue;
        f << "g" << i+1 << "=" << val << ",";
      }
      break;
    }
    case F_SerieTypes:
      if (fSz!=34 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read series type link\n"));
        f << "###";
        break;
      }
      if (m_link.m_fileType[0]!=0x3e800) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      if ((m_link.m_fileType[1]&0xFFD7)!=0x10) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected fileType1\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      m_link.m_name="ChartSerieType";
      f << m_link << "," << mess;
      val=static_cast<int>(input->readLong(4));
      if (val) {
        ZoneType zoneType;
        zoneType.m_type=F_Series2;
        f << "serie[list]=F" << val-1 << ",";
        insertZoneToBeParsed(val-1, zoneType);
      }
      break;
    case F_Setting: {
      if (((fSz!=28 || m_zoneType.m_id>=2) && (fSz!=32 || m_zoneType.m_id!=2)) ||
          !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a setting link\n"));
        f << "###link,";
        break;
      }
      m_what=2;
      unsigned long const expectedType=m_zoneType.m_id==0 ? 0x3e800 : m_zoneType.m_id==1 ? 0x35800 : 0x47040;
      if (m_link.m_fileType[0]!=expectedType) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      if (m_zoneType.m_id==2)
        m_link.m_name="settings";
      f << m_link << "," << mess;
      break;
    }
    case F_DZone3: {
      if (fSz!=28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a dZone3 link\n"));
        f << "###link,";
        break;
      }
      m_what=2;
      unsigned long const expectedType=m_zoneType.m_id==0 ? 0x3e800 : 0x35800;
      if (m_link.m_fileType[0]!=expectedType) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      f << m_link << "," << mess;
      break;
    }
    case F_DZone10:
      if (m_zoneType.m_id==2) {
        if (fSz!=29 && fSz!=34) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the size dzone10[2] of seems odd\n"));
          f << "###fSz,";
          break;
        }
        auto type=input->readULong(4);
        if (type!=0x16aa842) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the filetype0 seems bad\n"));
          f << "##fileType0=" << RagTime5StructManager::printType(type) << ",";
        }
        m_what=3;
        for (int i=0; i<6; ++i) { // always 0
          val=static_cast<int>(input->readLong(2));
          if (val) f << "f" << i << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if (val!=0x60 && val!=0x70) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the filetype1  seems bad\n"));
          f << "##fileType1=" << std::hex << val << std::dec << ",";
        }
        for (int i=0; i<5; ++i) { //g2=0|256
          val=static_cast<int>(input->readLong(fSz==29 ? 1 : 2));
          if (val) f << "g" << i << "=" << val << ",";
        }
        break;
      }
      if (fSz!=28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a dZone10 link\n"));
        f << "###link,";
        break;
      }
      m_what=2;
      if (m_link.m_fileType[0]!=(m_zoneType.m_id==0 ? 0x3e800 : 0x35800)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      f << m_link << "," << mess;
      break;
    case F_Value: {
      if (fSz!=50 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a value link\n"));
        f << "###link,";
        break;
      }
      if (m_link.m_fieldSize==8) {
        f << "double,";
        m_link.m_name="ChartValueDouble";
        m_linkId=1;
      }
      else if (m_link.m_fieldSize==14) {
        f << "text[zone],";
        m_link.m_name="ChartValueTZone";
        m_linkId=2;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unknown link\n"));
        f << "###unknown,";
      }
      f << m_link << "," << mess;
      auto type=input->readULong(4);
      if (type && (type&0xFFFD70F)!=0x16b400a) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unknown fileType2\n"));
        f << "###";
      }
      if (type)
        f << "fileType2=" << RagTime5StructManager::printType(type) << ",";
      for (int i=0; i<3; ++i) { // g0=1, g1,g2 small number
        val=static_cast<int>(input->readLong(4));
        if (!val) continue;
        if (i==1) {
          f << "serie=A" << val-1 << ",";
          if (!checkFatherType(val-1, F_Serie))
            f << "###";
        }
        else f << "g" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2)); // 0|21|40|4000
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      val=static_cast<int>(input->readULong(2)); // 1-4
      if (val) f << "g3=" << val << ",";
      break;
    }
    case F_DZone1: // fSz=74|117|119
      if (fSz!=74 && fSz!=117 && fSz!=119) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: the first zone size seems bad\n"));
        f << "###sz";
        break;
      }
      for (int step=0; step<2; ++step) {
        f << "data" << step << "=[";
        for (int i=0; i<3; ++i) { // f2=0|1000
          val=static_cast<int>(input->readLong(2));
          if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
        }
        for (int i=0; i<3; ++i) { // f3=0|d|e, f4=e|f, f5=0|f|10
          val=static_cast<int>(input->readLong(4));
          if (!val) continue;
          ZoneType zoneType;
          zoneType.m_type=F_DZoneF226;
          zoneType.m_id=m_zoneType.m_id;
          insertZoneToBeParsed(val-1, zoneType, true);
          f << "zone226=F" << val-1 << ",";
        }
        if (step==1)
          break;
        for (int i=0; i<2; ++i) { // always 0
          val=static_cast<int>(input->readLong(2));
          if (val) f << "f" << i+5 << "=" << std::hex << val << std::dec << ",";
        }
        for (int i=0; i<4; ++i) {
          auto lVal=long(input->readULong(4));
          static long const expected[]= {0x5ab56, 0x2d5ab, 0x8000, 0x7162c};
          if (lVal!=expected[i])
            f << "#fileType" << i << "=" << std::hex << lVal << std::dec << ",";
        }
        for (int i=0; i<4; ++i) { // g0=-1|0|1d,1e, g1=-1|0|9|a|d, g2=1
          val=static_cast<int>(input->readLong(2));
          if (val) f << "g" << i << "=" << val << ",";
        }
        for (int i=0; i<2; ++i) { // always ccd
          val=static_cast<int>(input->readLong(4));
          if (val!=0xccd) f << "g" << i+3 << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2)); // [04]1[01][137]
        if (val) f<<"fl=" << std::hex << val << std::dec << ",";
        val=static_cast<int>(input->readULong(2));
        if (val!=0xe07) f<<"fl2=" << std::hex << val << std::dec << ",";
        val=static_cast<int>(input->readULong(4));
        if (val) {
          f << "unknZone1=F" << val-1 << ",";
          ZoneType zoneType;
          zoneType.m_type=F_UnknZone1;
          zoneType.m_id=m_zoneType.m_id;
          insertZoneToBeParsed(val-1, zoneType);
        }
        for (int i=0; i<2; ++i) {
          val=static_cast<int>(input->readLong(2));
          if (val) f << "g" << i+5 << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if (val==0xc000)
          f << "fl3*";
        else if (val)
          f << "fl3=" << std::hex << val << std::dec << ",";
        f << "],";
        if (fSz==74)
          break;
      }
      if (fSz==74)
        break;
      f << "num=[";
      for (int i=0; i<12; ++i) { // a decreasing sequence
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << "],";
      val=static_cast<int>(input->readLong(1)); // alway 1 ?
      if (val!=1)
        f << "f0=" << val << ",";
      if (fSz==117)
        break;
      val=static_cast<int>(input->readULong(2)); // 0[01][01]2
      if (val)
        f << "fl2=" << std::hex << val << std::dec << ",";
      break;
    case F_DZone5:
      if (fSz!=32 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read the zone5 link\n"));
        f << "###link";
        break;
      }
      m_what=2;
      m_link.m_type=RagTime5ClusterManager::Link::L_List;
      m_link.m_name="ChartUnknLink5";
      m_link.m_N=N;
      if ((m_link.m_fileType[1]&0xFFD7)!=0x210) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected file type 1\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      f << m_link << "," << mess;
      break;
    case F_DZone8: // fSz=20
    case F_DZone9: // fSz=18
      if ((fSz!=20 || m_zoneType.m_type!=F_DZone8) &&
          (fSz!=18 || m_zoneType.m_type!=F_DZone9)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a data8 or 9 zone\n"));
        f << "###";
        break;
      }
      val=static_cast<int>(input->readULong(4));
      if (val)
        f << "f0=" << val << ",";
      val=static_cast<int>(input->readULong(2)); // 0|b00|1000
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      for (int i=0; i<4; ++i) { // small number
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+1 << "=" << val << ",";
        if (fSz==18 && i==2)
          break;
      }
      break;
    case F_DZoneF12: { // find with size 12 or 20
      if (fSz<12) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected size for zone 12\n"));
        f << "###";
        break;
      }
      val=static_cast<int>(input->readLong(4));
      if (!checkFatherType(val-1, F_DZoneF226))
        f << "###";
      f << "father=A" << val-1 << ",";
      val=static_cast<int>(input->readLong(2)); //  0|300|b00|1000 ...
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      int extra=int(fSz-12)/2;
      for (int i=0; i<extra; ++i) { // smal number
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      break;
    }
    case F_DZoneF226: {
      if (fSz!=226) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read a zone 226\n"));
        f << "###sz";
        break;
      }
      for (int i=0; i<3; ++i) { // f2=0|1000
        val=static_cast<int>(input->readULong(2));
        static int const expected[]= {0,0,0,0x400};
        if (val!=expected[i]) f << "f" << i << "=" << std::hex << val << std::dec << ",";
      }
      bool isType2=false;
      for (int i=0; i<2; ++i) {
        auto type=input->readULong(4);
        if (type && (type&0xFFF000F)!=0x16b000a && (i!=0 || (type&0xFFF000F)!=0x196000a)) {
          MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unknown fileType%d\n", i));
          f << "###";
        }
        f << "fileType" << i << "=" << RagTime5StructManager::printType(type) << ",";
        if (i==1 && type==0x16b684a) {
          // the second seems quite differents
          isType2=true;
          f << "type2,";
        }
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << "fl" << i << "=" << std::hex << val << std::dec << ",";
      }
      float dim[2];
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      f << "dim=" << MWAWVec2f(dim[0], dim[1]) << ",";
      for (int i=0; i<5; ++i) { // f3=1, f4=0|400, f5=0-f
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i+3 << "=" << val << ',';
      }
      f << "num0=[";
      for (int i=0; i<4; ++i) { // 1|12, 1-6, 3, 3
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ',';
        else
          f << ",";
      }
      f << "],";
      val=static_cast<int>(input->readLong(4));
      if (val) {
        f << "F" << val-1 << ",";
        ZoneType zoneType;
        zoneType.m_type=F_DZoneF12;
        zoneType.m_id=m_zoneType.m_id;
        insertZoneToBeParsed(val-1, zoneType);
      }
      f << "num1=[";
      for (int i=0; i<4; ++i) { // X1, X1, X1,5
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << val << ',';
        else
          f << ",";
      }
      f << "],";
      int n=isType2 ? 1 : 5;
      for (int i=0; i<n; ++i) {
        f << "unkn" << i << "=[";
        for (int j=0; j<10; ++j) {
          val=static_cast<int>(input->readULong(1));
          if (val)
            f << std::hex << val << std::dec << ',';
          else
            f << "_,";
        }
        val=static_cast<int>(input->readLong(2)); // 1|5
        if (val!=1)
          f << val << "],";
        else
          f << "_],";
      }
      input->seek(pos+116,librevenge::RVNG_SEEK_SET);
      double res;
      bool isNan;
      f << "val0=[";
      for (int i=0; i<3; ++i) { // checkme: sometimes double sometimes not double...
        if (!input->readDouble8(res, isNan))
          break;
        f << res << ",";
      }
      f << "],";
      input->seek(pos+140,librevenge::RVNG_SEEK_SET);
      for (int i=0; i<4; ++i) {
        val=static_cast<int>(input->readULong(1)); // 0|4
        if (val)
          f << "fl" << i+2 << "=" << val << ",";
      }
      f << "val1=[";
      for (int i=0; i<4; ++i) { // often 0,1,1,1
        if (!input->readDouble8(res, isNan))
          break;
        f << res << ",";
      }
      input->seek(pos+176,librevenge::RVNG_SEEK_SET);
      f << "],";
      int lDim[4];
      for (auto &d : lDim) d=static_cast<int>(input->readLong(4));
      MWAWBox2i box(MWAWVec2i(lDim[0],lDim[1]),MWAWVec2i(lDim[1],lDim[2]));
      if (box!=MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(0,0)))
        f << "dim1=" << box << ",";
      for (int i=0; i<3; ++i) { // g0=1-5, g1=1-14, g2=13|19|fa
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << "g" << i << "=" << val << ",";
      }
      f << "unkn2=[";
      for (int i=0; i<11; ++i) {
        val=static_cast<int>(input->readULong(2));
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      break;
    }
    case F_DZoneF70: // see only in one file, so ...
      if (fSz!=70 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: unexpected zoneF70 size\n"));
        f << "###fSz,";
        break;
      }
      m_link.m_name="ChartUnknown3";
      f << m_link << "," << mess;
      if ((m_link.m_fileType[1]&0xFFD7)!=0x50) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unknown fileType1\n"));
        f << "###fileType1="<<std::hex << m_link.m_fileType[1] << std::dec << ",";;
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(4));
      if (val && val!=0x25544f2c) // %To, ?
        f << "#f2="<<std::hex << val << std::dec << ",";;
      val=static_cast<int>(input->readULong(2)); // 9
      if (val!=9) f << "f3=" << val << ",";
      for (int i=0; i<9; ++i) {
        val=static_cast<int>(input->readULong(2));
        static int const expected[]= {0x6443,0x2554,0x3ee4,0,0,0,0,0x58a5,0x5c85,};
        if (val!=expected[i]) f << "g" << i << "=" << std::hex << val << std::dec << ",";
      }
      for (int i=0; i<6; ++i) { // h3=1, h5=0|800
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "h" << i << "=" << val << ",";
      }
      break;
    case F_UnknZone1:
      if (fSz!=30 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read the unknownZone1 link\n"));
        f << "###link";
        break;
      }
      if (m_link.m_fileType[0]!=0x16cd840) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find odd fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      if (m_link.m_fieldSize!=6) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unexpected field size\n"));
        f << "###field[size]=" << m_link.m_fieldSize << ",";
      }
      m_link.m_name="ChartUnknown1";
      m_what=4;
      f << m_link << "," << mess;
      break;
    case F_UnknZone2:
      if (fSz!=28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: can not read the unknownZone1 link\n"));
        f << "###link";
        break;
      }
      if (m_link.m_fileType[0]!=0x34800) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find odd fileType0\n"));
        f << "###fileType0=" << RagTime5StructManager::printType(m_link.m_fileType[0]) << ",";
      }
      f << m_link << "," << mess;
      m_what=2;
      break;
    case F_Unknown:
#if !defined(__clang__)
    default:
#endif
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseDataZone: find unknown zone type\n"));
      f << "###fSz,";
      break;
    }
    if (m_zoneType.m_type!=F_Unknown)
      m_fieldName=m_zoneType.getName();
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header, fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    m_what=0;
    if (N!=-5 || m_dataId!=0 || (fSz!=331 && fSz!=339)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (m_type>0 && val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) {
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      ZoneType zoneType;
      zoneType.m_type=i==0 ? F_ParentLink : F_Prefs;
      if (i==0)
        f << "parent=F" << val-1 << ",";
      else
        f << "prefs=F" << val-1 << ",";
      insertZoneToBeParsed(val-1, zoneType);
    }
    for (int i=0; i<8; ++i) { // f3=0|d, f7=0|1|2|3|7, f8=3|4, f9=2
      val=static_cast<int>(input->readLong(2));
      if (!val) continue;
      if (i==1) {
        f << "num[series]=" << val << ",";
        m_chart->m_numSeries=val;
      }
      else
        f << "f" << i+2 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 10
    if (val!=0x10)
      f << "fl0=" << val << ",";
    f << "double0=[";
    for (int i=0; i<6; ++i) { // something like [200,150,200,0,0,500,]
      double res;
      bool isNan;
      long actPos=input->tell();
      if (!input->readDouble8(res, isNan)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: can not read a double0\n"));
        f << "##double" << i << ",";
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      else f << res << ",";
    }
    f << "],";
    val=static_cast<int>(input->readLong(1)); // always f
    if (val!=0xf)
      f << "fl1=" << val << ",";
    f << "double1=[";
    for (int i=0; i<2; ++i) { // small number between 0 et 2: some angle?
      double res;
      bool isNan;
      long actPos=input->tell();
      if (!input->readDouble8(res, isNan)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: can not read a double1\n"));
        f << "##double" << i << ",";
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      else f << res << ",";
    }
    f << "],";
    float dim[4];
    for (int i=0; i<2; ++i) dim[i]=float(input->readLong(4))/65536.f;
    f << "dim?=" << MWAWVec2f(dim[0],dim[1]) << ",";
    f << "double2=[";
    int numData=fSz==331 ? 11 : 12;
    for (int i=0; i<1+numData; ++i) { // checkme 500+a matrix (without the last line and/or column)
      double res;
      bool isNan;
      long actPos=input->tell();
      if (!input->readDouble8(res, isNan)) {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: can not read a double1\n"));
        f << "##double" << i << ",";
        input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      }
      else f << res << ",";
    }
    f << "],";
    long pos=input->tell();
    libmwaw::DebugStream f2;
    f2 << "ClustChart-0-A:headerB,";
    f2 << "child=[";
    for (int i=0; i<12; ++i) {
      val=static_cast<int>(input->readLong(4));
      static Type const wh[]= {
        F_DZone1, F_Settings, F_DZones3, F_Series,
        F_DZone5, F_Unknown/*never seen*/, F_SerieTypes,  F_Serie,
        F_DZone8, F_DZone9, F_DZones10, F_Values
      };
      if (!val) continue;
      if (wh[i]!=F_Unknown) {
        ZoneType zoneType;
        zoneType.m_type=wh[i];
        insertZoneToBeParsed(val-1, zoneType);
        f2 << zoneType.getName() << "=F" << val-1 << ",";
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5ChartInternal::ChartCParser::parseHeaderZone: find unknown zone\n"));
        f2 << "###unk" << i << "=F" << val-1 << ",";
      }
    }
    f2 << "],";
    val=static_cast<int>(input->readULong(2)); // [128][8c][045]
    if (val) f2 << "fl2=" << std::hex << val << std::dec << ",";
    for (int i=0; i<3; ++i) { // never seems dim3 so unsure ...
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      MWAWBox2f bdbox(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
      if (bdbox!=MWAWBox2f(MWAWVec2f(0,0),MWAWVec2f(0,0)))
        f2 << "dim" << i+1 << "?=" << bdbox << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 0?
    if (val) f2 << "h0=" << val << ",";
    f2 << "ID?=[";
    for (int i=0; i<2; ++i) f2 << std::hex << input->readULong(4) << std::dec << ",";
    f2 << "],";
    val=static_cast<int>(input->readLong(2)); // 1|6|7
    if (val) f2 << "h1=" << val << ",";
    for (int i=0; i<2; ++i) {
      val=static_cast<int>(input->readULong(2));
      if (val) f2 << "fl" << i+2 << "=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readULong(4));
    if (val) {
      f2 << "dZone70=F" << val-1 << ",";
      ZoneType zoneType;
      zoneType.m_type=F_DZoneF70;
      insertZoneToBeParsed(val-1, zoneType);
    }
    for (int i=0; i<2; ++i) { // h2=small number, h3:1
      val=static_cast<int>(input->readULong(2));
      if (val) f2 << "h" << i+2 << "=" << val << ",";
    }
    m_asciiFile.addPos(pos);
    m_asciiFile.addNote(f2.str().c_str());
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterChart> m_cluster;
  //! the chart
  std::shared_ptr<Chart> m_chart;
  //! a index to know which field is parsed :  0: main, 1: common data, 2: list, 3: sub zone position, 4: unknown1, 5: the preferences
  int m_what;
  //! the link id: 1: value double, 2: value text zone
  int m_linkId;
  //! the actual field name
  std::string m_fieldName;
  //! the current zone type
  ZoneType m_zoneType;
  //! the list of id to zone type map
  std::map<int, ZoneType> m_fieldIdToZoneTypeMap;
  //! the list of know zone remaining to be parsed
  std::set<int> m_zoneToParseSet;
  //! the ascii file
  libmwaw::DebugFile &m_asciiFile;
private:
  //! copy constructor (not implemented)
  ChartCParser(ChartCParser const &orig) = delete;
  //! copy operator (not implemented)
  ChartCParser &operator=(ChartCParser const &orig) = delete;
};

ChartCParser::~ChartCParser()
{
}

}

std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Chart::readChartCluster(RagTime5Zone &zone, int zoneType)
{
  std::shared_ptr<RagTime5ClusterManager> clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Chart::readChartCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5ChartInternal::ChartCParser parser(*clusterManager, zoneType, zone.ascii());
  if (!clusterManager->readCluster(zone, parser) || !parser.getChartCluster() || !parser.getChart()) {
    MWAW_DEBUG_MSG(("RagTime5Chart::readChartCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  auto cluster=parser.getChartCluster();
  auto chart=parser.getChart();
  m_document.checkClusterList(cluster->m_clusterIdsList);

  // setting zone
  for (int i=0; i<3; ++i) {
    RagTime5ClusterManager::Link &link=chart->m_settingZone.m_listLinkId[i];
    if (link.empty())
      continue;
    if (i<2)
      m_document.readLongList(link, link.m_longList);
    else {
      RagTime5StructManager::FieldParser defaultParser("Settings");
      m_document.readStructZone(link, defaultParser, 0);
    }
  }
  // unknown zone3
  for (auto &link : chart->m_zone3.m_listLinkId) {
    if (link.empty())
      continue;
    m_document.readLongList(link, link.m_longList);
  }
  // unknown zone10
  for (int i=0; i<2; ++i) { // normally, we must already have use listLinkId[2]
    auto &link=chart->m_zone10.m_listLinkId[i];
    if (link.empty())
      continue;
    m_document.readLongList(link, link.m_longList);
  }
  if (!cluster->m_dataLink.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Chart::readChartCluster: oops do not how to parse the main data\n"));
  }
  if (!cluster->m_parentLink.empty()) {
    RagTime5ChartInternal::ClustListParser linkParser(*clusterManager, 60, "ChartParentLst");
    m_document.readListZone(cluster->m_parentLink, linkParser);
    m_document.checkClusterList(linkParser.m_clusterList);
  }
  if (!cluster->m_typesLink.empty()) {
    RagTime5ChartInternal::SerieTypeParser serieTypeParser;
    m_document.readFixedSizeZone(cluster->m_typesLink, serieTypeParser);
  }
  for (int i=0; i<2; ++i) {
    for (auto const &lnk : cluster->m_valuesLink[i]) {
      if (i==0) {
        RagTime5ChartInternal::DoubleParser doubleParser;
        m_document.readFixedSizeZone(lnk, doubleParser);
      }
      else { // argh, where is the textZoneId ?
        RagTime5ChartInternal::ChildTZoneParser textParser;
        m_document.readFixedSizeZone(lnk, textParser);
      }
    }
  }
  if (!cluster->m_unknownLink1.empty()) {
    RagTime5ChartInternal::ZoneUnknown1Parser zone1Parser;
    m_document.readFixedSizeZone(cluster->m_unknownLink1, zone1Parser);
  }
  if (!cluster->m_unknownLink3.empty()) {
    RagTime5ChartInternal::ZoneUnknown3Parser zone3Parser;
    m_document.readFixedSizeZone(cluster->m_unknownLink3, zone3Parser);
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
    s << "Chart_data" << lnk.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(lnk.m_name.empty() ? s.str() : lnk.m_name);
    m_document.readFixedSizeZone(lnk, defaultParser);
  }

  return cluster;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
