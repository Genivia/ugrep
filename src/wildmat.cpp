//  wildmat.cpp
//
//  Modified by Robert van Engelen, May 11, 2019 to support gitignore-style glob
//  matching, matching / in a glob against the windows \ path separator, to fix
//  a logic error, and to remove compiler errors.
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
//  [!a-z] matches one character not in the selected range of characters
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

/* $XConsortium: wildmat.c,v 1.2 94/04/13 18:40:59 rws Exp $ */
/*
**
**  Do shell-style pattern matching for ?, \, [], and * characters.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Rich $alz is now <rsalz@bbn.com>.
**  April, 1991:  Replaced mutually-recursive calls with in-line code
**  for the star character.
**
**  Special thanks to Lars Mathiesen <thorinn@diku.dk> for the ABORT code.
**  This can greatly speed up failing wildcard patterns.  For example:
**      pattern: -*-*-*-*-*-*-12-*-*-*-m-*-*-*
**      text 1:  -adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1
**      text 2:  -adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1
**  Text 1 matches with 51 calls, while text 2 fails with 54 calls.  Without
**  the ABORT, then it takes 22310 calls to fail.  Ugh.  The following
**  explanation is from Lars:
**  The precondition that must be fulfilled is that DoMatch will consume
**  at least one character in text.  This is true if *p is neither '*' nor
**  '\0'.)  The last return has ABORT instead of FALSE to avoid quadratic
**  behaviour in cases like pattern "*a*b*c*d" with text "abcxxxxx".  With
**  FALSE, each star-loop has to run to the end of the text; with ABORT
**  only the last one does.
**
**  Once the control of one instance of DoMatch enters the star-loop, that
**  instance will return either TRUE or ABORT, and any calling instance
**  will therefore return immediately after (without calling recursively
**  again).  In effect, only one star-loop is ever active.  It would be
**  possible to modify the code to maintain this context explicitly,
**  eliminating all recursive calls at the cost of some complication and
**  loss of clarity (and the ABORT stuff seems to be unclear enough by
**  itself).  I think it would be unwise to try to get this into a
**  released version unless you have a good test data base to try it out
**  on.
*/

// check if we are on a windows OS
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

#include <cstring>

#ifdef OS_WIN
#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"
#else
#define PATHSEPCHR '/'
#define PATHSEPSTR "/"
#endif

#define TRUE   1
#define FALSE  0
#define ABORT -1

/*
**  Match text and p, return TRUE, FALSE, or ABORT.
*/
static int DoMatch(const char *text, const char *p)
{
  int last;
  int matched;
  int reverse;

  for ( ; *p; text++, p++)
  {
    if (*text == '\0' && *p != '*')
      return ABORT;

    switch (*p)
    {
#ifdef OS_WIN
      case '/':
        /* / matches \ on Windows */
        if (*text != '\\')
          return FALSE;
        continue;
#endif

      case '\\':
        /* Literal match with following character. */
        p++;
        /* FALLTHROUGH */

      default:
        if (*text != *p)
          return FALSE;
        continue;

      case '?':
        /* Match anything except a /. */
        if (*text == PATHSEPCHR)
          return FALSE;
        continue;

      case '*':
        if (*++p == '*')
        {
          if (*++p == '\0')
            /* Two trailing stars match everything after /. */
            return TRUE;
          if (*p++ == '/')
          {
            /* Two consecutive stars followed by a / match zero or more directories. */
            while (*text)
            {
              if ((matched = DoMatch(text++, p)) != FALSE)
                return matched;
              if ((text = strchr(text, PATHSEPCHR)) == NULL)
                break;
              ++text;
            }
          }
          return ABORT;
        }
        if (*p == '\0')
          /* Trailing star matches everything except a /. */
          return strchr(text, PATHSEPCHR) == NULL;
        /* Match everything except a /. */
        while (*text)
        {
          if ((matched = DoMatch(text, p)) != FALSE)
            return matched;
          if (*text++ == PATHSEPCHR)
            break;
        }
        return ABORT;

      case '[':
        reverse = p[1] == '^' || p[1] == '!' ? TRUE : FALSE;
        if (reverse)
          /* Inverted character class. */
          p++;
        for (last = 0400, matched = FALSE; *++p && *p != ']'; last = *p)
          /* This next line requires a good C compiler. */
          if (*p == '-' && p[1] ? *text <= *++p && *text >= last : *text == *p)
            matched = TRUE;
        if (matched == reverse)
          return FALSE;
        continue;
    }
  }

  return *text == '\0';
}

/*
**  User-level routine: pathname or basename matching.  Returns TRUE or FALSE.
*/
bool globmat(const char *pathname, const char *basename, const char *glob)
{
  /* if the pathname starts with ./ then remove it */
  if (strncmp(pathname, "." PATHSEPSTR, 2) == 0)
    pathname += 2;
  /* match pathname if glob contains a /, basename otherwise */
  if (strchr(glob, '/') != NULL)
  {
    /* a leading / means globbing the pathname after removing the / */
    if (glob[0] == '/')
      ++glob;
    return DoMatch(pathname, glob) == TRUE;
  }
  return DoMatch(basename, glob) == TRUE;
}

#ifdef TEST

#include <stdio.h>

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
