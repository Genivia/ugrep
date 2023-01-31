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
@brief     a pattern search utility written in C++11
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

For download and installation instructions:

  https://github.com/Genivia/ugrep

This program uses RE/flex:

  https://github.com/Genivia/RE-flex

Optional libraries to support options -P and -z:

  -P: PCRE2 or Boost.Regex
  -z: zlib (.gz)
  -z: libbz2 (.bz, bz2, .bzip2)
  -z: liblzma (.lzma, .xz)
  -z: liblz4 (.lz4)
  -z: libzstd (.zst, .zstd)

Build ugrep as follows:

  $ ./configure --enable-colors
  $ make -j

Git does not preserve time stamps so ./configure may fail, in that case do:

  $ autoreconf -fi
  $ ./configure --enable-colors
  $ make -j

After this, you may want to test ugrep and install it (optional):

  $ make test
  $ sudo make install

*/

#include "ugrep.hpp"
#include "glob.hpp"
#include "mmap.hpp"
#include "output.hpp"
#include "query.hpp"
#include "stats.hpp"
#include <reflex/matcher.h>
#include <reflex/fuzzymatcher.h>
#include <iomanip>
#include <cctype>
#include <limits>
#include <functional>
#include <list>
#include <deque>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sstream>

#ifdef OS_WIN

// compiling for a windows OS, except Cygwin and MinGW

// optionally enable --color=auto by default
// #define WITH_COLOR

// optionally enable PCRE2 for -P
// #define HAVE_PCRE2

// optionally enable Boost.Regex for -P
// #define HAVE_BOOST_REGEX

// optionally enable zlib for -z
// #define HAVE_LIBZ

// optionally enable libbz2 for -z
// #define HAVE_LIBBZ2

// optionally enable liblzma for -z
// #define HAVE_LIBLZMA

// optionally enable liblz4 for -z
// #define HAVE_LIBLZ4

// optionally enable libzstd for -z
// #define HAVE_LIBZSTD

#include <stringapiset.h>
#include <direct.h>

#else

// not compiling for a windows OS

#include <signal.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

// use PCRE2 for option -P
#ifdef HAVE_PCRE2
# include <reflex/pcre2matcher.h>
#else
// use Boost.Regex for option -P
# ifdef HAVE_BOOST_REGEX
#  include <reflex/boostmatcher.h>
# endif
#endif

// optional: specify an optimal decompression block size, default is 65536, must be larger than 1024 for tar extraction
// #define Z_BUF_LEN 16384
// #define Z_BUF_LEN 32768

// use zlib, libbz2, liblzma for option -z
#ifdef HAVE_LIBZ
# include "zstream.hpp"
#endif

// ugrep exit codes
#define EXIT_OK    0 // One or more lines were selected
#define EXIT_FAIL  1 // No lines were selected
#define EXIT_ERROR 2 // An error occurred

// limit the total number of threads spawn (i.e. limit spawn overhead), because grepping is practically IO bound
#ifndef MAX_JOBS
# define MAX_JOBS 16U
#endif

// limit the job queue size to wait to give the worker threads some slack
#ifndef MAX_JOB_QUEUE_SIZE
# define MAX_JOB_QUEUE_SIZE 65536
#endif

// a hard limit on the recursive search depth
#ifndef MAX_DEPTH
# define MAX_DEPTH 100
#endif

// --min-steal default, the minimum co-worker's queue size of pending jobs to steal a job from, smaller values result in higher job stealing rates, should not be less than 3
#ifndef MIN_STEAL
# define MIN_STEAL 3U
#endif

// use dirent d_type when available to improve performance
#ifdef HAVE_STRUCT_DIRENT_D_TYPE
# define DIRENT_TYPE_UNKNOWN DT_UNKNOWN
# define DIRENT_TYPE_LNK     DT_LNK
# define DIRENT_TYPE_DIR     DT_DIR
# define DIRENT_TYPE_REG     DT_REG
#else
# define DIRENT_TYPE_UNKNOWN 0
# define DIRENT_TYPE_LNK     1
# define DIRENT_TYPE_DIR     1
# define DIRENT_TYPE_REG     1
#endif

// the -M MAGIC pattern DFA constructed before threads start, read-only afterwards
reflex::Pattern magic_pattern; // concurrent access is thread safe
reflex::Matcher magic_matcher; // concurrent access is not thread safe

// the --filter-magic-label pattern DFA
reflex::Pattern filter_magic_pattern; // concurrent access is thread safe

// TTY detected
bool tty_term = false;

// color term detected
bool color_term = false;

#ifdef OS_WIN

// CTRL-C handler
BOOL WINAPI sigint(DWORD signal)
{
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
  {
    // be nice, reset colors on interrupt when sending output to a color TTY
    if (color_term)
      color_term = write(1, "\033[m", 3) > 0; // appease -Wunused-result
  }

  // return FALSE to invoke the next handler (when applicable) or just exit
  return FALSE;
}

#else

// SIGINT and SIGTERM handler
static void sigint(int sig)
{
  // reset to the default handler
  signal(sig, SIG_DFL);

  // be nice, reset colors on interrupt when sending output to a color TTY
  if (color_term)
    color_term = write(1, "\033[m", 3) > 0; // appease -Wunused-result

  // signal again
  kill(getpid(), sig);
}

#endif

// unique identifier (address) for standard input path
static const char *LABEL_STANDARD_INPUT = "(standard input)";

// full home directory path
const char *home_dir = NULL;

// ANSI SGR substrings extracted from GREP_COLORS
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

char match_ms[COLORLEN];  // --match or --tag: matched text in a selected line
char match_mc[COLORLEN];  // --match or --tag: matched text in a context line
char match_off[COLORLEN]; // --match or --tag: off

std::string color_wd; // hyperlink working directory path

const char *color_hl      = NULL; // hyperlink
const char *color_st      = NULL; // ST

const char *color_del     = ""; // erase line after the cursor
const char *color_off     = ""; // disable colors

const char *color_high    = ""; // stderr highlighted text
const char *color_error   = ""; // stderr error text
const char *color_warning = ""; // stderr warning text
const char *color_message = ""; // stderr error or warning message text

// number of concurrent threads for workers
size_t threads;

// number of warnings given
std::atomic_size_t warnings;

// redirectable source is standard input by default or a pipe
FILE *source = stdin;

// redirectable output destination is standard output by default or a pipe
FILE *output = stdout;

// Grep object handle, to cancel the search with cancel_ugrep()
struct Grep *grep_handle = NULL;

std::mutex grep_handle_mutex;

// set/clear the handle to be able to use cancel_ugrep()
void set_grep_handle(struct Grep*);
void clear_grep_handle();

#ifndef OS_WIN

// output file stat is available when stat() result is true
bool output_stat_result  = false;
bool output_stat_regular = false;
struct stat output_stat;

// container of inodes to detect directory cycles when symlinks are traversed with --dereference
std::set<ino_t> visited;

#ifdef HAVE_STATVFS
// containers of file system ids to exclude from recursive searches or include in recursive searches
std::set<uint64_t> exclude_fs_ids, include_fs_ids;
#endif

#endif

// ugrep command-line options
bool flag_all_threads              = false;
bool flag_any_line                 = false;
bool flag_basic_regexp             = false;
bool flag_best_match               = false;
bool flag_bool                     = false;
bool flag_confirm                  = DEFAULT_CONFIRM;
bool flag_count                    = false;
bool flag_cpp                      = false;
bool flag_csv                      = false;
bool flag_decompress               = false;
bool flag_dereference              = false;
bool flag_files                    = false;
bool flag_files_with_matches       = false;
bool flag_files_without_match      = false;
bool flag_fixed_strings            = false;
bool flag_hex_star                 = false;
bool flag_hex_cbr                  = true;
bool flag_hex_chr                  = true;
bool flag_hex_hbr                  = true;
bool flag_hidden                   = DEFAULT_HIDDEN;
bool flag_invert_match             = false;
bool flag_json                     = false;
bool flag_line_buffered            = false;
bool flag_line_regexp              = false;
bool flag_match                    = false;
bool flag_no_dereference           = false;
bool flag_no_header                = false;
bool flag_no_messages              = false;
bool flag_not                      = false;
bool flag_null                     = false;
bool flag_only_line_number         = false;
bool flag_only_matching            = false;
bool flag_perl_regexp              = false;
bool flag_pretty                   = DEFAULT_PRETTY;
bool flag_quiet                    = false;
bool flag_sort_rev                 = false;
bool flag_stdin                    = false;
bool flag_usage_warnings           = false;
bool flag_word_regexp              = false;
bool flag_xml                      = false;
bool flag_hex                      = false;
bool flag_with_hex                 = false;
bool flag_no_filename              = false;
bool flag_with_filename            = false;
Flag flag_binary;
Flag flag_binary_without_match;
Flag flag_break;
Flag flag_byte_offset;
Flag flag_column_number;
Flag flag_empty;
Flag flag_dotall;
Flag flag_free_space;
Flag flag_heading;
Flag flag_ignore_case;
Flag flag_initial_tab;
Flag flag_line_number;
Flag flag_smart_case;
Flag flag_text;
Flag flag_ungroup;
Sort flag_sort_key                 = Sort::NA;
Action flag_devices_action         = Action::UNSP;
Action flag_directories_action     = Action::UNSP;
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_fuzzy                  = 0;
size_t flag_hex_after              = 0;
size_t flag_hex_before             = 0;
size_t flag_hex_columns            = 16;
size_t flag_jobs                   = 0;
size_t flag_max_count              = 0;
size_t flag_max_depth              = 0;
size_t flag_max_files              = 0;
size_t flag_max_line               = 0;
size_t flag_max_mmap               = DEFAULT_MAX_MMAP_SIZE;
size_t flag_min_count              = 0;
size_t flag_min_depth              = 0;
size_t flag_min_line               = 0;
size_t flag_min_magic              = 1;
size_t flag_min_steal              = MIN_STEAL;
size_t flag_not_magic              = 0;
size_t flag_query                  = 0;
size_t flag_tabs                   = DEFAULT_TABS;
size_t flag_width                  = 0;
size_t flag_zmax                   = 1;
const char *flag_apply_color       = NULL;
const char *flag_binary_files      = "binary";
const char *flag_color             = DEFAULT_COLOR;
const char *flag_colors            = NULL;
const char *flag_config            = NULL;
const char *flag_devices           = NULL;
const char *flag_directories       = NULL;
const char *flag_encoding          = NULL;
const char *flag_filter            = NULL;
const char *flag_format            = NULL;
const char *flag_format_begin      = NULL;
const char *flag_format_close      = NULL;
const char *flag_format_end        = NULL;
const char *flag_format_open       = NULL;
const char *flag_group_separator   = "--";
const char *flag_hexdump           = NULL;
const char *flag_label             = LABEL_STANDARD_INPUT;
const char *flag_pager             = DEFAULT_PAGER;
const char *flag_replace           = NULL;
const char *flag_save_config       = NULL;
const char *flag_separator         = ":";
const char *flag_sort              = NULL;
const char *flag_stats             = NULL;
const char *flag_tag               = NULL;
const char *flag_view              = "";
std::string              flag_config_file;
std::set<std::string>    flag_config_options;
std::vector<std::string> flag_regexp;
std::vector<std::string> flag_file;
std::vector<std::string> flag_file_type;
std::vector<std::string> flag_file_extension;
std::vector<std::string> flag_file_magic;
std::vector<std::string> flag_filter_magic_label;
std::vector<std::string> flag_glob;
std::vector<std::string> flag_ignore_files;
std::vector<std::string> flag_include;
std::vector<std::string> flag_include_dir;
std::vector<std::string> flag_include_from;
std::vector<std::string> flag_include_fs;
std::vector<std::string> flag_exclude;
std::vector<std::string> flag_exclude_dir;
std::vector<std::string> flag_exclude_from;
std::vector<std::string> flag_exclude_fs;
std::vector<std::string> flag_all_include;
std::vector<std::string> flag_all_include_dir;
std::vector<std::string> flag_all_exclude;
std::vector<std::string> flag_all_exclude_dir;
reflex::Input::file_encoding_type flag_encoding_type = reflex::Input::file_encoding::plain;

// the CNF of Boolean search queries and patterns
CNF bcnf;

// ugrep command-line arguments pointing to argv[]
const char *arg_pattern = NULL;
std::vector<const char*> arg_files;

#ifdef OS_WIN
// store UTF-8 arguments decoded from wargv[] in strings to re-populate argv[] with pointers
std::list<std::string> arg_strings;
#endif

// function protos
void options(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int argc, const char **argv);
void option_regexp(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg, bool is_neg = false);
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void init(int argc, const char **argv);
void set_color(const char *colors, const char *parameter, char color[COLORLEN]);
void trim(std::string& line);
void trim_pathname_arg(const char *arg);
bool is_output(ino_t inode);
size_t strtonum(const char *string, const char *message);
size_t strtopos(const char *string, const char *message);
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message);
size_t strtofuzzy(const char *string, const char *message);
void import_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs);
void format(const char *format, size_t matches);
void usage(const char *message, const char *arg = NULL, const char *valid = NULL);
void help(std::ostream& out);
void help(const char *what = NULL);
void version();
void is_directory(const char *pathname);
void cannot_decompress(const char *pathname, const char *message);

// open a file where - means stdin/stdout and an initial ~ expands to home directory
int fopen_smart(FILE **file, const char *filename, const char *mode)
{
  if (filename == NULL || *filename == '\0')
    return errno = ENOENT;

  if (strcmp(filename, "-") == 0)
  {
    *file = strchr(mode, 'w') == NULL ? stdin : stdout;
    return 0;
  }

  if (*filename == '~')
    return fopenw_s(file, std::string(home_dir).append(filename + 1).c_str(), mode);

  return fopenw_s(file, filename, mode);
}

// read a line from buffered input, returns true when eof
inline bool getline(reflex::BufferedInput& input, std::string& line)
{
  int ch;

  line.erase();
  while ((ch = input.get()) != EOF)
  {
    if (ch == '\n')
      break;
    line.push_back(ch);
  }
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
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

// return true if s[0..n-1] contains a \0 (NUL) or is non-displayable invalid UTF-8, which depends on -U and -W
inline bool is_binary(const char *s, size_t n)
{
  // file is binary if it contains a \0 (NUL)
  if (memchr(s, '\0', n) != NULL)
    return true;

  // not -U or -W with -U: file is binary if it has UTF-8
  if (!flag_binary || flag_with_hex)
  {
    if (n == 1)
      return (*s & 0xc0) == 0x80;

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
inline void copy_color(char to[COLORLEN], const char from[COLORLEN])
{
  size_t len = std::min(strlen(from), static_cast<size_t>(COLORLEN - 1));

  memcpy(to, from, len);
  to[len] = '\0';

  char *comma = strchr(to, ',');
  if (comma != NULL)
    *comma = '\0';
}

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD

// decompression thread state with shared objects
struct Zthread {

  Zthread(bool chained, std::string& partname) :
      ztchain(NULL),
      zstream(NULL),
      zpipe_in(NULL),
      chained(chained),
      quit(false),
      stop(false),
      extracting(false),
      waiting(false),
      assigned(false),
      partnameref(partname)
  {
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;
  }

  ~Zthread()
  {
    if (zstream != NULL)
    {
      delete zstream;
      zstream = NULL;
    }
    if (ztchain != NULL)
    {
      delete ztchain;
      ztchain = NULL;
    }
  }

  // start decompression thread and open new pipe, returns pipe or NULL on failure, this function is called by the main Grep thread
  FILE *start(int ztstage, const char *pathname, FILE *file_in)
  {
    // return pipe
    FILE *pipe_in = NULL;

    // reset pipe descriptors, pipe is closed
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;

    // partnameref is not assigned yet, used only when this decompression thread is chained
    assigned = false;

    // open pipe between Grep (or previous decompression) thread and this (new) decompression thread
    if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "rb")) != NULL)
    {
      // recursively add decompression stages to decompress multi-compressed files
      if (ztstage > 1)
      {
        // create a new decompression chain if not already created
        if (ztchain == NULL)
          ztchain = new Zthread(true, partname);

        // close the input pipe from the next decompression stage in the chain, if still open
        if (zpipe_in != NULL)
        {
          fclose(zpipe_in);
          zpipe_in = NULL;
        }

        // start the next stage in the decompression chain, return NULL if failed
        zpipe_in = ztchain->start(ztstage - 1, pathname, file_in);
        if (zpipe_in == NULL)
          return NULL;

        // wait for the partname to be assigned by the next decompression thread in the decompression chain
        std::unique_lock<std::mutex> lock(ztchain->pipe_mutex);
        if (!ztchain->assigned)
          ztchain->part_ready.wait(lock);
        lock.unlock();

        // create or open a zstreambuf to (re)start the decompression thread, reading from zpipe_in from the next stage in the chain
        if (zstream == NULL)
          zstream = new zstreambuf(partname.c_str(), zpipe_in);
        else
          zstream->open(partname.c_str(), zpipe_in);
      }
      else
      {
        // create or open a zstreambuf to (re)start the decompression thread, reading from the source input
        if (zstream == NULL)
          zstream = new zstreambuf(pathname, file_in);
        else
          zstream->open(pathname, file_in);
      }

      if (thread.joinable())
      {
        // wake decompression thread in close_wait_zstream_open()
        pipe_zstrm.notify_one();
      }
      else
      {
        // start a new decompression thread
        try
        {
          // reset flags
          quit = false;
          stop = false;
          extracting = false;
          waiting = false;

          thread = std::thread(&Zthread::decompress, this);
        }

        catch (std::system_error&)
        {
          // thread creation failed
          fclose(pipe_in);
          close(pipe_fd[1]);
          pipe_fd[0] = -1;
          pipe_fd[1] = -1;

          warning("cannot create thread to decompress",  pathname);

          return NULL;
        }
      }
    }
    else
    {
      // pipe failed
      if (pipe_fd[0] != -1)
      {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        pipe_fd[0] = -1;
        pipe_fd[1] = -1;
      }

      warning("cannot create pipe to decompress",  pathname);

      return NULL;
    }

    return pipe_in;
  }

  // open pipe to the next file in the archive or return NULL, this function is called by the main Grep thread
  FILE *open_next(const char *pathname)
  {
    if (pipe_fd[0] != -1)
    {
      // our end of the pipe was closed earlier, before open_next() was called
      pipe_fd[0] = -1;

      // if extracting and the decompression filter thread is not yet waiting, then wait until decompression thread closed its end of the pipe
      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (!waiting)
        pipe_close.wait(lock);
      lock.unlock();

      // partnameref is not assigned yet, used only when this decompression thread is chained
      assigned = false;

      // extract the next file from the archive when applicable, e.g. zip format
      if (extracting)
      {
        FILE *pipe_in = NULL;

        // open pipe between worker and decompression thread, then start decompression thread
        if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "rb")) != NULL)
        {
          if (chained)
          {
            // use lock and wait for partname ready
            std::unique_lock<std::mutex> lock(pipe_mutex);
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();
            // wait for the partname to be set by the next decompression thread in the ztchain
            if (!assigned)
              part_ready.wait(lock);
            lock.unlock();
          }
          else
          {
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();
          }

          return pipe_in;
        }

        // failed to create a new pipe
        warning("cannot create pipe to decompress", chained ? NULL : pathname);

        if (pipe_fd[0] != -1)
        {
          close(pipe_fd[0]);
          close(pipe_fd[1]);
        }

        // reset pipe descriptors, pipe was closed
        pipe_fd[0] = -1;
        pipe_fd[1] = -1;

        // notify the decompression thread filter_tar/filter_cpio of the closed pipe
        pipe_ready.notify_one();

        // when an error occurred, we may still need to notify the receiver in case it is waiting on the partname
        std::unique_lock<std::mutex> lock(pipe_mutex);
        assigned = true;
        part_ready.notify_one();
        lock.unlock();
      }
    }

    return NULL;
  }

  // cancel decompression
  void cancel()
  {
    stop = true;

    // recursively cancel decompression threads in the chain
    if (ztchain != NULL)
      ztchain->cancel();
  }

  // join this thread, this function is called by the main Grep thread
  void join()
  {
    // --zmax: when quitting, recursively join all stages of the decompression thread chain
    if (ztchain != NULL)
      ztchain->join();

    if (thread.joinable())
    {
      // decompression thread should terminate, notify decompression thread when it is waiting in close_wait_zstream_open()
      std::unique_lock<std::mutex> lock(pipe_mutex);
      quit = true;
      pipe_zstrm.notify_one();
      lock.unlock();

      // now wait for the decomprssion thread to join
      thread.join();
    }

    // we can release the zstream that is no longer needed
    if (zstream != NULL)
    {
      delete zstream;
      zstream = NULL;
    }
  }

  // if the pipe was closed, then wait until the Grep thread opens a new pipe to search the next part in an archive
  bool wait_pipe_ready()
  {
    if (pipe_fd[1] == -1)
    {
      // signal close and wait until a new zstream pipe is ready
      std::unique_lock<std::mutex> lock(pipe_mutex);
      pipe_close.notify_one();
      waiting = true;
      pipe_ready.wait(lock);
      waiting = false;
      lock.unlock();

      // the receiver did not create a new pipe in close_file()
      if (pipe_fd[1] == -1)
        return false;
    }

    return true;
  }

  // close the pipe and wait until the Grep thread opens a new zstream and pipe for the next decompression job, unless quitting
  void close_wait_zstream_open()
  {
    if (pipe_fd[1] != -1)
    {
      // close our end of the pipe
      close(pipe_fd[1]);
      pipe_fd[1] = -1;
    }

    // signal close and wait until zstream is open
    std::unique_lock<std::mutex> lock(pipe_mutex);
    pipe_close.notify_one();
    if (!quit)
    {
      waiting = true;
      pipe_zstrm.wait(lock);
      waiting = false;
    }
    lock.unlock();
  }

  // decompression thread execution
  void decompress()
  {
    while (!quit)
    {
      // use the zstreambuf internal buffer to hold decompressed data
      unsigned char *buf;
      size_t maxlen;
      zstream->get_buffer(buf, maxlen);

      // reset flags
      extracting = false;
      waiting = false;

      // extract the parts of a zip file, one by one, if zip file detected
      while (!stop)
      {
        // to hold the path (prefix + name) extracted from a zip file
        std::string path;

        // a regular file, may be reset when unzipping a directory
        bool is_regular = true;

        const zstreambuf::ZipInfo *zipinfo = zstream->zipinfo();

        if (zipinfo != NULL)
        {
          // extracting a zip file
          extracting = true;

          if (!zipinfo->name.empty() && zipinfo->name.back() == '/')
          {
            // skip zip directories
            is_regular = false;
          }
          else
          {
            // save the zip path (prefix + name), since zipinfo will become invalid
            path.assign(zipinfo->name);

            // produce headers with zip file pathnames for each archived part (Grep::partname)
            if (!flag_no_filename)
              flag_no_header = false;
          }
        }

        // decompress a block of data into the buffer
        std::streamsize len = zstream->decompress(buf, maxlen);
        if (len < 0)
          break;

        bool is_selected = true;

        if (!filter_tar(path, buf, maxlen, len) && !filter_cpio(path, buf, maxlen, len))
        {
          // not a tar/cpio file, decompress the data into pipe, if not unzipping or if zipped file meets selection criteria
          is_selected = is_regular && (zipinfo == NULL || select_matching(path.c_str(), buf, static_cast<size_t>(len), true));

          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
            {
              // close the input pipe from the next decompression chain stage
              if (ztchain != NULL && zpipe_in != NULL)
              {
                fclose(zpipe_in);
                zpipe_in = NULL;
              }
              break;
            }

            // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the new pipe
            if (ztchain == NULL)
              partnameref.assign(std::move(path));
            else if (path.empty())
              partnameref.assign(partname);
            else
              partnameref.assign(partname).append(":").append(std::move(path));

            // notify the receiver of the new partname
            if (chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // push decompressed data into pipe
          bool drain = false;
          while (len > 0 && !stop)
          {
            // write buffer data to the pipe, if the pipe is broken then the receiver is waiting for this thread to join so we drain the rest of the decompressed data
            if (is_selected && !drain && write(pipe_fd[1], buf, static_cast<size_t>(len)) < len)
              drain = true;

            // decompress the next block of data into the buffer
            len = zstream->decompress(buf, maxlen);
          }
        }

        // break if not unzipping or if no more files to unzip
        if (zstream->zipinfo() == NULL)
        {
          // no decompression chain
          if (ztchain == NULL)
            break;

          // close the input pipe from the next decompression chain stage
          if (zpipe_in != NULL)
          {
            fclose(zpipe_in);
            zpipe_in = NULL;
          }

          // open pipe to the next file in an archive if there is a next file to extract
          zpipe_in = ztchain->open_next(partname.c_str());
          if (zpipe_in == NULL)
            break;

          // open a zstreambuf to (re)start the decompression thread
          zstream->open(partname.c_str(), zpipe_in);
        }

        // extracting a file
        extracting = true;

        // after extracting files from an archive, close our end of the pipe and loop for the next file
        if (is_selected && pipe_fd[1] != -1)
        {
          close(pipe_fd[1]);
          pipe_fd[1] = -1;
        }
      }

      extracting = false;

      // when an error occurred, we may still need to notify the receiver in case it is waiting on the partname
      if (chained)
        part_ready.notify_one();

      // close the pipe and wait until zstream pipe is open, unless quitting
      close_wait_zstream_open();
    }
  }

  // if tar file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_tar(const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
  {
    const int BLOCKSIZE = 512;

    if (len > BLOCKSIZE)
    {
      // v7 and ustar formats
      const char ustar_magic[8] = { 'u', 's', 't', 'a', 'r', 0, '0', '0' };
      bool is_ustar = *buf != '\0' && memcmp(buf + 257, ustar_magic, 8) == 0;

      // gnu and oldgnu formats
      const char gnutar_magic[8] = { 'u', 's', 't', 'a', 'r', ' ', ' ', 0 };
      bool is_gnutar = *buf != '\0' && memcmp(buf + 257, gnutar_magic, 8) == 0;

      // is this a tar archive?
      if (is_ustar || is_gnutar)
      {
        // produce headers with tar file pathnames for each archived part (Grep::partname)
        if (!flag_no_filename)
          flag_no_header = false;

        // inform the main grep thread we are extracting an archive
        extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // to hold long path extracted from the previous header block that is marked with typeflag 'x' or 'L'
        std::string long_path;

        while (!stop)
        {
          // tar header fields name, size and prefix and make them \0-terminated by overwriting fields we do not use
          buf[100] = '\0';
          const char *name = reinterpret_cast<const char*>(buf);
          // path prefix is up to 155 bytes (ustar) or up to 131 bytes (gnutar)
          buf[345 + (is_ustar ? 155 : 131)] = '\0';
          const char *prefix = reinterpret_cast<const char*>(buf + 345);

          // check GNU tar extension with leading byte 0x80 (unsigned positive) or leading byte 0xff (negative)
          size_t size = 0;
          if (buf[124] == 0x80)
          {
            // 11 byte big-endian size field without the leading 0x80
            for (short i = 125; i < 136; ++i)
              size = (size << 8) + buf[i];
          }
          else if (buf[124] == 0xff)
          {
            // a negative size makes no sense, but let's not ignore it and cast to unsigned
            for (short i = 124; i < 136; ++i)
              size = (size << 8) + buf[i];
          }
          else
          {
            buf[136] = '\0';
            size = strtoull(reinterpret_cast<const char*>(buf + 124), NULL, 8);
          }

          // header types
          unsigned char typeflag = buf[156];
          bool is_regular = typeflag == '0' || typeflag == '\0';
          bool is_xhd = typeflag == 'x';
          bool is_extended = typeflag == 'L';

          // padding size
          int padding = (BLOCKSIZE - size % BLOCKSIZE) % BLOCKSIZE;

          // assign the (long) tar pathname, name and prefix are now \0-terminated
          path.clear();
          if (long_path.empty())
          {
            if (*prefix != '\0')
            {
              path.assign(prefix);
              path.push_back('/');
            }
            path.append(name);
          }
          else
          {
            path.assign(std::move(long_path));
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
            const char *body = reinterpret_cast<const char*>(buf);
            const char *end = body + minlen;
            const char *key = "path=";
            const char *str = std::search(body, end, key, key + 5);
            if (str != NULL)
            {
              end = static_cast<const char*>(memchr(str, '\n', end - str));
              if (end != NULL)
                long_path.assign(str + 5, end - str - 5);
            }
          }
          else if (is_extended)
          {
            // typeflag 'L': get long name from the body
            const char *body = reinterpret_cast<const char*>(buf);
            long_path.assign(body, strnlen(body, minlen));
          }

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
              break;

            // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
            if (ztchain != NULL)
            {
              if (!partprefix.empty())
                partnameref.assign(partname).append(":").append(partprefix).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!partprefix.empty())
                partnameref.assign(partprefix).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // notify the receiver of the new partname after wait_pipe_ready()
            if (chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          while (len > 0 && !stop)
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
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
            break;

          // fill the rest of the buffer with decompressed data
          while (len < BLOCKSIZE || static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error or EOF?
            if (len_in <= 0)
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

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (chained)
          part_ready.notify_one();

        // done extracting the tar file
        return true;
      }
    }

    // not a tar file
    return false;
  }

  // if cpio file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_cpio(const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
  {
    const int HEADERSIZE = 110;

    if (len > HEADERSIZE)
    {
      // cpio odc format
      const char odc_magic[6] = { '0', '7', '0', '7', '0', '7' };

      // cpio newc format
      const char newc_magic[6] = { '0', '7', '0', '7', '0', '1' };

      // cpio newc+crc format
      const char newc_crc_magic[6] = { '0', '7', '0', '7', '0', '2' };

      // is this a cpio archive?
      if (memcmp(buf, odc_magic, 6) == 0 || memcmp(buf, newc_magic, 6) == 0 || memcmp(buf, newc_crc_magic, 6) == 0)
      {
        // produce headers with cpio file pathnames for each archived part (Grep::partname)
        if (!flag_no_filename)
          flag_no_header = false;

        // inform the main grep thread we are extracting an archive
        extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // need a new pipe, close current pipe first to create a new pipe
        bool in_progress = false;

        while (!stop)
        {
          // true if odc format, false if newc format
          bool is_odc = buf[5] == '7';

          // odc header length is 76, newc header length is 110
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

          while (len > 0 && !stop)
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
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
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
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

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
          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
              break;

            // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
            if (ztchain != NULL)
            {
              if (!partprefix.empty())
                partnameref.assign(partname).append(":").append(partprefix).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!partprefix.empty())
                partnameref.assign(partprefix).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // notify the receiver of the new partname
            if (chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          size = filesize;

          while (len > 0 && !stop)
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
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
            break;

          if (static_cast<size_t>(len) < maxlen)
          {
            // fill the rest of the buffer with decompressed data
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

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

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (chained)
          part_ready.notify_one();

        // done extracting the cpio file
        return true;
      }
    }

    // not a cpio file
    return false;
  }

  // true if path matches search constraints or buf contains magic bytes
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

      if (*basename == '.' && !flag_hidden)
        return false;

      // -O, -t, and -g (--include and --exclude): check if pathname or basename matches globs, is_selected = false if not
      if (!flag_all_exclude.empty() || !flag_all_include.empty())
      {
        // exclude files whose basename matches any one of the --exclude globs
        for (const auto& glob : flag_all_exclude)
          if (!(is_selected = !glob_match(path, basename, glob.c_str())))
            break;

        // include only if not excluded
        if (is_selected)
        {
          // include files whose basename matches any one of the --include globs
          for (const auto& glob : flag_all_include)
            if ((is_selected = glob_match(path, basename, glob.c_str())))
              break;
        }
      }

      // -M: check magic bytes, requires sufficiently large len of buf[] to match patterns, which is fine when Z_BUF_LEN is large e.g. 64K
      if (buf != NULL && !flag_file_magic.empty() && (flag_all_include.empty() || !is_selected))
      {
        // create a matcher to match the magic pattern, we cannot use magic_matcher because it is not thread safe
        reflex::Matcher magic(magic_pattern);
        magic.buffer(const_cast<char*>(reinterpret_cast<const char*>(buf)), len + 1);
        size_t match = magic.scan();
        is_selected = match == flag_not_magic || match >= flag_min_magic;
      }
    }

    return is_selected;
  }

  Zthread                *ztchain;     // chain of Zthread decompression threads to decompress multi-compressed/archived files
  zstreambuf             *zstream;     // the decompressed stream buffer from compressed input
  FILE                   *zpipe_in;    // input pipe from the next ztchain stage, if any
  std::thread             thread;      // decompression thread handle
  bool                    chained;     // true if decompression thread is chained before another decompression thread
  std::atomic_bool        quit;        // true if decompression thread should terminate to exit the program
  std::atomic_bool        stop;        // true if decompression thread should stop (cancel search)
  volatile bool           extracting;  // true if extracting files from TAR or ZIP archive (no concurrent r/w)
  volatile bool           waiting;     // true if decompression thread is waiting (no concurrent r/w)
  volatile bool           assigned;    // true when partnameref was assigned
  int                     pipe_fd[2];  // decompressed stream pipe
  std::mutex              pipe_mutex;  // mutex to extract files in thread
  std::condition_variable pipe_zstrm;  // cv to control new pipe creation
  std::condition_variable pipe_ready;  // cv to control new pipe creation
  std::condition_variable pipe_close;  // cv to control new pipe creation
  std::condition_variable part_ready;  // cv to control new partname creation to pass along decompression chains
  std::string             partname;    // name of the archive part extracted by the next decompressor in the ztchain
  std::string&            partnameref; // reference to the partname of Grep or of the previous decompressor

};

#endif
#endif

// grep manages output, matcher, input, and decompression
struct Grep {

  // CNF of AND/OR/NOT matchers
  typedef std::list<std::list<std::unique_ptr<reflex::AbstractMatcher>>> Matchers;

  // exit search exception
  struct EXIT_SEARCH : public std::exception { };

  // entry type
  enum class Type { SKIP, DIRECTORY, OTHER };

  // entry data extracted from directory contents, moves pathname to this entry
  struct Entry {

    static const uint16_t MIN_COST       = 0;
    static const uint16_t UNDEFINED_COST = 65534;
    static const uint16_t MAX_COST       = 65535;

    Entry(std::string& pathname, ino_t inode, uint64_t info)
      :
        pathname(std::move(pathname)),
        inode(inode),
        info(info),
        cost(UNDEFINED_COST)
    { }

    std::string pathname;
    ino_t       inode;
    uint64_t    info;
    uint16_t    cost;

#ifndef OS_WIN
    // get sortable info from stat buf
    static uint64_t sort_info(const struct stat& buf)
    {
#if defined(HAVE_STAT_ST_ATIM) && defined(HAVE_STAT_ST_MTIM) && defined(HAVE_STAT_ST_CTIM)
      // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
      return static_cast<uint64_t>(flag_sort_key == Sort::SIZE ? buf.st_size : flag_sort_key == Sort::USED ? static_cast<uint64_t>(buf.st_atim.tv_sec) * 1000000 + buf.st_atim.tv_nsec / 1000 : flag_sort_key == Sort::CHANGED ? static_cast<uint64_t>(buf.st_mtim.tv_sec) * 1000000 + buf.st_mtim.tv_nsec / 1000 : flag_sort_key == Sort::CREATED ? static_cast<uint64_t>(buf.st_ctim.tv_sec) * 1000000 + buf.st_ctim.tv_nsec / 1000 : 0);
#elif defined(HAVE_STAT_ST_ATIMESPEC) && defined(HAVE_STAT_ST_MTIMESPEC) && defined(HAVE_STAT_ST_CTIMESPEC)
      // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
      return static_cast<uint64_t>(flag_sort_key == Sort::SIZE ? buf.st_size : flag_sort_key == Sort::USED ? static_cast<uint64_t>(buf.st_atimespec.tv_sec) * 1000000 + buf.st_atimespec.tv_nsec / 1000 : flag_sort_key == Sort::CHANGED ? static_cast<uint64_t>(buf.st_mtimespec.tv_sec) * 1000000 + buf.st_mtimespec.tv_nsec / 1000 : flag_sort_key == Sort::CREATED ? static_cast<uint64_t>(buf.st_ctimespec.tv_sec) * 1000000 + buf.st_ctimespec.tv_nsec / 1000 : 0);
#else
      return static_cast<uint64_t>(flag_sort_key == Sort::SIZE ? buf.st_size : flag_sort_key == Sort::USED ? buf.st_atime : flag_sort_key == Sort::CHANGED ? buf.st_mtime : flag_sort_key == Sort::CREATED ? buf.st_ctime : 0);
#endif
    }
#endif

    // compare two entries by pathname
    static bool comp_by_path(const Entry& a, const Entry& b)
    {
      return a.pathname < b.pathname;
    }

    // compare two entries by size or time (atime, mtime, or ctime), if equal compare by pathname
    static bool comp_by_info(const Entry& a, const Entry& b)
    {
      return a.info < b.info || (a.info == b.info && a.pathname < b.pathname);
    }

    // compare two entries by edit distance cost
    static bool comp_by_best(const Entry& a, const Entry& b)
    {
      return a.cost < b.cost || (a.cost == b.cost && a.pathname < b.pathname);
    }

    // reverse compare two entries by pathname
    static bool rev_comp_by_path(const Entry& a, const Entry& b)
    {
      return a.pathname > b.pathname;
    }

    // reverse compare two entries by size or time (atime, mtime, or ctime), if equal reverse compare by pathname
    static bool rev_comp_by_info(const Entry& a, const Entry& b)
    {
      return a.info > b.info || (a.info == b.info && a.pathname > b.pathname);
    }

    // reverse compare two entries by edit distance cost
    static bool rev_comp_by_best(const Entry& a, const Entry& b)
    {
      return a.cost > b.cost || (a.cost == b.cost && a.pathname > b.pathname);
    }
  };

  // a job in the job queue
  struct Job {

    // sentinel job NONE
    static const size_t NONE = UNDEFINED_SIZE;

    Job()
      :
        pathname(),
        cost(Entry::UNDEFINED_COST),
        slot(NONE)
    { }

    Job(const char *pathname, uint16_t cost, size_t slot)
      :
        pathname(pathname != NULL ? pathname : ""),
        cost(cost),
        slot(slot)
    { }

    bool none()
    {
      return slot == NONE;
    }

    std::string pathname;
    uint16_t    cost;
    size_t      slot;
  };

#ifndef OS_WIN
  // extend the reflex::Input::Handler to handle stdin from a TTY or from a slow pipe
  struct StdInHandler : public reflex::Input::Handler {

    StdInHandler(Grep *grep)
      :
        grep(grep)
    { }

    Grep *grep;

    int operator()(FILE *file)
    {
      grep->out.flush();
      
      while (true)
      {
        struct timeval tv;
        fd_set rfds, efds;
        FD_ZERO(&rfds);
        FD_ZERO(&efds);
        FD_SET(0, &rfds);
        FD_SET(0, &efds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int r = ::select(fileno(file) + 1, &rfds, NULL, &efds, &tv);
        if (r < 0 && errno != EINTR)
          return 0;
        if (r > 0 && FD_ISSET(fileno(file), &efds))
          return 0;
        if (r > 0)
          break;
      }

      // clear EAGAIN and EINTR error on the nonblocking stdin
      clearerr(file);

      // no error
      return 1;
    }
  };
#endif

  // extend the reflex::AbstractMatcher::Handler with a grep object reference and references to some of the grep::search locals
  struct GrepHandler : public reflex::AbstractMatcher::Handler {

    GrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        grep(grep),
        pathname(pathname),
        lineno(lineno),
        binfile(binfile),
        hex(hex),
        binary(binary),
        matches(matches),
        stop(stop)
    { }

    Grep&        grep;     // grep object
    const char*& pathname; // grep::search argument pathname
    size_t&      lineno;   // grep::search lineno local variable
    bool&        binfile;  // grep::search binfile local variable
    bool&        hex;      // grep::search hex local variable
    bool&        binary;   // grep::search binary local variable
    size_t&      matches;  // grep::search matches local variable
    bool&        stop;     // grep::search stop local variable

    // get the start of the before context, if present
    void begin_before(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num, const char*& ptr, size_t& size, size_t& offset)
    {
      ptr = NULL;
      size = 0;
      offset = 0;

      if (len == 0)
        return;

      size_t current = matcher.lineno();
      size_t between = current - lineno;

      if (between > 1)
      {
        const char *s = buf + len;
        const char *e = s;

        if (buf[len - 1] != '\n')
          --between;

        while (--s >= buf)
        {
          if (*s == '\n')
          {
            if (--between == 0)
              break;;
            e = s + 1;
          }
        }

        ptr = ++s;
        size = e - s;
        offset = s - buf + num;

        ++lineno;
      }
    }

    // advance to the next before context, if present
    void next_before(const char *buf, size_t len, size_t num, const char*& ptr, size_t& size, size_t& offset)
    {
      if (ptr == NULL)
        return;

      const char *s = ptr + size;
      const char *e = buf + len;

      if (s >= e)
      {
        ptr = NULL;
      }
      else
      {
        e = static_cast<const char*>(memchr(s, '\n', e - s));

        if (e == NULL)
          e = buf + len;
        else
          ++e;

        ptr = s;
        size = e - s;
        offset = s - buf + num;

        ++lineno;
      }
    }
  };

  // extend event GrepHandler to output invert match lines for -v
  struct InvertMatchGrepHandler : public GrepHandler {

    InvertMatchGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, binfile, hex, binary, matches, stop)
    { }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        // --max-files: max reached?
        if (matches == 0 && !Stats::found_part())
        {
          stop = true;
          break;
        }

        // --max-count: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (!flag_text && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, lineno, NULL, offset, flag_separator, binary);

        hex = binary;

        if (binary)
        {
          grep.out.dump.hex(Output::Dump::HEX_LINE, offset, ptr, size);
        }
        else
        {
          bool lf_only = false;
          if (size > 0)
          {
            lf_only = ptr[size - 1] == '\n';
            size_t sizen = size - lf_only;
            if (sizen > 0)
            {
              grep.out.str(color_sl);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }
  };

  // extend event GrepHandler to output formatted invert match lines for --format -v
  struct FormatInvertMatchGrepHandler : public GrepHandler {

    FormatInvertMatchGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, binfile, hex, binary, matches, stop)
    { }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        // output --format-open
        if (matches == 0)
        {
          // --format-open or --format-close: we must acquire lock early before Stats::found_part()
          if (flag_format_open != NULL || flag_format_close != NULL)
            grep.out.acquire();

          // --max-files: max reached?
          if (!Stats::found_part())
          {
            stop = true;
            break;
          }

          if (flag_format_open != NULL)
            grep.out.format(flag_format_open, pathname, grep.partname, Stats::found_parts(), &matcher, false, Stats::found_parts() > 1);
        }

        // --max-count: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        // output --format
        grep.out.format_invert(flag_format, pathname, grep.partname, matches, lineno, offset, ptr, size - (size > 0 && ptr[size - 1] == '\n'), matches > 1);

        next_before(buf, len, num, ptr, size, offset);
      }
    }
  };

  // extend event GrepHandler to output any context lines for -y
  struct AnyLineGrepHandler : public GrepHandler {

    AnyLineGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        GrepHandler(grep, pathname, lineno, binfile, hex, binary, matches, stop),
        rest_line_data(rest_line_data),
        rest_line_size(rest_line_size),
        rest_line_last(rest_line_last)
    { }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // display the rest of the matching line before the context lines
      if (rest_line_data != NULL && (lineno != matcher.lineno() || flag_ungroup))
      {
        if (binary)
        {
          grep.out.dump.hex(flag_invert_match ? Output::Dump::HEX_CONTEXT_LINE : Output::Dump::HEX_LINE, rest_line_last, rest_line_data, rest_line_size);
          grep.out.dump.done();
        }
        else
        {
          bool lf_only = false;
          if (rest_line_size > 0)
          {
            lf_only = rest_line_data[rest_line_size - 1] == '\n';
            rest_line_size -= lf_only;
            if (rest_line_size > 0)
            {
              grep.out.str(flag_invert_match ? color_cx : color_sl);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        rest_line_data = NULL;
      }

      // context colors with or without -v
      short v_hex_context_line = flag_invert_match ? Output::Dump::HEX_LINE : Output::Dump::HEX_CONTEXT_LINE;
      const char *v_color_cx = flag_invert_match ? color_sl : color_cx;
      const char *separator = flag_invert_match ? flag_separator : "-";

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        if (matches == 0 && flag_invert_match)
        {
          // --max-files: max reached?
          if (!Stats::found_part())
          {
            stop = true;
            break;
          }
        }

        // --max-count: max number of matches reached?
        if (flag_invert_match && flag_max_count > 0 && matches >= flag_max_count)
        {
          stop = true;
          break;
        }

        // output blocked?
        if (grep.out.eof)
          break;

        if (flag_with_hex)
          binary = false;

        if (flag_invert_match)
          ++matches;

        binary = binary || flag_hex || (!flag_text && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, lineno, NULL, offset, separator, binary);

        hex = binary;

        if (binary)
        {
          grep.out.dump.hex(v_hex_context_line, offset, ptr, size);
        }
        else
        {
          bool lf_only = false;
          if (size > 0)
          {
            lf_only = ptr[size - 1] == '\n';
            size_t sizen = size - lf_only;
            if (sizen > 0)
            {
              grep.out.str(v_color_cx);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }

    const char*& rest_line_data;
    size_t&      rest_line_size;
    size_t&      rest_line_last;

  };

  // extend event AnyLineGrepHandler to output specific context lines for -A, -B and -C
  struct ContextGrepHandler : public AnyLineGrepHandler {

    // context state to track context lines before and after a match
    struct ContextState {

      ContextState()
        :
          before_index(0),
          before_length(0),
          after_lineno(0),
          after_length(flag_after_context)
      {
        before_binary.resize(flag_before_context);
        before_offset.resize(flag_before_context);
        before_line.resize(flag_before_context);
      }

      size_t                   before_index;  // before context rotation index
      size_t                   before_length; // accumulated length of the before context
      std::vector<bool>        before_binary; // before context binary line
      std::vector<size_t>      before_offset; // before context offset of line
      std::vector<std::string> before_line;   // before context line data
      size_t                   after_lineno;  // after context line number
      size_t                   after_length;  // accumulated length of the after context

    };

    ContextGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        AnyLineGrepHandler(grep, pathname, lineno, binfile, hex, binary, matches, stop, rest_line_data, rest_line_size, rest_line_last)
    { }

    // display the before context
    void output_before_context()
    {
      // the group separator indicates lines skipped, like GNU grep
      if (state.after_lineno > 0 && state.after_lineno + state.after_length < grep.matcher->lineno() - state.before_length)
      {
        if (hex)
          grep.out.dump.done();

        if (flag_group_separator != NULL)
        {
          grep.out.str(color_se);
          grep.out.str(flag_group_separator);
          grep.out.str(color_off);
          grep.out.nl();
        }
      }

      // output the before context
      if (state.before_length > 0)
      {
        // the first line number of the before context
        size_t before_lineno = grep.matcher->lineno() - state.before_length;

        for (size_t i = 0; i < state.before_length; ++i)
        {
          size_t j = (state.before_index + i) % state.before_length;

          if (hex && !state.before_binary[j])
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, before_lineno + i, NULL, state.before_offset[j], "-", state.before_binary[j]);

          hex = state.before_binary[j];

          const char *ptr = state.before_line[j].c_str();
          size_t size = state.before_line[j].size();

          if (hex)
          {
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, state.before_offset[j], ptr, size);
          }
          else
          {
            bool lf_only = false;
            if (size > 0)
            {
              lf_only = ptr[size - 1] == '\n';
              size -= lf_only;
              if (size > 0)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr, size);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }
        }
      }

      // reset the before context state
      state.before_index = 0;
      state.before_length = 0;
    }

    // set the after context
    void set_after_lineno(size_t lineno)
    {
      // set the after context state with the first after context line number
      state.after_length = 0;
      state.after_lineno = lineno;
    }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // display the rest of the matching line before the context lines
      if (rest_line_data != NULL && (lineno != matcher.lineno() || flag_ungroup))
      {
        if (binary)
        {
          grep.out.dump.hex(flag_invert_match ? Output::Dump::HEX_CONTEXT_LINE : Output::Dump::HEX_LINE, rest_line_last, rest_line_data, rest_line_size);
        }
        else
        {
          bool lf_only = false;
          if (rest_line_size > 0)
          {
            lf_only = rest_line_data[rest_line_size - 1] == '\n';
            rest_line_size -= lf_only;
            if (rest_line_size > 0)
            {
              grep.out.str(flag_invert_match ? color_cx : color_sl);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        rest_line_data = NULL;
      }

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        if (matches == 0 && flag_invert_match)
        {
          // --max-files: max reached?
          if (!Stats::found_part())
          {
            stop = true;
            break;
          }
        }

        // --max-count: max number of matches reached?
        if (flag_invert_match && flag_max_count > 0 && matches >= flag_max_count)
        {
          stop = true;
          break;
        }

        // output blocked?
        if (grep.out.eof)
          break;

        if (flag_invert_match)
          ++matches;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (!flag_text && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (state.after_lineno > 0 && state.after_length < flag_after_context)
        {
          ++state.after_length;

          if (hex && !binary)
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, lineno, NULL, offset, "-", binary);

          hex = binary;

          if (binary)
          {
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, offset, ptr, size);
          }
          else
          {
            bool lf_only = false;
            if (size > 0)
            {
              lf_only = ptr[size - 1] == '\n';
              size_t sizen = size - lf_only;
              if (sizen > 0)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr, sizen);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }
        }
        else if (flag_before_context > 0)
        {
          if (state.before_length < flag_before_context)
            ++state.before_length;
          state.before_index %= state.before_length;
          state.before_binary[state.before_index] = binary;
          state.before_offset[state.before_index] = offset;
          state.before_line[state.before_index].assign(ptr, size);
          ++state.before_index;
        }
        else
        {
          break;
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }

    ContextState state;

  };

  // extend event AnyLineGrepHandler to output specific context lines for -A, -B and -C with -v
  struct InvertContextGrepHandler : public AnyLineGrepHandler {

    struct InvertContextMatch {

      InvertContextMatch(size_t pos, size_t size, size_t offset)
        :
          pos(pos),
          size(size),
          offset(offset)
      { }

      size_t pos;    // position on the line
      size_t size;   // size of the match
      size_t offset; // size of the match

    };

    typedef std::vector<InvertContextMatch> InvertContextMatches;

    // context state to track matching lines before non-matching lines
    struct InvertContextState {

      InvertContextState()
        :
          before_index(0),
          before_length(0),
          after_lineno(0)
      {
        before_binary.resize(flag_before_context);
        before_columno.resize(flag_before_context);
        before_offset.resize(flag_before_context);
        before_line.resize(flag_before_context);
        before_match.resize(flag_before_context);
      }

      size_t                            before_index;   // before context rotation index
      size_t                            before_length;  // accumulated length of the before context
      std::vector<bool>                 before_binary;  // before context binary line
      std::vector<size_t>               before_columno; // before context column number of first match
      std::vector<size_t>               before_offset;  // before context offset of first match
      std::vector<std::string>          before_line;    // before context line data
      std::vector<InvertContextMatches> before_match;   // before context matches per line
      size_t                            after_lineno;   // the after context line number

    };

    InvertContextGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        AnyLineGrepHandler(grep, pathname, lineno, binfile, hex, binary, matches, stop, rest_line_data, rest_line_size, rest_line_last)
    { }

    // display the before context
    void output_before_context()
    {
      // the group separator indicates lines skipped, like GNU grep
      if (state.after_lineno > 0 && state.after_lineno + flag_after_context + flag_before_context < lineno && flag_group_separator != NULL)
      {
        if (hex)
          grep.out.dump.done();

        grep.out.str(color_se);
        grep.out.str(flag_group_separator);
        grep.out.str(color_off);
        grep.out.nl();
      }

      // output the before context
      if (state.before_length > 0)
      {
        // the first line number of the before context
        size_t before_lineno = lineno - state.before_length;

        for (size_t i = 0; i < state.before_length; ++i)
        {
          size_t j = (state.before_index + i) % state.before_length;
          size_t offset = state.before_match[j].empty() ? state.before_offset[j] : state.before_match[j].front().offset;

          if (hex && !state.before_binary[j])
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, before_lineno + i, NULL, offset, "-", state.before_binary[j]);

          hex = state.before_binary[j];

          const char *ptr = state.before_line[j].c_str();
          size_t size = state.before_line[j].size();
          size_t pos = 0;

          for (auto& match : state.before_match[j])
          {
            if (hex)
            {
              grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, match.offset - (match.pos - pos), ptr + pos, match.pos - pos);
              grep.out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, match.offset, ptr + match.pos, match.size);
            }
            else
            {
              if (match.pos > pos)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr + pos, match.pos - pos);
                grep.out.str(color_off);
              }

              if (match.size > 0)
              {
                size_t sizen = match.size - (ptr[match.pos + match.size - 1] == '\n');
                if (sizen > 0)
                {
                  grep.out.str(match_mc);
                  grep.out.str(ptr + match.pos, sizen);
                  grep.out.str(match_off);
                }
              }
            }

            pos = match.pos + match.size;
          }

          if (hex)
          {
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, state.before_offset[j] + pos, ptr + pos, size - pos);
          }
          else
          {
            bool lf_only = false;
            if (size > pos)
            {
              lf_only = ptr[size - 1] == '\n';
              size -= lf_only;
              if (size > pos)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr + pos, size - pos);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }
        }
      }

      // reset the context state
      state.before_index = 0;
      state.before_length = 0;
      state.after_lineno = lineno;
    }

    // add line with the first match to the before context
    void add_before_context_line(const char *bol, const char *eol, size_t columno, size_t offset)
    {
      if (state.before_length < flag_before_context)
        ++state.before_length;
      state.before_index %= state.before_length;
      state.before_binary[state.before_index] = binary;
      state.before_columno[state.before_index] = columno;
      state.before_offset[state.before_index] = offset;
      state.before_line[state.before_index].assign(bol, eol - bol);
      state.before_match[state.before_index].clear();
      ++state.before_index;
    }

    // add match fragment to the before context
    void add_before_context_match(size_t pos, size_t size, size_t offset)
    {
      // only add a match if we have a before line, i.e. not an after line with a multiline match
      if (state.before_length > 0)
      {
        size_t index = (state.before_index + state.before_length - 1) % state.before_length;
        state.before_match[index].emplace_back(pos, size, offset);
      }
    }

    // set the after context
    void set_after_lineno(size_t lineno)
    {
      state.after_lineno = lineno;
    }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // display the rest of the "after" matching line
      if (rest_line_data != NULL && (lineno != matcher.lineno() || flag_ungroup))
      {
        if (binary)
        {
          grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, rest_line_last, rest_line_data, rest_line_size);
        }
        else
        {
          bool lf_only = false;
          if (rest_line_size > 0)
          {
            lf_only = rest_line_data[rest_line_size - 1] == '\n';
            rest_line_size -= lf_only;
            if (rest_line_size > 0)
            {
              grep.out.str(color_cx);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        rest_line_data = NULL;
      }

      if (ptr != NULL)
        output_before_context();

      while (ptr != NULL)
      {
        state.after_lineno = lineno + 1;

        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        if (matches == 0)
        {
          // --max-files: max reached?
          if (!Stats::found_part())
          {
            stop = true;
            break;
          }
        }

        // --max-count: max number of matches reached?
        if (flag_invert_match && flag_max_count > 0 && matches >= flag_max_count)
        {
          stop = true;
          break;
        }

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (!flag_text && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, lineno, NULL, offset, flag_separator, binary);

        hex = binary;

        if (binary)
        {
          grep.out.dump.hex(Output::Dump::HEX_LINE, offset, ptr, size);
        }
        else
        {
          bool lf_only = false;
          if (size > 0)
          {
            lf_only = ptr[size - 1] == '\n';
            size_t sizen = size - lf_only;
            if (sizen > 0)
            {
              grep.out.str(color_sl);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl(lf_only);
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }

    InvertContextState state;

  };

  Grep(FILE *file, reflex::AbstractMatcher *matcher, Matchers *matchers)
    :
      out(file),
      matcher(matcher),
      matchers(matchers),
      file_in(NULL)
#ifndef OS_WIN
    , stdin_handler(this)
#endif
#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
    , zthread(false, partname)
#else
    , zstream(NULL)
    , stream(NULL)
#endif
#endif
  {
    restline.reserve(256); // pre-reserve a "rest line" of input to display matches to limit heap allocs
  }

  virtual ~Grep()
  {
#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
    zthread.join();
#else
    if (stream != NULL)
    {
      delete stream;
      stream = NULL;
    }
    if (zstream != NULL)
    {
      delete zstream;
      zstream = NULL;
    }
#endif
#endif
  }

  // cancel all active searches
  void cancel()
  {
    // global cancellation is forced by cancelling the shared output
    out.cancel();

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
    // -z: cancel decompression thread's decompression loop
    if (flag_decompress)
      zthread.cancel();
#endif
#endif
  }

  // search the specified files or standard input for pattern matches
  virtual void ugrep();

  // search file or directory for pattern matches
  Type select(size_t level, const char *pathname, const char *basename, int type, ino_t& inode, uint64_t& info, bool is_argument = false);

  // recurse a directory
  virtual void recurse(size_t level, const char *pathname);

  // -Z and --sort=best: perform a presearch to determine edit distance cost, return cost of pathname file, MAX_COST when no match is found
  uint16_t compute_cost(const char *pathname);

  // search a file
  virtual void search(const char *pathname, uint16_t cost);

  // check CNF AND/OR/NOT conditions are met for the line(s) spanning bol to eol
  bool cnf_matching(const char *bol, const char *eol, bool acquire = false)
  {
    if (flag_files)
    {
      if (out.holding())
      {
        size_t k = 0;    // iterate over matching[] bitmask
        bool all = true; // all terms matched

        // for each AND term check if the AND term was matched before or has a match this time
        for (const auto& i : *matchers)
        {
          // an OR term hasn't matched before
          if (!matching[k])
          {
            auto j = i.begin();
            auto e = i.end();

            if (j != e)
            {
              // check OR terms
              if (*j && (*j)->buffer(const_cast<char*>(bol), eol - bol + 1).find() != 0)
              {
                matching[k] = true;
                ++j;
              }
              else
              {
                // check OR NOT terms
                size_t l = 0;     // iterate over notmaching[k] bitmask
                bool none = true; // all not-terms matched

                while (++j != e)
                {
                  if (*j && !notmatching[k][l])
                  {
                    if ((*j)->buffer(const_cast<char*>(bol), eol - bol + 1).find() != 0)
                      notmatching[k][l] = true;
                    else
                      all = none = false;
                  }

                  ++l;
                }

                if (none)
                {
                  // when all not-terms matched and we don't have a positive alternative then stop searching this file
                  if (!*i.begin())
                    throw EXIT_SEARCH();

                  all = false;
                }
              }
            }
          }
          ++k;
        }

        // if all terms matched globally per file then remove the hold to launch output
        if (all)
        {
          if (acquire)
            out.acquire();

          // --max-files: max reached?
          if (!Stats::found_part())
            throw EXIT_SEARCH();

          out.launch();
        }
      }
    }
    else
    {
      // for each AND term check if the line has a match
      for (const auto& i : *matchers)
      {
        auto j = i.begin();
        auto e = i.end();

        if (j != e)
        {
          // check OR terms
          if (*j && (*j)->buffer(const_cast<char*>(bol), eol - bol + 1).find() != 0)
            continue;

          // check OR NOT terms
          while (++j != e)
            if (*j && (*j)->buffer(const_cast<char*>(bol), eol - bol + 1).find() == 0)
              break;

          if (j == e)
            return false;
        }
      }
    }

    return true;
  }

  // if CNF AND/OR/NOT conditions are met globally then launch output after searching a file with --files
  bool cnf_satisfied(bool acquire = false)
  {
    if (out.holding())
    {
      size_t k = 0; // iterate over matching[] bitmask

      // for each AND term check if the term was matched before
      for (const auto& i : *matchers)
      {
        // an OR term hasn't matched
        if (!matching[k])
        {
          // return if there are no OR NOT terms to check
          if (i.size() <= 1)
            return false;

          auto j = i.begin();
          auto e = i.end();

          // check if not all of the OR NOT terms matched
          if (j != e)
          {
            size_t l = 0; // iterate over notmaching[k] bitmask
            while (++j != e)
            {
              if (*j && !notmatching[k][l])
                break;
              ++l;
            }
            // return if all OR NOT terms matched
            if (j == e)
              return false;
          }
        }
        ++k;
      }

      if (acquire)
        out.acquire();

      // --max-files: max reached?
      if (!Stats::found_part())
        throw EXIT_SEARCH();

      out.launch();
    }

    return true;
  }

  // open a file for (binary) reading and assign input, decompress the file when -z, --decompress specified, may throw bad_alloc
  bool open_file(const char *pathname)
  {
    if (pathname == LABEL_STANDARD_INPUT)
    {
      if (source == NULL)
        return false;

      pathname = flag_label;
      file_in = source;

#ifdef OS_WIN
      _setmode(fileno(source), _O_BINARY);
#endif
    }
    else if (fopenw_s(&file_in, pathname, "rb") != 0)
    {
      warning("cannot read", pathname);

      return false;
    }

#ifndef OS_WIN
    // check if a regular file is special and recursive searching files should not block
    struct stat buf;
    if (file_in != stdin && file_in != source && fstat(fileno(file_in), &buf) == 0 && S_ISREG(buf.st_mode))
    {
      // recursive searches should not block on special regular files like in /proc and /sys
      if (flag_directories_action == Action::RECURSE)
        fcntl(fileno(file_in), F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

      // check if an empty file is readable e.g. not a special sysfd file like in /proc and /sys
      char data[1];
      if (buf.st_size == 0 && (read(fileno(file_in), data, 1) < 0 || fseek(file_in, 0, SEEK_SET) != 0))
      {
        warning("cannot read", pathname);
        fclose(file_in);
        file_in = NULL;

        return false;
      }
    }
#endif

    // --filter: fork process to filter file, when applicable
    if (!filter(file_in, pathname))
      return false;

#ifdef HAVE_LIBZ
    if (flag_decompress)
    {
#ifdef WITH_DECOMPRESSION_THREAD

      // start decompression thread to read the current input file, get pipe with decompressed input
      FILE *pipe_in = zthread.start(flag_zmax, pathname, file_in);
      if (pipe_in == NULL)
      {
        fclose(file_in);
        file_in = NULL;

        return false;
      }

      input = reflex::Input(pipe_in, flag_encoding_type);

#else

      // create or open a new zstreambuf
      if (zstream == NULL)
        zstream = new zstreambuf(pathname, file_in);
      else
        zstream->open(pathname, file_in);

      if (stream != NULL)
        delete stream;

      stream = new std::istream(zstream);

      input = stream;

#endif
    }
    else
#endif
    {
      input = reflex::Input(file_in, flag_encoding_type);
    }

    return true;
  }

  // return true on success, create a pipe to replace file input if filtering files in a forked process
  bool filter(FILE *& in, const char *pathname)
  {
#ifndef OS_WIN

    // --filter
    if (flag_filter != NULL && in != NULL)
    {
      const char *basename = strrchr(pathname, PATHSEPCHR);
      if (basename == NULL)
        basename = pathname;
      else
        ++basename;

      // get the basenames's extension suffix
      const char *suffix = strrchr(basename, '.');

      // don't consider . at the front of basename, otherwise skip .
      if (suffix == basename)
        suffix = NULL;
      else if (suffix != NULL)
        ++suffix;

      // --filter-magic-label: if the file is seekable (to rewind later), then check for a magic pattern match
      if (!flag_filter_magic_label.empty() && fseek(in, 0, SEEK_SET) == 0)
      {
        bool is_plus = false;

        // --filter-magic-label: check for overriding +
        if (suffix != NULL)
        {
          for (const auto& i : flag_filter_magic_label)
          {
            if (i.front() == '+')
            {
              is_plus = true;

              break;
            }
          }
        }

        // --filter-magic-label: if the basename has no suffix or a +LABEL + then check magic bytes
        if (suffix == NULL || is_plus)
        {
          // create a matcher to match the magic pattern
          size_t match = reflex::Matcher(filter_magic_pattern, in).scan();

          // rewind the input after scan
          rewind(in);

          if (match > 0 && match <= flag_filter_magic_label.size())
          {
            suffix = flag_filter_magic_label[match - 1].c_str();

            if (*suffix == '+')
              ++suffix;
          }
        }
      }

      // basenames without a suffix get "*" as a suffix
      if (suffix == NULL || *suffix == '\0')
        suffix = "*";

      size_t sep = strlen(suffix);

      const char *command = flag_filter;
      const char *default_command = NULL;

      // find the command corresponding to the suffix
      while (true)
      {
        while (isspace(*command))
          ++command;

        if (*command == '*')
          default_command = strchr(command, ':');

        if (strncmp(suffix, command, sep) == 0 && (command[sep] == ':' || command[sep] == ',' || isspace(command[sep])))
        {
          command = strchr(command, ':');
          break;
        }

        command = strchr(command, ',');
        if (command == NULL)
          break;

        ++command;
      }

      // if no matching command, use the *:command if specified
      if (command == NULL)
        command = default_command;

      // suffix has a command to execute
      if (command != NULL)
      {
        // skip over the ':'
        ++command;

        int fd[2];

        if (pipe(fd) == 0)
        {
          int pid;

          if ((pid = fork()) == 0)
          {
            // child process

            // close the reading end of the pipe
            close(fd[0]);

            // dup the input file to stdin unless reading stdin
            if (in != stdin)
            {
              dup2(fileno(in), STDIN_FILENO);
              fclose(in);
            }

            // dup the writing end of the pipe to stdout
            dup2(fd[1], STDOUT_FILENO);
            close(fd[1]);

            // populate argv[] with the command and its arguments, thereby destroying flag_filter
            std::vector<const char*> args;

            char *arg = const_cast<char*>(command);

            while (*arg != '\0' && *arg != ',')
            {
              while (isspace(*arg))
                ++arg;

              char *p = arg;

              while (*p != '\0' && *p != ',' && !isspace(*p))
                ++p;

              if (p > arg)
              {
                if (p - arg == 1 && *arg == '%')
                  args.push_back(in == stdin ? "-" : pathname);
                else
                  args.push_back(arg);
              }

              if (*p == '\0')
                break;

              if (*p == ',')
              {
                *p = '\0';
                break;
              }

              *p = '\0';

              arg = p + 1;
            }

            // silently bail out if there is no command
            if (args.empty())
              exit(EXIT_SUCCESS);

            // add sentinel
            args.push_back(NULL);

            // get argv[] array data
            char * const *argv = const_cast<char * const *>(args.data());

            // execute
            execvp(argv[0], argv);

            error("--filter: cannot exec", argv[0]);
          }

          // close the writing end of the pipe
          close(fd[1]);

          // close the file and use the reading end of the pipe
          if (in != stdin)
            fclose(in);
          in = fdopen(fd[0], "r");
        }
        else
        {
          if (in != stdin)
            fclose(in);
          in = NULL;

          warning("--filter: cannot create pipe", flag_filter);

          return false;
        }
      }
    }

#endif

    return true;
  }

  // close the file and clear input, return true if next file is extracted from an archive to search
  bool close_file(const char *pathname)
  {
    // check if the input has no error conditions, but do not check stdin which is nonblocking and handled differently
    if (file_in != NULL && file_in != stdin && file_in != source && ferror(file_in))
    {
      warning("cannot read", pathname);

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
      if (flag_decompress)
        zthread.cancel();
#else
      if (stream != NULL)
      {
        delete stream;
        stream = NULL;
      }
#endif
#endif

      // close the current input file
      if (file_in != NULL && file_in != stdin && file_in != source)
      {
        fclose(file_in);
        file_in = NULL;
      }

      input.clear();

      return false;
    }

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD

    // -z: open next archived file if any or close the compressed file/archive
    if (flag_decompress)
    {
      // close the input FILE* and its underlying pipe previously created with pipe() and fdopen()
      if (input.file() != NULL)
      {
        // close and unassign input, i.e. input.file() == NULL, also closes pipe_fd[0] per fdopen()
        fclose(input.file());
        input.clear();
      }

      // if output is blocked, then cancel decompression threads
      if (out.eof)
        zthread.cancel();

      // open pipe to the next file in an archive if there is a next file to extract
      FILE *pipe_in = zthread.open_next(pathname);
      if (pipe_in != NULL)
      {
        // assign the next extracted file as input to search
        input = reflex::Input(pipe_in, flag_encoding_type);

        // loop back in search() to start searching the next file in the archive
        return true;
      }
    }

#else

    if (stream != NULL)
    {
      delete stream;
      stream = NULL;
    }

#endif
#endif

#ifdef WITH_STDIN_DRAIN
#ifndef OS_WIN
    // drain stdin until eof
    if (file_in == stdin && !feof(stdin))
    {
      if (fseek(stdin, 0, SEEK_END) != 0)
      {
        char buf[16384];
        while (true)
        {
          size_t r = fread(buf, 1, sizeof(buf), stdin);
          if (r == sizeof(buf))
            continue;
          if (feof(stdin))
            break;
          if (errno != EINTR)
            break;
          if (!(fcntl(0, F_GETFL) & O_NONBLOCK))
            break;
          struct timeval tv;
          fd_set rfds, efds;
          FD_ZERO(&rfds);
          FD_ZERO(&efds);
          FD_SET(0, &rfds);
          FD_SET(0, &efds);
          tv.tv_sec = 1;
          tv.tv_usec = 0;
          int r = ::select(1, &rfds, NULL, &efds, &tv);
          if (r < 0 && errno != EINTR)
            break;
          if (r > 0 && FD_ISSET(0, &efds))
            break;
        }
      }
    }
#endif
#endif

    // close the current input file
    if (file_in != NULL && file_in != stdin && file_in != source)
    {
      fclose(file_in);
      file_in = NULL;
    }

    input.clear();

    return false;
  }

  // specify input to read for matcher, when input is a regular file then try mmap for zero copy overhead
  bool init_read()
  {
    const char *base;
    size_t size;

    // attempt to mmap the input file, if mmap is supported and enabled (disabled by default)
    if (mmap.file(input, base, size))
    {
      // matcher reads directly from protected mmap memory (cast is safe: base[0..size] is not modified!)
      matcher->buffer(const_cast<char*>(base), size + 1);
    }
    else
    {
      matcher->input(input);

#if !defined(HAVE_PCRE2) && defined(HAVE_BOOST_REGEX)
      // buffer all input to work around Boost.Regex partial matching bug, but this may throw std::bad_alloc if the file is too large
      if (flag_perl_regexp)
        matcher->buffer();
#endif

#ifndef OS_WIN
      if (input == stdin)
      {
        struct stat buf;
        bool interactive = fstat(0, &buf) == 0 && (S_ISCHR(buf.st_mode) || S_ISFIFO(buf.st_mode));

        // if input is a TTY or pipe, then make stdin nonblocking and register a stdin handler to continue reading and to flush results to output
        if (interactive)
        {
          fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
          matcher->in.set_handler(&stdin_handler);
        }
      }
#endif
    }

    // -I: do not match binary
    if (flag_binary_without_match && init_is_binary())
      return false;

    // --range=NUM1[,NUM2]: start searching at line NUM1
    for (size_t i = flag_min_line; i > 1; --i)
      if (!matcher->skip('\n'))
        break;

    return true;
  }

  // after opening a file with init_read, check if its initial part (64K or what could be read) is binary
  bool init_is_binary()
  {
    return is_binary(matcher->begin(), matcher->avail());
  }

  const char                    *filename;      // the name of the file being searched
  std::string                    partname;      // the name of an extracted file from an archive
  std::string                    restline;      // a buffer to store the rest of a line to search
  Output                         out;           // asynchronous output
  reflex::AbstractMatcher       *matcher;       // the pattern matcher we're using, never NULL
  Matchers                      *matchers;      // the CNF of AND/OR/NOT matchers or NULL
  std::vector<bool>              matching;      // bitmap to keep track of globally matching CNF terms
  std::vector<std::vector<bool>> notmatching;   // bitmap to keep track of globally matching OR NOT CNF terms
  MMap                           mmap;          // mmap state
  reflex::Input                  input;         // input to the matcher
  FILE                          *file_in;       // the current input file
#ifndef OS_WIN
  StdInHandler                   stdin_handler; // a handler to handle nonblocking stdin from a TTY or a slow pipe
#endif
#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
  Zthread                        zthread;
#else
  zstreambuf                    *zstream;       // the decompressed stream buffer from compressed input
  std::istream                  *stream;        // input stream is the decompressed zstream
#endif
#endif

};

struct GrepWorker;

// master submits jobs to workers and implements operations to support lock-free job stealing
struct GrepMaster : public Grep {

  GrepMaster(FILE *file, reflex::AbstractMatcher *matcher, Matchers *matchers)
    :
      Grep(file, matcher, matchers),
      sync(flag_sort_key == Sort::NA ? Output::Sync::Mode::UNORDERED : Output::Sync::Mode::ORDERED)
  {
    // master and workers synchronize their output
    out.sync_on(&sync);

    // set global handle to be able to call cancel_ugrep()
    set_grep_handle(this);

    start_workers();

    iworker = workers.begin();
  }

  virtual ~GrepMaster()
  {
    stop_workers();
    clear_grep_handle();
  }

  // clone the pattern matcher - the caller is responsible to deallocate the returned matcher
  reflex::AbstractMatcher *matcher_clone() const
  {
    return matcher->clone();
  }

  // clone the CNF of AND/OR/NOT matchers - the caller is responsible to deallocate the returned list of matchers if not NULL
  Matchers *matchers_clone() const
  {
    if (matchers == NULL)
      return NULL;

    auto *new_matchers = new Matchers;

    for (const auto& i : *matchers)
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

  // search a file by submitting it as a job to a worker
  void search(const char *pathname, uint16_t cost) override
  {
    submit(pathname, cost);
  }

  // start worker threads
  void start_workers();

  // stop all workers
  void stop_workers();

  // submit a job with a pathname to a worker, workers are visited round-robin
  void submit(const char *pathname, uint16_t cost);

  // lock-free job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
  bool steal(GrepWorker *worker);

  std::list<GrepWorker>           workers; // workers running threads
  std::list<GrepWorker>::iterator iworker; // the next worker to submit a job to
  Output::Sync                    sync;    // sync output of workers

};

// worker runs a thread to execute jobs submitted by the master
struct GrepWorker : public Grep {

  GrepWorker(FILE *file, GrepMaster *master)
    :
      Grep(file, master->matcher_clone(), master->matchers_clone()),
      master(master),
      todo(0)
  {
    // all workers synchronize their output on the master's sync object
    out.sync_on(&master->sync);

    // run worker thread executing jobs assigned to its queue
    thread = std::thread(&GrepWorker::execute, this);
  }

  virtual ~GrepWorker()
  {
    // delete the cloned matcher
    delete matcher;

    // delete the cloned matchers, if any
    if (matchers != NULL)
      delete matchers;
  }

  // worker thread execution
  void execute();

  // submit Job::NONE sentinel to this worker
  void submit_job()
  {
    while (todo >= MAX_JOB_QUEUE_SIZE && !out.eof && !out.cancelled())
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give the worker threads some slack

    std::unique_lock<std::mutex> lock(queue_mutex);

    jobs.emplace_back();
    ++todo;

    queue_work.notify_one();
  }

  // submit a job to this worker
  void submit_job(const char *pathname, uint16_t cost, size_t slot)
  {
    while (todo >= MAX_JOB_QUEUE_SIZE && !out.eof && !out.cancelled())
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give the worker threads some slack

    std::unique_lock<std::mutex> lock(queue_mutex);

    jobs.emplace_back(pathname, cost, slot);
    ++todo;

    queue_work.notify_one();
  }

  // move a stolen job to this worker, maintaining job slot order
  void move_job(Job& job)
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    bool inserted = false;

    // insert job in the queue to maintain job order
    for (auto j = jobs.begin(); j != jobs.end(); ++j)
    {
      if (j->slot > job.slot)
      {
        jobs.insert(j, std::move(job));
        inserted = true;
        break;
      }
    }

    if (!inserted)
      jobs.emplace_back(std::move(job));

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

    jobs.pop_front();
    --todo;

    // if we popped a Job::NONE sentinel but the queue has some jobs, then move the sentinel to the back of the queue
    if (job.none() && !jobs.empty())
    {
      jobs.emplace_back();
      job = jobs.front();
      jobs.pop_front();
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

    // we cannot steal a Job::NONE sentinel
    if (job.none())
      return false;

    jobs.pop_front();
    --todo;

    return true;
  }

  // submit Job::NONE sentinel to stop this worker
  void stop()
  {
    submit_job();
  }

  std::thread             thread;      // thread of this worker, spawns GrepWorker::execute()
  GrepMaster             *master;      // the master of this worker
  std::mutex              queue_mutex; // job queue mutex
  std::condition_variable queue_work;  // cv to control the job queue
  std::deque<Job>         jobs;        // queue of pending jobs submitted to this worker
  std::atomic_size_t      todo;        // number of jobs in the queue, atomic for lock-free job stealing

};

// start worker threads
void GrepMaster::start_workers()
{
  size_t num;

  // create worker threads
  try
  {
    for (num = 0; num < threads; ++num)
      workers.emplace(workers.end(), out.file, this);
  }

  // if sufficient resources are not available then reduce the number of threads to the number of active workers created
  catch (std::system_error& error)
  {
    if (error.code() != std::errc::resource_unavailable_try_again)
      throw;

    threads = num;
  }
}

// stop all workers
void GrepMaster::stop_workers()
{
  // submit Job::NONE sentinel to workers
  for (auto& worker : workers)
    worker.stop();

  // wait for workers to join
  for (auto& worker : workers)
    worker.thread.join();
}

// submit a job with a pathname to a worker, workers are visited round-robin
void GrepMaster::submit(const char *pathname, uint16_t cost)
{
  iworker->submit_job(pathname, cost, sync.next++);

  // around we go
  ++iworker;
  if (iworker == workers.end())
    iworker = workers.begin();
}

// lock-free job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
bool GrepMaster::steal(GrepWorker *worker)
{
  // pick a random co-worker using thread-safe std::chrono::high_resolution_clock as a simple RNG
  size_t n = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() % threads;
  auto iworker = workers.begin();

  while (n > 0)
  {
    ++iworker;
    --n;
  }

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
        worker->move_job(job);

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

  while (!out.eof && !out.cancelled())
  {
    // wait for next job
    next_job(job);

    // worker should stop?
    if (job.none())
      break;

    // start synchronizing output for this job slot in ORDERED mode (--sort)
    out.begin(job.slot);

    // search the file for this job
    search(job.pathname.c_str(), job.cost);

    // end output in ORDERED mode (--sort) for this job slot
    out.end();

    // if only one job is left to do or nothing to do, then try stealing another job from a co-worker
    if (todo <= 1)
      master->steal(this);
  }
}

// table of RE/flex file encodings for option --encoding (may be specified in any case)
const Encoding encoding_table[] = {
  { "binary",      reflex::Input::file_encoding::plain      },
  { "ASCII",       reflex::Input::file_encoding::utf8       },
  { "UTF-8",       reflex::Input::file_encoding::utf8       },
  { "UTF-16",      reflex::Input::file_encoding::utf16be    },
  { "UTF-16BE",    reflex::Input::file_encoding::utf16be    },
  { "UTF-16LE",    reflex::Input::file_encoding::utf16le    },
  { "UTF-32",      reflex::Input::file_encoding::utf32be    },
  { "UTF-32BE",    reflex::Input::file_encoding::utf32be    },
  { "UTF-32LE",    reflex::Input::file_encoding::utf32le    },
  { "LATIN1",      reflex::Input::file_encoding::latin      },
  { "ISO-8859-1",  reflex::Input::file_encoding::latin      },
  { "ISO-8859-2",  reflex::Input::file_encoding::iso8859_2  },
  { "ISO-8859-3",  reflex::Input::file_encoding::iso8859_3  },
  { "ISO-8859-4",  reflex::Input::file_encoding::iso8859_4  },
  { "ISO-8859-5",  reflex::Input::file_encoding::iso8859_5  },
  { "ISO-8859-6",  reflex::Input::file_encoding::iso8859_6  },
  { "ISO-8859-7",  reflex::Input::file_encoding::iso8859_7  },
  { "ISO-8859-8",  reflex::Input::file_encoding::iso8859_8  },
  { "ISO-8859-9",  reflex::Input::file_encoding::iso8859_9  },
  { "ISO-8859-10", reflex::Input::file_encoding::iso8859_10 },
  { "ISO-8859-11", reflex::Input::file_encoding::iso8859_11 },
  { "ISO-8859-13", reflex::Input::file_encoding::iso8859_13 },
  { "ISO-8859-14", reflex::Input::file_encoding::iso8859_14 },
  { "ISO-8859-15", reflex::Input::file_encoding::iso8859_15 },
  { "ISO-8859-16", reflex::Input::file_encoding::iso8859_16 },
  { "MAC",         reflex::Input::file_encoding::macroman   },
  { "MACROMAN",    reflex::Input::file_encoding::macroman   },
  { "EBCDIC",      reflex::Input::file_encoding::ebcdic     },
  { "CP437",       reflex::Input::file_encoding::cp437      },
  { "CP850",       reflex::Input::file_encoding::cp850      },
  { "CP858",       reflex::Input::file_encoding::cp858      },
  { "CP1250",      reflex::Input::file_encoding::cp1250     },
  { "CP1251",      reflex::Input::file_encoding::cp1251     },
  { "CP1252",      reflex::Input::file_encoding::cp1252     },
  { "CP1253",      reflex::Input::file_encoding::cp1253     },
  { "CP1254",      reflex::Input::file_encoding::cp1254     },
  { "CP1255",      reflex::Input::file_encoding::cp1255     },
  { "CP1256",      reflex::Input::file_encoding::cp1256     },
  { "CP1257",      reflex::Input::file_encoding::cp1257     },
  { "CP1258",      reflex::Input::file_encoding::cp1258     },
  { "KOI8-R",      reflex::Input::file_encoding::koi8_r     },
  { "KOI8-U",      reflex::Input::file_encoding::koi8_u     },
  { "KOI8-RU",     reflex::Input::file_encoding::koi8_ru    },
  { NULL, 0 }
};

// table of file types for option -t, --file-type
const Type type_table[] = {
  { "actionscript", "as,mxml", NULL,                                                  NULL },
  { "ada",          "ada,adb,ads", NULL,                                              NULL },
  { "asm",          "asm,s,S", NULL,                                                  NULL },
  { "asp",          "asp", NULL,                                                      NULL },
  { "aspx",         "master,ascx,asmx,aspx,svc", NULL,                                NULL },
  { "autoconf",     "ac,in", NULL,                                                    NULL },
  { "automake",     "am,in", NULL,                                                    NULL },
  { "awk",          "awk", NULL,                                                      NULL },
  { "Awk",          "awk", NULL,                                                      "#!\\h*/.*\\Wg?awk(\\W.*)?\\n" },
  { "basic",        "bas,BAS,cls,frm,ctl,vb,resx", NULL,                              NULL },
  { "batch",        "bat,BAT,cmd,CMD", NULL,                                          NULL },
  { "bison",        "y,yy,ymm,ypp,yxx", NULL,                                         NULL },
  { "c",            "c,h,H,hdl,xs", NULL,                                             NULL },
  { "c++",          "cpp,CPP,cc,cxx,CXX,h,hh,H,hpp,hxx,Hxx,HXX", NULL,                NULL },
  { "clojure",      "clj", NULL,                                                      NULL },
  { "cpp",          "cpp,CPP,cc,cxx,CXX,h,hh,H,hpp,hxx,Hxx,HXX", NULL,                NULL },
  { "csharp",       "cs", NULL,                                                       NULL },
  { "css",          "css", NULL,                                                      NULL },
  { "csv",          "csv", NULL,                                                      NULL },
  { "dart",         "dart", NULL,                                                     NULL },
  { "Dart",         "dart", NULL,                                                     "#!\\h*/.*\\Wdart(\\W.*)?\\n" },
  { "delphi",       "pas,int,dfm,nfm,dof,dpk,dproj,groupproj,bdsgroup,bdsproj", NULL, NULL },
  { "elisp",        "el", NULL,                                                       NULL },
  { "elixir",       "ex,exs", NULL,                                                   NULL },
  { "erlang",       "erl,hrl", NULL,                                                  NULL },
  { "fortran",      "for,ftn,fpp,f,F,f77,F77,f90,F90,f95,F95,f03,F03", NULL,          NULL },
  { "gif",          "gif", NULL,                                                      NULL },
  { "Gif",          "gif", NULL,                                                      "GIF87a|GIF89a" },
  { "go",           "go", NULL,                                                       NULL },
  { "groovy",       "groovy,gtmpl,gpp,grunit,gradle", NULL,                           NULL },
  { "gsp",          "gsp", NULL,                                                      NULL },
  { "haskell",      "hs,lhs", NULL,                                                   NULL },
  { "html",         "htm,html,xhtml", NULL,                                           NULL },
  { "jade",         "jade", NULL,                                                     NULL },
  { "java",         "java,properties", NULL,                                          NULL },
  { "jpeg",         "jpg,jpeg", NULL,                                                 NULL },
  { "Jpeg",         "jpg,jpeg", NULL,                                                 "\\xff\\xd8\\xff[\\xdb\\xe0\\xe1\\xee]" },
  { "js",           "js", NULL,                                                       NULL },
  { "json",         "json", NULL,                                                     NULL },
  { "jsp",          "jsp,jspx,jthm,jhtml", NULL,                                      NULL },
  { "julia",        "jl", NULL,                                                       NULL },
  { "kotlin",       "kt,kts", NULL,                                                   NULL },
  { "less",         "less", NULL,                                                     NULL },
  { "lex",          "l,ll,lmm,lpp,lxx", NULL,                                         NULL },
  { "lisp",         "lisp,lsp", NULL,                                                 NULL },
  { "lua",          "lua", NULL,                                                      NULL },
  { "m4",           "m4", NULL,                                                       NULL },
  { "make",         "mk,mak", "makefile,Makefile,Makefile.Debug,Makefile.Release",    NULL },
  { "markdown",     "md", NULL,                                                       NULL },
  { "matlab",       "m", NULL,                                                        NULL },
  { "node",         "js", NULL,                                                       NULL },
  { "Node",         "js", NULL,                                                       "#!\\h*/.*\\Wnode(\\W.*)?\\n" },
  { "objc",         "m,h", NULL,                                                      NULL },
  { "objc++",       "mm,h", NULL,                                                     NULL },
  { "ocaml",        "ml,mli,mll,mly", NULL,                                           NULL },
  { "parrot",       "pir,pasm,pmc,ops,pod,pg,tg", NULL,                               NULL },
  { "pascal",       "pas,pp", NULL,                                                   NULL },
  { "pdf",          "pdf", NULL,                                                      NULL },
  { "Pdf",          "pdf", NULL,                                                      "\\x25\\x50\\x44\\x46\\x2d" },
  { "perl",         "pl,PL,pm,pod,t,psgi", NULL,                                      NULL },
  { "Perl",         "pl,PL,pm,pod,t,psgi", NULL,                                      "#!\\h*/.*\\Wperl(\\W.*)?\\n" },
  { "php",          "php,php3,php4,phtml", NULL,                                      NULL },
  { "Php",          "php,php3,php4,phtml", NULL,                                      "#!\\h*/.*\\Wphp(\\W.*)?\\n" },
  { "png",          "png", NULL,                                                      NULL },
  { "Png",          "png", NULL,                                                      "\\x89PNG\\x0d\\x0a\\x1a\\x0a" },
  { "prolog",       "pl,pro", NULL,                                                   NULL },
  { "python",       "py", NULL,                                                       NULL },
  { "Python",       "py", NULL,                                                       "#!\\h*/.*\\Wpython[23]?(\\W.*)?\\n" },
  { "r",            "R", NULL,                                                        NULL },
  { "rpm",          "rpm", NULL,                                                      NULL },
  { "Rpm",          "rpm", NULL,                                                      "\\xed\\xab\\xee\\xdb" },
  { "rst",          "rst", NULL,                                                      NULL },
  { "rtf",          "rtf", NULL,                                                      NULL },
  { "Rtf",          "rtf", NULL,                                                      "\\{\\rtf1" },
  { "ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec", "Rakefile",                    NULL },
  { "Ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec", "Rakefile",                    "#!\\h*/.*\\Wruby(\\W.*)?\\n" },
  { "rust",         "rs", NULL,                                                       NULL },
  { "scala",        "scala", NULL,                                                    NULL },
  { "scheme",       "scm,ss", NULL,                                                   NULL },
  { "shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish", NULL,                       NULL },
  { "Shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish", NULL,                       "#!\\h*/.*\\W(ba|da|t?c|k|z|fi)?sh(\\W.*)?\\n" },
  { "smalltalk",    "st", NULL,                                                       NULL },
  { "sql",          "sql,ctl", NULL,                                                  NULL },
  { "svg",          "svg", NULL,                                                      NULL },
  { "swift",        "swift", NULL,                                                    NULL },
  { "tcl",          "tcl,itcl,itk", NULL,                                             NULL },
  { "tex",          "tex,cls,sty,bib", NULL,                                          NULL },
  { "text",         "text,txt,TXT,md,rst", NULL,                                      NULL },
  { "tiff",         "tif,tiff", NULL,                                                 NULL },
  { "Tiff",         "tif,tiff", NULL,                                                 "\\x49\\x49\\x2a\\x00|\\x4d\\x4d\\x00\\x2a" },
  { "tt",           "tt,tt2,ttml", NULL,                                              NULL },
  { "typescript",   "ts,tsx", NULL,                                                   NULL },
  { "verilog",      "v,vh,sv", NULL,                                                  NULL },
  { "vhdl",         "vhd,vhdl", NULL,                                                 NULL },
  { "vim",          "vim", NULL,                                                      NULL },
  { "xml",          "xml,xsd,xsl,xslt,wsdl,rss,svg,ent,plist", NULL,                  NULL },
  { "Xml",          "xml,xsd,xsl,xslt,wsdl,rss,svg,ent,plist", NULL,                  "<\\?xml " },
  { "yacc",         "y", NULL,                                                        NULL },
  { "yaml",         "yaml,yml", NULL,                                                 NULL },
  { NULL,           NULL, NULL,                                                       NULL }
};

#ifdef OS_WIN
// ugrep main() for Windows to support wide string arguments and globbing
int wmain(int argc, const wchar_t **wargv)
#else
// ugrep main()
int main(int argc, const char **argv)
#endif
{

#ifdef OS_WIN

  // store UTF-8 arguments for the duration of main() and convert Unicode command line arguments wargv[] to UTF-8 arguments argv[]
  const char **argv = new const char *[argc];
  for (int i = 0; i < argc; ++i)
  {
    arg_strings.emplace_back(utf8_encode(wargv[i]));
    argv[i] = arg_strings.back().c_str();
  }

  // handle CTRL-C
  SetConsoleCtrlHandler(&sigint, TRUE);

#else

  // ignore SIGPIPE
  signal(SIGPIPE, SIG_IGN);

  // reset color on SIGINT and SIGTERM
  signal(SIGINT, sigint);
  signal(SIGTERM, sigint);

#endif

  try
  {
    init(argc, argv);
  }

  catch (std::exception& error)
  {
    abort("error: ", error.what());
  }

  if (flag_query > 0)
  {
    if (!flag_no_messages && warnings > 0)
      abort("option -Q: warnings are present, use -s to ignore");

    Query::query();
  }
  else
  {
    if (!flag_no_messages && flag_pager != NULL && warnings > 0)
      abort("option --pager: warnings are present, use -s to ignore");

    try
    {
      ugrep();
    }

    catch (reflex::regex_error& error)
    {
      abort("error: ", error.what());
    }

    catch (std::exception& error)
    {
      abort("error: ", error.what());
    }
  }

#ifdef OS_WIN

  delete[] argv;

#endif

  return warnings == 0 && Stats::found_any_file() ? EXIT_OK : EXIT_FAIL;
}

static void set_depth(const char *arg)
{
  if (flag_max_depth > 0)
  {
    if (flag_min_depth == 0)
      flag_min_depth = flag_max_depth;
    flag_max_depth = strtopos(arg, "invalid argument --");
    if (flag_min_depth > flag_max_depth)
      usage("invalid argument -", arg);
  }
  else
  {
    strtopos2(arg, flag_min_depth, flag_max_depth, "invalid argument --");
  }
}

// load config file specified or the default .ugrep, located in the working directory or home directory
static void load_config(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args)
{
  // warn about invalid options but do not exit
  flag_usage_warnings = true;

  // the default config file is .ugrep when FILE is not specified
  if (flag_config == NULL || *flag_config == '\0')
    flag_config_file.assign(".ugrep");
  else
    flag_config_file.assign(flag_config);

  FILE *file = NULL;

  if (fopen_smart(&file, flag_config_file.c_str(), "r") != 0)
  {
    if (home_dir != NULL)
    {
      // check the home directory for the configuration file
      if (flag_config == NULL || *flag_config == '\0')
        flag_config_file.assign(home_dir).append(PATHSEPSTR).append(".ugrep");
      else
        flag_config_file.assign(home_dir).append(PATHSEPSTR).append(flag_config);
      if (fopen_smart(&file, flag_config_file.c_str(), "r") != 0)
        file = NULL;
    }
  }

  if (file != NULL)
  {
    reflex::BufferedInput input(file);

    std::string line;
    size_t lineno = 1;
    bool errors = false;

    while (true)
    {
      // read the next line
      if (getline(input, line))
        break;

      trim(line);

      // skip empty lines and comments
      if (!line.empty() && line.front() != '#')
      {
        // construct an option argument to parse as argv[]
        line.insert(0, "--");
        const char *arg = flag_config_options.insert(line).first->c_str();
        const char *args[2] = { NULL, arg };

        warnings = 0;

        options(pattern_args, 2, args);

        if (warnings > 0)
        {
          std::cerr << "ugrep: error in " << flag_config_file << " at line " << lineno << "\n\n";

          errors = true;
        }
      }

      ++lineno;
    }

    if (ferror(file))
      error("error while reading", flag_config_file.c_str());

    if (file != stdin)
      fclose(file);

    if (errors)
    {
      std::cerr << "Try `ugrep --help' or `ugrep --help WHAT' for more information\n";

      exit(EXIT_ERROR);
    }
  }
  else if (flag_config != NULL && *flag_config != '\0')
  {
    error("option --config: cannot read", flag_config_file.c_str());
  }

  flag_usage_warnings = false;
}

// save a configuration file
static void save_config()
{
  FILE *file = NULL;

  if (fopen_smart(&file, flag_save_config, "w") != 0)
  {
    usage("cannot save configuration file ", flag_save_config);

    return;
  }

  if (strcmp(flag_save_config, ".ugrep") == 0)
    fprintf(file, "# default .ugrep configuration file used by ug and ugrep --config.\n");
  else if (strcmp(flag_save_config, "-") == 0)
    fprintf(file, "# ugrep configuration.\n");
  else
    fprintf(file, "# configuration used with ugrep --config=%s or ---%s.\n", flag_save_config, flag_save_config);

  fprintf(file, "\
#\n\
# A long option is defined per line with an optional `=' and its argument,\n\
# when applicable. Empty lines and lines starting with a `#' are ignored.\n\
#\n\
# Try `ugrep --help' or `ugrep --help WHAT' for more information.\n\n");

  fprintf(file, "### TERMINAL DISPLAY ###\n\n");

  fprintf(file, "# Custom color scheme overrides default GREP_COLORS parameters\ncolors=%s\n", flag_colors != NULL ? flag_colors : "");
  fprintf(file, "\
# The argument is a colon-separated list of one or more parameters `sl='\n\
# (selected line), `cx=' (context line), `mt=' (matched text), `ms=' (match\n\
# selected), `mc=' (match context), `fn=' (file name), `ln=' (line number),\n\
# `cn=' (column number), `bn=' (byte offset), `se=' (separator).  Parameter\n\
# values are ANSI SGR color codes or `k' (black), `r' (red), `g' (green), `y'\n\
# (yellow), `b' (blue), `m' (magenta), `c' (cyan), `w' (white).  Upper case\n\
# specifies background colors.  A `+' qualifies a color as bright.  A\n\
# foreground and a background color may be combined with font properties `n'\n\
# (normal), `f' (faint), `h' (highlight), `i' (invert), `u' (underline).\n\n");
  fprintf(file, "# Enable/disable color\n%s\n\n", flag_color != NULL ? "color" : "no-color");
  fprintf(file, "# Enable/disable query UI confirmation prompts, default: confirm\n%s\n\n", flag_confirm ? "confirm" : "no-confirm");
  fprintf(file, "# Enable/disable query UI file viewing command with CTRL-Y, default: view\n");
  if (flag_view != NULL && *flag_view == '\0')
    fprintf(file, "view\n\n");
  else if (flag_view != NULL)
    fprintf(file, "view=%s\n\n", flag_view);
  else
    fprintf(file, "no-view\n\n");
  fprintf(file, "# Enable/disable or specify a pager for terminal output, default: no-pager\n");
  if (flag_pager != NULL)
    fprintf(file, "pager=%s\n\n", flag_pager);
  else
    fprintf(file, "no-pager\n\n");
  fprintf(file, "# Enable/disable pretty output to the terminal, default: no-pretty\n%s\n\n", flag_pretty ? "pretty" : "no-pretty");
  fprintf(file, "# Enable/disable headings for terminal output, default: no-heading\n%s\n\n", flag_heading.is_undefined() ? "# no-heading" : flag_heading ? "heading" : "no-heading");

  if (flag_break.is_defined())
    fprintf(file, "# Enable/disable break for terminal output\n%s\n\n", flag_break ? "break" : "no-break");

  if (flag_line_number.is_defined() && flag_line_number != flag_pretty)
    fprintf(file, "# Enable/disable line numbers\n%s\n\n", flag_line_number ? "line-number" : "no-line-number");

  if (flag_column_number.is_defined())
    fprintf(file, "# Enable/disable column numbers\n%s\n\n", flag_column_number ? "column-number" : "no-column-number");

  if (flag_byte_offset.is_defined())
    fprintf(file, "# Enable/disable byte offsets\n%s\n\n", flag_byte_offset ? "byte-offset" : "no-byte-offset");

  if (flag_initial_tab.is_defined() && flag_line_number != flag_pretty)
    fprintf(file, "# Enable/disable initial tab\n%s\n\n", flag_initial_tab ? "initial-tab" : "no-initial-tab");

  if (strcmp(flag_binary_files, "hex") == 0)
    fprintf(file, "# Hex output\nhex\n\n");
  else if (strcmp(flag_binary_files, "with-hex") == 0)
    fprintf(file, "# Output with hex for binary matches\nwith-hex\n\n");
  if (flag_hexdump != NULL)
    fprintf(file, "# Hex dump (columns, no space breaks, no character column, no hex spacing)\nhexdump=%s\n\n", flag_hexdump);

  if (flag_any_line)
  {
    fprintf(file, "# Display any line as context\nany-line\n\n");
  }
  else if (flag_after_context > 0 && flag_before_context == flag_after_context)
  {
    fprintf(file, "# Display context lines\ncontext=%zu\n\n", flag_after_context);
  }
  else
  {
    if (flag_after_context > 0)
      fprintf(file, "# Display lines after context\nafter-context=%zu\n\n", flag_after_context);
    if (flag_before_context > 0)
      fprintf(file, "# Display lines before context\nbefore-context=%zu\n\n", flag_before_context);
  }
  if (flag_group_separator == NULL)
    fprintf(file, "# Disable group separator for contexts\nno-group-separator\n\n");
  else if (strcmp(flag_group_separator, "--") != 0)
    fprintf(file, "# Group separator for contexts\ngroup-separator=%s\n\n", flag_group_separator);

  fprintf(file, "### SEARCH PATTERNS ###\n\n");

  fprintf(file, "# Enable/disable case-insensitive search, default: no-ignore-case\n%s\n\n", flag_ignore_case.is_undefined() ? "# no-ignore-case" : flag_ignore_case ? "ignore-case" : "no-ignore-case");
  fprintf(file, "# Enable/disable smart case, default: no-smart-case\n%s\n\n", flag_smart_case.is_undefined() ? "# no-smart-case" : flag_smart_case ? "smart-case" : "no-smart-case");
  fprintf(file, "# Enable/disable empty pattern matches, default: no-empty\n%s\n\n", flag_empty.is_undefined() ? "# no-empty" : flag_empty ? "empty" : "no-empty");

  fprintf(file, "### SEARCH TARGETS ###\n\n");

  fprintf(file, "# Enable/disable searching hidden files and directories, default: no-hidden\n%s\n\n", flag_hidden ? "hidden" : "no-hidden");
  fprintf(file, "# Enable/disable binary files, default: no-ignore-binary\n%s\n\n", strcmp(flag_binary_files, "without-match") == 0 ? "ignore-binary" : "no-ignore-binary");
  fprintf(file, "# Enable/disable decompression and archive search, default: no-decompress\n%s\n\n", flag_decompress ? "decompress" : "no-decompress");
  fprintf(file, "# Maximum decompression and de-archiving nesting levels, default: 1\nzmax=%zu\n\n", flag_zmax);
  if (flag_ignore_files.empty())
  {
    fprintf(file, "# Enable/disable ignore files, default: no-ignore-files\nno-ignore-files\n\n");
  }
  else
  {
    fprintf(file, "# Enable/disable ignore files, default: no-ignore-files\n");
    for (const auto& ignore : flag_ignore_files)
      fprintf(file, "ignore-files=%s\n", ignore.c_str());
    fprintf(file, "\n");
  }
  if (flag_filter != NULL)
  {
    fprintf(file, "# Filtering\nfilter=%s\n\n", flag_filter);
    if (!flag_filter_magic_label.empty())
    {
      fprintf(file, "# Filter by file signature magic bytes\n");
      for (const auto& label : flag_filter_magic_label)
        fprintf(file, "filter-magic-label=%s\n", label.c_str());
      fprintf(file, "# Warning: filter-magic-label significantly reduces performance!\n\n");
    }
  }

  fprintf(file, "### OUTPUT ###\n\n");

  fprintf(file, "# Enable/disable sorted output, default: no-sort\n");
  if (flag_sort != NULL)
    fprintf(file, "sort=%s\n\n", flag_sort);
  else
    fprintf(file, "# no-sort\n\n");

  if (ferror(file))
    error("cannot save", flag_save_config);

  if (file != stdout)
    fclose(file);
}

// parse the command-line options
void options(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int argc, const char **argv)
{
  bool options = true;

  for (int i = 1; i < argc; ++i)
  {
    const char *arg = argv[i];

    if ((*arg == '-'
#ifdef OS_WIN
         || *arg == '/'
#endif
        ) && arg[1] != '\0' && options)
    {
      bool is_grouped = true;

      // parse a ugrep command-line option
      while (is_grouped && *++arg != '\0')
      {
        switch (*arg)
        {
          case '-':
            is_grouped = false;
            if (*++arg == '\0')
            {
              options = false;
              continue;
            }

            switch (*arg)
            {
              case '-':
                break;

              case 'a':
                if (strncmp(arg, "after-context=", 14) == 0)
                  flag_after_context = strtonum(arg + 14, "invalid argument --after-context=");
                else if (strcmp(arg, "and") == 0)
                  option_and(pattern_args, i, argc, argv);
                else if (strncmp(arg, "and=", 4) == 0)
                  option_and(pattern_args, arg + 4);
                else if (strcmp(arg, "andnot") == 0)
                  option_andnot(pattern_args, i, argc, argv);
                else if (strncmp(arg, "andnot=", 7) == 0)
                  option_andnot(pattern_args, arg + 7);
                else if (strcmp(arg, "any-line") == 0)
                  flag_any_line = true;
                else if (strcmp(arg, "after-context") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--after-context, --and, --andnot or --any-line");
                break;

              case 'b':
                if (strcmp(arg, "basic-regexp") == 0)
                  flag_basic_regexp = true;
                else if (strncmp(arg, "before-context=", 15) == 0)
                  flag_before_context = strtonum(arg + 15, "invalid argument --before-context=");
                else if (strcmp(arg, "best-match") == 0)
                  flag_best_match = true;
                else if (strcmp(arg, "binary") == 0)
                  flag_binary = true;
                else if (strncmp(arg, "binary-files=", 13) == 0)
                  flag_binary_files = arg + 13;
                else if (strcmp(arg, "bool") == 0)
                  flag_bool = true;
                else if (strcmp(arg, "break") == 0)
                  flag_break = true;
                else if (strcmp(arg, "byte-offset") == 0)
                  flag_byte_offset = true;
                else if (strcmp(arg, "before-context") == 0 || strcmp(arg, "binary-files") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--basic-regexp, --before-context, --binary, --binary-files, --bool, --break or --byte-offset");
                break;

              case 'c':
                if (strcmp(arg, "color") == 0 || strcmp(arg, "colour") == 0)
                  flag_color = "auto";
                else if (strncmp(arg, "color=", 6) == 0)
                  flag_color = arg + 6;
                else if (strncmp(arg, "colour=", 7) == 0)
                  flag_color = arg + 7;
                else if (strncmp(arg, "colors=", 7) == 0)
                  flag_colors = arg + 7;
                else if (strncmp(arg, "colours=", 8) == 0)
                  flag_colors = arg + 8;
                else if (strcmp(arg, "column-number") == 0)
                  flag_column_number = true;
                else if (strcmp(arg, "config") == 0 || strncmp(arg, "config=", 7) == 0)
                  ; // --config is pre-parsed before other options are parsed
                else if (strcmp(arg, "confirm") == 0)
                  flag_confirm = true;
                else if (strncmp(arg, "context=", 8) == 0)
                  flag_after_context = flag_before_context = strtonum(arg + 8, "invalid argument --context=");
                else if (strcmp(arg, "count") == 0)
                  flag_count = true;
                else if (strcmp(arg, "cpp") == 0)
                  flag_cpp = true;
                else if (strcmp(arg, "csv") == 0)
                  flag_csv = true;
                else if (strcmp(arg, "colors") == 0 ||
                    strcmp(arg, "colours") == 0 ||
                    strcmp(arg, "context") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--color, --colors, --column-number, --config, --confirm, --context, --count, --cpp or --csv");
                break;

              case 'd':
                if (strcmp(arg, "decompress") == 0)
                  flag_decompress = true;
                else if (strncmp(arg, "depth=", 6) == 0)
                  strtopos2(arg + 6, flag_min_depth, flag_max_depth, "invalid argument --depth=");
                else if (strcmp(arg, "dereference") == 0)
                  flag_dereference = true;
                else if (strcmp(arg, "dereference-recursive") == 0)
                  flag_directories = "dereference-recurse";
                else if (strncmp(arg, "devices=", 8) == 0)
                  flag_devices = arg + 8;
                else if (strncmp(arg, "directories=", 12) == 0)
                  flag_directories = arg + 12;
                else if (strcmp(arg, "dotall") == 0)
                  flag_dotall = true;
                else if (strcmp(arg, "depth") == 0 ||
                    strcmp(arg, "devices") == 0 ||
                    strcmp(arg, "directories") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--decompress, --depth, --dereference, --dereference-recursive, --devices, --directories or --dotall");
                break;

              case 'e':
                if (strcmp(arg, "empty") == 0)
                  flag_empty = true;
                else if (strncmp(arg, "encoding=", 9) == 0)
                  flag_encoding = arg + 9;
                else if (strncmp(arg, "exclude=", 8) == 0)
                  flag_exclude.emplace_back(arg + 8);
                else if (strncmp(arg, "exclude-dir=", 12) == 0)
                  flag_exclude_dir.emplace_back(arg + 12);
                else if (strncmp(arg, "exclude-from=", 13) == 0)
                  flag_exclude_from.emplace_back(arg + 13);
                else if (strncmp(arg, "exclude-fs=", 11) == 0)
                  flag_exclude_fs.emplace_back(arg + 11);
                else if (strcmp(arg, "extended-regexp") == 0)
                  flag_basic_regexp = false;
                else if (strcmp(arg, "encoding") == 0 ||
                    strcmp(arg, "exclude") == 0 ||
                    strcmp(arg, "exclude-dir") == 0 ||
                    strcmp(arg, "exclude-from") == 0 ||
                    strcmp(arg, "exclude-fs") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--empty, --encoding, --exclude, --exclude-dir, --exclude-from, --exclude-fs or --extended-regexp");
                break;

              case 'f':
                if (strncmp(arg, "file=", 5) == 0)
                  flag_file.emplace_back(arg + 5);
                else if (strncmp(arg, "file-extension=", 15) == 0)
                  flag_file_extension.emplace_back(arg + 15);
                else if (strncmp(arg, "file-magic=", 11) == 0)
                  flag_file_magic.emplace_back(arg + 11);
                else if (strncmp(arg, "file-type=", 10) == 0)
                  flag_file_type.emplace_back(arg + 10);
                else if (strcmp(arg, "files") == 0)
                  flag_files = true;
                else if (strcmp(arg, "files-with-matches") == 0)
                  flag_files_with_matches = true;
                else if (strcmp(arg, "files-without-match") == 0)
                  flag_files_without_match = true;
                else if (strcmp(arg, "fixed-strings") == 0)
                  flag_fixed_strings = true;
                else if (strncmp(arg, "filter=", 7) == 0)
                  flag_filter = arg + 7;
                else if (strncmp(arg, "filter-magic-label=", 19) == 0)
                  flag_filter_magic_label.emplace_back(arg + 19);
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
                else if (strcmp(arg, "fuzzy") == 0)
                  flag_fuzzy = 1;
                else if (strncmp(arg, "fuzzy=", 6) == 0)
                  flag_fuzzy = strtofuzzy(arg + 6, "invalid argument --fuzzy=");
                else if (strcmp(arg, "free-space") == 0)
                  flag_free_space = true;
                else if (strcmp(arg, "file") == 0 ||
                    strcmp(arg, "file-extension") == 0 ||
                    strcmp(arg, "file-magic") == 0 ||
                    strcmp(arg, "file-type") == 0 ||
                    strcmp(arg, "filter") == 0 ||
                    strcmp(arg, "filter-magic-label") == 0 ||
                    strcmp(arg, "format") == 0 ||
                    strcmp(arg, "format-begin") == 0 ||
                    strcmp(arg, "format-close") == 0 ||
                    strcmp(arg, "format-end") == 0 ||
                    strcmp(arg, "format-open") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--file, --file-extension, --file-magic, --file-type, --files, --files-with-matches, --files-without-match, --fixed-strings, --filter, --filter-magic-label, --format, --format-begin, --format-close, --format-end, --format-open, --fuzzy or --free-space");
                break;

              case 'g':
                if (strncmp(arg, "glob=", 5) == 0)
                  flag_glob.emplace_back(arg + 5);
                else if (strncmp(arg, "group-separator=", 16) == 0)
                  flag_group_separator = arg + 16;
                else if (strcmp(arg, "group-separator") == 0)
                  flag_group_separator = "--";
                else if (strcmp(arg, "glob") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--glob or --group-separator");
                break;

              case 'h':
                if (strcmp(arg, "heading") == 0)
                  flag_heading = true;
                else if (strncmp(arg, "help", 4) == 0)
                  help(arg[4] != '\0' ? arg + 4 : ++i < argc ? argv[i] : NULL);
                else if (strcmp(arg, "hex") == 0)
                  flag_binary_files = "hex";
                else if (strcmp(arg, "hexdump") == 0)
                  flag_hexdump = "2";
                else if (strncmp(arg, "hexdump=", 8) == 0)
                  flag_hexdump = arg + 8;
                else if (strcmp(arg, "hidden") == 0)
                  flag_hidden = true;
                else if (strcmp(arg, "hyperlink") == 0)
                  flag_colors = "hl";
                else
                  usage("invalid option --", arg, "--heading, --help, --hex, --hexdump, --hidden or --hyperlink");
                break;

              case 'i':
                if (strcmp(arg, "ignore-binary") == 0)
                  flag_binary_files = "without-match";
                else if (strcmp(arg, "ignore-case") == 0)
                  flag_ignore_case = true;
                else if (strcmp(arg, "ignore-files") == 0)
                  flag_ignore_files.emplace_back(DEFAULT_IGNORE_FILE);
                else if (strncmp(arg, "ignore-files=", 13) == 0)
                  flag_ignore_files.emplace_back(arg + 13);
                else if (strncmp(arg, "include=", 8) == 0)
                  flag_include.emplace_back(arg + 8);
                else if (strncmp(arg, "include-dir=", 12) == 0)
                  flag_include_dir.emplace_back(arg + 12);
                else if (strncmp(arg, "include-from=", 13) == 0)
                  flag_include_from.emplace_back(arg + 13);
                else if (strncmp(arg, "include-fs=", 11) == 0)
                  flag_include_fs.emplace_back(arg + 11);
                else if (strcmp(arg, "initial-tab") == 0)
                  flag_initial_tab = true;
                else if (strcmp(arg, "invert-match") == 0)
                  flag_invert_match = true;
                else if (strcmp(arg, "include") == 0 ||
                    strcmp(arg, "include-dir") == 0 ||
                    strcmp(arg, "include-from") == 0 ||
                    strcmp(arg, "include-fs") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--ignore-case, --ignore-files, --include, --include-dir, --include-from, --include-fs, --initial-tab or --invert-match");
                break;

              case 'j':
                if (strncmp(arg, "jobs=", 5) == 0)
                  flag_jobs = strtonum(arg + 5, "invalid argument --jobs=");
                else if (strcmp(arg, "json") == 0)
                  flag_json = true;
                else if (strcmp(arg, "jobs") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--jobs or --json");
                break;

              case 'l':
                if (strncmp(arg, "label=", 6) == 0)
                  flag_label = arg + 6;
                else if (strcmp(arg, "line-buffered") == 0)
                  flag_line_buffered = true;
                else if (strcmp(arg, "line-number") == 0)
                  flag_line_number = true;
                else if (strcmp(arg, "line-regexp") == 0)
                  flag_line_regexp = true;
                else if (strcmp(arg, "lines") == 0)
                  flag_files = false;
                else if (strcmp(arg, "label") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--label, --line-buffered, --line-number, --line-regexp or --lines");
                break;

              case 'm':
                if (strcmp(arg, "match") == 0)
                  flag_match = true;
                else if (strncmp(arg, "max-count=", 10) == 0)
                  flag_max_count = strtopos(arg + 10, "invalid argument --max-count=");
                else if (strncmp(arg, "max-depth=", 10) == 0)
                  flag_max_depth = strtopos(arg + 10, "invalid argument --max-depth=");
                else if (strncmp(arg, "max-files=", 10) == 0)
                  flag_max_files = strtopos(arg + 10, "invalid argument --max-files=");
                else if (strncmp(arg, "max-line=", 9) == 0)
                  flag_max_line = strtopos(arg + 9, "invalid argument --max-line=");
                else if (strncmp(arg, "min-count=", 10) == 0)
                  flag_min_count = strtopos(arg + 10, "invalid argument --min-count=");
                else if (strncmp(arg, "min-depth=", 10) == 0)
                  flag_min_depth = strtopos(arg + 10, "invalid argument --min-depth=");
                else if (strncmp(arg, "min-line=", 9) == 0)
                  flag_min_line = strtopos(arg + 9, "invalid argument --min-line=");
                else if (strncmp(arg, "min-steal=", 10) == 0)
                  flag_min_steal = strtopos(arg + 10, "invalid argument --min-steal=");
                else if (strcmp(arg, "mmap") == 0)
                  flag_max_mmap = MAX_MMAP_SIZE;
                else if (strncmp(arg, "mmap=", 5) == 0)
                  flag_max_mmap = strtopos(arg + 5, "invalid argument --mmap=");
                else if (strcmp(arg, "messages") == 0)
                  flag_no_messages = false;
                else if (strcmp(arg, "max-count") == 0 ||
                    strcmp(arg, "max-depth") == 0 ||
                    strcmp(arg, "max-files") == 0 ||
                    strcmp(arg, "max-line") == 0 ||
                    strcmp(arg, "min-count") == 0 ||
                    strcmp(arg, "min-depth") == 0 ||
                    strcmp(arg, "min-line") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--match, --max-count, --max-depth, --max-files, --max-line, --min-count, --min-depth, --min-line, --mmap or --messages");
                break;

              case 'n':
                if (strncmp(arg, "neg-regexp=", 11) == 0)
                  option_regexp(pattern_args, arg + 1, true);
                else if (strcmp(arg, "not") == 0)
                  option_not(pattern_args, i, argc, argv);
                else if (strncmp(arg, "not=", 4) == 0)
                  option_not(pattern_args, arg + 4);
                else if (strcmp(arg, "no-any-line") == 0)
                  flag_any_line = false;
                else if (strcmp(arg, "no-binary") == 0)
                  flag_binary = false;
                else if (strcmp(arg, "no-bool") == 0)
                  flag_bool = false;
                else if (strcmp(arg, "no-break") == 0)
                  flag_break = false;
                else if (strcmp(arg, "no-byte-offset") == 0)
                  flag_byte_offset = false;
                else if (strcmp(arg, "no-color") == 0 || strcmp(arg, "no-colour") == 0)
                  flag_color = "never";
                else if (strcmp(arg, "no-column-number") == 0)
                  flag_column_number = false;
                else if (strcmp(arg, "no-confirm") == 0)
                  flag_confirm = false;
                else if (strcmp(arg, "no-decompress") == 0)
                  flag_decompress = false;
                else if (strcmp(arg, "no-dereference") == 0)
                  flag_no_dereference = true;
                else if (strcmp(arg, "no-dotall") == 0)
                  flag_dotall = false;
                else if (strcmp(arg, "no-empty") == 0)
                  flag_empty = false;
                else if (strcmp(arg, "no-filename") == 0)
                  flag_no_filename = true;
                else if (strcmp(arg, "no-group-separator") == 0)
                  flag_group_separator = NULL;
                else if (strcmp(arg, "no-heading") == 0)
                  flag_heading = false;
                else if (strcmp(arg, "no-hidden") == 0)
                  flag_hidden = false;
                else if (strcmp(arg, "no-ignore-binary") == 0)
                  flag_binary_files = "binary";
                else if (strcmp(arg, "no-ignore-case") == 0)
                  flag_ignore_case = false;
                else if (strcmp(arg, "no-ignore-files") == 0)
                  flag_ignore_files.clear();
                else if (strcmp(arg, "no-initial-tab") == 0)
                  flag_initial_tab = false;
                else if (strcmp(arg, "no-invert-match") == 0)
                  flag_invert_match = false;
                else if (strcmp(arg, "no-line-number") == 0)
                  flag_line_number = false;
                else if (strcmp(arg, "no-only-line-number") == 0)
                  flag_only_line_number = false;
                else if (strcmp(arg, "no-only-matching") == 0)
                  flag_only_matching = false;
                else if (strcmp(arg, "no-messages") == 0)
                  flag_no_messages = true;
                else if (strcmp(arg, "no-mmap") == 0)
                  flag_max_mmap = 0;
                else if (strcmp(arg, "no-pager") == 0)
                  flag_pager = NULL;
                else if (strcmp(arg, "no-pretty") == 0)
                  flag_pretty = false;
                else if (strcmp(arg, "no-smart-case") == 0)
                  flag_smart_case = false;
                else if (strcmp(arg, "no-sort") == 0)
                  flag_sort = NULL;
                else if (strcmp(arg, "no-stats") == 0)
                  flag_stats = NULL;
                else if (strcmp(arg, "no-ungroup") == 0)
                  flag_ungroup = false;
                else if (strcmp(arg, "no-view") == 0)
                  flag_view = NULL;
                else if (strcmp(arg, "null") == 0)
                  flag_null = true;
                else if (strcmp(arg, "neg-regexp") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--neg-regexp, --not, --no-any-line, --no-binary, --no-bool, --no-break, --no-byte-offset, --no-color, --no-confirm, --no-decompress, --no-dereference, --no-dotall, --no-empty, --no-filename, --no-group-separator, --no-heading, --no-hidden, --no-ignore-binary, --no-ignore-case, --no-ignore-files --no-initial-tab, --no-invert-match, --no-line-number, --no-only-line-number, --no-only-matching, --no-messages, --no-mmap, --no-pager, --no-pretty, --no-smart-case, --no-sort, --no-stats, --no-ungroup, --no-view or --null");
                break;

              case 'o':
                if (strcmp(arg, "only-line-number") == 0)
                  flag_only_line_number = true;
                else if (strcmp(arg, "only-matching") == 0)
                  flag_only_matching = true;
                else
                  usage("invalid option --", arg, "--only-line-number or --only-matching");
                break;

              case 'p':
                if (strcmp(arg, "pager") == 0)
                  flag_pager = DEFAULT_PAGER_COMMAND;
                else if (strncmp(arg, "pager=", 6) == 0)
                  flag_pager = arg + 6;
                else if (strcmp(arg, "passthru") == 0)
                  flag_any_line = true;
                else if (strcmp(arg, "perl-regexp") == 0)
                  flag_perl_regexp = true;
                else if (strcmp(arg, "pretty") == 0)
                  flag_pretty = true;
                else
                  usage("invalid option --", arg, "--pager, --passthru, --perl-regexp or --pretty");
                break;

              case 'q':
                if (strcmp(arg, "query") == 0)
                  flag_query = DEFAULT_QUERY_DELAY;
                else if (strncmp(arg, "query=", 6) == 0)
                  flag_query = strtopos(arg + 6, "invalid argument --query=");
                else if (strcmp(arg, "quiet") == 0)
                  flag_quiet = flag_no_messages = true;
                else
                  usage("invalid option --", arg, "--query or --quiet");
                break;

              case 'r':
                if (strncmp(arg, "range=", 6) == 0)
                  strtopos2(arg + 6, flag_min_line, flag_max_line, "invalid argument --range=");
                else if (strcmp(arg, "recursive") == 0)
                  flag_directories = "recurse";
                else if (strncmp(arg, "regexp=", 7) == 0)
                  option_regexp(pattern_args, arg + 7);
                else if (strncmp(arg, "replace=", 8) == 0)
                  flag_replace = arg + 8;
                else if (strcmp(arg, "range") == 0 || strcmp(arg, "regexp") == 0 || strcmp(arg, "replace") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--range, --recursive, --regexp or --replace");
                break;

              case 's':
                if (strcmp(arg, "save-config") == 0)
                  flag_save_config = ".ugrep";
                else if (strncmp(arg, "save-config=", 12) == 0)
                  flag_save_config = arg + 12;
                else if (strcmp(arg, "separator") == 0)
                  flag_separator = ":";
                else if (strncmp(arg, "separator=", 10) == 0)
                  flag_separator = arg + 10;
                else if (strcmp(arg, "silent") == 0)
                  flag_quiet = flag_no_messages = true;
                else if (strcmp(arg, "smart-case") == 0)
                  flag_smart_case = true;
                else if (strcmp(arg, "sort") == 0)
                  flag_sort = "name";
                else if (strncmp(arg, "sort=", 5) == 0)
                  flag_sort = arg + 5;
                else if (strcmp(arg, "stats") == 0)
                  flag_stats = "";
                else if (strncmp(arg, "stats=", 6) == 0)
                  flag_stats = arg + 6;
                else
                  usage("invalid option --", arg, "--save-config, --separator, --silent, --smart-case, --sort or --stats");
                break;

              case 't':
                if (strcmp(arg, "tabs") == 0)
                  flag_tabs = DEFAULT_TABS;
                else if (strncmp(arg, "tabs=", 5) == 0)
                  flag_tabs = strtopos(arg + 5, "invalid argument --tabs=");
                else if (strcmp(arg, "tag") == 0)
                  flag_tag = DEFAULT_TAG;
                else if (strncmp(arg, "tag=", 4) == 0)
                  flag_tag = arg + 4;
                else if (strcmp(arg, "text") == 0)
                  flag_binary_files = "text";
                else
                  usage("invalid option --", arg, "--tabs, --tag or --text");
                break;

              case 'u':
                if (strcmp(arg, "ungroup") == 0)
                  flag_ungroup = true;
                else
                  usage("invalid option --", arg, "--ungroup");
                break;

              case 'v':
                if (strcmp(arg, "version") == 0)
                  version();
                else if (strncmp(arg, "view=", 5) == 0)
                  flag_view = arg + 5;
                else if (strcmp(arg, "view") == 0)
                  flag_view = "";
                else
                  usage("invalid option --", arg, "--view or --version");
                break;

              case 'w':
                if (strncmp(arg, "width=", 6) == 0)
                  flag_width = strtopos(arg + 6, "invalid argument --width=");
                else if (strcmp(arg, "width") == 0)
                  flag_width = Screen::getsize();
                else if (strcmp(arg, "with-filename") == 0)
                  flag_with_filename = true;
                else if (strcmp(arg, "with-hex") == 0)
                  flag_binary_files = "with-hex";
                else if (strcmp(arg, "word-regexp") == 0)
                  flag_word_regexp = true;
                else
                  usage("invalid option --", arg, "--width, --with-filename, --with-hex or --word-regexp");
                break;

              case 'x':
                if (strcmp(arg, "xml") == 0)
                  flag_xml = true;
                else
                  usage("invalid option --", arg, "--xml");
                break;

              case 'z':
                if (strncmp(arg, "zmax=", 5) == 0)
                  flag_zmax = strtopos(arg + 5, "invalid argument --zmax=");
                else if (strcmp(arg, "zmax") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--zmax=");
                break;

              default:
                if (isdigit(*arg))
                  set_depth(arg);
                else
                  usage("invalid option --", arg);
            }
            break;

          case 'A':
            ++arg;
            if (*arg)
              flag_after_context = strtonum(&arg[*arg == '='], "invalid argument -A=");
            else if (++i < argc)
              flag_after_context = strtonum(argv[i], "invalid argument -A=");
            else
              usage("missing NUM argument for option -A");
            is_grouped = false;
            break;

          case 'a':
            flag_binary_files = "text";
            break;

          case 'B':
            ++arg;
            if (*arg)
              flag_before_context = strtonum(&arg[*arg == '='], "invalid argument -B=");
            else if (++i < argc)
              flag_before_context = strtonum(argv[i], "invalid argument -B=");
            else
              usage("missing NUM argument for option -B");
            is_grouped = false;
            break;

          case 'b':
            flag_byte_offset = true;
            break;

          case 'C':
            ++arg;
            if (*arg)
              flag_after_context = flag_before_context = strtonum(&arg[*arg == '='], "invalid argument -C=");
            else if (++i < argc)
              flag_after_context = flag_before_context = strtonum(argv[i], "invalid argument -C=");
            else
              usage("missing NUM argument for option -C");
            is_grouped = false;
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
              usage("missing ACTION argument for option -D");
            is_grouped = false;
            break;

          case 'd':
            ++arg;
            if (*arg)
              flag_directories = &arg[*arg == '='];
            else if (++i < argc)
              flag_directories = argv[i];
            else
              usage("missing ACTION argument for option -d");
            is_grouped = false;
            break;

          case 'E':
            flag_basic_regexp = false;
            break;

          case 'e':
            ++arg;
            if (*arg)
              option_regexp(pattern_args, &arg[*arg == '=']);
            else if (++i < argc)
              option_regexp(pattern_args, argv[i]);
            else
              usage("missing PATTERN argument for option -e");
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
              usage("missing FILE argument for option -f");
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
              usage("missing GLOB argument for option -g");
            is_grouped = false;
            break;

          case 'H':
            flag_with_filename = true;
            break;

          case 'h':
            flag_no_filename = true;
            break;

          case 'I':
            flag_binary_files = "without-match";
            break;

          case 'i':
            flag_ignore_case = true;
            break;

          case 'J':
            ++arg;
            if (*arg)
              flag_jobs = strtonum(&arg[*arg == '='], "invalid argument -J=");
            else if (++i < argc)
              flag_jobs = strtonum(argv[i], "invalid argument -J=");
            else
              usage("missing NUM argument for option -J");
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
              usage("missing NUM argument for option -K");
            is_grouped = false;
            break;

          case 'k':
            flag_column_number = true;
            break;

          case 'L':
            flag_files_without_match = true;
            break;

          case 'l':
            flag_files_with_matches = true;
            break;

          case 'M':
            ++arg;
            if (*arg)
              flag_file_magic.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_magic.emplace_back(argv[i]);
            else
              usage("missing MAGIC argument for option -M");
            is_grouped = false;
            break;

          case 'm':
            ++arg;
            if (*arg)
              strtopos2(&arg[*arg == '='], flag_min_count, flag_max_count, "invalid argument -m=");
            else if (++i < argc)
              strtopos2(argv[i], flag_min_count, flag_max_count, "invalid argument -m=");
            else
              usage("missing MAX argument for option -m");
            is_grouped = false;
            break;

          case 'N':
            ++arg;
            if (*arg)
              option_regexp(pattern_args, &arg[*arg == '='], true);
            else if (++i < argc)
              option_regexp(pattern_args, argv[i], true);
            else
              usage("missing PATTERN argument for option -N");
            is_grouped = false;
            break;

          case 'n':
            flag_line_number = true;
            break;

          case 'O':
            ++arg;
            if (*arg)
              flag_file_extension.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_extension.emplace_back(argv[i]);
            else
              usage("missing EXTENSIONS argument for option -O");
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
            if (*arg == '=' || isdigit(*arg))
            {
              flag_query = strtopos(&arg[*arg == '='], "invalid argument -Q=");
              is_grouped = false;
            }
            else
            {
              flag_query = DEFAULT_QUERY_DELAY;
              --arg;
            }
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
              usage("missing TYPES argument for option -t");
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
            ++arg;
            if (*arg == '=' || strncmp(arg, "best", 4) == 0 || isdigit(*arg) || strchr("+-~", *arg) != NULL)
            {
              flag_fuzzy = strtofuzzy(&arg[*arg == '='], "invalid argument -Z=");
              is_grouped = false;
            }
            else
            {
              flag_fuzzy = 1;
              --arg;
            }
            break;


          case 'z':
            flag_decompress = true;
            break;

          case '0':
            flag_null = true;
            break;

          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            if (flag_min_depth == 0 && flag_max_depth > 0)
              flag_min_depth = flag_max_depth;
            flag_max_depth = *arg - '0';
            if (flag_min_depth > flag_max_depth)
              usage("invalid argument -", arg);
            break;

          case '?':
            help(arg[1] != '\0' ? arg + 1 : ++i < argc ? argv[i] : NULL);
            break;

          case '%':
            flag_bool = true;
            break;

          case '+':
            flag_heading = true;
            break;

          case '.':
            flag_hidden = true;
            break;

          default:
            usage("invalid option -", arg);
        }

        if (!is_grouped)
          break;
      }
    }
    else if (strcmp(arg, "-") == 0)
    {
      // read standard input
      flag_stdin = true;
    }
    else if (arg_pattern == NULL && !flag_match && !flag_not && pattern_args.empty() && flag_file.empty())
    {
      // no regex pattern specified yet, so assume it is PATTERN
      arg_pattern = arg;
    }
    else
    {
      // otherwise add the file argument to the list of FILE files
      arg_files.emplace_back(arg);
    }
  }

  if (flag_not)
    usage("missing PATTERN for --not");
}

// parse -e PATTERN and -N PATTERN
void option_regexp(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg, bool is_neg)
{
  if (flag_query)
  {
    // -Q: pass -e PATTERN and -N PATTERN patterns to the query engine
    if (is_neg)
    {
      std::string neg_arg(arg);
      neg_arg.insert(0, "(?^").append(")");
      flag_regexp.emplace_back(neg_arg);
    }
    else
    {
      flag_regexp.emplace_back(arg);
    }
  }
  else
  {
    pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::NA) | (is_neg ? CNF::PATTERN::NEG : CNF::PATTERN::NA), arg);
  }
}

// parse --and [PATTERN]
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  if (flag_query)
    usage("option -Q does not support --and");

  pattern_args.emplace_back(CNF::PATTERN::TERM, "");

  if (i + 1 < argc && *argv[i + 1] != '-')
    pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::NA), argv[++i]);
}

// parse --and=PATTERN
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  if (flag_query)
    usage("option -Q does not support --and");

  pattern_args.emplace_back(CNF::PATTERN::TERM, "");
  pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::NA), arg);
}

// parse --andnot [PATTERN]
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  if (flag_query)
    usage("option -Q does not support --andnot");

  pattern_args.emplace_back(CNF::PATTERN::TERM, "");

  flag_not = true;

  if (i + 1 < argc && *argv[i + 1] != '-')
  {
    pattern_args.emplace_back(CNF::PATTERN::NOT, argv[++i]);
    flag_not = false;
  }
}

// parse --andnot=PATTERN
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  if (flag_query)
    usage("option -Q does not support --andnot");

  pattern_args.emplace_back(CNF::PATTERN::TERM, "");
  pattern_args.emplace_back(CNF::PATTERN::NOT, arg);
}

// parse --not [PATTERN]
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  if (flag_query)
    usage("option -Q does not support --not");

  flag_not = !flag_not;

  if (i + 1 < argc && *argv[i + 1] != '-')
  {
    pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::NA), argv[++i]);
    flag_not = false;
  }
}

// parse --not=PATTERN
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg)
{
  if (flag_query)
    usage("option -Q does not support --not");

  flag_not = !flag_not;

  pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::NA), arg);
  flag_not = false;
}

// parse the command-line options and initialize
void init(int argc, const char **argv)
{
  // get home directory path to expand ~ in options with file arguments, using fopen_smart()

#ifdef OS_WIN
  home_dir = getenv("USERPROFILE");
#else
  home_dir = getenv("HOME");
#endif

  // --config=FILE or ---FILE: load configuration file first before parsing any other options

  for (int i = 1; i < argc; ++i)
  {
    if (strcmp(argv[i], "--") == 0)
      break;

    if (strncmp(argv[i], "--config", 8) == 0)
    {
      if (flag_config != NULL)
        std::cerr << "ugrep: warning: multiple configurations specified, ignoring extra " << argv[i] << '\n';
      else if (argv[i][8] == '\0')
        flag_config = "";
      else if (argv[i][8] == '=')
        flag_config = argv[i] + 9;
    }
    else if (strncmp(argv[i], "---", 3) == 0)
    {
      if (flag_config != NULL)
        std::cerr << "ugrep: warning: multiple configurations specified, ignoring extra " << argv[i] << '\n';
      else
        flag_config = argv[i] + 3;
    }
  }

  // collect regex pattern arguments -e PATTERN, -N PATTERN, --and PATTERN, --andnot PATTERN
  std::list<std::pair<CNF::PATTERN,const char*>> pattern_args;

  if (flag_config != NULL)
    load_config(pattern_args);

  // apply the appropriate options when the program is named grep, egrep, fgrep, zgrep, zegrep, zfgrep

  const char *program = strrchr(argv[0], PATHSEPCHR);

  if (program == NULL)
    program = argv[0];
  else
    ++program;

  size_t len = strlen(program);

#ifdef OS_WIN
  // Windows: compare executable name up to a dot when present, e.g. "ug.exe" compares as "ug"
  const char *dot = strchr(program, '.');
  if (dot != NULL)
    len = dot - program;
#endif

  if (strncmp(program, "ug", len) == 0)
  {
    // the 'ug' command is equivalent to 'ugrep --sort --config' to sort output by default and load custom configuration files, when no --config=FILE is specified
    flag_sort = "name";
    if (flag_config == NULL)
      load_config(pattern_args);
  }
  else if (strncmp(program, "grep", len) == 0)
  {
    // the 'grep' command is equivalent to 'ugrep -GY. --sort'
    flag_basic_regexp = true;
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "egrep", len) == 0)
  {
    // the 'egrep' command is equivalent to 'ugrep -Y. --sort'
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "fgrep", len) == 0)
  {
    // the 'fgrep' command is equivalent to 'ugrep -FY. --sort'
    flag_fixed_strings = true;
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zgrep", len) == 0)
  {
    // the 'zgrep' command is equivalent to 'ugrep -zGY. --sort'
    flag_decompress = true;
    flag_basic_regexp = true;
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zegrep", len) == 0)
  {
    // the 'zegrep' command is equivalent to 'ugrep -zY. --sort'
    flag_decompress = true;
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zfgrep", len) == 0)
  {
    // the 'zfgrep' command is equivalent to 'ugrep -zFY. --sort'
    flag_decompress = true;
    flag_fixed_strings = true;
    flag_hidden = true;
    flag_empty = true;
    flag_sort = "name";
  }

  // parse ugrep command-line options and arguments

  options(pattern_args, argc, argv);

  if (warnings > 0)
  {
    std::cerr << "Usage: ugrep [OPTIONS] [PATTERN] [-f FILE] [-e PATTERN] [FILE ...]\n";
    std::cerr << "Try `ugrep --help' or `ugrep --help WHAT' for more information\n";
    exit(EXIT_ERROR);
  }

  // -t list: list table of types and exit
  if (flag_file_type.size() == 1 && flag_file_type[0] == "list")
  {
    std::cerr << std::setw(12) << "FILE TYPE" << "   -O EXTENSIONS, -g FILENAMES AND FILE SIGNATURE -M 'MAGIC BYTES'\n";

    for (int i = 0; type_table[i].type != NULL; ++i)
    {
      std::cerr << std::setw(12) << type_table[i].type << " = -O " << type_table[i].extensions << '\n';
      if (type_table[i].filenames)
        std::cerr << std::setw(18) << "-g " << type_table[i].filenames << "\n";
      if (type_table[i].magic)
        std::cerr << std::setw(19) << "-M '" << type_table[i].magic << "'\n";
    }

    exit(EXIT_ERROR);
  }

#ifndef HAVE_LIBZ
  // -z: but we don't have libz
  if (flag_decompress)
    usage("option -z is not available in this build configuration of ugrep");
#endif

  // --zmax: NUM argument exceeds limit?
  if (flag_zmax > 99)
    usage("option --zmax argument exceeds upper limit");

  // -P disables -F, -G and -Z (P>F>G>E override)
  if (flag_perl_regexp)
  {
#if defined(HAVE_PCRE2) || defined(HAVE_BOOST_REGEX)
    flag_fixed_strings = false;
    flag_basic_regexp = false;
    if (flag_fuzzy > 0)
      usage("options -P and -Z are not compatible");
#else
    usage("option -P is not available in this build configuration of ugrep");
#endif
  }

  // -F disables -G (P>F>G>E override)
  if (flag_fixed_strings)
    flag_basic_regexp = false;

  // populate the CNF with the collected regex pattern args, each arg points to a persistent command line argv[]
  for (const auto &arg : pattern_args)
  {
    if (arg.first == CNF::PATTERN::TERM)
      bcnf.new_term();
    else
      bcnf.new_pattern(arg.first, arg.second); // relies on options --bool, -F, -G, -w, -x and -f
  }

  // --query: override --pager
  if (flag_query > 0)
    flag_pager = NULL;

  // check TTY info and set colors (warnings and errors may occur from here on)
  terminal();

  // --save-config and --save-config=FILE
  if (flag_save_config != NULL)
  {
    save_config();

    exit(EXIT_ERROR);
  }

#ifdef OS_WIN
  // save_config() and help() assume text mode, so switch to
  // binary after we're no longer going to call them.
  (void)_setmode(fileno(stdout), _O_BINARY);
#endif

  // --encoding: parse ENCODING value
  if (flag_encoding != NULL)
  {
    int i, j;

    // scan the encoding_table[] for a matching encoding, case insensitive ASCII
    for (i = 0; encoding_table[i].format != NULL; ++i)
    {
      for (j = 0; flag_encoding[j] != '\0' && encoding_table[i].format[j] != '\0'; ++j)
        if (toupper(flag_encoding[j]) != toupper(encoding_table[i].format[j]))
          break;

      if (flag_encoding[j] == '\0' && encoding_table[i].format[j] == '\0')
        break;
    }

    if (encoding_table[i].format == NULL)
    {
      std::string msg = "invalid argument --encoding=ENCODING, valid arguments are";

      for (int i = 0; encoding_table[i].format != NULL; ++i)
        msg.append(" '").append(encoding_table[i].format).append("',");
      msg.pop_back();

      usage(msg.c_str());
    }

    // encoding is the file encoding used by all input files, if no BOM is present
    flag_encoding_type = encoding_table[i].encoding;
  }

  // --binary-files: normalize by assigning flags
  if (strcmp(flag_binary_files, "without-match") == 0)
    flag_binary_without_match = true;
  else if (strcmp(flag_binary_files, "text") == 0)
    flag_text = true;
  else if (strcmp(flag_binary_files, "hex") == 0)
    flag_hex = true;
  else if (strcmp(flag_binary_files, "with-hex") == 0)
    flag_with_hex = true;
  else if (strcmp(flag_binary_files, "binary") != 0)
    usage("invalid argument --binary-files=TYPE, valid arguments are 'binary', 'without-match', 'text', 'hex' and 'with-hex'");

  // --hex takes priority over --with-hex takes priority over -I takes priority over -a
  if (flag_hex)
    flag_with_hex = (flag_binary_without_match = flag_text = false);
  else if (flag_with_hex)
    flag_binary_without_match = (flag_text = false);
  else if (flag_binary_without_match)
    flag_text = false;

  // --hexdump: normalize by assigning flags
  if (flag_hexdump != NULL)
  {
    int context = 0;

    flag_hex_after = (flag_after_context == 0);
    flag_hex_before = (flag_before_context == 0);

    for (const char *s = flag_hexdump; *s != '\0'; ++s)
    {
      switch (*s)
      {
        case 'a':
          flag_hex_star = true;
          context = 0;
          break;

        case 'b':
          flag_hex_hbr = flag_hex_cbr = false;
          context = 0;
          break;

        case 'c':
          flag_hex_chr = false;
          context = 0;
          break;

        case 'h':
          flag_hex_hbr = false;
          context = 0;
          break;

        case 'A':
          flag_hex_after = 0;
          context = 1;
          break;

        case 'B':
          flag_hex_before = 0;
          context = 2;
          break;

        case 'C':
          flag_hex_after = flag_hex_before = 0;
          context = 3;
          break;

        default:
          char *r = NULL;
          size_t num = static_cast<size_t>(strtoull(s, &r, 10));
          if (r == NULL)
            usage("invalid argument --hexdump=[1-8][a][bch][A[NUM]][B[NUM]][C[NUM]]");
          s = r - 1;
          switch (context)
          {
            case 0:
              flag_hex_columns = 8 * num;
              if (flag_hex_columns == 0 || flag_hex_columns > MAX_HEX_COLUMNS)
                usage("invalid argument --hexdump=[1-8][a][bch][A[NUM]][B[NUM]][C[NUM]]");
              break;

            case 1:
              flag_hex_after = num + 1;
              break;

            case 2:
              flag_hex_before = num + 1;
              break;

            case 3:
              flag_hex_after = flag_hex_before = num + 1;
              break;
          }
      }
    }

    // enable option -X if -W is not enabled
    if (!flag_with_hex)
      flag_hex = true;
  }

  // --tabs: value should be 1, 2, 4, or 8
  if (flag_tabs && flag_tabs != 1 && flag_tabs != 2 && flag_tabs != 4 && flag_tabs != 8)
    usage("invalid argument --tabs=NUM, valid arguments are 1, 2, 4, or 8");

  // --match: same as specifying an empty "" pattern argument
  if (flag_match)
    arg_pattern = "";

  // if no regex pattern is specified and no -e PATTERN and no -f FILE and not -Q, then exit with usage message
  if (arg_pattern == NULL && pattern_args.empty() && flag_file.empty() && flag_query == 0)
    usage("no PATTERN specified: specify --match or an empty \"\" pattern to match all input");

  // regex PATTERN should be a FILE argument when -Q or -e PATTERN is specified
  if (!flag_match && arg_pattern != NULL && (flag_query > 0 || !pattern_args.empty()))
  {
    arg_files.insert(arg_files.begin(), arg_pattern);
    arg_pattern = NULL;
  }

#ifdef OS_WIN

  // Windows shell does not expand wildcards in arguments, do that now (basename part only)
  if (!arg_files.empty())
  {
    std::vector<const char*> expanded_arg_files;

    for (const auto& arg_file : arg_files)
    {
      std::wstring filename = utf8_decode(arg_file);
      bool has_wildcard_char = false;

      size_t basename_pos;
      for (basename_pos = filename.size(); basename_pos > 0; --basename_pos)
      {
        wchar_t ch = filename[basename_pos - 1];

        if (ch == L'*' || ch == L'?')
          has_wildcard_char = true;
        else if (ch == L'\\' || ch == L'/' || ch == L':')
          break;
      }

      if (!has_wildcard_char)
      {
        // no wildcard chars, use argument as-is
        expanded_arg_files.push_back(arg_file);
        continue;
      }

      WIN32_FIND_DATAW find_data;

      HANDLE hFile = FindFirstFileExW(filename.c_str(), FindExInfoBasic, &find_data, FindExSearchNameMatch, NULL, 0);
      if (hFile == INVALID_HANDLE_VALUE)
      {
        // glob pattern didn't match any files, use argument as-is which will trigger a warning later
        expanded_arg_files.push_back(arg_file);
        continue;
      }

      bool glob_starts_with_dot = filename[basename_pos] == L'.';

      do
      {
        if (find_data.cFileName[0] == L'.')
        {
          // don't expand directories "." or ".."
          if (find_data.cFileName[1] == 0 ||
              (find_data.cFileName[1] == L'.' && find_data.cFileName[2] == 0))
            continue;

          // don't expand hidden files unless --hidden or the pattern started with '.'
          if (!flag_hidden && !glob_starts_with_dot)
            continue;
        }

        // replace glob pattern with matching filename converted to UTF-8, then add to expanded filename list
        filename.erase(basename_pos);
        filename += find_data.cFileName;
        arg_strings.emplace_back(utf8_encode(filename));
        expanded_arg_files.push_back(arg_strings.back().c_str());
      } while (FindNextFileW(hFile, &find_data));

      FindClose(hFile);
    }

    // replace the original filenames list with the expanded list
    arg_files.swap(expanded_arg_files);
  }

#endif

  if (flag_devices != NULL)
  {
    // -D: check ACTION value
    if (strcmp(flag_devices, "skip") == 0)
      flag_devices_action = Action::SKIP;
    else if (strcmp(flag_devices, "read") == 0)
      flag_devices_action = Action::READ;
    else
      usage("invalid argument -D ACTION, valid arguments are 'skip' and 'read'");
  }

  if (flag_directories != NULL)
  {
    // normalize -R (--dereference-recurse) option
    if (strcmp(flag_directories, "dereference-recurse") == 0)
    {
      flag_directories = "recurse";
      flag_dereference = true;
    }

    // -d: check ACTION value and set flags
    if (strcmp(flag_directories, "skip") == 0)
      flag_directories_action = Action::SKIP;
    else if (strcmp(flag_directories, "read") == 0)
      flag_directories_action = Action::READ;
    else if (strcmp(flag_directories, "recurse") == 0)
      flag_directories_action = Action::RECURSE;
    else
      usage("invalid argument -d ACTION, valid arguments are 'skip', 'read', 'recurse' and 'dereference-recurse'");
  }

  if (!flag_stdin && arg_files.empty())
  {
    // if no FILE specified when reading standard input from a TTY then enable -R if not already -r or -R
    if (isatty(STDIN_FILENO) && (flag_directories_action == Action::UNSP || flag_directories_action == Action::RECURSE))
    {
      if (flag_directories_action == Action::UNSP)
        flag_directories_action = Action::RECURSE;

      flag_all_threads = true;
    }
    else
    {
      // if no FILE specified then read standard input
      flag_stdin = true;
    }
  }

  // check FILE arguments, warn about non-existing and non-readable files and directories
  auto file = arg_files.begin();
  while (file != arg_files.end())
  {
#ifdef OS_WIN

    DWORD attr = GetFileAttributesW(utf8_decode(*file).c_str());

    if (attr == INVALID_FILE_ATTRIBUTES)
    {
      // FILE does not exist
      errno = ENOENT;
      warning(NULL, *file);

      file = arg_files.erase(file);
      if (arg_files.empty())
        exit(EXIT_ERROR);
    }
    else
    {
      // use threads to recurse into a directory
      if ((attr & FILE_ATTRIBUTE_DIRECTORY))
      {
        if (flag_directories_action == Action::UNSP)
          flag_all_threads = true;

        // remove trailing path separators, if any (*file points to argv[])
        trim_pathname_arg(*file);
      }

      ++file;
    }

#else

    struct stat buf;
    int ret;

    if (flag_no_dereference)
      ret = lstat(*file, &buf);
    else
      ret = stat(*file, &buf);

    if (ret != 0)
    {
      // the specified file or directory does not exist
      warning(NULL, *file);

      file = arg_files.erase(file);
      if (arg_files.empty())
        exit(EXIT_ERROR);
    }
    else
    {
      if (flag_no_dereference && S_ISLNK(buf.st_mode))
      {
        // -p: skip symlinks
        file = arg_files.erase(file);
        if (arg_files.empty())
          exit(EXIT_ERROR);
      }
      else if ((buf.st_mode & S_IRUSR) == 0)
      {
        // the specified file or directory is not readable
        errno = EACCES;
        warning("cannot read", *file);

        file = arg_files.erase(file);
        if (arg_files.empty())
          exit(EXIT_ERROR);
      }
      else
      {
        // use threads to recurse into a directory
        if (S_ISDIR(buf.st_mode))
        {
          if (flag_directories_action == Action::UNSP)
            flag_all_threads = true;

          // remove trailing path separators, if any (*file points to argv[])
          trim_pathname_arg(*file);
        }

        ++file;
      }
    }

#endif
  }

  // normalize --cpp, --csv, --json, --xml to their corresponding --format
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
    flag_format_open  = "%+";
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
  else if (flag_only_line_number)
  {
    flag_format_open  = "%+";
    flag_format       = "%F%n%s%K%B\n%u";
  }

  // --replace clashes with --format
  if (flag_replace != NULL && flag_format != NULL)
    abort("--format is not permitted with --replace");

  // -v with --files is not permitted
  if (flag_invert_match && flag_files)
    abort("--invert-match is not permitted with --files, invert the Boolean query instead");

  // --min-count is not permitted with --files
  if (flag_min_count > 0 && flag_files)
    abort("--min-count is not permitted with --files");

  // --min-count is not permitted with -v when not combined with -q, -l, -L or -c
  if (flag_min_count > 0 && flag_invert_match && !flag_quiet && !flag_files_with_matches && !flag_files_without_match && !flag_count)
    abort("--min-count is not permitted with --invert-match");

#ifdef HAVE_STATVFS

  // --exclude-fs: add file system ids to exclude
  for (const auto& mounts : flag_exclude_fs)
  {
    if (!mounts.empty())
    {
      struct statvfs buf;
      size_t from = 0;

      while (true)
      {
        size_t to = mounts.find(',', from);
        size_t size = (to == std::string::npos ? mounts.size() : to) - from;

        if (size > 0)
        {
          std::string mount(mounts.substr(from, size));

          if (statvfs(mount.c_str(), &buf) == 0)
            exclude_fs_ids.insert(static_cast<uint64_t>(buf.f_fsid));
          else
            warning("--exclude-fs", mount.c_str());
        }

        if (to == std::string::npos)
          break;

        from = to + 1;
      }
    }
  }

  // --include-fs: add file system ids to include
  for (const auto& mounts : flag_include_fs)
  {
    if (!mounts.empty())
    {
      struct statvfs buf;
      size_t from = 0;

      while (true)
      {
        size_t to = mounts.find(',', from);
        size_t size = (to == std::string::npos ? mounts.size() : to) - from;

        if (size > 0)
        {
          std::string mount(mounts.substr(from, size));

          if (statvfs(mount.c_str(), &buf) == 0)
            include_fs_ids.insert(static_cast<uint64_t>(buf.f_fsid));
          else
            warning("--include-fs", mount.c_str());
        }

        if (to == std::string::npos)
          break;

        from = to + 1;
      }
    }
  }

#endif

  // --exclude-from: add globs to the exclude and exclude-dir lists
  for (const auto& from : flag_exclude_from)
  {
    if (!from.empty())
    {
      FILE *file = NULL;

      if (fopen_smart(&file, from.c_str(), "r") != 0)
        error("option --exclude-from: cannot read", from.c_str());

      import_globs(file, flag_exclude, flag_exclude_dir);

      if (file != stdin)
        fclose(file);
    }
  }

  // --include-from: add globs to the include and include-dir lists
  for (const auto& from : flag_include_from)
  {
    if (!from.empty())
    {
      FILE *file = NULL;

      if (fopen_smart(&file, from.c_str(), "r") != 0)
        error("option --include-from: cannot read", from.c_str());

      import_globs(file, flag_include, flag_include_dir);

      if (file != stdin)
        fclose(file);
    }
  }

  // -t: parse TYPES and access type table to add -O (--file-extension), -g (--glob) and -M (--file-magic) values
  for (const auto& types : flag_file_type)
  {
    size_t from = 0;

    while (true)
    {
      size_t to = types.find(',', from);
      size_t size = (to == std::string::npos ? types.size() : to) - from;

      if (size > 0)
      {
        bool negate = size > 1 && (types[from] == '!' || types[from] == '^');

        if (negate)
        {
          ++from;
          --size;
        }

        std::string type(types.substr(from, size));

        size_t i;

        // scan the type_table[] for a matching type
        for (i = 0; type_table[i].type != NULL; ++i)
          if (type == type_table[i].type)
            break;

        if (type_table[i].type == NULL)
        {
          std::string msg = "invalid argument -t TYPES, valid arguments are";

          for (int i = 0; type_table[i].type != NULL; ++i)
            msg.append(" '").append(type_table[i].type).append("',");
          msg.append(" and 'list' to show a detailed list of file types");

          usage(msg.c_str());
        }

        std::string temp(type_table[i].extensions);

        if (negate)
        {
          temp.insert(0, "!");
          size_t j = 0;
          while ((j = temp.find(',', j)) != std::string::npos)
            temp.insert(++j, "!");
        }

        flag_file_extension.emplace_back(temp);

        if (type_table[i].filenames != NULL)
        {
          temp.assign(type_table[i].filenames);

          if (negate)
          {
            temp.insert(0, "!");
            size_t j = 0;
            while ((j = temp.find(',', j)) != std::string::npos)
              temp.insert(++j, "!");
          }

          flag_glob.emplace_back(temp);
        }

        if (type_table[i].magic != NULL)
        {
          flag_file_magic.emplace_back(type_table[i].magic);

          if (negate)
            flag_file_magic.back().insert(0, "!");
        }
      }

      if (to == std::string::npos)
        break;

      from = to + 1;
    }
  }

  // -O: add filename extensions as globs
  for (const auto& extensions : flag_file_extension)
  {
    size_t from = 0;
    std::string glob;

    while (true)
    {
      size_t to = extensions.find(',', from);
      size_t size = (to == std::string::npos ? extensions.size() : to) - from;

      if (size > 0)
      {
        bool negate = size > 1 && (extensions[from] == '!' || extensions[from] == '^');

        if (negate)
        {
          ++from;
          --size;
        }

        flag_glob.emplace_back(glob.assign(negate ? "^*." : "*.").append(extensions.substr(from, size)));
      }

      if (to == std::string::npos)
        break;

      from = to + 1;
    }
  }

  // -M: file "magic bytes" regex string
  std::string magic_regex;

  // -M !MAGIC: combine to create a regex string
  for (const auto& magic : flag_file_magic)
  {
    if (magic.size() > 1 && (magic.front() == '!' || magic.front() == '^'))
    {
      if (!magic_regex.empty())
        magic_regex.push_back('|');
      magic_regex.append(magic.substr(1));

      // tally negative MAGIC patterns
      ++flag_min_magic;
    }
  }

  // -M MAGIC: append to regex string
  for (const auto& magic : flag_file_magic)
  {
    if (magic.size() <= 1 || (magic.front() != '!' && magic.front() != '^'))
    {
      if (!magic_regex.empty())
        magic_regex.push_back('|');
      magic_regex.append(magic);

      // we have positive MAGIC patterns, so scan() is a match when flag_min_magic or greater
      flag_not_magic = flag_min_magic;
    }
  }

  // -M: create a magic matcher for the MAGIC regex to match file with magic.scan()
  try
  {
    // construct magic_pattern DFA for -M !MAGIC and -M MAGIC
    if (!magic_regex.empty())
      magic_pattern.assign(magic_regex, "r");
    magic_matcher.pattern(magic_pattern);
  }

  catch (reflex::regex_error& error)
  {
    abort("option -M: ", error.what());
  }

  // --filter-magic-label: construct filter_magic_pattern and map "magic bytes" to labels
  magic_regex = "(";

  // --filter-magic-label: append pattern to magic_labels, parenthesized to ensure capture indexing
  for (auto& label : flag_filter_magic_label)
  {
    if (!label.empty())
    {
      size_t sep = label.find(':');

      if (sep != std::string::npos && sep > 0 && sep + 1 < label.size())
      {
        if (!label.empty() && magic_regex.size() > 1)
          magic_regex.append(")|(");
        magic_regex.append(label.substr(sep + 1));

        // truncate so we end up with a list of labels without patterns
        label.resize(sep);
      }
      else
      {
        abort("option --filter-magic-label: invalid LABEL:MAGIC argument ", label);
      }
    }
  }

  magic_regex.push_back(')');

  // --filter-magic-label: create a filter_magic_pattern
  try
  {
    // construct filter_magic_pattern DFA
    if (magic_regex.size() > 2)
      filter_magic_pattern.assign(magic_regex, "r");
  }

  catch (reflex::regex_error& error)
  {
    abort("option --filter-magic-label: ", error.what());
  }
}

// check TTY info and set colors
void terminal()
{
  if (flag_query > 0)
  {
    // -Q: disable --quiet
    flag_quiet = false;
  }
  else if (!flag_quiet)
  {
    // is output sent to a color TTY, to a pager, or to /dev/null?

    // check if standard output is a TTY
    tty_term = isatty(STDOUT_FILENO) != 0;

#ifndef OS_WIN

    if (!tty_term)
    {
      output_stat_result = fstat(STDOUT_FILENO, &output_stat) == 0;
      output_stat_regular = output_stat_result && S_ISREG(output_stat.st_mode);

      // if output is sent to /dev/null, then enable -q (i.e. "cheat" like GNU grep!)
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
  }

  // whether to apply colors
  flag_apply_color = flag_tag != NULL ? "never" : flag_query > 0 ? "always" : flag_color;

  if (!flag_quiet)
  {
    if (tty_term || flag_query > 0)
    {
      if (flag_pretty)
      {
        // --pretty: if output is to a TTY then enable --color, --heading, -T, -n, and --sort

        // enable --color
        if (flag_apply_color == NULL)
          flag_apply_color = "auto";

        // enable --heading if not explicitly disabled (enables --break later)
        if (flag_heading.is_undefined())
          flag_heading = true;

        // enable -T if not explicitly disabled (initial tab)
        if (flag_initial_tab.is_undefined())
          flag_initial_tab = true;

        // enable -n if not explicitly disabled
        if (flag_line_number.is_undefined())
          flag_line_number = true;

        // enable --sort=name if no --sort specified
        if (flag_sort == NULL)
          flag_sort = "name";
      }
      else if (flag_apply_color != NULL)
      {
        // --colors: if output is to a TTY then enable --color and use the specified --colors

        // enable --color
        if (flag_apply_color == NULL)
          flag_apply_color = "auto";
      }

      if (flag_query > 0)
      {
        // --query: run the interactive query UI

        // enable --heading if not explicitly disabled (enables --break later)
        if (flag_heading.is_undefined())
          flag_heading = true;

        // enable --line-buffered to flush output immediately
        flag_line_buffered = true;
      }
      else if (flag_pager != NULL && *flag_pager != '\0')
      {
        // --pager: if output is to a TTY then page through the results

        // open a pipe to a forked pager
#ifdef OS_WIN
        output = popen(flag_pager, "wb");
#else
        output = popen(flag_pager, "w");
#endif
        if (output == NULL)
          error("cannot open pipe to pager", flag_pager);

        // enable --heading if not explicitly disabled (enables --break later)
        if (flag_heading.is_undefined())
          flag_heading = true;

        // enable --line-buffered to flush output to the pager immediately
        flag_line_buffered = true;
      }
    }

    // --color: (re)set flag_apply_color depending on color_term and TTY output
    if (flag_apply_color != NULL)
    {
      color_term = flag_query > 0;

      if (strcmp(flag_apply_color, "never") == 0 || strcmp(flag_apply_color, "no") == 0 || strcmp(flag_apply_color, "none") == 0)
      {
        flag_apply_color = NULL;
      }
      else
      {
#ifdef OS_WIN

        if (tty_term || flag_query > 0)
        {
#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
          // assume we have a color terminal on Windows if isatty() is true
          HANDLE hConOut = GetStdHandle(STD_OUTPUT_HANDLE);
          if (hConOut != INVALID_HANDLE_VALUE)
          {
#ifdef CP_UTF8
            // enable UTF-8 output
            SetConsoleOutputCP(CP_UTF8);
#endif
            // try virtual terminal processing for ANSI SGR codes, enable colors when successful
            DWORD outMode;
            GetConsoleMode(hConOut, &outMode);
            outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            color_term = SetConsoleMode(hConOut, outMode) != 0;
          }
#endif
        }

#else

        // check whether we have a color terminal
        if (tty_term)
        {
          const char *term;
          if (getenv("COLORTERM") != NULL ||
              ((term = getenv("TERM")) != NULL &&
               (strstr(term, "ansi") != NULL ||
                strstr(term, "xterm") != NULL ||
                strstr(term, "screen") != NULL ||
                strstr(term, "color") != NULL)))
            color_term = true;
        }

#endif

        if (strcmp(flag_apply_color, "auto") == 0 || strcmp(flag_apply_color, "tty") == 0 || strcmp(flag_apply_color, "if-tty") == 0)
        {
          if (!color_term)
            flag_apply_color = NULL;
        }
        else if (strcmp(flag_apply_color, "always") != 0 && strcmp(flag_apply_color, "yes") != 0 && strcmp(flag_apply_color, "force") != 0)
        {
          usage("invalid argument --color=WHEN, valid arguments are 'never', 'always' and 'auto'");
        }

        if (flag_apply_color != NULL)
        {
          // get GREP_COLOR and GREP_COLORS, when defined
          char *env_grep_color = NULL;
          dupenv_s(&env_grep_color, "GREP_COLOR");
          char *env_grep_colors = NULL;
          dupenv_s(&env_grep_colors, "GREP_COLORS");
          const char *grep_colors = env_grep_colors;

          // if GREP_COLOR is defined but not GREP_COLORS, use it to set mt= default value (overridden by GREP_COLORS mt=, ms=, mc=)
          if (env_grep_colors == NULL && env_grep_color != NULL)
            set_color(std::string("mt=").append(env_grep_color).c_str(), "mt=", color_mt);
          else if (grep_colors == NULL)
            grep_colors = DEFAULT_GREP_COLORS;

          // parse GREP_COLORS
          set_color(grep_colors, "sl=", color_sl); // selected line
          set_color(grep_colors, "cx=", color_cx); // context line
          set_color(grep_colors, "mt=", color_mt); // matched text in any line
          set_color(grep_colors, "ms=", color_ms); // matched text in selected line
          set_color(grep_colors, "mc=", color_mc); // matched text in a context line
          set_color(grep_colors, "fn=", color_fn); // file name
          set_color(grep_colors, "ln=", color_ln); // line number
          set_color(grep_colors, "cn=", color_cn); // column number
          set_color(grep_colors, "bn=", color_bn); // byte offset
          set_color(grep_colors, "se=", color_se); // separator

          // parse --colors to override GREP_COLORS
          set_color(flag_colors, "sl=", color_sl); // selected line
          set_color(flag_colors, "cx=", color_cx); // context line
          set_color(flag_colors, "mt=", color_mt); // matched text in any line
          set_color(flag_colors, "ms=", color_ms); // matched text in selected line
          set_color(flag_colors, "mc=", color_mc); // matched text in a context line
          set_color(flag_colors, "fn=", color_fn); // file name
          set_color(flag_colors, "ln=", color_ln); // line number
          set_color(flag_colors, "cn=", color_cn); // column number
          set_color(flag_colors, "bn=", color_bn); // byte offset
          set_color(flag_colors, "se=", color_se); // separator

          // -v: if rv in GREP_COLORS then swap the sl and cx colors (note that rv does not match color letters)
          if (flag_invert_match &&
              ((grep_colors != NULL && strstr(grep_colors, "rv") != NULL) ||
               (flag_colors != NULL && strstr(flag_colors, "rv") != NULL)))
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

          // if OSC hyperlinks are OK (note that "hl" does not match color letters so strstr can be used)
          if ((grep_colors != NULL && strstr(grep_colors, "hl") != NULL) || (flag_colors != NULL && strstr(flag_colors, "hl") != NULL))
          {
            char *cwd = getcwd0();
            if (cwd != NULL)
            {
              char *path = cwd;
              if (*path == PATHSEPCHR)
                ++path;
              color_wd.assign("file://localhost").append(PATHSEPSTR).append(path).push_back(PATHSEPCHR);
              free(cwd);
              color_hl = "\033]8;;";
              color_st = "\033\\";
            }
          }

          // if CSI erase line is OK (note that ne does not match color letters so strstr can be used)
          if ((grep_colors == NULL || strstr(grep_colors, "ne") == NULL) && (flag_colors == NULL || strstr(flag_colors, "ne") == NULL))
            color_del = "\033[K";

          color_off = "\033[m";

          copy_color(match_off, color_off);

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
}

// search the specified files, directories, and/or standard input for pattern matches
void ugrep()
{
  // reset warnings
  warnings = 0;

  // reset stats
  Stats::reset();

  // populate the combined all-include and all-exclude
  flag_all_include = flag_include;
  flag_all_include_dir = flag_include_dir;
  flag_all_exclude = flag_exclude;
  flag_all_exclude_dir = flag_exclude_dir;

  // -g, --glob: add globs to all-include/all-exclude
  for (const auto& globs : flag_glob)
  {
    size_t from = 0;
    std::string glob;

    while (true)
    {
      size_t to = globs.find(',', from);
      size_t size = (to == std::string::npos ? globs.size() : to) - from;

      if (size > 0)
      {
        bool negate = size > 1 && (globs[from] == '!' || globs[from] == '^');

        if (negate)
        {
          ++from;
          --size;
        }

        (negate ? flag_all_exclude : flag_all_include).emplace_back(globs.substr(from, size));
      }

      if (to == std::string::npos)
        break;

      from = to + 1;
    }
  }

  // all excluded files: normalize by moving directory globs (globs ending in a path separator /) to --exclude-dir
  auto i = flag_all_exclude.begin();
  while (i != flag_all_exclude.end())
  {
    if (i->empty())
    {
      i = flag_all_exclude.erase(i);
    }
    else if (i->back() == '/')
    {
      flag_all_exclude_dir.emplace_back(*i);
      i = flag_all_exclude.erase(i);
    }
    else
    {
      ++i;
    }
  }

  // all included files: normalize by moving directory globs (globs ending in a path separator /) to --include-dir
  i = flag_all_include.begin();
  while (i != flag_all_include.end())
  {
    if (i->empty())
    {
      i = flag_all_include.erase(i);
    }
    else
    {
      if (i->back() == '/')
      {
        flag_all_include_dir.emplace_back(*i);
        i = flag_all_include.erase(i);
      }
      else
      {
        // if an include file glob starts with a dot, then enable searching hidden files and directories
        if (i->front() == '.' || i->find(PATHSEPSTR ".") != std::string::npos)
          flag_hidden = true;

        ++i;
      }
    }
  }

  // if an include dir glob starts with a dot, then enable searching hidden files and directories
  if (!flag_hidden)
  {
    for (const auto& dir : flag_all_include_dir)
    {
      if (dir.front() == '.' || dir.find(PATHSEPSTR ".") != std::string::npos)
      {
        flag_hidden = true;
        break;
      }
    }
  }

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
  // -z with -M or -O/--include: add globs to search archive contents
  if (flag_decompress && (!flag_file_magic.empty() || !flag_all_include.empty()))
  {
    flag_all_include.emplace_back("*.cpio");
    flag_all_include.emplace_back("*.pax");
    flag_all_include.emplace_back("*.tar");
    flag_all_include.emplace_back("*.zip");
    flag_all_include.emplace_back("*.zipx");
    flag_all_include.emplace_back("*.ZIP");

    flag_all_include.emplace_back("*.cpio.gz");
    flag_all_include.emplace_back("*.pax.gz");
    flag_all_include.emplace_back("*.tar.gz");
    flag_all_include.emplace_back("*.taz");
    flag_all_include.emplace_back("*.tgz");
    flag_all_include.emplace_back("*.tpz");

    flag_all_include.emplace_back("*.cpio.Z");
    flag_all_include.emplace_back("*.pax.Z");
    flag_all_include.emplace_back("*.tar.Z");

    flag_all_include.emplace_back("*.cpio.zip");
    flag_all_include.emplace_back("*.pax.zip");
    flag_all_include.emplace_back("*.tar.zip");

#ifdef HAVE_LIBBZ2
    flag_all_include.emplace_back("*.cpio.bz");
    flag_all_include.emplace_back("*.pax.bz");
    flag_all_include.emplace_back("*.tar.bz");
    flag_all_include.emplace_back("*.cpio.bz2");
    flag_all_include.emplace_back("*.pax.bz2");
    flag_all_include.emplace_back("*.tar.bz2");
    flag_all_include.emplace_back("*.cpio.bzip2");
    flag_all_include.emplace_back("*.pax.bzip2");
    flag_all_include.emplace_back("*.tar.bzip2");
    flag_all_include.emplace_back("*.tb2");
    flag_all_include.emplace_back("*.tbz");
    flag_all_include.emplace_back("*.tbz2");
    flag_all_include.emplace_back("*.tz2");
#endif

#ifdef HAVE_LIBLZMA
    flag_all_include.emplace_back("*.cpio.lzma");
    flag_all_include.emplace_back("*.pax.lzma");
    flag_all_include.emplace_back("*.tar.lzma");
    flag_all_include.emplace_back("*.cpio.xz");
    flag_all_include.emplace_back("*.pax.xz");
    flag_all_include.emplace_back("*.tar.xz");
    flag_all_include.emplace_back("*.tlz");
    flag_all_include.emplace_back("*.txz");
#endif

#ifdef HAVE_LIBLZ4
    flag_all_include.emplace_back("*.cpio.lz4");
    flag_all_include.emplace_back("*.pax.lz4");
    flag_all_include.emplace_back("*.tar.lz4");
#endif

#ifdef HAVE_LIBZSTD
    flag_all_include.emplace_back("*.cpio.zst");
    flag_all_include.emplace_back("*.pax.zst");
    flag_all_include.emplace_back("*.tar.zst");
    flag_all_include.emplace_back("*.cpio.zstd");
    flag_all_include.emplace_back("*.pax.zstd");
    flag_all_include.emplace_back("*.tar.zstd");
    flag_all_include.emplace_back("*.tzst");
#endif
  }
#endif
#endif

  // all excluded-dirs: normalize by removing trailing path separators
  for (auto& i : flag_all_exclude_dir)
    while (i.size() > 1 && i.back() == '/')
      i.pop_back();

  // all included-dirs: normalize by removing trailing path separators
  for (auto& i : flag_all_include_dir)
    while (i.size() > 1 && i.back() == '/')
      i.pop_back();

  // --sort: check sort KEY and set flags
  if (flag_sort != NULL)
  {
    flag_sort_rev = *flag_sort == 'r' || *flag_sort == '^';

    const char *sort_by = flag_sort + flag_sort_rev;

    if (strcmp(sort_by, "name") == 0)
      flag_sort_key = Sort::NAME;
    else if (strcmp(sort_by, "best") == 0)
      flag_sort_key = Sort::BEST;
    else if (strcmp(sort_by, "size") == 0)
      flag_sort_key = Sort::SIZE;
    else if (strcmp(sort_by, "used") == 0)
      flag_sort_key = Sort::USED;
    else if (strcmp(sort_by, "changed") == 0)
      flag_sort_key = Sort::CHANGED;
    else if (strcmp(sort_by, "created") == 0)
      flag_sort_key = Sort::CREATED;
    else if (strcmp(sort_by, "list") == 0)
      flag_sort_key = Sort::LIST;
    else
      usage("invalid argument --sort=KEY, valid arguments are 'name', 'best', 'size', 'used', 'changed', 'created', 'list', 'rname', 'rbest', 'rsize', 'rused', 'rchanged', 'rcreated' and 'rlist'");
  }

  // add PATTERN to the CNF
  if (arg_pattern != NULL)
    bcnf.new_pattern(CNF::PATTERN::NA, arg_pattern);

  // the regex compiled from PATTERN, -e PATTERN, -N PATTERN, and -f FILE
  std::string regex;

  if (bcnf.defined())
  {
    // prune empty terms from the CNF that match anything
    bcnf.prune();

    // split the patterns at newlines, standard grep behavior
    bcnf.split();

    if (flag_file.empty())
    {
      // the CNF patterns to search, this matches more than necessary to support multiline matching and to highlight all matches in color
      regex.assign(bcnf.adjoin());

      // an empty pattern specified matches every line, including empty lines
      if (regex.empty())
      {
        if (flag_count)
        {
          // regex optimized for speed, but match count needs adjustment later when last line has no \n
          regex = "\\n";
        }
        else if (!flag_quiet && !flag_files_with_matches && !flag_files_without_match)
        {
          if (flag_hex)
            regex = ".*\\n?";
          else
            regex = "^.*"; // use ^.* to prevent -o from reporting an extra empty match
        }

        // an empty pattern matches every line
        flag_empty = true;
        flag_dotall = false;
      }

      // CNF is empty if all patterns are empty, i.e. match anything unless -f FILE specified
      if (bcnf.empty())
      {
        // match every line
        flag_match = true;
        flag_dotall = false;
      }
    }
    else
    {
      // -f FILE is combined with -e, --and, --andnot, --not

      if (bcnf.first_empty())
      {
        if (flag_count)
        {
          // regex optimized for speed, but match count needs adjustment later when last line has no \n
          regex = "\\n";
        }
        else if (!flag_quiet && !flag_files_with_matches && !flag_files_without_match)
        {
          if (flag_hex)
            regex = ".*\\n?";
          else
            regex = "^.*"; // use ^.* to prevent -o from reporting an extra empty match
        }

        // an empty pattern specified with -e '' matches every line
        flag_empty = true;
      }
      else
      {
        // for efficiency, take only the first CNF OR-list terms to search in combination with -f FILE patterns
        regex.assign(bcnf.first());
      }
    }
  }

  // -x or --match: enable -Y and disable --dotall and -w
  if (flag_line_regexp || flag_match)
  {
    flag_empty = true;
    flag_dotall = false;
    flag_word_regexp = false;
  }

  // -f: get patterns from file
  if (!flag_file.empty())
  {
    bool line_regexp = flag_line_regexp;
    bool word_regexp = flag_word_regexp;

    // -F: make newline-separated lines in regex literal with \Q and \E
    const char *Q = flag_fixed_strings ? "\\Q" : "";
    const char *E = flag_fixed_strings ? "\\E|" : flag_basic_regexp ? "\\|" : "|";

    // PATTERN or -e PATTERN: add an ending '|' (or BRE '\|') to the regex to concatenate sub-expressions
    if (!regex.empty())
    {
      // -F does not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
      Q = "";
      E = flag_basic_regexp ? "\\|" : "|";

      // -x and -w do not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
      line_regexp = false;
      word_regexp = false;

      regex.append(E);
    }

    // -f: read patterns from the specified file or files
    for (const auto& filename : flag_file)
    {
      FILE *file = NULL;

      if (fopen_smart(&file, filename.c_str(), "r") != 0)
        file = NULL;

      if (file == NULL)
      {
        // could not open, try GREP_PATH environment variable
        char *env_grep_path = NULL;
        dupenv_s(&env_grep_path, "GREP_PATH");

        if (env_grep_path != NULL)
        {
          if (fopen_smart(&file, std::string(env_grep_path).append(PATHSEPSTR).append(filename).c_str(), "r") != 0)
            file = NULL;

          free(env_grep_path);
        }
      }

#ifdef GREP_PATH
      if (file == NULL)
      {
        if (fopen_smart(&file, std::string(GREP_PATH).append(PATHSEPSTR).append(filename).c_str(), "r") != 0)
          file = NULL;
      }
#endif

      if (file == NULL)
        throw std::runtime_error(std::string("option -f: cannot read ").append(filename)); // to catch in query UI

      reflex::BufferedInput input(file);
      std::string line;

      while (true)
      {
        // read the next line
        if (getline(input, line))
          break;

        // add line to the regex if not empty
        if (!line.empty())
          regex.append(Q).append(line).append(E);
      }

      if (file != stdin)
        fclose(file);
    }

    // pop unused ending '|' (or BRE '\|') from the |-concatenated regexes in the regex string
    regex.pop_back();
    if (flag_basic_regexp)
      regex.pop_back();

    // -G requires \( \) instead of ( ) and -P requires (?<!\w) (?!\w) instead of \< and \>
    const char *xleft = flag_basic_regexp ? "^\\(" : "^(?:";
    const char *xright = flag_basic_regexp ? "\\)$" : ")$";
#if defined(HAVE_PCRE2)
    const char *wleft = flag_basic_regexp ? "\\<\\(" : flag_perl_regexp ? "(?<!\\w)(?:" : "\\<(";
    const char *wright = flag_basic_regexp ? "\\)\\>" : flag_perl_regexp ? ")(?!\\w)" : ")\\>";
#else // Boost.Regex
    const char *wleft = flag_basic_regexp ? "\\<\\(" : flag_perl_regexp ? "(?<![[:word:]])(?:" : "\\<(";
    const char *wright = flag_basic_regexp ? "\\)\\>" : flag_perl_regexp ? ")(?![[:word:]])" : ")\\>";
#endif

    // -x or -w: if no PATTERN is specified, then apply -x or -w to -f FILE patterns
    if (line_regexp)
      regex.insert(0, xleft).append(xright); // make the regex line-anchored
    else if (word_regexp)
      regex.insert(0, wleft).append(wright); // make the regex word-anchored
  }

  // --match: adjust color highlighting to show matches as selected lines without color
  if (flag_match)
  {
    copy_color(match_ms, color_sl);
    copy_color(match_mc, color_cx);
    copy_color(match_off, color_off);
  }
  else
  {
    // --tag: output tagged matches instead of colors
    if (flag_tag != NULL)
    {
      const char *s1 = strchr(flag_tag, ',');
      const char *s2 = s1 != NULL ? strchr(s1 + 1, ',') : NULL;

      copy_color(match_ms, flag_tag);

      if (s1 == NULL)
      {
        copy_color(match_mc, flag_tag);
        copy_color(match_off, flag_tag);
      }
      else
      {
        copy_color(match_off, s1 + 1);

        if (s2 == NULL)
          copy_color(match_mc, match_ms);
        else
          copy_color(match_mc, s2 + 1);
      }
    }
    else
    {
      copy_color(match_ms, color_ms);
      copy_color(match_mc, color_mc);
      copy_color(match_off, color_off);
    }
  }

  // -j: case insensitive search if regex does not contain an upper case letter
  if (flag_smart_case)
  {
    flag_ignore_case = true;

    for (size_t i = 0; i < regex.size(); ++i)
    {
      if (regex[i] == '\\')
      {
        ++i;
      }
      else if (regex[i] == '{')
      {
        while (++i < regex.size() && regex[i] != '}')
          continue;
      }
      else if (isupper(regex[i]))
      {
        flag_ignore_case = false;
        break;
      }
    }
  }

  // -y: disable -A, -B and -C
  if (flag_any_line)
    flag_after_context = flag_before_context = 0;

  // -v or -y: disable -o and -u
  if (flag_invert_match || flag_any_line)
    flag_only_matching = flag_ungroup = false;

  // --depth: if -R or -r is not specified then enable -r
  if ((flag_min_depth > 0 || flag_max_depth > 0) && flag_directories_action == Action::UNSP)
    flag_directories_action = Action::RECURSE;

  // -p (--no-dereference) and -S (--dereference): -p takes priority over -S and -R
  if (flag_no_dereference)
    flag_dereference = false;

  // display file name if more than one input file is specified or options -R, -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (flag_all_threads || flag_directories_action == Action::RECURSE || arg_files.size() > 1 || (flag_stdin && !arg_files.empty())))
    flag_with_filename = true;

  // if no display options -H, -n, -k, -b are set, enable --no-header to suppress headers for speed
  if (!flag_with_filename && !flag_line_number && !flag_column_number && !flag_byte_offset)
    flag_no_header = true;

  // -q: we only need to find one matching file and we're done
  if (flag_quiet)
  {
    flag_max_files = 1;

    // -q overrides -l and -L
    flag_files_with_matches = false;
    flag_files_without_match = false;

    // disable --format options
    flag_format_begin = NULL;
    flag_format_open = NULL;
    flag_format = NULL;
    flag_format_close = NULL;
    flag_format_end = NULL;
  }

  // -L: enable -l and flip -v i.e. -L=-lv and -l=-Lv
  if (flag_files_without_match)
  {
    flag_files_with_matches = true;
    flag_invert_match = !flag_invert_match;
  }

  // -l or -L: enable -H, disable -c
  if (flag_files_with_matches)
  {
    flag_with_filename = true;
    flag_count = false;
  }

  // --heading: enable --break when filenames are shown
  if (flag_heading && flag_with_filename)
    flag_break = true;

  // -J: when not set the default is the number of cores (or hardware threads), limited to MAX_JOBS
  if (flag_jobs == 0)
  {
    unsigned int cores = std::thread::hardware_concurrency();
    unsigned int concurrency = cores > 2 ? cores : 2;
    // reduce concurrency by one for 8+ core CPUs
    concurrency -= concurrency / 9;
    flag_jobs = std::min(concurrency, MAX_JOBS);
  }

  // --sort and --max-files: limit number of threads to --max-files to prevent unordered results, this is a special case
  if (flag_sort_key != Sort::NA && flag_max_files > 0)
    flag_jobs = std::min(flag_jobs, flag_max_files);

  // set the number of threads to the number of files or when recursing to the value of -J, --jobs
  if (flag_all_threads || flag_directories_action == Action::RECURSE)
    threads = flag_jobs;
  else
    threads = std::min(arg_files.size() + flag_stdin, flag_jobs);

  // inverted character classes and \s do not match newlines, e.g. [^x] matches anything except x and \n
  reflex::convert_flag_type convert_flags = reflex::convert_flag::notnewline;

  // not -U: convert regex to Unicode
  if (!flag_binary)
    convert_flags |= reflex::convert_flag::unicode;

  // -G: convert basic regex (BRE) to extended regex (ERE)
  if (flag_basic_regexp)
    convert_flags |= reflex::convert_flag::basic;

  // set reflex::Pattern options to enable multiline mode
  std::string pattern_options("(?m");

  // -i: case insensitive reflex::Pattern option, applies to ASCII only
  if (flag_ignore_case)
    pattern_options.push_back('i');

  // --dotall and not --match (or empty pattern): dot matches newline
  if (flag_dotall)
    pattern_options.push_back('s');

  // --free-space: convert_flags is needed to check free-space conformance by the converter
  if (flag_free_space)
  {
    convert_flags |= reflex::convert_flag::freespace;
    pattern_options.push_back('x');
  }

  // prepend the pattern options (?m...) to the regex
  pattern_options.push_back(')');
  regex.insert(0, pattern_options);

  // reflex::Matcher options
  std::string matcher_options;

  // -Y: permit empty pattern matches
  if (flag_empty)
    matcher_options.push_back('N');

  // -w: match whole words, i.e. make \< and \> match only left side and right side, respectively
  if (flag_word_regexp)
    matcher_options.push_back('W');

  // --tabs: set reflex::Matcher option T to NUM (1, 2, 4, or 8) tab size
  if (flag_tabs)
    matcher_options.append("T=").push_back(static_cast<char>(flag_tabs) + '0');

  // --format-begin
  if (flag_format_begin != NULL)
    format(flag_format_begin, 0);

  size_t nodes = 0;
  size_t edges = 0;
  size_t words = 0;
  size_t nodes_time = 0;
  size_t edges_time = 0;
  size_t words_time = 0;

  // -P: Perl matching with PCRE2 or Boost.Regex
  if (flag_perl_regexp)
  {
#if defined(HAVE_PCRE2)
    // construct the PCRE2 JIT-optimized NFA-based Perl pattern matcher
    std::string pattern(flag_binary ? reflex::PCRE2Matcher::convert(regex, convert_flags) : reflex::PCRE2UTFMatcher::convert(regex, convert_flags));
    reflex::PCRE2Matcher matcher(pattern, reflex::Input(), matcher_options.c_str(), flag_binary ? (PCRE2_NEVER_UTF | PCRE2_NEVER_UCP) : (PCRE2_UTF | PCRE2_UCP));
    Grep::Matchers matchers;

    if (!bcnf.singleton_or_undefined())
    {
      std::string subregex;

      for (const auto& i : bcnf.lists())
      {
        matchers.emplace_back();

        auto& submatchers = matchers.back();

        for (const auto& j : i)
        {
          if (j)
          {
            subregex.assign(pattern_options).append(*j);
            submatchers.emplace_back(new reflex::PCRE2Matcher((flag_binary ? reflex::PCRE2Matcher::convert(subregex, convert_flags) : reflex::PCRE2UTFMatcher::convert(subregex, convert_flags)), reflex::Input(), matcher_options.c_str(), flag_binary ? (PCRE2_NEVER_UTF | PCRE2_NEVER_UCP) : (PCRE2_UTF | PCRE2_UCP)));
          }
          else
          {
            submatchers.emplace_back();
          }
        }
      }
    }

    if (threads > 1)
    {
      GrepMaster grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
      grep.ugrep();
    }
    else
    {
      Grep grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
      set_grep_handle(&grep);
      grep.ugrep();
      clear_grep_handle();
    }
#elif defined(HAVE_BOOST_REGEX)
    std::string pattern;
    try
    {
      // construct the Boost.Regex NFA-based Perl pattern matcher
      pattern.assign(reflex::BoostPerlMatcher::convert(regex, convert_flags));
      reflex::BoostPerlMatcher matcher(pattern, reflex::Input(), matcher_options.c_str());
      Grep::Matchers matchers;

      if (!bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : bcnf.lists())
        {
          matchers.emplace_back();

          auto& submatchers = matchers.back();

          for (const auto& j : i)
          {
            if (j)
            {
              subregex.assign(pattern_options).append(*j);
              submatchers.emplace_back(new reflex::BoostPerlMatcher(reflex::BoostPerlMatcher::convert(subregex, convert_flags), reflex::Input(), matcher_options.c_str()));
            }
            else
            {
              submatchers.emplace_back();
            }
          }
        }
      }

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        set_grep_handle(&grep);
        grep.ugrep();
        clear_grep_handle();
      }
    }

    catch (boost::regex_error& error)
    {
      reflex::regex_error_type code;

      switch (error.code())
      {
        case boost::regex_constants::error_collate:
          code = reflex::regex_error::invalid_collating;
          break;
        case boost::regex_constants::error_ctype:
          code = reflex::regex_error::invalid_class;
          break;
        case boost::regex_constants::error_escape:
          code = reflex::regex_error::invalid_escape;
          break;
        case boost::regex_constants::error_backref:
          code = reflex::regex_error::invalid_backreference;
          break;
        case boost::regex_constants::error_brack:
          code = reflex::regex_error::invalid_class;
          break;
        case boost::regex_constants::error_paren:
          code = reflex::regex_error::mismatched_parens;
          break;
        case boost::regex_constants::error_brace:
          code = reflex::regex_error::mismatched_braces;
          break;
        case boost::regex_constants::error_badbrace:
          code = reflex::regex_error::invalid_repeat;
          break;
        case boost::regex_constants::error_range:
          code = reflex::regex_error::invalid_class_range;
          break;
        case boost::regex_constants::error_space:
          code = reflex::regex_error::exceeds_limits;
          break;
        case boost::regex_constants::error_badrepeat:
          code = reflex::regex_error::invalid_repeat;
          break;
        case boost::regex_constants::error_complexity:
          code = reflex::regex_error::exceeds_limits;
          break;
        case boost::regex_constants::error_stack:
          code = reflex::regex_error::exceeds_limits;
          break;
        default:
          code = reflex::regex_error::invalid_syntax;
      }

      throw reflex::regex_error(code, pattern, error.position() + 1);
    }
#endif
  }
  else
  {
    // construct the RE/flex DFA-based pattern matcher and start matching files
    reflex::Pattern pattern(reflex::Matcher::convert(regex, convert_flags), "r");
    std::list<reflex::Pattern> patterns;
    Grep::Matchers matchers;

    if (flag_fuzzy > 0)
    {
      // -U: disable fuzzy Unicode matching, ASCII/binary only with -Z MAX edit distance
      uint16_t max = static_cast<uint16_t>(flag_fuzzy) | (flag_binary ? reflex::FuzzyMatcher::BIN : 0);
      reflex::FuzzyMatcher matcher(pattern, max, reflex::Input(), matcher_options.c_str());

      if (!bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : bcnf.lists())
        {
          matchers.emplace_back();

          auto& submatchers = matchers.back();

          for (const auto& j : i)
          {
            if (j)
            {
              subregex.assign(pattern_options).append(*j);
              patterns.emplace_back(reflex::FuzzyMatcher::convert(subregex, convert_flags), "r");
              submatchers.emplace_back(new reflex::FuzzyMatcher(patterns.back(), reflex::Input(), matcher_options.c_str()));
            }
            else
            {
              submatchers.emplace_back();
            }
          }
        }
      }

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        set_grep_handle(&grep);
        grep.ugrep();
        clear_grep_handle();
      }
    }
    else
    {
      reflex::Matcher matcher(pattern, reflex::Input(), matcher_options.c_str());

      if (!bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : bcnf.lists())
        {
          matchers.emplace_back();

          auto& submatchers = matchers.back();

          for (const auto& j : i)
          {
            if (j)
            {
              subregex.assign(pattern_options).append(*j);
              patterns.emplace_back(reflex::Matcher::convert(subregex, convert_flags), "r");
              submatchers.emplace_back(new reflex::Matcher(patterns.back(), reflex::Input(), matcher_options.c_str()));
            }
            else
            {
              submatchers.emplace_back();
            }
          }
        }
      }

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher, bcnf.singleton_or_undefined() ? NULL : &matchers);
        set_grep_handle(&grep);
        grep.ugrep();
        clear_grep_handle();
      }
    }

    nodes = pattern.nodes();
    edges = pattern.edges();
    words = pattern.words();
    nodes_time = static_cast<size_t>(pattern.nodes_time());
    edges_time = static_cast<size_t>(pattern.parse_time() + pattern.edges_time());
    words_time = static_cast<size_t>(pattern.words_time());
  }

  // --format-end
  if (flag_format_end != NULL)
    format(flag_format_end, Stats::found_parts());

  // --stats: display stats when we're done
  if (flag_stats != NULL)
  {
    Stats::report(output);

    bcnf.report(output);

    if (strcmp(flag_stats, "vm") == 0 && words > 0)
      fprintf(output, "VM: %zu nodes (%zums), %zu edges (%zums), %zu opcode words (%zums)" NEWLINESTR, nodes, nodes_time, edges, edges_time, words, words_time);
  }

  // close the pipe to the forked pager
  if (flag_pager != NULL && output != NULL && output != stdout)
    pclose(output);
}

// cancel the search
void cancel_ugrep()
{
  std::unique_lock<std::mutex> lock(grep_handle_mutex);
  if (grep_handle != NULL)
    grep_handle->cancel();
}

// set the handle to be able to use cancel_ugrep()
void set_grep_handle(Grep *grep)
{
  std::unique_lock<std::mutex> lock(grep_handle_mutex);
  grep_handle = grep;
}

// reset the grep handle
void clear_grep_handle()
{
  std::unique_lock<std::mutex> lock(grep_handle_mutex);
  grep_handle = NULL;
}

// search the specified files or standard input for pattern matches
void Grep::ugrep()
{
  // read each input file to find pattern matches
  if (flag_stdin)
  {
    Stats::score_file();

    // search standard input
    search(LABEL_STANDARD_INPUT, static_cast<uint16_t>(flag_fuzzy));
  }

  if (arg_files.empty())
  {
    if (flag_directories_action == Action::RECURSE)
      recurse(1, ".");
  }
  else
  {
#ifndef OS_WIN
    std::pair<std::set<ino_t>::iterator,bool> vino;
#endif

    for (const auto pathname : arg_files)
    {
      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof || out.cancelled())
        break;

      // search file or directory, get the basename from the file argument first
      const char *basename = strrchr(pathname, PATHSEPCHR);
      if (basename != NULL)
        ++basename;
      else
        basename = pathname;

      ino_t inode = 0;
      uint64_t info;

      // search file or recursively search directory based on selection criteria
      switch (select(1, pathname, basename, DIRENT_TYPE_UNKNOWN, inode, info, true))
      {
        case Type::DIRECTORY:
          if (flag_directories_action != Action::SKIP)
          {
#ifndef OS_WIN
            if (flag_dereference)
              vino = visited.insert(inode);
#endif

            recurse(1, pathname);

#ifndef OS_WIN
            if (flag_dereference)
              visited.erase(vino.first);
#endif
          }
          break;

        case Type::OTHER:
          search(pathname, Entry::UNDEFINED_COST);
          break;

        case Type::SKIP:
          break;
      }
    }
  }
}

// select file or directory to search for pattern matches, return SKIP, DIRECTORY or OTHER
Grep::Type Grep::select(size_t level, const char *pathname, const char *basename, int type, ino_t& inode, uint64_t& info, bool is_argument)
{
  if (*basename == '.' && !flag_hidden && !is_argument)
    return Type::SKIP;

#ifdef OS_WIN

  DWORD attr = GetFileAttributesW(utf8_decode(pathname).c_str());

  if (attr == INVALID_FILE_ATTRIBUTES)
  {
    errno = ENOENT;
    warning("cannot read", pathname);
    return Type::SKIP;
  }

  if (!flag_hidden && !is_argument && ((attr & FILE_ATTRIBUTE_HIDDEN) || (attr & FILE_ATTRIBUTE_SYSTEM)))
    return Type::SKIP;

  if ((attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    if (flag_directories_action == Action::READ)
    {
      // directories cannot be read actually, so grep produces a warning message (errno is not set)
      is_directory(pathname);
      return Type::SKIP;
    }

    if (is_argument || flag_directories_action == Action::RECURSE)
    {
      // --depth: recursion level exceeds max depth?
      if (flag_max_depth > 0 && level > flag_max_depth)
        return Type::SKIP;

      // hard maximum recursion depth reached?
      if (level > MAX_DEPTH)
      {
        if (!flag_no_messages)
          fprintf(stderr, "%sugrep: %s%s%s recursion depth hit hard limit of %d\n", color_off, color_high, pathname, color_off, MAX_DEPTH);
        return Type::SKIP;
      }

      // check for --exclude-dir and --include-dir constraints if pathname != "."
      if (strcmp(pathname, ".") != 0)
      {
        if (!flag_all_exclude_dir.empty())
        {
          // exclude directories whose pathname matches any one of the --exclude-dir globs unless negated with !
          bool ok = true;
          for (const auto& glob : flag_all_exclude_dir)
          {
            if (glob.front() == '!')
            {
              if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
                ok = true;
            }
            else if (ok && glob_match(pathname, basename, glob.c_str()))
            {
              ok = false;
            }
          }
          if (!ok)
            return Type::SKIP;
        }

        if (!flag_all_include_dir.empty())
        {
          // include directories whose pathname matches any one of the --include-dir globs unless negated with !
          bool ok = false;
          for (const auto& glob : flag_all_include_dir)
          {
            if (glob.front() == '!')
            {
              if (ok && glob_match(pathname, basename, glob.c_str() + 1))
                ok = false;
            }
            else if (!ok && glob_match(pathname, basename, glob.c_str()))
            {
              ok = true;
            }
          }
          if (!ok)
            return Type::SKIP;
        }
      }

      return Type::DIRECTORY;
    }
  }
  else if ((attr & FILE_ATTRIBUTE_DEVICE) == 0 || flag_devices_action == Action::READ)
  {
    // --depth: recursion level not deep enough?
    if (flag_min_depth > 0 && level <= flag_min_depth)
      return Type::SKIP;

    if (!flag_all_exclude.empty())
    {
      // exclude files whose pathname matches any one of the --exclude globs unless negated with !
      bool ok = true;
      for (const auto& glob : flag_all_exclude)
      {
        if (glob.front() == '!')
        {
          if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
            ok = true;
        }
        else if (ok && glob_match(pathname, basename, glob.c_str()))
        {
          ok = false;
        }
      }
      if (!ok)
        return Type::SKIP;
    }

    // check magic pattern against the file signature, when --file-magic=MAGIC is specified
    if (!flag_file_magic.empty())
    {
      FILE *file;

      if (fopenw_s(&file, pathname, "rb") != 0)
      {
        warning("cannot read", pathname);
        return Type::SKIP;
      }

#ifdef HAVE_LIBZ
      if (flag_decompress)
      {
        zstreambuf streambuf(pathname, file);
        std::istream stream(&streambuf);

        // file has the magic bytes we're looking for: search the file
        size_t match = magic_matcher.input(&stream).scan();
        if (match == flag_not_magic || match >= flag_min_magic)
        {
          fclose(file);

          Stats::score_file();

          return Type::OTHER;
        }
      }
      else
#endif
      {
        size_t match = magic_matcher.input(reflex::Input(file, flag_encoding_type)).scan();
        if (match == flag_not_magic || match >= flag_min_magic)
        {
          fclose(file);

          Stats::score_file();

          return Type::OTHER;
        }
      }

      fclose(file);

      if (flag_all_include.empty())
        return Type::SKIP;
    }

    if (!flag_all_include.empty())
    {
      // include files whose pathname matches any one of the --include globs unless negated with !
      bool ok = false;
      for (const auto& glob : flag_all_include)
      {
        if (glob.front() == '!')
        {
          if (ok && glob_match(pathname, basename, glob.c_str() + 1))
            ok = false;
        }
        else if (!ok && glob_match(pathname, basename, glob.c_str()))
        {
          ok = true;
        }
      }
      if (!ok)
        return Type::SKIP;
    }

    Stats::score_file();

    return Type::OTHER;
  }

#else

  struct stat buf;

  // -R or -S or command line argument FILE to search: follow symlinks
  bool follow = flag_dereference || is_argument;

  // if dir entry is unknown and not following, then use lstat() to check if pathname is a symlink
  if (type != DIRENT_TYPE_UNKNOWN || follow || lstat(pathname, &buf) == 0)
  {
    // is it a symlink? If dir entry unknown and following then set to symlink = true to call stat() below
    bool symlink = type != DIRENT_TYPE_UNKNOWN ? type == DIRENT_TYPE_LNK : follow ? true : S_ISLNK(buf.st_mode);
    // if we got a symlink, use stat() to check if pathname is a directory or a regular file, we also stat when following and when sorting by stat info such as modification time
    if (( ( (type != DIRENT_TYPE_UNKNOWN && type != DIRENT_TYPE_LNK) ||         /* type is known and not symlink */
            (!follow && !symlink)                                               /* or not following and not symlink */
          ) &&
          (flag_sort_key == Sort::NA || flag_sort_key == Sort::NAME)            /* and we're not sorting or by name */
        ) ||
        stat(pathname, &buf) == 0)
    {
      // check if directory
      if (type == DIRENT_TYPE_DIR || ((type == DIRENT_TYPE_UNKNOWN || type == DIRENT_TYPE_LNK) && S_ISDIR(buf.st_mode)))
      {
        // if symlinked then follow into directory?
        if (follow || !symlink)
        {
          if (flag_directories_action == Action::READ)
          {
            // directories cannot be read actually, so grep produces a warning message (errno is not set)
            is_directory(pathname);
            return Type::SKIP;
          }

          if (is_argument || flag_directories_action == Action::RECURSE)
          {
            // --depth: recursion level exceeds max depth?
            if (flag_max_depth > 0 && level > flag_max_depth)
              return Type::SKIP;

            // hard maximum recursion depth reached?
            if (level > MAX_DEPTH)
            {
              if (!flag_no_messages)
                fprintf(stderr, "%sugrep: %s%s%s recursion depth hit hard limit of %d\n", color_off, color_high, pathname, color_off, MAX_DEPTH);
              return Type::SKIP;
            }

            // check for --exclude-dir and --include-dir constraints if pathname != "."
            if (strcmp(pathname, ".") != 0)
            {
              if (!flag_all_exclude_dir.empty())
              {
                // exclude directories whose pathname matches any one of the --exclude-dir globs unless negated with !
                bool ok = true;
                for (const auto& glob : flag_all_exclude_dir)
                {
                  if (glob.front() == '!')
                  {
                    if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
                      ok = true;
                  }
                  else if (ok && glob_match(pathname, basename, glob.c_str()))
                  {
                    ok = false;
                  }
                }
                if (!ok)
                  return Type::SKIP;
              }

              if (!flag_all_include_dir.empty())
              {
                // include directories whose pathname matches any one of the --include-dir globs unless negated with !
                bool ok = false;
                for (const auto& glob : flag_all_include_dir)
                {
                  if (glob.front() == '!')
                  {
                    if (ok && glob_match(pathname, basename, glob.c_str() + 1))
                      ok = false;
                  }
                  else if (!ok && glob_match(pathname, basename, glob.c_str()))
                  {
                    ok = true;
                  }
                }
                if (!ok)
                  return Type::SKIP;
              }
            }

            if (type != DIRENT_TYPE_DIR)
              inode = buf.st_ino;

            info = Entry::sort_info(buf);

            return Type::DIRECTORY;
          }
        }
      }
      else if (type == DIRENT_TYPE_REG ? !is_output(inode) : (type == DIRENT_TYPE_UNKNOWN || type == DIRENT_TYPE_LNK) && S_ISREG(buf.st_mode) ? !is_output(buf.st_ino) : flag_devices_action == Action::READ)
      {
        // if not -p or if follow or if not symlinked then search file
        if (!flag_no_dereference || follow || !symlink)
        {
          // --depth: recursion level not deep enough?
          if (flag_min_depth > 0 && level <= flag_min_depth)
            return Type::SKIP;

          if (!flag_all_exclude.empty())
          {
            // exclude files whose pathname matches any one of the --exclude globs unless negated with !
            bool ok = true;
            for (const auto& glob : flag_all_exclude)
            {
              if (glob.front() == '!')
              {
                if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
                  ok = true;
              }
              else if (ok && glob_match(pathname, basename, glob.c_str()))
              {
                ok = false;
              }
            }
            if (!ok)
              return Type::SKIP;
          }

          // check magic pattern against the file signature, when --file-magic=MAGIC is specified
          if (!flag_file_magic.empty())
          {
            FILE *file;

            if (fopenw_s(&file, pathname, "rb") != 0)
            {
              warning("cannot read", pathname);
              return Type::SKIP;
            }

#ifdef HAVE_LIBZ
            if (flag_decompress)
            {
              zstreambuf streambuf(pathname, file);
              std::istream stream(&streambuf);

              // file has the magic bytes we're looking for: search the file
              size_t match = magic_matcher.input(&stream).scan();
              if (match == flag_not_magic || match >= flag_min_magic)
              {
                fclose(file);

                Stats::score_file();

                info = Entry::sort_info(buf);

                return Type::OTHER;
              }
            }
            else
#endif
            {
              // if file has the magic bytes we're looking for: search the file
              size_t match = magic_matcher.input(reflex::Input(file, flag_encoding_type)).scan();
              if (match == flag_not_magic || match >= flag_min_magic)
              {
                fclose(file);

                Stats::score_file();

                info = Entry::sort_info(buf);

                return Type::OTHER;
              }
            }

            fclose(file);

            if (flag_all_include.empty())
              return Type::SKIP;
          }

          if (!flag_all_include.empty())
          {
            // include directories whose basename matches any one of the --include-dir globs if not negated with !
            bool ok = false;
            for (const auto& glob : flag_all_include)
            {
              if (glob.front() == '!')
              {
                if (ok && glob_match(pathname, basename, glob.c_str() + 1))
                  ok = false;
              }
              else if (!ok && glob_match(pathname, basename, glob.c_str()))
              {
                ok = true;
              }
            }
            if (!ok)
              return Type::SKIP;
          }

          Stats::score_file();

          info = Entry::sort_info(buf);

          return Type::OTHER;
        }
      }
    }
  }
  else
  {
    warning("lstat", pathname);
  }

#endif

  return Type::SKIP;
}

// recurse over directory, searching for pattern matches in files and subdirectories
void Grep::recurse(size_t level, const char *pathname)
{
  // output is closed or cancelled?
  if (out.eof || out.cancelled())
    return;

#ifdef OS_WIN

  WIN32_FIND_DATAW ffd;

  std::string glob;

  if (strcmp(pathname, ".") != 0)
    glob.assign(pathname).append("/*");
  else
    glob.assign("*");

  std::wstring wglob = utf8_decode(glob);
  HANDLE hFind = FindFirstFileW(wglob.c_str(), &ffd);

  if (hFind == INVALID_HANDLE_VALUE)
  {
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      warning("cannot open directory", pathname);
    return;
  }

#else

#ifdef HAVE_STATVFS

  if (!exclude_fs_ids.empty() || !include_fs_ids.empty())
  {
    struct statvfs buf;

    if (statvfs(pathname, &buf) == 0)
    {
      uint64_t id = static_cast<uint64_t>(buf.f_fsid);

      if (exclude_fs_ids.find(id) != exclude_fs_ids.end())
        return;

      if (!include_fs_ids.empty() && include_fs_ids.find(id) == include_fs_ids.end())
        return;
    }
  }

#endif

  DIR *dir = opendir(pathname);

  if (dir == NULL)
  {
    warning("cannot open directory", pathname);
    return;
  }

#endif

  // --ignore-files: check if one or more are present to read and extend the file and dir exclusions
  std::vector<std::string> save_all_exclude, save_all_exclude_dir;
  bool saved = false;

  if (!flag_ignore_files.empty())
  {
    std::string filename;

    for (const auto& i : flag_ignore_files)
    {
      filename.assign(pathname).append(PATHSEPSTR).append(i);

      FILE *file = NULL;
      if (fopenw_s(&file, filename.c_str(), "r") == 0)
      {
        if (!saved)
        {
          save_all_exclude = flag_all_exclude;
          save_all_exclude_dir = flag_all_exclude_dir;
          saved = true;
        }

        Stats::ignore_file(filename);
        import_globs(file, flag_all_exclude, flag_all_exclude_dir);
        fclose(file);
      }
    }
  }

  Stats::score_dir();

  std::vector<Entry> file_entries;
  std::vector<Entry> dir_entries;
  std::string dirpathname;

#ifdef OS_WIN

  std::string cFileName;
  uint64_t index = 0;

  do
  {
    cFileName.assign(utf8_encode(ffd.cFileName));

    // search directory entries that aren't . or .. or hidden
    if (cFileName[0] != '.' || (flag_hidden && cFileName[1] != '\0' && cFileName[1] != '.'))
    {
      size_t len = strlen(pathname);

      if (len == 1 && pathname[0] == '.')
        dirpathname.assign(cFileName);
      else if (len > 0 && pathname[len - 1] == PATHSEPCHR)
        dirpathname.assign(pathname).append(cFileName);
      else
        dirpathname.assign(pathname).append(PATHSEPSTR).append(cFileName);

      ino_t inode = 0;
      uint64_t info = 0;

      // --sort: get file info
      if (flag_sort_key != Sort::NA && flag_sort_key != Sort::NAME)
      {
        if (flag_sort_key == Sort::SIZE)
        {
          info = static_cast<uint64_t>(ffd.nFileSizeLow) | (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32);
        }
        else if (flag_sort_key == Sort::LIST)
        {
          info = index++;
        }
        else
        {
          struct _FILETIME& time = flag_sort_key == Sort::USED ? ffd.ftLastAccessTime : flag_sort_key == Sort::CHANGED ? ffd.ftLastWriteTime : ffd.ftCreationTime;
          info = static_cast<uint64_t>(time.dwLowDateTime) | (static_cast<uint64_t>(time.dwHighDateTime) << 32);
        }
      }

      // search dirpathname, unless searchable directory into which we should recurse
      switch (select(level + 1, dirpathname.c_str(), cFileName.c_str(), DIRENT_TYPE_UNKNOWN, inode, info))
      {
        case Type::DIRECTORY:
          dir_entries.emplace_back(dirpathname, 0, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(dirpathname.c_str(), Entry::UNDEFINED_COST);
          else
            file_entries.emplace_back(dirpathname, 0, info);
          break;

        case Type::SKIP:
          break;
      }

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof || out.cancelled())
        break;
    }
  } while (FindNextFileW(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  struct dirent *dirent = NULL;
  uint64_t index = 0;

  while ((dirent = readdir(dir)) != NULL)
  {
    // search directory entries that aren't . or .. or hidden
    if (dirent->d_name[0] != '.' || (flag_hidden && dirent->d_name[1] != '\0' && dirent->d_name[1] != '.'))
    {
      size_t len = strlen(pathname);

      if (len == 1 && pathname[0] == '.')
        dirpathname.assign(dirent->d_name);
      else if (len > 0 && pathname[len - 1] == PATHSEPCHR)
        dirpathname.assign(pathname).append(dirent->d_name);
      else
        dirpathname.assign(pathname).append(PATHSEPSTR).append(dirent->d_name);

      Type type;
      ino_t inode;
      uint64_t info;

      // search dirpathname, unless searchable directory into which we should recurse
#if defined(HAVE_STRUCT_DIRENT_D_TYPE) && defined(HAVE_STRUCT_DIRENT_D_INO)
      inode = dirent->d_ino;
      type = select(level + 1, dirpathname.c_str(), dirent->d_name, dirent->d_type, inode, info);
#else
      inode = 0;
      type = select(level + 1, dirpathname.c_str(), dirent->d_name, DIRENT_TYPE_UNKNOWN, inode, info);
#endif

      if (flag_sort_key == Sort::LIST)
        info = index++;

      switch (type)
      {
        case Type::DIRECTORY:
          dir_entries.emplace_back(dirpathname, inode, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(dirpathname.c_str(), Entry::UNDEFINED_COST);
          else
            file_entries.emplace_back(dirpathname, inode, info);
          break;

        case Type::SKIP:
          break;
      }

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof || out.cancelled())
        break;
    }
  }

  closedir(dir);

#endif

  // -Z and --sort=best: presearch the selected files to determine edit distance cost
  if (flag_fuzzy > 0 && flag_sort_key == Sort::BEST)
  {
    auto entry = file_entries.begin();
    while (entry != file_entries.end())
    {
      entry->cost = compute_cost(entry->pathname.c_str());

      // if a file cannot be opened, then remove it
      if (entry->cost == Entry::UNDEFINED_COST)
        entry = file_entries.erase(entry);
      else
        ++entry;
    }
  }

  // --sort: sort the selected non-directory entries and search them
  if (flag_sort_key != Sort::NA)
  {
    if (flag_sort_key == Sort::NAME)
    {
      if (flag_sort_rev)
        std::sort(file_entries.begin(), file_entries.end(), Entry::rev_comp_by_path);
      else
        std::sort(file_entries.begin(), file_entries.end(), Entry::comp_by_path);
    }
    else if (flag_sort_key == Sort::BEST)
    {
      if (flag_sort_rev)
        std::sort(file_entries.begin(), file_entries.end(), Entry::rev_comp_by_best);
      else
        std::sort(file_entries.begin(), file_entries.end(), Entry::comp_by_best);
    }
    else
    {
      if (flag_sort_rev)
        std::sort(file_entries.begin(), file_entries.end(), Entry::rev_comp_by_info);
      else
        std::sort(file_entries.begin(), file_entries.end(), Entry::comp_by_info);
    }

    // search the select sorted non-directory entries
    for (const auto& entry : file_entries)
    {
      search(entry.pathname.c_str(), entry.cost);

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof || out.cancelled())
        break;
    }
  }

  // --sort: sort the selected subdirectory entries
  if (flag_sort_key != Sort::NA)
  {
    if (flag_sort_key == Sort::NAME || flag_sort_key == Sort::BEST)
    {
      if (flag_sort_rev)
        std::sort(dir_entries.begin(), dir_entries.end(), Entry::rev_comp_by_path);
      else
        std::sort(dir_entries.begin(), dir_entries.end(), Entry::comp_by_path);
    }
    else
    {
      if (flag_sort_rev)
        std::sort(dir_entries.begin(), dir_entries.end(), Entry::rev_comp_by_info);
      else
        std::sort(dir_entries.begin(), dir_entries.end(), Entry::comp_by_info);
    }
  }

  // recurse into the selected subdirectories
  for (const auto& entry : dir_entries)
  {
    // stop after finding max-files matching files
    if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
      break;

    // stop when output is blocked or search cancelled
    if (out.eof || out.cancelled())
      break;

#ifndef OS_WIN
    // -R: check if this directory was visited before
    std::pair<std::set<ino_t>::iterator,bool> vino;

    if (flag_dereference)
    {
      vino = visited.insert(entry.inode);

      // if visited before, then do not recurse on this directory again
      if (!vino.second)
        continue;
    }
#endif

    recurse(level + 1, entry.pathname.c_str());

#ifndef OS_WIN
    if (flag_dereference)
      visited.erase(vino.first);
#endif
  }

  // --ignore-files: restore all exclusions when saved
  if (saved)
  {
    save_all_exclude.swap(flag_all_exclude);
    save_all_exclude_dir.swap(flag_all_exclude_dir);
  }
}

// -Z and --sort=best: perform a presearch to determine edit distance cost, returns MAX_COST when no match is found
uint16_t Grep::compute_cost(const char *pathname)
{
  // default cost is undefined, which erases pathname from the sorted list
  uint16_t cost = Entry::UNDEFINED_COST;

  // stop when output is blocked
  if (out.eof)
    return cost;

  try
  {
    // open (archive or compressed) file (pathname is NULL to read stdin), return on failure
    if (!open_file(pathname))
      return cost;
  }

  catch (...)
  {
    // this should never happen
    warning("exception while opening", pathname);

    return cost;
  }

  cost = Entry::MAX_COST;

  // -Z: matcher is a FuzzyMatcher for sure
  reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);
  fuzzy_matcher->distance(static_cast<uint16_t>(flag_fuzzy));

  // search file to compute minimum cost
  do
  {
    try
    {
      if (init_read())
      {
        while (fuzzy_matcher->find())
        {
          if (fuzzy_matcher->edits() < cost)
            cost = fuzzy_matcher->edits();

          // exact match?
          if (cost == 0)
            break;
        }
      }
    }

    catch (...)
    {
      // this should never happen
      warning("exception while searching", pathname);
    }

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
    if (flag_decompress && cost == 0)
      zthread.cancel();
#endif
#endif

    // close file or -z: loop over next extracted archive parts, when applicable
  } while (close_file(pathname));

  return cost;
}

// search input and display pattern matches
void Grep::search(const char *pathname, uint16_t cost)
{
  // -Zbest (or pseudo --best-match): compute cost if not yet computed by --sort=best
  if (flag_best_match && flag_fuzzy > 0 && !flag_quiet && !flag_files_with_matches && matchers == NULL)
  {
    // -Z: matcher is a FuzzyMatcher for sure
    reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);
    fuzzy_matcher->distance(static_cast<uint16_t>(flag_fuzzy));

    if (pathname != LABEL_STANDARD_INPUT)
    {
      // compute distance cost if not yet computed by --sort=best
      if (cost == Entry::UNDEFINED_COST)
      {
        cost = compute_cost(pathname);

        // if no match, then stop searching this file
        if (cost == Entry::UNDEFINED_COST)
          return;
      }

      // no match found?
      if (cost == Entry::MAX_COST)
      {
        if (!flag_invert_match)
          return;

        // -v: invert match when no match was found, zero cost since we don't expect any matches
        cost = 0;
      }

      // combine max distance cost (lower byte) with INS, DEL, SUB and BIN fuzzy flags (upper byte)
      cost = (cost & 0xff) | (flag_fuzzy & 0xff00);
      fuzzy_matcher->distance(cost);
    }
  }

  // stop when output is blocked
  if (out.eof)
    return;

  try
  {
    // open (archive or compressed) file (pathname is NULL to read stdin), return on failure
    if (!open_file(pathname))
      return;
  }

  catch (...)
  {
    // this should never happen
    warning("exception while opening", pathname != NULL && *pathname != '\0' ? pathname : flag_label);

    return;
  }

  // pathname is NULL when stdin is searched
  if (pathname == LABEL_STANDARD_INPUT)
    pathname = flag_label;

  bool colorize = flag_apply_color || flag_tag != NULL;
  bool matched = false;

  // -z: loop over extracted archive parts, when applicable
  do
  {
    if (!init_read())
      goto exit_search;

    try
    {
      size_t matches = 0;

      // --files: reset the matching[] bitmask used in cnf_matching() for each matcher in matchers
      if (flag_files && matchers != NULL)
      {
        // hold the output
        out.hold();

        // reset the bit corresponding to each matcher in matchers
        size_t n = matchers->size();
        matching.resize(0);
        matching.resize(n);

        // reset the bit corresponding to the OR NOT terms of each matcher in matchers
        notmatching.resize(n);
        size_t j = 0;
        for (auto& i : *matchers)
        {
          notmatching[j].resize(0);
          notmatching[j].resize(i.size() > 0 ? i.size() - 1 : 0);
          ++j;
        }
      }

      if (flag_quiet || flag_files_with_matches)
      {
        // option -q, -l, or -L

        // --match and -v matches nothing
        if (flag_match && flag_invert_match)
          goto exit_search;

        // --format: whether to out.acquire() early before Stats::found_part()
        bool acquire = flag_format != NULL && (flag_format_open != NULL || flag_format_close != NULL);

        size_t lineno = 0;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          // --range: max line exceeded?
          if (flag_max_line > 0 && current_lineno > flag_max_line)
            break;

          if (matchers != NULL)
          {
            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            // check CNF AND/OR/NOT matching
            if (!cnf_matching(bol, eol, acquire) || out.holding())
              continue;
          }

          if (flag_min_count == 0 || flag_ungroup || flag_only_matching || lineno != current_lineno)
            ++matches;

          // --min-count: require at least min-count matches, otherwise stop searching
          if (flag_min_count == 0 || matches >= flag_min_count)
            break;

          lineno = current_lineno;
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          matches = 0;

        // --files: if we are still holding the output and CNF is finally satisfyable then a match was made
        if (flag_files && matchers != NULL)
        {
          if (!cnf_satisfied(acquire))
            goto exit_search;

          matches = 1;
        }

        // -v: invert
        if (flag_invert_match)
          matches = !matches;

        if (matches > 0)
        {
          // --format-open or format-close: we must acquire lock early before Stats::found_part()
          if (acquire)
            out.acquire();

          if (!flag_files || matchers == NULL)
          {
            // --max-files: max reached?
            if (!Stats::found_part())
              goto exit_search;
          }

          // -l or -L
          if (flag_files_with_matches)
          {
            if (flag_format != NULL)
            {
              if (flag_format_open != NULL)
                out.format(flag_format_open, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
              out.format(flag_format, pathname, partname, 1, matcher, false, false);
              if (flag_format_close != NULL)
                out.format(flag_format_close, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
            }
            else
            {
              out.str(color_fn);
              if (color_hl != NULL)
              {
                out.str(color_hl);
                out.uri(color_wd);
                out.uri(pathname);
                out.str(color_st);
              }
              out.str(pathname);
              if (color_hl != NULL)
              {
                out.str(color_hl);
                out.str(color_st);
              }
              if (!partname.empty())
              {
                out.chr('{');
                out.str(partname);
                out.chr('}');
              }
              out.str(color_off);

              if (flag_null)
                out.chr('\0');
              else
                out.nl();
            }
          }
        }
      }
      else if (flag_count)
      {
        // option -c

        // --format: whether to out.acquire() early before Stats::found_part()
        bool acquire = flag_format != NULL && (flag_format_open != NULL || flag_format_close != NULL);

        if (!flag_match || !flag_invert_match)
        {
          if (flag_ungroup || flag_only_matching)
          {
            // -co or -cu: count the number of patterns matched in the file

            while (matcher->find())
            {
              // --range: max line exceeded?
              if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
                break;

              if (matchers != NULL)
              {
                const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
                const char *bol = matcher->bol();

                // check CNF AND/OR/NOT matching, with --files acquire lock before Stats::found_part()
                if (!cnf_matching(bol, eol, acquire))
                  continue;
              }

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

            while (matcher->find())
            {
              size_t current_lineno = matcher->lineno();

              if (lineno != current_lineno)
              {
                // --range: max line exceeded?
                if (flag_max_line > 0 && current_lineno > flag_max_line)
                  break;

                if (matchers != NULL)
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
                  const char *bol = matcher->bol();

                  // check CNF AND/OR/NOT matching, with --files acquire lock before Stats::found_part()
                  if (!cnf_matching(bol, eol, acquire))
                    continue;
                }

                ++matches;

                // -m: max number of matches reached?
                if (flag_max_count > 0 && matches >= flag_max_count)
                  break;

                lineno = current_lineno;
              }
            }

            // -c with -v: count non-matching lines
            if (flag_invert_match)
            {
              matches = matcher->lineno() - matches;
              if (matches > 0)
                --matches;
            }
          }

          // --match: adjust match count when the last line has no \n line ending
          if (flag_match && matcher->last() > 0 && *(matcher->eol(true) - 1) != '\n')
            ++matches;
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          goto exit_search;

        // --files: if we are still holding the output and CNF is not satisfyable then no global matches were made
        if (flag_files && matchers != NULL)
        {
          if (!cnf_satisfied(acquire))
            goto exit_search; // we cannot report 0 matches and ensure accurate output
        }
        else
        {
          // --format-open or --format-close: we must acquire lock early before Stats::found_part()
          if (acquire)
            out.acquire();

          // --max-files: max reached?
          // unfortunately, allowing 'acquire' below produces "x matching + y in archives"
          // but without this we cannot produce correct format-open and format-close outputs
          if (matches > 0 || acquire)
            if (!Stats::found_part())
              goto exit_search;
        }

        if (flag_format != NULL)
        {
          if (flag_format_open != NULL)
            out.format(flag_format_open, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
          out.format(flag_format, pathname, partname, matches, matcher, false, false);
          if (flag_format_close != NULL)
            out.format(flag_format_close, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
        }
        else
        {
          if (flag_with_filename || !partname.empty())
          {
            out.str(color_fn);
            if (color_hl != NULL)
            {
              out.str(color_hl);
              out.uri(color_wd);
              out.uri(pathname);
              out.str(color_st);
            }
            out.str(pathname);
            if (color_hl != NULL)
            {
              out.str(color_hl);
              out.str(color_st);
            }
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
          out.nl();
        }
      }
      else if (flag_format != NULL)
      {
        // option --format

        // whether to out.acquire() early before Stats::found_part()
        bool acquire = flag_format_open != NULL || flag_format_close != NULL;

        if (flag_invert_match)
        {
          // FormatInvertMatchHandler requires lineno to be set precisely, i.e. after skipping --range lines
          size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
          bool binfile = false; // unused
          bool hex = false;     // unused
          bool binary = false;  // unused
          bool stop = false;

          // construct event handler functor with captured *this and some of the locals
          FormatInvertMatchGrepHandler invert_match_handler(*this, pathname, lineno, binfile, hex, binary, matches, stop);

          // register an event handler to display non-matching lines
          matcher->set_handler(&invert_match_handler);

          // to get the context from the invert_match handler explicitly
          reflex::AbstractMatcher::Context context;

          while (matcher->find())
          {
            size_t current_lineno = matcher->lineno();

            if (lineno != current_lineno)
            {
              if (matchers != NULL)
              {
                const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
                const char *bol = matcher->bol();

                // check CNF AND/OR/NOT matching
                if (!cnf_matching(bol, eol))
                  continue;
              }

              // get the lines before the matched line
              context = matcher->before();

              // display non-matching lines up to this line
              if (context.len > 0)
                invert_match_handler(*matcher, context.buf, context.len, context.num);

              // --range: max line exceeded?
              if (flag_max_line > 0 && current_lineno > flag_max_line)
                goto done_search;

              // --max-files: max reached?
              if (stop)
                goto exit_search;

              // -m: max number of matches reached?
              if (flag_max_count > 0 && matches >= flag_max_count)
                goto done_search;

              // output blocked?
              if (out.eof)
                goto exit_search;
            }

            lineno = current_lineno + matcher->lines() - 1;
          }

          // get the remaining context
          context = matcher->after();

          if (context.len > 0)
            invert_match_handler(*matcher, context.buf, context.len, context.num);
        }
        else
        {
          while (matcher->find())
          {
            // --range: max line exceeded?
            if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
              break;

            if (matchers != NULL)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
              const char *bol = matcher->bol();

              // check CNF AND/OR/NOT matching
              if (!cnf_matching(bol, eol, acquire))
                continue;
            }

            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
              continue;

            // output --format-open=FORMAT for the first (or --min-count) match found
            if (matches == flag_min_count + (flag_min_count == 0))
            {
              if (flag_files && matchers != NULL)
              {
                // --format-open: we must acquire lock early before Stats::found_part()
                if (acquire && out.holding())
                {
                  out.acquire();

                  // --max-files: max reached?
                  if (!Stats::found_part())
                    goto exit_search;
                }
              }
              else
              {
                // --format-open: we must acquire lock early before Stats::found_part()
                if (acquire)
                  out.acquire();

                // --max-files: max reached?
                if (!Stats::found_part())
                  goto exit_search;
              }

              if (flag_format_open != NULL)
              {
                // output --format-open=FORMAT
                out.format(flag_format_open, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);

                // --files: undo files count
                if (flag_files && matchers != NULL && out.holding())
                  Stats::undo_found_part();
              }
            }

            // output --format=FORMAT
            out.format(flag_format, pathname, partname, matches, matcher, matches > 1, matches > 1);

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            out.check_flush();
          }
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          goto exit_search;

        // --files: if we are still holding the output and CNF is not satisfyable then no global matches were made
        if (flag_files && matchers != NULL)
          if (!cnf_satisfied(true))
            goto exit_search;

        // output --format-close=FORMAT
        if (matches > 0 && flag_format_close != NULL)
          out.format(flag_format_close, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
      }
      else if (flag_only_matching)
      {
        // option -o

        size_t count = 0;
        size_t width = flag_before_context + flag_after_context;
        size_t lineno = 0;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool nl = false;
        bool ungroup = flag_ungroup || flag_column_number || flag_byte_offset;
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        while (matcher->find())
        {
          const char *begin = matcher->begin();
          size_t size = matcher->size();
          bool binary = flag_hex || (!flag_text && is_binary(begin, size));

          if (hex && !binary)
          {
            out.dump.done();
          }
          else if (!hex && binary && nl)
          {
            out.nl();
            nl = false;
          }

          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno || ungroup)
          {
            if (nl)
            {
              if (restline_size > 0)
              {
                out.str(color_cx);
                if (utf8nlen(restline_data, restline_size) > width)
                {
                  out.utf8strn(restline_data, restline_size, width);
                  out.str(color_off);
                  out.str(color_se);
                  out.str("...");
                }
                else
                {
                  out.str(restline_data, restline_size);
                }
                out.str(color_off);
                restline_size = 0;
              }

              if (count > 0)
              {
                out.str(color_se);
                out.str("...");
                out.str(color_off);
                out.str(color_cn);
                out.str("[+");
                out.num(count);
                out.str(" more]");
                out.str(color_off);
              }

              out.nl();

              count = 0;
              width = flag_before_context + flag_after_context;
              nl = false;
            }

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            if (matchers != NULL)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
              const char *bol = matcher->bol();

              // check CNF AND/OR/NOT matching
              if (!cnf_matching(bol, eol))
                continue;
            }

            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
            {
              lineno = current_lineno;
              continue;
            }

            if (matches == flag_min_count + (flag_min_count == 0) && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }

            if (binfile || (binary && !flag_hex && !flag_with_hex))
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_separator : "+";
              out.header(pathname, partname, current_lineno, matcher, matcher->first(), separator, binary);
            }

            lineno = current_lineno;
          }

          // --min-count: require at least min-count matches
          if (flag_min_count > 0 && matches < flag_min_count)
            continue;

          hex = binary;

          if (binary)
          {
            if (flag_hex || flag_with_hex)
            {
              out.dump.next(matcher->first());
              out.dump.hex(Output::Dump::HEX_MATCH, matcher->first(), begin, size);
            }
            else
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            lineno += matcher->lines() - 1;
          }
          else if (flag_replace != NULL)
          {
            // output --replace=FORMAT
            out.str(match_ms);
            out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
            out.str(match_off);
          }
          else if (flag_before_context + flag_after_context > 0)
          {
            if (restline_size > 0)
            {
              size_t first = matcher->first();
              if (first - restline_last < restline_size)
                restline_size = first - restline_last;

              size_t after = utf8nlen(restline_data, restline_size);
              if (after > width)
                after = width;

              width -= after;

              if (after > 0)
              {
                out.str(color_cx);
                out.utf8strn(restline_data, restline_size, after);
                out.str(color_off);
              }
              restline_size = 0;
            }

            if (width == 0)
            {
              ++count;
            }
            else
            {
              // do not include additional lines in the match output
              const char *to = static_cast<const char*>(memchr(begin, '\n', size));
              if (to != NULL)
                size = to - begin;

              // length of the match is the number of UTF-8-encoded Unicode characters
              size_t length = utf8nlen(begin, size);
              size_t fit_length = length;

              if (fit_length > width)
              {
                if (fit_length > width + 4)
                  fit_length = width;
                width = 0;
              }
              else
              {
                width -= fit_length;
              }

              if (!nl)
              {
                const char *bol = matcher->bol();
                size_t border = matcher->border();
                size_t margin = utf8nlen(bol, border);
                size_t before = flag_before_context * fit_length / (flag_before_context + flag_after_context);

                if (before < flag_before_context)
                  before = flag_before_context - before;
                else
                  before = 0;

                if (margin > before)
                {
                  out.str(color_se);
                  out.str("...");
                  out.str(color_off);
                  out.str(color_cx);
                  out.utf8strn(utf8skipn(bol, border, margin - before), border, before);
                  out.str(color_off);
                  width -= before;
                }
                else
                {
                  out.str(color_cx);
                  out.str(bol, border);
                  out.str(color_off);
                  width -= margin;
                }
              }

              out.str(match_ms);
              if (fit_length == length)
              {
                out.str(begin, size);
              }
              else
              {
                out.utf8strn(begin, size, fit_length);
                out.str("[+");
                out.num(length - fit_length);
                out.str("]");
              }
              out.str(match_off);

              const char *eol = matcher->eol(); // warning: call eol() before bol() and end()
              const char *end = matcher->end();

              restline.assign(end, eol - end);
              restline_data = restline.c_str();
              restline_size = restline.size();
              restline_last = matcher->last();
            }

            nl = true;
          }
          else
          {
            // echo multi-line matches line-by-line

            const char *from = begin;
            const char *to;

            while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
            {
              out.str(match_ms);
              out.str(from, to - from);
              out.str(match_off);
              out.chr('\n');

              out.header(pathname, partname, ++lineno, NULL, matcher->first() + (to - begin) + 1, "|", false);

              from = to + 1;
            }

            size -= from - begin;

            if (size > 0)
            {
              bool lf_only = from[size - 1] == '\n';
              size -= lf_only;
              if (size > 0)
              {
                out.str(match_ms);
                out.str(from, size);
                out.str(match_off);
              }
              out.nl(lf_only);
            }
            else
            {
              nl = true;
            }
          }
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          goto exit_search;

        if (nl)
        {
          if (restline_size > 0)
          {
            out.str(color_cx);
            if (utf8nlen(restline_data, restline_size) > width)
            {
              out.utf8strn(restline_data, restline_size, width);
              out.str(color_off);
              out.str(color_se);
              out.str("...");
            }
            else
            {
              out.str(restline_data, restline_size);
            }
            out.str(color_off);
            restline_size = 0;
          }

          if (count > 0)
          {
            out.str(color_se);
            out.str("...");
            out.str(color_off);
            out.str(color_cn);
            out.str("[+");
            out.num(count);
            out.str(" more]");
            out.str(color_off);
          }

          out.nl();
        }

        if (hex)
          out.dump.done();
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line && !flag_invert_match)
      {
        // options -A, -B, -C, -y, -v are not specified

        size_t lineno = 0;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno || flag_ungroup)
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                if (flag_hex_after > 0)
                {
                  size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
                  if (right < restline_size)
                    restline_size = right;
                }

                out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);

                if (lineno + 1 < current_lineno)
                  out.dump.done();
              }
              else
              {
                bool lf_only = false;
                if (restline_size > 0)
                {
                  lf_only = restline_data[restline_size - 1] == '\n';
                  restline_size -= lf_only;
                  if (restline_size > 0)
                  {
                    out.str(color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl(lf_only);
              }

              restline_data = NULL;
            }

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            // check CNF AND/OR/NOT matching
            if (matchers != NULL && !cnf_matching(bol, eol))
              continue;

            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
            {
              lineno = current_lineno;
              continue;
            }

            if (matches == flag_min_count + (flag_min_count == 0) && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binfile || (binary && !flag_hex && !flag_with_hex))
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            size_t border = matcher->border();
            size_t first = matcher->first();
            const char *begin = matcher->begin();
            const char *end = matcher->end();
            size_t size = matcher->size();

            if (hex && !binary)
              out.dump.done();

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_separator : "+";
              out.header(pathname, partname, current_lineno, matcher, first, separator, binary);
            }

            hex = binary;

            lineno = current_lineno;

            if (binary)
            {
              if (flag_hex_before > 0)
              {
                size_t left = (flag_hex_before - 1) * flag_hex_columns + (first % flag_hex_columns);
                if (begin > bol + left)
                {
                  bol = begin - left;
                  border = left;
                }
              }

              out.dump.hex(Output::Dump::HEX_LINE, first - border, bol, border);
              out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size);

              if (flag_ungroup)
              {
                if (flag_hex_after > 0)
                {
                  size_t right = flag_hex_after * flag_hex_columns - ((matcher->last() - 1) % flag_hex_columns) - 1;
                  if (end + right < eol)
                    eol = end + right;
                }

                out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end);
                out.dump.done();
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              out.str(color_sl);
              out.str(bol, border);
              out.str(color_off);

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(match_ms);
                out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                out.str(match_off);
              }
              else
              {
                // echo multi-line matches line-by-line

                const char *from = begin;
                const char *to;

                while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                {
                  out.str(match_ms);
                  out.str(from, to - from);
                  out.str(match_off);
                  out.chr('\n');

                  out.header(pathname, partname, ++lineno, NULL, first + (to - begin) + 1, "|", false);

                  from = to + 1;
                }

                size -= from - begin;
                begin = from;

                out.str(match_ms);
                out.str(begin, size);
                out.str(match_off);
              }

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  bool lf_only = end[eol - end - 1] == '\n';
                  eol -= lf_only;
                  if (eol > end)
                  {
                    out.str(color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl(lf_only);
                }
                else if (matcher->hit_end())
                {
                  out.nl();
                }
                else
                {
                  out.check_flush();
                }
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }
            }
          }
          else
          {
            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
              continue;

            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || colorize)
              {
                size_t first = matcher->first();
                size_t last = matcher->last();
                const char *begin = matcher->begin();

                if (binary)
                {
                  if (flag_hex_after > 0 && flag_hex_before > 0)
                  {
                    size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
                    if (right < first - restline_last)
                    {
                      out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, right);

                      restline_data += right;
                      restline_size -= right;
                      restline_last += right;

                      size_t left = (flag_hex_before - 1) * flag_hex_columns + (first % flag_hex_columns);

                      if (left < first - restline_last)
                      {
                        if (!flag_no_header)
                          out.header(pathname, partname, current_lineno, matcher, first, "+", binary);

                        left = first - restline_last - left;

                        restline_data += left;
                        restline_size -= left;
                        restline_last += left;
                      }
                    }
                  }

                  out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, first - restline_last);
                  out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size);
                }
                else
                {
                  out.str(color_sl);
                  out.str(restline_data, first - restline_last);
                  out.str(color_off);

                  if (flag_replace != NULL)
                  {
                    // output --replace=FORMAT
                    out.str(match_ms);
                    out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                    out.str(match_off);
                  }
                  else
                  {
                    if (lines > 1)
                    {
                      // echo multi-line matches line-by-line

                      const char *from = begin;
                      const char *to;
                      size_t num = 1;

                      while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                      {
                        out.str(match_ms);
                        out.str(from, to - from);
                        out.str(match_off);
                        out.chr('\n');

                        out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "|", false);

                        from = to + 1;
                        ++num;
                      }

                      size -= from - begin;
                      begin = from;
                    }

                    out.str(match_ms);
                    out.str(begin, size);
                    out.str(match_off);
                  }
                }

                if (lines == 1)
                {
                  restline_data += last - restline_last;
                  restline_size -= last - restline_last;
                  restline_last = last;
                }
                else
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before end()
                  const char *end = matcher->end();

                  binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, flag_separator, binary);

                  hex = binary;

                  if (flag_ungroup)
                  {
                    if (binary)
                    {
                      if (flag_hex_after > 0)
                      {
                        size_t right = flag_hex_after * flag_hex_columns - ((matcher->last() - 1) % flag_hex_columns) - 1;
                        if (end + right < eol)
                          eol = end + right;
                      }

                      out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end);
                      out.dump.done();
                    }
                    else
                    {
                      if (eol > end)
                      {
                        bool lf_only = end[eol - end - 1] == '\n';
                        eol -= lf_only;
                        if (eol > end)
                        {
                          out.str(color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl(lf_only);
                      }
                      else if (matcher->hit_end())
                      {
                        out.nl();
                      }
                      else
                      {
                        out.check_flush();
                      }
                    }
                  }
                  else
                  {
                    restline.assign(end, eol - end);
                    restline_data = restline.c_str();
                    restline_size = restline.size();
                    restline_last = last;
                  }

                  lineno += lines - 1;
                }
              }
            }
          }
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          goto exit_search;

        if (restline_data != NULL)
        {
          if (binary)
          {
            if (flag_hex_after > 0)
            {
              size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
              if (right < restline_size)
                restline_size = right;
            }

            out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            bool lf_only = false;
            if (restline_size > 0)
            {
              lf_only = restline_data[restline_size - 1] == '\n';
              restline_size -= lf_only;
              if (restline_size > 0)
              {
                out.str(color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl(lf_only);
          }

          restline_data = NULL;
        }

        if (binary)
          out.dump.done();
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line)
      {
        // option -v without -A, -B, -C, -y

        // InvertMatchHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // construct event handler functor with captured *this and some of the locals
        InvertMatchGrepHandler invert_match_handler(*this, pathname, lineno, binfile, hex, binary, matches, stop);

        // register an event handler to display non-matching lines
        matcher->set_handler(&invert_match_handler);

        // to get the context from the invert_match handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno)
          {
            if (matchers != NULL)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
              const char *bol = matcher->bol();

              // check CNF AND/OR/NOT matching
              if (!cnf_matching(bol, eol))
                continue;
            }

            // get the lines before the matched line
            context = matcher->before();

            // display non-matching lines up to this line
            if (context.len > 0)
              invert_match_handler(*matcher, context.buf, context.len, context.num);

            if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
              break;

            if (binary)
              out.dump.done();

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              goto done_search;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              goto done_search;

            // output blocked?
            if (out.eof)
              goto exit_search;
          }

          lineno = current_lineno + matcher->lines() - 1;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          invert_match_handler(*matcher, context.buf, context.len, context.num);

        if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
        {
          if (flag_binary_without_match)
            matches = 0;
          else
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }
      else if (flag_any_line)
      {
        // option -y

        // AnyLineGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        AnyLineGrepHandler any_line_handler(*this, pathname, lineno, binfile, hex, binary, matches, stop, restline_data, restline_size, restline_last);

        // register an event handler functor to display non-matching lines
        matcher->set_handler(&any_line_handler);

        // to display colors with or without -v
        short v_hex_line = flag_invert_match ? Output::Dump::HEX_CONTEXT_LINE : Output::Dump::HEX_LINE;
        short v_hex_match = flag_invert_match ? Output::Dump::HEX_CONTEXT_MATCH : Output::Dump::HEX_MATCH;
        const char *v_color_sl = flag_invert_match ? color_cx : color_sl;
        const char *v_match_ms = flag_invert_match ? match_mc : match_ms;

        // to get the context from the any_line handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno) /* || flag_ungroup) // logically OK but dead code because -y */
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(v_hex_line, restline_last, restline_data, restline_size);
              }
              else
              {
                bool lf_only = false;
                if (restline_size > 0)
                {
                  lf_only = restline_data[restline_size - 1] == '\n';
                  restline_size -= lf_only;
                  if (restline_size > 0)
                  {
                    out.str(v_color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl(lf_only);
              }

              restline_data = NULL;
            }

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            // check CNF AND/OR/NOT matching
            if (matchers != NULL && !cnf_matching(bol, eol))
              continue;

            if (!flag_invert_match)
            {
              ++matches;

              // --min-count: require at least min-count matches
              if (flag_min_count > 0 && matches < flag_min_count)
              {
                lineno = current_lineno;
                continue;
              }

              if (matches == flag_min_count + (flag_min_count == 0) && (!flag_files || matchers == NULL))
              {
                // --max-files: max reached?
                if (!Stats::found_part())
                  goto exit_search;
              }
            }

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
            {
              any_line_handler(*matcher, context.buf, context.len, context.num);

              if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
              {
                if (flag_binary_without_match)
                {
                  matches = 0;
                }
                else
                {
                  out.binary_file_matches(pathname, partname);
                  matches = 1;
                }

                if (flag_files && matchers != NULL && out.holding())
                  continue;

                goto done_search;
              }
            }

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches > flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binfile || (binary && !flag_hex && !flag_with_hex))
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else if (flag_invert_match)
              {
                lineno = current_lineno + matcher->lines() - 1;
                continue;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            size_t border = matcher->border();
            size_t first = matcher->first();
            const char *begin = matcher->begin();
            const char *end = matcher->end();
            size_t size = matcher->size();

            if (hex && !binary)
              out.dump.done();

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_invert_match ? "-" : flag_separator : "+";
              out.header(pathname, partname, current_lineno, matcher, first, separator, binary);
            }

            hex = binary;

            lineno = current_lineno;

            if (binary)
            {
              out.dump.hex(v_hex_line, first - border, bol, border);
              out.dump.hex(v_hex_match, first, begin, size);

              if (false) /* flag_ungroup) // logically OK but dead code because -y */
              {
                out.dump.hex(v_hex_line, matcher->last(), end, eol - end);
                out.dump.done();
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              out.str(v_color_sl);
              out.str(bol, border);
              out.str(color_off);

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(v_match_ms);
                out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                out.str(match_off);
              }
              else
              {
                // echo multi-line matches line-by-line

                const char *from = begin;
                const char *to;

                while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                {
                  out.str(v_match_ms);
                  out.str(from, to - from);
                  out.str(match_off);
                  out.chr('\n');

                  out.header(pathname, partname, ++lineno, NULL, first + (to - begin) + 1, "|", false);

                  from = to + 1;
                }

                size -= from - begin;
                begin = from;

                out.str(v_match_ms);
                out.str(begin, size);
                out.str(match_off);
              }

              if (false) /* flag_ungroup) // logically OK but dead code because -y */
              {
                if (eol > end)
                {
                  bool lf_only = end[eol - end - 1] == '\n';
                  eol -= end[eol - end - 1] == '\n';
                  if (eol > end)
                  {
                    out.str(v_color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl(lf_only);
                }
                else if (matcher->hit_end())
                {
                  out.nl();
                }
                else
                {
                  out.check_flush();
                }
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }
            }
          }
          else if (!binfile && (!binary || flag_hex || flag_with_hex))
          {
            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
              continue;

            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || colorize)
              {
                size_t first = matcher->first();
                size_t last = matcher->last();
                const char *begin = matcher->begin();

                if (binary)
                {
                  out.dump.hex(v_hex_line, restline_last, restline_data, first - restline_last);
                  out.dump.hex(v_hex_match, first, begin, size);
                }
                else
                {
                  out.str(v_color_sl);
                  out.str(restline_data, first - restline_last);
                  out.str(color_off);

                  if (flag_replace != NULL)
                  {
                    // output --replace=FORMAT
                    out.str(v_match_ms);
                    out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                    out.str(match_off);
                  }
                  else
                  {
                    if (lines > 1)
                    {
                      // echo multi-line matches line-by-line

                      const char *from = begin;
                      const char *to;
                      size_t num = 1;

                      while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                      {
                        out.str(v_match_ms);
                        out.str(from, to - from);
                        out.str(match_off);
                        out.chr('\n');

                        out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "|", false);

                        from = to + 1;
                        ++num;
                      }

                      size -= from - begin;
                      begin = from;
                    }

                    out.str(v_match_ms);
                    out.str(begin, size);
                    out.str(match_off);
                  }
                }

                if (lines == 1)
                {
                  restline_data += last - restline_last;
                  restline_size -= last - restline_last;
                  restline_last = last;
                }
                else
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before end()
                  const char *end = matcher->end();

                  binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, flag_separator, binary);

                  hex = binary;

                  if (false) /* flag_ungroup) // logically OK but dead code because -y */
                  {
                    if (binary)
                    {
                      out.dump.hex(v_hex_line, matcher->last(), end, eol - end);
                      out.dump.done();
                    }
                    else
                    {
                      if (eol > end)
                      {
                        bool lf_only = end[eol - end - 1] == '\n';
                        eol -= lf_only;
                        if (eol > end)
                        {
                          out.str(v_color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl(lf_only);
                      }
                      else if (matcher->hit_end())
                      {
                        out.nl();
                      }
                      else
                      {
                        out.check_flush();
                      }
                    }
                  }
                  else
                  {
                    restline.assign(end, eol - end);
                    restline_data = restline.c_str();
                    restline_size = restline.size();
                    restline_last = last;
                  }

                  lineno += lines - 1;
                }
              }
            }
          }
        }

        // --min-count: require at least min-count matches
        if (flag_min_count > 0 && matches < flag_min_count)
          matches = 0;

        if (restline_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(v_hex_line, restline_last, restline_data, restline_size);
          }
          else
          {
            bool lf_only = false;
            if (restline_size > 0)
            {
              lf_only = restline_data[restline_size - 1] == '\n';
              restline_size -= lf_only;
              if (restline_size > 0)
              {
                out.str(v_color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl(lf_only);
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          any_line_handler(*matcher, context.buf, context.len, context.num);

        if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
        {
          if (flag_binary_without_match)
            matches = 0;
          else
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }
      else if (!flag_invert_match)
      {
        // options -A, -B, -C without -v

        // ContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        ContextGrepHandler context_handler(*this, pathname, lineno, binfile, hex, binary, matches, stop, restline_data, restline_size, restline_last);

        // register an event handler functor to display non-matching lines
        matcher->set_handler(&context_handler);

        // to get the context from the any_line handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno || flag_ungroup)
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                if (flag_hex_after > 0)
                {
                  size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
                  if (right < restline_size)
                    restline_size = right;
                }

                out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                bool lf_only = false;
                if (restline_size > 0)
                {
                  lf_only = restline_data[restline_size - 1] == '\n';
                  restline_size -= lf_only;
                  if (restline_size > 0)
                  {
                    out.str(color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl(lf_only);
              }

              restline_data = NULL;
            }

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            // check CNF AND/OR/NOT matching
            if (matchers != NULL && !cnf_matching(bol, eol))
              continue;

            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
            {
              lineno = current_lineno;
              continue;
            }

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              context_handler(*matcher, context.buf, context.len, context.num);

            if (binfile || (binary && !flag_hex && !flag_with_hex))
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            context_handler.output_before_context();

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            if (matches == 1 && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches > flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binfile || (binary && !flag_hex && !flag_with_hex))
            {
              if (flag_binary_without_match)
              {
                matches = 0;
              }
              else
              {
                out.binary_file_matches(pathname, partname);
                matches = 1;
              }

              if (flag_files && matchers != NULL && out.holding())
                continue;

              goto done_search;
            }

            size_t border = matcher->border();
            size_t first = matcher->first();
            const char *begin = matcher->begin();
            const char *end = matcher->end();
            size_t size = matcher->size();

            if (hex && !binary)
              out.dump.done();

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_invert_match ? "-" : flag_separator : "+";
              out.header(pathname, partname, current_lineno, matcher, first, separator, binary);
            }

            hex = binary;

            lineno = current_lineno;

            if (binary)
            {
              if (flag_hex_before > 0)
              {
                size_t left = (flag_hex_before - 1) * flag_hex_columns + (first % flag_hex_columns);
                if (begin > bol + left)
                {
                  bol = begin - left;
                  border = left;
                }
              }

              out.dump.hex(Output::Dump::HEX_LINE, first - border, bol, border);
              out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size);

              if (flag_ungroup)
              {
                out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end);
                out.dump.done();
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              out.str(color_sl);
              out.str(bol, border);
              out.str(color_off);

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(match_ms);
                out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                out.str(match_off);
              }
              else
              {
                // echo multi-line matches line-by-line

                const char *from = begin;
                const char *to;

                while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                {
                  out.str(match_ms);
                  out.str(from, to - from);
                  out.str(match_off);
                  out.chr('\n');

                  out.header(pathname, partname, ++lineno, NULL, first + (to - begin) + 1, "|", false);

                  from = to + 1;
                }

                size -= from - begin;
                begin = from;

                out.str(match_ms);
                out.str(begin, size);
                out.str(match_off);
              }

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  bool lf_only = end[eol - end - 1] == '\n';
                  eol -= lf_only;
                  if (eol > end)
                  {
                    out.str(color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl(lf_only);
                }
                else if (matcher->hit_end())
                {
                  out.nl();
                }
                else
                {
                  out.check_flush();
                }
              }
              else
              {
                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }
            }
          }
          else
          {
            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
              continue;

            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || colorize)
              {
                size_t first = matcher->first();
                size_t last = matcher->last();
                const char *begin = matcher->begin();

                if (binary)
                {
                  out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, first - restline_last);
                  out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size);
                }
                else
                {
                  out.str(color_sl);
                  out.str(restline_data, first - restline_last);
                  out.str(color_off);

                  if (flag_replace != NULL)
                  {
                    // output --replace=FORMAT
                    out.str(match_ms);
                    out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                    out.str(match_off);
                  }
                  else
                  {
                    if (lines > 1)
                    {
                      // echo multi-line matches line-by-line

                      const char *from = begin;
                      const char *to;
                      size_t num = 1;

                      while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                      {
                        out.str(match_ms);
                        out.str(from, to - from);
                        out.str(match_off);
                        out.chr('\n');

                        out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "|", false);

                        from = to + 1;
                        ++num;
                      }

                      size -= from - begin;
                      begin = from;
                    }

                    out.str(match_ms);
                    out.str(begin, size);
                    out.str(match_off);
                  }
                }

                if (lines == 1)
                {
                  restline_data += last - restline_last;
                  restline_size -= last - restline_last;
                  restline_last = last;
                }
                else
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before end()
                  const char *end = matcher->end();

                  binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, flag_separator, binary);

                  hex = binary;

                  if (flag_ungroup)
                  {
                    if (binary)
                    {
                      out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end);
                      out.dump.done();
                    }
                    else
                    {
                      if (eol > end)
                      {
                        bool lf_only = end[eol - end - 1] == '\n';
                        eol -= lf_only;
                        if (eol > end)
                        {
                          out.str(color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl(lf_only);
                      }
                      else if (matcher->hit_end())
                      {
                        out.nl();
                      }
                      else
                      {
                        out.check_flush();
                      }
                    }
                  }
                  else
                  {
                    restline.assign(end, eol - end);
                    restline_data = restline.c_str();
                    restline_size = restline.size();
                    restline_last = last;
                  }

                  lineno += lines - 1;
                }
              }
            }
          }

          context_handler.set_after_lineno(lineno + 1);
        }

        if (restline_data != NULL)
        {
          if (binary)
          {
            if (flag_hex_after > 0)
            {
              size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
              if (right < restline_size)
                restline_size = right;
            }

            out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            bool lf_only = false;
            if (restline_size > 0)
            {
              lf_only = restline_data[restline_size - 1] == '\n';
              restline_size -= lf_only;
              if (restline_size > 0)
              {
                out.str(color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl(lf_only);
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          context_handler(*matcher, context.buf, context.len, context.num);

        if (binfile || (binary && !flag_hex && !flag_with_hex))
        {
          if (flag_binary_without_match)
            matches = 0;
          else if (matches > 0)
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }
      else
      {
        // options -A, -B, -C with -v

        // InvertContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        size_t last_lineno = lineno;
        size_t after = flag_after_context;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        InvertContextGrepHandler invert_context_handler(*this, pathname, lineno, binfile, hex, binary, matches, stop, restline_data, restline_size, restline_last);

        // register an event handler functor to display non-matching lines
        matcher->set_handler(&invert_context_handler);

        // to get the context from the handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();
          size_t lines = matcher->lines();

          if (last_lineno != current_lineno)
          {
            if (lineno + 1 >= current_lineno)
              after += lines;
            else
              after = 0;
          }

          if (lineno != current_lineno)
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                bool lf_only = false;
                if (restline_size > 0)
                {
                  lf_only = restline_data[restline_size - 1] == '\n';
                  restline_size -= lf_only;
                  if (restline_size > 0)
                  {
                    out.str(color_cx);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl(lf_only);
              }

              restline_data = NULL;
            }

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            // check CNF AND/OR/NOT matching
            if (matchers != NULL && !cnf_matching(bol, eol))
              continue;

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
            {
              invert_context_handler(*matcher, context.buf, context.len, context.num);

              if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
              {
                if (flag_binary_without_match)
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

            lineno = current_lineno;

            // --range: max line exceeded?
            if (flag_max_line > 0 && lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            /* logically OK but dead code because -v
            if (matches == 0 && !flag_invert_match && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }
            */

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            if (after < flag_after_context)
            {
              binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

              if (binfile || (binary && !flag_hex && !flag_with_hex))
              {
                if (flag_binary_without_match)
                {
                  matches = 0;
                }
                else // if (flag_invert_match) is true
                {
                  lineno = last_lineno = current_lineno + matcher->lines() - 1;
                  continue;
                }
                /* logically OK but dead code because -v
                else
                {
                  out.binary_file_matches(pathname, partname);
                  matches = 1;
                }
                */

                goto done_search;
              }

              size_t border = matcher->border();
              size_t first = matcher->first();
              const char *begin = matcher->begin();
              const char *end = matcher->end();
              size_t size = matcher->size();

              if (hex && !binary)
                out.dump.done();

              if (!flag_no_header)
                out.header(pathname, partname, lineno, matcher, first, "-", binary);

              hex = binary;

              if (binary)
              {
                if (flag_hex || flag_with_hex)
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, first - border, bol, border);
                  out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, first, begin, size);

                  restline.assign(end, eol - end);
                  restline_data = restline.c_str();
                  restline_size = restline.size();
                  restline_last = matcher->last();
                }
              }
              else
              {
                out.str(color_cx);
                out.str(bol, border);
                out.str(color_off);

                if (flag_replace != NULL)
                {
                  // output --replace=FORMAT
                  out.str(match_mc);
                  out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                  out.str(match_off);
                }
                else
                {
                  if (lines > 1)
                  {
                    // echo multi-line matches line-by-line

                    const char *from = begin;
                    const char *to;
                    size_t num = 1;

                    while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                    {
                      out.str(match_mc);
                      out.str(from, to - from);
                      out.str(match_off);
                      out.chr('\n');

                      out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "-", false);

                      from = to + 1;
                      ++num;
                    }

                    size -= from - begin;
                    begin = from;
                  }

                  out.str(match_mc);
                  out.str(begin, size);
                  out.str(match_off);
                }

                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }
            }
            else if (flag_before_context > 0)
            {
              binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

              if (binfile || (binary && !flag_hex && !flag_with_hex))
              {
                if (flag_binary_without_match)
                {
                  matches = 0;
                }
                else // if (flag_invert_match) is true
                {
                  lineno = last_lineno = current_lineno + matcher->lines() - 1;
                  continue;
                }
                /* logically OK but dead code because -v
                else
                {
                  out.binary_file_matches(pathname, partname);
                  matches = 1;
                }
                */

                goto done_search;
              }

              if (hex && !binary)
                out.dump.done();
              hex = binary;

              const char *begin = matcher->begin();
              size_t size = matcher->size();
              size_t offset = matcher->first();

              if (lines == 1)
              {
                invert_context_handler.add_before_context_line(bol, eol, matcher->columno(), offset - (begin - bol));
                invert_context_handler.add_before_context_match(begin - bol, size, offset);
              }
              else
              {
                // add lines to the before context

                const char *from = begin;
                const char *to;

                while ((to = static_cast<const char*>(memchr(from, '\n', eol - from))) != NULL)
                {
                  if (from == begin)
                  {
                    invert_context_handler.add_before_context_line(bol, to + 1, matcher->columno(), offset - (begin - bol));
                    invert_context_handler.add_before_context_match(begin - bol, to - from + 1, offset);
                  }
                  else
                  {
                    invert_context_handler.add_before_context_line(from, to + 1, 1, offset);
                    invert_context_handler.add_before_context_match(0, to + 1 < from + size ? to - from + 1 : size, offset);
                  }

                  size -= to - from + 1;
                  offset += to - from + 1;
                  from = to + 1;
                }
              }
            }
          }
          else if (after < flag_after_context)
          {
            size_t size = matcher->size();

            if (size > 0)
            {
              if (lines > 1 || colorize)
              {
                size_t first = matcher->first();
                size_t last = matcher->last();
                const char *begin = matcher->begin();

                if (binary)
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, first - restline_last);
                  out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, first, begin, size);
                }
                else
                {
                  out.str(color_cx);
                  out.str(restline_data, first - restline_last);
                  out.str(color_off);

                  if (flag_replace != NULL)
                  {
                    // output --replace=FORMAT
                    out.str(match_mc);
                    out.format(flag_replace, pathname, partname, matches, matcher, matches > 1, matches > 1);
                    out.str(match_off);
                  }
                  else
                  {
                    if (lines > 1)
                    {
                      // echo multi-line matches line-by-line

                      const char *from = begin;
                      const char *to;
                      size_t num = 1;

                      while ((to = static_cast<const char*>(memchr(from, '\n', size - (from - begin)))) != NULL)
                      {
                        out.str(match_mc);
                        out.str(from, to - from);
                        out.str(match_off);
                        out.chr('\n');

                        out.header(pathname, partname, lineno + num, NULL, first + (to - begin) + 1, "-", false);

                        from = to + 1;
                        ++num;
                      }

                      size -= from - begin;
                      begin = from;
                    }

                    out.str(match_mc);
                    out.str(begin, size);
                    out.str(match_off);
                  }
                }

                if (lines == 1)
                {
                  restline_data += last - restline_last;
                  restline_size -= last - restline_last;
                  restline_last = last;
                }
                else
                {
                  const char *eol = matcher->eol(true); // warning: call eol() before end()
                  const char *end = matcher->end();

                  binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, lineno + lines - 1, matcher, last, "-", binary);

                  hex = binary;

                  restline.assign(end, eol - end);
                  restline_data = restline.c_str();
                  restline_size = restline.size();
                  restline_last = last;
                }
              }
            }
          }
          else
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                bool lf_only = false;
                if (restline_size > 0)
                {
                  lf_only = restline_data[restline_size - 1] == '\n';
                  restline_size -= lf_only;
                  if (restline_size > 0)
                  {
                    out.str(color_cx);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl(lf_only);
              }

              restline_data = NULL;
            }

            if (flag_before_context > 0)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol()
              const char *bol = matcher->bol();
              const char *begin = matcher->begin();
              size_t size = matcher->size();
              size_t offset = matcher->first();

              if (lines == 1)
              {
                invert_context_handler.add_before_context_match(begin - bol, size, offset);
              }
              else
              {
                // add lines to the before context

                const char *end = matcher->end();

                binary = flag_hex || (!flag_text && is_binary(end, eol - end));

                if (binfile || (binary && !flag_hex && !flag_with_hex))
                {
                  if (flag_binary_without_match)
                  {
                    matches = 0;
                  }
                  else // if (flag_invert_match) is true
                  {
                    lineno = last_lineno = current_lineno + matcher->lines() - 1;
                    continue;
                  }
                  /* logically OK but dead code because -v
                  else
                  {
                    out.binary_file_matches(pathname, partname);
                    matches = 1;
                  }
                  */

                  goto done_search;
                }

                if (hex && !binary)
                  out.dump.done();
                hex = binary;

                const char *from = begin;
                const char *to;

                while ((to = static_cast<const char*>(memchr(from, '\n', eol - from))) != NULL)
                {
                  if (from == begin)
                  {
                    invert_context_handler.add_before_context_match(begin - bol, to - from + 1, offset);
                  }
                  else
                  {
                    invert_context_handler.add_before_context_line(from, to + 1, 1, offset);
                    invert_context_handler.add_before_context_match(0, to + 1 < from + size ? to - from + 1 : size, offset);
                  }

                  size -= to - from + 1;
                  offset += to - from + 1;
                  from = to + 1;
                }
              }
            }
          }

          last_lineno = current_lineno;
          lineno = current_lineno + lines - 1;
        }

        if (restline_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            bool lf_only = false;
            if (restline_size > 0)
            {
              lf_only = restline_data[restline_size - 1] == '\n';
              restline_size -= lf_only;
              if (restline_size > 0)
              {
                out.str(color_cx);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl(lf_only);
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          invert_context_handler(*matcher, context.buf, context.len, context.num);

        if (matches > 0 && (binfile || (binary && !flag_hex && !flag_with_hex)))
        {
          if (flag_binary_without_match)
            matches = 0;
          else
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }

done_search:

      // --files: check if all CNF conditions are met globally to launch output or reset matches
      if (flag_files && matchers != NULL)
        if (!cnf_satisfied())
          matches = 0;

      // any matches in this file or archive?
      if (matches > 0)
        matched = true;

      // --break: add a line break when applicable
      if (flag_break && (matches > 0 || flag_any_line) && !flag_quiet && !flag_files_with_matches && !flag_count && flag_format == NULL)
        out.nl();

      Stats::score_matches(matches, matcher->lineno() > 0 ? matcher->lineno() - 1 : 0);
    }

    catch (EXIT_SEARCH&)
    {
      // --files: cnf_matching() rejected a file, no need to search this file any further
    }

    catch (...)
    {
      // this should never happen
      warning("exception while searching", pathname);
    }

exit_search:

    // flush and release output to allow other workers to output results
    out.release();

    // close file or -z: loop over next extracted archive parts, when applicable
  } while (close_file(pathname));

  // this file or archive has a match
  if (matched)
    Stats::found_file();
}

// read globs from a file and split them into files or dirs to include or exclude
void import_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs)
{
  // read globs from the specified file or files
  reflex::BufferedInput input(file);
  std::string line;

  while (true)
  {
    // read the next line
    if (getline(input, line))
      break;

    // trim white space from either end
    trim(line);

    // add glob to files or dirs using gitignore glob pattern rules
    if (!line.empty() && line.front() != '#')
    {
      if (line.front() != '!' || line.size() > 1)
      {
        if (line.back() == '/')
        {
          if (line.size() > 1)
            line.pop_back();
          dirs.emplace_back(line);
        }
        else
        {
          files.emplace_back(line);
        }
      }
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
#ifdef OS_WIN
        fputc('\r', output);
#endif
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

// trim white space from either end of the line
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

  if (len > pos)
    line.erase(pos, len - pos);
}

// trim path separators from an argv[] argument - important: modifies the argv[] string
void trim_pathname_arg(const char *arg)
{
  // remove trailing path separators after the drive prefix and path, if any - note: this truncates argv[] strings
  const char *path = strchr(arg, ':');
  if (path != NULL)
    ++path;
  else
    path = arg;
  size_t len = strlen(path);
  while (len > 1 && path[--len] == PATHSEPCHR)
    const_cast<char*>(path)[len] = '\0';
}

// convert GREP_COLORS and set the color substring to the ANSI SGR codes
void set_color(const char *colors, const char *parameter, char color[COLORLEN])
{
  if (colors != NULL)
  {
    const char *s = strstr(colors, parameter);

    // check if substring parameter is present in colors
    if (s != NULL)
    {
      s += 3;
      char *t = color + 2;

#ifdef WITH_EASY_GREP_COLORS

      // foreground colors: k=black, r=red, g=green, y=yellow b=blue, m=magenta, c=cyan, w=white
      // background colors: K=black, R=red, G=green, Y=yellow B=blue, M=magenta, C=cyan, W=white
      // bright colors: +k, +r, +g, +y, +b, +m, +c, +w, +K, +R, +G, +Y, +B, +M, +C, +W
      // modifiers: h=highlight, u=underline, i=invert, f=faint, n=normal, H=highlight off, U=underline off, I=invert off
      // semicolons are not required and abbreviations can be mixed with numeric ANSI SGR codes

      uint8_t offset = 30;
      bool sep = false;

      while (*s != '\0' && *s != ':' && t - color < COLORLEN - 6)
      {
        if (isdigit(*s))
        {
          if (sep)
            *t++ = ';';
          if (offset == 90)
          {
            *t++ = '1';
            *t++ = ';';
            offset = 30;
          }
          *t++ = *s++;
          while (isdigit(*s) && t - color < COLORLEN - 2)
            *t++ = *s++;
          sep = true;
          continue;
        }

        if (*s == '+')
        {
          offset = 90;
        }
        else if (*s == 'n')
        {
          if (sep)
            *t++ = ';';
          *t++ = '0';
          sep = true;
        }
        else if (*s == 'h')
        {
          if (sep)
            *t++ = ';';
          *t++ = '1';
          sep = true;
        }
        else if (*s == 'H')
        {
          if (sep)
            *t++ = ';';
          *t++ = '2';
          *t++ = '1';
          offset = 30;
          sep = true;
        }
        else if (*s == 'f')
        {
          if (sep)
            *t++ = ';';
          *t++ = '2';
          sep = true;
        }
        else if (*s == 'u')
        {
          if (sep)
            *t++ = ';';
          *t++ = '4';
          sep = true;
        }
        else if (*s == 'U')
        {
          if (sep)
            *t++ = ';';
          *t++ = '2';
          *t++ = '4';
          sep = true;
        }
        else if (*s == 'i')
        {
          if (sep)
            *t++ = ';';
          *t++ = '7';
          sep = true;
        }
        else if (*s == 'I')
        {
          if (sep)
            *t++ = ';';
          *t++ = '2';
          *t++ = '7';
          sep = true;
        }
        else if (*s == ',' || *s == ';' || isspace(*s))
        {
          if (sep)
            *t++ = ';';
          sep = false;
        }
        else
        {
          const char *c = "krgybmcw  KRGYBMCW";
          const char *k = strchr(c, *s);

          if (k != NULL)
          {
            if (sep)
              *t++ = ';';
            uint8_t n = offset + static_cast<uint8_t>(k - c);
            if (n >= 100)
            {
              *t++ = '1';
              n -= 100;
            }
            *t++ = '0' + n / 10;
            *t++ = '0' + n % 10;
            offset = 30;
            sep = true;
          }
        }

        ++s;
      }

#else

      // traditional grep SGR parameters
      while ((*s == ';' || isdigit(*s)) && t - color < COLORLEN - 2)
        *t++ = *s++;

#endif

      if (t > color + 2)
      {
        color[0] = '\033';
        color[1] = '[';
        *t++ = 'm';
        *t++ = '\0';
      }
      else
      {
        color[0] = '\0';
      }
    }
  }
}

// convert unsigned decimal to non-negative size_t, produce error when conversion fails
size_t strtonum(const char *string, const char *message)
{
  char *rest = NULL;
  size_t size = static_cast<size_t>(strtoull(string, &rest, 10));
  if (rest == NULL || *rest != '\0')
    usage(message, string);
  return size;
}

// convert unsigned decimal to positive size_t, produce error when conversion fails or when the value is zero
size_t strtopos(const char *string, const char *message)
{
  size_t size = strtonum(string, message);
  if (size == 0)
    usage(message, string);
  return size;
}

// convert one or two comma-separated unsigned decimals specifying a range to positive size_t, produce error when conversion fails or when the range is invalid
void strtopos2(const char *string, size_t& min, size_t& max, const char *message)
{
  char *rest = const_cast<char*>(string);
  if (*string != ',')
    min = static_cast<size_t>(strtoull(string, &rest, 10));
  else
    min = 0;
  if (*rest == ',')
    max = static_cast<size_t>(strtoull(rest + 1, &rest, 10));
  else
    max = min, min = 0;
  if (rest == NULL || *rest != '\0' || (max > 0 && min > max))
    usage(message, string);
}

// convert unsigned decimal MAX fuzzy with optional prefix '+', '-', or '~' to positive size_t
size_t strtofuzzy(const char *string, const char *message)
{
  char *rest = NULL;
  size_t flags = 0;
  size_t max = 1;
  while (*string != '\0')
  {
    switch (*string)
    {
      case 'b':
        if (strncmp(string, "best", 4) != 0)
          usage(message, string);
        flag_best_match = true;
        string += 4;
        break;
      case '+':
        flags |= reflex::FuzzyMatcher::INS;
        ++string;
        break;
      case '-':
        flags |= reflex::FuzzyMatcher::DEL;
        ++string;
        break;
      case '~':
        flags |= reflex::FuzzyMatcher::SUB;
        ++string;
        break;
      default:
        max = static_cast<size_t>(strtoull(string, &rest, 10));
        if (max == 0 || max > 0xff || rest == NULL || *rest != '\0')
          usage(message, string);
        string = rest;
    }
  }
  return max | flags;
}

// display diagnostic message
void usage(const char *message, const char *arg, const char *valid)
{
  std::cerr << "ugrep: " << message << (arg != NULL ? arg : "");
  if (valid != NULL)
  {
    std::cerr << ", did you mean " << valid << "?" << std::endl;
    std::cerr << "For more help on options, try `ugrep --help' or `ugrep --help WHAT'" << std::endl;
  }
  else
  {
    const char *s = message;
    while (*s != '\0' && *s != '-')
      ++s;
    std::cerr << std::endl;
    std::cerr << "For more help on options, try `ugrep --help' or `ugrep --help ";
    if (*s == '\0')
    {
      std::cerr << "WHAT'" << std::endl;
    }
    else
    {
      const char *e = s;
      while (*e == '-')
        ++e;
      if (*e == '\0')
      {
        if (arg != NULL)
          std::cerr << std::string(s, e - s) << arg << "'" << std::endl;
        else
          std::cerr << "WHAT'" << std::endl;
      }
      else
      {
        while (*e != '\0' && (*e == '-' || isalpha(*e)))
          ++e;
        std::cerr << std::string(s, e - s) << "'" << std::endl;
      }
    }
  }

  // do not exit when reading a config file
  if (!flag_usage_warnings)
    exit(EXIT_ERROR);

  ++warnings;
}

// display usage/help information and exit
void help(std::ostream& out)
{
  out <<
    "Usage: ugrep [OPTIONS] [PATTERN] [-f FILE] [-e PATTERN] [FILE ...]\n\n\
    -A NUM, --after-context=NUM\n\
            Output NUM lines of trailing context after matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  If -o is\n\
            specified, output the match with context to fit NUM columns after\n\
            the match or shortening the match.  See also options -B, -C and -y.\n\
    -a, --text\n\
            Process a binary file as if it were text.  This is equivalent to\n\
            the --binary-files=text option.  This option might output binary\n\
            garbage to the terminal, which can have problematic consequences if\n\
            the terminal driver interprets some of it as commands.\n\
    --and [-e] PATTERN ... -e PATTERN\n\
            Specify additional patterns to match.  Patterns must be specified\n\
            with -e.  Each -e PATTERN following this option is considered an\n\
            alternative pattern to match, i.e. each -e is interpreted as an OR\n\
            pattern.  For example, -e A -e B --and -e C -e D matches lines with\n\
            (`A' or `B') and (`C' or `D').  Note that multiple -e PATTERN are\n\
            alternations that bind more tightly together than --and.  Option\n\
            --stats displays the search patterns applied.  See also options\n\
            --not, --andnot, --bool, --files and --lines.\n\
    --andnot [-e] PATTERN\n\
            Combines --and --not.  See also options --and, --not and --bool.\n\
    -B NUM, --before-context=NUM\n\
            Output NUM lines of leading context before matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  If -o is\n\
            specified, output the match with context to fit NUM columns before\n\
            the match or shortening the match.  See also options -A, -C and -y.\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched line is displayed in front of the\n\
            respective matched line.  If -u is specified, displays the offset\n\
            for each pattern matched on the same line.  Byte offsets are exact\n\
            for ASCII, UTF-8 and raw binary input.  Otherwise, the byte offset\n\
            in the UTF-8 normalized input is displayed.\n\
    --binary-files=TYPE\n\
            Controls searching and reporting pattern matches in binary files.\n\
            TYPE can be `binary', `without-match`, `text`, `hex` and\n\
            `with-hex'.  The default is `binary' to search binary files and to\n\
            report a match without displaying the match.  `without-match'\n\
            ignores binary matches.  `text' treats all binary files as text,\n\
            which might output binary garbage to the terminal, which can have\n\
            problematic consequences if the terminal driver interprets some of\n\
            it as commands.  `hex' reports all matches in hexadecimal.\n\
            `with-hex' only reports binary matches in hexadecimal, leaving text\n\
            matches alone.  A match is considered binary when matching a zero\n\
            byte or invalid UTF.  Short options are -a, -I, -U, -W and -X.\n\
    --bool, -%\n\
            Specifies Boolean query patterns.  A Boolean query pattern is\n\
            composed of `AND', `OR', `NOT' operators and grouping with `(' `)'.\n\
            Spacing between subpatterns is the same as `AND', `|' is the same\n\
            as `OR' and a `-' is the same as `NOT'.  The `OR' operator binds\n\
            more tightly than `AND'.  For example, --bool 'A|B C|D' matches\n\
            lines with (`A' or `B') and (`C' or `D'), --bool 'A -B' matches\n\
            lines with `A' and not `B'.  Operators `AND', `OR', `NOT' require\n\
            proper spacing.  For example, --bool 'A OR B AND C OR D' matches\n\
            lines with (`A' or `B') and (`C' or `D'), --bool 'A AND NOT B'\n\
            matches lines with `A' without `B'.  Quoted subpatterns are matched\n\
            literally as strings.  For example, --bool 'A \"AND\"|\"OR\"' matches\n\
            lines with `A' and also either `AND' or `OR'.  Parenthesis are used\n\
            for grouping.  For example, --bool '(A B)|C' matches lines with `A'\n\
            and `B', or lines with `C'.  Note that all subpatterns in a Boolean\n\
            query pattern are regular expressions, unless -F is specified.\n\
            Options -E, -F, -G, -P and -Z can be combined with --bool to match\n\
            subpatterns as strings or regular expressions (-E is the default.)\n\
            This option does not apply to -f FILE patterns.  Option --stats\n\
            displays the search patterns applied.  See also options --and,\n\
            --andnot, --not, --files and --lines.\n\
    --break\n\
            Adds a line break between results from different files.\n\
    -C NUM, --context=NUM\n\
            Output NUM lines of leading and trailing context surrounding each\n\
            matching line.  Places a --group-separator between contiguous\n\
            groups of matches.  If -o is specified, output the match with\n\
            context to fit NUM columns before and after the match or shortening\n\
            the match.  See also options -A, -B and -y.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            If -o or -u is specified, counts the number of patterns matched.\n\
            If -v is specified, counts the number of non-matching lines.\n\
    --color[=WHEN], --colour[=WHEN]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR or GREP_COLORS environment variable.  WHEN can be\n\
            `never', `always', or `auto', where `auto' marks up matches only\n\
            when output on a terminal.  The default is `auto'.\n\
    --colors=COLORS, --colours=COLORS\n\
            Use COLORS to mark up text.  COLORS is a colon-separated list of\n\
            one or more parameters `sl=' (selected line), `cx=' (context line),\n\
            `mt=' (matched text), `ms=' (match selected), `mc=' (match\n\
            context), `fn=' (file name), `ln=' (line number), `cn=' (column\n\
            number), `bn=' (byte offset), `se=' (separator).  Parameter values\n\
            are ANSI SGR color codes or `k' (black), `r' (red), `g' (green),\n\
            `y' (yellow), `b' (blue), `m' (magenta), `c' (cyan), `w' (white).\n\
            Upper case specifies background colors.  A `+' qualifies a color as\n\
            bright.  A foreground and a background color may be combined with\n\
            font properties `n' (normal), `f' (faint), `h' (highlight), `i'\n\
            (invert), `u' (underline).  Parameter `hl' enables file name\n\
            hyperlinks.  Parameter `rv' reverses the `sl=' and `cx=' parameters\n\
            with option -v.  Selectively overrides GREP_COLORS.\n\
    --config[=FILE], ---[FILE]\n\
            Use configuration FILE.  The default FILE is `.ugrep'.  The working\n\
            directory is checked first for FILE, then the home directory.  The\n\
            options specified in the configuration FILE are parsed first,\n\
            followed by the remaining options specified on the command line.\n\
    --confirm\n\
            Confirm actions in -Q query mode.  The default is confirm.\n\
    --cpp\n\
            Output file matches in C++.  See also options --format and -u.\n\
    --csv\n\
            Output file matches in CSV.  If -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -D ACTION, --devices=ACTION\n\
            If an input file is a device, FIFO or socket, use ACTION to process\n\
            it.  By default, ACTION is `skip', which means that devices are\n\
            silently skipped.  If ACTION is `read', devices read just as if\n\
            they were ordinary files.\n\
    -d ACTION, --directories=ACTION\n\
            If an input file is a directory, use ACTION to process it.  By\n\
            default, ACTION is `skip', i.e., silently skip directories unless\n\
            specified on the command line.  If ACTION is `read', warn when\n\
            directories are read as input.  If ACTION is `recurse', read all\n\
            files under each directory, recursively, following symbolic links\n\
            only if they are on the command line.  This is equivalent to the -r\n\
            option.  If ACTION is `dereference-recurse', read all files under\n\
            each directory, recursively, following symbolic links.  This is\n\
            equivalent to the -R option.\n\
    --depth=[MIN,][MAX], -1, -2, -3, ... -9, --10, --11, --12, ...\n\
            Restrict recursive searches from MIN to MAX directory levels deep,\n\
            where -1 (--depth=1) searches the specified path without recursing\n\
            into subdirectories.  Note that -3 -5, -3-5, and -35 search 3 to 5\n\
            levels deep.  Enables -r if -R or -r is not specified.\n\
    --dotall\n\
            Dot `.' in regular expressions matches anything, including newline.\n\
            Note that `.*' matches all input and should not be used.\n\
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
    --encoding=ENCODING\n\
            The encoding format of the input.  The default ENCODING is binary\n\
            and UTF-8 which are the same.  Note that option -U specifies binary\n\
            PATTERN matching (text matching is the default.)  ENCODING can be:\n\
           ";
  size_t k = 10;
  for (int i = 0; encoding_table[i].format != NULL; ++i)
  {
    size_t n = strlen(encoding_table[i].format);
    k += n + 4;
    out << (i == 0 ? "" : ",");
    if (k > 79)
    {
      out << "\n           ";
      k = 14 + n;
    }
    out << " `" << encoding_table[i].format << "'";
  }
  out << ".\n\
    --exclude=GLOB\n\
            Skip files whose name matches GLOB using wildcard matching, same as\n\
            -g ^GLOB.  GLOB can use **, *, ?, and [...] as wildcards and \\ to\n\
            quote a wildcard or backslash character literally.  When GLOB\n\
            contains a `/', full pathnames are matched.  Otherwise basenames\n\
            are matched.  When GLOB ends with a `/', directories are excluded\n\
            as if --exclude-dir is specified.  Otherwise files are excluded.\n\
            Note that --exclude patterns take priority over --include patterns.\n\
            GLOB should be quoted to prevent shell globbing.  This option may\n\
            be repeated.\n\
    --exclude-dir=GLOB\n\
            Exclude directories whose name matches GLOB from recursive\n\
            searches, same as -g ^GLOB/.  GLOB can use **, *, ?, and [...] as\n\
            wildcards and \\ to quote a wildcard or backslash character\n\
            literally.  When GLOB contains a `/', full pathnames are matched.\n\
            Otherwise basenames are matched.  Note that --exclude-dir patterns\n\
            take priority over --include-dir patterns.  GLOB should be quoted\n\
            to prevent shell globbing.  This option may be repeated.\n\
    --exclude-from=FILE\n\
            Read the globs from FILE and skip files and directories whose name\n\
            matches one or more globs.  A glob can use **, *, ?, and [...] as\n\
            wildcards and \\ to quote a wildcard or backslash character\n\
            literally.  When a glob contains a `/', full pathnames are matched.\n\
            Otherwise basenames are matched.  When a glob ends with a `/',\n\
            directories are excluded as if --exclude-dir is specified.\n\
            Otherwise files are excluded.  A glob starting with a `!' overrides\n\
            previously-specified exclusions by including matching files.  Lines\n\
            starting with a `#' and empty lines in FILE are ignored.  When FILE\n\
            is a `-', standard input is read.  This option may be repeated.\n\
    --exclude-fs=MOUNTS\n\
            Exclude file systems specified by MOUNTS from recursive searches,\n\
            MOUNTS is a comma-separated list of mount points or pathnames of\n\
            directories on file systems.  Note that --exclude-fs mounts take\n\
            priority over --include-fs mounts.  This option may be repeated.\n"
#ifndef HAVE_STATVFS
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
            "\
    -F, --fixed-strings\n\
            Interpret pattern as a set of fixed strings, separated by newlines,\n\
            any of which is to be matched.  This makes ugrep behave as fgrep.\n\
            If a PATTERN is specified, or -e PATTERN or -N PATTERN, then this\n\
            option has no effect on -f FILE patterns to allow -f FILE patterns\n\
            to narrow or widen the scope of the PATTERN search.\n\
    -f FILE, --file=FILE\n\
            Read newline-separated patterns from FILE.  White space in patterns\n\
            is significant.  Empty lines in FILE are ignored.  If FILE does not\n\
            exist, the GREP_PATH environment variable is used as path to FILE.\n"
#ifdef GREP_PATH
            "\
            If that fails, looks for FILE in " GREP_PATH ".\n"
#endif
            "\
            When FILE is a `-', standard input is read.  Empty files contain no\n\
            patterns; thus nothing is matched.  This option may be repeated.\n"
#ifndef OS_WIN
            "\
    --filter=COMMANDS\n\
            Filter files through the specified COMMANDS first before searching.\n\
            COMMANDS is a comma-separated list of `exts:command [option ...]',\n\
            where `exts' is a comma-separated list of filename extensions and\n\
            `command' is a filter utility.  The filter utility should read from\n\
            standard input and write to standard output.  Files matching one of\n\
            `exts' are filtered.  When `exts' is `*', files with non-matching\n\
            extensions are filtered.  One or more `option' separated by spacing\n\
            may be specified, which are passed verbatim to the command.  A `%'\n\
            as `option' expands into the pathname to search.  For example,\n\
            --filter='pdf:pdftotext % -' searches PDF files.  The `%' expands\n\
            into a `-' when searching standard input.  Option --label=.ext may\n\
            be used to specify extension `ext' when searching standard input.\n\
    --filter-magic-label=[+]LABEL:MAGIC\n\
            Associate LABEL with files whose signature \"magic bytes\" match the\n\
            MAGIC regex pattern.  Only files that have no filename extension\n\
            are labeled, unless +LABEL is specified.  When LABEL matches an\n\
            extension specified in --filter=COMMANDS, the corresponding command\n\
            is invoked.  This option may be repeated.\n"
#endif
            "\
    --format=FORMAT\n\
            Output FORMAT-formatted matches.  For example --format='%f:%n:%O%~'\n\
            outputs matching lines `%O' with filename `%f` and line number `%n'\n\
            followed by a newline `%~'.  If -P is specified, FORMAT may include\n\
            `%1' to `%9', `%[NUM]#' and `%[NAME]#' to output group captures.  A\n\
            `%%' outputs `%'.  See `ugrep --help format' and `man ugrep'\n\
            section FORMAT for details.  Context options -A, -B, -C and -y are\n\
            ignored.\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret patterns as basic regular expressions (BREs), i.e. make\n\
            ugrep behave as traditional grep.\n\
    -g GLOBS, --glob=GLOBS\n\
            Search only files whose name matches the specified comma-separated\n\
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.\n\
            When a `glob' is preceded by a `!' or a `^', skip files whose name\n\
            matches `glob', same as --exclude='glob'.  When `glob' contains a\n\
            `/', full pathnames are matched.  Otherwise basenames are matched.\n\
            When `glob' ends with a `/', directories are matched, same as\n\
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'\n\
            matches the working directory.  This option may be repeated and may\n\
            be combined with options -M, -O and -t to expand searches.  See\n\
            `ugrep --help globs' and `man ugrep' section GLOBBING for details.\n\
    --group-separator[=SEP]\n\
            Use SEP as a group separator for context options -A, -B and -C.\n\
            The default is a double hyphen (`--').\n\
    -H, --with-filename\n\
            Always print the filename with output lines.  This is the default\n\
            when there is more than one file to search.\n\
    -h, --no-filename\n\
            Never print filenames with output lines.  This is the default\n\
            when there is only one file (or only standard input) to search.\n\
    --heading, -+\n\
            Group matches per file.  Adds a heading and a line break between\n\
            results from different files.\n\
    --help [WHAT], -? [WHAT]\n\
            Display a help message, specifically on WHAT when specified.\n\
            In addition, `--help format' displays an overview of FORMAT fields,\n\
            `--help regex' displays an overview of regular expressions and\n\
            `--help globs' displays an overview of glob syntax and conventions.\n\
    --hexdump=[1-8][a][bch][A[NUM]][B[NUM]][C[NUM]]\n\
            Output matches in 1 to 8 columns of 8 hexadecimal octets.  The\n\
            default is 2 columns or 16 octets per line.  Option `a' outputs a\n\
            `*' for all hex lines that are identical to the previous hex line,\n\
            `b' removes all space breaks, `c' removes the character column, `h'\n\
            removes hex spacing, `A' includes up to NUM hex lines after the\n\
            match, `B' includes up to NUM hex lines before the match and `C'\n\
            includes up to NUM hex lines.  When NUM is omitted, the matching\n\
            line is included in the output.  See also options -U, -W and -X.\n\
    --hidden, -.\n\
            Search "
#ifdef OS_WIN
            "Windows system and "
#endif
            "hidden files and directories.\n\
    --hyperlink\n\
            Hyperlinks are enabled for file names when colors are enabled.\n\
            Same as --colors=hl.\n\
    -I, --ignore-binary\n\
            Ignore matches in binary files.  This option is equivalent to the\n\
            --binary-files=without-match option.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching.  By default, ugrep is case\n\
            sensitive.  By default, this option applies to ASCII letters only.\n\
            Use options -P and -i for Unicode case insensitive matching.\n\
    --ignore-files[=FILE]\n\
            Ignore files and directories matching the globs in each FILE that\n\
            is encountered in recursive searches.  The default FILE is\n\
            `" DEFAULT_IGNORE_FILE "'.  Matching files and directories located in the\n\
            directory of a FILE's location and in directories below are ignored\n\
            by temporarily extending the --exclude and --exclude-dir globs, as\n\
            if --exclude-from=FILE is locally enforced.  Globbing syntax is the\n\
            same as the --exclude-from=FILE gitignore syntax; directories are\n\
            excluded when the glob ends in a `/', same as git.  Files and\n\
            directories explicitly specified as command line arguments are\n\
            never ignored.  This option may be repeated with additional files.\n\
    --include=GLOB\n\
            Search only files whose name matches GLOB using wildcard matching,\n\
            same as -g GLOB.  GLOB can use **, *, ?, and [...] as wildcards and\n\
            \\ to quote a wildcard or backslash character literally.  When GLOB\n\
            contains a `/', full pathnames are matched.  Otherwise basenames\n\
            are matched.  When GLOB ends with a `/', directories are included\n\
            as if --include-dir is specified.  Otherwise files are included.\n\
            Note that --exclude patterns take priority over --include patterns.\n\
            GLOB should be quoted to prevent shell globbing.  This option may\n\
            be repeated.\n\
    --include-dir=GLOB\n\
            Only directories whose name matches GLOB are included in recursive\n\
            searches, same as -g GLOB/.  GLOB can use **, *, ?, and [...] as\n\
            wildcards and \\ to quote a wildcard or backslash character\n\
            literally.  When GLOB contains a `/', full pathnames are matched.\n\
            Otherwise basenames are matched.  Note that --exclude-dir patterns\n\
            take priority over --include-dir patterns.  GLOB should be quoted\n\
            to prevent shell globbing.  This option may be repeated.\n\
    --include-from=FILE\n\
            Read the globs from FILE and search only files and directories\n\
            whose name matches one or more globs.  A glob can use **, *, ?, and\n\
            [...] as wildcards and \\ to quote a wildcard or backslash\n\
            character literally.  When a glob contains a `/', full pathnames\n\
            are matched.  Otherwise basenames are matched.  When a glob ends\n\
            with a `/', directories are included as if --include-dir is\n\
            specified.  Otherwise files are included.  A glob starting with a\n\
            `!' overrides previously-specified inclusions by excluding matching\n\
            files.  Lines starting with a `#' and empty lines in FILE are\n\
            ignored.  When FILE is a `-', standard input is read.  This option\n\
            may be repeated.\n\
    --include-fs=MOUNTS\n\
            Only file systems specified by MOUNTS are included in recursive\n\
            searches.  MOUNTS is a comma-separated list of mount points or\n\
            pathnames of directories on file systems.  --include-fs=. restricts\n\
            recursive searches to the file system of the working directory\n\
            only.  Note that --exclude-fs mounts take priority over\n\
            --include-fs mounts.  This option may be repeated.\n"
#ifndef HAVE_STATVFS
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
            "\
    -J NUM, --jobs=NUM\n\
            Specifies the number of threads spawned to search files.  By\n\
            default an optimum number of threads is spawned to search files\n\
            simultaneously.  -J1 disables threading: files are searched in the\n\
            same order as specified.\n\
    -j, --smart-case\n\
            Perform case insensitive matching like option -i, unless a pattern\n\
            is specified with a literal ASCII upper case letter.\n\
    --json\n\
            Output file matches in JSON.  If -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -K [MIN,][MAX], --range=[MIN,][MAX], --min-line=MIN, --max-line=MAX\n\
            Start searching at line MIN, stop at line MAX when specified.\n\
    -k, --column-number\n\
            The column number of a matched pattern is displayed in front of the\n\
            respective matched line, starting at column 1.  Tabs are expanded\n\
            when columns are counted, see also option --tabs.\n\
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
    --label=LABEL\n\
            Displays the LABEL value when input is read from standard input\n\
            where a file name would normally be printed in the output.\n\
            Associates a filename extension with standard input when LABEL has\n\
            a suffix.  The default value is `(standard input)'.\n\
    --line-buffered\n\
            Force output to be line buffered instead of block buffered.\n\
    --lines\n\
            Apply Boolean queries to match lines, the opposite of --files.\n\
            This is the default Boolean query mode to match specific lines.\n\
    -M MAGIC, --file-magic=MAGIC\n\
            Only files matching the signature pattern MAGIC are searched.  The\n\
            signature \"magic bytes\" at the start of a file are compared to\n\
            the MAGIC regex pattern.  When matching, the file will be searched.\n\
            When MAGIC is preceded by a `!' or a `^', skip files with matching\n\
            MAGIC signatures.  This option may be repeated and may be combined\n\
            with options -O and -t to expand the search.  Every file on the\n\
            search path is read, making searches potentially more expensive.\n\
    -m [MIN,][MAX], --min-count=MIN, --max-count=MAX\n\
            Require MIN matches, stop after MAX matches when specified.  Output\n\
            MIN to MAX matches.  For example, -m1 outputs the first match and\n\
            -cm1, (with a comma) counts non-zero matches.  See also option -K.\n\
    --match\n\
            Match all input.  Same as specifying an empty pattern to search.\n\
    --max-files=NUM\n\
            Restrict the number of files matched to NUM.  Note that --sort or\n\
            -J1 may be specified to produce replicable results.  If --sort is\n\
            specified, the number of threads spawned is limited to NUM.\n\
    --mmap[=MAX]\n\
            Use memory maps to search files.  By default, memory maps are used\n\
            under certain conditions to improve performance.  When MAX is\n\
            specified, use up to MAX mmap memory per thread.\n\
    -N PATTERN, --neg-regexp=PATTERN\n\
            Specify a negative PATTERN used during the search of the input: an\n\
            input line is selected only if it matches the specified patterns\n\
            unless it matches the negative PATTERN.  Same as -e (?^PATTERN).\n\
            Negative pattern matches are essentially removed before any other\n\
            patterns are matched.  Note that longer patterns take precedence\n\
            over shorter patterns.  This option may be repeated.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B and -C.\n\
    --not [-e] PATTERN\n\
            Specifies that PATTERN should not match.  Note that -e A --not -e B\n\
            matches lines with `A' or lines without a `B'.  To match lines with\n\
            `A' that have no `B', specify -e A --andnot -e B.  Option --stats\n\
            displays the search patterns applied.  See also options --and,\n\
            --andnot, --bool, --files and --lines.\n\
    -O EXTENSIONS, --file-extension=EXTENSIONS\n\
            Search only files whose filename extensions match the specified\n\
            comma-separated list of EXTENSIONS, same as --include='*.ext' for\n\
            each `ext' in EXTENSIONS.  When an `ext' is preceded by a `!' or a\n\
            `^', skip files whose filename extensions matches `ext', same as\n\
            --exclude='*.ext'.  This option may be repeated and may be combined\n\
            with options -g, -M and -t to expand the recursive search.\n\
    -o, --only-matching\n\
            Output only the matching part of lines.  If -b, -k or -u is\n\
            specified, output each match on a separate line.  When multiple\n\
            lines match a pattern, output the matching lines with `|' as the\n\
            field separator.  If -A, -B or -C is specified, fits the match and\n\
            its context on a line within the specified number of columns.\n\
    --only-line-number\n\
            The line number of the matching line in the file is output without\n\
            displaying the match.  The line number counter is reset for each\n\
            file processed.\n\
    --files\n\
            Apply Boolean queries to match files, the opposite of --lines.  A\n\
            file matches if all Boolean conditions are satisfied by the lines\n\
            matched in the file.  For example, --files -e A --and -e B -e C\n\
            --andnot -e D matches a file if some lines match `A' and some lines\n\
            match (`B' or `C') and no line in the file matches `D'.  May also\n\
            be specified as --files --bool 'A B|C -D'.  Option -v cannot be\n\
            specified with --files.  See also options --and, --andnot, --not,\n\
            --bool and --lines.\n\
    -P, --perl-regexp\n\
            Interpret PATTERN as a Perl regular expression"
#if defined(HAVE_PCRE2)
            " using PCRE2.\n"
#elif defined(HAVE_BOOST_REGEX)
            " using Boost.Regex.\n"
#else
            ".\n\
            This option is not available in this build configuration of ugrep.\n"
#endif
            "\
            Note that Perl pattern matching differs from the default grep POSIX\n\
            pattern matching.\n\
    -p, --no-dereference\n\
            If -R or -r is specified, no symbolic links are followed, even when\n\
            they are specified on the command line.\n\
    --pager[=COMMAND]\n\
            When output is sent to the terminal, uses COMMAND to page through\n\
            the output.  The default COMMAND is `" DEFAULT_PAGER_COMMAND "'.  Enables --heading\n\
            and --line-buffered.\n\
    --pretty\n\
            When output is sent to a terminal, enables --color, --heading, -n,\n\
            --sort and -T when not explicitly disabled.\n\
    -Q[DELAY], --query[=DELAY]\n\
            Query mode: user interface to perform interactive searches.  This\n\
            mode requires an ANSI capable terminal.  An optional DELAY argument\n\
            may be specified to reduce or increase the response time to execute\n\
            searches after the last key press, in increments of 100ms, where\n\
            the default is 5 (0.5s delay).  No whitespace may be given between\n\
            -Q and its argument DELAY.  Initial patterns may be specified with\n\
            -e PATTERN, i.e. a PATTERN argument requires option -e.  Press F1\n\
            or CTRL-Z to view the help screen.  Press F2 or CTRL-Y to invoke a\n\
            command to view or edit the file shown at the top of the screen.\n\
            The command can be specified with option --view, or defaults to\n\
            environment variable PAGER if defined, or EDITOR.  Press Tab and\n\
            Shift-Tab to navigate directories and to select a file to search.\n\
            Press Enter to select lines to output.  Press ALT-l for option -l\n\
            to list files, ALT-n for -n, etc.  Non-option commands include\n\
            ALT-] to increase fuzziness and ALT-} to increase context.  Enables\n\
            --heading.  See also options --confirm and --view.\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress all output.  ugrep will only search until a\n\
            match has been found.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory.  Follow all\n\
            symbolic links, unlike -r.  See also option --sort.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are specified on the command line.  Note that\n\
            when no FILE arguments are specified and input is read from a\n\
            terminal, recursive searches are performed as if -r is specified.\n\
            See also option --sort.\n\
    --replace=FORMAT\n\
            Replace matching patterns in the output by the specified FORMAT\n\
            with `%' fields.  If -P is specified, FORMAT may include `%1' to\n\
            `%9', `%[NUM]#' and `%[NAME]#' to output group captures.  A `%%'\n\
            outputs `%' and `%~' outputs a newline.  See option --format,\n\
            `ugrep --help format' and `man ugrep' section FORMAT for details.\n\
    -S, --dereference\n\
            If -r is specified, all symbolic links are followed, like -R.  The\n\
            default is not to follow symbolic links to directories.\n\
    -s, --no-messages\n\
            Silent mode: nonexistent and unreadable files are ignored, i.e.\n\
            their error messages and warnings are suppressed.\n\
    --save-config[=FILE]\n\
            Save configuration FILE.  By default `.ugrep' is saved.  If FILE is\n\
            a `-', write the configuration to standard output.\n\
    --separator[=SEP]\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset and the matched line.  The default is a colon\n\
            (`:').\n\
    --sort[=KEY]\n\
            Displays matching files in the order specified by KEY in recursive\n\
            searches.  Normally the ug command sorts by name whereas the ugrep\n\
            batch command displays matches in no particular order to improve\n\
            performance.  The sort KEY can be `name' to sort by pathname\n\
            (default), `best' to sort by best match with option -Z (sort by\n\
            best match requires two passes over files, which is expensive),\n\
            `size' to sort by file size, `used' to sort by last access time,\n\
            `changed' to sort by last modification time and `created' to sort\n\
            by creation time.  Sorting is reversed with `rname', `rbest',\n\
            `rsize', `rused', `rchanged', or `rcreated'.  Archive contents are\n\
            not sorted.  Subdirectories are sorted and displayed after matching\n\
            files.  FILE arguments are searched in the same order as specified.\n\
    --stats\n\
            Output statistics on the number of files and directories searched\n\
            and the inclusion and exclusion constraints applied.\n\
    -T, --initial-tab\n\
            Add a tab space to separate the file name, line number, column\n\
            number and byte offset with the matched line.\n\
    -t TYPES, --file-type=TYPES\n\
            Search only files associated with TYPES, a comma-separated list of\n\
            file types.  Each file type corresponds to a set of filename\n\
            extensions passed to option -O and filenames passed to option -g.\n\
            For capitalized file types, the search is expanded to include files\n\
            with matching file signature magic bytes, as if passed to option\n\
            -M.  When a type is preceded by a `!' or a `^', excludes files of\n\
            the specified type.  This option may be repeated.  The possible\n\
            file types can be (where -tlist displays a detailed list):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    out << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  out << ".\n\
    --tabs[=NUM]\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1, 2, 4, or 8.  The default tab size is 8.\n\
    --tag[=TAG[,END]]\n\
            Disables colors to mark up matches with TAG.  END marks the end of\n\
            a match if specified, otherwise TAG.  The default is `___'.\n\
    -U, --binary\n\
            Disables Unicode matching for binary file matching, forcing PATTERN\n\
            to match bytes, not Unicode characters.  For example, -U '\\xa3'\n\
            matches byte A3 (hex) instead of the Unicode code point U+00A3\n\
            represented by the UTF-8 sequence C2 A3.  See also option --dotall.\n\
    -u, --ungroup\n\
            Do not group multiple pattern matches on the same matched line.\n\
            Output the matched line again for each additional pattern match,\n\
            using `+' as a separator.\n\
    -V, --version\n\
            Display version with linked libraries and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    --view[=COMMAND]\n\
            Use COMMAND to view/edit a file in query mode when pressing CTRL-Y.\n\
    -W, --with-hex\n\
            Output binary matches in hexadecimal, leaving text matches alone.\n\
            This option is equivalent to the --binary-files=with-hex option\n\
            with --hexdump=2C.  To omit the matching line from the hex output,\n\
            combine option --hexdump with option -W.  See also option -U.\n\
    -w, --word-regexp\n\
            The PATTERN is searched for as a word, such that the matching text\n\
            is preceded by a non-word character and is followed by a non-word\n\
            character.  Word characters are letters, digits and the\n\
            underscore.  With option -P, word characters are Unicode letters,\n\
            digits and underscore.  This option has no effect if -x is also\n\
            specified.  If a PATTERN is specified, or -e PATTERN or -N PATTERN,\n\
            then this option has no effect on -f FILE patterns to allow -f FILE\n\
            patterns to narrow or widen the scope of the PATTERN search.\n\
    --width[=NUM]\n\
            Truncate the output to NUM visible characters per line.  The width\n\
            of the terminal window is used if NUM is not specified.  Note that\n\
            double wide characters in the input may result in wider lines.\n\
    -X, --hex\n\
            Output matches in hexadecimal.  This option is equivalent to the\n\
            --binary-files=hex option with --hexdump=2C.  To omit the matching\n\
            line from the hex output, use option --hexdump instead of -X.  See\n\
            also option -U.\n\
    -x, --line-regexp\n\
            Select only those matches that exactly match the whole line, as if\n\
            the patterns are surrounded by ^ and $.  If a PATTERN is specified,\n\
            or -e PATTERN or -N PATTERN, then this option has no effect on\n\
            -f FILE patterns to allow -f FILE patterns to narrow or widen the\n\
            scope of the PATTERN search.\n\
    --xml\n\
            Output file matches in XML.  If -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -Y, --empty\n\
            Permits empty matches.  By default, empty matches are disabled,\n\
            unless a pattern begins with `^' or ends with `$'.  With this\n\
            option, empty-matching patterns such as x? and x*, match all input,\n\
            not only lines containing the character `x'.\n\
    -y, --any-line, --passthru\n\
            Any line is output (passthru).  Non-matching lines are output as\n\
            context with a `-' separator.  See also options -A, -B and -C.\n\
    -Z[best][+-~][MAX], --fuzzy=[best][+-~][MAX]\n\
            Fuzzy mode: report approximate pattern matches within MAX errors.\n\
            The default is -Z1: one deletion, insertion or substitution is\n\
            allowed.  If `+`, `-' and/or `~' is specified, then `+' allows\n\
            insertions, `-' allows deletions and `~' allows substitutions.  For\n\
            example, -Z+~3 allows up to three insertions or substitutions, but\n\
            no deletions.  If `best' is specified, then only the best matching\n\
            lines are output with the lowest cost per file.  Option -Zbest\n\
            requires two passes over a file and cannot be used with standard\n\
            input or Boolean queries.  Option --sort=best orders matching files\n\
            by best match.  The first character of an approximate match always\n\
            matches a character at the beginning of the pattern.  To fuzzy\n\
            match the first character, replace it with a `.' or `.?'.  Option\n\
            -U applies fuzzy matching to ASCII and bytes instead of Unicode\n\
            text.  No whitespace may be given between -Z and its argument.\n\
    -z, --decompress\n\
            Decompress files to search, when compressed.  Archives (.cpio,\n\
            .pax, .tar and .zip) and compressed archives (e.g. .taz, .tgz,\n\
            .tpz, .tbz, .tbz2, .tb2, .tz2, .tlz, .txz, .tzst) are searched and\n\
            matching pathnames of files in archives are output in braces.  If\n\
            -g, -O, -M, or -t is specified, searches files stored in archives\n\
            whose filenames match globs, match filename extensions, match file\n\
            signature magic bytes, or match file types, respectively.\n"
#ifndef HAVE_LIBZ
            "\
            This option is not available in this build configuration of ugrep.\n"
#else
            "\
            Supported compression formats: gzip (.gz), compress (.Z), zip"
#ifdef HAVE_LIBBZ2
            ",\n\
            bzip2 (requires suffix .bz, .bz2, .bzip2, .tbz, .tbz2, .tb2, .tz2)"
#endif
#ifdef HAVE_LIBLZMA
            ",\n\
            lzma and xz (requires suffix .lzma, .tlz, .xz, .txz)"
#endif
#ifdef HAVE_LIBLZ4
            ",\n\
            lz4 (requires suffix .lz4)"
#endif
#ifdef HAVE_LIBZSTD
            ",\n\
            zstd (requires suffix .zst, .zstd, .tzst)"
#endif
            ".\n"
#endif
            "\
    --zmax=NUM\n\
            When used with option -z (--decompress), searches the contents of\n\
            compressed files and archives stored within archives by up to NUM\n\
            recursive expansions.  The default --zmax=1 only permits searching\n\
            uncompressed files stored in cpio, pax, tar and zip archives;\n\
            compressed files and archives are detected as binary files and are\n\
            effectively ignored.  Specify --zmax=2 to search compressed files\n\
            and archives stored in cpio, pax, tar and zip archives.  NUM may\n\
            range from 1 to 99 for up to 99 decompression and de-archiving\n\
            steps.  Increasing NUM values gradually degrades performance.\n"
#ifndef WITH_DECOMPRESSION_THREAD
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
            "\
    -0, --null\n\
            Output a zero-byte (NUL) after the file name.  This option can be\n\
            used with commands such as `find -print0' and `xargs -0' to process\n\
            arbitrary file names.\n\
\n\
    Long options may start with `--no-' to disable, when applicable.\n\
\n\
    The ugrep utility exits with one of the following values:\n\
    0       One or more lines were selected.\n\
    1       No lines were selected.\n\
    >1      An error occurred.\n\
\n\
    If -q or --quiet or --silent is used and a line is selected, the exit\n\
    status is 0 even if an error occurred.\n\n";
}

// display helpful information for WHAT, if specified, and exit
void help(const char *what)
{
  if (what != NULL && *what == '=')
    ++what;

  if (what != NULL && strncmp(what, "--no", 4) == 0)
    what += 4;

  if (what == NULL || *what == '\0')
  {
    help(std::cout);
  }
  else
  {
    std::stringstream text;
    help(text);
    const std::string& str = text.str();

    int found = 0;

    for (int pass = 0; pass < 2; ++pass)
    {
      size_t pos = 0;

      while (true)
      {
        size_t end = str.find("\n    -", pos + 1);

        if (end == std::string::npos)
          end = str.find("\n\n", pos + 1);

        if (end == std::string::npos)
          break;

        size_t nl = str.find('\n', pos + 1);

        // roughly find a case-independent match of WHAT
        for (size_t i = pos + 5; i < (pass == 0 ? nl : end); ++i)
        {
          size_t j = 0;

          for (j = 0; what[j] != '\0'; ++j)
            if (((what[j] ^ str.at(i + j)) & ~0x20) != 0)
              break;

          if (what[j] == '\0')
          {
            if (pass == 0 ? i < nl: i > nl)
            {
              if (found == 0 && pass == 0)
                std::cout << "\nOptions and arguments:\n";
              else if (found == 1 && pass == 1)
                std::cout << "\n\nRelated options:\n";
              else if (found == 0)
                std::cout << "\nNo matching option, other relevant options:\n";

              std::cout << str.substr(pos, end - pos);
              found = pass + 1;
            }
            break;
          }
        }

        pos = end;
      }
    }

    if (found == 0)
      std::cout << "ugrep --help: nothing appropriate for " << what;

    std::cout << "\n\n";

    if (strcmp(what, "format") == 0)
    {
      std::cout <<
"FORMAT fields for --format and --replace:\n\
\n\
 field       output                      field       output\n\
 ----------  --------------------------  ----------  --------------------------\n\
 %a          basename                    %%          %\n\
 %b          byte offset                 %~          newline\n\
 %B %[...]B  ... + byte offset, if -b    %+          %F as heading/break, if -+\n\
 %c          matching pattern, as C/C++  %[...]<     ... if %m = 1\n\
 %C          matching line, as C/C++     %[...]>     ... if %m > 1\n\
 %d          byte size                   %,          , if %m > 1, same as %[,]>\n\
 %e          end offset                  %:          : if %m > 1, same as %[:]>\n\
 %f          pathname                    %;          ; if %m > 1, same as %[;]>\n\
 %F %[...]F  ... + pathname, if -H       %|          | if %m > 1, same as %[|]>\n\
 %h          \"pathname\"                  %[...]$     assign ... to separator\n\
 %H %[...]H  ... + \"pathname\", if -H     --------------------------------------\n\
 %j          matching pattern, as JSON   \n\
 %J          matching line, as JSON      \n\
 %k          line number                 \n\
 %K %[...]K  ... + column number, if -k  Fields that require -P for captures:\n\
 %m          match number                \n\
 %n          line number                 field       output\n\
 %N %[...]N  ... + line number, if -n    ----------  --------------------------\n\
 %o          matching pattern            %1 %2...%9  group capture\n\
 %O          matching line               %[n]#       nth group capture\n\
 %p          path                        %[n]b       nth capture byte offset\n\
 %q          quoted matching pattern     %[n]d       nth capture byte size\n\
 %Q          quoted matching line        %[n]e       nth capture end offset\n\
 %s          separator, : by default     %[name]#    named group capture\n\
 %S %[...]S  ... + separator, if %m > 1  %[name]b    named capture byte offset\n\
 %t          tab                         %[name]d    named capture byte size\n\
 %T %[...]T  ... + tab, if -T            %[name]e    named capture end offset\n\
 %u          unique lines, unless -u     %[n|...]#   capture n,... that matched\n\
 %v          matching pattern, as CSV    %[n|...]b   cpature n,... byte offset\n\
 %V          matching line, as CSV       %[n|...]d   cpature n,... byte size\n\
 %w          match width in wide chars   %[n|...]e   cpature n,... end offset\n\
 %x          matching pattern, as XML    %g          capture number or name\n\
 %X          matching line, as XML       %G          all capture numbers/names\n\
 %z          path in archive             %[t|...]g   text t indexed by capture\n\
 %Z          edit distance cost, if -Z   %[t|...]G   all t indexed by captures\n\
 --------------------------------------  --------------------------------------\n\
\n\
";
    }
    else if (strcmp(what, "regex") == 0)
    {
      std::cout <<
"Extended regular expression syntax overview (excludes some advanced patterns):\n\
\n\
 pattern     matches                     pattern     removes special meaning   \n\
 ----------  --------------------------  ----------  --------------------------\n\
 .           any character except \\n     \\.          escapes . to match .\n\
 a           the character a             \\Q...\\E     the literal string ...\n\
 ab          the string ab               --------------------------------------\n\
 a|b         a or b                      \n\
 a*          zero or more a's            pattern     special characters\n\
 a+          one or more a's             ----------  --------------------------\n\
 a?          zero or one a               \\f          form feed\n\
 a{3}        3 a's                       \\n          newline\n\
 a{3,}       3 or more a's               \\r          carriage return\n\
 a{3,7}      3 to 7 a's                  \\R          any Unicode line break\n\
 a*?         zero or more a's lazily     \\t          tab\n\
 a+?         one or more a's lazily      \\v          vertical tab\n\
 a??         zero or one a lazily        \\X          any character and \\n\n\
 a{3}?       3 a's lazily                \\cZ         control character ^Z\n\
 a{3,}?      3 or more a's lazily        \\0          NUL\n\
 a{3,7}?     3 to 7 a's lazily           \\0ddd       octal character code ddd\n\
 --------------------------------------  \\xhh        hex character code hh\n\
                                         \\x{hex}     hex Unicode code point\n\
 pattern     character classes           \\u{hex}     hex Unicode code point\n\
 ----------  --------------------------  --------------------------------------\n\
 [abc-e]     one character a,b,c,d,e     \n\
 [^abc-e]    one char not a,b,c,d,e,\\n   pattern     anchors and boundaries\n\
 [[:name:]]  one char in POSIX class:    ----------  --------------------------\n\
      alnum  a-z,A-Z,0-9                 ^           begin of line anchor\n\
      alpha  a-z,A-Z                     $           end of line anchor\n\
      ascii  ASCII char \\x00-\\x7f        \\b          word boundary (-P)\n\
      blank  space or tab                \\B          non-word boundary (-P)\n\
      cntrl  control characters          \\<          start word boundary (-P)\n\
      digit  0-9                         \\>          end word boundary (-P)\n\
      graph  visible characters          (?=...)     lookahead (-P)\n\
      lower  a-z                         (?!...)     negative lookahead (-P)\n\
      print  visible chars and space     (?<=...)    lookbehind (-P)\n\
      punct  punctuation characters      (?<!...)    negative lookbehind (-P)\n\
      space  space,\\t,\\n,\\v,\\f,\\r        --------------------------------------\n\
      upper  A-Z                         \n\
       word  a-z,A-Z,0-9,_               pattern     grouping\n\
     xdigit  0-9,a-f,A-F                 ----------  --------------------------\n\
 \\p{class}   one character in class      (...)       capturing group (-P)\n\
 \\P{class}   one char not in class       (...)       non-capturing group\n\
 \\d          a digit                     (?:...)     non-capturing group\n\
 \\D          a non-digit                 (?<X>...)   capturing, named X (-P)\n\
 \\h          a space or tab              \\1          matches group 1 (-P)\n\
 \\H          not a space or tab          \\g{10}      matches group 10 (-P)\n\
 \\s          a whitespace except \\n      \\g{X}       matches group name X (-P)\n\
 \\S          a non-whitespace            (?#...)     comments ... are ignored\n\
 \\w          a word character            --------------------------------------\n\
 \\W          a non-word character        \n\
 --------------------------------------      (-P): pattern requires option -P\n\
\n\
Option -P enables Perl regex matching, supporting Unicode patterns, Unicode\n\
word boundary matching (\\b, \\B, \\<, \\>), lookaheads and capturing groups.\n\
\n\
Option -U disables full Unicode pattern matching: non-POSIX Unicode character\n\
classes \\p{class} are disabled, ASCII, LATIN1 and binary regex patterns only.\n\
\n\
Option --bool adds the following operations to regex patterns `a' and `b':\n\
\n\
 pattern     operation                   pattern     operation\n\
 ----------  --------------------------  ----------  --------------------------\n\
 (...)       grouping                    \"...\"       the literal string ...\n\
 -a          logical not a               NOT a       locical not a\n\
 a|b         logical a or b              a OR b      logical a or b\n\
 a b         logical a and b             a AND b     locical a and b\n\
 --------------------------------------  --------------------------------------\n\
\n\
Listed from the highest level of precedence to the lowest, NOT is performed\n\
before OR and OR is performed before AND.  Thus, `-x|y z' is `((-x)|y) z'.\n\
\n\
Spacing with --bool logical operators and grouping is recommended.  Parenthesis\n\
become part of the regex sub-patterns when nested and when regex operators are\n\
directly applied to a parenthesized sub-expression.  For example, `((x y)z)'\n\
matches `x yz' and likewise `((x y){3} z)' match three `x y' and a `z'.\n\
\n\
The default is to match lines satisfying the --bool query.  To match files,\n\
use option --files with --bool.  See also options --and, --andnot, --not.\n\
\n\
Option --stats displays the options and patterns applied to the matching files.\n\
\n\
";
    }
    else if (strcmp(what, "globs") == 0)
    {
      std::cout <<
"Glob syntax and conventions:\n\
\n\
Gitignore-style globbing is performed by -g (--glob), --include, --exclude,\n\
--include-dir, --exclude-dir, --include-from, --exclude-from, --ignore-files.\n\
\n\
 pattern     matches\n\
 ----------  -----------------------------------------------------------\n\
 *           anything except /\n\
 ?           any one character except /\n\
 [abc-e]     one character a,b,c,d,e\n\
 [^abc-e]    one character not a,b,c,d,e,/\n\
 [!abc-e]    one character not a,b,c,d,e,/\n\
 /           when used at the start of a glob, matches working directory\n\
 **/         zero or more directories\n\
 /**         when at the end of a glob, matches everything after the /\n\
 \\?          a ? or any other character specified after the backslash\n\
\n\
When a glob pattern contains a /, the full pathname is matched.  Otherwise, the\n\
basename of a file or directory is matched in recursive searches.\n\
\n\
When a glob pattern begins with a /, files and directories are matched at the\n\
working directory, not recursively.\n\
\n\
When a glob pattern ends with a /, directories are matched instead of files.\n\
\n\
Option --stats displays the search path globs applied to the matching files.\n\
\n\
";
    }
    else if (strcmp(what, "fuzzy") == 0)
    {
      std::cout <<
"Fuzzy (approximate) search is performed with option -Z (no whitespace may be\n\
given between -Z and its argument when specified):\n\
\n\
 -Z    fuzzy match\n\
 ----  ------------------------------------------------------------------\n\
       allow 1 character insertion, deletion or substitution (-Z default)\n\
 2     allow 2 character insertions, deletions or substitutions\n\
 +2    allow 2 character insertions\n\
 -2    allow 2 character deletions\n\
 ~2    allow 2 character substitutions\n\
 +-2   allow 2 character insertions or deletions\n\
 +~2   allow 2 character insertions or substitutions\n\
 -~2   allow 2 character deletions or substitutions\n\
 best  when prefixed to the above, output the best matches only\n\
\n\
Insertions, deletions and/or substitutions are applied to Unicode characters,\n\
except when option -U is specified for binary/ASCII pattern search.\n\
\n\
The best matches are those that minimize the difference with the specified\n\
pattern.  This requires two passes over an input file, which reduces\n\
performance significantly.\n\
\n\
When multiple files are searched, option --sort=best sorts the files in a\n\
directory such that files with minimum differences with the specified pattern\n\
are listed first.  This requires two passes over each input file, which reduces\n\
performance significantly.\n\
\n\
Important:\n\
\n\
Fuzzy search anchors matches at the beginning character or characters of the\n\
specified regex pattern.  This significantly improves search performance and\n\
prevents overmatching.  For example, the regex pattern `[1-9]buzz' begins with\n\
a nonzero digit at which all matches are anchored.  Replace the beginning of a\n\
regex pattern with a wildcard to match any character, for example `.buzz', or\n\
make the first characters of the regex pattern optional, for example\n\
`[1-9]?buzz' or `.?buzz'.\n\
\n\
";
    }
  }

  exit(EXIT_ERROR);
}

// display version info
void version()
{
#if defined(HAVE_PCRE2)
  uint32_t tmp = 0;
#endif
  std::cout << "ugrep " UGREP_VERSION " " PLATFORM <<
#if defined(HAVE_AVX512BW)
    (reflex::have_HW_AVX512BW() ? " +avx512" : (reflex::have_HW_AVX2() ? " +avx2" : reflex::have_HW_SSE2() ?  " +sse2" : " (no sse2!)")) <<
#elif defined(HAVE_AVX2)
    (reflex::have_HW_AVX2() ? " +avx2" : reflex::have_HW_SSE2() ?  " +sse2" : " (no sse2!)") <<
#elif defined(HAVE_SSE2)
    (reflex::have_HW_SSE2() ?  " +sse2" : " (no sse2!)") <<
#elif defined(HAVE_NEON)
    " +neon/AArch64" <<
#endif
#if defined(HAVE_PCRE2)
    (pcre2_config(PCRE2_CONFIG_JIT, &tmp) >= 0 && tmp != 0 ? " +pcre2jit" : " +pcre2") <<
#elif defined(HAVE_BOOST_REGEX)
    " +boost_regex" <<
#endif
#ifdef HAVE_LIBZ
    " +zlib" <<
#ifdef HAVE_LIBBZ2
    " +bzip2" <<
#endif
#ifdef HAVE_LIBLZMA
    " +lzma" <<
#endif
#ifdef HAVE_LIBLZ4
    " +lz4" <<
#endif
#ifdef HAVE_LIBZSTD
    " +zstd" <<
#endif
#endif
    "\n"
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
  {
    fprintf(stderr, "%sugrep: %swarning:%s %scannot decompress %s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, pathname, color_off, color_message, message != NULL ? message : "", color_off);
    ++warnings;
  }
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
    fprintf(stderr, "%sugrep: %swarning:%s %s%s%s%s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, message != NULL ? message : "", message != NULL ? " " : "", arg != NULL ? arg : "", color_off, color_message, errmsg, color_off);
    ++warnings;
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
  fprintf(stderr, "%sugrep: %serror:%s %s%s%s%s:%s %s%s%s\n\n", color_off, color_error, color_off, color_high, message != NULL ? message : "", message != NULL ? " " : "", arg != NULL ? arg : "", color_off, color_message, errmsg, color_off);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message)
{
  fprintf(stderr, "%sugrep: %s%s%s\n\n", color_off, color_error, message, color_off);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message, const std::string& what)
{
  fprintf(stderr, "%sugrep: %s%s%s%s%s%s\n\n", color_off, color_error, message != NULL ? message : "", color_off, color_high, what.c_str(), color_off);
  exit(EXIT_ERROR);
}
