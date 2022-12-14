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
#include "MWAWDebug.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSection.hxx"
#include "MWAWSubDocument.hxx"

#include "HanMacWrdJParser.hxx"

#include "HanMacWrdJText.hxx"

/** Internal: the structures of a HanMacWrdJText */
namespace HanMacWrdJTextInternal
{
/** different PLC types */
enum PLCType { CHAR=0, RULER, LINE, TOKEN, Unknown};
/** Internal and low level: the PLC different types and their structures of a HanMacWrdJText */
struct PLC {
  //! constructor
  PLC(PLCType w= Unknown, int id=0)
    : m_type(w)
    , m_id(id)
    , m_extra("") {}
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, PLC const &plc)
  {
    switch (plc.m_type) {
    case CHAR:
      o << "F" << plc.m_id << ",";
      break;
    case LINE:
      o << "L" << plc.m_id << ",";
      break;
    case RULER:
      o << "R" << plc.m_id << ",";
      break;
    case TOKEN:
      o << "T" << plc.m_id << ",";
      break;
    case Unknown:
#if !defined(__clang__)
    default:
#endif
      o << "#unknown" << plc.m_id << ",";
    }
    o << plc.m_extra;
    return o;
  }
  //! PLC type
  PLCType m_type;
  //! the indentificator
  int m_id;
  //! extra data
  std::string m_extra;
};

/** Internal: class to store a token of a HanMacWrdJText */
struct Token {
  //! constructor
  Token()
    : m_type(0)
    , m_id(0)
    , m_localId(0)
    , m_bookmark("")
    , m_length(0)
    , m_extra("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Token const &tk)
  {
    switch (tk.m_type) { // checkme
    case 0:
      break;
    case 1:
      o << "field,";
      break;
    case 2:
      o << "footnote,";
      break;
    case 8:
      o << "toc,";
      break;
    case 0x20:
      o << "bookmark,";
      break;
    default:
      o << "#type=" << tk.m_type << ",";
      break;
    }
    if (tk.m_bookmark.length())
      o << "text[bookmark]=" << tk.m_bookmark << ",";
    if (tk.m_id)
      o << "zId=" << std::hex << tk.m_id << std::dec << ",";
    if (tk.m_localId)
      o << "id=" << tk.m_localId << ",";
    if (tk.m_length)
      o << "length=" << tk.m_length << ",";
    o << tk.m_extra;
    return o;
  }
  //! the token type
  int m_type;
  //! the id ( to be send)
  long m_id;
  //! the local id
  int m_localId;
  //! the bookmark string
  std::string m_bookmark;
  //! the token length in caller text
  int m_length;
  //! extra string string
  std::string m_extra;
};

/** Internal: class to store a section of a HanMacWrdJText */
struct Section {
  //! constructor
  Section()
    : m_numCols(1)
    , m_colWidth()
    , m_colSep()
    , m_id(0)
    , m_extra("")
  {
  }
  //! returns a MWAWSection
  MWAWSection getSection() const
  {
    MWAWSection sec;
    if (m_colWidth.size()==0) {
      MWAW_DEBUG_MSG(("HanMacWrdJTextInternal::Section:getSection can not find any width\n"));
      return sec;
    }
    if (m_numCols <= 1)
      return sec;
    bool hasSep = m_colWidth.size()==m_colSep.size();
    sec.m_columns.resize(size_t(m_numCols));
    if (m_colWidth.size()==size_t(m_numCols)) {
      for (size_t c=0; c < size_t(m_numCols); c++) {
        sec.m_columns[c].m_width = double(m_colWidth[c]);
        sec.m_columns[c].m_widthUnit = librevenge::RVNG_POINT;
        if (!hasSep) continue;
        sec.m_columns[c].m_margins[libmwaw::Left]=
          sec.m_columns[c].m_margins[libmwaw::Right]=double(m_colSep[c])/2.0/72.;
      }
    }
    else {
      if (m_colWidth.size()>1) {
        MWAW_DEBUG_MSG(("HanMacWrdJTextInternal::Section:getSection colWidth is not coherent with numCols\n"));
      }
      sec.setColumns(m_numCols, double(m_colWidth[0]), librevenge::RVNG_POINT,
                     hasSep ? double(m_colSep[0])/72. : 0);
    }
    return sec;
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Section const &sec)
  {
    if (sec.m_numCols!=1)
      o << "numCols=" << sec.m_numCols << ",";
    if (sec.m_colWidth.size()) {
      o << "colWidth=[";
      for (size_t i = 0; i < sec.m_colWidth.size(); i++)
        o << sec.m_colWidth[i] << ":" << sec.m_colSep[i] << ",";
      o << "],";
    }
    if (sec.m_id)
      o << "id=" << std::hex << sec.m_id << std::dec << ",";
    o << sec.m_extra;
    return o;
  }
  //! the number of column
  int m_numCols;
  //! the columns width
  std::vector<double> m_colWidth;
  //! the columns separator width
  std::vector<double> m_colSep;
  //! the id
  long m_id;
  //! extra string string
  std::string m_extra;
};

//! Internal: a struct used to store a text zone
struct TextZone {
  //! enum used to define the zone type
  enum Type { T_Main=0, T_Header=1, T_Footer=2, T_Footnote=3, T_Textbox=4,
              T_Table=9, T_Comment=10, T_Unknown
            };
  //! constructor
  TextZone()
    : m_type(T_Unknown)
    , m_entry()
    , m_id(0)
    , m_PLCMap()
    , m_tokenList()
    , m_parsed(false)
  {
  }

  //! the zone type
  Type m_type;
  //! the main entry
  MWAWEntry m_entry;
  //! the file zone id
  long m_id;
  //! the plc map
  std::multimap<long, PLC> m_PLCMap;
  //! the tokens list
  std::vector<Token> m_tokenList;

  //! true if the zone is sended
  mutable bool m_parsed;
};


/** Internal: class to store the paragraph properties of a HanMacWrdJText */
struct Paragraph final : public MWAWParagraph {
  //! Constructor
  Paragraph()
    : MWAWParagraph()
    , m_type(0)
    , m_addPageBreak(false)
  {
  }
  Paragraph(Paragraph const &)=default;
  Paragraph &operator=(Paragraph const &)=default;
  Paragraph &operator=(Paragraph &&)=default;
  //! destructor
  ~Paragraph() final;
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Paragraph const &ind)
  {
    switch (ind.m_type) {
    case 0:
      break;
    case 1:
      o << "header,";
      break;
    case 2:
      o << "footer,";
      break;
    case 5:
      o << "footnote,";
      break;
    default:
      o << "#type=" << ind.m_type << ",";
      break;
    }
    o << static_cast<MWAWParagraph const &>(ind) << ",";
    if (ind.m_addPageBreak) o << "pageBreakBef,";
    return o;
  }
  //! the type
  int m_type;
  //! flag to store a force page break
  bool m_addPageBreak;
};

Paragraph::~Paragraph()
{
}

////////////////////////////////////////
//! Internal: the state of a HanMacWrdJText
struct State {
  //! constructor
  State()
    : m_version(-1)
    , m_fontList()
    , m_paragraphList()
    , m_sectionList()
    , m_ftnTextId(0)
    , m_ftnFirstPosList()
    , m_textZoneList()
    , m_idTextZoneMap()
    , m_numPages(-1)
    , m_actualPage(0)
  {
  }
  //! the file version
  mutable int m_version;
  /** the font list */
  std::vector<MWAWFont> m_fontList;
  //! the list of paragraph
  std::vector<Paragraph> m_paragraphList;
  //! the list of section
  std::vector<Section> m_sectionList;
  //! the footnote zone id;
  long m_ftnTextId;
  //! the footnote begin positions
  std::vector<long> m_ftnFirstPosList;
  //! the list of text zone
  std::vector<TextZone> m_textZoneList;
  //! a map textId -> id in m_textZoneList
  std::map<long, int> m_idTextZoneMap;
  int m_numPages /* the number of pages */, m_actualPage /* the actual page */;
};

////////////////////////////////////////
//! Internal: the subdocument of a HanMacWrdJText
class SubDocument final : public MWAWSubDocument
{
protected:
  //! the subdocument type
  enum Type { S_TextZone, S_String };
public:
  //! constructor to call a textzone
  SubDocument(HanMacWrdJText &pars, MWAWInputStreamPtr const &input, long id, long cPos=0)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_type(S_TextZone)
    , m_textParser(&pars)
    , m_id(id)
    , m_cPos(cPos)
    , m_bookmark("") {}
  //! constructor to send a string
  SubDocument(HanMacWrdJText &pars, MWAWInputStreamPtr const &input, std::string const &text)
    : MWAWSubDocument(pars.m_mainParser, input, MWAWEntry())
    , m_type(S_String)
    , m_textParser(&pars)
    , m_id(0)
    , m_cPos(0)
    , m_bookmark(text) {}


  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final;

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  /** the subdocument type */
  Type m_type;
  /** the text parser */
  HanMacWrdJText *m_textParser;
  //! the subdocument id
  long m_id;
  //! the first charecter position
  long m_cPos;
  //! the bookmark string
  std::string m_bookmark;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("HanMacWrdJTextInternal::SubDocument::parse: no listener\n"));
    return;
  }

  if (m_type==S_String) {
    for (auto &c : m_bookmark)
      listener->insertCharacter(static_cast<unsigned char>(c));
    return;
  }
  if (!m_textParser) {
    MWAW_DEBUG_MSG(("HanMacWrdJTextInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  m_textParser->sendText(m_id, m_cPos);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

bool SubDocument::operator!=(MWAWSubDocument const &doc) const
{
  if (MWAWSubDocument::operator!=(doc)) return true;
  auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
  if (!sDoc) return true;
  if (m_textParser != sDoc->m_textParser) return true;
  if (m_type != sDoc->m_type) return true;
  if (m_id != sDoc->m_id) return true;
  if (m_cPos != sDoc->m_cPos) return true;
  if (m_bookmark != sDoc->m_bookmark) return true;
  return false;
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
HanMacWrdJText::HanMacWrdJText(HanMacWrdJParser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new HanMacWrdJTextInternal::State)
  , m_mainParser(&parser)
{
}

HanMacWrdJText::~HanMacWrdJText()
{
}

int HanMacWrdJText::version() const
{
  if (m_state->m_version < 0)
    m_state->m_version = m_parserState->m_version;
  return m_state->m_version;
}

int HanMacWrdJText::numPages() const
{
  int nPages = 1;
  for (auto const &zone : m_state->m_textZoneList) {
    if (zone.m_type != HanMacWrdJTextInternal::TextZone::T_Main)
      continue;
    nPages = const_cast<HanMacWrdJText *>(this)->computeNumPages(zone);
    break;
  }
  // fixme: compute the number of page
  m_state->m_numPages = nPages;
  return nPages;
}

std::vector<long> HanMacWrdJText::getTokenIdList() const
{
  std::vector<long> res;
  for (auto const &zone : m_state->m_textZoneList) {
    for (auto const &token : zone.m_tokenList) {
      if (token.m_type==1)
        res.push_back(token.m_id);
    }
  }
  return res;
}

////////////////////////////////////////////////////////////
// Intermediate level
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
//     Text
////////////////////////////////////////////////////////////
bool HanMacWrdJText::canSendTextAsGraphic(long id, long cPos)
{
  if (m_state->m_idTextZoneMap.find(id)==m_state->m_idTextZoneMap.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::canSendTextAsGraphic: can not find text zone with id %lx\n", static_cast<long unsigned int>(id)));
    return false;
  }
  int zId = m_state->m_idTextZoneMap.find(id)->second;
  if (zId < 0 || zId >= static_cast<int>(m_state->m_textZoneList.size()))
    return false;
  return canSendTextAsGraphic(m_state->m_textZoneList[size_t(zId)], cPos);
}

bool HanMacWrdJText::canSendTextAsGraphic(HanMacWrdJTextInternal::TextZone const &zone, long cPos)
{
  if (!zone.m_entry.valid())
    return false;

  auto plcIt=zone.m_PLCMap.find(cPos);
  while (plcIt != zone.m_PLCMap.end() && plcIt->first < cPos)
    ++plcIt;
  while (plcIt != zone.m_PLCMap.end()) {
    auto const &plc = plcIt++->second;
    if (plc.m_type!=HanMacWrdJTextInternal::TOKEN) continue;
    if (plc.m_id < 0 || plc.m_id >= static_cast<int>(zone.m_tokenList.size()))
      continue;
    auto const &tkn=zone.m_tokenList[size_t(plc.m_id)];
    switch (tkn.m_type) {
    case 1:
    case 2:
    case 0x20:
      return false;
    default:
      break;
    }
  }
  return true;
}

bool HanMacWrdJText::sendText(long id, long cPos, MWAWListenerPtr listener)
{
  if (m_state->m_idTextZoneMap.find(id)==m_state->m_idTextZoneMap.end()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find text zone with id %lx\n", static_cast<long unsigned int>(id)));
    return false;
  }
  int zId = m_state->m_idTextZoneMap.find(id)->second;
  if (zId < 0 || zId >= static_cast<int>(m_state->m_textZoneList.size()))
    return false;
  return sendText(m_state->m_textZoneList[size_t(zId)], cPos, listener);
}

bool HanMacWrdJText::sendMainText()
{
  for (auto &zone : m_state->m_textZoneList) {
    if (zone.m_type != HanMacWrdJTextInternal::TextZone::T_Main)
      continue;
    sendText(zone,0);
    return true;
  }
  MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find the main zone\n"));
  return false;
}


bool HanMacWrdJText::sendText(HanMacWrdJTextInternal::TextZone const &zone, long fPos, MWAWListenerPtr listener)
{
  if (!zone.m_entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: call without entry\n"));
    return false;
  }
  bool withLocalListener=false;
  if (listener)
    withLocalListener=true;
  else
    listener=m_parserState->m_textListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find the listener\n"));
    return false;
  }

  zone.m_parsed=true;
  librevenge::RVNGBinaryData data;
  if (!m_mainParser->decodeZone(zone.m_entry, data)) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not decode a zone\n"));
    m_parserState->m_asciiFile.addPos(zone.m_entry.begin());
    m_parserState->m_asciiFile.addNote("###");
    return false;
  }
  if (!data.size())
    return true;
  if (fPos < 0 || 2*fPos > long(data.size())) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: first pos %ld is too big zone\n", fPos));
    return false;
  }

  MWAWInputStreamPtr input=MWAWInputStream::get(data, false);
  if (!input) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find my input\n"));
    return false;
  }
  libmwaw::DebugFile asciiFile(input);

#ifdef DEBUG_WITH_FILES
  if (fPos==0) {
    static int tId=0;
    std::stringstream s;
    s << "Text" << tId++;
    asciiFile.open(s.str().c_str());
  }
#endif

  bool isMain = zone.m_type==HanMacWrdJTextInternal::TextZone::T_Main;
  bool charOneIsEnd = zone.m_type==HanMacWrdJTextInternal::TextZone::T_Footnote ||
                      zone.m_type==HanMacWrdJTextInternal::TextZone::T_Table;

  long cPos=fPos, endCPos=-1;
  int actPage = 1, actCol = 0, numCol=1, actSection = 1;
  if (isMain)
    m_mainParser->newPage(1);
  if (isMain && !m_state->m_sectionList.size()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find section 0\n"));
  }
  else if (isMain) {
    auto sec = m_state->m_sectionList[0];
    if (sec.m_numCols >= 1 && sec.m_colWidth.size() > 0) {
      if (listener->isSectionOpened())
        listener->closeSection();
      listener->openSection(sec.getSection());
      numCol = listener->getSection().numColumns();
    }
  }
  long pos = 2*fPos;
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(TextData):";

  while (true) {
    if (cPos!=fPos) {
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      f.str("");
      f << "TextData:";
      pos = input->tell();
    }
    auto plcIt=zone.m_PLCMap.find(cPos);
    int expectedChar=0;
    if (plcIt != zone.m_PLCMap.end()) {
      while (plcIt != zone.m_PLCMap.end() && plcIt->first == cPos) {
        auto const &plc = plcIt++->second;
        f << "[" << plc << "]";
        switch (plc.m_type) {
        case HanMacWrdJTextInternal::CHAR: {
          if (plc.m_id < 0 || plc.m_id >= static_cast<int>(m_state->m_fontList.size())) {
            MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find font\n"));
            f << "[###font]";
            break;
          }
          listener->setFont(m_state->m_fontList[size_t(plc.m_id)]);
          break;
        }
        case HanMacWrdJTextInternal::RULER: {
          if (plc.m_id < 0 || plc.m_id >= static_cast<int>(m_state->m_paragraphList.size())) {
            MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find paragraph\n"));
            f << "[###paragraph]";
            break;
          }
          auto const &para=m_state->m_paragraphList[size_t(plc.m_id)];
          if (isMain && para.m_addPageBreak)
            m_mainParser->newPage(++actPage);
          listener->setParagraph(para);
          break;
        }
        case HanMacWrdJTextInternal::TOKEN: {
          if (plc.m_id < 0 || plc.m_id >= static_cast<int>(zone.m_tokenList.size())) {
            MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find token\n"));
            f << "[###token]";
            break;
          }
          auto const &tkn=zone.m_tokenList[size_t(plc.m_id)];
          switch (tkn.m_type) {
          case 1:
            expectedChar=0x1;
            if (withLocalListener) {
              MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: unexpected token type=1 in graphic\n"));
              break;
            }
            m_mainParser->sendZone(tkn.m_id);
            break;
          case 2: {
            expectedChar=0x11;
            if (withLocalListener) {
              MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not insert footnote in graphic\n"));
              break;
            }
            if (tkn.m_localId < 0 || tkn.m_localId >= int(m_state->m_ftnFirstPosList.size())) {
              MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find footnote\n"));
              f << "[###ftnote]";
              break;
            }
            MWAWSubDocumentPtr subdoc
            (new HanMacWrdJTextInternal::SubDocument
             (*this, input, m_state->m_ftnTextId,
              m_state->m_ftnFirstPosList[size_t(tkn.m_localId)]));
            m_parserState->m_textListener->insertNote(MWAWNote(MWAWNote::FootNote),subdoc);
            break;
          }
          case 8: // TOC, ok to ignore
            break;
          case 0x20:
            if (withLocalListener) {
              MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not insert bookmark in graphic\n"));
              break;
            }
            else {
              MWAWSubDocumentPtr subdoc(new HanMacWrdJTextInternal::SubDocument(*this, input, tkn.m_bookmark));
              m_parserState->m_textListener->insertComment(subdoc);
              break;
            }
          default:
            MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not send token with type %d\n", tkn.m_type));
            break;
          }

          break;
        }
        case HanMacWrdJTextInternal::LINE:
        case HanMacWrdJTextInternal::Unknown:
#if !defined(__clang__)
        default:
#endif
          break;
        }
      }
      endCPos = (plcIt != zone.m_PLCMap.end()) ? plcIt->first : -1;
    }
    if (input->isEnd())
      break;
    if (expectedChar) {
      if (static_cast<int>(input->readULong(1))==expectedChar) {
        cPos++;
        input->seek(1, librevenge::RVNG_SEEK_CUR);
      }
      else {
        MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find expected char token\n"));
        input->seek(-1, librevenge::RVNG_SEEK_CUR);
        f << "###";
      }
    }
    while (endCPos<0 || cPos < endCPos) {
      cPos++;
      if (input->isEnd())
        break;
      auto c = static_cast<int>(input->readULong(2));
      if (c==0) {
        if (input->isEnd())
          break;
        f << "#[0]";
        continue;
      }
      if (c==1 && charOneIsEnd)
        return true;
      switch (c) {
      case 0x1000:
        f << "[pgNum]";
        listener->insertField(MWAWField(MWAWField::PageNumber));
        break;
      case 0x1001:
        f << "[pgCount]";
        listener->insertField(MWAWField(MWAWField::PageCount));
        break;
      case 0x1002: {
        f << "[date]";
        MWAWField field(MWAWField::Date);
        field.m_DTFormat="%A, %b %d, %Y";
        listener->insertField(field);
        break;
      }
      case 0x1003: {
        f << "[time]";
        MWAWField field(MWAWField::Time);
        field.m_DTFormat="%I:%M %p";
        listener->insertField(field);
        break;
      }
      case 0x1004:
        f << "[title]";
        listener->insertField(MWAWField(MWAWField::Title));
        break;
      case 0x1005: {
        std::stringstream s;
        f << "[section]";
        s << actSection;
        listener->insertUnicodeString(librevenge::RVNGString(s.str().c_str()));
        break;
      }
      case 2:
        f << "[colBreak]";
        if (!isMain) {
          MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: find column break in auxilliary block\n"));
          break;
        }
        if (actCol < numCol-1 && numCol > 1) {
          listener->insertBreak(MWAWTextListener::ColumnBreak);
          actCol++;
        }
        else {
          actCol = 0;
          m_mainParser->newPage(++actPage);
        }
        break;
      case 3:
        f << "[pageBreak]";
        if (isMain) {
          m_mainParser->newPage(++actPage);
          actCol = 0;
        }
        break;
      case 4:
        f << "[sectionBreak]";
        if (!isMain) {
          MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: find section in auxilliary block\n"));
          break;
        }
        if (size_t(actSection) >= m_state->m_sectionList.size()) {
          MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: can not find section %d\n", actSection));
          break;
        }
        else {
          auto const &sec = m_state->m_sectionList[size_t(actSection++)];
          actCol = 0;
          if (listener->isSectionOpened())
            listener->closeSection();
          m_mainParser->newPage(++actPage);
          listener->openSection(sec.getSection());
          numCol = listener->getSection().numColumns();
        }
        break;
      case 9:
        f << char(c);
        listener->insertTab();
        break;
      case 0xd:
        f << char(c);
        listener->insertEOL();
        break;
      default: {
        if (c >= 0x1100 && c <= 0x11ff) // ok a footnote
          break;
        if (c <= 0x1f || c >= 0x100) {
          f << "#[" << std::hex << c << std::dec << "]";
          MWAW_DEBUG_MSG(("HanMacWrdJText::sendText: find a odd char %x\n", static_cast<unsigned int>(c)));
          break;
        }
        f << char(c);
        listener->insertCharacter(static_cast<unsigned char>(c), input);
        break;
      }
      }
    }
  }
  return true;
}

int HanMacWrdJText::computeNumPages(HanMacWrdJTextInternal::TextZone const &zone)
{
  if (zone.m_type!=HanMacWrdJTextInternal::TextZone::T_Main)
    return 1;
  if (!zone.m_entry.valid())
    return 0;
  librevenge::RVNGBinaryData data;
  if (!m_mainParser->decodeZone(zone.m_entry, data) || !data.size())
    return 0;

  MWAWInputStreamPtr input=MWAWInputStream::get(data, false);
  if (!input)
    return 0;
  int nPages = 1, actCol = 0, numCol=1, actSection = 1;

  if (m_state->m_sectionList.size()) {
    auto const &sec = m_state->m_sectionList[0];
    if (sec.m_numCols >= 1)
      numCol = sec.m_numCols;
  }
  input->seek(0, librevenge::RVNG_SEEK_SET);
  while (!input->isEnd()) {
    auto c = static_cast<int>(input->readULong(2));
    switch (c) {
    case 2:
      if (actCol < numCol-1 && numCol > 1)
        actCol++;
      else {
        actCol = 0;
        nPages++;
      }
      break;
    case 3:
      actCol = 0;
      nPages++;
      break;
    case 4: {
      if (size_t(actSection) >= m_state->m_sectionList.size())
        break;
      actCol = 0;
      nPages++;
      auto const &sec = m_state->m_sectionList[size_t(actSection++)];
      numCol = sec.m_numCols >= 1 ? sec.m_numCols : 1;
      break;
    }
    default:
      break;
    }
  }
  return nPages;
}

void HanMacWrdJText::updateTextZoneTypes(std::map<long,int> const &idTypeMap)
{
  auto numZones = static_cast<int>(m_state->m_textZoneList.size());
  for (auto it : idTypeMap) {
    if (m_state->m_idTextZoneMap.find(it.first)==m_state->m_idTextZoneMap.end()) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::updateTextZoneTypes: can not find text zone with id %lx\n", static_cast<long unsigned int>(it.first)));
      continue;
    }
    int zId = m_state->m_idTextZoneMap.find(it.first)->second;
    if (zId < 0 || zId >= numZones)
      continue;
    m_state->m_textZoneList[size_t(zId)].m_type=
      HanMacWrdJTextInternal::TextZone::Type(it.second);
  }
}

void HanMacWrdJText::updateFootnoteInformations(long const &textZId, std::vector<long> const &fPosList)
{
  m_state->m_ftnTextId = textZId;
  m_state->m_ftnFirstPosList = fPosList;
}

bool HanMacWrdJText::readTextZonesList(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: called without any entry\n"));
    return false;
  }
  if (entry.length() == 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: find an empty zone\n"));
    entry.setParsed(true);
    return true;
  }
  if (entry.length() < 12) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: the entry seems too short\n"));
    return false;
  }
  if (m_state->m_textZoneList.size()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: oops the text zone list os ,not empty\n"));
  }
  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(true);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=4) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: can not read an entry\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;
  long val;
  f << "listId=[" << std::hex;
  std::vector<long> listIds;
  for (int i = 0; i < mainHeader.m_n; i++) {
    val = long(input->readULong(4));
    m_state->m_idTextZoneMap[val]=i;
    listIds.push_back(val);
    f << val << ",";
  }
  f << std::dec << "],";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }
  m_state->m_textZoneList.resize(size_t(mainHeader.m_n));
  if (mainHeader.m_n)
    m_state->m_textZoneList[0].m_type=HanMacWrdJTextInternal::TextZone::T_Main;
  for (int i = 0; i < mainHeader.m_n; i++) {
    auto &zone=m_state->m_textZoneList[size_t(i)];
    zone.m_id = listIds[size_t(i)];
    // first a field of size 0x26
    pos = input->tell();
    f.str("");
    f << entry.name() << "-A" << i << ":";
    f << "id=" << std::hex << listIds[size_t(i)] << std::dec << ",";

    long dataSz = (pos+4>endPos) ? 0: long(input->readULong(4));
    long zoneEnd=pos+4+dataSz;
    if (zoneEnd > endPos) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: can not read first zone size for id=%d\n",i));
      f << "###sz=" << dataSz;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      return false;
    }
    if (dataSz < 38) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: first zone size for id=%d seems very short\n",i));
      f << "###";
    }
    else {
      int sel[3];
      for (auto &s : sel) s = static_cast<int>(input->readLong(4));
      if (sel[0] || sel[1] || sel[2]) {
        f << "select=" << sel[1] << "x" << sel[0];
        if (sel[1] != sel[2])
          f << "[" << sel[2] << "]";
        f << ",";
      }
      f << "listIds=[" << std::hex; // 5 or 6 id
      for (int j = 0; j < 6; j++) {
        val = long(input->readULong(4));
        if (val)
          f << val << ",";
        else
          f << "_,";
      }
      f << std::dec << "],";
      auto N1 = static_cast<int>(input->readULong(2)); // correspond to next N1
      f << "N=" << N1 << ",";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << entry.name() << "-B" << i << ":";
    f << "id=" << std::hex << listIds[size_t(i)] << std::dec << ",";

    pos = input->tell();
    HanMacWrdJZoneHeader header(false);
    if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=4) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: can not read second zone %d\n", i));
      f << "###" << header;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      if (header.m_length<16 || pos+4+header.m_length>endPos)
        return false;
      input->seek(pos+4+header.m_length, librevenge::RVNG_SEEK_SET);
      continue;
    }
    zoneEnd=pos+4+header.m_length;
    f << header;
    /* h0,h1,h2 seems to correspond to freeBlock, 0, 8[totalblock=8*..], h3=1*/
    f << "listId?=[" << std::hex;
    for (int j = 0; j < header.m_n; j++) {
      val = long(input->readULong(4));
      f << val << ",";
    }
    f << std::dec << "],";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    if (input->tell()!=zoneEnd)
      asciiFile.addDelimiter(input->tell(),'|');
    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  if (pos!=endPos) {
    f.str("");
    f << entry.name() << "[last]:###";
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZonesList: find unexpected end of data\n"));
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }

  return true;
}

bool HanMacWrdJText::readTextZone(MWAWEntry const &entry, int actZone)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: called without any entry\n"));
    return false;
  }
  if (entry.length() < 8+20*3) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: the entry seems too short\n"));
    return false;
  }

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  if (actZone >= static_cast<int>(m_state->m_textZoneList.size()) || actZone < 0) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: find an unexpected zone\n"));
    if (actZone < 0)
      actZone = static_cast<int>(m_state->m_textZoneList.size());
    m_state->m_textZoneList.resize(size_t(actZone)+1);
  }
  auto &zone = m_state->m_textZoneList[size_t(actZone)];
  long val;

  // first read the char properties list
  std::vector<HanMacWrdJTextInternal::PLC> cPLCList;
  std::vector<MWAWVec2i> cPLCPosList; // 0:line, 1:char

  f.str("");
  f << entry.name() << "-char:";

  pos = input->tell();
  HanMacWrdJZoneHeader header(false);
  bool ok = true;
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: can not read zone the char plc list\n"));
    f << "###";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    ok = false;
  }
  else {
    f << header;
    long zoneEnd=pos+4+header.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    for (int j = 0; j < header.m_n; j++) {
      pos = input->tell();
      f.str("");
      MWAWVec2i cPos;
      cPos[0] = static_cast<int>(input->readLong(2));
      cPos[1] = static_cast<int>(input->readLong(2));
      HanMacWrdJTextInternal::PLC plc;
      plc.m_type = HanMacWrdJTextInternal::CHAR;
      plc.m_id = static_cast<int>(input->readLong(2));
      val = long(input->readULong(2)); // 0|1|4|5|f|26|2d|42|4e|4f|..|6a|6e|7a|7d
      if (val) f << "#f0=" << std::hex << val << std::dec << ",";
      plc.m_extra = f.str();
      cPLCPosList.push_back(cPos);
      cPLCList.push_back(plc);

      f.str("");
      f << entry.name() << "-Char" << j << "]:lcPos=" << cPos << "," << plc;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      input->seek(pos+header.m_fieldSize, librevenge::RVNG_SEEK_SET);
    }

    if (input->tell() != zoneEnd) { // junk ?
      asciiFile.addDelimiter(input->tell(),'|');
      input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    }
  }

  // second read the ruler list
  std::map<int,HanMacWrdJTextInternal::PLC> rLineRulerMap;

  f.str("");
  f << entry.name() << "-ruler:";

  pos = input->tell();
  header=HanMacWrdJZoneHeader(false);
  if (!ok) {
  }
  else if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: can not read zone the ruler plc list\n"));
    f << "###";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    ok = false;
  }
  else {
    f << header;
    long zoneEnd=pos+4+header.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    for (int j = 0; j < header.m_n; j++) {
      pos = input->tell();
      f.str("");
      auto line = static_cast<int>(input->readLong(2));
      HanMacWrdJTextInternal::PLC plc;
      plc.m_type = HanMacWrdJTextInternal::RULER;
      plc.m_id = static_cast<int>(input->readLong(2));

      val = long(input->readULong(4)); // find 0,000008a0, 023ddd20, 0046ddbb, 5808001d, 5808004e
      if (val) f << "#f0=" << std::hex << val << std::dec << ",";
      plc.m_extra = f.str();
      if (rLineRulerMap.find(line) !=  rLineRulerMap.end()) {
        MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: already find a ruler for line=%d\n",line));
        f << "###";
      }
      else
        rLineRulerMap[line]=plc;

      f.str("");
      f << entry.name() << "-Ruler" << j << "]:line=" << line << "," << plc;
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      input->seek(pos+header.m_fieldSize, librevenge::RVNG_SEEK_SET);
    }

    if (input->tell() != zoneEnd) { // junk ?
      asciiFile.addDelimiter(input->tell(),'|');
      input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    }
  }

  // now we can read the line position
  std::vector<long> linePosList;

  f.str("");
  f << entry.name() << "-line:";
  pos = input->tell();
  header=HanMacWrdJZoneHeader(false);
  if (!ok) {
  }
  else if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=4) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: can not read zone the line plc list\n"));
    f << "###";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    ok = false;
  }
  else {
    f << header;
    long zoneEnd=pos+4+header.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    HanMacWrdJTextInternal::PLC plc;
    plc.m_type = HanMacWrdJTextInternal::LINE;

    f << "linePos=[" << std::hex;
    for (int j = 0; j < header.m_n; j++) {
      long linePos = input->readLong(4);
      linePosList.push_back(linePos);
      plc.m_id = j;
      zone.m_PLCMap.insert
      (std::multimap<long, HanMacWrdJTextInternal::PLC>::value_type(linePos, plc));
      f << linePos << ",";
    }
    f << std::dec << "],";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    if (input->tell() != zoneEnd) { // junk ?
      asciiFile.addDelimiter(input->tell(),'|');
      input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
    }
  }

  // ok, we can now update the plc
  if (ok) {
    auto nLines = int(linePosList.size());
    for (auto rIt : rLineRulerMap) {
      int line = rIt.first;
      if (line < 0 || line >= nLines) {
        MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: build rule plc, can not find line %d\n", line));
        continue;

      }
      zone.m_PLCMap.insert
      (std::multimap<long, HanMacWrdJTextInternal::PLC>::value_type(linePosList[size_t(line)], rIt.second));
    }
    size_t numCProp = cPLCPosList.size();
    if (numCProp != cPLCList.size()) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: cPLCPosList and cPLCList have not the same size\n"));
      if (numCProp > cPLCList.size())
        numCProp = cPLCList.size();
    }
    for (size_t i = 0; i < numCProp; i++) {
      int line = cPLCPosList[i][0];
      if (line < 0 || line >= nLines) {
        MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: build char plc, can not find line %d\n", line));
        continue;
      }
      long cPos = linePosList[size_t(line)]+long(cPLCPosList[i][1]);
      zone.m_PLCMap.insert
      (std::multimap<long, HanMacWrdJTextInternal::PLC>::value_type(cPos, cPLCList[i]));
    }
  }
  asciiFile.addPos(endPos);
  asciiFile.addNote("_");

  // now potentially a token zone, called with endPos-1 to avoid reading the last text zone
  readTextToken(endPos-1, zone);

  pos = input->tell();
  if (pos==endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: can not read find the last zone\n"));

    return true;
  }

  // normally now the text data but check if there remains some intermediar unparsed zone
  auto dataSz = long(input->readULong(4));
  while (dataSz>0 && pos+4+dataSz < endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: find some unparsed zone\n"));
    f.str("");
    f << entry.name() << "-###:";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    pos = pos+4+dataSz;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    dataSz = long(input->readULong(4));
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  // the text data
  f.str("");
  f << entry.name() << "-text:";
  dataSz = long(input->readULong(4));
  if (pos+4+dataSz>endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readTextZone: can not read last zone size\n"));
    f << "###sz=" << dataSz;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }

  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  zone.m_entry.setBegin(pos);
  zone.m_entry.setEnd(endPos);
  zone.m_entry.setName(entry.name());

  return true;
}

bool HanMacWrdJText::readTextToken(long endPos, HanMacWrdJTextInternal::TextZone &zone)
{
  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  long pos=input->tell();
  if (pos+4>=endPos)
    return true;

  f << "Entries(TextToken):";
  HanMacWrdJZoneHeader header(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=16 ||
      16+16*header.m_n+4 > header.m_length) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  int val;
  for (int i = 0; i < 2; i++) { // always 0?
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  f << header;
  long zoneEnd=pos+4+header.m_length;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  HanMacWrdJTextInternal::PLC plc(HanMacWrdJTextInternal::TOKEN);
  std::vector<int> bkmIdList;
  for (int i = 0; i < header.m_n; i++) {
    pos = input->tell();
    f.str("");
    HanMacWrdJTextInternal::Token tkn;
    long fPos = input->readLong(4);

    val = static_cast<int>(input->readLong(2)); // always 0 ?
    if (val) f << "f0=" << val << ",";
    tkn.m_length=static_cast<int>(input->readLong(2));
    tkn.m_id = long(input->readULong(4));

    tkn.m_type=static_cast<int>(input->readLong(1));
    for (int j=0; j < 2; j++) { // f1=0, f2=0|1[field]|11[footnote]
      val = static_cast<int>(input->readLong(1));
      if (val) f << "f" << j+1 << "=" << val << ",";
    }
    tkn.m_localId = static_cast<int>(input->readLong(1));
    tkn.m_extra = f.str();
    zone.m_tokenList.push_back(tkn);
    if (tkn.m_type==0x20)
      bkmIdList.push_back(i);
    plc.m_id = i;
    zone.m_PLCMap.insert(std::multimap<long, HanMacWrdJTextInternal::PLC>::value_type(fPos, plc));

    f.str("");
    f << "TextToken-" << i << ":";
    if (fPos) f << "fPos=" << std::hex << fPos << std::dec << ",";
    f << tkn;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+16, librevenge::RVNG_SEEK_SET);
  }

  if (input->tell() != zoneEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  }

  pos = input->tell();
  // next: we can find potential bookmark zone
  for (size_t i=0; i < bkmIdList.size(); i++) {
    pos = input->tell();
    auto dataSz = long(input->readULong(4));
    zoneEnd = pos+4+dataSz;
    if (input->isEnd() || dataSz<0 || zoneEnd >= endPos) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextToken: can not find bookmark text %d\n", int(i)));
      break;
    }

    f.str("");
    f << "TextToken-data" << i << ":";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    auto fSz=static_cast<int>(input->readULong(1));
    if (fSz == dataSz-2 || fSz==dataSz-1) {
      std::string bkmark("");
      for (int c=0; c < fSz; c++)
        bkmark+=char(input->readULong(1));
      f << bkmark;

      zone.m_tokenList[size_t(bkmIdList[i])].m_bookmark=bkmark;
    }
    else {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readTextToken: can not read bookmark text %d\n", int(i)));
      f << "###";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());

    pos = zoneEnd;
    input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);
  }
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
//     Fonts
////////////////////////////////////////////////////////////

// a single font
bool HanMacWrdJText::readFont(MWAWFont &font, long endPos)
{
  font = MWAWFont(-1,-1);

  MWAWInputStreamPtr input = m_parserState->m_input;
  long pos = input->tell(), debPos=pos;
  if (endPos <= 0) {
    auto dataSz=long(input->readULong(4));
    pos+=4;
    endPos=pos+dataSz;
    if (!input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readFont: pb reading font size\n"));
      input->seek(debPos, librevenge::RVNG_SEEK_SET);
      return false;
    }
  }
  long len=endPos-pos;
  if (len < 24) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFont: the zone is too short\n"));
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  libmwaw::DebugStream f;
  font.setId(static_cast<int>(input->readLong(2)));
  auto val = static_cast<int>(input->readLong(2));
  if (val) f << "#f1=" << val << ",";
  font.setSize(float(input->readLong(4))/65536.f);
  float expand = float(input->readLong(4))/65536.f;
  if (expand < 0 || expand > 0)
    font.setDeltaLetterSpacing(expand*font.size());
  float xScale = float(input->readLong(4))/65536.f;
  if (xScale < 1 || xScale > 1)
    font.setWidthStreching(xScale);

  auto flag =static_cast<int>(input->readULong(2));
  uint32_t flags=0;
  if (flag&1) {
    font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.setUnderlineType(MWAWFont::Line::Double);
  }
  if (flag&2)
    font.setUnderlineStyle(MWAWFont::Line::Dot);
  if (flag&4) {
    font.setUnderlineStyle(MWAWFont::Line::Dot);
    font.setUnderlineWidth(2.0);
  }
  if (flag&8)
    font.setUnderlineStyle(MWAWFont::Line::Dash);
  if (flag&0x10)
    font.setStrikeOutStyle(MWAWFont::Line::Simple);
  if (flag&0x20) {
    font.setStrikeOutStyle(MWAWFont::Line::Simple);
    font.setStrikeOutType(MWAWFont::Line::Double);
  }
  if (flag&0xFFC0)
    f << "#flag0=" << std::hex << (flag&0xFFF2) << std::dec << ",";
  flag =static_cast<int>(input->readULong(2));
  if (flag&1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) flags |= MWAWFont::outlineBit;
  if (flag&0x8) flags |= MWAWFont::shadowBit;
  if (flag&0x10) flags |= MWAWFont::reverseVideoBit;
  if (flag&0x20) font.set(MWAWFont::Script::super100());
  if (flag&0x40) font.set(MWAWFont::Script::sub100());
  if (flag&0x80) {
    if (flag&0x20)
      font.set(MWAWFont::Script(48,librevenge::RVNG_PERCENT,58));
    else if (flag&0x40)
      font.set(MWAWFont::Script(16,librevenge::RVNG_PERCENT,58));
    else
      font.set(MWAWFont::Script::super());
  }
  if (flag&0x100) {
    font.setOverlineStyle(MWAWFont::Line::Dot);
    font.setOverlineWidth(2.0);
  }
  if (flag&0x200) flags |= MWAWFont::boxedBit;
  if (flag&0x400) flags |= MWAWFont::boxedRoundedBit;
  if (flag&0x800) {
    font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.setUnderlineWidth(0.5);
  }
  if (flag&0x1000) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x2000) {
    font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.setUnderlineWidth(2.0);
  }
  if (flag&0x4000) {
    font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.setUnderlineWidth(3.0);
  }

  if (flag&0x8000) {
    font.setUnderlineStyle(MWAWFont::Line::Simple);
    font.setUnderlineType(MWAWFont::Line::Double);
    font.setUnderlineWidth(0.5);
  }
  auto color = static_cast<int>(input->readLong(2));
  MWAWColor col;
  if (color && m_mainParser->getColor(color, 1, col))
    font.setColor(col);
  else if (color)
    f << "##fColor=" << color << ",";
  val = static_cast<int>(input->readLong(2));
  if (val) f << "#unk=" << val << ",";
  if (len >= 28) {
    for (int i = 0; i < 2; i++) {
      val = static_cast<int>(input->readLong(2));
      if (val) f << "#g" << i << "=" << val << ",";
    }
  }
  if (len >= 36) {
    color = static_cast<int>(input->readLong(2));
    auto pattern = static_cast<int>(input->readLong(2));
    if ((color || pattern) && m_mainParser->getColor(color, pattern, col))
      font.setBackgroundColor(col);
    else if (color || pattern)
      f << "#backColor=" << color << ", #pattern=" << pattern << ",";
  }
  if (input->tell() != endPos)
    m_parserState->m_asciiFile.addDelimiter(input->tell(),'|');
  font.setFlags(flags);
  font.m_extra = f.str();

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool HanMacWrdJText::readFonts(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFonts: called without any entry\n"));
    return false;
  }
  if (entry.length() <= 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFonts: the entry seems too short\n"));
    return false;
  }
  if (m_state->m_fontList.size()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFonts: oops the font list is not empty\n"));
    m_state->m_fontList.resize(0);
  }
  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(false);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFonts: can not read the header\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;
  f << "unk=[";
  for (int i = 0; i < mainHeader.m_n; i++) {
    f << "[";
    long val = input->readLong(2); // always -2 ?
    if (val!=-2)
      f << val << ",";
    else
      f << "_,";
    val = long(input->readULong(2)); // 0 or 5020 : junk?
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
    val = long(input->readULong(4)); // id
    f << std::hex << val << std::dec;
    f << "]";
  }
  f << "],";
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < mainHeader.m_n; i++) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-" << i << ":";
    MWAWFont font(-1,-1);
    if (!readFont(font) || input->tell() > endPos) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readFonts: can not read font %d\n", i));
      f << "###";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());
      return false;
    }
    f << font.getDebugString(m_parserState->m_fontConverter) << ",";
    m_state->m_fontList.push_back(font);
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }
  asciiFile.addPos(endPos);
  asciiFile.addNote("_");
  return true;
}

// the list of fonts
bool HanMacWrdJText::readFontNames(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: called without any entry\n"));
    return false;
  }
  if (entry.length() < 28) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: the entry seems too short\n"));
    return false;
  }
  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  f << entry.name() << "[data]:";

  long pos = entry.begin()+8; // skip header
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  int N, val;
  auto readDataSz = long(input->readULong(4));
  if (readDataSz+12 != entry.length()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: the data size seems odd\n"));
    f << "##dataSz=" << readDataSz << ",";
  }
  N = static_cast<int>(input->readLong(2));
  f << "N=" << N << ",";
  auto fieldSz = long(input->readULong(4));
  if (fieldSz != 68) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: the field size seems odd\n"));
    f << "##fieldSz=" << fieldSz << ",";
  }
  for (int i = 0; i < 3; i++) { //f1=f2=1
    val = static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto id = long(input->readULong(4));
  if (id) f << "id=" << std::hex << id << std::dec << ",";

  long expectedSz = N*68+28;
  if (expectedSz != entry.length() && expectedSz+1 != entry.length()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: the entry size seems odd\n"));
    return false;
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < N; i++) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-" << i << ":";
    auto fId = static_cast<int>(input->readLong(2));
    f << "fId=" << fId << ",";
    val = static_cast<int>(input->readLong(2));
    if (val != fId)
      f << "#fId2=" << val << ",";
    auto fSz = static_cast<int>(input->readULong(1));
    if (fSz+5 > 68) {
      f << "###fSz";
      MWAW_DEBUG_MSG(("HanMacWrdJText::readFontNames: can not read a font\n"));
    }
    else {
      std::string name("");
      for (int c = 0; c < fSz; c++)
        name += char(input->readULong(1));
      f << name;
      m_parserState->m_fontConverter->setCorrespondance(fId, name);
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+68, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(entry.end());
  asciiFile.addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
//     Style
////////////////////////////////////////////////////////////
bool HanMacWrdJText::readStyles(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readStyles: called without any zone\n"));
    return false;
  }

  long dataSz = entry.length();
  if (dataSz < 4) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readStyles: the zone seems too short\n"));
    return false;
  }
  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  f << entry.name() << "[header]:";

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  HanMacWrdJZoneHeader mainHeader(false);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=4) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readStyles: can not read the header\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;

  f << "listIds=[" << std::hex;
  for (int i = 0; i < mainHeader.m_n; i++)
    f << input->readULong(4) << ",";
  f << std::dec << "],";
  input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < mainHeader.m_n; i++) {
    f.str("");
    f << entry.name() << "-" << i << ":";
    pos = input->tell();
    long fieldSz = long(input->readULong(4))+4;
    if (fieldSz < 0x1bc || pos+fieldSz > endPos) {
      f << "###";
      asciiFile.addPos(pos);
      asciiFile.addNote(f.str().c_str());

      MWAW_DEBUG_MSG(("HanMacWrdJText::readStyles: can not read field %d\n", i));
      return true;
    }
    auto val = static_cast<int>(input->readULong(1));
    if (val != i) f << "#id=" << val << ",";

    // f0=c2|c6, f2=0|44, f3=1|14|15|16: fontId?
    for (int j=0; j < 5; j++) {
      val = static_cast<int>(input->readULong(1));
      if (val)
        f << "f" << j << "=" << std::hex << val << std::dec << ",";
    }
    /* g1=9|a|c|e|12|18: size ?, g5=1, g8=0|1, g25=0|18|30|48, g31=1, g35=0|1 */
    for (int j=0; j < 33; j++) {
      val = static_cast<int>(input->readULong(2));
      if (val)
        f << "g" << j << "=" << val  << ",";
    }
    for (int j=0; j < 4; j++) { // b,b,b,0
      val = static_cast<int>(input->readULong(1));
      if ((j < 3 && val != 0xb) || (j==3 && val))
        f << "h" << j << "=" << val  << ",";
    }

    for (int j=0; j < 17; j++) { // always 0
      val = static_cast<int>(input->readULong(2));
      if (val)
        f << "l" << j << "=" << val  << ",";
    }
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    long pos2 = input->tell();
    f.str("");
    f << entry.name() << "-" << i << "[B]:";
    // checkme probably f15=numTabs  ..
    for (int j = 0; j < 50; j++) {
      val = static_cast<int>(input->readULong(2));
      if ((j < 5 && val != 1) || (j >= 5 && val))
        f << "f" << j << "=" << val  << ",";
    }
    for (int j = 0; j < 50; j++) {
      val = static_cast<int>(input->readULong(2));
      if (val)
        f << "g" << j << "=" << val  << ",";
    }
    for (int j = 0; j < 43; j++) {
      val = static_cast<int>(input->readULong(2));
      if (val)
        f << "h" << j << "=" << val  << ",";
    }
    asciiFile.addPos(pos2);
    asciiFile.addNote(f.str().c_str());

    pos2 = input->tell();
    f.str("");
    f << entry.name() << "-" << i << "[C]:";
    val = static_cast<int>(input->readLong(2));
    if (val != -1) f << "unkn=" << val << ",";
    val  = static_cast<int>(input->readLong(2));
    if (val != i) f << "#id" << val << ",";
    for (int j = 0; j < 4; j++) {
      val = static_cast<int>(input->readLong(2));
      if (val) f << "f" << j << "=" << val << ",";
    }
    auto fSz = static_cast<int>(input->readULong(1));
    if (input->tell()+fSz > pos+fieldSz) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readStyles: can not read styleName\n"));
      f << "###";
    }
    else {
      std::string name("");
      for (int j = 0; j < fSz; j++)
        name +=char(input->readULong(1));
      f << name;
    }
    asciiFile.addPos(pos2);
    asciiFile.addNote(f.str().c_str());
    if (input->tell() != pos+fieldSz)
      asciiFile.addDelimiter(input->tell(),'|');

    input->seek(pos+fieldSz, librevenge::RVNG_SEEK_SET);
  }

  if (!input->isEnd()) {
    asciiFile.addPos(input->tell());
    asciiFile.addNote("_");
  }
  return true;
}

////////////////////////////////////////////////////////////
//     Paragraph
////////////////////////////////////////////////////////////
bool HanMacWrdJText::readParagraph(HanMacWrdJTextInternal::Paragraph &para, long endPos)
{
  para = HanMacWrdJTextInternal::Paragraph();

  MWAWInputStreamPtr input = m_parserState->m_input;
  long pos = input->tell(), debPos=pos;
  if (endPos <= 0) {
    auto dataSz=long(input->readULong(4));
    pos+=4;
    endPos=pos+dataSz;
    if (!input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraph: pb reading para size\n"));
      input->seek(debPos, librevenge::RVNG_SEEK_SET);
      return false;
    }
  }
  long len=endPos-pos;
  if (len < 102) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraph: the zone is too short\n"));
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  libmwaw::DebugStream f;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  auto flags = static_cast<int>(input->readULong(1));
  if (flags&0x80)
    para.m_breakStatus = para.m_breakStatus.get()|MWAWParagraph::NoBreakWithNextBit;
  if (flags&0x40)
    para.m_breakStatus = para.m_breakStatus.get()|MWAWParagraph::NoBreakBit;
  if (flags&0x2)
    para.m_addPageBreak = true;
  if (flags&0x4)
    f << "linebreakByWord,";

  if (flags & 0x39) f << "#fl=" << std::hex << (flags&0x39) << std::dec << ",";

  auto val = static_cast<int>(input->readLong(2));
  if (val) f << "#f0=" << val << ",";
  val = static_cast<int>(input->readULong(2));
  switch (val&3) {
  case 0:
    para.m_justify = MWAWParagraph::JustificationLeft;
    break;
  case 1:
    para.m_justify = MWAWParagraph::JustificationRight;
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    break;
  default:
    break;
  }
  if (val&0xFFFC) f << "#f1=" << val << ",";
  val = static_cast<int>(input->readLong(1));
  if (val) f << "#f2=" << val << ",";
  para.m_type = static_cast<int>(input->readLong(2));

  float dim[3];
  for (auto &d : dim) d = float(input->readLong(4))/65536.0f;
  para.m_marginsUnit = librevenge::RVNG_POINT;
  para.m_margins[0]=double(dim[1]);
  para.m_margins[1]=double(dim[0]);
  para.m_margins[2]=double(dim[2]); // ie. distance to right border - ...

  for (auto &spacing : para.m_spacings)
    spacing = double(input->readLong(4))/65536.0;
  int spacingsUnit[3]; // 1=mm, ..., 4=pt, b=line
  for (auto &s : spacingsUnit) s= static_cast<int>(input->readULong(1));
  if (spacingsUnit[0]==0xb)
    para.m_spacingsInterlineUnit = librevenge::RVNG_PERCENT;
  else
    para.m_spacingsInterlineUnit = librevenge::RVNG_POINT;
  for (int i = 1; i < 3; i++) // convert point|line -> inches
    para.m_spacings[i]= ((spacingsUnit[i]==0xb) ? 12.0 : 1.0)*(para.m_spacings[i].get())/72.0;

  val = static_cast<int>(input->readLong(1));
  if (val) f << "#f3=" << val << ",";
  for (int i = 0;  i< 2; i++) { // one time f4=8000
    val = static_cast<int>(input->readULong(2));
    if (val) f << "#f" << i+4 << "=" << std::hex << val << std::dec << ",";
  }
  // the borders
  char const *wh[5] = { "T", "L", "B", "R", "VSep" };
  MWAWBorder borders[5];
  for (auto &border : borders) border.m_width = double(input->readLong(4))/65536.;
  for (int d=0; d < 5; d++) {
    val = static_cast<int>(input->readULong(1));
    switch (val) {
    case 0: // normal
      break;
    case 1:
      borders[d].m_type = MWAWBorder::Double;
      break;
    case 2:
      borders[d].m_type = MWAWBorder::Double;
      f << "bord" << wh[d] << "[ext=2],";
      break;
    case 3:
      borders[d].m_type = MWAWBorder::Double;
      f << "bord" << wh[d] << "[int=2],";
      break;
    default:
      f << "#bord" << wh[d] << "[style=" << val << "],";
      break;
    }
  }
  int color[5], pattern[5];
  for (auto &c : color) c = static_cast<int>(input->readULong(1));
  for (auto &p : pattern) p= static_cast<int>(input->readULong(2));
  for (int d=0; d < 5; d++) {
    if (!color[d] && !pattern[d])
      continue;
    MWAWColor col;
    if (m_mainParser->getColor(color[d], pattern[d], col))
      borders[d].m_color = col;
    else
      f << "#bord" << wh[d] << "[col=" << color[d] << ",pat=" << pattern[d] << "],";
  }
  // update the paragraph
  para.resizeBorders(6);
  libmwaw::Position const which[5] = {
    libmwaw::Top, libmwaw::Left, libmwaw::Bottom, libmwaw::Right,
    libmwaw::VMiddle
  };
  for (int d=0; d < 5; d++) {
    if (borders[d].m_width <= 0)
      continue;
    para.m_borders[which[d]]=borders[d];
  }
  val = static_cast<int>(input->readLong(1));
  if (val) f << "#f6=" << val << ",";
  double bMargins[5]= {0,0,0,0,0};
  for (int d = 0; d < 5; d++) {
    bMargins[d] =  double(input->readLong(4))/256./65536./72.;
    if (bMargins[d] > 0 || bMargins[d] < 0)
      f << "bordMarg" << wh[d] << "=" << bMargins[d] << ",";
  }
  auto nTabs = static_cast<int>(input->readULong(1));
  if (input->tell()+2+nTabs*12 > endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraph: can not read numbers of tab\n"));
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  val = static_cast<int>(input->readULong(2));
  if (val) f << "#h3=" << val << ",";
  para.m_extra=f.str();
  f.str("");
  f << "Ruler:" << para;

  asciiFile.addPos(debPos);
  asciiFile.addNote(f.str().c_str());
  for (int i = 0; i < nTabs; i++) {
    pos = input->tell();
    f.str("");
    f << "Ruler[Tabs-" << i << "]:";

    MWAWTabStop tab;
    val = static_cast<int>(input->readULong(1));
    switch (val) {
    case 0:
      break;
    case 1:
      tab.m_alignment = MWAWTabStop::CENTER;
      break;
    case 2:
      tab.m_alignment = MWAWTabStop::RIGHT;
      break;
    case 3:
      tab.m_alignment = MWAWTabStop::DECIMAL;
      break;
    case 4:
      tab.m_alignment = MWAWTabStop::BAR;
      break;
    default:
      f << "#type=" << val << ",";
      break;
    }
    val = static_cast<int>(input->readULong(1));
    if (val) f << "barType=" << val << ",";
    val = static_cast<int>(input->readULong(2));
    if (val) {
      int unicode= m_parserState->m_fontConverter->unicode(3, static_cast<unsigned char>(val));
      if (unicode==-1)
        tab.m_decimalCharacter = uint16_t(val);
      else
        tab.m_decimalCharacter = uint16_t(unicode);
    }
    val = static_cast<int>(input->readULong(2));
    if (val) {
      int unicode= m_parserState->m_fontConverter->unicode(3, static_cast<unsigned char>(val));
      if (unicode==-1)
        tab.m_leaderCharacter = uint16_t(val);
      else
        tab.m_leaderCharacter = uint16_t(unicode);
    }
    val = static_cast<int>(input->readULong(2)); // 0|73|74|a044|f170|f1e0|f590
    if (val) f << "f0=" << std::hex << val << std::dec << ",";

    tab.m_position = double(input->readLong(4))/65536./72.;
    para.m_tabs->push_back(tab);
    f << tab;

    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=endPos) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool HanMacWrdJText::readParagraphs(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraphs: called without any entry\n"));
    return false;
  }
  if (entry.length() <= 8) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraphs: the entry seems too short\n"));
    return false;
  }
  if (m_state->m_paragraphList.size()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraphs: oops the paragraph list is not empty\n"));
    m_state->m_paragraphList.resize(0);
  }

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);

  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();
  input->seek(pos, librevenge::RVNG_SEEK_SET);

  // first read the header
  f << entry.name() << "[header]:";
  HanMacWrdJZoneHeader mainHeader(false);
  if (!m_mainParser->readClassicHeader(mainHeader,endPos) || mainHeader.m_fieldSize!=12) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraphs: can not read the header\n"));
    f << "###sz=" << mainHeader.m_length;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  long headerEnd=pos+4+mainHeader.m_length;
  f << mainHeader;

  f << "unk=[";
  for (int i = 0; i < mainHeader.m_n; i++) {
    f << "[";
    long val = input->readLong(2); // always -2 ?
    if (val!=-2)
      f << "unkn0=" << val << ",";
    val = long(input->readULong(2)); // 0|1|2|5
    if (val)
      f << "type=" << val << ",";
    val = long(input->readULong(4)); // a id
    if (val)
      f << "id1=" << std::hex << val << std::dec << ",";
    val = long(input->readULong(4));
    if (val)
      f << "id2=" << std::hex << val << std::dec << ",";
    f << "]";
  }
  f << "],";
  if (input->tell()!=headerEnd) {
    asciiFile.addDelimiter(input->tell(),'|');
    input->seek(headerEnd, librevenge::RVNG_SEEK_SET);
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < mainHeader.m_n; i++) {
    pos = input->tell();
    HanMacWrdJTextInternal::Paragraph paragraph;
    if (!readParagraph(paragraph) || input->tell() > endPos) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readParagraphs: can not read paragraph %d\n", i));
      asciiFile.addPos(pos);
      asciiFile.addNote("Ruler###");
      return false;
    }
    m_state->m_paragraphList.push_back(paragraph);
  }
  asciiFile.addPos(endPos);
  asciiFile.addNote("_");
  return true;
}

////////////////////////////////////////////////////////////
//     the sections/footnotes
////////////////////////////////////////////////////////////
bool HanMacWrdJText::readSections(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: called without any entry\n"));
    return false;
  }
  if (entry.length() < 20) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: the entry seems too short\n"));
    return false;
  }
  if (m_state->m_sectionList.size()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: the list of section is not empty\n"));
    m_state->m_sectionList.resize(0);
  }
  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << entry.name() << "[header]:";
  long val;
  for (int i = 0; i < 2; i++) { // fl0=0|0
    val = long(input->readULong(1));
    if (val) f << "fl" << i << "=" << std::hex << val << std::hex << ",";
  }
  for (int i = 0; i < 3; i++) { // f0=1, f1=N, f2=0
    val = input->readLong(2);
    if (val) f << "f" << i << "=" << val << ",";
  }
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  auto dataSz = long(input->readULong(4));
  if (!dataSz) return true;

  input->seek(-4, librevenge::RVNG_SEEK_CUR);
  pos = input->tell();
  f.str("");
  f << entry.name() << ":";
  HanMacWrdJZoneHeader header(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=0x5c) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: can not read second zone\n"));
    f << "###" << header;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  f << header;
  long zoneEnd=pos+4+header.m_length;
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  for (int i = 0; i < header.m_n; i++) {
    pos = input->tell();
    f.str("");
    HanMacWrdJTextInternal::Section sec;
    sec.m_id= input->readLong(2);
    val = input->readLong(2); // almost alway=id, but not always
    if (val != sec.m_id)
      f << "#id2=" << val << ",";
    val = input->readLong(2); // 0|1
    if (val) f << "f0=" << val << ",";
    val = long(input->readULong(2));
    auto numCol = int(val >> 12);
    if (numCol<=0 || numCol > 8) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: can not determine the num of columns\n"));
      f << "#numCols=" << numCol << ",";
      numCol = 1;
    }
    else
      sec.m_numCols=numCol;
    bool differentWidth=(val & 0xFFF) == 0;
    if (val & 0x7FF)
      f << "#fl=" << std::hex << (val & 0xFFF) << ",";
    if (differentWidth) {
      for (int j = 0; j < numCol; j++) {
        sec.m_colWidth.push_back(double(input->readLong(4))/65536);
        sec.m_colSep.push_back(double(input->readLong(4))/65536);
      }
    }
    else {
      sec.m_colWidth.push_back(double(input->readLong(4))/65536);
      sec.m_colSep.push_back(double(input->readLong(4))/65536);
    }
    sec.m_extra = f.str();
    m_state->m_sectionList.push_back(sec);

    f.str("");
    f << entry.name() << "-" << i << ":" << sec;

    asciiFile.addDelimiter(input->tell(), '|');
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+0x5c, librevenge::RVNG_SEEK_SET);
  }
  input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);

  pos = input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readSections: find unexpected end data\n"));
    f.str("");
    f << entry.name() << "###:";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }
  return true;
}

bool HanMacWrdJText::readFtnPos(MWAWEntry const &entry)
{
  if (!entry.valid()) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFtnPos: called without any entry\n"));
    return false;
  }
  if (entry.length() < 16) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFtnPos: the entry seems too short\n"));
    return false;
  }
  long pos = entry.begin()+8; // skip header
  long endPos = entry.end();

  MWAWInputStreamPtr input = m_parserState->m_input;
  libmwaw::DebugFile &asciiFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  entry.setParsed(true);
  input->seek(pos, librevenge::RVNG_SEEK_SET);
  f << entry.name() << "[header]:";
  auto val = long(input->readULong(2)); // always 0x200
  if (val!=0x2000)
    f << "f0=" << std::hex << val << std::dec << ",";
  val = input->readLong(2); // always 1 ?
  if (val != 1)
    f << "f1=" << val << ",";
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  auto dataSz = long(input->readULong(4));
  if (!dataSz) return true;

  input->seek(-4, librevenge::RVNG_SEEK_CUR);
  pos = input->tell();
  f.str("");
  f << entry.name() << ":";
  HanMacWrdJZoneHeader header(false);
  if (!m_mainParser->readClassicHeader(header,endPos) || header.m_fieldSize!=20||
      16+20*header.m_n+28 > header.m_length) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFtnPos: can not read second zone\n"));
    f << "###" << header;
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    return false;
  }
  f << header;
  long zoneEnd=pos+4+header.m_length;
  asciiFile.addDelimiter(input->tell(),'|');
  asciiFile.addPos(pos);
  asciiFile.addNote(f.str().c_str());

  input->seek(28, librevenge::RVNG_SEEK_CUR);
  for (int i = 0; i < header.m_n; i++) {
    pos = input->tell();
    f.str("");
    f << entry.name() << "-" << i << ":";
    val = long(input->readULong(1));
    if (val!=0x11) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::readFtnPos: find unexpected type\n"));
      f << "#type=" << std::hex << val << std::dec << ",";
    }
    f << "id=" << input->readLong(1) << ",";
    for (int j = 0; j < 5; j++) { // always 0?
      val = input->readLong(2);
      if (val) f << "f" << j << "=" << val << ",";
    }
    // the text id
    f << "zId[text]=" << std::hex << input->readULong(4) << std::dec << ",";
    // the footnote id
    f << "zId[footnote]=" << std::hex << input->readULong(4) << std::dec << ",";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  input->seek(zoneEnd, librevenge::RVNG_SEEK_SET);

  pos = input->tell();
  if (pos!=endPos) {
    MWAW_DEBUG_MSG(("HanMacWrdJText::readFtnPos: find unexpected end data\n"));
    f.str("");
    f << entry.name() << "###:";
    asciiFile.addPos(pos);
    asciiFile.addNote(f.str().c_str());
  }
  return true;
}
////////////////////////////////////////////////////////////
//
// Low level
//
////////////////////////////////////////////////////////////

//! send data to the listener

void HanMacWrdJText::flushExtra()
{
  if (!m_parserState->m_textListener) return;

#ifdef DEBUG
  for (auto &zone : m_state->m_textZoneList) {
    if (zone.m_parsed)
      continue;

    static bool first = true;
    if (first) {
      MWAW_DEBUG_MSG(("HanMacWrdJText::flushExtra: find some unsent zone\n"));
      first = false;
    }
    sendText(zone,0);
    m_parserState->m_textListener->insertEOL();
  }
#endif
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
