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
@file      matcher_avx512bw.cpp
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#if defined(HAVE_AVX512BW)
# if !defined(__AVX512BW__)
#  error matcher_avx512bw.cpp must be compiled with -mavx512bw or /arch:avx512.
# endif
#endif

#include <reflex/matcher.h>

namespace reflex {

#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))

// AVX512BW runtime optimized function callback overrides
void Matcher::simd_init_advance_avx512bw()
{
  if (pat_->len_ == 0)
  {
    // no specialization
  }
  else if (pat_->len_ == 1)
  {
    // no specialization
  }
  else if (pat_->len_ == 2)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_chars_avx512bw<2>;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::simd_advance_chars_pma_avx512bw<2>;
    else
      adv_ = &Matcher::simd_advance_chars_pmh_avx512bw<2>;
  }
  else if (pat_->len_ == 3)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_chars_avx512bw<3>;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::simd_advance_chars_pma_avx512bw<3>;
    else
      adv_ = &Matcher::simd_advance_chars_pmh_avx512bw<3>;
  }
  else if (pat_->bmd_ == 0)
  {
    if (pat_->min_ >= 4)
      adv_ = &Matcher::simd_advance_string_pmh_avx512bw;
    else if (pat_->min_ > 0)
      adv_ = &Matcher::simd_advance_string_pma_avx512bw;
    else
      adv_ = &Matcher::simd_advance_string_avx512bw;
  }
}

/// Few chars
template<uint8_t LEN>
bool Matcher::simd_advance_chars_avx512bw(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
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
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          loc = s - lcp + offset - buf_;
          set_current(loc);
          return true;
        }
        mask &= mask - 1;
      }
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN > end_)
      return false;
    if (loc + LEN + 63 > end_)
      break;
  }
  return advance_chars<LEN>(loc);
}

/// Few chars followed by 2 to 3 minimal char pattern
template<uint8_t LEN>
bool Matcher::simd_advance_chars_pma_avx512bw(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
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
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
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
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 63 > end_)
      break;
  }
  return advance_chars_pma<LEN>(loc);
}

/// Few chars followed by 4 minimal char pattern
template<uint8_t LEN>
bool Matcher::simd_advance_chars_pmh_avx512bw(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
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
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          loc = s - lcp + offset - buf_;
          if (loc + LEN + min > end_ || pat_->predict_match(&buf_[loc + LEN], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 63 > end_)
      break;
  }
  return advance_chars_pmh<LEN>(loc);
}

/// Implements AVX512BW string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_avx512bw(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
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
          return true;
        }
        mask &= mask - 1;
      }
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len > end_)
      return false;
    if (loc + len + 63 > end_)
      break;
  }
  return advance_string(loc);
}

/// Implements AVX512BW string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_pma_avx512bw(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
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
          if (loc + len + 4 > end_ || pat_->predict_match(&buf_[loc + len]))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 63 > end_)
      break;
  }
  return advance_string_pma(loc);
}

/// Implements AVX512BW string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_pmh_avx512bw(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
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
          if (loc + len + min > end_ || pat_->predict_match(&buf_[loc + len], min))
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 64;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 63 > end_)
      break;
  }
  return advance_string_pmh(loc);
}

#else

// appease ranlib "has no symbols"
void matcher_not_compiled_with_avx512bw() { }

#endif

} // namespace reflex
