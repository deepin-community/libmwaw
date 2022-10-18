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

#ifndef POWER_POINT3_OLE
#  define POWER_POINT3_OLE

#include <string>

#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace PowerPoint3OLEInternal
{
struct State;
}

/** \brief the main class to read the ole stream in a Microsoft PowerPoint v4 files (MacOs and Windows)
 */
class PowerPoint3OLE
{
public:
  //! constructor
  PowerPoint3OLE(MWAWInputStreamPtr const &input, int vers, MWAWFontConverterPtr const &fontConverter, int fId);
  //! destructor
  virtual ~PowerPoint3OLE();

  // the main parse function
  bool parse();
  /** update the meta data, using information find in SummaryInformation */
  void updateMetaData(librevenge::RVNGPropertyList &metaData) const;
  //! returns the font encoding(or -1)
  int getFontEncoding() const;
  //! check for unparsed stream
  void checkForUnparsedStream();

protected:
  //
  // internal level
  //

  //! try to parse the "Current User" stream: v4 and v7
  bool parseCurrentUser(MWAWInputStreamPtr input, std::string const &name);
  //! try to parse the "Current Id" stream: v4
  bool parseCurrentId(MWAWInputStreamPtr input, std::string const &name);

  //! try to read the "Header" stream: v7
  bool parseHeader(MWAWInputStreamPtr input, std::string const &name);
  //! try to read the "PersistentStorage Directory" stream: v7
  bool parsePersistentStorage(MWAWInputStreamPtr input, std::string const &name);

  //
  // send data
  //

  //
  // low level
  //

  //! returns the file version
  int version() const;
protected:
  //
  // data
  //
  //! the state
  std::shared_ptr<PowerPoint3OLEInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
