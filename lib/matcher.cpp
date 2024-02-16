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
@file      matcher.cpp, matcher_avx2.cpp, matcher_avx512bw.cpp
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#if defined(COMPILE_AVX512BW) && !defined(HAVE_AVX512BW)

// appease ranlib "has no symbols"
void matcher_not_compiled_with_avx512bw() { }

#elif defined(COMPILE_AVX2) && !defined(HAVE_AVX2) && !defined(HAVE_AVX512BW)

// appease ranlib "has no symbols"
void matcher_not_compiled_with_avx2() { }

#else

#include <reflex/matcher.h>

namespace reflex {

/*
   The simd_match_avx512bw() and simd_match_avx2() methods are AVX-optimized
   versions of the match() method.  To compile these methods separately with
   the appropriate compilation flags, this file is copied to
   matcher_avx512bw.cpp and matcher_avx2.cpp then compiled with -mavx512bw
   -DCOMPILE_AVX512BW -DHAVE_AVX512BW and with -mavx2 -DCOMPILE_AVX2
   -DHAVE_AVX2, respectively.  Likewise, the simd_advance_avx512bw() and
   simd_advance_avx2() methods are optimized versions and separately compiled.
   This approach is preferred over maintaining three separate copies of source
   code files with these methods that only slightly differ.  On the other hand,
   combining these versions into one source file means more #if branches.

   If -DHAVE_AVX512BW is not defined, -DCOMPILE_AVX512BW has no effect.
   Likewise, if -DHAVE_AVX2 is not defined, -DCOMPILE_AVX2 has no effect.
*/

#if defined(COMPILE_AVX512BW)
/// Compile an optimized AVX512BW version defined in matcher_avx2.cpp
size_t Matcher::simd_match_avx512bw(Method method)
{
#elif defined(COMPILE_AVX2)
/// Compile an optimized AVX2 version defined in matcher_avx512bw.cpp
size_t Matcher::simd_match_avx2(Method method)
{
#else
/// Returns true if input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
size_t Matcher::match(Method method)
{
  DBGLOG("BEGIN Matcher::match()");
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
  if (have_HW_AVX512BW())
    return simd_match_avx512bw(method);
  if (have_HW_AVX2())
    return simd_match_avx2(method);
#elif defined(HAVE_AVX2)
  if (have_HW_AVX2())
    return simd_match_avx2(method);
#endif
#endif
  reset_text();
  len_ = 0;         // split text length starts with 0
  anc_ = false;     // no word boundary anchor found and applied
  size_t retry = 0; // retry regex match at lookback positions for predicted matches
scan:
  txt_ = buf_ + cur_;
#if !defined(WITH_NO_INDENT)
  mrk_ = false;
  ind_ = pos_; // ind scans input in buf[] in newline() up to pos - 1
  col_ = 0; // count columns for indent matching
#endif
find:
  int c1 = got_;
  bool bol = at_bol(); // at begin of line?
#if !defined(WITH_NO_CODEGEN)
  if (pat_->fsm_ != NULL)
    fsm_.c1 = c1;
#endif
#if !defined(WITH_NO_INDENT)
redo:
#endif
  lap_.resize(0);
  cap_ = 0;
  bool nul = method == Const::MATCH;
#if !defined(WITH_NO_CODEGEN)
  if (pat_->fsm_ != NULL)
  {
    DBGLOG("FSM code %p", pat_->fsm_);
    fsm_.bol = bol;
    fsm_.nul = nul;
    pat_->fsm_(*this);
    nul = fsm_.nul;
    c1 = fsm_.c1;
  }
  else
#endif
  if (pat_->opc_ != NULL)
  {
    const Pattern::Opcode *pc = pat_->opc_;
    Pattern::Index back = Pattern::Const::IMAX; // where to jump back to
    size_t bpos = 0; // backtrack position in the input
    while (true)
    {
      Pattern::Index jump;
      Pattern::Opcode opcode = *pc;
      DBGLOG("Fetch: code[%zu] = 0x%08X", pc - pat_->opc_, opcode);
      if (!Pattern::is_opcode_goto(opcode))
      {
        switch (opcode >> 24)
        {
          case 0xFE: // TAKE
            cap_ = Pattern::long_index_of(opcode);
            cur_ = pos_;
            ++pc;
            DBGLOG("Take: cap = %zu", cap_);
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
        if (c1 == EOF)
          break;
        int c0 = c1;
        c1 = get();
        DBGLOG("Get: c1 = %d", c1);
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
                  cap_ = Pattern::long_index_of(opcode);
                  cur_ = pos_;
                  if (c1 != EOF)
                    --cur_; // must unget one char
                  opcode = *++pc;
                  DBGLOG("Take: cap = %zu", cap_);
                  continue;
                case 0xFD: // REDO
                  cap_ = Const::REDO;
                  DBGLOG("Redo");
                  cur_ = pos_;
                  if (c1 != EOF)
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
                  DBGLOG("DED? %d", c1);
                  if (jump == Pattern::Const::IMAX && back == Pattern::Const::IMAX && bol && dedent())
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_IND - Pattern::META_MIN:
                  DBGLOG("IND? %d", c1);
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
                  DBGLOG("EOB? %d", c1);
                  if (jump == Pattern::Const::IMAX && c1 == EOF)
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_BOB - Pattern::META_MIN:
                  DBGLOG("BOB? %d", at_bob());
                  if (jump == Pattern::Const::IMAX && at_bob())
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_EOL - Pattern::META_MIN:
                  DBGLOG("EOL? %d", c1);
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && (c1 == EOF || c1 == '\n' || (c1 == '\r' && peek() == '\n')))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_BOL - Pattern::META_MIN:
                  DBGLOG("BOL? %d", bol);
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && bol)
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_EWE - Pattern::META_MIN:
                  DBGLOG("EWE? %d %d %d", c0, c1, isword(c0) && !isword(c1));
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && (isword(c0) || opt_.W) && !isword(c1))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_BWE - Pattern::META_MIN:
                  DBGLOG("BWE? %d %d %d", c0, c1, !isword(c0) && isword(c1));
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && !isword(c0) && isword(c1))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_EWB - Pattern::META_MIN:
                  DBGLOG("EWB? %d", at_eow());
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && isword(got_) &&
                      !isword(static_cast<unsigned char>(txt_[len_])))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_BWB - Pattern::META_MIN:
                  DBGLOG("BWB? %d", at_bow());
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && !isword(got_) &&
                      (opt_.W || isword(static_cast<unsigned char>(txt_[len_]))))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_NWE - Pattern::META_MIN:
                  DBGLOG("NWE? %d %d %d", c0, c1, isword(c0) == isword(c1));
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && isword(c0) == isword(c1))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_NWB - Pattern::META_MIN:
                  DBGLOG("NWB? %d %d", at_bow(), at_eow());
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX &&
                      isword(got_) == isword(static_cast<unsigned char>(txt_[len_])))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_WBE - Pattern::META_MIN:
                  DBGLOG("WBE? %d %d %d", c0, c1, isword(c0) != isword(c1));
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX && isword(c0) != isword(c1))
                  {
                    jump = Pattern::index_of(opcode);
                    if (jump == Pattern::Const::LONG)
                      jump = Pattern::long_index_of(*++pc);
                  }
                  opcode = *++pc;
                  continue;
                case Pattern::META_WBB - Pattern::META_MIN:
                  DBGLOG("WBB? %d %d", at_bow(), at_eow());
                  anc_ = true;
                  if (jump == Pattern::Const::IMAX &&
                      isword(got_) != isword(static_cast<unsigned char>(txt_[len_])))
                  {
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
            else if (c1 != EOF && !Pattern::is_opcode_halt(opcode))
            {
              if (jump == Pattern::Const::IMAX)
                break;
              if (back == Pattern::Const::IMAX)
              {
                back = static_cast<Pattern::Index>(pc - pat_->opc_);
                bpos = pos_ - (txt_ - buf_) - 1;
                DBGLOG("Backtrack point: back = %u pos = %zu", back, bpos);
              }
              pc = pat_->opc_ + jump;
              opcode = *pc;
            }
          }
          if (jump == Pattern::Const::IMAX)
          {
            if (back != Pattern::Const::IMAX)
            {
              pc = pat_->opc_ + back;
              opcode = *pc;
              back = Pattern::Const::IMAX;
            }
            break;
          }
          DBGLOG("Try jump = %u", jump);
          if (back == Pattern::Const::IMAX)
          {
            back = static_cast<Pattern::Index>(pc - pat_->opc_);
            bpos = pos_ - (txt_ - buf_) - 1;
            DBGLOG("Backtrack point: back = %u pos = %zu", back, bpos);
          }
          pc = pat_->opc_ + jump;
          opcode = *pc;
          jump = Pattern::Const::IMAX;
        }
        if (c1 == EOF)
          break;
      }
      else
      {
        if (Pattern::is_opcode_halt(opcode))
        {
          if (cap_ == 0 && back != Pattern::Const::IMAX)
          {
            pos_ = (txt_ - buf_) + bpos;
            pc = pat_->opc_ + back;
            DBGLOG("Backtrack: back = %u pos = %zu c1 = %d", back, pos_, c1);
            back = Pattern::Const::IMAX;
            continue;
          }
          break;
        }
        if (c1 == EOF)
          break;
        c1 = get();
        DBGLOG("Get: c1 = %d (0x%x) at pos %zu", c1, c1, pos_ - 1);
        if (c1 == EOF)
          break;
      }
      Pattern::Opcode lo = c1 << 24;
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
      jump = Pattern::index_of(opcode);
      if (jump == 0)
      {
        // loop back to start state w/o full match: advance to avoid backtracking, not used for lookback
        if (cap_ == 0 && pos_ > cur_ && method == Const::FIND)
        {
          // use bit_[] to check each char in buf_[cur_+1..pos_-1] if it is a starting char, if not then increase cur_
          while (++cur_ < pos_ && !pat_->fst_.test(static_cast<uint8_t>(buf_[cur_])))
            if (retry > 0)
              --retry;
        }
      }
      else if (jump >= Pattern::Const::LONG)
      {
        if (jump == Pattern::Const::HALT)
        {
          if (cap_ == 0 && back != Pattern::Const::IMAX)
          {
            pc = pat_->opc_ + back;
            pos_ = (txt_ - buf_) + bpos;
            DBGLOG("Backtrack: back = %u pos = %zu c1 = %d", back, pos_, c1);
            back = Pattern::Const::IMAX;
            continue;
          }
          break;
        }
        jump = Pattern::long_index_of(pc[1]);
      }
      pc = pat_->opc_ + jump;
    }
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
      // adjust stop when indents are not aligned (Python would give an error though)
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
      DBGLOG("END Matcher::match()");
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
    DBGLOG("END Matcher::match()");
    return cap_;
  }
  if (cap_ == 0)
  {
    if (method == Const::FIND)
    {
      if (!at_end())
      {
        // when looking back from a predicted match, advance by one position and retry a match
        if (retry > 0)
        {
          --retry;
          set_current(++cur_);
          anc_ = false;
          DBGLOG("Find: try next pos %zu", cur_);
          goto scan;
        }
        //
        if (anc_)
        {
          cur_ = txt_ - buf_; // reset current to pattern start when a word boundary was encountered
          anc_ = false;
        }
        if (pos_ > cur_) // if we didn't fail on META alone
        {
          if (
#if defined(COMPILE_AVX512BW)
              simd_advance_avx512bw()
#elif defined(COMPILE_AVX2)
              simd_advance_avx2()
#else
              advance()
#endif
              )
          {
            if (pat_->lbk_ > 0)
            {
              // look back and try/retry matching, over lookback chars (never includes \n)
              size_t n = pat_->lbk_ == 0xffff ? SIZE_MAX : pat_->lbk_;
              const char *s = buf_ + cur_;
              const char *e = txt_;
              while (n-- > 0 && --s > e && pat_->cbk_.test(static_cast<unsigned char>(*s)))
                ++retry;
              cur_ -= retry;
              // not at or before the begin of the last match
              s = buf_ + cur_;
              // don't retry at minimal look back distances that are too short for pattern to match
              if (retry > pat_->lbm_)
                retry -= pat_->lbm_;
              else
                retry = 0;
              set_current(cur_);
              DBGLOG("Find: look back %zu to pos %zu", retry, cur_);
              goto scan;
            }
            if (!pat_->one_)
              goto scan;
            txt_ = buf_ + cur_;
            len_ = pat_->len_;
            set_current(cur_ + len_);
            return cap_ = 1;
          }
        }
        txt_ = buf_ + cur_;
      }
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
      // allow FIND with "N" to match an empty line, with ^$ etc.
      if (cap_ == 0 || !opt_.N)
      {
        // if we found an empty match, we keep looking for non-empty matches when "N" is off
        if (cap_ != 0)
        {
          if (
#if defined(COMPILE_AVX512BW)
              simd_advance_avx512bw()
#elif defined(COMPILE_AVX2)
              simd_advance_avx2()
#else
              advance()
#endif
             )
          {
            goto scan;
          }
          set_current(++cur_);
          // at end of input, no matches remain
          cap_ = 0;
        }
        else
        {
          // advance one char to keep searching
          set_current(++cur_);
          goto scan;
        }
      }
      else
      {
        // advance one char to keep searching at the next character position when we return
        set_current(++cur_);
      }
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

#if defined(COMPILE_AVX512BW)
/// Compile an optimized AVX512BW version defined in matcher_avx512bw.cpp
bool Matcher::simd_advance_avx512bw()
{
#elif defined(COMPILE_AVX2)
/// Compile an optimized AVX2 version defined in matcher_avx2.cpp
bool Matcher::simd_advance_avx2()
{
#else
/// advance input cursor position after mismatch to align input for the next match
bool Matcher::advance()
{
#endif
  size_t loc = cur_ + 1;
  size_t min = pat_->min_;
  const Pattern::Pred *pma = pat_->pma_;
  const Pattern::Pred *pmh = pat_->pmh_;
  if (pat_->len_ == 0)
  {
    if (min == 0)
    {
      // if "N" is on (non-empty pattern matches only), then there is nothing to match
      if (opt_.N)
        return false;
      // if "N" is off, then match an empty-matching pattern as if non-empty
      min = 1;
    }
    if (loc + min > end_)
    {
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + min > end_)
        return false;
    }
    // look for a needle
    if (pat_->pin_ == 1)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
#if defined(COMPILE_AVX512BW) || defined(COMPILE_AVX2)
      __m256i vlcp = _mm256_set1_epi8(chr[0]);
      __m256i vlcs = _mm256_set1_epi8(chr[1]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs, vstrlcs);
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
#elif defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
      __m128i vlcp = _mm_set1_epi8(chr[0]);
      __m128i vlcs = _mm_set1_epi8(chr[1]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs, vstrlcs);
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
#elif defined(HAVE_NEON)
      uint8x16_t vlcp = vdupq_n_u8(chr[0]);
      uint8x16_t vlcs = vdupq_n_u8(chr[1]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 = vceqq_u8(vlcp, vstrlcp);
          uint8x16_t vmasklcs8 = vceqq_u8(vlcs, vstrlcs);
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
#else
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_;
        if (s < e && (s = static_cast<const char*>(std::memchr(s, chr[0], e - s))) != NULL)
        {
          s -= lcp;
          loc = s - buf_;
          set_current(loc);
          if (min >= 4)
          {
            if (s + min > e || (s[lcs] == chr[1] && Pattern::predict_match(pmh, s, min)))
              return true;
          }
          else
          {
            if (s > e - 4 || Pattern::predict_match(pma, s) == 0)
              return true;
          }
          ++loc;
        }
        else
        {
          loc = e - buf_;
          set_current_and_peek_more(loc - 1);
          loc = cur_ + 1;
          if (loc + min > end_)
            return false;
        }
      }
#endif
    }
#if defined(COMPILE_AVX512BW) || defined(COMPILE_AVX2)
    // look for needles
    else if (pat_->pin_ == 2)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[2]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[3]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 3)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[3]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[4]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[5]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 4)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[4]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[5]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[6]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[7]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 5)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcp4 = _mm256_set1_epi8(chr[4]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[5]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[6]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[7]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[8]);
      __m256i vlcs4 = _mm256_set1_epi8(chr[9]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 6)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcp4 = _mm256_set1_epi8(chr[4]);
      __m256i vlcp5 = _mm256_set1_epi8(chr[5]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[6]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[7]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[8]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[9]);
      __m256i vlcs4 = _mm256_set1_epi8(chr[10]);
      __m256i vlcs5 = _mm256_set1_epi8(chr[11]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 7)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcp4 = _mm256_set1_epi8(chr[4]);
      __m256i vlcp5 = _mm256_set1_epi8(chr[5]);
      __m256i vlcp6 = _mm256_set1_epi8(chr[6]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[7]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[8]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[9]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[10]);
      __m256i vlcs4 = _mm256_set1_epi8(chr[11]);
      __m256i vlcs5 = _mm256_set1_epi8(chr[12]);
      __m256i vlcs6 = _mm256_set1_epi8(chr[13]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 8)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcp4 = _mm256_set1_epi8(chr[4]);
      __m256i vlcp5 = _mm256_set1_epi8(chr[5]);
      __m256i vlcp6 = _mm256_set1_epi8(chr[6]);
      __m256i vlcp7 = _mm256_set1_epi8(chr[7]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[8]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[9]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[10]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[11]);
      __m256i vlcs4 = _mm256_set1_epi8(chr[12]);
      __m256i vlcs5 = _mm256_set1_epi8(chr[13]);
      __m256i vlcs6 = _mm256_set1_epi8(chr[14]);
      __m256i vlcs7 = _mm256_set1_epi8(chr[15]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp7, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs7, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 16)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m256i vlcp0 = _mm256_set1_epi8(chr[0]);
      __m256i vlcp1 = _mm256_set1_epi8(chr[1]);
      __m256i vlcp2 = _mm256_set1_epi8(chr[2]);
      __m256i vlcp3 = _mm256_set1_epi8(chr[3]);
      __m256i vlcp4 = _mm256_set1_epi8(chr[4]);
      __m256i vlcp5 = _mm256_set1_epi8(chr[5]);
      __m256i vlcp6 = _mm256_set1_epi8(chr[6]);
      __m256i vlcp7 = _mm256_set1_epi8(chr[7]);
      __m256i vlcp8 = _mm256_set1_epi8(chr[8]);
      __m256i vlcp9 = _mm256_set1_epi8(chr[9]);
      __m256i vlcpa = _mm256_set1_epi8(chr[10]);
      __m256i vlcpb = _mm256_set1_epi8(chr[11]);
      __m256i vlcpc = _mm256_set1_epi8(chr[12]);
      __m256i vlcpd = _mm256_set1_epi8(chr[13]);
      __m256i vlcpe = _mm256_set1_epi8(chr[14]);
      __m256i vlcpf = _mm256_set1_epi8(chr[15]);
      __m256i vlcs0 = _mm256_set1_epi8(chr[16]);
      __m256i vlcs1 = _mm256_set1_epi8(chr[17]);
      __m256i vlcs2 = _mm256_set1_epi8(chr[18]);
      __m256i vlcs3 = _mm256_set1_epi8(chr[19]);
      __m256i vlcs4 = _mm256_set1_epi8(chr[20]);
      __m256i vlcs5 = _mm256_set1_epi8(chr[21]);
      __m256i vlcs6 = _mm256_set1_epi8(chr[22]);
      __m256i vlcs7 = _mm256_set1_epi8(chr[23]);
      __m256i vlcs8 = _mm256_set1_epi8(chr[24]);
      __m256i vlcs9 = _mm256_set1_epi8(chr[25]);
      __m256i vlcsa = _mm256_set1_epi8(chr[26]);
      __m256i vlcsb = _mm256_set1_epi8(chr[27]);
      __m256i vlcsc = _mm256_set1_epi8(chr[28]);
      __m256i vlcsd = _mm256_set1_epi8(chr[29]);
      __m256i vlcse = _mm256_set1_epi8(chr[30]);
      __m256i vlcsf = _mm256_set1_epi8(chr[31]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 32)
        {
          __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
          __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp);
          __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp7, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp8, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp9, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpa, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpb, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpc, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpd, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpe, vstrlcp));
          veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpf, vstrlcp));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs7, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs8, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs9, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsa, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsb, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsc, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsd, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcse, vstrlcs));
          veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsf, vstrlcs));
          uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 32;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 31 > end_)
          break;
      }
    }
#elif defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
    // look for needles
    else if (pat_->pin_ == 2)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcs0 = _mm_set1_epi8(chr[2]);
      __m128i vlcs1 = _mm_set1_epi8(chr[3]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 3)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcs0 = _mm_set1_epi8(chr[3]);
      __m128i vlcs1 = _mm_set1_epi8(chr[4]);
      __m128i vlcs2 = _mm_set1_epi8(chr[5]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 4)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcp3 = _mm_set1_epi8(chr[3]);
      __m128i vlcs0 = _mm_set1_epi8(chr[4]);
      __m128i vlcs1 = _mm_set1_epi8(chr[5]);
      __m128i vlcs2 = _mm_set1_epi8(chr[6]);
      __m128i vlcs3 = _mm_set1_epi8(chr[7]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 5)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcp3 = _mm_set1_epi8(chr[3]);
      __m128i vlcp4 = _mm_set1_epi8(chr[4]);
      __m128i vlcs0 = _mm_set1_epi8(chr[5]);
      __m128i vlcs1 = _mm_set1_epi8(chr[6]);
      __m128i vlcs2 = _mm_set1_epi8(chr[7]);
      __m128i vlcs3 = _mm_set1_epi8(chr[8]);
      __m128i vlcs4 = _mm_set1_epi8(chr[9]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 6)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcp3 = _mm_set1_epi8(chr[3]);
      __m128i vlcp4 = _mm_set1_epi8(chr[4]);
      __m128i vlcp5 = _mm_set1_epi8(chr[5]);
      __m128i vlcs0 = _mm_set1_epi8(chr[6]);
      __m128i vlcs1 = _mm_set1_epi8(chr[7]);
      __m128i vlcs2 = _mm_set1_epi8(chr[8]);
      __m128i vlcs3 = _mm_set1_epi8(chr[9]);
      __m128i vlcs4 = _mm_set1_epi8(chr[10]);
      __m128i vlcs5 = _mm_set1_epi8(chr[11]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 7)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcp3 = _mm_set1_epi8(chr[3]);
      __m128i vlcp4 = _mm_set1_epi8(chr[4]);
      __m128i vlcp5 = _mm_set1_epi8(chr[5]);
      __m128i vlcp6 = _mm_set1_epi8(chr[6]);
      __m128i vlcs0 = _mm_set1_epi8(chr[7]);
      __m128i vlcs1 = _mm_set1_epi8(chr[8]);
      __m128i vlcs2 = _mm_set1_epi8(chr[9]);
      __m128i vlcs3 = _mm_set1_epi8(chr[10]);
      __m128i vlcs4 = _mm_set1_epi8(chr[11]);
      __m128i vlcs5 = _mm_set1_epi8(chr[12]);
      __m128i vlcs6 = _mm_set1_epi8(chr[13]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp6, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs6, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 8)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      __m128i vlcp0 = _mm_set1_epi8(chr[0]);
      __m128i vlcp1 = _mm_set1_epi8(chr[1]);
      __m128i vlcp2 = _mm_set1_epi8(chr[2]);
      __m128i vlcp3 = _mm_set1_epi8(chr[3]);
      __m128i vlcp4 = _mm_set1_epi8(chr[4]);
      __m128i vlcp5 = _mm_set1_epi8(chr[5]);
      __m128i vlcp6 = _mm_set1_epi8(chr[6]);
      __m128i vlcp7 = _mm_set1_epi8(chr[7]);
      __m128i vlcs0 = _mm_set1_epi8(chr[8]);
      __m128i vlcs1 = _mm_set1_epi8(chr[9]);
      __m128i vlcs2 = _mm_set1_epi8(chr[10]);
      __m128i vlcs3 = _mm_set1_epi8(chr[11]);
      __m128i vlcs4 = _mm_set1_epi8(chr[12]);
      __m128i vlcs5 = _mm_set1_epi8(chr[13]);
      __m128i vlcs6 = _mm_set1_epi8(chr[14]);
      __m128i vlcs7 = _mm_set1_epi8(chr[15]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
          __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp);
          __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs);
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp6, vstrlcp));
          veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp7, vstrlcp));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs6, vstrlcs));
          veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs7, vstrlcs));
          uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs));
          while (mask != 0)
          {
            uint32_t offset = ctz(mask);
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (Pattern::predict_match(pmh, &buf_[loc], min))
                return true;
            }
            else
            {
              if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                return true;
            }
            mask &= mask - 1;
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
#elif defined(HAVE_NEON)
    // look for needles
    else if (pat_->pin_ == 2)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[3]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 = vorrq_u8(vceqq_u8(vlcp0, vstrlcp), vceqq_u8(vlcp1, vstrlcp));
          uint8x16_t vmasklcs8 = vorrq_u8(vceqq_u8(vlcs0, vstrlcs), vceqq_u8(vlcs1, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 3)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[5]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
                vorrq_u8(
                  vceqq_u8(vlcp0, vstrlcp),
                  vceqq_u8(vlcp1, vstrlcp)),
                vceqq_u8(vlcp2, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
                vorrq_u8(
                  vceqq_u8(vlcs0, vstrlcs),
                  vceqq_u8(vlcs1, vstrlcs)),
                vceqq_u8(vlcs2, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 4)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcp3 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[5]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[6]);
      uint8x16_t vlcs3 = vdupq_n_u8(chr[7]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vceqq_u8(vlcp0, vstrlcp),
                  vceqq_u8(vlcp1, vstrlcp)),
                vceqq_u8(vlcp2, vstrlcp)),
              vceqq_u8(vlcp3, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vceqq_u8(vlcs0, vstrlcs),
                  vceqq_u8(vlcs1, vstrlcs)),
                vceqq_u8(vlcs2, vstrlcs)),
              vceqq_u8(vlcs3, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 5)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcp3 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcp4 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[5]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[6]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[7]);
      uint8x16_t vlcs3 = vdupq_n_u8(chr[8]);
      uint8x16_t vlcs4 = vdupq_n_u8(chr[9]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vceqq_u8(vlcp0, vstrlcp),
                    vceqq_u8(vlcp1, vstrlcp)),
                  vceqq_u8(vlcp2, vstrlcp)),
                vceqq_u8(vlcp3, vstrlcp)),
              vceqq_u8(vlcp4, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vceqq_u8(vlcs0, vstrlcs),
                    vceqq_u8(vlcs1, vstrlcs)),
                  vceqq_u8(vlcs2, vstrlcs)),
                vceqq_u8(vlcs3, vstrlcs)),
              vceqq_u8(vlcs4, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 6)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcp3 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcp4 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcp5 = vdupq_n_u8(chr[5]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[6]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[7]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[8]);
      uint8x16_t vlcs3 = vdupq_n_u8(chr[9]);
      uint8x16_t vlcs4 = vdupq_n_u8(chr[10]);
      uint8x16_t vlcs5 = vdupq_n_u8(chr[11]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vceqq_u8(vlcp0, vstrlcp),
                      vceqq_u8(vlcp1, vstrlcp)),
                    vceqq_u8(vlcp2, vstrlcp)),
                  vceqq_u8(vlcp3, vstrlcp)),
                vceqq_u8(vlcp4, vstrlcp)),
              vceqq_u8(vlcp5, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vceqq_u8(vlcs0, vstrlcs),
                      vceqq_u8(vlcs1, vstrlcs)),
                    vceqq_u8(vlcs2, vstrlcs)),
                  vceqq_u8(vlcs3, vstrlcs)),
                vceqq_u8(vlcs4, vstrlcs)),
              vceqq_u8(vlcs5, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 7)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcp3 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcp4 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcp5 = vdupq_n_u8(chr[5]);
      uint8x16_t vlcp6 = vdupq_n_u8(chr[6]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[7]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[8]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[9]);
      uint8x16_t vlcs3 = vdupq_n_u8(chr[10]);
      uint8x16_t vlcs4 = vdupq_n_u8(chr[11]);
      uint8x16_t vlcs5 = vdupq_n_u8(chr[12]);
      uint8x16_t vlcs6 = vdupq_n_u8(chr[13]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vorrq_u8(
                        vceqq_u8(vlcp0, vstrlcp),
                        vceqq_u8(vlcp1, vstrlcp)),
                      vceqq_u8(vlcp2, vstrlcp)),
                    vceqq_u8(vlcp3, vstrlcp)),
                  vceqq_u8(vlcp4, vstrlcp)),
                vceqq_u8(vlcp5, vstrlcp)),
              vceqq_u8(vlcp6, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vorrq_u8(
                        vceqq_u8(vlcs0, vstrlcs),
                        vceqq_u8(vlcs1, vstrlcs)),
                      vceqq_u8(vlcs2, vstrlcs)),
                    vceqq_u8(vlcs3, vstrlcs)),
                  vceqq_u8(vlcs4, vstrlcs)),
                vceqq_u8(vlcs5, vstrlcs)),
              vceqq_u8(vlcs6, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
    else if (pat_->pin_ == 8)
    {
      size_t lcp = pat_->lcp_;
      size_t lcs = pat_->lcs_;
      const char *chr = pat_->chr_;
      uint8x16_t vlcp0 = vdupq_n_u8(chr[0]);
      uint8x16_t vlcp1 = vdupq_n_u8(chr[1]);
      uint8x16_t vlcp2 = vdupq_n_u8(chr[2]);
      uint8x16_t vlcp3 = vdupq_n_u8(chr[3]);
      uint8x16_t vlcp4 = vdupq_n_u8(chr[4]);
      uint8x16_t vlcp5 = vdupq_n_u8(chr[5]);
      uint8x16_t vlcp6 = vdupq_n_u8(chr[6]);
      uint8x16_t vlcp7 = vdupq_n_u8(chr[7]);
      uint8x16_t vlcs0 = vdupq_n_u8(chr[8]);
      uint8x16_t vlcs1 = vdupq_n_u8(chr[9]);
      uint8x16_t vlcs2 = vdupq_n_u8(chr[10]);
      uint8x16_t vlcs3 = vdupq_n_u8(chr[11]);
      uint8x16_t vlcs4 = vdupq_n_u8(chr[12]);
      uint8x16_t vlcs5 = vdupq_n_u8(chr[13]);
      uint8x16_t vlcs6 = vdupq_n_u8(chr[14]);
      uint8x16_t vlcs7 = vdupq_n_u8(chr[15]);
      while (true)
      {
        const char *s = buf_ + loc + lcp;
        const char *e = buf_ + end_ + lcp - min + 1;
        while (s <= e - 16)
        {
          uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp));
          uint8x16_t vmasklcp8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vorrq_u8(
                        vorrq_u8(
                          vceqq_u8(vlcp0, vstrlcp),
                          vceqq_u8(vlcp1, vstrlcp)),
                        vceqq_u8(vlcp2, vstrlcp)),
                      vceqq_u8(vlcp3, vstrlcp)),
                    vceqq_u8(vlcp4, vstrlcp)),
                  vceqq_u8(vlcp5, vstrlcp)),
                vceqq_u8(vlcp6, vstrlcp)),
              vceqq_u8(vlcp7, vstrlcp));
          uint8x16_t vmasklcs8 =
            vorrq_u8(
              vorrq_u8(
                vorrq_u8(
                  vorrq_u8(
                    vorrq_u8(
                      vorrq_u8(
                        vorrq_u8(
                          vceqq_u8(vlcs0, vstrlcs),
                          vceqq_u8(vlcs1, vstrlcs)),
                        vceqq_u8(vlcs2, vstrlcs)),
                      vceqq_u8(vlcs3, vstrlcs)),
                    vceqq_u8(vlcs4, vstrlcs)),
                  vceqq_u8(vlcs5, vstrlcs)),
                vceqq_u8(vlcs6, vstrlcs)),
              vceqq_u8(vlcs7, vstrlcs));
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8));
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff))
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min >= 4)
                {
                  if (Pattern::predict_match(pmh, &buf_[loc], min))
                    return true;
                }
                else
                {
                  if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
                    return true;
                }
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
        s -= lcp;
        loc = s - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + min > end_)
          return false;
        if (loc + min + 15 > end_)
          break;
      }
    }
#endif
    if (min >= 4 || pat_->npy_ < 16 || (min >= 2 && pat_->npy_ >= 56))
    {
      if (min >= 4)
      {
        const Pattern::Pred *bit = pat_->bit_;
        Pattern::Pred state1 = ~0;
        Pattern::Pred state2 = ~0;
        Pattern::Pred mask = (1 << (min - 1));
        while (true)
        {
          const char *s = buf_ + loc;
          const char *e = buf_ + end_;
          while (s < e - 1)
          {
            state2 = (state1 << 1) | bit[static_cast<uint8_t>(*s)];
            ++s;
            state1 = (state2 << 1) | bit[static_cast<uint8_t>(*s)];
            if ((state1 & state2 & mask) == 0)
              break;
            ++s;
          }
          if ((state2 & mask) == 0)
          {
            state1 = state2;
            state2 = ~0;
            --s;
          }
          else if ((state1 & mask) != 0 && s == e - 1)
          {
            state1 = (state1 << 1) | bit[static_cast<uint8_t>(*s)];
            if ((state1 & mask) != 0)
              ++s;
          }
          if (s < e)
          {
            s -= min - 1;
            loc = s - buf_;
            if (Pattern::predict_match(pmh, s, min))
            {
              set_current(loc);
              return true;
            }
            loc += min;
          }
          else
          {
            loc = s - buf_;
            set_current_and_peek_more(loc - min);
            loc = cur_ + min;
            if (loc >= end_)
              return false;
          }
        }
      }
      if (min == 3)
      {
        const Pattern::Pred *bit = pat_->bit_;
        Pattern::Pred state = ~0;
        while (true)
        {
          const char *s = buf_ + loc;
          const char *e = buf_ + end_;
          while (s < e)
          {
            state = (state << 1) | bit[static_cast<uint8_t>(*s)];
            if ((state & 4) == 0)
              break;
            ++s;
          }
          if (s < e)
          {
            s -= 2;
            loc = s - buf_;
            if (s > e - 4 || Pattern::predict_match(pma, s) == 0)
            {
              set_current(loc);
              return true;
            }
            loc += 3;
          }
          else
          {
            loc = s - buf_;
            set_current_and_peek_more(loc - 3);
            loc = cur_ + 3;
            if (loc >= end_)
              return false;
          }
        }
      }
      if (min == 2)
      {
        const Pattern::Pred *bit = pat_->bit_;
        Pattern::Pred state = ~0;
        while (true)
        {
          const char *s = buf_ + loc;
          const char *e = buf_ + end_;
          while (s < e)
          {
            state = (state << 1) | bit[static_cast<uint8_t>(*s)];
            if ((state & 2) == 0)
              break;
            ++s;
          }
          if (s < e)
          {
            s -= 1;
            loc = s - buf_;
            if (s > e - 4 || Pattern::predict_match(pma, s) == 0)
            {
              set_current(loc);
              return true;
            }
            loc += 2;
          }
          else
          {
            loc = s - buf_;
            set_current_and_peek_more(loc - 2);
            loc = cur_ + 2;
            if (loc >= end_)
              return false;
          }
        }
      }
      const Pattern::Pred *bit = pat_->bit_;
      while (true)
      {
        const char *s = buf_ + loc;
        const char *e = buf_ + end_ - 3;
        bool f = true;
        while (s < e &&
            (f = ((bit[static_cast<uint8_t>(*s)] & 1) &&
                  (bit[static_cast<uint8_t>(*++s)] & 1) &&
                  (bit[static_cast<uint8_t>(*++s)] & 1) &&
                  (bit[static_cast<uint8_t>(*++s)] & 1))))
        {
          ++s;
        }
        loc = s - buf_;
        if (!f)
        {
          if (s < e && Pattern::predict_match(pma, s))
          {
            ++loc;
            continue;
          }
          set_current(loc);
          return true;
        }
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + 3 >= end_)
        {
          set_current(loc);
          return loc + min <= end_;
        }
      }
    }
    while (true)
    {
      const char *s = buf_ + loc;
      const char *e = buf_ + end_ - 6;
      bool f = true;
      while (s < e &&
          (f = (Pattern::predict_match(pma, s) &&
                Pattern::predict_match(pma, ++s) &&
                Pattern::predict_match(pma, ++s) &&
                Pattern::predict_match(pma, ++s))))
      {
        ++s;
      }
      loc = s - buf_;
      if (!f)
      {
        set_current(loc);
        return true;
      }
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + 6 >= end_)
      {
        set_current(loc);
        return loc + min <= end_;
      }
    }
  }
  const char *chr = pat_->chr_;
  size_t len = pat_->len_; // actually never more than 255
  if (len == 1)
  {
    while (true)
    {
      const char *s = buf_ + loc;
      const char *e = buf_ + end_;
      s = static_cast<const char*>(std::memchr(s, *chr, e - s));
      if (s != NULL)
      {
        loc = s - buf_;
        set_current(loc);
        if (min >= 4)
        {
          if (s + 1 + min > e || Pattern::predict_match(pmh, s + 1, min))
            return true;
        }
        else
        {
          if (min == 0 || s > e - 5 || Pattern::predict_match(pma, s + 1) == 0)
            return true;
        }
        ++loc;
      }
      else
      {
        loc = e - buf_;
        set_current_and_peek_more(loc - 1);
        loc = cur_ + 1;
        if (loc + len > end_)
          return false;
      }
    }
  }
  size_t lcp = pat_->lcp_;
  size_t lcs = pat_->lcs_;
  while (true)
  {
    if (pat_->bmd_ == 0)
    {
      const char *s = buf_ + loc + lcp;
      const char *e = buf_ + end_ + lcp - len + 1;
#if defined(COMPILE_AVX512BW)
      // implements AVX512BW string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      // enhanced with least frequent character matching
      __m512i vlcp = _mm512_set1_epi8(chr[lcp]);
      __m512i vlcs = _mm512_set1_epi8(chr[lcs]);
      while (s <= e - 64)
      {
        __m512i vlcpm = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s));
        __m512i vlcsm = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s + lcs - lcp));
        uint64_t mask = _mm512_cmpeq_epi8_mask(vlcp, vlcpm) & _mm512_cmpeq_epi8_mask(vlcs, vlcsm);
        while (mask != 0)
        {
          uint32_t offset = ctzl(mask);
          if (std::memcmp(s - lcp + offset, chr, len) == 0)
          {
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 64;
      }
#elif defined(COMPILE_AVX2)
      // implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      // enhanced with least frequent character matching
      __m256i vlcp = _mm256_set1_epi8(chr[lcp]);
      __m256i vlcs = _mm256_set1_epi8(chr[lcs]);
      while (s <= e - 32)
      {
        __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
        __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
        __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
        uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
        while (mask != 0)
        {
          uint32_t offset = ctz(mask);
          if (std::memcmp(s - lcp + offset, chr, len) == 0)
          {
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 32;
      }
#elif defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
      // implements SSE2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      // enhanced with least frequent character matching
      __m128i vlcp = _mm_set1_epi8(chr[lcp]);
      __m128i vlcs = _mm_set1_epi8(chr[lcs]);
      while (s <= e - 16)
      {
        __m128i vlcpm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vlcsm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp));
        __m128i vlcpeq = _mm_cmpeq_epi8(vlcp, vlcpm);
        __m128i vlcseq = _mm_cmpeq_epi8(vlcs, vlcsm);
        uint32_t mask = _mm_movemask_epi8(_mm_and_si128(vlcpeq, vlcseq));
        while (mask != 0)
        {
          uint32_t offset = ctz(mask);
          if (std::memcmp(s - lcp + offset, chr, len) == 0)
          {
            loc = s - lcp + offset - buf_;
            set_current(loc);
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 16;
      }
#elif defined(HAVE_NEON)
      // implements NEON/AArch64 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html 64 bit optimized
      // enhanced with least frequent character matching
      uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
      uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
      if (min >= 4)
      {
        while (s <= e - 16)
        {
          uint8x16_t vlcpm = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vlcsm = vld1q_u8(reinterpret_cast<const uint8_t*>(s) + lcs - lcp);
          uint8x16_t vlcpeq = vceqq_u8(vlcp, vlcpm);
          uint8x16_t vlcseq = vceqq_u8(vlcs, vlcsm);
          uint8x16_t vmask8 = vandq_u8(vlcpeq, vlcseq);
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vmask8);
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff) && std::memcmp(s - lcp + i, chr, len) == 0)
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
                  return true;
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff) && std::memcmp(s - lcp + i + 8, chr, len) == 0)
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
                  return true;
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
      }
      else
      {
        while (s <= e - 16)
        {
          uint8x16_t vlcpm = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
          uint8x16_t vlcsm = vld1q_u8(reinterpret_cast<const uint8_t*>(s) + lcs - lcp);
          uint8x16_t vlcpeq = vceqq_u8(vlcp, vlcpm);
          uint8x16_t vlcseq = vceqq_u8(vlcs, vlcsm);
          uint8x16_t vmask8 = vandq_u8(vlcpeq, vlcseq);
          uint64x2_t vmask64 = vreinterpretq_u64_u8(vmask8);
          uint64_t mask = vgetq_lane_u64(vmask64, 0);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff) && std::memcmp(s - lcp + i, chr, len) == 0)
              {
                loc = s - lcp + i - buf_;
                set_current(loc);
                if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
                  return true;
              }
              mask >>= 8;
            }
          }
          mask = vgetq_lane_u64(vmask64, 1);
          if (mask != 0)
          {
            for (int i = 0; i < 8; ++i)
            {
              if ((mask & 0xff) && std::memcmp(s - lcp + i + 8, chr, len) == 0)
              {
                loc = s - lcp + i + 8 - buf_;
                set_current(loc);
                if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
                  return true;
              }
              mask >>= 8;
            }
          }
          s += 16;
        }
      }
#endif
      while (s < e)
      {
        do
          s = static_cast<const char*>(std::memchr(s, chr[lcp], e - s));
        while (s != NULL && s[lcs - lcp] != chr[lcs] && ++s < e);
        if (s == NULL || s >= e)
        {
          s = e;
          break;
        }
        if (len <= 2 || memcmp(s - lcp, chr, len) == 0)
        {
          loc = s - lcp - buf_;
          set_current(loc);
          if (min == 0)
            return true;
          if (min >= 4)
          {
            if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
              return true;
          }
        }
        ++s;
      }
      loc = s - lcp - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + len > end_)
        return false;
    }
    else
    {
      // apply our improved Boyer-Moore scheme as a fallback
      const char *s = buf_ + loc + len - 1;
      const char *e = buf_ + end_;
      const char *t = chr + len - 1;
      size_t bmd = pat_->bmd_;
      const uint8_t *bms = pat_->bms_;
      while (s < e)
      {
        size_t k = 0;
        do
          s += k = bms[static_cast<uint8_t>(*s)];
        while (k > 0 ? s < e : s[lcp - len + 1] != chr[lcp] && (s += bmd) < e);
        if (s >= e)
          break;
        const char *p = t - 1;
        const char *q = s - 1;
        while (p >= chr && *p == *q)
        {
          --p;
          --q;
        }
        if (p < chr)
        {
          loc = q - buf_ + 1;
          set_current(loc);
          if (min >= 4)
          {
            if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (min == 0 || loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
              return true;
          }
        }
        if (chr + bmd >= p)
        {
          s += bmd;
        }
        else
        {
          size_t k = bms[static_cast<uint8_t>(*q)];
          if (p + k > t + bmd)
            s += k - (t - p);
          else
            s += bmd;
        }
      }
      s -= len - 1;
      loc = s - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + len > end_)
        return false;
    }
  }
}

} // namespace reflex

#endif
