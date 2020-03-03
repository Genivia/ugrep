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
@file      timer.h
@brief     Measure elapsed wall-clock time in milliseconds
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_TIMER_H
#define REFLEX_TIMER_H

#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#include <windows.h>

namespace reflex {

typedef SYSTEMTIME timer_type;

/// Start timer.
inline void timer_start(timer_type& t) ///< timer to be initialized
{
  GetLocalTime(&t);
}

/// Return elapsed time in milliseconds (ms) since the last call up to 1 minute (wraps if elapsed time exceeds 1 minute!)
inline float timer_elapsed(timer_type& t) ///< timer to be updated
{
  timer_type now;
  GetLocalTime(&now);
  float sec = now.wMilliseconds;
  sec -= t.wMilliseconds;
  t.wMilliseconds = now.wMilliseconds;
  sec += 1000.0f * (now.wSecond - t.wSecond);
  t.wSecond = now.wSecond;
  if (sec < 0.0)
    sec += 60000.0;
  return sec;
}

} // namespace reflex

#else

#include <cstddef>
#include <sys/time.h>

namespace reflex {

typedef timeval timer_type;

/// Start timer.
inline void timer_start(timer_type& t) ///< timer to be initialized
{
  gettimeofday(&t, NULL);
}

/// Return elapsed time in milliseconds (ms) with microsecond precision since the last call up to 1 minute (wraps if elapsed time exceeds 1 minute!)
inline float timer_elapsed(timer_type& t) ///< timer to be updated
{
  timer_type now;
  gettimeofday(&now, NULL);
  float sec = now.tv_usec;
  sec -= t.tv_usec;
  t.tv_usec = now.tv_usec;
  sec = 1000.0 * (now.tv_sec - t.tv_sec) + sec/1000.0;
  t.tv_sec = now.tv_sec;
  if (sec < 0.0)
    sec += 60000.0;
  return sec;
}

} // namespace reflex

#endif

#endif
