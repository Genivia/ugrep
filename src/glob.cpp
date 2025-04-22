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
@copyright (c) 2019-2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

// See my article "Fast String Matching with Wildcards, Globs, and
//                 Gitignore-Style Globs - How Not to Blow it Up":
// https://www.codeproject.com/Articles/5163931/Fast-String-Matching-with-Wildcards-Globs-and-Giti
//
//  - supports gitignore-style glob matching, see syntax below
//  - matches / in globs against the windows \ path separator
//  - replaced recursion by iteration (two levels of iteration are needed to
//    match a/bc/bc against a**/b*c, one for the last shallow * wildcard and
//    one for the last deep ** wildcard)
//  - linear time complexity in the length of the text for usual cases, with
//    worst-case quadratic time
//  - performs case-insensitive matching when the icase flag is set to true
//  - the lead option matches the leading path part of the glob against the
//    pathname, for example the glob foo/bar/baz matches pathname foo/bar
//  - the path option matches the path part but not the basename of a pathname,
//    for example, the glob foo/bar/baz matches pathname foo/bar/baz/file.txt
//    and the glob ./ (or just /) matches pathname file.txt
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
//  /      when at the start of the glob matches the working directory
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

// check if we are natively compiling for a Windows OS (not Cygwin and not MinGW)
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

#include <cstdio>
#include <cstring>
#include <cctype>

#ifdef OS_WIN
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

static int utf8(const char **s, bool icase = false);

// match text against glob, return true or false, perform case-insensitive match if icase=true, partial when part=true, path only when path=true
static bool match(const char *text, const char *glob, bool icase, bool lead, bool path)
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
          glob2_backup = glob;
          if (*text != '/')
            ++glob;
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
        ++glob;
        continue;

      case '[':
      {
        // match character class, ignoring case if icase is true
        int chr = utf8(&text, icase), last = 0x10ffff;

        // match anything except /
        if (chr == PATHSEP)
          break;

        bool matched = false;
        bool reverse = glob[1] == '^' || glob[1] == '!';

        // inverted character class
        if (reverse)
          ++glob;
        ++glob;

        int lc = chr, uc = (icase && lc >= 'a' && lc <= 'z' ? toupper(chr) : chr);

        if (lc == uc)
        {
          while (*glob != '\0' && *glob != ']')
            if (last < 0x10ffff && *glob == '-' && glob[1] != ']' && glob[1] != '\0' ?
                chr <= utf8(&++glob) && chr >= last :
                chr == (last = utf8(&glob)))
              matched = true;
        }
        else
        {
          while (*glob != '\0' && *glob != ']')
            if (last < 0x10ffff && *glob == '-' && glob[1] != ']' && glob[1] != '\0' ?
                ((lc <= (chr = utf8(&++glob)) && lc >= last) || (uc <= chr && uc >= last)) :
                (lc == (last = utf8(&glob)) || uc == last))
              matched = true;
        }

        if (matched == reverse)
          break;

        if (*glob)
          ++glob;
        continue;
      }

      case '/':
        if (*text != PATHSEP)
          break;
        ++text;
        ++glob;
        continue;

      case '\\':
        // literal match \-escaped character
        ++glob;
        // FALLTHROUGH

      default:
        if (icase ?
            tolower(static_cast<unsigned char>(*glob)) != tolower(static_cast<unsigned char>(*text)) :
            *glob != *text)
          break;

        ++text;
        ++glob;
        continue;
    }

    // the path option matches the path up to but not including the basename
    if (path && *glob == '\0' && *text == PATHSEP && strchr(text + 1, PATHSEP) == NULL)
      return true;

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
    ++glob;
  return (*glob == '\0' && !path) || (*glob == '/' && lead);
}
 
// pathname or basename glob matching, returns true or false, perform case-insensitive match if icase is true
bool glob_match(const char *pathname, const char *basename, const char *glob, bool icase, bool lead, bool path)
{
  // if pathname starts with ./ then skip this
  while (pathname[0] == '.' && pathname[1] == PATHSEP)
    pathname += 2;
  // if pathname starts with / then skip /
  while (pathname[0] == PATHSEP)
    ++pathname;

  // match pathname if glob contains a / or match the basename otherwise
  if (strchr(glob, '/') != NULL)
  {
    // remove leading ./ or /
    if (glob[0] == '.' && glob[1] == '/')
      glob += 2;
    else if (glob[0] == '/')
      ++glob;

    if (*glob != '\0')
      return match(pathname, glob, icase, lead, path);

    // the path option matches the path up to but not including the basename, check the ./ glob case (glob is empty)
    return *pathname == '\0' || (path && strchr(pathname, PATHSEP) == NULL);
  }

  // match basename, unless matching an empty path to the basename which always matches
  return path || match(basename, glob, icase, false, false);
}

// return wide character of UTF-8 multi-byte sequence, return ASCII lower case if icase is true
static int utf8(const char **s, bool icase)
{
  int c1, c2, c3, c = static_cast<unsigned char>(**s);
  if (c != '\0')
    ++*s;
  if (c < 0x80)
    return icase ? tolower(c) : c;
  c1 = static_cast<unsigned char>(**s);
  if (c < 0xC0 || (c == 0xC0 && c1 != 0x80) || c == 0xC1 || (c1 & 0xC0) != 0x80)
    return 0xFFFD;
  if (c1 != '\0')
    ++*s;
  c1 &= 0x3F;
  if (c < 0xE0)
    return (((c & 0x1F) << 6) | c1);
  c2 = static_cast<unsigned char>(**s);
  if ((c == 0xE0 && c1 < 0x20) || (c2 & 0xC0) != 0x80)
    return 0xFFFD;
  if (c2 != '\0')
    ++*s;
  c2 &= 0x3F;
  if (c < 0xF0)
    return (((c & 0x0F) << 12) | (c1 << 6) | c2);
  c3 = static_cast<unsigned char>(**s);
  if (c3 != '\0')
    ++*s;
  if ((c == 0xF0 && c1 < 0x10) || (c == 0xF4 && c1 >= 0x10) || c >= 0xF5 || (c3 & 0xC0) != 0x80)
    return 0xFFFD;
  return (((c & 0x07) << 18) | (c1 << 12) | (c2 << 6) | (c3 & 0x3F));
}
