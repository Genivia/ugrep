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
@file      simd.h
@brief     RE/flex SIMD primitives
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef SIMD_H
#define SIMD_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(HAVE_AVX512BW)
# include <immintrin.h>
#elif defined(HAVE_AVX2)
# include <immintrin.h>
#elif defined(HAVE_SSE2)
# include <emmintrin.h>
#elif defined(HAVE_NEON)
# include <arm_neon.h>
# if defined(__ARM_ACLE)
#  include <arm_acle.h>
# endif
#endif

#if defined(HAVE_AVX512BW) || defined(HAVE_AVX2) || defined(HAVE_SSE2)

#ifdef _MSC_VER
# include <intrin.h>
#endif

#ifdef _MSC_VER
# define cpuidex __cpuidex
#else
# ifndef __cpuid_count
#  include <cpuid.h>
# endif
# define cpuidex(CPUInfo, id, subid) __cpuid_count(id, subid, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])
#endif

namespace reflex {

// HW id
extern uint64_t HW;

// do we have AVX512BW?
inline bool have_HW_AVX512BW()
{
  return HW & (1ULL << 62);
}

// do we have AVX2?
inline bool have_HW_AVX2()
{
  return HW & (1ULL << 37);
}

// do we have SSE2?
inline bool have_HW_SSE2()
{
  return HW & (1ULL << 26);
}

// support hyperthreading?
inline bool have_HW_HTT()
{
  return HW & (1ULL << 28);
}

#ifdef _MSC_VER
#pragma intrinsic(_BitScanForward)
inline uint32_t ctz(uint32_t x)
{
  unsigned long r;
  _BitScanForward(&r, x);
  return r;
}
inline uint32_t popcount(uint32_t x)
{
  return __popcnt(x);
}
#ifdef _WIN64
#pragma intrinsic(_BitScanForward64)
inline uint32_t ctzl(uint64_t x)
{
  unsigned long r;
  _BitScanForward64(&r, x);
  return r;
}
inline uint32_t popcountl(uint64_t x)
{
  return static_cast<uint32_t>(__popcnt64(x));
}
#endif
#else
inline uint32_t ctz(uint32_t x)
{
  return __builtin_ctz(x);
}
inline uint32_t ctzl(uint64_t x)
{
  return __builtin_ctzl(x);
}
inline uint32_t popcount(uint32_t x)
{
  return __builtin_popcount(x);
}
inline uint32_t popcountl(uint64_t x)
{
  return __builtin_popcountl(x);
}
#endif

// Partially count newlines in string b up to e, updates b close to e with uncounted part
extern size_t simd_nlcount_avx2(const char *& b, const char *e);
extern size_t simd_nlcount_avx512bw(const char *& b, const char *e);

// Partially check if valid UTF-8 encoding
extern bool simd_isutf8_avx2(const char *& b, const char *e);

} // namespace reflex

#elif defined(HAVE_NEON)

// If we have hardware clz (but it is rather slow)
#if defined(__ARM_ACLE) && defined(__ARM_FEATURE_CLZ)
inline uint32_t ctz(uint32_t x)
{
  return __clz(__rbit(x));
}
inline uint32_t ctzl(uint64_t x)
{
  return __clzl(__rbitl(x));
}
#endif

#endif

namespace reflex {

/// Count newlines in string s up to position e in the string
extern size_t nlcount(const char *s, const char *e);

/// Check if valid UTF-8 encoding and does not include a NUL, but accept surrogates and 3/4 byte overlongs
extern bool isutf8(const char *s, const char *e);

} // namespace reflex

#endif
