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
@file      simd.cpp
@brief     RE/flex SIMD primitives
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/simd.h>

namespace reflex {

#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)

// simd.h get_HW()
static uint64_t get_HW()
{
  int CPUInfo1[4] = { 0, 0, 0, 0 };
  int CPUInfo7[4] = { 0, 0, 0, 0 };
  cpuidex(CPUInfo1, 0, 0);
  int n = CPUInfo1[0];
  if (n <= 0)
    return 0ULL;
  cpuidex(CPUInfo1, 1, 0); // cpuid EAX=1
  if (n >= 7)
    cpuidex(CPUInfo7, 7, 0); // cpuid EAX=7, ECX=0
  return static_cast<uint32_t>(CPUInfo1[2]) | (static_cast<uint64_t>(static_cast<uint32_t>(CPUInfo7[1])) << 32);
}

uint64_t HW = get_HW();

#endif

size_t nlcount(const char *s, const char *t)
{
  size_t n = 0;
  if (s <= t - 256)
  {
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
    if (have_HW_AVX512BW())
      n = simd_nlcount_avx512bw(s, t);
    else if (have_HW_AVX2())
      n = simd_nlcount_avx2(s, t);
    else
#elif defined(HAVE_AVX512BW) || defined(HAVE_AVX2)
    if (have_HW_AVX2())
      n = simd_nlcount_avx2(s, t);
    else
#endif
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)
    {
      const char *e = t - 64;
      // align on 16 bytes
      while ((reinterpret_cast<std::ptrdiff_t>(s) & 0x0f) != 0)
        n += (*s++ == '\n');
      __m128i vlcn = _mm_set1_epi8('\n');
      while (s <= e)
      {
        __m128i vlcm1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vlcm2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
        __m128i vlcm3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32));
        __m128i vlcm4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 48));
        __m128i vlceq1 = _mm_cmpeq_epi8(vlcm1, vlcn);
        __m128i vlceq2 = _mm_cmpeq_epi8(vlcm2, vlcn);
        __m128i vlceq3 = _mm_cmpeq_epi8(vlcm3, vlcn);
        __m128i vlceq4 = _mm_cmpeq_epi8(vlcm4, vlcn);
        n += popcount(_mm_movemask_epi8(vlceq1))
          +  popcount(_mm_movemask_epi8(vlceq2))
          +  popcount(_mm_movemask_epi8(vlceq3))
          +  popcount(_mm_movemask_epi8(vlceq4));
        s += 64;
      }
    }
#elif defined(HAVE_NEON)
    const char *e = t - 64;
    uint8x16_t vlcn = vdupq_n_u8('\n');
    while (s <= e)
    {
      uint8x16_t vlcm0 = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
      uint8x16_t vleq0 = vceqq_u8(vlcm0, vlcn);
      s += 16;
      uint8x16_t vlcm1 = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
      uint8x16_t vleq1 = vceqq_u8(vlcm1, vlcn);
      s += 16;
      uint8x16_t vlcm2 = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
      uint8x16_t vleq2 = vceqq_u8(vlcm2, vlcn);
      s += 16;
      uint8x16_t vlcm3 = vld1q_u8(reinterpret_cast<const uint8_t*>(s));
      uint8x16_t vleq3 = vceqq_u8(vlcm3, vlcn);
      s += 16;
#if defined(__aarch64__)
      n += vaddvq_s8(vqabsq_s8(vreinterpretq_s8_u8(vaddq_u8(vleq0, vaddq_u8(vleq1, vaddq_u8(vleq2, vleq3))))));
#else
      // my homebrew horizontal sum (we have a very limited range 0..4 to sum to a total max 4x16=64 < 256)
      uint64x2_t vsum = vreinterpretq_u64_s8(vqabsq_s8(vreinterpretq_s8_u8(vaddq_u8(vleq0, vaddq_u8(vleq1, vaddq_u8(vleq2, vleq3))))));
      uint64_t sum0 = vgetq_lane_u64(vsum, 0) + vgetq_lane_u64(vsum, 1);
      uint32_t sum1 = static_cast<uint32_t>(sum0) + (sum0 >> 32);
      uint16_t sum2 = static_cast<uint16_t>(sum1) + (sum1 >> 16);
      n += static_cast<uint8_t>(sum2) + (sum2 >> 8);
#endif
    }
#endif
  }
  // 4-way auto-vectorizable loop
  uint32_t n0 = 0, n1 = 0, n2 = 0, n3 = 0;
  while (s < t - 3)
  {
    n0 += s[0] == '\n';
    n1 += s[1] == '\n';
    n2 += s[2] == '\n';
    n3 += s[3] == '\n';
    s += 4;
  }
  n += n0 + n1 + n2 + n3;
  // epilogue
  if (s < t)
  {
    n += *s == '\n';
    if (++s < t)
    {
      n += *s == '\n';
      if (++s < t)
        n += *s == '\n';
    }
  }
  return n;
}

} // namespace reflex
