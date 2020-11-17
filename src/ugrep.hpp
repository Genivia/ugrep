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
@brief     a pattern search utility written in C++11
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef UGREP_HPP
#define UGREP_HPP

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

#ifdef OS_WIN

// disable min/max macros to use std::min and std::max
#define NOMINMAX

#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <io.h>
#include <strsafe.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <string>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"

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

// Wrap Windows _dupenv_s()
inline int dupenv_s(char **ptr, const char *name)
{
  size_t len;
  return _dupenv_s(ptr, &len, name);
}

// Convert a wide Unicode string to an UTF-8 string
inline std::string utf8_encode(const std::wstring &wstr)
{
  if (wstr.empty())
    return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
  std::string str(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &str[0], size, NULL, NULL);
  return str;
}

// Convert a UTF-8 string to a wide Unicode String
inline std::wstring utf8_decode(const std::string &str)
{
  if (str.empty())
    return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), NULL, 0);
  std::wstring wstr(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &wstr[0], size);
  return wstr;
}

// Open Unicode wide string UTF-8 encoded filename
inline int fopenw_s(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;

  std::wstring wfilename = utf8_decode(filename);
  HANDLE hFile = CreateFileW(wfilename.c_str(), GENERIC_READ, FILE_SHARE_READ, SECURITY_ANONYMOUS, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

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
    return errno;
  }

  return 0;
}

#else

// not compiling for a windows OS

#define _FILE_OFFSET_BITS 64

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
#endif

#if defined(HAVE_F_RDAHEAD) || defined(HAVE_O_NOATIME)
# include <fcntl.h>
# ifndef O_NOATIME
#  define O_NOATIME 0
# endif
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"

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

// Open Unicode wide string UTF-8 encoded filename
inline int fopenw_s(FILE **file, const char *filename, const char *mode)
{
#if defined(HAVE_F_RDAHEAD)
  if (strchr(mode, 'a') == NULL && strchr(mode, 'w') == NULL)
  {
    int fd = open(filename, O_RDONLY); // removed O_NOATIME which may fail
    if (fd < 0)
      return errno;
    fcntl(fd, F_RDAHEAD, 1);
    return (*file = fdopen(fd, mode)) == NULL ? errno : 0;
  }
#endif
  return (*file = fopen(filename, mode)) == NULL ? errno : 0;
}

#endif

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

#include <reflex/input.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

// undefined size_t value
#define UNDEFINED_SIZE static_cast<size_t>(~0UL)

// --sort=KEY is n/a or by name, score, size, used time, changed time, created time
enum class Sort { NA, NAME, BEST, SIZE, USED, CHANGED, CREATED };

// -D, --devices and -d, --directories
enum class Action { SKIP, READ, RECURSE };

// three-valued logic flags that behave as bool; this allows us to check if a flag was undefined (default) and explicitly enabled/disabled
class Flag {

 public:

  Flag()                     : value(UNDEFINED)  { }
  Flag(bool flag)            : value(flag ? T : F)  { }
  Flag& operator=(bool flag) { value = flag ? T : F; return *this; }
       operator bool() const { return is_true(); }
  bool is_undefined()  const { return value == UNDEFINED; }
  bool is_defined()    const { return value != UNDEFINED; }
  bool is_false()      const { return value == F; }
  bool is_true()       const { return value == T; }

 private:

  enum { UNDEFINED = -1, F = 0, T = 1 } value;

};

// ugrep command-line options
extern Flag flag_no_messages;
extern Flag flag_match;
extern Flag flag_count;
extern Flag flag_fixed_strings;
extern Flag flag_free_space;
extern Flag flag_ignore_case;
extern Flag flag_smart_case;
extern Flag flag_invert_match;
extern Flag flag_only_line_number;
extern Flag flag_with_filename;
extern Flag flag_no_filename;
extern Flag flag_line_number;
extern Flag flag_column_number;
extern Flag flag_byte_offset;
extern Flag flag_initial_tab;
extern Flag flag_line_buffered;
extern Flag flag_only_matching;
extern Flag flag_ungroup;
extern Flag flag_quiet;
extern Flag flag_files_with_matches;
extern Flag flag_files_without_match;
extern Flag flag_null;
extern Flag flag_basic_regexp;
extern Flag flag_perl_regexp;
extern Flag flag_word_regexp;
extern Flag flag_line_regexp;
extern Flag flag_dereference;
extern Flag flag_no_dereference;
extern Flag flag_binary;
extern Flag flag_binary_without_match;
extern Flag flag_text;
extern Flag flag_hex;
extern Flag flag_with_hex;
extern Flag flag_empty;
extern Flag flag_decompress;
extern Flag flag_any_line;
extern Flag flag_heading;
extern Flag flag_break;
extern Flag flag_cpp;
extern Flag flag_csv;
extern Flag flag_json;
extern Flag flag_xml;
extern bool flag_pretty;
extern bool flag_hidden;
extern bool flag_confirm;
extern bool flag_stdin;
extern bool flag_all_threads;
extern bool flag_no_header;
extern bool flag_hex_hbr;
extern bool flag_hex_cbr;
extern bool flag_hex_chr;
extern bool flag_sort_rev;
extern Sort flag_sort_key;
extern Action flag_devices_action;
extern Action flag_directories_action;
extern size_t flag_fuzzy;
extern size_t flag_query;
extern size_t flag_after_context;
extern size_t flag_before_context;
extern size_t flag_min_depth;
extern size_t flag_max_depth;
extern size_t flag_max_count;
extern size_t flag_max_files;
extern size_t flag_min_line;
extern size_t flag_max_line;
extern size_t flag_not_magic;
extern size_t flag_min_magic;
extern size_t flag_jobs;
extern size_t flag_hex_columns;
extern size_t flag_tabs;
extern size_t flag_max_mmap;
extern size_t flag_min_steal;
extern const char *flag_pager;
extern const char *flag_color;
extern const char *flag_tag;
extern const char *flag_apply_color;
extern const char *flag_hexdump;
extern const char *flag_colors;
extern const char *flag_encoding;
extern const char *flag_filter;
extern const char *flag_format;
extern const char *flag_format_begin;
extern const char *flag_format_end;
extern const char *flag_format_open;
extern const char *flag_format_close;
extern const char *flag_sort;
extern const char *flag_stats;
extern const char *flag_devices;
extern const char *flag_directories;
extern const char *flag_label;
extern const char *flag_separator;
extern const char *flag_group_separator;
extern const char *flag_binary_files;
extern const char *flag_config;
extern const char *flag_save_config;
extern std::string flag_config_file;
extern std::set<std::string> flag_config_options;
extern std::vector<std::string> flag_regexp;
extern std::vector<std::string> flag_neg_regexp;
extern std::vector<std::string> flag_file;
extern std::vector<std::string> flag_file_type;
extern std::vector<std::string> flag_file_extension;
extern std::vector<std::string> flag_file_magic;
extern std::vector<std::string> flag_filter_magic_label;
extern std::vector<std::string> flag_glob;
extern std::vector<std::string> flag_ignore_files;
extern std::vector<std::string> flag_include;
extern std::vector<std::string> flag_include_dir;
extern std::vector<std::string> flag_include_from;
extern std::vector<std::string> flag_include_fs;
extern std::vector<std::string> flag_exclude;
extern std::vector<std::string> flag_exclude_dir;
extern std::vector<std::string> flag_exclude_from;
extern std::vector<std::string> flag_exclude_fs;
extern std::vector<std::string> flag_all_include;
extern std::vector<std::string> flag_all_include_dir;
extern std::vector<std::string> flag_all_exclude;
extern std::vector<std::string> flag_all_exclude_dir;
extern reflex::Input::file_encoding_type flag_encoding_type;

// ugrep command-line arguments pointing to argv[]
extern const char *arg_pattern;
extern std::vector<const char*> arg_files;

// number of concurrent threads for workers
extern size_t threads;

// number of warnings given
extern std::atomic_size_t warnings;

// redirectable source is standard input by default or a pipe
extern FILE *source;

// redirectable output destination is standard output by default or a pipe
extern FILE *output;

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

extern char match_ms[COLORLEN];  // --match or --tag: matched text in a selected line
extern char match_mc[COLORLEN];  // --match or --tag: matched text in a context line
extern char match_off[COLORLEN]; // --match or --tag: off

extern const char *color_off; // disable colors
extern const char *color_del; // erase line after the cursor

// --encoding file format/encoding table
struct Encoding {
  const char                       *format;
  reflex::Input::file_encoding_type encoding;
};

// table of RE/flex file encodings for option --encoding
extern const Encoding encoding_tablep[];

// -t, --file-type type table
struct Type {
  const char *type;
  const char *extensions;
  const char *magic; 
};

// table of file types for option -t, --file-type
extern const Type type_table[];

// check TTY availability and set colors
extern void terminal();

// perform a ugrep search given the specified command line flags, patterns, and files
extern void ugrep();

// graciously shut down ugrep() if still running as a thread
extern void cancel_ugrep();

extern void warning(const char *message, const char *arg);
extern void error(const char *message, const char *arg);
extern void abort(const char *message);
extern void abort(const char *message, const std::string& what);

#endif
