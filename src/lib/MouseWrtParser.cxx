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

#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "MouseWrtParser.hxx"

/** Internal: the structures of a MouseWrtParser */
namespace MouseWrtParserInternal
{
////////////////////////////////////////
//! Internal: class to store zone information of a MouseWrtParser
struct Zone {
  //! constructor
  Zone()
    : m_font()
    , m_writingHebrew(false)
    , m_text()
  {
  }
  //! the font
  MWAWFont m_font;
  //! flag to know if the writing is reverted
  bool m_writingHebrew;
  //! the text entry
  MWAWEntry m_text;
};

////////////////////////////////////////
//! Internal: class to store paragraph information of a MouseWrtParser
struct Paragraph {
  //! constructor
  explicit Paragraph(int id=0)
    : m_id(id)
    , m_paragraph()
    , m_picture(false)
  {
  }
  //! the paragraph id
  int m_id;
  //! the paragraph
  MWAWParagraph m_paragraph;
  //! flag to know if this is a picture
  bool m_picture;
};
////////////////////////////////////////
//! Internal: the state of a MouseWrtParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(0)
    , m_charPLCMap()
    , m_paraPLCMap()
    , m_text()
  {
    for (auto &size : m_blockSizes) size=0;
  }

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;
  /** the first zone's size */
  long m_blockSizes[5];
  /** the map position to charPLC */
  std::map<int, MWAWFont> m_charPLCMap;
  /** the map position to paraPLC */
  std::map<int, Paragraph> m_paraPLCMap;
  /** the main text entry */
  MWAWEntry m_text;
  /** the header and the footer zone */
  Zone m_zones[2];
};

////////////////////////////////////////
//! Internal: the subdocument of a MouseWrtParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(MouseWrtParser &pars, MWAWInputStreamPtr const &input, int zoneId)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_id(zoneId)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the subdocument id
  int m_id;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("MouseWrtParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<MouseWrtParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("MouseWrtParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  parser->sendZone(m_id);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_id != sDoc->m_id) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MouseWrtParser::MouseWrtParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state(new MouseWrtParserInternal::State)
{
  setAsciiName("main-1");
}

MouseWrtParser::~MouseWrtParser()
{
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void MouseWrtParser::newPage(int number)
{
  if (number <= m_state->m_actPage || number > m_state->m_numPages)
    return;

  while (m_state->m_actPage < number) {
    m_state->m_actPage++;
    if (!getTextListener() || m_state->m_actPage == 1)
      continue;
    getTextListener()->insertBreak(MWAWTextListener::PageBreak);
  }
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MouseWrtParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok=createZones();
    if (ok) {
      createDocument(docInterface);
      sendMainZone();
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MouseWrtParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MouseWrtParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("MouseWrtParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  m_state->m_numPages=computeNumPages();
  ps.setPageSpan(m_state->m_numPages);
  for (int i=0; i<2; ++i) {
    if (!m_state->m_zones[i].m_text.valid()) continue;
    MWAWHeaderFooter hF(i==0 ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    hF.m_subDocument.reset(new MouseWrtParserInternal::SubDocument(*this, getInput(), i));
    ps.setHeaderFooter(hF);
  }
  std::vector<MWAWPageSpan> pageList;
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
bool MouseWrtParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  for (int i=0; i<5; ++i) {
    if (!m_state->m_blockSizes[i]) continue;
    long pos = input->tell();
    if (m_state->m_blockSizes[i]<0 || !input->checkPosition(pos+m_state->m_blockSizes[i])) {
      MWAW_DEBUG_MSG(("MouseWrtParser::createZones: the block sizes are wrong\n"));
      return false;
    }
    if (i==1) continue;
    bool done=false;
    switch (i) {
    case 0:
      done=readCharPLCs(m_state->m_blockSizes[i]);
      break;
    case 2:
      done=readParagraphPLCs(m_state->m_blockSizes[i]);
      break;
    case 3:
      done=m_state->m_blockSizes[i]>=120 && readPrintInfo();
      break;
    case 4:
      done=readDocumentInfo(m_state->m_blockSizes[i]);
      break;
    default:
      break;
    }
    if (done) {
      if (input->tell()!=pos+m_state->m_blockSizes[i])
        ascii().addDelimiter(input->tell(),'|');
    }
    else {
      f.str("");
      f << "Entries(Zone" << i << "):";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    input->seek(pos+m_state->m_blockSizes[i], librevenge::RVNG_SEEK_SET);
  }
  m_state->m_text.setBegin(input->tell());
  m_state->m_text.setLength(m_state->m_blockSizes[1]);
  if (m_state->m_blockSizes[1]<0 || !input->checkPosition(m_state->m_text.end())) {
    MWAW_DEBUG_MSG(("MouseWrtParser::createZones: can not find the text zone\n"));
    return false;
  }
  if (!input->isEnd()) {
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Unknown):");
  }
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
bool MouseWrtParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = MouseWrtParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  int headerSize=30;
  if (!input->checkPosition(headerSize)) {
    MWAW_DEBUG_MSG(("MouseWrtParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  if (input->readULong(4)!=0x4474d30 || input->readULong(2)!=0x3400)
    return false;
  long totalSize=m_state->m_blockSizes[0];
  for (int i=0; i<5; ++i) {
    m_state->m_blockSizes[i]=long(input->readLong(4));
    if (m_state->m_blockSizes[i]<0) return false;
    char const *wh[]= {"charPlc","text","paraPLC","printer","zone4"};
    f << wh[i] << "[sz]=" << m_state->m_blockSizes[i] << ",";
    totalSize+=m_state->m_blockSizes[i];
  }
  if (totalSize<0 || !input->checkPosition(30+totalSize)) return false;
  if (strict && ((m_state->m_blockSizes[0]%8)!=0 || (m_state->m_blockSizes[2]%38)!=0 ||
                 (m_state->m_blockSizes[3] && m_state->m_blockSizes[3]<120) ||
                 (m_state->m_blockSizes[4] && m_state->m_blockSizes[4]<76)))
    return false;
  // probably a size, maybe pict size
  auto dSz=long(input->readLong(4));
  if (dSz) {
    MWAW_DEBUG_MSG(("MouseWrtParser::checkHeader: find some extra size?\n"));
    f << "##f0=" << dSz << ",";
  }
  // ok, we can finish initialization
  if (header)
    header->reset(MWAWDocument::MWAW_T_MOUSEWRITE, 1);
  input->seek(headerSize,librevenge::RVNG_SEEK_SET);

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  ascii().addPos(headerSize);
  return true;
}

////////////////////////////////////////////////////////////
// read the basic structure
////////////////////////////////////////////////////////////
bool MouseWrtParser::readCharPLCs(long sz)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (sz<0 || (sz%8)!=0 || !input->checkPosition(pos+sz)) {
    MWAW_DEBUG_MSG(("MouseWrtParser::readCharPLCs: find unexpected size length\n"));
    return false;
  }
  long N=sz/8;
  libmwaw::DebugStream f;
  f << "Entries(CharPLC):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (long i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "CharPLC-C" << i << ":";
    int cPos;
    MWAWFont font;
    if (i+1!=N && readFont(font, cPos)) {
      f << "cPos=" << cPos << "," << font.getDebugString(getParserState()->m_fontConverter);
      m_state->m_charPLCMap[cPos]=font;
    }
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool MouseWrtParser::readFont(MWAWFont &font, int &cPos)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(pos+8)) return false;
  cPos=static_cast<int>(input->readULong(2));
  libmwaw::DebugStream f;
  font=MWAWFont();
  auto val=int(input->readULong(1));
  if (val) f << "f0=" << val << ",";
  font.setSize(float(input->readULong(1)));
  auto flag=static_cast<int>(input->readULong(1));
  uint32_t flags=0;
  if (flag&0x1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x8) flags |= MWAWFont::embossBit;
  if (flag&0x10) flags |= MWAWFont::shadowBit;
  if (flag&0x60) f << "#flag[hi]=" << std::hex << (flag&0x60) << std::dec << ",";
  font.setFlags(flags);
  val=static_cast<int>(input->readULong(1)); // 0|7d
  if (val) f << "f1=" << val << ",";
  font.setId(static_cast<int>(input->readULong(2)));
  font.m_extra=f.str().c_str();
  return true;
}

bool MouseWrtParser::readParagraphPLCs(long sz)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (sz<0 || (sz%38)!=0 || !input->checkPosition(pos+sz)) {
    MWAW_DEBUG_MSG(("MouseWrtParser::readParagraphPLCs: find unexpected size length\n"));
    return false;
  }
  auto N=int(sz/38);
  libmwaw::DebugStream f;
  f << "Entries(ParaPLC):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "ParaPLC-P" << i << ":";
    if (i+1==N) { // last is sometimes random
      input->seek(pos+36, librevenge::RVNG_SEEK_SET);
      auto cPos=static_cast<int>(input->readULong(2));
      f << "cPos=" << cPos << ",";
      input->seek(pos+38, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      break;
    }
    MouseWrtParserInternal::Paragraph para(i);
    para.m_paragraph.m_marginsUnit=librevenge::RVNG_POINT;
    // note: right margins is defined from left, so must be corrected when we know the page length
    para.m_paragraph.m_margins[1] = double(input->readLong(2));
    para.m_paragraph.m_margins[2] = double(input->readLong(2));
    auto val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0xf: // left
      break;
    case 0x10:
      para.m_paragraph.m_justify = MWAWParagraph::JustificationFull;
      break;
    case 0x11:
      para.m_paragraph.m_justify = MWAWParagraph::JustificationRight;
      break;
    case 0x12:
      para.m_paragraph.m_justify = MWAWParagraph::JustificationCenter;
      break;
    case 0x13:
      f << "justify=rowCol,";
      break;
    case 0x14:
      f << "justify=col,";
      break;
    default:
      if (!val) break;
      MWAW_DEBUG_MSG(("MouseWrtParser::readParagraphPLCs: unknown justify\n"));
      f << "#justify=" << std::hex << val << std::dec << ",";
    }
    for (int j=0; j<2; ++j) { // fl0=1|7, maybe related to tabs definition
      val=static_cast<int>(input->readULong(1)); // 1|7
      if (val) f << "fl" << j << "=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0xb:
      break;
    case 0xc:
      para.m_paragraph.setInterline(1.1, librevenge::RVNG_PERCENT);
      break;
    case 0xd:
      para.m_paragraph.setInterline(1.5, librevenge::RVNG_PERCENT);
      break;
    case 0xe:
      para.m_paragraph.setInterline(2, librevenge::RVNG_PERCENT);
      break;
    default:
      MWAW_DEBUG_MSG(("MouseWrtParser::readParagraphPLCs: unknown interline\n"));
      f << "#interline=" << val << ",";
      break;
    }
    val=static_cast<int>(input->readULong(1)); // always 0
    if (val) f << "fl2=" << val << ",";
    val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0:
      break;
    case 1:
      para.m_picture=true;
      f << "picture,";
      break;
    default:
      MWAW_DEBUG_MSG(("MouseWrtParser::readParagraphPLCs: unknown picture def\n"));
      f << "#picture=" << val << ",";
      break;
    }
    int lastTabPos=0; // check that the tabulation are in increasing order
    for (int j=0; j<10; ++j) {
      val=static_cast<int>(input->readLong(2));
      if (!val || val<=lastTabPos) break;
      MWAWTabStop tab;
      tab.m_alignment=MWAWTabStop::CENTER;
      tab.m_position=double(val)/72.;
      para.m_paragraph.m_tabs->push_back(tab);
      lastTabPos=val;
    }
    input->seek(pos+30, librevenge::RVNG_SEEK_SET);
    val=static_cast<int>(input->readLong(2));
    if (val) f << "act[tab]=" << val << ",";
    val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0:
      para.m_paragraph.m_writingMode=libmwaw::WritingRightTop;
      break;
    case 1: // normal writing
      break;
    default:
      MWAW_DEBUG_MSG(("MouseWrtParser::readParagraphPLCs: unknown writing mode\n"));
      f << "#writing[mode]=" << val << ",";
      break;
    }
    for (int j=0; j<3; ++j) { // always 0?
      val=static_cast<int>(input->readULong(1));
      if (val) f << "flA" << j << "=" << val << ",";
    }
    f << para.m_paragraph;
    auto cPos=static_cast<int>(input->readULong(2));
    f << "cPos=" << cPos << ",";
    m_state->m_paraPLCMap[cPos]=para;
    input->seek(pos+38, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool MouseWrtParser::readDocumentInfo(long sz)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (sz<76 || !input->checkPosition(pos+sz)) {
    MWAW_DEBUG_MSG(("MouseWrtParser::readDocumentInfo: find unexpected size length\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(DocumentInfo):";
  int val;
  f << "unkns=[";
  for (int i=0; i<2; ++i) { // footer,header
    // find 0|18|25, does not seems related to heigth...
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  for (auto &zone : m_state->m_zones) { // header,footer
    val=static_cast<int>(input->readULong(1));
    switch (val) {
    case 0:
      zone.m_writingHebrew=true;
      f << "writing[mode]=rt-lb,";
      break;
    case 1: // normal writing
      break;
    default:
      MWAW_DEBUG_MSG(("MouseWrtParser::readDocumentInfo: unknown writing mode\n"));
      f << "#writing[mode]=" << val << ",";
      break;
    }
  }
  f << "ids=[";
  for (int i=0; i<2; ++i) { // header,footer
    val=static_cast<int>(input->readULong(4));
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  long zoneSize[2];
  for (int i=0; i<2; ++i) { // header,footer
    zoneSize[i]=long(input->readULong(4));
    if (zoneSize[i]) f << "block" << i << "[sz]=" << zoneSize[i] << ",";
  }
  for (int i=0; i<2; ++i) { // header,footer, always 0|-1?
    val=static_cast<int>(input->readLong(4));
    if (val) f << "f" << i+2 << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) { // header,footer
    long actPos=input->tell();
    int cPos;
    if (zoneSize[i]==0 || !readFont(m_state->m_zones[i].m_font, cPos))
      input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
    else
      f << "font" << i << "=[" << m_state->m_zones[i].m_font.getDebugString(getParserState()->m_fontConverter) << "],";
  }
  ascii().addDelimiter(input->tell(),'|');
  if (sz > 76+zoneSize[0]+zoneSize[1] || zoneSize[0]<0 || zoneSize[1]<0) {
    MWAW_DEBUG_MSG(("MouseWrtParser::readDocumentInfo: problem with the zoneSize\n"));
    f << "##zoneSize,";
    input->seek(pos+sz, librevenge::RVNG_SEEK_SET);
  }
  else {
    input->seek(pos+76, librevenge::RVNG_SEEK_SET);
    for (int i=0; i<2; ++i) {
      if (zoneSize[i]<=0) continue;
      m_state->m_zones[i].m_text.setBegin(input->tell());
      m_state->m_zones[i].m_text.setLength(zoneSize[i]);
      input->seek(zoneSize[i], librevenge::RVNG_SEEK_CUR);
    }
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

int MouseWrtParser::computeNumPages()
{
  if (!m_state->m_text.valid()) return 1;
  MWAWInputStreamPtr input = getInput();
  int numPages=1;
  auto pIt=m_state->m_paraPLCMap.begin();
  long const beginPos=m_state->m_text.begin();
  while (pIt!=m_state->m_paraPLCMap.end()) {
    if (pIt->second.m_picture) {
      ++pIt;
      continue;
    }
    long actPos=beginPos+(pIt++)->first;
    long lastPos=(pIt!=m_state->m_paraPLCMap.end()) ? beginPos+pIt->first : m_state->m_text.end();
    if (lastPos>m_state->m_text.end()) {
      MWAW_DEBUG_MSG(("MouseWrtParser::computeNumPages: oops, problem with some plc pos\n"));
      break;
    }
    input->seek(actPos, librevenge::RVNG_SEEK_SET);
    for (long cPos=actPos; cPos<lastPos; ++cPos) {
      if (input->readULong(1)==0xd7)
        ++numPages;
    }
  }
  return numPages;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool MouseWrtParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
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
  MWAWVec2i rBotMargin=info.paper().pos(1) - info.page().pos(1);

  // move margin left | top
  int decalX = lTopMargin.x() > 14 ? lTopMargin.x()-14 : 0;
  int decalY = lTopMargin.y() > 14 ? lTopMargin.y()-14 : 0;
  lTopMargin -= MWAWVec2i(decalX, decalY);
  rBotMargin += MWAWVec2i(decalX, decalY);

  // decrease right | bottom
  int rightMarg = rBotMargin.x() -10;
  if (rightMarg < 0) rightMarg=0;
  int botMarg = rBotMargin.y() -50;
  if (botMarg < 0) botMarg=0;

  getPageSpan().setMarginTop(lTopMargin.y()/72.0);
  getPageSpan().setMarginBottom(botMarg/72.0);
  getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
  getPageSpan().setMarginRight(rightMarg/72.0);
  getPageSpan().setFormLength(paperSize.y()/72.);
  getPageSpan().setFormWidth(paperSize.x()/72.);

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  if (long(input->tell()) != pos+0x78) {
    MWAW_DEBUG_MSG(("MouseWrtParser::readPrintInfo: file is too short\n"));
    return false;
  }
  ascii().addPos(input->tell());

  return true;
}

////////////////////////////////////////////////////////////
// send the data
////////////////////////////////////////////////////////////
bool MouseWrtParser::sendMainZone()
{
  MWAWListenerPtr listener=getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MouseWrtParser::sendMainZone: can not find the listener\n"));
  }
  MWAWInputStreamPtr input = getInput();
  if (!m_state->m_text.valid() || !input->checkPosition(m_state->m_text.end())) {
    listener->insertChar(' ');
    return true;
  }
  long begPos = m_state->m_text.begin(), pos=begPos;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  auto N=static_cast<int>(m_state->m_text.length());
  libmwaw::DebugStream f;
  f << "Entries(Text):";
  double const pageWidth=72. * getPageSpan().getPageWidth();
  int actPage=1;
  newPage(actPage);
  for (int i=0; i<N; ++i) {
    auto pIt=m_state->m_paraPLCMap.find(i);
    if (pIt!=m_state->m_paraPLCMap.end()) {
      if (i!=0) {
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        pos=input->tell();
        f.str("");
        f << "Text:";
      }
      auto const &para=pIt->second;
      f << "[P" << para.m_id << "]";
      // time to send the paragraph, so first update the right margins
      MWAWParagraph paragraph=para.m_paragraph;
      if (*(paragraph.m_margins[2])>pageWidth) {
        f << "#";
        paragraph.m_margins[2]=0;
      }
      else
        paragraph.m_margins[2]=pageWidth-*paragraph.m_margins[2];
      if (para.m_picture) paragraph.m_justify = MWAWParagraph::JustificationRight;
      listener->setParagraph(paragraph);
      if (para.m_picture) {
        f << "[picture],";
        ++pIt;
        long endPos=pIt==m_state->m_paraPLCMap.end() ? m_state->m_text.end() : begPos+pIt->first;
        if (endPos<=input->tell()) { // check that we do not go backward
          f << "###";
          MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: can not compute the end of picture pos, stop!!!\n"));
          break;
        }
        long actPos=input->tell();
        bool ok=endPos-actPos>9;
        if (ok) {
          // look for pict
          auto dSz=static_cast<int>(input->readULong(2));
          if (dSz+9>endPos-actPos || dSz+12<endPos-actPos) {
            f << "#pict?";
            MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: no sure that this is a picture\n"));
            input->seek(endPos-9, librevenge::RVNG_SEEK_SET);
          }
          else
            input->seek(actPos+dSz, librevenge::RVNG_SEEK_SET);
          long pictSz=input->tell()-actPos;
          int dim[4];
          for (auto &d : dim) d=static_cast<int>(input->readLong(2));
          MWAWBox2i box(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
          f << "box=" << box << ",";
          if (box.size()[0]<0 || box.size()[1]<0 || box.size()[0]>2000 || box.size()[1]>2000) {
            MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: the bdbox is bad\n"));
            f << "###";
            ok=false;
          }
          else {
            librevenge::RVNGBinaryData data;
            input->seek(actPos, librevenge::RVNG_SEEK_SET);
            input->readDataBlock(pictSz, data);
            MWAWEmbeddedObject object(data);
            MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(box.size()), librevenge::RVNG_POINT);
            position.m_anchorTo=MWAWPosition::Char;
            listener->insertPicture(position, object);
#ifdef DEBUG_WITH_FILES
            static int volatile pictName = 0;
            libmwaw::DebugStream f2;
            f2 << "Pict-" << ++pictName << ".pct";
            libmwaw::Debug::dumpFile(data, f2.str().c_str());
#endif
            ascii().skipZone(actPos, actPos+pictSz-1);
          }
        }

        if (ok || endPos-actPos>20) {
          listener->insertEOL();
          ascii().addPos(pos);
          ascii().addNote(f.str().c_str());
          pos=input->tell();
          f.str("");
          f << "Text:";
          input->seek(endPos, librevenge::RVNG_SEEK_SET);
          i=int(endPos-begPos-1); // will be endPos-begPos after
          continue;
        }
      }
    }
    auto fIt=m_state->m_charPLCMap.find(i);
    if (fIt!=m_state->m_charPLCMap.end()) {
      listener->setFont(fIt->second);
      f << "[" << fIt->second.getDebugString(getParserState()->m_fontConverter) << "]";
    }
    auto c=static_cast<unsigned char>(input->readULong(1));
    f << c;
    switch (c) {
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      listener->insertEOL();
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      pos=input->tell();
      f.str("");
      f << "Text:";
      break;
    case 0xd7: {
      newPage(++actPage);
      long actPos=input->tell();
      if (i+1!=N && input->readULong(1)==0xd) {
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        pos=input->tell();
        f.str("");
        f << "Text:";
        ++i;
      }
      else
        input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
    default:
      listener->insertCharacter(c);
      break;
    }
  }
  if (input->tell()!=pos) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool MouseWrtParser::sendZone(int zoneId)
{
  if (zoneId < 0 || zoneId >= 2) {
    MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: invalid zone %d\n", zoneId));
    return false;
  }
  MWAWListenerPtr listener=getMainListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: can not find the listener\n"));
    return false;
  }
  auto const &zone=m_state->m_zones[zoneId];
  if (!zone.m_text.valid()) return true;
  if (zone.m_writingHebrew) {
    MWAWParagraph para;
    para.m_writingMode=libmwaw::WritingRightTop;
    listener->setParagraph(para);
  }
  listener->setFont(zone.m_font);
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;
  f << "Entries(Text):" << (zoneId==0 ? "header" : "footer") << ",";
  input->seek(zone.m_text.begin(), librevenge::RVNG_SEEK_SET);
  auto N=static_cast<int>(zone.m_text.length());
  for (long i=0; i<N ; ++i) {
    if (input->isEnd()) {
      MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: oops the text length seems too big\n"));
      f << "###";
      break;
    }
    auto c=static_cast<unsigned char>(input->readULong(1));
    f << c;
    switch (c) {
    case 0x9:
      MWAW_DEBUG_MSG(("MouseWrtParser::sendZone: oops unexpected tab\n"));
      listener->insertChar(' ');
      break;
    case 0xd:
      if (i+1==N)
        break;
      listener->insertEOL();
      break;
    default:
      listener->insertCharacter(c);
      break;
    }
  }
  ascii().addPos(zone.m_text.begin());
  ascii().addNote(f.str().c_str());
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
