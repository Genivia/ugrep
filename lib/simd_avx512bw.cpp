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
@file      simd_avx512bw.cpp
@brief     RE/flex SIMD intrinsics compiled with -mavx512bw
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2021, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/matcher.h>

namespace reflex {

// Partially count newlines in string b up to and including position e in b, updates b close to e with uncounted part
size_t simd_nlcount_avx512bw(const char*& b, const char *e)
{
  const char *s = b;
  size_t n = 0;
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
  __m512i vlcn = _mm512_set1_epi8('\n');
  while (s + 63 <= e)
  {
    __m512i vlcm = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s));
    uint64_t mask = _mm512_cmpeq_epi8_mask(vlcm, vlcn);
    n += popcountl(mask);
    s += 64;
  }
#else
  (void)e;
#endif
  b = s;
  return n;
}

// string search scheme based on in http://0x80.pl/articles/simd-friendly-karp-rabin.html
bool Matcher::simd_advance_avx512bw(const char*& b, const char *e, size_t &loc, size_t min, const char *pre, size_t len)
{
  const char *s = b;
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
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
#else
  (void)e, (void)loc, (void)min, (void)pre, (void)len;
#endif
  b = s;
  return false;
}

} // namespace reflex
