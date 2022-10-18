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

/* This header contains code specific to a pict which can be stored in a
 *      librevenge::RVNGBinaryData, this includes :
 *         - the mac Pict format (in MWAWPictMac)
 *         - some old data names db3
 *         - some potential short data file
 */

#ifndef MWAW_PICT_DATA
#  define MWAW_PICT_DATA

#  include <ostream>

#  include <librevenge/librevenge.h>

#  include "libmwaw_internal.hxx"
#  include "MWAWPict.hxx"

/** \brief an abstract class which defines basic formated picture ( Apple© Pict, DB3, ...) */
class MWAWPictData : public MWAWPict
{
public:
  //! the picture subtype
  enum SubType { PictMac, DB3, Unknown };
  //! destructor
  ~MWAWPictData() override;
  //! returns the picture type
  Type getType() const override
  {
    return MWAWPict::PictData;
  }
  //! returns the picture subtype
  virtual SubType getSubType() const = 0;

  //! returns the final picture
  bool getBinary(MWAWEmbeddedObject &picture) const override
  {
    if (!valid() || isEmpty()) return false;

    librevenge::RVNGBinaryData data;
    createFileData(m_data, data);
    picture=MWAWEmbeddedObject(data, "image/pict");
    return true;
  }

  //! returns true if we are relatively sure that the data are correct
  virtual bool sure() const
  {
    return getSubType() != Unknown;
  }

  //! returns true if the picture is valid
  virtual bool valid() const
  {
    return false;
  }

  //! returns true if the picture is valid and has size 0 or contains no data
  bool isEmpty() const
  {
    return m_empty;
  }

  /** checks if the data pointed by input is known
     - if not return MWAW_R_BAD
     - if true
     - fills box if possible, if not set box=MWAWBox2f() */
  static ReadResult check(MWAWInputStreamPtr const &input, int size, MWAWBox2f &box)
  {
    return checkOrGet(input, size, box, nullptr);
  }

  /** checks if the data pointed by input is known
   * - if not or if the pict is empty, returns 0L
   * - if not returns a container of picture */
  static MWAWPictData *get(MWAWInputStreamPtr const &input, int size)
  {
    MWAWPictData *res = nullptr;
    MWAWBox2f box;
    if (checkOrGet(input, size, box, &res) == MWAW_R_BAD) return nullptr;
    if (res) { // if the bdbox is good, we set it
      MWAWVec2f sz = box.size();
      if (sz.x()>0 && sz.y()>0) res->setBdBox(box);
    }
    return res;
  }

  /** a virtual function used to obtain a strict order,
   * must be redefined in the subs class */
  int cmp(MWAWPict const &a) const override
  {
    int diff = MWAWPict::cmp(a);
    if (diff) return diff;
    auto const &aPict = static_cast<MWAWPictData const &>(a);

    diff = static_cast<int>(m_empty) - static_cast<int>(aPict.m_empty);
    if (diff) return (diff < 0) ? -1 : 1;
    else if (m_empty) // both empty
      return 0;
    // the type
    diff = getSubType() - aPict.getSubType();
    if (diff) return (diff < 0) ? -1 : 1;

    if (m_data.size() < aPict.m_data.size())
      return 1;
    if (m_data.size() > aPict.m_data.size())
      return -1;
    unsigned char const *data=m_data.getDataBuffer();
    unsigned char const *aData=m_data.getDataBuffer();
    if (!data || !aData) return 0; // must only appear if the two buffers are empty
    for (unsigned long c=0; c < m_data.size(); c++, data++, aData++) {
      if (*data < *aData) return -1;
      if (*data > *aData) return 1;
    }
    return 0;
  }

protected:
  /** a file pict can be created from the data pict by adding a header with size 512,
   * this function do this conversion needed to return the final picture */
  static bool createFileData(librevenge::RVNGBinaryData const &orig, librevenge::RVNGBinaryData &result);

  //! protected constructor: use check to construct a picture
  MWAWPictData()
    : m_data()
    , m_empty(false)
  {
  }
  explicit MWAWPictData(MWAWBox2f &)
    : m_data()
    , m_empty(false)
  {
  }

  /** \brief checks if the data pointed by input and of given size is a pict
   * - if not returns MWAW_R_BAD
   * - if true
   *    - fills the box size
   *    - creates a picture if result is given and if the picture is not empty */
  static ReadResult checkOrGet(MWAWInputStreamPtr input, int size,
                               MWAWBox2f &box, MWAWPictData **result = nullptr);

  //! the data size (without the empty header of 512 characters)
  librevenge::RVNGBinaryData m_data;

  //! some picture can be valid but empty
  bool m_empty;
};

//! a small table file (known by open office)
class MWAWPictDB3 final : public MWAWPictData
{
public:
  //! destructor
  ~MWAWPictDB3() final;

  //! returns the picture subtype
  SubType getSubType() const final
  {
    return DB3;
  }

  //! returns true if the picture is valid
  bool valid() const final
  {
    return m_data.size() != 0;
  }

  /** a virtual function used to obtain a strict order,
  must be redefined in the subs class */
  int cmp(MWAWPict const &a) const final
  {
    return MWAWPictData::cmp(a);
  }

protected:

  //! protected constructor: uses check to construct a picture
  MWAWPictDB3()
  {
    m_empty = false;
  }

  friend class MWAWPictData;
  /** \brief checks if the data pointed by input and of given size is a pict
   * - if not returns MWAW_R_BAD
   * - if true
   *    - set empty to true if the picture contains no data
   *    - creates a picture if result is given and if the picture is not empty */
  static ReadResult checkOrGet(MWAWInputStreamPtr input, int size, MWAWPictData **result = nullptr);
};

//! class to store small data which are potentially a picture
class MWAWPictDUnknown final : public MWAWPictData
{
public:
  //! destructor
  ~MWAWPictDUnknown() final;

  //! returns the picture subtype
  SubType getSubType() const final
  {
    return Unknown;
  }

  //! returns true if the picture is valid
  bool valid() const final
  {
    return m_data.size() != 0;
  }

  /** a virtual function used to obtain a strict order,
   * must be redefined in the subs class */
  int cmp(MWAWPict const &a) const final
  {
    return MWAWPictData::cmp(a);
  }

protected:

  //! protected constructor: uses check to construct a picture
  MWAWPictDUnknown()
  {
    m_empty = false;
  }

  friend class MWAWPictData;

  /** \brief checks if the data pointed by input and of given size is a pict
   * - if not returns MWAW_R_BAD
   * - if true
   *    - set empty to true if the picture contains no data
   *    - creates a picture if result is given and if the picture is not empty */
  static ReadResult checkOrGet(MWAWInputStreamPtr input, int size, MWAWPictData **result = nullptr);
};

#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
