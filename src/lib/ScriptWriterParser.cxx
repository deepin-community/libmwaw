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

#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stack>

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
#include "MWAWStringStream.hxx"
#include "MWAWSubDocument.hxx"
#include "MWAWTable.hxx"

#include "ScriptWriterParser.hxx"

/** Internal: the structures of a ScriptWriterParser */
namespace ScriptWriterParserInternal
{
////////////////////////////////////////
//! Internal: the paragraph of a ScriptWriterParser
struct Paragraph {
  //! constructor
  Paragraph()
    : m_numChar(0)
    , m_height(1)
    , m_position(0,0)
    , m_align(-3)
  {
  }
  //! return true if the paragraph is empty
  bool empty() const
  {
    return m_numChar<=0;
  }
  //! an operator<<
  friend std::ostream &operator<<(std::ostream &o, Paragraph const &para)
  {
    if (para.empty())
      return o;
    o << "n=" << para.m_numChar << ",";
    if (para.m_height!=1)
      o << "h=" << para.m_height << "li,";
    o << "pos=" << para.m_position << ",";
    switch (para.m_align) {
    case 0:
      break;
    case 1:
      o << "center,";
      break;
    case -1:
      o << "right,";
      break;
    case 5:
      o << "justify,";
      break;
    default:
      o << "###align=" << para.m_align << ",";
      MWAW_DEBUG_MSG(("ScriptWriterParserInternal::Paragraph: unknown alignment\n"));
      break;
    }
    return o;
  }
  //! the number of caracters
  int m_numChar;
  //! the height
  int m_height;
  //! the position in the text zone
  MWAWVec2i m_position;
  //! the alignment
  int m_align;
};

////////////////////////////////////////
//! Internal: the frame of a ScriptWriterParser
struct Frame {
  //! constructor
  Frame()
    : m_position()
    , m_numChar(0)
  {
  }
  //! the frame bounding box
  MWAWBox2i m_position;
  //! the number of caracters
  int m_numChar;
  //! the text and the style entries
  MWAWEntry m_entries[2];
};

////////////////////////////////////////
//! Internal: the page of a ScriptWriterParser
struct Page {
  //! constructor
  Page()
    : m_lastPage(true)
    , m_zoneToParagraphsMap()
  {
  }

  //! a flag to know if this page is the last one
  bool m_lastPage;
  //! a map id to left/right paragraph
  std::map<int, std::array<Paragraph,2> > m_zoneToParagraphsMap;
  //! two map zone, position to a font, one for each potential columns
  std::map<std::pair<int,int>, MWAWFont> m_zonePosToFontMap[2];
};

////////////////////////////////////////
//! Internal: the data of a header footer of a ScriptWriterParser
struct HFData {
  //! constructor
  HFData()
    : m_numFrames(0)
    , m_frames()

    , m_pageNumberOrigin(-1,-1)
    , m_dateOrigin(-1,-1)
    , m_hasPicture(false)
    , m_picturePosition()
    , m_picture()
  {
  }
  //! the number of frames: text zone
  int m_numFrames;
  //! the list of frames
  std::vector<Frame> m_frames;

  //! returns true if the zone is empty
  bool empty() const
  {
    return m_frames.empty() && !m_hasPicture && !hasDate() && !hasPageNumber();
  }
  //! returns true if the zone has a date
  bool hasDate() const
  {
    return m_dateOrigin[0]>=0 && m_dateOrigin[1]<1000 && m_dateOrigin[1]>=0;
  }
  //! returns true if the zone has a page number
  bool hasPageNumber() const
  {
    return m_pageNumberOrigin[0]>=0 && m_pageNumberOrigin[1]<1000 && m_pageNumberOrigin[1]>=0;
  }
  //! the page number origin, only valid if 0<dim[0]<1000
  MWAWVec2i m_pageNumberOrigin;
  //! the date field origin, only valid if 0<dim[0]<1000
  MWAWVec2i m_dateOrigin;
  //! a flag to know if there is a picture
  bool m_hasPicture;
  //! the picture position
  MWAWBox2i m_picturePosition;
  //! the picture entry
  MWAWEntry m_picture;
};

////////////////////////////////////////
//! Internal: the state of a ScriptWriterParser
struct State {
  //! constructor
  State()
    : m_actPage(0)
    , m_numPages(1)

    , m_documentType(-1)
    , m_defaultFont(22,12)
    , m_lineSpacing(12)
    , m_columnSepPos(-1)
    , m_columnOriginPos(-1)

    , m_hasTitlePage(false)
    , m_pages()
    , m_mainZoneEntry()
  {
    m_lineSpacingPercent[0]=m_lineSpacingPercent[1]=1;
    for (auto &d : m_leftMargins) d=0;
  }

  int m_actPage /** the actual page */, m_numPages /** the number of page of the final document */;

  /** the document type: 0: advanced, 1: normal, 2: dual */
  int m_documentType;
  /** the default font */
  MWAWFont m_defaultFont;
  /** the default line spacing */
  int m_lineSpacing;
  /** the default line spacing percent */
  int m_lineSpacingPercent[2];
  /** the left margin positions */
  int m_leftMargins[6];
  /** the column separator position */
  int m_columnSepPos;
  /** the second column left margin */
  int m_columnOriginPos;

  /** a flag to know if the first page is a title page */
  bool m_hasTitlePage;
  /** the pages list */
  std::vector<ScriptWriterParserInternal::Page> m_pages;
  /// the main zone entry
  MWAWEntry m_mainZoneEntry;

  /** the header/footer data*/
  HFData m_hfData[2];
  /** the left/right columns tabulations */
  std::vector<MWAWTabStop> m_tabs[2];
};

////////////////////////////////////////
//! Internal: the subdocument of a ScriptWriterParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(ScriptWriterParser &pars, MWAWInputStreamPtr const &input, HFData const &hf)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_hfData(&hf)
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
    if (&m_hfData != &sDoc->m_hfData) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the header/footer zone
  HFData const *m_hfData;

  SubDocument(SubDocument const &)=delete;
  SubDocument &operator=(SubDocument const &)=delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType /*type*/)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("ScriptWriterParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<ScriptWriterParser *>(m_parser);
  if (!parser) {
    MWAW_DEBUG_MSG(("ScriptWriterParserInternal::SubDocument::parse: no parser\n"));
    return;
  }

  long pos = m_input->tell();
  if (m_hfData)
    parser->send(*m_hfData);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
ScriptWriterParser::ScriptWriterParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWTextParser(input, rsrcParser, header)
  , m_state()
{
  setAsciiName("main-1");

  m_state.reset(new ScriptWriterParserInternal::State);

  // reduce the margin (in case, the page is not defined)
  getPageSpan().setMargins(0.1);
}

ScriptWriterParser::~ScriptWriterParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void ScriptWriterParser::parse(librevenge::RVNGTextInterface *docInterface)
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
      sendMainZone();
    }

    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetTextListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void ScriptWriterParser::createDocument(librevenge::RVNGTextInterface *documentInterface)
{
  if (!documentInterface) throw(libmwaw::ParseException());
  if (getTextListener()) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::createDocument: listener already exist\n"));
    return;
  }

  m_state->m_numPages=int(m_state->m_pages.size());
  bool const hasHF[]= {!m_state->m_hfData[0].empty(), !m_state->m_hfData[1].empty()};
  std::vector<MWAWPageSpan> pageList;
  int numPageDone=0;
  if (m_state->m_hasTitlePage && (hasHF[0] || hasHF[1])) {
    MWAWPageSpan ps(getPageSpan());
    ps.setPageSpan(1);
    pageList.push_back(ps);
    numPageDone=1;
  }

  if (m_state->m_numPages>numPageDone) {
    MWAWPageSpan ps(getPageSpan());
    ps.setPageSpan(m_state->m_numPages-numPageDone);
    for (int hf=0; hf<2; ++hf) {
      if (!hasHF[hf])
        continue;
      MWAWHeaderFooter hfDoc(hf==1 ? MWAWHeaderFooter::FOOTER : MWAWHeaderFooter::HEADER, MWAWHeaderFooter::ALL);
      hfDoc.m_subDocument.reset(new ScriptWriterParserInternal::SubDocument(*this, getInput(),m_state->m_hfData[hf]));
      ps.setHeaderFooter(hfDoc);
    }
    pageList.push_back(ps);
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
bool ScriptWriterParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !readDocument())
    return false;
  for (auto &hf : m_state->m_hfData) {
    if (hf.m_hasPicture && !readPicture(hf.m_picture))
      return false;;
    for (int col=0; col<hf.m_numFrames; ++col) {
      ScriptWriterParserInternal::Frame frame;
      if (!readFrame(frame))
        return false;
      hf.m_frames.push_back(frame);
    }
  }

  if (!readPrintInfo())
    return false;

  while (true) {
    ScriptWriterParserInternal::Page page;
    if (!readPage(page))
      return false;
    m_state->m_pages.push_back(page);
    if (!page.m_lastPage) continue;

    if (!readTextZone())
      return false;

    if (!input->isEnd()) {
      ascii().addPos(input->tell());
      ascii().addNote("Entries(Extra):###");
      MWAW_DEBUG_MSG(("ScriptWriterParser::createZones: find extra data\n"));
      return false;
    }
    if (int(m_state->m_pages.size())!=m_state->m_numPages) {
      // rare but can happens
      MWAW_DEBUG_MSG(("ScriptWriterParser::createZones: the number of pages seems bad %d!=%d\n", int(m_state->m_pages.size()), m_state->m_numPages));
    }
    return true;
  }
  return false;
}

bool ScriptWriterParser::readDocument()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  if (!input->checkPosition(0x1ea)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readDocument: the entry seems too short\n"));
    return false;
  }

  input->seek(4, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  int val;

  pos=input->tell();
  f << "Entries(Document):";
  input->seek(pos+10, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  val=int(input->readLong(2));
  if (val!=1)
    f << "f0=" << val << ",";
  f << "IDS=["; // para zone id, ???
  for (int i=0; i<2; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  m_state->m_lineSpacing=int(input->readLong(2));
  if (m_state->m_lineSpacing!=12) f << "line[spacing]=" << m_state->m_lineSpacing << ",";
  val=int(input->readLong(2));
  if (val!=9) f << "f1=" << val << ",";
  val=int(input->readLong(2)); // 1|4
  if (val!=1) f << "f2=" << val << ",";
  m_state->m_columnSepPos=int(input->readLong(2));
  f << "column[pos]=" << m_state->m_columnSepPos << ",";
  int lastVal=-1;
  f << "margin[left]=[";
  for (int i=0; i<6; ++i) { // v0 and v1: margin left of the first column
    m_state->m_leftMargins[i]=val=int(input->readLong(2));
    if (val!=lastVal)
      f << val << ",";
    else
      f << "_,";
    lastVal=val;
  }
  f << "],";
  m_state->m_columnOriginPos=int(input->readLong(2)); // or ~1000 if only one column
  f << "beg[col2]=" << m_state->m_columnOriginPos << ",";
  lastVal=-1;
  f << "margin[right]=[";
  for (int i=0; i<6; ++i) { // v0 and v1: margin right of the first column
    val=int(input->readLong(2));
    if (val!=lastVal)
      f << val << ",";
    else
      f << "_,";
    lastVal=val;
  }
  f << "],";
  f << "f3=" << input->readLong(2) << ",";
  if (!readFont(m_state->m_defaultFont))
    f << "###";
  else
    f << "font=[" << m_state->m_defaultFont.getDebugString(getFontConverter()) << "]],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(0x42, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Document-1:";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val!=1) f << "f" << i << "=" << val << ",";
  }
  for (int st=0; st<2; ++st) {
    // unsure, maybe related to the current selection
    int unkn[3], unkn2[3];
    f << "unkn" << st << "=[";
    for (auto &v : unkn) {
      v=int(input->readLong(2));
      if (v==1)
        f << "_,";
      else
        f << v << ",";
    }
    f << "],";
    for (auto &v : unkn2)
      v=int(input->readLong(2));
    if (unkn[0]!=unkn2[0] || unkn[1]!=unkn2[1] || unkn[2]!=unkn2[2])
      f << "unkn" << st << "[col2]=[" << unkn2[0] << ","  << unkn2[1] << ","  << unkn2[2] << "],";
  }
  f << "left=" << input->readLong(2) << ","; // unsure
  f << "hf[S#]=" << input->readLong(2) << ","; // unsure two header/footer dimension, use for?
  ascii().addDelimiter(input->tell(),'|');
  input->seek(2, librevenge::RVNG_SEEK_CUR); // 200
  ascii().addDelimiter(input->tell(),'|');
  for (int col=0; col<2; ++col) {
    val=int(input->readLong(2));
    if (val!=1) {
      if (val>1 && val<=3)
        m_state->m_lineSpacingPercent[col]=val;
      else
        f << "###";
      f << "line[spacing," << col << "]=" << val << "%,";
    }
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(0x68, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "Document-tabs:";
  int N[2];
  for (int col=0; col<2; ++col) {
    N[col]=int(input->readLong(2));
    f << "N" << col << "=" << N[col] << ",";
    if (N[col]<0 || N[col]>20) {
      f << "###";
      MWAW_DEBUG_MSG(("ScriptWriterParser::readDocument: the numbers of tabs seems bad\n"));
      N[col]=0;
    }
  }
  for (int col=0; col<2; ++col) {
    long actPos=input->tell();
    f << "pos" << col << "=[";
    MWAWTabStop tab;
    for (int i=0; i<N[col]; ++i) {
      val=int(input->readLong(2));
      f << val << ",";
      tab.m_position=double(val)/72;
      m_state->m_tabs[col].push_back(tab);
    }
    f << "],";
    input->seek(actPos+40, librevenge::RVNG_SEEK_SET);
  }
  val=int(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(0xbe, librevenge::RVNG_SEEK_SET);
  for (int h=0; h<2; ++h) {
    ScriptWriterParserInternal::HFData &hf=m_state->m_hfData[h];
    char const *wh[]= {"header", "footer"};
    pos=input->tell();
    f.str("");
    f << "Document-" << wh[h] << ":";
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    for (int type=0; type<2; ++type) {
      if (dim[type]<0 || dim[type]>=4000 || dim[type+2]<0) {
        input->seek(6, librevenge::RVNG_SEEK_CUR);
        continue;
      }
      if (type==0)
        hf.m_pageNumberOrigin=MWAWVec2i(dim[type],dim[type+2]);
      else
        hf.m_dateOrigin=MWAWVec2i(dim[type],dim[type+2]);
      f << (type==0 ? "pagenumber" : "date") << "=[pos=" << MWAWVec2i(dim[type],dim[type+2]) << ",";
      MWAWFont font;
      readFont(font);
      f << "font=[" << font.getDebugString(getFontConverter()) << "]],";
    }
    val=int(input->readLong(4));
    if (val) {
      hf.m_hasPicture=true;
      f << "pict[id]=" << std::hex << val << std::dec << ",";
    }

    for (int i=0; i<3; ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) dim[i]=int(input->readLong(2));
    if (dim[0]>0 && dim[1]>0)
      f << "pict[sz]=" << MWAWVec2i(dim[1],dim[0]) << ",";
    for (auto &d : dim) d=int(input->readLong(2));
    if (dim[2]>dim[0] && dim[3]>dim[1]) {
      hf.m_picturePosition=MWAWBox2i(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
      f << "pict[dim]=" << hf.m_picturePosition << ",";
    }
    ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos+46, librevenge::RVNG_SEEK_SET);
    pos=input->tell();
    f.str("");
    f << "Document-" << wh[h] << "A:";
    bool lastOk=true;
    for (int i=0; i<10; ++i) { // unsure what is exactly the limit, at least 3
      val=int(input->readLong(4));
      if (!val) {
        lastOk=false;
        continue;
      }
      if (lastOk)
        ++hf.m_numFrames;
      f << "id" << i << "=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<8; ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "g" << i << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) dim[i]=int(input->readLong(2));
    // fixme: step=0, dim[0]>0 means show header, dim[1]>0 mean show footer
    if (h==0) {
      if (dim[0]>0)
        f << "h[max,header]=" << dim[0] << ",";
      if (dim[1]>0)
        f << "h[min,footer]=" << dim[1] << ",";
    }
    else
      f << "unkn=" << MWAWVec2i(dim[0],dim[1]);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos+60, librevenge::RVNG_SEEK_SET);
  }

  input->seek(0x192, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f<<"Document-2:";
  val=int(input->readLong(2));
  if (val!=1)
    f << "ruler[unit]=" << val << ","; // 1-4
  val=int(input->readULong(2));
  if (val)
    f << "fl0=" << std::hex << val << std::dec << ",";
  f << "flags=["; // empty on fl0 and fl8 set
  for (int i=0; i<8; ++i) { // fl1=titlepage?, fl4=fl7=1 header?
    val=int(input->readLong(1));
    if (val==1) {
      if (i==1) {
        m_state->m_hasTitlePage=true;
        f << "title[page],";
      }
      else
        f << "*,";
    }
    else if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  val=int(input->readLong(2));
  if (val) f << "first[page]=" << val << ",";
  for (int i=0; i<7; ++i) { // f9: num in parastyle group
    val=int(input->readLong(2));
    int const expected[]= {-1, 0, 0, 0xbb8, 1, 1, -1};
    if (val==expected[i])
      continue;
    if (i==4) {
      m_state->m_numPages=val;
      f << "num[pages]=" << val << ",";
    }
    else
      f << "f" << i+4 << "=" << val << ",";
  }
  val=int(input->readULong(2));
  f << "fl=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(2));
  if (val!=0x2e8)
    f << "f4=" << val << ",";
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  for (int i=0; i<2; ++i) { // g0=1|2, g1=1|2
    val=int(input->readLong(2));
    if (val!=1)
      f << "g" << i << "=" << val << ",";
  }
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(0x1ea, librevenge::RVNG_SEEK_SET);
  return true;
}

bool ScriptWriterParser::readFrame(ScriptWriterParserInternal::Frame &frame)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;

  libmwaw::DebugStream f;
  f << "Entries(Frame):";
  if (len<0x6a || endPos<pos+4+0x6a || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readFrame: the zone seems too short\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  input->seek(pos+4+94, librevenge::RVNG_SEEK_SET);
  int N=int(input->readULong(2));
  if (len!=0x68+2*N) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readFrame: can not find the number of lines\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  input->seek(pos+4, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<3; ++i) { // dim0: global position, dim0=dim1, dim2: local dimension
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    MWAWBox2i box(MWAWVec2i(dim[1],dim[0]),MWAWVec2i(dim[3],dim[2]));
    if (i==0)
      frame.m_position=box;
    else if (box==frame.m_position)
      continue;
    f << "dim" << i << "=" << box << ",";
  }
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+4+58, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  f << "cPos=[";
  for (int i=0; i<2; ++i) {
    int val=int(input->readULong(2));
    if (i==1)
      frame.m_numChar=val;
    f << val << ",";
  }
  f << "],";
  f << "IDS=[";
  for (int i=0; i<2; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  for (int i=0; i<2; ++i) { // f0=0|ff00
    int val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  f << "IDS1=" << std::hex << input->readULong(4) << std::dec << ",";
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(2));
    if (val!=(i==0 ? 1 : -1))
      f << "f" << i+2 << "=" << val << ",";
  }
  f << "IDS2=" << std::hex << input->readULong(4) << std::dec << ",";
  for (int i=0; i<4; ++i) {
    int val=int(input->readLong(2));
    if (val)
      f << "f" << i+4 << "=" << val << ",";
  }
  input->seek(2, librevenge::RVNG_SEEK_CUR); // num line
  f << "cPos[line]=[";
  for (int i=0; i<=N; ++i) f << input->readULong(2) << ",";
  f << "],";
  for (int i=0; i<3; ++i) {
    int val=int(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);

  // now the text zone
  pos=input->tell();
  len=long(input->readULong(4));
  if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readFrame: can not find a header/footer's text zone\n"));
    ascii().addPos(pos);
    ascii().addNote("Entries(TextData):###");
    return false;
  }
  frame.m_entries[0].setBegin(pos+4);
  frame.m_entries[0].setLength(len);
  f.str("");
  f << "Entries(TextData):";
  std::string text;
  for (int c=0; c<len; ++c) text+=char(input->readLong(1));
  f << text;
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);

  // and the style
  pos=input->tell();
  len=long(input->readULong(4));
  if (pos+4+len<pos+4+22 || !input->checkPosition(pos+4+len)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readFrame: can not find a the style zone\n"));
    ascii().addPos(pos);
    ascii().addNote("Entries(HFStyle):###");
    return false;
  }
  frame.m_entries[1].setBegin(pos+4);
  frame.m_entries[1].setLength(len);
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);

  return true;
}

bool ScriptWriterParser::readFont(MWAWFont &font)
{
  font=MWAWFont();
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+6)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readFont: the entry seems too short\n"));
    return false;
  }
  font.setId(int(input->readULong(2)));
  font.setSize(float(input->readULong(2)));
  int flag=int(input->readULong(1));
  uint32_t flags = 0;
  if (flag&0x1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (flag&0x8) flags |= MWAWFont::embossBit;
  if (flag&0x10) flags |= MWAWFont::shadowBit;
  if (flag&0x20) font.setDeltaLetterSpacing(-1.);
  if (flag&0x40) font.setDeltaLetterSpacing(1.);
  libmwaw::DebugStream f;
  if (flag&0x80) f << "#flags=" << std::hex << (flag&0x80) << std::dec << ",";
  font.setFlags(flags);
  font.m_extra=f.str();
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  return true;
}

bool ScriptWriterParser::readCharStyle(ScriptWriterParserInternal::Page &page, int column)
{
  if (column<0 || column>=2) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readCharStyle: called with unexpected column\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  if (len<2 || endPos<pos+6 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readCharStyle: the entry seems too short\n"));
    return false;
  }
  std::map<std::pair<int,int>, MWAWFont> &zPosToFontMap=page.m_zonePosToFontMap[column];
  libmwaw::DebugStream f;
  f << "Entries(CharStyle):";
  int N=int(input->readULong(2));
  if ((len-2)/10+1<N) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readCharStyle: the number of entry seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << "CharStyle-" << i << ":";
    int zone=int(input->readULong(2));
    f << "Z" << zone << ",";
    int cPos=int(input->readULong(2));
    f << "cPos=" << cPos << ",";
    MWAWFont font;
    readFont(font);
    f << ",font=[" << font.getDebugString(getFontConverter()) << "]";
    if (zPosToFontMap.find(std::make_pair(zone,cPos))!=zPosToFontMap.end()) {
      MWAW_DEBUG_MSG(("ScriptWriterParser::readCharStyle: find dupplicated position\n"));
      f << "###";
    }
    else
      zPosToFontMap[std::make_pair(zone,cPos)]=font;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+10, librevenge::RVNG_SEEK_SET);
  }

  if (input->tell()!=endPos) {
    ascii().addPos(input->tell());
    ascii().addNote("_");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool ScriptWriterParser::readHFStyle(std::map<int,MWAWFont> &posToFontMap)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  if (len<22 || endPos<pos+22+4 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readHFStyle: the entry seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(HFStyle):";
  int N=int(input->readULong(2));
  if ((len-2)/20<N) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readHFStyle: the number of entry seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "HFStyle-" << i+1 << ":";
    int val=int(input->readLong(2));
    if (val)
      f << "f0=" << val << ",";
    int cPos=int(input->readLong(2));
    f << "cPos=" << cPos << ",";
    f << "height=["; // max, min?
    for (int j=0; j<2; ++j) f << input->readLong(2) << ",";
    f << "],";
    MWAWFont font;
    font.setId(int(input->readULong(2)));
    int flag=int(input->readULong(1));
    uint32_t flags = 0;
    if (flag&0x1) flags |= MWAWFont::boldBit;
    if (flag&0x2) flags |= MWAWFont::italicBit;
    if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
    if (flag&0x8) flags |= MWAWFont::embossBit;
    if (flag&0x10) flags |= MWAWFont::shadowBit;
    if (flag&0x20) font.setDeltaLetterSpacing(-1.);
    if (flag&0x40) font.setDeltaLetterSpacing(1.);
    if (flag&0x80) f << "#flags=" << std::hex << (flag&0x80) << std::dec << ",";
    font.setFlags(flags);
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    font.setSize(float(input->readULong(2)));
    f << "font=[" << font.getDebugString(getFontConverter()) << "]],";
    for (int j=0; j<3; ++j) {
      val=int(input->readLong(2));
      if (val) f << "f" << j << "=" << val << ",";
    }
    if (posToFontMap.find(cPos)!=posToFontMap.end()) {
      MWAW_DEBUG_MSG(("ScriptWriterParser::readHFStyle: the position %d is duplicated\n", cPos));
      f << "###";
    }
    else
      posToFontMap[cPos]=font;

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }

  if (input->tell()!=endPos) {
    ascii().addPos(input->tell());
    ascii().addNote("_");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool ScriptWriterParser::readPage(ScriptWriterParserInternal::Page &page)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  libmwaw::DebugStream f;
  f << "Entries(Page):";
  if (len<30 || endPos<pos+34 || !input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("ScriptWriterParser::readPage: the entry seems too short\n"));
    return false;
  }
  page.m_lastPage=true;
  int val=int(input->readLong(2));
  if (val!=1)
    f << "page=" << val << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(1));
    if (val!=0x20)
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==0) {
      if (val&0x100)
        f << "striked,";
      val&=0xfeff;
      if (val) f << "fl=" << std::hex << val << std::dec << ",";
    }
    else
      f << "f" << i+2 << "=" << val << ",";
  }
  f << "ID=" << std::hex << input->readLong(4) << std::dec << ",";
  val=int(input->readLong(4));
  if (val) {
    f << "next[ID]=" << std::hex << val << std::dec << ",";
    page.m_lastPage=false;
  }
  f << "IDS=[";
  for (int i=0; i<2; ++i)
    f << std::hex << input->readLong(4) << std::dec << ",";
  f << "],";
  int N=int(input->readULong(2));
  if ((len-30)/40<N) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readPage: the number of entry seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Page-Z" << i+1 << ":";

    std::array<ScriptWriterParserInternal::Paragraph,2> paras;
    for (int j=0; j<3; ++j) { // f2 related to spacing line 1,2,3
      val=int(input->readLong(2));
      int const expected[]= {0,6,1};
      if (val==expected[j])
        continue;
      if (j==2)
        f << "height=" << val << "li,";
      else
        f << "f" << j << "=" << val << ",";
    }
    for (size_t col=0; col<2; ++col)
      paras[col].m_align=int(input->readLong(2));
    for (size_t col=0; col<2; ++col)
      paras[col].m_height=int(input->readLong(2));
    for (size_t col=0; col<2; ++col)
      paras[col].m_numChar=int(input->readLong(2));
    if (paras[0].empty() && paras[1].empty()) {
      ascii().addPos(pos);
      ascii().addNote("_");
      input->seek(pos+40, librevenge::RVNG_SEEK_SET);
      continue;
    }
    for (size_t col=0; col<2; ++col)
      paras[col].m_position[0]=int(input->readLong(4));
    for (size_t col=0; col<2; ++col)
      paras[col].m_position[1]=int(input->readLong(4));
    std::string wh;
    for (int j=0; j<4; ++j) wh+=char(input->readLong(1));
    if (!wh.empty() && wh!="    ")
      f << "marker=" << wh << ",";
    for (size_t col=0; col<2; ++col) {
      if (paras[col].empty()) continue;
      f << "para" << col << "=[" << paras[col] << "],";
    }
    val=int(input->readULong(2));
    if (val&0x100) f << "striked,";
    val &= 0xfeff;
    if (val&0xff00) f << "fl=" << std::hex << val << std::dec << ",";
    page.m_zoneToParagraphsMap[i]=paras;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);
  }

  if (input->tell()!=endPos) {
    ascii().addPos(input->tell());
    ascii().addNote("_");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  for (int col=0; col<2; ++col) {
    if (!readCharStyle(page,col)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
  }

  return true;
}

bool ScriptWriterParser::readPicture(MWAWEntry &entry)
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  if (len<18 || endPos<pos+22 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readPicture: the entry seems too short\n"));
    return false;
  }

  entry.setBegin(pos+4);
  entry.setLength(len);
  ascii().addPos(pos);
  ascii().addNote("Entries(Picture):");
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool ScriptWriterParser::readTextZone()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  libmwaw::DebugStream f;
  f << "Entries(TextZone):";
  if (len<8 || endPos<pos+20 || !input->checkPosition(endPos)) {
    f << "###";
    MWAW_DEBUG_MSG(("ScriptWriterParser::readTextZone: the entry seems too short\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  for (int step=0; step<2; ++step) {
    // the first length corresponds to a free list, the last length to the last used position
    long len2=long(input->readULong(4));
    if (len2+8>len) {
      f << "###";
      MWAW_DEBUG_MSG(("ScriptWriterParser::readPage: a limit seems bad\n"));
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (len2+8!=len) {
      ascii().addPos(pos+4+8+len2);
      ascii().addNote("_");
    }
  }

  m_state->m_mainZoneEntry.setBegin(pos+4);
  m_state->m_mainZoneEntry.setLength(len);

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// read the print info
////////////////////////////////////////////////////////////
bool ScriptWriterParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos = input->tell();
  long len = long(input->readULong(4));
  long endPos=pos+4+len;
  if (len<0x78 || endPos<pos+0x7c || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::readPrintInfo: the entry seems too short\n"));
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
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return true;
  }

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

  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
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
bool ScriptWriterParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = ScriptWriterParserInternal::State();

  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork())
    return false;

  if (!input->checkPosition(0x1ea+200)) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::checkHeader: file is too short\n"));
    return false;
  }
  input->seek(0,librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "FileHeader:";
  int val=int(input->readLong(2));
  if (val<130 || val>133)
    return false;
  f << "vers=" << val << ",";
  m_state->m_documentType=int(input->readULong(2));
  if (m_state->m_documentType>=3)
    return false;
  char const *wh[]= {"advanced", "normal", "dual"};
  f << wh[m_state->m_documentType] << ",";

  if (strict) {
    input->seek(0x1ea, librevenge::RVNG_SEEK_SET);
    bool lastIsShort=false;
    while (!input->isEnd()) {
      long pos=input->tell();
      long len = long(input->readULong(4));
      long endPos=pos+4+len;

      if (len<0 || endPos<pos+4 || !input->checkPosition(endPos))
        return false;
      if (len<22 && lastIsShort)
        return false;
      lastIsShort=len<22;
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
    }
  }

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  if (header)
    header->reset(MWAWDocument::MWAW_T_SCRIPTWRITER, 1);

  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool ScriptWriterParser::sendMainZone()
{
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendMainZone: can not find the main listener\n"));
    return false;
  }

  if (m_state->m_documentType==2) {
    MWAWSection section;
    if (m_state->m_columnSepPos>0 && m_state->m_columnSepPos<72*getPageSpan().getPageWidth()) {
      section.m_columns.resize(2);
      section.m_columns[0].m_width=double(m_state->m_columnSepPos)/72;
      section.m_columns[1].m_width=getPageSpan().getPageWidth()-double(m_state->m_columnSepPos)/72;
    }
    else
      section.setColumns(2, getPageSpan().getPageWidth()/double(2), librevenge::RVNG_INCH);
    listener->openSection(section);
  }
  bool firstPage=true;
  for (auto const &page : m_state->m_pages) {
    if (!firstPage)
      listener->insertBreak(MWAWListener::PageBreak);
    sendText(page);
    firstPage=false;
  }
  if (m_state->m_documentType==2)
    listener->closeSection();
  return true;
}

bool ScriptWriterParser::send(ScriptWriterParserInternal::HFData const &hf)
{
  auto input=getInput();
  if (!input)
    return false;
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[hfData]: can not find the main listener\n"));
    return false;
  }

  /* as it seems difficult to add many frame in an header/footer, sort
     all the shape by y pos, then send them one row by row (using a
     table, if many frames share the same beginning position) */
  std::map<int,std::vector<int> > posToType;
  if (hf.hasDate())
    posToType[hf.m_dateOrigin[1]]= {-3};
  if (hf.hasPageNumber()) {
    if (posToType.find(hf.m_pageNumberOrigin[1])==posToType.end())
      posToType[hf.m_dateOrigin[1]]= {-2};
    else
      posToType[hf.m_dateOrigin[1]].push_back(-2);
  }
  if (hf.m_hasPicture) {
    if (posToType.find(hf.m_picturePosition[0][1])==posToType.end())
      posToType[hf.m_picturePosition[0][1]]= {-1};
    else
      posToType[hf.m_picturePosition[0][1]].push_back(-1);
  }
  for (size_t f=0; f<hf.m_frames.size(); ++f) {
    auto const &frame = hf.m_frames[f];
    if (posToType.find(frame.m_position[0][1])==posToType.end())
      posToType[frame.m_position[0][1]]= {int(f)};
    else
      posToType[frame.m_position[0][1]].push_back(int(f));

    posToType.insert(std::make_pair(frame.m_position[0][1],int(f)));
  }
  for (auto const &it : posToType) {
    auto const &list=it.second;
    for (size_t i=0; i<list.size();) {
      int actVal=list[i];
      size_t nextI=i+1;
      while (nextI<list.size() && ((actVal<=-2 && list[nextI]<=-2) || (actVal>=0 && list[nextI]>=0)))
        ++nextI;
      bool useTable=false;
      if (nextI!=i+1) {
        useTable=true;
        MWAWTable table(MWAWTable::TableDimBit);
        std::vector<float> dim(nextI-i, 72*float(getPageSpan().getPageWidth())/float(nextI-i));
        table.setColsSize(dim);
        listener->openTable(table);
        listener->openTableRow(-float(m_state->m_lineSpacing), librevenge::RVNG_POINT);
      }

      MWAWParagraph para;
      if (m_state->m_lineSpacing>4 && m_state->m_lineSpacing<40)
        para.setInterline(m_state->m_lineSpacing, librevenge::RVNG_POINT, MWAWParagraph::AtLeast);
      para.m_justify=MWAWParagraph::JustificationCenter;

      int col=0;
      while (i<nextI) {
        if (useTable) {
          MWAWCell cell;
          cell.setPosition(MWAWVec2i(0,col++));
          listener->openTableCell(cell);
        }
        actVal=list[i++];
        listener->setParagraph(para);
        switch (actVal) {
        case -3: {
          MWAWField date(MWAWField::Date);
          date.m_DTFormat = "%a, %b %d, %Y";
          listener->insertField(date);
          break;
        }
        case -2:
          listener->insertField(MWAWField(MWAWField::PageNumber));
          break;
        case -1: {
          if (!hf.m_picture.valid() || hf.m_picture.length()<22) {
            MWAW_DEBUG_MSG(("ScriptWriterParser::send[hf]: the picture entry seems bd\n"));
            break;
          }
          input->seek(hf.m_picture.begin(), librevenge::RVNG_SEEK_SET);
          std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(hf.m_picture.length())));
          MWAWEmbeddedObject object;
          if (pict && pict->getBinary(object) && !object.m_dataList.empty()) {
            MWAWPosition pictPos(MWAWVec2f(0,0),MWAWVec2f(hf.m_picturePosition.size()), librevenge::RVNG_POINT);
            pictPos.setRelativePosition(MWAWPosition::Char);
            listener->insertPicture(pictPos, object);
#ifdef DEBUG_WITH_FILES
            static int volatile pictName = 0;
            libmwaw::DebugStream f2;
            f2 << "PICT-" << ++pictName << ".pct";
            libmwaw::Debug::dumpFile(object.m_dataList[0], f2.str().c_str());
            ascii().skipZone(hf.m_picture.begin(), hf.m_picture.end()-1);
#endif
          }
          else {
            MWAW_DEBUG_MSG(("ScriptWriterParser::send[hf]: can not find the picture\n"));
          }
          break;
        }
        default:
          sendText(hf.m_frames[size_t(actVal)]);
          break;
        }
        if (useTable)
          listener->closeTableCell();
      }
      listener->insertEOL();
      if (useTable) {
        listener->closeTableRow();
        listener->closeTable();
      }
    }
  }
  return true;
}

bool ScriptWriterParser::sendText(ScriptWriterParserInternal::Frame const &frame)
{
  auto input=getInput();
  if (!input)
    return false;
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[frame]: can not find the main listener\n"));
    return false;
  }

  std::map<int,MWAWFont> posToFontMap;
  if (frame.m_entries[1].valid()) {
    input->seek(frame.m_entries[1].begin()-4, librevenge::RVNG_SEEK_SET);
    readHFStyle(posToFontMap);
  }

  if (!frame.m_entries[0].valid() || frame.m_entries[0].length()<frame.m_numChar) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[frame]: can not find the text zone\n"));
    return false;
  }
  listener->setFont(m_state->m_defaultFont);
  input->seek(frame.m_entries[0].begin(), librevenge::RVNG_SEEK_SET);
  for (int c=0; c<frame.m_numChar; ++c) {
    auto fIt=posToFontMap.find(c);
    if (fIt!=posToFontMap.end())
      listener->setFont(fIt->second);
    unsigned char ch=(unsigned char)(input->readLong(1));
    switch (ch) {
    case 0:
      listener->insertEOL(true);
      break;
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      listener->insertEOL();
      break;
    default:
      if (ch<0x1f)
        MWAW_DEBUG_MSG(("StudentWritingCParser::sendText[frame]: find odd char c=%d\n", int(ch)));
      else
        listener->insertCharacter(ch);
      break;
    }
  }
  return true;
}

bool ScriptWriterParser::sendText(ScriptWriterParserInternal::Page const &page)
{
  auto input=getInput();
  if (!input)
    return false;
  auto listener=getTextListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[page]: can not find the main listener\n"));
    return false;
  }
  auto const &textEntry=m_state->m_mainZoneEntry;
  if (!textEntry.valid()) {
    MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[page]: can not find the main text entry\n"));
    return false;
  }
  bool const dual=m_state->m_documentType==2;
  libmwaw::DebugStream f;
  for (size_t col=0; col<2; ++col) {
    MWAWParagraph paragraph;
    if (m_state->m_lineSpacing>4 && m_state->m_lineSpacing<40)
      paragraph.setInterline(m_state->m_lineSpacing, librevenge::RVNG_POINT, MWAWParagraph::AtLeast);
    if (m_state->m_lineSpacingPercent[col]>1 && m_state->m_lineSpacingPercent[col]<5)
      paragraph.setInterline(m_state->m_lineSpacingPercent[col], librevenge::RVNG_PERCENT);
    paragraph.m_tabs=m_state->m_tabs[col];
    paragraph.m_marginsUnit=librevenge::RVNG_POINT;
    if (col==0)
      paragraph.m_margins[1]=m_state->m_leftMargins[0];
    else
      paragraph.m_margins[1]=m_state->m_columnOriginPos-m_state->m_columnSepPos;
    for (auto const &pIdPara : page.m_zoneToParagraphsMap) {
      auto const &para=pIdPara.second[col];
      switch (para.m_align) {
      case 0:// left
      default:
        paragraph.m_justify=MWAWParagraph::JustificationLeft;
        break;
      case 1:
        paragraph.m_justify=MWAWParagraph::JustificationCenter;
        break;
      case -1:
        paragraph.m_justify=MWAWParagraph::JustificationRight;
        break;
      case -5:
        paragraph.m_justify=MWAWParagraph::JustificationFull;
        break;
      }
      listener->setParagraph(paragraph);
      int totalHeight=std::max<int>(pIdPara.second[0].m_height, pIdPara.second[1].m_height);
      if (para.empty()) {
        if (dual) {
          for (int l=para.m_height; l<totalHeight; ++l)
            listener->insertEOL();
        }
        continue;
      }
      if (para.m_numChar<0 || para.m_position[0]<0 || para.m_position[1]<para.m_position[0]+4+para.m_numChar || para.m_position[1]+8>textEntry.length()) {
        MWAW_DEBUG_MSG(("ScriptWriterParser::sendText[page]: can not find a paragraph data\n"));
        continue;
      }
      input->seek(textEntry.begin()+8+para.m_position[0], librevenge::RVNG_SEEK_SET);

      f.str("");
      f << "TextZone:";
      int pg=int(input->readULong(2));
      f << "pg=" << pg << ",";
      int id=int(input->readLong(2));
      if (id>0)
        f << "id=" << id << ",";
      else { // col2
        id *=-1;
        f << "id2=" << id << ",";
      }
      std::string text;
      bool lastIsEOL=false;
      listener->setFont(m_state->m_defaultFont);
      for (int c=0; c<para.m_numChar; ++c) {
        auto fIt=page.m_zonePosToFontMap[col].find(std::make_pair(id,c));
        if (fIt!=page.m_zonePosToFontMap[col].end())
          listener->setFont(fIt->second);
        unsigned char ch=(unsigned char)(input->readLong(1));
        if (ch)
          text+=char(ch);
        else
          text+="[#0]";
        lastIsEOL=false;
        switch (ch) {
        case 0:
          lastIsEOL=true;
          listener->insertEOL(true);
          break;
        case 0x9:
          listener->insertTab();
          break;
        case 0xd:
          lastIsEOL=true;
          listener->insertEOL();
          break;
        default:
          if (ch<0x1f)
            MWAW_DEBUG_MSG(("StudentWritingCParser::sendText: find odd char c=%d\n", int(ch)));
          else
            listener->insertCharacter(ch);
          break;
        }
      }
      if (!lastIsEOL)
        listener->insertEOL();
      f << text << ",";
      ascii().addPos(textEntry.begin()+8+para.m_position[0]);
      ascii().addNote(f.str().c_str());
      ascii().addPos(textEntry.begin()+8+para.m_position[1]);
      ascii().addNote("_");
      if (dual) {
        for (int l=para.m_height; l<totalHeight; ++l)
          listener->insertEOL();
      }
    }
    if (!dual)
      break;
    if (col==0)
      listener->insertBreak(MWAWListener::ColumnBreak);
  }

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
