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
@file      simd_avx2.cpp
@brief     RE/flex SIMD primitives compiled with -mavx2 (and/or -msse2)
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#if defined(HAVE_AVX2) || defined(HAVE_AVX512BW)
# if !defined(__AVX2__) && !defined(__AVX512BW__)
#  error simd_avx2.cpp must be compiled with -mavx2 or /arch:avx2.
# endif
#endif

#include <reflex/simd.h>

namespace reflex {

// Partially count newlines in string b up to e, updates b close to e with uncounted part
size_t simd_nlcount_avx2(const char *& b, const char *e)
{
#if defined(HAVE_AVX2) || defined(HAVE_AVX512BW)
  const char *s = b;
  e -= 128;
  if (s > e)
    return 0;
  size_t n = 0;
  // align on 32 bytes
  while ((reinterpret_cast<std::ptrdiff_t>(s) & 0x1f) != 0)
    n += (*s++ == '\n');
  __m256i vlcn = _mm256_set1_epi8('\n');
  while (s <= e)
  {
    __m256i vlcm1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
    __m256i vlcm2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
    __m256i vlcm3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
    __m256i vlcm4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
    n += popcount(_mm256_movemask_epi8(_mm256_cmpeq_epi8(vlcm1, vlcn)))
      +  popcount(_mm256_movemask_epi8(_mm256_cmpeq_epi8(vlcm2, vlcn)))
      +  popcount(_mm256_movemask_epi8(_mm256_cmpeq_epi8(vlcm3, vlcn)))
      +  popcount(_mm256_movemask_epi8(_mm256_cmpeq_epi8(vlcm4, vlcn)));
    s += 128;
  }
  b = s;
  return n;
#else
  (void)b;
  (void)e;
  return 0;
#endif
}

// Partially check if valid UTF-8 encoding
bool simd_isutf8_avx2(const char *& b, const char *e)
{
#if defined(HAVE_AVX2) || defined(HAVE_AVX512BW)
  const char *s = b;
  // prep step: scan ASCII w/o NUL first for speed, then check remaining UTF-8
  const __m256i v00 = _mm256_setzero_si256();
  while (s <= e - 32)
  {
    __m256i vc = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
    __m256i vm = _mm256_cmpgt_epi8(vc, v00);
    if (_mm256_movemask_epi8(vm) != -1)
    {
      vm = _mm256_cmpeq_epi8(vc, v00);
      if (_mm256_movemask_epi8(vm) != 0)
        return false;
      break;
    }
    s += 32;
  }
  // my UTF-8 check method
  // 117ms to check 1,000,000,000 bytes on a Intel quad core i7 2.9 GHz 16GB 2133 MHz LPDDR3
  const __m256i vxc0 = _mm256_set1_epi8(0xc0);
  const __m256i vxc1 = _mm256_set1_epi8(0xc1);
  const __m256i vxf5 = _mm256_set1_epi8(0xf5);
  const __m256i v0 = _mm256_setzero_si256();
  __m256i vp = v0;
  __m256i vq = v0;
  __m256i vr = v0;
  while (s <= e - 32)
  {
    __m256i vc = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
    __m256i vt = _mm256_and_si256(_mm256_cmpgt_epi8(vc, vxc1), _mm256_cmpgt_epi8(vxf5, vc));
    vt = _mm256_or_si256(vt, _mm256_cmpgt_epi8(vxc0, vc));
    vt = _mm256_or_si256(vt, _mm256_cmpgt_epi8(vc, v0));
    __m256i vm = vt;
    __m256i vo = vp;
    vp = _mm256_and_si256(vc, _mm256_add_epi8(vc, vc));
    // vt = [vp,vo] >> 15*8 split in 128 bit lanes:
    // vthi = [vphi,vplo] >> 15*8
    // vtlo = [vplo,vohi] >> 15*8
    vt = _mm256_alignr_epi8(vp, _mm256_permute2x128_si256(vp, vo, 0x03), 15);
    vo = vq;
    vq = _mm256_and_si256(vp, _mm256_add_epi8(vp, vp));
    // vt = [vq,vo] >> 14*8 split in 128 bit lanes:
    // vthi |= [vqhi,vqlo] >> 14*8
    // vtlo |= [vqlo,vohi] >> 14*8
    vt = _mm256_or_si256(vt, _mm256_alignr_epi8(vq, _mm256_permute2x128_si256(vq, vo, 0x03), 14));
    vo = vr;
    vr = _mm256_and_si256(vq, _mm256_add_epi8(vq, vq));
    // vt = [vr,vo] >> 13*8 split in 128 bit lanes:
    // vthi |= [vrhi,vrlo] >> 13*8
    // vtlo |= [vrlo,vohi] >> 13*8
    vt = _mm256_or_si256(vt, _mm256_alignr_epi8(vr, _mm256_permute2x128_si256(vr, vo, 0x03), 13));
    vt = _mm256_xor_si256(vt, _mm256_cmpgt_epi8(vc, vxc1));
    vm = _mm256_and_si256(vm, vt);
    if (_mm256_movemask_epi8(vm) != -1)
      return false;
    s += 32;
  }
  while ((*--s & 0xc0) == 0x80)
    continue;
  b = s;
#else
  (void)b;
  (void)e;
#endif
  return true;
}

} // namespace reflex
