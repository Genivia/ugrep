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
@brief     Universal grep - a pattern search utility
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Universal grep: high-performance file search utility.  Supersedes GNU and BSD
grep with full Unicode support.  Offers easy options and predefined regex
patterns to quickly search source code, text, and binary files in large
directory trees.  Compatible with GNU/BSD grep, offering a faster drop-in
replacement.

For download and installation instructions:

  https://github.com/Genivia/ugrep

This program uses RE/flex:

  https://github.com/Genivia/RE-flex

Optional libraries to support options -P and -z:

  Boost.Regex
  zlib

Build ugrep as follows:

  ./configure
  make

Install as follows:

  ./configure
  make
  sudo make install

Prebuilt executables are located in ugrep/bin.

*/

#include <reflex/input.h>
#include <reflex/matcher.h>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <list>
#include <queue>
#include <set>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "glob.hpp"

// option: use a thread to decompress the stream into a pipe to search, for greater speed
#define WITH_LIBZ_THREAD

// check if we are compiling for a windows OS
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

#ifdef OS_WIN

// disable min/max macros to use std::min and std::max
#define NOMINMAX

#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <strsafe.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// windows has no isatty()
#define isatty(fildes) ((fildes) == 1 || (fildes) == 2)

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"

#else

#include <dirent.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_BOOST_REGEX
#include <reflex/boostmatcher.h>
#endif

#ifdef HAVE_LIBZ
#include "zstream.hpp"
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"

// handle SIGPIPE
void sigpipe_handle(int) { }

#endif

// ugrep version info
#define UGREP_VERSION "1.5.4"

// ugrep platform -- see configure.ac
#if !defined(PLATFORM)
# if defined(OS_WIN)
#  define PLATFORM "WIN"
# else
#  define PLATFORM ""
# endif
#endif

// ugrep exit codes
#define EXIT_OK    0 // One or more lines were selected
#define EXIT_FAIL  1 // No lines were selected
#define EXIT_ERROR 2 // An error occurred

// limit the total number of threads spawn (i.e. limit spawn overhead), because grepping is practically IO bound
#define MAX_JOBS 16U

// --min-steal default, the minimum co-worker's queue size of pending jobs to steal a job from, smaller values result in higher job stealing rates, not less than 3
#define MIN_STEAL 3U

// --min-mmap and --max-mmap file size to allocate with mmap(), not greater than 4294967295LL, max 0 disables mmap()
#define MIN_MMAP_SIZE 16384
#define MAX_MMAP_SIZE 2147483648LL

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
const char *color_off = "";

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
bool flag_no_labels                = false;
bool flag_no_group                 = false;
bool flag_no_messages              = false;
bool flag_no_hidden                = false;
bool flag_count                    = false;
bool flag_fixed_strings            = false;
bool flag_free_space               = false;
bool flag_ignore_case              = false;
bool flag_smart_case               = false;
bool flag_invert_match             = false;
bool flag_only_line_number         = false;
bool flag_line_number              = false;
bool flag_column_number            = false;
bool flag_byte_offset              = false;
bool flag_line_buffered            = false;
bool flag_only_matching            = false;
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
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_max_count              = 0;
size_t flag_max_depth              = 0;
size_t flag_max_files              = 0;
size_t flag_jobs                   = 0;
size_t flag_tabs                   = 8;
size_t flag_min_mmap               = MIN_MMAP_SIZE;
size_t flag_max_mmap               = MAX_MMAP_SIZE;
size_t flag_min_steal              = MIN_STEAL;
const char *flag_pager             = NULL;
const char *flag_color             = NULL;
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
std::vector<std::string> flag_file;
std::vector<std::string> flag_file_type;
std::vector<std::string> flag_file_extensions;
std::vector<std::string> flag_file_magic;
std::vector<std::string> flag_include;
std::vector<std::string> flag_include_dir;
std::vector<std::string> flag_include_from;
std::vector<std::string> flag_include_override;
std::vector<std::string> flag_include_override_dir;
std::vector<std::string> flag_exclude;
std::vector<std::string> flag_exclude_dir;
std::vector<std::string> flag_exclude_from;
std::vector<std::string> flag_exclude_override;
std::vector<std::string> flag_exclude_override_dir;

void set_color(const char *grep_colors, const char *parameter, char color[COLORLEN]);
void trim(std::string& line);
bool is_output(ino_t inode);
size_t strtopos(const char *s, const char *msg);

void format(const char *format, size_t matches);
void help(const char *message = NULL, const char *arg = NULL);
void version();
void warning_is_directory(const char *pathname);
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
#if 1 // assume memchr() is fast
  const char *s = static_cast<const char*>(memchr(here, '\n', left));
  if (s == NULL)
    s = here + left;
  else
    ++s;
#else
  const char *s = here;
  const char *e = here + left;
  while (s < e)
    if (*s++ == '\n')
      break;
#endif
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
#if 1 // assume memchr() is fast
    const char *s = static_cast<const char*>(memchr(here, '\n', left));
    if (s == NULL)
      s = here + left;
    else
      ++s;
#else
    const char *s = here;
    const char *e = here + left;
    while (s < e)
      if (*s++ == '\n')
        break;
#endif
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
      if ((*s & 0x0c) == 0x80)
        return true;
    } while ((*s & 0x0c) != 0xc0 && ++s < e);

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

#ifndef OS_WIN
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
      files(),
      dirs(),
      fileno()
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

  // report the statistics
  void report()
  {
    size_t n = found();
    fprintf(output, "Searched %zu file%s", files, (files == 1 ? "" : "s"));
    if (threads > 1)
      fprintf(output, " with %zu threads", threads);
    if (dirs > 0)
      fprintf(output, " in %zu director%s", dirs, (dirs == 1 ? "y" : "ies"));
    if (n > 0)
      fprintf(output, ": found %zu matching file%s\n", n, (n == 1 ? "" : "s"));
    else
      fprintf(output, ": found no matches\n");
  }

  size_t             files;  // number of files searched
  size_t             dirs;   // number of directories searched
  std::atomic_size_t fileno; // number of matching files, atomic for GrepWorker::search() update

} stats;

// mmap state
struct MMap {

  MMap()
    :
      mmap_base(),
      mmap_size()
  { }

  ~MMap()
  {
#if !defined(OS_WIN) && MAX_MMAP_SIZE > 0
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

#if !defined(OS_WIN) && MAX_MMAP_SIZE > 0

  // get current input file and check if its encoding is plain
  FILE *file = input.file();
  if (file == NULL || input.file_encoding() != reflex::Input::file_encoding::plain)
    return false;

  // is this a regular file that is not too large (for size_t)?
  int fd = fileno(file);
  struct stat buf;
  if (fstat(fd, &buf) != 0 || !S_ISREG(buf.st_mode) || buf.st_size > MAX_MMAP_SIZE)
    return false;

  // is this file not too small or too large?
  size = static_cast<size_t>(buf.st_size);
  if (size < flag_min_mmap || size > flag_max_mmap)
    return false;

  // mmap the file
  if (mmap_base == NULL || mmap_size < size)
  {
    mmap_size = (size + 0xfff) & ~0xfffUL; // round up to 4K page size
    base = static_cast<const char*>(mmap_base = mmap(mmap_base, mmap_size, PROT_READ, MAP_PRIVATE, fd, 0));
  }
  else
  {
    base = static_cast<const char*>(mmap_base = mmap(mmap_base, mmap_size, PROT_READ, MAP_FIXED | MAP_PRIVATE, fd, 0));
  }

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

  static const size_t SIZE = 16384; // size of each buffer in the buffers container

  struct Buffer { char data[SIZE]; }; // a buffer in the buffers container

  typedef std::list<Buffer> Buffers; // buffers container

  // hex dump state
  struct Dump {

    // hex dump mode for color highlighting
    static const short HEX_MATCH         = 0;
    static const short HEX_LINE          = 1;
    static const short HEX_CONTEXT_MATCH = 2;
    static const short HEX_CONTEXT_LINE  = 3;

    // hex color highlights for HEX_MATCH, HEX_LINE, HEX_CONTEXT_MATCH, HEX_CONTEXT_LINE
    static const char *color_hex[4];

    Dump(Output& out)
      :
        out(out),
        offset()
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
      dump(*this)
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

  // output a number with field width w (padded with spaces)
  void num(size_t i, size_t w = 1)
  {
    char tmp[24];
    size_t n = 0;

    do
    {
      tmp[n++] = i % 10 + '0';
      i /= 10;
    }
    while (i > 0);

    while (w-- > n)
      chr(' ');

    while (n > 0)
      chr(tmp[--n]);
  }

  // output a number in hex with width w (padded with '0')
  void hex(size_t i, size_t w = 1)
  {
    char tmp[16];
    size_t n = 0;

    do
    {
      tmp[n++] = "0123456789abcdef"[i % 16];
      i /= 16;
    }
    while (i > 0);

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

  // flush the buffers
  void flush()
  {
    // if multi-threaded and lock is not owned already, then lock on master's mutex
    if (lock != NULL && !lock->owns_lock())
      lock->lock();

    // flush the buffers container to the designated output file, pipe, or stream
    for (Buffers::iterator i = buffers.begin(); i != buf; ++i)
      fwrite(i->data, 1, SIZE, file);
    fwrite(buf->data, 1, cur - buf->data, file);
    fflush(file);

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

  // flush and release synchronization on the master's mutex, if one was assigned before with sync()
  void release()
  {
    flush();

    // if multi-threaded and lock is owned, then release it
    if (lock != NULL && lock->owns_lock())
      lock->unlock();
  }

  // output the header part of the match, preceeding the matched line
  void header(const char *& pathname, size_t lineno, size_t columno, size_t byte_offset, const char *sep, bool newline);

  // output "Binary file ... matches"
  void binary_file_matches(const char *pathname);

  // output formatted match with options --format, --format-open, --format-close
  void format(const char *format, const char *pathname, size_t matches, reflex::AbstractMatcher *matcher);

  // output a quoted string with escapes for \ and "
  void quote(const char *data, size_t size);

  // output string in C/C++
  void cpp(const char *data, size_t size);

  // output string in JSON
  void json(const char *data, size_t size);

  // output quoted string in CSV
  void csv(const char *data, size_t size);

  // output string in XML
  void xml(const char *data, size_t size);

  std::unique_lock<std::mutex> *lock;    // synchronization lock
  FILE                         *file;    // output stream
  Dump                          dump;    // hex dump state
  Buffers                       buffers; // buffers container
  Buffers::iterator             buf;     // current buffer in the container
  char                         *cur;     // current position in the current buffer

};

// avoid constexpr (Visual Studio) and C++11 constexpr initializer oddity that causes link errors
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
      out.str(color_off);
    }
    else
    {
      short byte = bytes[i];
      out.str(color_hex[byte >> 8]);
      out.chr(' ');
      out.hex(byte & 0xff, 2);
      out.str(color_off);
    }

    if ((i & 7) == 7)
      out.chr(' ');
  }

  out.chr(' ');

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
      if (byte < 0x20 && flag_color != NULL)
      {
        out.str("\033[7m");
        out.chr('@' + byte);
      }
      else if (byte == 0x7f && flag_color != NULL)
      {
        out.str("\033[7m~");
      }
      else if (byte < 0x20 || byte >= 0x7f)
      {
        out.chr(' ');
      }
      else
      {
        out.chr(byte);
      }
      out.str(color_off);
    }
  }

  out.nl();

  for (size_t i = 0; i < 16; ++i)
    bytes[i] = -1;
}

// output the header part of the match, preceeding the matched line
void Output::header(const char *& pathname, size_t lineno, size_t columno, size_t byte_offset, const char *separator, bool newline)
{
  bool sep = false;

  if (pathname != NULL && flag_with_filename)
  {
    str(color_fn);
    str(pathname);
    str(color_off);

    if (flag_break)
    {
      chr('\n');
      pathname = NULL;
    }
    else if (flag_null)
    {
      chr('\0');
    }
    else
    {
      sep = true;
    }
  }

  if (flag_line_number || flag_only_line_number)
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
    num(columno, flag_initial_tab ? 3 : 1);
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
void Output::binary_file_matches(const char *pathname)
{
  if (flag_color)
  {
    str("\033[0mBinary file \033[1m");
    str(pathname);
    str("\033[0m matches");
  }
  else
  {
    str("Binary file ");
    str(pathname);
    str(" matches");
  }
  nl();
}

// output formatted match with options --format, --format-open, --format-close
void Output::format(const char *format, const char *pathname, size_t matches, reflex::AbstractMatcher *matcher)
{
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
        if (flag_with_filename)
        {
          if (a)
            str(a, s - a - 1);
          str(pathname);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'H':
        if (flag_with_filename)
        {
          if (a)
            str(a, s - a - 1);
          quote(pathname, strlen(pathname));
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'N':
        if (flag_line_number && !flag_only_line_number)
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

      case 'T':
        if (flag_initial_tab)
        {
          if (a)
            str(a, s - a - 1);
          chr('\t');
        }
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

      case '$':
        sep = a;
        len = s - a - 1;
        break;

      case 'f':
        str(pathname);
        break;

      case 'h':
        quote(pathname, strlen(pathname));
        break;

      case 'n':
        num(matcher->lineno());
        break;

      case 'k':
        num(matcher->columno() + 1);
        break;

      case 'b':
        num(matcher->first());
        break;

      case 't':
        chr('\t');
        break;

      case 's':
        if (sep != NULL)
          str(sep, len);
        else
          str(flag_separator);
        break;

      case '~':
        chr('\n');
        break;

      case 'w':
        num(matcher->wsize());
        break;

      case 'd':
        num(matcher->size());
        break;

      case 'm':
        num( matches + 1);
        break;

      case 'o':
        str(matcher->begin(), matcher->size());
        break;

      case 'q':
        quote(matcher->begin(), matcher->size());
        break;

      case 'c':
        cpp(matcher->begin(), matcher->size());
        break;

      case 'v':
        csv(matcher->begin(), matcher->size());
        break;

      case 'j':
        json(matcher->begin(), matcher->size());
        break;

      case 'x':
        xml(matcher->begin(), matcher->size());
        break;

      case '<':
        if (matches == 0 && a)
          str(a, s - a - 1);
        break;

      case '>':
        if (matches > 0 && a)
          str(a, s - a - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (matches > 0)
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
        std::pair<const char*,size_t> capture = (*matcher)[c == '#' ? strtoul(a != NULL ? a : "0", NULL, 10) : c - '0'];
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

// output string in JSON
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

// output string in XML
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
        case 9:
          str(t, s - t);
          t = s + 1;
          str("&#x9;");
          break;

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
            str("&#x");
            hex(c);
            chr(';');
          }
      }
    }
    ++s;
  }
  str(t, s - t);
}

// grep manages output, matcher, and input
struct Grep {

  Grep(FILE *file, reflex::AbstractMatcher *matcher)
    :
      out(file),
      matcher(matcher),
      file()
#ifdef HAVE_LIBZ
    , stream(),
      streambuf()
#endif
  { }

  // search a file
  virtual void search(const char *pathname);

  // open a file for (binary) reading and assign input, decompress the file when --z, --decompress specified
  bool open_file(const char *pathname)
  {
    if (fopen_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
    {
      warning("cannot read", pathname);
      return false;
    }

#ifdef HAVE_LIBZ
    if (flag_decompress)
    {
      streambuf = new zstreambuf(file);
      stream = new std::istream(streambuf);

#ifdef WITH_LIBZ_THREAD
      pipe_fd[0] = -1;
      pipe_fd[1] = -1;
      if (pipe(pipe_fd) == 0)
      {
        thread = std::thread(&Grep::decompress, this);
        input = fdopen(pipe_fd[0], "r");
      }
      else
#endif

      {
        input = stream;
      }
    }
    else
#endif

    {
      input = reflex::Input(file, flag_encoding_type);
    }

    return true;
  }

#ifdef HAVE_LIBZ
#ifdef WITH_LIBZ_THREAD
  void decompress()
  {
    char buf[Z_BUF_LEN]; // matches zstream buffer size

    while (*stream)
    {
      std::streamsize len = stream->read(buf, sizeof(buf)).gcount();

      if (len <= 0)
        break;

      write(pipe_fd[1], buf, static_cast<size_t>(len));
    }

    close(pipe_fd[1]);
  }
#endif
#endif
  
  // close the file and clear input
  void close_file()
  {
    input.clear();

    if (file != NULL)
    {
      fclose(file);
      file = NULL;
    }

#ifdef HAVE_LIBZ

#ifdef WITH_LIBZ_THREAD
    if (thread.joinable())
      thread.join();
#endif

    if (stream != NULL)
    {
      delete stream;
      stream = NULL;
    }

    if (streambuf != NULL)
    {
      delete streambuf;
      streambuf = NULL;
    }
#endif
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

#ifdef HAVE_BOOST_REGEX
      // buffer all input to work around Boost.Regex bug
      if (flag_perl_regexp)
        matcher->buffer();
#endif
    }
  }

  Output                   out;        // buffered and synchronized output
  reflex::AbstractMatcher *matcher;    // the matcher we're using
  MMap                     mmap;       // mmap state
  reflex::Input            input;      // input to the matcher
  FILE                    *file;       // the current input file
#ifdef HAVE_LIBZ
  std::istream            *stream;     // the current input stream ...
  zstreambuf              *streambuf;  // of the compressed file
#ifdef WITH_LIBZ_THREAD
  std::thread              thread;     // decompression thread
  int                      pipe_fd[2]; // decompressed stream pipe
#endif
#endif
};

// a job in the job queue
struct Job {

  // sentinel job NONE
  static const char *NONE;

  Job()
    : pathname()
  { }

  Job(const char *pathname)
    : pathname(pathname)
  { }

  bool none()
  {
    return pathname.empty();
  }

  std::string pathname;
};

// avoid constexpr (Visual Studio) and C++11 constexpr initializer oddity that causes link errors
const char *Job::NONE = "";

struct GrepWorker;

// master submits jobs to workers and supports lock-free job stealing
struct GrepMaster : public Grep {

  GrepMaster(FILE *file, reflex::AbstractMatcher *matcher)
    : Grep(file, matcher)
  {
    start_workers();
    iworker = workers.begin();
  }

  ~GrepMaster()
  {
    stop_workers();
  }

  // search a file by submitting it as a job to a worker
  virtual void search(const char *pathname)
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

  std::list<GrepWorker>           workers; // workers running threads
  std::list<GrepWorker>::iterator iworker; // the next worker to submit a job to
  std::mutex                      mutex;   // mutex to sync output, shared by workers

};

// worker runs a thread waiting for jobs submitted by the master
struct GrepWorker : public Grep {

  GrepWorker(FILE *file, reflex::AbstractMatcher *matcher, GrepMaster *master)
    :
      Grep(file, matcher->clone()),
      master(master),
      todo()
  {
    // all workers synchronize their output on the master's mutex lock
    out.sync(master->mutex);

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
    std::unique_lock<std::mutex> lock(mutex);

    jobs.emplace(pathname);
    ++todo;

    work.notify_one();
  }

  // receive a job for this worker, wait until one arrives
  void next_job(Job& job)
  {
    std::unique_lock<std::mutex> lock(mutex);

    while (jobs.empty())
      work.wait(lock);

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

    std::unique_lock<std::mutex> lock(mutex);
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

  std::thread                  thread; // thread of this worker, spawns GrepWorker::execute()
  GrepMaster                  *master; // the master of this worker
  std::mutex                   mutex;  // job queue mutex
  std::condition_variable      work;   // cv to control the job queue
  std::queue<Job>              jobs;   // queue of pending jobs submitted to this worker
  std::atomic_size_t           todo;   // number of jobs in the queue, for lock-free job stealing

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
    // if almost nothing to do, try stealing a job from a co-worker
    if (todo <= 1)
      master->steal(this);

    // wait for next job
    next_job(job);

    // worker should stop?
    if (job.none())
      break;

    // search the file for this job
    search(job.pathname.c_str());
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
  std::string regex;
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
      while (is_grouped && *++arg)
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
              flag_file_type.emplace_back(arg + 10);
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
            else if (strncmp(arg, "group-separator=", 16) == 0)
              flag_group_separator = arg + 16;
            else if (strcmp(arg, "help") == 0)
              help();
            else if (strcmp(arg, "hex") == 0)
              flag_binary_files = "hex";
            else if (strcmp(arg, "ignore-case") == 0)
              flag_ignore_case = true;
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
            else if (strcmp(arg, "no-dereference") == 0)
              flag_no_dereference = true;
            else if (strcmp(arg, "no-filename") == 0)
              flag_no_filename = true;
            else if (strcmp(arg, "no-group") == 0)
              flag_no_group = true;
            else if (strcmp(arg, "no-group-separator") == 0)
              flag_group_separator = NULL;
            else if (strcmp(arg, "no-hidden") == 0)
              flag_no_hidden = true;
            else if (strcmp(arg, "no-messages") == 0)
              flag_no_messages = true;
            else if (strcmp(arg, "no-mmap") == 0)
              flag_max_mmap = 0;
            else if (strcmp(arg, "null") == 0)
              flag_null = true;
            else if (strcmp(arg, "only-line-number") == 0)
              flag_only_line_number = true;
            else if (strcmp(arg, "only-matching") == 0)
              flag_only_matching = true;
            else if (strncmp(arg, "pager=", 6) == 0)
              flag_pager = arg + 6;
            else if (strncmp(arg, "pager", 5) == 0)
              flag_pager = "less -R";
            else if (strcmp(arg, "perl-regexp") == 0)
              flag_basic_regexp = !(flag_perl_regexp = true);
            else if (strcmp(arg, "quiet") == 0 || strcmp(arg, "silent") == 0)
              flag_quiet = flag_no_messages = true;
            else if (strcmp(arg, "recursive") == 0)
              flag_directories = "recurse";
            else if (strncmp(arg, "regexp=", 7) == 0)
              flag_regexp.emplace_back(arg + 7);
            else if (strncmp(arg, "separator=", 10) == 0)
              flag_separator = arg + 10;
            else if (strcmp(arg, "smart-case") == 0)
              flag_smart_case = true;
            else if (strcmp(arg, "stats") == 0)
              flag_stats = true;
            else if (strncmp(arg, "tabs=", 5) == 0)
              flag_tabs = strtopos(arg + 5, "invalid argument --tabs=");
            else if (strcmp(arg, "text") == 0)
              flag_binary_files = "text";
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
            flag_no_group = true;
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

          case 'k':
            flag_column_number = true;
            break;

          case 'L':
            flag_files_without_match = true;
            break;

          case 'l':
            flag_files_with_match = true;
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

          case 'N':
            flag_only_line_number = true;
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
            flag_basic_regexp = false;
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
              flag_file_type.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_type.emplace_back(argv[i]);
            else
              help("missing TYPES argument for option -t");
            is_grouped = false;
            break;

          case 'U':
            flag_binary = true;
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
    else if (options && pattern == NULL && flag_file.empty())
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

#ifndef HAVE_LIBZ
  // -z: but we don't have libz
  if (flag_decompress)
    help("option -z is not available in this version of ugrep");
#endif

  // -t list: list table of types and exit
  if (flag_file_type.size() == 1 && flag_file_type[0] == "list")
  {
    int i;

    std::cerr << std::setw(12) << "FILE TYPE" << "   FILE NAME EXTENSIONS (-O) AND FILE SIGNATURE 'MAGIC' BYTES (-M)" << std::endl;

    for (i = 0; type_table[i].type != NULL; ++i)
    {
      std::cerr << std::setw(12) << type_table[i].type << " = -O " << type_table[i].extensions << std::endl;
      if (type_table[i].magic)
        std::cerr << std::setw(19) << "-M '" << type_table[i].magic << "'" << std::endl;
    }

    exit(EXIT_ERROR);
  }

  // regex PATTERN specified
  if (pattern != NULL)
  {
    // if one or more -e PATTERN given, add pattern to the front else add to the front of FILE args
    if (flag_regexp.empty())
      flag_regexp.insert(flag_regexp.begin(), pattern);
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
      regex.append(".*\\n?|");
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
          regex.append(Q).append(pattern.substr(from, to - from - (pattern[to - 1] == '\r'))).append(E);
        from = to + 1;
      }

      if (from < pattern.size())
        regex.append(Q).append(pattern.substr(from)).append(E);

      if (pattern == "^$")
        flag_empty = true; // we're matching empty lines, so enable -Y
    }
  }

  if (!regex.empty())
  {
    // remove the ending '|' from the |-concatenated regexes in the regex string
    regex.pop_back();

    // -x or -w
    if (flag_line_regexp)
      regex.insert(0, "^(").append(")$"); // make the regex line-anchored
    else if (flag_word_regexp)
      regex.insert(0, "\\<(").append(")\\>"); // make the regex word-anchored

    // -x and -w do not apply to patterns in -f FILE when PATTERN specified
    flag_line_regexp = false;
    flag_word_regexp = false;

    // -F does not apply to patterns in -f FILE when PATTERN specified
    Q = "";
    E = "|";
  }

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

#ifndef OS_WIN
      if (file == NULL)
      {
        // could not open, try GREP_PATH environment variable
        const char *grep_path = getenv("GREP_PATH");

        if (grep_path != NULL)
        {
          std::string path_file(grep_path);
          path_file.append(PATHSEPSTR).append(filename);

          if (fopen_s(&file, path_file.c_str(), "r") != 0)
            file = NULL;
        }
      }
#endif

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
          // enable -o when the first line is ###-o
          if (lineno == 1 && line == "###-o")
            flag_only_matching = true;
          else
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

  // -y, -A, -B, or -C: disable -o
  if (flag_any_line || flag_after_context > 0 || flag_before_context > 0)
    flag_only_matching = false;

  // -v: disable -g and -o
  if (flag_invert_match)
    flag_no_group = flag_only_matching = false;

  // -c with -o is the same as -c with -g
  if (flag_count && flag_only_matching)
    flag_no_group = true;

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

  // display file name if more than one input file is specified or options -R, -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (flag_directories_action == RECURSE || files.size() > 1 || (flag_stdin && !files.empty())))
    flag_with_filename = true;

  // if no display options -H, -n, -N, -k, -b are set, enable --no-labels to suppress labels for speed
  if (!flag_with_filename && !flag_line_number && !flag_only_line_number && !flag_column_number && !flag_byte_offset)
    flag_no_labels = true;

  // normalize --cpp, --csv, --json, --xml
  if (flag_cpp)
  {
    flag_format_begin = "const struct grep {\n  const char *file;\n  size_t line;\n  size_t column;\n  size_t offset;\n  const char *match;\n} matches[] = {\n";
    flag_format_open  = "  // %f\n";
    flag_format       = "  { %h, %n, %k, %b, %c },\n";
    flag_format_close = "\n";
    flag_format_end   = "  { NULL, 0, 0, 0, NULL }\n};\n";
  }
  else if (flag_csv)
  {
    flag_format       = "%[,]$%H%N%K%B%v\n";
  }
  else if (flag_json)
  {
    flag_format_begin = "[";
    flag_format_open  = "%,\n  {\n    %[,\n    ]$%[\"file\": ]H\"matches\": [";
    flag_format       = "%,\n      { %[, ]$%[\"line\": ]N%[\"column\": ]K%[\"offset\": ]B\"match\": %j }";
    flag_format_close = "\n    ]\n  }";
    flag_format_end   = "\n]\n";
  }
  else if (flag_xml)
  {
    flag_format_begin = "<grep>\n";
    flag_format_open  = "  <file%[]$%[ name=]H>\n";
    flag_format       = "    <match%[\"]$%[ line=\"]N%[ column=\"]K%[ offset=\"]B>%x</match>\n";
    flag_format_close = "  </file>\n";
    flag_format_end   = "</grep>\n";
  }

  // is output sent to a TTY or to /dev/null?
  if (!flag_quiet)
  {
#ifndef OS_WIN

    // handle SIGPIPE
    signal(SIGPIPE, sigpipe_handle);

    // check if standard output is a TTY
    tty_term = isatty(STDOUT_FILENO);

    // --pager: if output is to a TTY then page through the results
    if (flag_pager != NULL && tty_term)
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

    // (re)set flag_color depending on color_term and TTY output
    if (flag_color != NULL)
    {
      if (strcmp(flag_color, "never") == 0)
      {
        flag_color = NULL;
      }
      else
      {
#ifndef OS_WIN

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
          const char *grep_color = NULL;
          const char *grep_colors = NULL;

#ifndef OS_WIN
          // get GREP_COLOR and GREP_COLORS environment variables
          grep_color = getenv("GREP_COLOR");
          grep_colors = getenv("GREP_COLORS");
#endif

          if (grep_color != NULL)
            set_color(std::string("mt=").append(grep_color).c_str(), "mt", color_mt);
          else if (grep_colors == NULL)
            grep_colors = "mt=1;31:cx=2:fn=35:ln=32:cn=32:bn=32:se=36";

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

            // if ms= is not specified, use the mt= value
            if (*color_ms == '\0')
              copy_color(color_ms, color_mt);

            // if mc= is not specified, use the mt= value
            if (*color_mc == '\0')
              copy_color(color_mc, color_mt);

            color_off = "\033[0m";
          }
        }
      }
    }
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
  for (auto& type : flag_file_type)
  {
    int i;

    // scan the type_table[] for a matching type
    for (i = 0; type_table[i].type != NULL; ++i)
      if (type == type_table[i].type)
        break;

    if (type_table[i].type == NULL)
      help("invalid argument --file-type=TYPE, to list the valid values use -tlist");

    flag_file_extensions.emplace_back(type_table[i].extensions);

    if (type_table[i].magic != NULL)
      flag_file_magic.emplace_back(type_table[i].magic);
  }

  // -O: add extensions as globs to the --include list
  for (auto& extensions : flag_file_extensions)
  {
    size_t from = 0;
    size_t to;
    std::string glob;

    while ((to = extensions.find(',', from)) != std::string::npos)
    {
      flag_include.emplace_back(glob.assign("*.").append(extensions.substr(from, to - from)));
      from = to + 1;
    }

    flag_include.emplace_back(glob.assign("*.").append(extensions.substr(from)));
  }

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

      // read globs from the specified file or files

      reflex::BufferedInput input(file);
      std::string line;

      while (true)
      {
        // read the next line
        if (getline(input, line))
          break;

        trim(line);

        // add glob to --exclude and --exclude-dir using gitignore rules
        if (!line.empty() && line.front() != '#')
        {
          // gitignore-style ! negate pattern (overrides --exclude and --exclude-dir)
          if (line.front() == '!' && !line.empty())
          {
            line.erase(0, 1);

            // globs ending in / should only match directories
            if (line.back() == '/')
              line.pop_back();
            else
              flag_exclude_override.emplace_back(line);

            flag_exclude_override_dir.emplace_back(line);
          }
          else
          {
            // remove leading \ if present
            if (line.front() == '\\' && !line.empty())
              line.erase(0, 1);

            // globs ending in / should only match directories
            if (line.back() == '/')
              line.pop_back();
            else
              flag_exclude.emplace_back(line);

            flag_exclude_dir.emplace_back(line);
          }
        }
      }

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

      // read globs from the specified file or files

      reflex::BufferedInput input(file);
      std::string line;

      while (true)
      {
        // read the next line
        if (getline(input, line))
          break;

        trim(line);

        // add glob to --include and --include-dir using gitignore rules
        if (!line.empty() && line.front() != '#')
        {
          // gitignore-style ! negate pattern (overrides --include and --include-dir)
          if (line.front() == '!' && !line.empty())
          {
            line.erase(0, 1);

            // globs ending in / should only match directories
            if (line.back() == '/')
              line.pop_back();
            else
              flag_include_override.emplace_back(line);

            flag_include_override_dir.emplace_back(line);
          }
          else
          {
            // remove leading \ if present
            if (line.front() == '\\' && !line.empty())
              line.erase(0, 1);

            // globs ending in / should only match directories
            if (line.back() == '/')
              line.pop_back();
            else
              flag_include.emplace_back(line);

            flag_include_dir.emplace_back(line);
          }
        }
      }

      if (file != stdin)
        fclose(file);
    }
  }

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

  // if no files were specified then read standard input, unless recursive searches are specified
  if (files.empty() && flag_directories_action != RECURSE)
    flag_stdin = true;

  // -M: create a magic matcher for the MAGIC regex signature to match file signatures with magic.scan()
  reflex::Pattern magic_pattern;
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
      reflex::BoostPerlMatcher matcher(pattern, matcher_options.c_str());
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
      help("option -P is not available in this version of ugrep");
#endif
    }
    else
    {
      // construct the RE/flex DFA pattern matcher and start matching files
      reflex::Pattern pattern(reflex::Matcher::convert(regex, convert_flags), "r");
      reflex::Matcher matcher(pattern, matcher_options.c_str());
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

  catch (boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<std::runtime_error> > error)
  {
    abort("Boost.Regex error: ", error.what());
  }
#endif

  catch (std::runtime_error& error)
  {
    abort("Exception: ", error.what());
  }

  // --stats: display stats when we're done
  if (flag_stats)
    stats.report();

#ifndef OS_WIN
  if (output != stdout)
    pclose(output);
#endif

  exit(stats.found_any_file() ? EXIT_OK : EXIT_FAIL);
}

// search the specified files or standard input for pattern matches
void ugrep(reflex::Matcher& magic, Grep& grep, std::vector<const char*>& files)
{
  // --format-begin
  if (flag_format_begin != NULL)
    format(flag_format_begin, 0);

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

      // search file or directory, get the basename from the file argument first
      const char *basename = strrchr(file, PATHSEPCHR);

      if (basename != NULL)
        ++basename;
      else
        basename = file;

      find(1, magic, grep, file, basename, DIRENT_TYPE_UNKNOWN, 0, !flag_no_dereference);
    }
  }

  // --format-end
  if (flag_format_end != NULL)
    format(flag_format_end, stats.found_files());
}

// search file or directory for pattern matches
void find(size_t level, reflex::Matcher& magic, Grep& grep, const char *pathname, const char *basename, int type, ino_t inode, bool is_argument_dereference)
{
  if (*basename == '.' && flag_no_hidden)
    return;

#ifdef OS_WIN

  DWORD attr = GetFileAttributesA(pathname);

  if (flag_no_hidden && (attr & FILE_ATTRIBUTE_HIDDEN))
    return;

  if ((attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    if (flag_directories_action == READ)
    {
      // directories cannot be read actually, so grep produces a warning message (errno is not set)
      warning_is_directory(pathname);
      return;
    }

    if (flag_directories_action == RECURSE)
    {
      // check for --exclude-dir and --include-dir constraints if pathname != "."
      if (strcmp(pathname, ".") != 0)
      {
        // do not exclude directories that are overridden by ! negation
        bool negate = false;
        for (auto& glob : flag_exclude_override_dir)
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
          // do not include directories that are overridden by ! negation
          for (auto& glob : flag_include_override_dir)
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
    // do not exclude files that are overridden by ! negation
    bool negate = false;
    for (auto& glob : flag_exclude_override)
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

      if (fopen_s(&file, pathname, flag_binary ? "rb" : "r") != 0)
      {
        warning("cannot read", pathname);
        return;
      }

      // if file has the magic bytes we're looking for: search the file
      if (magic.input(reflex::Input(file, flag_encoding_type)).scan() != 0)
      {
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
      // do not include files that are overridden by ! negation
      for (auto& glob : flag_include_override)
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
            warning_is_directory(pathname);
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
              // do not exclude directories that are overridden by ! negation
              bool negate = false;
              for (auto& glob : flag_exclude_override_dir)
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
                // do not include directories that are overridden by ! negation
                for (auto& glob : flag_include_override_dir)
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
          // do not exclude files that are overridden by ! negation
          bool negate = false;
          for (auto& glob : flag_exclude_override)
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

            if (fopen_s(&file, pathname, flag_binary ? "rb" : "r") != 0)
            {
              warning("cannot read", pathname);
              return;
            }

#ifdef HAVE_LIBZ
            if (flag_decompress)
            {
              zstreambuf streambuf(file);
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
            // do not include files that are overridden by ! negation
            for (auto& glob : flag_include_override)
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
  // --max-depth: recursion level exceeds max depth?
  if (flag_max_depth > 0 && level > flag_max_depth)
    return;

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

  stats.score_dir();

  std::string dirpathname;

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
    }
  } while (FindNextFileA(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  DIR *dir = opendir(pathname);

  if (dir == NULL)
  {
    warning("cannot open directory", pathname);

    return;
  }

  stats.score_dir();

  struct dirent *dirent = NULL;
  std::string dirpathname;

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
    }
  }

  closedir(dir);

#endif
}

// search input, display pattern matches, return true when pattern matched anywhere
void Grep::search(const char *pathname)
{
  if (pathname == NULL)
  {
    pathname = flag_label;
    input = stdin;
  }
  else if (!open_file(pathname))
  {
    return;
  }

  size_t matches = 0;
  
  if (flag_quiet || flag_files_with_match)
  {
    // -q, -l, or -L: report if a single pattern match was found in the input

    read_file();

    matches = matcher->find() != 0;

    if (flag_invert_match)
      matches = !matches;

    if (matches > 0)
      if (!stats.found())
        goto exit_search;

    // -l or -L
    if (matches > 0 && flag_files_with_match)
    {
      out.str(color_fn);
      out.str(pathname);
      out.str(color_off);
      out.chr(flag_null ? '\0' : '\n');
    }
  }
  else if (flag_count)
  {
    // -c: count the number of lines/patterns matched

    if (flag_no_group)
    {
      // -c with -g or -o: count the number of patterns matched in the file

      read_file();

      while (matcher->find() != 0)
      {
        ++matches;

        // -m: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;
      }
    }
    else
    {
      // -c without -g/-o: count the number of matching lines

      size_t lineno = 0;

      read_file();

      while (matcher->find())
      {
        size_t current_lineno = matcher->lineno();

        if (lineno != current_lineno)
        {
          lineno = current_lineno;

          ++matches;

          // -m: max number of matches reached?
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;
        }
      }

      if (flag_invert_match)
        matches = matcher->lineno() - matches - 1;
    }

    if (!stats.found())
      goto exit_search;

    if (flag_with_filename)
    {
      out.str(color_fn);
      out.str(pathname);
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
  else if (flag_format != NULL)
  {
    // --format

    read_file();

    while (matcher->find())
    {
      // --format-open
      if (matches == 0)
      {
        if (!stats.found())
          goto exit_search;

        if (flag_format_open != NULL)
          out.format(flag_format_open, pathname, stats.found_files(), matcher);
      }

      out.format(flag_format, pathname, matches, matcher);

      ++matches;

      // -m: max number of matches reached?
      if (flag_max_count > 0 && matches >= flag_max_count)
        break;
    }

    // --format-close
    if (matches > 0 && flag_format_close != NULL)
      out.format(flag_format_close, pathname, stats.found_files(), matcher);
  }
  else if (flag_only_matching || flag_only_line_number)
  {
    // -o or -N

    bool hex = false;
    bool binary = flag_hex;
    size_t lineno = 0;
    const char *separator = flag_separator;

    read_file();

    while (matcher->find())
    {
      size_t current_lineno = matcher->lineno();

      separator = lineno != current_lineno ? flag_separator : "+";

      if (lineno != current_lineno || flag_no_group)
      {
        // -m: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        lineno = current_lineno;

        if (matches == 0)
          if (!stats.found())
            goto exit_search;

        ++matches;

        if (!flag_no_labels)
        {
          if (hex && out.dump.offset < matcher->first())
            out.dump.done(flag_separator);

          binary = flag_hex || (!flag_text && is_binary(matcher->begin(), matcher->size()));

          out.header(pathname, lineno, matcher->columno() + 1, matcher->first(), separator, binary);
        }
      }

      if (!flag_only_line_number)
      {
        if (flag_hex)
        {
          if (hex)
            out.dump.next(matcher->first(), flag_separator);
          out.dump.hex(Output::Dump::HEX_MATCH, matcher->first(), matcher->begin(), matcher->size(), flag_separator);
          hex = true;
        }
        else if (binary)
        {
          if (flag_with_hex)
          {
            if (hex)
              out.dump.next(matcher->first(), flag_separator);
            out.dump.hex(Output::Dump::HEX_MATCH, matcher->first(), matcher->begin(), matcher->size(), flag_separator);
            hex = true;
          }
          else if (!flag_binary_without_matches)
          {
            out.binary_file_matches(pathname);
          }
        }
        else
        {
          const char *begin = matcher->begin();
          size_t size = matcher->size();

          if (flag_line_number)
          {
            // -o with -n: echo multi-line matches line-by-line

            const char *from = begin;
            const char *to;

            while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
            {
              out.str(color_ms);
              out.str(from, to - from + 1);
              out.str(color_off);

              if (to + 1 < begin + size)
                out.header(pathname, ++lineno, 1, matcher->first() + (to - begin) + 1, "|", binary);

              from = to + 1;
            }

            out.str(color_ms);
            out.str(from, size - (from - begin));
            out.str(color_off);
            if (size == 0 || begin[size - 1] != '\n')
              out.chr('\n');
          }
          else
          {
            out.str(color_ms);
            out.str(begin, size);
            out.str(color_off);
            if (size == 0 || begin[size - 1] != '\n')
              out.chr('\n');
          }

          if (flag_line_buffered)
            out.flush();
        }
      }
    }

    if (hex)
      out.dump.done(separator);
  }
  else
  {
    // read input line-by-line and display lines that match the pattern

    // mmap base and size, set with read_file() and mmap.file()
    const char *base = NULL;
    size_t size = 0;

    bool is_mmap = mmap.file(input, base, size);

    if (is_mmap && flag_before_context == 0 && flag_after_context == 0 && !flag_any_line && !flag_invert_match && !flag_no_group)
    {
      // this branch is the same as the next branch but optimized for mmap() when options -A, -B, -C, -g, -v, -y are not used

      const char *here = base;
      size_t left = size;

      size_t byte_offset = 0;
      size_t lineno = 1;

      while (true)
      {
        const char *line = here;

        // read the next line from mmap
        if (getline(here, left))
          break;

        bool binary = flag_hex;

        size_t last = UNDEFINED;

        // the current input line to match
        read_line(matcher, line, here - line);

        // search the line for pattern matches
        while (matcher->find())
        {
          if (last == UNDEFINED)
          {
            if (matches == 0)
              if (!stats.found())
                goto exit_search;

            if (!flag_text && !flag_hex)
            {
              if (is_binary(line, here - line))
              {
                if (flag_binary_without_matches)
                {
                  matches = 0;
                  break;
                }
                binary = true;
              }

              if (binary && !flag_with_hex)
              {
                out.binary_file_matches(pathname);
                matches = 1;
                goto done_search;
              }
            }

            if (!flag_no_labels)
              out.header(pathname, lineno, matcher->columno() + 1, byte_offset, flag_separator, binary);

            ++matches;

            last = 0;

            // if no color highlighting, then display the matched line at once
            if (flag_color == NULL)
              break;
          }

          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_LINE, byte_offset + last, line + last, matcher->first() - last, flag_separator);
            out.dump.hex(Output::Dump::HEX_MATCH, byte_offset + matcher->first(), matcher->begin(), matcher->size(), flag_separator);
          }
          else
          {
            out.str(color_sl);
            out.str(line + last, matcher->first() - last);
            out.str(color_off);
            out.str(color_ms);
            out.str(matcher->begin(), matcher->size());
            out.str(color_off);
          }

          last = matcher->last();

          // skip any further empty pattern matches
          if (last == 0)
            break;
        }

        if (last != UNDEFINED)
        {
          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_LINE, byte_offset + last, line + last, here - line - last, flag_separator);
            out.dump.done(flag_separator);
          }
          else
          {
            out.str(color_sl);
            out.str(line + last, here - line - last);
            out.str(color_off);
          }

          if (flag_line_buffered)
            out.flush();
        }

        // -m: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // update byte offset and line number
        byte_offset += here - line;
        ++lineno;
      }
    }
    else
    {
      // read input line-by-line and display lines that match the pattern with context lines

      // TODO: replace line-by-line reading with block reading to improve speed

      reflex::BufferedInput buffered_input;

      if (!is_mmap)
        buffered_input = input;

      const char *here = base;
      size_t left = size;

      size_t byte_offset = 0;
      size_t lineno = 1;
      size_t before = 0;
      size_t after = 0;

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

                if (!flag_no_labels)
                  out.header(pathname, lineno, matcher->columno() + 1, byte_offset, "-", binary[current]);

                last = 0;
              }

              if (binary[current])
              {
                out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current] + last, lines[current].c_str() + last, matcher->first() - last, "-");
              }
              else
              {
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
              out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current] + last, lines[current].c_str() + last, lines[current].size() - last, "-");
              out.dump.done("-");
            }
            else
            {
              out.str(color_cx);
              out.str(lines[current].c_str() + last, lines[current].size() - last);
              out.str(color_off);
            }
          }
          else if (!found)
          {
            if (matches == 0)
              if (!stats.found())
                goto exit_search;

            if (binary[current] && !flag_hex && !flag_with_hex)
            {
              out.binary_file_matches(pathname);
              matches = 1;
              break;
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
                    if (!flag_no_labels)
                      out.header(pathname, begin, matcher->columno() + 1, byte_offsets[begin_context], "-", binary[begin_context]);

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
                    out.str(color_cx);
                    out.str(lines[begin_context].c_str() + last, lines[begin_context].size() - last);
                    out.str(color_off);
                  }
                }

                ++begin;
              }

              // remember the matched line
              before = lineno;
            }

            if (!flag_no_labels)
              out.header(pathname, lineno, 1, byte_offsets[current], flag_separator, binary[current]);

            if (binary[current])
            {
              out.dump.hex(Output::Dump::HEX_LINE, byte_offsets[current], lines[current].c_str(), lines[current].size(), flag_separator);
              out.dump.done(flag_separator);
            }
            else
            {
              out.str(color_sl);
              out.str(lines[current].c_str(), lines[current].size());
              out.str(color_off);
            }

            if (flag_line_buffered)
              out.flush();

            ++matches;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;
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

            if (last == UNDEFINED && !flag_hex && !flag_hex && !flag_with_hex && binary[current])
            {
              out.binary_file_matches(pathname);
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

                if (!flag_no_labels)
                  out.header(pathname, begin, 1, byte_offsets[begin_context], "-", binary[begin_context]);

                if (binary[begin_context])
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[begin_context], lines[begin_context].c_str(), lines[begin_context].size(), "-");
                  out.dump.done("-");
                }
                else
                {
                  out.str(color_cx);
                  out.str(lines[begin_context].c_str(), lines[begin_context].size());
                  out.str(color_off);
                }

                ++begin;
              }

              // remember the matched line and we're done with the before context
              before = lineno;
              before_context = false;
            }

            if (flag_no_group && !binary[current])
            {
              // -g: do not group matches on a single line but on multiple lines, counting each match separately

              const char *separator = (last == UNDEFINED ? flag_separator : "+");

              if (!flag_no_labels)
                out.header(pathname, lineno, matcher->columno() + 1, byte_offset + matcher->first(), separator, binary[current]);

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
            }
            else
            {
              if (last == UNDEFINED)
              {
                if (!flag_no_labels)
                  out.header(pathname, lineno, matcher->columno() + 1, byte_offset, flag_separator, binary[current]);

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
              out.dump.done(flag_separator);
            }
            else if (!flag_no_group)
            {
              out.str(color_sl);
              out.str(lines[current].c_str() + last, lines[current].size() - last);
              out.str(color_off);
            }

            if (flag_line_buffered)
              out.flush();
          }
          else if (flag_any_line || (after > 0 && after + flag_after_context >= lineno))
          {
            // -A NUM: show context after matched lines, simulates BSD grep -A

            // display line as part of the after context of the matched line
            if (!flag_no_labels)
              out.header(pathname, lineno, 1, byte_offsets[current], "-", binary[current]);

            if (binary[current])
            {
              out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, byte_offsets[current], lines[current].c_str(), lines[current].size(), "-");
              out.dump.done("-");
            }
            else
            {
              out.str(color_cx);
              out.str(lines[current].c_str(), lines[current].size());
              out.str(color_off);
            }
          }

          // -m: max number of matches reached?
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;
        }

        // update byte offset and line number
        byte_offset += lines[current].size();
        ++lineno;
      }
    }
  }

done_search:

  // --break: add a line break and flush
  if (matches > 0 || flag_any_line)
  {
    if (flag_break)
      out.chr('\n');
  }

  // flush and release output to allow other workers to output results
  out.release();

exit_search:

  close_file();
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
        if (matches > 0)
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
        fprintf(output, "%zu", matches + 1);
        break;

      case '<':
        if (matches == 0 && a)
          fwrite(a, 1, s - a - 1, output);
        break;

      case '>':
        if (matches > 0 && a)
          fwrite(a, 1, s - a - 1, output);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (matches > 0)
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
size_t strtopos(const char *str, const char *msg)
{
  char *r = NULL;
  size_t size = static_cast<size_t>(strtoull(str, &r, 10));
  if (r == NULL || *r != '\0' || size == 0)
    help(msg, str);
  return size;
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
            respective matched line.  When used with option -g, displays the\n\
            offset in bytes of each pattern matched.  Byte offsets are exact\n\
            for binary, ASCII, and UTF-8 input.  Otherwise, the byte offset in\n\
            the UTF-8-converted input is displayed.\n\
    --binary-files=TYPE\n\
            Controls searching and reporting pattern matches in binary files.\n\
            Options are `binary', `without-match`, `text`, `hex`, and\n\
            `with-hex'.  The default is `binary' to search binary files and to\n\
            report a match without displaying the match.  `without-match'\n\
            ignores binary matches.  `text' treats all binary files as text,\n\
            which might output binary garbage to the terminal, which can have\n\
            problematic consequences if the terminal driver interprets some of\n\
            it as commands.  `hex' reports all matches in hexadecimal.\n\
            `with-hex` only reports binary matches in hexadecimal, leaving text\n\
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
            If -g or -o is specified, counts the number of patterns matched.\n\
            If -v is specified, counts the number of non-matching lines.\n\
    --color[=WHEN], --colour[=WHEN]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR or GREP_COLORS environment variable.  The possible\n\
            values of WHEN can be `never', `always', or `auto', where `auto'\n\
            marks up matches only when output on a terminal.\n\
    --cpp\n\
            Output file matches in C++.  See also option --format.\n\
    --csv\n\
            Output file matches in CSV.  Use options -H, -n, -k, and -b to\n\
            specify additional fields.  See also option --format.\n\
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
            This option is most useful when multiple -e options are used to\n\
            specify multiple patterns, when a pattern begins with a dash (`-'),\n\
            to specify a pattern after option -f or after the FILE arguments.\n\
    --exclude=GLOB\n\
            Skip files whose name matches GLOB (using wildcard matching).  A\n\
            glob can use *, ?, and [...] as wildcards, and \\ to quote a\n\
            wildcard or backslash character literally.  If GLOB contains /,\n\
            full pathnames are matched.  Otherwise basenames are matched.  Note\n\
            that --exclude patterns take priority over --include patterns.\n\
            This option may be repeated.\n\
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
            ignored.  When FILE is a `-', standard input is read.  This option\n\
            may be repeated.\n\
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
            When FILE is a `-', standard input is read.  This option may be\n\
            repeated.\n\
    --format=FORMAT\n\
            Output FORMAT-formatted matches.  See `man ugrep' section FORMAT\n\
            for the `%' fields.  Options -A, -B, -C, -y, and -v are disabled.\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression, i.e. make ugrep\n\
            behave as traditional grep.\n\
    -g, --no-group\n\
            Do not group multiple pattern matches on the same matched line.\n\
            Output the matched line again for each additional pattern match,\n\
            using `+' as the field separator for each additional match.\n\
    --group-separator=SEP\n\
            Use SEP as a group separator for context options -A, -B, and -C. By\n\
            default SEP is a double hyphen (`--').\n\
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
    --include=GLOB\n\
            Search only files whose name matches GLOB (using wildcard\n\
            matching).  A glob can use *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  If GLOB\n\
            contains /, file pathnames are matched.  Otherwise file basenames\n\
            are matched.  Note that --exclude patterns take priority over\n\
            --include patterns.  This option may be repeated.\n\
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
            simultaneously.  -J1 disables threading: files are matched in the\n\
            same order as the files specified.\n\
    -j, --smart-case\n\
            Perform case insensitive matching unless PATTERN contains a capital\n\
            letter.  Case insensitive matching applies to ASCII letters only.\n\
    --json\n\
            Output file matches in JSON.    Use options -H, -n, -k, and -b to\n\
            specify additional properties.  See also option --format.\n\
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
            Only files matching the signature pattern `MAGIC' are searched.\n\
            The signature \"magic bytes\" at the start of a file are compared\n\
            to the `MAGIC' regex pattern.  When matching, the file will be\n\
            searched.  This option may be repeated and may be combined with\n\
            options -O and -t to expand the search.  This option is relatively\n\
            slow as every file on the search path is read to compare `MAGIC'.\n\
    -m NUM, --max-count=NUM\n\
            Stop reading the input after NUM matches for each file processed.\n\
    --max-depth=NUM\n\
            Restrict recursive search to NUM (NUM > 0) directories deep, where\n\
            --max-depth=1 searches the specified path without visiting\n\
            sub-directories.  By comparison, -dskip skips all directories even\n\
            when they are on the command line.\n\
    --max-files=NUM\n\
            If -R or -r is specified, restrict the number of files matched to\n\
            NUM.  If -J1 is specified, files are matched in the same order as\n\
            the files specified.\n\
    -N, --only-line-number\n\
            The line number of the matching line in the file is output without\n\
            displaying the match.  The line number counter is reset for each\n\
            file processed.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B, and -C.\n\
    --no-hidden\n\
            Do not search hidden files and hidden directories.\n\
    --no-mmap\n\
            Do not use memory maps to search files.  By default, memory maps\n\
            are used under certain conditions to improve performance.\n\
    -O EXTENSIONS, --file-extensions=EXTENSIONS\n\
            Search only files whose file name extensions match the specified\n\
            comma-separated list of file name EXTENSIONS.  This option is the\n\
            same as specifying --include='*.ext' for each extension name `ext'\n\
            in the EXTENSIONS list.  This option may be repeated and may be\n\
            combined with options -M and -t to expand the search.\n\
    -o, --only-matching\n\
            Prints only the matching part of lines and allows pattern matches\n\
            across newlines to span multiple lines.  Line numbers for\n\
            multi-line matches are displayed with option -n, using `|' as the\n\
            field separator for each additional line matched by the pattern.\n\
            This option cannot be combined with options -A, -B, -C, -v, and -y.\n\
    -P, --perl-regexp\n\
            Interpret PATTERN as a Perl regular expression.\n";
#ifndef HAVE_BOOST_REGEX
  std::cout << "\
            This feature is not available in this version of ugrep.\n";
#endif
  std::cout << "\
    -p, --no-dereference\n\
            If -R or -r is specified, no symbolic links are followed, even when\n\
            they are on the command line.\n\
    --pager[=COMMAND]\n\
            When output is sent to the terminal, uses `COMMAND' to page through\n\
            the output.  The default COMMAND is `less -R'.  This option makes\n\
            --color=auto behave as --color=always.  Enables --break.\n\
    -Q ENCODING, --encoding=ENCODING\n\
            The input file encoding.  The possible values of ENCODING can be:";
  for (int i = 0; format_table[i].format != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 6 ? " " : "\n            ") << "`" << format_table[i].format << "'";
  std::cout << "\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress normal output.  ugrep will only search until a\n\
            match has been found, making searches potentially less expensive.\n\
            Allows a pattern match to span multiple lines.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory.  Follow all\n\
            symbolic links, unlike -r.  If -J1 is specified, files are matched\n\
            in the same order as the files specified.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are on the command line.  If -J1 is specified,\n\
            files are matched in the same order as the files specified.\n\
    -S, --dereference\n\
            If -r is specified, all symbolic links are followed, like -R.  The\n\
            default is not to follow symbolic links.\n\
    -s, --no-messages\n\
            Silent mode.  Nonexistent and unreadable files are ignored (i.e.\n\
            their error messages are suppressed).\n\
    --separator=SEP\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset, and the matched line.  The default is a colon\n\
            (`:').\n\
    --stats\n\
            Display statistics on the number of files and directories searched.\n\
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
            (use option -tlist to display a detailed list):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  std::cout << "\n\
    --tabs=NUM\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1, 2, 4, or 8.\n\
    -U, --binary\n\
            Disables Unicode matching for binary file matching, forcing PATTERN\n\
            to match bytes, not Unicode characters.  For example, -U '\\xa3'\n\
            matches byte A3 (hex) instead of the Unicode code point U+00A3\n\
            represented by the two-byte UTF-8 sequence C2 A3.\n\
    -V, --version\n\
            Display version information and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    -W, --with-hex\n\
            Only output binary matches in hexadecimal, leaving text matches\n\
            alone.  This option is equivalent to the --binary-files=with-hex\n\
            option.\n\
    -w, --word-regexp\n\
            The PATTERN or -e PATTERN are searched for as a word (as if\n\
            surrounded by \\< and \\>).  If PATTERN or -e PATTERN is also\n\
            specified, then this option does not apply to -f FILE patterns.\n\
    -X, --hex\n\
            Output matches in hexadecimal.  This option is equivalent to the\n\
            --binary-files=hex option.\n\
    -x, --line-regexp\n\
            Only input lines selected against the entire PATTERN or -e PATTERN\n\
            are considered to be matching lines (as if surrounded by ^ and $).\n\
            If PATTERN or -e PATTERN is also specified, then this option does\n\
            not apply to -f FILE patterns.\n\
    --xml\n\
            Output file matches in XML.  Use options -H, -n, -k, and -b to\n\
            specify additional attributes.  See also option --format.\n\
    -Y, --empty\n\
            Permits empty matches, such as `^\\h*$' to match blank lines.  Empty\n\
            matches are disabled by default.  Note that empty-matching patterns\n\
            such as `x?' and `x*' match all input, not only lines with `x'.\n\
    -y, --any-line\n\
            Any matching or non-matching line is output.  Non-matching lines\n\
            are output with the `-' separator as context of the matching lines.\n\
            See also the -A, -B, and -C options.\n\
    -Z, --null\n\
            Prints a zero-byte after the file name.\n\
    -z, --decompress\n\
            Search zlib-compressed (.gz) files.\n";
#ifndef HAVE_LIBZ
  std::cout << "\
            This feature is not available in this version of ugrep.\n";
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
    "Written by Robert van Engelen: <https://github.com/Genivia/ugrep>" << std::endl;
  exit(EXIT_OK);
}

// print to standard error: ... is a directory if -q is not specified
void warning_is_directory(const char *pathname)
{
  if (!flag_no_messages)
  {
    if (flag_color)
      fprintf(stderr, "\033[0mugrep: \033[1m%s\033[0m is a directory\n", pathname);
    else
      fprintf(stderr, "ugrep: %s is a directory\n", pathname);
  }
}

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
    if (color_term && isatty(STDERR_FILENO))
      fprintf(stderr, "\033[0mugrep: \033[1;35mwarning:\033[0m \033[1m%s %s:\033[0m\033[1;36m %s\033[0m\n", message, arg, errmsg);
    else
      fprintf(stderr, "ugrep: warning: %s %s: %s\n", message, arg, errmsg);
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
  if (color_term && isatty(STDERR_FILENO))
    fprintf(stderr, "\033[0mugrep: \033[1;31merror:\033[0m \033[1m%s %s:\033[0m\033[1;36m %s\033[0m\n\n", message, arg, errmsg);
  else
    fprintf(stderr, "ugrep: error: %s %s: %s\n\n", message, arg, errmsg);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message, const std::string& what)
{
  if (color_term && isatty(STDERR_FILENO))
    fprintf(stderr, "\033[0mugrep: \033[1;31m%s\033[0m\033[1m%s\033[0m\n", message, what.c_str());
  else
    fprintf(stderr, "ugrep: %s%s\n", message, what.c_str());
  exit(EXIT_ERROR);
}
