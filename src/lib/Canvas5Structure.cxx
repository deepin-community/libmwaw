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
#include <set>
#include <vector>

#include "MWAWStringStream.hxx"
#include "MWAWPictBitmap.hxx"

#include "Canvas5Structure.hxx"

namespace Canvas5Structure
{

std::string getString(unsigned val)
{
  if (val<20) return std::to_string(val);
  std::string res;
  for (int dec=24; dec>=0; dec-=8) {
    char c=char((val>>dec)&0xff);
    if (!std::isprint(c))
      return std::to_string(val);
    res+=c;
  }
  return res;
}

bool readBitmap(Stream &stream, int version, MWAWEmbeddedObject &object, MWAWColor *avgColor)
{
  object=MWAWEmbeddedObject();
  auto input=stream.input();
  long pos=input->tell();
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(Bitmap):";
  int type0=int(input->readULong(4)); // found type0=5 in texture bw bitmap
  if (type0!=6) f << "type0=" << type0 << ",";
  if (!input->checkPosition(pos+64) || (type0!=5 && type0!=6)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: the zone beginning seems bad\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int type=int(input->readLong(2)); // 1-3
  switch (type) {
  case 0:
    f << "bw[indexed],"; // 1 bool by bytes
    break;
  case 1:
    f << "bw[color],"; // 1 plane
    break;
  case 2:
    f << "indexed,"; // 1 plane, color map
    break;
  case 3:
    f << "color,"; // with 3 planes
    break;
  case 4:
    f << "color4,"; // with 4 planes
    break;
  default:
    f << "##type=" << type << ",";
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: unexpected type\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int numBytes=int(input->readLong(2)); // number of byte?
  if (numBytes!=8) {
    if (numBytes==1 && type==0)
      f << "num[bytes]=1,";
    else {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: oops, find a number of bytes unexpected, unimplemented\n"));
      f << "##num[bytes]=" << numBytes << ",";
    }
  }
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(4));
  MWAWVec2i dimension(dim[1], dim[0]);
  f << "dim=" << dimension << ",";
  int numPlanes=int(input->readLong(2)); // 1-4
  int val=int(input->readLong(2)); // val
  if (numPlanes!=val)
    f << "num[planes]=" << numPlanes << "x" << val << ",";
  else if (numPlanes!=1) f << "f2=" << val << ",";
  float fDim[2];
  for (auto &v : fDim) v=float(input->readULong(4))/65536.f;
  if (MWAWVec2f(fDim[0],fDim[1])!=MWAWVec2f(72,72))
    f << "fDim=" << MWAWVec2f(fDim[0],fDim[1]) << ",";
  for (int i=0; i<4; ++i) { // 0
    val=int(input->readLong(2));
    if (val)
      f << "f" << i+3 << "=" << val << ",";
  }
  for (auto &d : dim) d=int(input->readULong(4));
  MWAWVec2i dim1(dim[1], dim[0]);
  if (dimension!=dim1)
    f << "dim1=" << dim1 << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  // FIXME: find correctly the data, color positions
  //   but only reconstruct correctly small bitmaps :-~
  std::shared_ptr<MWAWPictBitmapIndexed> bitmapIndexed;
  std::shared_ptr<MWAWPictBitmapColor> bitmapColor;
  switch (type) {
  case 0:
  case 2:
    bitmapIndexed.reset(new MWAWPictBitmapIndexed(dimension));
    break;
  case 1:
  case 3:
  case 4:
  default:
    bitmapColor.reset(new MWAWPictBitmapColor(dimension));
    break;
  }

  pos=input->tell();
  int const width=type==0 ? (dimension[0]+7)/8 : dimension[0];
  int const planeHeaderLength=(version<9 ? 20 : 40);
  long dataLength=((type==3||type==4) ? numPlanes : 1)*(planeHeaderLength+width*dimension[1]);
  if (width<=0 || dimension[1]<=0 || pos+dataLength<pos || !input->checkPosition(pos+dataLength)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: can not find the bitmap data\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  long dataPos=pos;
  // first read the color map
  input->seek(pos+dataLength, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  long len=input->readLong(4);
  if (pos+4+(len?4:0)+len<pos+4 || !input->checkPosition(pos+4+(len?4:0)+len)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: can not find the color block\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Bitmap[color]:###");
    return false;
  }
  if (len==0) {
    ascFile.addPos(pos);
    ascFile.addNote("_");
  }
  else {
    input->seek(4, librevenge::RVNG_SEEK_CUR);
    unsigned long numBytesRead;
    auto *data=input->read(size_t(len), numBytesRead);
    if (!data || long(numBytesRead)!=len) {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmap: can not find the color block\n"));
      ascFile.addPos(pos);
      ascFile.addNote("Bitmap[color]:###");
      return false;
    }
    size_t N=size_t(len/3);
    std::vector<MWAWColor> colors(N);
    for (size_t c=0; c<N; ++c) colors[c]=MWAWColor(data[c],data[c+N],data[c+2*N]);
    if (type==2)
      bitmapIndexed->setColors(colors);
    ascFile.addPos(pos);
    ascFile.addNote("Bitmap[color]:");
    ascFile.skipZone(pos+8, pos+8+len-1);
  }
  long endPos=input->tell();
  if (type==0)
    bitmapIndexed->setColors({MWAWColor::black(), MWAWColor::white()});
  // now read the bitmap data
  input->seek(dataPos, librevenge::RVNG_SEEK_SET);
  for (int plane=0; plane<((type==3||type==4) ? numPlanes : 1); ++plane) {
    pos=input->tell();
    f.str("");
    f << "Bitmap-P" << plane << ":";
    for (int i=0; i<3; ++i) {
      val=int(input->readLong(4));
      int const expected[]= {2 /* or 3*/,8,1};
      if (val==expected[i]) continue;
      if (i==1)
        f << "num[bytes]=" << val << ",";
      else
        f << "g" << i << "=" << val << ",";
    }
    for (auto &d : dim) d=int(input->readULong(4));
    dim1=MWAWVec2i(dim[1], dim[0]);
    if (dimension!=dim1)
      f << "dim2=" << dim1 << ",";
    input->seek(pos+planeHeaderLength, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    if (type==0) {
      // checkme: is the picture decomposed by block if dim[0]>128*8 or dim[1]>128 ?
      for (int y=0; y<dimension[1]; ++y) {
        int x=0;
        for (int w=0; w<width; ++w) {
          val=int(input->readULong(1));
          for (int v=0, depl=0x80; v<8; ++v, depl>>=1) {
            if (x>=dimension[0])
              break;
            bitmapIndexed->set(x++,y, (val&depl) ? 0 : 1);
          }
        }
      }
    }
    else {
      for (int nY=0; nY<(dimension[1]+127)/128; ++nY) {
        for (int nW=0; nW<(dimension[0]+127)/128; ++nW) {
          for (int y=128*nY; y<std::min(dimension[1], 128*(nY+1)); ++y) {
            for (int w=128*nW; w<std::min(dimension[0], 128*(nW+1)); ++w) {
              unsigned char c=(unsigned char)input->readULong(1);
              if (type==1)
                bitmapColor->set(w,y, MWAWColor(c,c,c));
              else if (type==2)
                bitmapIndexed->set(w,y,c);
              else {
                if (plane==0)
                  bitmapColor->set(w,y,MWAWColor(c,0,0));
                else {
                  int const decal=plane==3 ? 24 : (16-(8*plane));
                  uint32_t finalValue=bitmapColor->get(w,y).value()|(uint32_t(c)<<decal);
                  bitmapColor->set(w,y,MWAWColor(finalValue));
                }
              }
            }
          }
        }
      }
    }
    ascFile.skipZone(dataPos+20, dataPos+dataLength-1);
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  bool ok=false;
  if (type==0 || type==2) {
    ok=bitmapIndexed->getBinary(object);
    if (ok && avgColor) *avgColor=bitmapIndexed->getAverageColor();
  }
  else if (type==1 || type==3 || type==4) {
    ok=bitmapColor->getBinary(object);
    if (ok && avgColor) *avgColor=bitmapColor->getAverageColor();
  }
#ifdef DEBUG_WITH_FILES
  if (ok && !object.m_dataList.empty()) {
    std::stringstream s;
    static int index=0;
    s << "file" << ++index << ".png";
    libmwaw::Debug::dumpFile(object.m_dataList[0], s.str().c_str());
  }
#endif
  return ok && !object.m_dataList.empty();
}

bool readBitmapDAD58Bim(Stream &stream, int version, MWAWEmbeddedObject &object)
{
  if (!readBitmap(stream, version, object))
    return false;

  auto input=stream.input();
  long pos=input->tell();
  auto &ascFile=stream.ascii();

  // DAD5 block
  libmwaw::DebugStream f;
  f << "Bitmap[DAD5]:";
  if (!input->checkPosition(pos+12)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: can not find the DAD5 block\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  int val=int(input->readLong(4));
  if (val!=1) // checkme: val=0 means probably no data
    f << "f0=" << val << ",";
  f << "len?=" << std::hex << input->readULong(4) << std::dec << ",";
  int N=int(input->readULong(4)); // 1-3
  f << "N=" << N << ",";
  if (N<0 || (input->size()-pos-12)/16<N || pos+12+16*N<pos+12 || !input->checkPosition(pos+12+16*N)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5]: can not find the number of subblock\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  for (int j=0; j<N; ++j) {
    pos=input->tell();
    f.str("");
    f << "Bitmap[DAD5-A" << j << "]:";
    if (!input->checkPosition(pos+16)) {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5]: can not read subblock %d\n", j));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    unsigned types[2];
    for (auto &t : types) t=unsigned(input->readULong(4));
    f << getString(types[0]) << ":"  << getString(types[1]) << ",";
    val=int(input->readLong(4));
    if (val!=1) f << "f0=" << val << ",";
    long len=input->readLong(4);
    if (len<0 || pos+16+len<pos+16 || !input->checkPosition(pos+16+len)) {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5]: can not read subblock %d length\n", j));
      f << "###len=" << len << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      return false;
    }
    // DAD5::VISM (size 8), DAD5::hack (size 8c) or DAD5::1 (size variable, ie end with a string)
    if (types[0]==0x44414435) {
      switch (types[1]) {
      case 1: {
        std::string name;
        for (int k=0; k<len; ++k) {
          char c=char(input->readULong(1));
          if (c==0)
            break;
          name+=c;
        }
        f << "path=" << name << ",";
        break;
      }
      case 0x6861636b: {
        if (len!=0x8c) {
          MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5,hack]: unexpected length\n"));
          f << "###";
          break;
        }
        for (int k=0; k<2; ++k) { // 0
          val=int(input->readLong(4));
          if (val)
            f << "f" << k+1 << "=" << val << ",";
        }
        int maxN=int(input->readLong(4));
        f << "maxN=" << maxN << ",";
        f << "unkn=[";
        for (int k=0; k<32; ++k) { // unsure where to stop
          val=int(input->readLong(4));
          if (val<=0 || val>maxN) break;
          f << val << ",";
        }
        f << "],";
        ascFile.addDelimiter(input->tell(),'|');
        break;
      }
      case 0x5649534d: // VISM
        if (len!=8) {
          MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5,VISM]: unexpected length\n"));
          f << "###";
          break;
        }
        val=int(input->readLong(4)); // 0-1
        if (val)
          f << "f1=" << val << ",";
        val=int(input->readLong(4));
        if (val!=-1)
          f << "f2=" << val << ",";
        break;
      default:
        MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5]: unexpected type for sub zone\n"));
        f << "###";
        break;
      }
    }
    else {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[DAD5]: find unknown type0 for subblock %d\n", j));
      f << "###";
    }
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(pos+16+len, librevenge::RVNG_SEEK_SET);
  }

  // last block: 8BIM
  pos=input->tell();
  long len=input->readLong(4);
  f.str("");
  f << "Bitmap[8bim]:";
  long endBimBlock=pos+4+len;
  if (len<0 || endBimBlock<pos+4 || !input->checkPosition(endBimBlock)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: can not read 8bim block\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  while (input->tell()<endBimBlock) {
    pos=input->tell();
    f.str("");
    f << "Bitmap[8bim]:";
    if (pos+12>endBimBlock) {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: a 8bim block seems bad\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      break;
    }
    unsigned type=unsigned(input->readULong(4));
    f << getString(type) << ","; // 8BIM
    int id=int(input->readLong(2));
    f << "id=" << id << ",";
    val=int(input->readLong(2));
    if (val)
      f << "f0=" << val << ",";
    len=input->readLong(4);
    if (len<0 || pos+12+len>endBimBlock) {
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: a 8bim block len seems bad\n"));
      f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      break;
    }
    switch (type) {
    case 0x3842494d:
      switch (id) {
      case 1006:
        if (len==0)
          break;
        else {
          int sSz=int(input->readULong(1));
          if (1+sSz>len) {
            MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[8bim,1006]: can not find the string size\n"));
            f << "###";
            break;
          }
          std::string name;
          for (int k=0; k<sSz; ++k) {
            char c=char(input->readULong(1));
            if (!c)
              break;
            name+=c;
          }
          f << name << ",";
        }
        break;
      case 1007: {
        if ((len%14)!=0) {
          MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[8bim,1007]: the size seems bad\n"));
          f << "###";
          break;
        }
        int nUnkn=int(len/14);
        f << "unkn=[";
        for (int k=0; k<nUnkn; ++k) {
          f << "[";
          for (int l=0; l<7; ++l) {
            val=int(input->readLong(2));
            int const expected[]= {0,0/* or 255*/,0,0,0,50 /* or 100 */, 0};
            if (val!=expected[l])
              f << "f" << l << "=" << val << ",";
          }
          f << "],";
        }
        f << "],";
        break;
      }
      default:
        MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[8bim]: unknown id=%d\n", id));
        f << "###";
        break;
      }
      break;
    default:
      MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim[8bim]: unknown type=%s\n", getString(type).c_str()));
      f << "###";
    }
    if (input->tell()!=pos+12+len)
      ascFile.addDelimiter(input->tell(),'|');
    input->seek(pos+12+len, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  input->seek(endBimBlock, librevenge::RVNG_SEEK_SET);

  if (version<9)
    return true;

  if (input->isEnd()) // bitmap in cvi file ends here
    return true;
  // last block: unknown
  pos=input->tell();
  len=input->readLong(4);
  f.str("");
  f << "Bitmap[unknown]:";
  long endUnknownBlock=pos+4+len;
  if (len<0 || endUnknownBlock<pos+4 || !input->checkPosition(endUnknownBlock)) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: can not read unknown block\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  if (len) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readBitmapDAD58Bim: find an unknown block\n"));
    f << "###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    input->seek(endUnknownBlock, librevenge::RVNG_SEEK_SET);
  }
  else {
    ascFile.addPos(pos);
    ascFile.addNote("_");
  }
  return true;
}

bool readPreview(Stream &stream, bool hasPreviewBitmap)
{
  auto input=stream.input();
  if (!input) return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+12+(hasPreviewBitmap ? 12 : 0))) {
    MWAW_DEBUG_MSG(("Canvas5Structure::readPreview: the zone is too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  auto &ascFile=stream.ascii();
  f << "Entries(Preview):";
  int dims[3];
  for (auto &d : dims) d=int(input->readLong(4));
  f << "dim=" << MWAWVec2i(dims[1], dims[0]) << "[" << dims[2] << "],";
  int width=hasPreviewBitmap ? int(input->readLong(4)) : 0;
  if (width) f << "w=" << width << ",";
  long endPos=pos+(hasPreviewBitmap ? 24 : 12)+long(width*dims[0]);
  if (!hasPreviewBitmap || dims[0]<=0 || dims[1]<=0 || dims[2]!=3 || width<dims[1]*dims[2] ||
      endPos<=pos+24 || !input->checkPosition(endPos)) {
    if (dims[0]==0 && dims[1]==0 && input->checkPosition(endPos)) {
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
      ascFile.skipZone(input->tell(), endPos-1);
      return true;
    }
    f << "###";
    MWAW_DEBUG_MSG(("Canvas5Structure::readPreview: the dimensions seems bad\n"));
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  for (int i=0; i<2; ++i) {
    int val=int(input->readLong(4));
    int const expected[]= {3,1};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  MWAWPictBitmapColor pict(MWAWVec2i(dims[1], dims[0]), dims[2]==4);
  for (int y=0; y<dims[0]; ++y) {
    long actPos=input->tell();
    unsigned char cols[4]= {0,0,0,0};
    for (int w=0; w<dims[1]; ++w) {
      for (int c=0; c<dims[2]; ++c) cols[c]=(unsigned char)(input->readULong(1));
      if (dims[2]==4)
        pict.set(w, y, MWAWColor(cols[1], cols[2], cols[3], (unsigned char)(255-cols[0])));
      else
        pict.set(w, y, MWAWColor(cols[0], cols[1], cols[2]));
    }
    input->seek(actPos+width, librevenge::RVNG_SEEK_SET);
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascFile.skipZone(pos, endPos-1);
#ifdef DEBUG_WITH_FILES
  MWAWEmbeddedObject obj;
  if (pict.getBinary(obj) && !obj.m_dataList.empty())
    libmwaw::Debug::dumpFile(obj.m_dataList[0], "file.png");
#endif

  return true;
}

////////////////////////////////////////////////////////////
// decoder
////////////////////////////////////////////////////////////

//! a basic Unpack decoder
struct UnpackDecoder {
  //! constructor
  UnpackDecoder(unsigned char const *data, unsigned long len)
    : m_data(data)
    , m_len(len)

    , m_pos(0)
  {
  }

  bool decode(unsigned long expectedLength, std::vector<unsigned char> &output)
  {
    output.clear();
    output.reserve(expectedLength > 0x8000 ? 0x8000 : expectedLength);
    while (m_pos+2<=m_len) {
      unsigned num=unsigned(m_data[m_pos++]);
      unsigned char val=m_data[m_pos++];
      if (output.size()+num>expectedLength)
        return false;
      for (unsigned i=0; i<num; ++i)
        output.push_back(val);
    }
    return output.size()==expectedLength;
  }
protected:

  unsigned char const *m_data;
  unsigned long m_len;
  mutable unsigned long m_pos;
};

//! a basic NIB decoder
struct NIBDecoder {
  //! constructor
  NIBDecoder(unsigned char const *data, unsigned long len)
    : m_data(data)
    , m_len(len)

    , m_pos(0)
  {
  }

  bool decode(unsigned long expectedLength, std::vector<unsigned char> &output)
  {
    output.clear();
    output.reserve(expectedLength > 0x8000 ? 0x8000 : expectedLength);
    unsigned char dict[30];
    std::set<unsigned char> dictKeys;

    if (m_pos+30>m_len) {
      MWAW_DEBUG_MSG(("Canvas5Structure::NIBDecoder::can not read a dictionary at pos=%lx\n", m_pos));
      return false;
    }
    for (auto &c : dict) c=m_data[m_pos++];
    dictKeys.clear();
    for (auto &c : dict) dictKeys.insert(c);

    int newC=0;
    bool readC=false;
    unsigned char c;
    while (m_pos<=m_len) {
      bool ok=true;
      for (int st=0; st<4; ++st) {
        int val;
        if (!readC) {
          if (m_pos>m_len) {
            ok=false;
            break;
          }
          c=m_data[m_pos++];
          val=int(c>>4);
        }
        else
          val=int(c&0xf);
        readC=!readC;

        if (val && st<2) {
          output.push_back(dict[15*st+val-1]);
          break;
        }
        newC=(newC<<4)|val;
        if (st==3) {
          if (dictKeys.find((unsigned char) newC)!=dictKeys.end()) {
            ok=false;
            break;
          }
          output.push_back((unsigned char) newC);
          newC=0;
        }
      }
      if (!ok)
        break;
      if (m_pos+1>=m_len && output.size()==expectedLength)
        break;
    }
    return output.size()==expectedLength;
  }
protected:

  unsigned char const *m_data;
  unsigned long m_len;
  mutable unsigned long m_pos;
};

/** a basic LWZ decoder

    \note this code is freely inspired from https://github.com/MichaelDipperstein/lzw GLP 3
 */
struct LWZDecoder {
  static int const e_firstCode=(1<<8);
  static int const e_maxCodeLen=12;
  static int const e_maxCode=(1<<e_maxCodeLen);

  //! constructor
  LWZDecoder(unsigned char const *data, unsigned long len)
    : m_data(data)
    , m_len(len)

    , m_pos(0)
    , m_bit(0)
    , m_dictionary()
  {
    initDictionary();
  }

  bool decode(std::vector<unsigned char> &output);

protected:
  void initDictionary()
  {
    m_dictionary.resize(2); // 100 and 101
    m_dictionary.reserve(e_maxCode - e_firstCode); // max table 4000
  }

  unsigned getBit() const
  {
    if (m_pos>=m_len)
      throw libmwaw::ParseException();
    unsigned val=(m_data[m_pos]>>(7-m_bit++))&1;
    if (m_bit==8) {
      ++m_pos;
      m_bit=0;
    }
    return val;
  }
  unsigned getCodeWord(unsigned codeLen) const
  {
    unsigned code=0;
    for (unsigned i=0; i<codeLen;) {
      if (m_bit==0 && (codeLen-i)>=8 && m_pos<m_len) {
        code = (code<<8) | unsigned(m_data[m_pos++]);
        i+=8;
        continue;
      }
      code = (code<<1) | getBit();
      ++i;
    }
    return code;
  }

  struct LWZEntry {
    //! constructor
    LWZEntry(unsigned int prefixCode=0, unsigned char suffix=0)
      : m_suffix(suffix)
      , m_prefixCode(prefixCode)
    {
    }
    /** last char in encoded string */
    unsigned char m_suffix;
    /** code for remaining chars in string */
    unsigned int m_prefixCode;
  };

  unsigned char decodeRec(unsigned int code, std::vector<unsigned char> &output)
  {
    unsigned char c;
    unsigned char firstChar;

    if (code >= e_firstCode) {
      if (code-e_firstCode >= m_dictionary.size()) {
        MWAW_DEBUG_MSG(("Canvas5Structure::LWZDecoder::decodeRec: bad id=%x/%x\n", code, unsigned(m_dictionary.size())));
        throw libmwaw::ParseException();
      }
      /* code word is string + c */
      c = m_dictionary[code - e_firstCode].m_suffix;
      code = m_dictionary[code - e_firstCode].m_prefixCode;

      /* evaluate new code word for remaining string */
      firstChar = decodeRec(code, output);
    }
    else /* code word is just c */
      firstChar = c = (unsigned char)code;

    output.push_back(c);
    return firstChar;
  }
  LWZDecoder(LWZDecoder const &)=delete;
  LWZDecoder &operator=(LWZDecoder const &)=delete;
  unsigned char const *m_data;
  unsigned long m_len;
  mutable unsigned long m_pos, m_bit;

  std::vector<LWZEntry> m_dictionary;
};

bool LWZDecoder::decode(std::vector<unsigned char> &output)
try
{
  output.reserve(0x8000);

  unsigned int const currentCodeLen = 12;
  unsigned lastCode=0;
  unsigned char c=(unsigned char) 0;
  bool first=true;

  while (true) {
    unsigned code=getCodeWord(currentCodeLen);
    if (code==0x100) {
      initDictionary();
      first=true;
      continue;
    }
    if (code==0x101) // end of code
      break;
    if (code < e_firstCode+m_dictionary.size())
      /* we have a known code.  decode it */
      c = decodeRec(code, output);
    else {
      /***************************************************************
       * We got a code that's not in our dictionary.  This must be due
       * to the string + char + string + char + string exception.
       * Build the decoded string using the last character + the
       * string from the last code.
       ***************************************************************/
      unsigned char tmp = c;
      c = decodeRec(lastCode, output);
      output.push_back(tmp);
    }

    /* if room, add new code to the dictionary */
    if (!first && m_dictionary.size() < e_maxCode) {
      if (lastCode>=e_firstCode+m_dictionary.size()) {
        MWAW_DEBUG_MSG(("Canvas5Structure::LWZDecoder::decode: oops a loop with %x/%x\n", lastCode, unsigned(m_dictionary.size())));
        break;
      }
      m_dictionary.push_back(LWZEntry(lastCode, c));
    }

    /* save character and code for use in unknown code word case */
    lastCode = code;
    first=false;
  }
  return true;
}
catch (...)
{
  return false;
}


bool decodeZone5(MWAWInputStreamPtr input, long endPos, int type, unsigned long finalLength,
                 std::shared_ptr<MWAWStringStream> &stream)
{
  if (type<0 || type>8) {
    MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5: unknown type\n"));
    return false;
  }
  std::vector<unsigned long> lengths;
  lengths.push_back(finalLength);
  // checkme this code is only tested when type==0, 7, 8
  int const nExtraLength[]= {
    0, 0, 0, 0, 2, // _, _, Z, N, N+Z
    0, 0, 2, 3 // _, P, P+N, P+N+Z
  };
  long pos=input->tell();
  if (pos+4*nExtraLength[type]>endPos) {
    MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5: can not read the extra length\n"));
    return false;
  }
  bool readInverted=input->readInverted();
  input->setReadInverted(false);
  for (int n=0; n<nExtraLength[type]; ++n)
    lengths.push_back(input->readULong(4));
  if (lengths.size()==1)
    lengths.push_back((unsigned long)(endPos-pos));
  input->setReadInverted(readInverted);

  auto l=lengths.back();
  lengths.pop_back();
  for (size_t i=lengths.size(); i>0 && l==0xFFFFFFFF; --i) l=lengths[i-1];

  pos=input->tell();
  unsigned long read;
  unsigned char const *dt = l<=(unsigned long)(endPos-pos) ? input->read(l, read) : nullptr;
  if (!dt || read != l) {
    MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5: can not read some data\n"));
    return false;
  }
  std::vector<unsigned char> data(dt, dt+l);

  if (type==2 || type==4 || type==8) { // find with type==8
    l=lengths.back();
    lengths.pop_back();
    for (size_t i=lengths.size(); i>0 && l==0xFFFFFFFF; --i) l=lengths[i-1];
    if (l!=0xffffffff && l!=data.size()) {
      Canvas5Structure::LWZDecoder decoder(data.data(), data.size());
      std::vector<unsigned char> data2;
      if (!decoder.decode(data2) || data2.size()!=l) {
        MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5[LWZ]: can not decode some data\n"));
        return false;
      }
      std::swap(data, data2);
    }
  }

  if (type==3 || type==4 || type==7 || type==8) { // find with type==7,8
    l=lengths.back();
    lengths.pop_back();
    for (size_t i=lengths.size(); i>0 && l==0xFFFFFFFF; --i) l=lengths[i-1];
    if (l!=0xffffffff && l!=data.size()) {
      Canvas5Structure::NIBDecoder decoder(data.data(), data.size());
      std::vector<unsigned char> data2;
      if (!decoder.decode(l, data2)) {
        MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5[NIB]: can not decode some data\n"));
        return false;
      }
      std::swap(data, data2);
    }
  }

  if (type==6 || type==7 || type==8) { // find with type==7,8
    l=lengths.back();
    lengths.pop_back();
    for (size_t i=lengths.size(); i>0 && l==0xFFFFFFFF; --i) l=lengths[i-1];
    if (l!=0xffffffff && l!=data.size()) {
      Canvas5Structure::UnpackDecoder decoder(data.data(), data.size());
      std::vector<unsigned char> data2;
      if (!decoder.decode(l, data2)) {
        MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5[pack]: can not decode some data\n"));
        return false;
      }
      std::swap(data, data2);
    }
  }

  if (data.size()!=finalLength) {
    MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5[pack]: problem decoding data %lx/%lx\n", (unsigned long)data.size(), finalLength));
    return false;
  }

  stream->append(data.data(), unsigned(data.size()));

  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("Canvas5Structure::decodeZone5: find extra data\n"));
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:

