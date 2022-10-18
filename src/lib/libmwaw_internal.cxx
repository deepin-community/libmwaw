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
#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <string>
#include <sstream>
#include <time.h>

#include <ctype.h>
#include <locale.h>

#include <librevenge-stream/librevenge-stream.h>

#include "libmwaw_internal.hxx"

/** namespace used to regroup all libwpd functions, enumerations which we have redefined for internal usage */
namespace libmwaw
{
uint8_t readU8(librevenge::RVNGInputStream *input)
{
  unsigned long numBytesRead;
  uint8_t const *p = input->read(sizeof(uint8_t), numBytesRead);

  if (!p || numBytesRead != sizeof(uint8_t))
    throw libmwaw::FileException();

  return *p;
}

void appendUnicode(uint32_t val, librevenge::RVNGString &buffer)
{
  uint8_t first;
  int len;
  if (val < 0x80) {
    first = 0;
    len = 1;
  }
  else if (val < 0x800) {
    first = 0xc0;
    len = 2;
  }
  else if (val < 0x10000) {
    first = 0xe0;
    len = 3;
  }
  else if (val < 0x200000) {
    first = 0xf0;
    len = 4;
  }
  else if (val < 0x4000000) {
    first = 0xf8;
    len = 5;
  }
  else {
    first = 0xfc;
    len = 6;
  }

  char outbuf[7];
  int i;
  for (i = len - 1; i > 0; --i) {
    outbuf[i] = char((val & 0x3f) | 0x80);
    val >>= 6;
  }
  outbuf[0] = char(val | first);
  outbuf[len] = 0;
  buffer.append(outbuf);
}
}

namespace libmwaw
{
std::string numberingTypeToString(NumberingType type)
{
  switch (type) {
  case ARABIC:
    return "1";
  case LOWERCASE:
    return "a";
  case UPPERCASE:
    return "A";
  case LOWERCASE_ROMAN:
    return "i";
  case UPPERCASE_ROMAN:
    return "I";
  case NONE:
  case BULLET:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  MWAW_DEBUG_MSG(("libmwaw::numberingTypeToString: must not be called with type %d\n", int(type)));
  return "1";
}

std::string numberingValueToString(NumberingType type, int value)
{
  std::stringstream ss;
  std::string s("");
  switch (type) {
  case ARABIC:
    ss << value;
    return ss.str();
  case LOWERCASE:
  case UPPERCASE:
    if (value <= 0) {
      MWAW_DEBUG_MSG(("libmwaw::numberingValueToString: value can not be negative or null for type %d\n", int(type)));
      return (type == LOWERCASE) ? "a" : "A";
    }
    while (value > 0) {
      s = char((type == LOWERCASE ? 'a' : 'A')+((value-1)%26))+s;
      value = (value-1)/26;
    }
    return s;
  case LOWERCASE_ROMAN:
  case UPPERCASE_ROMAN: {
    static char const *romanS[] = {"M", "CM", "D", "CD", "C", "XC", "L",
                                   "XL", "X", "IX", "V", "IV", "I"
                                  };
    static char const *romans[] = {"m", "cm", "d", "cd", "c", "xc", "l",
                                   "xl", "x", "ix", "v", "iv", "i"
                                  };
    static int const romanV[] = {1000, 900, 500, 400,  100, 90, 50,
                                 40, 10, 9, 5, 4, 1
                                };
    if (value <= 0 || value >= 4000) {
      MWAW_DEBUG_MSG(("libmwaw::numberingValueToString: out of range value for type %d\n", int(type)));
      return (type == LOWERCASE_ROMAN) ? "i" : "I";
    }
    for (int p = 0; p < 13; p++) {
      while (value >= romanV[p]) {
        ss << ((type == LOWERCASE_ROMAN) ? romans[p] : romanS[p]);
        value -= romanV[p];
      }
    }
    return ss.str();
  }
  case NONE:
    return "";
  case BULLET:
#if !defined(__clang__)
  default:
#endif
    MWAW_DEBUG_MSG(("libmwaw::numberingValueToString: must not be called with type %d\n", int(type)));
    break;
  }
  return "";
}

std::string writingModeToString(WritingMode mode)
{
  switch (mode) {
  case WritingLeftTop:
    return "lt-rb";
  case WritingLeftBottom:
    return "lb-rt";
  case WritingRightTop:
    return "rt-lb";
  case WritingRightBottom:
    return "rb-lt";
  case WritingInherited:
#if !defined(__clang__)
  default:
#endif
    break;
  }
  return "";
}
}

// color function
MWAWColor MWAWColor::barycenter(float alpha, MWAWColor const &colA,
                                float beta, MWAWColor const &colB)
{
  uint32_t res = 0;
  for (int i=0, depl=0; i<4; i++, depl+=8) {
    float val=alpha*float((colA.m_value>>depl)&0xFF)+beta*float((colB.m_value>>depl)&0xFF);
    if (val < 0) val=0;
    if (val > 256) val=256;
    auto comp= static_cast<unsigned char>(val);
    res+=uint32_t(comp<<depl);
  }
  return MWAWColor(res);
}

std::ostream &operator<< (std::ostream &o, MWAWColor const &c)
{
  auto const width = o.width();
  auto const fill = o.fill();
  o << "#" << std::hex << std::setfill('0') << std::setw(6)
    << (c.m_value&0xFFFFFF)
    // std::ios::width() takes/returns std::streamsize (long), but
    // std::setw() takes int. Go figure...
    << std::dec << std::setfill(fill) << std::setw(static_cast<int>(width));
  return o;
}

std::string MWAWColor::str() const
{
  std::stringstream stream;
  stream << *this;
  return stream.str();
}

// field function
bool MWAWField::addTo(librevenge::RVNGPropertyList &propList) const
{
  switch (m_type) {
  case Date: {
    propList.insert("librevenge:field-type", "text:date");

    librevenge::RVNGPropertyListVector pVect;
    if (m_DTFormat.empty() || !libmwaw::convertDTFormat(m_DTFormat, pVect))
      break;

    propList.insert("librevenge:value-type", "date");
    propList.insert("number:automatic-order", "true");
    propList.insert("librevenge:format", pVect);
    break;
  }
  case PageCount:
    propList.insert("librevenge:field-type", "text:page-count");
    propList.insert("style:num-format", numberingTypeToString(m_numberingType).c_str());
    break;
  case PageNumber:
    propList.insert("librevenge:field-type", "text:page-number");
    propList.insert("style:num-format", numberingTypeToString(m_numberingType).c_str());
    break;
  case Title:
    propList.insert("librevenge:field-type", "text:title");
    break;
  case Time: {
    propList.insert("librevenge:field-type", "text:time");

    librevenge::RVNGPropertyListVector pVect;
    if (m_DTFormat.empty() || !libmwaw::convertDTFormat(m_DTFormat, pVect))
      break;

    propList.insert("librevenge:value-type", "time");
    propList.insert("number:automatic-order", "true");
    propList.insert("librevenge:format", pVect);
    break;
  }
  case BookmarkStart:
  case BookmarkEnd:
    propList.insert("librevenge:field-type", m_type==BookmarkStart ? "text:bookmark-start" : "text:bookmark-end");
    if (!m_data.empty())
      propList.insert("text:name",m_data.c_str());
    break;
  case Database:
  case None:
#if !defined(__clang__)
  default:
#endif
    return false;
  }
  return true;
}

librevenge::RVNGString MWAWField::getString() const
{
  librevenge::RVNGString res;
  switch (m_type) {
  case Database:
    if (m_data.length())
      res=librevenge::RVNGString(m_data.c_str());
    else
      res=librevenge::RVNGString("#DATAFIELD#");
    break;
  case BookmarkStart:
  case BookmarkEnd:
  case Date:
  case PageCount:
  case PageNumber:
  case Title:
  case Time:
  case None:
#if !defined(__clang__)
  default:
#endif
    break;
  }

  return res;
}

// format function
namespace libmwaw
{
bool convertDTFormat(std::string const &dtFormat, librevenge::RVNGPropertyListVector &propVect)
{
  propVect.clear();
  std::string text("");
  librevenge::RVNGPropertyList list;
  size_t len=dtFormat.size();
  for (size_t c=0; c < len; ++c) {
    if (dtFormat[c]!='%' || c+1==len) {
      text+=dtFormat[c];
      continue;
    }
    char ch=dtFormat[++c];
    if (ch=='%') {
      text += '%';
      continue;
    }
    if (!text.empty()) {
      list.clear();
      list.insert("librevenge:value-type", "text");
      list.insert("librevenge:text", text.c_str());
      propVect.append(list);
      text.clear();
    }
    list.clear();
    switch (ch) {
    case 'Y':
      list.insert("number:style", "long");
      MWAW_FALLTHROUGH;
    case 'y':
      list.insert("librevenge:value-type", "year");
      propVect.append(list);
      break;
    case 'B':
      list.insert("number:style", "long");
      MWAW_FALLTHROUGH;
    case 'b':
    case 'h':
      list.insert("librevenge:value-type", "month");
      list.insert("number:textual", true);
      propVect.append(list);
      break;
    case 'm':
      list.insert("librevenge:value-type", "month");
      propVect.append(list);
      break;
    case 'e':
      list.insert("number:style", "long");
      MWAW_FALLTHROUGH;
    case 'd':
      list.insert("librevenge:value-type", "day");
      propVect.append(list);
      break;
    case 'A':
      list.insert("number:style", "long");
      MWAW_FALLTHROUGH;
    case 'a':
      list.insert("librevenge:value-type", "day-of-week");
      propVect.append(list);
      break;

    case 'H':
      list.insert("number:style", "long");
      MWAW_FALLTHROUGH;
    case 'I':
      list.insert("librevenge:value-type", "hours");
      propVect.append(list);
      break;
    case 'M':
      list.insert("librevenge:value-type", "minutes");
      list.insert("number:style", "long");
      propVect.append(list);
      break;
    case 'S':
      list.insert("librevenge:value-type", "seconds");
      list.insert("number:style", "long");
      propVect.append(list);
      break;
    case 'p':
      list.clear();
      list.insert("librevenge:value-type", "am-pm");
      propVect.append(list);
      break;
#if !defined(__clang__)
    default:
      MWAW_DEBUG_MSG(("convertDTFormat: find unimplement command %c(ignored)\n", ch));
#endif
    }
  }
  if (!text.empty()) {
    list.clear();
    list.insert("librevenge:value-type", "text");
    list.insert("librevenge:text", text.c_str());
    propVect.append(list);
  }
  return propVect.count()!=0;
}
}

// link function
bool MWAWLink::addTo(librevenge::RVNGPropertyList &propList) const
{
  propList.insert("xlink:type","simple");
  if (!m_HRef.empty())
    propList.insert("xlink:href",m_HRef.c_str());
  return true;
}

// border function
int MWAWBorder::compare(MWAWBorder const &orig) const
{
  int diff = int(m_style)-int(orig.m_style);
  if (diff) return diff;
  diff = int(m_type)-int(orig.m_type);
  if (diff) return diff;
  if (m_width < orig.m_width) return -1;
  if (m_width > orig.m_width) return 1;
  if (m_color < orig.m_color) return -1;
  if (m_color > orig.m_color) return 1;
  return 0;
}
bool MWAWBorder::addTo(librevenge::RVNGPropertyList &propList, std::string const which) const
{
  std::stringstream stream, field;
  stream << m_width << "pt ";
  if (m_type==MWAWBorder::Double || m_type==MWAWBorder::Triple) {
    static bool first = true;
    if (first && m_style!=Simple) {
      MWAW_DEBUG_MSG(("MWAWBorder::addTo: find double or tripe border with complex style\n"));
      first = false;
    }
    stream << "double";
  }
  else {
    switch (m_style) {
    case Dot:
    case LargeDot:
      stream << "dotted";
      break;
    case Dash:
      stream << "dashed";
      break;
    case Simple:
      stream << "solid";
      break;
    case None:
#if !defined(__clang__)
    default:
#endif
      stream << "none";
      break;
    }
  }
  stream << " " << m_color;
  field << "fo:border";
  if (which.length())
    field << "-" << which;
  propList.insert(field.str().c_str(), stream.str().c_str());
  size_t numRelWidth=m_widthsList.size();
  if (numRelWidth==0)
    return true;
  if (m_type!=MWAWBorder::Double || numRelWidth!=3) {
    static bool first = true;
    if (first) {
      MWAW_DEBUG_MSG(("MWAWBorder::addTo: relative width is only implemented with double style\n"));
      first = false;
    }
    return true;
  }
  double totalWidth=0;
  for (auto const &w : m_widthsList)
    totalWidth+=w;
  if (totalWidth <= 0) {
    MWAW_DEBUG_MSG(("MWAWBorder::addTo: can not compute total width\n"));
    return true;
  }
  double factor=m_width/totalWidth;
  stream.str("");
  for (size_t w=0; w < numRelWidth; w++) {
    stream << factor *m_widthsList[w]<< "pt";
    if (w+1!=numRelWidth)
      stream << " ";
  }
  field.str("");
  field << "style:border-line-width";
  if (which.length())
    field << "-" << which;
  propList.insert(field.str().c_str(), stream.str().c_str());
  return true;
}

std::ostream &operator<< (std::ostream &o, MWAWBorder::Style const &style)
{
  switch (style) {
  case MWAWBorder::None:
    o << "none";
    break;
  case MWAWBorder::Simple:
    break;
  case MWAWBorder::Dot:
    o << "dot";
    break;
  case MWAWBorder::LargeDot:
    o << "large dot";
    break;
  case MWAWBorder::Dash:
    o << "dash";
    break;
#if !defined(__clang__)
  default:
    MWAW_DEBUG_MSG(("MWAWBorder::operator<<: find unknown style\n"));
    o << "#style=" << int(style);
    break;
#endif
  }
  return o;
}

std::ostream &operator<< (std::ostream &o, MWAWBorder const &border)
{
  o << border.m_style << ":";
  switch (border.m_type) {
  case MWAWBorder::Single:
    break;
  case MWAWBorder::Double:
    o << "double:";
    break;
  case MWAWBorder::Triple:
    o << "triple:";
    break;
#if !defined(__clang__)
  default:
    MWAW_DEBUG_MSG(("MWAWBorder::operator<<: find unknown type\n"));
    o << "#type=" << int(border.m_type) << ":";
    break;
#endif
  }
  if (border.m_width > 1 || border.m_width < 1) o << "w=" << border.m_width << ":";
  if (!border.m_color.isBlack())
    o << "col=" << border.m_color << ":";
  o << ",";
  if (!border.m_widthsList.empty()) {
    o << "bordW[rel]=[";
    for (auto const &w : border.m_widthsList)
      o << w << ",";
    o << "]:";
  }
  o << border.m_extra;
  return o;
}

// picture function
MWAWEmbeddedObject::~MWAWEmbeddedObject()
{
}

bool MWAWEmbeddedObject::addTo(librevenge::RVNGPropertyList &propList) const
{
  bool firstSet=false;
  librevenge::RVNGPropertyListVector auxiliarVector;
  for (size_t i=0; i<m_dataList.size(); ++i) {
    if (m_dataList[i].empty()) continue;
    std::string type=m_typeList.size() ? m_typeList[i] : "image/pict";
    if (!firstSet) {
      propList.insert("librevenge:mime-type", type.c_str());
      propList.insert("office:binary-data", m_dataList[i]);
      firstSet=true;
      continue;
    }
    librevenge::RVNGPropertyList auxiList;
    auxiList.insert("librevenge:mime-type", type.c_str());
    auxiList.insert("office:binary-data", m_dataList[i]);
    auxiliarVector.append(auxiList);
  }
  if (!auxiliarVector.empty())
    propList.insert("librevenge:replacement-objects", auxiliarVector);
  if (!firstSet) {
    MWAW_DEBUG_MSG(("MWAWEmbeddedObject::addTo: called without picture\n"));
    return false;
  }
  return true;
}

int MWAWEmbeddedObject::cmp(MWAWEmbeddedObject const &pict) const
{
  if (m_typeList.size()!=pict.m_typeList.size())
    return m_typeList.size()<pict.m_typeList.size() ? -1 : 1;
  for (size_t i=0; i<m_typeList.size(); ++i) {
    if (m_typeList[i]<pict.m_typeList[i]) return -1;
    if (m_typeList[i]>pict.m_typeList[i]) return 1;
  }
  if (m_dataList.size()!=pict.m_dataList.size())
    return m_dataList.size()<pict.m_dataList.size() ? -1 : 1;
  for (size_t i=0; i<m_dataList.size(); ++i) {
    if (m_dataList[i].size() < pict.m_dataList[i].size()) return 1;
    if (m_dataList[i].size() > pict.m_dataList[i].size()) return -1;

    const unsigned char *ptr=m_dataList[i].getDataBuffer();
    const unsigned char *aPtr=pict.m_dataList[i].getDataBuffer();
    if (!ptr || !aPtr) continue; // must only appear if the two buffers are empty
    for (unsigned long h=0; h < m_dataList[i].size(); ++h, ++ptr, ++aPtr) {
      if (*ptr < *aPtr) return 1;
      if (*ptr > *aPtr) return -1;
    }
  }
  return 0;
}

std::ostream &operator<<(std::ostream &o, MWAWEmbeddedObject const &pict)
{
  if (pict.isEmpty()) return o;
  o << "[";
  for (auto const &type : pict.m_typeList) {
    if (type.empty())
      o << "_,";
    else
      o << type << ",";
  }
  o << "],";
  return o;
}

// a little geometry
MWAWTransformation MWAWTransformation::rotation(float angle, MWAWVec2f const &center)
{
  auto angl=float(double(angle)*M_PI/180);
  auto cosA=float(std::cos(angl));
  auto sinA=float(std::sin(angl));
  return MWAWTransformation(MWAWVec3f(cosA, -sinA, center[0]-cosA*center[0]+sinA*center[1]),
                            MWAWVec3f(sinA, cosA, center[1]-sinA*center[0]-cosA*center[1]));
}

bool MWAWTransformation::decompose(float &rot, MWAWVec2f &shearing, MWAWTransformation &transform, MWAWVec2f const &origCenter) const
{
  if (m_isIdentity) return false;
  MWAWVec3f const &xRow=(*this)[0];
  MWAWVec3f const &yRow=(*this)[1];
  MWAWVec2f const &center=*this * origCenter;
  // first check shearing
  float shearY=0;
  float val1=xRow[0]*xRow[1];
  float val2=yRow[0]*yRow[1];
  float diff=val2-val1;
  if (diff<-0.01f || diff>0.01f) {
    float const &A=val1;
    float const B=xRow[1]*yRow[0]+xRow[0]*yRow[1];
    float const &C=diff;
    if (A>=0 && A<=0) {
      if (B>=0 && A<=0) {
        MWAW_DEBUG_MSG(("MWAWTransformation::decompose: can not determine the shearing\n"));
        return false;
      }
      shearY=C/B;
    }
    else {
      float const &delta=B*B-4*A*C;
      if (delta<0) {
        MWAW_DEBUG_MSG(("MWAWTransformation::decompose: can not determine the shearing\n"));
        return false;
      }
      shearY=(B-float(std::sqrt(delta)))/2.f/A;
    }
    transform=MWAWTransformation::shear(MWAWVec2f(0,-shearY), center) **this;
  }
  else
    transform=*this;
  shearing=MWAWVec2f(0,shearY);
  // fixme: we must first check for symetry here...
  // now the rotation
  rot=-std::atan2(-transform[1][0],transform[1][1]);
  rot *= float(180/M_PI);
  transform=MWAWTransformation::rotation(-rot, center) * transform;
  return true;
}

namespace libmwaw
{
MWAWVec2f rotatePointAroundCenter(MWAWVec2f const &point, MWAWVec2f const &center, float angle)
{
  float angl=float(M_PI/180.)*angle;
  MWAWVec2f pt = point-center;
  return center + MWAWVec2f(std::cos(angl)*pt[0]-std::sin(angl)*pt[1],
                            std::sin(angl)*pt[0]+std::cos(angl)*pt[1]);
}

MWAWBox2f rotateBoxFromCenter(MWAWBox2f const &box, float angle)
{
  MWAWVec2f center=box.center();
  MWAWVec2f minPt, maxPt;
  for (int p=0; p<4; ++p) {
    MWAWVec2f pt=rotatePointAroundCenter(MWAWVec2f(box[p<2?0:1][0],box[(p%2)?0:1][1]), center, angle);
    if (p==0) {
      minPt=maxPt=pt;
      continue;
    }
    for (int c=0; c<2; ++c) {
      if (pt[c]<minPt[c])
        minPt[c]=pt[c];
      else if (pt[c]>maxPt[c])
        maxPt[c]=pt[c];
    }
  }
  return MWAWBox2f(minPt,maxPt);
}

// debug message
#ifdef DEBUG
void printDebugMsg(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}
#endif
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
