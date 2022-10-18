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

#ifndef POWER_POINT7_PARSER
#  define POWER_POINT7_PARSER

#include <map>
#include <set>
#include <vector>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace PowerPoint7ParserInternal
{
struct State;
}

namespace PowerPoint7Struct
{
struct SlideId;
}

class PowerPoint7Graph;
class PowerPoint7Text;

/** \brief the main class to read a Microsoft PowerPoint 95 files (Windows)
 */
class PowerPoint7Parser final : public MWAWPresentationParser
{
  friend class PowerPoint7Graph;
  friend class PowerPoint7Text;
public:
  //! constructor
  PowerPoint7Parser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~PowerPoint7Parser() final;

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

  //
  // send data
  //

  // interface with the text parser

  //! returns the color corresponding to an id
  bool getColor(int cId, MWAWColor &col) const;
  //! try to send the text content
  bool sendText(int textId);

  //
  // low level
  //

  //! try to read the main zone: the zone 3
  bool readDocRoot();
  //! try to read the main sub zone: the zone 10: child of Root
  bool readMainSub10(long endPos);
  //! try to read the document's zone 1000: child of Root
  bool readDocument(long endPos);
  //! try to read the document atom's zone 1001: child of Document
  bool readDocAtom(int level, long endPos);
  //! try to read the slide information 1005: dimension, has background, ...
  bool readSlideInformation(int level, long endPos);
  //! try to read the slides zone 1006(slides) or 1016(master)
  bool readSlides(int level, long endPos, bool master);
  //! try to read the slide zone 1007
  bool readSlideAtom(int level, long endPos,
                     PowerPoint7Struct::SlideId &sId, PowerPoint7Struct::SlideId &mId);
  //! try to read the notes zone 1008
  bool readNotes(int level, long endPos);
  //! try to read the note atom zone 1009
  bool readNoteAtom(int level, long endPos, PowerPoint7Struct::SlideId &sId);
  //! try to read the environment zone 1010
  bool readEnvironment(int level, long endPos);
  //! try to read the color scheme 1012
  bool readColorScheme(int level, long endPos, std::vector<MWAWColor> &colors);
  //! try to read the zone 1015: child of Slides
  bool readZone1015(int level, long endPos);
  //! try to read the slide show info zone 1017
  bool readSlideShowInfo(int level, long endPos);
  //! try to read the slide view info zone 1018
  bool readSlideViewInfo(int level, long endPos);
  //! try to read the guide atom zone 1019
  bool readGuideAtom(int level, long endPos);
  //! try to read the view info zone 1021
  bool readViewInfoAtom(int level, long endPos);
  //! try to read the slide view info zone 1022
  bool readSlideViewInfoAtom(int level, long endPos);
  //! try to read the vba info zone 1023
  bool readVbaInfo(int level, long endPos);
  //! try to read the vba info zone 1024
  bool readVbaInfoAtom(int level, long endPos);
  //! try to read the slide show doc info atom 1025: child of Document
  bool readSSDocInfoAtom(int level, long endPos);
  //! try to read the summary zone 1026: child of Document
  bool readSummary(int level, long endPos);
  //! try to read the zone 1028: child of Environment
  bool readZone1028(int level, long endPos);
  //! try to read the zone 1029
  bool readZone1028Atom(int level, long endPos);
  //! try to read the outline view info zone 1031
  bool readOutlineViewInfo(int level, long endPos);
  //! try to read the sorter view info zone 1032
  bool readSorterViewInfo(int level, long endPos);

  //! try to read the container list zone 2000: child of Document
  bool readContainerList(int level, long endPos);
  //! try to read the container atom zone 2001
  bool readContainerAtom(int level, long endPos, int &N);
  //! try to read an identifier zone 2017
  bool readIdentifier(int level, long endPos, int &id, std::string const &wh);
  //! try to read the bookmark collection zone 2019
  bool readBookmarkCollection(int level, long endPos);
  //! try to read the sound collection zone 2020
  bool readSoundCollection(int level, long endPos);
  //! try to read the bookmark seed atom zone 2025
  bool readBookmarkSeedAtom(int level, long endPos);
  //! try to read the zone 2026: child of SlideViewInfo
  bool readZone2026(int level, long endPos);
  //! try to read the color list zone 2031
  bool readColorList(int level, long endPos, std::vector<MWAWColor> &colors);

  //! try to read the zone 3000: child of Handout/Notes/Slides/Zone3001
  bool readZone3000(int level, long endPos);
  //! try to read the zone 3012: child of Environment
  bool readZone3012(int level, long endPos);
  //! try to read the zone 3013: child of Zone3012
  bool readZone3012Atom(int level, long endPos);

  //! try to read the text chars atom zone 4000
  bool readTextCharsAtom(int level, long endPos);
  //! try to read the style text prop atom zone 4001
  bool readStyleTextPropAtom(int level, long endPos, int &textId);
  //! try to read the outline text props9 atom zone 4009
  bool readOutlineTextProps9Atom(int level, long endPos,
                                 int &pId, PowerPoint7Struct::SlideId &sId);
  //! try to read the outline text props header9 atom zone 4015
  bool readOutlineTextPropsHeader9Atom(int level, long endPos);
  //! try to read the string zone 4026
  bool readString(int level, long endPos, std::string &string, int &zId, std::string const &what="");
  //! try to read a slide identifier zone 4032
  bool readSlideIdentifier(int level, long endPos, PowerPoint7Struct::SlideId &sId);
  //! try to read the zone 4039: child of Zone4072
  bool readZone4039(int level, long endPos);
  //! try to read the Kinsoku zone 4040
  bool readKinsoku(int level, long endPos);
  //! try to read the handout zone 4041
  bool readHandout(int level, long endPos);
  //! try to read the zone 4042: child of FontCollection
  bool readZone4042(int level, long endPos);
  //! try to read the Kinsoku atom zone 4050
  bool readKinsokuAtom(int level, long endPos);
  //! try to read the zone 4052: child of Zone1028
  bool readZone1028Data(int level, long endPos);
  //! try to read a container of a slide identifier 4054
  bool readSlideIdentifierContainer(int level, long endPos, PowerPoint7Struct::SlideId &sId);
  //! try to read the header footer zone: 4057
  bool readHeaderFooters(int level, long endPos);
  //! try to read the header footer atom  zone: 4058
  bool readHeaderFooterAtom(int level, long endPos);
  //! try to read the zone 4072: child of Zone3008
  bool readZone4072(int level, long endPos);

  //! try to read a zone
  bool readZone(int level, long endPos);
  //! try to read a zone with no data
  bool readZoneNoData(int level, long endPos, std::string const &name, std::string const &wh="");

  //! try to read the "Text_Content" stream
  bool parseTextContent(MWAWInputStreamPtr input);
  //! check for unparsed zone
  void checkForUnparsedZones();
protected:
  //
  // data
  //
  //! the state
  std::shared_ptr<PowerPoint7ParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
