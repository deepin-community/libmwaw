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

#include "PixelPaintParser.hxx"

/** Internal: the structures of a PixelPaintParser */
namespace PixelPaintParserInternal
{
////////////////////////////////////////
//! Internal: the state of a PixelPaintParser
struct State {
  //! constructor
  State()
    : m_bitmapSize(0,0)
    , m_colorList()
    , m_bitmap()
  {
  }
  //! the bitmap size(v1)
  MWAWVec2i m_bitmapSize;
  //! the color map
  std::vector<MWAWColor> m_colorList;
  /// the bitmap
  std::shared_ptr<MWAWPict> m_bitmap;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PixelPaintParser::PixelPaintParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
{
  init();
}

PixelPaintParser::~PixelPaintParser()
{
}

void PixelPaintParser::init()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new PixelPaintParserInternal::State);

  getPageSpan().setMargins(0.1);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void PixelPaintParser::parse(librevenge::RVNGDrawingInterface *docInterface)
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
    MWAW_DEBUG_MSG(("PixelPaintParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void PixelPaintParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("PixelPaintParser::createDocument: listener already exist\n"));
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
bool PixelPaintParser::createZones()
{
  MWAWInputStreamPtr input = getInput();
  int const vers=version();
  if (input->size()<512) return false;
  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET); // seek file def
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  long pos;
  if ((vers<=1&&!readFileHeaderV1()) || (vers>=2&&!readFileHeaderV2()))
    return false;

  pos=input->tell();
  if (!readColorMap()) {
    ascii().addPos(pos);
    ascii().addNote("Entries(ColorMap):###");
    return false;
  }

  pos=input->tell();
  if (!readPatternMap()) {
    ascii().addPos(pos);
    ascii().addNote("Entries(PatternMap):###");
    return false;
  }
  if ((vers==1 && !readBitmapV1()) || (vers==2 && !readBitmapV2()))
    return false;
  if (!input->isEnd()) {
    if (!input->checkPosition(input->tell()+8)) {
      ascii().addPos(input->tell());
      ascii().addNote("Entries(Unused):");
    }
    else {
      MWAW_DEBUG_MSG(("PixelPaintParser::createZones: find some extra data\n"));
      ascii().addPos(input->tell());
      ascii().addNote("Entries(Unused):###");
    }
  }
  return m_state->m_bitmap.get()!=nullptr;
}

bool PixelPaintParser::readFileHeaderV1(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  if (!input->checkPosition(0x426)) {
    MWAW_DEBUG_MSG(("PixelPaintParser::readFileHeaderV1: file is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Zone0):";
  input->seek(4, librevenge::RVNG_SEEK_SET); // seek file def
  long pos=4;
  for (int i=0; i<144; ++i) { // always 0
    auto val=static_cast<int>(input->readULong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  if (!onlyCheck) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  for (int z=0; z<8; ++z) {
    pos=input->tell();
    f.str("");
    f << "Zone0-A" << z << ":";
    int val;
    val=static_cast<int>(input->readULong(2));
    if (onlyCheck && z==0 && (val<1 || val>4)) return false;
    switch (val) {
    case 0:
      break;
    case 1:
      f << "image=512x512,";
      if (z==0)
        m_state->m_bitmapSize=MWAWVec2i(512,512);
      break;
    case 2:
      f << "image=720x576,";
      if (z==0)
        m_state->m_bitmapSize=MWAWVec2i(720,576);
      break;
    case 3:
      f << "image=1024x768,";
      if (z==0)
        m_state->m_bitmapSize=MWAWVec2i(1024,768);
      break;
    case 4:
      f << "image=1024x1024,";
      if (z==0)
        m_state->m_bitmapSize=MWAWVec2i(1024,1024);
      break;
    default:
      MWAW_DEBUG_MSG(("PixelPaintParser::readFileHeaderV1: unknown image size\n"));
      f << "###image=" << val << ",";
      break;
    }
    for (int i=0; i<4; ++i) {
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    val=static_cast<int>(input->readULong(2));
    if (val!=5) // always 5
      f << "f5=" << val << ",";
    int dim[4];
    for (auto &d : dim) d=static_cast<int>(input->readULong(2));
    if (dim[0]||dim[1]||dim[2]||dim[3])
      f << "windows[dim]=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    val=static_cast<int>(input->readULong(2));
    if (val) // always 0
      f << "f6=" << val << ",";
    for (auto &d : dim) d=static_cast<int>(input->readULong(2));
    if (dim[0]||dim[1]||dim[2]||dim[3])
      f << "screen1[dim]=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    for (int i=0; i<8; ++i) { // always 0
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i+7 << "=" << val << ",";
    }
    for (auto &d : dim) d=static_cast<int>(input->readULong(2));
    if (dim[0]||dim[1]||dim[2]||dim[3])
      f << "screen1[sz]=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    for (int i=0; i<2; ++i) { // f15=1|8, f16=0|69
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "f" << i+15 << "=" << val << ",";
    }
    for (int i=0; i<2; ++i) dim[i]=static_cast<int>(input->readULong(2));
    if (dim[0]||dim[1]) f << "width=[" << dim[0] << "," << dim[1] << "],";
    for (int i=0; i<4; ++i) { // g2=1|3
      val=static_cast<int>(input->readULong(2));
      if (val)
        f << "g" << i << "=" << val << ",";
    }
    for (int i=0; i<3; ++i) {
      val=static_cast<int>(input->readULong(4));
      if (val)
        f << "ID" << i << "=" << std::hex << val << std::dec << ",";
    }
    val=static_cast<int>(input->readULong(2)); // always 0?
    if (val) f << "g2=" << val << ",";
    for (auto &d : dim) d=static_cast<int>(input->readULong(2));
    if (dim[0]||dim[1]||dim[2]||dim[3])
      f << "screen2[dim]=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    if (onlyCheck) {
      input->seek(0x426, librevenge::RVNG_SEEK_SET);
      return true;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+92, librevenge::RVNG_SEEK_SET);
  }
  pos=input->tell();
  f.str("");
  f << "Zone0-Prefs:";
  int val;
  for (int i=0; i<5; ++i) { // always 0?
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<6; ++i) {
    val=static_cast<int>(input->readULong(2));
    static int const expected[]= {1, 0x28, 0xaa, 3, 0xff, 9};
    if (val==expected[i]) continue;
    if (i==4)
      f << "font[sz]=" << val << ",";
    else
      f << "f" << i+5 << "=" << val << ",";

  }
  for (int i=0; i<6; ++i) {
    val=static_cast<int>(input->readULong(1));
    if (!val) continue;
    static char const *wh[]= {"setForAllTools", "noFullScreen[zoom]","fl2","autoscroll[fatbits]","center[createObject]","remap[color]"};
    f << wh[i];
    if (val!=1)
      f << "=" << val;
    f << ",";
  }
  val=static_cast<int>(input->readLong(2));
  if (val<0) {
    f << "option[first]=invertPattern,";
    val*=-1;
  }
  switch (val) {
  case 1:
    break;
  case 2:
    f << "option[effect]=transp,";
    break;
  case 3:
    f << "option[effect]=invert,";
    break;
  case 4:
    f << "option[effect]=erase,";
    break;
  default:
    f << "###option[effect]=" << val << ",";
    break;
  }
  val=static_cast<int>(input->readLong(2)); // numColors?
  if (val!=0x100) f << "g0=" << val << ",";
  val=static_cast<int>(input->readLong(2));
  if (val!=0x80) f << "g1=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(0x426, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PixelPaintParser::readFileHeaderV2(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  if (!input->checkPosition(58)) {
    MWAW_DEBUG_MSG(("PixelPaintParser::readFileHeaderV2: file is too short\n"));
    return false;
  }
  input->seek(4, librevenge::RVNG_SEEK_SET); // seek file def
  libmwaw::DebugStream f;
  f << "Entries(Zone0):";
  long pos=4;
  int val;
  for (int i=0; i<2; ++i) {
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int dim[2];
  for (auto &d : dim) d=static_cast<int>(input->readULong(2));
  if (dim[0]<=0||dim[0]>1024 || dim[1]<=0 || dim[1]>1024)
    return false;
  if (onlyCheck) {
    input->seek(58, librevenge::RVNG_SEEK_SET);
    return true;
  }
  m_state->m_bitmapSize=MWAWVec2i(dim[1], dim[0]);
  f << "sz=" << m_state->m_bitmapSize << ",";
  for (int i=0; i<5; ++i) {
    val=static_cast<int>(input->readULong(2));
    static int const expected[]= {8, 0, 0, 0, 0xff };
    if (val==expected[i]) continue;
    if (i==0) // checkme
      f << "font[sz]=" << val << ",";
    else
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<18; ++i) { // g9=256 : numColor?
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  input->seek(58, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PixelPaintParser::readColorMap(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+2048)) {
    MWAW_DEBUG_MSG(("PixelPaintParser::readColorMap: file is too short\n"));
    return false;
  }
  if (onlyCheck) {
    input->seek(pos+2048, librevenge::RVNG_SEEK_SET);
    return true;
  }
  libmwaw::DebugStream f;
  f << "Entries(ColorMap):";
  m_state->m_colorList.resize(256);
  for (auto &color : m_state->m_colorList) {
    unsigned char c[4];
    for (auto &col : c) col=static_cast<unsigned char>(input->readULong(2)>>8);
    color=MWAWColor(c[1],c[2],c[3],static_cast<unsigned char>(255-c[0]));
    f << color << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool PixelPaintParser::readPatternMap(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+144*8)) {
    MWAW_DEBUG_MSG(("PixelPaintParser::readPatternMap: file is too short\n"));
    return false;
  }
  if (onlyCheck) {
    input->seek(pos+144*8, librevenge::RVNG_SEEK_SET);
    return true;
  }
  libmwaw::DebugStream f;
  f << "Entries(PatternMap):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<144; ++i) {
    pos=input->tell();
    f.str("");
    f << "PatternMap-" << i << ":";
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}


////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////
bool PixelPaintParser::sendBitmap()
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("PixelPaintParser::sendBitmap: can not find the listener\n"));
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

bool PixelPaintParser::readBitmapV1(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();

  libmwaw::DebugStream f;
  f << "Entries(Bitmap):";
  std::shared_ptr<MWAWPictBitmapIndexed> pict;
  int numColors=256;
  if (!onlyCheck) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    if (m_state->m_bitmapSize[0]<=0||m_state->m_bitmapSize[0]>1024 ||
        m_state->m_bitmapSize[1]<=0||m_state->m_bitmapSize[1]>1024) {
      MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV1: argh can not find the bitmap size\n"));
      return false;
    }
    if (m_state->m_colorList.empty()) {
      MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV1: argh can not find the color list\n"));
      return false;
    }
    pict.reset(new MWAWPictBitmapIndexed(m_state->m_bitmapSize));
    numColors=static_cast<int>(m_state->m_colorList.size());
    pict->setColors(m_state->m_colorList);
  }
  for (int i=0; i<16*1024; ++i) {
    pos=input->tell();
    auto sz=long(input->readULong(4));
    long endPos=pos+4+sz;
    if (sz<2 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    int row=i/16;
    int col=(i%16)*64;
    f.str("");
    f << "Bitmap[R" << row << "C" << col << "]:";
    int nPixel=0;
    while (input->tell()+2<=endPos) { // UnpackBits
      auto n=static_cast<int>(input->readULong(1));
      if (n>=0x81) {
        auto color=static_cast<int>(input->readULong(1));
        if (color>=numColors) {
          static bool first=true;
          if (first) {
            MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV1: find some bad index\n"));
            first=false;
          }
          f << "###id=" << color << ",";
          color=0;
        }
        for (int c=0; c<0x101-n; ++c) {
          if (!pict || row >= m_state->m_bitmapSize[1] || col >= m_state->m_bitmapSize[0])
            break;
          pict->set(col++, row, color);
        }
        nPixel+=0x101-n;
      }
      else { // checkme normally 0x80 is reserved and almost nobody used it (for ending the compression)
        if (input->tell()+n+1>endPos) {
          input->seek(-1, librevenge::RVNG_SEEK_CUR);
          break;
        }
        nPixel+=n+1;
        for (int c=0; c<=n; ++c) {
          auto color=static_cast<int>(input->readULong(1));
          if (color>=numColors) {
            static bool first=true;
            if (first) {
              MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV1: find some bad index\n"));
              first=false;
            }
            f << "###id=" << color << ",";
            color=0;
          }
          if (!pict || row >= m_state->m_bitmapSize[1] || col >= m_state->m_bitmapSize[0])
            continue;
          pict->set(col++, row, color);
        }
      }
    }
    f << nPixel;
    if (onlyCheck) {
      if (nPixel!=64) return false;
    }
    else {
      if (input->tell()!=endPos) {
        ascii().addDelimiter(input->tell(),'|');
        f << "###";
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  m_state->m_bitmap=pict;
  return true;
}

bool PixelPaintParser::readBitmapV2(bool onlyCheck)
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  auto dataSz=static_cast<int>(input->readULong(2));
  if (dataSz<136+2048 || !input->checkPosition(pos+dataSz))
    return false;
  libmwaw::DebugStream f;
  f << "Entries(Bitmap)[header]:";
  int val;
  for (int i=0; i<2; ++i) { // always 0
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int dim[4];
  for (int i=0; i<2; ++i) dim[i]=static_cast<int>(input->readULong(2));
  if (dim[0]<=0||dim[0]>1024 || dim[1]<=0 || dim[1]>1024)
    return false;
  m_state->m_bitmapSize=MWAWVec2i(dim[1], dim[0]);
  f << "sz=" << m_state->m_bitmapSize << ",";
  for (int i=0; i<5; ++i) { // always 0
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i+2 << "=" << val << ",";
  }
  for (int i=0; i<9; ++i) { // always 0
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "f" << i+7 << "=" << val << ",";
  }
  for (int i=0; i<5; ++i) {
    val=static_cast<int>(input->readLong(2));
    static int const expected[]= {0x11, 0x2ff, 0xc00, -1, -1};
    if (val!=expected[i])
      f << "g" << i << "=" << val << ",";
  }
  for (int i=0; i<4; ++i) { // always 0
    val=static_cast<int>(input->readULong(2));
    if (val)
      f << "g" << i+4 << "=" << val << ",";
  }
  if (!onlyCheck) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  pos=input->tell();
  f.str("");
  f << "Bitmap[headerA]:";
  for (int i=0; i<2; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val!=m_state->m_bitmapSize[i])
      f << "##dim" << i << "=" << val << ",";
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "dim" << i << "[low]=" << val << ",";
  }
  for (int i=0; i<9; ++i) {
    val=static_cast<int>(input->readLong(2));
    static int const expected[]= {0, 0, 1, 0xa, 0, 0, 0x400, 0x400, 0x98 };
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  val=static_cast<int>(input->readULong(2)); // 8248|8400
  if (val) f << "fl?=" << std::hex << val << std::dec << ",";
  for (int i=0; i<2; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) dim[i]=static_cast<int>(input->readULong(2));
  if (MWAWVec2i(dim[1], dim[0])!=m_state->m_bitmapSize)
    f << "sz2=" << MWAWVec2i(dim[1], dim[0]) << ",";

  for (int i=0; i<4; ++i) {
    val=static_cast<int>(input->readLong(2));
    if (val)
      f << "g" << i+2 << "=" << val << ",";
  }
  for (int i=0; i<15; ++i) {
    val=static_cast<int>(input->readLong(2));
    static int const expected[]= {0x48, 0, 0x48, 0, 0, 8, 1, 8, 0, 0, 0, 0x1f10, 0, 0, 0 };
    if (val!=expected[i])
      f << "g" << i+6 << "=" << val << ",";
  }
  for (int i=0; i<2; ++i) {  // fl2=bc5|7bb9|bccc, fl3=8000
    val=static_cast<int>(input->readULong(2));
    if (val) f << "fl" << i+1 << "=" << std::hex << val << std::dec << ",";
  }
  val=static_cast<int>(input->readULong(2));
  if (val!=255) f << "h0=" << val << ",";
  if (!onlyCheck) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  pos=input->tell();
  if (onlyCheck)
    input->seek(pos+2048, librevenge::RVNG_SEEK_SET);
  else {
    f.str("");
    f << "Bitmap[color]:";
    // CHECKME: does we want to use this color list or the main color list ?
    m_state->m_colorList.resize(256);
    for (auto &color : m_state->m_colorList) {
      val=static_cast<int>(input->readULong(2));
      unsigned char c[3];
      for (auto &col : c) col=static_cast<unsigned char>(input->readULong(2)>>8);
      color=MWAWColor(c[0],c[1],c[2]);
      f << color;
      if (val!=0x800)
        f << "[" << std::hex << val << std::dec << "],";
      else
        f << ",";
    }

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    input->seek(pos+2048, librevenge::RVNG_SEEK_SET);
  }

  pos=input->tell();
  if (!input->checkPosition(pos+18))
    return false;
  if (!onlyCheck) {
    f.str("");
    f << "Bitmap[headerB]:";
    for (int i=0; i<2; ++i) {
      for (auto &d : dim) d=static_cast<int>(input->readULong(2));
      if (dim[0]||dim[1]||m_state->m_bitmapSize!=MWAWVec2i(dim[3],dim[2]))
        f << "dim" << i << "=" << MWAWBox2i(MWAWVec2i(dim[1],dim[0]), MWAWVec2i(dim[3],dim[2])) << ",";
    }
    val=static_cast<int>(input->readLong(2)); // always 0 ?
    if (val) f << "f0=" << val << ",";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  input->seek(pos+18, librevenge::RVNG_SEEK_SET);
  int numColors=0;
  std::shared_ptr<MWAWPictBitmapIndexed> pict;
  if (!onlyCheck) {
    ascii().addPos(input->tell());
    ascii().addNote(f.str().c_str());

    if (m_state->m_bitmapSize[0]<=0||m_state->m_bitmapSize[0]>1024 ||
        m_state->m_bitmapSize[1]<=0||m_state->m_bitmapSize[1]>1024) {
      MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV2: argh can not find the bitmap size\n"));
      return false;
    }
    if (m_state->m_colorList.empty()) {
      MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV2: argh can not find the color list\n"));
      return false;
    }
    pict.reset(new MWAWPictBitmapIndexed(m_state->m_bitmapSize));
    numColors=static_cast<int>(m_state->m_colorList.size());
    pict->setColors(m_state->m_colorList);
  }

  for (int row=0; row<m_state->m_bitmapSize[1]; ++row) {
    pos=input->tell();
    dataSz=static_cast<int>(input->readULong(2));
    long endPos=pos+2+dataSz;
    if (dataSz<2 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (onlyCheck) {
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      continue;
    }
    f.str("");
    f << "Bitmap[R" << row << "]:";
    int col=0, nPixel=0;
    while (input->tell()+2<=endPos) { // UnpackBits
      auto n=static_cast<int>(input->readULong(1));
      if (n>=0x81) {
        auto color=static_cast<int>(input->readULong(1));
        if (color>=numColors) {
          static bool first=true;
          if (first) {
            MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV2: find some bad index\n"));
            first=false;
          }
          f << "###id=" << color << ",";
          color=0;
        }
        for (int c=0; c<0x101-n; ++c) {
          if (!pict || col >= m_state->m_bitmapSize[0])
            break;
          pict->set(col++, row, color);
        }
        nPixel+=0x101-n;
      }
      else { // checkme normally 0x80 is reserved and almost nobody used it (for ending the compression)
        if (input->tell()+n+1>endPos) {
          input->seek(-1, librevenge::RVNG_SEEK_CUR);
          break;
        }
        nPixel+=n+1;
        for (int c=0; c<=n; ++c) {
          auto color=static_cast<int>(input->readULong(1));
          if (color>=numColors) {
            static bool first=true;
            if (first) {
              MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV2: find some bad index\n"));
              first=false;
            }
            f << "###id=" << color << ",";
            color=0;
          }
          if (!pict || col >= m_state->m_bitmapSize[0])
            continue;
          pict->set(col++, row, color);
        }
      }
    }
    if (nPixel<m_state->m_bitmapSize[0] || nPixel>m_state->m_bitmapSize[0]+32) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("PixelPaintParser::readBitmapV2: find row with odd number of pixel\n"));
        first=false;
      }
      f << "###numPixel=" << nPixel << ",";
    }
    if (input->tell()!=endPos) {
      ascii().addDelimiter(input->tell(),'|');
      f << "###";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  m_state->m_bitmap=pict;
  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool PixelPaintParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = PixelPaintParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(512))
    return false;
  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (input->readULong(2))
    return false;
  int vers=1;
  auto val=static_cast<int>(input->readULong(2));
  if (val==0x7fff)
    ;
  else if (val==0x8000)
    vers=2;
  else
    return false;
  if ((vers==1&&!readFileHeaderV1(true)) || (vers==2&&!readFileHeaderV2(true)))
    return false;
  if (!readColorMap(true) || !readPatternMap(true))
    return false;
  if (strict) {
    if ((vers==1 && !readBitmapV1(true)) || (vers==2 && !readBitmapV2(true)))
      return false;
  }
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_PIXELPAINT, vers, MWAWDocument::MWAW_K_PAINT);

  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
