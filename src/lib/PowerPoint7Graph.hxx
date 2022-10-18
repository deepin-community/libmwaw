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
 * Parser to PowerPoint 95 document ( graphic part )
 *
 */
#ifndef POWER_POINT7_GRAPH
#  define POWER_POINT7_GRAPH

#include <set>
#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

namespace PowerPoint7GraphInternal
{
struct Frame;
struct Picture;

struct State;
class SubDocument;
}

namespace PowerPoint7Struct
{
struct SlideId;
}

class PowerPoint7Parser;

/** \brief the main class to read the graphic part of a PowerPoint 95 file
 *
 *
 *
 */
class PowerPoint7Graph
{
  friend class PowerPoint7Parser;
  friend class PowerPoint7GraphInternal::SubDocument;
public:
  //! constructor
  explicit PowerPoint7Graph(PowerPoint7Parser &parser);
  //! destructor
  virtual ~PowerPoint7Graph();

  /** returns the file version */
  int version() const;
  /** sets the page size */
  void setPageSize(MWAWVec2i &pageSize);
  /** sets the slide id */
  void setSlideId(PowerPoint7Struct::SlideId const &id);
  /** sets the color list */
  void setColorList(std::vector<MWAWColor> const &colorList);
protected:
  //! try to send the slide content
  bool sendSlide(PowerPoint7Struct::SlideId const &id, bool sendBackground);
  //! try to send a frame
  bool sendFrame(PowerPoint7GraphInternal::Frame const &frame, bool master);
  //! try to send the text content
  bool sendText(int textId);

  //
  // Intermediate level
  //

  //! try to read the bitmap container zone 1027
  bool readBitmapContainer(int level, long endPos, PowerPoint7GraphInternal::Picture &picture);
  //! try to read the font collection 10 zone 2006
  bool readPictureList(int level, long endPos);
  //! try to read a bitmap zone 2012
  bool readBitmap(int level, long endPos, MWAWEmbeddedObject &object, MWAWBox2i &box);
  //! try to read the bitmap type zone 3038
  bool readBitmapFlag(int level, long endPos);

  //! try to read the picture container 4028
  bool readPictureContainer(int level, long endPos, PowerPoint7GraphInternal::Picture &picture);
  //! try to read the picture id container zone 4053
  bool readPictureIdContainer(int level, long endPos, int &id);
  //! try to read the picture id 4036
  bool readPictureId(int level, long endPos, int &id);

  //! try to read the meta file zone 4033
  bool readMetaFile(int level, long endPos, MWAWEmbeddedObject &object);
  //! try to read the meta file container zone 4037
  bool readMetaFileContainer(int level, long endPos, PowerPoint7GraphInternal::Picture &picture);
  //! try to read the meta file box zone 4038
  bool readMetaFileBox(int level, long endPos, MWAWBox2i &box);

  //! try to read the external ole object atom zone: 4035
  bool readExternalOleObjectAtom(int level, long endPos);
  //! try to read the external ole embed zone 4044
  bool readExternalOleEmbed(int level, long endPos, int &id);
  //! try to read the external ole object atom zone: 4045
  bool readExternalOleEmbedAtom(int level, long endPos);

  //! try to read the group zone
  bool readGroup(int level, long endPos);
  //! try to read the zone 3002
  bool readGroupAtom(int level, long endPos);
  //! try to read the graphic style zone 3005
  bool readStyle(int level, long endPos);
  //! try to read the line arrow zone 3007
  bool readLineArrows(int level, long endPos);
  //! try to read the graph rectangle zone 3008
  bool readRect(int level, long endPos);
  //! try to read the graph shape zone 3009
  bool readRectAtom(int level, long endPos);
  //! try to read a place holder container 3010
  bool readPlaceholderContainer(int level, long endPos);
  //! try to read the place holder atom zone 3011
  bool readPlaceholderAtom(int level, long endPos);
  //! try to read the line graph zone 3014
  bool readLine(int level, long endPos);
  //! try to read the graph line atom zone  3015
  bool readLineAtom(int level, long endPos);
  //! try to read the polygon zone 3016
  bool readPolygon(int level, long endPos);
  //! try to read the polygon atom zone 3017
  bool readPolygonAtom(int level, long endPos);
  //! try to read the graph arc zone 3018
  bool readArc(int level, long endPos);
  //! try to read the graph arc atom 3019
  bool readArcAtom(int level, long endPos);

  //! try to read the list of point zone 3035
  bool readPointList(int level, long endPos, std::vector<MWAWVec2i> &points);
  //! try to read the graph zone flags zone 3036
  bool readZoneFlags(int level, long endPos);

  //! try to read the zone 5000: child of GraphRect (only found in 95 an 97 document)
  bool readZone5000(int level, long endPos);
  //! try to read the zone 5000 header: 5001
  bool readZone5000Header(int level, long endPos);
  //! try to read the zone 5000 data: 5002
  bool readZone5000Data(int level, long endPos);

  //
  // low level
  //

private:
  PowerPoint7Graph(PowerPoint7Graph const &orig) = delete;
  PowerPoint7Graph &operator=(PowerPoint7Graph const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<PowerPoint7GraphInternal::State> m_state;

  //! the main parser;
  PowerPoint7Parser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
