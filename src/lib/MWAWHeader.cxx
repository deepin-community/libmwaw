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

/** \file MWAWHeader.cxx
 * Implements MWAWHeader (document's type, version, kind)
 */

#include <string.h>
#include <iostream>

#include "libmwaw_internal.hxx"

#include "MWAWEntry.hxx"
#include "MWAWInputStream.hxx"
#include "MWAWRSRCParser.hxx"

#include "MWAWHeader.hxx"

MWAWHeader::MWAWHeader(MWAWDocument::Type documentType, int vers, MWAWDocument::Kind kind)
  : m_version(vers)
  , m_docType(documentType)
  , m_docKind(kind)
{
}

MWAWHeader::~MWAWHeader()
{
}

/**
 * So far, we have identified
 */
std::vector<MWAWHeader> MWAWHeader::constructHeader
(MWAWInputStreamPtr input, std::shared_ptr<MWAWRSRCParser> /*rsrcParser*/)
{
  std::vector<MWAWHeader> res;
  if (!input) return res;
  // ------------ first check finder info -------------
  std::string type, creator;
  if (input->getFinderInfo(type, creator) && !creator.empty()) {
    // set basic version, the correct will be filled by check header
    if (creator[0]=='A') {
      if (creator=="ACTA") {
        if (type=="OTLN") { // at least basic v2
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_ACTA, 1));
          return res;
        }
        else if (type=="otln") {   // classic version
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_ACTA, 2));
          return res;
        }
      }
      else if (creator=="AISW") {
        if (type=="SWDC" || type=="SWSP" || type=="SWWP") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SCRIPTWRITER, 1));
          return res;
        }
      }
      else if (creator=="APBP") {
        if (type=="APBL") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_DRAWINGTABLE, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="ARTX") { // Painter X
        if (type=="RIFF") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CORELPAINTER, 10, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
      }
    }
    else if (creator[0]=='B') {
      if (creator=="BOBO") {
        if (type=="CWDB" || type=="CWD2" || type=="sWDB") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_DATABASE));
          return res;
        }
        if (type=="CWGR" || type=="sWGR") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="CWSS" || type=="CWS2" || type=="sWSS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
        if (type=="CWPR") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_PRESENTATION));
          return res;
        }
        if (type=="CWPT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        if (type=="CWWP" || type=="CWW2" || type=="sWPP") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, 1, MWAWDocument::MWAW_K_TEXT));
          return res;
        }
      }
      else if (creator=="BWks") {
        if (type=="BWwp") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1));
          return res;
        }
        if (type=="BWdb") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_DATABASE));
          return res;
        }
        if (type=="BWdr") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="BWpt") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        if (type=="BWss") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
      }
    }
    else if (creator[0]=='C') {
      if (creator=="CDrw") {
        if (type=="dDrw" || type=="dDst" || type=="iLib") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISDRAW, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="CRDW") {
        if (type=="CKDT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CRICKETDRAW, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="C#+A") { // solo
        if (type=="C#+D" || type=="C#+F") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_RAGTIME, 5));
          return res;
        }
      }
    }
    else if (creator[0]=='D') {
      if (creator.substr(0,3)=="DAD") {
        if (creator=="DAD2") {
          if (type=="drw2") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 2, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
        }
        else if (creator=="DAD5") {
          if (type=="drw2") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 2, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
          if (type=="drw5" || type=="drwt") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 5, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
          if (type=="VINF") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 5, MWAWDocument::MWAW_K_PAINT));
            return res;
          }
        }
        else if (creator=="DAD6") {
          if (type=="drw6" || type=="drwt") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 6, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
          if (type=="VINF") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 6, MWAWDocument::MWAW_K_PAINT));
            return res;
          }
        }
        else if (creator=="DAD7") {
          if (type=="drw7" || type=="drwt") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 7, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
          if (type=="VINF") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 7, MWAWDocument::MWAW_K_PAINT));
            return res;
          }
        }
        else if (creator=="DAD8") {
          if (type=="drw8" || type=="drwt") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 8, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
          if (type=="VINF") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 8, MWAWDocument::MWAW_K_PAINT));
            return res;
          }
        }
        else if (creator=="DAD9" || creator=="DADX") {
          if (type=="drwX" || type=="drwt") {
            res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 9, MWAWDocument::MWAW_K_DRAW));
            return res;
          }
        }
      }
      else if (creator=="Dc@P" || creator=="Dk@P") {
        if (type=="APPL") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_DOCMAKER, 1));
          return res;
        }
      }
    }
    else if (creator[0]=='F') {
      if (creator=="FHA2") {
        if (type=="FHD2" || type=="FHT2") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FREEHAND, 2, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="FS03") {
        if (type=="WRT+") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITERPLUS, 1));
          return res;
        }
      }
      else if (creator=="FSPS" || creator=="FSDA") { // Fractal Design Painter or Dabbler
        if (type=="RIFF") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CORELPAINTER, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
      }
      else if (creator=="FSX3") {
        if (type=="RIFF") { // also FSFS list of uncompressed picture data for movie
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CORELPAINTER, 3, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
      }
      else if (creator=="FWRT") {
        if (type=="FWRM") { // 1.7 ?
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLWRITE, 1));
          return res;
        }
        if (type=="FWRT") { // 1.0 ?
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLWRITE, 1));
          return res;
        }
        if (type=="FWRI") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLWRITE,2));
          return res;
        }
      }
      else if (creator=="F#+A") { // Classic
        if (type=="F#+D" || type=="F#+F") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_RAGTIME, 3));
          return res;
        }
      }
    }
    else if (creator=="GM01") {
      if (type=="GfMt") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MOUSEWRITE, 1));
        return res;
      }
    }
    else if (creator=="HMiw") {   // japonese
      if (type=="IWDC") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_HANMACWORDJ,1));
        return res;
      }
    }
    else if (creator=="HMdr") {   // korean
      if (type=="DRD2") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_HANMACWORDK,1));
        return res;
      }
    }
    else if (creator=="JAZZ") {
      if (type=="JWPD") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_JAZZLOTUS,1));
        return res;
      }
      else if (type=="JWKS") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_JAZZLOTUS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
        return res;
      }
      else if (type=="JDBS") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_JAZZLOTUS, 1, MWAWDocument::MWAW_K_DATABASE));
        return res;
      }
    }
    else if (creator[0]=='L') {
      if (creator=="LMAN") {
        if (type=="TEXT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 7, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="LWTE") {
        if (type=="TEXT" || type=="ttro") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_LIGHTWAYTEXT,1));
          return res;
        }
      }
      else if (creator=="LWTR") {
        if (type=="APPL") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_LIGHTWAYTEXT,1));
          return res;
        }
      }
    }
    else if (creator[0]=='M') {
      if (creator=="MACA") {
        if (type=="WORD") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITE, 1));
          return res;
        }
      }
      else if (creator=="MACD") { // v1.0
        if (type=="DRWG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MART") {
        if (type=="RSGF") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        else if (type=="RSGI") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 2, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MAXW") {
        if (type=="MWCT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MAXWRITE, 1));
          return res;
        }
      }
      else if (creator=="MD40") {
        if (type=="MDDC" || type=="MSYM") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 4, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MDFT") { // v1.2
        if (type=="DRWG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MDPL") { // MacDraw II
        if (type=="DRWG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 0, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="STAT") { // stationery
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 0, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MDRW") {
        if (type=="DRWG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAW, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MDsr") {
        if (type=="APPL") { // auto content
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDOC, 1));
          return res;
        }
      }
      else if (creator=="MDvr") {
        if (type=="MDdc") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDOC, 1));
          return res;
        }
      }
      else if (creator=="MEMR") { // 4.5
        if (type=="RSGR") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 5, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MMBB") {
        if (type=="MBBT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MARINERWRITE, 1));
          return res;
        }
      }
      else if (creator=="MORE") {
        if (type=="MORE") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MORE, 1));
          return res;
        }
      }
      else if (creator=="MOR2") {
        if (type=="MOR2") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MORE, 2));
          return res;
        }
        if (type=="MOR3") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MORE, 3));
          return res;
        }
      }
      else if (creator=="MPNT") {
        if (type=="PNTG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACPAINT, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
      }
      else if (creator=="MRSN") {
        if (type=="RSGJ") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 3, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="RSGK") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 4, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="MSWD") {
        if (type=="WDBN" || type=="GLOS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORD, 3));
          return res;
        }
      }
      else if (creator=="MSWK") {
        if (type=="AWWP") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 3));
          return res;
        }
        if (type=="AWDB") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 3, MWAWDocument::MWAW_K_DATABASE));
          return res;
        }
        if (type=="AWDR") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 3, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="AWSS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 2, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
        if (type=="RLRB" || type=="sWRB") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 4));
          return res;
        }
      }
      else if (creator=="MWII") {   // MacWriteII
        if (type=="MW2D") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITEPRO, 0));
          return res;
        }
      }
      else if (creator=="MWPR") {
        if (type=="MWPd") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITEPRO, 1));
          return res;
        }
      }
    }
    else if (creator=="NISI") {
      if (type=="TEXT") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_NISUSWRITER, 1));
        return res;
      }
      if (type=="GLOS") { // checkme: glossary, ie. a list of picture/word, keep it ?
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_NISUSWRITER, 1));
        return res;
      }
      // "edtt": empty file, probably created when the file is edited
    }
    else if (creator[0]=='P') {
      if (creator=="PANT") {
        if (type=="PNTG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLPAINT, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
      }
      else if (creator=="PLAN") {
        if (type=="MPBN") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
      }
      else if (creator=="PIXR") {
        if (type=="PX01") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_PIXELPAINT, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="PPT3") {
        if (type=="SLD3") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, 3, MWAWDocument::MWAW_K_PRESENTATION));
          return res;
        }
      }
      else if (creator=="PPNT") {
        if (type=="SLDS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, 2, MWAWDocument::MWAW_K_PRESENTATION));
          return res;
        }
      }
      else if (creator=="PSIP") {
        if (type=="AWWP") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 1));
          return res;
        }
      }
      else if (creator=="PSI2") {
        if (type=="AWWP") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 2));
          return res;
        }
        if (type=="AWDB") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 2, MWAWDocument::MWAW_K_DATABASE));
          return res;
        }
        if (type=="AWSS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 2, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
      }
      else if (creator=="PWRI") {
        if (type=="OUTL") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MINDWRITE, 2));
          return res;
        }
      }
    }
    else if (creator[0]=='R') {
      if (creator=="Rslv") {
        if (type=="RsWs") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISRESOLVE, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
      }
      else if (creator=="R#+A") {
        if (type=="R#+D" || type=="R#+F") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_RAGTIME, 3));
          return res;
        }
      }
    }
    else if (creator[0]=='S') {
      if (creator=="Spud") {
        if (type=="SPUB") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SCOOP, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
      }
      else if (creator=="SPNT") {
        if (type=="SPTG") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SUPERPAINT, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        if (type=="PNTG") {
          // same as MacPaint format, so use the MacPaint parser
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACPAINT, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        // other type seems to correspond to basic picture file, so we do not accept them
      }
      else if (creator=="StAV") {
        if (type=="APPL") { // Style: document application
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_STYLE, 1));
          return res;
        }
      }
      else if (creator==std::string("St")+'\xd8'+"l") { //  argh, not standart character
        std::string type1=std::string("TEd")+'\xb6'; // argh, not standart character
        if (type==type1) {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_STYLE, 1));
          return res;
        }
      }
      else if (creator=="SWCM") {
        if (type=="JRNL" || type=="LTTR" || type=="NWSL" || type=="RPRT" || type=="SIGN") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_STUDENTWRITING, 1));
          return res;
        }
      }
    }
    else if (creator=="TBB5") {
      if (type=="TEXT" || type=="ttro") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_TEXEDIT, 1));
        return res;
      }
    }
    else if (creator[0]=='W') {
      if (creator=="WMkr") {
        if (type=="Word" || type=="WSta") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WORDMAKER, 1));
          return res;
        }
      }
      else if (creator=="WNGZ") {
        if (type=="WZSS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WINGZ, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
      }
      else if (creator=="WORD") {
        if (type=="WDBN") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORD, 1));
          return res;
        }
      }
    }
    else if (creator[0]=='Z') {
      if (creator=="ZEBR") {
        if (type=="ZWRT") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, 1));
          return res;
        }
        if (type=="ZOBJ") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, 1, MWAWDocument::MWAW_K_DRAW));
          return res;
        }
        if (type=="PNTG") {
          // same as MacPaint format, so use the MacPaint parser
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACPAINT, 1, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        if (type=="ZPNT") {
          /* the ZPNT(v2) are basic pct files with some resources, but
             we treat them to be complete */
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, 2, MWAWDocument::MWAW_K_PAINT));
          return res;
        }
        if (type=="ZCAL") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
          return res;
        }
        if (type=="ZDBS") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, 1, MWAWDocument::MWAW_K_DATABASE));
          return res;
        }
        // can we treat also ZOLN ?
      }
      else if (creator=="ZWRT") {
        if (type=="Zart") {
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_ZWRITE, 1));
          return res;
        }
      }
    }
    else if (creator=="aca3") {
      if (type=="acf3" || type=="act3") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FREEHAND, 1, MWAWDocument::MWAW_K_DRAW));
        return res;
      }
    }
    else if (creator=="dPro") {
      if (type=="dDoc") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 1, MWAWDocument::MWAW_K_DRAW));
        return res;
      }
      if (type=="dLib") { // macdraw pro slide/library
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 1, MWAWDocument::MWAW_K_DRAW));
        return res;
      }
    }
    else if (creator=="eDcR") {
      if (type=="eDoc") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_EDOC, 1));
        return res;
      }
    }
    else if (creator=="eSRD") {   // self reading application
      if (type=="APPL") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_EDOC, 1));
        return res;
      }
    }
    else if (creator=="nX^n") {
      if (type=="nX^d") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITENOW, 2));
        return res;
      }
      if (type=="nX^2") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITENOW, 3));
        return res;
      }
    }
    else if (creator=="ttxt") {
      if (type=="TEXT" || type=="ttro") {
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_TEACHTEXT, 1));
        return res;
      }
    }
    // check also basic type
    if (type=="PICT") {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_APPLEPICT, 1, MWAWDocument::MWAW_K_DRAW));
      return res;
    }
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: unknown finder info: type=%s[%s]\n", type.c_str(), creator.c_str()));

  }

  // ----------- now check resource fork ------------
  // ----------- now check data fork ------------
  if (!input->hasDataFork() || input->size() < 8)
    return res;

  input->seek(0, librevenge::RVNG_SEEK_SET);
  int val[5];
  for (auto &v : val) v = int(input->readULong(2));

  // ----------- clearly discriminant ------------------
  if (val[2] == 0x424F && val[3] == 0x424F && (val[0]>>8) < 7) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Claris Works file\n"));
    int vers= (val[0] >> 8);
    static int const typePos[7] = {0, 242, 248, 248, 256, 268, 278};
    int typeFile=-1;
    if (vers >= 1 && vers <= 6 && input->checkPosition(typePos[vers])) {
      input->seek(typePos[vers], librevenge::RVNG_SEEK_SET);
      typeFile=int(input->readLong(1));
    }
    switch (typeFile) {
    case 0:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_DRAW));
      return res;
    case 1:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_TEXT));
      return res;
    case 2:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_SPREADSHEET));
      return res;
    case 3:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_DATABASE));
      return res;
    case 4:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_PAINT));
      return res;
    case 5:
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISWORKS, vers, MWAWDocument::MWAW_K_PRESENTATION));
      return res;
    default:
      break;
    }
  }
  if (val[0]==0x5772 && val[1]==0x6974 && val[2]==0x654e && val[3]==0x6f77) {
    input->seek(8, librevenge::RVNG_SEEK_SET);
    auto version = int(input->readLong(2));

#ifdef DEBUG
    bool ok = (version >= 0 && version <= 3);
    if (ok)
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a WriteNow file version 3.0 or 4.0\n"));
    else
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a WriteNow file (unknown version %d)\n", version));
#else
    bool ok = version == 2;
#endif

    if (ok) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITENOW, 3));
      return res;
    }
  }
  if (val[0]==0x574e && val[1]==0x475a && val[2]==0x575a && val[3]==0x5353) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Wingz file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WINGZ, 1, MWAWDocument::MWAW_K_SPREADSHEET));
    return res;
  }
  if (val[0]==0x4241 && val[1]==0x545F && val[2]==0x4254 && val[3]==0x5353) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a ClarisResolve file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISRESOLVE, 1, MWAWDocument::MWAW_K_SPREADSHEET));
    return res;
  }
  if (val[0]==0x4323 && val[1]==0x2b44 && val[2]==0xa443 && val[3]==0x4da5) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a RagTime 5-6 file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_RAGTIME, 5));
    return res;
  }
  if (val[0]==0x4646 && val[1]==0x4646 && val[2]==0x3030 && val[3]==0x3030) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Mariner Write file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MARINERWRITE, 1));
    return res;
  }
  if (val[0]==0x000c && val[1]==0x1357 && (val[2]==0x13 || val[2]==0x14) && val[3]==0) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Drawing Table file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_DRAWINGTABLE, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==0x4257 && val[1]==0x6b73 && val[2]==0x4257) {
    if (val[3]==0x7770) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a BeagleWorks file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1));
      return res;
    }
    if (val[3]==0x6462) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a BeagleWorks Database file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_DATABASE));
      return res;
    }
    if (val[3]==0x6472) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a BeagleWorks Draw file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_DRAW));
      return res;
    }
    if (val[3]==0x7074) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a BeagleWorks Paint file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_PAINT));
      return res;
    }
    if (val[3]==0x7373) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a BeagleWorks Spreadsheet file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_BEAGLEWORKS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
      return res;
    }
  }
  if (val[0]==0 && val[1]==0 && val[2]==0 && val[3]==0x70 && val[4]==0x1100) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Scoop file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SCOOP, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==0x4452 && val[1]==0x5747) { // DRWG
    if (val[2]==0x4d44) { // MD
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraw file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAW, 1, MWAWDocument::MWAW_K_DRAW));
      return res;
    }
    if (val[2]==0 || val[2]==0x4432) { // D2
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraw II file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 0, MWAWDocument::MWAW_K_DRAW));
      // can also be a classic apple pict, so let's continue
    }
  }
  if (val[0]==0x1a54 && val[1]==0x4c43 && (val[2]&0xfeff)==0x246 && val[3]==0x4600) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Student Writing Center file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_STUDENTWRITING, 1));
  }
  if (val[0]==0x5354 && val[1]==0x4154 && (val[2]==0 || val[2]==0x4432)) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraw II template file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 0, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
#ifdef DEBUG
  // we need the resource fork to find the colors, patterns, ... ; so not active in normal mode
  if (val[0]==0x6444 && val[1]==0x6f63 && val[2]==0x4432) { // dDocD2
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraw Pro file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==0x644c && val[1]==0x6962 && val[2]==0x4432) { // dLibD2
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraw Pro template file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAWPRO, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
#endif
  // Canvas
  if (val[0]==0x200 && val[1]==0x80) {
    if (val[2]==0 && val[3]==0 && (val[4]>>8)<=8 && (val[4]&0xff)==0) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 5 file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 5, MWAWDocument::MWAW_K_DRAW));
    }
    else { // 0 followed by compression mode
      input->seek(9, librevenge::RVNG_SEEK_SET);
      auto len=input->readULong(4);
      if (len>=0x800 && len<=0x8000) { // block size
        auto len1=input->readULong(4);
        if (len1>0x800 && len1<=0x800c) {
          MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 6-8 file\n"));
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 6, MWAWDocument::MWAW_K_DRAW));
        }
      }
    }
  }
  if (val[0]==0x100 && val[1]==0x8000) {
    if ((val[2]>=0&&val[2]<=8) && val[3]==0 && (val[4]>>8)==0) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 5 win file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 5, MWAWDocument::MWAW_K_DRAW));
    }
    else {
      input->setReadInverted(true);
      input->seek(9, librevenge::RVNG_SEEK_SET);
      auto len=input->readULong(4);
      if (len>=0x800 && len<=0x8000) { // block size
        auto len1=input->readULong(4);
        if (len1>0x800 && len1<=0x800c) {
          MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 6-8 win file\n"));
          res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 6, MWAWDocument::MWAW_K_DRAW));
        }
      }
      input->setReadInverted(false);
    }
  }
  if (val[0]==0 && val[1]==0 && val[2]==0 && val[3]==0 && val[4]==0) {
    input->seek(10, librevenge::RVNG_SEEK_SET);
    int v=int(input->readULong(2));
    if ((v==0x100 && input->readULong(2)==0x8000) || // windows
        (v==0x200 && input->readULong(2)==0x80)) { // mac
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 9-11 file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, 9, MWAWDocument::MWAW_K_DRAW));
    }
  }
  if (val[0]==0 && (val[1]==1||val[1]==2) && val[2]==0x4441 && val[3]==0x4435 && val[4]==0x5052) {
    if (val[1]==1) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 5-8 image file\n"));
    }
    else {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 9-10 image file\n"));
    }
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, val[1]==1 ? 5 : 9, MWAWDocument::MWAW_K_PAINT));
  }
  if (val[0]==2 && val[1]==0 && val[2]==2 && val[3]==0x262 && val[4]==0x262) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacDraft file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==0x4859 && val[1]==0x4c53 && val[2]==0x0210) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a HanMac Word-K file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_HANMACWORDK, 1));
    return res;
  }
  if (val[0]==0x594c && val[1]==0x5953 && val[2]==0x100) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a HanMac Word-J file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_HANMACWORDJ, 1));
    return res;
  }
  if (val[0]==0x6163 && val[1]==0x6633 && val[2]<9) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a FreeHand v1\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FREEHAND, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==0x4648 && val[1]==0x4432 && val[2]<20) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a FreeHand v2\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FREEHAND, 2, MWAWDocument::MWAW_K_DRAW));
    return res;
  }
  if (val[0]==3 && val[1]==0x4d52 && val[2]==0x4949 && val[3]==0x80) { // MRII
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MORE, 2));
    return res;
  }
  if (val[0]==6 && val[1]==0x4d4f && val[2]==0x5233 && val[3]==0x80) { // MOR3
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MORE, 3));
    return res;
  }
  if ((val[0]==0x100||val[0]==0x200) && val[2]==0x4558 && val[3]==0x5057) { // CHANGEME: ClarisDraw
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CLARISDRAW, 1, MWAWDocument::MWAW_K_DRAW));
    return res;
  }

  if (val[0]==0x100 || val[0]==0x200) {
    if (val[1]==0x5a57 && val[2]==0x5254) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, val[0]==0x100 ? 1 : 2));
      return res;
    }
    if (val[1]==0x5a4f && val[2]==0x424a) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, val[0]==0x100 ? 1 : 2, MWAWDocument::MWAW_K_DRAW));
      return res;
    }
    if (val[1]==0x5a43 && val[2]==0x414C) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, val[0]==0x100 ? 1 : 2, MWAWDocument::MWAW_K_SPREADSHEET));
      return res;
    }
    if (val[1]==0x5a44 && val[2]==0x4253) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_GREATWORKS, val[0]==0x100 ? 1 : 2, MWAWDocument::MWAW_K_DATABASE));
      return res;
    }
    // maybe we can also add outline: if (val[1]==0x5a4f && val[2]==0x4c4e)
  }
  if (val[0]==0x11ab && val[1]==0 && val[2]==0x13e8 && val[3]==0) {
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN, 1, MWAWDocument::MWAW_K_SPREADSHEET));
    return res;
  }
  if (val[3]==6 && val[4]<6) {
    if (val[0]==0x4d44 && val[1]==0x4443 && val[2]==0x3230) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 4, MWAWDocument::MWAW_K_DRAW));
      return res;
    }
    // can be a library file, this will be test in the parser
    if (input->size()>=30)
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAFT, 4, MWAWDocument::MWAW_K_DRAW));
  }
  // magic ole header
  if (val[0]==0xd0cf && val[1]==0x11e0 && val[2]==0xa1b1 && val[3]==0x1ae1 && input->isStructured()) {
    MWAWInputStreamPtr mainOle = input->getSubStreamByName("MN0");
    if (mainOle && mainOle->readULong(4) == 0x43484e4b)
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 4));
    else if (mainOle && mainOle->size()>18) {
      mainOle->seek(16, librevenge::RVNG_SEEK_SET);
      auto value=static_cast<int>(mainOle->readULong(2));
      switch (value) {
      case 2:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 4, MWAWDocument::MWAW_K_DATABASE));
        break;
      case 3:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 4, MWAWDocument::MWAW_K_SPREADSHEET));
        break;
      case 12:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, 4, MWAWDocument::MWAW_K_DRAW));
        break;
      default:
        break;
      }
    }
    if (!mainOle && input->getSubStreamByName("PP40"))
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, 4, MWAWDocument::MWAW_K_PRESENTATION));
    else if (!mainOle && input->getSubStreamByName("PowerPoint Document") && input->getSubStreamByName("PersistentStorage Directory"))
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, 7, MWAWDocument::MWAW_K_PRESENTATION));
  }
  if (val[0]==0 && val[1]==2 && val[2]==11) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Jazz spreadsheet file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_JAZZLOTUS, 1, MWAWDocument::MWAW_K_SPREADSHEET));
  }

  if ((val[0]==0xfe32 && val[1]==0) || (val[0]==0xfe34 && val[1]==0) ||
      (val[0] == 0xfe37 && (val[1] == 0x23 || val[1] == 0x1c))) {
    int vers = -1;
    switch (val[1]) {
    case 0:
      if (val[0]==0xfe34) {
        MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Word 3.0 file\n"));
        vers = 3;
      }
      else if (val[0]==0xfe32) {
        MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Word 1.0 file\n"));
        vers = 1;
      }
      break;
    case 0x1c:
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Word 4.0 file\n"));
      vers = 4;
      break;
    case 0x23:
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Word 5.0 file\n"));
      vers = 5;
      break;
    default:
      break;
    }
    if (vers >= 0)
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORD, vers));
  }
  if (val[0]==0xbad && val[1]==0xdeed && val[2]==0 && (val[3]>=2 && val[3]<=3)) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Presentation file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, val[3], MWAWDocument::MWAW_K_PRESENTATION));
  }
  if (val[0]==0xedde && val[1]==0xad0b && val[3]==0 && (val[2]&0xFF)==0) {
    int vers=(val[2]>>8);
    if (vers>=2 && vers<=3) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Presentation file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_POWERPOINT, vers, MWAWDocument::MWAW_K_PRESENTATION));
    }
  }
  if (val[0]==0x4348 && val[1]==0x4e4b && val[2]==0x100 && val[3]==0) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Style file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_STYLE, 1));
  }
  if (val[0]==0x0447 && val[1]==0x4d30 && val[2]==0x3400) { // ^DGM04
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MouseWrite file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MOUSEWRITE, 1));
  }
  if (val[0]==0x1e && val[1]==0 && val[2]==0x86) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential ReadySetGo 3 file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 3, MWAWDocument::MWAW_K_DRAW));
  }
  if (val[0]==0x190 && (val[1]&0xff00)==0) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential ReadySetGo 4 file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 4, MWAWDocument::MWAW_K_DRAW));
  }
  // ----------- less discriminant ------------------
  if (val[0] == 0x2e && val[1] == 0x2e) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacWrite II file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITEPRO, 0));
  }
  if (val[0] == 4 && val[1] == 4) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MacWritePro file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITEPRO, 1));
  }
  if (val[0]==0x464f && val[1]==0x524d) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a WordMaker file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WORDMAKER, 1));
  }
  if (val[0] == 0x7704) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a MindWrite file 2.1\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MINDWRITE, 2));
  }
  if (val[0] == 0x78) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential ReadySetGo 1/2 file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 1, MWAWDocument::MWAW_K_DRAW));
  }
  if (val[0] == 0x138b) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential ReadySetGo 4.5 file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_READYSETGO, 5, MWAWDocument::MWAW_K_DRAW));
  }
  // ----------- other ------------------
  if (val[0]==0 && val[1]==0 && val[2]==0 && val[3]==0) {
    input->seek(8, librevenge::RVNG_SEEK_SET);
    auto value=static_cast<int>(input->readULong(1));
    if (value==0x4 || value==0x44) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a WriteNow 1.0 or 2.0 file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITENOW, 2));
    }
  }
  if (val[0]==0 && input->size() > 32) {
    input->seek(16, librevenge::RVNG_SEEK_SET);
    if (input->readLong(2)==0x688f && input->readLong(2)==0x688f) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a RagTime file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_RAGTIME));
    }
  }
  if (val[0]==0) {
    int vers = -1;
    switch (val[1]) {
    case 4:
      vers = 1;
      break;
    case 8:
      vers = 2;
      break;
    case 9:
      vers = 3;
      break;
    case 11: // embedded data
      vers = 4;
      break;
    default:
      break;
    }
    if (vers > 0 && input->size()>16) {
      input->seek(16, librevenge::RVNG_SEEK_SET);
      auto value=static_cast<int>(input->readULong(2));
      switch (value) {
      case 1:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, vers));
        break;
      case 2:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, vers, MWAWDocument::MWAW_K_DATABASE));
        break;
      case 3:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, vers, MWAWDocument::MWAW_K_SPREADSHEET));
        break;
      case 12:
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MICROSOFTWORKS, vers, MWAWDocument::MWAW_K_DRAW));
        break;
      default:
        break;
      }
    }
  }
  if (val[0]==0x4d44 && input->size()>=512) { // maybe a MacDraw 0 file, will be check later
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACDRAW, 0, MWAWDocument::MWAW_K_DRAW));
  }
  if (val[0]==2 && (val[1]&0xff)==0 && input->size() > 300) {
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CORELPAINTER, 1, MWAWDocument::MWAW_K_PAINT));
  }
  if (val[0] == 3 || val[0] == 6) {
    // version will be print by MacWrtParser::check
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACWRITE, val[0]));
  }
  if (val[0] == 0x110) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a Writerplus file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_WRITERPLUS, 1));
  }
  if (val[0] == 0x1000) {
    input->seek(10, librevenge::RVNG_SEEK_SET);
    auto value=static_cast<int>(input->readULong(2));
    // 1: bitmap, 2: vectorized graphic
    if (value==1)
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SUPERPAINT, 1, MWAWDocument::MWAW_K_PAINT));
    else if (value==2)
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SUPERPAINT, 1, MWAWDocument::MWAW_K_DRAW));
  }
  if (val[0]==0 && (val[1]==0x7FFF || val[1]==0x8000)) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential PixelPaint file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_PIXELPAINT, (val[1]==0x7FFF) ? 1  : 2, MWAWDocument::MWAW_K_PAINT));
  }
  if (val[0]>=1 && val[0]<=4) {
    int sSz=(val[1]>>8);
    if (sSz>=6 && sSz<=8) {
      // check if we find a date
      input->seek(3, librevenge::RVNG_SEEK_SET);
      bool ok=true;
      int numSlash=0;
      for (int i=0; i<sSz; ++i) {
        auto c=char(input->readULong(1));
        if (c>='0' && c<='9')
          continue;
        if (c=='/')
          ++numSlash;
        else {
          ok=false;
          break;
        }
      }
      if (ok && numSlash==2)
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CRICKETDRAW, 1, MWAWDocument::MWAW_K_DRAW));
    }
  }

  //
  // check for pict
  //
  for (int st=0; st<2; ++st) {
    if (!input->checkPosition(512*st+13))
      break;
    input->seek(512*st+10, librevenge::RVNG_SEEK_SET);
    auto value=static_cast<int>(input->readULong(2));
    if (value==0x1101) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_APPLEPICT, 1, MWAWDocument::MWAW_K_DRAW));
      break;
    }
    else if (value==0x11 && input->readULong(2)==0x2ff && input->readULong(2) == 0xC00) {
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_APPLEPICT, 2, MWAWDocument::MWAW_K_DRAW));
      break;
    }
  }
  //
  // middle of file
  //
  if (input->size()>=512+720*2) {
    // check for a MacPaint file
    input->seek(512, librevenge::RVNG_SEEK_SET);
    bool ok=true;
    // check the first 3 row
    for (int row=0; row<3; ++row) {
      int lastColor=-1;
      int col=0;
      while (col<72) {
        if (input->tell()+2>input->size()) {
          ok=false;
          break;
        }
        auto wh=static_cast<int>(input->readULong(1));
        if (wh>=0x81) {
          auto color=static_cast<int>(input->readULong(1));
          // consider that repeat color is anormal...
          if (col+(0x101-wh)>72 || (lastColor>=0 && color==lastColor)) {
            ok=false;
            break;
          }
          col+=(0x101-wh);
          lastColor=color;
          continue;
        }
        if (col+1+wh>72) {
          ok=false;
          break;
        }
        lastColor=-1;
        col += wh+1;
        input->seek(wh+1, librevenge::RVNG_SEEK_CUR);
      }
      if (!ok) break;
    }
    if (ok) {
      MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential MacPaint file\n"));
      res.push_back(MWAWHeader(MWAWDocument::MWAW_T_MACPAINT, 1, MWAWDocument::MWAW_K_PAINT));
    }
  }
  if ((val[0]>=0x82 && val[0]<=0x85) && val[1]<=0x2) {
    MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential ScriptWriter file\n"));
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_SCRIPTWRITER, 1));
  }
  if ((val[0]>0 || val[1]>=86) && input->size()>0x900) {
    input->seek(0x34, librevenge::RVNG_SEEK_SET);
    int littleEndian=int(input->readULong(1));
    if (littleEndian==1)
      input->setReadInverted(true);
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    int vers=int(input->readULong(2));
    if (littleEndian<=1 && (vers==1 || vers==2)) {
      input->seek(0, librevenge::RVNG_SEEK_SET);
      int numZero=0;
      for (int i=0; i<13; ++i) {
        int len=int(input->readLong(4));
        if (len<0) {
          numZero=1000;
          break;
        }
        if (len==0)
          ++numZero;
      }
      if (numZero<=2+littleEndian) {
        MWAW_DEBUG_MSG(("MWAWHeader::constructHeader: find a potential Canvas 2/3 file\n"));
        res.push_back(MWAWHeader(MWAWDocument::MWAW_T_CANVAS, vers+1, MWAWDocument::MWAW_K_DRAW));
      }
    }
    if (littleEndian==1)
      input->setReadInverted(false);
  }
  //
  //ok now look at the end of file
  //
  if (input->seek(-4, librevenge::RVNG_SEEK_END))
    return res;
  int lVal[2];
  for (auto &v : lVal) v=int(input->readULong(2));
  if (lVal[0] == 0x4E4C && lVal[1]==0x544F) // NLTO
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_ACTA, 2));
  else if (lVal[1]==0 && val[0]==1 && (val[1]==1||val[1]==2))
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_ACTA, 1));
  else if (lVal[0] == 0x4657 && lVal[1]==0x5254) // FWRT
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLWRITE, 2));
  else if (lVal[0] == 0 && lVal[1]==1) // not probable, but
    res.push_back(MWAWHeader(MWAWDocument::MWAW_T_FULLWRITE, 1));

  input->seek(0, librevenge::RVNG_SEEK_SET);
  return res;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
