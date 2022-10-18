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
#ifndef MWAW_GRAPHIC_STYLE
#  define MWAW_GRAPHIC_STYLE
#  include <ostream>
#  include <string>
#  include <vector>

#  include "librevenge/librevenge.h"
#  include "libmwaw_internal.hxx"

/** a structure used to define a picture style

 \note in order to define the internal surface style, first it looks for
 a gradient, if so it uses it. Then it looks for a pattern. Finally if
 it found nothing, it uses surfaceColor and surfaceOpacity.*/
class MWAWGraphicStyle
{
public:
  //! an enum used to define the basic line cap
  enum LineCap { C_Butt, C_Square, C_Round };
  //! an enum used to define the basic line join
  enum LineJoin { J_Miter, J_Round, J_Bevel };
  /**  an enum used to define the vertical alignment
       \note actually mainly used for text box */
  enum VerticalAlignment { V_AlignBottom, V_AlignCenter, V_AlignJustify, V_AlignTop, V_AlignDefault };

  //! a structure used to define an arrow
  struct Arrow {
    //! constructor ( no arrow)
    Arrow()
      : m_viewBox()
      , m_path("")
      , m_width(0)
      , m_isCentered(false)
    {
    }
    //! constructor
    Arrow(float w, MWAWBox2i const &box, std::string const &path, bool centered=false)
      : m_viewBox(box)
      , m_path(path)
      , m_width(w)
      , m_isCentered(centered)
    {
    }
    //! returns a basic plain arrow
    static Arrow plain()
    {
      return Arrow(5, MWAWBox2i(MWAWVec2i(0,0),MWAWVec2i(20,30)), "m10 0-10 30h20z", false);
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Arrow const &arrow)
    {
      if (arrow.isEmpty()) return o;
      o << "w=" << arrow.m_width << ",";
      o << "viewbox=" << arrow.m_viewBox << ",";
      o << "path=" << arrow.m_path << ",";
      if (arrow.m_isCentered) o << "centered,";
      return o;
    }
    //! operator==
    bool operator==(Arrow const &arrow) const
    {
      return m_width>=arrow.m_width && m_width<=arrow.m_width &&
             m_viewBox==arrow.m_viewBox && m_path==arrow.m_path && m_isCentered==arrow.m_isCentered;
    }
    //! operator!=
    bool operator!=(Arrow const &arrow) const
    {
      return !(*this==arrow);
    }
    //! operator<
    bool operator<(Arrow const &arrow) const
    {
      if (m_isCentered<arrow.m_isCentered) return m_isCentered ? true : false;
      return m_width<arrow.m_width && m_viewBox<arrow.m_viewBox && m_path < arrow.m_path;
    }
    //! operator<=
    bool operator<=(Arrow const &arrow) const
    {
      return *this<arrow || *this==arrow;
    }
    //! operator>
    bool operator>(Arrow const &arrow) const
    {
      return !(*this<=arrow);
    }
    //! operator>=
    bool operator>=(Arrow const &arrow) const
    {
      return !(*this<arrow);
    }
    //! returns true if there is no arrow
    bool isEmpty() const
    {
      return m_width<=0 || m_path.empty();
    }
    //! add a arrow to the propList knowing the type (start, end)
    void addTo(librevenge::RVNGPropertyList &propList, std::string const &type) const;

    //! the arrow viewbox
    MWAWBox2i m_viewBox;
    //! the arrow path
    std::string m_path;
    //! the arrow width in point
    float m_width;
    //! flag to know if the arrow is centered
    bool m_isCentered;
  };

  /** a basic gradient used in a MWAWGraphicStyle */
  struct Gradient {
    //! a structure used to define the gradient limit in MWAWGraphicStyle
    struct Stop {
      //! constructor
      explicit Stop(float offset=0.0, MWAWColor const &col=MWAWColor::black(), float opacity=1.0)
        : m_offset(offset)
        , m_color(col)
        , m_opacity(opacity)
      {
      }
      /** compare two gradient */
      int cmp(Stop const &a) const
      {
        if (m_offset < a.m_offset) return -1;
        if (m_offset > a.m_offset) return 1;
        if (m_color < a.m_color) return -1;
        if (m_color > a.m_color) return 1;
        if (m_opacity < a.m_opacity) return -1;
        if (m_opacity > a.m_opacity) return 1;
        return 0;
      }
      //! a print operator
      friend std::ostream &operator<<(std::ostream &o, Stop const &st)
      {
        o << "offset=" << st.m_offset << ",";
        o << "color=" << st.m_color << ",";
        if (st.m_opacity<1)
          o << "opacity=" << st.m_opacity*100.f << "%,";
        return o;
      }
      //! the offset
      float m_offset;
      //! the color
      MWAWColor m_color;
      //! the opacity
      float m_opacity;
    };
    //! an enum used to define the gradient type
    enum Type { G_None, G_Axial, G_Linear, G_Radial, G_Rectangular, G_Square, G_Ellipsoid };
    //! constructor
    Gradient()
      : m_type(G_None)
      , m_stopList()
      , m_angle(0)
      , m_border(0)
      , m_percentCenter(0.5f,0.5f)
      , m_radius(1)
    {
      m_stopList.push_back(Stop(0.0, MWAWColor::white()));
      m_stopList.push_back(Stop(1.0, MWAWColor::black()));
    }
    //! returns true if the gradient is defined
    bool hasGradient(bool complex=false) const
    {
      return m_type != G_None && static_cast<int>(m_stopList.size()) >= (complex ? 3 : 2);
    }
    //! add a gradient to the propList
    void addTo(librevenge::RVNGPropertyList &propList) const;
    /** returns the average gradient color if the gradient is defined. */
    bool getAverageColor(MWAWColor &color) const;
    //! a print operator
    friend std::ostream &operator<<(std::ostream &o, Gradient const &grad)
    {
      switch (grad.m_type) {
      case Gradient::G_Axial:
        o << "axial,";
        break;
      case Gradient::G_Linear:
        o << "linear,";
        break;
      case Gradient::G_Radial:
        o << "radial,";
        break;
      case Gradient::G_Rectangular:
        o << "rectangular,";
        break;
      case Gradient::G_Square:
        o << "square,";
        break;
      case Gradient::G_Ellipsoid:
        o << "ellipsoid,";
        break;
      case Gradient::G_None:
#if !defined(__clang__)
      default:
#endif
        break;
      }
      if (grad.m_angle>0 || grad.m_angle<0) o << "angle=" << grad.m_angle << ",";
      if (grad.m_stopList.size() >= 2) {
        o << "stops=[";
        for (auto const &stop : grad.m_stopList)
          o << "[" << stop << "],";
        o << "],";
      }
      if (grad.m_border>0) o << "border=" << grad.m_border*100 << "%,";
      if (grad.m_percentCenter != MWAWVec2f(0.5f,0.5f)) o << "center=" << grad.m_percentCenter << ",";
      if (grad.m_radius<1) o << "radius=" << grad.m_radius << ",";
      return o;
    }

    /** compare two gradient */
    int cmp(Gradient const &a) const
    {
      if (m_type < a.m_type) return -1;
      if (m_type > a.m_type) return 1;
      if (m_angle < a.m_angle) return -1;
      if (m_angle > a.m_angle) return 1;
      if (m_stopList.size() < a.m_stopList.size()) return 1;
      if (m_stopList.size() > a.m_stopList.size()) return -1;
      for (auto c :m_stopList) {
        int diff = c.cmp(c);
        if (diff) return diff;
      }
      if (m_border < a.m_border) return -1;
      if (m_border > a.m_border) return 1;
      int diff=m_percentCenter.cmp(a.m_percentCenter);
      if (diff) return diff;
      if (m_radius < a.m_radius) return -1;
      if (m_radius > a.m_radius) return 1;
      return 0;
    }
    //! the gradient type
    Type m_type;
    //! the list of gradient limits
    std::vector<Stop> m_stopList;
    //! the gradient angle
    float m_angle;
    //! the gradient border opacity
    float m_border;
    //! the gradient center
    MWAWVec2f m_percentCenter;
    //! the gradient radius
    float m_radius;
  };

  /** a basic hatch used in  MWAWGraphicStyle */
  struct Hatch {
    //! the potential type
    enum Type { H_None, H_Single, H_Double, H_Triple };
    //! constructor
    Hatch()
      : m_type(H_None)
      , m_color(MWAWColor::black())
      , m_distance(1.f/72)
      , m_rotation(0)
    {
    }
    //! returns true if the gradient is defined
    bool hasHatch() const
    {
      return m_type != H_None && m_distance>0;
    }
    //! add a hatch to the propList
    void addTo(librevenge::RVNGPropertyList &propList) const;
    //! a print operator
    friend std::ostream &operator<<(std::ostream &o, Hatch const &hatch)
    {
      if (hatch.m_type==H_None || hatch.m_distance<=0)
        return o;
      switch (hatch.m_type) {
      case Hatch::H_None:
        break;
      case Hatch::H_Single:
        o << "single,";
        break;
      case Hatch::H_Double:
        o << "double,";
        break;
      case Hatch::H_Triple:
        o << "triple,";
        break;
      default:
        o << "###type=" << int(hatch.m_type) << ",";
        break;
      }
      if (!hatch.m_color.isBlack())
        o << hatch.m_color << ",";
      o << "dist=" << 72*hatch.m_distance << "pt,";
      if (hatch.m_rotation>0 || hatch.m_rotation<0)
        o << "rot=" << hatch.m_rotation << "deg,";
      return o;
    }
    /** compare two hatchs */
    int cmp(Hatch const &a) const
    {
      if (m_type < a.m_type) return -1;
      if (m_type > a.m_type) return 1;
      if (m_color < a.m_color) return -1;
      if (m_color > a.m_color) return 1;
      if (m_distance < a.m_distance) return -1;
      if (m_distance > a.m_distance) return 1;
      if (m_rotation < a.m_rotation) return -1;
      if (m_rotation > a.m_rotation) return 1;
      return 0;
    }
    //! the hatch type
    Type m_type;
    //! the hatch color
    MWAWColor m_color;
    //! the hatch distance in inches
    float m_distance;
    //! the rotation (in degrees)
    float m_rotation;
  };
  /** a basic pattern used in a MWAWGraphicStyle:
      - either given a list of 8x8, 16x16, 32x32 bytes with two colors
      - or with a picture ( and an average color)
   */
  struct Pattern {
    //! constructor
    Pattern()
      : m_dim(0,0)
      , m_data()
      , m_picture()
      , m_pictureAverageColor(MWAWColor::white())
    {
      m_colors[0]=MWAWColor::black();
      m_colors[1]=MWAWColor::white();
    }
    //!  constructor from a binary data
    Pattern(MWAWVec2i dim, MWAWEmbeddedObject const &picture, MWAWColor const &avColor)
      : m_dim(dim)
      , m_data()
      , m_picture(picture)
      , m_pictureAverageColor(avColor)
    {
      m_colors[0]=MWAWColor::black();
      m_colors[1]=MWAWColor::white();
    }
    Pattern(Pattern const &)=default;
    Pattern &operator=(Pattern const &)=default;
    Pattern &operator=(Pattern &&)=default;
    //! virtual destructor
    virtual ~Pattern();
    //! return true if we does not have a pattern
    bool empty() const
    {
      if (m_dim[0]==0 || m_dim[1]==0) return true;
      if (!m_picture.m_dataList.empty()) return false;
      if (m_dim[0]!=8 && m_dim[0]!=16 && m_dim[0]!=32) return true;
      return m_data.size()!=size_t((m_dim[0]/8)*m_dim[1]);
    }
    //! return the average color
    bool getAverageColor(MWAWColor &col) const;
    //! check if the pattern has only one color; if so returns true...
    bool getUniqueColor(MWAWColor &col) const;
    /** tries to convert the picture in a binary data ( ppm) */
    bool getBinary(MWAWEmbeddedObject &picture) const;

    /** compare two patterns */
    int cmp(Pattern const &a) const
    {
      int diff = m_dim.cmp(a.m_dim);
      if (diff) return diff;
      if (m_data.size() < a.m_data.size()) return -1;
      if (m_data.size() > a.m_data.size()) return 1;
      for (size_t h=0; h < m_data.size(); ++h) {
        if (m_data[h]<a.m_data[h]) return 1;
        if (m_data[h]>a.m_data[h]) return -1;
      }
      for (int i=0; i<2; ++i) {
        if (m_colors[i] < a.m_colors[i]) return 1;
        if (m_colors[i] > a.m_colors[i]) return -1;
      }
      if (m_pictureAverageColor < a.m_pictureAverageColor) return 1;
      if (m_pictureAverageColor > a.m_pictureAverageColor) return -1;
      diff=m_picture.cmp(a.m_picture);
      if (diff) return diff;
      return 0;
    }
    //! a print operator
    friend std::ostream &operator<<(std::ostream &o, Pattern const &pat)
    {
      o << "dim=" << pat.m_dim << ",";
      if (!pat.m_picture.isEmpty()) {
        o << "pict=" << pat.m_picture << ",";
        o << "col[average]=" << pat.m_pictureAverageColor << ",";
      }
      else {
        if (!pat.m_colors[0].isBlack()) o << "col0=" << pat.m_colors[0] << ",";
        if (!pat.m_colors[1].isWhite()) o << "col1=" << pat.m_colors[1] << ",";
        o << "[";
        for (auto data : pat.m_data)
          o << std::hex << static_cast<int>(data) << std::dec << ",";
        o << "],";
      }
      return o;
    }
    //! the dimension width x height
    MWAWVec2i m_dim;

    //! the two indexed colors
    MWAWColor m_colors[2];
    //! the pattern data: a sequence of data: p[0..7,0],p[8..15,0]...p[0..7,1],p[8..15,1], ...
    std::vector<unsigned char> m_data;
  protected:
    //! a picture
    MWAWEmbeddedObject m_picture;
    //! the picture average color
    MWAWColor m_pictureAverageColor;
  };
  //! constructor
  MWAWGraphicStyle()
    : m_lineDashWidth()
    , m_lineWidth(1)
    , m_lineCap(C_Butt)
    , m_lineJoin(J_Miter)
    , m_lineOpacity(1)
    , m_lineColor(MWAWColor::black())
    , m_surfaceColor(MWAWColor::white())
    , m_surfaceOpacity(0)
    , m_shadowColor(MWAWColor::black())
    , m_shadowOpacity(0)
    , m_shadowOffset(1,1)
    , m_pattern()
    , m_gradient()
    , m_hatch()
    , m_backgroundColor(MWAWColor::white())
    , m_backgroundOpacity(-1)
    , m_rotate(0)
    , m_bordersList()
    , m_frameName("")
    , m_frameNextName("")
    , m_fillRuleEvenOdd(false)
    , m_doNotPrint(false)
    , m_verticalAlignment(V_AlignDefault)
    , m_extra("")
  {
    for (auto &fl : m_flip) fl=false;
  }
  MWAWGraphicStyle(MWAWGraphicStyle const &)=default;
  MWAWGraphicStyle &operator=(MWAWGraphicStyle const &)=default;
  MWAWGraphicStyle &operator=(MWAWGraphicStyle &&)=default;
  /** returns an empty style. Can be used to initialize a default frame style...*/
  static MWAWGraphicStyle emptyStyle()
  {
    MWAWGraphicStyle res;
    res.m_lineWidth=0;
    return res;
  }
  //! virtual destructor
  virtual ~MWAWGraphicStyle();
  //! returns true if the border is defined
  bool hasLine() const
  {
    return m_lineWidth>0 && m_lineOpacity>0;
  }
  //! set the surface color
  void setSurfaceColor(MWAWColor const &col, float opacity = 1)
  {
    m_surfaceColor = col;
    m_surfaceOpacity = opacity;
  }
  //! returns true if the surface is defined
  bool hasSurfaceColor() const
  {
    return m_surfaceOpacity > 0;
  }
  //! set the pattern
  void setPattern(Pattern const &pat, float opacity = 1)
  {
    m_pattern=pat;
    m_surfaceOpacity = opacity;
  }
  //! returns true if the pattern is defined
  bool hasPattern() const
  {
    return !m_pattern.empty() && m_surfaceOpacity > 0;
  }
  //! returns true if the gradient is defined
  bool hasGradient(bool complex=false) const
  {
    return m_gradient.hasGradient(complex);
  }
  //! returns true if the hatch is defined
  bool hasHatch() const
  {
    return m_hatch.hasHatch();
  }
  //! returns true if the interior surface is defined
  bool hasSurface() const
  {
    return hasSurfaceColor() || hasPattern() || hasGradient() || hasHatch();
  }
  //! set the background color
  void setBackgroundColor(MWAWColor const &col, float opacity = 1)
  {
    m_backgroundColor = col;
    m_backgroundOpacity = opacity;
  }
  //! returns true if the background is defined
  bool hasBackgroundColor() const
  {
    return m_backgroundOpacity > 0;
  }
  //! set the shadow color
  void setShadowColor(MWAWColor const &col, float opacity = 1)
  {
    m_shadowColor = col;
    m_shadowOpacity = opacity;
  }
  //! returns true if the shadow is defined
  bool hasShadow() const
  {
    return m_shadowOpacity > 0;
  }
  //! return true if the frame has some border
  bool hasBorders() const
  {
    return !m_bordersList.empty();
  }
  //! return true if the frame has some border
  bool hasSameBorders() const
  {
    if (m_bordersList.empty()) return true;
    if (m_bordersList.size()!=4) return false;
    for (size_t i=1; i<m_bordersList.size(); ++i) {
      if (m_bordersList[i]!=m_bordersList[0])
        return false;
    }
    return true;
  }
  //! return the frame border: libmwaw::Left | ...
  std::vector<MWAWBorder> const &borders() const
  {
    return m_bordersList;
  }
  //! reset the border
  void resetBorders()
  {
    m_bordersList.resize(0);
  }
  //! sets the cell border: wh=libmwaw::LeftBit|...
  void setBorders(int wh, MWAWBorder const &border);
  //! a print operator
  friend std::ostream &operator<<(std::ostream &o, MWAWGraphicStyle const &st);
  //! add all the parameters to the propList excepted the frame parameter: the background and the borders
  void addTo(librevenge::RVNGPropertyList &pList, bool only1d=false) const;
  //! add all the frame parameters to propList: the background and the borders
  void addFrameTo(librevenge::RVNGPropertyList &pList) const;
  /** compare two styles */
  int cmp(MWAWGraphicStyle const &a) const;

  //! the dash array: a sequence of (fullsize, emptysize)
  std::vector<float> m_lineDashWidth;
  //! the linewidth
  float m_lineWidth;
  //! the line cap
  LineCap m_lineCap;
  //! the line join
  LineJoin m_lineJoin;
  //! the line opacity: 0=transparent
  float m_lineOpacity;
  //! the line color
  MWAWColor m_lineColor;
  //! the surface color
  MWAWColor m_surfaceColor;
  //! true if the surface has some color
  float m_surfaceOpacity;

  //! the shadow color
  MWAWColor m_shadowColor;
  //! true if the shadow has some color
  float m_shadowOpacity;
  //! the shadow offset
  MWAWVec2f m_shadowOffset;

  //! the pattern if it exists
  Pattern m_pattern;

  //! the gradient
  Gradient m_gradient;
  //! the hatch
  Hatch m_hatch;

  //
  // related to the frame
  //

  //! the background color
  MWAWColor m_backgroundColor;
  //! true if the background has some color
  float m_backgroundOpacity;
  //! the rotation
  float m_rotate;
  //! the borders MWAWBorder::Pos (for a frame)
  std::vector<MWAWBorder> m_bordersList;
  //! the frame name
  std::string m_frameName;
  //! the frame next name (if there is a link)
  std::string m_frameNextName;

  //! the two arrows corresponding to start and end extremity
  Arrow m_arrows[2];

  //
  // some transformation: must probably be somewhere else
  //

  //! two bool to indicated we need to flip the shape or not
  bool m_flip[2];

  //! true if the fill rule is evenod
  bool m_fillRuleEvenOdd;

  //! a bool to know if the shape must not be printed
  bool m_doNotPrint;

  //! related to text area
  VerticalAlignment m_verticalAlignment;

  //! extra data
  std::string m_extra;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
