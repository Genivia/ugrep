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
@brief     RE/flex SIMD primitives compiled with -mavx512bw
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#if defined(HAVE_AVX512BW)
# if !defined(__AVX512BW__)
#  error simd_avx512bw.cpp must be compiled with -mavx512bw or /arch:avx512.
# endif
#endif

#include <reflex/simd.h>

namespace reflex {

// Partially count newlines in string b up to e, updates b close to e with uncounted part
size_t simd_nlcount_avx512bw(const char *& b, const char *e)
{
#if defined(HAVE_AVX512BW) && (!defined(_MSC_VER) || defined(_WIN64))
  const char *s = b;
  e -= 128;
  if (s > e)
    return 0;
  size_t n = 0;
  // align on 64 bytes
  while ((reinterpret_cast<std::ptrdiff_t>(s) & 0x3f) != 0)
    n += (*s++ == '\n');
  __m512i vlcn = _mm512_set1_epi8('\n');
  while (s <= e)
  {
    __m512i vlcm1 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s));
    __m512i vlcm2 = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(s + 64));
    n += popcountl(_mm512_cmpeq_epi8_mask(vlcm1, vlcn)) + popcountl(_mm512_cmpeq_epi8_mask(vlcm2, vlcn));
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

} // namespace reflex
