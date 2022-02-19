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
    "cannot save tables file",
  };
  return regex_error_message(messages[code], pattern, pos);
}

std::string regex_error::regex_error_message(const char *message, const char *pattern, size_t pos)
{
  size_t l = strlen(pattern);
  if (pos > l)
    pos = l;
  l = strlen(message);
  size_t n = pos / 40;
  size_t k = pos % 40 + (n == 0 ? 0 : 20);
  const char *p = n == 0 ? pattern : pattern + 40 * n - 20;
  while (p > pattern && (*p & 0xc0) == 0x80)
  {
    --p;
    ++k;
  }
  size_t m = disppos(p, 79) - p;
  size_t r = displen(p, k);
  std::string what("error at position ");
  what.append(ztoa(pos)).append("\n").append(p, m).append("\n");
  if (r >= l + 4)
    what.append(r - l - 4, ' ').append(message).append("___/\n");
  else
    what.append(r, ' ').append("\\___").append(message).append("\n");
  return what;
}

size_t regex_error::displen(const char *s, size_t k)
{
  size_t n = 0;
  while (k > 0 && *s != '\0')
  {
    unsigned char c = *s++;
    if (c >= 0x80)
    {
      if (c >= 0xf0 &&
          (c > 0xf0 ||
           (static_cast<unsigned char>(s[0]) >= 0x9f &&
            (static_cast<unsigned char>(s[0]) > 0x9f ||
             (static_cast<unsigned char>(s[1]) >= 0x86 &&
              (static_cast<unsigned char>(s[1]) > 0x86 ||
               static_cast<unsigned char>(s[2]) >= 0x8e))))))
      {
        // U+1F18E (UTF-8 F0 9F 86 8E) and higher is usually double width
        ++n;
        if (k < 4)
          break;
        s += (s[0] != '\0') + (s[1] != '\0') + (s[2] != 0);
        k -= 3;
      }
      else
      {
        while (k > 1 && (*s & 0xc0) == 0x80)
        {
          ++s;
          --k;
        }
      }
    }
    ++n;
    --k;
  }
  return n;
}

const char *regex_error::disppos(const char *s, size_t k)
{
  while (k > 0 && *s != '\0')
  {
    unsigned char c = *s++;
    if (c >= 0x80)
    {
      if (c >= 0xf0 &&
          (c > 0xf0 ||
           (static_cast<unsigned char>(s[0]) >= 0x9f &&
            (static_cast<unsigned char>(s[0]) > 0x9f ||
             (static_cast<unsigned char>(s[1]) >= 0x86 &&
              (static_cast<unsigned char>(s[1]) > 0x86 ||
               static_cast<unsigned char>(s[2]) >= 0x8e))))))
      {
        // U+1F18E (UTF-8 F0 9F 86 8E) and higher is usually double width
        if (k < 4)
          break;
        s += (s[0] != '\0') + (s[1] != '\0') + (s[2] != 0);
        k -= 3;
      }
      else
      {
        while (k > 1 && (*s & 0xc0) == 0x80)
        {
          ++s;
          --k;
        }
      }
    }
    --k;
  }
  return s;
}

} // namespace reflex
