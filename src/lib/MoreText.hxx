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

/*
 * Parser to More document
 *
 */
#ifndef MORE_TEXT
#  define MORE_TEXT

#include <set>

#include "libmwaw_internal.hxx"
#include "MWAWDebug.hxx"

struct MWAWListLevel;

namespace MoreTextInternal
{
struct Outline;
struct Paragraph;
struct State;

class SubDocument;
}

class MoreParser;

/** \brief the main class to read the text part of More Text file
 *
 *
 *
 */
class MoreText
{
  friend class MoreParser;
  friend class MoreTextInternal::SubDocument;
public:
  //! constructor
  explicit MoreText(MoreParser &parser);
  //! destructor
  virtual ~MoreText();

  /** returns the file version */
  int version() const;

  /** returns the number of pages */
  int numPages() const;

protected:
  //! try to create the text zones using read data
  bool createZones();
  //! send a main zone
  bool sendMainText();

  //! returns a subdocument to send the header or the footer
  std::shared_ptr<MWAWSubDocument> getHeaderFooter(bool header);

  //
  // intermediate level
  //

  //! read the list of topic positions
  bool readTopic(MWAWEntry const &entry);

  //! returns the last sub topic id corresponding to a topic and its child
  int getLastTopicChildId(int tId) const;

  /** check that the topic and its child does not loop (if so, cut some edge),
      return the number of breakpages in the sublist */
  int checkTopicList(size_t tId, std::set<size_t> &parent);

  //! read the list of comment/header/footer zones
  bool readComment(MWAWEntry const &entry);

  //! read the list of speaker note
  bool readSpeakerNote(MWAWEntry const &entry);

  //! send a text entry
  bool sendText(MWAWEntry const &entry, MWAWFont const &font);

  //! try to send a comment knowing the comment id
  bool sendComment(int cId);

  //! try to send a speakernote knowing the note id
  bool sendSpeakerNote(int nId);

  //! try to send a topic knowing the topic id
  bool sendTopic(int tId, int dLevel, std::vector<MWAWParagraph> &paraStack);

  //! read the list of fonts
  bool readFonts(MWAWEntry const &entry);

  //! read the list of outlines
  bool readOutlineList(MWAWEntry const &entry);

  //! read a outline
  bool readOutline(MWAWEntry const &entry, MoreTextInternal::Outline &outline);

  /** try to read a fontname

  \note: fId is set to -1 is the field contains only a fontname and can not find the associated id
   */
  bool readFont(MWAWEntry const &entry, std::string &fName, int &fId);
  /** try to read some tabs */
  bool readTabs(MWAWEntry const &entry, MoreTextInternal::Paragraph &para, std::string &mess);
  /** read a custom list level */
  bool readCustomListLevel(MWAWEntry const &entry, MWAWListLevel &level);

  //! try to read either a font, a fontname, a pattern, a int
  bool parseUnknown(MWAWEntry const &entry, long fDecal);

private:
  MoreText(MoreText const &orig) = delete;
  MoreText &operator=(MoreText const &orig) = delete;

protected:
  //
  // data
  //
  //! the parser state
  MWAWParserStatePtr m_parserState;

  //! the state
  std::shared_ptr<MoreTextInternal::State> m_state;

  //! the main parser;
  MoreParser *m_mainParser;
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
