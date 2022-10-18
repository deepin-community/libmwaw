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
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stack>
#include <utility>

#include <librevenge/librevenge.h>

#include "MWAWFontConverter.hxx"
#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWHeader.hxx"
#include "MWAWPosition.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWStringStream.hxx"

#include "CanvasGraph.hxx"
#include "CanvasStyleManager.hxx"

#include "CanvasParser.hxx"

/** Internal: the structures of a CanvasParser */
namespace CanvasParserInternal
{
//! Internal: the layer of a CanvasParser
struct Layer {
  //! constructor
  Layer()
    : m_name()
    , m_numShapes()
    , m_shapesId()
  {
  }
  //! the layer name
  librevenge::RVNGString m_name;
  //! the number of shape
  int m_numShapes;
  //! the shape id
  std::vector<int> m_shapesId;
};

////////////////////////////////////////
//! Internal and low level: the decoder of a canvas file
struct Decoder {
  //! constructor
  Decoder()
    : m_version(2)
    , m_isWindows(false)
    , m_input()
    , m_inputPos(0)
    , m_stream()
  {
  }
  //! first function to init the output (and copy the first headerSize characters)
  bool initOutput(MWAWInputStreamPtr &input, unsigned long const headerSize=0x89c);
  //! returns true if the input is completly decoded
  bool isEnd() const
  {
    return m_inputPos>=m_input->size();
  }
  //! try to read the following sz bytes and append them to the output
  bool append(long length);
  //! try to decode a part of the input
  bool decode(long length=-1);
  //! try to decode a part of the input: v3
  bool decode3(long length);
  //! try to unpack some bits from buffer into buffer2, assuming buffer and buffer2 differs
  bool unpackBits(unsigned char const(&buffer)[256], int n,
                  unsigned char (&buffer2)[256], int &n2) const;

  //! the file version
  int m_version;
  //! a flag to know if the file is a windows file
  bool m_isWindows;
  //! the initial input
  MWAWInputStreamPtr m_input;
  //! the input current position
  long m_inputPos;
  //! the current stream
  std::shared_ptr<MWAWStringStream> m_stream;
};

bool Decoder::unpackBits(unsigned char const(&buffer)[256], int n,
                         unsigned char (&buffer2)[256], int &n2) const
{
  if (n<=0 || n>256 || &buffer == &buffer2) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::unpackBits: bad arguments\n"));
    return false;
  }
  int r=0;
  n2=0;
  // canvas only packs zone with less than 127 characters
  // => we must not found <M> M+1 bits <N> N+1 bits
  bool lastCopy=false;
  while (r+1<n) {
    int c=buffer[r++];
    if (c>=0x81) {
      unsigned char val=buffer[r++];
      int num=0x101-c;
      if (n2+num>256)
        return false;
      for (int i=0; i<num; ++i)
        buffer2[n2++]=val;
      lastCopy=false;
    }
    else {
      // normally c==0x80 is reserved, but must not be used
      if (lastCopy && !m_isWindows)
        return false;
      int num=c+1;
      if (r+num>n || n2+num>256)
        return false;
      for (int i=0; i<num; ++i)
        buffer2[n2++]=buffer[r++];
      lastCopy=true;
    }
  }
  return r==n;
}

bool Decoder::initOutput(MWAWInputStreamPtr &input, unsigned long const headerSize)
{
  m_input=input;
  if (!m_input || !m_input->checkPosition(long(headerSize)+20)) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::initOutput: can not find the input\n"));
    return false;
  }

  m_input->seek(0, librevenge::RVNG_SEEK_SET);
  unsigned long read;
  const unsigned char *dt = m_input->read(headerSize, read);
  if (!dt || read != headerSize) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::initOutput: can not read some data\n"));
    return false;
  }
  m_stream.reset(new MWAWStringStream(dt, unsigned(headerSize)));
  m_inputPos=long(headerSize);
  return true;
}

bool Decoder::append(long length)
{
  if (length==0)
    return true;
  if (!m_input || !m_stream || length<0 || !m_input->checkPosition(m_input->tell()+length)) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::append: the zone seems too short\n"));
    return false;
  }
  auto actIPos=m_input->tell();
  auto actOPos=m_stream->tell();
  m_input->seek(m_inputPos, librevenge::RVNG_SEEK_SET);
  m_stream->seek(0, librevenge::RVNG_SEEK_END);

  unsigned long read;
  const unsigned char *dt = m_input->read((unsigned long)length, read);
  bool ok=true;
  if (!dt || read != (unsigned long)length) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::append: can not read some data\n"));
    ok=false;
  }
  if (ok) {
    m_stream->append(dt, unsigned(length));
    m_inputPos=m_input->tell();
  }

  m_input->seek(actIPos, librevenge::RVNG_SEEK_SET);
  m_stream->seek(actOPos, librevenge::RVNG_SEEK_SET);
  return ok;
}

bool Decoder::decode(long length)
{
  if (!m_input || !m_stream) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode: can not find the input/output\n"));
    return false;
  }
  auto actIPos=m_input->tell();
  auto actOPos=m_stream->tell();
  m_input->seek(m_inputPos, librevenge::RVNG_SEEK_SET);
  m_stream->seek(0, librevenge::RVNG_SEEK_END);

  long lastPos=m_input->size();
  bool ok=true;
  if (m_inputPos>=lastPos)
    ok=false;
  if (m_version<=2) {
    long nWrite=0;
    unsigned char data[256], data2[256];
    while (ok && !m_input->isEnd()) {
      if (length>=0 && nWrite>=length)
        break;
      long pos=m_input->tell();
      int zSz=int(m_input->readULong(1));
      long endPos=pos+zSz;
      if (zSz==0 || endPos>lastPos) {
        MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode: can not read some data zSz=%d, pos=%lx\n", zSz, (unsigned long) pos));
        ok=false;
        break;
      }
      for (int i=0; i<zSz; ++i) data[i]=(unsigned char)(m_input->readULong(1));
      int n;
      if (!unpackBits(data, zSz, data2, n)) {
        MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode: can not read some data at %lx\n", (unsigned long) pos));
        ok=false;
        break;
      }
      m_stream->append(data2, static_cast<unsigned int>(n));
      nWrite+=n;
    }
    if (ok && length>=0 && nWrite!=length) {
      MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode: can not decode some data\n"));
      ok=false;
    }
  }
  else if (ok)
    ok=decode3(length);

  if (ok)
    m_inputPos=m_input->tell();

  m_input->seek(actIPos, librevenge::RVNG_SEEK_SET);
  m_stream->seek(actOPos, librevenge::RVNG_SEEK_SET);
  return ok;
}

#ifdef DEBUG_WITH_FILES
// flag to debug/or not the uncompress function
static bool s_showData=false;
#endif
bool Decoder::decode3(long length)
{
  if (!m_input || !m_stream) {
    MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode3: can not find the input/output\n"));
    return false;
  }
  long lastPos=m_input->size();
  long numWrite=0;

  int const maxFinalSize=120;
  unsigned char data[256], data2[256];
  bool forceDict=false;

  unsigned char m_dict[30];
  std::set<unsigned char> m_dictKeys;
  bool m_isDictInitialized=false;
  long lastDictPos=0;
  // a zone is stored:
  // - either as a list of [length] packbits [checksum]
  // - or as a dictionary (30 keys) and a list of [length] bytes where bytes can be:
  //    . packbits [checksum] as before
  //    . or compressed with dictionary of (packbits [checksum])
  // I supposed that the dictionary is only created if the zone's length is greated than a constant (to be verified).
  // There remains also the problem to know if (packbits [checksum]) has been compressed with the dictionary or not ;
  //   currently, I test if I can decode these sub zones with the dictionary, ...
  while (m_input->tell()<lastPos) {
    if (length>=0 && numWrite>=length)
      return numWrite==length;

    long pos=m_input->tell();
    int zSz=int(m_input->readULong(1));

    // FIXME: find a method to detect if the zone begins with a dictionary, maybe length>some constant
    if ((length<0 || numWrite==0) && lastDictPos+30!=pos && (zSz<2 || zSz>maxFinalSize+3 || forceDict)) {
      if (pos+30>lastPos) {
        MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode3: can not read a dictionary at pos=%lx\n", (unsigned long) pos));
        return false;
      }
      // create the dictionary
      lastDictPos=pos;
      m_dict[0]=(unsigned char)(zSz);
      for (int i=1; i<30; ++i)
        m_dict[i]=(unsigned char)m_input->readULong(1);
      m_dictKeys.clear();
      for (auto &c : m_dict) m_dictKeys.insert(c);
      m_isDictInitialized=true;
      forceDict=false;
      continue;
    }
    else if (forceDict) {
      MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode3: can not place a dictionary at pos=%lx\n", (unsigned long) pos));
      return false;
    }

    long endPos=pos+1+zSz;
    if (endPos>lastPos) {
      MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode3: force a dictionary in pos=%lx\n", (unsigned long) pos));
      forceDict=true;
      m_input->seek(pos, librevenge::RVNG_SEEK_SET);
      continue;
    }
    // FIXME: find a method if the data are compressed or not
    int const lastChecksumSz=m_isWindows ? 1 : 0;
    for (int step=0; step<3; ++step) {
      if (step==2) {
        MWAW_DEBUG_MSG(("CanvasParserInternal::Decoder::decode3: force a dictionary in pos=%lx\n", (unsigned long) pos));
        forceDict=true;
        m_input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      m_input->seek(pos+1, librevenge::RVNG_SEEK_SET);
      int numChar=int(m_input->readULong(1));
#ifdef DEBUG_WITH_FILES
      int nChar=numChar;
#endif
      if (step==0) {
        if (!m_isDictInitialized || zSz>numChar || numChar>2*zSz || numChar>maxFinalSize+2+lastChecksumSz) continue;
        // try to decode with the dictionary has been used to pack the data
        bool ok=true;
        int w=0;
        unsigned char c;
        bool readC=false;
        while (m_input->tell()<=endPos && w<numChar) {
          int newC=0;
          for (int st=0; st<4; ++st) {
            int val;
            if (!readC) {
              if (m_input->tell()>endPos) {
                ok=false;
                break;
              }
              c=(unsigned char) m_input->readULong(1);
              val=int(c>>4);
            }
            else
              val=int(c&0xf);
            readC=!readC;

            if (val && st<2) {
              data[w++]=m_dict[15*st+val-1];
              break;
            }
            newC=(newC<<4)|val;
            if (st==3) {
              if (m_dictKeys.find((unsigned char) newC)!=m_dictKeys.end()) {
                ok=false;
                break;
              }
              data[w++]=(unsigned char) newC;
            }
          }
          if (ok==false)
            break;
        }
        if (ok==false || w!=numChar || m_input->tell()<endPos)
          continue;
      }
      else {
        // basic copy
        // checkme: on mac, the first bytes is always ignored when numChar+1==zSz ;
        //          but only sometimes on windows :-~
        if (numChar+1!=zSz) {
          m_input->seek(-1, librevenge::RVNG_SEEK_CUR);
          numChar=zSz;
        }
        for (int i=0; i<numChar; ++i)
          data[i]=(unsigned char) m_input->readULong(1);
      }

      // first check the checksum
      int checkSum;
      bool ok=false;
      for (int step2=0; step2<2; ++step2) {
        if (step2==1) {
          if (!m_isWindows || step!=1 || numChar+1!=zSz)
            break;
          m_input->seek(-zSz, librevenge::RVNG_SEEK_CUR);
          numChar=zSz;
          for (int i=0; i<numChar; ++i)
            data[i]=(unsigned char) m_input->readULong(1);
        }
        checkSum=0;
        for (int i=0; i<numChar-1; ++i)
          checkSum+=int(data[i]);
        if (numChar==0 || (checkSum&0xff)!=int(data[numChar-1]))
          continue;
        ok=true;
        break;
      }
      if (!ok) continue;

      --numChar;
#ifdef DEBUG_WITH_FILES
      if (s_showData) {
        std::cout << zSz << "[" << nChar << "," << numChar << "]:";
        auto prev=std::cout.fill('0');
        for (int i=0; i < numChar; ++i)
          std::cout << std::hex << std::setw(2) << int(data[i]) << std::dec;
        std::cout.fill(prev);
        std::cout << "\n";
      }
#endif
      // then check if we can unpack the data
      int finalN;
      if (!unpackBits(data, numChar, data2, finalN) || finalN>maxFinalSize+lastChecksumSz || (m_isWindows && finalN<=numChar) ||
          (length>=0 && numWrite+finalN>length+lastChecksumSz) || finalN<1+lastChecksumSz) {
        if (m_isWindows && (length<0 || numWrite+numChar<=length+lastChecksumSz) &&
            numChar<=maxFinalSize+lastChecksumSz && numChar>=1+lastChecksumSz) {
          for (int i=0; i < numChar; ++i)
            data2[i]=data[i];
          finalN=numChar;
        }
        else
          continue;
      }
#ifdef DEBUG_WITH_FILES
      if (s_showData) {
        std::cout << "\t" << finalN << ":";
        auto prev=std::cout.fill('0');
        for (int i=0; i < finalN; ++i)
          std::cout << std::hex << std::setw(2) << int(data2[i]) << std::dec;
        std::cout.fill(prev);
        std::cout << "\n";
      }
#endif
      if (lastChecksumSz==1) {
        checkSum=0;
        for (int i=0; i<finalN-1; ++i)
          checkSum+=int(data2[i]);
        if ((checkSum&0xff)!=int(data2[finalN-1]))
          continue;
        --finalN;
      }
      m_stream->append(data2, static_cast<unsigned int>(finalN));
      numWrite+=finalN;
      break;
    }
  }
  return length<0 || numWrite==length;
}
////////////////////////////////////////
//! Internal: the state of a CanvasParser
struct State {
  //! constructor
  State()
    : m_isWindowsFile(false)
    , m_lengths()
    , m_brushLengths()
    , m_bitmapSize(0)
    , m_input()
    , m_decoder()
    , m_numLayers(1)
    , m_numShapes(0)
    , m_numViews(0)
    , m_numColors(256) // FIXME: check if this number is stored in the file or not
    , m_numPatterns(120)
    , m_sprayLengths()

    , m_numPages(1,1)
    , m_pageDimension(425, 624)
    , m_layers()

    , m_metaData()
  {
  }

  //! true if this is a windows file
  bool m_isWindowsFile;
  //! the file header first 4+1 lengths
  std::vector<unsigned long> m_lengths;
  //! the brush lengths
  std::vector<unsigned long> m_brushLengths;
  //! the file bitmap size (Windows v3)
  long m_bitmapSize;
  //! the uncompressed input
  MWAWInputStreamPtr m_input;
  //! the main decoder
  Decoder m_decoder;
  //! the number of layer
  int m_numLayers;
  //! the number of shapes
  int m_numShapes;
  //! the number of views
  int m_numViews;
  //! the number of colors
  int m_numColors;
  //! the number of patterns
  int m_numPatterns;
  //! the list of spray size
  std::vector<unsigned long> m_sprayLengths;

  //! the number of pages
  MWAWVec2i m_numPages;
  //! the page dimension
  MWAWVec2i m_pageDimension;
  //! the layer
  std::vector<Layer> m_layers;

  //! the meta data
  librevenge::RVNGPropertyList m_metaData;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
CanvasParser::CanvasParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state()
  , m_graphParser()
  , m_styleManager()
{
  resetGraphicListener();
  setAsciiName("main-1");

  m_state.reset(new CanvasParserInternal::State);

  m_styleManager.reset(new CanvasStyleManager(*this));
  m_graphParser.reset(new CanvasGraph(*this));

  getPageSpan().setMargins(0.1);
}

CanvasParser::~CanvasParser()
{
}

MWAWInputStreamPtr &CanvasParser::getInput()
{
  if (m_state->m_input)
    return m_state->m_input;
  return MWAWGraphicParser::getInput();
}

bool CanvasParser::decode(long length)
{
  long prevSize=m_state->m_input ? m_state->m_input->size() : 0;
  if (!m_state->m_input || !m_state->m_decoder.decode(length)) {
    if (m_state->m_decoder.m_stream)
      m_state->m_decoder.m_stream->resize((unsigned long)(prevSize));
    return false;
  }
  m_state->m_input->recomputeStreamSize();
  return true;
}

bool CanvasParser::isWindowsFile() const
{
  return m_state->m_isWindowsFile;
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void CanvasParser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    checkHeader(nullptr);
    ascii().setStream(getInput());
    ascii().open(asciiName());
    if (!readFileHeader())
      throw libmwaw::ParseException();

    bool const isWindows=isWindowsFile();
    m_state->m_decoder.m_isWindows=isWindows;
    m_state->m_decoder.m_version=version();
    if (!m_state->m_decoder.initOutput(getInput(), isWindows ? (unsigned long)(0x920+m_state->m_bitmapSize) : 0x89c) || !m_state->m_decoder.m_stream)
      throw libmwaw::ParseException();
    m_state->m_input.reset(new MWAWInputStream(m_state->m_decoder.m_stream, isWindows));

    // update the style manager and the graph parser and the asciiFile input
    m_styleManager->setInput(m_state->m_input);
    m_graphParser->setInput(m_state->m_input);
    ascii().setStream(m_state->m_input);

    ok = createZones();
    if (ok) {
      createDocument(docInterface);
      for (auto const &layer : m_state->m_layers)
        send(layer);
      m_graphParser->checkUnsent();
    }
  }
  catch (...) {
    MWAW_DEBUG_MSG(("CanvasParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  ascii().reset();
  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void CanvasParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("CanvasParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  MWAWPageSpan ps(getPageSpan());
  if (m_state->m_numPages!=MWAWVec2i(1,1)) {
    ps.setFormWidth(double(m_state->m_numPages[0])*ps.getFormWidth());
    ps.setFormLength(double(m_state->m_numPages[1])*ps.getFormLength());
  }
  ps.setPageSpan(1);
  std::vector<MWAWPageSpan> pageList(1,ps);
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);

  if (!m_state->m_metaData.empty())
    listen->setDocumentMetaData(m_state->m_metaData);
  listen->startDocument();
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool CanvasParser::createZones()
{
  auto input=getInput();
  if (!input) return false;

  MWAWRSRCParserPtr rsrcParser = getRSRCParser();
  if (rsrcParser) {
    auto rsrcInput=rsrcParser->getInput();
    auto &rsrcAscii=rsrcParser->ascii();
    auto &entryMap = rsrcParser->getEntriesMap();

    for (int w=0; w<5; ++w) {
      static char const *wh[]= {"HeAd" /*11164*/, "Jinf" /* 10568 */, "WIND" /* 10568 */, "LPol" /* 2,4,... */, "USER" /*0*/};
      auto it = entryMap.lower_bound(wh[w]);
      while (it != entryMap.end() && it->first==wh[w]) {
        auto const &entry=it++->second;
        if (!entry.valid()) continue;
        switch (w) {
        case 0:
          readRSRCFileHeader(rsrcInput, entry, rsrcAscii);
          break;
        case 1:
          readPrintInfo(rsrcInput, entry, rsrcAscii);
          break;
        case 2:
          readWindows(rsrcInput, entry, rsrcAscii);
          break;
        case 3:
          readLPOL(rsrcInput, entry, rsrcAscii);
          break;
        case 4:
        default:
          readUsers(rsrcInput, entry, rsrcAscii);
          break;
        }
      }
    }
  }

  bool const isWindows=m_state->m_isWindowsFile;
  input->seek(0x3c, librevenge::RVNG_SEEK_SET);
  if (isWindows) {
    if (!m_graphParser->readFileBitmap(m_state->m_bitmapSize) || !input->checkPosition(input->tell()+132))
      return false;
    long pos=input->tell();
    libmwaw::DebugStream f;
    f << "Entries(Brush):lengths=[";
    for (int i=0; i<32; ++i) {
      auto length=input->readULong(4);
      m_state->m_brushLengths.push_back(length);
      f << length << ",";
    }
    f << "],";
    f << "f0=" << input->readULong(4) << ","; // small number
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (!readUnknownZoneHeader() || !m_styleManager->readPenSize() || !readDocumentHeader() || !readMacroNames() ||
      !readUnknownZone0() || !m_styleManager->readArrows() || !readFormats() || !readGrids() || !readUnknownZone1())
    return false;

  if (!m_graphParser->readShapes(m_state->m_numShapes, m_state->m_lengths[0], m_state->m_lengths[1]))
    return false;

  if (!readLayers() || !readViews() || !m_styleManager->readPatterns(m_state->m_numPatterns))
    return false;

  if (!m_styleManager->readColors(m_state->m_numColors))
    return true;
  if (!readUnknownZone2() || !readBrushes() || !readUnknownZone3())
    return true;
  if (!readSprays() || !readUnknownZone4())
    return true;

  // end of v2
  if (m_state->m_decoder.isEnd())
    return true;

  if (!m_styleManager->readDashes(6) || !readEndV3())
    return true;

  if (isWindows && !readRSRCWindowsFile())
    return true;

  if (m_state->m_decoder.isEnd())
    return true;

  decode(-1);
  MWAW_DEBUG_MSG(("CanvasParser::createZones: unexpected last zone size\n"));
  ascii().addPos(input->tell());
  ascii().addNote("Entries(Last):###");

  return true;
}

bool CanvasParser::readLayers()
{
  if (long(m_state->m_lengths[2])<0 || !decode(long(m_state->m_lengths[2]))) {
    MWAW_DEBUG_MSG(("CanvasParser::readLayers: can not decode the input\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  long endPos=pos+long(m_state->m_lengths[2]);
  if (!input->checkPosition(endPos) || m_state->m_numLayers < 0 ||
      long(m_state->m_lengths[2])/42 < m_state->m_numLayers) {
    MWAW_DEBUG_MSG(("CanvasParser::readLayers: zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Layer):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  std::vector<unsigned long> dataSize;
  m_state->m_layers.resize(size_t(m_state->m_numLayers));
  for (size_t i=0; i<size_t(m_state->m_numLayers); ++i) {
    pos=input->tell();
    f.str("");
    f << "Layer-" << i << ":";
    CanvasParserInternal::Layer &layer=m_state->m_layers[i];
    dataSize.push_back(input->readULong(4));
    f << "dSz=" << dataSize.back() << ",";
    layer.m_numShapes=int(input->readULong(2));
    f << "n[shapes]=" << layer.m_numShapes << ",";
    ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+22, librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');
    if (readString(layer.m_name, 20))
      f << layer.m_name.cstr() << ",";
    else {
      f << "###name,";
      MWAW_DEBUG_MSG(("CanvasParser::readLayers: bad name\n"));
    }
    input->seek(pos+42, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=endPos) {
    ascii().addPos(input->tell());
    ascii().addNote("Layer-End:");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  for (size_t i=0; i<size_t(m_state->m_numLayers); ++i) {
    if (!dataSize[i]) continue;
    if (long(dataSize[i])<0 || !decode(long(dataSize[i]))) {
      MWAW_DEBUG_MSG(("CanvasParser::readLayers: can not decode the data %d input\n", int(i)));
      return false;
    }
    pos=input->tell();
    f.str("");
    f << "Layer-data" << i << ":";
    auto &layer=m_state->m_layers[i];
    if (!input->checkPosition(pos+long(dataSize[size_t(i)]))) {
      MWAW_DEBUG_MSG(("CanvasParser::readLayers: can not find data %d\n", int(i)));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (long(dataSize[size_t(i)])<2*layer.m_numShapes) {
      MWAW_DEBUG_MSG(("CanvasParser::readLayers: the size seems too short\n"));
      f << "###";
    }
    else {
      if (layer.m_numShapes)
        f << "f0=" << std::hex << input->readULong(2) << std::dec << ",";
      f << "ids=[";
      for (int j=1; j<layer.m_numShapes; ++j) {
        layer.m_shapesId.push_back(int(input->readULong(2)));
        f << layer.m_shapesId.back() << ",";
      }
      f << "],";
      if (long(dataSize[size_t(i)])!=2*layer.m_numShapes)
        ascii().addDelimiter(input->tell(),'|');
    }
    input->seek(pos+long(dataSize[size_t(i)]), librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  return true;
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool CanvasParser::checkHeader(MWAWHeader *header, bool strict)
{
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(0x89e))
    return false;

  input->setReadInverted(false);
  input->seek(0x36, librevenge::RVNG_SEEK_SET);
  int val=int(input->readULong(2));
  int vers=0;
  switch (val) {
  case 1:
    vers=2;
    break;
  case 2:
    vers=3;
    break;
  case 0x100:
    input->setReadInverted(true);
    m_state->m_isWindowsFile=true;
    vers=3;
    break;
  default:
    MWAW_DEBUG_MSG(("CanvasParser::checkHeader: unknown version=%d\n", val));
    return false;
  }

  input->seek(0, librevenge::RVNG_SEEK_SET);
  unsigned long lengths[3];
  for (auto &l : lengths) {
    // check that no shape/shape data/layer lengths is empty
    l=input->readULong(4);
    if (l==0)
      return false;
  }
  if (strict) {
    // try to decode the shape and the shape data zone
    CanvasParserInternal::Decoder decoder;
    decoder.m_isWindows=m_state->m_isWindowsFile;
    decoder.m_version=vers;
    input->seek(0x38, librevenge::RVNG_SEEK_SET);
    unsigned long bitmapSize=m_state->m_isWindowsFile ? input->readULong(4) : 0;
    if (long(bitmapSize)<0 || (m_state->m_isWindowsFile && !input->checkPosition(long(0x920+bitmapSize))) ||
        !decoder.initOutput(input, m_state->m_isWindowsFile ? 0x920+bitmapSize : 0x89c) ||
        !decoder.decode(long(lengths[0])) || !decoder.decode(long(lengths[1])))
      return false;
  }
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_CANVAS, vers, MWAWDocument::MWAW_K_DRAW);

  return true;
}

bool CanvasParser::readFileHeader()
{
  auto input=getInput();
  long const endPos=0x3c;
  if (!input || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("CanvasParser::readFileHeader: file is too short\n"));
    return false;
  }

  m_state->m_lengths.clear();

  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  f << "length=[";
  for (int i=0; i<13; ++i) {
    auto length=input->readULong(4);
    if (i>=4 && i<12)
      m_state->m_brushLengths.push_back(length);
    else
      m_state->m_lengths.push_back(length);
    f << length << ",";
  }
  f << "],";
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "FileHeader-end:";
  int val;
  val=int(input->readLong(1));
  if (val==1)
    f << "little[endian],";
  else if (val)
    f << "##endian=" << val << ",";
  val=int(input->readULong(1)); // v2: 100, v3: 104
  switch (val) {
  case 100:
    f << "v2.0,";
    break;
  case 102:
    f << "v2.1,";
    break;
  case 104:
    f << "v3.0,";
    break;
  case 105:
    f << "v3.5,";
    break;
  case 107: // or windows 3.5
    f << "v3.5.2,";
    break;
  default:
    f << "version=" << val << ",";
    break;
  }
  val=int(input->readULong(2));
  if (val!=1) f << "vers=" << val+1 << ",";
  if (isWindowsFile()) {
    m_state->m_bitmapSize=long(input->readULong(4));
    f << "bitmap[size]=" << m_state->m_bitmapSize << ",";
  }
  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readDocumentHeader()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+230)) {
    MWAW_DEBUG_MSG(("CanvasParser::readDocumentHeader: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Document):";
  input->seek(pos+46, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(2));
  if (dim[0]!=1 || dim[1]!=1) {
    m_state->m_numPages=MWAWVec2i(dim[0], dim[1]);
    f << "pages=" << m_state->m_numPages << ",";
    if (dim[0]<=0 || dim[0]>15 || dim[1]<=0 || dim[1]>15) {
      MWAW_DEBUG_MSG(("CanvasParser::readDocumentHeader: the number of pages seems bad\n"));
      f << "###";
      m_state->m_numPages=MWAWVec2i(1,1);
    }
  }
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+60, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-0:";
  m_state->m_numPatterns=int(input->readULong(2));
  if (m_state->m_numPatterns!=120)
    f << "num[patterns]=" << m_state->m_numPatterns << ",";
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+58, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-1:";
  int val=int(input->readULong(2));
  m_state->m_numShapes=int(input->readULong(2));
  f << "num[shapes]=" << m_state->m_numShapes << ",";
  if (val!=m_state->m_numShapes)
    f << "max[shapes]=" << val << ",";
  val=int(input->readULong(2));
  f << "f0=" << val << ","; // small number
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+30, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-2:";
  for (int i=0; i<9; ++i) { // f0=-1|1, f1=1|5, f2=0|1,f3=0|2, f7=-1000|-2000
    val=int(input->readLong(2));
    int const expected[]= {-1,1,0,0,1,0,0,-1000,-1000};
    if (val==expected[i])
      continue;
    if (i==4) {
      m_state->m_numLayers=val;
      f << "N[layer]=" << val << ",";
    }
    else if (i==6) {
      m_state->m_numViews=val;
      f << "N[view]=" << val << ",";
    }
    else
      f << "f" << i << "=" << val << ",";
  }
  if (version()==2) {
    librevenge::RVNGString text;
    if (readString(text, 64)) // find "1page Grp B&W"
      f << text.cstr() << ",";
    else
      f << "###string,";
  }
  else // something like 0a40800...0
    ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+18+64, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readMacroNames()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+32*20)) {
    MWAW_DEBUG_MSG(("CanvasParser::readMacroNames: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  for (int i=0; i<32; ++i) {
    pos=input->tell();
    f.str("");
    f << "Entries(Macro)[" << i << "]:";
    librevenge::RVNGString text;
    if (readString(text, 20, true)) {
      if (text.empty()) {
        ascii().addPos(pos);
        ascii().addNote("_");
        input->seek(pos+20, librevenge::RVNG_SEEK_SET);
        continue;
      }
      f << "name=" << text.cstr() << ",";
    }
    else
      f << "##name,";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool CanvasParser::readUnknownZoneHeader()
{
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  if (!input || !input->checkPosition(pos+28)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZoneHeader: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(ZoneH):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+28, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readUnknownZone0()
{
  auto input=getInput();
  long pos=input ? input->tell() : 0;
  if (!input || !input->checkPosition(pos+252)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone0: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Zone0):";
  int val=int(input->readLong(2));
  if (val!=-1) // or 5
    f << "f0=" << val << ",";
  for (int i=0; i<20; ++i) { // f1=0|30, f7=f9=0|8000, f12=0|1, f14=f19=0|5, f16=f17=0|20, f20=0|29
    val=int(input->readULong(2));
    if (!val) continue;
    if (val<0x1000)
      f << "f" << i+1 << "=" << val << ",";
    else
      f << "f" << i+1 << "=" << std::hex << val << std::dec << ",";
  }
  for (int st=0; st<2; ++st) {
    unsigned char col[3];
    for (auto &c : col) c=(unsigned char)(input->readULong(2)>>8);
    MWAWColor color(col[0],col[1],col[2]);
    if (color!=MWAWColor::black())
      f << "col" << st << "=" << color << ",";
  }
  f << "id=" << std::hex << input->readULong(4) << std::dec << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Zone0-1:";
  for (int i=0; i<9; ++i) { // f0=3|5|4e, f1=1
    val=int(input->readULong(2));
    if (!val) continue;
    if (val<0x1000)
      f << "f" << i+1 << "=" << val << ",";
    else
      f << "f" << i+1 << "=" << std::hex << val << std::dec << ",";
  }
  librevenge::RVNGString text;
  if (readString(text, 20))
    f << "name=" << text.cstr() << ","; // Layer
  else
    f << "###name,";
  input->seek(pos+18+20, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Zone0-2:";
  f << "font=[";
  f << "id=" << input->readULong(2) << ",";
  val=int(input->readULong(2));
  if (val) f << "fl=" << std::hex << val << std::dec << ",";
  f << "sz=" << input->readULong(2) << ",";
  f << "],";
  for (int i=0; i<36; ++i) { // f3=-2, f7=0|1, f11=0|28, f13=0|18, f15=0|8, f35=0|-16
    val=int(input->readLong(2));
    if (!val) continue;
    f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Zone0-3:";
  for (int i=0; i<7; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {-50,16,-26,0,0,0,0};
    if (val==expected[i]) continue;
    f << "f" << i << "=" << val << ",";
  }
  for (int st=0; st<2; ++st) {
    int dim[4];
    for (auto &d : dim) d=int(input->readLong(2));
    if (dim[0]!=dim[2])
      f << "box" << st << "=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  }
  for (int i=0; i<12; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {1,16,0,3,1,0,0x48 /* or 4b*/, 0, 1, 0, 1, 0};
    if (val==expected[i]) continue;
    f << "g" << i << "=" << val << ",";
  }
  if (readString(text, 20)) // checkme: in v3.5 windows, probably junk
    f << "name=" << text.cstr() << ",";
  else
    f << "###name,";
  input->seek(pos+14+16+24+20, librevenge::RVNG_SEEK_SET);
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    f << "overlap[" << (i==0 ? "H" : "V") << "]=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readBrushes()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input) {
    MWAW_DEBUG_MSG(("CanvasParser::readBrushes: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Brush):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  bool const isWindows=isWindowsFile();
  if (!isWindows) {
    for (size_t i=0 ; i<m_state->m_brushLengths.size() ; ++i) {
      auto len=m_state->m_brushLengths[i];
      if (!len) continue;
      if (long(len)<0 || !decode(long(len))) {
        MWAW_DEBUG_MSG(("CanvasParser::readBrushes: can not decode the input %d\n", int(i)));
        return false;
      }
      pos=input->tell();
      f.str("");
      f << "Brush-" << i << ":";
      int N=int(input->readULong(2));
      if (!input->checkPosition(pos+2+4*N) || 2+4*N>long(len)) {
        MWAW_DEBUG_MSG(("CanvasParser::readBrushes: can not read a brush\n"));
        return false;
      }
      input->seek(pos+long(len), librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    return true;
  }

  // windows file have more brushes, the first 36 are generally stored by pair, the last one are stored one by one ?
  for (size_t i=0 ; i<m_state->m_brushLengths.size() ; ++i) {
    auto len=m_state->m_brushLengths[i];
    if (i+1<m_state->m_brushLengths.size())
      len+=m_state->m_brushLengths[i+1];
    if (!len) {
      ++i;
      continue;
    }
    if (i>=36 || len>256 || long(len)<0 || !decode(long(len))) { // check me: nbig blocks are stored one by one, what is the limit ?
      len=m_state->m_brushLengths[i];
      if (long(len)<0 || !decode(long(len))) {
        MWAW_DEBUG_MSG(("CanvasParser::readBrushes: can not decode the input %d\n", int(i)));
        return false;
      }
      pos=input->tell();
      f.str("");
      f << "Brush-" << i << ":";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());

      input->seek(pos+long(len), librevenge::RVNG_SEEK_SET);
      continue;
    }
    pos=input->tell();
    f.str("");
    f << "Brush-" << i << ":";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    f.str("");
    f << "Brush-" << ++i << ":";
    ascii().addPos(pos+long(m_state->m_brushLengths[i-1]));
    ascii().addNote(f.str().c_str());

    input->seek(pos+long(len), librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool CanvasParser::readSprays()
{
  auto input=getInput();
  for (auto const &l : m_state->m_sprayLengths) {
    if (!l) continue;
    if (long(l)<0 || !decode(long(l))) {
      MWAW_DEBUG_MSG(("CanvasParser::readSprays: can not decode the input\n"));
      return false;
    }
    long pos=input->tell();
    if (long(l)<=0 || !input->checkPosition(pos+long(l))) {
      MWAW_DEBUG_MSG(("CanvasParser::readSprays: can not read a spray\n"));
      return false;
    }
    /* spray:
       ID dSz=0006 bdBox=ffefffef00100010 ymin,xmin ymax,xmax
       then dY*dSz
       ID dSz=0004 bdBox=fff7fff30007000b
    */
    ascii().addPos(pos);
    ascii().addNote("Spray:");
    input->seek(pos+long(l), librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool CanvasParser::readFormats()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(8+6*44)) {
    MWAW_DEBUG_MSG(("CanvasParser::readFormats: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Format):";
  input->seek(pos+8, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int u=0; u<6; ++u) {
    pos=input->tell();
    f.str("");
    f << "Format-" << u << ":";
    input->seek(pos+8, librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');
    for (int i=0; i<4; ++i) {
      int val=int(input->readULong(4));
      if (val!=0x10000)
        f << "dim" << i << "=" << double(val)/double(0x10000) << ",";
    }
    librevenge::RVNGString text;
    if (readString(text, 20))
      f << "name=" << text.cstr() << ",";
    else
      f << "###name,";
    input->seek(pos+44, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool CanvasParser::readGrids()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+18*20)) {
    MWAW_DEBUG_MSG(("CanvasParser::readGrids: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  for (int i=0; i<3; ++i) {
    pos=input->tell();
    f.str("");
    f << "Entries(Grid)[" << i << "]:";
    librevenge::RVNGString text;
    if (readString(text, 20)) {
      if (text.empty()) {
        ascii().addPos(pos);
        ascii().addNote("_");
        input->seek(pos+20, librevenge::RVNG_SEEK_SET);
        continue;
      }
      f << "name=" << text.cstr() << ",";
    }
    else
      f << "###name,";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  pos=input->tell();
  f.str("");
  f << "Entries(Spray):lengths=[";
  for (int i=0; i<20; ++i) {
    m_state->m_sprayLengths.push_back(input->readULong(4));
    if (m_state->m_sprayLengths.back())
      f << m_state->m_sprayLengths.back() << ",";
    else
      f << "_,";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<11; ++i) {
    // checkme: make no sense
    pos=input->tell();
    f.str("");
    f << "Entries(Grid)[" << i+3 << "]:";
    librevenge::RVNGString text;
    if (readString(text, 20)) {
      if (text.empty()) {
        ascii().addPos(pos);
        ascii().addNote("_");
        input->seek(pos+20, librevenge::RVNG_SEEK_SET);
        continue;
      }
      f << "name=" << text.cstr() << ",";
    }
    else
      f << "###name,";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+20, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool CanvasParser::readUnknownZone1()
{
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+162)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone1: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Zone1):";
  int val;
  for (int i=0; i<18; ++i) {
    val=int(input->readULong(2));
    if (!val) continue;
    f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<16; ++i) {
    val=int(input->readLong(2));
    int const expected[]= { 1, 0, 1, 1, 10, 0, 10, 0, 100 /* or 10000 */, 1,
                            2, 1/* or 4*/, 0/*or 1*/, 0, 1, 1
                          };
    if (val==expected[i]) continue;
    f << "g" << i << "=" << val << ",";
  }
  for (int i=0; i<9; ++i) {
    val=int(input->readLong(4));
    int const expected[]= { 100 /* or 120*/, 100, 100, 1 /* or 4*/, 1, 1, 1, 1, 1 };
    if (val==65536*expected[i]) continue;
    f << "h" << i << "=" << double(val)/65536. << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  pos=input->tell();
  f.str("");
  f << "Zone1-1:";
  for (int i=0; i<10; ++i) { // f5=small number, f6/f7: related to spray(a list of bit?) f14: related to brush?
    val=int(input->readLong(2));
    int const expected[]= { 50, 10, 0 /* or 1*/, 0, 0, 0, 2, 0, 0, 0};
    if (val==expected[i]) continue;
    f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<19; ++i) { // f27= almost always 1, f28=almost always 3
    val=int(input->readLong(2));
    if (!val) continue;
    f << "f" << i+10 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readUnknownZone2()
{
  if (!decode(96)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone2: can not decode the input\n"));
    return false;
  }
  auto input=getInput();
  long pos=input->tell();
  if (!input || !input->checkPosition(pos+96)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone2: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Zone2):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+96, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readUnknownZone3()
{
  if (long(m_state->m_lengths[4])==0)
    return true;
  if (long(m_state->m_lengths[4])<0 || !decode(long(m_state->m_lengths[4]))) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone3: can not decode the input\n"));
    return false;
  }

  // 003a07000b010202080205030a040e0400050605030609060c0607070f0705080908000903090c09060a0a0a0e0a040b010c070c0a0c0d0d040e080f
  auto input=getInput();
  long pos=input->tell();
  int sz=int(input->readULong(2));
  if (!input || !input->checkPosition(pos+2+sz) || 2+sz>long(m_state->m_lengths[4])) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone3: file is too short\n"));
    return false;
  }

  libmwaw::DebugStream f;
  f << "Entries(Zone3):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+long(m_state->m_lengths[4]), librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readUnknownZone4()
{
  if (!decode(486)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone4: can not decode data\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  long endPos=pos+486;
  if (!input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("CanvasParser::readUnknownZone4: zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Zone4):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  int val=int(input->readLong(2)); // small number
  if (val) f << "f0=" << val << ",";
  for (int i=0; i<2; ++i) { // f1=-1|25, f2=0|8
    val=int(input->readLong(1));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }
  int dim[2];
  for (auto &d : dim) d=int(input->readLong(2));
  m_state->m_pageDimension=MWAWVec2i(dim[0], dim[1]);
  f << "dim=" << m_state->m_pageDimension << ",";
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  ascii().addPos(pos+200);
  ascii().addNote("Zone4-0");
  ascii().addPos(pos+350);
  ascii().addNote("Zone4-1");
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readViews()
{
  if (long(m_state->m_lengths[3])<0 || (m_state->m_lengths[3] && !decode(long(m_state->m_lengths[3])))) {
    MWAW_DEBUG_MSG(("CanvasParser::readViews: can not decode the input\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  long endPos=pos+long(m_state->m_lengths[3]);
  if (!input->checkPosition(endPos) ||
      long(m_state->m_lengths[3])/26 < m_state->m_numViews) {
    MWAW_DEBUG_MSG(("CanvasParser::readViews: zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(View):";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<m_state->m_numViews; ++i) {
    pos=input->tell();
    f.str("");
    f << "View-" << i << ":";
    // 3 int:  a position + ?
    input->seek(pos+6, librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');
    librevenge::RVNGString text;
    if (readString(text, 20))
      f << text.cstr() << ",";
    else {
      f << "###name,";
      MWAW_DEBUG_MSG(("CanvasParser::readViews: bad name\n"));
    }
    input->seek(pos+26, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=endPos && m_state->m_numViews) {
    ascii().addPos(input->tell());
    ascii().addNote("View-End:");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  return true;
}

bool CanvasParser::readEndV3()
{
  if (!decode(40)) {
    MWAW_DEBUG_MSG(("CanvasParser::readEndV3: can not decode the input zone\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+40)) {
    MWAW_DEBUG_MSG(("CanvasParser::readEndV3: zone seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(EndV3):lengths=[";
  long lengths[10];
  for (auto &l : lengths) {
    l=long(input->readULong(4));
    if (l)
      f << l << ",";
    else
      f << "_,";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<10; ++i) {
    if (lengths[i]==0)
      continue;
    if (!decode(lengths[i])) {
      MWAW_DEBUG_MSG(("CanvasParser::readEndV3: can not decode the zone %d\n", i));
      return false;
    }
    pos=input->tell();
    int const expectedLengths[]= {60, 46, 34, 8, 132,
                                  0, 0, 0, 0, 0
                                 };
    int const dataLength=expectedLengths[i];
    long endPos=pos+lengths[i];
    if (lengths[i]<0 || endPos<=pos || !input->checkPosition(endPos) ||
        (dataLength && lengths[i]<dataLength)) {
      MWAW_DEBUG_MSG(("CanvasParser::readEndV3: zone %d seems too short\n", i));
      ascii().addPos(pos);
      ascii().addNote("Entries(Bad):###");
      return false;
    }
    bool done=false;
    switch (i) {
    case 0:
      done=m_styleManager->readDashes(int(lengths[i]/60), true);
      break;
    case 4:
      done=m_styleManager->readFonts(int(lengths[i]/132));
      break;
    default:
      break;
    }

    char const *wh[]= {"Dash", nullptr, nullptr, nullptr, "Font",
                       nullptr, nullptr, nullptr, nullptr, nullptr
                      };
    std::string what;
    if (wh[i])
      what=wh[i];
    else {
      std::stringstream s;
      s << "ZoneA" << i;
      what=s.str();
    }
    if (done) {
      if (input->tell()!=endPos) {
        MWAW_DEBUG_MSG(("CanvasParser::readEndV3: find extra data for zone %d\n", i));
        f.str("");
        f << what << "-extra:###";
        ascii().addPos(input->tell());
        ascii().addNote(f.str().c_str());
        input->seek(endPos, librevenge::RVNG_SEEK_SET);
      }
      continue;
    }
    f.str("");
    f << "Entries(" << what << "):";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    if (dataLength>0) {
      int n=0;
      while (input->tell()+dataLength<=endPos) {
        pos=input->tell();
        f.str("");
        f << what << "-" << n++ << ":";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        input->seek(pos+dataLength, librevenge::RVNG_SEEK_SET);
      }
      if (input->tell()!=endPos) {
        f.str("");
        f << what << ":extra##";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
      }
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  return true;
}

bool CanvasParser::readString(librevenge::RVNGString &string, int maxSize, bool canBeCString)
{
  return readString(getInput(), string, maxSize, canBeCString);
}

bool CanvasParser::readString(MWAWInputStreamPtr input, librevenge::RVNGString &string, int maxSize, bool canBeCString)
{
  string.clear();
  if (!input) {
    MWAW_DEBUG_MSG(("CanvasParser::readString: can not find the input\n"));
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
        MWAW_DEBUG_MSG(("CanvasParser::readString: find unknown unicode for char=%d\n", int(c)));
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
        MWAW_DEBUG_MSG(("CanvasParser::readString: find unknown unicode for char=%d\n", int(c)));
      }
    }
  }
  else {
    MWAW_DEBUG_MSG(("CanvasParser::readString: bad size=%d\n", sSz));
    return false;
  }
  return true;
}

// ------------------------------------------------------------
// mac resource fork
// ------------------------------------------------------------

bool CanvasParser::readPrintInfo(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile)
{
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<120) {
    MWAW_DEBUG_MSG(("CanvasParser::readPrintInfo: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  libmwaw::PrinterInfo info;
  if (!info.read(input)) {
    MWAW_DEBUG_MSG(("CanvasParser::readPrintInfo: can not read the zone length\n"));
    f << "###";
    ascFile.addPos(entry.begin()-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  f << info;
  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    f << "###";
    ascFile.addPos(entry.begin()-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }

  if (entry.id()==10568) {
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
    int botMarg = rBotMargin.y() -50;
    if (botMarg < 0) botMarg=0;

    getPageSpan().setMarginTop(lTopMargin.y()/72.0);
    getPageSpan().setMarginBottom(botMarg/72.0);
    getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
    getPageSpan().setMarginRight(rightMarg/72.0);
    getPageSpan().setFormLength(paperSize.y()/72.);
    getPageSpan().setFormWidth(paperSize.x()/72.);
  }
  else {
    MWAW_DEBUG_MSG(("CanvasParser::readPrintInfo: find unexpected\n"));
  }
  if (entry.length()>124)
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readLPOL(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile)
{
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<2) {
    MWAW_DEBUG_MSG(("CanvasParser::readLPOL: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(LPOL)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  int N=int(input->readULong(2));
  if (2+4*N>entry.length()) {
    f << "###N=" << N << ",";
    MWAW_DEBUG_MSG(("CanvasParser::readLPOL: can not read the number of elements\n"));
    ascFile.addPos(entry.begin()-4);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  for (int i=0; i<N; ++i) {
    f << "[";
    for (int v=0; v<4; ++v) f << input->readLong(1) << ",";
    f << "],";
  }
  if (input->tell()!=entry.end())
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readRSRCFileHeader(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile)
{
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<56) {
    MWAW_DEBUG_MSG(("CanvasParser:readRSRCFileHeader: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "FileHeader[RSRC" << entry.id() << "]:";
  f << "length?=[";
  for (int i=0; i<13; ++i) {
    auto length=input->readULong(4);
    f << length << ",";
  }
  int val=int(input->readLong(2)); // v2: 100, v3: 104
  switch (val) {
  case 100:
    f << "v2.0,";
    break;
  case 102:
    f << "v2.1,";
    break;
  case 104:
    f << "v3.0,";
    break;
  default:
    f << "version=" << val << ",";
    break;
  }
  val=int(input->readULong(2));
  if (val!=1) f << "vers=" << val+1 << ",";

  if (input->tell()!=entry.end())
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readUsers(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile)
{
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<514) {
    MWAW_DEBUG_MSG(("CanvasParser::readUsers: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Users)[" << entry.id() << "]:";

  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  librevenge::RVNGString text;
  if (readString(input, text, 64)) { // user name
    if (!text.empty())
      m_state->m_metaData.insert("meta:initial-creator", text);
    f << text.cstr() << ",";
  }
  else {
    f << "###name,";
    MWAW_DEBUG_MSG(("CanvasParser::readUsers: bad user name\n"));
  }
  input->seek(entry.begin()+64,librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(entry.begin()+128,librevenge::RVNG_SEEK_SET);
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "Users-0:";
  input->seek(pos+128,librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Users-1:";
  int val=int(input->readLong(2));
  if (val) f << "f0=" << val << ",";
  if (readString(input, text, 64)) // limits?
    f << text.cstr() << ",";
  else {
    f << "###dir,";
    MWAW_DEBUG_MSG(("CanvasParser::readUsers: bad dir\n"));
  }
  input->seek(pos+2+64,librevenge::RVNG_SEEK_SET);
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(pos+128,librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Users-2:";
  input->seek(pos+130,librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  if (input->tell()!=entry.end())
    ascFile.addDelimiter(input->tell(),'|');
  return true;
}

bool CanvasParser::readWindows(MWAWInputStreamPtr input, MWAWEntry const &entry, libmwaw::DebugFile &ascFile)
{
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<20) {
    MWAW_DEBUG_MSG(("CanvasParser::readWindows: the zone seems too small\n"));
    return false;
  }
  entry.setParsed(true);
  libmwaw::DebugStream f;
  f << "Entries(Windows)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(2));
  f << "win=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  for (int i=0; i<6; ++i) { // f6=small number
    int val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  if (input->tell()!=entry.end())
    ascFile.addDelimiter(input->tell(),'|');
  ascFile.addPos(entry.begin()-4);
  ascFile.addNote(f.str().c_str());
  return true;
}

// ------------------------------------------------------------
// windows resource fork
// ------------------------------------------------------------

bool CanvasParser::readRSRCWindowsFile()
{
  auto input=getInput();
  if (!input)
    return false;

  MWAWEntry entries[2];
  for (int step=0; step<2; ++step) {
    if (!m_state->m_decoder.append(4)) {
      MWAW_DEBUG_MSG(("CanvasParser::readRSRCWindowsFile: zone5 can not retrieve the length of zone %dB\n", step));
      return false;
    }
    input->recomputeStreamSize();
    long pos=input->tell();
    long sz=long(input->readULong(4));
    long endPos=pos+4+sz;
    if (sz<0 || !decode(sz) || !input->checkPosition(endPos)) {
      MWAW_DEBUG_MSG(("CanvasParser::readRSRCWindowsFile: can not decode zone %dB\n", step));
      return false;
    }

    entries[step].setBegin(pos+4);
    entries[step].setLength(sz);
    libmwaw::DebugStream f;
    if (step==0)
      f << "Entries(RSRCMap):";
    else
      f << "Entries(RSRCData):";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }

  long endPos=input->tell();
  int N=int(entries[0].length()/64);
  input->seek(entries[0].begin(), librevenge::RVNG_SEEK_SET);
  for (int n=0; n<N; ++n) {
    long pos=input->tell();
    libmwaw::DebugStream f;
    f << "RSRCMap-" << n << ":";

    bool ok=true;
    std::string types[2];
    for (auto &text : types) {
      for (int c=0; c<4; ++c) {
        char ch=char(input->readULong(1));
        if (ch==0) {
          ok=false;
          break;
        }
        text+=ch;
      }
      if (!ok) break;
      f << text << ",";
    }
    if (!ok) { // empty field
      ascii().addPos(pos);
      ascii().addNote("_");
      input->seek(pos+64, librevenge::RVNG_SEEK_SET);
      continue;
    }

    int val=int(input->readULong(2)); // 1 or 100
    if (val!=1) f << "f0=" << val << ",";
    librevenge::RVNGString name;
    if (!readString(name, 28, true))
      f << "##name,";
    else if (!name.empty())
      f << name.cstr() << ",";
    input->seek(pos+38, librevenge::RVNG_SEEK_SET);
    MWAWEntry entry;
    entry.setBegin(entries[1].begin()+input->readLong(4));
    entry.setLength(input->readLong(4));
    f << "unkn=" << std::hex << input->readULong(4) << std::dec << ",";
    val=int(input->readLong(4));
    if (val!=n)
      f << "id0=" << val << ",";
    int val2=int(input->readLong(4));
    if (val2!=val)
      f << "id1=" << val2 << ",";
    entry.setId(int(input->readLong(2)));
    if (entry.valid()) {
      f << std::hex << entry.begin() << "<->" << entry.end() << std::dec << entry << ",";
      if (entry.end()<=endPos) {
        static std::map<std::string, int> const nameToId= {
          { "Page", 0}, {"PSST", 1}, {"DevM", 2}, {"CSet", 3}, {"CVal", 4}, {"CNam", 5},
          { "FLDF", 6}
        };
        int wh=nameToId.find(types[1])!=nameToId.end() ? nameToId.find(types[1])->second : -1;
        bool done=false;
        long actPos=input->tell();
        switch (wh) {
        case 0:
          done=readPage(entry);
          break;
        case 1:
          done=readPrinterSST(entry);
          break;
        case 2:
          done=readPrinterDev(entry);
          break;
        case 3:
          done=readCSet(entry);
          break;
        case 4:
          done=m_styleManager->readColorValues(entry);
          break;
        case 5:
          done=readCNam(entry);
          break;
        case 6: {
          MWAWGraphicStyle::Gradient gradient;
          done=m_styleManager->readGradient(entry, gradient);
          break;
        }
        default:
          break;
        }
        input->seek(actPos, librevenge::RVNG_SEEK_SET);
        if (!done) {
          libmwaw::DebugStream f2;
          f2 << "RSRCData-" << types[1] << "[" << entry.id() << "]:";
          ascii().addPos(entry.begin());
          ascii().addNote(f2.str().c_str());
        }
      }
      else
        f << "###";
    }
    ascii().addDelimiter(input->tell(),'|');
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+64, librevenge::RVNG_SEEK_SET);
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool CanvasParser::readCNam(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<256) {
    MWAW_DEBUG_MSG(("CanvasParser::readCNam: the zone seems too small\n"));
    return false;
  }
  // checkme: find always an empty zone
  libmwaw::DebugStream f;
  f << "Entries(CNam)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());

  for (int st=0; st<2; ++st) {
    long pos=input->tell();
    f.str("");
    f << "CNam-" << st << ":";
    for (int i=0; i<64; ++i) {
      int val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool CanvasParser::readCSet(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<160) {
    MWAW_DEBUG_MSG(("CanvasParser::readCSet: the zone seems too small\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(CSet)[" << entry.id() << "]:";
  input->seek(entry.begin()+31,librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(), '|');
  librevenge::RVNGString name;
  if (!readString(name, 128, true)) // find Default
    f << "##name,";
  else if (!name.empty())
    f << name.cstr() << ",";
  input->seek(entry.begin()+31+128, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(), '|');
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readPage(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<936) {
    MWAW_DEBUG_MSG(("CanvasParser::readPage: the zone seems too small\n"));
    return false;
  }
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(Page)[" << entry.id() << "]:";
  int val;
  for (int i=0; i<64; ++i) { // f0=1
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());

  long pos=input->tell();
  f.str("");
  f << "Page-A0:";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  librevenge::RVNGString name;
  if (!readString(name, 128, true))
    f << "##name,";
  else if (!name.empty())
    f << "printer=" << name.cstr() << ",";
  input->seek(pos+4+128, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Page-A1:";
  for (int i=0; i<64; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Page-A2:";
  for (int i=0; i<18; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {
      1/*or 7*/,1,0,8,0,
      0xe/*or 0x13*/,0, 0x30f, 0, 0x255/* or 251*/,
      0, 0x318, 0, 0x264, 0/* or 4*/,
      0,1,1
    };
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<46; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+128, librevenge::RVNG_SEEK_SET);

  for (int wh=3; wh<7; ++wh) {
    pos=input->tell();
    f.str("");
    f << "Page-A" << wh << ":";
    for (int i=0; i<(wh==6 ? 18 : 64); ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  return true;
}

bool CanvasParser::readPrinterDev(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<198) {
    MWAW_DEBUG_MSG(("CanvasParser::readPrinterDev: the zone seems too small\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Printer)[Dev," << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  librevenge::RVNGString name;
  if (!readString(name, 32, true))
    f << "##name,";
  else if (!name.empty())
    f << name.cstr() << ",";
  input->seek(entry.begin()+32, librevenge::RVNG_SEEK_SET);

  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  input->seek(entry.begin()+128, librevenge::RVNG_SEEK_SET);

  long pos=input->tell();
  f.str("");
  f << "Printer-A[Dev]:";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool CanvasParser::readPrinterSST(MWAWEntry const &entry)
{
  auto input=getInput();
  if (!input || !entry.valid() || !input->checkPosition(entry.end()))
    return false;
  if (entry.length()<113) {
    MWAW_DEBUG_MSG(("CanvasParser::readPrinterSST: the zone seems too small\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Printer)[" << entry.id() << "]:";
  input->seek(entry.begin(),librevenge::RVNG_SEEK_SET);
  for (int i=0; i<4; ++i) { // find FILE:, "", driver file, driver name
    long pos=input->tell();
    librevenge::RVNGString name;
    if (!readString(name, 25, true))
      f << "##name,";
    else if (!name.empty())
      f << "text" << i << "=" << name.cstr() << ",";
    input->seek(pos+25, librevenge::RVNG_SEEK_SET);
  }
  int val;
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(i==3 ? 1 : 2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {1, 0xc6 /*or c8 or 34c */, 1};
    if (val!=expected[i]) f << "f" << i << "=" << val << ",";
  }
  ascii().addPos(entry.begin());
  ascii().addNote(f.str().c_str());
  return true;
}
////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////


bool CanvasParser::send(CanvasParserInternal::Layer const &layer)
{
  MWAWGraphicListenerPtr listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("CanvasParser::send[layer]: can not find the listener\n"));
    return false;
  }
  if (layer.m_shapesId.empty())
    return true;
  bool openLayer=false;
  if (!layer.m_name.empty())
    openLayer=listener->openLayer(layer.m_name);
  for (auto const &id : layer.m_shapesId)
    m_graphParser->sendShape(id);
  if (openLayer)
    listener->closeLayer();
  return true;
}


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
