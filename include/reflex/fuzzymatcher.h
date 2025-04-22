/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      fuzzymatcher.h
@brief     RE/flex fuzzy matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_FUZZYMATCHER_H
#define REFLEX_FUZZYMATCHER_H

#include <reflex/matcher.h>
#include <reflex/pattern.h>

namespace reflex {

/// RE/flex fuzzy matcher engine class, implements reflex::Matcher fuzzy pattern matching interface with scan, find, split functors and iterators.
/** More info TODO */
class FuzzyMatcher : public Matcher {
 public:
  /// Optional flags for the max parameter to constrain fuzzy matching, otherwise no constraints
  static const uint16_t INS = 0x1000; ///< fuzzy match allows character insertions (default)
  static const uint16_t DEL = 0x2000; ///< fuzzy match allows character deletions (default)
  static const uint16_t SUB = 0x4000; ///< character substitutions count as one edit, not two (insert+delete) (default)
  static const uint16_t BIN = 0x8000; ///< binary matching without UTF-8 multibyte encodings
  /// Default constructor.
  FuzzyMatcher()
    :
      Matcher()
  {
    distance(1);
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  FuzzyMatcher(
      const P     *pattern,         ///< points to a reflex::Pattern or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      Matcher(pattern, input, opt)
  {
    distance(1);
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  FuzzyMatcher(
      const P     *pattern,         ///< points to a reflex::Pattern or a string regex for this matcher
      uint16_t     max,             ///< max errors
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      Matcher(pattern, input, opt)
  {
    distance(max);
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  FuzzyMatcher(
      const P&     pattern,         ///< a reflex::Pattern or a string regex for this matcher
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      Matcher(pattern, input, opt)
  {
    distance(1);
  }
  /// Construct matcher engine from a pattern or a string regex, and an input character sequence.
  template<typename P> /// @tparam <P> a reflex::Pattern or a string regex 
  FuzzyMatcher(
      const P&     pattern,         ///< a reflex::Pattern or a string regex for this matcher
      uint16_t     max,             ///< max errors
      const Input& input = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)      ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      Matcher(pattern, input, opt)
  {
    distance(max);
  }
  /// Copy constructor.
  FuzzyMatcher(const FuzzyMatcher& matcher) ///< matcher to copy with pattern (pattern may be shared)
    :
      Matcher(matcher),
      max_(matcher.max_),
      err_(0),
      ins_(matcher.ins_),
      del_(matcher.del_),
      sub_(matcher.sub_),
      bin_(matcher.bin_)
  {
    DBGLOG("FuzzyMatcher::FuzzyMatcher(matcher)");
    bpt_.resize(max_);
  }
  using Matcher::operator=;
  /// Assign a matcher.
  virtual FuzzyMatcher& operator=(const FuzzyMatcher& matcher) ///< matcher to copy
  {
    Matcher::operator=(matcher);
    max_ = matcher.max_;
    err_ = 0;
    ins_ = matcher.ins_;
    del_ = matcher.del_;
    sub_ = matcher.sub_;
    bin_ = matcher.bin_;
    bpt_.resize(max_);
    return *this;
  }
  /// Polymorphic cloning.
  virtual FuzzyMatcher *clone()
  {
    return new FuzzyMatcher(*this);
  }
  /// Returns the number of edits made for the match, edits() <= max, not guaranteed to be the minimum edit distance.
  uint8_t edits()
    /// @returns 0 to max edit distance
    const
  {
    return err_;
  }
  /// Set or update fuzzy distance parameters
  void distance(uint16_t max) ///< max errors, INS, DEL, SUB
  {
    max_ = static_cast<uint8_t>(max);
    err_ = 0;
    ins_ = ((max & (INS | DEL | SUB)) == 0 || (max & INS));
    del_ = ((max & (INS | DEL | SUB)) == 0 || (max & DEL));
    sub_ = ((max & (INS | DEL | SUB)) == 0 || (max & SUB));
    bin_ = (max & BIN);
    bpt_.resize(max_);
  }
  /// Get the fuzzy distance parameters, the max is stored in the lower byte and INS, DEL, SUB are hi byte bits
  uint16_t distance()
  {
    return max_;
  }
 protected:
  /// Save state to restore fuzzy matcher state after a second pass
  struct SaveState {
    SaveState(size_t ded)
      :
        use(false),
        loc(0),
        cap(0),
        txt(0),
        cur(0),
        pos(0),
        ded(ded),
        mrk(false),
        err(0)
    { }
    bool    use;
    size_t  loc;
    size_t  cap;
    size_t  txt;
    size_t  cur;
    size_t  pos;
    size_t  ded;
    bool    mrk;
    uint8_t err;
  };
  /// Backtrack point.
  struct BacktrackPoint {
    BacktrackPoint()
      :
        pc0(NULL),
        pc1(NULL),
        len(0),
        err(0),
        alt(true),
        sub(true)
    { }
    const Pattern::Opcode *pc0; ///< start of opcode
    const Pattern::Opcode *pc1; ///< pointer to opcode to rerun on backtracking
    size_t                 len; ///< length of string matched so far
    uint8_t                err; ///< to restore errors
    bool                   alt; ///< true if alternating between pattern char substitution and insertion, otherwise insertion only
    bool                   sub; ///< flag alternates between pattern char substitution (true) and insertion (false)
  };
  /// Set backtrack point.
  void point(BacktrackPoint& bpt, const Pattern::Opcode *pc, size_t len, bool alternate = true, bool eof = false)
  {
    // advance to a goto opcode
    while (!Pattern::is_opcode_goto(*pc))
      ++pc;
    bpt.pc0 = pc;
    bpt.pc1 = pc;
    bpt.len = len - !eof;
    bpt.err = err_;
    bpt.alt = sub_ && alternate;
    bpt.sub = bpt.alt;
  }
  /// backtrack on a backtrack point to insert or substitute a pattern char, restoring current text char matched and errors.
  const Pattern::Opcode *backtrack(BacktrackPoint& bpt, int& ch)
  {
    // no more alternatives
    if (bpt.pc1 == NULL)
      return NULL;
    // done when no more goto opcodes on characters remain
    if (!Pattern::is_opcode_goto(*bpt.pc1))
      return bpt.pc1 = NULL;
    Pattern::Index jump = Pattern::index_of(*bpt.pc1);
    // last opcode is a HALT?
    if (jump == Pattern::Const::HALT)
    {
      if (bin_ || !Pattern::is_opcode_goto(*bpt.pc0) || (Pattern::lo_of(*bpt.pc0) & 0xC0) != 0xC0 || (Pattern::hi_of(*bpt.pc0) & 0xC0) != 0xC0)
        return bpt.pc1 = NULL;
      // loop over UTF-8 multibytes, checking linear case only (i.e. one wide char or a short range)
      for (int i = 0; i < 3; ++i)
      {
        jump = Pattern::index_of(*bpt.pc0);
        if (jump == Pattern::Const::HALT || pat_->opc_ + jump == bpt.pc0)
          return bpt.pc1 = NULL;
        if (jump == Pattern::Const::LONG)
          jump = Pattern::long_index_of(bpt.pc0[1]);
        const Pattern::Opcode *pc0 = pat_->opc_ + jump;
        const Pattern::Opcode *pc1 = pc0;
        while (!Pattern::is_opcode_goto(*pc1))
          ++pc1;
        if (Pattern::is_meta(Pattern::lo_of(*pc1)) || ((Pattern::lo_of(*pc1) & 0xC0) != 0x80 && (Pattern::hi_of(*pc1) & 0xC0) != 0x80))
          break;
        bpt.pc0 = pc0;
        bpt.pc1 = pc1;
      }
      jump = Pattern::index_of(*bpt.pc1);
      if (jump == Pattern::Const::HALT)
        return bpt.pc1 = NULL;
      if (jump == Pattern::Const::LONG)
        jump = Pattern::long_index_of(*++bpt.pc1);
      bpt.sub = bpt.alt;
      DBGLOG("Multibyte jump to %u", jump);
    }
    else if (jump == Pattern::Const::LONG)
    {
      jump = Pattern::long_index_of(*++bpt.pc1);
    }
    // restore errors
    err_ = bpt.err;
    // restore pos in the input
    pos_ = (txt_ - buf_) + bpt.len;
    // set ch to previous char before pos
    if (pos_ > 0)
      ch = static_cast<unsigned char>(buf_[pos_ - 1]);
    else
      ch = got_;
    // substitute or insert a pattern char in the text?
    if (bpt.sub)
    {
      // try substituting a pattern char for a mismatching char in the text
      DBGLOG("Substitute: jump to %u at pos %zu char %d (0x%x)", jump, pos_, ch, ch);
      int c = get();
      if (!bin_ && c != EOF)
      {
        // skip UTF-8 multibytes
        if (c >= 0xC0)
        {
          int n = (c >= 0xE0) + (c >= 0xF0);
          while (n-- >= 0)
            if ((c = get()) == EOF)
              break;
        }
        else
        {
          while ((peek() & 0xC0) == 0x80)
            if ((c = get()) == EOF)
              break;
        }
      }
      bpt.sub = false;
      bpt.pc1 += !bpt.alt;
    }
    else if (del_)
    {
      // try inserting a pattern char in the text to match a missing char in the text
      DBGLOG("Delete: jump to %u at pos %zu char %d (0x%x)", jump, pos_, ch, ch);
      bpt.sub = bpt.alt;
      ++bpt.pc1;
    }
    else
    {
      // no more alternatives
      return NULL;
    }
    return pat_->opc_ + jump;
  }
  /// Returns true if input fuzzy-matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  virtual size_t match(Method method) ///< Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH
    /// @returns nonzero if input matched the pattern
  {
    DBGLOG("BEGIN FuzzyMatcher::match()");
    reset_text();
    SaveState sst(ded_);
    len_ = 0; // split text length starts with 0
scan:
    txt_ = buf_ + cur_;
#if !defined(WITH_NO_INDENT)
    mrk_ = false;
    ind_ = pos_; // ind scans input in buf[] in newline() up to pos - 1
    col_ = 0; // count columns for indent matching
#endif
find:
    int ch = got_;
    bool bol = at_bol(); // at begin of line?
#if !defined(WITH_NO_INDENT)
redo:
#endif
    lap_.resize(0);
    cap_ = 0;
    bool nul = method == Const::MATCH;
    if (pat_->opc_ != NULL && (!opt_.W || at_wb()))
    {
      // skip to next line and keep searching if matching on anchor ^ and not at begin of line
      if (method == Const::FIND && pat_->bol_ && !bol)
        if (skip('\n'))
          goto scan;
      err_ = 0;
      uint8_t stack = 0;
      const Pattern::Opcode *pc = pat_->opc_;
      // backtrack point (DFA and relative position in the match)
      const Pattern::Opcode *pc0 = pc;
      size_t len0 = pos_ - (txt_ - buf_);
      while (true)
      {
        Pattern::Index back = Pattern::Const::IMAX; // where to jump back to
        size_t bpos = 0; // backtrack position in the input
        while (true)
        {
          Pattern::Opcode opcode = *pc;
          Pattern::Index jump;
          DBGLOG("Fetch: code[%zu] = 0x%08X", pc - pat_->opc_, opcode);
          if (!Pattern::is_opcode_goto(opcode))
          {
            // save backtrack point (DFA and relative position in the match)
            pc0 = pc;
            len0 = pos_ - (txt_ - buf_);
            switch (opcode >> 24)
            {
              case 0xFE: // TAKE
                int c;
                if (!opt_.W || (c = peek(), at_we(c, pos_)))
                {
                  cap_ = Pattern::long_index_of(opcode);
                  DBGLOG("Take: cap = %zu", cap_);
                  cur_ = pos_;
                }
                ++pc;
                continue;
              case 0xFD: // REDO
                cap_ = Const::REDO;
                DBGLOG("Redo");
                cur_ = pos_;
                ++pc;
                continue;
              case 0xFC: // TAIL
                {
                  Pattern::Lookahead la = Pattern::lookahead_of(opcode);
                  DBGLOG("Tail: %u", la);
                  if (lap_.size() > la && lap_[la] >= 0)
                    cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]); // mind the (new) gap
                  ++pc;
                  continue;
                }
              case 0xFB: // HEAD
                {
                  Pattern::Lookahead la = Pattern::lookahead_of(opcode);
                  DBGLOG("Head: lookahead[%u] = %zu", la, pos_ - (txt_ - buf_));
                  if (lap_.size() <= la)
                    lap_.resize(la + 1, -1);
                  lap_[la] = static_cast<int>(pos_ - (txt_ - buf_)); // mind the gap
                  ++pc;
                  continue;
                }
#if !defined(WITH_NO_INDENT)
              case Pattern::META_DED - Pattern::META_MIN:
                if (ded_ > 0)
                {
                  jump = Pattern::index_of(opcode);
                  if (jump == Pattern::Const::LONG)
                    jump = Pattern::long_index_of(pc[1]);
                  DBGLOG("Dedent ded = %zu", ded_); // unconditional dedent matching \j
                  nul = true;
                  pc = pat_->opc_ + jump;
                  continue;
                }
#endif
            }
            if (ch == EOF)
              break;
            ch = get();
            DBGLOG("Get: ch = %d (0x%x)", ch, ch);
            // count down to halt cycling on lone meta edges in repetitions, 5 should suffice
            int metas = 5;
            // to jump to longest sequence of matching metas
            jump = Pattern::Const::IMAX;
            while (true)
            {
              if (jump == Pattern::Const::IMAX || back == Pattern::Const::IMAX)
              {
                if (!Pattern::is_opcode_goto(opcode))
                {
                  // we no longer have to pass through all if jump and back are set
                  switch (opcode >> 24)
                  {
                    case 0xFE: // TAKE
                      if (!opt_.W || at_we(ch, pos_ - 1))
                      {
                        cap_ = Pattern::long_index_of(opcode);
                        DBGLOG("Take: cap = %zu", cap_);
                        cur_ = pos_;
                        if (ch != EOF)
                          --cur_; // must unget one char
                      }
                      opcode = *++pc;
                      continue;
                    case 0xFD: // REDO
                      cap_ = Const::REDO;
                      DBGLOG("Redo");
                      cur_ = pos_;
                      if (ch != EOF)
                        --cur_; // must unget one char
                      opcode = *++pc;
                      continue;
                    case 0xFC: // TAIL
                      {
                        Pattern::Lookahead la = Pattern::lookahead_of(opcode);
                        DBGLOG("Tail: %u", la);
                        if (lap_.size() > la && lap_[la] >= 0)
                          cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]); // mind the (new) gap
                        opcode = *++pc;
                        continue;
                      }
                    case 0xFB: // HEAD
                      opcode = *++pc;
                      continue;
#if !defined(WITH_NO_INDENT)
                    case Pattern::META_DED - Pattern::META_MIN:
                      DBGLOG("DED? %d", ch);
                      if (jump == Pattern::Const::IMAX && back == Pattern::Const::IMAX && bol && dedent())
                      {
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_IND - Pattern::META_MIN:
                      DBGLOG("IND? %d", ch);
                      if (jump == Pattern::Const::IMAX && back == Pattern::Const::IMAX && bol && indent())
                      {
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_UND - Pattern::META_MIN:
                      DBGLOG("UND");
                      if (mrk_)
                      {
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      mrk_ = false;
                      ded_ = 0;
                      opcode = *++pc;
                      continue;
#endif
                    case Pattern::META_EOB - Pattern::META_MIN:
                      DBGLOG("EOB? %d", ch);
                      if (metas > 0 && jump == Pattern::Const::IMAX && ch == EOF)
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_BOB - Pattern::META_MIN:
                      DBGLOG("BOB? %d", at_bob());
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_bob())
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_EOL - Pattern::META_MIN:
                      DBGLOG("EOL? %d", ch);
                      if (metas > 0 && jump == Pattern::Const::IMAX &&
                          (ch == EOF || ch == '\n' || (ch == '\r' && peek() == '\n')))
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_BOL - Pattern::META_MIN:
                      DBGLOG("BOL? %d", bol);
                      if (metas > 0 && jump == Pattern::Const::IMAX && bol)
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_EWE - Pattern::META_MIN:
                      DBGLOG("EWE? %d", at_ewe(ch));
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_ewe(ch))
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_BWE - Pattern::META_MIN:
                      DBGLOG("BWE? %d", at_bwe(ch));
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_bwe(ch))
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_EWB - Pattern::META_MIN:
                      DBGLOG("EWB? %d", at_ewb());
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_ewb())
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_BWB - Pattern::META_MIN:
                      DBGLOG("BWB? %d", at_bwb());
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_bwb())
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_NWE - Pattern::META_MIN:
                      DBGLOG("NWE? %d", at_nwe(ch));
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_nwe(ch))
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_NWB - Pattern::META_MIN:
                      DBGLOG("NWB? %d", at_nwb());
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_nwb())
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_WBE - Pattern::META_MIN:
                      DBGLOG("WBE? %d", at_wbe(ch));
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_wbe(ch))
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case Pattern::META_WBB - Pattern::META_MIN:
                      DBGLOG("WBB? %d", at_wbb());
                      if (metas > 0 && jump == Pattern::Const::IMAX && at_wbb())
                      {
                        --metas;
                        jump = Pattern::index_of(opcode);
                        if (jump == Pattern::Const::LONG)
                          jump = Pattern::long_index_of(*++pc);
                      }
                      opcode = *++pc;
                      continue;
                    case 0xFF: // LONG
                      opcode = *++pc;
                      continue;
                  }
                }
                else if (ch != EOF && !Pattern::is_opcode_halt(opcode))
                {
                  if (jump == Pattern::Const::IMAX)
                    break;
                  if (back == Pattern::Const::IMAX)
                  {
                    back = static_cast<Pattern::Index>(pc - pat_->opc_);
                    bpos = pos_ - (txt_ - buf_) - 1;
                    DBGLOG("Backtrack point: back = %u pos = %zu", back, bpos);
                  }
                }
              }
              if (jump == Pattern::Const::IMAX)
              {
                if (back != Pattern::Const::IMAX && bpos + 1 == pos_ - (txt_ - buf_))
                {
                  pc = pat_->opc_ + back;
                  opcode = *pc;
                  DBGLOG("Backtrack 1: back = %u pos = %zu ch = %d", back, pos_, ch);
                  back = Pattern::Const::IMAX;
                }
                break;
              }
              if (back == static_cast<Pattern::Index>(pc - pat_->opc_))
              {
                bpos = pos_ - (txt_ - buf_) - 1;
                DBGLOG("Backtrack update point: back = %u pos = %zu", back, bpos);
              }
              DBGLOG("Try jump = %u", jump);
              pc = pat_->opc_ + jump;
              opcode = *pc;
              jump = Pattern::Const::IMAX;
            }
            if (ch == EOF)
              break;
          }
          else
          {
            if (Pattern::is_opcode_halt(opcode))
            {
              if (back != Pattern::Const::IMAX)
              {
                pos_ = (txt_ - buf_) + bpos;
                pc = pat_->opc_ + back;
                DBGLOG("Backtrack 2: back = %u pos = %zu ch = %d", back, pos_, ch);
                back = Pattern::Const::IMAX;
                continue;
              }
              break;
            }
            if (ch == EOF)
              break;
            ch = get();
            DBGLOG("Get: ch = %d (0x%x) at pos %zu", ch, ch, pos_ - 1);
            if (bin_ || (ch & 0xC0) != 0x80 || ch == EOF)
            {
              // save backtrack point (DFA and relative position in the match)
              pc0 = pc;
              len0 = pos_ - (txt_ - buf_);
            }
            if (ch == EOF)
              break;
          }
          {
            Pattern::Opcode lo = ch << 24;
            Pattern::Opcode hi = lo | 0x00FFFFFF;
unrolled:
            if (hi < opcode || lo > (opcode << 8))
            {
              opcode = *++pc;
              if (hi < opcode || lo > (opcode << 8))
              {
                opcode = *++pc;
                if (hi < opcode || lo > (opcode << 8))
                {
                  opcode = *++pc;
                  if (hi < opcode || lo > (opcode << 8))
                  {
                    opcode = *++pc;
                    if (hi < opcode || lo > (opcode << 8))
                    {
                      opcode = *++pc;
                      if (hi < opcode || lo > (opcode << 8))
                      {
                        opcode = *++pc;
                        if (hi < opcode || lo > (opcode << 8))
                        {
                          opcode = *++pc;
                          if (hi < opcode || lo > (opcode << 8))
                          {
                            opcode = *++pc;
                            goto unrolled;
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          jump = Pattern::index_of(opcode);
          if (jump == 0)
          {
            // loop back to start state w/o full match: advance to avoid backtracking
            if (cap_ == 0 && method == Const::FIND)
            {
              if (cur_ + 1 == pos_)
              {
                // matched one char in a loop, do not backtrack here
                ++cur_;
              }
              else
              {
                // check each char in buf_[cur_+1..pos_-1] if it is a starting char, if not then increase cur_
                while (cur_ + 1 < pos_ && !pat_->fst_.test(static_cast<uint8_t>(buf_[cur_ + 1])))
                  ++cur_;
              }
            }
          }
          else if (jump >= Pattern::Const::LONG)
          {
            if (jump == Pattern::Const::HALT)
            {
              if (back != Pattern::Const::IMAX)
              {
                pc = pat_->opc_ + back;
                pos_ = (txt_ - buf_) + bpos;
                DBGLOG("Backtrack 3: back = %u pos = %zu ch = %d", back, pos_, ch);
                back = Pattern::Const::IMAX;
                continue;
              }
              break;
            }
            jump = Pattern::long_index_of(pc[1]);
          }
          pc = pat_->opc_ + jump;
        }
        // exit fuzzy loop if nothing consumed
        if (pos_ == static_cast<size_t>(txt_ + len_ - buf_))
          break;
        // match, i.e. cap_ > 0?
        if (method == Const::MATCH)
        {
          // exit fuzzy loop if fuzzy match succeeds till end of input when insertions are allowed
          if (cap_ > 0)
          {
            if (ch != EOF && ins_)
            {
              // text insertions are allowed
              while (err_ < max_)
              {
                // reached the end?
                ch = get();
                if (ch == EOF)
                  break;
                ++err_;
                // skip one (multibyte) char
                if (!bin_ && ch >= 0xC0)
                {
                  int n = (ch >= 0xE0) + (ch >= 0xF0);
                  while (n-- >= 0)
                    if ((ch = get()) == EOF)
                      break;
                }
              }
            }
            if (ch == EOF || ins_)
            {
              // reached the end?
              if (at_end())
              {
                DBGLOG("Match pos = %zu", pos_);
                set_current(pos_);
                break;
              }
            }
            cap_ = 0;
          }
        }
        else
        {
          // exit fuzzy loop if match or if first char mismatched
          if (cap_ > 0 || pos_ == static_cast<size_t>(txt_ + len_ - buf_ + 1))
            break;
        }
        // no match, use fuzzy matching with max error
        if (ch == '\0' || ch == '\n' || ch == EOF)
        {
          // do not try to fuzzy match NUL, LF, or EOF
          if (err_ < max_ && del_)
          {
            ++err_;
            // set backtrack point to insert pattern char only, not substitute, if pc0 os a different point than the last
            if (stack == 0 || bpt_[stack - 1].pc0 != pc0)
            {
              point(bpt_[stack++], pc0, len0, false, ch == EOF);
              DBGLOG("Point[%u] at %zu pos %zu (\\0|\\nEOF)", stack - 1, pc0 - pat_->opc_, pos_ - 1);
            }
          }
          else
          {
            // backtrack to try insertion or substitution of pattern char
            pc = NULL;
            while (stack > 0 && pc == NULL)
            {
              pc = backtrack(bpt_[stack - 1], ch);
              if (pc == NULL)
                --stack;
            }
            // exhausted all backtracking points?
            if (pc == NULL)
              break;
          }
        }
        else
        {
          if (err_ < max_)
          {
            ++err_;
            if (del_ || sub_)
            {
              // set backtrack point if pc0 is a different point than the last
              if (stack == 0 || bpt_[stack - 1].pc0 != pc0)
              {
                point(bpt_[stack++], pc0, len0);
                DBGLOG("Point[%u] at %zu pos %zu", stack - 1, pc0 - pat_->opc_, pos_ - 1);
              }
            }
            if (ins_)
            {
              if (!bin_)
              {
                // try pattern char deletion (text insertion): skip one (multibyte) char then rerun opcode at pc0
                if (ch >= 0xC0)
                {
                  int n = (ch >= 0xE0) + (ch >= 0xF0);
                  while (n-- >= 0)
                    if ((ch = get()) == EOF)
                      break;
                }
                else
                {
                  while ((peek() & 0xC0) == 0x80)
                    if ((ch = get()) == EOF)
                      break;
                }
              }
              pc = pc0;
              DBGLOG("Insert: %d (0x%x) at pos %zu", ch, ch, pos_ - 1);
            }
          }
          else
          {
            // backtrack to try insertion or substitution of pattern char
            pc = NULL;
            while (stack > 0 && pc == NULL)
            {
              pc = backtrack(bpt_[stack - 1], ch);
              if (pc == NULL)
                --stack;
            }
            // exhausted all backtracking points?
            if (pc == NULL)
              break;
          }
        }
      }
    }
    // if fuzzy find/split with errors then perform a second pass ahead of this match to check for an exact match
    if (cap_ > 0 && err_ > 0 && !sst.use && (method == Const::FIND || method == Const::SPLIT))
    {
      // this part is based on advance() in matcher.cpp, limited to advancing ahead till the one of the first pattern char(s) match excluding \n
      size_t loc = txt_ - buf_ + 1;
      const char *s = buf_ + loc;
      const char *e = static_cast<const char*>(std::memchr(s, '\n', cur_ - loc));
      if (e == NULL)
        e = buf_ + cur_;
      if (pat_->len_ == 0)
      {
        if (pat_->min_ > 0)
        {
          while (s < e && !pat_->fst_.test(static_cast<uint8_t>(*s)))
            ++s;
          if (s < e)
          {
            loc = s - buf_;
            sst.use = true;
            sst.loc = loc;
            sst.cap = cap_;
            sst.txt = txt_ - buf_;
            sst.cur = cur_;
            sst.pos = pos_;
            size_t tmp = ded_;
            ded_ = sst.ded;
            sst.ded = tmp;
            sst.mrk = mrk_;
            sst.err = err_;
            set_current(loc);
            goto scan;
          }
        }
      }
      else if (s < e)
      {
        s = static_cast<const char*>(std::memchr(s, *pat_->chr_, e - s));
        if (s != NULL)
        {
          loc = s - buf_;
          sst.use = true;
          sst.loc = loc;
          sst.cap = cap_;
          sst.txt = txt_ - buf_;
          sst.cur = cur_;
          sst.pos = pos_;
          size_t tmp = ded_;
          ded_ = sst.ded;
          sst.ded = tmp;
          sst.mrk = mrk_;
          sst.err = err_;
          set_current(loc);
          goto scan;
        }
      }
    }
    else if (sst.use && (cap_ == 0 || err_ >= sst.err))
    {
      // if the buffer was shifted then cur_, pos_ and txt_ are no longer at the same location in the buffer, we must adjust for this
      size_t loc = txt_ - buf_;
      size_t shift = sst.loc - loc;
      cap_ = sst.cap;
      cur_ = sst.cur - shift;
      pos_ = sst.pos - shift;
      ded_ = sst.ded;
      mrk_ = sst.mrk;
      err_ = sst.err;
      txt_ = buf_ + sst.txt - shift;
    }
    else if (sst.use && cap_ > 0 && method == Const::SPLIT)
    {
      size_t loc = txt_ - buf_;
      size_t shift = sst.loc - loc;
      len_ = loc - sst.txt + shift;
    }
#if !defined(WITH_NO_INDENT)
    if (mrk_ && cap_ != Const::REDO)
    {
      if (col_ > 0 && (tab_.empty() || tab_.back() < col_))
      {
        DBGLOG("Set new stop: tab_[%zu] = %zu", tab_.size(), col_);
        tab_.push_back(col_);
      }
      else if (!tab_.empty() && tab_.back() > col_)
      {
        size_t n;
        for (n = tab_.size() - 1; n > 0; --n)
          if (tab_.at(n - 1) <= col_)
            break;
        ded_ += tab_.size() - n;
        DBGLOG("Dedents: ded = %zu tab_ = %zu", ded_, tab_.size());
        tab_.resize(n);
        // adjust stop when indents are not aligned (Python would give an error)
        if (n > 0)
          tab_.back() = col_;
      }
    }
    if (ded_ > 0)
    {
      DBGLOG("Dedents: ded = %zu", ded_);
      if (col_ == 0 && bol)
      {
        ded_ += tab_.size();
        tab_.resize(0);
        DBGLOG("Rescan for pending dedents: ded = %zu", ded_);
        pos_ = ind_;
        // avoid looping, match \j exactly
        bol = false;
        goto redo;
      }
      --ded_;
    }
#endif
    if (method == Const::SPLIT)
    {
      DBGLOG("Split: len = %zu cap = %zu cur = %zu pos = %zu end = %zu txt-buf = %zu eob = %d got = %d", len_, cap_, cur_, pos_, end_, txt_-buf_, (int)eof_, got_);
      if (cap_ == 0 || (cur_ == static_cast<size_t>(txt_ - buf_) && !at_bob()))
      {
        if (!hit_end() && (txt_ + len_ < buf_ + end_ || peek() != EOF))
        {
          ++len_;
          DBGLOG("Split continue: len = %zu", len_);
          set_current(++cur_);
          goto find;
        }
        if (got_ != Const::EOB)
          cap_ = Const::EMPTY;
        else
          cap_ = 0;
        set_current(end_);
        got_ = Const::EOB;
        DBGLOG("Split at eof: cap = %zu txt = '%s' len = %zu", cap_, std::string(txt_, len_).c_str(), len_);
        DBGLOG("END FuzzyMatcher::match()");
        return cap_;
      }
      if (cur_ == 0 && at_bob() && at_end())
      {
        cap_ = Const::EMPTY;
        got_ = Const::EOB;
      }
      else
      {
        set_current(cur_);
      }
      DBGLOG("Split: txt = '%s' len = %zu", std::string(txt_, len_).c_str(), len_);
      DBGLOG("END FuzzyMatcher::match()");
      return cap_;
    }
    if (cap_ == 0)
    {
      if (method == Const::FIND)
      {
        if (!at_end())
        {
          // fuzzy search with find() can safely advance on a single prefix char of the regex
          if (cur_ < pos_)
          {
            // this part is based on advance() in matcher.cpp, limited to advancing ahead till the one of the first pattern char(s) match
            size_t loc = cur_ + 1;
            if (pat_->len_ == 0)
            {
              if (pat_->min_ > 0)
              {
                while (true)
                {
                  const char *s = buf_ + loc;
                  const char *e = buf_ + end_;
                  while (s < e && !pat_->fst_.test(static_cast<uint8_t>(*s)))
                    ++s;
                  if (s < e)
                  {
                    loc = s - buf_;
                    set_current(loc);
                    goto scan;
                  }
                  loc = e - buf_;
                  set_current_and_peek_more(loc);
                  loc = cur_;
                  if (loc >= end_ && eof_)
                    break;
                }
              }
            }
            else
            {
              while (true)
              {
                const char *s = buf_ + loc;
                const char *e = buf_ + end_;
                s = static_cast<const char*>(std::memchr(s, *pat_->chr_, e - s));
                if (s != NULL)
                {
                  loc = s - buf_;
                  set_current(loc);
                  goto scan;
                }
                loc = e - buf_;
                set_current_and_peek_more(loc);
                loc = cur_;
                if (loc + pat_->len_ > end_ && eof_)
                  break;
              }
            }
          }
        }
        txt_ = buf_ + cur_;
      }
      else
      {
        // SCAN and MATCH: no match: backup to begin of unmatched text to report as error
        cur_ = txt_ - buf_;
      }
    }
    len_ = cur_ - (txt_ - buf_);
    if (len_ == 0 && !nul)
    {
      DBGLOG("Empty or no match cur = %zu pos = %zu end = %zu", cur_, pos_, end_);
      pos_ = cur_;
      if (at_end())
      {
        set_current(cur_);
        DBGLOG("Reject empty match at EOF");
        cap_ = 0;
      }
      else if (method == Const::FIND)
      {
        DBGLOG("Reject empty match and continue?");
        // skip one char to keep searching
        set_current(++cur_);
        // allow FIND with "N" to match an empty line, with ^$ etc.
        if (cap_ == 0 || !opt_.N || (!bol && (ch == '\n' || (ch == '\r' && peek() == '\n'))))
          goto scan;
        DBGLOG("Accept empty match");
      }
      else
      {
        set_current(cur_);
        DBGLOG("Reject empty match");
        cap_ = 0;
      }
    }
    else if (len_ == 0 && cur_ == end_)
    {
      DBGLOG("Hit end: got = %d", got_);
      if (cap_ == Const::REDO && !opt_.A)
        cap_ = 0;
    }
    else
    {
      set_current(cur_);
      if (len_ > 0 && cap_ == Const::REDO && !opt_.A)
      {
        DBGLOG("Ignore accept and continue: len = %zu", len_);
        len_ = 0;
        if (method != Const::MATCH)
          goto scan;
        cap_ = 0;
      }
    }
    DBGLOG("Return: cap = %zu txt = '%s' len = %zu pos = %zu got = %d", cap_, std::string(txt_, len_).c_str(), len_, pos_, got_);
    DBGLOG("END match()");
    return cap_;
  }
  std::vector<BacktrackPoint> bpt_; ///< vector of backtrack points, max_ size
  uint8_t max_;                     ///< max errors
  uint8_t err_;                     ///< accumulated edit distance (not guaranteed minimal)
  bool ins_;                        ///< fuzzy match permits inserted chars (extra chars in the input)
  bool del_;                        ///< fuzzy match permits deleted chars (missing chars in the input)
  bool sub_;                        ///< fuzzy match permits substituted chars
  bool bin_;                        ///< fuzzy match bytes, not UTF-8 multibyte encodings
};

} // namespace reflex

#endif
