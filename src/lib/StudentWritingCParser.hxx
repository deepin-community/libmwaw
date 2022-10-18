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

/*
 * Parser to convert some Student Writing Center 1.0 text document
 *
 */
#ifndef STUDENT_WRITING_C__PARSER
#  define STUDENT_WRITING_C__PARSER

#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace StudentWritingCParserInternal
{
struct State;
struct Zone;

class SubDocument;
}

/** \brief the main class to read a Student Writing Center file
 *
 *
 *
 */
class StudentWritingCParser final : public MWAWTextParser
{
  friend class StudentWritingCParserInternal::SubDocument;

public:
  //! constructor
  StudentWritingCParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~StudentWritingCParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGTextInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGTextInterface *documentInterface);

  //! finds the different objects zones
  bool createZones();

  //! try to read a text zone with type=1
  bool readTextZone(StudentWritingCParserInternal::Zone const &zone);
  //! try to read a frame zone with type=3
  bool readFrame(StudentWritingCParserInternal::Zone &zone);
  //! try to read the paragraph zone with type=4
  bool readParagraph(StudentWritingCParserInternal::Zone &zone);
  //! try to read a picture zone with type=6
  bool readPicture(StudentWritingCParserInternal::Zone &zone);
  //! try to read the font list: the first entry of a zone with type=7
  bool readFontsList(MWAWEntry const &entry);
  //! try to read the printer information
  bool readPrintInfo();

  //! try to send a zone knowing its id
  bool sendZone(int id);
  //! try to send the text(s) corresponding to a zone of type 5
  bool sendText(StudentWritingCParserInternal::Zone const &zone, StudentWritingCParserInternal::Zone const &parent, bool isMain=false);
  //! try to send a picture: type 6
  bool sendPicture(MWAWPosition const &pos, int id);

  //! low level: try to uncompress the different compressed zone
  MWAWInputStreamPtr decode();
protected:
  //
  // data
  //

  //! the state
  std::shared_ptr<StudentWritingCParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
