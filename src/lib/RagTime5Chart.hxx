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
 * Parser to RagTime 5-6 document ( chart part )
 *
 */
#ifndef RAGTIME5_CHART
#  define RAGTIME5_CHART

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

namespace RagTime5ChartInternal
{
struct State;

class SubDocument;
}

class RagTime5Document;
class RagTime5StructManager;
class RagTime5StyleManager;
class RagTime5Zone;

/** \brief the main class to read the chart part of RagTime 56 file
 *
 *
 *
 */
class RagTime5Chart
{
  friend class RagTime5ChartInternal::SubDocument;
  friend class RagTime5Document;

public:
  //! constructor
  explicit RagTime5Chart(RagTime5Document &doc);
  //! destructor
  virtual ~RagTime5Chart();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

protected:

  //! sends the data which have not yet been sent to the listener
  void flushExtra();

  // interface with main parser

  //! try to read a chart cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> readChartCluster(RagTime5Zone &zone, int zoneType);

  //
  // Intermediate level
  //

  //
  // basic
  //

  //
  // low level
  //

  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }


private:
  RagTime5Chart(RagTime5Chart const &orig) = delete;
  RagTime5Chart &operator=(RagTime5Chart const &orig) = delete;

protected:
  //
  // data
  //
  //! the document
  RagTime5Document &m_document;

  //! the structure manager
  std::shared_ptr<RagTime5StructManager> m_structManager;
  //! the style manager
  std::shared_ptr<RagTime5StyleManager> m_styleManager;
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<RagTime5ChartInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
