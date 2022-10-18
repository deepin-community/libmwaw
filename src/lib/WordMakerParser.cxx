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

#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

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
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "WordMakerParser.hxx"

/** Internal: the structures of a WordMakerParser */
namespace WordMakerParserInternal
{
//! Internal: small structure used to store a zone of a WordMakerParser
struct Zone {
  //! the zone type
  enum Type { Z_MAIN, Z_HEADER, Z_FOOTER };
  //! constructor
  explicit Zone(Type type)
    : m_type(type)
    , m_id(0)
    , m_hasTitlePage(false)
    , m_beginPos(0)

    , m_numCharacter(0)
    , m_numParagraph(0)
    , m_numPicture(0)
  {
  }
  //! small function to know if a zone is empty
  bool empty() const
  {
    return m_numCharacter<=0 && m_numParagraph<=1 && m_numPicture<=0;
  }
  //! the zone type
  Type m_type;
  //! the zone id
  int m_id;
  //! true if the document has a title page
  bool m_hasTitlePage;
  //! the zone beginning in the file
  long m_beginPos;
  //! the number of character
  long m_numCharacter;
  //! the number of paragraph
  int m_numParagraph;
  //! the number of picture
  int m_numPicture;
};

////////////////////////////////////////
//! Internal: the state of a WordMakerParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)

    , m_endDataZone(0)
    , m_currentZone(nullptr)

    , m_typeToZoneMap()
    , m_pictureList()
  {
  }
  State(State const &) = delete;
  State &operator=(State const &) = default;

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  //! the end data zone limit
  long m_endDataZone;
  //! the current zone
  Zone *m_currentZone;
  //! a map type to zone
  std::multimap<Zone::Type, Zone> m_typeToZoneMap;
  //! the list of document picture entries
  std::vector<MWAWEntry> m_pictureList;
};

////////////////////////////////////////
//! Internal: the subdocument of a WordMakerParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(WordMakerParser &pars, MWAWInputStreamPtr const &input, Zone const &zone)
    : MWAWSubDocument(&pars, input, MWAWEntry()), m_zone(zone)
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
    if (m_zone.m_beginPos != sDoc->m_zone.m_beginPos) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the zone
  Zone m_zone;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("WordMakerParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<WordMakerParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("WordMakerParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendZone(m_zone);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
WordMakerParser::WordMakerParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  setAsciiName("main-1");

  m_state.reset(new WordMakerParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

WordMakerParser::~WordMakerParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void WordMakerParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());

    checkHeader(nullptr);
    ok = createZones();
    ascii().addPos(getInput()->tell());
    ascii().addNote("_");
    if (ok) {
      createDocument(docInterface);
      for (auto const &entry : m_state->m_pictureList)
        sendPicture(entry);
      for (auto const &it : m_state->m_typeToZoneMap) {
        if (it.first==WordMakerParserInternal::Zone::Z_MAIN)
          sendZone(it.second);
      }
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("WordMakerParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void WordMakerParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("WordMakerParser::createDocument: listener already exist\n"));
    return;
  }

  // first parse the zone to look for header/footer
  MWAWPageSpan ps(getPageSpan());
  bool hasTitlePage=false, hasHF=false;
  for (auto const &it : m_state->m_typeToZoneMap) {
    if (it.first==WordMakerParserInternal::Zone::Z_MAIN || it.second.empty())
      continue;
    if (it.second.m_hasTitlePage)
      hasTitlePage=true;
    hasHF=true;
    MWAWHeaderFooter hf(it.first==WordMakerParserInternal::Zone::Z_FOOTER ?
                        MWAWHeaderFooter::FOOTER : MWAWHeaderFooter::HEADER,
                        it.second.m_id==1 ? MWAWHeaderFooter::ODD :
                        it.second.m_id==2 ? MWAWHeaderFooter::EVEN : MWAWHeaderFooter::ALL);
    hf.m_subDocument.reset(new WordMakerParserInternal::SubDocument(*this, getInput(),it.second));
    ps.setHeaderFooter(hf);
  }

  // create the page list
  std::vector<MWAWPageSpan> pageList;
  if (hasTitlePage && hasHF) {
    MWAWPageSpan title(getPageSpan());
    title.setPageSpan(1);
    pageList.push_back(title);
  }
  ps.setPageSpan(m_state->m_numPages+(hasTitlePage && hasHF ? 0 : 1));
  pageList.push_back(ps);

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
bool WordMakerParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  input->seek(4,librevenge::RVNG_SEEK_SET);
  long len=long(input->readLong(4));
  long endPos=8+len;
  m_state->m_endDataZone=endPos;
  if (len<20 || !input->checkPosition(endPos) || input->readULong(4) != 0x574f5231) {
    MWAW_DEBUG_MSG(("WordMakerParser::createZone: can not read the data size\n"));
    return false;
  }

  libmwaw::DebugStream f;
  m_state->m_numPages=1;
  while (input->tell()+8<endPos) {
    long pos=input->tell();
    f.str("");
    std::string what;
    for (int i=0; i<4; ++i)
      what+=char(input->readULong(1));
    f << "Entries(" << what << "):";
    long dataLen=long(input->readLong(4));
    if (dataLen<0 || pos+8+dataLen<pos+8 || pos+8+dataLen>endPos) {
      MWAW_DEBUG_MSG(("WordMakerParser::createZone: can not retrieve a zone header\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (what=="COLR") {
      if (dataLen!=8) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: the color length seems bad\n"));
        f << "###";
      }
      else {
        for (int i=0; i<8; ++i) {
          int val=int(input->readLong(1));
          if (val!=i)
            f << "f" << i << "=" << val << ",";
        }
      }
    }
    else if (what=="DOC ") {
      if (dataLen!=8) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: the document length seems bad\n"));
        f << "###";
      }
      else {
        WordMakerParserInternal::Zone zone(WordMakerParserInternal::Zone::Z_MAIN);
        for (int i=0; i<4; ++i) { // f2=0|256
          int val=int(input->readLong(2));
          int const expected[]= {1,2,0,0};
          if (val!=expected[i])
            f << "f" << i << "=" << val << ",";
        }
        zone.m_beginPos=input->tell();
        if (m_state->m_typeToZoneMap.find(WordMakerParserInternal::Zone::Z_MAIN)!=
            m_state->m_typeToZoneMap.end()) {
          MWAW_DEBUG_MSG(("WordMakerParser::createZone: arghhs, find multiple main zone\n"));
          f << "###";
          ascii().addPos(pos);
          ascii().addNote(f.str().c_str());
          throw libmwaw::ParseException();
        }
        auto zIt=m_state->m_typeToZoneMap.insert(std::make_pair(zone.m_type, zone));
        m_state->m_currentZone=&zIt->second;
      }
    }
    else if (what=="DPIC") {
      if (m_state->m_currentZone)
        ++m_state->m_currentZone->m_numPicture;
      // the picture will be read by sendPicture
      MWAWEntry picture;
      picture.setBegin(pos+8);
      picture.setLength(dataLen);
      m_state->m_pictureList.push_back(picture);
    }
    else if (what=="FONT") {
      if (!readFontNames(dataLen))
        f << "###";
    }
    else if (what=="FOOT" || what=="HEAD") {
      if (dataLen!=6) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: the footer/header length seems too short\n"));
        f << "###";
      }
      else {
        WordMakerParserInternal::Zone zone(what=="FOOT" ? WordMakerParserInternal::Zone::Z_FOOTER :
                                           WordMakerParserInternal::Zone::Z_HEADER);
        for (int i=0; i<6; ++i) {
          int val=int(input->readLong(1));
          if (i==0)
            zone.m_id=val;
          int const expected[]= {0, 1, 0/*or 1*/, 0, 0, 0};
          if (val==expected[i])
            continue;
          switch (i) {
          case 0:
            if (val==1)
              f << "odd,";
            else if (val==2)
              f << "even,";
            else
              f << "type=" << val << ",";
            break;
          case 1:
            if (val==0) {
              zone.m_hasTitlePage=true;
              f << "hasTitle[page],";
            }
            else
              f << "hasTitle[page]=" << val << ",";
            break;
          default:
            f << "f" << i << "=" << val << ",";
            break;
          }
        }
        zone.m_beginPos=input->tell();
        auto zIt=m_state->m_typeToZoneMap.insert(std::make_pair(zone.m_type, zone));
        m_state->m_currentZone=&zIt->second;
      }
    }
    else if (what=="PAGE") {
      ++m_state->m_numPages;
      if (m_state->m_currentZone)
        ++m_state->m_currentZone->m_numParagraph;
      if (dataLen!=0) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: the page length seems bad\n"));
        f << "###";
      }
    }
    else if (what=="PARA") // will be parsed when we send the zone
      ;
    else if (what=="PREC") {
      if (!readPrintInfo(dataLen))
        f << "###";
    }
    else if (what=="STYL") // follow TEXT, will be parsed when we send the zone
      ;
    else if (what=="TABS") // will be parsed when we send the zone
      ;
    else if (what=="TEXT") {
      std::string text;
      for (int i=0; i<dataLen; ++i) {
        char c=char(input->readLong(1));
        if (c)
          text+=c;
        else if (i+1!=dataLen)
          text+="##[0]";
      }
      f << text << ",";
      if (m_state->m_currentZone) {
        m_state->m_currentZone->m_numCharacter+=dataLen;
        ++m_state->m_currentZone->m_numParagraph;
      }
    }
    else if (what=="WIND") {
      if (dataLen!=8) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: the windows length seems too short\n"));
        f << "###";
      }
      else {
        int dim[4];
        for (auto &d: dim) d=int(input->readLong(2));
        f << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
      }
    }
    else {
      if (!what.empty()) {
        MWAW_DEBUG_MSG(("WordMakerParser::createZone: unexpected tags=%s\n", what.c_str()));
      }
      f << "###";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+8+dataLen+(dataLen%2), librevenge::RVNG_SEEK_SET);
  }
  input->seek(endPos,librevenge::RVNG_SEEK_SET);
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("WordMakerParser::createZone: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Extra):###");
  }
  return m_state->m_typeToZoneMap.find(WordMakerParserInternal::Zone::Z_MAIN)!=
         m_state->m_typeToZoneMap.end();
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool WordMakerParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = WordMakerParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  if (!input->checkPosition(12)) {
    MWAW_DEBUG_MSG(("WordMakerParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(4) != 0x464f524d)
    return false;
  long len=long(input->readLong(4));
  if (len<20 || !input->checkPosition(8+len)) {
    MWAW_DEBUG_MSG(("WordMakerParser::checkHeader: can not read the data size\n"));
    return false;
  }
  if (input->readULong(4) != 0x574f5231) {
    MWAW_DEBUG_MSG(("WordMakerParser::checkHeader: can not find the first type\n"));
    return false;
  }
  ascii().addPos(0);
  ascii().addNote("FileHeader");

  if (header)
    header->reset(MWAWDocument::MWAW_T_WORDMAKER, 1);

  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool WordMakerParser::readPrintInfo(long len)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (len<0x78 || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readPrintInfo: the entry seems too short\n"));
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
      paperSize.x() <= 0 || paperSize.y() <= 0) return false;

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

  if (input->tell()!=pos+len)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(pos+len, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read the style
////////////////////////////////////////////////////////////
bool WordMakerParser::readFontNames(long len)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (len<3 || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readFontNames: the entry seems too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "FONT:";
  int id=int(input->readULong(2));
  f << "id=" << id << ",";
  std::string name;
  for (int i=2; i<len; ++i) {
    char c=char(input->readLong(1));
    if (c==0)
      break;
    name+=c;
  }
  f << name << ",";
  if (!name.empty())
    getFontConverter()->setCorrespondance(id, name);
  ascii().addPos(pos-8);
  ascii().addNote(f.str().c_str());

  return true;
}

bool WordMakerParser::readFont(long len, MWAWFont &font)
{
  font=MWAWFont();
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (len!=8 || !input->checkPosition(pos+8)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readFont: the entry seems too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "STYL:";

  font.setId(int(input->readULong(2)));
  font.setSize(float(input->readLong(1)));
  uint32_t flags=0;
  int val=int(input->readULong(1));
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  if (val&0xe0) f << "fl=" << std::hex << (val&0xe0) << std::dec << ",";
  font.setFlags(flags);
  val=int(input->readULong(1));
  switch (val&3) {
  case 0:
    break;
  case 1:
    font.set(MWAWFont::Script::super100());
    break;
  case 2:
    font.set(MWAWFont::Script::sub100());
    break;
  default:
    MWAW_DEBUG_MSG(("WordMakerParser::readFont: unknown script\n"));
    f << "##script3,";
    break;
  }
  if (val&0xfc) f << "fl2=" << std::hex << (val&0xfc) << std::dec << ",";
  val=int(input->readULong(1));
  if (val>0 && val<7) {
    uint32_t const colors[]= {0,0xff0000,0xffff00,0xff00,0xffff, 0xff, 0xff00ff};
    font.setColor(MWAWColor(colors[val]));
  }
  else if (val) {
    MWAW_DEBUG_MSG(("WordMakerParser::readFont: unknown color\n"));
    f << "##color=" << val << ",";
  }
  f << "font=[" << font.getDebugString(getFontConverter()) << "],";
  val=int(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool WordMakerParser::readParagraph(long len, MWAWParagraph &para, MWAWFont &font)
{
  font=MWAWFont();
  std::vector<MWAWTabStop> oldTabs; // do not modify the tabulations
  std::swap(*para.m_tabs,oldTabs);
  para=MWAWParagraph();
  std::swap(*para.m_tabs,oldTabs);

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (len!=18 || !input->checkPosition(pos+18)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readParagraph: the entry seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "PARA:";
  para.m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<3; ++i) // first, left, right
    para.m_margins[i]=double(input->readLong(2))/20;
  *(para.m_margins[0])-=para.m_margins[1].get();
  int val=int(input->readLong(1));
  switch (val) {
  case 0:
    break;
  case 8:
  case 0x10:
    para.setInterline(1+double(val)/16, librevenge::RVNG_PERCENT);
    break;
  default:
    MWAW_DEBUG_MSG(("WordMakerParser::readParagraph: unknow interline\n"));
    f << "###interline=" << val << ",";
    break;
  }
  val=int(input->readULong(1));
  switch (val & 3) {
  case 1:
    para.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationRight;
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    break;
  case 0: // left
  default:
    break;
  }
  if (val&0xfc) f << "fl=" << std::hex << (val&0xfc) << std::dec << ",";
  val=int(input->readULong(1));
  if (val)
    para.setInterline(val, librevenge::RVNG_POINT);
  val=int(input->readULong(1));
  if (val&1)
    para.m_spacings[1]=12/72.;
  if (val&0x10)
    para.m_spacings[2]=12/72.;
  if (val&0xee) f << "fl1=" << std::hex << (val&0xee) << std::dec << ",";

  f << "para=[" << para << "],";

  readFont(8, font);
  ascii().addPos(pos-8);
  ascii().addNote(f.str().c_str());

  return true;
}

bool WordMakerParser::readTabulations(long len, MWAWParagraph &para)
{
  para.m_tabs->resize(0);

  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if ((len%4)!=0 || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readTabulations: the entry seems bad\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "TABS:";

  int N=int(len/4);
  f << "tabs=[";
  for (int i=0; i<N; ++i) {
    MWAWTabStop newTab;
    newTab.m_position=double(input->readLong(2))/20/72;
    int val=int(input->readULong(1));
    switch (val&3) {
    case 0:
      break;
    case 1:
      newTab.m_alignment = MWAWTabStop::CENTER;
      break;
    case 2:
      newTab.m_alignment = MWAWTabStop::RIGHT;
      break;
    case 3:
      newTab.m_alignment = MWAWTabStop::DECIMAL;
      break;
    default:
      break;
    }
    f << "[" << newTab << ",";
    if (val&0xfc) f << "fl=" << std::hex << (val&0xfc) << ",";
    val=int(input->readLong(1)); // 0
    if (val) f << "f0=" << val << ",";
    f << "],";
    para.m_tabs->push_back(newTab);
  }
  f << "],";

  ascii().addPos(pos-8);
  ascii().addNote(f.str().c_str());

  return true;
}

bool WordMakerParser::readPicture(long len, MWAWEmbeddedObject &object, MWAWBox2f &bdbox, int &page)
{
  object=MWAWEmbeddedObject();
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (len<=10 || !input->checkPosition(pos+len)) {
    MWAW_DEBUG_MSG(("WordMakerParser::readPicture: the zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "DPIC:";

  int dim[2];
  for (auto &d: dim) d=int(input->readLong(2));
  page=int(input->readLong(2));
  if (page) f << "page=" << page << ",";
  int pPos[2];
  for (auto &d: pPos) d=int(input->readLong(2));
  MWAWVec2f orig=MWAWVec2f(float(pPos[0])/10, float(pPos[1])/10);
  bdbox=MWAWBox2f(orig, orig+MWAWVec2f(float(dim[0]), float(dim[1])));
  f << "box=" << bdbox << ",";
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(len-10)));
  if (pict && pict->getBinary(object) && !object.m_dataList.empty()) {
#ifdef DEBUG_WITH_FILES
    static int volatile pictName = 0;
    libmwaw::DebugStream f2;
    f2 << "PICT-" << ++pictName;
    libmwaw::Debug::dumpFile(object.m_dataList[0], f2.str().c_str());
    ascii().skipZone(pos+10, pos+len-1);
#endif
  }
  else {
    MWAW_DEBUG_MSG(("WordMakerParser::readPicture: can not retrieve a object\n"));
    f << "###";
  }

  ascii().addPos(pos-8);
  ascii().addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
void WordMakerParser::newPage()
{
  if (m_state->m_actPage >= m_state->m_numPages)
    return;

  m_state->m_actPage++;
  if (!getTextListener())
    return;
  getTextListener()->insertBreak(MWAWTextListener::PageBreak);
}

bool WordMakerParser::sendPicture(MWAWEntry const &entry)
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WordMakerParser::sendPicture: can not find the text listener\n"));
    return false;
  }
  if (!entry.valid() || entry.length()<=20 || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("WordMakerParser::sendPicture: can not find the picture zone\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  MWAWEmbeddedObject object;
  MWAWBox2f bdbox;
  int objPage;
  if (!readPicture(entry.length(),object,bdbox,objPage))
    return false;
  MWAWPosition pictPos(bdbox[0], bdbox.size(), librevenge::RVNG_POINT);
  pictPos.setPage(objPage+1);
  pictPos.setRelativePosition(MWAWPosition::Page);
  pictPos.m_wrapping = MWAWPosition::WBackground;
  listener->insertPicture(pictPos, object);
  return true;
}

bool WordMakerParser::sendZone(WordMakerParserInternal::Zone const &zone)
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("WordMakerParser::sendZone: can not find the text listener\n"));
    return false;
  }
  long endPos=m_state->m_endDataZone;
  if (zone.m_beginPos<=20 || !input->checkPosition(zone.m_beginPos+8) || zone.m_beginPos+8>endPos) {
    MWAW_DEBUG_MSG(("WordMakerParser::sendZone: can not find the text zone\n"));
    return false;
  }
  input->seek(zone.m_beginPos, librevenge::RVNG_SEEK_SET);

  MWAWParagraph para;
  MWAWFont paraFont;
  listener->setParagraph(para);
  while (input->tell()+8<endPos) {
    long pos=input->tell();
    std::string what;
    for (int i=0; i<4; ++i)
      what+=char(input->readULong(1));
    long dataLen=long(input->readLong(4));
    if (dataLen<0 || pos+8+dataLen<pos+8 || pos+8+dataLen>endPos)
      return false;
    if (what=="DOC " || what=="FOOT" || what=="HEAD") // begin of the next zone
      return true;
    else if (what=="PAGE")
      newPage();
    else if (what=="PARA") {
      if (readParagraph(dataLen, para, paraFont))
        listener->setParagraph(para);
    }
    else if (what=="TABS") {
      if (readTabulations(dataLen, para))
        listener->setParagraph(para);
    }
    else if (what=="TEXT") {
      // first look for the following style zone
      long nextPos=pos+8+dataLen+(dataLen%2);
      std::map<int,MWAWFont> posToFont;
      if (nextPos+8<endPos) {
        input->seek(nextPos, librevenge::RVNG_SEEK_SET);
        std::string what2;
        for (int i=0; i<4; ++i)
          what2+=char(input->readULong(1));
        long dataLen2=long(input->readLong(4));
        if (what2=="STYL" && nextPos+8+dataLen2<=endPos) {
          if ((dataLen2%10)!=0) {
            MWAW_DEBUG_MSG(("WordMakerParser::sendZone: the style length seems bad\n"));
            ascii().addPos(nextPos);
            ascii().addNote("###");
          }
          else {
            int N=int(dataLen2/10);
            libmwaw::DebugStream f;
            for (int i=0; i<N; ++i) {
              f << "STYL-" << i << ":";
              long pos2=input->tell();
              int cPos=int(input->readULong(2));
              if (cPos) f << "pos=" << cPos << ",";
              MWAWFont font;
              readFont(8, font);
              posToFont[cPos]=font;
              input->seek(pos2+10, librevenge::RVNG_SEEK_SET);
              ascii().addPos(pos2);
              ascii().addNote(f.str().c_str());
            }
          }
        }
      }
      // now read the text
      input->seek(pos+8, librevenge::RVNG_SEEK_SET);
      listener->setFont(paraFont);
      for (int i=0; i<dataLen; ++i) {
        auto it=posToFont.find(i);
        if (it!=posToFont.end())
          listener->setFont(it->second);

        unsigned char c=(unsigned char)(input->readULong(1));
        switch (c) {
        case 0x4:
          listener->insertField(MWAWField(MWAWField::PageNumber));
          break;
        case 0x5: {
          MWAWField date(MWAWField::Date);
          date.m_DTFormat = "%a, %b %d, %Y";
          listener->insertField(date);
          break;
        }
        case 0x6: {
          MWAWField time(MWAWField::Time);
          time.m_DTFormat="%H:%M";
          listener->insertField(time);
          break;
        }
        case 0x9:
          listener->insertTab();
          break;
        default:
          if (c<0x1f) {
            MWAW_DEBUG_MSG(("WordMakerParser::sendZone: find unknown char=%d\n", int(c)));
            break;
          }
          listener->insertCharacter(c);
        }
      }
      listener->insertEOL();
    }
    input->seek(pos+8+dataLen+(dataLen%2), librevenge::RVNG_SEEK_SET);
  }
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
