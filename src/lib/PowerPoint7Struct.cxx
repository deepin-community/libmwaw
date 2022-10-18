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

#include "MWAWDebug.hxx"

#include "PowerPoint7Struct.hxx"

bool PowerPoint7Struct::Zone::read(MWAWInputStreamPtr input, long endPos)
{
  if (!input) {
    MWAW_DEBUG_MSG(("PowerPoint7Struct::Zone::read: called without input\n"));
    return false;
  }
  long pos=input->tell();
  long lastPos=endPos<0 ? input->size() : endPos;
  if (pos+16>lastPos || !input->checkPosition(lastPos))
    return false;
  m_type=int(input->readULong(2));
  for (int i=0; i<3; ++i) // z0=0|2|62|76-7b, z1=-1-4|f0e[12], z2=-1|0|2
    m_values[i]=int(input->readLong(2));
  m_dataSize=long(input->readULong(4));
  if (m_dataSize<0 || pos+16+m_dataSize>lastPos || lastPos-pos-16<m_dataSize) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  for (int i=0; i<2; ++i) // z3=0-ff, z4=0|f|b3|71|d1|dd|ff,
    m_values[i+3]=int(input->readULong(1));
  m_values[5]=int(input->readLong(2)); // -1|0|62|77|79
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
