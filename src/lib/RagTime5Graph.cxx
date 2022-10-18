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
#include <stack>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWListener.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5Document.hxx"
#include "RagTime5StyleManager.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5ClusterManager.hxx"

#include "RagTime5Graph.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Graph */
namespace RagTime5GraphInternal
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
  //! returns a name with can be used for debugging
  std::string getClusterDebugName(int id) const
  {
    return m_clusterManager.getClusterDebugName(id);
  }

  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    long fSz=endPos-pos;
    if (fSz!=8 && fSz!=14 && fSz!=28) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ClustListParser::parse: bad data size\n"));
      return false;
    }
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ClustListParser::parse: can not read an cluster id\n"));
      f << "##clusterIds,";
      return false;
    }
    if (listIds[0]) {
      m_clusterList.push_back(listIds[0]);
      // a e,2003,200b, ... cluster
      f << getClusterDebugName(listIds[0]) << ",";
    }
    if (fSz==8) { // f0=1, f1=1|2
      for (int i=0; i<2; ++i) {
        auto val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      return true;
    }
    unsigned long lVal=input->readULong(4); // c00..small number
    if ((lVal&0xc0000000)==0xc0000000)
      f << "f0=" << (lVal&0x3fffffff) << "*,";
    else if ((lVal&0xc0000000))
      f << "f0=" << (lVal&0x3fffffff) << "[" << (lVal>>30) << "],";
    else
      f << "f0" << lVal << ",";
    if (fSz==14) {
      for (int i=0; i<3; ++i) { // always 0
        auto val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+1 << "=" << val << ",";
      }
      return true;
    }
    auto val=static_cast<int>(input->readLong(2)); // 0|2
    if (val) f << "f1=" << val << ",";
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    // very often (0x0<->1x1), if not, we often have dim[0]+dim[2]~1 and dim[1]+dim[3]~1, some margins?
    f << "dim=" << MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3])) << ",";
    val=static_cast<int>(input->readLong(2)); // 0|1
    if (val) f << "f2=" << val << ",";
    return true;
  }

  //! the list of read cluster
  std::vector<int> m_clusterList;
private:
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

//! Internal: the helper to read an integer list
struct IntListParser final : public RagTime5StructManager::DataParser {
  //! constructor
  IntListParser(int fieldSz, std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
    , m_fieldSize(fieldSz)
    , m_dataList()
  {
    if (m_fieldSize!=1 && m_fieldSize!=2 && m_fieldSize!=4) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::IntListParser: bad field size\n"));
      m_fieldSize=0;
    }
  }
  //! destructor
  ~IntListParser() final;
  //! try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (m_fieldSize<=0 || (endPos-pos)%m_fieldSize) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::IntListParser::parseData: bad data size\n"));
      return false;
    }
    auto N=int((endPos-pos)/m_fieldSize);
    f << "data=[";
    for (int i=0; i<N; ++i) {
      auto val=static_cast<int>(input->readLong(m_fieldSize));
      f << val << ",";
      m_dataList.push_back(val);
    }
    f << "],";
    return true;
  }

  //! the field size
  int m_fieldSize;
  //! the list of read int
  std::vector<int> m_dataList;

  //! copy constructor, not implemented
  IntListParser(IntListParser &orig) = delete;
  //! copy operator, not implemented
  IntListParser &operator=(IntListParser &orig) = delete;
};

IntListParser::~IntListParser()
{
}

//! Internal: the helper to read a int16 float
struct FloatParser final : public RagTime5StructManager::DataParser {
  //! constructor
  explicit FloatParser(std::string const &zoneName)
    : RagTime5StructManager::DataParser(zoneName)
  {
  }
  //! destructor
  ~FloatParser() final;
  //!  try to parse a data
  bool parseData(MWAWInputStreamPtr &input, long endPos, RagTime5Zone &/*zone*/, int, libmwaw::DebugStream &f) final
  {
    long pos=input->tell();
    if (endPos-pos!=4) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::FloatParser: bad field size\n"));
      return false;
    }
    f << float(input->readLong(4))/65536.f;
    return true;
  }
};

FloatParser::~FloatParser()
{
}

//! Internal: the shape of a RagTime5Graph
struct Shape {
  //! the different shape
  enum Type { S_Line, S_Rect, S_RectOval, S_Circle, S_Pie, S_Arc, S_Polygon, S_Spline, S_RegularPoly, S_TextBox, S_Group, S_Unknown };
  //! constructor
  Shape()
    : m_id(0)
    , m_parentId(0)
    , m_linkId(0)
    , m_partId(0)
    , m_type(S_Unknown)
    , m_dimension()
    , m_shape()
    , m_childIdList()
    , m_flags(0)
    , m_borderId(0)
    , m_graphicId(0)
    , m_transformId(0)
    , m_extra("")
  {
  }
  //! return the shape bdbox
  MWAWBox2f getBdBox() const
  {
    return (m_type==S_TextBox || m_type==S_Group || m_type==S_Unknown) ? m_dimension : m_shape.getBdBox();
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Shape const &shape);
  //! the shape id
  int m_id;
  //! the shape parent id
  int m_parentId;
  //! the link to a zone id
  int m_linkId;
  //! the link part id
  int m_partId;
  //! the shape type
  Type m_type;
  //! the dimension
  MWAWBox2f m_dimension;
  //! the graphic shape
  MWAWGraphicShape m_shape;
  //! the child list (for group)
  std::vector<int> m_childIdList;
  //! the shape flag
  uint32_t m_flags;
  //! the border id
  int m_borderId;
  //! the graphic id
  int m_graphicId;
  //! the transformation id
  int m_transformId;
  //! extra data
  std::string m_extra;
};

std::ostream &operator<<(std::ostream &o, Shape const &shape)
{
  if (shape.m_id) o << "id=" << shape.m_id << ",";
  if (shape.m_parentId) o << "id[parent]=" << shape.m_parentId << ",";
  if (shape.m_linkId) {
    o << "id[link]=" << shape.m_linkId;
    if (shape.m_partId) o << "[" << shape.m_partId << "]";
    o << ",";
  }
  if (!shape.m_childIdList.empty()) {
    o << "child[id]=[";
    for (auto &id : shape.m_childIdList)
      o << id << ",";
    o << "],";
  }
  switch (shape.m_type) {
  case Shape::S_Rect:
    o << "rect,";
    break;
  case Shape::S_RectOval:
    o << "rectoval,";
    break;
  case Shape::S_Circle:
    o << "circle,";
    break;
  case Shape::S_Pie:
    o << "pie,";
    break;
  case Shape::S_Arc:
    o << "arc,";
    break;
  case Shape::S_Group:
    o << "group,";
    break;
  case Shape::S_Line:
    o << "line,";
    break;
  case Shape::S_Polygon:
    o << "poly,";
    break;
  case Shape::S_Spline:
    o << "spline,";
    break;
  case Shape::S_TextBox:
    o << "textbox,";
    break;
  case Shape::S_RegularPoly:
    o << "poly[regular],";
    break;
  // also b and c
  case Shape::S_Unknown:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  o << "dim=" << shape.m_dimension << ",";
  uint32_t fl=shape.m_flags;
  if (fl&1) o << "arrow[beg],";
  if (fl&2) o << "arrow[end],";
  if (fl&0x8) o << "hasTransf,";
  if (fl&0x40) o << "text[flowArround],";
  if (fl&0x200) o << "fixed,";
  if (fl&0x400) o << "hasName,";
  if (fl&0x800) o << "hasDist[bordTB],";
  if (fl&0x1000) o << "hasDist[flowTB],";
  if ((fl&0x4000)==0) o << "noPrint,";
  if (fl&0x8000) o << "hasDist[bordLR],";
  if (fl&0x10000) o << "hasDist[flowLR],";
  if (fl&0x40000) o << "protected,";
  if (fl&0x100000) o << "hasBorder,"; // checkme, maybe related to link data
  fl &= 0xFFEA21B4;
  if (fl) o << "flags=" << std::hex << fl << std::dec << ",";
  if (shape.m_borderId) o << "border[id]=GS" << shape.m_borderId << ",";
  if (shape.m_graphicId) o << "surface[id]=GS" << shape.m_graphicId << ",";
  if (shape.m_transformId) o << "GT" << shape.m_transformId << ",";
  o << shape.m_extra;
  return o;
}

//! the button cluster
struct ClusterButton final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterButton()
    : RagTime5ClusterManager::Cluster(C_ButtonZone)
    , m_item(0)
    , m_buttonType(0)
    , m_idToItemStringMap()
    , m_itemNamesLink()
    , m_scriptComment()
    , m_buttonName("")
  {
  }
  //! destructor
  ~ClusterButton() final;
  //! the chosen item: 0 means none
  int m_item;
  //! the button type: 1:push, 2:radio, 3:checkbox, 4:popup, 5:push(invisible)
  int m_buttonType;
  //! the list of item strings
  std::map<int, librevenge::RVNGString> m_idToItemStringMap;
  //! the item name link
  RagTime5ClusterManager::Link m_itemNamesLink;
  //! the script comment zone
  RagTime5ClusterManager::Link m_scriptComment;
  //! the button name if known
  librevenge::RVNGString m_buttonName;
};

ClusterButton::~ClusterButton()
{
}

//! the shape cluster
struct ClusterGraphic final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterGraphic()
    : RagTime5ClusterManager::Cluster(C_GraphicZone)
    , m_textboxZoneId(0)
    , m_usedZoneId(0)
    , m_transformationLinks()
    , m_dimensionLinks()
    , m_idToShapeMap()
    , m_rootIdList()
    , m_linkList()
  {
    for (auto &n : m_N) n=0;
  }
  //! destructor
  ~ClusterGraphic() final;
  //! number of graph shape(+1) and number of graph used
  int m_N[2];
  //! the main textbox zone id(if defined)
  int m_textboxZoneId;
  //! the graphic used zone id
  int m_usedZoneId;
  //! the list of  transformation's link
  std::vector<RagTime5ClusterManager::Link> m_transformationLinks;
  //! the list of dimension's link
  std::vector<RagTime5ClusterManager::Link> m_dimensionLinks;
  //! two cluster links: list of pipeline: fixedSize=12, , fixedSize=8
  RagTime5ClusterManager::Link m_clusterLinks[2];

  //! the shape list
  std::map<int, std::shared_ptr<Shape> > m_idToShapeMap;
  //! the root id list
  std::vector<int> m_rootIdList;
  //! list of link to other zone
  std::vector<RagTime5StructManager::ZoneLink> m_linkList;
};

ClusterGraphic::~ClusterGraphic()
{
}

//! the picture cluster
struct ClusterPicture final : public RagTime5ClusterManager::Cluster {
  //! constructor
  ClusterPicture()
    : RagTime5ClusterManager::Cluster(C_PictureZone)
    , m_auxilliarLink()
    , m_containerId(0)
    , m_dimension(0,0)
  {
  }
  //! destructor
  ~ClusterPicture() final;
  //! the first auxilliar data
  RagTime5ClusterManager::Link m_auxilliarLink;
  //! the picture container id
  int m_containerId;
  //! the picture dimension
  MWAWVec2f m_dimension;
};

ClusterPicture::~ClusterPicture()
{
}

////////////////////////////////////////
//! Internal: the state of a RagTime5Graph
struct State {
  //! enum used to defined list of classical pict
  enum PictureType { P_Pict, P_Tiff, P_Epsf, P_Jpeg, P_PNG, P_ScreenRep, P_WMF, P_Unknown };

  //! constructor
  State()
    : m_numPages(0)
    , m_shapeTypeIdVector()
    , m_idButtonMap()
    , m_idGraphicMap()
    , m_idPictClusterMap()
    , m_idPictureMap()
  {
  }
  //! the number of pages
  int m_numPages;
  //! try to return a set type
  Shape::Type getShapeType(int id) const;
  //! returns the picture type corresponding to a name
  static PictureType getPictureType(std::string const &type)
  {
    if (type=="TIFF") return P_Tiff;
    if (type=="PICT") return P_Pict;
    if (type=="PNG") return P_PNG;
    if (type=="JPEG") return P_Jpeg;
    if (type=="WMF") return P_WMF;
    if (type=="EPSF") return P_Epsf;
    if (type=="ScreenRep" || type=="Thumbnail") return P_ScreenRep;
    return P_Unknown;
  }

  //! the vector of shape type id
  std::vector<unsigned long> m_shapeTypeIdVector;
  //! map data id to button zone
  std::map<int, std::shared_ptr<ClusterButton> > m_idButtonMap;
  //! map data id to graphic zone
  std::map<int, std::shared_ptr<ClusterGraphic> > m_idGraphicMap;
  //! map data id to picture zone
  std::map<int, std::shared_ptr<ClusterPicture> > m_idPictClusterMap;
  //! map data id to picture
  std::map<int, std::shared_ptr<MWAWEmbeddedObject> > m_idPictureMap;
};

Shape::Type State::getShapeType(int id) const
{
  if (id<=0 || id>static_cast<int>(m_shapeTypeIdVector.size())) {
    MWAW_DEBUG_MSG(("RagTime5GraphInternal::State::getShapeType: find some unknown id %d\n", id));
    return Shape::S_Unknown;
  }
  unsigned long type=m_shapeTypeIdVector[size_t(id-1)];
  switch (type) {
  case 0x14e8842:
    return Shape::S_Rect;
  case 0x14e9042:
    return Shape::S_Circle;
  case 0x14e9842:
    return Shape::S_RectOval;
  case 0x14ea042:
    return Shape::S_Arc;
  case 0x14ea842:
    return Shape::S_TextBox;
  case 0x14eb842:
    return Shape::S_Polygon;
  case 0x14ec842:
    return Shape::S_Line;
  case 0x14ed842:
    return Shape::S_Spline;
  case 0x14f0042:
    return Shape::S_Group;
  case 0x14f8842:
    return Shape::S_Pie;
  case 0x1bbc042:
    return Shape::S_RegularPoly;
  default:
    break;
  }
  MWAW_DEBUG_MSG(("RagTime5GraphInternal::State::getShapeType: find some unknown type %lx\n", type));
  return Shape::S_Unknown;
}

//! Internal: the subdocument of a RagTime5Graph
class SubDocument final : public MWAWSubDocument
{
public:
  // constructor
  SubDocument(RagTime5Graph &parser, MWAWInputStreamPtr const &input, int zoneId, int partId, bool inButton=false, double width=-1)
    : MWAWSubDocument(&parser.m_document.getMainParser(), input, MWAWEntry())
    , m_ragtimeParser(parser)
    , m_id(zoneId)
    , m_subId(partId)
    , m_inButton(inButton)
    , m_width(width)
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
  RagTime5Graph &m_ragtimeParser;
  //! the zone id
  int m_id;
  //! the zone sub id
  int m_subId;
  //! a flag to know if we need to send the button content
  bool m_inButton;
  //! the zone width if known
  double m_width;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("RagTime5GraphInternal::SubDocument::parse: no listener\n"));
    return;
  }

  long pos = m_input->tell();
  if (m_inButton)
    m_ragtimeParser.sendButtonZoneAsText(listener, m_id);
  else
    m_ragtimeParser.sendTextZone(listener, m_id, m_subId, m_width);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_id != sDoc->m_id) return true;
  if (m_subId != sDoc->m_subId) return true;
  if (m_inButton != sDoc->m_inButton) return true;
  if (m_width < sDoc->m_width || m_width > sDoc->m_width) return true;
  if (&m_ragtimeParser != &sDoc->m_ragtimeParser) return true;
  return false;
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Graph::RagTime5Graph(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_styleManager(m_document.getStyleManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5GraphInternal::State)
{
}

RagTime5Graph::~RagTime5Graph()
{
}

int RagTime5Graph::version() const
{
  return m_parserState->m_version;
}

int RagTime5Graph::numPages() const
{
  // TODO IMPLEMENT ME
  MWAW_DEBUG_MSG(("RagTime5Graph::numPages: is not implemented\n"));
  return 0;
}

bool RagTime5Graph::send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos)
{
  if (m_state->m_idGraphicMap.find(zoneId)!=m_state->m_idGraphicMap.end() &&
      m_state->m_idGraphicMap.find(zoneId)->second)
    return send(*m_state->m_idGraphicMap.find(zoneId)->second, listener, pos);
  if (m_state->m_idPictClusterMap.find(zoneId)!=m_state->m_idPictClusterMap.end() &&
      m_state->m_idPictClusterMap.find(zoneId)->second)
    return send(*m_state->m_idPictClusterMap.find(zoneId)->second, listener, pos);
  if (m_state->m_idButtonMap.find(zoneId)!=m_state->m_idButtonMap.end() &&
      m_state->m_idButtonMap.find(zoneId)->second)
    return send(*m_state->m_idButtonMap.find(zoneId)->second, listener, pos, MWAWGraphicStyle::emptyStyle());
  MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find zone %d\n", zoneId));
  return false;
}

bool RagTime5Graph::sendTextZone(MWAWListenerPtr listener, int zId, int pId, double totalWidth)
{
  return m_document.send(zId, listener, MWAWPosition(), pId, 0, totalWidth);
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// main graphic
////////////////////////////////////////////////////////////
bool RagTime5Graph::readGraphicTypes(RagTime5ClusterManager::Link const &link)
{
  if (link.empty() || link.m_ids.size()<2) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTypes: call with no zone\n"));
    return false;
  }
  auto dataZone=m_document.getDataZone(link.m_ids[1]);
  // not frequent, but can happen...
  if (dataZone && !dataZone->m_entry.valid())
    return true;
  if (!dataZone || dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTypes: the first zone seems bad\n"));
    return false;
  }
  long length=dataZone->m_entry.length();
  std::vector<long> decal;
  if (link.m_ids[0])
    m_document.readPositions(link.m_ids[0], decal);
  if (decal.empty())
    decal=link.m_longList;
  if (!length) {
    if (decal.empty()) return true;
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTypes: can not find the type positions for zone %d\n", link.m_ids[0]));
    return false;
  }

  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  dataZone->m_isParsed=true;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(GraphType)[" << *dataZone << "]:";
  input->seek(dataZone->m_entry.begin(), librevenge::RVNG_SEEK_SET);
  ascFile.addPos(dataZone->m_entry.end());
  ascFile.addNote("_");
  if (decal.size()<=1) {
    f << "###";
    ascFile.addPos(dataZone->m_entry.begin());
    ascFile.addNote(f.str().c_str());
    input->setReadInverted(false);
    return false;
  }
  ascFile.addPos(dataZone->m_entry.begin());
  ascFile.addNote(f.str().c_str());
  m_state->m_shapeTypeIdVector.resize(size_t(static_cast<int>(decal.size())-1),0);
  for (size_t i=0; i+1<decal.size(); ++i) {
    auto dLength=int(decal[i+1]-decal[i]);
    if (!dLength) continue;
    long pos=dataZone->m_entry.begin()+decal[i];
    f.str("");
    f  << "GraphType-" << i << ":";
    if (decal[i+1]>length || dLength<16) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTypes: something look bad for decal %d\n", static_cast<int>(i)));
      f << "###";
      if (decal[i]<length) {
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
      }
      continue;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    auto type=input->readULong(4);
    m_state->m_shapeTypeIdVector[i]=type;
    f << "type=" << RagTime5Graph::printType(type) << ",";
    for (int j=0; j<4; ++j) { // always 0
      auto val=static_cast<int>(input->readLong(2));
      if (val)  f << "f" << j << "=" << val << ",";
    }
    auto N=static_cast<int>(input->readULong(4));
    if (N!=(dLength-16)/4) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTypes: the number of data seems bad\n"));
      f << "##N=" << N << ",";
      N=0;
    }
    if (N) {
      f << "unkn=[" << std::hex;
      for (int j=0; j<N; ++j)
        f << input->readULong(4) << ",";
      f << std::dec << "],";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
// colors
////////////////////////////////////////////////////////////
bool RagTime5Graph::readColorPatternZone(RagTime5ClusterManager::Cluster &cluster)
{
  for (const auto &lnk : cluster.m_linksList) {
    auto data=m_document.getDataZone(lnk.m_ids[0]);
    if (!data || !data->m_entry.valid()) {
      if (lnk.m_N && lnk.m_fieldSize) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readColorPatternZone: can not find data zone %d\n", lnk.m_ids[0]));
      }
      continue;
    }
    long pos=data->m_entry.begin();
    data->m_isParsed=true;
    libmwaw::DebugFile &dAscFile=data->ascii();
    libmwaw::DebugStream f;
    std::string what("unkn");
    switch (lnk.m_fileType[1]) {
    case 0x40:
      what="col2";
      break;
    case 0x84040:
      what="color";
      break;
    case 0x16de842:
      what="pattern";
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5Graph::readColorPatternZone: find unexpected field\n"));
      break;
    }

    if (lnk.m_fieldSize<=0 || lnk.m_N*lnk.m_fieldSize!=data->m_entry.length()) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readColorPatternZone: bad fieldSize/N for zone %d\n", lnk.m_ids[0]));
      f << "Entries(GraphCPData)[" << *data << "]:N=" << lnk.m_N << ",fSz=" << lnk.m_fieldSize << ",###" << what;
      dAscFile.addPos(pos);
      dAscFile.addNote(f.str().c_str());
      continue;
    }
    MWAWInputStreamPtr input=data->getInput();
    input->setReadInverted(!data->m_hiLoEndian);
    if (lnk.m_fieldSize!=8 && lnk.m_fieldSize!=10) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readColorPatternZone: find some unknown field size for zone %d\n", lnk.m_ids[0]));
    }
    for (int j=0; j<lnk.m_N; ++j) {
      f.str("");
      if (j==0)
        f << "Entries(GraphCPData)[" << *data << "]:";
      else
        f << "GraphCPData-" << j+1 << ":";
      f << what << ",";
      if (lnk.m_fieldSize==10) {
        auto val=static_cast<int>(input->readLong(2));
        if (val!=1)
          f << "numUsed?=" << val << ",";
        unsigned char col[4];
        for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8); // unsure if rgba, or ?
        f << MWAWColor(col[0],col[1],col[2],col[3]);
      }
      else if (lnk.m_fieldSize==8) {
        MWAWGraphicStyle::Pattern pat;
        pat.m_colors[0]=MWAWColor::white();
        pat.m_colors[1]=MWAWColor::black();
        pat.m_dim=MWAWVec2i(8,8);
        pat.m_data.resize(8);
        for (auto &dta : pat.m_data) dta=static_cast<unsigned char>(input->readULong(1));
        f << pat;
      }
      else
        f << "###";
      dAscFile.addPos(pos);
      dAscFile.addNote(f.str().c_str());
      pos+=lnk.m_fieldSize;
    }
    input->setReadInverted(false);
  }

  return true;
}

////////////////////////////////////////////////////////////
// graphic zone
////////////////////////////////////////////////////////////
bool RagTime5Graph::readGraphicShapes(RagTime5GraphInternal::ClusterGraphic &cluster)
{
  RagTime5ClusterManager::Link const &link= cluster.m_dataLink;
  if (link.m_ids.size()<2 || !link.m_ids[1]) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: can not find main data\n"));
    return true;
  }

  std::map<int, librevenge::RVNGString> idToNameMap;
  if (!cluster.m_nameLink.empty()) {
    m_document.readUnicodeStringList(cluster.m_nameLink, idToNameMap);
    cluster.m_nameLink=RagTime5ClusterManager::NameLink();
  }
  std::vector<long> decal;
  if (link.m_ids[0])
    m_document.readPositions(link.m_ids[0], decal);
  if (decal.empty())
    decal=link.m_longList;
  if (decal.size()<size_t(cluster.m_N[0])) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: the decal array seems bad\n"));
  }
  else if (decal.size()>size_t(cluster.m_N[0])) // rare but can happens
    decal.resize(size_t(cluster.m_N[0]));
  int const dataId=link.m_ids[1];
  auto dataZone=m_document.getDataZone(dataId);
  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
    if (dataZone && decal.size()==1) {
      // a graphic zone with 0 zone is ok...
      dataZone->m_isParsed=true;
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: the data zone %d seems bad\n", dataId));
    return false;
  }
  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(GraphShape)[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  auto N=int(decal.size());
  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!cluster.m_hiLoEndian); // checkme: can be !zone.m_hiLoEndian
  long debPos=entry.begin();
  long endPos=entry.end();
  if (N==0) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: can not find decal list for zone %d, let try to continue\n", dataId));
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    int n=0;
    while (input->tell()+8 < endPos) {
      long pos=input->tell();
      int id=++n;
      librevenge::RVNGString name("");
      if (idToNameMap.find(id)!=idToNameMap.end())
        name=idToNameMap.find(id)->second;
      if (!readGraphicShape(cluster, *dataZone, endPos, id, name)) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
    }
    if (input->tell()!=endPos) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: can not read some block\n"));
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
        MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: can not read the data zone %d-%d seems bad\n", dataId, i));
        continue;
      }
      input->seek(debPos+pos, librevenge::RVNG_SEEK_SET);
      librevenge::RVNGString name("");
      if (idToNameMap.find(i+1)!=idToNameMap.end())
        name=idToNameMap.find(i+1)->second;
      readGraphicShape(cluster, *dataZone, debPos+nextPos, i+1, name);
      if (input->tell()!=debPos+nextPos) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShapes: can not read some block\n"));
          first=false;
        }
        ascFile.addPos(debPos+pos);
        ascFile.addNote("###");
      }
    }
  }
  return true;
}

bool RagTime5Graph::readGraphicShape(RagTime5GraphInternal::ClusterGraphic &cluster, RagTime5Zone &zone,
                                     long endPos, int n, librevenge::RVNGString const &dataName)
{
  MWAWInputStreamPtr input=zone.getInput();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "GraphShape-" << n << ":";
  if (!dataName.empty())
    f << "\"" << dataName.cstr() << "\",";
  if (pos+42>endPos) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: a graphic seems bad\n"));
    f<<"###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::shared_ptr<RagTime5GraphInternal::Shape> shape(new RagTime5GraphInternal::Shape);
  shape->m_flags=static_cast<uint32_t>(input->readULong(4));
  f.str("");
  int val;
  for (int i=0; i<7; ++i) { //  f1=0..16
    val=static_cast<int>(input->readLong(2));
    if (!val) continue;
    if (i==0)
      shape->m_id=val;
    else if (i==1 && shape->m_id) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: main id is already set\n"));
      f << "#shape[id]=" << shape->m_id << ",";
      shape->m_id=val;
    }
    else if (i==1)
      shape->m_id=val;
    else if (i==5)
      shape->m_parentId=val;
    else if (i==6)
      shape->m_linkId=val;
    else
      f << "f" << i << "=" << val << ",";
  }
  val=static_cast<int>(input->readLong(2));
  shape->m_type=m_state->getShapeType(val);
  int typeFieldSize=8;
  switch (shape->m_type) {
  case RagTime5GraphInternal::Shape::S_Line:
  case RagTime5GraphInternal::Shape::S_Rect:
  case RagTime5GraphInternal::Shape::S_Circle:
    break;
  case RagTime5GraphInternal::Shape::S_RectOval:
    typeFieldSize+=8;
    break;
  case RagTime5GraphInternal::Shape::S_Pie:
    typeFieldSize+=10;
    break;
  case RagTime5GraphInternal::Shape::S_Arc:
    typeFieldSize+=10;
    break;
  case RagTime5GraphInternal::Shape::S_Group:
    typeFieldSize=6;
    break;
  case RagTime5GraphInternal::Shape::S_Polygon:
    typeFieldSize=10;
    break;
  case RagTime5GraphInternal::Shape::S_Spline:
    typeFieldSize=18;
    break;
  case RagTime5GraphInternal::Shape::S_TextBox:
    typeFieldSize+=4;
    break;
  case RagTime5GraphInternal::Shape::S_RegularPoly:
    typeFieldSize=16;
    break;
  // also b and c
  case RagTime5GraphInternal::Shape::S_Unknown:
#if !defined(__clang__)
  default:
#endif
    if (val<=0 || val>static_cast<int>(m_state->m_shapeTypeIdVector.size())) {
      f << "###type[id]=" << val << ",";
    }
    else
      f << "type=" << RagTime5Graph::printType(m_state->m_shapeTypeIdVector[size_t(val-1)]) << ",";
  }
  shape->m_transformId=static_cast<int>(input->readLong(2));
  shape->m_graphicId=static_cast<int>(input->readLong(2));
  if (!dataName.empty())
    f << "\"" << dataName.cstr() << "\",";
  float dim[4];
  for (auto &d : dim) d=float(input->readLong(4))/65536.f;
  shape->m_dimension=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3]));
  long dataPos=input->tell();
  if (shape->m_flags&0xB4)
    shape->m_borderId=static_cast<int>(input->readLong(2));

  if (input->tell()+typeFieldSize>endPos) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: the data size seems too short\n"));
    f << "###sz,";

    shape->m_extra=f.str();
    f.str("");
    f << "GraphShape-" << n << ":";
    f << *shape;
    input->seek(dataPos, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  bool ok=true;
  MWAWBox2f box2;
  if (shape->m_type!=RagTime5GraphInternal::Shape::S_Polygon && shape->m_type!=RagTime5GraphInternal::Shape::S_RegularPoly
      && shape->m_type!=RagTime5GraphInternal::Shape::S_Spline && shape->m_type!=RagTime5GraphInternal::Shape::S_Group) {
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    if ((dim[2]<=dim[0] || dim[3]<=dim[1]) && shape->m_type != RagTime5GraphInternal::Shape::S_Line) {
      f << "###";
      ok=false;
    }
    box2=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3]));
    f << "box2=" << box2 << ",";
  }
  switch (shape->m_type) {
  case RagTime5GraphInternal::Shape::S_Rect:
    shape->m_shape=MWAWGraphicShape::rectangle(box2);
    break;
  case RagTime5GraphInternal::Shape::S_Circle:
    shape->m_shape=MWAWGraphicShape::circle(box2);
    break;
  case RagTime5GraphInternal::Shape::S_Line:
    shape->m_shape=MWAWGraphicShape::line(box2[0], box2[1]);
    break;
  case RagTime5GraphInternal::Shape::S_RectOval: {
    for (int i=0; i<2; ++i) dim[i]=float(input->readLong(4))/65536.f;
    MWAWVec2f corner=MWAWVec2f(dim[1],dim[0]);
    f << "round=" << corner << ",";
    shape->m_shape=MWAWGraphicShape::rectangle(box2, corner);
    break;
  }
  case RagTime5GraphInternal::Shape::S_Arc:
  case RagTime5GraphInternal::Shape::S_Pie: {
    float fileAngle[2];
    for (auto &angle : fileAngle) angle=360.f *float(input->readLong(4))/65536.f;
    f << "angle=" << fileAngle[0] << "x" << fileAngle[0]+fileAngle[1] << ",";
    if (fileAngle[1]<0) {
      fileAngle[0]+=fileAngle[1];
      fileAngle[1]*=-1;
    }
    float angle[2] = { 90-fileAngle[0]-fileAngle[1], 90-fileAngle[0] };
    if (angle[1]>360) {
      int numLoop=int(angle[1]/360)-1;
      angle[0]-=float(numLoop*360);
      angle[1]-=float(numLoop*360);
      while (angle[1] > 360) {
        angle[0]-=360;
        angle[1]-=360;
      }
    }
    if (angle[0] < -360) {
      int numLoop=int(angle[0]/360)+1;
      angle[0]-=float(numLoop*360);
      angle[1]-=float(numLoop*360);
      while (angle[0] < -360) {
        angle[0]+=360;
        angle[1]+=360;
      }
    }
    if (shape->m_type==RagTime5GraphInternal::Shape::S_Arc)
      shape->m_shape=MWAWGraphicShape::arc(shape->m_dimension, box2, MWAWVec2f(angle[0],angle[1]));
    else
      shape->m_shape=MWAWGraphicShape::pie(shape->m_dimension, box2, MWAWVec2f(angle[0],angle[1]));

    val=static_cast<int>(input->readLong(2));
    if (val) f << "h1=" << val << ",";
    break;
  }
  case RagTime5GraphInternal::Shape::S_TextBox: {
    unsigned long id=input->readULong(4);
    if ((id&0xfc000000) != 0x4000000) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: textbox subId[high] seems odd\n"));
      f << "#partId[h]=" << (id>>26) << ",";
    }
    id &= 0x3ffffff;
    if (!id || !cluster.m_textboxZoneId) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: find a textbox but can not find textbox zone\n"));
      f << "###partId=" << id << ",";
      shape->m_linkId=0;
      break;
    }
    if (shape->m_linkId) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: link id is already defined\n"));
      f << "###linkId,";
    }
    f << "data" << cluster.m_textboxZoneId << "[" << id << "],";
    shape->m_linkId=cluster.m_textboxZoneId;
    shape->m_partId=static_cast<int>(-id);
    break;
  }
  case RagTime5GraphInternal::Shape::S_Polygon:
  case RagTime5GraphInternal::Shape::S_RegularPoly:
  case RagTime5GraphInternal::Shape::S_Spline: {
    long actPos=input->tell();
    bool isSpline=shape->m_type==RagTime5GraphInternal::Shape::S_Spline;
    if (actPos+10+(isSpline ? 8 : 0)>endPos) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: can not read the polygon data\n"));
      break;
    }
    val=static_cast<int>(input->readLong(2));
    if (val) f << "h1=" << val << ",";
    if (isSpline) {
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      if ((dim[2]<=dim[0] || dim[3]<=dim[1]) && shape->m_type != RagTime5GraphInternal::Shape::S_Line) {
        f << "###";
        ok=false;
      }
      f << "dim2=" << MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3])) << ",";
      shape->m_shape.m_bdBox=ok ? MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[2],dim[3])) : shape->m_dimension;
    }
    for (int i=0; i<2; ++i) { // h2=0|1
      val=static_cast<int>(input->readLong(2));
      if (val) f << "h" << i+2 << "=" << val << ",";
    }
    auto N=static_cast<int>(input->readULong(4));
    actPos=input->tell();
    if (N<0 || N>(endPos-actPos)/8 || endPos-actPos<N*8+(shape->m_type==RagTime5GraphInternal::Shape::S_RegularPoly ? 6 : 0) ||
        N*8+(shape->m_type==RagTime5GraphInternal::Shape::S_RegularPoly ? 6 : 0) < 0) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: can not read the polygon number of points\n"));
      f << "#N=" << N << ",";
      ok=false;
      break;
    }

    if (shape->m_type!=RagTime5GraphInternal::Shape::S_Spline) {
      shape->m_shape.m_type = MWAWGraphicShape::Polygon;
      std::vector<MWAWVec2f> &vertices=shape->m_shape.m_vertices;
      f << "pts=[";
      MWAWBox2f box;
      for (int i=0; i<N; ++i) {
        float coord[2];
        for (auto &c : coord) c= float(input->readLong(4))/65536.f;
        MWAWVec2f pt(coord[0],coord[1]);
        if (i==0)
          box=MWAWBox2f(pt, pt);
        else
          box=box.getUnion(MWAWBox2f(pt,pt));
        vertices.push_back(pt);
        f << pt << ",";
      }
      shape->m_shape.m_bdBox=box;
      f << "],";
    }
    else {
      f << "pts=[";
      std::vector<MWAWVec2f> points;
      for (int i=0; i<N; ++i) {
        float coord[2];
        for (auto &c : coord) c= float(input->readLong(4))/65536.f;
        MWAWVec2f pt(coord[0],coord[1]);
        points.push_back(pt);
        f << pt << ",";
      }
      f << "],";
      if (N%3!=1) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: the number of points seems odd\n"));
        f << "#N=" << N << ",";
        break;
      }
      shape->m_shape.m_type=MWAWGraphicShape::Path;
      auto &path=shape->m_shape.m_path;
      path.push_back(MWAWGraphicShape::PathData('M', points[0],  points[0], points[0]));
      for (size_t i=0; i<size_t(N/3); ++i)
        path.push_back(MWAWGraphicShape::PathData('C', points[3*i+3], points[3*i+1], points[3*i+2]));
    }
    if (shape->m_type!=RagTime5GraphInternal::Shape::S_RegularPoly)
      break;
    // the number of points with define a regular polygon
    f << "N=" << input->readLong(2) << ",";
    val=static_cast<int>(input->readLong(4));
    if (val) f << "rot=" << 360.*double(val)/65536. << ",";
    break;
  }
  case RagTime5GraphInternal::Shape::S_Group: {
    val=static_cast<int>(input->readLong(2)); // always 0?
    if (val) f << "h1=" << val << ",";
    auto N=static_cast<int>(input->readULong(4));
    long actPos=input->tell();
    if (actPos+N*4>endPos) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: can not read the group number of points\n"));
      f << "#N=" << N << ",";
      ok=false;
      break;
    }
    for (int i=0; i<N; ++i)
      shape->m_childIdList.push_back(static_cast<int>(input->readLong(4)));
    break;
  }
  case RagTime5GraphInternal::Shape::S_Unknown:
#if !defined(__clang__)
  default:
#endif
    ok=false;
    break;
  }

  shape->m_extra=f.str();
  f.str("");
  f << "GraphShape-" << n << ":";
  f << *shape;

  if (shape->m_id==0) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: checkme: find some shape with no id\n"));
      first=false;
    }
    f << "#noId,";
  }
  else if (cluster.m_idToShapeMap.find(shape->m_id)!=cluster.m_idToShapeMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicShape: shape %d already exist\n", shape->m_id));
    f << "###duplicatedId,";
  }
  else
    cluster.m_idToShapeMap[shape->m_id]=shape;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return ok;
}

bool RagTime5Graph::readGraphicUsed(int typeId)
{
  if (!typeId)
    return false;

  auto zone=m_document.getDataZone(typeId);
  if (!zone || !zone->m_entry.valid() || (zone->m_entry.length()%10) ||
      zone->getKindLastPart(zone->m_kinds[1].empty())!="ItemData") {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicUsed: the entry of zone %d seems bad\n", typeId));
    return false;
  }
  MWAWEntry entry=zone->m_entry;
  MWAWInputStreamPtr input=zone->getInput();
  input->setReadInverted(!zone->m_hiLoEndian);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugFile &ascFile=zone->ascii();
  libmwaw::DebugStream f;
  zone->m_isParsed=true;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  f << "Entries(GraphUsed)[" << *zone << "]:";
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  auto N=int(entry.length()/10);
  for (int i=1; i<=N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "GraphUsed-" << i << ":";

    auto used=static_cast<int>(input->readLong(4));
    auto id=static_cast<int>(input->readLong(4));
    if (id==0) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(pos+10, librevenge::RVNG_SEEK_SET);
      continue;
    }

    auto subId=static_cast<int>(input->readLong(2));
    if (subId)
      f << "id=" << id << "-" << subId << ",";
    else
      f << "id=" << id << ",";
    if (used!=1)
      f << "used=" << used << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
// transformation
////////////////////////////////////////////////////////////
bool RagTime5Graph::readGraphicTransformations(RagTime5ClusterManager::Link const &link)
{
  if (link.empty() || link.m_ids[0]==0 || link.m_fieldSize<34) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTransformations: can not find the transformation id\n"));
    return false;
  }

  auto dataZone=m_document.getDataZone(link.m_ids[0]);
  if (!dataZone || !dataZone->m_entry.valid() || dataZone->m_entry.length()!=link.m_N*link.m_fieldSize ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData") {
    if (link.m_N==0 && !dataZone->m_entry.valid()) {
      // an empty transformation zone is ok...
      dataZone->m_isParsed=true;
      return true;
    }

    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicTransformations: the transformation zone %d seems bad\n", link.m_ids[0]));
    return false;
  }
  MWAWEntry entry=dataZone->m_entry;
  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);

  dataZone->m_isParsed=true;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(GraphTransform)[" << *dataZone << "]:";
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(entry.end());
  ascFile.addNote("_");

  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  for (int i=0; i<link.m_N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "GraphTransform-GT" << i+1 << ":mat=[";
    for (int j=0; j<9; ++j) {
      if ((j%3)==0) f << "[";
      bool isShort=(j==8) && (link.m_fieldSize==34);
      long val=input->readLong(isShort ? 2 : 4);
      if (!val) f << "_";
      else if (isShort) f << val;
      else f << float(val)/65536.f;
      if ((j%3)==2) f << "]";
      f << ",";
    }
    f << "],";
    if (input->tell()!=pos+link.m_fieldSize)
      ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+link.m_fieldSize, librevenge::RVNG_SEEK_SET);
  }
  input->setReadInverted(false);
  return true;
}

////////////////////////////////////////////////////////////
// picture
////////////////////////////////////////////////////////////
bool RagTime5Graph::readPictureList(RagTime5Zone &zone)
{
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  if (zone.m_name.empty())
    f << "Entries(PictureList)[" << zone << "]:";
  else
    f << "Entries(" << zone.m_name << ")[pictureList," << zone << "]:";
  MWAWEntry &entry=zone.m_entry;
  zone.m_isParsed=true;
  std::vector<int> listIds;
  if (entry.valid()) {
    ascFile.addPos(entry.end());
    ascFile.addNote("_");
    m_document.ascii().addPos(zone.m_defPosition);
    m_document.ascii().addNote("picture[list]");

    if (entry.length()%4) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readPictureList: the entry size seems bad\n"));
      f << "###";
      ascFile.addPos(entry.begin());
      ascFile.addNote(f.str().c_str());
      return false;
    }

    MWAWInputStreamPtr input = zone.getInput();
    input->setReadInverted(!zone.m_hiLoEndian); // checkme never seens
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

    auto N=int(entry.length()/4);
    for (int i=0; i<N; ++i) {
      auto val=static_cast<int>(input->readLong(2)); // always 1
      auto id=static_cast<int>(input->readLong(2));
      if (val==1) {
        f << "Data" << id << ",";
        listIds.push_back(id);
      }
      else if (val)
        f << "#" << i << ":" << val << ",";
    }
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    input->setReadInverted(false);
  }
  else if (zone.m_variableD[0]==1)
    listIds.push_back(zone.m_variableD[1]);
  else {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureList: can not find the list of pictures\n"));
    return false;
  }
  for (int listId : listIds) {
    auto dataZone=m_document.getDataZone(listId);
    if (!dataZone) continue;
    readPictureRep(*dataZone);
  }
  return true;
}

bool RagTime5Graph::readPictureRep(RagTime5Zone &zone)
{
  if (!zone.m_entry.valid() ||
      m_state->getPictureType(zone.getKindLastPart())==RagTime5GraphInternal::State::P_Unknown) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureRep: can not find the picture %d\n", zone.m_ids[0]));
    return false;
  }
  readPictureData(zone);

  for (auto it : zone.m_childIdToZoneMap) {
    std::shared_ptr<RagTime5Zone> child=it.second;
    if (!child || child->m_isParsed) continue;
    child->m_isParsed=true;
    std::string kind=child->getKindLastPart();
    if (kind=="ScreenRepMatchData" || kind=="ScreenRepMatchDataColor") {
      readPictureMatch(*child, kind=="ScreenRepMatchDataColor");
      continue;
    }

    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureRep: find unknown child for picture list zone %d\n", child->m_ids[0]));
    child->addErrorInDebugFile("PictureList");
  }
  return true;
}

bool RagTime5Graph::readPictureContainer(RagTime5Zone &zone)
{
  zone.m_isParsed=true;
  libmwaw::DebugFile &mainAscii=m_document.ascii();
  mainAscii.addPos(zone.m_defPosition);
  mainAscii.addNote("pict[container]");
  if (zone.m_entry.valid()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: find unexpected data in a picture container\n"));
    zone.ascii().addPos(zone.m_entry.begin());
    zone.ascii().addNote("Entries(PictureContainer):###");
  }
  if (zone.m_childIdToZoneMap.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: find a picture container %d without any child\n", zone.m_ids[0]));
    return false;
  }
  std::shared_ptr<MWAWEmbeddedObject> picture(new MWAWEmbeddedObject);
  for (auto cIt : zone.m_childIdToZoneMap) {
    std::shared_ptr<RagTime5Zone> child=cIt.second;
    if (!child) continue;
    child->m_isParsed=true;
    if (cIt.first==8) {
      if (child->m_variableD[0] || child->m_variableD[1]<=0 || child->m_entry.valid()) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: refCount seems odd\n"));
        mainAscii.addPos(child->m_defPosition);
        mainAscii.addNote("###badRef[pictContainer]");
      }
      continue;
    }
    // the screen representation
    if (child->getKindLastPart(child->m_kinds[1].empty())=="ScreenRepList") {
      if (child->m_entry.valid() || (child->m_variableD[0]==1 && child->m_variableD[1])) {
        readPictureList(*child);
        continue;
      }
      // screenrep can be also undefined
      mainAscii.addPos(child->m_defPosition);
      mainAscii.addNote("[empty]");
      continue;
    }
    if (child->getKindLastPart()=="TCubics" && child->m_entry.valid()) {
      libmwaw::DebugFile &ascFile=child->ascii();
      libmwaw::DebugStream f;
      f << "Entries(TCubics):" << *child;
      ascFile.addPos(child->m_entry.begin());
      ascFile.addNote(f.str().c_str());
      ascFile.addPos(child->m_entry.end());
      ascFile.addNote("_");
      continue;
    }
    librevenge::RVNGBinaryData data;
    std::string type;
    if (child->m_entry.valid() && readPictureData(*child, data, type)) {
      if (data.empty())
        continue;
      picture->add(data,type);
      continue;
    }

    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: find unknown child zone\n"));
    mainAscii.addPos(child->m_defPosition);
    mainAscii.addNote("###unknChild[pictContainer]");
  }
  if (picture->m_dataList.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: oops can not find any picture for zone %d\n", zone.m_ids[0]));
  }
  else if (m_state->m_idPictureMap.find(zone.m_ids[0])!=m_state->m_idPictureMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureContainer: a picture already exists for id %d\n", zone.m_ids[0]));
  }
  else
    m_state->m_idPictureMap[zone.m_ids[0]]=picture;
  return true;
}

bool RagTime5Graph::readPictureData(RagTime5Zone &zone)
{
  librevenge::RVNGBinaryData data;
  std::string type;
  return readPictureData(zone, data, type);
}
bool RagTime5Graph::readPictureData(RagTime5Zone &zone, librevenge::RVNGBinaryData &pictData, std::string &pictType)
{
  pictData.clear();
  MWAWEntry const &entry=zone.m_entry;
  if (entry.length()<=40)
    return false;
  auto type=m_state->getPictureType(zone.getKindLastPart());
  bool testForScreenRep=false;
  if (type==RagTime5GraphInternal::State::P_ScreenRep && !zone.m_kinds[1].empty()) {
    type=m_state->getPictureType(zone.getKindLastPart(false));
    if (type==RagTime5GraphInternal::State::P_Unknown)
      type=RagTime5GraphInternal::State::P_ScreenRep;
    else
      testForScreenRep=true;
  }
  if (type==RagTime5GraphInternal::State::P_Unknown)
    return false;
  MWAWInputStreamPtr input = zone.getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long val;
  std::string extension("");
  bool ok=true;
  switch (type) {
  case RagTime5GraphInternal::State::P_Epsf:
    val=long(input->readULong(4));
    if (val!=long(0xc5d0d3c6) && val != long(0x25215053)) {
      ok=false;
      break;
    }
    extension="eps";
    pictType="application/postscript";
#if 0
    // when header==0xc5d0d3c6, we may want to decompose the data
    input->setReadInverted(true);
    MWAWEntry fEntry[3];
    for (int i=0; i<3; ++i) {
      fEntry[i].setBegin(long(input->readULong(4)));
      fEntry[i].setLength(long(input->readULong(4)));
      if (!fEntry[i].length()) continue;
      f << "decal" << i << "=" << std::hex << fEntry[i].begin() << "<->" << fEntry[i].end() << std::dec << ",";
      if (fEntry[i].begin()<0x1c ||fEntry[i].end()>zone.m_entry.length()) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readPictureData: the address %d seems too big\n", i));
        fEntry[i]=MWAWEntry();
        f << "###";
        continue;
      }
    }
    for (int i=0; i<2; ++i) { // always -1,0
      if (input->tell()>=pos+fDataPos)
        break;
      val=static_cast<int>(input->readLong(2));
      if (val!=i-1)
        f << "f" << i << "=" << val << ",";
    }
    // now first fEntry=eps file, second WMF?, third=tiff file
#endif
    break;
  case RagTime5GraphInternal::State::P_Jpeg:
    val=long(input->readULong(2));
    // jpeg format begin by 0xffd8 and jpeg-2000 format begin by 0000 000c 6a50...
    if (val!=0xffd8 && (val!=0 || input->readULong(4)!=0xc6a50 || input->readULong(4)!=0x20200d0a)) {
      ok=false;
      break;
    }
    extension="jpg";
    pictType="image/jpeg";
    break;
  case RagTime5GraphInternal::State::P_Pict:
    input->seek(10, librevenge::RVNG_SEEK_CUR);
    val=long(input->readULong(2));
    if (val!=0x1101 && val !=0x11) {
      ok=false;
      break;
    }
    extension="pct";
    pictType="image/x-pict";
    break;
  case RagTime5GraphInternal::State::P_PNG:
    if (input->readULong(4) != 0x89504e47) {
      ok=false;
      break;
    }
    extension="png";
    pictType="image/png";
    break;
  case RagTime5GraphInternal::State::P_ScreenRep:
    val=long(input->readULong(1));
    if (val!=0x49 && val!=0x4d) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readPictureData: find unknown picture format for zone %d\n", zone.m_ids[0]));
      ok=false;
      break;
    }
    extension="sRep";
    pictType="image/tiff"; // chekme
    break;
  case RagTime5GraphInternal::State::P_Tiff:
    val=long(input->readULong(2));
    if (val!=0x4949 && val != 0x4d4d) {
      ok=false;
      break;
    }
    pictType="image/tiff";
    val=long(input->readULong(2));
    /* find also frequently 4d4d 00dd b300 d61e here ?
       and one time 4d 00 b3 2a d6 */
    if (val!=0x2a00 && val!=0x002a) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("RagTime5Graph::readPictureData: some tiffs seem bad, zone %d, ...\n", zone.m_ids[0]));
        first=false;
      }
      extension="check.tiff";
    }
    else
      extension="tiff";
    break;
  case RagTime5GraphInternal::State::P_WMF:
    if (input->readULong(4)!=0x01000900) {
      ok=false;
      break;
    }
    extension="wmf";
    pictType="image/wmf";
    break;
  // coverity[dead_error_line : FALSE]: intended, needed for avoiding compiler warning
  case RagTime5GraphInternal::State::P_Unknown:
#if !defined(__clang__)
  default:
#endif
    ok=false;
    break;
  }
  if (!ok && testForScreenRep) {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    val=long(input->readULong(1));
    if (val==0x49 || val==0x4d) {
      ok=true;
      MWAW_DEBUG_MSG(("RagTime5Graph::readPictureData: find unknown picture format for zone %d\n", zone.m_ids[0]));
      extension="sRep";
#ifdef DEBUG_WITH_FILES
      type=RagTime5GraphInternal::State::P_ScreenRep;
#endif
    }
  }
  zone.m_isParsed=true;
  libmwaw::DebugStream f;
  f << "picture[" << extension << "],";
  m_document.ascii().addPos(zone.m_defPosition);
  m_document.ascii().addNote(f.str().c_str());
  if (!ok) {
    f.str("");
    f << "Entries(BADPICT)[" << zone << "]:###";
    libmwaw::DebugFile &ascFile=zone.ascii();
    ascFile.addPos(zone.m_entry.begin());
    ascFile.addNote(f.str().c_str());
    return true;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(entry.length(), pictData);
#ifdef DEBUG_WITH_FILES
  if (type==RagTime5GraphInternal::State::P_ScreenRep) {
    libmwaw::DebugFile &ascFile=zone.ascii();
    f.str("");
    f << "Entries(ScrRep)[" << zone << "]:";
    ascFile.addPos(zone.m_entry.begin());
    ascFile.addNote(f.str().c_str());
    return true;
  }
  if (zone.isMainInput())
    m_document.ascii().skipZone(entry.begin(), entry.end()-1);
  static int volatile pictName = 0;
  f.str("");
  f << "Pict-" << ++pictName << "." << extension;
  libmwaw::Debug::dumpFile(pictData, f.str().c_str());
#endif
  return true;
}

bool RagTime5Graph::readPictureMatch(RagTime5Zone &zone, bool color)
{
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  if (zone.m_name.empty())
    f << "Entries(" << (color ? "PictureColMatch" : "PictureMatch") << ")[" << zone << "]:";
  else
    f << "Entries(" << zone.m_name << "[" << (color ? "pictureColMatch" : "pictureMatch") << ")[" << zone << "]:";
  MWAWEntry &entry=zone.m_entry;
  zone.m_isParsed=true;
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  m_document.ascii().addPos(zone.m_defPosition);
  m_document.ascii().addNote(color ? "picture[matchCol]" : "picture[match]");

  int const expectedSz=color ? 42 : 32;
  if (entry.length() != expectedSz) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureMatch: the entry size seems bad\n"));
    f << "###";
    ascFile.addPos(entry.begin());
    ascFile.addNote(f.str().c_str());
    return false;
  }

  MWAWInputStreamPtr input = zone.getInput();
  input->setReadInverted(!zone.m_hiLoEndian); // checkme never seens
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);

  int val;
  for (int i=0; i<4; ++i) {
    static int const expected[]= {0,0,0x7fffffff,0x7fffffff};
    val=static_cast<int>(input->readLong(4));
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  int dim[2];
  for (int &i : dim)
    i=static_cast<int>(input->readLong(2));
  f << "dim=" << dim[0] << "x" << dim[1] << ",";
  for (int i=0; i<2; ++i) { // f2=0-3, f4=0-1
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i+2 << "=" << val << ",";
  }
  // a very big number
  f << "ID?=" << std::hex << input->readULong(4) << std::dec << ",";
  for (int i=0; i<2; ++i) { // f5=f6=0
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i+4 << "=" << val << ",";
  }
  if (color) {
    for (int i=0; i<5; ++i) { // g0=a|32, g1=0|1, other 0, color and pattern ?
      val=static_cast<int>(input->readLong(2));
      if (val)
        f << "g" << i << "=" << val << ",";
    }
  }
  input->setReadInverted(false);
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

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
void RagTime5Graph::flushExtra(bool onlyCheck)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Graph::flushExtra: can not find the listener\n"));
    return;
  }

  MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(100,100), librevenge::RVNG_POINT);
  position.m_anchorTo=MWAWPosition::Char;
  for (auto gIt : m_state->m_idGraphicMap) {
    if (!gIt.second || gIt.second->m_isSent)
      continue;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Graph::flushExtra: find some unsent graphic zones\n"));
      first=false;
    }
    if (!onlyCheck)
      send(*gIt.second, listener, position);
  }
  for (auto pIt : m_state->m_idPictClusterMap) {
    if (!pIt.second || pIt.second->m_isSent)
      continue;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Graph::flushExtra: find some unsent picture zones\n"));
      first=false;
    }
    if (!onlyCheck)
      send(*pIt.second, listener, position);
  }
}

bool RagTime5Graph::sendButtonZoneAsText(MWAWListenerPtr listener, int zId)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::sendButtonZoneAsText: can not find the listener\n"));
    return false;
  }
  auto it=m_state->m_idButtonMap.find(zId);
  if (it==m_state->m_idButtonMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("RagTime5Graph::sendButtonZoneAsText: can not find the button for zone %d\n", zId));
    return false;
  }
  auto const &button=*it->second;
  auto itemIt=button.m_idToItemStringMap.find((button.m_buttonType==1 || button.m_buttonType==5) ? 1 : button.m_item); // for push button, retrieve item 1
  if (itemIt!=button.m_idToItemStringMap.end())
    listener->insertUnicodeString(itemIt->second);
  else if (button.m_item) {
    MWAW_DEBUG_MSG(("RagTime5Graph::sendButtonZoneAsText: can not find the button item %d for zone %d\n", button.m_item, zId));
  }
  return true;
}

bool RagTime5Graph::send(RagTime5GraphInternal::ClusterButton &cluster,
                         MWAWListenerPtr listener, MWAWPosition const &position, MWAWGraphicStyle const &style)
{
  cluster.m_isSent=true;
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find the listener\n"));
    return false;
  }

  std::shared_ptr<MWAWSubDocument> doc(new RagTime5GraphInternal::SubDocument(*this, m_parserState->m_input, cluster.m_zoneId, 0, true, position.size()[0]));
  MWAWPosition pos(position);
  pos.m_wrapping=MWAWPosition::WDynamic;
  listener->insertTextBox(pos, doc, style);
  return true;
}

bool RagTime5Graph::send(RagTime5GraphInternal::Shape const &shape, RagTime5GraphInternal::ClusterGraphic const &cluster,
                         MWAWListenerPtr listener, MWAWPosition const &position)
{
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find the listener\n"));
    return false;
  }
  auto type=shape.m_linkId ? m_document.getClusterType(shape.m_linkId) : RagTime5ClusterManager::Cluster::C_Unknown;
  bool sendAsTextBox=type==RagTime5ClusterManager::Cluster::C_ButtonZone ||
                     type==RagTime5ClusterManager::Cluster::C_TextZone;
  MWAWGraphicStyle style=MWAWGraphicStyle::emptyStyle();
  if (shape.m_type==RagTime5GraphInternal::Shape::S_Line ||
      shape.m_type==RagTime5GraphInternal::Shape::S_Arc) {
    if (shape.m_graphicId)
      m_styleManager->updateBorderStyle(shape.m_graphicId, style, true);
  }
  else {
    if (shape.m_borderId)
      m_styleManager->updateBorderStyle(shape.m_borderId, style, false);
    if (shape.m_graphicId) {
      if (sendAsTextBox && listener->getType()!=MWAWListener::Graphic && listener->getType()!=MWAWListener::Presentation)
        m_styleManager->updateFrameStyle(shape.m_graphicId, style);
      else
        m_styleManager->updateSurfaceStyle(shape.m_graphicId, style);
    }
  }
  if ((shape.m_flags&3) && style.hasLine() &&
      (shape.m_type==RagTime5GraphInternal::Shape::S_Line ||
       shape.m_type==RagTime5GraphInternal::Shape::S_Arc ||
       shape.m_type==RagTime5GraphInternal::Shape::S_Spline)) {
    // by construction arc wise is inverted
    int wh=shape.m_type==RagTime5GraphInternal::Shape::S_Arc ? 1 : 0;
    auto arrow=MWAWGraphicStyle::Arrow::plain();
    arrow.m_width=2.f*style.m_lineWidth+2.f;
    if (shape.m_flags&1)
      style.m_arrows[wh]=arrow;
    if (shape.m_flags&2)
      style.m_arrows[1-wh]=arrow;
  }
  MWAWBox2f bdbox=shape.getBdBox();
  MWAWPosition pos(position);
  pos.setOrigin(bdbox[0]);
  pos.setSize(bdbox.size());
  pos.setUnit(librevenge::RVNG_POINT);
  /*if ((shape.m_flags&0x40)==0)
    pos.m_wrapping=MWAWPosition::WForeground;
    else */
  pos.m_wrapping=MWAWPosition::WParallel;
  if (type==RagTime5ClusterManager::Cluster::C_TextZone) {
    std::shared_ptr<MWAWSubDocument> doc(new RagTime5GraphInternal::SubDocument(*this, m_parserState->m_input, shape.m_linkId, shape.m_partId, false, bdbox.size()[0]));
    pos.m_wrapping=MWAWPosition::WDynamic;
    if (shape.m_type==RagTime5GraphInternal::Shape::S_Rect || shape.m_type==RagTime5GraphInternal::Shape::S_TextBox)
      listener->insertTextBox(pos, doc, style);
    else
      listener->insertTextBoxInShape(pos, doc, shape.m_shape, style);
    return true;
  }
  else if (type==RagTime5ClusterManager::Cluster::C_PictureZone &&
           m_state->m_idPictClusterMap.find(shape.m_linkId)!=m_state->m_idPictClusterMap.end() && m_state->m_idPictClusterMap.find(shape.m_linkId)->second) {
    return send(*m_state->m_idPictClusterMap.find(shape.m_linkId)->second, listener, pos);
  }
  else if (type==RagTime5ClusterManager::Cluster::C_ButtonZone &&
           m_state->m_idButtonMap.find(shape.m_linkId)!=m_state->m_idButtonMap.end() && m_state->m_idButtonMap.find(shape.m_linkId)->second) {
    return send(*m_state->m_idButtonMap.find(shape.m_linkId)->second, listener, pos, style);
  }
  else if (type==RagTime5ClusterManager::Cluster::C_Pipeline) {
    type=m_document.getPipelineContainerType(shape.m_linkId);
    if (type==RagTime5ClusterManager::Cluster::C_TextZone) {
      pos.m_wrapping=MWAWPosition::WDynamic;
      std::shared_ptr<MWAWSubDocument> doc(new RagTime5GraphInternal::SubDocument(*this, m_parserState->m_input, shape.m_linkId, shape.m_partId, false, bdbox.size()[0]));
      if (shape.m_type==RagTime5GraphInternal::Shape::S_Rect || shape.m_type==RagTime5GraphInternal::Shape::S_TextBox)
        listener->insertTextBox(pos, doc, style);
      else
        listener->insertTextBoxInShape(pos, doc, shape.m_shape, style);
      return true;
    }
    else if (type==RagTime5ClusterManager::Cluster::C_SpreadsheetZone) {
      pos.m_wrapping=MWAWPosition::WDynamic;
      m_document.send(shape.m_linkId, listener, pos, shape.m_partId);
    }
    return true;
  }
  else if (type==RagTime5ClusterManager::Cluster::C_SpreadsheetZone) {
    pos.m_wrapping=MWAWPosition::WDynamic;
    m_document.send(shape.m_linkId, listener, pos, shape.m_partId);
    return true;
  }

  switch (shape.m_type) {
  case RagTime5GraphInternal::Shape::S_Arc:
  case RagTime5GraphInternal::Shape::S_Circle:
  case RagTime5GraphInternal::Shape::S_Line:
  case RagTime5GraphInternal::Shape::S_Pie:
  case RagTime5GraphInternal::Shape::S_Polygon:
  case RagTime5GraphInternal::Shape::S_Rect:
  case RagTime5GraphInternal::Shape::S_RectOval:
  case RagTime5GraphInternal::Shape::S_RegularPoly:
  case RagTime5GraphInternal::Shape::S_Spline:
    listener->insertShape(pos, shape.m_shape, style);
    break;
  case RagTime5GraphInternal::Shape::S_Group: {
    bool openGroup=listener->openGroup(pos);
    for (auto childId : shape.m_childIdList) {
      if (cluster.m_idToShapeMap.find(childId)!=cluster.m_idToShapeMap.end() &&
          cluster.m_idToShapeMap.find(childId)->second)
        send(*cluster.m_idToShapeMap.find(childId)->second, cluster, listener, pos);
    }
    if (openGroup)
      listener->closeGroup();
    break;
  }
  case RagTime5GraphInternal::Shape::S_TextBox:
  case RagTime5GraphInternal::Shape::S_Unknown:
#if !defined(__clang__)
  default:
#endif
  {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5Graph::send: sorry sending some shape is not implemented\n"));
      first=false;
    }
    break;
  }
  }
  return true;
}

bool RagTime5Graph::send(RagTime5GraphInternal::ClusterGraphic &cluster, MWAWListenerPtr listener, MWAWPosition const &pos)
{
  cluster.m_isSent=true;
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find the listener\n"));
    return false;
  }
  size_t numShapes=cluster.m_rootIdList.size();
  for (size_t i=0; i<numShapes; ++i) {
    int shapeId=cluster.m_rootIdList[i];
    if (cluster.m_idToShapeMap.find(shapeId)==cluster.m_idToShapeMap.end() ||
        !cluster.m_idToShapeMap.find(shapeId)->second)
      continue;
    MWAWPosition position(pos);
    position.setOrder(static_cast<int>(i+1));
    //position.setOrder(static_cast<int>(numShapes-i+2));
    send(*cluster.m_idToShapeMap.find(shapeId)->second, cluster, listener, position);
  }
  return true;
}

bool RagTime5Graph::send(RagTime5GraphInternal::ClusterPicture &cluster, MWAWListenerPtr listener, MWAWPosition const &position)
{
  cluster.m_isSent=true;
  if (!listener)
    listener=m_parserState->getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find the listener\n"));
    return false;
  }
  if (!cluster.m_containerId ||
      m_state->m_idPictureMap.find(cluster.m_containerId)==m_state->m_idPictureMap.end() ||
      !m_state->m_idPictureMap.find(cluster.m_containerId)->second) {
    MWAW_DEBUG_MSG(("RagTime5Graph::send: can not find picture for zone %d\n", cluster.m_zoneId));
    return false;
  }
  listener->insertPicture(position, *m_state->m_idPictureMap.find(cluster.m_containerId)->second);
  return true;
}

////////////////////////////////////////////////////////////
//
// read cluster data
//
////////////////////////////////////////////////////////////

namespace RagTime5GraphInternal
{
//
//! low level: parser of script cluster : zone 2,a,4002,400a
//
struct ButtonCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_nextId, F_formula, F_formulaRoot, F_name, F_parentList, F_itemNames, F_buttonList };
  //! constructor
  ButtonCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustButton")
    , m_cluster(new ClusterButton)
    , m_fieldName("")

    , m_expectedIdToType()
    , m_idStack()
  {
    m_cluster->m_type=RagTime5ClusterManager::Cluster::C_ButtonZone;
  }
  //! destructor
  ~ButtonCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the button cluster
  std::shared_ptr<RagTime5GraphInternal::ClusterButton> getButtonCluster()
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
    if (N<0 && expected!=F_name) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unexpected data block\n"));
      f << "###N=" << N << ",";
      return true;
    }
    m_link.m_N=N;
    int val;
    long linkValues[4];
    std::string mess;
    switch (expected) {
    case F_name:
      m_fieldName="script:name";
      f << m_fieldName <<",";
      if (!isANameHeader(N)) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: not a name file\n"));
        f << "###";
        return true;
      }
      return true;
    case F_formula:
      m_fieldName="formula";
      f << m_fieldName <<",";
      if (fSz<30) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: the expected field[%d] seems bad\n", expected));
        f << "##fSz=" << fSz << ",";
        return true;
      }
      for (int i=0; i<4; ++i) { // f3=1
        val=int(input->readLong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      val=int(input->readULong(4));
      if (val) // c800/de80
        f << "fl=" << std::hex << val << std::dec << ",";
      val=int(input->readULong(4));
      if (val!=0x1d4e042)
        f << "type1=" << std::hex << val << std::dec << ",";
      for (int i=0; i<4; ++i) { // f4=1
        val=int(input->readLong(2));
        if (val) f << "f" << i+4 << "=" << val << ",";
      }
      return true;
    case F_formulaRoot:
    case F_itemNames:
    case F_buttonList:
    case F_buttonList+1:
    case F_buttonList+2: {
      if (expected==F_formulaRoot && fSz==36) {
        val=static_cast<int>(input->readLong(4));
        if (val) f << "#f0=" << val << ",";
        val=static_cast<int>(input->readLong(4));
        if (val!=0x17db042) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unexpected type0\n"));
          f << "#fileType0=" << std::hex << val << std::dec << ",";
        }
        for (int i=0; i<2; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (val) f << "f" << i+1 << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if ((val&0xFFD7)!=0x10) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unexpected type1[fSz36]\n"));
          f << "#fileType1=" << std::hex << val << std::dec << ",";
        }
        f << "ids=[";
        for (int i=0; i<3; ++i) {
          val=static_cast<int>(input->readLong(4));
          if (!val) {
            f << "_,";
            continue;
          }
          setExpectedType(val-1,F_buttonList+i);
          f << "F" << val-1 << ",";
        }
        f << "],";
        return true;
      }
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=-1, expectedFieldSize=0;
      if (expected==F_itemNames && fSz==32) {
        expectedFileType1=0x600;
        m_link.m_name="itemName";
        m_link.m_type=RagTime5ClusterManager::Link::L_UnicodeList;
      }
      else if (expected==F_formulaRoot && fSz==29) {
        if (m_link.m_fileType[0]!=0x3c052) {
          f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: the expected field[%d] seems bad\n", expected));
        }
        if (linkValues[0]!=0x1454877) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unexpected linkValue[0]\n"));
          f << "#lValues0,";
        }
        expectedFileType1=0x50;
        m_link.m_name="formula[root]";
        val=int(input->readULong(1));
        if (val) f << "g0=" << val << ",";
      }
      else if (expected==F_buttonList && m_link.m_fileType[0]==0x3e800)
        m_link.m_name="buttonList0";
      else if (expected==F_buttonList+1 && m_link.m_fileType[0]==0x35800)
        m_link.m_name="buttonList1";
      else if (expected==F_buttonList+2 && m_link.m_fileType[0]==0x45080) {
        m_link.m_name="buttonListInt";
        expectedFieldSize=2;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        f << m_link.m_name << ",";
        m_fieldName=m_link.m_name;
      }
      if (expectedFileType1>=0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_nextId:
    case F_parentList:
    default:
      break;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unexpected field[%d]\n", m_dataId));
      f << "###";
    }

    switch (fSz) {
    case 36:
      f << "parentList,";
      if (!readLinkHeader(input,fSz,m_link,linkValues,mess)) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: can not read the link\n"));
        f << "###link,";
        return true;
      }
      if ((m_link.m_fileType[1]&0xFFD7)!= 0x10) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::RootCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1,";
      }
      setExpectedType(m_dataId, F_parentList);
      m_fieldName=m_link.m_name="parentList";
      f << m_link << "," << mess;
      for (int i=0; i<2; ++i) { // g0: small number between 38 and 64, g1: 0|-1
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseZone: find unknown size[%ld]\n", fSz));
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
    case F_name:
      if (field.m_type==RagTime5StructManager::Field::T_Unicode && field.m_fileType==0xc8042) {
        m_cluster->m_buttonName=field.m_string.cstr();
        f << field.m_string.cstr();
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseField: find unexpected script field\n"));
      f << "###" << field;
      break;
    case F_formulaRoot:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) {
        f << "ids=[";
        for (auto val : field.m_longList) {
          if (val==0) {
            f << "_,";
            continue;
          }
          setExpectedType(int(val-1), F_formula);
          f << "F" << val-1 << ",";
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_itemNames:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_buttonList:
    case F_buttonList+1:
    case F_buttonList+2:
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
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case F_formula:
    case F_nextId:
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseField: find unexpected field\n"));
      f << "###" << field;
      break;
    }
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header,fl=" << std::hex << flag << std::dec << ",";
    if (N!=-5 || m_dataId!=0 || fSz!=74) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseHeaderZone: find unexpected main field\n"));
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
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseHeaderZone: unexpected zone type[graph]\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    f << "button,";
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(int(val-1), F_nextId); // either next[id] or parentList
      f << "next[id]=F" << val-1 << ",";
    }
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(int(val-1), F_formulaRoot); // or button list v6
      f << "formula[root]=F" << val-1 << ",";
    }
    for (int i=0; i<7; ++i) { //  g4=1-5, g7=1-5
      val=static_cast<int>(input->readULong(i==0 ? 4 : 2));
      if (!val) continue;
      if (i==0) {
        if (val&0x2)
          f << "return[title],"; // else index
        if (val&0x20)
          f << "recalculate[demand],";
        val &= 0xffffffdd;
        if (val)
          f << "fl1=" << std::hex << val << std::dec << ",";
      }
      else if (i==2) {
        m_cluster->m_item=val;
        f << "item=" << val << ","; // 0: no selected, 1: first, 2:second,
      }
      else if (i==3) {
        m_cluster->m_buttonType=val;
        f << "type=" << val << ","; // 1:push, 2:radio, 3:checkbox, 4:popup, 5:push(invisible)
      }
      else if (i==4)
        f << "appearence=" << val << ","; // 1: mac, 2: windows, 3: current platform
      else
        f << "g" << i << "=" << val << ",";
    }
    auto type=input->readULong(4);  // find 0|93037 maybe a type
    if (type) f << "fileType=" << RagTime5ClusterManager::printType(type) << ",";
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(int(val-1), F_name);
      f << "name=F" << val-1 << ",";
    }
    long actPos=input->tell();
    std::vector<int> listIds;
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::parseHeaderZone: can not find the unicode string data\n"));
      f << "##noData,";
      input->seek(actPos+2, librevenge::RVNG_SEEK_SET);
    }
    else if (listIds[0]) {
      // find a script comment
      RagTime5ClusterManager::Link scriptLink;
      scriptLink.m_type=RagTime5ClusterManager::Link::L_List;
      scriptLink.m_name="buttonComment";
      scriptLink.m_ids.push_back(listIds[0]);
      m_cluster->m_scriptComment=scriptLink;
      f << scriptLink << ",";
    }
    for (int i=0; i<9; ++i) { // h0=1-8d, h2=1f40, h4=0-26, h8=0|30
      val=static_cast<int>(input->readLong(i==1 ? 4 : 2));
      if (i==2) {
        switch (val&3) {
        case 1:
          f << "arrange[height],";
          break;
        case 2:
          f << "arrange[oneCol],";
          break;
        case 3:
          f << "arrange[oneRow],";
          break;
        case 0:
        default:
          break;
        }
        val &= 0xfffc;
        if (val==0x1f40) continue;
        f << "#fileType1=" << std::hex << val << std::dec << ",";
      }
      else {
        if (!val) continue;
        if (i==1) {
          setExpectedType(int(val-1), F_itemNames);
          f << "itemName=F" << val-1 << ",";
        }
        else if (i==6) {
          f << "avalaible[form]=FD" << val << ",";
        }
        else
          f << "h" << i << "=" << val << ",";
      }
    }
    std::string code(""); // find cent, left
    auto cod=input->readULong(4);
    for (int i=0; i<4; ++i) code+=char(cod>>(24-8*i));
    if (!code.empty()) f << "align=" << code << ",";
    return true;
  }
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected==F_itemNames) {
      if (m_cluster->m_itemNamesLink.empty())
        m_cluster->m_itemNamesLink=m_link;
      else {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::ButtonCParser::endZone: oops the item name link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
    }
    else if (expected==F_parentList)
      m_cluster->m_parentLink=m_link;
    else
      m_cluster->m_linksList.push_back(m_link);
  }

protected:
  //! the current cluster
  std::shared_ptr<RagTime5GraphInternal::ClusterButton> m_cluster;
  //! the actual field name
  std::string m_fieldName;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
};

ButtonCParser::~ButtonCParser()
{
}

//
//! low level: parser of picture cluster
//
struct PictCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_nextId, F_pictList, F_pictRoot=F_pictList+3 };
  //! constructor
  PictCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustPict")
    , m_cluster(new ClusterPicture)
    , m_what(-1)
    , m_linkId(-1)
    , m_fieldName("")

    , m_expectedIdToType()
    , m_idStack()
  {
  }
  //! destructor
  ~PictCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the current cluster
  std::shared_ptr<ClusterPicture> getPictureCluster()
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
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    switch (m_linkId) {
    case 0:
      m_cluster->m_auxilliarLink=m_link;
      break;
    case 1:
      m_cluster->m_parentLink=m_link;
      break;
    default:
      if (m_what==0) {
        if (m_cluster->m_dataLink.empty())
          m_cluster->m_dataLink=m_link;
        else {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::endZone: oops the main link is already set\n"));
          m_cluster->m_linksList.push_back(m_link);
        }
      }
      else
        m_cluster->m_linksList.push_back(m_link);
      break;
    }
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_what=m_linkId=-1;
    m_fieldName="";
    if (N==-5)
      return parseHeaderZone(input,fSz,N,flag,f);
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: expected N value\n"));
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
    case 0:
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x170c8e5) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      else if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x3c057) {
        // rare, find one time with 4
        for (auto const &id : field.m_longList)
          f << "unkn0=" << id << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseField: find unexpected header field\n"));
      f << "###" << field << ",";
      break;
    case 1: // list link
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      // only with long2 list and with unk=[4]
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
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseField: find unexpected list link field\n"));
      f << "###" << field << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseField: find unexpected field\n"));
      f << "###" << field << ",";
      break;
    }
    return true;
  }
protected:
  //! parse a data block, find fSz=36, 36|36|28|28|32
  bool parseDataZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    if (expected!=-1)
      f << "[F" << m_dataId << "]";
    f << "fl=" << std::hex << flag << std::dec << ",";
    m_link.m_N=N;
    std::string mess;
    switch (expected) {
    case F_pictList:
    case F_pictList+1:
    case F_pictList+2: {
      long linkValues[4];
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=0, expectedFieldSize=0;
      m_what=1;
      if (expected==F_pictList && m_link.m_fileType[0]==0x3e800)
        m_link.m_name="pictList0";
      else if (expected==F_pictList+1 && m_link.m_fileType[0]==0x35800)
        m_link.m_name="pictList1";
      else if (expected==F_pictList+2 && ((unsigned long)(m_link.m_fileType[0])&0x7fffffff)==0x45080) { // v6.6 0x45080 other 0x80045080
        m_link.m_name="pictListInt";
        m_linkId=0;
        if (m_link.m_fileType[0]==0x45080) expectedFieldSize=2;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        f << m_link.m_name << ",";
        m_fieldName=m_link.m_name;
      }
      if (expectedFileType1>=0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_pictRoot: {
      if (fSz<36) {
        f << "###fSz,";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      m_fieldName="pictList[root]";
      f << m_fieldName << ",";
      auto val=static_cast<int>(input->readLong(4));
      if (val) f << "#f0=" << val << ",";
      val=static_cast<int>(input->readLong(4));
      if (val!=0x17d4842 && val!=0x17db042) { // 0x17db042: v66
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: find unexpected type0\n"));
        f << "#fileType0=" << std::hex << val << std::dec << ",";
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (val) f << "f" << i+1 << "=" << val << ",";
      }
      val=static_cast<int>(input->readULong(2));
      if ((val&0xFFD7)!=0x10) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: find unexpected type1[fSz36]\n"));
        f << "#fileType1=" << std::hex << val << std::dec << ",";
      }
      f << "ids=[";
      for (int i=0; i<3; ++i) {
        val=static_cast<int>(input->readLong(4));
        if (!val) {
          f << "_,";
          continue;
        }
        setExpectedType(val-1,F_pictList+i);
        f << "F" << val-1 << ",";
      }
      f << "],";
      return true;
    }
    case F_nextId:
      break;
    default:
      break;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseDataZone: find unexpected field\n"));
      f << "###field,";
    }
    switch (fSz) {
    case 36: {
      long linkValues[4];
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: the field fSz28... type seems bad\n"));
        return true;
      }
      m_what=1;
      long expectedFileType1=0;
      if (m_link.m_fileType[0]==0) {
        expectedFileType1=0x10;
        // field of sz=28, dataId + ?
        m_linkId=1;
        m_link.m_name="pictParentList";
        m_fieldName="parentList";
      }
      else {
        f << "###fType=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: the field fSz28... type seems bad\n"));
        return true;
      }
      if (expectedFileType1>=0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      f << m_link << "," << mess;
      f << "unkn=";
      for (int i=0; i<2; ++i) { // unsure some seq
        int val=int(input->readLong(2));
        f << val << (i==0 ? "-" : ",");
      }
      break;
    }
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseZone: find unexpected fieldSize\n"));
      f << "##fSz=" << fSz << ",";
      break;
    }
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    f << "header,fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    m_what=0;
    if (N!=-5 || m_dataId!=0 || (fSz!=64 && fSz!=104 && fSz!=109)) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseHeaderZone: find unexpected main field\n"));
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
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) {// f0=0|2|3, f1=0|3
      val=static_cast<int>(input->readLong(4));
      if (!val) continue;
      if (i==0) {
        setExpectedType(val-1,F_nextId);
        f << "next[id]=F" << val-1 << ",";
      }
      else {
        setExpectedType(val-1,F_pictRoot);
        f << "pict[root]=F" << val-1 << ",";
      }
    }
    if (fSz==64) { // movie
      for (int i=0; i<2; ++i) { // 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+2 << "=" << val << ",";
      }
      f << "movie,";
      float dim[2];
      for (auto &d : dim) d=float(input->readLong(4))/65536.f;
      f << "dim=" << MWAWVec2f(dim[0],dim[1]) << ",";
      for (int i=0; i<15; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
      return true;
    }
    for (int i=0; i<5; ++i) {
      val=static_cast<int>(input->readLong(2));
      static int const expected[]= {2, 0, 0x2000, 0, 0x2710};
      if (val!=expected[i]) f << "f" << i+2 << "=" << val << ",";
    }
    auto type=input->readULong(4);
    if (type!=0x3f7ff5) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseHeaderZone: unexpected type [104|109]\n"));
      f << "#fieldType=" << RagTime5Graph::printType(type) << ",";
    }
    for (int i=0; i<2; ++i) { // always 1,1 ?
      val=static_cast<int>(input->readLong(1));
      if (val!=1) f << "fl" << i << "=" << val << ",";
    }
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(4))/65536.f;
    m_cluster->m_dimension=MWAWVec2f(dim[0],dim[1]);
    f << "dim=" << m_cluster->m_dimension << ",sz=" << MWAWVec2f(dim[2],dim[3]) << ",";
    for (int i=0; i<5; ++i) { // fl2=708|718|f18|...|7d4b, fl3=0|4, f5=800|900|8000,fl6=0|1|a
      val=static_cast<int>(input->readULong(2));
      if (val) f << "fl" << i+2 << "=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<4; ++i) { // some selection ?
      val= static_cast<int>(input->readLong(4));
      if ((i<2&&val)||(i>=2&&val!=0x7FFFFFFF))
        f << "g" << i << "=" << val << ",";
    }
    for (int i=0; i<6; ++i) { // h2=0|1|3|8
      val= static_cast<int>(input->readLong(2));
      if (val) f << "h" << i << "=" << val << ",";
    }
    // find 5b84|171704|171804|172d84, so unsure
    m_link.m_fileType[0]=input->readULong(4);
    if (m_link.m_fileType[0])
      f << "fieldType1=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
    std::vector<int> listIds;
    long actPos=input->tell();
    if (!RagTime5StructManager::readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::PictCParser::parseHeaderZone: can not find the data[104|109]\n"));
      f << "##noData,";
      m_link.m_ids.clear();
      input->seek(actPos+2, librevenge::RVNG_SEEK_SET);
    }
    else if (listIds[0]) {
      m_cluster->m_containerId=listIds[0];
      f << "container=data" << listIds[0] << "A,";
    }
    for (int i=0; i<2; ++i) { // always 0
      val= static_cast<int>(input->readLong(2));
      if (val) f << "h" << i+6 << "=" << val << ",";
    }
    if (fSz==109) {
      int dim2[2];
      for (auto &d : dim2) d=static_cast<int>(input->readLong(2));
      f << "dim2=" << MWAWVec2i(dim2[0], dim2[1]) << ",";
      val= static_cast<int>(input->readLong(1)); // 0 or 1
      if (val) f << "h8=" << val << ",";
    }
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterPicture> m_cluster;
  //! a index to know which field is parsed :  0: main, 1: list
  int m_what;
  //! the link id: 0: fieldSz=8 ?, data2: dataId+?
  int m_linkId;
  //! the actual field name
  std::string m_fieldName;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;

private:
  //! copy constructor (not implemented)
  PictCParser(PictCParser const &orig) = delete;
  //! copy operator (not implemented)
  PictCParser &operator=(PictCParser const &orig) = delete;
};

PictCParser::~PictCParser()
{
}

//
//! low level: parser of graph cluster
//
struct GraphicCParser final : public RagTime5ClusterManager::ClusterParser {
  enum { F_clustLink2, F_dim, F_graphLink=F_dim+3, F_graphList, F_name=F_graphList+3, F_name2=F_name+3, F_nextId=F_name2+3, F_unknA, F_unknClustLinkA };
  //! constructor
  GraphicCParser(RagTime5ClusterManager &parser, int type)
    : ClusterParser(parser, type, "ClustGraph")
    , m_cluster(new ClusterGraphic)
    , m_what(-1)
    , m_linkId(-1)
    , m_fieldName("")
    , m_conditionFormulaLinks()

    , m_expectedIdToType()
    , m_idStack()
  {
  }
  //! destructor
  ~GraphicCParser() final;
  //! return the current cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> getCluster() final
  {
    return m_cluster;
  }
  //! return the current graphic cluster
  std::shared_ptr<ClusterGraphic> getGraphicCluster()
  {
    return m_cluster;
  }
  //! return the condition formula link (unsure)
  std::vector<RagTime5ClusterManager::Link> const &getConditionFormulaLinks() const
  {
    return m_conditionFormulaLinks;
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
  //! end of a start zone call
  void endZone() final
  {
    if (m_link.empty())
      return;
    switch (m_linkId) {
    case 0:
      if (m_cluster->m_nameLink.empty())
        m_cluster->m_nameLink=RagTime5ClusterManager::NameLink(m_link);
      else {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::endZone: oops the name link is already set\n"));
        m_cluster->m_linksList.push_back(m_link);
      }
      break;
    case 1:
      m_conditionFormulaLinks.push_back(m_link);
      break;
    case 2:
    case 4:
      m_cluster->m_clusterLinks[m_linkId==2 ? 0 : 1]=m_link;
      break;
    case 3:
      m_cluster->m_parentLink=m_link;
      break;
    case 5:
      m_cluster->m_transformationLinks.push_back(m_link);
      break;
    case 6:
      m_cluster->m_dimensionLinks.push_back(m_link);
      break;
    default:
      if (m_what==0) {
        if (m_cluster->m_dataLink.empty())
          m_cluster->m_dataLink=m_link;
        else {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::endZone: oops the main link is already set\n"));
          m_cluster->m_linksList.push_back(m_link);
        }
      }
      else
        m_cluster->m_linksList.push_back(m_link);
      break;
    }
  }
  //! parse a zone
  bool parseZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f) final
  {
    m_what=m_linkId=-1;
    m_fieldName="";
    if (N==-5)
      return parseHeaderZone(input,fSz,N,flag,f);
    if (N<0) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseZone: expected N value\n"));
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
    /* TODO: use the field type
    auto const &it=m_expectedIdToType.find(m_dataId);
    int expected=(it!=m_expectedIdToType.end())?it->second : -1;
    */
    switch (m_what) {
    case 0: // graph data
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0x3c057) {
        // a small value 3|4
        for (auto const &id : field.m_longList)
          f << "unkn0=" << id << ",";
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14e6825) {
        f << "decal=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList)
              f << val << ",";
            m_link.m_longList=child.m_longList;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected decal child[graph]\n"));
          f << "##[" << child << "],";
        }
        f << "],";
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14e6875) {
        f << "listFlag?=[";
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0xce017)
            f << child.m_extra << ","; // find data with different length there
          else {
            MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected unstructured child[graphZones]\n"));
            f << "##" << child << ",";
          }
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected child[graphZones]\n"));
      f << "##" << field << ",";
      break;
    case 1: // list link
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xce842) {
        f << "pos=[";
        for (auto val : field.m_longList)
          f << val << ",";
        f << "],";
        m_link.m_longList=field.m_longList;
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      // only with long2 list and with unk=[4]
      if (field.m_type==RagTime5StructManager::Field::T_LongList && field.m_fileType==0xcf042) {
        f << "unkn=[";
        for (auto val : field.m_longList) {
          if (val==0)
            f << "_,";
          else {
            setExpectedType(int(val-1), F_unknA);
            f << "rootA=F" << val-1 << ",";
          }
        }
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected list link field\n"));
      f << "###" << field;
      break;
    case 2: // cluster link, graph transform
      if (field.m_type==RagTime5StructManager::Field::T_Long && field.m_fileType==0xcf817) {
        // only in graph transform, small value between 3b|51|52|78
        f << "f0="<<field.m_longValue[0] << ",";
        break;
      }
      if (field.m_type==RagTime5StructManager::Field::T_Unstructured && field.m_fileType==0xce017) {
        // a small value 2|4|a|1c|40
        f << "unkn="<<field.m_extra << ",";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected cluster link field\n"));
      f << "###" << field;
      break;
    case 3: // fSz=91
      if (field.m_type==RagTime5StructManager::Field::T_FieldList && field.m_fileType==0x14f1825) {
        f << "list=["; // find only list with one element: 1,4,8,11 number of data?
        for (auto const &child : field.m_fieldList) {
          if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==0xce842) {
            for (auto val : child.m_longList)
              f << val << ",";
            m_link.m_longList=child.m_longList;
            continue;
          }
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected child[fSz=91]\n"));
          f << "##[" << child << "],";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected cluster field[fSz=91]\n"));
      f << "###" << field;
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseField: find unexpected field\n"));
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
    f << "fl=" << std::hex << flag << std::dec << ",";
    std::string mess("");
    m_link.m_N=N;
    int val;
    switch (expected) {
    case F_clustLink2:
    case F_dim:
    case F_dim+1:
    case F_dim+2:
    case F_name:
    case F_name+1:
    case F_name+2:
    case F_name2:
    case F_name2+1:
    case F_graphLink:
    case F_graphList:
    case F_graphList+1:
    case F_graphList+2:
    case F_unknClustLinkA: {
      long linkValues[4];
      if (fSz<28 || !readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        f << "###fType=" << RagTime5ClusterManager::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        return true;
      }
      f << m_link << "," << mess;
      long expectedFileType1=0, expectedFieldSize=0;
      if (expected==F_clustLink2 && fSz==36) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find file type in the field[%d]\n", expected));
          f << "###fileType0,";
        }
        m_what=1;
        m_linkId=3;
        m_link.m_name="parentLink";
        expectedFileType1=0x10;
        for (int i=0; i<2; ++i) { // g0=3a-4f, g1=0|-1
          val=static_cast<int>(input->readLong(2));
          if (val)
            f << "g" << i << "=" << val << ",";
        }
      }
      else if (expected==F_graphLink && fSz==30) {
        m_what=2;
        m_linkId=2;
        m_link.m_name="graphLinkLst";
        expectedFieldSize=12;
        expectedFileType1=0xd0;
        m_link.m_type=RagTime5ClusterManager::Link::L_ClusterLink;
      }
      else if ((expected==F_name||expected==F_name2||expected==F_dim) && m_link.m_fileType[0]==0x3e800) {
        m_what=1;
        m_link.m_name=(expected==F_name ? "unicodeList0" : expected==F_name2 ? "name2List0" : "dimList0");
      }
      else if ((expected==F_name+1||expected==F_name2+1||expected==F_dim+1) && m_link.m_fileType[0]==0x35800) {
        m_what=1;
        m_link.m_name=(expected==F_name+1 ? "unicodeList1" : expected==F_name2+1 ? "name2List1" : "dimList1");
      }
      else if (expected==F_dim+2 && m_link.m_fileType[0]==0x33000) { // 30
        expectedFieldSize=4;
        m_linkId=6;
        m_link.m_name="dims";
      }
      else if (expected==F_name+2 && m_link.m_fileType[0]==0) { // fSz==32
        expectedFileType1=0x200;
        m_what=1;
        m_linkId=0;
        m_link.m_type=RagTime5ClusterManager::Link::L_UnicodeList;
        m_link.m_name="unicodeNames";
      }
      else if (expected==F_unknClustLinkA && fSz==30) {
        if (m_link.m_fileType[0]) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find file type in the field[%d]\n", expected));
          f << "###fileType0,";
        }
        expectedFieldSize=8;
        m_linkId=4;
        m_link.m_name="clustLink3";
      }
      else if (expected==F_graphList && m_link.m_fileType[0]==0x3e800) {
        m_what=1;
        m_link.m_name="graphList0";
      }
      else if (expected==F_graphList+1 && m_link.m_fileType[0]==0x35800) {
        m_what=1;
        m_link.m_name="graphList1";
      }
      else if (expected==F_graphList+2 && m_link.m_fileType[0]==0x45080) {
        m_link.m_name="graphListInt";
        //m_linkId=0;
        expectedFieldSize=2;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: the expected field[%d] seems bad\n", expected));
        f << "###";
      }
      if (!m_link.m_name.empty()) {
        f << m_link.m_name << ",";
        m_fieldName=m_link.m_name;
      }
      if (expectedFileType1>0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: the expected field[%d] fileType1 seems odd\n", expected));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      if (expectedFieldSize>0 && m_link.m_fieldSize!=expectedFieldSize) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: fieldSize seems odd[%d]\n", expected));
        f << "###fieldSize,";
      }
      return true;
    }
    case F_name2+2:
      if (fSz<28) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: unexpected data of size for name2\n"));
        f << "##fSz,";
        return true;
      }
      m_fieldName="name2Unkn";
      f << m_fieldName << ",";
      val=static_cast<int>(input->readLong(4));
      if (val!=0x46000) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find odd file type in the field[%d]\n", expected));
        f << "###fileType0=" << RagTime5Graph::printType((unsigned long)val) << ",";
      }
      for (int i=0; i<9; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      m_what=1;
      return true;
    case F_unknA: {
      if (fSz<91) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: unexpected data of size for setting root\n"));
        f << "##fSz,";
        return true;
      }
      m_fieldName="unknA";
      f << m_fieldName << ",";
      m_what=3;
      if (N) // find always 0
        f << "#N=" << N << ",";
      for (int i=0; i<2; ++i) { // always 0
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(4)); // always 1
      if (val!=1)
        f << "f2=" << val << ",";
      val=static_cast<int>(input->readLong(2)); // 0|4
      if (val) f << "f3=" << val << ",";
      val=static_cast<int>(input->readULong(2)); // ?
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      auto type=input->readULong(4);
      if (type!=0x14e7842) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected file type\n"));
        f << "##filetype0=" << RagTime5Graph::printType(type) << ",";
      }
      for (int i=0; i<2; ++i) {
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "f" << i+4 << "=" << val << ",";
      }
      val=static_cast<int>(input->readLong(4));
      if (val) {
        setExpectedType(val-1, F_unknClustLinkA);
        f << "clustLinkA=F" << val-1 << ",";
      }
      for (int wh=0; wh<2; ++wh) { // checkme unsure about field separations
        f << "unkn" << wh << "=[";
        val=static_cast<int>(input->readLong(1)); // 0
        if (val) f << "g0=" << val << ",";
        for (int i=0; i<3; ++i) { // g0=8|16 g3=0|1
          static int const expectedVal[]= {16, 0, 0};
          val=static_cast<int>(input->readLong(2)); // 16
          if (val!=expectedVal[i])
            f << "g" << i+1 << "=" << val << ",";
        }
        val=static_cast<int>(input->readLong(1)); // 0
        if (val) f << "g4=" << val << ",";
        for (int i=0; i<7; ++i) { // g5=348,
          val=static_cast<int>(input->readLong(2));
          if (val) f << "g" << i+5 << "=" << val << ",";
        }
        val=static_cast<int>(input->readLong(1)); // 0
        if (val) f << "h0=" << val << ",";
        for (int i=0; i<2; ++i) { // h1=21|29
          val=static_cast<int>(input->readLong(2));
          if (val) f << "h" << i+1 << "=" << val << ",";
        }
        val=static_cast<int>(input->readLong(1)); // 0
        if (val) f << "h3=" << val << ",";
        f << "],";
      }
      for (int i=0; i<5; ++i) { // always 0
        val=static_cast<int>(input->readLong(1));
        if (val) f << "g" << i << "=" << val << ",";
      }
      return true;
    }
    default:
      break;
    }
    if (expected==-1) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected field\n"));
      f << "###field,";
    }
    long endPos=input->tell()+fSz-6;
    switch (fSz) {
    case 28:
    case 29:
    case 30:
    case 32:
    case 34: {
      long linkValues[4];
      if (!readLinkHeader(input, fSz, m_link, linkValues, mess)) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected link\n"));
        f << "###link";
        return true;
      }
      long expectedFileType1=0;
      if (m_link.m_fileType[0]==0x34800) {
        m_what=1;
        if (linkValues[0]!=0x14ff840) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected linkValue[0]\n"));
          f << "#lValues0,";
        }
        m_fieldName="zone:longs1";
        if (linkValues[1]) {
          setExpectedType(int(linkValues[1]-1), F_nextId);
          f << "next[id]=F" << linkValues[1]-1 << ",";
        }
      }
      else if (m_link.m_fileType[0]==0x3c052) {
        m_what=1;
        if (linkValues[0]!=0x1454877) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected linkValue[0]\n"));
          f << "#lValues0,";
        }
        m_fieldName="zone:longs2";
        expectedFileType1=0x50;
      }
      else if (m_link.m_fileType[0]==0x9f840) {
        if (m_link.m_fieldSize!=34 && m_link.m_fieldSize!=36) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected fieldSize[fSz28...]\n"));
          f << "###fielSize,";
        }
        expectedFileType1=0x10;
        if (linkValues[0]!=0x1500040) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected linkValues[fSz28...]\n"));
          f << "#linkValue0,";
        }
        m_linkId=5;
        m_what=2;
        m_fieldName="graphTransform";
        if (linkValues[1]) {
          setExpectedType(int(linkValues[1]-1), F_nextId);
          f << "next[id]=F" << linkValues[1]-1 << ",";
        }
      }
      else if (m_link.m_fileType[0]==0x14ff040) {
        if (linkValues[0]!=0x14ff040) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected linkValues[fSz28...]\n"));
          f << "#linkValue0,";
        }
        m_what=1;
        m_linkId=1;
        m_link.m_name=m_fieldName="condFormula";
        expectedFileType1=0x10;
      }
      else {
        f << "###fType=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: the field fSz28 type seems bad\n"));
        return true;
      }
      if (linkValues[2]) {
        setExpectedType(int(linkValues[2]-1), F_nextId);
        f << "next[id]=F" << linkValues[2]-1 << ",";
      }
      if (expectedFileType1>=0 && long(m_link.m_fileType[1]&0xFFD7)!=expectedFileType1) {
        MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: fileType1 seems odd[fSz=28...]\n"));
        f << "###fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
      }
      f << m_link << "," << mess;
      m_link.m_fileType[0]=0;
      auto remain=int(endPos-input->tell());
      if (remain==0) break;
      if (remain==4) {
        for (int i=0; i<2; ++i) { // g0=3a-4f, g1=0|-1
          val=static_cast<int>(input->readLong(2));
          if (val)
            f << "g" << i << "=" << val << ",";
        }
        break;
      }
      val=static_cast<int>(input->readLong(1));
      if (val!=1) // always 1
        f << "g0=" << val << ",";
      if (remain<6) break;
      val=static_cast<int>(input->readLong(1));
      if (val) // always 0
        f << "g1=" << val << ",";
      for (int i=0; i<2; ++i) { // g3=0|3c042
        val=static_cast<int>(input->readLong(2));
        if (val)
          f << "g" << i+2 << "=" << val << ",";
      }
      break;
    }
    case 36: {
      val=static_cast<int>(input->readLong(4));
      unsigned long type= input->readULong(4);
      if (type==0x7d01a || type==0x7d42a || (type&0xFFFFF8F)==0x14e818a||type==0x17db042) {
        m_what=2;
        m_fieldName=type==0x7d01a ? "name[root]" : type==0x7d42a ? "name2[root]" : type==0x17db042 ? "graphList" : "dim[root]";
        f << m_fieldName << ",";
        f << "type=" << RagTime5Graph::printType(type) << ",";
        if (val) f << "#f0=" << val << ",";
        for (int i=0; i<2; ++i) { // f1=0|7-11
          val=static_cast<int>(input->readLong(4));
          if (!val) continue;
          if (i==0) {
            setExpectedType(val-1, F_nextId);
            f << "next[id]=F" << val-1 << ",";
          }
          else
            f << "f" << i+1 << "=" << val << ",";
        }
        val=static_cast<int>(input->readULong(2));
        if ((val&0xFFD7)!=0x10) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected type1[fSz36]\n"));
          f << "#fileType1=" << std::hex << val << std::dec << ",";
        }
        f << "ids=[";
        for (int i=0; i<3; ++i) { // g0=3-a, g1=g0+1, g2=g1+1
          val=static_cast<int>(input->readLong(4));
          if (!val) {
            f << "_,";
            continue;
          }
          if (type==0x7d01a)
            setExpectedType(val-1,F_name+i);
          else if (type==0x7d42a)
            setExpectedType(val-1,F_name2+i);
          else if (type==0x17db042)
            setExpectedType(val-1,F_graphList+i);
          else
            setExpectedType(val-1,F_dim+i);
          f << "F" << val-1 << ",";
        }
        f << "],";
        break;
      }
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected link\n"));
      f << "###link=" << std::hex << type << std::dec;
      return true;
    }
    default:
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseDataZone: find unexpected field size\n"));
      f << "##fSz=" << fSz << ",";
      break;
    }
    if (!m_fieldName.empty())
      f << m_fieldName << ",";

    return true;
  }
  //! parse the header zone
  bool parseHeaderZone(MWAWInputStreamPtr &input, long fSz, int N, int flag, libmwaw::DebugStream &f)
  {
    // flag&0x100: selected? or unlock?
    f << "header,fl=" << std::hex << flag << std::dec << ",";
    m_fieldName="header";
    if (N!=-5 || m_dataId!=0 || fSz!=118) {
      f << "###N=" << N << ",fSz=" << fSz << ",";
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: find unexpected main field\n"));
      return true;
    }
    m_what=0;

    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    val=static_cast<int>(input->readLong(2));
    f << "id=" << val << ",";
    val=static_cast<int>(input->readULong(2));
    if (m_type>0 && val!=m_type) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: unexpected zone type\n"));
      f << "##zoneType=" << std::hex << val << std::dec << ",";
    }
    m_fieldName="graphZone";
    val=static_cast<int>(input->readLong(4));
    if (val) {
      setExpectedType(val-1, F_clustLink2);
      f << "clusterLink2[id]=F" << val-1 << ",";
    }
    val=static_cast<int>(input->readLong(4)); // 0|2|3|4
    if (val) {
      setExpectedType(val-1, F_nextId);
      f << "next[id]=F" << val-1 << ",";
    }
    m_link.m_fileType[0]=input->readULong(4); // find 0|80|81|880|8000|8080

    if ((m_link.m_fileType[0]&0x777E)!=0) {
      MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: the file type0 seems bad[graph]\n"));
      f << "##fileType0=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
    }
    else if (m_link.m_fileType[0])
      f << "fileType0=" << RagTime5Graph::printType(m_link.m_fileType[0]) << ",";
    for (int wh=0; wh<2; ++wh) {
      f << "block" << wh << "[";
      val=static_cast<int>(input->readLong(2)); // 1 or 10
      if (val!=1) f << "g0=" << val << ",";
      m_cluster->m_N[wh]=static_cast<int>(input->readLong(4));
      if (m_cluster->m_N[wh]) f << "N=" << m_cluster->m_N[wh] << ","; // 0:graphShape(size)+1, 1:graphUsed(size)
      for (int i=0; i<4; ++i) { // g1=numData+1? and g2 small number, other 0 ?
        val=static_cast<int>(input->readLong(4));
        if (val) f << "g" << i+1 << "=" << val << ",";
      }
      if (wh==0) {
        m_link.m_fileType[1]=input->readULong(2);
        if (m_link.m_fileType[1]!=0x8000 && m_link.m_fileType[1]!=0x8020) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: the file type1 seems bad[graph]\n"));
          f << "##fileType1=" << std::hex << m_link.m_fileType[1] << std::dec << ",";
        }
        long actPos=input->tell();
        if (!RagTime5StructManager::readDataIdList(input, 2, m_link.m_ids) || m_link.m_ids[1]==0) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: can not find the graph data\n"));
          f << "##noData,";
          m_link.m_ids.clear();
          m_link.m_ids.resize(2,0);
          input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
        }
        val=static_cast<int>(input->readLong(2)); // always 0
        if (val) f << "g6=" << val << ",";
        val=static_cast<int>(input->readLong(4)); // 0|2
        if (val) {
          setExpectedType(val-1, F_graphLink);
          f << "graphLink=F" << val-1 << ",";
        }
        float dim[2];
        for (auto &d : dim) d=float(input->readLong(4))/65536.f;
        f << "dim=" << MWAWVec2f(dim[0], dim[1]) << ",";
        for (int i=0; i<4; ++i) { // always 0
          val=static_cast<int>(input->readLong(2));
          if (val) f << "h" << i << "=" << val << ",";
        }
      }
      else {
        RagTime5ClusterManager::Link unknLink;
        unknLink.m_fileType[1]=input->readULong(2);
        unknLink.m_fieldSize=static_cast<int>(input->readULong(2));
        if ((unknLink.m_fileType[1]!=0x50 && unknLink.m_fileType[1]!=0x58) || unknLink.m_fieldSize!=10) {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: the file type2 seems bad[graph]\n"));
          f << "##fileType2=" << std::hex << unknLink.m_fileType[1] << std::dec << "[" << unknLink.m_fieldSize << "],";
        }
        // fixme store unknLink instead of updating the main link
        std::vector<int> listIds;
        if (RagTime5StructManager::readDataIdList(input, 3, listIds)) {
          if (listIds[0]) {
            m_cluster->m_usedZoneId=listIds[0];
            f << "graphUsed=data"  << listIds[0] << "A,";
          }
          if (listIds[1]) {
            m_cluster->m_textboxZoneId=listIds[1];
            m_cluster->m_clusterIdsList.push_back(listIds[1]);
            f << "textboxId=data"  << listIds[1] << "A,";
          }
          if (listIds[2]) {
            m_cluster->m_clusterIdsList.push_back(listIds[2]);
            f << "clusterId=" << getClusterDebugName(listIds[2]) << ",";
          }
        }
        else {
          MWAW_DEBUG_MSG(("RagTime5GraphInternal::GraphicCParser::parseHeaderZone: can not read unkn link list[graph]\n"));
          f << "##graph[unknown],";
        }
      }
      f << "],";
    }
    f << m_link << ",";
    if (!m_fieldName.empty())
      f << m_fieldName << ",";
    return true;
  }

  //! the current cluster
  std::shared_ptr<ClusterGraphic> m_cluster;
  //! a index to know which field is parsed :  0: graphdata, 1: list, 2: clustLink, graph transform, 3:fSz=91
  int m_what;
  //! the link id: 0: unicode, 1: condition, 2: clustLink, 3: clustLink[list], 4: clustLink[8], 5: transformation, 6: some dimension?
  int m_linkId;
  //! the actual field name
  std::string m_fieldName;
  //! the conditions formula links (unsure)
  std::vector<RagTime5ClusterManager::Link> m_conditionFormulaLinks;

  //! the expected id
  std::map<int,int> m_expectedIdToType;
  //! the id stack
  std::stack<int> m_idStack;
private:
  //! copy constructor (not implemented)
  GraphicCParser(GraphicCParser const &orig) = delete;
  //! copy operator (not implemented)
  GraphicCParser &operator=(GraphicCParser const &orig) = delete;
};

GraphicCParser::~GraphicCParser()
{
}

}

////////////////////////////////////////////////////////////
// button
////////////////////////////////////////////////////////////
std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Graph::readButtonCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readButtonCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5GraphInternal::ButtonCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getButtonCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readButtonCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  auto button=parser.getButtonCluster();
  if (m_state->m_idButtonMap.find(zone.m_ids[0])!=m_state->m_idButtonMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readButtonCluster: oops button zone %d is already stored\n", zone.m_ids[0]));
  }
  else
    m_state->m_idButtonMap[zone.m_ids[0]]=button;
  if (!button->m_itemNamesLink.empty()) {
    RagTime5ClusterManager::NameLink nameLink(button->m_itemNamesLink);
    m_document.readUnicodeStringList(nameLink, button->m_idToItemStringMap);
  }
  std::shared_ptr<RagTime5Zone> dataZone;
  int id=button->m_scriptComment.m_ids.empty() ? 0 : button->m_scriptComment.m_ids[0];
  if (id) dataZone=m_document.getDataZone(button->m_scriptComment.m_ids[0]);
  if (!dataZone && id) {
    MWAW_DEBUG_MSG(("RagTime5Document::readButtonCluster: the script comment zone %d seems bad\n", id));
  }
  else if (dataZone) {
    dataZone->m_hiLoEndian=button->m_hiLoEndian;
    m_document.readScriptComment(*dataZone);
  }
  std::vector<RagTime5StructManager::ZoneLink> listCluster;
  m_document.readClusterLinkList(button->m_parentLink, listCluster, "ButtonParentLst");

  for (auto const &lnk : button->m_linksList) {
    if (lnk.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(lnk);
      continue;
    }
    std::stringstream s;
    s << "DataScript_" << lnk.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(s.str());
    m_document.readFixedSizeZone(lnk, defaultParser);
  }
  return button;
}

////////////////////////////////////////////////////////////
// picture
////////////////////////////////////////////////////////////
std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Graph::readPictureCluster(RagTime5Zone &zone, int zoneType)
{
  auto clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5GraphInternal::PictCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getPictureCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }

  auto cluster=parser.getPictureCluster();
  if (m_state->m_idPictClusterMap.find(zone.m_ids[0])!=m_state->m_idPictClusterMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readPictureCluster: oops picture zone %d is already stored\n", zone.m_ids[0]));
  }
  else
    m_state->m_idPictClusterMap[zone.m_ids[0]]=cluster;
  m_document.checkClusterList(cluster->m_clusterIdsList);
  if (cluster->m_containerId>0) {
    auto data=m_document.getDataZone(cluster->m_containerId);
    if (!data) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readPictureCluster: can not find container zone %d\n", cluster->m_containerId));
    }
    else if (!data->m_isParsed) {
      data->m_hiLoEndian=cluster->m_hiLoEndian;
      readPictureContainer(*data);
    }
  }
  if (!cluster->m_auxilliarLink.empty()) { // list of increasing int sequence....
    if (cluster->m_auxilliarLink.m_fileType[0]==(unsigned long)(long(0x80045080))) { // v5-6.5
      RagTime5GraphInternal::IntListParser intParser(2, "PictListInt");
      m_document.readListZone(cluster->m_auxilliarLink, intParser);
    }
    else if (cluster->m_auxilliarLink.m_ids.size()==1) { // v6.6
      std::vector<long> intList;
      cluster->m_auxilliarLink.m_name="PictListInt";
      m_document.readLongList(cluster->m_auxilliarLink, intList);
    }
    else {
      MWAW_DEBUG_MSG(("RagTime5Graph::readPictureCluster: unexpected auxilliar link for zone %d\n", cluster->m_containerId));
    }
  }
  if (!cluster->m_parentLink.empty()) {
    RagTime5GraphInternal::ClustListParser clustParser(*clusterManager, "PictParentLst");
    m_document.readListZone(cluster->m_parentLink, clustParser);
    m_document.checkClusterList(clustParser.m_clusterList);
  }
  for (auto const &lnk : cluster->m_linksList) {
    if (lnk.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(lnk);
      continue;
    }
    std::stringstream s;
    s << "PictData" << lnk.m_fieldSize;
    RagTime5StructManager::DataParser defaultParser(s.str());
    m_document.readFixedSizeZone(lnk, defaultParser);
  }

  return cluster;
}

////////////////////////////////////////////////////////////
// shape
////////////////////////////////////////////////////////////
std::shared_ptr<RagTime5ClusterManager::Cluster> RagTime5Graph::readGraphicCluster(RagTime5Zone &zone, int zoneType)
{
  std::shared_ptr<RagTime5ClusterManager> clusterManager=m_document.getClusterManager();
  if (!clusterManager) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicCluster: oops can not find the cluster manager\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }
  RagTime5GraphInternal::GraphicCParser parser(*clusterManager, zoneType);
  if (!clusterManager->readCluster(zone, parser) || !parser.getGraphicCluster()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicCluster: oops can not find the cluster\n"));
    return std::shared_ptr<RagTime5ClusterManager::Cluster>();
  }

  auto cluster=parser.getGraphicCluster();
  if (m_state->m_idGraphicMap.find(zone.m_ids[0])!=m_state->m_idGraphicMap.end()) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicCluster: oops graphic zone %d is already stored\n", zone.m_ids[0]));
  }
  else
    m_state->m_idGraphicMap[zone.m_ids[0]]=cluster;
  m_document.checkClusterList(cluster->m_clusterIdsList);

  if (cluster->m_usedZoneId && !readGraphicUsed(cluster->m_usedZoneId)) {
    MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicCluster: the zone id=%d seems bad\n", cluster->m_usedZoneId));
  }

  for (auto &link : cluster->m_transformationLinks)
    readGraphicTransformations(link);
  for (auto &link : cluster->m_dimensionLinks) {
    RagTime5GraphInternal::FloatParser floatParser("GraphDim");
    m_document.readFixedSizeZone(link, floatParser);
  }
  if (!cluster->m_clusterLinks[0].empty()) {
    // change me
    auto data=m_document.getDataZone(cluster->m_clusterLinks[0].m_ids[0]);
    if (!data || data->m_isParsed) {
      MWAW_DEBUG_MSG(("RagTime5Graph::readGraphicCluster: can not find data zone %d\n", cluster->m_clusterLinks[0].m_ids[0]));
    }
    else {
      data->m_hiLoEndian=cluster->m_hiLoEndian;
      m_document.readClusterLinkList(*data, cluster->m_clusterLinks[0], cluster->m_linkList);
    }
  }
  if (!cluster->m_parentLink.empty()) {
    std::vector<RagTime5StructManager::ZoneLink> list;
    m_document.readClusterLinkList(cluster->m_parentLink, list, "GraphParentLst");
  }
  if (!cluster->m_clusterLinks[1].empty()) {
    RagTime5GraphInternal::ClustListParser clustParser(*clusterManager, "GraphClustLst3");
    m_document.readFixedSizeZone(cluster->m_clusterLinks[1], clustParser);
  }
  readGraphicShapes(*cluster);

  // can have some condition formula ?
  for (int wh=0; wh<2; ++wh) {
    auto const &list=wh==0 ? parser.getConditionFormulaLinks() : cluster->m_settingLinks;
    for (auto const &link : list) {
      if (link.empty()) continue;
      RagTime5StructManager::FieldParser defaultParser(wh==0 ? "CondFormula" : "Settings");
      m_document.readStructZone(link, defaultParser, 0);
    }
  }

  for (auto const &lnk : cluster->m_linksList) {
    if (lnk.m_type==RagTime5ClusterManager::Link::L_List) {
      m_document.readListZone(lnk);
    }
    else if (lnk.m_type==RagTime5ClusterManager::Link::L_LongList) {
      std::vector<long> list;
      m_document.readLongList(lnk, list);
    }
    else {
      std::stringstream s;
      s << "Graph_Data" << lnk.m_fieldSize;
      m_document.readFixedSizeZone(lnk, s.str());
    }
  }

  checkGraphicCluster(*cluster);
  return cluster;
}

void RagTime5Graph::checkGraphicCluster(RagTime5GraphInternal::ClusterGraphic &cluster)
{
  // time to check that all is valid and update root list
  std::vector<int> &rootList=cluster.m_rootIdList;
  std::stack<int> toCheck;
  std::multimap<int, int> idToChildIpMap;
  for (auto sIt : cluster.m_idToShapeMap) {
    if (!sIt.second)
      continue;
    auto &shape=*sIt.second;
    if (shape.m_parentId>0 && cluster.m_idToShapeMap.find(shape.m_parentId)==cluster.m_idToShapeMap.end()) {
      MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: find unexpected parent %d for shape %d\n",
                      shape.m_parentId, sIt.first));
      shape.m_parentId=0;
      continue;
    }
    else if (shape.m_parentId>0) {
      idToChildIpMap.insert(std::multimap<int, int>::value_type(shape.m_parentId,sIt.first));
      continue;
    }
    rootList.push_back(sIt.first);
    toCheck.push(sIt.first);
  }

  std::set<int> seens;
  while (true) {
    int posToCheck=0; // to make clang happy
    if (!toCheck.empty()) {
      posToCheck=toCheck.top();
      toCheck.pop();
    }
    else if (seens.size()==cluster.m_idToShapeMap.size())
      break;
    else {
      bool ok=false;
      for (auto sIt : cluster.m_idToShapeMap) {
        if (!sIt.second || seens.find(sIt.first)!=seens.end())
          continue;
        MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: find unexpected root %d\n", sIt.first));
        posToCheck=sIt.first;
        rootList.push_back(sIt.first);

        RagTime5GraphInternal::Shape &shape=*sIt.second;
        shape.m_parentId=0;
        ok=true;
        break;
      }
      if (!ok)
        break;
    }
    if (seens.find(posToCheck)!=seens.end()) {
      MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: oops, %d is already seens\n", posToCheck));
      continue;
    }

    seens.insert(posToCheck);
    auto childIt=idToChildIpMap.lower_bound(posToCheck);
    std::vector<int> badChildList, goodChildList;

    RagTime5GraphInternal::Shape *group=nullptr;
    if (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
      if (cluster.m_idToShapeMap.find(posToCheck)!=cluster.m_idToShapeMap.end() &&
          cluster.m_idToShapeMap.find(posToCheck)->second)
        group=cluster.m_idToShapeMap.find(posToCheck)->second.get();
      if (group && group->m_type!=RagTime5GraphInternal::Shape::S_Group)
        group=nullptr;
      if (!group) {
        MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: oops, %d is not a group\n", posToCheck));
      }
    }
    while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
      int childId=childIt++->second;
      bool ok=group!=nullptr;
      if (ok && seens.find(childId)!=seens.end()) {
        MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: find loop for child %d\n", childId));
        ok=false;
      }
      if (ok) {
        ok=false;
        for (auto child : group->m_childIdList) {
          if (child!=childId)
            continue;
          ok=true;
          break;
        }
        if (!ok) {
          MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: can not find child %d in group %d\n", childId, posToCheck));
        }
      }
      if (!ok) {
        if (cluster.m_idToShapeMap.find(childId)!=cluster.m_idToShapeMap.end() &&
            cluster.m_idToShapeMap.find(childId)->second)
          cluster.m_idToShapeMap.find(childId)->second->m_parentId=0;
        badChildList.push_back(childId);
        continue;
      }
      goodChildList.push_back(childId);
      toCheck.push(childId);
    }
    if (group && group->m_childIdList.size()!=goodChildList.size()) {
      MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: need to update the child list of group %d: %d child->%d new child\n",
                      posToCheck, int(group->m_childIdList.size()), static_cast<int>(goodChildList.size())));
      group->m_childIdList=goodChildList;
    }
    for (auto badId : badChildList) {
      childIt=idToChildIpMap.lower_bound(posToCheck);
      while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
        if (childIt->second==badId) {
          idToChildIpMap.erase(childIt);
          break;
        }
        ++childIt;
      }
    }
  }
  // check that all linkId are valid
  for (auto sIt : cluster.m_idToShapeMap) {
    if (!sIt.second)
      continue;
    auto &shape=*sIt.second;
    if (!shape.m_linkId || shape.m_type==RagTime5GraphInternal::Shape::S_TextBox)
      continue;
    if (shape.m_linkId<1 || shape.m_linkId>=static_cast<int>(cluster.m_linkList.size())) {
      MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: can not find link %d\n", shape.m_linkId));
      shape.m_linkId=0;
      continue;
    }
    auto const &link=cluster.m_linkList[size_t(shape.m_linkId)];
    if (!link.m_dataId || link.m_subZoneId[1]!=shape.m_id) {
      MWAW_DEBUG_MSG(("RagTime5Graph::checkGraphicCluster: link %d seems bad\n", shape.m_linkId));
      shape.m_linkId=0;
      continue;
    }
    shape.m_linkId=link.m_dataId;
    shape.m_partId=link.getSubZoneId(0);
  }
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
