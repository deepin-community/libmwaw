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
 * Parser to RagTime 5-6 document ( pipeline part )
 *
 */
#ifndef RAGTIME5_PIPELINE
#  define RAGTIME5_PIPELINE

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

namespace RagTime5PipelineInternal
{
struct ClusterPipeline;
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
class RagTime5Pipeline
{
  friend class RagTime5Document;

public:

  //! constructor
  explicit RagTime5Pipeline(RagTime5Document &doc);
  //! destructor
  virtual ~RagTime5Pipeline();

  /** returns the file version */
  int version() const;

protected:

  //! sends the data which have not yet been sent to the listener
  void flushExtra();

  // interface with main parser

  //! try to read a pipeline cluster
  std::shared_ptr<RagTime5ClusterManager::Cluster> readPipelineCluster(RagTime5Zone &zone, int zoneType);
  //! try to return the container's type corresponding to an id
  RagTime5ClusterManager::Cluster::Type getContainerType(int pipelineId) const;
  //! try to send the container corresponding to pipelineId (mainly unimplemented)
  bool send(int pipelineId, MWAWListenerPtr listener, MWAWPosition const &pos, int partId=0, double totalWidth=-1);

  //
  // Intermediate level
  //

  //
  // low level
  //

  //
  // send data
  //

public:
  //! debug: print a file type
  static std::string printType(unsigned long fileType)
  {
    return RagTime5StructManager::printType(fileType);
  }

private:
  RagTime5Pipeline(RagTime5Pipeline const &orig) = delete;
  RagTime5Pipeline &operator=(RagTime5Pipeline const &orig) = delete;

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
  std::shared_ptr<RagTime5PipelineInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
