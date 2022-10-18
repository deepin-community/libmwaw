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
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWCell.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWHeader.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPictMac.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWStream.hxx"
#include "MWAWStringStream.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTextListener.hxx"

#include "MacWrtProStructures.hxx"

#include "MacWrtProParser.hxx"

/** Internal: the structures of a MacWrtProParser */
namespace MacWrtProParserInternal
{
////////////////////////////////////////
//! Internal: a struct used to store a zone
struct Zone {
  Zone()
    : m_type(-1)
    , m_blockId(0)
    , m_stream()
    , m_parsed(false)
  {
  }
  ~Zone()
  {
  }

  //! the type : 0(text), 1(graphic)
  int m_type;

  //! the first block id
  int m_blockId;

  //! the storage
  std::shared_ptr<MWAWStream> m_stream;

  //! true if the zone is sended
  bool m_parsed;
};

//! Internal: a struct used to store a text zone
struct TextZoneData {
  TextZoneData()
    : m_type(-1)
    , m_length(0)
    , m_id(0)
  {
  }
  friend std::ostream &operator<<(std::ostream &o, TextZoneData const &tData)
  {
    switch (tData.m_type) {
    case 0:
      o << "C" << tData.m_id << ",";
      break;
    case 1:
      o << "P" << tData.m_id << ",";
      break;
    default:
      o << "type=" << tData.m_type << ",id=" << tData.m_id << ",";
      break;
    }
    o << "nC=" << tData.m_length << ",";
    return o;
  }
  //! the type
  int m_type;
  //! the text length
  int m_length;
  //! an id
  int m_id;
};

//! Internal: a struct used to store a text zone
struct Token {
  Token()
    : m_type(-1)
    , m_length(0)
    , m_blockId(-1)
    , m_box()
  {
    for (auto &fl : m_flags) fl = 0;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Token const &tkn)
  {
    o << "nC=" << tkn.m_length << ",";
    switch (tkn.m_type) {
    case 1:
      o << "pagenumber,";
      break;
    case 2:
      o << "footnote(pos),";
      break;
    case 3:
      o << "footnote(content),";
      break;
    case 4:
      o << "figure,";
      break;
    case 5:
      o << "hyphen,";
      break;
    case 6:
      o << "date,";
      break;
    case 7:
      o << "time,";
      break;
    case 8:
      o << "title,";
      break;
    case 9:
      o << "revision,";
      break;
    case 10:
      o << "sectionnumber,";
      break;
    default:
      o << "#type=" << tkn.m_type << ",";
    }
    if (tkn.m_blockId >= 0) o << "blockId=" << tkn.m_blockId << ",";
    for (int i = 0; i < 4; i++) {
      if (tkn.m_flags[i]) o << "fl" << i << "=" << std::hex << tkn.m_flags[i] << ",";
    }
    return o;
  }
  //! the type
  int m_type;
  //! the text length
  int m_length;
  //! the block id
  int m_blockId;
  //! the bdbox ( filled in MWII for figure)
  MWAWBox2f m_box;
  //! some flags
  unsigned int m_flags[4];
};

//! Internal: a struct used to store a text zone
struct TextZone {
  TextZone()
    : m_textLength(0)
    , m_entries()
    , m_tokens()
    , m_parsed(false)
  {
  }

  //! the text length
  int m_textLength;

  //! the list of entries
  std::vector<MWAWEntry> m_entries;

  //! two vector list of id ( charIds, paragraphIds)
  std::vector<TextZoneData> m_ids[2];

  //! the tokens list
  std::vector<Token> m_tokens;

  //! true if the zone is sended
  bool m_parsed;
};


////////////////////////////////////////
//! Internal: the state of a MacWrtProParser
struct State {
  //! constructor
  State()
    : m_parsedBlocks()
    , m_dataMap()
    , m_textMap()
    , m_graphicIdsCallByTokens()
    , m_fileNumPages(0)
    , m_col(1)
    , m_colSeparator(0.16667)
    , m_actPage(0)
    , m_numPages(0)
    , m_hasTitlePage(false)
  {
  }

  //! the list of retrieved block : block
  std::set<int> m_parsedBlocks;

  //! the list of blockId->data zone
  std::map<int, std::shared_ptr<Zone> > m_dataMap;

  //! the list of blockId->text zone
  std::map<int, std::shared_ptr<TextZone> > m_textMap;

  //! the list of graphicId called by tokens
  std::vector<int> m_graphicIdsCallByTokens;

  int m_fileNumPages /** the number of page in MWII */;
  int m_col /** the number of columns in MWII */;
  double m_colSeparator /** the columns separator in inch MWII */;
  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;
  bool m_hasTitlePage /** flag to know if we have a title page */;
};

////////////////////////////////////////
//! Internal: the subdocument of a MacWrtProParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(MacWrtProParser &pars, MWAWInputStreamPtr const &input, int zoneId)
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
  if (m_id == -3) return; // empty block
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("MacWrtProParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser = dynamic_cast<MacWrtProParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("MacWrtProParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  if (parser->m_structures.get())
    parser->m_structures->send(m_id);
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
MacWrtProParser::MacWrtProParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
  , m_structures()
{
  init();
}

MacWrtProParser::~MacWrtProParser()
{
}

void MacWrtProParser::init()
{
  resetTextListener();
  setAsciiName("main-1");

  m_state.reset(new MacWrtProParserInternal::State);
  m_structures.reset(new MacWrtProStructures(*this));

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// position and height
////////////////////////////////////////////////////////////
int MacWrtProParser::numColumns(double &sep) const
{
  sep=m_state->m_colSeparator;
  if (m_state->m_col <= 1) return 1;
  return m_state->m_col;
}

////////////////////////////////////////////////////////////
// new page
////////////////////////////////////////////////////////////
void MacWrtProParser::newPage(int number, bool softBreak)
{
  if (number <= m_state->m_actPage) return;
  if (number > m_state->m_numPages) {
    MWAW_DEBUG_MSG(("MacWrtProParser::newPage: can not create new page\n"));
    return;
  }

  while (m_state->m_actPage < number) {
    m_state->m_actPage++;
    if (!getTextListener() || m_state->m_actPage == 1)
      continue;
    if (softBreak)
      getTextListener()->insertBreak(MWAWTextListener::SoftPageBreak);
    else
      getTextListener()->insertBreak(MWAWTextListener::PageBreak);
  }
}

bool MacWrtProParser::hasTitlePage() const
{
  return m_state->m_hasTitlePage;
}

std::vector<int> const &MacWrtProParser::getGraphicIdCalledByToken() const
{
  return m_state->m_graphicIdsCallByTokens;
}

std::shared_ptr<MWAWSubDocument> MacWrtProParser::getSubDocument(int blockId)
{
  return std::make_shared<MacWrtProParserInternal::SubDocument>(*this, getInput(), blockId);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MacWrtProParser::parse(librevenge::RVNGTextInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = true;
  try {
    m_state->m_parsedBlocks.clear();

    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());

    checkHeader(nullptr);

    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      if (m_structures) {
        m_structures->sendMainZone();
        m_structures->flushExtra();
      }
    }

#ifdef DEBUG
    if (version()>0) {
      std::vector<int> freeList;
      getFreeZoneList(freeList);
      for (auto bl : freeList) {
        ascii().addPos((bl-1)*0x100);
        ascii().addNote("Entries(Free)");
      }
    }
    checkUnparsed();
#endif

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MacWrtProParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// stream (internal)
////////////////////////////////////////////////////////////
std::shared_ptr<MWAWStream> MacWrtProParser::getStreamForBlock(int block)
{
  MWAWInputStreamPtr input=getInput();
  std::shared_ptr<MWAWStream> res;
  int const vers=version();
  if (block<3 || !input->checkPosition(0x100*block)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::getStreamForBlock: unexpected block=%d\n", block));
    return res;
  }
  int const linkSz = vers<= 0 ? 2 : 4;
  input->seek(0x100*block-linkSz, librevenge::RVNG_SEEK_SET);
  if (input->readULong(linkSz)==0) {
    m_state->m_parsedBlocks.insert(block-1);
    input->seek(0x100*(block-1), librevenge::RVNG_SEEK_SET);
    res.reset(new MWAWStream(input, ascii()));
    res->m_bof=0x100*(block-1);
    res->m_eof=0x100*block-linkSz;
    return res;
  }
  int const fBlock=block;
  int numBlock=1, decal=0;
  std::shared_ptr<MWAWStringStream> resInput;
  while (true) {
    --block; // block i : (i-1)00..i00
    if (numBlock<=0 || block<2 || block+numBlock<0 || !input->checkPosition(0x100*(block+numBlock))) {
      MWAW_DEBUG_MSG(("MacWrtProParser::getStreamForBlock: can not read some block %dx%d\n", block, numBlock));
      break;
    }
    bool ok=true;
    for (int bl=block; bl<block+numBlock; ++bl) {
      if (m_state->m_parsedBlocks.find(bl)!=m_state->m_parsedBlocks.end()) {
        MWAW_DEBUG_MSG(("MacWrtProParser::getStreamForBlock: block %d is already m_state->m_parsedBlockss\n", bl));
        ok=false;
        break;
      }
      m_state->m_parsedBlocks.insert(bl);
    }
    if (!ok) break;
    ascii().skipZone(0x100*block, 0x100*(block+numBlock)-1);
    input->seek(0x100*block+decal, librevenge::RVNG_SEEK_SET);
    unsigned long read;
    unsigned long sz=static_cast<unsigned long>(0x100*numBlock-linkSz-decal);
    const unsigned char *dt = input->read(sz, read);
    if (!dt || read != sz) {
      MWAW_DEBUG_MSG(("MacWrtProParser::getStreamForBlock: can not read some data\n"));
      break;
    }
    if (!resInput)
      resInput.reset(new MWAWStringStream(dt, unsigned(sz)));
    else
      resInput->append(dt, unsigned(sz));
    decal=0;
    numBlock=1;
    block=int(input->readLong(linkSz));
    if (block==0) break;
    if (block<0) {
      block*=-1;
      if (block<3 || !input->checkPosition(0x100*(block-1)+linkSz)) {
        MWAW_DEBUG_MSG(("MacWrtProParser::getStreamForBlock: bad block %d\n", block));
        break;
      }
      input->seek(0x100*(block-1), librevenge::RVNG_SEEK_SET);
      numBlock=int(input->readULong(linkSz));
      decal=linkSz;
    }
  }
  if (!resInput) return res;
  res.reset(new MWAWStream(std::make_shared<MWAWInputStream>(resInput, false)));
  std::stringstream s;
  s << "DataZone" << std::hex << fBlock << std::dec;
  res->m_ascii.open(s.str().c_str());
  res->m_input->seek(0, librevenge::RVNG_SEEK_SET);
  return res;
}

////////////////////////////////////////////////////////////
// return the chain list of block ( used to get free blocks)
////////////////////////////////////////////////////////////
bool MacWrtProParser::getFreeZoneList(std::vector<int> &blockLists)
{
  blockLists.clear();
  MWAWInputStreamPtr input = getInput();
  if (!input->checkPosition(0x200) || version() <= 0)
    return false;
  input->seek(0x200-4, librevenge::RVNG_SEEK_SET);
  int blockId=int(input->readULong(4));
  if (!blockId) return true;

  if (blockId<2 || !input->checkPosition(blockId*0x100)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::getFreeZoneList: find a bad free block=%x\n", unsigned(blockId)));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Free):";
  input->seek((blockId-1)*0x100, librevenge::RVNG_SEEK_SET);
  // FIXME: use (blockId)*0x100-4 to get the complete list when there is more than 63/64 free blocks
  // Note: the different blocks seems linked together N free block -> N-1 free block -> ...
  for (int b=0; b<63; ++b) { // checkme limit=63 or 64
    int bId=int(input->readULong(4));
    if (bId==0) break;
    if (bId<2 || !input->checkPosition(bId*0x100) || m_state->m_parsedBlocks.find(bId-1)!=m_state->m_parsedBlocks.end()) {
      MWAW_DEBUG_MSG(("MacWrtProParser::getFreeZoneList: find a bad block %x\n", unsigned(bId)));
      f << "###" << std::hex << bId << std::dec << ",";
      break;
    }
    f << std::hex << bId << std::dec << ",";
    blockLists.push_back(bId);
    m_state->m_parsedBlocks.insert(bId-1);
  }
  ascii().addPos((blockId-1)*0x100);
  ascii().addNote(f.str().c_str());
  if (input->tell()!=blockId*0x100) ascii().addDelimiter(input->tell(),'|');
  return blockLists.size() != 0;
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MacWrtProParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::createDocument: listener already exist\n"));
    return;
  }

  // update the page
  m_state->m_actPage = 0;
  int numPages = m_structures ? m_structures->numPages() : 0;
  if (numPages <= 0) numPages = 1;
  m_state->m_numPages = numPages;

  // create the page list
  std::vector<MWAWPageSpan> pageList;
  for (int i = 0; i < m_state->m_numPages;) {
    MWAWPageSpan ps(getPageSpan());
    m_structures->updatePageSpan(i, m_state->m_hasTitlePage, ps);
    pageList.push_back(ps);
    i+=std::max<int>(1,ps.getPageSpan());
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
bool MacWrtProParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();

  if (!readPrintInfo()) {
    // can happens in some valid file
    ascii().addPos(pos);
    ascii().addNote("Entries(PrintInfo):###");
    input->seek(pos+0x78, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  if (!readDocHeader()) {
    ascii().addPos(pos);
    ascii().addNote("##Entries(Data0)");
  }

  // ok now ask the structure manager to retrieve its data
  auto stream=getStreamForBlock(3);
  if (!stream)
    return false;
  return m_structures->createZones(stream, m_state->m_fileNumPages);
}

////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool MacWrtProParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = MacWrtProParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  libmwaw::DebugStream f;
  int const headerSize=4;
  if (!input->checkPosition(0x300)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);

  auto vers = static_cast<int>(input->readULong(2));
  auto val = static_cast<int>(input->readULong(2));

  f << "FileHeader:";
  switch (vers) {
  case 0x2e:
    vers = 0;
    if (val != 0x2e)
      return false;
    break;
  case 4:
    vers = 1;
    if (val != 4) {
#ifdef DEBUG
      if (strict || val < 3 || val > 5)
        return false;
      f << "#unk=" << val << ",";
#else
      return false;
#endif
    }
    break;
  default:
    MWAW_DEBUG_MSG(("MacWrtProParser::checkHeader: unknown version\n"));
    return false;
  }
  setVersion(vers);
  f << "vers=" << vers << ",";
  if (strict) {
    if (vers) {
      input->seek(0xdd, librevenge::RVNG_SEEK_SET);
      // "MP" seems always in this position
      if (input->readULong(2) != 0x4d50)
        return false;
    }
    else if (!readPrintInfo()) { // last chance, check DocHeader
      input->seek(4+0x78+2, librevenge::RVNG_SEEK_SET);
      val=static_cast<int>(input->readULong(2));
      if ((val&0x0280)!=0x0280) return false;
      for (int i=0; i<4; ++i) {
        val=static_cast<int>(input->readLong(1));
        if (val<-1 || val>1) return false;
      }
    }
  }


  // ok, we can finish initialization
  if (header)
    header->reset(MWAWDocument::MWAW_T_MACWRITEPRO, version());

  //
  input->seek(headerSize, librevenge::RVNG_SEEK_SET);

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  ascii().addPos(headerSize);

  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool MacWrtProParser::readPrintInfo()
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
  int botMarg = rBotMargin.y() -10;
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
    MWAW_DEBUG_MSG(("MacWrtProParser::readPrintInfo: file is too short\n"));
    return false;
  }
  ascii().addPos(input->tell());

  return true;
}

////////////////////////////////////////////////////////////
// read the document header
////////////////////////////////////////////////////////////
bool MacWrtProParser::readDocHeader()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  libmwaw::DebugStream f;

  f << "Entries(Data0):";
  long val;
  if (version()==0) {
    val = input->readLong(2); // always 0 ?
    if (val) f << "f0=" << val << ",";
    /* fl0=[2|6|82|86], fl1=[80|a0|a4], other 0|1|-1 */
    for (int i = 0; i < 9; i++) {
      val = (i<2) ? int(input->readULong(1)) : input->readLong(1);
      if (i==0 && (val&4)) {
        f << "has[titlePage],";
        m_state->m_hasTitlePage=true;
        val &= 0xfb; // 2 or 82
      }
      if (!val) continue;
      if (i < 2)
        f << "fl" << i << "=" << std::hex << val << std::dec << ",";
      else
        f << "fl" << i << "=" << val << ",";
    }
    val = input->readLong(2); // always 612 ?
    if (val != 0x612) f << "f1=" << val << ",";
    val = input->readLong(1); // always 1 ?
    if (val != 1) f << "f2=" << val << ",";
    val = input->readLong(2); // always 2 ?
    if (val != 2) f << "f3=" << val << ",";
    val = input->readLong(2); // always 12c ?
    if (val != 0x12c) f << "f4=" << val << ",";
    for (int i = 0; i < 3; i++) { // 0, 0, 3c, a small number
      val = input->readLong(2);
      if (val) f << "g" << i << "=" << val << ",";
    }
    m_state->m_fileNumPages = int(input->readLong(2));
    if (m_state->m_fileNumPages!=1)
      f << "num[pages]=" << m_state->m_fileNumPages << ",";
    /* then
      0009000020000000fd803333000600000000000120 |
      000c000020000000fd803333000600000000000180 |
      000c000020000000fd8033330006000000000001a0 |
      000c0000200e0000fd8033330006000000000001a0 |
      00240000200e0000fd8033330006000000000001a0

      and
      000001000000016f66000000000000000800090001000000
     */
  }
  else {
    val = input->readLong(1); // always 0 ?
    if (val) f << "unkn=" << val << ",";
    auto N=static_cast<int>(input->readLong(2)); // find 2, a, 9e, 1a
    f << "N?=" << N << ",";
    N = static_cast<int>(input->readLong(1)); // almost always 0, find one time 6 ?
    if (N) f << "N1?=" << N << ",";
    val = static_cast<int>(input->readLong(2)); // almost always 0x622, find also 0 and 12
    f << "f0=" << std::hex << val << std::dec << ",";
    val = static_cast<int>(input->readLong(1)); // always 0 ?
    if (val) f << "unkn1=" << val << ",";
    N = static_cast<int>(input->readLong(2));
    f << "N2?=" << N << ",";
    val = input->readLong(1); // almost always 1 ( find one time 2)
    f << "f1=" << val << ",";
    int const defVal[] = { 0x64, 0/*small number between 1 and 8*/, 0x24 };
    for (int i = 0; i < 3; i++) {
      val = input->readLong(2);
      if (i==1) {
        m_state->m_fileNumPages = int(val);
        if (m_state->m_fileNumPages!=1)
          f << "num[pages]=" << val << ",";
        continue;
      }
      if (val != defVal[i])
        f << "f" << i+2 << "=" << val << ",";
    }
    for (int i = 5; i < 10; i++) { // always 0 ?
      val = input->readLong(1);
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    val = input->readLong(2); // always 480 ?
    if (val != 0x480) f << "f10=" << val << ",";
    val = long(input->readULong(1)); // always 0 ?
    if (val) f << "f11=" << val << ",";
  }
  float dim[6];
  bool ok = true;
  for (auto &d : dim) {
    d = float(input->readLong(4))/65356.f;
    if (d<0) ok=false;
  }
  if (ok) ok = dim[0] > dim[2]+dim[3] && dim[1] > dim[4]+dim[5];

  if (ok) {
    getPageSpan().setMarginTop(double(dim[2])/72.0);
    getPageSpan().setMarginLeft(double(dim[4])/72.0);
    /* decrease a little the right/bottom margin to allow fonts discrepancy*/
    getPageSpan().setMarginBottom((dim[3]<36) ? 0.0 : double(dim[3])/72.0-0.1);
    getPageSpan().setMarginRight((dim[5]<18) ? 0.0 : double(dim[5])/72.0-0.1);
    getPageSpan().setFormLength(double(dim[0])/72.);
    getPageSpan().setFormWidth(double(dim[1])/72.);
  }
  else {
    MWAW_DEBUG_MSG(("MacWrtProParser::readDocHeader: find odd page dimensions, ignored\n"));
    f << "#";
  }
  f << "dim=" << dim[1] << "x" << dim[0] << ",";
  f << "margins=["; // top, bottom, left, right
  for (int i = 2; i < 6; i++) f << dim[i] << ",";
  f << "],";
  if (version()==0) {
    m_state->m_col = static_cast<int>(input->readLong(2));
    if (m_state->m_col != 1) f << "col=" << m_state->m_col << ",";
    m_state->m_colSeparator=double(input->readLong(4))/65536./72.;
    f << "col[sep]=" << m_state->m_colSeparator << "in,";
  }

  ascii().addDelimiter(input->tell(), '|');
  if (version()>=1) {
    /** then find
        000000fd0000000000018200000100002f00
        44[40|80] followed by something like a7c3ec07|a7c4c3c6 : 2 date
        6f6600000000000000080009000105050506010401
    */
    input->seek(20, librevenge::RVNG_SEEK_CUR);
    ascii().addDelimiter(input->tell(), '|');
    for (int i=0; i<2; ++i)
      f << "date" << i << "=" << convertDateToDebugString(unsigned(input->readULong(4)));
    ascii().addDelimiter(input->tell(), '|');
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  f.str("");
  f << "Data0-A:";
  if (version()==0) {
    input->seek(pos+120, librevenge::RVNG_SEEK_SET);
    pos = input->tell();
    f << "selection=[" << input->readULong(4) << "x" << input->readULong(4) << ",";
    for (int i=0; i<2; ++i) {
      val = long(input->readULong(2));
      if (!val) continue;
      f << (i==0 ? "zone" : "pg") << "=" << val << ",";
    }
    f << "],";
  }
  else {
    input->seek(pos+97, librevenge::RVNG_SEEK_SET);
    pos = input->tell();
    val = long(input->readULong(2));
    if (val != 0x4d50) // MP
      f << "#keyWord=" << std::hex << val <<std::dec;
    //always 4, 4, 6 ?
    for (int i = 0; i < 3; i++) {
      val = input->readLong(1);
      if ((i==2 && val!=6) || (i < 2 && val != 4))
        f << "f" << i << "=" << val << ",";
    }
    for (int i = 3; i < 9; i++) { // always 0 ?
      val = input->readLong(2);
      if (val) f << "f"  << i << "=" << val << ",";
    }
  }
  // some dim ?
  f << "dim=[";
  for (int i = 0; i < 4; i++)
    f << input->readLong(2) << ",";
  f << "],";
  // always 0x48 0x48
  for (int i = 0; i < 2; i++) {
    val = input->readLong(2);
    if (val != 0x48) f << "g"  << i << "=" << val << ",";
  }
  // always 0 ?
  for (int i = 2; i < 42; i++) {
    val = long(input->readULong(2));
    if (val) f << "g"  << i << "=" << std::hex << val << std::dec << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // then junk ? (ie. find a string portion, a list of 0...),
  pos = input->tell();
  f.str("");
  f << "Data0-B:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  // interesting data seems to begin again in 0x200...
  input->seek(0x200, librevenge::RVNG_SEEK_SET);
  ascii().addPos(input->tell());
  ascii().addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
// try to parse a data zone
////////////////////////////////////////////////////////////
bool MacWrtProParser::parseDataZone(int blockId, int type)
{
  if (m_state->m_dataMap.find(blockId) != m_state->m_dataMap.end())
    return true;
  if (blockId < 1) {
    MWAW_DEBUG_MSG(("MacWrtProParser::parseDataZone: block %d seems bad\n", blockId));
    return false;
  }
  if (m_state->m_parsedBlocks.find(blockId-1) != m_state->m_parsedBlocks.end()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::parseDataZone: block %d is already parsed\n", blockId));
    return false;
  }

  auto input=getInput();
  long pos=input->tell();
  std::shared_ptr<MacWrtProParserInternal::Zone> zone(new MacWrtProParserInternal::Zone);
  zone->m_blockId = blockId;
  zone->m_type = type;
  auto &stream = zone->m_stream = getStreamForBlock(blockId);
  if (!stream)
    return false;
  m_state->m_dataMap[blockId] = zone;

  // ok init is done
  if (type == 0)
    parseTextZone(zone);
  else if (type == 1)
    ;
  else {
    libmwaw::DebugStream f;
    f << "Entries(DataZone):type" << type;
    stream->m_ascii.addPos(stream->m_input->tell());
    stream->m_ascii.addNote(f.str().c_str());
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MacWrtProParser::parseTextZone(std::shared_ptr<MacWrtProParserInternal::Zone> zone)
{
  if (!zone || !zone->m_stream) return false;
  if (zone->m_type != 0) {
    MWAW_DEBUG_MSG(("MacWrtProParser::parseTextZone: not a text zone\n"));
    return false;
  }

  auto &stream = zone->m_stream;
  MWAWInputStreamPtr input = stream->m_input;
  MWAWInputStreamPtr fileInput = getInput();
  libmwaw::DebugFile &asciiFile = stream->m_ascii;
  libmwaw::DebugStream f;

  std::shared_ptr<MacWrtProParserInternal::TextZone> text(new MacWrtProParserInternal::TextZone);

  long pos = input->tell();
  f << "Entries(TextZone):";
  text->m_textLength = static_cast<int>(input->readLong(4));
  f << "textLength=" << text->m_textLength << ",";

  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  if (!readTextEntries(zone, text->m_entries, text->m_textLength))
    return false;
  m_state->m_textMap[zone->m_blockId] = text;

  int n=0;
  for (auto &entry : text->m_entries) {
    n++;
    if (!fileInput->checkPosition(entry.begin())) {
      MWAW_DEBUG_MSG(("MacWrtProParser::parseTextZone: bad block id for block %ld\n", long(n-1)));
      entry.setBegin(-1);
    }
  }
  for (int i = 0; i < 2; i++) {
    if (!readTextIds(zone, text->m_ids[i], text->m_textLength, i))
      return true;
  }

  if (!readTextTokens(zone, text->m_tokens, text->m_textLength))
    return true;

  asciiFile.addPos(input->tell());
  asciiFile.addNote("TextZone(end)");

  return true;
}

bool MacWrtProParser::readTextEntries(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                                      std::vector<MWAWEntry> &res, int textLength)
{
  res.resize(0);
  int vers = version();
  int expectedSize = vers == 0 ? 4 : 6;
  auto &stream=zone->m_stream;
  MWAWInputStreamPtr input = stream->m_input;
  libmwaw::DebugFile &asciiFile = stream->m_ascii;
  libmwaw::DebugStream f;
  long pos = input->tell();

  auto sz = static_cast<int>(input->readULong(4));
  long endPos = pos+sz+4;
  if ((sz%expectedSize) != 0 || pos+sz<pos || !stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::readTextEntries: find an odd size\n"));
    return false;
  }

  int numElt = sz/expectedSize;
  f << "TextZone:entry(header),N=" << numElt << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  int remainLength = textLength;
  for (int i = 0; i < numElt; i++) {
    pos = input->tell();
    f.str("");
    f << "TextZone-" << i << ":entry,";
    int unkn = 0;
    if (vers >= 1) {
      unkn = static_cast<int>(input->readLong(2));
      if (unkn) f << "unkn=" << unkn << ",";
    }
    auto bl = static_cast<int>(input->readLong(2));
    f << "block=" << std::hex << bl << std::dec << ",";
    auto nChar = static_cast<int>(input->readULong(2));
    f << "blockSz=" << nChar;

    if (nChar > remainLength || nChar > 256) {
      MWAW_DEBUG_MSG(("MacWrtProParser::readTextEntries: bad size for block %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    remainLength -= nChar;
    bool ok = bl >= 3 && m_state->m_parsedBlocks.find(bl-1) == m_state->m_parsedBlocks.end();
    if (!ok) {
      MWAW_DEBUG_MSG(("MacWrtProParser::readTextEntries: bad block id for block %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }

    m_state->m_parsedBlocks.insert(bl-1);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    if (nChar==0) continue;

    MWAWEntry entry;
    entry.setId(unkn);
    entry.setBegin((bl-1)*0x100);
    entry.setLength(nChar);
    res.push_back(entry);
  }

  if (remainLength) {
    MWAW_DEBUG_MSG(("MacWrtProParser::readTextEntries: can not find %d characters\n", remainLength));
    asciiFile.addPos(input->tell());
    asciiFile.addNote("TextEntry-#");
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool MacWrtProParser::readTextIds(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                                  std::vector<MacWrtProParserInternal::TextZoneData> &res,
                                  int textLength, int type)
{
  res.resize(0);
  auto &stream=zone->m_stream;
  MWAWInputStreamPtr input = stream->m_input;
  libmwaw::DebugFile &asciiFile = stream->m_ascii;
  libmwaw::DebugStream f;
  long pos = input->tell();

  auto val = static_cast<int>(input->readULong(2));
  auto sz = static_cast<int>(input->readULong(2));
  if (sz == 0) {
    asciiFile.addPos(pos);
    asciiFile.addNote("_");
    return true;
  }

  long endPos = pos+sz+4;
  if ((sz%6) != 0 || !stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::readTextIds: find an odd size\n"));
    return false;
  }

  int numElt = sz/6;
  f << "TextZone:type=" << type << "(header),N=" << numElt << ",";
  if (val) f << "unkn=" << val << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  long remainLength = textLength;
  for (int i = 0; i < numElt; i++) {
    MacWrtProParserInternal::TextZoneData data;
    data.m_type = type;
    pos = input->tell();
    data.m_id = static_cast<int>(input->readLong(2));
    auto nChar = long(input->readULong(4));
    data.m_length = static_cast<int>(nChar);
    f.str("");
    f << "TextZone-" << i<< ":" << data;

    if (nChar > remainLength) {
      MWAW_DEBUG_MSG(("MacWrtProParser::readTextIds: bad size for block %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    remainLength -= nChar;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    if (nChar==0) continue;

    res.push_back(data);
  }

  if (remainLength) {
    MWAW_DEBUG_MSG(("MacWrtProParser::readTextIds: can not find %ld characters\n", remainLength));
    asciiFile.addPos(input->tell());
    asciiFile.addNote("TextZone:id-#");
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return res.size() != 0;
}

bool MacWrtProParser::readTextTokens(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                                     std::vector<MacWrtProParserInternal::Token> &res,
                                     int textLength)
{
  res.resize(0);
  int vers = version();
  int expectedSz = vers==0 ? 8 : 10;
  auto &stream=zone->m_stream;
  MWAWInputStreamPtr input = stream->m_input;
  libmwaw::DebugFile &asciiFile = stream->m_ascii;
  libmwaw::DebugStream f;
  long pos = input->tell();

  auto val = static_cast<int>(input->readULong(2));
  if (val && vers == 0) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    asciiFile.addPos(pos);
    asciiFile.addNote("_");
    return true;
  }
  long sz = static_cast<int>(input->readULong(2));
  if (sz == 0) {
    asciiFile.addPos(pos);
    asciiFile.addNote("_");
    return true;
  }

  long endPos = pos+sz+4;
  if ((sz%expectedSz) != 0 || !stream->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::readTextTokens: find an odd size\n"));
    return false;
  }

  auto numElt = int(sz/expectedSz);
  f << "TextZone:token(header),N=" << numElt << ",";
  if (val) f << "unkn=" << val << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  long remainLength = textLength;
  int numFootnotes = 0;
  std::vector<int> pictPos;
  for (int i = 0; i < numElt; i++) {
    f.str("");
    pos = input->tell();

    MacWrtProParserInternal::Token data;
    data.m_type = static_cast<int>(input->readULong(1));
    if (vers==0) { // check me
      switch (data.m_type) {
      case 2:  // page number
        data.m_type=1;
        break;
      case 3:  // footnote content
        break;
      case 4: // figure
        break;
      case 5: // footnote pos
        data.m_type=2;
        data.m_blockId = ++numFootnotes; // for MW2
        break;
      case 0x15: // Fixme: must find other date
      case 0x17: // date alpha
        data.m_type=6;
        break;
      case 0x1a: // time
        data.m_type=7;
        break;
      default:
        MWAW_DEBUG_MSG(("MacWrtProParser::readTextTokens: unknown block type %d\n", data.m_type));
        f << "#type=" << data.m_type << ",";
        data.m_type = -1;
        break;
      }
    }
    data.m_flags[0] = static_cast<unsigned int>(input->readULong(1));
    auto nChar = long(input->readULong(vers == 0 ? 2 : 4));
    data.m_length = static_cast<int>(nChar);

    if (vers==0)
      data.m_flags[1]=static_cast<unsigned int>(input->readULong(4)); // some kind of ID
    else {
      for (int j = 1; j < 3; j++) data.m_flags[j] = static_cast<unsigned int>(input->readULong(1));
      data.m_blockId = static_cast<int>(input->readULong(2));
    }
    f << "TextZone-" << i<< ":token," << data;
    if (nChar > remainLength) {
      MWAW_DEBUG_MSG(("MacWrtProParser::readTextTokens: bad size for block %d\n", i));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    remainLength -= nChar;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    if (data.m_type == 4) pictPos.push_back(static_cast<int>(res.size()));
    res.push_back(data);

    if (vers == 1 && data.m_blockId && (data.m_type == 2 || data.m_type == 4))
      m_state->m_graphicIdsCallByTokens.push_back(data.m_blockId);
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  if (vers == 0 && pictPos.size()) {
    size_t numPict = pictPos.size();
    // checkme always inverted ?
    for (size_t i = numPict; i > 0; i--) {
      MacWrtProParserInternal::Token &token = res[size_t(pictPos[i-1])];
      pos = input->tell();
      f.str("");
      f << "TextZone-pict" << i-1<< ":";
      val = static_cast<int>(input->readLong(2));
      if (val) f << "unkn=" << val << ",";
      auto blockId = static_cast<int>(input->readULong(2));
      if (blockId) {
        token.m_blockId = blockId;
        f << "block=" << blockId << ",";
        parseDataZone(blockId,1);
      }
      sz = long(input->readULong(4));
      f << "sz=" << std::hex << sz << std::dec << ",";
      int dim[4];
      for (auto &d : dim) d = static_cast<int>(input->readLong(2));
      token.m_box = MWAWBox2f(MWAWVec2f(float(dim[1]),float(dim[0])), MWAWVec2f(float(dim[3]),float(dim[2])));
      f << "dim=" << token.m_box << ",";
      for (auto &d : dim) d = static_cast<int>(input->readLong(2));
      f << "dim2=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
      // followed by junk ?
      ascii().addDelimiter(input->tell(),'|');
      input->seek(pos+62, librevenge::RVNG_SEEK_SET);
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
    }
  }

  return res.size() != 0;
}

////////////////////////////////////////////////////////////
// try to send a empty zone
////////////////////////////////////////////////////////////
bool MacWrtProParser::sendEmptyFrameZone(MWAWPosition const &pos, MWAWGraphicStyle const &style)
{
  std::shared_ptr<MacWrtProParserInternal::SubDocument> subdoc
  (new MacWrtProParserInternal::SubDocument(*this, getInput(), -3));
  if (getTextListener())
    getTextListener()->insertTextBox(pos, subdoc, style);
  return true;
}

////////////////////////////////////////////////////////////
// try to send a text
////////////////////////////////////////////////////////////
int MacWrtProParser::findNumHardBreaks(int blockId)
{
  auto it = m_state->m_textMap.find(blockId);
  if (it == m_state->m_textMap.end()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::findNumHardBreaks: can not find text zone\n"));
    return 0;
  }
  return findNumHardBreaks(it->second);
}

int MacWrtProParser::findNumHardBreaks(std::shared_ptr<MacWrtProParserInternal::TextZone> zone)
{
  if (!zone->m_entries.size()) return 0;
  int num = 0;
  MWAWInputStreamPtr input = getInput();
  for (auto const &entry : zone->m_entries) {
    input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
    for (long j = 0; j < entry.length(); j++) {
      switch (input->readULong(1)) {
      case 0xc: // hard page
      case 0xb: // difficult to differentiate column/page break so...
        num++;
        break;
      default:
        break;
      }
    }
  }
  return num;
}

////////////////////////////////////////////////////////////
// try to send a text
////////////////////////////////////////////////////////////
bool MacWrtProParser::sendTextZone(int blockId, bool mainZone)
{
  auto it = m_state->m_textMap.find(blockId);
  if (it == m_state->m_textMap.end()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::sendTextZone: can not find text zone %x\n", unsigned(blockId)));
    return false;
  }
  sendText(it->second, mainZone);
  return true;
}

bool MacWrtProParser::sendTextBoxZone(int blockId, MWAWPosition const &pos, MWAWGraphicStyle const &style)
{
  std::shared_ptr<MacWrtProParserInternal::SubDocument> subdoc
  (new MacWrtProParserInternal::SubDocument(*this, getInput(), blockId));
  if (getTextListener())
    getTextListener()->insertTextBox(pos, subdoc, style);
  return true;
}

namespace MacWrtProParserInternal
{
/** Internal and low level: structure used to sort the position of data */
struct DataPosition {
  //! constructor
  DataPosition(int type=-1, int id=-1, long pos=0)
    : m_type(type)
    , m_id(id)
    , m_pos(pos)
  {
  }
  //! the type
  int m_type;
  //! an id
  int m_id;
  //! the position
  long m_pos;
  //! the comparison structure
  struct Compare {
    //! comparaison function
    bool operator()(DataPosition const &p1, DataPosition const &p2) const
    {
      long diff = p1.m_pos - p2.m_pos;
      if (diff) return (diff < 0);
      diff = p1.m_type - p2.m_type;
      if (diff) return (diff < 0);
      diff = p1.m_id - p2.m_id;
      return (diff < 0);
    }
  };
};
}

bool MacWrtProParser::sendText(std::shared_ptr<MacWrtProParserInternal::TextZone> zone, bool mainZone)
{
  if (!zone->m_entries.size()) // can happen in header/footer
    return false;
  int vers = version();
  MacWrtProStructuresListenerState listenerState(m_structures, mainZone, vers);
  MacWrtProParserInternal::DataPosition::Compare compareFunction;
  std::set<MacWrtProParserInternal::DataPosition, MacWrtProParserInternal::DataPosition::Compare>
  set(compareFunction);
  long cPos = 0;
  for (size_t i = 0; i < zone->m_entries.size(); i++) {
    set.insert(MacWrtProParserInternal::DataPosition(3, static_cast<int>(i), cPos));
    cPos += zone->m_entries[i].length();
  }
  set.insert(MacWrtProParserInternal::DataPosition(4, 0, cPos));
  cPos = 0;
  for (size_t i = 0; i < zone->m_tokens.size(); i++) {
    cPos += zone->m_tokens[i].m_length;
    set.insert(MacWrtProParserInternal::DataPosition(2, static_cast<int>(i), cPos));
  }
  for (int id = 0; id < 2; id++) {
    cPos = 0;
    for (size_t i = 0; i < zone->m_ids[id].size(); i++) {
      set.insert(MacWrtProParserInternal::DataPosition(1-id, static_cast<int>(i), cPos));
      cPos += zone->m_ids[id][i].m_length;
    }
  }
  std::vector<int> pageBreaks=listenerState.getPageBreaksPos();
  for (size_t i = 0; i < pageBreaks.size(); i++) {
    if (pageBreaks[i]<=0 || pageBreaks[i] >= zone->m_textLength) {
      if (pageBreaks[i] >= zone->m_textLength+1) {
        MWAW_DEBUG_MSG(("MacWrtProParser::sendText: page breaks seems bad\n"));
      }
      break;
    }
    set.insert(MacWrtProParserInternal::DataPosition(-1, static_cast<int>(i), pageBreaks[i]));
  }

  MWAWInputStreamPtr input = getInput();
  long pos = zone->m_entries[0].begin();
  long asciiPos = pos;
  if (pos > 0)
    input->seek(pos, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f, f2;
  cPos = 0;
  for (auto const &data : set) {
    long oldPos = pos;
    if (data.m_pos < cPos) {
      MWAW_DEBUG_MSG(("MacWrtProParser::sendText: position go backward, stop...\n"));
      break;
    }
    if (data.m_pos != cPos) {
      if (pos > 0) {
        std::string text("");
        for (long i = cPos; i < data.m_pos && !input->isEnd(); i++) {
          auto ch = char(input->readULong(1));
          if (!ch)
            text+= "#";
          else {
            listenerState.sendChar(ch);
            if (ch > 0 && ch < 20 && ch != 0xd && ch != 0x9) text+="#";
            text+=ch;
          }
        }
        f << "'" << text << "'";
      }

      if (pos > 0 && f.str().length()) {
        f2.str("");
        f2 << "Entries(TextContent):" << f.str();
        f.str("");
        ascii().addPos(asciiPos);
        ascii().addNote(f2.str().c_str());
        pos += (data.m_pos-cPos);
      }

      cPos = data.m_pos;
    }
    switch (data.m_type) {
    case -1:
      listenerState.insertSoftPageBreak();
      break;
    case 4:
    case 3:
      if (pos > 0 && (pos&0xFF))
        ascii().addDelimiter(pos,'|');
      if (data.m_type == 3) {
        pos = zone->m_entries[size_t(data.m_id)].begin();
        if (pos > 0)
          input->seek(pos, librevenge::RVNG_SEEK_SET);
      }
      break;
    case 2: {
      // save the position because we read some extra data ( footnote, table, textbox)
      long actPos = input->tell();
      switch (zone->m_tokens[size_t(data.m_id)].m_type) {
      case 1:
        if (getTextListener()) getTextListener()->insertField(MWAWField(MWAWField::PageNumber));
        break;
      case 2:
        if (vers == 1 && listenerState.isSent(zone->m_tokens[size_t(data.m_id)].m_blockId)) {
          MWAW_DEBUG_MSG(("MacWrtProParser::sendText: footnote is already sent...\n"));
        }
        else {
          int id = zone->m_tokens[size_t(data.m_id)].m_blockId;
          if (vers == 0) id = -id;
          MWAWSubDocumentPtr subdoc(new MacWrtProParserInternal::SubDocument(*this, getInput(), id));
          getTextListener()->insertNote(MWAWNote(MWAWNote::FootNote), subdoc);
        }
        break;
      case 3:
        break; // footnote content, ok
      case 4:
        if (vers==0) {
          MWAWPosition pictPos(MWAWVec2f(0,0), zone->m_tokens[size_t(data.m_id)].m_box.size(), librevenge::RVNG_POINT);
          pictPos.setRelativePosition(MWAWPosition::Char, MWAWPosition::XLeft, MWAWPosition::YBottom);
          sendPictureZone(zone->m_tokens[size_t(data.m_id)].m_blockId, pictPos);
        }
        else
          listenerState.send(zone->m_tokens[size_t(data.m_id)].m_blockId);
        break;
      case 5:
        break; // hyphen ok
      case 6:
        if (getTextListener()) getTextListener()->insertField(MWAWField(MWAWField::Date));
        break;
      case 7:
        if (getTextListener()) getTextListener()->insertField(MWAWField(MWAWField::Time));
        break;
      case 8:
        if (getTextListener()) getTextListener()->insertField(MWAWField(MWAWField::Title));
        break;
      case 9:
        if (getTextListener()) getTextListener()->insertUnicodeString(librevenge::RVNGString("#REVISION#"));
        break;
      case 10:
        if (getTextListener()) {
          int numSection = listenerState.numSection()+1;
          std::stringstream s;
          s << numSection;
          getTextListener()->insertUnicodeString(librevenge::RVNGString(s.str().c_str()));
        }
        break;
      default:
        break;
      }
      f << "token[" << zone->m_tokens[size_t(data.m_id)] << "],";
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
    case 1:
      if (m_structures)
        listenerState.sendFont(zone->m_ids[0][size_t(data.m_id)].m_id);
      f << "[C" << zone->m_ids[0][size_t(data.m_id)].m_id << "],";
      break;
    case 0:
      if (m_structures)
        listenerState.sendParagraph(zone->m_ids[1][size_t(data.m_id)].m_id);
      f << "[P" << zone->m_ids[1][size_t(data.m_id)].m_id << "],";
      break;
    default: {
      static bool firstError = true;
      if (firstError) {
        MWAW_DEBUG_MSG(("MacWrtProParser::sendText: find unexpected data type...\n"));
        firstError = false;
      }
      f << "#";
      break;
    }

    }
    if (pos >= 0 && pos != oldPos)
      asciiPos = pos;
  }

  return true;
}


////////////////////////////////////////////////////////////
// try to send a picture
////////////////////////////////////////////////////////////
bool MacWrtProParser::sendPictureZone(int blockId, MWAWPosition const &pictPos,
                                      MWAWGraphicStyle const &style)
{
  auto it = m_state->m_dataMap.find(blockId);
  if (it == m_state->m_dataMap.end()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::sendPictureZone: can not find picture zone\n"));
    return false;
  }
  sendPicture(it->second, pictPos, style);
  return true;
}

bool MacWrtProParser::sendPicture(std::shared_ptr<MacWrtProParserInternal::Zone> zone,
                                  MWAWPosition pictPos, MWAWGraphicStyle const &style)
{
  if (!zone) return false;
  if (zone->m_type != 1) {
    MWAW_DEBUG_MSG(("MacWrtProParser::sendPicture: not a picture date\n"));
    return false;
  }

  zone->m_parsed = true;

  // ok init is done
  auto &stream=zone->m_stream;
  MWAWInputStreamPtr input = stream->m_input;
  libmwaw::DebugFile &asciiFile = stream->m_ascii;
  long pos=stream->m_bof;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;

  f << "Entries(PICT),";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  auto pictSize = long(input->readULong(4));
  if (pictSize < 10 || !stream->checkPosition(pos+4+pictSize)) {
    MWAW_DEBUG_MSG(("MacWrtProParser::sendPicture: oops a pb with pictSize\n"));
    asciiFile.addPos(4);
    asciiFile.addNote("#PICT");
    return false;
  }
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, static_cast<int>(pictSize)));
  if (!pict) {
    // sometimes this just fails because the pictSize is not correct
    input->seek(pos+14, librevenge::RVNG_SEEK_SET);
    if (input->readULong(2) == 0x1101) { // try to force the size to be ok
      librevenge::RVNGBinaryData data;
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      input->readDataBlock(4+pictSize, data);
      auto *dataPtr=const_cast<unsigned char *>(data.getDataBuffer());
      if (!dataPtr) {
        MWAW_DEBUG_MSG(("MacWrtProParser::sendPicture: oops where is the picture...\n"));
        return false;
      }

      dataPtr[4]=dataPtr[2];
      dataPtr[5]=dataPtr[3];

      MWAWInputStreamPtr pictInput=MWAWInputStream::get(data, false);
      if (!pictInput) {
        MWAW_DEBUG_MSG(("MacWrtProParser::sendPicture: oops where is the picture input...\n"));
        return false;
      }

      pictInput->seek(4, librevenge::RVNG_SEEK_SET);
      pict.reset(MWAWPictData::get(pictInput, static_cast<int>(pictSize)));
    }
  }

#ifdef DEBUG_WITH_FILES
  asciiFile.skipZone(pos+4, pos+4+pictSize-1);
  librevenge::RVNGBinaryData file;
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  input->readDataBlock(pictSize, file);
  static int volatile pictName = 0;
  f.str("");
  f << "PICT-" << ++pictName;
  libmwaw::Debug::dumpFile(file, f.str().c_str());
  asciiFile.addPos(pos+4+pictSize);
  asciiFile.addNote("PICT(end)");
#endif

  if (!pict) { // ok, we can not do anything except sending the data...
    MWAW_DEBUG_MSG(("MacWrtProParser::sendPicture: no sure this is a picture\n"));
    if (pictPos.size().x() <= 0 || pictPos.size().y() <= 0)
      pictPos=MWAWPosition(MWAWVec2f(0,0),MWAWVec2f(100.,100.), librevenge::RVNG_POINT);
    if (getTextListener()) {
      librevenge::RVNGBinaryData data;
      input->seek(pos+4, librevenge::RVNG_SEEK_SET);
      input->readDataBlock(pictSize, data);
      getTextListener()->insertPicture(pictPos, MWAWEmbeddedObject(data, "image/pict"), style);
    }
    return true;
  }

  if (pictPos.size().x() <= 0 || pictPos.size().y() <= 0) {
    pictPos.setOrigin(MWAWVec2f(0,0));
    pictPos.setSize(pict->getBdBox().size());
    pictPos.setUnit(librevenge::RVNG_POINT);
  }
  if (pict->getBdBox().size().x() > 0 && pict->getBdBox().size().y() > 0)
    pictPos.setNaturalSize(pict->getBdBox().size());

  if (getTextListener()) {
    MWAWEmbeddedObject picture;
    if (pict->getBinary(picture))
      getTextListener()->insertPicture(pictPos, picture, style);
  }
  return true;
}

////////////////////////////////////////////////////////////
// some debug functions
////////////////////////////////////////////////////////////
void MacWrtProParser::checkUnparsed()
{
  MWAWInputStreamPtr input = getInput();
  libmwaw::DebugStream f;

  long pos;
  std::stringstream notParsed;
  for (int bl = 3; bl < 1000; bl++) {
    if (m_state->m_parsedBlocks.find(bl) != m_state->m_parsedBlocks.end())
      continue;

    pos = bl*0x100;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    if (input->isEnd()) break;
    notParsed << std::hex <<  bl << std::dec << ",";

    // normaly there must remains only text entry...
    f.str("");
    f << "Entries(Unparsed):";

    std::string text("");
    bool findZero = false;
    for (int c = 0; c < 256; c++) {
      auto ch = char(input->readULong(1));
      if (!ch) {
        if (findZero) {
          input->seek(-1, librevenge::RVNG_SEEK_CUR);
          break;
        }
        findZero = true;
        continue;
      }
      if (findZero) {
        text += "#";
        findZero = false;
      }
      text+=ch;
    }
    f << text;
    if (long(input->tell()) != pos+256)
      ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (!notParsed.str().empty()) {
    MWAW_DEBUG_MSG(("MacWrtProParser::checkUnparsed: not parsed %s\n", notParsed.str().c_str()));
  }
}

std::string MacWrtProParser::convertDateToDebugString(unsigned dt)
{
  int Y, M, D, HH, MM, SS;
  MWAWCellContent::double2Date(double(dt/3600/24)+1460., Y, M, D); // change the reference date from 1/1/1904 to 1/1/1900
  double time=double(dt%(3600*24))/3600/24;
  MWAWCellContent::double2Time(time, HH, MM, SS);
  std::stringstream s;
  s << D << "/" << M << "/" << Y << " " << HH << ":" << MM << ",";
  return s.str();
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
