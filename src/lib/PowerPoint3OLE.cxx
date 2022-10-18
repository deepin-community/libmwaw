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

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWOLEParser.hxx"

#include "PowerPoint3OLE.hxx"

/** Internal: the structures of a PowerPoint3OLE */
namespace PowerPoint3OLEInternal
{
////////////////////////////////////////
//! Internal: the state of a PowerPoint3OLE
struct State {
  //! constructor
  explicit State(MWAWInputStreamPtr input, int vers)
    : m_input(input)
    , m_version(vers)
    , m_oleParser()
    , m_unparsedNameSet()
  {
  }
  /** the input */
  MWAWInputStreamPtr m_input;
  /** the version */
  int m_version;
  /** the ole parser */
  std::shared_ptr<MWAWOLEParser> m_oleParser;
  /** the list of unparsed zone */
  std::set<std::string> m_unparsedNameSet;
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint3OLE::PowerPoint3OLE(MWAWInputStreamPtr const &input, int vers, MWAWFontConverterPtr const &fontConverter, int fId)
  : m_state(new PowerPoint3OLEInternal::State(input, vers))
{
  char const *mainOle =(version()<=4 ? "PP40" : "PowerPoint Document");
  if (input && input->isStructured() && input->getSubStreamByName(mainOle))
    m_state->m_oleParser.reset(new MWAWOLEParser(mainOle, fontConverter, fId));
}

PowerPoint3OLE::~PowerPoint3OLE()
{
}

int PowerPoint3OLE::version() const
{
  return m_state->m_version;
}

int PowerPoint3OLE::getFontEncoding() const
{
  if (m_state->m_oleParser)
    return m_state->m_oleParser->getFontEncoding();
  return -1;
}

void PowerPoint3OLE::updateMetaData(librevenge::RVNGPropertyList &metaData) const
{
  if (m_state->m_oleParser)
    m_state->m_oleParser->updateMetaData(metaData);
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
bool PowerPoint3OLE::parse()
{
  int const vers=version();
  MWAWInputStreamPtr input=m_state->m_input;
  if (!input || !m_state->m_oleParser || !m_state->m_oleParser->parse(input)) return false;
  std::vector<std::string> unparsed = m_state->m_oleParser->getNotParse();

  for (auto const &name : unparsed) {
    // separated the directory and the name
    //    MatOST/MatadorObject1/Ole10Native
    //      -> dir="MatOST/MatadorObject1", base="Ole10Native"
    std::string::size_type pos = name.find_last_of('/');
    std::string base;
    if (pos == std::string::npos) base = name;
    else if (pos == 0) base = name.substr(1);
    else
      base = name.substr(pos+1);

    MWAWInputStreamPtr ole = input->getSubStreamByName(name.c_str());
    if (!ole.get()) {
      MWAW_DEBUG_MSG(("PowerPoint3OLE::createZones: error: can not find OLE part: \"%s\"\n", name.c_str()));
      continue;
    }
    ole->setReadInverted(true);
    bool done=false;
    switch (base[0]) {
    case 'C':
      if (base=="Current User")
        done=parseCurrentUser(ole, name);
      else if (base=="Current ID")
        done=parseCurrentId(ole, name);
      break;
    case 'H':
      if (vers>=7 && name=="Header")
        done=parseHeader(ole, name);
      break;
    case 'P':
      if (vers>=7 && name=="PersistentStorage Directory")
        done=parsePersistentStorage(ole, name);
      break;
    default:
      break;
    }
    if (done) continue;
    m_state->m_unparsedNameSet.insert(name);
  }
  return true;
}

void PowerPoint3OLE::checkForUnparsedStream()
{
  int const vers=version();
  for (auto const &name : m_state->m_unparsedNameSet) {
    if (vers>=7 && name=="Text_Content") continue;
    auto pos = name.find_last_of('/');
    std::string base;
    if (pos == std::string::npos) base = name;
    else if (pos == 0) base = name.substr(1);
    else
      base = name.substr(pos+1);
    MWAWInputStreamPtr ole = m_state->m_input->getSubStreamByName(name.c_str());
    if (!ole.get()) {
      MWAW_DEBUG_MSG(("PowerPoint3OLE::checkForUnparsedStream: error: can not find OLE part: \"%s\"\n", name.c_str()));
      continue;
    }
    libmwaw::DebugFile asciiFile(ole);
    asciiFile.open(name);
    libmwaw::DebugStream f;
    f << "Entries(" << base << "):";
    asciiFile.addPos(0);
    asciiFile.addNote(f.str().c_str());
  }
}

////////////////////////////////////////////////////////////
// try to read the different stream
////////////////////////////////////////////////////////////
bool PowerPoint3OLE::parseCurrentId(MWAWInputStreamPtr input, std::string const &name)
{
  if (!input||input->size()!=4) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parseCurrentId: can not find the input\n"));
    return false;
  }
  libmwaw::DebugFile ascFile(input);
  ascFile.open(name);
  input->seek(0, librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "Entries(CurrentId):";
  auto val=int(input->readLong(4));
  if (val) f << "id=" << val << ",";
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint3OLE::parseCurrentUser(MWAWInputStreamPtr input, std::string const &name)
{
  int const vers=version();
  int const szSize=vers<=4 ? 1 : 4;
  if (!input||input->size() < szSize) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parseCurrentUser: can not find the input\n"));
    return false;
  }
  libmwaw::DebugFile ascFile(input);
  ascFile.open(name);
  input->seek(0, librevenge::RVNG_SEEK_SET);
  long endPos=input->size();
  libmwaw::DebugStream f;
  f << "Entries(CurrentUser):";
  auto sSz=int(input->readULong(szSize));
  if (sSz<0 || sSz>input->size()-szSize) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parseCurrentUser: the stream size seems bad\n"));
    f << "###sSz,";
    ascFile.addPos(0);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  std::string user;
  for (int c=0; c<sSz; ++c) {
    auto ch=char(input->readULong(1));
    if (ch)
      user+=ch;
    else if (c+1!=sSz)
      f << "###";
  }
  f << user;
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());
  if (input->tell()!=endPos) {
    ascFile.addPos(input->tell());
    ascFile.addNote("CurrentUser:##extra");
  }
  return true;
}

bool PowerPoint3OLE::parseHeader(MWAWInputStreamPtr input, std::string const &name)
{
  if (!input || input->size()<19) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parseHeader: the input seems bad\n"));
    return false;
  }
  input->seek(0, librevenge::RVNG_SEEK_SET);
  long endPos=input->size();
  libmwaw::DebugFile ascFile(input);
  ascFile.open(name);
  libmwaw::DebugStream f;
  f << "Entries(Headr):";
  std::string text; // Microsoft (R) PowerPoint (R) Windows
  for (long i=0; i<endPos; ++i) {
    auto c=char(input->readULong(1));
    if (!c) break;
    text+=c;
  }
  f << text << ",";
  if (input->tell()+18>endPos) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parseHeader: the input seems short\n"));
    f << "###";
    ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(0);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  for (int i=0; i<9; ++i) {
    auto val=int(input->readULong(2));
    int const expected[]= {7,0,0x3f0,0,0xc05f,0xe391,1,0,0};
    if (val!=expected[i]) f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());
  if (input->tell()!=endPos) {
    ascFile.addPos(input->tell());
    ascFile.addNote("Headr:###");
  }
  return true;
}

bool PowerPoint3OLE::parsePersistentStorage(MWAWInputStreamPtr input, std::string const &name)
{
  if (!input || input->size()<62) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parsePersistentStorage: the input seems bad\n"));
    return false;
  }
  input->seek(0, librevenge::RVNG_SEEK_SET);
  long endPos=input->size();
  libmwaw::DebugFile ascFile(input);
  ascFile.open(name);
  libmwaw::DebugStream f;
  f << "Entries(PersistentStorage):";
  int val;
  for (int i=0; i<2; ++i) { // f1=3c6|3e0
    val=int(input->readULong(2));
    int const expected[]= {7,0x3e0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<13; ++i) { // f9=f11=f13=1
    val=int(input->readULong(2));
    if (val) f << "f" << i+2 << "=" << val << ",";
  }
  auto sSz=int(input->readULong(4));
  if (sSz<0 || endPos-30-8<sSz || 30+sSz+8>endPos) {
    MWAW_DEBUG_MSG(("PowerPoint3OLE::parsePersistentStorage: the string size seems bad\n"));
    f << "###sSz=" << sSz << ",";
    ascFile.addPos(0);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  std::string text; // PowerPoint Document
  for (int i=0; i<sSz; ++i) {
    auto c=char(input->readULong(1));
    if (c)
      text+=c;
    else if (i+1!=sSz) {
      MWAW_DEBUG_MSG(("PowerPoint3OLE::parsePersistentStorage: the name seems bad\n"));
      f << "##name,";
    }
  }
  f << text << ",";
  for (int i=0; i<4; ++i) { // g0=1
    val=int(input->readULong(2));
    if (val) f << "g" << i << "=" << val << ",";
  }
  ascFile.addPos(0);
  ascFile.addNote(f.str().c_str());
  if (input->tell()!=endPos) { // seems junk, unsure
    ascFile.addPos(input->tell());
    ascFile.addNote("_");
  }
  return true;
}

////////////////////////////////////////////////////////////
// try to send data
////////////////////////////////////////////////////////////


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
