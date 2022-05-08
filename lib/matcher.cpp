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
  len_ = 0;     // split text length starts with 0
  anc_ = false; // no word boundary anchor found and applied
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
  if (pat_->fsm_ != NULL)
    fsm_.c1 = c1;
#if !defined(WITH_NO_INDENT)
redo:
#endif
  lap_.resize(0);
  cap_ = 0;
  bool nul = method == Const::MATCH;
  if (pat_->fsm_ != NULL)
  {
    DBGLOG("FSM code %p", pat_->fsm_);
    fsm_.bol = bol;
    fsm_.nul = nul;
    pat_->fsm_(*this);
    nul = fsm_.nul;
    c1 = fsm_.c1;
  }
  else if (pat_->opc_ != NULL)
  {
    const Pattern::Opcode *pc = pat_->opc_;
    while (true)
    {
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
              Pattern::Index jump = Pattern::index_of(opcode);
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
        Pattern::Index back = Pattern::Const::IMAX; // where to jump back to (backtrack on meta transitions)
        Pattern::Index jump = Pattern::Const::IMAX; // to jump to longest sequence of matching metas
        while (true)
        {
          if ((jump == Pattern::Const::IMAX || back == Pattern::Const::IMAX) && !Pattern::is_opcode_goto(opcode))
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
              case 0xFF: // LONG
                opcode = *++pc;
                continue;
            }
          }
          if (jump == Pattern::Const::IMAX)
          {
            if (back != Pattern::Const::IMAX)
            {
              pc = pat_->opc_ + back;
              opcode = *pc;
            }
            break;
          }
          DBGLOG("Backtrack: pc = %u", jump);
          if (back == Pattern::Const::IMAX)
            back = static_cast<Pattern::Index>(pc - pat_->opc_);
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
          break;
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
      Pattern::Index jump = Pattern::index_of(opcode);
      if (jump == 0)
      {
        // loop back to start state after only one char matched (one transition) but w/o full match, then optimize
        if (cap_ == 0 && pos_ == cur_ + 1 && method == Const::FIND)
          cur_ = pos_; // set cur_ to move forward from cur_ + 1 with FIND advance()
      }
      else if (jump >= Pattern::Const::LONG)
      {
        if (jump == Pattern::Const::HALT)
          break;
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
      // skip one char to keep searching
      set_current(++cur_);
      // allow FIND with "N" to match an empty line, with ^$ etc.
      if (cap_ == 0 || !opt_.N)
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
  if (pat_->len_ == 0)
  {
    if (min == 0)
      return false;
    if (loc + min > end_)
    {
      set_current_match(loc - 1);
      (void)peek_more();
      loc = cur_ + 1;
      if (loc + min > end_)
        return false;
    }
    if (min >= 4)
    {
      const Pattern::Pred *bit = pat_->bit_;
      Pattern::Pred state = ~0;
      Pattern::Pred mask = (1 << (min - 1));
      while (true)
      {
        const char *s = buf_ + loc;
        const char *e = buf_ + end_;
        while (s < e)
        {
          state = (state << 1) | bit[static_cast<uint8_t>(*s)];
          if ((state & mask) == 0)
            break;
          ++s;
        }
        if (s < e)
        {
          s -= min - 1;
          loc = s - buf_;
          if (Pattern::predict_match(pat_->pmh_, s, min))
          {
            set_current(loc);
            return true;
          }
          loc += min;
        }
        else
        {
          loc = s - buf_;
          set_current_match(loc - min);
          (void)peek_more();
          loc = cur_ + min;
          if (loc >= end_)
            return false;
        }
      }
    }
    const Pattern::Pred *pma = pat_->pma_;
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
          if (s + 4 > e || Pattern::predict_match(pma, s) == 0)
          {
            set_current(loc);
            return true;
          }
          loc += 3;
        }
        else
        {
          loc = s - buf_;
          set_current_match(loc - 3);
          (void)peek_more();
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
          if (s + 4 > e || Pattern::predict_match(pma, s) == 0)
          {
            set_current(loc);
            return true;
          }
          loc += 2;
        }
        else
        {
          loc = s - buf_;
          set_current_match(loc - 2);
          (void)peek_more();
          loc = cur_ + 2;
          if (loc >= end_)
            return false;
        }
      }
    }
    while (true)
    {
      const char *s = buf_ + loc;
      const char *e = buf_ + end_;
      while (s < e && (pma[static_cast<uint8_t>(*s)] & 0xc0) == 0xc0)
        ++s;
      if (s < e)
      {
        loc = s - buf_;
        if (s + 4 > e)
        {
          set_current(loc);
          return true;
        }
        size_t k = Pattern::predict_match(pma, s);
        if (k == 0)
        {
          set_current(loc);
          return true;
        }
        loc += k;
      }
      else
      {
        loc = s - buf_;
        set_current_match(loc - 1);
        (void)peek_more();
        loc = cur_ + 1;
        if (loc >= end_)
          return false;
      }
    }
  }
  const char *pre = pat_->pre_;
  size_t len = pat_->len_; // actually never more than 255
  if (len == 1)
  {
    while (true)
    {
      const char *s = buf_ + loc;
      const char *e = buf_ + end_;
      s = static_cast<const char*>(std::memchr(s, *pre, e - s));
      if (s != NULL)
      {
        loc = s - buf_;
        set_current(loc);
        return true;
      }
      loc = e - buf_;
      set_current_match(loc - 1);
      (void)peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
        return false;
    }
  }
  if (bmd_ == 0)
  {
    // Boyer-Moore preprocessing of the given pattern pat of length len, generates bmd_ > 0 and bms_[] shifts.
    // updated relative frequency table of English letters (with upper/lower-case ratio = 0.0563), punctuation and UTF-8 bytes
    static unsigned char freq[256] =
      // x64 binary ugrep.exe frequencies combined with ASCII TAB/LF/CR control code frequencies
      "\377\101\14\22\15\21\10\10\24\73\41\10\11\41\6\51"
      "\16\4\3\3\3\3\3\3\6\3\3\2\3\4\4\12"
      // TAB/LF/CR control code frequencies in text
      // "\0\0\0\0\0\0\0\0\0\73\41\0\0\41\0\0"
      // "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
      // ASCII frequencies
      "\377\0\1\1\0\0\16\33\6\6\7\0\27\11\27\14"
      "\13\14\10\5\4\5\4\4\4\7\12\21\10\14\10\0"
      "\0\11\2\3\5\16\2\2\7\10\0\1\4\3\7\10"
      "\2\0\6\7\12\3\1\3\0\2\0\70\1\70\0\1"
      "\0\237\35\64\133\373\53\47\170\205\3\20\115\64\202\227"
      "\45\2\162\170\272\64\23\56\3\47\2\3\15\3\0\0"
      // upper half with UTF-8 multibyte frequencies (synthesized)
      "\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47"
      "\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45"
      "\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45"
      "\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44"
      "\0\0\5\5\5\5\5\5\5\5\5\5\5\5\5\5"
      "\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5"
      "\46\56\56\56\56\56\56\56\56\56\56\56\56\46\56\56"
      "\73\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
    uint8_t n = static_cast<uint8_t>(len); // okay to cast: actually never more than 255
    uint16_t i;
    for (i = 0; i < 256; ++i)
      bms_[i] = n;
    lcp_ = 0;
    lcs_ = n > 1;
    for (i = 0; i < n; ++i)
    {
      uint8_t pch = static_cast<uint8_t>(pre[i]);
      bms_[pch] = static_cast<uint8_t>(n - i - 1);
      if (i > 0)
      {
        unsigned char freqpch = freq[pch];
        uint8_t lcpch = static_cast<uint8_t>(pre[lcp_]);
        uint8_t lcsch = static_cast<uint8_t>(pre[lcs_]);
        if (freq[lcpch] > freqpch)
        {
          lcs_ = lcp_;
          lcp_ = i;
        }
        else if (lcpch != pch && freq[lcsch] > freqpch)
        {
          lcs_ = i;
        }
      }
    }
    uint16_t j;
    for (i = n - 1, j = i; j > 0; --j)
      if (pre[j - 1] == pre[i])
        break;
    bmd_ = i - j + 1;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2) || defined(__SSE2__) || defined(__x86_64__) || _M_IX86_FP == 2 || defined(HAVE_NEON)
    size_t score = 0;
    for (i = 0; i < n; ++i)
      score += bms_[static_cast<uint8_t>(pre[i])];
    score /= n;
    uint8_t fch = freq[static_cast<uint8_t>(pre[lcp_])];
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
    if (!have_HW_SSE2() && !have_HW_AVX2() && !have_HW_AVX512BW())
    {
      // if scoring is high and freq is high, then use our improved Boyer-Moore instead
#if defined(__SSE2__) || defined(__x86_64__) || _M_IX86_FP == 2
      // SSE2 is available, expect fast memchr()
      if (score > 1 && fch > 35 && (score > 3 || fch > 50) && fch + score > 52)
        lcs_ = 0xffff;
#else
      // no SSE2 available, expect slow memchr()
      if (fch > 37 || (fch > 8 && score > 0))
        lcs_ = 0xffff;
#endif
    }
#elif defined(__SSE2__) || defined(__x86_64__) || _M_IX86_FP == 2 || defined(HAVE_NEON)
    // SIMD is available, if scoring is high and freq is high, then use our improved Boyer-Moore
    if (score > 1 && fch > 35 && (score > 3 || fch > 50) && fch + score > 52)
      lcs_ = 0xffff;
#endif
#endif
  }
  while (true)
  {
    if (lcs_ < len)
    {
      const char *s = buf_ + loc + lcp_;
      const char *e = buf_ + end_ + lcp_ - len + 1;
#if defined(COMPILE_AVX512BW)
      // implements AVX512BW string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      __m512i vlcp = _mm512_set1_epi8(pre[lcp_]);
      __m512i vlcs = _mm512_set1_epi8(pre[lcs_]);
      while (s + 64 <= e)
      {
        __m512i vlcpm = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s));
        __m512i vlcsm = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s + lcs_ - lcp_));
        uint64_t mask = _mm512_cmpeq_epi8_mask(vlcp, vlcpm) & _mm512_cmpeq_epi8_mask(vlcs, vlcsm);
        while (mask != 0)
        {
          uint32_t offset = ctzl(mask);
          if (std::memcmp(s - lcp_ + offset, pre, len) == 0)
          {
            loc = s - lcp_ + offset - buf_;
            set_current(loc);
            if (min == 0)
              return true;
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 64;
      }
#elif defined(COMPILE_AVX2)
      // implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      __m256i vlcp = _mm256_set1_epi8(pre[lcp_]);
      __m256i vlcs = _mm256_set1_epi8(pre[lcs_]);
      while (s + 32 <= e)
      {
        __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs_ - lcp_));
        __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
        __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
        uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
        while (mask != 0)
        {
          uint32_t offset = ctz(mask);
          if (std::memcmp(s - lcp_ + offset, pre, len) == 0)
          {
            loc = s - lcp_ + offset - buf_;
            set_current(loc);
            if (min == 0)
              return true;
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 32;
      }
#elif defined(HAVE_SSE2)
      // implements SSE2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
      __m128i vlcp = _mm_set1_epi8(pre[lcp_]);
      __m128i vlcs = _mm_set1_epi8(pre[lcs_]);
      while (s + 16 <= e)
      {
        __m128i vlcpm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vlcsm = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs_ - lcp_));
        __m128i vlcpeq = _mm_cmpeq_epi8(vlcp, vlcpm);
        __m128i vlcseq = _mm_cmpeq_epi8(vlcs, vlcsm);
        uint32_t mask = _mm_movemask_epi8(_mm_and_si128(vlcpeq, vlcseq));
        while (mask != 0)
        {
          uint32_t offset = ctz(mask);
          if (std::memcmp(s - lcp_ + offset, pre, len) == 0)
          {
            loc = s - lcp_ + offset - buf_;
            set_current(loc);
            if (min == 0)
              return true;
            if (min >= 4)
            {
              if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
                return true;
            }
            else
            {
              if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
                return true;
            }
          }
          mask &= mask - 1;
        }
        s += 16;
      }
#elif defined(HAVE_NEON)
      // implements NEON/AArch64 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html but 64 bit optimized
      uint8x16_t vlcp = vdupq_n_u8(pre[lcp_]);
      uint8x16_t vlcs = vdupq_n_u8(pre[lcs_]);
      while (s + 16 <= e)
      {
        uint8x16_t vlcpm = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
        uint8x16_t vlcsm = vld1q_u8(reinterpret_cast<const uint8_t*>(s) + lcs_ - lcp_);
        uint8x16_t vlcpeq = vceqq_u8(vlcp, vlcpm);
        uint8x16_t vlcseq = vceqq_u8(vlcs, vlcsm);
        uint8x16_t vmask8 = vandq_u8(vlcpeq, vlcseq);
        uint64x2_t vmask64 = vreinterpretq_u64_u8(vmask8);
        uint64_t mask = vgetq_lane_u64(vmask64, 0);
        if (mask != 0)
        {
          for (int i = 0; i < 8; ++i)
          {
            if ((mask & 0xff) && std::memcmp(s - lcp_ + i, pre, len) == 0)
            {
              loc = s - lcp_ + i - buf_;
              set_current(loc);
              if (min == 0)
                return true;
              if (min >= 4)
              {
                if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
                  return true;
              }
              else
              {
                if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
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
            if ((mask & 0xff) && std::memcmp(s - lcp_ + i + 8, pre, len) == 0)
            {
              loc = s - lcp_ + i + 8 - buf_;
              set_current(loc);
              if (min == 0)
                return true;
              if (min >= 4)
              {
                if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
                  return true;
              }
              else
              {
                if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
                  return true;
              }
            }
            mask >>= 8;
          }
        }
        s += 16;
      }
#endif
      while (s < e)
      {
        do
          s = static_cast<const char*>(std::memchr(s, pre[lcp_], e - s));
        while (s != NULL && s[lcs_ - lcp_] != pre[lcs_] && ++s < e);
        if (s == NULL || s >= e)
        {
          s = e;
          break;
        }
        if (len <= 2 || memcmp(s - lcp_, pre, len) == 0)
        {
          loc = s - lcp_ - buf_;
          set_current(loc);
          if (min == 0)
            return true;
          if (min >= 4)
          {
            if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
              return true;
          }
        }
        ++s;
      }
      loc = s - lcp_ - buf_;
      set_current_match(loc - 1);
      (void)peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
        return false;
    }
    else
    {
      // implements our improved Boyer-Moore scheme
      const char *s = buf_ + loc + len - 1;
      const char *e = buf_ + end_;
      const char *t = pre + len - 1;
      while (s < e)
      {
        size_t k = 0;
        do
          s += k = bms_[static_cast<uint8_t>(*s)];
        while (k > 0 ? s < e : s[lcp_ - len + 1] != pre[lcp_] && (s += bmd_) < e);
        if (s >= e)
          break;
        const char *p = t - 1;
        const char *q = s - 1;
        while (p >= pre && *p == *q)
        {
          --p;
          --q;
        }
        if (p < pre)
        {
          loc = q - buf_ + 1;
          set_current(loc);
          if (min == 0)
            return true;
          if (min >= 4)
          {
            if (loc + len + min > end_ || Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (loc + len + 4 > end_ || Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
              return true;
          }
        }
        if (pre + bmd_ >= p)
        {
          s += bmd_;
        }
        else
        {
          size_t k = bms_[static_cast<uint8_t>(*q)];
          if (p + k > t + bmd_)
            s += k - (t - p);
          else
            s += bmd_;
        }
      }
      s -= len - 1;
      loc = s - buf_;
      set_current_match(loc - 1);
      (void)peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
        return false;
    }
  }
}

} // namespace reflex

#endif
