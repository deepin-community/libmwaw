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

#include <stdio.h>
#include <unistd.h>

#include <cstring>

#include <librevenge/librevenge.h>
#include <librevenge-generators/librevenge-generators.h>
#include <librevenge-stream/librevenge-stream.h>

#include <libmwaw/libmwaw.hxx>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include "helper.h"

#ifndef VERSION
#define VERSION "UNKNOWN VERSION"
#endif

static int printUsage()
{
  printf("Usage: mwaw2html [OPTION] <Text Mac Document>\n");
  printf("\n");
  printf("Options:\n");
  printf("\t-h:                Shows this help message\n");
  printf("\t-v:                Output mwaw2html version \n");
  return -1;
}

static int printVersion()
{
  printf("mwaw2html %s\n", VERSION);
  return 0;
}

int main(int argc, char *argv[])
{
  char const *file = 0;
  bool printHelp=false;
  int ch;

  while ((ch = getopt(argc, argv, "hv")) != -1) {
    switch (ch) {
    case 'v':
      printVersion();
      return 0;
    default:
    case 'h':
      printHelp = true;
      break;
    }
  }
  if (argc != 1+optind || printHelp) {
    printUsage();
    return -1;
  }
  file=argv[optind];

  MWAWDocument::Kind kind;
  auto confidence = MWAWDocument::MWAW_C_NONE;
  auto input=libmwawHelper::isSupported(file, confidence, kind);
  if (!input || confidence != MWAWDocument::MWAW_C_EXCELLENT) {
    printf("ERROR: Unsupported file format!\n");
    return 1;
  }
  if (kind != MWAWDocument::MWAW_K_TEXT) {
    printf("ERROR: find a not text document!\n");
    return 1;
  }
  auto error=MWAWDocument::MWAW_R_OK;
  librevenge::RVNGString document;
  try {
    librevenge::RVNGHTMLTextGenerator documentGenerator(document);
    error = MWAWDocument::parse(input.get(), &documentGenerator);
  }
  catch (MWAWDocument::Result &err) {
    error=err;
  }
  catch (...) {
    error=MWAWDocument::MWAW_R_UNKNOWN_ERROR;
  }

  if (libmwawHelper::checkErrorAndPrintMessage(error))
    return 1;
  printf("%s", document.cstr());

  return 0;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
