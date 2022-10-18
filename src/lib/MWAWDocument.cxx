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

/** \file MWAWDocument.cxx
 * libmwaw API: implementation of main interface functions
 */

#include "MWAWHeader.hxx"
#include "MWAWGraphicDecoder.hxx"
#include "MWAWParser.hxx"
#include "MWAWPropertyHandler.hxx"
#include "MWAWRSRCParser.hxx"
#include "MWAWSpreadsheetDecoder.hxx"

#include <libmwaw/libmwaw.hxx>

#include "ActaParser.hxx"
#include "ApplePictParser.hxx"
#include "BeagleWksParser.hxx"
#include "BeagleWksBMParser.hxx"
#include "BeagleWksDBParser.hxx"
#include "BeagleWksDRParser.hxx"
#include "BeagleWksSSParser.hxx"
#include "CanvasParser.hxx"
#include "Canvas5BMParser.hxx"
#include "Canvas5Parser.hxx"
#include "ClarisDrawParser.hxx"
#include "ClarisWksParser.hxx"
#include "ClarisWksBMParser.hxx"
#include "ClarisWksPRParser.hxx"
#include "ClarisWksSSParser.hxx"
#include "CorelPainterParser.hxx"
#include "CricketDrawParser.hxx"
#include "DocMkrParser.hxx"
#include "DrawTableParser.hxx"
#include "EDocParser.hxx"
#include "FreeHandParser.hxx"
#include "FullWrtParser.hxx"
#include "GreatWksParser.hxx"
#include "GreatWksBMParser.hxx"
#include "GreatWksDBParser.hxx"
#include "GreatWksDRParser.hxx"
#include "GreatWksSSParser.hxx"
#include "HanMacWrdJParser.hxx"
#include "HanMacWrdKParser.hxx"
#include "JazzSSParser.hxx"
#include "JazzWriterParser.hxx"
#include "LightWayTxtParser.hxx"
#include "MacDocParser.hxx"
#include "MacDraftParser.hxx"
#include "MacDraft5Parser.hxx"
#include "MacDrawParser.hxx"
#include "MacDrawProParser.hxx"
#include "MacPaintParser.hxx"
#include "MacWrtParser.hxx"
#include "MacWrtProParser.hxx"
#include "MarinerWrtParser.hxx"
#include "MaxWrtParser.hxx"
#include "MindWrtParser.hxx"
#include "MoreParser.hxx"
#include "MouseWrtParser.hxx"
#include "MsWksParser.hxx"
#include "MsWksDBParser.hxx"
#include "MsWksDRParser.hxx"
#include "MsWksSSParser.hxx"
#include "MsWrd1Parser.hxx"
#include "MsWrdParser.hxx"
#include "MultiplanParser.hxx"
#include "NisusWrtParser.hxx"
#include "PixelPaintParser.hxx"
#include "PowerPoint1Parser.hxx"
#include "PowerPoint3Parser.hxx"
#include "PowerPoint7Parser.hxx"
#include "RagTimeParser.hxx"
#include "RagTime5Parser.hxx"
#include "RagTime5SSParser.hxx"
#include "ReadySetGoParser.hxx"
#include "ScoopParser.hxx"
#include "ScriptWriterParser.hxx"
#include "StudentWritingCParser.hxx"
#include "StyleParser.hxx"
#include "SuperPaintParser.hxx"
#include "TeachTxtParser.hxx"
#include "WingzParser.hxx"
#include "WordMakerParser.hxx"
#include "WriteNowParser.hxx"
#include "WriterPlsParser.hxx"
#include "ZWrtParser.hxx"

/** small namespace use to define private class/method used by MWAWDocument */
namespace MWAWDocumentInternal
{
std::shared_ptr<MWAWGraphicParser> getGraphicParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
std::shared_ptr<MWAWPresentationParser> getPresentationParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
std::shared_ptr<MWAWSpreadsheetParser> getSpreadsheetParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
std::shared_ptr<MWAWTextParser> getTextParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
MWAWHeader *getHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, bool strict);
bool checkBasicMacHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader &header, bool strict);
}

MWAWDocument::Confidence MWAWDocument::isFileFormatSupported(librevenge::RVNGInputStream *input,  MWAWDocument::Type &type, Kind &kind)
try
{
  type = MWAW_T_UNKNOWN;
  kind = MWAW_K_UNKNOWN;

  if (!input) {
    MWAW_DEBUG_MSG(("MWAWDocument::isFileFormatSupported(): no input\n"));
    return MWAW_C_NONE;
  }

  MWAWInputStreamPtr ip(new MWAWInputStream(input, false, true));
  MWAWInputStreamPtr rsrc=ip->getResourceForkStream();
  std::shared_ptr<MWAWRSRCParser> rsrcParser;
  if (rsrc)
    rsrcParser.reset(new MWAWRSRCParser(rsrc));
  std::shared_ptr<MWAWHeader> header;
#ifdef DEBUG
  header.reset(MWAWDocumentInternal::getHeader(ip, rsrcParser, false));
#else
  header.reset(MWAWDocumentInternal::getHeader(ip, rsrcParser, true));
#endif

  if (!header.get())
    return MWAW_C_NONE;
  type = static_cast<MWAWDocument::Type>(header->getType());
  kind = static_cast<MWAWDocument::Kind>(header->getKind());
  Confidence confidence = MWAW_C_NONE;

  switch (type) {
  case MWAW_T_ACTA:
  case MWAW_T_APPLEPICT:
  case MWAW_T_BEAGLEWORKS:
  case MWAW_T_CANVAS:
  case MWAW_T_CLARISDRAW:
  case MWAW_T_CLARISRESOLVE:
  case MWAW_T_CLARISWORKS:
  case MWAW_T_CORELPAINTER:
  case MWAW_T_CRICKETDRAW:
  case MWAW_T_DOCMAKER:
  case MWAW_T_DRAWINGTABLE:
  case MWAW_T_EDOC:
  case MWAW_T_FREEHAND:
  case MWAW_T_FULLPAINT:
  case MWAW_T_FULLWRITE:
  case MWAW_T_GREATWORKS:
  case MWAW_T_HANMACWORDJ:
  case MWAW_T_HANMACWORDK:
  case MWAW_T_LIGHTWAYTEXT:
  case MWAW_T_JAZZLOTUS:
  case MWAW_T_MACDOC:
  case MWAW_T_MACDRAFT:
  case MWAW_T_MACDRAW:
  case MWAW_T_MACDRAWPRO:
  case MWAW_T_MACPAINT:
  case MWAW_T_MACWRITE:
  case MWAW_T_MACWRITEPRO:
  case MWAW_T_MARINERWRITE:
  case MWAW_T_MAXWRITE:
  case MWAW_T_MINDWRITE:
  case MWAW_T_MORE:
  case MWAW_T_MOUSEWRITE:
  case MWAW_T_MICROSOFTMULTIPLAN:
  case MWAW_T_MICROSOFTWORD:
  case MWAW_T_MICROSOFTWORKS:
  case MWAW_T_NISUSWRITER:
  case MWAW_T_PIXELPAINT:
  case MWAW_T_POWERPOINT:
  case MWAW_T_RAGTIME:
  case MWAW_T_READYSETGO:
  case MWAW_T_SCOOP:
  case MWAW_T_SCRIPTWRITER:
  case MWAW_T_STUDENTWRITING:
  case MWAW_T_STYLE:
  case MWAW_T_SUPERPAINT:
  case MWAW_T_TEACHTEXT:
  case MWAW_T_TEXEDIT:
  case MWAW_T_WINGZ:
  case MWAW_T_WORDMAKER:
  case MWAW_T_WRITENOW:
  case MWAW_T_WRITERPLUS:
  case MWAW_T_XPRESS:
  case MWAW_T_ZWRITE:
    confidence = MWAW_C_EXCELLENT;
    break;

  case MWAW_T_ADOBEILLUSTRATOR:
  case MWAW_T_DBASE:
  case MWAW_T_FAMILYTREEMAKER:
  case MWAW_T_FILEMAKER:
  case MWAW_T_FOXBASE:
  case MWAW_T_FRAMEMAKER:
  case MWAW_T_FULLIMPACT:
  case MWAW_T_INFOGENIE:
  case MWAW_T_KALEIDAGRAPH:
  case MWAW_T_MICROSOFTFILE:
  case MWAW_T_OVERVUE:
  case MWAW_T_PAGEMAKER:
  case MWAW_T_SYMPOSIUM:
  case MWAW_T_TRAPEZE:
  case MWAW_T_4DIMENSION:

  case MWAW_T_RESERVED17:
  case MWAW_T_RESERVED18:
  case MWAW_T_RESERVED19:
  case MWAW_T_RESERVED20:
  case MWAW_T_RESERVED21:
  case MWAW_T_RESERVED22:
  case MWAW_T_RESERVED23:
  case MWAW_T_RESERVED24:
  case MWAW_T_RESERVED25:
  case MWAW_T_RESERVED26:
  case MWAW_T_RESERVED27:
  case MWAW_T_RESERVED28:
  case MWAW_T_RESERVED29:

  case MWAW_T_UNKNOWN:
#if !defined(__clang__)
  default:
#endif
    break;
  }

  return confidence;
}
catch (...)
{
  MWAW_DEBUG_MSG(("MWAWDocument::isFileFormatSupported: exception catched\n"));
  type = MWAW_T_UNKNOWN;
  kind = MWAW_K_UNKNOWN;
  return MWAW_C_NONE;
}

MWAWDocument::Result MWAWDocument::parse(librevenge::RVNGInputStream *input, librevenge::RVNGDrawingInterface *documentInterface, char const *)
try
{
  if (!input)
    return MWAW_R_UNKNOWN_ERROR;

  MWAWInputStreamPtr ip(new MWAWInputStream(input, false, true));
  MWAWInputStreamPtr rsrc=ip->getResourceForkStream();
  std::shared_ptr<MWAWRSRCParser> rsrcParser;
  if (rsrc) {
    rsrcParser.reset(new MWAWRSRCParser(rsrc));
    rsrcParser->setAsciiName("RSRC");
    rsrcParser->parse();
  }
  std::shared_ptr<MWAWHeader> header(MWAWDocumentInternal::getHeader(ip, rsrcParser, false));

  if (!header.get()) return MWAW_R_UNKNOWN_ERROR;

  auto parser=MWAWDocumentInternal::getGraphicParserFromHeader(ip, rsrcParser, header.get());
  if (!parser) return MWAW_R_UNKNOWN_ERROR;
  parser->parse(documentInterface);

  return MWAW_R_OK;
}
catch (libmwaw::FileException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: File exception trapped\n"));
  return MWAW_R_FILE_ACCESS_ERROR;
}
catch (libmwaw::ParseException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Parse exception trapped\n"));
  return MWAW_R_PARSE_ERROR;
}
catch (...)
{
  //fixme: too generic
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Unknown exception trapped\n"));
  return MWAW_R_UNKNOWN_ERROR;
}

MWAWDocument::Result MWAWDocument::parse(librevenge::RVNGInputStream *input, librevenge::RVNGPresentationInterface *documentInterface, char const *)
try
{
  if (!input)
    return MWAW_R_UNKNOWN_ERROR;

  MWAWInputStreamPtr ip(new MWAWInputStream(input, false, true));
  MWAWInputStreamPtr rsrc=ip->getResourceForkStream();
  std::shared_ptr<MWAWRSRCParser> rsrcParser;
  if (rsrc) {
    rsrcParser.reset(new MWAWRSRCParser(rsrc));
    rsrcParser->setAsciiName("RSRC");
    rsrcParser->parse();
  }
  std::shared_ptr<MWAWHeader> header(MWAWDocumentInternal::getHeader(ip, rsrcParser, false));

  if (!header.get()) return MWAW_R_UNKNOWN_ERROR;

  auto parser=MWAWDocumentInternal::getPresentationParserFromHeader(ip, rsrcParser, header.get());
  if (!parser) return MWAW_R_UNKNOWN_ERROR;
  parser->parse(documentInterface);

  return MWAW_R_OK;
}
catch (libmwaw::FileException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: File exception trapped\n"));
  return MWAW_R_FILE_ACCESS_ERROR;
}
catch (libmwaw::ParseException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Parse exception trapped\n"));
  return MWAW_R_PARSE_ERROR;
}
catch (...)
{
  //fixme: too generic
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Unknown exception trapped\n"));
  return MWAW_R_UNKNOWN_ERROR;
}

MWAWDocument::Result MWAWDocument::parse(librevenge::RVNGInputStream *input, librevenge::RVNGSpreadsheetInterface *documentInterface, char const *)
try
{
  if (!input)
    return MWAW_R_UNKNOWN_ERROR;

  MWAWInputStreamPtr ip(new MWAWInputStream(input, false, true));
  MWAWInputStreamPtr rsrc=ip->getResourceForkStream();
  std::shared_ptr<MWAWRSRCParser> rsrcParser;
  if (rsrc) {
    rsrcParser.reset(new MWAWRSRCParser(rsrc));
    rsrcParser->setAsciiName("RSRC");
    rsrcParser->parse();
  }
  std::shared_ptr<MWAWHeader> header(MWAWDocumentInternal::getHeader(ip, rsrcParser, false));

  if (!header.get()) return MWAW_R_UNKNOWN_ERROR;

  auto parser=MWAWDocumentInternal::getSpreadsheetParserFromHeader(ip, rsrcParser, header.get());
  if (!parser) return MWAW_R_UNKNOWN_ERROR;
  parser->parse(documentInterface);

  return MWAW_R_OK;
}
catch (libmwaw::FileException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: File exception trapped\n"));
  return MWAW_R_FILE_ACCESS_ERROR;
}
catch (libmwaw::ParseException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Parse exception trapped\n"));
  return MWAW_R_PARSE_ERROR;
}
catch (...)
{
  //fixme: too generic
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Unknown exception trapped\n"));
  return MWAW_R_UNKNOWN_ERROR;
}

MWAWDocument::Result MWAWDocument::parse(librevenge::RVNGInputStream *input, librevenge::RVNGTextInterface *documentInterface, char const * /*password*/)
try
{
  if (!input)
    return MWAW_R_UNKNOWN_ERROR;

  MWAWInputStreamPtr ip(new MWAWInputStream(input, false, true));
  MWAWInputStreamPtr rsrc=ip->getResourceForkStream();
  std::shared_ptr<MWAWRSRCParser> rsrcParser;
  if (rsrc) {
    rsrcParser.reset(new MWAWRSRCParser(rsrc));
    rsrcParser->setAsciiName("RSRC");
    rsrcParser->parse();
  }
  std::shared_ptr<MWAWHeader> header(MWAWDocumentInternal::getHeader(ip, rsrcParser, false));

  if (!header.get()) return MWAW_R_UNKNOWN_ERROR;

  auto parser=MWAWDocumentInternal::getTextParserFromHeader(ip, rsrcParser, header.get());
  if (!parser) return MWAW_R_UNKNOWN_ERROR;
  parser->parse(documentInterface);

  return MWAW_R_OK;
}
catch (libmwaw::FileException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: File exception trapped\n"));
  return MWAW_R_FILE_ACCESS_ERROR;
}
catch (libmwaw::ParseException)
{
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Parse exception trapped\n"));
  return MWAW_R_PARSE_ERROR;
}
catch (...)
{
  //fixme: too generic
  MWAW_DEBUG_MSG(("MWAWDocument::parse: Unknown exception trapped\n"));
  return MWAW_R_UNKNOWN_ERROR;
}

bool MWAWDocument::decodeGraphic(librevenge::RVNGBinaryData const &binary, librevenge::RVNGDrawingInterface *paintInterface)
try
{
  if (!paintInterface || !binary.size()) {
    MWAW_DEBUG_MSG(("MWAWDocument::decodeGraphic: called with no data or no converter\n"));
    return false;
  }
  MWAWGraphicDecoder tmpHandler(paintInterface);
  if (!tmpHandler.checkData(binary) || !tmpHandler.readData(binary)) return false;
  return true;
}
catch (...)
{
  MWAW_DEBUG_MSG(("MWAWDocument::decodeGraphic: unknown error\n"));
  return false;
}

bool MWAWDocument::decodeSpreadsheet(librevenge::RVNGBinaryData const &binary, librevenge::RVNGSpreadsheetInterface *sheetInterface)
try
{
  if (!sheetInterface || !binary.size()) {
    MWAW_DEBUG_MSG(("MWAWDocument::decodeSpreadsheet: called with no data or no converter\n"));
    return false;
  }
  MWAWSpreadsheetDecoder tmpHandler(sheetInterface);
  if (!tmpHandler.checkData(binary) || !tmpHandler.readData(binary)) return false;
  return true;
}
catch (...)
{
  MWAW_DEBUG_MSG(("MWAWDocument::decodeSpreadsheet: unknown error\n"));
  return false;
}

bool MWAWDocument::decodeText(librevenge::RVNGBinaryData const &, librevenge::RVNGTextInterface *)
{
  MWAW_DEBUG_MSG(("MWAWDocument::decodeText: unimplemented\n"));
  return false;
}

namespace MWAWDocumentInternal
{
/** return the header corresponding to an input. Or 0L if no input are found */
MWAWHeader *getHeader(MWAWInputStreamPtr &ip,
                      MWAWRSRCParserPtr const &rsrcParser,
                      bool strict)
try
{
  std::vector<MWAWHeader> listHeaders;

  if (!ip.get()) return nullptr;

  if (ip->hasDataFork()) {
    /** avoid very short file */
    if (!ip->hasResourceFork() && ip->size() < 10) return nullptr;

    ip->seek(0, librevenge::RVNG_SEEK_SET);
    ip->setReadInverted(false);
  }
  else if (!ip->hasResourceFork())
    return nullptr;

  listHeaders = MWAWHeader::constructHeader(ip, rsrcParser);
  size_t numHeaders = listHeaders.size();
  if (numHeaders==0) return nullptr;

  for (int step=0; step<2; ++step) {
    for (size_t i = 0; i < numHeaders; i++) {
      if (!MWAWDocumentInternal::checkBasicMacHeader(ip, rsrcParser, listHeaders[i], step==0))
        continue;
      return new MWAWHeader(listHeaders[i]);
    }
    if (strict) break;
  }
  return nullptr;
}
catch (libmwaw::FileException)
{
  MWAW_DEBUG_MSG(("MWAWDocumentInternal::MWAWDocument[getHeader]:File exception trapped\n"));
  return nullptr;
}
catch (libmwaw::ParseException)
{
  MWAW_DEBUG_MSG(("MWAWDocumentInternal::getHeader:Parse exception trapped\n"));
  return nullptr;
}
catch (...)
{
  //fixme: too generic
  MWAW_DEBUG_MSG(("MWAWDocumentInternal::getHeader:Unknown exception trapped\n"));
  return nullptr;
}

std::shared_ptr<MWAWGraphicParser> getGraphicParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
{
  std::shared_ptr<MWAWGraphicParser> parser;
  if (!header)
    return parser;
  if (header->getKind()!=MWAWDocument::MWAW_K_DRAW && header->getKind()!=MWAWDocument::MWAW_K_PAINT)
    return parser;
  if (header->getKind()==MWAWDocument::MWAW_K_DRAW && header->getType()==MWAWDocument::MWAW_T_CLARISWORKS)
    return parser;

  try {
    switch (header->getType()) {
    case MWAWDocument::MWAW_T_APPLEPICT:
      parser.reset(new ApplePictParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_BEAGLEWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_PAINT)
        parser.reset(new BeagleWksBMParser(input, rsrcParser, header));
      else
        parser.reset(new BeagleWksDRParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CANVAS:
      if (header->getKind()==MWAWDocument::MWAW_K_DRAW) {
        if (header->getMajorVersion()<=3)
          parser.reset(new CanvasParser(input, rsrcParser, header));
        else
          parser.reset(new Canvas5Parser(input, rsrcParser, header));
      }
      else if (header->getMajorVersion()>=5)
        parser.reset(new Canvas5BMParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_DRAWINGTABLE:
      parser.reset(new DrawTableParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CLARISDRAW:
      parser.reset(new ClarisDrawParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CLARISWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_PAINT)
        parser.reset(new ClarisWksBMParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CORELPAINTER:
      parser.reset(new CorelPainterParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CRICKETDRAW:
      parser.reset(new CricketDrawParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_FULLPAINT: // same format as MacPaint, so ok...
      parser.reset(new MacPaintParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_FREEHAND:
      parser.reset(new FreeHandParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_GREATWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_PAINT)
        parser.reset(new GreatWksBMParser(input, rsrcParser, header));
      else
        parser.reset(new GreatWksDRParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACDRAFT:
      if (header->getMajorVersion()<=1)
        parser.reset(new MacDraftParser(input, rsrcParser, header));
      else
        parser.reset(new MacDraft5Parser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACDRAW:
      parser.reset(new MacDrawParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACDRAWPRO:
      parser.reset(new MacDrawProParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACPAINT:
      parser.reset(new MacPaintParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MICROSOFTWORKS:
      parser.reset(new MsWksDRParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_PIXELPAINT:
      parser.reset(new PixelPaintParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_READYSETGO:
      parser.reset(new ReadySetGoParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_SCOOP:
      parser.reset(new ScoopParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_SUPERPAINT:
      parser.reset(new SuperPaintParser(input, rsrcParser, header));
      break;

    // TODO: first separate graphic format to other formats, then implement parser...
    case MWAWDocument::MWAW_T_ACTA:
    case MWAWDocument::MWAW_T_ADOBEILLUSTRATOR:
    case MWAWDocument::MWAW_T_CLARISRESOLVE:
    case MWAWDocument::MWAW_T_DBASE:
    case MWAWDocument::MWAW_T_DOCMAKER:
    case MWAWDocument::MWAW_T_EDOC:
    case MWAWDocument::MWAW_T_FAMILYTREEMAKER:
    case MWAWDocument::MWAW_T_FILEMAKER:
    case MWAWDocument::MWAW_T_FOXBASE:
    case MWAWDocument::MWAW_T_FRAMEMAKER:
    case MWAWDocument::MWAW_T_FULLIMPACT:
    case MWAWDocument::MWAW_T_FULLWRITE:
    case MWAWDocument::MWAW_T_INFOGENIE:
    case MWAWDocument::MWAW_T_JAZZLOTUS:
    case MWAWDocument::MWAW_T_KALEIDAGRAPH:
    case MWAWDocument::MWAW_T_HANMACWORDJ:
    case MWAWDocument::MWAW_T_HANMACWORDK:
    case MWAWDocument::MWAW_T_LIGHTWAYTEXT:
    case MWAWDocument::MWAW_T_MACDOC:
    case MWAWDocument::MWAW_T_MACWRITE:
    case MWAWDocument::MWAW_T_MACWRITEPRO:
    case MWAWDocument::MWAW_T_MARINERWRITE:
    case MWAWDocument::MWAW_T_MAXWRITE:
    case MWAWDocument::MWAW_T_MINDWRITE:
    case MWAWDocument::MWAW_T_MICROSOFTFILE:
    case MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN:
    case MWAWDocument::MWAW_T_MICROSOFTWORD:
    case MWAWDocument::MWAW_T_MORE:
    case MWAWDocument::MWAW_T_MOUSEWRITE:
    case MWAWDocument::MWAW_T_NISUSWRITER:
    case MWAWDocument::MWAW_T_OVERVUE:
    case MWAWDocument::MWAW_T_PAGEMAKER:
    case MWAWDocument::MWAW_T_POWERPOINT:
    case MWAWDocument::MWAW_T_RAGTIME:
    case MWAWDocument::MWAW_T_SCRIPTWRITER:
    case MWAWDocument::MWAW_T_STUDENTWRITING:
    case MWAWDocument::MWAW_T_STYLE:
    case MWAWDocument::MWAW_T_SYMPOSIUM:
    case MWAWDocument::MWAW_T_TEACHTEXT:
    case MWAWDocument::MWAW_T_TEXEDIT:
    case MWAWDocument::MWAW_T_TRAPEZE:
    case MWAWDocument::MWAW_T_WINGZ:
    case MWAWDocument::MWAW_T_WORDMAKER:
    case MWAWDocument::MWAW_T_WRITENOW:
    case MWAWDocument::MWAW_T_WRITERPLUS:
    case MWAWDocument::MWAW_T_XPRESS:
    case MWAWDocument::MWAW_T_ZWRITE:
    case MWAWDocument::MWAW_T_4DIMENSION:

    case MWAWDocument::MWAW_T_RESERVED17:
    case MWAWDocument::MWAW_T_RESERVED18:
    case MWAWDocument::MWAW_T_RESERVED19:
    case MWAWDocument::MWAW_T_RESERVED20:
    case MWAWDocument::MWAW_T_RESERVED21:
    case MWAWDocument::MWAW_T_RESERVED22:
    case MWAWDocument::MWAW_T_RESERVED23:
    case MWAWDocument::MWAW_T_RESERVED24:
    case MWAWDocument::MWAW_T_RESERVED25:
    case MWAWDocument::MWAW_T_RESERVED26:
    case MWAWDocument::MWAW_T_RESERVED27:
    case MWAWDocument::MWAW_T_RESERVED28:
    case MWAWDocument::MWAW_T_RESERVED29:

    case MWAWDocument::MWAW_T_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      break;
    }
  }
  catch (...) {
  }
  return parser;
}

std::shared_ptr<MWAWPresentationParser> getPresentationParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
{
  std::shared_ptr<MWAWPresentationParser> parser;
  if (!header)
    return parser;
  if (header->getKind()!=MWAWDocument::MWAW_K_PRESENTATION)
    return parser;

  try {
    switch (header->getType()) {
    case MWAWDocument::MWAW_T_CLARISWORKS:
      parser.reset(new ClarisWksPRParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_POWERPOINT:
      if (header->getMajorVersion()<=2)
        parser.reset(new PowerPoint1Parser(input, rsrcParser, header));
      else if (header->getMajorVersion()<=4)
        parser.reset(new PowerPoint3Parser(input, rsrcParser, header));
      else
        parser.reset(new PowerPoint7Parser(input, rsrcParser, header));
      break;

    case MWAWDocument::MWAW_T_ACTA:
    case MWAWDocument::MWAW_T_ADOBEILLUSTRATOR:
    case MWAWDocument::MWAW_T_APPLEPICT:
    case MWAWDocument::MWAW_T_BEAGLEWORKS:
    case MWAWDocument::MWAW_T_CANVAS:
    case MWAWDocument::MWAW_T_CLARISDRAW:
    case MWAWDocument::MWAW_T_CLARISRESOLVE:
    case MWAWDocument::MWAW_T_CORELPAINTER:
    case MWAWDocument::MWAW_T_CRICKETDRAW:
    case MWAWDocument::MWAW_T_DBASE:
    case MWAWDocument::MWAW_T_DOCMAKER:
    case MWAWDocument::MWAW_T_DRAWINGTABLE:
    case MWAWDocument::MWAW_T_EDOC:
    case MWAWDocument::MWAW_T_FAMILYTREEMAKER:
    case MWAWDocument::MWAW_T_FILEMAKER:
    case MWAWDocument::MWAW_T_FREEHAND:
    case MWAWDocument::MWAW_T_FOXBASE:
    case MWAWDocument::MWAW_T_FRAMEMAKER:
    case MWAWDocument::MWAW_T_FULLIMPACT:
    case MWAWDocument::MWAW_T_FULLPAINT:
    case MWAWDocument::MWAW_T_FULLWRITE:
    case MWAWDocument::MWAW_T_GREATWORKS:
    case MWAWDocument::MWAW_T_INFOGENIE:
    case MWAWDocument::MWAW_T_KALEIDAGRAPH:
    case MWAWDocument::MWAW_T_JAZZLOTUS:
    case MWAWDocument::MWAW_T_HANMACWORDJ:
    case MWAWDocument::MWAW_T_HANMACWORDK:
    case MWAWDocument::MWAW_T_LIGHTWAYTEXT:
    case MWAWDocument::MWAW_T_MACDOC:
    case MWAWDocument::MWAW_T_MACDRAFT:
    case MWAWDocument::MWAW_T_MACDRAW:
    case MWAWDocument::MWAW_T_MACDRAWPRO:
    case MWAWDocument::MWAW_T_MACPAINT:
    case MWAWDocument::MWAW_T_MACWRITE:
    case MWAWDocument::MWAW_T_MACWRITEPRO:
    case MWAWDocument::MWAW_T_MARINERWRITE:
    case MWAWDocument::MWAW_T_MAXWRITE:
    case MWAWDocument::MWAW_T_MINDWRITE:
    case MWAWDocument::MWAW_T_MICROSOFTFILE:
    case MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN:
    case MWAWDocument::MWAW_T_MICROSOFTWORD:
    case MWAWDocument::MWAW_T_MICROSOFTWORKS:
    case MWAWDocument::MWAW_T_MORE:
    case MWAWDocument::MWAW_T_MOUSEWRITE:
    case MWAWDocument::MWAW_T_NISUSWRITER:
    case MWAWDocument::MWAW_T_OVERVUE:
    case MWAWDocument::MWAW_T_PAGEMAKER:
    case MWAWDocument::MWAW_T_PIXELPAINT:
    case MWAWDocument::MWAW_T_RAGTIME:
    case MWAWDocument::MWAW_T_READYSETGO:
    case MWAWDocument::MWAW_T_SCOOP:
    case MWAWDocument::MWAW_T_SCRIPTWRITER:
    case MWAWDocument::MWAW_T_STUDENTWRITING:
    case MWAWDocument::MWAW_T_STYLE:
    case MWAWDocument::MWAW_T_SUPERPAINT:
    case MWAWDocument::MWAW_T_SYMPOSIUM:
    case MWAWDocument::MWAW_T_TEACHTEXT:
    case MWAWDocument::MWAW_T_TEXEDIT:
    case MWAWDocument::MWAW_T_TRAPEZE:
    case MWAWDocument::MWAW_T_WINGZ:
    case MWAWDocument::MWAW_T_WORDMAKER:
    case MWAWDocument::MWAW_T_WRITENOW:
    case MWAWDocument::MWAW_T_WRITERPLUS:
    case MWAWDocument::MWAW_T_XPRESS:
    case MWAWDocument::MWAW_T_ZWRITE:
    case MWAWDocument::MWAW_T_4DIMENSION:

    case MWAWDocument::MWAW_T_RESERVED17:
    case MWAWDocument::MWAW_T_RESERVED18:
    case MWAWDocument::MWAW_T_RESERVED19:
    case MWAWDocument::MWAW_T_RESERVED20:
    case MWAWDocument::MWAW_T_RESERVED21:
    case MWAWDocument::MWAW_T_RESERVED22:
    case MWAWDocument::MWAW_T_RESERVED23:
    case MWAWDocument::MWAW_T_RESERVED24:
    case MWAWDocument::MWAW_T_RESERVED25:
    case MWAWDocument::MWAW_T_RESERVED26:
    case MWAWDocument::MWAW_T_RESERVED27:
    case MWAWDocument::MWAW_T_RESERVED28:
    case MWAWDocument::MWAW_T_RESERVED29:

    case MWAWDocument::MWAW_T_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      break;
    }
  }
  catch (...) {
  }
  return parser;
}

/** Factory wrapper to construct a parser corresponding to an spreadsheet header */
std::shared_ptr<MWAWSpreadsheetParser> getSpreadsheetParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
{
  std::shared_ptr<MWAWSpreadsheetParser> parser;
  if (!header)
    return parser;
  if (header->getKind()!=MWAWDocument::MWAW_K_SPREADSHEET && header->getKind()!=MWAWDocument::MWAW_K_DATABASE)
    return parser;

  try {
    switch (header->getType()) {
    case MWAWDocument::MWAW_T_BEAGLEWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_SPREADSHEET)
        parser.reset(new BeagleWksSSParser(input, rsrcParser, header));
      else
        parser.reset(new BeagleWksDBParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CLARISRESOLVE:
      parser.reset(new WingzParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CLARISWORKS:
      parser.reset(new ClarisWksSSParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_GREATWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_SPREADSHEET)
        parser.reset(new GreatWksSSParser(input, rsrcParser, header));
      else
        parser.reset(new GreatWksDBParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_JAZZLOTUS:
      parser.reset(new JazzSSParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN:
      parser.reset(new MultiplanParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MICROSOFTWORKS:
      if (header->getKind()==MWAWDocument::MWAW_K_SPREADSHEET)
        parser.reset(new MsWksSSParser(input, rsrcParser, header));
      else
        parser.reset(new MsWksDBParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_RAGTIME:
      if (header->getMajorVersion()>=5)
        parser.reset(new RagTime5SSParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_WINGZ:
      parser.reset(new WingzParser(input, rsrcParser, header));
      break;
    // TODO
    case MWAWDocument::MWAW_T_FULLIMPACT:
    case MWAWDocument::MWAW_T_KALEIDAGRAPH:
    case MWAWDocument::MWAW_T_TRAPEZE:

    // no spreadsheet, but some database
    case MWAWDocument::MWAW_T_ACTA:
    case MWAWDocument::MWAW_T_ADOBEILLUSTRATOR:
    case MWAWDocument::MWAW_T_APPLEPICT:
    case MWAWDocument::MWAW_T_CANVAS:
    case MWAWDocument::MWAW_T_CLARISDRAW:
    case MWAWDocument::MWAW_T_CORELPAINTER:
    case MWAWDocument::MWAW_T_CRICKETDRAW:
    case MWAWDocument::MWAW_T_DBASE:
    case MWAWDocument::MWAW_T_DOCMAKER:
    case MWAWDocument::MWAW_T_DRAWINGTABLE:
    case MWAWDocument::MWAW_T_EDOC:
    case MWAWDocument::MWAW_T_FAMILYTREEMAKER:
    case MWAWDocument::MWAW_T_FILEMAKER:
    case MWAWDocument::MWAW_T_FOXBASE:
    case MWAWDocument::MWAW_T_FRAMEMAKER:
    case MWAWDocument::MWAW_T_FREEHAND:
    case MWAWDocument::MWAW_T_FULLPAINT:
    case MWAWDocument::MWAW_T_FULLWRITE:
    case MWAWDocument::MWAW_T_HANMACWORDJ:
    case MWAWDocument::MWAW_T_HANMACWORDK:
    case MWAWDocument::MWAW_T_INFOGENIE:
    case MWAWDocument::MWAW_T_LIGHTWAYTEXT:
    case MWAWDocument::MWAW_T_MACDOC:
    case MWAWDocument::MWAW_T_MACDRAFT:
    case MWAWDocument::MWAW_T_MACDRAW:
    case MWAWDocument::MWAW_T_MACDRAWPRO:
    case MWAWDocument::MWAW_T_MACPAINT:
    case MWAWDocument::MWAW_T_MACWRITE:
    case MWAWDocument::MWAW_T_MACWRITEPRO:
    case MWAWDocument::MWAW_T_MARINERWRITE:
    case MWAWDocument::MWAW_T_MAXWRITE:
    case MWAWDocument::MWAW_T_MICROSOFTFILE:
    case MWAWDocument::MWAW_T_MICROSOFTWORD:
    case MWAWDocument::MWAW_T_MINDWRITE:
    case MWAWDocument::MWAW_T_MORE:
    case MWAWDocument::MWAW_T_MOUSEWRITE:
    case MWAWDocument::MWAW_T_NISUSWRITER:
    case MWAWDocument::MWAW_T_OVERVUE:
    case MWAWDocument::MWAW_T_PAGEMAKER:
    case MWAWDocument::MWAW_T_POWERPOINT:
    case MWAWDocument::MWAW_T_PIXELPAINT:
    case MWAWDocument::MWAW_T_READYSETGO:
    case MWAWDocument::MWAW_T_SCOOP:
    case MWAWDocument::MWAW_T_SCRIPTWRITER:
    case MWAWDocument::MWAW_T_STUDENTWRITING:
    case MWAWDocument::MWAW_T_STYLE:
    case MWAWDocument::MWAW_T_SUPERPAINT:
    case MWAWDocument::MWAW_T_SYMPOSIUM:
    case MWAWDocument::MWAW_T_TEACHTEXT:
    case MWAWDocument::MWAW_T_TEXEDIT:
    case MWAWDocument::MWAW_T_WORDMAKER:
    case MWAWDocument::MWAW_T_WRITENOW:
    case MWAWDocument::MWAW_T_WRITERPLUS:
    case MWAWDocument::MWAW_T_XPRESS:
    case MWAWDocument::MWAW_T_ZWRITE:
    case MWAWDocument::MWAW_T_4DIMENSION:

    case MWAWDocument::MWAW_T_RESERVED17:
    case MWAWDocument::MWAW_T_RESERVED18:
    case MWAWDocument::MWAW_T_RESERVED19:
    case MWAWDocument::MWAW_T_RESERVED20:
    case MWAWDocument::MWAW_T_RESERVED21:
    case MWAWDocument::MWAW_T_RESERVED22:
    case MWAWDocument::MWAW_T_RESERVED23:
    case MWAWDocument::MWAW_T_RESERVED24:
    case MWAWDocument::MWAW_T_RESERVED25:
    case MWAWDocument::MWAW_T_RESERVED26:
    case MWAWDocument::MWAW_T_RESERVED27:
    case MWAWDocument::MWAW_T_RESERVED28:
    case MWAWDocument::MWAW_T_RESERVED29:

    case MWAWDocument::MWAW_T_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      break;
    }
  }
  catch (...) {
  }
  return parser;
}

/** Factory wrapper to construct a parser corresponding to an text header */
std::shared_ptr<MWAWTextParser> getTextParserFromHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
{
  std::shared_ptr<MWAWTextParser> parser;
  if (!header)
    return parser;
  // removeme: actually ClarisWorks draw file are exported as text file
  if (header->getKind()!=MWAWDocument::MWAW_K_TEXT &&
      (header->getKind()!=MWAWDocument::MWAW_K_DRAW || header->getType()!=MWAWDocument::MWAW_T_CLARISWORKS))
    return parser;
  try {
    switch (header->getType()) {
    case MWAWDocument::MWAW_T_ACTA:
      parser.reset(new ActaParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_BEAGLEWORKS:
      parser.reset(new BeagleWksParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_CLARISWORKS:
      parser.reset(new ClarisWksParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_DOCMAKER:
      parser.reset(new DocMkrParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_EDOC:
      parser.reset(new EDocParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_FULLWRITE:
      parser.reset(new FullWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_GREATWORKS:
      parser.reset(new GreatWksParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_HANMACWORDJ:
      parser.reset(new HanMacWrdJParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_HANMACWORDK:
      parser.reset(new HanMacWrdKParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_JAZZLOTUS:
      parser.reset(new JazzWriterParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_LIGHTWAYTEXT:
      parser.reset(new LightWayTxtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACDOC:
      parser.reset(new MacDocParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACWRITE:
      parser.reset(new MacWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MACWRITEPRO:
      parser.reset(new MacWrtProParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MARINERWRITE:
      parser.reset(new MarinerWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MAXWRITE:
      parser.reset(new MaxWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MICROSOFTWORD:
      if (header->getMajorVersion()==1)
        parser.reset(new MsWrd1Parser(input, rsrcParser, header));
      else
        parser.reset(new MsWrdParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MICROSOFTWORKS:
      parser.reset(new MsWksParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MINDWRITE:
      parser.reset(new MindWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MORE:
      parser.reset(new MoreParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_MOUSEWRITE:
      parser.reset(new MouseWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_NISUSWRITER:
      parser.reset(new NisusWrtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_RAGTIME:
      if (header->getMajorVersion()<5)
        parser.reset(new RagTimeParser(input, rsrcParser, header));
      else
        parser.reset(new RagTime5Parser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_SCRIPTWRITER:
      parser.reset(new ScriptWriterParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_STUDENTWRITING:
      parser.reset(new StudentWritingCParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_STYLE:
      parser.reset(new StyleParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_TEACHTEXT:
    case MWAWDocument::MWAW_T_TEXEDIT:
      parser.reset(new TeachTxtParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_WORDMAKER:
      parser.reset(new WordMakerParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_WRITENOW:
      parser.reset(new WriteNowParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_WRITERPLUS:
      parser.reset(new WriterPlsParser(input, rsrcParser, header));
      break;
    case MWAWDocument::MWAW_T_ZWRITE:
      parser.reset(new ZWrtParser(input, rsrcParser, header));
      break;

    case MWAWDocument::MWAW_T_ADOBEILLUSTRATOR:
    case MWAWDocument::MWAW_T_APPLEPICT:
    case MWAWDocument::MWAW_T_CANVAS:
    case MWAWDocument::MWAW_T_CLARISDRAW:
    case MWAWDocument::MWAW_T_CLARISRESOLVE:
    case MWAWDocument::MWAW_T_CORELPAINTER:
    case MWAWDocument::MWAW_T_CRICKETDRAW:
    case MWAWDocument::MWAW_T_DBASE:
    case MWAWDocument::MWAW_T_DRAWINGTABLE:
    case MWAWDocument::MWAW_T_FAMILYTREEMAKER:
    case MWAWDocument::MWAW_T_FILEMAKER:
    case MWAWDocument::MWAW_T_FOXBASE:
    case MWAWDocument::MWAW_T_FREEHAND:
    case MWAWDocument::MWAW_T_FULLIMPACT:
    case MWAWDocument::MWAW_T_FULLPAINT:
    case MWAWDocument::MWAW_T_FRAMEMAKER:
    case MWAWDocument::MWAW_T_INFOGENIE:
    case MWAWDocument::MWAW_T_KALEIDAGRAPH:
    case MWAWDocument::MWAW_T_MACDRAFT:
    case MWAWDocument::MWAW_T_MACDRAW:
    case MWAWDocument::MWAW_T_MACDRAWPRO:
    case MWAWDocument::MWAW_T_MACPAINT:
    case MWAWDocument::MWAW_T_MICROSOFTFILE:
    case MWAWDocument::MWAW_T_MICROSOFTMULTIPLAN:
    case MWAWDocument::MWAW_T_OVERVUE:
    case MWAWDocument::MWAW_T_PAGEMAKER:
    case MWAWDocument::MWAW_T_PIXELPAINT:
    case MWAWDocument::MWAW_T_POWERPOINT:
    case MWAWDocument::MWAW_T_READYSETGO:
    case MWAWDocument::MWAW_T_SCOOP:
    case MWAWDocument::MWAW_T_SUPERPAINT:
    case MWAWDocument::MWAW_T_SYMPOSIUM:
    case MWAWDocument::MWAW_T_TRAPEZE:
    case MWAWDocument::MWAW_T_WINGZ:
    case MWAWDocument::MWAW_T_XPRESS:
    case MWAWDocument::MWAW_T_4DIMENSION:

    case MWAWDocument::MWAW_T_RESERVED17:
    case MWAWDocument::MWAW_T_RESERVED18:
    case MWAWDocument::MWAW_T_RESERVED19:
    case MWAWDocument::MWAW_T_RESERVED20:
    case MWAWDocument::MWAW_T_RESERVED21:
    case MWAWDocument::MWAW_T_RESERVED22:
    case MWAWDocument::MWAW_T_RESERVED23:
    case MWAWDocument::MWAW_T_RESERVED24:
    case MWAWDocument::MWAW_T_RESERVED25:
    case MWAWDocument::MWAW_T_RESERVED26:
    case MWAWDocument::MWAW_T_RESERVED27:
    case MWAWDocument::MWAW_T_RESERVED28:
    case MWAWDocument::MWAW_T_RESERVED29:

    case MWAWDocument::MWAW_T_UNKNOWN:
#if !defined(__clang__)
    default:
#endif
      break;
    }
  }
  catch (...) {
  }
  return parser;
}

/** Wrapper to check a basic header of a mac file */
bool checkBasicMacHeader(MWAWInputStreamPtr &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader &header, bool strict)
try
{
  std::shared_ptr<MWAWParser> parser=getTextParserFromHeader(input, rsrcParser, &header);
  if (!parser)
    parser=getSpreadsheetParserFromHeader(input, rsrcParser, &header);
  if (!parser)
    parser=getGraphicParserFromHeader(input, rsrcParser, &header);
  if (!parser)
    parser=getPresentationParserFromHeader(input, rsrcParser, &header);
  if (!parser)
    return false;
  return parser->checkHeader(&header, strict);
}
catch (...)
{
  MWAW_DEBUG_MSG(("MWAWDocumentInternal::checkBasicMacHeader:Unknown exception trapped\n"));
  return false;
}

}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
