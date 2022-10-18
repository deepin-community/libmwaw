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

#ifndef POWER_POINT1_PARSER
#  define POWER_POINT1_PARSER

#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace PowerPoint1ParserInternal
{
struct Frame;
struct Scheme;
struct Slide;
struct TextZone;

struct State;

class SubDocument;
}

/** \brief the main class to read a Mac Microsoft PowerPoint v1, v2 files
 *
 * \note there is some basic code to find the main zones in a Windows v2 files
 */
class PowerPoint1Parser final : public MWAWPresentationParser
{
  friend class PowerPoint1ParserInternal::SubDocument;
public:
  //! constructor
  PowerPoint1Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~PowerPoint1Parser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGPresentationInterface *documentInterface) final;

protected:
  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGPresentationInterface *documentInterface);

  //! finds the different zones
  bool createZones();
  //! try to send all slides
  void sendSlides();

  //! try to read the list of zones
  bool readListZones(int &docInfoId);

  //
  // internal level
  //

  //! try to read a frame zone in a page
  bool readFramesList(MWAWEntry const &entry, std::vector<PowerPoint1ParserInternal::Frame> &frame, int schemeId);
  //! try to read a zone id list zone
  bool readZoneIdList(MWAWEntry const &entry, int zId);
  //! try to read a zone id list zone for v2 pc file
  bool readZoneIdList2(MWAWEntry const &entry, int zId);

  //! try to read a color list
  bool readColors(MWAWEntry const &entry);
  //! try to read a color zone, probably used to define the menu, ...
  bool readColorZone(MWAWEntry const &entry);
  //! try to read a font style list
  bool readFonts(MWAWEntry const &entry);
  //! try to read a font names list
  bool readFontNames(MWAWEntry const &entry);
  //! try to read a picture zone
  bool readPicture(MWAWEntry const &entry, MWAWEmbeddedObject &picture);
  //! try to read the picture definition: windows v2
  bool readPictureDefinition(MWAWEntry const &entry, size_t id);
  //! try to read a print info zone
  bool readPrintInfo(MWAWEntry const &entry);
  //! try to read the paragraph style
  bool readRulers(MWAWEntry const &entry);
  //! try to read some ruler: windows v2
  bool readRuler(MWAWEntry const &entry, size_t id);
  //! try to read a scheme
  bool readScheme(MWAWEntry const &entry, int id);
  //! try to read the schemes
  bool readSchemes();
  //! try to read a text zone
  bool readTextZone(MWAWEntry const &entry, PowerPoint1ParserInternal::TextZone &zone);
  //! try to read a slide zone, update the list of slide ids
  bool readSlide(MWAWEntry const &entry, std::vector<int> &slideIds);
  //! try to read the document info zone
  bool readDocInfo(MWAWEntry const &entry);
  //! try to read a unknown zone with size 22: related to scheme?
  bool readZone2(MWAWEntry const &entry);

  //
  // send data
  //

  //! try to send a slide
  bool sendSlide(PowerPoint1ParserInternal::Slide const &slide, bool master);
  //! try to send a frame zone
  bool sendFrame(PowerPoint1ParserInternal::Frame const &frame, PowerPoint1ParserInternal::TextZone const &textZone);
  //! try to send a picture
  bool sendPicture(MWAWPosition const &position, MWAWGraphicStyle const &style, int pId);
  //! try to send a text zone
  bool sendText(PowerPoint1ParserInternal::TextZone const &textZone, MWAWVec2i tId, int rulerId);
  //! try to send the slide note text
  bool sendSlideNote(PowerPoint1ParserInternal::Slide const &slide);

  //
  // low level
  //

  //! try to return a color corresponding to a scheme and color
  bool getColor(int colorId, int schemeId, MWAWColor &color) const;
  //! check for unparsed zone
  void checkForUnparsedZones();
protected:
  //
  // data
  //
  //! the state
  std::shared_ptr<PowerPoint1ParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
