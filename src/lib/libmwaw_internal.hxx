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

#ifndef LIBMWAW_INTERNAL_H
#define LIBMWAW_INTERNAL_H
#ifdef DEBUG
#include <stdio.h>
#endif

#include <math.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <librevenge-stream/librevenge-stream.h>
#include <librevenge/librevenge.h>

#if defined(_MSC_VER) || defined(__DJGPP__)

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef unsigned __int64 uint64_t;
typedef __int64 int64_t;

#else /* !_MSC_VER && !__DJGPP__*/

#  ifdef HAVE_CONFIG_H

#    include <config.h>
#    ifdef HAVE_STDINT_H
#      include <stdint.h>
#    endif
#    ifdef HAVE_INTTYPES_H
#      include <inttypes.h>
#    endif

#  else

// assume that the headers are there inside LibreOffice build when no HAVE_CONFIG_H is defined
#    include <stdint.h>
#    include <inttypes.h>

#  endif

#endif /* _MSC_VER || __DJGPP__ */

// define gmtime_r and localtime_r on Windows, so that can use
// thread-safe functions on other environments
#ifdef _WIN32
#  define gmtime_r(tp,tmp) (gmtime(tp)?(*(tmp)=*gmtime(tp),(tmp)):0)
#  define localtime_r(tp,tmp) (localtime(tp)?(*(tmp)=*localtime(tp),(tmp)):0)
#endif

/* ---------- memory  --------------- */
/** an noop deleter used to transform a libwpd pointer in a false std::shared_ptr */
template <class T>
struct MWAW_shared_ptr_noop_deleter {
  void operator()(T *) {}
};

#if defined(HAVE_FUNC_ATTRIBUTE_FORMAT)
#  define LIBMWAW_ATTRIBUTE_PRINTF(fmt, arg) __attribute__((format(printf, fmt, arg)))
#else
#  define LIBMWAW_ATTRIBUTE_PRINTF(fmt, arg)
#endif

#define MWAW_N_ELEMENTS(m) sizeof(m)/sizeof(m[0])

#if defined(HAVE_CLANG_ATTRIBUTE_FALLTHROUGH)
#  define MWAW_FALLTHROUGH [[clang::fallthrough]]
#elif defined(HAVE_GCC_ATTRIBUTE_FALLTHROUGH)
#  define MWAW_FALLTHROUGH __attribute__((fallthrough))
#else
#  define MWAW_FALLTHROUGH ((void) 0)
#endif

/* ---------- debug  --------------- */
#ifdef DEBUG
namespace libmwaw
{
void printDebugMsg(const char *format, ...) LIBMWAW_ATTRIBUTE_PRINTF(1,2);
}
#define MWAW_DEBUG_MSG(M) libmwaw::printDebugMsg M
#else
#define MWAW_DEBUG_MSG(M)
#endif

namespace libmwaw
{
// Various exceptions:
class VersionException
{
};

class FileException
{
};

class ParseException
{
};

class GenericException
{
};

class WrongPasswordException
{
};
}

/* ---------- input ----------------- */
namespace libmwaw
{
uint8_t readU8(librevenge::RVNGInputStream *input);
//! adds an unicode character to a string
void appendUnicode(uint32_t val, librevenge::RVNGString &buffer);

//! checks whether addition of \c x and \c y would overflow
template<typename T>
bool checkAddOverflow(T x, T y)
{
  return (x < 0 && y < std::numeric_limits<T>::lowest() - x)
         || (x > 0 && y > std::numeric_limits<T>::max() - x);
}
}

/* ---------- small enum/class ------------- */
namespace libmwaw
{
//! basic position enum
enum Position { Left = 0, Right = 1, Top = 2, Bottom = 3, HMiddle = 4, VMiddle = 5 };
//! basic position enum bits
enum { LeftBit = 0x01,  RightBit = 0x02, TopBit=0x4, BottomBit = 0x08, HMiddleBit = 0x10, VMiddleBit = 0x20 };

enum NumberingType { NONE, BULLET, ARABIC, LOWERCASE, UPPERCASE, LOWERCASE_ROMAN, UPPERCASE_ROMAN };
std::string numberingTypeToString(NumberingType type);
std::string numberingValueToString(NumberingType type, int value);

//! the different writing mode
enum WritingMode { WritingLeftTop, WritingLeftBottom, WritingRightTop, WritingRightBottom, WritingInherited };
//! a function to convert a writing mode in string lt-rb, ...
std::string writingModeToString(WritingMode mode);
enum SubDocumentType { DOC_NONE, DOC_CHART, DOC_CHART_ZONE, DOC_COMMENT_ANNOTATION, DOC_GRAPHIC_GROUP, DOC_HEADER_FOOTER, DOC_NOTE, DOC_SHEET, DOC_TABLE, DOC_TEXT_BOX };
}

//! the class to store a color
struct MWAWColor {
  //! constructor
  explicit MWAWColor(uint32_t argb=0)
    : m_value(argb)
  {
  }
  //! constructor from color
  MWAWColor(unsigned char r, unsigned char g,  unsigned char b, unsigned char a=255)
    : m_value(uint32_t((a<<24)+(r<<16)+(g<<8)+b))
  {
  }
  //! copy constructor
  MWAWColor(MWAWColor const &) = default;
  //! move assignement
  MWAWColor(MWAWColor &&) = default;
  //! operator=
  MWAWColor &operator=(MWAWColor const &) = default;
  //! move operator=
  MWAWColor &operator=(MWAWColor &&) = default;
  //! operator=
  MWAWColor &operator=(uint32_t argb)
  {
    m_value = argb;
    return *this;
  }
  //! return a color from a cmyk color ( basic)
  static MWAWColor colorFromCMYK(unsigned char c, unsigned char m,  unsigned char y, unsigned char k)
  {
    double w=1.-static_cast<double>(k)/255.;
    return MWAWColor
           (static_cast<unsigned char>(255 * (1-static_cast<double>(c)/255) * w),
            static_cast<unsigned char>(255 * (1-static_cast<double>(m)/255) * w),
            static_cast<unsigned char>(255 * (1-static_cast<double>(y)/255) * w)
           );
  }
  //! return a color from a hsl color (basic)
  static MWAWColor colorFromHSL(unsigned char H, unsigned char S,  unsigned char L)
  {
    double c=(1-((L>=128) ? (2*static_cast<double>(L)-255) : (255-2*static_cast<double>(L)))/255)*
             static_cast<double>(S)/255;
    double tmp=std::fmod((static_cast<double>(H)*6/255),2)-1;
    double x=c*(1-(tmp>0 ? tmp : -tmp));
    auto C=static_cast<unsigned char>(255*c);
    auto M=static_cast<unsigned char>(static_cast<double>(L)-255*c/2);
    auto X=static_cast<unsigned char>(255*x);
    if (H<=42) return MWAWColor(static_cast<unsigned char>(M+C),static_cast<unsigned char>(M+X),static_cast<unsigned char>(M));
    if (H<=85) return MWAWColor(static_cast<unsigned char>(M+X),static_cast<unsigned char>(M+C),static_cast<unsigned char>(M));
    if (H<=127) return MWAWColor(static_cast<unsigned char>(M),static_cast<unsigned char>(M+C),static_cast<unsigned char>(M+X));
    if (H<=170) return MWAWColor(static_cast<unsigned char>(M),static_cast<unsigned char>(M+X),static_cast<unsigned char>(M+C));
    if (H<=212) return MWAWColor(static_cast<unsigned char>(M+X),static_cast<unsigned char>(M),static_cast<unsigned char>(M+C));
    return MWAWColor(static_cast<unsigned char>(M+C),static_cast<unsigned char>(M),static_cast<unsigned char>(M+X));
  }
  //! return the back color
  static MWAWColor black()
  {
    return MWAWColor(0,0,0);
  }
  //! return the white color
  static MWAWColor white()
  {
    return MWAWColor(255,255,255);
  }

  //! return alpha*colA+beta*colB
  static MWAWColor barycenter(float alpha, MWAWColor const &colA,
                              float beta, MWAWColor const &colB);
  //! return the rgba value
  uint32_t value() const
  {
    return m_value;
  }
  //! returns the alpha value
  unsigned char getAlpha() const
  {
    return static_cast<unsigned char>((m_value>>24)&0xFF);
  }
  //! returns the green value
  unsigned char getBlue() const
  {
    return static_cast<unsigned char>(m_value&0xFF);
  }
  //! returns the red value
  unsigned char getRed() const
  {
    return static_cast<unsigned char>((m_value>>16)&0xFF);
  }
  //! returns the green value
  unsigned char getGreen() const
  {
    return static_cast<unsigned char>((m_value>>8)&0xFF);
  }
  //! return true if the color is black
  bool isBlack() const
  {
    return (m_value&0xFFFFFF)==0;
  }
  //! return true if the color is white
  bool isWhite() const
  {
    return (m_value&0xFFFFFF)==0xFFFFFF;
  }
  //! operator==
  bool operator==(MWAWColor const &c) const
  {
    return (c.m_value&0xFFFFFF)==(m_value&0xFFFFFF);
  }
  //! operator!=
  bool operator!=(MWAWColor const &c) const
  {
    return !operator==(c);
  }
  //! operator<
  bool operator<(MWAWColor const &c) const
  {
    return (c.m_value&0xFFFFFF)<(m_value&0xFFFFFF);
  }
  //! operator<=
  bool operator<=(MWAWColor const &c) const
  {
    return (c.m_value&0xFFFFFF)<=(m_value&0xFFFFFF);
  }
  //! operator>
  bool operator>(MWAWColor const &c) const
  {
    return !operator<=(c);
  }
  //! operator>=
  bool operator>=(MWAWColor const &c) const
  {
    return !operator<(c);
  }
  //! operator<< in the form \#rrggbb
  friend std::ostream &operator<< (std::ostream &o, MWAWColor const &c);
  //! print the color in the form \#rrggbb
  std::string str() const;
protected:
  //! the argb color
  uint32_t m_value;
};

//! a border
struct MWAWBorder {
  /** the line style */
  enum Style { None, Simple, Dot, LargeDot, Dash };
  /** the line repetition */
  enum Type { Single, Double, Triple };

  //! constructor
  MWAWBorder()
    : m_style(Simple)
    , m_type(Single)
    , m_width(1)
    , m_widthsList()
    , m_color(MWAWColor::black())
    , m_extra("") { }
  MWAWBorder(MWAWBorder const &) = default;
  MWAWBorder(MWAWBorder &&) = default;
  MWAWBorder &operator=(MWAWBorder const &) = default;
  MWAWBorder &operator=(MWAWBorder &&) = default;
  /** add the border property to proplist (if needed )

  \note if set which must be equal to "left", "top", ... */
  bool addTo(librevenge::RVNGPropertyList &propList, std::string which="") const;
  //! returns true if the border is empty
  bool isEmpty() const
  {
    return m_style==None || m_width <= 0;
  }
  //! operator==
  bool operator==(MWAWBorder const &orig) const
  {
    return !operator!=(orig);
  }
  //! operator!=
  bool operator!=(MWAWBorder const &orig) const
  {
    return m_style != orig.m_style || m_type != orig.m_type ||
           m_width < orig.m_width || m_width > orig.m_width || m_color != orig.m_color ||
           m_widthsList != orig.m_widthsList;
  }
  //! compare two borders
  int compare(MWAWBorder const &orig) const;

  //! operator<<
  friend std::ostream &operator<< (std::ostream &o, MWAWBorder const &border);
  //! operator<<: prints data in form "none|dot|..."
  friend std::ostream &operator<< (std::ostream &o, MWAWBorder::Style const &style);
  //! the border style
  Style m_style;

  // multiple borders

  //! the border repetition
  Type m_type;
  //! the border total width in point
  double m_width;
  /** the different length used for each line/sep (if defined)

  \note when defined, the size of this list must be equal to 2*Type-1*/
  std::vector<double> m_widthsList;
  //! the border color
  MWAWColor m_color;
  //! extra data ( if needed)
  std::string m_extra;
};

//! a field
struct MWAWField {
  /** Defines some basic type for field */
  enum Type { None, PageCount, PageNumber, Date, Time, Title, Database, BookmarkStart, BookmarkEnd };

  /** basic constructor */
  explicit MWAWField(Type type)
    : m_type(type)
    , m_numberingType(libmwaw::ARABIC)
    , m_DTFormat("")
    , m_data("")
  {
  }
  MWAWField(MWAWField &&) = default;
  MWAWField(MWAWField const &) = default;
  MWAWField &operator=(MWAWField const &) = default;
  MWAWField &operator=(MWAWField &&) = default;
  /** add the link property to proplist (if possible) */
  bool addTo(librevenge::RVNGPropertyList &propList) const;
  //! returns a string corresponding to the field (if possible) */
  librevenge::RVNGString getString() const;
  //! the type
  Type m_type;
  //! the number type ( for number field )
  libmwaw::NumberingType m_numberingType;
  //! the date/time format using strftime format if defined
  std::string m_DTFormat;
  //! the database/link field ( if defined ) or the bookmark name
  std::string m_data;
};

//! a link
struct MWAWLink {
  /** basic constructor */
  MWAWLink()
    : m_HRef("")
  {
  }

  /** add the link property to proplist (if needed ) */
  bool addTo(librevenge::RVNGPropertyList &propList) const;

  //! the href field
  std::string m_HRef;
};

//! a note
struct MWAWNote {
  //! enum to define note type
  enum Type { FootNote, EndNote };
  //! constructor
  explicit MWAWNote(Type type)
    : m_type(type)
    , m_label("")
    , m_number(-1)
  {
  }
  //! the note type
  Type m_type;
  //! the note label
  librevenge::RVNGString m_label;
  //! the note number if defined
  int m_number;
};

/** small class use to define a embedded object

    \note mainly used to store picture
 */
struct MWAWEmbeddedObject {
  //! empty constructor
  MWAWEmbeddedObject()
    : m_dataList()
    , m_typeList()
  {
  }
  //! constructor
  MWAWEmbeddedObject(librevenge::RVNGBinaryData const &binaryData,
                     std::string const &type="image/pict") : m_dataList(), m_typeList()
  {
    add(binaryData, type);
  }
  MWAWEmbeddedObject(MWAWEmbeddedObject const &)=default;
  MWAWEmbeddedObject &operator=(MWAWEmbeddedObject const &)=default;
  MWAWEmbeddedObject &operator=(MWAWEmbeddedObject &&)=default;
  //! destructor
  ~MWAWEmbeddedObject();
  //! return true if the picture contains no data
  bool isEmpty() const
  {
    for (auto const &data : m_dataList) {
      if (!data.empty())
        return false;
    }
    return true;
  }
  //! add a picture
  void add(librevenge::RVNGBinaryData const &binaryData, std::string const &type="image/pict")
  {
    size_t pos=m_dataList.size();
    if (pos<m_typeList.size()) pos=m_typeList.size();
    m_dataList.resize(pos+1);
    m_dataList[pos]=binaryData;
    m_typeList.resize(pos+1);
    m_typeList[pos]=type;
  }
  /** add the link property to proplist */
  bool addTo(librevenge::RVNGPropertyList &propList) const;
  /** operator<<*/
  friend std::ostream &operator<<(std::ostream &o, MWAWEmbeddedObject const &pict);
  /** a comparison function */
  int cmp(MWAWEmbeddedObject const &pict) const;

  //! the picture content: one data by representation
  std::vector<librevenge::RVNGBinaryData> m_dataList;
  //! the picture type: one type by representation
  std::vector<std::string> m_typeList;
};

// forward declarations of basic classes and smart pointers
struct MWAWStream;
class MWAWEntry;
class MWAWFont;
class MWAWGraphicEncoder;
class MWAWGraphicShape;
class MWAWGraphicStyle;
class MWAWHeader;
class MWAWList;
class MWAWPageSpan;
class MWAWParagraph;
class MWAWParser;
class MWAWPosition;
class MWAWSection;

class MWAWFontConverter;
class MWAWFontManager;
class MWAWGraphicListener;
class MWAWInputStream;
class MWAWListener;
class MWAWListManager;
class MWAWParserState;
class MWAWPresentationListener;
class MWAWRSRCParser;
class MWAWSpreadsheetListener;
class MWAWSubDocument;
class MWAWTextListener;
//! a smart pointer of MWAWFontConverter
typedef std::shared_ptr<MWAWFontConverter> MWAWFontConverterPtr;
//! a smart pointer of MWAWFontManager
typedef std::shared_ptr<MWAWFontManager> MWAWFontManagerPtr;
//! a smart pointer of MWAWGraphicListener
typedef std::shared_ptr<MWAWGraphicListener> MWAWGraphicListenerPtr;
//! a smart pointer of MWAWInputStream
typedef std::shared_ptr<MWAWInputStream> MWAWInputStreamPtr;
//! a smart pointer of MWAWListener
typedef std::shared_ptr<MWAWListener> MWAWListenerPtr;
//! a smart pointer of MWAWListManager
typedef std::shared_ptr<MWAWListManager> MWAWListManagerPtr;
//! a smart pointer of MWAWParserState
typedef std::shared_ptr<MWAWParserState> MWAWParserStatePtr;
//! a smart pointer of MWAWPresentationListener
typedef std::shared_ptr<MWAWPresentationListener> MWAWPresentationListenerPtr;
//! a smart pointer of MWAWRSRCParser
typedef std::shared_ptr<MWAWRSRCParser> MWAWRSRCParserPtr;
//! a smart pointer of MWAWSpreadsheetListener
typedef std::shared_ptr<MWAWSpreadsheetListener> MWAWSpreadsheetListenerPtr;
//! a smart pointer of MWAWSubDocument
typedef std::shared_ptr<MWAWSubDocument> MWAWSubDocumentPtr;
//! a smart pointer of MWAWTextListener
typedef std::shared_ptr<MWAWTextListener> MWAWTextListenerPtr;

/** a generic variable template: value + flag to know if the variable is set

\note the variable is considered set as soon a new value is set or
when its content is acceded by a function which returns a not-const
reference... You can use the function setSet to unset it.

\note must be replaced by std::optional when we will be comptable with std::c++-17
*/
template <class T> struct MWAWVariable {
  //! constructor
  MWAWVariable()
    : m_data()
    , m_set(false) {}
  //! constructor with a default value
  explicit MWAWVariable(T const &def)
    : m_data(def)
    , m_set(false) {}
  //! copy constructor
  MWAWVariable(MWAWVariable const &orig)
    : m_data(orig.m_data)
    , m_set(orig.m_set) {}
  //! copy operator
  MWAWVariable &operator=(MWAWVariable const &) = default;
  //! set a value
  MWAWVariable &operator=(T const &val)
  {
    m_data = val;
    m_set = true;
    return std::forward<MWAWVariable &>(*this);
  }
  //! update the current value if orig is set
  void insert(MWAWVariable const &orig)
  {
    if (orig.m_set) {
      m_data = orig.m_data;
      m_set = orig.m_set;
    }
  }
  //! operator*
  T const *operator->() const
  {
    return &m_data;
  }
  /** operator* */
  T *operator->()
  {
    m_set = true;
    return &m_data;
  }
  //! operator*
  T const &operator*() const
  {
    return m_data;
  }
  //! operator*
  T &operator*()
  {
    m_set = true;
    return m_data;
  }
  //! return the current value
  T const &get() const
  {
    return m_data;
  }
  //! return true if the variable is set
  bool isSet() const
  {
    return m_set;
  }
  //! define if the variable is set
  void setSet(bool newVal)
  {
    m_set=newVal;
  }
protected:
  //! the value
  T m_data;
  //! a flag to know if the variable is set or not
  bool m_set;
};

/* ---------- vec2/box2f ------------- */
/*! \class MWAWVec2
 *   \brief small class which defines a vector with 2 elements
 */
template <class T> class MWAWVec2
{
public:
  //! constructor
  explicit MWAWVec2(T xx=0,T yy=0)
    : m_x(xx)
    , m_y(yy) { }
  //! generic copy constructor
  template <class U> explicit MWAWVec2(MWAWVec2<U> const &p)
    : m_x(T(p.x()))
    , m_y(T(p.y())) {}

  //! first element
  T x() const
  {
    return m_x;
  }
  //! second element
  T y() const
  {
    return m_y;
  }
  //! operator[]
  T operator[](int c) const
  {
    if (c<0 || c>1) throw libmwaw::GenericException();
    return (c==0) ? m_x : m_y;
  }
  //! operator[]
  T &operator[](int c)
  {
    if (c<0 || c>1) throw libmwaw::GenericException();
    return (c==0) ? m_x : m_y;
  }

  //! resets the two elements
  void set(T xx, T yy)
  {
    m_x = xx;
    m_y = yy;
  }
  //! resets the first element
  void setX(T xx)
  {
    m_x = xx;
  }
  //! resets the second element
  void setY(T yy)
  {
    m_y = yy;
  }

  //! increases the actuals values by \a dx and \a dy
  void add(T dx, T dy)
  {
    if (libmwaw::checkAddOverflow(m_x, dx) || libmwaw::checkAddOverflow(m_y, dy))
      throw libmwaw::GenericException();
    m_x += dx;
    m_y += dy;
  }

  //! operator+=
  MWAWVec2<T> &operator+=(MWAWVec2<T> const &p)
  {
    add(p.m_x, p.m_y);
    return *this;
  }
  //! operator-=
  MWAWVec2<T> &operator-=(MWAWVec2<T> const &p)
  {
    // check if negation of either of the coords will cause overflow
    const T diff = std::numeric_limits<T>::min() + std::numeric_limits<T>::max();
    if (libmwaw::checkAddOverflow(p.m_x, diff) || libmwaw::checkAddOverflow(p.m_y, diff))
      throw libmwaw::GenericException();
    add(-p.m_x, -p.m_y);
    return *this;
  }
  //! generic operator*=
  template <class U>
  MWAWVec2<T> &operator*=(U scale)
  {
    m_x = T(m_x*scale);
    m_y = T(m_y*scale);
    return *this;
  }

  //! operator+
  friend MWAWVec2<T> operator+(MWAWVec2<T> const &p1, MWAWVec2<T> const &p2)
  {
    MWAWVec2<T> p(p1);
    return p+=p2;
  }
  //! operator-
  friend MWAWVec2<T> operator-(MWAWVec2<T> const &p1, MWAWVec2<T> const &p2)
  {
    MWAWVec2<T> p(p1);
    return p-=p2;
  }
  //! generic operator*
  template <class U>
  friend MWAWVec2<T> operator*(U scale, MWAWVec2<T> const &p1)
  {
    MWAWVec2<T> p(p1);
    return p *= scale;
  }

  //! comparison==
  bool operator==(MWAWVec2<T> const &p) const
  {
    return cmpY(p) == 0;
  }
  //! comparison!=
  bool operator!=(MWAWVec2<T> const &p) const
  {
    return cmpY(p) != 0;
  }
  //! comparison<: sort by y
  bool operator<(MWAWVec2<T> const &p) const
  {
    return cmpY(p) < 0;
  }
  //! a comparison function: which first compares x then y
  int cmp(MWAWVec2<T> const &p) const
  {
    if (m_x < p.m_x) return -1;
    if (m_x > p.m_x) return 1;
    if (m_y < p.m_y) return -1;
    if (m_y > p.m_y) return 1;
    return 0;
  }
  //! a comparison function: which first compares y then x
  int cmpY(MWAWVec2<T> const &p) const
  {
    if (m_y < p.m_y) return -1;
    if (m_y > p.m_y) return 1;
    if (m_x < p.m_x) return -1;
    if (m_x > p.m_x) return 1;
    return 0;
  }

  //! operator<<: prints data in form "XxY"
  friend std::ostream &operator<< (std::ostream &o, MWAWVec2<T> const &f)
  {
    o << f.m_x << "x" << f.m_y;
    return o;
  }

  /*! \struct PosSizeLtX
   * \brief internal struct used to create sorted map, sorted by X
   */
  struct PosSizeLtX {
    //! comparaison function
    bool operator()(MWAWVec2<T> const &s1, MWAWVec2<T> const &s2) const
    {
      return s1.cmp(s2) < 0;
    }
  };
  /*! \typedef MapX
   *  \brief map of MWAWVec2
   */
  typedef std::map<MWAWVec2<T>, T,struct PosSizeLtX> MapX;

  /*! \struct PosSizeLtY
   * \brief internal struct used to create sorted map, sorted by Y
   */
  struct PosSizeLtY {
    //! comparaison function
    bool operator()(MWAWVec2<T> const &s1, MWAWVec2<T> const &s2) const
    {
      return s1.cmpY(s2) < 0;
    }
  };
  /*! \typedef MapY
   *  \brief map of MWAWVec2
   */
  typedef std::map<MWAWVec2<T>, T,struct PosSizeLtY> MapY;
protected:
  T m_x/*! \brief first element */, m_y/*! \brief second element */;
};

/*! \brief MWAWVec2 of bool */
typedef MWAWVec2<bool> MWAWVec2b;
/*! \brief MWAWVec2 of int */
typedef MWAWVec2<int> MWAWVec2i;
/*! \brief MWAWVec2 of long */
typedef MWAWVec2<long> MWAWVec2l;
/*! \brief MWAWVec2 of float */
typedef MWAWVec2<float> MWAWVec2f;

/*! \class MWAWVec3
 *   \brief small class which defines a vector with 3 elements
 */
template <class T> class MWAWVec3
{
public:
  //! constructor
  explicit MWAWVec3(T xx=0,T yy=0,T zz=0)
  {
    m_val[0] = xx;
    m_val[1] = yy;
    m_val[2] = zz;
  }
  //! generic copy constructor
  template <class U> explicit MWAWVec3(MWAWVec3<U> const &p)
  {
    for (int c = 0; c < 3; c++) m_val[c] = T(p[c]);
  }

  //! first element
  T x() const
  {
    return m_val[0];
  }
  //! second element
  T y() const
  {
    return m_val[1];
  }
  //! third element
  T z() const
  {
    return m_val[2];
  }
  //! operator[]
  T operator[](int c) const
  {
    if (c<0 || c>2) throw libmwaw::GenericException();
    return m_val[c];
  }
  //! operator[]
  T &operator[](int c)
  {
    if (c<0 || c>2) throw libmwaw::GenericException();
    return m_val[c];
  }

  //! resets the three elements
  void set(T xx, T yy, T zz)
  {
    m_val[0] = xx;
    m_val[1] = yy;
    m_val[2] = zz;
  }
  //! resets the first element
  void setX(T xx)
  {
    m_val[0] = xx;
  }
  //! resets the second element
  void setY(T yy)
  {
    m_val[1] = yy;
  }
  //! resets the third element
  void setZ(T zz)
  {
    m_val[2] = zz;
  }

  //! increases the actuals values by \a dx, \a dy, \a dz
  void add(T dx, T dy, T dz)
  {
    m_val[0] += dx;
    m_val[1] += dy;
    m_val[2] += dz;
  }

  //! operator+=
  MWAWVec3<T> &operator+=(MWAWVec3<T> const &p)
  {
    for (int c = 0; c < 3; c++) m_val[c] = T(m_val[c]+p.m_val[c]);
    return *this;
  }
  //! operator-=
  MWAWVec3<T> &operator-=(MWAWVec3<T> const &p)
  {
    for (int c = 0; c < 3; c++) m_val[c] = T(m_val[c]-p.m_val[c]);
    return *this;
  }
  //! generic operator*=
  template <class U>
  MWAWVec3<T> &operator*=(U scale)
  {
    for (auto &c : m_val) c = T(c*scale);
    return *this;
  }

  //! operator+
  friend MWAWVec3<T> operator+(MWAWVec3<T> const &p1, MWAWVec3<T> const &p2)
  {
    MWAWVec3<T> p(p1);
    return p+=p2;
  }
  //! operator-
  friend MWAWVec3<T> operator-(MWAWVec3<T> const &p1, MWAWVec3<T> const &p2)
  {
    MWAWVec3<T> p(p1);
    return p-=p2;
  }
  //! generic operator*
  template <class U>
  friend MWAWVec3<T> operator*(U scale, MWAWVec3<T> const &p1)
  {
    MWAWVec3<T> p(p1);
    return p *= scale;
  }

  //! comparison==
  bool operator==(MWAWVec3<T> const &p) const
  {
    return cmp(p) == 0;
  }
  //! comparison!=
  bool operator!=(MWAWVec3<T> const &p) const
  {
    return cmp(p) != 0;
  }
  //! comparison<: which first compares x values, then y values then z values.
  bool operator<(MWAWVec3<T> const &p) const
  {
    return cmp(p) < 0;
  }
  //! a comparison function: which first compares x values, then y values then z values.
  int cmp(MWAWVec3<T> const &p) const
  {
    for (int c = 0; c < 3; c++) {
      if (m_val[c]<p.m_val[c]) return -1;
      if (m_val[c]>p.m_val[c]) return 1;
    }
    return 0;
  }

  //! operator<<: prints data in form "XxYxZ"
  friend std::ostream &operator<< (std::ostream &o, MWAWVec3<T> const &f)
  {
    o << f.m_val[0] << "x" << f.m_val[1] << "x" << f.m_val[2];
    return o;
  }

  /*! \struct PosSizeLt
   * \brief internal struct used to create sorted map, sorted by X, Y, Z
   */
  struct PosSizeLt {
    //! comparaison function
    bool operator()(MWAWVec3<T> const &s1, MWAWVec3<T> const &s2) const
    {
      return s1.cmp(s2) < 0;
    }
  };
  /*! \typedef Map
   *  \brief map of MWAWVec3
   */
  typedef std::map<MWAWVec3<T>, T,struct PosSizeLt> Map;

protected:
  //! the values
  T m_val[3];
};

/*! \brief MWAWVec3 of unsigned char */
typedef MWAWVec3<unsigned char> MWAWVec3uc;
/*! \brief MWAWVec3 of int */
typedef MWAWVec3<int> MWAWVec3i;
/*! \brief MWAWVec3 of float */
typedef MWAWVec3<float> MWAWVec3f;

/*! \class MWAWBox2
 *   \brief small class which defines a 2D Box
 */
template <class T> class MWAWBox2
{
public:
  //! constructor
  explicit MWAWBox2(MWAWVec2<T> minPt=MWAWVec2<T>(), MWAWVec2<T> maxPt=MWAWVec2<T>())
    : m_data(minPt, maxPt)
  {
  }
  //! generic constructor
  template <class U> explicit MWAWBox2(MWAWBox2<U> const &p)
    : m_data(MWAWVec2<T>(p.min()), MWAWVec2<T>(p.max()))
  {
  }

  //! the minimum 2D point (in x and in y)
  MWAWVec2<T> const &min() const
  {
    return m_data.first;
  }
  //! the maximum 2D point (in x and in y)
  MWAWVec2<T> const &max() const
  {
    return m_data.second;
  }
  //! the minimum 2D point (in x and in y)
  MWAWVec2<T> &min()
  {
    return m_data.first;
  }
  //! the maximum 2D point (in x and in y)
  MWAWVec2<T> &max()
  {
    return m_data.second;
  }
  /*! \brief the two extremum points which defined the box
   * \param c 0 means the minimum and 1 the maximum
   */
  MWAWVec2<T> const &operator[](int c) const
  {
    if (c<0 || c>1) throw libmwaw::GenericException();
    return c==0 ? m_data.first : m_data.second;
  }
  //! the box size
  MWAWVec2<T> size() const
  {
    return m_data.second-m_data.first;
  }
  //! the box center
  MWAWVec2<T> center() const
  {
    return MWAWVec2<T>((m_data.first.x()+m_data.second.x())/2,
                       (m_data.first.y()+m_data.second.y())/2);
  }

  //! resets the data to minimum \a x and maximum \a y
  void set(MWAWVec2<T> const &x, MWAWVec2<T> const &y)
  {
    m_data.first = x;
    m_data.second = y;
  }
  //! resets the minimum point
  void setMin(MWAWVec2<T> const &x)
  {
    m_data.first = x;
  }
  //! resets the maximum point
  void setMax(MWAWVec2<T> const &y)
  {
    m_data.second = y;
  }

  //!  resize the box keeping the minimum
  void resizeFromMin(MWAWVec2<T> const &sz)
  {
    m_data.second = m_data.first+sz;
  }
  //!  resize the box keeping the maximum
  void resizeFromMax(MWAWVec2<T> const &sz)
  {
    m_data.first = m_data.second-sz;
  }
  //!  resize the box keeping the center
  void resizeFromCenter(MWAWVec2<T> const &sz)
  {
    MWAWVec2<T> centerPt = center();
    MWAWVec2<T> decal(sz.x()/2,sz.y()/2);
    m_data.first = centerPt - decal;
    m_data.second = centerPt + (sz - decal);
  }

  //! scales all points of the box by \a factor
  template <class U> void scale(U factor)
  {
    m_data.first *= factor;
    m_data.second *= factor;
  }

  //! extends the bdbox by (\a val, \a val) keeping the center
  void extend(T val)
  {
    m_data.first -= MWAWVec2<T>(val/2,val/2);
    m_data.second += MWAWVec2<T>(val-(val/2),val-(val/2));
  }

  //! returns the union between this and box
  MWAWBox2<T> getUnion(MWAWBox2<T> const &box) const
  {
    MWAWBox2<T> res;
    res.m_data.first=MWAWVec2<T>(m_data.first[0]<box.m_data.first[0]?m_data.first[0] : box.m_data.first[0],
                                 m_data.first[1]<box.m_data.first[1]?m_data.first[1] : box.m_data.first[1]);
    res.m_data.second=MWAWVec2<T>(m_data.second[0]>box.m_data.second[0]?m_data.second[0] : box.m_data.second[0],
                                  m_data.second[1]>box.m_data.second[1]?m_data.second[1] : box.m_data.second[1]);
    return res;
  }
  //! returns the intersection between this and box
  MWAWBox2<T> getIntersection(MWAWBox2<T> const &box) const
  {
    MWAWBox2<T> res;
    res.m_data.first=MWAWVec2<T>(m_data.first[0]>box.m_data.first[0]?m_data.first[0] : box.m_data.first[0],
                                 m_data.first[1]>box.m_data.first[1]?m_data.first[1] : box.m_data.first[1]);
    res.m_data.second=MWAWVec2<T>(m_data.second[0]<box.m_data.second[0]?m_data.second[0] : box.m_data.second[0],
                                  m_data.second[1]<box.m_data.second[1]?m_data.second[1] : box.m_data.second[1]);
    return res;
  }
  //! operator==
  bool operator==(MWAWBox2<T> const &mat) const
  {
    return m_data==mat.m_data;
  }
  //! operator!=
  bool operator!=(MWAWBox2<T> const &mat) const
  {
    return m_data!=mat.m_data;
  }
  //! operator<
  bool operator<(MWAWBox2<T> const &mat) const
  {
    return m_data<mat.m_data;
  }
  //! operator<=
  bool operator<=(MWAWBox2<T> const &mat) const
  {
    return m_data<=mat.m_data;
  }
  //! operator>
  bool operator>(MWAWBox2<T> const &mat) const
  {
    return m_data>mat.m_data;
  }
  //! operator>=
  bool operator>=(MWAWBox2<T> const &mat) const
  {
    return m_data>=mat.m_data;
  }
  //! print data in form X0xY0<->X1xY1
  friend std::ostream &operator<< (std::ostream &o, MWAWBox2<T> const &f)
  {
    o << "(" << f.min() << "<->" << f.max() << ")";
    return o;
  }

protected:
  //! the data
  std::pair<MWAWVec2<T>, MWAWVec2<T> > m_data;
};

/*! \brief MWAWBox2 of int */
typedef MWAWBox2<int> MWAWBox2i;
/*! \brief MWAWBox2 of float */
typedef MWAWBox2<float> MWAWBox2f;
/*! \brief MWAWBox2 of long */
typedef MWAWBox2<long> MWAWBox2l;

/** a transformation which stored the first row of a 3x3 perspective matrix */
class MWAWTransformation
{
public:
  //! constructor
  explicit MWAWTransformation(MWAWVec3f const &xRow=MWAWVec3f(1,0,0), MWAWVec3f const &yRow=MWAWVec3f(0,1,0))
    : m_data(xRow, yRow)
    , m_isIdentity(false)
  {
    checkIdentity();
  }
  //! returns true if the matrix is an identity matrix
  bool isIdentity() const
  {
    return m_isIdentity;
  }
  //! check if a matrix is the identity matrix
  void checkIdentity() const
  {
    m_isIdentity= m_data.first==MWAWVec3f(1,0,0) && m_data.second==MWAWVec3f(0,1,0);
  }
  /*! \brief the two extremum points which defined the box
   * \param c 0 means the minimum and 1 the maximum
   */
  MWAWVec3f const &operator[](int c) const
  {
    if (c<0 || c>1) throw libmwaw::GenericException();
    return c==0 ? m_data.first : m_data.second;
  }
  //! operator* for vec2f
  MWAWVec2f operator*(MWAWVec2f const &pt) const
  {
    if (m_isIdentity) return pt;
    return multiplyDirection(pt)+MWAWVec2f(m_data.first[2],m_data.second[2]);
  }
  //! operator* for direction
  MWAWVec2f multiplyDirection(MWAWVec2f const &dir) const
  {
    if (m_isIdentity) return dir;
    MWAWVec2f res;
    for (int coord=0; coord<2; ++coord) {
      MWAWVec3f const &row=coord==0 ? m_data.first : m_data.second;
      float value=0;
      for (int i=0; i<2; ++i)
        value+=row[i]*dir[i];
      res[coord]=value;
    }
    return res;
  }
  //! operator* for box2f
  MWAWBox2f operator*(MWAWBox2f const &box) const
  {
    if (m_isIdentity) return box;
    return MWAWBox2f(operator*(box.min()), operator*(box.max()));
  }
  //! operator* for transform
  MWAWTransformation operator*(MWAWTransformation const &mat) const
  {
    if (mat.m_isIdentity) return *this;
    MWAWTransformation res;
    for (int row=0; row<2; ++row) {
      MWAWVec3f &resRow=row==0 ? res.m_data.first : res.m_data.second;
      for (int col=0; col<3; ++col) {
        float value=0;
        for (int i=0; i<3; ++i)
          value+=(*this)[row][i]*(i==2 ? (col==2 ? 1.f : 0.f) : mat[i][col]);
        resRow[col]=value;
      }
    }
    res.checkIdentity();
    return res;
  }
  //! operator*=
  MWAWTransformation &operator*=(MWAWTransformation const &mat)
  {
    if (!mat.m_isIdentity)
      *this=(*this)*mat;
    return *this;
  }
  //! operator==
  bool operator==(MWAWTransformation const &mat) const
  {
    return m_data==mat.m_data;
  }
  //! operator!=
  bool operator!=(MWAWTransformation const &mat) const
  {
    return m_data!=mat.m_data;
  }
  //! operator<
  bool operator<(MWAWTransformation const &mat) const
  {
    return m_data<mat.m_data;
  }
  //! operator<=
  bool operator<=(MWAWTransformation const &mat) const
  {
    return m_data<=mat.m_data;
  }
  //! operator>
  bool operator>(MWAWTransformation const &mat) const
  {
    return m_data>mat.m_data;
  }
  //! operator>=
  bool operator>=(MWAWTransformation const &mat) const
  {
    return m_data>=mat.m_data;
  }
  /** try to decompose the matrix in a rotation + scaling/translation matrix.

      Note: the center of rotation is given before applying the transformation(this) */
  bool decompose(float &rotation, MWAWVec2f &shearing, MWAWTransformation &transform, MWAWVec2f const &center) const;

  /** returns a translation transformation */
  static MWAWTransformation translation(MWAWVec2f const &trans)
  {
    return MWAWTransformation(MWAWVec3f(1, 0, trans[0]), MWAWVec3f(0, 1, trans[1]));
  }
  /** returns a scaling transformation */
  static MWAWTransformation scale(MWAWVec2f const &trans)
  {
    return MWAWTransformation(MWAWVec3f(trans[0], 0, 0), MWAWVec3f(0, trans[1], 0));
  }
  /** returns a rotation transformation around center.

   \note angle must be given in degree */
  static MWAWTransformation rotation(float angle, MWAWVec2f const &center=MWAWVec2f(0,0));
  /** returns a shear transformation letting center invariant, ie. a matrix
      ( 1 s[0] -s[0]*center[1], s[1] 1 -s[1]*center[0], 0 0 1)
   */
  static MWAWTransformation shear(MWAWVec2f s, MWAWVec2f const &center=MWAWVec2f(0,0))
  {
    return MWAWTransformation(MWAWVec3f(1, s[0], -s[0]*center[1]), MWAWVec3f(s[1], 1, -s[1]*center[0]));
  }
protected:
  //! the data
  std::pair<MWAWVec3f, MWAWVec3f > m_data;
  //! flag to know if this matrix is an identity matrix
  mutable bool m_isIdentity;
};

// some format function
namespace libmwaw
{
//! convert a DTFormat in a propertyList
bool convertDTFormat(std::string const &dtFormat, librevenge::RVNGPropertyListVector &propVect);
}

// some geometrical function
namespace libmwaw
{
//! rotate a point around center, angle is given in degree
MWAWVec2f rotatePointAroundCenter(MWAWVec2f const &point, MWAWVec2f const &center, float angle);
//! rotate a bdox and returns the final bdbox, angle is given in degree
MWAWBox2f rotateBoxFromCenter(MWAWBox2f const &box, float angle);
}
#endif /* LIBMWAW_INTERNAL_H */
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
