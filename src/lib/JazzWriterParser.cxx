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
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "JazzWriterParser.hxx"

/** Internal: the structures of a JazzWriterParser */
namespace JazzWriterParserInternal
{

////////////////////////////////////////
//! Internal: the structure used to store a paragraph/section
struct Paragraph {
  //! constructor
  Paragraph()
    : m_paragraph()
    , m_dimension(0,0)
    , m_nextParagraphId(0)
    , m_plcId(0)
  {
  }
  //! the paragraph
  MWAWParagraph m_paragraph;
  //! the dimension
  MWAWVec2i m_dimension;
  //! the next paragraph id
  unsigned m_nextParagraphId;
  //! the plc id
  unsigned m_plcId;
};

////////////////////////////////////////
//! Internal: the structure used to store a zone
struct Zone {
  //! constructor
  Zone()
    : m_paragraphId(0)
    , m_entry()
  {
    for (auto &id : m_hfIds) id=0;
  }
  //! the header/footer id
  unsigned m_hfIds[2];
  //! the paragraph id
  unsigned m_paragraphId;
  //! the text position in the data fork
  MWAWEntry m_entry;
};

////////////////////////////////////////
//! Internal: the state of a JazzWriterParser
struct State {
  //! constructor
  State()
    : m_idToZones()
    , m_idToParagraphs()
  {
  }

  /// map WDOC id to zones
  std::map<unsigned, Zone> m_idToZones;
  /// map WPPD id to zones
  std::map<unsigned, Paragraph> m_idToParagraphs;
};

////////////////////////////////////////
//! Internal: the subdocument of a JazzWriterParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(JazzWriterParser &pars, MWAWInputStreamPtr const &input, MWAWInputStreamPtr const &rsrcInput, unsigned id)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_rsrcInput(rsrcInput)
    , m_zId(id)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the resource fork input
  MWAWInputStreamPtr m_rsrcInput;
  //! the zone id
  unsigned m_zId;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("JazzWriterParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<JazzWriterParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("JazzWriterParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  if (!m_input || !m_rsrcInput) {
    MWAW_DEBUG_MSG(("JazzWriterParserInternal::SubDocument::parse: no input\n"));
    return;
  }
  long pos = m_input->tell();
  long rPos = m_rsrcInput->tell();
  parser->sendZone(m_zId);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
  m_rsrcInput->seek(rPos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_zId != sDoc->m_zId) return true;
  if (m_rsrcInput != sDoc->m_rsrcInput) return true;
  return false;
}
}


////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
JazzWriterParser::JazzWriterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state(new JazzWriterParserInternal::State)
{
  setAsciiName("main-1");
}

JazzWriterParser::~JazzWriterParser()
{
}

MWAWInputStreamPtr JazzWriterParser::rsrcInput()
{
  return getRSRCParser()->getInput();
}

libmwaw::DebugFile &JazzWriterParser::rsrcAscii()
{
  return getRSRCParser()->ascii();
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void JazzWriterParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !getRSRCParser() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendZone(257);
      if (!getInput()->isEnd()) {
        MWAW_DEBUG_MSG(("JazzWriterParser::parse: find some unsent characters\n"));
      }
    }
  }
  catch (...) {
    MWAW_DEBUG_MSG(("JazzWriterParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void JazzWriterParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::createDocument: listener already exist\n"));
    return;
  }

  // found the numbers of page and update the page
  int numPages = 1;
  auto input=getInput();
  input->seek(0, librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    if (input->readULong(1)==0xc)
      ++numPages;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(numPages+1);
  auto it=m_state->m_idToZones.find(257);
  if (it==m_state->m_idToZones.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::createDocument: can not find the main zone\n"));
    throw (libmwaw::ParseException());
  }

  for (int wh=0; wh<2; ++wh) {
    if (!it->second.m_hfIds[wh])
      continue;
    MWAWHeaderFooter header(wh==0 ? MWAWHeaderFooter::HEADER : MWAWHeaderFooter::FOOTER, MWAWHeaderFooter::ALL);
    header.m_subDocument.reset(new JazzWriterParserInternal::SubDocument(*this, getInput(), rsrcInput(), it->second.m_hfIds[wh]));
    ps.setHeaderFooter(header);
  }

  std::vector<MWAWPageSpan> pageList(1,ps);
  MWAWTextListenerPtr listen(new MWAWTextListener(*getParserState(), pageList, documentInterface));
  setTextListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool JazzWriterParser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    MWAW_DEBUG_MSG(("JazzWriterParser::createZones: can not find the entry map\n"));
    return false;
  }

  auto &entryMap = rsrcParser->getEntriesMap();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();

  char const *zNames[] = {"LFRF", "LLNK", "WDOC", "WPPD"};
  char const *what[]= {"FileRef", "Link", "Zone", "Paragraph"};
  libmwaw::DebugStream f;
  for (int wh=0; wh<4; ++wh) {
    auto it = entryMap.lower_bound(zNames[wh]);
    while (it != entryMap.end()) {
      if (it->first != zNames[wh] || !it->second.valid())
        break;
      MWAWEntry const &entry = it++->second;
      if (!input->checkPosition(entry.end())) {
        MWAW_DEBUG_MSG(("JazzWriterParser::createZones: find bad entry\n"));
        continue;
      }
      entry.setParsed(true);
      bool ok=false, done=false;
      int val;
      f.str("");
      f << "Entries(" << what[wh] << ")[" << entry.id() << "]:";
      input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
      switch (wh) {
      case 0: // link name
        f << entry.name() << ","; // the file name
        ok=true;
        if (entry.length()!=2) {
          MWAW_DEBUG_MSG(("JazzWriterParser::createZones[lref]: the entry length seems bad\n"));
          f << "###";
          break;
        }
        val=int(input->readLong(2));
        if (val!=1) f << "f0=" << val << ",";
        break;
      case 1: {
        librevenge::RVNGString text;
        ok=true;
        if (!readString(input, text, entry.end())) {
          MWAW_DEBUG_MSG(("JazzWriterParser::createZones[lnk]: can not find the text\n"));
          f << "###";
          break;
        }
        if (!text.empty())
          f << text.cstr() << ",";
        long cPos=input->tell();
        if (cPos+2>entry.end()) {
          MWAW_DEBUG_MSG(("JazzWriterParser::createZones[lnk]: can not find the file ref\n"));
          f << "###";
          break;
        }
        f << "file[ref]=" << input->readULong(2) << ",";
        break;
      }
      case 2:
        done=readZone(entry);
        break;
      case 3:
        done=readParagraph(entry);
        break;
      default:
        break;
      }
      if (done)
        continue;
      if (!ok)
        f << "###";
      ascFile.addPos(entry.begin()-4);
      ascFile.addNote(f.str().c_str());
    }
  }
  return checkZones();
}

bool JazzWriterParser::checkZones()
{
  auto it=m_state->m_idToZones.find(257);
  if (it==m_state->m_idToZones.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkZones: can not find the main zone\n"));
    return false;
  }
  unsigned zones[3]= {it->second.m_hfIds[0],it->second.m_hfIds[1],257};
  std::set<unsigned> seens;
  long pos=0;
  for (auto const &id : zones) {
    it=m_state->m_idToZones.find(id);
    if (it==m_state->m_idToZones.end()) {
      MWAW_DEBUG_MSG(("JazzWriterParser::checkZones: can not find the %x zone\n", id));
      return false;
    }
    long length=0;
    if (!checkParagraphs(it->second.m_paragraphId, length, seens))
      return false;
    MWAWEntry entry;
    entry.setBegin(pos);
    entry.setLength(length);
    it->second.m_entry=entry;
    pos+=length;
  }
  if (!getInput() || getInput()->size()<pos) {
    MWAW_DEBUG_MSG(("JazzWriterParser::createZones: the data fork seems too short\n"));
    return false;
  }
  getInput()->seek(0, librevenge::RVNG_SEEK_SET);

  return true;
}

bool JazzWriterParser::checkParagraphs(unsigned id, long &num, std::set<unsigned> &seens) const
{
  if (id==0)
    return true;
  if (seens.find(id) != seens.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkParagraphs: paragraph %x is already seen\n", id));
    return false;
  }
  seens.insert(id);
  auto it=m_state->m_idToParagraphs.find(id);
  if (it==m_state->m_idToParagraphs.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkParagraphs: can not find paragraph %x\n", id));
    return false;
  }
  long n=0;
  if (!const_cast<JazzWriterParser *>(this)->countCharactersInPLC(it->second.m_plcId, n))
    return false;
  num+=n;
  checkParagraphs(it->second.m_nextParagraphId, num, seens);
  return true;
}

bool JazzWriterParser::countCharactersInPLC(unsigned plcId, long &n)
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    MWAW_DEBUG_MSG(("JazzWriterParser::countCharactersInPLC: can not find the rsrc parser\n"));
    return false;
  }
  MWAWInputStreamPtr input = rsrcInput();
  MWAWEntry entry=rsrcParser->getEntry("WSCR", int(plcId));
  if (!entry.valid() || !input || !input->checkPosition(entry.end())) {
    MWAW_DEBUG_MSG(("JazzWriterParser::countCharactersInPLC: can not find the %x WSCR\n", plcId));
    return false;
  }
  n=0;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  long endPos=entry.end();
  while (input->tell()+6<=endPos) {
    long pos=input->tell();
    int type=int(input->readULong(2));
    switch (type) {
    case 1:
      n+=int(input->readULong(4));
      break;
    case 3:
      input->seek(3, librevenge::RVNG_SEEK_CUR);
      n+=int(input->readULong(1));
      break;
    case 5:
      pos+=6;
      break;
    default:
      break;
    }
    input->seek(pos+6, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool JazzWriterParser::readParagraph(MWAWEntry const &entry)
{
  if (entry.length()!=122) {
    MWAW_DEBUG_MSG(("JazzWriterParser::readParagraph: unexpected size\n"));
    return false;
  }
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;

  JazzWriterParserInternal::Paragraph para;
  f << "Entries(Paragraph)[" << entry.id() << "]:";
  int val;
  para.m_nextParagraphId=unsigned(input->readLong(2));
  if (para.m_nextParagraphId) f << "next[para]=" << para.m_nextParagraphId << ",";
  val=int(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  val=int(input->readLong(2));
  if (val!=2)
    f << "ID1=" << input->readULong(2) << ",";
  else
    f << "ID1=" << input->readULong(2) << "[" << val << "],";
  f << "dim0=" << input->readLong(2) << ",";
  int dims[2];
  for (auto &d : dims) d=int(input->readLong(2));
  para.m_dimension=MWAWVec2i(dims[1],dims[0]);
  f << "dim=" << para.m_dimension << ",";
  val=int(input->readLong(2));
  if (val==2)
    f << "ID2=" << input->readULong(2) << ",";
  else
    f << "ID2=" << input->readULong(2) << "[" << val << "],";
  for (int i=0; i<11; ++i) { // f5,f7 some heigths?, f8=0|600
    val=int(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  f << "unk=" << input->readLong(2) << ","; // 1000|5000
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val!=(i==1 ? 4 : 0))
      f << "g" << i << "=" << val << ",";
  }
  f << "fl=" << std::hex << input->readULong(2) << std::dec << ","; // 1002|1003
  val=int(input->readLong(2));
  if (val!=0xc00) f << "g2=" << val << ",";
  for (int i=0; i<4; ++i) { // g3=0|8
    val=int(input->readULong(2));
    if (i==2 && (val&0x300)) {
      f << "align=" << ((val>>8)&3) << ",";
      switch ((val>>8)&3) {
      case 1:
        para.m_paragraph.m_justify=MWAWParagraph::JustificationRight;
        break;
      case 2:
        para.m_paragraph.m_justify=MWAWParagraph::JustificationCenter;
        break;
      case 3:
        para.m_paragraph.m_justify=MWAWParagraph::JustificationFull;
        break;
      default:
        break;
      }
      val &= 0xfcff;
    }
    if (i==3 && (val&3)) {
      f << "line spacing=" << 1+float(val&3)/2 << ",";
      para.m_paragraph.setInterline(1+float(val&3)/2, librevenge::RVNG_PERCENT);
      val &= 0xfffc;
    }
    if (!val) continue;
    if (i==0)
      f << "g3=" << std::hex << val << std::dec << ",";
    else
      f << "g" << i+3 << "=" << val << ",";
  }
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());

  input->seek(entry.begin()+58, librevenge::RVNG_SEEK_SET);
  long pos=input->tell();
  f.str("");
  f << "Paragraph-A:";
  int N=int(input->readLong(2));
  f << "num[tabs]=" << N << ",";
  if (N<0 || N>12) {
    MWAW_DEBUG_MSG(("JazzWriterParser::readParagraph: the number of tabs seems bads\n"));
    f << "###";
    N=0;
  }
  para.m_paragraph.m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {72, 0x21c, 72};
    para.m_paragraph.m_margins[i==2 ? 0 : i+1]=val;
    if (val==expected[i]) continue;
    f << (i==0 ? "marg[left]" : i==1 ? "marg[right]" : "first[ident]") << "=" << val << ",";
  }
  *para.m_paragraph.m_margins[0]-=*para.m_paragraph.m_margins[1];
  para.m_paragraph.m_margins[2]=0; // margin from left, so ignore
  f << "tabs[";
  for (int i=0; i<N; ++i) {
    MWAWTabStop tab;
    val=int(input->readLong(2));
    if (val>=0) {
      tab.m_position=double(val)/72.;
      f << val << ",";
    }
    else {
      tab.m_position=double(-val)/72.;
      tab.m_alignment = MWAWTabStop::CENTER;
      f << -val << "[C],";
    }
    para.m_paragraph.m_tabs->push_back(tab);
  }
  f << "],";
  input->seek(pos+8+24, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<6; ++i) {
    val=int(input->readLong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  val=int(input->readLong(2));
  if (val!=2)
    f << "h0=" << val << ",";
  f << "ID1=" << input->readULong(2) << ",";
  for (int i=0; i<8; ++i) {
    val=int(input->readULong(2));
    if (!val) continue;
    if (i==4) {
      para.m_plcId=unsigned(val);
      f << "plc[id]=" << val << ",";
    }
    else
      f << "h" << i+1 << "=" << val << ",";
  }
  if (m_state->m_idToParagraphs.find(unsigned(entry.id()))==m_state->m_idToParagraphs.end())
    m_state->m_idToParagraphs[unsigned(entry.id())]=para;
  else {
    MWAW_DEBUG_MSG(("JazzWriterParser::readParagraph: paragraph %d already exists\n", entry.id()));
    f << "###id,";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool JazzWriterParser::readZone(MWAWEntry const &entry)
{
  if (entry.length()!=44) {
    MWAW_DEBUG_MSG(("JazzWriterParser::readZone: unexpected size\n"));
    return false;
  }
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  libmwaw::DebugStream f;

  JazzWriterParserInternal::Zone zone;
  f << "Entries(Zone)[" << entry.id() << "]:";
  zone.m_paragraphId=unsigned(input->readULong(2));
  f << "para[id]=" << zone.m_paragraphId << ",";
  int id1=int(input->readULong(2));
  f << "ID1=" << id1 << ",";
  int val=int(input->readULong(2));
  if (unsigned(val)!=zone.m_paragraphId)
    f << "para[id1]=" << val << ",";
  val=int(input->readULong(2));
  if (val!=id1)
    f << "ID2=" << val << ",";
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int dims[2];
  for (auto &d : dims) d=int(input->readLong(2));
  f << "dim=" << MWAWVec2i(dims[1],dims[0]) << ",";
  val=int(input->readLong(2));
  if (val)
    f << "f0=" << val << ",";
  zone.m_hfIds[0]=unsigned(input->readULong(2));
  if (zone.m_hfIds[0]) f << "zone[header]=" << zone.m_hfIds[0] << ",";
  val=int(input->readULong(2));
  if (val)
    f << "ID[header]=" << val << ",";
  zone.m_hfIds[1]=unsigned(input->readULong(2));
  if (zone.m_hfIds[1]) f << "zone[footer]=" << zone.m_hfIds[1] << ",";
  val=int(input->readULong(2));
  if (val)
    f << "ID[footer]=" << val << ",";
  for (int i=0; i<8; ++i) { // f3=1 header/footer, c:main?
    val=int(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  if (m_state->m_idToZones.find(unsigned(entry.id()))==m_state->m_idToZones.end())
    m_state->m_idToZones[unsigned(entry.id())]=zone;
  else {
    MWAW_DEBUG_MSG(("JazzWriterParser::readZone: zone %d already exists\n", entry.id()));
    f << "###id,";
  }
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////
bool JazzWriterParser::readString(MWAWInputStreamPtr input, librevenge::RVNGString &string, long endPos)
{
  string.clear();
  if (!input) {
    MWAW_DEBUG_MSG(("JazzWriterParser::readString: can not find the input\n"));
    return false;
  }
  auto fontConverter=getFontConverter();
  int defaultFont=3;
  long pos=input->tell();
  int n=int(input->readULong(1));
  if (!input->checkPosition(pos+1+n) || pos+1+n>endPos) {
    MWAW_DEBUG_MSG(("JazzWriterParser::readString: can not read the string length\n"));
    return false;
  }
  for (int i=0; i<n; ++i) {
    char c=char(input->readULong(1));
    int unicode = fontConverter->unicode(defaultFont, static_cast<unsigned char>(c));
    if (unicode>0)
      libmwaw::appendUnicode(uint32_t(unicode), string);
    else {
      MWAW_DEBUG_MSG(("JazzWriterParser::readString: find unknown unicode for char=%d\n", int(c)));
    }
  }
  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool JazzWriterParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  *m_state = JazzWriterParserInternal::State();
  if (!getRSRCParser())
    return false;
  // check if the WDOC entry exists
  MWAWEntry entry = getRSRCParser()->getEntry("WDOC", 257);
  if (entry.begin()<=0) { // length can be not 0, so ...
    MWAW_DEBUG_MSG(("JazzWriterParser::checkHeader: can not find the WDOC[257] resource\n"));
    return false;
  }
  if (!getInput()->hasDataFork() || getInput()->size()<=0) {
    // checkme: is this possible when the document contains only a picture
    MWAW_DEBUG_MSG(("JazzWriterParser::checkHeader: can not find any data fork\n"));
    return false;
  }
  if (header)
    header->reset(MWAWDocument::MWAW_T_JAZZLOTUS, 1);

  return true;
}

////////////////////////////////////////////////////////////
// send the data
////////////////////////////////////////////////////////////
bool JazzWriterParser::sendZone(unsigned zId)
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkHeader: can not find the main listener\n"));
    return false;
  }
  auto it=m_state->m_idToZones.find(zId);
  if (it==m_state->m_idToZones.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkZones: can not find the %x zone\n", zId));
    return false;
  }

  auto input=getInput();
  input->seek(it->second.m_entry.begin(), librevenge::RVNG_SEEK_SET);
  sendParagraph(it->second.m_paragraphId);
  return true;
}

bool JazzWriterParser::sendParagraph(unsigned pId)
{
  auto it=m_state->m_idToParagraphs.find(pId);
  if (it==m_state->m_idToParagraphs.end()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::checkZones: can not find the %x paragraph\n", pId));
    return false;
  }
  auto const &para=it->second;
  getTextListener()->setParagraph(para.m_paragraph);
  sendPLC(para.m_plcId);
  if (para.m_nextParagraphId)
    sendParagraph(para.m_nextParagraphId);
  return true;
}

bool JazzWriterParser::sendPLC(unsigned pId)
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    MWAW_DEBUG_MSG(("JazzWriterParser::sendPLC: can not find the rsrc parser\n"));
    return false;
  }
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("JazzWriterParser::sendPLC: can not find the text listener\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  MWAWInputStreamPtr rInput = rsrcInput();
  libmwaw::DebugFile &rAscFile = rsrcAscii();
  libmwaw::DebugStream f;

  MWAWEntry entry=rsrcParser->getEntry("WSCR", int(pId));
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::sendPLC: can not find the %x plc\n", pId));
    return false;
  }

  f << "Entries(PLC)[" << entry.id() << "]:";
  rAscFile.addPos(entry.begin()-4);
  rAscFile.addNote(f.str().c_str());

  rInput->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  int N=int(entry.length()/6);
  int val;
  auto fontConverter=getFontConverter();
  for (int i=0; i<N; ++i) {
    long rPos=rInput->tell();
    f.str("");
    f << "PLC-" << i << ":";
    int wh=int(rInput->readLong(2));
    int numChar=0;
    switch (wh) {
    case 1:
      numChar=int(rInput->readULong(4));
      f << "num=" << numChar << ",";
      break;
    case 2: {
      MWAWFont font;
      f << "font,";
      f << "h=" << rInput->readULong(1) << ",";
      font.setId(int(rInput->readULong(1)));
      font.setSize(float(rInput->readULong(1)));
      val=int(rInput->readULong(1));
      uint32_t flags = 0;
      if (val&0x1) flags |= MWAWFont::boldBit;
      if (val&0x2) flags |= MWAWFont::italicBit;
      if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
      if (val&0x8) flags |= MWAWFont::embossBit;
      if (val&0x10) flags |= MWAWFont::shadowBit;
      font.setFlags(flags);
      val &= 0xe0;
      f << "font=[" << font.getDebugString(fontConverter) << "]";
      if (val)
        f << "fl=" << std::hex << val << std::dec << ",";
      listener->setFont(font);
      break;
    }
    case 3: {
      val=int(rInput->readULong(2));
      bool sent=false;
      if (val>=0xFFF0) {
        f << "field" << (0xFFFF-val) << ",";
        sent=true;
        switch (0xFFFF-val) {
        case 0: {
          MWAWField field(MWAWField::Date);
          field.m_DTFormat="%B %d, %Y";
          listener->insertField(field);
          break;
        }
        case 1: {
          MWAWField field(MWAWField::Time);
          field.m_DTFormat="%I:%M %p";
          listener->insertField(field);
          break;
        }
        case 2:
          listener->insertField(MWAWField(MWAWField::PageNumber));
          break;
        default:
          MWAW_DEBUG_MSG(("JazzWriterParser::readPLC: find unknown field\n"));
          f << "###";
          sent=false;
          break;
        }
      }
      else
        f << "link[id]=" << val << ",";
      f << "unk=" << int(rInput->readULong(1)) << ","; // 8-1e
      numChar=int(rInput->readULong(1));
      f << "len=" << numChar << ",";
      if (sent) {
        input->seek(numChar, librevenge::RVNG_SEEK_CUR);
        numChar=0;
      }
      break;
    }
    case 5: {
      f << "pict[link],";
      if (i+1>=N) {
        MWAW_DEBUG_MSG(("JazzWriterParser::readPLC: the zone seems too short\n"));
        f << "###";
        break;
      }
      f << "link[id]=" << int(rInput->readULong(2)) << ",";
      unsigned pictId=unsigned(rInput->readULong(2));
      f << "pict[id]=" << pictId << ",";
      int dim[2];
      for (auto &d : dim) d=int(rInput->readULong(2));
      f << "sz=" << MWAWVec2i(dim[1], dim[0]) << ",";
      int xPos=int(rInput->readLong(2));
      f << "xPos=" << xPos << ","; // related to baseline?
      ++i;
      rPos+=6;
      MWAWEmbeddedObject obj;
      if (!getPicture(pictId, obj) || obj.isEmpty()) {
        f << "##pictId";
        break;
      }
      // TODO: use xPos
      MWAWPosition position(MWAWVec2f(0,0), MWAWVec2f(float(dim[1]), float(dim[0])), librevenge::RVNG_POINT);
      position.setRelativePosition(MWAWPosition::Char);
      listener->insertPicture(position, obj);
      listener->insertEOL(); // one picture by line
      break;
    }
    default:
      MWAW_DEBUG_MSG(("JazzWriterParser::readPLC: find unknown type\n"));
      f << "###type" << wh << ",";
      break;
    }
    rAscFile.addPos(rPos);
    rAscFile.addNote(f.str().c_str());
    if (rInput->tell()!=rPos+6)
      rAscFile.addDelimiter(rInput->tell(),'|');
    rInput->seek(rPos+6, librevenge::RVNG_SEEK_SET);

    // now read/send the character
    if (numChar<=0) continue;
    long pos=input->tell();
    if (!input->checkPosition(pos+numChar)) {
      MWAW_DEBUG_MSG(("JazzWriterParser::readPLC: can not find some character\n"));
      break;
    }
    for (int c=0; c<numChar; ++c) {
      auto ch=(unsigned char)input->readULong(1);
      switch (ch) {
      case 0x9:
        listener->insertChar(ch);
        break;
      case 0xc:
        listener->insertBreak(MWAWTextListener::PageBreak);
        break;
      case 0xd:
        listener->insertEOL();
        break;
      default:
        if (ch<=0x1f) {
          MWAW_DEBUG_MSG(("JazzWriterParser::readPLC: find bad character %d at pos=0x%lx\n", int(c), input->tell()));
          break;
        }
        listener->insertCharacter(ch);
      }
    }
  }
  rAscFile.addPos(rInput->tell());
  rAscFile.addNote("PLC-end:");
  val=int(rInput->readLong(2));
  if (val)
    f << "##f0=" << val << ",";
  return true;
}

bool JazzWriterParser::getPicture(unsigned pId, MWAWEmbeddedObject &obj)
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (!rsrcParser) {
    MWAW_DEBUG_MSG(("JazzWriterParser::getPicture: can not find the rsrc parser\n"));
    return false;
  }
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();

  MWAWEntry entry=rsrcParser->getEntry("PICT", int(pId));
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("JazzWriterParser::getPicture: can not find the %x picture\n", pId));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(entry.length())));
  if (!pict || !pict->getBinary(obj)) {
    MWAW_DEBUG_MSG(("JazzWriterParser::getPicture: can not read the %x picture\n", pId));
    return false;
  }

  ascFile.skipZone(entry.begin(), entry.end());
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
