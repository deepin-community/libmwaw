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

/** \file MWAWDocument.hxx
 * libmwaw API: main interface functions of the libmwaw
 *
 * \see libmwaw.hxx
 */
#ifndef MWAWDOCUMENT_HXX
#define MWAWDOCUMENT_HXX

#ifdef _WINDLL
#  ifdef BUILD_MWAW
#    define MWAWLIB _declspec(dllexport)
#  else
#    define MWAWLIB _declspec(dllimport)
#  endif
#else // !DLL_EXPORT
#  ifdef LIBMWAW_VISIBILITY
#    define MWAWLIB __attribute__((visibility("default")))
#  else
#    define MWAWLIB
#  endif
#endif

namespace librevenge
{
class RVNGBinaryData;
class RVNGDrawingInterface;
class RVNGPresentationInterface;
class RVNGSpreadsheetInterface;
class RVNGTextInterface;
class RVNGInputStream;
}

/**
This class provides all the functions needed by applications to parse many pre-MacOSX documents.
*/
class MWAWDocument
{
public:
  /** an enum which defines if we have confidence that a file is supported */
  enum Confidence {
    MWAW_C_NONE=0/**< not supported */,
    MWAW_C_UNSUPPORTED_ENCRYPTION /** encryption not supported*/,
    MWAW_C_SUPPORTED_ENCRYPTION /** encryption supported */,
    MWAW_C_EXCELLENT /** supported */
  };
  /** an enum to define the kind of document */
  enum Kind {
    MWAW_K_UNKNOWN=0 /**< unknown*/,
    MWAW_K_TEXT /** word processing file*/,
    MWAW_K_DRAW /** vectorized grphic*/,
    MWAW_K_PAINT /** bitmap graphic*/,
    MWAW_K_PRESENTATION /** presentation graphic*/,
    MWAW_K_SPREADSHEET /** spreadsheet */,
    MWAW_K_DATABASE /** database */
  };
  /** an enum which defines the result of the file parsing */
  enum Result {
    MWAW_R_OK=0 /**< conversion ok*/,
    MWAW_R_FILE_ACCESS_ERROR /** problem when accessing file*/,
    MWAW_R_OLE_ERROR /** problem when reading the OLE structure*/,
    MWAW_R_PARSE_ERROR /** problem when parsing the file*/,
    MWAW_R_PASSWORD_MISSMATCH_ERROR /** problem when using the given password*/,
    MWAW_R_UNKNOWN_ERROR /** unknown error*/
  };
  /** an enum to define the different type of document */
  enum Type {
    MWAW_T_UNKNOWN=0 /**< Unrecognised file type*/,
    MWAW_T_ACTA /**Acta (v2 and Classic v1)*/,
    MWAW_T_ADOBEILLUSTRATOR /** Adobe Illustrator: TODO */,
    MWAW_T_BEAGLEWORKS /**BeagleWorks (v1.0)/WordPerfect Works (v1.2): export database(as spreadsheet), draw, paint, spreadsheet and text files.*/,
    MWAW_T_CLARISRESOLVE /**Claris Resolve (v1.1)*/,
    MWAW_T_CLARISWORKS /**ClarisWorks/AppleWorks: all versions, export database (as spreadsheet), draw(as text), paint, presentation, spreadsheet, text document*/,
    MWAW_T_DBASE /** DBase: TODO*/,
    MWAW_T_DOCMAKER /** DocMaker (v4)*/,
    MWAW_T_EDOC /** eDOC (v2)*/,
    MWAW_T_FAMILYTREEMAKER /** Family Tree Maker: TODO*/,
    MWAW_T_FILEMAKER /** FileMaker: TODO*/,
    MWAW_T_FOXBASE /** FoxBase: TODO*/,
    MWAW_T_FRAMEMAKER /** FrameMaker: TODO*/,
    MWAW_T_FULLIMPACT /** FullImpact: TODO*/,
    MWAW_T_FULLPAINT /** FullPaint: v1 ( same format as MacPaint file)*/,
    MWAW_T_FULLWRITE /** FullWrite Professional: basic*/,
    MWAW_T_GREATWORKS /** GreatWorks (v1-v2): export text, drawing, paint and spreadsheet document.*/,
    MWAW_T_HANMACWORDJ /** HanMac Word-J (v2.0.4) */,
    MWAW_T_HANMACWORDK /** HanMac Word-K (v2.0.5-2.0.6) */,
    MWAW_T_INFOGENIE /** InfoGenie: TODO*/,
    MWAW_T_KALEIDAGRAPH /** Kaleida Graph: TODO*/,
    MWAW_T_LIGHTWAYTEXT /** LightWayText (only v4 Mac format)*/,
    MWAW_T_MACDOC /** MacDoc (v1.3)*/,
    MWAW_T_MACDRAFT /** MacDraft: v1, v4-v5*/,
    MWAW_T_MACDRAW /** MacDraw: v0-v1 */,
    MWAW_T_MACDRAWPRO /** MacDraw II: v1.0-v1.1, MacDraw Pro: v1*/,
    MWAW_T_MACPAINT /** MacPaint: v1-v2 */,
    MWAW_T_MARINERWRITE /** Mariner Write (only v1.6-v3.5 Mac Classic) */,
    MWAW_T_MINDWRITE /** MindWrite */,
    MWAW_T_MORE /** More (v2-3): retrieve the organization part but not the slide/tree parts*/,
    MWAW_T_MICROSOFTFILE /** Microsoft File(data base): TODO*/,
    MWAW_T_MICROSOFTMULTIPLAN /** Microsoft Multiplan: v1.11 */,
    MWAW_T_MICROSOFTWORD /** Microsoft Word  (v1-v5)*/,
    MWAW_T_MICROSOFTWORKS /** Microsoft Works Mac: export database(as spreadsheet), graphic, spreadsheet and text files.*/,
    MWAW_T_MACWRITE /** MacWrite */,
    MWAW_T_MACWRITEPRO /** MacWrite II/Pro*/,
    MWAW_T_NISUSWRITER /** Nisus Writer (v3.4-v6.5)*/,
    MWAW_T_OVERVUE /** OverVUE: TODO*/,
    MWAW_T_PAGEMAKER /** PageMaker: TODO*/,
    MWAW_T_PIXELPAINT /** PixelPaint: v1-v2.1 */,
    /** RagTime:
        -Mac v2.1-v3.2: done,
        -all v5.0-v6.5: incomplete, only try to retrieve the picture/shape/textboxes
     */
    MWAW_T_RAGTIME,
    MWAW_T_READYSETGO /** Ready,Set,Go!: v1-v4.5 (only Mac)*/,
    /** SuperPaint: export drawing and paint v1 document

     \note the other documents v2-v3 seems to be basic MacPaint/Pict
     files */
    MWAW_T_SUPERPAINT,
    MWAW_T_SYMPOSIUM /** Symposium: TODO*/,
    MWAW_T_TEACHTEXT /** TeachText/SimpleText*/,
    MWAW_T_TEXEDIT /** Tex-Edit (v2)*/,
    MWAW_T_TRAPEZE /** Trapeze spreadsheet: TODO*/,
    MWAW_T_WINGZ /** Wingz (v1.1)*/,
    MWAW_T_WRITENOW /** WriteNow*/,
    MWAW_T_WRITERPLUS /** WriterPlus*/,
    MWAW_T_XPRESS /** XPress: TODO*/,
    MWAW_T_ZWRITE /** Z-Write (v1.3)*/,
    MWAW_T_4DIMENSION /** 4th Dimension: TODO*/,

    MWAW_T_RESERVED1 /** Used for ClarisDraw v1 file: see below */,
    MWAW_T_RESERVED2 /** Used for Apple's Pict file: see below */,
    MWAW_T_RESERVED3 /** Used for Style v1.6: see below */,
    MWAW_T_RESERVED4 /** Used for FreeHand: see below */,
    MWAW_T_RESERVED5 /** Used for Cricket Draw: see below */,
    MWAW_T_RESERVED6 /** Used for MaxWrite: see below */,
    MWAW_T_RESERVED7 /** Used for MouseWrite: see below */,
    MWAW_T_RESERVED8 /** Used for PowerPoint: see below */,
    MWAW_T_RESERVED9 /** Used for Corel Painter: see below*/,

    //
    // the reserved 10-29 were added in libmwaw 0.3.17
    //
    MWAW_T_RESERVED10 /** Used for Deneba Canvas: see below */,
    MWAW_T_RESERVED11 /** Used for Drawing Table: see below */,
    MWAW_T_RESERVED12 /** Used for Jazz Lotus: see below */,
    MWAW_T_RESERVED13 /** Used for WordMaker: see below */,
    MWAW_T_RESERVED14 /** Used for Student Writing Center: see below */,
    MWAW_T_RESERVED15 /** Used for Script Writer: see below */,
    MWAW_T_RESERVED16 /** Used for Scoop: see below */,
    MWAW_T_RESERVED17 /** Reserved for future used */,
    MWAW_T_RESERVED18 /** Reserved for future used */,
    MWAW_T_RESERVED19 /** Reserved for future used */,
    MWAW_T_RESERVED20 /** Reserved for future used */,
    MWAW_T_RESERVED21 /** Reserved for future used */,
    MWAW_T_RESERVED22 /** Reserved for future used */,
    MWAW_T_RESERVED23 /** Reserved for future used */,
    MWAW_T_RESERVED24 /** Reserved for future used */,
    MWAW_T_RESERVED25 /** Reserved for future used */,
    MWAW_T_RESERVED26 /** Reserved for future used */,
    MWAW_T_RESERVED27 /** Reserved for future used */,
    MWAW_T_RESERVED28 /** Reserved for future used */,
    MWAW_T_RESERVED29 /** Reserved for future used */,

    /**Claris Draw: v1.0.1-v1.0.3.
       \note same enum as \a MWAW_T_RESERVED1 */
    MWAW_T_CLARISDRAW=MWAW_T_RESERVED1,
    /**Apple Pict: v1 or v2.
       \note same enum as \a MWAW_T_RESERVED2*/
    MWAW_T_APPLEPICT=MWAW_T_RESERVED2,
    /**Style: v1.6, v1.9
       \note same enum as \a MWAW_T_RESERVED3*/
    MWAW_T_STYLE=MWAW_T_RESERVED3,
    /**FreeHand: v1
       \note same enum as \a MWAW_T_RESERVED4*/
    MWAW_T_FREEHAND=MWAW_T_RESERVED4,
    /**Cricket Draw: v1.0.1 and v1.1.1
       \note same enum as \a MWAW_T_RESERVED5*/
    MWAW_T_CRICKETDRAW=MWAW_T_RESERVED5,
    /**MaxWrite: v1
       \note same enum as \a MWAW_T_RESERVED6*/
    MWAW_T_MAXWRITE=MWAW_T_RESERVED6,
    /** MouseWrite: v1
     \note same enum as \a MWAW_T_RESERVED7*/
    MWAW_T_MOUSEWRITE=MWAW_T_RESERVED7,
    /** PowerPoint: v1-v4 and pc v2-v4,95
     \note same enum as \a MWAW_T_RESERVED8*/
    MWAW_T_POWERPOINT=MWAW_T_RESERVED8,
    /** Corel Painter: Fractal Design Painter Mac v1-v4, MetaCreations Mac v5-v6, Corel Painter Mac v7-v10
        and Fractal Design Painter Windows v3
     \note same enum as \a MWAW_T_RESERVED9
     */
    MWAW_T_CORELPAINTER=MWAW_T_RESERVED9,
    /** Deneba Canvas mac: v2-v3 and v5-v8, windows: v3 and v5-v6 and probably v7-v8
        ADC Canvas mac: v9-v10, windows: probably v9-v11
     \note same enum as \a MWAW_T_RESERVED10
    */
    MWAW_T_CANVAS=MWAW_T_RESERVED10,
    /** Drawing Table: v1
     \note same enum as \a MWAW_T_RESERVED11
    */
    MWAW_T_DRAWINGTABLE=MWAW_T_RESERVED11,
    /** Jazz (Lotus): v1 ; retrieve the spreadsheet and if the resource fork is available, the writer document
        \note same enum as \a MWAW_T_RESERVED12
    */
    MWAW_T_JAZZLOTUS=MWAW_T_RESERVED12,
    /** WordMaker: v1
        \note same enum as \a MWAW_T_RESERVED13
     */
    MWAW_T_WORDMAKER=MWAW_T_RESERVED13,
    /** Student Writing Center: v1
        \note same enum as \a MWAW_T_RESERVED14
     */
    MWAW_T_STUDENTWRITING=MWAW_T_RESERVED14,
    /** Script Writer: v1.3
        \note same enum as \a MWAW_T_RESERVED15
     */
    MWAW_T_SCRIPTWRITER=MWAW_T_RESERVED15,
    /** Scoop: v1
        \note same enum as \a MWAW_T_RESERVED16
     */
    MWAW_T_SCOOP=MWAW_T_RESERVED16,

    /* FORMAT TO ADD:
       PublishIt, tools to create SlideShow, complexity probably medium-hard
    */
  };

  /** Analyzes the content of an input stream to see if it can be parsed
      \param input The input stream
      \param type The document type ( filled if the file is supported )
      \param kind The document kind ( filled if the file is supported )
      \return A confidence value which represents the likelyhood that the content from
      the input stream can be parsed

      \note encryption enum appears with MWAW_TEXT_VERSION==2 */
  static MWAWLIB Confidence isFileFormatSupported(librevenge::RVNGInputStream *input, Type &type, Kind &kind);

  // ------------------------------------------------------------
  // the different main parsers
  // ------------------------------------------------------------

  /** Parses the input stream content. It will make callbacks to the functions provided by a
     librevenge::RVNGTextInterface class implementation when needed. This is often commonly called the
     'main parsing routine'.
     \param input The input stream
     \param documentInterface A RVNGTextInterface implementation
     \param password The file password

   \note password appears with MWAW_TEXT_VERSION==2 */
  static MWAWLIB Result parse(librevenge::RVNGInputStream *input, librevenge::RVNGTextInterface *documentInterface, char const *password=nullptr);

  /** Parses the input stream content. It will make callbacks to the functions provided by a
     librevenge::RVNGDrawingInterface class implementation when needed. This is often commonly called the
     'main parsing routine'.
     \param input The input stream
     \param documentInterface A RVNGDrawingInterface implementation
     \param password The file password

     \note this function appears with MWAW_GRAPHIC_VERSION==2 in libmwaw-0.3
  */
  static MWAWLIB Result parse(librevenge::RVNGInputStream *input, librevenge::RVNGDrawingInterface *documentInterface, char const *password=nullptr);

  /** Parses the input stream content. It will make callbacks to the functions provided by a
     librevenge::RVNGPresentationInterface class implementation when needed. This is often commonly called the
     'main parsing routine'.
     \param input The input stream
     \param documentInterface A RVNGPresentationInterface implementation
     \param password The file password

     \note this function appeared in libmwaw-0.3.0 ( and returns MWAW_R_UNKNOWN_ERROR ). It was implemented in libmwaw-0.3.3 ( MWAW_PRESENTATION_VERSION==1 )
  */
  static MWAWLIB Result parse(librevenge::RVNGInputStream *input, librevenge::RVNGPresentationInterface *documentInterface, char const *password=nullptr);

  /** Parses the input stream content. It will make callbacks to the functions provided by a
     librevenge::RVNGSpreadsheetInterface class implementation when needed. This is often commonly called the
     'main parsing routine'.
     \param input The input stream
     \param documentInterface A RVNGSpreadsheetInterface implementation
     \param password The file password

     \note this function appears with MWAW_SPREADSHEET_VERSION==1 in libmwaw-0.3
  */
  static MWAWLIB Result parse(librevenge::RVNGInputStream *input, librevenge::RVNGSpreadsheetInterface *documentInterface, char const *password=nullptr);

  // ------------------------------------------------------------
  // decoders of the embedded zones created by libmwaw
  // ------------------------------------------------------------

  /** Parses the graphic contained in the binary data and called documentInterface to reconstruct
    a graphic. The input is normally send to a librevenge::RVNGXXXInterface with mimeType="image/mwaw-odg",
    ie. it must correspond to a picture created by the MWAWGraphicEncoder class via
    a MWAWPropertyEncoder.

   \param binary a list of librevenge::RVNGDrawingInterface stored in a documentInterface,
   \param documentInterface the RVNGDrawingInterface which will convert the graphic is some specific format.

   \note this function appears with MWAW_GRAPHIC_VERSION==1 in libmwaw-0.2 */
  static MWAWLIB bool decodeGraphic(librevenge::RVNGBinaryData const &binary, librevenge::RVNGDrawingInterface *documentInterface);

  /** Parses the spreadsheet contained in the binary data and called documentInterface to reconstruct
    a spreadsheet. The input is normally send to a librevenge::RVNGXXXInterface with mimeType="image/mwaw-ods",
    ie. it must correspond to a spreadsheet created by the MWAWSpreadsheetInterface class via
    a MWAWPropertyEncoder.

   \param binary a list of librevenge::RVNGSpreadsheetInterface stored in a documentInterface,
   \param documentInterface the RVNGSpreadsheetInterface which will convert the spreadsheet is some specific format.

   \note this function appears in libmwaw-0.3.0 (and returns always false).
   It was only implemented with MWAW_SPREADSHEET_VERSION==2 in libmwaw-0.3.1 */
  static MWAWLIB bool decodeSpreadsheet(librevenge::RVNGBinaryData const &binary, librevenge::RVNGSpreadsheetInterface *documentInterface);

  /** Parses the text contained in the binary data and called documentInterface to reconstruct
    a text. The input is normally send to a librevenge::RVNGXXXInterface with mimeType="image/mwaw-odt",
    ie. it must correspond to a text created by the MWAWTextInterface class via
    a MWAWPropertyEncoder.

   \param binary a list of librevenge::RVNGTextInterface stored in a documentInterface,
   \param documentInterface the RVNGTextInterface which will convert the text is some specific format.

   \note Reserved for future use. Actually, it only returns false. */
  static MWAWLIB bool decodeText(librevenge::RVNGBinaryData const &binary, librevenge::RVNGTextInterface *documentInterface);
};

#endif /* MWAWDOCUMENT_HXX */
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
