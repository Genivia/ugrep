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
@file      matcher_avx2.cpp
@brief     RE/flex matcher engine
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#if defined(HAVE_AVX2) || defined(HAVE_AVX512BW)
# if !defined(__AVX2__) && !defined(__AVX512BW__)
#  error simd_avx2.cpp must be compiled with -mavx2 or /arch:avx2.
# endif
#endif

#include <reflex/matcher.h>

namespace reflex {

#if defined(HAVE_AVX2) || defined(HAVE_AVX512BW)

#define SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(PIN) \
  if (pat_->min_ <= 0) \
    adv_ = &Matcher::simd_advance_pattern_pin##PIN##_one_avx2; \
  else \
    adv_ = &Matcher::simd_advance_pattern_pin##PIN##_pma_avx2;

// AVX2 runtime optimized function callback overrides
void Matcher::simd_init_advance_avx2()
{
  if (pat_->len_ == 0)
  {
    switch (pat_->pin_)
    {
      case 1:
        if (pat_->min_ >= 2)
          adv_ = &Matcher::simd_advance_pattern_pin1_pma_avx2;
        break;
      case 2:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(2);
        break;
      case 3:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(3);
        break;
      case 4:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(4);
        break;
      case 5:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(5);
        break;
      case 6:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(6);
        break;
      case 7:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(7);
        break;
      case 8:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(8);
        break;
      case 16:
        SIMD_INIT_ADV_PAT_PIN_CASE_AVX2(16);
        break;
#ifdef WITH_BITAP_AVX2 // use in case vectorized bitap (hashed) is faster than serial version (typically not!!)
      default:
        switch (pat_->min_)
        {
          case 4:
            adv_ = &Matcher::simd_advance_pattern_min4_avx2<4>;
            break;
          case 5:
            adv_ = &Matcher::simd_advance_pattern_min4_avx2<5>;
            break;
          case 6:
            adv_ = &Matcher::simd_advance_pattern_min4_avx2<6>;
            break;
          case 7:
            adv_ = &Matcher::simd_advance_pattern_min4_avx2<7>;
            break;
          case 8:
            adv_ = &Matcher::simd_advance_pattern_min4_avx2<8>;
            break;
        }
#endif
    }
  }
  else if (pat_->len_ == 1)
  {
    // no specialization
  }
  else if (pat_->len_ == 2)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_chars_avx2<2>;
    else
      adv_ = &Matcher::simd_advance_chars_pma_avx2<2>;
  }
  else if (pat_->len_ == 3)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_chars_avx2<3>;
    else
      adv_ = &Matcher::simd_advance_chars_pma_avx2<3>;
  }
  else if (pat_->bmd_ == 0)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_string_avx2;
    else
      adv_ = &Matcher::simd_advance_string_pma_avx2;
  }
}

// My homegrown "needle search" method when needle pin=1
bool Matcher::simd_advance_pattern_pin1_pma_avx2(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint16_t min = pat_->min_;
  const uint16_t lcp = pat_->lcp_;
  const uint16_t lcs = pat_->lcs_;
  const char chr0 = chr[0];
  const char chr1 = chr[1];
  __m256i vlcp = _mm256_set1_epi8(chr0);
  __m256i vlcs = _mm256_set1_epi8(chr1);
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
      while (REFLEX_UNLIKELY(mask != 0))
      {
        uint32_t offset = ctz(mask);
        size_t k = s - lcp + offset - buf_;
        set_current(k);
        if (REFLEX_UNLIKELY(k + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k]))
          return true;
        mask &= mask - 1;
      }
      s += 32;
    }
    e = buf_ + end_;
    if (s < e && (s = static_cast<const char*>(std::memchr(s, chr0, e - s))) != NULL)
    {
      s -= lcp;
      loc = s - buf_;
      if (REFLEX_UNLIKELY(s > e - Pattern::Const::PM_M) || (s[lcs] == chr1 && pat_->predict_match(s)))
      {
        set_current(loc);
        return true;
      }
      ++loc;
    }
    else
    {
      if (e > buf_ + loc + lcp)
        loc = e - buf_ - lcp;
      set_current_and_peek_more(loc);
      loc = cur_;
      if (loc + min > end_ && eof_)
        return false;
    }
  }
}

// My homegrown "needle search" methods
#define ADV_PAT_PIN_ONE(N, INIT, COMP) \
bool Matcher::simd_advance_pattern_pin##N##_one_avx2(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc; \
    const char *e = buf_ + end_; \
    while (s <= e - 32) \
    { \
      __m256i vstr = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s)); \
      __m256i veq = _mm256_cmpeq_epi8(v0, vstr); \
      COMP \
      uint32_t mask = _mm256_movemask_epi8(veq); \
      while (REFLEX_UNLIKELY(mask != 0)) \
      { \
        uint32_t offset = ctz(mask); \
        size_t k = s + offset - buf_; \
        if (REFLEX_UNLIKELY(k + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k])) \
        { \
          set_current(k); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 32; \
    } \
    while (s < e - (Pattern::Const::PM_M - 1)) \
    { \
      if (pat_->predict_match(s++)) \
      { \
        size_t k = s - buf_ - 1; \
        set_current(k); \
        return true; \
      } \
    } \
    loc = s - buf_; \
    set_current_and_peek_more(loc); \
    loc = cur_; \
    if (loc + (Pattern::Const::PM_M - 1) >= end_) \
      return true; \
  } \
}

ADV_PAT_PIN_ONE(2, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
  )

ADV_PAT_PIN_ONE(3, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
  )

ADV_PAT_PIN_ONE(4, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
  )

ADV_PAT_PIN_ONE(5, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
    __m256i v4 = _mm256_set1_epi8(chr[4]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v4, vstr)); \
  )

ADV_PAT_PIN_ONE(6, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
    __m256i v4 = _mm256_set1_epi8(chr[4]); \
    __m256i v5 = _mm256_set1_epi8(chr[5]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v4, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v5, vstr)); \
  )

ADV_PAT_PIN_ONE(7, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
    __m256i v4 = _mm256_set1_epi8(chr[4]); \
    __m256i v5 = _mm256_set1_epi8(chr[5]); \
    __m256i v6 = _mm256_set1_epi8(chr[6]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v4, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v5, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v6, vstr)); \
  )

ADV_PAT_PIN_ONE(8, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
    __m256i v4 = _mm256_set1_epi8(chr[4]); \
    __m256i v5 = _mm256_set1_epi8(chr[5]); \
    __m256i v6 = _mm256_set1_epi8(chr[6]); \
    __m256i v7 = _mm256_set1_epi8(chr[7]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v4, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v5, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v6, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v7, vstr)); \
  )

ADV_PAT_PIN_ONE(16, \
    __m256i v0 = _mm256_set1_epi8(chr[0]); \
    __m256i v1 = _mm256_set1_epi8(chr[1]); \
    __m256i v2 = _mm256_set1_epi8(chr[2]); \
    __m256i v3 = _mm256_set1_epi8(chr[3]); \
    __m256i v4 = _mm256_set1_epi8(chr[4]); \
    __m256i v5 = _mm256_set1_epi8(chr[5]); \
    __m256i v6 = _mm256_set1_epi8(chr[6]); \
    __m256i v7 = _mm256_set1_epi8(chr[7]); \
    __m256i v8 = _mm256_set1_epi8(chr[8]); \
    __m256i v9 = _mm256_set1_epi8(chr[9]); \
    __m256i va = _mm256_set1_epi8(chr[10]); \
    __m256i vb = _mm256_set1_epi8(chr[11]); \
    __m256i vc = _mm256_set1_epi8(chr[12]); \
    __m256i vd = _mm256_set1_epi8(chr[13]); \
    __m256i ve = _mm256_set1_epi8(chr[14]); \
    __m256i vf = _mm256_set1_epi8(chr[15]); \
  , \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v1, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v2, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v3, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v4, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v5, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v6, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v7, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v8, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(v9, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(va, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(vb, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(vc, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(vd, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(ve, vstr)); \
    veq = _mm256_or_si256(veq, _mm256_cmpeq_epi8(vf, vstr)); \
  )

// My homegrown "needle search" methods
#define ADV_PAT_PIN(N, INIT, COMP) \
bool Matcher::simd_advance_pattern_pin##N##_pma_avx2(size_t loc) \
{ \
  const char *chr = pat_->chr_; \
  const uint16_t min = pat_->min_; \
  const uint16_t lcp = pat_->lcp_; \
  const uint16_t lcs = pat_->lcs_; \
  INIT \
  while (true) \
  { \
    const char *s = buf_ + loc + lcp; \
    const char *e = buf_ + end_ + lcp - min + 1; \
    while (s <= e - 32) \
    { \
      __m256i vstrlcp = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s)); \
      __m256i vstrlcs = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp)); \
      __m256i veqlcp = _mm256_cmpeq_epi8(vlcp0, vstrlcp); \
      __m256i veqlcs = _mm256_cmpeq_epi8(vlcs0, vstrlcs); \
      COMP \
      uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(veqlcp, veqlcs)); \
      while (REFLEX_UNLIKELY(mask != 0)) \
      { \
        uint32_t offset = ctz(mask); \
        size_t k = s - lcp + offset - buf_; \
        if (REFLEX_UNLIKELY(k + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k])) \
        { \
          set_current(k); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 32; \
    } \
    s -= lcp; \
    e = buf_ + end_ - (Pattern::Const::PM_M - 1); \
    while (s < e) \
    { \
      if (pat_->predict_match(s++)) \
      { \
        size_t k = s - buf_ - 1; \
        set_current(k); \
        return true; \
      } \
    } \
    loc = s - buf_; \
    set_current_and_peek_more(loc); \
    loc = cur_; \
    if (loc + min > end_ && eof_) \
      return false; \
    if (loc + (Pattern::Const::PM_M - 1) >= end_) \
      return true; \
  } \
}

ADV_PAT_PIN(2, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[3]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
  )

ADV_PAT_PIN(3, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[5]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
  )

ADV_PAT_PIN(4, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[7]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
  )

ADV_PAT_PIN(5, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcp4 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[7]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[8]); \
    __m256i vlcs4 = _mm256_set1_epi8(chr[9]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs)); \
  )

ADV_PAT_PIN(6, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcp4 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcp5 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[7]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[8]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[9]); \
    __m256i vlcs4 = _mm256_set1_epi8(chr[10]); \
    __m256i vlcs5 = _mm256_set1_epi8(chr[11]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs)); \
  )

ADV_PAT_PIN(7, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcp4 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcp5 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcp6 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[7]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[8]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[9]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[10]); \
    __m256i vlcs4 = _mm256_set1_epi8(chr[11]); \
    __m256i vlcs5 = _mm256_set1_epi8(chr[12]); \
    __m256i vlcs6 = _mm256_set1_epi8(chr[13]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs)); \
  )

ADV_PAT_PIN(8, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcp4 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcp5 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcp6 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcp7 = _mm256_set1_epi8(chr[7]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[8]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[9]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[10]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[11]); \
    __m256i vlcs4 = _mm256_set1_epi8(chr[12]); \
    __m256i vlcs5 = _mm256_set1_epi8(chr[13]); \
    __m256i vlcs6 = _mm256_set1_epi8(chr[14]); \
    __m256i vlcs7 = _mm256_set1_epi8(chr[15]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp7, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs7, vstrlcs)); \
  )

ADV_PAT_PIN(16, \
    __m256i vlcp0 = _mm256_set1_epi8(chr[0]); \
    __m256i vlcp1 = _mm256_set1_epi8(chr[1]); \
    __m256i vlcp2 = _mm256_set1_epi8(chr[2]); \
    __m256i vlcp3 = _mm256_set1_epi8(chr[3]); \
    __m256i vlcp4 = _mm256_set1_epi8(chr[4]); \
    __m256i vlcp5 = _mm256_set1_epi8(chr[5]); \
    __m256i vlcp6 = _mm256_set1_epi8(chr[6]); \
    __m256i vlcp7 = _mm256_set1_epi8(chr[7]); \
    __m256i vlcp8 = _mm256_set1_epi8(chr[8]); \
    __m256i vlcp9 = _mm256_set1_epi8(chr[9]); \
    __m256i vlcpa = _mm256_set1_epi8(chr[10]); \
    __m256i vlcpb = _mm256_set1_epi8(chr[11]); \
    __m256i vlcpc = _mm256_set1_epi8(chr[12]); \
    __m256i vlcpd = _mm256_set1_epi8(chr[13]); \
    __m256i vlcpe = _mm256_set1_epi8(chr[14]); \
    __m256i vlcpf = _mm256_set1_epi8(chr[15]); \
    __m256i vlcs0 = _mm256_set1_epi8(chr[16]); \
    __m256i vlcs1 = _mm256_set1_epi8(chr[17]); \
    __m256i vlcs2 = _mm256_set1_epi8(chr[18]); \
    __m256i vlcs3 = _mm256_set1_epi8(chr[19]); \
    __m256i vlcs4 = _mm256_set1_epi8(chr[20]); \
    __m256i vlcs5 = _mm256_set1_epi8(chr[21]); \
    __m256i vlcs6 = _mm256_set1_epi8(chr[22]); \
    __m256i vlcs7 = _mm256_set1_epi8(chr[23]); \
    __m256i vlcs8 = _mm256_set1_epi8(chr[24]); \
    __m256i vlcs9 = _mm256_set1_epi8(chr[25]); \
    __m256i vlcsa = _mm256_set1_epi8(chr[26]); \
    __m256i vlcsb = _mm256_set1_epi8(chr[27]); \
    __m256i vlcsc = _mm256_set1_epi8(chr[28]); \
    __m256i vlcsd = _mm256_set1_epi8(chr[29]); \
    __m256i vlcse = _mm256_set1_epi8(chr[30]); \
    __m256i vlcsf = _mm256_set1_epi8(chr[31]); \
  , \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp1, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp2, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp3, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp4, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp5, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp6, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp7, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp8, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcp9, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpa, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpb, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpc, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpd, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpe, vstrlcp)); \
    veqlcp = _mm256_or_si256(veqlcp, _mm256_cmpeq_epi8(vlcpf, vstrlcp)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs1, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs2, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs3, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs4, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs5, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs6, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs7, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs8, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcs9, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsa, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsb, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsc, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsd, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcse, vstrlcs)); \
    veqlcs = _mm256_or_si256(veqlcs, _mm256_cmpeq_epi8(vlcsf, vstrlcs)); \
  )

#ifdef WITH_BITAP_AVX2 // use in case vectorized bitap (hashed) is faster than serial version (typically not!!)

/// Minimal 4 byte long patterns (MIN>=4) using bitap hashed pairs with AVX2
template <uint8_t MIN>
bool Matcher::simd_advance_pattern_min4_avx2(size_t loc)
{
  const uint32_t btap = Pattern::Const::BTAP;
  const __m128i vmod = _mm_set1_epi32(btap - 1);
  const __m128i vselect = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, 9, 5, 1);
  const __m128i voffset = _mm_set_epi32(0, btap, 2 * btap, 3 * btap);
  uint32_t state0 = ~0u << (8 - (MIN - 1));
  uint32_t state1 = ~0u << (8 - (MIN - 2));
  uint32_t state2 = ~0u << (8 - (MIN - 3));
  uint32_t state3 = ~0u << (8 - (MIN - 4));
  if (MIN <= 6)
    state3 = state2;
  if (MIN <= 5)
    state2 = state1;
  if (MIN <= 4)
    state1 = state0;
  __m128i vstate = _mm_set_epi32(state0, state1, state2, state3);
  __m128i vc0 = _mm_set1_epi32(buf_[loc++]);
  while (true)
  {
    const char *s = buf_ + loc;
    const char *e = buf_ + end_ - 7;
    while (s < e)
    {
      __m128i vc1 = _mm_cvtepu8_epi32(_mm_loadu_si32(reinterpret_cast<const uint32_t*>(s)));
      vc0 = _mm_alignr_epi8(vc1, vc0, 12);
      // hash
      __m128i vh = _mm_and_si128(_mm_xor_si128(vc0, _mm_slli_epi32(vc1, 6)), vmod);
      // gather bitap hashed bits
      __m128i vb = _mm_i32gather_epi32(reinterpret_cast<const int32_t*>(pat_->vtp_), _mm_or_si128(vh, voffset), 2);
      // shift-or
      vstate = _mm_or_si128(_mm_slli_epi32(vstate, 4), vb);
      // pass last char to the next iteration
      vc0 = vc1;
      // get most significant bit of each byte, check each 2nd byte of the 4x32 bit words
      uint32_t mask = _mm_extract_epi32(_mm_shuffle_epi8(vstate, vselect), 0);
      if ((mask & 0x00000008) == 0 && pat_->predict_match(s - MIN + 0))
      {
        size_t k = s - buf_ - MIN + 0;
        set_current(k);
        return true;
      }
      if ((mask & 0x00000404) == 0 && pat_->predict_match(s - MIN + 1))
      {
        size_t k = s - buf_ - MIN + 1;
        set_current(k);
        return true;
      }
      if ((mask & 0x00020202) == 0 && pat_->predict_match(s - MIN + 2))
      {
        size_t k = s - buf_ - MIN + 2;
        set_current(k);
        return true;
      }
      if ((mask & 0x01010101) == 0 && pat_->predict_match(s - MIN + 3))
      {
        size_t k = s - buf_ - MIN + 3;
        set_current(k);
        return true;
      }
      // butterfly-or:
      //    a       b       c       d     input vec
      //    c       d       a       b     swizzle
      //   a|c     b|d     c|a     d|b    or
      //   b|d     a|c     d|b     c|a    swizzle
      // a|c|b|d b|d|a|c c|a|d|b d|b|c|a  or
      vstate = _mm_or_si128(vstate, _mm_shuffle_epi32(vstate, 0x4e)); // = 01 00 11 10 = 1 0 3 2
      vstate = _mm_or_si128(vstate, _mm_shuffle_epi32(vstate, 0xb1)); // = 10 11 00 01 = 2 3 0 1
      s += 4;
    }
    loc = s - buf_;
    size_t m =  std::min<size_t>(MIN, loc); // to clamp loc - MIN
    set_current_and_peek_more(loc - m); // clamp loc - MIN
    loc = cur_ + m;
    if (loc + 7 >= end_)
      return advance_pattern_min4(loc - MIN);
  }
}

#endif

/// Few chars
template<uint8_t LEN>
bool Matcher::simd_advance_chars_avx2(size_t loc)
{
  const uint16_t lcp = 0;
  const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN + 1;
    __m256i vlcp = _mm256_set1_epi8(chr[lcp]);
    __m256i vlcs = _mm256_set1_epi8(chr[lcs]);
    while (s <= e - 32)
    {
      __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
      __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
      __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
      __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
      uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
      while (REFLEX_UNLIKELY(mask != 0))
      {
        uint32_t offset = ctz(mask);
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          size_t k = s - lcp + offset - buf_;
          set_current(k);
          return true;
        }
        mask &= mask - 1;
      }
      s += 32;
    }
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
        size_t k = s - lcp - buf_;
        set_current(k);
        return true;
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc);
    loc = cur_;
    if (loc + LEN > end_ && eof_)
      return false;
  }
}

/// Few chars followed by 2 to 3 minimal char pattern
template<uint8_t LEN>
bool Matcher::simd_advance_chars_pma_avx2(size_t loc)
{
  const uint16_t lcp = 0;
  const uint16_t lcs = LEN - 1;
  const char *chr = pat_->chr_;
  const uint16_t min = pat_->min_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - LEN - min + 1;
    __m256i vlcp = _mm256_set1_epi8(chr[lcp]);
    __m256i vlcs = _mm256_set1_epi8(chr[lcs]);
    while (s <= e - 32)
    {
      __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
      __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
      __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
      __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
      uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
      while (REFLEX_UNLIKELY(mask != 0))
      {
        uint32_t offset = ctz(mask);
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          size_t k = s - lcp + offset - buf_;
          if (REFLEX_UNLIKELY(k + LEN + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k + LEN]))
          {
            set_current(k);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 32;
    }
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
        size_t k = s - lcp - buf_;
        if (REFLEX_UNLIKELY(k + LEN + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k + LEN]))
        {
          set_current(k);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc);
    loc = cur_;
    if (loc + LEN + min > end_ && eof_)
      return false;
  }
}

/// Implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_avx2(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint16_t len = pat_->len_;
  const uint16_t lcp = pat_->lcp_;
  const uint16_t lcs = pat_->lcs_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len + 1;
    __m256i vlcp = _mm256_set1_epi8(chr[lcp]);
    __m256i vlcs = _mm256_set1_epi8(chr[lcs]);
    while (s <= e - 32)
    {
      __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
      __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
      __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
      __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
      uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
      while (REFLEX_UNLIKELY(mask != 0))
      {
        uint32_t offset = ctz(mask);
        if (std::memcmp(s - lcp + offset, chr, len) == 0)
        {
          size_t k = s - lcp + offset - buf_;
          set_current(k);
          return true;
        }
        mask &= mask - 1;
      }
      s += 32;
    }
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
        size_t k = s - lcp - buf_;
        set_current(k);
        return true;
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc);
    loc = cur_;
    if (loc + len > end_ && eof_)
      return false;
  }
}

/// Implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_pma_avx2(size_t loc)
{
  const char *chr = pat_->chr_;
  const uint16_t len = pat_->len_;
  const uint16_t min = pat_->min_;
  const uint16_t lcp = pat_->lcp_;
  const uint16_t lcs = pat_->lcs_;
  while (true)
  {
    const char *s = buf_ + loc + lcp;
    const char *e = buf_ + end_ + lcp - len - min + 1;
    __m256i vlcp = _mm256_set1_epi8(chr[lcp]);
    __m256i vlcs = _mm256_set1_epi8(chr[lcs]);
    while (s <= e - 32)
    {
      __m256i vlcpm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
      __m256i vlcsm = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + lcs - lcp));
      __m256i vlcpeq = _mm256_cmpeq_epi8(vlcp, vlcpm);
      __m256i vlcseq = _mm256_cmpeq_epi8(vlcs, vlcsm);
      uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(vlcpeq, vlcseq));
      while (REFLEX_UNLIKELY(mask != 0))
      {
        uint32_t offset = ctz(mask);
        if (std::memcmp(s - lcp + offset, chr, len) == 0)
        {
          size_t k = s - lcp + offset - buf_;
          if (REFLEX_UNLIKELY(k + len + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k + len]))
          {
            set_current(k);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 32;
    }
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
        size_t k = s - lcp - buf_;
        if (REFLEX_UNLIKELY(k + len + Pattern::Const::PM_M > end_) || pat_->predict_match(&buf_[k + len]))
        {
          set_current(k);
          return true;
        }
      }
      ++s;
    }
    loc = s - lcp - buf_;
    set_current_and_peek_more(loc);
    loc = cur_;
    if (loc + len + min > end_ && eof_)
      return false;
  }
}

#else

// appease ranlib "has no symbols"
void matcher_not_compiled_with_avx2() { }

#endif

} // namespace reflex
