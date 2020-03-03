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
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/matcher.h>

#if defined(HAVE_AVX512BW)
# include <immintrin.h>
#elif defined(HAVE_AVX)
# include <immintrin.h>
#elif defined(HAVE_SSE2)
# include <emmintrin.h>
#elif defined(HAVE_NEON)
# include <arm_neon.h>
#endif

#if defined(HAVE_AVX512BW) || defined(HAVE_AVX) || defined(HAVE_SSE2)
# ifdef _MSC_VER
#  include <intrin.h>
#  define cpuid __cpuid
# else
#  include <cpuid.h>
#  define cpuid(CPUInfo, InfoType) __cpuid(InfoType, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])
# endif
#endif

namespace reflex {

#if defined(HAVE_AVX512BW) || defined(HAVE_AVX) || defined(HAVE_SSE2)

#ifdef _MSC_VER
#ifdef _WIN64
#pragma intrinsic(_BitScanForward64)
inline uint32_t ctzl(uint64_t x)
{
  unsigned long r;
  _BitScanForward64(&r, x);
  return r;
}
#endif
#pragma intrinsic(_BitScanForward)
inline uint32_t ctz(uint32_t x)
{
  unsigned long r;
  _BitScanForward(&r, x);
  return r;
}
#else
inline uint32_t ctzl(uint64_t x)
{
  return __builtin_ctzl(x);
}
inline uint32_t ctz(uint32_t x)
{
  return __builtin_ctz(x);
}
#endif

uint64_t Matcher::get_HW()
{
  int CPUInfo1[4] = { -1, 0, 0, 0 };
  int CPUInfo7[4] = {  0, 0, 0, 0 };
  cpuid(CPUInfo1, 0);
  int n = CPUInfo1[0];
  if (n < 1)
    return 0ULL;
  cpuid(CPUInfo1, 1);
  if (n >= 7)
    cpuid(CPUInfo7, 7);
  return static_cast<uint64_t>(CPUInfo1[2]) | (static_cast<uint64_t>(CPUInfo7[1]) << 32);
}

#else

uint64_t Matcher::get_HW()
{
  return 0ULL;
}

#endif

uint64_t Matcher::HW = Matcher::get_HW();

/// Boyer-Moore preprocessing of the given pattern prefix pat of length len (<=255), generates bmd_ > 0 and bms_[] shifts.
void Matcher::boyer_moore_init(const char *pat, size_t len)
{
  // Relative frequency table of English letters, source code, and UTF-8 bytes
  static unsigned char freq[256] = "\0\0\0\0\0\0\0\0\0\73\4\0\0\4\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\73\70\70\1\1\2\2\70\70\70\2\2\70\70\70\2\3\3\3\3\3\3\3\3\3\3\70\70\70\70\70\70\2\35\14\24\26\37\20\17\30\33\11\12\25\22\32\34\15\7\27\31\36\23\13\21\10\16\6\70\1\70\2\70\1\67\46\56\60\72\52\51\62\65\43\44\57\54\64\66\47\41\61\63\71\55\45\53\42\50\40\70\2\70\2\0\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\47\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\45\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\44\0\0\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\5\46\56\56\56\56\56\56\56\56\56\56\56\56\46\56\56\73\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
  uint8_t n = static_cast<uint8_t>(len); // okay to cast: actually never more than 255
  uint16_t i;
  for (i = 0; i < 256; ++i)
    bms_[i] = n;
  lcp_ = 0;
  lcs_ = n > 1;
  for (i = 0; i < n; ++i)
  {
    uint8_t pch = static_cast<uint8_t>(pat[i]);
    bms_[pch] = static_cast<uint8_t>(n - i - 1);
    if (i > 0)
    {
      if (freq[static_cast<uint8_t>(pat[lcp_])] > freq[pch])
      {
        lcs_ = lcp_;
        lcp_ = i;
      }
      else if (freq[static_cast<uint8_t>(pat[lcs_])] > freq[pch])
      {
        lcs_ = i;
      }
    }
  }
  uint16_t j;
  for (i = n - 1, j = i; j > 0; --j)
    if (pat[j - 1] == pat[i])
      break;
  bmd_ = i - j + 1;
#if !defined(HAVE_NEON)
  size_t score = 0;
  for (i = 0; i < n; ++i)
    score += bms_[static_cast<uint8_t>(pat[i])];
  score /= n;
  uint8_t fch = freq[static_cast<uint8_t>(pat[lcp_])];
  if (!have_HW_SSE2() && !have_HW_AVX() && !have_HW_AVX512BW())
  {
    // if scoring is high and freq is high, then use improved Boyer-Moore instead of memchr()
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
#endif
}

// advance input cursor position after mismatch to align input for the next match
bool Matcher::advance()
{
  size_t loc = cur_ + 1;
  size_t min = pat_->min_;
  if (pat_->len_ == 0)
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
      peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
      {
        set_current(loc);
        return false;
      }
    }
  }
  if (bmd_ == 0)
    boyer_moore_init(pre, len);
  while (true)
  {
    if (lcs_ < len)
    {
      const char *s = buf_ + loc + lcp_;
      const char *e = buf_ + end_ + lcp_ - len + 1;
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
      if (have_HW_AVX512BW())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m512i vlcp = _mm512_set1_epi8(pre[lcp_]);
        __m512i vlcs = _mm512_set1_epi8(pre[lcs_]);
        while (s + 64 < e)
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
            mask &= ~(1ULL << offset);
          }
          s += 64;
        }
      }
      else if (have_HW_AVX())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m256i vlcp = _mm256_set1_epi8(pre[lcp_]);
        __m256i vlcs = _mm256_set1_epi8(pre[lcs_]);
        while (s + 32 < e)
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
            mask &= ~(1 << offset);
          }
          s += 32;
        }
      }
      else if (have_HW_SSE2())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m128i vlcp = _mm_set1_epi8(pre[lcp_]);
        __m128i vlcs = _mm_set1_epi8(pre[lcs_]);
        while (s + 16 < e)
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
            mask &= ~(1 << offset);
          }
          s += 16;
        }
      }
#elif defined(HAVE_AVX)
      if (have_HW_AVX())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m256i vlcp = _mm256_set1_epi8(pre[lcp_]);
        __m256i vlcs = _mm256_set1_epi8(pre[lcs_]);
        while (s + 32 < e)
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
            mask &= ~(1 << offset);
          }
          s += 32;
        }
      }
      else if (have_HW_SSE2())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m128i vlcp = _mm_set1_epi8(pre[lcp_]);
        __m128i vlcs = _mm_set1_epi8(pre[lcs_]);
        while (s + 16 < e)
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
            mask &= ~(1 << offset);
          }
          s += 16;
        }
      }
#elif defined(HAVE_SSE2)
      if (have_HW_SSE2())
      {
        // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
        __m128i vlcp = _mm_set1_epi8(pre[lcp_]);
        __m128i vlcs = _mm_set1_epi8(pre[lcs_]);
        while (s + 16 < e)
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
            mask &= ~(1 << offset);
          }
          s += 16;
        }
      }
#elif defined(HAVE_NEON)
      // implements SIMD string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
      uint8x16_t vlcp = vdupq_n_u8(pre[lcp_]);
      uint8x16_t vlcs = vdupq_n_u8(pre[lcs_]);
      while (s + 16 < e)
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
      peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
      {
        set_current(loc);
        return false;
      }
    }
    else
    {
      // implementation of our improved Boyer-Moore scheme
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
      peek_more();
      loc = cur_ + 1;
      if (loc + len > end_)
      {
        set_current(loc);
        return false;
      }
    }
  }
}

} // namespace reflex
