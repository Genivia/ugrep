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
@file      matcher.cpp
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/matcher.h>

// minimal anchor support for greater speed, disables \i, \j, \k
// #define WITH_MINIMAL

// minimal length of the prefix pattern for Boyer-Moore search
#define BOYER_MOORE_MIN_LENGTH 9

namespace reflex {

size_t Matcher::match(Method method)
{
  DBGLOG("BEGIN Matcher::match()");
  reset_text();
  len_ = 0; // split text length starts with 0
scan:
  txt_ = buf_ + cur_;
#if !defined(WITH_MINIMAL)
  mrk_ = false;
  ind_ = pos_; // ind scans input in buf[] in newline() up to pos - 1
  col_ = 0; // count columns for indent matching
  if (ded_ == 0 && hit_end() && tab_.empty())
  {
    if (method == Const::SPLIT && !at_bob() && cap_ != 0 && cap_ != Const::EMPTY)
    {
      cap_ = Const::EMPTY;
      DBGLOG("Split empty at end, cap = %zu", cap_);
      DBGLOG("END Matcher::match()");
      return cap_;
    }
    cap_ = 0;
    DBGLOG("END Matcher::match()");
    return 0;
  }
#endif
find:
  int c1 = got_;
  bool bol = at_bol();
  if (pat_->fsm_)
    fsm_.c1 = c1;
redo:
  lap_.resize(0);
  cap_ = 0;
  bool nul = method == Const::MATCH;
  if (pat_->fsm_)
  {
    DBGLOG("FSM code %p", pat_->fsm_);
    fsm_.bol = bol;
    fsm_.nul = nul;
    pat_->fsm_(*this);
    nul = fsm_.nul;
    c1 = fsm_.c1;
  }
  else if (pat_->opc_)
  {
    const Pattern::Opcode *pc = pat_->opc_;
    while (true)
    {
      Pattern::Opcode opcode = *pc;
      DBGLOG("Fetch: code[%zu] = 0x%08X", pc - pat_->opc_, opcode);
      Pattern::Index index;
      if (Pattern::is_opcode_halt(opcode))
        break;
      if (Pattern::is_opcode_meta(opcode))
      {
        switch (opcode >> 16)
        {
          case 0xFF00: // TAKE
            cap_ = Pattern::index_of(opcode);
            DBGLOG("Take: cap = %zu", cap_);
            cur_ = pos_;
            ++pc;
            continue;
          case 0xFF7E: // TAIL
            index = Pattern::index_of(opcode);
            DBGLOG("Tail: %u", index);
            if (lap_.size() > index && lap_[index] >= 0)
              cur_ = txt_ - buf_ + static_cast<size_t>(lap_[index]); // mind the (new) gap
            ++pc;
            continue;
          case 0xFF7F: // HEAD
            index = Pattern::index_of(opcode);
            DBGLOG("Head: lookahead[%u] = %zu", index, pos_ - (txt_ - buf_));
            if (lap_.size() <= index)
              lap_.resize(index + 1, -1);
            lap_[index] = static_cast<int>(pos_ - (txt_ - buf_)); // mind the gap
            ++pc;
            continue;
#if !defined(WITH_MINIMAL)
          case 0xFF00 | Pattern::META_DED:
            if (ded_ > 0)
            {
              index = Pattern::index_of(opcode);
              DBGLOG("Dedent ded = %zu", ded_); // unconditional dedent matching \j
              nul = true;
              pc = pat_->opc_ + index;
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
        Pattern::Index la;
        index = Pattern::Const::IMAX;
        while (true)
        {
          if (Pattern::is_opcode_meta(opcode) && (index == Pattern::Const::IMAX || back == Pattern::Const::IMAX))
          {
            // we no longer have to pass through all if index and back are set
            switch (opcode >> 16)
            {
              case 0xFF00: // TAKE
                cap_ = Pattern::index_of(opcode);
                DBGLOG("Take: cap = %zu", cap_);
                cur_ = pos_;
                if (c1 != EOF)
                  --cur_; // must unget one char
                opcode = *++pc;
                continue;
              case 0xFF7E: // TAIL
                la = Pattern::index_of(opcode);
                DBGLOG("Tail: %u", la);
                if (lap_.size() > la && lap_[la] >= 0)
                  cur_ = txt_ - buf_ + static_cast<size_t>(lap_[la]); // mind the (new) gap
                opcode = *++pc;
                continue;
              case 0xFF7F: // HEAD
                opcode = *++pc;
                continue;
#if !defined(WITH_MINIMAL)
              case 0xFF00 | Pattern::META_DED:
                DBGLOG("DED? %d", c1);
                if (index == Pattern::Const::IMAX && back == Pattern::Const::IMAX && bol && dedent())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_IND:
                DBGLOG("IND? %d", c1);
                if (index == Pattern::Const::IMAX && back == Pattern::Const::IMAX && bol && indent())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_UND:
                DBGLOG("UND");
                if (mrk_)
                  index = Pattern::index_of(opcode);
                mrk_ = false;
                ded_ = 0;
                opcode = *++pc;
                continue;
#endif
              case 0xFF00 | Pattern::META_EOB:
                DBGLOG("EOB? %d", c1);
                if (index == Pattern::Const::IMAX && c1 == EOF)
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_BOB:
                DBGLOG("BOB? %d", at_bob());
                if (index == Pattern::Const::IMAX && at_bob())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_EOL:
                DBGLOG("EOL? %d", c1);
                if (index == Pattern::Const::IMAX && (c1 == EOF || c1 == '\n'))
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_BOL:
                DBGLOG("BOL? %d", bol);
                if (index == Pattern::Const::IMAX && bol)
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
#if !defined(WITH_MINIMAL)
              case 0xFF00 | Pattern::META_EWE:
                DBGLOG("EWE? %d %d %d", c0, c1, isword(c0) && !isword(c1));
                if (index == Pattern::Const::IMAX && isword(c0) && !isword(c1))
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_BWE:
                DBGLOG("BWE? %d %d %d", c0, c1, !isword(c0) && isword(c1));
                if (index == Pattern::Const::IMAX && !isword(c0) && isword(c1))
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_EWB:
                DBGLOG("EWB? %d", at_eow());
                if (index == Pattern::Const::IMAX && at_eow())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_BWB:
                DBGLOG("BWB? %d", at_bow());
                if (index == Pattern::Const::IMAX && at_bow())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_NWE:
                DBGLOG("NWE? %d %d %d", c0, c1, isword(c0) == isword(c1));
                if (index == Pattern::Const::IMAX && isword(c0) == isword(c1))
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
              case 0xFF00 | Pattern::META_NWB:
                DBGLOG("NWB? %d %d", at_bow(), at_eow());
                if (index == Pattern::Const::IMAX && !at_bow() && !at_eow())
                  index = Pattern::index_of(opcode);
                opcode = *++pc;
                continue;
#endif
            }
          }
          if (index == Pattern::Const::IMAX)
          {
            if (back != Pattern::Const::IMAX)
            {
              pc = pat_->opc_ + back;
              opcode = *pc;
            }
            break;
          }
          DBGLOG("Backtrack: pc = %u", index);
          if (back == Pattern::Const::IMAX)
            back = static_cast<Pattern::Index>(pc - pat_->opc_);
          pc = pat_->opc_ + index;
          opcode = *pc;
          index = Pattern::Const::IMAX;
        }
        if (c1 == EOF)
          break;
      }
      else
      {
        if (c1 == EOF)
          break;
        c1 = get();
        DBGLOG("Get: c1 = %d", c1);
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
      index = Pattern::index_of(opcode);
      if (index == Pattern::Const::IMAX)
        break;
      if (index == 0 && cap_ == 0) // failed to match so far, set cur_ to move forward from cur_ + 1 with FIND advance()
        cur_ = pos_;
      pc = pat_->opc_ + index;
    }
  }
#if !defined(WITH_MINIMAL)
  if (mrk_ && cap_ != Const::EMPTY)
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
      if (n > 0)
        tab_.back() = col_; // adjust stop when indents are not aligned (Python would give an error)
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
      bol = false; // avoid looping, match \j exactly
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
      {
        cap_ = Const::EMPTY;
        set_current(pos_);
        got_ = Const::EOB;
      }
      DBGLOG("Split at eof: cap = %zu txt = '%s' len = %zu", cap_, std::string(txt_, len_).c_str(), len_);
      DBGLOG("END Matcher::match()");
      return cap_;
    }
    if (cur_ == 0 && at_bob() && at_end())
      cap_ = Const::EMPTY;
    set_current(cur_);
    DBGLOG("Split: txt = '%s' len = %zu", std::string(txt_, len_).c_str(), len_);
    DBGLOG("END Matcher::match()");
    return cap_;
  }
  if (cap_ == 0)
  {
    if (method == Const::FIND && !at_end())
    {
      if (pos_ == cur_ + 1) // early fail after one non-matching char, i.e. no META executed
      {
        if (advance())
        {
          txt_ = buf_ + cur_;
          goto find;
        }
      }
      else if (pos_ > cur_) // we didn't fail on META alone
      {
        if (advance())
          goto scan;
      }
      txt_ = buf_ + cur_;
    }
    else
    {
      cur_ = txt_ - buf_; // no match: backup to begin of unmatched text
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
      set_current(++cur_); // skip one char to keep searching
      if (cap_ == 0 || !opt_.N || (!bol && c1 == '\n')) // allow FIND with "N" to match an empty line, with ^$ etc.
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
    if (cap_ == Const::EMPTY && !opt_.A)
      cap_ = 0;
  }
  else
  {
    set_current(cur_);
    if (len_ > 0)
    {
      if (cap_ == Const::EMPTY && !opt_.A)
      {
        DBGLOG("Ignore accept and continue: len = %zu", len_);
        len_ = 0;
        if (method != Const::MATCH)
          goto scan;
        cap_ = 0;
      }
    }
  }
  DBGLOG("Return: cap = %zu txt = '%s' len = %zu pos = %zu got = %d", cap_, std::string(txt_, len_).c_str(), len_, pos_, got_);
  DBGLOG("END match()");
  return cap_;
}

// advance input cursor after mismatch to align input for next match
bool Matcher::advance()
{
  size_t loc = cur_ + 1;
  size_t min = pat_->min_;
  if (pat_->pre_.empty())
  {
    if (min == 0)
      return false;
    if (loc + min > end_)
    {
      set_current_match(loc - 1);
      peek_more();
      loc = cur_ + 1;
      if (loc + min > end_)
      {
        set_current(loc);
        return false;
      }
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
          if (predict_match(pat_->pmh_, s, min))
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
          peek_more();
          loc = cur_ + min;
          if (loc >= end_)
          {
            set_current(loc);
            return false;
          }
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
          if (s + 4 > e || predict_match(pma, s) == 0)
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
          peek_more();
          loc = cur_ + 3;
          if (loc >= end_)
          {
            set_current(loc);
            return false;
          }
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
          if (s + 4 > e || predict_match(pma, s) == 0)
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
          peek_more();
          loc = cur_ + 2;
          if (loc >= end_)
          {
            set_current(loc);
            return false;
          }
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
        size_t k = predict_match(pma, s);
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
        peek_more();
        loc = cur_ + 1;
        if (loc >= end_)
        {
          set_current(loc);
          return false;
        }
      }
    }
  }
  const char *pre = pat_->pre_.c_str();
  size_t len = static_cast<uint8_t>(pat_->pre_.size()); // okay to cast: actually never more than 255
  if (bmd_ == 0)
    boyer_moore_init(pre, len);
  while (true)
  {
    const char *s = buf_ + loc + len - 1;
    const char *e = buf_ + end_;
    const char *t = pre + len - 1;
    if (lcp_ < len)
    {
      while (s < e)
      {
        const char *hit = static_cast<const char*>(std::memchr(s - len + 1 + lcp_, pre[lcp_], e - s));
        if (hit == NULL)
        {
          s = e;
          break;
        }
        s = hit + len - 1 - lcp_;
        size_t k = bms_[static_cast<uint8_t>(*s)];
        if (k > 0)
        {
          s += k;
          continue;
        }
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
          if (min == 0 || loc + 4 > end_)
            return true;
          if (min >= 4)
          {
            if (predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (predict_match(pat_->pma_, &buf_[loc + len]) == 0)
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
    }
    else
    {
      while (s < e)
      {
        size_t k = 0;
        do
        {
          k = bms_[static_cast<uint8_t>(*s)];
          s += k;
        } while (k > 0 && s < e);
        if (k > 0)
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
          if (min == 0 || loc + 4 > end_)
            return true;
          if (min >= 4)
          {
            if (predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (predict_match(pat_->pma_, &buf_[loc + len]) == 0)
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
    }
    s -= len - 1;
    loc = s - buf_;
    set_current_match(loc - 1);
    peek_more();
    loc = cur_ + 1;
    if (loc + len > end_)
    {
      set_current(loc);
      return false;
    }

  }
}

} // namespace reflex
