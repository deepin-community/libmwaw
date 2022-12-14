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

#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "MWAWDebug.hxx"
#include "MWAWPrinter.hxx"

#include "RagTime5ClusterManager.hxx"
#include "RagTime5Document.hxx"
#include "RagTime5Formula.hxx"
#include "RagTime5Text.hxx"

#include "RagTime5StructManager.hxx"

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5StructManager::RagTime5StructManager(RagTime5Document &doc)
  : m_document(doc)
{
}

RagTime5StructManager::~RagTime5StructManager()
{
}

RagTime5StructManager::DataParser::DataParser(std::string const &zoneName)
  : m_name(zoneName)
{
  if (!m_name.empty())
    m_name[0]=char(std::toupper((unsigned char)(m_name[0])));
}

RagTime5StructManager::DataParser::~DataParser()
{
}

RagTime5StructManager::FieldParser::~FieldParser()
{
}
////////////////////////////////////////////////////////////
// read basic structures
////////////////////////////////////////////////////////////
bool RagTime5StructManager::readCompressedLong(MWAWInputStreamPtr &input, long endPos, long &val)
{
  val=long(input->readULong(1));
  if ((val&0xF0)==0xC0) {
    input->seek(-1, librevenge::RVNG_SEEK_CUR);
    val=long(MWAWInputStream::readULong(input->input().get(), 4, 0, false)&0xFFFFFFF);
  }
  else if (val>=0xD0) { // never seems, but may be ok
    MWAW_DEBUG_MSG(("RagTime5Struct::readCompressedLong: can not read a long\n"));
    return false;
  }
  else if (val>=0x80)
    val=((val&0x7F)<<8)+long(input->readULong(1));
  return input->tell()<=endPos;
}

std::string RagTime5StructManager::printType(unsigned long fileType)
{
  static std::map<unsigned long, char const *> const s_typeToName = {
    {0x145e042, "fillStyle[container]"},
    {0x1460042, "lineStyle[container]"},
    {0x146902a, "unit[base,from]"},
    {0x146903a, "unit[base,to]"},
    {0x146904a, "unit[base,id]"},
    {0x146905a, "unit[name]"},
    {0x146907a, "unit[second,id]"}, // rare
    {0x146908a, "unit[digits,place]"},
    {0x146a042, "unit[container]"},
    {0x146e02a, "ruler[unit,id]"},
    {0x146e03a, "ruler[step,major]"},
    {0x146e04a, "ruler[step,minor]"},
    {0x146e05a, "ruler[grid/major]"}, // a fraction or a fixed decimal?
    {0x146e06a, "ruler[grid,line/gridPoint]"},
    {0x146f042, "ruler[container]"},
    {0x17d5042, "color[container]"},

    // functions collections
    {0x14c2042, "functions[layout]"},
    {0x1559842, "functions[standart]"},
    {0x1663842, "functions[spreadsheet]"},
    {0x1be5042, "functions[fax]"},
    {0x1d50842, "functions[button]"},
    {0x1e16842, "functions[slide]"},
    {0x23aa042, "functions[calendar]"},
    {0x23af042, "functions[serialNumber]"},
    {0x23b4042, "functions[euro]"},
  };
  auto it=s_typeToName.find(fileType);
  if (it!=s_typeToName.end())
    return it->second;
  std::stringstream s;
  s << (fileType>>11) << "-" << std::hex << (fileType&0x7ff) << std::dec;
  return s.str();
}

bool RagTime5StructManager::readTypeDefinitions(RagTime5Zone &zone)
{
  if (zone.m_entry.length()<26) return false;
  MWAWInputStreamPtr input=zone.getInput();
  long endPos=zone.m_entry.end();
  input->setReadInverted(!zone.m_hiLoEndian);
  input->seek(zone.m_entry.begin(), librevenge::RVNG_SEEK_SET);

  libmwaw::DebugStream f;
  f << "Entries(TypeDef):[" << zone << "]";
  long N;
  if (!input->checkPosition(endPos) || !RagTime5StructManager::readCompressedLong(input, endPos, N) || N<20 || 12+14*N>zone.m_entry.length()) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: can not read the list type zone\n"));
    input->setReadInverted(false);
    return false;
  }
  zone.m_isParsed=true;
  libmwaw::DebugFile &ascFile=zone.ascii();
  f << "N=" << N << ",";
  int val;
  for (int i=0; i<2; ++i) { // always 0,0
    val=static_cast<int>(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  auto sz=static_cast<int>(input->readULong(1));
  if (input->tell()+sz>endPos) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: can not find the data1 sz\n"));
    return false;
  }
  if (sz) {
    f << "data1=[" << std::hex;
    for (int i=0; i<sz+1; ++i) {
      val=static_cast<int>(input->readULong(1));
      if (val) f << val << ",";
      else f << "_,";
    }
    f << std::dec << "],";
  }
  long debDataPos=input->tell()+4*(N+1);
  long remain=endPos-debDataPos;
  if (remain<=0) {
    input->setReadInverted(false);
    return false;
  }
  f << "ptr=[" << std::hex;
  std::vector<long> listPtrs(size_t(N+1), -1);
  long lastPtr=0;
  int numOk=0;
  for (size_t i=0; i<=size_t(N); ++i) {
    auto ptr=long(input->readULong(4));
    if (ptr<0 || ptr>remain || ptr<lastPtr) {
      f << "###";
      static bool first=true;
      if (first) {
        first = false;
        MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: problem reading some type position\n"));
      }
      listPtrs[size_t(i)]=lastPtr;
    }
    else {
      ++numOk;
      lastPtr=listPtrs[size_t(i)]=ptr;
    }
    f << ptr << ",";
  }
  f << std::dec << "],";
  ascFile.addPos(zone.m_entry.begin());
  ascFile.addNote(f.str().c_str());
  ascFile.addPos(endPos);
  ascFile.addNote("_");
  if (!numOk) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: problem reading some type position\n"));
    input->setReadInverted(false);
    return false;
  }

  // first 10 main types are the same:
  //   0: 1451042, 1:6d042, 2:cf042, 3:1454042, 4:74040, 5:df842, 6:ce042, 7:3c042, 8:ce842, 9:67842, 10:6a842
  // Component correspondance
  // 14b5842: layout, 14b7842: master layout,
  // 14e6842: drawing
  // 15e0842: Text
  // 1645042: spreadsheet
  // 16a8842: Graph
  // 170c842: Picture
  // 1d4d042: Button
  // 1d7f842: Sound
  // 1db0842: Movie
  for (size_t i=0; i+1<listPtrs.size(); ++i) {
    if (listPtrs[i]<0 || listPtrs[i]==listPtrs[i+1]) continue;
    if (listPtrs[i]==remain) break;
    f.str("");
    f << "TypeDef-" << i << "[head]:";
    auto dSz=int(listPtrs[i+1]-listPtrs[i]);
    long pos=debDataPos+listPtrs[i];
    if (dSz<4+20) {
      f << "###";
      MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: problem with some type size\n"));
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    val=static_cast<int>(input->readLong(4));
    if (val!=1) f << "num[used]=" << val << ",";
    auto hSz=static_cast<int>(input->readULong(2));
    int nData=(dSz-4-hSz)/12;
    if (4+hSz>dSz || dSz!=4+hSz+12*nData || nData<0 || hSz<20) {
      f << "###hSz=" << hSz << ",";
      MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: the header size seems bad\n"));
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    val=static_cast<int>(input->readULong(2));
    if (val) f << "fl=" << std::hex << val << std::dec << ",";
    val=static_cast<int>(input->readULong(4));
    if (val) f << "id?=" << std::hex << val << std::dec << ","; // maybe a date, origin?
    auto type=input->readULong(4);
    if (type) f << "type=" << printType(type) << ",";
    for (int j=0; j<2; ++j) { // f1=0..12,
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f" << j+1 << "=" << val << ",";
    }
    auto type2=input->readULong(4);
    if (type2) f << "type2=" << printType(type2) << ",";
    if (hSz>20) {
      auto sSz=static_cast<int>(input->readULong(1));
      if ((sSz%2)!=0 || 20+1+sSz>hSz) {
        f << "###sSz=" << sSz << ",";
        MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: the string size seems bad\n"));
      }
      else { // Layout, Text, Picture, .. a componant name
        std::string name("");
        for (int c=0; c<sSz/2; ++c)
          name+=char(input->readULong(2));
        f << "name=" << name << ",";
        if (type2!=0x14a9842) {
          f << "###type2,";
          MWAW_DEBUG_MSG(("RagTime5StructManager::readTypeDefinitions: find odd type2 with a string size\n"));
        }
      }
    }
    if (input->tell()!=pos+4+hSz)
      ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());

    input->seek(pos+4+hSz, librevenge::RVNG_SEEK_SET);
    for (int j=0; j<nData; ++j) {
      pos=input->tell();
      f.str("");
      f << "TypeDef-" << i << "[" << j << "]:";
      f << "type=" << printType(input->readULong(4)) << ","; // a big number, ~type (but a multiple of 5)
      f << "type2=" << printType(input->readULong(4)) << std::dec << ","; // a big number
      for (int k=0; k<2; ++k) {
        val=static_cast<int>(input->readULong(1));
        if (val) f << "fl" << k << "=" << std::hex << val << std::dec << ",";
      }
      val=static_cast<int>(input->readLong(2));
      if (val) f << "f0=" << val << ",";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      input->seek(pos+12, librevenge::RVNG_SEEK_SET);
    }
  }
  if (!listPtrs.empty() && listPtrs.back()!=remain) {
    ascFile.addPos(debDataPos+listPtrs.back());
    ascFile.addNote("TypeDef-end");
  }
  input->setReadInverted(false);
  return true;
}


bool RagTime5StructManager::readUnicodeString(MWAWInputStreamPtr input, long endPos, librevenge::RVNGString &string)
{
  string="";
  long pos=input->tell();
  if (pos==endPos) return true;
  long length=endPos-pos;
  if (length<0 || (length%2)==1) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readUnicodeString: find unexpected data length\n"));
    return false;
  }
  length/=2;
  int lEndian=0, hEndian=0; // for checking if little/big endian is set correctly
  for (long i=0; i<length; ++i) {
    auto c=static_cast<uint32_t>(input->readULong(2));
    if ((c&0xFF00)==0) ++hEndian;
    else if ((c&0xFF)==0) ++lEndian;
    if (c!=0)
      libmwaw::appendUnicode(c, string);
  }
  if (lEndian>hEndian) {
    static bool first=true;
    if (first) {
      first=false;
      MWAW_DEBUG_MSG(("RagTime5StructManager::readUnicodeString: the endian reading seems bad...\n"));
    }
  }
  return true;
}

bool RagTime5StructManager::readDataIdList(MWAWInputStreamPtr input, int n, std::vector<int> &listIds)
{
  listIds.clear();
  long pos=input->tell();
  for (int i=0; i<n; ++i) {
    int val=static_cast<int>(MWAWInputStream::readULong(input->input().get(), 2, 0, false));
    if (val==0) {
      listIds.push_back(0);
      input->seek(2, librevenge::RVNG_SEEK_CUR);
      continue;
    }
    if (val!=1) {
      // update the position
      input->seek(pos+4*n, librevenge::RVNG_SEEK_SET);
      return false;
    }
    listIds.push_back(static_cast<int>(MWAWInputStream::readULong(input->input().get(), 2, 0, false)));
  }
  return true;
}

bool RagTime5StructManager::readField(MWAWInputStreamPtr input, long endPos, libmwaw::DebugFile &ascFile,
                                      RagTime5StructManager::Field &field, long fSz)
{
  libmwaw::DebugStream f;
  long debPos=input->tell();
  if ((fSz>0 && (fSz<4 || debPos+fSz>endPos)) || (fSz<=0 && debPos+5>endPos)) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readField: the zone seems too short\n"));
    return false;
  }
  auto type=input->readULong(4);
  if ((type>>16)==0) {
    input->seek(debPos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  field.m_fileType=type;
  bool complex=static_cast<int>(input->readULong(1))==0xc0;
  input->seek(-1, librevenge::RVNG_SEEK_CUR);
  if (fSz<=0) {
    if (!readCompressedLong(input, endPos, fSz) || fSz<=0 || input->tell()+fSz>endPos) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read the data size\n"));
      input->seek(debPos, librevenge::RVNG_SEEK_SET);
      return false;
    }
  }
  else
    fSz-=4;
  long debDataPos=input->tell();
  long endDataPos=debDataPos+fSz;
  switch (static_cast<unsigned long>(type)) {
  case 0x360c0:
  case 0x368c0:
    if (fSz!=1) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for bool\n"));
      f << "###bool,";
      break;
    }
    field.m_type=Field::T_Bool;

    field.m_name="bool";
    field.m_longValue[0]=long(input->readLong(1));
    return true;

  case 0x328c0:
    if (fSz!=1) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for bInt\n"));
      f << "###bInt,";
      break;
    }
    field.m_type=Field::T_Long;
    field.m_name="bInt";
    field.m_longValue[0]=long(input->readLong(1));
    return true;

  case 0x3b880:
  case 0x1479080:
  case 0x147b880:
  case 0x147c080:
  case 0x149d880:
  case 0x149e080:
  case 0x17d5880:
    if (fSz!=2) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for int\n"));
      f << "###int,";
      break;
    }
    field.m_type=Field::T_Long;
    field.m_name="int";
    field.m_longValue[0]=long(input->readLong(2));
    return true;
  case 0x34080:
  case 0xcf817: // bigger int dataId?: not in typedef
    if (fSz!=2) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for uint\n"));
      f << "###uint,";
      break;
    }
    field.m_type=Field::T_Long;
    field.m_name="uint";
    field.m_longValue[0]=long(input->readULong(2));
    return true;
  case 0xb6000: // color percent
  case 0x1493800: // checkme double(as int)
  case 0x1494800: // checkme double(as int)
  case 0x1495000:
  case 0x1495800: // checkme double(as int)
    if (fSz!=4) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for double4\n"));
      f << "###float,";
      break;
    }
    // checkme if val=0xFFFFFFFF, inf?
    field.m_type=Field::T_Double;
    field.m_name="double4";
    field.m_doubleValue=double(input->readLong(4))/65536;
    return true;
  case 0x45840: {
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for double\n"));
      f << "###double,";
      break;
    }
    double res;
    bool isNan;
    if (!input->readDouble8(res, isNan)) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read a double\n"));
      f << "###double";
      break;
    }
    field.m_type=Field::T_Double;
    field.m_name="double";
    field.m_doubleValue=res;
    return true;
  }
  case 0x14510b7: {
    std::vector<int> listIds;
    if (!readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read the cluster id\n"));
      f << "###clustId,";
      break;
    }
    field.m_name="colPatId";
    field.m_type=Field::T_ZoneId;
    field.m_longValue[0]=listIds[0];
    return true;
  }
  case 0x34800:
  case 0x8c000: // a dim
  case 0x234800:  // checkme, find always with 0x0
  case 0x147415a: // checkme, find always with 0x0
  case 0x15e3017: // 2 long, not in typedef
    if (fSz!=4) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for 2xint\n"));
      f << "###2xint,";
      break;
    }
    field.m_type=Field::T_2Long;
    field.m_name="2xint";
    field.m_longValue[0]=long(input->readLong(2));
    field.m_longValue[1]=long(input->readLong(2));
    return true;
  case 0x7d01a:
  case 0xc8042: { // unicode
    if (fSz<2) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for unicode\n"));
      f << "###unicode";
      break;
    }
    field.m_type=Field::T_Unicode;
    field.m_name="unicode";
    auto val=static_cast<int>(input->readULong(2));
    if ((val&0x70FF)==0) {
      if (val) f << "f1=" << ((val&0x7F00)>>8);
      for (long i=2; i<fSz; ++i)
        libmwaw::appendUnicode(static_cast<uint32_t>(input->readULong(1)), field.m_string);
    }
    else {
      input->seek(debDataPos, librevenge::RVNG_SEEK_SET);
      if (!readUnicodeString(input, endDataPos, field.m_string))
        f << "###";
    }
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    field.m_extra=f.str();
    return true;
  }
  case 0x1f6817:
  case 0x1f6827:
  case 0x1f7877: { // CHECKME: is this also valid for unicode ?
    if (fSz<2) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for int+unicode\n"));
      f << "###int+unicode";
      break;
    }
    field.m_type=Field::T_Unicode;
    field.m_name="int+unicode";
    int val=int(input->readULong(2));
    if ((val&0xff) || val==0xff00) { // ff00 means list of {length+str} for normal string
      f << "multistring,";
      f << "val=" << std::hex << val << std::dec << ",";
      val=static_cast<int>(input->readULong(2));
    }
    //std::cout << "ICI1:" << std::hex << val << std::dec << "\n";
    if ((val&0x8000)==0) {
      if ((val>>8)>2) f << "f1=" << (val>>8);
      fSz=int(endDataPos-input->tell());
      for (long i=0; i<fSz; ++i) {
        auto c=static_cast<uint32_t>(input->readULong(1));
        if (c)
          libmwaw::appendUnicode(c, field.m_string);
        else
          field.m_string.append('#');
      }
    }
    else if (!readUnicodeString(input, endDataPos, field.m_string)) {
      field.m_string="###";
    }
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    field.m_extra=f.str();
    return true;
  }
  case 0x149a940: {
    if (fSz!=6) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for interline\n"));
      f << "###interline";
      break;
    }
    field.m_type=Field::T_LongDouble;
    field.m_name="interline";
    field.m_longValue[0]=static_cast<int>(input->readLong(2));
    field.m_doubleValue=double(input->readLong(4))/65536.;
    return true;
  }
  case 0x149c940: { // checkme
    if (fSz!=6) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for floatxint\n"));
      f << "###floatxint";
      break;
    }
    field.m_type=Field::T_LongDouble;
    field.m_name="floatxint";
    field.m_doubleValue=double(input->readLong(4))/65536.;
    field.m_longValue[0]=static_cast<int>(input->readLong(2));
    return true;
  }
  case 0x74040: {
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for 2xfloat\n"));
      f << "###2xfloat";
      break;
    }
    field.m_type=Field::T_DoubleList;
    field.m_name="2xfloat";
    for (int i=0; i<2; ++i)
      field.m_doubleList.push_back(double(input->readLong(4))/65536);
    return true;
  }
  case 0x1474040: // maybe one tab
  case 0x81474040: {
    if ((fSz%8)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for tab\n"));
      f << "###tab[list]";
      break;
    }
    field.m_type=Field::T_TabList;
    field.m_name="tab";
    auto N=int(fSz/8);
    for (int i=0; i<N; ++i) {
      TabStop tab;
      tab.m_position=float(input->readLong(4))/65536.f;
      tab.m_type=static_cast<int>(input->readLong(2));
      tab.m_leaderChar=static_cast<uint16_t>(input->readULong(2));
      field.m_tabList.push_back(tab);
    }
    return true;
  }
  case 0x74840: // dimension
  case 0x112040: // also some dimension ? ( often 0,0,0,1 but can be 0,-0.00564575,0,0.25 )
    if (fSz!=16) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for dim\n"));
      f << "###dim";
      break;
    }
    field.m_type=Field::T_DoubleList;
    field.m_name="dim";
    for (long i=0; i<4; ++i)
      field.m_doubleList.push_back(double(input->readLong(4))/65536.);
    field.m_extra=f.str();
    return true;

  case 0x1476840:
    if (fSz!=10) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for 3intxfloat\n"));
      f << "###3intxfloat";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="3intxfloat";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    for (long i=0; i<3; ++i) { // 1|3,1,1
      auto val=static_cast<int>(input->readLong(2));
      if (val) f << val << ":";
      else f << "_:";
    }
    f << double(input->readLong(4))/65536. << ",";
    field.m_extra=f.str();
    return true;
  case 0x79040: { // checkme: unsure
    if (fSz!=14) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for 0x79040\n"));
      f << "###unstr";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="unstr";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    for (long i=0; i<2; ++i) { // something like 49c4x6c2b
      f << std::hex << input->readULong(4) << std::dec;
      if (i==0) f << "x";
      else f << ",";
    }
    for (int i=0; i<2; ++i) { // often 4000, 3fed
      f << std::hex << input->readULong(2) << std::dec;
      if (i==0) f << "x";
      else f << ",";
    }
    auto val=static_cast<int>(input->readLong(2));
    if (val!=0x100) f << val << ":";
    field.m_extra=f.str();
    return true;
  }
  case 0x84040: {
    if (fSz!=10) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for rgba\n"));
      f << "###rgba";
      break;
    }
    field.m_type=Field::T_Color;
    field.m_name="rgba";
    field.m_longValue[0]=long(input->readLong(2)); // id or numUsed
    if (field.m_longValue[0]==50) {
      field.m_longValue[1]=long(input->readULong(2));
      field.m_color=MWAWColor(255,255,255);
      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
      return true;
    }
    unsigned char col[4];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8); // rgba
    field.m_color=MWAWColor(col[0],col[1],col[2],col[3]);
    return true;
  }
  case 0x8d000: {
    if (fSz!=4) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for rsrcName\n"));
      f << "###rsrcName";
      break;
    }
    field.m_type=Field::T_Code;
    field.m_name="rsrcName";
    auto cod=input->readULong(4);
    for (int i=0; i<4; ++i) field.m_string.append(char(cod>>(24-8*i)));
    return true;
  }
  case 0x31e040:
    // <!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    if (fSz<30) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for xml printer\n"));
      f << "###printInfoX";
      break;
    }
    field.m_type=Field::T_String;
    field.m_name="printInfoX";
    for (long i=0; i<fSz && i<30; ++i)
      field.m_string.append(char(input->readULong(1)));
    field.m_string.append("...");
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  case 0x2fd040: {
    if (fSz<120) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for printInfo\n"));
      f << "###printInfo";
      break;
    }
    libmwaw::PrinterInfo info;
    if (!info.read(input)) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read printInfo\n"));
      f << "###printInfo";
      break;
    }
    field.m_type=Field::T_PrintInfo;
    field.m_name="printInfo";
    f << info << ",";
    field.m_extra=f.str();
    // then sometimes 4 string title, ...
    if (input->tell()!=endDataPos)
      ascFile.addDelimiter(input->tell(),'|');
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  case 0x333140: // AppleWriter pref, probably safe to ignore
    if (fSz!=908) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for AppleWriter prefs\n"));
      f << "###appleWriterInfo";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="appleWriterInfo";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    // name in lohi ?
    for (int i=0; i<32; ++i) {
      auto c=static_cast<int>(input->readULong(1));
      c+=static_cast<int>(input->readULong(1)<<8);
      if (c==0) break;
      f << char(c);
    }
    f << "...";
    field.m_extra=f.str();
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  case 0x148c01a: // 2 int + 8 bytes for pat ?
    if (fSz!=12) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for pat\n"));
      f << "###pat";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="pat";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    field.m_extra="...";
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  case 0x226140:
    if (fSz!=21) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for font\n"));
      f << "###font";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="font";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    field.m_extra="...";
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  case 0x226940: { // checkme
    if (fSz<262 || ((fSz-262)%6)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for para\n"));
      f << "###para";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="para";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    field.m_extra="...";
    ascFile.addPos(input->tell()+70);
    ascFile.addNote("TextStyle-para-B0:");
    ascFile.addPos(input->tell()+166);
    ascFile.addNote("TextStyle-para-B1:");
    if (fSz>262) {
      ascFile.addPos(input->tell()+262);
      ascFile.addNote("TextStyle-para-C:");
    }
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  case 0x71940: // checkme: locale data ?
    if (fSz!=108) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for locale\n"));
      f << "###locale";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="locale";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    f << "fls=[" << std::hex;
    for (long i=0; i<8; ++i) {
      auto val=static_cast<int>(input->readLong(1));
      if (val==1) f << "_,";
      else f << val << ",";
    }
    f << "],";
    f << "chars=[";
    for (long i=0; i<21; ++i) {
      auto val=static_cast<int>(input->readULong(2));
      if (!val) f << "_,";
      else if (val<128) f << char(val) << ",";
      else f << std::hex << val << std::dec << ",";
    }
    f << "],";
    ascFile.addDelimiter(input->tell(),'|');
    field.m_extra=f.str();
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  case 0x32040:
    if (fSz<160) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: the data fSz for docInfo data seems too short\n"));
      f << "###docInfo";
      break;
    }
    else {
      field.m_type=Field::T_Unstructured;
      field.m_name="docInfo";
      field.m_entry.setBegin(input->tell());
      field.m_entry.setEnd(endDataPos);
      field.m_extra="...";
      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
      return true;
    }
  case 0x227140: { // border checkme
    if ((fSz%6)!=2) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of border\n"));
      f << "###border";
      break;
    }
    auto val=static_cast<int>(input->readULong(2)); // c000 or c1000
    if (val!=0xc000) f << "f0=" << std::hex << val << std::dec << ",";
    field.m_type=Field::T_LongList;
    field.m_name="border";
    field.m_numLongByData=3;
    fSz/=6;
    for (long i=0; i<fSz; ++i) {
      field.m_longList.push_back(long(input->readLong(2))); // row?
      field.m_longList.push_back(long(input->readLong(2))); // col?
      field.m_longList.push_back(long(input->readULong(2))); // flags?
    }
    field.m_extra=f.str();
    return true;
  }
  case 0x64040: { // chart pref
    if (fSz<3) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for chart pref\n"));
      f << "###chart pref";
      break;
    }
    field.m_type=Field::T_FieldList;
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    for (int i=0; i<3; ++i) { // f2=8|9
      auto val=static_cast<int>(input->readLong(1));
      if (val) f << "f" << i << "=" << val << ",";
    }
    bool ok=true;
    while (input->tell()<endDataPos) {
      Field child;
      long pos=input->tell();
      if (!readField(input, endDataPos, ascFile, child)) {
        ok=false;
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      field.m_fieldList.push_back(child);
    }
    if (!ok || input->tell() != endDataPos) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read some charr pref list data\n"));
      f.str("");
      f << "###pos=" << input->tell()-debPos;
      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    }
    field.m_extra=f.str();
    return true;
  }
  case 0xce017: { // unstructured: not in typedef
    if (fSz<5) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for unstructured\n"));
      f << "###unstr";
      break;
    }
    field.m_type=Field::T_Unstructured;
    field.m_name="unstruct";
    field.m_longValue[0]=input->readLong(4);
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    f << "data=" << std::hex;
    for (long i=0; i<fSz-4; ++i)
      f << std::setfill('0') << std::setw(2) << static_cast<int>(input->readULong(1));
    f << std::dec << ",";
    field.m_extra=f.str();
    return true;
  }
  case 0xce842:  // list of long : header fl=2000, f2=7
  case 0x170c8e5: { // maybe list of color: f0=418,fl1=40,fl2=8
    if ((fSz%4)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of long\n"));
      f << "###";
      break;
    }
    fSz/=4;
    field.m_type=Field::T_LongList;
    field.m_name="longList";
    for (long i=0; i<fSz; ++i)
      field.m_longList.push_back(long(input->readLong(4)));
    return true;
  }
  case 0x3c057:
  case 0x80045080: // child of 14741fa
  case 0xcf042: { // list of small int: header fl=2000 with f2=9
    if ((fSz%2)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of long\n"));
      f << "###";
      break;
    }
    fSz/=2;
    field.m_type=Field::T_LongList;
    field.m_name="intList";
    for (long i=0; i<fSz; ++i)
      field.m_longList.push_back(long(input->readLong(2)));
    return true;
  }
  case 0x154f017: // list of bytes
    field.m_type=Field::T_LongList;
    field.m_name="byteList";
    for (long i=0; i<fSz; ++i)
      field.m_longList.push_back(long(input->readLong(1)));
    return true;
  case 0x35000: // checkme find also a string code here...
  case 0x35800: // unsure, ie small int
  case 0x3e800: // unsure, find with 000af040, 00049840 or 0004c040
  case 0xa4000:
    if (fSz!=4) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of uint32_t\n"));
      f << "###uint32";
      break;
    }
    field.m_type=Field::T_Long;
    field.m_longValue[0]=long(input->readULong(4));
    return true;
  case 0xa4840:
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of 2xuint32_t\n"));
      f << "###2xuint32";
      break;
    }
    field.m_type=Field::T_2Long;
    field.m_name="2xuint32";
    for (long &i : field.m_longValue)
      i=long(input->readULong(4));
    return true;

  case 0x33000: // maybe one 2xint
  case 0x1671817:
  case 0x16b491a: // in chart,
  case 0x16b492a: // in chart, dim[4byte], id, 0
  case 0x16b5aea: // chart preference, always 0,0,id,0
  case 0x80033000:
    if ((fSz%4)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for list of 2xint\n"));
      f << "###";
      break;
    }
    field.m_type=Field::T_LongList;
    field.m_name="2intList";
    field.m_numLongByData=2;
    fSz/=2;
    for (long i=0; i<fSz; ++i)
      field.m_longList.push_back(long(input->readLong(2)));
    return true;
  case 0x81452040: {
    if (fSz!=8) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for id+long\n"));
      f << "###clustIdxlong";
      break;
    }
    field.m_type=Field::T_ZoneId;
    field.m_name="clustIdxlong";
    field.m_entry.setBegin(input->tell());
    field.m_entry.setEnd(endDataPos);
    std::vector<int> listIds;
    if (!readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read the cluster id for id+long\n"));
      f << "###clustId,";
      break;
    }
    field.m_longValue[0]=listIds[0];
    unsigned long val= input->readULong(4); // always 90000001
    if (val!=0x90000001) f << std::hex << val << std::dec << ",";
    field.m_extra=f.str();
    return true;
  }
  case 0xa7017: // unicode
  case 0xa7027:
  case 0xa7037:
  case 0xa7047:
  case 0xa7057:
  case 0xa7067:
  case 0x146905a: // unicode

  case 0x7a047: // font definition
  case 0x7a057: // ?? definition
  case 0x7a067: // ?? definition

  case 0x146005a: // code
  case 0x146007a:
  case 0x14600aa:
  case 0x147403a:
  case 0x14740ba:
  case 0x147501a:
  case 0x148981a:

  case 0x145e0ba: // bool
  case 0x147406a:
  case 0x147550a:
  case 0x17d486a:

  case 0x147512a: // small int

  case 0xa7077: // with type=3b880
  case 0x145e01a:
  case 0x146904a: // int with type=0x149d880
  case 0x146907a: // withe type = 149d880
  case 0x146908a: // with type=0x3b880
  case 0x1469840: // with type=147b88
  case 0x146e02a:
  case 0x146e03a:
  case 0x146e04a:
  case 0x146e06a:
  case 0x146e08a:
  case 0x145e11a: // with type=0x17d5880
  case 0x145e12a: // with type=017d5880
  case 0x147407a:
  case 0x147408a:
  case 0x1474042:
  case 0x147416a:
  case 0x14741ea:
  case 0x147420a: // with type=3b880
  case 0x14754aa: // with type=328c0
  case 0x147551a: // with type=1479080
  case 0x147e81a: // with type=3b880
  case 0x17d481a: // int

  case 0x7d04a:
  case 0x147405a:
  case 0x14741ca: // 2 long
  case 0x145e02a: // with type=b600000
  case 0x14741ba: // with type=b600000
  case 0x145e0ea:
  case 0x146008a:
  case 0x14752da: // with type=1495000
  case 0x14740ea: // with type=b600000
  case 0x147536a: // with type=1495000
  case 0x147538a: // with type=1495000
  case 0x146902a: // double
  case 0x146903a: // double
  case 0x17d484a: // with type=34800
  case 0x147404a: // with type=149c94
  case 0x7d02a: // rgba color?
  case 0x145e05a:

  case 0x14750ea: // keep with next para
  case 0x147530a: // break behavior
  case 0x14753aa: // min word spacing
  case 0x14753ca: // optimal word spacing
  case 0x14753ea: // max word spacing
  case 0x147546a: // number line in widows
  case 0x147548a: // number line in orphan
  case 0x147552a: // do not use spacing for single word
  case 0x147516a: // align paragraph on grid
  case 0x147418a: // small caps scaling x
  case 0x14741aa: // small caps scaling y

  case 0x14c2042: /* function container*/
  case 0x1559842:
  case 0x1663842:
  case 0x1be5042:
  case 0x1d50842:
  case 0x1e16842:
  case 0x23aa042:
  case 0x23af042:
  case 0x23b4042:

  case 0x7a09a: // with a4000 or a4840
  case 0x7a05a: // unknown
  case 0x7a0aa:
  case 0x14600ca: // with type=80033000
  case 0x146e05a: // with type=33000
  case 0x147402a:
  case 0x14741fa: // unsure find with type=80045080
  case 0x147502a: // with type=149a940
  case 0x147505a: // with type=1493800
  case 0x147506a: // with type=1493800
  case 0x147507a: // with type=1493800
  case 0x14750aa: // with type=149a940
  case 0x14750ba: // with type=149a940
  case 0x14750ca: // with type=1474040
  case 0x147510a: // with type=81474040 or 1474040
  case 0x147513a: // with type=1493800
  case 0x14754ba: // with type=1476840
  case 0x148983a: // with type=0074040
  case 0x148985a: // with type=1495800

  case 0x16c1825: // chart pref with type=0042040 CHECKME
  case 0x16d5840: // chart main pref

  // docinfo zone
  case 0x1f7817:
  case 0x1f7827:
  case 0x1f7837:
  case 0x1f7847:
  case 0x1f7857:
  case 0x1f7887:

  // gobj property
  case 0x6615a:
  case 0x6616a:
  case 0x6617a:
  case 0x6619a:
  case 0xfd827:
  case 0x10581a:
  case 0x111817:
  case 0x111827:
  case 0x1467837:
  case 0x146789a:
  case 0x14678aa: {
    if (fSz<4) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected list field size\n"));
      f << "###list,";
      break;
    }

    long pos=input->tell();
    Field child;
    if (!readField(input, endDataPos, ascFile, child, fSz)) {
      f << "###pos=" << pos-debPos;
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    field.m_name="container";

    field.m_type=Field::T_FieldList;
    field.m_fieldList.push_back(child);
    if (input->tell() != endDataPos) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read some field list data\n"));
      f.str("");
      f << "###pos=" << pos-debPos;
      field.m_extra+=f.str();
      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    }
    return true;
  }

  // condition, function, ...
  case 0xe2c59:
  case 0x1a473a:
  case 0x1c58b1:
  case 0x1d5ab5:
  case 0x1dad60:
  case 0x1e1c3b:
  case 0x329eef:
  case 0x6604ee:
  case 0xcfdfc0:
  case 0x1466794:
  case 0x1468721:
  case 0x1919327:
  case 0x28b427c:
  case 0x2a72e5f:
  case 0x3217ef3: {
    if (fSz!=12) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for condition\n"));
      f << "###condition";
      break;
    }
    field.m_type=Field::T_CondColor;
    field.m_name="condition";
    field.m_longValue[0]=long(input->readLong(2)); // numUsed ?
    field.m_longValue[1]=long(input->readLong(2)); // formula id ?
    unsigned char col[4];
    for (auto &c : col) c=static_cast<unsigned char>(input->readULong(2)>>8); // rgba
    field.m_color=MWAWColor(col[0],col[1],col[2],col[3]);
    return true;
  }

  case 0x154a840: {
    if (fSz<6) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected field size for functions def list\n"));
      f << "###func[def],";
      break;
    }
    auto val=static_cast<int>(input->readLong(1)); // always 0?
    if (val) f << "f1=" << val << ",";
    auto N=static_cast<int>(input->readULong(1));
    if (6+N!=fSz) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected field N for functions def list\n"));
      f << "###func[def],N=" << N << ",";
      break;
    }
    for (int i=0; i<3; ++i) { // 0,-1|0|1,0
      val=static_cast<int>(input->readLong(1));
      if (!val) continue;
      f << "f" << i+2 << "=" << val << ",";
    }
    val=static_cast<int>(input->readULong(1)); //0|40|c0
    if (val) f << "f5=" << std::hex << val << std::dec << ",";
    // list of [0|1|20]*
    field.m_type=Field::T_LongList;
    field.m_name="func[def]";
    for (int i=0; i<N; ++i)
      field.m_longList.push_back(static_cast<int>(input->readULong(1)));
    field.m_extra=f.str();
    return true;
  }
  case 0x42040: {
    if (fSz<10) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected field size for day/month list\n"));
      f << "###list[day/month],";
      break;
    }
    int val;
    for (int i=0; i<2; ++i) { // always 0?
      val=static_cast<int>(input->readULong(2));
      if (val) f << "f" << i+1 << "=" << val << ",";
    }
    auto N=static_cast<int>(input->readULong(2));
    val=static_cast<int>(input->readULong(2)); // always 20 ?
    if (val!=20) f << "f3=" << val << ",";
    val=static_cast<int>(input->readULong(2)); // always 0 ?
    if (val) f << "f4=" << val << ",";
    field.m_type=Field::T_FieldList;
    field.m_name="container[list]"; // can be day/month, ...
    bool ok=true;
    for (int i=0; i<N; ++i) {
      Field child;
      long pos=input->tell();
      if (!readField(input, endDataPos, ascFile, child)) {
        ok=false;
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        break;
      }
      field.m_fieldList.push_back(child);
    }
    if (!ok || input->tell() != endDataPos) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read some container list data\n"));
      f.str("");
      f << "###pos=" << input->tell()-debPos;
      input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    }
    field.m_extra+=f.str();
    return true;
  }
  case 0xd7842: { // list of ? : header fl=0|4000, f2=3
    if ((fSz%6)!=0) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected data fSz for 0xd7842\n"));
      f << "###";
      break;
    }
    fSz/=2;
    field.m_type=Field::T_LongList;
    field.m_name="3unknList";
    field.m_numLongByData=3;
    for (long i=0; i<fSz; ++i)
      field.m_longList.push_back(long(input->readLong(2)));
    return true;
  }
  default: {
    auto const &functIds=m_document.getFormulaParser()->getFunctionsId();
    if (functIds.find(static_cast<unsigned long>(type))!=functIds.end()) {
      if (fSz<14) {
        MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected field size for functions name list\n"));
        f << "###func[name],";
        break;
      }
      for (int i=0; i<3; ++i) { // f1=0|-1, f2=small number, other 0
        auto val=static_cast<int>(input->readLong(2));
        if (val) f << "f" << i+1 << "=" << val << ",";
      }
      field.m_type=Field::T_FieldList;
      field.m_name="func[name]";
      bool ok=true;
      for (int i=0; i<2; ++i) {
        Field child;
        long pos=input->tell();
        if (!readField(input, endDataPos, ascFile, child)) {
          ok=false;
          input->seek(pos, librevenge::RVNG_SEEK_SET);
          break;
        }
        field.m_fieldList.push_back(child);
      }
      if (!ok || input->tell() != endDataPos) {
        MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read some 2fields list data\n"));
        f.str("");
        f << "###pos=" << input->tell()-debPos;
        input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
      }
      field.m_extra+=f.str();
      return true;
    }
    break;
  }
  }

  input->seek(debDataPos, librevenge::RVNG_SEEK_SET);
  if (!complex) {
    static bool first=true;
    if (first) {
      MWAW_DEBUG_MSG(("RagTime5StructManager::readField: find some unexpected data type=%lx, ...\n", static_cast<unsigned long>(type)));
      first=false;
    }
    field.m_name="#unknType";
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  switch (type) {
  case 0x14b5815: // increasing list: with fl1=3, fl2=80, f1=29
  case 0x16be055: // fl1=f, fl2=80, f1=30
  case 0x16be065: // fl1=f, fl2=80, f1=30
  case 0x146e815: // maybe a list of dim: fl1=28-2a, fl2=80, f1=34
  case 0x1473815: // maybe a list of dim, fl2=80,f1=32
  case 0x14e6825: // maybe a list of coldim
  case 0x14eb015: // maybe a list of rowdim
  case 0x14f1825:
  case 0x15f4815: // maybe related to list
  case 0x160f815:
  case 0x1671845:
  case 0x17db015: // in group cluster
    field.m_name="longList";
    break;
  case 0x1451025:
  case 0x146c015: // with ce017
  case 0x14e6875:
  case 0x15f4015: // with ce017
  case 0x15f6815:
  case 0x15f9015: // sometimes a list of 15f6815
    field.m_name="unstructList";
    break;
  case 0x15e0825:
    field.m_name="3unknList";
    break;
  case 0x14b4815: // with type=ce842
    field.m_name="unknLayout";
    break;
  case 0x1715815: // with type=ce842
    field.m_name="unknLstPict";
    break;
  default:
    MWAW_DEBUG_MSG(("RagTime5StructManager::readField: unexpected list type=%lx\n", static_cast<unsigned long>(type)));
    field.m_name="#unknList";
    break;
  }
  field.m_type=Field::T_FieldList;
  while (input->tell()<endDataPos) {
    long pos=input->tell();
    Field child;
    if (!readField(input, endDataPos, ascFile, child)) {
      f << "###pos=" << pos-debPos;
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      break;
    }
    field.m_fieldList.push_back(child);
  }
  if (input->tell()+4<endDataPos) {
    MWAW_DEBUG_MSG(("RagTime5StructManager::readField: can not read some data\n"));
    ascFile.addDelimiter(input->tell(),'|');
    input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  input->seek(endDataPos, librevenge::RVNG_SEEK_SET);
  field.m_extra=f.str();
  return true;
}

////////////////////////////////////////////////////////////
// field function
////////////////////////////////////////////////////////////
std::ostream &operator<<(std::ostream &o, RagTime5StructManager::Field const &field)
{
  if (!field.m_name.empty())
    o << field.m_name << ":" << RagTime5StructManager::printType(field.m_fileType);
  else
    o << "T:" << RagTime5StructManager::printType(field.m_fileType);
  switch (field.m_type) {
  case RagTime5StructManager::Field::T_Double:
    o << "=" << field.m_doubleValue << ",";
    break;
  case RagTime5StructManager::Field::T_Bool:
    if (field.m_longValue[0]==1)
      o << ",";
    else if (field.m_longValue[0]==0)
      o << "=no,";
    else
      o << field.m_longValue[0] << ",";
    break;
  case RagTime5StructManager::Field::T_Long:
    if (field.m_longValue[0]>1000)
      o << "=0x" << std::hex << field.m_longValue[0] << std::dec << ",";
    else
      o << "=" << field.m_longValue[0] << ",";
    break;
  case RagTime5StructManager::Field::T_2Long:
    o << "=" << field.m_longValue[0] << ":" <<  field.m_longValue[1] << ",";
    break;
  case RagTime5StructManager::Field::T_LongDouble:
    o << "=" << field.m_doubleValue << ":" <<  field.m_longValue[0] << ",";
    break;
  case RagTime5StructManager::Field::T_Color:
    o << "=" << field.m_color;
    if (field.m_longValue[0])
      o << "[" << field.m_longValue[0] << "]";
    o << ",";
    return o;
  case RagTime5StructManager::Field::T_CondColor:
    o << "=" << field.m_color << "[" << field.m_longValue[0] << "," << field.m_longValue[1] << "],";
    return o;
  case RagTime5StructManager::Field::T_String:
  case RagTime5StructManager::Field::T_Code:
    o << "=" << field.m_string.cstr() << ",";
    return o;
  case RagTime5StructManager::Field::T_ZoneId:
    if (field.m_longValue[0])
      o << "=data" << field.m_longValue[0] << "A,";
    return o;
  case RagTime5StructManager::Field::T_Unicode:
    o << "=\"" << field.m_string.cstr() << "\",";
    return o;
  case RagTime5StructManager::Field::T_PrintInfo:
  case RagTime5StructManager::Field::T_Unstructured:
    o << "=" << field.m_extra << ",";
    return o;
  case RagTime5StructManager::Field::T_FieldList:
    if (!field.m_fieldList.empty()) {
      o << "=[";
      for (auto const &val : field.m_fieldList)
        o << "[" << val << "],";
      o << "]";
    }
    o << ",";
    break;
  case RagTime5StructManager::Field::T_DoubleList:
    if (!field.m_doubleList.empty()) {
      o << "=[";
      for (auto const &val : field.m_doubleList)
        o << val << ",";
      o << "],";
    }
    break;
  case RagTime5StructManager::Field::T_LongList:
    if (!field.m_longList.empty() && field.m_numLongByData>0) {
      o << "=[";
      size_t pos=0;
      while (pos+size_t(field.m_numLongByData-1)<field.m_longList.size()) {
        for (int i=0; i<field.m_numLongByData; ++i) {
          long val=field.m_longList[pos++];
          if (!val)
            o << "_";
          else if (val>-1000 && val <1000)
            o << val;
          else if (val==long(0x80000000))
            o << "inf";
          // find sometime 0x3e7f0001
          else
            o << "0x" << std::hex << val << std::dec;
          o << ((i+1==field.m_numLongByData) ? "," : ":");
        }
      }
      o << "]";
    }
    o << ",";
    break;
  case RagTime5StructManager::Field::T_TabList:
    if (!field.m_tabList.empty()) {
      o << "=[";
      for (auto const &tab : field.m_tabList)
        o << tab << ",";
      o << "],";
    }
    break;
  case RagTime5StructManager::Field::T_Unknown:
#if !defined(__clang__)
  default:
#endif
    o << "[###unkn],";
    break;
  }
  o << field.m_extra;
  return o;
}

////////////////////////////////////////////////////////////
// parser function
////////////////////////////////////////////////////////////
bool RagTime5StructManager::GObjPropFieldParser::parseField(Field &field, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f)
{
  if (field.m_type==Field::T_FieldList) {
    switch (field.m_fileType) {
    case 0x6615a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_2Long && child.m_fileType==0x8c000) {
          f << "dim=" << child.m_longValue[0] << "x" << child.m_longValue[1] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected dim field\n"));
        f << "##dim=" << child << ",";
      }
      break;
    case 0x6616a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_Unstructured && child.m_fileType==0x79040) {
          f << "data1=" << child << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected data1 field\n"));
        f << "##data1=" << child << ",";
      }
      break;
    case 0x6617a: // 0[13]0[01]
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_Long && child.m_fileType==0x34080) {
          f << "data2=" << std::hex << child.m_longValue[0] << std::dec << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected data2 field\n"));
        f << "##data2=" << child << ",";
      }
      break;
    case 0x6619a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_ZoneId && static_cast<unsigned long>(child.m_fileType)==0x81452040) {
          f << "cluster[id]=data" << child.m_longValue[0] << "A,";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected cluster id field\n"));
        f << "##cluster[id]=" << child << ",";
      }
      break;
    case 0xfd827: // rare with 0
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_Long && child.m_fileType==0x3b880) {
          f << "data3=" << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected data3 field\n"));
        f << "##data3=" << child << ",";
      }
      break;
    case 0x10581a: // 1,_ or _,1 or 4000,1
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_LongList && child.m_fileType==0x33000) {
          f << "long[list]=[";
          for (auto val : child.m_longList) f << std::hex << val << std::dec << ",";
          f << "],";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find long[list] field\n"));
        f << "##long[list]=" << child << ",";
      }
      break;
    case 0x111817: // margin in %
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_DoubleList && child.m_fileType==0x112040 && child.m_doubleList.size()==4) {
          f << "margins=" << MWAWBox2f(MWAWVec2f(float(child.m_doubleList[0]),float(child.m_doubleList[1])),
                                       MWAWVec2f(float(child.m_doubleList[2]),float(child.m_doubleList[3]))) << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find margins field\n"));
        f << "##margins[list]=" << child << ",";
      }
      break;
    case 0x111827: // always 0,0
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_2Long && child.m_fileType==0x34800) {
          f << "unknPos=" << child.m_longValue[0] << "x" << child.m_longValue[1] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected unknPos field\n"));
        f << "##unknPos=" << child << ",";
      }
      break;
    case 0x1467837: // always 0,0
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_DoubleList && child.m_fileType==0x74040) {
          f << "float[list]=[";
          for (auto const &val : child.m_doubleList) f << val << ",";
          f << "],";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected float[list] field\n"));
        f << "##float[list]=" << child << ",";
      }
      break;
    case 0x146789a: // 1-2
    case 0x14678aa: // 1-2
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==Field::T_Long && child.m_fileType==0x149e080) {
          f << "d" << ((field.m_fileType>>4)&0xf) << "=" << std::hex << child.m_longValue[0] << std::dec << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected data%ld field\n", ((field.m_fileType>>4)&0xf)));
        f << "##d" << ((field.m_fileType>>4)&0xf) << "=" << child << ",";
      }
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected list field\n"));
      f << "###field=" << field << ",";
      break;
    }
  }
  else {
    MWAW_DEBUG_MSG(("RagTime5StructManager::GObjPropFieldParser::parseField: find unexpected field type\n"));
    f << "###field=" << field << ",";
  }
  return true;
}

////////////////////////////////////////////////////////////
// zone function
////////////////////////////////////////////////////////////
RagTime5Zone::~RagTime5Zone()
{
}

void RagTime5Zone::createAsciiFile()
{
  if (!m_input)
    return;
  if (m_asciiName.empty()) {
    MWAW_DEBUG_MSG(("RagTime5Zone::createAsciiFile: can not find the ascii name\n"));
    return;
  }
  if (m_localAsciiFile) {
    MWAW_DEBUG_MSG(("RagTime5Zone::createAsciiFile: the ascii file already exist\n"));
  }
  m_localAsciiFile.reset(new libmwaw::DebugFile(m_input));
  m_asciiFile = m_localAsciiFile.get();
  m_asciiFile->open(m_asciiName.c_str());
}

std::string RagTime5Zone::getZoneName() const
{
  if (m_level==1) {
    if (m_ids[0]==0 && m_idsFlag[0]==1)
      return "FileHeader";
    else if (m_ids[0]==1 && m_idsFlag[0]==0)
      return "ZoneInfo";
  }
  std::stringstream s;
  if (m_level==1)
    s << "Data" << m_ids[0] << "A";
  else if (m_level<0 || m_level>3)
    s << "###unknLevel" << m_level << "-" << m_ids[0];
  else if (!m_parentName.empty())
    s << m_parentName << "-" << m_ids[0] << char('A'+m_level-1);
  else
    s << "###unknChild" << m_ids[0] << char('A'+m_level-1) ;
  return s.str();
}

void RagTime5Zone::addErrorInDebugFile(std::string const &zoneName)
{
  m_isParsed=true;
  if (m_entry.valid()) {
    libmwaw::DebugStream f;
    f << "Entries(" << zoneName << ")[" << *this << "]:###bad";
    ascii().addPos(m_entry.begin());
    ascii().addNote(f.str().c_str());
    ascii().addPos(m_entry.end());
    ascii().addNote("_");
  }
  libmwaw::DebugStream f;
  f << zoneName << ":###bad";
  m_mainAsciiFile->addPos(m_defPosition);
  m_mainAsciiFile->addNote(f.str().c_str());
}

std::ostream &operator<<(std::ostream &o, RagTime5Zone const &z)
{
  o << z.getZoneName();
  if (z.m_idsFlag[0]==0)
    o << "[head],";
  else if (z.m_idsFlag[0]==1)
    o << ",";
  else
    o << "[" << z.m_idsFlag[0] << "],";
  for (int i=1; i<3; ++i) {
    if (!z.m_kinds[i-1].empty()) {
      o << z.m_kinds[i-1] << ",";
      continue;
    }
    if (!z.m_ids[i] && !z.m_idsFlag[i]) continue;
    o << "id" << i << "=" << z.m_ids[i];
    if (z.m_idsFlag[i]==0)
      o << "*";
    else if (z.m_idsFlag[i]!=1)
      o << ":" << z.m_idsFlag[i] << ",";
    o << ",";
  }
  if (z.m_variableD[0] || z.m_variableD[1])
    o << "varD=[" << z.m_variableD[0] << "," << z.m_variableD[1] << "],";
  if (z.m_entry.valid())
    o << z.m_entry.begin() << "<->" << z.m_entry.end() << ",";
  else if (!z.m_entriesList.empty()) {
    o << "ptr=" << std::hex;
    for (size_t i=0; i< z.m_entriesList.size(); ++i) {
      o << z.m_entriesList[i].begin() << "<->" << z.m_entriesList[i].end();
      if (i+1<z.m_entriesList.size())
        o << "+";
    }
    o << std::dec << ",";
  }
  if (!z.m_hiLoEndian) o << "loHi[endian],";
  o << z.m_extra << ",";
  return o;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
