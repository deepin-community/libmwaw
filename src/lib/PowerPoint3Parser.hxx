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

#ifndef POWER_POINT3_PARSER
#  define POWER_POINT3_PARSER

#include <map>
#include <set>
#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace PowerPoint3ParserInternal
{
struct FieldParser;
struct Frame;
struct Polygon;
struct Ruler;
struct Slide;
struct SlideContent;
struct SlideFormat;
struct State;
struct TextZone;

class SubDocument;
}

/** \brief the main class to read a Microsoft PowerPoint v3 or v4 files (MacOs and Windows)
 */
class PowerPoint3Parser final : public MWAWPresentationParser
{
  friend class PowerPoint3ParserInternal::SubDocument;
public:
  //! constructor
  PowerPoint3Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~PowerPoint3Parser() final;

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
  //! try to read a list of structure
  bool readStructList(MWAWEntry const &entry, PowerPoint3ParserInternal::FieldParser &parser);

  //
  // internal level
  //

  //! try to read a color list
  bool readColors(MWAWEntry const &entry);
  //! try to read a color zone, probably used to define the menu, ...: the 8th zone
  bool readColorZone(MWAWEntry const &entry);
  //! try to read the document info zone
  bool readDocInfo(MWAWEntry const &entry);
  //! try to read the main child of doc info
  bool readDocRoot(MWAWEntry const &entry);
  //! try to read a font
  bool readFont(MWAWFont &font, int schemeId);
  //! try to read a font names list
  bool readFontNamesList(std::map<int,int> const &fIdtoZIdMap);
  //! try to read a font name : 11th zone
  bool readFontName(MWAWEntry const &entry, int id);
  //! try to read a frame zone in a page
  bool readFramesList(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &content);
  //! try to read a paragraph
  bool readParagraph(MWAWParagraph &para, PowerPoint3ParserInternal::Ruler const &ruler, int schemeId);
  //! try to read the picture definition
  bool readPictureDefinition(MWAWEntry const &entry, int id);
  //! try to read the first child of the picture
  bool readPictureContent(MWAWEntry const &entry, MWAWEmbeddedObject &pict);
  //! try to read the 5th zone
  bool readPictureMain(MWAWEntry const &entry);
  //! try to read a picture list
  bool readPicturesList(std::map<int,int> const &pIdtoZIdMap);
  //! try to read a print info zone
  bool readPrintInfo(MWAWEntry const &entry);
  //! try to read some ruler
  bool readRuler(MWAWEntry const &entry, int id);
  //! try to read a scheme
  bool readScheme(MWAWEntry const &entry, int id);
  //! try to read a slide main zone
  bool readSlide(MWAWEntry const &entry, PowerPoint3ParserInternal::Slide &slide, int zId);
  //! try to read the second/third child of slideMain: main, master ?
  bool readSlideContent(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &slide);
  //! try to read the second child of slide content which contains some shadow offset...
  bool readSlideFormats(MWAWEntry const &entry, std::vector<PowerPoint3ParserInternal::SlideFormat> &formatList);
  //! try to read the third child of slide content
  bool readSlidePolygons(MWAWEntry const &entry, std::vector<PowerPoint3ParserInternal::Polygon> &polyList);
  //! try to read the first child of slideMain
  bool readSlideTransition(MWAWEntry const &entry);
  //! try to read the first child of docRoot
  bool readSlidesList(MWAWEntry const &entry);
  //! try to read a text zone
  bool readTextZone(MWAWEntry const &entry, PowerPoint3ParserInternal::SlideContent &content);


  //
  // send data
  //
  //! try to send a slide
  bool sendSlide(PowerPoint3ParserInternal::SlideContent const &slide, bool master);
  //! try to send a frame zone
  bool sendFrame(PowerPoint3ParserInternal::Frame const &frame, PowerPoint3ParserInternal::SlideContent const &content, bool master, std::set<int> &seen);
  //! try to send a text zone
  bool sendText(PowerPoint3ParserInternal::SlideContent const &slide, int tId, bool placeHolder, bool master);

  //
  // low level
  //
  //! try to read the 9th zone
  bool readZone9(MWAWEntry const &entry);
  //! try to read the 10th zone
  bool readZone10(MWAWEntry const &entry);


  //! try to return a color corresponding to a scheme and color
  bool getColor(int colorId, int schemeId, MWAWColor &color) const;
  //! check for unparsed zone
  void checkForUnparsedZones();
protected:
  //
  // data
  //
  //! the state
  std::shared_ptr<PowerPoint3ParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
