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

#include <iomanip>
#include <map>
#include <sstream>
#include <stack>

#include "MWAWCell.hxx"
#include "MWAWDebug.hxx"
#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWSection.hxx"

#include "RagTime5Document.hxx"
#include "RagTime5StructManager.hxx"

#include "RagTime5StyleManager.hxx"

/** Internal: the structures of a RagTime5Style */
namespace RagTime5StyleManagerInternal
{
////////////////////////////////////////
//! Internal: the helper to read field color field for a RagTime5StyleManager
struct ColorFieldParser final : public RagTime5StructManager::FieldParser {
  //! constructor
  ColorFieldParser()
    : RagTime5StructManager::FieldParser("GraphColor")
    , m_colorsList()
  {
    m_regroupFields=false;
  }
  //! destructor
  ~ColorFieldParser() final;
  //! return the debug name corresponding to a field
  std::string getZoneName(int n) const final
  {
    std::stringstream s;
    s << "GraphColor-GC" << n;
    return s.str();
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field &field, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    if (field.m_type!=RagTime5StructManager::Field::T_FieldList) {
      MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find unexpected field type \n"));
      f << "##field,";
      return true;
    }
    switch (field.m_fileType) {
    case 0x7d02a:
      for (auto const &child : field.m_fieldList) {
        // checkme:
        if (child.m_type==RagTime5StructManager::Field::T_Color && child.m_fileType==0x84040) {
          if (n>=0 && int(m_colorsList.size())<n)
            m_colorsList.resize(size_t(n));
          if (n>=1 && n<=int(m_colorsList.size()))
            m_colorsList[size_t(n-1)]=child.m_color;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find bad n\n"));
            f << "col=" << child.m_color << "[###],";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find some unknown color block\n"));
        f << "##col=" << child << ",";
      }
      break;
    case 0x17d481a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x3b880) {
          f << "id=" << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find some unknown id block\n"));
        f << "##id=" << child << ",";
      }
      break;
    case 0x17d484a: // always 0:1
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_2Long && child.m_fileType==0x34800) {
          f << "unkn0=" << child.m_longValue[0] << "x" << child.m_longValue[1] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find some unknown unkn0 block\n"));
        f << "##unkn0=" << child << ",";
      }
      break;
    case 0x17d486a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          f << "fl0=" << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find some unknown fl0 block\n"));
        f << "##fl0=" << child << ",";
      }
      break;
    default:
      MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::ColorFieldParser::parseField: find some unknown block\n"));
      f << "###" << field;
      break;
    }
    return true;
  }

  //! the list of color
  std::vector<MWAWColor> m_colorsList;
};

ColorFieldParser::~ColorFieldParser()
{
}

////////////////////////////////////////
//! Internal: the helper to read field graphic field for a RagTime5StyleManager
struct GraphicFieldParser final : public RagTime5StructManager::FieldParser {
  //! constructor
  explicit GraphicFieldParser(std::vector<MWAWColor> const &colorList)
    : RagTime5StructManager::FieldParser("GraphStyle")
    , m_colorsList(colorList)
    , m_styleList()
  {
    m_regroupFields=true;
  }
  //! destructor
  ~GraphicFieldParser() final;
  //! return the debug name corresponding to a field
  std::string getZoneName(int n) const final
  {
    // we need to resize here (if the style does not contains any field)
    if (n>=int(m_styleList.size()))
      const_cast<GraphicFieldParser *>(this)->m_styleList.resize(size_t(n+1));
    std::stringstream s;
    s << "GraphStyle-GS" << n;
    return s.str();
  }
  //! parse a header field
  bool parseHeaderField(RagTime5StructManager::Field &field, RagTime5Zone &zone, int n, libmwaw::DebugStream &f) final
  {
    if (n>=int(m_styleList.size()))
      m_styleList.resize(size_t(n+1));
    auto &style=m_styleList[size_t(n)];
    MWAWInputStreamPtr input=zone.getInput();
    if (style.read(input, field, m_colorsList))
      f << style;
    else
      f << "###" << field;
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field &field, RagTime5Zone &zone, int n, libmwaw::DebugStream &f) final
  {
    if (n<=0) {
      MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::GraphicFieldParser::parseField: n=%d is bad\n", n));
      n=0;
    }
    if (n>=int(m_styleList.size()))
      m_styleList.resize(size_t(n+1));
    auto &style=m_styleList[size_t(n)];
    MWAWInputStreamPtr input=zone.getInput();
    if (style.read(input, field, m_colorsList)) {
      RagTime5StyleManager::GraphicStyle modStyle;
      modStyle.read(input, field, m_colorsList);
      f << modStyle;
    }
    else
      f << "##" << field;
    return true;
  }

  // !the main color map
  std::vector<MWAWColor> const &m_colorsList;
//! the list of graphic style
  std::vector<RagTime5StyleManager::GraphicStyle> m_styleList;
};

GraphicFieldParser::~GraphicFieldParser()
{
}

////////////////////////////////////////
//! Internal: the helper to read style for a RagTime5StyleManager
struct TextFieldParser final : public RagTime5StructManager::FieldParser {
  //! constructor
  TextFieldParser()
    : RagTime5StructManager::FieldParser("TextStyle")
    , m_styleList()
  {
  }
  //! destructor
  ~TextFieldParser() final;
  //! return the debug name corresponding to a field
  std::string getZoneName(int n) const final
  {
    std::stringstream s;
    s << "TextStyle-TS" << n;
    return s.str();
  }
  //! parse a header field
  bool parseHeaderField(RagTime5StructManager::Field &field, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    if (n>=int(m_styleList.size()))
      m_styleList.resize(size_t(n+1));
    auto &style=m_styleList[size_t(n)];
    if (style.read(field))
      f << style;
    else
      f << "###" << field;
    return true;
  }
  //! parse a field
  bool parseField(RagTime5StructManager::Field &field, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    if (n<=0) {
      MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::TextFieldParser::parseField: n=%d is bad\n", n));
      n=0;
    }
    if (n>=int(m_styleList.size()))
      m_styleList.resize(size_t(n+1));
    auto &style=m_styleList[size_t(n)];
    if (style.read(field)) {
      RagTime5StyleManager::TextStyle modStyle;
      modStyle.read(field);
      f << modStyle;
    }
    else
      f << "#" << field;
    return true;
  }

  //! the list of read style
  std::vector<RagTime5StyleManager::TextStyle> m_styleList;
};

//! Internal: the state of a RagTime5Style
struct State {
  //! constructor
  State()
    : m_colorsList()
    , m_formatList()
    , m_graphicStyleList()
    , m_textStyleList()
  {
  }
  //! init the color list (if empty)
  void initColorsList();
  //! the list of color
  std::vector<MWAWColor> m_colorsList;
  //! the list of format
  std::vector<MWAWCell::Format> m_formatList;
  //! the list of graphic styles
  std::vector<RagTime5StyleManager::GraphicStyle> m_graphicStyleList;
  //! the list of text styles
  std::vector<RagTime5StyleManager::TextStyle> m_textStyleList;
};

TextFieldParser::~TextFieldParser()
{
}

void State::initColorsList()
{
  if (!m_colorsList.empty()) return;
  MWAW_DEBUG_MSG(("RagTime5StyleManagerInternal::State::initColorsList: colors' list is empty, set it to default\n"));
  m_colorsList.push_back(MWAWColor::white());
  m_colorsList.push_back(MWAWColor(0,0,0,0)); // transparent
  m_colorsList.push_back(MWAWColor::black());
}
}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5StyleManager::RagTime5StyleManager(RagTime5Document &doc)
  : m_document(doc)
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5StyleManagerInternal::State)
{
}

RagTime5StyleManager::~RagTime5StyleManager()
{
}

////////////////////////////////////////////////////////////
// read style
////////////////////////////////////////////////////////////
bool RagTime5StyleManager::readGraphicColors(RagTime5ClusterManager::Cluster &cluster)
{
  RagTime5StyleManagerInternal::ColorFieldParser fieldParser;
  if (!m_document.readStructZone(cluster.m_dataLink, fieldParser, 14, &cluster.m_nameLink))
    return false;
  m_state->m_colorsList=fieldParser.m_colorsList;
  return true;
}

bool RagTime5StyleManager::readGraphicStyles(RagTime5ClusterManager::Cluster &cluster)
{
  m_state->initColorsList();
  RagTime5StyleManagerInternal::GraphicFieldParser fieldParser(m_state->m_colorsList);
  if (!m_document.readStructZone(cluster.m_dataLink, fieldParser, 14, &cluster.m_nameLink))
    return false;
  if (fieldParser.m_styleList.empty())
    fieldParser.m_styleList.resize(1);

  //
  // check parent relation, check for loop, ...
  //
  std::vector<size_t> rootList;
  std::stack<size_t> toCheck;
  std::multimap<size_t, size_t> idToChildIpMap;
  auto numStyles=size_t(fieldParser.m_styleList.size());
  for (size_t i=0; i<numStyles; ++i) {
    auto &style=fieldParser.m_styleList[i];
    if (style.m_parentId>=0 && style.m_parentId>=static_cast<int>(numStyles)) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: find unexpected parent %d for style %d\n",
                      static_cast<int>(style.m_parentId), static_cast<int>(i)));
      style.m_parentId=0;
      continue;
    }
    else if (style.m_parentId>=0) {
      idToChildIpMap.insert(std::multimap<size_t, size_t>::value_type(size_t(style.m_parentId),i));
      continue;
    }
    rootList.push_back(i);
    toCheck.push(i);
  }
  std::set<size_t> seens;
  while (true) {
    size_t posToCheck=0; // to make clang happy
    if (!toCheck.empty()) {
      posToCheck=toCheck.top();
      toCheck.pop();
    }
    else if (seens.size()+1==numStyles)
      break;
    else {
      bool ok=false;
      for (size_t i=1; i<numStyles; ++i) {
        if (seens.find(i)!=seens.end())
          continue;
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: find unexpected root %d\n", static_cast<int>(i)));
        posToCheck=i;
        rootList.push_back(i);

        auto &style=fieldParser.m_styleList[i];
        style.m_parentId=0;
        ok=true;
        break;
      }
      if (!ok)
        break;
    }
    if (seens.find(posToCheck)!=seens.end()) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: oops, %d is already seens\n", static_cast<int>(posToCheck)));
      continue;
    }
    seens.insert(posToCheck);
    auto childIt=idToChildIpMap.lower_bound(posToCheck);
    std::vector<size_t> badChildList;
    while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
      size_t childId=childIt++->second;
      if (seens.find(childId)!=seens.end()) {
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: find loop for child %d\n", static_cast<int>(childId)));
        RagTime5StyleManager::GraphicStyle &style=fieldParser.m_styleList[childId];
        style.m_parentId=0;
        badChildList.push_back(childId);
        continue;
      }
      toCheck.push(childId);
    }
    for (auto badId : badChildList) {
      childIt=idToChildIpMap.lower_bound(posToCheck);
      while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
        if (childIt->second==badId) {
          idToChildIpMap.erase(childIt);
          break;
        }
        ++childIt;
      }
    }
  }

  if (!m_state->m_graphicStyleList.empty()) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: Ooops, we already set some graphicStyles\n"));
  }

  // now let generate the final style
  m_state->m_graphicStyleList.resize(numStyles);
  seens.clear();
  for (auto id : rootList) {
    if (id>=numStyles) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readGraphicStyles: find loop for id=%d\n", static_cast<int>(id)));
      continue;
    }
    updateGraphicStyles(id, fieldParser.m_styleList[id], fieldParser.m_styleList, idToChildIpMap, seens);
  }
  return true;
}

void RagTime5StyleManager::updateGraphicStyles
(size_t id, RagTime5StyleManager::GraphicStyle const &style, std::vector<RagTime5StyleManager::GraphicStyle> const &listReadStyles,
 std::multimap<size_t, size_t> const &idToChildIpMap, std::set<size_t> &seens)
{
  if (id>=m_state->m_graphicStyleList.size() || seens.find(id)!=seens.end()) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateGraphicStyles: problem with style with id=%d\n", static_cast<int>(id)));
    return;
  }
  seens.insert(id);
  m_state->m_graphicStyleList[id]=style;

  auto childIt=idToChildIpMap.lower_bound(id);
  while (childIt!=idToChildIpMap.end() && childIt->first==id) {
    size_t childId=childIt++->second;
    if (childId>=listReadStyles.size()) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::updateGraphicStyles: problem with style with childId=%d\n", static_cast<int>(childId)));
      continue;
    }
    auto childStyle=style;
    childStyle.insert(listReadStyles[childId]);
    updateGraphicStyles(childId, childStyle, listReadStyles, idToChildIpMap, seens);
  }
}

bool RagTime5StyleManager::getLineColor(int gId, MWAWColor &color) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::getLineColor: can not find graphic style %d\n", gId));
    return false;
  }
  auto const &style=m_state->m_graphicStyleList[size_t(gId)];
  color=style.m_colors[0].get();
  if (style.m_colorsAlpha[0]>=0 && style.m_colorsAlpha[0]<1)
    color=MWAWColor::barycenter(style.m_colorsAlpha[0],color,1-style.m_colorsAlpha[0],MWAWColor::white());

  return true;
}

bool RagTime5StyleManager::getCellBorder(int gId, MWAWBorder &border) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::getCellBorder: can not find graphic style %d\n", gId));
    border.m_width=0;
    return false;
  }
  auto const &gStyle=m_state->m_graphicStyleList[size_t(gId)];
  if (gStyle.m_width>=0)
    border.m_width=double(gStyle.m_width);
  else
    border.m_width=1;
  if (gStyle.m_pattern) {
    MWAWColor color;
    if (gStyle.m_pattern->getAverageColor(color)) {
      if (gStyle.m_colors[0].isSet() || gStyle.m_colors[1].isSet()) {
        float alpha=(float(color.getRed())+float(color.getGreen())+float(color.getBlue()))/765.f;
        border.m_color=MWAWColor::barycenter(1.f-alpha, *gStyle.m_colors[0], alpha, *gStyle.m_colors[1]);
      }
      else
        border.m_color=color;
    }
  }
  else if (gStyle.m_colors[0].isSet())
    border.m_color=gStyle.m_colors[0].get();
  else // default
    border.m_color=MWAWColor(0,0,0);
  if (gStyle.m_dash.isSet() && gStyle.m_dash->size()>=4) {
    long fullWidth=0, emptyWidth=0;
    for (size_t i=0; i<gStyle.m_dash->size(); i+=2) {
      if ((i%4)==0)
        fullWidth+= (*gStyle.m_dash)[i];
      else
        emptyWidth+=(*gStyle.m_dash)[i];
    }
    if (fullWidth==2 && emptyWidth==2)
      border.m_style=MWAWBorder::Dot;
    else if (fullWidth==10 && emptyWidth==5)
      border.m_style=MWAWBorder::Dash;
    else // ok, specific dash, let use large dot
      border.m_style=MWAWBorder::LargeDot;
  }
  return true;

}

bool RagTime5StyleManager::getCellBackgroundColor(int gId, MWAWColor &color) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::getCellBackgroundColor: can not find graphic style %d\n", gId));
    return false;
  }
  auto const &gStyle=m_state->m_graphicStyleList[size_t(gId)];
  if (gStyle.m_pattern) {
    MWAWColor col;
    if (gStyle.m_pattern->getAverageColor(col)) {
      if (gStyle.m_colors[0].isSet() || gStyle.m_colors[1].isSet()) {
        float alpha=(float(col.getRed())+float(col.getGreen())+float(col.getBlue()))/765.f;
        color=MWAWColor::barycenter(1.f-alpha, *gStyle.m_colors[0], alpha, *gStyle.m_colors[1]);
      }
      else
        color=col;
    }
  }
  else if (gStyle.m_colors[0].isSet())
    color=gStyle.m_colors[0].get();
  else // default is white
    color=MWAWColor(255,255,255);

  return true;
}

bool RagTime5StyleManager::updateBorderStyle(int gId, MWAWGraphicStyle &style, bool isLine) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateBorderStyle: can not find graphic style %d\n", gId));
    style.m_lineWidth=0;
    return false;
  }
  auto const &gStyle=m_state->m_graphicStyleList[size_t(gId)];
  if (gStyle.m_width>=0)
    style.m_lineWidth=gStyle.m_width;
  else
    style.m_lineWidth=1;
  if (gStyle.m_pattern) {
    MWAWColor color;
    if (gStyle.m_pattern->getAverageColor(color)) {
      if (gStyle.m_colors[0].isSet() || gStyle.m_colors[1].isSet()) {
        float alpha=(float(color.getRed())+float(color.getGreen())+float(color.getBlue()))/765.f;
        style.m_lineColor=MWAWColor::barycenter(1.f-alpha, *gStyle.m_colors[0], alpha, *gStyle.m_colors[1]);
      }
      else
        style.m_lineColor=color;
    }
  }
  else if (isLine || gStyle.m_colors[0].isSet())
    style.m_lineColor=gStyle.m_colors[0].get();
  else // default is blue
    style.m_lineColor=MWAWColor(0,0,255);
  if (gStyle.m_colorsAlpha[0]>=0)
    style.m_lineOpacity=gStyle.m_colorsAlpha[0];
  if (gStyle.m_dash.isSet() && gStyle.m_dash->size()>=4) {
    for (size_t i=0; i<gStyle.m_dash->size(); i+=2)
      style.m_lineDashWidth.push_back(float((*gStyle.m_dash)[i]));
  }
  return true;
}

bool RagTime5StyleManager::updateFrameStyle(int gId, MWAWGraphicStyle &style) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateSurfaceStyle: can not find graphic style %d\n", gId));
    return false;
  }
  auto const &gStyle=m_state->m_graphicStyleList[size_t(gId)];
  if (gStyle.m_colorsAlpha[0]<=0 && gStyle.m_colorsAlpha[0]>=0)
    return true;
  float alpha=gStyle.m_colorsAlpha[0]>=0 ? gStyle.m_colorsAlpha[0] : 1;
  if (((gStyle.m_gradient>=1 && gStyle.m_gradient<=2) || gStyle.m_pattern) &&
      gStyle.m_colors[0].isSet() && gStyle.m_colors[1].isSet())
    style.setBackgroundColor(MWAWColor::barycenter(0.5, gStyle.m_colors[0].get(),
                             0.5, gStyle.m_colors[1].get()),
                             0.5f*gStyle.m_colorsAlpha[0]+0.5f*gStyle.m_colorsAlpha[1]);
  else if (gStyle.m_colors[0].isSet())
    style.setBackgroundColor(gStyle.m_colors[0].get(), alpha);
  return true;
}

bool RagTime5StyleManager::updateSurfaceStyle(int gId, MWAWGraphicStyle &style) const
{
  if (gId<=0 || gId>=static_cast<int>(m_state->m_graphicStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateSurfaceStyle: can not find graphic style %d\n", gId));
    return false;
  }
  auto const &gStyle=m_state->m_graphicStyleList[size_t(gId)];
  if (gStyle.m_colorsAlpha[0]<=0 && gStyle.m_colorsAlpha[0]>=0)
    return true;
  float alpha=gStyle.m_colorsAlpha[0]>=0 ? gStyle.m_colorsAlpha[0] : 1;
  if (gStyle.m_gradient>=1 && gStyle.m_gradient<=2) {
    auto &finalGrad=style.m_gradient;
    finalGrad.m_type=gStyle.m_gradient==2 ? MWAWGraphicStyle::Gradient::G_Radial : MWAWGraphicStyle::Gradient::G_Linear;
    finalGrad.m_stopList.resize(0);
    if (gStyle.m_gradient==1)
      finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(0, MWAWColor::white()));
    else
      finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(0, gStyle.m_colors[0].get()));
    finalGrad.m_stopList.push_back(MWAWGraphicStyle::Gradient::Stop(1, gStyle.m_colors[1].get()));
    if (gStyle.m_gradientCenter.isSet())
      finalGrad.m_percentCenter=*gStyle.m_gradientCenter;
    if (gStyle.m_gradientRotation>-1000)
      finalGrad.m_angle=gStyle.m_gradientRotation+90;
  }
  else if (gStyle.m_pattern) {
    auto pat=*gStyle.m_pattern;
    if (gStyle.m_colors[0].isSet())
      pat.m_colors[1]=*gStyle.m_colors[0];
    if (gStyle.m_colors[1].isSet())
      pat.m_colors[0]=*gStyle.m_colors[1];
    style.setPattern(pat, alpha);
  }
  else if (gStyle.m_colors[0].isSet())
    style.setSurfaceColor(gStyle.m_colors[0].get(), alpha);
  return true;
}

bool RagTime5StyleManager::readTextStyles(RagTime5ClusterManager::Cluster &cluster)
{
  RagTime5StyleManagerInternal::TextFieldParser fieldParser;
  if (!m_document.readStructZone(cluster.m_dataLink, fieldParser, 14, &cluster.m_nameLink))
    return false;

  if (fieldParser.m_styleList.empty())
    fieldParser.m_styleList.resize(1);

  //
  // check parent relation, check for loop, ...
  //
  std::vector<size_t> rootList;
  std::stack<size_t> toCheck;
  std::multimap<size_t, size_t> idToChildIpMap;
  auto numStyles=size_t(fieldParser.m_styleList.size());
  for (size_t i=0; i<numStyles; ++i) {
    RagTime5StyleManager::TextStyle &style=fieldParser.m_styleList[i];
    if (!style.m_fontName.empty()) // update the font it
      style.m_fontId=m_parserState->m_fontConverter->getId(style.m_fontName.cstr());
    bool ok=true;
    for (auto &parentId : style.m_parentId) {
      if (parentId<=0)
        continue;
      if (parentId>=static_cast<int>(numStyles)) {
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: find unexpected parent %d for style %d\n",
                        static_cast<int>(parentId), static_cast<int>(i)));
        parentId=0;
        continue;
      }
      ok=false;
      idToChildIpMap.insert(std::multimap<size_t, size_t>::value_type(size_t(parentId),i));
    }
    if (!ok) continue;
    rootList.push_back(i);
    toCheck.push(i);
  }
  std::set<size_t> seens;
  while (true) {
    size_t posToCheck=0; // to make clang happy
    if (!toCheck.empty()) {
      posToCheck=toCheck.top();
      toCheck.pop();
    }
    else if (seens.size()+1==numStyles)
      break;
    else {
      bool ok=false;
      for (size_t i=1; i<numStyles; ++i) {
        if (seens.find(i)!=seens.end())
          continue;
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: find unexpected root %d\n", static_cast<int>(i)));
        posToCheck=i;
        rootList.push_back(i);

        auto &style=fieldParser.m_styleList[i];
        style.m_parentId[0]=style.m_parentId[1]=0;
        ok=true;
        break;
      }
      if (!ok)
        break;
    }
    if (seens.find(posToCheck)!=seens.end()) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: oops, %d is already seens\n", static_cast<int>(posToCheck)));
      continue;
    }
    seens.insert(posToCheck);
    auto childIt=idToChildIpMap.lower_bound(posToCheck);
    std::vector<size_t> badChildList;
    while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
      size_t childId=childIt++->second;
      if (seens.find(childId)!=seens.end()) {
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: find loop for child %d\n", static_cast<int>(childId)));
        auto &style=fieldParser.m_styleList[childId];
        if (style.m_parentId[0]==static_cast<int>(posToCheck))
          style.m_parentId[0]=0;
        if (style.m_parentId[1]==static_cast<int>(posToCheck))
          style.m_parentId[1]=0;
        badChildList.push_back(childId);
        continue;
      }
      toCheck.push(childId);
    }
    for (auto badId : badChildList) {
      childIt=idToChildIpMap.lower_bound(posToCheck);
      while (childIt!=idToChildIpMap.end() && childIt->first==posToCheck) {
        if (childIt->second==badId) {
          idToChildIpMap.erase(childIt);
          break;
        }
        ++childIt;
      }
    }
  }

  if (!m_state->m_textStyleList.empty()) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: Ooops, we already set some textStyles\n"));
  }

  // now let generate the final style
  m_state->m_textStyleList.resize(numStyles);
  seens.clear();
  for (auto id : rootList) {
    if (id>=numStyles) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readTextStyles: find loop for id=%d\n", static_cast<int>(id)));
      continue;
    }
    updateTextStyles(id, fieldParser.m_styleList[id], fieldParser.m_styleList, idToChildIpMap, seens);
  }
  return true;
}

void RagTime5StyleManager::updateTextStyles
(size_t id, RagTime5StyleManager::TextStyle const &style, std::vector<RagTime5StyleManager::TextStyle> const &listReadStyles,
 std::multimap<size_t, size_t> const &idToChildIpMap, std::set<size_t> &seens)
{
  if (id>=m_state->m_textStyleList.size() || seens.find(id)!=seens.end()) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateTextStyles: problem with style with id=%d\n", static_cast<int>(id)));
    return;
  }
  seens.insert(id);
  auto styl=style;
  styl.m_fontFlags[0]&=(~style.m_fontFlags[1]);
  m_state->m_textStyleList[id]=styl;

  auto childIt=idToChildIpMap.lower_bound(id);
  while (childIt!=idToChildIpMap.end() && childIt->first==id) {
    size_t childId=childIt++->second;
    if (childId>=listReadStyles.size()) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::updateTextStyles: problem with style with childId=%d\n", static_cast<int>(childId)));
      continue;
    }
    auto childStyle=styl;
    childStyle.insert(listReadStyles[childId]);
    updateTextStyles(childId, childStyle, listReadStyles, idToChildIpMap, seens);
  }
}

bool RagTime5StyleManager::updateTextStyles(int tId, MWAWFont &font, MWAWParagraph &para, MWAWSection &section, double totalWidth) const
{
  font=MWAWFont();
  para=MWAWParagraph();
  section=MWAWSection();

  if (tId<=0 || tId>=static_cast<int>(m_state->m_textStyleList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateTextStyles: can not find text style %d\n", tId));
    return false;
  }
  auto const &style=m_state->m_textStyleList[size_t(tId)];
  if (style.m_fontId>0) font.setId(style.m_fontId);
  if (style.m_fontSize>0) font.setSize(float(style.m_fontSize));

  MWAWFont::Line underline(MWAWFont::Line::None);
  uint32_t flag=style.m_fontFlags[0];
  uint32_t flags=0;
  if (flag&0x1) flags |= MWAWFont::boldBit;
  if (flag&0x2) flags |= MWAWFont::italicBit;
  if (flag&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple); // checkme
  if (flag&0x8) flags |= MWAWFont::embossBit;
  if (flag&0x10) flags |= MWAWFont::shadowBit;

  if (flag&0x200) font.setStrikeOutStyle(MWAWFont::Line::Simple);
  if (flag&0x400) flags |= MWAWFont::smallCapsBit;
  // flag&0x800: kumorarya
  if (flag&0x2000)
    underline.m_word=true;
  switch (style.m_caps) {
  case 1:
    flags |= MWAWFont::uppercaseBit;
    break;
  case 2:
    flags |= MWAWFont::lowercaseBit;
    break;
  case 3:
    flags |= MWAWFont::initialcaseBit;
    break;
  default:
    break;
  }
  switch (style.m_underline) {
  case 1:
    underline.m_style=MWAWFont::Line::Simple;
    font.setUnderline(underline);
    break;
  case 2:
    underline.m_style=MWAWFont::Line::Simple;
    underline.m_type=MWAWFont::Line::Double;
    font.setUnderline(underline);
    break;
  default:
    break;
  }
  if (style.m_letterSpacings[0]>0 || style.m_letterSpacings[0]<0)
    font.setDeltaLetterSpacing(float(1+style.m_letterSpacings[0]), librevenge::RVNG_PERCENT);
  if (style.m_widthStreching>0)
    font.setWidthStreching(float(style.m_widthStreching));
  if (style.m_scriptPosition.isSet() || style.m_fontScaling>=0) {
    float scaling=style.m_fontScaling>0 ? style.m_fontScaling : 1;
    font.set(MWAWFont::Script(*style.m_scriptPosition*100,librevenge::RVNG_PERCENT,int(scaling*100)));
  }
  if (style.m_language>0) {
    std::string lang=TextStyle::getLanguageLocale(style.m_language);
    if (!lang.empty())
      font.setLanguage(lang);
  }
  font.setFlags(flags);
  MWAWColor color;
  if (style.m_graphStyleId>0 && getLineColor(style.m_graphStyleId, color))
    font.setColor(color);

  //
  // para
  //
  if (style.m_keepWithNext.isSet() && *style.m_keepWithNext)
    para.m_breakStatus = para.m_breakStatus.get()|MWAWParagraph::NoBreakWithNextBit;
  switch (style.m_justify) {
  case 0:
    break;
  case 1:
    para.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationRight ;
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    break;
  case 4:
    para.m_justify = MWAWParagraph::JustificationFullAllLines;
    break;
  default:
    break;
  }
  // TODO: use style.m_breakMethod
  para.m_marginsUnit=librevenge::RVNG_POINT;
  for (int i=0; i<3; ++i) {
    if (style.m_margins[i]<0) continue;
    if (i==2)
      para.m_margins[0]=style.m_margins[2]-*para.m_margins[1];
    else
      para.m_margins[i+1] = style.m_margins[i];
  }
  if (style.m_spacings[0]>0) {
    if (style.m_spacingUnits[0]==0)
      para.setInterline(style.m_spacings[0], librevenge::RVNG_PERCENT);
    else if (style.m_spacingUnits[0]==1)
      para.setInterline(style.m_spacings[0], librevenge::RVNG_POINT);
  }
  for (int i=1; i<3; ++i) {
    if (style.m_spacings[i]<0) continue;
    if (style.m_spacingUnits[i]==0)
      para.m_spacings[i]=style.m_spacings[i]*12./72.;
    else if (style.m_spacingUnits[0]==1)
      para.m_spacings[i]=style.m_spacings[i]/72.;
  }
  // tabs stop
  for (auto const &tab : style.m_tabList) {
    MWAWTabStop newTab;
    newTab.m_position = double(tab.m_position)/72.;
    switch (tab.m_type) {
    case 2:
    case 5: // kintou waritsuke
      newTab.m_alignment = MWAWTabStop::CENTER;
      break;
    case 3:
      newTab.m_alignment = MWAWTabStop::RIGHT;
      break;
    case 4:
      newTab.m_alignment = MWAWTabStop::DECIMAL;
      break;
    case 1: // left
    default:
      break;
    }
    newTab.m_leaderCharacter=tab.m_leaderChar;
    para.m_tabs->push_back(newTab);
  }
  if (totalWidth>0 && style.m_numColumns>1)
    section.setColumns(style.m_numColumns, totalWidth/double(style.m_numColumns), librevenge::RVNG_POINT, style.m_columnGap>0 ? style.m_columnGap/72. : 0.05);
  return true;
}

bool RagTime5StyleManager::readFormats(RagTime5ClusterManager::Cluster &cluster)
{
  RagTime5ClusterManager::Link const &link=cluster.m_dataLink;
  if (link.m_ids.size()<2 || !link.m_ids[1])
    return false;

  std::map<int, librevenge::RVNGString> idToNameMap;
  if (!cluster.m_nameLink.empty()) {
    m_document.readUnicodeStringList(cluster.m_nameLink, idToNameMap);
    cluster.m_nameLink=RagTime5ClusterManager::NameLink();
  }
  std::vector<long> decal;
  if (link.m_ids[0])
    m_document.readPositions(link.m_ids[0], decal);
  if (decal.empty())
    decal=link.m_longList;
  int const dataId=link.m_ids[1];
  auto dataZone=m_document.getDataZone(dataId);
  auto N=int(decal.size());

  if (!dataZone || !dataZone->m_entry.valid() ||
      dataZone->getKindLastPart(dataZone->m_kinds[1].empty())!="ItemData" || N<=1) {
    if (N==1 && dataZone && !dataZone->m_entry.valid()) {
      // a zone with 0 zone is ok...
      dataZone->m_isParsed=true;
      return true;
    }
    MWAW_DEBUG_MSG(("RagTime5StyleManager::readFormats: the data zone %d seems bad\n", dataId));
    return false;
  }

  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  f << "Entries(FormatDef)[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  long debPos=entry.begin();
  long endPos=entry.end();
  if (!input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::readFormats:bad endPos\n"));
    return false;
  }
  m_state->m_formatList.resize(size_t(N ? N-1 : 0));
  for (int i=1; i<N; ++i) {
    long pos=debPos+decal[size_t(i-1)], endDPos=debPos+decal[size_t(i)];
    if (pos==endDPos) continue;
    if (pos<debPos || endDPos>endPos || endDPos-pos<4) {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readFormats: can not read the data zone %d-%d seems bad\n", dataId, i));
      continue;
    }
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "FormatDef-Fo" << i << ":";
    auto val=static_cast<int>(input->readLong(4));
    if (val) f << "num[used]=" << val << ",";
    if (endDPos-pos<10) {
      if (endDPos!=pos+4) f << "###";
      ascFile.addPos(pos);
      ascFile.addNote(f.str().c_str());
      continue;
    }
    auto nId=static_cast<int>(input->readLong(2));
    if (idToNameMap.find(i)!=idToNameMap.end())
      f << "\"" << idToNameMap.find(i)->second.cstr() << "\",";
    else {
      MWAW_DEBUG_MSG(("RagTime5StyleManager::readFormats: can not find the format name for zone %d\n", dataId));
      f << "###name[id]=" << nId << ",";
    }
    auto numFormat=static_cast<int>(input->readLong(1));
    if (numFormat!=1)
      f << "numFormat=" << numFormat << ",";
    auto type=static_cast<int>(input->readLong(1)); // 6, 10 abd one time 4(slide number) and 14(unknown)
    if (type==10) f << "dateTime,";
    else if (type!=6) f << "#type=" << type << ",";
    for (int fo=0; fo<numFormat; ++fo) {
      MWAWCell::Format format;
      f << "form" << fo << "=[";
      auto type2=static_cast<int>(input->readULong(1));
      bool isDateTime=false, isMoneyThousand=false, isCurrency=false;
      switch (type2) {
      case 0:
        f << "general,";
        format.m_format=MWAWCell::F_NUMBER;
        format.m_numberFormat=MWAWCell::F_NUMBER_GENERIC;
        break;
      case 1: // number normal
        format.m_format=MWAWCell::F_NUMBER;
        format.m_numberFormat=MWAWCell::F_NUMBER_DECIMAL;
        break;
      case 4:
        f << "money/thousand,";
        format.m_format=MWAWCell::F_NUMBER;
        format.m_numberFormat=MWAWCell::F_NUMBER_DECIMAL;
        isMoneyThousand=true;
        break;
      default:
        isDateTime=true;
        format.m_format=MWAWCell::F_DATE; // or time
        if (type2&0x80) f << "%a";
        if (type2&0x40) f << "%y";
        if (type2&0x20) f << "%m";
        if (type2&8) f << "%d";
        if (type2&4) f << "%H";
        if (type2&2) f << "%M";
        if (type2&1) f << "%S";
        if (type2&0x10)
          f << "#type2[high]";
        f << ",";
        break;
      }
      val=static_cast<int>(input->readULong(1));
      if (val) f << "num[decim]=" << val << ",";
      if (format.m_format==MWAWCell::F_NUMBER && format.m_numberFormat!=MWAWCell::F_NUMBER_GENERIC)
        format.m_digits=val;
      for (int j=0; j<4; ++j) {
        val=static_cast<int>(input->readULong(1));
        if (val) f << "fl" << j << "=" << std::hex << val << std::dec << ",";
      }
      auto fSz=static_cast<int>(input->readULong(1));
      if (input->tell()+fSz>endDPos) {
        MWAW_DEBUG_MSG(("RagTime5StyleManager::readFormats: can not read the string format zone %d\n", dataId));
        f << "###fSz=" << fSz << ",";
        break;
      }
      f << "format=\"";
      for (int j=0; j<fSz; ++j) {
        val=static_cast<int>(input->readULong(1));
        if (isMoneyThousand && val!=2 && val!=3 && val!=5 && !isCurrency)
          isCurrency=true;
        switch (val) {
        case 1: // general digit
          f << "*";
          break;
        case 2: // decimal digit
          f << "0";
          break;
        case 3: // potential digit (ie. diese)
          f << "1";
          break;
        case 5: // commas
          f << ".";
          if (isDateTime)
            format.m_DTFormat.append(".");
          break;
        case 6:
          if (isDateTime)
            format.m_DTFormat.append("%y");
          f << "%y";
          break;
        case 7: // year or fraction
          if (isDateTime) {
            format.m_DTFormat.append("%Y");
            f << "%Y";
            break;
          }
          f << "/";
          if (format.m_format==MWAWCell::F_NUMBER && format.m_numberFormat==MWAWCell::F_NUMBER_DECIMAL)
            format.m_numberFormat=MWAWCell::F_NUMBER_FRACTION;
          break;
        case 8:
          if (isDateTime)
            format.m_DTFormat.append("%m");
          f << "%m";
          break;
        case 9: // month with two digits
          if (isDateTime)
            format.m_DTFormat.append("%m");
          f << "%0m";
          break;
        case 0xa: // month abbrev or exponant
          if (isDateTime) {
            format.m_DTFormat.append("%b");
            f << "%b";
            break;
          }
          f << "e";
          if (format.m_format==MWAWCell::F_NUMBER && format.m_numberFormat==MWAWCell::F_NUMBER_DECIMAL)
            format.m_numberFormat=MWAWCell::F_NUMBER_SCIENTIFIC;
          break;
        case 0xb: // month
          if (isDateTime)
            format.m_DTFormat.append("%B");
          f << "%B";
          break;
        case 0xc: // day or percent
          if (isDateTime) {
            format.m_DTFormat.append("%d");
            f << "%d";
            break;
          }
          f << "%";
          if (format.m_format==MWAWCell::F_NUMBER && format.m_numberFormat==MWAWCell::F_NUMBER_DECIMAL)
            format.m_numberFormat=MWAWCell::F_NUMBER_PERCENT;
          break;
        case 0xd: // day 2 digits
          if (isDateTime)
            format.m_DTFormat.append("%d");
          f << "%0d";
          break;
        case 0xe: // checkme
          if (isDateTime)
            format.m_DTFormat.append("%a");
          f << "%a";
          break;
        case 0xf:
          if (isDateTime)
            format.m_DTFormat.append("%A");
          f << "%A";
          break;
        case 0x10: // pm (precedeed by c0)
          if (isDateTime)
            format.m_DTFormat.append("%p");
          f << "%p";
          break;
        case 0x14: // hour
          if (isDateTime)
            format.m_DTFormat.append("%H");
          f << "%0H";
          break;
        case 0x15:
          if (isDateTime)
            format.m_DTFormat.append("%H");
          f << "%H";
          break;
        case 0x16:
          if (isDateTime)
            format.m_DTFormat.append("%M");
          f << "%0M";
          break;
        case 0x17: // minute
          if (isDateTime)
            format.m_DTFormat.append("%M");
          f << "%M";
          break;
        case 0x19: // second
          if (isDateTime)
            format.m_DTFormat.append("%S");
          f << "%S";
          break;
        case 0x1f:
          if (isDateTime)
            format.m_DTFormat.append("%p");
          f << "%p";
          break;
        case 0xa3: // pound symbol
          f << "[pound]";
          break;
        case 0xc0: // pm/am condition?
          if (j+1>=fSz) {
            f << "[##c0]";
            break;
          }
          break;
        case 0xfd: // parenthesis delimiter ?
          if (j+1>=fSz) {
            f << "[##fd]";
            break;
          }
          ++j;
          input->seek(1, librevenge::RVNG_SEEK_CUR);
          break;
        case 0xff: // unicode
          if (j+2>=fSz) {
            f << "[##ff]";
            break;
          }
          j+=2;
          f << "[U" << std::hex << input->readULong(2) << std::dec << "]";
          break;
        default:
          if (val>=0x20 && val<0x80) {
            f << char(val);
            if (isDateTime)
              format.m_DTFormat+=char(val);
            else if (format.m_format==MWAWCell::F_NUMBER && val=='(')
              format.m_parenthesesForNegative=true;
          }
          else
            f << "[#" << std::hex << val << std::dec << "]";
          break;
        }
      }
      f << "\",";
      f << "],";
      if (isCurrency)
        format.m_numberFormat=MWAWCell::F_NUMBER_CURRENCY;
      else if (isMoneyThousand)
        format.m_thousandHasSeparator=true;
      if (fo==0)
        m_state->m_formatList[size_t(i-1)]=format;
    }
    f << "],";

    if (input->tell()!=endDPos)
      ascFile.addDelimiter(input->tell(),'|');
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
  }

  input->setReadInverted(false);

  for (auto lnk : cluster.m_linksList) {
    lnk.m_name=std::string("FormatUnkn")+(lnk.m_fileType[0]==0x3e800 ? "A" : lnk.m_fileType[0]==0x35800 ? "B" : lnk.getZoneName().c_str());
    if (lnk.m_fileType[0]==0x3e800 || lnk.m_fileType[0]==0x35800) {
      /* rare only find in two files,
        FormA: list of 0 or small int: next list?,
        FormB: list of 0, 80000000 or small int: prev list ?
        when the value are small ints, FormB(FormA(val)+1)=val
      */
      std::vector<long> data;
      m_document.readLongList(lnk, data);
    }
    else
      m_document.readFixedSizeZone(lnk, lnk.m_name);
  }

  return true;
}

bool RagTime5StyleManager::updateCellFormat(int formatId, MWAWCell &cell) const
{
  if (formatId<=0 || formatId>static_cast<int>(m_state->m_formatList.size())) {
    MWAW_DEBUG_MSG(("RagTime5StyleManager::updateCellFormat: can not find format %d\n", formatId));
    return false;
  }
  auto format=m_state->m_formatList[size_t(formatId-1)];
  auto cellType=cell.getFormat().m_format;
  if (cellType==format.m_format && (cellType==MWAWCell::F_NUMBER || cellType==MWAWCell::F_DATE))
    cell.setFormat(format);
  else if (cellType==MWAWCell::F_TIME && format.m_format==MWAWCell::F_DATE) {
    format.m_format=MWAWCell::F_TIME;
    cell.setFormat(format);
  }
  return true;
}

////////////////////////////////////////////////////////////
// parse cluster
////////////////////////////////////////////////////////////

//
// graphic style
//
RagTime5StyleManager::GraphicStyle::~GraphicStyle()
{
}

bool RagTime5StyleManager::GraphicStyle::read(MWAWInputStreamPtr &input, RagTime5StructManager::Field const &field, std::vector<MWAWColor> const &colorList)
{
  std::stringstream s;
  if (field.m_type==RagTime5StructManager::Field::T_Long) { // header
    switch (field.m_fileType) {
    case 0x148c042: // -2<->8
      if (field.m_longValue[0])
        s << "H" << RagTime5StyleManager::printType(field.m_fileType) << "=" << field.m_longValue[0] << ",";
      else
        s << "H" << RagTime5StyleManager::printType(field.m_fileType) << ",";
      m_extra+=s.str();
      return true;
    case 0x1460042: // -3-23
      s << "lineStyle,";
      if (field.m_longValue[0]!=-3)
        s << "pId?=" << field.m_longValue[0] << ",";
      m_extra += s.str();
      return true;
    case 0x145e042: // -2<->24 : fill style CHECKME related to parent id?
    case 0x1489842: // -2<->19
      m_parentId=static_cast<int>(field.m_longValue[0]);
      return true;
    default:
      return false;
    }
  }
  else if (field.m_type==RagTime5StructManager::Field::T_FieldList) {
    switch (field.m_fileType) {
    case 0x7d02a:
    case 0x145e05a: {
      int wh=field.m_fileType==0x7d02a ? 0 : 1;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Color && child.m_fileType==0x84040) {
          if (child.m_longValue[0]==50) {
            if (!updateColor(field.m_fileType==0x7d02a, int(child.m_longValue[1])+1, colorList)) {
              MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown color %ld block\n", child.m_longValue[1]+1));
              s << "###";
            }
            s << "col=GC" << child.m_longValue[1]+1 << ",";
            continue;
          }
          m_colors[wh]=child.m_color;
          m_colorsAlpha[wh]=1; // checkme
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown color %d block\n", wh));
        s << "##col[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x145e02a:
    case 0x145e0ea: {
      int wh=field.m_fileType==0x145e02a ? 0 : 1;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0xb6000) {
          m_colorsAlpha[wh]=float(child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown colorAlpha[%d] block\n", wh));
        s << "###colorAlpha[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x145e01a: {
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x147c080) {
          if (m_parentId>-1000) {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: parent id is already set\n"));
            s << "###newParentId,";
          }
          m_parentId=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown parent block\n"));
        s << "###parent=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x7d04a:
      for (auto  const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1494800) {
          m_width=float(child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown width block\n"));
        s << "###w=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x145e0ba: {
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          m_hidden=child.m_longValue[0]!=0;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown no print block\n"));
        s << "###hidden=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }

    case 0x14600ca:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==(unsigned long)(long(0x80033000))) {
          m_dash=child.m_longList;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown dash block\n"));
        s << "###dash=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x146005a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="LiOu")
            m_position=3;
          else if (child.m_string=="LiCe") // checkme
            m_position=2;
          else if (child.m_string=="LiIn")
            m_position=1;
          else if (child.m_string=="LiRo")
            m_position=4;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown position string %s\n", child.m_string.cstr()));
            s << "##pos=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown position block\n"));
        s << "###pos=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x146007a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="LiRo")
            m_mitter=2;
          else if (child.m_string=="LiBe")
            m_mitter=3;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown mitter string %s\n", child.m_string.cstr()));
            s << "##mitter=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown mitter block\n"));
        s << "###mitter=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x148981a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="GrNo")
            m_gradient=1;
          else if (child.m_string=="GrRa")
            m_gradient=2;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown gradient string %s\n", child.m_string.cstr()));
            s << "##gradient=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown gradient block\n"));
        s << "###gradient=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x14600aa:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="CaRo")
            m_cap=2;
          else if (child.m_string=="CaSq")
            m_cap=3;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown cap string %s\n", child.m_string.cstr()));
            s << "##cap=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown cap block\n"));
        s << "###cap=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x148985a: // checkme
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1495800) {
          m_gradientRotation=float(360*child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown grad rotation block\n"));
        s << "###rot[grad]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x148983a: // checkme
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_DoubleList && child.m_doubleList.size()==2 && child.m_fileType==0x74040) {
          m_gradientCenter=MWAWVec2f(float(child.m_doubleList[0]), float(child.m_doubleList[1]));
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown grad center block\n"));
        s << "###rot[center]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x146008a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0xb6000) {
          m_limitPercent=float(child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown limit percent block\n"));
        s << "###limitPercent=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    // unknown small id
    case 0x145e11a: // frequent
    case 0x145e12a: {  // unknown small int 2|3
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x17d5880) {
          if (!updateColor(field.m_fileType==0x145e11a, int(child.m_longValue[0]), colorList)) {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown color %ld block\n", child.m_longValue[0]));
            s << "###";
          }
          s << "col=GC" << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some unknown unkn0 block\n"));
        s << "###unkn0=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    default:
      break;
    }
  }
  else if (field.m_type==RagTime5StructManager::Field::T_Unstructured) {
    switch (field.m_fileType) {
    case 0x148c01a: {
      if (field.m_entry.length()!=12) {
        MWAW_DEBUG_MSG(("RagTime5StyleManager::GraphicStyle::read: find some odd size for pattern\n"));
        s << "##pattern=" << field << ",";
        m_extra+=s.str();
        return true;
      }
      input->seek(field.m_entry.begin(), librevenge::RVNG_SEEK_SET);
      for (int i=0; i<2; ++i) {
        static int const expected[]= {0xb, 0x40};
        auto val=static_cast<int>(input->readULong(2));
        if (val!=expected[i])
          s << "pat" << i << "=" << std::hex << val << std::dec << ",";
      }
      m_pattern.reset(new MWAWGraphicStyle::Pattern);
      m_pattern->m_colors[0]=MWAWColor::white();
      m_pattern->m_colors[1]=MWAWColor::black();
      m_pattern->m_dim=MWAWVec2i(8,8);
      m_pattern->m_data.resize(8);
      for (auto &data : m_pattern->m_data) data=static_cast<unsigned char>(input->readULong(1));
      m_extra+=s.str();
      return true;
    }
    default:
      break;
    }
  }
  return false;
}

bool RagTime5StyleManager::GraphicStyle::updateColor(bool first, int colorId, std::vector<MWAWColor> const &colorList)
{
  if (colorId>=1 && colorId <= int(colorList.size())) {
    auto const &color=colorList[size_t(colorId-1)];
    m_colors[first ? 0 : 1]=color;
    if (color.getAlpha()<255)
      m_colorsAlpha[first ? 0 : 1]=float(color.getAlpha())/255.f;
    return true;
  }
  return false;
}

void RagTime5StyleManager::GraphicStyle::insert(RagTime5StyleManager::GraphicStyle const &childStyle)
{
  if (childStyle.m_width>=0) m_width=childStyle.m_width;
  bool updateCol=true;
  if (childStyle.m_dash.isSet()) m_dash=childStyle.m_dash;
  if (childStyle.m_pattern) m_pattern=childStyle.m_pattern;
  if (childStyle.m_gradient>=0) m_gradient=childStyle.m_gradient;
  else if (m_gradient==1) updateCol=false; // we need to use the gradient color
  if (childStyle.m_gradientRotation>-1000) m_gradientRotation=childStyle.m_gradientRotation;
  if (childStyle.m_gradientCenter.isSet()) m_gradientCenter=childStyle.m_gradientCenter;
  if (childStyle.m_position>=0) m_position=childStyle.m_position;
  if (childStyle.m_cap>=0) m_cap=childStyle.m_cap;
  if (childStyle.m_mitter>=0) m_mitter=childStyle.m_mitter;
  if (childStyle.m_limitPercent>=0) m_limitPercent=childStyle.m_limitPercent;
  if (childStyle.m_hidden.isSet()) m_hidden=childStyle.m_hidden;
  if (updateCol) {
    if (childStyle.m_colors[0].isSet()) m_colors[0]=*childStyle.m_colors[0];
    if (childStyle.m_colors[1].isSet()) m_colors[1]=*childStyle.m_colors[1];
    for (int i=0; i<2; ++i) {
      if (childStyle.m_colorsAlpha[i]>=0)
        m_colorsAlpha[i]=childStyle.m_colorsAlpha[i];
    }
  }
  m_extra+=childStyle.m_extra;
}

std::ostream &operator<<(std::ostream &o, RagTime5StyleManager::GraphicStyle const &style)
{
  if (style.m_parentId>-1000) {
    if (style.m_parentId<0)
      o << "parent=def" << -style.m_parentId << ",";
    else if (style.m_parentId)
      o << "parent=GS" << style.m_parentId << ",";
  }
  if (style.m_width>=0) o << "w=" << style.m_width << ",";
  if (style.m_colors[0].isSet()) o << "color0=" << *style.m_colors[0] << ",";
  if (style.m_colors[1].isSet()) o << "color1=" << *style.m_colors[1] << ",";
  for (int i=0; i<2; ++i) {
    if (style.m_colorsAlpha[i]>=0)
      o << "color" << i << "[alpha]=" << style.m_colorsAlpha[i] << ",";
  }
  if (style.m_dash.isSet()) {
    o << "dash=";
    for (auto dash : *style.m_dash)
      o << dash << ":";
    o << ",";
  }
  if (style.m_pattern)
    o << "pattern=[" << *style.m_pattern << "],";
  switch (style.m_gradient) {
  case -1:
    break;
  case 0:
    break;
  case 1:
    o << "grad[normal],";
    break;
  case 2:
    o << "grad[radial],";
    break;
  default:
    o<< "##gradient=" << style.m_gradient;
    break;
  }
  if (style.m_gradientRotation>-1000 && (style.m_gradientRotation<0 || style.m_gradientRotation>0))
    o << "rot[grad]=" << style.m_gradientRotation << ",";
  if (style.m_gradientCenter.isSet())
    o << "center[grad]=" << *style.m_gradientCenter << ",";
  switch (style.m_position) {
  case -1:
    break;
  case 1:
    o << "pos[inside],";
    break;
  case 2:
    break;
  case 3:
    o << "pos[outside],";
    break;
  case 4:
    o << "pos[round],";
    break;
  default:
    o << "#pos=" << style.m_position << ",";
    break;
  }
  switch (style.m_cap) {
  case -1:
    break;
  case 1: // triangle
    break;
  case 2:
    o << "cap[round],";
    break;
  case 3:
    o << "cap[square],";
    break;
  default:
    o << "#cap=" << style.m_cap << ",";
    break;
  }
  switch (style.m_mitter) {
  case -1:
    break;
  case 1: // no add
    break;
  case 2:
    o << "mitter[round],";
    break;
  case 3:
    o << "mitter[out],";
    break;
  default:
    o << "#mitter=" << style.m_mitter << ",";
    break;
  }
  if (style.m_limitPercent>=0 && style.m_limitPercent<1)
    o << "limit=" << 100*style.m_limitPercent << "%,";
  if (style.m_hidden.get())
    o << "hidden,";
  o << style.m_extra;
  return o;
}

//
// text style
//
RagTime5StyleManager::TextStyle::~TextStyle()
{
}

std::string RagTime5StyleManager::TextStyle::getLanguageLocale(int id)
{
  switch (id) {
  case 1:
    return "hr_HR";
  case 4:
    return "ru_RU";
  case 8:
    return "da_DK";
  case 9:
    return "sv_SE";
  case 0xa:
    return "nl_NL";
  case 0xb:
    return "fi_FI";
  case 0xc:
    return "it_IT";
  case 0xd: // initial accent
  case 0x800d:
    return "es_ES";
  case 0xf:
    return "gr_GR";
  case 0x11:
    return "ja_JP";
  case 0x16:
    return "tr_TR";
  case 0x4005:
  case 0x8005: // initial accent
    return "fr_FR";
  case 0x4006: // old?
  case 0x6006:
    return "de_CH";
  case 0x8006: // old?
  case 0xa006:
    return "de_DE";
  case 0x4007:
    return "en_GB";
  case 0x8007:
    return "en_US";
  case 0x400e:
    return "pt_BR";
  case 0x800e:
    return "pt_PT";
  case 0x4012:
    return "nn_NO";
  case 0x8012:
    return "no_NO";
  default:
    break;
  }
  return "";
}

bool RagTime5StyleManager::TextStyle::read(RagTime5StructManager::Field const &field)
{
  std::stringstream s;
  if (field.m_type==RagTime5StructManager::Field::T_Long) { // header
    switch (field.m_fileType) {
    case 0: // one time with 0
      return true;
    case 0x1475042: // -3<->32 : ?
    case 0x147e842: // always 0?
    case 0x14b2042: // always 0?
      if (field.m_longValue[0])
        s << "H" << RagTime5StyleManager::printType(field.m_fileType) << "=" << field.m_longValue[0] << ",";
      else
        s << "H" << RagTime5StyleManager::printType(field.m_fileType) << ",";
      m_extra+=s.str();
      return true;
    case 0x1474042: // -1<->39 : CHECKME related to parent id?
      s << "parent[id]?=" << field.m_longValue[0] << ",";
      m_extra+=s.str();
      return true;
    default:
      return false;
    }
  }
  else if (field.m_type==RagTime5StructManager::Field::T_FieldList) {
    switch (field.m_fileType) {
    case 0x7a0aa: // style parent id?
    case 0x1474042: // main parent id?
    case 0x147551a: { // find one time with 3
      int wh=field.m_fileType==0x1474042 ? 0 : 1;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x1479080) {
          if (field.m_fileType==0x147551a)
            s << "unkn[pId]=" << child.m_longValue[0] << ",";
          else
            m_parentId[wh]=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown parent id[%d] block\n", wh));
        s << "###parent" << wh << "[id]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x14741fa:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_LongList && child.m_fileType==(unsigned long)(long(0x80045080))) {
          for (auto val : child.m_longList)
            m_linkIdList.push_back(static_cast<int>(val));
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown link id block\n"));
        s << "###link[id]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x1469840:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x147b880) {
          m_dateStyleId=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown date style block\n"));
        s << "###date[id]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x145e01a:
    case 0x14741ea:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x147c080) {
          if (field.m_fileType==0x145e01a)
            m_graphStyleId=static_cast<int>(child.m_longValue[0]);
          else
            m_graphLineStyleId=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown graphic style block\n"));
        s << "###graph[" << RagTime5StyleManager::printType(field.m_fileType) << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    //
    // para
    //
    case 0x14750ea:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          m_keepWithNext=child.m_longValue[0]!=0;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown keep with next block\n"));
        s << "###keep[withNext]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147505a: // left margin
    case 0x147506a: // right margin
    case 0x147507a: { // first margin
      auto wh=int(((field.m_fileType&0xF0)>>4)-5);
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1493800) {
          m_margins[wh]=child.m_doubleValue;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown margins[%d] block\n", wh));
        s << "###margins[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x147501a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="----") // checkme
            m_justify=-1;
          else if (child.m_string=="left")
            m_justify=0;
          else if (child.m_string=="cent")
            m_justify=1;
          else if (child.m_string=="rght")
            m_justify=2;
          else if (child.m_string=="full")
            m_justify=3;
          else if (child.m_string=="fful")
            m_justify=4;
          // find also thgr
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some justify block %s\n", child.m_string.cstr()));
            s << "##justify=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown justify block\n"));
        s << "###justify=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147502a:
    case 0x14750aa:
    case 0x14750ba: {
      int wh=field.m_fileType==0x147502a ? 0 : field.m_fileType==0x14750aa ? 1 : 2;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_LongDouble && child.m_fileType==0x149a940) {
          m_spacings[wh]=child.m_doubleValue;
          m_spacingUnits[wh]=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown spacings %d block\n", wh));
        s << "###spacings[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x14752da:
    case 0x147536a:
    case 0x147538a: {
      int wh=field.m_fileType==0x14752da ? 0 : field.m_fileType==0x147536a ? 1 : 2;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1495000) {
          s << "delta[" << (wh==0 ? "interline" : wh==1 ? "before" : "after") << "]=" << child.m_doubleValue << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown spacings delta %d block\n", wh));
        s << "###delta[spacings" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x147530a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="----") // checkme
            m_breakMethod=0;
          else if (child.m_string=="nxtC")
            m_breakMethod=1;
          else if (child.m_string=="nxtP")
            m_breakMethod=2;
          else if (child.m_string=="nxtE")
            m_breakMethod=3;
          else if (child.m_string=="nxtO")
            m_breakMethod=4;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown break method block %s\n", child.m_string.cstr()));
            s << "##break[method]=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown break method block\n"));
        s << "###break[method]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147550a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          if (child.m_longValue[0])
            s << "text[margins]=canOverlap,";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown text margin overlap block\n"));
        s << "###text[margins]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147516a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          if (child.m_longValue[0])
            s << "line[align]=ongrid,";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown line grid align block\n"));
        s << "###line[gridalign]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147546a:
    case 0x147548a:
    case 0x14754aa: { // find one time with 1
      std::string wh(field.m_fileType==0x147546a ? "orphan" :
                     field.m_fileType==0x147548a ?  "widows" : "unkn54aa");
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x328c0) {
          s << wh << "=" << child.m_longValue[0];
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown number %s block\n", wh.c_str()));
        s << "###" << wh << "=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x14754ba:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Unstructured && child.m_fileType==0x1476840) {
          // height in line, number of character, first line with text, scaling
          s << "drop[initial]=" << child.m_extra << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown drop initial block\n"));
        s << "###drop[initial]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x14750ca: // one tab, remove tab?
    case 0x147510a:
      if (field.m_fileType==0x14750ca) s << "#tab0";
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_TabList && (child.m_fileType==(unsigned long)(long(0x81474040)) || child.m_fileType==0x1474040)) {
          m_tabList=child.m_tabList;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown tab block\n"));
        s << "###tab=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    //
    // char
    //
    case 0x7a05a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1495000) {
          m_fontSize=float(child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown font size block\n"));
        s << "###size[font]=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    case 0xa7017:
    case 0xa7037:
    case 0xa7047:
    case 0xa7057:
    case 0xa7067: {
      auto wh=int(((field.m_fileType&0x70)>>4)-1);
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Unicode && child.m_fileType==0xc8042) {
          if (wh==2)
            m_fontName=child.m_string;
          else {
            static char const *what[]= {"[full]" /* unsure */, "[##UNDEF]", "", "[style]" /* regular, ...*/, "[from]", "[full2]"};
            s << "font" << what[wh] << "=\"" << child.m_string.cstr() << "\",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some font name[%d] block\n", wh));
        s << "###font[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0xa7077:
    case 0x147407a:
    case 0x147408a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x3b880) {
          switch (field.m_fileType) {
          case 0xa7077:
            m_fontId=static_cast<int>(child.m_longValue[0]);
            break;
          case 0x147407a:
            s << "hyph[minSyl]=" << child.m_longValue[0] << ",";
            break;
          case 0x147408a:
            s << "hyph[minWord]=" << child.m_longValue[0] << ",";
            break;
          default:
            MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown long=%lx\n", static_cast<unsigned long>(field.m_fileType)));
            break;
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown long=%lx block\n", static_cast<unsigned long>(field.m_fileType)));
        s << "###long[" << RagTime5StyleManager::printType(field.m_fileType) << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x7a09a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_2Long && child.m_fileType==0xa4840) {
          m_fontFlags[0]=static_cast<uint32_t>(child.m_longValue[0]);
          m_fontFlags[1]=static_cast<uint32_t>(child.m_longValue[1]);
          continue;
        }
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0xa4000) {
          m_fontFlags[0]=static_cast<uint32_t>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown font flags block\n"));
        s << "###flags[font]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x14740ba:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="----") // checkme
            m_underline=0;
          else if (child.m_string=="undl")
            m_underline=1;
          else if (child.m_string=="Dund")
            m_underline=2;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown underline block %s\n", child.m_string.cstr()));
            s << "##underline=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some underline block\n"));
        s << "###underline=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147403a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Code && child.m_fileType==0x8d000) {
          if (child.m_string=="----") // checkme
            m_caps=0;
          else if (child.m_string=="alcp")
            m_caps=1;
          else if (child.m_string=="lowc")
            m_caps=2;
          else if (child.m_string=="Icas")
            m_caps=3;
          else {
            MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown caps block %s\n", child.m_string.cstr()));
            s << "##caps=" << child.m_string.cstr() << ",";
          }
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some caps block\n"));
        s << "###caps=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x14753aa: // min spacing
    case 0x14753ca: // optimal spacing
    case 0x14753ea: { // max spacing
      int wh=field.m_fileType==0x14753aa ? 2 : field.m_fileType==0x14753ca ? 1 : 3;
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0xb6000) {
          m_letterSpacings[wh]=child.m_doubleValue;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown spacings[%d] block\n", wh));
        s << "###spacings[" << wh << "]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }

    case 0x147404a: // space scaling
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_LongDouble && child.m_fileType==0x149c940) {
          m_letterSpacings[0]=child.m_doubleValue;
          // no sure what do to about this int : a number between 0 and 256...
          if (child.m_longValue[0]) s << "[" << child.m_longValue[0] << "],";
          else s << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown space scaling block\n"));
        s << "###space[scaling]=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    case 0x147405a: // script position
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_LongDouble && child.m_fileType==0x149c940) {
          m_scriptPosition=float(child.m_doubleValue);
          if ((child.m_doubleValue<0 || child.m_doubleValue>0) && m_fontScaling<0)
            m_fontScaling=0.75;
          // no sure what do to about this int : a number between 0 and 256...
          if (child.m_longValue[0]) s << "script2[pos]?=" << child.m_longValue[0] << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown font script block\n"));
        s << "###font[script]=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    case 0x14741ba:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0xb6000) {
          m_fontScaling=float(child.m_doubleValue);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown font scaling block\n"));
        s << "###scaling=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    case 0x14740ea: // horizontal streching
    case 0x147418a: // small cap horizontal scaling
    case 0x14741aa: { // small cap vertical scaling
      std::string wh(field.m_fileType==0x14740ea ? "font[strech]" :
                     field.m_fileType==0x147418a ? "font[smallScaleH]" : "font[smallScaleV]");
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0xb6000) {
          if (field.m_fileType==0x14740ea)
            m_widthStreching=child.m_doubleValue;
          else
            s << wh << "=" << child.m_doubleValue << ",";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown %s block\n", wh.c_str()));
        s << "###" << wh << "=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x147406a: // automatic hyphenation
    case 0x147552a: { // ignore 1 word ( for spacings )
      std::string wh(field.m_fileType==0x147406a ? "hyphen" : "spacings[ignore1Word]");
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Bool && child.m_fileType==0x360c0) {
          if (child.m_longValue[0])
            s << wh << ",";
          else
            s << wh << "=no,";
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown %s block\n", wh.c_str()));
        s << "###" << wh << "=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    }
    case 0x147402a: // language
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x34080) {
          m_language=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown language block\n"));
        s << "###language=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    //
    // columns
    //
    case 0x147512a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Long && child.m_fileType==0x328c0) {
          m_numColumns=static_cast<int>(child.m_longValue[0]);
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown column's number block\n"));
        s << "###num[cols]=" << child << ",";
      }
      m_extra+=s.str();
      return true;
    case 0x147513a:
      for (auto const &child : field.m_fieldList) {
        if (child.m_type==RagTime5StructManager::Field::T_Double && child.m_fileType==0x1493800) {
          m_columnGap=child.m_doubleValue;
          continue;
        }
        MWAW_DEBUG_MSG(("RagTime5StyleManager::TextStyle::read: find some unknown columns gaps block\n"));
        s << "###col[gap]=" << child << ",";
      }
      m_extra+=s.str();
      return true;

    default:
      break;
    }
  }
  return false;
}

void RagTime5StyleManager::TextStyle::insert(RagTime5StyleManager::TextStyle const &child)
{
  if (!child.m_linkIdList.empty()) m_linkIdList=child.m_linkIdList; // usefull?
  if (child.m_graphStyleId>=0) m_graphStyleId=child.m_graphStyleId;
  if (child.m_graphLineStyleId>=0) m_graphLineStyleId=child.m_graphLineStyleId;
  if (child.m_dateStyleId>=0) m_dateStyleId=child.m_dateStyleId;
  if (child.m_keepWithNext.isSet()) m_keepWithNext=child.m_keepWithNext;
  if (child.m_justify>=0) m_justify=child.m_justify;
  if (child.m_breakMethod>=0) m_breakMethod=child.m_breakMethod;
  for (int i=0; i<3; ++i) {
    if (child.m_margins[i]>=0) m_margins[i]=child.m_margins[i];
  }
  for (int i=0; i<3; ++i) {
    if (child.m_spacings[i]<0) continue;
    m_spacings[i]=child.m_spacings[i];
    m_spacingUnits[i]=child.m_spacingUnits[i];
  }
  if (!child.m_tabList.empty()) m_tabList=child.m_tabList; // append ?
  // char
  if (!child.m_fontName.empty()) m_fontName=child.m_fontName;
  if (child.m_fontId>=0) m_fontId=child.m_fontId;
  if (child.m_fontSize>=0) m_fontSize=child.m_fontSize;
  for (int i=0; i<2; ++i) {
    uint32_t fl=child.m_fontFlags[i];
    if (!fl) continue;
    if (i==0) m_fontFlags[0]|=fl;
    else m_fontFlags[0]&=(~fl);
  }
  if (child.m_caps>=0) m_caps=child.m_caps;
  if (child.m_underline>=0) m_underline=child.m_underline;
  if (child.m_scriptPosition.isSet()) m_scriptPosition=child.m_scriptPosition;
  if (child.m_fontScaling>=0) m_fontScaling=child.m_fontScaling;

  for (int i=0; i<4; ++i) {
    if (child.m_letterSpacings[i]>0 || child.m_letterSpacings[i]<0)
      m_letterSpacings[i]=child.m_letterSpacings[i];
  }
  if (child.m_language>=0) m_language=child.m_language;
  if (child.m_widthStreching>=0) m_widthStreching=child.m_widthStreching;
  // column
  if (child.m_numColumns>=0) m_numColumns=child.m_numColumns;
  if (child.m_columnGap>=0) m_columnGap=child.m_columnGap;
}

std::ostream &operator<<(std::ostream &o, RagTime5StyleManager::TextStyle const &style)
{
  if (style.m_parentId[0]>=0) o << "parent=TS" << style.m_parentId[0] << ",";
  if (style.m_parentId[1]>=0) o << "parent[style?]=TS" << style.m_parentId[1] << ",";
  if (!style.m_linkIdList.empty()) {
    // fixme: 3 text style's id values with unknown meaning, probably important...
    o << "link=[";
    for (auto id : style.m_linkIdList)
      o << "TS" << id << ",";
    o << "],";
  }
  if (style.m_graphStyleId>=0) o << "graph[id]=GS" << style.m_graphStyleId << ",";
  if (style.m_graphLineStyleId>=0) o << "graphLine[id]=GS" << style.m_graphLineStyleId << ",";
  if (style.m_dateStyleId>=0) o << "date[id]=DS" << style.m_dateStyleId << ",";
  if (style.m_keepWithNext.isSet()) {
    o << "keep[withNext]";
    if (!*style.m_keepWithNext)
      o << "=false,";
    else
      o << ",";
  }
  switch (style.m_justify) {
  case 0: // left
    break;
  case 1:
    o << "justify=center,";
    break;
  case 2:
    o << "justify=right,";
    break;
  case 3:
    o << "justify=full,";
    break;
  case 4:
    o << "justify=full[all],";
    break;
  default:
    if (style.m_justify>=0)
      o << "##justify=" << style.m_justify << ",";
  }

  switch (style.m_breakMethod) {
  case 0: // as is
    break;
  case 1:
    o << "break[method]=next[container],";
    break;
  case 2:
    o << "break[method]=next[page],";
    break;
  case 3:
    o << "break[method]=next[evenP],";
    break;
  case 4:
    o << "break[method]=next[oddP],";
    break;
  default:
    if (style.m_breakMethod>=0)
      o << "##break[method]=" << style.m_breakMethod << ",";
  }
  for (int i=0; i<3; ++i) {
    if (style.m_margins[i]<0) continue;
    static char const *wh[]= {"left", "right", "first"};
    o << "margins[" << wh[i] << "]=" << style.m_margins[i] << ",";
  }
  for (int i=0; i<3; ++i) {
    if (style.m_spacings[i]<0) continue;
    o << (i==0 ? "interline" : i==1 ? "before[spacing]" : "after[spacing]");
    o << "=" << style.m_spacings[i];
    if (style.m_spacingUnits[i]==0)
      o << "%";
    else if (style.m_spacingUnits[i]==1)
      o << "pt";
    else
      o << "[###unit]=" << style.m_spacingUnits[i];
    o << ",";
  }
  if (!style.m_tabList.empty()) {
    o << "tabs=[";
    for (auto const &tab : style.m_tabList)
      o << tab << ",";
    o << "],";
  }
  // char
  if (!style.m_fontName.empty())
    o << "font=\"" << style.m_fontName.cstr() << "\",";
  if (style.m_fontId>=0)
    o << "id[font]=" << style.m_fontId << ",";
  if (style.m_fontSize>=0)
    o << "sz[font]=" << style.m_fontSize << ",";
  for (int i=0; i<2; ++i) {
    uint32_t fl=style.m_fontFlags[i];
    if (!fl) continue;
    if (i==1)
      o << "flag[rm]=[";
    if (fl&1) o << "bold,";
    if (fl&2) o << "it,";
    // 4 underline?
    if (fl&8) o << "outline,";
    if (fl&0x10) o << "shadow,";
    if (fl&0x200) o << "strike[through],";
    if (fl&0x400) o << "small[caps],";
    if (fl&0x800) o << "kumoraru,"; // ie. with some char overlapping
    if (fl&0x20000) o << "underline[word],";
    if (fl&0x80000) o << "key[pairing],";
    fl &= 0xFFF5F1E4;
    if (fl) o << "#fontFlags=" << std::hex << fl << std::dec << ",";
    if (i==1)
      o << "],";
  }
  switch (style.m_caps) {
  case 0:
    break;
  case 1:
    o << "upper[caps],";
    break;
  case 2:
    o << "lower[caps],";
    break;
  case 3:
    o << "upper[initial+...],";
    break;
  default:
    if (style.m_caps >= 0)
      o << "###caps=" << style.m_caps << ",";
    break;
  }
  switch (style.m_underline) {
  case 0:
    break;
  case 1:
    o << "underline=single,";
    break;
  case 2:
    o << "underline=double,";
    break;
  default:
    if (style.m_underline>=0)
      o << "###underline=" << style.m_underline << ",";
  }
  if (style.m_scriptPosition.isSet())
    o << "ypos[font]=" << *style.m_scriptPosition << "%,";
  if (style.m_fontScaling>=0)
    o << "scale[font]=" << style.m_fontScaling << "%,";

  for (int i=0; i<4; ++i) {
    if (style.m_letterSpacings[i]<=0&&style.m_letterSpacings[i]>=0) continue;
    static char const *wh[]= {"", "[optimal]", "[min]", "[max]"};
    o << "letterSpacing" << wh[i] << "=" << style.m_letterSpacings[i] << ",";
  }
  if (style.m_widthStreching>=0)
    o << "width[streching]=" << style.m_widthStreching*100 << "%,";
  if (style.m_language>0) {
    std::string lang=RagTime5StyleManager::TextStyle::getLanguageLocale(style.m_language);
    if (!lang.empty())
      o << lang << ",";
    else
      o << "##language=" << std::hex << style.m_language << std::dec << ",";
  }
  // column
  if (style.m_numColumns>=0)
    o << "num[col]=" << style.m_numColumns << ",";
  if (style.m_columnGap>=0)
    o << "col[gap]=" << style.m_columnGap << ",";
  o << style.m_extra;
  return o;
}


// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
