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

#ifndef POWER_POINT7_STRUCT
#  define POWER_POINT7_STRUCT

#include <string>
#include <sstream>

#include "libmwaw_internal.hxx"

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

/** \brief namespace used to define basic struct of a Microsoft PowerPoint 95 files (Windows)
 */
namespace PowerPoint7Struct
{
//! a slide id
struct SlideId {
  explicit SlideId(unsigned long id=0)
    : m_id(int(id&0x7fffffffL))
    , m_isMaster((id&0x80000000L) ? true : false)
    , m_inNotes(false)
    , m_inHandout(false)
  {
  }
  //! returns true if the id is valid
  bool isValid() const
  {
    return m_isMaster || m_inHandout || m_id!=0;
  }
  //! operator<
  bool operator<(SlideId const &id) const
  {
    if (m_isMaster!=id.m_isMaster) return m_isMaster;
    if (m_inNotes!=id.m_inNotes) return m_inNotes;
    if (m_inHandout!=id.m_inHandout) return m_inHandout;
    return m_id < id.m_id;
  }
  //! operator==
  bool operator==(SlideId const &id) const
  {
    if (m_isMaster!=id.m_isMaster) return false;
    if (m_inNotes!=id.m_inNotes) return false;
    if (m_inHandout!=id.m_inHandout) return false;
    return m_id == id.m_id;
  }
  //! operator==
  bool operator!=(SlideId const &id) const
  {
    return !operator==(id);
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, SlideId const &id)
  {
    if (id.m_isMaster)
      o << "MS" << id.m_id;
    else if (id.m_id)
      o << "S" << id.m_id;
    if (id.m_inNotes)
      o << "[note]";
    if (id.m_inHandout)
      o << "Handout";
    return o;
  }
  //! the slide id
  int m_id;
  //! a flag to know if this is a master slide or a normal slide
  bool m_isMaster;
  //! a flag to know if the content is in the notes part
  bool m_inNotes;
  //! a flag to know if the content is in the handout part
  bool m_inHandout;
};
//! a zone header of a PowerPoint7Parser
struct Zone {
  //! constructor
  Zone() : m_type(0), m_dataSize(0)
  {
    for (auto &val : m_values) val=0;
  }
  //! try to read a zone header
  bool read(MWAWInputStreamPtr stream, long endPos=-1);
  //! returns a basic name
  std::string getName() const
  {
    std::stringstream s;
    s << "Zone" << std::hex << m_type << std::dec << "A";
    return s.str();
  }
  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, Zone const &z)
  {
    for (int i=0; i<6; ++i) {
      if (z.m_values[i]) o << "z" << i << "=" << z.m_values[i] << ",";
    }
    return o;
  }
  //! the type
  int m_type;
  //! the data size
  long m_dataSize;
  //! some value
  int m_values[6];
};
}
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
