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

#include "Canvas5BMParser.hxx"

/** Internal: the structures of a Canvas5BMParser */
namespace Canvas5BMParserInternal
{
////////////////////////////////////////
//! Internal: the state of a Canvas5BMParser
struct State {
  //! constructor
  State()
    : m_isWindowsFile(false)
    , m_stream()

    , m_dimension()
    , m_image()
  {
  }

  //! true if this is a windows file
  bool m_isWindowsFile;
  //! the current stream
  std::shared_ptr<Canvas5Structure::Stream> m_stream;
  //! the image dimension
  MWAWVec2i m_dimension;
  //! the image
  MWAWEmbeddedObject m_image;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
Canvas5BMParser::Canvas5BMParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new Canvas5BMParserInternal::State);

  getPageSpan().setMargins(0);
}

Canvas5BMParser::~Canvas5BMParser()
{
}

bool Canvas5BMParser::isWindowsFile() const
{
  return m_state->m_isWindowsFile;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void Canvas5BMParser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    checkHeader(nullptr);

    auto input=getInput();
    if (!input)
      throw(libmwaw::ParseException());

    // create the main stream
    m_state->m_stream=std::make_shared<Canvas5Structure::Stream>(input);
    m_state->m_stream->ascii().open(asciiName());

    ok = createZones() && createDocument(docInterface);
  }
  catch (...) {
    MWAW_DEBUG_MSG(("Canvas5BMParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  ascii().reset();
  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
bool Canvas5BMParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return false;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("Canvas5BMParser::createDocument: listener already exist\n"));
    return false;
  }

  if (m_state->m_dimension[0]<=0 || m_state->m_dimension[1]<=0 || m_state->m_image.isEmpty()) {
    MWAW_DEBUG_MSG(("Canvas5BMParser::createDocument: can not find the image\n"));
    return false;
  }
  std::vector<MWAWPageSpan> pageList;
  MWAWPageSpan ps(getPageSpan());
  ps.setFormLength(double(m_state->m_dimension[1])/72);
  ps.setFormWidth(double(m_state->m_dimension[0])/72);
  ps.setPageSpan(1);
  pageList.push_back(ps);
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->startDocument();

  MWAWPosition pos(MWAWVec2f(0, 0), MWAWVec2f(m_state->m_dimension), librevenge::RVNG_POINT);
  pos.setRelativePosition(MWAWPosition::Page);
  pos.m_wrapping = MWAWPosition::WNone;
  listen->insertPicture(pos, m_state->m_image);
  return true;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool Canvas5BMParser::createZones()
{
  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (rsrcParser)
    rsrcParser->getEntriesMap();

  auto stream=m_state->m_stream;
  if (!stream || !stream->input())
    return false;
  if (!readFileHeader(*stream))
    return false;
  if (!Canvas5Structure::readBitmapDAD58Bim(*stream, version(), m_state->m_image))
    return false;

  if (!stream->input()->isEnd()) {
    MWAW_DEBUG_MSG(("Canvas5BMParser::createZones: find extra data\n"));
    auto &ascFile=stream->ascii();
    ascFile.addPos(stream->input()->tell());
    ascFile.addNote("Entries(Extra):###");
  }
  return !m_state->m_image.isEmpty();
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool Canvas5BMParser::checkHeader(MWAWHeader *header, bool /*strict*/)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(0x100))
    return false;

  input->setReadInverted(false);
  input->seek(0, librevenge::RVNG_SEEK_SET);
  int val=int(input->readULong(4));
  if ((val!=1&&val!=2) || input->readULong(4)!=0x44414435 || input->readULong(4)!=0x50524f58) // DAD5, PROX
    return false;

  int vers=val==1 ? 5 : 9;
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_CANVAS, vers, MWAWDocument::MWAW_K_PAINT);

  input->seek(12, librevenge::RVNG_SEEK_SET);
  return true;
}

bool Canvas5BMParser::readFileHeader(Canvas5Structure::Stream &stream)
{
  auto input=stream.input();
  if (!input) return false;

  int const vers=version();
  if (!input->checkPosition(vers<9 ? 36 : 40)) {
    MWAW_DEBUG_MSG(("Canvas5BMParser::readFileHeader: the zone is too short\n"));
    return false;
  }
  input->seek(12, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "FileHeader:";
  f << "len=" << input->readULong(4) << ","; // seems a little greater than file length
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(4));
  m_state->m_dimension=MWAWVec2i(dim[1], dim[0]);
  f << "dim=" << m_state->m_dimension << ",";
  int numPlanes=int(input->readLong(4)); // 1-4
  if (numPlanes!=1)
    f << "num[planes]=" << numPlanes << ",";
  int numBytes=int(input->readLong(4)); // number of byte?
  if (numBytes!=8)
    f << "num[bytes]=" << numBytes << ",";
  double res=72;
  if (vers<9)
    res=double(input->readULong(4))/65536.f;
  else {
    bool isNan;
    if (!input->readDouble8(res, isNan))
      f << "###";
  }
  if (res<72 || res>72)
    f << "res=" << res << ",";
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());
  return true;
}

// ------------------------------------------------------------
// mac resource fork
// ------------------------------------------------------------

// ------------------------------------------------------------
// windows resource fork
// ------------------------------------------------------------

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
