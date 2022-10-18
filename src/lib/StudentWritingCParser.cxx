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

#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stack>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWSection.hxx"
#include "MWAWStringStream.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "StudentWritingCParser.hxx"

/** Internal: the structures of a StudentWritingCParser */
namespace StudentWritingCParserInternal
{
typedef std::pair<int,int> ZoneEntry;
//! an structure of StudentWritingCParserInternal to store the position of a frame and its entries
struct FrameStruct {
  //! constructor
  explicit FrameStruct(int type)
    : m_type(type)
    , m_page(0)
    , m_id()
  {
  }
  //! the frame type
  int m_type;
  //! the page
  int m_page;
  //! the zone id
  ZoneEntry m_id;
  //! the bounding boxes
  MWAWBox2f m_boxes[2];
};

//! an structure of StudentWritingCParserInternal to store the page's data
struct PageStruct {
  //! constructor
  PageStruct()
    : m_pageNumber(0)
    , m_firstChar(0)
    , m_numColumns(0)
  {
  }
  //! the page number
  int m_pageNumber;
  //! the first character
  int m_firstChar;
  //! the number of columns
  int m_numColumns;
  //! the header/footer id
  ZoneEntry m_hfIds[2];
};

//! an structure of StudentWritingCParserInternal to store the position of a picture and its entries
struct PictureStruct {
  //! constructor
  PictureStruct()
    : m_box()
  {
    for (auto &id : m_ids) id=0;
  }
  //! the bounding box
  MWAWBox2f m_box;
  //! the zone id and the zone sub ids
  int m_ids[2];
};

//! an structure of StudentWritingCParserInternal to store the position of a zone and its entries
struct ZoneStruct {
  //! constructor
  ZoneStruct()
    : m_flags(0)
    , m_subZones2(1)
  {
    for (auto &m : m_margins) m=0;
  }
  //! the zone ids
  ZoneEntry m_ids[9];
  //! the zone flag
  int m_flags;
  //! the number of subzone in zone2
  int m_subZones2;
  //! the margins: LTRB
  float m_margins[4];
};

//! a list of entry of StudentWritingCParser defining a zone
struct Zone {
  //! constructor
  Zone(int type, int id)
    : m_type(type)
    , m_id(id)
    , m_idToEntryMap()

    , m_idToParagraphMap()

    , m_idToDataMap()
    , m_idToPageMap()
    , m_frames()
    , m_frameDates()
    , m_idToFrameNoteMap()
    , m_idToFrameBiblioMap()
    , m_idToPictureMap()
    , m_idToObjectMap()
  {
  }

  //! insert a new entry
  bool insert(int id, MWAWEntry const &entry)
  {
    if (m_idToEntryMap.find(id)!=m_idToEntryMap.end()) {
      MWAW_DEBUG_MSG(("StudentWritingCParserInternal::Zone::insert: entry %d already exists\n", id));
      return false;
    }
    m_idToEntryMap[id]=entry;
    return true;
  }
  //! the zone type
  int m_type;
  //! the zone id
  int m_id;
  //! the id to sub zone map
  std::map<int, MWAWEntry> m_idToEntryMap;

  // specific data

  //! a map id to paragraph
  std::map<int, MWAWParagraph> m_idToParagraphMap;

  //! a map id to main zone data, ...
  std::map<int, ZoneStruct> m_idToDataMap;
  //! a map id to page positions, ...
  std::map<int, PageStruct> m_idToPageMap;
  //! a list of frames
  std::vector<FrameStruct> m_frames;
  //! a list of frames date
  std::vector<std::array<int,3> > m_frameDates;
  //! a map id to frame note entry
  std::map<int, ZoneEntry> m_idToFrameNoteMap;
  //! a map id to biblio text
  std::map<int,librevenge::RVNGString> m_idToFrameBiblioMap;
  //! a map id to picture positions, ...
  std::map<int, PictureStruct> m_idToPictureMap;
  //! a map id to an embedded object
  std::map<int, MWAWEmbeddedObject> m_idToObjectMap;
};

////////////////////////////////////////
//! Internal: the state of a StudentWritingCParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)

    , m_isUncompressed(false)
    , m_zones()
    , m_idToZoneMap()

    , m_idToFontNameMap()
    , m_idToFontNameUsed()

    , m_sendZoneSet()
    , m_sendBoxesStack()
  {
  }

  //! try to return a color corresponding to a color id
  bool getColor(int id, MWAWColor &color) const
  {
    if (id<0 || id>15) {
      MWAW_DEBUG_MSG(("StudentWritingCParserInternal::State::getColor: unknown id=%d\n", id));
      return false;
    }
    static uint32_t colors[]= { 0, 0xffffff, 0x838300, 0x808080, 0xc9c9c9,
                                0xff0000, 0xff00, 0xff, 0xffff, 0xff00ff,
                                0xffff00, 0x8f8f, 0x8f00, 0x8f0000, 0x8f,
                                0xb000b0
                              };
    color=MWAWColor(colors[id]);
    return true;
  }
  //! small function to know if a zone with given type exists
  bool checkIfZone(int id, int type) const
  {
    auto const &zId=m_idToZoneMap.find(id);
    return zId!=m_idToZoneMap.end() && zId->second->m_type==type;
  }
  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  //! a flag to know if we have uncompress the data
  bool m_isUncompressed;
  //! the main zone id and the font id
  ZoneEntry m_ids[2];
  //! the list of zone
  std::vector<std::shared_ptr<Zone> > m_zones;
  //! a map id to zone data
  std::map<int,std::shared_ptr<Zone> > m_idToZoneMap;

  //! a map id to font name
  std::map<int,std::string> m_idToFontNameMap;
  //! a set to store the font name used
  std::set<int> m_idToFontNameUsed;

  //! a set to keep the list of send zone (to avoid loop)
  std::set<int> m_sendZoneSet;
  //! a stack of send bounding box (use to send background picture)
  std::stack<MWAWBox2f> m_sendBoxesStack;
};

////////////////////////////////////////
//! Internal: the subdocument of a StudentWritingCParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(StudentWritingCParser &pars, MWAWInputStreamPtr const &input, int id)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_zoneId(id)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    if (MWAWSubDocument::operator!=(doc)) return true;
    auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
    if (!sDoc) return true;
    if (m_zoneId != sDoc->m_zoneId) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the zone id
  int m_zoneId;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("StudentWritingCParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<StudentWritingCParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("StudentWritingCParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendZone(m_zoneId);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
StudentWritingCParser::StudentWritingCParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  setAsciiName("main-1");

  m_state.reset(new StudentWritingCParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

StudentWritingCParser::~StudentWritingCParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void StudentWritingCParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    getParserState()->m_input=decode();
    if (!getInput())
      throw(libmwaw::ParseException());
    m_state->m_isUncompressed=true;
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());

    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendZone(m_state->m_ids[0].first);
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void StudentWritingCParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) throw(libmwaw::ParseException());
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::createDocument: listener already exist\n"));
    return;
  }

  std::vector<MWAWPageSpan> pageList;
  // first find the main zone to look for header/footer
  auto mainIt=m_state->m_idToZoneMap.find(m_state->m_ids[0].first);
  if (mainIt==m_state->m_idToZoneMap.end() || !mainIt->second || mainIt->second->m_type!=5) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::createDocument: can not find the main zone\n"));
    throw (libmwaw::ParseException());
  }

  auto const &mainZone=*mainIt->second;
  if (mainZone.m_idToDataMap.find(1)!=mainZone.m_idToDataMap.end()) {
    auto const &mainStruct=mainZone.m_idToDataMap.find(1)->second;
    bool hasTitlePage=mainStruct.m_ids[7].first>0;
    if (hasTitlePage)
      ++m_state->m_numPages;
    // look also if we need to add a final page for the biblio at the end
    if (mainStruct.m_ids[1].first) {
      auto biblioIt=m_state->m_idToZoneMap.find(mainStruct.m_ids[1].first);
      if (biblioIt!=m_state->m_idToZoneMap.end() && biblioIt->second && !biblioIt->second->m_idToFrameBiblioMap.empty())
        ++m_state->m_numPages;
    }
    // now look for header/footer
    auto const &flags=mainStruct.m_flags;
    bool hasHF[2];
    for (int i=0; i<2; ++i) hasHF[i]=mainStruct.m_ids[i+4].first>0;
    bool hasFirstHF[2]= {hasHF[0] &&(flags&0x800)==0, hasHF[1] &&(flags&0x1000)==0};
    bool needFirstPage=(hasTitlePage && (hasHF[0] || hasHF[1])) || hasHF[0]!=hasFirstHF[0] || hasHF[1]!=hasFirstHF[1];
    for (auto st=0; st<2; ++st) {
      MWAWPageSpan ps(getPageSpan());
      if (st==0) {
        if (!needFirstPage)
          continue;
        ps.setPageSpan(1);
      }
      else
        ps.setPageSpan(m_state->m_numPages+(needFirstPage ? 0 : 1));
      ps.setMarginTop(mainStruct.m_margins[1]);
      ps.setMarginBottom(mainStruct.m_margins[3]);
      ps.setMarginLeft(mainStruct.m_margins[0]);
      ps.setMarginRight(mainStruct.m_margins[2]);
      for (int wh=0; wh<2; ++wh) {
        if ((st==0 && (hasTitlePage || !hasFirstHF[wh])) || (st==1 && !hasHF[wh]))
          continue;
        MWAWHeaderFooter hf(wh==1 ? MWAWHeaderFooter::FOOTER : MWAWHeaderFooter::HEADER, MWAWHeaderFooter::ALL);
        hf.m_subDocument.reset(new StudentWritingCParserInternal::SubDocument(*this, getInput(),mainStruct.m_ids[wh+4].first));
        ps.setHeaderFooter(hf);
      }
      pageList.push_back(ps);
    }
  }
  else {
    MWAW_DEBUG_MSG(("StudentWritingCParser::createDocument: can not find the main zone's structure\n"));
    MWAWPageSpan ps(getPageSpan());
    ps.setPageSpan(m_state->m_numPages+1);
    pageList.push_back(ps);
  }

  //
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool StudentWritingCParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->checkPosition(352))
    return false;

  libmwaw::DebugStream f;
  input->seek(4, librevenge::RVNG_SEEK_SET);
  int val=int(input->readLong(1));
  if (val==3)
    f << "template,";
  else if (val!=2)
    f << "unk=" << val << ",";
  val=int(input->readLong(2));
  if (val!=0x4646)
    f << "f0=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(1));
  if (val) f << "f1=" << val << ",";

  // first block: 220
  // then 124?
  val=int(input->readLong(2)); // 0-4
  switch (val) {
  case 0:
    f << "report,";
    break;
  case 1:
    f << "journal,";
    break;
  case 2:
    f << "sign,";
    break;
  case 3:
    f << "newletter,";
    break;
  case 4:
    f << "letter,";
    break;
  default:
    f << "type=" << val << ",";
    break;
  }

  for (int i=0; i<4; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {2, 1, 0, 0};
    if (val==expected[i]) continue;
    f << "f" << i+2 << "=" << val << ",";
  }
  for (int st=0; st<2; ++st) {
    int cId=int(input->readLong(2));
    int type=int(input->readLong(2));
    m_state->m_ids[st]=std::make_pair(cId,type);
    if (!cId) continue;
    f << (st==0 ? "main" : "font") << "=Z" << cId << ":" << type << ",";
  }
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val==(i==0 ? 1 : 0)) continue;
    f << "f" << i+6 << "=" << val << ",";
  }
  val=int(input->readLong(1));
  if (val) f << "f7=" << val << ",";
  for (int st=0; st<2; ++st) {
    long aPos=input->tell();
    std::string name;
    for (int i=0; i<32; ++i) {
      char c=char(input->readLong(1));
      if (!c)
        break;
      name+=c;
    }
    if (!name.empty()) f << "text" << st << "=" << name << ",";
    input->seek(aPos+32, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "FileHeader-A:";
  for (int i=0; i<60; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "FileHeader-B:";
  val=int(input->readLong(1));
  if (val)
    f << "f0=" << val << ",";
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0x28 /* or aa*/, 0, 0x7cb};
    if (val==expected[i])
      continue;
    if (i==0)
      f << "day=" << val << ",";
    else if (i==2)
      f << "year=" << val << ",";
    else
      f << "f" << i+1 << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) {  // f3=0|2|4
    val=int(input->readLong(1));
    if (val)
      f << "f" << i+3 << "=" << val << ",";
  }
  for (int i=0; i<18; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i+5 << "=" << val << ",";
  }
  for (int i=0; i<12; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0x320, 0, 0x7c, 0x78, 0,
                           0x11f, 1, 0, 0xaea, 0x86f,
                           0x64, 1
                          };
    if (val==expected[i])
      continue;
    f << "g" << i << "=" << val << ",";
  }

  for (int i=0; i<34; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "h" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  if (!readPrintInfo())
    return false;
  input->seek(pos+120, librevenge::RVNG_SEEK_SET);

  while (!input->isEnd()) {
    pos=input->tell();
    if (!input->checkPosition(pos+10))
      break;
    long dataSize=long(input->readLong(4));
    long endPos=pos+10+dataSize;
    if (endPos<pos+10 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    int ids[3];
    for (int i=0; i<3; ++i)
      ids[i]=int(input->readLong(2));

    MWAWEntry entry;
    entry.setBegin(pos);
    entry.setEnd(endPos);
    entry.setId(ids[0]);
    if (m_state->m_idToZoneMap.find(ids[0])==m_state->m_idToZoneMap.end()) {
      m_state->m_zones.push_back(std::make_shared<StudentWritingCParserInternal::Zone>(ids[1], ids[0]));
      m_state->m_idToZoneMap[ids[0]]=m_state->m_zones.back();
    }
    auto &zone=*m_state->m_idToZoneMap.find(ids[0])->second;
    if (zone.m_type!=ids[1] || !zone.insert(ids[2],entry)) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::createZones: find a bad zone type\n"));
      ascii().addPos(pos);
      ascii().addNote("Entries(BadZone):###");
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Unknown):###");
  }

  bool mainZoneFound=false;
  for (auto &zIt : m_state->m_idToZoneMap) {
    int zoneId=zIt.first;
    auto &zone=*zIt.second;

    switch (zone.m_type) {
    case 0:
      for (auto const &dataIt : zone.m_idToEntryMap) {
        auto const &id=dataIt.first;
        auto const &entry=dataIt.second;
        f.str("");
        f << "Entries(End):";
        if (zoneId!=0 || id!=0 || entry.length()!=10) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::createZones: unexpected end zone data\n"));
          f << "###";
        }
        ascii().addPos(entry.begin());
        ascii().addNote(f.str().c_str());
      }
      break;
    case 1:
      readTextZone(zone);
      break;
    case 2:
      for (auto const &dataIt : zone.m_idToEntryMap) {
        auto const &id=dataIt.first;
        auto const &entry=dataIt.second;
        input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);

        f.str("");
        f << "Entries(Data2)[Z" << zoneId << "]:id=" << id << ",";
        if (entry.length()!=10+0x54) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[data2]: unexpected end zone size\n"));
          f << "###";
        }
        else {
          StudentWritingCParserInternal::PageStruct page;
          for (int i=0; i<7; ++i) {
            val=int(input->readLong(2));
            int const expected[]= {1, 0, 0, 0, 1, 1, 0 };
            if (val!=expected[i])
              f << "f" << i << "=" << val << ",";
          }
          for (int st=0; st<2; ++st) {
            int cId=int(input->readLong(2));
            int type=int(input->readLong(2));
            page.m_hfIds[st]=std::make_pair(cId,type);
            if (!cId) continue;
            f << (st==0 ? "pageNumber[header]" : "pageNumber[footer]") << "=Z" << cId << ":" << type << ",";
            if (!m_state->checkIfZone(cId, type)) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[data2]: unexpected zone5 id/size\n"));
              f << "###";
            }
          }
          val=int(input->readLong(2));
          if (val!=1)
            f << "g0=" << val << ",";
          page.m_pageNumber=int(input->readLong(2));
          if (page.m_pageNumber!=1)
            f << "page[number]=" << page.m_pageNumber << ",";
          val=int(input->readULong(2));
          page.m_numColumns=1+(val&7);
          if (page.m_numColumns!=1)
            f << "num[columns]=" << page.m_numColumns << ",";
          val &= 0xfff8;
          if (val)
            f << "fl=" << std::hex << val << std::dec << ",";
          for (int i=0; i<9; ++i) { // g2 related to g0 height?
            val=int(input->readLong(2));
            int const expected[]= { 0xc8, 0x48, 0, 0, 0, 0, 0, 1, 0};
            if (val==expected[i])
              continue;
            if (i==5) {
              page.m_firstChar=val;
              f << "first[char]=" << val << ",";
            }
            else
              f << "g" << i+1 << "=" << val << ",";
          }
          for (int i=0; i<12; ++i) { // h1=h4
            val=int(input->readLong(2));
            if (val)
              f << "h" << i << "=" << val << ",";
          }
          for (int i=0; i<7; ++i) { // k0=small number
            val=int(input->readLong(2));
            int const expected[]= {0, 0x40, 0x78, 0, 0, 0, 0};
            if (val!=expected[i])
              f << "k" << i << "=" << val << ",";
          }
          zone.m_idToPageMap[id]=page;
        }
        ascii().addPos(entry.begin());
        ascii().addNote(f.str().c_str());
      }
      break;
    case 3:
      readFrame(zone);
      break;
    case 4:
      readParagraph(zone);
      break;
    case 5:
      if (zoneId==m_state->m_ids[0].first)
        mainZoneFound=true;
      for (auto const &dataIt : zone.m_idToEntryMap) {
        auto const &id=dataIt.first;
        auto const &entry=dataIt.second;
        f.str("");
        f << "Entries(Data5)[Z" << zoneId << "]:id=" << id << ",";
        input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);

        if (id!=1 || entry.length()!=10+0x72) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[data5]: unexpected end zone id/size\n"));
          f << "###";
        }
        else {
          StudentWritingCParserInternal::ZoneStruct data;
          f << "IDS=[";
          for (int i=0; i<7; ++i) // big number multiple of 4: change for each save
            f << std::hex << input->readULong(4) << std::dec << ",";
          f << "],";
          for (int i=0; i<2; ++i) { // 0
            val=int(input->readULong(2));
            if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
          }
          for (int i=0; i<8; ++i) { // zone2-zone5, pagenumber:5, next zone5, zone1, title page
            int cId=int(input->readLong(2));
            int type=int(input->readLong(2));
            data.m_ids[i]=std::make_pair(cId, type);
            if (cId==0)
              continue;
            char const *what[]= {"pages", "frames", "paragraph", "Zone5",
                                 "header", "footer", "text", "title"
                                };
            if (what[i])
              f << what[i] << "=Z" << cId << ":" << type << ",";
            else
              f << "unkn" << i << "=Z" << cId << ":" << type << ",";
            if (!m_state->checkIfZone(cId, type)) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[data5]: unexpected child id/type\n"));
              f << "###";
            }
          }
          data.m_flags=val=int(input->readULong(2));
          if (val&7)
            f << "num[columns]=" << 1+int(val&7) << ",";
          if (val&0x40)
            f << "has[master,head],";
          if (val&0x800)
            f << "header[first,skip],";
          if (val&0x1000)
            f << "footer[first,skip],";
          if (val&0x4000)
            f << "pagenumber[bottom],";
          if (val&0x8000)
            f << "pagenumber[center/right],";
          val&=0x27b8;
          if (val)
            f << "fl0=" << std::hex << val << std::dec << ",";
          val=int(input->readULong(2)); // 2: header?, 3: footer?
          if (val)
            f << "fl1=" << std::hex << val << std::dec << ",";
          f << "IDS2=" << std::hex << input->readULong(4) << std::dec << ",";
          f << "margins=["; // L, T, R, B
          for (auto &m : data.m_margins) {
            m=float(input->readLong(2))/1000;
            f << m << "in,";
          }
          f << "],";
          val=int(input->readULong(2));
          if (val!=0xc8)
            f << "f2=" << val << ",";
          val=int(input->readULong(2)); // &1: line between column
          if (val)
            f << "fl2=" << std::hex << val << std::dec << ",";
          for (int i=0; i<12; ++i) { // N8 related to page number align ?
            val=int(input->readLong(2));
            int const expected[]= {1,0,0,1,1,
                                   0,0,0/*or 402|404*/,0x64,0x78,
                                   0 /* 0|2|7|9|14|84|97*/, 0 /*0|2|4|5*/
                                  };
            if (val==expected[i]) continue;
            if (i==0) {
              data.m_subZones2=val;
              f << "N[data2]=" << val << ",";
            }
            else
              f << "N" << i << "=" << val << ",";
          }
          int cId=int(input->readLong(2));
          int type=int(input->readLong(2));
          data.m_ids[8]=std::make_pair(cId, type);
          if (cId) {
            f << "bgPict?=Z" << cId << ":" << type << ",";
            if (!m_state->checkIfZone(cId, type)) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[data5]: unexpected picture id/type\n"));
              f << "###";
            }
          }
          val=int(input->readLong(2)); // 0
          if (val)
            f << "f3=" << val << ",";
          zone.m_idToDataMap[id]=data;
        }
        ascii().addPos(entry.begin());
        ascii().addNote(f.str().c_str());
      }
      break;
    case 6:
      readPicture(zone);
      break;
    case 7:
      for (auto const &dataIt : zone.m_idToEntryMap) {
        auto const &id=dataIt.first;
        auto const &entry=dataIt.second;
        f.str("");
        f << "Entries(Fonts)[Z" << zoneId << "]:";
        if (id!=1 || !readFontsList(entry)) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::createZones[fonts]: unexpected id=%d\n", id));
          f << "###";
          ascii().addPos(entry.begin());
          ascii().addNote(f.str().c_str());
        }
      }
      break;
    default:
      for (auto const &dataIt : zone.m_idToEntryMap) {
        auto const &id=dataIt.first;
        auto const &entry=dataIt.second;
        f.str("");
        f << "Entries(Zone" << zone.m_type << "A)[Z" << zoneId << "]:";
        if (id!=1)
          f << "id=" << id << ",";
        ascii().addPos(entry.begin());
        ascii().addNote(f.str().c_str());
      }
      break;
    }
  }
  if (!mainZoneFound) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::createZone: can not found the main zone\n"));
    return false;
  }
  return mainZoneFound;
}

bool StudentWritingCParser::readTextZone(StudentWritingCParserInternal::Zone const &zone)
{
  auto input=getInput();
  if (!input || zone.m_type!=1) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone: called will incorrect zone type\n"));
    return false;
  }
  libmwaw::DebugStream f;
  int val;
  std::map<int,int> textIdNumChars;
  std::map<int,std::array<int,4> > styleIdValues;
  for (auto const &dataIt : zone.m_idToEntryMap) {
    auto const &id=dataIt.first;
    auto const &entry=dataIt.second;
    input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);
    switch (id) {
    case 1: {
      f.str("");
      f << "Entries(TZone)[Z" << zone.m_id << "]:header,";
      if (entry.length()<10+10) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone[header]: the entry seems too short\n"));
        f << "###";
        break;
      }
      val=int(input->readLong(2));
      if (val)
        f << "f0=" << val << ",";

      int begPos[2];
      int N[2];
      f << "zones=[";
      for (int i=0; i<2; ++i) {
        f << "[";
        begPos[i]=int(input->readULong(2));
        f << "pos=" <<10+begPos[i] << ",";
        N[i]=int(input->readLong(2));
        f << "N=" << N[i] << ",";
        if (begPos[i]<10 || N[i]<0 || (entry.length()-10-begPos[i])/10<N[i] || begPos[i]+10+10*N[i]>entry.length()) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone[header]: a sub zone seems bad\n"));
          f << "###";
          N[i]=0;
        }
        f << "],";
      }
      f << "],";
      input->seek(entry.begin()+10+begPos[0], librevenge::RVNG_SEEK_SET);
      f << "text=[";
      for (int n=0; n<N[0]; ++n) {
        f << "[";
        int cId=int(input->readULong(2));
        f << "id=" << cId << ",";
        for (int j=0; j<4; ++j) {
          val=int(input->readLong(2));
          if (j==1)
            textIdNumChars[cId]=val;
          int const expected[]= {0xff0, 0xff0, 0, 0}; // num[max], num[char], 0, 0
          if (val==expected[j])
            continue;
          if (j==0)
            f << "zone[sz]=" << val << ",";
          else if (j==1)
            f << "num[char]=" << val << ",";
          else
            f << "f" << j << "=" << val << ",";
        }
        f << "],";
      }
      f << "],";

      input->seek(entry.begin()+10+begPos[1], librevenge::RVNG_SEEK_SET);
      f << "style=[";
      for (int n=0; n<N[1]; ++n) {
        f << "[";
        int cId=int(input->readULong(2));
        f << "id=" << cId << ",";
        std::array<int,4> values;
        for (size_t j=0; j<4; ++j) {
          values[j]=int(input->readLong(2));
          int const expected[]= {0xff, 0, 0, 0};
          if (values[j]==expected[j]) continue;
          switch (j) {
          case 0:
            f << "numStyle[max]=" << values[j] << ",";
            break;
          case 1:
            f << "numStyle=" << values[j] << ",";
            break;
          case 2:
            f << "f0=" << values[j] << ",";
            break;
          case 3:
          default:
            f << "numChar=" << values[j] << ",";
            break;
          }
        }
        styleIdValues[cId]=values;
        f << "],";
      }
      f << "],";
      break;
    }
    default: {
      auto tId=textIdNumChars.find(id);
      if (tId!=textIdNumChars.end()) {
        int n=tId->second;
        f.str("");
        f << "Entries(TZone)[Z" << zone.m_id << "]:text,id=" << id << ",";
        if (10+n<10 || 10+n>entry.length()) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone: bad number of characters\n"));
          f << "###";
          break;
        }
        std::string text;
        for (int i=0; i<n; ++i) {
          char c=char(input->readLong(1));
          if (!c)
            text+="#[0]";
          else
            text+=c;
        }
        f << text << ",";
        if (input->tell()!=entry.end()) {
          ascii().addPos(input->tell());
          ascii().addNote("_");
          input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
        }
        break;
      }
      auto sIt=styleIdValues.find(id);
      if (sIt!=styleIdValues.end()) {
        f.str("");
        f << "Entries(TZone)[Z" << zone.m_id << "]:style,id=" << id << ",";
        int numStyles=sIt->second[1];
        if (numStyles<0 || entry.length()<10+6*numStyles || (entry.length()-10)/6<numStyles) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone: bad number of style\n"));
          f << "###N=" << numStyles << ",";
          break;
        }
        libmwaw::DebugStream f2;
        int cPos=0;
        for (int i=0; i<numStyles; ++i) {
          long pos=input->tell();
          f2.str("");
          f2 << "TZone-S" << i << ":";
          int type=int(input->readULong(2));
          int nChar=int(input->readLong(2));
          val=int(input->readULong(2));
          if (cPos)
            f2 << "pos=" << cPos << ",";
          cPos+=nChar;
          switch (type) {
          // 0,2-3
          case 0x1:
            f2 << "endNote=F" << val << ",";
            break;
          case 0x2:
            f2 << "setDate=F" << val << ",";
            break;
          // 10-18
          case 0x10:
            f2 << "font,bold=" << val << ","; // true/false
            break;
          case 0x11:
            f2 << "font,italic=" << val << ","; // true/false
            break;
          case 0x12:
            f2 << "font,underline=" << val << ","; // true/false
            break;
          case 0x13:
            f2 << "font,FN" << val << ",";
            break;
          case 0x14:
            f2 << "font,size=" << float(val)/10 << ",";
            break;
          case 0x15:
            f2 << "font,outline=" << val << ","; // true/false
            break;
          case 0x16:
            f2 << "font,color=" << val << ",";
            break;
          case 0x17:
            f2 << "font,sub/super=" << val << ","; // 0: none 1: super, 2: subs
            break;
          case 0x18:
            f2 << "font,shadow=" << val << ","; // true/false
            break;
          // 20-22
          case 0x20: // checkme
            f2 << "page[number],";
            if (val) f << "f0=" << val << ",";
            break;
          case 0x21:
            f2 << "date,form="  << std::hex << val << std::dec << ",";
            break;
          case 0x22:
            f2 << "bullet,";
            if (val) f << "f0=" << val << ",";
            break;

          case 0x100:
            f2 << "para,P" << val << ",";
            break;

          case 0x300:
            f2 << "col[break],col=" << val << ",";
            break;
          case 0x500:
            ++m_state->m_numPages;
            f2 << "page[break],page=" << val << ",";
            break;
          case 0x700:
            f2 << "zone[break]=" << val << ",";
            break;

          default:
            f2 << "type=" << std::hex << type << std::dec << ",";
          }
          ascii().addPos(pos);
          ascii().addNote(f2.str().c_str());
          input->seek(pos+6, librevenge::RVNG_SEEK_SET);
        }
        if (input->tell()!=entry.end()) {
          ascii().addPos(input->tell());
          ascii().addNote("_");
          input->seek(entry.end(), librevenge::RVNG_SEEK_SET);
        }
        break;
      }
      MWAW_DEBUG_MSG(("StudentWritingCParser::readTextZone[header]: find unknown zone\n"));
      f.str("");
      f << "Entries(TZone)[Z" << zone.m_id << "]:id=" << id << "," << "###";
      break;
    }
    }
    if (input->tell()!=entry.end())
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool StudentWritingCParser::readFrame(StudentWritingCParserInternal::Zone &zone)
{
  auto input=getInput();
  if (!input || zone.m_type!=3) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame: called will incorrect zone type\n"));
    return false;
  }
  libmwaw::DebugStream f;
  std::set<int> biblioIds;
  for (auto const &dataIt : zone.m_idToEntryMap) {
    auto const &id=dataIt.first;
    auto const &entry=dataIt.second;
    input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Entries(Frame)[Z" << zone.m_id << "]:";

    int val;
    if (id==0) {
      f << "none,";
      if (entry.length()!=10) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[empty]: find some data\n"));
        f << "###";
      }
    }
    else if (biblioIds.find(id)!=biblioIds.end()) {
      f << "biblio,id=" << id << ",";
      if (entry.length()<10+58) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[biblio]: the entry seems too short\n"));
        f << "###";
      }
      else {
        // f2=1,f3=1,f7=5,f14=2,f15=0 => 9 data
        for (int i=0; i<16; ++i) {
          val=int(input->readLong(2));
          int const expected[]= { 5, 0x275, 0, 0, 1, 0, 4, 2, 0, 0, 0, 0, 0, 0, 1, 2 };
          if (val!=expected[i])
            f << "f" << i << "=" << val << ",";
        }
        f << "IDS=[";
        for (int i=0; i<4; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
        f << "],";
        val=int(input->readLong(2));
        if (val) f << "g0=" << val << ",";
        // a list of strings, the number probably depends on some f_i values...
        // there are added at the end of the files
        auto &fontConverter=getFontConverter();
        librevenge::RVNGString finalText;
        while (input->tell()<entry.end()) {
          long actPos=input->tell();
          librevenge::RVNGString text;
          bool endFound=false;
          while (input->tell()<entry.end()) {
            char c=char(input->readLong(1));
            if (!c) {
              endFound=true;
              break;
            }
            else if (c!=0x9 && c<0x1f)
              break;
            int unicode=fontConverter->unicode(12,(unsigned char) c);
            if (unicode!=-1)
              libmwaw::appendUnicode(uint32_t(unicode),text);
            else
              text.append(c);
          }
          if (!endFound) {
            ascii().addDelimiter(actPos, '|');
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame: can not find text\n"));
            f << "###";
            break;
          }
          if (text.empty())
            continue;
          f << text.cstr() << ",";
          if (!finalText.empty())
            finalText.append(", ");
          finalText.append(text);
        }
        if (!finalText.empty())
          zone.m_idToFrameBiblioMap[id]=finalText;
      }
    }
    else {
      if (id!=1)
        f << "id=" << id << ",";
      if (entry.length()<10+4) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main]: the size seems bad\n"));
        f << "###";
      }
      else {
        int zType=int(input->readLong(2));
        switch (zType) {
        case 2: {
          f << "list,";
          if (entry.length()<10+16) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main-2]: the size seems bad\n"));
            f << "###";
            break;
          }
          int page=0; // 0: means background?
          for (int i=0; i<6; ++i) { // f2=0-1
            val=int(input->readLong(2));
            if (!val) continue;
            if (i==0)
              f << "next=" << val << ",";
            else if (i==1)
              f << "prev=" << val << ",";
            else if (i==2) {
              page=val;
              f << "page=" << val << ",";
            }
            else
              f << "f" << i << "=" << val << ",";
          }
          int n=int(input->readLong(2));
          f << "n=" << n << ",";
          if (n<0 || (entry.length()-26)/28<n) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main-2]: the number of sub zone seems bad\n"));
            f << "###";
            break;
          }
          libmwaw::DebugStream f2;
          for (int i=0; i<n; ++i) {
            long pos=input->tell();
            f2.str("");
            f2 << "Frame-F" << i << ":";
            StudentWritingCParserInternal::FrameStruct frame(2);
            frame.m_page=page;
            int cId=int(input->readLong(2));
            int type=int(input->readLong(2));
            frame.m_id=std::make_pair(cId,type);
            if (cId) {
              f2 << "content=Z" << cId << ":" << type << ",";
              if (!m_state->checkIfZone(cId, type)) {
                MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame: unexpected id/type\n"));
                f2 << "###";
              }
            }
            float dim[4];
            for (auto &d : dim) d=float(input->readLong(2))/14; // unit ?
            frame.m_boxes[0]=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
            f2 << "box=" << frame.m_boxes[0] << ",";
            for (auto &d : dim) d=float(input->readLong(2));
            frame.m_boxes[1]=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[2],dim[3]));
            f2 << "box2=" << frame.m_boxes[1] << ",";
            for (int j=0; j<2; ++j) { // 0|1
              val=int(input->readLong(2));
              if (val)
                f2 << "f" << i << "=" << val << ",";
            }
            val=int(input->readULong(2));
            f2 << "border=[";
            if (val==0)
              f << "none,";
            else {
              if (val&0x80) f2 << "L";
              if (val&0x100) f2 << "T";
              if (val&0x200) f2 << "R";
              if (val&0x400) f2 << "B";
              f2 << ":";
              if ((val&0x7)!=1) f2 << "style=" << (val&0x7) << ",";
              if (((val>>3)&0xf)!=0) f2 << "color=" << ((val>>3)&0xf) << ","; // the color are not in the same order than text color, 0=black, 1=yellow, 2=red .. 15=white
              if (val&0x800) f2 << "shade:"; // background=gray
              val&=0xf000;
              if (val) f2 << "fl=" << std::hex << val << std::dec << ",";
            }
            f2 << "],";
            val=int(input->readLong(2)); // 0
            if (val) f2 << "f2=" << val << ",";
            ascii().addPos(pos);
            ascii().addNote(f2.str().c_str());
            input->seek(pos+28, librevenge::RVNG_SEEK_SET);
            zone.m_frames.push_back(frame);
          }
          if (input->tell()<entry.end()) {
            ascii().addPos(input->tell());
            ascii().addNote("_");
          }
          break;
        }
        case 3: {
          f << "note,";
          if (entry.length()<10+16) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[note]: the zone seems too short\n"));
            f << "###,";
            break;
          }
          int cId=int(input->readLong(2));
          int type=int(input->readLong(2));
          if (cId) {
            f << "Z" << cId << ":" << type << ",";
            if (!m_state->checkIfZone(cId, type)) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame: unexpected type\n"));
              f << "###";
            }
          }
          for (int i=0; i<6; ++i) { // f1: small number
            val=int(input->readLong(2));
            if (!val) continue;
            f << "f" << i << "=" << val << ",";
          }
          zone.m_idToFrameNoteMap[id]=std::make_pair(cId,type);
          break;
        }
        case 5: {
          f << "biblio,";
          int n=int(input->readLong(2));
          if (n<0 || entry.length()<10+4+2*n) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[biblio]: can not find the number of id\n"));
            f << "###n=" << n << ",";
            break;
          }
          f << "ids=[";
          for (int i=0; i<n; ++i) {
            val=int(input->readLong(2));
            biblioIds.insert(val);
            f << val << ",";
          }
          f << "],";
          break;
        }
        case 6: {
          int n=int(input->readLong(2));
          f << "n=" << n << ",";
          if (entry.length()<10+8+6*n) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main:6]: the entry seems too short\n"));
            f << "###";
            break;
          }
          for (int i=0; i<2; ++i) {
            val=int(input->readLong(2));
            if (val)
              f << "f" << i << "=" << val << ",";
          }
          if (!zone.m_frameDates.empty()) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main:6]: oops, we have already found a date list\n"));
            f << "###";
          }
          f << "dates=[";
          for (int i=0; i<n; ++i) {
            f << "[";
            std::array<int,3> date; // year,month,day
            for (size_t j=0; j<3; ++j) date[j]=int(input->readLong(j==0 ? 2 : 1));
            f << date[0] << "/" << date[1] << "/" << date[2] << ",";
            val=int(input->readLong(2));
            if (val) f << "hours?=" << std::hex << val << std::dec << ",";
            f << "],";
            zone.m_frameDates.push_back(date);
          }
          f << "],";
          break;
        }
        default:
          MWAW_DEBUG_MSG(("StudentWritingCParser::readFrame[main:%d]: unknown type\n", zType));
          f << "##zType=" << zType << ",";
          break;
        }
      }
    }
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool StudentWritingCParser::readParagraph(StudentWritingCParserInternal::Zone &zone)
{
  auto input=getInput();
  if (!input || zone.m_type!=4) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: called will incorrect zone type\n"));
    return false;
  }
  libmwaw::DebugStream f;
  std::set<int> cIds;
  bool first=true;
  std::map<int,MWAWParagraph> &idToPara=zone.m_idToParagraphMap;
  for (auto const &dataIt : zone.m_idToEntryMap) {
    auto const &id=dataIt.first;
    auto const &entry=dataIt.second;
    input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Entries(Paragraph)[Z" << zone.m_id << "]:";
    if (first) {
      f << "id=" << id << ",";
      // normally, begin with id=1 but find some case where the first id=6 and data in id=7
      f << "header,";
      if (entry.length()<10+4) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: the first entry seems bad\n"));
        f << "###";
      }
      else {
        int N[2];
        for (int i=0; i<2; ++i) {
          N[i]=int(input->readLong(2));
          if (N[i]==0)
            continue;
          if (i==0)
            f << "N=" << N[i] << ",";
          else {
            MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: find unknown value for N1\n"));
            f << "N1=" << N[i] << ",###";
          }
        }
        if (N[0]<0 || (entry.length()-14)/4<N[0]) {
          MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: the value for N0 seems bad\n"));
          f << "###";
          N[0]=0;
        }
        f << "zones=[";
        for (int i=0; i<N[0]; ++i) {
          f << "[";
          f << std::hex << input->readULong(2) << std::dec << ','; // unknown big number
          int cId=int(input->readLong(2));
          cIds.insert(cId);
          f << cId << ",";
          f << "],";
        }
        f << "],";
      }
      first=false;
    }
    else if (cIds.find(id)==cIds.end() || entry.length()!=10+0x5a) {
      f << "id=" << id << ",";
      MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: find unexpected zone\n"));
      f << "###";
    }
    else {
      f << "P" << id << ",";
      MWAWParagraph para;
      para.m_marginsUnit=librevenge::RVNG_POINT;
      for (int i=0; i<3; ++i) {
        int val=int(input->readLong(2));
        para.m_margins[i]=double(val)/20;
      }
      *para.m_margins[1]+=*para.m_margins[0];
      int val=int(input->readLong(1)); // 0
      if (val) f << "f0=" << val << ",";
      val=int(input->readULong(1)); // 0-3
      switch (val&3) {
      case 0:// left
      default:
        break;
      case 1:
        para.m_justify=MWAWParagraph::JustificationCenter;
        break;
      case 2:
        para.m_justify=MWAWParagraph::JustificationFull;
        break;
      case 3:
        para.m_justify=MWAWParagraph::JustificationRight;
        break;
      }
      if (val&0xfc) f << "fl1=" << std::hex << (val&0xfc) << std::dec << ",";
      val=int(input->readULong(1));
      if (val&3)
        para.setInterline(1+double(val&3)/2, librevenge::RVNG_PERCENT);
      if (val&0xfc) f << "fl2=" << std::hex << (val&0xfc) << std::dec << ",";
      int N=int(input->readLong(1));
      if (N<0 || N>20) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readParagraph: the number of tabs seems bad\n"));
        f << "###N=" << N << ",";
        N=0;
      }
      for (int i=0; i<N; ++i) {
        MWAWTabStop tab;
        val=int(input->readULong(2));
        switch (val&3) {
        case 0: // left
        default:
          break;
        case 1:
          tab.m_alignment=MWAWTabStop::CENTER;
          break;
        case 2:
          tab.m_alignment=MWAWTabStop::RIGHT;
          break;
        case 3:
          tab.m_alignment=MWAWTabStop::DECIMAL;
          break;
        }
        tab.m_position=double(input->readLong(2))/20/72;
        para.m_tabs->push_back(tab);
        if (val&0xfffc)
          f << "#tab" << i << "=" << std::hex << (val&0xfffc) << std::dec << ",";;
      }
      f << para << ",";
      idToPara[id]=para;
    }
    if (input->tell()!=entry.end())
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool StudentWritingCParser::readPicture(StudentWritingCParserInternal::Zone &zone)
{
  auto input=getInput();
  if (!input || zone.m_type!=6) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readPicture: called will incorrect zone type\n"));
    return false;
  }
  libmwaw::DebugStream f;
  std::set<int> pictIds;
  for (auto const &dataIt : zone.m_idToEntryMap) {
    auto const &id=dataIt.first;
    auto const &entry=dataIt.second;
    input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Entries(Picture)[Z" << zone.m_id << "]:id=" << id << ",";
    switch (id) {
    case 1: {
      if (entry.length()!=10+30) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readPicture: the first entry seems bad\n"));
        f << "###";
        break;
      }
      StudentWritingCParserInternal::PictureStruct pictEntry;
      float fDim[4];
      for (auto &d : fDim) d=float(input->readLong(2))/20; // checkme unit?
      pictEntry.m_box=MWAWBox2f(MWAWVec2f(fDim[0],fDim[1]), MWAWVec2f(fDim[2],fDim[3]));
      f << "box=" << pictEntry.m_box << ",";
      for (int i=0; i<4; ++i) { // 0
        int val=int(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      int dim[2];
      for (auto &d : dim) d=int(input->readLong(2));
      f << "res?=" << MWAWVec2i(dim[0],dim[1]) << ",";
      int val=int(input->readULong(2)); // 0|10|12
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
      val=int(input->readULong(2));
      if (val!=2) f << "f4=" << val << ",";
      int cIds[3];
      for (int &cId : cIds) cId=int(input->readLong(2));
      pictEntry.m_ids[0]=cIds[0];
      pictEntry.m_ids[1]=cIds[2];
      if (cIds[0]==zone.m_id && cIds[1]==6)
        pictIds.insert(cIds[2]);
      else {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readPicture: reading picture in other zone is not implemented\n"));
        f << "###";
      }
      f << "Z" << cIds[0] << ":" << cIds[2] << ",";
      zone.m_idToPictureMap[id]=pictEntry;
      break;
    }
    default:
      if (pictIds.find(id)==pictIds.end()) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readPicture: find unknown id=%d\n", id));
        f << "###";
        break;
      }
      if (entry.length()<10+20) {
        MWAW_DEBUG_MSG(("StudentWritingCParser::readPicture: the picture size seems to short\n"));
        f << "###";
        break;
      }
      std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(entry.length()-10)));
      MWAWEmbeddedObject object;
      if (pict && pict->getBinary(object) && !object.m_dataList.empty()) {
        zone.m_idToObjectMap[id]=object;
#ifdef DEBUG_WITH_FILES
        static int volatile pictName = 0;
        libmwaw::DebugStream f2;
        f2 << "PICT-" << ++pictName << ".pct";
        libmwaw::Debug::dumpFile(object.m_dataList[0], f2.str().c_str());
        ascii().skipZone(entry.begin()+10, entry.end()-1);
#endif
      }
      break;
    }
    if (input->tell()!=entry.end())
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool StudentWritingCParser::readFontsList(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  long endPos=entry.end();
  if (entry.length()<10+36 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readFontsList: the entry seems too short\n"));
    return false;
  }
  input->seek(entry.begin()+10, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Fonts):";
  bool isMain=true;
  if (entry.id()!=m_state->m_ids[1].first) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::readFontsList: find multiple fonts list zones\n"));
      first=false;
    }
    f << "zone=Z" << entry.id() << ",";
    isMain=false;
  }
  int val=int(input->readLong(2));
  if (val!=0x14)
    f << "f0=" << val << ",";
  int N=int(input->readLong(2));
  f << "N=" << N << ",";
  if ((entry.length()-10-36)/34<N || N<0) {
    f << "###";
    MWAW_DEBUG_MSG(("StudentWritingCParser::readFontsList: can not read the number of entries\n"));
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    return true;
  }
  for (int i=0; i<16; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i+1 << "=" << val << ",";
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    long pos=input->tell();
    f.str("");
    f << "Fonts-FN" << i << ":";
    f << "f0=" << input->readLong(2) << ","; // unsure, number between -500 and 600, can be dupplicated in the same files
    std::string name;
    for (int j=0; j<32; ++j) {
      char c=char(input->readLong(1));
      if (!c)
        break;
      name+=c;
    }
    if (!name.empty() && isMain)
      m_state->m_idToFontNameMap[i]=name;
    f << name << ",";
    input->seek(pos+34, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readFontsList: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Fonts-Extra:###");
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool StudentWritingCParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+0x78)) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::readPrintInfo: the entry seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  // print info
  libmwaw::PrinterInfo info;
  if (!info.read(input)) return false;
  f << "Entries(PrintInfo):"<< info;

  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }

  // define margin from print info
  MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
  MWAWVec2i rBotMargin=info.paper().size() - info.page().size();

  // move margin left | top
  int decalX = lTopMargin.x() > 14 ? lTopMargin.x()-14 : 0;
  int decalY = lTopMargin.y() > 14 ? lTopMargin.y()-14 : 0;
  lTopMargin -= MWAWVec2i(decalX, decalY);
  rBotMargin += MWAWVec2i(decalX, decalY);

  // decrease right | bottom
  int rightMarg = rBotMargin.x() -50;
  if (rightMarg < 0) rightMarg=0;
  int botMarg = rBotMargin.y() -50;
  if (botMarg < 0) botMarg=0;

  getPageSpan().setMarginTop(lTopMargin.y()/72.0);
  getPageSpan().setMarginBottom(botMarg/72.0);
  getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
  getPageSpan().setMarginRight(rightMarg/72.0);
  getPageSpan().setFormLength(paperSize.y()/72.);
  getPageSpan().setFormWidth(paperSize.x()/72.);

  if (input->tell()!=pos+0x78)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool StudentWritingCParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = StudentWritingCParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  if (!input->checkPosition(352+120)) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(4) != 0x1a544c43)
    return false;
  int val=int(input->readULong(1));
#ifndef DEBUG
  if (val<2 || val>3)
    return false;
#else
  if (val>5) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::checkHeader: unexpected document type\n"));
    return false;
  }
#endif
  if (input->readULong(2) != 0x4646)
    return false;

  ascii().addPos(0);
  ascii().addNote("FileHeader:");

  if (header)
    header->reset(MWAWDocument::MWAW_T_STUDENTWRITING, 1);

  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool StudentWritingCParser::sendZone(int id)
{
  if (m_state->m_sendZoneSet.find(id)!=m_state->m_sendZoneSet.end()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: oops, find a loop for zone %d\n", id));
    return false;
  }

  auto input=getInput();
  auto listener=getTextListener();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: called with no listener\n"));
    return false;
  }

  auto const &it=m_state->m_idToZoneMap.find(id);
  if (it==m_state->m_idToZoneMap.end() || !it->second) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: unknown zone=%d\n", id));
    return false;
  }

  auto const &zone=*it->second;
  if (zone.m_type!=5) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: sending a zone with type=%d is not implemented\n", zone.m_type));
    return false;
  }
  bool isMainZone=id==m_state->m_ids[0].first;
  if (zone.m_idToDataMap.find(1)==zone.m_idToDataMap.end()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: can not find the main sub zone\n"));
    return false;
  }

  m_state->m_sendZoneSet.insert(id);
  auto const &mainData=zone.m_idToDataMap.find(1)->second;
  //
  // background picture
  //
  if (mainData.m_ids[8].first) {
    auto const &pictIt=m_state->m_idToZoneMap.find(mainData.m_ids[8].first);
    if (pictIt==m_state->m_idToZoneMap.end() || !pictIt->second || pictIt->second->m_type!=6) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone[background]: can not find picture=%d\n", mainData.m_ids[8].first));
    }
    else if (!m_state->m_sendBoxesStack.empty()) {
      // FIXME: the text hides the picture...
      MWAWPosition pos(MWAWVec2f(0,0), m_state->m_sendBoxesStack.top().size(), librevenge::RVNG_POINT);
      pos.setRelativePosition(MWAWPosition::Frame);
      pos.m_wrapping = MWAWPosition::WBackground;

      sendPicture(pos, mainData.m_ids[8].first);
    }
    else {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: oops, sending background picture is not implemented\n"));
    }
  }
  //
  // FRAMES
  //
  auto const &frameIt=m_state->m_idToZoneMap.find(mainData.m_ids[1].first);
  if (frameIt==m_state->m_idToZoneMap.end() || !frameIt->second || frameIt->second->m_type!=3) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: can not find the frame zone=%d\n", mainData.m_ids[1].first));
  }
  else {
    auto const &frames=*frameIt->second;
    for (auto const &frame : frames.m_frames) {
      MWAWPosition pos(frame.m_boxes[0][0], frame.m_boxes[0].size(), librevenge::RVNG_POINT);
      pos.setRelativePosition(isMainZone ? MWAWPosition::Page : MWAWPosition::Frame);
      if (isMainZone && frame.m_page)
        pos.setPage(frame.m_page+(mainData.m_ids[7].first ? 1 : 0));
      pos.m_wrapping = MWAWPosition::WDynamic;

      if (frame.m_id.second==5) {
        m_state->m_sendBoxesStack.push(frame.m_boxes[0]);
        auto subdoc=std::make_shared<StudentWritingCParserInternal::SubDocument>(*this, input,frame.m_id.first);
        listener->insertTextBox(pos, subdoc);
        m_state->m_sendBoxesStack.pop();
      }
      else if (frame.m_id.second==6)
        sendPicture(pos, frame.m_id.first);
      else {
        MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone: find unexpected sub zone type=%d\n", frame.m_id.second));
      }
    }
  }
  if (mainData.m_ids[7].first) {
    // we need to retrieve the title page
    auto const &titleIt=m_state->m_idToZoneMap.find(mainData.m_ids[7].first);
    if (titleIt==m_state->m_idToZoneMap.end() || !titleIt->second || titleIt->second->m_type!=1) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone[background]: can not find title page=%d\n", mainData.m_ids[7].first));
    }
    else {
      sendText(*titleIt->second, zone, id==m_state->m_ids[0].first);
      listener->insertBreak(MWAWListener::PageBreak);
    }
  }

  auto const &pageIt=m_state->m_idToZoneMap.find(mainData.m_ids[6].first);
  if (pageIt==m_state->m_idToZoneMap.end() || !pageIt->second || pageIt->second->m_type!=1) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendZone[background]: can not find text page=%d\n", mainData.m_ids[6].first));
    return false;
  }
  bool result=sendText(*pageIt->second, zone, id==m_state->m_ids[0].first);

  if (id==m_state->m_ids[0].first && frameIt!=m_state->m_idToZoneMap.end() &&
      !frameIt->second->m_idToFrameBiblioMap.empty()) {
    listener->insertBreak(MWAWListener::PageBreak);
    // reset to default
    listener->setFont(MWAWFont());
    MWAWParagraph para;
    para.m_justify=MWAWParagraph::JustificationCenter;
    listener->setParagraph(para);
    listener->insertUnicodeString("Bibliography");
    listener->insertEOL();
    para.m_justify=MWAWParagraph::JustificationLeft;
    listener->setParagraph(para);
    listener->insertEOL();
    for (auto const &biblioIt : frameIt->second->m_idToFrameBiblioMap) {
      listener->insertUnicode(0x2022);
      listener->insertChar(' ');
      listener->insertUnicodeString(biblioIt.second);
      listener->insertChar('.');
      listener->insertEOL();
    }
  }
  m_state->m_sendZoneSet.erase(id);
  return result;
}

bool StudentWritingCParser::sendPicture(MWAWPosition const &pos, int id)
{
  auto listener=getTextListener();
  auto it=m_state->m_idToZoneMap.find(id);
  if (!listener || it==m_state->m_idToZoneMap.end() || !it->second || it->second->m_type!=6) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendPicture: can not find picture %d\n", id));
    return false;
  }
  auto const *zone=it->second.get();
  auto const &pIt=zone->m_idToPictureMap.find(1);
  if (pIt==zone->m_idToPictureMap.end()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendPicture: can not find the picture header for id=%d\n", id));
    return false;
  }

  int ids[]= {pIt->second.m_ids[0], pIt->second.m_ids[1]};
  if (ids[0]!=id) {
    it=m_state->m_idToZoneMap.find(ids[0]);
    if (it==m_state->m_idToZoneMap.end() || !it->second || it->second->m_type!=6)  {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendPicture: can not find the picture final zone %d\n", ids[0]));
      return false;
    }
    zone=it->second.get();
  }
  auto const &objIt=zone->m_idToObjectMap.find(ids[1]);
  if (objIt==zone->m_idToObjectMap.end()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendPicture: can not find the embedded picture in zone %d[%d]\n", ids[0], ids[1]));
    return false;
  }
  listener->insertPicture(pos,objIt->second);
  return true;
}

bool StudentWritingCParser::sendText(StudentWritingCParserInternal::Zone const &textZone,
                                     StudentWritingCParserInternal::Zone const &zone, bool isMain)
{
  auto input=getInput();
  auto listener=getTextListener();
  if (!input || !listener || zone.m_type!=5 || textZone.m_type!=1 || zone.m_idToDataMap.find(1)==zone.m_idToDataMap.end()) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: called with bad data\n"));
    return false;
  }

  auto const &mainData=zone.m_idToDataMap.find(1)->second;

  // retrieve the paragraph zone
  auto const &paraIt=m_state->m_idToZoneMap.find(mainData.m_ids[2].first);
  std::map<int, MWAWParagraph> const *paraIdMap=nullptr;
  if (paraIt==m_state->m_idToZoneMap.end() || !paraIt->second || paraIt->second->m_type!=4) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find the para zone=%d\n", mainData.m_ids[2].first));
  }
  else
    paraIdMap=&paraIt->second->m_idToParagraphMap;
  // retrieve the list of sub zone
  auto const &subIt=m_state->m_idToZoneMap.find(mainData.m_ids[0].first);
  std::map<int,StudentWritingCParserInternal::PageStruct const *> pageLimits;
  if (subIt==m_state->m_idToZoneMap.end() || !subIt->second || subIt->second->m_type!=2) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find the sub zone=%d\n", mainData.m_ids[0].first));
  }
  else {
    for (auto const &p : subIt->second->m_idToPageMap)
      pageLimits[p.second.m_firstChar] = &p.second;
  }
  // retrieve the note entries, dates
  auto const &frameIt=m_state->m_idToZoneMap.find(mainData.m_ids[1].first);
  std::vector<std::array<int,3> > dates;
  std::map<int, StudentWritingCParserInternal::ZoneEntry> const *idToFrameNoteMap=nullptr;
  if (frameIt==m_state->m_idToZoneMap.end() || !frameIt->second || frameIt->second->m_type!=3) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find the frame zone=%d\n", mainData.m_ids[1].first));
  }
  else {
    dates=frameIt->second->m_frameDates;
    idToFrameNoteMap=&frameIt->second->m_idToFrameNoteMap;
  }

  auto const &mainIt=textZone.m_idToEntryMap.find(1);
  if (mainIt==textZone.m_idToEntryMap.end() || !mainIt->second.valid() || mainIt->second.length()<10+10) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find the main zone\n"));
    return false;
  }
  auto const &entry=mainIt->second;
  input->seek(entry.begin()+10+2, librevenge::RVNG_SEEK_SET);

  int begPos[2];
  int N[2];
  for (int i=0; i<2; ++i) {
    begPos[i]=int(input->readULong(2));
    N[i]=int(input->readLong(2));
    if (begPos[i]<10 || N[i]<0 || (entry.length()-10-begPos[i])/10<N[i] || begPos[i]+10+10*N[i]>entry.length()) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: a sub zone seems bad\n"));
      N[i]=0;
    }
  }

  input->seek(entry.begin()+10+begPos[0], librevenge::RVNG_SEEK_SET);
  std::vector<std::array<int,5> > textData;
  std::array<int,5> values; // text: zone[id], num[max], num[char], 0, 0
  for (int n=0; n<N[0]; ++n) {
    for (auto &v : values) v=int(input->readLong(2));
    textData.push_back(values);
  }

  input->seek(entry.begin()+10+begPos[1], librevenge::RVNG_SEEK_SET);
  std::vector<std::array<int,5> > styleData;
  // style: zone[id], num[max], num[style], 0, num[char]
  for (int n=0; n<N[1]; ++n) {
    for (auto &v : values) v=int(input->readLong(2));
    styleData.push_back(values);
  }

  // first retrieve the styles data
  std::vector<std::array<int,3> > styles; // type, numChar, values
  for (auto const &st : styleData) {
    auto const &stIt=textZone.m_idToEntryMap.find(st[0]);
    if (stIt==textZone.m_idToEntryMap.end() || !stIt->second.valid() || st[2]<0 || stIt->second.length()<10+6*st[2]) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find style zone=%d\n", st[0]));
      break;
    }
    input->seek(stIt->second.begin()+10, librevenge::RVNG_SEEK_SET);
    std::array<int,3> data;
    for (int i=0; i<st[2]; ++i) {
      for (auto &d : data) d=int(input->readULong(2));
      styles.push_back(data);
    }
  }

  // now retrieve the text
  int actChar=0, actStyleChar=0;
  std::array<int,3> actDate= {{0,0,0}};
  auto stIt=styles.begin();
  MWAWFont font;
  int numColumns=1;
  for (auto const &txt : textData) {
    auto const &txtIt=textZone.m_idToEntryMap.find(txt[0]);
    if (txtIt==textZone.m_idToEntryMap.end() || !txtIt->second.valid() || txt[2]<0 || txtIt->second.length()<10+txt[2]) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find text zone=%d\n", txt[0]));
      actChar=-1;
      continue;
    }
    input->seek(txtIt->second.begin()+10, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<txt[2]; ++i) {
      auto const &sectIt=pageLimits.find(actChar);
      if (sectIt!=pageLimits.end() && sectIt->second) {
        auto const &page=*sectIt->second;
        if (page.m_numColumns!=numColumns && page.m_numColumns>=1) {
          if (listener->isSectionOpened())
            listener->closeSection();
          numColumns=page.m_numColumns;
          if (numColumns>1) {
            MWAWSection section;
            // CHANGEME: use margins, etc...
            section.setColumns(numColumns, getPageSpan().getPageWidth()/double(numColumns), librevenge::RVNG_INCH);
            listener->openSection(section);
          }
        }
      }
      bool isSpecialChar=false;
      while (actStyleChar<=actChar && stIt!=styles.end()) {
        auto data=*(stIt++);
        if (data[0]==1) {
          isSpecialChar=true; // with 0xb
          if (!idToFrameNoteMap || idToFrameNoteMap->find(data[2])==idToFrameNoteMap->end()) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not retrieve the note=%d\n", data[2]));
          }
          else {
            MWAWSubDocumentPtr subdoc(new StudentWritingCParserInternal::SubDocument(*this, input, idToFrameNoteMap->find(data[2])->second.first));
            listener->insertNote(MWAWNote(MWAWNote::EndNote), subdoc);
          }
        }
        else if (data[0]==2) {
          isSpecialChar=true; // with 0x7
          if (data[2]>0 && data[2]<=int(dates.size()))
            actDate=dates[size_t(data[2]-1)];
          else {
            if (data[2]) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not retrieve the actual date=%d\n", data[2]));
            }
            actDate= {{0,0,0}};
          }
        }
        else if (data[0]>=0x10 && data[0]<=0x18) {
          uint32_t flags=font.flags();
          switch (data[0]) {
          case 0x10:
          case 0x11:
          case 0x15:
          case 0x18: {
            uint32_t fl=data[0]==0x10 ? MWAWFont::boldBit :
                        data[0]==0x11 ? MWAWFont::italicBit :
                        data[0]==0x15 ? MWAWFont::outlineBit : MWAWFont::shadowBit;
            if (data[2]==1)
              flags|=fl;
            else if (data[2]==0)
              flags&=uint32_t(~fl);
            else {
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: unexpected flag\n"));
            }
            break;
          }
          case 0x12:
            if (data[2]==1)
              font.setUnderlineStyle(MWAWFont::Line::Simple);
            else if (data[2]==0)
              font.setUnderlineStyle(MWAWFont::Line::None);
            else {
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: unexpected underline flag\n"));
            }
            break;
          case 0x13: {
            auto const &nameIt=m_state->m_idToFontNameMap.find(data[2]);
            if (nameIt==m_state->m_idToFontNameMap.end()) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: can not find font FN%d\n", data[2]));
              break;
            }
            if (m_state->m_idToFontNameUsed.find(data[2])==m_state->m_idToFontNameUsed.end()) {
              getFontConverter()->setCorrespondance(data[2], nameIt->second);
              m_state->m_idToFontNameUsed.insert(data[2]);
            }
            font.setId(data[2]);
            break;
          }
          case 0x14:
            if (data[2]<=0) {
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: the font size=%d seems bad\n", data[2]));
              break;
            }
            font.setSize(float(data[2])/10);
            break;
          case 0x16: {
            MWAWColor color;
            if (m_state->getColor(data[2], color))
              font.setColor(color);
            else
              font.setColor(MWAWColor::black());
            break;
          }
          case 0x17:
            switch (data[2]) {
            case 0:
              font.set(MWAWFont::Script());
              break;
            case 1:
              font.set(MWAWFont::Script::super100());
              break;
            case 2:
              font.set(MWAWFont::Script::sub100());
              break;
            default:
              MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: unknown script=%d\n", data[2]));
              break;
            }
            break;
          default:
            MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: unexpected type=%d\n", data[0]));
            break;
          }
          font.setFlags(flags);
          listener->setFont(font);
        }
        else if (data[0]>=0x20 && data[0]<=0x22) {
          isSpecialChar=true; // 0xb
          if (data[0]==0x20)
            listener->insertField(MWAWField(MWAWField::PageNumber));
          else if (data[0]==0x21) {
            if (actDate[0]) { // fixme, use a real date field with a fixed date
              std::stringstream s;
              s << actDate[1] << "/" << actDate[2] << "/" << actDate[0]-1;
              listener->insertUnicodeString(librevenge::RVNGString(s.str().c_str()));
            }
            else {
              MWAWField date(MWAWField::Date);
              date.m_DTFormat = "%a, %b %d, %Y";
              listener->insertField(date);
            }
          }
          else
            listener->insertUnicode(0x2022);
        }
        else if (data[0]==0x100) {
          if (paraIdMap) {
            auto const &it=paraIdMap->find(data[2]);
            if (it!=paraIdMap->end())
              listener->setParagraph(it->second);
          }
          else {
            MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: unknown paragraph id=%d\n", data[2]));
          }
        }
        else if (data[0]==0x300) { // column break
          if (data[2]>0 && data[2]<=numColumns)
            listener->insertBreak(MWAWListener::ColumnBreak);
        }
        else if (data[0]==0x500) {
          if (isMain)
            listener->insertBreak(MWAWListener::PageBreak);
        }
        else {
        }
        actStyleChar+=data[1];
      }
      unsigned char c=(unsigned char)(input->readULong(1));
      switch (c) {
      case 0x9:
        listener->insertTab();
        break;
      case 0xd:
        listener->insertEOL();
        break;
      default:
        if (c<0x1f) {
          if (!isSpecialChar) {
            MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: find odd char c=%d\n", int(c)));
          }
        }
        else
          listener->insertCharacter(c);
        break;
      }
      if (actChar>=0) ++actChar;
    }
  }
  if (listener->isSectionOpened())
    listener->closeSection();
  return true;
}


////////////////////////////////////////////////////////////
// decoder
////////////////////////////////////////////////////////////
namespace StudentWritingCParserInternal
{
/** a basic LWZ decoder

    \note this code is freely inspired from https://github.com/MichaelDipperstein/lzw GLP 3
 */
struct LWZDecoder {
  static int const e_firstCode=(1<<8);
  static int const e_maxCodeLen=14;
  static int const e_maxCode=(1<<e_maxCodeLen);

  //! constructor
  LWZDecoder(unsigned char const *data, unsigned long len)
    : m_data(data)
    , m_len(len)

    , m_pos(0)
    , m_bit(0)
    , m_dictionary()
  {
    initDictionary();
  }

  //! decode data
  bool decode(std::vector<unsigned char> &output);
protected:
  void initDictionary()
  {
    m_dictionary.resize(2); // 100 and 101
    m_dictionary.reserve(e_maxCode - e_firstCode); // max table 4000
  }

  unsigned getBit() const
  {
    if (m_pos>=m_len)
      throw libmwaw::ParseException();
    unsigned val=(m_data[m_pos]>>(7-m_bit++))&1;
    if (m_bit==8) {
      ++m_pos;
      m_bit=0;
    }
    return val;
  }
  unsigned getCodeWord(unsigned codeLen) const
  {
    unsigned code=0;
    for (unsigned i=0; i<codeLen;) {
      if (m_bit==0 && (codeLen-i)>=8 && m_pos<m_len) {
        code = (code<<8) | unsigned(m_data[m_pos++]);
        i+=8;
        continue;
      }
      code = (code<<1) | getBit();
      ++i;
    }
    return code;
  }

  struct LWZEntry {
    //! constructor
    LWZEntry(unsigned int prefixCode=0, unsigned char suffix=0)
      : m_suffix(suffix)
      , m_prefixCode(prefixCode)
    {
    }
    /** last char in encoded string */
    unsigned char m_suffix;
    /** code for remaining chars in string */
    unsigned int m_prefixCode;
  };

  unsigned char decodeRec(unsigned int code, std::vector<unsigned char> &output)
  {
    unsigned char c;
    unsigned char firstChar;

    if (code >= e_firstCode) {
      if (code-e_firstCode >= m_dictionary.size()) {
        MWAW_DEBUG_MSG(("StudentWritingCParserInternal::LWZDecoder::decodeRec: bad id=%x/%x\n", code, unsigned(m_dictionary.size())));
        throw libmwaw::ParseException();
      }
      /* code word is string + c */
      c = m_dictionary[code - e_firstCode].m_suffix;
      code = m_dictionary[code - e_firstCode].m_prefixCode;

      /* evaluate new code word for remaining string */
      firstChar = decodeRec(code, output);
    }
    else /* code word is just c */
      firstChar = c = (unsigned char)code;

    output.push_back(c);
    return firstChar;
  }

  LWZDecoder(LWZDecoder const &)=delete;
  LWZDecoder &operator=(LWZDecoder const &)=delete;
  unsigned char const *m_data;
  unsigned long m_len;
  mutable unsigned long m_pos, m_bit;

  std::vector<LWZEntry> m_dictionary;
};

bool LWZDecoder::decode(std::vector<unsigned char> &output)
try
{
  unsigned int currentCodeLen = 9;
  unsigned lastCode=0;
  unsigned char c=(unsigned char) 0;
  unsigned endDictCode=0x1ff;
  bool first=true;
  while (true) {
    unsigned code=getCodeWord(currentCodeLen);
    if (code==0x100) {
      initDictionary();
      currentCodeLen = 9;
      endDictCode=0x1ff;

      lastCode=0;
      c=(unsigned char) 0;
      first=true;
      continue;
    }
    if (code==0x101) { // end of code
      if (m_pos+2<m_len) {
        MWAW_DEBUG_MSG(("StudentWritingCParserInternal::LWZDecoder::decode: unexpected end at position %ld/%ld\n", m_pos, m_len));
      }
      break;
    }
    if (code < e_firstCode+m_dictionary.size())
      /* we have a known code.  decode it */
      c = decodeRec(code, output);
    else {
      /***************************************************************
       * We got a code that's not in our dictionary.  This must be due
       * to the string + char + string + char + string exception.
       * Build the decoded string using the last character + the
       * string from the last code.
       ***************************************************************/
      unsigned char tmp = c;
      c = decodeRec(lastCode, output);
      output.push_back(tmp);
    }
    /* if room, add new code to the dictionary */
    if (!first && m_dictionary.size()+e_firstCode < e_maxCode-1) {
      if (lastCode>=e_firstCode+m_dictionary.size()) {
        MWAW_DEBUG_MSG(("StudentWritingCParserInternal::LWZDecoder::decode: oops a loop with %x/%x\n", lastCode, unsigned(m_dictionary.size())));
        break;
      }
      m_dictionary.push_back(LWZEntry(lastCode, c));
      if (m_dictionary.size()+e_firstCode>endDictCode) {
        ++currentCodeLen;
        endDictCode=2*endDictCode+1;
      }
    }

    /* save character and code for use in unknown code word case */
    lastCode = code;
    first=false;
  }
  return true;
}
catch (...)
{
  return false;
}

}

MWAWInputStreamPtr StudentWritingCParser::decode()
{
  MWAWInputStreamPtr bad;
  auto input=getInput();
  if (m_state->m_isUncompressed)
    return input;
  long const begPos=0x1d8;
  if (!input->checkPosition(begPos)) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::decode: the file is too short\n"));
    return bad;
  }
  std::set<long> listBeginPosition;
  listBeginPosition.insert(input->size());
  input->seek(begPos, librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    long pos=input->tell();
    if (!input->checkPosition(pos+18) || input->readLong(4)!=0x1a46461a) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: oops code break at position %ld\n", pos));
      return bad;
    }
    long nextPos=long(input->readLong(4));
    if (nextPos==0) { // ok last position
      listBeginPosition.insert(pos);
      break;
    }
    if (nextPos<begPos+18 || !input->checkPosition(nextPos) || listBeginPosition.find(nextPos)!=listBeginPosition.end()) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: oops code break at position %ld\n", pos));
      return bad;
    }
    listBeginPosition.insert(pos);
    input->seek(nextPos, librevenge::RVNG_SEEK_SET);
  }

  input->seek(0, librevenge::RVNG_SEEK_SET);
  unsigned long read;
  const unsigned char *data = input->read((unsigned long)(begPos), read);
  if (!data || read != (unsigned long)(begPos)) {
    MWAW_DEBUG_MSG(("StudentWritingCParser::decode: can not retrieve the begin data\n"));
    return bad;
  }

  auto stream=std::make_shared<MWAWStringStream>(data, unsigned(begPos));
  for (auto posIt=listBeginPosition.begin(); posIt!=listBeginPosition.end();) {
    long first=*(posIt++);
    if (posIt==listBeginPosition.end()) break;
    long end=*posIt;
    if (first+18>end) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: oops the zone at position %ld seems too short\n", first));
      return bad;
    }
    input->seek(first+8, librevenge::RVNG_SEEK_SET);
    long dataSize=long(input->readLong(4));
    if (dataSize<0 || dataSize>=10000000) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: oops can not read the data size of the zone at position %ld\n", first));
      return bad;
    }

    input->seek(-4, librevenge::RVNG_SEEK_CUR);
    data=input->read(10, read);
    if (!data || read!=10) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: can not retrieve zone's header at position %ld\n", first));
      return bad;
    }
    stream->append(data, 10);

    if (dataSize==0)
      continue;

    data=input->read(size_t(end-18-first), read);
    if (!data || read!=(unsigned long)(end-18-first)) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: can not retrieve zone %ld-%ld\n", first, end));
      continue;
    }
    StudentWritingCParserInternal::LWZDecoder decoder(data, read);
    std::vector<unsigned char> output;
    output.reserve(size_t(dataSize));
    decoder.decode(output);
    if (output.size()!=size_t(dataSize)) {
      MWAW_DEBUG_MSG(("StudentWritingCParser::decode: unexpected output size %lx-%lx\n", long(output.size()), long(dataSize)));
      continue;
    }
    stream->append(output.data(), unsigned(output.size()));
  }

  MWAWInputStreamPtr res(new MWAWInputStream(stream, false));
  res->seek(0, librevenge::RVNG_SEEK_SET);
  return res;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
