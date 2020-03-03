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
@file      debug.cpp
@brief     Debug logs
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

See `debug.h` for details.
*/

#include <stdio.h>
#include <cstring>

FILE *REFLEX_DBGFD_ = NULL;

extern "C" {

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__)

#include <windows.h>
void REFLEX_DBGOUT_(const char *log, const char *file, int line)
{
  SYSTEMTIME tm;
  if (REFLEX_DBGFD_ == NULL && (log[0] == '.' || ::fopen_s(&REFLEX_DBGFD_, log, "a")))
    REFLEX_DBGFD_ = stderr;
  GetLocalTime(&tm);
  ::fprintf(REFLEX_DBGFD_, "\n%02d%02d%02d/%02d%02d%02d.%03d%14.14s:%-5d", tm.wYear%100, tm.wMonth, tm.wDay, tm.wHour, tm.wMinute, tm.wSecond, tm.wMilliseconds, file, line);
}

#else

#include <time.h>
#include <sys/time.h>
void REFLEX_DBGOUT_(const char *log, const char *file, int line)
{
  struct timeval tv;
  struct tm tm;
  const char *name = std::strrchr(file, '/');
  if (name)
    name++;
  else
    name = file;
  if (REFLEX_DBGFD_ == NULL && (log[0] == '.' || (REFLEX_DBGFD_ = ::fopen(log, "a")) == NULL))
    REFLEX_DBGFD_ = stderr;
  gettimeofday(&tv, NULL);
  localtime_r(&tv.tv_sec, &tm);
  ::fprintf(REFLEX_DBGFD_, "\n%02d%02d%02d/%02d%02d%02d.%06ld%14.14s:%-5d", tm.tm_year%100, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long>(tv.tv_usec), name, line);
}

#endif

}
