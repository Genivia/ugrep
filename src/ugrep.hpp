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
@brief     Universal grep - a pattern search utility written in C++11
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
    fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), 0);
    fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), 0);
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

#else

// not compiling for a windows OS

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_MMAP
# include <sys/mman.h>
#endif

#ifdef HAVE_SYS_STATVFS_H
# include <sys/statvfs.h>
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

// Windows-compatible fopen_s()
inline int fopen_s(FILE **file, const char *filename, const char *mode)
{
  return (*file = fopen(filename, mode)) == NULL ? errno : 0;
}

#endif

// platform -- see configure.ac
#if !defined(PLATFORM)
# if defined(OS_WIN)
#  if defined(_WIN32)
#   define PLATFORM "WIN32"
#  elif defined(_WIN64)
#   define PLATFORM "WIN64"
#  else
#   define PLATFORM "WIN"
#  endif
# else
#  define PLATFORM ""
# endif
#endif

#include <reflex/input.h>
#include <string>
#include <vector>

// --sort=KEY is n/a or by name, size, used time, changed time, created time
enum class Sort { NA, NAME, SIZE, USED, CHANGED, CREATED };

// -D, --devices and -d, --directories
enum class Action { SKIP, READ, RECURSE };

// ugrep command-line options
extern bool flag_with_filename;
extern bool flag_no_filename;
extern bool flag_no_header;
extern bool flag_no_messages;
extern bool flag_match;
extern bool flag_count;
extern bool flag_fixed_strings;
extern bool flag_free_space;
extern bool flag_ignore_case;
extern bool flag_smart_case;
extern bool flag_invert_match;
extern bool flag_line_number;
extern bool flag_only_line_number;
extern bool flag_column_number;
extern bool flag_byte_offset;
extern bool flag_line_buffered;
extern bool flag_only_matching;
extern bool flag_ungroup;
extern bool flag_quiet;
extern bool flag_files_with_match;
extern bool flag_files_without_match;
extern bool flag_null;
extern bool flag_basic_regexp;
extern bool flag_perl_regexp;
extern bool flag_word_regexp;
extern bool flag_line_regexp;
extern bool flag_dereference;
extern bool flag_no_dereference;
extern bool flag_binary;
extern bool flag_binary_without_matches;
extern bool flag_text;
extern bool flag_hex;
extern bool flag_with_hex;
extern bool flag_empty;
extern bool flag_initial_tab;
extern bool flag_decompress;
extern bool flag_any_line;
extern bool flag_heading;
extern bool flag_break;
extern bool flag_stats;
extern bool flag_cpp;
extern bool flag_csv;
extern bool flag_json;
extern bool flag_xml;
extern bool flag_stdin;
extern bool flag_all_threads;
extern bool flag_pretty;
extern bool flag_no_hidden;
extern bool flag_hex_hbr;
extern bool flag_hex_cbr;
extern bool flag_hex_chr;
extern bool flag_sort_rev;
extern Sort flag_sort_key;
extern Action flag_devices_action;
extern Action flag_directories_action;
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
extern size_t flag_tabs;
extern size_t flag_hex_columns;
extern size_t flag_max_mmap;
extern size_t flag_min_steal;
extern const char *flag_pager;
extern const char *flag_color;
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
extern const char *flag_devices;
extern const char *flag_directories;
extern const char *flag_label;
extern const char *flag_separator;
extern const char *flag_group_separator;
extern const char *flag_binary_files;
extern std::vector<std::string> flag_regexp;
extern std::vector<std::string> flag_neg_regexp;
extern std::vector<std::string> flag_file;
extern std::vector<std::string> flag_file_types;
extern std::vector<std::string> flag_file_extensions;
extern std::vector<std::string> flag_file_magic;
extern std::vector<std::string> flag_glob;
extern std::vector<std::string> flag_ignore_files;
extern std::vector<std::string> flag_include;
extern std::vector<std::string> flag_include_dir;
extern std::vector<std::string> flag_include_from;
extern std::vector<std::string> flag_include_fs;
extern std::vector<std::string> flag_not_include;
extern std::vector<std::string> flag_not_include_dir;
extern std::vector<std::string> flag_exclude;
extern std::vector<std::string> flag_exclude_dir;
extern std::vector<std::string> flag_exclude_from;
extern std::vector<std::string> flag_exclude_fs;
extern std::vector<std::string> flag_not_exclude;
extern std::vector<std::string> flag_not_exclude_dir;
extern reflex::Input::file_encoding_type flag_encoding_type;

// ugrep command-line arguments pointing to argv[]
extern const char *arg_pattern;
extern std::vector<const char*> arg_files;

// redirectable source is standard input by default or a pipe
extern FILE *source;

// redirectable output destination is standard output by default or a pipe
extern FILE *output;

// check TTY availability and set colors
extern void terminal();

// perform a ugrep search given the specified command line flags, patterns, and files
extern void ugrep();

extern void warning(const char *message, const char *arg);
extern void error(const char *message, const char *arg);
extern void abort(const char *message);
extern void abort(const char *message, const std::string& what);

#endif
