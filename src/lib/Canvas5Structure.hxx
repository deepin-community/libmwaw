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

#ifndef CANVAS5_STRUCTURE
#  define CANVAS5_STRUCTURE

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

class MWAWStringStream;

//! a namespace used to define basic function or structure to read a Canvas v5-v11 file
namespace Canvas5Structure
{
//! very low level: debug print some uint32_t either at char4 or sample int
std::string getString(unsigned val);

//! a sub stream of Canvas5Structure
struct Stream {
  //! constructor
  explicit Stream(MWAWInputStreamPtr input)
    : m_input(input)
    , m_ascii(m_asciiFile)
    , m_asciiFile(input)
  {
  }
  //! constructor from input and ascii file
  Stream(MWAWInputStreamPtr input, libmwaw::DebugFile &ascii)
    : m_input(input)
    , m_ascii(ascii)
    , m_asciiFile(input)
  {
  }
  //! returns the input file
  MWAWInputStreamPtr input()
  {
    return m_input;
  }
  //! return the ascii file
  libmwaw::DebugFile &ascii()
  {
    return m_ascii;
  }
protected:
  //! the input file
  MWAWInputStreamPtr m_input;
  //! the ascii file
  libmwaw::DebugFile &m_ascii;
  //! the ascii file
  libmwaw::DebugFile m_asciiFile;
};

//! try to read a bitmap(low level)
bool readBitmap(Stream &stream, int version, MWAWEmbeddedObject &object, MWAWColor *avgColor=nullptr);
/** try to read a bitmap followed by DAD5 and 8BIM zones

    \note such a bitmap appears in the bitmap lists or in a .cvi bitmap file
*/
bool readBitmapDAD58Bim(Stream &stream, int version, MWAWEmbeddedObject &object);
//! try to read the preview bitmap
bool readPreview(Canvas5Structure::Stream &stream, bool hasPreviewBitmap);

//! try to decode a zone v5-v6
bool decodeZone5(MWAWInputStreamPtr input, long endPos, int type, unsigned long finalLength,
                 std::shared_ptr<MWAWStringStream> &stream);

}

#endif
