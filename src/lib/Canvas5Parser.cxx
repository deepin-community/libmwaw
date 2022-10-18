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

#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <algorithm>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <utility>

#include <librevenge/librevenge.h>

#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWHeader.hxx"
#include "MWAWOLEParser.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWPosition.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWStringStream.hxx"

#include "Canvas5Graph.hxx"
#include "Canvas5Image.hxx"
#include "Canvas5Structure.hxx"
#include "Canvas5StyleManager.hxx"

#include "Canvas5Parser.hxx"

/** Internal: the structures of a Canvas5Parser */
namespace Canvas5ParserInternal
{
//! Internal: the slide data of a Canvas5Parser
struct Slide {
  Slide()
    : m_dim(0,0)
    , m_numLayers(0)
    , m_layers()
  {
  }

  //! the slide dimension
  MWAWVec2f m_dim;
  //! the number of layer
  int m_numLayers;
  //! the layer id
  std::vector<int> m_layers;
};

//! Internal: the layer of a Canvas5Parser
struct Layer {
  //! constructor
  Layer()
    : m_name()
    , m_numShapes()
    , m_shapesId()
    , m_type(-1)
  {
  }
  //! the layer name
  librevenge::RVNGString m_name;
  //! the number of shape
  int m_numShapes;
  //! the shape id
  std::vector<int> m_shapesId;
  //! the layer type (unknonw)
  int m_type;
};

////////////////////////////////////////
//! Internal: the state of a Canvas5Parser
struct State {
  //! constructor
  State()
    : m_isWindowsFile(false)
    , m_stream()

    , m_type(1)
    , m_fileFlags(0)
    , m_documentSetup(0)
    , m_facingPages(false)

    , m_numSlides(1)
    , m_slideIds()
    , m_idToSlide()

    , m_numLayers(1)
    , m_idToLayer()
    , m_layerIdInMasterSet()
    , m_numShapes(0)

    , m_idToTextLink()

    , m_metaData()
  {
  }

  //! true if this is a windows file
  bool m_isWindowsFile;
  //! the current stream
  std::shared_ptr<Canvas5Structure::Stream> m_stream;

  //! the document type 1: graphic, 2: list of pages, 3: slides
  int m_type;
  //! the file flags
  int m_fileFlags;
  //! the document setup: 0 full page, 1: two page bottom/down, 2: four page
  int m_documentSetup;
  //! true if the document uses facing page
  bool m_facingPages;

  //! the number of slides
  int m_numSlides;
  //! the slides id
  std::vector<int> m_slideIds;
  //! the slide data
  std::map<int, Slide> m_idToSlide;

  //! the number of layer
  int m_numLayers;
  //! the layer data
  std::map<int, Layer> m_idToLayer;
  //! the list of layer present in the master page
  std::set<int> m_layerIdInMasterSet;
  //! the number of shapes
  int m_numShapes;

  //! the id the text link map
  std::map<int, librevenge::RVNGString> m_idToTextLink;

  //! the meta data
  librevenge::RVNGPropertyList m_metaData;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
Canvas5Parser::Canvas5Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
  , m_graphParser()
  , m_imageParser()
  , m_styleManager()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new Canvas5ParserInternal::State);

  m_styleManager.reset(new Canvas5StyleManager(*this));
  m_imageParser.reset(new Canvas5Image(*this));
  m_graphParser.reset(new Canvas5Graph(*this));

  getPageSpan().setMargins(0.1);
}

Canvas5Parser::~Canvas5Parser()
{
}

bool Canvas5Parser::isWindowsFile() const
{
  return m_state->m_isWindowsFile;
}


librevenge::RVNGString Canvas5Parser::getTextLink(int textLinkId) const
{
  auto const &it=m_state->m_idToTextLink.find(textLinkId);
  if (it==m_state->m_idToTextLink.end()) {
    MWAW_DEBUG_MSG(("Canvas5Parser::getTextLink: can not find the a with id=%d\n", textLinkId));
    return librevenge::RVNGString();
  }
  return it->second;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void Canvas5Parser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    checkHeader(nullptr);

    auto input=decode(getInput(), version());
    if (!input)
      throw(libmwaw::ParseException());

    // create the main stream
    m_state->m_stream=std::make_shared<Canvas5Structure::Stream>(input);
    m_state->m_stream->ascii().open(asciiName());

    ok = createZones();
    if (ok)
      createDocument(docInterface);
  }
  catch (...) {
    MWAW_DEBUG_MSG(("Canvas5Parser::parse: exception catched when parsing\n"));
    ok = false;
  }

  ascii().reset();
  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void Canvas5Parser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("Canvas5Parser::createDocument: listener already exist\n"));
    return;
  }

  std::vector<MWAWPageSpan> pageList;
  MWAWPageSpan page(getPageSpan());

  bool createMasterPage=false;
  size_t numMasters=m_state->m_type==1 ? 0 : 1;
  size_t numPagesOnOnePage=1, decal=0;
  int const vers=version();
  if (m_state->m_type==2) {
    switch (m_state->m_documentSetup) {
    case 0:
      if (m_state->m_facingPages) {
        /** FIXME: it is simpler to create a big page which contains the left and right page,
            but it may be better to create each page and to only keep in each page the
            used shapes, ie. to translate back the right shape and also decompose the master
            page shapes in left/right
         */
        numMasters=2;
        numPagesOnOnePage=2;
        decal=1; // first page is the left page
        page.setFormWidth(2*page.getFormWidth());
      }
      break;
    case 1:
      if (vers>5)
        break;
      page.setFormLength(page.getFormLength()/2);
      break;
    case 2:
      if (vers>5)
        break;
      page.setFormWidth(page.getFormWidth()/2);
      page.setFormLength(page.getFormLength()/2);
      break;
    default:
      break;
    }
  }
  std::vector<std::vector<int> > listSlides;
  for (size_t p=0, n=decal; p<m_state->m_slideIds.size(); ++p) {
    int sId=m_state->m_slideIds[p];
    auto const &it=m_state->m_idToSlide.find(sId);
    if (it==m_state->m_idToSlide.end()) {
      MWAW_DEBUG_MSG(("Canvas5Parser::createDocument: can not find the slide %d\n", sId));
      continue;
    }
    auto const &slide=it->second;
    /*
       if type==1(illustration), one slide, multiple layer
       if type==2(publication), the first slide is the master page
       if type==3(slide), the first slide is the master page
     */
    MWAWPageSpan ps(page);
    ps.setPageSpan(1);
    if (p>=numMasters && createMasterPage) {
      for (auto l : slide.m_layers) {
        if (l!=1) continue;
        ps.setMasterPageName(librevenge::RVNGString("Master"));
        break;
      }
    }
    if (p==0 && p<numMasters) {
      for (auto l : slide.m_layers) {
        auto lIt=m_state->m_idToLayer.find(l);
        if (lIt!=m_state->m_idToLayer.end() && !lIt->second.m_shapesId.empty())
          m_state->m_layerIdInMasterSet.insert(l);
      }
      createMasterPage=!m_state->m_layerIdInMasterSet.empty();
    }
    if (p<numMasters) continue;
    size_t nPage=n++/numPagesOnOnePage;
    if (listSlides.size() >= nPage) {
      listSlides.resize(nPage+1);
      pageList.push_back(ps);
    }
    listSlides[nPage].push_back(sId);
  }

  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->setDocumentMetaData(m_state->m_metaData);

  listen->startDocument();

  if (createMasterPage) {
    MWAWPageSpan ps(page);
    ps.setMasterPageName(librevenge::RVNGString("Master"));
    if (!listen->openMasterPage(ps)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::createDocument: can not create the master page\n"));
    }
    else {
      for (auto &lId : m_state->m_layerIdInMasterSet) {
        auto lIt=m_state->m_idToLayer.find(lId);
        if (lIt!=m_state->m_idToLayer.end())
          send(lIt->second);
      }
      listen->closeMasterPage();
    }
  }

  bool first=true;
  for (auto const &lId : listSlides) {
    if (!first)
      listen->insertBreak(MWAWListener::PageBreak);
    first=false;
    for (auto id : lId) {
      auto const &it=m_state->m_idToSlide.find(id);
      if (it==m_state->m_idToSlide.end()) {
        MWAW_DEBUG_MSG(("Canvas5Parser::createDocument: can not find slide %d\n", id));
        continue;
      }
      send(it->second);
    }
  }
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool Canvas5Parser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (rsrcParser) {
    Canvas5Structure::Stream stream(rsrcParser->getInput(), rsrcParser->ascii());
    auto &entryMap = rsrcParser->getEntriesMap();

    for (int w=0; w<2; ++w) {
      // also some icons: ICN#, icl8, ics#, ics8
      static char const *wh[]= {"pnot" /*0*/, "PICT" /* value in pnot */};
      auto it = entryMap.lower_bound(wh[w]);
      while (it != entryMap.end() && it->first==wh[w]) {
        auto const &entry=it++->second;
        if (!entry.valid()) continue;
        switch (w) {
        case 0:
          readPnot(stream, entry);
          break;
        case 1:
        default:
          readPicture(stream, entry);
          break;
        }
      }
    }
  }

  auto stream=m_state->m_stream;
  int const vers=version();
  if (!stream || !stream->input() || !readFileHeader(stream))
    return false;
  if (vers<9) {
    if (!readMainBlock(stream) || !m_imageParser->readImages(stream))
      return false;
  }
  else if (!readMainBlock9(stream))
    return false;
  if (!readFileRSRCs(stream))
    return false;

  auto input=stream->input();
  bool ok;
  if (vers>5) {
    long pos=input->tell();
    ok=readSI200(*stream);
    if (!ok)
      input->seek(pos, librevenge::RVNG_SEEK_SET);
  }
  if (!input->isEnd()) {
    long pos=input->tell();
    ok=readFileDesc(*stream);
    if (!ok)
      input->seek(pos, librevenge::RVNG_SEEK_SET);
  }

  if (input->isEnd())
    return !m_state->m_idToSlide.empty();

  MWAW_DEBUG_MSG(("Canvas5Parser::createZones: find extra data\n"));
  int n=0;
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  ascFile.addPos(pos);
  ascFile.addNote("Entries(Extra):###");

  while (!input->isEnd()) {
    pos=input->tell();
    f.str("");
    f << "Extra-" << ++n << ":";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+256, librevenge::RVNG_SEEK_SET);
  }
  return !m_state->m_idToSlide.empty();
}

bool Canvas5Parser::readMainBlock(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;

  if (!Canvas5Structure::readPreview(*stream, (m_state->m_fileFlags&3)!=2) ||
      !readDocumentSettings(stream))
    return false;
  if (!m_graphParser->findShapeDataZones(stream))
    return false;
  if (!m_graphParser->readShapes(*stream, m_state->m_numShapes))
    return false;

  if (!readSlides(stream) || !readLayers(stream))
    return false;

  if (!m_styleManager->readInks(stream))
    return false;

  if (!m_graphParser->readMatrices(stream))
    return false;

  //
  // the styles
  //
  if (!m_styleManager->readStrokes(stream) || !m_styleManager->readPenStyles(stream) ||
      !m_styleManager->readArrows(stream) || !m_styleManager->readDashes(stream))
    return false;

  if (!m_styleManager->readParaStyles(stream) || !m_styleManager->readCharStyles(stream))
    return false;

  return readTextLinks(stream);
}

bool Canvas5Parser::readMainBlock9(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;

  auto input=stream->input();
  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;
  std::map<std::string,std::pair<int, char const *> > const nameToTagId= {
    { "PREVIEW", {0, "Preview"} }, {"SAVEGLOBS", {1, "DocSettings"}}, {"RECTOBJHANDLE", {2, nullptr} }, {"OBJECTDATA", {3, "DataShap"} },
    { "DOCPAGELIST", {4, "Slide"}}, { "DOCLAYERLIST", {5, "Layer"}}, { "INKCLUSTER", {6, "Color"} }, { "MATRIXCLUSTER", {7, "Matrix"}},
    { "FRAMECLUSTER", {8, nullptr}}, { "OBJSTYLECLUSTER", {9, nullptr} }, { "MASKCLUSTER", {10, "Mask"}}, {"PARASTYLECLUSTER", {11, "ParaStyl"}},
    { "CHARSTYLECLUSTER", {12, "CharStyle"}}, { "IMAGECLUSTER", {13, "Image"}}, {"OBJNAMECLUSTER", {14, "ObjName"}}, {"DEPCLUSTER", {15, nullptr} },
  };
  MWAWEntry shapeEntry;
  while (!input->isEnd()) {
    long pos=input->tell();
    std::string tag;
    int fTag;
    if (!getTAG9(*stream, tag, fTag) || fTag!=0) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return true;
    }
    f.str("");
    f << "Entries(" << tag << ")[TAG]:";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    bool ok=true;
    pos=input->tell();
    f.str("");
    auto wh=-1;
    std::string what(tag);
    auto const &it=nameToTagId.find(tag);
    if (it!=nameToTagId.end()) {
      wh=it->second.first;
      if (it->second.second)
        what=it->second.second;
    }
    f << "Entries(" << what << ")[data]:";
    long len=-1;
    switch (wh) {
    case 0: {
      int fFlags=int(input->readULong(1));
      ok=Canvas5Structure::readPreview(*stream, (fFlags&3)!=2);
      break;
    }
    case 1:
      ok=readDocumentSettings(stream);
      break;
    case 2:
      len=long(input->readULong(4));
      ok=pos+len>=pos && input->checkPosition(pos+4+len);
      if (!ok)
        break;
      shapeEntry.setBegin(pos);
      shapeEntry.setLength(len+4);
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
      break;
    case 3: {
      ok=m_graphParser->findShapeDataZones(stream);
      if (ok && !shapeEntry.valid()) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readMainBlock9: oops, can not find the object handle zone\n"));
        break;
      }
      if (!ok)
        break;
      long actPos=input->tell();
      input->seek(shapeEntry.begin(), librevenge::RVNG_SEEK_SET);
      m_graphParser->readShapes(*stream, m_state->m_numShapes);
      input->seek(actPos, librevenge::RVNG_SEEK_SET);
      break;
    }
    case 4:
      ok=readSlides(stream);
      break;
    case 5:
      ok=readLayers(stream);
      break;
    case 6:
      ok=m_styleManager->readInks9(stream);
      break;
    case 7:
      ok=m_graphParser->readMatrices(stream);
      break;
    case 8:
      ok=m_styleManager->readFrameStyles9(stream);
      break;
    case 9: // readStyle
    case 10: // unseen but probably fSz=18+276
      ok=readArray9(stream, what);
      break;
    case 11:
      ok=m_styleManager->readParaStyles(stream);
      break;
    case 12:
      ok=m_styleManager->readCharStyles(stream);
      break;
    case 13:
      ok=m_imageParser->readImages9(stream);
      break;
    case 14:
      ok=readArray9(stream, what, &Canvas5Parser::stringDataFunction);
      break;
    case 15: // A,B: dependency_group list
      ok=readArray9(stream, what) && readArray9(stream, what+"-A") && readArray9(stream, what+"-B");
      break;
    default:
      ok=readArray9(stream, what);
      break;
    }
    if (!ok) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      break;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    if (!checkTAG9(*stream, tag, 1)) break;
  }
  ascFile.addPos(input->tell());
  ascFile.addNote("Entries(Extra):###");
  return false;
}

bool Canvas5Parser::readFileRSRCs(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;
  auto input=stream->input();

  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: the zone is too short\n"));
    return false;
  }

  int const vers=version();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "Entries(RsrcList):";
  int N=int(input->readLong(4));
  f << "N=" << N << ",";
  if (N<0 || (input->size()-pos-4)/16<N || pos+4+N*16<pos+4 || !input->checkPosition(pos+4+N*16)) {
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  int numRsrc=N;
  for (int i=0; i<numRsrc; ++i) {
    pos=input->tell();
    f.str("");
    if (!input->checkPosition(pos+16)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not find block %d\n", i));
      f << "RsrcList-" << i << ":###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    unsigned what=unsigned(input->readULong(4));
    f << "Entries(Rsrc" << (what==0x54455854 ? "TeXT" : Canvas5Structure::getString(what)) << "),";
    int id=int(input->readLong(4));
    f << "id=" << Canvas5Structure::getString(unsigned(id)) << ",";
    f << "fl=" << std::hex << input->readULong(4) << std::dec << ","; // 2XXXXXX ?
    long len=input->readLong(4);
    long endPos=pos+16+len;
    if (endPos<pos+16 || !input->checkPosition(endPos+4)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not find block %d\n", i));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    input->pushLimit(endPos);
    int val;
    std::vector<bool> defined;
    switch (what) {
    case 0x446f496e: // DoIn
      if (len!=32) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[DoIn]: unexpected size\n"));
        f << "###";
        break;
      }
      // checkme: more probably a serie of bytes
      for (int j=0; j<4; ++j) { // f0=0|100|146-148
        val=int(input->readLong(4));
        if (val) f << "f" << j << "=" << val << ",";
      }
      f << "N=["; // 1-a, 1-3, 1-3, 1-2
      for (int j=0; j<12; ++j) f << input->readLong(4) << ",";
      f << "],";
      val=int(input->readLong(4));
      if (val==2)
        f << "docUnit=points,";
      else if (val!=1)
        f << "#docUnit=" << val << ",";
      break;
    case 0x45646974: // Edit
      if (len!=8) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[Edit]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<2; ++j) {
        val=int(input->readLong(4));
        if (val!=(j==0 ? 0 : -1)) f << "f" << j << "=" << val << ",";
      }
      break;
    case 0x4d41434f: // MACO : object from macros
      if (len==0)
        break;
      if (!m_imageParser->readMACORsrc(stream))
        f << "###";
      break;
    case 0x4d676f72: { // Mgor
      if (len!=48) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[Mgor]: unexpected size\n"));
        f << "###";
        break;
      }
      ascFile.addDelimiter(input->tell(),'|');
      input->seek(20, librevenge::RVNG_SEEK_CUR);
      ascFile.addDelimiter(input->tell(),'|');
      int dim[2];
      for (auto &d : dim) d=int(input->readLong(4));
      f << "windows[dim]=" << MWAWVec2i(dim[1], dim[0]) << ","; // ~700x1000
      val=int(input->readLong(4));
      if (val) f << "f0=" << val << ",";
      break;
    }
    case 0x516b546d: // QkTm
      if (len==0)
        break;
      if (!m_imageParser->readQkTmRsrc(*stream))
        f << "###";
      break;
    case 0x54455854: // TEXT
      switch (id) {
      case 1: {
        f << "char[style],";
        Canvas5StyleManager::CharStyle font;
        if (!m_styleManager->readCharStyle(*stream, -1, font))
          f << "###";
        break;
      }
      case 2:
        f << "para[style],";
        if (!m_styleManager->readParaStyle(stream, -1))
          f << "###";
        break;
      case 3:
        if (len!=2) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown len[3]\n"));
          f << "###";
          break;
        }
        val=int(input->readLong(2));
        if (val) f << "f0=" << val << ",";
        break;
      case 4: // 0
      case 5: // small number
        if (len!=4) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown len[%d]\n", id));
          f << "###";
          break;
        }
        val=int(input->readLong(4));
        if (val) f << "f0=" << val << ",";
        break;
      case 6:
        if (len!=48) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown len[6]\n"));
          f << "###";
          break;
        }
        for (int j=0; j<12; ++j) { // f1 big number
          val=int(input->readULong(4));
          if (val) f << "f" << j << "=" << std::hex << val << std::dec << ",";
        }
        break;
      case 7:
        if (len!=40) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown len[7]\n"));
          f << "###";
          break;
        }
        for (int j=0; j<10; ++j) { // f0=0-19
          val=int(input->readULong(4));
          if (val) f << "f" << j << "=" << std::hex << val << std::dec << ",";
        }
        break;
      case 8: {
        if (len<16) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown len[8]\n"));
          f << "###";
          break;
        }
        auto invert=input->readInverted();
        val=int(input->readULong(2));
        if (val==256)
          input->setReadInverted(!invert);
        else if (val!=1)
          f << "f0=" << val << ",";
        for (int j=0; j<2; ++j) {
          val=int(input->readULong(1));
          if (val!=1-j)
            f << "f" << j+1 << "=" << val << ",";
        }
        int N0=int(input->readLong(4));
        f << "N=" << N0 << ",";
        if ((len-16)/4188<N0 || len!=4188*N0+16) {
          input->setReadInverted(invert);
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT,8]: can not find N\n"));
          f << "###";
          break;
        }
        for (int j=0; j<4; ++j) {
          val=int(input->readULong(2));
          if (val)
            f << "f" << j+3 << "=" << val << ",";
        }

        for (int j=0; j<N0; ++j) {
          long aPos=input->tell();
          ascFile.addPos(aPos);
          ascFile.addNote("RsrcTeXT-B[8]:");
          input->seek(aPos+4188, librevenge::RVNG_SEEK_SET);
        }
        input->setReadInverted(invert);
        break;
      }
      default:
        if (id<1001 || id>1100) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT]: unknown id=%d\n", id));
          f << "###";
          break;
        }
        if (len<40) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT,%d]: the length seems bad\n", id));
          f << "###";
          break;
        }
        val=int(input->readULong(1));
        if (val!=1 && val!=2) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT,%d]: the length seems bad\n", id));
          f << "###endian=" << val << ",";;
          break;
        }
        else {
          auto invert=input->readInverted();
          input->seek(3, librevenge::RVNG_SEEK_CUR);
          input->setReadInverted(val==2);
          int n=int(input->readLong(4));
          f << "N=" << n << ",";
          if (n<0 || (len-40)/8<n || len!=40+8*n) {
            MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[TEXT,%d]: can not read the n value\n", id));
            f << "###";
            input->setReadInverted(invert);
            break;
          }
          f << "unk=[";
          for (int j=0; j<8; ++j)
            f << std::hex << input->readULong(4) << std::dec << ((j%2)==0 ? "x" : ",");
          f << "],";
          if (n) {
            f << "unkn1=[";
            for (int j=0; j<2*n; ++j)
              f << std::hex << input->readULong(4) << std::dec << ((j%2)==0 ? "x" : ",");
            f << "],";
          }
          input->setReadInverted(invert);
        }
        break;
      }
      break;
    case 0x65666665: // effe
      if (len==0) break;
      if (!readExtendedHeader(stream, 0xc, "Rsrceffe",
      [](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &) {
      auto lInput=lStream->input();
        int lVal=int(lInput->readULong(4)); // Enve
        libmwaw::DebugStream lF;
        auto &asciiFile=lStream->ascii();
        if (lVal!=1)
          lF << "f0=" << Canvas5Structure::getString(unsigned(lVal)) << ",";
        for (int j=0; j<2; ++j) { // f1=580
          lVal=int(lInput->readLong(4));
          if (lVal)
            lF << "f" << j+1 << "=" << lVal << ",";
        }
        asciiFile.addPos(item.m_pos);
        asciiFile.addNote(lF.str().c_str());
      })) {
        f << "###";
        break;
      }
      if (input->isEnd())
        break;
      if (!readIndexMap(stream, "Rsrceffe")) {
        f << "###";
        break;
      }
      if (input->isEnd())
        break;
      if (!readDefined(*stream, defined, "Rsrceffe")) {
        f << "###";
        break;
      }
      break;
    case 0x666e6474: //fndt
      if (id==2) { // unsure what to parse
        if (len<514) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not read id=2 fndt block\n"));
          f << "###";
          break;
        }
        break;
      }
      if (id==3) {
        if (len<132) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not read id=3 fndt block\n"));
          f << "###";
          break;
        }
        f << "N=[";
        for (int j=0; j<3; ++j) f << input->readLong(4) << ",";
        f << "],";
        ascFile.addPos(input->tell());
        ascFile.addNote("Rsrcfndt3-A:");
        input->seek(120, librevenge::RVNG_SEEK_CUR);

        while (!input->isEnd() && input->checkPosition(input->tell()+60)) {
          ascFile.addPos(input->tell());
          ascFile.addNote("Rsrcfndt3-B:");
          input->seek(60, librevenge::RVNG_SEEK_CUR);
        }
        break;
      }
      if (id==4 || id==5 || id==7 || id==8) { // id=4 replace id=3 in v7
        if (len<(id==4 ? 168 : id==7 ? 544 : 192)) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not read id=%d fndt block\n", id));
          f << "###";
          break;
        }
        f << "N=["; // 2|3|1|1
        for (int j=0; j<4; ++j) f << input->readLong(4) << ",";
        f << "],";

        libmwaw::DebugStream f2;
        f2 << "Rsrcfndt" << id << "-A:";
        ascFile.addPos(input->tell());
        ascFile.addNote(f2.str().c_str());
        input->seek(id==4 ? 32 : id==7 ? 48 : 56, librevenge::RVNG_SEEK_CUR);

        while (!input->isEnd() && input->checkPosition(input->tell()+60)) {
          f2.str("");
          f2 << "Rsrcfndt" << id << "-B:";
          ascFile.addPos(input->tell());
          ascFile.addNote(f2.str().c_str());
          input->seek(60, librevenge::RVNG_SEEK_CUR);
        }
        break;
      }
      MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: unexpected id=%d fndt block\n", id));
      f << "###";
      break;
    case 0x4f4c4532: // OLE2 windows (checkme, probably bad)
      if (len!=12) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[OLE2]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<6; ++j) { // checkme probably a serie of bytes
        val=int(input->readLong(2));
        int const expected[]= {0x100, 0, 0, 0, 0x200, 0};
        if (val!=expected[j])
          f << "f" << j << "=" << val << ",";
      }
      break;
    case 0x70636567: // pceg: related to link
      switch (id) {
      case 1: {
        int const headerSz=vers<9 ? 56 : 52;
        if (len<headerSz) {
          // N headerSz, ??, ...
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: unexpected size\n"));
          f << "###";
          break;
        }
        int const fieldSz=vers<9?73:81;
        N=int(input->readLong(4));
        f << "N=" << N << ",";
        if (N<0 || (len-headerSz)/fieldSz<N || len<headerSz+N*fieldSz) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: can not find the number of data\n"));
          f << "###";
        }
        val=int(input->readLong(4));
        if (val!=len)
          f << "#len=" << val << ",";
        val=int(input->readLong(4));
        if (val)
          f << "f0=" << val << ",";
        if (vers<9) {
          val=int(input->readLong(1));
          if (val!=1) f << "endian=" << val << ","; // CHECKME: we must probably check here if we need to reverse the endian
          input->seek(1, librevenge::RVNG_SEEK_CUR);
        }
        for (int j=0; j<(vers<9 ? 21 : 20); ++j) {
          val=int(input->readLong(2));
          if (val)
            f << "g" << j << "=" << val << ",";
        }

        libmwaw::DebugStream f2;
        auto fontConverter=getFontConverter();
        int defaultFont=isWindowsFile() ? fontConverter->getId("CP1252") : 3;
        for (int j=0; j<N; ++j) {
          long actPos=input->tell();
          f2.str("");
          f2 << "Rsrcpceg-Tl" << j+1 << ":";
          if (actPos+fieldSz>endPos) {
            MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: can not read a link\n"));
            f2 << "###";
            ascFile.addPos(actPos);
            ascFile.addNote(f2.str().c_str());
            break;
          }
          input->seek(actPos+fieldSz-2, librevenge::RVNG_SEEK_SET);
          ascFile.addDelimiter(input->tell(),'|');
          librevenge::RVNGString link;
          bool first=true;
          while (input->tell()<endPos) {
            char c=char(input->readULong(1));
            if (c==0) {
              if (first) {
                first=false;
                continue;
              }
              break;
            }
            first=false;
            int unicode = fontConverter->unicode(defaultFont, static_cast<unsigned char>(c));
            if (unicode>0)
              libmwaw::appendUnicode(uint32_t(unicode), link);
            else {
              MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: find unknown unicode for char=%d\n", int(c)));
            }
          }
          if (!link.empty())
            m_state->m_idToTextLink[j+1]=link;
          f2 << link.cstr() << ",";
          ascFile.addPos(actPos);
          ascFile.addNote(f2.str().c_str());
        }
        break;
      }
      case 3200: { // link to graphic shape, see link[id]
        if (len<16) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: unexpected size\n"));
          f << "###";
          break;
        }
        val=int(input->readLong(4));
        if (val!=1) f << "f0=" << val << ",";
        int nData=int(input->readLong(4));
        f << "N=" << nData << ",";
        for (int j=0; j<2; ++j) { // between 0 and nData
          val=int(input->readLong(4));
          if (val) f << "f" << j+1 << "=" << val << ",";
        }
        f << "data=[";
        for (int j=0; j<nData; ++j) {
          long actPos=input->tell();
          if (actPos+8>endPos) {
            MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: bad entry\n"));
            f << "###";
            break;
          }
          f << "[";
          f << "f0=" << input->readLong(4) << ",";
          auto dataLen=input->readLong(4);
          if (dataLen==-1 && actPos+12<=endPos)
            f << "id=" << input->readLong(4) << ",";
          else if (dataLen>=0 && actPos+8+dataLen>=actPos+8 && actPos+8+dataLen<=endPos) {
            std::string name;
            for (int k=0; k<dataLen; ++k) {
              char c=char(input->readULong(1));
              if (!c)
                break;
              name+=c;
            }
            f << name << ",";
            input->seek(actPos+8+dataLen, librevenge::RVNG_SEEK_SET);
          }
          else {
            MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: bad entry length\n"));
            f << "###";
            break;
          }
          f << "],";
        }
        f << "],";
        break;
      }
      default:
        /* find also
           id3210: 00000001000000010000000001000000000100
         */
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pceg]: reading other than id=1 data is not implemented\n"));
        f << "###";
      }
      break;
    case 0x706f626a: // pobj
    case 0x7478726c: // txrl: very rare
      if (len%4) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[pobj/txrl]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<len/4; ++j) {
        val=int(input->readLong(4));
        if (val!=(j==0 ? 1 : 0))
          f << "f" << j << "=" << val << ",";
      }
      break;
    case 0x70726e74: // prnt
      readPrinterRsrc(*stream);
      break;
    case 0x76696e66: { // vinf
      if (len==0) break;
      switch (id) {
      case 1: { // v5 or v6
        if (len<4) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[vinf]: can not find the number of view\n"));
          f << "###";
          break;
        }
        N=int(input->readULong(4));
        f << "N=" << N << ",";
        if (52*(N+1)+4<4 || N<0 || (len-4-52)/52<N || len!=52*(N+1)+4) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[vinf]: can not find the number of view\n"));
          f << "###";
          break;
        }
        ascFile.addPos(input->tell());
        ascFile.addNote("_");
        input->seek(52, librevenge::RVNG_SEEK_CUR);
        libmwaw::DebugStream f2;
        for (int v=1; v<=N; ++v) {
          long actPos=input->tell();
          f2.str("");
          f2 << "Rsrcvinf-v:";
          std::string text;
          for (int c=0; c<36; ++c) {
            char ch=char(input->readULong(1));
            if (!ch) break;
            text+=ch;
          }
          f2 << text << ",";
          input->seek(actPos+36, librevenge::RVNG_SEEK_SET);
          f2 << "val=["; // scale then translation
          for (int d=0; d<4; ++d) f2 << float(input->readLong(4))/65536.f << ",";
          f2 << "],";
          ascFile.addPos(actPos);
          ascFile.addNote(f2.str().c_str());
          input->seek(actPos+52, librevenge::RVNG_SEEK_SET);
        }
        break;
      }
      case 3: { // v7
        if (len<4) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[vinf]: can not find the number of view\n"));
          f << "###";
          break;
        }
        N=int(input->readULong(4));
        f << "N=" << N << ",";
        if (196*(N+1)+4<4 || N<0 || (len-4-196)/196<N || len!=196*(N+1)+4) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[vinf,3]: can not find the number of view\n"));
          f << "###";
          break;
        }
        ascFile.addPos(input->tell());
        ascFile.addNote("_");
        input->seek(196, librevenge::RVNG_SEEK_CUR);
        libmwaw::DebugStream f2;
        for (int v=1; v<=N; ++v) {
          long actPos=input->tell();
          f2.str("");
          f2 << "Rsrcvinf-v:";
          std::string text;
          for (int c=0; c<52; ++c) { // at least 52 characters, maybe more
            char ch=char(input->readULong(1));
            if (!ch) break;
            text+=ch;
          }
          f2 << text << ",";
          input->seek(actPos+52, librevenge::RVNG_SEEK_SET);
          ascFile.addDelimiter(input->tell(),'|');
          ascFile.addPos(actPos);
          ascFile.addNote(f2.str().c_str());
          input->seek(actPos+196, librevenge::RVNG_SEEK_SET);
        }
        break;
      }
      default:
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[vinf]: unexpected id=%d\n", id));
        f << "###";
        break;
      }
      break;
    }
    // ----- v6 ---------
    case 0x41474946: // AGIF
      if (len==0)
        break;
      m_imageParser->readAGIFRsrc(stream);
      break;
    case 0x4c617944: // LayD
      switch (id) {
      case 101:
        if (len!=8) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[LayD,101]: unexpected size\n"));
          f << "###";
          break;
        }
        for (int j=0; j<2; ++j) { // f0=1|4, f1=1|4|5
          val=int(input->readLong(4));
          if (val!=1)
            f << "f" << j << "=" << Canvas5Structure::getString(unsigned(val)) << ",";
        }
        break;
      case 1000: { // v9
        if ((len%4)!=0) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[LayD,1000]: unexpected size\n"));
          f << "###";
          break;
        }
        f << "unkn=[";
        int n=int(len/4);
        for (int j=0; j<n; ++j) {
          val=int(input->readULong(4));
          if (val>1000) // a date ?
            f << std::hex << val << std::dec << ",";
          else if (val) // small number
            f << val << ",";
          else
            f << "_,";
        }
        f << "],";
        break;
      }
      default:
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[LayD]: unexpected id=%d\n", id));
        f << "###";
        break;
      }
      break;
    case 0x4f4c6e6b: // OLnk
      readOLnkRsrc(stream);
      break;
    case 0x50414750: // PAGP
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[PAGD]: unexpected size\n"));
        f << "###";
        break;
      }
      f << "pag?=" << int(input->readLong(4)) << ",";
      break;
    case 0x584f4244: // XOBD
      readObjectDBRsrc(stream);
      break;
    // case 0x57454245: WEBE, size 10c, little endian? maybe related to a button url?
    case 0x74797065: // type
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[type]: unexpected size\n"));
        f << "###";
        break;
      }
      val=int(input->readLong(4));
      if (val!=1)
        f << "f0=" << val << ",";
      break;
    // ----- v7 ---------
    case 0x6368636b: // chck
      if (len!=16) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[chck]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<2; ++j) {
        val=int(input->readLong(2));
        if (val!=1-j)
          f << "f" << j << "=" << val << ",";
      }
      f << "unkn=[";
      for (int j=0; j<10; ++j) { // unsure: either small number or big
        val=int(input->readLong(1));
        if (val)
          f << val << ",";
        else
          f << "_";
      }
      f << "],";
      val=int(input->readLong(2));
      if (val)
        f << "f1=" << val << ",";
      break;
    case 0x48544d4c: // HTML
      if (len!=16) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[HTML]: unexpected size\n"));
        f << "###";
        break;
      }
      val=int(input->readLong(2));
      if (val!=0x100)
        f << "f0=" << val << ",";
      f << "unkn=[";
      for (int j=0; j<10; ++j) { // 0-2
        val=int(input->readLong(1));
        if (val)
          f << val << ",";
        else
          f << "_";
      }
      f << "],";
      val=int(input->readLong(4));
      if (val!=250)
        f << "f1=" << val << ",";
      break;
    case 0x6d747874: // mtxt (with id==KERN)
      if (len!=4) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[mtext]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<2; ++j) { // small number
        val=int(input->readLong(2));
        if (val)
          f << "f" << j << "=" << val << ",";
      }
      f << "],";
      break;
    // ---- v8 ----
    case 0x4453504c: // DSPL with id=2
      if (len!=8) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[DSPL]: unexpected size\n"));
        f << "###";
        break;
      }
      for (int j=0; j<2; ++j) { // f0=72
        val=int(input->readLong(4));
        if (val)
          f << "f" << j << "=" << val << ",";
      }
      f << "],";
      break;
    // ----- v9 ---------
    case 0x23524c52: // #RLR
      if (len<2) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[#RLR]: unexpected size\n"));
        f << "###";
        break;
      }
      val=int(input->readLong(2));
      if (val) // a sub type?
        f << "f0=" << val << ",";
      while (input->tell()<endPos) {
        long actPos=input->tell();
        if (actPos+22>endPos)
          break;
        f << "[";
        val=int(input->readULong(2));
        f << char(val>>8) << char(val&0xff) << ",";
        long l=long(input->readULong(4));
        if (l<0 || actPos+22+l < actPos+22 || actPos+22+l>endPos) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[#RLR]: unexpected size\n"));
          f << "###";
          break;
        }
        std::string name;
        for (int j=0; j<l; ++j) {
          char c=char(input->readLong(1));
          if (!c)
            break;
          name+=c;
        }
        f << name << ",";
        input->seek(actPos+6+l, librevenge::RVNG_SEEK_SET);
        for (int j=0; j<2; ++j) {
          if (j==1 && input->tell()+12>=endPos) {
            // the last one look like a special case
            for (int k=0; k<3; ++k) {
              val=int(input->readLong(4));
              int const expected[]= {1,17,1};
              if (val!=expected[k])
                f << "f" << k << "=" << val << ",";
            }
            break;
          }
          double value;
          bool isNan;
          if (!readDouble(*stream, value, isNan)) {
            f << "###,";
            input->seek(6+actPos+l+8*(j+1), librevenge::RVNG_SEEK_SET);
          }
          else
            f << value << ",";
        }
        f << "],";
      }
      break;
    case 0x67697321: // gis!
      switch (id) {
      case 1: {
        std::string name;
        for (int j=0; j<len; ++j) {
          char c=char(input->readLong(1));
          if (!c) break;
          name+=c;
        }
        f << name << ",";
        break;
      }
      case 0x64676973:
        if (len<0xc0) {
          MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[gis!]: unexpected length\n"));
          f << "###";
          break;
        }
        for (int j=0; j<5; ++j) {
          val=int(input->readULong(4));
          int const expected[]= {0x1e /* a000000*/, 0x17c /* c0000000*/, 0, 2, 0x18b6};
          if (val!=expected[j])
            f << "f" << j << "=" << std::hex << val << std::dec << ",";
        }
        for (int j=0; j<11; ++j) { // g0=-100,g3=40,g4=30.5,g5=47.5,g6=0|40,g9=g10=0|1
          double value=readDouble(*stream, 8);
          if (value<0 || value>0)
            f << "g" << j << "=" << value << ",";
        }
        for (int j=0; j<5; ++j) {
          val=int(input->readLong(4));
          if (val)
            f << "h" << j << "=" << val << ",";
        }
        for (int j=0; j<2; ++j) {
          double value=readDouble(*stream, 8);
          if (value<1 || value>1)
            f << "g" << j+6 << "=" << value << ",";
        }
        f << "unkn=[";
        for (int st=0; st<2; ++st) {
          f << "[";
          for (int j=0; j<5; ++j) { // f0=1, f1=2329|2384|2394, f2=0|-1, f4=3|0 : font?
            val=int(input->readLong(4));
            if (val)
              f << "f" << j << "=" << val << ",";
          }
          f << "],";
        }
        f << "],";
        f << "g8=" << readDouble(*stream, 8) << ","; // 0|792
        ascFile.addDelimiter(input->tell(),'|');
        break;
      // find also 0x67697332 with size=17c
      default:
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[gis!]: unexpected id\n"));
        f << "###";
        break;
      }
      break;
    case 0x444e4156: // DNAV
      if (len>0) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[DNAV]: unexpected size\n"));
        f << "###";
        break;
      }
      break;
    // case 0x4d524b50: MRKP with size 121 a list of string ?
    default:
      MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs[%s]: unexpected resource\n", Canvas5Structure::getString(what).c_str()));
      f << "###";
      break;
    }
    input->popLimit();
    if (input->tell()!=endPos)
      ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  long len=input->readLong(4);
  if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileRSRCs: can not find font block\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Entries(Font):###");
    return false;
  }
  if (!m_styleManager->readFonts(stream, int(len/136)))
    return false;
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);

  if (version()>=9)
    return true;

  pos=input->tell();
  ascFile.addPos(pos);
  ascFile.addNote("Entries(RsrcStrings):");

  if (!readUsed(*stream, "RsrcStrings"))
    return false;

  if (!readIndexMap(stream, "RsrcStrings", &Canvas5Parser::stringDataFunction))
    return false;

  return true;
}

bool Canvas5Parser::readSI200(Canvas5Structure::Stream &stream)
{
  auto input=stream.input();
  if (!input) return false;

  long pos=input->tell(), begPos=pos;
  if (!input->checkPosition(pos+12)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSI200: the zone is too short\n"));
    return false;
  }
  bool isWindows=isWindowsFile();
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(SumInfo):";
  std::string name;
  for (int i=0; i<8; ++i) name+=char(input->readULong(1));
  if (name!="%SI-0200") {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSI200: can not find the zone name\n"));
    return false;
  }
  long endPos=input->size();
  if (!isWindows) {
    long len=long(input->readULong(4));
    endPos=pos+8+len+12;
    if (len<0 || endPos<pos+12 || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readSI200: can not find the zone len\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
  }
  else {
    int val=int(input->readLong(4));
    if (val!=0x100)
      f << "f0=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  bool ok=true;
  MWAWOLEParser OLEParser("", getParserState()->m_fontConverter, 2);
  for (int wh=0; wh<2; ++wh) {
    int encoding;
    librevenge::RVNGPropertyList pList;
    if (!OLEParser.readSummaryInformation(input, wh==0 ? "SummaryInformation" : "DocumentSummaryInformation", encoding, wh==0 ? m_state->m_metaData : pList, ascFile, endPos)) {
      f << "###";
      ok=false;
      break;
    }
    // fixme: v7 seems to do not write the potentially unused data at the end...
  }

  pos=input->tell();
  f.str("");
  f << "SumInfo-End:";
  if (ok && pos+28<=endPos) {
    long len=long(input->readLong(4));
    if (begPos+len<pos+4 || begPos+len>endPos)
      ok=false;
    else
      input->seek(begPos+len, librevenge::RVNG_SEEK_SET);
  }
  if (!ok)
    f << "###";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  if (!isWindows) {
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  return ok;
}

bool Canvas5Parser::readFileDesc(Canvas5Structure::Stream &stream)
{
  auto input=stream.input();
  if (!input) return false;

  long pos=input->tell();
  if (!input->checkPosition(pos+0x30c)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileDesc: the zone is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(FileDesc):";
  int byteOrdering=int(input->readULong(1));
  switch (byteOrdering) {
  case 1:
    input->setReadInverted(true);
    break;
  case 2:
    input->setReadInverted(false);
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileDesc: unknown byte ordering\n"));
    return false;
  }
  input->seek(3, librevenge::RVNG_SEEK_CUR);
  unsigned what=unsigned(input->readULong(4));
  if (what!=0x434e5635)
    return false;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int z=0; z<3; ++z) { // z=0: always empty, z=1: full path, z=2: filename
    pos=input->tell();
    f.str("");
    f << "FileDesc" << z << ":";
    std::string text;
    for (int c=0; c<256; ++c) {
      char ch=char(input->readULong(1));
      if (!ch) break;
      text+=ch;
    }
    f << text << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+256, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  f.str("");
  f << "FileDesc-end:";
  what=unsigned(input->readULong(4));
  if (what!=0x434e5635)
    f << what;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool Canvas5Parser::readFileHeader(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;
  auto input=stream->input();
  int const vers=version();
  int const headerSize=vers>=9 ? 0x2d : 0x2a;
  if (!input->checkPosition(headerSize)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileHeader: the zone is too short\n"));
    return false;
  }
  input->seek(vers>=9 ? 15 : 5, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "FileHeader:";
  int val=int(input->readULong(1));
  switch (val) {
  case 0x17:
    f << "win,";
    break;
  case 0x18:
    f << "mac,";
    break;
  default:
#ifdef DEBUG
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileHeader: unknown file type\n"));
    f << "###file[type]=" << val << ",";
    break;
#else
    return false;
#endif
  }
  val=int(input->readULong(1));
  f << "byte[order]=" << val << ",";
  switch (val) {
  case 1:
    input->setReadInverted(true);
    break;
  case 2:
    input->setReadInverted(false);
    break;
  default:
#ifdef DEBUG
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileHeader: unknown byte order\n"));
    break;
#else
    return false;
#endif
  }
  val=int(input->readULong(2)); // 0 or 2
  if (val) f << "f0=" << val << ",";
  val=int(input->readULong(4));
  if (val!=0xea8da) f << "f1=" << std::hex << val << std::dec << ",";

  std::string name;
  for (int i=0; i<7; ++i)
    name+=char(input->readULong(1));
#ifdef DEBUG
  if (name!="CANVAS5" && name!="CANVAS6") {
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileHeader: not a Canvas 5-8 file\n"));
    f << "name=" << name << ",";
  }
#else
  if (name!="CANVAS5" && name!="CANVAS6")
    return false;
#endif
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "FileHeader-A:";
  m_state->m_type=int(input->readULong(1));
  switch (m_state->m_type) {
  case 1:
    f << "illustration,";
    break;
  case 2: // list of pages with header/footer
    f << "publi,";
    break;
  case 3:
    f << "slide,";
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5Parser::readFileHeader: unknown document type %d\n", m_state->m_type));
    f << "##type=" << m_state->m_type << ",";
    m_state->m_type=1;
#ifndef DEBUG
    return false;
#endif
    break;
  }
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  int N=int(input->readULong(2)); // number block of size 400000 ?
  if (N) f << "h[sz]=" << N << "*256k,";
  val=int(input->readULong(4));
  f << "vers=" << (val>>8) << ",";
  val &= 0xff;
  if (val!=0x2)  // 0|1|2, 0 if no FileDesc,Rsrcpceg,... ?
    f << "f0=" << std::hex << val << std::dec << ",";
  for (int i=0; i<6; ++i) {
    val=int(input->readULong(2));
    if (val==0)
      continue;
    f << "f" << i << "=" << val << ",";
  }
  if (vers<9) {
    m_state->m_fileFlags=int(input->readULong(1));
    if (m_state->m_fileFlags==0x22)
      f << "no[preview],";
    else if (m_state->m_fileFlags!=0x21)
      f << "fl=" << std::hex << m_state->m_fileFlags << std::dec << ",";
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+(vers>=9 ? 24 : 21), librevenge::RVNG_SEEK_SET);

  return true;
}

bool Canvas5Parser::readDocumentSettings(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input())
    return false;
  auto input=stream->input();
  auto &ascFile=stream->ascii();
  long pos=input->tell();
  long endPos=pos+54;
  int const vers=version();
  if (vers>=9)
    endPos=pos+input->readLong(4);
  if (endPos<pos+54 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDocumentSettings: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(DocSettings):";
  if (vers>=9) {
    int val=int(input->readLong(4));
    if (val!=100)
      f << "f0=" << val << ",";
  }
  f << "lengths=[";
  for (int i=0; i<5; ++i) { // 5 small number: f2=f3?, f4: not empty shape
    int val=int(input->readLong(4));
    if (i==1) m_state->m_numSlides=val;
    else if (i==3) m_state->m_numShapes=val;
    if (val)
      f << val << ",";
    else
      f << "_,";
  }
  f << "],";
  for (int i=0; i<(vers<9 ? 3 : 1); ++i) { // 3 small number
    int val=int(input->readLong(2));
    if (val==1)
      continue;
    if (i==0) {
      f << "num[layers]=" << val << ",";
      m_state->m_numLayers=val;
      continue;
    }
    else if (i==2) // checkme
      f << "cur[layer]=" << val << ",";
    else
      f << "f" << i+1 << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "DocSettings-A:";
  if (vers>=9) {
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  int val=int(input->readLong(2));
  if (val)
    f << "f0=" << val << ",";
  val=int(input->readLong(4)); // 72|100
  if (val!=0x480000)
    f << "f1=" << float(val)/65536 << ",";
  double dVal;
  bool isNan;
  if (!readDouble(*stream, dVal, isNan)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDocumentSettings: can not read a double\n"));
    f << "###";
  }
  else
    f << "grid[dim]=" << dVal << "pt,";
  input->seek(pos+14, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+28, librevenge::RVNG_SEEK_SET);

  if (!m_styleManager->readPenSize(stream))
    return false;

  pos=input->tell();
  f.str("");
  f << "DocSettings-B:";
  if (!input->checkPosition(pos+4*256+134)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDocumentSettings: the 0 zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  input->seek(pos+38, librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');

  f << "grid[dims]=[";
  for (int i=0; i<2; ++i) { // inches and points
    long actPos=input->tell();
    if (!readDouble(*stream, dVal, isNan)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readDocumentSettings: can not read a double\n"));
      f << "###";
      input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
    }
    else
      f << dVal << (i==0 ? "in" : "pt") << ",";
  }
  ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+128, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "DocSettings-B1:";
  input->seek(pos+128+22, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!m_styleManager->readFormats(stream))
    return false;

  for (int i=0; i<6; ++i) {
    pos=input->tell();
    int const len=i==1 ? 118 : i==5 ? 58 : 98;
    f.str("");
    f << "DocSettings-C" << i << ":";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+len, librevenge::RVNG_SEEK_SET);
  }

  return true;
}

bool Canvas5Parser::readLayers(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream || !stream->input()) return false;
  auto input=stream->input();
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "Entries(Layer):";
  long len=input->readLong(4);
  long endPos=pos+4+len;
  int numLayers=vers<9 ? m_state->m_numLayers : int(len/60)-1;
  if (numLayers<0 || len<60*(numLayers+1) || endPos<pos+4 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readLayers: can not find the layer's header\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  ascFile.addPos(pos+4);
  ascFile.addNote("_");
  input->seek(pos+60, librevenge::RVNG_SEEK_SET);
  auto fontConverter=getFontConverter();
  int defaultFont=isWindowsFile() ? fontConverter->getId("CP1252") : 3;
  for (int z=0; z<numLayers; ++z) {
    pos=input->tell();
    f.str("");
    f << "Layer-L" << z+1 << ":";
    Canvas5ParserInternal::Layer layer;
    for (int i=0; i<2; ++i) { // ?
      int val=int(input->readLong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
    int numShapes=int(input->readLong(4));
    f << "N=" << numShapes-1 << ",";
    layer.m_numShapes=numShapes-1;
    layer.m_type=int(input->readLong(4));
    f << "type=" << layer.m_type << ",";
    int val=int(input->readULong(4));
    if ((val&4)==0) f << "no[print],";
    if (val&8) f << "bw,";
    if (val&0x10) f << "protected,";
    val &= 0xffe3;
    if (val!=1) f << "fl=" << std::hex << val << std::dec << ",";
    val=int(input->readLong(4));
    if (val) // &8: also bw?
      f << "f0=" << val << ",";
    for (int i=0; i<36; ++i) {
      char c=char(input->readULong(1));
      if (!c)
        break;

      int unicode = fontConverter->unicode(defaultFont, static_cast<unsigned char>(c));
      if (unicode>0)
        libmwaw::appendUnicode(uint32_t(unicode), layer.m_name);
      else {
        MWAW_DEBUG_MSG(("CanvasParser::readLayers: find unknown unicode for char=%d\n", int(c)));
      }
    }
    if (!layer.m_name.empty())
      f << layer.m_name.cstr() << ",";
    m_state->m_idToLayer[z+1]=layer;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+60, librevenge::RVNG_SEEK_SET);
  }
  if (input->tell()!=endPos) { // find four bytes : junk?
    ascFile.addPos(input->tell());
    ascFile.addNote("Layer-end:");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  for (auto &it : m_state->m_idToLayer) {
    auto &layer = it.second;
    if (layer.m_type==-1) continue;
    int const nextShape=layer.m_numShapes+1;
    pos=input->tell();
    len=input->readLong(4);
    f.str("");
    f << "Layer-L" << it.first << ":";
    if (len<0 || (nextShape>1 && len<4*nextShape) || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readLayers: can not find some layer\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    if (nextShape>1) {
      input->seek(4, librevenge::RVNG_SEEK_CUR); // junk?
      f << "id=[";
      for (int s=1; s<nextShape; ++s) {
        layer.m_shapesId.push_back(int(input->readLong(4)));
        f << "S" << layer.m_shapesId.back() << ",";
      }
      f << "],";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool Canvas5Parser::readPrinterRsrc(Canvas5Structure::Stream &stream)
{
  auto input=stream.input();
  long pos=input->tell();
  if (!input->checkPosition(pos+16)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPrinterRsrc: can not find the input\n"));
    return false;
  }
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Rsrcprnt-header:";
  int val;
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(4));
    if (val!=(i==0 ? 4 : 0))
      f << "f" << i << "=" << val << ",";
  }

  long len=input->readLong(4);
  long endPos=pos+16+len;
  if (endPos<pos+16+24 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPrinterRsrc: can not find the input\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Rsrcprnt-A:";
  for (int i=0; i<5; ++i) {
    val=int(input->readLong(4));
    int const expected[]= {0x4000, 0, 3, 0, 0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  len=input->readLong(4);
  long end1Pos=pos+24+len;
  if (end1Pos>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPrinterRsrc: first block seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  int N=int(len/64);
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Rsrcprnt-B" << i << ":";
    std::string name;
    for (int j=0; j<32; ++j) {
      char ch=char(input->readLong(1));
      if (!ch)
        break;
      name+=ch;
    }
    f << name << ",";
    input->seek(pos+32, librevenge::RVNG_SEEK_SET);
    for (int j=0; j<6; ++j) { // 0
      val=int(input->readLong(2));
      if (val!=(j==5 ? -1 : 0))
        f << "f" << j << "=" << val << ",";
    }
    f << "col=[";
    for (int j=0; j<4; ++j) f << int(input->readULong(2)>>8) << ",";
    f << "],";
    std::string what; // cmyk, rgb, sepp
    for (int j=0; j<4; ++j) what+=char(input->readULong(1));
    f << what << ",";
    for (int j=0; j<4; ++j) { // g0=2d-69, g2=3c
      val=int(input->readLong(2));
      if (val!=(j==2 ? 0x3c : 0))
        f << "g" << j << "=" << val << ",";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+64, librevenge::RVNG_SEEK_SET);
  }
  input->seek(end1Pos, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  f.str("");
  f << "Rsrcprnt-C:";
  int const vers=version();
  int const zoneLen=vers==5 ? 18 : 58;
  if (pos+zoneLen>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPrinterRsrc: second block seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  for (int i=0; i<7; ++i) {
    val=int(input->readLong(2));
    if (val!=(i==6 ? 1 : 0))
      f << "f" << i << "=" << val << ",";
  }
  if (input->tell()!=pos+zoneLen-4) {
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+zoneLen-4, librevenge::RVNG_SEEK_SET);
    ascFile.addDelimiter(input->tell(),'|');
  }
  len=input->readLong(4);
  if ((len && len<0x78) || pos+len+zoneLen<pos || pos+len+zoneLen>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPrinterRsrc: printInfo block seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (len) {
    pos=input->tell();
    f.str("");
    f << "Rsrcprnt-PrintInfo:";
    if (!m_state->m_isWindowsFile) {
      libmwaw::PrinterInfo info;
      if (!info.read(input)) {
        MWAW_DEBUG_MSG(("CanvasParser::readPrinterRsrc: can not read the print info data\n"));
        f << "###";
        ascFile.addPos(pos);
        ascFile.addNote(f.str().c_str());
        return false;
      }
      f << info;
    }
    else {
      std::string name;
      for (int i=0; i<32; ++i) {
        char c=char(input->readULong(1));
        if (!c)
          break;
        name += c;
      }
      f << name << ",";
      input->seek(pos+32, librevenge::RVNG_SEEK_SET);
      ascFile.addDelimiter(input->tell(),'|');
      // TODO: read the end of this big zone
    }
    input->seek(pos+len, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  if (input->tell()<endPos) { // find sometimes 1,0 in mac file and in windows 1 len...
    ascFile.addPos(input->tell());
    ascFile.addNote("Rsrcprnt-end:#");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  return true;
}

bool Canvas5Parser::readOLnkRsrc(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readOLnkRsrc: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos;
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();

  if (!readExtendedHeader(stream, 12, "RsrcOLnk", &Canvas5Parser::defDataFunction)) // id?, Posn, id?
    return false;
  if (!readIndexMap(stream, "RsrcOLnk")) // size 6, X, Y, 1
    return false;

  std::vector<bool> defined;
  if (!readDefined(*stream, defined, "RsrcOLnk"))
    return false;

  pos=input->tell();
  f.str("");
  f << "RsrcOLnk-A:";
  int N;
  if (!readDataHeader(*stream, 12,N)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readOLnkRsrc: can not the number N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i=1; i<=N; ++i) { // id?, id?, ????
    pos=input->tell();
    f.str("");
    f << "RsrcOLnk-A" << i << ":";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }

  if (input->isEnd()) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readOLnkRsrc: can not find the last part\n"));
    return true;
  }
  // find 0004 here, unsure if this is normal, maybe there is some decalage
  pos=input->tell();
  f.str("");
  f << "RsrcOLnk-extra:#";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool Canvas5Parser::readObjectDBRsrc(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readObjectDBRsrc: can not find the stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  if (!input->checkPosition(pos+32)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readObjectDBRsrc: can not find the input\n"));
    return false;
  }
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << "RsrcXOBD-header:";
  for (int i=0; i<4; ++i) {
    int val=int(input->readLong(2));
    int const expected[]= {0, 1, 0x200, 0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  float fDim[4];
  for (auto &d : fDim) d=float(input->readULong(4))/65536.f;
  f << "box=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]), MWAWVec2f(fDim[2], fDim[3])) << ","; // checkme: probably bad
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!readExtendedHeader(stream, 4, "RsrcXOBD0", &Canvas5Parser::defDataFunction)) // small number: some flag?
    return false;
  if (!readIndexMap(stream, "RsrcXOBD0")) // size 12, [type:4, subId:2, 1|2:1 endian?, 0:1, id:4] #XAP with suid=10, TRSP with subId=1,2, XOBD with subid=1000,1001
    return false;
  std::vector<bool> defined;
  if (!readDefined(*stream, defined, "RsrcXOBD0"))
    return false;
  if (!readExtendedHeader(stream, 4, "RsrcXOBD1", &Canvas5Parser::defDataFunction)) // data to type?
    return false;
  if (!readIndexMap(stream, "RsrcXOBD1")) // size at least 40, depend probably of the first index map
    return false;
  if (!readDefined(*stream, defined, "RsrcXOBD1"))
    return false;

  if (input->isEnd())
    return true;

  MWAW_DEBUG_MSG(("Canvas5Parser::readObjectDBRsrc: find extra data\n"));
  pos=input->tell();
  f.str("");
  f << "RsrcXOBD-extra:###";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool Canvas5Parser::readTextLinks(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: bad stream\n"));
    return false;
  }
  auto input=stream->input();
  if (!input || !input->checkPosition(input->tell()+1)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: the zone is too short\n"));
    return false;
  }
  auto &ascFile=stream->ascii();
  long pos=input->tell();
  ascFile.addPos(pos);
  ascFile.addNote("Entries(TxtLink):");
  input->seek(1, librevenge::RVNG_SEEK_CUR); // 0-3 | 67 | 72 | 99
  if (!readExtendedHeader(stream, 1, "TxtLink", &Canvas5Parser::defDataFunction))
    return false;
  if (!readIndexMap(stream, "TxtLink",
  [](std::shared_ptr<Canvas5Structure::Stream> lStream, Item const &item, std::string const &) {
  auto lInput=lStream->input();
    auto &asciiFile=lStream->ascii();
    if (item.m_length<8) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: can not read the txtLink\n"));
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote("###");
      return;
    }
    libmwaw::DebugStream f;
    f << "TL" << item.m_id << ":";
    int N=int(lInput->readULong(4));
    f << "N=" << N << ",";
    if ((item.m_length-8)/4<N || 8+4*N>item.m_length) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: can not read the txtLink N\n"));
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote("###");
    }
    int val=int(lInput->readULong(4));
    if (val)
      f << "f0=" << val << ",";
    f << "id=[";
    for (int i=0; i<N; ++i)
      f << "TLb" << lInput->readULong(4) << ",";
    f << "],";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(f.str().c_str());
  }))
  return false;
  std::vector<bool> defined;
  if (!readDefined(*stream, defined, "TxtLink"))
    return false;

  if (!readExtendedHeader(stream, 1, "TxtLink-B", &Canvas5Parser::defDataFunction))
    return false;
  if (!readIndexMap(stream, "TxtLink-B",
  [](std::shared_ptr<Canvas5Structure::Stream> lStream, Item const &item, std::string const &) {
  auto lInput=lStream->input();
    long lPos=lInput->tell();
    auto &asciiFile=lStream->ascii();
    if (item.m_length<28) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: can not read the txtPlcB\n"));
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote("###");
    }
    libmwaw::DebugStream f;
    f << "TLb" << item.m_id << ":";
    f << "fl=" << std::hex << lInput->readULong(2) << std::dec << ","; // 6[01]0
    int val=int(lInput->readULong(2)); // 78|100
    if (val)
      f << "f0=" << val << ",";
    f << Canvas5Structure::getString(unsigned(lInput->readULong(4))) << ","; // TexU
    val=int(lInput->readULong(4));
    if (val)
      f << "TLc" << val << ","; // checkme
    lInput->seek(4, librevenge::RVNG_SEEK_CUR);
    int N=int(lInput->readULong(4));
    f << "N=" << N << ",";
    if (N>(item.m_length-28)/4 || N<0 || item.m_length<28+4*N) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readTextLinks: can not find the list of block\n"));
      f << "###";
      N=0;
    }
    lInput->seek(8, librevenge::RVNG_SEEK_CUR); // junk, flag?
    f << "shapes=[";
    for (int i=0; i<N; ++i)
      f << "S" << lInput->readULong(4) << ",";
    f << "],";
    if (lInput->tell()!=lPos+item.m_length)
      asciiFile.addDelimiter(lInput->tell(),'|');
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(f.str().c_str());
  }))
  return false;
  if (!readDefined(*stream, defined, "TxtLink-B"))
    return false;

  if (!readExtendedHeader(stream, 1, "TxtLink-C", &Canvas5Parser::defDataFunction))
    return false;
  // find 0000000200000001000000020238fc000238fcc00000000000014440023a58100000002d0000000a010100c50238fcc00000
  if (!readIndexMap(stream, "TxtLink-C"))
    return false;
  return readDefined(*stream, defined, "TxtLink-C");
}

bool Canvas5Parser::readSlides(std::shared_ptr<Canvas5Structure::Stream> stream)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: no stream\n"));
    return false;
  }
  auto input=stream->input();
  int const vers=version();
  int const headerSize=vers<9 ? 64 : 268;
  if (!input || !input->checkPosition(input->tell()+headerSize+4)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: the zone is too short\n"));
    return false;
  }

  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();

  f << "Entries(Slide):";
  int val=int(input->readLong(4)); // 1-3
  if (val) f << "f0=" << val << ",";
  if (vers>=9) {
    val=int(input->readLong(4)); // 0
    if (val) f << "f1=" << val << ",";
  }
  float fDim[4];
  for (auto &d : fDim) d=float(readDouble(*stream,vers<9 ? 4 : 8));
  f << "dim?=" << MWAWVec2f(fDim[0], fDim[1]) << ",";
  MWAWVec2f pageDim=MWAWVec2f(fDim[2], fDim[3]);
  f << "page[dim]=" << pageDim << ",";
  val=int(input->readULong(4));
  switch (val) {
  case 0: // full page
    break;
  case 1: // the page's height is divided by 2, ie. we print on each page TOP: page 2*N+1(reverted), BOTTOM: page 2*N
    m_state->m_documentSetup=1;
    f << "setup=top/bottom,";
    break;
  case 2: // greetings pages, page height/width is divided by 2
    m_state->m_documentSetup=2;
    f << "setup=greetings,";
    break;
  default:
    MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: find unknown setup type\n"));
    f << "###setup=" << val << ",";
    break;
  }
  val=int(input->readULong(4));
  if (val==2) {
    f << "pages[facing],";
    m_state->m_facingPages=true;
  }
  else if (val) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: find unknown facing pages\n"));
    f << "###pages[facing]=" << val << ",";
  }
  for (auto &d : fDim) d=float(readDouble(*stream, vers<9 ? 4 : 8));
  f << "margins=" << MWAWBox2f(MWAWVec2f(fDim[0], fDim[1]),MWAWVec2f(fDim[2], fDim[3])) << ",";

  // time to set the page dimension
  if (pageDim[0]>10 && pageDim[1]>10) { // I find one time 12.75x16.5
    // checkme: check the margins ordering
    if (fDim[0]>=0)
      getPageSpan().setMarginTop((fDim[0]>14 ? fDim[0]-14 : 0)/72);
    if (fDim[1]>=0)
      getPageSpan().setMarginLeft((fDim[1]>14 ? fDim[1]-14 : 0)/72);
    if (fDim[2]>=0)
      getPageSpan().setMarginBottom((fDim[2]>10 ? fDim[2]-10 : 0)/72);
    if (fDim[3]>=0)
      getPageSpan().setMarginRight((fDim[3]>10 ? fDim[3]-10 : 0)/72);
    getPageSpan().setFormLength(pageDim[1]/72);
    getPageSpan().setFormWidth(pageDim[0]/72);
  }

  for (int i=0; i<4; ++i) { // g3=0
    val=int(input->readLong(2));
    if (!val) continue;
    f << "g" << i << "=" << val << ",";
  }
  int N=int(input->readLong(4));
  if (N)
    f << "N=" << N << ",";
  f << "IDs=[";
  for (int i=0; i<2; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  if (vers>=9) ascFile.addDelimiter(input->tell(), '|');
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+headerSize, librevenge::RVNG_SEEK_SET);

  pos=input->tell();
  long len=input->readLong(4);
  if (len<4 || N<0 || len/4<N || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: can not read the Slides length\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Entries(Bad):###");
    return false;
  }
  f.str("");
  f << "Slide-id:";
  input->seek(4, librevenge::RVNG_SEEK_CUR);
  f << "id=[";
  for (int i=0; i<N; ++i) {
    m_state->m_slideIds.push_back(int(input->readLong(4)));
    f << m_state->m_slideIds.back() << ",";
  }
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);

  if (vers>=9) {
    return readArray9(stream, "Slide",
    [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
      auto lInput=lStream->input();
      long lPos=lInput->tell();
      long endPos=lPos+item.m_length;
      libmwaw::DebugStream lF;
      lF << what << "-S" << item.m_id << ":";
      auto &asciiFile=lStream->ascii();
      int lVal=int(lInput->readLong(4));
      if (lVal==0) { // dummy?
        asciiFile.addPos(lPos);
        asciiFile.addNote(lF.str().c_str());
        return;
      }
      else if (lVal!=1) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readSlide: unexpected first value\n"));
        lF << "###f0=" << lVal << ",";
        asciiFile.addPos(lPos);
        asciiFile.addNote(lF.str().c_str());
        return;
      }
      if (item.m_length<380) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readSlide: the size of a slide seems too short\n"));
        lF << "###";
        asciiFile.addPos(lPos);
        asciiFile.addNote(lF.str().c_str());
        return;
      }

      if (m_state->m_idToSlide.find(item.m_id)==m_state->m_idToSlide.end())
        m_state->m_idToSlide[item.m_id]=Canvas5ParserInternal::Slide();
      auto &slide=m_state->m_idToSlide.find(item.m_id)->second;

      float fDims[2];
      for (auto &d : fDims) d=float(readDouble(*lStream, 8));
      slide.m_dim = MWAWVec2f(fDims[0],fDims[1]);
      lF << "page[dim]=" << slide.m_dim << ",";
      lVal=int(lInput->readLong(4));
      if (lVal) // 0,1,6
        lF << "f1=" << lVal << ",";
      lF << "ID=[";
      for (int i=0; i<2; ++i)
        lF << std::hex << lInput->readULong(4) << std::dec << ",";
      asciiFile.addPos(lPos);
      asciiFile.addNote(lF.str().c_str());

      lPos=lInput->tell();
      lF.str("");
      lF << what << "-S" << item.m_id << "[name]:";
      std::string text;
      for (int i=0; i<256; ++i) {
        char c=char(lInput->readULong(1));
        if (c==0)
          break;
        text+=c;
      }
      lF << text << ",";
      asciiFile.addPos(lPos);
      asciiFile.addNote(lF.str().c_str());
      lInput->seek(lPos+256, librevenge::RVNG_SEEK_SET);

      lPos=lInput->tell();
      lF.str("");
      lF << what << "-S" << item.m_id << "[II]:";
      lVal=int(lInput->readLong(4));
      if (lVal) lF << "f0=" << lVal << ",";
      for (int i=0; i<2; ++i) {
        double dVal=readDouble(*lStream, 8);
        if (dVal<0 || dVal>0) lF << "unk" << i << "=" << dVal << ",";
      }
      lF << "num[layer?]=" << lInput->readLong(4) << ",";
      slide.m_numLayers=int(lInput->readLong(4));
      if (slide.m_numLayers!=1)
        lF << "num[layer]=" << slide.m_numLayers << ",";
      lF << "unkn=[";
      for (int i=0; i<7; ++i) { // firsts is a double, other?
        double dVal=readDouble(*lStream, 8);
        if (dVal<0 || dVal>0)
          lF << dVal << ",";
        else
          lF << "_,";
      }
      lF << "],";
      asciiFile.addPos(lPos);
      asciiFile.addNote(lF.str().c_str());

      lPos=lInput->tell();
      lF.str("");
      lF << what << "-S" << item.m_id << "[layer]:";
      long lLen=long(lInput->readULong(4));
      if (slide.m_numLayers<0 || lLen<8+8*slide.m_numLayers || (lLen-8)/8<slide.m_numLayers ||
          lPos+4+lLen<lPos+20 || lPos+4+lLen>endPos) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readSlide: can not find the slide list\n"));
        lF << "###";
        asciiFile.addPos(lPos);
        asciiFile.addNote(lF.str().c_str());
        return;
      }
      lF << "ID=[";
      for (int i=0; i<2; ++i)
        lF << std::hex << lInput->readULong(4) << std::dec << ",";
      lF << "],";
      for (int i=0; i<slide.m_numLayers; ++i) {
        lF << "[";
        slide.m_layers.push_back(int(lInput->readULong(4)));
        lF << "L" << slide.m_layers.back() << ",";
        lVal=int(lInput->readLong(4));
        if (lVal!=5)
          lF << "f0=" << lVal << ",";
        lF << "]";
      }
      asciiFile.addPos(lPos);
      asciiFile.addNote(lF.str().c_str());
    });
  }
  if (!readExtendedHeader(stream, 0xac, "Slide",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Canvas5Parser::Item const &item, std::string const &what) {
  auto lInput=lStream->input();
    long lPos=lInput->tell();
    libmwaw::DebugStream lF;
    auto &asciiFile=lStream->ascii();

    if (m_state->m_idToSlide.find(item.m_id)==m_state->m_idToSlide.end())
      m_state->m_idToSlide[item.m_id]=Canvas5ParserInternal::Slide();
    auto &slide=m_state->m_idToSlide.find(item.m_id)->second;
    int lVal;
    for (int i=0; i<4; ++i) { // f1=240|2c0|6c0, f3=0|1
      lVal=int(lInput->readLong(2));
      if (lVal)
        lF << "f" << i << "=" << lVal << ",";
    }
    float fDims[2];
    for (auto &d : fDims) d=float(lInput->readLong(4))/65536.f;
    slide.m_dim = MWAWVec2f(fDims[0],fDims[1]);
    lF << "page[dim]=" << slide.m_dim << ",";
    for (int i=0; i<2; ++i) {  // f5=1-13,
      lVal=int(lInput->readLong(2));
      if (lVal)
        lF << "f" << 4+i << "=" << lVal << ",";
    }
    asciiFile.addDelimiter(lInput->tell(),'|');
    lInput->seek(8, librevenge::RVNG_SEEK_CUR);
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());

    lPos=lInput->tell();
    lF.str("");
    lF << what << "-E" << item.m_id << "[name]:";
    std::string text;
    for (int i=0; i<128; ++i) {
      char c=char(lInput->readULong(1));
      if (c==0)
        break;
      text+=c;
    }
    lF << text << ",";
    asciiFile.addPos(lPos);
    asciiFile.addNote(lF.str().c_str());
    lInput->seek(lPos+128, librevenge::RVNG_SEEK_SET);

    lPos=lInput->tell();
    lF.str("");
    lF << what << "-E" << item.m_id << "[A]:";
    for (int i=0; i<6; ++i) { // f2=0|260, f4=0-3, f5=0-2
      lVal=int(lInput->readLong(2));
      if (!lVal) continue;
      lF << "f" << i << "=" << lVal << ",";
    }
    slide.m_numLayers=int(lInput->readLong(4));
    if (slide.m_numLayers!=1)
      lF << "num[layer]=" << slide.m_numLayers << ",";
    asciiFile.addPos(lPos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  if (!readIndexMap(stream, "SlideLa",
  [this](std::shared_ptr<Canvas5Structure::Stream> lStream, Item const &item, std::string const &) {
  auto lInput=lStream->input();
    auto &asciiFile=lStream->ascii();
    auto it=m_state->m_idToSlide.find(item.m_id);
    if (it==m_state->m_idToSlide.end() || item.m_length<8+8*it->second.m_numLayers) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readSlides: can not read the slides index %d\n", item.m_id));
      asciiFile.addPos(item.m_pos);
      asciiFile.addNote("###");
      return;
    }
    libmwaw::DebugStream lF;
    auto &slide=it->second;
    lInput->seek(8, librevenge::RVNG_SEEK_CUR); // 0
    lF << "layers=[";
    for (int i=0; i<slide.m_numLayers; ++i) {
      lF << "[";
      slide.m_layers.push_back(int(lInput->readULong(4)));
      lF << "L" << slide.m_layers.back() << ",";
      for (int j=0; j<2; ++j) { // f2 0 | big number
        int lVal=int(lInput->readLong(2));
        if (lVal) lF << "f" << i+1 << "=" << lVal << ",";
      }
      lF << "],";
    }
    lF << "],";
    asciiFile.addPos(item.m_pos);
    asciiFile.addNote(lF.str().c_str());
  }))
  return false;

  std::vector<bool> defined;
  return readDefined(*stream, defined, "Slide");
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool Canvas5Parser::checkHeader(MWAWHeader *header, bool strict)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(0x100))
    return false;

  input->setReadInverted(false);
  input->seek(0, librevenge::RVNG_SEEK_SET);
  int vers=5;
  int byteOrdering=int(input->readLong(1));
  if (byteOrdering==0) {
    if (input->readLong(1) || input->readLong(4) || input->readLong(4))
      return false;
    byteOrdering=int(input->readLong(1));
    vers=9;
  }
  switch (byteOrdering) {
  case 1:
    m_state->m_isWindowsFile=true;
    input->setReadInverted(true);
    break;
  case 2:
    break;
  default:
    return false;
  }
  if (input->readULong(4)!=0x8000)
    return false;

  for (int i=0; i<2; ++i) {
    long pos=input->tell();
    // try to read the ith zone header
    int compType=int(input->readULong(4));
    if (vers>=9)
      compType&=0xf;
    else if (compType<0 || compType>8) {
      compType=compType&0xf; // ok assume that this is an type (with a checksum)
      if (compType<0 || compType>8)
        return false;
      if (vers==5)
        vers=6;
    }

    long len=long(input->readULong(4));
    if ((i==0 && len<0x800) || len>0x8000)
      return false;

    long len1=long(input->readULong(4));
    if (len1<0 || len1>len+12 || pos+len1+12<=pos+12 || !input->checkPosition(pos+len1+12))
      return false;
    input->seek(len1, librevenge::RVNG_SEEK_CUR);
    if (!strict)
      break;
  }
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_CANVAS, vers, MWAWDocument::MWAW_K_DRAW);

  input->seek(vers>=9 ? 15 : 5, librevenge::RVNG_SEEK_SET);
  return true;
}

// ------------------------------------------------------------
// mac resource fork
// ------------------------------------------------------------

bool Canvas5Parser::readPnot(Canvas5Structure::Stream &stream, MWAWEntry const &entry)
{
  auto input=stream.input();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<14) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPnot: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(Pnot)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  int val=int(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  val=int(input->readULong(4)); // PICT
  f << Canvas5Structure::getString(unsigned(val)) << ",";
  f << "id=" << input->readULong(2) << ",";
  val=int(input->readLong(2));
  if (val) f << "f1=" << val << ",";

  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5Parser::readPicture(Canvas5Structure::Stream &stream, MWAWEntry const &entry)
{
  auto input=stream.input();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<14) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readPicture: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(RSRCPicture)[" << entry.id() << "]:";
#ifdef DEBUG_WITH_FILES
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  // 42 is the size of a v2 empty picture, so probably ok
  std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(entry.length())));
  if (!pict && entry.length()!=42)
    f << "###";
  else
    ascFile.skipZone(entry.begin(), entry.end()-1);
  MWAWEmbeddedObject obj;
  librevenge::RVNGBinaryData file;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  input->readDataBlock(entry.length(), file);
  libmwaw::DebugStream s;
  s << "PICT-" << entry.id() << ".pct";
  libmwaw::Debug::dumpFile(file, s.str().c_str());
#endif
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

// ------------------------------------------------------------
// windows resource fork
// ------------------------------------------------------------

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////

bool Canvas5Parser::send(Canvas5ParserInternal::Slide const &slide)
{
  auto listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("Canvas5Parser::send[slide]: can not find the listener\n"));
    return false;
  }
  size_t numLayers=slide.m_layers.size();
  bool needOpenLayer=numLayers==2 ? (m_state->m_type==1 || slide.m_layers[0]!=1) : numLayers>2;
  int layerId=0;
  for (auto lId : slide.m_layers) {
    if (m_state->m_layerIdInMasterSet.find(lId)!=m_state->m_layerIdInMasterSet.end())
      continue; // do not resend layer in id
    auto it = m_state->m_idToLayer.find(lId);
    if (it==m_state->m_idToLayer.end()) {
      MWAW_DEBUG_MSG(("Canvas5Parser::send[slide]: can not find layer %d\n", lId));
      continue;
    }
    auto const &layer=it->second;
    if (needOpenLayer) {
      if (!layer.m_name.empty())
        listener->openLayer(layer.m_name);
      else {
        std::stringstream s;
        s << "Layer" << ++layerId;
        listener->openLayer(s.str().c_str());
      }
    }
    send(layer);
    if (needOpenLayer)
      listener->closeLayer();
  }
  return true;
}

bool Canvas5Parser::send(Canvas5ParserInternal::Layer const &layer)
{
  auto listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("Canvas5Parser::send[layer]: can not find the listener\n"));
    return false;
  }
  for (auto sId : layer.m_shapesId)
    m_graphParser->sendShape(sId);
  return true;
}

// ------------------------------------------------------------
// low level
// ------------------------------------------------------------
int Canvas5Parser::readInteger(Canvas5Structure::Stream &stream, int fieldSize) const
{
  auto input=stream.input();
  if (!input || !input->checkPosition(input->tell()+fieldSize)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readInteger: can not find the input\n"));
    return 0;
  }
  switch (fieldSize) {
  case 2:
  case 4:
    return int(input->readLong(fieldSize));
  case 8: {
    double val;
    bool isNan;
    if (!readDouble(stream, val, isNan))
      return 0;
    return int(val);
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5Parser::readInteger: unexpected field size=%d\n",fieldSize));
    if (fieldSize>0)
      input->seek(fieldSize, librevenge::RVNG_SEEK_CUR);
    return 0;
  }
}

double Canvas5Parser::readDouble(Canvas5Structure::Stream &stream, int fieldSize) const
{
  auto input=stream.input();
  long endPos=input->tell()+fieldSize;
  if (!input || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDouble: can not find the input\n"));
    return 0;
  }
  switch (fieldSize) {
  case 4:
    return double(input->readLong(4))/65536;
  case 8: {
    double val;
    bool isNan;
    if (!readDouble(stream, val, isNan)) {
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      return 0;
    }
    return val;
  }
  default:
    MWAW_DEBUG_MSG(("Canvas5Parser::readDouble: unexpected field size=%d\n",fieldSize));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return 0;
  }
}

bool Canvas5Parser::readDouble(Canvas5Structure::Stream &stream, double &val, bool &isNaN) const
{
  auto input=stream.input();
  if (!input || !input->checkPosition(input->tell()+8)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDouble: can not find the input\n"));
    return false;
  }
  if (input->readInverted())
    return input->readDoubleReverted8(val, isNaN);
  return input->readDouble8(val, isNaN);
}

bool Canvas5Parser::readString(Canvas5Structure::Stream &stream, librevenge::RVNGString &string, int maxSize, bool canBeCString)
{
  string.clear();
  auto input=stream.input();
  if (!input) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readString: can not find the input\n"));
    return false;
  }
  bool const isWindows=isWindowsFile();
  auto fontConverter=getFontConverter();
  int defaultFont=isWindows ? fontConverter->getId("CP1252") : 3;
  if (isWindows && canBeCString) {
    int n=0;
    while (!input->isEnd() && (maxSize<=0 || n<maxSize)) {
      char c=char(input->readULong(1));
      if (c==0)
        break;
      int unicode = fontConverter->unicode(defaultFont, static_cast<unsigned char>(c));
      if (unicode>0)
        libmwaw::appendUnicode(uint32_t(unicode), string);
      else {
        MWAW_DEBUG_MSG(("Canvas5Parser::readString: find unknown unicode for char=%d\n", int(c)));
      }
    }
    return true;
  }
  int sSz=int(input->readULong(1));
  if ((maxSize<=0 || sSz<maxSize) && input->checkPosition(input->tell()+sSz)) {
    for (int ch=0; ch<sSz; ++ch) {
      char c=char(input->readULong(1));
      if (c==0)
        break;
      int unicode = fontConverter->unicode(defaultFont, static_cast<unsigned char>(c));
      if (unicode>0)
        libmwaw::appendUnicode(uint32_t(unicode), string);
      else {
        MWAW_DEBUG_MSG(("Canvas5Parser::readString: find unknown unicode for char=%d\n", int(c)));
      }
    }
  }
  else {
    MWAW_DEBUG_MSG(("Canvas5Parser::readString: bad size=%d\n", sSz));
    return false;
  }
  return true;
}

bool Canvas5Parser::readDataHeader(Canvas5Structure::Stream &stream, int expectedSize, int &N)
{
  auto input=stream.input();
  if (!input)
    return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+4))
    return false;
  int dSize=int(input->readULong(4));
  if (!dSize) {
    N=0;
    return true;
  }
  if (dSize<0 || dSize!=expectedSize || !input->checkPosition(pos+8))
    return false;
  N=int(input->readULong(4));
  if (N<0 || (input->size()-pos)/long(dSize)<N || pos+8+long(dSize)*N<pos+8 || !input->checkPosition(pos+8+long(dSize)*N))
    return false;
  return true;
}

bool Canvas5Parser::readExtendedHeader(std::shared_ptr<Canvas5Structure::Stream> stream, int expectedValue, std::string const &what, DataFunction const &func)
{
  if (!stream) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readExtendedHeader: no stream\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input ? input->tell() : 0;
  libmwaw::DebugStream f;
  auto &ascFile=stream->ascii();
  f << what << "-extended:";
  if (!input || !input->checkPosition(input->tell()+12) || int(input->readULong(4))!=expectedValue) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readExtendedHeader: the size seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int f0=int(input->readLong(4));
  int N=int(input->readULong(4));
  if (N) f << "N=" << N << ",";
  if (f0) f << "f0=" << f0 << ",";
  if (N<0 || pos+8+N<pos || !input->checkPosition(pos+8+N)) {
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  if (expectedValue<=0 || (N%expectedValue)!=0) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readExtendedHeader: the data size seems bad\n"));
    f << "###";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+12+N, librevenge::RVNG_SEEK_SET);
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (!N) return true;
  if (expectedValue==1) {
    pos=input->tell();
    f.str("");
    f<< what << "-E0:";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    Item item;
    item.m_id=0;
    item.m_pos=pos;
    item.m_length=N;
    func(stream, item, what);
    if (input->tell()!=pos && input->tell()!=pos+N)
      ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+N, librevenge::RVNG_SEEK_SET);
    return true;
  }

  N/=expectedValue;
  // the first value seems always a buffer (which contains junk data)
  ascFile.addPos(input->tell());
  ascFile.addNote("_");
  input->seek(expectedValue, librevenge::RVNG_SEEK_CUR);
  for (int i=1; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f<< what << "-E" << i << ":";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    Item item;
    item.m_id=i;
    item.m_pos=pos;
    item.m_length=expectedValue;
    func(stream, item, what);
    if (input->tell()!=pos && input->tell()!=pos+expectedValue)
      ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+expectedValue, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

void Canvas5Parser::stringDataFunction(std::shared_ptr<Canvas5Structure::Stream> stream, Canvas5Parser::Item const &item, std::string const &)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5Parser::stringDataFunction: can not find any input\n"));
    return;
  }
  auto input=stream->input();
  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;
  for (int i=0; i<int(item.m_length); ++i) {
    char c=char(input->readULong(1));
    if (c==0)
      break;
    f << c;
  }
  ascFile.addPos(item.m_pos);
  ascFile.addNote(f.str().c_str());
}

bool Canvas5Parser::readIndexMap(std::shared_ptr<Canvas5Structure::Stream> stream, std::string const &what, DataFunction const &func)
{
  if (!stream || !stream->input()) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readIndexMap: can not find the input\n"));
    return false;
  }
  auto input=stream->input();
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile = stream->ascii();
  f << what << "[id]:";

  int N;
  if (!readDataHeader(*stream, 12,N)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readIndexMap: can not read the header N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  std::map<int, MWAWEntry> idToEntry;
  for (int i=1; i<=N; ++i) {
    pos=input->tell();
    f.str("");
    f << what << "-" << i << "[id]:";
    MWAWEntry entry;
    long len=long(input->readULong(4));
    int id=int(input->readULong(4));
    if (id==0) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      input->seek(pos+12, librevenge::RVNG_SEEK_SET);
      continue;
    }
    if (id!=1) f << "id=" << id << ",";
    entry.setBegin(long(input->readULong(4)));
    entry.setLength(len);
    entry.setId(id);
    f << std::hex << entry.begin() << "<->" << entry.end() << std::dec << ",";
    if (entry.valid())
      idToEntry[i]=entry;
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+12, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readIndexMap: can not find the number of map\n"));
    return false;
  }
  f.str("");
  f << what << "[data]:";
  N=int(input->readULong(4));
  f << "num=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  for (int z=1; z<=N; ++z) {
    pos=input->tell();
    f.str("");
    f << what << "[data-" << z << "]:";
    int len=int(input->readULong(4));
    f << "len=" << len << ",";
    long endPos=pos+4+len;
    if (len<0 || !input->checkPosition(pos+4+len)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readIndexMap: can not find the length of the map data %d\n", z));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }

    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    if (len==0) continue;
    pos+=4;

    for (auto const &it : idToEntry) {
      auto const &entry=it.second;
      if (entry.id()!=z) continue;
      if (pos+entry.end()>endPos) {
        MWAW_DEBUG_MSG(("Canvas5Parser::readIndexMap: can not find data %d\n", it.first));
        continue;
      }
      ascFile.addPos(pos+entry.end());
      ascFile.addNote("_");
      f.str("");
      f << what << "-Dt" << it.first << ":";
      ascFile.addPos(pos+entry.begin());
      ascFile.addNote(f.str().c_str());
      input->seek(pos+entry.begin(), librevenge::RVNG_SEEK_SET);
      Item item;
      item.m_pos=pos+entry.begin();
      item.m_id=it.first;
      item.m_length=entry.length();
      func(stream, item, what);
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  return true;
}

bool Canvas5Parser::readDefined(Canvas5Structure::Stream &stream, std::vector<bool> &defined, std::string const &what)
{
  auto input=stream.input();
  long pos=input ? input->tell() : 0;
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << what << "[def,N]:";
  if (!input || !input->checkPosition(pos+16)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDefined: can not find the input\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=["; //
  for (int i=0; i<2; ++i) f << input->readLong(4) << ",";
  f << "],";
  int val=int(input->readLong(4)); // 0
  if (val) f << "f0=" << val << ",";
  if (input->readLong(4)!=4) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDefined: bad header\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << what << "[def]:";
  int N;
  if (!readDataHeader(stream, 4,N)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readDefined: can not read N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  f << "def=[";
  defined.clear();
  for (int i=0; i<N; ++i) {
    defined.push_back(input->readLong(4)!=0); // 0 or -1
    f << (defined.back() ? "*" : "_") << ",";
  }
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+8+4*N, librevenge::RVNG_SEEK_SET);
  return true;
}

bool Canvas5Parser::readUsed(Canvas5Structure::Stream &stream, std::string const &what)
{
  auto input=stream.input();
  long pos=input ? input->tell() : 0;
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << what << "[used,N]:";
  if (!input || !input->checkPosition(pos+20) || input->readULong(4)!=4) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readUsed: can not find the input\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=["; // used, id
  for (int i=0; i<2; ++i) f << input->readLong(4) << ",";
  f << "],";
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(4));
    if (val!=(i==1 ? 8 : 0)) f << "f" << i << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << what << "[used]:";
  int N;
  if (!readDataHeader(stream,8,N)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readUsed: can not read N\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << what << "-U" << i+1 << ":";
    int val=int(input->readLong(4));
    if (val!=-1) f << "f0=" << val << ",";
    val=int(input->readLong(4));
    if (val==0) {
      ascFile.addPos(pos);
      ascFile.addNote("_");
      continue;
    }
    if (val!=1) f << "used=" << val << ",";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }
  return true;
}

bool Canvas5Parser::readArray9(std::shared_ptr<Canvas5Structure::Stream> stream, std::string const &what,
                               Canvas5Parser::DataFunction const &func)
{
  if (!stream)
    return false;
  auto input=stream->input();
  if (!input)
    return false;

  auto &ascFile=stream->ascii();
  libmwaw::DebugStream f;
  f << what << "[header]:";
  long pos=input->tell();
  if (!checkTAG9(*stream,"ARRAY",0)) {
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  pos=input->tell();
  if (!input->checkPosition(pos+44)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readArray9: the array zone seems too short\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  std::string name;
  for (int i=0; i<32; ++i) {
    char c=char(input->readLong(1));
    if (!c)
      break;
    name+=c;
  }
  f << name << ",";
  input->seek(pos+32, librevenge::RVNG_SEEK_SET);
  int type=int(input->readLong(4));
  int decal=int(input->readLong(4));
  if (type==400)
    f << "fixed=" << decal << ",";
  else
    f << "type=" << type << "[" << decal << "],";
  int N=int(input->readLong(4));
  f << "N=" << N << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << what << "-" << i << ":";
    Canvas5Parser::Item item;
    item.m_pos=input->tell();
    item.m_decal=decal;
    int used;
    if (!readItemHeader9(*stream, item.m_id, used)) {
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    f << "id=" << item.m_id << ",";
    if (used!=1)
      f << "used=" << used << ",";
    if (used==0) {
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }

    unsigned uVal=unsigned(input->readULong(4));
    if (type==200 || type==400) {
      item.m_type=uVal;
      if (uVal!=1)
        f << "type=" << Canvas5Structure::getString(uVal) << ",";
    }
    else if (uVal)
      f << "f0=" << uVal << ",";
    long len=-1;
    switch (type) {
    case 100:
    case 200: // ink, the type can be found at position pos+14 ?
    case 500: // name
    case 600: // image?
      if (decal<0 || pos+18+decal<pos+18 || !input->checkPosition(pos+18+decal))
        break;
      input->seek(pos+14+decal, librevenge::RVNG_SEEK_SET);
      len=input->readLong(4);
      if (len>=0 && len+18+decal>=len)
        len+=18+decal;
      else
        len=-1;
      break;
    // 200: len at pos 22 f1=decal?
    // 500: name, len at pos 14
    // 600: len at pos 14
    case 400: // checkme: sometimes the data begin at position 14
      len=18+decal;
      break;
    default:
      break;
    }
    if (len<18 || pos+len<pos+18 || !input->checkPosition(pos+18+len)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::readArray9: can not read an item\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+18, librevenge::RVNG_SEEK_SET);
    item.m_length=len-18;
    func(stream, item, what);
    input->seek(pos+len, librevenge::RVNG_SEEK_SET);
  }
  pos=input->tell();
  if (!checkTAG9(*stream,"ARRAY",1)) {
    f.str("");
    f << what << "-end:###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  return true;
}

bool Canvas5Parser::readItemHeader9(Canvas5Structure::Stream &stream, int &id, int &used)
{
  auto input=stream.input();
  if (!input)
    return false;

  long pos=input->tell();
  if (!input->checkPosition(pos+14)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::readItemHeader9: the item zone seems too short\n"));
    return false;
  }
  std::string name;
  for (int i=0; i<6; ++i) name+=char(input->readLong(1));
  if (name!="X+--+X") {
    MWAW_DEBUG_MSG(("Canvas5Parser::readItemHeader9: can not read the item header\n"));
    return false;
  }
  id=int(input->readLong(4));
  used=int(input->readLong(4));
  return true;
}

// ------------------------------------------------------------
// TAG
// ------------------------------------------------------------
bool Canvas5Parser::checkTAG9(Canvas5Structure::Stream &stream, std::string const &tag, int type)
{
  if (version()<9)
    return true;

  auto input=stream.input();
  if (!input)
    return false;
  long pos=input->tell();

  std::string fTag;
  int fType;
  if (!getTAG9(stream, fTag, fType) || fTag!=tag || fType!=type)
    return false;
  auto &ascFile=stream.ascii();
  libmwaw::DebugStream f;
  f << "TAG[" << tag << "]";
  switch (type) {
  case 0:
    f << "begin,";
    break;
  case 1:
    f << "end,";
    break;
  default:
    f << "###";
    break;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool Canvas5Parser::getTAG9(Canvas5Structure::Stream &stream, std::string &tag, int &type)
{
  auto input=stream.input();
  long pos=input->tell();
  if (!input->checkPosition(pos+1+3+1+1+1+3+1) || input->readULong(1)!='<') {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  bool ok=true;
  for (int st=0; st<3; ++st) {
    std::string what;
    while (true) {
      if (input->isEnd()) {
        ok=false;
        break;
      }
      char c=char(input->readLong(1));
      if ((c=='_'&&st<2) || (c=='>'&&st==2))
        break;
      if ((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9'))
        what+=c;
      else {
        ok=false;
        break;
      }
    }
    ok=ok && !what.empty();
    if (!ok)
      break;
    switch (st) {
    case 0:
      if (what=="BEGIN")
        type=0;
      else if (what=="END")
        type=1;
      else
        ok=1;
      break;
    case 1:
      tag=what;
      break;
    case 2:
    default:
      ok=what=="TAG";
      break;
    }
  }

  if (!ok)
    input->seek(pos, librevenge::RVNG_SEEK_SET);
  return ok;
}

// ------------------------------------------------------------
// decode stream
// ------------------------------------------------------------
MWAWInputStreamPtr Canvas5Parser::decode(MWAWInputStreamPtr input, int version)
{
  MWAWInputStreamPtr res;
  if (!input)
    return res;

  long pos=version>=9 ? 15 : 5;
  if (!input->checkPosition(pos+12)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::decode: the input seems too short\n"));
    return res;
  }

  input->seek(0, librevenge::RVNG_SEEK_SET);
  unsigned long read;
  const unsigned char *dt = input->read((unsigned long)(pos), read);
  if (!dt || read != (unsigned long)(pos)) {
    MWAW_DEBUG_MSG(("Canvas5Parser::decode: can not read some data\n"));
    return res;
  }

  auto stream=std::make_shared<MWAWStringStream>(dt, unsigned(pos));
  while (!input->isEnd()) {
    pos=input->tell();
    if (!input->checkPosition(pos+12))
      break;
    int type=int(input->readULong(4));
    // v5: compressed type (between 0 and 8)
    // v6: if checksum (check=0, for i in data check+=i), Canvas store (checksum)<<4|(compressed type)
    if (version>=6) type=(type&0xf);
    unsigned long lengths[2];
    for (auto &l : lengths) l=input->readULong(4);
    long endPos=pos+12+long(lengths[1]);
    if (type<0 || type>8 || long(lengths[0])<=0 || long(lengths[0])>0x8000 ||
        lengths[0]+12<lengths[1] || long(lengths[1])<0 ||
        endPos<pos+12 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    // checkme: v5 I only see type=0|7|8, v6 I only see type=0|8
    if (!Canvas5Structure::decodeZone5(input, endPos, type, lengths[0], stream)) {
      MWAW_DEBUG_MSG(("Canvas5Parser::decode: problem with type=%d at position=%lx\n", type, (unsigned long)pos));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return res;
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
#ifdef DEBUG
    if (lengths[0]!=0x8000) {
      stream->seek(0, librevenge::RVNG_SEEK_END);
      std::cout << "\t" << std::hex << stream->tell() << std::dec << "\n";
    }
#endif
  }
  if (!input->isEnd()) { // last zone is not compressed
    MWAW_DEBUG_MSG(("Canvas5Parser::decode: stop at pos=%lx->%lx\n", (unsigned long) input->tell(),
                    (unsigned long) stream->tell()));
    unsigned long remain=(unsigned long)(input->size()-input->tell());
    dt = input->read(remain, read);
    if (!dt || read != remain) {
      MWAW_DEBUG_MSG(("Canvas5Parser::decode: can not read some data\n"));
      return res;
    }
    stream->append(dt, unsigned(remain));
  }

  res.reset(new MWAWInputStream(stream, false));
  res->seek(0, librevenge::RVNG_SEEK_SET);
  res->setReadInverted(input->readInverted());
  return res;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
