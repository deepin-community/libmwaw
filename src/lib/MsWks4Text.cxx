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

#ifdef DEBUG
// set to 1 to debug the font property
#  define DEBUG_FP 1
// set to 1 to debug the paragraph property
#  define DEBUG_PP 1
// set to 1 to print the plc position
#  define DEBUG_PLC_POS 1
#else
#  define DEBUG_FP 0
#  define DEBUG_PP 0
#  define DEBUG_PLC_POS 0
#endif

#include <iomanip>
#include <iostream>

#include <map>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWListener.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWParagraph.hxx"

#include "MsWks4Zone.hxx"
#include "MsWksDocument.hxx"

#include "MsWks4Text.hxx"

//////////////////////////////////////////////////////////////////////////////
// general enum
//////////////////////////////////////////////////////////////////////////////
namespace MsWks4TextInternal
{
/** different types
 *
 * - BTE: font/paragraph properties
 * - PGD: page break
 * - FTNT: footnote
 * - TOKEN: field type
 */
enum PLCType { BTE=0, PGD, FTNT, TOKEN, EOBJ, Unknown};
}

////////////////////////////////////////////////////////////
// Low level struct to read the plc
////////////////////////////////////////////////////////////
/** Internal and low level: the structures of a MsWks4Text used to parse PLC*/
namespace MsWks4PLCInternal
{
/** Internal and low level: the PLC different types and their structures */
struct PLC {
  /** the PLC types */
  typedef enum MsWks4TextInternal::PLCType PLCType;
  /** the way to define the text positions
   *
   * - P_ABS: absolute position,
   * - P_REL: position are relative to the beginning text offset,
   * - P_INCR: position are the length of text consecutive zones */
  typedef enum { P_ABS=0, P_REL, P_INCR, P_UNKNOWN} Position;
  /** the type of the content
   *
   * - T_CST: size is constant
   * - T_STRUCT: a structured type ( which unknown size) */
  typedef enum { T_CST=0, T_STRUCT, T_UNKNOWN} Type;
  //! constructor
  PLC(PLCType w= MsWks4TextInternal::Unknown, Position p=P_UNKNOWN, Type t=T_UNKNOWN)
    : m_type(w)
    , m_pos(p)
    , m_contentType(t)
  {
  }
  //! PLC type
  PLCType m_type;
  //! the way to define the text positions
  Position m_pos;
  //! the type of the content
  Type m_contentType;
};

//! a map of known plc
struct KnownPLC {
public:
  //! creates the mapping
  KnownPLC()
    : m_knowns()
  {
    createMapping();
  }

  //! returns the PLC corresponding to a name
  PLC get(std::string const &name)
  {
    auto pos = m_knowns.find(name);
    if (pos == m_knowns.end()) return PLC();
    return pos->second;
  }

protected:
  //! creates the map of known PLC
  void createMapping()
  {
    m_knowns["BTEP"] =
      PLC(MsWks4TextInternal::BTE,PLC::P_ABS, PLC::T_CST);
    m_knowns["BTEC"] =
      PLC(MsWks4TextInternal::BTE,PLC::P_ABS, PLC::T_CST);
    m_knowns["FTNT"] =
      PLC(MsWks4TextInternal::FTNT,PLC::P_REL, PLC::T_STRUCT);
    m_knowns["PGD "] =
      PLC(MsWks4TextInternal::PGD,PLC::P_REL, PLC::T_STRUCT);
    m_knowns["TOKN"] =
      PLC(MsWks4TextInternal::TOKEN,PLC::P_REL,PLC:: T_STRUCT);
    m_knowns["EOBJ"] =
      PLC(MsWks4TextInternal::EOBJ,PLC::P_REL, PLC::T_STRUCT);
  }

  //! map name -> known PLC
  std::map<std::string, PLC> m_knowns;
};
}

////////////////////////////////////////////////////////////
// Internal structures to store/parse the Zone
////////////////////////////////////////////////////////////

/** Internal: the structures of a MsWks4Text */
namespace MsWks4TextInternal
{
/** Internal: class to store a font name: name with sysid */
class FontName
{
public:
  //! constructor
  FontName()
    : m_name("")
    , m_id(-1)
    , m_unknown(0)
  {
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, FontName const &ft);

  //! the font name
  std::string m_name;
  //! the font id
  int m_id;
  //! unknown
  int m_unknown;
};

std::ostream &operator<<(std::ostream &o, FontName const &ft)
{
  o << "Font(name=" << ft.m_name << ", id=" << ft.m_id;
  if (ft.m_unknown != 0) o << ", unk=" << ft.m_unknown;
  o << ")";
  return o;
}

/** Internal: class to store a font properties */
struct Font {
  //! the field type: pagenumber, note, DTT=date/time/type
  enum FieldType { None, Page, Eobj, Note, DTT, DTTUnk};
  //! constructor: default value Geneva:12
  explicit Font(int fId=3, int size=12)
    : m_font(fId,float(size))
    , m_fieldType(None)
    , m_error("")
  {
  }
  //! copy constructor
  explicit Font(MWAWFont const &ft)
    : m_font(ft)
    , m_fieldType(None)
    , m_error("")
  {
  }
  //! operator <<
  friend std::ostream &operator<<(std::ostream &o, Font const &ft);
  //! the font
  MWAWFont m_font;
  //! the field type
  FieldType m_fieldType;

  //! a string used to store the parsing errors
  std::string m_error;
};
//! operator<< for a font
std::ostream &operator<<(std::ostream &o, Font const &ft)
{
  o << std::dec;
  switch (ft.m_fieldType) {
  case Font::Page:
    o << ", field[Page]";
    break;
  case Font::Eobj:
    o << ", field[Eobj]";
    break;
  case Font::Note:
    o << ", field[Note]";
    break;
  case Font::DTT:
  case Font::DTTUnk:
    o << ", field[with content]";
    break;
  case Font::None:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  if (!ft.m_error.empty()) o << ", errors=(" << ft.m_error << ")";
  return o;
}

/** Internal: class to store a paragraph properties */
struct Paragraph final : public MWAWParagraph {
  //! constructor
  Paragraph()
    : MWAWParagraph()
    , m_pageBreak(false)
  {
  }
  Paragraph(Paragraph const &)=default;
  Paragraph &operator=(Paragraph const &)=default;
  //! destructor
  ~Paragraph() final;
  //! operator <<
  friend std::ostream &operator<<(std::ostream &o, Paragraph const &ind);

  //! flag to know if there is a pagebreak before the paragraph
  bool m_pageBreak;
};
//! operator<<
std::ostream &operator<<(std::ostream &o, Paragraph const &ind)
{
  o << static_cast<MWAWParagraph const &>(ind);
  if (ind.m_pageBreak) o << "pgBrk, ";
  return o;
}

Paragraph::~Paragraph()
{
}

/** Internal: class to store footnote definition */
struct Ftnt {
  //! constructor
  Ftnt()
    : m_type(-1)
    , m_id(-1)
    , m_begin(-1)
    , m_end(-1)
    , m_error("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Ftnt const &ftnt);
  /** the note type
   *
   * Checkme: can we find other values than 1 ?*/
  int m_type;
  //! the footnote number
  int m_id;
  long m_begin /**the first offset in the text*/, m_end/**the last offset in the text*/;
  //! a string used to store the parsing errors
  std::string m_error;
};
//!operator<< for footnote
std::ostream &operator<<(std::ostream &o, Ftnt const &ftnt)
{
  o << std::dec;
  switch (ftnt.m_type) {
  case 1:
    o << "footnote,";
    break;
  default:
    o << "###type=" << ftnt.m_type << ",";
  }
  if (ftnt.m_id != -1) o << "id=" << ftnt.m_id;
  if (ftnt.m_begin > 0)
    o << std::hex << ",pos=" << ftnt.m_begin
      << "(" << ftnt.m_end-ftnt.m_begin <<")" << std::dec;
  if (!ftnt.m_error.empty()) o << ",error=(" << ftnt.m_error << ")";
  return o;
}

/** Internal: class to store field definition: TOKN entry*/
struct Token {
  //! constructor
  Token()
    : m_type(MWAWField::None)
    , m_textLength(-1)
    , m_unknown(-1)
    , m_error("")
  {
  }
  //! operator <<
  friend std::ostream &operator<<(std::ostream &o, Token const &tok);
  //! the type
  MWAWField::Type m_type;
  //! the length of the text corresponding to the token
  int m_textLength;
  //! unknown field
  int m_unknown;
  //! a string used to store the parsing errors
  std::string m_error;
};
//! operator<< for Token
std::ostream &operator<<(std::ostream &o, Token const &tok)
{
  o << std::dec;
  switch (tok.m_type) {
  case MWAWField::PageCount:
    o << "field[pageCount],";
    break;
  case MWAWField::PageNumber:
    o << "field[page],";
    break;
  case MWAWField::Date:
    o << "field[date],";
    break;
  case MWAWField::Time:
    o << "field[time],";
    break;
  case MWAWField::Title:
    o << "field[title],";
    break;
  case MWAWField::Database:
    o << "field[database],";
    break;
  case MWAWField::BookmarkStart:
  case MWAWField::BookmarkEnd:
  case MWAWField::None:
#if !defined(__clang__)
  default:
#endif
    o << "##field[unknown]" << ",";
    break;
  }
  if (tok.m_textLength != -1) o << "textLen=" << tok.m_textLength << ",";
  if (tok.m_unknown != -1) o << "unkn=" << std::hex << tok.m_unknown << std::dec << ",";
  if (!tok.m_error.empty()) o << "err=[" << tok.m_error << "]";
  return o;
}

/** Internal: class to store field definition: TOKN entry*/
struct Object {
  //! constructor
  Object()
    : m_type(-1)
    , m_id(-1)
    , m_dim()
    , m_fileId(-1)
    , m_error("")
  {
  }
  //! operator <<
  friend std::ostream &operator<<(std::ostream &o, Object const &tok);
  //! the object type
  int m_type;
  //! the local id
  int m_id;
  //! the dimension
  MWAWVec2i m_dim;
  //! the file id
  long m_fileId;
  //! a string used to store the parsing errors
  std::string m_error;
};
//! operator<< for Object
std::ostream &operator<<(std::ostream &o, Object const &obj)
{
  if (obj.m_type != 1) o << "###type=" << obj.m_type << ",";
  if (obj.m_id >= 0) o << "id=" << obj.m_id << ",";
  o << "dim=" << obj.m_dim << ",";
  if (obj.m_fileId > 0) o << "X" << std::hex << obj.m_fileId << std::dec << ",";
  if (!obj.m_error.empty()) o << "err=[" << obj.m_error << "]";
  return o;
}

/** Internal: class to store the PLC: Pointer List Content ? */
struct DataPLC {
  //! constructor
  DataPLC()
    : m_name("")
    , m_type(Unknown)
    , m_value(-1)
    , m_error("")
  {
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, DataPLC const &plc);
  //! the entry name
  std::string m_name;
  //! the type
  PLCType m_type;
  //! a potential value
  long m_value;
  //! a string used to store the parsing errors
  std::string m_error;
};
//! operator<< for DataPLC
std::ostream &operator<<(std::ostream &o, DataPLC const &plc)
{
  o << "type=" << plc.m_name << ",";
  if (plc.m_value != -1) o << "val=" << std::hex << plc.m_value << std::dec << ", ";
  if (!plc.m_error.empty()) o << "errors=(" << plc.m_error << ")";
  return o;
}

/** Internal: the state of a MsWks4Text
 *
 * \note in order to diffenciate a note definition with
 * its main text position, we define a flag to indicate that we parse the MN0
 * ole or the Footnote ole ( see parseMain, setParse)
 */
struct State {
  //! constructor
  State()
    : m_paragraph()
    , m_defFont(3,12)
    , m_fontNames()
    , m_fontList()
    , m_paragraphList()
    , m_pgdList()
    , m_ftntList()
    , m_ftntMap()
    , m_eobjMap()
    , m_plcList()
    , m_knownPLC()
    , m_main(false)
  {
  }

  //! returns true if we parse the main block
  bool parseMain() const
  {
    return m_main;
  }
  //! sets \a main to true if we parse the main block
  void setParse(bool main)
  {
    m_main = main;
  }

  //! the actual paragraph
  Paragraph m_paragraph;

  //! the default font
  MWAWFont m_defFont;

  //! the list of fonts names
  std::vector<FontName> m_fontNames;

  //! a list of all font properties
  std::vector<Font> m_fontList;
  //! a list of all paragraph properties
  std::vector<Paragraph> m_paragraphList;
  //! a list of all page breaks
  std::vector<long> m_pgdList;

  //! list of footnotes
  std::vector<Ftnt> m_ftntList;
  //! mapping text offset to footnote
  std::map<long, Ftnt> m_ftntMap;

  //! mapping text offset to object
  std::map<long, Object> m_eobjMap;

  //! list of all PLCs
  std::vector<DataPLC> m_plcList;

  //! the known plc
  MsWks4PLCInternal::KnownPLC m_knownPLC;
protected:
  //! true if we parse the main block
  bool m_main;
};

}

//////////////////////////////////////////////////////////////////////////////
//
//   MAIN CODE
//
//////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// constructor/destructor
////////////////////////////////////////////////////////////
MsWks4Text::MsWks4Text(MsWksDocument &document)
  : m_mainParser(&document.getMainParser())
  , m_parserState()
  , m_document(document)
  , m_textPositions()
  , m_state()
  , m_FODsList()
  , m_FDPCs()
  , m_FDPPs()
{
  m_parserState=m_mainParser->getParserState();
  m_state.reset(new MsWks4TextInternal::State);
}

MsWks4Text::~MsWks4Text()
{
}

////////////////////////////////////////////////////////////
// number of page
////////////////////////////////////////////////////////////
int MsWks4Text::numPages() const
{
  int res = 1;
  for (auto const &fod : m_FODsList) {
    if (fod.m_type != DataFOD::ATTR_PARAG) continue;

    int id = fod.m_id;
    if (id < 0) continue;
    if (m_state->m_paragraphList[size_t(id)].m_pageBreak) res++;
  }

  if (res < int(m_state->m_pgdList.size())-1)
    res = int(m_state->m_pgdList.size())-1;

  return res;
}

////////////////////////////////////////////////////////////
//
// find all the text structures
//
////////////////////////////////////////////////////////////
bool MsWks4Text::readStructures(MWAWInputStreamPtr input, bool mainOle)
{
  m_state->setParse(mainOle);
  // reset data
  m_textPositions = MWAWEntry();
  m_FODsList.resize(0);

  m_state->m_fontNames.resize(0);
  m_state->m_fontList.resize(0);
  m_state->m_paragraphList.resize(0);
  m_state->m_plcList.resize(0);
  m_state->m_ftntList.resize(0);
  m_state->m_ftntMap.clear();
  m_state->m_pgdList.resize(0);

  auto &entryMap = m_document.getEntryMap();

  /* the text limit */
  auto pos = entryMap.find("TEXT");
  if (entryMap.end() == pos) {
    MWAW_DEBUG_MSG(("MsWks4Text::readStructures: error: no TEXT in header index table\n"));
    throw libmwaw::ParseException();
  }
  m_textPositions = pos->second;

  /* read fonts table */
  pos = entryMap.lower_bound("FONT");
  if (entryMap.end() == pos) {
    MWAW_DEBUG_MSG(("MsWks4Text::readStructures: error: no FONT in header index table\n"));
    throw libmwaw::ParseException();
  }
  readFontNames(input, pos->second);

  //
  // find the FDDP and FDPC positions
  //
  for (int st = 0; st < 2; st++) {
    if (!findFDPStructures(input, st))
      findFDPStructuresByHand(input, st);
  }

  /* read character FODs (FOrmatting Descriptors) */
  std::vector<DataFOD> fdps;

  for (auto fdp : m_FDPCs)
    readFDP(input, *fdp, fdps, FDPParser(&MsWks4Text::readFont));
  m_FODsList = mergeSortedLists(fdps, m_FODsList);

  fdps.resize(0);
  for (auto fdp : m_FDPPs)
    readFDP(input, *fdp, fdps, FDPParser(&MsWks4Text::readParagraph));
  m_FODsList = mergeSortedLists(fdps, m_FODsList);

  //
  // read the plc data
  //
  pos = entryMap.lower_bound("FTNT");
  while (pos != entryMap.end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName("FTNT")) break;
    if (!entry.hasType("PLC ")) continue;

    std::vector<long> textPtrs, listValues;
    readPLC(input, entry, textPtrs, listValues,  &MsWks4Text::ftntDataParser);
  }
  pos = entryMap.lower_bound("TOKN");
  while (pos != entryMap.end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName("TOKN")) break;
    if (!entry.hasType("PLC ")) continue;

    std::vector<long> textPtrs, listValues;
    readPLC(input, entry, textPtrs, listValues, &MsWks4Text::toknDataParser);
  }
  /**
     eobj and RBIL seems linked ( and associate with a 0xc6 symbol in file)
     RBIL: can store a chart, a calendar, ...
  */
  pos = entryMap.lower_bound("EOBJ");
  while (pos != entryMap.end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName("EOBJ")) break;
    if (!entry.hasType("PLC ")) continue;

    std::vector<long> textPtrs, listValues;
    readPLC(input, entry, textPtrs, listValues, &MsWks4Text::eobjDataParser);
  }
  pos = entryMap.lower_bound("PGD ");
  while (pos != entryMap.end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName("PGD ")) break;
    if (!entry.hasType("PLC ")) continue;

    std::vector<long> listValues;
    readPLC(input, entry, m_state->m_pgdList, listValues, &MsWks4Text::pgdDataParser);
  }

  return true;
}

////////////////////////////////////////////////////////////
//
//  Read the text of a foot note.
//  FIXME: keep a trace of what is read...
//
////////////////////////////////////////////////////////////
bool MsWks4Text::readFootNote(MWAWInputStreamPtr input, int id)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (!listener) return true;
  if (id < 0 || id >= int(m_state->m_ftntList.size())) {
    if (id >= 0) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFootNote: can not find footnote: %d\n", id));
    }
    listener->insertChar(' ');
    return false;
  }
  auto const &ft = m_state->m_ftntList[size_t(id)];
  if (ft.m_begin < m_textPositions.begin() || ft.m_end > m_textPositions.end()) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFootNote: invalid zone\n"));
    listener->insertChar(' ');
    return false;
  }

  MWAWEntry entry;
  entry.setBegin(ft.m_begin);
  entry.setEnd(ft.m_end);
  entry.setType("TEXT");
  // if the last character is a newline, delete if
  input->seek(ft.m_end-1, librevenge::RVNG_SEEK_SET);
  if (input->readULong(1) == 0xd) entry.setEnd(ft.m_end-1);

  return readText(input, entry, false);
}

////////////////////////////////////////////////////////////
//
//  Read the text of the document using previously-read
//  formatting information.
//
////////////////////////////////////////////////////////////
bool MsWks4Text::readText(MWAWInputStreamPtr input,  MWAWEntry const &zone,
                          bool mainOle)
{
  MWAWListenerPtr listener=m_parserState->getMainListener();
  if (zone.begin() < m_textPositions.begin() || zone.end() > m_textPositions.end()) {
    MWAW_DEBUG_MSG(("MsWks4Text::readText: invalid zone\n"));
    if (listener) listener->insertChar(' ');
    return false;
  }

  m_state->setParse(mainOle);

  // update the property to correspond to the text
  int prevFId = -1, prevPId = -1;
  auto FODs_iter = m_FODsList.begin();
  for (; FODs_iter!= m_FODsList.end(); ++FODs_iter) {
    DataFOD const &fod = *(FODs_iter);
    if (fod.m_pos >= zone.begin()) break;

    int id = (*FODs_iter).m_id;
    if (fod.m_type == DataFOD::ATTR_TEXT) prevFId = id;
    else if (fod.m_type == DataFOD::ATTR_PARAG) prevPId = id;
  }

  MWAWFont actFont(prevFId != -1 ?
                   m_state->m_fontList[size_t(prevFId)].m_font :
                   m_state->m_defFont);
  if (actFont.id()<=0)
    actFont.setId(3);
  if (listener) listener->setFont(actFont);
  if (prevPId != -1) setProperty(m_state->m_paragraphList[size_t(prevPId)]);
  else setProperty(MsWks4TextInternal::Paragraph());

  bool first = true;
  auto fType = MsWks4TextInternal::Font::None;
  bool pageBreak = false;
  int page=1;
  libmwaw::DebugFile &ascFile = m_document.ascii();
  for (; FODs_iter!= m_FODsList.end(); ++FODs_iter) {
    DataFOD const &fod = *(FODs_iter);
    uint32_t actPos = uint32_t(first ? zone.begin() : fod.m_pos), lastPos;
    if (long(actPos) >= zone.end()) break;
    bool isObject = false;
    first = false;

    libmwaw::DebugStream f;
    f << "TEXT";

    if (++FODs_iter!= m_FODsList.end()) lastPos = uint32_t((*FODs_iter).m_pos);
    else lastPos = uint32_t(zone.end());
    --FODs_iter;
    auto len = int(lastPos-actPos);

    if (fod.m_type == DataFOD::ATTR_TEXT) {
#if DEBUG_FP
      f << "[";
      if (fod.m_id >= 0  && fod.m_id<static_cast<int>(m_state->m_fontList.size())) {
        f << "C"<<fod.m_id << ":";
        f << m_state->m_fontList[size_t(fod.m_id)].m_font.getDebugString(m_parserState->m_fontConverter);
        f << m_state->m_fontList[size_t(fod.m_id)];
      }
      else if (fod.m_id>=0) {
        MWAW_DEBUG_MSG(("MsWks4Text::readText:find a bad font id=%d\n", fod.m_id));
        f << "###C" << fod.m_id;
      }
      else f << "C_";
      f << "]";
#endif
      if (fod.m_id >= 0 && fod.m_id<static_cast<int>(m_state->m_fontList.size())) {
        fType = m_state->m_fontList[size_t(fod.m_id)].m_fieldType;
        actFont=m_state->m_fontList[size_t(fod.m_id)].m_font;
      }
      else actFont=m_state->m_defFont;
      if (listener) listener->setFont(actFont);
    }
    else if (fod.m_type == DataFOD::ATTR_PARAG) {
#if DEBUG_PP
      f << "[";
      if (fod.m_id >= 0 && fod.m_id<static_cast<int>(m_state->m_paragraphList.size()))
        f << "P"<<fod.m_id << ":" << m_state->m_paragraphList[size_t(fod.m_id)];
      else if (fod.m_id>=0) {
        MWAW_DEBUG_MSG(("MsWks4Text::readText:find a bad paragraph id=%d\n", fod.m_id));
        f << "###P" << fod.m_id;
      }
      else f << "P_";
      f << "]";
#endif
      if (fod.m_id >= 0 && fod.m_id<static_cast<int>(m_state->m_paragraphList.size())) {
        setProperty(m_state->m_paragraphList[size_t(fod.m_id)]);
        if (m_state->m_paragraphList[size_t(fod.m_id)].m_pageBreak) pageBreak = true;
      }
      else setProperty(MsWks4TextInternal::Paragraph());
    }
    else {
#if DEBUG_PLC_POS
      f << "[PLC" << fod.m_id << ":";
      if (fod.m_id >= 0 && fod.m_id<static_cast<int>(m_state->m_plcList.size())) f << m_state->m_plcList[size_t(fod.m_id)];
      else if (fod.m_id>=0) {
        MWAW_DEBUG_MSG(("MsWks4Text::readText:find a bad plc id=%d\n", fod.m_id));
        f << "###";
      }
      f << "]";
#endif
      if (fod.m_id >= 0 && fod.m_id<static_cast<int>(m_state->m_plcList.size()) && long(actPos) != zone.begin() && long(actPos) < zone.end()-1) {
        if (m_state->m_plcList[size_t(fod.m_id)].m_type == MsWks4TextInternal::PGD)
          pageBreak = true;
        else if (m_state->m_plcList[size_t(fod.m_id)].m_type == MsWks4TextInternal::EOBJ) {
          auto eobjIt=m_state->m_eobjMap.find(long(actPos));
          if (eobjIt == m_state->m_eobjMap.end()) {
            MWAW_DEBUG_MSG(("MsWks4Text::readText: can not find object\n"));
          }
          else {
            m_document.sendRBIL(eobjIt->second.m_id, eobjIt->second.m_dim);
            isObject = true;
          }
        }
      }
    }

    /* plain text */
    input->seek(long(actPos), librevenge::RVNG_SEEK_SET);

    std::string s;
    if (fType == MsWks4TextInternal::Font::Page && listener) {
      listener->insertField(MWAWField(MWAWField::PageNumber));
      fType = MsWks4TextInternal::Font::None;
    }
    if (len) {
      if (pageBreak) {
        pageBreak = false;
        m_document.newPage(++page);
      }
    }

    for (auto i = uint32_t(len); i>0; i--) {
      auto readVal = static_cast<uint8_t>(input->readULong(1));
      s += char(readVal);
      if (isObject) {
        isObject = false;
        if (readVal != 0xc6) {
          s+='#';
          MWAW_DEBUG_MSG(("MsWks4Text::readText: warning: odd caracter for object\n"));
        }
        continue;
      }

      if (0x00 == readVal) continue;

      if (fType == MsWks4TextInternal::Font::Note && readVal == 0x5e) { // '^'
        fType = MsWks4TextInternal::Font::None;
        if (!m_state->parseMain()) continue;

        if (m_state->m_ftntMap.find(long(actPos)) == m_state->m_ftntMap.end()) {
          MWAW_DEBUG_MSG(("MsWks4Text::readText: warning: can not find footnote for entry at %x\n", actPos));
          m_document.sendFootnote(-1);
        }
        else
          m_document.sendFootnote(m_state->m_ftntMap[long(actPos)].m_id);
        continue;
      }

      switch (readVal) {
      case 0x09:
        if (!listener) break;
        listener->insertTab();
        break;

      case 0x0D: {
        if (!listener) break;
        listener->insertEOL();
        break;
      }
      default: {
        if (!listener) break;
        auto extra = static_cast<uint32_t>(listener->insertCharacter(static_cast<unsigned char>(readVal), input, input->tell()+long(i)-1));
        if (extra > i-1) {
          MWAW_DEBUG_MSG(("MsWks4Text::readText: warning: extra is too large\n"));
          input->seek(-long(extra+1-i), librevenge::RVNG_SEEK_CUR);
          i = 0;
        }
        else
          i -= extra;
      }
      }
    }
    if (len && fType == MsWks4TextInternal::Font::DTTUnk && listener)
      fType = MsWks4TextInternal::Font::None;

    f << ", '" << s << "'";
    ascFile.addPos(long(actPos));
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

//////////////////////////////////////////////////////////////////////////////
//
//   PLC gestion
//
//////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////
// read a plc
// CHECKME: how does this code differs with MWAW4Text
////////////////////////////////////////
bool MsWks4Text::readPLC(MWAWInputStreamPtr input,
                         MWAWEntry const &entry,
                         std::vector<long> &textPtrs,
                         std::vector<long> &listValues,
                         MsWks4Text::DataParser parser)
{
  if (!entry.hasType("PLC ")) {
    MWAW_DEBUG_MSG(("MsWks4Text::readPLC: warning: PLC name=%s, type=%s\n",
                    entry.name().c_str(), entry.type().c_str()));
    return false;
  }

  auto page_offset = static_cast<uint32_t>(entry.begin());
  long length = entry.length();
  long endPos = entry.end();

  input->seek(long(page_offset), librevenge::RVNG_SEEK_SET);
  auto nPLC = static_cast<uint16_t>(input->readULong(2)); /* number of PLC */
  if (4*nPLC+10 > length) {
    MWAW_DEBUG_MSG(("MsWks4Text::readPLC: error: nPLC = %i, pSize=%ld\n",
                    nPLC, length));
    return false;
  }
  auto dataSz = static_cast<int>(input->readULong(2));
  bool ok = true;

  libmwaw::DebugFile &ascFile = m_document.ascii();
  libmwaw::DebugStream f, f2;

  auto plcType = m_state->m_knownPLC.get(entry.name());
  if (plcType.m_type == MsWks4TextInternal::Unknown) {
    MWAW_DEBUG_MSG(("MsWks4Text::readPLC: unknown PLC name=%s, type=%s\n",
                    entry.name().c_str(), entry.type().c_str()));
  }

  auto nPLC_ = static_cast<long>(static_cast<unsigned long>(nPLC));
  if (4*nPLC_+10 + dataSz *nPLC_ != length || length/(4+dataSz) < nPLC_) {
    MWAW_DEBUG_MSG(("MsWks4Text::readPLC: error: odd dataSize: nPLC = %i, pSize=%ld\n",
                    nPLC, length));
    if (length/(4+dataSz) < nPLC_)
      dataSz=0;
    ok = false;
  }

  f << "N=" << nPLC << ", SZ=" << dataSz << ", unk=" << input->readLong(2) << ")";

  // read text pointer
  std::vector<DataFOD> fods;
  textPtrs.resize(0);
  long lastPtr =m_textPositions.begin();
  f << ",pos = (";
  for (int i = 0; i <= nPLC; i++) {
    auto pos = long(input->readULong(4));
    switch (plcType.m_pos) {
    case MsWks4PLCInternal::PLC::P_ABS:
      if (pos == 0) pos = m_textPositions.begin();
      break;
    case MsWks4PLCInternal::PLC::P_REL:
      pos += m_textPositions.begin();
      break;
    case MsWks4PLCInternal::PLC::P_INCR: {
      long newPos = lastPtr + pos;
      pos = lastPtr;
      lastPtr = newPos;
      break;
    }
    case MsWks4PLCInternal::PLC::P_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      if (pos < m_textPositions.begin() &&
          pos+m_textPositions.begin() <= m_textPositions.end()) {
        plcType.m_pos = MsWks4PLCInternal::PLC::P_REL;
        pos += m_textPositions.begin();
      }
      else
        plcType.m_pos = MsWks4PLCInternal::PLC::P_ABS;
      break;
    }
    // this can happen for some token, ...
    if (pos == m_textPositions.end()+1) pos = m_textPositions.end();
    bool posOk = pos >= m_textPositions.begin() && pos <= m_textPositions.end();
    if (!posOk) f << "###";
    f << std::hex << pos << ",";

    DataFOD fod;
    fod.m_type = DataFOD::ATTR_PLC;
    fod.m_pos = posOk ? pos : 0;

    textPtrs.push_back(fod.m_pos);
    if (i != nPLC) fods.push_back(fod);
  }
  f << ")";
  ascFile.addPos(long(page_offset));
  ascFile.addNote(f.str().c_str());

  listValues.resize(0);
  long pos = input->tell();
  for (size_t i = 0; i < size_t(nPLC); i++) {
    MsWks4TextInternal::DataPLC plc;
    plc.m_type = plcType.m_type;
    plc.m_name = entry.name();
    bool printPLC = true, dataOk=true;

    switch (plcType.m_contentType) {
    case MsWks4PLCInternal::PLC::T_CST : {
      if (dataSz == 0) {
        printPLC = false;
        break;
      }
      if (dataSz > 4) {
        f2.str("");
        for (int j = 0; j < dataSz; j++)
          f2 << std::hex << input->readULong(1) << ",";
        plc.m_error = f2.str();
      }
      else {
        plc.m_value = long(input->readULong(dataSz));
        listValues.push_back(plc.m_value);
      }

      break;
    }
    case MsWks4PLCInternal::PLC::T_STRUCT : {
      if (dataSz == 0) {
        printPLC = false;
        break;
      }
      if (parser) {
        std::string mess;
        if (!(this->*parser)
            (input, dataSz+pos, textPtrs[i], textPtrs[i+1], static_cast<int>(i), mess)) {
          dataOk = false;
          break;
        }
        plc.m_error = mess;
      }
      else {
        plc.m_error = "###unread";
        printPLC=true;
      }
      break;
    }
    case MsWks4PLCInternal::PLC::T_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      dataOk = false;
      break;
    }

    fods[i].m_id = static_cast<int>(m_state->m_plcList.size());
    if (dataOk) fods[i].m_defPos = pos;
    m_state->m_plcList.push_back(plc);

    if (printPLC) {
      f2.str("");
      f2 << plc.m_name << "(PLC"<<i<<"):" << plc;
      ascFile.addPos(pos);
      ascFile.addNote(f2.str().c_str());
    }

    pos += dataSz;
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    if (!dataOk) plcType.m_contentType = MsWks4PLCInternal::PLC::T_UNKNOWN;
  }

  if (fods.size())
    m_FODsList = mergeSortedLists(m_FODsList, fods);

  ascFile.addPos(input->tell());
  if (input->tell() != endPos) {
    static bool first = false;
    if (!first) {
      first = true;
      MWAW_DEBUG_MSG(("MsWks4Text::readPLC: problem reading end plc"));
    }
    f.str("");
    f << "###" << entry.name() << "/PLC";
    ascFile.addNote(f.str().c_str());
  }

  entry.setParsed(true);
  return ok;
}

// default
bool MsWks4Text::defDataParser(MWAWInputStreamPtr input, long endPos,
                               long, long, int, std::string &mess)
{
  mess = "";
  libmwaw::DebugStream f;

  long actPos = input->tell();
  long length = endPos-actPos;
  int sz = length%4==0 ? 4 : length%2==0 ? 2 : 1;
  auto nbElt = int(length/sz);

  f << "[" << sz << "]{" << std::hex;
  for (int c = 0; c < nbElt; c++) f << input->readULong(sz) << ",";
  f << "}";

  mess = f.str();
  return true;
}

////////////////////////////////////////////////////////////
//
// the font name list
//
////////////////////////////////////////////////////////////
bool MsWks4Text::readFontNames(MWAWInputStreamPtr input, MWAWEntry const &entry)
{
  long debPos = entry.begin();
  long endPos = entry.end();
  entry.setParsed(true);

  input->seek(debPos, librevenge::RVNG_SEEK_SET);
  auto len = static_cast<uint32_t>(input->readULong(2));
  auto n_fonts = static_cast<uint32_t>(input->readULong(2));
  libmwaw::DebugFile &ascFile = m_document.ascii();
  libmwaw::DebugStream f;

  f << "N=" << n_fonts;
  if (int(len)+10 != entry.length())
    f << ", ###size=" << std::hex << len+10 << std::dec ;
  for (int i = 0; i < 3; i++) f << ", " << input->readLong(2); // -1/4/0
  if (debPos+10+2*long(n_fonts)>endPos) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFontNames: the number of font seems bad\n"));
    f << "###";
    ascFile.addPos(debPos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << ", defPos=[" << std::hex; // FIXME: used it
  for (int i = 0; i < int(n_fonts); i++)
    f << debPos+10+input->readLong(2) << ", ";
  f << "]" << std::dec;

  ascFile.addPos(debPos);
  ascFile.addNote(f.str().c_str());

  /* read each font in the table */
  while (input->tell() > 0 && long(input->tell()+8) < endPos
         && m_state->m_fontNames.size() < n_fonts) {
    debPos = input->tell();
    auto string_size = static_cast<uint16_t>(input->readULong(1));

    std::string s;
    for (; string_size>0; string_size--) s.append(1, char(input->readULong(1)));

    MsWks4TextInternal::FontName ft;
    ft.m_name = s;
    ft.m_id = static_cast<int>(input->readULong(2));
    ft.m_unknown = static_cast<int>(input->readULong(2));

    if (s.empty()) continue;

    // fixed the relation id<->name
    m_parserState->m_fontConverter->setCorrespondance(ft.m_id, s);
    m_state->m_fontNames.push_back(ft);

    f.str("");
    f << ft;

    ascFile.addPos(debPos);
    ascFile.addNote(f.str().c_str());
  }

  if (m_state->m_fontNames.size() != n_fonts) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFontNames: warning: expected %i fonts but only found %i\n",
                    static_cast<int>(n_fonts), static_cast<int>(m_state->m_fontNames.size())));
    return false;
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// the font:
//
////////////////////////////////////////////////////////////
void MsWks4Text::setDefault(MWAWFont &font)
{
  m_state->m_defFont = font;
}

// read
bool MsWks4Text::readFont(MWAWInputStreamPtr &input, long endPos,
                          int &id, std::string &mess)
{
  libmwaw::DebugStream f;

  MsWks4TextInternal::Font font(-1,-1);

  // 4[c] : gras, 5[c] : italic, 6[c] : souligne, 7[c] : ombre,
  // c[c] : normal ?, e[i]:font ?, f[i]: size?, 18[c] : supper, 19[c] : subs
  uint32_t textAttributeBits = 0;

  unsigned char col[3]= {0,0,0};

  while (input->tell() <= endPos-2) {
    bool ok = true;

    auto val = static_cast<int>(input->readLong(1));
    long pos = input->tell();

    switch (val) {
    case 0x3: {
      auto v = static_cast<int>(input->readLong(1));
      switch (v) {
      case 1:
        font.m_fieldType = MsWks4TextInternal::Font::Page;
        break;
      case 2:
        font.m_fieldType = MsWks4TextInternal::Font::Eobj;
        break;
      case 3: // this one is the indication to add a note (with char ^)
        font.m_fieldType = MsWks4TextInternal::Font::Note;
        break;
      default:
        f << "#3=" << v;
        break;
      }
      break;
    }
    case 0x12: { // date or title
      font.m_fieldType = MsWks4TextInternal::Font::DTT;
      auto unkn = static_cast<int>(input->readLong(1));
      if (unkn) {
        font.m_fieldType = MsWks4TextInternal::Font::DTTUnk;
        f << "#DTT=" << unkn;
      }
      break;
    }
    case 0xc: { // ???
      f << "#c="<<  input->readLong(1) << ",";
      break;
    }
    case 0x4: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##bold=" << v << ",";
      textAttributeBits |= MWAWFont::boldBit;
      break;
    }
    case 0x5: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##it=" << v << ",";
      textAttributeBits |= MWAWFont::italicBit;
      break;
    }
    case 0x6: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##under=" << v << ",";
      font.m_font.setUnderlineStyle(MWAWFont::Line::Simple);
      break;
    }
    case 0x7: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##emboss=" << v << ",";
      textAttributeBits |= MWAWFont::embossBit;
      break;
    }
    case 0x8: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##shadow=" << v << ",";
      textAttributeBits |= MWAWFont::shadowBit;
      break;
    }
    case 0x9: { // small interval between char
      font.m_font.setDeltaLetterSpacing(-1);
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##Condensed=" << v << ",";
      else f << "Cond,";
      break;
    }
    case 0xa: { // big interval between char
      font.m_font.setDeltaLetterSpacing(1);
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##Expand=" << v << ",";
      else f << "Expand,";
      break;
    }
    case 0x18: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##super=" << v << ",";
      font.m_font.set(MWAWFont::Script::super100());
      break;
    }
    case 0x19: {
      auto v = static_cast<int>(input->readLong(1));
      if (v != 1) f << "##sub =" << v << ",";
      font.m_font.set(MWAWFont::Script::sub100());
    }
    break;
    case 0xe:
    case 0xf: {
      if (pos+2 > endPos) {
        ok = false;
        break;
      }
      auto v = static_cast<int>(input->readLong(2));

      if (val == 0xe) { // fontId
        if (v < 0 || v >= int(m_state->m_fontNames.size())) {
          ok = false;
          break;
        }
        font.m_font.setId(m_state->m_fontNames[size_t(v)].m_id);
        font.m_font.setSize(12);
      }
      else {   // fontSize
        if (v <= 0 || v > 200) {
          ok = false;
          break;
        }
        font.m_font.setSize(float(v));
      }
      break;
    }
    case 0x10: { // color
      if (pos+6 > endPos) {
        ok = false;
        break;
      }

      for (auto &c : col) c = static_cast<unsigned char>(input->readULong(2)>>8);
      break;
    }
    default:
      ok = false;
      break;
    }

    if (ok) continue;

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    // try to guess the attribute size
    auto v = static_cast<int>(input->readULong(1));
    if (v == 1) ok = true;
    else if (pos+2 < endPos && v == 0 && input->readULong(1) == 1) ok = true;

    if (ok) {
      f << "#" << val << "=" << std::hex << 1 << std::dec << ",";
      continue;
    }

    input->seek(pos-1, librevenge::RVNG_SEEK_SET);
    f << "#end=" << std::hex;
    while (pos++ < endPos) f << input->readULong(1) << ",";
    break;
  }

  font.m_font.setColor(MWAWColor(col[0],col[1],col[2]));
  font.m_font.setFlags(textAttributeBits);
  font.m_error = f.str();

  id = static_cast<int>(m_state->m_fontList.size());
  m_state->m_fontList.push_back(font);

  f.str("");
  f << font.m_font.getDebugString(m_parserState->m_fontConverter);
  f << font;
  mess = f.str();

  return true;
}

////////////////////////////////////////////////////////////
//
// the paragraph properties:
//
////////////////////////////////////////////////////////////
void MsWks4Text::setProperty(MsWks4TextInternal::Paragraph const &p)
{
  if (!m_parserState->getMainListener()) return;
  m_parserState->getMainListener()->setParagraph(p);
  m_state->m_paragraph = p;
}

// read
bool MsWks4Text::readParagraph(MWAWInputStreamPtr &input, long endPos,
                               int &id, std::string &mess)
{
  MsWks4TextInternal::Paragraph parag;

  libmwaw::DebugFile &ascFile = m_document.ascii();
  libmwaw::DebugStream f;

  bool customSpacing = false;

  while (input->tell() <  endPos) {
    auto val = static_cast<int>(input->readLong(1));
    auto pos = long(input->tell());

    bool ok = true;
    switch (val) {
    case 0x1b: { // justification : 1:right 2:center, 3:full
      if (pos + 1 > endPos) {
        ok = false;
        break;
      }
      auto value = static_cast<int>(input->readLong(1));
      switch (value) {
      case 0:
        parag.m_justify = MWAWParagraph::JustificationLeft;
        break;
      case 1:
        parag.m_justify = MWAWParagraph::JustificationRight;
        break;
      case 2:
        parag.m_justify = MWAWParagraph::JustificationCenter;
        break;
      case 3:
        parag.m_justify = MWAWParagraph::JustificationFull;
        break;
      default:
        parag.m_justify = MWAWParagraph::JustificationLeft;
        f  << "#just="<<value<<",";
        break;
      }
      break;
    }
    case 0x21: { // page break ?
      if (pos + 1 > endPos) {
        ok = false;
        break;
      }
      auto value= static_cast<int>(input->readLong(1));
      if (value == 1) parag.m_pageBreak = true;
      else f << "#pgBreak="<<value<<",";
      break;
    }
    case 0x22: { // bullet
      if (pos + 1 > endPos) {
        ok = false;
        break;
      }
      auto value= static_cast<int>(input->readLong(1));
      if (value != 1) {
        f << "#bullet="<<value<<",";
        break;
      }
      MWAWListLevel level;
      level.m_type = MWAWListLevel::BULLET;
      level.m_labelWidth = 0.1;
      libmwaw::appendUnicode(0x2022, level.m_bullet);
      auto list = m_parserState->m_listManager->getNewList(std::shared_ptr<MWAWList>(), 1, level);
      if (!list) {
        f << "#bullet,";
        MWAW_DEBUG_MSG(("MsWks4Text::readParagraph: can not create bullet list\n"));
        break;
      }
      parag.m_listId = list->getId();
      parag.m_listLevelIndex = 1;
      break;
    }
    case 0x23: { // alignment -2 (up space), -3 (large?)
      if (pos + 2 > endPos) {
        ok = false;
        break;
      }
      auto value = static_cast<int>(input->readLong(2));
      if (value > 100) {
        ok = false;
        break;
      }
      if (value > 0) {
        customSpacing = true;
        parag.setInterline(value, librevenge::RVNG_POINT, MWAWParagraph::AtLeast);
      }
      else {
        switch (-value) {
        case 1: // normal
          break;
        case 2:
          parag.setInterline(1.5, librevenge::RVNG_PERCENT);
          break;
        case 3: // double
          parag.setInterline(2.0, librevenge::RVNG_PERCENT);
          break;
        default:
          f << "#spacing=" << value << ",";
          break;
        }
        break;
      }
      break;
    }
    case 0x24:
    case 0x25:
    case 0x26: {
      if (pos + 2 > endPos) {
        ok = false;
        break;
      }
      auto v = static_cast<int>(input->readLong(2));
      if (v < -300 || v > 1000) {
        ok = false;
        break;
      }

      if (val==0x26 && v > 28) // decrease a little the right margins
        parag.m_margins[2] = double(v-28)/72.;
      else
        parag.m_margins[val-0x24] = double(v)/72.;
      break;
    }
    case 0x27: {
      if (pos + 42 > endPos) {
        ok = false;
        break;
      }
      auto nbt = static_cast<int>(input->readLong(2));
      if (nbt < 0 || nbt > 20) {
        ok = false;
        break;
      }

      parag.m_tabs->resize(size_t(nbt));
      for (auto &tab : *parag.m_tabs) {
        auto value = static_cast<int>(input->readULong(2));
        int flag = (value & 0xc000) >> 14;
        auto align = MWAWTabStop::LEFT;
        switch (flag) {
        case 0:
          align = MWAWTabStop::LEFT;
          break;
        case 1:
          align = MWAWTabStop::DECIMAL;
          break;
        case 2:
          align = MWAWTabStop::RIGHT;
          break;
        case 3:
          align = MWAWTabStop::CENTER;
          break;
        default:
          ;
        }
        value &= 0x3fff;

        tab.m_alignment = align;
        tab.m_position = value/72.0;
      }
      input->seek(pos+42, librevenge::RVNG_SEEK_SET);
      break;
    }
    case 0x28: { // alignment: exact or not
      if (pos + 1 > endPos) {
        ok = false;
        break;
      }
      auto value= static_cast<int>(input->readLong(1));
      if (value != 1)
        f << "#spacingType=" << value << ",";
      else if (customSpacing)
        parag.m_spacingsInterlineType = MWAWParagraph::Fixed;
      break;
    }
    default:
      ok = false;
      break;
    }

    if (ok) continue;

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(pos, '|');
    f << "#end=(";
    while (input->tell() < endPos)
      f << std::hex << static_cast<unsigned int>(input->readLong(1)) << ",";
    f << ")";
    break;
  }
  // absolute first indent -> relative
  parag.m_margins[0] = *parag.m_margins[0]-*parag.m_margins[1];

  parag.m_extra=f.str();
  id = static_cast<int>(m_state->m_paragraphList.size());
  m_state->m_paragraphList.push_back(parag);

  f.str("");
  f << parag;
  mess = f.str();

  return true;
}

////////////////////////////////////////////////////////////
//
// the object ( data stored in RBIL ? )
//
////////////////////////////////////////////////////////////
bool MsWks4Text::eobjDataParser(MWAWInputStreamPtr input, long endPos,
                                long bot, long, int id, std::string &mess)
{
  mess = "";
  libmwaw::DebugStream f;
  long actPos = input->tell();
  long length = endPos-actPos;

  if (length != 10) {
    mess = "###";
    return true;
  }
  MsWks4TextInternal::Object obj;
  obj.m_id = id;
  obj.m_type = static_cast<int>(input->readLong(2));
  for (int i = 0; i < 2; i++) obj.m_dim[i] = static_cast<int>(input->readLong(2));
  obj.m_fileId = long(input->readULong(4));
  obj.m_error = f.str();

  m_state->m_eobjMap[bot] = obj;

  f.str("");
  f << obj;
  mess = f.str();
  return true;
}

////////////////////////////////////////////////////////////
//
// the ftnt ( footnote )
//
////////////////////////////////////////////////////////////
bool MsWks4Text::ftntDataParser(MWAWInputStreamPtr input, long endPos,
                                long bot, long eot, int id, std::string &mess)
{
  mess = "";
  libmwaw::DebugStream f;
  long actPos = input->tell();
  long length = endPos-actPos;

  if (length != 10) {
    mess = "###";
    return true;
  }
  MsWks4TextInternal::Ftnt ftnt;
  ftnt.m_type = static_cast<int>(input->readULong(2)); // always 1=footnote, endnote=?
  if (ftnt.m_type != 1) {
    MWAW_DEBUG_MSG(("MsWks4Text::ftntDataParser: unknown type=%d\n",ftnt.m_type));
  }
  ftnt.m_id = id;
  if (!m_state->parseMain()) {
    ftnt.m_begin = bot;
    ftnt.m_end = eot;
  }
  for (int i = 0; i < 4; i++) {
    auto val = static_cast<int>(input->readLong(2));
    if (val) f << "unkn" << i << "=" << val << ",";
  }
  ftnt.m_error= f.str();
  if (m_state->parseMain())
    m_state->m_ftntMap[bot] = ftnt;
  else
    m_state->m_ftntList.push_back(ftnt);

  f.str("");
  f << ftnt;
  mess = f.str();
  return true;
}

////////////////////////////////////////////////////////////
//
// the pgd (page break )
// Fixme: only parsed.....
////////////////////////////////////////////////////////////
bool MsWks4Text::pgdDataParser(MWAWInputStreamPtr input, long endPos,
                               long, long, int, std::string &mess)
{
  mess = "";
  libmwaw::DebugStream f;

  long actPos = input->tell();
  long length = endPos-actPos;

  if (length != 2) {
    mess = "###";
    return false;
  }
  f << "val=" << input->readULong(1); // 0, 1, 2, 0x12, 0x1a
  auto val = static_cast<int>(input->readLong(1));
  if (val) f << ":" << val;
  mess = f.str();
  return true;
}

////////////////////////////////////////////////////////////
//
// the token (ie. a field)
// Fixme: only parsed.....
//
////////////////////////////////////////////////////////////
bool MsWks4Text::toknDataParser(MWAWInputStreamPtr input, long endPos,
                                long bot, long, int id, std::string &mess)
{
  mess = "";
  libmwaw::DebugFile &ascFile = m_document.ascii();
  libmwaw::DebugStream f;

  long actPos = input->tell();
  long length = endPos-actPos;

  if (length < 10 || !input->checkPosition(endPos)) {
    mess = "###";
    return true;
  }
  MsWks4TextInternal::Token tok;
  auto type = static_cast<int>(input->readLong(2));
  int beginType = 0;
  switch (type) {
  case 1:
    tok.m_type = MWAWField::Date;
    beginType=1;
    break;
  case 2:
    tok.m_type = MWAWField::Time;
    beginType=1;
    break;
  case 4:
    tok.m_type = MWAWField::PageNumber;
    beginType = 0;
    break;
  // next int
  case 8:
    tok.m_type = MWAWField::Title;
    beginType = 0;
    break;
  // next int
  case 16:
    tok.m_type = MWAWField::Database;
    beginType = 2;
    break;
  default:
    MWAW_DEBUG_MSG(("MsWks4Text::toknDataParser: unknown type=%d\n",type));
    f << "###type=" << type <<",";
    break;
  }
  // the text length
  tok.m_textLength = static_cast<int>(input->readLong(2));

  switch (beginType) {
  case 1: {
    tok.m_unknown = static_cast<int>(input->readULong(2));
    auto v = static_cast<int>(input->readLong(2));
    if (v) f << std::hex << "###unkn0=" << v << "," << std::dec;
    break;
  }
  case 2: {
    long len = input->readLong(1);
    if (len>=0 && actPos+5+len <= endPos) {
      std::string str;
      for (long i = 0; i < len; i++)
        str+= char(input->readULong(1));
      f << "str=" << str <<",";
    }
    else
      input->seek(-1, librevenge::RVNG_SEEK_CUR);
    break;
  }
  default:
    break;
  }

  // sometimes, followed by the bot or by val, 0, bot
  // sometines, no
  auto debDataPos = static_cast<int>(input->readLong(2));
  if (m_textPositions.begin()+debDataPos == bot);
  else {
    MWAW_DEBUG_MSG(("MsWks4Text::toknDataParser: odd token\n"));
    f << std::hex << "###deb=" << debDataPos << "," << std::dec;
  }
  f << tok;

  mess = f.str();

  actPos = input->tell();
  if (actPos != endPos) {
    f.str("");
    f << std::dec << "TOKN(PLC" << id << "):len=" << endPos-actPos<< ",###" << tok ;
    ascFile.addPos(actPos);
    ascFile.addNote(f.str().c_str());
  }

  return true;
}

////////////////////////////////////////////////////////////
// find data
////////////////////////////////////////////////////////////
bool MsWks4Text::findFDPStructures(MWAWInputStreamPtr &input, int which)
{
  auto &zones = which ? m_FDPCs : m_FDPPs;
  zones.resize(0);

  char const *indexName = which ? "BTEC" : "BTEP";
  char const *sIndexName = which ? "FDPC" : "FDPP";

  auto pos = m_document.getEntryMap().lower_bound(indexName);
  std::vector<MWAWEntry const *> listIndexed;
  while (pos != m_document.getEntryMap().end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName(indexName)) break;
    if (!entry.hasType("PLC ")) continue;
    listIndexed.push_back(&entry);
  }

  size_t nFind = listIndexed.size();
  if (nFind==0) return false;

  // can nFind be > 1 ?
  for (size_t i = 0; i < nFind-1; i++) {
    bool ok = true;
    for (size_t j = 0; j < nFind-1-i; j++) {
      if (listIndexed[j]->id() <= listIndexed[j+1]->id()) continue;
      std::swap(listIndexed[j],listIndexed[j+1]);
      ok = false;
    }
    if (ok) break;
  }

  for (size_t i = 0; i < nFind-1; i++)
    if (listIndexed[i]->id() == listIndexed[i+1]->id()) return false;

  // create a map offset -> entry
  std::map<long, MWAWEntry const *> offsetMap;
  pos = m_document.getEntryMap().lower_bound(sIndexName);
  while (pos != m_document.getEntryMap().end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName(sIndexName)) break;
    offsetMap.insert(std::map<long, MWAWEntry const *>::value_type
                     (entry.begin(), &entry));
  }

  for (auto entry : listIndexed) {
    std::vector<long> textPtrs;
    std::vector<long> listValues;

    if (!readSimplePLC(input, *entry, textPtrs, listValues)) return false;

    size_t numV = listValues.size();
    if (textPtrs.size() != numV+1) return false;

    for (auto position : listValues) {
      if (position <= 0) return false;

      auto offsIt = offsetMap.find(position);
      if (offsIt == offsetMap.end()) return false;

      zones.push_back(offsIt->second);
    }
  }

  return true;
}

bool MsWks4Text::findFDPStructuresByHand(MWAWInputStreamPtr &/*input*/, int which)
{
  char const *indexName = which ? "FDPC" : "FDPP";
  MWAW_DEBUG_MSG(("MsWks4Text::findFDPStructuresByHand: error: need to create %s list by hand \n", indexName));

  auto &zones = which ? m_FDPCs : m_FDPPs;
  zones.resize(0);

  auto pos = m_document.getEntryMap().lower_bound(indexName);
  while (pos != m_document.getEntryMap().end()) {
    MWAWEntry const &entry = pos++->second;
    if (!entry.hasName(indexName)) break;
    if (!entry.hasType(indexName)) continue;

    zones.push_back(&entry);
  }
  return zones.size() != 0;
}

////////////////////////////////////////////////////////////
// read data
////////////////////////////////////////////////////////////
bool MsWks4Text::readFDP(MWAWInputStreamPtr &input, MWAWEntry const &entry,
                         std::vector<DataFOD> &fods,
                         MsWks4Text::FDPParser parser)
{
  if (entry.length() <= 0 || entry.begin() <= 0) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFDP warning: FDP entry unintialized"));
    return false;
  }

  entry.setParsed();
  auto page_offset = static_cast<uint32_t>(entry.begin());
  long length = entry.length();
  long endPage = entry.end();

  int const deplSize = 2;
  int const headerSize = 8;

  if (length < headerSize) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFDP: warning: FDP offset=0x%X, length=0x%lx\n",
                    page_offset, static_cast<long unsigned int>(length)));
    return false;
  }

  libmwaw::DebugFile &ascFile = m_document.ascii();
  libmwaw::DebugStream f;
  input->seek(long(page_offset), librevenge::RVNG_SEEK_SET);
  auto cfod = static_cast<uint16_t>(input->readULong(deplSize));

  f << "FDP: N="<<static_cast<int>(cfod);
  f << ", unk=" << input->readLong(2);

  if (headerSize+(4+deplSize)*cfod > length) {
    MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: cfod = %i (0x%X)\n", cfod, static_cast<unsigned int>(cfod)));
    return false;
  }

  auto firstFod = static_cast<int>(fods.size());
  long lastLimit = firstFod ? fods.back().m_pos : 0;

  long lastReadPos = 0L;

  DataFOD::Type type = DataFOD::ATTR_UNKN;
  if (entry.hasType("FDPC")) type = DataFOD::ATTR_TEXT;
  else if (entry.hasType("FDPP")) type = DataFOD::ATTR_PARAG;
  else {
    MWAW_DEBUG_MSG(("MsWks4Text::readFDP: FDP error: unknown type = '%s'\n", entry.type().c_str()));
  }

  /* Read array of fcLim of FODs.  The fcLim refers to the offset of the
     last character covered by the formatting. */
  for (int i = 0; i <= cfod; i++) {
    DataFOD fod;
    fod.m_type = type;
    fod.m_pos = long(input->readULong(4));
    if (fod.m_pos == 0) fod.m_pos=m_textPositions.begin();

    /* check that fcLim is not too large */
    if (fod.m_pos > m_textPositions.end()) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: length of 'text selection' %ld > "
                      "total text length %ld\n", fod.m_pos, m_textPositions.end()));
      return false;
    }

    /* check that pos is monotonic */
    if (lastLimit > fod.m_pos) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: character position list must "
                      "be monotonic, but found %ld, %ld\n", lastLimit, fod.m_pos));
      return false;
    }

    lastLimit = fod.m_pos;

    if (i != cfod)
      fods.push_back(fod);
    else // ignore the last text position
      lastReadPos = fod.m_pos;
  }

  std::vector<DataFOD>::iterator fods_iter;
  /* Read array of bfprop of FODs.  The bfprop is the offset where
     the FPROP is located. */
  f << ", Tpos:defP=(" << std::hex;
  for (fods_iter = fods.begin() + firstFod; fods_iter!= fods.end(); ++fods_iter) {
    auto depl = static_cast<int>(input->readULong(deplSize));
    /* check size of bfprop  */
    if ((depl < headerSize+(4+deplSize)*cfod && depl > 0) ||
        long(page_offset)+depl  > endPage) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: pos of bfprop is bad "
                      "%i (0x%X)\n", depl, static_cast<unsigned int>(depl)));
      return false;
    }

    if (depl)
      (*fods_iter).m_defPos = depl + long(page_offset);

    f << (*fods_iter).m_pos << ":";
    if (depl) f << (*fods_iter).m_defPos << ", ";
    else f << "_, ";
  }
  f << "), lstPos=" << lastReadPos << ", ";

  ascFile.addPos(long(page_offset));
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(input->tell());

  std::map<long,int> mapPtr;
  for (fods_iter = fods.begin() + firstFod; fods_iter!= fods.end(); ++fods_iter) {
    long pos = (*fods_iter).m_defPos;
    if (pos == 0) continue;

    auto it= mapPtr.find(pos);
    if (it != mapPtr.end()) {
      (*fods_iter).m_id = mapPtr[pos];
      continue;
    }

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    auto szProp = static_cast<int>(input->readULong(1));
    szProp++;
    if (szProp == 0) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: 0 == szProp at file offset 0x%lx\n", static_cast<unsigned long int>(input->tell()-1)));
      return false;
    }
    long endPos = pos+szProp;
    if (endPos > endPage) {
      MWAW_DEBUG_MSG(("MsWks4Text::readFDP: error: cch = %d, too large\n", szProp));
      return false;
    }

    ascFile.addPos(endPos);
    ascFile.addPos(pos);
    int id=0;
    std::string mess;
    if (parser &&(this->*parser)(input, endPos, id, mess)) {
      (*fods_iter).m_id = mapPtr[pos] = id;

      f.str("");
      f << entry.type()  << std::dec << id <<":" << mess;
      ascFile.addNote(f.str().c_str());
      pos = input->tell();
    }

    if (pos != endPos) {
      ascFile.addPos(pos);
      f.str("");
      f << entry.type() << "###";
    }
  }

  /* go to end of page */
  input->seek(endPage, librevenge::RVNG_SEEK_SET);

  return m_textPositions.end() > lastReadPos;
}

std::vector<MsWks4Text::DataFOD> MsWks4Text::mergeSortedLists
(std::vector<MsWks4Text::DataFOD> const &lst1,
 std::vector<MsWks4Text::DataFOD> const &lst2) const
{
  std::vector<MsWks4Text::DataFOD> res;
  // we regroup these two lists in one list
  size_t num1 = lst1.size(), i1 = 0;
  size_t num2 = lst2.size(), i2 = 0;

  while (i1 < num1 || i2 < num2) {
    DataFOD val;
    if (i2 == num2) val = lst1[i1++];
    else if (i1 == num1 || lst2[i2].m_pos < lst1[i1].m_pos)
      val = lst2[i2++];
    else val = lst1[i1++];

    if (val.m_pos < m_textPositions.begin() || val.m_pos > m_textPositions.end())
      continue;

    res.push_back(val);
  }
  return res;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
