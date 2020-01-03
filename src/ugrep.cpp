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
@file      ugrep.cpp
@brief     Universal grep - a pattern search utility written in C++11
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

For download and installation instructions:

  https://github.com/Genivia/ugrep

This program uses RE/flex:

  https://github.com/Genivia/RE-flex

Optional libraries to support options -P and -z:

  Boost.Regex
  zlib
  libbz2
  liblzma

Build ugrep as follows:

  ./configure
  make

Github does not preserve time stamps so ./configure may fail, in that case do:

  ./autoreconf -fi
  ./configure
  make

Install as follows:

  ./configure
  make
  sudo make install

Prebuilt executables are located in ugrep/bin.

*/

// ugrep version
#define UGREP_VERSION "1.6.12"

#include <reflex/input.h>
#include <reflex/matcher.h>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <limits>
#include <algorithm>
#include <functional>
#include <list>
#include <queue>
#include <set>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

// compiling for a windows OS, except Cygwin and MinGW

#ifdef OS_WIN

// optionally enable --color=auto by default
// #define WITH_COLOR

// optionally enable Boost.Regex for -P
// #define HAVE_BOOST_REGEX

// optionally enable zlib for -z
// #define HAVE_LIBZ

// optionally enable libbz2 for -z
// #define HAVE_LIBBZ2

// optionally enable liblzma for -z
// #define HAVE_LIBLZMA

// because colors are dim in the Windows command window, use bold & bright except for context lines and separators
#define DEFAULT_GREP_COLORS "sl=1;37:cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36"

// disable min/max macros to use std::min and std::max
#define NOMINMAX

#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <io.h>
#include <strsafe.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"

// POSIX read() and write() return type is ssize_t
typedef int ssize_t;

// POSIX pipe() emulation
int pipe(int fd[2])
{
  HANDLE pipe_r = NULL;
  HANDLE pipe_w = NULL;
  if (CreatePipe(&pipe_r, &pipe_w, NULL, 0))
  {
    fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), 0);
    fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), 0);
    return 0;
  }
  return 1;
}

#else

// not compiling for a windows OS

#include <dirent.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"

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

// fast gitignore-style glob matching
#include "glob.hpp"

// use Boost.Regex for option -P
#ifdef HAVE_BOOST_REGEX
#include <reflex/boostmatcher.h>
#endif

// use zlib, libbz2, liblzma for option -z
#ifdef HAVE_LIBZ
#include "zstream.hpp"
#endif

// use a task-parallel thread to decompress the stream into a pipe to search, increases speed on most systems
#define WITH_DECOMPRESSION_THREAD

// optional: specify an optimal decompression block size, default is 65536, must be larger than 1024 for tar extraction
// #define Z_BUF_LEN 16384
// #define Z_BUF_LEN 32768

// the default GREP_COLORS
#ifndef DEFAULT_GREP_COLORS
#define DEFAULT_GREP_COLORS "cx=33:mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36"
#endif

// the default pager when --pager is used
#ifndef DEFAULT_PAGER
#define DEFAULT_PAGER "less -R"
#endif

// the default ignore file
#ifndef DEFAULT_IGNORE_FILE
#define DEFAULT_IGNORE_FILE ".gitignore"
#endif

// color is disabled by default, unless enabled with WITH_COLOR
#ifdef WITH_COLOR
#define FLAG_COLOR "auto"
#else
#define FLAG_COLOR NULL
#endif

// pager is disabled by default, unless enabled with WITH_PAGER
#ifdef WITH_PAGER
#define FLAG_PAGER DEFAULT_PAGER
#else
#define FLAG_PAGER NULL
#endif

// ugrep exit codes
#define EXIT_OK    0 // One or more lines were selected
#define EXIT_FAIL  1 // No lines were selected
#define EXIT_ERROR 2 // An error occurred

// limit the total number of threads spawn (i.e. limit spawn overhead), because grepping is practically IO bound
#ifndef MAX_JOBS
#define MAX_JOBS 16U
#endif

// a hard limit on the recursive search depth
// TODO use iteration and a stack for virtually unlimited recursion depth, but it is important to have a hard limit
#ifndef MAX_DEPTH
#define MAX_DEPTH 100
#endif

// --min-steal default, the minimum co-worker's queue size of pending jobs to steal a job from, smaller values result in higher job stealing rates, should not be less than 3
#ifndef MIN_STEAL
#define MIN_STEAL 3U
#endif

// --min-mmap and --max-mmap file size to allocate with mmap(), not greater than 4294967295LL, max 0 disables mmap()
#ifndef MIN_MMAP_SIZE
#define MIN_MMAP_SIZE 16384LL
#endif
#ifndef MAX_MMAP_SIZE
#define MAX_MMAP_SIZE 2147483648LL
#endif

// default --min-mmap
#define DEFAULT_MIN_MMAP_SIZE MIN_MMAP_SIZE

// default --max-mmap: mmap is enabled by default, unless disabled with WITH_NO_MMAP
#ifdef WITH_NO_MMAP
#define DEFAULT_MAX_MMAP_SIZE 0
#else
#define DEFAULT_MAX_MMAP_SIZE MAX_MMAP_SIZE
#endif

// hidden file and directory search is enabled by default, unless disabled with WITH_NO_HIDDEN
#ifdef WITH_NO_HIDDEN
#define DEFAULT_HIDDEN true
#else
#define DEFAULT_HIDDEN false
#endif

// use dirent d_type when available
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
#define DIRENT_TYPE_UNKNOWN DT_UNKNOWN
#define DIRENT_TYPE_LNK     DT_LNK
#define DIRENT_TYPE_DIR     DT_DIR
#define DIRENT_TYPE_REG     DT_REG
#else
#define DIRENT_TYPE_UNKNOWN 0
#define DIRENT_TYPE_LNK     1
#define DIRENT_TYPE_DIR     1
#define DIRENT_TYPE_REG     1
#endif

// undefined size_t value
#define UNDEFINED static_cast<size_t>(~0UL)

// the -M MAGIC pattern (compiled, read-only)
reflex::Pattern magic_pattern;

// number of concurrent threads for workers
size_t threads;

// TTY detected
bool tty_term = false;

// color term detected
bool color_term = false;

// ANSI SGR substrings extracted from GREP_COLORS
#define COLORLEN 16
char color_sl[COLORLEN]; // selected line
char color_cx[COLORLEN]; // context line
char color_mt[COLORLEN]; // matched text in any matched line
char color_ms[COLORLEN]; // matched text in a selected line
char color_mc[COLORLEN]; // matched text in a context line
char color_fn[COLORLEN]; // file name
char color_ln[COLORLEN]; // line number
char color_cn[COLORLEN]; // column number
char color_bn[COLORLEN]; // byte offset
char color_se[COLORLEN]; // separator

const char *color_off     = ""; // disable colors

const char *color_high    = ""; // stderr highlighted text
const char *color_error   = ""; // stderr error text
const char *color_warning = ""; // stderr warning text
const char *color_message = ""; // stderr error or warning message text

// default file encoding is plain (no conversion), but detect UTF-8/16/32 automatically
reflex::Input::file_encoding_type flag_encoding_type = reflex::Input::file_encoding::plain;

// -D, --devices and -d, --directories
enum { READ, RECURSE, SKIP } flag_devices_action, flag_directories_action;

// output destination is standard output by default or a pipe to --pager
FILE *output = stdout;

#ifndef OS_WIN

// output file stat is available when stat() result is true
bool output_stat_result = false;
bool output_stat_regular = false;
struct stat output_stat;

// container of inodes to detect directory cycles when symlinks are traversed with --dereference
std::set<ino_t> visited;

#endif

// ugrep command-line options
bool flag_with_filename            = false;
bool flag_no_filename              = false;
bool flag_no_header                = false;
bool flag_no_messages              = false;
bool flag_match                    = false;
bool flag_count                    = false;
bool flag_fixed_strings            = false;
bool flag_free_space               = false;
bool flag_ignore_case              = false;
bool flag_smart_case               = false;
bool flag_invert_match             = false;
bool flag_line_number              = false;
bool flag_only_line_number         = false;
bool flag_column_number            = false;
bool flag_byte_offset              = false;
bool flag_line_buffered            = false;
bool flag_only_matching            = false;
bool flag_ungroup                  = false;
bool flag_quiet                    = false;
bool flag_files_with_match         = false;
bool flag_files_without_match      = false;
bool flag_null                     = false;
bool flag_basic_regexp             = false;
bool flag_perl_regexp              = false;
bool flag_word_regexp              = false;
bool flag_line_regexp              = false;
bool flag_dereference              = false;
bool flag_no_dereference           = false;
bool flag_binary                   = false;
bool flag_binary_without_matches   = false;
bool flag_text                     = false;
bool flag_hex                      = false;
bool flag_with_hex                 = false;
bool flag_empty                    = false;
bool flag_initial_tab              = false;
bool flag_decompress               = false;
bool flag_any_line                 = false;
bool flag_break                    = false;
bool flag_stats                    = false;
bool flag_cpp                      = false;
bool flag_csv                      = false;
bool flag_json                     = false;
bool flag_xml                      = false;
bool flag_stdin                    = false;
bool flag_no_hidden                = DEFAULT_HIDDEN;
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_max_count              = 0;
size_t flag_max_depth              = 0;
size_t flag_max_files              = 0;
size_t flag_min_line               = 0;
size_t flag_max_line               = 0;
size_t flag_jobs                   = 0;
size_t flag_tabs                   = 8;
size_t flag_min_mmap               = DEFAULT_MIN_MMAP_SIZE;
size_t flag_max_mmap               = DEFAULT_MAX_MMAP_SIZE;
size_t flag_min_steal              = MIN_STEAL;
const char *flag_pager             = FLAG_PAGER;
const char *flag_color             = FLAG_COLOR;
const char *flag_encoding          = NULL;
const char *flag_format            = NULL;
const char *flag_format_begin      = NULL;
const char *flag_format_end        = NULL;
const char *flag_format_open       = NULL;
const char *flag_format_close      = NULL;
const char *flag_devices           = "skip";
const char *flag_directories       = "read";
const char *flag_label             = "(standard input)";
const char *flag_separator         = ":";
const char *flag_group_separator   = "--";
const char *flag_binary_files      = "binary";
std::vector<std::string> flag_regexp;;
std::vector<std::string> flag_not_regexp;;
std::vector<std::string> flag_file;
std::vector<std::string> flag_file_types;
std::vector<std::string> flag_file_extensions;
std::vector<std::string> flag_file_magic;
std::vector<std::string> flag_glob;
std::vector<std::string> flag_ignore_files;
std::vector<std::string> flag_include;
std::vector<std::string> flag_include_dir;
std::vector<std::string> flag_include_from;
std::vector<std::string> flag_not_include;
std::vector<std::string> flag_not_include_dir;
std::vector<std::string> flag_exclude;
std::vector<std::string> flag_exclude_dir;
std::vector<std::string> flag_exclude_from;
std::vector<std::string> flag_not_exclude;
std::vector<std::string> flag_not_exclude_dir;

void set_color(const char *grep_colors, const char *parameter, char color[COLORLEN]);
void trim(std::string& line);
bool is_output(ino_t inode);
size_t strtopos(const char *string, const char *message);
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message);

void extend(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs, std::vector<std::string>& not_files, std::vector<std::string>& not_dirs);
void format(const char *format, size_t matches);
void help(const char *message = NULL, const char *arg = NULL);
void version();
void is_directory(const char *pathname);
void cannot_decompress(const char *pathname, const char *message);
void warning(const char *message, const char *arg);
void error(const char *message, const char *arg);
void abort(const char *message, const std::string& what);

// read a line from buffered input, returns true when eof
inline bool getline(reflex::BufferedInput& input, std::string& line)
{
  int ch;

  line.erase();
  while ((ch = input.get()) != EOF)
  {
    line.push_back(ch);
    if (ch == '\n')
      break;
  }
  return ch == EOF && line.empty();
}

// read a line from mmap memory, returns true when eof
inline bool getline(const char*& here, size_t& left)
{
  // read line from mmap memory
  if (left == 0)
    return true;
  const char *s = static_cast<const char*>(memchr(here, '\n', left));
  if (s == NULL)
    s = here + left;
  else
    ++s;
  left -= s - here;
  here = s;
  return false;
}

// read a line from mmap memory or from buffered input or from unbuffered input, returns true when eof
inline bool getline(const char*& here, size_t& left, reflex::BufferedInput& buffered_input, reflex::Input& input, std::string& line)
{
  if (here != NULL)
  {
    // read line from mmap memory
    if (left == 0)
      return true;
    const char *s = static_cast<const char*>(memchr(here, '\n', left));
    if (s == NULL)
      s = here + left;
    else
      ++s;
    line.assign(here, s - here);
    left -= s - here;
    here = s;
    return false;
  }

  int ch;

  line.erase();

  if (buffered_input.assigned())
  {
    // read line from buffered input
    while ((ch = buffered_input.get()) != EOF)
    {
      line.push_back(ch);
      if (ch == '\n')
        break;
    }
    return ch == EOF && line.empty();
  }

  // read line from unbuffered input
  while ((ch = input.get()) != EOF)
  {
    line.push_back(ch);
    if (ch == '\n')
      break;
  }
  return ch == EOF && line.empty();
}

// return true if s[0..n-1] contains a NUL or is non-displayable invalid UTF-8
inline bool is_binary(const char *s, size_t n)
{
  if (memchr(s, '\0', n) != NULL)
    return true;

  const char *e = s + n;

  while (s < e)
  {
    do
    {
      if ((*s & 0xc0) == 0x80)
        return true;
    } while ((*s & 0xc0) != 0xc0 && ++s < e);

    if (s >= e)
      return false;

    if (++s >= e || (*s & 0xc0) != 0x80)
      return true;

    if (++s < e && (*s & 0xc0) == 0x80)
      if (++s < e && (*s & 0xc0) == 0x80)
        if (++s < e && (*s & 0xc0) == 0x80)
          ++s;
  }

  return false;
}

// check if a file's inode is the current output file
inline bool is_output(ino_t inode)
{
#ifdef OS_WIN
  return false; // TODO check that two FILE* on Windows are the same, is this possible?
#else
  return output_stat_regular && inode == output_stat.st_ino;
#endif
}

#ifdef OS_WIN

// Wrap Windows _dupenv_s()
inline int dupenv_s(char **ptr, const char *name)
{
  size_t len;
  return _dupenv_s(ptr, &len, name);
}

#else

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

// specify a line of input for the matcher to read, matcher must not use text() or rest() to keep the line contents unmodified
inline void read_line(reflex::AbstractMatcher *matcher, const char *line, size_t size)
{
  // safe cast: buffer() is read-only if no matcher.text() and matcher.rest() are used, size + 1 to include final \0
  matcher->buffer(const_cast<char*>(line), size + 1);
}

// specify a line of input for the matcher to read, matcher must not use text() or rest() to keep the line contents unmodified
inline void read_line(reflex::AbstractMatcher *matcher, const std::string& line)
{
  // safe cast: buffer() is read-only if no matcher.text() and matcher.rest() are used, size + 1 to include final \0
  matcher->buffer(const_cast<char*>(line.c_str()), line.size() + 1);
}

// copy color buffers
inline void copy_color(char to[COLORLEN], char from[COLORLEN])
{
  memcpy(to, from, COLORLEN);
}

// collect global statistics
struct Stats {

  Stats()
    :
      files(0),
      dirs(0),
      fileno(0)
  { }

  // score a file searched
  void score_file()
  {
    ++files;
  }

  // score a directory searched
  void score_dir()
  {
    ++dirs;
  }

  // atomically update the number of matching files found, returns true if max file matches is not reached yet
  bool found()
  {
    if (flag_max_files > 0)
      return fileno.fetch_add(1, std::memory_order_relaxed) < flag_max_files;
    fileno.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // the number of matching files found
  size_t found_files()
  {
    size_t n = fileno.load(std::memory_order_relaxed);
    return flag_max_files > 0 ? std::min(n, flag_max_files) : n;
  }

  // any matching file was found
  bool found_any_file()
  {
    return fileno > 0;
  }

  // a .gitignore or similar file was encountered
  void ignore_file(const std::string& filename)
  {
    ignore.emplace_back(filename);
  }

  // report the statistics
  void report()
  {
    size_t n = found_files();
    fprintf(output, "Searched %zu file%s", files, (files == 1 ? "" : "s"));
    if (threads > 1)
      fprintf(output, " with %zu threads", threads);
    if (dirs > 0)
      fprintf(output, " in %zu director%s", dirs, (dirs == 1 ? "y" : "ies"));
    fprintf(output, ": %zu matching\n", n);
    if (!flag_file_magic.empty() ||
        !flag_include.empty() ||
        !flag_include_dir.empty() ||
        !flag_not_include.empty() ||
        !flag_not_include_dir.empty() ||
        !flag_exclude.empty() ||
        !flag_exclude_dir.empty() ||
        !flag_not_exclude.empty() ||
        !flag_not_exclude_dir.empty())
    {
      fprintf(output, "The following pathname selections and restrictions were applied:\n");
      if (flag_no_hidden)
        fprintf(output, "--no-hidden\n");
      for (auto& i : flag_ignore_files)
        fprintf(output, "--ignore-files='%s'\n", i.c_str());
      for (auto& i : ignore)
        fprintf(output, "  %s exclusions were applied to %s\n", i.c_str(), i.substr(0, i.find_last_of(PATHSEPCHR)).c_str());
      for (auto& i : flag_file_magic)
        fprintf(output, "--file-magic='%s'\n", i.c_str());
      for (auto& i : flag_include)
        fprintf(output, "--include='%s'\n", i.c_str());
      for (auto& i : flag_not_include)
        fprintf(output, "--include='!%s' (negation override)\n", i.c_str());
      for (auto& i : flag_include_dir)
        fprintf(output, "--include-dir='%s'\n", i.c_str());
      for (auto& i : flag_not_include_dir)
        fprintf(output, "--include-dir='!%s' (negation override)\n", i.c_str());
      for (auto& i : flag_exclude)
        fprintf(output, "--exclude='%s'\n", i.c_str());
      for (auto& i : flag_not_exclude)
        fprintf(output, "--exclude='!%s' (negation override)\n", i.c_str());
      for (auto& i : flag_exclude_dir)
        fprintf(output, "--exclude-dir='%s'\n", i.c_str());
      for (auto& i : flag_not_exclude_dir)
        fprintf(output, "--exclude-dir='!%s' (negation override)\n", i.c_str());
    }
  }

  size_t                   files;  // number of files searched
  size_t                   dirs;   // number of directories searched
  std::atomic_size_t       fileno; // number of matching files, atomic for GrepWorker::search() update
  std::vector<std::string> ignore; // the .gitignore files encountered in the recursive search with --ignore-files

} stats;

// mmap state
struct MMap {

  MMap()
    :
      mmap_base(NULL),
      mmap_size(0)
  { }

  ~MMap()
  {
#if defined(HAVE_MMAP) && MAX_MMAP_SIZE > 0
    if (mmap_base != NULL)
      munmap(mmap_base, mmap_size);
#endif
  }

  // attempt to mmap the given file-based input, return true if successful with base and size
  bool file(reflex::Input& input, const char*& base, size_t& size);

  void  *mmap_base; // mmap() base address
  size_t mmap_size; // mmap() allocated size

};

// attempt to mmap the given file-based input, return true if successful with base and size
bool MMap::file(reflex::Input& input, const char*& base, size_t& size)
{
  base = NULL;
  size = 0;

#if defined(HAVE_MMAP) && MAX_MMAP_SIZE > 0

  // get current input file and check if its encoding is plain
  FILE *file = input.file();
  if (file == NULL || input.file_encoding() != reflex::Input::file_encoding::plain)
    return false;

  // is this a regular file that is not too large (for size_t)?
  int fd = fileno(file);
  struct stat buf;
  if (fstat(fd, &buf) != 0 || !S_ISREG(buf.st_mode) || static_cast<unsigned long long>(buf.st_size) > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
    return false;

  // is this file not too small or too large? if -P is used, try to mmap a large file without imposing a max
  size = static_cast<size_t>(buf.st_size);
  if (size < flag_min_mmap || (size > flag_max_mmap && !flag_perl_regexp))
    return false;

  // mmap the file and round requested size up to 4K (typical page size)
  if (mmap_base == NULL || mmap_size < size)
    mmap_size = (size + 0xfff) & ~0xfffUL;

  base = static_cast<const char*>(mmap_base = mmap(mmap_base, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0));

  // mmap OK?
  if (mmap_base != MAP_FAILED)
    return true;

  // not OK
  mmap_base = NULL;
  mmap_size = 0;
  base = NULL;
  size = 0;

#else

  (void)input;

#endif

  return false;
}

// output buffering and synchronization
struct Output {

  static constexpr size_t SIZE = 16384; // size of each buffer in the buffers container

  struct Buffer { char data[SIZE]; }; // a buffer in the buffers container

  typedef std::list<Buffer> Buffers; // buffers container

  // hex dump state
  struct Dump {

    // hex dump mode for color highlighting
    static constexpr short HEX_MATCH         = 0;
    static constexpr short HEX_LINE          = 1;
    static constexpr short HEX_CONTEXT_MATCH = 2;
    static constexpr short HEX_CONTEXT_LINE  = 3;

    // hex color highlights for HEX_MATCH, HEX_LINE, HEX_CONTEXT_MATCH, HEX_CONTEXT_LINE
    static const char *color_hex[4];

    Dump(Output& out)
      :
        out(out),
        offset(0)
    {
      for (int i = 0; i < 16; ++i)
        bytes[i] = -1;
    }

    // dump matching data in hex, mode is
    void hex(short mode, size_t byte_offset, const char *data, size_t size, const char *separator);

    // next hex dump location
    void next(size_t byte_offset, const char *separator);

    // done dumping hex
    void done(const char *separator);

    // dump one line of hex
    void line(const char *separator);

    Output& out;       // reference to the output state of this hex dump state
    size_t  offset;    // current byte offset in the hex dump
    short   bytes[16]; // one line of hex dump bytes with their mode bits for color highlighting

  };

  Output(FILE *file)
    :
      lock(),
      file(file),
      lineno(0),
      dump(*this),
      eof(false)
  {
    grow();
  }

  ~Output()
  {
    if (lock != NULL)
      delete lock;
  }

  // output a character c
  void chr(int c)
  {
    if (cur >= buf->data + SIZE)
      next();
    *cur++ = c;
  }

  // output a string s
  void str(const std::string& s)
  {
    str(s.c_str(), s.size());
  }

  // output a string s
  void str(const char *s)
  {
    while (*s != '\0')
      chr(*s++);
  }

  // output a string s up to n characters
  void str(const char *s, size_t n)
  {
    while (n-- > 0)
      chr(*s++);
  }

  // output a match
  void mat(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      str(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      str(b, e - b);
    }
  }

  // output a quoted match with escapes for \ and "
  void quote(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      quote(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      quote(b, e - b);
    }
  }

  // output a match as a string in C/C++
  void cpp(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      cpp(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      cpp(b, e - b);
    }
  }

  // output a match as a quoted string in CSV
  void csv(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      csv(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      csv(b, e - b);
    }
  }

  // output a match as a quoted string in JSON
  void json(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      json(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      json(b, e - b);
    }
  }

  // output a match in XML
  void xml(reflex::AbstractMatcher *matcher)
  {
    if (flag_only_matching)
    {
      xml(matcher->begin(), matcher->size());
    }
    else
    {
      const char *e = matcher->eol(); // warning: must call eol() before bol()
      const char *b = matcher->bol();
      xml(b, e - b);
    }
  }

  // output a number with field width w (padded with spaces)
  void num(size_t i, size_t w = 1)
  {
    char tmp[24];
    size_t n = 0;

    do
      tmp[n++] = i % 10 + '0';
    while ((i /= 10) > 0);

    while (w-- > n)
      chr(' ');

    while (n > 0)
      chr(tmp[--n]);
  }

  // output a number in hex with width w (padded with digit '0')
  void hex(size_t i, size_t w = 1)
  {
    char tmp[16];
    size_t n = 0;

    do
      tmp[n++] = "0123456789abcdef"[i % 16];
    while ((i /= 16) > 0);

    while (w-- > n)
      chr('0');

    while (n > 0)
      chr(tmp[--n]);
  }

  // output a new line, flush if --line-buffered
  void nl()
  {
    chr('\n');

    if (flag_line_buffered)
      flush();
  }

  // acquire synchronization lock on the master's mutex, if not owned already
  void acquire()
  {
    // if multi-threaded and lock is not owned already, then lock on master's mutex
    if (lock != NULL && !lock->owns_lock())
      lock->lock();
  }

  // flush the buffers and acquire lock
  void flush()
  {
    // if multi-threaded and lock is not owned already, then lock on master's mutex
    acquire();

    if (!eof)
    {
      // flush the buffers container to the designated output file, pipe, or stream
      for (Buffers::iterator i = buffers.begin(); i != buf; ++i)
      {
        if (fwrite(i->data, 1, SIZE, file) < SIZE)
        {
          eof = true;
          break;
        }
      }
      if (!eof)
      {
        size_t num = cur - buf->data;
        if (fwrite(buf->data, 1, num, file) < num)
          eof = true;
        else
          fflush(file);
      }
    }

    buf = buffers.begin();
    cur = buf->data;
  }

  // next buffer, allocate one if needed (when multi-threaded and lock is owned by another thread)
  void next()
  {
    if (lock == NULL || lock->owns_lock() || lock->try_lock())
    {
      flush();
    }
    else
    {
      // allocate a new buffer if no next buffer was allocated before
      if (++buf == buffers.end())
        grow();
      cur = buf->data;
    }
  }

  // allocate a new buffer to grow the buffers container
  void grow()
  {
    buf = buffers.emplace(buffers.end());
    cur = buf->data;
  }

  // synchronize output on the given mutex
  void sync(std::mutex& mutex)
  {
    lock = new std::unique_lock<std::mutex>(mutex, std::defer_lock);
  }

  // flush and release synchronization lock on the master's mutex, if one was assigned before with sync()
  void release()
  {
    flush();

    // if multi-threaded and lock is owned, then release it
    if (lock != NULL && lock->owns_lock())
      lock->unlock();
  }

  // output the header part of the match, preceeding the matched line
  void header(const char *& pathname, const std::string& partname, size_t lineno, reflex::AbstractMatcher *matcher, size_t byte_offset, const char *sep, bool newline);

  // output "Binary file ... matches"
  void binary_file_matches(const char *pathname, const std::string& partname);

  // output formatted match with options --format, --format-open, --format-close
  void format(const char *format, const char *pathname, const std::string& partname, size_t matches, reflex::AbstractMatcher *matcher);

  // output a quoted string with escapes for \ and "
  void quote(const char *data, size_t size);

  // output quoted string in C/C++
  void cpp(const char *data, size_t size);

  // output quoted string in CSV
  void csv(const char *data, size_t size);

  // output quoted string in JSON
  void json(const char *data, size_t size);

  // output in XML
  void xml(const char *data, size_t size);

  std::unique_lock<std::mutex> *lock;    // synchronization lock
  FILE                         *file;    // output stream
  size_t                        lineno;  // last line number matched, when --format field %u (unique) is used
  Dump                          dump;    // hex dump state
  Buffers                       buffers; // buffers container
  Buffers::iterator             buf;     // current buffer in the container
  char                         *cur;     // current position in the current buffer
  bool                          eof;     // the other end closed or has an error

};

// we could use C++20 constinit, though declaring here is more efficient:
const char *Output::Dump::color_hex[4] = { color_ms, color_sl, color_mc, color_cx };

// dump matching data in hex
void Output::Dump::hex(short mode, size_t byte_offset, const char *data, size_t size, const char *separator)
{
  offset = byte_offset;
  while (size > 0)
  {
    bytes[offset++ & 0x0f] = (mode << 8) | *reinterpret_cast<const unsigned char*>(data++);
    if ((offset & 0x0f) == 0)
      line(separator);
    --size;
  }
}

// next hex dump location
void Output::Dump::next(size_t byte_offset, const char *separator)
{
  if ((offset & ~static_cast<size_t>(0x0f)) != (byte_offset & ~static_cast<size_t>(0x0f)))
    done(separator);
}

// done dumping hex
void Output::Dump::done(const char *separator)
{
  if ((offset & 0x0f) != 0)
  {
    line(separator);
    offset = (offset + 0x0f) & ~static_cast<size_t>(0x0f);
  }
}

// dump one line of hex
void Output::Dump::line(const char *separator)
{
  out.str(color_bn);
  out.hex((offset - 1) & ~static_cast<size_t>(0x0f), 8);
  out.str(color_off);
  out.str(color_se);
  out.str(separator);
  out.str(color_off);
  out.chr(' ');

  for (size_t i = 0; i < 16; ++i)
  {
    if (bytes[i] < 0)
    {
      out.str(color_cx);
      out.str(" --");
      if ((i & 7) == 7)
        out.chr(' ');
      out.str(color_off);
    }
    else
    {
      short byte = bytes[i];
      out.str(color_hex[byte >> 8]);
      out.chr(' ');
      out.hex(byte & 0xff, 2);
      if ((i & 7) == 7)
        out.chr(' ');
      out.str(color_off);
    }
  }

  out.chr(' ');
  out.str(color_se);
  out.chr('|');
  out.str(color_off);

  for (size_t i = 0; i < 16; ++i)
  {
    if (bytes[i] < 0)
    {
      out.str(color_cx);
      out.chr('-');
      out.str(color_off);
    }
    else
    {
      short byte = bytes[i];
      out.str(color_hex[byte >> 8]);
      byte &= 0xff;
      if (flag_color != NULL)
      {
        if (byte < 0x20)
        {
          out.str("\033[7m");
          out.chr('@' + byte);
        }
        else if (byte == 0x7f)
        {
          out.str("\033[7m~");
        }
        else if (byte > 0x7f)
        {
          out.str("\033[7m.");
        }
        else
        {
          out.chr(byte);
        }
      }
      else if (byte < 0x20 || byte >= 0x7f)
      {
        out.chr('.');
      }
      else
      {
        out.chr(byte);
      }
      out.str(color_off);
    }
  }

  out.str(color_se);
  out.chr('|');
  out.str(color_off);
  out.nl();

  for (size_t i = 0; i < 16; ++i)
    bytes[i] = -1;
}

// output the header part of the match, preceeding the matched line
void Output::header(const char *& pathname, const std::string& partname, size_t lineno, reflex::AbstractMatcher *matcher, size_t byte_offset, const char *separator, bool newline)
{
  bool sep = false;

  if (flag_with_filename && pathname != NULL)
  {
    str(color_fn);
    str(pathname);
    str(color_off);

    if (flag_null)
    {
      chr('\0');
    }
    else if (flag_break)
    {
      chr('\n');
      pathname = NULL;
    }
    else
    {
      sep = true;
    }
  }

  if (!flag_no_filename && !partname.empty())
  {
    str(color_fn);
    chr('{');
    str(partname);
    chr('}');
    str(color_off);

    sep = true;
  }

  if (flag_line_number)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_ln);
    num(lineno, flag_initial_tab ? 6 : 1);
    str(color_off);

    sep = true;
  }

  if (flag_column_number)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_cn);
    num((matcher != NULL ? matcher->columno() + 1 : 1), (flag_initial_tab ? 3 : 1));
    str(color_off);

    sep = true;
  }

  if (flag_byte_offset)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_bn);
    num(byte_offset, flag_initial_tab ? 7 : 1);
    str(color_off);

    sep = true;
  }

  if (sep)
  {
    str(color_se);
    str(separator);
    str(color_off);

    if (flag_initial_tab)
      chr('\t');

    if (newline)
      nl();
  }
}

// output "Binary file ... matches"
void Output::binary_file_matches(const char *pathname, const std::string& partname)
{
  str(color_off);
  str("Binary file");
  str(color_high);
  if (pathname != NULL)
  {
    chr(' ');
    str(pathname);
  }
  if (!partname.empty())
  {
    if (pathname == NULL)
      chr(' ');
    chr('{');
    str(partname);
    chr('}');
  }
  str(color_off);
  str(" matches");
  nl();
}

// output formatted match with options --format, --format-open, --format-close
void Output::format(const char *format, const char *pathname, const std::string& partname, size_t matches, reflex::AbstractMatcher *matcher)
{
  if (lineno > 0 && lineno == matcher->lineno() && matcher->lines() == 1)
    return;

  size_t len = 0;
  const char *sep = NULL;
  const char *s = format;

  while (*s != '\0')
  {
    const char *a = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '[')
    {
      a = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case 'F':
        if (flag_with_filename && pathname != NULL)
        {
          if (a)
            str(a, s - a - 1);
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'f':
        if (pathname != NULL)
        {
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
        }
        break;

      case 'H':
        if (flag_with_filename)
        {
          if (a)
            str(a, s - a - 1);
          if (!partname.empty())
          {
            std::string name(pathname);
            name.append("{").append(partname).append("}");
            quote(name.c_str(), name.size());
          }
          else
          {
            quote(pathname, strlen(pathname));
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'h':
        if (!partname.empty())
        {
          std::string name(pathname);
          name.append("{").append(partname).append("}");
          quote(name.c_str(), name.size());
        }
        else
        {
          quote(pathname, strlen(pathname));
        }
        break;

      case 'N':
        if (flag_line_number)
        {
          if (a)
            str(a, s - a - 1);
          num(matcher->lineno());
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'n':
        num(matcher->lineno());
        break;

      case 'K':
        if (flag_column_number)
        {
          if (a)
            str(a, s - a - 1);
          num(matcher->columno() + 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'k':
        num(matcher->columno() + 1);
        break;

      case 'B':
        if (flag_byte_offset)
        {
          if (a)
            str(a, s - a - 1);
          num(matcher->first());
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'b':
        num(matcher->first());
        break;

      case 'T':
        if (flag_initial_tab)
        {
          if (a)
            str(a, s - a - 1);
          chr('\t');
        }
        break;

      case 't':
        chr('\t');
        break;

      case 'S':
        if (matches > 0)
        {
          if (a)
            str(a, s - a - 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 's':
        if (sep != NULL)
          str(sep, len);
        else
          str(flag_separator);
        break;

      case 'w':
        num(matcher->wsize());
        break;

      case 'd':
        num(matcher->size());
        break;

      case 'e':
        num(matcher->last());
        break;

      case 'm':
        num(matches);
        break;

      case 'O':
        mat(matcher);
        break;

      case 'o':
        str(matcher->begin(), matcher->size());
        break;

      case 'Q':
        quote(matcher);
        break;

      case 'q':
        quote(matcher->begin(), matcher->size());
        break;

      case 'C':
        if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(matcher);
        break;

      case 'c':
        if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(matcher->begin(), matcher->size());
        break;

      case 'V':
        if (flag_count)
          num(matches);
        else
          csv(matcher);
        break;

      case 'v':
        if (flag_count)
          num(matches);
        else
          csv(matcher->begin(), matcher->size());
        break;

      case 'J':
        if (flag_count)
          num(matches);
        else
          json(matcher);
        break;

      case 'j':
        if (flag_count)
          num(matches);
        else
          json(matcher->begin(), matcher->size());
        break;

      case 'X':
        if (flag_count)
          num(matches);
        else
          xml(matcher);
        break;

      case 'x':
        if (flag_count)
          num(matches);
        else
          xml(matcher->begin(), matcher->size());
        break;

      case 'z':
        str(partname);
        break;

      case 'u':
        if (!flag_ungroup)
          lineno = matcher->lineno();
        break;

      case '$':
        sep = a;
        len = s - a - 1;
        break;

      case '~':
        chr('\n');
        break;

      case '<':
        if (matches <= 1 && a)
          str(a, s - a - 1);
        break;

      case '>':
        if (matches > 1 && a)
          str(a, s - a - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (matches > 1)
          chr(c);
        break;

      default:
        chr(c);
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '#':
        std::pair<const char*,size_t> capture = (*matcher)[c == '#' ? strtoul((a != NULL ? a : "0"), NULL, 10) : c - '0'];
        str(capture.first, capture.second);
        break;
    }
    ++s;
  }
}

// output a quoted string with escapes for \ and "
void Output::quote(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    if (*s == '\\' || *s == '"')
    {
      str(t, s - t);
      t = s;
      chr('\\');
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output string in C/C++
void Output::cpp(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c < 0x20 || c == '"' || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        if (c > 0x20)
        {
          chr('\\');
          chr(c);
        }
        else
        {
          str("\\x");
          hex(c, 2);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output quoted string in CSV
void Output::csv(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c == '"')
      {
        str(t, s - t);
        t = s + 1;
        str("\"\"");
      }
      else if ((c < 0x20 && c != '\t') || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        if (c > 0x20)
        {
          chr('\\');
          chr(c);
        }
        else
        {
          str("\\x");
          hex(c, 2);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output quoted string in JSON
void Output::json(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c < 0x20 || c == '"' || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        if (c > 0x20)
        {
          chr('\\');
          chr(c);
        }
        else
        {
          str("\\u");
          hex(c, 4);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output in XML
void Output::xml(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      switch (c)
      {
        case '&':
          str(t, s - t);
          t = s + 1;
          str("&amp;");
          break;

        case '<':
          str(t, s - t);
          t = s + 1;
          str("&lt;");
          break;

        case '>':
          str(t, s - t);
          t = s + 1;
          str("&gt;");
          break;

        case '"':
          str(t, s - t);
          t = s + 1;
          str("&quot;");
          break;

        case 0x7f:
          str(t, s - t);
          t = s + 1;
          str("&#x7f;");
          break;

        default:
          if (c < 0x20)
          {
            str(t, s - t);
            t = s + 1;
            str("&#");
            num(c);
            chr(';');
          }
      }
    }
    ++s;
  }
  str(t, s - t);
}

// grep manages output, matcher, input, and decompression
struct Grep {

  Grep(FILE *file, reflex::AbstractMatcher *matcher)
    :
      out(file),
      matcher(matcher),
      file(NULL)
#ifdef HAVE_LIBZ
    , stream(NULL),
      streambuf(NULL)
#endif
  { }

  // search a file
  virtual void search(const char *pathname);

  // open a file for (binary) reading and assign input, decompress the file when --z, --decompress specified
  bool open_file(const char *pathname)
  {
    if (pathname == NULL)
    {
      pathname = flag_label;
      file = stdin;
    }
    else if (fopen_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
    {
      warning("cannot read", pathname);

      return false;
    }

#ifdef HAVE_LIBZ
    if (flag_decompress)
    {
#ifdef WITH_DECOMPRESSION_THREAD

      pipe_fd[0] = -1;
      pipe_fd[1] = -1;
      extracting = false;

      FILE *pipe_in = NULL;

      // open pipe between worker and decompression thread, then start decompression thread
      if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "r")) != NULL)
      {
        thread = std::thread(&Grep::decompress, this, pathname);

        input = reflex::Input(pipe_in, flag_encoding_type);
      }
      else
      {
        if (pipe_fd[0] != -1)
        {
          close(pipe_fd[0]);
          close(pipe_fd[1]);
          pipe_fd[0] = -1;
          pipe_fd[1] = -1;
        }

        // if creating a pipe fails, then we fall back on using a decompression stream as input
        streambuf = new zstreambuf(pathname, file);
        stream = new std::istream(streambuf);
        input = stream;
      }

#else

      // create a decompression stream to read as input
      streambuf = new zstreambuf(pathname, file);
      stream = new std::istream(streambuf);
      input = stream;

#endif
    }
    else
#endif
    {
      input = reflex::Input(file, flag_encoding_type);
    }

    return true;
  }

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD

  // decompression thread
  void decompress(const char *pathname)
  {
    // create a decompression stream buffer
    zstreambuf zstrm(pathname, file);

    // let's use the internal zstreambuf buffer to hold decompressed data
    unsigned char *buf;
    size_t maxlen;
    zstrm.get_buffer(buf, maxlen);

    // by default, we are not extracting parts of an archive
    extracting = false;

    // to hold the path (prefix + name) extracted from the zip file
    std::string path;

    // extract the parts of a zip file, one by one, if zip file detected
    while (true)
    {
      // a regular file, may be reset when unzipping a directory
      bool is_regular = true;

      const zstreambuf::ZipInfo *zipinfo = zstrm.zipinfo();
      if (zipinfo != NULL)
      {
        if (!zipinfo->name.empty() && zipinfo->name.back() == '/')
        {
          // skip zip directories
          is_regular = false;
        }
        else
        {
          path.assign(zipinfo->name);

          // produce headers with zip file pathnames for each archived part (Grep::partname)
          if (!flag_no_filename)
            flag_no_header = false;
        }
      }

      // decompress a block of data into the buffer
      std::streamsize len = zstrm.decompress(buf, maxlen);
      if (len < 0)
        break;

      if (!filter_tar(zstrm, path, buf, maxlen, len) && !filter_cpio(zstrm, path, buf, maxlen, len))
      {
        // not a tar/cpio file, decompress the data into pipe, if not unzipping or if zip file meets selection criteria
        bool is_selected = is_regular && (zipinfo == NULL || select_matching(path.c_str(), buf, static_cast<size_t>(len), true));
        if (is_selected)
        {
          // if pipe is closed, then reopen it
          if (pipe_fd[1] == -1)
          {
            // signal close and wait until the main grep thread created a new pipe in close_file()
            std::unique_lock<std::mutex> lock(pipe_mutex);
            pipe_close.notify_one();
            waiting = true;
            pipe_ready.wait(lock);
            waiting = false;
            lock.unlock();

            // failed to create a pipe in close_file()
            if (pipe_fd[1] == -1)
              break;
          }

          // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
          partname.swap(path);
        }

        // push decompressed data into pipe
        while (len > 0)
        {
          // write buffer data to the pipe, if the pipe is broken then the receiver is waiting for this thread to join
          if (is_selected && write(pipe_fd[1], buf, static_cast<size_t>(len)) < len)
            break;

          // decompress the next block of data into the buffer
          len = zstrm.decompress(buf, maxlen);
        }
      }

      // break if not unzipping or if no more files to unzip
      if (zstrm.zipinfo() == NULL)
        break;

      // extracting a zip file
      extracting = true;

      // close our end of the pipe
      close(pipe_fd[1]);
      pipe_fd[1] = -1;
    }

    if (extracting)
    {
      // inform the main grep thread we are done extracting
      extracting = false;

      // close our end of the pipe
      if (pipe_fd[1] >= 0)
      {
        close(pipe_fd[1]);
        pipe_fd[1] = -1;
      }

      // signal close
      std::unique_lock<std::mutex> lock(pipe_mutex);
      pipe_close.notify_one();
      lock.unlock();
    }
    else
    {
      // close our end of the pipe
      close(pipe_fd[1]);
      pipe_fd[1] = -1;
    }
  }

  // if tar file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_tar(zstreambuf& zstrm, const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
  {
    const int BLOCKSIZE = 512;

    if (len > BLOCKSIZE)
    {
      // v7 and ustar formats
      const char ustar_magic[8] = { 'u', 's', 't', 'a', 'r', 0, '0', '0' };

      // gnu and oldgnu formats
      const char gnutar_magic[8] = { 'u', 's', 't', 'a', 'r', ' ', ' ', 0 };

      // is this a tar archive?
      if (*buf != '\0' && (memcmp(buf + 257, ustar_magic, 8) == 0 || memcmp(buf + 257, gnutar_magic, 8) == 0))
      {
        // produce headers with tar file pathnames for each archived part (Grep::partname)
        if (!flag_no_filename)
          flag_no_header = false;

        // inform the main grep thread we are extracing an archive
        extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // to hold long path extracted from the previous header block that is marked with typeflag 'x' or 'L'
        std::string long_path;

        while (true)
        {
          // extract tar header fields (name and prefix strings are not \0-terminated!!)
          const char *name = reinterpret_cast<const char*>(buf);
          const char *prefix = reinterpret_cast<const char*>(buf + 345);
          size_t size = strtoul(reinterpret_cast<const char*>(buf + 124), NULL, 8);
          int padding = (BLOCKSIZE - size % BLOCKSIZE) % BLOCKSIZE;
          unsigned char typeflag = buf[156];

          // header types
          bool is_regular = typeflag == '0' || typeflag == '\0';
          bool is_xhd = typeflag == 'x';
          bool is_extended = typeflag == 'L';

          // assign the (long) tar pathname
          path.clear();
          if (long_path.empty())
          {
            if (*prefix != '\0')
            {
              if (prefix[154] == '\0')
                path.assign(prefix);
              else
                path.assign(prefix, 155);
              path.push_back('/');
            }
            if (name[99] == '\0')
              path.append(name);
            else
              path.append(name, 100);
          }
          else
          {
            path.swap(long_path);
          }

          // remove header to advance to the body
          len -= BLOCKSIZE;
          memmove(buf, buf + BLOCKSIZE, static_cast<size_t>(len));

          // check if archived file meets selection criteria
          size_t minlen = std::min(static_cast<size_t>(len), size);
          bool is_selected = select_matching(path.c_str(), buf, minlen, is_regular);

          // if extended headers are present
          if (is_xhd)
          {
            // typeflag 'x': extract the long path from the pax extended header block in the body
            const char *b = reinterpret_cast<const char*>(buf);
            const char *e = b + minlen;
            const char *t = "path=";
            const char *s = std::search(b, e, t, t + 5);
            if (s != NULL)
            {
              e = static_cast<const char*>(memchr(s, '\n', e - s));
              if (e != NULL)
                long_path.assign(s + 5, e - s - 5);
            }
          }
          else if (is_extended)
          {
            // typeflag 'L': get long name from the body
            long_path.assign(reinterpret_cast<const char*>(buf), minlen);
          }

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected && pipe_fd[1] == -1)
          {
            // signal close and wait until the main grep thread created a new pipe in close_file()
            std::unique_lock<std::mutex> lock(pipe_mutex);
            pipe_close.notify_one();
            waiting = true;
            pipe_ready.wait(lock);
            waiting = false;
            lock.unlock();

            // failed to create a pipe in close_file()
            if (pipe_fd[1] == -1)
              break;
          }

          // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
          if (is_selected)
          {
            if (!partprefix.empty())
              partname.assign(partprefix).append(":").append(path);
            else
              partname.swap(path);
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          while (len > 0)
          {
            size_t len_out = std::min(static_cast<size_t>(len), size);

            if (ok)
            {
              // write decompressed data to the pipe, if the pipe is broken then stop pushing more data into this pipe
              if (write(pipe_fd[1], buf, len_out) < static_cast<ssize_t>(len_out))
                ok = false;
            }

            size -= len_out;

            // reached the end of the tar body?
            if (size == 0)
            {
              len -= len_out;
              memmove(buf, buf + len_out, static_cast<size_t>(len));

              break;
            }

            // decompress the next block of data into the buffer
            len = zstrm.decompress(buf, maxlen);
          }

          // error?
          if (len < 0)
            break;

          // fill the rest of the buffer with decompressed data
          if (static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstrm.decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error?
            if (len_in < 0)
              break;

            len += len_in;
          }

          // skip padding
          if (len > padding)
          {
            len -= padding;
            memmove(buf, buf + padding, static_cast<size_t>(len));
          }

          // rest of the file is too short, something is wrong
          if (len <= BLOCKSIZE)
            break;

          // no more parts to extract?
          if (*buf == '\0' || (memcmp(buf + 257, ustar_magic, 8) != 0 && memcmp(buf + 257, gnutar_magic, 8) != 0))
            break;

          // get a new pipe to search the next part in the archive, if the previous part was a regular file
          if (is_selected)
          {
            // close our end of the pipe
            close(pipe_fd[1]);
            pipe_fd[1] = -1;
          }
        }

        // done extracting the tar file
        return true;
      }
    }

    // not a tar file
    return false;
  }

  // if cpio file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_cpio(zstreambuf& zstrm, const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
  {
    const int HEADERSIZE = 110;

    if (len > HEADERSIZE)
    {
      // odc format
      const char odc_magic[6] = { '0', '7', '0', '7', '0', '7' };

      // newc format
      const char newc_magic[6] = { '0', '7', '0', '7', '0', '1' };

      // newc+crc format
      const char newc_crc_magic[6] = { '0', '7', '0', '7', '0', '2' };

      // is this a cpio archive?
      if (memcmp(buf, odc_magic, 6) == 0 || memcmp(buf, newc_magic, 6) == 0 || memcmp(buf, newc_crc_magic, 6) == 0)
      {
        // produce headers with cpio file pathnames for each archived part (Grep::partname)
        if (!flag_no_filename)
          flag_no_header = false;

        // inform the main grep thread we are extracing an archive
        extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // need a new pipe, close current pipe first to create a new pipe
        bool in_progress = false;

        while (true)
        {
          // true if odc, false if newc
          bool is_odc = buf[5] == '7';

          int header_len = is_odc ? 76 : 110;

          char tmp[16];
          char *rest;

          // get the namesize
          size_t namesize;
          if (is_odc)
          {
            memcpy(tmp, buf + 59, 6);
            tmp[6] = '\0';
            namesize = strtoul(tmp, &rest, 8);
          }
          else
          {
            memcpy(tmp, buf + 94, 8);
            tmp[8] = '\0';
            namesize = strtoul(tmp, &rest, 16);
          }

          // if not a valid mode value, then something is wrong
          if (rest == NULL || *rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // pathnames with trailing \0 cannot be empty or too large
          if (namesize <= 1 || namesize >= 65536)
            break;
          
          // get the filesize
          size_t filesize;
          if (is_odc)
          {
            memcpy(tmp, buf + 65, 11);
            tmp[11] = '\0';
            filesize = strtoul(tmp, &rest, 8);
          }
          else
          {
            memcpy(tmp, buf + 54, 8);
            tmp[8] = '\0';
            filesize = strtoul(tmp, &rest, 16);
          }

          // if not a valid mode value, then something is wrong
          if (rest == NULL || *rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // true if this is a regular file when (mode & 0170000) == 0100000
          bool is_regular;
          if (is_odc)
          {
            memcpy(tmp, buf + 18, 6);
            tmp[6] = '\0';
            is_regular = (strtoul(tmp, &rest, 8) & 0170000) == 0100000;
          }
          else
          {
            memcpy(tmp, buf + 14, 8);
            tmp[8] = '\0';
            is_regular = (strtoul(tmp, &rest, 16) & 0170000) == 0100000;
          }

          // if not a valid mode value, then something is wrong
          if (rest == NULL || *rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // remove header to advance to the body
          len -= header_len;
          memmove(buf, buf + header_len, static_cast<size_t>(len));

          // assign the cpio pathname
          path.clear();

          size_t size = namesize;

          while (len > 0)
          {
            size_t n = std::min(static_cast<size_t>(len), size);
            char *b = reinterpret_cast<char*>(buf);

            path.append(b, n);
            size -= n;

            if (size == 0)
            {
              // remove pathname to advance to the body
              len -= n;
              memmove(buf, buf + n, static_cast<size_t>(len));

              break;
            }

            // decompress the next block of data into the buffer
            len = zstrm.decompress(buf, maxlen);
          }

          // error?
          if (len < 0)
            break;

          // remove trailing \0
          if (path.back() == '\0')
            path.pop_back();

          // reached the end of the cpio archive?
          if (path == "TRAILER!!!")
            break;

          // fill the rest of the buffer with decompressed data
          if (static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstrm.decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error?
            if (len_in < 0)
              break;

            len += len_in;
          }

          // skip newc format \0 padding after the pathname
          if (!is_odc && len > 3)
          {
            size_t n = 4 - (110 + namesize) % 4;
            len -= n;
            memmove(buf, buf + n, static_cast<size_t>(len));
          }

          // check if archived file meets selection criteria
          size_t minlen = std::min(static_cast<size_t>(len), filesize);
          bool is_selected = select_matching(path.c_str(), buf, minlen, is_regular);

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected && pipe_fd[1] == -1)
          {
            // signal close and wait until the main grep thread created a new pipe in close_file()
            std::unique_lock<std::mutex> lock(pipe_mutex);
            pipe_close.notify_one();
            waiting = true;
            pipe_ready.wait(lock);
            waiting = false;
            lock.unlock();

            // failed to create a pipe in close_file()
            if (pipe_fd[1] == -1)
              break;
          }

          // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
          if (is_selected)
          {
            if (!partprefix.empty())
              partname.assign(partprefix).append(":").append(path);
            else
              partname.swap(path);
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          size = filesize;

          while (len > 0)
          {
            size_t len_out = std::min(static_cast<size_t>(len), size);

            if (ok)
            {
              // write decompressed data to the pipe, if the pipe is broken then stop pushing more data into this pipe
              if (write(pipe_fd[1], buf, len_out) < static_cast<ssize_t>(len_out))
                ok = false;
            }

            size -= len_out;

            // reached the end of the cpio body?
            if (size == 0)
            {
              len -= len_out;
              memmove(buf, buf + len_out, static_cast<size_t>(len));

              break;
            }

            // decompress the next block of data into the buffer
            len = zstrm.decompress(buf, maxlen);
          }

          // error?
          if (len < 0)
            break;

          if (static_cast<size_t>(len) < maxlen)
          {
            // fill the rest of the buffer with decompressed data
            std::streamsize len_in = zstrm.decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error?
            if (len_in < 0)
              break;

            len += len_in;
          }

          // skip newc format \0 padding
          if (!is_odc && len > 2)
          {
            size_t n = (4 - filesize % 4) % 4;
            len -= n;
            memmove(buf, buf + n, static_cast<size_t>(len));
          }

          // rest of the file is too short, something is wrong
          if (len <= HEADERSIZE)
            break;

          // quit if this is not valid cpio header magic
          if (memcmp(buf, odc_magic, 6) != 0 && memcmp(buf, newc_magic, 6) != 0 && memcmp(buf, newc_crc_magic, 6) != 0)
            break;

          // get a new pipe to search the next part in the archive, if the previous part was a regular file
          if (is_selected)
          {
            // close our end of the pipe
            close(pipe_fd[1]);
            pipe_fd[1] = -1;

            in_progress = true;
          }
        }

        // done extracting the cpio file
        return true;
      }
    }

    // not a cpio file
    return false;
  }

  bool select_matching(const char *path, const unsigned char *buf, size_t len, bool is_regular)
  {
    bool is_selected = is_regular;

    if (is_selected)
    {
      const char *basename = strrchr(path, '/');
      if (basename == NULL)
        basename = path;
      else
        ++basename;

      if (flag_no_hidden && *basename == '.')
        return false;

      // -O, -t, and -g (--include and --exclude): check if pathname or basename matches globs, is_selected = false if not
      if (!flag_exclude.empty() || !flag_include.empty())
      {
        // exclude files whose basename matches any one of the --exclude globs
        for (auto& glob : flag_exclude)
          if (!(is_selected = !glob_match(path, basename, glob.c_str())))
            break;

        // include only if not excluded
        if (is_selected)
        {
          // include files whose basename matches any one of the --include globs
          for (auto& glob : flag_include)
            if ((is_selected = glob_match(path, basename, glob.c_str())))
              break;
        }
      }

      // -M: check magic bytes, requires sufficiently large len of buf[]
      if ((flag_include.empty() || !is_selected) && !flag_file_magic.empty())
      {
        if (buf != NULL)
        {
          reflex::Matcher magic(magic_pattern);
          magic.buffer(const_cast<char*>(reinterpret_cast<const char*>(buf)), len + 1);
          is_selected = magic.scan() != 0;
        }
      }
    }

    return is_selected;
  }

#endif
#endif
  
  // close the file and clear input, return true if next file is extracted from an archive to search
  bool close_file(const char *pathname)
  {

#ifdef HAVE_LIBZ

#ifdef WITH_DECOMPRESSION_THREAD

    if (flag_decompress && pipe_fd[0] != -1)
    {
      // close the FILE* pipe created with fdopen()
      if (input.file() != NULL)
        fclose(input.file());

      // our end of the pipe is no longer in use
      pipe_fd[0] = -1;

      // if extracting and the decompression filter thread is not yet waiting, then wait until the other end closed the pipe
      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (extracting && !waiting)
        pipe_close.wait(lock);
      lock.unlock();

      // extract the next file
      if (extracting)
      {
        // output is not blocked
        if (!out.eof)
        {
          FILE *pipe_in = NULL;

          // open pipe between worker and decompression thread, then start decompression thread
          if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "r")) != NULL)
          {
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();

            input = reflex::Input(pipe_in, flag_encoding_type);

            // loop back the the start to search next file in the archive
            return true;
          }

          // failed to create a new pipe
          if (pipe_fd[0] != -1)
          {
            warning("cannot open pipe while reading", pathname);

            close(pipe_fd[0]);
            close(pipe_fd[1]);
          }
        }

        pipe_fd[0] = -1;
        pipe_fd[1] = -1;

        // notify the decompression thread filter_tar/filter_cpio
        pipe_ready.notify_one();
      }

      // join the decompression thread
      thread.join();
    }

#endif

    if (streambuf != NULL)
    {
      delete streambuf;
      streambuf = NULL;
    }

    if (stream != NULL)
    {
      delete stream;
      stream = NULL;
    }

#endif

    // close the file
    if (file != NULL && file != stdin)
    {
      fclose(file);
      file = NULL;
    }

    input.clear();

    return false;
  }

  // specify input to read for matcher, when input is a regular file then try mmap for zero copy overhead
  void read_file()
  {
    const char *base;
    size_t size;

    // attempt to mmap the input file
    if (mmap.file(input, base, size))
    {
      // matcher reads directly from protected mmap memory (cast is safe: base[0..size] is not modified!)
      matcher->buffer(const_cast<char*>(base), size + 1);
    }
    else
    {
      matcher->input(input);

      // buffer all input to work around Boost.Regex bug, this may throw std::bad_alloc if the file is too large
      if (flag_perl_regexp)
        matcher->buffer();
    }

    // -K=NUM1[,NUM2]: start searching at line NUM1
    for (size_t i = flag_min_line; i > 1; --i)
      if (!matcher->skip('\n'))
        break;
  }

  std::string              partname;   // the name of an extracted file from an archive
  Output                   out;        // buffered and synchronized output
  reflex::AbstractMatcher *matcher;    // the matcher we're using
  MMap                     mmap;       // mmap state
  reflex::Input            input;      // input to the matcher
  FILE                    *file;       // the current input file
#ifdef HAVE_LIBZ
  std::istream            *stream;     // the current input stream ...
  zstreambuf              *streambuf;  // of the compressed file
#ifdef WITH_DECOMPRESSION_THREAD
  std::thread              thread;     // decompression thread
  int                      pipe_fd[2]; // decompressed stream pipe
  std::mutex               pipe_mutex; // mutex to extract files in thread
  std::condition_variable  pipe_ready; // cv to control new pipe creation
  std::condition_variable  pipe_close; // cv to control new pipe creation
  volatile bool            extracting; // true if extracting files from TAR
  volatile bool            waiting;    // true if decompression thread is waiting
#endif
#endif

};

// a job in the job queue
struct Job {

  // sentinel job NONE
  static const char *NONE;

  Job()
    :
      pathname()
  { }

  Job(const char *pathname)
    :
      pathname(pathname)
  { }

  bool none()
  {
    return pathname.empty();
  }

  std::string pathname;
};

// we could use C++20 constinit, though declaring here is more efficient:
const char *Job::NONE = "";

struct GrepWorker;

// master submits jobs to workers and supports lock-free job stealing
struct GrepMaster : public Grep {

  GrepMaster(FILE *file, reflex::AbstractMatcher *matcher)
    :
      Grep(file, matcher)
  {
    start_workers();
    iworker = workers.begin();
  }

  ~GrepMaster()
  {
    stop_workers();
  }

  // search a file by submitting it as a job to a worker
  void search(const char *pathname) override
  {
    submit(pathname);
  }

  // start worker threads
  void start_workers();

  // stop all workers
  void stop_workers();

  // submit a job with a pathname to a worker, workers are visited round-robin
  void submit(const char *pathname);

  // lock-free job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
  bool steal(GrepWorker *worker);

  std::list<GrepWorker>           workers;    // workers running threads
  std::list<GrepWorker>::iterator iworker;    // the next worker to submit a job to
  std::mutex                      sync_mutex; // mutex to sync output, shared by workers

};

// worker runs a thread waiting for jobs submitted by the master
struct GrepWorker : public Grep {

  GrepWorker(FILE *file, reflex::AbstractMatcher *matcher, GrepMaster *master)
    :
      Grep(file, matcher->clone()),
      master(master),
      todo(0)
  {
    // all workers synchronize their output on the master's mutex lock
    out.sync(master->sync_mutex);

    // run worker thread executing jobs assigned in its queue
    thread = std::thread(&GrepWorker::execute, this);
  }

  ~GrepWorker()
  {
    // delete cloned matcher
    delete matcher;
  }

  // worker thread execution
  void execute();

  // submit a job to this worker
  void submit_job(const char *pathname)
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    jobs.emplace(pathname);
    ++todo;

    queue_work.notify_one();
  }

  // receive a job for this worker, wait until one arrives
  void next_job(Job& job)
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    while (jobs.empty())
      queue_work.wait(lock);

    job = jobs.front();

    jobs.pop();
    --todo;

    // if we popped a NONE sentinel but the queue has some jobs, then move the sentinel to the back of the queue
    if (job.none() && !jobs.empty())
    {
      jobs.emplace(Job::NONE);
      job = jobs.front();
      jobs.pop();
    }
  }

  // steal a job from this worker, if at least --min-steal jobs to do, returns true if successful
  bool steal_job(Job& job)
  {
    // not enough jobs in the queue to steal from
    if (todo < flag_min_steal)
      return false;

    std::unique_lock<std::mutex> lock(queue_mutex);
    if (jobs.empty())
      return false;

    job = jobs.front();

    // we cannot steal a NONE sentinel
    if (job.none())
      return false;

    jobs.pop();
    --todo;

    return true;
  }

  // submit stop to this worker
  void stop()
  {
    submit_job(Job::NONE);
  }

  std::thread                  thread;      // thread of this worker, spawns GrepWorker::execute()
  GrepMaster                  *master;      // the master of this worker
  std::mutex                   queue_mutex; // job queue mutex
  std::condition_variable      queue_work;  // cv to control the job queue
  std::queue<Job>              jobs;        // queue of pending jobs submitted to this worker
  std::atomic_size_t           todo;        // number of jobs in the queue, for lock-free job stealing

};

// start worker threads
void GrepMaster::start_workers()
{
  for (size_t i = 0; i < threads; ++i)
    workers.emplace(workers.end(), out.file, matcher, this);
}

// stop all workers
void GrepMaster::stop_workers()
{
  for (auto& worker : workers)
    worker.stop();

  for (auto& worker : workers)
    worker.thread.join();
}

// submit a job with a pathname to a worker, workers are visited round-robin
void GrepMaster::submit(const char *pathname)
{
  iworker->submit_job(pathname);

  // around we go
  ++iworker;
  if (iworker == workers.end())
    iworker = workers.begin();
}

// lock-free job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
bool GrepMaster::steal(GrepWorker *worker)
{
  // pick a random co-worker
  long n = rand() % threads;
  std::list<GrepWorker>::iterator iworker = workers.begin();
  while (--n >= 0)
    ++iworker;

  // try to steal a job from the random co-worker or the next co-workers
  for (size_t i = 0; i < threads; ++i)
  {
    // around we go
    if (iworker == workers.end())
      iworker = workers.begin();

    // if co-worker isn't this worker (no self-stealing!)
    if (&*iworker != worker)
    {
      Job job;

      // if co-worker has at least --min-steal jobs then steal one for this worker
      if (iworker->steal_job(job))
      {
        worker->submit_job(job.pathname.c_str());

        return true;
      }
    }

    // try next co-worker
    ++iworker;
  }

  // couldn't steal any job
  return false;
}

// execute worker thread
void GrepWorker::execute()
{
  Job job;

  while (true)
  {
    // wait for next job
    next_job(job);

    // worker should stop?
    if (job.none())
      break;

    // search the file for this job
    search(job.pathname.c_str());

    // if almost nothing to do and we need a next job, then try stealing a job from a co-worker
    if (todo <= 1)
      master->steal(this);
  }
}

// table of RE/flex file encodings for ugrep option --encoding
const struct { const char *format; reflex::Input::file_encoding_type encoding; } format_table[] = {
  { "binary",     reflex::Input::file_encoding::plain   },
  { "ISO-8859-1", reflex::Input::file_encoding::latin   },
  { "ASCII",      reflex::Input::file_encoding::utf8    },
  { "EBCDIC",     reflex::Input::file_encoding::ebcdic  },
  { "UTF-8",      reflex::Input::file_encoding::utf8    },
  { "UTF-16",     reflex::Input::file_encoding::utf16be },
  { "UTF-16BE",   reflex::Input::file_encoding::utf16be },
  { "UTF-16LE",   reflex::Input::file_encoding::utf16le },
  { "UTF-32",     reflex::Input::file_encoding::utf32be },
  { "UTF-32BE",   reflex::Input::file_encoding::utf32be },
  { "UTF-32LE",   reflex::Input::file_encoding::utf32le },
  { "CP437",      reflex::Input::file_encoding::cp437   },
  { "CP850",      reflex::Input::file_encoding::cp850   },
  { "CP858",      reflex::Input::file_encoding::cp858   },
  { "CP1250",     reflex::Input::file_encoding::cp1250  },
  { "CP1251",     reflex::Input::file_encoding::cp1251  },
  { "CP1252",     reflex::Input::file_encoding::cp1252  },
  { "CP1253",     reflex::Input::file_encoding::cp1253  },
  { "CP1254",     reflex::Input::file_encoding::cp1254  },
  { "CP1255",     reflex::Input::file_encoding::cp1255  },
  { "CP1256",     reflex::Input::file_encoding::cp1256  },
  { "CP1257",     reflex::Input::file_encoding::cp1257  },
  { "CP1258",     reflex::Input::file_encoding::cp1258  },
  { NULL, 0 }
};

// table of file types for ugrep option -t, --file-type
const struct { const char *type; const char *extensions; const char *magic; } type_table[] = {
  { "actionscript", "as,mxml",                                                  NULL },
  { "ada",          "ada,adb,ads",                                              NULL },
  { "asm",          "asm,s,S",                                                  NULL },
  { "asp",          "asp",                                                      NULL },
  { "aspx",         "master,ascx,asmx,aspx,svc",                                NULL },
  { "autoconf",     "ac,in",                                                    NULL },
  { "automake",     "am,in",                                                    NULL },
  { "awk",          "awk",                                                      NULL },
  { "Awk",          "awk",                                                      "#!/.*\\Wg?awk(\\W.*)?\\n" },
  { "basic",        "bas,BAS,cls,frm,ctl,vb,resx",                              NULL },
  { "batch",        "bat,BAT,cmd,CMD",                                          NULL },
  { "bison",        "y,yy,yxx",                                                 NULL },
  { "c",            "c,h,H,hdl,xs",                                             NULL },
  { "c++",          "cpp,CPP,cc,cxx,CXX,h,hh,H,hpp,hxx,Hxx,HXX",                NULL },
  { "clojure",      "clj",                                                      NULL },
  { "csharp",       "cs",                                                       NULL },
  { "css",          "css",                                                      NULL },
  { "csv",          "csv",                                                      NULL },
  { "dart",         "dart",                                                     NULL },
  { "Dart",         "dart",                                                     "#!/.*\\Wdart(\\W.*)?\\n" },
  { "delphi",       "pas,int,dfm,nfm,dof,dpk,dproj,groupproj,bdsgroup,bdsproj", NULL },
  { "elisp",        "el",                                                       NULL },
  { "elixir",       "ex,exs",                                                   NULL },
  { "erlang",       "erl,hrl",                                                  NULL },
  { "fortran",      "for,ftn,fpp,f,F,f77,F77,f90,F90,f95,F95,f03,F03",          NULL },
  { "gif",          "gif",                                                      NULL },
  { "Gif",          "gif",                                                      "GIF87a|GIF89a" },
  { "go",           "go",                                                       NULL },
  { "groovy",       "groovy,gtmpl,gpp,grunit,gradle",                           NULL },
  { "gsp",          "gsp",                                                      NULL },
  { "haskell",      "hs,lhs",                                                   NULL },
  { "html",         "htm,html,xhtml",                                           NULL },
  { "jade",         "jade",                                                     NULL },
  { "java",         "java,properties",                                          NULL },
  { "jpeg",         "jpg,jpeg",                                                 NULL },
  { "Jpeg",         "jpg,jpeg",                                                 "\\xff\\xd8\\xff[\\xdb\\xe0\\xe1\\xee]" },
  { "js",           "js",                                                       NULL },
  { "json",         "json",                                                     NULL },
  { "jsp",          "jsp,jspx,jthm,jhtml",                                      NULL },
  { "julia",        "jl",                                                       NULL },
  { "kotlin",       "kt,kts",                                                   NULL },
  { "less",         "less",                                                     NULL },
  { "lex",          "l,ll,lxx",                                                 NULL },
  { "lisp",         "lisp,lsp",                                                 NULL },
  { "lua",          "lua",                                                      NULL },
  { "m4",           "m4",                                                       NULL },
  { "make",         "mk,mak,makefile,Makefile,Makefile.Debug,Makefile.Release", NULL },
  { "markdown",     "md",                                                       NULL },
  { "matlab",       "m",                                                        NULL },
  { "node",         "js",                                                       NULL },
  { "Node",         "js",                                                       "#!/.*\\Wnode(\\W.*)?\\n" },
  { "objc",         "m,h",                                                      NULL },
  { "objc++",       "mm,h",                                                     NULL },
  { "ocaml",        "ml,mli,mll,mly",                                           NULL },
  { "parrot",       "pir,pasm,pmc,ops,pod,pg,tg",                               NULL },
  { "pascal",       "pas,pp",                                                   NULL },
  { "pdf",          "pdf",                                                      NULL },
  { "Pdf",          "pdf",                                                      "\\x25\\x50\\x44\\x46\\x2d" },
  { "perl",         "pl,PL,pm,pod,t,psgi",                                      NULL },
  { "Perl",         "pl,PL,pm,pod,t,psgi",                                      "#!/.*\\Wperl(\\W.*)?\\n" },
  { "php",          "php,php3,php4,phtml",                                      NULL },
  { "Php",          "php,php3,php4,phtml",                                      "#!/.*\\Wphp(\\W.*)?\\n" },
  { "png",          "png",                                                      NULL },
  { "Png",          "png",                                                      "\\x89png\\x0d\\x0a\\x1a\\x0a" },
  { "prolog",       "pl,pro",                                                   NULL },
  { "python",       "py",                                                       NULL },
  { "Python",       "py",                                                       "#!/.*\\Wpython(\\W.*)?\\n" },
  { "r",            "R",                                                        NULL },
  { "rpm",          "rpm",                                                      NULL },
  { "Rpm",          "rpm",                                                      "\\xed\\xab\\xee\\xdb" },
  { "rst",          "rst",                                                      NULL },
  { "rtf",          "rtf",                                                      NULL },
  { "Rtf",          "rtf",                                                      "\\{\\rtf1" },
  { "ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec,Rakefile",                 NULL },
  { "Ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec,Rakefile",                 "#!/.*\\Wruby(\\W.*)?\\n" },
  { "rust",         "rs",                                                       NULL },
  { "scala",        "scala",                                                    NULL },
  { "scheme",       "scm,ss",                                                   NULL },
  { "shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish",                       NULL },
  { "Shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish",                       "#!/.*\\W(ba|da|t?c|k|z|fi)?sh(\\W.*)?\\n" },
  { "smalltalk",    "st",                                                       NULL },
  { "sql",          "sql,ctl",                                                  NULL },
  { "svg",          "svg",                                                      NULL },
  { "swift",        "swift",                                                    NULL },
  { "tcl",          "tcl,itcl,itk",                                             NULL },
  { "tex",          "tex,cls,sty,bib",                                          NULL },
  { "text",         "text,txt,TXT,md",                                          NULL },
  { "tiff",         "tif,tiff",                                                 NULL },
  { "Tiff",         "tif,tiff",                                                 "\\x49\\x49\\x2a\\x00|\\x4d\\x4d\\x00\\x2a" },
  { "tt",           "tt,tt2,ttml",                                              NULL },
  { "typescript",   "ts,tsx",                                                   NULL },
  { "verilog",      "v,vh,sv",                                                  NULL },
  { "vhdl",         "vhd,vhdl",                                                 NULL },
  { "vim",          "vim",                                                      NULL },
  { "xml",          "xml,xsd,xsl,xslt,wsdl,rss,svg,ent,plist",                  NULL },
  { "Xml",          "xml,xsd,xsl,xslt,wsdl,rss,svg,ent,plist",                  "<\\?xml " },
  { "yacc",         "y",                                                        NULL },
  { "yaml",         "yaml,yml",                                                 NULL },
  { NULL,           NULL,                                                       NULL }
};

// function protos
void ugrep(reflex::Matcher& magic, Grep& grep, std::vector<const char*>& files);
void find(size_t level, reflex::Matcher& magic, Grep& grep, const char *pathname, const char *basename, int type, ino_t inode, bool is_argument = false);
void recurse(size_t level, reflex::Matcher& magic, Grep& grep, const char *pathname);

// ugrep main()
int main(int argc, char **argv)
{
  const char *pattern = NULL;
  std::vector<const char*> files;
  bool options = true;

  // parse ugrep command-line options and arguments
  for (int i = 1; i < argc; ++i)
  {
    const char *arg = argv[i];

    if ((*arg == '-'
#ifdef OS_WIN
         || *arg == '/'
#endif
        ) && arg[1] && options)
    {
      bool is_grouped = true;

      // parse a ugrep command-line option
      while (is_grouped && *++arg != '\0')
      {
        switch (*arg)
        {
          case '-':
            ++arg;
            if (!*arg)
              options = false;
            else if (strncmp(arg, "after-context=", 14) == 0)
              flag_after_context = strtopos(arg + 14, "invalid argument --after-context=");
            else if (strcmp(arg, "any-line") == 0)
              flag_any_line = true;
            else if (strcmp(arg, "basic-regexp") == 0)
              flag_basic_regexp = true;
            else if (strncmp(arg, "before-context=", 15) == 0)
              flag_before_context = strtopos(arg + 15, "invalid argument --before-context=");
            else if (strcmp(arg, "binary") == 0)
              flag_binary = true;
            else if (strncmp(arg, "binary-files=", 13) == 0)
              flag_binary_files = arg + 13;
            else if (strcmp(arg, "break") == 0)
              flag_break = true;
            else if (strcmp(arg, "byte-offset") == 0)
              flag_byte_offset = true;
            else if (strcmp(arg, "color") == 0 || strcmp(arg, "colour") == 0)
              flag_color = "auto";
            else if (strncmp(arg, "color=", 6) == 0)
              flag_color = arg + 6;
            else if (strncmp(arg, "colour=", 7) == 0)
              flag_color = arg + 7;
            else if (strcmp(arg, "column-number") == 0)
              flag_column_number = true;
            else if (strncmp(arg, "context=", 8) == 0)
              flag_after_context = flag_before_context = strtopos(arg + 8, "invalid argument --context=");
            else if (strcmp(arg, "context") == 0)
              flag_after_context = flag_before_context = 2;
            else if (strcmp(arg, "count") == 0)
              flag_count = true;
            else if (strcmp(arg, "cpp") == 0)
              flag_cpp = true;
            else if (strcmp(arg, "csv") == 0)
              flag_csv = true;
            else if (strcmp(arg, "decompress") == 0)
              flag_decompress = true;
            else if (strcmp(arg, "dereference") == 0)
              flag_dereference = true;
            else if (strcmp(arg, "dereference-recursive") == 0)
              flag_directories = "dereference-recurse";
            else if (strncmp(arg, "devices=", 8) == 0)
              flag_devices = arg + 8;
            else if (strncmp(arg, "directories=", 12) == 0)
              flag_directories = arg + 12;
            else if (strcmp(arg, "empty") == 0)
              flag_empty = true;
            else if (strncmp(arg, "encoding=", 9) == 0)
              flag_encoding = arg + 9;
            else if (strncmp(arg, "exclude=", 8) == 0)
              flag_exclude.emplace_back(arg + 8);
            else if (strncmp(arg, "exclude-dir=", 12) == 0)
              flag_exclude_dir.emplace_back(arg + 12);
            else if (strncmp(arg, "exclude-from=", 13) == 0)
              flag_exclude_from.emplace_back(arg + 13);
            else if (strcmp(arg, "extended-regexp") == 0)
              flag_basic_regexp = false;
            else if (strncmp(arg, "file=", 5) == 0)
              flag_file.emplace_back(arg + 5);
            else if (strncmp(arg, "file-extensions=", 16) == 0)
              flag_file_extensions.emplace_back(arg + 16);
            else if (strncmp(arg, "file-magic=", 11) == 0)
              flag_file_magic.emplace_back(arg + 11);
            else if (strncmp(arg, "file-type=", 10) == 0)
              flag_file_types.emplace_back(arg + 10);
            else if (strcmp(arg, "files-with-match") == 0)
              flag_files_with_match = true;
            else if (strcmp(arg, "files-without-match") == 0)
              flag_files_without_match = true;
            else if (strcmp(arg, "fixed-strings") == 0)
              flag_fixed_strings = true;
            else if (strncmp(arg, "format=", 7) == 0)
              flag_format = arg + 7;
            else if (strncmp(arg, "format-begin=", 13) == 0)
              flag_format_begin = arg + 13;
            else if (strncmp(arg, "format-close=", 13) == 0)
              flag_format_close = arg + 13;
            else if (strncmp(arg, "format-end=", 11) == 0)
              flag_format_end = arg + 11;
            else if (strncmp(arg, "format-open=", 12) == 0)
              flag_format_open = arg + 12;
            else if (strcmp(arg, "free-space") == 0)
              flag_free_space = true;
            else if (strncmp(arg, "glob=", 5) == 0)
              flag_glob.emplace_back(arg + 5);
            else if (strncmp(arg, "group-separator=", 16) == 0)
              flag_group_separator = arg + 16;
            else if (strcmp(arg, "group-separator") == 0)
              flag_group_separator = "--";
            else if (strcmp(arg, "help") == 0)
              help();
            else if (strcmp(arg, "hex") == 0)
              flag_binary_files = "hex";
            else if (strcmp(arg, "hidden") == 0)
              flag_no_hidden = false;
            else if (strcmp(arg, "ignore-case") == 0)
              flag_ignore_case = true;
            else if (strncmp(arg, "ignore-files=", 13) == 0)
              flag_ignore_files.emplace_back(arg + 13);
            else if (strcmp(arg, "ignore-files") == 0)
              flag_ignore_files.emplace_back(DEFAULT_IGNORE_FILE);
            else if (strncmp(arg, "include=", 8) == 0)
              flag_include.emplace_back(arg + 8);
            else if (strncmp(arg, "include-dir=", 12) == 0)
              flag_include_dir.emplace_back(arg + 12);
            else if (strncmp(arg, "include-from=", 13) == 0)
              flag_include_from.emplace_back(arg + 13);
            else if (strcmp(arg, "initial-tab") == 0)
              flag_initial_tab = true;
            else if (strcmp(arg, "invert-match") == 0)
              flag_invert_match = true;
            else if (strncmp(arg, "jobs=", 4) == 0)
              flag_jobs = strtopos(arg + 4, "invalid argument --jobs=");
            else if (strcmp(arg, "json") == 0)
              flag_json = true;
            else if (strncmp(arg, "label=", 6) == 0)
              flag_label = arg + 6;
            else if (strcmp(arg, "label") == 0)
              flag_label = "";
            else if (strcmp(arg, "line-buffered") == 0)
              flag_line_buffered = true;
            else if (strcmp(arg, "line-number") == 0)
              flag_line_number = true;
            else if (strcmp(arg, "line-regexp") == 0)
              flag_line_regexp = true;
            else if (strcmp(arg, "match") == 0)
              flag_match = true;
            else if (strncmp(arg, "max-count=", 10) == 0)
              flag_max_count = strtopos(arg + 10, "invalid argument --max-count=");
            else if (strncmp(arg, "max-depth=", 10) == 0)
              flag_max_depth = strtopos(arg + 10, "invalid argument --max-depth=");
            else if (strncmp(arg, "max-files=", 10) == 0)
              flag_max_files = strtopos(arg + 10, "invalid argument --max-files=");
            else if (strncmp(arg, "max-mmap=", 9) == 0)
              flag_max_mmap = strtopos(arg + 9, "invalid argument --max-mmap=");
            else if (strncmp(arg, "min-mmap=", 9) == 0)
              flag_min_mmap = strtopos(arg + 9, "invalid argument --min-mmap=");
            else if (strncmp(arg, "min-steal=", 10) == 0)
              flag_min_steal = strtopos(arg + 10, "invalid argument --min-steal=");
            else if (strcmp(arg, "mmap") == 0)
              flag_max_mmap = MAX_MMAP_SIZE;
            else if (strcmp(arg, "no-dereference") == 0)
              flag_no_dereference = true;
            else if (strcmp(arg, "no-filename") == 0)
              flag_no_filename = true;
            else if (strcmp(arg, "no-group-separator") == 0)
              flag_group_separator = NULL;
            else if (strcmp(arg, "no-hidden") == 0)
              flag_no_hidden = true;
            else if (strcmp(arg, "no-messages") == 0)
              flag_no_messages = true;
            else if (strcmp(arg, "no-mmap") == 0)
              flag_max_mmap = 0;
            else if (strncmp(arg, "not-regexp=", 11) == 0)
              flag_not_regexp.emplace_back(arg + 11);
            else if (strcmp(arg, "null") == 0)
              flag_null = true;
            else if (strcmp(arg, "only-line-number") == 0)
              flag_only_line_number = true;
            else if (strcmp(arg, "only-matching") == 0)
              flag_only_matching = true;
            else if (strncmp(arg, "pager=", 6) == 0)
              flag_pager = arg + 6;
            else if (strncmp(arg, "pager", 5) == 0)
              flag_pager = DEFAULT_PAGER;
            else if (strcmp(arg, "perl-regexp") == 0)
              flag_perl_regexp = true;
            else if (strcmp(arg, "quiet") == 0 || strcmp(arg, "silent") == 0)
              flag_quiet = flag_no_messages = true;
            else if (strncmp(arg, "range=", 6) == 0)
              strtopos2(arg + 6, flag_min_line, flag_max_line, "invalid argument --range=");
            else if (strcmp(arg, "recursive") == 0)
              flag_directories = "recurse";
            else if (strncmp(arg, "regexp=", 7) == 0)
              flag_regexp.emplace_back(arg + 7);
            else if (strncmp(arg, "separator=", 10) == 0)
              flag_separator = arg + 10;
            else if (strcmp(arg, "separator") == 0)
              flag_separator = ":";
            else if (strcmp(arg, "smart-case") == 0)
              flag_smart_case = true;
            else if (strcmp(arg, "stats") == 0)
              flag_stats = true;
            else if (strncmp(arg, "tabs=", 5) == 0)
              flag_tabs = strtopos(arg + 5, "invalid argument --tabs=");
            else if (strcmp(arg, "text") == 0)
              flag_binary_files = "text";
            else if (strcmp(arg, "ungroup") == 0)
              flag_ungroup = true;
            else if (strcmp(arg, "version") == 0)
              version();
            else if (strcmp(arg, "with-filename") == 0)
              flag_with_filename = true;
            else if (strcmp(arg, "with-hex") == 0)
              flag_binary_files = "with-hex";
            else if (strcmp(arg, "word-regexp") == 0)
              flag_word_regexp = true;
            else if (strcmp(arg, "xml") == 0)
              flag_xml = true;
            else
              help("invalid option --", arg);
            is_grouped = false;
            break;

          case 'A':
            ++arg;
            if (*arg)
              flag_after_context = strtopos(&arg[*arg == '='], "invalid argument -A=");
            else if (++i < argc)
              flag_after_context = strtopos(argv[i], "invalid argument -A=");
            else
              help("missing NUM argument for option -A");
            is_grouped = false;
            break;

          case 'a':
            flag_binary_files = "text";
            break;

          case 'B':
            ++arg;
            if (*arg)
              flag_before_context = strtopos(&arg[*arg == '='], "invalid argument -B=");
            else if (++i < argc)
              flag_before_context = strtopos(argv[i], "invalid argument -B=");
            else
              help("missing NUM argument for option -B");
            is_grouped = false;
            break;

          case 'b':
            flag_byte_offset = true;
            break;

          case 'C':
            ++arg;
            if (*arg == '=' || isdigit(*arg))
            {
              flag_after_context = flag_before_context = strtopos(&arg[*arg == '='], "invalid argument -C=");
              is_grouped = false;
            }
            else
            {
              flag_after_context = flag_before_context = 2;
              --arg;
            }
            break;

          case 'c':
            flag_count = true;
            break;

          case 'D':
            ++arg;
            if (*arg)
              flag_devices = &arg[*arg == '='];
            else if (++i < argc)
              flag_devices = argv[i];
            else
              help("missing ACTION argument for option -D");
            is_grouped = false;
            break;

          case 'd':
            ++arg;
            if (*arg)
              flag_directories = &arg[*arg == '='];
            else if (++i < argc)
              flag_directories = argv[i];
            else
              help("missing ACTION argument for option -d");
            is_grouped = false;
            break;

          case 'E':
            flag_basic_regexp = false;
            break;

          case 'e':
            ++arg;
            if (*arg)
              flag_regexp.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_regexp.emplace_back(argv[i]);
            else
              help("missing PATTERN argument for option -e");
            is_grouped = false;
            break;

          case 'F':
            flag_fixed_strings = true;
            break;

          case 'f':
            ++arg;
            if (*arg)
              flag_file.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file.emplace_back(argv[i]);
            else
              help("missing FILE argument for option -f");
            is_grouped = false;
            break;

          case 'G':
            flag_basic_regexp = true;
            break;

          case 'g':
            ++arg;
            if (*arg)
              flag_glob.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_glob.emplace_back(argv[i]);
            else
              help("missing GLOB argument for option -g");
            is_grouped = false;
            break;

          case 'H':
            flag_with_filename = true;
            break;

          case 'h':
            flag_no_filename = true;
            break;

          case 'I':
            flag_binary_files = "without-matches";
            break;

          case 'i':
            flag_ignore_case = true;
            break;

          case 'J':
            ++arg;
            if (*arg)
              flag_jobs = strtopos(&arg[*arg == '='], "invalid argument -J=");
            else if (++i < argc)
              flag_jobs = strtopos(argv[i], "invalid argument -J=");
            else
              help("missing NUM argument for option -J");
            is_grouped = false;
            break;

          case 'j':
            flag_smart_case = true;
            break;

          case 'K':
            ++arg;
            if (*arg)
              strtopos2(&arg[*arg == '='], flag_min_line, flag_max_line, "invalid argument -K=");
            else if (++i < argc)
              strtopos2(argv[i], flag_min_line, flag_max_line, "invalid argument -K=");
            else
              help("missing NUM argument for option -K");
            is_grouped = false;
            break;

          case 'k':
            flag_column_number = true;
            break;

          case 'L':
            flag_files_without_match = true;
            break;

          case 'l':
            flag_files_with_match = true;
            break;

          case 'M':
            ++arg;
            if (*arg)
              flag_file_magic.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_magic.emplace_back(argv[i]);
            else
              help("missing MAGIC argument for option -M");
            is_grouped = false;
            break;

          case 'm':
            ++arg;
            if (*arg)
              flag_max_count = strtopos(&arg[*arg == '='], "invalid argument -m=");
            else if (++i < argc)
              flag_max_count = strtopos(argv[i], "invalid argument -m=");
            else
              help("missing NUM argument for option -m");
            is_grouped = false;
            break;

          case 'N':
            ++arg;
            if (*arg)
              flag_not_regexp.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_not_regexp.emplace_back(argv[i]);
            else
              help("missing PATTERN argument for option -N");
            is_grouped = false;
            break;

          case 'n':
            flag_line_number = true;
            break;

          case 'O':
            ++arg;
            if (*arg)
              flag_file_extensions.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_extensions.emplace_back(argv[i]);
            else
              help("missing EXTENSIONS argument for option -O");
            is_grouped = false;
            break;

          case 'o':
            flag_only_matching = true;
            break;

          case 'P':
            flag_perl_regexp = true;
            break;

          case 'p':
            flag_no_dereference = true;
            break;

          case 'Q':
            ++arg;
            if (*arg)
              flag_encoding = &arg[*arg == '='];
            else if (++i < argc)
              flag_encoding = argv[i];
            else
              help("missing ENCODING argument for option -:");
            is_grouped = false;
            break;

          case 'q':
            flag_quiet = true;
            break;

          case 'R':
            flag_directories = "dereference-recurse";
            break;

          case 'r':
            flag_directories = "recurse";
            break;

          case 'S':
            flag_dereference = true;
            break;

          case 's':
            flag_no_messages = true;
            break;

          case 'T':
            flag_initial_tab = true;
            break;

          case 't':
            ++arg;
            if (*arg)
              flag_file_types.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_types.emplace_back(argv[i]);
            else
              help("missing TYPES argument for option -t");
            is_grouped = false;
            break;

          case 'U':
            flag_binary = true;
            break;

          case 'u':
            flag_ungroup = true;
            break;

          case 'V':
            version();
            break;

          case 'v':
            flag_invert_match = true;
            break;

          case 'W':
            flag_binary_files = "with-hex";
            break;

          case 'w':
            flag_word_regexp = true;
            break;

          case 'X':
            flag_binary_files = "hex";
            break;

          case 'x':
            flag_line_regexp = true;
            break;

          case 'Y':
            flag_empty = true;
            break;

          case 'y':
            flag_any_line = true;
            break;

          case 'Z':
            flag_null = true;
            break;

          case 'z':
            flag_decompress = true;
            break;

          default:
            help("invalid option -", arg);
        }
      }
    }
    else if (options && strcmp(arg, "-") == 0)
    {
      // read standard input
      flag_stdin = true;
    }
    else if (pattern == NULL && flag_file.empty())
    {
      // no regex pattern specified yet, so assume it is PATTERN
      pattern = arg;
    }
    else
    {
      // otherwise add the file argument to the list of FILE files
      files.emplace_back(arg);
    }
  }

#ifndef OS_WIN
  // ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

#ifndef HAVE_LIBZ
  // -z: but we don't have libz
  if (flag_decompress)
    help("option -z is not available in this build configuration of ugrep");
#endif

  // -t list: list table of types and exit
  if (flag_file_types.size() == 1 && flag_file_types[0] == "list")
  {
    std::cerr << std::setw(12) << "FILE TYPE" << "   FILE NAME -O EXTENSIONS AND FILE SIGNATURE -M 'MAGIC BYTES'\n";

    for (int i = 0; type_table[i].type != NULL; ++i)
    {
      std::cerr << std::setw(12) << type_table[i].type << " = -O " << type_table[i].extensions << '\n';
      if (type_table[i].magic)
        std::cerr << std::setw(19) << "-M '" << type_table[i].magic << "'\n";
    }

    exit(EXIT_ERROR);
  }

  // --binary-files: normalize by assigning flags
  if (strcmp(flag_binary_files, "without-matches") == 0)
    flag_binary_without_matches = true;
  else if (strcmp(flag_binary_files, "text") == 0)
    flag_text = true;
  else if (strcmp(flag_binary_files, "hex") == 0)
    flag_hex = true;
  else if (strcmp(flag_binary_files, "with-hex") == 0)
    flag_with_hex = true;
  else if (strcmp(flag_binary_files, "binary") != 0)
    help("invalid argument --binary-files=TYPE, valid arguments are 'binary', 'without-match', 'text', 'hex', and 'with-hex'");

  // --match-all is the same as specifying an '' empty pattern argument
  if (flag_match)
    flag_regexp.emplace_back("");

  // the regex compiled from PATTERN
  std::string regex;

  // regex PATTERN specified
  if (pattern != NULL)
  {
    // if not --match-all, then add pattern to the front else add to the front of FILE args
    if (flag_regexp.empty())
      flag_regexp.emplace_back(pattern);
    else
      files.insert(files.begin(), pattern);
  }
  
  // if no regex pattern is specified and no -f file then exit with usage message
  if (flag_regexp.empty() && flag_file.empty())
    help("");

  // -F: make newline-separated lines in regex literal with \Q and \E
  const char *Q = flag_fixed_strings ? "\\Q" : "";
  const char *E = flag_fixed_strings ? "\\E|" : "|";

  // combine all -e PATTERN into a single regex string for matching
  for (auto& pattern : flag_regexp)
  {
    // empty PATTERN matches everything
    if (pattern.empty())
    {
      // pattern ".*\n?|" could be used throughout without flag_empty = true, but this garbles output for -o, -H, -n, etc.
      if (flag_line_regexp)
      {
        regex.append("^$|");
      }
      else if (flag_hex)
      {
        regex.append(".*\n?|");

        // indicate we're matching everything
        flag_match = true;
      }
      else
      {
        regex.append(".*|");

        // indicate we're matching everything
        flag_match = true;
      }

      // include empty pattern matches in the results
      flag_empty = true;
    }
    else
    {
      // split newline-separated regex up into alternations
      size_t from = 0;
      size_t to;

      // split regex at newlines, for -F add \Q \E to each string, separate by |
      while ((to = pattern.find('\n', from)) != std::string::npos)
      {
        if (from < to)
        {
          size_t len = to - from - (pattern[to - 1] == '\r');
          if (len > 0)
            regex.append(Q).append(pattern.substr(from, to - from - (pattern[to - 1] == '\r'))).append(E);
        }
        from = to + 1;
      }

      if (from < pattern.size())
        regex.append(Q).append(pattern.substr(from)).append(E);

      if (pattern == "^$")
        flag_empty = true; // we're matching empty lines, so enable -Y
    }
  }

  // the regex compiled from -N PATTERN
  std::string not_regex;

  // append -N PATTERN to regex as negative patterns
  for (auto& pattern : flag_not_regexp)
  {
    if (!pattern.empty())
    {
      // split newline-separated regex up into alternations
      size_t from = 0;
      size_t to;

      // split regex at newlines, for -F add \Q \E to each string, separate by |
      while ((to = pattern.find('\n', from)) != std::string::npos)
      {
        if (from < to)
        {
          size_t len = to - from - (pattern[to - 1] == '\r');
          if (len > 0)
            not_regex.append(Q).append(pattern.substr(from, to - from - (pattern[to - 1] == '\r'))).append(E);
        }
        from = to + 1;
      }

      if (from < pattern.size())
        not_regex.append(Q).append(pattern.substr(from)).append(E);
    }
  }

  // -x or -w: apply to -N PATTERN
  if (!not_regex.empty())
  {
    // remove the ending '|' from the |-concatenated regexes in the regex string
    not_regex.pop_back();

    // -x or -w
    if (flag_line_regexp)
      not_regex.insert(0, "^(").append(")$"); // make the regex line-anchored
    else if (flag_word_regexp)
      not_regex.insert(0, "\\<(").append(")\\>"); // make the regex word-anchored

    // construct negative (?^PATTERN)
    not_regex.insert(0, "(?^").push_back(')');
  }

  // -x or -w: apply to PATTERN then disable -x, -w, -F for patterns in -f FILE when PATTERN or -e PATTERN is specified
  if (!regex.empty())
  {
    // remove the ending '|' from the |-concatenated regexes in the regex string
    regex.pop_back();

    if (regex != "^$")
    {
      // -x or -w
      if (flag_line_regexp)
        regex.insert(0, "^(").append(")$"); // make the regex line-anchored
      else if (flag_word_regexp)
        regex.insert(0, "\\<(").append(")\\>"); // make the regex word-anchored
    }

    // -x and -w do not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
    flag_line_regexp = false;
    flag_word_regexp = false;

    // -F does not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
    Q = "";
    E = "|";
  }

  // combine regexes
  if (regex.empty())
    regex.swap(not_regex);
  else if (!not_regex.empty())
    regex.append("|").append(not_regex);

  // -P disables -G
  if (flag_perl_regexp)
    flag_basic_regexp = false;

  // -j: case insensitive search if regex does not contain a capital letter
  if (flag_smart_case)
  {
    flag_ignore_case = true;

    for (size_t i = 0; i < regex.size(); ++i)
    {
      if (regex[i] == '\\')
      {
        ++i;
      }
      else if (isupper(regex[i]))
      {
        flag_ignore_case = false;
        break;
      }
    }
  }

  // -f: get patterns from file
  if (!flag_file.empty())
  {
    // add an ending '|' to the regex to concatenate sub-expressions
    if (!regex.empty())
      regex.push_back('|');

    // -f: read patterns from the specified file or files
    for (auto& filename : flag_file)
    {
      FILE *file = NULL;

      if (filename == "-")
        file = stdin;
      else if (fopen_s(&file, filename.c_str(), "r") != 0)
        file = NULL;

      if (file == NULL)
      {
        // could not open, try GREP_PATH environment variable
        char *env_grep_path = NULL;
        dupenv_s(&env_grep_path, "GREP_PATH");

        if (env_grep_path != NULL)
        {
          std::string path_file(env_grep_path);
          path_file.append(PATHSEPSTR).append(filename);

          if (fopen_s(&file, path_file.c_str(), "r") != 0)
            file = NULL;

          free(env_grep_path);
        }
      }

#ifdef GREP_PATH
      if (file == NULL)
      {
        std::string path_file(GREP_PATH);
        path_file.append(PATHSEPSTR).append(filename);

        if (fopen_s(&file, path_file.c_str(), "r") != 0)
          file = NULL;
      }
#endif

      if (file == NULL)
        error("cannot read", filename.c_str());

      reflex::BufferedInput input(file);
      std::string line;
      size_t lineno = 0;

      while (true)
      {
        // read the next line
        if (getline(input, line))
          break;

        ++lineno;

        trim(line);

        // add line to the regex if not empty
        if (!line.empty())
        {
          // enable -o when the first line is ###-o, skip ###-comments
          if (lineno == 1 && line == "###-o")
            flag_only_matching = true;
          else if (line.substr(0, 3) != "###")
            regex.append(Q).append(line).append(E);
        }
      }

      if (file != stdin)
        fclose(file);
    }

    // remove the ending '|' from the |-concatenated regexes in the regex string
    regex.pop_back();

    // -x or -w
    if (flag_line_regexp)
      regex.insert(0, "^(").append(")$"); // make the regex line-anchored
    else if (flag_word_regexp)
      regex.insert(0, "\\<(").append(")\\>"); // make the regex word-anchored
  }

  // -y: disable -A, -B, and -C
  if (flag_any_line)
    flag_after_context = flag_before_context = 0;

  // -A, -B, or -C: disable -o
  if (flag_after_context > 0 || flag_before_context > 0)
    flag_only_matching = false;

  // -v or -y: disable -o and -u
  if (flag_invert_match || flag_any_line)
    flag_only_matching = flag_ungroup = false;

  // normalize -R (--dereference-recurse) option
  if (strcmp(flag_directories, "dereference-recurse") == 0)
  {
    flag_directories = "recurse";
    flag_dereference = true;
  }

  // -D: check ACTION value
  if (strcmp(flag_devices, "read") == 0)
    flag_devices_action = READ;
  else if (strcmp(flag_devices, "skip") == 0)
    flag_devices_action = SKIP;
  else
    help("invalid argument --devices=ACTION, valid arguments are 'read' and 'skip'");

  // -d: check ACTION value
  if (strcmp(flag_directories, "read") == 0)
    flag_directories_action = READ;
  else if (strcmp(flag_directories, "recurse") == 0)
    flag_directories_action = RECURSE;
  else if (strcmp(flag_directories, "skip") == 0)
    flag_directories_action = SKIP;
  else
    help("invalid argument --directories=ACTION, valid arguments are 'read', 'recurse', 'dereference-recurse', and 'skip'");

  // normalize -p (--no-dereference) and -S (--dereference) options, -p taking priority over -S
  if (flag_no_dereference)
    flag_dereference = false;

  // normalize --cpp, --csv, --json, --xml
  if (flag_cpp)
  {
    flag_format_begin = "const struct grep {\n  const char *file;\n  size_t line;\n  size_t column;\n  size_t offset;\n  const char *match;\n} matches[] = {\n";
    flag_format_open  = "  // %f\n";
    flag_format       = "  { %h, %n, %k, %b, %C },\n%u";
    flag_format_close = "  \n";
    flag_format_end   = "  { NULL, 0, 0, 0, NULL }\n};\n";
  }
  else if (flag_csv)
  {
    flag_format       = "%[,]$%H%N%K%B%V\n%u";
  }
  else if (flag_json)
  {
    flag_format_begin = "[";
    flag_format_open  = "%,\n  {\n    %[,\n    ]$%[\"file\": ]H\"matches\": [";
    flag_format       = "%,\n      { %[, ]$%[\"line\": ]N%[\"column\": ]K%[\"offset\": ]B\"match\": %J }%u";
    flag_format_close = "\n    ]\n  }";
    flag_format_end   = "\n]\n";
  }
  else if (flag_xml)
  {
    flag_format_begin = "<grep>\n";
    flag_format_open  = "  <file%[]$%[ name=]H>\n";
    flag_format       = "    <match%[\"]$%[ line=\"]N%[ column=\"]K%[ offset=\"]B>%X</match>\n%u";
    flag_format_close = "  </file>\n";
    flag_format_end   = "</grep>\n";
  }

  // is output sent to a color TTY, to a pager, or to /dev/null?
  if (!flag_quiet)
  {
    // check if standard output is a TTY
    tty_term = isatty(STDOUT_FILENO) != 0;

#ifndef OS_WIN

    // --pager: if output is to a TTY then page through the results
    if (tty_term && flag_pager != NULL)
    {
      output = popen(flag_pager, "w");
      if (output == NULL)
        error("cannot open pipe to pager", flag_pager);

      // enable --break
      flag_break = true;

      // enable --line-buffered to flush output to the pager immediately
      flag_line_buffered = true;
    }
    else
    {
      output_stat_result = fstat(STDOUT_FILENO, &output_stat) == 0;
      output_stat_regular = output_stat_result && S_ISREG(output_stat.st_mode);

      // if output is sent to /dev/null, then enable -q (similar cheat as GNU grep!)
      struct stat dev_null_stat;
      if (output_stat_result &&
          S_ISCHR(output_stat.st_mode) &&
          stat("/dev/null", &dev_null_stat) == 0 &&
          output_stat.st_dev == dev_null_stat.st_dev &&
          output_stat.st_ino == dev_null_stat.st_ino)
      {
        flag_quiet = true;
      }
    }

#endif

    // --color: (re)set flag_color depending on color_term and TTY output
    if (flag_color != NULL)
    {
      if (strcmp(flag_color, "never") == 0)
      {
        flag_color = NULL;
      }
      else
      {
#ifdef OS_WIN

        if (tty_term)
        {
          // blindly assume that we have a color terminal on Windows if isatty() is true
          color_term = true;

#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
          HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
          DWORD dwMode;
          GetConsoleMode(hOutput, &dwMode);
          dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
          SetConsoleMode(hOutput, dwMode);
#endif

        }

#else

        // check whether we have a color terminal
        if (tty_term)
        {
          const char *term = getenv("TERM");
          if (term &&
              (strstr(term, "ansi") != NULL ||
               strstr(term, "xterm") != NULL ||
               strstr(term, "color") != NULL))
            color_term = true;
        }

#endif

        if (strcmp(flag_color, "auto") == 0)
        {
          if (!color_term)
            flag_color = NULL;
        }
        else if (strcmp(flag_color, "always") != 0)
        {
          help("invalid argument --color=WHEN, valid arguments are 'never', 'always', and 'auto'");
        }

        if (flag_color != NULL)
        {
          // get GREP_COLOR and GREP_COLORS
          char *env_grep_color = NULL;
          dupenv_s(&env_grep_color, "GREP_COLOR");
          char *env_grep_colors = NULL;
          dupenv_s(&env_grep_colors, "GREP_COLORS");
          const char *grep_colors = env_grep_colors;

          // if GREP_COLOR is defined, use it to set mt= default value (overridden by GREP_COLORS mt=, ms=, mc=)
          if (env_grep_color != NULL)
            set_color(std::string("mt=").append(env_grep_color).c_str(), "mt", color_mt);
          else if (grep_colors == NULL)
            grep_colors = DEFAULT_GREP_COLORS;

          if (grep_colors != NULL)
          {
            // parse GREP_COLORS
            set_color(grep_colors, "sl", color_sl); // selected line
            set_color(grep_colors, "cx", color_cx); // context line
            set_color(grep_colors, "mt", color_mt); // matched text in any line
            set_color(grep_colors, "ms", color_ms); // matched text in selected line
            set_color(grep_colors, "mc", color_mc); // matched text in a context line
            set_color(grep_colors, "fn", color_fn); // file name
            set_color(grep_colors, "ln", color_ln); // line number
            set_color(grep_colors, "cn", color_cn); // column number
            set_color(grep_colors, "bn", color_bn); // byte offset
            set_color(grep_colors, "se", color_se); // separator

            // -v: if rv in GREP_COLORS then swap the sl and cx colors
            if (flag_invert_match && strstr(grep_colors, "rv") != NULL)
            {
              char color_tmp[COLORLEN];
              copy_color(color_tmp, color_sl);
              copy_color(color_sl, color_cx);
              copy_color(color_cx, color_tmp);
            }

            // if pattern is empty to match all, matches are shown as selected lines
            if (flag_match)
            {
              copy_color(color_ms, color_sl);
              copy_color(color_mc, color_cx);
            }
            else
            {
              // if ms= is not specified, use the mt= value
              if (*color_ms == '\0')
                copy_color(color_ms, color_mt);

              // if mc= is not specified, use the mt= value
              if (*color_mc == '\0')
                copy_color(color_mc, color_mt);
            }
          }

          color_off = "\033[0m";

          if (isatty(STDERR_FILENO))
          {
            color_high    = "\033[1m";
            color_error   = "\033[1;31m";
            color_warning = "\033[1;35m";
            color_message = "\033[1;36m";
          }

          if (env_grep_color != NULL)
            free(env_grep_color);
          if (env_grep_colors != NULL)
            free(env_grep_colors);
        }
      }
    }
  }

  // -Q: parse ENCODING value
  if (flag_encoding != NULL)
  {
    int i;

    // scan the format_table[] for a matching encoding
    for (i = 0; format_table[i].format != NULL; ++i)
      if (strcmp(flag_encoding, format_table[i].format) == 0)
        break;

    if (format_table[i].format == NULL)
      help("invalid argument --encoding=ENCODING");

    // encoding is the file encoding used by all input files, if no BOM is present
    flag_encoding_type = format_table[i].encoding;
  }

  // -t: parse TYPES and access type table to add -O (--file-extensions) and -M (--file-magic) values
  for (auto& types : flag_file_types)
  {
    size_t from = 0;

    while (true)
    {
      size_t to = types.find(',', from);
      size_t size = (to == std::string::npos ? types.size() : to) - from;
      std::string type(types.substr(from, size));

      size_t i;

      // scan the type_table[] for a matching type
      for (i = 0; type_table[i].type != NULL; ++i)
        if (type == type_table[i].type)
          break;

      if (type_table[i].type == NULL)
        help("invalid argument --file-type=TYPE, to list the valid values use -tlist");

      flag_file_extensions.emplace_back(type_table[i].extensions);

      if (type_table[i].magic != NULL)
        flag_file_magic.emplace_back(type_table[i].magic);

      if (to == std::string::npos)
        break;

      from = to + 1;
    }
  }

  // -g, --glob: add globs to --include and --exclude
  for (auto& i : flag_glob)
  {
    bool negate = !i.empty() && i.front() == '!';
    if (negate)
      flag_exclude.emplace_back(i.substr(1));
    else
      flag_include.emplace_back(i);
  }

  // -O: add extensions as globs to the --include list
  for (auto& extensions : flag_file_extensions)
  {
    size_t from = 0;
    std::string glob;

    while (true)
    {
      size_t to = extensions.find(',', from);
      size_t size = (to == std::string::npos ? extensions.size() : to) - from;

      flag_include.emplace_back(glob.assign("*.").append(extensions.substr(from, size)));

#ifdef HAVE_LIBZ
      // -z: add globs to search compressed files and tarballs
      if (flag_decompress)
      {
        const char *zextensions[] = {
          ".gz", ".Z", ".zip", ".ZIP",
#ifdef HAVE_LIBBZ2
          ".bz", ".bz2", ".bzip2",
#endif
#ifdef HAVE_LIBLZMA
          ".lzma", ".xz",
#endif
          NULL
        };

        for (size_t i = 0; zextensions[i] != NULL; ++i)
          flag_include.emplace_back(glob.assign("*.").append(extensions.substr(from, size)).append(zextensions[i]));
      }
#endif

      if (to == std::string::npos)
        break;

      from = to + 1;
    }
  }

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
  // -z with -M or -O/--include: add globs to search tarballs
  if (flag_decompress && (!flag_file_magic.empty() || !flag_include.empty()))
  {
    flag_include.emplace_back("*.cpio");
    flag_include.emplace_back("*.pax");
    flag_include.emplace_back("*.tar");
    flag_include.emplace_back("*.zip");
    flag_include.emplace_back("*.ZIP");

    flag_include.emplace_back("*.cpio.gz");
    flag_include.emplace_back("*.pax.gz");
    flag_include.emplace_back("*.tar.gz");
    flag_include.emplace_back("*.taz");
    flag_include.emplace_back("*.tgz");
    flag_include.emplace_back("*.tpz");

    flag_include.emplace_back("*.cpio.Z");
    flag_include.emplace_back("*.pax.Z");
    flag_include.emplace_back("*.tar.Z");

    flag_include.emplace_back("*.cpio.zip");
    flag_include.emplace_back("*.pax.zip");
    flag_include.emplace_back("*.tar.zip");

#ifdef HAVE_LIBBZ2
    flag_include.emplace_back("*.cpio.bz");
    flag_include.emplace_back("*.pax.bz");
    flag_include.emplace_back("*.tar.bz");
    flag_include.emplace_back("*.cpio.bz2");
    flag_include.emplace_back("*.pax.bz2");
    flag_include.emplace_back("*.tar.bz2");
    flag_include.emplace_back("*.cpio.bzip2");
    flag_include.emplace_back("*.pax.bzip2");
    flag_include.emplace_back("*.tar.bzip2");
    flag_include.emplace_back("*.tb2");
    flag_include.emplace_back("*.tbz");
    flag_include.emplace_back("*.tbz2");
    flag_include.emplace_back("*.tz2");
#endif

#ifdef HAVE_LIBLZMA
    flag_include.emplace_back("*.cpio.lzma");
    flag_include.emplace_back("*.pax.lzma");
    flag_include.emplace_back("*.tar.lzma");
    flag_include.emplace_back("*.cpio.xz");
    flag_include.emplace_back("*.pax.xz");
    flag_include.emplace_back("*.tar.xz");
    flag_include.emplace_back("*.tlz");
    flag_include.emplace_back("*.txz");
#endif
  }
#endif
#endif

  // -M: file signature "magic bytes" MAGIC regex
  std::string signature;

  // -M: combine to create a signature regex from MAGIC
  for (auto& magic : flag_file_magic)
  {
    if (!signature.empty())
      signature.push_back('|');
    signature.append(magic);
  }

  // --exclude-from: add globs to the --exclude and --exclude-dir lists
  for (auto& i : flag_exclude_from)
  {
    if (!i.empty())
    {
      FILE *file = NULL;

      if (i == "-")
        file = stdin;
      else if (fopen_s(&file, i.c_str(), "r") != 0)
        error("cannot read", i.c_str());

      extend(file, flag_exclude, flag_exclude_dir, flag_not_exclude, flag_not_exclude_dir);

      if (file != stdin)
        fclose(file);
    }
  }

  // --include-from: add globs to the --include and --include-dir lists
  for (auto& i : flag_include_from)
  {
    if (!i.empty())
    {
      FILE *file = NULL;

      if (i == "-")
        file = stdin;
      else if (fopen_s(&file, i.c_str(), "r") != 0)
        error("cannot read", i.c_str());

      extend(file, flag_include, flag_include_dir, flag_not_include, flag_not_include_dir);

      if (file != stdin)
        fclose(file);
    }
  }

  // if no FILE specified and reading standard input from a TTY but one or more of -g, -O, -m, -t, --include, --include-dir, --exclude, --exclude dir: enable -dRECURSE
  if (!flag_stdin && files.empty() && (!flag_include.empty() || !flag_include_dir.empty() || !flag_exclude.empty() || !flag_exclude_dir.empty() || !flag_file_magic.empty()) && isatty(STDIN_FILENO))
    flag_directories_action = RECURSE;

  // display file name if more than one input file is specified or options -R, -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (flag_directories_action == RECURSE || files.size() > 1 || (flag_stdin && !files.empty())))
    flag_with_filename = true;

  // --only-line-number implies -n
  if (flag_only_line_number)
    flag_line_number = true;

  // if no display options -H, -n, -k, -b are set, enable --no-labels to suppress labels for speed
  if (!flag_with_filename && !flag_line_number && !flag_column_number && !flag_byte_offset)
    flag_no_header = true;

  // -q: we only need to find one matching file and we're done
  if (flag_quiet)
  {
    flag_max_files = 1;

    // -q overrides -l and -L
    flag_files_with_match = false;
    flag_files_without_match = false;
  }

  // -L: enable -l and flip -v i.e. -L=-lv and -l=-Lv
  if (flag_files_without_match)
  {
    flag_files_with_match = true;
    flag_invert_match = !flag_invert_match;
  }

  // -l or -L: enable -H, disable -c
  if (flag_files_with_match)
  {
    flag_with_filename = true;
    flag_count = false;
  }

  // -J: when not set the default is the number of cores (or hardware threads), limited to MAX_JOBS
  if (flag_jobs == 0)
  {
    unsigned int cores = std::thread::hardware_concurrency();
    unsigned int concurrency = cores > 2 ? cores : 2;
    flag_jobs = std::min(concurrency, MAX_JOBS);
  }

  // set the number of threads to the number of files or when recursing to the value of -J, --jobs
  if (flag_directories_action == RECURSE)
    threads = flag_jobs;
  else
    threads = std::min(files.size() + flag_stdin, flag_jobs);

  // if no FILE specified then read standard input, unless recursive searches are specified
  if (files.empty() && flag_directories_action != RECURSE)
    flag_stdin = true;

  // -M: create a magic matcher for the MAGIC regex signature to match file signatures with magic.scan()
  reflex::Matcher magic;

  try
  {
    magic_pattern.assign(signature, "r");
    magic.pattern(magic_pattern);
  }

  catch (reflex::regex_error& error)
  {
    if (!flag_no_messages)
      std::cerr << "option -M:\n" << error.what();

    exit(EXIT_ERROR);
  }

  // --format-begin
  if (!flag_quiet && flag_format_begin != NULL)
    format(flag_format_begin, 0);

  try
  {
    // -U: set flags to convert regex to Unicode
    reflex::convert_flag_type convert_flags = flag_binary ? reflex::convert_flag::none : reflex::convert_flag::unicode;

    // -G: convert basic regex (BRE) to extended regex (ERE)
    if (flag_basic_regexp)
      convert_flags |= reflex::convert_flag::basic;

    // set reflex::Pattern options to raise exceptions and to enable multiline mode
    std::string pattern_options("(?m");

    // -i: case-insensitive reflex::Pattern option, applies to ASCII only
    if (flag_ignore_case)
      pattern_options.append("i");

    // --free-space: this is needed to check free-space conformance by the converter
    if (flag_free_space)
    {
      convert_flags |= reflex::convert_flag::freespace;
      pattern_options.append("x");
    }

    // prepend the pattern options (?m...) to the regex
    pattern_options.append(")");
    regex = pattern_options + regex;

    // reflex::Matcher options
    std::string matcher_options;

    // -Y: permit empty pattern matches
    if (flag_empty)
      matcher_options.append("N");

    // --tabs: set reflex::Matcher option T to NUM tab size
    if (flag_tabs)
    {
      if (flag_tabs == 1 || flag_tabs == 2 || flag_tabs == 4 || flag_tabs == 8)
        matcher_options.append("T=").push_back(static_cast<char>(flag_tabs) + '0');
      else
        help("invalid argument -T=NUM, --tabs=NUM, valid arguments are 1, 2, 4, or 8");
    }

    // -P: Perl matching with Boost.Regex
    if (flag_perl_regexp)
    {
#ifdef HAVE_BOOST_REGEX
      // construct the Boost.Regex NFA-based Perl pattern matcher
      std::string pattern(reflex::BoostPerlMatcher::convert(regex, convert_flags));
      reflex::BoostPerlMatcher matcher(pattern, reflex::Input(), matcher_options.c_str());

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher);
        ugrep(magic, grep, files);
      }
      else
      {
        Grep grep(output, &matcher);
        ugrep(magic, grep, files);
      }
#else
      help("option -P is not available in this build configuration of ugrep");
#endif
    }
    else
    {
      // construct the RE/flex DFA pattern matcher and start matching files
      reflex::Pattern pattern(reflex::Matcher::convert(regex, convert_flags), "r");
      reflex::Matcher matcher(pattern, reflex::Input(), matcher_options.c_str());

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher);
        ugrep(magic, grep, files);
      }
      else
      {
        Grep grep(output, &matcher);
        ugrep(magic, grep, files);
      }
    }
  }

  catch (reflex::regex_error& error)
  {
    abort("error: ", error.what());
  }

#ifdef HAVE_BOOST_REGEX
  catch (boost::regex_error& error)
  {
    if (!flag_no_messages)
    {
      const char *message;
      reflex::regex_error_type code;

      switch (error.code())
      {
        case boost::regex_constants::error_collate:
          message = "Boost.Regex error: invalid collating element in a [[.name.]] block\n";
          code = reflex::regex_error::invalid_collating;
          break;
        case boost::regex_constants::error_ctype:
          message = "Boost.Regex error: invalid character class name in a [[:name:]] block\n";
          code = reflex::regex_error::invalid_class;
          break;
        case boost::regex_constants::error_escape:
          message = "Boost.Regex error: invalid or trailing escape\n";
          code = reflex::regex_error::invalid_escape;
          break;
        case boost::regex_constants::error_backref:
          message = "Boost.Regex error: back-reference to a non-existent marked sub-expression\n";
          code = reflex::regex_error::invalid_backreference;
          break;
        case boost::regex_constants::error_brack:
          message = "Boost.Regex error: invalid character set [...]\n";
          code = reflex::regex_error::invalid_class;
          break;
        case boost::regex_constants::error_paren:
          message = "Boost.Regex error: mismatched ( and )\n";
          code = reflex::regex_error::mismatched_parens;
          break;
        case boost::regex_constants::error_brace:
          message = "Boost.Regex error: mismatched { and }\n";
          code = reflex::regex_error::mismatched_braces;
          break;
        case boost::regex_constants::error_badbrace:
          message = "Boost.Regex error: invalid contents of a {...} block\n";
          code = reflex::regex_error::invalid_repeat;
          break;
        case boost::regex_constants::error_range:
          message = "Boost.Regex error: character range is invalid, for example [d-a]\n";
          code = reflex::regex_error::invalid_class_range;
          break;
        case boost::regex_constants::error_space:
          message = "Boost.Regex error: out of memory\n";
          code = reflex::regex_error::exceeds_limits;
          break;
        case boost::regex_constants::error_badrepeat:
          message = "Boost.Regex error: attempt to repeat something that cannot be repeated\n";
          code = reflex::regex_error::invalid_repeat;
          break;
        case boost::regex_constants::error_complexity:
          message = "Boost.Regex error: the expression became too complex to handle\n";
          code = reflex::regex_error::exceeds_limits;
          break;
        case boost::regex_constants::error_stack:
          message = "Boost.Regex error: out of program stack space\n";
          code = reflex::regex_error::exceeds_limits;
          break;
        default:
          message = "Boost.Regex error: bad pattern\n";
          code = reflex::regex_error::invalid_syntax;
      }

      abort(message, reflex::regex_error(code, regex, error.position() + 1).what());
    }

    exit(EXIT_ERROR);
  }

  catch (boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<std::runtime_error> >& error)
  {
    abort("Boost.Regex error: ", error.what());
  }
#endif

  catch (std::runtime_error& error)
  {
    abort("Exception: ", error.what());
  }

  // --format-end
  if (!flag_quiet && flag_format_end != NULL)
    format(flag_format_end, stats.found_files());

  // --stats: display stats when we're done
  if (flag_stats)
    stats.report();

#ifndef OS_WIN
  if (output != stdout)
    pclose(output);
#endif

  return stats.found_any_file() ? EXIT_OK : EXIT_FAIL;
}

// search the specified files or standard input for pattern matches
void ugrep(reflex::Matcher& magic, Grep& grep, std::vector<const char*>& files)
{
  if (!flag_stdin && files.empty())
  {
    recurse(1, magic, grep, ".");
  }
  else
  {
    // read each input file to find pattern matches
    if (flag_stdin)
    {
      stats.score_file();

      // search standard input
      grep.search(NULL);
    }

    for (auto file : files)
    {
      // stop after finding max-files matching files
      if (flag_max_files > 0 && stats.found_files() >= flag_max_files)
        break;

      // stop when output is blocked
      if (grep.out.eof)
        break;

      // search file or directory, get the basename from the file argument first
      const char *basename = strrchr(file, PATHSEPCHR);

      if (basename != NULL)
        ++basename;
      else
        basename = file;

      find(1, magic, grep, file, basename, DIRENT_TYPE_UNKNOWN, 0, !flag_no_dereference);
    }
  }
}

// search file or directory for pattern matches
void find(size_t level, reflex::Matcher& magic, Grep& grep, const char *pathname, const char *basename, int type, ino_t inode, bool is_argument_dereference)
{
  if (*basename == '.' && flag_no_hidden)
    return;

#ifdef OS_WIN

  DWORD attr = GetFileAttributesA(pathname);

  if (attr == INVALID_FILE_ATTRIBUTES)
  {
    errno = ENOENT;
    warning("cannot read", pathname);
    return;
  }

  if (flag_no_hidden && ((attr & FILE_ATTRIBUTE_HIDDEN) || (attr & FILE_ATTRIBUTE_SYSTEM)))
    return;

  if ((attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    if (flag_directories_action == READ)
    {
      // directories cannot be read actually, so grep produces a warning message (errno is not set)
      is_directory(pathname);
      return;
    }

    if (flag_directories_action == RECURSE)
    {
      // check for --exclude-dir and --include-dir constraints if pathname != "."
      if (strcmp(pathname, ".") != 0)
      {
        // do not exclude directories that are reversed by ! negation
        bool negate = false;
        for (auto& glob : flag_not_exclude_dir)
          if ((negate = glob_match(pathname, basename, glob.c_str())))
            break;

        if (!negate)
        {
          // exclude directories whose basename matches any one of the --exclude-dir globs
          for (auto& glob : flag_exclude_dir)
            if (glob_match(pathname, basename, glob.c_str()))
              return;
        }

        if (!flag_include_dir.empty())
        {
          // do not include directories that are reversed by ! negation
          for (auto& glob : flag_not_include_dir)
            if (glob_match(pathname, basename, glob.c_str()))
              return;

          // include directories whose basename matches any one of the --include-dir globs
          bool ok = false;
          for (auto& glob : flag_include_dir)
            if ((ok = glob_match(pathname, basename, glob.c_str())))
              break;
          if (!ok)
            return;
        }
      }

      recurse(level, magic, grep, pathname);
    }
  }
  else if ((attr & FILE_ATTRIBUTE_DEVICE) == 0 || flag_devices_action == READ)
  {
    // do not exclude files that are reversed by ! negation
    bool negate = false;
    for (auto& glob : flag_not_exclude)
      if ((negate = glob_match(pathname, basename, glob.c_str())))
        break;

    if (!negate)
    {
      // exclude files whose basename matches any one of the --exclude globs
      for (auto& glob : flag_exclude)
        if (glob_match(pathname, basename, glob.c_str()))
          return;
    }

    // check magic pattern against the file signature, when --file-magic=MAGIC is specified
    if (!flag_file_magic.empty())
    {
      FILE *file;

      if (fopen_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
      {
        warning("cannot read", pathname);
        return;
      }

#ifdef HAVE_LIBZ
      if (flag_decompress)
      {
        zstreambuf streambuf(pathname, file);
        std::istream stream(&streambuf);

        // file has the magic bytes we're looking for: search the file
        if (magic.input(&stream).scan() != 0)
        {
          stats.score_file();

          fclose(file);

          grep.search(pathname);

          return;
        }
      }
      else
#endif
      if (magic.input(reflex::Input(file, flag_encoding_type)).scan() != 0)
      {
        // if file has the magic bytes we're looking for: search the file
        stats.score_file();

        fclose(file);

        grep.search(pathname);

        return;
      }

      fclose(file);

      if (flag_include.empty())
        return;
    }

    if (!flag_include.empty())
    {
      // do not include files that are reversed by ! negation
      for (auto& glob : flag_not_include)
        if (glob_match(pathname, basename, glob.c_str()))
          return;

      // include files whose basename matches any one of the --include globs
      bool ok = false;
      for (auto& glob : flag_include)
        if ((ok = glob_match(pathname, basename, glob.c_str())))
          break;
      if (!ok)
        return;
    }

    stats.score_file();

    grep.search(pathname);
  }

#else

  struct stat buf;

  // use lstat() to check if pathname is a symlink
  if (type != DIRENT_TYPE_UNKNOWN || lstat(pathname, &buf) == 0)
  {
    // symlinks are followed when specified on the command line (unless option -p) or with options -R, -S, --dereference
    if (is_argument_dereference || flag_dereference || (type != DIRENT_TYPE_UNKNOWN ? type != DIRENT_TYPE_LNK : !S_ISLNK(buf.st_mode)))
    {
      // if we got a symlink, use stat() to check if pathname is a directory or a regular file
      if (type != DIRENT_TYPE_LNK || !S_ISLNK(buf.st_mode) || stat(pathname, &buf) == 0)
      {
        // check if directory
        if (type == DIRENT_TYPE_DIR || (type == DIRENT_TYPE_UNKNOWN && S_ISDIR(buf.st_mode)))
        {
          if (flag_directories_action == READ)
          {
            // directories cannot be read actually, so grep produces a warning message (errno is not set)
            is_directory(pathname);
            return;
          }

          if (flag_directories_action == RECURSE)
          {
            std::pair<std::set<ino_t>::iterator,bool> ino;

            // this directory was visited before?
            if (flag_dereference)
            {
              ino = visited.insert(type == DIRENT_TYPE_UNKNOWN ? buf.st_ino : inode);

              // if visited before, then do not recurse on this directory again
              if (!ino.second)
                return;
            }

            // check for --exclude-dir and --include-dir constraints if pathname != "."
            if (strcmp(pathname, ".") != 0)
            {
              // do not exclude directories that are reversed by ! negation
              bool negate = false;
              for (auto& glob : flag_not_exclude_dir)
                if ((negate = glob_match(pathname, basename, glob.c_str())))
                  break;

              if (!negate)
              {
                // exclude directories whose pathname matches any one of the --exclude-dir globs
                for (auto& glob : flag_exclude_dir)
                  if (glob_match(pathname, basename, glob.c_str()))
                    return;
              }

              if (!flag_include_dir.empty())
              {
                // do not include directories that are reversed by ! negation
                for (auto& glob : flag_not_include_dir)
                  if (glob_match(pathname, basename, glob.c_str()))
                    return;

                // include directories whose pathname matches any one of the --include-dir globs
                bool ok = false;
                for (auto& glob : flag_include_dir)
                  if ((ok = glob_match(pathname, basename, glob.c_str())))
                    break;
                if (!ok)
                  return;
              }
            }

            recurse(level, magic, grep, pathname);

            if (flag_dereference)
              visited.erase(ino.first);
          }
        }
        else if (type == DIRENT_TYPE_REG ? !is_output(inode) : type == DIRENT_TYPE_UNKNOWN && S_ISREG(buf.st_mode) ? !is_output(buf.st_ino) : flag_devices_action == READ)
        {
          // do not exclude files that are reversed by ! negation
          bool negate = false;
          for (auto& glob : flag_not_exclude)
            if ((negate = glob_match(pathname, basename, glob.c_str())))
              break;

          if (!negate)
          {
            // exclude files whose pathname matches any one of the --exclude globs
            for (auto& glob : flag_exclude)
              if (glob_match(pathname, basename, glob.c_str()))
                return;
          }

          // check magic pattern against the file signature, when --file-magic=MAGIC is specified
          if (!flag_file_magic.empty())
          {
            FILE *file;

            if (fopen_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
            {
              warning("cannot read", pathname);
              return;
            }

#ifdef HAVE_LIBZ
            if (flag_decompress)
            {
              zstreambuf streambuf(pathname, file);
              std::istream stream(&streambuf);

              // file has the magic bytes we're looking for: search the file
              if (magic.input(&stream).scan() != 0)
              {
                stats.score_file();

                fclose(file);

                grep.search(pathname);

                return;
              }
            }
            else
#endif
            {
              // if file has the magic bytes we're looking for: search the file
              if (magic.input(reflex::Input(file, flag_encoding_type)).scan() != 0)
              {
                stats.score_file();

                fclose(file);

                grep.search(pathname);

                return;
              }
            }

            fclose(file);

            if (flag_include.empty())
              return;
          }

          if (!flag_include.empty())
          {
            // do not include files that are reversed by ! negation
            for (auto& glob : flag_not_include)
              if (glob_match(pathname, basename, glob.c_str()))
                return;

            // include files whose pathname matches any one of the --include globs
            bool ok = false;
            for (auto& glob : flag_include)
              if ((ok = glob_match(pathname, basename, glob.c_str())))
                break;
            if (!ok)
              return;
          }

          stats.score_file();

          grep.search(pathname);
        }
      }
    }
  }
  else
  {
    warning("cannot stat", pathname);
  }

#endif
}

// recurse over directory, searching for pattern matches in files and sub-directories
void recurse(size_t level, reflex::Matcher& magic, Grep& grep, const char *pathname)
{
  // --max-depth: soft recursion level exceeds max depth?
  if (flag_max_depth > 0 && level > flag_max_depth)
    return;

  // hard maximum recursion depth reached?
  if (level > MAX_DEPTH)
  {
    if (!flag_no_messages)
      fprintf(stderr, "%sugrep: %s%s%s recursion depth exceeds hard limit of %d\n", color_off, color_high, pathname, color_off, MAX_DEPTH);
    return;
  }

#ifdef OS_WIN

  WIN32_FIND_DATAA ffd;

  std::string glob;

  if (strcmp(pathname, ".") != 0)
    glob.assign(pathname).append(PATHSEPSTR).append("*");
  else
    glob.assign("*");

  HANDLE hFind = FindFirstFileA(glob.c_str(), &ffd);

  if (hFind == INVALID_HANDLE_VALUE) 
  {
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      warning("cannot open directory", pathname);
    return;
  } 

#else

  DIR *dir = opendir(pathname);

  if (dir == NULL)
  {
    warning("cannot open directory", pathname);
    return;
  }

#endif

  // --ignore-files: check if one or more a present to read and extend the file and dir exclusions
  // std::vector<std::string> *save_exclude = NULL, *save_exclude_dir = NULL, *save_not_exclude = NULL, *save_not_exclude_dir = NULL;
  std::unique_ptr< std::vector<std::string> > save_exclude, save_exclude_dir, save_not_exclude, save_not_exclude_dir;
  bool saved = false;

  if (!flag_ignore_files.empty())
  {
    std::string filename;

    for (auto& i : flag_ignore_files)
    {
      filename.assign(pathname).append(PATHSEPSTR).append(i);

      FILE *file = NULL;
      if (fopen_s(&file, filename.c_str(), "r") == 0)
      {
        if (!saved)
        {
          save_exclude = std::unique_ptr< std::vector<std::string> >(new std::vector<std::string>);
          save_exclude->swap(flag_exclude);
          save_exclude_dir = std::unique_ptr< std::vector<std::string> >(new std::vector<std::string>);
          save_exclude_dir->swap(flag_exclude_dir);
          save_not_exclude = std::unique_ptr< std::vector<std::string> >(new std::vector<std::string>);
          save_not_exclude->swap(flag_not_exclude);
          save_not_exclude_dir = std::unique_ptr< std::vector<std::string> >(new std::vector<std::string>);
          save_not_exclude_dir->swap(flag_not_exclude_dir);

          saved = true;
        }

        stats.ignore_file(filename);
        extend(file, flag_exclude, flag_exclude_dir, flag_not_exclude, flag_not_exclude_dir);
        fclose(file);
      }
    }
  }

  stats.score_dir();

  std::string dirpathname;

#ifdef OS_WIN

  do
  {
    if (strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0)
    {
      if (strcmp(pathname, ".") != 0)
        dirpathname.assign(pathname).append(PATHSEPSTR).append(ffd.cFileName);
      else
        dirpathname.assign(ffd.cFileName);

      find(level + 1, magic, grep, dirpathname.c_str(), ffd.cFileName, DIRENT_TYPE_UNKNOWN, 0);

      // stop after finding max-files matching files
      if (flag_max_files > 0 && stats.found_files() >= flag_max_files)
        break;

      // stop when output is blocked
      if (grep.out.eof)
        break;
    }
  } while (FindNextFileA(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  struct dirent *dirent = NULL;

  while ((dirent = readdir(dir)) != NULL)
  {
    // search directory entries that aren't . or .. or hidden when --no-hidden is enabled
    if (dirent->d_name[0] != '.' || (!flag_no_hidden && dirent->d_name[1] != '\0' && dirent->d_name[1] != '.'))
    {
      if (pathname[0] == '.' && pathname[1] == '\0')
        dirpathname.assign(dirent->d_name);
      else
        dirpathname.assign(pathname).append(PATHSEPSTR).append(dirent->d_name);

#if defined(HAVE_STRUCT_DIRENT_D_TYPE) && defined(HAVE_STRUCT_DIRENT_D_INO)
      find(level + 1, magic, grep, dirpathname.c_str(), dirent->d_name, dirent->d_type, dirent->d_ino);
#else
      find(level + 1, magic, grep, dirpathname.c_str(), dirent->d_name, DIRENT_TYPE_UNKNOWN, 0);
#endif

      // stop after finding max-files matching files
      if (flag_max_files > 0 && stats.found_files() >= flag_max_files)
        break;

      // stop when output is blocked
      if (grep.out.eof)
        break;
    }
  }

  closedir(dir);

#endif

  // --ignore-files: restore if changed
  if (saved)
  {
    save_exclude->swap(flag_exclude);
    save_exclude_dir->swap(flag_exclude_dir);
    save_not_exclude->swap(flag_not_exclude);
    save_not_exclude_dir->swap(flag_not_exclude_dir);
  }
}

// search input, display pattern matches, return true when pattern matched anywhere
void Grep::search(const char *pathname)
{
  // stop when output is blocked
  if (out.eof)
    return;

  // open file (pathname is NULL to read stdin)
  if (!open_file(pathname))
    return;

  // pathname is NULL when stdin is searched
  if (pathname == NULL)
    pathname = flag_label;

  // -z: loop over extracted archive parts, when applicable
  do
  {
    try
    {
      size_t matches = 0;

      if (flag_quiet || flag_files_with_match)
      {
        // -q, -l, or -L: report if a single pattern match was found in the input

        read_file();

        // -I: do not match binary
        if (flag_binary_without_matches)
        {
          const char *eol = matcher->eol(); // warning: call eol() before bol()
          const char *bol = matcher->bol();
          if (is_binary(bol, eol - bol))
            goto exit_search;
        }

        matches = matcher->find() != 0;

        // -K: max line exceeded?
        if (matches > 0 && flag_max_line > 0 && matcher->lineno() > flag_max_line)
          matches = 0;

        if (flag_invert_match)
          matches = !matches;

        if (matches > 0)
        {
          // --format-open or format-close: acquire lock early before stats.found()
          if (flag_files_with_match && (flag_format_open != NULL || flag_format_close != NULL))
            out.acquire();

          if (!stats.found())
            goto exit_search;

          // -l or -L
          if (flag_files_with_match)
          {
            if (flag_format != NULL)
            {
              if (flag_format_open != NULL)
                out.format(flag_format_open, pathname, partname, stats.found_files(), matcher);
              out.format(flag_format, pathname, partname, 1, matcher);
              if (flag_format_close != NULL)
                out.format(flag_format_close, pathname, partname, stats.found_files(), matcher);
            }
            else
            {
              out.str(color_fn);
              out.str(pathname);
              if (!partname.empty())
              {
                out.chr('{');
                out.str(partname);
                out.chr('}');
              }
              out.str(color_off);
              out.chr(flag_null ? '\0' : '\n');
            }
          }
        }
      }
      else if (flag_count)
      {
        // -c: count the number of lines/patterns matched

        if (flag_ungroup || flag_only_matching)
        {
          // -co or -cu: count the number of patterns matched in the file

          read_file();

          // -I: do not match binary
          if (flag_binary_without_matches)
          {
            const char *eol = matcher->eol(); // warning: call eol() before bol()
            const char *bol = matcher->bol();
            if (is_binary(bol, eol - bol))
              goto exit_search;
          }

          while (matcher->find())
          {
            // -K: max line exceeded?
            if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
              break;

            ++matches;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;
          }
        }
        else
        {
          // -c without -o/-u: count the number of matching lines

          size_t lineno = 0;

          read_file();

          // -I: do not match binary
          if (flag_binary_without_matches)
          {
            const char *eol = matcher->eol(); // warning: call eol() before bol()
            const char *bol = matcher->bol();
            if (is_binary(bol, eol - bol))
              goto exit_search;
          }

          while (matcher->find())
          {
            size_t current_lineno = matcher->lineno();

            if (lineno != current_lineno)
            {
              // -K: max line exceeded?
              if (flag_max_line > 0 && current_lineno > flag_max_line)
                break;

              ++matches;

              // -m: max number of matches reached?
              if (flag_max_count > 0 && matches >= flag_max_count)
                break;

              lineno = current_lineno;
            }
          }

          if (flag_invert_match)
            matches = matcher->lineno() - matches - 1;
        }

        // --format: acquire lock early before stats.found()
        if (flag_format_open != NULL || flag_format_close != NULL)
          out.acquire();

        if (matches > 0 || flag_format_open != NULL || flag_format_close != NULL)
          if (!stats.found())
            goto exit_search;

        if (flag_format != NULL)
        {
          if (flag_format_open != NULL)
            out.format(flag_format_open, pathname, partname, stats.found_files(), matcher);
          out.format(flag_format, pathname, partname, matches, matcher);
          if (flag_format_close != NULL)
            out.format(flag_format_close, pathname, partname, stats.found_files(), matcher);
        }
        else
        {
          if (flag_with_filename || !partname.empty())
          {
            out.str(color_fn);
            out.str(pathname);
            if (!partname.empty())
            {
              out.chr('{');
              out.str(partname);
              out.chr('}');
            }
            out.str(color_off);

            if (flag_null)
            {
              out.chr('\0');
            }
            else
            {
              out.str(color_se);
              out.str(flag_separator);
              out.str(color_off);
            }
          }
          out.num(matches);
          out.chr('\n');
        }
      }
      else if (flag_format != NULL)
      {
        // --format

        read_file();

        while (matcher->find())
        {
          // -K: max line exceeded?
          if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
            break;

          // output --format-open
          if (matches == 0)
          {
            // --format-open or --format-close: acquire lock early before stats.found()
            if (flag_format_open != NULL || flag_format_close != NULL)
              out.acquire();

            if (!stats.found())
              goto exit_search;

            if (flag_format_open != NULL)
              out.format(flag_format_open, pathname, partname, stats.found_files(), matcher);
          }

          ++matches;

          // output --format
          out.format(flag_format, pathname, partname, matches, matcher);

          // -m: max number of matches reached?
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;

          if (flag_line_buffered)
            out.flush();
        }

        // output --format-close
        if (matches > 0 && flag_format_close != NULL)
          out.format(flag_format_close, pathname, partname, stats.found_files(), matcher);
      }
      else if (flag_only_line_number)
      {
        // --only-line-number

        size_t lineno = 0;
        const char *separator = flag_separator;

        read_file();

        // -I: do not match binary
        if (flag_binary_without_matches)
        {
          const char *eol = matcher->eol(); // warning: call eol() before bol()
          const char *bol = matcher->bol();
          if (is_binary(bol, eol - bol))
            goto exit_search;
        }

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          separator = lineno != current_lineno ? flag_separator : "+";

          if (lineno != current_lineno || flag_ungroup)
          {
            // -K: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            if (matches == 0)
              if (!stats.found())
                goto exit_search;

            ++matches;

            out.header(pathname, partname, current_lineno, matcher, matcher->first(), separator, true);

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            lineno = current_lineno;
          }
        }
      }
      else if (flag_only_matching)
      {
        // -o

        bool hex = false;
        size_t lineno = 0;

        read_file();

        // -I: do not match binary
        if (flag_binary_without_matches)
        {
          const char *eol = matcher->eol(); // warning: call eol() before bol()
          const char *bol = matcher->bol();
          if (is_binary(bol, eol - bol))
            goto exit_search;
        }

        while (matcher->find())
        {
          const char *begin = matcher->begin();
          size_t size = matcher->size();

          bool binary = flag_hex || (!flag_text && is_binary(begin, size));

          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno || flag_ungroup)
          {
            // -K: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            if (matches == 0)
              if (!stats.found())
                goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            ++matches;

            if (binary && !flag_hex && !flag_with_hex)
            {
              if (flag_binary_without_matches)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              goto done_search;
            }

            if (!flag_no_header)
            {
              if (hex && out.dump.offset < matcher->first())
                out.dump.done(flag_separator);
              hex = false;

              const char *separator = lineno != current_lineno ? flag_separator : "+";

              out.header(pathname, partname, current_lineno, matcher, matcher->first(), separator, binary);
            }

            lineno = current_lineno;
          }

          if (binary)
          {
            if (flag_hex || flag_with_hex)
            {
              if (hex)
                out.dump.next(matcher->first(), flag_separator);

              out.dump.hex(Output::Dump::HEX_MATCH, matcher->first(), begin, size, flag_separator);
              hex = true;
            }
            else
            {
              if (flag_binary_without_matches)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              goto done_search;
            }
          }
          else
          {
            if (hex && out.dump.offset < matcher->first())
              out.dump.done(flag_separator);
            hex = false;

            if (!flag_no_header)
            {
              // -o with -n: echo multi-line matches line-by-line

              const char *from = begin;
              const char *to;

              while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
              {
                out.str(color_ms);
                out.str(from, to - from + 1);
                out.str(color_off);

                if (to + 1 <= begin + size)
                  out.header(pathname, partname, ++lineno, NULL, matcher->first() + (to - begin) + 1, "|", false);

                from = to + 1;
              }

              size -= from - begin;
              begin = from;
            }

            out.str(color_ms);
            out.str(begin, size);
            out.str(color_off);

            if (size == 0 || begin[size - 1] != '\n')
              out.chr('\n');
          }

          if (flag_line_buffered)
            out.flush();
        }

        if (hex)
          out.dump.done(flag_separator);
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line && !flag_invert_match)
      {
        // options -A, -B, -C, -y, -v are not specified

        bool binary = false;
        std::string rest_line;
        const char *rest_line_data = NULL;
        size_t rest_line_size = 0;
        size_t rest_line_last = 0;
        size_t lineno = 0;

        read_file();

        // -I: do not match binary
        if (flag_binary_without_matches)
        {
          const char *eol = matcher->eol(); // warning: call eol() before bol()
          const char *bol = matcher->bol();
          if (is_binary(bol, eol - bol))
            goto exit_search;
        }

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno || flag_ungroup)
          {
            if (rest_line_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(Output::Dump::HEX_LINE, rest_line_last, rest_line_data, rest_line_size, flag_separator);
                out.dump.done(flag_separator);
              }
              else
              {
                if (rest_line_size > 0)
                {
                  out.str(color_sl);
                  if (rest_line_data[rest_line_size - 1] == '\n')
                    out.str(rest_line_data, rest_line_size - 1);
                  else
                    out.str(rest_line_data, rest_line_size);
                  out.str(color_off);
                  out.nl();
                }
              }

              rest_line_data = NULL;
            }

            // -K: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            if (matches == 0)
              if (!stats.found())
                goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            const char *eol = matcher->eol(true); // warning: call eol() before bol()
            const char *bol = matcher->bol();

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            ++matches;

            if (binary && !flag_hex && !flag_with_hex)
            {
              if (flag_binary_without_matches)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              goto done_search;
            }

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_separator : "+";

              out.header(pathname, partname, current_lineno, matcher, matcher->first(), separator, binary);
            }

            lineno = current_lineno;

            size_t border = matcher->border();
            size_t first = matcher->first();
            const char *begin = matcher->begin();
            const char *end = matcher->end();
            size_t size = matcher->size();

            if (binary)
            {
              if (flag_hex || flag_with_hex)
              {
                out.dump.hex(Output::Dump::HEX_LINE, first - border, bol, border, flag_separator);
                out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size, flag_separator);

                if (flag_ungroup)
                {
                  out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end, flag_separator);
                  out.dump.done(flag_separator);
                }
                else
                {
                  rest_line.assign(end, eol - end);
                  rest_line_data = rest_line.c_str();
                  rest_line_size = rest_line.size();
                  rest_line_last = matcher->last();
                }
              }
            }
            else
            {
              out.str(color_sl);
              out.str(bol, border);
              out.str(color_off);

              if (!flag_no_header)
              {
                // -n: echo multi-line matches line-by-line

                const char *from = begin;
                const char *to;
                size_t num = 1;

                while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                {
                  out.str(color_ms);
                  out.str(from, to - from + 1);
                  out.str(color_off);

                  if (to + 1 <= begin + size)
                    out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "|", false);

                  from = to + 1;
                  ++num;
                }

                size -= from - begin;
                begin = from;
              }

              out.str(color_ms);
              out.str(begin, size);
              out.str(color_off);

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  out.str(color_sl);
                  if (end[eol - end - 1] == '\n')
                    out.str(end, eol - end - 1);
                  else
                    out.str(end, eol - end);
                  out.str(color_off);
                  out.nl();
                }
                else if (matcher->hit_end())
                {
                  out.nl();
                }
                else if (flag_line_buffered)
                {
                  out.flush();
                }
              }
              else
              {
                rest_line.assign(end, eol - end);
                rest_line_data = rest_line.c_str();
                rest_line_size = rest_line.size();
                rest_line_last = matcher->last();
              }
            }

            lineno += matcher->lines() - 1;
          }
          else
          {
            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || flag_color)
              {
                size_t first = matcher->first();
                size_t last = matcher->last();
                const char *begin = matcher->begin();

                if (binary)
                {
                  out.dump.hex(Output::Dump::HEX_LINE, rest_line_last, rest_line_data, first - rest_line_last, flag_separator);
                  out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size, flag_separator);
                }
                else
                {
                  out.str(color_sl);
                  out.str(rest_line_data, first - rest_line_last);
                  out.str(color_off);

                  if (lines > 1 && !flag_no_header)
                  {
                    // -n: echo multi-line matches line-by-line

                    const char *from = begin;
                    const char *to;
                    size_t num = 1;

                    while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                    {
                      out.str(color_ms);
                      out.str(from, to - from + 1);
                      out.str(color_off);

                      if (to + 1 <= begin + size)
                        out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "|", false);

                      from = to + 1;
                      ++num;
                    }

                    size -= from - begin;
                    begin = from;
                  }

                  out.str(color_ms);
                  out.str(begin, size);
                  out.str(color_off);
                }

                if (lines == 1)
                {
                  rest_line_data += last - rest_line_last;
                  rest_line_size -= last - rest_line_last;
                  rest_line_last = last;
                }
                else
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before bol()
                  const char *end = matcher->end();

                  bool rest_binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                  if (binary && !rest_binary)
                  {
                    out.dump.done(flag_separator);
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, flag_separator, false);
                  }
                  else if (!binary && rest_binary)
                  {
                    out.nl();
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, flag_separator, true);
                  }

                  binary = rest_binary;

                  if (flag_ungroup)
                  {
                    if (binary)
                    {
                      out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end, flag_separator);
                      out.dump.done(flag_separator);
                    }
                    else
                    {
                      if (eol > end)
                      {
                        out.str(color_sl);
                        if (end[eol - end - 1] == '\n')
                          out.str(end, eol - end - 1);
                        else
                          out.str(end, eol - end);
                        out.str(color_off);
                        out.nl();
                      }
                      else if (matcher->hit_end())
                      {
                        out.nl();
                      }
                      else if (flag_line_buffered)
                      {
                        out.flush();
                      }
                    }
                  }
                  else
                  {
                    rest_line.assign(end, eol - end);
                    rest_line_data = rest_line.c_str();
                    rest_line_size = rest_line.size();
                    rest_line_last = last;
                  }

                  lineno += lines - 1;
                }
              }
            }
          }
        }

        if (rest_line_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_LINE, rest_line_last, rest_line_data, rest_line_size, flag_separator);
          }
          else
          {
            if (rest_line_size > 0)
            {
              out.str(color_sl);
              if (rest_line_data[rest_line_size - 1] == '\n')
                out.str(rest_line_data, rest_line_size - 1);
              else
                out.str(rest_line_data, rest_line_size);
              out.str(color_off);
            }
            out.chr('\n');
          }
        }

        if (binary)
          out.dump.done(flag_separator);
      }
      else
      {
        // read input line-by-line and display lines that match the pattern with context lines

        // mmap base and size, set with mmap.file()
        const char *base = NULL;
        size_t size = 0;

        reflex::BufferedInput buffered_input;

        if (!mmap.file(input, base, size))
          buffered_input = input;

        const char *here = base;
        size_t left = size;

        size_t byte_offset = 0;
        size_t lineno = 1;
        size_t before = 0;
        size_t after = 0;

        const char *hex = NULL;

        // -K=NUM1[,NUM2]: start searching at line NUM1
        if (flag_min_line > 0)
        {
          std::string line;

          for (size_t i = flag_min_line; i > 1; --i)
          {
            // read the next line from mmap, buffered input, or unbuffered input
            if (getline(here, left, buffered_input, input, line))
              goto exit_search;
          }

          lineno = flag_min_line;
        }

        std::vector<bool> binary;
        std::vector<size_t> byte_offsets;
        std::vector<std::string> lines;

        binary.reserve(flag_before_context + 1);
        byte_offsets.reserve(flag_before_context + 1);
        lines.reserve(flag_before_context + 1);

        for (size_t i = 0; i <= flag_before_context; ++i)
        {
          binary[i] = false;
          byte_offsets.emplace_back(0);
          lines.emplace_back("");
        }

        while (true)
        {
          // -K: max line exceeded?
          if (flag_max_line > 0 && lineno > flag_max_line)
            break;

          size_t current = lineno % (flag_before_context + 1);

          binary[current] = flag_hex;
          byte_offsets[current] = byte_offset;

          // read the next line from mmap, buffered input, or unbuffered input
          if (getline(here, left, buffered_input, input, lines[current]))
            break;

          bool before_context = flag_before_context > 0;
          bool after_context = flag_after_context > 0;

          size_t last = UNDEFINED;

          // the current input line to match
          read_line(matcher, lines[current]);

          if (!flag_text && !flag_hex && is_binary(lines[current].c_str(), lines[current].size()))
          {
            if (flag_binary_without_matches)
            {
              matches = 0;

              break;
            }

            binary[current] = true;
          }

          if (flag_invert_match)
          {
            // -v: select non-matching line

            bool found = false;

            while (matcher->find())
            {
              if (flag_any_line || (after > 0 && after + flag_after_context >= lineno))
              {
                // -A NUM: show context after matched lines, simulates BSD grep -A

                if (last == UNDEFINED)
                {
                  if (matches == 0)
                    if (!stats.found())
                      goto exit_search;

                  if (!flag_no_header)
                  {
                    if (hex != NULL && !binary[current])
                    {
                      out.dump.done(hex);
                      hex = NULL;
                    }

                    if (!binary[current] || flag_hex || flag_with_hex)
                      out.header(pathname, partname, lineno, matcher, byte_offset, "-", binary[current]);
                  }

                  last = 0;
                }

                if (binary[current])
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current] + last, lines[current].c_str() + last, matcher->first() - last, "-");
                }
                else
                {
                  if (hex != NULL)
                  {
                    out.dump.done(hex);
                    hex = NULL;
                  }

                  out.str(color_cx);
                  out.str(lines[current].c_str() + last, matcher->first() - last);
                  out.str(color_off);
                }

                last = matcher->last();

                // skip any further empty pattern matches
                if (last == 0)
                  break;

                if (binary[current])
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, byte_offsets[current] + matcher->first(), matcher->begin(), matcher->size(), "-");
                }
                else
                {
                  out.str(color_mc);
                  out.str(matcher->begin(), matcher->size());
                  out.str(color_off);
                }
              }
              else
              {
                found = true;

                break;
              }
            }

            if (last != UNDEFINED)
            {
              if (binary[current])
              {
                if (flag_hex || flag_with_hex)
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current] + last, lines[current].c_str() + last, lines[current].size() - last, "-");

                  if (flag_any_line)
                    hex = "-";
                  else
                    out.dump.done("-");
                }
              }
              else
              {
                if (hex != NULL)
                {
                  out.dump.done(hex);
                  hex = NULL;
                }

                size_t size = lines[current].size();

                if (size > last)
                {
                  if (lines[current][size - 1] == '\n')
                    --size;
                  out.str(color_cx);
                  out.str(lines[current].c_str() + last, size - last);
                  out.str(color_off);
                  out.nl();
                }
              }
            }
            else if (!found)
            {
              if (matches == 0)
                if (!stats.found())
                  goto exit_search;

              if (binary[current] && !flag_hex && !flag_with_hex)
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;

                goto done_search;
              }

              if (after_context)
              {
                // -A NUM: show context after matched lines, simulates BSD grep -A

                // indicate the end of the group of after lines of the previous matched line
                if (after + flag_after_context < lineno && matches > 0 && flag_group_separator != NULL)
                {
                  out.str(color_se);
                  out.str(flag_group_separator);
                  out.str(color_off);
                  out.nl();
                }

                // remember the matched line
                after = lineno;
              }

              if (before_context)
              {
                // -B NUM: show context before matched lines, simulates BSD grep -B

                size_t begin = before + 1;

                if (lineno > flag_before_context && begin < lineno - flag_before_context)
                  begin = lineno - flag_before_context;

                // indicate the begin of the group of before lines
                if (begin < lineno && matches > 0 && flag_group_separator != NULL)
                {
                  out.str(color_se);
                  out.str(flag_group_separator);
                  out.str(color_off);
                  out.nl();
                }

                // display lines before the matched line
                while (begin < lineno)
                {
                  size_t begin_context = begin % (flag_before_context + 1);

                  last = UNDEFINED;

                  read_line(matcher, lines[begin_context]);

                  while (matcher->find())
                  {
                    if (last == UNDEFINED)
                    {
                      if (!flag_no_header)
                        out.header(pathname, partname, begin, matcher, byte_offsets[begin_context], "-", binary[begin_context]);

                      last = 0;
                    }

                    if (binary[begin_context])
                    {
                      out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[begin_context] + last, lines[begin_context].c_str() + last, matcher->first() - last, "-");
                    }
                    else
                    {
                      out.str(color_cx);
                      out.str(lines[begin_context].c_str() + last, matcher->first() - last);
                      out.str(color_off);
                    }

                    last = matcher->last();

                    // skip any further empty pattern matches
                    if (last == 0)
                      break;

                    if (binary[begin_context])
                    {
                      out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, byte_offsets[begin_context] + matcher->first(), matcher->begin(), matcher->size(), "-");
                    }
                    else
                    {
                      out.str(color_mc);
                      out.str(matcher->begin(), matcher->size());
                      out.str(color_off);
                    }
                  }

                  if (last != UNDEFINED)
                  {
                    if (binary[begin % (flag_before_context + 1)])
                    {
                      out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[begin_context] + last, lines[begin_context].c_str() + last, lines[begin_context].size() - last, "-");
                      out.dump.done("-");
                    }
                    else
                    {
                      size_t size = lines[begin_context].size();

                      if (size > last)
                      {
                        if (lines[begin_context][size - 1] == '\n')
                          --size;
                        out.str(color_cx);
                        out.str(lines[begin_context].c_str() + last, size - last);
                        out.str(color_off);
                        out.nl();
                      }
                    }
                  }

                  ++begin;
                }

                // remember the matched line
                before = lineno;
              }

              if (!flag_no_header)
              {
                if (hex != NULL && !binary[current])
                {
                  out.dump.done(hex);
                  hex = NULL;
                }

                out.header(pathname, partname, lineno, NULL, byte_offsets[current], flag_separator, binary[current]);
              }

              if (binary[current])
              {
                out.dump.hex(Output::Dump::HEX_LINE, byte_offsets[current], lines[current].c_str(), lines[current].size(), flag_separator);

                if (flag_any_line)
                  hex = flag_separator;
                else
                  out.dump.done(flag_separator);
              }
              else
              {
                if (hex != NULL)
                {
                  out.dump.done(hex);
                  hex = NULL;
                }

                size_t size = lines[current].size();

                if (size > 0)
                {
                  if (lines[current][size - 1] == '\n')
                    --size;
                  out.str(color_sl);
                  out.str(lines[current].c_str(), size);
                  out.str(color_off);
                  out.nl();
                }
              }

              ++matches;

              // -m: max number of matches reached?
              if (flag_max_count > 0 && matches >= flag_max_count)
                break;

              // output blocked?
              if (out.eof)
                goto exit_search;
            }
          }
          else
          {
            // search the line for pattern matches

            while (matcher->find())
            {
              if (matches == 0)
                if (!stats.found())
                  goto exit_search;

              if (last == UNDEFINED && !flag_hex && !flag_with_hex && binary[current])
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;

                goto done_search;
              }

              if (after_context)
              {
                // -A NUM: show context after matched lines, simulates BSD grep -A

                // indicate the end of the group of after lines of the previous matched line
                if (after + flag_after_context < lineno && matches > 0 && flag_group_separator != NULL)
                {
                  out.str(color_se);
                  out.str(flag_group_separator);
                  out.str(color_off);
                  out.nl();
                }

                // remember the matched line and we're done with the after context
                after = lineno;
                after_context = false;
              }

              if (before_context)
              {
                // -B NUM: show context before matched lines, simulates BSD grep -B

                size_t begin = before + 1;

                if (lineno > flag_before_context && begin < lineno - flag_before_context)
                  begin = lineno - flag_before_context;

                // indicate the begin of the group of before lines
                if (begin < lineno && matches > 0 && flag_group_separator != NULL)
                {
                  out.str(color_se);
                  out.str(flag_group_separator);
                  out.str(color_off);
                  out.nl();
                }

                // display lines before the matched line
                while (begin < lineno)
                {
                  size_t begin_context = begin % (flag_before_context + 1);

                  if (!flag_no_header)
                    out.header(pathname, partname, begin, NULL, byte_offsets[begin_context], "-", binary[begin_context]);

                  if (binary[begin_context])
                  {
                    out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[begin_context], lines[begin_context].c_str(), lines[begin_context].size(), "-");
                    out.dump.done("-");
                  }
                  else
                  {
                    size_t size = lines[begin_context].size();

                    if (size > 0)
                    {
                      if (lines[begin_context][size - 1] == '\n')
                        --size;
                      out.str(color_cx);
                      out.str(lines[begin_context].c_str(), size);
                      out.str(color_off);
                      out.nl();
                    }
                  }

                  ++begin;
                }

                // remember the matched line and we're done with the before context
                before = lineno;
                before_context = false;
              }

              if (flag_ungroup && !binary[current])
              {
                // -u: do not group matches on a single line but on multiple lines, counting each match separately

                const char *separator = last == UNDEFINED ? flag_separator : "+";

                if (!flag_no_header)
                  out.header(pathname, partname, lineno, matcher, byte_offset + matcher->first(), separator, binary[current]);

                out.str(color_sl);
                out.str(lines[current].c_str(), matcher->first());
                out.str(color_off);
                out.str(color_ms);
                out.str(matcher->begin(), matcher->size());
                out.str(color_off);
                out.str(color_sl);
                out.str(lines[current].c_str() + matcher->last(), lines[current].size() - matcher->last());
                out.str(color_off);

                ++matches;

                // -m: max number of matches reached?
                if (flag_max_count > 0 && matches >= flag_max_count)
                  goto done_search;

                // output blocked?
                if (out.eof)
                  goto exit_search;
              }
              else
              {
                if (last == UNDEFINED)
                {
                  if (!flag_no_header)
                  {
                    if (hex != NULL && !binary[current])
                    {
                      out.dump.done(hex);
                      hex = NULL;
                    }

                    out.header(pathname, partname, lineno, matcher, byte_offset, flag_separator, binary[current]);
                  }

                  ++matches;

                  last = 0;
                }

                if (binary[current])
                {
                  out.dump.hex(Output::Dump::HEX_LINE, byte_offsets[current] + last, lines[current].c_str() + last, matcher->first() - last, flag_separator);
                  out.dump.hex(Output::Dump::HEX_MATCH, byte_offsets[current] + matcher->first(), matcher->begin(), matcher->size(), flag_separator);
                }
                else
                {
                  if (hex != NULL)
                  {
                    out.dump.done(hex);
                    hex = NULL;
                  }

                  out.str(color_sl);
                  out.str(lines[current].c_str() + last, matcher->first() - last);
                  out.str(color_off);
                  out.str(color_ms);
                  out.str(matcher->begin(), matcher->size());
                  out.str(color_off);
                }
              }

              last = matcher->last();

              // skip any further empty pattern matches
              if (last == 0)
                break;
            }

            if (last != UNDEFINED)
            {
              if (binary[current])
              {
                out.dump.hex(Output::Dump::HEX_LINE, byte_offsets[current] + last, lines[current].c_str() + last, lines[current].size() - last, flag_separator);

                if (flag_any_line)
                  hex = flag_separator;
                else
                  out.dump.done(flag_separator);
              }
              else if (!flag_ungroup)
              {
                if (hex != NULL)
                {
                  out.dump.done(hex);
                  hex = NULL;
                }

                size_t size = lines[current].size();

                if (size > last)
                {
                  if (lines[current][size - 1] == '\n')
                    --size;
                  out.str(color_sl);
                  out.str(lines[current].c_str() + last, size - last);
                  out.str(color_off);
                  out.nl();
                }
              }
            }
            else if (flag_any_line || (after > 0 && after + flag_after_context >= lineno))
            {
              // -A NUM: show context after matched lines, simulates BSD grep -A

              if (!binary[current] || flag_hex || flag_with_hex)
              {
                // display line as part of the after context of the matched line
                if (!flag_no_header)
                {
                  if (hex != NULL && !binary[current])
                  {
                    out.dump.done(hex);
                    hex = NULL;
                  }

                  out.header(pathname, partname, lineno, NULL, byte_offsets[current], "-", binary[current]);
                }

                if (binary[current])
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current], lines[current].c_str(), lines[current].size(), "-");

                  if (flag_any_line)
                    hex = "-";
                  else
                    out.dump.done("-");
                }
                else
                {
                  if (hex != NULL)
                  {
                    out.dump.done(hex);
                    hex = NULL;
                  }

                  size_t size = lines[current].size();

                  if (size > 0)
                  {
                    if (lines[current][size - 1] == '\n')
                      --size;
                    out.str(color_cx);
                    out.str(lines[current].c_str(), size);
                    out.str(color_off);
                    out.nl();
                  }
                }
              }
            }

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;
          }

          // update byte offset and line number
          byte_offset += lines[current].size();
          ++lineno;
        }
      }

done_search:

      // --break: add a line break when applicable
      if (flag_break && flag_format == NULL && (matches > 0 || flag_count || flag_any_line))
        out.chr('\n');

    }

    catch (...)
    {
      warning("exception while searching", pathname);
    }

exit_search:

    // flush and release output to allow other workers to output results
    out.release();

    // close file or -z: loop over next extracted archive parts, when applicable
  } while (close_file(pathname));
}

// read globs from a file to extend include/exclude files and dirs
void extend(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs, std::vector<std::string>& not_files, std::vector<std::string>& not_dirs)
{
  // read globs from the specified file or files
  reflex::BufferedInput input(file);
  std::string line;

  while (true)
  {
    // read the next line
    if (getline(input, line))
      break;

    trim(line);

    // add glob to files and dirs using gitignore glob pattern rules
    if (!line.empty() && line.front() != '#')
    {
      // gitignore-style ! negate pattern to override include/exclude
      bool negate = line.front() == '!' && !line.empty();
      if (negate)
        line.erase(0, 1);

      // remove leading \ if present
      if (line.front() == '\\' && !line.empty())
        line.erase(0, 1);

      // globs ending in / should only match directories
      if (line.back() == '/')
        line.pop_back();
      else
        (negate ? not_files : files).emplace_back(line);

      (negate ? not_dirs : dirs).emplace_back(line);
    }
  }
}

// display format with option --format-begin and --format-end
void format(const char *format, size_t matches)
{
  const char *sep = NULL;
  size_t len = 0;
  const char *s = format;
  while (*s != '\0')
  {
    const char *a = NULL;
    const char *t = s;
    while (*s != '\0' && *s != '%')
      ++s;
    fwrite(t, 1, s - t, output);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '[')
    {
      a = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }
    int c = *s;
    switch (c)
    {
      case 'T':
        if (flag_initial_tab)
        {
          if (a)
            fwrite(a, 1, s - a - 1, output);
          fputc('\t', output);
        }
        break;

      case 'S':
        if (matches > 1)
        {
          if (a)
            fwrite(a, 1, s - a - 1, output);
          if (sep != NULL)
            fwrite(sep, 1, len, output);
          else
            fputs(flag_separator, output);
        }
        break;

      case '$':
        sep = a;
        len = s - a - 1;
        break;

      case 't':
        fputc('\t', output);
        break;

      case 's':
        if (sep != NULL)
          fwrite(sep, 1, len, output);
        else
          fputs(flag_separator, output);
        break;

      case '~':
        fputc('\n', output);
        break;

      case 'm':
        fprintf(output, "%zu", matches);
        break;

      case '<':
        if (matches <= 1 && a)
          fwrite(a, 1, s - a - 1, output);
        break;

      case '>':
        if (matches > 1 && a)
          fwrite(a, 1, s - a - 1, output);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (matches > 1)
          fputc(c, output);
        break;

      default:
        fputc(c, output);
    }
    ++s;
  }
}

// trim line to remove leading and trailing white space
void trim(std::string& line)
{
  size_t len = line.length();
  size_t pos;

  for (pos = 0; pos < len && isspace(line.at(pos)); ++pos)
    continue;

  if (pos > 0)
    line.erase(0, pos);

  len -= pos;

  for (pos = len; pos > 0 && isspace(line.at(pos - 1)); --pos)
    continue;

  line.erase(pos, len - pos);
}

// convert GREP_COLORS and set the color substring to the ANSI SGR sequence
void set_color(const char *grep_colors, const char *parameter, char color[COLORLEN])
{
  const char *substr = strstr(grep_colors, parameter);

  // check if substring parameter is present in GREP_COLORS
  if (substr != NULL && substr[2] == '=')
  {
    substr += 3;
    const char *colon = substr;

    while (*colon && (isdigit(*colon) || *colon == ';'))
      ++colon;

    size_t sublen = colon - substr;

    if (sublen > 0 && sublen < COLORLEN - 4)
    {
      color[0] = '\033';
      color[1] = '[';
      memcpy(color + 2, substr, sublen);
      color[sublen + 2] = 'm';
      color[sublen + 3] = '\0';
    }
  }
}

// convert unsigned decimal to positive size_t, produce error when conversion fails or when the value is zero
size_t strtopos(const char *string, const char *message)
{
  char *rest = NULL;
  size_t size = static_cast<size_t>(strtoull(string, &rest, 10));
  if (rest == NULL || *rest != '\0' || size == 0)
    help(message, string);
  return size;
}

// convert two comma-separated unsigned decimals specifying a range to positive size_t, produce error when conversion fails or when the range is invalid
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message)
{
  char *rest = const_cast<char*>(string);
  if (*string != ',')
    pos1 = static_cast<size_t>(strtoull(string, &rest, 10));
  else
    pos1 = 0;
  if (*rest == ',')
    pos2 = static_cast<size_t>(strtoull(rest + 1, &rest, 10));
  else
    pos2 = 0;
  if (rest == NULL || *rest != '\0' || (pos2 > 0 && pos1 > pos2))
    help(message, string);
}

// display usage/help information with an optional diagnostic message and exit
void help(const char *message, const char *arg)
{
  if (message && *message)
    std::cout << "ugrep: " << message << (arg != NULL ? arg : "") << std::endl;

  std::cout << "Usage: ugrep [OPTIONS] [PATTERN] [-f FILE] [-e PATTERN] [FILE ...]\n";

  if (!message)
  {
    std::cout << "\n\
    -A NUM, --after-context=NUM\n\
            Print NUM lines of trailing context after matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            the -B, -C, and -y options.\n\
    -a, --text\n\
            Process a binary file as if it were text.  This is equivalent to\n\
            the --binary-files=text option.  This option might output binary\n\
            garbage to the terminal, which can have problematic consequences if\n\
            the terminal driver interprets some of it as commands.\n\
    -B NUM, --before-context=NUM\n\
            Print NUM lines of leading context before matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            the -A, -C, and -y options.\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched line is displayed in front of the\n\
            respective matched line.  When used with option -u, displays the\n\
            offset in bytes of each pattern matched.  Byte offsets are exact\n\
            for ASCII, UTF-8, and raw binary input.  Otherwise, the byte offset\n\
            in the UTF-8 converted input is displayed.\n\
    --binary-files=TYPE\n\
            Controls searching and reporting pattern matches in binary files.\n\
            Options are `binary', `without-match`, `text`, `hex`, and\n\
            `with-hex'.  The default is `binary' to search binary files and to\n\
            report a match without displaying the match.  `without-match'\n\
            ignores binary matches.  `text' treats all binary files as text,\n\
            which might output binary garbage to the terminal, which can have\n\
            problematic consequences if the terminal driver interprets some of\n\
            it as commands.  `hex' reports all matches in hexadecimal.\n\
            `with-hex' only reports binary matches in hexadecimal, leaving text\n\
            matches alone.  A match is considered binary if a match contains a\n\
            zero byte or invalid UTF encoding.  See also the -a, -I, -U, -W,\n\
            and -X options.\n\
    --break\n\
            Groups matches per file and adds a line break between results from\n\
            different files.\n\
    -C[NUM], --context[=NUM]\n\
            Print NUM lines of leading and trailing context surrounding each\n\
            match.  The default is 2 and is equivalent to -A 2 -B 2.  Places\n\
            a --group-separator between contiguous groups of matches.\n\
            No whitespace may be given between -C and its argument NUM.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            If -o or -u is specified, counts the number of patterns matched.\n\
            If -v is specified, counts the number of non-matching lines.\n\
    --color[=WHEN], --colour[=WHEN]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR or GREP_COLORS environment variable.  The possible\n\
            values of WHEN can be `never', `always', or `auto', where `auto'\n\
            marks up matches only when output on a terminal.  The default is\n\
            `auto'.\n\
    --cpp\n\
            Output file matches in C++.  See also option --format.\n\
    --csv\n\
            Output file matches in CSV.  Use options -H, -n, -k, and -b to\n\
            specify CSV values to output.  See also option --format.\n\
    -D ACTION, --devices=ACTION\n\
            If an input file is a device, FIFO or socket, use ACTION to process\n\
            it.  By default, ACTION is `skip', which means that devices are\n\
            silently skipped.  If ACTION is `read', devices read just as if\n\
            they were ordinary files.\n\
    -d ACTION, --directories=ACTION\n\
            If an input file is a directory, use ACTION to process it.  By\n\
            default, ACTION is `read', i.e., read directories just as if they\n\
            were ordinary files.  If ACTION is `skip', silently skip\n\
            directories.  If ACTION is `recurse', read all files under each\n\
            directory, recursively, following symbolic links only if they are\n\
            on the command line.  This is equivalent to the -r option.  If\n\
            ACTION is `dereference-recurse', read all files under each\n\
            directory, recursively, following symbolic links.  This is\n\
            equivalent to the -R option.\n\
    -E, --extended-regexp\n\
            Interpret patterns as extended regular expressions (EREs). This is\n\
            the default.\n\
    -e PATTERN, --regexp=PATTERN\n\
            Specify a PATTERN used during the search of the input: an input\n\
            line is selected if it matches any of the specified patterns.\n\
            Note that longer patterns take precedence over shorter patterns.\n\
            This option is most useful when multiple -e options are used to\n\
            specify multiple patterns, when a pattern begins with a dash (`-'),\n\
            to specify a pattern after option -f or after the FILE arguments.\n\
    --exclude=GLOB\n\
            Skip files whose name matches GLOB using wildcard matching, same as\n\
            -g !GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  If GLOB\n\
            contains /, full pathnames are matched.  Otherwise basenames are\n\
            matched.  Note that --exclude patterns take priority over --include\n\
            patterns.  This option may be repeated.\n\
    --exclude-dir=GLOB\n\
            Exclude directories whose name matches GLOB from recursive\n\
            searches.  If GLOB contains /, full pathnames are matched.\n\
            Otherwise basenames are matched.  Note that --exclude-dir patterns\n\
            take priority over --include-dir patterns.  This option may be\n\
            repeated.\n\
    --exclude-from=FILE\n\
            Read the globs from FILE and skip files and directories whose name\n\
            matches one or more globs (as if specified by --exclude and\n\
            --exclude-dir).  Lines starting with a `#' and empty lines in FILE\n\
            are ignored.  When FILE is a `-', standard input is read.  This\n\
            option may be repeated.\n\
    -F, --fixed-strings\n\
            Interpret pattern as a set of fixed strings, separated by newlines,\n\
            any of which is to be matched.  This makes ugrep behave as fgrep.\n\
            If PATTERN or -e PATTERN is also specified, then this option does\n\
            not apply to -f FILE patterns.\n\
    -f FILE, --file=FILE\n\
            Read one or more newline-separated patterns from FILE.  Empty\n\
            pattern lines in FILE are not processed.  If FILE does not exist,\n\
            the GREP_PATH environment variable is used as the path to FILE.\n"
#ifdef GREP_PATH
"\
            If that fails, looks for FILE in " GREP_PATH ".\n"
#endif
"\
            When FILE is a `-', standard input is read.  Empty files contain no\n\
            patterns, thus nothing is matched.  This option may be repeated.\n\
    --format=FORMAT\n\
            Output FORMAT-formatted matches.  See `man ugrep' section FORMAT\n\
            for the `%' fields.  Options -A, -B, -C, -y, and -v are disabled.\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression, i.e. make ugrep\n\
            behave as traditional grep.\n\
    -g GLOB, --glob=GLOB\n\
            Search only files whose name matches GLOB, same as --include=GLOB.\n\
            If GLOB is preceded by a `!', skip files whose name matches GLOB,\n\
            same as --exclude=GLOB.\n\
    --group-separator[=SEP]\n\
            Use SEP as a group separator for context options -A, -B, and -C.\n\
            The default is a double hyphen (`--').\n\
    -H, --with-filename\n\
            Always print the filename with output lines.  This is the default\n\
            when there is more than one file to search.\n\
    -h, --no-filename\n\
            Never print filenames with output lines.  This is the default\n\
            when there is only one file (or only standard input) to search.\n\
    --help\n\
            Print a help message.\n\
    -I\n\
            Ignore matches in binary files.  This option is equivalent to the\n\
            --binary-files=without-match option.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching.  By default, ugrep is case\n\
            sensitive.  This option applies to ASCII letters only.\n\
    --ignore-files[=FILE]\n\
            Ignore files and directories specified in a FILE when encountered\n\
            in recursive searches.  The default is `" DEFAULT_IGNORE_FILE "'.  Files and\n\
            directories matching the globs in FILE are ignored in the directory\n\
            tree rooted at each FILE's location by temporarily overriding\n\
            --exclude and --exclude-dir globs.  This option may be repeated.\n\
    --include=GLOB\n\
            Search only files whose name matches GLOB using wildcard matching,\n\
            same as -g GLOB.  GLOB can use **, *, ?, and [...] as wildcards,\n\
            and \\ to quote a wildcard or backslash character literally.  If\n\
            GLOB contains /, file pathnames are matched.  Otherwise file\n\
            basenames are matched.  Note that --exclude patterns take priority\n\
            over --include patterns.  This option may be repeated.\n\
    --include-dir=GLOB\n\
            Only directories whose name matches GLOB are included in recursive\n\
            searches.  If GLOB contains /, full pathnames are matched.\n\
            Otherwise basenames are matched.  Note that --exclude-dir patterns\n\
            take priority over --include-dir patterns.  This option may be\n\
            repeated.\n\
    --include-from=FILE\n\
            Read the globs from FILE and search only files and directories\n\
            whose name matches one or more globs (as if specified by --include\n\
            and --include-dir).  Lines starting with a `#' and empty lines in\n\
            FILE are ignored.  When FILE is a `-', standard input is read.\n\
            This option may be repeated.\n\
    -J NUM, --jobs=NUM\n\
            Specifies the number of threads spawned to search files.  By\n\
            default, an optimum number of threads is spawned to search files\n\
            simultaneously.  -J1 disables threading: files are searched in the\n\
            same order as specified.\n\
    -j, --smart-case\n\
            Perform case insensitive matching unless PATTERN contains a capital\n\
            letter.  Case insensitive matching applies to ASCII letters only.\n\
    --json\n\
            Output file matches in JSON.  Use options -H, -n, -k, and -b to\n\
            specify JSON properties to output.  See also option --format.\n\
    -K NUM1[,NUM2], --range=NUM1[,NUM2]\n\
            Start searching at line NUM1 and end at line NUM2 when specified.\n\
    -k, --column-number\n\
            The column number of a matched pattern is displayed in front of the\n\
            respective matched line, starting at column 1.  Tabs are expanded\n\
            when columns are counted, see option --tabs.\n\
    -L, --files-without-match\n\
            Only the names of files not containing selected lines are written\n\
            to standard output.  Pathnames are listed once per file searched.\n\
            If the standard input is searched, the string ``(standard input)''\n\
            is written.\n\
    -l, --files-with-matches\n\
            Only the names of files containing selected lines are written to\n\
            standard output.  ugrep will only search a file until a match has\n\
            been found, making searches potentially less expensive.  Pathnames\n\
            are listed once per file searched.  If the standard input is\n\
            searched, the string ``(standard input)'' is written.\n\
    --label[=LABEL]\n\
            Displays the LABEL value when input is read from standard input\n\
            where a file name would normally be printed in the output.  This\n\
            option applies to options -H, -L, and -l.\n\
    --line-buffered\n\
            Force output to be line buffered.  By default, output is line\n\
            buffered when standard output is a terminal and block buffered\n\
            otherwise.\n\
    -M MAGIC, --file-magic=MAGIC\n\
            Only files matching the signature pattern MAGIC are searched.  The\n\
            signature \"magic bytes\" at the start of a file are compared to\n\
            the `MAGIC' regex pattern.  When matching, the file will be\n\
            searched.  This option may be repeated and may be combined with\n\
            options -O and -t to expand the search.  This option is relatively\n\
            slow as every file on the search path is read to compare MAGIC.\n\
    -m NUM, --max-count=NUM\n\
            Stop reading the input after NUM matches for each file processed.\n\
    --match\n\
            Match all input.  Same as specifying an empty pattern to search.\n\
    --max-depth=NUM\n\
            Restrict recursive search to NUM (NUM > 0) directories deep, where\n\
            --max-depth=1 searches the specified path without visiting\n\
            sub-directories.  By comparison, -dskip skips all directories even\n\
            when they are on the command line.\n\
    --max-files=NUM\n\
            If -R or -r is specified, restrict the number of files matched to\n\
            NUM.  Specify -J1 to produce replicable results by ensuring that\n\
            files are searched in the same order as specified.\n\
    -N PATTERN, --not-regexp=PATTERN\n\
            Specify a negative PATTERN used during the search of the input: an\n\
            input line is selected only if it matches any of the specified\n\
            patterns when PATTERN does not match.  Same as -e (?^PATTERN)\n\
            Negative PATTERN matches are removed before any other specified\n\
            patterns are matched.  Note that longer patterns take precedence\n\
            over shorter patterns.  This option may be repeated.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B, and -C.\n\
    --[no-]hidden\n\
            Do (not) search ";
#ifdef OS_WIN
  std::cout << "Windows system and ";
#endif
  std::cout << "hidden files and directories.\n";
#ifndef OS_WIN
  std::cout << "\
    --[no-]mmap\n\
            Do (not) use memory maps to search files.  By default, memory maps\n\
            are used under certain conditions to improve performance.\n";
#endif
  std::cout << "\
    -O EXTENSIONS, --file-extensions=EXTENSIONS\n\
            Search only files whose file name extensions match the specified\n\
            comma-separated list of file name EXTENSIONS.  This option is the\n\
            same as specifying --include='*.ext' for each extension name `ext'\n\
            in the EXTENSIONS list.  This option may be repeated and may be\n\
            combined with options -M and -t to expand the search.\n\
    -o, --only-matching\n\
            Prints only the matching part of lines.  When multiple lines match,\n\
            the line numbers with option -n are displayed using `|' as the\n\
            field separator for each additional line matched by the pattern.\n\
            This option cannot be combined with options -A, -B, -C, -v, and -y.\n\
    --only-line-number\n\
            The line number of the matching line in the file is output without\n\
            displaying the match.  The line number counter is reset for each\n\
            file processed.\n\
    -P, --perl-regexp\n\
            Interpret PATTERN as a Perl regular expression.\n";
#ifndef HAVE_BOOST_REGEX
  std::cout << "\
            This feature is not available in this build configuration of ugrep.\n";
#endif
  std::cout << "\
    -p, --no-dereference\n\
            If -R or -r is specified, no symbolic links are followed, even when\n\
            they are specified on the command line.\n";
#ifndef OS_WIN
  std::cout << "\
    --pager[=COMMAND]\n\
            When output is sent to the terminal, uses COMMAND to page through\n\
            the output.  The default COMMAND is `less -R'.  This option makes\n\
            --color=auto behave as --color=always.  Enables --break and\n\
            --line-buffered.\n";
#endif
  std::cout << "\
    -Q ENCODING, --encoding=ENCODING\n\
            The input file encoding.  The possible values of ENCODING can be:";
  for (int i = 0; format_table[i].format != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 6 ? " " : "\n            ") << "`" << format_table[i].format << "'";
  std::cout << "\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress normal output.  ugrep will only search until a\n\
            match has been found, making searches potentially less expensive.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory.  Follow all\n\
            symbolic links, unlike -r.  If -J1 is specified, files are searched\n\
            in the same order as specified.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are on the command line.  If -J1 is specified,\n\
            files are searched in the same order as specified.\n\
    -S, --dereference\n\
            If -r is specified, all symbolic links are followed, like -R.  The\n\
            default is not to follow symbolic links.\n\
    -s, --no-messages\n\
            Silent mode: nonexistent and unreadable files are ignored, i.e.\n\
            their error messages are suppressed.\n\
    --separator[=SEP]\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset, and the matched line.  The default is a colon\n\
            (`:').\n\
    --stats\n\
            Display statistics on the number of files and directories searched.\n\
            Display MAGIC and GLOB selections applied to recursive searches.\n\
    -T, --initial-tab\n\
            Add a tab space to separate the file name, line number, column\n\
            number, and byte offset with the matched line.\n\
    -t TYPES, --file-type=TYPES\n\
            Search only files associated with TYPES, a comma-separated list of\n\
            file types.  Each file type corresponds to a set of file name\n\
            extensions passed to option -O.  For capitalized file types, the\n\
            search is expanded to include files found on the search path with\n\
            matching file signature magic bytes passed to option -M.  This\n\
            option may be repeated.  The possible values of TYPES can be\n\
            (use option -tlist to display a more detailed list):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  std::cout << "\n\
    --tabs=NUM\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1, 2, 4, or 8.  The default tab size is 8.\n\
    -U, --binary\n\
            Disables Unicode matching for binary file matching, forcing PATTERN\n\
            to match bytes, not Unicode characters.  For example, -U '\\xa3'\n\
            matches byte A3 (hex) instead of the Unicode code point U+00A3\n\
            represented by the two-byte UTF-8 sequence C2 A3.\n\
    -u, --ungroup\n\
            Do not group multiple pattern matches on the same matched line.\n\
            Output the matched line again for each additional pattern match,\n\
            using `+' as the field separator.\n\
    -V, --version\n\
            Display version information and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    -W, --with-hex\n\
            Output binary matches in hexadecimal, leaving text matches alone.\n\
            This option is equivalent to the --binary-files=with-hex option.\n\
    -w, --word-regexp\n\
            The PATTERN or -e PATTERN is searched for as a word (as if\n\
            surrounded by \\< and \\>).  If PATTERN or -e PATTERN is also\n\
            specified, then this option does not apply to -f FILE patterns.\n\
    -X, --hex\n\
            Output matches in hexadecimal.  This option is equivalent to the\n\
            --binary-files=hex option.\n\
    -x, --line-regexp\n\
            Only input lines selected against the entire PATTERN or -e PATTERN\n\
            is considered to be matching lines (as if surrounded by ^ and $).\n\
            If PATTERN or -e PATTERN is also specified, then this option does\n\
            not apply to -f FILE patterns.\n\
    --xml\n\
            Output file matches in XML.  Use options -H, -n, -k, and -b to\n\
            specify XML attributes to output.  See also option --format.\n\
    -Y, --empty\n\
            Permits empty matches, such as `^\\h*$' to match blank lines.  Empty\n\
            matches are disabled by default.  Note that empty-matching patterns,\n\
            such as x? and x*, match all input with -Y, not only lines with `x'.\n\
    -y, --any-line\n\
            Any matching or non-matching line is output.  Non-matching lines\n\
            are output with the `-' separator as context of the matching lines.\n\
            See also the -A, -B, and -C options.\n\
    -Z, --null\n\
            Prints a zero-byte after the file name.\n\
    -z, --decompress\n\
            Decompress files to search, when compressed.  Archives (.cpio,\n\
            .pax, .tar, .zip) and compressed archives (e.g. .taz, .tgz, .tpz,\n\
            .tbz, .tbz2, .tb2, .tz2, .tlz, and .txz) are searched and matching\n\
            pathnames of files in archives are output in braces.  If -g, -O,\n\
            -M, or -t is specified, searches files within archives whose name\n\
            matches globs, matches file name extensions, matches file\n\
            signature magic bytes, or matches file types, respectively.\n";
#ifndef HAVE_LIBZ
  std::cout << "\
            This feature is not available in this build configuration of ugrep.\n";
#else
  std::cout << "\
            Supported compression formats: gzip (default, optional suffix .gz),\n\
            compress (requires suffix .Z), zip (requires suffix .zip or .ZIP)";
#ifdef HAVE_LIBBZ2
  std::cout << ",\n\
            bzip2 (requires suffix .bz, .bz2, or .bzip2)";
#endif
#ifdef HAVE_LIBLZMA
  std::cout << ",\n\
            lzma (requires suffix .lzma), and xz (requires suffix .xz)";
#endif
  std::cout << ".\n";
#endif
  std::cout << "\
\n\
    The ugrep utility exits with one of the following values:\n\
\n\
    0       One or more lines were selected.\n\
    1       No lines were selected.\n\
    >1      An error occurred.\n\
\n\
    If -q or --quiet or --silent is used and a line is selected, the exit\n\
    status is 0 even if an error occurred.\n\
" << std::endl;
  }

  exit(EXIT_ERROR);
}

// display version info
void version()
{
  std::cout << "ugrep " UGREP_VERSION " " PLATFORM << "\n"
    "Copyright (c) Genivia Inc.\n"
    "License BSD-3-Clause: <https://opensource.org/licenses/BSD-3-Clause>\n"
    "Written by Robert van Engelen and others: <https://github.com/Genivia/ugrep>" << std::endl;
  exit(EXIT_OK);
}

// print to standard error: ... is a directory if -q is not specified
void is_directory(const char *pathname)
{
  if (!flag_no_messages)
    fprintf(stderr, "%sugrep: %s%s%s is a directory\n", color_off, color_high, pathname, color_off);
}

#ifdef HAVE_LIBZ
// print to standard error: cannot decompress message if -q is not specified
void cannot_decompress(const char *pathname, const char *message)
{
  if (!flag_no_messages)
    fprintf(stderr, "%sugrep: %swarning:%s %scannot decompress %s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, pathname, color_off, color_message, message, color_off);
}
#endif

// print to standard error: warning message if -q is not specified, assumes errno is set, like perror()
void warning(const char *message, const char *arg)
{
  if (!flag_no_messages)
  {
    // use safe strerror_s() instead of strerror() when available
#if defined(__STDC_LIB_EXT1__) || defined(OS_WIN)
    char errmsg[256]; 
    strerror_s(errmsg, sizeof(errmsg), errno);
#else
    const char *errmsg = strerror(errno);
#endif
    fprintf(stderr, "%sugrep: %swarning:%s %s%s %s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, message, arg, color_off, color_message, errmsg, color_off);
  }
}

// print to standard error: error message, assumes errno is set, like perror(), then exit
void error(const char *message, const char *arg)
{
  // use safe strerror_s() instead of strerror() when available
#if defined(__STDC_LIB_EXT1__) || defined(OS_WIN)
  char errmsg[256]; 
  strerror_s(errmsg, sizeof(errmsg), errno);
#else
  const char *errmsg = strerror(errno);
#endif
  fprintf(stderr, "%sugrep: %serror:%s %s%s %s:%s %s%s%s\n\n", color_off, color_error, color_off, color_high, message, arg, color_off, color_message, errmsg, color_off);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message, const std::string& what)
{
  fprintf(stderr, "%sugrep: %s%s%s%s%s%s\n\n", color_off, color_error, message, color_off, color_high, what.c_str(), color_off);
  exit(EXIT_ERROR);
}
