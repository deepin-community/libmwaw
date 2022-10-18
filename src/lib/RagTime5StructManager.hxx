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

#ifndef RAG_TIME_5_STRUCT_MANAGER
#  define RAG_TIME_5_STRUCT_MANAGER

#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "libmwaw_internal.hxx"
#include "MWAWDebug.hxx"
#include "MWAWEntry.hxx"

class RagTime5Document;

//! main zone in a RagTime v5-v6 document
class RagTime5Zone
{
public:
  //! constructor
  RagTime5Zone(MWAWInputStreamPtr const &input, libmwaw::DebugFile &asc)
    : m_level(-1)
    , m_parentName("")
    , m_defPosition(0)
    , m_entry()
    , m_name("")
    , m_hiLoEndian(true)
    , m_entriesList()
    , m_childIdToZoneMap()
    , m_isInitialised(false)
    , m_isParsed(false)
    , m_extra("")
    , m_input()
    , m_defaultInput(input)
    , m_asciiName("")
    , m_asciiFile(&asc)
    , m_mainAsciiFile(&asc)
    , m_localAsciiFile()
  {
    for (auto &id : m_ids) id=0;
    for (auto &idFlag : m_idsFlag) idFlag=0;
    for (auto &kind : m_kinds) kind="";
    for (auto &variableD : m_variableD) variableD=0;
  }
  //! destructor
  virtual ~RagTime5Zone();
  //! returns the zone name
  std::string getZoneName() const;
  //! returns the main type
  std::string getKindLastPart(bool main=true) const
  {
    std::string res(m_kinds[main ? 0 : 1]);
    std::string::size_type pos = res.find_last_of(':');
    if (pos == std::string::npos) return res;
    return res.substr(pos+1);
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, RagTime5Zone const &z);
  //! returns the current input
  MWAWInputStreamPtr getInput()
  {
    return m_input;
  }
  //! reset the current input
  void setInput(MWAWInputStreamPtr const &input)
  {
    m_input = input;
  }
  //! returns true if the input correspond to the basic file
  bool isMainInput() const
  {
    return m_input.get()==m_defaultInput.get();
  }
  //! returns the current ascii file
  libmwaw::DebugFile &ascii()
  {
    if (!isMainInput() && !m_localAsciiFile)
      createAsciiFile();
    return *m_asciiFile;
  }
  //! defines the ascii name
  void setAsciiFileName(std::string const &name)
  {
    m_asciiName = name;
  }
  //! creates the ascii file
  void createAsciiFile();
  //! mark a zone as bad
  void addErrorInDebugFile(std::string const &zoneName);

  //! the zone level
  int m_level;
  //! the parent name
  std::string m_parentName;
  //! the position of the definition in the main zones
  long m_defPosition;
  //! the zone types: normal and packing
  std::string m_kinds[2];
  //! the zone entry
  MWAWEntry m_entry;
  //! the zone name ( mainly used for debugging)
  std::string m_name;
  //! true if the endian is hilo
  bool m_hiLoEndian;
  //! the zone id
  int m_ids[3];
  //! the zone flag
  int m_idsFlag[3];
  //! the list of original entries
  std::vector<MWAWEntry> m_entriesList;
  //! the child zones
  std::map<int,std::shared_ptr<RagTime5Zone> > m_childIdToZoneMap;
  //! the content of the zone D if it exists
  int m_variableD[2];
  //! a flag to know if the zone is initialised (stream, hiLoEndian)
  mutable bool m_isInitialised;
  //! a flag to know if the zone is parsed
  mutable bool m_isParsed;
  //! extra data
  std::string m_extra;
protected:
  //! the main input
  MWAWInputStreamPtr m_input;
  //! the main file input
  MWAWInputStreamPtr m_defaultInput;
  //! the ascii file name ( used if we need to create a ascii file)
  std::string m_asciiName;
  //! the ascii file corresponding to an input
  libmwaw::DebugFile *m_asciiFile;
  //! the main ascii file (used to print error)
  libmwaw::DebugFile *m_mainAsciiFile;
  //! the local ascii file ( if we need to create a new input)
  std::shared_ptr<libmwaw::DebugFile> m_localAsciiFile;
private:
  RagTime5Zone(RagTime5Zone const &orig) = delete;
  RagTime5Zone &operator=(RagTime5Zone const &orig) = delete;
};

//! basic class used to store RagTime 5/6 structures
class RagTime5StructManager
{
public:
  struct Field;
  //! constructor
  explicit RagTime5StructManager(RagTime5Document &doc);
  //! destructor
  ~RagTime5StructManager();

  //! try to read a list of type definition
  bool readTypeDefinitions(RagTime5Zone &zone);
  //! try to read a field
  bool readField(MWAWInputStreamPtr input, long endPos, libmwaw::DebugFile &ascFile,
                 Field &field, long fSz=0);
  //! try to read a compressed long
  static bool readCompressedLong(MWAWInputStreamPtr &input, long endPos, long &val);
  //! try to read a unicode string
  static bool readUnicodeString(MWAWInputStreamPtr input, long endPos, librevenge::RVNGString &string);
  //! try to read n data id
  static bool readDataIdList(MWAWInputStreamPtr input, int n, std::vector<int> &listIds);

  //! debug: print a file type
  static std::string printType(unsigned long fileType);

  //! a tabulation in RagTime 5/6 structures
  struct TabStop {
    //! constructor
    TabStop()
      : m_position(0)
      , m_type(1)
      , m_leaderChar(0)
    {
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, TabStop const &tab)
    {
      o << tab.m_position;
      switch (tab.m_type) {
      case 1:
        break;
      case 2:
        o << "R";
        break;
      case 3:
        o << "C";
        break;
      case 4:
        o << "D";
        break;
      case 5: // Kintou Waritsuke: sort of center
        o << "K";
        break;
      default:
        o << ":#type=" << tab.m_type;
        break;
      }
      if (tab.m_leaderChar>0)
        o << ":leader=" << char(tab.m_leaderChar);
      return o;
    }
    //! the position
    float m_position;
    //! the type
    int m_type;
    //! the unicode leader char
    uint16_t m_leaderChar;
  };
  //! a field of RagTime 5/6 structures
  struct Field {
    //! the different type
    enum Type { T_Unknown, T_Bool, T_Double, T_Long, T_2Long, T_FieldList, T_LongList, T_DoubleList, T_TabList,
                T_Code, T_Color, T_CondColor, T_PrintInfo, T_String, T_Unicode, T_ZoneId, T_LongDouble, T_Unstructured
              };

    //! constructor
    Field()
      : m_type(T_Unknown)
      , m_fileType(0)
      , m_name("")
      , m_doubleValue(0)
      , m_color()
      , m_string("")
      , m_longList()
      , m_doubleList()
      , m_numLongByData(1)
      , m_tabList()
      , m_fieldList()
      , m_entry()
      , m_extra("")
    {
      for (auto &val : m_longValue) val=0;
    }
    Field(Field const &)=default;
    //! destructor
    ~Field()
    {
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, Field const &field);
    //! the field type
    Type m_type;
    //! the file type
    unsigned long m_fileType;
    //! the field type name
    std::string m_name;
    //! the long value
    long m_longValue[2];
    //! the double value
    double m_doubleValue;
    //! the color
    MWAWColor m_color;
    //! small string use to store a string or a 4 char code
    librevenge::RVNGString m_string;
    //! the list of long value
    std::vector<long> m_longList;
    //! the list of double value
    std::vector<double> m_doubleList;
    //! the number of long by data (in m_longList)
    int m_numLongByData;
    //! the list of tabStop
    std::vector<TabStop> m_tabList;
    //! the list of field
    std::vector<Field> m_fieldList;
    //! entry to defined the position of a String or Unstructured data
    MWAWEntry m_entry;
    //! extra data
    std::string m_extra;
  };
  //! a zone link in RagTime 5/6 structures
  struct ZoneLink {
    //! constructor
    ZoneLink()
      : m_dataId(0)
      , m_valuesList()
      , m_extra("")
    {
      for (auto &id : m_subZoneId) id=0;
    }
    //! returns the sub zone id
    int getSubZoneId(int i) const
    {
      if (i<0 || i>=2) {
        MWAW_DEBUG_MSG(("RagTime5StructManager::ZoneLink::getSubZoneId: called with bad value=%d\n", i));
        return 0;
      }
      return (m_subZoneId[0]&0x3fffffff);
    }
    //! operator<<
    friend std::ostream &operator<<(std::ostream &o, ZoneLink const &link)
    {
      if (link.m_dataId) o << "data" << link.m_dataId << "A,";
      if (link.m_subZoneId[0]) {
        if (link.m_subZoneId[0]> 0&& (static_cast<unsigned long>(link.m_subZoneId[0])&0xc0000000)==0)
          o << "data[subId]=" << link.m_subZoneId[0] << ",";
        else
          o << "data[subId]=" << (link.m_subZoneId[0]&0x3fffffff) << "[" << (link.m_subZoneId[0]>>30) << "],";
      }
      if (link.m_subZoneId[1]) {
        if (link.m_subZoneId[1]>0 && (static_cast<unsigned long>(link.m_subZoneId[1])&0xff000000)==0)
          o << "orig[subId]=" << link.m_subZoneId[1] << ",";
        else
          o << "orig[subId]=" << (link.m_subZoneId[1]&0x00ffffff) << "[" << (link.m_subZoneId[1]>>24) << "],";
      }
      for (size_t i=0; i<link.m_valuesList.size(); ++i) {
        if (!link.m_valuesList[i]) continue;
        o << "f" << i << "=" << link.m_valuesList[i] << ",";
      }
      return o;
    }
    //! the data id (or 0)
    int m_dataId;
    //! the sub id zones
    long m_subZoneId[2];
    //! list of potential values
    std::vector<long> m_valuesList;
    //! extra data
    std::string m_extra;
  };
  //! virtual class use to parse the field data
  struct FieldParser {
    //! constructor
    explicit FieldParser(std::string const &zoneName)
      : m_regroupFields(false)
      , m_name(zoneName)
    {
    }
    //! destructor
    virtual ~FieldParser();
    //! return the debug name corresponding to a zone
    virtual std::string getZoneName() const
    {
      return m_name;
    }
    //! return the debug name corresponding to a field
    virtual std::string getZoneName(int n) const
    {
      std::stringstream s;
      s << m_name << "-" << n;
      return s.str();
    }
    //! parse a header field
    virtual bool parseHeaderField(Field &field, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f)
    {
      f << field;
      return true;
    }
    //! parse a field
    virtual bool parseField(Field &field, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &f)
    {
      f << field;
      return true;
    }
    //! a flag use to decide if we output one debug message by field or not
    bool m_regroupFields;
  protected:
    //! the field name
    std::string m_name;
  private:
    FieldParser(FieldParser const &orig) = delete;
    FieldParser &operator=(FieldParser const &orig) = delete;
  };
  //! virtual class use to parse the unstructured data
  struct DataParser {
    //! constructor
    explicit DataParser(std::string const &zoneName);
    //! destructor
    virtual ~DataParser();
    //! return the debug name corresponding to a zone
    virtual std::string getZoneName() const
    {
      return m_name;
    }
    //! return the debug name corresponding to a field
    virtual std::string getZoneName(int n) const
    {
      std::stringstream s;
      s << m_name << "-" << n;
      return s.str();
    }
    //! parse a data
    virtual bool parseData(MWAWInputStreamPtr &/*input*/, long /*endPos*/, RagTime5Zone &/*zone*/, int /*n*/, libmwaw::DebugStream &/*f*/)
    {
      return true;
    }
  protected:
    //! the field name
    std::string m_name;
  private:
    DataParser(DataParser const &orig) = delete;
    DataParser &operator=(DataParser const &orig) = delete;
  };

  /** basic parser to read image/main graphic position properties*/
  struct GObjPropFieldParser final : public FieldParser {
    //! constructor
    explicit GObjPropFieldParser(std::string const &name)
      : FieldParser(name)
      , m_clusterId(0)
    {
    }
    //! parse a field
    bool parseField(Field &field, RagTime5Zone &zone, int n, libmwaw::DebugStream &f) final;

    //! the id of the zone corresponding to this property (always a clust graph ?)
    int m_clusterId;
  };

private:
  //! the main document
  RagTime5Document &m_document;

  RagTime5StructManager(RagTime5StructManager const &orig) = delete;
  RagTime5StructManager operator=(RagTime5StructManager const &orig) = delete;
};

#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
