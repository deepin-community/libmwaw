/* -*- Mode: C++; c-default-style: "k&r"; indent-tabs-mode: nil; tab-width: 2; c-basic-offset: 2 -*- */

/* libmwaw: tools
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>

#include "file_internal.h"
#include "input.h"
#include "ole.h"
#include "rsrc.h"
#include "xattr.h"

#include <sys/stat.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "UNKNOWN VERSION"
#endif

namespace libmwaw_tools
{
class Exception
{
};

struct File {
  //! the constructor
  explicit File(char const *path)
    : m_fName(path ? path : "")
    , m_fInfoCreator("")
    , m_fInfoType("")
    , m_fInfoResult("")
    , m_fileVersion()
    , m_appliVersion()
    , m_rsrcMissingMessage("")
    , m_rsrcResult("")
    , m_dataResult()
    , m_printFileName(false)
  {
    if (m_fName.empty()) {
      std::cerr << "File::File: call without path\n";
      throw libmwaw_tools::Exception();
    }

    // check if it is a regular file
    struct stat status;
    if (!path || stat(path, &status) == -1) {
      std::cerr << "File::File: the file " << m_fName << " cannot be read\n";
      throw libmwaw_tools::Exception();
    }
    if (!S_ISREG(status.st_mode)) {
      std::cerr << "File::File: the file " << m_fName << " is a not a regular file\n";
      throw libmwaw_tools::Exception();
    }
  }

  //! operator<<
  friend std::ostream &operator<<(std::ostream &o, File const &info)
  {
    if (info.m_printFileName)
      o << info.m_fName << ":\n";
    if (info.m_fInfoCreator.length() || info.m_fInfoType.length()) {
      o << "------- fileInfo -------\n";
      if (info.m_fInfoCreator.length())
        o << "\tcreator=" << info.m_fInfoCreator << "\n";
      if (info.m_fInfoType.length())
        o << "\ttype=" << info.m_fInfoType << "\n";
      if (info.m_fInfoResult.length())
        o << "\t\t=>" << info.m_fInfoResult << "\n";
    }
    if (info.m_fileVersion.ok() || info.m_appliVersion.ok() ||
        info.m_rsrcMissingMessage.length() || info.m_rsrcResult.length()) {
      o << "------- resource fork -------\n";
      if (info.m_fileVersion.ok())
        o << "\tFile" << info.m_fileVersion << "\n";
      if (info.m_appliVersion.ok())
        o << "\tAppli" << info.m_appliVersion << "\n";
      if (info.m_rsrcMissingMessage.length())
        o << "\tmissingString=\"" << info.m_rsrcMissingMessage << "\"\n";
      if (info.m_rsrcResult.length())
        o << "\t\t=>" << info.m_rsrcResult << "\n";
    }
    if (info.m_dataResult.size()) {
      o << "------- data fork -------\n";
      for (auto const &res : info.m_dataResult)
        o << "\t\t=>" << res << "\n";
    }
    return o;
  }

  //! try to read the file information
  bool readFileInformation();
  //! try to read the data fork
  bool readDataInformation();
  //! try to read the resource version data
  bool readRSRCInformation();

  //! can type the file
  bool canPrintResult(int verbose) const
  {
    if (m_fInfoResult.length() || m_dataResult.size() || m_rsrcResult.length()) return true;
    if (verbose <= 0) return false;
    if (m_fInfoCreator.length() || m_fInfoType.length()) return true;
    if (verbose <= 1) return false;
    return m_fileVersion.ok() || m_appliVersion.ok();
  }
  //! print the file type
  bool printResult(std::ostream &o, int verbose) const;

  bool checkFInfoType(char const *type, char const *result)
  {
    if (m_fInfoType != type) return false;
    m_fInfoResult=result;
    return true;
  }
  bool checkFInfoType(char const *result)
  {
    m_fInfoResult=result;
    if (m_fInfoType=="AAPL")
      m_fInfoResult+="[Application]";
    else if (m_fInfoType=="AIFF" || m_fInfoType=="AIFC")
      m_fInfoResult+="[sound]";
    else
      m_fInfoResult+="["+m_fInfoType+"]";
    return true;
  }
  bool checkFInfoCreator(char const *result)
  {
    m_fInfoResult=result;
    if (m_fInfoCreator.length())
      m_fInfoResult+="["+m_fInfoCreator+"]";
    return true;
  }

  //! the file name
  std::string m_fName;
  //! the file info creator
  std::string m_fInfoCreator;
  //! the file info type
  std::string m_fInfoType;
  //! the result of the finfo
  std::string m_fInfoResult;

  //! the file version (extracted from the resource fork )
  RSRC::Version m_fileVersion;
  //! the application version (extracted from the resource fork )
  RSRC::Version m_appliVersion;
  //! the application missing message
  std::string m_rsrcMissingMessage;
  //! the result of the resource fork
  std::string m_rsrcResult;

  //! the result of the data analysis
  std::vector<std::string> m_dataResult;

  //! print or not the filename
  bool m_printFileName;
};

bool File::readFileInformation()
{
  if (!m_fName.length())
    return false;

  XAttr xattr(m_fName.c_str());
  std::unique_ptr<InputStream> input{xattr.getStream("com.apple.FinderInfo")};
  if (!input) return false;

  if (input->length() < 8) {
    return false;
  }

  input->seek(0, libmwaw_tools::InputStream::SK_SET);
  m_fInfoType = "";
  for (int i = 0; i < 4; i++) {
    char c= input->read8();
    if (c==0) break;
    m_fInfoType+= c;
  }
  m_fInfoCreator="";
  for (int i = 0; i < 4; i++) {
    char c= input->read8();
    if (c==0) break;
    m_fInfoCreator+= c;
  }

  if (m_fInfoCreator=="" || m_fInfoType=="")
    return true;
  if (m_fInfoCreator=="2CTY") {
    checkFInfoType("SPUB", "PublishIt") || checkFInfoType("PublishIt");
  }
  else if (m_fInfoCreator=="AB65") {
    checkFInfoType("AD65", "Pagemaker6.5") ||
    checkFInfoType("AT65", "Pagemaker6.5[template]") ||
    checkFInfoType("Pagemaker6.5");
  }
  else if (m_fInfoCreator=="ACTA") {
    checkFInfoType("OTLN", "Acta") || checkFInfoType("otln", "Acta") || checkFInfoType("Acta");
  }
  else if (m_fInfoCreator=="ALB3") {
    checkFInfoType("ALD3", "Pagemaker3") || checkFInfoType("Pagemaker3");
  }
  else if (m_fInfoCreator=="ALB4") {
    checkFInfoType("ALD4", "Pagemaker4") || checkFInfoType("Pagemaker4");
  }
  else if (m_fInfoCreator=="ALB5") {
    checkFInfoType("ALD5", "Pagemaker5") || checkFInfoType("Pagemaker5");
  }
  else if (m_fInfoCreator=="ALB6") {
    checkFInfoType("ALD6", "Pagemaker6") || checkFInfoType("Pagemaker6");
  }
  else if (m_fInfoCreator=="AOqc") {
    checkFInfoType("TEXT","America Online") || checkFInfoType("ttro","America Online[readOnly]") ||
    checkFInfoType("America Online");
  }
  else if (m_fInfoCreator=="AOS1") {
    checkFInfoType("TEXT","eWorld") || checkFInfoType("ttro","eWorld[readOnly]") ||
    checkFInfoType("eWorld");
  }
  else if (m_fInfoCreator=="APBP") {
    checkFInfoType("APBL", "Drawing Table") || checkFInfoType("Drawing Table");
  }
  else if (m_fInfoCreator=="ARTX") {
    checkFInfoType("RIFF","Corel Painter X") || checkFInfoType("Corel Painter X");
  }
  else if (m_fInfoCreator=="BOBO") {
    checkFInfoType("CWDB","ClarisWorks/AppleWorks[Database]")||
    checkFInfoType("CWD2","ClarisWorks/AppleWorks 2.0-3.0[Database]")||
    checkFInfoType("sWDB","ClarisWorks/AppleWorks 2.0-3.0[Database]")||
    checkFInfoType("CWGR","ClarisWorks/AppleWorks[Draw]")||
    checkFInfoType("sWGR","ClarisWorks/AppleWorks 2.0-3.0[Draw]")||
    checkFInfoType("CWSS","ClarisWorks/AppleWorks[SpreadSheet]")||
    checkFInfoType("CWS2","ClarisWorks/AppleWorks 2.0-3.0[SpreadSheet]")||
    checkFInfoType("sWSS","ClarisWorks/AppleWorks 2.0-3.0[SpreadSheet]")||
    checkFInfoType("CWPR","ClarisWorks/AppleWorks[Presentation]")||
    checkFInfoType("CWPT","ClarisWorks/AppleWorks[Paint]")||
    checkFInfoType("CWWP","ClarisWorks/AppleWorks")||
    checkFInfoType("CWW2","ClarisWorks/AppleWorks 2.0-3.0")||
    checkFInfoType("sWWP","ClarisWorks/AppleWorks 2.0-3.0")||
    checkFInfoType("ClarisWorks/AppleWorks");
  }
  else if (m_fInfoCreator=="BWks") {
    checkFInfoType("BWwp","BeagleWorks/WordPerfect Works") ||
    checkFInfoType("BWdb","BeagleWorks/WordPerfect Works[Database]") ||
    checkFInfoType("BWss","BeagleWorks/WordPerfect Works[SpreadSheet]") ||
    checkFInfoType("BWpt","BeagleWorks/WordPerfect Works[Paint]") ||
    checkFInfoType("BWdr","BeagleWorks/WordPerfect Works[Draw]") ||
    checkFInfoType("BeagleWorks/WordPerfect Works");
  }
  else if (m_fInfoCreator=="CARO") {
    checkFInfoType("PDF ", "Acrobat PDF");
  }
  else if (m_fInfoCreator=="C#+A") {
    checkFInfoType("C#+D","RagTime 5") || checkFInfoType("C#+F","RagTime 5[form]") ||
    checkFInfoType("RagTime 5");
  }
  else if (m_fInfoCreator=="CDrw") {
    checkFInfoType("dDrw", "ClarisDraw") || checkFInfoType("dDst", "ClarisDraw[stationary]") ||
    checkFInfoType("iLib", "ClarisDraw[library]") || checkFInfoType("ClarisDraw");
  }
  else if (m_fInfoCreator=="CRDW") {
    checkFInfoType("CKDT","CricketDraw") || checkFInfoType("CricketDraw");
  }
  else if (m_fInfoCreator.substr(0,3)=="DAD") {
    if (m_fInfoCreator=="DAD2") {
      checkFInfoType("drw2","Canvas 2-3") || checkFInfoType("Canvas 2-3");
    }
    else if (m_fInfoCreator=="DAD5") {
      checkFInfoType("drw5","Canvas 5") || checkFInfoType("drwt","Canvas 5[template]") ||
      checkFInfoType("VINF","Canvas 5[image]") || checkFInfoType("Canvas 5");
    }
    else if (m_fInfoCreator=="DAD6") {
      checkFInfoType("drw6","Canvas 6") || checkFInfoType("drwt","Canvas 6[template]") ||
      checkFInfoType("VINF","Canvas 6[image]") || checkFInfoType("Canvas 6");
    }
    else if (m_fInfoCreator=="DAD7") {
      checkFInfoType("drw7","Canvas 7") || checkFInfoType("drwt","Canvas 7[template]") ||
      checkFInfoType("VINF","Canvas 7[image]") || checkFInfoType("Canvas 7");
    }
    else if (m_fInfoCreator=="DAD8") {
      checkFInfoType("drw8","Canvas 8") || checkFInfoType("drwt","Canvas 8[template]") ||
      checkFInfoType("VINF","Canvas 8[image]") || checkFInfoType("Canvas 8");
    }
    else if (m_fInfoCreator=="DAD9") {
      checkFInfoType("drwX","Canvas 9") || checkFInfoType("drwt","Canvas 9[template]") ||
      checkFInfoType("Canvas 9");
    }
    else if (m_fInfoCreator=="DADX") {
      checkFInfoType("drwX","Canvas X") || checkFInfoType("drwt","Canvas X[template]") ||
      checkFInfoType("Canvas X");
    }
  }
  else if (m_fInfoCreator=="DkmR") {
    checkFInfoType("TEXT","Basic text(created by DOCMaker)") || checkFInfoType("DOCMaker");
  }
  else if (m_fInfoCreator=="Dc@P" || m_fInfoCreator=="Dk@P") {
    checkFInfoType("APPL","DOCMaker") || checkFInfoType("DOCMaker");
  }
  else if (m_fInfoCreator=="DDAP") {
    checkFInfoType("DDFL+","DiskDoubler") || checkFInfoType("DiskDoubler");
  }
  else if (m_fInfoCreator=="FAIR") {
    checkFInfoType("FWXX","Fair Witness") || checkFInfoType("Fair Witness");
  }
  else if (m_fInfoCreator=="FH50") {
    checkFInfoType("AGD1","FreeHand 5") || checkFInfoType("FreeHand 5");
  }
  else if (m_fInfoCreator=="FHA2") {
    checkFInfoType("FHD2","FreeHand 2") || checkFInfoType("FHT2","FreeHand 2[template]") || checkFInfoType("FreeHand 2");
  }
  else if (m_fInfoCreator=="FHA3") {
    checkFInfoType("FHD3","FreeHand 3") || checkFInfoType("FreeHand 3");
  }
  else if (m_fInfoCreator=="FMPR") {
    checkFInfoType("FMPR","Claris FileMaker Pro") || checkFInfoType("Claris FileMaker Pro");
  }
  else if (m_fInfoCreator=="FS03") {
    checkFInfoType("WRT+","WriterPlus") || checkFInfoType("WriterPlus");
  }
  else if (m_fInfoCreator=="FSDA") {
    checkFInfoType("RIFF","Dabbler 1") || checkFInfoType("Dabbler 1");
  }
  else if (m_fInfoCreator=="FSPS") {
    checkFInfoType("RIFF","Painter 1") || checkFInfoType("FSPP","Painter[texture]") || checkFInfoType("Painter 1");
  }
  else if (m_fInfoCreator=="FSX3") {
    checkFInfoType("RIFF","Painter 3-6") || checkFInfoType("FSFS","Painter 3-6[movie]") || checkFInfoType("Painter 3-6");
  }
  else if (m_fInfoCreator=="Fram") {
    checkFInfoType("FASL","FrameMaker") || checkFInfoType("MIF2","FrameMaker MIF2.0") ||
    checkFInfoType("MIF3","FrameMaker MIF3.0") || checkFInfoType("MIF ","FrameMaker MIF") ||
    checkFInfoType("FrameMaker");
  }
  else if (m_fInfoCreator=="FWRT") {
    checkFInfoType("FWRT","FullWrite 1.0") || checkFInfoType("FWRM","FullWrite 1.0") ||
    checkFInfoType("FWRI","FullWrite 2.0") || checkFInfoType("FullWrite");
  }
  else if (m_fInfoCreator=="F#+A") {
    checkFInfoType("F#+D","RagTime Classic") || checkFInfoType("F#+F","RagTime Classic[form]") ||
    checkFInfoType("RagTime Classic");
  }
  else if (m_fInfoCreator=="GM01") {
    checkFInfoType("GfMt","MouseWrite") || checkFInfoType("MouseWrite");
  }
  else if (m_fInfoCreator=="JAZZ") { // Jazz Lotus
    checkFInfoType("JWKS","Jazz(spreadsheet)") || checkFInfoType("JWPD","Jazz(text)") || checkFInfoType("Jazz");
  }
  else if (m_fInfoCreator=="JWrt") {
    checkFInfoType("TEXT","JoliWrite") || checkFInfoType("ttro","JoliWrite[readOnly]") ||
    checkFInfoType("JoliWrite");
  }
  else if (m_fInfoCreator=="HMiw") {
    checkFInfoType("IWDC","HanMac Word-J") || checkFInfoType("HanMac Word-J");
  }
  else if (m_fInfoCreator=="HMdr") {
    checkFInfoType("DRD2","HanMac Word-K") || checkFInfoType("HanMac Word-K");
  }
  else if (m_fInfoCreator=="L123") {
    checkFInfoType("LWKS","Lotus123") || checkFInfoType("Lotus123");
  }
  else if (m_fInfoCreator=="LibW") {
    checkFInfoType("Chnk","Microspot Media Assistant") || checkFInfoType("Microspot Media Assistant");
  }
  else if (m_fInfoCreator=="LETR") {
    checkFInfoType("APPL","Take A Letter[auto]") || checkFInfoType("Take A Letter");
  }
  else if (m_fInfoCreator=="LMAN") { // tutorial installed with canvas
    checkFInfoType("TEXT","Canvas 7") || checkFInfoType("Canvas 7");
  }
  else if (m_fInfoCreator=="LWTE") {
    checkFInfoType("TEXT","LightWayText") || checkFInfoType("MACR","LightWayText[MACR]") ||
    checkFInfoType("pref","LightWayText[Preferences]") ||
    checkFInfoType("ttro","LightWayText[Tutorial]") || checkFInfoType("LightWayText");
  }
  else if (m_fInfoCreator=="LWTR") {
    checkFInfoType("APPL","LightWayText[appli]") || checkFInfoType("LightWayText");
  }
  else if (m_fInfoCreator=="MACA") {
    checkFInfoType("WORD","MacWrite") || checkFInfoType("MacWrite");
  }
  else if (m_fInfoCreator=="MACD") {
    checkFInfoType("DRWG","MacDraft 1.0") || checkFInfoType("MacDraft 1.0");
  }
  else if (m_fInfoCreator=="MACW") {
    checkFInfoType("MWCT","MaxWrite 1.0") || checkFInfoType("MaxWrite 1.0");
  }
  else if (m_fInfoCreator=="MART") {
    checkFInfoType("RSGF","ReadySetGo 1") || checkFInfoType("RSGI","ReadySetGo 2") || checkFInfoType("ReadySetGo 1/2");
  }
  else if (m_fInfoCreator=="MD40") {
    checkFInfoType("MDDC","MacDraft 4-5") || checkFInfoType("MSYM","MacDraft 4-5[lib]") ||
    checkFInfoType("MacDraft 4-5");
  }
  else if (m_fInfoCreator=="MDsr") {
    checkFInfoType("APPL","MacDoc(appli)") || checkFInfoType("MacDoc");
  }
  else if (m_fInfoCreator=="MDvr") {
    checkFInfoType("MDdc","MacDoc") || checkFInfoType("MacDoc");
  }
  else if (m_fInfoCreator=="MDFT") {
    checkFInfoType("DRWG","MacDraft 1.2") || checkFInfoType("MacDraft 1.2");
  }
  else if (m_fInfoCreator=="MDRW") {
    checkFInfoType("DRWG","MacDraw") || checkFInfoType("MacDraw");
  }
  else if (m_fInfoCreator=="MDPL") {
    checkFInfoType("DRWG","MacDraw II") || checkFInfoType("STAT","MacDraw II(template)") || checkFInfoType("MacDraw II");
  }
  else if (m_fInfoCreator=="MEMR") {
    checkFInfoType("RSGR","ReadySetGo 4.5") || checkFInfoType("ReadySetGo 4.5");
  }
  else if (m_fInfoCreator=="MMBB") {
    checkFInfoType("MBBT","Mariner Write") || checkFInfoType("Mariner Write");
  }
  else if (m_fInfoCreator=="MORE") {
    checkFInfoType("MORE","More") || checkFInfoType("More");
  }
  else if (m_fInfoCreator=="MOR2") {
    checkFInfoType("MOR2","More 2") || checkFInfoType("MOR3","More 3") ||
    checkFInfoType("More 2-3");
  }
  else if (m_fInfoCreator=="MPNT") {
    checkFInfoType("PNTG","MacPaint") || checkFInfoType("MacPaint");
  }
  else if (m_fInfoCreator=="MRSM") {
    checkFInfoType("RSGJ","ReadySetGo 3") || checkFInfoType("RSGK","ReadySetGo 4") || checkFInfoType("ReadySetGo 3/4");
  }
  else if (m_fInfoCreator=="MSWD") {
    checkFInfoType("WDBN","Microsoft Word 3-5") ||
    checkFInfoType("GLOS","Microsoft Word 3-5[glossary]") ||
    checkFInfoType("W6BN", "Microsoft Word 6") ||
    checkFInfoType("W8BN", "Microsoft Word 8") ||
    checkFInfoType("W8TN", "Microsoft Word 8[W8TN]") || // ?
    checkFInfoType("WXBN", "Microsoft Word 97-2004") || // Office X ?
    checkFInfoType("Microsoft Word");
  }
  else if (m_fInfoCreator=="MSWK") {
    checkFInfoType("AWWP","Microsoft Works 3") ||
    checkFInfoType("AWDB","Microsoft Works 3-4[database]") ||
    checkFInfoType("AWDR","Microsoft Works 3-4[draw]") ||
    checkFInfoType("AWSS","Microsoft Works 3-4[spreadsheet]") ||
    checkFInfoType("RLRB","Microsoft Works 4") ||
    checkFInfoType("sWRB","Microsoft Works 4[template]") ||
    checkFInfoType("Microsoft Works 3-4");
  }
  else if (m_fInfoCreator=="MWII") {
    checkFInfoType("MW2D","MacWrite II") || checkFInfoType("MacWrite II");
  }
  else if (m_fInfoCreator=="MWPR") {
    checkFInfoType("MWPd","MacWrite Pro") || checkFInfoType("MacWrite Pro");
  }
  else if (m_fInfoCreator=="NISI") {
    checkFInfoType("TEXT","Nisus") || checkFInfoType("GLOS","Nisus[glossary]") ||
    checkFInfoType("SMAC","Nisus[macros]") || checkFInfoType("edtt","Nisus[lock]") ||
    checkFInfoType("Nisus");
  }
  else if (m_fInfoCreator=="PaPy") {
    checkFInfoType("PAPD","Papyrus") || checkFInfoType("Papyrus");
  }
  else if (m_fInfoCreator=="PANT") {
    checkFInfoType("PANT","FullPaint") || checkFInfoType("FullPaint");
  }
  else if (m_fInfoCreator=="PIXR") {
    checkFInfoType("PX01","Pixel Paint") || checkFInfoType("Pixel Paint");
  }
  else if (m_fInfoCreator=="PLAN") {
    checkFInfoType("MPBN","MultiPlan") || checkFInfoType("MultiPlan");
  }
  else if (m_fInfoCreator=="PPNT") {
    checkFInfoType("SLDS","Microsoft PowerPoint") || checkFInfoType("Microsoft PowerPoint v1/2");
  }
  else if (m_fInfoCreator=="PPT3") {
    checkFInfoType("SLD3","Microsoft PowerPoint v3.0") || checkFInfoType("SLD8","Microsoft PowerPoint 97-2004") ||
    checkFInfoType("Microsoft PowerPoint");
  }
  else if (m_fInfoCreator=="PSIP") {
    checkFInfoType("AWWP","Microsoft Works 1.0") || checkFInfoType("Microsoft Works 1.0");
  }
  else if (m_fInfoCreator=="PSI2") {
    checkFInfoType("AWWP","Microsoft Works 2.0") || checkFInfoType("AWDB","Microsoft Works 2.0[database]") ||
    checkFInfoType("AWSS","Microsoft Works 2.0[spreadsheet]") || checkFInfoType("Microsoft Works 2.0");
  }
  else if (m_fInfoCreator=="PWRI") {
    checkFInfoType("OUTL","MindWrite") || checkFInfoType("MindWrite");
  }
  else if (m_fInfoCreator=="R#+A") {
    checkFInfoType("R#+D","RagTime") || checkFInfoType("R#+F","RagTime[form]") ||
    checkFInfoType("RagTime");
  }
  else if (m_fInfoCreator=="RTF ") {
    checkFInfoType("RTF ","RTF ") || checkFInfoType("RTF");
  }
  else if (m_fInfoCreator=="Rslv") {
    checkFInfoType("RsWs","Claris Resolve") || checkFInfoType("Claris Resolve");
  }
  else if (m_fInfoCreator=="SIT!") {
    checkFInfoType("SIT5", "archive SIT") ||
    checkFInfoType("SITD", "archive SIT") ||
    checkFInfoType("SIT!", "archive SIT") || checkFInfoType("SIT");
  }
  else if (m_fInfoCreator=="SPNT") {
    checkFInfoType("SPTG","SuperPaint 1.") || checkFInfoType("PICT","SuperPaint 2.[pict]") ||
    checkFInfoType("DTXR","SuperPaint 3.[texture,pict]") || /* pict without 512 header*/
    checkFInfoType("PNTG","SuperPaint 3.[macpaint]") || /* MacPaint format*/
    checkFInfoType("PTXR","SuperPaint 3.[texture,pict]") || /* pict without 512 header*/
    checkFInfoType("SPn3","SuperPaint 3.[pict]") || checkFInfoType("SPSt","SuperPaint 3.[pict,stationary]") ||
    checkFInfoType("SuperPaint");
  }
  else if (m_fInfoCreator=="SSIW") {   // check me
    checkFInfoType("WordPerfect 1.0");
  }
  else if (m_fInfoCreator==std::string("St")+char(0xd8)+"l") { // argh, not standart character
    std::string type1=std::string("TEd")+char(0xb6); // argh, not standart character
    checkFInfoType(type1.c_str(), "Style") || checkFInfoType("Style");
  }
  else if (m_fInfoCreator=="StAV") {
    checkFInfoType("APPL", "Style[auto]") || checkFInfoType("Style");
  }
  else if (m_fInfoCreator=="SVsc") {
    checkFInfoType("SVsc", "StarCalc 3.0") || checkFInfoType("StarCalc 3.0");
  }
  else if (m_fInfoCreator=="SVsd") {
    checkFInfoType("SVsd", "StarDraw 3.0") || checkFInfoType("StarDraw 3.0");
  }
  else if (m_fInfoCreator==std::string("SW/")+char(0xa9)) {
    std::string type1=std::string("SW/")+char(0xa9);
    checkFInfoType(type1.c_str(), "StarWriter 3.0") ||  checkFInfoType("StarWriter 3.0");
  }
  else if (m_fInfoCreator=="SWCM") {
    checkFInfoType("JRNL","Student Writing Center[journal]") || checkFInfoType("LTTR","Student Writing Center[letter]") ||
    checkFInfoType("RPRT","Student Writing Center[report]") || checkFInfoType("SIGN","Student Writing Center[sign]") ||
    checkFInfoType("Student Writing Center");
  }
  else if (m_fInfoCreator=="TBB5") {
    checkFInfoType("TEXT","Tex-Edit") || checkFInfoType("ttro","Tex-Edit[readOnly]") ||
    checkFInfoType("Tex-Edit");
  }
  else if (m_fInfoCreator=="WILD") {
    checkFInfoType("STAK", "HyperCard") || checkFInfoType("HyperCard");
  }
  else if (m_fInfoCreator=="WMkr") {
    checkFInfoType("Word","WordMaker") || checkFInfoType("WSta","WordMaker[template]") || checkFInfoType("WordMaker");
  }
  else if (m_fInfoCreator=="WNGZ") {
    checkFInfoType("WZSS","Wingz[spreadsheet]") || checkFInfoType("WZSC","Wingz[script]") ||
    checkFInfoType("Wingz");
  }
  else if (m_fInfoCreator=="WORD") {
    checkFInfoType("WDBN","Microsoft Word 1") || checkFInfoType("Microsoft Word 1");
  }
  else if (m_fInfoCreator=="WPC2") {
    checkFInfoType("WordPerfect");
  }
  else if (m_fInfoCreator=="XCEL") {
    checkFInfoType("XCEL","Microsoft Excel 1") ||
    checkFInfoType("XLS3","Microsoft Excel 3") ||
    checkFInfoType("XLS4","Microsoft Excel 4") ||
    checkFInfoType("XLS5","Microsoft Excel 5") ||
    checkFInfoType("XLS8","Microsoft Excel 97-2004") ||
    checkFInfoType("TEXT","Microsoft Excel[text export]") ||
    checkFInfoType("Microsoft Excel");
  }
  else if (m_fInfoCreator=="XPR3") {
    checkFInfoType("XDOC","QuarkXPress 3-4") ||
    checkFInfoType("XTMP","QuarkXPress 3-4[template]") ||
    checkFInfoType("XBOK","QuarkXPress 4[book]") ||
    checkFInfoType("XLIB","QuarkXPress 3-4[library]") ||
    checkFInfoType("QuarkXPress 3-4");
  }
  else if (m_fInfoCreator=="XPRS") {
    checkFInfoType("XDOC","QuarkXPress 1-2") || checkFInfoType("QuarkXPress 1-2");
  }
  else if (m_fInfoCreator=="ZEBR") {
    checkFInfoType("ZWRT","GreatWorks") || checkFInfoType("ZTRM","GreatWorks[comm]") ||
    checkFInfoType("ZDBS","GreatWorks[database]") || checkFInfoType("ZCAL","GreatWorks[spreadsheet]") ||
    checkFInfoType("ZOLN","GreatWorks[outline]") || checkFInfoType("PNTG","GreatWorks v1[paint]") ||
    checkFInfoType("ZPNT","GreatWorks v2[paint]") || checkFInfoType("ZOBJ","GreatWorks[draw]") ||
    checkFInfoType("ZCHT","GreatWorks[chart]") || checkFInfoType("GreatWorks");
  }
  else if (m_fInfoCreator=="ZWRT") {
    checkFInfoType("Zart","Z-Write") || checkFInfoType("Z-Write");
  }
  else if (m_fInfoCreator=="aca3") {
    checkFInfoType("acf3","FreeHand v1") || checkFInfoType("act3","FreeHand v1[template]") ||
    checkFInfoType("FreeHand v1");
  }
  else if (m_fInfoCreator=="cAni") {
    checkFInfoType("curs","CursorAnimator") || checkFInfoType("CursorAnimator");
  }
  else if (m_fInfoCreator=="dPro") {
    checkFInfoType("dDoc","MacDraw Pro") || checkFInfoType("dLib","MacDraw Pro(slide)") || checkFInfoType("MacDraw Pro");
  }
  else if (m_fInfoCreator=="eDcR") {
    checkFInfoType("eDoc","eDOC") || checkFInfoType("eDOC");
  }
  else if (m_fInfoCreator=="eSRD") {
    checkFInfoType("APPL","eDOC(appli)") || checkFInfoType("eDOC");
  }
  else if (m_fInfoCreator=="nX^n") {
    checkFInfoType("nX^d","WriteNow 2") || checkFInfoType("nX^2","WriteNow 3-4") ||
    checkFInfoType("WriteNow");
  }
  else if (m_fInfoCreator=="ntxt") {
    checkFInfoType("TEXT","Anarcho");
  }
  else if (m_fInfoCreator=="ttxt") {
    if (m_fInfoType=="TEXT") {
      /* a little complex can be Classic MacOS SimpleText/TeachText or
      a << normal >> text file */
      XAttr rsrcAttr(m_fName.c_str());
      std::unique_ptr<InputStream> rsrcStream{rsrcAttr.getStream("com.apple.ResourceFork")};
      bool ok = false;
      if (rsrcStream && rsrcStream->length()) {
        libmwaw_tools::RSRC rsrcManager(*rsrcStream);
        ok = rsrcManager.hasEntry("styl", 128);
      }
      if (ok) checkFInfoType("TEXT","TeachText/SimpleText");
      else checkFInfoType("TEXT","Basic text");
    }
    else
      checkFInfoType("ttro","TeachText/SimpleText[readOnly]");
  }
  // now by type
  else if (m_fInfoType=="AAPL") {
    checkFInfoCreator("Application");
  }
  else if (m_fInfoType=="JFIF") {
    checkFInfoCreator("JPEG");
  }
  if (m_fInfoCreator.length()==0) {
    MWAW_DEBUG_MSG(("File::readFileInformation: Find unknown file info %s[%s]\n", m_fInfoCreator.c_str(), m_fInfoType.c_str()));
  }
  return true;
}

bool File::readDataInformation()
{
  if (!m_fName.length())
    return false;
  libmwaw_tools::FileStream input(m_fName.c_str());
  if (!input.ok()) {
    MWAW_DEBUG_MSG(("File::readDataInformation: can not open the data fork\n"));
    return false;
  }
  if (input.length() < 10)
    return true;
  input.seek(0, InputStream::SK_SET);
  int val[5];
  for (auto &v : val) v = int(input.readU16());
  // ----------- clearly discriminant ------------------
  if (val[2] == 0x424F && val[3] == 0x424F && (val[0]>>8) < 8) {
    m_dataResult.push_back("ClarisWorks/AppleWorks");
    return true;
  }
  if (val[0]==0x4257 && val[1]==0x6b73 && val[2]==0x4257 && val[4]==0x4257) {
    if (val[3]==0x6462)
      m_dataResult.push_back("BeagleWorks/WordPerfect Works[Database]");
    else if (val[3]==0x6472)
      m_dataResult.push_back("BeagleWorks/WordPerfect Works[Draw]");
    else if (val[3]==0x7074)
      m_dataResult.push_back("BeagleWorks/WordPerfect Works[Paint]");
    else if (val[3]==0x7373)
      m_dataResult.push_back("BeagleWorks/WordPerfect Works[Spreadsheet]");
    else if (val[3]==0x7770)
      m_dataResult.push_back("BeagleWorks/WordPerfect Works");
    else
      m_dataResult.push_back("BeagleWorks/WordPerfect Works[Unknown]");
    return true;
  }
  if (val[0]==0x4323 && val[1]==0x2b44 && val[2]==0xa443 && val[3]==0x4da5 && val[4]==0x4864) {
    m_dataResult.push_back("RagTime 5-6");
    return true;
  }
  if (val[0]==0x5772 && val[1]==0x6974 && val[2]==0x654e && val[3]==0x6f77 && val[4]==2) {
    m_dataResult.push_back("WriteNow 3-4");
    return true;
  }
  if (val[0]==0x4241 && val[1]==0x545F && val[2]==0x4254 && val[3]==0x5353) {
    m_dataResult.push_back("Claris Resolve");
    return true;
  }
  if (val[0]==0x574e && val[1]==0x475a && val[2]==0x575a) {
    if (val[3]==0x5353) {
      m_dataResult.push_back("Wingz");
      return true;
    }
    if (val[3]==0x5343) {
      m_dataResult.push_back("Wingz[script]");
      return true;
    }
  }
  if (val[0]==3 && val[1]==0x4d52 && val[2]==0x4949 && val[3]==0x80) {
    m_dataResult.push_back("More 2");
    return true;
  }
  if (val[0]==6 && val[1]==0x4d4f && val[2]==0x5233 && val[3]==0x80) {
    m_dataResult.push_back("More 3");
    return true;
  }
  if (val[0]==2 && val[1]==0 && val[2]==2 && val[3]==0x262 && val[4]==0x262) {
    m_dataResult.push_back("MacDraft 1");
    return true;
  }
  if (val[0]==0x4646 && val[1]==0x4646 && val[2]==0x3030 && val[3]==0x3030) {
    m_dataResult.push_back("Mariner Write");
    return true;
  }
  if (val[0]==0x4452 && val[1]==0x5747) { // DRWG
    if (val[2]==0x4d44) { // MD
      m_dataResult.push_back("MacDraw");
      return true;
    }
    if (val[2]==0 || val[2]==0x4432) { // nothing or D2
      m_dataResult.push_back("MacDraw II");
      return true;
    }
  }
  if (val[0]==0x5354 && val[1]==0x4154 && (val[2]==0||val[2]==0x4432)) { // STATD2
    m_dataResult.push_back("MacDraw II(template)");
    return true;
  }
  if (val[0]==0x6444 && val[1]==0x6f63 && val[2]==0x4432) { // dDocD2
    m_dataResult.push_back("MacDraw Pro");
    return true;
  }
  if (val[0]==0x644c && val[1]==0x6962 && val[2]==0x4432) { // dLibD2
    m_dataResult.push_back("MacDraw Pro(slide)");
    return true;
  }
  if (val[0]==0x4859 && val[1]==0x4c53 && val[2]==0x0210) {
    m_dataResult.push_back("HanMac Word-K");
    return true;
  }
  if (val[0]==0x594c && val[1]==0x5953 && val[2]==0x100) {
    m_dataResult.push_back("HanMac Word-J");
    return true;
  }
  if (val[0]==0x6163 && val[1]==0x6633 && val[2]<9) {
    m_dataResult.push_back("FreeHand v1");
    return true;
  }
  if (val[0]==0x4648 && val[1]==0x4432 && val[2]<20) {
    m_dataResult.push_back("FreeHand v2");
    return true;
  }
  if (val[0]==0x0447 && val[1]==0x4d30 && val[2]==0x3400) { // ^DGM04
    m_dataResult.push_back("MouseWrite");
    return true;
  }
  if (val[0]==0x000c && val[1]==0x1357 && (val[2]==0x13 || val[2]==0x14) && val[3]==0) {
    m_dataResult.push_back("Drawing Table");
    return true;
  }
  if (val[0]==0x2550 && val[1]==0x4446) {
    m_dataResult.push_back("PDF");
    return true;
  }
  if (val[0]==0x2854 && val[1]==0x6869 && val[2]==0x7320 && val[3]==0x6669) {
    m_dataResult.push_back("BinHex");
    return true;
  }
  if (val[0]==0x2521 && val[1]==0x5053 && val[2]==0x2d41 && val[3] == 0x646f && val[4]==0x6265) {
    m_dataResult.push_back("PostScript");
    return true;
  }
  if (val[0]==0xc5d0 && val[1]==0xd3c6) {
    m_dataResult.push_back("Adobe EPS");
    return true;
  }
  if (val[0]==0x7b5c && val[1]==0x7274 && (val[2]>>8)==0x66) {
    m_dataResult.push_back("RTF");
    return true;
  }
  if (val[2]==0x6d6f && val[3]==0x6f76) {
    m_dataResult.push_back("QuickTime movie");
    return true;
  }
  if (val[0]==0 && (val[1]>>8)==0 && val[2]==0x6674 && val[3]==0x7970 && val[4]==0x3367) {
    m_dataResult.push_back("MP4");
    return true;
  }
  if (val[0]==0x4749 && val[1]==0x4638 && (val[2]==0x3761 || val[2]==0x3961)) {
    m_dataResult.push_back("GIF");
    return true;
  }
  if (val[0]==0x8950 && val[1]==0x4e47 && val[2]==0x0d0a && val[3]==0x1a0a) {
    m_dataResult.push_back("PNG");
    return true;
  }
  if (val[0]==0x1a54 && val[1]==0x4c43 && (val[2]&0xfeff)==0x246 && val[3]==0x4600) {
    m_dataResult.push_back("Student Writing Center");
    return true;
  }
  if (val[3]==6 && val[4]==3 && input.length()>30) {
    input.seek(10, InputStream::SK_SET);
    if (val[0]==0x4d44 && val[1]==0x4443 && val[2]==0x3230) {
      m_dataResult.push_back("MacDraft 4-5");
      return true;
    }
    else if (input.readU16()==0 && input.readU16()==0x48 && input.readU16()==0x48) {
      m_dataResult.push_back("MacDraft 4-5[lib]");
      return true;
    }
  }
  if (val[0]==0 && (val[1]==1||val[1]==2) && val[2]==0x4441 && val[3]==0x4435 && val[4]==0x5052) {
    if (val[1]==1)
      m_dataResult.push_back("Canvas Image 5-8");
    else
      m_dataResult.push_back("Canvas Image 9");
    return true;
  }
  if (val[0]==0x200 && val[1]==0x80) {
    if (val[2]==0 && val[3]==0 && (val[4]>>8)<=8 && (val[4]&0xff)==0) {
      m_dataResult.push_back("Canvas 5[mac]");
      return true;
    }
    input.seek(9, InputStream::SK_SET);
    auto len=input.readU32();
    if (len>=0x800 && len<=0x8000) { // block size
      auto len1=input.readU32();
      if (len1>0x800 && len1<=0x800c) {
        m_dataResult.push_back("Canvas 6-8[mac]");
        return true;
      }
    }
  }
  if (val[0]==0x100 && val[1]==0x8000) {
    if ((val[2]>=0&&val[2]<=8) && val[3]==0 && (val[4]>>8)==0) {
      m_dataResult.push_back("Canvas 5[windows]");
      return true;
    }

    input.seek(9, InputStream::SK_SET);
    unsigned len=0, decal=0;
    for (int i=0; i<4; ++i, decal+=8)
      len |= unsigned(input.readU8())<<decal;
    if (len>=0x800 && len<=0x8000) { // block size
      unsigned len1=0;
      decal=0;
      for (int i=0; i<4; ++i, decal+=8)
        len1 |= unsigned(input.readU8())<<decal;
      if (len1>0x800 && len1<=0x800c) {
        m_dataResult.push_back("Canvas 6-8[windows]");
        return true;
      }
    }
  }
  if (val[0]==0x1e && val[1]==0 && val[2]==0x86) {
    m_dataResult.push_back("ReadySetGo 3");
    return true;
  }
  if (val[0]==0 && val[1]==0 && val[2]==0 && val[3]==0 && val[4]==0) {
    input.seek(10, InputStream::SK_SET);
    unsigned endian=unsigned(input.readU16());
    if (endian==0x100 && input.readU16()==0x8000) {
      m_dataResult.push_back("Canvas 9-11[windows]");
      return true;
    }
    if (endian==0x200 && input.readU16()==0x80) {
      m_dataResult.push_back("Canvas 9-10[mac]");
      return true;
    }
  }
  if (val[0]==0xffd8 &&
      ((val[1]==0xffe0 && val[3]==0x4a46 && val[4] == 0x4946) ||
       (val[1]==0xffe1 && val[3]==0x4578 && val[4] == 0x6966) ||
       (val[1]==0xffe8 && val[3]==0x5350 && val[4] == 0x4946))) {
    m_dataResult.push_back("JPEG");
    return true;
  }
  if (val[0]==0x4949 && val[1]==0x2a00) {
    m_dataResult.push_back("TIF");
    return true;
  }
  if (val[0]==0x4d4d && val[1]==0x002a) {
    m_dataResult.push_back("TIFF");
    return true;
  }
  if (val[0]==0x4f67 && val[1]==0x6753) {
    m_dataResult.push_back("OGG data");
    return true;
  }
  // ----------- less discriminant ------------------
  if (val[0]==0xd0cf && val[1]==0x11e0 && val[2]==0xa1b1 && val[3]==0x1ae1) {
    libmwaw_tools::OLE ole(input);
    for (int step=0; step < 3; step++) {
      std::string res=step==0 ? ole.getClipName() : step==1 ? ole.getCLSIDType() : ole.getCompObjType();
      if (res.empty())
        continue;
      m_dataResult.push_back(res);
      return true;
    }
    m_dataResult.push_back("OLE file: can be DOC, DOT, PPS, PPT, XLA, XLS, WIZ, WPS(4.0), ...");
    return true;
  }
  if (val[0]==0x100 || val[0]==0x200) {
    if (val[1]==0x5a57 && val[2]==0x5254) {
      m_dataResult.push_back("GreatWorks");
      return true;
    }
    if (val[1]==0x5a4f && val[2]==0x4c4e) {
      m_dataResult.push_back("GreatWorks[outline]");
      return true;
    }
    if (val[1]==0x5a44 && val[2]==0x4253) {
      m_dataResult.push_back("GreatWorks[database]");
      return true;
    }
    if (val[1]==0x5a43 && val[2]==0x414c) {
      m_dataResult.push_back("GreatWorks[spreadsheet]");
      return true;
    }
    if (val[1]==0x5a4f && val[2]==0x424a) {
      m_dataResult.push_back("GreatWorks[draw]");
      return true;
    }
    if (val[1]==0x5a43 && val[2]==0x4854) {
      m_dataResult.push_back("GreatWorks[chart]");
      return true;
    }
  }
  if (val[0]==0 && (val[1]==0x4d4d||val[1]==0x4949) && (val[2]==0x5850 || val[2]==0xd850) && (val[3]&0xff00)==0x5200) {
    if ((val[3]&0xff)==0x33) {
      if (val[2]==0x5850)
        m_dataResult.push_back("QuarkXPress 3");
      else
        m_dataResult.push_back("QuarkXPress 3[library]");
      return true;
    }
    else if ((val[3]&0xff)==0x61) {
      if (val[2]==0x5850)
        m_dataResult.push_back("QuarkXPress 3[khorean]");
      else
        m_dataResult.push_back("QuarkXPress 3[khorean,library]");
      return true;
    }
  }
  if ((val[0]==0x100||val[0]==0x200) && val[2]==0x4558 && val[3]==0x5057) {
    if (val[0]==0x100)
      m_dataResult.push_back("ClarisDraw");
    else
      m_dataResult.push_back("ClarisDraw[library]");
    return true;
  }
  if (val[0]==0x4348 && val[1]==0x4e4b && val[2]==0x100 && val[3]==0) {
    m_dataResult.push_back("Style");
    return true;
  }
  if (val[0]==0x5041 && val[1]==0x5031 && (val[2]>=0x1fa0 && val[2]<=0x1fbc) && val[3]==0x0fa0) {
    m_dataResult.push_back("Papyrus");
    return true;
  }
  // less discriminant
  if ((val[0]==0xfe32 && val[1]==0) || (val[0]==0xfe34 && val[1]==0) ||
      (val[0] == 0xfe37 && (val[1] == 0x23 || val[1] == 0x1c))) {
    switch (val[1]) {
    case 0:
      if (val[0]==0xfe34)
        m_dataResult.push_back("Microsoft Word 3.0");
      else if (val[0]==0xfe32)
        m_dataResult.push_back("Microsoft Word 1.0");
      break;
    case 0x1c:
      m_dataResult.push_back("Microsoft Word 4.0");
      break;
    case 0x23:
      m_dataResult.push_back("Microsoft Word 5.0");
      break;
    default:
      break;
    }
  }
  if (val[0]==0x464f && val[1]==0x524d)
    m_dataResult.push_back("WordMaker");
  if (val[0]==0 && input.length() > 30) {
    input.seek(16, InputStream::SK_SET);
    if (input.readU16()==0x688f && input.readU16()==0x688f) {
      m_dataResult.push_back("RagTime");
      return true;
    }
  }
  if (val[0]==0 && val[1]==0 && val[2]==0 && val[3]==0 &&
      ((val[4]>>8)==4 || (val[4]>>8)==0x44))
    m_dataResult.push_back("WriteNow 1-2");
  if (val[0] == 0x2e && val[1] == 0x2e)
    m_dataResult.push_back("MacWrite II");
  if (val[0] == 4 && val[1] == 4)
    m_dataResult.push_back("MacWrite Pro");
  if (val[0] == 0x20 && val[1] == 0x20)
    m_dataResult.push_back("QuarkXpress 1");
  if (val[0] == 0x26 && val[1] == 0x26)
    m_dataResult.push_back("QuarkXpress 2");
  if (val[0] == 0x78)
    m_dataResult.push_back("ReadySetGo 1[unsure]");
  if (val[0] == 0x7704)
    m_dataResult.push_back("MindWrite");
  if (val[0] == 0x110)
    m_dataResult.push_back("WriterPlus");
  if (val[0] == 0x190 && (val[1]&0xff00)==0)
    m_dataResult.push_back("ReadySetGo 4.0[unsure]");
  if (val[0] == 0x138b)
    m_dataResult.push_back("ReadySetGo 4.5[unsure]");
  if (val[0]==0xdba5 && val[1]==0x2d00) {
    m_dataResult.push_back("Microsoft Word 2.0[pc]");
    return true;
  }
  if (val[0]==0xabcd && val[1]==0x54) {
    m_dataResult.push_back("DiskDoubler[archive]");
    return true;
  }
  if (val[0]==0x4D44) { // MD
    m_dataResult.push_back("MacDraw v0[unsure]");
  }
  if (val[0]==0xbad && val[1]==0xdeed && val[2]==0) {
    m_dataResult.push_back("Microsoft PowerPoint Mac");
    return true;
  }
  if (val[0]==0xedde && val[1]==0xad0b && val[3]==0) {
    m_dataResult.push_back("Microsoft PowerPoint Windows");
    return true;
  }
  if (val[0]==0x11ab && val[1]==0x0 && val[2]==0x13e8 && val[3]==0) {
    m_dataResult.push_back("Microsoft Multiplan Mac");
    return true;
  }
  if (val[0] == 3 || val[0] == 6) {
    int numParaPos = val[0] == 3 ? 2 : 1;
    if (val[numParaPos] < 0x1000 && val[numParaPos+1] < 0x100 && val[numParaPos+2] < 0x100)
      m_dataResult.push_back("MacWrite[unsure]");
  }
  if (val[0]==0 && val[1]==2 && val[2]==11)
    m_dataResult.push_back("Jazz(spreadsheet)[unsure]");
  if (val[0]==0) {
    std::stringstream s;
    bool ok = true;
    switch (val[1]) {
    case 4:
      s << "Microsoft Works 1.0";
      break;
    case 8:
      s << "Microsoft Works 2.0";
      break;
    case 9:
      s << "Microsoft Works 3.0";
      break;
    case 11:
      s << "Microsoft Works 4.0";
      break; // all excepted a text file
    default:
      ok = false;
      break;
    }
    input.seek(16, InputStream::SK_SET);
    int type = ok ? int(input.readU16()) : -1;
    switch (type) {
    case 1:
      break;
    case 2:
      s << "[database]";
      break;
    case 3:
      s << "[spreadsheet]";
      break;
    case 12:
      s << "[draw]";
      break;
    default:
      ok = false;
      break;
    }
    if (ok)
      m_dataResult.push_back(s.str());
  }
  if (val[0]==0 && (val[1]==0x7FFF || val[1]==0x8000)) {
    m_dataResult.push_back("PixelPaint[unsure]");
  }
  if (val[0]>=1 && val[0]<=4) {
    int sSz=(val[1]>>8);
    if (sSz>=6 && sSz<=8) {
      // check if we have a date
      input.seek(3, InputStream::SK_SET);
      bool ok=true;
      int numSlash=0;
      for (int i=0; i<sSz; ++i) {
        auto c=char(input.readU8());
        if (c>='0' && c<='9')
          continue;
        if (c=='/')
          ++numSlash;
        else {
          ok=false;
          break;
        }
      }
      if (ok && numSlash==2)
        m_dataResult.push_back("Cricket Draw 1.0");
    }
  }
  input.seek(-4, InputStream::SK_END);
  int lVal[2];
  for (auto &v : lVal) v = int(input.readU16());
  if (lVal[0] == 0x4657 && lVal[1]==0x5254)
    m_dataResult.push_back("FullWrite 2.0");
  else if (lVal[0] == 0x4E4C && lVal[1]==0x544F)
    m_dataResult.push_back("Acta Classic");
  else if (lVal[1]==0 && val[0]==1 && (val[1]==1||val[1]==2))
    m_dataResult.push_back("Acta v2[unsure]");
  else if (lVal[0] == 0 && lVal[1]==1) { // Maybe a FullWrite 1.0 file, limited check
    input.seek(-38, InputStream::SK_END);
    long eof = input.length();
    bool ok = true;
    for (int i = 0; i < 2; i++) {
      auto pos = long(input.readU32());
      auto sz = long(input.read32());
      if (sz <= 0 || pos+sz > eof) {
        ok = false;
        break;
      }
    }
    if (ok)
      m_dataResult.push_back("FullWrite 1.0[unsure]");
  }
#ifdef DEBUG
  if (m_dataResult.size()==0) {
    std::stringstream s;
    s << "Unknown: " << std::hex << std::setfill('0');
    for (auto v : val)
      s << std::setw(4) << v << " ";
    m_dataResult.push_back(s.str());
  }
#endif
  return true;
}

bool File::readRSRCInformation()
{
  if (!m_fName.length())
    return false;

  XAttr xattr(m_fName.c_str());
  std::unique_ptr<libmwaw_tools::InputStream> rsrcStream{xattr.getStream("com.apple.ResourceFork")};
  if (!rsrcStream) return false;

  if (!rsrcStream->length()) {
    return true;
  }
  libmwaw_tools::RSRC rsrcManager(*rsrcStream);
#  if 0
  MWAW_DEBUG_MSG(("File::readRSRCInformation: find a resource fork\n"));
#  endif
  m_rsrcResult = rsrcManager.getString(-16396); // the application missing name
  m_rsrcMissingMessage = rsrcManager.getString(-16397);
  auto listVersion = rsrcManager.getVersionList();
  for (auto const &vers : listVersion) {
    switch (vers.m_id) {
    case 1:
      m_fileVersion = vers;
      break;
    case 2:
      if (!m_appliVersion.ok()) m_appliVersion = vers;
      break;
    case 2002:
      m_appliVersion = vers;
      break;
    default:
      break;
    }
  }
  return true;
}

bool File::printResult(std::ostream &o, int verbose) const
{
  if (!canPrintResult(verbose)) return false;
  if (m_printFileName)
    o << m_fName << ":";
  if (m_fInfoResult.length())
    o << m_fInfoResult;
  else if (m_rsrcResult.length())
    o << m_rsrcResult;
  else if (m_dataResult.size()) {
    size_t num = m_dataResult.size();
    if (num>1)
      o << "[";
    for (size_t i = 0; i < num; i++) {
      o << m_dataResult[i];
      if (i+1!=num)
        o << ",";
    }
    if (num>1)
      o << "]";
  }
  else
    o << "unknown";
  if (verbose > 0) {
    if (m_fInfoCreator.length() || m_fInfoType.length())
      o << ":type=" << m_fInfoCreator << "["  << m_fInfoType << "]";
  }
  if (verbose > 1) {
    if (m_fileVersion.ok())
      o << "\n\tFile" << m_fileVersion;
    if (m_appliVersion.ok())
      o << "\n\tAppli" << m_appliVersion;
  }
  o << "\n";
  return true;
}
}

static void usage(char const *fName)
{
  std::cerr << "Usage: " << fName << " [OPTION] FILENAME\n";
  std::cerr << "\n";
  std::cerr << "try to find the file type of FILENAME\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "\t -f: Does not print the filename,\n";
  std::cerr << "\t -F: Prints the filename[default],\n";
  std::cerr << "\t -h: Shows this help message,\n";
  std::cerr << "\t -v: Output mwawFile version\n";
  std::cerr << "\t -wNum: define the verbose level.\n";
}

static int printVersion()
{
  std::cerr << "mwawFile " << VERSION << "\n";
  return 0;
}

int main(int argc, char *const argv[])
{
  int ch, verbose=0;
  bool printFileName=true;

  while ((ch = getopt(argc, argv, "fFhvw:")) != -1) {
    switch (ch) {
    case 'w':
      verbose=atoi(optarg);
      break;
    case 'f':
      printFileName = false;
      break;
    case 'F':
      printFileName = true;
      break;
    case 'v':
      printVersion();
      return 0;
    case 'h':
    case '?':
    default:
      verbose=-1;
      break;
    }
  }
  if (argc != 1+optind || verbose < 0) {
    usage(argv[0]);
    return -1;
  }
  std::unique_ptr<libmwaw_tools::File> file;
  try {
    file.reset(new libmwaw_tools::File(argv[optind]));
    file->readFileInformation();
  }
  catch (...) {
    std::cerr << argv[0] << ": can not open file " << argv[optind] << "\n";
    return -1;
  }
  if (!file)
    return -1;
  try {
    file->readDataInformation();
  }
  catch (...) {
  }
  try {
    file->readRSRCInformation();
  }
  catch (...) {
  }

  file->m_printFileName = printFileName;
  if (verbose >= 4)
    std::cout << *file;
  else if (file->canPrintResult(verbose))
    file->printResult(std::cout, verbose);
  return 0;
}
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
