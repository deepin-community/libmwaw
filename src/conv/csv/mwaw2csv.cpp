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
#include <string.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

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
  printf("Usage: mwaw2csv [OPTION] <Mac Spreadsheet Document>\n");
  printf("\n");
  printf("Options:\n");
  printf("\t-h:          Shows this help message\n");
  printf("\t-dc:         Sets the decimal commas to character c: default .\n");
  printf("\t-fc:         Sets the field separator to character c: default ,\n");
  printf("\t-tc:         Sets the text separator to character c: default \"\n");
  printf("\t-F:          Sets to output the formula which exists in the file\n");
  printf("\t-Dformat:    Sets the date format: default \"%%m/%%d/%%y\"\n");
  printf("\t-Tformat:    Sets the time format: default \"%%H:%%M:%%S\"\n");
  printf("\t-N:          Output the number of sheets \n");
  printf("\t-n num:      Sets the choose the sheet to convert (1: means first sheet) \n");
  printf("\t-o file.csv: Defines the ouput file\n");
  printf("\t-v:          Output mwaw2csv version\n");
  printf("\n");
  printf("Example:\n");
  printf("\tmwaw2cvs -d, -D\"%%d/%%m/%%y\" file : Converts a file using french locale\n");
  printf("\n");
  printf("Note:\n");
  printf("\t If -F is present, the formula are generated which english names\n");
  return -1;
}

static int printVersion()
{
  printf("mwaw2csv %s\n", VERSION);
  return 0;
}

int main(int argc, char *argv[])
{
  bool printHelp=false;
  bool printNumberOfSheet=false;
  bool generateFormula=false;
  char const *output = 0;
  int sheetToConvert=0;
  int ch;
  char decSeparator='.', fieldSeparator=',', textSeparator='"';
  std::string dateFormat("%m/%d/%y"), timeFormat("%H:%M:%S");

  while ((ch = getopt(argc, argv, "hvo:d:f:t:D:FNn:T:")) != -1) {
    switch (ch) {
    case 'D':
      dateFormat=optarg;
      break;
    case 'F':
      generateFormula=true;
      break;
    case 'N':
      printNumberOfSheet=true;
      break;
    case 'T':
      timeFormat=optarg;
      break;
    case 'd':
      decSeparator=optarg[0];
      break;
    case 'f':
      fieldSeparator=optarg[0];
      break;
    case 'n':
      sheetToConvert=std::atoi(optarg);
      break;
    case 't':
      textSeparator=optarg[0];
      break;
    case 'o':
      output=optarg;
      break;
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
  char const *file=argv[optind];

  MWAWDocument::Kind kind;
  auto confidence = MWAWDocument::MWAW_C_NONE;
  auto input=libmwawHelper::isSupported(file, confidence, kind);
  if (!input || confidence != MWAWDocument::MWAW_C_EXCELLENT) {
    fprintf(stderr,"ERROR: Unsupported file format!\n");
    return 1;
  }
  if (kind != MWAWDocument::MWAW_K_SPREADSHEET && kind != MWAWDocument::MWAW_K_DATABASE) {
    fprintf(stderr,"ERROR: not a spreadsheet!\n");
    return 1;
  }
  auto error=MWAWDocument::MWAW_R_OK;
  librevenge::RVNGStringVector vec;

  try {
    librevenge::RVNGCSVSpreadsheetGenerator listenerImpl(vec, generateFormula);
    listenerImpl.setSeparators(fieldSeparator, textSeparator, decSeparator);
    listenerImpl.setDTFormats(dateFormat.c_str(),timeFormat.c_str());
    error= MWAWDocument::parse(input.get(), &listenerImpl);
  }
  catch (MWAWDocument::Result const &err) {
    error=err;
  }
  catch (...) {
    error=MWAWDocument::MWAW_R_UNKNOWN_ERROR;
  }

  if (libmwawHelper::checkErrorAndPrintMessage(error))
    return 1;
  if (vec.empty()) {
    fprintf(stderr, "ERROR: can not find any sheet!\n");
    error = MWAWDocument::MWAW_R_PARSE_ERROR;
  }
  else if (sheetToConvert>0 && sheetToConvert>int(vec.size())) {
    fprintf(stderr, "ERROR: Can not find sheet %d\n", sheetToConvert);
    error = MWAWDocument::MWAW_R_PARSE_ERROR;
  }

  if (error != MWAWDocument::MWAW_R_OK)
    return 1;

  if (printNumberOfSheet) {
    std::cout << vec.size() << "\n";
    return 0;
  }

  if (!output)
    std::cout << vec[sheetToConvert>0 ? unsigned(sheetToConvert-1) : 0].cstr() << std::endl;
  else {
    std::ofstream out(output);
    out << vec[sheetToConvert>0 ? unsigned(sheetToConvert-1) : 0].cstr() << std::endl;
  }
  return 0;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
