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
@file      utf8.h
@brief     RE/flex UCS to UTF-8 converters
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_UTF8_H
#define REFLEX_UTF8_H

#include <cstring>
#include <string>

#if defined(WITH_STANDARD_REPLACEMENT_CHARACTER)
/// Replace invalid UTF-8 with the standard replacement character U+FFFD.  This is not the default in RE/flex.
# define REFLEX_NONCHAR      (0xFFFD)
# define REFLEX_NONCHAR_UTF8 "\xef\xbf\xbd"
#else
/// Replace invalid UTF-8 with the non-character U+200000 code point for guaranteed error detection (the U+FFFD code point makes error detection harder and possible to miss).
# define REFLEX_NONCHAR      (0x200000)
# define REFLEX_NONCHAR_UTF8 "\xf8\x88\x80\x80\x80"
#endif

namespace reflex {

/// Convert an 8-bit ASCII + Latin-1 Supplement range [a,b] to a regex pattern.
std::string latin1(
    int  a,               ///< lower bound of UCS range
    int  b,               ///< upper bound of UCS range
    int  esc = 'x',       ///< escape char 'x' for hex \xXX, or '0' or '\0' for octal \0nnn and \nnn
    bool brackets = true) ///< place in [ brackets ]
  /// @returns regex string to match the UCS range encoded in UTF-8
  ;

/// Convert a UCS-4 range [a,b] to a UTF-8 regex pattern.
std::string utf8(
    int  a,                ///< lower bound of UCS range
    int  b,                ///< upper bound of UCS range
    int  esc = 'x',        ///< escape char 'x' for hex \xXX, or '0' or '\0' for octal \0nnn and \nnn
    const char *par = "(", ///< capturing or non-capturing parenthesis "(?:"
    bool strict = true)    ///< returned regex is strict UTF-8 (true) or permissive and lean UTF-8 (false)
  /// @returns regex string to match the UCS range encoded in UTF-8
  ;

/// Convert UCS-4 to UTF-8, fills with REFLEX_NONCHAR_UTF8 when out of range, or unrestricted UTF-8 with WITH_UTF8_UNRESTRICTED.
inline size_t utf8(
    int   c, ///< UCS-4 character U+0000 to U+10ffff (unless WITH_UTF8_UNRESTRICTED)
    char *s) ///< points to the buffer to populate with UTF-8 (1 to 6 bytes) not NUL-terminated
  /// @returns length (in bytes) of UTF-8 character sequence stored in s
{
  if (c < 0x80)
  {
    *s++ = static_cast<char>(c);
    return 1;
  }
#ifndef WITH_UTF8_UNRESTRICTED
  if (c > 0x10FFFF)
  {
    static const size_t n = sizeof(REFLEX_NONCHAR_UTF8) - 1;
    std::memcpy(s, REFLEX_NONCHAR_UTF8, n);
    return n;
  }
#endif
  char *t = s;
  if (c < 0x0800)
  {
    *s++ = static_cast<char>(0xC0 | ((c >> 6) & 0x1F));
  }
  else
  {
    if (c < 0x010000)
    {
      *s++ = static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
    }
    else
    {
      size_t w = c;
#ifndef WITH_UTF8_UNRESTRICTED
      *s++ = static_cast<char>(0xF0 | ((w >> 18) & 0x07));
#else
      if (c < 0x200000)
      {
        *s++ = static_cast<char>(0xF0 | ((w >> 18) & 0x07));
      }
      else
      {
        if (w < 0x04000000)
        {
          *s++ = static_cast<char>(0xF8 | ((w >> 24) & 0x03));
        }
        else
        {
          *s++ = static_cast<char>(0xFC | ((w >> 30) & 0x01));
          *s++ = static_cast<char>(0x80 | ((w >> 24) & 0x3F));
        }
        *s++ = static_cast<char>(0x80 | ((w >> 18) & 0x3F));
      }
#endif
      *s++ = static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    }
    *s++ = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
  }
  *s++ = static_cast<char>(0x80 | (c & 0x3F));
  return s - t;
}

/// Convert UTF-8 to UCS, returns REFLEX_NONCHAR for invalid UTF-8 except for MUTF-8 U+0000 and 0xD800-0xDFFF surrogate halves (use WITH_UTF8_UNRESTRICTED to remove any limits on UTF-8 encodings up to 6 bytes).
inline int utf8(
    const char *s,         ///< points to the buffer with UTF-8 (1 to 6 bytes)
    const char **r = NULL) ///< points to pointer to set to the new position in s after the UTF-8 sequence, optional
  /// @returns UCS character
{
  int c;
  c = static_cast<unsigned char>(*s++);
  if (c >= 0x80)
  {
    int c1 = static_cast<unsigned char>(*s);
#ifndef WITH_UTF8_UNRESTRICTED
    // reject invalid UTF-8 but permit Modified UTF-8 (MUTF-8) U+0000
    if (c < 0xC0 || (c == 0xC0 && c1 != 0x80) || c == 0xC1 || (c1 & 0xC0) != 0x80)
    {
      c = REFLEX_NONCHAR;
    }
    else
#endif
    {
      ++s;
      c1 &= 0x3F;
      if (c < 0xE0)
      {
        c = (((c & 0x1F) << 6) | c1);
      }
      else
      {
        int c2 = static_cast<unsigned char>(*s);
#ifndef WITH_UTF8_UNRESTRICTED
        // reject invalid UTF-8
        if ((c == 0xE0 && c1 < 0x20) || (c2 & 0xC0) != 0x80)
        {
          c = REFLEX_NONCHAR;
        }
        else
#endif
        {
          ++s;
          c2 &= 0x3F;
          if (c < 0xF0)
          {
            c = (((c & 0x0F) << 12) | (c1 << 6) | c2);
          }
          else
          {
            int c3 = static_cast<unsigned char>(*s);
#ifndef WITH_UTF8_UNRESTRICTED
            // reject invalid UTF-8
            if ((c == 0xF0 && c1 < 0x10) || (c == 0xF4 && c1 >= 0x10) || c >= 0xF5 || (c3 & 0xC0) != 0x80)
            {
              c = REFLEX_NONCHAR;
            }
            else
            {
              ++s;
              c = (((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | (c3 & 0x3F));
            }
#else
            ++s;
            c3 &= 0x3F;
            if (c < 0xF8)
            {
              c = (((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
            }
            else
            {
              int c4 = static_cast<unsigned char>(*s++) & 0x3F;
              if (c < 0xFC)
                c = (((c & 0x03) << 24) | (c1 << 18) | (c2 << 12) | (c3 << 6) | c4);
              else
                c = (((c & 0x01) << 30) | (c1 << 24) | (c2 << 18) | (c3 << 12) | (c4 << 6) | (static_cast<unsigned char>(*s++) & 0x3F));
            }
#endif
          }
        }
      }
    }
  }
  if (r != NULL)
    *r = s;
  return c;
}

/// Convert UTF-8 string to wide string.
inline std::wstring wcs(
    const char *s, ///< string with UTF-8 to convert
    size_t      n) ///< length of the string to convert
  /// @returns wide string
{
  std::wstring ws;
  const char *e = s + n;
  if (sizeof(wchar_t) == 2)
  {
    // sizeof(wchar_t) == 2 bytes: store wide string in std::wstring encoded in UTF-16
    while (s < e)
    {
      int wc = utf8(s, &s);
      if (wc > 0xFFFF)
      {
        if (wc <= 0x10FFFF)
        {
          ws.push_back(0xD800 | (wc - 0x010000) >> 10); // first half of UTF-16 surrogate pair
          ws.push_back(0xDC00 | (wc & 0x03FF));         // second half of UTF-16 surrogate pair
        }
        else
        {
          ws.push_back(0xFFFD);
        }
      }
      else
      {
        ws.push_back(wc);
      }
    }
  }
  else
  {
    while (s < e)
      ws.push_back(utf8(s, &s));
  }
  return ws;
}

/// Convert UTF-8 string to wide string.
inline std::wstring wcs(const std::string& s) ///< string with UTF-8 to convert
  /// @returns wide string
{
  return wcs(s.c_str(), s.size());
}

} // namespace reflex

#endif
