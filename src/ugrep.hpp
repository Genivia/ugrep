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
@file      ugrep.hpp
@brief     file pattern searcher
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef UGREP_HPP
#define UGREP_HPP

// DO NOT ALTER THIS LINE: updated by makemake.sh and we need it physically here for MSVC++ build from source
#define UGREP_VERSION "7.4.2"

// disable mmap because mmap is almost always slower than the file reading speed improvements since 3.0.0
#define WITH_NO_MMAP

// use a task-parallel thread to decompress the stream into a pipe to search, also handles nested archives
#define WITH_DECOMPRESSION_THREAD

// use a lock-free job queue, which is appears to be SLOWER than a standard simple lock-based queue for each worker
// #define WITH_LOCK_FREE_JOB_QUEUE

// drain stdin until eof to keep reading from stdin such as a pipe (and prevent broken pipe signal)
#define WITH_STDIN_DRAIN

// quick warn about unreadable file/dir arguments before searching by checking stat S_IRUSR
// #define WITH_WARN_UNREADABLE_FILE_ARG

// enable easy-to-use abbreviated ANSI SGR color codes with WITH_EASY_GREP_COLORS
// semicolons are not required and abbreviations can be mixed with numeric ANSI SGR codes
// foreground colors: k=black, r=red, g=green, y=yellow b=blue, m=magenta, c=cyan, w=white
// background colors: K=black, R=red, G=green, Y=yellow B=blue, M=magenta, C=cyan, W=white
// bright colors: +k, +r, +g, +y, +b, +m, +c, +w, +K, +R, +G, +Y, +B, +M, +C, +W
// modifiers: h=highlight, u=underline, i=invert, f=faint, n=normal, H=highlight off, U=underline off, I=invert off
#define WITH_EASY_GREP_COLORS

// use $XDG_CONFIG_HOME/ugrep/config for ug (or ugrep --config) when no .ugrep files are found, migh be confusing!
// #define WITH_XDG_CONFIG_HOME

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

#ifdef OS_WIN // compiling for a windows OS

// disable legacy min/max macros so we can use std::min and std::max
#define NOMINMAX

#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <io.h>
#include <strsafe.h>
#include <string.h>
#include <fcntl.h>
#include <direct.h>
#include <errno.h>
#include <string>
#include <cstdint>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"
#define NEWLINESTR "\r\n" // Note: also hard-coded into Output class

// POSIX read() and write() return type is ssize_t
typedef int ssize_t;

// POSIX pipe() emulation
inline int pipe(int fd[2])
{
  HANDLE pipe_r = NULL;
  HANDLE pipe_w = NULL;
  if (CreatePipe(&pipe_r, &pipe_w, NULL, 0))
  {
    fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), _O_RDONLY);
    fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), _O_WRONLY);
    return 0;
  }
  errno = GetLastError();
  return -1;
}

// POSIX pipe() emulation with inherited pipe handles for child processes (Windows specific)
inline int pipe_inherit(int fd[2])
{
  HANDLE pipe_r = NULL;
  HANDLE pipe_w = NULL;
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(SECURITY_ATTRIBUTES));
  sa.nLength = sizeof(SECURITY_ATTRIBUTES); 
  sa.bInheritHandle = TRUE; 
  sa.lpSecurityDescriptor = NULL; 
  if (CreatePipe(&pipe_r, &pipe_w, &sa, 0))
  {
    fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), _O_RDONLY);
    fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), _O_WRONLY);
    if (SetHandleInformation(reinterpret_cast<HANDLE>(_get_osfhandle(fd[0])), HANDLE_FLAG_INHERIT, 0))
      return 0;
    close(fd[0]);
    close(fd[1]);
  }
  errno = GetLastError();
  return -1;
}

// POSIX popen()
inline FILE *popen(const char *command, const char *mode)
{
  return _popen(command, mode);
}

// POSIX pclose()
inline int pclose(FILE *stream)
{
  return _pclose(stream);
}

// wrap Windows _dupenv_s()
inline int dupenv_s(char **ptr, const char *name)
{
  size_t len;
  return _dupenv_s(ptr, &len, name);
}

// convert a wide Unicode string to an UTF-8 string
inline std::string utf8_encode(const std::wstring &wstr)
{
  if (wstr.empty())
    return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
  std::string str;
  if (size >= 0)
  {
    str.resize(size);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &str[0], size, NULL, NULL);
  }
  return str;
}

// convert a UTF-8 string to a wide Unicode String
inline std::wstring utf8_decode(const std::string &str)
{
  if (str.empty())
    return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), NULL, 0);
  std::wstring wstr;
  if (size >= 0)
  {
    wstr.resize(size);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &wstr[0], size);
  }
  return wstr;
}

// POSIX chdir()
inline int chdir(const char *path)
{
  return _wchdir(utf8_decode(path).c_str());
}

// POSIX getcwd() - but without buffer argument
inline char *getcwd0()
{
  wchar_t *wcwd = _wgetcwd(NULL, 0);
  if (wcwd == NULL)
    return NULL;
  std::string cwd(utf8_encode(wcwd));
  free(wcwd);
  return strdup(cwd.c_str());
}

// open UTF-8 encoded Unicode filename
inline int fopenw_s(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;
  std::wstring wfilename(utf8_decode(filename));
  HANDLE hFile;
  if (strchr(mode, 'a') == NULL && strchr(mode, 'w') == NULL)
    hFile = CreateFileW(wfilename.c_str(), (strchr(mode, '+') == NULL ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  else if (strchr(mode, 'a') == NULL)
    hFile = CreateFileW(wfilename.c_str(), (strchr(mode, '+') == NULL ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  else
    hFile = CreateFileW(wfilename.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return errno = (GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT);
  int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), _O_RDONLY);
  if (fd == -1)
  { 
    CloseHandle(hFile);
    return errno = EINVAL; 
  }
  *file = _fdopen(fd, mode);
  if (*file == NULL)
  {
    _close(fd);
    return errno ? errno : (errno = EINVAL);
  }
  return 0;
}

#else // not compiling for a windows OS

#define _FILE_OFFSET_BITS 64

#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif

#ifdef HAVE_SCHED_H
# include <sched.h>
#endif

#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef HAVE_SYS_MOUNT_H
# include <sys/mount.h>
#endif

#ifdef HAVE_SYS_CPUSET_H
# include <sys/cpuset.h>
#endif

#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif

#if defined(HAVE_F_RDAHEAD) || defined(HAVE_O_NOATIME)
# include <fcntl.h>
# ifndef O_NOATIME
#  define O_NOATIME 0
# endif
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"
#define NEWLINESTR "\n" // Note: Also hard-coded into Output class.

// Windows-like dupenv_s()
inline int dupenv_s(char **ptr, const char *name)
{
  if (ptr == NULL)
    return EINVAL;
  *ptr = NULL;
  const char *env = getenv(name);
  if (env != NULL && (*ptr = strdup(env)) == NULL)
    return ENOMEM;
  return 0;
}

// Open UTF-8 encoded Unicode filename
inline int fopenw_s(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;
#if defined(HAVE_F_RDAHEAD)
  if (strchr(mode, 'a') == NULL && strchr(mode, 'w') == NULL)
  {
    // removed O_NOATIME which may fail
#if defined(O_NOCTTY)
    int fd = open(filename, O_RDONLY | O_NOCTTY);
#else
    int fd = open(filename, O_RDONLY);
#endif
    if (fd < 0)
      return errno;
    fcntl(fd, F_RDAHEAD, 1);
    return (*file = fdopen(fd, mode)) == NULL ? errno ? errno : (errno = EINVAL) : 0;
  }
#endif
  return (*file = fopen(filename, mode)) == NULL ? errno ? errno : (errno = EINVAL) : 0;
}

// POSIX getcwd() - but without buffer argument
inline char *getcwd0()
{
  return getcwd(NULL, 0);
}

#endif

// UTF-8 multibyte string length (number of UTF-8-encoded Unicode characters) without validity checking
inline size_t utf8len(const char *s)
{
  size_t k = 0;
  while (*s != '\0')
    k += (static_cast<uint8_t>(*s++ & 0xc0) != 0x80);
  return k;
}

// UTF-8 multibyte string length (number of UTF-8-encoded Unicode characters) without validity checking
inline size_t utf8nlen(const char *s, size_t n)
{
  size_t k = 0;
  while (n-- > 0)
    k += (static_cast<uint8_t>(*s++ & 0xc0) != 0x80);
  return k;
}

// UTF-8 multibyte string advance k UTF-8-encoded characters or until \0
inline const char *utf8skip(const char *s, size_t k)
{
  while (*s != '\0' && k > 0)
    k -= (static_cast<uint8_t>(*++s & 0xc0) != 0x80);
  return s;
}

// UTF-8 multibyte string of length n advance k UTF-8-encoded characters
inline const char *utf8skipn(const char *s, size_t n, size_t k)
{
  while (n-- > 0 && k > 0)
    k -= (static_cast<uint8_t>(*++s & 0xc0) != 0x80);
  return s;
}

// platform -- see configure.ac
#if !defined(PLATFORM)
# if defined(OS_WIN)
#  if defined(_WIN64)
#   define PLATFORM "WIN64"
#  elif defined(_WIN32)
#   define PLATFORM "WIN32"
#  else
#   define PLATFORM "WIN"
#  endif
# else
#  define PLATFORM ""
# endif
#endif

// the default GREP_COLORS
#ifndef DEFAULT_GREP_COLORS
# ifdef OS_WIN
#  define DEFAULT_GREP_COLORS "sl=1;37:cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36:qp=1;32:qe=1;37;41:qr=1;37:qm=1;32:ql=36:qb=1;35"
# else
#  define DEFAULT_GREP_COLORS "cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35"
# endif
#endif

// the default --tabs
#ifndef DEFAULT_TABS
# define DEFAULT_TABS 8
#endif

// the default --tag
#ifndef DEFAULT_TAG
# define DEFAULT_TAG "___"
#endif

// the default pager command when --pager is used
#ifndef DEFAULT_PAGER_COMMAND
# ifdef OS_WIN
#  define DEFAULT_PAGER_COMMAND "more"
# else
#  define DEFAULT_PAGER_COMMAND "less"
# endif
#endif

// the default -Q UI view command when --view is used and PAGER or EDITOR are not set
#ifndef DEFAULT_VIEW_COMMAND
# ifdef OS_WIN
#  define DEFAULT_VIEW_COMMAND "more"
# else
#  define DEFAULT_VIEW_COMMAND "less"
# endif
#endif

// the default ignore file
#ifndef DEFAULT_IGNORE_FILE
# define DEFAULT_IGNORE_FILE ".gitignore"
#endif

// color is disabled by default, unless enabled with WITH_COLOR
#ifdef WITH_COLOR
# define DEFAULT_COLOR Static::AUTO
#else
# define DEFAULT_COLOR NULL
#endif

// pager is disabled by default, unless enabled with WITH_PAGER
#ifdef WITH_PAGER
# define DEFAULT_PAGER DEFAULT_PAGER_COMMAND
#else
# define DEFAULT_PAGER NULL
#endif

// default --mmap
#define DEFAULT_MIN_MMAP_SIZE MIN_MMAP_SIZE

// default --max-mmap: mmap is disabled by default with WITH_NO_MMAP
#ifdef WITH_NO_MMAP
# define DEFAULT_MAX_MMAP_SIZE 0
#else
# define DEFAULT_MAX_MMAP_SIZE MAX_MMAP_SIZE
#endif

// pretty is disabled by default for ugrep (but always enabled by ug), unless enabled with WITH_PRETTY
#ifdef WITH_PRETTY
# define DEFAULT_PRETTY Static::AUTO
#else
# define DEFAULT_PRETTY NULL
#endif

// hidden file and directory search is enabled by default, unless disabled with WITH_HIDDEN
#ifdef WITH_HIDDEN
# define DEFAULT_HIDDEN true
#else
# define DEFAULT_HIDDEN false
#endif

// do not confirm query UI actions
#ifdef WITH_NO_CONFIRM
# define DEFAULT_CONFIRM false
#else
# define DEFAULT_CONFIRM true
#endif

#include "flag.hpp"
#include "cnf.hpp"
#include <reflex/absmatcher.h>
#include <reflex/pattern.h>
#include <reflex/input.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <string>
#include <vector>

// undefined size_t value
#define UNDEFINED_SIZE static_cast<size_t>(~0UL)

// managed global static state
struct Static {

  // CNF of AND/OR/NOT matchers
  typedef std::list<std::list<std::unique_ptr<reflex::AbstractMatcher>>> Matchers;

  // the CNF of Boolean search queries and patterns
  static CNF bcnf;

  // pointer to the --index pattern DFA with HFA constructed before threads start
  static const reflex::Pattern *index_pattern; // concurrent access is thread safe

  // the -M MAGIC pattern DFA constructed before threads start, read-only afterwards
  static reflex::Pattern magic_pattern; // concurrent access is thread safe
  static reflex::Matcher magic_matcher; // concurrent access is not thread safe

  // the --filter-magic-label pattern DFA
  static reflex::Pattern filter_magic_pattern; // concurrent access is thread safe

  // unique address and label to identify standard input path
  static const char *LABEL_STANDARD_INPUT;

  // unique address to identify color and pretty WHEN arguments
  static const char *NEVER;
  static const char *ALWAYS;
  static const char *AUTO;

  // ugrep command-line arguments pointing to argv[]
  static const char *arg_pattern;
  static std::vector<const char*> arg_files;

  // number of cores
  static size_t cores;

  // number of concurrent threads for workers
  static size_t threads;

  // number of warnings given
  static std::atomic_size_t warnings;

  // redirectable source is standard input by default or a pipe
  static FILE *source;

  // redirectable output destination is standard output by default or a pipe
  static FILE *output;

  // redirectable error output destination is standard error by default or a pipe
  static FILE *errout;

  // full home directory path or NULL to expand ~ in options with path arguments
  static const char *home_dir;

  // Grep object handle, to cancel the search with cancel_ugrep()
  static struct Grep *grep_handle;
  static std::mutex grep_handle_mutex;

  // set/clear the handle to use cancel_ugrep()
  static void set_grep_handle(struct Grep*);
  static void clear_grep_handle();

  // graciously shut down ugrep() if still running as a thread
  static void cancel_ugrep();

  // patterns
  static reflex::Pattern reflex_pattern;
  static std::string string_pattern;
  static std::list<reflex::Pattern> reflex_patterns;
  static std::list<std::string> string_patterns;

  // the LineMatcher, PCRE2Matcher, FuzzyMatcher or Matcher, concurrent access is not thread safe
  static std::unique_ptr<reflex::AbstractMatcher> matcher;

  // the CNF of AND/OR/NOT matchers or NULL, concurrent access is not thread safe
  static Matchers matchers;

  // clone the CNF of AND/OR/NOT matchers - the caller is responsible to deallocate the returned list of matchers if not NULL
  static Matchers *matchers_clone()
  {
    Matchers *new_matchers = new Matchers;

    for (const auto& i : matchers)
    {
      new_matchers->emplace_back();

      auto& last = new_matchers->back();

      for (const auto& j : i)
      {
        if (j)
          last.emplace_back(j->clone());
        else
          last.emplace_back();
      }
    }

    return new_matchers;
  }

};

// ANSI SGR substrings extracted from GREP_COLORS and --colors
#define COLORLEN 32
extern char color_sl[COLORLEN]; // selected line
extern char color_cx[COLORLEN]; // context line
extern char color_mt[COLORLEN]; // matched text in any matched line
extern char color_ms[COLORLEN]; // matched text in a selected line
extern char color_mc[COLORLEN]; // matched text in a context line
extern char color_fn[COLORLEN]; // file name
extern char color_ln[COLORLEN]; // line number
extern char color_cn[COLORLEN]; // column number
extern char color_bn[COLORLEN]; // byte offset
extern char color_se[COLORLEN]; // separator

extern char color_qp[COLORLEN]; // TUI prompt
extern char color_qe[COLORLEN]; // TUI errors
extern char color_qr[COLORLEN]; // TUI regex highlight
extern char color_qm[COLORLEN]; // TUI regex meta symbols highlight
extern char color_ql[COLORLEN]; // TUI regex bracket list highlight
extern char color_qb[COLORLEN]; // TUI regex brace highlight

extern char match_ms[COLORLEN];  // --match or --tag: matched text in a selected line
extern char match_mc[COLORLEN];  // --match or --tag: matched text in a context line
extern char match_off[COLORLEN]; // --match or --tag: off

extern std::string color_wd; // hyperlink working directory path

extern const char *color_hl; // hyperlink
extern const char *color_st; // ST

extern const char *color_off; // disable colors
extern const char *color_del; // erase line after the cursor

// --encoding file format/encoding table
struct Encoding {
  const char                       *format;
  reflex::Input::file_encoding_type encoding;
};

// table of RE/flex file encodings for option --encoding
extern const Encoding encoding_table[];

// -t, --file-type type table
struct Type {
  const char *type;
  const char *extensions;
  const char *filenames;
  const char *magic;
};

// table of file types for option -t, --file-type
extern const Type type_table[];

// check TTY availability and set colors
extern void terminal();

// set or update hyperlink with host and current working directory, e.g. when changed
extern void set_terminal_hyperlink();

// search the specified files, directories, and/or standard input for pattern matches, may throw an exception
extern void ugrep();

// perform a limited ugrep search on a single file with optional archive part and store up to num results in a vector, may throw an exception
extern void ugrep_find_text_preview(const char *filename, const char *partname, size_t from_lineno, size_t max, size_t& lineno, size_t& num, std::vector<std::string>& text);

// extract a part from an archive and send to a stream
extern void ugrep_extract(const char *filename, const char *partname, FILE *output);

extern void warning(const char *message, const char *arg);
extern void error(const char *message, const char *arg);
extern void abort(const char *message);
extern void abort(const char *message, const std::string& what);

#endif
