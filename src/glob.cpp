/******************************************************************************\
* Copyright (c) 2019, Robert van Engelen, Genivia Inc. All rights reserved.    *
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
@file      glob.cpp
@brief     gitignore-style pathname globbing
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

//  - supports gitignore-style glob matching, see syntax below
//  - matches / in globs against the windows \ path separator
//  - replaced recursion by iteration (two levels of iteration are needed to
//    match a/bc/bc against a**/b*c, one for the last shallow * wildcard and
//    one for the last deep ** wildcard)
//  - linear time complexity in the length of the text for usual cases, with
//    worst-case quadratic time
//
//  Pathnames are normalized by removing any leading ./ and / from the pathname
//
//  Glob syntax:
//
//  *      matches anything except a /
//  ?      matches any one character except a /
//  [a-z]  matches one character in the selected range of characters
//  [^a-z] matches one character not in the selected range of characters
//  [!a-z] same as [^a-z]
//  /      when at the begin of the glob matches if the pathname has no /
//  **/    matches zero or more directories
//  /**    when at the end of the glob matches everything after the /
//  \?     matches a ? (or any character specified after the backslash)
//
//  Examples:
//
//  *         matches a, b, x/a, x/y/b
//  a         matches a, x/a, x/y/a        but not b, x/b, a/a/b
//  /*        matches a, b                 but not x/a, x/b, x/y/a
//  /a        matches a                    but not x/a, x/y/a
//  /a?b      matches axb, ayb             but not a, b, ab, a/b
//  /a[xy]b   matches axb, ayb             but not a, b, azb
//  /a[a-z]b  matches aab, abb, acb, azb   but not a, b, a3b, aAb, aZb
//  /a[^xy]b  matches aab, abb, acb, azb   but not a, b, axb, ayb
//  /a[^a-z]b matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb
//  a/*/b     matches a/x/b, a/y/b         but not a/b, a/x/y/b
//  **/a      matches a, x/a, x/y/a        but not b, x/b
//  a/**/b    matches a/b, a/x/b, a/x/y/b  but not x/a/b, a/b/x
//  a/**      matches a/x, a/y, a/x/y      but not a, b/x
//  a\?b      matches a?b                  but not a, b, ab, axb, a/b

// check if we are on a windows OS
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

#include <cstdio>
#include <cstring>

#ifdef OS_WIN
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

static int utf8(const char **s);

// match text against glob, return true or false
static bool match(const char *text, const char *glob)
{
  // to iteratively backtrack on *
  const char *text1_backup = NULL;
  const char *glob1_backup = NULL;

  // to iteratively backtrack on **
  const char *text2_backup = NULL;
  const char *glob2_backup = NULL;

  // match until end of text
  while (*text != '\0')
  {
    switch (*glob)
    {
      case '*':
        if (*++glob == '*')
        {
          // trailing ** match everything after /
          if (*++glob == '\0')
            return true;

          // ** followed by a / match zero or more directories
          if (*glob != '/')
            return false;

          // iteratively backtrack on **
          text1_backup = NULL;
          glob1_backup = NULL;
          text2_backup = text;
          glob2_backup = ++glob;
          continue;
        }

        // iteratively backtrack on *
        text1_backup = text;
        glob1_backup = glob;
        continue;

      case '?':
        // match anything except /
        if (*text == PATHSEP)
          break;

        utf8(&text);
        glob++;
        continue;

      case '[':
      {
        int chr = utf8(&text), last = 0x10ffff;

        // match anything except /
        if (chr == PATHSEP)
          break;

        bool matched = false;
        bool reverse = glob[1] == '^' || glob[1] == '!';

        // inverted character class
        if (reverse)
          glob++;
        glob++;

        // match character class
        while (*glob != '\0' && *glob != ']')
          if (last < 0x10ffff && *glob == '-' && glob[1] != ']' && glob[1] != '\0' ?
              chr <= utf8(&++glob) && chr >= last :
              chr == (last = utf8(&glob)))
            matched = true;

        if (matched == reverse)
          break;

        if (*glob)
          glob++;
        continue;
      }

      case '\\':
        // literal match \-escaped character
        glob++;
        // FALLTHROUGH

      default:
#ifdef OS_WIN
        if (*glob != *text && !(*glob == '/' && *text == '\\'))
#else
        if (*glob != *text)
#endif
          break;

        text++;
        glob++;
        continue;
    }

    if (glob1_backup != NULL && *text1_backup != PATHSEP)
    {
      // backtrack on the last *, do not jump over /
      text = ++text1_backup;
      glob = glob1_backup;
      continue;
    }

    if (glob2_backup != NULL)
    {
      // backtrack on the last **
      text = ++text2_backup;
      glob = glob2_backup;
      continue;
    }

    return false;
  }

  while (*glob == '*')
    glob++;
  return *glob == '\0';
}
 
// pathname or basename matching, returns true or false
bool glob_match(const char *pathname, const char *basename, const char *glob)
{
  // if pathname starts with ./ then skip this
  while (pathname[0] == '.' && pathname[1] == PATHSEP)
    pathname += 2;
  // if pathname starts with / then skip it
  while (pathname[0] == PATHSEP)
    ++pathname;

  // match pathname if glob contains a / or match the basename otherwise
  if (strchr(glob, '/') != NULL)
  {
    // a leading ./ or / in the glob means globbing the pathname after removing the ./ or /
    if (glob[0] == '.' && glob[1] == '/')
      glob += 2;
    else if (glob[0] == '/')
      ++glob;
    return match(pathname, glob);
  }

  return match(basename, glob);
}

// return wide character of UTF-8 multi-byte sequence
static int utf8(const char **s)
{
  int c1, c2, c3, c = (unsigned char)**s;
  if (c != '\0')
    (*s)++;
  if (c < 0x80)
    return c;
  c1 = (unsigned char)**s;
  if (c < 0xC0 || (c == 0xC0 && c1 != 0x80) || c == 0xC1 || (c1 & 0xC0) != 0x80)
    return 0xFFFD;
  if (c1 != '\0')
    (*s)++;
  c1 &= 0x3F;
  if (c < 0xE0)
    return (((c & 0x1F) << 6) | c1);
  c2 = (unsigned char)**s;
  if ((c == 0xE0 && c1 < 0x20) || (c2 & 0xC0) != 0x80)
    return 0xFFFD;
  if (c2 != '\0')
    (*s)++;
  c2 &= 0x3F;
  if (c < 0xF0)
    return (((c & 0x0F) << 12) | (c1 << 6) | c2);
  c3 = (unsigned char)**s;
  if (c3 != '\0')
    (*s)++;
  if ((c == 0xF0 && c1 < 0x10) || (c == 0xF4 && c1 >= 0x10) || c >= 0xF5 || (c3 & 0xC0) != 0x80)
    return 0xFFFD;
  return (((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | (c3 & 0x3F));
}
