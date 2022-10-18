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
#include <set>
#include <sstream>
#include <stack>

#include <librevenge/librevenge.h>

#include "MWAWFont.hxx"
#include "MWAWFontConverter.hxx"
#include "MWAWListener.hxx"
#include "MWAWParagraph.hxx"
#include "MWAWPosition.hxx"
#include "MWAWSubDocument.hxx"

#include "RagTime5ClusterManager.hxx"
#include "RagTime5Document.hxx"
#include "RagTime5Spreadsheet.hxx"
#include "RagTime5StructManager.hxx"
#include "RagTime5StyleManager.hxx"

#include "RagTime5Formula.hxx"

#include "libmwaw_internal.hxx"

/** Internal: the structures of a RagTime5Formula */
namespace RagTime5FormulaInternal
{
////////////////////////////////////////
//! Internal: the state of a RagTime5Formula
struct State {
  //! constructor
  State()
    : m_idFunctionMap()
    , m_idFunctionSet()
    , m_documentFunctionNames()
    , m_idFormulaMap()
  {
    m_idFunctionMap= {
      // layout
      {0x14c2017, "Page"},
      {0x14c2027, "PageIndex"},
      {0x14c2037, "NoOfPages"},
      {0x14c2047, "Container"},
      {0x14c2057, "StartingPageNumber"},
      {0x14c2067, "EndingPageNumber"},
      // standart
      {0x1559817, "Abs"},
      {0x1559827, "Sign"},
      {0x1559837, "Sqrt"},
      {0x1559847, "SumSqr"}, // Sqr
      {0x1559857, "Int"},
      {0x1559867, "Round"},
      {0x1559877, "Floor"},
      {0x1559887, "Ceiling"},
      {0x1559897, "Trunc"},
      {0x15598a7, "Max"},
      {0x15598b7, "Min"},
      {0x15598c7, "IsNumber"},
      {0x15598d7, "IsBlank"},
      {0x15598e7, "Type"},
      {0x15598f7, "ErrorType"},
      {0x1559907, "IsErr"},
      {0x1559917, "IsNA"},
      {0x1559927, "Error"},
      {0x1559937, "If"},
      {0x1559947, "True"},
      {0x1559957, "False"},
      {0x1559967, "Pi"},
      {0x1559977, "Pi180"},
      {0x1559987, "Rand"},
      {0x1559997, "And"},
      {0x15599a7, "Or"},
      {0x15599c7, "IsOdd"},
      {0x15599d7, "IsEven"},
      {0x15599e7, "Sin"},
      {0x15599f7, "Cos"},
      {0x1559a07, "Tan"},
      {0x1559a17, "Mod"},
      {0x1559a27, "SpecialIf"},
      {0x1559a37, "NA"},
      {0x1559a47, "Frac"},
      {0x1559a87, "ASin"}, // ArcSin
      {0x1559a97, "ACos"}, // ArcCos
      {0x1559aa7, "ATan"}, // ArcTan
      {0x1559b07, "Degrees"},
      {0x1559b17, "Radians"},
      {0x1559b27, "Exp"},
      {0x1559b37, "Ln"},
      {0x1559b47, "Log2"},
      {0x1559b57, "Log10"},
      {0x1559b67, "Log"},
      {0x1559b77, "Exp1"},
      {0x1559b87, "Ln1"},
      {0x1559bc7, "Sum"},
      {0x1559bd7, "SumSqr"}, // SqrSum
      {0x1559be7, "SumProduct"},
      {0x1559bf7, "SumXPY2"},
      {0x1559c07, "SumXMY2"},
      {0x1559c17, "SumX2PY2"},
      {0x1559c27, "SumX2MY2"},
      {0x1559c37, "Count"},
      {0x1559c47, "Average"},
      {0x1559c57, "StDev"},
      {0x1559c67, "Var"},
      {0x1559c77, "RegressionB"},
      {0x1559c87, "RegressionM"},
      {0x1559c97, "LogRegressionB"},
      {0x1559ca7, "LogRegressionM"},
      {0x1559d07, "Annuity"},
      {0x1559d17, "Compound"},
      {0x1559d27, "FV"},
      {0x1559d37, "NPV"},
      {0x1559d47, "Factorial"},
      {0x1559d57, "Combinations"},
      {0x1559d67, "Permutations"},
      {0x1559dc7, "SetHour"},
      {0x1559dd7, "SetMinute"},
      {0x1559de7, "SetSecond"},
      {0x1559df7, "AddSecond"},
      {0x1559e07, "AddMinute"},
      {0x1559e17, "AddHour"},
      {0x1559e27, "AddDay"},
      {0x1559e37, "Date"}, // ragtime setdate
      {0x1559e47, "AddMonth"},
      {0x1559e57, "AddYear"},
      {0x1559e67, "Second"},
      {0x1559e77, "Month"},
      {0x1559e87, "Hour"},
      {0x1559e97, "DiffSecond"},
      {0x1559ea7, "DiffMinute"},
      {0x1559eb7, "DiffHour"},
      {0x1559ec7, "DiffDay"},
      {0x1559ed7, "DiffDays30"},
      {0x1559ee7, "DiffMonth"},
      {0x1559ef7, "DiffYear"},
      {0x1559f07, "DayOfWeekISO"},
      {0x1559f17, "DayOfWeekUS"},
      {0x1559f27, "WeekOfYearISO"},
      {0x1559f37, "WeekOfYearUS"},
      {0x1559f47, "Now"},
      {0x1559f57, "Today"},
      {0x1559f67, "SetDay"},
      {0x1559f77, "SetMonth"},
      {0x1559f87, "SetYear"},
      {0x1559f97, "DayOfYear"},
      {0x1559fa7, "Second"}, // SecondOf
      {0x1559fb7, "Minute"}, // MinuteOf
      {0x1559fc7, "Hour"}, // HourOf
      {0x1559fd7, "Day"}, // DayOf
      {0x1559fe7, "Month"}, // ragtime monthof
      {0x1559ff7, "YearOf"}, // YearOf
      {0x155a017, "Length"},
      {0x155a027, "Left"},
      {0x155a037, "Right"},
      {0x155a047, "Mid"},
      {0x155a057, "Replace"},
      {0x155a067, "Repeat"},
      {0x155a077, "Concatenate"},
      {0x155a087, "Concatenate"}, // Smart concatenate
      {0x155a097, "Exact"},
      {0x155a0a7, "Code"},
      {0x155a0b7, "Code"}, // WinCode
      {0x155a0c7, "UniCode"},
      {0x155a0d7, "Char"},
      {0x155a0e7, "Char"}, // WinCharx
      {0x155a0f7, "UniChar"},
      {0x155a107, "Clean"},
      {0x155a117, "Trim"},
      {0x155a127, "Lower"},
      {0x155a137, "Upper"},
      {0x155a147, "Proper"},
      {0x155a157, "Small"},
      {0x155a167, "Large"},
      {0x155a177, "Median"},
      {0x155a187, "Percentile"},
      {0x155a197, "Quartile"},
      {0x155a1a7, "Choose"},
      {0x155a1b7, "Find"},
      {0x155a1c7, "Text"},
      {0x155a1d7, "ValueFormat"},
      {0x155a1e7, "Value"},
      {0x155a1f7, "SetDocName"},
      {0x155a207, "DocumentDate"},
      {0x155a217, "DocumentName"},
      {0x155a227, "Date"},
      {0x155a237, "Number"},
      {0x155a247, "TimeSpan"},
      {0x155a257, "SystemCurrency"},
      {0x155a287, "SetTime"},
      {0x155a297, "SetTimeSpan"},
      {0x155a2a7, "Developers"},
      // spreadsheet
      {0x1663817, "Row"},
      {0x1663827, "Column"},
      {0x1663837, "Plane"},
      {0x16638a7, "Search"},
      {0x16638b7, "HSearch"},
      {0x16638c7, "VSearch"},
      {0x16638d7, "LookUp"},
      {0x1663907, "Index"},
      {0x1663917, "Selection"},
      {0x1663947, "CurrentResult"},
      {0x1663967, "CurrentIndex"},
      {0x1663977, "CurrentCell"},
      {0x1663987, "ColumnValue"},
      {0x1663997, "RowValue"},
      {0x16639e7, "SetCell"},
      {0x16639f7, "MailMerge"},
      {0x1663a07, "PrintCycle"},
      {0x1663a17, "PrintStop"},
      // fax
      {0x1be5027, "FaxAddress"},
      {0x1be5037, "FaxAddressRange"},
      // button
      {0x1d50817, "Button"},
      // slide time
      {0x1e16827, "STStart"},
      {0x1e16837, "STStop"},
      {0x1e16847, "STNextPage"},
      {0x1e16857, "STShownPage"},
      {0x1e16867, "STRequestedPage"},
      {0x1e16877, "STLayout"},
      {0x1e16887, "STStartTime"},
      {0x1e16897, "STLastChange"},
      {0x1e168a7, "STUpdate"},
      {0x1e168b7, "STSlideCount"},
      {0x1e168c7, "STPreparePage"},
      {0x1e168d7, "STPreparePage"},
      {0x1e168e7, "STSetPreviousPage"},
      // calendar
      {0x23aa067, "ClJulianDate"},
      {0x23aa077, "ClModJulian"},
      {0x23aa087, "ClNumberToDate"},
      {0x23aa097, "ClDateToNumber"},
      {0x23aa0d7, "ClAddWorkDaysUSA"},
      // serial number
      {0x23af017, "SnGetSerNum"},
      {0x23af027, "SnFillSerNum"},
      // euro
      {0x23b4017, "Euro"},
      {0x23b4027, "EuroRound"},
      {0x23b4037, "EuroCoinRound"},
    };
    for (auto const &it : m_idFunctionMap)
      m_idFunctionSet.insert(it.first);
  }
  //! map id to function name
  std::map<unsigned long, char const *> m_idFunctionMap;
  //! the set of function id
  std::set<unsigned long> m_idFunctionSet;
  //! the document fonction names
  std::vector<std::string> m_documentFunctionNames;

  //! map data id to text zone
  std::map<int, std::shared_ptr<ClusterFormula> > m_idFormulaMap;
};

//! Internal: the helper to read function name
struct FunctionNameParser final : public RagTime5StructManager::FieldParser {
  //! constructor
  explicit FunctionNameParser(std::map<unsigned long, char const *> const &idFunctionMap, std::vector<std::string> &functionNames)
    : RagTime5StructManager::FieldParser("FunctionName")
    , m_idFunctionMap(idFunctionMap)
    , m_functionNames(functionNames)
  {
  }
  //! destructor
  ~FunctionNameParser() final;
  //! parse a field
  bool parseField(RagTime5StructManager::Field &field, RagTime5Zone &/*zone*/, int n, libmwaw::DebugStream &f) final
  {
    bool ok=false;
    if (m_functionNames.size()<=size_t(n))
      m_functionNames.resize(size_t(n+1));
    if (field.m_type==RagTime5StructManager::Field::T_FieldList &&
        field.m_fieldList.size()==1) {
      auto const &child=field.m_fieldList[0];
      if (child.m_type==RagTime5StructManager::Field::T_FieldList &&
          child.m_name=="func[name]" && child.m_fieldList.size()==2) {
        ok=true;

        auto const &it=m_idFunctionMap.find(child.m_fileType);
        if (it!=m_idFunctionMap.end()) {
          m_functionNames[size_t(n)]=it->second;
          f << it->second << ",";
        }
        for (auto const &c : child.m_fieldList) {
          if (c.m_type==RagTime5StructManager::Field::T_Unicode)
            f << c.m_string.cstr() << ",";
          else
            f << "[" << c << "]";
        }
        f << child.m_extra;
      }
    }
    if (!ok) {
      MWAW_DEBUG_MSG(("RagTime5FormulaInternal::FunctionNameParser::parseField: find unexpected field\n"));
      f << "###" << field;
    }
    return true;
  }

protected:
  //! map id to function name
  std::map<unsigned long, char const *> const &m_idFunctionMap;
  //! the fonction names
  std::vector<std::string> &m_functionNames;
};

FunctionNameParser::~FunctionNameParser()
{
}

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
RagTime5Formula::RagTime5Formula(RagTime5Document &doc)
  : m_document(doc)
  , m_structManager(m_document.getStructManager())
  , m_parserState(doc.getParserState())
  , m_state(new RagTime5FormulaInternal::State)
{
}

RagTime5Formula::~RagTime5Formula()
{
}

int RagTime5Formula::version() const
{
  return m_parserState->m_version;
}

std::set<unsigned long> const &RagTime5Formula::getFunctionsId() const
{
  return m_state->m_idFunctionSet;
}

bool RagTime5Formula::readFunctionNames(RagTime5ClusterManager::Link const &link)
{
  if (link.empty())
    return true;
  RagTime5FormulaInternal::FunctionNameParser defaultParser(m_state->m_idFunctionMap, m_state->m_documentFunctionNames);
  return m_document.readStructZone(link, defaultParser, 0);
}

bool RagTime5Formula::readFormulaClusters(RagTime5ClusterManager::Link const &link, int sheetId)
{
  if (link.m_ids.size()!=2) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaClusters: call with bad ids\n"));
    return false;
  }
  for (size_t i=0; i<2; ++i) {  // formuladef and formulapos
    if (!link.m_ids[i]) continue;
    auto data=m_document.getDataZone(link.m_ids[i]);
    if (!data || data->m_isParsed || data->getKindLastPart(data->m_kinds[1].empty())!="Cluster") {
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaClusters: the child cluster id %d seems bad\n", link.m_ids[i]));
      continue;
    }
    int zoneType=0x20000+int(i);
    std::shared_ptr<RagTime5ClusterManager::Cluster> cluster;
    if (!m_document.getClusterManager()->readCluster(*data, cluster, zoneType) || !cluster)
      continue;
    m_document.checkClusterList(cluster->m_clusterIdsList);

    RagTime5ClusterManager::Link const &clustLink=cluster->m_dataLink;
    readFormulaZones(*cluster, clustLink, sheetId, i==0);

    for (auto const &lnk : cluster->m_linksList)
      m_document.readFixedSizeZone(lnk, "FormulaUnknown");
  }
  return true;
}

////////////////////////////////////////////////////////////
//
// Intermediate level
//
////////////////////////////////////////////////////////////

//
// formula
//
bool RagTime5Formula::readFormulaZones(RagTime5ClusterManager::Cluster &/*cluster*/, RagTime5ClusterManager::Link const &link,
                                       int sheetId, bool isDefinition)
{
  if (link.m_ids.size()<2 || !link.m_ids[1])
    return false;

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
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaZones: the data zone %d seems bad\n", dataId));
    return false;
  }

  dataZone->m_isParsed=true;
  MWAWEntry entry=dataZone->m_entry;
  libmwaw::DebugFile &ascFile=dataZone->ascii();
  libmwaw::DebugStream f;
  std::string name(isDefinition ? "FormulaDef" : "FormulaPos");
  f << "Entries(" << name << ")[" << *dataZone << "]:";
  ascFile.addPos(entry.end());
  ascFile.addNote("_");
  ascFile.addPos(entry.begin());
  ascFile.addNote(f.str().c_str());

  MWAWInputStreamPtr input=dataZone->getInput();
  input->setReadInverted(!dataZone->m_hiLoEndian);
  long debPos=entry.begin();
  long endPos=entry.end();

  std::map<int, std::vector<MWAWCellContent::FormulaInstruction> > idToFormulaMap;
  for (int i=0; i<N-1; ++i) {
    long pos=decal[size_t(i)];
    long nextPos=decal[size_t(i+1)];
    if (nextPos==pos) continue;
    if (pos<0 || debPos+nextPos>endPos || pos>nextPos) {
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaZones: can not read the data zone %d-%d seems bad\n", dataId, i));
      if (debPos+pos<endPos) {
        f.str("");
        f << name << "-" << i+1 << ":###";
        ascFile.addPos(debPos+pos);
        ascFile.addNote(f.str().c_str());
      }
      continue;
    }
    input->seek(debPos+pos, librevenge::RVNG_SEEK_SET);
    if (isDefinition) {
      std::vector<MWAWCellContent::FormulaInstruction> formula;
      if (readFormulaDefinition(*dataZone, debPos+nextPos, sheetId, i+1, formula))
        idToFormulaMap[i+1]=formula;
    }
    else if (!readFormulaPosition(*dataZone, debPos+nextPos,i+1)) {
      f.str("");
      f << name << "-" << i+1 << ":";
      ascFile.addPos(debPos+pos);
      ascFile.addNote(f.str().c_str());
    }
  }
  if (!idToFormulaMap.empty() && sheetId>=0) // sheetId==-1 correspond to the document
    m_document.getSpreadsheetParser()->storeFormula(sheetId, idToFormulaMap);
  input->setReadInverted(false);
  return true;
}
namespace RagTime5FormulaInternal
{
struct Functions {
  char const *m_name;
  int m_arity;
};

static Functions const s_listFunctions[] = {
  { "+", 2}, {"-", 2}, {"*", 2}, {"/", 2},
  {nullptr, -2}, {"^", 2}, {"+", 1}, {"-", 1},
  {nullptr, -2}, {nullptr, -2}, {"=", 2}, {"!=", 2},
  {">", 2}, {"<", 2}, {">=", 2}, {"<=", 2},

  {nullptr, -2}, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
  {"AND", 2}, {"OR", 2}, {"NOT", 1}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
  {nullptr, -2}, {"&", 2}/*concat + space*/, {"&", 2}, {"_", 1} /* optional*/,

  {";", 2} /*list of arg*/, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}/*date*/, {nullptr, -2}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}/*double*/, {nullptr, -2}, {nullptr, -2}/*char*/,
  {nullptr, -2}/*short*/, {nullptr, -2}, {nullptr, -2}/*text*/, {nullptr, -2},

  {nullptr, -2}, {nullptr, -2}, {";", 2}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
  {nullptr, -2}, {nullptr, -2}, {nullptr, -2}, {nullptr, -2},
};

static size_t const s_numFunctions=MWAW_N_ELEMENTS(s_listFunctions);

static char const *s_listFunctions2[] = {
  "^", "*", "/", nullptr, "+", "-", "&", "&", /*concat+space*/ // 80-9c
  "=", "!=", ">", "<", ">=", "<=", "AND", "OR", // a0-bc
  "NOT" // c0
};
}

bool RagTime5Formula::readFormula(MWAWInputStreamPtr &input,
                                  std::vector<MWAWCellContent::FormulaInstruction> &formula,
                                  long const(&limitPos)[5], std::vector<std::string> const &functions, std::vector<MWAWCellContent::FormulaInstruction> const &cells,
                                  libmwaw::DebugStream &f1) const
{
  formula.clear();
  long pos=input->tell();
  long const endFormula=limitPos[1];
  long const endFormula2=limitPos[2];
  if (pos>=endFormula || !input->checkPosition(endFormula) || !input->checkPosition(endFormula2)) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormula: the zone seems too short\n"));
    return false;
  }
  size_t numFuncs=functions.size();
  size_t numCells=cells.size();
  struct StackType {
    enum Type { S_Constant, S_Function, S_Operator, S_Operator1 };
    explicit StackType(Type type=S_Constant, std::string const &op="")
      : m_type(type)
      , m_operator(op)
    {
    }
    Type m_type;
    std::string m_operator;
  };
  std::vector<std::vector<MWAWCellContent::FormulaInstruction> > stack;
  std::vector<StackType> stackType;
  bool ok=false;
  bool firstOptionalParam=false;
  std::vector<bool> firstOptionalParamStack;
  libmwaw::DebugStream f;
  while (input->tell()<endFormula) {
    auto code=input->readULong(1);
    pos=input->tell();
    MWAWCellContent::FormulaInstruction instr;
    bool noneInstr=false;
    int arity=0;
    switch (code) {
    case 0x25: { // fixme
      if (pos+8>endFormula)
        break;
      double res;
      bool isNan;
      if (input->readDouble8(res, isNan)) {
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Double;
        instr.m_doubleValue=res;
        f << "DT=" << res << ",";
        ok=true;
      }
      break;
    }
    case 0x26: { // fixme
      if (pos+8>endFormula)
        break;
      double res;
      bool isNan;
      if (input->readDouble8(res, isNan)) {
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Double;
        instr.m_doubleValue=res;
        f << "H=" << res << ",";
        ok=true;
      }
      break;
    }
    case 0x29: {
      if (pos+8>endFormula)
        break;
      double res;
      bool isNan;
      if (input->readDouble8(res, isNan)) {
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Double;
        instr.m_doubleValue=res;
        f << res << ",";
        ok=true;
      }
      break;
    }
    case 0x2b:
      if (pos+1>endFormula)
        break;
      ok=true;
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Long;
      instr.m_longValue=double(input->readULong(1));
      f << instr.m_longValue << ",";
      break;
    case 0x2c:
      if (pos+2>endFormula)
        break;
      ok=true;
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Long;
      instr.m_longValue=double(input->readULong(2));
      f << instr.m_longValue << ",";
      break;
    case 0x2e: { // string
      if (pos+2>endFormula)
        break;
      int decal=int(input->readULong(2));
      if (pos+decal<endFormula||pos+1+decal>endFormula2)
        break;
      input->seek(pos+decal,librevenge::RVNG_SEEK_SET);
      int len=int(input->readULong(1));
      if (pos+decal<endFormula||pos+1+decal+len>endFormula2)
        break;
      ok=true;
      std::string text;
      for (int c=0; c<len; ++c) text+=char(input->readULong(1));
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
      instr.m_content=text;
      f << text << ",";
      input->seek(pos+2,librevenge::RVNG_SEEK_SET);
      break;
    }
    case 0x2f: { // unicode string
      if (pos+2>endFormula)
        break;
      int decal=int(input->readULong(2));
      if (pos+decal<endFormula||pos+2+decal>endFormula2)
        break;
      input->seek(pos+decal,librevenge::RVNG_SEEK_SET);
      int len=int(input->readULong(2));
      if (pos+decal<endFormula||pos+decal+2+2*len>endFormula2)
        break;
      ok=true;
      librevenge::RVNGString text;
      for (int c=0; c<len; ++c) libmwaw::appendUnicode(uint32_t(input->readULong(2)), text);
      f << text.cstr() << ",";
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
      if (text.cstr())
        instr.m_content=text.cstr();
      input->seek(pos+2,librevenge::RVNG_SEEK_SET);
      break;
    }
    case 0x30: // reference id at end
    case 0x37: // reference to data, difference with 0x30?
    case 0x3a: { // reference id at end (+copy format)
      if (pos+1>endFormula)
        break;
      ok=true;
      auto id=input->readULong(1);
      if (id==0 || id>numCells) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormula: sorry, find unexpected cell id\n"));
        f << "##C" << id << ",";
      }
      else {
        instr=cells[id-1];
        f << cells[id-1] << ",";
      }
      break;
    }
    case 0x33: { // begins of parameters (+deplacement)
      if (pos+2>endFormula)
        break;
      noneInstr=ok=true;
      auto decal=input->readULong(2); // in pos+decal the end of }, ie 0x35
      firstOptionalParamStack.push_back(firstOptionalParam);
      firstOptionalParam=true;
      f << "{" << std::hex << decal << std::dec << ",";
      break;
    }
    case 0x34: // function id at end
    case 0x35: { // function id + deplacement
      if (pos+(code==0x34 ? 2 : 4)>endFormula)
        break;
      arity=int(input->readULong(1));
      auto id=input->readULong(1);
      if (code==0x35)
        f << "}";
      if (id==0 || id>numFuncs) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormula: sorry, find unexpected function\n"));
        f << "##F" << id;
      }
      else {
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Function;
        instr.m_content=functions[id-1];
        f << functions[id-1];
      }
      if (arity)
        f << ":" << arity;
      if (code==0x34) {
        ok=true;
        f << ",";
        break;
      }
      int N=int(input->readLong(2));
      if (pos+4+2*N>=endFormula)
        break;
      if (!firstOptionalParamStack.empty()) {
        firstOptionalParam=firstOptionalParamStack.back();
        firstOptionalParamStack.pop_back();
      }
      else
        firstOptionalParam=false;
      ok=true;
      if (N)
        ++arity;
      // in pos+4+value: the beginning of each parameters
      f << "[";
      for (int i=0; i<N; ++i)
        f << input->readLong(2) << ":";
      f << "],";
      break;
    }
    case 0x39: // happens in Button("toto", True) for true + dec{beg:end}
      if (pos+4>endFormula)
        break;
      noneInstr=ok=true;
      f << "Action" << std::hex << input->readULong(2) << "-" << input->readULong(2) << std::dec << ",";
      break;
    default:
      if (code<RagTime5FormulaInternal::s_numFunctions && RagTime5FormulaInternal::s_listFunctions[code].m_name) {
        auto const &func=RagTime5FormulaInternal::s_listFunctions[code];
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
        instr.m_content=func.m_name;
        f << func.m_name << ",";
        arity=func.m_arity;
        ok=true;
        break;
      }
      break;
    }
    if (!ok)
      break;
    if (noneInstr)
      continue;
    if (instr.m_type!=MWAWCellContent::FormulaInstruction::F_Function && instr.m_type!=MWAWCellContent::FormulaInstruction::F_Operator) {
      stack.push_back(std::vector<MWAWCellContent::FormulaInstruction>(1,instr));
      stackType.push_back(StackType());
      continue;
    }
    size_t numElt = stack.size();
    if (instr.m_type==MWAWCellContent::FormulaInstruction::F_Operator && instr.m_content==";") {
      if (firstOptionalParam) {
        firstOptionalParam=false;
        continue;
      }
      if (static_cast<int>(numElt)<arity && input->tell()>=endFormula) {
        arity=1;
        instr.m_content="=";
      }
    }
    if (static_cast<int>(numElt) < arity) {
      f << "###";
      ok = false;
      continue;
    }
    std::vector<MWAWCellContent::FormulaInstruction> child;
    if ((instr.m_content[0] >= 'A' && instr.m_content[0] <= 'Z') || instr.m_type==MWAWCellContent::FormulaInstruction::F_Function) {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Function;
      child.push_back(instr);
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      instr.m_content="(";
      child.push_back(instr);
      for (int i = 0; i < arity; i++) {
        if (i) {
          instr.m_content=";";
          child.push_back(instr);
        }
        auto const &node=stack[size_t(static_cast<int>(numElt)-arity+i)];
        child.insert(child.end(), node.begin(), node.end());
      }
      instr.m_content=")";
      child.push_back(instr);

      size_t newSize=size_t(static_cast<int>(numElt)-arity+1);
      stack.resize(newSize);
      stack[newSize-1] = child;
      stackType.resize(newSize);
      stackType[newSize-1] = StackType(StackType::S_Function);
      continue;
    }
    if (arity==1) {
      if (instr.m_content=="_") continue;
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      bool needAddPara=stack[numElt-1].size()!=1 && stackType[numElt-1].m_type==StackType::S_Operator && input->tell()!=endFormula;
      stack[numElt-1].insert(stack[numElt-1].begin(), instr);
      stackType[numElt-1] = StackType(StackType::S_Operator1, instr.m_content);
      if (needAddPara) {
        instr.m_content="(";
        stack[numElt-1].insert(stack[numElt-1].begin()+1, instr);
        instr.m_content=")";
        stack[numElt-1].push_back(instr);
      }
      continue;
    }
    if (arity==2) {
      instr.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
      bool needAddPara=stack[numElt-2].size()!=1 && instr.m_content!=";" && stackType[numElt-2].m_type==StackType::S_Operator && stackType[numElt-2].m_operator!=instr.m_content;
      if (needAddPara) {
        MWAWCellContent::FormulaInstruction instr2;
        instr2.m_type=MWAWCellContent::FormulaInstruction::F_Operator;
        instr2.m_content="(";
        stack[numElt-2].insert(stack[numElt-2].begin(), instr2);
        instr2.m_content=")";
        stack[numElt-2].push_back(instr2);
      }
      stack[numElt-2].push_back(instr);
      stackType[numElt-2] = StackType(StackType::S_Operator, instr.m_content);
      needAddPara=stack[numElt-1].size()!=1 && instr.m_content!=";" && stackType[numElt-1].m_type==StackType::S_Operator;
      if (needAddPara) {
        instr.m_content="(";
        stack[numElt-2].push_back(instr);
      }
      stack[numElt-2].insert(stack[numElt-2].end(), stack[numElt-1].begin(), stack[numElt-1].end());
      if (needAddPara) {
        instr.m_content=")";
        stack[numElt-2].push_back(instr);
      }
      stack.resize(numElt-1);
      stackType.resize(numElt-1);
      continue;
    }
    ok=false;
    f << "### unexpected arity[" << instr << "]";
    break;
  }
  if (!ok || stack.size()!=1 || stack[0].size()<2 || stack[0][0].m_content!="=" || input->tell()!=endFormula) {
    f1 << f.str();
    f1 << "###[";
    for (auto const &i : stack) {
      f1 << "[";
      for (auto const &j : i)
        f1 << j << ",";
      f1 << "]";
    }
    f1 << "],";
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormula: sorry, can not read a formula\n"));
    return false;
  }

  f1 << "[";
  for (auto const &j : stack[0])
    f1 << j << ",";
  f1 << "],";
  formula.insert(formula.begin(),stack[0].begin()+1, stack[0].end());
  return true;
}

bool RagTime5Formula::readFormula2(MWAWInputStreamPtr &input, long const(&limitPos)[5],
                                   std::vector<std::string> const &functions, std::vector<MWAWCellContent::FormulaInstruction> const &cells,
                                   libmwaw::DebugStream &f) const
{
  long pos=input->tell();
  long const endFormula=limitPos[2];
  if (pos>=endFormula) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormula2: the zone seems too short\n"));
    return false;
  }
  f << "form2=";
  size_t numFuncs=functions.size();
  size_t numCells=cells.size();
  while (input->tell()<endFormula) {
    auto code=input->readULong(1);
    bool ok=false;
    pos=input->tell();
    switch (code) {
    case 0:
      f << "1";
      ok=true;
      break;
    case 1:
      if (pos+1>endFormula)
        break;
      f << input->readULong(1);
      ok=true;
      break;
    case 2:
      if (pos+2>endFormula)
        break;
      f << input->readULong(2);
      ok=true;
      break;
    case 5:
    case 6: // date
    case 7: { // time
      if (pos+8>endFormula)
        break;
      double res;
      bool isNan;
      if (input->readDouble8(res, isNan)) {
        if (code==6)
          f << "D[" << res << "]";
        else if (code==7)
          f << "T[" << res << "]";
        else
          f << res;
        ok=true;
      }
      break;
    }
    case 0xc: {
      if (pos+1>endFormula)
        break;
      int len=int(input->readULong(1));
      if (pos+1+len>endFormula)
        break;
      std::string texte;
      for (int c=0; c<len; ++c) texte+=char(input->readULong(1));
      f << texte;
      ok=true;
      break;
    }
    case 0x10: { // unicode text
      if (pos+2>endFormula)
        break;
      int len=int(input->readULong(2));
      if (pos+2+2*len>endFormula)
        break;
      std::string texte;
      for (int c=0; c<len; ++c) texte+=char(input->readULong(2));
      f << texte;
      ok=true;
      break;
    }
    case 0x20: { // cell id
      if (pos+1>endFormula)
        break;
      size_t id=size_t(input->readULong(1));
      if (id==0 || id>numCells) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormula2: sorry, find unexpected cells\n"));
        f << "##C" << id;
      }
      else
        f << cells[id-1];
      ok=true;
      break;
    }
    case 0x24: {
      if (pos+1>endFormula)
        break;
      size_t id=size_t(input->readULong(1));
      if (id==0 || id>numFuncs) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormula2: sorry, find unexpected function\n"));
        f << "##F" << id;
      }
      else
        f << functions[id-1];
      ok=true;
      break;
    }
    case 0x40:
      f << "(";
      ok=true;
      break;
    case 0x44:
      f << ")";
      ok=true;
      break;
    case 0x49:
      f << ";";
      ok=true;
      break;
    case 0x54:
      f << "%";
      ok=true;
      break;
    default:
      if ((code%4) || (code<0x80||code>0xc0) || !RagTime5FormulaInternal::s_listFunctions2[code/4-0x20])
        break;
      f << RagTime5FormulaInternal::s_listFunctions2[code/4-0x20];
      ok=true;
      break;
    }
    if (!ok) {
      f << "###";
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormula2: sorry, unknown code=%lx\n", code));
      return false;
    }
  }
  f << ",";
  return true;
}

bool RagTime5Formula::readFormulaDefinition(RagTime5Zone &zone, long endPos, int sheetId, int n,
    std::vector<MWAWCellContent::FormulaInstruction> &formula)
{
  MWAWInputStreamPtr input=zone.getInput();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "FormulaDef-FD" << n << ":";
  if (pos+6>endPos) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the zone seems too short\n"));
    f<<"###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  long limitPos[5]= {6,0,0,0,int(endPos-pos)};
  for (int i=1; i<4; ++i) {
    limitPos[i]=static_cast<long>(input->readULong(2));
    if (limitPos[i]==0) continue;
    if (limitPos[i]&0x8000) {
      f << "fl" << i << ",";
      limitPos[i] &= 0x7FFF;
    }
    if (limitPos[i]<6 || pos+limitPos[i]>=endPos) {
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the %d pointer seems bad\n", i));
      f << "##limitPos[" << i << "]=" << limitPos[i] << ",";
      limitPos[i]=0;
      continue;
    }
  }
  for (int i=3; i>=1; --i) {
    if (!limitPos[i])
      limitPos[i]=limitPos[i+1];
  }
  for (auto &d : limitPos) d += pos;

  // first retrieve the function names
  std::vector<std::string> functions;
  bool functionsOk=true;
  if (((limitPos[4]-limitPos[3])%2) || limitPos[3]>limitPos[4]) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the function's name zone seems bad\n"));
    f << "###function,";
    ascFile.addDelimiter(limitPos[3],'|');
  }
  else if (limitPos[3]<limitPos[4]) {
    input->seek(limitPos[3], librevenge::RVNG_SEEK_SET);
    size_t N=size_t((limitPos[4]-limitPos[3])/2);
    functions.resize(N);
    size_t numDocFunc=m_state->m_documentFunctionNames.size();
    for (size_t i=0; i<N; ++i) {
      size_t id=size_t(input->readULong(2));
      if (id>=numDocFunc) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the function's name zone seems bad\n"));
        f << "###F" << id << ",";
        functionsOk=true;
      }
      else {
        functions[i]=m_state->m_documentFunctionNames[id];
        if (m_state->m_documentFunctionNames[id].empty())
          functionsOk=false;
      }
    }
    ascFile.addDelimiter(limitPos[3],'|');
  }
  // now retrieve the cells
  std::vector<MWAWCellContent::FormulaInstruction> cells;
  bool cellsOk=true;
  if (((limitPos[3]-limitPos[2])%4) || limitPos[2]>limitPos[3]) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the cell's zone seems bad\n"));
    f << "###cells,";
    ascFile.addDelimiter(limitPos[2],'|');
  }
  else if (limitPos[2]<limitPos[3]) {
    auto const &sheetManager=*m_document.getSpreadsheetParser();
    input->seek(limitPos[2], librevenge::RVNG_SEEK_SET);
    long endDataPos=limitPos[3];
    f << "cells=[";
    while (!input->isEnd()) {
      long begDataPos=input->tell();
      if (begDataPos==endDataPos) break;
      if (begDataPos+4>endDataPos) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: problem with length for cells' zone\n"));
        f << "###end,";
        break;
      }
      auto lVal=long(input->readULong(4));
      int type=int(lVal>>24);
      lVal=(lVal&0xFFFFFF);
      MWAWCellContent::FormulaInstruction instr;
      std::stringstream s;
      if (type==3) {
        if (sheetManager.getFormulaRef(sheetId, int(lVal), instr))
          f << instr << ",";
        else {
          s << "##RP" << std::hex << lVal << std::dec;
          instr.m_content=s.str();
          instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
          f << "#" << instr.m_content << ",";
          cellsOk=false;
        }
        cells.push_back(instr);
        continue;
      }
      input->seek(begDataPos, librevenge::RVNG_SEEK_SET);
      std::vector<int> listIds;
      if (begDataPos+8>endDataPos || !m_structManager->readDataIdList(input, 1, listIds)) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: can not read data for cells zone\n"));
        f << "#type=" << std::hex << lVal << std::dec << ",";
        break;
      }
      if (listIds[0]) // some cluster data
        f << m_document.getClusterManager()->getClusterDebugName(listIds[0]);
      lVal=long(input->readULong(4));
      type=int(lVal>>24);
      lVal=(lVal&0xFFFFFF);
      if (type==3) {
        if (sheetManager.getFormulaRef(listIds[0], int(lVal), instr))
          f << instr << ",";
        else {
          s << "##RP" << std::hex << lVal << std::dec;
          instr.m_content=s.str();
          instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
          f << "##" << instr.m_content << ",";
          cellsOk=false;
        }
      }
      else if ((type&0xef)==0) { // 0 or 10
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: reference to button is not implemented\n"));
          first=false;
        }
        if (lVal==0x2a01)
          f << "Button,";
        else
          f << "#Button=" << std::hex << lVal << std::dec << ",";
        s << "#Button" << listIds[0];
        instr.m_content=s.str();
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
        cellsOk=false;
      }
      else {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: unknown cells type\n"));
        s << "Unknown" << std::hex << lVal << std::dec << "[" << type << "],";
        instr.m_content=s.str();
        instr.m_type=MWAWCellContent::FormulaInstruction::F_Text;
        f << "###" << s.str() << ",";
        cellsOk=false;
      }
      if (cellsOk && instr.m_type==MWAWCellContent::FormulaInstruction::F_CellList &&
          (instr.m_position[0][0]>instr.m_position[1][0] || instr.m_position[0][1]>instr.m_position[1][1])) {
        cellsOk=false;
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: find some invalid cells\n"));
          first=false;
        }
      }
      cells.push_back(instr);
    }
    f << "],";
  }
  bool formulaOk=false;
  for (int i=0; i<2; ++i) {
    if (limitPos[i+1]==limitPos[i])
      continue;
    if (limitPos[i+1]<limitPos[i]) {
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the %d pointer seems bad\n", i));
      f << "##decal" << i << ",";
      continue;
    }
    switch (i) {
    case 0: {
      if (limitPos[i+1]-limitPos[i]<8) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: the zone 0 size seems bad\n"));
        f << "##decal2,";
        break;
      }
      input->seek(limitPos[i], librevenge::RVNG_SEEK_SET);
      auto val=static_cast<int>(input->readLong(2)); // always 0?
      if (val) f<< "#f0=" << val << ",";
      auto lVal=input->readULong(4);
      int type=int(lVal>>16);
      int id=int(lVal&0xffff);
      if ((type!=1 && type!=0x100) && (id==1||id==0x100)) {
        static bool first=true;
        if (first) {
          MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: orderings seems bad\n"));
          first=false;
        }
        lVal=((lVal&0xff)<<24)|((lVal&0xff00)<<8)|(lVal>>16);
        type=int(lVal>>16);
        id=int(lVal&0xffff);
      }
      f << "id=" << id << ",";
      f << "type=" << type << ",";
      if (type==1 && input->tell()+4<limitPos[i+1]) {
        auto type2=input->readULong(4);
        if (type2) f << "type2=" << printType(type2) << ",";
      }
      else if (type!=256) {
        MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaDefinition: unexpected type\n"));
        f << "##type,";
      }
      if (input->tell()!=limitPos[i+1]) {
        f << "hasForm,";
        ascFile.addDelimiter(input->tell(),'|');
        if (!readFormula(input, formula, limitPos, functions, cells, f)) {
          f << "###";
          ascFile.addDelimiter(input->tell(),'@');
        }
        else
          formulaOk=true;
      }
      ascFile.addDelimiter(limitPos[i+1],'|');
      break;
    }
    case 1:
      input->seek(limitPos[i], librevenge::RVNG_SEEK_SET);
      if (input->tell()!=limitPos[i+1]) {
        ascFile.addDelimiter(input->tell(),'|');
        if (!readFormula2(input, limitPos, functions, cells, f)) {
          f << "###";
          ascFile.addDelimiter(input->tell(),'@');
        }
      }
      ascFile.addDelimiter(limitPos[i+1],'|');
      break;
    default:
      break;
    }
  }
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return functionsOk && cellsOk && formulaOk;
}

bool RagTime5Formula::readFormulaPosition(RagTime5Zone &zone, long endPos, int n)
{
  MWAWInputStreamPtr input=zone.getInput();
  long pos=input->tell();
  libmwaw::DebugFile &ascFile=zone.ascii();
  libmwaw::DebugStream f;
  f << "FormulaPos-" << n << ":";
  if ((endPos-pos)%8) {
    MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaPosition: the zone seems bad\n"));
    f<<"###";
    ascFile.addPos(pos);
    ascFile.addNote(f.str().c_str());
    return false;
  }
  auto N=int((endPos-pos)/8);
  f << "cluster=[";
  for (int i=0; i<N; ++i) {
    long actPos=input->tell();
    std::vector<int> listIds; // the cluster which contains the definition
    if (!m_structManager->readDataIdList(input, 1, listIds)) {
      MWAW_DEBUG_MSG(("RagTime5Formula::readFormulaPosition: find unknown block type\n"));
      f << "##type,";
      input->seek(actPos+8, librevenge::RVNG_SEEK_SET);
      continue;
    }
    auto id=input->readULong(4);
    if (listIds[0]==0) f << "_,";
    else if (id&0xc0000000) f << "data" << listIds[0] << "A-FD" << (id&0x3fffffff) << "[" << (id>>30) << ",]";
    else f << "data" << listIds[0] << "A-FD" << (id&0x3fffffff) << ",";
  }
  f << "],";
  ascFile.addPos(pos);
  ascFile.addNote(f.str().c_str());
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
