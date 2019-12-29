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

namespace reflex {

/// Boyer-Moore preprocessing of the given pattern pat of length len, generates bmd_ > 0 and bms_[] shifts.
void Matcher::boyer_moore_init(const char *pat, size_t len)
{
  // Relative frquency table of English letters, source code, and UTF-8 bytes
  static unsigned char freq[256] = "\0\0\0\0\0\0\0\0\0\73\4\0\0\4\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\73\70\70\1\1\2\2\70\70\70\2\2\70\70\70\2\3\3\3\3\3\3\3\3\3\3\70\70\70\70\70\70\2\35\14\24\26\37\20\17\30\33\11\12\25\22\32\34\15\7\27\31\36\23\13\21\10\16\6\70\1\70\2\70\1\67\46\56\60\72\52\51\62\65\43\44\57\54\64\66\47\41\61\63\71\55\45\53\42\50\40\70\2\70\2\0\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\0\0\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\46\56\56\56\56\56\56\56\56\56\56\56\56\46\56\56\73\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
  size_t i;
  for (i = 0; i < 256; ++i)
    bms_[i] = static_cast<uint8_t>(len);
  size_t sum = 0;
  lcp_ = 0;
  for (i = 0; i < len; ++i)
  {
    uint8_t pch = static_cast<uint8_t>(pat[i]);
    bms_[pch] = static_cast<uint8_t>(len - i - 1);
    sum += bms_[pch];
    if (freq[static_cast<uint8_t>(pat[lcp_])] > freq[pch])
      lcp_ = i;
  }
  size_t j;
  for (i = len - 1, j = i; j > 0; --j)
    if (pat[j - 1] == pat[i])
      break;
  bmd_ = i - j + 1;
  sum /= len;
  uint8_t fch = freq[static_cast<uint8_t>(pat[lcp_])];
  if (sum > 1 && fch > 35 && (sum > 3 || fch > 48) && fch + sum > 48)
    lcp_ = 0xffff;
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
            if (Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
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
            if (Pattern::predict_match(pat_->pmh_, &buf_[loc + len], min))
              return true;
          }
          else
          {
            if (Pattern::predict_match(pat_->pma_, &buf_[loc + len]) == 0)
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
