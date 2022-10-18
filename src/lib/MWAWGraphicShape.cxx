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

/* This header contains code specific to a pict mac file
 */
#include <string.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include <librevenge/librevenge.h>

#include "libmwaw_internal.hxx"

#include "MWAWGraphicEncoder.hxx"
#include "MWAWGraphicStyle.hxx"

#include "MWAWGraphicShape.hxx"

////////////////////////////////////////////////////////////
// MWAWGraphicShape::PathData
////////////////////////////////////////////////////////////
std::ostream &operator<<(std::ostream &o, MWAWGraphicShape::PathData const &path)
{
  o << path.m_type;
  switch (path.m_type) {
  case 'H':
    o << ":" << path.m_x[0];
    break;
  case 'V':
    o << ":" << path.m_x[1];
    break;
  case 'M':
  case 'L':
  case 'T':
    o << ":" << path.m_x;
    break;
  case 'Q':
  case 'S':
    o << ":" << path.m_x << ":" << path.m_x1;
    break;
  case 'C':
    o << ":" << path.m_x << ":" << path.m_x1 << ":" << path.m_x2;
    break;
  case 'A':
    o << ":" << path.m_x << ":r=" << path.m_r;
    if (path.m_largeAngle) o << ":largeAngle";
    if (path.m_sweep) o << ":sweep";
    if (path.m_rotate<0 || path.m_rotate>0) o << ":rot=" << path.m_rotate;
    break;
  case 'Z':
    break;
  default:
    o << "###";
  }
  return o;
}

void MWAWGraphicShape::PathData::translate(MWAWVec2f const &decal)
{
  if (m_type=='Z')
    return;
  m_x += decal;
  if (m_type=='H' || m_type=='V' || m_type=='M' || m_type=='L' || m_type=='T' || m_type=='A')
    return;
  m_x1 += decal;
  if (m_type=='Q' || m_type=='S')
    return;
  m_x2 += decal;
}

void MWAWGraphicShape::PathData::scale(MWAWVec2f const &scaling)
{
  if (m_type=='Z')
    return;
  m_x = MWAWVec2f(m_x[0]*scaling[0], m_x[1]*scaling[1]);
  if (m_type=='H' || m_type=='V' || m_type=='M' || m_type=='L' || m_type=='T' || m_type=='A')
    return;
  m_x1 = MWAWVec2f(m_x1[0]*scaling[0], m_x1[1]*scaling[1]);
  if (m_type=='Q' || m_type=='S')
    return;
  m_x2 = MWAWVec2f(m_x2[0]*scaling[0], m_x2[1]*scaling[1]);
}

void MWAWGraphicShape::PathData::rotate(float angle, MWAWVec2f const &decal)
{
  if (m_type=='Z')
    return;
  float angl=angle*float(M_PI/180.);
  m_x = MWAWVec2f(std::cos(angl)*m_x[0]-std::sin(angl)*m_x[1],
                  std::sin(angl)*m_x[0]+std::cos(angl)*m_x[1])+decal;
  if (m_type=='A') {
    m_rotate += angle;
    return;
  }
  if (m_type=='H' || m_type=='V' || m_type=='M' || m_type=='L' || m_type=='T')
    return;
  m_x1 = MWAWVec2f(std::cos(angl)*m_x1[0]-std::sin(angl)*m_x1[1],
                   std::sin(angl)*m_x1[0]+std::cos(angl)*m_x1[1])+decal;
  if (m_type=='Q' || m_type=='S')
    return;
  m_x2 = MWAWVec2f(std::cos(angl)*m_x2[0]-std::sin(angl)*m_x2[1],
                   std::sin(angl)*m_x2[0]+std::cos(angl)*m_x2[1])+decal;
}

void MWAWGraphicShape::PathData::transform(MWAWTransformation const &matrix, float rotation)
{
  if (m_type=='Z')
    return;
  m_x = matrix*m_x;
  if (m_type=='A') {
    m_rotate += rotation;
    return;
  }
  if (m_type=='H' || m_type=='V' || m_type=='M' || m_type=='L' || m_type=='T')
    return;
  m_x1 = matrix*m_x1;
  if (m_type=='Q' || m_type=='S')
    return;
  m_x2 = matrix*m_x2;
}

bool MWAWGraphicShape::PathData::get(librevenge::RVNGPropertyList &list, MWAWVec2f const &orig) const
{
  list.clear();
  std::string type("");
  type += m_type;
  list.insert("librevenge:path-action", type.c_str());
  if (m_type=='Z')
    return true;
  if (m_type=='H') {
    list.insert("svg:x",double(m_x[0]-orig[0]), librevenge::RVNG_POINT);
    return true;
  }
  if (m_type=='V') {
    list.insert("svg:y",double(m_x[1]-orig[1]), librevenge::RVNG_POINT);
    return true;
  }
  list.insert("svg:x",double(m_x[0]-orig[0]), librevenge::RVNG_POINT);
  list.insert("svg:y",double(m_x[1]-orig[1]), librevenge::RVNG_POINT);
  if (m_type=='M' || m_type=='L' || m_type=='T')
    return true;
  if (m_type=='A') {
    list.insert("svg:rx",double(m_r[0]), librevenge::RVNG_POINT);
    list.insert("svg:ry",double(m_r[1]), librevenge::RVNG_POINT);
    list.insert("librevenge:large-arc", m_largeAngle);
    list.insert("librevenge:sweep", m_sweep);
    list.insert("librevenge:rotate", double(m_rotate), librevenge::RVNG_GENERIC);
    return true;
  }
  list.insert("svg:x1",double(m_x1[0]-orig[0]), librevenge::RVNG_POINT);
  list.insert("svg:y1",double(m_x1[1]-orig[1]), librevenge::RVNG_POINT);
  if (m_type=='Q' || m_type=='S')
    return true;
  list.insert("svg:x2",double(m_x2[0]-orig[0]), librevenge::RVNG_POINT);
  list.insert("svg:y2",double(m_x2[1]-orig[1]), librevenge::RVNG_POINT);
  if (m_type=='C')
    return true;
  MWAW_DEBUG_MSG(("MWAWGraphicShape::PathData::get: unknown command %c\n", m_type));
  list.clear();
  return false;
}

int MWAWGraphicShape::PathData::cmp(MWAWGraphicShape::PathData const &a) const
{
  if (m_type < a.m_type) return 1;
  if (m_type > a.m_type) return 1;
  int diff = m_x.cmp(a.m_x);
  if (diff) return diff;
  diff = m_x1.cmp(a.m_x1);
  if (diff) return diff;
  diff = m_x2.cmp(a.m_x2);
  if (diff) return diff;
  diff = m_r.cmp(a.m_r);
  if (diff) return diff;
  if (m_rotate < a.m_rotate) return 1;
  if (m_rotate > a.m_rotate) return -1;
  if (m_largeAngle != a.m_largeAngle)
    return m_largeAngle ? 1 : -1;
  if (m_sweep != a.m_sweep)
    return m_sweep ? 1 : -1;
  return 0;
}

////////////////////////////////////////////////////////////
// MWAWGraphicShape
////////////////////////////////////////////////////////////
MWAWGraphicShape::~MWAWGraphicShape()
{
}

MWAWGraphicShape MWAWGraphicShape::line(MWAWVec2f const &orig, MWAWVec2f const &dest)
{
  MWAWGraphicShape res;
  res.m_type = MWAWGraphicShape::Line;
  res.m_vertices.resize(2);
  res.m_vertices[0]=orig;
  res.m_vertices[1]=dest;
  MWAWVec2f minPt(orig), maxPt(orig);
  for (int c=0; c<2; ++c) {
    if (orig[c] < dest[c])
      maxPt[c]=dest[c];
    else
      minPt[c]=dest[c];
  }
  res.m_bdBox=MWAWBox2f(minPt,maxPt);
  return res;
}

MWAWGraphicShape MWAWGraphicShape::measure(MWAWVec2f const &orig, MWAWVec2f const &dest)
{
  MWAWGraphicShape res=line(orig,dest);
  res.m_type= MWAWGraphicShape::Measure;
  return res;
}

std::ostream &operator<<(std::ostream &o, MWAWGraphicShape const &sh)
{
  o << "box=" << sh.m_bdBox << ",";
  switch (sh.m_type) {
  case MWAWGraphicShape::Line:
    o << "line,";
    if (sh.m_vertices.size()!=2)
      o << "###pts,";
    else
      o << "pts=" << sh.m_vertices[0] << "<->" << sh.m_vertices[1] << ",";
    break;
  case MWAWGraphicShape::Measure:
    o << "measure,";
    if (sh.m_vertices.size()!=2)
      o << "###pts,";
    else
      o << "pts=" << sh.m_vertices[0] << "<->" << sh.m_vertices[1] << ",";
    break;
  case MWAWGraphicShape::Rectangle:
    o << "rect,";
    if (sh.m_formBox!=sh.m_bdBox)
      o << "box[rect]=" << sh.m_formBox << ",";
    if (sh.m_cornerWidth!=MWAWVec2f(0,0))
      o << "corners=" << sh.m_cornerWidth << ",";
    break;
  case MWAWGraphicShape::Circle:
    o << "circle,";
    break;
  case MWAWGraphicShape::Arc:
  case MWAWGraphicShape::Pie:
    o << (sh.m_type == MWAWGraphicShape::Arc ? "arc," : "pie,");
    o << "box[ellipse]=" << sh.m_formBox << ",";
    o << "angle=" << sh.m_arcAngles << ",";
    break;
  case MWAWGraphicShape::Polygon:
  case MWAWGraphicShape::Polyline:
    if (sh.m_type==MWAWGraphicShape::Polygon)
      o << "polygon,pts=[";
    else
      o << "polyline,pts=[";
    for (auto const &pt : sh.m_vertices)
      o << pt << ",";
    o << "],";
    break;
  case MWAWGraphicShape::Path:
    o << "path,pts=[";
    for (auto const &pt : sh.m_path)
      o << pt << ",";
    o << "],";
    break;
  case MWAWGraphicShape::ShapeUnknown:
#if !defined(__clang__)
  default:
#endif
    o << "###unknown[shape],";
    break;
  }
  o << sh.m_extra;
  return o;
}

int MWAWGraphicShape::cmp(MWAWGraphicShape const &a) const
{
  if (m_type < a.m_type) return 1;
  if (m_type > a.m_type) return -1;
  if (m_bdBox < a.m_bdBox) return 1;
  if (m_bdBox > a.m_bdBox) return -1;
  if (m_formBox < a.m_formBox) return 1;
  if (m_formBox > a.m_formBox) return -1;
  int diff = m_cornerWidth.cmp(a.m_cornerWidth);
  if (diff) return diff;
  diff = m_arcAngles.cmp(a.m_arcAngles);
  if (diff) return diff;
  if (m_vertices.size()<a.m_vertices.size()) return 1;
  if (m_vertices.size()>a.m_vertices.size()) return -1;
  for (size_t pt=0; pt < m_vertices.size(); ++pt) {
    diff = m_vertices[pt].cmp(a.m_vertices[pt]);
    if (diff) return diff;
  }
  if (m_path.size()<a.m_path.size()) return 1;
  if (m_path.size()>a.m_path.size()) return -1;
  for (size_t pt=0; pt < m_path.size(); ++pt) {
    diff = m_path[pt].cmp(a.m_path[pt]);
    if (diff) return diff;
  }
  return 0;
}

MWAWBox2f MWAWGraphicShape::getBdBox(MWAWGraphicStyle const &style, bool moveToO) const
{
  MWAWBox2f bdBox=m_bdBox;
  if (moveToO)
    bdBox=MWAWBox2f(MWAWVec2f(0,0),m_bdBox.size());
  if (style.hasLine())
    bdBox.extend(style.m_lineWidth/2.f);
  if (m_type==Line) {
    // fixme: add 4pt for each arrows
    int numArrows=(style.m_arrows[0].isEmpty() ? 0 : 1)+(style.m_arrows[1].isEmpty() ? 0 : 1);
    if (numArrows) bdBox.extend(float(2*numArrows));
  }
  return bdBox;
}

void MWAWGraphicShape::translate(MWAWVec2f const &decal)
{
  if (decal==MWAWVec2f(0,0))
    return;
  m_bdBox=MWAWBox2f(m_bdBox.min()+decal, m_bdBox.max()+decal);
  m_formBox=MWAWBox2f(m_formBox.min()+decal, m_formBox.max()+decal);
  for (auto &pt : m_vertices)
    pt+=decal;
  for (auto &pt : m_path)
    pt.translate(decal);
}

void MWAWGraphicShape::scale(MWAWVec2f const &scaling)
{
  // checkme: does not work for symetry if shape is an arc...
  m_bdBox=MWAWBox2f(MWAWVec2f(scaling[0]*m_bdBox.min()[0],scaling[1]*m_bdBox.min()[1]),
                    MWAWVec2f(scaling[0]*m_bdBox.max()[0],scaling[1]*m_bdBox.max()[1]));
  m_formBox=MWAWBox2f(MWAWVec2f(scaling[0]*m_formBox.min()[0],scaling[1]*m_formBox.min()[1]),
                      MWAWVec2f(scaling[0]*m_formBox.max()[0],scaling[1]*m_formBox.max()[1]));
  for (auto &pt : m_vertices)
    pt=MWAWVec2f(scaling[0]*pt[0], scaling[1]*pt[1]);
  for (auto &pt : m_path)
    pt.scale(scaling);
}

MWAWGraphicShape MWAWGraphicShape::rotate(float angle, MWAWVec2f const &center) const
{
  while (angle >= 360) angle -= 360;
  while (angle <= -360) angle += 360;
  if (angle >= -1.e-3f && angle <= 1.e-3f) return *this;
  float angl=angle*float(M_PI/180.);
  MWAWVec2f decal=center-MWAWVec2f(std::cos(angl)*center[0]-std::sin(angl)*center[1],
                                   std::sin(angl)*center[0]+std::cos(angl)*center[1]);
  MWAWBox2f fBox;
  for (int i=0; i < 4; ++i) {
    MWAWVec2f pt=MWAWVec2f(m_bdBox[i%2][0],m_bdBox[i/2][1]);
    pt = MWAWVec2f(std::cos(angl)*pt[0]-std::sin(angl)*pt[1],
                   std::sin(angl)*pt[0]+std::cos(angl)*pt[1])+decal;
    if (i==0) fBox=MWAWBox2f(pt,pt);
    else fBox=fBox.getUnion(MWAWBox2f(pt,pt));
  }
  MWAWGraphicShape res = path(fBox);
  res.m_path=getPath(false);
  for (auto &pt : res.m_path)
    pt.rotate(angle, decal);
  return res;
}

MWAWGraphicShape MWAWGraphicShape::transform(MWAWTransformation const &matrix) const
{
  if (matrix.isIdentity()) return *this;
  if (matrix[0][1]<=0 && matrix[0][1]>=0 && matrix[1][0]<=0 && matrix[1][0]>=0) {
    MWAWGraphicShape res=*this;
    if (matrix[0][0]<1 || matrix[0][0]>1 || matrix[1][1]<1 || matrix[1][1]>1)
      res.scale(MWAWVec2f(matrix[0][0], matrix[1][1]));
    res.translate(MWAWVec2f(matrix[0][2],matrix[1][2]));
    return res;
  }

  MWAWBox2f fBox;
  for (int i=0; i < 4; ++i) {
    MWAWVec2f pt = matrix*MWAWVec2f(m_bdBox[i%2][0],m_bdBox[i/2][1]);
    if (i==0) fBox=MWAWBox2f(pt,pt);
    else fBox=fBox.getUnion(MWAWBox2f(pt,pt));
  }
  MWAWGraphicShape res = path(fBox);
  res.m_path=getPath(true);

  MWAWTransformation transf;
  float rotation=0;
  MWAWVec2f shearing;
  if (!matrix.decompose(rotation,shearing,transf,fBox.center()))
    rotation=0;
  for (auto &pt : res.m_path)
    pt.transform(matrix, rotation);
  return res;
}

bool MWAWGraphicShape::addPathTo(MWAWVec2f const &orig, librevenge::RVNGPropertyListVector &vect) const
{
  MWAWVec2f decal=orig-m_bdBox[0];
  std::vector<MWAWGraphicShape::PathData> fPath=getPath(false);
  size_t n=fPath.size();
  if (!n) {
    MWAW_DEBUG_MSG(("MWAWGraphicShape::addPathTo: can not find the path\n"));
    return false;
  }
  librevenge::RVNGPropertyList list;
  for (auto const &pt : fPath) {
    list.clear();
    if (pt.get(list, -1.0f*decal))
      vect.append(list);
  }
  if (fPath[n-1].m_type != 'Z') {
    // odg need a closed path to draw surface, so ...
    list.clear();
    list.insert("librevenge:path-action", "Z");
    vect.append(list);
  }
  return true;
}

MWAWGraphicShape::Command MWAWGraphicShape::addTo(MWAWVec2f const &orig, bool asSurface, librevenge::RVNGPropertyList &propList) const
{
  MWAWVec2f pt;
  librevenge::RVNGPropertyList list;
  librevenge::RVNGPropertyListVector vect;
  MWAWVec2f decal=orig-m_bdBox[0];
  switch (m_type) {
  case Line:
  case Measure:
    if (m_vertices.size()!=2) break;
    if (m_type==Measure)
      propList.insert("draw:show-unit", true);
    pt=m_vertices[0]+decal;
    list.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
    list.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
    vect.append(list);
    pt=m_vertices[1]+decal;
    list.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
    list.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
    vect.append(list);
    propList.insert("svg:points", vect);
    return C_Polyline;
  case Rectangle:
    if (m_cornerWidth[0] > 0 && m_cornerWidth[1] > 0) {
      propList.insert("svg:rx",double(m_cornerWidth[0]), librevenge::RVNG_POINT);
      propList.insert("svg:ry",double(m_cornerWidth[1]), librevenge::RVNG_POINT);
    }
    pt=m_formBox[0]+decal;
    propList.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
    propList.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
    pt=m_formBox.size();
    propList.insert("svg:width",double(pt.x()), librevenge::RVNG_POINT);
    propList.insert("svg:height",double(pt.y()), librevenge::RVNG_POINT);
    return C_Rectangle;
  case Circle:
    pt=0.5f*(m_formBox[0]+m_formBox[1])+decal;
    propList.insert("svg:cx",double(pt.x()), librevenge::RVNG_POINT);
    propList.insert("svg:cy",double(pt.y()), librevenge::RVNG_POINT);
    pt=0.5f*(m_formBox[1]-m_formBox[0]);
    propList.insert("svg:rx",double(pt.x()), librevenge::RVNG_POINT);
    propList.insert("svg:ry",double(pt.y()), librevenge::RVNG_POINT);
    return C_Ellipse;
  case Arc:
  case Pie: {
    MWAWVec2f center=0.5f*(m_formBox[0]+m_formBox[1])+decal;
    MWAWVec2f rad=0.5f*(m_formBox[1]-m_formBox[0]);
    float angl0=m_arcAngles[0];
    float angl1=m_arcAngles[1];
    if (rad[1]<0) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("MWAWGraphicShape::addTo: oops radiusY for arc is negative, inverse it\n"));
        first=false;
      }
      rad[1]=-rad[1];
    }
    while (angl1<angl0)
      angl1+=360.f;
    while (angl1>angl0+360.f)
      angl1-=360.f;
    if (angl1-angl0>=180.f && angl1-angl0<=180.f)
      angl1+=0.01f;
    float angl=angl0*float(M_PI/180.);
    bool addCenter=m_type==Pie && asSurface;
    if (addCenter) {
      pt=center;
      list.insert("librevenge:path-action", "M");
      list.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
      list.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
      vect.append(list);
    }
    list.clear();
    pt=center+MWAWVec2f(std::cos(angl)*rad[0],-std::sin(angl)*rad[1]);
    list.insert("librevenge:path-action", addCenter ? "L" : "M");
    list.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
    list.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
    vect.append(list);

    list.clear();
    angl=angl1*float(M_PI/180.);
    pt=center+MWAWVec2f(std::cos(angl)*rad[0],-std::sin(angl)*rad[1]);
    list.insert("librevenge:path-action", "A");
    list.insert("librevenge:large-arc", !(angl1-angl0<180.f));
    list.insert("librevenge:sweep", false);
    list.insert("svg:rx",double(rad.x()), librevenge::RVNG_POINT);
    list.insert("svg:ry",double(rad.y()), librevenge::RVNG_POINT);
    list.insert("svg:x",double(pt.x()), librevenge::RVNG_POINT);
    list.insert("svg:y",double(pt.y()), librevenge::RVNG_POINT);
    vect.append(list);
    if (asSurface) {
      list.clear();
      list.insert("librevenge:path-action", "Z");
      vect.append(list);
    }

    propList.insert("svg:d", vect);
    return C_Path;
  }
  case Polygon:
  case Polyline: {
    size_t n=m_vertices.size();
    if (n<2) break;
    for (auto point : m_vertices) {
      list.clear();
      point += decal;
      list.insert("svg:x", double(point.x()), librevenge::RVNG_POINT);
      list.insert("svg:y", double(point.y()), librevenge::RVNG_POINT);
      vect.append(list);
    }
    propList.insert("svg:points", vect);
    return (asSurface && m_type==Polygon) ? C_Polygon : C_Polyline;
  }
  case Path: {
    size_t n=m_path.size();
    if (!n) break;
    for (auto const &point : m_path) {
      list.clear();
      if (point.get(list, -1.0f*decal))
        vect.append(list);
    }
    if (asSurface && m_path[n-1].m_type != 'Z') {
      // odg need a closed path to draw surface, so ...
      list.clear();
      list.insert("librevenge:path-action", "Z");
      vect.append(list);
    }
    propList.insert("svg:d", vect);
    return C_Path;
  }
  case ShapeUnknown:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  MWAW_DEBUG_MSG(("MWAWGraphicShape::addTo: can not send a shape with type=%d\n", int(m_type)));
  return C_Bad;
}

std::vector<MWAWGraphicShape::PathData> MWAWGraphicShape::getPath(bool forTransformation) const
{
  std::vector<MWAWGraphicShape::PathData> res;
  float const delta=0.55228f;
  switch (m_type) {
  case Measure:
    MWAW_DEBUG_MSG(("MWAWGraphicShape::getPath: called on a measure, transform it in line\n"));
    MWAW_FALLTHROUGH;
  case Line:
  case Polygon:
  case Polyline: {
    size_t n=m_vertices.size();
    if (n<2) break;
    res.push_back(PathData('M',m_vertices[0]));
    for (size_t i = 1; i < n; ++i)
      res.push_back(PathData('L', m_vertices[i]));
    break;
  }
  case Rectangle:
    if (m_cornerWidth[0] > 0 && m_cornerWidth[1] > 0) {
      MWAWBox2f box=m_formBox;
      if (box.min()[0]>box.max()[0]) std::swap(box.min()[0],box.max()[0]);
      if (box.min()[1]>box.max()[1]) std::swap(box.min()[1],box.max()[1]);
      MWAWVec2f c=m_cornerWidth;
      if (2*c[0]>box.size()[0]) c[0]=0.5f*box.size()[0];
      if (2*c[1]>box.size()[1]) c[1]=0.5f*box.size()[1];
      if (forTransformation) {
        MWAWVec2f pt0(box[1][0]-c[0],box[0][1]);
        res.push_back(PathData('M',pt0));
        MWAWVec2f pt1(box[1][0],box[0][1]+c[1]);
        res.push_back(PathData('C',pt1,pt0+MWAWVec2f(delta*c[0],0),pt1-MWAWVec2f(0,delta*c[1])));
        pt0=MWAWVec2f(box[1][0],box[1][1]-c[1]);
        res.push_back(PathData('L',pt0));
        pt1=MWAWVec2f(box[1][0]-c[0],box[1][1]);
        res.push_back(PathData('C',pt1,pt0+MWAWVec2f(0,delta*c[1]),pt1+MWAWVec2f(delta*c[0],0)));
        pt0=MWAWVec2f(box[0][0]+c[0],box[1][1]);
        res.push_back(PathData('L',pt0));
        pt1=MWAWVec2f(box[0][0],box[1][1]-c[1]);
        res.push_back(PathData('C',pt1,pt0-MWAWVec2f(delta*c[0],0),pt1+MWAWVec2f(0,delta*c[1])));
        pt0=MWAWVec2f(box[0][0],box[0][1]+c[1]);
        res.push_back(PathData('L',pt0));
        pt1=MWAWVec2f(box[0][0]+c[0],box[0][1]);
        res.push_back(PathData('C',pt1,pt0-MWAWVec2f(0,delta*c[1]),pt1-MWAWVec2f(delta*c[0],0)));
      }
      else {
        res.push_back(PathData('M',MWAWVec2f(box[1][0]-c[0],box[0][1])));
        PathData data('A',MWAWVec2f(box[1][0],box[0][1]+c[1]));
        data.m_r=c;
        data.m_sweep=true;
        res.push_back(data);
        res.push_back(PathData('L',MWAWVec2f(box[1][0],box[1][1]-c[1])));
        data.m_x=MWAWVec2f(box[1][0]-c[0],box[1][1]);
        res.push_back(data);
        res.push_back(PathData('L',MWAWVec2f(box[0][0]+c[0],box[1][1])));
        data.m_x=MWAWVec2f(box[0][0],box[1][1]-c[1]);
        res.push_back(data);
        res.push_back(PathData('L',MWAWVec2f(box[0][0],box[0][1]+c[1])));
        data.m_x=MWAWVec2f(box[0][0]+c[0],box[0][1]);
        res.push_back(data);
      }
      res.push_back(PathData('Z'));
      break;
    }
    res.push_back(PathData('M',m_formBox[0]));
    res.push_back(PathData('L',MWAWVec2f(m_formBox[0][0],m_formBox[1][1])));
    res.push_back(PathData('L',m_formBox[1]));
    res.push_back(PathData('L',MWAWVec2f(m_formBox[1][0],m_formBox[0][1])));
    res.push_back(PathData('Z'));
    break;
  case Circle: {
    if (forTransformation) {
      MWAWVec2f center=m_formBox.center();
      MWAWVec2f dir=0.5f*delta*(m_formBox[1]-m_formBox[0]);
      MWAWVec2f pt0(m_formBox[0][0],center[1]);
      res.push_back(PathData('M',pt0));
      MWAWVec2f pt1(center[0],m_formBox[0][1]);
      res.push_back(PathData('C',pt1, pt0-MWAWVec2f(0,dir[1]), pt1-MWAWVec2f(dir[0],0)));
      pt0=MWAWVec2f(m_formBox[1][0],center[1]);
      res.push_back(PathData('C',pt0, pt1+MWAWVec2f(dir[0],0), pt0-MWAWVec2f(0,dir[1])));
      pt1=MWAWVec2f(center[0],m_formBox[1][1]);
      res.push_back(PathData('C',pt1, pt0+MWAWVec2f(0,dir[1]), pt1+MWAWVec2f(dir[0],0)));
      pt0=MWAWVec2f(m_formBox[0][0],center[1]);
      res.push_back(PathData('C',pt0, pt1-MWAWVec2f(dir[0],0), pt0+MWAWVec2f(0,dir[1])));
      res.push_back(PathData('Z'));
    }
    else {
      MWAWVec2f pt0 = MWAWVec2f(m_formBox[0][0],0.5f*(m_formBox[0][1]+m_formBox[1][1]));
      MWAWVec2f pt1 = MWAWVec2f(m_formBox[1][0],pt0[1]);
      res.push_back(PathData('M',pt0));
      PathData data('A',pt1);
      data.m_r=0.5f*(m_formBox[1]-m_formBox[0]);
      data.m_largeAngle=true;
      res.push_back(data);
      data.m_x=pt0;
      res.push_back(data);
    }
    break;
  }
  case Arc:
  case Pie: {
    MWAWVec2f center=0.5f*(m_formBox[0]+m_formBox[1]);
    MWAWVec2f rad=0.5f*(m_formBox[1]-m_formBox[0]);
    float angl0=m_arcAngles[0];
    float angl1=m_arcAngles[1];
    if (rad[1]<0) {
      static bool first=true;
      if (first) {
        MWAW_DEBUG_MSG(("MWAWGraphicShape::getPath: oops radiusY for arc is negative, inverse it\n"));
        first=false;
      }
      rad[1]=-rad[1];
    }
    while (angl1<angl0)
      angl1+=360.f;
    while (angl1>angl0+360.f)
      angl1-=360.f;
    if (angl1-angl0>=180.f && angl1-angl0<=180.f)
      angl1+=0.01f;
    float angl=angl0*float(M_PI/180.);
    bool addCenter=m_type==Pie;
    if (addCenter)
      res.push_back(PathData('M', center));
    MWAWVec2f pt=center+MWAWVec2f(std::cos(angl)*rad[0],-std::sin(angl)*rad[1]);
    res.push_back(PathData(addCenter ? 'L' : 'M', pt));
    if (!forTransformation) {
      angl=angl1*float(M_PI/180.);
      pt=center+MWAWVec2f(std::cos(angl)*rad[0],-std::sin(angl)*rad[1]);
      PathData data('A',pt);
      data.m_largeAngle=(angl1-angl0>=180.f);
      data.m_r=rad;
      res.push_back(data);
    }
    else {
      int N=int(angl1-angl0)/90;
      float dAngle=float(angl1-angl0)/float(N+1);
      for (int i=0; i<=N; ++i) {
        float newAngl= i==N ? angl1 : angl0+float(i+1)*dAngle;
        newAngl*=float(M_PI/180.);
        MWAWVec2f newPt=center+MWAWVec2f(std::cos(angl1)*rad[0],-std::sin(angl1)*rad[1]);
        MWAWVec2f dir(-std::sin(angl)*rad[0],-std::cos(angl)*rad[1]);
        MWAWVec2f newDir(-std::sin(newAngl)*rad[0],-std::cos(newAngl)*rad[1]);
        float deltaDir=4/3.f*std::tan((newAngl-angl)/4);
        res.push_back(PathData('C',newPt,pt+deltaDir*dir,newPt-deltaDir*newDir));
        pt=newPt;
        angl=newAngl;
      }
      if (m_type==Pie) res.push_back(PathData('Z'));
    }
    break;
  }
  case Path:
    return m_path;
  case ShapeUnknown:
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("MWAWGraphicShape::getPath: unexpected type\n"));
    break;
  }
  return res;
}

std::vector<MWAWGraphicShape::PathData> MWAWGraphicShape::offsetVertices(std::vector<MWAWGraphicShape::PathData> const &path, float offset, MWAWBox2f &finalBox)
{
  size_t N=path.size();

  MWAWVec2f prevPoint(0,0);
  std::vector<MWAWVec2f> listPoints;
  listPoints.reserve(N);
  size_t begComponent=0;
  std::vector<size_t> componentsSize;
  for (size_t i=0; i<N; ++i) {
    auto const &p=path[i];
    if (p.m_type=='Z') {
      listPoints.push_back(prevPoint);
      componentsSize.push_back(i+1-begComponent);
      begComponent=i+1;
      continue;
    }
    auto pt=p.m_x;
    if (p.m_type=='H')
      pt[1]=prevPoint[1];
    else if (p.m_type=='V')
      pt[0]=prevPoint[0];
    listPoints.push_back(pt);
    prevPoint=pt;
  }
  if (begComponent!=N)
    componentsSize.push_back(N-begComponent);

  std::vector<MWAWGraphicShape::PathData> res;
  res.reserve(N);

  size_t first=0;
  bool firstPtInBox=true;
  for (auto n : componentsSize) {
    if (!n) continue;
    bool const endZ=path[first+n-1].m_type=='Z';
    size_t nPt=endZ ? n-1 : n;
    if (nPt<=1) continue;

    std::vector<MWAWVec2f> decal;
    decal.reserve(nPt);
    bool const endEquiv = endZ ? listPoints[first] == listPoints[first+nPt-1] : false;
    size_t nMod=endEquiv ? nPt-1 : nPt;
    for (size_t i=0; i<nMod; ++i) {
      MWAWVec2f dir=listPoints[first+((i+1!=nMod || endZ) ? (i+1)%nMod : i)] - listPoints[first+(i+nMod-1)%nMod];
      if (dir==MWAWVec2f(0,0))
        dir=listPoints[first+(i+1==nMod ? i : i+1)] - listPoints[first+(i+1==nMod ? i-1 : i)];
      float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]);
      if (len<=0) {
        decal.push_back(MWAWVec2f(0,0));
        continue;
      }
      decal.push_back(MWAWVec2f(-dir[1]*offset/len, dir[0]*offset/len));
    }
    for (size_t i=0; i<nMod; ++i) {
      if (firstPtInBox) {
        finalBox=MWAWBox2f(listPoints[first+i]+decal[i], listPoints[first+i]+decal[i]);
        firstPtInBox=false;
      }
      else
        finalBox=finalBox.getUnion(MWAWBox2f(listPoints[first+i]+decal[i], listPoints[first+i]+decal[i]));
    }
    if (endEquiv) decal.push_back(decal[0]);


    for (size_t i=0; i<n; ++i) {
      auto p=path[first+i];
      if (p.m_type=='Z') {
        res.push_back(p);
        continue;
      }
      p.m_x+=decal[i];
      if (p.m_type=='H' || p.m_type=='V' || p.m_type=='M' || p.m_type=='L' || p.m_type=='T' || p.m_type=='A') {
        res.push_back(p);
        continue;
      }
      if (p.m_type=='Q') {
        p.m_x1 += 0.5f*(decal[i]+decal[(i+nPt-1)%nPt]);
        res.push_back(p);
        continue;
      }
      if (p.m_type=='S') {
        p.m_x1 += decal[i];
        res.push_back(p);
        continue;
      }
      p.m_x1 += decal[(i+nPt-1)%nPt];
      p.m_x2 += decal[i];
      res.push_back(p);
    }

    first+=n;
  }
  return res;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:

