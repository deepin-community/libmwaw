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

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include <librevenge/librevenge.h>

#include "MWAWFontConverter.hxx"
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

#include "ReadySetGoParser.hxx"

/** Internal: the structures of a ReadySetGoParser */
namespace ReadySetGoParserInternal
{

////////////////////////////////////////
//! Internal: a shape in a ReadySetGoParser document
struct Shape {
  //! the shape type
  enum Type { T_Empty, T_Line, T_Oval, T_Picture, T_Rectangle, T_RectOval, T_Text, T_Unknown };
  //! constructor
  explicit Shape(Type type)
    : m_type(type)
    , m_box()
    , m_style(MWAWGraphicStyle::emptyStyle())
    , m_wrapRoundAround(false)
    , m_cornerSize(-1,-1)
    , m_textId(-1)
    , m_paragraph()
    , m_hasPicture(false)
  {
    for (auto &link : m_linkIds) link=-1;
    for (auto &cPos : m_textPositions) cPos=-1;
  }

  //! the shape type
  Type m_type;
  //! the bounding box
  MWAWBox2f m_box;
  //! the graphic style
  MWAWGraphicStyle m_style;
  //! the round around wraping flag
  bool m_wrapRoundAround;
  //! the line points
  MWAWVec2f m_points[2];
  //! the corner size: rectangle oval
  MWAWVec2i m_cornerSize;

  //! the text limits: v4
  int m_textPositions[2];
  //! the text link id
  int m_textId;
  //! the text links: prev/next
  int m_linkIds[2];
  //! the paragraph style
  MWAWParagraph m_paragraph;

  //! a flag to know if a picture is empty or not
  bool m_hasPicture;

  //! the zone entries: picture or text zones
  MWAWEntry m_entries[3];
};

////////////////////////////////////////
//! Internal: a layout in a ReadySetGoParser document
struct Layout {
  //! constructor
  Layout()
    : m_useMasterPage(true)
    , m_shapes()
  {
  }

  //! a flag to know if we use or not the master page
  bool m_useMasterPage;
  //! a map id to shape
  std::vector<Shape> m_shapes;
};

////////////////////////////////////////
//! Internal: the state of a ReadySetGoParser
struct State {
  //! constructor
  State()
    : m_version(1)
    , m_numLayouts(1)
    , m_numGlossary(0)
    , m_numStyles(0)
    , m_hasCustomColors(false)
    , m_layouts()
    , m_colors()
    , m_patterns()
  {
  }
  //! init the color's list
  void initColors();
  //! init the patterns' list
  void initPatterns();
  //! try to retrieve a pattern
  bool get(int id, MWAWGraphicStyle::Pattern &pat)
  {
    if (m_patterns.empty())
      initPatterns();
    if (id < 0 || size_t(id) >= m_patterns.size()) {
      MWAW_DEBUG_MSG(("ReadSetGoParserInternal::get: can not find pattern %d\n", id));
      return false;
    }
    pat=m_patterns[size_t(id)];
    return true;
  }

  //! the file version, used to define the patterns, ...
  int m_version;
  //! the number of layouts: used for v3
  int m_numLayouts;
  //! the number of glossary: used for v4
  int m_numGlossary;
  //! the number of styles: v4
  int m_numStyles;
  //! a flag to know if the document has custom colors: v5
  bool m_hasCustomColors;
  //! the list of shapes
  std::vector<Layout> m_layouts;
  //! the list of colors: v45
  std::vector<MWAWColor> m_colors;
  //! the list of patterns: v3
  std::vector<MWAWGraphicStyle::Pattern> m_patterns;
};

void State::initColors()
{
  if (!m_colors.empty()) return;
  if (m_version<5) {
    MWAW_DEBUG_MSG(("ReadSetGoParserInternal::initColors: unknown version\n"));
    return;
  }
  static uint32_t const values[797]= {
    0x000000, 0xffee00, 0xde4f16, 0xa1006a, 0xc5008e, 0x7d0089, 0x0c0087, 0x0075ad,
    0x00a36e, 0x080d02, 0x30007b, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0xe5dec5, 0xdcd9c7, 0xbaada4, 0xa6968d,
    0x827872, 0xaf9b8f, 0xa9988d, 0x857a74, 0x786e6b, 0x605857, 0x443c3e, 0xcfc9b5,
    0xcac6ba, 0xbeb5b2, 0xb0a6a6, 0x918e92, 0xaa9b98, 0x918d90, 0x75747e, 0x5b5a68,
    0x4d4d5c, 0x323543, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0xffffff, 0xdd0806, 0x008011,
    0x0000d4, 0x02abea, 0xf20884, 0xfcf305, 0xff37b9, 0x9c66fe, 0xff5f0d, 0x00cb00,
    0x4a1209, 0x848484, 0xf9e2a6, 0xfc4b44, 0xe0ad0d, 0xe06d9b, 0x79b4ff, 0x002eb2,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
    0x000000, 0x000000, 0x000000, 0xffffff, 0xfff681, 0xfffb6b, 0xfff400, 0xc5b00a,
    0xa49300, 0x6a6000, 0xfff37a, 0xfff46b, 0xffef5e, 0xffed2f, 0xd9bd11, 0xa28b00,
    0x8e7e00, 0xffee7a, 0xffef6a, 0xffea57, 0xffd900, 0xc2a100, 0xa1850d, 0x73690f,
    0xffed7a, 0xffee76, 0xffe21c, 0xffd200, 0xc39800, 0x9a7f00, 0x847200, 0xffe97a,
    0xffea78, 0xffe01a, 0xffc700, 0xc89900, 0x846d00, 0x524d05, 0xffe072, 0xffd65f,
    0xffc51a, 0xffac00, 0xce8b00, 0x936c00, 0x5a4e09, 0xffd561, 0xffce4c, 0xf2b52b,
    0xcb8300, 0xc57e00, 0x906f00, 0x5b5101, 0xffd788, 0xffca74, 0xffa719, 0xff9800,
    0xcb7d00, 0x956500, 0x7f5800, 0xffe4a3, 0xffd483, 0xf59a4c, 0xed7f2e, 0xb2582a,
    0x7a4702, 0x4d3506, 0xffcb8b, 0xffb073, 0xff9351, 0xe67500, 0xc26200, 0x9b5405,
    0x43290d, 0xffc198, 0xff9e7c, 0xff855d, 0xff6d0d, 0xb85802, 0x723d07, 0x482904,
    0xffbdab, 0xff8e83, 0xff7061, 0xc64230, 0x8e3825, 0x582a23, 0xffc5ba, 0xff8e9e,
    0xec768e, 0xcc304f, 0xa3223a, 0x741019, 0x491010, 0xffb2b1, 0xff98a9, 0xdf5978,
    0xd62654, 0x9a2447, 0x611333, 0x44182e, 0xffc3bd, 0xebaab1, 0xca356a, 0xaa1743,
    0x8d0034, 0x711839, 0x591d1f, 0xffc0c3, 0xe97da2, 0xd55686, 0xaf0755, 0x7c1549,
    0x530b34, 0x431030, 0xffa8b9, 0xe76d9c, 0xdb3381, 0xb81063, 0xa1185b, 0x671544,
    0x440930, 0xffcedf, 0xda74b1, 0xbc1a88, 0x7c0056, 0x650049, 0x400031, 0xff7ac5,
    0xe85dac, 0xd33198, 0xb80081, 0x7d005e, 0x570046, 0x3c1036, 0xffbad4, 0xe46cb2,
    0xd0349e, 0xa6007b, 0x6c0059, 0x4a0040, 0xffa1d8, 0xef60c6, 0xc63ca5, 0xab0089,
    0x800071, 0x68005d, 0x3f003c, 0xffd0ed, 0xde95c5, 0xd067c2, 0x8d008a, 0x7c007c,
    0x66006e, 0x44004d, 0xf9d5ee, 0xe493ff, 0xab35be, 0x630074, 0x4e0058, 0x370047,
    0xe6b9d3, 0xd29bc6, 0x9145a5, 0x640a7f, 0x450f56, 0x3b174e, 0x351d4a, 0xdac4de,
    0xb18ad0, 0x6f2bb1, 0x48128a, 0x360570, 0x2f0a5e, 0x21054d, 0xc2a6cc, 0xab89c4,
    0x8c6fba, 0x3e1d88, 0x250765, 0x220c57, 0x1c0d45, 0xc2c9dc, 0xa8abdd, 0x7c89c4,
    0x06106a, 0x000e4f, 0x0a0c44, 0xc9d5f0, 0xa7b8df, 0x5f74e2, 0x253fb6, 0x001769,
    0x000067, 0x000e44, 0xc4d5e5, 0x94b0dc, 0x7592c7, 0x0000b4, 0x002a7f, 0x00286c,
    0x001c48, 0x8bb0c9, 0x6c97b8, 0x407fb3, 0x0f59a0, 0x004085, 0x002f5d, 0x00294d,
    0xcceae4, 0x7fcde1, 0x56a9c7, 0x006795, 0x004261, 0x002f4e, 0x9ed7d2, 0x72b7c1,
    0x2e8aa1, 0x007291, 0x006281, 0x00445f, 0x00293c, 0xdbffe4, 0xc7f5df, 0x72d6c1,
    0x009696, 0x00787d, 0x006b71, 0x004b55, 0xb7dfc2, 0x89d0b0, 0x6bb9a1, 0x178479,
    0x105f5e, 0x004f50, 0x16373f, 0xccf5c2, 0xc0f0be, 0x74e2a3, 0x17855c, 0x156e54,
    0x12584b, 0xa1e8b1, 0x82ce9d, 0x59aa80, 0x2a9167, 0x1b6d52, 0x245c4a, 0x11413d,
    0xc8eea5, 0xbcf4a2, 0x8dee88, 0x3bb253, 0x309540, 0x2a6f36, 0x2d4a1f, 0xc8f6a6,
    0xbcffa0, 0x92f58f, 0x2bcb4c, 0x318b40, 0x2a6739, 0x274730, 0xd6f094, 0xb7e36f,
    0x8fd44e, 0x5ea223, 0x4a7d22, 0x466c18, 0x3f5c12, 0xe5f491, 0xdaf173, 0xc0e752,
    0x89ca03, 0x76a600, 0x628000, 0x45520a, 0xf6ff74, 0xe8ff6c, 0xcce73d, 0xb0de00,
    0x98bd00, 0x7f9300, 0x515700, 0xf7f666, 0xe6f157, 0xd1e022, 0xc4d600, 0xacae00,
    0x989600, 0x6e6b00, 0xf9fa7e, 0xf4fa62, 0xedfa37, 0xecff0d, 0xbbbf00, 0x9f9900,
    0x747300, 0xfdf378, 0xf6f94f, 0xf1f220, 0xe9f200, 0xbfba00, 0xa19b00, 0x8e8603,
    0xd3c9af, 0xb2a390, 0x968d80, 0x827a6d, 0x5e5854, 0x33312e, 0xd6cab5, 0xb5a69a,
    0xaa9c97, 0x7c7371, 0x544a48, 0x433e44, 0x110f0a, 0xc8c0a6, 0xaca396, 0x89867d,
    0x656764, 0x4b4d4b, 0x393b3b, 0x131317, 0xc8c3bb, 0xbcb7b8, 0x8d8b92, 0x7f7d82,
    0x5d5b60, 0x3a3a48, 0x070b1c, 0xccc7bb, 0xb2acb0, 0x8f8f99, 0x787882, 0x515567,
    0x282e48, 0x0f1020, 0xccb0a6, 0xc3a49d, 0x9a7b7f, 0x78606a, 0x3f2d3d, 0x261a25,
    0x211b22, 0xd4d2c6, 0xbdbbb8, 0x8b8e91, 0x696a6d, 0x474850, 0x2e2e3b, 0x222422,
    0x3a3116, 0x4f463a, 0x594e2d, 0xa08f72, 0xad9d86, 0xc8ba95, 0xddd8ad, 0x544b25,
    0x7f7019, 0xa88f2f, 0xe2d360, 0xeae171, 0xf7ef83, 0xf7ee98, 0x594535, 0x6d4f2b,
    0x835a35, 0xc2a075, 0xd6ba8a, 0xdbc58e, 0xead9a3, 0x4d3522, 0x9f612e, 0xc26d21,
    0xe9a872, 0xe6b480, 0xf0cf99, 0xf3d59e, 0x3b292d, 0x5a3d3b, 0x683e3b, 0xa57f6f,
    0xb19286, 0xccab97, 0xd6c4af, 0x4f3020, 0x914120, 0xc83f31, 0xdb9279, 0xefab8d,
    0xebb298, 0xf5cfb0, 0x4e2b33, 0x602732, 0x702237, 0xc5707d, 0xe89b9e, 0xefb5b1,
    0xf4cab6, 0x3f291b, 0x593331, 0x723c38, 0xc7807e, 0xd59890, 0xe4b1a5, 0xf0cdb9,
    0x421f31, 0x532138, 0x602944, 0xbc6c83, 0xd28390, 0xe9a6a9, 0xecb8b8, 0x48264a,
    0x682f6f, 0x802e83, 0xc26aa9, 0xd38bb4, 0xeba5c2, 0xf7c7cf, 0x3b1d3e, 0x552961,
    0x62296f, 0xa6729a, 0xbc8da6, 0xd5aab9, 0xe7c7ce, 0x3c2056, 0x4e2077, 0x5b1c8b,
    0x9462a8, 0xba86c1, 0xd2a2cd, 0xe2c5e1, 0x211e3c, 0x2d234e, 0x3e376b, 0x8b8099,
    0xad9eb5, 0xb9afc2, 0xccc8d0, 0x18234c, 0x0f2d67, 0x0f337c, 0x657bab, 0x8c9bbc,
    0xa9b1c6, 0xc3cddc, 0x001b39, 0x093151, 0x1f4769, 0x597a96, 0x819cac, 0x9fabb3,
    0xc3cfcd, 0x192b00, 0x183e00, 0x214e27, 0x69866d, 0x95a690, 0xa5b19a, 0xb7c5aa,
    0x1d3027, 0x275046, 0x396c5d, 0x74a185, 0x92b499, 0xc4ddbe, 0xe1f6d7, 0x1b382d,
    0x164f3b, 0x2b6c55, 0x67a684, 0x93c79e, 0xb6d9b2, 0xd1e8c1, 0x394100, 0x4e6200,
    0x567700, 0xa6be64, 0xc8d885, 0xd5de87, 0xdde49a, 0x5a5600, 0x909100, 0xa5ae00,
    0xd0da5b, 0xdde365, 0xe7ec73, 0xe9ef8d, 0xffd170, 0xffcd64, 0xffb615, 0xff9c00,
    0xce8500, 0x7c5a00, 0x463400, 0xffcc8f, 0xffa963, 0xf58532, 0xf77800, 0xc26300,
    0x864e00, 0x573100, 0xffb68c, 0xffa57b, 0xff834f, 0xe66000, 0xc25600, 0x7d3b00,
    0x4d2700, 0xffa7a5, 0xff888f, 0xff6375, 0xde2c1a, 0xc62d37, 0x781915, 0x4f1818,
    0xca61ff, 0xbc39ff, 0x800ebe, 0x590089, 0x4c0074, 0x380058, 0x2f004e, 0xbf6bff,
    0xae3dff, 0x8a20db, 0x470080, 0x3d0070, 0x350063, 0x280052, 0xcf99ff, 0xa84eff,
    0x6c0fc0, 0x5100a0, 0x20005f, 0x1e124c, 0x9c79cc, 0x8e74cc, 0x6c22cf, 0x33008e,
    0x26006f, 0x1d0057, 0x170045, 0xbcd5da, 0x7cacd1, 0x569ac9, 0x2176c1, 0x13528a,
    0x00395d, 0x002540, 0x9ed7ca, 0x72b4b6, 0x0092a1, 0x0d768b, 0x00687c, 0x00475b,
    0x003548, 0xaee9c3, 0x7ee3b7, 0x50c39e, 0x00947c, 0x00786c, 0x00514f, 0x003238,
    0xacdfb7, 0x7cd0a6, 0x5eb996, 0x009176, 0x0a7160, 0x0c5b51, 0x0d3732, 0x86d6b0,
    0x70cda6, 0x29a889, 0x008a6e, 0x006e5c, 0x005749, 0x002e26, 0xa4e9a7, 0x7ce595,
    0x5cd481, 0x34a562, 0x308057, 0x245c44, 0x113823, 0xffff78, 0xfbfe4f, 0xf8fa20,
    0xe9ec00, 0xaba200, 0x858100, 0x595600, 0x4f420c, 0x776900, 0x8a7c3e, 0xbbab7e,
    0xbfb084, 0xcac49b, 0xd6d1a9, 0x452a00, 0x7e5943, 0xa17a62, 0xbe9675, 0xc9a98c,
    0xc9ac91, 0xd3ba9c, 0x4d2b32, 0x6a3e3e, 0x926e66, 0xae8474, 0xbc9580, 0xc8ac99,
    0xd1b79e, 0x432d3a, 0x714355, 0x92606d, 0xc6949a, 0xd5a4a7, 0xdbc2b9, 0xddccc3,
    0x3f184a, 0x632e6f, 0x8c5691, 0xba89ad, 0xc09ab3, 0xceb1c2, 0xd9c8cd, 0x2d1b3e,
    0x442952, 0x694870, 0x90728b, 0xac8d9f, 0xc2aab3, 0xd7c7c6, 0x1e004b, 0x3a2077,
    0x421c8b, 0x735cac, 0x9b84c1, 0xb7a4cd, 0xccc3e1, 0x061c50, 0x1b3365, 0x4d5f85,
    0x75809c, 0x9a9eb8, 0xb0afc5, 0xc3c8d4, 0x00253e, 0x1a475b, 0x517483, 0x899fa3,
    0xa2b4b4, 0xbccac2, 0xd1dcd0, 0x001b31, 0x18394c, 0x375363, 0x77878c, 0x94a0a0,
    0xadb4b0, 0xc8d0c7, 0x283840, 0x4c645c, 0x6a8377, 0x7b8f82, 0xa1ad9f, 0xafb9a8,
    0xc1ccb7, 0x172721, 0x3a504d, 0x576963, 0x909d91, 0xacb1a2, 0xc0ccb5, 0xd6dec8,
    0x343d29, 0x4a5735, 0x5d6c3d, 0x9ea175, 0xb1b07e, 0xc9cba2, 0xd8d9ae, 0x3f4511,
    0x5d6600, 0x7e8746, 0xb1b273, 0xc8cc91, 0xd5d9a2, 0xdddead, 0x494a2a, 0x71743d,
    0xa5a069, 0xc4c07d, 0xcac785, 0xd3d08f, 0xdad7a3
  };

  m_colors.resize(797);
  for (size_t i=0; i < 797; ++i)
    m_colors[i] = values[i];
}

void State::initPatterns()
{
  if (!m_patterns.empty()) return;
  if (m_version<3) {
    MWAW_DEBUG_MSG(("ReadSetGoParserInternal::initPatterns: unknown version\n"));
    return;
  }
  if (m_version==3) {
    static uint16_t const values[] = {
      0xffff, 0xffff, 0xffff, 0xffff,  0xddff, 0x77ff, 0xddff, 0x77ff,  0xdd77, 0xdd77, 0xdd77, 0xdd77,  0xaa55, 0xaa55, 0xaa55, 0xaa55,
      0x55ff, 0x55ff, 0x55ff, 0x55ff,  0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,  0xeedd, 0xbb77, 0xeedd, 0xbb77,  0x8888, 0x8888, 0x8888, 0x8888,
      0xb130, 0x031b, 0xd8c0, 0x0c8d,  0x8010, 0x0220, 0x0108, 0x4004,  0xff88, 0x8888, 0xff88, 0x8888,  0xff80, 0x8080, 0xff08, 0x0808,
      0x0000, 0x0002, 0x0000, 0x0002,  0x8040, 0x2000, 0x0204, 0x0800,  0x8244, 0x3944, 0x8201, 0x0101,  0xf874, 0x2247, 0x8f17, 0x2271,
      0x55a0, 0x4040, 0x550a, 0x0404,  0x2050, 0x8888, 0x8888, 0x0502,  0xbf00, 0xbfbf, 0xb0b0, 0xb0b0,  0x0000, 0x0000, 0x0000, 0x0000,
      0x8000, 0x0800, 0x8000, 0x0800,  0x8800, 0x2200, 0x8800, 0x2200,  0x8822, 0x8822, 0x8822, 0x8822,  0xaa00, 0xaa00, 0xaa00, 0xaa00,
      0x00ff, 0x00ff, 0x00ff, 0x00ff,  0x1122, 0x4488, 0x1122, 0x4488,  0x8040, 0x2000, 0x0204, 0x0800,  0x0102, 0x0408, 0x1020, 0x4080,
      0xaa00, 0x8000, 0x8800, 0x8000,  0xff80, 0x8080, 0x8080, 0x8080,  0x0814, 0x2241, 0x8001, 0x0204,  0x8814, 0x2241, 0x8800, 0xaa00,
      0x40a0, 0x0000, 0x040a, 0x0000,  0x0384, 0x4830, 0x0c02, 0x0101,  0x8080, 0x413e, 0x0808, 0x14e3,  0x1020, 0x54aa, 0xff02, 0x0408,
      0x7789, 0x8f8f, 0x7798, 0xf8f8,  0x0008, 0x142a, 0x552a, 0x1408,  0x0000, 0x0000, 0x0000, 0x0000,
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
    return;
  }
  static uint16_t const values[] = {
    0xffff, 0xffff, 0xffff, 0xffff, 0x7f7f, 0x7f7f, 0x7f7f, 0x7f7f, 0xff, 0xffff, 0xffff, 0xffff, 0xefdf, 0xbf7f, 0xfefd, 0xfbf7,
    0xbfff, 0xffff, 0xfbff, 0xffff, 0x3f3f, 0x3f3f, 0x3f3f, 0x3f3f, 0x0, 0xffff, 0xffff, 0xffff, 0xe7cf, 0x9f3f, 0x7efc, 0xf9f3,
    0xff77, 0xffff, 0xffdd, 0xffff, 0x1f1f, 0x1f1f, 0x1f1f, 0x1f1f, 0x0, 0xff, 0xffff, 0xffff, 0xe3c7, 0x8f1f, 0x3e7c, 0xf8f1,
    0xddff, 0x77ff, 0xddff, 0x77ff, 0xf0f, 0xf0f, 0xf0f, 0xf0f, 0x0, 0x0, 0xffff, 0xffff, 0xe1c3, 0x870f, 0x1e3c, 0x78f0,
    0xdd77, 0xdd77, 0xdd77, 0xdd77, 0x707, 0x707, 0x707, 0x707, 0x0, 0x0, 0xff, 0xffff, 0xc183, 0x70e, 0x1c38, 0x70e0,
    0xaa55, 0xaa55, 0xaa55, 0xaa55, 0x303, 0x303, 0x303, 0x303, 0x0, 0x0, 0x0, 0xffff, 0x306, 0xc18, 0x3060, 0xc081,
    0x8822, 0x8822, 0x8822, 0x8822, 0x8080, 0x8080, 0x8080, 0x8080, 0x0, 0x0, 0x0, 0xff, 0x102, 0x408, 0x1020, 0x4080,
    0x8800, 0x2200, 0x8800, 0x2200, 0x8888, 0x8888, 0x8888, 0x8888, 0xff, 0x0, 0xff, 0x0, 0x1122, 0x4488, 0x1122, 0x4488,
    0x8000, 0x800, 0x8000, 0x800, 0xcccc, 0xcccc, 0xcccc, 0xcccc, 0x0, 0xffff, 0x0, 0xffff, 0x3366, 0xcc99, 0x3366, 0xcc99,
    0x0, 0x2000, 0x0, 0x200, 0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa, 0xff, 0xffff, 0xff, 0xffff, 0x77ee, 0xddbb, 0x77ee, 0xddbb,
    0x0, 0x0, 0x0, 0x0, 0x40a0, 0x0, 0x40a, 0x0, 0xff80, 0x8080, 0xff08, 0x808, 0xff77, 0x3311, 0xff77, 0x3311, 0xb130,
    0x31b, 0xd8c0, 0xc8d, 0x8040, 0x2000, 0x204, 0x800, 0x8010, 0x220, 0x108, 0x4004, 0x4, 0xc3f, 0x1c2c, 0x4400,
    0x0, 0x0, 0x0, 0x0, /* none*/ 0x7789, 0x8f8f, 0x7798, 0xf8f8, 0x8, 0x142a, 0x552a, 0x1408, 0xfffb, 0xf3c0, 0xe3d3, 0xbbff,
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
//! Internal: the subdocument of a ReadySetGoParser
class SubDocument final : public MWAWSubDocument
{
public:
  SubDocument(ReadySetGoParser &pars, MWAWInputStreamPtr const &input, Shape const &shape)
    : MWAWSubDocument(&pars, input, MWAWEntry())
    , m_shape(&shape)
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
    if (&m_shape != &sDoc->m_shape) return true;
    return false;
  }

  //! the parser function
  void parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType type) final;

protected:
  //! the text shape
  Shape const *m_shape;
private:
  SubDocument(SubDocument const &orig) = delete;
  SubDocument &operator=(SubDocument const &orig) = delete;
};

void SubDocument::parse(MWAWListenerPtr &listener, libmwaw::SubDocumentType)
{
  if (!listener || !listener->canWriteText()) {
    MWAW_DEBUG_MSG(("ReadySetGoParserInternal::SubDocument::parse: no listener\n"));
    return;
  }
  auto *parser=dynamic_cast<ReadySetGoParser *>(m_parser);
  if (!parser || !m_shape) {
    MWAW_DEBUG_MSG(("ReadySetGoParserInternal::SubDocument::parse: no parser\n"));
    return;
  }
  long pos = m_input->tell();
  parser->sendText(*m_shape);
  m_input->seek(pos, librevenge::RVNG_SEEK_SET);
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
ReadySetGoParser::ReadySetGoParser(MWAWInputStreamPtr const &input, MWAWRSRCParserPtr const &rsrcParser, MWAWHeader *header)
  : MWAWGraphicParser(input, rsrcParser, header)
  , m_state(new ReadySetGoParserInternal::State)
{
  setAsciiName("main-1");
  getPageSpan().setMargins(0.1);
}

ReadySetGoParser::~ReadySetGoParser()
{
}

////////////////////////////////////////////////////////////
// the parser
////////////////////////////////////////////////////////////
void ReadySetGoParser::parse(librevenge::RVNGDrawingInterface *docInterface)
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
      if (listener) {
        bool firstPage=true;
        for (size_t layout=(version()<3 ? 0 : 2); layout<m_state->m_layouts.size(); ++layout) {
          if (!firstPage)
            listener->insertBreak(MWAWListener::PageBreak);
          for (auto const &shape : m_state->m_layouts[layout].m_shapes)
            send(shape);
          firstPage=false;
        }
      }
      else
        ok=false;
    }
    ascii().reset();
  }
  catch (...) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::parse: exception catched when parsing\n"));
    ok = false;
  }

  resetGraphicListener();
  if (!ok) throw(libmwaw::ParseException());
}

////////////////////////////////////////////////////////////
// create the document
////////////////////////////////////////////////////////////
void ReadySetGoParser::createDocument(librevenge::RVNGDrawingInterface *documentInterface)
{
  if (!documentInterface) return;
  if (getGraphicListener()) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::createDocument: listener already exist\n"));
    return;
  }

  // create the page list
  size_t num=m_state->m_layouts.size();
  int const vers=version();
  std::vector<MWAWPageSpan> pageList;
  bool hasMaster[]= {false, false};
  if (vers<3) {
    MWAWPageSpan ps(getPageSpan());
    ps.setPageSpan(std::max<int>(1,int(num)));
    pageList.push_back(ps);
  }
  else {
    if (num<2) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createDocument: unexpected number of pages\n"));
      throw (libmwaw::ParseException());
    }
    for (size_t i=0; i<2; ++i)
      hasMaster[i]=!m_state->m_layouts[i].m_shapes.empty();
    for (size_t i=2; i<num; ++i) {
      auto const &shape=m_state->m_layouts[i];
      MWAWPageSpan ps(getPageSpan());
      ps.setPageSpan(1);
      if (shape.m_useMasterPage && hasMaster[1-(i%2)])
        ps.setMasterPageName((i%2)==0 ? "MasterPage1" : "MasterPage0");
      pageList.push_back(ps);
    }
  }
  MWAWGraphicListenerPtr listen(new MWAWGraphicListener(*getParserState(), pageList, documentInterface));
  setGraphicListener(listen);
  listen->startDocument();

  for (size_t i=0; i<2; ++i) {
    if (!hasMaster[i]) continue;
    MWAWPageSpan ps(getPageSpan());
    ps.setMasterPageName(librevenge::RVNGString(i==0 ? "MasterPage0" : "MasterPage1"));
    if (!listen->openMasterPage(ps)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createDocument: can not create the master page\n"));
    }
    else {
      for (auto const &shape : m_state->m_layouts[i].m_shapes)
        send(shape);
      listen->closeMasterPage();
    }
  }
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////
bool ReadySetGoParser::createZones()
{
  auto input=getInput();
  int const vers=version();
  if (!input) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: no input\n"));
    return false;
  }
  libmwaw::DebugStream f;
  if (vers<3)
    input->seek(0, librevenge::RVNG_SEEK_SET);
  else if (vers==3) {
    input->seek(2, librevenge::RVNG_SEEK_SET);
    if (!readDocument())
      return false;
  }
  else {
    if (!input->checkPosition(0x64)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: the file seems too short\n"));
      return false;
    }
    input->seek(2, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Entries(ZonePos):";
    std::map<long,int> posToId;
    f << "pos=[";
    for (int i=0; i<(vers==4 ? 2 : 5); ++i) { // Style, ????
      long posi=input->readLong(4);
      f << std::hex << posi << std::dec << ",";
      if (posi<(vers==4 ? 0x100 : 0x300) || !input->checkPosition(posi)) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: the %d th positions seems bad\n", i));
        f << "###";
        ascii().addPos(2);
        ascii().addNote(f.str().c_str());
        return false;
      }
      posToId[posi]=i;
    }
    f << "],";
    for (int i=0; i<(vers==4 ? 45 : 39); ++i) {
      int val=int(input->readLong(2));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    ascii().addPos(2);
    ascii().addNote(f.str().c_str());

    // first zone
    if (!readDocument() || !readPrintInfo() || !readLayoutsList() || !readIdsList() || input->tell()>posToId.begin()->first)
      return false;
    if (input->tell()<posToId.begin()->first) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: find extra data for the beginning zone\n"));
      ascii().addPos(input->tell());
      ascii().addNote("ZonePos:extra###");
    }
    if (vers>=5)
      m_state->initColors();
    for (auto posIdIt=posToId.begin(); posIdIt!=posToId.end();) {
      long pos=posIdIt->first;
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      int id=posIdIt->second;
      ++posIdIt;
      long endPos=posIdIt==posToId.end() ? -1 : posIdIt->first;
      if (endPos>0)
        input->pushLimit(endPos);
      bool ok=true;
      switch (id) {
      case 0:
        ok=readStyles();
        break;
      case 1:
        ok=readGlossary();
        break;
      case 2:
        if (!m_state->m_hasCustomColors) {
          if (!input->checkPosition(pos+4)) {
            MWAW_DEBUG_MSG(("ReadySetGoParser::createZones[color]: can not find the data\n"));
            ok=false;
            break;
          }
          // normally followed by 0
          ascii().addPos(pos);
          ascii().addNote("_");
          break;
        }
        if (!input->checkPosition(pos+120)) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::createZones[color]: can not find the data\n"));
          ok=false;
          break;
        }
        f.str("");
        f << "Entries(Colors):";
        if (m_state->m_colors.size()<60+20) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::createZones[color]: can not use the data\n"));
          f << "###";
          ascii().addPos(pos);
          ascii().addNote(f.str().c_str());
          input->seek(pos+120, librevenge::RVNG_SEEK_SET);
          break;
        }
        f << "colors=[";
        for (size_t i=0; i<20; ++i) {
          uint8_t colors[3];
          for (auto &c : colors) c=uint8_t(input->readULong(2)>>8);
          m_state->m_colors[60+i]=MWAWColor(colors[0],colors[1],colors[2]);
          f << m_state->m_colors[60+i] << ",";
        }
        f << "],";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        break;
      case 3: {
        f.str("");
        f << "Entries(ColorNames):";
        long len=long(input->readLong(4));
        long zEndPos=pos+4+len;
        if (zEndPos<pos+4 || (endPos>0 && zEndPos>endPos) || (endPos<=0 && !input->checkPosition(zEndPos))) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::createZones[color,names]: can not find the data\n"));
          ok=false;
          break;
        }
        if (len==0) {
          ascii().addPos(pos);
          ascii().addNote("_");
        }
        else {
          f << "names=[";
          for (int i=0; i<20; ++i) {
            int cLen=int(input->readULong(1));
            if (input->tell()+cLen>zEndPos) {
              MWAW_DEBUG_MSG(("ReadySetGoParser::createZones[color,names]: can not read a name\n"));
              f << "###";
              break;
            }
            std::string name;
            for (int c=0; c<cLen; ++c) {
              char ch=char(input->readLong(1));
              if (ch)
                name+=ch;
            }
            f << name << ",";
          }
          f << "],";
          ascii().addPos(pos);
          ascii().addNote(f.str().c_str());
          if (input->tell()!=zEndPos)
            ascii().addDelimiter(input->tell(),'|');
          input->seek(zEndPos, librevenge::RVNG_SEEK_SET);
        }
        break;
      }
      case 4:
        ok=readFontsBlock();
        break;
      default:
        MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: find unexpected zone=%d\n", id));
        ok=false;
        break;
      }
      if (endPos>0)
        input->popLimit();
      if (ok)
        continue;
      ascii().addPos(pos);
      ascii().addNote("Entries(Bad):###");
      if (endPos==-1) // no way to continue
        return false;
    }
    for (auto &layout : m_state->m_layouts) {
      while (!input->isEnd()) {
        bool last;
        if (!readShapeV3(layout, last))
          return false;
        if (last)
          break;
      }
    }
    updateTextBoxLinks();
    return true;
  }


  if (!readPrintInfo())
    return false;

  if (vers>=3) {
    if (!readLayoutsList() || !readIdsList())
      return false;
    // always empty
    long pos=input->tell();
    long len=long(input->readLong(4));
    f.str("");
    f << "Entries(Zone0):";
    if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: can not find a initial zone0\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (len==0) {
      ascii().addPos(pos);
      ascii().addNote("_");
    }
    else {
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
    }
    for (auto &layout : m_state->m_layouts) {
      while (!input->isEnd()) {
        bool last;
        if (!readShapeV3(layout, last))
          return false;
        if (last)
          break;
      }
    }
    updateTextBoxLinks();
    if (!input->isEnd()) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: find extra data\n"));
      ascii().addPos(input->tell());
      ascii().addNote("Entries(End):###:");
      if (m_state->m_layouts.size()<=2) // only master page
        return false;
      for (auto const &layout : m_state->m_layouts) {
        if (!layout.m_shapes.empty())
          return true;
      }
      return false; // no shape
    }
    return true;
  }

  long pos=input->tell();
  if (!input->checkPosition(pos+2)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: can not find the shapes\n"));
    return false;
  }

  if (vers==1) {
    f.str("");
    f << "Entries(Zones):";
    int n=int(input->readULong(2));
    f << "N=" << n << ",";
    if (n<=0) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: can not find any shape\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    m_state->m_layouts.resize(1);
    for (int sh=0; sh<n; ++sh) {
      pos=input->tell();
      if (!readShapeV1() || m_state->m_layouts[0].m_shapes.empty() ||
          (m_state->m_layouts[0].m_shapes.back().m_type==ReadySetGoParserInternal::Shape::T_Empty && sh+1!=n)) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: can not read a shape\n"));
        ascii().addPos(pos);
        ascii().addNote("Entries(BadShape):###");
        return false;
      }
    }
  }
  else {
    f.str("");
    f << "Entries(Pages):";
    int numPages=int(input->readULong(2));
    if (!input->checkPosition(pos+2+2*numPages)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: can not read the numbers of shapes by page\n"));
      f << "##n=" << numPages << ",";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    std::vector<int> numShapesByPage;
    f << "N=[";
    for (int i=0; i<numPages; ++i) {
      numShapesByPage.push_back(int(input->readULong(2)));
      f << numShapesByPage.back();
    }
    f << "],";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    for (size_t p=0; p<size_t(numPages); ++p) {
      m_state->m_layouts.push_back(ReadySetGoParserInternal::Layout());
      for (int sh=0; sh<numShapesByPage[p]; ++sh) {
        if (!readShapeV2(m_state->m_layouts.back()))
          return false;
      }
    }
  }
  if (!input->isEnd()) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::createZones: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Entries(Extra):###");
    if (m_state->m_layouts.empty())
      return false;
  }
  return true;
}


////////////////////////////////////////////////////////////
// read the header
////////////////////////////////////////////////////////////
bool ReadySetGoParser::checkHeader(MWAWHeader *header, bool strict)
{
  *m_state = ReadySetGoParserInternal::State();
  MWAWInputStreamPtr input = getInput();
  if (!input || !input->hasDataFork() || !input->checkPosition(126))
    return false;

  libmwaw::DebugStream f;
  f << "FileHeader:";
  input->seek(0, librevenge::RVNG_SEEK_SET);
  int val=int(input->readULong(2));
  int vers=1;
  if (val==0x1e) {
    if (input->readULong(2) || input->readULong(2)!=0x86)
      return false;
    vers=3;
  }
  else if (val==0x190)
    vers=4;
  else if (val==0x138b)// 4.5
    vers=5;
  else if (val!=0x78)
    return false;
  ascii().addPos(0);
  ascii().addNote(f.str().c_str());

  // we need to retrieve the version

  if (vers<3) {
    input->seek(2+120, librevenge::RVNG_SEEK_SET);
    int n=int(input->readULong(2));
    if (n<=0)
      return false;

    //
    // first test for version 2, the more structured
    //
    bool ok=true;
    vers=2;
    int nShapes=0;
    for (int p=0; p<n; ++p) {
      long pos=input->tell();
      if (!input->checkPosition(pos+2)) {
        ok=false;
        break;
      }
      val=int(input->readLong(2));
      if (val<0) {
        ok=false;
        break;
      }
      nShapes+=val;
    }
    for (int s=0; s<nShapes; ++s) {
      long pos=input->tell();
      if (!ok || !input->checkPosition(pos+4)) {
        ok=false;
        break;
      }
      int type=int(input->readULong(2));
      if (type<0 || type>6) {
        ok=false;
        break;
      }
      input->seek(2, librevenge::RVNG_SEEK_CUR);
      for (int i=0; i<2; ++i) {
        pos=input->tell();
        int len=int(input->readULong(2));
        if ((i==0 && len!=0x1c) || !input->checkPosition(pos+2+len)) {
          ok=false;
          break;
        }
        input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
      }
      if (!ok)
        break;
      if (type==3 && !input->isEnd()) {
        // we must check if there is a picture
        pos=input->tell();
        int len=int(input->readULong(2));
        if (len<10)
          input->seek(pos, librevenge::RVNG_SEEK_SET);
        else {
          if (!input->checkPosition(pos+2+len)) {
            ok=false;
            break;
          }
          input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
        }
      }
      if (type!=4)
        continue;
      for (int st=0; st<2; ++st) { // zone1=[text, style], zone2=[para]
        pos=input->tell();
        int len=int(input->readULong(2));
        if (!input->checkPosition(pos+2+len)) {
          ok=false;
          break;
        }
        input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
      }
    }
    if (ok && nShapes<=10 && !input->isEnd())
      ok=false;

    if (!ok) {
      // test for version 1
      ok=true;
      vers=1;
      input->seek(2+120+2, librevenge::RVNG_SEEK_SET);
      for (int i=0; i<std::min(10,n); ++i) {
        long pos=input->tell();
        if (!input->checkPosition(pos+26)) {
          ok=false;
          break;
        }
        int type=int(input->readLong(2));
        if (type<0 || type>5 || type==2) {
          ok=false;
          break;
        }
        int const expectedSize[]= {26, 74, 0, 30, 28, 28};
        if (expectedSize[type]<=0 || !input->checkPosition(pos+expectedSize[type])) {
          ok=false;
          break;
        }
        input->seek(pos+expectedSize[type], librevenge::RVNG_SEEK_SET);
        if (type==0 && i+1!=n) {
          ok=false;
          break;
        }
        if (type==5 && !input->isEnd()) {
          // we must check if there is a picture
          pos=input->tell();
          int len=int(input->readULong(2));
          if (len<10)
            input->seek(pos, librevenge::RVNG_SEEK_SET);
          else {
            if (!input->checkPosition(pos+2+len)) {
              ok=false;
              break;
            }
            input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
          }
        }
        if (type!=1)
          continue;
        for (int st=0; st<2; ++st) {
          pos=input->tell();
          int len=int(input->readULong(2));
          if (!input->checkPosition(pos+2+len)) {
            ok=false;
            break;
          }
          input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
        }
      }
      if (ok && n<=10 && !input->isEnd())
        ok=false;
    }
    if (!ok)
      return false;
  }
  else if (vers==3) {
    if (strict) {
      input->seek(2, librevenge::RVNG_SEEK_SET);
      for (int i=0; i<3; ++i) {
        long pos=input->tell();
        long len=long(input->readLong(4));
        if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len))
          return false;
        if (len==0 && i<2)
          return false;
        input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
      }
    }
  }
  else if (vers>3) {
    if (strict) {
      input->seek(2, librevenge::RVNG_SEEK_SET);
      // first check the initial list of pointers
      for (int i=0; i<(vers==4 ? 2 : 5); ++i) {
        long pos=long(input->readLong(4));
        if (pos<(vers==4 ? 0x100 : 0x300) || !input->checkPosition(pos))
          return false;
      }
      // now check the fourst first zone
      input->seek(0x64, librevenge::RVNG_SEEK_SET);
      for (int step=0; step<4; ++step) {
        long pos=input->tell();
        long len=long(input->readLong(4));
        if (pos+4+len<pos+len || !input->checkPosition(pos+4+len))
          return false;
        if (step==0 && len!=(vers==4 ? 0xcc : 0x188))
          return false;
        input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
      }
    }
  }
  m_state->m_version=vers;
  setVersion(vers);
  if (header)
    header->reset(MWAWDocument::MWAW_T_READYSETGO, vers, MWAWDocument::MWAW_K_DRAW);

  return true;
}

////////////////////////////////////////////////////////////
// try to read the main zone
////////////////////////////////////////////////////////////
bool ReadySetGoParser::readDocument()
{
  int const vers=version();
  if (vers<3) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readDocument: unexpected version\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readDocument: can not read the zone length\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Document):";
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  if (len<0 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readDocument: can not read the zone length\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  long const expectedLength=vers==3 ? 0x86 : vers==4 ? 0xcc : 0x188;
  if (len!=expectedLength) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readDocument: unexpected zone length\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  int val=int(input->readLong(2));
  if (val!=1)
    f << "first[page]=" << val << ",";
  m_state->m_numLayouts=int(input->readLong(2));
  if (m_state->m_numLayouts)
    f << "num[layout]=" << m_state->m_numLayouts << ",";
  val=int(input->readLong(2));
  if (val+1!=m_state->m_numLayouts)
    f << "act[layout]=" << val << ",";
  f << "IDS=[";
  for (int i=0; i<3; ++i)
    f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  val=int(input->readLong(2));
  if (val) {
    m_state->m_numGlossary=val;
    f << "num[glossary]=" << val << ",";
  }
  f << "ID1=" << std::hex << input->readULong(4) << std::dec << ",";
  if (vers>3) {
    m_state->m_numStyles=int(input->readLong(2));
    if (m_state->m_numStyles)
      f << "num[styles]=" << m_state->m_numStyles << ",";
    f << "ID2=" << std::hex << input->readULong(4) << std::dec << ",";
  }
  f << "margins=[";
  for (int i=0; i<4; ++i) f << float(input->readLong(4))/65536 << ",";
  f << "],";
  f << "unkns=[";
  for (int i=0; i<2; ++i) f << float(input->readLong(4))/65536 << ",";
  f << "],";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    int const expected[]= {4,2};
    if (val!=expected[i])
      f << "f" << i+3 << "=" << val << ",";
  }
  if (vers>3) {
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(1));
      if (val==-1)
        f << "fl" << i << ",";
      else if (val)
        f << "fl" << i << "=" << val << ",";
    }
  }
  f << "ID3=" << std::hex << input->readULong(4) << std::dec << ",";
  val=int(input->readLong(2));
  if (val)
    f << "f4=" << val << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "Document-A:";
  if (vers>3) {
    int cLen=int(input->readULong(1));
    if (cLen>61) {
      f << "###";
      MWAW_DEBUG_MSG(("ReadySetGoParser::readDocument: unexpected file name len\n"));
      cLen=0;
    }
    std::string name;
    for (int i=0; i<cLen; ++i) {
      char c=char(input->readLong(1));
      if (c==0)
        break;
      name+=c;
    }
    f << "file=" << name << ",";
    input->seek(pos+62, librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');

    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    if (vers>=5) {
      input->seek(pos+134, librevenge::RVNG_SEEK_SET);
      pos=input->tell();
      f.str("");
      f << "Document-B:";
      f << "IDS=[";
      for (int i=0; i<4; ++i)
        f << std::hex << input->readULong(4) << std::dec << ",";
      f << "],";
      ascii().addDelimiter(input->tell(),'|');
      input->seek(pos+24, librevenge::RVNG_SEEK_SET);
      ascii().addDelimiter(input->tell(),'|');
      val=int(input->readULong(4));
      if (val) {
        m_state->m_hasCustomColors=true;
        f << "color[IDS]=" << std::hex << val << std::dec << ",";
      }
      val=int(input->readULong(4));
      if (val)
        f << "color[IDS,name]=" << std::hex << val << std::dec << ",";
      ascii().addDelimiter(input->tell(),'|');
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
    }

    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  std::string name;
  for (int i=0; i<62; ++i) { // checkme, maybe a string of size 32, followed by...
    char c=char(input->readLong(1));
    if (c==0)
      break;
    name+=c;
  }
  f << "file=" << name << ",";
  input->seek(pos+62, librevenge::RVNG_SEEK_SET);
  int dim[2];
  for (auto &d : dim) d=int(input->readLong(2));
  f << "dim=" << MWAWVec2i(dim[0],dim[1]) << ",";
  val=int(input->readLong(2));
  if (val!=1) f << "unit=" << val << ","; // inch, centimeters, pica
  for (int i=0; i<4; ++i) {
    val=int(input->readLong(1));
    if (val==-1)
      continue;
    if (i==0)
      f << "hide[ruler]";
    else if (i==1)
      f << "hide[grid]";
    else
      f << "fl" << i;
    if (val!=0)
      f << "=" << val << ",";
    else
      f << ",";
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

bool ReadySetGoParser::readIdsList()
{
  if (version()<3) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readIdsList: unexpected version\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readIdsList: can not read the zone length\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(IDLists):";
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  if (len<0 || endPos<pos+4 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readIdsList: can not read the zone length\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  if (len==0) {
    ascii().addPos(pos);
    ascii().addNote("_");
    return true;
  }
  if (len%4) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readIdsList: can not determine the number of IDS\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  f << "ids=[";
  for (int i=0; i<len/4; ++i) {
    auto val=input->readULong(4);
    if (val)
      f << std::hex << val << std::dec << ",";
    else
      f << "_,";
  }
  f << "],";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool ReadySetGoParser::readLayoutsList()
{
  int const vers=version();
  if (vers<3) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readLayoutsList: unexpected version\n"));
    return false;
  }
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+4)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readLayoutsList: can not read the zone length\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(Layout):";
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  int const dataSize=vers==3 ? 10 : vers==4 ? 14 : 136;
  if (len<0 || len/dataSize<m_state->m_numLayouts || endPos<pos+4 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readLayoutsList: can not read the zone length\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int l=0; l<m_state->m_numLayouts; ++l) { // LR,1:R,2:L,...
    m_state->m_layouts.push_back(ReadySetGoParserInternal::Layout());
    auto &layout=m_state->m_layouts.back();
    pos=input->tell();
    f.str("");
    f << "Layout-" << l << ":";
    for (int i=0; i<2; ++i) { // f0=0 or 8(rare)
      int val=int(input->readLong(vers==3 ? 2 : 4));
      if (val)
        f << "f" << i << "=" << val << ",";
    }
    int val=int(input->readULong(4));
    if (val)
      f << "ID=" << std::hex << val << std::dec << ","; // last shape id in layout?
    val=int(input->readULong(2)); // 1|3
    if ((val&1)==0) {
      f << "use[master]=false,";
      layout.m_useMasterPage=false;
    }
    val &= 0xfffe;
    if (val)
      f << "fl=" << std::hex << val << std::dec << ",";
    if (input->tell()!=pos+dataSize)
      ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+dataSize, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readLayoutsList: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Layout-extra:###");
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
// try to read the shapes zone
////////////////////////////////////////////////////////////
bool ReadySetGoParser::readShapeV1()
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  long pos=input->tell();
  if (!input->checkPosition(pos+26)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: can not read a shape\n"));
    return false;
  }
  if (m_state->m_layouts.empty()) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: oops, must create a new layout\n"));
    m_state->m_layouts.resize(1);
  }
  int type=int(input->readULong(2));

  static char const *wh[]= {"EndZone", "Text", nullptr, "Frame", "Solid", "Picture"};
  if (type<0 || type==2 || type>5) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: unknown type\n"));
    return false;
  }
  libmwaw::DebugStream f;
  if (type>=0 && type<=5 && wh[type])
    f << "Entries(" << wh[type] << "):";
  else
    f << "Entries(Zone" << type << "):";
  int const expectedSize[]= {26, 74, 0, 30, 28, 28};
  if (expectedSize[type]<=0 || !input->checkPosition(pos+expectedSize[type])) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: the zone seems too short for a shape\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  static ReadySetGoParserInternal::Shape::Type const shapeTypes[]= {
    ReadySetGoParserInternal::Shape::T_Empty,
    ReadySetGoParserInternal::Shape::T_Text,
    ReadySetGoParserInternal::Shape::T_Unknown,
    ReadySetGoParserInternal::Shape::T_Rectangle, // frame
    ReadySetGoParserInternal::Shape::T_Rectangle, // solid
    ReadySetGoParserInternal::Shape::T_Picture
  };
  ReadySetGoParserInternal::Shape shape(shapeTypes[type]);
  float dim[4];
  for (auto &d : dim) d=float(input->readLong(2));
  shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[0]+dim[2],dim[1]+dim[3]));
  f << "box=" << shape.m_box << ",";
  if (type!=0) {
    for (auto &d : dim) {
      d=float(input->readLong(2));
      d+=float(input->readLong(2))/10000;
    }
    f << "box[inch]=" << MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[0]+dim[2],dim[1]+dim[3])) << ",";
    int val;
    if (type==3) {
      val=int(input->readLong(2));
      if (val<0 || val>100) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: the frame size seems bad\n"));
        f << "###";
      }
      else
        shape.m_style.m_lineWidth=float(val);
      if (val!=1)
        f << "frame[size]=" << val << ",";
    }
    if (type==5) {
      val=int(input->readLong(2));
      shape.m_hasPicture=val!=0;
      if (val==0)
        f <<"noPict,";
      else if (val!=1)
        f << "###pict=" << val << ",";
    }
    else if (type!=1) {
      val=int(input->readLong(2));
      if (val<0 || val>4) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: the color id seems bad\n"));
        f << "###col=" << val << ",";
      }
      else {
        uint8_t grey=uint8_t(val==0 ? 255 : 32*val);
        if (type==3)
          shape.m_style.m_lineColor=MWAWColor(grey,grey,grey);
        else
          shape.m_style.setSurfaceColor(MWAWColor(grey,grey,grey));
        f << "color="  << val << ","; // 0: none, 1: black, 2: grey1, .., 4: light grey
      }
    }
    else {
      shape.m_paragraph.m_marginsUnit=librevenge::RVNG_INCH;
      for (int i=0; i<2; ++i) { // 0
        val=int(input->readLong(2));
        shape.m_paragraph.m_margins[1-i]=float(val)+float(input->readULong(2))/10000;
        if (*shape.m_paragraph.m_margins[1-i]>0) f << (i==0 ? "para" : "left") << "[indent]=" << *shape.m_paragraph.m_margins[1-i] << ",";
      }
      shape.m_paragraph.m_margins[0]=*shape.m_paragraph.m_margins[0]-*shape.m_paragraph.m_margins[1];
      std::string extra;
      readTabulationsV1(*shape.m_paragraph.m_tabs, extra);
      f << extra;
    }
  }
  if (input->tell()!=pos+expectedSize[type]) {
    ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+expectedSize[type], librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (type!=1 && (type!=5 || !shape.m_hasPicture)) {
    m_state->m_layouts[0].m_shapes.push_back(shape);
    return true;
  }
  // before size zone0+zone1
  for (int st=0; st<2; ++st) { // zone1=[text, style], zone2=[para]
    pos=input->tell();
    f.str("");
    if (type==1)
      f << "Text-" << (st==0 ? "char" : "para") << ":";
    else
      f << "Picture:";
    int len=int(input->readULong(2));
    if (!input->checkPosition(pos+2+len)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV1: the zone seems too short for a text sub zone\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    shape.m_entries[st].setBegin(pos+2);
    shape.m_entries[st].setLength(len);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+2+len+(len%2), librevenge::RVNG_SEEK_SET);
    if (type==5)
      break;
  }

  m_state->m_layouts[0].m_shapes.push_back(shape);
  return true;
}

bool ReadySetGoParser::readShapeV2(ReadySetGoParserInternal::Layout &layout)
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;

  long pos=input->tell();
  if (!input->checkPosition(pos+8)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: can not read a shape\n"));
    return false;
  }
  int type=int(input->readULong(2));
  int id=int(input->readULong(2));
  libmwaw::DebugStream f;
  if (type<0 || type>6) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: find bad type=%d\n", type));
    f << "Entries(Zone" << type << ")[S" << id << "]:###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  char const *wh[]= { nullptr, "Solid", "Frame", "Picture", "Text", nullptr, nullptr };
  std::string what;
  if (wh[type])
    what=wh[type];
  else {
    std::stringstream s;
    s << "Zone" << type;
    what=s.str();
  }
  f << "Entries(" << what << ")[S" << id << "]:";
  static ReadySetGoParserInternal::Shape::Type const shapeTypes[]= {
    ReadySetGoParserInternal::Shape::T_Unknown,
    ReadySetGoParserInternal::Shape::T_Rectangle, // solid
    ReadySetGoParserInternal::Shape::T_Rectangle, // frame
    ReadySetGoParserInternal::Shape::T_Picture,
    ReadySetGoParserInternal::Shape::T_Text,
    ReadySetGoParserInternal::Shape::T_Unknown,
    ReadySetGoParserInternal::Shape::T_Unknown
  };
  ReadySetGoParserInternal::Shape shape(shapeTypes[type]);
  int len=int(input->readULong(2));
  if (len<0x1c || !input->checkPosition(pos+6+len)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: find unexpected size for generic data block\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  int val=int(input->readLong(2));
  if (val!=type) f << "##type2=" << val << ",";
  for (int i=0; i<2; ++i) {
    val=int(input->readLong(2));
    if (val!=id) f << "id" << i+1 << "=" << val << ",";
  }
  val=int(input->readLong(2));
  if (val!=1) f << "f0=" << val << ",";
  float dim[4];
  for (auto &d : dim) {
    d=72*float(input->readLong(2));
    d+=72*float(input->readLong(2))/10000;
  }
  shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[0]+dim[2],dim[1]+dim[3]));
  f << "box=" << shape.m_box << ",";
  f << "ID=" << std::hex << input->readULong(4) << std::dec << ",";
  if (input->tell()!=pos+6+len) {
    ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+6+len, librevenge::RVNG_SEEK_SET);
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << what << "-data:S" << id << ",";
  len=int(input->readULong(2));
  long endPos=pos+2+len;
  if (len<0 || !input->checkPosition(endPos)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: find unexpected size for shape data block\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  switch (type) {
  case 1: // solid
  case 2: { // frame
    if (len!=2+2*type) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2[%d]: find unexpected size for shape data block\n", type));
      f << "###";
      break;
    }
    val=int(input->readLong(2));
    if (val<0 || val>4) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: the color id seems bad\n"));
      f << "###col=" << val << ",";
    }
    else {
      uint8_t grey=uint8_t(val==0 ? 255 : 32*val);
      if (type==2)
        shape.m_style.m_lineColor=MWAWColor(grey,grey,grey);
      else
        shape.m_style.setSurfaceColor(MWAWColor(grey,grey,grey));
      f << "color="  << val << ","; // 0: none, 1: black, 2: grey1, .., 4: light grey
    }
    if (type==2) {
      val=int(input->readLong(2));
      if (val<0 || val>100) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: the frame size seems bad\n"));
        f << "###";
      }
      else
        shape.m_style.m_lineWidth=float(val);
      if (val!=1)
        f << "frame[size]=" << val << ",";
    }
    int subType=int(input->readLong(2));
    switch (subType) {
    case 1: // rectangle
      break;
    case 2:
      shape.m_type=ReadySetGoParserInternal::Shape::T_RectOval;
      f << "rectOval,";
      break;
    case 3:
      shape.m_type=ReadySetGoParserInternal::Shape::T_Oval;
      f << "oval,";
      break;
    default:
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: unknown rectangle type\n"));
      f << "###type=" << subType << ",";
      break;
    }
    break;
  }
  case 3: {
    if (len!=16) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2[pict]: find unexpected size for shape data block\n"));
      f << "###";
      break;
    }
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(2));
      if (val!=100)
        f << "scale" << (i==0 ? "X" : "Y") << "=" << val << "%,";
    }
    int iDim[2]; // some multiples of 6, link to decal?
    for (auto &d : iDim) d=int(input->readLong(2));
    if (iDim[0] || iDim[1])
      f << "unkn=" << MWAWVec2i(iDim[0],iDim[1]) << ",";
    val=int(input->readULong(4));
    if (val) {
      shape.m_hasPicture=true;
      f << "ID=" << std::hex << val << std::dec << ",";
    }
    for (int i=0; i<2; ++i) { // 0
      val=int(input->readULong(2));
      if (val) f << "f" << i << "=" << val << ",";
    }
    break;
  }
  case 4: {
    if (len!=0x9a) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2[text]: find unexpected size for shape data block\n"));
      f << "###";
      break;
    }
    shape.m_paragraph.m_marginsUnit=librevenge::RVNG_INCH;
    for (int i=0; i<2; ++i) { // 0
      val=int(input->readLong(2));
      shape.m_paragraph.m_margins[1-i]=float(val)+float(input->readULong(2))/10000;
      if (*shape.m_paragraph.m_margins[1-i]>0) f << (i==0 ? "para" : "left") << "[indent]=" << *shape.m_paragraph.m_margins[i] << ",";
    }
    shape.m_paragraph.m_margins[0]=*shape.m_paragraph.m_margins[0]-*shape.m_paragraph.m_margins[1];
    std::string extra;
    readTabulationsV1(*shape.m_paragraph.m_tabs, extra);
    f << extra;
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << what << "-data1:";
    input->seek(pos+66, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << what << "-data2:";
    // pos+16: maybe alignement
    break;
  }
  default:
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: reading data of type=%d is not implemented\n", type));
    f << "###";
    break;
  }
  if (input->tell()!=pos && input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  if (type==3 && shape.m_hasPicture) {
    pos=input->tell();
    len=int(input->readULong(2));
    f.str("");
    f << "Picture:";
    if (!input->checkPosition(pos+2+len)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: find unexpected size for picture\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    shape.m_entries[0].setBegin(pos+2);
    shape.m_entries[0].setLength(len);
    input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (type!=4) {
    layout.m_shapes.push_back(shape);
    return true;
  }
  for (int st=0; st<2; ++st) { // zone1=[text, style], zone2=[para]
    pos=input->tell();
    f.str("");
    f << "Text-" << (st==0 ? "char" : "para") << ":";
    len=int(input->readULong(2));
    if (!input->checkPosition(pos+2+len)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV2: find unexpected size for text sub zone=%d\n", st));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    shape.m_entries[st].setBegin(pos+2);
    shape.m_entries[st].setLength(len);
    input->seek(pos+2+len, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  layout.m_shapes.push_back(shape);
  return true;
}

bool ReadySetGoParser::readShapeV3(ReadySetGoParserInternal::Layout &layout, bool &last)
{
  last=false;
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;

  int const vers=version();
  long pos=input->tell();
  if (!input->checkPosition(pos+2)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3: can not read a shape\n"));
    return false;
  }

  int type=int(input->readLong(2));
  if (type==-1) {
    ascii().addPos(pos);
    ascii().addNote("Layout-end:");
    last=true;
    return true;
  }
  if (type<0 || type>6) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3: the type seems bad\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  libmwaw::DebugStream f;

  long len=long(input->readLong(4));
  int const decal=vers<=3 ? 0 : 4;
  if (len<32+decal || pos+6+len<pos+6 || !input->checkPosition(pos+6+len)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3: can not find a shape length\n"));
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f << "Entries(Shape" << type << "):";
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    return false;
  }
  static ReadySetGoParserInternal::Shape::Type const shapeTypes[]= {
    ReadySetGoParserInternal::Shape::T_Rectangle,
    ReadySetGoParserInternal::Shape::T_RectOval, // + oval size
    ReadySetGoParserInternal::Shape::T_Oval,
    ReadySetGoParserInternal::Shape::T_Picture,
    ReadySetGoParserInternal::Shape::T_Text,
    ReadySetGoParserInternal::Shape::T_Line,
    ReadySetGoParserInternal::Shape::T_Line
  };
  static char const *what[]= { "Rectangle", "RectOval", "Oval", "Picture", "Text", "Line" /* hv*/, "Line" /* not axis aligned*/};
  f << "Entries(" << what[type] << "):";
  ReadySetGoParserInternal::Shape shape(shapeTypes[type]);
  f << "IDS=["; // next, prev
  for (int i=0; i<2; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
  f << "],";
  float dim[4];
  for (auto &d : dim) d=72*float(input->readLong(4))/65536;
  shape.m_box=MWAWBox2f(MWAWVec2f(dim[0],dim[1]), MWAWVec2f(dim[0]+dim[2],dim[1]+dim[3]));
  f << "box=" << shape.m_box << ",";
  int val;
  if (vers>3) {
    val=int(input->readULong(4));
    if (val!=0x1555)
      f << "dist[text,repel]=" << float(val)/65536 << ",";;
  }
  val=int(input->readLong(1));
  if (val!=type)
    f << "##type1=" << val << ",";
  val=int(input->readLong(1));
  if (val==-1)
    f << "selected,";
  else if (val)
    f << "#selected=" << val << ",";
  bool hasPicture=false, hasTabs=false;
  val=int(input->readLong(1));
  if (val==-1) {
    if (vers<4)
      shape.m_wrapRoundAround=true;
    f << "run[around],";
  }
  else if (val)
    f << "run[around]=" << val << ",";
  val=int(input->readULong(1)); // only v4?
  if (val&1)
    f << "locked,";
  if (val&2)
    f << "print[no],";
  if (val&4) {
    f << "run[around],";
    if (vers>=4)
      shape.m_wrapRoundAround=true;
  }
  val &=0xf8;
  if (val)
    f << "fl=" << std::hex << val << std::dec << ",";
  switch (type) {
  case 0:
  case 1:
  case 2:
  case 5:
  case 6: {
    if (len!=(type==1 ? 36 : type==6 ? 40 : 32)+decal+(vers>=5 ? 4 : 0)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[%d]: unexpected data length\n", type));
      f << "###";
      break;
    }
    auto &style=shape.m_style;
    val=int(input->readLong(1));
    int const extraVal=vers<5 ? 0 : 3;
    if (val>=0 && val<=5+extraVal) {
      float const w[]= {0.125f, 0.25f, 0.5f, 0.75f, 1, 2, 4, 6, 8};
      style.m_lineWidth=w[val+(3-extraVal)];
    }
    else if (val>=6+extraVal && val<=10+extraVal) {
      float const w[]= {1, 2, 4, 6, 8};
      style.m_lineWidth=w[val-6-extraVal];
      f << "dash,";
      style.m_lineDashWidth= {10,10};
    }
    else if (val>=11+extraVal && val<=13+extraVal) {
      // changeme: double line 2-1-1, 1-1-2, 1-1-1
      style.m_lineWidth=val==13+extraVal ? 3 : 4;
      f << "double[line],";
    }
    else {
      style.m_lineWidth=1;
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[%d]: find unknown line style\n", type));
      f << "###line[style]=" << val << ",";
    }
    if (style.m_lineWidth<1 || style.m_lineWidth>1)
      f << "line[width]=" << style.m_lineWidth << ",";

    int patIds[2];
    int const nonePatId=vers==3 ? 39 : 48;
    for (auto &p : patIds) p=int(input->readULong(1));
    input->seek(1, librevenge::RVNG_SEEK_CUR); // junk?

    int colIds[2]= {-1,-1};
    MWAWColor colors[2]= {MWAWColor::white(), MWAWColor::black()};
    if (vers>=5) {
      for (int i=0; i<2; ++i) {
        colIds[i]=int(input->readULong(2));
        int const expected[]= {7, 60};
        if (colIds[i]==expected[i]) continue;
        f << "col[" << (i==0 ? "surf" : "line") << "]=";
        if (colIds[i]>0 && size_t(colIds[i])<m_state->m_colors.size()) {
          colors[i]=m_state->m_colors[size_t(colIds[i])];
          f << colors[i];
        }
        else {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3: unknown color id=%d\n", colIds[i]));
          f << "###" << colIds[i];
          colIds[i]=-1;
        }
        f << ",";
      }
    }
    if (type==1) {
      int corner[2];
      for (auto &d : corner) d=int(input->readLong(2));
      shape.m_cornerSize=MWAWVec2i(corner[0], corner[1]);
      f << "corner=" << shape.m_cornerSize << ",";
    }
    if (type==5) { // we must retrieve the line points
      auto const &box=shape.m_box;
      if (box.size()[0]>box.size()[1]) {
        auto y=(box[0][1]+box[1][1])/2;
        shape.m_points[0]=MWAWVec2f(box[0][0],y);
        shape.m_points[1]=MWAWVec2f(box[1][0],y);
      }
      else {
        auto x=(box[0][0]+box[1][0])/2;
        shape.m_points[0]=MWAWVec2f(x,box[0][1]);
        shape.m_points[1]=MWAWVec2f(x,box[1][1]);
      }
    }
    if (type==6) {
      float iDim[4];
      for (auto &d: iDim) d=float(input->readLong(2));
      shape.m_points[0]=MWAWVec2f(iDim[1],iDim[0]);
      shape.m_points[1]=MWAWVec2f(iDim[3],iDim[2]);
      f << "pos=" << shape.m_points[0] << "<->" << shape.m_points[1] << ",";
    }

    // time to set the patterns/colors
    if (patIds[0]!=nonePatId && (type!=5 && type!=6)) {
      MWAWGraphicStyle::Pattern pat;
      MWAWColor color;
      if (!m_state->get(patIds[0]-1, pat))
        f << "##surface[color]=" << patIds[0] << ",";
      else {
        if (colIds[0]>=0)
          pat.m_colors[0]=colors[0];
        if (pat.getUniqueColor(color)) {
          style.setSurfaceColor(color);
          f << "surface[color]=" << color << ",";
        }
        else {
          style.setPattern(pat);
          f << "surface[pat]=" << pat << ",";
        }
      }
    }
    if (patIds[1]==nonePatId) // transparent
      style.m_lineWidth=0;
    else {
      MWAWGraphicStyle::Pattern pat;
      MWAWColor color;
      if (!m_state->get(patIds[1]-1, pat))
        f << "##line[color]=" << patIds[1] << ",";
      else {
        if (colIds[1]>=0)
          pat.m_colors[0]=colors[1];
        if (pat.getAverageColor(style.m_lineColor))
          f << "line[color]=" << style.m_lineColor << ",";
        else {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[%d]: can not determine a shape color\n", type));
          f << "###line[color]=" << patIds[1] << ",";
        }
      }
    }
    break;
  }
  case 3: {
    if (len!=(vers==3 ? 40 : vers==4 ? 84 : 0x16c)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[picture]: unexpected data length\n"));
      f << "###";
      break;
    }
    val=int(input->readLong(4));
    if (val) {
      f << "ID1=" << std::hex << val << std::dec << ",";
      hasPicture=true;
    }
    if (vers>4) {
      val=int(input->readLong(2));
      if (val) f << "f2=" << val << ",";
    }
    int iDim[2]; // some multiples of 6, link to decal?
    for (auto &d : iDim) d=int(input->readLong(2));
    if (iDim[0] || iDim[1])
      f << "unkn=" << MWAWVec2i(iDim[0],iDim[1]) << ",";
    for (int i=0; i<2; ++i) {
      val=int(input->readULong(2));
      if (val!=100)
        f << "scale" << (i==0 ? "X" : "Y") << "=" << val << "%,";
    }
    if (vers==3)
      break;
    for (int i=0; i<(vers==4 ? 20 : 3); ++i) {
      val=int(input->readLong(2));
      if (!val) continue;
      if (i==2) // g2&2 run around graphic/frame?, g2&200 print as gray
        f << "g" << i << "=" << std::hex << val << std::dec << ",";
      else
        f << "g" << i << "=" << val << ",";
    }
    break;
  }
  case 4: {
    if (len!=(vers==3 ? 80 : vers==4 ? 100 : 104)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[text]: unexpected data length\n"));
      f << "###";
      break;
    }
    f << "IDS1=["; // next, prev
    for (int i=0; i<2; ++i) f << std::hex << input->readULong(4) << std::dec << ",";
    f << "],";
    if (vers==3) {
      f << "id=" << input->readLong(2) << ","; // 1-f
      f << "ID2=" << std::hex << input->readULong(4) << std::dec << ",";
      val=int(input->readULong(4));
      if (val) {
        f << "tab[ID]=" << std::hex << val << std::dec << ",";
        hasTabs=true;
      }
    }
    else {
      val=int(input->readULong(4));
      if (val) {
        f << "tab[ID]=" << std::hex << val << std::dec << ",";
        hasTabs=true;
      }
      f << "id=" << input->readLong(2) << ","; // 1-f
      f << "ID2=" << std::hex << input->readULong(4) << std::dec << ",";
    }
    int tDim[4];
    for (auto &d : tDim) d=int(input->readLong(2));
    f << "unkn=" << MWAWBox2i(MWAWVec2i(tDim[0],tDim[1]),MWAWVec2i(tDim[2],tDim[3])) << ",";
    for (int i=0; i<5; ++i) {
      val=int(input->readLong(2));
      if (val)
        f << "f" << i+2 << "=" << val << ",";
    }
    val=int(input->readULong(2));
    if (val&0x4)
      f << "postscript,";
    if (val&0x10) // useme: basically the text is hidden
      f << "white[type],";
    if (val&0x20)
      f << "ignore[run,around],";
    val&=0xffdb;
    if (val)
      f << "fl1=" << std::hex << val << std::dec << ",";
    ascii().addDelimiter(input->tell(),'|');
    input->seek(pos+76+(vers==3 ? 0 : 4), librevenge::RVNG_SEEK_SET);
    ascii().addDelimiter(input->tell(),'|');
    shape.m_textId=int(input->readLong(2));
    f << "text[id]=" << shape.m_textId << ",";
    for (int i=0; i<2; ++i) {
      shape.m_linkIds[i]=int(input->readLong(2));
      if (shape.m_linkIds[i]==-1)
        continue;
      f << (i==0 ? "prev" : "next") << "[link]=" << shape.m_linkIds[i] << ",";
    }
    for (int i=0; i<2; ++i) {
      val=int(input->readLong(2));
      if (val!=-1)
        f << "g" << i << "=" << val << ",";
    }
    if (vers==3)
      break;
    for (int i=0; i<(vers==4 ? 8 : 9); ++i) { // g4=3100
      val=int(input->readLong(2));
      if (val)
        f << "g" << i+2 << "=" << val << ",";
    }
    if (vers==4)
      break;
    val=int(input->readLong(1));
    switch (val) {
    case 0: // top
      break;
    case 1:
      shape.m_style.m_verticalAlignment=MWAWGraphicStyle::V_AlignBottom;
      f << "vAlign=bottom,";
      break;
    case 2:
      shape.m_style.m_verticalAlignment=MWAWGraphicStyle::V_AlignCenter;
      f << "vAlign=center,";
      break;
    case 3:
      shape.m_style.m_verticalAlignment=MWAWGraphicStyle::V_AlignJustify;
      f << "vAlign=justify[feathering],";
      break;
    case 4:
      shape.m_style.m_verticalAlignment=MWAWGraphicStyle::V_AlignJustify;
      f << "vAlign=justify[paragraph],";
      break;
    default:
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[text]: unknown vertical alignment\n"));
      f << "##vAlign=" << val << ",";
      break;
    }
    val=int(input->readLong(1));
    if (val) f << "h0=" << val << ",";
    break;
  }
  default:
    MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[%d]: unexpected data\n", type));
    f << "###";
    break;
  }
  if (input->tell()!=pos+6+len)
    ascii().addDelimiter(input->tell(),'|');
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  input->seek(pos+6+len, librevenge::RVNG_SEEK_SET);

  if (hasPicture) {
    pos=input->tell();
    len=long(input->readLong(4));
    if ((len&0xffff)<7 || !input->checkPosition(pos+4+len))
      input->seek(pos, librevenge::RVNG_SEEK_SET);
    else {
      shape.m_entries[0].setBegin(pos+4);
      shape.m_entries[0].setLength(len);
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
      ascii().addPos(pos);
      ascii().addNote("Picture-data:");
    }
  }
  if (type==4 && vers>3) {
    pos=input->tell();
    f.str("");
    f << "Text-limits:";
    if (!input->checkPosition(pos+8)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3: can not find the text limits positions\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    for (int i=0; i<2; ++i) {
      shape.m_textPositions[i]=int(input->readLong(4));
      if (shape.m_textPositions[i]==0) continue;
      f << (i==0 ? "min[pos]" : "max[pos]") << "=" << shape.m_textPositions[i] << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (type==4 && shape.m_linkIds[0]<0) {
    for (int st=0; st<(!hasTabs ? 2 : 3); ++st) {
      pos=input->tell();
      len=long(input->readLong(4));
      if (len<10 || pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::readShapeV3[text]: can not find a shape length\n"));
        input->seek(pos, librevenge::RVNG_SEEK_SET);
        f << "###";
        ascii().addPos(pos);
        ascii().addNote("Text-####");
        return false;
      }
      shape.m_entries[st].setBegin(pos+4);
      shape.m_entries[st].setLength(len);
      ascii().addPos(pos);
      ascii().addNote(st==0 ? "Text-text" : st==1 ? "Entries(Style):" : "Entries(Tabs):");
      input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
    }
  }
  layout.m_shapes.push_back(shape);
  return true;
}

bool ReadySetGoParser::readFontsBlock()
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(FontBlock):";
  if (vers<5) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readFontsBlock: unexpected version\n"));
    return false;
  }
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  if (len<4 || endPos<pos+8 || !input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readFontsBlock: the zone's length seems bad\n"));
    return false;
  }

  int N=int(input->readULong(2));
  f << "N=" << N << ",";
  if (N<0 || (len-4)/1110<N) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readFontsBlock: the n values seems bad\n"));
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  f << "unk=" << input->readLong(2) << ",";
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    if (pos+1110 > endPos)
      break;
    f.str("");
    f << "FontBlock-A" << i << ":";
    int cLen=int(input->readULong(1));
    if (cLen>63) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readFontsBlock: the name seems too long\n"));
      f << "###";
      cLen=0;
    }
    std::string name;
    for (int c=0; c<cLen; ++c) {
      char ch=char(input->readLong(1));
      if (ch==0)
        break;
      name+=ch;
    }
    f << name << ",";
    ascii().addDelimiter(pos+64,'|');
    input->seek(pos+1110, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    len=long(input->readLong(4));
    f.str("");
    f << "FontBlock-B" << i << ":";
    if (pos+4+len<pos+4 || !input->checkPosition(pos+4+len)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readFontsBlock: can not find a data block\n"));
      f << "###";
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      return false;
    }
    if (len==0) {
      ascii().addPos(pos);
      ascii().addNote("_");
      continue;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    input->seek(pos+4+len, librevenge::RVNG_SEEK_SET);
  }

  return true;
}

bool ReadySetGoParser::readGlossary()
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Glossary):";
  if (vers<4) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary: unexpected version\n"));
    return false;
  }
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  if (m_state->m_numGlossary<0 || len<52*m_state->m_numGlossary || endPos<pos+4 || !input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary: the zone's length seems bad\n"));
    return false;
  }

  if (len==0) {
    ascii().addPos(pos);
    ascii().addNote("_");
    return true;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  std::vector<bool> hasTabs;
  for (int i=0; i<m_state->m_numGlossary; ++i) {
    pos=input->tell();
    f.str("");
    f << "Glossary-" << i << ":";
    int cLen=int(input->readULong(1));
    if (cLen>35) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary: the name's length seems bad\n"));
      f << "###";
      cLen=0;
    }
    std::string name;
    for (int c=0; c<cLen; ++c) {
      char ch=char(input->readLong(1));
      if (!ch)
        break;
      name+=ch;
    }
    f << name << ",";
    input->seek(pos+36, librevenge::RVNG_SEEK_SET);
    f << "IDS=[";
    for (int j=0; j<4; ++j) {
      auto id=input->readULong(4);
      if (j==2)
        hasTabs.push_back(id!=0);
      if (!id)
        f << "_,";
      else
        f << std::hex << id << std::dec << ",";
    }
    f << "],";
    input->seek(pos+52, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  for (size_t i=0; i<size_t(m_state->m_numGlossary); ++i) {
    for (int step=0; step<(hasTabs[i] ? 3 : 2); ++step) {
      pos=input->tell();
      len=long(input->readLong(4));
      f.str("");
      f << "Glossary-" << (step==0 ? "text" : step==1 ? "style" : "tabs") << "[" << i << "]:";
      endPos=pos+4+len;
      if (endPos<pos+4 || !input->checkPosition(endPos)) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary: can not find a data block\n"));
        f << "###";
        ascii().addPos(pos);
        ascii().addNote(f.str().c_str());
        return false;
      }
      switch (step) {
      case 0: {
        if (len<20) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary[text]: the zone length seems bad\n"));
          f << "###";
          break;
        }
        int cLen=int(input->readULong(4));
        f << "N=" << cLen << ",";
        if (cLen+20>len || cLen+20<20) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary: can not read the number of caracters\n"));
          f << "###";
          cLen=0;
        }
        for (int j=0; j<2; ++j) { // maybe last change font/para
          int val=int(input->readLong(4));
          if (val!=cLen)
            f << "N" << j+1 << "=" << val << ",";
        }
        f << "IDS=[";
        for (int j=0; j<2; ++j) {
          auto val=input->readULong(4);
          if (val)
            f << std::hex << val << std::dec << ",";
          else
            f << "_,";
        }
        f << "],";
        for (int c=0; c<cLen; ++c) {
          unsigned char ch=(unsigned char)input->readULong(1);
          if (ch<0x1f && ch!=0x9)
            f << "[#" << std::hex << int(ch) << std::dec << "]";
          else
            f << ch;
        }
        break;
      }
      case 1: {
        if (len<4) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary[style]: the zone length seems bad\n"));
          f << "###";
          break;
        }
        int N=int(input->readULong(4));
        f << "N=" << N << ",";
        if (N<0 || (len-4)/(vers==4 ? 30 : 38)<N) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary[style]: can not detect the number of styles\n"));
          f << "###";
          break;
        }
        for (int j=0; j<N; ++j) {
          int cPos;
          MWAWFont font;
          MWAWParagraph para;
          if (!readStyle(font, para, &cPos))
            break;
        }
        break;
      }
      case 2: {
        if (len<2) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary[tab]: the zone length seems bad\n"));
          f << "###";
          break;
        }
        int N=int(input->readULong(2));
        f << "N=" << N << ",";
        if (N<0 || (len-2)/148<N) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::readGlossary[tab]: can not detect the number of tabulations\n"));
          f << "###";
          break;
        }
        for (int j=0; j<N; ++j) {
          int cPos;
          std::vector<MWAWTabStop> tabs;
          if (!readTabulations(tabs, 148, &cPos))
            break;
        }
        break;
      }
      default:
        break;
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(endPos, librevenge::RVNG_SEEK_SET);
    }
  }

  return true;
}

bool ReadySetGoParser::readStyles()
{
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Entries(Style):";
  if (vers<4) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected version\n"));
    return false;
  }
  long len=long(input->readLong(4));
  long endPos=pos+4+len;
  if (endPos<pos+4 || !input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: the zone's length seems bad\n"));
    return false;
  }
  int const dataSize=vers==4 ? 74 : 82;
  if (m_state->m_numStyles<0 || m_state->m_numStyles*dataSize>len) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: the zone's length seems too short\n"));
    return false;
  }
  if (len==0) {
    ascii().addPos(pos);
    ascii().addNote("_");
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  int N=int(len/dataSize), numTabZones=0;
  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Style-" << i << ":";
    int cLen=int(input->readULong(1));
    if (cLen>39) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: can not determine the name length\n"));
      f << "##name[len]=" << cLen << ",";
      cLen=0;
    }
    std::string name;
    for (auto j=0; j<cLen; ++j) {
      char c=char(input->readLong(1));
      if (!c)
        break;
      name+=c;
    }
    f << name << ",";
    input->seek(pos+40, librevenge::RVNG_SEEK_SET);
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());

    int cPos;
    MWAWFont font;
    MWAWParagraph para;
    readStyle(font, para, &cPos);

    pos=input->tell();
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    f.str("");
    f << "Style-id" << ":";
    long tabId=long(input->readULong(4));
    if (tabId) {
      ++numTabZones;
      f << "tab[id]=" << std::hex << tabId << std::dec << ",";
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }
  if (input->tell()!=endPos) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: find extra data\n"));
    ascii().addPos(input->tell());
    ascii().addNote("Style:extra#");
  }
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  for (int i=0; i<numTabZones; ++i) {
    int cPos=0;
    std::vector<MWAWTabStop> tabs;
    if (!readTabulations(tabs, -1, &cPos))
      return false;
  }
  return true;
}

bool ReadySetGoParser::readStyle(MWAWFont &font, MWAWParagraph &para, int *cPos)
{
  font=MWAWFont();
  para=MWAWParagraph();
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  libmwaw::DebugStream f;
  f << "Style:";
  if (vers<3) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected version\n"));
    return false;
  }

  long endPos=pos+(vers==3 ? 22 : vers==4 ? 26 : 34)+(cPos ? 4 : 0);
  if (!input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: the zone is too short\n"));
    return false;
  }
  if (cPos) {
    *cPos=int(input->readLong(4));
    f << "pos[char]=" << *cPos << ",";
  }

  font.setId(int(input->readULong(2)));
  para.m_marginsUnit=librevenge::RVNG_INCH;
  for (int i=0; i<3; ++i)
    para.m_margins[i]=float(input->readLong(4))/65536;
  para.m_margins[0]=*para.m_margins[0]-*para.m_margins[1];
  if (vers<=4)
    font.setSize(float(input->readULong(1)));
  else
    font.setSize(float(input->readULong(2))/100);
  uint32_t flags=0;
  int val=int(input->readULong(1));
  if (val&0x1) flags |= MWAWFont::boldBit;
  if (val&0x2) flags |= MWAWFont::italicBit;
  if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
  if (val&0x8) flags |= MWAWFont::embossBit;
  if (val&0x10) flags |= MWAWFont::shadowBit;
  if (val&0x80) font.setStrikeOutStyle(MWAWFont::Line::Simple); // only v4
  if (val&0x60) f << "fl=#" << std::hex << (val&0x60) << std::dec << ",";
  font.setFlags(flags);
  if (vers<=4) {
    val=int(input->readULong(1));
    if (val && val<100)
      para.setInterline(val, librevenge::RVNG_POINT, MWAWParagraph::AtLeast);
    else if (val) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected interline\n"));
      f << "###interline=" << val << ",";
    }
    val=int(input->readULong(1));
    if (val && val<40)
      para.m_spacings[1]=double(val)/72;
    else if (val) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected paragraph spacing\n"));
      f << "###para[spacing]=" << val << ",";
    }
  }
  else {
    input->seek(1, librevenge::RVNG_SEEK_CUR);
    val=int(input->readULong(2));
    if (val && val<100*100)
      para.setInterline(float(val)/100, librevenge::RVNG_POINT, MWAWParagraph::AtLeast);
    else if (val) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected interline\n"));
      f << "###interline=" << float(val)/100 << ",";
    }
    val=int(input->readULong(2));
    if (val && val<40*100)
      para.m_spacings[1]=double(val)/72/100;
    else if (val) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: unexpected paragraph spacing\n"));
      f << "###para[spacing]=" << val << ",";
    }
  }
  val=int(input->readULong(1));
  switch (val & 3) {
  case 1:
    para.m_justify = MWAWParagraph::JustificationRight;
    break;
  case 2:
    para.m_justify = MWAWParagraph::JustificationCenter;
    break;
  case 3:
    para.m_justify = MWAWParagraph::JustificationFull;
    break;
  case 0: // left
  default:
    break;
  }
  if (val&0xfc) f << "fl1=" << std::hex << (val&0xfc) << std::dec << ",";
  val=int(input->readLong(1));
  if (val) f << "f0=" << val << ",";
  if (vers<=4) {
    val=int(input->readLong(1));
    if (val) font.setDeltaLetterSpacing(float(val), librevenge::RVNG_POINT);
    val=int(input->readLong(1));
    if (val) font.set(MWAWFont::Script(-float(val), librevenge::RVNG_POINT));
  }
  else {
    val=int(input->readLong(2));
    if (val) font.set(MWAWFont::Script(-float(val)/100, librevenge::RVNG_POINT));
    val=int(input->readLong(2));
    if (val) font.setDeltaLetterSpacing(float(val)/100, librevenge::RVNG_POINT);
  }
  if (vers>=4) {
    val=int(input->readULong(1));
    if (val!=100)
      f << "word[spacing]=" << val << "%,";
    val=int(input->readULong(1));
    if (val!=100)
      f << "f1=" << val << ",";
  }
  if (vers>=5) {
    val=int(input->readULong(2));
    if (val>=0 && size_t(val)<m_state->m_colors.size())
      font.setColor(m_state->m_colors[size_t(val)]);
    else {
      MWAW_DEBUG_MSG(("ReadySetGoParser::readStyle: find unexpected paragraph color\n"));
      f << "color=###" << val << ",";
    }
  }
  f << font.getDebugString(getFontConverter())  << ",";
  f << para << ",";

  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());
  if (input->tell()!=endPos)
    ascii().addDelimiter(input->tell(),'|');
  input->seek(endPos, librevenge::RVNG_SEEK_SET);

  return true;
}

bool ReadySetGoParser::readTabulationsV1(std::vector<MWAWTabStop> &tabulations, std::string &extra)
{
  tabulations.clear();
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  if (vers>=3) {
    extra="###";
    MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulationsV1: unexpected version\n"));
    return false;
  }
  long endPos=pos+(vers<=1 ? 40 : 32);
  if (!input->checkPosition(endPos)) {
    extra="###";
    MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulationsV1: bad length\n"));
    return false;
  }
  libmwaw::DebugStream f;

  MWAWTabStop tabs[5];
  if (vers==1) {
    for (int i=0; i<5; ++i) {
      tabs[i].m_position=int(input->readLong(2));
      tabs[i].m_position+=float(input->readLong(2))/10000;
      if (tabs[i].m_position>0)
        f << "tab" << i << "[pos]=" << tabs[i].m_position << ",";
    }
  }
  else {
    for (int i=0; i<5; ++i) tabs[i].m_position=int(input->readLong(2));
    for (int i=0; i<5; ++i) {
      tabs[i].m_position+=float(input->readLong(2))/10000;
      if (tabs[i].m_position>0)
        f << "tab" << i << "[pos]=" << tabs[i].m_position << ",";
    }
  }
  bool tabOn[5];
  for (int i=0; i<5; ++i) {
    int val=int(input->readLong(vers<=1 ? 2 : 1));
    tabOn[i]=val==1;
    if (val==(vers==1 ? -1 : 0)) // off
      continue;
    if (val==1)
      f << "tab" << i << "=on,";
    else
      f << "tab" << i << "[on]=" << val << ",";
  }
  if (vers==2)
    input->seek(1, librevenge::RVNG_SEEK_CUR);
  for (int i=0; i<5; ++i) {
    int val=int(input->readLong(vers<=1 ? 2 : 1));
    if (val==1) // left
      continue;
    if (val==(vers==1 ? -1 : 0)) {
      tabs[i].m_alignment=MWAWTabStop::DECIMAL;
      f << "tab" << i << "=decimal,";
    }
    else
      f << "tab" << i << "[type]=" << val << ",";
  }
  if (vers==2)
    input->seek(1, librevenge::RVNG_SEEK_CUR);

  for (int i=0; i<5; ++i) {
    if (tabOn[i])
      tabulations.push_back(tabs[i]);
  }

  extra=f.str();
  return true;
}

bool ReadySetGoParser::readTabulations(std::vector<MWAWTabStop> &tabs, long len, int *cPos)
{
  tabs.clear();
  MWAWInputStreamPtr input = getInput();
  if (!input)
    return false;
  int const vers=version();
  long pos=input->tell();
  long endPos=pos+len;
  if (len<=0) {
    len=long(input->readLong(4));
    endPos=pos+4+len;
  }
  libmwaw::DebugStream f;
  f << "Tabs[list]:";
  if (vers<3) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulations: unexpected version\n"));
    return false;
  }
  if (len<2+(len<=0 ? 4 : 0)+(cPos ? 4 : 0) || !input->checkPosition(endPos)) {
    f << "###";
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulations: bad length\n"));
    return false;
  }
  if (cPos) {
    *cPos=int(input->readLong(4));
    f << "pos[char]=" << *cPos << ",";
  }
  int N=int(input->readLong(2));
  f << "N=" << N << ",";
  int const dataSize=vers<=3 ? 10 : 14;
  if (2+(cPos ? 4 : 0) + dataSize*N > len) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulations: can not read the number of tabs\n"));
    f << "###";
    N=0;
  }
  ascii().addPos(pos);
  ascii().addNote(f.str().c_str());

  for (int i=0; i<N; ++i) {
    pos=input->tell();
    f.str("");
    f << "Tabs" << i << ":";
    MWAWTabStop tab;
    tab.m_position=float(input->readLong(4))/65536;
    f << "pos=" << tab.m_position << ",";
    int val=int(input->readLong(4));
    if (val)
      f << "measure=" << float(val)/65536 << ",";
    val=int(input->readLong(1));
    switch (val) {
    case 0: // left
      f << "left,";
      break;
    case 1:
      tab.m_alignment=MWAWTabStop::CENTER;
      f << "center,";
      break;
    case 2:
      tab.m_alignment=MWAWTabStop::RIGHT;
      f << "right,";
      break;
    case 3:
      tab.m_alignment=MWAWTabStop::DECIMAL;
      f << "decimal,";
      break;
    default:
      MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulations: unknown tab's alignment\n"));
      f << "###align=" << val << ",";
    }
    val=int(input->readLong(1));
    if (val) {
      f << "on,";
      if (val!=1) {
        f << "leader=" << char(val) << ",";
        int unicode=getFontConverter()->unicode(12,(unsigned char) val);
        if (unicode!=-1)
          tab.m_leaderCharacter=uint16_t(unicode);
        else if (val>0x1f && val<0x80)
          tab.m_leaderCharacter=uint16_t(unicode);
        else {
          f << "###";
          MWAW_DEBUG_MSG(("ReadySetGoParser::readTabulations: unknown tab's leader character\n"));
        }
      }
      tabs.push_back(tab);
    }
    if (vers>3) {
      val=int(input->readLong(4));
      if (val)
        f << "decal[decimal]=" << float(val)/65536;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
  }

  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}


////////////////////////////////////////////////////////////
// try to read the print info zone
////////////////////////////////////////////////////////////
bool ReadySetGoParser::readPrintInfo()
{
  MWAWInputStreamPtr input = getInput();
  int const vers=version();
  long pos=input->tell();
  long endPos=pos+120+(vers<3 ? 2 : 4);
  if (!input->checkPosition(endPos) || input->readULong((vers<3 ? 2 : 4))!=0x78) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readPrintInfo: file seems too short\n"));
    return false;
  }
  libmwaw::DebugStream f;
  f << "Entries(PrintInfo):";
  libmwaw::PrinterInfo info;
  if (!info.read(input)) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::readPrintInfo: can not read print info\n"));
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
  input->seek(endPos, librevenge::RVNG_SEEK_SET);
  return true;
}

////////////////////////////////////////////////////////////
//
// send data
//
////////////////////////////////////////////////////////////
bool ReadySetGoParser::updateTextBoxLinks()
{
  if (version()<=2) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks: bad version\n"));
    return false;
  }
  std::map<int, ReadySetGoParserInternal::Shape *> idToShapeMap;
  std::map<int, int> idToLinkIdsMap[2];
  for (auto &layout : m_state->m_layouts) {
    for (auto &shape : layout.m_shapes) {
      if (shape.m_linkIds[0]<0 && shape.m_linkIds[1]<0)
        continue;
      if (idToShapeMap.find(shape.m_textId)!=idToShapeMap.end()) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks: find dupplicated text id=%d\n", shape.m_textId));
        return false;
      }
      idToShapeMap[shape.m_textId]=&shape;
      for (int i=0; i<2; ++i) {
        if (shape.m_linkIds[i]<0) continue;
        if (idToLinkIdsMap[i].find(shape.m_linkIds[i])!=idToLinkIdsMap[i].end()) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks[%d]: find dupplicated text id=%d\n", i, shape.m_linkIds[i]));
          return false;
        }
        idToLinkIdsMap[i][shape.m_linkIds[i]]=shape.m_textId;
      }
    }
  }

  // check that the link are coherent, ie. for each link, there exists a reciprocal link
  for (auto st=0; st<2; ++st) {
    std::set<int> badIds;
    for (auto const &it : idToLinkIdsMap[st]) {
      auto rIt=idToLinkIdsMap[1-st].find(it.second);
      if (rIt==idToLinkIdsMap[1-st].end() || rIt->second!=it.first) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks: find no reciprocal link for link=%d-%d\n", it.first, it.second));
        badIds.insert(it.first);
      }
    }
    for (auto bad : badIds)
      idToLinkIdsMap[st].erase(bad);
  }

  // check that there is no loop: following next path
  for (auto const &iIt : idToShapeMap) {
    std::set<int> ids;
    int id=iIt.first, firstId=id;
    bool ok=true;
    while (true) {
      if (ids.find(id)!=ids.end()) {
        ok=false;
        MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks: find a look for link id=%d\n", id));
        break;
      }
      ids.insert(id);
      auto const &nextIt=idToLinkIdsMap[1].find(id);
      if (nextIt==idToLinkIdsMap[1].end())
        break;
      id=nextIt->second;
    }
    if (ok) continue;
    // ok: remove this loop
    id=firstId;
    while (true) {
      auto const &nextIt=idToLinkIdsMap[1].find(id);
      if (nextIt==idToLinkIdsMap[1].end())
        break;
      int nextId=nextIt->second;
      idToLinkIdsMap[1].erase(id);
      idToLinkIdsMap[0].erase(nextId);
      id=nextId;
    }
  }

  if (version()<4) {
    // update the shape's style name
    for (auto &iIt : idToShapeMap) {
      auto &shape=*iIt.second;
      int prevId=shape.m_linkIds[0];
      if (prevId>=0 && idToLinkIdsMap[0].find(prevId)!=idToLinkIdsMap[0].end()) {
        std::stringstream s;
        s << "Frame" << iIt.first;
        shape.m_style.m_frameName=s.str();
      }
      int nextId=shape.m_linkIds[1];
      if (nextId>=0 && idToLinkIdsMap[1].find(nextId)!=idToLinkIdsMap[1].end()) {
        std::stringstream s;
        s << "Frame" << nextId;
        shape.m_style.m_frameNextName=s.str();
      }
    }
  }
  else {
    for (auto const &iIt : idToShapeMap) {
      auto const &shape=*iIt.second;
      if (shape.m_linkIds[0]>=0 || shape.m_linkIds[1]<0) continue;
      // shape is the first node of a loop
      int nextId=shape.m_linkIds[1];
      while (nextId>=0) {
        if (idToLinkIdsMap[1].find(nextId)==idToLinkIdsMap[1].end()) // the link has been cutted
          break;
        auto nextShapeIt=idToShapeMap.find(nextId);
        if (nextShapeIt==idToShapeMap.end()) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::updateTextBoxLinks: can not find shape corresponding to id=%d\n", nextId));
          break;
        }
        for (int l=0; l<3; ++l) // update the shape entries
          nextShapeIt->second->m_entries[l]=shape.m_entries[l];
        nextId=nextShapeIt->second->m_linkIds[1];
      }
    }
  }
  return true;
}

bool ReadySetGoParser::send(ReadySetGoParserInternal::Shape const &shape)
{
  auto input=getInput();
  auto listener=getGraphicListener();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::send: can not find the listener\n"));
    return false;
  }
  MWAWPosition pos(shape.m_box[0], shape.m_box.size(), librevenge::RVNG_POINT);
  pos.setRelativePosition(MWAWPosition::Page);
  if (shape.m_wrapRoundAround)
    pos.m_wrapping=MWAWPosition::WDynamic;
  switch (shape.m_type) {
  case ReadySetGoParserInternal::Shape::T_Empty:
    break;
  case ReadySetGoParserInternal::Shape::T_Text: {
    auto subdoc=std::make_shared<ReadySetGoParserInternal::SubDocument>(*this, getInput(), shape);
    listener->insertTextBox(pos, subdoc, shape.m_style);
    break;
  }
  case ReadySetGoParserInternal::Shape::T_Line: {
    auto gShape=MWAWGraphicShape::line(shape.m_points[0], shape.m_points[1]);
    listener->insertShape(pos, gShape, shape.m_style);
    break;
  }
  case ReadySetGoParserInternal::Shape::T_Oval: {
    auto gShape=MWAWGraphicShape::circle(shape.m_box);
    listener->insertShape(pos, gShape, shape.m_style);
    break;
  }
  case ReadySetGoParserInternal::Shape::T_Rectangle: {
    auto gShape=MWAWGraphicShape::rectangle(shape.m_box);
    listener->insertShape(pos, gShape, shape.m_style);
    break;
  }
  case ReadySetGoParserInternal::Shape::T_RectOval: {
    auto gShape=MWAWGraphicShape::rectangle(shape.m_box, shape.m_cornerSize[0]>=0 ? 0.5f*MWAWVec2f(float(shape.m_cornerSize[0]), float(shape.m_cornerSize[1]))
                                            : 0.25f*shape.m_box.size());
    listener->insertShape(pos, gShape, shape.m_style);
    break;
  }
  case ReadySetGoParserInternal::Shape::T_Picture: {
    if (!shape.m_entries[0].valid() || !input->checkPosition(shape.m_entries[0].end())) {
      MWAWGraphicStyle style;
      listener->openGroup(pos);
      auto gShape=MWAWGraphicShape::rectangle(shape.m_box);
      listener->insertShape(pos, gShape, style);
      gShape=MWAWGraphicShape::line(shape.m_box[0],shape.m_box[1]);
      listener->insertShape(pos, gShape, style);
      gShape=MWAWGraphicShape::line(MWAWVec2f(shape.m_box[0][0],shape.m_box[1][1]), MWAWVec2f(shape.m_box[1][0],shape.m_box[0][1]));
      listener->insertShape(pos, gShape, style);
      listener->closeGroup();
      break;
    }
    input->seek(shape.m_entries[0].begin(), librevenge::RVNG_SEEK_SET);
    std::shared_ptr<MWAWPict> pict(MWAWPictData::get(input, int(shape.m_entries[0].length())));
    MWAWEmbeddedObject object;
    if (pict && pict->getBinary(object) && !object.m_dataList.empty()) {
      listener->insertPicture(pos, object);
#ifdef DEBUG_WITH_FILES
      static int volatile pictName = 0;
      libmwaw::DebugStream f2;
      f2 << "PICT-" << ++pictName << ".pct";
      libmwaw::Debug::dumpFile(object.m_dataList[0], f2.str().c_str());
      ascii().skipZone(shape.m_entries[0].begin(), shape.m_entries[0].end()-1);
#endif
    }
    else {
      MWAW_DEBUG_MSG(("ReadySetGoParser::send: sorry, can not retrieve a picture\n"));
    }
    break;
  }
  case ReadySetGoParserInternal::Shape::T_Unknown:
  default:
    MWAW_DEBUG_MSG(("ReadySetGoParser::send: sorry sending a shape with type=%d is not implemented\n", int(shape.m_type)));
    break;
  }
  return true;
}

bool ReadySetGoParser::sendText(ReadySetGoParserInternal::Shape const &shape)
{
  auto input=getInput();
  auto listener=getGraphicListener();
  int const vers=version();
  if (!input || !listener) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the listener\n"));
    return false;
  }
  if (shape.m_type!=shape.T_Text) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: unexpected type\n"));
    return false;
  }
  if (!shape.m_entries[0].valid() || shape.m_entries[0].length()<4 || !input->checkPosition(shape.m_entries[0].end())) {
    if (shape.m_linkIds[0]<0) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the character zone\n"));
      return false;
    }
    return true;
  }
  input->seek(shape.m_entries[0].begin(), librevenge::RVNG_SEEK_SET);
  libmwaw::DebugStream f;
  int const lengthSize=vers<3 ? 2 : 4;
  int len=int(input->readULong(lengthSize));
  long begTextPos=shape.m_entries[0].begin()+(vers<3 ? 2 : 20);
  if (len+(vers<3 ? 2*lengthSize : 20)>shape.m_entries[0].length()) {
    MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the character zone\n"));
    f << "###";
    ascii().addPos(shape.m_entries[0].begin());
    ascii().addNote("Text-text:###");
    return false;
  }

  int minCPos=0, maxCPos=len;

  // first try to read the pararagraph style: v1-v2 or the list of style v3
  auto fontConverter=getFontConverter();
  std::map<int,MWAWFont> posToFont;
  std::map<int,MWAWParagraph> posToPara;
  std::map<int, std::vector<MWAWTabStop> > posToTabs;
  if (vers<3) {
    MWAWParagraph para=shape.m_paragraph;
    if (!shape.m_entries[1].valid() || shape.m_entries[1].length()!=0x1e || !input->checkPosition(shape.m_entries[1].end())) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the paragraph zone\n"));
    }
    else {
      input->seek(shape.m_entries[1].begin(), librevenge::RVNG_SEEK_SET);
      long pos=input->tell();
      f.str("");
      // unsure the first line's style is sometimes different than the
      // other lines', but the interface is so weird that it is
      // difficult to understand what happens
      for (int i=0; i<2; ++i) { // left and right margins from left,
        int val=int(input->readLong(2));
        if (val) f << "margins[" << (i==0 ? "left" : "right") << "]=" << val << ",";
      }
      int val=int(input->readLong(1));
      switch (val & 3) {
      case 1:
        para.m_justify = MWAWParagraph::JustificationCenter;
        break;
      case 2:
        para.m_justify = MWAWParagraph::JustificationRight;
        break;
      case 3:
        para.m_justify = MWAWParagraph::JustificationFull;
        break;
      case 0: // left
      default:
        break;
      }
      if (val&0xfc) f << "fl=" << std::hex << (val&0xfc) << std::dec << ",";
      int interline=0;
      for (int i=0; i<3; ++i) { // small number maybe another justification
        val=int(input->readLong(1));
        if (!val) continue;
        if ((i==0 && vers==1) || (i==2 && vers==2))
          interline=val;
        else
          f << "f" << i << "=" << val << ",";
      }
      switch (interline) {
      case 0: // normal
      case 1:
      case 2:
        para.setInterline(1+double(interline)/2, librevenge::RVNG_PERCENT);
        break;
      default:
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: unknown interline\n"));
        f << "interline=###" << interline << ",";
        break;
      }
      int dim[4];
      for (auto &d : dim) d=int(input->readLong(2));
      f << "box?=" << MWAWBox2i(MWAWVec2i(dim[0],dim[1]),MWAWVec2i(dim[2],dim[3])) << ",";
      f << para;
      ascii().addDelimiter(input->tell(),'|');
      ascii().addPos(pos-lengthSize);
      ascii().addNote(f.str().c_str());
    }
    listener->setParagraph(para);

    // now read the list of char style
    input->seek(shape.m_entries[0].begin()+lengthSize+len+(len%2), librevenge::RVNG_SEEK_SET);
    long pos=input->tell();
    int cLen=int(input->readULong(lengthSize));
    f.str("");
    f << "Text-font:";
    if (pos+2+cLen>shape.m_entries[0].end() || (cLen%6)) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the find the number of fonts\n"));
      f << "###";
      cLen=0;
    }
    ascii().addPos(pos);
    ascii().addNote(f.str().c_str());
    for (int s=0; s<(cLen/6); ++s) {
      pos=input->tell();
      f.str("");
      f << "Text-font" << s << ":";
      int cPos=int(input->readULong(2));
      if (cPos) f << "pos=" << cPos << ",";
      MWAWFont font;
      font.setSize(float(input->readULong(1)));
      uint32_t flags=0;
      int val=int(input->readULong(1));
      if (val&0x1) flags |= MWAWFont::boldBit;
      if (val&0x2) flags |= MWAWFont::italicBit;
      if (val&0x4) font.setUnderlineStyle(MWAWFont::Line::Simple);
      if (val&0x8) flags |= MWAWFont::embossBit;
      if (val&0x10) flags |= MWAWFont::shadowBit;
      if (val&0xe0) f << "fl=#" << std::hex << (val&0xe0) << std::dec << ",";
      font.setFlags(flags);
      font.setId(int(input->readULong(2)));
      f << font.getDebugString(fontConverter)  << ",";
      if (posToFont.find(cPos)==posToFont.end())
        posToFont[cPos]=font;
      else {
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: find duplicated position for font's style\n"));
        f << "###";
      }
      ascii().addPos(pos);
      ascii().addNote(f.str().c_str());
      input->seek(pos+6, librevenge::RVNG_SEEK_SET);
    }


  }
  else {
    if (shape.m_entries[2].valid() && shape.m_entries[2].length()>2) {
      input->seek(shape.m_entries[2].begin(), librevenge::RVNG_SEEK_SET);

      int numPara=1;
      if (vers>3) {
        f.str("");
        numPara=int(input->readLong(2));
        f << "N=" << numPara << ",";
        if (2+148*numPara>shape.m_entries[2].length()) {
          MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not determine the number of differents tabulation\n"));
          f << "###";
          numPara=0;
        }
        ascii().addPos(shape.m_entries[2].begin()-4);
        ascii().addNote(f.str().c_str());
      }

      for (int p=0; p<numPara; ++p) {
        int cPos=0;
        std::vector<MWAWTabStop> tabs;
        if (!readTabulations(tabs, vers==3 ? shape.m_entries[2].length() : 148, vers==3 ? nullptr : &cPos))
          break;
        posToTabs[cPos]=tabs;
      }
    }

    if (!shape.m_entries[1].valid() || shape.m_entries[1].length()<4 || !input->checkPosition(shape.m_entries[1].end())) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the style zone\n"));
    }
    else {
      input->seek(shape.m_entries[1].begin(), librevenge::RVNG_SEEK_SET);
      f.str("");
      int N=int(input->readLong(4));
      f << "N=" << N << ",";
      int const dataSize=vers==3 ? 26 : 30;
      if (N<0 || (shape.m_entries[1].length()-4)/dataSize<N || 4+N*dataSize>shape.m_entries[1].length()) {
        f << "###";
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: can not find the number of styles\n"));
        N=0;
      }
      ascii().addPos(shape.m_entries[1].begin()-4);
      ascii().addNote(f.str().c_str());
      for (int s=0; s<N; ++s) {
        int cPos;
        MWAWFont font;
        MWAWParagraph para;
        if (!readStyle(font,para,&cPos))
          break;

        // the position can be sometimes dupplicated, so use the latter
        posToFont[cPos]=font;
        posToPara[cPos]=para;
      }
    }
    if (shape.m_entries[0].valid() && shape.m_entries[0].length()>=20) {
      input->seek(shape.m_entries[0].begin()+4, librevenge::RVNG_SEEK_SET);
      f.str("");
      f << "N=" << len << ",";
      for (int i=0; i<2; ++i) { // maybe last change font/para
        int val=int(input->readLong(4));
        if (val!=len)
          f << "N" << i+1 << "=" << val << ",";
      }
      f << "IDS=[";
      for (int i=0; i<2; ++i) {
        auto val=input->readULong(4);
        if (val)
          f << std::hex << val << std::dec << ",";
        else
          f << "_,";
      }
      f << "],";
      ascii().addPos(shape.m_entries[0].begin());
      ascii().addNote(f.str().c_str());
    }
    if (vers>3) {
      // time to use the text limit
      if (shape.m_textPositions[0]<0 || shape.m_textPositions[0]>len) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: the minimum position seems bad\n"));
      }
      else
        minCPos=shape.m_textPositions[0];
      if (shape.m_textPositions[1]<minCPos || shape.m_textPositions[1]>len) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: the maximum position seems bad\n"));
      }
      // min=max=0 means all data
      // if there is not a next frame, we do not want to cut the text
      else if (shape.m_textPositions[1]>0 && shape.m_textPositions[1]+1<len && shape.m_linkIds[1]>=0)
        maxCPos=shape.m_textPositions[1]+1;
    }
  }

  f.str("");
  f << "Text-text:";
  input->seek(begTextPos+minCPos, librevenge::RVNG_SEEK_SET);
  std::vector<MWAWTabStop> tabs;
  MWAWParagraph para;
  if (minCPos!=0) {
    // we need to retrieve the current style
    auto tIt=posToTabs.lower_bound(minCPos);
    if (tIt!=posToTabs.begin()) {
      --tIt;
      tabs=tIt->second;
      para.m_tabs=tabs;
      listener->setParagraph(para);
    }
    auto pIt=posToPara.lower_bound(minCPos);
    if (pIt!=posToPara.begin()) {
      --pIt;
      para=pIt->second;
      para.m_tabs=tabs;
      listener->setParagraph(para);
    }
    auto fIt=posToFont.lower_bound(minCPos);
    if (fIt!=posToFont.begin()) {
      --fIt;
      listener->setFont(fIt->second);
    }
  }
  for (int c=minCPos; c<maxCPos; ++c) {
    auto tIt=posToTabs.find(c);
    if (tIt!=posToTabs.end()) {
      tabs=tIt->second;
      para.m_tabs=tabs;
      listener->setParagraph(para);
    }
    auto pIt=posToPara.find(c);
    if (pIt!=posToPara.end()) {
      para=pIt->second;
      para.m_tabs=tabs;
      listener->setParagraph(para);
    }
    auto fIt=posToFont.find(c);
    if (fIt!=posToFont.end())
      listener->setFont(fIt->second);
    if (input->isEnd()) {
      MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: find end of input at pos=%d\n", c));
      f << "###";
      break;
   }
    unsigned char ch=(unsigned char)(input->readULong(1));
    if (ch)
      f << ch;
    else
      f << "[#page]";
    switch (ch) {
    case 0:
      listener->insertField(MWAWField(MWAWField::PageNumber));
      break;
    case 5:
      listener->insertField(MWAWField(MWAWField::PageCount));
      break;
    case 0x9:
      listener->insertTab();
      break;
    case 0xd:
      listener->insertEOL();
      break;
    case 0x1f: // soft hyphen
      break;
    default:
      if (ch<=0x1f) {
        MWAW_DEBUG_MSG(("ReadySetGoParser::sendText: find unknown char=%d at pos=%d\n", int(ch), c));
        f << "###";
        break;
      }
      listener->insertCharacter(ch);
    }
  }
  ascii().addPos(shape.m_entries[0].begin());
  ascii().addNote(f.str().c_str());
  return true;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
