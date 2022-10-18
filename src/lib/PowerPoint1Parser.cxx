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
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWPresentationListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWSubDocument.hxx"

#include "PowerPoint1Parser.hxx"

/** Internal: the structures of a PowerPoint1Parser */
namespace PowerPoint1ParserInternal
{
//! Internal: a ruler
struct Ruler {
  //! constructor
  Ruler()
    : m_tabs()
  {
  }
  //! the outline
  struct Outline {
    //! constructor
    Outline()
    {
      for (auto &margin : m_margins) margin=0;
      for (auto &interline : m_interlines) interline=0;
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Outline const &outline)
    {
      for (int i=0; i<2; ++i) {
        if (outline.m_margins[i]==0) continue;
        o << (i==0 ? "first[margin]" : "left[margin]") << "=" << outline.m_margins[i] << ",";
      }
      for (int i=0; i<2; ++i) {
        if (outline.m_interlines[i]==100) continue;
        o << (i==0 ? "space[interline]" : "space[paragraph]") << "=" << outline.m_interlines[i] << "%,";
      }
      return o;
    }
    //! the first margin and left margin
    int m_margins[2];
    //! the interline and paragraph spacing
    int m_interlines[2];
  };
  //! the tabs
  std::vector<MWAWTabStop> m_tabs;
  //! the outline
  Outline m_outlines[5];
};
//! a scheme of a PowerPoint1Parser
struct Scheme {
  //! the color: back, foreground, accents
  MWAWColor m_colors[8];
};
//! Internal: a text zone of a PowerPoint1Parser
struct TextZone {
  //! constructor
  TextZone()
    : m_lineList()
    , m_schemeId(-1)
  {
  }
  //! small structure used to store a line of text and its format
  struct Line {
    //! constructor
    Line()
      : m_text()
      , m_format()
      , m_ruler()
      , m_justify(MWAWParagraph::JustificationLeft)
      , m_outlineLevel(0)
    {
    }
    //! the text entry
    MWAWEntry m_text;
    //! the format entry
    MWAWEntry m_format;
    //! the ruler entry (windows v2)
    MWAWEntry m_ruler;
    //! the justification
    MWAWParagraph::Justification m_justify;
    //! the outline level
    int m_outlineLevel;
  };
  //! return true if the zone has no text
  bool empty() const
  {
    for (auto const &line : m_lineList) {
      if (line.m_text.valid()) return false;
    }
    return true;
  }
  //! the line list
  std::vector<Line> m_lineList;
  //! the scheme id (if v2)
  mutable int m_schemeId;
};
//! Internal: a frame of a PowerPoint1Parser
struct Frame {
  //! constructor
  Frame()
    : m_type(-1)
    , m_dimension()
    , m_cornerSize(0)
    , m_style()
    , m_rulerId(-1)
    , m_pictureId(-1)
    , m_textId(-1,-1)
  {
  }
  //! the type: 0:line, 1:rect, 2: textbox, ...
  int m_type;
  //! the dimension
  MWAWBox2i m_dimension;
  //! the corner width
  int m_cornerSize;
  //! the style
  MWAWGraphicStyle m_style;
  //! the paragraph id
  int m_rulerId;
  //! the picture id
  int m_pictureId;
  //! the text sub id: first and last id
  MWAWVec2i m_textId;
};
//! Internal: a slide of a PowerPoint1Parser
struct Slide {
  //! constructor
  Slide()
    : m_useMasterPage(true)
    , m_schemeId(-1)
  {
  }
  //! the textzone: main's and note's zone
  TextZone m_textZones[2];
  //! the list of frames: main's and note's list of frame
  std::vector<Frame> m_framesList[2];
  //! a flag to know if we need to use the master page
  bool m_useMasterPage;
  //! the scheme id
  int m_schemeId;
};

////////////////////////////////////////
//! Internal: the state of a PowerPoint1Parser
struct State {
  //! constructor
  State()
    : m_isMacFile(true)
    , m_unit(1)
    , m_zoneListBegin(0)
    , m_zonesList()
    , m_origin(0,0)
    , m_rulersList()
    , m_idToSlideMap()
    , m_idToSchemeMap()
    , m_idToUserColorMap()
    , m_picturesIdList()
    , m_schemesIdList()
    , m_badEntry()
  {
    for (auto &slideId : m_slideIds) slideId=-1;
    for (auto &printInfoId : m_printInfoIds) printInfoId=-1;
    for (auto &zoneId : m_zoneIds) zoneId=-1;
  }
  //! try to return a zone
  MWAWEntry const &getZoneEntry(int id) const
  {
    if (id==-1) return m_badEntry;
    if (id<0||size_t(id)>=m_zonesList.size()) {
      MWAW_DEBUG_MSG(("PowerPoint1ParserInternal::State::getZone: can find entry with id=%d\n", id));
      return m_badEntry;
    }
    return m_zonesList[size_t(id)];
  }
  //! try to return a pattern
  bool getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const;
  //! flag to know if the file is a mac file or a pc file
  bool m_isMacFile;
  //! the data unit: 1 if the file is a mac file, 1/8 if the file is a windows file
  float m_unit;
  //! the begin position of the list of zones
  long m_zoneListBegin;
  //! the list of zone entries
  std::vector<MWAWEntry> m_zonesList;
  //! the origin
  MWAWVec2i m_origin;
  //! the ruler
  std::vector<Ruler> m_rulersList;
  //! a map between zoneId and slide
  std::map<int,Slide> m_idToSlideMap;
  //! a map between schemeId and scheme
  std::map<int,Scheme> m_idToSchemeMap;
  //! a map between colorId and user color map
  std::map<int,MWAWColor> m_idToUserColorMap;
  //! the list of slides ids: 0 (master, slide 1, slide 2, ...), 1 (handout slide)
  std::vector<int> m_slidesIdList[2];
  //! the list of pictures id
  std::vector<int> m_picturesIdList;
  //! the list of scheme id
  std::vector<int> m_schemesIdList;
  //! the slide id
  int m_slideIds[2];
  //! the printInfo id
  int m_printInfoIds[2];
  //! the sequential zones id: picture list, ...
  int m_zoneIds[10];
  //! an entry used by getZoneEntry if it does not find the zone
  MWAWEntry m_badEntry;
};

bool State::getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const
{
  // normally between 1 and 22 but find a pattern resource with 39 patterns
  if (id<=0 || id>39) {
    MWAW_DEBUG_MSG(("PowerPoint1ParserInternal::State::getPattern: unknown id=%d\n", id));
    return false;
  }
  static uint16_t const values[] = {
    0xffff, 0xffff, 0xffff, 0xffff, 0x0000, 0x0000, 0x0000, 0x0000,
    0xddff, 0x77ff, 0xddff, 0x77ff, 0x8000, 0x0800, 0x8000, 0x0800,
    0xdd77, 0xdd77, 0xdd77, 0xdd77, 0x8800, 0x2200, 0x8800, 0x2200,
    0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x8822, 0x8822, 0x8822, 0x8822,
    0x8844, 0x2211, 0x8844, 0x2211, 0x1122, 0x4488, 0x1122, 0x4488,
    0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xff00, 0xff00, 0xff00, 0xff00,
    0x81c0, 0x6030, 0x180c, 0x0603, 0x8103, 0x060c, 0x1830, 0x60c0,
    0x8888, 0x8888, 0x8888, 0x8888, 0xff00, 0x0000, 0xff00, 0x0000,
    0xb130, 0x031b, 0xd8c0, 0x0c8d, 0x8010, 0x0220, 0x0108, 0x4004,
    0xff80, 0x8080, 0x8080, 0x8080, 0xff88, 0x8888, 0xff88, 0x8888,
    0xff80, 0x8080, 0xff08, 0x0808, 0xeedd, 0xbb77, 0xeedd, 0xbb77,
    0x8040, 0x2000, 0x0204, 0x0800, 0x8000, 0x0000, 0x0000, 0x0000,
    0x8244, 0x3944, 0x8201, 0x0101, 0xf874, 0x2247, 0x8f17, 0x2271,
    0x55a0, 0x4040, 0x550a, 0x0404, 0x2050, 0x8888, 0x8888, 0x0502,
    0xbf00, 0xbfbf, 0xb0b0, 0xb0b0, 0x0102, 0x0408, 0x1020, 0x4080,
    0xaa00, 0x8000, 0x8800, 0x8000, 0x081c, 0x22c1, 0x8001, 0x0204,
    0x8814, 0x2241, 0x8800, 0xaa00, 0x40a0, 0x0000, 0x040a, 0x0000,
    0x0384, 0x4830, 0x0c02, 0x0101, 0x8080, 0x413e, 0x0808, 0x14e3,
    0x1020, 0x54aa, 0xff02, 0x0408, 0x7789, 0x8f8f, 0x7798, 0xf8f8,
    0x0008, 0x142a, 0x552a, 0x1408
  };
  pattern.m_dim=MWAWVec2i(8,8);
  uint16_t const *ptr=&values[4*(id-1)];
  pattern.m_data.resize(8);
  for (size_t i=0; i < 4; ++i, ++ptr) {
    pattern.m_data[2*i]=static_cast<unsigned char>((*ptr)>>8);
    pattern.m_data[2*i+1]=static_cast<unsigned char>((*ptr)&0xff);
  }
  return true;
}

////////////////////////////////////////
//! Internal: the subdocument of a PowerPointParser
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor for text
  SubDocument(PowerPoint1Parser &pars, MWAWInputStreamPtr const &input, TextZone const *textZone, MWAWVec2i const &tId, int rulerId)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_slide(nullptr)
    , m_textZone(textZone)
    , m_id(tId)
    , m_rulerId(rulerId)
  {
  }
  //! constructor for slide note
  SubDocument(PowerPoint1Parser &pars, MWAWInputStreamPtr const &input, Slide const *slide)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_slide(slide)
    , m_textZone(nullptr)
    , m_id()
    , m_rulerId(-1)
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
    if (m_slide != sDoc->m_slide) return true;
    if (m_textZone != sDoc->m_textZone) return true;
    if (m_id != sDoc->m_id) return true;
    if (m_rulerId != sDoc->m_rulerId) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the slide
  Slide const *m_slide;
  //! the text zone
  TextZone const *m_textZone;
  //! the text id in text zone
  MWAWVec2i m_id;
  //! the ruler id
  int m_rulerId;

private:
  SubDocument(SubDocument const &) = delete;
  SubDocument &operator=(SubDocument const &) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("PowerPoint1ParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<PowerPoint1Parser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("PowerPoint1ParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  if (m_slide) {
    parser->sendSlideNote(*m_slide);
    return;
  }
  if (!m_textZone) {
    MWAW_DEBUG_MSG(("PowerPoint1ParserInternal::SubDocument::parse: no text zone\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendText(*m_textZone, m_id, m_rulerId);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint1Parser::PowerPoint1Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWPresentationParser(input, rsrcParser, header)
  , m_state(new PowerPoint1ParserInternal::State)
{
  setAsciiName("main-1");
}

PowerPoint1Parser::~PowerPoint1Parser()
{
}

bool PowerPoint1Parser::getColor(int colorId, int schemeId, MWAWColor &color) const
{
  // if scheme is defined, we must use it for 0<=colorId<8
  if (schemeId>=0 && colorId>=0 && colorId<8 && m_state->m_idToSchemeMap.find(schemeId)!=m_state->m_idToSchemeMap.end()) {
    color=m_state->m_idToSchemeMap.find(schemeId)->second.m_colors[colorId];
    return true;
  }
  if (m_state->m_idToUserColorMap.find(colorId)!=m_state->m_idToUserColorMap.end()) {
    color=m_state->m_idToUserColorMap.find(colorId)->second;
    return true;
  }
  if (schemeId!=0) { // seems to happens in the master slide
    MWAW_DEBUG_MSG(("PowerPoint1Parser::getColor: can not find color=%d in scheme=%d\n", colorId, schemeId));
  }
  return false;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void PowerPoint1Parser::parse(librevenge::RVNGPresentationInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendSlides();
    }

#ifdef DEBUG
    checkForUnparsedZones();
#endif
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetPresentationListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void PowerPoint1Parser::createDocument(librevenge::RVNGPresentationInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getPresentationListener()) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  std::vector<MWAWPageSpan> pageList;
  for (size_t i=1; i<m_state->m_slidesIdList[0].size(); ++i) {
    MWAWPageSpan ps(getPageSpan());
    int sId=m_state->m_slidesIdList[0][i];
    if (m_state->m_idToSlideMap.find(sId)!=m_state->m_idToSlideMap.end()) {
      auto const &slide=m_state->m_idToSlideMap.find(sId)->second;
      if (slide.m_useMasterPage)
        ps.setMasterPageName(librevenge::RVNGString("Master"));
      MWAWColor backColor;
      if (slide.m_schemeId>=0 && getColor(0, slide.m_schemeId, backColor))
        ps.setBackgroundColor(backColor);
    }
    pageList.push_back(ps);
  }

  //
  MWAWPresentationListenerPtr listen(new MWAWPresentationListener(*getParserState(), pageList, documentInterface));
  setPresentationListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

// create the different zones
bool PowerPoint1Parser::createZones()
{
  MWAWInputStreamPtr input=getInput();
  if (!input) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::createZones: can not find the main input\n"));
    return false;
  }
  int docInfo;
  if (!readListZones(docInfo)) return false;
  size_t numZones=m_state->m_zonesList.size();
  if (docInfo<0 || docInfo>=int(numZones) || !readDocInfo(m_state->m_zonesList[size_t(docInfo)])) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::createZones: can not find the document info zone\n"));
    return false;
  }
  int const vers=version();
  bool const isMacFile=m_state->m_isMacFile;
  int numStyles=vers<=1 ? 4 : m_state->m_isMacFile ? 6 : 8;
  if (isMacFile) {
    for (int i=0; i<numStyles; ++i) {
      MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_zoneIds[i]);
      if (!entry.valid() || entry.isParsed()) continue;
      if (i==0 || i==3)
        readZoneIdList(entry, i);
      else if (i==1)
        readRulers(entry);
      else if (i==2)
        readFonts(entry);
      else if (i==4)
        readColorZone(entry);
      else if (i==5)
        readZone2(entry);
    }
  }
  else {
    for (int i=0; i<numStyles; ++i) {
      MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_zoneIds[i]);
      if (!entry.valid() || entry.isParsed()) continue;
      if (i<3) // list of 0: picture, 1: rulers, 2: scheme
        readZoneIdList2(entry, i);
      else if (i==3)
        readColorZone(entry);
      else if (i==4)
        readZone2(entry);
      // 5: never seens
      else if (i==6)
        readFonts(entry);
      else if (i==7)
        readFontNames(entry);
    }
  }
  readSchemes();
  for (int i=0; i<2; ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_slideIds[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    readSlide(entry, m_state->m_slidesIdList[i]);
  }
  for (int i=0; i<2; ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_printInfoIds[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    if (m_state->m_isMacFile && i==1)
      readPrintInfo(entry);
    else {
      entry.setParsed(true);
      libmwaw::DebugStream f;
      f << "Entries(PrintInfo" << i << ")[Z" << entry.id() << "]:";
      ascii().addPos(entry.begin());
      ascii().addNote(f.str().c_str());
      ascii().addPos(entry.end());
      ascii().addNote("_");
    }
  }
  for (int i=0; i<10; ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_zoneIds[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("PowerPoint1Parser::createZones: find unknown Zone%d\n", i));
    }
    entry.setParsed(true);
    libmwaw::DebugStream f;
    f << "Entries(Zone" << i << ")[Z" << entry.id() << "]:";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }
  return !m_state->m_slidesIdList[0].empty();
}

bool PowerPoint1Parser::readListZones(int &docInfoId)
{
  docInfoId=-1;
  MWAWInputStreamPtr input=getInput();
  libmwaw::DebugStream f;
  f << "Entries(ListZones):";
  // v3: N in 4, then 16+8*N (potential extra data)
  long pos=input->tell();
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (!input->checkPosition(m_state->m_zoneListBegin+N*8)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readListZones: the number of zones seems bad\n"));
    f << "###zone";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  auto val=int(input->readULong(2)); // always 4
  if (val!=4) f << "f0=" << val << ",";
  auto endPos=long(input->readULong(4));
  if (!input->checkPosition(endPos) || input->checkPosition(endPos+1)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readListZones: the endPos seems bad\n"));
    f << "###endPos=" << std::hex << endPos << std::dec << ",";
  }
  val=int(input->readULong(2)); // find a|10
  if (val) f << "f1=" << val << ",";
  docInfoId=int(input->readULong(2));
  if (docInfoId) f << "docInfo=Z" << docInfoId << ",";
  if (input->tell()!=m_state->m_zoneListBegin)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(m_state->m_zoneListBegin, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "ListZones:zones=[";
  m_state->m_zonesList.resize(size_t(N));
  std::set<long> posList;
  for (int i=0; i<N; ++i) {
    unsigned long length=input->readULong(4);
    auto begin=long(input->readULong(4));
    if (length&0x80000000) {
      f << "*";
      length&=0x7FFFFFFF;
    }
    if (length==0) {
      f << "_,";
      continue;
    }
    if (begin+long(length)<=begin || !input->checkPosition(begin+long(length))) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readListZones: a zone seems bad\n"));
      f << std::hex << begin << ":" << begin+long(length) << std::dec << "###,";
      continue;
    }
    MWAWEntry &zone=m_state->m_zonesList[size_t(i)];
    zone.setBegin(begin);
    zone.setLength(long(length));
    zone.setId(i);
    posList.insert(begin);
    posList.insert(zone.end());
    f << std::hex << begin << ":" << begin+long(length) << std::dec << ",";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // check that the zones do not overlap
  for (size_t i=0; i<m_state->m_zonesList.size(); ++i) {
    MWAWEntry &zone=m_state->m_zonesList[size_t(i)];
    if (!zone.valid()) continue;
    auto it=posList.find(zone.begin());
    bool ok=it!=posList.end();
    if (ok) {
      if (++it==posList.end() || *it!=zone.end())
        ok=false;
    }
    if (ok) continue;
    MWAW_DEBUG_MSG(("PowerPoint3Parser::readListZones: the zone %d overlaps with other zones\n", int(i)));
    m_state->m_zonesList[size_t(i)]=MWAWEntry();
  }
  ascii().addPos(input->tell());
  ascii().addNote("_");
  return true;
}

void PowerPoint1Parser::sendSlides()
{
  MWAWPresentationListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendSlides: can not find the listener\n"));
    return;
  }
  if (m_state->m_slidesIdList[0].empty())
    return;
  // first send the master page
  MWAWPageSpan ps(getPageSpan());
  ps.setMasterPageName(librevenge::RVNGString("Master"));
  if (!listener->openMasterPage(ps)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendSlides: can not create the master page\n"));
  }
  else {
    int id=m_state->m_slidesIdList[0][0];
    if (m_state->m_idToSlideMap.find(id)!=m_state->m_idToSlideMap.end())
      sendSlide(m_state->m_idToSlideMap.find(id)->second, true);
    listener->closeMasterPage();
  }

  for (size_t i=1; i<m_state->m_slidesIdList[0].size(); ++i) {
    if (i>1)
      listener->insertBreak(MWAWListener::PageBreak);
    int id=m_state->m_slidesIdList[0][i];
    if (m_state->m_idToSlideMap.find(id)==m_state->m_idToSlideMap.end())
      continue;
    sendSlide(m_state->m_idToSlideMap.find(id)->second, false);
  }
}

void PowerPoint1Parser::checkForUnparsedZones()
{
  for (auto id : m_state->m_picturesIdList) {
    MWAWEntry const &entry=m_state->getZoneEntry(id);
    if (!entry.valid() || entry.isParsed()) continue;
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::checkForUnparsedZones: find some unparsed picture\n"));
      first=false;
    }
    MWAWEmbeddedObject picture;
    readPicture(entry, picture);
  }
  // check if there remains some unparsed zone
  for (auto const &entry : m_state->m_zonesList) {
    if (!entry.valid() || entry.isParsed()) continue;
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("PowerPoint1Parser::checkForUnparsedZones: find some unknown zone\n"));
    }
    libmwaw::DebugStream f;
    f << "Entries(UnknZone)[Z" << entry.id() << "]:";
    ascii().addPos(entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
  }
}

////////////////////////////////////////////////////////////
// try to read the different zones
////////////////////////////////////////////////////////////
bool PowerPoint1Parser::readFramesList(MWAWEntry const &entry, std::vector<PowerPoint1ParserInternal::Frame> &frameList, int schemeId)
{
  MWAWInputStreamPtr input=getInput();
  int const vers=version();
  int const isMacFile=m_state->m_isMacFile;
  int dataSz=isMacFile ? 28 : 32;
  if (!entry.valid() || (entry.length()%dataSz)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: the entry seems bad\n"));
    return false;
  }
  if (!isMacFile) dataSz=30;
  entry.setParsed(true);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Frames)[Z" << entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  auto N=size_t(entry.length()/dataSz);
  frameList.resize(N);
  int tId=0;
  MWAWColor colors[4]= {MWAWColor::black(),MWAWColor::white(),MWAWColor::black(),MWAWColor::black()}; // frame, fill, shadow, pat2
  for (size_t fr=0; fr<N; ++fr) {
    PowerPoint1ParserInternal::Frame &frame=frameList[fr];
    pos=input->tell();
    f.str("");
    f << "Frames[F" << fr << "]:";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    if (!isMacFile) {
      std::swap(dim[0],dim[1]);
      std::swap(dim[2],dim[3]);
    }
    frame.m_dimension=MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
    f << "dim=" << frame.m_dimension << ",";
    frame.m_type=int(input->readULong(1));
    switch (frame.m_type) {
    case 0:
      f << "line,";
      break;
    case 1: // rect, roundrect, oval, this depends on corner width
      f << "rect,";
      break;
    case 2:
      if (isMacFile)
        f << "textbox,";
      else {
        frame.m_textId=MWAWVec2i(tId,tId);
        f << "textbox=T" << tId++ << ",";
      }
      break;
    case 3:
      frame.m_textId=MWAWVec2i(tId,tId);
      f << "textbox[small]=T" << tId++ << ",";
      break;
    default:
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unknown frame type\n"));
      f << "##frame.m_type=" << frame.m_type << ",";
    }
    int val;
    int flags[5];
    if (vers<=1) {
      for (int i=0; i<5; ++i) {
        val=flags[i]=int(input->readULong(1));
        if (!val) continue;
        char const *wh[]= {"opaque", "frame", "filled", "shadowed", "sized to text"};
        if (val!=1) { // v2 can have other value
          static bool first=true;
          if (first) {
            MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find some unexpected draw value\n"));
            first=false;
          }
          f << "#" << wh[i] << "=" << val << ",";
        }
        else
          f << wh[i] << ",";
      }
    }
    else {
      if (isMacFile) {
        val=int(input->readULong(1));
        for (int i=0, bit=1; i<5; ++i, bit<<=1) {
          flags[i]=(val&bit);
          if (!flags[i]) continue;
          char const *wh[]= {"opaque", "frame", "filled", "shadowed", "sized to text"};
          f << wh[i] << ",";
        }
        if (val&0xE0) {
          MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unexpected flags\n"));
          f << "##fl=" << (val>>5) << ",";
        }
      }
      else {
        val=int(input->readULong(1));
        if (val) f << "fl0=" << val << ",";
        val=int(input->readULong(1));
        for (int i=0, bit=1; i<5; ++i, bit<<=1) {
          int const corresp[]= {2,1,3,0,4};
          flags[corresp[i]]=(val&bit);
          if (!flags[corresp[i]]) continue;
          char const *wh[]= {"filled", "frame", "shadowed", "opaque", "sized to text"};
          f << wh[i] << ",";
        }
        if (val&0xE0) {
          MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unexpected flags\n"));
          f << "##fl=" << (val>>5) << ",";
        }
        val=int(input->readULong(1));
        if (val) f << "fl2=" << val << ",";
      }
      for (int i=0; i<4; ++i) { // frame, fill, shadow, pat2
        auto col=int(input->readULong(1));
        int const expected[]= { 1, 4, 2, 0};
        if (schemeId>=0 && !getColor(col, schemeId, colors[i])) f << "##col,";
        if (col!=expected[i]) f << "col" << i << "=" << col << ",";
      }
      if (!isMacFile) std::swap(colors[1],colors[3]);
    }
    MWAWGraphicStyle &style=frame.m_style;
    val=int(input->readULong(1));
    if (val>=1 && val<=10) {
      if (val!=1) {
        char const *wh[]= {"", "w=1", "w=2","w=4", "w=8", "w=16", "w=32",
                           "double", "double1x2", "double2x1", "triple1x2x1"
                          };
        f << "line=[" << wh[val] << "],";
      }
      float const lWidth[]= {0, 1, 2, 4, 8, 12, 16, 3, 4, 4, 6};
      style.m_lineWidth=lWidth[val];
      style.m_lineColor=colors[0];
      MWAWBorder border;
      border.m_width=double(lWidth[val]);
      border.m_color=colors[0];
      switch (val) {
      case 7:
        border.m_type=MWAWBorder::Double;
        break;
      case 8:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        break;
      case 9:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      case 10:
        border.m_type=MWAWBorder::Triple;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      default:
        break;
      }
      style.setBorders(0xF, border);
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unexpected line type\n"));
      f << "##line=" << val << ",";
    }
    if (flags[1]==0 && frame.m_type!=0) {
      style.m_lineWidth=0;
      style.resetBorders();
    }
    val=int(input->readULong(1));
    MWAWGraphicStyle::Pattern pattern;
    if (m_state->getPattern(val, pattern)) {
      pattern.m_colors[0]=colors[1];
      pattern.m_colors[1]=colors[3];
      if (val!=1)
        f << "pat=" << pattern << ",";
      if (flags[2]) { // filled
        MWAWColor color;
        if (pattern.getUniqueColor(color))
          style.setSurfaceColor(color);
        else
          style.setPattern(pattern);
      }
      else if (flags[0]) // opaque
        style.setSurfaceColor(colors[1]);
      if (!flags[2] && !flags[0] && flags[1]) {
        // the pattern is used for border
        MWAWColor color;
        if (pattern.getAverageColor(color))
          style.m_lineColor=color;
      }
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unexpected pattern\n"));
      f << "##pattern=" << val << ",";
      if (flags[0]) style.setSurfaceColor(colors[1]);
    }
    if (flags[3]) {
      style.setShadowColor(colors[2]);
      style.m_shadowOffset=MWAWVec2f(3,3);
    }
    for (int i=0; i<2; ++i) { // often f0=0 and f1=small number
      val=int(input->readULong(1));
      if (!val) continue;
      if (i==0 && frame.m_type==0) {
        if (val==1) {
          style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
          f << "arrow[end],";
        }
        else if (val==2) {
          style.m_arrows[0]=style.m_arrows[1]=MWAWGraphicStyle::Arrow::plain();
          f << "arrow[beg,end],";
        }
        else if (val) {
          MWAW_DEBUG_MSG(("PowerPoint1Parser::readFramesList: find unexpected arrow\n"));
          f << "##arrow=" << val << ",";
        }
      }
      else if (i==0 && frame.m_type==1) {
        frame.m_cornerSize=val;
        f << "size[corner]=" << val << ",";
      }
      else
        f << "f" << i << "=" << val << ",";
    }
    val=int(input->readLong(2));
    if (val!=-1) {
      frame.m_pictureId=val;
      f << "P" << val << ",";
    }
    val=int(input->readULong(2));
    if (val) {
      frame.m_rulerId=val;
      f << "para=R" << val << ",";
    }
    val=int(input->readULong(2));
    if (frame.m_type==2 && isMacFile) {
      frame.m_textId=MWAWVec2i(tId,tId+val-1);
      tId+=val;
      f << "text=T" << frame.m_textId[0] << "<->T" << frame.m_textId[1] << ",";
    }
    val=int(input->readULong(2));
    if (frame.m_type==1 && val && frame.m_pictureId<0) {
      // unsure, find some rectangle with text, in this case this value is set
      frame.m_type=3;
      frame.m_textId=MWAWVec2i(tId,tId);
      f << "textbox[small]=T" << tId++ << ",";
    }
    if (input->tell()!=pos+dataSz)
      ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+dataSz, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    ascii().addPos(input->tell());
    ascii().addNote("Frames:extra");
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint1Parser::readTextZone(MWAWEntry const &entry, PowerPoint1ParserInternal::TextZone &zone)
{
  MWAWInputStreamPtr input=getInput();
  bool isMacFile=m_state->m_isMacFile;
  if (!entry.valid() || entry.length()<(isMacFile ? 6 : 32)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readTextZone: the entry seems bad\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(TextZone)[Z" << entry.id() << "]:";
  int val;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  long const endPos=entry.end();
  int n=0;
  while (input->tell()+6<=endPos) {
    PowerPoint1ParserInternal::TextZone::Line line;
    pos=input->tell();
    if (!isMacFile && pos+32>endPos)
      break;
    f.str("");
    f << "TextZone-T" << ++n << ":";
    if (isMacFile) {
      val=int(input->readLong(1));
      switch (val) {
      case 0: // left
        break;
      case 1:
        line.m_justify=MWAWParagraph::JustificationCenter;
        f << "center,";
        break;
      case 2:
        line.m_justify=MWAWParagraph::JustificationRight;
        f << "right,";
        break;
      case 3:
        line.m_justify=MWAWParagraph::JustificationFull;
        f << "justify,";
        break;
      default:
        MWAW_DEBUG_MSG(("PowerPoint1Parser::readTextZone: find unknown justification\n"));
        f << "##justify=" << val << ",";
      }
      line.m_outlineLevel=int(input->readLong(1));
      if (line.m_outlineLevel) f << "outline[levl]=" << line.m_outlineLevel << ",";
    }
    else if (entry.length()>32+16) {
      for (int i=0; i<16; ++i) { // f0=1|8|c|10,f1=d|12,f2=1|3f,f3=0|c,f5=sz|big number,f6=0-3,f7=0-1a,f9=0|1,f13=0-2,f14=0|5
        val=int(input->readLong(2));
        if (val)
          f << "f" << i << "=" << val << ",";
      }
      for (int i=0; i<4; ++i) {
        val=int(input->readULong(2));
        if (val) f << "g" << i << "=" << val << ",";
      }
    }
    else {
      input->seek(pos+32, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      zone.m_lineList.push_back(line);
      break;
    }
    auto sSz=int(input->readULong(2));
    if (input->tell()+sSz+(isMacFile ? (sSz&1)+2:16)>endPos) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      n--;
      break;
    }
    line.m_text.setBegin(input->tell());
    line.m_text.setLength(sSz);
    std::string text;
    for (int i=0; i<sSz; ++i) text+=char(input->readULong(1));
    f << text << ",";
    if (isMacFile && (sSz&1)) input->seek(1, librevenge::RVNG_SEEK_CUR);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << "TextZone-F" << n << ":";
    if (isMacFile) {
      sSz=int(input->readULong(2));
      if ((sSz!=0 && sSz<6) || pos+2+sSz>endPos) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        n--;
        break;
      }
      line.m_format.setBegin(pos+2);
      line.m_format.setLength(sSz);
      input->seek(sSz, librevenge::RVNG_SEEK_CUR);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    else {
      for (int i=0; i<3; ++i) {
        val=int(input->readULong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto nFonts=int(input->readULong(2));
      if (pos+nFonts*14+8>endPos) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        n--;
        break;
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      line.m_format.setBegin(pos+8);
      line.m_format.setLength(nFonts*14);
      input->seek(nFonts*14, librevenge::RVNG_SEEK_CUR);

      pos=input->tell();
      f.str("");
      f << "TextZone-R" << n << ":";
      for (int i=0; i<3; ++i) {
        val=int(input->readULong(2));
        if (val) f << "f" << i << "=" << val << ",";
      }
      auto nRulers=int(input->readULong(2));
      if (pos+nRulers*6>endPos) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        n--;
        break;
      }
      line.m_ruler.setBegin(pos+8);
      line.m_ruler.setLength(nRulers*6);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(nRulers*6, librevenge::RVNG_SEEK_CUR);
    }
    zone.m_lineList.push_back(line);
  }
  if (n==0 && isMacFile) return false;
  entry.setParsed(true);
  ascii().addPos(endPos);
  ascii().addNote("_");
  pos=input->tell();
  if (pos!=endPos) {
    if (!isMacFile && pos<endPos && endPos<pos+32) {
      ascii().addPos(pos);
      ascii().addNote("TextZone-extra");
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readTextZone: find extra data\n"));
      ascii().addPos(pos);
      ascii().addNote("TextZone-###extra");
    }
  }
  return true;
}

bool PowerPoint1Parser::readSlide(MWAWEntry const &entry, std::vector<int> &listIds)
{
  int const isMacFile=m_state->m_isMacFile;

  if (!entry.valid() || entry.length()!=(isMacFile ? 58 : 64)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readSlide: the entry %d seems bad\n", entry.id()));
    return false;
  }
  if (entry.isParsed()) return true;
  entry.setParsed(true);
  listIds.push_back(entry.id());
  PowerPoint1ParserInternal::Slide bugSlide;
  auto *slide=&bugSlide;
  if (m_state->m_idToSlideMap.find(entry.id())==m_state->m_idToSlideMap.end()) {
    m_state->m_idToSlideMap[entry.id()]=PowerPoint1ParserInternal::Slide();
    slide=&m_state->m_idToSlideMap.find(entry.id())->second;
  }
  else {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readSlide: oops, an slide already exists with id=%d\n", entry.id()));
  }
  MWAWInputStreamPtr input=getInput();
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Slide)[Z" << entry.id() << "]:";
  auto numZones=int(m_state->m_zonesList.size());
  long childIds[5]= {-1,-1,-1,-1,-1};
  long id=input->readLong(4);
  if (id>=0 && id<numZones) {
    childIds[0]=id;
    f << "prev[page]=Z" << id << ",";
  }
  else if (id!=-1) // can happen in the last slide
    f << "#prev[page]=" << id << ",";
  int val;
  for (int i=0; i<3; ++i) { //f2=0
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  if (isMacFile) {
    f << "ids=[";
    for (int i=0; i<2; ++i) { // two big num or 0
      val=int(input->readULong(4));
      if (val)
        f << std::hex << val << std::dec << ",";
      else
        f << "_,";
    }
    f << "],";
  }
  val=int(input->readULong(2)); // always 0
  if (val)
    f << "f3=" << val << ",";
  id=int(input->readULong(2));
  if (id>=0 && id<numZones) {
    childIds[1]=id;
    f << "text=Z" << id << ",";
  }
  else if (id!=0xFFFF) // can happen in the last slide
    f << "#text=" << id << ",";
  val=int(input->readLong(2)); // 0
  if (val) f << "f4=" << val << ",";
  if (isMacFile) {
    val=int(input->readULong(2)); // always 0
    if (val)
      f << "f5=" << val << ",";
  }
  id=int(input->readULong(2));
  if (id>=0 && id<numZones) {
    childIds[2]=id;
    f << "frame=Z" << id << ",";
  }
  else if (id!=0xFFFF) // can happen in the last slide
    f << "#frame=" << id << ",";
  val=int(input->readLong(2));
  if (val) f << "num[frames]=" << val << ",";
  val=int(input->readLong(1));
  if (val==0) {
    slide->m_useMasterPage=false;
    f << "no[master],";
  }
  else if (val!=1)
    f << "#use[master]=" << val << ",";
  val=int(input->readLong(1)); // always 0
  if (val) f << "f6=" << val << ",";
  if (!isMacFile) {
    // maybe junk
    val=int(input->readULong(2));
    if (val)
      f << "f7=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readULong(2));
  if (val>=0 && val<int(m_state->m_schemesIdList.size())) {
    slide->m_schemeId=val;
    f << "scheme=S" << val << ",";
  }
  else if (val)
    f << "#scheme=" << val << ",";
  if (isMacFile) {
    // maybe junk
    val=int(input->readULong(2));
    if (val)
      f << "f7=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readULong(2)); // 0 or big number
  if (val) f << "g0=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(2));
  if (val) f << "g1=" << val << ",";
  id=int(input->readULong(2));
  val=isMacFile ? int(input->readLong(2)) : 0; // 0 or junk ?
  if (val==0 && id>0 && id<numZones) {
    childIds[3]=id;
    f << "note[text]=Z" << id << ",";
  }
  else if (val==0 && id!=0 && id!=0xFFFF) { // can be bad
    f << "#note[text]=" << id << ",";
  }
  val=int(input->readULong(2)); // 0
  if (val) f << "g2=" << val << ",";
  id=int(input->readULong(2));
  val=int(input->readLong(2)); // 1|2 or junk
  if (val>=1 && val<32) {
    if (id>0 && id<numZones) {
      childIds[4]=id;
      f << "note[frame]=Z" << id << ",";
      f << "num[note,frame]=" << val << ",";
    }
    else if (id!=0 && id!=0xFFFF) { // can be bad
      f << "#note[frame]=" << id << ",";
    }
  }
  for (int i=0; i<2; ++i) { // g3=1|junk, g4=0|1|junk
    val=int(input->readLong(1));
    if (val!=1) f << "g" << i+3 << "=" << val << ",";
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  // now read the child zone
  for (int i=0; i<5; ++i) {
    long const cId=childIds[i];
    if (cId<0 || cId>=numZones) continue;
    MWAWEntry const &childEntry=m_state->m_zonesList[size_t(cId)];
    if (!childEntry.valid()) continue;
    if (i==0 && childEntry.isParsed()) {
      // we do not want loop here
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readSlide: the entry %d is already parsed, we may loose some part\n", int(cId)));
      continue;
    }
    if (i==0) readSlide(childEntry, listIds);
    else if ((i%2)==1) readTextZone(childEntry, slide->m_textZones[i/2]);
    else readFramesList(childEntry, slide->m_framesList[i/2-1], slide->m_schemeId);
  }
  return true;
}

bool PowerPoint1Parser::readDocInfo(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input=getInput();
  int vers=version();
  int const isMacFile=m_state->m_isMacFile;
  bool ok=entry.valid() && vers==1;
  if (ok && !isMacFile) {
    ok=entry.length()==192;
    vers=2;
    setVersion(vers);
  }
  else if (ok && entry.length()==164) {
    vers=2;
    setVersion(vers);
  }
  else
    ok=ok && entry.length()==160;
  if (!ok) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: the entry %d seems bad\n", entry.id()));
    return false;
  }
  entry.setParsed(true);
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(DocInfo)[Z" << entry.id() << "]:";
  int val;
  auto numZones=int(m_state->m_zonesList.size());
  f << "unkn=[";
  for (int i=0; i<4; ++i) { // list of 0 or big number
    val=int(input->readLong(2));
    if (val) f << val << ",";
    else f << "_,";
  }
  f << "],";
  int numId=isMacFile ? 1 : 2;
  for (int i=0; i<numId; ++i) {
    val=int(input->readLong(2));
    if (val) f << "id" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<2; ++i) { // f0=3|6, f1=0
    val=int(input->readLong(1));
    if (val) f << "f" << i << "=" << val << ",";
  }
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim[screen]=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
  int pages[2];
  for (auto &p : pages) p=int(input->readLong(2));
  f << "num[pages]=" << pages[0] << ",";
  if (pages[0]!=pages[1]) f << "act[page]=" << pages[1] << ",";
  for (int i=0; i<2; ++i) { // id1=0, id2=0|big number
    val=int(input->readULong(!isMacFile ? 2 : 4));
    if (val) f << "id" << i+2 << "=" << std::hex << val << std::dec << ",";
  }
  if (isMacFile) {
    val=int(input->readLong(2)); // 0
    if (val) f << "f4=" << val << ",";
  }
  m_state->m_slideIds[0]=int(input->readULong(2));
  f << "slide[id]=Z" <<  m_state->m_slideIds[0] << ",";
  if (m_state->m_slideIds[0]>=numZones) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: the slideId %d seems bad\n", m_state->m_slideIds[0]));
    f << "###";
    m_state->m_slideIds[0]=-1;
  }
  for (int i=0; i<2; ++i) { // two big number
    val=int(input->readULong(!isMacFile ? 2 : 4));
    if (val) f << "id" << i+4 << "=" << std::hex << val << std::dec << ",";
  }
  for (int &i : dim) i=int(input->readLong(2));
  if (!isMacFile) {
    std::swap(dim[0],dim[1]);
    std::swap(dim[2],dim[3]);
  }
  MWAWBox2i pageBox(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
  f << "dim[page]=" << pageBox << ",";
  for (int &i : dim) i=int(input->readLong(2));
  if (!isMacFile) {
    std::swap(dim[0],dim[1]);
    std::swap(dim[2],dim[3]);
  }
  MWAWBox2i paperBox(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
  paperBox=MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  f << "dim[paper]=" << paperBox << ",";
  m_state->m_origin=-1*paperBox[0];
  MWAWVec2i paperSize = paperBox.size();
  MWAWVec2i pageSize = pageBox.size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: the page dimension seems bad\n"));
  }
  else {
    double const unit=double(m_state->m_unit);
    // checkme, maybe better to define a slide with pageSize and no margins
    getPageSpan().setFormOrientation(MWAWPageSpan::PORTRAIT);
    if (pageBox[0][1]>=paperBox[0][1])
      getPageSpan().setMarginTop(double(pageBox[0][1]-paperBox[0][1])*unit/72.0);
    if (pageBox[1][1]<=paperBox[1][1])
      getPageSpan().setMarginBottom(double(paperBox[1][1]-pageBox[1][1])*unit/72.0);
    if (pageBox[0][0]>=paperBox[0][0])
      getPageSpan().setMarginLeft(double(pageBox[0][0]-paperBox[0][0])*unit/72.0);
    if (pageBox[1][0]<=paperBox[1][0])
      getPageSpan().setMarginRight(double(paperBox[1][0]-pageBox[1][0])*unit/72.0);
    getPageSpan().setFormLength(double(paperSize.y())*unit/72.);
    getPageSpan().setFormWidth(double(paperSize.x())*unit/72.);
  }
  if (isMacFile) {
    val=int(input->readLong(2)); // 0
    if (val) f << "f5=" << val << ",";
  }
  m_state->m_slideIds[1]=int(input->readULong(2));
  f << "slide[handout,id]=Z" <<  m_state->m_slideIds[1] << ",";
  if (m_state->m_slideIds[1]>=numZones) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: the slideIds[1] %d seems bad\n",  m_state->m_slideIds[1]));
    f << "###";
    m_state->m_slideIds[1]=-1;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo-2:";
  val=int(input->readULong(!isMacFile ? 2 : 4));
  if (val) f << "id=" << std::hex << val << std::dec << ",";
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
  val=int(input->readLong(1)); // 0|1
  if (val) f << "f0=" << val << ",";
  val=int(input->readLong(2)); // 0 or big number
  if (val) f << "f1=" << val << ",";
  val=int(input->readULong(1)); // 2..80
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  if (!isMacFile) {
    val=int(input->readLong(2)); // 1|25|26
    if (val) f << "f2=" << val << ",";
  }
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << 3+i << "=" << val << ",";
  }
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim2=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2])) << ",";
  f << "unkn=[";
  for (int i=0; i<3; ++i) { // 3 small int
    val=int(input->readLong(2));
    if (val) f << val << ",";
    else f << "_,";
  }
  f << "],";
  val=int(input->readULong(1)); // 2
  if (val!=2) f << "fl1=" << val << ",";
  val=int(input->readLong(2)); // 1
  if (val!=1) f << "f5=" << val << ",";
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+(!isMacFile ? 66 : 48), librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocInfo-3:";
  f << "zones=[";
  int numSubZone=isMacFile ? 5+vers : 10;
  for (int i=0; i<numSubZone; ++i) {
    // 0: picture zones, 1: picture pos?, 2: some style?,
    long id=input->readLong(!isMacFile ? 2 : 4);
    if (id==0 || id==-1)
      f << "_,";
    else if (id>0 && id<numZones) {
      f << "Z" << id << ",";
      m_state->m_zoneIds[i]=int(id);
    }
    else {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: find odd zone\n"));
      f << "###" << id << ",";
    }
  }
  f << "],";
  for (auto &d: dim) d=int(input->readULong(2));
  f << "page=" << MWAWVec2i(dim[0],dim[1]) << ",";
  f << "dim?=" << MWAWVec2i(dim[3],dim[2]) << ","; // frame, slide dim?
  for (int i=0; i<2; ++i) { // f0=1, f1=0|80
    val=int(input->readULong(2));
    if (val) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<2; ++i) {
    m_state->m_printInfoIds[i]= int(input->readULong(2));
    if (!m_state->m_printInfoIds[i]) continue;
    f << "printInfo[id" << i << "]=Z" << m_state->m_printInfoIds[i] << ",";
    if (m_state->m_printInfoIds[i]>=numZones) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readDocInfo: the printInfoId %d seems bad\n", m_state->m_printInfoIds[i]));
      f << "###";
      m_state->m_printInfoIds[i]=-1;
    }
  }
  if (isMacFile) {
    for (int i=0; i<4; ++i) { // 0
      val=int(input->readULong(2));
      if (val) f << "g" << i << "=" << val << ",";
    }
  }
  else
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  return true;
}

bool PowerPoint1Parser::readPictureDefinition(MWAWEntry const &entry, size_t pId)
{
  if (!entry.valid() || entry.length()<28) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readPictureDefinition: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  ascii().addPos(entry.end());
  ascii().addNote("_");
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Picture)[Z"<< entry.id() << "-" << pId << "]:def,";
  auto val=int(input->readULong(2)); // big number [0-3]XXX
  if (val) f << "id=" << std::hex << val << std::dec << ",";
  auto type=int(input->readULong(2)); // 1-4
  if (type)
    f << "type=" << type << ",";
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  val=int(input->readULong(2));
  if (val!=2) {
    f << "###type2=" << val << ",";
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readPictureDefinition: find unexpected type 2\n"));
  }
  auto child=int(input->readULong(2));
  if (child>=0 && child<int(m_state->m_zonesList.size())) {
    f << "child[id]=Z" << child << ",";
    if (pId>=m_state->m_picturesIdList.size())
      m_state->m_picturesIdList.resize(pId+1,-1);
    m_state->m_picturesIdList[pId]=child;
  }
  else {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readPictureDefinition: find some bad child\n"));
    f << "child[id]=##Z" << child << ",";
  }
  if (type==4) {
    for (int i=0; i<3; ++i) {
      val=int(input->readULong(2));
      child=int(input->readULong(2));
      if (child>=0 && child<int(m_state->m_zonesList.size())) {
        f << "child" << i << "[id]=Z" << child << "[" << val << "],";
        MWAWEntry const &cEntry=m_state->getZoneEntry(child);
        if (!cEntry.valid() || cEntry.isParsed()) continue;
        // find type=10,14(string: Graph),16(probably the graph structure)
        cEntry.setParsed(true);
        libmwaw::DebugStream f2;
        f2 << "Entries(Pict" << val << "):";
        ascii().addPos(cEntry.begin());
        ascii().addNote(f2.str().c_str());
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint1Parser::readPictureDefinition: find some bad child\n"));
        f << "child" << i << "[id]=##Z" << child << "[" << val << "],";
      }
    }
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint1Parser::readPicture(MWAWEntry const &entry, MWAWEmbeddedObject &picture)
{
  if (!entry.valid() || entry.length()<20) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readPicture: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  ascii().skipZone(pos, entry.end()-1);
  librevenge::RVNGBinaryData file;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(entry.length(), file);
  picture.add(file);
#ifdef DEBUG_WITH_FILES
  static int volatile pictName = 0;
  libmwaw::DebugStream f;
  f << "PICT-" << ++pictName << ".pct";
  libmwaw::Debug::dumpFile(file, f.str().c_str());
#endif

  ascii().addPos(entry.end());
  ascii().addNote("_");

  return true;
}

bool PowerPoint1Parser::readZoneIdList(MWAWEntry const &entry, int zId)
{
  if (!entry.valid() || (entry.length()%6) != 0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList: the zone seems bad\n"));
    return false;
  }
  if (zId!=0 && zId!=3) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList: find unexpected zone id=%d\n", zId));
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  std::string const wh(zId==0 ? "PictureList" : zId==3 ? "Scheme" : "UnknownList");
  f << "Entries(" << wh << ")[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  auto N=size_t(entry.length()/6);
  auto const numZones=int(m_state->m_zonesList.size());
  std::vector<int> unknownList;
  std::vector<int> &list=zId==0 ? m_state->m_picturesIdList : zId==3 ? m_state->m_schemesIdList : unknownList;
  list.resize(N, -1);
  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    if (zId==0)
      f << "PictureList-P" << i << ":";
    else if (zId==3)
      f << "Scheme-S" << i << ":";
    else
      f << wh << "-" << i << ":";
    auto type=int(input->readULong(2));
    auto id=int(input->readLong(4));
    if (type==0 || id==-1) {
      f << "_,";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    f << "Z" << id << ":" << type;
    if (id<0 || id>=numZones) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList: the picture id seems bad\n"));
      f << "###";
    }
    else
      list[i]=id;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint1Parser::readZoneIdList2(MWAWEntry const &entry, int zId)
{
  if (!entry.valid() || entry.length()<16 || (entry.length()%4) != 0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList2: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  std::string const wh(zId==0 ? "Picture" : zId==1 ? "Ruler" : zId==2 ? "Scheme" : "UnknownList");
  f << "Entries(" << wh << ")[Z"<< entry.id() << "]:list,";
  auto val=int(input->readULong(2)); // 8001
  if (val!=0x8001) f << "f0=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(2)); // big number
  if (val) f << "id=" << std::hex << val << std::dec << ",";
  auto N=size_t(input->readULong(2));
  f << "N=" << N << ",";
  if (16+4*long(N)>entry.length()) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList2: the N value seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return false;
  }
  for (int i=0; i<5; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x7fff, 0, 2, 0, 0};
    if (val!=expected[i])
      f << "f" << i+2 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  auto const numZones=int(m_state->m_zonesList.size());
  std::vector<int> list;
  list.resize(N, -1);
  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    if (zId==0)
      f << "Picture-P" << i << ":";
    else if (zId==1)
      f << "Ruler-R" << i << ":";
    else if (zId==2)
      f << "Scheme-S" << i << ":";
    else
      f << wh << "-" << i << ":";
    auto type=int(input->readULong(2));
    auto id=int(input->readLong(2));
    if (type==0 || id==-1) {
      f << "_,";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    f << "Z" << id << ":" << type;
    if (id<0 || id>=numZones) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readZoneIdList2: the picture id seems bad\n"));
      f << "###";
    }
    else
      list[i]=id;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    ascii().addPos(input->tell());
    ascii().addNote("UnkList:extra");
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  if (zId==2)
    m_state->m_schemesIdList=list;
  else {
    f.str("");
    f << "Entries(UnknList" << zId << "):";
    for (size_t i=0; i<N; ++i) {
      if (list[i]==-1) continue;
      MWAWEntry const &cEntry=m_state->getZoneEntry(list[i]);
      if (!cEntry.valid() || cEntry.isParsed()) continue;
      if (zId==0)
        readPictureDefinition(cEntry, i);
      else if (zId==1)
        readRuler(cEntry, i);
      else {
        cEntry.setParsed(true);
        ascii().addPos(cEntry.begin());
        ascii().addNote(f.str().c_str());
        ascii().addPos(cEntry.end());
        ascii().addNote("_");
      }
    }
  }
  return true;
}

bool PowerPoint1Parser::readPrintInfo(MWAWEntry const &entry)
{
  if (entry.length() != 0x78) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readPrintInfo: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  // print info
  libmwaw::PrinterInfo info;
  if (!info.read(input)) return false;
  f << "Entries(PrintInfo)[Z"<< entry.id() << "]:" << info;

  // this is the final paper, so let ignore this
  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) return false;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  ascii().addPos(entry.end());
  ascii().addNote("_");

  return true;
}

bool PowerPoint1Parser::readRulers(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%66)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readRulers: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Ruler)[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  auto N=size_t(entry.length()/66);
  m_state->m_rulersList.resize(N);
  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Ruler-R" << i+1 << ":";
    auto &ruler=m_state->m_rulersList[i];
    auto val=int(input->readULong(2)); // 1, 2, 5, 8, 1fff, 2000, 2001,
    if (val) f << "f0=" << std::hex << val << std::dec << ",";
    auto nTabs=int(input->readULong(2));
    if (nTabs>10) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readRulers: the number of tab seems bad\n"));
      f << "###n[tabs]=" << nTabs << ",";
      nTabs=0;
    }
    std::vector<int> tPos;
    for (int j=0; j<nTabs; ++j) tPos.push_back(int(input->readULong(2)));
    input->seek(pos+24, librevenge::RVNG_SEEK_SET);
    val=int(input->readULong(2));
    f << "tabs=[";
    for (int j=0, bit=1; j<nTabs; ++j, bit<<=1) {
      MWAWTabStop tab;
      tab.m_position=double(tPos[size_t(j)])/72.;
      tab.m_alignment=(val&bit) ? MWAWTabStop::CENTER : MWAWTabStop::LEFT;
      ruler.m_tabs.push_back(tab);
      f << tab << ",";
    }
    f << "],";
    f << "levels=[";
    for (auto &outline : ruler.m_outlines) {
      f << "[";
      for (int &margin : outline.m_margins) margin=int(input->readULong(2));
      for (int &interline : outline.m_interlines) interline=10*int(input->readULong(1));
      f << outline << ",";
      f << "fl=" << std::hex << input->readULong(2) << std::dec << ",";
      f << "],";
    }
    f << "],";
    input->seek(pos+66, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint1Parser::readRuler(MWAWEntry const &entry, size_t id)
{
  if (!entry.valid() || (entry.length()<54)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readRuler: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Ruler)[Z"<< entry.id() << "]:R" << id << ",";

  if (m_state->m_rulersList.size()<id+1)
    m_state->m_rulersList.resize(id+1);
  PowerPoint1ParserInternal::Ruler &ruler=m_state->m_rulersList[id];
  f << "levels=[";
  for (auto &outline : ruler.m_outlines) {
    f << "[";
    for (int &margin : outline.m_margins) margin=int(input->readULong(2));
    for (int &interline : outline.m_interlines) interline=int(input->readULong(2));
    f << outline << ",";
    f << "fl=" << std::hex << input->readULong(2) << std::dec << ",";
    f << "],";
  }
  f << "],";
  auto val=int(input->readULong(2)); // 2-3: align?
  if (val!=3) f << "f0=" << val << ",";
  auto nTabs=int(input->readULong(2));
  if (input->tell()+4*nTabs>entry.end()) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readRuler: the number of tab seems bad\n"));
    f << "###n[tabs]=" << nTabs << ",";
    nTabs=0;
  }
  f << "tabs=[";
  for (int j=0; j<nTabs; ++j) {
    MWAWTabStop tab;
    tab.m_position=double(input->readULong(2))/8./72.;
    val=int(input->readULong(2));
    switch (val) {
    case 0:
      tab.m_alignment=MWAWTabStop::DECIMAL;
      break;
    case 1:
      tab.m_alignment=MWAWTabStop::RIGHT;
      break;
    case 2:
      tab.m_alignment=MWAWTabStop::CENTER;
      break;
    case 3:
      tab.m_alignment=MWAWTabStop::LEFT;
      break;
    default:
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readRuler: find unknown alignment\n"));
      f << "##align=" << val << ",";
      break;
    }
    ruler.m_tabs.push_back(tab);
    f << tab << ",";
  }
  f << "],";
  if (input->tell()!=entry.end())
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}


bool PowerPoint1Parser::readColors(MWAWEntry const &entry)
{
  if (!entry.valid() || (entry.length()%8)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readColors: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  bool const isMacFile=m_state->m_isMacFile;
  libmwaw::DebugStream f;
  f << "Entries(Color)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<3; ++i) { // always 0
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if ((isMacFile && 8+(N+1)*8 != int(entry.length())) || (!isMacFile && 8+(N+1)*8 > int(entry.length()))) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readColors: the N value seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  // cmyk picker 32-33-34-35
  for (int i=0; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Color-C" << i << ":";
    val=int(input->readLong(2));
    if (val) {
      unsigned char col[3];
      for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
      MWAWColor color(col[0],col[1],col[2]);
      m_state->m_idToUserColorMap[i]=color;
      f << color << ",";
    }
    else
      f << "_,";
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=entry.end()) {
    ascii().addPos(input->tell());
    ascii().addNote("Color:extra");
  }
  return true;
}

bool PowerPoint1Parser::readColorZone(MWAWEntry const &entry)
{
  bool const isMacFile=m_state->m_isMacFile;
  if (!entry.valid() || (entry.length()<(isMacFile ? 48 : 43))) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readColorZone: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Color)[Z"<< entry.id() << "]:menu,";
  auto N=int(input->readULong(2));
  f << "N=" << N << ",";
  if ((isMacFile && 48+2*N!=int(entry.length())) || (!isMacFile && 43+2*N>int(entry.length()))) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readColorZone: the N value seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    ascii().addPos(entry.end());
    ascii().addNote("_");
    return true;
  }
  auto val=int(input->readLong(2)); // always a
  if (val!=10) f << "f0=" << val << ",";
  auto id=int(input->readLong(isMacFile ? 4 : 2));
  auto const numZones=int(m_state->m_zonesList.size());
  if (id>0 && id<numZones)
    f << "colors=Z" << id << ",";
  else {
    if (id!=0 && id!=-1) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readColorZone: the child zone seems bad\n"));
      f << "###colors=Z" << id << ",";
    }
    id = -1;
  }
  ascii().addDelimiter(input->tell(),'|');
  // unsure probably some dimension here
  input->seek(pos+(isMacFile ? 46 : 43), librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  f << "num[used]=[";
  for (int i=0; i<N; ++i) { // 0|1|2
    val=int(input->readLong(2));
    if (val) f << val << ",";
    else f << "_,";
  }
  f << "],";
  if (isMacFile) {
    val=int(input->readULong(2));
    if (val) f << "g0=" << std::hex << val << std::dec << ",";
  }
  if (input->tell()!=entry.end())
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");

  MWAWEntry const &cEntry=m_state->getZoneEntry(id);
  if (cEntry.valid() && !cEntry.isParsed()) readColors(cEntry);
  return true;
}

bool PowerPoint1Parser::readFonts(MWAWEntry const &entry)
{
  static bool isMacFile=m_state->m_isMacFile;
  if (!entry.valid() || entry.length()<(isMacFile ? 6 : 13) || (isMacFile && entry.length()%6)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readFonts: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(FontDef)[Z"<< entry.id() << "]:";
  auto N=size_t(entry.length()/6);
  if (!isMacFile) {
    N=size_t(input->readULong(2)); // always 6?
    if (long(6+7*N)>entry.length()) {
      MWAW_DEBUG_MSG(("PowerPoint1Parser::readFonts: the zone seems bad\n"));
      return false;
    }
    f << "N=" << N << ",";
    f << "id=" << std::hex << input->readULong(4) << std::dec << ","; // big number
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "FontDef-F" << i << ":";
    MWAWFont font;
    font.setId(int(input->readULong(2)));
    font.setSize(float(input->readULong(2)));
    auto flag = int(input->readULong(isMacFile ? 1 : 2));
    uint32_t flags=0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0xE0) f << "#flag=" << (flag>>5) << ",";
    font.setFlags(flags);
    f << font.getDebugString(getParserState()->m_fontConverter);
    auto val=int(input->readULong(1)); // 1-4: another flag or maybe the font's color ?
    if (val) f << "fl=" << std::hex << val << std::dec << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  if (input->tell()!=entry.end()) {
    ascii().addPos(input->tell());
    ascii().addNote("FontDef:extra");
  }
  return true;
}

bool PowerPoint1Parser::readFontNames(MWAWEntry const &entry)
{
  if (!entry.valid() || entry.length()<16) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readFontNames: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(FontName)[Z"<< entry.id() << "]:";
  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x8001,0x25ba};
    if (val!=expected[i])
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  auto N=size_t(input->readULong(2)); // always 6?
  if (long(16+52*N)>entry.length()) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readFontNames: the zone seems bad\n"));
    return false;
  }
  f << "N=" << N << ",";
  for (int i=0; i<5; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x7fff,0,0x32,0,0};
    if (val!=expected[i])
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (size_t i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "FontName-FN" << i << ":";
    val=int(input->readULong(2));
    if (!val) {
      f << "_,";
      input->seek(pos+52,librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    f << "id=" << val << ",";
    for (int j=0; j<9; ++j) { // f4=0|190, f8=0|22|52
      val=int(input->readULong(2));
      if (val)
        f << "f" << j << "=" << std::hex << val << std::dec << ",";
    }
    std::string name; // Helv, Tms Rmn, ZapfDingbats
    for (int c=0; c<32; ++c) {
      auto ch=char(input->readULong(1));
      if (!ch) break;
      name+=ch;
    }
    if (!name.empty()) {
      f << name << ",";
      /* FIXME: by default, we force the family to be CP1252,
         but we may want to use the file/font encoding */
      getFontConverter()->setCorrespondance(int(i), name, (name=="Monotype Sorts" || name=="Wingdings") ? "" : "CP1252");
    }
    input->seek(pos+52,librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  ascii().addPos(entry.end());
  ascii().addNote("_");
  if (input->tell()!=entry.end()) {
    ascii().addPos(input->tell());
    ascii().addNote("FontName:extra");
  }
  return true;
}

bool PowerPoint1Parser::readSchemes()
{
  for (size_t i=0; i<m_state->m_schemesIdList.size(); ++i) {
    MWAWEntry const &entry=m_state->getZoneEntry(m_state->m_schemesIdList[i]);
    if (!entry.valid() || entry.isParsed()) continue;
    readScheme(entry, int(i));
  }
  return true;
}
bool PowerPoint1Parser::readScheme(MWAWEntry const &entry, int id)
{
  bool const isMacFile=m_state->m_isMacFile;
  if (!entry.valid() || (isMacFile && entry.length()!=86) || (!isMacFile && entry.length() < 96)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readScheme: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  PowerPoint1ParserInternal::Scheme scheme;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Scheme)[Z"<< entry.id() << "]:S" << id << ",";
  int val;
  if (isMacFile) {
    for (int i=0; i<10; ++i) { // f8=0|80
      val=int(input->readLong(2));
      int const expected[]= {16,0,0,100,100,100, 0x101, 0, 0, 0};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
  }
  else {
    for (int i=0; i<12; ++i) { // f9=0|80
      val=int(input->readLong(i==3 ? 1 : 2));
      int const expected[]= {0,16,0,0, 100,100,100, 1, 1, 0, 0, 0};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
  }
  val=int(input->readLong(2));
  if (val!=7) f << "max[color]=##" << val << ",";
  f << "colors=[";
  for (auto &color : scheme.m_colors) {
    val=int(input->readULong(2));
    unsigned char col[3];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8);
    color=MWAWColor(col[0],col[1],col[2]);
    f << color << ":" << val << ",";
  }
  f << "],";
  if (m_state->m_idToSchemeMap.find(id)!=m_state->m_idToSchemeMap.end()) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readScheme: oops, scheme S%d is already defined\n", id));
  }
  else
    m_state->m_idToSchemeMap[id]=scheme;
  if (input->tell()!=entry.end())
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

bool PowerPoint1Parser::readZone2(MWAWEntry const &entry)
{
  // probably the document current style
  int const expectedSize=m_state->m_isMacFile ? 22 : 32;
  if (!entry.valid() || entry.length()!=expectedSize) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::readZone2: the zone seems bad\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = entry.begin();
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Zone2)[Z"<< entry.id() << "]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(entry.end());
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// try to send data
////////////////////////////////////////////////////////////
bool PowerPoint1Parser::sendSlide(PowerPoint1ParserInternal::Slide const &slide, bool master)
{
  MWAWPresentationListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendSlide: can not find the listener\n"));
    return false;
  }
  slide.m_textZones[0].m_schemeId=slide.m_textZones[1].m_schemeId=slide.m_schemeId;
  // first is title, better to remove it in the master slide
  for (size_t f=master ? 1 : 0; f<slide.m_framesList[0].size(); ++f)
    sendFrame(slide.m_framesList[0][f], slide.m_textZones[0]);
  if (!slide.m_framesList[1].empty() && !slide.m_textZones[1].empty()) {
    MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(200,200), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    MWAWSubDocumentPtr doc(new PowerPoint1ParserInternal::SubDocument(*this, getInput(), &slide));
    listener->insertSlideNote(pos, doc);
  }
  return true;
}

bool PowerPoint1Parser::sendSlideNote(PowerPoint1ParserInternal::Slide const &slide)
{
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendSlideNote: can not find the listener\n"));
    return false;
  }
  // normally, the note rectangles, followed by the note's text
  for (auto const &frame : slide.m_framesList[1]) {
    if (frame.m_type==1) continue;
    if (frame.m_type!=2 && frame.m_type!=3) {
      static bool first=true;
      if (first) {
        first=false;
        MWAW_DEBUG_MSG(("PowerPoint1Parser::sendSlideNote: find unexpected frame\n"));
      }
      continue;
    }
    sendText(slide.m_textZones[1], frame.m_textId, frame.m_type==2 ? frame.m_rulerId : -1);
  }
  return true;
}

bool PowerPoint1Parser::sendPicture(MWAWPosition const &position, MWAWGraphicStyle const &style, int pId)
{
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendPicture: can not find the listener\n"));
    return false;
  }
  if (pId<0) return true;
  if (pId>=int(m_state->m_picturesIdList.size())) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendPicture: can not find the picture with id=%d\n", pId));
    return false;
  }
  int zId=m_state->m_picturesIdList[size_t(pId)];
  if (zId<=0 || zId>=int(m_state->m_zonesList.size()))
    return true;
  MWAWEmbeddedObject picture;
  if (!readPicture(m_state->m_zonesList[size_t(zId)], picture) || picture.isEmpty())
    return true;
  listener->insertPicture(position, picture, style);
  return true;
}

bool PowerPoint1Parser::sendText(PowerPoint1ParserInternal::TextZone const &textZone, MWAWVec2i tId, int rulerId)
{
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: can not find the listener\n"));
    return false;
  }
  if (tId[0]<0 || tId[0]>=int(textZone.m_lineList.size())) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops can not find the text Z%d\n", tId[0]));
    return false;
  }
  if (tId[1]>=int(textZone.m_lineList.size())) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops can not find the text Z%d\n", tId[1]));
    tId[1]=tId[0];
  }
  PowerPoint1ParserInternal::Ruler ruler;
  bool const isMacFile=m_state->m_isMacFile;
  bool hasRuler=false;
  if (rulerId>=0 && rulerId<int(m_state->m_rulersList.size())) {
    ruler=m_state->m_rulersList[size_t(rulerId)];
    hasRuler=true;
  }
  else if (rulerId != -1) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops can not find the ruler id R%d\n", rulerId));
  }
  MWAWInputStreamPtr input = getInput();
  long pos;
  int const vers=version();
  auto const unit=double(m_state->m_unit);
  for (int z=tId[0]; z<=tId[1]; ++z) {
    if (z!=tId[0]) listener->insertEOL();
    auto const &line=textZone.m_lineList[size_t(z)];
    MWAWEntry const &fEntry=line.m_format;
    MWAWEntry const &rEntry=line.m_ruler;
    MWAWEntry const &tEntry=line.m_text;
    // update the paragraph
    MWAWParagraph para;
    para.m_tabs=ruler.m_tabs;
    para.m_justify=line.m_justify;
    if (hasRuler && line.m_outlineLevel>=0 && line.m_outlineLevel<=4) {
      auto const &outline=ruler.m_outlines[line.m_outlineLevel];
      para.m_marginsUnit=librevenge::RVNG_POINT;
      for (int i=0; i<2; ++i)
        para.m_margins[i]=unit*double(outline.m_margins[i]);
      *para.m_margins[0]-=*(para.m_margins[1]);
      para.setInterline(double(outline.m_interlines[0])*0.01, librevenge::RVNG_PERCENT);
      if (outline.m_interlines[1]>outline.m_interlines[0]) // assume 12 pt
        para.m_spacings[2]=double(outline.m_interlines[1]-outline.m_interlines[0])*0.01*12/72;
    }
    listener->setParagraph(para);
    // now read the format
    input->seek(fEntry.begin(), librevenge::RVNG_SEEK_SET);
    int const dtSz=vers==1 ? 6 : isMacFile ? 8 : 14;
    int N=(fEntry.length()%dtSz)==0 ? int(fEntry.length()/dtSz) : 0;
    libmwaw::DebugStream f;
    std::map<int, MWAWFont> posToFontMap;
    int cPos=0;
    for (int i=0; i<N; ++i) {
      pos=input->tell();
      f.str("");
      f << "TextZone-F[" << i << "]:";
      auto numC=int(input->readULong(2));
      if (isMacFile) cPos=numC;
      f << "pos=" << cPos << ",";
      MWAWFont font;
      if (!isMacFile)
        font.setId(int(input->readULong(2)));
      font.setSize(float(input->readULong(isMacFile ? 1 : 2)));
      auto flag = int(input->readULong(isMacFile ? 1 : 2));
      uint32_t flags=0;
      if (flag&0x1) flags |= MWAWFont::boldBit;
      if (flag&0x2) flags |= MWAWFont::italicBit;
      if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
      if (flag&0x8) flags |= MWAWFont::embossBit;
      if (flag&0x10) flags |= MWAWFont::shadowBit;
      if (flag&0xE0) f << "#flag=" << (flag>>5) << ",";
      font.setFlags(flags);
      if (isMacFile)
        font.setId(int(input->readULong(2)));
      if (dtSz>=8) {
        auto col=int(input->readULong(1));
        MWAWColor color;
        if (textZone.m_schemeId>=0 && getColor(col, textZone.m_schemeId, color)) {
          font.setColor(color);
          if (!color.isBlack())
            f << "col=" << color << ",";
        }
        else
          f << "#col=" << color << ",";
        auto val=int(input->readULong(1)); // 0-255
        if (val) f << "f0=" << val << ",";
      }
      if (posToFontMap.find(cPos)!=posToFontMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops, find duplicated position\n"));
        f << "##dup,";
      }
      else
        posToFontMap[cPos]=font;
      f << font.getDebugString(getParserState()->m_fontConverter);
      if (input->tell()!=pos+dtSz)
        ascii().addDelimiter(input->tell(),'|');
      input->seek(pos+dtSz, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      if (!isMacFile) cPos+=numC;
    }
    std::map<int, MWAWParagraph> posToRulerMap;
    if (rEntry.valid()) {
      // now read the rulers
      input->seek(rEntry.begin(), librevenge::RVNG_SEEK_SET);
      N=(rEntry.length()%6)==0 ? int(rEntry.length()/6) : 0;
      cPos=0;
      for (int i=0; i<N; ++i) {
        pos=input->tell();
        f.str("");
        f << "TextZone-R[" << i << "]:";
        auto numC=int(input->readULong(2));
        f << "pos=" << cPos << ",";
        MWAWParagraph cPara(para);
        auto outlineLevel=int(input->readULong(2));
        if (hasRuler && outlineLevel>0 && outlineLevel<=4) {
          f << "level=" << outlineLevel << ",";
          auto const &outline=ruler.m_outlines[outlineLevel];
          cPara.m_marginsUnit=librevenge::RVNG_POINT;
          for (int j=0; j<2; ++j)
            cPara.m_margins[j]=unit*double(outline.m_margins[j]);
          *cPara.m_margins[0]-=*(cPara.m_margins[1]);
          cPara.setInterline(double(outline.m_interlines[0])*0.01, librevenge::RVNG_PERCENT);
          if (outline.m_interlines[1]>outline.m_interlines[0]) // assume 12 pt
            cPara.m_spacings[2]=double(outline.m_interlines[1]-outline.m_interlines[0])*0.01*12/72;
        }
        else if (outlineLevel>4) {
          MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops, the outline level seems bad\n"));
          f << "###outlineLevel=" << outlineLevel << ",";
        }
        auto adjust=int(input->readULong(2));
        switch (adjust) {
        case 0: // left
          cPara.m_justify=MWAWParagraph::JustificationLeft;
          break;
        case 1:
          cPara.m_justify=MWAWParagraph::JustificationCenter;
          f << "center,";
          break;
        case 2:
          cPara.m_justify=MWAWParagraph::JustificationRight;
          f << "right,";
          break;
        case 3:
          cPara.m_justify=MWAWParagraph::JustificationFull;
          f << "justify,";
          break;
        default:
          MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: find unknown alignment\n"));
          f << "##align=" << adjust << ",";
          break;
        }
        if (posToRulerMap.find(cPos)!=posToRulerMap.end()) {
          MWAW_DEBUG_MSG(("PowerPoint1Parser::sendText: oops, find duplicated paragraph\n"));
          f << "##dup,";
        }
        else
          posToRulerMap[cPos]=cPara;
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        cPos+=numC;
      }
    }
    input->seek(tEntry.begin(), librevenge::RVNG_SEEK_SET);
    for (int i=0; i<int(tEntry.length()); ++i) {
      if (posToRulerMap.find(i)!=posToRulerMap.end())
        listener->setParagraph(posToRulerMap.find(i)->second);
      if (posToFontMap.find(i)!=posToFontMap.end())
        listener->setFont(posToFontMap.find(i)->second);
      auto c=static_cast<unsigned char>(input->readULong(1));
      switch (c) {
      case 0x9:
        listener->insertTab();
        break;
      case 0xd:
        listener->insertEOL();
        break;
      case 0x11: // command key
        listener->insertUnicode(0x2318);
        break;
      // special, if dupplicated, this is a field
      case '/': // date
      case ':': // time
      case '#': { // page number
        pos=input->tell();
        if (i+1<int(tEntry.length()) && char(input->readULong(1))==char(c)) {
          ++i;
          listener->insertField(MWAWField(c=='#' ? MWAWField::PageNumber : c=='/' ? MWAWField::Date : MWAWField::Time));
        }
        else {
          input->seek(pos, librevenge::RVNG_SEEK_SET);
          listener->insertCharacter(c);
        }
        break;
      }
      default:
        listener->insertCharacter(c);
        break;
      }
    }
  }
  return true;
}

bool PowerPoint1Parser::sendFrame(PowerPoint1ParserInternal::Frame const &frame, PowerPoint1ParserInternal::TextZone const &zone)
{
  MWAWListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendFrame: can not find the listener\n"));
    return false;
  }
  MWAWBox2f fBox(m_state->m_unit*MWAWVec2f(frame.m_dimension[0]+m_state->m_origin),
                 m_state->m_unit*MWAWVec2f(frame.m_dimension[1]+m_state->m_origin));
  if (frame.m_textId[0]>=0) {
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    MWAWSubDocumentPtr subdoc(new PowerPoint1ParserInternal::SubDocument(*this, getInput(), &zone, frame.m_textId, frame.m_type==2 ? frame.m_rulerId : -1));
    listener->insertTextBox(pos, subdoc, frame.m_style);
    return true;
  }
  switch (frame.m_type) {
  case 0:
  case 1: {
    MWAWGraphicShape shape;
    if (frame.m_type==0)
      shape=MWAWGraphicShape::line(fBox[0], fBox[1]);
    else {
      if (float(frame.m_cornerSize) >= fBox.size()[0] || float(frame.m_cornerSize) >= fBox.size()[1])
        shape=MWAWGraphicShape::circle(fBox);
      else
        shape=MWAWGraphicShape::rectangle(fBox, MWAWVec2f(float(frame.m_cornerSize)/2.f, float(frame.m_cornerSize)/2.f));
    }
    MWAWBox2f box=shape.getBdBox();
    MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    if (frame.m_type==1 && frame.m_pictureId>=0)
      sendPicture(pos, MWAWGraphicStyle::emptyStyle(), frame.m_pictureId);
    else
      listener->insertShape(pos, shape, frame.m_style);
    return true;
  }
  default:
    MWAW_DEBUG_MSG(("PowerPoint1Parser::sendFrame: can not send some frame\n"));
    break;
  }
  return false;
}

////////////////////////////////////////////////////////////
// Low level
////////////////////////////////////////////////////////////

// read the header
bool PowerPoint1Parser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = PowerPoint1ParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  if (!input->checkPosition(24+8)) {
    MWAW_DEBUG_MSG(("PowerPoint1Parser::checkHeader: file is too short\n"));
    return false;
  }
  long pos = 0;
  input->setReadInverted(false);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  unsigned long signature=input->readULong(4);
  if (signature==0xeddead0b) {
    input->setReadInverted(true);
    m_state->m_isMacFile=false;
    m_state->m_unit=1.f/8.f;
  }
  else if (signature!=0xbaddeed)
    return false;
  f << "FileHeader:";
  auto vers=int(input->readLong(4));
  if (vers!=2) return false;
  m_state->m_zoneListBegin=long(input->readULong(4));
  if (m_state->m_zoneListBegin<24 || !input->checkPosition(m_state->m_zoneListBegin))
    return false;
  f << "zone[begin]=" << std::hex << m_state->m_zoneListBegin << std::dec << ",";

  if (strict) {
    input->seek(12, librevenge::RVNG_SEEK_SET);
    auto val=int(input->readULong(2));
    if (!input->checkPosition(m_state->m_zoneListBegin+val*8))
      return false;
  }
  input->seek(12, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  setVersion(1);
  if (header)
    header->reset(MWAWDocument::MWAW_T_POWERPOINT, 1, MWAWDocument::MWAW_K_PRESENTATION);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
