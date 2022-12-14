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
#include "MWAWSection.hxx"
#include "MWAWSubDocument.hxx"

#include "MsWrd1Parser.hxx"

/** Internal: the structures of a MsWrd1Parser */
namespace MsWrd1ParserInternal
{
/** different types
 *
 * - FONT: font
 * - RULER: ruler
 * - PAGE: page break
 * - FOOTNOTE: footnote marker
 * - ZONE: unknown(zone4)
 */
enum PLCType { FONT=0, RULER, FOOTNOTE, PAGE, ZONE, UNKNOWN};

/** Internal: class to store the PLC: Pointer List Content ? */
struct PLC {
  //! constructor
  explicit PLC(PLCType type=UNKNOWN)
    : m_type(type)
    , m_id(-1)
    , m_extras("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, PLC const &plc);
  //! the type
  PLCType m_type;
  //! the id
  int m_id;
  //! a string used to store the parsing extrass
  std::string m_extras;
};

std::ostream &operator<<(std::ostream &o, PLC const &plc)
{
  switch (plc.m_type) {
  case FONT:
    o << "F";
    break;
  case RULER:
    o << "P";
    break;
  case FOOTNOTE:
    o << "Fn";
    break;
  case PAGE:
    o << "Page";
    break;
  case ZONE:
    o << "Z";
    break;
  case UNKNOWN:
#if !defined(__clang__)
  default:
#endif
    o << "#type" << int(plc.m_type);
    break;
  }
  if (plc.m_id != -1) o << plc.m_id;
  else o << "_";
  if (!plc.m_extras.empty()) o << ":" << plc.m_extras;
  return o;
}

////////////////////////////////////////
//! Internal: the font of a MsWrd1Parser
struct Font {
  //! constructor
  Font()
    : m_font()
    , m_type(0)
    , m_extras("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Font const &ft);
  //! the basic font property
  MWAWFont m_font;
  //! a unknown int, maybe 0x80 means defined font
  int m_type;
  //! a string used to store the parsing extrass
  std::string m_extras;
};

std::ostream &operator<<(std::ostream &o, Font const &ft)
{
  if (ft.m_type) o << "type=" << std::hex << ft.m_type << std::dec << ",";
  if (!ft.m_extras.empty()) o << ft.m_extras;
  return o;
}

////////////////////////////////////////
//! Internal: the paragraph of a MsWrd1Parser
struct Paragraph final : public MWAWParagraph {
  //! constructor
  Paragraph()
    : MWAWParagraph()
    , m_type(0)
    , m_type2(0)
  {
  }
  Paragraph(Paragraph const &)=default;
  Paragraph &operator=(Paragraph const &)=default;
  Paragraph &operator=(Paragraph &&)=default;
  //! destructor
  ~Paragraph() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Paragraph const &ft);
  //! the initial type
  int m_type;
  //! another type
  int m_type2;
};

Paragraph::~Paragraph()
{
}

std::ostream &operator<<(std::ostream &o, Paragraph const &para)
{
  o << static_cast<MWAWParagraph const &>(para);
  // 0|80 frequent: means redefine paragraph? find also a7 in a footnote
  if (para.m_type) o << "type=" << std::hex << para.m_type << std::dec << ",";

  if (para.m_type2 & 0xF0) {
    bool foot = (para.m_type2 & 0x10);
    if (foot) o << "footer/footnote[";
    else o << "header[";
    if (para.m_type2 & 0x20) o << (foot ? "even," : "odd,");
    if (para.m_type2 & 0x40) o << (foot ? "odd," : "even,");
    if (para.m_type2 & 0x80) o << "first,";
    o << "]";
  }
  if (para.m_type2 & 0xF)
    o << "#type2=" << std::hex << (para.m_type2 & 0xF) << std::dec << ",";

  return o;
}

////////////////////////////////////////
//! Internal: the state of a MsWrd1Parser
struct State {
  //! constructor
  State()
    : m_eot(-1)
    , m_numColumns(1)
    , m_columnsSep(0)
    , m_textZonesList()
    , m_mainTextZonesList()
    , m_fontsList()
    , m_paragraphsList()
    , m_endNote(false)
    , m_footnotesList()
    , m_plcMap()
    , m_actPage(0)
    , m_numPages(1)
    , m_headersId()
    , m_footersId()
  {
    for (auto &limit : m_fileZonesLimit) limit = -1;
  }

  //! end of text
  long m_eot;
  //! the number of columns
  int m_numColumns;
  //! the column separator
  float m_columnsSep;
  //! the zones limits
  int m_fileZonesLimit[7];
  //! the list of text zones
  std::vector<MWAWVec2l> m_textZonesList;
  //! the list of main text zones
  std::vector<int> m_mainTextZonesList;
  //! the list of fonts
  std::vector<Font> m_fontsList;
  //! the list of paragraph
  std::vector<Paragraph> m_paragraphsList;
  //! a flag to know if we send endnote or footnote
  bool m_endNote;
  //! the footnote positions ( list of beginPos, endPos)
  std::vector<MWAWVec2l> m_footnotesList;
  //! the text correspondance zone ( filepos, plc )
  std::multimap<long, PLC> m_plcMap;

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;
  /** the list of header id which corresponds to each page */
  std::vector<int> m_headersId;
  /** the list of footer id which corresponds to each page */
  std::vector<int> m_footersId;
};

////////////////////////////////////////
//! Internal: the subdocument of a MsWrdParser
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor for footnote, header
  SubDocument(MsWrd1Parser &pars, MWAWInputStreamPtr const &input, MWAWEntry const &position)
    : MWAWSubDocument(&pars, input, position)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    return MWAWSubDocument::operator!=(doc);
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("MsWrd1ParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<MsWrd1Parser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("MsWrd1ParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  if (!m_zone.valid()) {
    listener->insertChar(' ');
    return;
  }
  long pos = m_input->tell();
  parser->sendText(m_zone);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MsWrd1Parser::MsWrd1Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

MsWrd1Parser::~MsWrd1Parser()
{
}

void MsWrd1Parser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new MsWrd1ParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void MsWrd1Parser::newPage(int number)
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

void MsWrd1Parser::removeLastCharIfEOL(MWAWEntry &entry)
{
  if (!entry.valid()) return;
  MWAWInputStreamPtr input = getInput();
  long actPos = input->tell();
  input->seek(entry.end()-1, librevenge::RVNG_SEEK_SET);
  if (input->readLong(1)==0xd)
    entry.setLength(entry.length()-1);
  input->seek(actPos, librevenge::RVNG_SEEK_SET);
}
////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MsWrd1Parser::parse(librevenge::RVNGTextInterface *docInterface)
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
      sendMain();
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// send the main zone
////////////////////////////////////////////////////////////
void MsWrd1Parser::sendMain()
{
  for (auto id : m_state->m_mainTextZonesList) {
    if (id < 0 || id >= int(m_state->m_textZonesList.size()))
      continue;
    MWAWEntry entry;
    entry.setBegin(m_state->m_textZonesList[size_t(id)][0]);
    entry.setEnd(m_state->m_textZonesList[size_t(id)][1]);
    sendText(entry, true);
  }
  // maybe need if we have no text ; if not, nobody will see it
  if (getTextListener())
    getTextListener()->insertChar(' ');
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MsWrd1Parser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;
  // create the page list
  std::vector<MWAWPageSpan> pageList;
  auto numHeaders=int(m_state->m_headersId.size());
  auto numFooters=int(m_state->m_footersId.size());
  for (int i = 0; i <= m_state->m_numPages;) {
    int numSim[2]= {1,1};
    MWAWPageSpan ps(getPageSpan());
    while (i < numHeaders) {
      int id = m_state->m_headersId[size_t(i)];
      if (id < 0 || id >= int(m_state->m_textZonesList.size()))
        break;
      MWAWEntry entry;
      entry.setBegin(m_state->m_textZonesList[size_t(id)][0]);
      entry.setEnd(m_state->m_textZonesList[size_t(id)][1]);
      removeLastCharIfEOL(entry);
      if (!entry.valid()) break;
      MWAWHeaderFooter header(MWAWHeaderFooter::HEADER, MWAWHeaderFooter::ALL);
      header.m_subDocument.reset
      (new MsWrd1ParserInternal::SubDocument(*this, getInput(), entry));
      ps.setHeaderFooter(header);
      int j = i+1;
      while (j < numHeaders && m_state->m_headersId[size_t(j)]==id) {
        numSim[0]++;
        j++;
      }
      break;
    }
    while (i < int(numFooters)) {
      int id = m_state->m_footersId[size_t(i)];
      if (id < 0 || id >= int(m_state->m_textZonesList.size()))
        break;
      MWAWEntry entry;
      entry.setBegin(m_state->m_textZonesList[size_t(id)][0]);
      entry.setEnd(m_state->m_textZonesList[size_t(id)][1]);
      removeLastCharIfEOL(entry);
      if (!entry.valid()) break;
      MWAWHeaderFooter footer(MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
      footer.m_subDocument.reset
      (new MsWrd1ParserInternal::SubDocument(*this, getInput(), entry));
      ps.setHeaderFooter(footer);
      int j = i+1;
      while (j < numFooters && m_state->m_footersId[size_t(j)]==id) {
        numSim[1]++;
        j++;
      }
      break;
    }
    if (numSim[1] < numSim[0]) numSim[0]=numSim[1];
    if (numSim[0] < 1) numSim[0]=1;
    ps.setPageSpan(numSim[0]);
    i+=numSim[0];
    pageList.push_back(ps);
  }

  //
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

// create the different zones
bool MsWrd1Parser::createZones()
{
  libmwaw::DebugStream f;
  if (m_state->m_eot < 0x80) return false;

  ascii().addPos(0x80);
  ascii().addNote("TextContent");

  ascii().addPos(m_state->m_eot);
  ascii().addNote("_");

  MWAWInputStreamPtr input = getInput();
  for (int z = 5; z >= 0; z--) {
    if (m_state->m_fileZonesLimit[z] == m_state->m_fileZonesLimit[z+1])
      continue;
    if (!input->checkPosition(m_state->m_fileZonesLimit[z+1]*0x80) ||
        m_state->m_fileZonesLimit[z] > m_state->m_fileZonesLimit[z+1]) {
      f.str("");
      f << "Entries(Zone" << z << "):###";
      MWAW_DEBUG_MSG(("MsWrd1Parser::createZones: zone %d is too long\n",z));
      ascii().addPos(m_state->m_fileZonesLimit[z]*0x80);
      ascii().addNote(f.str().c_str());
      break;
    }
    MWAWVec2i limit(m_state->m_fileZonesLimit[z],m_state->m_fileZonesLimit[z+1]);
    bool done = false;
    switch (z) {
    case 0:
    case 1:
      done = readPLC(limit,z);
      break;
    case 2:
      done = readFootnoteCorrespondance(limit);
      break;
    case 3:
      done = readDocInfo(limit);
      break;
    case 4:
      done = readZones(limit);
      break;
    case 5:
      done = readPageBreak(limit);
      break;
    default:
      break;
    }
    if (done) continue;
    for (int p = m_state->m_fileZonesLimit[z], i=0; p < m_state->m_fileZonesLimit[z+1]; p++, i++) {
      f.str("");
      f << "Entries(Zone" << z << ")[" << i << "]:";
      ascii().addPos(p*0x80);
      ascii().addNote(f.str().c_str());
    }
    ascii().addPos(m_state->m_fileZonesLimit[z+1]*0x80);
    ascii().addNote("_");
  }
  prepareTextZones();
  return true;
}

// try to read retrieve the header/footer zones ...
bool MsWrd1Parser::prepareTextZones()
{
  m_state->m_numPages = 1;
  m_state->m_textZonesList.resize(0);
  m_state->m_mainTextZonesList.resize(0);
  m_state->m_headersId.resize(0);
  m_state->m_footersId.resize(0);
  long endMain = m_state->m_eot;
  for (auto const &footnote : m_state->m_footnotesList) {
    long pos = footnote[0];
    if (pos >= 0x80 && pos < endMain)
      endMain = pos;
  }
  if (endMain < 0x80) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::sendText: oops problem computing the limit of the main section"));
    m_state->m_textZonesList.push_back(MWAWVec2l(0x80, m_state->m_eot));
    m_state->m_mainTextZonesList.push_back(0);
    return false;
  }

  auto plcIt = m_state->m_plcMap.begin();
  long pos = 0x80, prevMainPos=pos;
  int actPage = 1;
  int actType = 0;
  MWAWVec2i headerId(-1,-1), footerId(-1,-1);
  int firstHeaderId=-1, firstFooterId=-1;
  while (pos < endMain) {
    int newType = 0;
    if (plcIt == m_state->m_plcMap.end() || plcIt->first>=endMain) {
      pos = endMain;
      newType = -1;
    }
    else {
      pos = plcIt->first;
      MsWrd1ParserInternal::PLC const &plc = plcIt++->second;
      if (plc.m_type==MsWrd1ParserInternal::PAGE && pos!=0x80) {
        if (actPage> int(m_state->m_headersId.size())) {
          m_state->m_headersId.resize(size_t(actPage),-1);
          m_state->m_headersId[size_t(actPage)-1] = headerId[(actPage%2)];
        }
        if (actPage> int(m_state->m_footersId.size())) {
          m_state->m_footersId.resize(size_t(actPage),-1);
          m_state->m_footersId[size_t(actPage)-1] = footerId[(actPage%2)];
        }
        actPage++;
      }
      if (plc.m_type!=MsWrd1ParserInternal::RULER) continue;
      if (plc.m_id >= 0 && plc.m_id < int(m_state->m_paragraphsList.size()))
        newType = (m_state->m_paragraphsList[size_t(plc.m_id)].m_type2>>4);
      if (newType == actType)
        continue;
    }
    if (pos==prevMainPos) {
      actType = newType;
      continue;
    }

    auto id = int(m_state->m_textZonesList.size());
    m_state->m_textZonesList.push_back(MWAWVec2l(prevMainPos, pos));
    prevMainPos=pos;
    if (actType==0) {
      m_state->m_mainTextZonesList.push_back(id);
      actType = newType;
      continue;
    }
    if (actType&1) {
      if (actType&2) footerId[1]=id;
      if (actType&4) footerId[0]=id;
      if (actType&8) firstFooterId=id;
      m_state->m_footersId.resize(size_t(actPage),-1);
      m_state->m_footersId[size_t(actPage)-1] =
        (actPage==1 && firstFooterId >= 0) ? firstFooterId :
        (actPage%2) ? footerId[1] : footerId[0];
    }
    else {
      if (actType&2) headerId[0]=id;
      if (actType&4) headerId[1]=id;
      if (actType&8) firstHeaderId=id;
      m_state->m_headersId.resize(size_t(actPage),-1);
      m_state->m_headersId[size_t(actPage)-1] =
        (actPage==1 && firstHeaderId >= 0) ? firstHeaderId :
        (actPage%2) ? headerId[1] : headerId[0];
    }
    actType = newType;
  }
  if (actPage> int(m_state->m_headersId.size())) {
    m_state->m_headersId.resize(size_t(actPage),-1);
    m_state->m_headersId[size_t(actPage)-1] = headerId[(actPage%2)];
  }
  if (actPage> int(m_state->m_footersId.size())) {
    m_state->m_footersId.resize(size_t(actPage),-1);
    m_state->m_footersId[size_t(actPage)-1] = footerId[(actPage%2)];
  }
  m_state->m_numPages = actPage;
  return true;
}

////////////////////////////////////////////////////////////
// try to read the different zones
////////////////////////////////////////////////////////////

// read the character property
bool MsWrd1Parser::readFont(long fPos, MsWrd1ParserInternal::Font &font)
{
  font = MsWrd1ParserInternal::Font();
  libmwaw::DebugStream f;
  MWAWInputStreamPtr input = getInput();
  input->seek(fPos, librevenge::RVNG_SEEK_SET);
  auto sz = static_cast<int>(input->readLong(1));
  if (sz < 1 || sz > 0x7f || !input->checkPosition(fPos+1+sz)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readFont: the zone size seems bad\n"));
    return false;
  }
  font.m_type = static_cast<int>(input->readULong(1));
  int val;
  uint32_t flags=0;
  if (sz >= 2) {
    val = static_cast<int>(input->readULong(1));
    if (val & 0x80) flags |= MWAWFont::boldBit;
    if (val & 0x40) flags |= MWAWFont::italicBit;
    if (val & 0x3f)
      font.m_font.setId((val & 0x3f));
  }
  if (sz >= 3) {
    val = static_cast<int>(input->readULong(1));
    if (val) font.m_font.setSize(float(val)/2.f);
  }
  if (sz >= 4) {
    val = static_cast<int>(input->readULong(1));
    if (val & 0x80) font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
    switch ((val&0xc)>>2) {
    case 0:
      break;
    case 3:
      flags |= MWAWFont::uppercaseBit;
      break;
    default:
      f << "#capBits=" << int((val&0xc)>>2) << ",";
    }
    // find also &2 for footnote
    if (val & 0x73)
      f << "#flags1=" << std::hex << (val & 0x73) << std::dec << ",";
  }
  if (sz >= 5) {
    val = static_cast<int>(input->readULong(1));
    if (val & 0x10) flags |= MWAWFont::embossBit;
    if (val & 0x8) flags |= MWAWFont::shadowBit;
    if (val & 0xe7)
      f << "#flags2=" << std::hex << (val & 0xe7) << std::dec << ",";
  }
  if (sz >= 6) { // vdepl
    val = static_cast<int>(input->readLong(1));
    if (val > 0) font.m_font.set(MWAWFont::Script::super100());
    else if (val < 0) font.m_font.set(MWAWFont::Script::sub100());
  }
  if (sz >= 7) {
    f << "###";
    ascii().addDelimiter(input->tell(),'|');
  }
  font.m_font.setFlags(flags);
  font.m_extras = f.str();

  return true;
}

/* read the paragraph property */
bool MsWrd1Parser::readParagraph(long fPos, MsWrd1ParserInternal::Paragraph &para)
{
  para = MsWrd1ParserInternal::Paragraph();
  libmwaw::DebugStream f;
  MWAWInputStreamPtr input = getInput();
  input->seek(fPos, librevenge::RVNG_SEEK_SET);
  auto sz = static_cast<int>(input->readLong(1));
  if (sz < 1 || sz > 0x7f || !input->checkPosition(fPos+1+sz)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readParagraph: the zone size seems bad\n"));
    return false;
  }
  para.m_type = static_cast<int>(input->readULong(1));
  int val;
  if (sz >= 2) {
    val = static_cast<int>(input->readULong(1));
    switch (val>>6) {
    case 0:
      break; // left
    case 1:
      para.m_justify = MWAWParagraph::JustificationCenter;
      break;
    case 2:
      para.m_justify = MWAWParagraph::JustificationRight;
      break;
    case 3:
      para.m_justify = MWAWParagraph::JustificationFull;
      break;
    default:
      break;
    }
    if (val & 0x10) f << "dontbreak[para],";
    if (val & 0x20) f << "dontbreak[line],";
    if (val & 0xf)
      f << "#justify=" << std::hex << (val & 0xf) << std::dec << ",";
  }
  if (sz >= 4) { // find always 0 here
    val = static_cast<int>(input->readLong(2));
    if (val) f << "#f0=" << val << ",";
  }
  if (sz >= 6) {
    val = static_cast<int>(input->readLong(2));
    if (val)
      para.m_margins[2] = double(val)/1440.0;
  }
  if (sz >= 8) {
    val = static_cast<int>(input->readLong(2));
    if (val)
      para.m_margins[0] = double(val)/1440.0;
  }
  if (sz >= 10) {
    val = static_cast<int>(input->readLong(2));
    if (val && !para.m_margins[0].isSet())
      para.m_margins[1] = double(val)/1440.0;
    else if (val)
      para.m_margins[1] = para.m_margins[0].get()+double(val)/1440.0;
  }
  if (sz >= 12) {
    val = static_cast<int>(input->readLong(2));
    if (val)
      para.setInterline(double(val)/1440.0, librevenge::RVNG_INCH);
  }
  if (sz >= 14) {
    val = static_cast<int>(input->readLong(2));
    if (val)
      para.m_spacings[1] = double(val)/1440.0;
  }
  if (sz >= 16) {
    val = static_cast<int>(input->readLong(2));
    if (val)
      para.m_spacings[2] = double(val)/1440.0;
  }
  if (sz >= 17)
    para.m_type2 = static_cast<int>(input->readULong(1));
  // checkme: not sure what is the exact decomposition of the following
  if (sz >= 22) { // find always 0 here
    for (int i = 0; i < 5; i++) {
      val = static_cast<int>(input->readLong(1));
      if (val) f << "#f" << i+1 << "=" << val << ",";
    }
  }
  if (sz >= 26) {
    int numTabs = (sz-26)/4;
    for (int i = 0; i < numTabs; i++) {
      MWAWTabStop newTab;
      newTab.m_position = double(input->readLong(2))/1440.;
      auto flags = static_cast<int>(input->readULong(1));
      switch ((flags>>5)&3) {
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
      switch ((flags>>2)&3) {
      case 0:
        break;
      case 1:
        newTab.m_leaderCharacter = '.';
        break;
      case 2:
        newTab.m_leaderCharacter = '-';
        break;
      case 3:
        newTab.m_leaderCharacter = '_';
        break;
      default:
        break;
      }
      if (flags & 0x93)
        f << "#tabs" << i << "[fl1=" << std::hex << (flags & 0x93) << std::dec << ",";
      val = static_cast<int>(input->readULong(1));
      if (val)
        f << "#tabs" << i << "[fl2=" << std::hex << val << std::dec << ",";
      para.m_tabs->push_back(newTab);
    }
  }
  if (input->tell() != fPos+1+sz)
    ascii().addDelimiter(input->tell(), '|');
  para.m_extra = f.str();
  return true;
}

/* read the page break separation */
bool MsWrd1Parser::readPageBreak(MWAWVec2i limits)
{
  MWAWInputStreamPtr input = getInput();
  if (limits[1] <= limits[0] || !input->checkPosition(limits[1]*0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readPageBreak: the zone is not well defined\n"));
    return false;
  }
  libmwaw::DebugStream f;
  long pos = limits[0]*0x80;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(PageBreak):";
  auto N = static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (N==0 || 4+6*N > (limits[1]-limits[0])*0x80) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readPageBreak: the number of element seems odds\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  long val = static_cast<int>(input->readULong(2)); // 1|a
  f << "unkn=" << val << ",";
  MsWrd1ParserInternal::PLC plc(MsWrd1ParserInternal::PAGE);
  for (int i = 0; i < N; i++) {
    auto pg = static_cast<int>(input->readULong(2));
    long textPos = long(input->readULong(4))+0x80;
    f << "Page" << i << "=" << std::hex << textPos << std::dec;
    if (pg != i+1) f << "[page=" << pg << "]";
    if (textPos < m_state->m_eot) {
      plc.m_id = pg;
      m_state->m_plcMap.insert
      (std::multimap<long,MsWrd1ParserInternal::PLC>::value_type(textPos, plc));
    }
    else if (i != N-1)
      f << "###";
    f << ",";
  }
  if (input->tell() != limits[1]*0x80)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

/* read the footnote zone */
bool MsWrd1Parser::readFootnoteCorrespondance(MWAWVec2i limits)
{
  MWAWInputStreamPtr input = getInput();
  if (limits[1] <= limits[0] || !input->checkPosition(limits[1]*0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readFootnoteCorrespondance: the zone is not well defined\n"));
    return false;
  }
  libmwaw::DebugStream f;

  long textEnd = m_state->m_eot;
  MsWrd1ParserInternal::PLC plc(MsWrd1ParserInternal::FOOTNOTE);
  long pos = limits[0]*0x80;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(Footnote):";
  auto N = static_cast<int>(input->readULong(2));
  auto N1 = static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (N!=N1) f << "N1=" << N1 << ",";
  if (N!=N1 || N==0 || 4+8*N > (limits[1]-limits[0])*0x80) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readFootnoteCorrespondance: the number of element seems odds\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  std::map<long, int> footnoteMap;
  for (int i = 0; i < N; i++) {
    long textPos = long(input->readULong(4))+0x80;
    long notePos = long(input->readULong(4))+0x80;
    bool ok = textPos <= textEnd && notePos <= textEnd;
    f << "Fn" << i << ":" << std::hex << textPos << "<->" << notePos << std::dec << ",";
    if (!ok) {
      if (i==N-1) break;
      f << "###";
      continue;
    }
    plc.m_id = int(footnoteMap.size());
    footnoteMap[notePos]=plc.m_id;
    m_state->m_plcMap.insert
    (std::multimap<long,MsWrd1ParserInternal::PLC>::value_type(textPos, plc));
    m_state->m_plcMap.insert
    (std::multimap<long,MsWrd1ParserInternal::PLC>::value_type(notePos, plc));
  }
  m_state->m_footnotesList.resize(footnoteMap.size(),MWAWVec2l(0,0));
  for (auto fIt=footnoteMap.begin(); fIt!=footnoteMap.end();) {
    MWAWVec2l fPos;
    fPos[0] = fIt->first;
    int id = fIt++->second;
    fPos[1] = fIt==footnoteMap.end() ? m_state->m_eot : fIt->first;
    if (id >= int(m_state->m_footnotesList.size()))
      m_state->m_footnotesList.resize(size_t(id)+1,MWAWVec2l(0,0));
    m_state->m_footnotesList[size_t(id)]=fPos;
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

/* read the zone4: a list of main zone ( headers, footers ) ? */
bool MsWrd1Parser::readZones(MWAWVec2i limits)
{
  MWAWInputStreamPtr input = getInput();
  if (limits[1] <= limits[0] || !input->checkPosition(limits[1]*0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readZones: the zone is not well defined\n"));
    return false;
  }
  libmwaw::DebugStream f;

  MsWrd1ParserInternal::PLC plc(MsWrd1ParserInternal::ZONE);
  long pos = limits[0]*0x80;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(Zones):";
  auto N = static_cast<int>(input->readULong(2));
  auto N1 = static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  if (N!=N1) f << "N1=" << N1 << ",";
  if (N!=N1 || N==0 || 4+10*N > (limits[1]-limits[0])*0x80) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readZones: the number of element seems odds\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  for (int i = 0; i < N; i++) {
    long textPos = long(input->readULong(4))+0x80;
    f << std::hex << textPos << std::dec;
    f << ":f0=" << input->readLong(2); // find 1|2|3
    auto val = static_cast<int>(input->readLong(4)); // find -1, 0x900, 0xa00
    if (val!=-1) f << ":f1=" << std::hex << val << std::dec;
    if (textPos < m_state->m_eot) {
      plc.m_id = i;
      m_state->m_plcMap.insert
      (std::multimap<long,MsWrd1ParserInternal::PLC>::value_type(textPos, plc));
    }
    else if (textPos != m_state->m_eot && i != N-1)
      f << "###";
    f << ",";
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

/* read the document information */
bool MsWrd1Parser::readDocInfo(MWAWVec2i limits)
{
  MWAWInputStreamPtr input = getInput();
  if (limits[1] != limits[0]+1 || !input->checkPosition(limits[1]*0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readDocInfo: the zone is not well defined\n"));
    return false;
  }

  libmwaw::DebugStream f;
  long pos = limits[0]*0x80;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << "Entries(DocInfo):";
  int val;
  for (int i=0; i < 2; i++) { // find 66|0
    val = static_cast<int>(input->readULong(1));
    if (val)
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  auto flags = static_cast<int>(input->readULong(1));
  switch (flags>>5) {
  case 0:
    f << "division=no,";
    break;
  case 1:
    f << "division=columns,";
    break;
  case 2:
    f << "division=page,";
    break; // default
  case 3:
    f << "division=evenpage,";
    break;
  case 4:
    f << "division=oddpage,";
    break;
  default:
    f << "#division=" << (flags>>5) << ",";
    break;
  }
  switch ((flags>>2)&7) {
  case 0: // default (numeric)
    break;
  case 1:
    f << "numbering=roman[upper],";
    break;
  case 2:
    f << "numbering=roman[lower],";
    break;
  case 3:
    f << "numbering=alpha[upper],";
    break;
  case 4:
    f << "numbering=alpha[lower],";
    break;
  default:
    f << "#numbering[type]=" << ((flags>>2)&7) << ",";
    break;
  }
  if (flags&3) f << "flags=" << (flags&3) << ",";

  float pageDim[2];
  for (auto &d : pageDim) d = float(input->readULong(2))/1440.f;
  f << "dim=[" << pageDim[1] << "x" << pageDim[0] << "],";
  val = static_cast<int>(input->readLong(2));
  if (val != -1) f << "firstPage=" << val << ",";
  // check me
  float pagePos[2][2]; // [Y|X][header|size]
  char const *wh[] = {"TopMargin", "Y[page]", "LeftMargin", "X[page]" };
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      pagePos[i][j] = float(input->readULong(2))/1440.f;
      f << wh[i*2+j] << "=" << pagePos[i][j] << ",";
    }
  }
  flags = static_cast<int>(input->readULong(1));
  bool endNote = false;
  if (flags&1) {
    f << "endnote,";
    endNote = true;
  }
  if (flags&2)
    f << "autonumbering,";
  if (flags&0xFC)
    f << "flags2=" << std::hex << (flags&0xFC) << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos = input->tell();
  f.str("");
  f << "DocInfo(II):";
  auto numCols = static_cast<int>(input->readULong(1));
  if (numCols != 1) {
    f << "nCols=" << numCols << ",";
    if (numCols < 1 || numCols > 6) {
      f << "###";
      numCols = 1;
    }
  }
  float hfLength[2];
  for (auto &hf : hfLength) hf = float(input->readULong(2))/1440.f;
  hfLength[1]=pageDim[0]-hfLength[1];

  f << "headerLength=" << hfLength[0] << ",";
  f << "footerLength=" << hfLength[1] << ",";
  float colSep = float(input->readULong(2))/1440.f;
  f << "colSep=" << colSep << ",";
  val = static_cast<int>(input->readLong(2));
  if (val)
    f << "f3=" << val << ",";
  f << "distToHeader=" << float(input->readULong(2))/1440.f << ",";
  f << "distToNote=" << float(input->readULong(2))/1440.f << ",";
  // probably follows by other distance

  if (pageDim[0] > 0 && pageDim[1] > 0 &&
      pagePos[0][0]>=0 && pagePos[0][1]>=0 && pageDim[0] >= pagePos[0][0]+pagePos[0][1] &&
      pagePos[1][0]>=0 && pagePos[1][1]>=0 && pageDim[1] >= pagePos[1][0]+pagePos[1][1] &&
      pageDim[1] >= float(numCols)*pagePos[1][1]) {
    getPageSpan().setMarginTop(double(pagePos[0][0]));
    getPageSpan().setMarginLeft(double(pagePos[1][0]));
    getPageSpan().setFormLength(double(pageDim[0]));
    getPageSpan().setFormWidth(double(pageDim[1]));
    m_state->m_endNote = endNote;
    m_state->m_numColumns = numCols;
    m_state->m_columnsSep = colSep;
  }
  else {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readDocInfo: some dimension do not look good\n"));
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(pos+53);
  ascii().addNote("DocInfo(III)");
  return true;
}

// read a plc zone (char or paragraph properties )
bool MsWrd1Parser::readPLC(MWAWVec2i limits, int wh)
{
  MWAWInputStreamPtr input = getInput();
  if (limits[1] <= limits[0] || !input->checkPosition(limits[1]*0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::readPLC: the zone is not well defined\n"));
    return false;
  }
  libmwaw::DebugStream f, f2;

  std::map<long, int> posIdMap;
  MsWrd1ParserInternal::PLC plc(wh==0 ? MsWrd1ParserInternal::FONT :
                                MsWrd1ParserInternal::RULER);
  char const *what = wh==0 ? "Char" : "Para";

  for (int z = limits[0], n=0; z < limits[1]; z++, n++) {
    f.str("");
    f << "Entries(" << what << ")[" << n << "]:";
    long pos = z*0x80;
    input->seek(pos+0x7f, librevenge::RVNG_SEEK_SET);
    auto N = static_cast<int>(input->readULong(1));
    f << "N=" << N << ",";
    if (4+N*6 > 0x7f) {
      f << "###";
      MWAW_DEBUG_MSG(("MsWrd1Parser::readPLC: the number of element seems to big\n"));
      ascii().addDelimiter(input->tell(),'|');
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      continue;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    auto fPos = long(input->readULong(4));

    for (int i = 0; i < N; i++) {
      f << "fPos=" << std::hex << fPos;
      auto newPos = long(input->readULong(4));
      f << "->" << newPos << std::dec;

      auto depl = static_cast<int>(input->readLong(2));
      if (depl == -1)
        plc.m_id = -1;
      else if (depl < N*6 || 4+depl >= 0x7f) {
        f << "[###pos=" << std::hex << depl << std::dec << "]";
        plc.m_id = -1;
      }
      else {
        long dataPos = pos+depl+4;
        long actPos = input->tell();
        if (posIdMap.find(dataPos) == posIdMap.end()) {
          f2.str("");
          f2 << what << "-";
          if (wh == 0) {
            MsWrd1ParserInternal::Font font;
            if (readFont(dataPos, font)) {
              plc.m_id=int(m_state->m_fontsList.size());
              m_state->m_fontsList.push_back(font);
              f2 << plc.m_id << ":";
#ifdef DEBUG
              f2 << font.m_font.getDebugString(getFontConverter()) << font;
#endif
            }
            else {
              plc.m_id = -1;
              f2 << "###";
            }
            ascii().addPos(dataPos);
            ascii().addNote(f2.str().c_str());
          }
          else {
            MsWrd1ParserInternal::Paragraph para;
            if (readParagraph(dataPos, para)) {
              plc.m_id=int(m_state->m_paragraphsList.size());
              m_state->m_paragraphsList.push_back(para);
              f2 << plc.m_id << ":" << para;
            }
            else {
              plc.m_id = -1;
              f2 << "###";
            }
            ascii().addPos(dataPos);
            ascii().addNote(f2.str().c_str());
          }
          posIdMap[dataPos] = plc.m_id;
        }
        else
          plc.m_id = posIdMap.find(dataPos)->second;
        input->seek(actPos, librevenge::RVNG_SEEK_SET);
      }
      m_state->m_plcMap.insert
      (std::multimap<long,MsWrd1ParserInternal::PLC>::value_type(fPos, plc));
      fPos = newPos;
      f << ":" << plc << ",";
    }
    ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  return true;
}

////////////////////////////////////////////////////////////
// try to read a text entry
////////////////////////////////////////////////////////////
bool MsWrd1Parser::sendText(MWAWEntry const &textEntry, bool isMain)
{
  if (!textEntry.valid()) return false;
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::sendText: can not find a listener!"));
    return true;
  }
  if (isMain) {
    int numCols = m_state->m_numColumns;
    if (numCols > 1 && !getTextListener()->isSectionOpened()) {
      MWAWSection sec;
      sec.setColumns(numCols, getPageWidth()/double(numCols), librevenge::RVNG_INCH, double(m_state->m_columnsSep));
      getTextListener()->openSection(sec);
    }
  }
  long pos = textEntry.begin();
  MWAWInputStreamPtr input = getInput();

  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "TextContent:";
  int actFId=-1, actRId = -1, actPage=0;
  auto plcIt = m_state->m_plcMap.begin();
  while (plcIt != m_state->m_plcMap.end() && plcIt->first < pos) {
    MsWrd1ParserInternal::PLC const &plc = plcIt++->second;
    if (plc.m_type == MsWrd1ParserInternal::FONT)
      actFId = plc.m_id;
    else if (plc.m_type == MsWrd1ParserInternal::RULER)
      actRId = plc.m_id;
    else if (plc.m_type == MsWrd1ParserInternal::PAGE)
      actPage++;
  }
  // new page can be in header, ..., so sometimes we must force a new page...
  if (isMain && actPage > m_state->m_actPage)
    newPage(actPage);
  MsWrd1ParserInternal::Font actFont, defFont;
  defFont.m_font = MWAWFont(3,12);
  if (actFId>=0 && actFId < int(m_state->m_fontsList.size()))
    actFont = m_state->m_fontsList[size_t(actFId)];
  else
    actFont = defFont;
  bool rulerNotSent = actRId != -1, fontNotSent = true;
  while (!input->isEnd() && input->tell() < textEntry.end()) {
    long actPos = input->tell();
    bool firstPlc = true;
    while (plcIt != m_state->m_plcMap.end() && plcIt->first <= actPos) {
      if (firstPlc) {
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        pos = actPos;
        f.str("");
        f << "TextContent:";
        firstPlc = false;
      }

      auto const &plc = plcIt++->second;
      switch (plc.m_type) {
      case MsWrd1ParserInternal::FONT:
        if (plc.m_id >= 0 && plc.m_id < int(m_state->m_fontsList.size()))
          getTextListener()->setFont(m_state->m_fontsList[size_t(plc.m_id)].m_font);
        else
          getTextListener()->setFont(defFont.m_font);
        actFont.m_font = getTextListener()->getFont();
        fontNotSent = false;
        break;
      case MsWrd1ParserInternal::RULER:
        actRId = plc.m_id;
        rulerNotSent = true;
        break;
      case MsWrd1ParserInternal::PAGE:
        if (isMain) newPage(++actPage);
        break;
      case MsWrd1ParserInternal::FOOTNOTE: {
        if (!isMain) break;
        if (plc.m_id < 0 || plc.m_id >= int(m_state->m_footnotesList.size())) {
          MWAW_DEBUG_MSG(("MsWrd1Parser::sendText: oops, can not find a footnote!\n"));
          break;
        }
        MWAWEntry entry;
        entry.setBegin(m_state->m_footnotesList[size_t(plc.m_id)][0]);
        entry.setEnd(m_state->m_footnotesList[size_t(plc.m_id)][1]);
        removeLastCharIfEOL(entry);
        std::shared_ptr<MWAWSubDocument> subdoc
        (new MsWrd1ParserInternal::SubDocument(*this, getInput(), entry));
        getTextListener()->insertNote(MWAWNote(m_state->m_endNote ? MWAWNote::EndNote : MWAWNote::FootNote), subdoc);
        break;
      }
      case MsWrd1ParserInternal::ZONE:
      case MsWrd1ParserInternal::UNKNOWN:
#if !defined(__clang__)
      default:
#endif
        break;
      }
      f << "[" << plc << "]";
    }
    if (rulerNotSent) {
      if (actRId >= 0 && actRId < int(m_state->m_paragraphsList.size()))
        setProperty(m_state->m_paragraphsList[size_t(actRId)]);
      else
        setProperty(MsWrd1ParserInternal::Paragraph());
      rulerNotSent = false;
    }
    if (fontNotSent) getTextListener()->setFont(actFont.m_font);
    auto c = static_cast<unsigned char>(input->readULong(1));
    f << char(c);
    switch (c) {
    case 1:
      getTextListener()->insertUnicodeString(librevenge::RVNGString("(picture)"));
      break;
    case 5: // footnote mark
    case 0xc: // end of file
      break;
    case 0x9:
      getTextListener()->insertTab();
      break;
    case 0xd:
      getTextListener()->insertEOL();
      break;
    default:
      getTextListener()->insertCharacter(static_cast<unsigned char>(c), input, textEntry.end());
      break;
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

// send the ruler properties
void MsWrd1Parser::setProperty(MsWrd1ParserInternal::Paragraph const &para)
{
  if (!getTextListener()) return;
  getTextListener()->setParagraph(para);
}

////////////////////////////////////////////////////////////
// Low level
////////////////////////////////////////////////////////////

// read the header
bool MsWrd1Parser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = MsWrd1ParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  if (!input->checkPosition(0x80)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::checkHeader: file is too short\n"));
    return false;
  }
  long pos = 0;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  auto val = static_cast<int>(input->readULong(2));
  switch (val) {
  case 0xfe32:
    switch (input->readULong(2)) {
    case 0x0:
      setVersion(1);
      break;
    default:
      return false;
    }
    break;
  default:
    return false;
  }

  f << "FileHeader:";
  val = static_cast<int>(input->readULong(1)); // v1: ab other 0 ?
  if (val) f << "f0=" << val << ",";
  for (int i = 1; i < 3; i++) { // always 0
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  for (int i = 0; i < 5; i++) { // always 0 ?
    val = static_cast<int>(input->readLong(1));
    if (val) f << "g" << i << "=" << val << ",";
  }

  m_state->m_eot = long(input->readULong(4));
  f << "text=" << std::hex << 0x80 << "<->" << m_state->m_eot << ",";
  if (0x80 > m_state->m_eot || !input->checkPosition(m_state->m_eot)) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::checkHeader: problem with text position must stop\n"));
    return false;
  }

  m_state->m_fileZonesLimit[0] = int((m_state->m_eot+0x7f)/0x80);
  f << "zonesPos=[" << std::hex;
  for (int i = 0; i < 6; i++) {
    m_state->m_fileZonesLimit[i+1] = static_cast<int>(input->readLong(2));
    if (m_state->m_fileZonesLimit[i]==m_state->m_fileZonesLimit[i+1]) {
      f << "_,";
      continue;
    }
    if (m_state->m_fileZonesLimit[i]<m_state->m_fileZonesLimit[i+1]) {
      f << m_state->m_fileZonesLimit[i]*0x80 << "<->"
        << m_state->m_fileZonesLimit[i+1]*0x80 << ",";
      continue;
    }
    MWAW_DEBUG_MSG(("MsWrd1Parser::checkHeader: problem reading the zones positions\n"));
    if (strict) return false;
    f << "###" << m_state->m_fileZonesLimit[i+1]*0x80 << ",";
    m_state->m_fileZonesLimit[i+1] = m_state->m_fileZonesLimit[i];
  }
  f << std::dec << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  pos = input->tell();
  f.str("");
  f << "FileHeader[A]:";
  for (int i = 0; i < 17; i++) {
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  long textSize[2];
  for (auto &tSize : textSize) tSize = input->readLong(4);
  if (textSize[0] != textSize[1] || 0x80+textSize[0] != m_state->m_eot) {
    MWAW_DEBUG_MSG(("MsWrd1Parser::checkHeader: problem with text position length\n"));
    if (strict) return false;
    f << "##textSize=" << std::hex << textSize[0] << ":" << textSize[1] << std::dec << ",";
    if (textSize[1] > textSize[0]) textSize[0] = textSize[1];
    if (0x80+textSize[0] > m_state->m_eot && input->checkPosition(0x80+textSize[0]))
      m_state->m_eot = 0x80+textSize[0];
  }
  pos=input->tell();
  f.str("");
  f << "FileHeader[B]:";
  for (int i = 0; i < 28; i++) { // always 0
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (header)
    header->reset(MWAWDocument::MWAW_T_MICROSOFTWORD, 1);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
