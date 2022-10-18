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
 * Parser to RagTime 5-6 document ( layout part )
 *
 */
#ifndef RAGTIME5_LAYOUT
#  define RAGTIME5_LAYOUT

#include <set>
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

namespace RagTime5LayoutInternal
{
struct ClusterLayout;
struct State;
}

class RagTime5Document;
class RagTime5StructManager;
class RagTime5Zone;

/** \brief the main class to read the text part of RagTime 56 file
 *
 *
 *
 */
class RagTime5Layout
{
  friend class RagTime5Document;

public:

  //! constructor
  explicit RagTime5Layout(RagTime5Document &doc);
  //! destructor
  virtual ~RagTime5Layout();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

protected:

  //! sends the data which have not yet been sent to the listener
  void flushExtra();

  // interface with main parser

  //! try to read a layout cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> readLayoutCluster(RagTime5Zone &zone, int zoneType);

  //
  // Intermediate level
  //

  //! update all internal data: list of master layout, ...
  void updateLayouts();

  //! check that a layout is valid, ...
  void updateLayout(RagTime5LayoutInternal::ClusterLayout &layout);
  //
  // low level
  //

  //
  // send data
  //

  //! try to send the page content corresponding to the layout
  bool sendPageContents();
  //! try to send the cluster zone
  bool send(RagTime5LayoutInternal::ClusterLayout &cluster, MWAWListenerPtr listener, int page);

public:
  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }

private:
  RagTime5Layout(RagTime5Layout const &orig) = delete;
  RagTime5Layout &operator=(RagTime5Layout const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser
  RagTime5Document &m_document;

  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<RagTime5LayoutInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
