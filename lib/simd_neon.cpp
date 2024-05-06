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
@file      simd_neon.cpp
@brief     RE/flex SIMD primitives
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/simd.h>

namespace reflex {

// Partially count newlines in string b up to e, updates b close to e with uncounted part
size_t simd_nlcount_neon(const char*& b, const char *e)
{
#if defined(HAVE_NEON)
  const char *s = b;
  e -= 64;
  if (s > e)
    return 0;
  size_t n = 0;
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
  b = s;
  return n;
#else
  (void)b;
  (void)e;
  return 0;
#endif
}

} // namespace reflex
