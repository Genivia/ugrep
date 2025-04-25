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
      __m128i v0 = _mm_setzero_si128();
      while (s <= e)
      {
        __m128i vlcm1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vlcm2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
        __m128i vlcm3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 32));
        __m128i vlcm4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 48));
        // take absolute value of comparisons to get 0 or 1 per byte
        __m128i vlceq1 = _mm_sub_epi8(v0, _mm_cmpeq_epi8(vlcm1, vlcn));
        __m128i vlceq2 = _mm_sub_epi8(v0, _mm_cmpeq_epi8(vlcm2, vlcn));
        __m128i vlceq3 = _mm_sub_epi8(v0, _mm_cmpeq_epi8(vlcm3, vlcn));
        __m128i vlceq4 = _mm_sub_epi8(v0, _mm_cmpeq_epi8(vlcm4, vlcn));
        // sum all up (we have a limited range 0..4 to sum to a total max 4x16=64 < 256)
        // more than two times faster than four popcounts over four movemasks for SSE2 (not for AVX2)
        __m128i vsum = _mm_add_epi8(_mm_add_epi8(vlceq1, vlceq2), _mm_add_epi8(vlceq3, vlceq4));
        uint16_t sum =
          _mm_extract_epi16(vsum, 0) +
          _mm_extract_epi16(vsum, 1) +
          _mm_extract_epi16(vsum, 2) +
          _mm_extract_epi16(vsum, 3) +
          _mm_extract_epi16(vsum, 4) +
          _mm_extract_epi16(vsum, 5) +
          _mm_extract_epi16(vsum, 6) +
          _mm_extract_epi16(vsum, 7);
        n += static_cast<uint8_t>(sum) + (sum >> 8);
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
      // my horizontal sum method (we have a limited range 0..4 to sum to a total max 4x16=64 < 256)
      uint64x2_t vsum = vreinterpretq_u64_s8(vqabsq_s8(vreinterpretq_s8_u8(vaddq_u8(vaddq_u8(vleq0, vleq1), vaddq_u8(vleq2, vleq3)))));
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

// Check if valid UTF-8 encoding and does not include a NUL, but pass surrogates and 3/4 byte overlongs
bool isutf8(const char *s, const char *e)
{
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)

  if (s <= e - 16)
  {
#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2)
    if (s <= e - 32 && have_HW_AVX2())
    {
      if (!simd_isutf8_avx2(s, e))
        return false;
    }
    else
#endif
    {
      // prep step: scan ASCII w/o NUL first for speed, then check remaining UTF-8
      const __m128i v0 = _mm_setzero_si128();
      while (s <= e - 16)
      {
        __m128i vc = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vm = _mm_cmpgt_epi8(vc, v0);
        if (_mm_movemask_epi8(vm) != 0xffff)
        {
          // non-ASCII, return false if a NUL was found
          vm = _mm_cmpeq_epi8(vc, v0);
          if (_mm_movemask_epi8(vm) != 0x0000)
            return false;
          break;
        }
        s += 16;
      }
      // my UTF-8 check method
      // 204ms to check 1,000,000,000 bytes on a Intel quad core i7 2.9 GHz 16GB 2133 MHz LPDDR3
      //
      // scalar code:
      //   int8_t p = 0, q = 0, r = 0;
      //   while (s < e)
      //   {
      //     int8_t c = static_cast<int8_t>(*s++);
      //     if (!(c > 0 || c < -64 || (c > -63 && c < -11)))
      //       return false;
      //     if (((-(c > -63) ^ (p | q | r)) & 0x80) != 0x80)
      //       return false;
      //     r = (q & (q << 1));
      //     q = (p & (p << 1));
      //     p = (c & (c << 1));
      //   }
      //   return (p | q | r) & 0x80;
      //
      const __m128i vxc0 = _mm_set1_epi8(0xc0);
      const __m128i vxc1 = _mm_set1_epi8(0xc1);
      const __m128i vxf5 = _mm_set1_epi8(0xf5);
      __m128i vp = v0;
      __m128i vq = v0;
      __m128i vr = v0;
      while (s <= e - 16)
      {
        // step 1: check valid signed byte ranges, including continuation bytes 0x80 to 0xbf
        //   c = s[i]
        //   if (!(c > 0 || c < -64 || (c > -63 && c < -11)))
        //     return false
        //
        __m128i vc = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        __m128i vt = _mm_and_si128(_mm_cmpgt_epi8(vc, vxc1), _mm_cmplt_epi8(vc, vxf5));
        vt = _mm_or_si128(vt, _mm_cmplt_epi8(vc, vxc0));
        vt = _mm_or_si128(vt, _mm_cmpgt_epi8(vc, v0));
        __m128i vm = vt;
        //
        //   step 2: check UTF-8 multi-byte sequences of 2, 3 and 4 bytes long
        //     if (((-(c > -63) ^ (p | q | r)) & 0x80) != 0x80)
        //       return false
        //     r = (q & (q << 1))
        //     q = (p & (p << 1))
        //     p = (c & (c << 1))
        //
        //   possible values of c after step 1 and subsequent values of p, q, r:
        //     c at 1st byte   p at 2nd byte   q at 3rd byte   r at 4th byte
        //       0xxxxxxx        0xxxxxxx        0xxxxxxx        0xxxxxxx
        //       10xxxxxx        00xxxxxx        00xxxxxx        00xxxxxx
        //       110xxxxx        100xxxxx        000xxxxx        000xxxxx
        //       1110xxxx        1100xxxx        1000xxxx        0000xxxx
        //       11110xxx        11100xxx        11000xxx        10000xxx
        //
        //   byte vectors vc, vp, vq, vr and previous values:
        //                             | c | c | c | c | ... | c |
        //                     | old p | p | p | p | p | ... | p |
        //             | old q | old q | q | q | q | q | ... | q |
        //     | old r | old r | old r | r | r | r | r | ... | r |
        //
        //   shift vectors vp, vq, vr to align to compute bitwise-or vp | vq | vr -> vt:
        //                 |     c |     c |     c | c | ... | c | = vc
        //                 | old p |     p |     p | p | ... | p |
        //                 | old q | old q |     q | q | ... | q |
        //                 | old r | old r | old r | r | ... | r |
        //                   -----   -----   -----   -   ---   -   or
        //                 |     t |     t |     t | t | ... | t | = vt
        //
        //   SSE2 code to perform r = (q & (q << 1)); q = (p & (p << 1)); p = (c & (c << 1));
        //   shift parts of the old vp, vq, vr and new vp, vq, vr in vt using psrldq and por
        //   then check if ((-(c > -63) ^ (p | q | r))) bit 7 is 1 in a combined test with step 1
        //
        vt = _mm_bsrli_si128(vp, 15);
        vp = _mm_and_si128(vc, _mm_add_epi8(vc, vc));
        vt = _mm_or_si128(vt, _mm_bsrli_si128(vq, 14));
        vq = _mm_and_si128(vp, _mm_add_epi8(vp, vp));
        vt = _mm_or_si128(vt, _mm_bsrli_si128(vr, 13));
        vr = _mm_and_si128(vq, _mm_add_epi8(vq, vq));
        vt = _mm_or_si128(vt, _mm_bslli_si128(vp, 1));
        vt = _mm_or_si128(vt, _mm_bslli_si128(vq, 2));
        vt = _mm_or_si128(vt, _mm_bslli_si128(vr, 3));
        vt = _mm_xor_si128(vt, _mm_cmpgt_epi8(vc, vxc1));
        vm = _mm_and_si128(vm, vt);
        if (_mm_movemask_epi8(vm) != 0xffff)
          return false;
        s += 16;
      }
      // do not end in the middle of a UTF-8 multibyte sequence, backtrack when necessary (this will terminate)
      while ((*--s & 0xc0) == 0x80)
        continue;
    }
  }

#elif defined(HAVE_NEON)

  if (s <= e - 16)
  {
    // prep step: scan ASCII first for speed, then check remaining UTF-8
    const int8x16_t v0 = vdupq_n_s8(0);
    while (s <= e - 16)
    {
      int8x16_t vc = vld1q_s8(reinterpret_cast<const int8_t*>(s));
      int64x2_t vm = vreinterpretq_s64_u8(vcgtq_s8(vc, v0));
      if ((vgetq_lane_s64(vm, 0) & vgetq_lane_s64(vm, 1)) != -1LL)
      {
        // non-ASCII, return false if a NUL was found
        vm = vreinterpretq_s64_u8(vceqq_s8(vc, v0));
        if ((vgetq_lane_s64(vm, 0) | vgetq_lane_s64(vm, 1)) != 0LL)
          return false;
        break;
      }
      s += 16;
    }
    // my UTF-8 check method
    // 116ms to check 1,000,000,000 bytes on Apple M1 Pro (AArch64)
    //
    // scalar code:
    //   int8_t p = 0, q = 0, r = 0;
    //   while (s < e)
    //   {
    //     int8_t c = static_cast<int8_t>(*s++);
    //     if (!(c > 0 || c < -64 || (c > -63 && c < -11)))
    //       return false;
    //     if (((-(c > -63) ^ (p | q | r)) & 0x80) != 0x80)
    //       return false;
    //     r = (q & (q << 1));
    //     q = (p & (p << 1));
    //     p = (c & (c << 1));
    //   }
    //   return (p | q | r) & 0x80;
    //
    const int8x16_t vxc0 = vdupq_n_s8(0xc0);
    const int8x16_t vxc1 = vdupq_n_s8(0xc1);
    const int8x16_t vxf5 = vdupq_n_s8(0xf5);
    int8x16_t vp = v0;
    int8x16_t vq = v0;
    int8x16_t vr = v0;
    while (s <= e - 16)
    {
      // step 1: check valid signed byte ranges, including continuation bytes 0x80 to 0xbf
      //   c = s[i]
      //   if (!(c > 0 || c < -64 || (c > -63 && c < -11)))
      //     return false
      //
      int8x16_t vc = vld1q_s8(reinterpret_cast<const int8_t*>(s));
      int8x16_t vt = vandq_s8(vreinterpretq_s8_u8(vcgtq_s8(vc, vxc1)), vreinterpretq_s8_u8(vcltq_s8(vc, vxf5)));
      vt = vorrq_s8(vt, vreinterpretq_s8_u8(vcltq_s8(vc, vxc0)));
      vt = vorrq_s8(vt, vreinterpretq_s8_u8(vcgtq_s8(vc, v0)));
      int64x2_t vm = vreinterpretq_s64_s8(vt);
      //
      //   step 2: check UTF-8 multi-byte sequences of 2, 3 and 4 bytes long
      //     if (((-(c > -63) ^ (p | q | r)) & 0x80) != 0x80)
      //       return false
      //     r = (q & (q << 1))
      //     q = (p & (p << 1))
      //     p = (c & (c << 1))
      //
      //   possible values of c after step 1 and subsequent values of p, q, r:
      //     c at 1st byte   p at 2nd byte   q at 3rd byte   r at 4th byte
      //       0xxxxxxx        0xxxxxxx        0xxxxxxx        0xxxxxxx
      //       10xxxxxx        00xxxxxx        00xxxxxx        00xxxxxx
      //       110xxxxx        100xxxxx        000xxxxx        000xxxxx
      //       1110xxxx        1100xxxx        1000xxxx        0000xxxx
      //       11110xxx        11100xxx        11000xxx        10000xxx
      //
      //   byte vectors vc, vp, vq, vr and previous values:
      //                             | c | c | c | c | ... | c | = vc
      //                     | old p | p | p | p | p | ... | p | = vp
      //             | old q | old q | q | q | q | q | ... | q | = vq
      //     | old r | old r | old r | r | r | r | r | ... | r | = vr
      //
      //   shift vectors vp, vq, vr to align to compute bitwise-or vp | vq | vr -> vt:
      //                 |     c |     c |     c | c | ... | c | = vc
      //                 | old p |     p |     p | p | ... | p |
      //                 | old q | old q |     q | q | ... | q |
      //                 | old r | old r | old r | r | ... | r |
      //                   -----   -----   -----   -   ---   -   or
      //                 |     t |     t |     t | t | ... | t | = vt
      //
      //   optimized code to perform r = (q & (q << 1)); q = (p & (p << 1)); p = (c & (c << 1));
      //   shift parts of the old vp, vq, vr and new vp, vq, vr in vt using EXT
      //   then check if ((-(c > -63) ^ (p | q | r))) bit 7 is 1 in a combined test with step 1
      //
      int8x16_t vo = vp;
      vp = vandq_s8(vc, vshlq_n_s8(vc, 1));
      vt = vextq_s8(vo, vp, 15);
      vo = vq;
      vq = vandq_s8(vp, vshlq_n_s8(vp, 1));
      vt = vorrq_s8(vt, vextq_s8(vo, vq, 14));
      vo = vr;
      vr = vandq_s8(vq, vshlq_n_s8(vq, 1));
      vt = vorrq_s8(vt, vextq_s8(vo, vr, 13));
      vt = veorq_s8(vt, vreinterpretq_s8_u8(vcgtq_s8(vc, vxc1)));
      vm = vandq_s64(vm, vreinterpretq_s64_s8(vt));
      if (((vgetq_lane_s64(vm, 0) & vgetq_lane_s64(vm, 1)) & 0x8080808080808080LL) != 0x8080808080808080LL)
        return false;
      s += 16;
    }
    // do not end in the middle of a UTF-8 multibyte sequence, backtrack when necessary (this will terminate)
    while ((*--s & 0xc0) == 0x80)
      continue;
  }

#endif

  while (s < e)
  {
    int8_t c = 0;
    while (s < e && (c = static_cast<int8_t>(*s)) > 0)
      ++s;
    if (s++ >= e)
      break;
    // U+0080 ~ U+07ff <-> c2 80 ~ df bf (disallow 2 byte overlongs)
    if (c < -62 || c > -12 || s >= e || (*s++ & 0xc0) != 0x80)
      return false;
    // U+0800 ~ U+ffff <-> e0 a0 80 ~ ef bf bf (quick but allows surrogates and 3 byte overlongs)
    if (c >= -32 && (s >= e || (*s++ & 0xc0) != 0x80))
      return false;
    // U+010000 ~ U+10ffff <-> f0 90 80 80 ~ f4 8f bf bf (quick but allows 4 byte overlongs)
    if (c >= -16 && (s >= e || (*s++ & 0xc0) != 0x80))
      return false;
  }
  return true;
}

} // namespace reflex
