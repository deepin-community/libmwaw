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

#ifndef HELPER_H
#  define HELPER_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <memory>

#  include <librevenge/librevenge.h>
#  include <libmwaw/libmwaw.hxx>

namespace libmwawHelper
{
/** check if a file is supported, if so returns the input stream
 the confidence, ... If not, returns an empty input stream.
*/
std::shared_ptr<librevenge::RVNGInputStream> isSupported
(char const *filename, MWAWDocument::Confidence &confidence, MWAWDocument::Kind &kind);
/** check for error, if yes, print an error message and returns
    true. If not return false */
bool checkErrorAndPrintMessage(MWAWDocument::Result result);
}
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
