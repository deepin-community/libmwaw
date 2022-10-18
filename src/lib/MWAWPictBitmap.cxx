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

/* This header contains code specific to some bitmap
 */

#include <sstream>
#include <string>
#ifdef USE_ZLIB
#  include <zlib.h>
#endif

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWPictBitmap.hxx"

//! Internal: namespace used to define some internal function
namespace MWAWPictBitmapInternal
{
#ifdef USE_ZLIB
//
// functions used by getPNGData
//

//! Internal: small function to store an unsigned value in big endian
static void writeBEU32(unsigned char *buffer, const unsigned value)
{
  *(buffer++) = static_cast<unsigned char>((value >> 24) & 0xFF);
  *(buffer++) = static_cast<unsigned char>((value >> 16) & 0xFF);
  *(buffer++) = static_cast<unsigned char>((value >> 8) & 0xFF);
  *(buffer++) = static_cast<unsigned char>(value & 0xFF);
}

//! Internal: add a chunk zone in a PNG file
static void addChunkInPNG(unsigned chunkType, unsigned char const *buffer, unsigned length, librevenge::RVNGBinaryData &data)
{
  unsigned char buf4[4];
  writeBEU32(buf4, length);
  data.append(buf4, 4); // add length
  writeBEU32(buf4, chunkType);
  data.append(buf4, 4); // add type
  unsigned crc=unsigned(crc32(0, buf4, 4));
  if (length) {
    data.append(buffer, length); // add data
    writeBEU32(buf4, unsigned(crc32(crc,buffer, length))); // add crc
  }
  else
    writeBEU32(buf4, crc); // add crc
  data.append(buf4, 4);
}

/** Internal: helper function to create a PNG knowing the ihdr, image zone
    and the palette zone(indexed bitmap)
 */
static bool createPNGFile(unsigned char const *ihdr, unsigned ihdrSize,
                          unsigned char const *image, unsigned imageSize,
                          unsigned char const *palette, unsigned paletteSize,
                          librevenge::RVNGBinaryData &data)
{
  unsigned char const signature[] = {
    /* PNG signature */
    0x89, 0x50, 0x4e, 0x47,
    0x0d, 0x0a, 0x1a, 0x0a
  };
  data.append(signature, MWAW_N_ELEMENTS(signature));

  if (ihdr && ihdrSize)
    addChunkInPNG(0x49484452 /* IHDR*/, ihdr, ihdrSize, data);
  if (palette && paletteSize)
    addChunkInPNG(0x504C5445 /*PLTE*/, palette, paletteSize, data);
  // now compress the picture
  const unsigned tmpBufSize = 128 * 1024;
  std::unique_ptr<unsigned char[]> tmpBuffer{new unsigned char[tmpBufSize]};
  std::vector<unsigned char> idatBuffer;

  z_stream strm;
  strm.zalloc = nullptr;
  strm.zfree = nullptr;
  strm.next_in = const_cast<unsigned char *>(image);
  strm.avail_in = imageSize;
  strm.next_out = tmpBuffer.get();
  strm.avail_out = tmpBufSize;
  strm.total_in = strm.total_out=0;

  if (deflateInit(&strm, Z_RLE)!=Z_OK) return false;
  while (strm.avail_in != 0) {
    if (deflate(&strm, Z_NO_FLUSH)!=Z_OK) return false;
    if (strm.avail_out == 0) {
      idatBuffer.insert(idatBuffer.end(), tmpBuffer.get(), tmpBuffer.get() + tmpBufSize);
      strm.next_out = tmpBuffer.get();
      strm.avail_out = tmpBufSize;
    }
  }
  while (deflate(&strm, Z_FINISH)==Z_OK) {
    if (strm.avail_out == 0) {
      idatBuffer.insert(idatBuffer.end(), tmpBuffer.get(), tmpBuffer.get() + tmpBufSize);
      strm.next_out = tmpBuffer.get();
      strm.avail_out = tmpBufSize;
    }
  }
  idatBuffer.insert(idatBuffer.end(), tmpBuffer.get(), tmpBuffer.get() + tmpBufSize - strm.avail_out);
  deflateEnd(&strm);

  addChunkInPNG(0x49444154/*IDAT*/, idatBuffer.data(), unsigned(idatBuffer.size()), data);
  addChunkInPNG(0x49454e44 /*IEND*/, nullptr, 0, data);
  return true;
}

//! Internal: fonction to create a PNG image from a true color bitmap (need zlib)
static bool getPNGData(MWAWPictBitmapContainer<MWAWColor> const &orig, librevenge::RVNGBinaryData &data, bool useAlpha)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[0] > 10000 || sz[1] <= 0) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapInternal:getPNGData: the bitmap size seems bad\n"));
    return false;
  }

  unsigned char ihdr[] = {
    /* IHDR -- Image header */
    0, 0, 0, 0,           // width
    0, 0, 0, 0,           // height
    8,                    // bit depth
    static_cast<unsigned char>(useAlpha ? 6 : 2),     // 2: rgb, 6: rgba
    0,                    // compression method: 0=deflate
    0,                    // filter method: 0=adaptative
    0                     // interlace method: 0=none
  };
  writeBEU32(ihdr, unsigned(sz[0]));
  writeBEU32(ihdr+4, unsigned(sz[1]));

  unsigned const numBytes=useAlpha ? 4 : 3;
  unsigned const lineWidth=1+unsigned(sz[0])*numBytes;
  unsigned const imageSize=lineWidth*unsigned(sz[1]);
  if (!lineWidth || imageSize/lineWidth<unsigned(sz[1])) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapInternal:getPNGData: the idat chunk is too big\n"));
    return false;
  }

  // create the image data
  std::unique_ptr<unsigned char[]> imageBuffer{new unsigned char[imageSize]};
  unsigned char *imagePtr=imageBuffer.get();
  for (int i = 0; i<sz[1]; ++i) {
    MWAWColor const *row = orig.getRow(i);
    *(imagePtr++) = 0; // 0: means none ; sometimes better with 1 but not always...
    for (int j = 0; j < sz[0]; j++) {
      uint32_t col = row[j].value();

      *(imagePtr++)=static_cast<unsigned char>((col>>16)&0xFF);
      *(imagePtr++)=static_cast<unsigned char>((col>>8)&0xFF);
      *(imagePtr++)=static_cast<unsigned char>(col&0xFF);
      if (useAlpha)
        *(imagePtr++)=static_cast<unsigned char>((col>>24)&0xFF);
    }
  }
  return createPNGFile(ihdr, unsigned(MWAW_N_ELEMENTS(ihdr)), imageBuffer.get(), imageSize, nullptr, 0, data);
}

//! Internal: fonction to create a PNG image from a indexed bitmap (need zlib)
template <class T>
bool getPNGData(MWAWPictBitmapContainer<T> const &orig, librevenge::RVNGBinaryData &data, std::vector<MWAWColor> const &indexedColor)
{
  MWAWVec2i sz = orig.size();
  auto nColors = int(indexedColor.size());
  if (sz[0] <= 0 || sz[1] <= 0 || nColors==0) return false;
  bool useIndex=nColors<=256;
  unsigned char ihdr[] = {
    /* IHDR -- Image header */
    0, 0, 0, 0,           // width
    0, 0, 0, 0,           // height
    8,                    // bit depth
    static_cast<unsigned char>(useIndex ? 3 : 2),     // 2: rgb, 3: indexed
    0,                    // compression method: 0=deflate
    0,                    // filter method: 0=adaptative
    0                     // interlace method: 0=none
  };
  writeBEU32(ihdr, unsigned(sz[0]));
  writeBEU32(ihdr+4, unsigned(sz[1]));

  unsigned const numBytes=(useIndex ? 1 : 3);
  unsigned const lineWidth=1+unsigned(sz[0])*numBytes;
  unsigned const imageSize=lineWidth*unsigned(sz[1]);
  if (!lineWidth || imageSize/lineWidth<unsigned(sz[1])) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapInternal:getPNGData: the idat chunk is too big\n"));
    return false;
  }

  // create the image data
  std::unique_ptr<unsigned char[]> imageBuffer{new unsigned char[imageSize]};
  unsigned char *imagePtr=imageBuffer.get();
  for (int i = 0; i<sz[1]; ++i) {
    T const *row = orig.getRow(i);
    *(imagePtr++) = 0; // 0: means none, using sub filtering seems counterproductif...
    for (int j = 0; j < sz[0]; j++) {
      int ind = row[j];
      if (ind < 0 || ind >= nColors) {
        MWAW_DEBUG_MSG(("MWAWPictBitmapInternal::getPNGData invalid index %d\n", ind));
        return false;
      }
      if (useIndex)
        *(imagePtr++)=static_cast<unsigned char>(ind);
      else {
        uint32_t col = indexedColor[size_t(ind)].value();

        *(imagePtr++)=static_cast<unsigned char>((col>>16)&0xFF);
        *(imagePtr++)=static_cast<unsigned char>((col>>8)&0xFF);
        *(imagePtr++)=static_cast<unsigned char>(col&0xFF);
      }
    }
  }
  if (!useIndex)
    return createPNGFile(ihdr, unsigned(MWAW_N_ELEMENTS(ihdr)), imageBuffer.get(), imageSize, nullptr, 0, data);
  std::unique_ptr<unsigned char[]> paletteBuffer{new unsigned char[3*unsigned(nColors)]};
  unsigned char *palettePtr=paletteBuffer.get();
  for (auto const &color : indexedColor) {
    uint32_t col = color.value();
    *(palettePtr++)=static_cast<unsigned char>((col>>16)&0xFF);
    *(palettePtr++)=static_cast<unsigned char>((col>>8)&0xFF);
    *(palettePtr++)=static_cast<unsigned char>(col&0xFF);
  }
  return createPNGFile(ihdr, unsigned(MWAW_N_ELEMENTS(ihdr)), imageBuffer.get(), imageSize, paletteBuffer.get(), 3*unsigned(nColors), data);
}

//! Internal: helper function to create a 2 color PNG file
template <class T>
bool getPNG1Data(MWAWPictBitmapContainer<T> const &orig, librevenge::RVNGBinaryData &data, T white)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[1] <= 0) return false;

  unsigned char ihdr[] = {
    /* IHDR -- Image header */
    0, 0, 0, 0,           // width
    0, 0, 0, 0,           // height
    1,                    // bit depth
    3,										// 3: indexed
    0,                    // compression method: 0=deflate
    0,                    // filter method: 0=adaptative
    0                     // interlace method: 0=none
  };
  writeBEU32(ihdr, unsigned(sz[0]));
  writeBEU32(ihdr+4, unsigned(sz[1]));

  unsigned const lineWidth=1+unsigned((sz[0]+7)/8);
  unsigned const imageSize=lineWidth*unsigned(sz[1]);
  if (!lineWidth || imageSize/lineWidth<unsigned(sz[1])) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapInternal:getPNG1Data: the idat chunk is too big\n"));
    return false;
  }

  // create the image data
  std::unique_ptr<unsigned char[]> imageBuffer{new unsigned char[imageSize]};
  unsigned char *imagePtr=imageBuffer.get();

  for (int j = 0; j < sz[1]; j++) {
    T const *row = orig.getRow(j);
    *(imagePtr++) = 0; // 0: means none

    unsigned char mask = 0x80, value = 0;
    for (int i = 0; i < sz[0]; i++) {
      if (row[i] != white) value |= mask;
      mask = static_cast<unsigned char>(mask >> 1);
      if (mask != 0) continue;
      *(imagePtr++) = value;
      value = 0;
      mask = 0x80;
    }
    if (mask!= 0x80) *(imagePtr++) = value;
  }
  // create a black and white palette
  unsigned const nColors=2;
  std::unique_ptr<unsigned char[]> paletteBuffer{new unsigned char[3*unsigned(nColors)]};
  unsigned char *palettePtr=paletteBuffer.get();
  for (int i=0; i<2; ++i) {
    uint32_t col = i==1 ? 0 : 0xffffff;
    *(palettePtr++)=static_cast<unsigned char>((col>>16)&0xFF);
    *(palettePtr++)=static_cast<unsigned char>((col>>8)&0xFF);
    *(palettePtr++)=static_cast<unsigned char>(col&0xFF);
  }
  return createPNGFile(ihdr, unsigned(MWAW_N_ELEMENTS(ihdr)), imageBuffer.get(), imageSize, paletteBuffer.get(), 3*unsigned(nColors), data);
}
#else
//! Internal: helper function to create a PBM
template <class T>
bool getPBMData(MWAWPictBitmapContainer<T> const &orig, librevenge::RVNGBinaryData &data, T white)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[1] <= 0) return false;

  data.clear();
  std::stringstream f;
  f << "P4\n" << sz[0] << " " << sz[1] << "\n";
  std::string const &header = f.str();
  data.append(reinterpret_cast<const unsigned char *>(header.c_str()), header.size());

  for (int j = 0; j < sz[1]; j++) {
    T const *row = orig.getRow(j);

    unsigned char mask = 0x80, value = 0;
    for (int i = 0; i < sz[0]; i++) {
      if (row[i] != white) value |= mask;
      mask = static_cast<unsigned char>(mask >> 1);
      if (mask != 0) continue;
      data.append(value);
      value = 0;
      mask = 0x80;
    }
    if (mask!= 0x80) data.append(value);
  }
  return true;
}

//! Internal: helper function to create a PPM
template <class T>
bool getPPMData(MWAWPictBitmapContainer<T> const &orig, librevenge::RVNGBinaryData &data, std::vector<MWAWColor> const &indexedColor)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[1] <= 0) return false;

  auto nColors = int(indexedColor.size());

  data.clear();
  std::stringstream f;
  f << "P6\n" << sz[0] << " " << sz[1] << " 255\n";
  std::string const &header = f.str();
  data.append(reinterpret_cast<const unsigned char *>(header.c_str()), header.size());
  std::vector<unsigned char> buf;
  buf.reserve(size_t(long(sz[1]) * sz[0] * 3));
  for (int j = 0; j < sz[1]; j++) {
    T const *row = orig.getRow(j);

    for (int i = 0; i < sz[0]; i++) {
      int ind = row[i];
      if (ind < 0 || ind >= nColors) {
        MWAW_DEBUG_MSG(("MWAWPictBitmapInternal::getPPMData invalid index %d\n", ind));
        if (!buf.empty())
          data.append(&buf[0], buf.size());
        return false;
      }
      uint32_t col = indexedColor[size_t(ind)].value();
      for (int c = 0, depl=16; c < 3; c++, depl-=8)
        buf.push_back(static_cast<unsigned char>((col>>depl)&0xFF));
    }
  }
  if (!buf.empty())
    data.append(&buf[0], buf.size());
  return true;
}

//! Internal: helper function to create a PPM for a color bitmap
static bool getPPMData(MWAWPictBitmapContainer<MWAWColor> const &orig, librevenge::RVNGBinaryData &data)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[1] <= 0) return false;

  data.clear();
  std::stringstream f;
  f << "P6\n" << sz[0] << " " << sz[1] << " 255\n";
  std::string const &header = f.str();
  data.append(reinterpret_cast<const unsigned char *>(header.c_str()), header.size());
  for (int j = 0; j < sz[1]; j++) {
    MWAWColor const *row = orig.getRow(j);

    for (int i = 0; i < sz[0]; i++) {
      uint32_t col = row[i].value();
      for (int c = 0, depl=16; c < 3; c++, depl-=8)
        data.append(static_cast<unsigned char>((col>>depl)&0xFF));
    }
  }
  return true;
}

//
// functions used by getPBMData
//
static void writeU16(unsigned char *buffer, unsigned &position, const unsigned value)
{
  buffer[position++] = static_cast<unsigned char>(value & 0xFF);
  buffer[position++] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

static void writeU32(unsigned char *buffer, unsigned &position, const unsigned value)
{
  buffer[position++] = static_cast<unsigned char>(value & 0xFF);
  buffer[position++] = static_cast<unsigned char>((value >> 8) & 0xFF);
  buffer[position++] = static_cast<unsigned char>((value >> 16) & 0xFF);
  buffer[position++] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

//! Internal: helper function to create a BMP for a color bitmap (freely inspired from libpwg::WPGBitmap.cpp)
static bool getBMPData(MWAWPictBitmapContainer<MWAWColor> const &orig, librevenge::RVNGBinaryData &data)
{
  MWAWVec2i sz = orig.size();
  if (sz[0] <= 0 || sz[1] <= 0) return false;

  auto tmpPixelSize = static_cast<unsigned>(sz[0]*sz[1]);
  unsigned tmpBufferPosition = 0;

  unsigned tmpDIBImageSize = tmpPixelSize * 4;
  if (tmpPixelSize > tmpDIBImageSize) // overflow !!!
    return false;

  unsigned const headerSize=56;
  unsigned tmpDIBOffsetBits = 14 + headerSize;
  unsigned tmpDIBFileSize = tmpDIBOffsetBits + tmpDIBImageSize;
  if (tmpDIBImageSize > tmpDIBFileSize) // overflow !!!
    return false;

  std::unique_ptr<unsigned char[]> tmpDIBBuffer{new unsigned char[tmpDIBFileSize]};
  if (!tmpDIBBuffer) {
    MWAW_DEBUG_MSG(("getBMPData: fail to allocated the data buffer\n"));
    return false;
  }
  // Create DIB file header
  writeU16(tmpDIBBuffer.get(), tmpBufferPosition, 0x4D42);  // Type
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, static_cast<unsigned>(tmpDIBFileSize)); // Size
  writeU16(tmpDIBBuffer.get(), tmpBufferPosition, 0); // Reserved1
  writeU16(tmpDIBBuffer.get(), tmpBufferPosition, 0); // Reserved2
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, static_cast<unsigned>(tmpDIBOffsetBits)); // OffsetBits

  // Create DIB Info header
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, headerSize); // Size
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, static_cast<unsigned>(sz[0]));  // Width
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, static_cast<unsigned>(sz[1])); // Height
  writeU16(tmpDIBBuffer.get(), tmpBufferPosition, 1); // Planes
  writeU16(tmpDIBBuffer.get(), tmpBufferPosition, 32); // BitCount
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0); // Compression
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, static_cast<unsigned>(tmpDIBImageSize)); // SizeImage
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 5904); // XPelsPerMeter: 300ppi
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 5904); // YPelsPerMeter: 300ppi
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0); // ColorsUsed
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0); // ColorsImportant

  // Create DIB V3 Info header

  /* this is needed to create alpha picture ; but as both LibreOffice/OpenOffice ignore the alpha channel... */
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0x00FF0000); /* biRedMask */
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0x0000FF00); /* biGreenMask */
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0x000000FF); /* biBlueMask */
  writeU32(tmpDIBBuffer.get(), tmpBufferPosition, 0xFF000000); /* biAlphaMask */

  // Write DIB Image data
  for (int i = sz[1] - 1; i >= 0 && tmpBufferPosition < tmpDIBFileSize; i--) {
    MWAWColor const *row = orig.getRow(i);

    for (int j = 0; j < sz[0] && tmpBufferPosition < tmpDIBFileSize; j++) {
      uint32_t col = row[j].value();

      tmpDIBBuffer.get()[tmpBufferPosition++]=static_cast<unsigned char>(col&0xFF);
      tmpDIBBuffer.get()[tmpBufferPosition++]=static_cast<unsigned char>((col>>8)&0xFF);
      tmpDIBBuffer.get()[tmpBufferPosition++]=static_cast<unsigned char>((col>>16)&0xFF);
      tmpDIBBuffer.get()[tmpBufferPosition++]=static_cast<unsigned char>((col>>24)&0xFF);
    }
  }
  data.clear();
  data.append(tmpDIBBuffer.get(), tmpDIBFileSize);

  return true;
}
#endif
}

MWAWPictBitmapContainerBool::~MWAWPictBitmapContainerBool()
{
}

MWAWPictBitmap::~MWAWPictBitmap()
{
}
////////////////////////////////////////////////////////////
// BW bitmap
////////////////////////////////////////////////////////////

bool MWAWPictBitmapBW::createFileData(librevenge::RVNGBinaryData &result) const
{
#ifdef USE_ZLIB
  return MWAWPictBitmapInternal::getPNG1Data<bool>(m_data,result,false);
#else
  return MWAWPictBitmapInternal::getPBMData<bool>(m_data,result,false);
#endif
}

MWAWColor MWAWPictBitmapBW::getAverageColor() const
{
  auto const &sz=m_data.size();
  if (sz[0] <= 0 || sz[1] <= 0) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapBW::getAverageColor: called on empty picture\n"));
    return MWAWColor::black();
  }
  unsigned long n=0;
  for (int j = 0; j < sz[1]; j++) {
    bool const *row = getRow(j);
    for (int i = 0; i < sz[0]; i++)
      if (!row[i]) ++n;
  }
  unsigned char c=(unsigned char)((255*n)/(unsigned long)(sz[0]*sz[1]));
  return MWAWColor(c,c,c);
}

////////////////////////////////////////////////////////////
// Color bitmap
////////////////////////////////////////////////////////////

bool MWAWPictBitmapColor::createFileData(librevenge::RVNGBinaryData &result) const
{
#ifdef USE_ZLIB
  return MWAWPictBitmapInternal::getPNGData(m_data,result,m_hasAlpha);
#else
  if (m_hasAlpha) return MWAWPictBitmapInternal::getBMPData(m_data,result);
  return MWAWPictBitmapInternal::getPPMData(m_data,result);
#endif
}

MWAWColor MWAWPictBitmapColor::getAverageColor() const
{
  auto const &sz=m_data.size();
  if (sz[0] <= 0 || sz[1] <= 0) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapColor::getAverageColor: called on empty picture\n"));
    return MWAWColor::black();
  }
  unsigned long n[]= {0,0,0,0};
  for (int j = 0; j < sz[1]; j++) {
    MWAWColor const *row = getRow(j);
    for (int i = 0; i < sz[0]; i++) {
      n[0]+=row[i].getRed();
      n[1]+=row[i].getGreen();
      n[2]+=row[i].getBlue();
      n[3]+=row[i].getAlpha();
    }
  }
  return MWAWColor((unsigned char)(n[0]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[1]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[2]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[3]/(unsigned long)(sz[0]*sz[1])));
}

////////////////////////////////////////////////////////////
// Indexed bitmap
////////////////////////////////////////////////////////////

bool MWAWPictBitmapIndexed::createFileData(librevenge::RVNGBinaryData &result) const
{
#ifdef USE_ZLIB
  if (m_colors.size() && MWAWPictBitmapInternal::getPNGData<int>(m_data,result,m_colors)) return true;
  return MWAWPictBitmapInternal::getPNG1Data<int>(m_data,result,0);
#else
  if (m_colors.size() && MWAWPictBitmapInternal::getPPMData<int>(m_data,result,m_colors)) return true;
  return MWAWPictBitmapInternal::getPBMData<int>(m_data,result,0);
#endif
}

MWAWColor MWAWPictBitmapIndexed::getAverageColor() const
{
  auto const &sz=m_data.size();
  if (sz[0] <= 0 || sz[1] <= 0) {
    MWAW_DEBUG_MSG(("MWAWPictBitmapIndexed::getAverageColor: called on empty picture\n"));
    return MWAWColor::black();
  }
  unsigned long n[]= {0,0,0,0};
  size_t nCol=m_colors.size();
  for (int j = 0; j < sz[1]; j++) {
    int const *row = getRow(j);
    for (int i = 0; i < sz[0]; i++) {
      size_t const id=size_t(row[i]);
      if (id >= nCol) continue;
      n[0]+=m_colors[id].getRed();
      n[1]+=m_colors[id].getGreen();
      n[2]+=m_colors[id].getBlue();
      n[3]+=m_colors[id].getAlpha();
    }
  }
  return MWAWColor((unsigned char)(n[0]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[1]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[2]/(unsigned long)(sz[0]*sz[1])),
                   (unsigned char)(n[3]/(unsigned long)(sz[0]*sz[1])));
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
