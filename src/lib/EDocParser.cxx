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
#include <limits>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWTextListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSubDocument.hxx"

#include "libmwaw_internal.hxx"

#include "EDocParser.hxx"

/** Internal: the structures of a EDocParser */
namespace EDocParserInternal
{
////////////////////////////////////////
//! Internal: an index of a EDocParser
struct Index {
  //! constructor
  Index()
    : m_levelId(0)
    , m_text("")
    , m_page(-1)
    , m_extra("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Index const &index)
  {
    if (index.m_text.length()) o << "text=\"" << index.m_text << "\",";
    if (index.m_levelId) o << "levelId=" << index.m_levelId << ",";
    if (index.m_page>0) o << "page=" << index.m_page << ",";
    o << index.m_extra;
    return o;
  }
  //! the font id
  int m_levelId;
  //! the text
  std::string m_text;
  //! the page number
  int m_page;
  //! extra data
  std::string m_extra;
};

////////////////////////////////////////
//! Internal: the state of a EDocParser
struct State {
  //! constructor
  State()
    : m_compressed(false)
    , m_maxPictId(0)
    , m_idCPICMap()
    , m_idPICTMap()
    , m_indexList()
    , m_actPage(0)
    , m_numPages(0)
    , m_headerHeight(0)
    , m_footerHeight(0)
  {
  }
  //! a flag to know if the data are compressed or not
  bool m_compressed;
  //! the maximum of picture to read
  int m_maxPictId;
  //! a map id -> cPIC zone
  std::map<int,MWAWEntry> m_idCPICMap;
  //! a map id -> PICT zone
  std::map<int,MWAWEntry> m_idPICTMap;
  //! the index list
  std::vector<Index> m_indexList;
  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  int m_headerHeight /** the header height if known */,
      m_footerHeight /** the footer height if known */;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
EDocParser::EDocParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

EDocParser::~EDocParser()
{
}

void EDocParser::init()
{
  resetTextListener();

  m_state.reset(new EDocParserInternal::State);

  // no margins ( ie. the document is a set of picture corresponding to each page )
  getPageSpan().setMargins(0.01);
}

MWAWInputStreamPtr EDocParser::rsrcInput()
{
  return getRSRCParser()->getInput();
}

libmwaw::DebugFile &EDocParser::rsrcAscii()
{
  return getRSRCParser()->ascii();
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void EDocParser::newPage(int number)
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
void EDocParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !getRSRCParser() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendContents();
#ifdef DEBUG
      flushExtra();
#endif
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("EDocParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void EDocParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("EDocParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;

  // create the page list
  int numPages = m_state->m_maxPictId;
  if (!m_state->m_indexList.empty())
    numPages++;
  if (numPages <= 0) numPages=1;
  m_state->m_numPages=numPages;

  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(numPages+1);
  std::vector<MWAWPageSpan> pageList(1,ps);
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
bool EDocParser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  auto &entryMap = rsrcParser->getEntriesMap();

  // the 128 zone
  auto it = entryMap.lower_bound("eDcF");
  while (it != entryMap.end()) {
    if (it->first != "eDcF")
      break;
    MWAWEntry const &entry = it++->second;
    readFontsName(entry);
  }
  it = entryMap.lower_bound("eIdx");
  while (it != entryMap.end()) {
    if (it->first != "eIdx")
      break;
    MWAWEntry const &entry = it++->second;
    readIndex(entry);
  }
  it = entryMap.lower_bound("Info");
  while (it != entryMap.end()) {
    if (it->first != "Info")
      break;
    MWAWEntry const &entry = it++->second;
    readInfo(entry);
  }
  bool res=findContents();
#ifdef DEBUG_WITH_FILES
  // get rid of the default application resource
  libmwaw::DebugFile &ascFile = rsrcAscii();
  static char const *appliRsrc[]= {
    // default, Dialog (3000: DLOG,DITL,DLGX,dctb","ictb","STR ")
    "ALRT","BNDL","CNTL","CURS","CDEF", "DLOG","DLGX","DITL","FREF","ICON",
    "ICN#","MENU","SIZE","WIND",
    "cicn","crsr","dctb","icl4","icl8", "ics4","ics8","ics#","ictb","mstr",
    "snd ",
    "eSRD"
  };
  for (int r=0; r < 14+11+1; r++) {
    it = entryMap.lower_bound(appliRsrc[r]);
    while (it != entryMap.end()) {
      if (it->first != appliRsrc[r])
        break;
      MWAWEntry const &entry = it++->second;
      if (entry.isParsed()) continue;
      entry.setParsed(true);
      ascFile.skipZone(entry.begin()-4,entry.end()-1);
    }
  }
#endif
  return res;
}

bool EDocParser::findContents()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  auto &entryMap = rsrcParser->getEntriesMap();

  /* if the data is compress, we must look for cPIC zone ; if not,
     we look for the first PICT zone.
     Note: maybe we can also find text in TEXT zone, but I never see that
   */
  char const *wh[2]= {"cPIC", "PICT"};
  for (int st = 0; st < 2; st++) {
    std::map<int, MWAWEntry> &map=st==0 ? m_state->m_idCPICMap : m_state->m_idPICTMap;
    std::set<int> seens;
    auto it = entryMap.lower_bound(wh[st]);
    while (it != entryMap.end()) {
      if (it->first != wh[st])
        break;
      MWAWEntry const &entry = it++->second;
      map[entry.id()]= entry;
      seens.insert(entry.id());
    }
    if (seens.empty() || m_state->m_maxPictId)
      continue;
    auto sIt=seens.lower_bound(1);
    if (sIt==seens.end()|| *sIt>10)
      continue;
    int maxId=*sIt;
    while (sIt!=seens.end() && *sIt<maxId+5)
      maxId=*(sIt++);
    m_state->m_maxPictId=maxId;
    m_state->m_compressed=(st==0);
  }

  return true;
}

bool EDocParser::sendContents()
{
  bool compressed=m_state->m_compressed;
  int actPage=0;
  for (int i=1; i <= m_state->m_maxPictId; i++) {
    newPage(++actPage);
    sendPicture(i, compressed);
  }
  if (!m_state->m_indexList.empty()) {
    newPage(++actPage);
    sendIndex();
  }
  return true;
}

bool EDocParser::sendPicture(int pictId, bool compressed)
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("EDocParser::sendPicture: can not find the listener\n"));
    return false;
  }
  std::map<int, MWAWEntry>::const_iterator it;
  librevenge::RVNGBinaryData data;
  if (compressed) {
    it = m_state->m_idCPICMap.find(pictId);
    if (it==m_state->m_idCPICMap.end() || !decodeZone(it->second,data))
      return false;
  }
  else {
    it = m_state->m_idPICTMap.find(pictId);
    if (it==m_state->m_idPICTMap.end() ||
        !getRSRCParser()->parsePICT(it->second, data))
      return false;
  }

  auto dataSz=int(data.size());
  if (!dataSz)
    return false;
  MWAWInputStreamPtr pictInput=MWAWInputStream::get(data, false);
  if (!pictInput) {
    MWAW_DEBUG_MSG(("EDocParser::sendPicture: oops can not find an input\n"));
    return false;
  }
  MWAWBox2f box;
  auto res = MWAWPictData::check(pictInput, dataSz,box);
  if (res == MWAWPict::MWAW_R_BAD) {
    MWAW_DEBUG_MSG(("EDocParser::sendPicture: can not find the picture\n"));
    return false;
  }
  pictInput->seek(0,librevenge::RVNG_SEEK_SET);
  std::shared_ptr<MWAWPict> thePict(MWAWPictData::get(pictInput, dataSz));
  MWAWPosition pictPos=MWAWPosition(MWAWVec2f(0,0),box.size(), librevenge::RVNG_POINT);
  pictPos.setRelativePosition(MWAWPosition::Char);
  if (thePict) {
    MWAWEmbeddedObject picture;
    if (thePict->getBinary(picture))
      getTextListener()->insertPicture(pictPos, picture);
  }
  return true;
}

void EDocParser::flushExtra()
{
#ifdef DEBUG
  for (auto rIt : m_state->m_idCPICMap) {
    MWAWEntry const &entry = rIt.second;
    if (entry.isParsed()) continue;
    sendPicture(entry.id(), true);
  }
  for (auto rIt : m_state->m_idPICTMap) {
    MWAWEntry const &entry = rIt.second;
    if (entry.isParsed()) continue;
    sendPicture(entry.id(), false);
  }
#endif
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////


// the font name
bool EDocParser::readFontsName(MWAWEntry const &entry)
{
  long length = entry.length();
  if (!entry.valid() || (length%0x100)!=2) {
    MWAW_DEBUG_MSG(("EDocParser::readFontsName: the entry seems very short\n"));
    return false;
  }

  entry.setParsed(true);
  long pos = entry.begin();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(FontsName):";
  if (entry.id()!=128)
    f << "#id=" << entry.id() << ",";
  auto N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  if (N*0x100+2!=length) {
    MWAW_DEBUG_MSG(("EDocParser::readFontsName: the number of elements seems bad\n"));
    return false;
  }
  for (int i = 0; i < N; i++) {
    pos = input->tell();

    f.str("");
    f << "FontsName-" << i << ":";
    auto fSz=static_cast<int>(input->readULong(1));
    if (!fSz || fSz >= 255) {
      f << "##" << fSz << ",";
      MWAW_DEBUG_MSG(("EDocParser::readFontsName: the font name %d seems bad\n", i));
    }
    else {
      std::string name("");
      for (int c=0; c < fSz; c++)
        name += char(input->readULong(1));
      f << "\"" << name << "\",";
    }
    input->seek(pos+32, librevenge::RVNG_SEEK_SET);
    for (int j = 0; j < 112; j++) { // always 0 .
      auto val = static_cast<int>(input->readLong(2));
      if (val) f << "f" << j << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+0x100, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

// the index
bool EDocParser::sendIndex()
{
  if (!getTextListener()) {
    MWAW_DEBUG_MSG(("EDocParser::sendIndex: can not find the listener\n"));
    return false;
  }
  if (!m_state->m_indexList.size())
    return true;

  double w = getPageWidth();
  MWAWParagraph para;
  MWAWTabStop tab;
  tab.m_alignment = MWAWTabStop::RIGHT;
  tab.m_leaderCharacter='.';
  tab.m_position = w-0.3;

  para.m_tabs->push_back(tab);
  para.m_marginsUnit=librevenge::RVNG_INCH;

  MWAWFont cFont(3,10);
  cFont.setFlags(MWAWFont::boldBit);
  MWAWFont actFont(3,12);

  getTextListener()->insertEOL();
  std::stringstream ss;
  for (auto const &index : m_state->m_indexList) {
    para.m_margins[0] = 0.3*double(index.m_levelId+1);
    getTextListener()->setParagraph(para);
    getTextListener()->setFont(actFont);
    for (char c : index.m_text)
      getTextListener()->insertCharacter(static_cast<unsigned char>(c));

    if (index.m_page >= 0) {
      getTextListener()->setFont(cFont);
      getTextListener()->insertTab();
      ss.str("");
      ss << index.m_page;
      getTextListener()->insertUnicodeString(librevenge::RVNGString(ss.str().c_str()));
    }
    getTextListener()->insertEOL();
  }
  return true;
}

bool EDocParser::readIndex(MWAWEntry const &entry)
{
  long length = entry.length();
  if (!entry.valid() || length < 20) {
    MWAW_DEBUG_MSG(("EDocParser::readIndex: the entry seems very short\n"));
    return false;
  }

  entry.setParsed(true);
  long pos = entry.begin();
  long endPos = entry.end();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Index):";
  if (entry.id()!=128)
    f << "#id=" << entry.id() << ",";
  auto val=static_cast<int>(input->readULong(2));
  if (val) // 100 ?
    f << "f0=" << std::hex << val << std::dec << ",";
  auto N=static_cast<int>(input->readULong(2));
  f << "N=" << N << ",";
  for (int i = 0; i < 8; i++) { // always 0
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  if (N*14+20>length) {
    MWAW_DEBUG_MSG(("EDocParser::readIndex: the number of elements seems bad\n"));
    return false;
  }

  for (int i = 0; i < N; i++) {
    pos = input->tell();
    f.str("");
    if (pos+14 > endPos) {
      f << "Index-" << i << ":###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());

      MWAW_DEBUG_MSG(("EDocParser::readIndex: can not read index %d\n", i));
      return false;
    }
    EDocParserInternal::Index index;
    val = static_cast<int>(input->readULong(1)); // 0|80
    if (val) f << "fl=" << std::hex << val << std::dec << ",";
    index.m_levelId = static_cast<int>(input->readULong(1));
    index.m_page = static_cast<int>(input->readLong(2));
    // f1: y pos, other 0
    for (int j = 0; j < 4; j++) {
      val = static_cast<int>(input->readLong(2));
      if (val)
        f << "f" << j << "=" << val << ",";
    }
    auto fSz = static_cast<int>(input->readULong(1));
    if (pos+13+fSz > endPos) {
      index.m_extra=f.str();
      f.str("");
      f << "Index-" << i << ":" << index << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());

      MWAW_DEBUG_MSG(("EDocParser::readIndex: can not read index %d text\n", i));
      return false;
    }
    std::string text("");
    for (int j = 0; j < fSz; j++)
      text += char(input->readULong(1));
    index.m_text=text;
    index.m_extra=f.str();
    m_state->m_indexList.push_back(index);
    f.str("");
    f << "Index-" << i << ":" << index;
    if ((fSz%2)==0) //
      input->seek(1,librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

// the document information
bool EDocParser::readInfo(MWAWEntry const &entry)
{
  long length = entry.length();
  if (!entry.valid() || length < 0x68) {
    MWAW_DEBUG_MSG(("EDocParser::readInfo: the entry seems very short\n"));
    return false;
  }

  entry.setParsed(true);
  long pos = entry.begin();
  long endPos = entry.end();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Info):";
  if (entry.id()!=128)
    f << "#id=" << entry.id() << ",";
  int val;
  for (int i = 0; i < 4; i++) { // f0=0, other big number
    val = static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  // creator, file name
  for (int i = 0; i < 2; i++) {
    auto sz=static_cast<int>(input->readULong(1));
    if (sz > 31) {
      MWAW_DEBUG_MSG(("EDocParser::readInfo: can not read string %d\n", i));
      f << "###,";
    }
    else {
      std::string name("");
      for (int c=0; c < sz; c++)
        name += char(input->readULong(1));
      f << name << ",";
    }
    input->seek(pos+8+(i+1)*32, librevenge::RVNG_SEEK_SET);
  }
  for (int i = 0; i < 5; i++) { // always 4, 0, 210, 0, 0 ?
    val = static_cast<int>(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  int dim[2];
  for (auto &d : dim) d = static_cast<int>(input->readLong(2));
  f << "dim?=" << dim[1] << "x" << dim[0] << ",";
  if (dim[1]>100 && dim[1]<2000 && dim[0]>100 && dim[0]< 2000) {
    getPageSpan().setFormLength(double(dim[0])/72.);
    getPageSpan().setFormWidth(double(dim[1])/72.);
  }
  else {
    MWAW_DEBUG_MSG(("EDocParser::readInfo: the page dimension seems bad\n"));
    f << "###,";
  }
  auto N=static_cast<int>(input->readLong(2));
  f << "numPict?=" << N << ","; // seems ok in eDcR, but no in eSRD
  for (int i = 0; i < 2; i++) { // fl0=hasIndex ?, fl1=0
    val = static_cast<int>(input->readLong(1));
    if (val)
      f << "fl" << i << "=" << val << ",";
  }
  val = static_cast<int>(input->readLong(2)); // 0 or bf
  if (val)
    f << "g5=" << val << ",";
  for (int i = 0; i < 3; i++) { // 3 big number: some size?
    val = static_cast<int>(input->readULong(4));
    if (val)
      f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());
  if (input->tell() != endPos) {
    ascFile.addPos(input->tell());
    ascFile.addNote("Info(II)");
  }
  return true;
}

// code to uncompress data ( very low level)
namespace EDocParserInternal
{
//! very low structure to help uncompress data
struct DeflateStruct {
  //! constructor
  DeflateStruct(long size, long initSize)
    : m_toWrite(size)
    , m_data()
    , m_circQueue(0x2000,0)
    , m_circQueuePos(0)
    , m_numDelayed(0)
    , m_delayedChar('\0')
  {
    m_data.reserve(size_t(initSize));
  }
  //! true if we have build of the data
  bool isEnd() const
  {
    return m_toWrite <= 0;
  }
  //! push a new character
  bool push(unsigned char c)
  {
    if (m_toWrite <= 0) return false;
    m_circQueue[m_circQueuePos++]=c;
    if (m_circQueuePos==0x2000)
      m_circQueuePos=0;
    if (m_numDelayed)
      return treatDelayed(c);
    if (c==0x81 && m_toWrite!=1) {
      m_numDelayed++;
      return true;
    }
    m_delayedChar=c;
    m_data.push_back(c);
    m_toWrite--;
    return true;
  }
  //! send a duplicated part of the data
  bool sendDuplicated(int num, int depl);
  //! check if there is delayed char, if so treat them
  bool treatDelayed(unsigned char c);
  //! return the content of the block in dt
  bool getBinaryData(librevenge::RVNGBinaryData &dt) const
  {
    dt.clear();
    if (m_data.empty()) return false;
    unsigned char const *firstPos=&m_data[0];
    dt.append(firstPos, static_cast<unsigned long>(m_data.size()));
    return true;
  }
protected:
  //! the number of data that we need to write
  long m_toWrite;
  //! the resulting data
  std::vector<unsigned char> m_data;

  //! a circular queue
  std::vector<unsigned char> m_circQueue;
  //! the position in the circular queue
  size_t m_circQueuePos;
  //! the number of character delayed
  int m_numDelayed;
  //! the delayed character
  unsigned char m_delayedChar;
private:
  DeflateStruct(DeflateStruct const &orig) = delete;
  DeflateStruct &operator=(DeflateStruct const &orig) = delete;
};

bool DeflateStruct::sendDuplicated(int num, int depl)
{
  int readPos=int(m_circQueuePos)+depl;
  while (readPos < 0) readPos+=0x2000;
  while (readPos >= 0x2000) readPos-=0x2000;

  while (num-->0) {
    push(m_circQueue[size_t(readPos++)]);
    if (readPos==0x2000)
      readPos=0;
  }
  return true;
}
bool DeflateStruct::treatDelayed(unsigned char c)
{
  if (m_toWrite <= 0)
    return false;
  if (m_numDelayed==1) {
    if (c==0x82) {
      m_numDelayed++;
      return true;
    }
    m_delayedChar=0x81;
    m_data.push_back(m_delayedChar);
    if (--m_toWrite==0) return true;
    if (c==0x81 && m_toWrite==1)
      return true;
    m_numDelayed=0;
    m_delayedChar=c;
    m_data.push_back(c);
    m_toWrite--;
    return true;
  }

  m_numDelayed=0;
  if (c==0) {
    m_data.push_back(0x81);
    if (--m_toWrite==0) return true;
    m_delayedChar=0x82;
    m_data.push_back(m_delayedChar);
    m_toWrite--;
    return true;
  }
  if (c-1 > m_toWrite) return false;
  for (int i = 0; i < int(c-1); i++)
    m_data.push_back(m_delayedChar);
  m_toWrite -= (c-1);
  return true;
}
}

bool EDocParser::decodeZone(MWAWEntry const &entry, librevenge::RVNGBinaryData &data)
{
  data.clear();
  long length = entry.length();
  if (!entry.valid() || length<0x21+12) {
    MWAW_DEBUG_MSG(("EDocParser::decodeZone: the entry seems very short\n"));
    return false;
  }
  entry.setParsed(true);
  long pos = entry.begin();
  long endPos = entry.end();
  MWAWInputStreamPtr input = rsrcInput();
  libmwaw::DebugFile &ascFile = rsrcAscii();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Entries(CompressZone):";
  if (long(input->readULong(4))!=length) {
    MWAW_DEBUG_MSG(("EDocParser::decodeZone: unexpected zone size\n"));
    return false;
  }
  auto zoneSize=long(input->readULong(4));
  f << "sz[final]=" << std::hex << zoneSize << std::dec << ",";

  if (!zoneSize) {
    MWAW_DEBUG_MSG(("EDocParser::decodeZone: unexpected final zone size\n"));
    return false;
  }
  f << "checkSum=" << std::hex << input->readULong(4) << std::dec << ",";

  ascFile.addPos(pos-4);
  ascFile.addNote(f.str().c_str());

  // make an initial size estimate to avoid big allocation in case zoneSize is damaged
  const long maxInputSize = input->size() - input->tell();
  const long initSize = (zoneSize / 4 > maxInputSize) ? 4 * maxInputSize : zoneSize;
  EDocParserInternal::DeflateStruct deflate(zoneSize, initSize);
  int const maxData[]= {0x80, 0x20, 0x40};
  int val;

  while (!deflate.isEnd() && input->tell() < endPos-3) {
    // only find a simple compress zone but seems ok to have more
    std::vector<unsigned char> vectors32K[3];
    std::vector<unsigned char> originalValues[3];
    for (int st=0; st < 3; st++) {
      pos=input->tell();
      f.str("");
      f << "CompressZone[data" << st << "]:";
      auto num=static_cast<int>(input->readULong(1));
      f << "num=" << num << ",";
      if (num > maxData[st] || pos+1+num > endPos) {
        MWAW_DEBUG_MSG(("EDocParser::decodeZone: find unexpected num of data : %d for zone %d\n", num, st));
        f << "###";

        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return false;
      }
      std::multimap<int,int> mapData;
      originalValues[st].resize(size_t(maxData[st])*2, 0);
      for (int i = 0; i < num; i++) {
        val=static_cast<int>(input->readULong(1));
        for (int b=0; b < 2; b++) {
          int byte= b==0 ? (val>>4) : (val&0xF);
          originalValues[st][size_t(2*i+b)]=static_cast<unsigned char>(byte);
          if (byte==0)
            continue;
          mapData.insert(std::multimap<int,int>::value_type(byte,2*i+b));
        }
      }
      vectors32K[st].resize(0x8000,0);
      int writePos=0;
      for (auto it : mapData) {
        int n=0x8000>>(it.first);
        if (writePos+n>0x8000) {
          MWAW_DEBUG_MSG(("EDocParser::decodeZone: find unexpected value writePos=%x for zone %d\n",static_cast<unsigned int>(writePos+n), st));

          f << "###";

          ascFile.addPos(pos);
          ascFile.addNote(f.str().c_str());
          return false;
        }
        for (int j = 0; j < n; j++)
          vectors32K[st][size_t(writePos++)]=static_cast<unsigned char>(it.second);
      }

      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    pos = input->tell();
    int byte=0;
    long maxBlockSz=0xFFF0;
    unsigned int value=(static_cast<unsigned int>(input->readULong(2)))<<16;
    while (maxBlockSz) {
      if (deflate.isEnd() || input->tell()>endPos) break;
      int ind0=(value>>16);
      if (ind0 & 0x8000) {
        auto ind1 = static_cast<int>(vectors32K[0][size_t(ind0&0x7FFF)]);
        int byt1=originalValues[0][size_t(ind1)]+1;
        if (byte<byt1) {
          value = (value<<byte);
          byt1 -= byte;
          value |= static_cast<unsigned int>(input->readULong(2));
          byte=16;
        }
        value=(value<<byt1);
        byte-=byt1;

        deflate.push(static_cast<unsigned char>(ind1));
        maxBlockSz-=2;
        continue;
      }

      auto ind1 = static_cast<int>(vectors32K[1][size_t(ind0)]);
      int byt1 = originalValues[1][size_t(ind1)]+1;
      if (byte<byt1) {
        value = (value<<byte);
        byt1 -= byte;
        value |= static_cast<unsigned int>(input->readULong(2));
        byte=16;
      }
      value=(value<<byt1);
      byte-=byt1;
      auto ind2 = static_cast<int>(vectors32K[2][size_t(value>>17)]);
      int byt2=originalValues[2][size_t(ind2)];
      if (byte<byt2) {
        value = (value<<byte);
        byt2 -= byte;
        value |= static_cast<unsigned int>(input->readULong(2));
        byte=16;
      }
      value=(value<<byt2);
      byte-=byt2;

      ind2=int(value>>26) | (ind2<<6);
      int byt3=6;
      if (byte<byt3) {
        value = (value<<byte);
        byt3 -= byte;
        value |= static_cast<unsigned int>(input->readULong(2));
        byte=16;
      }
      value=(value<<byt3);
      byte-=byt3;
      deflate.sendDuplicated(ind1, -ind2);
      maxBlockSz-=3;
    }
  }

  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("EDocParser::decodeZone: unexpected end of data\n"));
    ascFile.addPos(input->tell());
    ascFile.addNote("CompressZone[after]");
  }
  bool res = deflate.getBinaryData(data);
  ascFile.skipZone(pos,input->tell()-1);
#if defined(DEBUG_WITH_FILES)
  if (res) {
    static int volatile cPictName = 0;
    libmwaw::DebugStream f2;
    f2 << "CPICT" << ++cPictName << ".pct";
    libmwaw::Debug::dumpFile(data, f2.str().c_str());
  }
#endif
  return res;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool EDocParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = EDocParserInternal::State();
  /** no data fork, may be ok, but this means
      that the file contains no text, so... */
  MWAWInputStreamPtr input = getInput();
  if (!input || !getRSRCParser())
    return false;
  if (input->hasDataFork()) {
    MWAW_DEBUG_MSG(("EDocParser::checkHeader: find a datafork, odd!!!\n"));
  }
  if (strict) {
    // check that the fontname zone exists
    auto &entryMap = getRSRCParser()->getEntriesMap();
    if (entryMap.find("eDcF") == entryMap.end())
      return false;
  }
  if (header)
    header->reset(MWAWDocument::MWAW_T_EDOC, version());

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
