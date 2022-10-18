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

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWGraphicListener.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWHeader.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPictBitmap.hxx"
#include "MWAWPictData.hxx"
#include "MWAWPrinter.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "ScoopParser.hxx"

/** Internal: the structures of a ScoopParser */
namespace ScoopParserInternal
{
////////////////////////////////////////
//! Internal: the shape of a ScoopParser
struct Shape {
  //! constructor
  Shape()
    : m_type(-1)
    , m_style()
    , m_mode(0)

    , m_page(0)
    , m_rotation(0)
    , m_verticalMode(2)

    , m_vertices()

    , m_textId(0)
    , m_textLinkId(0)

    , m_children()

  {
    for (auto &l : m_local) l=0;
    for (auto &id : m_ids) id=0;
    for (auto &f : m_flips) f=false;
  }

  //! the shape type
  int m_type;
  //! the graphic style
  MWAWGraphicStyle m_style;
  //! the mode: 0 copy, 1: or, ...
  int m_mode;

  //! the page
  int m_page;
  //! the bounding boxes: final and original
  MWAWBox2f m_boxes[2];
  //! the rotation
  int m_rotation;
  //! the vertical position: 0: center, 1: bottom, 2: top, 3: justify
  int m_verticalMode;
  //! the flip flags
  bool m_flips[2];
  //! the local values: circle: angles, rect: round dimension
  int m_local[2];

  //! the vertices: poly, spline, ...
  std::vector<MWAWVec2i> m_vertices;

  //! the text main id
  long m_textId;
  //! the text link id
  int m_textLinkId;
  //! the list of ids
  long m_ids[4];
  //! the list of entries
  MWAWEntry m_entries[4];

  //! the list of children: group
  std::vector<Shape> m_children;
};

////////////////////////////////////////
//! Internal: a special field of a ScoopParser
struct Special {
  //! constructor
  Special()
    : m_type(0)
    , m_value(0)
  {
  }
  //! the special type: 1: numeric, 2: roman, 3: Alpha, 4: time, 5: date numeric, 6:  date alpha
  int m_type;
  //! the special value
  int m_value;
};

////////////////////////////////////////
//! Internal: a paragraph of a ScoopParser
struct Paragraph {
  //! constructor
  Paragraph()
    : m_numChar(0)
    , m_text()
    , m_cPosToFontMap()
    , m_cPosToKernelMap()
    , m_cPosToSpecialMap()
    , m_paragraph()
  {
  }
  //! the number of characters
  int m_numChar;
  //! the text entry
  MWAWEntry m_text;
  //! a map character position to font
  std::map<int, MWAWFont> m_cPosToFontMap;
  //! a map character position to kernel modifier
  std::map<int, float> m_cPosToKernelMap;
  //! a map character positions to special field
  std::map<std::pair<int,int>, Special> m_cPosToSpecialMap;
  //! the paragraph style
  MWAWParagraph m_paragraph;
};

////////////////////////////////////////
//! Internal: a text zone shape of a ScoopParser
struct TextZoneShape {
  //! constructor
  TextZoneShape()
    : m_page(0)
    , m_box()
    , m_verticalMode(2)
  {
    for (auto &l : m_limits) l=0;
    for (auto &s : m_slants) s=0;
    for (auto &f : m_flips) f=false;
  }
  //! the page
  int m_page;
  //! the bounding box
  MWAWBox2f m_box;
  //! the paragraph id list corresponding to this shape
  int m_limits[2];
  //! the slant values: original, decal?
  float m_slants[2];
  //! the vertical position: 0: center, 1: bottom, 2: top, 3: justify
  int m_verticalMode;
  //! the flip flags
  bool m_flips[2];
};

////////////////////////////////////////
//! Internal: a text zone of a ScoopParser
struct TextZone {
  //! constructor
  TextZone()
    : m_id(0)
    , m_storyEntry()
    , m_font()
    , m_paragraphs()
    , m_shapes()
  {
  }

  //! the text zone id
  long m_id;
  //! the story name entry
  MWAWEntry m_storyEntry;
  //! the default font (or maybe the story font)
  MWAWFont m_font;
  //! the paragraph list
  std::vector<Paragraph> m_paragraphs;
  //! the list of shapes displaying this text
  std::vector<TextZoneShape> m_shapes;
};

////////////////////////////////////////
//! Internal: the state of a ScoopParser
struct State {
  //! constructor
  State()
    : m_numPages(1)
    , m_displayMode(1)
    , m_leftPage(0)
    , m_rightPage(-20)
    , m_thumbnailSize(1,1)
    , m_layoutDimension()
    , m_hasScrapPage(false)
    , m_patterns()
    , m_shapes()
    , m_idToParagraphMap()
    , m_idToTextZoneMap()
  {
    initPatterns();
  }

  //! init the patterns
  void initPatterns();

  //! the number of pages
  int m_numPages;
  //! the display mode 0: thumbnail, 1: one page, 2: facings pages, 3: one page+scrap
  int m_displayMode;
  //! the left-top page
  int m_leftPage;
  //! the right-bottom page
  int m_rightPage;
  //! the number of page using in thumbnail display
  MWAWVec2i m_thumbnailSize;
  //! the layout dimension
  MWAWVec2i m_layoutDimension;
  //! a flag to know if some shape are on the scrap page
  bool m_hasScrapPage;
  //! the list of patterns
  std::vector<MWAWGraphicStyle::Pattern> m_patterns;
  //! the main list of shapes
  std::vector<Shape> m_shapes;
  //! the style map: id to paragraph
  std::map<long,MWAWParagraph> m_idToParagraphMap;
  //! the text zone map: id to text zone
  std::map<long,TextZone> m_idToTextZoneMap;
};

void State::initPatterns()
{
  if (!m_patterns.empty()) return;
  uint16_t const values[]= {
    0xffff, 0xffff, 0xffff, 0xffff,  0xddff, 0x77ff, 0xddff, 0x77ff,  0xdd77, 0xdd77, 0xdd77, 0xdd77,  0xaa55, 0xaa55, 0xaa55, 0xaa55,
    0x55ff, 0x55ff, 0x55ff, 0x55ff,  0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,  0xeedd, 0xbb77, 0xeedd, 0xbb77,  0x8888, 0x8888, 0x8888, 0x8888,
    0xb130, 0x031b, 0xd8c0, 0x0c8d,  0x8010, 0x0220, 0x0108, 0x4004,  0xff88, 0x8888, 0xff88, 0x8888,  0xff80, 0x8080, 0xff08, 0x0808,
    0x0000, 0x0002, 0x0000, 0x0002,  0x8040, 0x2000, 0x0204, 0x0800,  0x8244, 0x3944, 0x8201, 0x0101,  0xf874, 0x2247, 0x8f17, 0x2271,
    0x55a0, 0x4040, 0x550a, 0x0404,  0x2050, 0x8888, 0x8888, 0x0502,  0xbf00, 0xbfbf, 0xb0b0, 0xb0b0,  0x0000, 0x0000, 0x0000, 0x0000,
    0x8000, 0x0800, 0x8000, 0x0800,  0x8800, 0x2200, 0x8800, 0x2200,  0x8822, 0x8822, 0x8822, 0x8822,  0xaa00, 0xaa00, 0xaa00, 0xaa00,
    0x00ff, 0x00ff, 0x00ff, 0x00ff,  0x1122, 0x4488, 0x1122, 0x4488,  0x8040, 0x2000, 0x0204, 0x0800,  0x0102, 0x0408, 0x1020, 0x4080,
    0xaa00, 0x8000, 0x8800, 0x8000,  0xff80, 0x8080, 0x8080, 0x8080,  0x0814, 0x2241, 0x8001, 0x0204,  0x8814, 0x2241, 0x8800, 0xaa00,
    0x40a0, 0x0000, 0x040a, 0x0000,  0x0384, 0x4830, 0x0c02, 0x0101,  0x8080, 0x413e, 0x0808, 0x14e3,  0x1020, 0x54aa, 0xff02, 0x0408,
    0x7789, 0x8f8f, 0x7798, 0xf8f8,  0x0008, 0x142a, 0x552a, 0x1408 //,  0x0000, 0x0000, 0x0000, 0x0000,
  };
  size_t N=MWAW_N_ELEMENTS(values)/4;
  m_patterns.resize(N);
  uint16_t const *ptr=values;
  for (size_t i=0; i<N; ++i) {
    auto &pat=m_patterns[i];
    pat.m_dim=MWAWVec2i(8,8);
    pat.m_data.resize(8);
    for (size_t j=0; j<8; j+=2) {
      pat.m_data[j]=uint8_t(~(*ptr>>8));
      pat.m_data[j+1]=uint8_t(~(*(ptr++)&0xff));
    }
  }

}

////////////////////////////////////////
//! Internal: the subdocument of a ScoopParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(ScoopParser &pars, MWAWInputStreamPtr const &input, long zoneId, int subZoneId)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_id(zoneId)
    , m_subId(subZoneId)
  {
  }

  //! destructor
  ~SubDocument() final {}

  //! operator!=
  bool operator!=(MWAWSubDocument const &doc) const final
  {
    if (MWAWSubDocument::operator!=(doc)) return true;
    auto const *sDoc = dynamic_cast<SubDocument const *>(&doc);
    if (!sDoc) return true;
    if (m_id != sDoc->m_id) return true;
    if (m_subId != sDoc->m_subId) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the textzone id
  long m_id;
  //! the sub zone id
  int m_subId;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("ScoopParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  if (!m_parser) {
    MWAW_DEBUG_MSG(("ScoopParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  static_cast<ScoopParser *>(m_parser)->sendText(m_id, m_subId);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}


}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
ScoopParser::ScoopParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state(new ScoopParserInternal::State)
{
  setAsciiName("main-1");

  getPageSpan().setMargins(0.1);
}

ScoopParser::~ScoopParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void ScoopParser::parse(librevenge::RVNGDrawingInterface *docInterface)
{
  if (!getInput().get() || !checkHeader(nullptr))  throw(libmwaw::ParseException());
  bool ok = false;
  try {
    // create the asciiFile
    ascii().setStream(getInput());
    ascii().open(asciiName());
    checkHeader(nullptr);
    ok = createZones();
    if (ok) {
      createDocument(docInterface);

      auto listener=getGraphicListener();
      int numPages=std::max<int>(1, m_state->m_numPages);
      for (int p=0; p<numPages+(m_state->m_hasScrapPage ? 1 : 0);) {
        if (p && listener)
          listener->insertBreak(MWAWListener::PageBreak);
        MWAWVec2i decal(0,0);
        int pId=p>=numPages ? -3 : p;
        switch (m_state->m_displayMode) {
        case 2:
        case 3:
          if (pId==m_state->m_rightPage)
            decal[0]=-m_state->m_layoutDimension[0];
          break;
        default:
          break;
        }
        for (auto const &shape : m_state->m_shapes) {
          if (shape.m_page==pId)
            send(shape, decal);
        }
        if (p==0 && m_state->m_displayMode==0) {
          p=m_state->m_thumbnailSize[0]*m_state->m_thumbnailSize[1];
          if (p<=0) {
            MWAW_DEBUG_MSG(("ScoopParser::parse: oops, can not use the thumbnail size\n"));
            p=1;
          }
        }
        else
          ++p;
      }
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("ScoopParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void ScoopParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("ScoopParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  std::vector<MWAWPageSpan> pageList;
  int numPages=std::max<int>(1,m_state->m_numPages);
  if (m_state->m_displayMode==0 && m_state->m_thumbnailSize!=MWAWVec2i(1,1)) {
    if (m_state->m_thumbnailSize[0]<1 || m_state->m_thumbnailSize[1]<1) {
      MWAW_DEBUG_MSG(("ScoopParser::createDocument: can not use the thumbnail size, assume 1x1\n"));
      m_state->m_thumbnailSize=MWAWVec2i(1,1);
    }
    else {
      MWAWPageSpan ps(getPageSpan());
      ps.setFormWidth(ps.getFormWidth()*m_state->m_thumbnailSize[0]);
      ps.setFormLength(ps.getFormLength()*m_state->m_thumbnailSize[1]);
      ps.setPageSpan(1);
      pageList.push_back(ps);
      numPages-=m_state->m_thumbnailSize[0]*m_state->m_thumbnailSize[1];
      numPages=std::max<int>(0,m_state->m_numPages);
    }
  }
  if (numPages || m_state->m_hasScrapPage) {
    MWAWPageSpan ps(getPageSpan());
    ps.setPageSpan(numPages+(m_state->m_hasScrapPage ? 1 : 0));
    pageList.push_back(ps);
  }
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->startDocument();
}


////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool ScoopParser::createZones()
{
  /* the file contains also a resource fork which contains a resource
     WWWW:19018, the windows' position, so it seems safe to ignore it   */
  MWAWInputStreamPtr input = getInput();
  if (!input || !readHeader())
    return false;
  ScoopParserInternal::TextZone tZone;
  while (readTextZone(tZone))
    ;
  long pos=input->tell();
  if (!input->checkPosition(pos+4) || input->readLong(4)) {
    MWAW_DEBUG_MSG(("ScoopParser::createZones: can not find the shape id=0\n"));
    ascii().addPos(pos);
    ascii().addNote("Entries(Shape):###id");
    return false;
  }
  ascii().addPos(pos);
  ascii().addNote("_");
  if (!readShapesList(m_state->m_shapes))
    return false;
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("ScoopParser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Extra):###");
    return !m_state->m_shapes.empty();
  }
  return true;
}

////////////////////////////////////////////////////////////
// send shapes
////////////////////////////////////////////////////////////

bool ScoopParser::send(ScoopParserInternal::Shape const &shape, MWAWVec2i const &decal)
{
  auto input=getInput();
  auto listener=getGraphicListener();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("ScoopParser::send: can not find the listener\n"));
    return false;
  }
  MWAWBox2f box=MWAWBox2f(shape.m_boxes[0][0]+MWAWVec2f(decal), shape.m_boxes[0][1]+MWAWVec2f(decal));
  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.setRelativePosition(MWAWPosition::Page);
  MWAWGraphicShape gShape;

  switch (shape.m_type) {
  case 0: // group
    listener->openGroup(pos);
    for (auto const &child : shape.m_children)
      send(child, decal);
    listener->closeGroup();
    return true;
  case 3: { // line
    gShape=MWAWGraphicShape::line(box[0], box[1]);
    auto shapeBox=gShape.getBdBox();
    pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
    break;
  }
  case 5: // rect or rect oval
    gShape=MWAWGraphicShape::rectangle(box,
                                       MWAWVec2f(0.5f*float(shape.m_local[0]), 0.5f*float(shape.m_local[1])));
    break;
  case 7: { // circle or arc
    if (shape.m_local[0]<=0 || shape.m_local[1]<=0) {
      gShape=MWAWGraphicShape::circle(box);
      break;
    }
    int angle[2] = { 90-shape.m_local[0]-shape.m_local[1], 90-shape.m_local[1] };
    if (angle[1]>360) {
      int numLoop=int(angle[1]/360)-1;
      angle[0]-=numLoop*360;
      angle[1]-=numLoop*360;
      while (angle[1] > 360) {
        angle[0]-=360;
        angle[1]-=360;
      }
    }
    if (angle[0] < -360) {
      int numLoop=int(angle[0]/360)+1;
      angle[0]-=numLoop*360;
      angle[1]-=numLoop*360;
      while (angle[0] < -360) {
        angle[0]+=360;
        angle[1]+=360;
      }
    }
    MWAWVec2f center = box.center();
    MWAWVec2f axis = 0.5f*MWAWVec2f(box.size());
    // we must compute the real bd box
    float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
    int limitAngle[2];
    for (int i = 0; i < 2; i++)
      limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
    for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; bord++) {
      float ang = (bord == limitAngle[0]) ? float(angle[0]) :
                  (bord == limitAngle[1]+1) ? float(angle[1]) : float(90 * bord);
      ang *= float(M_PI/180.);
      float actVal[2] = { axis[0] *std::cos(ang), -axis[1] *std::sin(ang)};
      if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
      else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
      if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
      else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
    }
    MWAWBox2f realBox(MWAWVec2f(center[0]+minVal[0],center[1]+minVal[1]),
                      MWAWVec2f(center[0]+maxVal[0],center[1]+maxVal[1]));
    if (shape.m_style.hasSurface())
      gShape = MWAWGraphicShape::pie(realBox, box, MWAWVec2f(float(angle[0]),float(angle[1])));
    else
      gShape = MWAWGraphicShape::arc(realBox, box, MWAWVec2f(float(angle[0]),float(angle[1])));
    break;
  }
  case 8: { // bitmap
    MWAWEmbeddedObject object;
    if (!readBitmap(shape.m_entries[0], object) || object.isEmpty()) {
      MWAW_DEBUG_MSG(("ScoopParser::send[bitmap]: the bitmap entries does not seem valid\n"));
      return false;
    }
    auto fStyle=shape.m_style;
    if (shape.m_rotation)
      fStyle.m_rotate=float(shape.m_rotation);
    for (int i=0; i<2; ++i)
      fStyle.m_flip[i]=shape.m_flips[i];
    listener->insertPicture(pos, object, fStyle);
    return true;
  }
  case 9: { // polygon
    if (shape.m_vertices.size()<4) {
      MWAW_DEBUG_MSG(("ScoopParser::send[poly]: the number of points seems too short\n"));
      return false;
    }
    if (shape.m_style.hasSurface())
      gShape=MWAWGraphicShape::polygon(box);
    else
      gShape=MWAWGraphicShape::polyline(box);
    float scaling[]= {1,1};
    for (int coord=0; coord<2; ++coord) {
      float dirC=float(shape.m_vertices[1][coord]-shape.m_vertices[0][coord]);
      if ((dirC<=0 && dirC>=0))
        continue;
      scaling[coord]=box.size()[coord]/dirC;
    }
    gShape.m_vertices.resize(shape.m_vertices.size()-2);
    for (size_t p=2; p<shape.m_vertices.size(); ++p)
      gShape.m_vertices[p-2]=box[0]+MWAWVec2f(scaling[0]*float(shape.m_vertices[p][0]), scaling[1]*float(shape.m_vertices[p][1]));
    if (shape.m_style.hasSurface() && gShape.m_vertices[0]!=gShape.m_vertices.back())
      gShape.m_vertices.push_back(gShape.m_vertices[0]);
    break;
  }
  case 10: { // picture
    MWAWEmbeddedObject object;
    for (int i=0; i<3; ++i) {
      auto const &entry=shape.m_entries[i];
      if (!entry.valid())
        continue;
      input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
      librevenge::RVNGBinaryData picture;
      if (!input->readDataBlock(entry.length(), picture)) {
        MWAW_DEBUG_MSG(("ScoopParser::send[picture]: can not read a picture\n"));
        continue;
      }
      object.add(picture);
#ifdef DEBUG_WITH_FILES
      static int volatile pictName = 0;
      libmwaw::DebugStream f2;
      f2 << "PICT-" << ++pictName << ".pct";
      libmwaw::Debug::dumpFile(picture, f2.str().c_str());
      ascii().skipZone(entry.begin(), entry.end()-1);
#endif
    }
    if (object.isEmpty())
      return false;
    auto fStyle=shape.m_style;
    if (shape.m_rotation)
      fStyle.m_rotate=float(shape.m_rotation);
    for (int i=0; i<2; ++i)
      fStyle.m_flip[i]=shape.m_flips[i];
    listener->insertPicture(pos, object, fStyle);
    return true;
  }
  case 11: // layout
    return true;
  case 12: // diamond
    gShape=MWAWGraphicShape::polygon(box);
    gShape.m_vertices= {MWAWVec2f(0.5f*(box[0][0]+box[1][0]),box[0][1]),
                        MWAWVec2f(box[0][0],0.5f*(box[0][1]+box[1][1])),
                        MWAWVec2f(0.5f*(box[0][0]+box[1][0]),box[1][1]),
                        MWAWVec2f(box[1][0],0.5f*(box[0][1]+box[1][1])),
                        MWAWVec2f(0.5f*(box[0][0]+box[1][0]),box[0][1])
                       };
    break;
  case 13: { // cross-line
    listener->openGroup(pos);
    MWAWVec2f const center=box.center();
    MWAWVec2f const dir=0.5f*box.size();
    for (int i=0; i<12; ++i) {
      MWAWVec2f newDir(float(std::cos(i*M_PI/12))*dir[0], float(std::sin(i*M_PI/12))*dir[1]);
      gShape=MWAWGraphicShape::line(center-newDir, center+newDir);
      auto shapeBox=gShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, gShape, shape.m_style);
    }
    listener->closeGroup();
    return true;
  }
  case 14: // multi line
    listener->openGroup(pos);
    for (int st=0; st<2; ++st) {
      gShape=MWAWGraphicShape::line(MWAWVec2f(box[0][0],box[st][1]),
                                    MWAWVec2f(box[1][0],box[st][1]));
      auto shapeBox=gShape.getBdBox();
      pos=MWAWPosition(shapeBox[0], shapeBox.size(), librevenge::RVNG_POINT);
      listener->insertShape(pos, gShape, shape.m_style);
    }
    listener->closeGroup();
    return true;
  case 15: { // spline
    if (shape.m_vertices.size()<4) {
      MWAW_DEBUG_MSG(("ScoopParser::send[spline]: the number of points seems too short\n"));
      return false;
    }
    if (shape.m_vertices.size()%3) {
      MWAW_DEBUG_MSG(("ScoopParser::send[spline]: the number of points seems odd\n"));
    }
    float scaling[]= {1,1};
    for (int coord=0; coord<2; ++coord) {
      float dirC=float(shape.m_vertices[1][coord]-shape.m_vertices[0][coord]);
      if ((dirC<=0 && dirC>=0))
        continue;
      scaling[coord]=box.size()[coord]/dirC;
    }
    std::vector<MWAWVec2f> points;
    points.resize(shape.m_vertices.size()-2);
    for (size_t p=2; p<shape.m_vertices.size(); ++p)
      points[p-2]=box[0]+MWAWVec2f(scaling[0]*float(shape.m_vertices[p][0]), scaling[1]*float(shape.m_vertices[p][1]));

    gShape=MWAWGraphicShape::path(box);
    std::vector<MWAWGraphicShape::PathData> &path=gShape.m_path;
    path.push_back(MWAWGraphicShape::PathData('M', points[0]));
    for (size_t i=1; i+2<points.size(); i+=3)
      path.push_back(MWAWGraphicShape::PathData('C', points[i+2], points[i], points[i+1]));
    if (shape.m_style.hasSurface())
      path.push_back(MWAWGraphicShape::PathData('Z'));
    break;
  }
  case 17: { // text
    std::shared_ptr<MWAWSubDocument> doc(new ScoopParserInternal::SubDocument(*this, input, shape.m_textId, shape.m_textLinkId));
    auto textStyle=shape.m_style;
    switch (shape.m_verticalMode) {
    case 0:
      textStyle.m_verticalAlignment=MWAWGraphicStyle::V_AlignCenter;
      break;
    case 1:
      textStyle.m_verticalAlignment=MWAWGraphicStyle::V_AlignBottom;
      break;
    case 2: // top
      break;
    case 3:
      textStyle.m_verticalAlignment=MWAWGraphicStyle::V_AlignJustify;
      break;
    default:
      MWAW_DEBUG_MSG(("ScoopParse::send: unknown alignment %x\n", shape.m_verticalMode));
      break;
    }
    if (shape.m_rotation)
      textStyle.m_rotate=float(shape.m_rotation);
    for (int i=0; i<2; ++i)
      textStyle.m_flip[i]=shape.m_flips[i];
    listener->insertTextBox(pos, doc, textStyle);
    return true;
  }
  default:
    gShape=MWAWGraphicShape::rectangle(box);
    break;
  }
  listener->insertShape(pos, gShape, shape.m_style);
  return true;
}

bool ScoopParser::sendText(long tZoneId, int subZone)
{
  auto listener=getGraphicListener();
  if (!listener) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: can not find the listener\n"));
    return false;
  }
  if (tZoneId==0) // ok, no text zone associated
    return true;
  auto const &it=m_state->m_idToTextZoneMap.find(tZoneId);
  if (it==m_state->m_idToTextZoneMap.end()) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: can not find zone with id=%lx\n", (unsigned long) tZoneId));
    return false;
  }
  auto const &zone=it->second;
  if (subZone<0 || size_t(subZone)>=zone.m_shapes.size()) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: can not find the shape %d for zone with id=%lx\n", subZone, (unsigned long) tZoneId));
    return false;
  }
  auto const &shape=zone.m_shapes[size_t(subZone)];
  for (int p=shape.m_limits[0]; p<shape.m_limits[1]; ++p) {
    if (p<0 || size_t(p)>=zone.m_paragraphs.size()) {
      MWAW_DEBUG_MSG(("ScoopParser::sendText: find bad paragraph id for zone with id=%lx[%d]\n", (unsigned long) tZoneId, subZone));
      break;
    }
    sendText(zone.m_paragraphs[size_t(p)]);
  }
  return true;
}

bool ScoopParser::sendText(ScoopParserInternal::Paragraph const &paragraph)
{
  auto input=getInput();
  auto listener=getGraphicListener();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: can not find the listener\n"));
    return false;
  }
  listener->setParagraph(paragraph.m_paragraph);
  int numChar=paragraph.m_numChar;
  if (numChar==0) {
    listener->insertEOL();
    return true;
  }
  if (numChar>paragraph.m_text.length()) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: the number of characters seems too big\n"));
    numChar=int(paragraph.m_text.length());
  }
  if (!input->checkPosition(paragraph.m_text.end())) {
    MWAW_DEBUG_MSG(("ScoopParser::sendText: can not find the text zone\n"));
    return true;
  }
  input->seek(paragraph.m_text.begin(), librevenge::RVNG_SEEK_SET);
  bool lastIsKerning=false;
  for (int cPos=0; cPos<numChar; ++cPos) {
    auto const &fIt=paragraph.m_cPosToFontMap.find(cPos);
    if (fIt!=paragraph.m_cPosToFontMap.end()) {
      listener->setFont(fIt->second);
      lastIsKerning=false;
    }
    auto const &kIt=paragraph.m_cPosToKernelMap.find(cPos+1);
    if (kIt!=paragraph.m_cPosToKernelMap.end()) {
      lastIsKerning=true;
      auto font=listener->getFont();
      font.setDeltaLetterSpacing(kIt->second);
      listener->setFont(font);
    }
    else if (lastIsKerning) {
      lastIsKerning=false;
      auto font=listener->getFont();
      font.setDeltaLetterSpacing(0);
      listener->setFont(font);
    }
    unsigned char ch=(unsigned char)(input->readLong(1));
    switch (ch) {
    case 0x9:
      listener->insertTab();
      break;
    case 0x1f: // hyphen character
      break;
    default:
      if (ch<0x1f)
        MWAW_DEBUG_MSG(("ScoopParser::sendText: find odd char c=%d\n", int(ch)));
      else
        listener->insertCharacter(ch);
      break;
    }
  }
  listener->insertEOL();
  return true;
}

////////////////////////////////////////////////////////////
// read shapes
////////////////////////////////////////////////////////////
bool ScoopParser::readTextZone(ScoopParserInternal::TextZone &tZone)
{
  tZone=ScoopParserInternal::TextZone();
  auto input=getInput();
  long pos=input->tell();
  if (!input->checkPosition(pos+90))
    return false;
  libmwaw::DebugStream f;
  static int id=0;
  f << "Entries(TextZone)[" << id++ << "]:list,";
  int val;
  tZone.m_id=long(input->readULong(4));
  if (!tZone.m_id || input->readULong(4)!=0x52) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "ID=" << std::hex << tZone.m_id << std::dec << ",";
  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+28, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  bool hasStoryName=false;
  val=int(input->readULong(4));
  if (val) {
    hasStoryName=true;
    f << "ID[name]=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readULong(2)); // 40|100 : a dim?
  f << "f0=" << val << ",";
  val=int(input->readULong(2)); // 0
  if (val)
    f << "f1=" << val << ",";
  f << "h=" << 2*72*float(input->readLong(4))/65536 << ",";

  ascii().addDelimiter(input->tell(),'|');
  input->seek(pos+46, librevenge::RVNG_SEEK_SET);
  ascii().addDelimiter(input->tell(),'|');
  readFont(tZone.m_font);
  f << "font=[" << tZone.m_font.getDebugString(getFontConverter()) << "],";
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int numShapes=int(input->readULong(2));
  f << "num[shape]=" << numShapes << ",";
  val=int(input->readULong(4));
  if (val)
    f << "shape[ID]=" << std::hex << val << std::dec << ",";
  int numPara=int(input->readULong(2));
  f << "num[para]=" << numPara << ",";
  val=int(input->readULong(4));
  if (val)
    f << "para[ID]=" << std::hex << val << std::dec << ",";
  for (int i=0; i<6; ++i) { // 0
    val=int(input->readLong(2));
    if (val)
      f << "g" << i << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(pos+90, librevenge::RVNG_SEEK_SET);
  if (hasStoryName && !readText(tZone.m_storyEntry, "name"))
    return false;

  // first look for name style
  pos=input->tell();
  while (!input->isEnd()) {
    pos=input->tell();
    val=int(input->readULong(4));
    if (!val) {
      ascii().addPos(pos);
      ascii().addNote("_");
      break;
    }
    input->seek(-4, librevenge::RVNG_SEEK_CUR);
    MWAWParagraph para;
    if (!readParagraph(para, true))
      return false;
  }

  for (int i=0; i<numPara; ++i) {
    ScoopParserInternal::Paragraph para;
    if (!readTextZoneParagraph(para, i))
      return false;
    tZone.m_paragraphs.push_back(para);
  }

  pos=input->tell();
  if (!input->checkPosition(pos+numShapes*56)) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    MWAW_DEBUG_MSG(("ScoopParser::readTextZone: can find some find the last sub zone\n"));
    return false;
  }
  for (int n=0; n<numShapes; ++n) {
    pos=input->tell();
    f.str("");
    f << "TextZone-shape" << n << ":";
    ScoopParserInternal::TextZoneShape shape;
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(4))/65536;
    shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]),MWAWVec2f(dim[0]+dim[2],dim[1]+dim[3]));
    f << "box=" << shape.m_box << ",";
    for (int i=0; i<3; ++i) { // f1=0|1, f2=0|1
      val=int(input->readLong(2));
      if (!val) continue;
      if (i==0) {
        shape.m_page=val;
        f << "page=" << val << ",";
        if (val!=-3 && (val<0 || val>=m_state->m_numPages)) {
          f << "###";
          MWAW_DEBUG_MSG(("ScoopParser::readTextZone: find unexpected pages\n"));
        }
      }
      else
        f << "f" << i << "=" << val << ",";
    }
    for (auto &l: shape.m_limits) l=int(input->readULong(2));
    shape.m_limits[1]+=shape.m_limits[0];
    f << "pPos[para]=" << MWAWVec2i(shape.m_limits[0], shape.m_limits[1]) << ",";
    for (int i=0; i<4; ++i) { // f3: small number
      val=int(input->readULong(2));
      if (val) f << "f" << i+3 << "=" << val << ",";
    }
    int numAround=int(input->readLong(2));
    if (numAround) // data size 4*numAround if numAround>0 else 4
      f << "num[around]=" << numAround << ",";
    val=int(input->readULong(4));
    if (val)
      f << "ID[run,around]=" << std::hex << val << std::dec << ",";
    int iDim[2];
    for (auto &d : iDim) d=int(input->readLong(2));
    if (iDim[0]||iDim[1]) {
      shape.m_slants[0]=float(iDim[0])/256;
      shape.m_slants[1]=float(iDim[1])/256;
      MWAW_DEBUG_MSG(("ScoopParser::readShape: oops, retrieving slant is not implemented\n"));
      f << "slant=[" << shape.m_slants[0] << "," << shape.m_slants[1] << "],"; // orig, decal ?
    }
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(1));
      if (val==2-i) continue;
      if (i==0) {
        shape.m_verticalMode=val&3;
        f << "vertical[mode]=" << shape.m_verticalMode << ","; // 0: center, 2: top
        if (val&4) {
          shape.m_flips[0]=true;
          f << "flip[hori],";
        }
        if (val&8) {
          shape.m_flips[1]=true;
          f << "flip[vertical],";
        }
        val&=0xf0;
        if (val)
          f << "fl1=" << std::hex << val << ",";
      }
      else
        f << "g0=" << val << ",";
    }
    for (int i=0; i<5; ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "g" << i+1 << "=" << val << ",";
    }
    tZone.m_shapes.push_back(shape);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+56, librevenge::RVNG_SEEK_SET);

    if (numAround<0) {
      pos=input->tell();
      f.str("");
      f << "TextZone-wrap" << n << ":";
      if (!input->checkPosition(pos+4)) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        MWAW_DEBUG_MSG(("ScoopParser::readTextZone: can find some find the round around zone\n"));
        return false;
      }
      f << "f0=" << input->readLong(4) << ",";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }
    else if (numAround>0) {
      pos=input->tell();
      f.str("");
      f << "TextZone-wrap" << n << ":";
      long len=long(input->readULong(4));
      if (len<0 || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        MWAW_DEBUG_MSG(("ScoopParser::readTextZone: can find some find the round around zone\n"));
        return false;
      }
      if (len<4*numAround) {
        f << "###";
        MWAW_DEBUG_MSG(("ScoopParser::readTextZone: the round around size seems too short\n"));
        numAround=0;
      }
      f << "sz=[";
      for (int i=0; i<numAround; ++i) {
        int wrap[2];
        for (auto &w : wrap) w=int(input->readLong(2));
        f << MWAWVec2i(wrap[0],wrap[1]) << ",";
      }
      f << "],";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
    }
  }
  if (!tZone.m_shapes.empty() && tZone.m_shapes.back().m_limits[1]<numPara) // force the last shape to display the remaining text
    tZone.m_shapes.back().m_limits[1]=numPara;
  if (m_state->m_idToTextZoneMap.find(tZone.m_id)!=m_state->m_idToTextZoneMap.end()) {
    MWAW_DEBUG_MSG(("ScoopParser::readTextZone: find dupplicated text zone id=%lx\n", (unsigned long) tZone.m_id));
  }
  else
    m_state->m_idToTextZoneMap[tZone.m_id]=tZone;
  return true;
}

bool ScoopParser::readTextZoneParagraph(ScoopParserInternal::Paragraph &para, int id)
{
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;

  f.str("");
  f << "TextZone-para" << id << "[beg]:";
  if (!input->checkPosition(pos+56)) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: can find the second sub zone part\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  int val;
  float dim[2];
  for (auto &d : dim) d=float(input->readLong(4))/65536;
  f << "h=" << MWAWVec2f(dim[0],dim[0]+dim[1]) << ",";

  bool hasSpecial=false;
  val=int(input->readULong(4));
  if (val) {
    hasSpecial=true;
    f << "special[ID]=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<5; ++i) { // 0
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  int numCharStyle=int(input->readULong(2));
  f << "num[char,style]=" << numCharStyle << ",";
  f << "ID[cStyle]=" << std::hex << input->readULong(4) << std::dec << ",";
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i+5 << "=" << val << ",";
  }
  int numKerns=int(input->readLong(2));
  if (numKerns) f << "num[kern]=" << numKerns << ",";
  val=int(input->readULong(4));
  if (val)
    f << "ID[kern]=" << std::hex << val << std::dec << ",";
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    if (val)
      f << "f" << i+3 << "=" << val << ",";
  }
  para.m_numChar=int(input->readULong(2));
  if (para.m_numChar)
    f << "text[len]=" << para.m_numChar << ",";
  f << "IDs[text]=["; // text, style
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(4));
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+56, librevenge::RVNG_SEEK_SET);

  if (hasSpecial) {
    pos=input->tell();
    long len=long(input->readULong(4));
    long endPos=pos+4+len;
    if (len<0 || endPos<pos+4 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: can find the special zone\n"));
      return false;
    }
    static bool first=true;
    if (first) {
      // as each special field is replaced by some text in the text zone, we can ignore them
      first=false;
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: this file contains some special fields, the conversion will ignore them\n"));
    }
    f.str("");
    f << "TextZone-special:";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    int n=0;
    while (input->tell()<endPos) {
      pos=input->tell();
      f.str("");
      f << "TextZone-Sp" << n++ << ":";
      len=long(input->readULong(4));
      if (len<16 || pos+len<pos+4 || pos+len>endPos) {
        MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: can not find a special\n"));
        f << "###";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        break;
      }
      int cPos[2];
      for (auto &p : cPos) p=int(input->readULong(2));
      f << "cPos=" << MWAWVec2i(cPos[0],cPos[0]+cPos[1]) << ",";
      ScoopParserInternal::Special special;
      special.m_type=int(input->readLong(2));
      f << "type=" << special.m_type << ",";
      for (int i=0; i<3; ++i) { // f2: the value
        val=int(input->readLong(2));
        if (!val) continue;
        if (i==2) special.m_value=val;
        f << "f" << i << "=" << val << ",";
      }
      if (len!=16)
        ascii().addDelimiter(input->tell(),'|');
      para.m_cPosToSpecialMap[std::make_pair(cPos[0],cPos[0]+cPos[1])]=special;
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+len, librevenge::RVNG_SEEK_SET);
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  if (numCharStyle) {
    pos=input->tell();
    f.str("");
    f << "Entries(CStyle):";
    long len=long(input->readULong(4));
    long endPos=pos+4+len;
    if (len<0 || endPos<pos+4 || !input->checkPosition(endPos)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: can find the char style zone\n"));
      return false;
    }
    if (len<numCharStyle*18) {
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: the char style zone seems too short\n"));
      f << "###";
      numCharStyle=0;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    auto fontConverter=getFontConverter();
    int cPos=0;
    for (int i=0; i<numCharStyle; ++i) {
      pos=input->tell();
      f.str("");
      f << "CStyle" << i << ":";
      int cLen=int(input->readULong(2));
      f << "pos=" << cPos << ",";
      MWAWFont font;
      readFont(font);
      f << font.getDebugString(fontConverter) << ",";
      para.m_cPosToFontMap[cPos]=font;
      cPos+=cLen;
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+18, librevenge::RVNG_SEEK_SET);
    }
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
  }
  if (numKerns) {
    pos=input->tell();
    long len=long(input->readULong(4));
    if (len<0 || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: can find the kerns' zone\n"));
      return false;
    }
    f.str("");
    f << "TextZone-kern:";
    if (len<4*numKerns) {
      MWAW_DEBUG_MSG(("ScoopParser::readTextZoneParagraph: the number of kerns seems bad\n"));
      f << "###";
      numKerns=0;
    }
    f << "kerns=[";
    int cPos=0;
    for (int i=0; i<numKerns; ++i) {
      cPos+=int(input->readULong(2));
      float kernel=float(input->readLong(2))/256;
      para.m_cPosToKernelMap[cPos]=kernel;
      f << kernel << ":c=" << cPos << ",";
    }
    f << "],";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  }
  if ((para.m_numChar && !readText(para.m_text, "para")) || !readParagraph(para.m_paragraph))
    return false;
  return true;
}

bool ScoopParser::readFont(MWAWFont &font)
{
  font=MWAWFont();
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;

  if (!input->checkPosition(pos+16)) {
    MWAW_DEBUG_MSG(("ScoopParser::readFont[font]: the zone seems too short\n"));
    f << "###";
    font.m_extra=f.str();
    return false;
  }

  font.setId(int(input->readULong(2)));
  font.setSize(float(input->readULong(1)));
  input->seek(1, librevenge::RVNG_SEEK_CUR);
  int val=int(input->readULong(2));
  uint32_t flags=0;
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  if (val&0xffe0) f << "fl=#" << std::hex << (val&0xffe0) << std::dec << ",";
  font.setFlags(flags);

  val=int(input->readULong(2));
  if (val!=0x700)
    f << "unk=" << float(val)/0x100 << ","; // related to streching, maybe related to delta spacing
  val=int(input->readLong(2));
  if (val)
    font.set(MWAWFont::Script(-float(val)/256, librevenge::RVNG_POINT));
  val=int(input->readULong(2));
  if (val!=0x100)
    font.setWidthStreching(float(val)/256);
  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16, librevenge::RVNG_SEEK_SET);
  font.m_extra=f.str();
  return true;
}

bool ScoopParser::readParagraph(MWAWParagraph &para, bool define)
{
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;

  para=MWAWParagraph();
  f << "Entries(PStyle):";
  if (!input->checkPosition(pos+4)) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readParagraph: the zone seems too short\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  long id=long(input->readULong(4));
  if (define && !id) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readParagraph: can not find the text id\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  if (id) {
    f << "ID=" << std::hex << id << std::dec << ",";
    if (!define) {
      auto sIt=m_state->m_idToParagraphMap.find(id);
      if (sIt!=m_state->m_idToParagraphMap.end())
        para=sIt->second;
      else {
        MWAW_DEBUG_MSG(("ScoopParser::readParagraph: unknown style id=%lx\n", (unsigned long) id));
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return true;
    }
  }

  if (!input->checkPosition(pos+4+58) || input->readULong(4)!=0x36) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readParagraph: unexpected data length\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  bool hasName=false;
  int val=int(input->readULong(4));
  if (val) {
    hasName=true;
    f << "name[has,ID]=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<4; ++i) { // f0=0|1
    val=int(input->readLong(2));
    if (val==(i==0 ? 1 : 0))
      continue;
    if (i==1) {
      para.m_spacings[2]=float(val)/256/72;
      f << "after[line]=" << float(val)/256 << "pt,";
    }
    else if (i==2) {
      para.setInterline(double(val)/256, librevenge::RVNG_POINT);
      f << "line[fixed]=" << float(val)/256 << "pt,";
    }
    else if (i==3) {
      para.m_spacings[1]=float(val)/256/72; // checkme
      f << "bef[line]=" << float(val)/256 << "pt,";
    }
    else // does f0=0 means default, ie. ignore margins?
      f << "f0=" << val << ",";
  }
  val=int(input->readLong(2));
  if (val!=0x100) {
    para.setInterline(double(val)/256, librevenge::RVNG_PERCENT, MWAWParagraph::AtLeast);
    f << "interline=" << float(val)/256 << "%,";
  }
  for (int i=0; i<5; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {0xcd, 0x200, 0xcd, 0x180, 0x100};
    if (val==expected[i]) continue;
    char const *wh[]= {"min[word,spacing]", "max[word,spacing]", "min[letter,spacing]", "max[letter,spacing]", "raggedness"};
    f << wh[i] << "=" << float(val)/256 << "%,";
  }
  val=int(input->readLong(2));
  switch (val) {
  case 2: // left
    break;
  case 0:
    para.m_justify = MWAWParagraph::JustificationCenter;
    f << "center,";
    break;
  case 1:
    para.m_justify = MWAWParagraph::JustificationRight;
    f << "right,";
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    f << "justify,";
    break;
  default:
    MWAW_DEBUG_MSG(("ScoopParser::readParagraph: find unknown alignment\n"));
    f << "###align=" << val << ",";;
    break;
  }
  val=int(input->readLong(2));
  if (val)
    f << "f4=" << val << ",";
  para.m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(4));
    if (!val) continue;
    // check validity sometimes right margin is too big
    f << (i==0 ? "left" : i==1 ? "right" : "indent") << "[margin]=" << float(val)/65536 << ",";
    if (val<-200*65536 || val>200*65536) {
      f << "###";
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("ScoopParser::readParagraph: some margins seem bad\n"));
        first=false;
      }
    }
    else
      para.m_margins[i<2 ? 1+i : 0]=double(val)/65536*(i==1 ? -1 : 1);
  }
  *para.m_margins[0]-=*para.m_margins[1];
  val=int(input->readLong(2));
  if (val!=0x2400)
    f << "inter[tab]=" << float(val)/256 << ",";

  int numTabs=int(input->readULong(2));
  if (numTabs)
    f << "num[tabs]=" << numTabs << ",";
  val=int(input->readULong(4));
  if (val)
    f << "tab[ID]=" << std::hex << val << std::dec << ",";
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    if (val!=-1)
      f << "g" << i+1 << "=" << val << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+4+58, librevenge::RVNG_SEEK_SET);

  MWAWEntry nameEntry;
  if (hasName && !readText(nameEntry, "stylename"))
    return false;

  if (numTabs) {
    pos=input->tell();
    f.str("");
    f << "PStyle-tabs:";
    long len=long(input->readLong(4));
    if (len<0 || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      f << "###";
      MWAW_DEBUG_MSG(("ScoopParser::readParagraph: unexpected tabs length\n"));
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }

    if (len<6*numTabs) {
      f << "###";
      MWAW_DEBUG_MSG(("ScoopParser::readParagraph: the number of tabs seems bad\n"));
      numTabs=0;
    }
    f << "tabs=[";
    for (int i=0; i<numTabs; ++i) {
      MWAWTabStop tab;
      tab.m_position=float(input->readLong(4))/65536/72;
      val=int(input->readULong(1));
      switch (val&3) {
      case 0:
        tab.m_alignment = MWAWTabStop::CENTER;
        break;
      case 1:
        tab.m_alignment = MWAWTabStop::RIGHT;
        break;
      case 2:
        break;
      case 3:
        tab.m_alignment = MWAWTabStop::DECIMAL;
        break;
      default:
        break;
      }
      f << tab << ",";
      if (val&0xfc) f << "fl=" << std::hex << (val&0xfc) << std::dec << ",";
      input->seek(1, librevenge::RVNG_SEEK_CUR);
      para.m_tabs->push_back(tab);
    }
    f << "],";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  }

  if (id)
    m_state->m_idToParagraphMap[id]=para;
  return true;
}

bool ScoopParser::readText(MWAWEntry &entry, std::string const &what)
{
  entry=MWAWEntry();

  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;

  f << "TextZone-" << what << ":";
  long len=long(input->readULong(4));
  if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readText:can not find the text\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  std::string text;
  for (long i=0; i<len; ++i) text+=char(input->readLong(1));
  f << text << ",";
  entry.setBegin(pos+4);
  entry.setLength(len);
  input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

bool ScoopParser::readShapesList(std::vector<ScoopParserInternal::Shape> &shapes)
{
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Shape):";
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("ScoopParser::readShapesList: the header seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }

  long len=long(input->readULong(4));
  long endPos=pos+4+len;
  if (len<0 || endPos<pos+4 || !input->checkPosition(endPos) || (len%80)!=0) {
    f << "###";
    MWAW_DEBUG_MSG(("ScoopParser::readShapesList:can not find the zone's length\n"));
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  int N=int(len/80);
  shapes.reserve(size_t(N));
  for (int i=0; i<N; ++i) {
    ScoopParserInternal::Shape shape;
    if (!readShape(shape, i))
      return false;
    shapes.push_back(shape);
  }
  return true;
}

bool ScoopParser::readShape(ScoopParserInternal::Shape &shape, int id)
{
  auto input=getInput();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Shape-" << id << ":";
  if (!input->checkPosition(pos+80)) {
    MWAW_DEBUG_MSG(("ScoopParser::readShape: the zone seems too short\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  int val;
  shape.m_type=int(input->readULong(1));
  if (shape.m_type&0x80)
    f << "selected,";
  shape.m_type&=0x7f;
  char const *wh[]= { "group", nullptr, nullptr, "line", nullptr,
                      "rect", nullptr, "circle", "bitmap", "poly",
                      "picture", "layout", "diamond", "cross[line]", "multi[line]",
                      "spline", nullptr, "text"
                    };
  std::string what;
  if (shape.m_type<=17 && wh[shape.m_type])
    what=wh[shape.m_type];
  else {
    std::stringstream s;
    s << "typ" << shape.m_type;
    what=s.str();
  }
  f << what << ",";
  int patterns[]= {155, 0};
  for (int i=0; i<5; ++i) {
    val=int(input->readULong(1));
    int const expected[]= { 0 /*|-50*/, 0x11, 0, 155, 0};
    if (val==expected[i]) continue;
    if (i==1) {
      MWAWVec2i penSize=MWAWVec2i((val>>4),val&0xf);
      shape.m_style.m_lineWidth=float(penSize[0]+penSize[1])/2;
      f << "sz=" << penSize << ",";
    }
    else if (i==2) {
      shape.m_mode=val;
      f << "mode=" << val << ","; // 0: copy, 1: or, ...
    }
    else if (i==3) {
      patterns[0]=val;
      f << "pat[line]=" << val << ","; // 0-.. pat , 0x80+... gray
    }
    else if (i==4) {
      patterns[1]=val;
      f << "pat[surf]=" << val << ",";
    }
    else // 1: pos, 2: size, 4:existence, 8: pen setting, 10: attribute, 20: originality, 40:associated with layout???, 80:associated with layout???
      f << "lock=" << std::hex << val << std::dec << ",";
  }

  // time to affect the color
  for (int i=0; i<2; ++i) {
    if (patterns[i]==0) { // none
      if (i==0)
        shape.m_style.m_lineWidth=0;
    }
    else if (patterns[i]>=128 && patterns[i]<=255) {
      uint8_t grey=uint8_t(1+2*(patterns[i]-128));
      MWAWColor color(grey, grey, grey);
      if (i==0)
        shape.m_style.m_lineColor=color;
      else
        shape.m_style.setSurfaceColor(color);
    }
    else if (patterns[i]>0 && size_t(patterns[i])<=m_state->m_patterns.size()) {
      auto const &pattern=m_state->m_patterns[size_t(patterns[i]-1)];
      if (i==0) {
        MWAWColor color;
        if (pattern.getAverageColor(color))
          shape.m_style.m_lineColor=color;
      }
      else
        shape.m_style.setPattern(pattern);
    }
    else {
      MWAW_DEBUG_MSG(("ScoopParser::readShape: find unknown pattern %d\n", patterns[i]));
    }
  }
  for (int i=0; i<2; ++i) { // f6=0|-3
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==1) {
      shape.m_page=val;
      f << "page=" << val << ",";
      // can -2 or -1 appear? does this mean master page?
      if (val==-3)
        m_state->m_hasScrapPage=true;
      else if (val<0 || val>=m_state->m_numPages) {
        MWAW_DEBUG_MSG(("ScoopParser::readShape: find bad page=%d\n", val));
        f << "###";
      }
    }
    else
      f << "f0=" << val << ",";
  }
  val=int(input->readULong(1));
  if (val!=0) {
    shape.m_verticalMode=val&3;
    f << "vertical[mode]=" << shape.m_verticalMode << ","; // 0: center, 2: top
    if (val&4) {
      shape.m_flips[0]=true;
      f << "flip[hori],";
    }
    if (val&8) {
      shape.m_flips[1]=true;
      f << "flip[verti],";
    }
    // val&0x80 can also appear with bitmap, unsure what this means
    if (shape.m_type==17) {
      if (val&0x80) // ie. it is possible to write postscript commands and display them
        f << "as[graphic],";
      val&=0x7f;
    }
    val&=0xf8;
    if (val)
      f << "fl=" << std::hex << val << std::dec << ",";
  }
  val=int(input->readLong(1));
  if (val!=1) // main ff, sometime 1
    f << "fl1=" << val << ",";
  for (int st=0; st<2; ++st) {
    float dim[4];
    for (auto &d : dim) d=float(input->readLong(2));
    shape.m_boxes[st]=MWAWBox2f(MWAWVec2f(dim[1],dim[0]),MWAWVec2f(dim[3],dim[2]));
    if (st==0)
      f << "box=" << shape.m_boxes[st] << ",";
    else if (shape.m_boxes[st]!=MWAWBox2f() && shape.m_boxes[1]!=shape.m_boxes[0] &&
             shape.m_type!=5 && shape.m_type!=7 && shape.m_type!=8) // unsure, this box is often broken
      f << "box[orig]=" << shape.m_boxes[st] << ",";
  }
  switch (shape.m_type) {
  case 11:
    f << "id=" << input->readLong(1) << ",";
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    val=int(input->readLong(2));
    if (val) f << "f1=" << val << ",";
    break;
  case 0:
  case 8:
  case 9:
  case 10:
  case 15:
    shape.m_ids[0]=long(input->readULong(4));
    if (shape.m_ids[0])
      f << "ID[" << what << "]=" << std::hex << shape.m_ids[0] << std::dec << ",";
    break;
  default:
    for (int i=0; i<2; ++i) shape.m_local[i]=int(input->readULong(2));
    if (!shape.m_local[0] && !shape.m_local[1]) break;

    if (shape.m_type==5)
      f << "round=" << MWAWVec2i(shape.m_local[0],shape.m_local[1]) << ",";
    else if (shape.m_type==7)
      f << "arc,angles=" << MWAWVec2i(shape.m_local[1],shape.m_local[0]+shape.m_local[1]) << ",";
    else
      f << "unkn=" << MWAWVec2i(shape.m_local[0],shape.m_local[1]) << ",";
    break;
  }
  for (int i=0; i<2; ++i) {
    shape.m_ids[i+1]=int(input->readULong(4));
    if (!shape.m_ids[i+1]) continue;
    if (i==0 && shape.m_type==17) { // special this text data is stored in previous zone
      std::swap(shape.m_ids[i+1], shape.m_textId);
      f << "ID[text]=" << std::hex << shape.m_textId << std::dec << ",";
    }
    else
      f << "ID" << i+1 << "=" << std::hex << shape.m_ids[i+1] << std::dec << ",";
  }
  for (int i=0; i<10; ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    if (i==3) {
      if (shape.m_type==17) {
        shape.m_textLinkId=val;
        f << "link[id]=" << val << ",";
      }
      else if (shape.m_type==8)
        f << "res=" << val << ",";
      else
        f << "g" << i << "=" << val << ","; // find g3=0|1|2 in header zones
    }
    else if (i==5) {
      shape.m_rotation=val;
      f << "rot=" << val << ",";
    }
    else
      f << "g" << i << "=" << val << ",";
  }
  val=int(input->readULong(4)); // really big number
  if (val)
    f << "unkn1=" << std::hex << val << std::dec << ",";
  for (int i=0; i<6; ++i) { // 0
    val=int(input->readLong(2));
    if (val)
      f << "h" << i << "=" << val << ",";
  }
  shape.m_ids[3]=long(input->readULong(4)); // a bitmap to replace the content (flip mode)
  if (shape.m_ids[3])
    f << "ID[bitmap,final]=" << std::hex << shape.m_ids[3] << std::dec << ",";
  input->seek(pos+80, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int c=0; c<4; ++c) {
    if (!shape.m_ids[c]) continue;
    if (c==0 && shape.m_type==0) {
      if (!readShapesList(shape.m_children))
        return false;
      continue;
    }
    if (shape.m_type==11 && c==2)
      continue;
    pos=input->tell();
    long len=long(input->readULong(4));
    if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      MWAW_DEBUG_MSG(("ScoopParser::readShape: can not find a child at position=%lx\n", (unsigned long) pos));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return true;
    }

    shape.m_entries[c].setBegin(pos+4);
    shape.m_entries[c].setLength(len);

    f.str("");
    f << "Shape[data]:" << what << ",";
    if (c==0) {
      switch (shape.m_type) {
      case 9: // poly
      case 15: { // spline
        if (len<2) {
          MWAW_DEBUG_MSG(("ScoopParser::readShape: the vertices zone seems bad\n"));
          f << "###";
          break;
        }
        long len2=long(input->readULong(2));
        if (len2<len || (len2%4)!=2) {
          MWAW_DEBUG_MSG(("ScoopParser::readShape: can not determine the number of vertices\n"));
          f << "###";
          break;
        }

        int N=int(len2/4);
        f << "pts=[";
        for (int i=0; i<N; ++i) {
          int coords[2];
          for (auto &co : coords) co=int(input->readLong(2));
          shape.m_vertices.push_back(MWAWVec2i(coords[1],coords[0]));
          f << shape.m_vertices.back() << ",";
        }
        f << "],";
        break;
      }
      // case 10: // picture
      default:
        break;
      }
    }
    else if (c==3) {
      MWAWEmbeddedObject bitmap;
      if (readBitmap(shape.m_entries[c], bitmap, false))
        shape.m_entries[c]=MWAWEntry();
    }

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  }

  return true;
}

bool ScoopParser::readBitmap(MWAWEntry const &entry, MWAWEmbeddedObject &object, bool compressed)
{
  auto input=getInput();
  object=MWAWEmbeddedObject();
  if (!input) return false;
  if (!entry.valid() || !input->checkPosition(entry.end()) || entry.length()<14) {
    MWAW_DEBUG_MSG(("ScoopParser::readBitmap: the data seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  input->seek(entry.begin(), librevenge::RVNG_SEEK_SET);
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  auto numColByRow=static_cast<int>(input->readLong(2));
  f << "numCol[byRow]=" << numColByRow << ",";
  int dim[4];
  for (auto &d : dim) d=static_cast<int>(input->readULong(2));
  f << "dim=" << dim[1] << "x" << dim[0] << "<->" <<  dim[3] << "x" << dim[2] << ",";
  if (dim[2]<dim[0] || numColByRow*8 < dim[3]-dim[1] || dim[1]<0) {
    MWAW_DEBUG_MSG(("ScoopParser::readBitmap: the dimension seems bad\n"));
    f << "###";
    ascii().addPos(entry.begin()-4);
    ascii().addNote(f.str().c_str());
    return false;
  }
  ascii().addPos(entry.begin()-4);
  ascii().addNote(f.str().c_str());

  MWAWPictBitmapIndexed pict(MWAWVec2i(dim[3],dim[2]));
  std::vector<MWAWColor> colors= {MWAWColor::white(), MWAWColor::black()};
  pict.setColors(colors);

  for (int r=dim[0]; r<dim[2]; ++r) {
    long pos=input->tell();
    f.str("");
    f << "bitmap-R" << r << ":";
    if (input->tell()+1>entry.end()) {
      MWAW_DEBUG_MSG(("ScoopParser::readBitmap: can not read row %d\n", r));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    int col=dim[1];
    while ((col-dim[1])<8*numColByRow && input->tell()<entry.end()) { // UnpackBits
      auto wh=compressed ? static_cast<int>(input->readULong(1)) : 256;
      if (wh>=0x81) {
        auto color=static_cast<int>(input->readULong(1));
        for (int j=0; j < 0x101-wh; ++j) {
          for (int b=7; b>=0; --b) {
            if (col<dim[3])
              pict.set(col, r, (color>>b)&1);
            ++col;
          }
        }
      }
      else { // checkme normally 0x80 is reserved and almost nobody used it (for ending the compression)
        if (input->tell()+wh+1>entry.end()) {
          MWAW_DEBUG_MSG(("ScoopParser::readBitmap: can not read row %d\n", r));
          f << "###";
          ascii().addPos(pos);
          ascii().addNote(f.str().c_str());
          return false;
        }
        for (int j=0; j < wh+1; ++j) {
          auto color=static_cast<int>(input->readULong(1));
          for (int b=7; b>=0; --b) {
            if (col<dim[3])
              pict.set(col, r, (color>>b)&1);
            ++col;
          }
        }
      }
    }
  }
  ascii().skipZone(entry.begin()+14, input->tell()-1);
  return pict.getBinary(object);
}

////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool ScoopParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = ScoopParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(288))
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  if (input->readULong(4)!=0 && input->readULong(4)!=0x70 && input->readULong(2)!=0x1100) return false;

  if (strict) {
    // look for the printer information structure
    input->seek(0x7c, librevenge::RVNG_SEEK_SET);
    if (input->readULong(4)!=0x78)
      return false;
    // look if the first zone is a text zone or a list of shape
    input->seek(0x118, librevenge::RVNG_SEEK_SET);
    bool hasId=input->readULong(4)!=0;
    long len=long(input->readULong(4));
    if (len<0 || !input->checkPosition(0x118+len) || (hasId && len!=0x52) || (!hasId && (len%80)!=0))
      return false;
  }

  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  if (header)
    header->reset(MWAWDocument::MWAW_T_SCOOP, 1, MWAWDocument::MWAW_K_DRAW);

  return true;
}

////////////////////////////////////////////////////////////
// try to read the header zone
////////////////////////////////////////////////////////////
bool ScoopParser::readHeader()
{
  MWAWInputStreamPtr input = getInput();
  if (!input->checkPosition(124)) {
    MWAW_DEBUG_MSG(("ScoopParser::readHeader: the header zone seems too short\n"));
    ascii().addPos(10);
    ascii().addNote("Entries(Header):#");
    return false;
  }
  input->seek(10,librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  f << "fl=" << std::hex << input->readULong(2) << std::dec << ","; // 0404, 9bff
  int val;
  for (int i=0; i<2; ++i) {
    val=int(input->readULong(2));
    int const expected[]= {0x600, 0x101 };
    if (val==expected[i]) continue;
    f << "f" << i << "=" << std::hex << val << std::dec << ",";
  }
  f << "fl2=" << std::hex << input->readULong(2) << std::dec << ","; // 30|1174|170|470
  val=int(input->readULong(1));
  if (val!=0x2)
    f << "f2=" << std::hex << val << std::dec << ",";
  m_state->m_displayMode=int(input->readULong(1));
  if (m_state->m_displayMode!=1)
    f << "display[mode]=" << m_state->m_displayMode << ",";
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(2));
  f << "unkn=" << MWAWVec2i(dim[1],dim[0]) << ",";
  for (auto &d : dim) d=int(input->readULong(2));
  m_state->m_thumbnailSize=MWAWVec2i(dim[1],dim[0]);
  if (m_state->m_thumbnailSize!=MWAWVec2i(1,1))
    f << "num[pages]=" << m_state->m_thumbnailSize << ",";
  int dim4[4];
  for (auto &d : dim4) d=int(input->readULong(2));
  f << "box[layout?]=" << MWAWBox2i(MWAWVec2i(dim4[0],dim4[1]),MWAWVec2i(dim4[2],dim4[3])) << ",";
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(2));
    if (!val) continue;
    f << "f" << i+2 << "=" << val << ",";
  }
  m_state->m_numPages=int(input->readULong(2));
  if (m_state->m_numPages!=1) {
    f << "num[pages]=" << m_state->m_numPages << ",";
    if (m_state->m_numPages>100) {
      MWAW_DEBUG_MSG(("ScoopParser::readHeader: the number of pages seems bad, limits it to 100 pages\n"));
      f << "###";
      m_state->m_numPages=100;
    }
  }
  for (int i=0; i<3; ++i) { // g0=small negatif number: -20, ..
    val=int(input->readLong(2));
    int const expected[]= {-20, 0, 0x1ff};
    if (val==expected[i])
      continue;
    if (i==0) {
      m_state->m_rightPage=val;
      f << "page[right]=" << val << ",";
    }
    else if (i==1) {
      m_state->m_leftPage=val;
      f << "page[left]=" << val << ",";
    }
    else
      f << "g" << i << "=" << val << ",";
  }
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  val=int(input->readULong(4));
  if (val)
    f << "ID1=" << std::hex << val << std::dec << ",";
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());
  input->seek(60, librevenge::RVNG_SEEK_SET);

  long pos=input->tell();
  f.str("");
  f << "FileHeader-A:";
  for (auto &d : dim) d=int(input->readULong(2));
  m_state->m_layoutDimension=MWAWVec2i(dim[1],dim[0]);
  f << "dim=" << m_state->m_layoutDimension << ",";
  ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+64, librevenge::RVNG_SEEK_SET);

  if (!readPrintInfo())
    return false;

  pos=input->tell();
  f.str("");
  f << "FileHeader-B:";
  if (!input->checkPosition(pos+32)) {
    MWAW_DEBUG_MSG(("ScoopParser::readHeader: can not find the end of the header\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  for (int i=0; i<8; ++i) {
    val=int(input->readULong(4));
    // maybe some size: frame size?, frame dataB size?, ..., frame end size,
    int const expected[]= {82, 56, 56, 40, 18, 54, 32, 0};
    if (val!=expected[i])
      f << "f" << i << "=" << val << ",";
  }

  input->seek(pos+32, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  return true;
}

////////////////////////////////////////////////////////////
// try to read the print info zone
////////////////////////////////////////////////////////////
bool ScoopParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  long pos=input->tell();
  long len=long(input->readULong(4));
  long endPos=pos+4+len;
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo):";
  if (len<120 || endPos<pos+124 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ScoopParser::readPrintInfo: file seems too short\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  libmwaw::PrinterInfo info;
  if (!info.read(input)) {
    MWAW_DEBUG_MSG(("ScoopParser::readPrintInfo: can not read print info\n"));
    return false;
  }
  f << info;
  MWAWVec2i paperSize = info.paper().size();
  MWAWVec2i pageSize = info.page().size();
  if (pageSize.x() <= 0 || pageSize.y() <= 0 ||
      paperSize.x() <= 0 || paperSize.y() <= 0) {
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }

  // define margin from print info
  MWAWVec2i lTopMargin= -1 * info.paper().pos(0);
  MWAWVec2i rBotMargin=info.paper().size() - info.page().size();

  // move margin left | top
  int decalX = lTopMargin.x() > 14 ? lTopMargin.x()-14 : 0;
  int decalY = lTopMargin.y() > 14 ? lTopMargin.y()-14 : 0;
  lTopMargin -= MWAWVec2i(decalX, decalY);
  rBotMargin += MWAWVec2i(decalX, decalY);

  // decrease right | bottom
  int rightMarg = rBotMargin.x() -50;
  if (rightMarg < 0) rightMarg=0;
  int botMarg = rBotMargin.y() -50;
  if (botMarg < 0) botMarg=0;

  getPageSpan().setMarginTop(lTopMargin.y()/72.0);
  getPageSpan().setMarginBottom(botMarg/72.0);
  getPageSpan().setMarginLeft(lTopMargin.x()/72.0);
  getPageSpan().setMarginRight(rightMarg/72.0);
  getPageSpan().setFormLength(paperSize.y()/72.);
  getPageSpan().setFormWidth(paperSize.x()/72.);

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  if (input->tell()!=endPos) {
    ascii().addPos(input->tell());
    ascii().addNote("PrintInfo-extra:###");
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
