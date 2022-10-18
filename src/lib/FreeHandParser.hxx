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

#ifndef FREEHAND_PARSER
#  define FREEHAND_PARSER

#include <string>
#include <vector>

#include <librevenge/librevenge.h>

#include "MWAWDebug.hxx"
#include "MWAWInputStream.hxx"

#include "MWAWParser.hxx"

namespace FreeHandParserInternal
{
struct Shape;
struct ShapeHeader;
struct StyleHeader;
struct ScreenMode;
struct Textbox;
struct State;

class SubDocument;
}

/** \brief the main class to read a FreeHand v0,v1 file
 *
 */
class FreeHandParser final : public MWAWGraphicParser
{
  friend class FreeHandParserInternal::SubDocument;
public:
  //! constructor
  FreeHandParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header);
  //! destructor
  ~FreeHandParser() final;

  //! checks if the document header is correct (or not)
  bool checkHeader(MWAWHeader *header, bool strict=false) final;

  // the main parse function
  void parse(librevenge::RVNGDrawingInterface *documentInterface) final;

protected:
  //! inits all internal variables
  void init();

  //! creates the listener which will be associated to the document
  void createDocument(librevenge::RVNGDrawingInterface *documentInterface);

protected:
  //! finds the different objects zones
  bool createZones();
  //! try to read a zone: version 1
  bool readZoneV1(int zId);
  //! try to read a zone: version 2
  bool readZoneV2(int zId);

  //! try to read a special scren mode
  bool readScreenMode(FreeHandParserInternal::ScreenMode &screen);
  //! try to read a style header
  bool readStyleHeader(FreeHandParserInternal::StyleHeader &style);
  //! try to read a color zone
  bool readColor(int zId);
  //! try to read the dash zone
  bool readDash(int zId);
  //! try to read a fill zone
  bool readFillStyle(int zId);
  //! try to read a line style zone
  bool readLineStyle(int zId);
  //! try to read a postscript zone
  bool readPostscriptStyle(int zId);

  //! try to read the list of group
  bool readRootGroup(int zId);
  //! try to read a style group zone
  bool readStyleGroup(int zId);
  //! try to read a group zone: version 1
  bool readGroupV1(int zId);
  //! try to read a group zone: version 2
  bool readGroupV2(int zId);
  //! try to read a join zone (used to put text around path)
  bool readJoinGroup(int zId);
  //! try to read a node which contain the group transformation
  bool readTransformGroup(int zId);
  //! try to read a label/font name zone
  bool readStringZone(int zId);

  //! try to read a shape header
  bool readShapeHeader(FreeHandParserInternal::ShapeHeader &shape);
  //! try to read a data zone
  bool readDataZone(int zId);

  //! try to read a background picture zone
  bool readBackgroundPicture(int zId);
  //! try to read a picture node
  bool readPictureZone(int zId);
  //! try to read a shape
  bool readShape(int zId);
  //! try to read a textbox zone: version 1
  bool readTextboxV1(int zId);
  //! try to read a textbox zone: version 2
  bool readTextboxV2(int zId);

  //! try to send a zone
  bool sendZone(int zId, MWAWTransformation const &transform);
  //! try to send a group shape
  bool sendGroup(FreeHandParserInternal::Shape const &group, MWAWTransformation const &transform);
  //! try to send a background picture
  bool sendBackgroundPicture(FreeHandParserInternal::Shape const &picture, MWAWTransformation const &transform);
  //! try to send a picture
  bool sendPicture(FreeHandParserInternal::Shape const &picture, MWAWTransformation const &transform);
  //! try to send a basic shape
  bool sendShape(FreeHandParserInternal::Shape const &shape, MWAWTransformation const &transform);
  //! try to send a basic textbox
  bool sendTextbox(FreeHandParserInternal::Textbox const &textbox, MWAWTransformation const &transform);
  //! try to send the text of a text box
  bool sendText(int zId);
  //! try to open a layer
  bool openLayer(int zId);
  //! try to close a layer
  void closeLayer();
  //! sends the data which have not yet been sent to the listener
  void flushExtra();

  /** try to decompose the matrix in a rotation + scaling/translation matrix.

      Note: because of the y-symetry this function is different from MWAWTransformation::decompose */
  static bool decomposeMatrix(MWAWTransformation const &matrix, float &rotation, MWAWTransformation &transform, MWAWVec2f const &center);

  //! the state
  std::shared_ptr<FreeHandParserInternal::State> m_state;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
