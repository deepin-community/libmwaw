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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <librevenge/librevenge.h>
#include <libmwaw/libmwaw.hxx>

#include "libmwaw_internal.hxx"

#include "MWAWFontConverter.hxx"
#include "MWAWPictBitmap.hxx"

#include "MWAWGraphicStyle.hxx"

////////////////////////////////////////////////////////////
// arrow
////////////////////////////////////////////////////////////
void MWAWGraphicStyle::Arrow::addTo(librevenge::RVNGPropertyList &propList, std::string const &type) const
{
  if (isEmpty())
    return;
  if (type!="start" && type!="end") {
    MWAW_DEBUG_MSG(("MWAWGraphicStyle::Arrow::addTo: oops, find unexpected type\n"));
    return;
  }
  std::stringstream s, s2;
  s << "draw:marker-" << type << "-path";
  propList.insert(s.str().c_str(), m_path.c_str());
  s.str("");
  s << "draw:marker-" << type << "-viewbox";
  s2 << m_viewBox[0][0] << " " << m_viewBox[0][1] << " " << m_viewBox[1][0] << " " << m_viewBox[1][1];
  propList.insert(s.str().c_str(), s2.str().c_str());
  s.str("");
  s << "draw:marker-" << type << "-center";
  propList.insert(s.str().c_str(), m_isCentered);
  s.str("");
  s << "draw:marker-" << type << "-width";
  propList.insert(s.str().c_str(), double(m_width), librevenge::RVNG_POINT);
}

////////////////////////////////////////////////////////////
// pattern
////////////////////////////////////////////////////////////
MWAWGraphicStyle::Pattern::~Pattern()
{
}

bool MWAWGraphicStyle::Pattern::getUniqueColor(MWAWColor &col) const
{
  if (empty() || !m_picture.isEmpty() || m_data.empty()) return false;
  if (m_colors[0]==m_colors[1]) {
    col = m_colors[0];
    return true;
  }
  unsigned char def=m_data[0];
  if (def!=0 && def!=0xFF) return false;
  for (size_t c=1; c < m_data.size(); ++c)
    if (m_data[c]!=def) return false;
  col = m_colors[def ? 1 : 0];
  return true;
}

bool MWAWGraphicStyle::Pattern::getAverageColor(MWAWColor &color) const
{
  if (empty()) return false;
  if (!m_picture.isEmpty()) {
    color=m_pictureAverageColor;
    return true;
  }
  if (m_data.empty()) return false;
  if (m_colors[0]==m_colors[1]) {
    color = m_colors[0];
    return true;
  }
  int numOne=0, numZero=0;
  for (auto data : m_data) {
    for (int depl=1, b=0; b < 8; ++b, depl*=2) {
      if (data & depl)
        numOne++;
      else
        numZero++;
    }
  }
  if (!numOne && !numZero) return false;
  float percent=float(numOne)/float(numOne+numZero);
  color = MWAWColor::barycenter(1.f-percent,m_colors[0],percent,m_colors[1]);
  return true;
}

bool MWAWGraphicStyle::Pattern::getBinary(MWAWEmbeddedObject &picture) const
{
  if (empty()) {
    MWAW_DEBUG_MSG(("MWAWGraphicStyle::Pattern::getBinary: called on invalid pattern\n"));
    return false;
  }
  if (!m_picture.isEmpty()) {
    picture=m_picture;
    return true;
  }
  /* We create a indexed bitmap to obtain a final binary data.

     But it will probably better to recode that differently
   */
  MWAWPictBitmapIndexed bitmap(m_dim);
  std::vector<MWAWColor> colors;
  for (auto color : m_colors)
    colors.push_back(color);
  bitmap.setColors(colors);
  int numBytesByLines = m_dim[0]/8;
  unsigned char const *ptr = &m_data[0];
  std::vector<int> rowValues(static_cast<size_t>(m_dim[0]));
  for (int h=0; h < m_dim[1]; ++h) {
    size_t i=0;
    for (int b=0; b < numBytesByLines; ++b) {
      unsigned char c=*(ptr++);
      unsigned char depl=0x80;
      for (int byt=0; byt<8; ++byt) {
        rowValues[i++] = (c&depl) ? 1 : 0;
        depl=static_cast<unsigned char>(depl>>1);
      }
    }
    bitmap.setRow(h, &rowValues[0]);
  }
  return bitmap.getBinary(picture);
}

////////////////////////////////////////////////////////////
// gradient
////////////////////////////////////////////////////////////
bool MWAWGraphicStyle::Gradient::getAverageColor(MWAWColor &color) const
{
  if (m_stopList.empty())
    return false;
  if (m_stopList.size()==1) {
    color=m_stopList[0].m_color;
    return true;
  }
  // fixme: check the offset are sorted and use then to compute a better barycenter
  unsigned n[]= {0,0,0,0};
  for (auto const &st : m_stopList) {
    n[0]+=st.m_color.getRed();
    n[1]+=st.m_color.getGreen();
    n[2]+=st.m_color.getBlue();
    n[3]+=st.m_color.getAlpha();
  }
  color=MWAWColor((unsigned char)(n[0]/(unsigned)(m_stopList.size())),
                  (unsigned char)(n[1]/(unsigned)(m_stopList.size())),
                  (unsigned char)(n[2]/(unsigned)(m_stopList.size())),
                  (unsigned char)(n[3]/(unsigned)(m_stopList.size())));
  return true;
}

void MWAWGraphicStyle::Gradient::addTo(librevenge::RVNGPropertyList &propList) const
{
  if (!hasGradient()) return;
  propList.insert("draw:fill", "gradient");
  switch (m_type) {
  case G_Axial:
    propList.insert("draw:style", "axial");
    break;
  case G_Radial:
    propList.insert("draw:style", "radial");
    break;
  case G_Rectangular:
    propList.insert("draw:style", "rectangular");
    break;
  case G_Square:
    propList.insert("draw:style", "square");
    break;
  case G_Ellipsoid:
    propList.insert("draw:style", "ellipsoid");
    break;
  case G_Linear:
  case G_None:
#if !defined(__clang__)
  default:
#endif
    propList.insert("draw:style", "linear");
    break;
  }
  if (m_stopList.size()==2 &&m_stopList[0].m_offset <= 0 &&
      m_stopList[1].m_offset >=1) {
    size_t first=(m_type==G_Linear || m_type==G_Axial) ? 0 : 1;
    propList.insert("draw:start-color",m_stopList[first].m_color.str().c_str());
    propList.insert("librevenge:start-opacity", double(m_stopList[first].m_opacity), librevenge::RVNG_PERCENT);
    propList.insert("draw:end-color",m_stopList[1-first].m_color.str().c_str());
    propList.insert("librevenge:end-opacity", double(m_stopList[1-first].m_opacity), librevenge::RVNG_PERCENT);
  }
  else {
    librevenge::RVNGPropertyListVector gradient;
    for (auto const &gr :m_stopList) {
      librevenge::RVNGPropertyList grad;
      grad.insert("svg:offset", double(gr.m_offset), librevenge::RVNG_PERCENT);
      grad.insert("svg:stop-color", gr.m_color.str().c_str());
      grad.insert("svg:stop-opacity", double(gr.m_opacity), librevenge::RVNG_PERCENT);
      gradient.append(grad);
    }
    propList.insert("svg:linearGradient", gradient);
  }
  propList.insert("draw:angle", double(m_angle), librevenge::RVNG_GENERIC);
  propList.insert("draw:border", double(m_border), librevenge::RVNG_PERCENT);
  if (m_type != G_Linear) {
    propList.insert("svg:cx", double(m_percentCenter[0]), librevenge::RVNG_PERCENT);
    propList.insert("svg:cy", double(m_percentCenter[1]), librevenge::RVNG_PERCENT);
  }
  if (m_type == G_Radial)
    propList.insert("svg:r", double(m_radius), librevenge::RVNG_PERCENT); // checkme
}

////////////////////////////////////////////////////////////
// hatch
////////////////////////////////////////////////////////////
void MWAWGraphicStyle::Hatch::addTo(librevenge::RVNGPropertyList &propList) const
{
  if (!hasHatch()) return;
  propList.insert("draw:fill", "hatch");
  if (int(m_type)>=1 && int(m_type)<4) {
    char const *wh[]= {"single", "double", "triple"};
    propList.insert("draw:style", wh[int(m_type)-1]);
  }
  else {
    MWAW_DEBUG_MSG(("MWAWGraphicStyle::Hatch::addTo: unknown hash type %d\n", int(m_type)));
  }
  propList.insert("draw:color", m_color.str().c_str());
  propList.insert("draw:distance", double(m_distance), librevenge::RVNG_INCH);
  if (m_rotation<0 || m_rotation>0) propList.insert("draw:rotation", double(m_rotation), librevenge::RVNG_GENERIC);
}

////////////////////////////////////////////////////////////
// style
////////////////////////////////////////////////////////////
MWAWGraphicStyle::~MWAWGraphicStyle()
{
}

void MWAWGraphicStyle::setBorders(int wh, MWAWBorder const &border)
{
  int const allBits = libmwaw::LeftBit|libmwaw::RightBit|libmwaw::TopBit|libmwaw::BottomBit;
  if (wh & (~allBits)) {
    MWAW_DEBUG_MSG(("MWAWGraphicStyle::setBorders: unknown borders\n"));
    return;
  }
  size_t numData = 4;
  if (m_bordersList.size() < numData) {
    MWAWBorder emptyBorder;
    emptyBorder.m_style = MWAWBorder::None;
    m_bordersList.resize(numData, emptyBorder);
  }
  if (wh & libmwaw::LeftBit) m_bordersList[libmwaw::Left] = border;
  if (wh & libmwaw::RightBit) m_bordersList[libmwaw::Right] = border;
  if (wh & libmwaw::TopBit) m_bordersList[libmwaw::Top] = border;
  if (wh & libmwaw::BottomBit) m_bordersList[libmwaw::Bottom] = border;
}

void MWAWGraphicStyle::addTo(librevenge::RVNGPropertyList &list, bool only1D) const
{
  if (!hasLine())
    list.insert("draw:stroke", "none");
  else if (m_lineDashWidth.size()>=2) {
    int nDots1=0, nDots2=0;
    float size1=0, size2=0, totalGap=0.0;
    for (size_t c=0; c+1 < m_lineDashWidth.size();) {
      float sz=m_lineDashWidth[c++];
      if (nDots2 && (sz<size2||sz>size2)) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("MWAWGraphicStyle::addTo: can not set some dash\n"));
          first = false;
        }
        break;
      }
      if (nDots2)
        nDots2++;
      else if (!nDots1 || (sz>=size1 && sz <= size1)) {
        nDots1++;
        size1=sz;
      }
      else {
        nDots2=1;
        size2=sz;
      }
      totalGap += m_lineDashWidth[c++];
    }
    list.insert("draw:stroke", "dash");
    list.insert("draw:dots1", nDots1);
    list.insert("draw:dots1-length", double(size1), librevenge::RVNG_POINT);
    if (nDots2) {
      list.insert("draw:dots2", nDots2);
      list.insert("draw:dots2-length", double(size2), librevenge::RVNG_POINT);
    }
    const double distance = ((nDots1 + nDots2) > 0) ? double(totalGap)/double(nDots1+nDots2) : double(totalGap);
    list.insert("draw:distance", distance, librevenge::RVNG_POINT);;
  }
  else
    list.insert("draw:stroke", "solid");
  list.insert("svg:stroke-color", m_lineColor.str().c_str());
  list.insert("svg:stroke-width", double(m_lineWidth),librevenge::RVNG_POINT);

  if (m_lineOpacity < 1)
    list.insert("svg:stroke-opacity", double(m_lineOpacity), librevenge::RVNG_PERCENT);
  switch (m_lineCap) {
  case C_Round:
    list.insert("svg:stroke-linecap", "round");
    break;
  case C_Square:
    list.insert("svg:stroke-linecap", "square");
    break;
  case C_Butt:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  switch (m_lineJoin) {
  case J_Round:
    list.insert("draw:stroke-linejoin", "round");
    break;
  case J_Bevel:
    list.insert("draw:stroke-linejoin", "bevel");
    break;
  case J_Miter:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  // alignment
  switch (m_verticalAlignment) {
  case V_AlignTop:
    list.insert("draw:textarea-vertical-align", "top");
    break;
  case V_AlignCenter:
    list.insert("draw:textarea-vertical-align", "middle");
    break;
  case V_AlignBottom:
    list.insert("draw:textarea-vertical-align", "bottom");
    break;
  case V_AlignJustify:
    list.insert("draw:textarea-vertical-align", "justify");
    break;
  case V_AlignDefault:
    break; // default
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("MWAWStyle::addTo: called with unknown valign=%d\n", m_verticalAlignment));
  }
  if (!m_arrows[0].isEmpty()) m_arrows[0].addTo(list,"start");
  if (!m_arrows[1].isEmpty()) m_arrows[1].addTo(list,"end");
  if (hasShadow()) {
    list.insert("draw:shadow", "visible");
    list.insert("draw:shadow-color", m_shadowColor.str().c_str());
    list.insert("draw:shadow-opacity", double(m_shadowOpacity), librevenge::RVNG_PERCENT);
    // in cm
    list.insert("draw:shadow-offset-x", double(m_shadowOffset[0])/72.*2.54, librevenge::RVNG_GENERIC); // cm
    list.insert("draw:shadow-offset-y", double(m_shadowOffset[1])/72.*2.54, librevenge::RVNG_GENERIC); // cm
  }
  if (m_doNotPrint)
    list.insert("style:print-content", false);
  if (only1D || !hasSurface()) {
    list.insert("draw:fill", "none");
    return;
  }
  list.insert("svg:fill-rule", m_fillRuleEvenOdd ? "evenodd" : "nonzero");
  if (hasGradient())
    m_gradient.addTo(list);
  else if (hasHatch()) {
    m_hatch.addTo(list);
    if (hasSurfaceColor()) {
      list.insert("draw:fill-color", m_surfaceColor.str().c_str());
      list.insert("draw:opacity", double(m_surfaceOpacity), librevenge::RVNG_PERCENT);
      list.insert("draw:fill-hatch-solid", true);
    }
  }
  else {
    bool done = false;
    MWAWColor surfaceColor=m_surfaceColor;
    float surfaceOpacity = m_surfaceOpacity;
    if (hasPattern()) {
      MWAWColor col;
      if (m_pattern.getUniqueColor(col)) {
        // no need to create a uniform pattern
        surfaceColor = col;
        surfaceOpacity = 1;
      }
      else {
        MWAWEmbeddedObject picture;
        if (m_pattern.getBinary(picture) && !picture.m_dataList.empty() && !picture.m_dataList[0].empty()) {
          list.insert("draw:fill", "bitmap");
          list.insert("draw:fill-image", picture.m_dataList[0].getBase64Data());
          list.insert("draw:fill-image-width", m_pattern.m_dim[0], librevenge::RVNG_POINT);
          list.insert("draw:fill-image-height", m_pattern.m_dim[1], librevenge::RVNG_POINT);
          list.insert("draw:fill-image-ref-point-x",0, librevenge::RVNG_POINT);
          list.insert("draw:fill-image-ref-point-y",0, librevenge::RVNG_POINT);
          if (surfaceOpacity<1)
            list.insert("draw:opacity", double(surfaceOpacity), librevenge::RVNG_PERCENT);
          list.insert("librevenge:mime-type", picture.m_typeList.empty() ? "image/pict" : picture.m_typeList[0].c_str());
          done = true;
        }
        else {
          MWAW_DEBUG_MSG(("MWAWGraphicStyle::addTo: can not set the pattern\n"));
        }
      }
    }
    if (!done) {
      list.insert("draw:fill", "solid");
      list.insert("draw:fill-color", surfaceColor.str().c_str());
      list.insert("draw:opacity", double(surfaceOpacity), librevenge::RVNG_PERCENT);
    }
  }
}

void MWAWGraphicStyle::addFrameTo(librevenge::RVNGPropertyList &list) const
{
  if (m_backgroundOpacity>=0) {
    if (m_backgroundOpacity>0)
      list.insert("fo:background-color", m_backgroundColor.str().c_str());
    if (m_backgroundOpacity<1)
      list.insert("style:background-transparency", 1.-double(m_backgroundOpacity), librevenge::RVNG_PERCENT);
  }
  if (hasBorders()) {
    if (hasSameBorders())
      m_bordersList[0].addTo(list, "");
    else {
      for (size_t c = 0; c < m_bordersList.size(); c++) {
        if (c >= 4) break;
        switch (c) {
        case libmwaw::Left:
          m_bordersList[c].addTo(list, "left");
          break;
        case libmwaw::Right:
          m_bordersList[c].addTo(list, "right");
          break;
        case libmwaw::Top:
          m_bordersList[c].addTo(list, "top");
          break;
        case libmwaw::Bottom:
          m_bordersList[c].addTo(list, "bottom");
          break;
#if !defined(__clang__)
        default:
          MWAW_DEBUG_MSG(("MWAWGraphicStyle::addFrameTo: can not send %d border\n",int(c)));
          break;
#endif
        }
      }
    }
  }
  if (hasShadow()) {
    list.insert("draw:shadow", "visible");
    list.insert("draw:shadow-color", m_shadowColor.str().c_str());
    list.insert("draw:shadow-opacity", double(m_shadowOpacity), librevenge::RVNG_PERCENT);
    // in cm
    list.insert("draw:shadow-offset-x", double(m_shadowOffset[0])/72.*2.54, librevenge::RVNG_GENERIC); // cm
    list.insert("draw:shadow-offset-y", double(m_shadowOffset[1])/72.*2.54, librevenge::RVNG_GENERIC); // cm
  }
  if (!m_frameName.empty())
    list.insert("librevenge:frame-name",m_frameName.c_str());
}

int MWAWGraphicStyle::cmp(MWAWGraphicStyle const &a) const
{
  if (m_lineWidth < a.m_lineWidth) return -1;
  if (m_lineWidth > a.m_lineWidth) return 1;
  if (m_lineCap < a.m_lineCap) return -1;
  if (m_lineCap > a.m_lineCap) return 1;
  if (m_lineJoin < a.m_lineJoin) return -1;
  if (m_lineJoin > a.m_lineJoin) return 1;
  if (m_lineOpacity < a.m_lineOpacity) return -1;
  if (m_lineOpacity > a.m_lineOpacity) return 1;
  if (m_lineColor < a.m_lineColor) return -1;
  if (m_lineColor > a.m_lineColor) return 1;

  if (m_lineDashWidth.size() < a.m_lineDashWidth.size()) return -1;
  if (m_lineDashWidth.size() > a.m_lineDashWidth.size()) return 1;
  for (size_t d=0; d < m_lineDashWidth.size(); ++d) {
    if (m_lineDashWidth[d] > a.m_lineDashWidth[d]) return -1;
    if (m_lineDashWidth[d] < a.m_lineDashWidth[d]) return 1;
  }
  for (int i=0; i<2; ++i) {
    if (m_arrows[i]!=a.m_arrows[i])
      return m_arrows[i]<a.m_arrows[i] ? -1 : 1;
    if (m_flip[i]!=a.m_flip[i])
      return m_flip[i] ? 1 : -1;
  }

  if (m_fillRuleEvenOdd != a.m_fillRuleEvenOdd) return m_fillRuleEvenOdd ? 1: -1;

  if (m_surfaceColor < a.m_surfaceColor) return -1;
  if (m_surfaceColor > a.m_surfaceColor) return 1;
  if (m_surfaceOpacity < a.m_surfaceOpacity) return -1;
  if (m_surfaceOpacity > a.m_surfaceOpacity) return 1;

  if (m_shadowColor < a.m_shadowColor) return -1;
  if (m_shadowColor > a.m_shadowColor) return 1;
  if (m_shadowOpacity < a.m_shadowOpacity) return -1;
  if (m_shadowOpacity > a.m_shadowOpacity) return 1;
  int diff=m_shadowOffset.cmp(a.m_shadowOffset);
  if (diff) return diff;

  diff = m_pattern.cmp(a.m_pattern);
  if (diff) return diff;
  diff = m_gradient.cmp(a.m_gradient);
  if (diff) return diff;
  diff = m_hatch.cmp(a.m_hatch);
  if (diff) return diff;

  size_t numBorders=m_bordersList.size();
  if (a.m_bordersList.size()>numBorders)
    numBorders=a.m_bordersList.size();
  for (size_t b=0; b<numBorders; ++b) {
    bool empty=b>=m_bordersList.size() || m_bordersList[b].isEmpty();
    bool aEmpty=b>=a.m_bordersList.size() || a.m_bordersList[b].isEmpty();
    if (empty!=aEmpty) return empty ? 1 : -1;
    diff=m_bordersList[b].compare(a.m_bordersList[b]);
    if (diff) return diff;
  }
  if (m_backgroundColor < a.m_backgroundColor) return -1;
  if (m_backgroundColor > a.m_backgroundColor) return 1;
  if (m_backgroundOpacity < a.m_backgroundOpacity) return -1;
  if (m_backgroundOpacity > a.m_backgroundOpacity) return 1;
  if (m_frameName < a.m_frameName) return -1;
  if (m_frameName > a.m_frameName) return 1;
  if (m_frameNextName < a.m_frameNextName) return -1;
  if (m_frameNextName > a.m_frameNextName) return 1;
  if (m_verticalAlignment < a.m_verticalAlignment) return -1;
  if (m_verticalAlignment > a.m_verticalAlignment) return 1;

  if (m_rotate < a.m_rotate) return -1;
  if (m_rotate > a.m_rotate) return 1;
  return 0;
}

std::ostream &operator<<(std::ostream &o, MWAWGraphicStyle const &st)
{
  if (st.m_rotate<0 || st.m_rotate>0)
    o << "rot=" << st.m_rotate << ",";
  if (st.m_flip[0]) o << "flipX,";
  if (st.m_flip[1]) o << "flipY,";
  o << "line=[";
  if (st.m_lineWidth<1 || st.m_lineWidth>1)
    o << "width=" << st.m_lineWidth << ",";
  if (!st.m_lineDashWidth.empty()) {
    o << "dash=[";
    for (auto w : st.m_lineDashWidth)
      o << w << ",";
    o << "],";
  }
  switch (st.m_lineCap) {
  case MWAWGraphicStyle::C_Square:
    o << "cap=square,";
    break;
  case MWAWGraphicStyle::C_Round:
    o << "cap=round,";
    break;
  case MWAWGraphicStyle::C_Butt:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  switch (st.m_lineJoin) {
  case MWAWGraphicStyle::J_Bevel:
    o << "join=bevel,";
    break;
  case MWAWGraphicStyle::J_Round:
    o << "join=round,";
    break;
  case MWAWGraphicStyle::J_Miter:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  if (st.m_lineOpacity<1)
    o << "opacity=" << st.m_lineOpacity << ",";
  if (!st.m_lineColor.isBlack())
    o << "color=" << st.m_lineColor << ",";
  if (!st.m_arrows[0].isEmpty()) o << "arrow[start]=[" << st.m_arrows[0] << "],";
  if (!st.m_arrows[1].isEmpty()) o << "arrow[end]=[" << st.m_arrows[1] << "],";
  o << "],";
  if (st.hasSurfaceColor()) {
    o << "surf=[";
    if (!st.m_surfaceColor.isWhite())
      o << "color=" << st.m_surfaceColor << ",";
    if (st.m_surfaceOpacity > 0)
      o << "opacity=" << st.m_surfaceOpacity << ",";
    o << "],";
    if (st.m_fillRuleEvenOdd)
      o << "fill[evenOdd],";
  }
  if (st.hasPattern())
    o << "pattern=[" << st.m_pattern << "],";
  if (st.hasGradient())
    o << "grad=[" << st.m_gradient << "],";
  if (st.hasHatch())
    o << "hatch=[" << st.m_hatch << "],";
  if (st.hasShadow()) {
    o << "shadow=[";
    if (!st.m_shadowColor.isBlack())
      o << "color=" << st.m_shadowColor << ",";
    if (st.m_shadowOpacity > 0)
      o << "opacity=" << st.m_shadowOpacity << ",";
    o << "offset=" << st.m_shadowOffset << ",";
    o << "],";
  }
  if (st.hasBorders()) {
    for (size_t i = 0; i < st.m_bordersList.size(); i++) {
      if (st.m_bordersList[i].m_style == MWAWBorder::None)
        continue;
      o << "bord";
      if (i < 4) {
        static char const *wh[] = { "L", "R", "T", "B"};
        o << wh[i];
      }
      else o << "[#wh=" << i << "]";
      o << "=" << st.m_bordersList[i] << ",";
    }
  }
  if (!st.m_backgroundColor.isWhite())
    o << "background[color]=" << st.m_backgroundColor << ",";
  if (st.m_backgroundOpacity>=0)
    o << "background[opacity]=" << 100.f *st.m_backgroundOpacity << "%,";
  if (!st.m_frameName.empty())
    o << "frame[name]=" << st.m_frameName << ",";
  if (!st.m_frameNextName.empty())
    o << "frame[linkedto]=" << st.m_frameNextName << ",";
  o << st.m_extra;
  return o;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
