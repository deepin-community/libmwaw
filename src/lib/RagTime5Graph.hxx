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
 * Parser to RagTime 5-6 document ( graphic part )
 *
 */
#ifndef RAGTIME5_GRAPH
#  define RAGTIME5_GRAPH

#include <string>
#include <map>
#include <vector>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWPosition.hxx"

#include "RagTime5StructManager.hxx"
#include "RagTime5ClusterManager.hxx"

namespace RagTime5GraphInternal
{
struct ClusterButton;
struct ClusterGraphic;
struct ClusterPicture;

struct Shape;
struct State;

class SubDocument;
}

class RagTime5Document;
class RagTime5StructManager;
class RagTime5StyleManager;
class RagTime5Zone;

class MWAWGraphicStyle;

/** \brief the main class to read the graphic part of RagTime 56 file
 *
 *
 *
 */
class RagTime5Graph
{
  friend class RagTime5GraphInternal::SubDocument;
  friend class RagTime5Document;

public:
  //! constructor
  explicit RagTime5Graph(RagTime5Document &doc);
  //! destructor
  virtual ~RagTime5Graph();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

protected:

  //! sends the data which have not yet been sent to the listener
  void flushExtra(bool onlyCheck=false);

  // interface with main parser

  //! ask to send a text zone
  bool sendTextZone(MWAWListenerPtr listener, int zId, int pId, double totalWidth=-1);
  //! ask to send the button content as text
  bool sendButtonZoneAsText(MWAWListenerPtr listener, int zId);

  //
  // Intermediate level
  //

  //
  // picture
  //

  //! try to read a picture data zone
  bool readPictureContainer(RagTime5Zone &zone);
  //! try to read a picture data zone: ie. the zone which contains jpeg, tiff, ...
  bool readPictureData(RagTime5Zone &zone);
  //! try to read a picture data zone: ie. the zone which contains jpeg, tiff and then returns the picture content...
  bool readPictureData(RagTime5Zone &zone, librevenge::RVNGBinaryData &data, std::string &type);
  //! try to read a picture list
  bool readPictureList(RagTime5Zone &zone);
  //! try to read a picture rep
  bool readPictureRep(RagTime5Zone &zone);
  //! try to read a picture match zone
  bool readPictureMatch(RagTime5Zone &zone, bool color);

  //
  // basic graphic
  //

  //! try to read a main graphic types
  bool readGraphicTypes(RagTime5ClusterManager::Link const &link);

  //! try to read a zone of color and pattern
  bool readColorPatternZone(RagTime5ClusterManager::Cluster &cluster);

  //! try to read a button zone
  std::shared_ptr<RagTime5ClusterManager::Cluster> readButtonCluster(RagTime5Zone &zone, int zoneType);

  //! try to read a graphic zone
  std::shared_ptr<RagTime5ClusterManager::Cluster> readGraphicCluster(RagTime5Zone &zone, int zoneType);
  //! try to read the id zone => used
  bool readGraphicUsed(int typeId);
  //! try to read the graphic shapes of a cluster
  bool readGraphicShapes(RagTime5GraphInternal::ClusterGraphic &cluster);
  //! try to read a graphic
  bool readGraphicShape(RagTime5GraphInternal::ClusterGraphic &cluster, RagTime5Zone &dataZone, long endPos, int n, librevenge::RVNGString const &dataName);
  //! try to read a graphic transformations zone
  bool readGraphicTransformations(RagTime5ClusterManager::Link const &link);

  //! try to read a picture zone
  std::shared_ptr<RagTime5ClusterManager::Cluster> readPictureCluster(RagTime5Zone &zone, int zoneType);

  //
  // low level
  //

  //! check the graphic cluster data: check if there is no loop, ...
  void checkGraphicCluster(RagTime5GraphInternal::ClusterGraphic &cluster);

  //
  // send data
  //

  //! try to send the cluster zone
  bool send(int zoneId, MWAWListenerPtr listener, MWAWPosition const &pos);

  //! try to send a button of cluster zone
  bool send(RagTime5GraphInternal::ClusterButton &cluster, MWAWListenerPtr listener, MWAWPosition const &pos, MWAWGraphicStyle const &style);
  //! try to send the shapes of cluster zone
  bool send(RagTime5GraphInternal::ClusterGraphic &cluster, MWAWListenerPtr listener, MWAWPosition const &pos);
  //! try to send a shape of cluster zone
  bool send(RagTime5GraphInternal::Shape const &shape, RagTime5GraphInternal::ClusterGraphic const &cluster, MWAWListenerPtr listener, MWAWPosition const &pos);

  //! try to send the picture of cluster zone
  bool send(RagTime5GraphInternal::ClusterPicture &cluster, MWAWListenerPtr listener, MWAWPosition const &pos);

public:
  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }

private:
  RagTime5Graph(RagTime5Graph const &orig);
  RagTime5Graph &operator=(RagTime5Graph const &orig);

protected:
  //
  // data
  //
  //! the parser
  RagTime5Document &m_document;

  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
  //! the style manager
  std::shared_ptr<RagTime5StyleManager> m_styleManager;
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<RagTime5GraphInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
