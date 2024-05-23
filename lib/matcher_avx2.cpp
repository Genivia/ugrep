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
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
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

// AVX2 runtime optimized function callback overrides
void Matcher::simd_init_advance_avx2()
{
  if (pat_->len_ == 0)
  {
    switch (pat_->pin_)
    {
      case 1:
        if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin1_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin1_pmh_avx2;
        break;
      case 2:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin2_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin2_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin2_pmh_avx2;
        break;
      case 3:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin3_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin3_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin3_pmh_avx2;
        break;
      case 4:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin4_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin4_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin4_pmh_avx2;
        break;
      case 5:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin5_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin5_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin5_pmh_avx2;
        break;
      case 6:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin6_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin6_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin6_pmh_avx2;
        break;
      case 7:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin7_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin7_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin7_pmh_avx2;
        break;
      case 8:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin8_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin8_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin8_pmh_avx2;
        break;
      case 16:
        if (pat_->min_ == 1)
          adv_ = &Matcher::simd_advance_pattern_pin16_one_avx2;
        else if (pat_->min_ < 4)
          adv_ = &Matcher::simd_advance_pattern_pin16_pma_avx2;
        else
          adv_ = &Matcher::simd_advance_pattern_pin16_pmh_avx2;
        break;
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
    else if (pat_->min_ < 4)
      adv_ = &Matcher::simd_advance_chars_pma_avx2<2>;
    else
      adv_ = &Matcher::simd_advance_chars_pmh_avx2<2>;
  }
  else if (pat_->len_ == 3)
  {
    if (pat_->min_ == 0)
      adv_ = &Matcher::simd_advance_chars_avx2<3>;
    else if (pat_->min_ < 4)
      adv_ = &Matcher::simd_advance_chars_pma_avx2<3>;
    else
      adv_ = &Matcher::simd_advance_chars_pmh_avx2<3>;
  }
  else if (pat_->bmd_ == 0)
  {
#if defined(WITH_STRING_PM)
    if (pat_->min_ >= 4)
      adv_ = &Matcher::simd_advance_string_pmh_avx2;
    else if (pat_->min_ > 0)
      adv_ = &Matcher::simd_advance_string_pma_avx2;
    else
#endif
      adv_ = &Matcher::simd_advance_string_avx2;
  }
}

// My "needle search" method when pin=1
bool Matcher::simd_advance_pattern_pin1_pma_avx2(size_t loc)
{
  const Pattern::Pred *pma = pat_->pma_;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
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
        if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0)
          return true;
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
  return advance_pattern_pin1_pma(loc);
}

// My "needle search" method when pin=1
bool Matcher::simd_advance_pattern_pin1_pmh_avx2(size_t loc)
{
  const Pattern::Pred *pmh = pat_->pmh_;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
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
        if (Pattern::predict_match(pmh, &buf_[loc], min))
          return true;
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
  return advance_pattern_pin1_pmh(loc);
}

// My "needle search" methods
#define ADV_PAT_PIN_ONE(N, INIT, COMP) \
bool Matcher::simd_advance_pattern_pin##N##_one_avx2(size_t loc) \
{ \
  const Pattern::Pred *pma = pat_->pma_; \
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
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s + offset - buf_; \
        if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 32; \
    } \
    loc = s - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + 1 > end_) \
      return false; \
    if (loc + 32 > end_) \
      break; \
  } \
  return advance_pattern(loc); \
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

// My "needle search" methods
#define ADV_PAT_PIN(N, INIT, COMP) \
bool Matcher::simd_advance_pattern_pin##N##_pma_avx2(size_t loc) \
{ \
  const Pattern::Pred *pma = pat_->pma_; \
  const char *chr = pat_->chr_; \
  size_t min = pat_->min_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
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
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s - lcp + offset - buf_; \
        if (loc + 4 > end_ || Pattern::predict_match(pma, &buf_[loc]) == 0) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 32; \
    } \
    s -= lcp; \
    loc = s - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + min > end_) \
      return false; \
    if (loc + min + 31 > end_) \
      break; \
  } \
  return advance_pattern(loc); \
} \
\
bool Matcher::simd_advance_pattern_pin##N##_pmh_avx2(size_t loc) \
{ \
  const Pattern::Pred *pmh = pat_->pmh_; \
  const char *chr = pat_->chr_; \
  size_t min = pat_->min_; \
  uint16_t lcp = pat_->lcp_; \
  uint16_t lcs = pat_->lcs_; \
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
      while (mask != 0) \
      { \
        uint32_t offset = ctz(mask); \
        loc = s - lcp + offset - buf_; \
        if (Pattern::predict_match(pmh, &buf_[loc], min)) \
        { \
          set_current(loc); \
          return true; \
        } \
        mask &= mask - 1; \
      } \
      s += 32; \
    } \
    s -= lcp; \
    loc = s - buf_; \
    set_current_and_peek_more(loc - 1); \
    loc = cur_ + 1; \
    if (loc + min > end_) \
      return false; \
    if (loc + min + 31 > end_) \
      break; \
  } \
  return advance_pattern_min4(loc); \
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

/// Few chars
template<uint8_t LEN>
bool Matcher::simd_advance_chars_avx2(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
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
      while (mask != 0)
      {
        uint32_t offset = ctz(mask);
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          loc = s - lcp + offset - buf_;
          set_current(loc);
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
    if (loc + LEN > end_)
      return false;
    if (loc + LEN + 31 > end_)
      break;
  }
  return advance_chars<LEN>(loc);
}

/// Few chars followed by 2 to 3 minimal char pattern
template<uint8_t LEN>
bool Matcher::simd_advance_chars_pma_avx2(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const Pattern::Pred *pma = pat_->pma_;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
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
      while (mask != 0)
      {
        uint32_t offset = ctz(mask);
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          loc = s - lcp + offset - buf_;
          if (loc + LEN + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + LEN]) == 0)
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 32;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 31 > end_)
      break;
  }
  return advance_chars_pma<LEN>(loc);
}

/// Few chars followed by 4 minimal char pattern
template<uint8_t LEN>
bool Matcher::simd_advance_chars_pmh_avx2(size_t loc)
{
  static const uint16_t lcp = 0;
  static const uint16_t lcs = LEN - 1;
  const Pattern::Pred *pmh = pat_->pmh_;
  const char *chr = pat_->chr_;
  size_t min = pat_->min_;
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
      while (mask != 0)
      {
        uint32_t offset = ctz(mask);
        if (LEN == 2 ||
            (LEN == 3 ? s[offset + 1 - lcp] == chr[1] : std::memcmp(s + 1 - lcp + offset, chr + 1, LEN - 2) == 0))
        {
          loc = s - lcp + offset - buf_;
          set_current(loc);
          if (loc + LEN + min > end_ || Pattern::predict_match(pmh, &buf_[loc + LEN], min))
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
    if (loc + LEN + min > end_)
      return false;
    if (loc + LEN + min + 31 > end_)
      break;
  }
  return advance_chars_pmh<LEN>(loc);
}

/// Implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_avx2(size_t loc)
{
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
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
      s += 32;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len > end_)
      return false;
    if (loc + len + 31 > end_)
      break;
  }
  return advance_string(loc);
}

#if defined(WITH_STRING_PM)

/// Implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_pma_avx2(size_t loc)
{
  const Pattern::Pred *pma = pat_->pma_;
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
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
      while (mask != 0)
      {
        uint32_t offset = ctz(mask);
        if (std::memcmp(s - lcp + offset, chr, len) == 0)
        {
          loc = s - lcp + offset - buf_;
          if (loc + len + 4 > end_ || Pattern::predict_match(pma, &buf_[loc + len]) == 0)
          {
            set_current(loc);
            return true;
          }
        }
        mask &= mask - 1;
      }
      s += 32;
    }
    s -= lcp;
    loc = s - buf_;
    set_current_and_peek_more(loc - 1);
    loc = cur_ + 1;
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 31 > end_)
      break;
  }
  return advance_string_pma(loc);
}

/// Implements AVX2 string search scheme based on http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_string_pmh_avx2(size_t loc)
{
  const Pattern::Pred *pmh = pat_->pmh_;
  const char *chr = pat_->chr_;
  size_t len = pat_->len_;
  size_t min = pat_->min_;
  uint16_t lcp = pat_->lcp_;
  uint16_t lcs = pat_->lcs_;
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
      while (mask != 0)
      {
        uint32_t offset = ctz(mask);
        if (std::memcmp(s - lcp + offset, chr, len) == 0)
        {
          loc = s - lcp + offset - buf_;
          set_current(loc);
          if (loc + len + min > end_ || Pattern::predict_match(pmh, &buf_[loc + len], min))
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
    if (loc + len + min > end_)
      return false;
    if (loc + len + min + 31 > end_)
      break;
  }
  return advance_string_pmh(loc);
}

#endif // WITH_STRING_PM

#else

// appease ranlib "has no symbols"
void matcher_not_compiled_with_avx2() { }

#endif

} // namespace reflex
