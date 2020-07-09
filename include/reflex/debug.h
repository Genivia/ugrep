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
@file      debug.h
@brief     RE/flex debug logs and assertions
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Exploiting macro magic to simplify debug logging.

Usage
-----

Enable macro DEBUG to debug the compiled source code:

| Source files compiled with	| DBGLOG(...) entry added to	|
| ----------------------------- | ----------------------------- |
| `c++ -DDEBUG`			| `DEBUG.log`			|
| `c++ -DDEBUG=TEST`		| `TEST.log`			|
| `c++ -DDEBUG= `		| `stderr`			|

`DBGLOG(format, ...)` creates a timestamped log entry with a printf-formatted
message. The log entry is added to a log file or sent to `stderr` as specified:

`DBGLOGN(format, ...)` creates a log entry without a timestamp.

`DBGLOGA(format, ...)` appends the formatted string to the previous log entry.

`DBGCHK(condition)` calls `assert(condition)` when compiled in DEBUG mode.

The utility macro `DBGSTR(const char *s)` returns string `s` or `"(null)"` when
`s == NULL`.

@note to temporarily enable debugging a specific block of code without globally
debugging all code, use a leading underscore, e.g. `_DBGLOG(format, ...)`.
This appends the debugging information to `DEBUG.log`.

@warning Be careful to revert these statements by removing the leading
underscore for production-quality code.

Example
-------

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
    #include <reflex/debug.h>

    int main(int argc, char *argv[])
    {
      FILE *fd;
      DBGLOG("Program start");
      if ((fd = fopen("foo.bar", "r")) == NULL)
      {
        DBGLOG("Error %d: %s ", errno, DBGSTR(strerror(errno)));
        for (int i = 1; i < argc; ++i)
          DBGLOGA(" %s", argv[1]);
      }
      else
      {
        DBGCHK(fd != NULL);
        // OK, so go ahead to read foo.bar ...
        // ...
        fclose(fd);
      }
      DBGLOG("Program end");
    }
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Compiled with `-DDEBUG` this example logs the following messages in `DEBUG.log`:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.txt}
    140201/225654.692194   example.cpp:11   Program has started
    140201/225654.692564   example.cpp:15   Error 2: No such file or directory
    140201/225654.692577   example.cpp:17   Program ended
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The first column records the date (140201 is February 1, 2014) and the time
(225654 is 10:56PM + 54 seconds) with microsecond fraction. The second column
records the source code file name and the line number of the `DBGLOG` command.
The third column shows the printf-formatted message.

The `DEBUG.log` file is created in the current directory when it does not
already exist.

Techniques used:

- Variadic macros with `__VA_ARGS__`.
- Standard predefined macros `__FILE__` and `__LINE__`.
- Macro "stringification": expand content of macro `DEBUG` as a string in a
  macro body.
- `#if DEBUG + 0` to test whether macro `DEBUG` is set to a value, since
  `DEBUG` is 1 when set without a value (for example at the command line).
- `"" __VA_ARGS__` forces `__VA_ARGS__` to start with a literal format string
  (printf security advisory).
*/

#ifndef REFLEX_DEBUG_H
#define REFLEX_DEBUG_H

#include <cassert>
#include <cstdio>

/// If ASSERT not defined, make ASSERT a no-op
#ifndef ASSERT
#define ASSERT(c)
#endif

#undef DBGLOG
#undef DBGLOGN
#undef DBGLOGA

extern FILE *REFLEX_DBGFD_;

extern "C" void REFLEX_DBGOUT_(const char *log, const char *file, int line);

#define DBGXIFY(S) DBGIFY_(S)
#define DBGIFY_(S) #S
#if DEBUG + 0
# define DBGFILE "DEBUG.log"
#else
# define DBGFILE DBGXIFY(DEBUG) ".log"
#endif
#define DBGSTR(S) (S?S:"(NULL)")
#define _DBGLOG(...) \
( REFLEX_DBGOUT_(DBGFILE, __FILE__, __LINE__), ::fprintf(REFLEX_DBGFD_, "" __VA_ARGS__), ::fflush(REFLEX_DBGFD_))
#define _DBGLOGN(...) \
( ::fprintf(REFLEX_DBGFD_, "\n                                        " __VA_ARGS__), ::fflush(REFLEX_DBGFD_) )
#define _DBGLOGA(...) \
( ::fprintf(REFLEX_DBGFD_, "" __VA_ARGS__), ::fflush(REFLEX_DBGFD_) )

#ifdef DEBUG

#define DBGCHK(c) assert(c)

#define DBGLOG _DBGLOG
#define DBGLOGN _DBGLOGN
#define DBGLOGA _DBGLOGA

#else

#define DBGCHK(c) (void)0

#define DBGLOG(...) (void)0
#define DBGLOGN(...) (void)0
#define DBGLOGA(...) (void)0

#endif

#endif
