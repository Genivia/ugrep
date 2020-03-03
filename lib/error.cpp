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
@file      error.cpp
@brief     RE/flex regex errors
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/error.h>
#include <cstdio>
#include <cstring>

namespace reflex {

std::string regex_error::regex_error_message_code(regex_error_type code, const char *pattern, size_t pos)
{
  static const char *messages[] = {
    "mismatched ( )",
    "mismatched { }",
    "mismatched [ ]",
    "mismatched quotation",
    "empty expression",
    "empty character class",
    "invalid character class",
    "invalid character class range",
    "invalid escape",
    "invalid anchor or boundary",
    "invalid repeat",
    "invalid quantifier",
    "invalid modifier",
    "invalid collating element",
    "invalid backreference",
    "invalid syntax",
    "exceeds length limit",
    "exceeds complexity limits",
    "undefined name",
  };
  return regex_error_message(messages[code], pattern, pos);
}

std::string regex_error::regex_error_message(const char *message, const char *pattern, size_t pos)
{
  size_t l = std::strlen(message);
  size_t n = pos / 80;
  const char *p = pattern + 80 * n;
  while (p > pattern && (p[0] & 0xc0) == 0x80)
    --p;

  size_t m = std::strlen(p);
  if (m >= 80)
    m = 79;

  size_t r = pos % 80;
  for (size_t i = r; i > 0; --i)
    if ((p[i] & 0xc0) == 0x80)
      --r;

  std::string what("error in regex at position ");
  char buf[24];
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
  sprintf_s(buf, sizeof(buf), "%zu\n", pos);
#else
  std::sprintf(buf, "%zu\n", pos);
#endif
  what.append(buf).append(p, m).append("\n");
  if (r >= l + 4)
    what.append(r - l - 4, ' ').append(message).append("___/\n");
  else
    what.append(r, ' ').append("\\___").append(message).append("\n");

  return what;
}

} // namespace reflex
