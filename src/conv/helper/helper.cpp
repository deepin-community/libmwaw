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

#include <sys/stat.h>
#include <sys/types.h>
#if defined(WITH_EXTENDED_FS) && WITH_EXTENDED_FS>0
#  include <sys/xattr.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "helper.h"
#include <librevenge/librevenge.h>
#include <libmwaw/libmwaw.hxx>

namespace libmwawHelper
{
#ifndef __EMSCRIPTEN__
////////////////////////////////////////////////////////////
// static class to create a RVNGInputStream for some data
////////////////////////////////////////////////////////////

/** internal class used to create a RVNGInputStream from a unsigned char's pointer

    \note this class (highly inspired from librevenge) does not
    implement the isStructured's protocol, ie. it only returns false.
 */
class StringStream final : public librevenge::RVNGInputStream
{
public:
  //! constructor
  explicit StringStream(std::vector<unsigned char> const &buffer)
    : librevenge::RVNGInputStream()
    , m_buffer(buffer)
    , m_offset(0)
  {
  }

  //! destructor
  ~StringStream() final
  {
  }

  /**! reads numbytes data.

   * \return a pointer to the read elements
   */
  const unsigned char *read(unsigned long numBytes, unsigned long &numBytesRead) final;
  //! returns actual offset position
  long tell() final
  {
    return m_offset;
  }
  /*! \brief seeks to a offset position, from actual, beginning or ending position
   * \return 0 if ok
   */
  int seek(long offset, librevenge::RVNG_SEEK_TYPE seekType) final;
  //! returns true if we are at the end of the section/file
  bool isEnd() final
  {
    if (long(m_offset) >= long(m_buffer.size()))
      return true;

    return false;
  }

  /** returns true if the stream is ole

   \sa returns always false*/
  bool isStructured() final
  {
    return false;
  }
  /** returns the number of sub streams.

   \sa returns always 0*/
  unsigned subStreamCount() final
  {
    return 0;
  }
  /** returns the ith sub streams name

   \sa returns always 0*/
  const char *subStreamName(unsigned) final
  {
    return 0;
  }
  /** returns true if a substream with name exists

   \sa returns always false*/
  bool existsSubStream(const char *) final
  {
    return false;
  }
  /** return a new stream for a ole zone

   \sa returns always 0 */
  librevenge::RVNGInputStream *getSubStreamByName(const char *) final
  {
    return 0;
  }
  /** return a new stream for a ole zone

   \sa returns always 0 */
  librevenge::RVNGInputStream *getSubStreamById(unsigned) final
  {
    return 0;
  }

private:
  //! the stream buffer
  std::vector<unsigned char> m_buffer;
  //! the stream offset
  long m_offset;
  StringStream(const StringStream &) = delete; // copy is not allowed
  StringStream &operator=(const StringStream &) = delete; // assignment is not allowed
};

const unsigned char *StringStream::read(unsigned long numBytes, unsigned long &numBytesRead)
{
  numBytesRead = 0;

  if (numBytes == 0)
    return 0;

  long numBytesToRead;

  if (static_cast<unsigned long>(m_offset)+numBytes < m_buffer.size())
    numBytesToRead = long(numBytes);
  else
    numBytesToRead = long(m_buffer.size()) - m_offset;

  numBytesRead = static_cast<unsigned long>(numBytesToRead); // about as paranoid as we can be..

  if (numBytesToRead == 0)
    return 0;

  long oldOffset = m_offset;
  m_offset += numBytesToRead;

  return &m_buffer[size_t(oldOffset)];

}

int StringStream::seek(long offset, librevenge::RVNG_SEEK_TYPE seekType)
{
  if (seekType == librevenge::RVNG_SEEK_CUR)
    m_offset += offset;
  else if (seekType == librevenge::RVNG_SEEK_SET)
    m_offset = offset;
  else if (seekType == librevenge::RVNG_SEEK_END)
    m_offset = offset+long(m_buffer.size());

  if (m_offset < 0) {
    m_offset = 0;
    return -1;
  }
  if (long(m_offset) > long(m_buffer.size())) {
    m_offset = long(m_buffer.size());
    return -1;
  }

  return 0;
}

/** internal class used to create a structrured RVNGInputStream from some files given there path name or there data */
class FolderStream final : public librevenge::RVNGInputStream
{
public:
  //! constructor
  FolderStream()
    : librevenge::RVNGInputStream()
    , m_nameToPathMap()
    , m_nameToBufferMap()
  {
  }

  //! destructor
  ~FolderStream() final
  {
  }

  //! add a file
  void addFile(std::string const &path, std::string const &shortName)
  {
    m_nameToPathMap[shortName]=path;
  }
  //! add a file
  void addFile(std::vector<unsigned char> const &buffer, std::string const &shortName)
  {
    m_nameToBufferMap[shortName]=buffer;
  }
  /**! reads numbytes data.

   * \return a pointer to the read elements
   */
  const unsigned char *read(unsigned long, unsigned long &) final
  {
    return 0;
  }
  //! returns actual offset position
  long tell() final
  {
    return 0;
  }
  /*! \brief seeks to a offset position, from actual, beginning or ending position
   * \return 0 if ok
   */
  int seek(long, librevenge::RVNG_SEEK_TYPE) final
  {
    return 1;
  }
  //! returns true if we are at the end of the section/file
  bool isEnd() final
  {
    return true;
  }

  /** returns true if the stream is ole

   \sa returns always false*/
  bool isStructured() final
  {
    return true;
  }
  /** returns the number of sub streams.

   \sa returns always 2*/
  unsigned subStreamCount() final
  {
    return unsigned(m_nameToPathMap.size()+m_nameToBufferMap.size());
  }
  /** returns the ith sub streams name */
  const char *subStreamName(unsigned id) final
  {
    if (id<m_nameToPathMap.size()) {
      auto it=m_nameToPathMap.begin();
      for (unsigned i=0; i<id; ++i) {
        if (it==m_nameToPathMap.end()) return 0;
        ++it;
      }
      if (it==m_nameToPathMap.end()) return 0;
      return it->first.c_str();
    }
    id=unsigned(id-m_nameToPathMap.size());
    auto it=m_nameToBufferMap.begin();
    for (unsigned i=0; i<id; ++i) {
      if (it==m_nameToBufferMap.end()) return 0;
      ++it;
    }
    if (it==m_nameToBufferMap.end()) return 0;
    return it->first.c_str();
  }
  /** returns true if a substream with name exists */
  bool existsSubStream(const char *name) final
  {
    return name && (m_nameToPathMap.find(name)!= m_nameToPathMap.end() || m_nameToBufferMap.find(name)!= m_nameToBufferMap.end());
  }
  /** return a new stream for a ole zone */
  librevenge::RVNGInputStream *getSubStreamByName(const char *name) final;
  /** return a new stream for a ole zone */
  librevenge::RVNGInputStream *getSubStreamById(unsigned id) final
  {
    char const *name=subStreamName(id);
    if (name==0) return 0;
    return getSubStreamByName(name);
  }
private:
  /// the map short name to path
  std::map<std::string, std::string> m_nameToPathMap;
  /// the map short name to buffer
  std::map<std::string, std::vector<unsigned char> > m_nameToBufferMap;
  FolderStream(const FolderStream &) = delete; // copy is not allowed
  FolderStream &operator=(const FolderStream &) = delete; // assignment is not allowed
};

librevenge::RVNGInputStream *FolderStream::getSubStreamByName(const char *name)
{
  if (m_nameToPathMap.find(name) != m_nameToPathMap.end())
    return new librevenge::RVNGFileStream(m_nameToPathMap.find(name)->second.c_str());
  if (m_nameToBufferMap.find(name) != m_nameToBufferMap.end())
    return new StringStream(m_nameToBufferMap.find(name)->second);
  return 0;
}

////////////////////////////////////////////////////////////
// static interface to the file system
////////////////////////////////////////////////////////////

#if defined(WITH_EXTENDED_FS) && (WITH_EXTENDED_FS>0)
static std::shared_ptr<FolderStream> getFileInput(char const *fName)
{
  std::shared_ptr<FolderStream> res;
  if (!fName)
    return res;
  // the rsrc fork can be accessed by adding "/..namedfork/rsrc" to the file name
  std::string rsrcName(fName);
  rsrcName += "/..namedfork/rsrc";
  struct stat status;
  if (stat(rsrcName.c_str(), &status) || !S_ISREG(status.st_mode) || status.st_size==0)
    return res;
  /* I do not find any way to access the finderinfo fork directly.
     So I use getxattr */
#  if WITH_EXTENDED_FS==1
#    define MWAW_EXTENDED_FS , 0, XATTR_SHOWCOMPRESSION
#  else
#    define MWAW_EXTENDED_FS
#  endif
  if (getxattr(fName, "com.apple.FinderInfo", 0, 0 MWAW_EXTENDED_FS)<=0)
    return res;
  ssize_t sz=getxattr(fName, "com.apple.FinderInfo", 0, 0 MWAW_EXTENDED_FS);
  if (sz<=0) return res;
  std::vector<unsigned char> buffer;
  buffer.resize(size_t(sz));
  if (getxattr(fName, "com.apple.FinderInfo", &buffer[0], size_t(sz) MWAW_EXTENDED_FS) != sz)
    return res;
  // ok, let create the folder stream
  res.reset(new FolderStream);
  res->addFile(fName, "DataFork");
  res->addFile(rsrcName, "RsrcFork");
  res->addFile(buffer, "InfoFork");
  return res;
}
#else
static std::shared_ptr<FolderStream> getFileInput(char const *)
{
  return std::shared_ptr<FolderStream>();
}
#endif

/* check if the file contains some resource, if yes, try to
   convert it in a structured input which can be parsed by libmwaw */
static std::shared_ptr<librevenge::RVNGInputStream> createFolderInput(char const *fName, librevenge::RVNGInputStream &input)
try
{
  std::shared_ptr<FolderStream> res;

  /* we do not want to compress already compressed file.
     So check if the file is structured, is a binhex file
   */
  if (!fName) return res;

  unsigned long fileSize= input.seek(0, librevenge::RVNG_SEEK_END)==0 ? static_cast<unsigned long>(input.tell()) : 0;

  if (fileSize>46) {
    input.seek(0, librevenge::RVNG_SEEK_SET);
    unsigned long numBytesRead;
    const unsigned char *buf=input.read(46, numBytesRead);
    if (buf && numBytesRead==46 && strcmp(reinterpret_cast<char const *>(buf), "(This file must be converted with BinHex 4.0)")==0)
      return res;
  }
  res=getFileInput(fName);
  if (res) return res;

  // check if the resource are stored in a ._XXX or a __MACOSX/.XXX file
  std::string originalFile(fName);
  /** find folder and base file name*/
  size_t sPos=originalFile.rfind('/');
  std::string folder(""), fileName("");
  if (sPos==std::string::npos)
    fileName = originalFile;
  else {
    folder=originalFile.substr(0,sPos+1);
    fileName=originalFile.substr(sPos+1);
  }
  for (int test=0; test<2; ++test) {
    std::string rsrcName=test==0 ? folder+"._"+fileName : folder+"__MACOSX/._"+fileName;
    struct stat status;
    if (stat(rsrcName.c_str(), &status) || !S_ISREG(status.st_mode) || status.st_size==0)
      continue;
    res.reset(new FolderStream);
    res->addFile(originalFile, "DataFork");
    res->addFile(rsrcName, "RsrcInfo");
    return res;
  }

  return res;
}
catch (...)
{
  return std::shared_ptr<librevenge::RVNGInputStream>();
}
#endif

////////////////////////////////////////////////////////////
// main functions
////////////////////////////////////////////////////////////

std::shared_ptr<librevenge::RVNGInputStream> isSupported
(char const *filename, MWAWDocument::Confidence &confidence, MWAWDocument::Kind &kind)
{
  std::shared_ptr<librevenge::RVNGInputStream> input(new librevenge::RVNGFileStream(filename));
  MWAWDocument::Type type;
#ifndef __EMSCRIPTEN__
  try {
    auto mimeInput=createFolderInput(filename, *input);
    if (mimeInput) {
      confidence=MWAWDocument::isFileFormatSupported(mimeInput.get(), type, kind);
      if (confidence == MWAWDocument::MWAW_C_EXCELLENT)
        return mimeInput;
    }
  }
  catch (...) {
  }
#endif
  try {
    confidence = MWAWDocument::isFileFormatSupported(input.get(), type, kind);
    if (confidence == MWAWDocument::MWAW_C_EXCELLENT)
      return input;
  }
  catch (...) {
  }
  return std::shared_ptr<librevenge::RVNGInputStream>();
}

bool checkErrorAndPrintMessage(MWAWDocument::Result result)
{
  if (result == MWAWDocument::MWAW_R_FILE_ACCESS_ERROR)
    fprintf(stderr, "ERROR: File Exception!\n");
  else if (result == MWAWDocument::MWAW_R_PARSE_ERROR)
    fprintf(stderr, "ERROR: Parse Exception!\n");
  else if (result == MWAWDocument::MWAW_R_OLE_ERROR)
    fprintf(stderr, "ERROR: File is an OLE document!\n");
  else if (result != MWAWDocument::MWAW_R_OK)
    fprintf(stderr, "ERROR: Unknown Error!\n");
  else
    return false;
  return true;
}

}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
