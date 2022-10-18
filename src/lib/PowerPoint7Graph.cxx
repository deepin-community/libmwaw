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
#include <map>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWGraphicShape.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWParser.hxx"
#include "MWAWPosition.hxx"
#include "MWAWGraphicStyle.hxx"
#include "MWAWPresentationListener.hxx"
#include "MWAWSubDocument.hxx"

#include "PowerPoint7Struct.hxx"

#include "PowerPoint7Graph.hxx"
#include "PowerPoint7Parser.hxx"

/** Internal: the structures of a PowerPoint7Graph */
namespace PowerPoint7GraphInternal
{
//! Internal: a frame of a PowerPoint7Graph
struct Frame {
  //! the frame type
  enum Type { Arc, Line, Group, Placeholder, Polygon, Rect, Unknown };
  //! constructor
  explicit Frame(Type type=Unknown)
    : m_type(type)
    , m_subType(-10000)
    , m_dimension()
    , m_rotation(0)
    , m_style()
    , m_pictureId(-1)
    , m_textId(-1)
    , m_isBackground(false)
    , m_isSent(false)
  {
    for (auto &flip : m_flip) flip=false;
  }
  //! destructor
  virtual ~Frame();
  //! try to update the list of text sub zone
  virtual void getTextZoneList(std::vector<int> &textIdList) const
  {
    if (m_textId>=0)
      textIdList.push_back(m_textId);
  }
  //! the type:
  enum Type m_type;
  //! the sub type
  int m_subType;
  //! the dimension
  MWAWBox2i m_dimension;
  //! the rotation
  float m_rotation;
  //! the flip flags: horizontal and vertical
  bool m_flip[2];
  //! the style
  MWAWGraphicStyle m_style;
  //! the picture id(if positif)
  int m_pictureId;
  //! the text id(if positif)
  int m_textId;
  //! a flag to know if this is the slide's background
  bool m_isBackground;
  //! flag to know if a frame is sent
  mutable bool m_isSent;
};

Frame::~Frame()
{
}

//! Internal: a frame rect of a PowerPoint7Graph
struct FrameArc final : public Frame {
  //! constructor
  FrameArc() : Frame(Arc)
  {
    m_angles[0]=0;
    m_angles[1]=90;
  }
  //! destructor
  ~FrameArc() final;
  //! update the shape
  bool updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const;
  //! the arc angles
  float m_angles[2];
};

FrameArc::~FrameArc()
{
}

bool FrameArc::updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const
{
  float angle[2] = { m_angles[0], m_angles[0]+m_angles[1] };
  if (angle[1]<angle[0])
    std::swap(angle[0],angle[1]);
  if (angle[1]>360) {
    int numLoop=int(angle[1]/360)-1;
    angle[0]-=float(numLoop*360);
    angle[1]-=float(numLoop*360);
    while (angle[1] > 360) {
      angle[0]-=360;
      angle[1]-=360;
    }
  }
  if (angle[0] < -360) {
    int numLoop=int(angle[0]/360)+1;
    angle[0]-=float(numLoop*360);
    angle[1]-=float(numLoop*360);
    while (angle[0] < -360) {
      angle[0]+=360;
      angle[1]+=360;
    }
  }
  MWAWVec2f center = finalBox.center();
  MWAWVec2f axis = 0.5f*MWAWVec2f(finalBox.size());
  // we must compute the real bd box
  float minVal[2] = { 0, 0 }, maxVal[2] = { 0, 0 };
  int limitAngle[2];
  for (int i = 0; i < 2; i++)
    limitAngle[i] = (angle[i] < 0) ? int(angle[i]/90)-1 : int(angle[i]/90);
  for (int bord = limitAngle[0]; bord <= limitAngle[1]+1; bord++) {
    float ang = (bord == limitAngle[0]) ? angle[0] :
                (bord == limitAngle[1]+1) ? angle[1] : 90 * float(bord);
    ang *= float(M_PI/180.);
    float actVal[2] = { axis[0] *std::cos(ang), -axis[1] *std::sin(ang)};
    if (actVal[0] < minVal[0]) minVal[0] = actVal[0];
    else if (actVal[0] > maxVal[0]) maxVal[0] = actVal[0];
    if (actVal[1] < minVal[1]) minVal[1] = actVal[1];
    else if (actVal[1] > maxVal[1]) maxVal[1] = actVal[1];
  }
  MWAWBox2f realBox(MWAWVec2f(center[0]+minVal[0],center[1]+minVal[1]),
                    MWAWVec2f(center[0]+maxVal[0],center[1]+maxVal[1]));
  shape = MWAWGraphicShape::pie(realBox, finalBox, MWAWVec2f(float(angle[0]),float(angle[1])));
  return true;
}

//! Internal: a group of a PowerPoint7Graph
struct FrameGroup final : public Frame {
  //! constructor
  FrameGroup()
    : Frame(Group)
    , m_child()
  {
  }
  //! destructor
  ~FrameGroup() final;
  //! try to update the list of text sub zone
  void getTextZoneList(std::vector<int> &textIdList) const final
  {
    for (auto child : m_child) {
      if (child)
        child->getTextZoneList(textIdList);
    }
  }
  //! the child
  std::vector<std::shared_ptr<Frame> > m_child;
};

FrameGroup::~FrameGroup()
{
}

//! Internal: a frame placeholder of a PowerPoint7Graph
struct FramePlaceholder final : public Frame {
  //! constructor
  FramePlaceholder()
    : Frame(Placeholder)
  {
  }
  //! destructor
  ~FramePlaceholder() final;
};

FramePlaceholder::~FramePlaceholder()
{
}

//! Internal: a polygon of a PowerPoint7Graph
struct FramePolygon final : public Frame {
  //! constructor
  FramePolygon()
    : Frame(Polygon)
    , m_vertices()
  {
  }
  //! destructor
  ~FramePolygon();
  //! update the shape
  bool updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const;
  //! the vertices
  std::vector<MWAWVec2i> m_vertices;
};

FramePolygon::~FramePolygon()
{
}

bool FramePolygon::updateShape(MWAWBox2f const &finalBox, MWAWGraphicShape &shape) const
{
  if (m_vertices.empty()) return false;
  MWAWBox2i actBox(m_vertices[0],m_vertices[0]);
  for (size_t i=1; i<m_vertices.size(); ++i) actBox=actBox.getUnion(MWAWBox2i(m_vertices[i],m_vertices[i]));
  float factor[2], decal[2];
  for (int i=0; i<2; ++i) {
    if (actBox.size()[i]<0||actBox.size()[i]>0)
      factor[i]=float(finalBox.size()[i])/float(actBox.size()[i]);
    else
      factor[i]=1.f;
    decal[i]=finalBox[0][i]-factor[i]*float(actBox[0][i]);
  }
  shape.m_type = MWAWGraphicShape::Polygon;
  for (auto const &pt : m_vertices)
    shape.m_vertices.push_back(MWAWVec2f(decal[0]+factor[0]*float(pt[0]), decal[1]+factor[1]*float(pt[1])));
  //if (m_type==1) shape.m_vertices.push_back(shape.m_vertices[0]);
  return true;
}

//! Internal: a frame rect of a PowerPoint7Graph
struct FrameRect final : public Frame {
  //! constructor
  FrameRect()
    : Frame(Rect)
  {
  }
  //! destructor
  ~FrameRect() final;
};

FrameRect::~FrameRect()
{
}

//! Internal: a picture of a PowerPoint7Graph
struct Picture {
  //! constructor
  Picture()
    : m_object()
    , m_box()
    , m_name("")
  {
  }
  //! returns true if the picture is empty
  bool isEmpty() const
  {
    return m_object.isEmpty();
  }
  //! the picture data
  MWAWEmbeddedObject m_object;
  //! the picture box
  MWAWBox2i m_box;
  //! the picture name
  std::string m_name;
};

////////////////////////////////////////
//! Internal: the state of a PowerPoint7Graph
struct State {
  //! constructor
  State()
    : m_decal(-2880,-2160)
    , m_actualSlideId()
    , m_colorList()
    , m_arrowList()
    , m_actualFrame()
    , m_actualGroup()
    , m_idToFrameMap()
    , m_idToPictureMap()
  {
  }
  //! try to add a frame
  void setFrame(Frame *frame)
  {
    if (!frame) {
      MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::setFrame: oops called with no frame\n"));
      return;
    }
    auto *group=dynamic_cast<FrameGroup *>(frame);
    std::shared_ptr<Frame> newFrame;
    bool inGroup=m_actualGroup.get()!=nullptr;
    if (group==nullptr) {
      if (m_actualFrame) {
        MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::setFrame: oops a frame is not closed\n"));
      }
      newFrame.reset(frame);
      m_actualFrame=newFrame;
      if (inGroup)
        m_actualGroup->m_child.push_back(newFrame);
    }
    else {
      std::shared_ptr<FrameGroup> newGroup(group);
      newFrame=newGroup;
      if (inGroup)
        m_actualGroup->m_child.push_back(newFrame);
      m_actualGroup=newGroup;
    }
    if (!inGroup && !m_actualSlideId.isValid()) {
      MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::setFrame: oops called with no parent\n"));
    }
    else if (!inGroup) {
      if (m_idToFrameMap.find(m_actualSlideId)==m_idToFrameMap.end())
        m_idToFrameMap[m_actualSlideId]=std::vector<std::shared_ptr<Frame> >();
      m_idToFrameMap.find(m_actualSlideId)->second.push_back(newFrame);
    }
  }
  //! reset the actual frame
  void resetFrame()
  {
    m_actualFrame.reset();
  }
  //! try to return a pattern
  bool getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const;
  //! returns an arrow if possible
  bool getArrow(int id, MWAWGraphicStyle::Arrow &arrow)
  {
    if (m_arrowList.empty()) initArrows();
    if (id<=0 || id>int(m_arrowList.size())) {
      MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::getArrow: can not find arrow %d\n", id));
      return false;
    }
    arrow=m_arrowList[size_t(id-1)];
    return true;
  }
  //! init the arrow list
  void initArrows();
  //! returns a custom shape corresponding to an id
  static bool getCustomShape(int id, MWAWGraphicShape &shape);
  //! the decal from file position to final position
  MWAWVec2i m_decal;
  //! the actual slide id
  PowerPoint7Struct::SlideId m_actualSlideId;
  //! the current color list
  std::vector<MWAWColor> m_colorList;
  //! the arrow list
  std::vector<MWAWGraphicStyle::Arrow> m_arrowList;
  //! the actual frame
  std::shared_ptr<Frame> m_actualFrame;
  //! the actual group
  std::shared_ptr<FrameGroup> m_actualGroup;
  //! a map slide id to the list of frame
  std::map<PowerPoint7Struct::SlideId, std::vector<std::shared_ptr<Frame> > > m_idToFrameMap;
  //! a map id to picture
  std::map<int, Picture> m_idToPictureMap;
};

bool State::getPattern(int id, MWAWGraphicStyle::Pattern &pattern) const
{
  // normally between 1 and 32 but find a pattern resource with 38 patterns
  if (id<=0 || id>=39) {
    MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::getPattern: unknown id=%d\n", id));
    return false;
  }
  static uint16_t const values[] = {
    0xffff, 0xffff, 0xffff, 0xffff, 0x0, 0x0, 0x0, 0x0,
    0xddff, 0x77ff, 0xddff, 0x77ff, 0x8000, 0x800, 0x8000, 0x800,
    0xdd77, 0xdd77, 0xdd77, 0xdd77, 0x8800, 0x2200, 0x8800, 0x2200,
    0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x8822, 0x8822, 0x8822, 0x8822,
    0x8844, 0x2211, 0x8844, 0x2211, 0x1122, 0x4488, 0x1122, 0x4488,
    0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xff00, 0xff00, 0xff00, 0xff00,
    0x81c0, 0x6030, 0x180c, 0x603, 0x8103, 0x60c, 0x1830, 0x60c0,
    0x8888, 0x8888, 0x8888, 0x8888, 0xff00, 0x0, 0xff00, 0x0,
    0xb130, 0x31b, 0xd8c0, 0xc8d, 0x8010, 0x220, 0x108, 0x4004,
    0xff80, 0x8080, 0x8080, 0x8080, 0xff88, 0x8888, 0xff88, 0x8888,
    0xff80, 0x8080, 0xff08, 0x808, 0xeedd, 0xbb77, 0xeedd, 0xbb77,
    0x7fff, 0xffff, 0xf7ff, 0xffff, 0x88, 0x4422, 0x1100, 0x0,
    0x11, 0x2244, 0x8800, 0x0, 0x8080, 0x8080, 0x808, 0x808, 0xf000,
    0x0, 0xf00, 0x0, 0x8142, 0x2418, 0x8142, 0x2418,
    0x8000, 0x2200, 0x800, 0x2200, 0x1038, 0x7cfe, 0x7c38, 0x1000,
    0x102, 0x408, 0x1824, 0x4281, 0xc1e0, 0x7038, 0x1c0e, 0x783,
    0x8307, 0xe1c, 0x3870, 0xe0c1, 0xcccc, 0xcccc, 0xcccc, 0xcccc,
    0xffff, 0x0, 0xffff, 0x0, 0xf0f0, 0xf0f0, 0xf0f, 0xf0f,
    0x6699, 0x9966, 0x6699, 0x9966, 0x8142, 0x2418, 0x1824, 0x4281,
  };
  pattern.m_dim=MWAWVec2i(8,8);
  uint16_t const *ptr=&values[4*(id-1)];
  pattern.m_data.resize(8);
  for (size_t i=0; i < 4; ++i, ++ptr) {
    pattern.m_data[2*i]=static_cast<unsigned char>((*ptr)>>8);
    pattern.m_data[2*i+1]=static_cast<unsigned char>((*ptr)&0xff);
  }
  return true;
}

void State::initArrows()
{
  if (!m_arrowList.empty())
    return;
  m_arrowList.push_back(MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1580)),
                        "M1013 1491l118 89-567-1580-564 1580 114-85 136-68 148-46 161-17 161 13 153 46z", false));
  m_arrowList.push_back(MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1131)), "M462 1118l-102-29-102-51-93-72-72-93-51-102-29-102-13-105 13-102 29-106 51-102 72-89 93-72 102-50 102-34 106-9 101 9 106 34 98 50 93 72 72 89 51 102 29 106 13 102-13 105-29 102-51 102-72 93-93 72-98 51-106 29-101 13z", false));
  m_arrowList.push_back(MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1131)), "M462 1118l-102-29-102-51-93-72-72-93-51-102-29-102-13-105 13-102 29-106 51-102 72-89 93-72 102-50 102-34 106-9 101 9 106 34 98 50 93 72 72 89 51 102 29 106 13 102-13 105-29 102-51 102-72 93-93 72-98 51-106 29-101 13z", false));
  m_arrowList.push_back(MWAWGraphicStyle::Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(1131,1580)),
                        "M1013 1491l118 89-567-1580-564 1580 114-85 136-68 148-46 161-17 161 13 153 46z", false));
}

bool State::getCustomShape(int id, MWAWGraphicShape &shape)
{
  int N=4;
  double const *vertices=nullptr;
  switch (id) {
  case 0: {
    static double const v[]= {0.5,1, 1,0.5, 0.5,0, 0,0.5 };
    vertices=v;
    break;
  }
  case 1: {
    N=3;
    static double const v[]= {0,1, 1,1, 0.5,0};
    vertices=v;
    break;
  }
  case 2: {
    N=3;
    static double const v[]= {0,1, 1,1, 0,0};
    vertices=v;
    break;
  }
  case 3: {
    static double const v[]= {0,1, 0.7,1, 1,0, 0.3,0 };
    vertices=v;
    break;
  }
  case 4: {
    static double const v[]= {0,1, 0.3,0, 0.7,0, 1,1 };
    vertices=v;
    break;
  }
  case 5: {
    N=6;
    static double const v[]= {0,0.5, 0.2,1, 0.8,1, 1,0.5, 0.8,0, 0.2,0};
    vertices=v;
    break;
  }
  case 6: {
    N=8;
    static double const v[]= {0,0.3, 0,0.7, 0.3,1, 0.7,1, 1,0.7, 1,0.3, 0.7,0, 0.3,0};
    vertices=v;
    break;
  }
  case 7: {
    N=12;
    static double const v[]= {0,0.2, 0,0.8, 0.2,0.8, 0.2,1,
                              0.8,1, 0.8,0.8, 1,0.8, 1,0.2,
                              0.8,0.2, 0.8,0, 0.2,0, 0.2,0.2
                             };
    vertices=v;
    break;
  }
  case 8: {
    N=10;
    static double const v[]= {0.5,0, 0.383,0.383, 0,0.383, 0.3112,0.62,
                              0.1943,1, 0.5,0.78, 0.8056,1, 0.688,0.62,
                              1,0.3822, 0.6167,0.3822,
                             };
    vertices=v;
    break;
  }
  case 9: {
    N=7;
    static double const v[]= {0,0.333, 0,0.666, 0.7,0.666, 0.7,1,
                              1,0.5, 0.7,0, 0.7,0.333
                             };
    vertices=v;
    break;
  }
  case 10: {
    N=7;
    static double const v[]= {0,0.2, 0,0.8, 0.7,0.8, 0.7,1,
                              1,0.5, 0.7,0, 0.7,0.2
                             };
    vertices=v;
    break;
  }
  case 11: {
    N=5;
    static double const v[]= {0,0, 0,1, 0.7,1, 1,0.5, 0.7,0};
    vertices=v;
    break;
  }
  case 12: {
    N=12;
    static double const v[]= {0,1, 0.8,1, 1,0.8, 1,0,
                              0.8,0.2, 0.8,1, 0.8,0.2, 0,0.2,
                              0.2,0., 1,0, 0.2,0, 0,0.2
                             };

    vertices=v;
    break;
  }
  case 13: {
    N=11;
    static double const v[]= {0,0.1, 0,0.8, 0.1,0.9, 0.2,0.9,
                              0.1,1, 0.3,0.9, 0.9,0.9, 1,0.8,
                              1,0.1, 0.9,0, 0.1,0
                             };
    vertices=v;
    break;
  }
  case 14: {
    N=24;
    static double const v[]= { 0.5,0, 0.55,0.286, 0.7465,0.07, 0.656,0.342,
                               0.935,0.251, 0.7186,0.4465, 1,0.5, 0.7186,0.5535,
                               0.935,0.75, 0.6558,0.66558, 0.7465,0.9349, 0.558,0.7186,
                               0.495,1, 0.44,0.7186, 0.2511,0.935, 0.3418,0.6627,
                               0.063,0.7535, 0.279,0.558, 0,0.502, 0.279,0.4465,
                               0.063,0.2511, 0.3418,0.3418, 0.2511,0.069, 0.4395,0.286
                             };
    vertices=v;
    break;
  }
  default:
    break;
  }
  if (N<=0 || !vertices) {
    MWAW_DEBUG_MSG(("PowerPoint7GraphInternal::State::getCustomShape: unknown id %d\n", id));
    return false;
  }
  shape.m_type = MWAWGraphicShape::Polygon;
  shape.m_vertices.resize(size_t(N+1));
  for (int i=0; i<N; ++i)
    shape.m_vertices[size_t(i)]=MWAWVec2f(float(vertices[2*i]),float(vertices[2*i+1]));
  shape.m_vertices[size_t(N)]=MWAWVec2f(float(vertices[0]),float(vertices[1]));
  return true;
}

////////////////////////////////////////
//! Internal: the subdocument of a PowerPoint7Graph
class SubDocument final : public MWAWSubDocument
{
public:
  //! constructor for a text zone
  SubDocument(PowerPoint7Graph &parser, MWAWInputStreamPtr const &input, int tId)
    : MWAWSubDocument(nullptr, input, MWAWEntry())
    , m_powerpointParser(parser)
    , m_textId(tId)
    , m_listTextId()
  {
  }
  //! constructor for a list text zone
  SubDocument(PowerPoint7Graph &parser, MWAWInputStreamPtr const &input, std::vector<int> const &listTextId)
    : MWAWSubDocument(nullptr, input, MWAWEntry())
    , m_powerpointParser(parser)
    , m_textId(-1)
    , m_listTextId(listTextId)
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
    if (&m_powerpointParser != &sDoc->m_powerpointParser) return true;
    if (m_textId != sDoc->m_textId) return true;
    if (m_listTextId != sDoc->m_listTextId) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the parser
  PowerPoint7Graph &m_powerpointParser;
  //! the text id
  int m_textId;
  //! a list of text id
  std::vector<int> m_listTextId;
private:
  SubDocument(SubDocument const &) = delete;
  SubDocument &operator=(SubDocument const &) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener.get()) {
    MWAW_DEBUG_MSG(("PowerPoint7ParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  long pos = m_input->tell();
  if (m_textId>=0)
    m_powerpointParser.sendText(m_textId);
  else {
    for (size_t z=0; z<m_listTextId.size(); ++z) {
      if (z) listener->insertEOL();
      m_powerpointParser.sendText(m_listTextId[z]);
    }
  }
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
PowerPoint7Graph::PowerPoint7Graph(PowerPoint7Parser &parser)
  : m_parserState(parser.getParserState())
  , m_state(new PowerPoint7GraphInternal::State)
  , m_mainParser(&parser)
{
}

PowerPoint7Graph::~PowerPoint7Graph()
{ }

int PowerPoint7Graph::version() const
{
  return m_parserState->m_version;
}

void PowerPoint7Graph::setPageSize(MWAWVec2i &pageSize)
{
  m_state->m_decal=MWAWVec2i(pageSize[0]/2,pageSize[1]/2);
}

void PowerPoint7Graph::setSlideId(PowerPoint7Struct::SlideId const &id)
{
  m_state->m_actualSlideId=id;
}

void PowerPoint7Graph::setColorList(std::vector<MWAWColor> const &colorList)
{
  m_state->m_colorList=colorList;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

bool PowerPoint7Graph::readGroup(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3001) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readGroup: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Group)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  auto actualGroup=m_state->m_actualGroup;
  m_state->setFrame(new PowerPoint7GraphInternal::FrameGroup);
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2000:
      done=m_mainParser->readContainerList(level+1,endPos);
      break;
    case 3000:
      done=m_mainParser->readZone3000(level+1,endPos);
      break;
    case 3002:
      done=readGroupAtom(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readGroup: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readGroup: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Group:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->m_actualGroup=actualGroup;
  return true;
}

bool PowerPoint7Graph::readGroupAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3002) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readGroupAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Group)[atom," << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readGroupAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) {
      auto val=int(input->readULong(2));
      int const expected[]= {0x3b5b, 0x5000};
      if (val!=expected[i]) f << "f" << i << "=" << std::hex << val << std::dec << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readStyle(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3005) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphStyle)[" << level << "]:" << header;
  if (header.m_dataSize!=0x38) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  MWAWGraphicStyle emptyStyle;
  MWAWGraphicStyle *style=m_state->m_actualFrame ? &m_state->m_actualFrame->m_style : &emptyStyle;
  int val;
  //
  // line
  //
  val=int(input->readLong(1));
  bool showLine=true;
  if (val==-1 || val==1) { // normally -1, but =1 in dual powerpoint 95 and 97 files
    showLine=false;
    f << "no[line],";
  }
  else if (val) f << "fl0=" << val << ",";
  auto dashId=int(input->readLong(1));
  switch (dashId) {
  case 0: // no dash
    break;
  case 1:
    f << "dot,";
    break;
  case 2:
    f << "dot[2x2],";
    break;
  case 3:
    f << "dot[4x2],";
    break;
  case 4:
    f << "dot[4,4,1,4],";
    break;
  default:
    f << "###dashId=" << dashId << ",";
  }
  val=int(input->readLong(1)); // 0
  if (val) f << "f0=" << val << ",";
  auto lineW=int(input->readLong(1));
  if (lineW>=1 && lineW<=9) {
    char const *wh[]= {"w=1", "w=2","w=4", "w=8", "w=16", "w=32",
                       "double", "double1x2", "double2x1", "triple1x2x1"
                      };
    f << wh[lineW] << ",";
  }
  else if (lineW) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: find unexpected line style\n"));
    f << "##style[line]=" << lineW << ",";
  }
  unsigned char col[4];
  for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
  MWAWColor lineColor=MWAWColor::black();
  if (col[3]==0xfe)
    lineColor=MWAWColor(col[0],col[1],col[2]);
  else if (col[3]<int(m_state->m_colorList.size()))
    lineColor=m_state->m_colorList[size_t(col[3])];
  else {
    // normally can happen one time at the beginning
    if (m_state->m_actualSlideId.isValid()) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: can not find the line color\n"));
      f << "##";
    }
    f << "color[lineId]=" << int(col[3]) << ",";
  }
  if (!lineColor.isBlack())
    f << "color[line]=" << lineColor<< ",";
  if (!showLine)
    style->m_lineWidth=0;
  else {
    int lineWidth=1;
    MWAWBorder border;
    if (lineW>=1&&lineW<=9) {
      int const lWidth[]= {1, 2, 3, 6, 8, 10, 3, 4, 4, 6};
      lineWidth=lWidth[lineW];
      switch (lineW) {
      case 6:
        border.m_type=MWAWBorder::Double;
        break;
      case 7:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        break;
      case 8:
        border.m_type=MWAWBorder::Double;
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      case 9:
        border.m_type=MWAWBorder::Triple;
        border.m_widthsList.push_back(1);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(2);
        border.m_widthsList.push_back(0);
        border.m_widthsList.push_back(1);
        break;
      default:
        break;
      }
    }
    style->m_lineWidth=float(lineWidth);
    border.m_width=double(lineWidth);
    style->setBorders(0xF, border);

    style->m_lineColor=border.m_color=lineColor;
    switch (dashId) {
    case 0: // no dash
    default: // problem
      break;
    case 1:
      style->m_lineDashWidth.resize(2,float(lineWidth));
      break;
    case 2:
      style->m_lineDashWidth.resize(2,float(2*lineWidth));
      break;
    case 3:
      style->m_lineDashWidth.resize(2,float(4*lineWidth));
      break;
    case 4:
      style->m_lineDashWidth.resize(4,float(2*lineWidth));
      style->m_lineDashWidth[2]=float(lineWidth);
      break;
    }
  }

  for (int i=0; i<2; ++i) { // 0
    val=int(input->readLong(2));
    if (val) f << "f" << i+1 << "=" << val << ",";
  }

  //
  // surface
  //
  val=int(input->readLong(1));
  bool showSurf=true;
  if (val==-1 || val==1) {
    showSurf=false;
    f << "no[surf],";
  }
  else if (val) f << "fl1=" << val << ",";
  auto surfType=int(input->readLong(1));
  switch (surfType) {
  case 1: // color
    break;
  case 2:
    f << "background,";
    break;
  case 3:
    f << "transparent[semi],";
    break;
  case 4:
    f << "pattern,";
    break;
  case 5:
    f << "gradient,";
    break;
  case 6:
    f << "picture,";
    break;
  case 7:
    f << "background[picture],";
    break;
  default:
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: find unexpected surface type\n"));
    f << "##surf[type]=" << surfType << ",";
    break;
  }
  int patGradId=0, gradType=0, gradColorMapId=0, subGradientId=0;
  for (int i=0; i<6; ++i) { // fl3=0|78-b7,fl4=0|1|4,then small number
    val=int(input->readULong(1));
    if (!val) continue;
    switch (i) {
    case 2:
      patGradId=val;
      f << "patGrad[id]=" << val << ",";
      break;
    case 3:
      subGradientId=val;
      f << "grad[subId]=" << val << ",";
      break;
    case 4:
      gradColorMapId=val;
      f << "grad[colorMap]=" << val << ",";
      break;
    case 5: // 0: one color, 1: two color, 2: preset
      gradType=val;
      if (val==2)
        f << "gradType=preset,";
      else if (val!=1)
        f << "###gradType=" << val << ",";
      break;
    default:
      f << "fl" << i+3 << "=" << std::hex << val << std::dec << ",";
    }
  }
  MWAWColor surfColors[2]= {MWAWColor::white(), MWAWColor::black()};
  for (int c=0; c<2; ++c) {
    for (auto &co : col) co=static_cast<unsigned char>(input->readULong(1));
    if (col[3]==0xfe)
      surfColors[c]=MWAWColor(col[0],col[1],col[2]);
    else if (col[3]<int(m_state->m_colorList.size()))
      surfColors[c]=m_state->m_colorList[size_t(col[3])];
    else {
      // normally can happen one time at the beginning
      if (m_state->m_actualSlideId.isValid()) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: can not find the surface color\n"));
        f << "##";
      }
      f << "color" << c << "[surf]=" << int(col[3]) << ",";
    }
    if ((c==0 && !surfColors[c].isWhite()) || (c==1 && !surfColors[c].isBlack()))
      f << "color" << c << "[surf]=" << surfColors[c] << ",";
  }
  if (showSurf) {
    switch (surfType) {
    case 1:
      style->setSurfaceColor(surfColors[0]);
      break;
    case 2:
      if (!m_state->m_colorList.empty())
        style->setSurfaceColor(m_state->m_colorList[0]);
      break;
    case 3:
      style->setSurfaceColor(surfColors[0], 0.5);
      break;
    case 4: {
      MWAWGraphicStyle::Pattern pattern;
      if (m_state->getPattern(patGradId+1, pattern)) {
        pattern.m_colors[0]=surfColors[1];
        pattern.m_colors[1]=surfColors[0];
        MWAWColor color;
        if (pattern.getUniqueColor(color))
          style->setSurfaceColor(color);
        else
          style->setPattern(pattern);
      }
      break;
    }
    case 5: {
      auto &finalGrad=style->m_gradient;
      finalGrad.m_stopList.resize(0);
      MWAWColor colors[]= {surfColors[0],surfColors[1]};
      if (gradType==2 && gradColorMapId>=0 && gradColorMapId<=15) {
        uint32_t const defColors[]= {
          0xff, 0xff0000, // early sunset
          0xff, 0xffff00, // late sunset
          0, 0x80, // night fall
          0xff, 0xffffff, // daybreak
          0xfff8dc, 0xd284bc, // parchment
          0xfff8dc, 0xbf8f8f, // Mahogany
          0x80, 0x808080, // fog
          0xffffff, 0xff00, // moss
          0xff, 0x80, // ocean
          0xffff00, 0xff0000, // fire
          0xff00ff, 0xffff00, // rainbow
          0xffffff, 0xffff00, // gold
          0xffff00, 0x808000, // brass
          0xffffff, 0x808080, // chrome
          0xffffff, 0x808080, // silver
          0xff, 0x80, // sapphire
        };
        colors[0]=defColors[2*gradColorMapId];
        colors[1]=defColors[2*gradColorMapId+1];
      }
      if (patGradId>=1 && patGradId<=4) {
        if (subGradientId<2) {
          finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Linear;
          for (int c=0; c < 2; ++c)
            finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), (c==subGradientId)  ? colors[0] : colors[1]));
        }
        else {
          finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Axial;
          for (int c=0; c < 3; ++c)
            finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c)/2.f, ((c%2)==(subGradientId%2)) ? colors[0] : colors[1]));
        }
        float angles[]= {90,0,45,315};
        finalGrad.m_angle=angles[patGradId-1];
      }
      else if (patGradId==5) {
        finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Rectangular;
        for (int c=0; c < 2; ++c)
          finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), c==0 ? colors[0] : colors[1]));
        finalGrad.m_percentCenter=MWAWVec2f(float(subGradientId&1),float(subGradientId<2 ? 0 : 1));
      }
      else if (patGradId==7) {
        finalGrad.m_type=MWAWGraphicStyle::Gradient::G_Rectangular;
        for (int c=0; c < 2; ++c)
          finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(float(c), ((c%2)==(subGradientId%2))  ? colors[0] : colors[1]));
      }
      else {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: find unknown gradient\n"));
        style->setSurfaceColor(colors[0]);
      }
      break;
    }
    case 6: {
      if (m_state->m_idToPictureMap.find(patGradId)==m_state->m_idToPictureMap.end()) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: can not find picture %d\n", patGradId));
        break;
      }
      auto const &picture=m_state->m_idToPictureMap.find(patGradId)->second;
      MWAWGraphicStyle::Pattern pattern(picture.m_box.size(), picture.m_object.m_dataList[0], surfColors[0]);
      style->setPattern(pattern);
      break;
    }
    default:
      break;
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  //
  // the shadow
  //
  pos=input->tell();
  f.str("");
  f << "GraphStyle-A:";
  bool hasShadow=false;
  val=int(input->readLong(1));
  if (val==0) {
    f << "has[shadow],";
    hasShadow=true;
  }
  else if (val!=-1 && val!=1) f << "#has[shadow]=" << val << ",";
  auto shadowType=int(input->readLong(1)); // 1
  switch (shadowType) {
  case 1: // basic
    break;
  case 2:
    f << "semi[transparent],";
    break;
  default:
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: find unexpected shadow type\n"));
    f << "##shadow[type]=" << shadowType << ",";
  }
  for (auto &c : col) c=static_cast<unsigned char>(input->readULong(1));
  MWAWColor shadowColor=MWAWColor::black();
  if (col[3]==0xfe)
    shadowColor=MWAWColor(col[0],col[1],col[2]);
  else if (col[3]<int(m_state->m_colorList.size()))
    shadowColor=m_state->m_colorList[size_t(col[3])];
  else {
    // normally can happen one time at the beginning
    if (m_state->m_actualSlideId.isValid()) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::readStyle: can not find the shadow color\n"));
      f << "##";
    }
    f << "color[shadowId]=" << int(col[3]) << ",";
  }
  if (!shadowColor.isBlack())
    f << "color[shadow]=" << shadowColor<< ",";
  val=int(input->readLong(2)); // 0|bd
  if (val) f << "f1=" << val << ",";
  float shadowDepl[2]= {6,6};
  for (int i=0; i<2; ++i) {
    long depl=input->readLong(4);
    if (depl==48) continue;
    shadowDepl[i]=float(depl)/8.f;
    f << "depl[" << (i==0 ? "right" : "bottom") << "]=" << shadowDepl[i] << ",";
  }
  if (hasShadow) {
    style->setShadowColor(shadowColor, shadowType==2 ? 0.5f : 1.f);
    style->m_shadowOffset=MWAWVec2f(shadowDepl[0],shadowDepl[1]);
  }
  val=int(input->readULong(1)); // 0|1|4
  if (val) f << "f2=" << val << ",";
  val=int(input->readULong(2)); // 0|62XX
  if (val) f << "f3=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(1)); // 0|1|50
  if (val) f << "f4=" << std::hex << val << std::dec << ",";
  val=int(input->readLong(2));
  if (val) f << "rot=" << float(val)/16.f << ",";
  val=int(input->readULong(2)); // 0
  if (val) f << "f5=" << val << ",";
  val=int(input->readULong(1));
  if (val&0x1) {
    if (m_state->m_actualFrame) m_state->m_actualFrame->m_flip[0]=true;
    f << "flipX,";
  }
  if (val&0x2) {
    if (m_state->m_actualFrame) m_state->m_actualFrame->m_flip[1]=true;
    f << "flipY,";
  }
  val&=0xfc;
  if (val) f << "fl1=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(2));
  if (val) f << "f5=" << std::hex << val << std::dec << ",";
  val=int(input->readULong(1)); // 0
  if (val) f << "fl2=" << val << ",";
  input->seek(pos+28, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readLineArrows(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3007) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLineArrows: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  MWAWGraphicStyle emptyStyle;
  MWAWGraphicStyle *style=m_state->m_actualFrame ? &m_state->m_actualFrame->m_style : &emptyStyle;
  f << "Entries(GraphLine)[arrows," << level << "]:" << header;
  if (header.m_dataSize!=2) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLineArrows: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<2; ++i) { // f0=0|1, f1=0|1
      auto val=int(input->readULong(1));
      if (!val) continue;
      MWAWGraphicStyle::Arrow arrow;
      if (m_state->getArrow(val, arrow))
        style->m_arrows[i]=arrow;
      f << "arrow[" << (i==0 ? "start" : "end") << "]=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readRect(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3008) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readRect: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphRect)[" << level << "]:" << header;
  switch (header.m_values[3]) {
  case 16:
    f << "type=16,";
    break;
  case 19: // basic
    break;
  case 28:
    f << "background,";
    break;
  default:
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readRect: find unknow type\n"));
    f << "##type=" << header.m_values[3] << ",";
    break;
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->setFrame(new PowerPoint7GraphInternal::FrameRect);
  auto &frame=*m_state->m_actualFrame;
  if (header.m_values[3]==28)
    frame.m_isBackground=true;
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=readStyle(level+1,endPos);
      break;
    case 3009:
      done=readRectAtom(level+1,endPos);
      break;
    case 3036:
      done=readZoneFlags(level+1,endPos);
      break;
    case 4001:
      if (frame.m_textId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readRect: already find some text zone\n"));
      }
      done=m_mainParser->readStyleTextPropAtom(level+1,endPos,frame.m_textId);
      break;
    case 4014: {
      PowerPoint7Struct::SlideId sId;
      done=m_mainParser->readOutlineTextProps9Atom(level+1,endPos,frame.m_pictureId,sId);
      break;
    }
    case 4072:
      done=m_mainParser->readZone4072(level+1,endPos);
      break;
    case 5000:
      done=readZone5000(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readRect: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readRect: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("GraphRect:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->resetFrame();
  return true;
}

bool PowerPoint7Graph::readRectAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3009) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readRectAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphRect)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x28) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readRectAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  auto frame=m_state->m_actualFrame;
  auto type=int(input->readLong(1));
  if (frame) frame->m_subType=type;
  switch (type) {
  case -3:
    f << "rect,";
    break;
  case -2:
    f << "rectOval,";
    break;
  case -1:
    f << "circle,";
    break;
  default: // >=1 && <=? : the shape id
    f << "type=" << type << ",";
    break;
  }
  auto val=int(input->readULong(1));
  if (val!=0xff) { // unsure
    switch ((val>>5)&3) {
    case 0:
      break;
    case 2:
      f << "flipX,";
      break;
    case 3:
      f << "flipY,";
      break;
    default:
      f << "##flip=1,";
      break;
    }
    val&=0x9f;
    if (val) f << "##flip[other]=" << std::hex << val << std::dec << ",";
  }
  for (int i=0; i<3; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {-1,0,0};
    if (val!=expected[i]) f << "f" << i << "=" << val << ",";
  }
  for (int i=0; i<4; ++i) {
    val=int(input->readULong(1));
    if (val) f << "fl" << i << "=" << std::hex << val << std::dec << ",";
  }
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  MWAWBox2i dimension(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  if (frame) frame->m_dimension=dimension;
  f << "dim=" << dimension << ",";
  val=int(input->readLong(2));
  if (val) {
    if (frame) frame->m_rotation=float(val)/16.f;
    f << "rot=" << float(val)/16.f << ",";
  }
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(i==0 ? 2 : 4));
    int const expected[]= {0,-3};
    if (val!=expected[i])
      f << "f" << i+6 << "=" << val << ",";
  }
  val=int(input->readULong(1));
  if (val==1)
    f << "has[anchor],";
  else if (val)
    f << "##has[anchor]=" << val << ",";
  for (int i=0; i<3; ++i) { // fl0=0-1, fl1=0|76-b8
    val=int(input->readULong(1));
    if (val) f << "fl" << i+4 << "=" << std::hex << val << std::dec << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readPlaceholderContainer(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3010) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPlaceholderContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Placeholder)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->setFrame(new PowerPoint7GraphInternal::FramePlaceholder);
  auto &frame=*m_state->m_actualFrame;
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=readStyle(level+1,endPos);
      break;
    case 3009:
      done=readRectAtom(level+1,endPos);
      break;
    case 3011:
      done=readPlaceholderAtom(level+1,endPos);
      break;
    case 3036:
      done=readZoneFlags(level+1,endPos);
      break;
    case 4001:
      if (frame.m_textId!=-1) {
        MWAW_DEBUG_MSG(("PowerPoint7GraphPlaceholderContainer::read: already find some text zone\n"));
      }
      done=m_mainParser->readStyleTextPropAtom(level+1,endPos,frame.m_textId);
      break;
    case 4014: {
      int pId;
      PowerPoint7Struct::SlideId sId;
      done=m_mainParser->readOutlineTextProps9Atom(level+1,endPos,pId,sId);
      break;
    }
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPlaceholderContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPlaceholderContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Placeholder:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->resetFrame();
  return true;
}

bool PowerPoint7Graph::readPlaceholderAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3011) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPlaceholderAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Placeholder)[atom," << level << "]:" << header;
  if (header.m_dataSize!=8) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPlaceholderAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<4; ++i) { // f0 id?, f1=62, f2=0-6
      auto val=int(input->readULong(2));
      if (!val) continue;
      f << "f" << i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readLine(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3014) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLine: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphLine)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->setFrame(new PowerPoint7GraphInternal::Frame(PowerPoint7GraphInternal::Frame::Line));
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=readStyle(level+1,endPos);
      break;
    case 3007:
      done=readLineArrows(level+1,endPos);
      break;
    case 3015:
      done=readLineAtom(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readLine: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLine: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("GraphLine:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->resetFrame();
  return true;
}

bool PowerPoint7Graph::readLineAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3015) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLineAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphLine)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x10) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readLineAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  auto frame=m_state->m_actualFrame;
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  MWAWBox2i dimension(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  if (frame) frame->m_dimension=dimension;
  f << "dim=" << dimension << ",";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readPolygon(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3016) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPolygon: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphPolygon)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  auto *poly=new PowerPoint7GraphInternal::FramePolygon;
  m_state->setFrame(poly);
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=readStyle(level+1,endPos);
      break;
    case 3007:
      done=readLineArrows(level+1,endPos);
      break;
    case 3017:
      done=readPolygonAtom(level+1,endPos);
      break;
    case 3035:
      done=readPointList(level+1,endPos, poly->m_vertices);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPolygon: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPolygon: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("GraphPolygon:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->resetFrame();
  return true;
}

bool PowerPoint7Graph::readPolygonAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3017) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPolygonAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphPolygon)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x28) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPolygonAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  auto frame=m_state->m_actualFrame;
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  MWAWBox2i dimension(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  if (frame) frame->m_dimension=dimension;
  f << "dim=" << dimension << ",";
  for (auto &d : dim) d=int(input->readLong(4));
  f << "dim2=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
  for (int i=0; i<4; ++i) { // f0=4,f2=dc01,f3=12
    int val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readArc(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3018) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readArc: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphArc)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  m_state->setFrame(new PowerPoint7GraphInternal::FrameArc);
  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 3005:
      done=readStyle(level+1,endPos);
      break;
    case 3007:
      done=readLineArrows(level+1,endPos);
      break;
    case 3019:
      done=readArcAtom(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readArc: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readArc: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("GraphArc:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  m_state->resetFrame();
  return true;
}

bool PowerPoint7Graph::readArcAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3019) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readArcAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphArc)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x20) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readArcAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  auto frame=m_state->m_actualFrame;
  PowerPoint7GraphInternal::FrameArc *arc=nullptr;
  if (frame) arc=dynamic_cast<PowerPoint7GraphInternal::FrameArc *>(frame.get());
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  MWAWBox2i dimension(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  if (frame) frame->m_dimension=dimension;
  f << "dim=" << dimension << ",";
  f << "angles=[";
  for (int i=0; i<2; ++i) { // find 1440,-1440
    float angle=float(input->readLong(4))/16.f;
    if (arc) arc->m_angles[i]=angle;
    f << angle << ",";
  }
  f << "],";
  auto val=int(input->readLong(2));
  if (val) {
    if (frame)
      frame->m_rotation=float(val)/16.0f;
    f << "rot=" << float(val)/16.0f << ",";
  }
  for (int i=0; i<3; ++i) { // f2=74
    val=int(input->readLong(2));
    if (val)
      f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readPointList(int level, long lastPos, std::vector<MWAWVec2i> &points)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3035) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPointList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphPointList)[" << level << "]:" << header;
  int N=header.m_dataSize>=2 ? int(input->readLong(2)) : 0;
  if (8*N+2!=header.m_dataSize) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPointList: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  f << "points=[";
  points.resize(size_t(N));
  for (auto &pt : points) {
    int dim[2];
    for (auto &d : dim) d=int(input->readLong(4));
    pt=MWAWVec2i(dim[0],dim[1]);
    f << pt << ",";
  }
  f << "],";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readZoneFlags(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3036) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZoneFlags: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(GraphZone)[flags" << level << "]:" << header;
  if (header.m_dataSize!=0x24) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZoneFlags: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  for (int i=0; i<18; ++i) { // f0=1,f2=2,f5=700,f6=a,f8=3,f16=f7=-1
    int val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

////////////////////////////////////////////////////////////
// Picture
////////////////////////////////////////////////////////////
bool PowerPoint7Graph::readPictureList(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2006) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureList: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Picture)[list," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  PowerPoint7GraphInternal::Picture picture;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 1027:
      done=readBitmapContainer(level+1,endPos,picture);
      break;
    case 2017: {
      int id;
      done=m_mainParser->readIdentifier(level+1,endPos,id,"Picture");
      if (!done || picture.isEmpty()) break;
      if (m_state->m_idToPictureMap.find(id)!=m_state->m_idToPictureMap.end()) {
        picture=PowerPoint7GraphInternal::Picture();
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureList: a picture %d is already defined\n", id));
        break;
      }
      m_state->m_idToPictureMap.insert(std::map<int, PowerPoint7GraphInternal::Picture>::value_type(id,picture));
      break;
    }
    case 2018:
      done=m_mainParser->readZoneNoData(level+1,endPos,"Picture","id,end");
      break;
    case 4028:
      done=readPictureContainer(level+1,endPos,picture);
      break;
    case 4043: // check me
      done=m_mainParser->readZoneNoData(level+1,endPos,"Picture","flags");
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureList: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureList: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Picture:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readBitmapContainer(int level, long lastPos, PowerPoint7GraphInternal::Picture &picture)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=1027) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmapContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Bitmap)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 2012:
      done=readBitmap(level+1, endPos, picture.m_object, picture.m_box);
      break;
    case 3038:
      done=readBitmapFlag(level+1, endPos);
      break;
    case 4026: {
      int zId; // 130: picture name
      done=m_mainParser->readString(level+1, endPos, picture.m_name, zId, "Bitmap");
      break;
    }
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmapContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmapContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Bitmap:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readBitmap(int level, long lastPos, MWAWEmbeddedObject &object, MWAWBox2i &box)
{
  object=MWAWEmbeddedObject();
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=2012) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmap: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Bitmap)[" << level << "]:" << header;
  if (header.m_dataSize<40) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmap: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  // BITMAPINFOHEADER: normally with size 40, but can probably be longer
  auto headerSz=int(input->readLong(4));
  if (headerSz<40 || headerSz>=header.m_dataSize-16) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmap: find unexpected header size\n"));
    f << "###headerSz=" << headerSz << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(input->tell(),'|');
    if (16+headerSz<header.m_dataSize)
      ascFile.skipZone(pos+16+headerSz, pos+16+header.m_dataSize-1);
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  int dim[2];
  for (auto &d : dim) d=int(input->readULong(4));
  f << "dim=" << MWAWVec2i(dim[0],dim[1]) << ",";
  auto val=int(input->readULong(2));
  if (val!=1) f << "num[planes]=" << val << ",";
  auto nbBytes=int(input->readULong(2));
  f << "nunBytes=" << nbBytes << ",";
  ascFile.addDelimiter(input->tell(),'|');
  input->seek(pos+16+32, librevenge::RVNG_SEEK_SET);
  auto nColors=int(input->readULong(4));
  if (nColors)
    f << "numColors=" << nColors << ",";
  else if (nbBytes<=8) {
    nColors=1;
    for (int b=0; b<=nbBytes; ++b)
      nColors<<=1;
  }
  if ((header.m_dataSize-16-headerSz)/4<=nColors) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmap: can not find the pixel data zone\n"));
    f << "###nColors,";
    ascFile.skipZone(pos+16+headerSz, pos+16+header.m_dataSize-1);
    input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  // ok, let create a bmp file
  box=MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(dim[0],dim[1]));
  unsigned char bmHeader[14];
  bmHeader[0]='B';
  bmHeader[1]='M';
  auto fileSize=uint32_t(14+header.m_dataSize);
  for (int i=0; i<4; ++i, fileSize>>=8)
    bmHeader[i+2]=static_cast<unsigned char>(fileSize&0xFF);
  for (int i=0; i<4; ++i)
    bmHeader[i+6]=0;
  auto dataOffs=uint32_t(14+headerSz+4*nColors);
  for (int i=0; i<4; ++i, dataOffs>>=8)
    bmHeader[i+10]=static_cast<unsigned char>(dataOffs&0xFF);
  librevenge::RVNGBinaryData file(bmHeader,14);
  input->seek(pos+16, librevenge::RVNG_SEEK_SET);

  const unsigned char *readData;
  unsigned long sizeRead;
  if ((readData=input->read(static_cast<unsigned long>(header.m_dataSize), sizeRead)) != nullptr || long(sizeRead)==header.m_dataSize) {
    file.append(readData, sizeRead);
    object.add(file, "image/bmp");
#ifdef DEBUG_WITH_FILES
    static int volatile pictName = 0;
    libmwaw::DebugStream f2;
    f2 << "PICT-" << ++pictName << ".bmp";
    libmwaw::Debug::dumpFile(file, f2.str().c_str());
#endif
  }
  else {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmap: can not reconstruct the final bmp file\n"));
    f << "###";
  }
  ascFile.skipZone(pos+16+headerSz, pos+16+header.m_dataSize-1);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PowerPoint7Graph::readBitmapFlag(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=3038) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmapFlag: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Bitmap)[flag," << level << "]:" << header;
  if (header.m_dataSize!=1) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readBitmapFlag: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    auto val=int(input->readULong(1));
    if (val==1)
      f << "on[disk],";
    else if (val)
      f << "bitmap[type]=" << val << ",";
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readMetaFileContainer(int level, long lastPos, PowerPoint7GraphInternal::Picture &picture)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4037) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFileContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(MetaFile)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4033:
      done=readMetaFile(level+1,endPos,picture.m_object);
      break;
    case 4038:
      done=readMetaFileBox(level+1,endPos,picture.m_box);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFileContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFileContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("MetaFile:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readMetaFile(int level, long lastPos, MWAWEmbeddedObject &object)
{
  object=MWAWEmbeddedObject();
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4033) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFile: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(MetaFile)[" << level << "]:" << header;
  if (header.m_dataSize<10) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFile: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  ascFile.skipZone(input->tell(), pos+16+header.m_dataSize-1);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  librevenge::RVNGBinaryData file;
  input->readDataBlock(header.m_dataSize, file);
  object.add(file, "image/wmf");
#ifdef DEBUG_WITH_FILES
  static int volatile pictName = 0;
  f.str("");
  f << "PICT-" << ++pictName << ".wmf";
  libmwaw::Debug::dumpFile(file, f.str().c_str());
#endif
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  return true;
}

bool PowerPoint7Graph::readMetaFileBox(int level, long lastPos, MWAWBox2i &box)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4038) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFileBox: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(MetaFile)[box," << level << "]:" << header;
  if (header.m_dataSize!=0x14) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readMetaFileBox: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }

  for (int i=0; i<2; ++i) { // f0=f00
    int val=int(input->readLong(2));
    if (val) f << "f" << i << "=" << val << ",";
  }
  int dim[4];
  for (auto &d : dim) d=int(input->readLong(4));
  box=MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3]));
  f << "box=" << box << ",";
  input->seek(pos+16+header.m_dataSize, librevenge::RVNG_SEEK_SET);
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readExternalOleObjectAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4035) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleObjectAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalOleEmbed)[object," << level << "]:" << header;
  if (header.m_dataSize!=0x14) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleObjectAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<10; ++i) { // f0=1, f6=0|1, f8=1|5
      auto val=int(input->readLong(2));
      if (!val) continue;
      f << "f"<< i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readExternalOleEmbed(int level, long lastPos, int &id)
{
  id=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4044) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleEmbed: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalOleEmbed)[list," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4026: {
      std::string string;
      int zId; // 0: Ole name, 2:prog, 3:type, 45: program
      done=m_mainParser->readString(level+1,endPos,string,zId,"ExternalOleEmbed");
      break;
    }
    case 4035:
      done=readExternalOleObjectAtom(level+1,endPos);
      break;
    case 4036:
      done=readPictureId(level+1,endPos,id);
      break;
    case 4045:
      done=readExternalOleEmbedAtom(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleEmbed: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleEmbed: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("ExternalOleEmbed:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readExternalOleEmbedAtom(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4045) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleEmbedAtom: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(ExternalOleEmbed)[atom," << level << "]:" << header;
  if (header.m_dataSize!=0x8) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readExternalOleEmbedAtom: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    for (int i=0; i<4; ++i) { // f0=0|1, f2=1
      auto val=int(input->readLong(2));
      if (!val) continue;
      f << "f"<< i << "=" << val << ",";
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

bool PowerPoint7Graph::readPictureContainer(int level, long lastPos, PowerPoint7GraphInternal::Picture &picture)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4028) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Picture)[container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4037:
      done=readMetaFileContainer(level+1,endPos, picture);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Picture:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readPictureId(int level, long lastPos, int &id)
{
  id=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4036) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureId: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Picture)[id," << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureId: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  id=int(input->readLong(4));
  if (id) f << "id=" << id << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readPictureIdContainer(int level, long lastPos, int &id)
{
  id=-1;
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=4053) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureIdContainer: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Picture)[id,container," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4036:
      done=readPictureId(level+1,endPos,id);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureIdContainer: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readPictureIdContainer: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Picture:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readZone5000(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=5000) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone5000B)[" << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 5001:
      done=readZone5000Header(level+1,endPos);
      break;
    case 5002:
      done=readZone5000Data(level+1,endPos);
      break;
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Zone5000B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

bool PowerPoint7Graph::readZone5000Header(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=5001) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000Header: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone5000B)[header," << level << "]:" << header;
  if (header.m_dataSize!=4) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000Header: find unexpected data size\n"));
    f << "###dataSz=" << header.m_dataSize << ",";
    if (header.m_dataSize)
      ascFile.addDelimiter(pos+16,'|');
    input->seek(header.m_dataSize, librevenge::RVNG_SEEK_CUR);
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return true;
  }
  auto val=int(input->readLong(4));
  if (val!=4) f << "num[data]=" << val << ",";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  return true;
}

bool PowerPoint7Graph::readZone5000Data(int level, long lastPos)
{
  MWAWInputStreamPtr input=m_parserState->m_input;
  long pos=input->tell();

  libmwaw::DebugFile &ascFile = m_parserState->m_asciiFile;
  libmwaw::DebugStream f;
  PowerPoint7Struct::Zone header;
  if (!header.read(input,lastPos) || header.m_type!=5002) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000Data: can not find the zone header\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "Entries(Zone5000B)[data," << level << "]:" << header;
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());

  long endPos=pos+16+header.m_dataSize;
  while (input->tell()<endPos) {
    pos=input->tell();
    auto cType=int(input->readULong(2));
    input->seek(pos, librevenge::RVNG_SEEK_SET);

    bool done=false;
    switch (cType) {
    case 4026: {
      std::string string;
      int zId;
      done=m_mainParser->readString(level+1,endPos,string,zId,"Zone5000B");
      break;
    }
    default:
      done=m_mainParser->readZone(level+1,endPos);
      if (done) {
        MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000: find unexpected zone %d\n", cType));
      }
      break;
    }
    if (done) continue;
    MWAW_DEBUG_MSG(("PowerPoint7Graph::readZone5000: can not read some data\n"));
    ascFile.addPos(pos);
    ascFile.addNote("Zone5000B:###extra");
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    break;
  }
  return true;
}

////////////////////////////////////////////////////////////
// send data
////////////////////////////////////////////////////////////

bool PowerPoint7Graph::sendSlide(PowerPoint7Struct::SlideId const &id, bool sendBackground)
{
  MWAWPresentationListenerPtr listener=m_parserState->m_presentationListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::sendSlide: can not find the listener\n"));
    return false;
  }
  std::vector<int> textIdList;
  if (m_state->m_idToFrameMap.find(id)!=m_state->m_idToFrameMap.end()) {
    std::vector<std::shared_ptr<PowerPoint7GraphInternal::Frame> > const &frames=m_state->m_idToFrameMap.find(id)->second;
    for (auto fram : frames) {
      if (!fram) continue;
      if (!sendBackground && fram->m_isBackground) continue;
      sendFrame(*fram, id.m_isMaster);
    }
  }
  // check if the slide has some note
  if (id.m_isMaster) return true;
  PowerPoint7Struct::SlideId noteId=id;
  noteId.m_inNotes=true;
  if (m_state->m_idToFrameMap.find(noteId)==m_state->m_idToFrameMap.end())
    return true;
  auto const &frames=m_state->m_idToFrameMap.find(noteId)->second;
  for (auto fram : frames) {
    if (!fram) continue;
    if (!sendBackground && fram->m_isBackground) continue;
    fram->getTextZoneList(textIdList);
  }
  if (textIdList.empty())
    return true;

  MWAWPosition pos(MWAWVec2f(0,0), MWAWVec2f(200,200), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  MWAWSubDocumentPtr subdoc(new PowerPoint7GraphInternal::SubDocument(*this, m_parserState->m_input,textIdList));
  listener->insertSlideNote(pos, subdoc);
  return true;
}

bool PowerPoint7Graph::sendFrame(PowerPoint7GraphInternal::Frame const &frame, bool master)
{
  frame.m_isSent=true;
  if (master && frame.m_type==PowerPoint7GraphInternal::Frame::Placeholder)
    return true;
  MWAWListenerPtr listener=m_parserState->m_presentationListener;
  if (!listener) {
    MWAW_DEBUG_MSG(("PowerPoint7Graph::sendFrame: can not find the listener\n"));
    return false;
  }
  MWAWBox2f fBox(1.f/8.f*MWAWVec2f(frame.m_dimension[0]+m_state->m_decal),
                 1.f/8.f*MWAWVec2f(frame.m_dimension[1]+m_state->m_decal));
  if (frame.m_textId>=0) {
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    MWAWSubDocumentPtr subdoc(new PowerPoint7GraphInternal::SubDocument(*this, m_parserState->m_input, frame.m_textId));
    listener->insertTextBox(pos, subdoc, frame.m_style);
    return true;
  }
  if (frame.m_pictureId>=0) {
    if (m_state->m_idToPictureMap.find(frame.m_pictureId)==m_state->m_idToPictureMap.end()) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::sendFrame: can not find the picture %d\n", frame.m_pictureId));
      return false;
    }
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    if (frame.m_isBackground) pos.m_wrapping = MWAWPosition::WBackground;
    listener->insertPicture(pos, m_state->m_idToPictureMap.find(frame.m_pictureId)->second.m_object);
    return true;
  }
  MWAWGraphicShape shape;
  switch (frame.m_type) {
  case PowerPoint7GraphInternal::Frame::Arc: {
    auto const *arc= dynamic_cast<PowerPoint7GraphInternal::FrameArc const *>(&frame);
    if (!arc) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::sendFrame: can not find the arc\n"));
      return false;
    }
    if (!arc->updateShape(fBox, shape)) return false;
    if (frame.m_rotation<0||frame.m_rotation>0)
      shape=shape.rotate(-frame.m_rotation, fBox.center());
    break;
  }
  case PowerPoint7GraphInternal::Frame::Line:
    shape=MWAWGraphicShape::line(fBox[0], fBox[1]);
    break;
  case PowerPoint7GraphInternal::Frame::Group: {
    auto const *group=dynamic_cast<PowerPoint7GraphInternal::FrameGroup const *>(&frame);
    if (!group) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::sendFrame: can not find the group\n"));
      return false;
    }
    if (group->m_child.empty())
      return true;
    MWAWPosition pos(fBox[0], fBox.size(), librevenge::RVNG_POINT);
    pos.m_anchorTo = MWAWPosition::Page;
    listener->openGroup(pos);
    for (auto child : group->m_child) {
      if (!child) continue;
      sendFrame(*child, master);
    }
    listener->closeGroup();
    return true;
  }
  case PowerPoint7GraphInternal::Frame::Polygon: {
    auto const *poly=dynamic_cast<PowerPoint7GraphInternal::FramePolygon const *>(&frame);
    if (!poly) {
      MWAW_DEBUG_MSG(("PowerPoint7Graph::sendFrame: can not find the polygon\n"));
      return false;
    }
    if (!poly->updateShape(fBox, shape)) return false;
    break;
  }
  case PowerPoint7GraphInternal::Frame::Rect:
    if (frame.m_subType>=0) {
      if (!m_state->getCustomShape(frame.m_subType, shape))
        return false;
      if (frame.m_flip[0]||frame.m_flip[1]) {
        shape.translate(MWAWVec2f(-0.5f,-0.5f));
        if (frame.m_flip[0]) shape.scale(MWAWVec2f(-1,1));
        if (frame.m_flip[1]) shape.scale(MWAWVec2f(1,-1));
        shape.translate(MWAWVec2f(0.5f,0.5f));
      }
      shape.scale(fBox.size());
      shape.translate(fBox[0]);
      if (frame.m_rotation<0||frame.m_rotation>0)
        shape=shape.rotate(-frame.m_rotation, fBox.center());
      break;
    }
    switch (frame.m_subType) {
    case -1:
      shape=MWAWGraphicShape::circle(fBox);
      break;
    case -2:
      shape=MWAWGraphicShape::rectangle(fBox, MWAWVec2f(3,3));
      break;
    case -3:
      shape=MWAWGraphicShape::rectangle(fBox);
      break;
    default:
      return false;
    }
    if (frame.m_rotation<0||frame.m_rotation>0)
      shape=shape.rotate(-frame.m_rotation, fBox.center());
    break;
  case PowerPoint7GraphInternal::Frame::Placeholder:
  case PowerPoint7GraphInternal::Frame::Unknown:
#if !defined(__clang__)
  default:
#endif
    return false;
  }

  MWAWBox2f box=shape.getBdBox();
  MWAWPosition pos(box[0], box.size(), librevenge::RVNG_POINT);
  pos.m_anchorTo = MWAWPosition::Page;
  if (frame.m_isBackground) pos.m_wrapping = MWAWPosition::WBackground;
  listener->insertShape(pos, shape, frame.m_style);
  return true;
}

bool PowerPoint7Graph::sendText(int textId)
{
  return m_mainParser->sendText(textId);
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
