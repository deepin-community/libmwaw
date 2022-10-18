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
#include <sstream>
#include <stack>
#include <utility>

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

#include "PowerPoint3OLE.hxx"
#include "PowerPoint7Graph.hxx"
#include "PowerPoint7Parser.hxx"
#include "PowerPoint7Struct.hxx"
#include "PowerPoint7Text.hxx"

/** Internal: the structures of a PowerPoint7Parser */
namespace PowerPoint7ParserInternal
{
//! Internal: the basic information about a slide/notes/... zone in a PowerPoint7Parser
struct SlideInfo {
  //! constructor
  SlideInfo()
    : m_box()
    , m_displayMaster(true)
    , m_hasBackground(false)
  {
  }
  //! the bdbox
  MWAWBox2i m_box;
  //! a flag to know if we need to display the master slide graphic
  bool m_displayMaster;
  //! a flag to know if the slide has some background or no background
  bool m_hasBackground;
};
////////////////////////////////////////
//! Internal: the state of a PowerPoint7Parser
struct State {
  //! constructor
  State()
    : m_fontFamily("CP1252")
    , m_pageDimension(5760,4320)
    , m_graphParser()
    , m_textParser()
    , m_oleParser()
    , m_slideList()
    , m_masterSlideIdToNameMap()
    , m_idToMasterIdMap()
    , m_idToSlideInfoMap()
    , m_slideIdStack()
    , m_colorListStack()
  {
  }
  //! add a slide
  void addSlide(PowerPoint7Struct::SlideId const &sId, PowerPoint7Struct::SlideId const &mId)
  {
    if (!sId.isValid())
      return;
    if (sId.m_isMaster) {
      if (m_masterSlideIdToNameMap.find(sId)!=m_masterSlideIdToNameMap.end())
        return;
      std::stringstream s;
      s << "Master" << m_masterSlideIdToNameMap.size();
      m_masterSlideIdToNameMap[sId]=librevenge::RVNGString(s.str().c_str());
      return;
    }
    m_slideList.push_back(sId);
    if (mId.isValid())
      m_idToMasterIdMap[sId]=mId;
  }
  //! push a new slide id
  void pushSlideId(PowerPoint7Struct::SlideId const &id)
  {
    if (m_graphParser) m_graphParser->setSlideId(id);
    m_slideIdStack.push(id);
  }
  //! pop a slide id
  void popSlideId()
  {
    if (m_slideIdStack.empty()) {
      MWAW_DEBUG_MSG(("PowerPoint7ParserInternal::State::popSlideId: the stack is empty\n"));
      return;
    }
    m_slideIdStack.pop();
    if (m_graphParser)
      m_graphParser->setSlideId(m_slideIdStack.empty() ? PowerPoint7Struct::SlideId() : m_slideIdStack.top());
  }
  //! push a new slide id
  void pushColorList(std::vector<MWAWColor> const &colorList)
  {
    if (m_graphParser) m_graphParser->setColorList(colorList);
    m_colorListStack.push(colorList);
  }
  //! pop a slide id
  void popColorList()
  {
    if (m_colorListStack.empty()) {
      MWAW_DEBUG_MSG(("PowerPoint7ParserInternal::State::popSlideId: the stack is empty\n"));
      return;
    }
    m_colorListStack.pop();
    if (m_graphParser)
      m_graphParser->setColorList(m_colorListStack.empty() ? std::vector<MWAWColor>() : m_colorListStack.top());
  }
  //! the basic pc font family if known
  std::string m_fontFamily;
  //! the page dimension
  MWAWVec2i m_pageDimension;
  //! the graph parser
  std::shared_ptr<PowerPoint7Graph> m_graphParser;
  //! the text parser
  std::shared_ptr<PowerPoint7Text> m_textParser;
  //! the ole parser
  std::shared_ptr<PowerPoint3OLE> m_oleParser;
  //! the list of slides
  std::vector<PowerPoint7Struct::SlideId> m_slideList;
  //! the master slide
  std::map<PowerPoint7Struct::SlideId, librevenge::RVNGString> m_masterSlideIdToNameMap;
  //! the slideId to masterId slide
  std::map<PowerPoint7Struct::SlideId, PowerPoint7Struct::SlideId> m_idToMasterIdMap;
  //! the slideId to information slide
  std::map<PowerPoint7Struct::SlideId, SlideInfo> m_idToSlideInfoMap;
  //! a stack of slide id
  std::stack<PowerPoint7Struct::SlideId> m_slideIdStack;
  //! a stack of color list
  std::stack<std::vector<MWAWColor> > m_colorListStack;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint7Parser::PowerPoint7Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWPresentationParser(input, rsrcParser, header)
  , m_state(new PowerPoint7ParserInternal::State)
{
  setAsciiName("main-1");
  m_state->m_graphParser.reset(new PowerPoint7Graph(*this));
  m_state->m_textParser.reset(new PowerPoint7Text(*this));
}

PowerPoint7Parser::~PowerPoint7Parser()
{
}

bool PowerPoint7Parser::sendText(int textId)
{
  return m_state->m_textParser->sendText(textId);
}

bool PowerPoint7Parser::getColor(int cId, MWAWColor &col) const
{
  if (m_state->m_colorListStack.empty() || cId<0 ||
      cId>=int(m_state->m_colorListStack.top().size())) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::getColor: can not find color %d\n", cId));
    return false;
  }
  col=m_state->m_colorListStack.top()[size_t(cId)];
  return true;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void PowerPoint7Parser::parse(librevenge::RVNGPresentationInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendSlides();
    }

#ifdef DEBUG
    if (m_state->m_oleParser)
      m_state->m_oleParser->checkForUnparsedStream();
    checkForUnparsedZones();
#endif
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetPresentationListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void PowerPoint7Parser::createDocument(librevenge::RVNGPresentationInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getPresentationListener()) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::createDocument: listener already exist\n"));
    return;
  }

  std::vector<MWAWPageSpan> pageList;
  for (auto const &id : m_state->m_slideList) {
    if (!id.isValid()) continue;
    MWAWPageSpan ps(getPageSpan());
    bool showMaster=true;
    if (m_state->m_idToSlideInfoMap.find(id)!=m_state->m_idToSlideInfoMap.end())
      showMaster=m_state->m_idToSlideInfoMap.find(id)->second.m_displayMaster;
    if (showMaster && m_state->m_idToMasterIdMap.find(id)!=m_state->m_idToMasterIdMap.end()) {
      PowerPoint7Struct::SlideId mId=m_state->m_idToMasterIdMap.find(id)->second;
      if (m_state->m_masterSlideIdToNameMap.find(mId)!=m_state->m_masterSlideIdToNameMap.end())
        ps.setMasterPageName(m_state->m_masterSlideIdToNameMap.find(mId)->second);
    }
    pageList.push_back(ps);
  }

  //
  MWAWPresentationListenerPtr listen(new MWAWPresentationListener(*getParserState(), pageList, documentInterface));
  setPresentationListener(listen);
  if (m_state->m_oleParser) {
    librevenge::RVNGPropertyList metaData;
    m_state->m_oleParser->updateMetaData(metaData);
    listen->setDocumentMetaData(metaData);
  }
  listen->startDocument();
}


////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

// create the different zones
bool PowerPoint7Parser::createZones()
{
  MWAWInputStreamPtr input=getInput();
  if (!input || !input->isStructured()) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::createZones: can not find the main input\n"));
    return false;
  }

  MWAWInputStreamPtr mainOle=input->getSubStreamByName("PowerPoint Document");
  if (!mainOle) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::createZones: can not find the main ole\n"));
    return false;
  }
  int fId=getFontConverter()->getId("CP1252");
  m_state->m_oleParser.reset(new PowerPoint3OLE(input, version(), getFontConverter(), fId));
  m_state->m_oleParser->parse();
  int encoding=m_state->m_oleParser->getFontEncoding();
  if (encoding>=1250 && encoding<=1258) {
    std::stringstream s;
    s << "CP" << encoding;
    m_state->m_fontFamily=s.str();
    m_state->m_textParser->setFontFamily(m_state->m_fontFamily);
  }
  parseTextContent(input->getSubStreamByName("Text_Content"));
  getParserState()->m_input=input=mainOle;
  input->setReadInverted(true);

  // create the asciiFile
  ascii().setStream(input);
  ascii().open(asciiName());

  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (!readDocRoot())
    return false;
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Extra):###");
  }
  return !m_state->m_slideList.empty();
}

bool PowerPoint7Parser::readDocRoot()
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();
  long lastPos=input->size();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocRoot: can not find the main zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Root):" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2)); //3e8 followed by a
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 0xa:
      done=readMainSub10(endPos);
      break;
    case 1000:
      done=readDocument(endPos);
      break;
    default:
      done=readZone(1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocRoot: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocRoot: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Root:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readMainSub10(long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=10) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readMainSub10: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(MSub10):" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readMainSub10: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    int val;
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(2));
      if (val!=1-i) f << "f" << i << "=" << val << ",";
    }
    val=int(input->readULong(4)); // 100|11c|123
    if (val!=0x100) f << "unk=" << std::hex << val << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readDocument(long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1000) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocument: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(DocMain):" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1001:
      done=readDocAtom(1,endPos);
      break;
    case 1002:
      done=readZoneNoData(1,endPos,"DocMain","end");
      break;
    case 1008:
      done=readNotes(1,endPos);
      break;
    case 1010:
      done=readEnvironment(1,endPos);
      break;
    case 1025:
      done=readSSDocInfoAtom(1,endPos);
      break;
    case 1026:
      done=readSummary(1,endPos);
      break;
    case 2000:
      done=readContainerList(1,endPos);
      break;
    case 4041:
      done=readHandout(1,endPos);
      break;
    default:
      done=readZone(1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocument: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocument: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("DocMain:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readDocAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1001) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(DocMain)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x2c) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readDocAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    int val;
    for (int i=0; i<2; ++i) {
      int dim[2];
      for (auto &d : dim) d=int(input->readLong(4));
      MWAWVec2i size(dim[0],dim[1]);
      if (i==0 && dim[0]>0 && dim[1]>0) {
        m_state->m_pageDimension=size;
        m_state->m_graphParser->setPageSize(size);

        getPageSpan().setFormLength(double(size.y())/576.0);
        getPageSpan().setFormWidth(double(size.x())/576.0);
      }
      char const *wh[]= {"page","paper"};
      f << "dim[" << wh[i] << "]=" << size << ",";
    }
    for (int i=0; i<2; ++i) { // f0=0|1, f1=1
      val=int(input->readULong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    f << "ids=[";
    for (int i=0; i<2; ++i)
      f << std::hex << input->readULong(4) << std::dec << ",";
    f << "],";
    for (int i=0; i<7; ++i) { // f2=db00|e[34]00, f3,f5,f7 small number
      val=int(input->readULong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlideInformation(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1005) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideInformation: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideInfo)[" << level << "]:" << header;
  if (header.m_dataSize!=0x18) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideInformation: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }
  PowerPoint7ParserInternal::SlideInfo info;
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  info.m_box=MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  f << "box=" << info.m_box << ",";
  int val;
  for (int i=0; i<2; ++i) { // always 1,1
    val=int(input->readLong(1));
    if (val==1) continue;
    if (i==0) {
      if (val==0) {
        info.m_displayMaster=false;
        f << "omit[graphic,master],";
      }
      else
        f << "###omit[graphic,master]=" << val << ",";
    }
    else
      f << "fl" << i << "=" << val << ",";
  }
  val=int(input->readLong(2)); // 0
  if (val) f << "f0=" << val << ",";
  val=int(input->readULong(1));
  if (val==0) {
    info.m_hasBackground=true;
    f << "has[background],";
  }
  else if (val!=1)
    f << "##has[background]=" << val << ",";
  for (int i=0; i<3; ++i) { //  fl2=0-e3, fl3=0|8|9|62|b7,fl4=0|50
    val=int(input->readULong(1));
    if (val) f << "fl" << i+2 << "=" << std::hex << val << std::dec << ",";
  }
  if (m_state->m_slideIdStack.empty() ||
      m_state->m_idToSlideInfoMap.find(m_state->m_slideIdStack.top())!=m_state->m_idToSlideInfoMap.end()) {
    f << "###noSave,";
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideInformation: can not save the information\n"));
  }
  else
    m_state->m_idToSlideInfoMap[m_state->m_slideIdStack.top()]=info;
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlides(int level, long lastPos, bool master)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  int const expectedType=master ? 1016 : 1006;
  if (!header.read(input,lastPos) || header.m_type!=expectedType) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlides: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideContent)[" << level << "]:" << header;
  if (master) f << "master,";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  bool pushSlideId=false, pushColor=false;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1005:
      done=readSlideInformation(level+1,endPos);
      break;
    case 1007: {
      PowerPoint7Struct::SlideId sId, mId;
      done=readSlideAtom(level+1,endPos, sId, mId);
      if (done && !pushSlideId) {
        pushSlideId=true;
        m_state->addSlide(sId,mId);
        m_state->pushSlideId(sId);
      }
      break;
    }
    case 1008:
      done=readNotes(level+1,endPos);
      break;
    case 1012: {
      std::vector<MWAWColor> colors;
      done=readColorScheme(level+1,endPos,colors);
      if (done && !pushColor) {
        pushColor=true;
        m_state->pushColorList(colors);
      }
      break;
    }
    case 1015:
      done=readZone1015(level+1,endPos);
      break;
    case 1017:
      done=readSlideShowInfo(level+1,endPos);
      break;
    case 2031: { // in master, the different color scheme are stored directly here
      std::vector<MWAWColor> colors;
      done=readColorList(level+1,endPos, colors);
      break;
    }
    case 3000:
      done=readZone3000(level+1,endPos);
      break;
    case 3008:
      done=m_state->m_graphParser->readRect(level+1,endPos);
      break;
    case 4026: {
      std::string string;
      int zId; // 140: template name(in master)
      done=readString(level+1,endPos, string, zId, "SlideContent");
      break;
    }
    case 4057:
      done=readHeaderFooters(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlides: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlides: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SlideContent:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (pushColor)
    m_state->popColorList();
  if (pushSlideId)
    m_state->popSlideId();
  return true;
}

bool PowerPoint7Parser::readSlideAtom(int level, long lastPos, PowerPoint7Struct::SlideId &sId, PowerPoint7Struct::SlideId &mId)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1007) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideContent)[atom," << level << "]:" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) {
      PowerPoint7Struct::SlideId id(input->readULong(4));
      if (!id.isValid()) continue;
      if (i==0) {
        sId=id;
        f << id << ",";
      }
      else {
        mId=id;
        f << "master=" << id << ",";
      }
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readNotes(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1008) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readNotes: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Note)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  bool pushSlideId=false, pushColor=false;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1005:
      done=readSlideInformation(level+1,endPos);
      break;
    case 1009: {
      PowerPoint7Struct::SlideId sId;
      done=readNoteAtom(level+1,endPos, sId);
      if (done && !pushSlideId) {
        pushSlideId=true;
        m_state->pushSlideId(sId);
      }
      break;
    }
    case 1012: {
      std::vector<MWAWColor> colors;
      done=readColorScheme(level+1,endPos,colors);
      if (done && !pushColor) {
        pushColor=true;
        m_state->pushColorList(colors);
      }
      break;
    }
    case 3000:
      done=readZone3000(level+1,endPos);
      break;
    case 3008:
      done=m_state->m_graphParser->readRect(level+1,endPos);
      break;
    case 4057:
      done=readHeaderFooters(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readNotes: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readNotes: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Note:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (pushColor)
    m_state->popColorList();
  if (pushSlideId)
    m_state->popSlideId();
  return true;
}

bool PowerPoint7Parser::readNoteAtom(int level, long lastPos, PowerPoint7Struct::SlideId &sId)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1009) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readNoteAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Note)[atom" << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readNoteAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    sId=PowerPoint7Struct::SlideId(input->readULong(4));
    sId.m_inNotes=true;
    if (sId.isValid())
      f << sId << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readEnvironment(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1010) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readEnvironment: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(EnvironList)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1028:
      done=readZone1028(level+1,endPos);
      break;
    case 2005: // FontCollection
      done=m_state->m_textParser->readFontCollection(level+1,endPos);
      break;
    case 2006:
      done=m_state->m_graphParser->readPictureList(level+1,endPos);
      break;
    case 2020: // SoundCollection
      done=readSoundCollection(level+1,endPos);
      break;
    case 2027:
      done=m_state->m_textParser->readFieldList(level+1,endPos);
      break;
    case 2031: { // basic font color?
      std::vector<MWAWColor> colors;
      done=readColorList(level+1,endPos, colors);
      break;
    }
    case 3012:
      done=readZone3012(level+1,endPos);
      break;
    case 4016:
      done=m_state->m_textParser->readRulerList(level+1,endPos);
      break;
    case 4040:
      done=readKinsoku(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readEnvironment: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readEnvironment: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("EnvironList:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readColorScheme(int level, long lastPos, std::vector<MWAWColor> &colors)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1012) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorScheme: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ColorScheme)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2031:
      done=readColorList(level+1,endPos, colors);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorScheme: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorScheme: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("ColorScheme:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone1015(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1015) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1015: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone1015B)[" << level << "]:" << header;
  if (header.m_dataSize!=12) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1015: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    int val;
    for (int i=0; i<2; ++i) { // f0=1|10
      val=int(input->readULong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    for (int i=0; i<4; ++i) { // small number
      val=int(input->readULong(1));
      if (val) f << "f" << i+2 << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) { // f6=0|7
      val=int(input->readULong(2));
      if (val) f << "f" << i+6 << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlideShowInfo(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1017) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideShowInfo: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideShowInfo)[" << level << "]:" << header;
  if (header.m_dataSize!=24) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideShowInfo: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<6; ++i) {
      auto val=int(input->readLong(4));
      int const expected[]= {0,2,2,0,1,-1};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlideViewInfo(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1018) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideViewInfo: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideViewInfo)[list," << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1021:
      done=readViewInfoAtom(level+1,endPos);
      break;
    case 1022:
      done=readSlideViewInfoAtom(level+1,endPos);
      break;
    case 2026:
      done=readZone2026(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideViewInfo: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideViewInfo: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SlideViewInfo:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readGuideAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1019) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readGuideAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GuideAtom)[" << level << "]:" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readGuideAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<4; ++i) { // f0=0|1
      auto val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readViewInfoAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1021) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readViewInfoAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(VViewInfoAtom)[" << level << "]:" << header;
  if (header.m_dataSize!=52) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readViewInfoAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    f << "dims=[";
    for (int i=0; i<4; ++i) {
      long number=int(input->readLong(4));
      long denom=int(input->readLong(4));
      f << number << "/" << denom << ",";
    }
    f << "],";
    f << "dim2=[";
    for (int i=0; i<4; ++i)
      f << input->readLong(4) << ",";
    f << "],";
    for (int i=0; i<2; ++i) { // f0=0|1, f1=76-b8
      auto val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlideViewInfoAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1022) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideViewInfoAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideViewInfo)[atom," << level << "]:" << header;
  if (header.m_dataSize!=2) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideViewInfoAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    unsigned long val=input->readULong(2);
    if (val!=0x100)
      f << "id?=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readVbaInfo(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1023) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readVbaInfo: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(VbaInfo)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1024:
      done=readVbaInfoAtom(level+1,endPos);
      break;
    case 4026: {
      std::string string;
      int zId; // 7:_VBA_PROJECT, 160:""
      done=readString(level+1,endPos,string,zId,"VbaInfo");
      break;
    }
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readVbaInfo: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readVbaInfo: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("VbaInfo:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readVbaInfoAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1024) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readVbaInfoAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(VbaInfo)[atom," << level << "]:" << header;
  if (header.m_dataSize%4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readVbaInfoAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    auto N=int(header.m_dataSize/4);
    for (int i=0; i<N; ++i) { // always 0
      auto val=int(input->readLong(4));
      if (val) f << "f" << i << "=" << val << ",";
    }
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSSDocInfoAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1025) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSSDocInfoAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SSDocInfoAtom)[" << level << "]:" << header;
  if (header.m_dataSize!=0xc) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSSDocInfoAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<6; ++i) {
      auto val=int(input->readLong(2));
      int const expected[]= {1,0,0,0,0,0x100};
      if (val!=expected[i])
        f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSummary(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1026) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSummary: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SummaryList)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2019:
      done=readBookmarkCollection(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSummary: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSummary: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SummaryList:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone1028(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1028) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone1028B)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1029:
      done=readZone1028Atom(level+1,endPos);
      break;
    case 4052:
      done=readZone1028Data(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Zone1028:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone1028Atom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1029) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028Atom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone1028B)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0xa) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028Atom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<10; ++i) { // f0-f6=0|1,f7=f8=0, f9=d-b2
      auto val=int(input->readULong(1));
      if (val==1)
        f << "f" << i << ",";
      else if (val)
        f << "f" << i << "=" << std::hex << val << std::dec << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readOutlineViewInfo(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1031) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineViewInfo: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(OutlineViewInfo)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1021:
      done=readViewInfoAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineViewInfo: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineViewInfo: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("OutlineViewInfo:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readSorterViewInfo(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1032) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSorterViewInfo: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SorterViewInfo)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1021:
      done=readViewInfoAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSorterViewInfo: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSorterViewInfo: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SorterViewInfo:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readContainerList(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2000) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Container)[list," << level << "]:" << header;
  switch (header.m_values[3]) {
  case 10:
    f << "slides,";
    break;
  case 11:
    f << "slides[master],";
    break;
  case 12: // vbaInfo, viewInfo, bookmark collection
    f << "info,";
    break;
  case 15:
    f << "group,";
    break;
  default:
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerList: unknown type\n"));
    f << "##type=" << header.m_values[3] << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1006:
      done=readSlides(level+1,endPos,false);
      break;
    case 1016:
      done=readSlides(level+1,endPos,true);
      break;
    case 1018:
      done=readSlideViewInfo(level+1,endPos);
      break;
    case 1023:
      done=readVbaInfo(level+1,endPos);
      break;
    case 1031:
      done=readOutlineViewInfo(level+1,endPos);
      break;
    case 1032:
      done=readSorterViewInfo(level+1,endPos);
      break;
    case 2001: {
      int N; // the number of child: slides, shapes, ...
      done=readContainerAtom(level+1,endPos,N);
      break;
    }
    case 3001:
      done=m_state->m_graphParser->readGroup(level+1,endPos);
      break;
    case 3008:
      done=m_state->m_graphParser->readRect(level+1,endPos);
      break;
    case 3014:
      done=m_state->m_graphParser->readLine(level+1,endPos);
      break;
    case 3016:
      done=m_state->m_graphParser->readPolygon(level+1,endPos);
      break;
    case 3018:
      done=m_state->m_graphParser->readArc(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerList: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerList: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Container:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readContainerAtom(int level, long lastPos, int &N)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2001) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Container)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readContainerAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    N=0;
  }
  else {
    N=int(input->readULong(4));
    f << "N=" << N << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readIdentifier(int level, long endPos, int &id, std::string const &wh)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();
  long lastPos=endPos<0 ? input->size() : endPos;
  if (pos+16>lastPos)
    return false;

  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2017) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  libmwaw::DebugStream f;
  if (!wh.empty())
    f << "Entries(" << wh << ")[id," << level << "]:" << header;
  else
    f << "Entries(Identifier)[" << level << "]:" << header;
  if (header.m_dataSize) {
    f << "###dSz=" << header.m_dataSize << ",";
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readIdentifier: find unexpected data for zone\n"));
  }
  id=header.m_values[3];
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readBookmarkCollection(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2019) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readBookmarkCollection: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(BookmarkCollection)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2017: {
      int id;
      done=readIdentifier(level+1,endPos,id,"BookmarkCollection");
      break;
    }
    case 2018:
      done=readZoneNoData(level+1,endPos,"BookmarkCollection","id,end");
      break;
    case 2025:
      done=readBookmarkSeedAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readBookmarkCollection: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readBookmarkCollection: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("BookmarkCollection:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readSoundCollection(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2020) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSoundCollection: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SoundCollection)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSoundCollection: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSoundCollection: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SoundCollection:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readBookmarkSeedAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2025) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readBookmarkSeedAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(BookmarkSeedAtom)[" << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readBookmarkSeedAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) { // f0=1
      auto val=int(input->readULong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readZone2026(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2026) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone2026: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone2026B)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1019:
      done=readGuideAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone2026: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone2026: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Zone2026B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readColorList(int level, long lastPos, std::vector<MWAWColor> &colors)
{
  colors.resize(0);

  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2031) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  long endPos=pos+16+header.m_dataSize;
  f << "Entries(ColorList)[" << level << "]:" << header;
  bool ok=true;
  if (header.m_dataSize<4 || (header.m_dataSize%4)!=0) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorList: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    ok=false;
  }
  int N=ok? int(input->readULong(4)) : 0;
  if (ok && header.m_dataSize/4-1!=N) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readColorList: the number of colors seems bad\n"));
    f << "###N=" << N << ",";
    N=0;
  }
  f << "colors=[";
  colors.resize(size_t(N));
  for (auto &color : colors) {
    unsigned char col[4];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
    color=MWAWColor(col[0],col[1],col[2]);
    f << color << ",";
  }
  f << "],";
  if (input->tell()!=endPos) {
    ascii().addDelimiter(pos+16,'|');
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readZone3000(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3000) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3000: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone3000B)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3001:
      done=m_state->m_graphParser->readGroup(level+1,endPos);
      break;
    case 3008:
      done=m_state->m_graphParser->readRect(level+1,endPos);
      break;
    case 3010:
      done=m_state->m_graphParser->readPlaceholderContainer(level+1,endPos);
      break;
    case 3014:
      done=m_state->m_graphParser->readLine(level+1,endPos);
      break;
    case 3016:
      done=m_state->m_graphParser->readPolygon(level+1,endPos);
      break;
    case 3018:
      done=m_state->m_graphParser->readArc(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3000: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3000: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Zone3000B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone3012(int level, long lastPos)
{
  // one by file: maybe a default frame?
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3012) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3012: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone3012B)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=m_state->m_graphParser->readStyle(level+1,endPos);
      break;
    case 3009:
      done=m_state->m_graphParser->readRectAtom(level+1,endPos);
      break;
    case 3013:
      done=readZone3012Atom(level+1,endPos);
      break;
    case 4001: {
      int tId=-1;
      done=readStyleTextPropAtom(level+1,endPos,tId);
      break;
    }
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3012: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3012: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Zone3012B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone3012Atom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3013) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3012Atom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone3012B)[atom," << level << "]:" << header;
  if (header.m_dataSize!=2) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone3012Atom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    auto val=int(input->readULong(2)); // 0
    if (val)
      f << "f0=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readTextCharsAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4000) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readTextCharsAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(TextCharsAtom)[" << level << "]:" << header;
  if (header.m_dataSize!=16) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readTextCharsAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) { // the text anchor dimension
      unsigned long val=input->readULong(4);
      if (!val) continue;
      f << "anchor" << (i==0 ? "LR" : "BT") << "=" << float(val)/8.f << "pt,";
    }
    auto val=int(input->readULong(1));
    if (val&1) f << "adjust[text],";
    if (val&4) f << "wrap[word],";
    val&=0xfa;
    if (val!=0xc0)
      f << "fl0=" << std::hex << val << std::dec << ",";
    for (int i=0; i<7; ++i) {
      val=int(input->readULong(1));
      int const expected[]= {0x6e,7,0x50,3,0xe0,0x62,0};
      if (val==expected[i])
        continue;
      if (i==3) {
        f << "v[align]=" << (val>>4) << ","; // 1: center, 2:top
        f << "h[align]=" << (val&0xf) << ","; // 1: center, 3:left?
      }
      else
        f << "fl" << i+1 << "=" << std::hex << val << std::dec << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readStyleTextPropAtom(int level, long lastPos, int &tId)
{
  tId=-1;
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4001) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readStyleTextPropAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(StyleTextPropAtom)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4000:
      done=readTextCharsAtom(level+1,endPos);
      break;
    case 4002:
      if (tId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readStyleTextPropAtom: find duplicated text zone\n"));
      }
      done=m_state->m_textParser->readTextMasterProp(level+1,endPos,tId);
      break;
    case 4068:
      if (tId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readStyleTextPropAtom: find duplicated text zone\n"));
      }
      done=m_state->m_textParser->readExternalHyperlink9(level+1,endPos,tId);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readStyleTextPropAtom: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readStyleTextPropAtom: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("StyleTextPropAtom:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readOutlineTextProps9Atom(int level, long lastPos, int &id, PowerPoint7Struct::SlideId &sId)
{
  id=-1;
  sId=PowerPoint7Struct::SlideId();
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4014) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextProps9Atom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(OutlineTextProps9)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4015:
      done=readOutlineTextPropsHeader9Atom(level+1,endPos);
      break;
    case 4044: {
      int pId;
      done=m_state->m_graphParser->readExternalOleEmbed(level+1,endPos,pId);
      if (done && pId!=-1 && id==-1) id=pId;
      break;
    }
    case 4053: {
      int pId;
      done=m_state->m_graphParser->readPictureIdContainer(level+1,endPos,pId);
      if (done && pId!=-1) id=pId;
      break;
    }
    case 4054:
      done=readSlideIdentifierContainer(level+1,endPos,sId);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextProps9Atom: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextProps9Atom: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("OutlineTextProps9:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (id==-1 && !sId.isValid()) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextProps9Atom: does not find any data\n"));
  }
  return true;
}

bool PowerPoint7Parser::readOutlineTextPropsHeader9Atom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4015) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextPropsHeader9Atom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(OutlineTextProps9)[header," << level << "]:" << header;
  if (header.m_dataSize!=16) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readOutlineTextPropsHeader9Atom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<8; ++i) { // 0
      auto val=int(input->readULong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readString(int level, long lastPos, std::string &text, int &zId, std::string const &what)
{
  text="";
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4026) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readString: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  zId=header.m_values[3];
  if (!what.empty())
    f << "Entries(" << what << ")[string," << level << "]:" << header;
  else
    f << "Entries(CString)[" << level << "]:" << header;
  for (long i=0; i<header.m_dataSize; ++i) text+=char(input->readULong(1));
  f << text << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readSlideIdentifier(int level, long lastPos, PowerPoint7Struct::SlideId &sId)
{
  sId=PowerPoint7Struct::SlideId();
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4032) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideIdentifier: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideId)[" << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideIdentifier: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    sId=PowerPoint7Struct::SlideId(input->readULong(4));
    if (sId.isValid())
      f << sId << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readKinsoku(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4040) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readKinsoku: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Kinsoku)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4026: {
      std::string string;
      int zId; // 4:"", 5:""
      done=readString(level+1,endPos,string,zId,"Kinsoku");
      break;
    }
    case 4050:
      done=readKinsokuAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readKinsoku: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readKinsoku: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Kinsoku:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZone4039(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4039) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone4039: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone4039B)[" << level << "]:" << header;
  if (header.m_dataSize!=0x20) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone4039: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }

  for (int i=0; i<16; ++i) { // f0=1010,f1=6,f14=f15=-1
    auto val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Parser::readHandout(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4041) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHandout: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(HandoutList)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  PowerPoint7Struct::SlideId hId;
  hId.m_inHandout=true;
  m_state->pushSlideId(hId);
  long endPos=pos+16+header.m_dataSize;
  bool pushColor=false;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1005:
      done=readSlideInformation(level+1,endPos);
      break;
    case 1012: {
      std::vector<MWAWColor> colors;
      done=readColorScheme(level+1,endPos,colors);
      if (done && !pushColor) {
        pushColor=true;
        m_state->pushColorList(colors);
      }
      break;
    }
    case 3000:
      done=readZone3000(level+1,endPos);
      break;
    case 3008:
      done=m_state->m_graphParser->readRect(level+1,endPos);
      break;
    case 4057:
      done=readHeaderFooters(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readHandout: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHandout: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("HandoutList:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  if (pushColor)
    m_state->popColorList();
  m_state->popSlideId();
  return true;
}

bool PowerPoint7Parser::readKinsokuAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4050) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readKinsokuAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Kinsoku)[atom," << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readKinsokuAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else { // 0,10000,cdcdcdcd
    f << "f0=" << std::hex << input->readULong(4) << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readZone1028Data(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4052) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028Data: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone1028B)[data," << level << "]:" << header;
  if (header.m_dataSize!=0x1d8) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone1028Data: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+16+8, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<12; ++i) {
    // A0-A4 is probably related to font, A6-A11 is probably related to ruler
    pos=input->tell();
    int const dSz=i<5 ? 24 : i==5 ? 32 : 52;
    f.str("");
    f << "Zone1028B-A" << i << ":";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+dSz, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool PowerPoint7Parser::readSlideIdentifierContainer(int level, long lastPos, PowerPoint7Struct::SlideId &sId)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4054) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideIdentifierContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(SlideId)[container," << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4032:
      done=readSlideIdentifier(level+1,endPos, sId);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideIdentifierContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readSlideIdentifierContainer: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("SlideId:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readHeaderFooters(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4057) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHeaderFooters: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(HF)[list," << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4026: {
      std::string string;
      int zId; // 46:"", 47:"", 48:""
      done=readString(level+1,endPos,string,zId,"HF");
      break;
    }
    case 4058:
      done=readHeaderFooterAtom(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readHeaderFooters: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHeaderFooters: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("HF:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readHeaderFooterAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4058) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHeaderFooterAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(HF)[atom," << level << "]:" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readHeaderFooterAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascii().addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    f << "flags=[";
    for (int i=0; i<8; ++i) { // list of 0 and 1
      auto val=int(input->readULong(1));
      if (val==1)
        f << "*,";
      else if (val)
        f << val << ",";
      else
        f << "_,";
    }
    f << "],";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readZone4072(int level, long lastPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4072) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone4072: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone4072B)[" << level << "]:" << header;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4026: {
      std::string string;
      int zId; // 160:""
      done=readString(level+1,endPos,string,zId,"Zone4072B");
      break;
    }
    case 4039:
      done=readZone4039(level+1,endPos);
      break;
    default:
      done=readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone4072: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone4072: can not read some data\n"));
    ascii().addPos(pos);
    ascii().addNote("Zone4072B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Parser::readZoneNoData(int level, long endPos, std::string const &name, std::string const &wh)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();
  long lastPos=endPos<0 ? input->size() : endPos;
  if (pos+16>lastPos)
    return false;

  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  libmwaw::DebugStream f;
  if (!wh.empty())
    f << "Entries(" << name << ")[" << wh << "," << level << "]:" << header;
  else
    f << "Entries(" << name << ")[" << level << "]:" << header;
  if (header.m_dataSize) {
    f << "###dSz=" << header.m_dataSize << ",";
    MWAW_DEBUG_MSG(("PowerPoint7Parser::readZoneNoData: find unexpected data for zone %s\n", name.c_str()));
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Parser::readZone(int level, long endPos)
{
  MWAWInputStreamPtr input=getInput();
  long pos=input->tell();
  long lastPos=endPos<0 ? input->size() : endPos;
  if (pos+16>lastPos)
    return false;

  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(" << header.getName() << ")[" << level << "]:" << header;
  long lastDataPos=pos+16+header.m_dataSize;
  bool isList=false;
  if (header.m_dataSize>=16) {
    // first check that we can read all data
    isList=true;
    while (input->tell()<lastDataPos) {
      PowerPoint7Struct::Zone cHeader;
      if (!cHeader.read(input, lastDataPos)) {
        isList=false;
        break;
      }
      input->seek(cHeader.m_dataSize, librevenge::RVNG_SEEK_CUR);
    }
    input->seek(pos+16, librevenge::RVNG_SEEK_SET);
    if (isList) {
      while (input->tell()<lastDataPos) {
        long actPos=input->tell();
        if (readZone(level+1, lastDataPos))
          continue;
        MWAW_DEBUG_MSG(("PowerPoint7Parser::readZone: can not read some data\n"));
        libmwaw::DebugStream f1;
        f1 <<  header.getName() << ":###extra";
        ascii().addPos(actPos);
        ascii().addNote(f1.str().c_str());
        break;
      }
    }
  }
  if (header.m_dataSize && !isList)
    ascii().addDelimiter(input->tell(),'|');
  input->seek(lastDataPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

void PowerPoint7Parser::checkForUnparsedZones()
{
  // check if there remains some unparsed zone
}

////////////////////////////////////////////////////////////
// try to read the different zones
////////////////////////////////////////////////////////////
bool PowerPoint7Parser::parseTextContent(MWAWInputStreamPtr input)
{
  if (!input) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::parseTextContent: can not find the input\n"));
    return false;
  }
  libmwaw::DebugFile ascFile(input);
  ascFile.open("Text_Content");
  libmwaw::DebugStream f;
  f << "Entries(TextContent):";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  long pos=0;
  while (!input->isEnd()) {
    auto c=char(input->readULong(1));
    if (c==0) {
      input->seek(-1, librevenge::RVNG_SEEK_CUR);
      break;
    }
    f << c;
    if (c==0xd) {
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      pos=input->tell();
      f.str("");
      f << "TextContent:";
    }
  }
  if (!input->isEnd()) {
    ascFile.addPos(input->tell());
    ascFile.addNote("TextContent:#");
  }
  return true;
}

////////////////////////////////////////////////////////////
// try to send data
////////////////////////////////////////////////////////////
void PowerPoint7Parser::sendSlides()
{
  MWAWPresentationListenerPtr listener=getPresentationListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::sendSlides: can not find the listener\n"));
    return;
  }
  for (auto mIt : m_state->m_masterSlideIdToNameMap) {
    if (!mIt.first.isValid()) continue;
    MWAWPageSpan ps(getPageSpan());
    ps.setMasterPageName(mIt.second);
    if (!listener->openMasterPage(ps)) {
      MWAW_DEBUG_MSG(("PowerPoint7Parser::sendSlides: can not create the master page\n"));
    }
    else {
      m_state->m_graphParser->sendSlide(mIt.first,true);
      listener->closeMasterPage();
    }
  }
  bool firstSlideSent=false;
  for (auto const &id : m_state->m_slideList) {
    if (!id.isValid()) continue;
    if (firstSlideSent)
      listener->insertBreak(MWAWListener::PageBreak);
    firstSlideSent=true;
    bool sendBackground=false;
    if (m_state->m_idToSlideInfoMap.find(id)!=m_state->m_idToSlideInfoMap.end())
      sendBackground=m_state->m_idToSlideInfoMap.find(id)->second.m_hasBackground;
    m_state->m_graphParser->sendSlide(id,sendBackground);
  }
}


////////////////////////////////////////////////////////////
// Low level
////////////////////////////////////////////////////////////

// read the header
bool PowerPoint7Parser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->isStructured())
    return false;
  /* a PPT95 containing a PPT97. Let suppose that a PPT97 filter
     exists...*/
  if (input->getSubStreamByName("PP97_DUALSTORAGE/PowerPoint Document")) {
#ifndef DEBUG
    MWAW_DEBUG_MSG(("PowerPoint7Parser::checkHeader: this file is a dual PowerPoint 95 and 97, it will only be converted in debug mode\n"));
#endif
    return false;
  }
  input=input->getSubStreamByName("PowerPoint Document");
  if (!input || !getInput()->getSubStreamByName("PersistentStorage Directory"))
    return false;
  auto endPos=long(input->size());
  input->setReadInverted(true);
  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (endPos<116 || input->readULong(2)!=3) {
    MWAW_DEBUG_MSG(("PowerPoint7Parser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(8, librevenge::RVNG_SEEK_SET);
  auto dSz=long(input->readULong(4));
  if (dSz<100 || endPos-16<dSz || 16+dSz>endPos) return false;

  setVersion(7);
  if (header)
    header->reset(MWAWDocument::MWAW_T_POWERPOINT, 7, MWAWDocument::MWAW_K_PRESENTATION);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
