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

#ifndef RAG_TIME_5_CLUSTER_MANAGER
#  define RAG_TIME_5_CLUSTER_MANAGER

#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "libmwaw_internal.hxx"
#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"

#include "RagTime5StructManager.hxx"

class RagTime5Document;
class RagTime5StructManager;

namespace RagTime5ClusterManagerInternal
{
struct State;
}

//! basic class used to manage RagTime 5/6 zones
class RagTime5ClusterManager
{
public:
  struct Link;
  struct NameLink;

  struct Cluster;
  struct ClusterRoot;
  struct ClusterParser;

  friend struct ClusterParser;

  //! constructor
  explicit RagTime5ClusterManager(RagTime5Document &doc);
  //! destructor
  ~RagTime5ClusterManager();

  //! try to send the root cluster zone
  bool sendClusterMainList();

  //! try to read a cluster zone
  bool readCluster(RagTime5Zone &zone, ClusterParser &parser, bool warnForUnparsed=true);
  //! try to read a cluster zone
  bool readCluster(RagTime5Zone &zone, std::shared_ptr<Cluster> &cluster, int type=-1);
  //! try to read the root cluster zone
  std::shared_ptr<Cluster> readRootCluster(RagTime5Zone &zone);
  //! try to read the cluster root list (in general Data14)
  bool readClusterMainList(ClusterRoot &root, std::vector<int> &list, std::vector<int> const &clusterIdList);

  //! try to read a level 2 child of a cluster (picture resizing, ...)
  bool readClusterGObjProperties(RagTime5Zone &zone);
  //! try to read some unknown cluster
  bool readUnknownClusterC(Link const &link);
  //! try to find a cluster zone type ( heuristic when the cluster type is unknown )
  int getClusterFileType(RagTime5Zone &zone);
  //! returns the local zone type
  int getClusterType(RagTime5Zone &zone, int fileType);
  //! try to return basic information about the header cluster's zone
  bool getClusterBasicHeaderInfo(RagTime5Zone &zone, long &N, long &fSz, long &debHeaderPos);

  // low level

  //! try to read a field header, if ok set the endDataPos positions
  bool readFieldHeader(RagTime5Zone &zone, long endPos, std::string const &headerName, long &endDataPos, long expectedLVal=-99999);
  //! returns "data"+id+"A" ( followed by the cluster type and name if know)
  std::string getClusterDebugName(int id);
  //! define a cluster name (used to associate graph name)
  void setClusterName(int id, librevenge::RVNGString const &name);
  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }

  //! a link to a small zone (or set of zones) in RagTime 5/6 documents
  struct Link {
    //! the link type
    enum Type { L_ClusterLink,
                L_LongList, L_UnicodeList,
                L_FieldsList, L_List,
                L_UnknownClusterC,
                L_Unknown
              };
    //! constructor
    explicit Link(Type type=L_Unknown)
      : m_type(type)
      , m_name("")
      , m_ids()
      , m_N(0)
      , m_fieldSize(0)
      , m_longList()
    {
      for (auto &typ : m_fileType) typ=0;
    }
    //! returns true if all link are empty
    bool empty() const
    {
      if (m_type==L_LongList && !m_longList.empty())
        return false;
      for (auto id : m_ids)
        if (id>0) return false;
      return true;
    }
    //! returns the zone name
    std::string getZoneName() const
    {
      switch (m_type) {
      case L_ClusterLink:
        return "clustLink";
      case L_LongList:
        if (!m_name.empty())
          return m_name;
        else {
          std::stringstream s;
          s << "longList" << m_fieldSize;
          return s.str();
        }
      case L_UnicodeList:
        return "unicodeListLink";
      case L_UnknownClusterC:
        return "unknownClusterC";
      case L_FieldsList:
        if (!m_name.empty())
          return m_name;
        return "fieldsList[unkn]";
      case L_List:
        if (!m_name.empty())
          return m_name;
        break;
      case L_Unknown:
#if !defined(__clang__)
      default:
#endif
        break;
      }
      std::stringstream s;
      if (m_type==L_List)
        s << "ListZone";
      else
        s << "FixZone";
      s << std::hex << m_fileType[0] << "_" << m_fileType[1] << std::dec;
      if (m_fieldSize)
        s << "_" << m_fieldSize;
      s << "A";
      return s.str();
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Link const &z)
    {
      if (z.empty()) return o;
      o << z.getZoneName() << ":";
      size_t numLinks=z.m_ids.size();
      if (numLinks>1) o << "[";
      for (size_t i=0; i<numLinks; ++i) {
        if (z.m_ids[i]<=0)
          o << "_";
        else
          o << "data" << z.m_ids[i] << "A";
        if (i+1!=numLinks) o << ",";
      }
      if (numLinks>1) o << "]";
      if (z.m_fieldSize&0x8000)
        o << "[" << std::hex << z.m_fieldSize << std::dec << ":" << z.m_N << "]";
      else
        o << "[" << z.m_fieldSize << ":" << z.m_N << "]";
      return o;
    }
    //! the link type
    Type m_type;
    //! the link name
    std::string m_name;
    //! the data ids
    std::vector<int> m_ids;
    //! the number of data ( or some flag if m_N & 0x8020)
    int m_N;
    //! the field size
    int m_fieldSize;
    //! the zone type in file
    unsigned long m_fileType[2];
    //! a list of long used to store decal
    std::vector<long> m_longList;
  };

  //! a link to a name zone in RagTime 5/6 documents
  struct NameLink {
    //! default constructor
    NameLink()
      : m_ids()
      , m_N(0)
      , m_decalList()
    {
    }
    //! constructor from list
    explicit NameLink(Link const &lnk)
      : m_ids(lnk.m_ids)
      , m_N(lnk.m_N)
      , m_decalList(lnk.m_longList)
    {
    }
    //! returns true if all link are empty
    bool empty() const
    {
      for (auto id : m_ids)
        if (id>0) return false;
      return true;
    }
    //! the data ids
    std::vector<int> m_ids;
    //! the number of data
    int m_N;
    //! a list of long used to store decal
    std::vector<long> m_decalList;
    //! for unicode list field ids and field id to name
    std::vector<long> m_posToNames[2];
    //! the corresponding link (for big list)
    Link m_posToNamesLinks[2];
  };

  ////////////////////////////////////////////////////////////
  // cluster classes
  ////////////////////////////////////////////////////////////

  //! the cluster data
  struct Cluster {
    //! the cluster type
    enum Type {
      C_ColorPattern, C_FormulaDef, C_FormulaPos, C_Layout, C_Pipeline,
      C_Root, C_ClusterGProp, C_Sound,

      // the main zones
      C_ButtonZone, C_ChartZone, C_GraphicZone, C_PictureZone, C_SpreadsheetZone, C_TextZone,
      // group zones: 6.6
      C_GroupZone,
      // the styles
      C_ColorStyles, C_FormatStyles, C_GraphicStyles, C_TextStyles, C_UnitStyles,
      // unknown clusters
      C_ClusterC,

      C_Empty, C_Unknown
    };
    //! constructor
    explicit Cluster(Type type)
      : m_type(type)
      , m_zoneId(0)
      , m_hiLoEndian(true)
      , m_name("")
      , m_childLink()
      , m_parentLink()
      , m_dataLink()
      , m_nameLink()
      , m_formulaLink()
      , m_settingLinks()
      , m_linksList()
      , m_clusterIdsList()
      , m_isSent(false)
    {
    }
    //! destructor
    virtual ~Cluster();
    //! the cluster type
    Type m_type;
    //! the zone id
    int m_zoneId;
    //! the cluster hiLo endian
    bool m_hiLoEndian;
    //! the cluster name (if know)
    librevenge::RVNGString m_name;
    //! the child link
    Link m_childLink;
    //! the parent link
    Link m_parentLink;
    //! the main data link
    Link m_dataLink;
    //! the name link
    NameLink m_nameLink;
    //! the formula cluster links (def and pos)
    Link m_formulaLink;
    //! the settings links
    std::vector<Link> m_settingLinks;
    //! the link list
    std::vector<Link> m_linksList;
    //! the cluster ids
    std::vector<int> m_clusterIdsList;
    //! true if the cluster was send
    bool m_isSent;
  };

  /** returns the cluster type corresponding to zone id or C_Unknown (if the zone is not a cluster or was not parsed)  */
  Cluster::Type getClusterType(int zId) const;

  //! the cluster for root
  struct ClusterRoot final : public Cluster {
    //! constructor
    ClusterRoot()
      : Cluster(C_Root)
      , m_docInfoLink()
      , m_functionNameLink()
      , m_graphicTypeLink()
      , m_listUnicodeLink()
      , m_listClusterId(0)
      , m_listClusterName()
      , m_linkUnknown()
      , m_fileName("")
    {
      for (auto &id : m_styleClusterIds) id=0;
      for (auto &id : m_clusterIds) id=0;
    }
    //! destructor
    ~ClusterRoot() final;
    //! the list of style cluster ( graph, units, unitsbis, text, format, unknown, graphcolor, col/pattern id)
    int m_styleClusterIds[8];

    //! other cluster id (unknown cluster b, )
    int m_clusterIds[1];

    //! the doc info link
    Link m_docInfoLink;
    //! the function name links
    Link m_functionNameLink;
    //! the graphic type id
    Link m_graphicTypeLink;

    //! a link to a list of unknown index+unicode string
    Link m_listUnicodeLink;
    //! the cluster list id
    int m_listClusterId;
    //! the cluster list id name zone link
    NameLink m_listClusterName;
    //! first the main cluster link, second list of field definition link, third in header
    Link m_listClusterLink[3];

    //! other link: scripts and field 6
    Link m_linkUnknown;

    //! the filename if known
    librevenge::RVNGString m_fileName;
  };

  ////////////////////////////////////////////////////////////
  // parser class
  ////////////////////////////////////////////////////////////

  //! virtual class use to parse the cluster data
  struct ClusterParser {
    //! constructor
    ClusterParser(RagTime5ClusterManager &parser, int type, std::string const &zoneName)
      : m_parser(parser)
      , m_type(type)
      , m_hiLoEndian(true)
      , m_name(zoneName)
      , m_dataId(0)
      , m_link()
    {
    }
    //! destructor
    virtual ~ClusterParser();
    //! return the current cluster
    virtual std::shared_ptr<Cluster> getCluster()=0;
    //! return the debug name corresponding to a zone
    virtual std::string getZoneName() const
    {
      return m_name;
    }
    //! return the debug name corresponding to a cluster
    virtual std::string getZoneName(int n, int m=-1) const
    {
      std::stringstream s;
      s << m_name << "-" << n;
      if (m>=0)
        s << "-B" << m;
      return s.str();
    }
    //! start a new zone
    virtual void startZone()
    {
    }
    //! parse a zone
    virtual bool parseZone(MWAWInputStreamPtr &/*input*/, long /*fSz*/, int /*N*/, int /*flag*/, libmwaw::DebugStream &/*f*/)
    {
      return false;
    }
    //! end of a start zone call
    virtual void endZone()
    {
    }
    //! parse a the data of a zone, n_dataId:m
    virtual bool parseField(RagTime5StructManager::Field const &/*field*/, int /*m*/, libmwaw::DebugStream &/*f*/)
    {
      return false;
    }
    /** returns to new zone to parse. -1: means no preference, 0: means first zone, ...
     */
    virtual int getNewZoneToParse()
    {
      return -1;
    }
    //
    // some tools
    //

    //! return true if N correspond to a file/script name
    bool isANameHeader(long N) const
    {
      return (m_hiLoEndian && N==int(0x80000000)) || (!m_hiLoEndian && N==0x8000);
    }

    //! try to read a link header
    bool readLinkHeader(MWAWInputStreamPtr &input, long fSz, Link &link, long(&values)[4], std::string &message);
    //! returns "data"+id+"A" ( followed by the cluster type and name if know)
    std::string getClusterDebugName(int id);
    //! the main parser
    RagTime5ClusterManager &m_parser;
    //! the cluster type
    int m_type;
    //! zone endian
    bool m_hiLoEndian;
    //! the cluster name
    std::string m_name;
    //! the actual zone id
    int m_dataId;
    //! the actual link
    Link m_link;
  private:
    explicit ClusterParser(ClusterParser const &orig) = delete;
    ClusterParser &operator=(ClusterParser const &orig) = delete;
  };
protected:
  //! the state
  std::shared_ptr<RagTime5ClusterManagerInternal::State> m_state;
  //! the main parser
  RagTime5Document &m_document;
  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
private:
  RagTime5ClusterManager(RagTime5ClusterManager const &orig) = delete;
  RagTime5ClusterManager operator=(RagTime5ClusterManager const &orig) = delete;
};

#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
