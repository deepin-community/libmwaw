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

#include "MWAWGraphicListener.hxx"
#include "MWAWHeader.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPosition.hxx"

#include "MacPaintParser.hxx"

/** Internal: the structures of a MacPaintParser */
namespace MacPaintParserInternal
{
////////////////////////////////////////
//! Internal: the state of a MacPaintParser
struct State {
  //! constructor
  State()
    : m_bitmap()
  {
  }
  /// the bitmap (v1)
  std::shared_ptr<MWAWPict> m_bitmap;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
MacPaintParser::MacPaintParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

MacPaintParser::~MacPaintParser()
{
}

void MacPaintParser::init()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new MacPaintParserInternal::State);

  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void MacPaintParser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      sendBitmap();
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("MacPaintParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void MacPaintParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("MacPaintParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  ps.setPageSpan(1);
  std::vector<MWAWPageSpan> pageList(1,ps);
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool MacPaintParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  if (input->size()<512) return false;
#ifdef DEBUG
  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<256; i++) { // normally 0, but can be a list of patter
    auto val=static_cast<int>(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
#endif
  if (!readBitmap()) return false;
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("MacPaintParser::createZones: find some extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(End):###");
  }
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool MacPaintParser::sendBitmap()
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("MacPaintParser::sendBitmap: can not find the listener\n"));
    return false;
  }

  MWAWEmbeddedObject picture;
  if (!m_state->m_bitmap || !m_state->m_bitmap->getBinary(picture)) return false;

  MWAWPageSpan const &page=getPageSpan();
  MWAWPosition pos(MWAWVec2f(float(page.getMarginLeft()),float(page.getMarginRight())),
                   MWAWVec2f(float(page.getPageWidth()),float(page.getPageLength())), librevenge::RVNG_INCH);
  pos.setRelativePosition(MWAWPosition::Page);
  pos.m_wrapping = MWAWPosition::WNone;
  listener->insertPicture(pos, picture);
  return true;
}

bool MacPaintParser::readBitmap(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  long endPos=input->size();
  input->seek(512, librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  // a bitmap is composed of 720 rows of (72x8bytes)
  std::shared_ptr<MWAWPictBitmapIndexed> pict;
  if (!onlyCheck) {
    pict.reset(new MWAWPictBitmapIndexed(MWAWVec2i(576,720)));
    std::vector<MWAWColor> colors(2);
    colors[0]=MWAWColor::white();
    colors[1]=MWAWColor::black();
    pict->setColors(colors);
  }

  for (int r=0; r<720; ++r) {
    long rowPos=input->tell();
    f.str("");
    f << "Entries(Bitmap)-" << r << ":";
    int col=0;
    while (col<72*8) { // UnpackBits
      if (input->tell()+2>endPos) {
        MWAW_DEBUG_MSG(("MacPaintParser::readBitmap: can not read row %d\n", r));
        f << "###";
        ascii().addPos(rowPos);
        ascii().addNote(f.str().c_str());
        return false;
      }
      auto wh=static_cast<int>(input->readULong(1));
      if (wh>=0x81) {
        auto color=static_cast<int>(input->readULong(1));
        if (onlyCheck) {
          col+=8*(0x101-wh);
          if (col>72*8)
            return false;
          continue;
        }
        for (int j=0; j < 0x101-wh; ++j) {
          if (col>=72*8) {
            MWAW_DEBUG_MSG(("MacPaintParser::readBitmap: can not read row %d\n", r));
            f << "###";
            ascii().addPos(rowPos);
            ascii().addNote(f.str().c_str());
            return false;
          }
          for (int b=7; b>=0; --b)
            pict->set(col++, r, (color>>b)&1);
        }
      }
      else { // checkme normally 0x80 is reserved and almost nobody used it (for ending the compression)
        if (input->tell()+wh+1>endPos) {
          MWAW_DEBUG_MSG(("MacPaintParser::readBitmap: can not read row %d\n", r));
          f << "###";
          ascii().addPos(rowPos);
          ascii().addNote(f.str().c_str());
          return false;
        }
        for (int j=0; j < wh+1; ++j) {
          auto color=static_cast<int>(input->readULong(1));
          if (col>=72*8) {
            MWAW_DEBUG_MSG(("MacPaintParser::readBitmap: can not read row %d\n", r));
            f << "###";
            ascii().addPos(rowPos);
            ascii().addNote(f.str().c_str());
            return false;
          }
          if (onlyCheck) {
            col+=8;
            continue;
          }
          for (int b=7; b>=0; --b)
            pict->set(col++, r, (color>>b)&1);
        }
      }
    }
    ascii().addPos(rowPos);
    ascii().addNote(f.str().c_str());
  }
  if (!onlyCheck)
    m_state->m_bitmap=pict;
  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool MacPaintParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = MacPaintParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(512+720*2))
    return false;

  MWAWDocument::Type type=MWAWDocument::MWAW_T_MACPAINT;
  std::string fType, fCreator;
  if (input->getFinderInfo(fType, fCreator) && fCreator=="PANT")
    type=MWAWDocument::MWAW_T_FULLPAINT;

  int const vers=1;
  if (strict) {
    /* check :
       - if we can read the bitmap,
       - if the data have been packed: ie. if the bitmap size is 720x144
         the bitmap's creator clearly creates the worst possible data,
       - and if after reading the bitmap we are at the end of the file
       (up to 512 char) */
    input->seek(512, librevenge::RVNG_SEEK_SET);
    if (!readBitmap(true) || input->tell()==512+720*144 || input->checkPosition(input->tell()+512))
      return false;
  }
  setVersion(vers);
  if (header)
    header->reset(type, vers, MWAWDocument::MWAW_K_PAINT);

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
