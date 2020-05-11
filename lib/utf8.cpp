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
@file      utf8.cpp
@brief     RE/flex UCS to UTF8 converters
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/utf8.h>

namespace reflex {

static const char *regex_char(char *buf, int a, int esc, size_t *n = NULL)
{
  static const char digits[] = "0123456789abcdef";
  if (a >= '!' && a <= '~' && a != '#' && a != '-' && a != '[' && a != '\\' && a != ']' && a != '^' &&
      (n != NULL || (a <= 'z' && a != '$' && a != '(' && a != ')' && a != '*' && a != '+' && a != '.' && a != '?'))
     )
  {
    buf[0] = a;
    buf[1] = '\0';
    if (n)
      *n = 1;
  }
  else
  {
    buf[0] = '\\';
    if (esc == 'x')
    {
      buf[1] = 'x';
      buf[2] = digits[a >> 4 & 0xf];
      buf[3] = digits[a & 0xf];
      buf[4] = '\0';
      if (n)
	*n = 4;
    }
    else if (esc == '0')
    {
      buf[1] = '0';
      buf[2] = digits[a >> 6 & 7];
      buf[3] = digits[a >> 3 & 7];
      buf[4] = digits[a & 7];
      buf[5] = '\0';
      if (n)
	*n = 5;
    }
    else
    {
      buf[1] = digits[a >> 6 & 7];
      buf[2] = digits[a >> 3 & 7];
      buf[3] = digits[a & 7];
      buf[4] = '\0';
      if (n)
	*n = 4;
    }
  }
  return buf;
}

static const char *regex_range(char *buf, int a, int b, int esc, bool brackets = true)
{
  if (a == b)
    return regex_char(buf, a, esc);
  char *s = buf;
  if (brackets)
    *s++ = '[';
  size_t n;
  regex_char(s, a, esc, &n);
  s += n;
  if (b > a + 1)
    *s++ = '-';
  regex_char(s, b, esc, &n);
  s += n;
  if (brackets)
    *s++ = ']';
  *s++ = '\0';
  return buf;
}

/// Convert an 8-bit ASCII + Latin-1 Supplement range [a,b] to a regex pattern.
std::string latin1(int a, int b, int esc, bool brackets)
{
  if (a < 0)
    return ""; // undefined
  if (a > b)
    b = a;
  char buf[16];
  return regex_range(buf, a, b, esc, brackets);
}

/// Convert a UCS-4 range [a,b] to a UTF-8 regex pattern.
std::string utf8(int a, int b, int esc, const char *par, bool strict)
{
  if (a < 0)
    return ""; // undefined
  if (a > b)
    b = a;
  static const char *min_utf8_strict[6] = { // strict: pattern is strict, matching only strictly valid UTF-8
    "\x00",
    "\xc2\x80",
    "\xe0\xa0\x80",
    "\xf0\x90\x80\x80",
    "\xf8\x88\x80\x80\x80",
    "\xfc\x84\x80\x80\x80\x80"
  };
  static const char *min_utf8_lean[6] = { // lean: pattern is permissive, matching also some invalid UTF-8 but more tightly compressed UTF-8
    "\x00",
    "\xc2\x80",
    "\xe0\x80\x80",
    "\xf0\x80\x80\x80",
    "\xf8\x80\x80\x80\x80",
    "\xfc\x80\x80\x80\x80\x80"
  };
  static const char *max_utf8[6] = {
    "\x7f",
    "\xdf\xbf",
    "\xef\xbf\xbf",
    "\xf7\xbf\xbf\xbf",
    "\xfb\xbf\xbf\xbf\xbf",
    "\xfd\xbf\xbf\xbf\xbf\xbf"
  };
  const char **min_utf8 = (strict ? min_utf8_strict : min_utf8_lean);
  char any[16];
  char buf[16];
  char at[6];
  char bt[6];
  size_t n = utf8(a, at);
  size_t m = utf8(b, bt);
  const unsigned char *as = reinterpret_cast<const unsigned char*>(at);
  const unsigned char *bs = NULL;
  std::string regex;
  if (strict)
  {
    regex_range(any, 0x80, 0xbf, esc);
  }
  else
  {
    any[0] = '.';
    any[1] = '\0';
  }
  while (n <= m)
  {
    if (n < m)
      bs = reinterpret_cast<const unsigned char*>(max_utf8[n - 1]);
    else
      bs = reinterpret_cast<const unsigned char*>(bt);
    size_t i;
    for (i = 0; i < n && as[i] == bs[i]; ++i)
      regex.append(regex_char(buf, as[i], esc));
    int l = 0; // pattern compression: l == 0 -> as[i+1..n-1] == UTF-8 lower bound
    for (size_t k = i + 1; k < n && l == 0; ++k)
      if (as[k] != 0x80)
        l = 1;
    int h = 0; // pattern compression: h == 0 -> bs[i+1..n-1] == UTF-8 upper bound
    for (size_t k = i + 1; k < n && h == 0; ++k)
      if (bs[k] != 0xbf)
        h = 1;
    if (i + 1 < n)
    {
      size_t j = i;
      if (i != 0)
        regex.append(par);
      if (l != 0)
      {
        size_t p = 0;
        regex.append(regex_char(buf, as[i], esc));
        ++i;
        while (i + 1 < n)
        {
          if (as[i + 1] == 0x80) // pattern compression
          {
            regex.append(regex_range(buf, as[i], 0xbf, esc));
            for (++i; i < n && as[i] == 0x80; ++i)
              regex.append(any);
          }
          else
          {
            if (as[i] != 0xbf)
            {
              ++p;
              regex.append(par).append(regex_range(buf, as[i] + 1, 0xbf, esc));
              for (size_t k = i + 1; k < n; ++k)
                regex.append(any);
              regex.append("|");
            }
            regex.append(regex_char(buf, as[i], esc));
            ++i;
          }
        }
        if (i < n)
          regex.append(regex_range(buf, as[i], 0xbf, esc));
        for (size_t k = 0; k < p; ++k)
          regex.append(")");
        i = j;
      }
      if (i + 1 < n && as[i] + l <= bs[i] - h)
      {
        if (l != 0)
          regex.append("|");
        regex.append(regex_range(buf, as[i] + l, bs[i] - h, esc));
        for (size_t k = i + 1; k < n; ++k)
          regex.append(any);
      }
      if (h != 0)
      {
        size_t p = 0;
        regex.append("|").append(regex_char(buf, bs[i], esc));
        ++i;
        while (i + 1 < n)
        {
          if (bs[i + 1] == 0xbf) // pattern compression
          {
            regex.append(regex_range(buf, 0x80, bs[i], esc));
            for (++i; i < n && bs[i] == 0xbf; ++i)
              regex.append(any);
          }
          else
          {
            if (bs[i] != 0x80)
            {
              ++p;
              regex.append(par).append(regex_range(buf, 0x80, bs[i] - 1, esc));
              for (size_t k = i + 1; k < n; ++k)
                regex.append(any);
              regex.append("|");
            }
            regex.append(regex_char(buf, bs[i], esc));
            ++i;
          }
        }
        if (i < n)
          regex.append(regex_range(buf, 0x80, bs[i], esc));
        for (size_t k = 0; k < p; ++k)
          regex.append(")");
      }
      if (j != 0)
        regex.append(")");
    }
    else if (i < n)
    {
      regex.append(regex_range(buf, as[i], bs[i], esc));
    }
    if (n < m)
    {
      as = reinterpret_cast<const unsigned char*>(min_utf8[n]);
      regex.append("|");
    }
    ++n;
  }
  return regex;
}

} // namespace reflex
