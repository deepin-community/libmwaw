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
 * Copyright (C) 2006 Ariya Hidayat (ariya@kde.org)
 * Copyright (C) 2004 Marc Oude Kotte (marc@solcon.nl)
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

#ifndef MWAW_GRAPHIC_DECODER_HXX
#define MWAW_GRAPHIC_DECODER_HXX

#include <librevenge/librevenge.h>
#include "libmwaw_internal.hxx"

#include "MWAWPropertyHandler.hxx"

/** main class used to decode a librevenge::RVNGBinaryData created by
    \see MWAWGraphicEncoder (with mimeType="image/mwaw-odg") and to send
    it contents to librevenge::RVNGDrawingInterface
*/
class MWAWGraphicDecoder final : public MWAWPropertyHandler
{
public:
  /** constructor */
  explicit MWAWGraphicDecoder(librevenge::RVNGDrawingInterface *output)
    : MWAWPropertyHandler()
    , m_output(output) { }
  /** destructor */
  ~MWAWGraphicDecoder() final {}

  /** insert an element */
  void insertElement(const char *psName) final ;
  /** insert an element ( with a librevenge::RVNGPropertyList ) */
  void insertElement(const char *psName, const librevenge::RVNGPropertyList &xPropList) final;
  /** insert an element ( with a librevenge::RVNGPropertyListVector parameter ) */
  void insertElement(const char *psName, const librevenge::RVNGPropertyList &xPropList,
                     const librevenge::RVNGPropertyListVector &vector);
  /** insert a sequence of character */
  void characters(const librevenge::RVNGString &sCharacters) final
  {
    if (!m_output) return;
    m_output->insertText(sCharacters);
  }
private:
  /// copy constructor (undefined)
  MWAWGraphicDecoder(MWAWGraphicDecoder const &) = delete;
  /// operator= (undefined)
  MWAWGraphicDecoder operator=(MWAWGraphicDecoder const &) = delete;
  /** the interface output */
  librevenge::RVNGDrawingInterface *m_output;
};

#endif

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
