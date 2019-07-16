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
@brief     Universal grep - a pattern search utility
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
//  Glob syntax:
//
//  **/    matches zero or more directories
//  /**    when at the end of the glob matches everything after the /
//  *      matches anything except a /
//  /      when at the start of the glob matches if the pathname has no /
//  ?      matches any character except a /
//  [a-z]  matches one character in the selected range of characters
//  [^a-z] matches one character not in the selected range of characters
//  [!a-z] same as [^a-z]
//  \?     matches a ? (or any character specified after the backslash)
//
//  Examples:
//
//  **/a     matches a, x/a, x/y/a,       but not b, x/b
//  a/**/b   matches a/b, a/x/b, a/x/y/b, but not x/a/b, a/b/x
//  a/**     matches a/x, a/y, a/x/y,     but not b/x
//  a/*/b    matches a/x/b, a/y/b,        but not a/x/y/b
//  /a       matches a,                   but not x/a
//  /*       matches a, b,                but not x/a
//  a?b      matches axb, ayb,            but not a, b, ab
//  a[xy]b   matches axb, ayb             but not a, b, azb
//  a[a-z]b  matches aab, abb, acb, azb,  but not a, b, a3b, aAb, aZb
//  a[^xy]b  matches aab, abb, acb, azb,  but not a, b, axb, ayb
//  a[^a-z]b matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb

#ifdef TEST
#include <stdio.h>
#endif

/* check if we are on a windows OS */
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

#include <cstring>

#ifdef OS_WIN
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

#define TRUE   1
#define FALSE  0

/* match text against glob, return TRUE or FALSE */
static int match(const char *text, const char *glob)
{
  /* to iteratively backtrack on * */
  const char *back1 = NULL;
  const char *star1 = NULL;
  /* to iteratively backtrack on ** */
  const char *back2 = NULL;
  const char *star2 = NULL;
  /* to match [ ] character ranges in */
  int last;
  int matched;
  int reverse;

  for ( ; *text != '\0' || *glob == '\0' || *glob == '*'; text++, glob++)
  {
    switch (*glob)
    {
      case '\0':
        /* end of the glob and end of the text? */
        if (*text == '\0')
          return TRUE;
        break;

#ifdef OS_WIN
      case '/':
        /* / matches \ on Windows */
        if (*text == PATHSEP)
          continue;
        break;
#endif

      case '?':
        /* match anything except a / */
        if (*text != PATHSEP)
          continue;
        break;

      case '*':
        if (*++glob == '*')
        {
          if (*++glob == '\0')
            /* two trailing stars match everything after / */
            return TRUE;

          if (*glob++ == '/')
          {
            /* two consecutive stars followed by a / match zero or more directories */
            back1 = NULL;
            star1 = NULL;
            back2 = text;
            star2 = glob;
            text--;
            glob--;
            continue;
          }

          return FALSE;
        }

        if (*glob == '\0')
          /* trailing star matches everything except a / */
          return strchr(text, PATHSEP) == NULL ? TRUE : FALSE;

        /* continue matching everything except a / */
        back1 = text--;
        star1 = glob--;
        continue;

      case '[':
        reverse = glob[1] == '^' || glob[1] == '!' ? TRUE : FALSE;
        if (reverse)
          /* inverted character class */
          glob++;
        for (last = 256, matched = FALSE; *++glob && *glob != ']'; last = *glob)
          if (*glob == '-' && glob[1] ? *text <= *++glob && *text >= last : *text == *glob)
            matched = TRUE;
        if (matched != reverse)
          continue;
        break;

      case '\\':
        /* literal match with \-escaped character */
        glob++;
        /* FALLTHROUGH */

      default:
        if (*text == *glob)
          continue;
        break;
    }

    if (back1 != NULL)
    {
      /* backtrack on the last *, do not jump over / */
      if (*back1 != PATHSEP)
      {
        text = back1++;
        glob = star1 - 1;
        continue;
      }
    }

    if (back2 != NULL)
    {
      /* backtrack on the last ** */
      text = back2++;
      glob = star2 - 1;
      back1 = NULL;
      star1 = NULL;
      continue;
    }

    return FALSE;
  }

  return FALSE;
}

/* pathname or basename matching, returns TRUE or FALSE */
bool globmat(const char *pathname, const char *basename, const char *glob)
{
  /* if pathname starts with ./ then remove these pairs */
  while (pathname[0] == '.' && pathname[1] == PATHSEP)
    pathname += 2;

  /* match pathname if glob contains a /, match the basename otherwise */
  if (strchr(glob, '/') != NULL)
  {
    /* a leading / in the glob means globbing the pathname after removing the / */
    if (glob[0] == '/')
      ++glob;
    return match(pathname, glob) == TRUE;
  }

  return match(basename, glob) == TRUE;
}

#ifdef TEST

/* test and demo */
int main(int argc, char **argv)
{
  if (argc >= 4)
  {
    printf("pathname=%s basename=%s glob=%s\n", argv[1], argv[2], argv[3]);
    if (globmat(argv[1], argv[2], argv[3]))
      printf("Match\n");
    else
      printf("No match\n");
  }
}

#endif
