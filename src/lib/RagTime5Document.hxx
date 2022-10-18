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

#ifndef RAGTIME_5_DOCUMENT
#  define RAGTIME_5_DOCUMENT

#include <map>
#include <string>
#include <set>
#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

#include "RagTime5StructManager.hxx"
#include "RagTime5ClusterManager.hxx"

namespace RagTime5DocumentInternal
{
struct DocInfoFieldParser;
struct State;
class SubDocument;
}

class RagTime5Chart;
class RagTime5Formula;
class RagTime5Graph;
class RagTime5Layout;
class RagTime5Parser;
class RagTime5Pipeline;
class RagTime5SSParser;
class RagTime5Spreadsheet;
class RagTime5StructManager;
class RagTime5StyleManager;
class RagTime5Text;
class RagTime5Zone;
class RagTime5ClusterManager;

/** \brief the main class to read a RagTime v5 file
 *
 *
 *
 */
class RagTime5Document
{
  friend class RagTime5Chart;
  friend class RagTime5Formula;
  friend class RagTime5Graph;
  friend class RagTime5Layout;
  friend class RagTime5Parser;
  friend class RagTime5Pipeline;
  friend class RagTime5Spreadsheet;
  friend class RagTime5SSParser;
  friend class RagTime5StructManager;
  friend class RagTime5Text;
  friend class RagTime5ClusterManager;
  friend struct RagTime5DocumentInternal::DocInfoFieldParser;
  friend class RagTime5StyleManager;
  friend class RagTime5DocumentInternal::SubDocument;

public:
  //! constructor
  explicit RagTime5Document(MWAWParser &parser);
  //! destructor
  ~RagTime5Document();

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false);
  //! returns the main parser
  MWAWParser &getMainParser()
  {
    return *m_parser;
  }

protected:
  //! inits all internal variables
  void init();

  //
  // interface
  //

  //! returns the document number of page
  int numPages() const;
  /** updates the page span list */
  void updatePageSpanList(std::vector<MWAWPageSpan> &spanList);
  /** returns the document meta data */
  librevenge::RVNGPropertyList getDocumentMetaData() const;
  //! returns the parser state
  MWAWParserStatePtr getParserState()
  {
    return m_parserState;
  }
  //! returns the document version
  int version() const;
  //! sets the document version
  void setVersion(int vers);

  //! a DebugFile used to write what we recognize when we parse the document
  libmwaw::DebugFile &ascii()
  {
    return getParserState()->m_asciiFile;
  }

  //! returns the zone corresponding to a data id (or 0)
  std::shared_ptr<RagTime5Zone> getDataZone(int dataId) const;
  /** returns the cluster type corresponding to zone id or C_Unknown (if the zone is not a cluster or was not parsed)  */
  RagTime5ClusterManager::Cluster::Type getClusterType(int zId) const;
  //! try to return the container's type corresponding to an id
  RagTime5ClusterManager::Cluster::Type getPipelineContainerType(int pipelineId) const;

  //! returns the cluster manager
  std::shared_ptr<RagTime5ClusterManager> getClusterManager();
  //! returns the structure manager
  std::shared_ptr<RagTime5StructManager> getStructManager();
  //! returns the style manager
  std::shared_ptr<RagTime5StyleManager> getStyleManager();
  //! returns the formula parser
  std::shared_ptr<RagTime5Formula> getFormulaParser();
  //! returns the graphic parser
  std::shared_ptr<RagTime5Graph> getGraphParser();
  //! returns the spreadsheet parser
  std::shared_ptr<RagTime5Spreadsheet> getSpreadsheetParser();

  //! try to read a button cluster (via the graphic manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readButtonCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a chart cluster (via the spreadsheet manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readChartCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a graphic cluster (via the graphic manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readGraphicCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a layout cluster (via the layout manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readLayoutCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a pipeline cluster (via the pipeline manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readPipelineCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a picture cluster (via the graphic manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readPictureCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a spreadsheet cluster (via the spreadsheet manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readSpreadsheetCluster(RagTime5Zone &zone, int zoneType);
  //! try to read a text cluster (via the text manager)
  std::shared_ptr<RagTime5ClusterManager::Cluster> readTextCluster(RagTime5Zone &zone, int zoneType);

  //! try to send the different zones
  bool sendZones(MWAWListenerPtr listener);
  //! try to send the spreadsheet (assuming there is only one spreadsheet)
  bool sendSpreadsheet(MWAWListenerPtr listener);
  //! try to send a cluster zone (mainly unimplemented)
  bool send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId=0, int cellId=0, double totalWidth=-1);
  //! try to send a button content as text
  bool sendButtonZoneAsText(MWAWListenerPtr listener, int buttonId);
  //! adds a new page
  void newPage(int number, bool softBreak);

  //! finds the different objects zones
  bool createZones();
  //! try to find the list of zones (and stores them in a list)
  bool findZones(MWAWEntry const &entry);
  //! try to find the zone's kind
  bool findZonesKind();
  //! try to update a zone: information + input
  bool updateZone(std::shared_ptr<RagTime5Zone> &zone);
  //! try to update a zone: create a new input if the zone is stored in different positions, ...
  bool updateZoneInput(RagTime5Zone &zone);
  //! try to read the zone data
  bool readZoneData(RagTime5Zone &zone);
  //! try to unpack a zone
  bool unpackZone(RagTime5Zone &zone, MWAWEntry const &entry, std::vector<unsigned char> &data);
  //! try to unpack a zone
  bool unpackZone(RagTime5Zone &zone);

  //! try to read the main zone info zone and the main cluster(and child)
  bool useMainZoneInfoData();
  //! try to parse the zoneInfo child
  bool parseMainZoneInfoData(RagTime5Zone const &zone);

  //! check if the document is a spreadsheet
  bool checkIsSpreadsheet();

  //! try to read a cluster zone
  bool readClusterZone(RagTime5Zone &zone, int type=-1);
  //! try to read a cluster link zone
  bool readClusterLinkList(RagTime5Zone &zone,
                           RagTime5ClusterManager::Link const &link,
                           std::vector<RagTime5StructManager::ZoneLink> &listLinks);
  //! try to read a cluster list link zone
  bool readClusterLinkList(RagTime5ClusterManager::Link const &link,
                           std::vector<RagTime5StructManager::ZoneLink> &list, std::string const &name="");

  //! try to read a string zone ( zone with id1=21,id2=23:24)
  bool readString(RagTime5Zone &zone, std::string &string);
  //! try to read a unicode string zone
  bool readUnicodeString(RagTime5Zone &zone, std::string const &what="");
  //! try to read a int/long zone data
  bool readLongListWithSize(int dataId, int fSz, std::vector<long> &list, std::string const &zoneName="");
  //! try to read a positions zone in data
  bool readPositions(int posId, std::vector<long> &listPosition);
  //! try to read/get the list of long of a L_LongList
  bool readLongList(RagTime5ClusterManager::Link const &link, std::vector<long> &list);
  //! try to read a list of unicode string zone
  bool readUnicodeStringList(RagTime5ClusterManager::NameLink const &link, std::map<int, librevenge::RVNGString> &idToStringMap);

  //! try to read the document version zone
  bool readDocumentVersion(RagTime5Zone &zone);
  //! try to read the main cluster
  bool readClusterRootData(RagTime5ClusterManager::ClusterRoot &cluster);
  //! try to read the main doc info cluster data
  bool readDocInfoClusterData(RagTime5Zone &zone, MWAWEntry const &entry);
  //! try to read a script comment zone
  bool readScriptComment(RagTime5Zone &zone);
  //! try to read the cluster with contains main graphic object properties
  bool readClusterGProp(RagTime5ClusterManager::Cluster &cluster);
  //! try to read the unknown clusterC data
  bool readUnknownClusterCData(RagTime5ClusterManager::Cluster &cluster);

  //! try to read a structured zone
  bool readStructZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::FieldParser &parser, int headerSz, RagTime5ClusterManager::NameLink *nameLink=nullptr);
  //! try to read a data in a structured zone
  bool readStructData(RagTime5Zone &zone, long endPos, int n, int headerSz,
                      RagTime5StructManager::FieldParser &parser, librevenge::RVNGString const &dataName);

  //! try to read a list zone
  bool readListZone(RagTime5ClusterManager::Link const &link);
  //! try to read a list zone
  bool readListZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::DataParser &parser);
  //! try to read a fixed size zone
  bool readFixedSizeZone(RagTime5ClusterManager::Link const &link, std::string const &name);
  //! try to read a fixed size zone
  bool readFixedSizeZone(RagTime5ClusterManager::Link const &link, RagTime5StructManager::DataParser &parser);

  //! try to read a cluster child list
  bool readChildList(RagTime5ClusterManager::Link const &link, std::vector<RagTime5StructManager::ZoneLink> &childList, bool findN=false);
  //! check a cluster list
  bool checkClusterList(std::vector<RagTime5StructManager::ZoneLink> const &list);
  //! check a cluster list
  bool checkClusterList(std::vector<int> const &list);
  //! flush unsent zone (debugging function)
  void flushExtra(MWAWListenerPtr listener, bool onlyCheck=false);

protected:
  //
  // data
  //
  //! the main parser
  MWAWParser *m_parser;
  //! the parser state
  std::shared_ptr<MWAWParserState> m_parserState;
  //! the state
  std::shared_ptr<RagTime5DocumentInternal::State> m_state;
  //! the chart manager
  std::shared_ptr<RagTime5Chart> m_chartParser;
  //! the formula manager
  std::shared_ptr<RagTime5Formula> m_formulaParser;
  //! the graph manager
  std::shared_ptr<RagTime5Graph> m_graphParser;
  //! the layout manager
  std::shared_ptr<RagTime5Layout> m_layoutParser;
  //! the pipeline manager
  std::shared_ptr<RagTime5Pipeline> m_pipelineParser;
  //! the spreadsheet manager
  std::shared_ptr<RagTime5Spreadsheet> m_spreadsheetParser;
  //! the text manager
  std::shared_ptr<RagTime5Text> m_textParser;

  //! the cluster manager
  std::shared_ptr<RagTime5ClusterManager> m_clusterManager;
  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
  //! the style manager
  std::shared_ptr<RagTime5StyleManager> m_styleManager;

  //
  // the callback
  //

  /** callback used to send a page break */
  typedef void (MWAWParser::* NewPage)(int page, bool softBreak);
  //! callback used to send a footnote
  typedef void (MWAWParser::* SendFootnote)(int zoneId);

  /** the new page callback */
  NewPage m_newPage;
  /** the send footnote callback */
  SendFootnote m_sendFootnote;
private:
  RagTime5Document(RagTime5Document const &)=delete;
  RagTime5Document &operator=(RagTime5Document const &)=delete;

};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
