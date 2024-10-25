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
@file      matcher.cpp regex engine
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/matcher.h>

namespace reflex {

/// Returns true if input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
size_t Matcher::match(Method method)
{
  DBGLOG("BEGIN Matcher::match()");
  reset_text();
  len_ = 0;         // split text length starts with 0
  size_t retry = 0; // retry regex match at lookback positions for predicted matches
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
#if !defined(WITH_NO_CODEGEN)
  if (pat_->fsm_ != NULL)
    fsm_.ch = ch;
#endif
#if !defined(WITH_NO_INDENT)
redo:
#endif
  lap_.resize(0);
  cap_ = 0;
  bool nul = method == Const::MATCH;
  if (!opt_.W || at_wb())
  {
    // skip to next line and keep searching if matching on anchor ^ and not at begin of line
    if (method == Const::FIND && pat_->bol_ && !bol)
      if (skip('\n'))
        goto scan;
#if !defined(WITH_NO_CODEGEN)
    if (pat_->fsm_ != NULL)
    {
      DBGLOG("FSM code %p", pat_->fsm_);
      fsm_.bol = bol;
      fsm_.nul = nul;
      pat_->fsm_(*this);
      nul = fsm_.nul;
      ch = fsm_.ch;
    }
    else
#endif
    if (pat_->opc_ != NULL)
    {
      const Pattern::Opcode *pc = pat_->opc_;
      Pattern::Index back = Pattern::Const::IMAX; // where to jump back to when backtracking over meta edges
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
            case 0xfe: // TAKE
              {
                int c;
                if (!opt_.W || (c = peek(), at_we(c, pos_)))
                {
                  cap_ = Pattern::long_index_of(opcode);
                  DBGLOG("Take: cap = %zu", cap_);
                  cur_ = pos_;
                }
              }
              ++pc;
              continue;
            case 0xfd: // REDO
              cap_ = Const::REDO;
              DBGLOG("Redo");
              cur_ = pos_;
              ++pc;
              continue;
            case 0xfc: // TAIL
              {
                Pattern::Lookahead la = Pattern::lookahead_of(opcode);
                DBGLOG("Tail: %u", la);
                if (lap_.size() > la && lap_[la] >= 0)
                  cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]); // mind the (new) gap
                ++pc;
                continue;
              }
            case 0xfb: // HEAD
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
          DBGLOG("Get: ch = %d", ch);
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
                  case 0xfe: // TAKE
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
                  case 0xfd: // REDO
                    cap_ = Const::REDO;
                    DBGLOG("Redo");
                    cur_ = pos_;
                    if (ch != EOF)
                      --cur_; // must unget one char
                    opcode = *++pc;
                    continue;
                  case 0xfc: // TAIL
                    {
                      Pattern::Lookahead la = Pattern::lookahead_of(opcode);
                      DBGLOG("Tail: %u", la);
                      if (lap_.size() > la && lap_[la] >= 0)
                        cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]); // mind the (new) gap
                      opcode = *++pc;
                      continue;
                    }
                  case 0xfb: // HEAD
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
                  case 0xff: // LONG
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
          if (ch == EOF)
            break;
        }
        Pattern::Opcode lo = ch << 24;
        Pattern::Opcode hi = lo | 0x00ffffff;
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
          // loop back to start state w/o full match: advance to avoid backtracking
          if (cap_ == 0 && method == Const::FIND)
          {
            if (cur_ + 1 == pos_)
            {
              // matched one char in a loop, do not backtrack here
              ++cur_;
              if (retry > 0)
                --retry;
            }
            else
            {
              // check each char in buf_[cur_+1..pos_-1] if it is a starting char, if not then increase cur_
              while (cur_ + 1 < pos_ && !pat_->fst_.test(static_cast<uint8_t>(buf_[cur_ + 1])))
              {
                ++cur_;
                if (retry > 0)
                  --retry;
              }
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
          DBGLOG("Find: try next pos %zu", cur_);
          goto scan;
        }
        if (cur_ < pos_) // if we didn't fail on META alone
        {
          if ((this->*adv_)(cur_ + 1))
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
            size_t k = cur_ + pat_->len_;
            ch = k < end_ ? static_cast<unsigned char>(buf_[k]) : EOF;
            if (opt_.W && (!at_wb() || !(at_end() || at_we(ch, k))))
              goto scan;
            txt_ = buf_ + cur_;
            len_ = pat_->len_;
            set_current(k);
            return cap_ = 1;
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
      // allow FIND with "N" to match an empty line, with ^$ etc.
      if (cap_ == 0 || !opt_.N)
      {
        // if we found an empty match, we keep looking for non-empty matches when "N" is off
        if (cap_ != 0)
        {
          if ((this->*adv_)(cur_ + 1))
            goto scan;
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
        DBGLOG("Accept empty match");
      }
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

// expand code for all pin minimal cases
#define INIT_ADV_PAT_PIN_CASE(PIN) \
  switch (pat_->min_) \
  { \
    case 0: \
    case 1: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_one; \
      break; \
    case 2: \
    case 3: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pma; \
      break; \
    case 4: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pmh<4>; \
      break; \
    case 5: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pmh<5>; \
      break; \
    case 6: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pmh<6>; \
      break; \
    case 7: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pmh<7>; \
      break; \
    case 8: \
      adv_ = &Matcher::advance_pattern_pin##PIN##_pmh<8>; \
      break; \
  }

// in init_advance(): select advance_pattern_min1() or advance_pattern_pma() based on the machine, heuristically
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
# define MAX_PATTERN_MIN1_NPY 33
# define MAX_PATTERN_MIN2_NPY 36
# define MAX_PATTERN_MIN3_NPY 47
#elif defined(HAVE_NEON) && defined(__aarch64__)
# define MAX_PATTERN_MIN1_NPY 30
# define MAX_PATTERN_MIN2_NPY 24
# define MAX_PATTERN_MIN3_NPY 36
#else
# define MAX_PATTERN_MIN1_NPY 50
# define MAX_PATTERN_MIN2_NPY 56
# define MAX_PATTERN_MIN3_NPY 56
#endif

/// Initialize specialized pattern search methods to advance the engine to a possible match
void Matcher::init_advance()
{
  adv_ = &Matcher::advance_none;
  if (pat_ == NULL)
    return;
  if (pat_->len_ == 0)
  {
    if (pat_->min_ == 0 && opt_.N)
      return;
    switch (pat_->pin_)
    {
      case 1:
        switch (pat_->min_)
        {
          case 0:
          case 1:
            adv_ = &Matcher::advance_pattern_pin1_one;
            break;
          case 2:
          case 3:
            adv_ = &Matcher::advance_pattern_pin1_pma;
            break;
          case 4:
            adv_ = &Matcher::advance_pattern_pin1_pmh<4>;
            break;
          case 5:
            adv_ = &Matcher::advance_pattern_pin1_pmh<5>;
            break;
          case 6:
            adv_ = &Matcher::advance_pattern_pin1_pmh<6>;
            break;
          case 7:
            adv_ = &Matcher::advance_pattern_pin1_pmh<7>;
            break;
          case 8:
            adv_ = &Matcher::advance_pattern_pin1_pmh<8>;
            break;
        }
        break;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2) || defined(HAVE_NEON)
      case 2:
        INIT_ADV_PAT_PIN_CASE(2);
        break;
      case 3:
        INIT_ADV_PAT_PIN_CASE(3);
        break;
      case 4:
        INIT_ADV_PAT_PIN_CASE(4);
        break;
      case 5:
        INIT_ADV_PAT_PIN_CASE(5);
        break;
      case 6:
        INIT_ADV_PAT_PIN_CASE(6);
        break;
      case 7:
        INIT_ADV_PAT_PIN_CASE(7);
        break;
      case 8:
        INIT_ADV_PAT_PIN_CASE(8);
        break;
#endif
      default:
        switch (pat_->min_)
        {
          case 0:
          case 1:
            if (pat_->npy_ <= MAX_PATTERN_MIN1_NPY)
              adv_ = &Matcher::advance_pattern_min1;
            else
              adv_ = &Matcher::advance_pattern_pma;
            break;
          case 2:
            if (pat_->npy_ <= MAX_PATTERN_MIN2_NPY)
              adv_ = &Matcher::advance_pattern_min2;
            else
              adv_ = &Matcher::advance_pattern_pma;
            break;
          case 3:
            if (pat_->npy_ <= MAX_PATTERN_MIN3_NPY)
              adv_ = &Matcher::advance_pattern_min3;
            else
              adv_ = &Matcher::advance_pattern_pma;
            break;
          case 4:
            adv_ = &Matcher::advance_pattern_min4<4>;
            break;
          case 5:
            adv_ = &Matcher::advance_pattern_min4<5>;
            break;
          case 6:
            adv_ = &Matcher::advance_pattern_min4<6>;
            break;
          case 7:
            adv_ = &Matcher::advance_pattern_min4<7>;
            break;
          case 8:
            adv_ = &Matcher::advance_pattern_min4<8>;
            break;
        }
    }
  }
  else if (pat_->len_ == 1)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::advance_char;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::advance_char_pma;
    else
      adv_ = &Matcher::advance_char_pmh;
  }
  else if (pat_->len_ == 2)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::advance_chars<2>;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::advance_chars_pma<2>;
    else
      adv_ = &Matcher::advance_chars_pmh<2>;
  }
  else if (pat_->len_ == 3)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::advance_chars<3>;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::advance_chars_pma<3>;
    else
      adv_ = &Matcher::advance_chars_pmh<3>;
  }
  else if (pat_->bmd_ == 0)
  {
    if (pat_->min_ >= 4)
      adv_ = &Matcher::advance_string_pmh;
    else if (pat_->min_ > 0)
      adv_ = &Matcher::advance_string_pma;
    else
      adv_ = &Matcher::advance_string;
  }
  else
  {
    if (pat_->min_ >= 4)
      adv_ = &Matcher::advance_string_bm_pmh;
    else if (pat_->min_ > 0)
      adv_ = &Matcher::advance_string_bm_pma;
    else
      adv_ = &Matcher::advance_string_bm;
  }
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2)
  // AVX2 runtime optimized function callback overrides
  if (have_HW_AVX2())
    simd_init_advance_avx2();
#endif
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
  // AVX512BW runtime optimized function callback overrides
  if (have_HW_AVX512BW())
    simd_init_advance_avx512bw();
#endif
}

/// Default method is none (unset)
bool Matcher::advance_none(size_t)
{
  return false;
}

/// My homegrown "needle search" method when needle pin=1 and min=0 or 1
bool Matcher::advance_pattern_pin1_one(size_t loc)
{
  const char *chr = pat_->chr_;
  uint16_t lcp = pat_->lcp_;
  char chr0 = chr[0];
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_;
    if (s < e && (s = static_cast<const char*>(std::memchr(s, chr0, e - s))) != NULL)
    {
      s -= lcp;
      loc = s - buf_;
      if (s > e - 4 || pat_->predict_match(s))
      {
        set_current(loc);
        return true;
      }
      ++loc;
    }
    else
    {
      loc = e - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + 1 > end_)
        return false;
    }
  }
}

/// My homegrown "needle search" method when needle pin=1 and min=2 or 3
bool Matcher::advance_pattern_pin1_pma(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
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
        if (loc + 4 > end_ || pat_->predict_match(&buf_[loc]))
        {
          set_current(loc);
          return true;
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
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
        loc = s - lcp - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff))
          {
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc]))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      mask = vgetq_lane_u64(vmask64, 1);
      if (mask != 0)
      {
        loc = s - lcp + 8 - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff))
          {
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc]))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + min > end_)
      return false;
    if (loc + min + 15 > end_)
      break;
  }
#endif
  char chr0 = chr[0];
  char chr1 = chr[1];
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_;
    if (s < e && (s = static_cast<const char*>(std::memchr(s, chr0, e - s))) != NULL)
    {
      s -= lcp;
      loc = s - buf_;
      if (s > e - 4 || (s[lcs] == chr1 && pat_->predict_match(s)))
      {
        set_current(loc);
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
}

/// My homegrown "needle search" method when needle pin=1 and min>=4
template <uint8_t MIN>
bool Matcher::advance_pattern_pin1_pmh(size_t loc)
{
  const char *chr = pat_->chr_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  __m128i vlcp = _mm_set1_epi8(chr[0]);
  __m128i vlcs = _mm_set1_epi8(chr[1]);
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - MIN + 1;
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
        if (pat_->predict_match(&buf_[loc], MIN))
        {
          set_current(loc);
          return true;
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + MIN > end_)
      return false;
    if (loc + MIN + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  uint8x16_t vlcp = vdupq_n_u8(chr[0]);
  uint8x16_t vlcs = vdupq_n_u8(chr[1]);
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - MIN + 1;
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
        loc = s - lcp - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff))
          {
            if (pat_->predict_match(&buf_[loc], MIN))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      mask = vgetq_lane_u64(vmask64, 1);
      if (mask != 0)
      {
        loc = s - lcp  + 8 - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff))
          {
            if (pat_->predict_match(&buf_[loc], MIN))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + MIN > end_)
      return false;
    if (loc + MIN + 15 > end_)
      break;
  }
#endif
  int chr0 = chr[0];
  int chr1 = chr[1];
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_;
    if (s < e && (s = static_cast<const char*>(std::memchr(s, chr0, e - s))) != NULL)
    {
      s -= lcp;
      loc = s - buf_;
      if (s + MIN > e || (s[lcs] == chr1 && pat_->predict_match(s, MIN)))
      {
        set_current(loc);
        return true;
      }
      ++loc;
    }
    else
    {
      loc = e - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + MIN > end_)
        return false;
    }
  }
}

#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)

/// My homegrown "needle search" methods
#define ADV_PAT_PIN_ONE(N, INIT, COMP) \
bool Matcher::advance_pattern_pin##N##_one(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc; \
    const char *e = buf_ + end_; \
    while (s <= e - 16) \
    { \
      __m128i vstr = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)); \
      __m128i veq = _mm_cmpeq_epi8(v0, vstr); \
      COMP \
      uint32_t mask = _mm_movemask_epi8(veq); \
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s + offset - buf_; \
        if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 16; \
    } \
    loc = s - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + 1 > end_) \
      return false; \
    if (loc + 16 > end_) \
      break; \
  } \
  return advance_pattern_pma(loc); \
}

ADV_PAT_PIN_ONE(2, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
  )

ADV_PAT_PIN_ONE(3, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
  )

ADV_PAT_PIN_ONE(4, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
    __m128i v3 = _mm_set1_epi8(chr[3]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v3, vstr)); \
  )

ADV_PAT_PIN_ONE(5, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
    __m128i v3 = _mm_set1_epi8(chr[3]); \
    __m128i v4 = _mm_set1_epi8(chr[4]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v3, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v4, vstr)); \
  )

ADV_PAT_PIN_ONE(6, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
    __m128i v3 = _mm_set1_epi8(chr[3]); \
    __m128i v4 = _mm_set1_epi8(chr[4]); \
    __m128i v5 = _mm_set1_epi8(chr[5]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v3, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v4, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v5, vstr)); \
  )

ADV_PAT_PIN_ONE(7, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
    __m128i v3 = _mm_set1_epi8(chr[3]); \
    __m128i v4 = _mm_set1_epi8(chr[4]); \
    __m128i v5 = _mm_set1_epi8(chr[5]); \
    __m128i v6 = _mm_set1_epi8(chr[6]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v3, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v4, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v5, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v6, vstr)); \
  )

ADV_PAT_PIN_ONE(8, \
    __m128i v0 = _mm_set1_epi8(chr[0]); \
    __m128i v1 = _mm_set1_epi8(chr[1]); \
    __m128i v2 = _mm_set1_epi8(chr[2]); \
    __m128i v3 = _mm_set1_epi8(chr[3]); \
    __m128i v4 = _mm_set1_epi8(chr[4]); \
    __m128i v5 = _mm_set1_epi8(chr[5]); \
    __m128i v6 = _mm_set1_epi8(chr[6]); \
    __m128i v7 = _mm_set1_epi8(chr[7]); \
  , \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v1, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v2, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v3, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v4, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v5, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v6, vstr)); \
    veq = _mm_or_si128(veq, _mm_cmpeq_epi8(v7, vstr)); \
  )

/// My homegrown "needle search" methods
#define ADV_PAT_PIN(N, INIT, COMP) \
bool Matcher::advance_pattern_pin##N##_pma(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  size_t min = pat_->min_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc + lcp; \
    const char *e = buf_ + end_ + lcp - min + 1; \
    while (s <= e - 16) \
    { \
      __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)); \
      __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp)); \
      __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp); \
      __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs); \
      COMP \
      uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs)); \
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s - lcp + offset - buf_; \
        if (loc + min + 3 > end_ || pat_->predict_match(&buf_[loc])) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 16; \
    } \
    loc = s - lcp - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + min > end_) \
      return false; \
    if (loc + min + 15 > end_) \
      break; \
  } \
  return advance_pattern_pma(loc); \
} \
\
template <uint8_t MIN> \
bool Matcher::advance_pattern_pin##N##_pmh(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc + lcp; \
    const char *e = buf_ + end_ + lcp - MIN + 1; \
    while (s <= e - 16) \
    { \
      __m128i vstrlcp = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)); \
      __m128i vstrlcs = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + lcs - lcp)); \
      __m128i veqlcp = _mm_cmpeq_epi8(vlcp0, vstrlcp); \
      __m128i veqlcs = _mm_cmpeq_epi8(vlcs0, vstrlcs); \
      COMP \
      uint32_t mask = _mm_movemask_epi8(_mm_and_si128(veqlcp, veqlcs)); \
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s - lcp + offset - buf_; \
        if (pat_->predict_match(&buf_[loc], MIN)) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 16; \
    } \
    loc = s - lcp - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + MIN > end_) \
      return false; \
    if (loc + MIN + 15 > end_) \
      break; \
  } \
  return advance_pattern_min4<MIN>(loc); \
}

ADV_PAT_PIN(2, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[2]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[3]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
  )

ADV_PAT_PIN(3, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[3]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[4]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[5]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
  )

ADV_PAT_PIN(4, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcp3 = _mm_set1_epi8(chr[3]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[4]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[5]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[6]); \
    __m128i vlcs3 = _mm_set1_epi8(chr[7]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs)); \
  )

ADV_PAT_PIN(5, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcp3 = _mm_set1_epi8(chr[3]); \
    __m128i vlcp4 = _mm_set1_epi8(chr[4]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[5]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[6]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[7]); \
    __m128i vlcs3 = _mm_set1_epi8(chr[8]); \
    __m128i vlcs4 = _mm_set1_epi8(chr[9]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs)); \
  )

ADV_PAT_PIN(6, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcp3 = _mm_set1_epi8(chr[3]); \
    __m128i vlcp4 = _mm_set1_epi8(chr[4]); \
    __m128i vlcp5 = _mm_set1_epi8(chr[5]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[6]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[7]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[8]); \
    __m128i vlcs3 = _mm_set1_epi8(chr[9]); \
    __m128i vlcs4 = _mm_set1_epi8(chr[10]); \
    __m128i vlcs5 = _mm_set1_epi8(chr[11]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs)); \
  )

ADV_PAT_PIN(7, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcp3 = _mm_set1_epi8(chr[3]); \
    __m128i vlcp4 = _mm_set1_epi8(chr[4]); \
    __m128i vlcp5 = _mm_set1_epi8(chr[5]); \
    __m128i vlcp6 = _mm_set1_epi8(chr[6]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[7]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[8]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[9]); \
    __m128i vlcs3 = _mm_set1_epi8(chr[10]); \
    __m128i vlcs4 = _mm_set1_epi8(chr[11]); \
    __m128i vlcs5 = _mm_set1_epi8(chr[12]); \
    __m128i vlcs6 = _mm_set1_epi8(chr[13]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp6, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs6, vstrlcs)); \
  )

ADV_PAT_PIN(8, \
    __m128i vlcp0 = _mm_set1_epi8(chr[0]); \
    __m128i vlcp1 = _mm_set1_epi8(chr[1]); \
    __m128i vlcp2 = _mm_set1_epi8(chr[2]); \
    __m128i vlcp3 = _mm_set1_epi8(chr[3]); \
    __m128i vlcp4 = _mm_set1_epi8(chr[4]); \
    __m128i vlcp5 = _mm_set1_epi8(chr[5]); \
    __m128i vlcp6 = _mm_set1_epi8(chr[6]); \
    __m128i vlcp7 = _mm_set1_epi8(chr[7]); \
    __m128i vlcs0 = _mm_set1_epi8(chr[8]); \
    __m128i vlcs1 = _mm_set1_epi8(chr[9]); \
    __m128i vlcs2 = _mm_set1_epi8(chr[10]); \
    __m128i vlcs3 = _mm_set1_epi8(chr[11]); \
    __m128i vlcs4 = _mm_set1_epi8(chr[12]); \
    __m128i vlcs5 = _mm_set1_epi8(chr[13]); \
    __m128i vlcs6 = _mm_set1_epi8(chr[14]); \
    __m128i vlcs7 = _mm_set1_epi8(chr[15]); \
  , \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp6, vstrlcp)); \
    veqlcp = _mm_or_si128(veqlcp, _mm_cmpeq_epi8(vlcp7, vstrlcp)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs5, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs6, vstrlcs)); \
    veqlcs = _mm_or_si128(veqlcs, _mm_cmpeq_epi8(vlcs7, vstrlcs)); \
  )

#elif defined(HAVE_NEON)

/// My homegrown "needle search" methods
#define ADV_PAT_PIN_ONE(N, INIT, COMP) \
bool Matcher::advance_pattern_pin##N##_one(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc; \
    const char *e = buf_ + end_; \
    while (s <= e - 16) \
    { \
      uint8x16_t vstr = vld1q_u8(reinterpret_cast<const uint8_t*>(s)); \
      COMP \
      uint64x2_t vmask64 = vreinterpretq_u64_u8(vmask8); \
      uint64_t mask = vgetq_lane_u64(vmask64, 0); \
      if (mask != 0) \
      { \
        loc = s - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      mask = vgetq_lane_u64(vmask64, 1); \
      if (mask != 0) \
      { \
        loc = s + 8 - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      s += 16; \
    } \
    loc = s - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + 1 > end_) \
      return false; \
    if (loc + 16 > end_) \
      break; \
  } \
  return advance_pattern_pma(loc); \
}

ADV_PAT_PIN_ONE(2, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
  , \
    uint8x16_t vmask8 = vorrq_u8(vceqq_u8(v0, vstr), vceqq_u8(v1, vstr)); \
  )

ADV_PAT_PIN_ONE(3, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vceqq_u8(v0, vstr), \
          vceqq_u8(v1, vstr)), \
        vceqq_u8(v2, vstr)); \
  )

ADV_PAT_PIN_ONE(4, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
    uint8x16_t v3 = vdupq_n_u8(chr[3]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vceqq_u8(v0, vstr), \
            vceqq_u8(v1, vstr)), \
          vceqq_u8(v2, vstr)), \
        vceqq_u8(v3, vstr)); \
  )

ADV_PAT_PIN_ONE(5, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
    uint8x16_t v3 = vdupq_n_u8(chr[3]); \
    uint8x16_t v4 = vdupq_n_u8(chr[4]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vceqq_u8(v0, vstr), \
              vceqq_u8(v1, vstr)), \
            vceqq_u8(v2, vstr)), \
          vceqq_u8(v3, vstr)), \
        vceqq_u8(v4, vstr)); \
  )

ADV_PAT_PIN_ONE(6, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
    uint8x16_t v3 = vdupq_n_u8(chr[3]); \
    uint8x16_t v4 = vdupq_n_u8(chr[4]); \
    uint8x16_t v5 = vdupq_n_u8(chr[5]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vceqq_u8(v0, vstr), \
                vceqq_u8(v1, vstr)), \
              vceqq_u8(v2, vstr)), \
            vceqq_u8(v3, vstr)), \
          vceqq_u8(v4, vstr)), \
        vceqq_u8(v5, vstr)); \
  )

ADV_PAT_PIN_ONE(7, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
    uint8x16_t v3 = vdupq_n_u8(chr[3]); \
    uint8x16_t v4 = vdupq_n_u8(chr[4]); \
    uint8x16_t v5 = vdupq_n_u8(chr[5]); \
    uint8x16_t v6 = vdupq_n_u8(chr[6]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vceqq_u8(v0, vstr), \
                  vceqq_u8(v1, vstr)), \
                vceqq_u8(v2, vstr)), \
              vceqq_u8(v3, vstr)), \
            vceqq_u8(v4, vstr)), \
          vceqq_u8(v5, vstr)), \
        vceqq_u8(v6, vstr)); \
  )

ADV_PAT_PIN_ONE(8, \
    uint8x16_t v0 = vdupq_n_u8(chr[0]); \
    uint8x16_t v1 = vdupq_n_u8(chr[1]); \
    uint8x16_t v2 = vdupq_n_u8(chr[2]); \
    uint8x16_t v3 = vdupq_n_u8(chr[3]); \
    uint8x16_t v4 = vdupq_n_u8(chr[4]); \
    uint8x16_t v5 = vdupq_n_u8(chr[5]); \
    uint8x16_t v6 = vdupq_n_u8(chr[6]); \
    uint8x16_t v7 = vdupq_n_u8(chr[7]); \
  , \
    uint8x16_t vmask8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vorrq_u8( \
                    vceqq_u8(v0, vstr), \
                    vceqq_u8(v1, vstr)), \
                  vceqq_u8(v2, vstr)), \
                vceqq_u8(v3, vstr)), \
              vceqq_u8(v4, vstr)), \
            vceqq_u8(v5, vstr)), \
          vceqq_u8(v6, vstr)), \
        vceqq_u8(v7, vstr)); \
  )

/// My homegrown "needle search" methods
#define ADV_PAT_PIN(N, INIT, COMP) \
bool Matcher::advance_pattern_pin##N##_pma(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  size_t min = pat_->min_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc + lcp; \
    const char *e = buf_ + end_ + lcp - min + 1; \
    while (s <= e - 16) \
    { \
      uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s)); \
      uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp)); \
      COMP \
      uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8)); \
      uint64_t mask = vgetq_lane_u64(vmask64, 0); \
      if (mask != 0) \
      { \
        loc = s - lcp - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      mask = vgetq_lane_u64(vmask64, 1); \
      if (mask != 0) \
      { \
        loc = s - lcp + 8 - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      s += 16; \
    } \
    loc = s - lcp - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + min > end_) \
      return false; \
    if (loc + min + 15 > end_) \
      break; \
  } \
  return advance_pattern_pma(loc); \
} \
\
template <uint8_t MIN> \
bool Matcher::advance_pattern_pin##N##_pmh(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc + lcp; \
    const char *e = buf_ + end_ + lcp - MIN + 1; \
    while (s <= e - 16) \
    { \
      uint8x16_t vstrlcp = vld1q_u8(reinterpret_cast<const uint8_t*>(s)); \
      uint8x16_t vstrlcs = vld1q_u8(reinterpret_cast<const uint8_t*>(s + lcs - lcp)); \
      COMP \
      uint64x2_t vmask64 = vreinterpretq_u64_u8(vandq_u8(vmasklcp8, vmasklcs8)); \
      uint64_t mask = vgetq_lane_u64(vmask64, 0); \
      if (mask != 0) \
      { \
        loc = s - lcp - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      mask = vgetq_lane_u64(vmask64, 1); \
      if (mask != 0) \
      { \
        loc = s - lcp + 8 - buf_; \
        if (static_cast<uint32_t>(mask) == 0) \
        { \
          mask >>= 32; \
          loc += 4; \
        } \
        do \
        { \
          if ((mask & 0xff) != 0) \
          { \
            if (loc + 4 > end_ || pat_->predict_match(&buf_[loc])) \
            { \
              set_current(loc); \
              return true; \
            } \
          } \
          mask >>= 8; \
          ++loc; \
        } while (mask != 0); \
      } \
      s += 16; \
    } \
    loc = s - lcp - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + MIN > end_) \
      return false; \
    if (loc + MIN + 15 > end_) \
      break; \
  } \
  return advance_pattern_min4<MIN>(loc); \
}

ADV_PAT_PIN(2, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[3]); \
  , \
    uint8x16_t vmasklcp8 = vorrq_u8(vceqq_u8(vlcp0, vstrlcp), vceqq_u8(vlcp1, vstrlcp)); \
    uint8x16_t vmasklcs8 = vorrq_u8(vceqq_u8(vlcs0, vstrlcs), vceqq_u8(vlcs1, vstrlcs)); \
  )

ADV_PAT_PIN(3, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[5]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vceqq_u8(vlcp0, vstrlcp), \
          vceqq_u8(vlcp1, vstrlcp)), \
        vceqq_u8(vlcp2, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vceqq_u8(vlcs0, vstrlcs), \
          vceqq_u8(vlcs1, vstrlcs)), \
        vceqq_u8(vlcs2, vstrlcs)); \
  )

ADV_PAT_PIN(4, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcp3 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[5]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[6]); \
    uint8x16_t vlcs3 = vdupq_n_u8(chr[7]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vceqq_u8(vlcp0, vstrlcp), \
            vceqq_u8(vlcp1, vstrlcp)), \
          vceqq_u8(vlcp2, vstrlcp)), \
        vceqq_u8(vlcp3, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vceqq_u8(vlcs0, vstrlcs), \
            vceqq_u8(vlcs1, vstrlcs)), \
          vceqq_u8(vlcs2, vstrlcs)), \
        vceqq_u8(vlcs3, vstrlcs)); \
  )

ADV_PAT_PIN(5, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcp3 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcp4 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[5]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[6]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[7]); \
    uint8x16_t vlcs3 = vdupq_n_u8(chr[8]); \
    uint8x16_t vlcs4 = vdupq_n_u8(chr[9]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vceqq_u8(vlcp0, vstrlcp), \
              vceqq_u8(vlcp1, vstrlcp)), \
            vceqq_u8(vlcp2, vstrlcp)), \
          vceqq_u8(vlcp3, vstrlcp)), \
        vceqq_u8(vlcp4, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vceqq_u8(vlcs0, vstrlcs), \
              vceqq_u8(vlcs1, vstrlcs)), \
            vceqq_u8(vlcs2, vstrlcs)), \
          vceqq_u8(vlcs3, vstrlcs)), \
        vceqq_u8(vlcs4, vstrlcs)); \
  )

ADV_PAT_PIN(6, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcp3 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcp4 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcp5 = vdupq_n_u8(chr[5]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[6]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[7]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[8]); \
    uint8x16_t vlcs3 = vdupq_n_u8(chr[9]); \
    uint8x16_t vlcs4 = vdupq_n_u8(chr[10]); \
    uint8x16_t vlcs5 = vdupq_n_u8(chr[11]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vceqq_u8(vlcp0, vstrlcp), \
                vceqq_u8(vlcp1, vstrlcp)), \
              vceqq_u8(vlcp2, vstrlcp)), \
            vceqq_u8(vlcp3, vstrlcp)), \
          vceqq_u8(vlcp4, vstrlcp)), \
        vceqq_u8(vlcp5, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vceqq_u8(vlcs0, vstrlcs), \
                vceqq_u8(vlcs1, vstrlcs)), \
              vceqq_u8(vlcs2, vstrlcs)), \
            vceqq_u8(vlcs3, vstrlcs)), \
          vceqq_u8(vlcs4, vstrlcs)), \
        vceqq_u8(vlcs5, vstrlcs)); \
  )

ADV_PAT_PIN(7, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcp3 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcp4 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcp5 = vdupq_n_u8(chr[5]); \
    uint8x16_t vlcp6 = vdupq_n_u8(chr[6]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[7]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[8]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[9]); \
    uint8x16_t vlcs3 = vdupq_n_u8(chr[10]); \
    uint8x16_t vlcs4 = vdupq_n_u8(chr[11]); \
    uint8x16_t vlcs5 = vdupq_n_u8(chr[12]); \
    uint8x16_t vlcs6 = vdupq_n_u8(chr[13]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vceqq_u8(vlcp0, vstrlcp), \
                  vceqq_u8(vlcp1, vstrlcp)), \
                vceqq_u8(vlcp2, vstrlcp)), \
              vceqq_u8(vlcp3, vstrlcp)), \
            vceqq_u8(vlcp4, vstrlcp)), \
          vceqq_u8(vlcp5, vstrlcp)), \
        vceqq_u8(vlcp6, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vceqq_u8(vlcs0, vstrlcs), \
                  vceqq_u8(vlcs1, vstrlcs)), \
                vceqq_u8(vlcs2, vstrlcs)), \
              vceqq_u8(vlcs3, vstrlcs)), \
            vceqq_u8(vlcs4, vstrlcs)), \
          vceqq_u8(vlcs5, vstrlcs)), \
        vceqq_u8(vlcs6, vstrlcs)); \
  )

ADV_PAT_PIN(8, \
    uint8x16_t vlcp0 = vdupq_n_u8(chr[0]); \
    uint8x16_t vlcp1 = vdupq_n_u8(chr[1]); \
    uint8x16_t vlcp2 = vdupq_n_u8(chr[2]); \
    uint8x16_t vlcp3 = vdupq_n_u8(chr[3]); \
    uint8x16_t vlcp4 = vdupq_n_u8(chr[4]); \
    uint8x16_t vlcp5 = vdupq_n_u8(chr[5]); \
    uint8x16_t vlcp6 = vdupq_n_u8(chr[6]); \
    uint8x16_t vlcp7 = vdupq_n_u8(chr[7]); \
    uint8x16_t vlcs0 = vdupq_n_u8(chr[8]); \
    uint8x16_t vlcs1 = vdupq_n_u8(chr[9]); \
    uint8x16_t vlcs2 = vdupq_n_u8(chr[10]); \
    uint8x16_t vlcs3 = vdupq_n_u8(chr[11]); \
    uint8x16_t vlcs4 = vdupq_n_u8(chr[12]); \
    uint8x16_t vlcs5 = vdupq_n_u8(chr[13]); \
    uint8x16_t vlcs6 = vdupq_n_u8(chr[14]); \
    uint8x16_t vlcs7 = vdupq_n_u8(chr[15]); \
  , \
    uint8x16_t vmasklcp8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vorrq_u8( \
                    vceqq_u8(vlcp0, vstrlcp), \
                    vceqq_u8(vlcp1, vstrlcp)), \
                  vceqq_u8(vlcp2, vstrlcp)), \
                vceqq_u8(vlcp3, vstrlcp)), \
              vceqq_u8(vlcp4, vstrlcp)), \
            vceqq_u8(vlcp5, vstrlcp)), \
          vceqq_u8(vlcp6, vstrlcp)), \
        vceqq_u8(vlcp7, vstrlcp)); \
    uint8x16_t vmasklcs8 = \
      vorrq_u8( \
        vorrq_u8( \
          vorrq_u8( \
            vorrq_u8( \
              vorrq_u8( \
                vorrq_u8( \
                  vorrq_u8( \
                    vceqq_u8(vlcs0, vstrlcs), \
                    vceqq_u8(vlcs1, vstrlcs)), \
                  vceqq_u8(vlcs2, vstrlcs)), \
                vceqq_u8(vlcs3, vstrlcs)), \
              vceqq_u8(vlcs4, vstrlcs)), \
            vceqq_u8(vlcs5, vstrlcs)), \
          vceqq_u8(vlcs6, vstrlcs)), \
        vceqq_u8(vlcs7, vstrlcs)); \
  )

#endif

/// Minimal 1 byte long patterns using 4-way bitap hashed pairs then PM4
bool Matcher::advance_pattern_min1(size_t loc)
{
  const Pattern::Pred *tap = pat_->tap_;
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_ - 4;
    uint8_t c0 = static_cast<uint8_t>(buf_[loc]);
    while (s < e)
    {
      uint8_t c1 = static_cast<uint8_t>(s[1]);
      uint8_t c2 = static_cast<uint8_t>(s[2]);
      uint8_t c3 = static_cast<uint8_t>(s[3]);
      uint8_t c4 = static_cast<uint8_t>(s[4]);
      if (!(tap[Pattern::bihash(c0, c1)] &
            tap[Pattern::bihash(c1, c2)] &
            tap[Pattern::bihash(c2, c3)] &
            tap[Pattern::bihash(c3, c4)] & 1))
        break;
      c0 = c4;
      s += 4;
    }
    if (s < e)
    {
      if (!pat_->predict_match(s) &&
          ++s <= e && !pat_->predict_match(s) &&
          ++s <= e && !pat_->predict_match(s) &&
          ++s <= e && !pat_->predict_match(s))
      {
        loc = s - buf_ + 1;
        continue;
      }
      loc = s - buf_;
      set_current(loc);
      return true;
    }
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 4 >= end_)
    {
      set_current(loc);
      return loc + 1 <= end_;
    }
  }
}

/// Minimal 2 byte long patterns using bitap hashed pairs and PM4
bool Matcher::advance_pattern_min2(size_t loc)
{
  const Pattern::Pred *tap = pat_->tap_;
  uint32_t state = ~0;
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_ - 1;
    uint8_t c0 = static_cast<uint8_t>(*s);
    while (s < e)
    {
      uint8_t c1 = static_cast<uint8_t>(*++s);
      state = (state << 1) | tap[Pattern::bihash(c0, c1)];
      c0 = c1;
      if ((state & 2) == 0 && (s > e - 2 || pat_->predict_match(s - 2)))
      {
        loc = s - buf_ - 2;
        set_current(loc);
        return true;
      }
    }
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 1 >= end_)
      return false;
  }
}

/// Minimal 3 byte long pattern using bitap hashed pairs and PM4
bool Matcher::advance_pattern_min3(size_t loc)
{
  const Pattern::Pred *tap = pat_->tap_;
  uint32_t state = ~0;
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_ - 1;
    uint8_t c0 = static_cast<uint8_t>(*s);
    while (s < e)
    {
      uint8_t c1 = static_cast<uint8_t>(*++s);
      state = (state << 1) | tap[Pattern::bihash(c0, c1)];
      c0 = c1;
      if ((state & 4) == 0 && (s > e - 1 || pat_->predict_match(s - 3)))
      {
        loc = s - buf_ - 3;
        set_current(loc);
        return true;
      }
    }
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 1 >= end_)
      return false;
  }
}

/// Minimal 4 byte long patterns (MIN>=4) using bitap hashed pairs and Bloom filter hashing
template <uint8_t MIN>
bool Matcher::advance_pattern_min4(size_t loc)
{
  const Pattern::Pred *tap = pat_->tap_;
  uint32_t state1 = ~0;
  uint32_t state2 = ~0;
  uint32_t mask = 1 << (MIN - 1);
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_ - 2;
    uint8_t c0 = static_cast<uint8_t>(*s);
    while (s < e)
    {
      uint8_t c1 = static_cast<uint8_t>(*++s);
      state2 = (state1 << 1) | tap[Pattern::bihash(c0, c1)];
      c0 = static_cast<uint8_t>(*++s);
      state1 = (state2 << 1) | tap[Pattern::bihash(c1, c0)];
      if ((state2 & mask) == 0 && pat_->predict_match(s - MIN - 1, MIN))
      {
        loc = s - buf_ - MIN - 1;
        set_current(loc);
        return true;
      }
      if ((state1 & mask) == 0 && pat_->predict_match(s - MIN, MIN))
      {
        loc = s - buf_ - MIN;
        set_current(loc);
        return true;
      }
    }
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 2 >= end_)
    {
      if (loc + 1 >= end_)
        return false;
      --loc;
      state1 = state2;
    }
  }
}

/// Minimal 1 byte long pattern using PM4
bool Matcher::advance_pattern_pma(size_t loc)
{
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_;
    while (s < e - 6 &&
        !pat_->predict_match(s) &&
        !pat_->predict_match(++s) &&
        !pat_->predict_match(++s) &&
        !pat_->predict_match(++s))
    {
      ++s;
    }
    loc = s - buf_;
    if (s < e)
    {
      set_current(loc);
      return true;
    }
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 6 >= end_)
    {
      set_current(loc);
      return loc + pat_->min_ <= end_;
    }
  }
}

/// One char
bool Matcher::advance_char(size_t loc)
{
  char chr0 = pat_->chr_[0];
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_;
    s = static_cast<const char*>(std::memchr(s, chr0, e - s));
    if (s != NULL)
    {
      loc = s - buf_;
      set_current(loc);
      return true;
    }
    loc = e - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + 1 > end_)
      return false;
  }
}

/// One char followed by 1 to 4 characters
bool Matcher::advance_char_pma(size_t loc)
{
  char chr0 = pat_->chr_[0];
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_;
    s = static_cast<const char*>(std::memchr(s, chr0, e - s));
    if (s != NULL)
    {
      loc = s - buf_;
      if (s > e - 5 || pat_->predict_match(s + 1))
      {
        set_current(loc);
        return true;
      }
      ++loc;
    }
    else
    {
      loc = e - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + 1 > end_)
        return false;
    }
  }
}

/// One char followed by 4 to 8 characters
bool Matcher::advance_char_pmh(size_t loc)
{
  char chr0 = pat_->chr_[0];
  size_t min = pat_->min_;
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_;
    s = static_cast<const char*>(std::memchr(s, chr0, e - s));
    if (s != NULL)
    {
      loc = s - buf_;
      if (s + 1 + min > e || pat_->predict_match(s + 1, min))
      {
        set_current(loc);
        return true;
      }
      ++loc;
    }
    else
    {
      loc = e - buf_;
      set_current_and_peek_more(loc - 1);
      loc = cur_ + 1;
      if (loc + 1 > end_)
        return false;
    }
  }
}

/// Few chars
template <uint8_t LEN>
bool Matcher::advance_chars(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
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
        loc = s - lcp + offset - buf_;
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 1) == 0))
        {
          set_current(loc);
          return true;
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN > end_)
      return false;
    if (loc + LEN + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
    uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
    uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
        loc = s - lcp - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            set_current(loc);
            return true;
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      mask = vgetq_lane_u64(vmask64, 1);
      if (mask != 0)
      {
        loc = s - lcp + 8 - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            set_current(loc);
            return true;
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN > end_)
      return false;
    if (loc + LEN + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
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
      if (LEN == 2 ||
          (LEN == 3 ? s[1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp, chr + 1, LEN - 1) == 0))
      {
        loc = s - lcp - buf_;
        set_current(loc);
        return true;
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN > end_)
      return false;
  }
}

/// Few chars followed by 1 to 4 characters
template<uint8_t LEN>
bool Matcher::advance_chars_pma(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
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
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 1) == 0))
        {
          loc = s - lcp + offset - buf_;
          if (loc + LEN + 4 > end_ || pat_->predict_match(&buf_[loc + LEN]))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
    uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
    uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
        loc = s - lcp - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            if (loc + LEN + 4 > end_ || pat_->predict_match(&buf_[loc + LEN]))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      mask = vgetq_lane_u64(vmask64, 1);
      if (mask != 0)
      {
        loc = s - lcp + 8 - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            if (loc + LEN + 4 > end_ || pat_->predict_match(&buf_[loc + LEN]))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
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
      if (LEN == 2 ||
          (LEN == 3 ? s[1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp, chr + 1, LEN - 1) == 0))
      {
        loc = s - lcp - buf_;
        if (loc + LEN + 4 > end_ || pat_->predict_match(&buf_[loc + LEN]))
        {
          set_current(loc);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
  }
}

/// Few chars followed by 4 to 8 characters
template<uint8_t LEN>
bool Matcher::advance_chars_pmh(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
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
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 1) == 0))
        {
          loc = s - lcp + offset - buf_;
          if (pat_->predict_match(&buf_[loc + LEN], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
    uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
    uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
        loc = s - lcp - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            if (pat_->predict_match(&buf_[loc + LEN], min))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      mask = vgetq_lane_u64(vmask64, 1);
      if (mask != 0)
      {
        loc = s - lcp + 8 - buf_;
        if (static_cast<uint32_t>(mask) == 0)
        {
          mask >>= 32;
          loc += 4;
        }
        do
        {
          if ((mask & 0xff) &&
              (LEN == 2 ||
               (LEN == 3 ? buf_[loc + 1] == chr[1] : std::memcmp(&buf_[loc + 1], chr + 1, LEN - 1) == 0)))
          {
            if (pat_->predict_match(&buf_[loc + LEN], min))
            {
              set_current(loc);
              return true;
            }
          }
          mask >>= 8;
          ++loc;
        } while (mask != 0);
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
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
      if (LEN == 2 ||
          (LEN == 3 ? s[1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp, chr + 1, LEN - 1) == 0))
      {
        loc = s - lcp - buf_;
        if (pat_->predict_match(&buf_[loc + LEN], min))
        {
          set_current(loc);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
  }
}

/// String
bool Matcher::advance_string(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  // implements SSE2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
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
          return true;
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len > end_)
      return false;
    if (loc + len + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
    if (simd_advance_string_neon(s, e))
      return true;
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len > end_)
      return false;
    if (loc + len + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
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
      if (std::memcmp(s - lcp, chr, len) == 0)
      {
        loc = s - lcp - buf_;
        set_current(loc);
        return true;
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len > end_)
      return false;
  }
}

/// String followed by 1 to 4 characters
bool Matcher::advance_string_pma(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  // implements SSE2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
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
          if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
    if (simd_advance_string_pma_neon(s, e))
      return true;
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
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
      if (std::memcmp(s - lcp, chr, len) == 0)
      {
        loc = s - lcp - buf_;
        if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
        {
          set_current(loc);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
  }
}

/// String followed by 4 to 8 characters
bool Matcher::advance_string_pmh(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
  // implements SSE2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
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
          if (pat_->predict_match(&buf_[loc + len], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 16;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 15 > end_)
      break;
  }
#elif defined(HAVE_NEON)
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
    if (simd_advance_string_pmh_neon(s, e))
      return true;
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 15 > end_)
      break;
  }
#endif
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
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
      if (std::memcmp(s - lcp, chr, len) == 0)
      {
        loc = s - lcp - buf_;
        if (pat_->predict_match(&buf_[loc + len], min))
        {
          set_current(loc);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
  }
}

#if defined(HAVE_NEON)

// Implements NEON/AArch64 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html 64 bit optimized
bool Matcher::simd_advance_string_neon(const char *&s, const char *e)
{
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  size_t len = pat_->len_;
  const char *chr = pat_->chr_;
  uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
  uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
      size_t loc = s - lcp - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          set_current(loc);
          return true;
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    mask = vgetq_lane_u64(vmask64, 1);
    if (mask != 0)
    {
      size_t loc = s - lcp + 8 - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          set_current(loc);
          return true;
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    s += 16;
  }
  return false;
}

// Implements NEON/AArch64 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html 64 bit optimized
bool Matcher::simd_advance_string_pma_neon(const char *&s, const char *e)
{
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  size_t len = pat_->len_;
  const char *chr = pat_->chr_;
  uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
  uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
      size_t loc = s - lcp - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
          {
            set_current(loc);
            return true;
          }
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    mask = vgetq_lane_u64(vmask64, 1);
    if (mask != 0)
    {
      size_t loc = s - lcp + 8 - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
          {
            set_current(loc);
            return true;
          }
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    s += 16;
  }
  return false;
}

// Implements NEON/AArch64 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html 64 bit optimized
bool Matcher::simd_advance_string_pmh_neon(const char *&s, const char *e)
{
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  const char *chr = pat_->chr_;
  uint8x16_t vlcp = vdupq_n_u8(chr[lcp]);
  uint8x16_t vlcs = vdupq_n_u8(chr[lcs]);
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
      size_t loc = s - lcp - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          if (pat_->predict_match(&buf_[loc + len], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    mask = vgetq_lane_u64(vmask64, 1);
    if (mask != 0)
    {
      size_t loc = s - lcp + 8 - buf_;
      if (static_cast<uint32_t>(mask) == 0)
      {
        mask >>= 32;
        loc += 4;
      }
      do
      {
        if ((mask & 0xff) && std::memcmp(&buf_[loc], chr, len) == 0)
        {
          if (pat_->predict_match(&buf_[loc + len], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask >>= 8;
        ++loc;
      } while (mask != 0);
    }
    s += 16;
  }
  return false;
}

#endif // HAVE_NEON

/// My homegrown improved Boyer-Moore string search, a fallback method
bool Matcher::advance_string_bm(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint8_t *bms = pat_->bms_;
  size_t len = pat_->len_;
  size_t bmd = pat_->bmd_;
  uint16_t lcp = pat_->lcp_;
  while (true)
  {
    const char *s = buf_ + loc + len - 1;
    const char *e = buf_ + end_;
    const char *t = chr + len - 1;
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
        return true;
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

/// My homegrown improved Boyer-Moore string search followed by 1 to 3 minimal byte long patterns, using PM4
bool Matcher::advance_string_bm_pma(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint8_t *bms = pat_->bms_;
  size_t len = pat_->len_;
  size_t bmd = pat_->bmd_;
  uint16_t lcp = pat_->lcp_;
  while (true)
  {
    const char *s = buf_ + loc + len - 1;
    const char *e = buf_ + end_;
    const char *t = chr + len - 1;
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
        if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
        {
          set_current(loc);
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

/// My homegrown improved Boyer-Moore string search followed by 4 minimal bytes long patterns
bool Matcher::advance_string_bm_pmh(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint8_t *bms = pat_->bms_;
  size_t bmd = pat_->bmd_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  while (true)
  {
    const char *s = buf_ + loc + len - 1;
    const char *e = buf_ + end_;
    const char *t = chr + len - 1;
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
        if (loc + len + min > end_ || pat_->predict_match(&buf_[loc + len], min))
        {
          set_current(loc);
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

} // namespace reflex
