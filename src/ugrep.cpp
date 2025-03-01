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
@brief     file pattern searcher
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019,2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

User manual:

  <https://ugrep.com>

Source code repository:

  <https://github.com/Genivia/ugrep>

This program uses RE/flex:

  <https://github.com/Genivia/RE-flex>

Optional libraries to support options -P and -z:

  -P: PCRE2 (or Boost.Regex)    recommended
  -z: zlib (.gz)                recommended
  -z: bzip2 (.bz, bz2, .bzip2)  recommended
  -z: lzma (.lzma, .xz)         recommended
  -z: lz4 (.lz4)                optional
  -z: zstd (.zst, .zstd)        optional
  -z: brotli (.br)              optional
  -z: bzip3 (.bz3)              optional

Build ugrep as follows:

  $ ./configure
  $ make -j

Git does not preserve time stamps so ./configure may fail, in that case do:

  $ autoreconf -fi
  $ ./configure
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
#include <reflex/linematcher.h>
#include <reflex/fuzzymatcher.h>
#include <reflex/unicode.h>
#include <iomanip>
#include <cctype>
#include <limits>
#include <functional>
#include <list>
#include <deque>
#include <thread>
#include <memory>
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

// optionally enable brotli library for -z
// #define HAVE_LIBBROTLI

// optionally enable libbzip3 for -z
// #define HAVE_LIBBZIP3

#include <stringapiset.h>       // internationalization
#include <direct.h>             // directory access
#include <winsock.h>            // gethostname() for --hyperlink
#pragma comment(lib, "Ws2_32.lib")

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

// use zlib, libbz2, liblzma etc for option -z
#ifdef HAVE_LIBZ
# define WITH_MAX_7ZIP_SIZE 1073741824 // 7zip expands archived files in memory, we should limit expansion up to 1GB
# include "zstream.hpp"
#endif

// ugrep exit codes
#define EXIT_OK    0 // One or more lines were selected
#define EXIT_FAIL  1 // No lines were selected
#define EXIT_ERROR 2 // An error occurred

// limit the total number of threads spawn (i.e. limit spawn overhead), because grepping is practically IO bound
#ifndef DEFAULT_MAX_JOBS
# define DEFAULT_MAX_JOBS 12
#endif

// default limit on the job queue size to wait for worker threads to finish more work
#ifndef DEFAULT_MAX_JOB_QUEUE_SIZE
# define DEFAULT_MAX_JOB_QUEUE_SIZE 8192
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

char color_qp[COLORLEN]; // TUI prompt
char color_qe[COLORLEN]; // TUI errors
char color_qr[COLORLEN]; // TUI regex highlight
char color_qm[COLORLEN]; // TUI regex meta characters highlight
char color_ql[COLORLEN]; // TUI regex lists and literals highlight
char color_qb[COLORLEN]; // TUI regex braces highlight

char match_ms[COLORLEN];  // --match or --tag: matched text in a selected line
char match_mc[COLORLEN];  // --match or --tag: matched text in a context line
char match_off[COLORLEN]; // --match or --tag: off

const char *color_hl      = NULL; // hyperlink
const char *color_st      = NULL; // ST

const char *color_del     = ""; // erase line after the cursor
const char *color_off     = ""; // disable colors

const char *color_high    = ""; // stderr highlighted text
const char *color_error   = ""; // stderr error text
const char *color_warning = ""; // stderr warning text
const char *color_message = ""; // stderr error or warning message text

#ifndef OS_WIN

// output file stat is available when stat() result is true
bool output_stat_result  = false;
bool output_stat_regular = false;
struct stat output_stat;

// container of inodes to detect directory cycles when symlinks are traversed with --dereference
std::set<ino_t> visited;

// use statvfs() for --include-fs and --exclude-fs
#if defined(HAVE_STATVFS)

// containers of file system ids to exclude from recursive searches or include in recursive searches
std::set<uint64_t> exclude_fs_ids, include_fs_ids;

// the type of statvfs buffer to use
typedef struct statvfs stat_fs_t;

// wrapper to call statvfs()
inline int stat_fs(const char *path, stat_fs_t *buf)
{
  return statvfs(path, buf);
}

#if defined(_AIX) || defined(__TOS_AIX__)
// NetBSD compatible struct fsid_t with unsigned integers f_fsid.val[2]
inline uint64_t fsid_to_uint64(fsid_t& fsid)
{
  return static_cast<uint64_t>(fsid.val[0]) | (static_cast<uint64_t>(fsid.val[1]) << 32);
}
#else
// POSIX compliant unsigned integer f_fsid
template<typename T>
inline uint64_t fsid_to_uint64(T& fsid)
{
  return static_cast<uint64_t>(fsid);
}
#endif

// if statvfs() is not available then use statfs() for --include-fs and --exclude-fs
#elif defined(HAVE_STATFS)

// containers of file system ids to exclude from recursive searches or include in recursive searches
std::set<uint64_t> exclude_fs_ids, include_fs_ids;

// the type of statfs buffer to use
typedef struct statfs stat_fs_t;

// wrapper to call statfs()
inline int stat_fs(const char *path, stat_fs_t *buf)
{
  return statfs(path, buf);
}

inline uint64_t fsid_to_uint64(fsid_t& fsid)
{
  return static_cast<uint64_t>(fsid.val[0]) | (static_cast<uint64_t>(fsid.val[1]) << 32);
}

#endif

#endif

// ugrep command-line options
bool flag_all_threads              = false;
bool flag_any_line                 = false;
bool flag_basic_regexp             = false;
bool flag_best_match               = false;
bool flag_bool                     = false;
bool flag_color_term               = false;
bool flag_confirm                  = DEFAULT_CONFIRM;
bool flag_count                    = false;
bool flag_cpp                      = false;
bool flag_csv                      = false;
bool flag_decompress               = false;
bool flag_dereference              = false;
bool flag_dereference_files        = false;
bool flag_files                    = false;
bool flag_files_with_matches       = false;
bool flag_files_without_match      = false;
bool flag_fixed_strings            = false;
bool flag_glob_ignore_case         = false;
bool flag_grep                     = false;
bool flag_hex                      = false;
bool flag_hex_star                 = false;
bool flag_hex_cbr                  = true;
bool flag_hex_chr                  = true;
bool flag_hex_hbr                  = true;
bool flag_hidden                   = DEFAULT_HIDDEN;
bool flag_hyperlink_line           = false;
bool flag_invert_match             = false;
bool flag_json                     = false;
bool flag_line_buffered            = false;
bool flag_line_regexp              = false;
bool flag_match                    = false;
bool flag_multiline                = false;
bool flag_no_dereference           = false;
bool flag_no_filename              = false;
bool flag_no_header                = false;
bool flag_no_messages              = false;
bool flag_not                      = false;
bool flag_null                     = false;
bool flag_null_data                = false;
bool flag_only_line_number         = false;
bool flag_only_matching            = false;
bool flag_perl_regexp              = false;
bool flag_query                    = false;
bool flag_quiet                    = false;
bool flag_sort_rev                 = false;
bool flag_split                    = false;
bool flag_stdin                    = false;
bool flag_tty_term                 = false;
bool flag_usage_warnings           = false;
bool flag_word_regexp              = false;
bool flag_xml                      = false;
bool flag_with_hex                 = false;
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
Flag flag_tree;
Flag flag_ungroup;
Sort flag_sort_key                 = Sort::NA;
Action flag_devices_action         = Action::UNSP;
Action flag_directories_action     = Action::UNSP;
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_delay                  = DEFAULT_QUERY_DELAY;
size_t flag_exclude_iglob_size     = 0;
size_t flag_exclude_iglob_dir_size = 0;
size_t flag_fuzzy                  = 0;
size_t flag_hex_after              = 0;
size_t flag_hex_before             = 0;
size_t flag_hex_columns            = 16;
size_t flag_include_iglob_size     = 0;
size_t flag_include_iglob_dir_size = 0;
size_t flag_jobs                   = 0;
size_t flag_max_count              = 0;
size_t flag_max_depth              = 0;
size_t flag_max_files              = 0;
size_t flag_max_line               = 0;
size_t flag_max_mmap               = DEFAULT_MAX_MMAP_SIZE;
size_t flag_max_queue              = DEFAULT_MAX_JOB_QUEUE_SIZE;
size_t flag_min_count              = 0;
size_t flag_min_depth              = 0;
size_t flag_min_line               = 0;
size_t flag_min_magic              = 1;
size_t flag_min_steal              = MIN_STEAL;
size_t flag_not_magic              = 0;
size_t flag_tabs                   = DEFAULT_TABS;
size_t flag_width                  = 0;
size_t flag_zmax                   = 1;
const char *flag_binary_files      = "binary";
const char *flag_color             = DEFAULT_COLOR;
const char *flag_color_query       = NULL;
const char *flag_colors            = NULL;
const char *flag_config            = NULL;
const char *flag_devices           = NULL;
const char *flag_directories       = NULL;
const char *flag_encoding          = NULL;
const char *flag_format            = NULL;
const char *flag_format_begin      = NULL;
const char *flag_format_close      = NULL;
const char *flag_format_end        = NULL;
const char *flag_format_open       = NULL;
const char *flag_group_separator   = "--";
const char *flag_hexdump           = NULL;
const char *flag_hyperlink         = NULL;
const char *flag_index             = NULL;
const char *flag_label             = Static::LABEL_STANDARD_INPUT;
const char *flag_pager             = NULL;
const char *flag_pretty            = DEFAULT_PRETTY;
const char *flag_replace           = NULL;
const char *flag_save_config       = NULL;
const char *flag_separator         = NULL;
const char *flag_separator_dash    = "-";
const char *flag_separator_bar     = "|";
const char *flag_sort              = NULL;
const char *flag_stats             = NULL;
const char *flag_tag               = NULL;
const char *flag_view              = "";
std::string              flag_filter;
std::string              flag_hyperlink_prefix;
std::string              flag_hyperlink_host;
std::string              flag_hyperlink_path;
std::string              flag_regexp;
std::set<std::string>    flag_config_files;
std::set<std::string>    flag_ignore_files;
std::vector<std::string> flag_file;
std::vector<std::string> flag_file_type;
std::vector<std::string> flag_file_extension;
std::vector<std::string> flag_file_magic;
std::vector<std::string> flag_filter_magic_label;
std::vector<std::string> flag_glob;
std::vector<std::string> flag_iglob;
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

// store string arguments and the UTF-8 arguments decoded from wargv[] in strings to re-populate argv[] with pointers
std::list<std::string> arg_strings;

// helper function protos
void options(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int argc, const char **argv);
void option_regexp(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg, bool is_neg = false);
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv);
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg);
void option_all_files();
void init(int argc, const char **argv);
void set_color(const char *colors, const char *parameter, char color[COLORLEN]);
void trim(std::string& line);
void trim_pathname_arg(const char *arg);
bool is_output(ino_t inode);
const char *getoptarg(int argc, const char **argv, const char *arg, int& i);
const char *getloptarg(int argc, const char **argv, const char *arg, int& i);
const char *strarg(const char *string);
size_t strtonum(const char *string, const char *message);
size_t strtopos(const char *string, const char *message);
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message);
size_t strtofuzzy(const char *string, const char *message);
void import_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs, bool gitignore = false);
void usage(const char *message, const char *arg = NULL, const char *valid = NULL);
void help(std::ostream& out);
void help(const char *what = NULL);
void version();
void is_directory(const char *pathname);
void cannot_decompress(const char *pathname, const char *message);
void open_pager();
void close_pager();

#ifdef OS_WIN

// CTRL-C handler
BOOL WINAPI sigint(DWORD signal)
{
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
  {
    // be nice, reset colors on interrupt when sending output to a color TTY
    if (flag_color_term)
      flag_color_term = write(1, "\033[m", 3) > 0; // appease -Wunused-result

    close_pager();
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
  if (flag_color_term)
    flag_color_term = write(1, "\033[m", 3) > 0; // appease -Wunused-result

  close_pager();

  // signal again
  kill(getpid(), sig);
}

#endif

// set this thread's affinity and priority, if supported by the OS, ignore errors to leave scheduling to the OS
static void set_this_thread_affinity_and_priority(size_t cpu)
{
  // set affinity

#if defined(OS_WIN) || defined(__CYGWIN__)

  (void)SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu);

#elif defined(__APPLE__) && defined(HAVE_PTHREAD_SET_QOS_CLASS_SELF_NP)

  (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);

#elif defined(__FreeBSD__) && defined(HAVE_CPUSET_SETAFFINITY)

  cpuset_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  (void)cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset), &cpuset);

#elif defined(__DragonFly__) && defined(HAVE_PTHREAD_SETAFFINITY_NP)

  cpuset_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  (void)pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

#elif defined(__NetBSD__) && defined(HAVE_PTHREAD_SETAFFINITY_NP)

  cpuset_t *cpuset = cpuset_create();
  cpuset_set(cpu, cpuset);
  (void)pthread_setaffinity_np(pthread_self(), cpuset_size(cpuset), cpuset);
  cpuset_destroy(cpuset);

#elif defined(HAVE_SCHED_SETAFFINITY)

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  (void)sched_setaffinity(0, sizeof(cpuset), &cpuset);

#elif defined(HAVE_CPUSET_SETAFFINITY)

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  (void)cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, sizeof(cpuset), &cpuset);

#elif defined(HAVE_PTHREAD_SETAFFINITY_NP)

  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);
  (void)pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

#endif

  // set priority

#if defined(OS_WIN) || defined(__CYGWIN__)

  (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

#elif defined(__APPLE__)

  (void)setpriority(PRIO_DARWIN_THREAD, 0, -20);

#elif defined(HAVE_PTHREAD_SETSCHEDPRIO)

  (void)pthread_setschedprio(pthread_self(), -20);

#elif defined(HAVE_SETPRIORITY)

  (void)setpriority(PRIO_PROCESS, 0, -20);

#endif

  (void)cpu;
}

// open a pager if output is to a TTY then page through the results
void open_pager()
{
  if (flag_pager != NULL)
  {
    // --pager without argument uses $PAGER or DEFAULT_PAGER_COMMAND
    if (*flag_pager == '\0')
    {
      const char *pager = getenv("PAGER");

      if (pager != NULL && *pager != '\0')
        flag_pager = pager;
      else
        flag_pager = DEFAULT_PAGER_COMMAND;
    }

    // pager 'less' requires -R
    if (strcmp(flag_pager, "less") == 0)
      flag_pager = "less -R";

    // open a pipe to a forked pager
#ifdef OS_WIN
    Static::output = popen(flag_pager, "wb");
#else
    Static::output = popen(flag_pager, "w");
#endif
    if (Static::output == NULL)
      error("cannot open pipe to pager", flag_pager);

    Static::errout = Static::output;

    // enable --heading if not explicitly disabled (enables --break later)
    if (flag_heading.is_undefined())
      flag_heading = true;

    // enable --line-buffered to flush output to the pager immediately
    flag_line_buffered = true;
  }
}

// close the pipe to the forked pager
void close_pager()
{
  if (flag_pager != NULL && Static::output != NULL && Static::output != stdout)
    pclose(Static::output);
}

// open a file where - means stdin/stdout and an initial ~ expands to home directory
int fopen_smart(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;

  if (filename == NULL || *filename == '\0')
    return errno = ENOENT;

  if (strcmp(filename, "-") == 0)
  {
    *file = strchr(mode, 'w') == NULL ? stdin : stdout;
    return 0;
  }

  if (*filename == '~')
    return fopenw_s(file, std::string(Static::home_dir).append(filename + 1).c_str(), mode);

  return fopenw_s(file, filename, mode);
}

// read a line from buffered input, returns true when eof
inline bool getline(reflex::BufferedInput& input, std::string& line)
{
  int ch;

  line.erase();

  while ((ch = input.get()) != EOF && ch != '\n')
    line.push_back(ch);

  if (!line.empty() && line.back() == '\r')
    line.pop_back();

  return ch == EOF && line.empty();
}

// read a line from mmap memory, returns true when eof
inline bool getline(const char *& here, size_t& left)
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
inline bool getline(const char *& here, size_t& left, reflex::BufferedInput& buffered_input, reflex::Input& input, std::string& line)
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

// return true if s[0..n-1] contains a \0 (NUL) or a non-displayable invalid UTF-8 encoding
inline bool is_binary(const char *s, size_t n)
{
  // not --null-data or --encoding=null-data that permit NUL in the input and non-UTF-8 like GNU grep
  if (flag_encoding_type == reflex::Input::file_encoding::null_data)
    return false;

  // not -a and -U or -W: file is binary if it has a \0 (NUL) or an invalid UTF-8 encoding
  if (!flag_text && (!flag_binary || flag_with_hex))
    return !reflex::isutf8(s, s + n);

  // otherwise, file is binary if it contains a \0 (NUL) which is what GNU grep checks
  return memchr(s, '\0', n) != NULL;
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
  size_t len = std::min<size_t>(strlen(from), COLORLEN - 1);

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

  Zthread(bool is_chained, std::string& partname) :
      ztchain(NULL),
      zstream(NULL),
      zpipe_in(NULL),
      is_chained(is_chained),
      quit(false),
      stop(false),
      is_extracting(false),
      is_waiting(false),
      is_assigned(false),
      partnameref(partname),
      findpart(NULL)
  {
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;
  }

  ~Zthread()
  {
    // recursively join all stages of the decompression thread chain (--zmax>1), delete zstream
    join();

    // delete the decompression chain (--zmax>1)
    if (ztchain != NULL)
    {
      delete ztchain;
      ztchain = NULL;
    }
  }

  // start decompression thread if not running, open new pipe, returns pipe or NULL on failure, this function is called by the main thread
  FILE *start(size_t ztstage, const char *pathname, FILE *file_in, const char *find = NULL)
  {
    // return pipe
    FILE *pipe_in = NULL;

    // reset pipe descriptors, pipe is closed
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;

    // partnameref is not assigned yet, used only when this decompression thread is chained
    is_assigned = false;

    // if there is a specific part to search in a (nested) archive, NULL otherwise
    findpart = find;

    // a colon separator means we should find the part in a nested archive corresponding to a decompressor depth (stage)
    if (findpart != NULL)
    {
      for (size_t strip_prefix = 1; strip_prefix < ztstage; ++strip_prefix)
      {
        const char *colon = strchr(findpart, ':');
        if (colon == NULL)
          break;

        // this nested archive part to find is after the colon
        findpart = colon + 1;
      }
    }

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
        zpipe_in = ztchain->start(ztstage - 1, pathname, file_in, find);
        if (zpipe_in == NULL)
          return NULL;

        // wait for the partname to be assigned by the next decompression thread in the decompression chain
        std::unique_lock<std::mutex> lock(ztchain->pipe_mutex);
        if (!ztchain->is_assigned)
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
        // wake decompression thread waiting in close_wait_zstream_open(), there is work to do
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
          is_extracting = false;
          is_waiting = false;

          thread = std::thread(&Zthread::decompress, this);
        }

        catch (std::system_error&)
        {
          // thread creation failed
          fclose(pipe_in);
          close(pipe_fd[1]);
          pipe_fd[0] = -1;
          pipe_fd[1] = -1;

          warning("cannot create thread to decompress", pathname);

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

      warning("cannot create pipe to decompress", pathname);

      return NULL;
    }

    return pipe_in;
  }

  // open pipe to the next file or part in the archive or return NULL, this function is called by the main thread or by the previous decompression thread
  FILE *open_next(const char *pathname)
  {
    if (pipe_fd[0] != -1)
    {
      // our end of the pipe was closed earlier, before open_next() was called
      pipe_fd[0] = -1;

      // if extracting and the decompression filter thread is not yet waiting, then wait until decompression thread closed its end of the pipe
      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (!is_waiting)
        pipe_close.wait(lock);
      lock.unlock();

      // partnameref is not assigned yet, used only when this decompression thread is chained
      is_assigned = false;

      // extract the next file from the archive when applicable, e.g. zip format
      if (is_extracting)
      {
        FILE *pipe_in = NULL;

        // open pipe between worker and decompression thread, then start decompression thread
        if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "rb")) != NULL)
        {
          if (is_chained)
          {
            // use lock and wait for partname ready
            std::unique_lock<std::mutex> lock(pipe_mutex);
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();
            // wait for the partname to be set by the next decompression thread in the ztchain
            if (!is_assigned)
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
        warning("cannot create pipe to decompress", is_chained ? NULL : pathname);

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

        // when an error occurred, we still need to notify the receiver in case it is waiting on the partname
        std::unique_lock<std::mutex> lock(pipe_mutex);
        is_assigned = true;
        part_ready.notify_one();
        lock.unlock();
      }
    }

    return NULL;
  }

  // cancel decompression gracefully
  void cancel()
  {
    stop = true;

    // recursively cancel decompression threads in the chain
    if (ztchain != NULL)
      ztchain->cancel();
  }

  // join this thread, this function is called by the main thread
  void join()
  {
    // --zmax>1: recursively join all stages of the decompression thread chain
    if (ztchain != NULL)
      ztchain->join();

    if (thread.joinable())
    {
      std::unique_lock<std::mutex> lock(pipe_mutex);

      // decompression thread should quit to join
      quit = true;

      if (!is_waiting)
      {
        // wait until decompression thread closes the pipe
        pipe_close.wait(lock);
      }
      else
      {
        // wake decompression thread waiting in close_wait_zstream_open(), there is no more work to do
        pipe_zstrm.notify_one();
      }

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

  // if the pipe was closed, then wait until the main thread opens a new pipe to search the next file or part in an archive
  bool wait_pipe_ready()
  {
    if (pipe_fd[1] == -1)
    {
      // signal close and wait until a new zstream pipe is ready
      std::unique_lock<std::mutex> lock(pipe_mutex);
      pipe_close.notify_one();
      is_waiting = true;
      pipe_ready.wait(lock);
      is_waiting = false;
      lock.unlock();

      // the receiver did not create a new pipe in close_file()
      if (pipe_fd[1] == -1)
        return false;
    }

    return true;
  }

  // close the pipe and wait until the main thread opens a new zstream and pipe for the next decompression job, unless quitting
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
      is_waiting = true;
      pipe_zstrm.wait(lock);
      is_waiting = false;
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
      is_extracting = false;
      is_waiting = false;

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
          is_extracting = true;

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

        bool is_selected = false;

        // decompress a block of data into the buffer
        std::streamsize len = zstream->decompress(buf, maxlen);

        if (len >= 0)
        {
          is_selected = true;

          if (!filter_tar(path, buf, maxlen, len, is_selected) &&
              !filter_cpio(path, buf, maxlen, len, is_selected))
          {
            // not a tar/cpio file, decompress the data into pipe, if not unzipping or if zipped file meets selection criteria
            is_selected = is_regular && (zipinfo == NULL || select_matching(NULL, path.c_str(), buf, static_cast<size_t>(len), true));

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
              if (is_chained)
              {
                std::unique_lock<std::mutex> lock(pipe_mutex);
                is_assigned = true;
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
              {
                // if no next decompression thread and decompressing a single file (not zip), then stop immediately
                if (ztchain == NULL && zipinfo == NULL)
                  break;

                drain = true;
              }

              // decompress the next block of data into the buffer
              len = zstream->decompress(buf, maxlen);
            }
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
        is_extracting = true;

        // after extracting files from an archive, close our end of the pipe and loop for the next file
        if (is_selected && pipe_fd[1] != -1)
        {
          close(pipe_fd[1]);
          pipe_fd[1] = -1;
        }
      }

      is_extracting = false;

      // when an error occurred or nothing was selected, then we still need to notify the receiver in case it is waiting on the partname
      if (is_chained)
      {
        std::unique_lock<std::mutex> lock(pipe_mutex);
        is_assigned = true;
        part_ready.notify_one();
        lock.unlock();
      }

      // close the pipe and wait until zstream pipe is open, unless quitting
      close_wait_zstream_open();
    }
  }

  // if tar/pax file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_tar(const std::string& archive, unsigned char *buf, size_t maxlen, std::streamsize len, bool& is_selected)
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

      // is this a tar/pax archive?
      if (is_ustar || is_gnutar)
      {
        // produce headers with tar file pathnames for each archived part (Grep::partname)
        if (!flag_no_filename)
          flag_no_header = false;

        // inform the main grep thread we are extracting an archive
        is_extracting = true;

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

          // check gnutar size with leading byte 0x80 (unsigned positive) or leading byte 0xff (negative)
          uint64_t size = 0;
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
          size_t minlen = static_cast<size_t>(std::min<uint64_t>(len, size)); // size_t is OK: len is streamsize but non-negative
          is_selected = select_matching(archive.c_str(), path.c_str(), buf, minlen, is_regular);

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
              if (!archive.empty())
                partnameref.assign(partname).append(":").append(archive).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!archive.empty())
                partnameref.assign(archive).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // notify the receiver of the new partname after wait_pipe_ready()
            if (is_chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              is_assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          while (len > 0 && !stop)
          {
            size_t len_out = static_cast<size_t>(std::min<uint64_t>(len, size)); // size_t is OK: len is streamsize but non-negative

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
              memmove(buf, buf + len_out, static_cast<size_t>(len)); // size_t is OK: len is streamsize but non-negative

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

            is_selected = false;
          }
        }

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (is_chained)
        {
          std::unique_lock<std::mutex> lock(pipe_mutex);
          is_assigned = true;
          part_ready.notify_one();
          lock.unlock();
        }

        // done extracting the tar file
        return true;
      }
    }

    // not a tar file
    return false;
  }

  // if cpio file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_cpio(const std::string& archive, unsigned char *buf, size_t maxlen, std::streamsize len, bool& is_selected)
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
        is_extracting = true;

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
          char *rest = tmp;

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
          if (*rest != '\0')
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
          if (*rest != '\0')
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
          if (*rest != '\0')
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
            size_t n = std::min<size_t>(len, size);
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
          size_t minlen = std::min<size_t>(len, filesize);
          is_selected = select_matching(archive.c_str(), path.c_str(), buf, minlen, is_regular);

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
              break;

            // assign the Grep::partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
            if (ztchain != NULL)
            {
              if (!archive.empty())
                partnameref.assign(partname).append(":").append(archive).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!archive.empty())
                partnameref.assign(archive).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // notify the receiver of the new partname
            if (is_chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              is_assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          size = filesize;

          while (len > 0 && !stop)
          {
            size_t len_out = std::min<size_t>(len, size); // size_t is OK: len is streamsize but non-negative

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
            is_selected = false;
          }
        }

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (is_chained)
        {
          std::unique_lock<std::mutex> lock(pipe_mutex);
          is_assigned = true;
          part_ready.notify_one();
          lock.unlock();
        }

        // done extracting the cpio file
        return true;
      }
    }

    // not a cpio file
    return false;
  }

  // true if path matches search constraints or buf contains matching magic bytes
  bool select_matching(const char *archive, const char *path, const unsigned char *buf, size_t len, bool is_regular)
  {
    bool is_selected = is_regular;

    if (is_selected)
    {
      // select one specific archive part to search, up to a colon when present
      if (findpart != NULL)
      {
        const char *colon = strchr(findpart, ':');

        // empty archive name means no nested archive, coding artifact
        if (archive != NULL && *archive == '\0')
          archive = NULL;

        // check if this is a nested archive that is also in findpart up to the first colon
        if (archive != NULL)
          if (colon == NULL || strncmp(archive, findpart, colon - findpart) != 0 || archive[colon - findpart] != '\0')
            return false;

        const char *start = findpart;

        // if this is not a nested archive, but path is a nested archive, then compare to findpart up to the first colon
        if (archive == NULL && colon != NULL)
          return strncmp(path, start, colon - start) == 0 && path[colon - start] == '\0';

        // path to compare to starts after the colon of the archive in findpart
        if (colon != NULL)
        {
          start = colon + 1;
          colon = strchr(start, ':');

          // now compare the path between the colons in findpart
          if (colon != NULL)
            return strncmp(path, start, colon - start) == 0 && path[colon - start] == '\0';
        }

        return strcmp(path, start) == 0;
      }

      // extract the basename from the path
      const char *basename = strrchr(path, '/');
      if (basename == NULL)
        basename = path;
      else
        ++basename;

      if (*basename == '.' && !flag_hidden)
        return false;

      // exclude files whose basename matches any one of the --exclude globs
      for (const auto& glob : flag_all_exclude)
      {
        bool ignore_case = &glob < &flag_all_exclude.front() + flag_exclude_iglob_size;
        if (glob_match(path, basename, glob.c_str(), ignore_case))
          return false;
      }

      // include files whose basename matches any one of the --include globs
      for (const auto& glob : flag_all_include)
      {
        bool ignore_case = &glob < &flag_all_include.front() + flag_include_iglob_size;
        if ((is_selected = glob_match(path, basename, glob.c_str(), ignore_case)))
          break;
      }

      // -M: check magic bytes, requires sufficiently large len of buf[] to match patterns, which is fine when Z_BUF_LEN is large e.g. 64K to contain all magic bytes
      if (buf != NULL && !flag_file_magic.empty() && (flag_all_include.empty() || !is_selected))
      {
        // create a matcher to match the magic pattern, we cannot use magic_matcher because it is not thread safe
        reflex::Matcher magic(Static::magic_pattern);
        magic.buffer(const_cast<char*>(reinterpret_cast<const char*>(buf)), len + 1);
        size_t match = magic.scan();
        is_selected = match == flag_not_magic || match >= flag_min_magic;
      }
    }

    return is_selected;
  }

  Zthread                *ztchain;       // chain of decompression threads to decompress multi-compressed/archived files
  zstreambuf             *zstream;       // the decompressed stream buffer from compressed input
  FILE                   *zpipe_in;      // input pipe from the next ztchain stage, if any
  std::thread             thread;        // decompression thread handle
  bool                    is_chained;    // true if decompression thread is chained before another decompression thread
  std::atomic_bool        quit;          // true if decompression thread should terminate to exit the program
  std::atomic_bool        stop;          // true if decompression thread should stop (cancel search)
  volatile bool           is_extracting; // true if extracting files from TAR or ZIP archive (no concurrent r/w)
  volatile bool           is_waiting;    // true if decompression thread is waiting (no concurrent r/w)
  volatile bool           is_assigned;   // true when partnameref was assigned
  int                     pipe_fd[2];    // decompressed stream pipe
  std::mutex              pipe_mutex;    // mutex to extract files in thread
  std::condition_variable pipe_zstrm;    // cv to control new pipe creation
  std::condition_variable pipe_ready;    // cv to control new pipe creation
  std::condition_variable pipe_close;    // cv to control new pipe creation
  std::condition_variable part_ready;    // cv to control new partname creation to pass along decompression chains
  std::string             partname;      // name of the archive part extracted by the next decompressor in the ztchain
  std::string&            partnameref;   // reference to the partname of Grep or of the previous decompressor
  const char             *findpart;      // when non-NULL, select a specific part in an archive to search

};

#endif
#endif

// grep manages output, matcher, input, and decompression
struct Grep {

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

#ifdef OS_WIN

    // get modification time (micro seconds) from directory entry
    static uint64_t modified_time(const WIN32_FIND_DATAW& ffd)
    {
      const struct _FILETIME& time = ffd.ftLastWriteTime;
      return static_cast<uint64_t>(time.dwLowDateTime) | (static_cast<uint64_t>(time.dwHighDateTime) << 32);
    }

    // get modification time (micro seconds) from file handle
    static int64_t modified_time(const HANDLE& hFile)
    {
      struct _FILETIME time;
      GetFileTime(hFile, NULL, NULL, &time);
      return static_cast<uint64_t>(time.dwLowDateTime) | (static_cast<uint64_t>(time.dwHighDateTime) << 32);
    }

#else

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

    // get modification time (micro seconds) from stat buf
    static uint64_t modified_time(const struct stat& buf)
    {
#if defined(HAVE_STAT_ST_ATIM) && defined(HAVE_STAT_ST_MTIM) && defined(HAVE_STAT_ST_CTIM)
      // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
      return static_cast<uint64_t>(static_cast<uint64_t>(buf.st_mtim.tv_sec) * 1000000 + buf.st_mtim.tv_nsec / 1000);
#elif defined(HAVE_STAT_ST_ATIMESPEC) && defined(HAVE_STAT_ST_MTIMESPEC) && defined(HAVE_STAT_ST_CTIMESPEC)
      // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
      return static_cast<uint64_t>(static_cast<uint64_t>(buf.st_mtimespec.tv_sec) * 1000000 + buf.st_mtimespec.tv_nsec / 1000);
#else
      return static_cast<uint64_t>(buf.st_mtime);
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
        pathname(pathname != Static::LABEL_STANDARD_INPUT ? pathname : ""), // empty pathname means stdin
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

#ifdef WITH_LOCK_FREE_JOB_QUEUE

  // a lock-free job queue for one producer and one consumer with a bounded circular buffer
  struct JobQueue {

    JobQueue()
      :
        head(ring),
        tail(ring),
        todo(0)
    { }

    bool empty() const
    {
      return head.load() == tail.load();
    }

    // add a sentinel NONE job to the queue
    void enqueue()
    {
      enqueue("", Entry::UNDEFINED_COST, Job::NONE);
    }

    // add a job to the queue
    void enqueue(const char *pathname, uint16_t cost, size_t slot)
    {
      Job *job = tail.load();
      Job *next = job + 1;
      if (next == &ring[DEFAULT_MAX_JOB_QUEUE_SIZE])
        next = ring;

      while (next == head.load())
      {
        // we must lock and wait until the buffer is not full
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_full.wait(lock);
        lock.unlock();
      }

      job->pathname.assign(pathname);
      job->cost = cost;
      job->slot = slot;
      tail.store(next);
      ++todo;
      queue_data.notify_one();
    }

    // try to add a job to the queue if the queue is not too large
    bool try_enqueue(const char *pathname, uint16_t cost, size_t slot)
    {
      Job *job = tail.load();
      Job *next = job + 1;
      if (next == &ring[DEFAULT_MAX_JOB_QUEUE_SIZE])
        next = ring;

      if (next == head.load())
        return false;

      job->pathname.assign(pathname);
      job->cost = cost;
      job->slot = slot;
      tail.store(next);
      ++todo;
      queue_data.notify_one();

      return true;
    }

    // pop a job
    void dequeue(Job& job)
    {
      while (empty())
      {
        // we must lock and wait until the buffer is not empty
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_data.wait(lock);
        lock.unlock();
      }

      Job *next = head.load() + 1;
      if (next == &ring[DEFAULT_MAX_JOB_QUEUE_SIZE])
        next = ring;

      job = *head.load();
      head.store(next);
      --todo;
      queue_full.notify_one();
    }

    Job                     ring[DEFAULT_MAX_JOB_QUEUE_SIZE];
    std::atomic<Job*>       head;
    std::atomic<Job*>       tail;
    std::mutex              queue_mutex; // job queue mutex used when queue is empty or full
    std::condition_variable queue_data;  // cv to control the job queue
    std::condition_variable queue_full;  // cv to control the job queue
    std::atomic_size_t      todo;        // number of jobs in the queue
  };

#else

  // a job queue
  struct JobQueue : public std::deque<Job> {

    JobQueue()
      :
        todo(0)
    { }

    // add a sentinel NONE job to the queue
    void enqueue()
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      emplace_back();
      ++todo;
      lock.unlock();

      queue_work.notify_one();
    }

    // add a job to the queue
    void enqueue(const char *pathname, uint16_t cost, size_t slot)
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      emplace_back(pathname, cost, slot);
      ++todo;
      lock.unlock();

      queue_work.notify_one();
    }

    // pop a job
    void dequeue(Job& job)
    {
      std::unique_lock<std::mutex> lock(queue_mutex);

      while (empty())
        queue_work.wait(lock);

      job = front();
      pop_front();
      --todo;

      // if we popped a Job::NONE sentinel but the queue has some jobs, then move the sentinel to the back of the queue
      if (job.none() && !empty())
      {
        emplace_back();
        job = front();
        pop_front();
      }

      lock.unlock();
    }

    // steal a job from this worker, if at least --min-steal jobs to do, returns true if successful
    bool steal_job(Job& job)
    {
      std::unique_lock<std::mutex> lock(queue_mutex);

      if (empty())
        return false;

      job = front();

      // we cannot steal a Job::NONE sentinel
      if (job.none())
        return false;

      pop_front();
      --todo;

      lock.unlock();

      return true;
    }

    // move a stolen job to this worker, maintaining job slot order
    void move_job(Job& job)
    {
      bool inserted = false;

      std::unique_lock<std::mutex> lock(queue_mutex);

      // insert job in the small queue to maintain job order
      const auto e = end();
      for (auto j = begin(); j != e; ++j)
      {
        if (j->slot > job.slot)
        {
          insert(j, std::move(job));
          inserted = true;
          break;
        }
      }

      if (!inserted)
        emplace_back(std::move(job));

      ++todo;

      lock.unlock();

      queue_work.notify_one();
    }

    std::mutex              queue_mutex; // job queue mutex
    std::condition_variable queue_work;  // cv to control the job queue
    std::atomic_size_t      todo;        // number of jobs in the queue, atomic for job stealing
  };

#endif

#ifndef OS_WIN

  // extend the reflex::Input::Handler to handle nonblocking stdin from a TTY or from a slow pipe
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

    GrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        grep(grep),
        pathname(pathname),
        lineno(lineno),
        heading(heading),
        binfile(binfile),
        hex(hex),
        binary(binary),
        matches(matches),
        stop(stop)
    { }

    Grep&         grep;     // grep object
    const char *& pathname; // grep::search argument pathname
    size_t&       lineno;   // grep::search lineno local variable
    bool&         heading;  // grep::search heading local variable
    bool&         binfile;  // grep::search binfile local variable
    bool&         hex;      // grep::search hex local variable
    bool&         binary;   // grep::search binary local variable
    size_t&       matches;  // grep::search matches local variable
    bool&         stop;     // grep::search stop local variable

    // define a reflex::AbstractMatcher functor to handle buffer shifts by moving restline string from the buffer to a temporary string buffer
    virtual void operator()(reflex::AbstractMatcher&, const char*, size_t, size_t) override
    {
      // if the rest of the line was not output yet, then save it in a temporary string buffer to output later
      if (grep.restline_data != NULL)
      {
        grep.restline.assign(grep.restline_data, grep.restline_size);
        grep.restline_data = grep.restline.c_str();
      }
    }

    // get the start of the after/before context, if present
    void begin_before(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num, const char *& ptr, size_t& size, size_t& offset)
    {
      ptr = NULL;
      size = 0;
      offset = 0;

      // if no gap to shift then return (this also happens when the buffer grows)
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
    void next_before(const char *buf, size_t len, size_t num, const char *& ptr, size_t& size, size_t& offset)
    {
      if (ptr == NULL)
        return;

      const char *pos = ptr + size;
      const char *end = buf + len;

      if (pos >= end)
      {
        ptr = NULL;
      }
      else
      {
        const char *eol = static_cast<const char*>(memchr(pos, '\n', end - pos));

        if (eol == NULL)
          eol = buf + len;
        else
          ++eol;

        ptr = pos;
        size = eol - pos;
        offset = pos - buf + num;

        ++lineno;
      }
    }
  };

  // extend event GrepHandler to output invert match lines for -v
  struct InvertMatchGrepHandler : public GrepHandler {

    InvertMatchGrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, heading, binfile, hex, binary, matches, stop)
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

        binary = binary || flag_hex || (flag_with_hex && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, heading, lineno, NULL, offset, flag_separator, binary);

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

    FormatInvertMatchGrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, heading, binfile, hex, binary, matches, stop)
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
          // --format-open or --format-close: we must out.acquire() lock before Stats::found_part()
          if (flag_format_open != NULL || flag_format_close != NULL)
            grep.out.acquire();

          // --max-files: max reached?
          if (!Stats::found_part())
          {
            stop = true;
            break;
          }

          if (flag_format_open != NULL)
            grep.out.format(flag_format_open, pathname, grep.partname, Stats::found_parts(), NULL, &matcher, heading, false, Stats::found_parts() > 1);
        }

        // --max-count: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        // output --format
        grep.out.format_invert(flag_format, pathname, grep.partname, matches, lineno, offset, ptr, size - (size > 0 && ptr[size - 1] == '\n'), heading, matches > 1);

        next_before(buf, len, num, ptr, size, offset);
      }
    }
  };

  // extend event GrepHandler to output any context lines for -y
  struct AnyLineGrepHandler : public GrepHandler {

    AnyLineGrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, heading, binfile, hex, binary, matches, stop)
    { }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // output the rest of the matching line before the context lines
      if (grep.restline_data != NULL)
      {
        if (lineno != matcher.lineno() || flag_ungroup)
        {
          if (binary)
          {
            grep.out.dump.hex(flag_invert_match ? Output::Dump::HEX_CONTEXT_LINE : Output::Dump::HEX_LINE, grep.restline_last, grep.restline_data, grep.restline_size);
            grep.out.dump.done();
          }
          else
          {
            bool lf_only = false;
            if (grep.restline_size > 0)
            {
              lf_only = grep.restline_data[grep.restline_size - 1] == '\n';
              grep.restline_size -= lf_only;
              if (grep.restline_size > 0)
              {
                grep.out.str(flag_invert_match ? color_cx : color_sl);
                grep.out.str(grep.restline_data, grep.restline_size);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }

          grep.restline_data = NULL;
        }
        else
        {
          // save restline before the buffer shifts
          GrepHandler::operator()(matcher, buf, len, num);
        }
      }

      // context colors with or without -v
      short v_hex_context_line = flag_invert_match ? Output::Dump::HEX_LINE : Output::Dump::HEX_CONTEXT_LINE;
      const char *v_color_cx = flag_invert_match ? color_sl : color_cx;
      const char *separator = flag_invert_match ? flag_separator : flag_separator_dash;

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

        binary = binary || flag_hex || (flag_with_hex && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, heading, lineno, NULL, offset, separator, binary);

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
  };

  // extend event AnyLineGrepHandler to output specific context lines for -ABC without -v
  struct ContextGrepHandler : public AnyLineGrepHandler {

    // context state to track context lines before and after a match
    struct ContextState {

      struct Line {

        Line() :
          binary(false),
          offset(0),
          ptr(NULL),
          size(0)
        { }

        bool        binary; // before context binary line
        size_t      offset; // before context offset of line
        const char* ptr;    // before context pointer to line
        size_t      size;   // before context length of the line
        std::string line;   // before context line data saved from ptr with size bytes

      };

      typedef std::vector<Line> Lines;

      ContextState()
        :
          before_index(0),
          before_length(0),
          after_lineno(0),
          after_length(flag_after_context)
      {
        before_lines.resize(flag_before_context);
      }

      size_t before_index;  // before context rotation index
      size_t before_length; // accumulated length of the before context
      Lines  before_lines;  // before context line data
      size_t after_lineno;  // after context line number
      size_t after_length;  // accumulated length of the after context

    };

    ContextGrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        AnyLineGrepHandler(grep, pathname, lineno, heading, binfile, hex, binary, matches, stop)
    { }

    // output the before context
    void output_before_context()
    {
      // the group separator indicates lines skipped between contexts, like GNU grep
      if (state.after_lineno > 0 && state.after_lineno + state.after_length < grep.matcher->lineno() - state.before_length)
      {
        if (hex)
          grep.out.dump.done();

        if (flag_group_separator != NULL)
        {
          if (flag_query && !flag_text)
          {
            grep.out.chr('\0');
            grep.out.str(color_se);
            grep.out.chr('\0');
            grep.out.str(flag_group_separator);
            grep.out.chr('\0');
          }
          else
          {
            grep.out.str(color_se);
            grep.out.str(flag_group_separator);
          }
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
          const ContextState::Line& before_line = state.before_lines[j];

          if (hex && !before_line.binary)
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, heading, before_lineno + i, NULL, before_line.offset, flag_separator_dash, before_line.binary);

          hex = before_line.binary;

          // lines are always saved at the moment, so ptr is NULL, but we may optimize this in the future to not always save
          const char *ptr = before_line.ptr != NULL ? before_line.ptr : before_line.line.c_str();
          size_t size = before_line.size;

          if (hex)
          {
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, before_line.offset, ptr, size);
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

    // set the after context to the current line + 1
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

      // if we only need the before context, then look for it right before the current lineno
      if (state.after_length >= flag_after_context)
      {
        size_t current = matcher.lineno();
        if (lineno + flag_before_context + 1 < current)
           lineno = current - flag_before_context - 1;
      }

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // output the rest of the matching line before the context lines
      if (grep.restline_data != NULL)
      {
        if (lineno != matcher.lineno() || flag_ungroup)
        {
          if (binary)
          {
            grep.out.dump.hex(Output::Dump::HEX_LINE, grep.restline_last, grep.restline_data, grep.restline_size);
          }
          else
          {
            bool lf_only = false;
            if (grep.restline_size > 0)
            {
              lf_only = grep.restline_data[grep.restline_size - 1] == '\n';
              grep.restline_size -= lf_only;
              if (grep.restline_size > 0)
              {
                grep.out.str(color_sl);
                grep.out.str(grep.restline_data, grep.restline_size);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }

          grep.restline_data = NULL;
        }
        else
        {
          // save restline before the buffer shifts
          GrepHandler::operator()(matcher, buf, len, num);
        }
      }

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (flag_with_hex && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (state.after_lineno > 0 && state.after_length < flag_after_context)
        {
          ++state.after_length;

          if (hex && !binary)
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, heading, lineno, NULL, offset, flag_separator_dash, binary);

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
          state.before_lines[state.before_index].binary = binary;
          state.before_lines[state.before_index].offset = offset;
          state.before_lines[state.before_index].ptr    = ptr;
          state.before_lines[state.before_index].size   = size;

          ++state.before_index;
        }
        else
        {
          break;
        }

        next_before(buf, len, num, ptr, size, offset);
      }

      // save the (new or additional) before lines that become inaccessible after shift or buffer growth
      for (size_t i = 0; i < state.before_length; ++i)
      {
        if (state.before_lines[i].ptr != NULL)
        {
          state.before_lines[i].line.assign(state.before_lines[i].ptr, state.before_lines[i].size);
          state.before_lines[i].ptr = NULL;
        }
      }
    }

    ContextState state;

  };

  // extend event AnyLineGrepHandler to output specific context lines for -ABC with -v
  struct InvertContextGrepHandler : public AnyLineGrepHandler {

    // context state to track matching lines before non-matching lines
    struct InvertContextState {

      struct Match {

        Match(size_t pos, size_t size, size_t offset)
          :
            pos(pos),
            size(size),
            offset(offset)
        { }

        size_t pos;    // position on the line
        size_t size;   // size of the match
        size_t offset; // byte offset of the match

      };

      typedef std::vector<Match> Matches;

      struct Line {

        Line()
          :
            binary(false),
            columno(0),
            offset(0)
        { }

        bool        binary;  // before context binary line
        size_t      columno; // before context column number of first match
        size_t      offset;  // before context offset of first match
        std::string line;    // before context line data
        Matches     matches; // before context matches per line

      };

      typedef std::vector<Line> Lines;

      InvertContextState()
        :
          before_index(0),
          before_length(0),
          after_lineno(0)
      {
        before_lines.resize(flag_before_context);
      }

      size_t before_index;  // before context rotation index
      size_t before_length; // accumulated length of the before context
      Lines  before_lines;  // before context line data
      size_t after_lineno;  // the after context line number

    };

    InvertContextGrepHandler(Grep& grep, const char *& pathname, size_t& lineno, bool& heading, bool& binfile, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        AnyLineGrepHandler(grep, pathname, lineno, heading, binfile, hex, binary, matches, stop)
    { }

    // output the inverted before context
    void output_before_context()
    {
      // the group separator indicates lines skipped between contexts, like GNU grep
      if (state.after_lineno > 0 && state.after_lineno + flag_after_context + flag_before_context < lineno && flag_group_separator != NULL)
      {
        if (hex)
          grep.out.dump.done();

        if (flag_query && !flag_text)
        {
          grep.out.chr('\0');
          grep.out.str(color_se);
          grep.out.chr('\0');
          grep.out.str(flag_group_separator);
          grep.out.chr('\0');
        }
        else
        {
          grep.out.str(color_se);
          grep.out.str(flag_group_separator);
        }
        grep.out.str(color_off);
        grep.out.nl();
      }

      // output the inverted before context
      if (state.before_length > 0)
      {
        // the first line number of the before context
        size_t before_lineno = lineno - state.before_length;

        for (size_t i = 0; i < state.before_length; ++i)
        {
          size_t j = (state.before_index + i) % state.before_length;
          const InvertContextState::Line& before_line = state.before_lines[j];
          size_t offset = before_line.matches.empty() ? before_line.offset : before_line.matches.front().offset;

          if (hex && !before_line.binary)
            grep.out.dump.done();

          if (!flag_no_header)
            grep.out.header(pathname, grep.partname, heading, before_lineno + i, NULL, offset, flag_separator_dash, before_line.binary);

          hex = before_line.binary;

          const char *ptr = before_line.line.c_str();
          size_t size = before_line.line.size();
          size_t pos = 0;

          for (const auto& match : before_line.matches)
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
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, before_line.offset + pos, ptr + pos, size - pos);
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
      state.after_lineno = lineno + 1;
    }

    // add context line with the first match to the inverted before context
    void add_before_context_line(const char *bol, const char *eol, size_t columno, size_t offset)
    {
      if (state.before_length < flag_before_context)
        ++state.before_length;

      state.before_index %= state.before_length;

      state.before_lines[state.before_index].binary  = binary;
      state.before_lines[state.before_index].columno = columno;
      state.before_lines[state.before_index].offset  = offset;
      state.before_lines[state.before_index].line.assign(bol, eol - bol);
      state.before_lines[state.before_index].matches.clear();

      ++state.before_index;
    }

    // add match fragment to the inverted before context
    void add_before_context_match(size_t pos, size_t size, size_t offset)
    {
      // only add a match if we have a before line, i.e. not an after line with a multiline match
      if (state.before_length > 0)
      {
        size_t index = (state.before_index + state.before_length - 1) % state.before_length;
        state.before_lines[index].matches.emplace_back(pos, size, offset);
      }
    }

    // functor invoked by the reflex::AbstractMatcher when the buffer contents are shifted out, also called explicitly in grep::search
    virtual void operator()(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num) override
    {
      const char *ptr;
      size_t size;
      size_t offset;

      begin_before(matcher, buf, len, num, ptr, size, offset);

      // output the rest of the "after" matching line
      if (grep.restline_data != NULL)
      {
        if (lineno != matcher.lineno() || flag_ungroup)
        {
          if (binary)
          {
            grep.out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, grep.restline_last, grep.restline_data, grep.restline_size);
          }
          else
          {
            bool lf_only = false;
            if (grep.restline_size > 0)
            {
              lf_only = grep.restline_data[grep.restline_size - 1] == '\n';
              grep.restline_size -= lf_only;
              if (grep.restline_size > 0)
              {
                grep.out.str(color_cx);
                grep.out.str(grep.restline_data, grep.restline_size);
                grep.out.str(color_off);
              }
            }
            grep.out.nl(lf_only);
          }

          grep.restline_data = NULL;
        }
        else
        {
          // save restline before the buffer shifts
          GrepHandler::operator()(matcher, buf, len, num);
        }
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
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (flag_with_hex && is_binary(ptr, size));

        if (binfile || (binary && !flag_hex && !flag_with_hex))
          break;

        if (hex && !binary)
          grep.out.dump.done();

        if (!flag_no_header)
          grep.out.header(pathname, grep.partname, heading, lineno, NULL, offset, flag_separator, binary);

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

  Grep(FILE *file, reflex::AbstractMatcher *matcher, Static::Matchers *matchers)
    :
      filename(NULL),
      restline_data(NULL),
      restline_size(0),
      restline_last(0),
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
  { }

  virtual ~Grep()
  {
#ifdef HAVE_LIBZ
#ifndef WITH_DECOMPRESSION_THREAD
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

  // search a file or archive
  virtual void search(const char *pathname, uint16_t cost);

  // search input after lineno to populate a string vector with the matching line and lines after up to max lines
  void find_text_preview(const char *filename, const char *partname, size_t from_lineno, size_t max, size_t& lineno, size_t& num, std::vector<std::string>& text);

  // extract a part from an archive and send to a stream
  void extract(const char *filename, const char *partname, FILE *output);

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
  bool open_file(const char *pathname, const char *find = NULL)
  {
    if (pathname == Static::LABEL_STANDARD_INPUT)
    {
      if (Static::source == NULL)
        return false;

      pathname = flag_label;
      file_in = Static::source;

#ifdef OS_WIN
      (void)_setmode(fileno(Static::source), _O_BINARY);
#endif
    }
    else if (fopenw_s(&file_in, pathname, "rb") != 0)
    {
      warning("cannot read", pathname);

      return false;
    }

#if !defined(OS_WIN) && !defined(__APPLE__)
    if (file_in != stdin && file_in != Static::source)
    {
      // recursive searches and -Dskip should not block on devices and special "empty" regular files like in /proc and /sys
      if (flag_directories_action == Action::RECURSE || flag_devices_action != Action::READ)
      {
        int fd = fileno(file_in);
        int fl = fcntl(fd, F_GETFL);
        if (fl >= 0)
          fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        else
          clearerr(file_in);
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

      // start decompression thread if not running, get pipe with decompressed input
      FILE *pipe_in = zthread.start(flag_zmax, pathname, file_in, find);
      if (pipe_in == NULL)
      {
        fclose(file_in);
        file_in = NULL;

        return false;
      }

      input = reflex::Input(pipe_in, flag_encoding_type);

#else
      (void)find; // appease -Wunused

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
    {
      input = reflex::Input(file_in, flag_encoding_type);
    }
#else
    (void)find; // appease -Wunused
    input = reflex::Input(file_in, flag_encoding_type);
#endif

    return true;
  }

  // --filter: return true on success, create a pipe to replace file input if filtering files in a forked process
  bool filter(FILE*& in, const char *pathname)
  {
    if (!flag_filter.empty() && in != NULL)
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
          size_t match = reflex::Matcher(Static::filter_magic_pattern, in).scan();

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

      const char *command = flag_filter.c_str();
      const char *default_command = NULL;

      // find the command corresponding to the first matching suffix specified in the filter
      while (true)
      {
        while (isspace(static_cast<unsigned char>(*command)))
          ++command;

        // wildcard *:command is considered only when no matching suffix was found
        if (*command == '*')
          default_command = strchr(command, ':');

        // match filter filename extension (case sensitive)
        if (strncmp(suffix, command, sep) == 0 && (command[sep] == ':' || command[sep] == ',' || isspace(static_cast<unsigned char>(command[sep]))))
        {
          command = strchr(command, ':');
          break;
        }

        command = strchr(command, ',');
        if (command == NULL)
          break;

        ++command;
      }

      // if no matching command, use the wildcard *:command when specified
      if (command == NULL)
        command = default_command;

      // suffix has a command to execute
      if (command != NULL)
      {
        // skip over the ':'
        ++command;

        int fd[2];

#ifdef OS_WIN
        // Windows CreateProcess requires an "inherited" pipe handle specific to Windows
        bool ok = (pipe_inherit(fd) == 0);
#else
        bool ok = (pipe(fd) == 0);
#endif

        if (ok)
        {
#ifdef OS_WIN

          std::wstring wcommand(utf8_decode(command));
          size_t pathname_pos = 0;

          // replace all % by the pathname, except when quoted
          while (true)
          {
            size_t size = wcommand.size();

            for (; pathname_pos < size && wcommand[pathname_pos] != L'%'; ++pathname_pos)
              if (wcommand[pathname_pos] == L'"')
                while (++pathname_pos < size && wcommand[pathname_pos] != L'"')
                  continue;

            if (pathname_pos >= size)
              break;

            std::wstring wpathname(utf8_decode(in == stdin ? "-" : pathname));
            wcommand.replace(pathname_pos, 1, wpathname);
            pathname_pos += wpathname.size();
          }

          // set up inherited stdin, stdout and stderr for the child process
          STARTUPINFOW si;
          memset(&si, 0, sizeof(STARTUPINFOW));
          si.cb = sizeof(STARTUPINFOW);
          si.dwFlags = STARTF_USESTDHANDLES;
          si.hStdInput = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(in)));
          si.hStdOutput = reinterpret_cast<HANDLE>(_get_osfhandle(fd[1]));
          if (!flag_quiet && !flag_no_messages)
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
          if (in != stdin)
            SetHandleInformation(si.hStdInput, HANDLE_FLAG_INHERIT, 1);

          PROCESS_INFORMATION pi;
          memset(&pi, 0, sizeof(PROCESS_INFORMATION));

          // use buffer to allow CreateProcessW to change the command and arguments to pass them as argc argv
          wchar_t *wbuffer = new wchar_t[wcommand.size() + 1];
          wcscpy(wbuffer, wcommand.c_str());

          if (CreateProcessW(NULL, wbuffer, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
          {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
          }
          else
          {
            const char *end;

            for (end = command; *end != '\0' && *end != ','; ++end)
              if (*end == '"')
                while (*++end != '\0' && *end != '"')
                  continue;

            std::string arg(command, end - command);
            errno = GetLastError();
            warning("--filter: cannot create process for command", arg.c_str());
          }

          delete[] wbuffer;

#else

          int pid;

          // fork to execute the specified --filter utility command on the input to produce filtered output
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

            // -q or -q: suppress error messages sent to stderr by the filter command by redirecting to /dev/null
            if (flag_quiet || flag_no_messages)
            {
              int dev_null = open("/dev/null", O_WRONLY);
              if (dev_null >= 0)
              {
                dup2(dev_null, STDERR_FILENO);
                close(dev_null);
              }
            }

            // populate argv[] with the command and its arguments, destroying flag_filter in the child process
            std::vector<const char*> args;

            char *arg = const_cast<char*>(command);

            while (*arg != '\0' && *arg != ',')
            {
              while (isspace(static_cast<unsigned char>(*arg)))
                ++arg;

              char *sep = arg;

              if (*arg == '"')
              {
                // "quoted argument" separated by space
                ++sep;

                while (*sep != '\0' &&
                    (*sep != '"' ||
                     (sep[1] != '\0' && sep[1] != ',' && !isspace(static_cast<unsigned char>(sep[1])))))
                  ++sep;

                if (*sep == '"')
                {
                  ++arg;
                  *sep++ = '\0';
                }
              }
              else
              {
                // space-separated argument
                while (*sep != '\0' && *sep != ',' && !isspace(static_cast<unsigned char>(*sep)))
                  ++sep;
              }

              if (sep > arg)
              {
                if (sep - arg == 1 && *arg == '%')
                  args.push_back(in == stdin ? "-" : pathname);
                else
                  args.push_back(arg);
              }

              if (*sep == ',')
                *sep = '\0';

              if (*sep == '\0')
                break;

              *sep = '\0';

              arg = sep + 1;
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

#endif

          // close the writing end of the pipe
          close(fd[1]);

          // close the file and use the reading end of the pipe
          if (in != NULL && in != stdin)
            fclose(in);
          in = fdopen(fd[0], "r");
        }
        else
        {
          if (in != stdin)
            fclose(in);
          in = NULL;

          warning("--filter: cannot create pipe", flag_filter.c_str());

          return false;
        }
      }
    }

    return true;
  }

  // close the file and clear input, return true if next file is extracted from an archive to search
  bool close_file(const char *pathname)
  {
    // check if the input has no error conditions, but do not check stdin which is nonblocking and handled differently
    if (file_in != NULL && file_in != stdin && file_in != Static::source && ferror(file_in))
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
      if (file_in != NULL && file_in != stdin && file_in != Static::source)
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

      // open pipe to the next file or part in an archive if there is a next file to extract
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
      if (fseek(stdin, 0, SEEK_END) < 0 && errno != EINVAL)
      {
        char buf[16384];
        while (true)
        {
          if (fread(buf, 1, sizeof(buf), stdin) == sizeof(buf))
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
    if (file_in != NULL && file_in != stdin && file_in != Static::source)
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
      // assign input to the matcher to search
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
          if (fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK) == -1)
            clearerr(stdin);
          else
            matcher->in.set_handler(&stdin_handler);
        }
      }
#endif
    }

    // -I: do not match binary
    if (flag_binary_without_match && init_is_binary())
      return false;

    // --range=[MIN,][MAX]: start searching at line MIN
    for (size_t i = flag_min_line; i > 1; --i)
      if (!matcher->skip('\n'))
        break;

    return true;
  }

  // after opening a file with init_read, check if its initial part is binary
  bool init_is_binary()
  {
    // check up to 64K in buffer for binary data, the buffer is a window over the input file
    size_t avail = std::min<size_t>(matcher->avail(), 65536);
    if (avail == 0)
      return false;

    // do not cut off the last UTF-8 sequence, ignore it, otherwise we risk failing the UTF-8 check
    const char *buf = matcher->begin();
    if ((buf[avail - 1] & 0x80) == 0x80)
    {
      size_t n = std::min<size_t>(avail, 4); // note: 1 <= n <= 4 bytes to check
      while (n > 0 && (buf[--avail] & 0xc0) == 0x80)
        --n;
      if ((buf[avail] & 0xc0) != 0xc0)
        return true;
    }

    return is_binary(matcher->begin(), avail);
  }

  const char                    *filename;      // the name of the file being searched
  std::string                    partname;      // the name of an extracted file from an archive
  std::string                    restline;      // a buffer to store the rest of a line to search
  const char                    *restline_data; // rest of the line data pointer or NULL
  size_t                         restline_size; // rest of the line size
  size_t                         restline_last; // rest of the line last byte offset
  Output                         out;           // asynchronous output
  reflex::AbstractMatcher       *matcher;       // the pattern matcher we're using, never NULL
  Static::Matchers              *matchers;      // the CNF of AND/OR/NOT matchers or NULL
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

// master submits jobs to workers and implements operations to support job stealing
struct GrepMaster : public Grep {

  GrepMaster(FILE *file, reflex::AbstractMatcher *matcher, Static::Matchers *matchers)
    :
      Grep(file, matcher, matchers),
      sync(flag_sort_key == Sort::NA ? Output::Sync::Mode::UNORDERED : Output::Sync::Mode::ORDERED)
  {
    // master and workers synchronize their output
    out.sync_on(&sync);

    // set global handle to be able to call cancel_ugrep()
    Static::set_grep_handle(this);

    if (Static::cores >= 8)
      set_this_thread_affinity_and_priority(Static::cores - 1);

    start_workers();

    iworker = workers.begin();
  }

  virtual ~GrepMaster()
  {
    stop_workers();
    Static::clear_grep_handle();
  }

  // clone the pattern matcher - the caller is responsible to deallocate the returned matcher
  reflex::AbstractMatcher *matcher_clone() const
  {
    return matcher->clone();
  }

  // clone the CNF of AND/OR/NOT matchers - the caller is responsible to deallocate the returned list of matchers if not NULL
  Static::Matchers *matchers_clone() const
  {
    if (matchers == NULL)
      return NULL;

    Static::Matchers *new_matchers = new Static::Matchers;

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

  // job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
  bool steal(GrepWorker *worker);

  std::list<GrepWorker>           workers; // workers running threads
  std::list<GrepWorker>::iterator iworker; // the next worker to submit a job to
  Output::Sync                    sync;    // sync output of workers

};

// worker runs a thread to execute jobs submitted by the master
struct GrepWorker : public Grep {

  GrepWorker(FILE *file, size_t id, GrepMaster *master)
    :
      Grep(file, master->matcher_clone(), master->matchers_clone()),
      id(id),
      master(master)
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
    jobs.enqueue();
  }

  // submit a job to this worker
  void submit_job(const char *pathname, uint16_t cost, size_t slot)
  {
    jobs.enqueue(pathname, cost, slot);
  }

  // receive a job for this worker, wait until one arrives
  void next_job(Job& job)
  {
    jobs.dequeue(job);
  }

  // submit Job::NONE sentinel to stop this worker
  void stop()
  {
    submit_job();
  }

  std::thread             thread;      // thread of this worker, spawns GrepWorker::execute()
  const size_t            id;          // worker number 0 and up
  GrepMaster             *master;      // the master of this worker
  JobQueue                jobs;        // queue of pending jobs submitted to this worker
};

// start worker threads
void GrepMaster::start_workers()
{
  size_t num;

  // create worker threads
  try
  {
    for (num = 0; num < Static::threads; ++num)
      workers.emplace(workers.end(), out.file, num, this);
  }

  // if sufficient resources are not available then reduce the number of threads to the number of active workers created
  catch (std::system_error& error)
  {
    if (error.code() != std::errc::resource_unavailable_try_again)
      throw;

    Static::threads = num;
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

// submit a job with a pathname to a worker
void GrepMaster::submit(const char *pathname, uint16_t cost)
{
  while (true)
  {
    size_t min_todo = iworker->jobs.todo;

    // find a worker with the minimum number of jobs
    if (min_todo > 0)
    {
      auto min_worker = iworker;

      for (size_t num = 1; num < Static::threads; ++num)
      {
        ++iworker;
        if (iworker == workers.end())
          iworker = workers.begin();

        if (iworker->jobs.todo < min_todo)
        {
          min_todo = iworker->jobs.todo;
          if (min_todo == 0)
            break;

          min_worker = iworker;
        }
      }

      iworker = min_worker;
    }

#ifdef WITH_LOCK_FREE_JOB_QUEUE

    if (iworker->try_submit_job(pathname, cost, sync.next) || out.eof || out.cancelled())
      break;

    // give the worker threads some slack to make progress, then try again
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

#else

    // give the worker threads some slack to make progress when their queues reached a soft max size
    if (min_todo > flag_max_queue && flag_max_queue > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // submit job to worker, do not loop
    iworker->submit_job(pathname, cost, sync.next);
    break;

#endif
  }

  ++sync.next;

  // around we go
  ++iworker;
  if (iworker == workers.end())
    iworker = workers.begin();
}

#ifndef WITH_LOCK_FREE_JOB_QUEUE

// job stealing on behalf of a worker from a co-worker with at least --min-steal jobs still to do
bool GrepMaster::steal(GrepWorker *worker)
{
  // try to steal a job from a co-worker with the most jobs
  auto coworker = workers.begin();
  auto max_worker = coworker;
  size_t max_todo = 0;

  for (size_t i = 0; i < Static::threads; ++i)
  {
    if (&*coworker != worker && coworker->jobs.todo > max_todo)
    {
      max_todo = coworker->jobs.todo;
      max_worker = coworker;
    }

    // around we go
    ++coworker;
    if (coworker == workers.end())
      coworker = workers.begin();
  }

  // not enough jobs in the co-worker's queue to steal from
  if (max_todo < flag_min_steal)
    return false;

  coworker = max_worker;

  Job job;

  // steal a job from the co-worker for this worker
  if (coworker->jobs.steal_job(job))
  {
    worker->jobs.move_job(job);

    return true;
  }

  // couldn't steal any job
  return false;
}

#endif

// execute worker thread
void GrepWorker::execute()
{
  if (Static::cores >= 3)
    set_this_thread_affinity_and_priority(id);

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

    // search the file for this job, an empty pathname means stdin
    search(job.pathname.empty() ? Static::LABEL_STANDARD_INPUT : job.pathname.c_str(), job.cost);

    // end output in ORDERED mode (--sort) for this job slot
    out.end();

#ifndef WITH_LOCK_FREE_JOB_QUEUE
    // if only one job is left to do or nothing to do, then try stealing another job from a co-worker
    if (jobs.todo <= 1)
      master->steal(this);
#endif
  }
}

// the CNF of Boolean search queries and patterns
CNF Static::bcnf;

// unique address to identify standard input path
const char *Static::LABEL_STANDARD_INPUT = "(standard input)";

// unique address to identify color and pretty WHEN arguments
const char *Static::NEVER = "never";
const char *Static::ALWAYS = "always";
const char *Static::AUTO = "auto";

// pointer to the --index pattern DFA with HFA constructed before threads start
const reflex::Pattern *Static::index_pattern = NULL; // concurrent access is thread safe

// the -M MAGIC pattern DFA constructed before threads start, read-only afterwards
reflex::Pattern Static::magic_pattern; // concurrent access is thread safe
reflex::Matcher Static::magic_matcher; // concurrent access is not thread safe

// the --filter-magic-label pattern DFA
reflex::Pattern Static::filter_magic_pattern; // concurrent access is thread safe

// ugrep command-line arguments pointing to argv[]
const char *Static::arg_pattern = NULL;
std::vector<const char*> Static::arg_files;

// number of cores
size_t Static::cores;

// number of concurrent threads for workers
size_t Static::threads;

// number of warnings given
std::atomic_size_t Static::warnings;

// redirectable source is standard input by default or a pipe
FILE *Static::source = stdin;

// redirectable output destination is standard output by default or a pipe
FILE *Static::output = stdout;

// redirectable error output destination is standard error by default or a pipe
FILE *Static::errout = stderr;

// full home directory path
const char *Static::home_dir = NULL;

// Grep object handle, to cancel the search with cancel_ugrep()
struct Grep *Static::grep_handle = NULL;
std::mutex Static::grep_handle_mutex;

// patterns
reflex::Pattern Static::reflex_pattern;
std::string Static::string_pattern;
std::list<reflex::Pattern> Static::reflex_patterns;
std::list<std::string> Static::string_patterns;

// the LineMatcher, PCRE2Matcher, FuzzyMatcher or Matcher, concurrent access is not thread safe
std::unique_ptr<reflex::AbstractMatcher> Static::matcher;

// the CNF of AND/OR/NOT matchers or NULL, concurrent access is not thread safe
Static::Matchers Static::matchers;

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
  { "null-data",   reflex::Input::file_encoding::null_data  },
  { NULL, 0 }
};

// table of file types for option -t, --file-type
const Type type_table[] = {
  { "actionscript", "as,mxml", NULL,                                                  NULL },
  { "ada",          "ada,adb,ads", NULL,                                              NULL },
  { "adoc",         "adoc", NULL,                                                     NULL },
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
  { "text",         "text,txt,TXT,md,rst,adoc", NULL,                                 NULL },
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
  { "zig",          "zig,zon", NULL,                                                  NULL },
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

  if (flag_query)
  {
    if (!flag_no_messages && Static::warnings > 0)
      abort("option -Q: warnings are present, specify -s to ignore");

    // increase worker threads queue
    flag_max_queue = 65536;

    // -Q: TUI query mode
    Query::query();
  }
  else
  {
    if (!flag_no_messages && flag_pager != NULL && Static::warnings > 0)
      abort("option --pager: warnings are present, specify -s to ignore");

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

  return Static::warnings > 0 ? EXIT_ERROR : Stats::found_any_file() ? EXIT_OK : EXIT_FAIL;
}

// set -1,...,-9,-10,... recursion depth flags
static void set_depth(const char *& arg)
{
  const char *range = arg;
  char *rest = NULL;

  if (flag_max_depth > 0)
  {
    if (flag_min_depth == 0)
      flag_min_depth = flag_max_depth;
    flag_max_depth = static_cast<size_t>(strtoull(range, &rest, 10));
    range = rest;
  }
  else
  {
    flag_max_depth = static_cast<size_t>(strtoull(range, &rest, 10));
    range = rest;
    if (*range == '-' || *range == ',')
    {
      flag_min_depth = flag_max_depth;
      flag_max_depth = static_cast<size_t>(strtoull(range + 1, &rest, 10));
      range = rest;
    }
  }

  if (flag_min_depth > flag_max_depth)
    usage("invalid argument -", arg);

  arg = range - 1;
}

// set --10,... and --depth=10,... recursion depth flags
static void set_depth_long(const char *arg)
{
  const char *range = arg;
  set_depth(range);

  if (range[1] != '\0')
    usage("invalid argument --depth=", arg);
}

// load config file specified or the default .ugrep, located in the working directory or home directory
static void load_config(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, bool recurse = false)
{
  // the default config file is .ugrep when FILE is not specified
  if (flag_config == NULL || *flag_config == '\0')
    flag_config = ".ugrep";

  // if config file was parsed before, then only try parsing the home dir config file
  bool home = flag_config_files.find(flag_config) != flag_config_files.end();

  // open a local config file or in the home directory
  std::string config_file(flag_config);
  FILE *file = NULL;
  if (home || fopen_smart(&file, flag_config, "r") != 0)
  {
    file = NULL;
    if (Static::home_dir != NULL && *flag_config != '~' && *flag_config != PATHSEPCHR)
    {
      // check the home directory for the configuration file, parse only if not parsed before
      config_file.assign(Static::home_dir).append(PATHSEPSTR).append(flag_config);
      if (flag_config_files.find(config_file) != flag_config_files.end() ||
          fopen_smart(&file, config_file.c_str(), "r") != 0)
      {
        file = NULL;
      }
      else
      {
        // new config file in the home dir to parse, add to the set
        flag_config_files.insert(config_file);
      }
    }
  }
  else
  {
    // new config file to parse, add to the set
    flag_config_files.insert(flag_config);
  }

  // parse config file
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

      // parse option or skip empty lines and comments
      if (!line.empty() && line.front() != '#')
      {
        // construct an option argument to parse as argv[]
        line.insert(0, "--");
        const char *arg = line.c_str();
        const char *args[2] = { NULL, arg };

        Static::warnings = 0;

        // warn about invalid options but do not exit
        flag_usage_warnings = true;

        options(pattern_args, 2, args);

        if (Static::warnings > 0)
        {
          std::cerr << "ugrep: error in " << config_file << " at line " << lineno << '\n';
          errors = true;
        }
        else if (line.compare(0, 8, "--config") == 0)
        {
          // parse a config file, but do not recurse more than one level deep
          if (recurse)
          {
            std::cerr << "ugrep: recursive configuration in " << config_file << " at line " << lineno << '\n';
            errors = true;
          }
          else
          {
            // save flag and pathname
            const char *this_config = flag_config;
            std::string this_file;
            this_file.swap(config_file);

            // load config to include in this config
            flag_config = line.size() == 8 ? NULL : line.c_str() + 9;
            load_config(pattern_args, true);

            // restore flag and pathname
            config_file.swap(this_file);
            flag_config = this_config;
          }
        }
      }

      ++lineno;
    }

    if (ferror(file))
      error("error while reading", config_file.c_str());

    if (file != stdin)
      fclose(file);

    if (errors)
      exit(EXIT_ERROR);
  }
  else if (strcmp(flag_config, ".ugrep") != 0)
  {
    error("option --config: cannot read", flag_config);
  }

  flag_usage_warnings = false;
}

// save a configuration file after loading it first when present
static void save_config()
{
  bool exists = false;

  // rename old config file to .old when present and not standard input
  if (strcmp(flag_save_config, "-") != 0)
  {
    std::string config_old(flag_save_config);
    config_old.append(".old");
    if (std::rename(flag_save_config, config_old.c_str()) == 0)
    {
      exists = true;
      errno = EEXIST;
      warning("saved old configuration file to", config_old.c_str());
    }
  }

  FILE *file = NULL;

  // if not saved to standard output ("-"), then inform user
  if (!flag_no_messages && strcmp(flag_save_config, "-") != 0)
  {
    if (flag_config == NULL)
      fprintf(Static::errout, "ugrep: saving configuration file %s\n", flag_save_config);
    else if (exists && strcmp(flag_config, flag_save_config) == 0)
      fprintf(Static::errout, "ugrep: updating configuration file %s\n", flag_save_config);
    else
      fprintf(Static::errout, "ugrep: saving configuration file %s with options based on %s\n", flag_save_config, flag_config);
  }

  if (fopen_smart(&file, flag_save_config, "w") != 0)
  {
    usage("cannot save configuration file ", flag_save_config);

    return;
  }

  if (strcmp(flag_save_config, ".ugrep") == 0)
    fprintf(file, "# ugrep configuration used by ug and ugrep --config.\n");
  else if (strcmp(flag_save_config, "-") == 0)
    fprintf(file, "# ugrep configuration\n");
  else
    fprintf(file, "# ugrep configuration used with --config=%s or ---%s.\n", flag_save_config, flag_save_config);

  fprintf(file, "\
#\n\
# A long option is defined per line with an optional `=' and its argument,\n\
# when applicable.  Empty lines and lines starting with a `#' are ignored.\n\
#\n\
# Try `ug --help' or `ug --help WHAT' for help with options.\n\n");

  fprintf(file, "### TERMINAL DISPLAY ###\n\n");

  fprintf(file, "# Custom color scheme, overrides default GREP_COLORS parameters\n");
  if (flag_colors != NULL)
    fprintf(file, "colors=%s\n", flag_colors);
  else
    fprintf(file, "# colors=\n");
  fprintf(file, "\
# The argument is a colon-separated list of one or more parameters `sl='\n\
# (selected line), `cx=' (context line), `mt=' (matched text), `ms=' (match\n\
# selected), `mc=' (match context), `fn=' (file name), `ln=' (line number),\n\
# `cn=' (column number), `bn=' (byte offset), `se=' (separator), `qp=' (TUI\n\
# prompt), `qe=' (TUI errors), `qr=' (TUI regex), `qm=' (TUI regex meta\n\
# characters), `ql=' (TUI regex lists and literals), `qb=' (TUI regex braces).\n\
# Parameter values are ANSI SGR color codes or `k' (black), `r' (red), `g'\n\
# (green), `y' (yellow), `b' (blue), `m' (magenta), `c' (cyan), `w' (white), or\n\
# leave empty for no color.\n\
# Upper case specifies background colors.\n\
# A `+' qualifies a color as bright.\n\
# A foreground and a background color may be combined with font properties `n'\n\
# (normal), `f' (faint), `h' (highlight), `i' (invert), `u' (underline).\n\
# Parameter `hl' enables file name hyperlinks (same as --hyperlink).\n\
# Parameter `rv' reverses the `sl=' and `cx=' parameters when option -v is\n\
# used.\n\
#\n\
# The ugrep default color scheme:\n\
#   colors=cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35\n\
# The GNU grep and ripgrep default color scheme:\n\
#   colors=sl=37:cx=33:mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35\n\
# The silver searcher default color scheme:\n\
#   colors=mt=30;43:fn=1;32:ln=1;33:cn=1;33:bn=1;33:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35\n\
# Underlined bright green matches with shaded background on bright selected lines:\n\
#   colors=sl=1:cx=33:ms=1;4;32;100:mc=1;4;32:fn=1;32;100:ln=1;32:cn=1;32:bn=1;32:se=36:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35\n\
# Inverted bright yellow matches and TUI regex syntax highlighting with background colors:\n\
#   colors=cx=hb:ms=hiy:mc=hic:fn=hi+y+K:ln=hg:cn=hg:bn=hg:se=c:gp=hg:qr=hwB:qm=hwG:ql=hwC:qb=hwM\n\
# Only change the TUI regex syntax highlighting to use background colors:\n\
#   colors=gp=hg:qr=hwB:qm=hwG:ql=hwC:qb=hwM\n\n");

  fprintf(file, "# Enable color output to a terminal\n%s\n\n", flag_color != NULL ? "color" : "no-color");
  if (flag_hyperlink != NULL && *flag_hyperlink == '\0')
    fprintf(file, "# Enable hyperlinks in color output\nhyperlink\n\n");
  else if (flag_hyperlink != NULL)
    fprintf(file, "# Enable hyperlinks in color output\nhyperlink=%s\n\n", flag_hyperlink);
  fprintf(file, "# Enable query TUI confirmation prompts, default: confirm\n%sno-confirm\n\n", flag_confirm ? "# " : "");
  fprintf(file, "# Split query TUI screen on startup, default: no-split\n%ssplit\n\n", flag_split ? "" : "# ");
  fprintf(file, "# Default query TUI response delay in units of 100ms, default: delay=4\n");
  if (flag_delay == DEFAULT_QUERY_DELAY)
    fprintf(file, "# delay=4\n\n");
  else
    fprintf(file, "delay=%zu\n\n", flag_delay);

  fprintf(file, "# Enable query TUI file viewing command with CTRL-Y or F2, default: view\n");
  if (flag_view != NULL && *flag_view == '\0')
    fprintf(file, "# view=less\n\n");
  else if (flag_view != NULL)
    fprintf(file, "view=%s\n\n", flag_view);
  else
    fprintf(file, "no-view\n\n");

  fprintf(file, "# Enable a pager for terminal output, default: no-pager\n");
  if (flag_pager != NULL && *flag_pager != '\0')
    fprintf(file, "pager=%s\n\n", flag_pager);
  else
    fprintf(file, "# pager=less\n\n");

  fprintf(file, "# Enable pretty output to the terminal, default: pretty\n%s\n\n", flag_pretty != NULL ? "pretty" : "no-pretty");

  fprintf(file, "# Enable directory tree output to a terminal for -l (--files-with-matches) and -c (--count)\n%s\n\n", flag_tree ? "tree" : "no-tree");

  if (flag_heading.is_defined() && flag_heading != (flag_pretty != NULL))
    fprintf(file, "# Enable headings (enabled with --pretty)\n%s\n\n", flag_heading ? "heading" : "no-heading");

  if (flag_break.is_defined() && flag_break != (flag_pretty != NULL))
    fprintf(file, "# Enable break after matching files (enabled with --pretty)\n%s\n\n", flag_break ? "break" : "no-break");

  if (flag_initial_tab.is_defined() && flag_initial_tab != (flag_pretty != NULL))
    fprintf(file, "# Enable initial tab (enabled with --pretty)\n%s\n\n", flag_initial_tab ? "initial-tab" : "no-initial-tab");

  if (flag_line_number.is_defined() && flag_line_number != (flag_pretty != NULL))
    fprintf(file, "# Enable line numbers (enabled with --pretty)\n%s\n\n", flag_line_number ? "line-number" : "no-line-number");

  if (flag_column_number.is_defined())
    fprintf(file, "# Enable column numbers\n%s\n\n", flag_column_number ? "column-number" : "no-column-number");

  if (flag_byte_offset.is_defined())
    fprintf(file, "# Enable byte offsets\n%s\n\n", flag_byte_offset ? "byte-offset" : "no-byte-offset");

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

  fprintf(file, "# Enable case-insensitive search, default: no-ignore-case\n%signore-case\n\n", flag_ignore_case.is_undefined() ? "# " : flag_ignore_case ? "" : "no-");
  fprintf(file, "# Enable smart case, default: no-smart-case\n%ssmart-case\n\n", flag_smart_case.is_undefined() ? "# " : flag_smart_case ? "" : "no-");
  fprintf(file, "# Enable empty pattern matches, default: no-empty\n%sempty\n\n", flag_empty.is_undefined() ? "# " : flag_empty ? "" : "no-");
  fprintf(file, "# Force option -c (--count) to return nonzero matches with --min-count=1, default: --min-count=0\n");
  if (flag_min_count == 0)
    fprintf(file, "# min-count=1\n\n");
  else
    fprintf(file, "min-count=%zu\n\n", flag_min_count);

  fprintf(file, "### SEARCH TARGETS ###\n\n");

  fprintf(file, "# Case-insensitive glob matching, default: no-glob-ignore-case\n%sglob-ignore-case\n\n", flag_glob_ignore_case ? "" : "# ");
  fprintf(file, "# Search hidden files and directories, default: no-hidden\n%shidden\n\n", flag_hidden ? "" : "# ");
  fprintf(file, "# Ignore binary files, default: no-ignore-binary\n%signore-binary\n\n", strcmp(flag_binary_files, "without-match") == 0 ? "" : "# ");
  if (!flag_include_fs.empty())
  {
    fprintf(file, "# Include specific file systems only\n");
    for (const auto& fs : flag_include_fs)
      fprintf(file, "include-fs=%s\n", fs.c_str());
    fprintf(file, "\n");
  }
  if (!flag_exclude_fs.empty())
  {
    fprintf(file, "# Exclude specific file systems\n");
    for (const auto& fs : flag_exclude_fs)
      fprintf(file, "exclude-fs=%s\n", fs.c_str());
    fprintf(file, "\n");
  }
  if (!flag_include_dir.empty())
  {
    fprintf(file, "# Include specific directories only\n");
    for (const auto& d : flag_include_dir)
      fprintf(file, "include-dir=%s\n", d.c_str());
    fprintf(file, "\n");
  }
  if (!flag_exclude_dir.empty())
  {
    fprintf(file, "# Exclude specific directories\n");
    for (const auto& d : flag_exclude_dir)
      fprintf(file, "exclude-dir=%s\n", d.c_str());
    fprintf(file, "\n");
  }
  if (!flag_include.empty())
  {
    fprintf(file, "# Include specific files only\n");
    for (const auto& f : flag_include)
      fprintf(file, "include=%s\n", f.c_str());
    fprintf(file, "\n");
  }
  if (!flag_exclude.empty())
  {
    fprintf(file, "# Exclude specific files\n");
    for (const auto& f : flag_exclude)
      fprintf(file, "exclude-dir=%s\n", f.c_str());
    fprintf(file, "\n");
  }
  fprintf(file, "# Enable decompression and archive search, default: no-decompress\n%sdecompress\n\n", flag_decompress ? "" : "# ");
  fprintf(file, "# Maximum decompression and de-archiving nesting levels, default: zmax=1\nzmax=%zu\n\n", flag_zmax);
  if (flag_dereference)
    fprintf(file, "# Dereference symlinks, default: no-dereference\ndereference\n\n");
  else if (flag_dereference_files)
    fprintf(file, "# Dereference symlinks to files, not directories, default: no-dereference-files\ndereference-files\n\n");
  fprintf(file, "# Search devices, default: devices=skip\n%sdevices=%s\n\n", flag_devices == NULL ? "# " : "", flag_devices == NULL ? "skip" : flag_devices);
  if (flag_directories == NULL || strcmp(flag_directories, "read") == 0)
    fprintf(file, "# Warn when searching directories specified on the command line (like grep) with directories=read\n%sdirectories=read\n\n", flag_directories == NULL ? "# " : "");
  if (flag_max_depth > 0)
    fprintf(file, "# Recursively search directories up to %zu levels deep\nmax-depth=%zu\n\n", flag_max_depth, flag_max_depth);
  if (flag_ignore_files.empty())
  {
    fprintf(file, "# Ignore files and directories specified in .gitignore, default: no-ignore-files\n# ignore-files\n\n");
  }
  else
  {
    fprintf(file, "# Ignore files and directories specified in .gitignore, default: no-ignore-files\n");
    for (const auto& ignore : flag_ignore_files)
      fprintf(file, "ignore-files=%s\n", ignore.c_str());
    fprintf(file, "\n");
  }

  if (!flag_filter.empty())
  {
    fprintf(file, "# Filter search with file format conversion tools\nfilter=%s\n\n", flag_filter.c_str());
    if (!flag_filter_magic_label.empty())
    {
      fprintf(file, "# Filter by file signature magic bytes\n");
      for (const auto& label : flag_filter_magic_label)
        fprintf(file, "filter-magic-label=%s\n", label.c_str());
      fprintf(file, "# Warning: filter-magic-label significantly reduces performance!\n\n");
    }
  }

  fprintf(file, "### OUTPUT ###\n\n");

  if (flag_separator != NULL)
    fprintf(file, "# Separator, default: none specified to output a `:'\nseparator=%s\n\n", flag_separator);

  fprintf(file, "# Sort the list of files and directories searched and matched, default: sort\n");
  if (flag_sort != NULL)
    fprintf(file, "sort=%s\n\n", flag_sort);
  else
    fprintf(file, "# sort\n\n");

  if (ferror(file))
    error("cannot save", flag_save_config);

  if (file != stdout)
    fclose(file);

  if (!flag_file_type.empty() || !flag_file_extension.empty() || !flag_file_magic.empty() || !flag_glob.empty() || !flag_iglob.empty())
    warning("options --file-type, --file-extension, --file-magic, --glob and --iglob are not saved to", flag_save_config);
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
                if (strcmp(arg, "after-context") == 0) // legacy form --after-context NUM
                  flag_after_context = strtonum(getloptarg(argc, argv, "", i), "invalid argument --after-context=");
                else if (strncmp(arg, "after-context=", 14) == 0)
                  flag_after_context = strtonum(getloptarg(argc, argv, arg + 14, i), "invalid argument --after-context=");
                else if (strcmp(arg, "all") == 0)
                  option_all_files();
                else if (strcmp(arg, "and") == 0)
                  option_and(pattern_args, i, argc, argv);
                else if (strncmp(arg, "and=", 4) == 0)
                  option_and(pattern_args, getloptarg(argc, argv, arg + 4, i));
                else if (strcmp(arg, "andnot") == 0)
                  option_andnot(pattern_args, i, argc, argv);
                else if (strncmp(arg, "andnot=", 7) == 0)
                  option_andnot(pattern_args, getloptarg(argc, argv, arg + 7, i));
                else if (strcmp(arg, "any-line") == 0)
                  flag_any_line = true;
                else if (strcmp(arg, "ascii") == 0)
                  flag_binary = true;
                else
                  usage("invalid option --", arg, "--after-context=, all, --and, --andnot, --any-line or --ascii");
                break;

              case 'b':
                if (strcmp(arg, "basic-regexp") == 0)
                  flag_basic_regexp = true;
                else if (strcmp(arg, "before-context") == 0) // legacy form --before-context NUM
                  flag_before_context = strtonum(getloptarg(argc, argv, "", i), "invalid argument --before-context=");
                else if (strncmp(arg, "before-context=", 15) == 0)
                  flag_before_context = strtonum(getloptarg(argc, argv, arg + 15, i), "invalid argument --before-context=");
                else if (strcmp(arg, "best-match") == 0)
                  flag_best_match = true;
                else if (strcmp(arg, "binary") == 0)
                  flag_binary = true;
                else if (strcmp(arg, "binary-files") == 0) // legacy form --binary-files TYPE
                  flag_binary_files = strarg(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "binary-files=", 13) == 0)
                  flag_binary_files = strarg(getloptarg(argc, argv, arg + 13, i));
                else if (strcmp(arg, "bool") == 0)
                  flag_bool = true;
                else if (strcmp(arg, "break") == 0)
                  flag_break = true;
                else if (strcmp(arg, "byte-offset") == 0)
                  flag_byte_offset = true;
                else
                  usage("invalid option --", arg, "--basic-regexp, --before-context=, --binary, --binary-files=, --bool, --break or --byte-offset");
                break;

              case 'c':
                if (strcmp(arg, "color") == 0 || strcmp(arg, "colour") == 0)
                  flag_color = Static::AUTO;
                else if (strncmp(arg, "color=", 6) == 0)
                  flag_color = strarg(getloptarg(argc, argv, arg + 6, i));
                else if (strncmp(arg, "colour=", 7) == 0)
                  flag_color = strarg(getloptarg(argc, argv, arg + 7, i));
                else if (strncmp(arg, "colors=", 7) == 0)
                  flag_colors = strarg(arg + 7);
                else if (strncmp(arg, "colours=", 8) == 0)
                  flag_colors = strarg(arg + 8);
                else if (strcmp(arg, "column-number") == 0)
                  flag_column_number = true;
                else if (strcmp(arg, "config") == 0 || strncmp(arg, "config=", 7) == 0)
                  ; // --config is pre-parsed before other options are parsed
                else if (strcmp(arg, "confirm") == 0)
                  flag_confirm = true;
                else if (strcmp(arg, "context") == 0) // legacy form --context NUM
                  flag_after_context = flag_before_context = strtonum(getloptarg(argc, argv, "", i), "invalid argument --context=");
                else if (strncmp(arg, "context=", 8) == 0)
                  flag_after_context = flag_before_context = strtonum(getloptarg(argc, argv, arg + 8, i), "invalid argument --context=");
                else if (strncmp(arg, "context-separator=", 18) == 0)
                  flag_separator_dash = strarg(arg + 18);
                else if (strcmp(arg, "count") == 0)
                  flag_count = true;
                else if (strcmp(arg, "cpp") == 0)
                  flag_cpp = true;
                else if (strcmp(arg, "csv") == 0)
                  flag_csv = true;
                else if (strcmp(arg, "colors") == 0 || strcmp(arg, "colours") == 0 || strcmp(arg, "context-separator") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--color, --colors=, --column-number, --config, --confirm, --context=, context-separator=, --count, --cpp or --csv");
                break;

              case 'd':
                if (strcmp(arg, "decompress") == 0)
                  flag_decompress = true;
                else if (strncmp(arg, "delay=", 6) == 0)
                  flag_delay = strtonum(getloptarg(argc, argv, arg + 6, i), "invalid argument --delay=");
                else if (strcmp(arg, "depth") == 0) // legacy form --depth NUM
                  set_depth_long(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "depth=", 6) == 0)
                  set_depth_long(getloptarg(argc, argv, arg + 6, i));
                else if (strcmp(arg, "dereference") == 0)
                  flag_dereference = true;
                else if (strcmp(arg, "dereference-files") == 0)
                  flag_dereference_files = true;
                else if (strcmp(arg, "dereference-recursive") == 0)
                  flag_directories = "dereference-recurse";
                else if (strcmp(arg, "devices") == 0) // legacy form --devices ACTION
                  flag_devices = strarg(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "devices=", 8) == 0)
                  flag_devices = strarg(getloptarg(argc, argv, arg + 8, i));
                else if (strcmp(arg, "directories") == 0) // legacy form --directories ACTION
                  flag_directories = strarg(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "directories=", 12) == 0)
                  flag_directories = strarg(getloptarg(argc, argv, arg + 12, i));
                else if (strcmp(arg, "dotall") == 0)
                  flag_dotall = true;
                else if (strcmp(arg, "delay") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--decompress, --delay=, --depth=, --dereference, --dereference-files, --dereference-recursive, --devices=, --directories= or --dotall");
                break;

              case 'e':
                if (strcmp(arg, "empty") == 0)
                  flag_empty = true;
                else if (strncmp(arg, "encoding=", 9) == 0)
                  flag_encoding = strarg(getloptarg(argc, argv, arg + 9, i));
                else if (strcmp(arg, "exclude") == 0) // legacy form --exclude GLOB
                  flag_exclude.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "exclude=", 8) == 0)
                  flag_exclude.emplace_back(getloptarg(argc, argv, arg + 8, i));
                else if (strcmp(arg, "exclude-dir") == 0) // legacy form --exclude-dir GLOB
                  flag_exclude_dir.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "exclude-dir=", 12) == 0)
                  flag_exclude_dir.emplace_back(getloptarg(argc, argv, arg + 12, i));
                else if (strcmp(arg, "exclude-from") == 0) // legacy form --exclude-from FILE
                  flag_exclude_from.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "exclude-from=", 13) == 0)
                  flag_exclude_from.emplace_back(getloptarg(argc, argv, arg + 13, i));
                else if (strcmp(arg, "exclude-fs") == 0)
                  flag_exclude_fs.emplace_back();
                else if (strncmp(arg, "exclude-fs=", 11) == 0)
                  flag_exclude_fs.emplace_back(getloptarg(argc, argv, arg + 11, i));
                else if (strcmp(arg, "extended-regexp") == 0)
                  flag_basic_regexp = false;
                else if (strcmp(arg, "encoding") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--empty, --encoding=, --exclude=, --exclude-dir=, --exclude-from=, --exclude-fs= or --extended-regexp");
                break;

              case 'f':
                if (strcmp(arg, "file") == 0) // legacy form --file FILE
                  flag_file.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "file=", 5) == 0)
                  flag_file.emplace_back(getloptarg(argc, argv, arg + 5, i));
                else if (strncmp(arg, "file-extension=", 15) == 0)
                  flag_file_extension.emplace_back(getloptarg(argc, argv, arg + 15, i));
                else if (strncmp(arg, "file-magic=", 11) == 0)
                  flag_file_magic.emplace_back(getloptarg(argc, argv, arg + 11, i));
                else if (strncmp(arg, "file-type=", 10) == 0)
                  flag_file_type.emplace_back(getloptarg(argc, argv, arg + 10, i));
                else if (strcmp(arg, "files") == 0)
                  flag_files = true;
                else if (strcmp(arg, "files-with-matches") == 0)
                  flag_files_with_matches = true;
                else if (strcmp(arg, "files-without-match") == 0)
                  flag_files_without_match = true;
                else if (strcmp(arg, "fixed-strings") == 0)
                  flag_fixed_strings = true;
                else if (strncmp(arg, "filter=", 7) == 0)
                  flag_filter.append(flag_filter.empty() ? "" : ",").append(getloptarg(argc, argv, arg + 7, i));
                else if (strncmp(arg, "filter-magic-label=", 19) == 0)
                  flag_filter_magic_label.emplace_back(getloptarg(argc, argv, arg + 19, i));
                else if (strncmp(arg, "format=", 7) == 0)
                  flag_format = strarg(getloptarg(argc, argv, arg + 7, i));
                else if (strncmp(arg, "format-begin=", 13) == 0)
                  flag_format_begin = strarg(arg + 13);
                else if (strncmp(arg, "format-close=", 13) == 0)
                  flag_format_close = strarg(arg + 13);
                else if (strncmp(arg, "format-end=", 11) == 0)
                  flag_format_end = strarg(arg + 11);
                else if (strncmp(arg, "format-open=", 12) == 0)
                  flag_format_open = strarg(arg + 12);
                else if (strcmp(arg, "fuzzy") == 0)
                  flag_fuzzy = 1;
                else if (strncmp(arg, "fuzzy=", 6) == 0)
                  flag_fuzzy = strtofuzzy(getloptarg(argc, argv, arg + 6, i), "invalid argument --fuzzy=");
                else if (strcmp(arg, "free-space") == 0)
                  flag_free_space = true;
                else if (strcmp(arg, "file-extension") == 0 ||
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
                  usage("invalid option --", arg, "--file=, --file-extension=, --file-magic=, --file-type=, --files, --files-with-matches, --files-without-match, --fixed-strings, --filter=, --filter-magic-label=, --format=, --format-begin=, --format-close, --format-end=, --format-open=, --fuzzy or --free-space");
                break;

              case 'g':
                if (strncmp(arg, "glob=", 5) == 0)
                  flag_glob.emplace_back(getloptarg(argc, argv, arg + 5, i));
                else if (strcmp(arg, "glob-ignore-case") == 0)
                  flag_glob_ignore_case = true;
                else if (strcmp(arg, "grep") == 0)
                  flag_grep = true;
                else if (strcmp(arg, "group-separator") == 0)
                  flag_group_separator = "--";
                else if (strncmp(arg, "group-separator=", 16) == 0)
                  flag_group_separator = strarg(arg + 16);
                else if (strcmp(arg, "glob") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--glob=, --glob-ignore-case, --grep or --group-separator");
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
                  flag_hexdump = strarg(arg + 8);
                else if (strcmp(arg, "hidden") == 0)
                  flag_hidden = true;
                else if (strncmp(arg, "hyperlink=", 10) == 0)
                  flag_hyperlink = strarg(arg + 10);
                else if (strcmp(arg, "hyperlink") == 0)
                  flag_hyperlink = "";
                else
                  usage("invalid option --", arg, "--heading, --help, --hex, --hexdump, --hidden or --hyperlink");
                break;

              case 'i':
                if (strncmp(arg, "iglob=", 6) == 0)
                  flag_iglob.emplace_back(getloptarg(argc, argv, arg + 6, i));
                else if (strcmp(arg, "ignore-binary") == 0)
                  flag_binary_files = "without-match";
                else if (strcmp(arg, "ignore-case") == 0)
                  flag_ignore_case = true;
                else if (strcmp(arg, "ignore-files") == 0)
                  flag_ignore_files.insert(DEFAULT_IGNORE_FILE);
                else if (strncmp(arg, "ignore-files=", 13) == 0)
                  flag_ignore_files.insert(getloptarg(argc, argv, arg + 13, i));
                else if (strcmp(arg, "include") == 0) // legacy form --include GLOB
                  flag_include.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "include=", 8) == 0)
                  flag_include.emplace_back(getloptarg(argc, argv, arg + 8, i));
                else if (strcmp(arg, "include-dir") == 0) // legacy form --include-dir GLOB
                  flag_include_dir.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "include-dir=", 12) == 0)
                  flag_include_dir.emplace_back(getloptarg(argc, argv, arg + 12, i));
                else if (strcmp(arg, "include-from") == 0) // legacy form --include-from FILE
                  flag_include_from.emplace_back(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "include-from=", 13) == 0)
                  flag_include_from.emplace_back(getloptarg(argc, argv, arg + 13, i));
                else if (strcmp(arg, "include-fs") == 0)
                  flag_include_fs.emplace_back();
                else if (strncmp(arg, "include-fs=", 11) == 0)
                  flag_include_fs.emplace_back(getloptarg(argc, argv, arg + 11, i));
                else if (strcmp(arg, "index") == 0)
                  flag_index = "safe";
                else if (strncmp(arg, "index=", 6) == 0)
                  flag_index = strarg(getloptarg(argc, argv, arg + 6, i));
                else if (strcmp(arg, "initial-tab") == 0)
                  flag_initial_tab = true;
                else if (strcmp(arg, "invert-match") == 0)
                  flag_invert_match = true;
                else
                  usage("invalid option --", arg, "--iglob=, --ignore-case, --ignore-files, --include=, --include-dir=, --include-from=, --include-fs=, --initial-tab or --invert-match");
                break;

              case 'j':
                if (strncmp(arg, "jobs=", 5) == 0)
                  flag_jobs = strtonum(getloptarg(argc, argv, arg + 5, i), "invalid argument --jobs=");
                else if (strcmp(arg, "json") == 0)
                  flag_json = true;
                else if (strcmp(arg, "jobs") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--jobs= or --json");
                break;

              case 'l':
                if (strcmp(arg, "label") == 0) // legacy form --label LABEL
                  flag_label = strarg(getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "label=", 6) == 0)
                  flag_label = strarg(arg + 6);
                else if (strcmp(arg, "line-buffered") == 0)
                  flag_line_buffered = true;
                else if (strcmp(arg, "line-number") == 0)
                  flag_line_number = true;
                else if (strcmp(arg, "line-regexp") == 0)
                  flag_line_regexp = true;
                else if (strcmp(arg, "lines") == 0)
                  flag_files = false;
                else
                  usage("invalid option --", arg, "--label=, --line-buffered, --line-number, --line-regexp or --lines");
                break;

              case 'm':
                if (strcmp(arg, "match") == 0)
                  flag_match = true;
                else if (strncmp(arg, "max-count=", 10) == 0)
                  flag_max_count = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --max-count=");
                else if (strncmp(arg, "max-depth=", 10) == 0)
                  flag_max_depth = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --max-depth=");
                else if (strncmp(arg, "max-files=", 10) == 0)
                  flag_max_files = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --max-files=");
                else if (strncmp(arg, "max-line=", 9) == 0)
                  flag_max_line = strtopos(getloptarg(argc, argv, arg + 9, i), "invalid argument --max-line=");
                else if (strncmp(arg, "max-queue=", 10) == 0)
                  flag_max_queue = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --max-queue=");
                else if (strncmp(arg, "min-count=", 10) == 0)
                  flag_min_count = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --min-count=");
                else if (strncmp(arg, "min-depth=", 10) == 0)
                  flag_min_depth = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --min-depth=");
                else if (strncmp(arg, "min-line=", 9) == 0)
                  flag_min_line = strtopos(getloptarg(argc, argv, arg + 9, i), "invalid argument --min-line=");
                else if (strncmp(arg, "min-steal=", 10) == 0)
                  flag_min_steal = strtopos(getloptarg(argc, argv, arg + 10, i), "invalid argument --min-steal=");
                else if (strcmp(arg, "mmap") == 0)
                  flag_max_mmap = MAX_MMAP_SIZE;
                else if (strncmp(arg, "mmap=", 5) == 0)
                  flag_max_mmap = strtopos(getloptarg(argc, argv, arg + 5, i), "invalid argument --mmap=");
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
                  usage("invalid option --", arg, "--match, --max-count=, --max-depth=, --max-files=, --max-line=, --min-count=, --min-depth=, --min-line=, --mmap or --messages");
                break;

              case 'n':
                if (strncmp(arg, "neg-regexp=", 11) == 0)
                  option_regexp(pattern_args, getloptarg(argc, argv, arg + 11, i), true);
                else if (strcmp(arg, "not") == 0)
                  option_not(pattern_args, i, argc, argv);
                else if (strncmp(arg, "not=", 4) == 0)
                  option_not(pattern_args, getloptarg(argc, argv, arg + 4, i));
                else if (strcmp(arg, "no-any-line") == 0)
                  flag_any_line = false;
                else if (strcmp(arg, "no-ascii") == 0)
                  flag_binary = false;
                else if (strcmp(arg, "no-binary") == 0)
                  flag_binary = false;
                else if (strcmp(arg, "no-bool") == 0)
                  flag_bool = false;
                else if (strcmp(arg, "no-break") == 0)
                  flag_break = false;
                else if (strcmp(arg, "no-byte-offset") == 0)
                  flag_byte_offset = false;
                else if (strcmp(arg, "no-color") == 0 || strcmp(arg, "no-colour") == 0)
                  flag_color = Static::NEVER;
                else if (strcmp(arg, "no-column-number") == 0)
                  flag_column_number = false;
                else if (strcmp(arg, "no-config") == 0)
                  ;
                else if (strcmp(arg, "no-confirm") == 0)
                  flag_confirm = false;
                else if (strcmp(arg, "no-count") == 0)
                  flag_count = false;
                else if (strcmp(arg, "no-decompress") == 0)
                  flag_decompress = false;
                else if (strcmp(arg, "no-dereference") == 0)
                  flag_no_dereference = true;
                else if (strcmp(arg, "no-dereference-files") == 0)
                  flag_dereference_files = false;
                else if (strcmp(arg, "no-dotall") == 0)
                  flag_dotall = false;
                else if (strcmp(arg, "no-empty") == 0)
                  flag_empty = false;
                else if (strcmp(arg, "no-encoding") == 0)
                  flag_encoding = NULL;
                else if (strcmp(arg, "no-filename") == 0)
                  flag_no_filename = true;
                else if (strcmp(arg, "no-files-with-matches") == 0)
                  flag_files_with_matches = false;
                else if (strcmp(arg, "no-filter") == 0)
                  flag_filter.clear();
                else if (strcmp(arg, "no-glob-ignore-case") == 0)
                  flag_glob_ignore_case = false;
                else if (strcmp(arg, "no-group-separator") == 0)
                  flag_group_separator = NULL;
                else if (strcmp(arg, "no-heading") == 0)
                  flag_heading = false;
                else if (strcmp(arg, "no-hidden") == 0)
                  flag_hidden = false;
                else if (strcmp(arg, "no-hyperlink") == 0)
                  flag_hyperlink = NULL;
                else if (strcmp(arg, "no-ignore-binary") == 0)
                  flag_binary_files = "binary";
                else if (strcmp(arg, "no-ignore-case") == 0)
                  flag_ignore_case = false;
                else if (strcmp(arg, "no-ignore-files") == 0)
                  flag_ignore_files.clear();
                else if (strcmp(arg, "no-index") == 0)
                  flag_index = NULL;
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
                else if (strcmp(arg, "no-passthru") == 0)
                  flag_any_line = false;
                else if (strcmp(arg, "no-pretty") == 0)
                  flag_pretty = NULL;
                else if (strcmp(arg, "no-smart-case") == 0)
                  flag_smart_case = false;
                else if (strcmp(arg, "no-sort") == 0)
                  flag_sort = NULL;
                else if (strcmp(arg, "no-split") == 0)
                  flag_split = false;
                else if (strcmp(arg, "no-tree") == 0)
                  flag_tree = false;
                else if (strcmp(arg, "no-stats") == 0)
                  flag_stats = NULL;
                else if (strcmp(arg, "no-ungroup") == 0)
                  flag_ungroup = false;
                else if (strcmp(arg, "no-view") == 0)
                  flag_view = NULL;
                else if (strcmp(arg, "null") == 0)
                  flag_null = true;
                else if (strcmp(arg, "null-data") == 0)
                  flag_null_data = true;
                else if (strcmp(arg, "neg-regexp") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--neg-regexp=, --not, --no-any-line, --no-ascii, --no-binary, --no-bool, --no-break, --no-byte-offset, --no-color, --no-config, --no-confirm, --no-count, --no-decompress, --no-dereference, --no-dereference-files, --no-dotall, --no-encoding, --no-empty, --no-filename, --no-files-with-matches, --no-filter, --no-glob-ignore-case, --no-group-separator, --no-heading, --no-hidden, --no-hyperlink, --no-ignore-binary, --no-ignore-case, --no-ignore-files, --no-index, --no-initial-tab, --no-invert-match, --no-line-number, --no-only-line-number, --no-only-matching, --no-messages, --no-mmap, --no-pager, --no-pretty, --no-smart-case, --no-sort, --no-split, --no-stats, --no-tree, --no-ungroup, --no-view, --null or --null-data");
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
                  flag_pager = "";
                else if (strncmp(arg, "pager=", 6) == 0)
                  flag_pager = strarg(getloptarg(argc, argv, arg + 6, i));
                else if (strcmp(arg, "passthru") == 0)
                  flag_any_line = true;
                else if (strcmp(arg, "perl-regexp") == 0)
                  flag_perl_regexp = true;
                else if (strcmp(arg, "pretty") == 0)
                  flag_pretty = Static::AUTO;
                else if (strncmp(arg, "pretty=", 7) == 0)
                  flag_pretty = strarg(getloptarg(argc, argv, arg + 7, i));
                else
                  usage("invalid option --", arg, "--pager, --passthru, --perl-regexp= or --pretty");
                break;

              case 'q':
                if (strcmp(arg, "query") == 0)
                  flag_query = true;
                else if (strncmp(arg, "query=", 6) == 0)
                  flag_query = (flag_delay = strtonum(getloptarg(argc, argv, arg + 6, i), "invalid argument --query="), true);
                else if (strcmp(arg, "quiet") == 0)
                  flag_quiet = flag_no_messages = true;
                else
                  usage("invalid option --", arg, "--query or --quiet");
                break;

              case 'r':
                if (strncmp(arg, "range=", 6) == 0)
                  strtopos2(getloptarg(argc, argv, arg + 6, i), flag_min_line, flag_max_line, "invalid argument --range=");
                else if (strcmp(arg, "recursive") == 0)
                  flag_directories = "recurse";
                else if (strcmp(arg, "regexp") == 0) // legacy form --regexp PATTERN
                  option_regexp(pattern_args, getloptarg(argc, argv, "", i));
                else if (strncmp(arg, "regexp=", 7) == 0)
                  option_regexp(pattern_args, getloptarg(argc, argv, arg + 7, i));
                else if (strncmp(arg, "replace=", 8) == 0)
                  flag_replace = strarg(getloptarg(argc, argv, arg + 8, i));
                else if (strcmp(arg, "range") == 0 ||
                    strcmp(arg, "replace") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--range=, --recursive, --regexp= or --replace=");
                break;

              case 's':
                if (strcmp(arg, "save-config") == 0)
                  flag_save_config = ".ugrep";
                else if (strncmp(arg, "save-config=", 12) == 0)
                  flag_save_config = strarg(getloptarg(argc, argv, arg + 12, i));
                else if (strcmp(arg, "separator") == 0)
                  flag_separator = NULL;
                else if (strncmp(arg, "separator=", 10) == 0)
                  flag_separator = strarg(arg + 10);
                else if (strcmp(arg, "silent") == 0)
                  flag_quiet = flag_no_messages = true;
                else if (strcmp(arg, "smart-case") == 0)
                  flag_smart_case = true;
                else if (strcmp(arg, "sort") == 0)
                  flag_sort = "name";
                else if (strncmp(arg, "sort=", 5) == 0)
                  flag_sort = strarg(getloptarg(argc, argv, arg + 5, i));
                else if (strcmp(arg, "split") == 0)
                  flag_split = true;
                else if (strcmp(arg, "stats") == 0)
                  flag_stats = "";
                else if (strncmp(arg, "stats=", 6) == 0)
                  flag_stats = strarg(arg + 6);
                else
                  usage("invalid option --", arg, "--save-config, --separator, --silent, --smart-case, --sort, --split or --stats");
                break;

              case 't':
                if (strcmp(arg, "tabs") == 0)
                  flag_tabs = DEFAULT_TABS;
                else if (strncmp(arg, "tabs=", 5) == 0)
                  flag_tabs = strtopos(getloptarg(argc, argv, arg + 5, i), "invalid argument --tabs=");
                else if (strcmp(arg, "tag") == 0)
                  flag_tag = DEFAULT_TAG;
                else if (strncmp(arg, "tag=", 4) == 0)
                  flag_tag = strarg(getloptarg(argc, argv, arg + 4, i));
                else if (strcmp(arg, "text") == 0)
                  flag_binary_files = "text";
                else if (strcmp(arg, "tree") == 0)
                  flag_tree = true;
                else
                  usage("invalid option --", arg, "--tabs, --tag, --text or --tree");
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
                  flag_view = strarg(getloptarg(argc, argv, arg + 5, i));
                else if (strcmp(arg, "view") == 0)
                  flag_view = "";
                else
                  usage("invalid option --", arg, "--view or --version");
                break;

              case 'w':
                if (strcmp(arg, "width") == 0)
                  flag_width = Screen::getsize();
                else if (strncmp(arg, "width=", 6) == 0)
                  flag_width = strtopos(getloptarg(argc, argv, arg + 6, i), "invalid argument --width=");
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
                  flag_zmax = strtopos(getloptarg(argc, argv, arg + 5, i), "invalid argument --zmax=");
                else if (strcmp(arg, "zmax") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--zmax=");
                break;

              default:
                if (isdigit(static_cast<unsigned char>(*arg)))
                  set_depth_long(arg);
                else
                  usage("invalid option --", arg);
            }
            break;

          case 'A':
            flag_after_context = strtonum(getoptarg(argc, argv, arg, i), "invalid argument -A=");
            is_grouped = false;
            break;

          case 'a':
            flag_binary_files = "text";
            break;

          case 'B':
            flag_before_context = strtonum(getoptarg(argc, argv, arg, i), "invalid argument -B=");
            is_grouped = false;
            break;

          case 'b':
            flag_byte_offset = true;
            break;

          case 'C':
            flag_after_context = flag_before_context = strtonum(getoptarg(argc, argv, arg, i), "invalid argument -C=");
            is_grouped = false;
            break;

          case 'c':
            flag_count = true;
            break;

          case 'D':
            flag_devices = getoptarg(argc, argv, arg, i);
            is_grouped = false;
            break;

          case 'd':
            flag_directories = getoptarg(argc, argv, arg, i);
            is_grouped = false;
            break;

          case 'E':
            flag_basic_regexp = false;
            break;

          case 'e':
            option_regexp(pattern_args, getoptarg(argc, argv, arg, i));
            is_grouped = false;
            break;

          case 'F':
            flag_fixed_strings = true;
            break;

          case 'f':
            flag_file.emplace_back(getoptarg(argc, argv, arg, i));
            is_grouped = false;
            break;

          case 'G':
            flag_basic_regexp = true;
            break;

          case 'g':
            flag_glob.emplace_back(getoptarg(argc, argv, arg, i));
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
            flag_jobs = strtonum(getoptarg(argc, argv, arg, i), "invalid argument -J=");
            is_grouped = false;
            break;

          case 'j':
            flag_smart_case = true;
            break;

          case 'K':
            strtopos2(getoptarg(argc, argv, arg, i), flag_min_line, flag_max_line, "invalid argument -K=");
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
            flag_file_magic.emplace_back(getoptarg(argc, argv, arg, i));
            is_grouped = false;
            break;

          case 'm':
            strtopos2(getoptarg(argc, argv, arg, i), flag_min_count, flag_max_count, "invalid argument -m=");
            is_grouped = false;
            break;

          case 'N':
            option_regexp(pattern_args, getoptarg(argc, argv, arg, i), true);
            is_grouped = false;
            break;

          case 'n':
            flag_line_number = true;
            break;

          case 'O':
            flag_file_extension.emplace_back(getoptarg(argc, argv, arg, i));
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
            if (arg[1] == '=')
            {
              ++arg;
              flag_delay = strtonum(arg + 1, "invalid argument -Q=");
              flag_query = true;
              is_grouped = false;
            }
            else
            {
              flag_query = true;
            }
            break;

          case 'q':
            flag_quiet = flag_no_messages = true;
            break;

          case 'R':
            flag_directories = "dereference-recurse";
            break;

          case 'r':
            flag_directories = "recurse";
            break;

          case 'S':
            flag_dereference_files = true;
            break;

          case 's':
            flag_no_messages = true;
            break;

          case 'T':
            flag_initial_tab = true;
            break;

          case 't':
            flag_file_type.emplace_back(getoptarg(argc, argv, arg, i));
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
            if (flag_grep)
            {
              flag_null = true;
            }
            else
            {
              ++arg;
              if (*arg == '=' || strncmp(arg, "best", 4) == 0 || isdigit(static_cast<unsigned char>(*arg)) || strchr("+-~", *arg) != NULL)
              {
                flag_fuzzy = strtofuzzy(&arg[*arg == '='], "invalid argument -Z=");
                is_grouped = false;
              }
              else
              {
                flag_fuzzy = 1;
                --arg;
              }
            }
            break;


          case 'z':
            if (flag_grep)
              flag_null_data = true;
            else
              flag_decompress = true;
            break;

          case '0':
            flag_null_data = flag_null;
            flag_null = !flag_null;
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
            set_depth(arg);
            break;

          case '?':
            help(arg[1] != '\0' ? arg + 1 : ++i < argc ? argv[i] : NULL);
            break;

          case '%':
            if (flag_bool)
              flag_files = true;
            flag_bool = true;
            break;

          case '^':
            flag_tree = true;
            break;

          case '+':
            flag_heading = true;
            break;

          case '.':
            flag_hidden = true;
            break;

          case '@':
            option_all_files();
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
    else if (Static::arg_pattern == NULL && !flag_match && !flag_not && pattern_args.empty() && flag_file.empty())
    {
      // no regex pattern specified yet, so assume it is PATTERN
      Static::arg_pattern = arg;
    }
    else
    {
      // otherwise add the file argument to the list of FILE files
      Static::arg_files.emplace_back(arg);
    }
  }

  if (flag_not)
    usage("missing PATTERN for --not");
}

// parse -e PATTERN and -N PATTERN
void option_regexp(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg, bool is_neg)
{
  pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::ALT) | (is_neg ? CNF::PATTERN::NEG : CNF::PATTERN::ALT), arg);
}

// parse --and [PATTERN]
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  pattern_args.emplace_back(CNF::PATTERN::AND, "");

  if (i + 1 < argc && *argv[i + 1] != '-')
    pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::ALT), argv[++i]);
}

// parse --and=PATTERN
void option_and(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  pattern_args.emplace_back(CNF::PATTERN::AND, "");
  pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::ALT), arg);
}

// parse --andnot [PATTERN]
void option_andnot(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  if (flag_not)
    usage("missing PATTERN for --not");

  pattern_args.emplace_back(CNF::PATTERN::AND, "");

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

  pattern_args.emplace_back(CNF::PATTERN::AND, "");
  pattern_args.emplace_back(CNF::PATTERN::NOT, arg);
}

// parse --not [PATTERN]
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, int& i, int argc, const char **argv)
{
  flag_not = !flag_not;

  if (i + 1 < argc && *argv[i + 1] != '-')
  {
    pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::ALT), argv[++i]);
    flag_not = false;
  }
}

// parse --not=PATTERN
void option_not(std::list<std::pair<CNF::PATTERN,const char*>>& pattern_args, const char *arg)
{
  flag_not = !flag_not;

  pattern_args.emplace_back((flag_not ? CNF::PATTERN::NOT : CNF::PATTERN::ALT), arg);
  flag_not = false;
}

// --all: cancel all previously specified file and directory search restrictions
void option_all_files()
{
  flag_glob.clear();
  flag_iglob.clear();
  flag_exclude.clear();
  flag_exclude_dir.clear();
  flag_exclude_from.clear();
  flag_include.clear();
  flag_include_dir.clear();
  flag_include_from.clear();
  flag_file_type.clear();
  flag_file_extension.clear();
  flag_file_magic.clear();
  flag_ignore_files.clear();
  if (strcmp(flag_binary_files, "without-match") == 0)
    flag_binary_files = "binary";
}

// parse the command-line options and initialize
void init(int argc, const char **argv)
{
  // get home directory path to expand ~ in options with path arguments, using fopen_smart()

#ifdef OS_WIN
  Static::home_dir = getenv("USERPROFILE");
#else
  Static::home_dir = getenv("HOME");
#endif

  // --config=FILE or ---FILE or --no-config: load configuration file first before parsing any other options
  bool no_config = false;

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
    else if (strcmp(argv[i], "--no-config") == 0)
    {
      no_config = true;
    }
  }

  // collect regex pattern arguments -e PATTERN, -N PATTERN, --and PATTERN, --andnot PATTERN
  std::list<std::pair<CNF::PATTERN,const char*>> pattern_args;

  if (flag_config != NULL)
    load_config(pattern_args);

  // reset warnings
  Static::warnings = 0;

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
    // the 'ug' command is equivalent to 'ugrep --config --pretty --sort' to load custom configuration files, when no --config=FILE is specified
    flag_pretty = Static::AUTO;
    flag_sort = "name";
    if (!no_config && flag_config == NULL)
      load_config(pattern_args);
  }
  else if (strncmp(program, "grep", len) == 0)
  {
    // the 'grep' command is equivalent to 'ugrep --grep -G -. --sort'
    flag_basic_regexp = true;
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "egrep", len) == 0)
  {
    // the 'egrep' command is equivalent to 'ugrep --grep -E -. --sort'
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "fgrep", len) == 0)
  {
    // the 'fgrep' command is equivalent to 'ugrep --grep -F -. --sort'
    flag_fixed_strings = true;
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zgrep", len) == 0)
  {
    // the 'zgrep' command is equivalent to 'ugrep --decompress --grep -G -. --sort'
    flag_decompress = true;
    flag_basic_regexp = true;
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zegrep", len) == 0)
  {
    // the 'zegrep' command is equivalent to 'ugrep --decompress --grep -E -. --sort'
    flag_decompress = true;
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }
  else if (strncmp(program, "zfgrep", len) == 0)
  {
    // the 'zfgrep' command is equivalent to 'ugrep --decompress --grep -F -. --sort'
    flag_decompress = true;
    flag_fixed_strings = true;
    flag_grep = true;
    flag_hidden = true;
    flag_sort = "name";
  }

  // parse ugrep command-line options and arguments

  options(pattern_args, argc, argv);

  if (Static::warnings > 0)
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

  // -P disables -F, -G and -Z (P>F>G>E override) and does not allow -N patterns
  if (flag_perl_regexp)
  {
#if defined(HAVE_PCRE2) || defined(HAVE_BOOST_REGEX)
    flag_fixed_strings = false;
    flag_basic_regexp = false;
    if (flag_fuzzy > 0)
      usage("options -P and -Z are not compatible");
    for (const auto& arg : pattern_args)
      if ((arg.first & CNF::PATTERN::NEG) != 0)
        usage("options -P and -N are not compatible");
#else
    usage("option -P is not available in this build configuration of ugrep");
#endif
  }

  // --grep: enable -Y
  if (flag_grep)
    flag_empty = true;

  // -o (or -u) disables -Y to emulate grep behavior when ugrep is aliased to grep/egrep/fgrep
  if (flag_only_matching || flag_ungroup)
    flag_empty = false;

  // -F disables -G (P>F>G>E override)
  if (flag_fixed_strings)
    flag_basic_regexp = false;

  // -e, -N, --and, --andnot, --not
  if (!pattern_args.empty())
  {
    if (flag_bool || flag_query)
    {
      // -Q: if --and, --andnot or --not specified then switch to bool mode, -F can't work with -N
      for (const auto& arg : pattern_args)
      {
        if (arg.first == CNF::PATTERN::AND || arg.first == CNF::PATTERN::NOT)
          flag_bool = true;
        else if (arg.first == CNF::PATTERN::NEG && flag_fixed_strings)
          usage("option -F with -% or -Q does not support -N PATTERN");
      }

      // -%: collect -e, --and, --andnot, --not patterns
      const char *lparen = flag_bool ? "(" : "";
      const char *rparen = flag_bool ? ")" : "";
      bool sep = false;

      for (const auto& arg : pattern_args)
      {
        if (sep)
          flag_regexp.append(arg.first == CNF::PATTERN::AND ? " " : "\n");
        sep = true;
        if (arg.first == CNF::PATTERN::ALT)
          flag_regexp.append(lparen).append(arg.second).append(rparen);
        else if (arg.first == CNF::PATTERN::NEG && *arg.second != '\0')
          flag_regexp.append("(?^").append(arg.second).append(")");
        else if (arg.first == CNF::PATTERN::NOT && *arg.second != '\0')
          flag_regexp.append("-(").append(arg.second).append(")");
        else
          sep = false;
      }

      if (!flag_query)
        Static::bcnf.new_pattern(CNF::PATTERN::ALT, flag_regexp.c_str());
    }
    else
    {
      // populate the CNF with the collected pattern args, each arg points to a persistent command line argv[]
      for (const auto& arg : pattern_args)
      {
        if (arg.first == CNF::PATTERN::AND)
          Static::bcnf.new_term();
        else
          Static::bcnf.new_pattern(arg.first, arg.second);
      }
    }
  }

  // --query: override --pager
  if (flag_query)
    flag_pager = NULL;

  // --tree: require sort to produce directory tree
  if (flag_tree && flag_sort == NULL)
    flag_sort = "name";

  // check TTY info and set colors (warnings and errors may occur from here on)
  terminal();

  // --save-config and --save-config=FILE
  if (flag_save_config != NULL)
  {
    save_config();

    exit(EXIT_OK);
  }

  // --separator: override : and | otherwise use default separators : and |
  if (flag_separator == NULL || *flag_separator == '\0')
    flag_separator = ":";
  else
    flag_separator_bar = flag_separator;

#ifdef OS_WIN
  // save_config() and help() assume text mode, so switch to
  // binary after we're no longer going to call them.
  (void)_setmode(fileno(stdout), _O_BINARY);
#endif

  // --encoding: parse ENCODING value
  if (flag_encoding != NULL)
  {
    int i, j;

    if (strcmp(flag_encoding, "list") == 0)
    {
      // list the encoding_table[]
      for (i = 0; encoding_table[i].format != NULL; ++i)
        std::cerr << encoding_table[i].format << '\n';

      exit(EXIT_ERROR);
    }

    // scan the encoding_table[] for a matching encoding, case insensitive
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
      std::string msg("invalid argument --encoding=ENCODING, valid arguments are");

      for (int i = 0; encoding_table[i].format != NULL; ++i)
        msg.append(" '").append(encoding_table[i].format).append("',");
      msg.pop_back();

      usage(msg.c_str());
    }

    // encoding is the file encoding used by all input files, if no BOM is present
    flag_encoding_type = encoding_table[i].encoding;
  }
  else if (flag_null_data)
  {
    flag_encoding_type = reflex::Input::file_encoding::null_data;
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

  // --hexdump: normalize by assigning flags
  if (flag_hexdump != NULL)
  {
    int context = 0;

    flag_hex_after = flag_after_context + 1;
    flag_hex_before = flag_before_context + 1;

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
          flag_hex_after = 2;
          context = 1;
          break;

        case 'B':
          flag_hex_before = 2;
          context = 2;
          break;

        case 'C':
          flag_hex_after = flag_hex_before = 2;
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

    // disable -ABC line context to use hex binary context
    flag_after_context = flag_before_context = 0;
  }

  // --hex takes priority over --with-hex takes priority over -I takes priority over -a
  if (flag_hex)
    flag_with_hex = (flag_binary_without_match = flag_text = false);
  else if (flag_with_hex)
    flag_binary_without_match = (flag_text = false);
  else if (flag_binary_without_match)
    flag_text = false;

  // --tabs: value should be 1, 2, 4, or 8
  if (flag_tabs && flag_tabs != 1 && flag_tabs != 2 && flag_tabs != 4 && flag_tabs != 8)
    usage("invalid argument --tabs=NUM, valid arguments are 1, 2, 4, or 8");

  // --match: same as specifying an empty "" pattern argument
  if (flag_match)
    Static::arg_pattern = "";

  // if no regex pattern is specified and no -e PATTERN and no -f FILE and not -Q, then exit with usage message
  if (Static::arg_pattern == NULL && pattern_args.empty() && flag_file.empty() && !flag_query)
    usage("no PATTERN specified: specify --match or an empty \"\" pattern to match all input");

  // regex PATTERN should be a FILE argument when -Q or -e PATTERN is specified
  if (!flag_match && Static::arg_pattern != NULL && (flag_query || !pattern_args.empty()))
  {
    Static::arg_files.insert(Static::arg_files.begin(), Static::arg_pattern);
    Static::arg_pattern = NULL;
  }

#ifdef OS_WIN

  // Windows shell does not expand wildcards in arguments, do that now (basename part only)
  if (!Static::arg_files.empty())
  {
    std::vector<const char*> expanded_arg_files;

    for (const auto& arg_file : Static::arg_files)
    {
      std::wstring filename(utf8_decode(arg_file));
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
    Static::arg_files.swap(expanded_arg_files);
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

  if (flag_index != NULL)
    if (strcmp(flag_index, "safe") != 0 && strcmp(flag_index, "fast") != 0 && strcmp(flag_index, "log") != 0)
      usage("invalid argument --index=MODE, valid arguments are 'safe', 'fast' and 'log'");

  if (!flag_stdin && Static::arg_files.empty())
  {
    // if no FILE specified when reading standard input from a TTY then enable -R if not already -r or -R
    if (isatty(STDIN_FILENO) && (flag_directories_action == Action::UNSP || flag_directories_action == Action::RECURSE))
    {
      if (flag_directories_action == Action::UNSP)
        flag_directories_action = Action::RECURSE;

      // recursive search with worker threads
      flag_all_threads = true;
    }
    else
    {
      // if no FILE specified then read standard input
      flag_stdin = true;
    }
  }

  // check FILE arguments, warn about non-existing and non-readable files and directories
  auto file = Static::arg_files.begin();
  while (file != Static::arg_files.end())
  {
#ifdef OS_WIN

    DWORD attr = GetFileAttributesW(utf8_decode(*file).c_str());

    if (attr == INVALID_FILE_ATTRIBUTES)
    {
      // FILE does not exist
      errno = ENOENT;
      warning(NULL, *file);

      file = Static::arg_files.erase(file);
      if (Static::arg_files.empty())
        exit(EXIT_ERROR);
    }
    else
    {
      // use threads to descent into a directory
      if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
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
      // the specified FILE does not exist
      warning(NULL, *file);

      file = Static::arg_files.erase(file);
      if (Static::arg_files.empty())
        exit(EXIT_ERROR);
    }
    else
    {
      if (flag_no_dereference && S_ISLNK(buf.st_mode))
      {
        // -p: skip symlinks
        file = Static::arg_files.erase(file);
        if (Static::arg_files.empty())
          exit(EXIT_ERROR);
      }
#ifdef WITH_WARN_UNREADABLE_FILE_ARG
      else if ((buf.st_mode & S_IRUSR) == 0)
      {
        // the specified file or directory is not readable, even when executing as su
        errno = EACCES;
        warning("cannot read", *file);

        file = Static::arg_files.erase(file);
        if (Static::arg_files.empty())
          exit(EXIT_ERROR);
      }
#endif
      else
      {
        // use threads to descent into a directory
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
    flag_format_open  = "  <file%[\"]$%[ name=\"]I>\n";
    flag_format       = "    <match%[\"]$%[ line=\"]N%[ column=\"]K%[ offset=\"]B>%X</match>\n%u";
    flag_format_close = "  </file>\n";
    flag_format_end   = "</grep>\n";
  }
  else if (flag_only_line_number)
  {
    flag_format_open  = "%[fn]=%+%=";
    flag_format       = "%[fn]=%F%=%[ln]=%n%=%[se]=%s%=%[cn]=%K%=%[bn]=%B%=\n%u";
    flag_format_close = "%R";
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

#if defined(HAVE_STATVFS) || defined(HAVE_STATFS)

  // --exclude-fs: add file system ids to exclude
  for (const auto& mounts : flag_exclude_fs)
  {
    stat_fs_t buf;

    if (mounts.empty())
    {
      if (Static::arg_files.empty())
      {
        // --exclude-fs without MOUNT points and no targets for recursive search: only include the working dir FS
        if (stat_fs(".", &buf) == 0)
          include_fs_ids.insert(fsid_to_uint64(buf.f_fsid));
      }
      else
      {
        // --exclude-fs without MOUNT points: only include FS associated with the specified file and directory targets
        for (const auto& file : Static::arg_files)
          if (stat_fs(file, &buf) == 0)
            include_fs_ids.insert(fsid_to_uint64(buf.f_fsid));
      }
    }
    else
    {
      size_t from = 0;

      while (true)
      {
        size_t to = mounts.find(',', from);
        size_t size = (to == std::string::npos ? mounts.size() : to) - from;

        if (size > 0)
        {
          std::string mount(mounts.substr(from, size));

          if (stat_fs(mount.c_str(), &buf) == 0)
            exclude_fs_ids.insert(fsid_to_uint64(buf.f_fsid));
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
    stat_fs_t buf;

    if (mounts.empty())
    {
      // --include-fs without MOUNT points includes the working directory
      if (stat_fs(".", &buf) == 0)
        include_fs_ids.insert(fsid_to_uint64(buf.f_fsid));
    }
    else
    {
      size_t from = 0;

      while (true)
      {
        size_t to = mounts.find(',', from);
        size_t size = (to == std::string::npos ? mounts.size() : to) - from;

        if (size > 0)
        {
          std::string mount(mounts.substr(from, size));

          if (stat_fs(mount.c_str(), &buf) == 0)
            include_fs_ids.insert(fsid_to_uint64(buf.f_fsid));
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

      // push globs imported from the file to the back of the vectors
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

      // push globs imported from the file to the back of the vectors
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

        size_t i = 0;
        bool found = false;
        bool valid = true;

        // scan the type_table[] for the specified type, allowing type to be a prefix when unambiguous
        for (size_t j = 0; type_table[j].type != NULL; ++j)
        {
          size_t typelen = strlen(type_table[j].type);

          if (size <= typelen && type.compare(0, size, type_table[j].type, size) == 0)
          {
            // an ambiguous prefix is not a valid type
            if (found)
              valid = false;

            found = true;
            i = j;

            // if full type match then we found a valid type match
            if (size == typelen)
            {
              valid = true;
              break;
            }
          }
        }

        // if not found a valid type, then find an unambiguous type with the specified filename suffix
        if (!found && valid)
        {
          for (size_t j = 0; type_table[j].type != NULL; ++j)
          {
            if (islower(static_cast<unsigned char>(type_table[j].type[0])))
            {
              const char *s = strstr(type_table[j].extensions, type.c_str());
              if (s != NULL && (s == type_table[j].extensions || *--s == ','))
              {
                if (found)
                {
                  valid = false;
                  break;
                }
                found = true;
                i = j;
              }
            }
          }
        }

        if (!found || !valid)
        {
          std::string msg("invalid argument -t TYPES, valid arguments are");

          for (i = 0; type_table[i].type != NULL; ++i)
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
      size_t to;

      for (to = from; to < extensions.size() && extensions[to] != ','; ++to)
      {
        if (extensions[to] == '[')
          while (++to < extensions.size() && extensions[to] != ']')
            to += extensions[to] == '\\';
        else if (extensions[to] == '\\')
          ++to;
      }

      size_t size = to - from;

      if (size == 0)
        break;

      bool negate = size > 1 && (extensions[from] == '!' || extensions[from] == '^');

      if (negate)
      {
        ++from;
        --size;
      }

      flag_glob.emplace_back(glob.assign(negate ? "^*." : "*.").append(extensions.substr(from, size)));

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
      Static::magic_pattern.assign(magic_regex, "r");
    Static::magic_matcher.pattern(Static::magic_pattern);
  }

  catch (reflex::regex_error& error)
  {
    abort("option -M: ", error.what());
  }

  // --filter-magic-label: construct Static::filter_magic_pattern and map "magic bytes" to labels
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

  // --filter-magic-label: create a Static::filter_magic_pattern
  try
  {
    // construct Static::filter_magic_pattern DFA
    if (magic_regex.size() > 2)
      Static::filter_magic_pattern.assign(magic_regex, "r");
  }

  catch (reflex::regex_error& error)
  {
    abort("option --filter-magic-label: ", error.what());
  }

#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__)
  // --filter: Cygwin forked process may hang when searching with multiple threads, force one worker thread
  if (!flag_filter.empty())
    flag_jobs = 1;
#endif
}

// check TTY info and set colors
void terminal()
{
  // check if standard output is a TTY
  flag_tty_term = isatty(STDOUT_FILENO) != 0;

  if (flag_query)
  {
    // -Q: disable --quiet
    flag_quiet = false;
  }
  else if (!flag_quiet)
  {
#ifndef OS_WIN

    // if not to a TTY, is output sent to a pager or to /dev/null?
    if (!flag_tty_term)
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

  // --color=WHEN: normalize WHEN argument
  if (flag_color != NULL)
  {
    if (strcmp(flag_color, "never") == 0 || strcmp(flag_color, "no") == 0 || strcmp(flag_color, "none") == 0)
      flag_color = Static::NEVER;
    else if (strcmp(flag_color, "always") == 0 || strcmp(flag_color, "yes") == 0 || strcmp(flag_color, "force") == 0)
      flag_color = Static::ALWAYS;
    else if (strcmp(flag_color, "auto") == 0 || strcmp(flag_color, "tty") == 0 || strcmp(flag_color, "if-tty") == 0)
      flag_color = Static::AUTO;
    else
      usage("invalid argument --color=WHEN, valid arguments are 'never', 'always' and 'auto'");
  }

  // --pretty=WHEN: normalize WHEN argument
  if (flag_pretty != NULL)
  {
    if (strcmp(flag_pretty, "never") == 0 || strcmp(flag_pretty, "no") == 0 || strcmp(flag_pretty, "none") == 0)
      flag_pretty = NULL;
    else if (strcmp(flag_pretty, "always") == 0 || strcmp(flag_pretty, "yes") == 0 || strcmp(flag_pretty, "force") == 0)
      flag_pretty = Static::ALWAYS;
    else if (strcmp(flag_pretty, "auto") == 0 || strcmp(flag_pretty, "tty") == 0 || strcmp(flag_pretty, "if-tty") == 0)
      flag_pretty = Static::AUTO;
    else
      usage("invalid argument --pretty=WHEN, valid arguments are 'never', 'always' and 'auto'");
  }

  // whether to apply colors based on --tag, --query and --pretty
  if (flag_tag != NULL)
    flag_color = NULL;

  if (!flag_quiet)
  {
    if (flag_tty_term || flag_query || flag_pretty == Static::ALWAYS)
    {
      if (flag_pretty != NULL)
      {
        // --pretty: if output is sent to a TTY then enable --color, --heading, -T, -n, --sort and --tree

        // enable --color if not set yet and not --tag
        if (flag_color == NULL && flag_tag == NULL)
          flag_color = Static::ALWAYS;

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

        // enable --tree
        if (flag_tree.is_undefined())
          flag_tree = true;
      }

      if (flag_query)
      {
        // --query: run the interactive query UI

        // enable --heading if not explicitly disabled (enables --break later)
        if (flag_heading.is_undefined())
          flag_heading = true;

        // enable --line-buffered to flush output immediately
        flag_line_buffered = true;
      }
    }

    // --tree: check UTF-8 terminal support, this is a guess on LANG or LC_CTYPE or LC_ALL
    if (flag_tree && (flag_query || flag_files_with_matches || flag_files_without_match || flag_count))
    {
      const char *lang = getenv("LANG");

      if (lang == NULL || strstr(lang, "UTF-8") == NULL)
      {
        lang = getenv("LC_CTYPE");

        if (lang == NULL || strstr(lang, "UTF-8") == NULL)
        {
          lang = getenv("LC_ALL");

          if (lang == NULL || strstr(lang, "UTF-8") == NULL)
            lang = NULL;
        }
      }

      if (lang != NULL)
      {
        Output::Tree::bar = "│ ";
        Output::Tree::ptr = "╰╴";
        Output::Tree::end = "▔ ";
      }
    }

    // --color: (re)set flag_color depending on color term and TTY output
    if (flag_color != NULL)
    {
      if (flag_color == Static::NEVER)
      {
        flag_color = NULL;
      }
      else
      {
#ifdef OS_WIN

        if (flag_tty_term)
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
            flag_color_term = SetConsoleMode(hConOut, outMode) != 0;
          }
#endif
        }

#else

        // check whether we have a color terminal
        if (flag_tty_term)
        {
          const char *val;

          // if NO_COLOR is unset
          if ((val = getenv("NO_COLOR")) == NULL || *val == '\0')
          {
            // check COLORTERM or if TERM supports color
            if (getenv("COLORTERM") != NULL)
              flag_color_term = true;
            else if ((val = getenv("TERM")) != NULL &&
                (strstr(val, "ansi") != NULL ||
                 strstr(val, "xterm") != NULL ||
                 strstr(val, "screen") != NULL ||
                 strstr(val, "color") != NULL))
              flag_color_term = true;
          }
        }

#endif

        if (flag_query)
        {
          // --query: assume that a color terminal is used, save color to use with TUI output
          if (flag_color_term || flag_color == Static::ALWAYS)
            flag_color_query = flag_color;
          flag_color = Static::ALWAYS;
        }
        else if (flag_color == Static::AUTO)
        {
          if (flag_pretty == Static::ALWAYS)
            flag_color = Static::ALWAYS;
          else if (!flag_color_term && flag_save_config == NULL)
            flag_color = NULL;
        }

        if (flag_color != NULL)
        {
          // get GREP_COLOR and GREP_COLORS, when defined
          char *env_grep_color = NULL;
          dupenv_s(&env_grep_color, "GREP_COLOR");
          char *env_grep_colors = NULL;
          dupenv_s(&env_grep_colors, "GREP_COLORS");
          const char *grep_colors = env_grep_colors;
          std::string deprecated_grep_colors;

          // if GREP_COLOR is defined but not GREP_COLORS, use it
          if (env_grep_colors == NULL && env_grep_color != NULL)
            grep_colors = env_grep_color;
          else if (grep_colors == NULL)
            grep_colors = DEFAULT_GREP_COLORS;

          // parse deprecated GREP_COLOR format with ANSI SGR codes for matched text only
          if (grep_colors != NULL && strchr(grep_colors, '=') == NULL)
          {
            // use default colors then override matched text color with the specified color
            deprecated_grep_colors.assign(grep_colors);
            grep_colors = DEFAULT_GREP_COLORS;
          }

          // parse GREP_COLORS
          if (grep_colors != NULL)
          {
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
            set_color(grep_colors, "qp=", color_qp); // TUI prompt
            set_color(grep_colors, "qe=", color_qe); // TUI errors
            set_color(grep_colors, "qr=", color_qr); // TUI regex highlight
            set_color(grep_colors, "qm=", color_qm); // TUI regex meta characters highlight
            set_color(grep_colors, "ql=", color_ql); // TUI regex lists and literals highlight
            set_color(grep_colors, "qb=", color_qb); // TUI regex braces highlight
          }

          // deprecated GREP_COLOR format with ANSI SGR codes for matched text only
          if (!deprecated_grep_colors.empty())
            set_color(deprecated_grep_colors.c_str(), "", color_ms);

          // parse --colors to override GREP_COLORS
          if (flag_colors != NULL && strchr(flag_colors, '=') == NULL)
          {
            // deprecated format with ANSI SGR codes only
            set_color(flag_colors, "", color_mt); // matched text in any line
          }
          else if (flag_colors != NULL)
          {
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
            set_color(flag_colors, "qp=", color_qp); // TUI prompt
            set_color(flag_colors, "qe=", color_qe); // TUI errors
            set_color(flag_colors, "qr=", color_qr); // TUI regex highlight
            set_color(flag_colors, "qm=", color_qm); // TUI regex meta characters highlight
            set_color(flag_colors, "ql=", color_ql); // TUI regex lists and literals highlight
            set_color(flag_colors, "qb=", color_qb); // TUI regex braces highlight
          }

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

          // if not --hyperlink and hl or hl= is specified, then set --hyperlink
          if (flag_hyperlink == NULL)
          {
            if (grep_colors != NULL && strstr(grep_colors, "hl=") != NULL)
              flag_hyperlink = grep_colors + 3;
            else if (flag_colors != NULL && strstr(flag_colors, "hl=") != NULL)
              flag_hyperlink = flag_colors + 3;
            else if (grep_colors != NULL && strstr(grep_colors, "hl") != NULL)
              flag_hyperlink = "";
            else if (flag_colors != NULL && strstr(flag_colors, "hl") != NULL)
              flag_hyperlink = "";
          }

          // --hyperlink
          set_terminal_hyperlink();

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

// --hyperlink parsing
void set_terminal_hyperlink()
{
  // set prefix, host, current working directory path to output hyperlinks with --hyperlink
  if (flag_hyperlink != NULL)
  {
    // get current working directory path in hyperlink
    char *cwd = getcwd0();

    if (cwd != NULL)
    {
      char *path = cwd;

      if (*path == PATHSEPCHR)
        ++path;

      // get host in hyperlink
      char host[80] = "localhost";

#ifdef OS_WIN
      WSADATA w;
      if (WSAStartup(MAKEWORD(1, 1), &w) == 0)
      {
        gethostname(host, sizeof(host));
        WSACleanup();
      }
#else
      gethostname(host, sizeof(host));
#endif

      flag_hyperlink_host.assign(host);
      flag_hyperlink_path.append(path);

      free(cwd);

      // get custom prefix in hyperlink or default file://
      const char *s = flag_hyperlink;

      while (*s != '\0' && isalnum(static_cast<unsigned char>(*s)))
        ++s;

      if (s == flag_hyperlink)
        flag_hyperlink_prefix.assign("file");
      else
        flag_hyperlink_prefix.assign(flag_hyperlink, s - flag_hyperlink);

      flag_hyperlink_line = *s == '+';

      color_hl = "\033]8;;";
      color_st = "\033\\";
    }
  }
}

// search the specified files, directories, and/or standard input for pattern matches, may throw an exception
void ugrep()
{
  // reset stats
  Stats::reset();

  // --tree: reset directory tree output
  Output::Tree::path.clear();
  Output::Tree::depth = 0;

  // clear internal combined all-include and all-exclude globs
  flag_all_include.clear();
  flag_all_exclude.clear();

  // all globs are case-sensitive by default, unless --glob-ignore-case and --iglob are specified
  flag_include_iglob_size = 0;
  flag_exclude_iglob_size = 0;
  flag_include_iglob_dir_size = 0;
  flag_exclude_iglob_dir_size = 0;

  // --iglob: add case-insensitive globs to all-include-iglob/all-exclude-iglob if not --glob-ignore-case
  for (const auto& globs : flag_iglob)
  {
    size_t from = 0;

    while (true)
    {
      size_t to;

      for (to = from; to < globs.size() && globs[to] != ','; ++to)
      {
        if (globs[to] == '[')
          while (++to < globs.size() && globs[to] != ']')
            to += globs[to] == '\\';
        else if (globs[to] == '\\')
          ++to;
      }

      size_t size = to - from;

      if (size == 0)
        break;

      bool negate = size > 1 && (globs[from] == '!' || globs[from] == '^');

      if (negate)
      {
        ++from;
        --size;
        flag_all_exclude.emplace_back(globs.substr(from, size));
        if (globs[from + size - 1] == '/')
          ++flag_exclude_iglob_dir_size;
        else
          ++flag_exclude_iglob_size;
      }
      else
      {
        flag_all_include.emplace_back(globs.substr(from, size));
        if (globs[from + size - 1] == '/')
          ++flag_include_iglob_dir_size;
        else
          ++flag_include_iglob_size;
      }

      from = to + 1;
    }
  }

  // -g, --glob: add globs to all-include/all-exclude
  for (const auto& globs : flag_glob)
  {
    size_t from = 0;

    while (true)
    {
      size_t to;

      for (to = from; to < globs.size() && globs[to] != ','; ++to)
      {
        if (globs[to] == '[')
          while (++to < globs.size() && globs[to] != ']')
            to += globs[to] == '\\';
        else if (globs[to] == '\\')
          ++to;
      }

      size_t size = to - from;

      if (size == 0)
        break;

      bool negate = size > 1 && (globs[from] == '!' || globs[from] == '^');

      if (negate)
      {
        ++from;
        --size;
        flag_all_exclude.emplace_back(globs.substr(from, size));
      }
      else
      {
        flag_all_include.emplace_back(globs.substr(from, size));
      }

      from = to + 1;
    }
  }

  // populate the internal combined all-include and all-exclude globs
  flag_all_include.insert(flag_all_include.begin(), flag_include.begin(), flag_include.end());
  flag_all_exclude.insert(flag_all_exclude.begin(), flag_exclude.begin(), flag_exclude.end());
  flag_all_include_dir = flag_include_dir;
  flag_all_exclude_dir = flag_exclude_dir;

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
  // -z with -M or -O/-t/-g/--include: add globs to also search archive contents for matching files
  if (flag_decompress && (!flag_file_magic.empty() || !flag_all_include.empty()))
  {
    flag_all_include.emplace_back("*.cpio");
    flag_all_include.emplace_back("*.pax");
    flag_all_include.emplace_back("*.tar");
    flag_all_include.emplace_back("*.zip");
    flag_all_include.emplace_back("*.zipx");
    flag_all_include.emplace_back("*.ZIP");

#ifndef WITH_NO_7ZIP
    flag_all_include.emplace_back("*.7z");
    flag_all_include.emplace_back("*.7Z");
#endif

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

#ifdef HAVE_LIBBROTLI
    flag_all_include.emplace_back("*.cpio.br");
    flag_all_include.emplace_back("*.pax.br");
    flag_all_include.emplace_back("*.tar.br");
#endif

#ifdef HAVE_LIBBZIP3
    flag_all_include.emplace_back("*.cpio.bz3");
    flag_all_include.emplace_back("*.pax.bz3");
    flag_all_include.emplace_back("*.tar.bz3");
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

  // keep track which globs are case-insensitive when --glob-ignore-case is specified, i.e. not --ignore-file globs
  if (flag_glob_ignore_case)
  {
    flag_exclude_iglob_size = flag_all_exclude.size();
    flag_exclude_iglob_dir_size = flag_all_exclude_dir.size();
    flag_include_iglob_size = flag_all_include.size();
    flag_include_iglob_dir_size = flag_all_include_dir.size();
  }

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
    else if (strcmp(sort_by, "used") == 0 || strcmp(sort_by, "atime") == 0)
      flag_sort_key = Sort::USED;
    else if (strcmp(sort_by, "changed") == 0 || strcmp(sort_by, "mtime") == 0)
      flag_sort_key = Sort::CHANGED;
    else if (strcmp(sort_by, "created") == 0 || strcmp(sort_by, "ctime") == 0)
      flag_sort_key = Sort::CREATED;
    else if (strcmp(sort_by, "list") == 0)
      flag_sort_key = Sort::LIST;
    else
      usage("invalid argument --sort=KEY, valid arguments are 'name', 'best', 'size', 'used' ('atime'), 'changed' ('mtime'), 'created' ('ctime'), 'list', 'rname', 'rbest', 'rsize', 'rused' ('ratime'), 'rchanged' ('rmtime'), 'rcreated' ('rctime') and 'rlist'");
  }

  // add PATTERN to the CNF
  if (Static::arg_pattern != NULL)
    Static::bcnf.new_pattern(CNF::PATTERN::ALT, Static::arg_pattern);

  // the regex compiled from PATTERN, -e PATTERN, -N PATTERN, and -f FILE
  std::string regex;

  if (Static::bcnf.defined())
  {
    // prune empty terms from the CNF that match anything
    Static::bcnf.prune();

    // split the patterns at newlines, standard grep behavior
    Static::bcnf.split();

    if (flag_file.empty())
    {
      // the CNF patterns to search, this matches more than necessary to support multiline matching and to highlight all matches in color
      regex.assign(Static::bcnf.adjoin());

      // an empty pattern specified matches every line, including empty lines
      if (regex.empty())
      {
        if (Static::bcnf.empty())
        {
          if (!flag_hex)
            flag_only_matching = false;
        }
        else if (!flag_quiet && !flag_files_with_matches && !flag_files_without_match)
        {
          if (flag_hex)
            regex.assign("(?-u)[^\\n]*\\n?"); // include trailing \n of a line when outputting hex per line
          else
            regex.assign("(?-u)[^\\n]*");
        }

        // an empty pattern matches every line, including empty lines
        flag_empty = true;
        flag_dotall = false;
      }

      // CNF is empty if all patterns are empty, i.e. match anything unless -f FILE specified
      if (Static::bcnf.empty())
      {
        flag_match = true;
        flag_dotall = false;
      }
    }
    else
    {
      // -f FILE is combined with -e, --and, --andnot, --not

      if (Static::bcnf.first_empty())
      {
        if (!flag_quiet && !flag_files_with_matches && !flag_files_without_match)
        {
          if (flag_hex)
            regex.assign("(?-u)[^\\n]*\\n?"); // include trailing \n of a line when outputting hex per line
          else
            regex.assign("(?-u)[^\\n]*");
        }

        // an empty pattern specified with -e '' matches every line, including empty lines
        flag_empty = true;
      }
      else
      {
        // for efficiency, take only the first CNF OR-list terms to search in combination with -f FILE patterns
        regex.assign(Static::bcnf.first());
      }
    }
  }

  // -f: get patterns from file
  if (!flag_file.empty())
  {
    // -F: make newline-separated lines in regex literal with \Q and \E
    bool fixed_strings = flag_fixed_strings;
    const char *bar = flag_basic_regexp ? "\\|" : "|";

    // PATTERN or -e PATTERN: add an ending '|' (or BRE '\|') to the regex to concatenate sub-expressions
    if (!regex.empty())
    {
      // -F does not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
      fixed_strings = false;
      regex.append(bar);
    }

    // -f: read patterns from the specified file or files
    for (const auto& filename : flag_file)
    {
      FILE *file = NULL;

      if (fopen_smart(&file, filename.c_str(), "r") != 0)
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
        {
          if (fixed_strings)
            CNF::quote(line);
          regex.append(line).append(bar);
        }
      }

      if (file != stdin)
        fclose(file);
    }

    if (regex.empty())
    {
      if (Static::bcnf.empty())
      {
        if (!flag_hex)
          flag_only_matching = false;
      }
      else if (!flag_quiet && !flag_files_with_matches && !flag_files_without_match)
      {
        if (flag_hex)
          regex.assign("(?-u)[^\\n]*\\n?"); // include trailing \n of a line when outputting hex per line
        else
          regex.assign("(?-u)[^\\n]*");
      }

      // an empty pattern matches every line, including empty lines
      flag_empty = true;
      flag_dotall = false;

      // CNF is empty if all patterns are empty
      if (Static::bcnf.empty())
      {
        flag_match = true;
        flag_word_regexp = false;
      }
    }
    else
    {
      // pop unused ending '|' (or BRE '\|') from the |-concatenated regexes in the regex string
      regex.pop_back();
      if (flag_basic_regexp)
        regex.pop_back();
    }

    // -x or -w: add line or word anchors
    CNF::anchor(regex);
  }

  // patterns ^, $ and ^$ are special cases to optimize for search
  if (!flag_fixed_strings && Static::bcnf.singleton_or_undefined())
  {
    if (regex == "^$")
    {
      regex.clear();
      flag_match = true;
      flag_line_regexp = true;
    }
    else if (regex == "^" || regex == "$")
    {
      flag_match = true;
      regex.clear();
    }
  }

  // -x or --match: enable -Y and disable --dotall and -w
  if (flag_line_regexp || flag_match)
  {
    flag_empty = true;
    flag_dotall = false;
    flag_word_regexp = false;
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

  // -i: disable -j (unconditional overrides conditional)
  if (flag_ignore_case)
    flag_smart_case = false;

  // -j: smart case insensitive search if regex does not contain a Unicode upper case letter
  if (flag_smart_case)
  {
    flag_ignore_case = true;

    for (const char *s = regex.c_str(); *s != '\0'; ++s)
    {
      if (*s == '\\')
      {
        ++s;
      }
      else if (*s == '{')
      {
        while (*++s != '\0' && *s != '}')
          continue;
      }
      else
      {
        int c = reflex::utf8(s, &s);
        if (reflex::Unicode::tolower(c) != c)
        {
          flag_ignore_case = false;
          break;
        }
        --s;
      }
    }
  }

  // -v or -y: disable -o and -u
  if (flag_invert_match || flag_any_line)
    flag_only_matching = flag_ungroup = false;

  // --match: when matching everything disable -ABC unless -o or -x
  if (flag_match && !flag_only_matching && !flag_line_regexp)
    flag_after_context = flag_before_context = 0;

  // -y: disable -ABC
  if (flag_any_line)
    flag_after_context = flag_before_context = 0;

  // -o and --replace: disable -ABC
  if (flag_only_matching && flag_replace != NULL)
    flag_after_context = flag_before_context = 0;

  // --depth: if -R or -r is not specified then enable -r
  if (flag_min_depth > 0 || flag_max_depth > 0)
    flag_directories_action = Action::RECURSE;

  // --index: enable -r if -r or -R is not enabled, reject -dskip
  if (flag_index != NULL)
  {
    if (flag_directories_action == Action::SKIP)
      usage("option --index searches recursively, but option -dskip is specified");
    flag_directories_action = Action::RECURSE;
  }

  // -p (--no-dereference) and -S (--dereference): -p takes priority over -S and -R
  if (flag_no_dereference)
    flag_dereference = flag_dereference_files = false;

  // output file name if more than one input file is specified or options -R, -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (flag_all_threads || flag_directories_action == Action::RECURSE || Static::arg_files.size() > 1 || (flag_stdin && !Static::arg_files.empty())))
    flag_with_filename = true;

  // if no output options -H, -n, -k, -b are set, enable --no-header to suppress headers for speed
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

    // disable sort, unnecessary
    flag_sort_key = Sort::NA;
  }

  // -o and --format: enable -u to ungroup matches
  if (flag_only_matching && flag_format != NULL)
    flag_ungroup = true;

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
  if (flag_heading && flag_with_filename && flag_break.is_undefined())
    flag_break = true;

  // get number of CPU cores, returns 0 if unknown
#if defined(__APPLE__) && defined(HAVE_NEON)
  // apple silicon with 8 or more cores should be reduced to 4 P cores
  Static::cores = 4;
#else
  Static::cores = std::thread::hardware_concurrency();
#endif

  // -J: the default is the number of cores, minus one for >= 8 cores, limited to DEFAULT_MAX_JOBS
  if (flag_jobs == 0)
  {
    flag_jobs = Static::cores - (Static::cores >= 8);

    // we want at least 2 threads to be available for workers if needed, but not more than DEFAULT_MAX_JOBS
    if (flag_jobs < 2)
      flag_jobs = 2;
    else if (flag_jobs > DEFAULT_MAX_JOBS)
      flag_jobs = DEFAULT_MAX_JOBS;
  }

  // --sort and --max-filea and not -l or -L or -c: limit number of threads to --max-files to prevent unordered results, this is a special case
  if (flag_sort_key != Sort::NA && flag_max_files > 0 && !flag_files_with_matches && !flag_count)
    flag_jobs = std::min(flag_jobs, flag_max_files);

  // set the number of threads to the number of files or when recursing to the value of -J, --jobs
  if (flag_all_threads || flag_directories_action == Action::RECURSE)
    Static::threads = flag_jobs;
  else
    Static::threads = std::min(Static::arg_files.size() + flag_stdin, flag_jobs);

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

  // -i: case insensitive reflex::Pattern option
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

  // -Y: literally match closing ) like GNU grep when not paired with ( unless -P or -F
  if (flag_grep && !flag_perl_regexp && !flag_fixed_strings)
    convert_flags |= reflex::convert_flag::closing;

  // --empty: permit empty pattern matches
  if (flag_empty)
    matcher_options.push_back('N');

  // -w: match whole words
  if (flag_word_regexp)
    matcher_options.push_back('W');

  // --tabs: set reflex::Matcher option T to NUM (1, 2, 4, or 8) tab size
  if (flag_tabs)
    matcher_options.append("T=").push_back(static_cast<char>(flag_tabs) + '0');

  // --format-begin
  if (flag_format_begin != NULL)
    Output(Static::output).format(flag_format_begin, 0);

  // --index: search is only possible with compatible options
  if (flag_index != NULL)
  {
    if (flag_perl_regexp)
      usage("options --index and -P (--perl-regexp) are not compatible");
    if (!flag_filter.empty())
      usage("options --index and --filter are not compatible");
    if (flag_fuzzy > 0)
      usage("options --index and -Z (--fuzzy) are not compatible");
    if (flag_invert_match)
      usage("options --index and -v (--invert-match) are not compatible");
    if (flag_encoding != NULL)
      usage("options --index and --encoding are not compatible");

    // -c and --index: force --min-count larger than 0, because indexed search skips non-matching files
    if (flag_count && flag_min_count == 0)
      flag_min_count = 1;
  }

  if (flag_match)
  {
    // --match: match lines

    if (flag_line_regexp)
    {
      // -xv and --match: don't match empty lines (remove LineMatcher option N)
      if (flag_invert_match)
        matcher_options.clear();
      else // -x and --match: only match empty lines (set LineMatcher option W)
        matcher_options.push_back('X');

      // do not invert when searching
      flag_invert_match = false;
    }

    // --match and -v matches nothing, but -y and also -c should still list files when --min-count > 0
    if (!flag_invert_match || flag_any_line || (flag_count && flag_min_count == 0))
    {
      // -X (--hex) and --match: also output terminating \n in hexdumps (set LineMatcher option A)
      if (flag_hex)
        matcher_options.push_back('A');

      // --match: match lines with the RE/flex Line Matcher
      Static::matcher = std::unique_ptr<reflex::AbstractMatcher>(new reflex::LineMatcher(reflex::Input(), matcher_options.c_str()));

      // --pager
      open_pager();

      if (Static::threads > 1)
      {
        GrepMaster grep(Static::output, Static::matcher.get(), NULL);
        grep.ugrep();
      }
      else
      {
        Grep grep(Static::output, Static::matcher.get(), NULL);
        Static::set_grep_handle(&grep);
        grep.ugrep();
        Static::clear_grep_handle();
      }
    }
  }
  else if (flag_perl_regexp)
  {
    // -P: Perl matching with PCRE2 or Boost.Regex
#if defined(HAVE_PCRE2)
    // construct the PCRE2 JIT-optimized Perl pattern matcher
    uint32_t options = flag_binary ? (PCRE2_NEVER_UTF | PCRE2_NEVER_UCP) : (PCRE2_UTF | PCRE2_UCP);
    if (!Static::bcnf.singleton_or_undefined())
      options |= PCRE2_DUPNAMES;
    Static::string_pattern.assign(flag_binary ? reflex::PCRE2Matcher::convert(regex, convert_flags, &flag_multiline) : reflex::PCRE2UTFMatcher::convert(regex, convert_flags, &flag_multiline));
    Static::matcher = std::unique_ptr<reflex::AbstractMatcher>(new reflex::PCRE2Matcher(Static::string_pattern, reflex::Input(), matcher_options.c_str(), options));
    Static::matchers.clear();

    if (!Static::bcnf.singleton_or_undefined())
    {
      std::string subregex;

      for (const auto& i : Static::bcnf.lists())
      {
        Static::matchers.emplace_back();

        auto& submatchers = Static::matchers.back();

        for (const auto& j : i)
        {
          if (j)
          {
            subregex.assign(pattern_options).append(*j);
            submatchers.emplace_back(new reflex::PCRE2Matcher((flag_binary ? reflex::PCRE2Matcher::convert(subregex, convert_flags) : reflex::PCRE2UTFMatcher::convert(subregex, convert_flags)), reflex::Input(), matcher_options.c_str(), options));
          }
          else
          {
            submatchers.emplace_back();
          }
        }
      }
    }

    // --pager
    open_pager();

    if (Static::threads > 1)
    {
      GrepMaster grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
      grep.ugrep();
    }
    else
    {
      Grep grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
      Static::set_grep_handle(&grep);
      grep.ugrep();
      Static::clear_grep_handle();
    }
#elif defined(HAVE_BOOST_REGEX)
    try
    {
      // construct the Boost.Regex Perl pattern matcher
      Static::string_pattern.assign(reflex::BoostPerlMatcher::convert(regex, convert_flags, &flag_multiline));
      Static::matcher = std::unique_ptr<reflex::AbstractMatcher>(new reflex::BoostPerlMatcher(Static::string_pattern, reflex::Input(), matcher_options.c_str()));
      Static::matchers.clear();

      if (!Static::bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : Static::bcnf.lists())
        {
          Static::matchers.emplace_back();

          auto& submatchers = Static::matchers.back();

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

      // --pager
      open_pager();

      if (Static::threads > 1)
      {
        GrepMaster grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        Static::set_grep_handle(&grep);
        grep.ugrep();
        Static::clear_grep_handle();
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

      throw reflex::regex_error(code, Static::string_pattern, error.position() + 1);
    }
#endif
  }
  else
  {
    // construct the RE/flex DFA-based pattern matcher and start matching files
    Static::reflex_pattern.assign(reflex::Matcher::convert(regex, convert_flags, &flag_multiline), (flag_index != NULL ? "hr" : "r"));
    Static::matchers.clear();

    if (flag_fuzzy > 0)
    {
      // -U or --binary: disable fuzzy Unicode matching, ASCII/binary matching with -Z MAX edit distance
      uint16_t max = static_cast<uint16_t>(flag_fuzzy) | (flag_binary ? reflex::FuzzyMatcher::BIN : 0);
      Static::matcher = std::unique_ptr<reflex::AbstractMatcher>(new reflex::FuzzyMatcher(Static::reflex_pattern, max, reflex::Input(), matcher_options.c_str()));

      if (!Static::bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : Static::bcnf.lists())
        {
          Static::matchers.emplace_back();

          auto& submatchers = Static::matchers.back();

          for (const auto& j : i)
          {
            if (j)
            {
              subregex.assign(pattern_options).append(*j);
              Static::reflex_patterns.emplace_back(reflex::FuzzyMatcher::convert(subregex, convert_flags), "r");
              submatchers.emplace_back(new reflex::FuzzyMatcher(Static::reflex_patterns.back(), reflex::Input(), matcher_options.c_str()));
            }
            else
            {
              submatchers.emplace_back();
            }
          }
        }
      }

      // --pager
      open_pager();

      if (Static::threads > 1)
      {
        GrepMaster grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        Static::set_grep_handle(&grep);
        grep.ugrep();
        Static::clear_grep_handle();
      }
    }
    else
    {
      Static::matcher = std::unique_ptr<reflex::AbstractMatcher>(new reflex::Matcher(Static::reflex_pattern, reflex::Input(), matcher_options.c_str()));

      if (!Static::bcnf.singleton_or_undefined())
      {
        std::string subregex;

        for (const auto& i : Static::bcnf.lists())
        {
          Static::matchers.emplace_back();

          auto& submatchers = Static::matchers.back();

          for (const auto& j : i)
          {
            if (j)
            {
              subregex.assign(pattern_options).append(*j);
              Static::reflex_patterns.emplace_back(reflex::Matcher::convert(subregex, convert_flags), "r");
              submatchers.emplace_back(new reflex::Matcher(Static::reflex_patterns.back(), reflex::Input(), matcher_options.c_str()));
            }
            else
            {
              submatchers.emplace_back();
            }
          }
        }
      }

      // --index: perform indexed search using the pattern indexing hash finite state automaton (HFA)
      if (flag_index != NULL && Static::reflex_pattern.has_hfa())
        Static::index_pattern = &Static::reflex_pattern;

      // --pager
      open_pager();

      if (Static::threads > 1)
      {
        GrepMaster grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        grep.ugrep();
      }
      else
      {
        Grep grep(Static::output, Static::matcher.get(), Static::bcnf.singleton_or_undefined() ? NULL : &Static::matchers);
        Static::set_grep_handle(&grep);
        grep.ugrep();
        Static::clear_grep_handle();
      }

      // pattern is out of scope and implicitly deleted, invalidating this pointer
      Static::index_pattern = NULL;
    }
  }

  // --tree with -l or -c but not --format: finish tree output
  if (flag_tree && (flag_files_with_matches || flag_count) && flag_format == NULL)
  {
    Output out(Static::output);
    for (int i = 1; i < Output::Tree::depth; ++i)
      out.str(Output::Tree::end);
    out.nl();
  }

  // --format-end
  if (flag_format_end != NULL)
    Output(Static::output).format(flag_format_end, Stats::found_parts());

  // --stats: output stats when we're done
  if (flag_stats != NULL)
  {
    Stats::report(Static::output);

    Static::bcnf.report(Static::output);

    if (strcmp(flag_stats, "vm") == 0)
    {
      size_t nodes = Static::reflex_pattern.nodes();
      size_t edges = Static::reflex_pattern.edges();
      size_t words = Static::reflex_pattern.words();
      size_t hashes = Static::reflex_pattern.hashes();

      size_t nodes_time = static_cast<size_t>(Static::reflex_pattern.nodes_time());
      size_t edges_time = static_cast<size_t>(Static::reflex_pattern.parse_time() + Static::reflex_pattern.edges_time());
      size_t words_time = static_cast<size_t>(Static::reflex_pattern.words_time());
      size_t study_time = static_cast<size_t>(Static::reflex_pattern.analysis_time());

      fprintf(Static::output, "VM: %zu nodes (%zums) %zu edges (%zums) %zu opcode words (%zums)", nodes, nodes_time, edges, edges_time, words, words_time);
      if (hashes > 0)
        fprintf(Static::output, " %zu hash tables (%zums)", hashes, study_time);
      fprintf(Static::output, NEWLINESTR);
    }
  }

  // --pager
  close_pager();
}

// perform a limited ugrep search on a single file with optional archive part and store up to num results in a vector, may throw an exception, used by the TUI
void ugrep_find_text_preview(const char *filename, const char *partname, size_t from_lineno, size_t max, size_t& lineno, size_t& num, std::vector<std::string>& text)
{
  // we can only return a text preview when the main search engine ugrep() already started and executes, or after it ends
  if (!Static::matcher)
    return;

  // clone matcher, since it is not thread safe to use when the main search engine still executes
  reflex::AbstractMatcher *matcher = Static::matcher->clone();

  // clone the CNF matchers if any are used
  Static::Matchers *matchers = Static::bcnf.singleton_or_undefined() ? NULL : Static::matchers_clone();

  Grep grep(NULL, matcher, matchers);
  grep.find_text_preview(filename, partname, from_lineno, max, lineno, num, text);

  // delete the cloned matchers, if any
  if (matchers != NULL)
    delete matchers;

  // delete the cloned matcher
  delete matcher;
}

// extract a part from an archive and send to a stream, used by the TUI
void ugrep_extract(const char *filename, const char *partname, FILE *output)
{
  Grep grep(NULL, NULL, NULL);
  grep.extract(filename, partname, output);
}

// set the handle to be able to use cancel_ugrep()
void Static::set_grep_handle(Grep *grep)
{
  std::unique_lock<std::mutex> lock(Static::grep_handle_mutex);
  Static::grep_handle = grep;
  lock.unlock();
}

// reset the grep handle
void Static::clear_grep_handle()
{
  std::unique_lock<std::mutex> lock(Static::grep_handle_mutex);
  Static::grep_handle = NULL;
  lock.unlock();
}

// cancel the search
void Static::cancel_ugrep()
{
  std::unique_lock<std::mutex> lock(Static::grep_handle_mutex);
  if (Static::grep_handle != NULL)
    Static::grep_handle->cancel();
  lock.unlock();
}

// search the specified files or standard input for pattern matches
void Grep::ugrep()
{
  // read each input file to find pattern matches
  if (flag_stdin)
  {
    Stats::score_file();

    // search standard input
    search(Static::LABEL_STANDARD_INPUT, static_cast<uint16_t>(flag_fuzzy));
  }

  if (Static::arg_files.empty())
  {
    if (flag_directories_action == Action::RECURSE)
      recurse(1, ".");
  }
  else
  {
#ifndef OS_WIN
    std::pair<std::set<ino_t>::iterator,bool> vino;
#endif

    for (const auto pathname : Static::arg_files)
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

  // skip FILE_ATTRIBUTE_REPARSE_POINT even when it is a symlink
  if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    return Type::SKIP;

  // skip hidden and system files unless explicitly searched with --hidden
  if (!flag_hidden && !is_argument && ((attr & FILE_ATTRIBUTE_HIDDEN) || (attr & FILE_ATTRIBUTE_SYSTEM)))
    return Type::SKIP;

  if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
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
          fprintf(Static::errout, "%sugrep: %s%s%s recursion depth hit hard limit of %d\n", color_off, color_high, pathname, color_off, MAX_DEPTH);
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
            bool ignore_case = &glob < &flag_all_exclude_dir.front() + flag_exclude_iglob_size;
            if (glob.front() == '!')
            {
              if (!ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                ok = true;
            }
            else if (ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
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
            bool ignore_case = &glob < &flag_all_include_dir.front() + flag_include_iglob_size;
            if (glob.front() == '!')
            {
              if (ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                ok = false;
            }
            else if (!ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
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

    bool ok = true;

    if (!flag_all_exclude.empty())
    {
      // exclude files whose pathname matches any one of the --exclude globs unless negated with !
      for (const auto& glob : flag_all_exclude)
      {
        bool ignore_case = &glob < &flag_all_exclude.front() + flag_exclude_iglob_size;
        if (glob.front() == '!')
        {
          if (!ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
            ok = true;
        }
        else if (ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
        {
          ok = false;
        }
      }
      if (!ok)
        return Type::SKIP;
    }

    if (!flag_all_include.empty())
    {
      // include files whose pathname matches any one of the --include globs unless negated with !
      ok = false;
      for (const auto& glob : flag_all_include)
      {
        bool ignore_case = &glob < &flag_all_include.front() + flag_include_iglob_size;
        if (glob.front() == '!')
        {
          if (ok && glob_match(pathname, basename, glob.c_str() + 1), ignore_case)
            ok = false;
        }
        else if (!ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
        {
          ok = true;
        }
      }
      if (!ok && flag_file_magic.empty())
        return Type::SKIP;
    }

    // check magic pattern against the file signature, when --file-magic=MAGIC is specified
    if (!flag_file_magic.empty() && (flag_all_include.empty() || !ok))
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
        size_t match = Static::magic_matcher.input(&stream).scan();
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
        size_t match = Static::magic_matcher.input(reflex::Input(file, flag_encoding_type)).scan();
        if (match == flag_not_magic || match >= flag_min_magic)
        {
          fclose(file);

          Stats::score_file();

          return Type::OTHER;
        }
      }

      fclose(file);

      return Type::SKIP;
    }

    Stats::score_file();

    return Type::OTHER;
  }

#else

  struct stat buf;

  // -R or -S or command line argument FILE to search: follow symlinks to directories
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
        // if symlinked directory, then follow only if -R is specified or if FILE is a command line argument
        if (!symlink || follow)
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
                fprintf(Static::errout, "%sugrep: %s%s%s recursion depth hit hard limit of %d\n", color_off, color_high, pathname, color_off, MAX_DEPTH);
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
                  bool ignore_case = &glob < &flag_all_exclude_dir.front() + flag_exclude_iglob_dir_size;
                  if (glob.front() == '!')
                  {
                    if (!ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                      ok = true;
                  }
                  else if (ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
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
                  bool ignore_case = &glob < &flag_all_include_dir.front() + flag_include_iglob_dir_size;
                  if (glob.front() == '!')
                  {
                    if (ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                      ok = false;
                  }
                  else if (!ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
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
        // if symlinked files, then follow only if -R or -S is specified or if FILE is a command line argument
        if (!symlink || follow || flag_dereference_files)
        {
          // --depth: recursion level not deep enough?
          if (flag_min_depth > 0 && level <= flag_min_depth)
            return Type::SKIP;

          bool ok = true;

          if (!flag_all_exclude.empty())
          {
            // exclude files whose pathname matches any one of the --exclude globs unless negated with !
            for (const auto& glob : flag_all_exclude)
            {
              bool ignore_case = &glob < &flag_all_exclude.front() + flag_exclude_iglob_size;
              if (glob.front() == '!')
              {
                if (!ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                  ok = true;
              }
              else if (ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
              {
                ok = false;
              }
            }
            if (!ok)
              return Type::SKIP;
          }

          if (!flag_all_include.empty())
          {
            // include files whose basename matches any one of the --include globs if not negated with !
            ok = false;
            for (const auto& glob : flag_all_include)
            {
              bool ignore_case = &glob < &flag_all_include.front() + flag_include_iglob_size;
              if (glob.front() == '!')
              {
                if (ok && glob_match(pathname, basename, glob.c_str() + 1, ignore_case))
                  ok = false;
              }
              else if (!ok && glob_match(pathname, basename, glob.c_str(), ignore_case))
              {
                ok = true;
              }
            }
            if (!ok && flag_file_magic.empty())
              return Type::SKIP;
          }

          // check magic pattern against the file signature, when --file-magic=MAGIC is specified
          if (!flag_file_magic.empty() && (flag_all_include.empty() || !ok))
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
              size_t match = Static::magic_matcher.input(&stream).scan();
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
              size_t match = Static::magic_matcher.input(reflex::Input(file, flag_encoding_type)).scan();
              if (match == flag_not_magic || match >= flag_min_magic)
              {
                fclose(file);

                Stats::score_file();

                info = Entry::sort_info(buf);

                return Type::OTHER;
              }
            }

            fclose(file);

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

  std::wstring wglob(utf8_decode(glob));
  HANDLE hFind = FindFirstFileW(wglob.c_str(), &ffd);

  if (hFind == INVALID_HANDLE_VALUE)
  {
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      warning("cannot open directory", pathname);
    return;
  }

#else

#if defined(HAVE_STATVFS) || defined(HAVE_STATFS)

  if (!exclude_fs_ids.empty() || !include_fs_ids.empty())
  {
    stat_fs_t buf;

    if (stat_fs(pathname, &buf) == 0)
    {
      if (exclude_fs_ids.find(fsid_to_uint64(buf.f_fsid)) != exclude_fs_ids.end())
        return;

      if (!include_fs_ids.empty() && include_fs_ids.find(fsid_to_uint64(buf.f_fsid)) == include_fs_ids.end())
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

  // --index: check index file, but only when necessary on demand to store indexed file entries in a map
  bool index_demand = Static::index_pattern != NULL;
  std::map<std::string,bool> indexed;

  // the indexing file stored per indexed directory and index file identifying magic bytes
  static const char ugrep_index_filename[] = "._UG#_Store";
  static const char ugrep_index_file_magic[5] = "UG#\x03";

  // --ignore-files: check if one or more are present to read and extend the file and dir exclusions
  size_t saved_all_exclude_size = 0;
  size_t saved_all_exclude_dir_size = 0;
  bool saved = false;

  if (!flag_ignore_files.empty())
  {
    std::string ignore_filename;

    for (const auto& ignore_file : flag_ignore_files)
    {
      ignore_filename.assign(pathname).append(PATHSEPSTR).append(ignore_file);

      FILE *file = NULL;
      if (fopenw_s(&file, ignore_filename.c_str(), "r") == 0)
      {
        if (!saved)
        {
          saved_all_exclude_size = flag_all_exclude.size();
          saved_all_exclude_dir_size = flag_all_exclude_dir.size();
          saved = true;
        }

        // push globs imported from the ignore file to the back of the vectors
        Stats::ignore_file(ignore_filename);
        import_globs(file, flag_all_exclude, flag_all_exclude_dir, true);
        fclose(file);
      }
    }
  }

  Stats::score_dir();

  std::vector<Entry> file_entries;
  std::vector<Entry> dir_entries;
  std::string entry_pathname;

#ifdef OS_WIN

  std::string cFileName;
  uint64_t list = 0;
  uint64_t index_time = 0;

  do
  {
    cFileName.assign(utf8_encode(ffd.cFileName));

    // search directory entries that aren't . or .. or hidden
    if (cFileName[0] != '.' || (flag_hidden && cFileName[1] != '\0' && cFileName[1] != '.'))
    {
      // --index: do not search index files themselves, even when --hidden is specified
      if (flag_index != NULL && cFileName == ugrep_index_filename)
        continue;

      size_t len = strlen(pathname);

      if (len == 1 && pathname[0] == '.')
        entry_pathname.assign(cFileName);
      else if (len > 0 && pathname[len - 1] == PATHSEPCHR)
        entry_pathname.assign(pathname).append(cFileName);
      else
        entry_pathname.assign(pathname).append(PATHSEPSTR).append(cFileName);

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
          info = list++;
        }
        else
        {
          struct _FILETIME& time = flag_sort_key == Sort::USED ? ffd.ftLastAccessTime : flag_sort_key == Sort::CHANGED ? ffd.ftLastWriteTime : ffd.ftCreationTime;
          info = static_cast<uint64_t>(time.dwLowDateTime) | (static_cast<uint64_t>(time.dwHighDateTime) << 32);
        }
      }

      ino_t inode = 0;
      Type type = select(level + 1, entry_pathname.c_str(), cFileName.c_str(), DIRENT_TYPE_UNKNOWN, inode, info);

      // --index: search indexed files quickly, but only when necessary on demand
      if (type == Type::OTHER && Static::index_pattern != NULL)
      {
        // if we did not check for an index file yet, then check it now on demand and read it when found
        if (index_demand)
        {
          // check no more than once at most
          index_demand = false;

          // check if an index file is present and use it
          std::string index_filename(pathname);
          index_filename.append(PATHSEPSTR).append(ugrep_index_filename);
          std::wstring windex_filename(utf8_decode(index_filename));
          HANDLE hFile = CreateFileW(windex_filename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

          if (hFile != INVALID_HANDLE_VALUE)
          {
            char check_magic[sizeof(ugrep_index_file_magic)];
            DWORD numread = 0;

            // if an index file is present in the directory pathname, then read and stat it
            if (ReadFile(hFile, check_magic, sizeof(ugrep_index_file_magic), &numread, NULL) &&
                numread == sizeof(ugrep_index_file_magic) &&
                memcmp(check_magic, ugrep_index_file_magic, sizeof(ugrep_index_file_magic)) == 0)
            {
              // time of indexing to check which files were modified after indexing
              index_time = Entry::modified_time(hFile);

              // allocate a buffer, not on the stack, because we are in a deeply recursive function
              char *buffer = new char[65536]; // size must be 65536
              uint8_t header[4];
              std::map<std::string,bool>::iterator skip = indexed.end();

              // populate indexed map
              while (true)
              {
                if (!ReadFile(hFile, header, sizeof(header), &numread, NULL) || numread != sizeof(header))
                  break;

                uint16_t basename_size = header[2] | (header[3] << 8);

                if (!ReadFile(hFile, buffer, basename_size, &numread, NULL) || numread != basename_size)
                  break;

                // make basename in buffer 0-terminated
                buffer[basename_size] = '\0';

                // hashes table size, zero to skip empty files and binary files when -I is specified
                uint32_t hashes_size = 0;
                uint8_t logsize = header[1] & 0x1f;
                if (logsize > 0)
                  for (hashes_size = 1; logsize > 0; --logsize)
                    hashes_size <<= 1;

                // sanity check
                if (hashes_size > 65536)
                  break;

                // archives have multiple entries in the index file, we keep the skip iterator to compare entries
                if ((header[1] & 0x40) == 0)
                  skip = indexed.end();

                // we're still looking in the same archive?
                bool same_archive = skip != indexed.end() && skip->first == buffer;

                // if not and archive or not the same archive entry, then add basename to the indexed map
                if (!same_archive)
                {
                  Stats::score_indexed();
                  skip = indexed.emplace(buffer, true).first;
                }

                // now read the hashes into the buffer to check for a possible match
                if (hashes_size > 0 && (!ReadFile(hFile, buffer, hashes_size, &numread, NULL) || numread != hashes_size))
                  break;

                if ((header[1] & 0x80) != 0 && flag_binary_without_match)
                {
                  // -I: if the indexed file to search is a binary file, then skip it
                }
                else if ((header[1] & 0x60) != 0 && !flag_decompress)
                {
                  // not -z: if the indexed file to search is an archive or is a compressed file, then skip it
                }
                else if (hashes_size > 0)
                {
                  // check if the hashed pattern has a potential match with the file's index hash
                  if (Static::index_pattern->match_hfa(reinterpret_cast<const uint8_t*>(buffer), hashes_size))
                  {
                    skip->second = false;
                    if (*flag_index == 'l')
                      fprintf(Static::errout, "INDEX LOG: %s" PATHSEPSTR "%s\n", pathname, skip->first.c_str());
                  }
                  else
                  {
                    // skip indexed file, does not match
                  }
                }
                else if ((header[1] & 0x80) == 0)
                {
                  // skip indexed file
                }
                else
                {
                  // not -I: do not skip non-indexed binary files (marked empty in index) that we need to search
                  skip->second = false;
                  if (*flag_index == 'l')
                    fprintf(Static::errout, "INDEX LOG: %s" PATHSEPSTR "%s (not indexed binary)\n", pathname, skip->first.c_str());
                }
              }

              delete[] buffer;
            }

            CloseHandle(hFile);
          }
        }

        // check if the file to search was indexed and did not match the specified pattern
        const std::map<std::string,bool>::const_iterator skip = indexed.find(cFileName);
        if (skip == indexed.end())
        {
          // a new file that was not indexed
          Stats::score_added();
          if (*flag_index == 'l')
            fprintf(Static::errout, "INDEX LOG: %s (not indexed)\n", entry_pathname.c_str());
        }
        else if (skip->second)
        {
          bool is_changed = Entry::modified_time(ffd) > index_time;

          if (is_changed)
          {
            // search the file that was changed after indexing
            Stats::score_changed();
            if (*flag_index == 'l')
              fprintf(Static::errout, "INDEX LOG: %s (changed)\n", entry_pathname.c_str());
          }
          else
          {
            // skip this indexed file that does not match the specified pattern
            Stats::score_skipped();
            type = Type::SKIP;
          }
        }
      }

      // search entry_pathname, unless searchable directory into which we should recurse
      switch (type)
      {
        case Type::DIRECTORY:
          dir_entries.emplace_back(entry_pathname, 0, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(entry_pathname.c_str(), Entry::UNDEFINED_COST);
          else
            file_entries.emplace_back(entry_pathname, 0, info);
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
  uint64_t list = 0;

  while ((dirent = readdir(dir)) != NULL)
  {
    // search directory entries that aren't . or .. or hidden
    if (dirent->d_name[0] != '.' || (flag_hidden && dirent->d_name[1] != '\0' && dirent->d_name[1] != '.'))
    {
      // --index: do not search index files themselves, even when --hidden is specified
      if (flag_index != NULL && strcmp(dirent->d_name, ugrep_index_filename) == 0)
        continue;

      size_t len = strlen(pathname);

      if (len == 1 && pathname[0] == '.')
        entry_pathname.assign(dirent->d_name);
      else if (len > 0 && pathname[len - 1] == PATHSEPCHR)
        entry_pathname.assign(pathname).append(dirent->d_name);
      else
        entry_pathname.assign(pathname).append(PATHSEPSTR).append(dirent->d_name);

      ino_t inode;
      uint64_t info;
      Type type;

      // search entry_pathname, unless searchable directory into which we should recurse
#if defined(HAVE_STRUCT_DIRENT_D_TYPE) && defined(HAVE_STRUCT_DIRENT_D_INO)
      inode = dirent->d_ino;
      type = select(level + 1, entry_pathname.c_str(), dirent->d_name, dirent->d_type, inode, info);
#else
      inode = 0;
      type = select(level + 1, entry_pathname.c_str(), dirent->d_name, DIRENT_TYPE_UNKNOWN, inode, info);
#endif

      if (flag_sort_key == Sort::LIST)
        info = list++;

      // --index: search indexed files quickly, but only when necessary on demand
      if (type == Type::OTHER && Static::index_pattern != NULL)
      {
        // if we did not check for an index file yet, then check it now on demand and read it when found
        if (index_demand)
        {
          // check no more than once at most
          index_demand = false;

          // check if an index file is present and use it
          FILE *index_file = NULL;
          std::string index_filename(pathname);
          index_filename.append(PATHSEPSTR).append(ugrep_index_filename);

          if (fopenw_s(&index_file, index_filename.c_str(), "rb") == 0)
          {
            char check_magic[sizeof(ugrep_index_file_magic)];

            // if an index file is present in the directory pathname, then read and stat it
            if (fread(check_magic, sizeof(ugrep_index_file_magic), 1, index_file) != 0 &&
                memcmp(check_magic, ugrep_index_file_magic, sizeof(ugrep_index_file_magic)) == 0)
            {
              struct stat buf;

              if (fstat(fileno(index_file), &buf) == 0)
              {
                // time of indexing to check which files were modified after indexing
                uint64_t index_time = Entry::modified_time(buf);

                // allocate a buffer, not on the stack, because we are in a deeply recursive function
                char *buffer = new char[65536]; // size must be 65536
                uint8_t header[4];
                std::string index_pathname;
                bool is_changed = false;
                std::map<std::string,bool>::iterator skip = indexed.end();

                // populate indexed map
                while (true)
                {
                  if (fread(header, sizeof(header), 1, index_file) == 0)
                    break;

                  uint16_t basename_size = header[2] | (header[3] << 8);

                  if (fread(buffer, 1, basename_size, index_file) < basename_size)
                    break;

                  // make basename in buffer 0-terminated
                  buffer[basename_size] = '\0';

                  // hashes table size, zero to skip empty files and binary files when -I is specified
                  uint32_t hashes_size = 0;
                  uint8_t logsize = header[1] & 0x1f;
                  if (logsize > 0)
                    for (hashes_size = 1; logsize > 0; --logsize)
                      hashes_size <<= 1;

                  // sanity check
                  if (hashes_size > 65536)
                    break;

                  // archives have multiple entries in the index file, we keep the skip iterator to compare entries
                  if ((header[1] & 0x40) == 0)
                    skip = indexed.end();

                  // we're still looking in the same archive?
                  bool same_archive = skip != indexed.end() && skip->first == buffer;

                  // if not and archive or not the same archive entry, then add basename to the indexed map
                  if (!same_archive)
                  {
                    Stats::score_indexed();

                    skip = indexed.emplace(buffer, true).first;

                    // not --index=fast: check if the file to search was not modified after indexing
                    if (*flag_index != 'f')
                    {
                      if (len == 1 && pathname[0] == '.')
                        index_pathname.assign(buffer, basename_size);
                      else if (len > 0 && pathname[len - 1] == PATHSEPCHR)
                        index_pathname.assign(pathname).append(buffer, basename_size);
                      else
                        index_pathname.assign(pathname).append(PATHSEPSTR).append(buffer, basename_size);

                      // does the file exist and was changed after indexing?
                      is_changed = stat(index_pathname.c_str(), &buf) == 0 && Entry::modified_time(buf) > index_time;

                      if (is_changed)
                      {
                        // search the file that was changed after indexing
                        skip->second = false;
                        Stats::score_changed();
                        if (*flag_index == 'l')
                          fprintf(Static::errout, "INDEX LOG: %s (changed)\n", index_pathname.c_str());
                      }
                    }
                  }

                  // now read the hashes into the buffer to check for a possible match
                  if (hashes_size > 0 && fread(buffer, hashes_size, 1, index_file) == 0)
                    break;

                  if (!is_changed)
                  {
                    if ((header[1] & 0x80) != 0 && flag_binary_without_match)
                    {
                      // -I: if the indexed file to search is a binary file, then skip it
                    }
                    else if ((header[1] & 0x60) != 0 && !flag_decompress)
                    {
                      // not -z: if the indexed file to search is an archive or is a compressed file, then skip it
                    }
                    else if (hashes_size > 0)
                    {
                      // check if the hashed pattern has a potential match with the file's index hash
                      if (Static::index_pattern->match_hfa(reinterpret_cast<const uint8_t*>(buffer), hashes_size))
                      {
                        skip->second = false;
                        if (*flag_index == 'l')
                          fprintf(Static::errout, "INDEX LOG: %s\n", index_pathname.c_str());
                      }
                      else
                      {
                        // skip indexed file, not changed and does not match
                      }
                    }
                    else if ((header[1] & 0x80) == 0)
                    {
                      // skip indexed file, not changed and is empty
                    }
                    else
                    {
                      // not -I: do not skip non-indexed binary files (marked empty in index) that we need to search
                      skip->second = false;
                      if (*flag_index == 'l')
                        fprintf(Static::errout, "INDEX LOG: %s (not indexed binary)\n", index_pathname.c_str());
                    }
                  }
                }

                delete[] buffer;
              }
            }
          }

          if (index_file != NULL)
            fclose(index_file);
        }

        // check if the file to search was indexed and did not match the specified pattern
        const std::map<std::string,bool>::const_iterator skip = indexed.find(dirent->d_name);
        if (skip == indexed.end())
        {
          // a new file that was not indexed
          Stats::score_added();
          if (*flag_index == 'l')
            fprintf(Static::errout, "INDEX LOG: %s (not indexed)\n", entry_pathname.c_str());
        }
        else if (skip->second)
        {
          // skip this indexed file that does not match the specified pattern
          Stats::score_skipped();
          type = Type::SKIP;
        }
      }

      switch (type)
      {
        case Type::DIRECTORY:
          dir_entries.emplace_back(entry_pathname, inode, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(entry_pathname.c_str(), Entry::UNDEFINED_COST);
          else
            file_entries.emplace_back(entry_pathname, inode, info);
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
  dir = NULL;

#endif

  // -Z and --sort=best without --match: presearch the selected files to determine edit distance cost
  if (flag_fuzzy > 0 && flag_sort_key == Sort::BEST && !flag_match)
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

  // --ignore-files: restore all exclusions when saved by popping off all additions
  if (saved)
  {
    flag_all_exclude.resize(saved_all_exclude_size);
    flag_all_exclude_dir.resize(saved_all_exclude_dir_size);
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

// search input to output the pattern matches
void Grep::search(const char *pathname, uint16_t cost)
{
  // -Zbest (or --best-match) without --match: compute cost if not yet computed by --sort=best
  if (flag_best_match && flag_fuzzy > 0 && !flag_match && !flag_quiet && (!flag_files_with_matches || flag_format != NULL) && matchers == NULL && pathname != Static::LABEL_STANDARD_INPUT)
  {
    // -Z: matcher is a FuzzyMatcher for sure
    reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);
    fuzzy_matcher->distance(static_cast<uint16_t>(flag_fuzzy));

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
    fuzzy_matcher->distance((cost & 0xff) | (flag_fuzzy & 0xff00));
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
  if (pathname == Static::LABEL_STANDARD_INPUT)
    pathname = flag_label;

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

        // --format with open/close or --max-files: we must out.acquire() lock before Stats::found_part()
        bool acquire = (flag_format != NULL && (flag_format_open != NULL || flag_format_close != NULL)) || (!flag_quiet && flag_max_files > 0);

        bool heading = flag_with_filename;
        size_t lineno = 0;

        // skip line number counting, only count matching lines
        if (!flag_multiline && flag_max_line == 0 && flag_stats == NULL)
          matcher->lineno_skip(true);

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
          matches = (matches == 0);

        if (matches > 0)
        {
          // --format with open/close or --max-files: we must out.acquire() lock before Stats::found_part()
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
                out.format(flag_format_open, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);
              out.format(flag_format, pathname, partname, 1, NULL, matcher, heading, false, false);
              if (flag_format_close != NULL)
                out.format(flag_format_close, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);
            }
            else
            {
              out.header(pathname, partname);

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

        // --format with open/close or --max-files: we must out.acquire() lock before Stats::found_part()
        bool acquire = (flag_format != NULL && (flag_format_open != NULL || flag_format_close != NULL)) || flag_max_files > 0;
        bool heading = flag_with_filename;

        if (!flag_match || !flag_invert_match)
        {
          if (flag_ungroup || flag_only_matching)
          {
            // -co or -cu: count the number of patterns matched

            // skip line number counting, only count matching lines
            if (!flag_multiline && flag_max_line == 0 && flag_stats == NULL)
              matcher->lineno_skip(true);

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
          else if (!flag_multiline && !flag_invert_match && flag_max_line == 0 && matchers == NULL && flag_stats == NULL)
          {
            // -c without -o/-u/-v/-K/-%: count the number of matching lines DO NOT KEEP LINE COUNT!

            // skip line number counting, only count matching lines
            matcher->lineno_skip(true);

            while (matcher->find())
            {
              ++matches;

              // -m: max number of matches reached?
              if (flag_max_count > 0 && matches >= flag_max_count)
                break;

              // if the match does not span more than one line, then skip to end of the line (we count matching lines)
              if (!matcher->at_bol())
                matcher->skip('\n');
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

                // if the match does not span more than one line, then skip to end of the line (we count matching lines)
                if (!flag_multiline && !matcher->at_bol())
                  matcher->skip('\n');
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
          // --format with open/close or --max-files: we must out.acquire() lock before Stats::found_part()
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
            out.format(flag_format_open, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);
          out.format(flag_format, pathname, partname, matches, NULL, matcher, heading, false, false);
          if (flag_format_close != NULL)
            out.format(flag_format_close, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);
        }
        else
        {
          if (flag_with_filename || !partname.empty())
          {
            out.header(pathname, partname);

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

        // --format with open/close: we must out.acquire() lock before Stats::found_part()
        bool acquire = flag_format_open != NULL || flag_format_close != NULL;
        bool heading = flag_with_filename;

        if (flag_invert_match)
        {
          // FormatInvertMatchHandler requires lineno to be set precisely, i.e. after skipping --range lines
          size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
          bool binfile = false; // unused
          bool hex = false;     // unused
          bool binary = false;  // unused
          bool stop = false;

          // construct event handler functor with captured *this and some of the locals
          FormatInvertMatchGrepHandler invert_match_handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

          // register the event handler to output non-matching lines
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

              // output non-matching lines up to this line
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

          // get the remaining context after the last match to output
          context = matcher->after();

          if (context.len > 0)
            invert_match_handler(*matcher, context.buf, context.len, context.num);
        }
        else
        {
          size_t lineno = 0;
          size_t matching = 0; // format match counter per match, differs from matches counter per line

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
              if (!cnf_matching(bol, eol, acquire))
                continue;
            }

            if (current_lineno != lineno || flag_ungroup)
            {
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
                  out.format(flag_format_open, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);

                  // --files: undo files count
                  if (flag_files && matchers != NULL && out.holding())
                    Stats::undo_found_part();
                }
              }
            }

            // output --format=FORMAT
            // if the match does not span more than one line, then skip to end of the line
            if (!out.format(flag_format, pathname, partname, matches, &matching, matcher, heading, matches > 1 || current_lineno == lineno, matches > 1))
              if (!flag_multiline && !matcher->at_bol())
                if (!matcher->skip('\n'))
                  break;

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            out.check_flush();

            lineno = current_lineno;
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
          out.format(flag_format_close, pathname, partname, Stats::found_parts(), NULL, matcher, heading, false, Stats::found_parts() > 1);
      }
      else if (flag_only_matching && flag_before_context == 0 && flag_after_context == 0)
      {
        // option -o without -ABC

        size_t lineno = 0;
        size_t matching = 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool nl = false;

        // skip line number counting, only count matching lines
        if (!flag_line_number && !flag_multiline && flag_max_line == 0 && flag_stats == NULL)
          matcher->lineno_skip(true);

        while (matcher->find())
        {
          const char *begin = matcher->begin();
          size_t size = matcher->size();

          binary = flag_hex || (flag_with_hex && is_binary(begin, size));

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

          if (nl)
          {
            out.nl();
            nl = false;
          }

          // --range: max line exceeded?
          if (flag_max_line > 0 && current_lineno > flag_max_line)
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

          if (lineno != current_lineno || flag_ungroup)
          {
            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
            {
              lineno = current_lineno;
              continue;
            }

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches > flag_max_count)
              break;

            if (matches == flag_min_count + (flag_min_count == 0) && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }
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
            out.header(pathname, partname, heading, current_lineno, matcher, matcher->first(), flag_separator, binary);

          lineno = current_lineno;

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
            out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
            out.str(match_off);
          }
          else
          {
            if (flag_multiline)
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

                out.header(pathname, partname, heading, ++lineno, NULL, matcher->first() + (to - begin) + 1, flag_separator_bar, false);

                from = to + 1;
              }

              size -= from - begin;
              begin = from;
            }

            if (size > 0)
            {
              bool lf_only = begin[size - 1] == '\n';
              size -= lf_only;
              if (size > 0)
              {
                out.str(match_ms);
                out.str(begin, size);
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
          out.nl();

        if (hex)
          out.dump.done();
      }
      else if (flag_only_matching)
      {
        // option -o with -ABC specified

        size_t count = 0;
        size_t width = flag_before_context + flag_after_context;
        size_t lineno = 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool nl = false;
        bool stop = false;

        // skip line number counting, only count matching lines
        if (!flag_line_number && !flag_multiline && flag_max_line == 0 && flag_stats == NULL)
          matcher->lineno_skip(true);

        // construct event handler functor with captured *this and some of the locals
        GrepHandler handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler to update restline on buffer shift
        matcher->set_handler(&handler);

        // the rest of the matching line
        restline_data = NULL;
        restline_size = 0;
        restline_last = 0;

        while (matcher->find())
        {
          const char *begin = matcher->begin();
          size_t size = matcher->size();

          binary = flag_hex || (flag_with_hex && is_binary(begin, size));

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

          if (nl)
          {
            if (restline_size > 0)
            {
              out.str(color_cx);
              if (utf8nlen(restline_data, restline_size) > width + 3)
              {
                out.utf8strn(restline_data, restline_size, width);
                out.str(color_off);
                out.str(color_se);
                out.str("...", 3);
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
              out.str("...", 3);
              out.str(color_off);
              out.str(color_cn);
              out.str("[+", 2);
              out.num(count);
              out.str(" more]", 6);
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

          if (lineno != current_lineno || flag_ungroup)
          {
            ++matches;

            // --min-count: require at least min-count matches
            if (flag_min_count > 0 && matches < flag_min_count)
            {
              lineno = current_lineno;
              continue;
            }

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches > flag_max_count)
              break;

            if (matches == flag_min_count + (flag_min_count == 0) && (!flag_files || matchers == NULL))
            {
              // --max-files: max reached?
              if (!Stats::found_part())
                goto exit_search;
            }
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
            out.header(pathname, partname, heading, current_lineno, matcher, matcher->first(), flag_separator, binary);

          lineno = current_lineno;

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
          else
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
              while (true)
              {
                // check for additional lines in the match output
                size_t rest = 0;
                const char *to = static_cast<const char*>(memchr(begin, '\n', size));
                if (to != NULL)
                {
                  rest = size;
                  size = to - begin;
                  rest -= size;
                }

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
                    out.str("...", 3);
                    out.str(color_off);
                    out.str(color_cx);
                    out.utf8strn(utf8skipn(bol, border, margin - before), border, before);
                    out.str(color_off);
                    width -= before;
                  }
                  else
                  {
                    if (border > 0)
                    {
                      out.str(color_cx);
                      out.str(bol, border);
                      out.str(color_off);
                    }
                    if (margin >= 3)
                      width -= margin - 3;
                    else
                      width += 3 - margin;
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
                  out.str("[+", 2);
                  out.num(length - fit_length);
                  out.chr(']');
                }
                out.str(match_off);

                // no more additional lines to output?
                if (to == NULL)
                  break;

                out.nl();

                // output header and repeat to output a multiline match after previous newline
                if (!flag_no_header)
                  out.header(pathname, partname, heading, ++lineno, matcher, matcher->first() + (to - begin), flag_separator_bar, false);

                begin = to + 1;
                size = rest - 1;
                width = flag_before_context + flag_after_context;

                nl = true;
              }

              const char *eol = matcher->eol(); // warning: call eol() before bol() and end()
              const char *end = matcher->end();

              restline_data = end;
              restline_size = eol - end;
              restline_last = matcher->last();
            }

            nl = true;
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
            if (utf8nlen(restline_data, restline_size) > width + 3)
            {
              out.utf8strn(restline_data, restline_size, width);
              out.str(color_off);
              out.str(color_se);
              out.str("...", 3);
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
            out.str("...", 3);
            out.str(color_off);
            out.str(color_cn);
            out.str("[+", 2);
            out.num(count);
            out.str(" more]", 6);
            out.str(color_off);
          }

          out.nl();
        }

        if (hex)
          out.dump.done();
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line && !flag_invert_match)
      {
        // options -ABC, -y, -v are not specified, --hexdump context is supported (including with options -ABC)

        size_t lineno = 0;
        size_t matching = 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;
        bool colorize = flag_color != NULL || flag_replace != NULL || flag_tag != NULL;

        // skip line number counting, only count matching lines
        if (!flag_line_number && !flag_multiline && flag_max_line == 0 && flag_stats == NULL)
          matcher->lineno_skip(true);

        // construct event handler functor with captured *this and some of the locals
        GrepHandler handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler to update restline on buffer shift
        matcher->set_handler(&handler);

        // --hexdump: keep -B NUM+1 before context times hex column bytes in the buffer when shifting
        if (flag_hex_before > 0)
          matcher->set_reserve(flag_hex_before * flag_hex_columns);

        // the rest of the matching line
        restline_data = NULL;
        restline_size = 0;
        restline_last = 0;

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
                  if (restline_last + right > matcher->first())
                    right = matcher->first() - restline_last;
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

            binary = flag_hex || (flag_with_hex && is_binary(bol, eol - bol));

            if (binary && (flag_hex_after > 0 || flag_hex_before > 0))
            {
              const char *aft = matcher->aft((flag_hex_after + flag_hex_before) * flag_hex_columns);
              if (aft > eol)
              {
                eol = aft;
                bol = matcher->bol(); // warning: bol() position may have shifted with aft()
              }
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

            size_t border = matcher->border();
            size_t first = matcher->first();
            const char *begin = matcher->begin();
            const char *end = matcher->end();
            size_t size = matcher->size();

            if (hex && !binary)
              out.dump.done();

            if (!flag_no_header)
              out.header(pathname, partname, heading, current_lineno, matcher, first, flag_separator, binary);

            hex = binary;

            lineno = current_lineno;

            if (binary)
            {
              if (flag_hex_before > 0)
              {
                const char *bef = matcher->bef(flag_hex_before * flag_hex_columns);
                if (bef < bol)
                {
                  bol =  bef;
                  border = begin - bol;
                }

                size_t left = 0;
                if (restline_last + restline_size < first)
                {
                  left = flag_hex_before * flag_hex_columns + (first % flag_hex_columns) - flag_hex_columns;
                  if (restline_last + restline_size + left > first)
                    left = first - (restline_last + restline_size);
                }

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
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              if (border > 0)
              {
                out.str(color_sl);
                out.str(bol, border);
                out.str(color_off);
              }

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(match_ms);
                out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
                out.str(match_off);
              }
              else
              {
                if (flag_multiline)
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

                    out.header(pathname, partname, heading, ++lineno, NULL, first + (to - begin) + 1, flag_separator_bar, false);

                    from = to + 1;
                  }

                  size -= from - begin;
                  begin = from;
                }

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
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }
            }

            // no -u, no --hexdump and no colors: if the match does not span more than one line, then skip to end of the line
            if (!flag_ungroup && !colorize && !(binary && (flag_hex_after > 0 || flag_hex_before > 0)))
              if (!flag_multiline && !matcher->at_bol())
                if (!matcher->skip('\n'))
                  break;
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
                  if (restline_data != NULL)
                  {
                    if (restline_last + restline_size > first)
                      restline_size = first - restline_last;

                    if (flag_hex_after > 0)
                    {
                      size_t right = flag_hex_after * flag_hex_columns - ((restline_last - 1) % flag_hex_columns) - 1;
                      if (right < restline_size)
                      {
                        out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, right);

                        restline_data += right;
                        restline_size -= right;
                        restline_last += right;
                      }
                      else
                      {
                        out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);

                        restline_data = NULL;
                      }
                    }

                    if (restline_data != NULL)
                    {
                      if (flag_hex_before > 0)
                      {
                        size_t left = flag_hex_before * flag_hex_columns + (first % flag_hex_columns) - flag_hex_columns;
                        if (left < restline_size)
                        {
                          if (!flag_no_header)
                            out.header(pathname, partname, heading, current_lineno, matcher, first, flag_separator_bar, binary);

                          restline_data = restline_data + restline_size - left;
                          restline_last = restline_last + restline_size - left;
                          restline_size = left;
                        }
                      }

                      out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
                    }

                    restline_data = NULL;
                  }

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
                    out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
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

                        out.header(pathname, partname, heading, lineno + num, NULL, first + (to - begin) + 1, flag_separator_bar, false);

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

                const char *eol = matcher->eol(true); // warning: call eol() before end()
                const char *end = matcher->end();

                if (lines == 1)
                {
                  if (binary && (flag_hex_after > 0 || flag_hex_before > 0))
                  {
                    const char *aft = matcher->aft((flag_hex_after + flag_hex_before) * flag_hex_columns);
                    if (aft > eol)
                    {
                      eol = aft;
                      end = matcher->end(); // warning: end() position may have shifted with aft()
                    }
                  }

                  restline_data = end;
                  restline_size = eol - end;
                  restline_last = last;
                }
                else
                {
                  binary = flag_hex || (flag_with_hex && is_binary(end, eol - end));

                  if (binary && (flag_hex_after > 0 || flag_hex_before > 0))
                  {
                    const char *aft = matcher->aft((flag_hex_after + flag_hex_before) * flag_hex_columns);
                    if (aft > eol)
                    {
                      eol = aft;
                      end = matcher->end(); // warning: end() position may have shifted with aft()
                    }
                  }

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, heading, lineno + lines - 1, matcher, last, flag_separator, binary);

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
                    restline_data = end;
                    restline_size = eol - end;
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
        // option -v without -ABC, -y

        // InvertMatchHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // construct event handler functor with captured *this and some of the locals
        InvertMatchGrepHandler invert_match_handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler to output non-matching lines
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

            // output non-matching lines up to this line
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

        // get the remaining context after the last match to output
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
        size_t matching = 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;
        bool colorize = flag_color != NULL || flag_replace != NULL || flag_tag != NULL;

        // to output the rest of the matching line
        restline_data = NULL;
        restline_size = 0;
        restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        AnyLineGrepHandler any_line_handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler functor to output non-matching lines
        matcher->set_handler(&any_line_handler);

        // to output colors with or without -v
        short v_hex_line = flag_invert_match ? Output::Dump::HEX_CONTEXT_LINE : Output::Dump::HEX_LINE;
        short v_hex_match = flag_invert_match ? Output::Dump::HEX_CONTEXT_MATCH : Output::Dump::HEX_MATCH;
        const char *v_color_sl = flag_invert_match ? color_cx : color_sl;
        const char *v_match_ms = flag_invert_match ? match_mc : match_ms;
        const char *separator = flag_invert_match ? flag_separator_dash : flag_separator;

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

            binary = flag_hex || (flag_with_hex && is_binary(bol, eol - bol));

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
              out.header(pathname, partname, heading, current_lineno, matcher, first, separator, binary);

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
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              if (border > 0)
              {
                out.str(v_color_sl);
                out.str(bol, border);
                out.str(color_off);
              }

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(v_match_ms);
                out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
                out.str(match_off);

                lineno += matcher->lines() - 1;
              }
              else
              {
                if (flag_multiline)
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

                    out.header(pathname, partname, heading, ++lineno, NULL, first + (to - begin) + 1, flag_separator_bar, false);

                    from = to + 1;
                  }

                  size -= from - begin;
                  begin = from;
                }

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
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }
            }

            // no -u and no colors: if the match does not span more than one line, then skip to end of the line
            if (!flag_ungroup && !colorize)
              if (!flag_multiline && !matcher->at_bol())
                if (!matcher->skip('\n'))
                  break;
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
                    out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
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

                        out.header(pathname, partname, heading, lineno + num, NULL, first + (to - begin) + 1, flag_separator_bar, false);

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

                  binary = flag_hex || (flag_with_hex && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, heading, lineno + lines - 1, matcher, last, flag_separator, binary);

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
                    restline_data = end;
                    restline_size = eol - end;
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

        // get the remaining context after the last match to output
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
        // options -ABC without -v

        // ContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        size_t matching = 0;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;
        bool colorize = flag_color != NULL || flag_replace != NULL || flag_tag != NULL;

        // to output the rest of the matching line
        restline_data = NULL;
        restline_size = 0;
        restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        ContextGrepHandler context_handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler functor to output non-matching lines
        matcher->set_handler(&context_handler);

        // to get the context from the context handler explicitly
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

            // output blocked?
            if (out.eof)
              goto exit_search;

            // --max-count: max number of matches reached?
            if (flag_max_count > 0 && matches > flag_max_count)
            {
              if (flag_after_context == 0 || matches > flag_max_count + 1)
                break;

              // one more iteration to get the after context to output
              lineno = current_lineno;
              context_handler.set_after_lineno(lineno + matcher->lines());
              continue;
            }

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              context_handler(*matcher, context.buf, context.len, context.num);

            context_handler.output_before_context();

            binary = flag_hex || (flag_with_hex && is_binary(bol, eol - bol));

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
              out.header(pathname, partname, heading, current_lineno, matcher, first, flag_separator, binary);

            hex = binary;

            lineno = current_lineno;

            if (binary)
            {
              out.dump.hex(Output::Dump::HEX_LINE, first - border, bol, border);
              out.dump.hex(Output::Dump::HEX_MATCH, first, begin, size);

              if (flag_ungroup)
              {
                out.dump.hex(Output::Dump::HEX_LINE, matcher->last(), end, eol - end);
                out.dump.done();
              }
              else
              {
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }

              lineno += matcher->lines() - 1;
            }
            else
            {
              if (border > 0)
              {
                out.str(color_sl);
                out.str(bol, border);
                out.str(color_off);
              }

              if (flag_replace != NULL)
              {
                // output --replace=FORMAT
                out.str(match_ms);
                out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
                out.str(match_off);

                lineno += matcher->lines() - 1;
              }
              else
              {
                if (flag_multiline)
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

                    out.header(pathname, partname, heading, ++lineno, NULL, first + (to - begin) + 1, flag_separator_bar, false);

                    from = to + 1;
                  }

                  size -= from - begin;
                  begin = from;
                }

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
                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }
            }

            // no -u and no colors: if the match does not span more than one line, then skip to end of the line
            if (!flag_ungroup && !colorize)
              if (!flag_multiline && !matcher->at_bol())
                if (!matcher->skip('\n'))
                  break;
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
                  if (restline_data != NULL)
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
                    out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
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

                        out.header(pathname, partname, heading, lineno + num, NULL, first + (to - begin) + 1, flag_separator_bar, false);

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

                  binary = flag_hex || (flag_with_hex && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, heading, lineno + lines - 1, matcher, last, flag_separator, binary);

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
                    restline_data = end;
                    restline_size = eol - end;
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

        // get the remaining context after the last match to output
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
        // options -ABC with -v

        // InvertContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        size_t last_lineno = lineno;
        size_t next_lineno = lineno + 1;
        size_t matching = 0;
        size_t after = flag_after_context;
        bool heading = flag_with_filename;
        bool binfile = !flag_text && !flag_hex && !flag_with_hex && !flag_binary_without_match && init_is_binary();
        bool hex = false;
        bool binary = false;
        bool stop = false;
        bool colorize = flag_color != NULL || flag_replace != NULL || flag_tag != NULL;

        // to output the rest of the matching line
        restline_data = NULL;
        restline_size = 0;
        restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        InvertContextGrepHandler invert_context_handler(*this, pathname, lineno, heading, binfile, hex, binary, matches, stop);

        // register the event handler functor to output non-matching lines
        matcher->set_handler(&invert_context_handler);

        // to get the context from the context handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();
          size_t lines = matcher->lines();

          // adjust the number of after lines output so far upward if uninterrupted, or reset to zero
          if (last_lineno != current_lineno)
          {
            if (next_lineno >= current_lineno)
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
              binary = flag_hex || (flag_with_hex && is_binary(bol, eol - bol));

              if (binfile || (binary && !flag_hex && !flag_with_hex))
              {
                if (flag_binary_without_match)
                {
                  matches = 0;
                }
                else // if (flag_invert_match) is true
                {
                  last_lineno = current_lineno;
                  next_lineno = current_lineno + lines;
                  lineno = next_lineno - 1;
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
                out.header(pathname, partname, heading, lineno, matcher, first, flag_separator_dash, binary);

              hex = binary;

              if (binary)
              {
                if (flag_hex || flag_with_hex)
                {
                  out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, first - border, bol, border);
                  out.dump.hex(Output::Dump::HEX_CONTEXT_MATCH, first, begin, size);

                  restline_data = end;
                  restline_size = eol - end;
                  restline_last = matcher->last();
                }
              }
              else
              {
                if (border > 0)
                {
                  out.str(color_cx);
                  out.str(bol, border);
                  out.str(color_off);
                }

                if (flag_replace != NULL)
                {
                  // output --replace=FORMAT
                  out.str(match_mc);
                  out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
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

                      out.header(pathname, partname, heading, lineno + num, NULL, first + (to - begin) + 1, flag_separator_dash, false);

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

                restline_data = end;
                restline_size = eol - end;
                restline_last = matcher->last();
              }
            }
            else if (flag_before_context > 0)
            {
              binary = flag_hex || (flag_with_hex && is_binary(bol, eol - bol));

              if (binfile || (binary && !flag_hex && !flag_with_hex))
              {
                if (flag_binary_without_match)
                {
                  matches = 0;
                }
                else // if (flag_invert_match) is true
                {
                  last_lineno = current_lineno;
                  next_lineno = current_lineno + lines;
                  lineno = next_lineno - 1;
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
                    out.format(flag_replace, pathname, partname, matches, &matching, matcher, heading, matches > 1, matches > 1);
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

                        out.header(pathname, partname, heading, lineno + num, NULL, first + (to - begin) + 1, flag_separator_dash, false);

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

                  binary = flag_hex || (flag_with_hex && is_binary(end, eol - end));

                  if (hex && !binary)
                    out.dump.done();
                  else if (!hex && binary)
                    out.nl();

                  if (hex != binary && !flag_no_header)
                    out.header(pathname, partname, heading, lineno + lines - 1, matcher, last, flag_separator_dash, binary);

                  hex = binary;

                  restline_data = end;
                  restline_size = eol - end;
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

                binary = flag_hex || (flag_with_hex && is_binary(end, eol - end));

                if (binfile || (binary && !flag_hex && !flag_with_hex))
                {
                  if (flag_binary_without_match)
                  {
                    matches = 0;
                  }
                  else // if (flag_invert_match) is true
                  {
                    last_lineno = current_lineno;
                    next_lineno = current_lineno + lines;
                    lineno = next_lineno - 1;
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
          next_lineno = current_lineno + lines;
          lineno = next_lineno - 1;
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

        // get the remaining context after the last match to output
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

      if (flag_stats != NULL)
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

// search input after lineno to populate a string vector with the matching line and lines after up to max lines. used by the TUI
void Grep::find_text_preview(const char *filename, const char *findpart, size_t from_lineno, size_t max, size_t& lineno, size_t& num, std::vector<std::string>& text)
{
  // no results yet
  lineno = from_lineno;
  num = 0;

  try
  {
    // open (archive or compressed) file
    if (!open_file(filename, findpart))
      return;
  }

  catch (...)
  {
    // this should never happen
    return;
  }

  matcher->input(input);

#if !defined(HAVE_PCRE2) && defined(HAVE_BOOST_REGEX)
  // buffer all input to work around Boost.Regex partial matching bug, but this may throw std::bad_alloc if the file is too large
  if (flag_perl_regexp)
    matcher->buffer();
#endif

  bool eof = true;

  for (size_t i = from_lineno; i > 1; --i)
    if (!matcher->skip('\n'))
      break;

  if (flag_invert_match)
  {
    lineno = matcher->lineno();
    eof = false;
  }
  else
  {
    while (matcher->find())
    {
      const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
      const char *bol = matcher->bol();

      // check CNF AND/OR/NOT matching
      if (matchers != NULL && !flag_files && !cnf_matching(bol, eol))
        continue;

      lineno = matcher->lineno();

      if (text.empty())
        text.emplace_back(bol, eol - bol);
      else
        text[0].assign(bol, eol - bol);

      num = 1;
      eof = !matcher->skip('\n');

      break;
    }
  }

  while (!eof && num < max)
  {
    const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
    const char *bol = matcher->bol();

    if (text.size() <= num)
      text.emplace_back(bol, eol - bol);
    else
      text[num].assign(bol, eol - bol);

    ++num;

    eof = !matcher->skip('\n');
  }

  // close file or -z: loop over next extracted archive parts, when applicable
  while (close_file(filename))
    continue;
}

// extract a part from an archive and send to a stream, used by the TUI
void Grep::extract(const char *filename, const char *findpart, FILE *output)
{
  try
  {
    // open (archive or compressed) file
    if (!open_file(filename, findpart))
      return;
  }

  catch (...)
  {
    // this should never happen
    return;
  }

  // copy input to output
  char buffer[65536];
  while (true)
  {
    size_t len = input.get(buffer, sizeof(buffer));
    if (len == 0 || fwrite(buffer, 1, len, output) < len)
      break;
  }

  // close file or -z: loop over next extracted archive parts, when applicable
  while (close_file(filename))
    continue;
}

// read globs from a file and split them into files or dirs to include or exclude by pushing them onto the vectors
void import_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs, bool gitignore)
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
          if (gitignore)
            dirs.emplace_back(line);
        }
      }
    }
  }
}

// trim white space from either end of the line
void trim(std::string& line)
{
  size_t len = line.length();
  size_t pos;

  for (pos = 0; pos < len && isspace(static_cast<unsigned char>(line.at(pos))); ++pos)
    continue;

  if (pos > 0)
    line.erase(0, pos);

  len -= pos;

  for (pos = len; pos > 0 && isspace(static_cast<unsigned char>(line.at(pos - 1))); --pos)
    continue;

  if (len > pos)
    line.erase(pos, len - pos);
}

// trim path separators from an argv[] argument - important: modifies the argv[] string passed as arg
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
  const char *s = strstr(colors, parameter);

  // check if substring parameter is present in colors
  if (s != NULL)
  {
    s += strlen(parameter);
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
      if (isdigit(static_cast<unsigned char>(*s)))
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
        while (isdigit(static_cast<unsigned char>(*s)) && t - color < COLORLEN - 2)
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
      else if (*s == ',' || *s == ';' || isspace(static_cast<unsigned char>(*s)))
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
    while ((*s == ';' || isdigit(static_cast<unsigned char>(*s))) && t - color < COLORLEN - 2)
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

// get short option argument
const char *getoptarg(int argc, const char **argv, const char *arg, int& i)
{
  if (*++arg == '=')
    ++arg;
  if (*arg != '\0')
    return arg;
  if (++i < argc)
    return argv[i];
  return "";
}

// get required non-empty long option argument after =
const char *getloptarg(int argc, const char **argv, const char *arg, int& i)
{
  if (*arg != '\0')
    return arg;
  if (++i < argc)
    return argv[i];
  return "";
}

// save a string argument parsed from the command line or from a config file
const char *strarg(const char *string)
{
  arg_strings.emplace_back(string);
  return arg_strings.back().c_str();
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
  if (*string == '\0')
  {
    usage(message, string);
    return;
  }
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

// print a diagnostic message
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
        while (*e != '\0' && (*e == '-' || isalpha(static_cast<unsigned char>(*e))))
          ++e;
        std::cerr << std::string(s, e - s) << "'" << std::endl;
      }
    }
  }

  // do not exit when reading a config file
  if (!flag_usage_warnings)
    exit(EXIT_ERROR);

  ++Static::warnings;
}

// print usage/help information and exit
void help(std::ostream& out)
{
  out <<
    "Usage: ugrep [OPTIONS] [PATTERN] [-f FILE] [-e PATTERN] [FILE ...]\n\n\
    -A NUM, --after-context=NUM\n\
            Output NUM lines of trailing context after matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  If -o is\n\
            specified, output the match with context to fit NUM columns after\n\
            the match or shortens the match.  See also options -B, -C and -y.\n\
    -a, --text\n\
            Process a binary file as if it were text.  This is equivalent to\n\
            the --binary-files=text option.  This option might output binary\n\
            garbage to the terminal, which can have problematic consequences if\n\
            the terminal driver interprets some of it as terminal commands.\n\
    --all, -@\n\
            Search all files except hidden: cancel previous file and directory\n\
            search restrictions and cancel --ignore-binary and --ignore-files\n\
            when specified.  Restrictions specified after this option, i.e. to\n\
            the right, are still applied.  For example, -@I searches all\n\
            non-binary files and -@. searches all files including hidden files.\n\
            Note that hidden files and directories are never searched, unless\n\
            option -. or --hidden is specified.\n\
    --and [-e] PATTERN\n\
            Specify additional PATTERN that must match.  Additional -e PATTERN\n\
            following this option is considered an alternative pattern to\n\
            match, i.e. each -e is interpreted as an OR pattern enclosed within\n\
            the AND.  For example, -e A -e B --and -e C -e D matches lines with\n\
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
            the match or shortens the match.  See also options -A, -C and -y.\n\
    -b, --byte-offset\n\
            The offset in bytes of a pattern match is displayed in front of the\n\
            respective matched line.  When -u is specified, displays the offset\n\
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
    --bool, -%, -%%\n\
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
            lines with `A' and also either `AND' or `OR'.  Parentheses are used\n\
            for grouping.  For example, --bool '(A B)|C' matches lines with `A'\n\
            and `B', or lines with `C'.  Note that all subpatterns in a Boolean\n\
            query pattern are regular expressions, unless -F is specified.\n\
            Options -E, -F, -G, -P and -Z can be combined with --bool to match\n\
            subpatterns as strings or regular expressions (-E is the default.)\n\
            This option does not apply to -f FILE patterns.  The double short\n\
            option -%% enables options --bool --files.  Option --stats displays\n\
            the Boolean search patterns applied.  See also options --and,\n\
            --andnot, --not, --files and --lines.\n\
    --break\n\
            Adds a line break between results from different files.  This\n\
            option is enabled by --heading.\n\
    -C NUM, --context=NUM\n\
            Output NUM lines of leading and trailing context surrounding each\n\
            matching line.  Places a --group-separator between contiguous\n\
            groups of matches.  If -o is specified, output the match with\n\
            context to fit NUM columns before and after the match or shortens\n\
            the match.  See also options -A, -B and -y.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            When -o or -u is specified, counts the number of patterns matched.\n\
            When -v is specified, counts the number of non-matching lines.\n\
            When -m1, (with a comma or --min-count=1) is specified, counts only\n\
            matching files without outputting zero matches.\n\
    --color[=WHEN], --colour[=WHEN]\n\
            Mark up the matching text with the colors specified with option\n\
            --colors or the GREP_COLOR or GREP_COLORS environment variable.\n\
            WHEN can be `never', `always', or `auto', where `auto' marks up\n\
            matches only when output on a terminal.  The default is `auto'.\n\
    --colors=COLORS, --colours=COLORS\n\
            Use COLORS to mark up text.  COLORS is a colon-separated list of\n\
            one or more parameters `sl=' (selected line), `cx=' (context line),\n\
            `mt=' (matched text), `ms=' (match selected), `mc=' (match\n\
            context), `fn=' (file name), `ln=' (line number), `cn=' (column\n\
            number), `bn=' (byte offset), `se=' (separator), `qp=' (TUI\n\
            prompt), `qe=' (TUI errors), `qr=' (TUI regex), `qm=' (TUI regex\n\
            meta characters), `ql=' (TUI regex lists and literals), `qb=' (TUI\n\
            regex braces).  Parameter values are ANSI SGR color codes or `k'\n\
            (black), `r' (red), `g' (green), `y' (yellow), `b' (blue), `m'\n\
            (magenta), `c' (cyan), `w' (white), or leave empty for no color.\n\
            Upper case specifies background colors.  A `+' qualifies a color as\n\
            bright.  A foreground and a background color may be combined with\n\
            font properties `n' (normal), `f' (faint), `h' (highlight), `i'\n\
            (invert), `u' (underline).  Parameter `hl' enables file name\n\
            hyperlinks.  Parameter `rv' reverses the `sl=' and `cx=' parameters\n\
            when option -v is specified.  Selectively overrides GREP_COLORS.\n\
            Legacy grep single parameter codes may be specified, for example\n\
            --colors='7;32' or --colors=ig to set ms (match selected).\n\
    --config[=FILE], ---[FILE]\n\
            Use configuration FILE.  The default FILE is `.ugrep'.  The working\n\
            directory is checked first for FILE, then the home directory.  The\n\
            options specified in the configuration FILE are parsed first,\n\
            followed by the remaining options specified on the command line.\n\
            The ug command automatically loads a `.ugrep' configuration file,\n\
            unless --config=FILE or --no-config is specified.\n\
    --no-config\n\
            Do not automatically load the default .ugrep configuration file.\n\
    --no-confirm\n\
            Do not confirm actions in -Q query TUI.  The default is confirm.\n\
    --cpp\n\
            Output file matches in C++.  See also options --format and -u.\n\
    --csv\n\
            Output file matches in CSV.  When -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -D ACTION, --devices=ACTION\n\
            If an input file is a device, FIFO or socket, use ACTION to process\n\
            it.  By default, ACTION is `skip', which means that devices are\n\
            silently skipped.  When ACTION is `read', devices read just as if\n\
            they were ordinary files.\n\
    -d ACTION, --directories=ACTION\n\
            If an input file is a directory, use ACTION to process it.  By\n\
            default, ACTION is `skip', i.e., silently skip directories unless\n\
            specified on the command line.  When ACTION is `read', warn when\n\
            directories are read as input.  When ACTION is `recurse', read all\n\
            files under each directory, recursively, following symbolic links\n\
            only if they are on the command line.  This is equivalent to the -r\n\
            option.  When ACTION is `dereference-recurse', read all files under\n\
            each directory, recursively, following symbolic links.  This is\n\
            equivalent to the -R option.\n\
    --delay=DELAY\n\
            Set the default -Q key response delay.  Default is 3 for 300ms.\n\
    --depth=[MIN,][MAX], -1, -2, -3, ... -9, -10, -11, ...\n\
            Restrict recursive searches from MIN to MAX directory levels deep,\n\
            where -1 (--depth=1) searches the specified path without recursing\n\
            into subdirectories.  The short forms -3 -5, -3-5 and -3,5 search 3\n\
            to 5 levels deep.  Enables -r if -R or -r is not specified.\n\
    --dotall\n\
            Dot `.' in regular expressions matches anything, including newline.\n\
            Note that `.*' matches all input and should not be used.\n\
    -E, --extended-regexp\n\
            Interpret patterns as extended regular expressions (EREs). This is\n\
            the default.\n\
    -e PATTERN, --regexp=PATTERN\n\
            Specify a PATTERN to search the input.  An input line is selected\n\
            if it matches any of the specified patterns.  This option is useful\n\
            when multiple -e options are used to specify multiple patterns, or\n\
            when a pattern begins with a dash (`-'), or to specify a pattern\n\
            after option -f or after the FILE arguments.\n\
    --encoding=ENCODING\n\
            The encoding format of the input.  The default ENCODING is binary\n\
            or UTF-8 which are treated the same.  Therefore, --encoding=binary\n\
            has no effect.  Note that option -U or --binary specifies binary\n\
            PATTERN matching (text matching is the default).  ENCODING can be:\n\
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
            Exclude files whose name matches GLOB, same as -g ^GLOB.  GLOB can\n\
            use **, *, ?, and [...] as wildcards and \\ to quote a wildcard or\n\
            backslash character literally.  When GLOB contains a `/', full\n\
            pathnames are matched.  Otherwise basenames are matched.  When GLOB\n\
            ends with a `/', directories are excluded as if --exclude-dir is\n\
            specified.  Otherwise files are excluded.  Note that --exclude\n\
            patterns take priority over --include patterns.  GLOB should be\n\
            quoted to prevent shell globbing.  This option may be repeated.\n\
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
            Exclude file systems specified by MOUNTS from recursive searches.\n\
            MOUNTS is a comma-separated list of mount points or pathnames to\n\
            directories.  When MOUNTS is not specified, only descends into the\n\
            file systems associated with the specified file and directory\n\
            search targets, i.e. excludes all other file systems.  Note that\n\
            --exclude-fs=MOUNTS take priority over --include-fs=MOUNTS.  This\n\
            option may be repeated.\n"
#if !defined(HAVE_STATVFS) && !defined(HAVE_STATFS)
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
            COMMANDS is a comma-separated list of `exts:command arguments',\n\
            where `exts' is a comma-separated list of filename extensions and\n\
            `command' is a filter utility.  Files matching one of `exts' are\n\
            filtered.  A `*' matches any file.  The specified `command' may\n\
            include arguments separated by spaces.  An argument may be quoted\n\
            to include spacing, commas or a `%'.  A `%' argument expands into\n\
            the pathname to search.  For example, --filter='pdf:pdftotext % -'\n\
            searches PDF files.  The `%' expands into a `-' when searching\n\
            standard input.  When a `%' is not specified, the filter command\n\
            should read from standard input and write to standard output.\n\
            Option --label=.ext may be used to specify extension `ext' when\n\
            searching standard input.  This option may be repeated.\n\
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
            section FORMAT for details.  When option -o is specified, option -u\n\
            is also enabled.  Context options -A, -B, -C and -y are ignored.\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret patterns as basic regular expressions (BREs).\n\
    -g GLOBS, --glob=GLOBS, --iglob=GLOBS\n\
            Only search files whose name matches the specified comma-separated\n\
            list of GLOBS, same as --include=glob for each `glob' in GLOBS.\n\
            When a `glob' is preceded by a `!' or a `^', skip files whose name\n\
            matches `glob', same as --exclude='glob'.  When `glob' contains a\n\
            `/', full pathnames are matched.  Otherwise basenames are matched.\n\
            When `glob' ends with a `/', directories are matched, same as\n\
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'\n\
            matches the working directory.  Option --iglob performs\n\
            case-insensitive name matching.  This option may be repeated and\n\
            may be combined with options -M, -O and -t.  For more details, see\n\
            `ugrep --help globs' and `man ugrep' section GLOBBING for details.\n\
    --glob-ignore-case\n\
            Perform case-insensitive glob matching in general.\n\
    --group-separator[=SEP]\n\
            Use SEP as a group separator for context options -A, -B and -C.\n\
            The default is a double hyphen (`--').\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B and -C.\n\
    -H, --with-filename\n\
            Always print the filename with output lines.  This is the default\n\
            when there is more than one file to search.\n\
    -h, --no-filename\n\
            Never print filenames with output lines.  This is the default\n\
            when there is only one file (or only standard input) to search.\n\
    --heading, -+\n\
            Group matches per file.  Adds a heading and a line break between\n\
            results from different files.  This option is enabled by --pretty\n\
            when the output is sent to a terminal.\n\
    --help [WHAT], -? [WHAT]\n\
            Display a help message on options related to WHAT when specified.\n\
            In addition, `--help regex' displays an overview of regular\n\
            expressions, `--help globs' displays an overview of glob syntax and\n\
            conventions, `--help fuzzy' displays details of fuzzy search, and\n\
            `--help format' displays a list of option --format=FORMAT fields.\n\
    --hexdump[=[1-8][a][bch][A[NUM]][B[NUM]][C[NUM]]]\n\
            Output matches in 1 to 8 columns of 8 hexadecimal octets.  The\n\
            default is 2 columns or 16 octets per line.  Argument `a' outputs a\n\
            `*' for all hex lines that are identical to the previous hex line,\n\
            `b' removes all space breaks, `c' removes the character column, `h'\n\
            removes hex spacing, `A' includes up to NUM hex lines after a\n\
            match, `B' includes up to NUM hex lines before a match and `C'\n\
            includes up to NUM hex lines before and after a match.  Arguments\n\
            `A', `B' and `C' are the same as options -A, -B and -C when used\n\
            with --hexdump.  See also options -U, -W and -X.\n\
    --hidden, -.\n\
            Search "
#ifdef OS_WIN
            "Windows system and "
#endif
            "hidden files and directories\n\
            (enabled by default in grep compatibility mode).\n\
    --hyperlink[=[PREFIX][+]]\n\
            Hyperlinks are enabled for file names when colors are enabled.\n\
            Same as --colors=hl.  When PREFIX is specified, replaces file://\n\
            with PREFIX:// in the hyperlink.  A `+' includes the line number in\n\
            the hyperlink and when option -k is specified, the column number.\n\
    -I, --ignore-binary\n\
            Ignore matches in binary files.  This option is equivalent to the\n\
            --binary-files=without-match option.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching.  By default, ugrep is case\n\
            sensitive.\n\
    --ignore-files[=FILE]\n\
            Ignore files and directories matching the globs in each FILE that\n\
            is encountered in recursive searches.  The default FILE is\n\
            `" DEFAULT_IGNORE_FILE "'.  Matching files and directories located in the\n\
            directory of the FILE and in subdirectories below are ignored.\n\
            Globbing syntax is the same as the --exclude-from=FILE gitignore\n\
            syntax, but files and directories are excluded instead of only\n\
            files.  Directories are specifically excluded when the glob ends in\n\
            a `/'.  Files and directories explicitly specified as command line\n\
            arguments are never ignored.  This option may be repeated to\n\
            specify additional files.\n\
    --no-ignore-files\n\
            Do not ignore files, i.e. cancel --ignore-files when specified.\n\
    --include=GLOB\n\
            Only search files whose name matches GLOB, same as -g GLOB.  GLOB\n\
            can use **, *, ?, and [...] as wildcards and \\ to quote a wildcard\n\
            or backslash character literally.  When GLOB contains a `/', full\n\
            pathnames are matched.  Otherwise basenames are matched.  When GLOB\n\
            ends with a `/', directories are included as if --include-dir is\n\
            specified.  Otherwise files are included.  Note that --exclude\n\
            patterns take priority over --include patterns.  GLOB should be\n\
            quoted to prevent shell globbing.  This option may be repeated.\n\
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
            pathnames to directories.  When MOUNTS is not specified, restricts\n\
            recursive searches to the file system of the working directory,\n\
            same as --include-fs=. (dot). Note that --exclude-fs=MOUNTS take\n\
            priority over --include-fs=MOUNTS.  This option may be repeated.\n"
#if !defined(HAVE_STATVFS) && !defined(HAVE_STATFS)
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
            "\
    --index\n\
            Perform fast index-based recursive search.  This option assumes,\n\
            but does not require, that files are indexed with ugrep-indexer.\n\
            This option also enables option -r or --recursive.  Skips indexed\n\
            non-matching files, archives and compressed files.  Significant\n\
            acceleration may be achieved on cold (not file-cached) and large\n\
            file systems, or any file system that is slow to search.  Note that\n\
            the start-up time to search may be increased when complex search\n\
            patterns are specified that contain large Unicode character classes\n\
            combined with `*' or `+' repeats, which should be avoided.  Option\n\
            -U or --ascii improves performance.  Option --stats displays an\n\
            index search report.\n\
    -J NUM, --jobs=NUM\n\
            Specifies the number of threads spawned to search files.  By\n\
            default an optimum number of threads is spawned to search files\n\
            simultaneously.  -J1 disables threading: files are searched in the\n\
            same order as specified.\n\
    -j, --smart-case\n\
            Perform case insensitive matching, unless a pattern is specified\n\
            with a literal upper case letter.\n\
    --json\n\
            Output file matches in JSON.  When -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -K [MIN,][MAX], --range=[MIN,][MAX], --min-line=MIN, --max-line=MAX\n\
            Start searching at line MIN, stop at line MAX when specified.\n\
    -k, --column-number\n\
            The column number of a pattern match is displayed in front of the\n\
            respective matched line, starting at column 1.  Tabs are expanded\n\
            in counting columns, see also option --tabs.\n\
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
            Boolean line matching mode for option --bool, the default mode.\n\
    -M MAGIC, --file-magic=MAGIC\n\
            Only search files matching the magic signature pattern MAGIC.  The\n\
            signature \"magic bytes\" at the start of a file are compared to\n\
            the MAGIC regex pattern.  When matching, the file will be searched.\n\
            When MAGIC is preceded by a `!' or a `^', skip files with matching\n\
            MAGIC signatures.  This option may be repeated and may be combined\n\
            with options -O and -t.  Every file on the search path is read,\n\
            making recursive searches potentially more expensive.\n\
    -m [MIN,][MAX], --min-count=MIN, --max-count=MAX\n\
            Require MIN matches, stop after MAX matches when specified.  Output\n\
            MIN to MAX matches.  For example, -m1 outputs the first match and\n\
            -cm1, (with a comma) counts nonzero matches.  When -u or --ungroup\n\
            is specified, each individual match counts.  See also option -K.\n\
    --match\n\
            Match all input.  Same as specifying an empty pattern to search.\n\
    --max-files=NUM\n\
            Restrict the number of files matched to NUM.  Note that --sort or\n\
            -J1 may be specified to produce replicable results.  If --sort is\n\
            specified, then the number of threads spawned is limited to NUM.\n\
    --mmap[=MAX]\n\
            Use memory maps to search files.  By default, memory maps are used\n\
            under certain conditions to improve performance.  When MAX is\n\
            specified, use up to MAX mmap memory per thread.\n\
    -N PATTERN, --neg-regexp=PATTERN\n\
            Specify a negative PATTERN to reject specific -e PATTERN matches\n\
            with a counter pattern.  Note that longer patterns take precedence\n\
            over shorter patterns, i.e. a negative pattern must be of the same\n\
            length or longer to reject matching patterns.  Option -N cannot be\n\
            specified with -P.  This option may be repeated.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --not [-e] PATTERN\n\
            Specifies that PATTERN should not match.  Note that -e A --not -e B\n\
            matches lines with `A' or lines without a `B'.  To match lines with\n\
            `A' that have no `B', specify -e A --andnot -e B.  Option --stats\n\
            displays the search patterns applied.  See also options --and,\n\
            --andnot, --bool, --files and --lines.\n\
    --null, -0";
  out << (flag_grep ? ", -Z" : "") << "\n\
            Output a zero byte after the file name.  This option can be used\n\
            with commands such as `find -print0' and `xargs -0' to process\n\
            arbitrary file names, even those that contain newlines.  See also\n\
            options -H or --with-filename and --null-data.\n\
    --null-data, -00";
  out << (flag_grep ? ", -z" : "") << "\n\
            Input and output are treated as sequences of lines with each line\n\
            terminated by a zero byte instead of a newline; effectively swaps\n\
            NUL with LF in the input and the output.  When combined with option\n\
            --encoding=ENCODING, output each line terminated by a zero byte\n\
            without affecting the input specified as per ENCODING.  Instead of\n\
            option --null-data, option --encoding=null-data treats the input as\n\
            a sequence of lines terminated by a zero byte without affecting the\n\
            output.  Option --null-data is not compatible with UTF-16/32 input.\n\
            See also options --encoding and --null.\n\
    -O EXTENSIONS, --file-extension=EXTENSIONS\n\
            Only search files whose filename extensions match the specified\n\
            comma-separated list of EXTENSIONS, same as -g '*.ext' for each\n\
            `ext' in EXTENSIONS.  When an `ext' is preceded by a `!' or a `^',\n\
            skip files whose filename extensions matches `ext', same as\n\
            -g '^*.ext'.  This option may be repeated and may be combined with\n\
            options -g, -M and -t.\n\
    -o, --only-matching\n\
            Only the matching part of a pattern match is output.  When -A, -B\n\
            or -C is specified, fits the match and its context on a line within\n\
            the specified number of columns.\n\
    --only-line-number\n\
            Only the line number of a matching line is output.  The line number\n\
            counter is reset for each file processed.\n\
    --files, -%%\n\
            Boolean file matching mode, the opposite of --lines.  When combined\n\
            with option --bool, matches a file if all Boolean conditions are\n\
            satisfied.  For example, --bool --files 'A B|C -D' matches a file\n\
            if some lines match `A', and some lines match either `B' or `C',\n\
            and no line matches `D'.  See also options --and, --andnot, --not,\n\
            --bool and --lines.  The double short option -%% enables options\n\
            --bool --files.\n\
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
            If -R or -r is specified, do not follow symbolic links, even when\n\
            symbolic links are specified on the command line.\n\
    --pager[=COMMAND]\n\
            When output is sent to the terminal, uses COMMAND to page through\n\
            the output.  COMMAND defaults to environment variable PAGER when\n\
            defined or `" DEFAULT_PAGER_COMMAND "'.  Enables --heading and --line-buffered.\n\
    --pretty[=WHEN]\n\
            When output is sent to a terminal, enables --color, --heading, -n,\n\
            --sort, --tree and -T when not explicitly disabled.  WHEN can be\n\
            `never', `always', or `auto'.  The default is `auto'.\n\
    -Q[=DELAY], --query[=DELAY]\n\
            Query mode: start a TUI to perform interactive searches.  This mode\n\
            requires an ANSI capable terminal.  An optional DELAY argument may\n\
            be specified to reduce or increase the response time to execute\n\
            searches after the last key press, in increments of 100ms, where\n\
            the default is 3 (300ms delay).  No whitespace may be given between\n\
            -Q and its argument DELAY.  Initial patterns may be specified with\n\
            -e PATTERN, i.e. a PATTERN argument requires option -e.  Press F1\n\
            or CTRL-Z to view the help screen.  Press F2 or CTRL-Y to invoke a\n\
            command to view or edit the file shown at the top of the screen.\n\
            The command can be specified with option --view and defaults to\n\
            environment variable PAGER when defined, or VISUAL or EDITOR.\n\
            Press TAB or SHIFT-TAB to navigate directories and to select a file\n\
            to search.  Press ENTER to select lines to output.  Press ALT-l for\n\
            option -l to list files, ALT-n for -n, etc.  Non-option commands\n\
            include ALT-] to increase context and ALT-} to increase fuzzyness.\n\
            If ALT or OPTION keys are not available, then press CTRL-O + KEY to\n\
            switch option `KEY', or press F1 or CTRL-Z for help and press KEY.\n\
            See also options --no-confirm, --delay, --split and --view.\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress all output.  Only search a file until a match\n\
            has been found.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links to files and directories, unlike -r.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are on the command line.  Note that when no FILE\n\
            arguments are specified and input is read from a terminal,\n\
            recursive searches are performed as if -r is specified.\n\
    --replace=FORMAT\n\
            Replace matching patterns in the output by FORMAT with `%' fields.\n\
            If -P is specified, FORMAT may include `%1' to `%9', `%[NUM]#' and\n\
            `%[NAME]#' to output group captures.  A `%%' outputs `%' and `%~'\n\
            outputs a newline.  See also option --format, `ugrep --help format'\n\
            and `man ugrep' section FORMAT for details.\n\
    -S, --dereference-files\n\
            When -r is specified, follow symbolic links to files, but not to\n\
            directories.  The default is not to follow symbolic links.\n\
    -s, --no-messages\n\
            Silent mode: nonexistent and unreadable files are ignored and\n\
            their error messages and warnings are suppressed.\n\
    --save-config[=FILE] [OPTIONS]\n\
            Save configuration FILE to include OPTIONS.  Update FILE when\n\
            first loaded with --config=FILE.  The default FILE is `.ugrep',\n\
            which is automatically loaded by the ug command.  When FILE is a\n\
            `-', writes the configuration to standard output.  Only part of the\n\
            OPTIONS are saved that do not cause searches to fail when combined\n\
            with other options.  Additional options may be specified by editing\n\
            the saved configuration file.  A configuration file may be modified\n\
            manually to specify one or more config[=FILE] to indirectly load\n\
            the specified FILE, but recursive config loading is not allowed.\n\
    --separator[=SEP], --context-separator=SEP\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset and the matched line.  The default separator is\n\
            a colon (`:') and a bar (`|') for multi-line pattern matches, and a\n\
            dash (`-') for context lines.  See also option --group-separator.\n\
    --split\n\
            Split the -Q query TUI screen on startup.\n\
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
            the specified type.  Specifying the initial part of a type name\n\
            suffices when the choice is unambiguous.  This option may be\n\
            repeated.  The possible file types can be (-tlist displays a list):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    out << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  out << ".\n\
    --tabs[=NUM]\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1 (no expansion), 2, 4, or 8.  The default size is 8.\n\
    --tag[=TAG[,END]]\n\
            Disables colors to mark up matches with TAG.  END marks the end of\n\
            a match if specified, otherwise TAG.  The default is `___'.\n\
    --tree, -^\n\
            Output directories with matching files in a tree-like format for\n\
            option -c or --count, -l or --files-with-matches, -L or\n\
            --files-without-match.  This option is enabled by --pretty when the\n\
            output is sent to a terminal.\n\
    -U, --ascii, --binary\n\
            Disables Unicode matching for ASCII and binary matching.  PATTERN\n\
            matches bytes, not Unicode characters.  For example, -U '\\xa3'\n\
            matches byte A3 (hex) instead of the Unicode code point U+00A3\n\
            represented by the UTF-8 sequence C2 A3.  See also option --dotall.\n\
    -u, --ungroup\n\
            Do not group multiple pattern matches on the same matched line.\n\
            Output the matched line again for each additional pattern match.\n\
    -V, --version\n\
            Display version with linked libraries and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    --view[=COMMAND]\n\
            Use COMMAND to view/edit a file in -Q query TUI by pressing CTRL-Y.\n\
    -W, --with-hex\n\
            Output binary matches in hexadecimal, leaving text matches alone.\n\
            This option is equivalent to the --binary-files=with-hex option.\n\
            To omit the matching line from the hex output, use both options -W\n\
            and --hexdump.  See also options -U.\n\
    -w, --word-regexp\n\
            The PATTERN is searched for as a word, such that the matching text\n\
            is preceded by a non-word character and is followed by a non-word\n\
            character.  Word-like characters are Unicode letters, digits and\n\
            connector punctuations such as underscore.\n\
    --width[=NUM]\n\
            Truncate the output to NUM visible characters per line.  The width\n\
            of the terminal window is used if NUM is not specified.  Note that\n\
            double-width characters in the output may result in wider lines.\n\
    -X, --hex\n\
            Output matches and matching lines in hexadecimal.  This option is\n\
            equivalent to the --binary-files=hex option.  To omit the matching\n\
            line from the hex output use option --hexdump.  See also option -U.\n\
    -x, --line-regexp\n\
            Select only those matches that exactly match the whole line, as if\n\
            the patterns are surrounded by ^ and $.\n\
    --xml\n\
            Output file matches in XML.  When -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -Y, --empty\n\
            Empty-matching patterns match all lines.  Normally, empty matches\n\
            are not output, unless a pattern begins with `^' or ends with `$'.\n\
            With this option, empty-matching patterns, such as x? and x*, match\n\
            all lines, not only lines with an `x' (enabled by default in grep\n\
            compatibility mode).\n\
    -y, --any-line, --passthru\n\
            Any line is output (passthru).  Non-matching lines are output as\n\
            context with a `-' separator.  See also options -A, -B and -C.\n\
    ";
  if (!flag_grep)
    out << "-Z[best][+-~][MAX], ";
  out << "--fuzzy[=[best][+-~][MAX]]\n\
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
    ";
  if (!flag_grep)
    out << "-z, ";
  out << "--decompress\n\
            Search compressed files and archives.  Archives (.cpio, .pax, .tar)\n\
            and compressed archives (e.g. .zip, .7z, .taz, .tgz, .tpz, .tbz,\n\
            .tbz2, .tb2, .tz2, .tlz, .txz, .tzst) are searched and matching\n\
            pathnames of files in archives are output in braces.  When used\n\
            with option --zmax=NUM, searches the contents of compressed files\n\
            and archives stored within archives up to NUM levels.  When -g, -O,\n\
            -M, or -t is specified, searches archives for files that match the\n\
            specified globs, file extensions, file signature magic bytes, or\n\
            file types, respectively; a side-effect of these options is that\n\
            the compressed files and archives searched are only those with\n\
            filename extensions that match known compression and archive types.\n"
#ifndef HAVE_LIBZ
            "\
            This option is not available in this build configuration of ugrep.\n"
#else
            "\
            Supported compression formats: gzip (.gz), compress (.Z), zip"
#ifndef WITH_NO_7ZIP
            ", 7z"
#endif
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
#ifdef HAVE_LIBBROTLI
            ",\n\
            brotli (requires suffix .br)"
#endif
#ifdef HAVE_LIBBZIP3
            ",\n\
            bzip3 (requires suffix .bz3)"
#endif
            ".\n"
#endif
            "\
    --zmax=NUM\n\
            When used with option -z or --decompress, searches the contents of\n\
            compressed files and archives stored within archives by up to NUM\n\
            expansion stages.  The default --zmax=1 only permits searching\n\
            uncompressed files stored in cpio, pax, tar, zip and 7z archives;\n\
            compressed files and archives are detected as binary files and are\n\
            effectively ignored.  Specify --zmax=2 to search compressed files\n\
            and archives stored in cpio, pax, tar, zip and 7z archives.  NUM\n\
            may range from 1 to 99 for up to 99 decompression and de-archiving\n\
            steps.  Increasing NUM values gradually degrades performance.\n"
#ifndef WITH_DECOMPRESSION_THREAD
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
            ;
  if (flag_grep)
    out << "\nGrep compatibility mode: -Z and -z reassigned to --null and --null-data.\n";
  out << "\n\
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

// print a helpful information for WHAT, if specified, and exit
void help(const char *what)
{
  // strip = from =WHAT
  if (what != NULL && *what == '=')
    ++what;

  // strip --no from --no-WHAT
  if (what != NULL && strncmp(what, "--no", 4) == 0)
    what += 4;

  // strip one dash from --WHAT
  if (what != NULL && strncmp(what, "--", 2) == 0)
    ++what;

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

          if (what[j] == '\0' || what[j] == '=' || isspace(static_cast<unsigned char>(what[j])))
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
    else
      std::cout << "\n\nLong options may start with `--no-' to disable, when applicable.";

    std::cout << "\n\n";

    if (strcmp(what, "format") == 0)
    {
      std::cout <<
"FORMAT fields for --format and --replace:\n\
\n\
 field       output                      field       output\n\
 ----------  --------------------------  ----------  --------------------------\n\
 %%          %                           %[...]<     text ... if %m = 1\n\
 %~          newline (LF or CRLF)        %[...]>     text ... if %m > 1\n\
 %a          basename of matching file   %,          , if %m > 1, same as %[,]>\n\
 %A          byte range in hex of match  %:          : if %m > 1, same as %[:]>\n\
 %b          byte offset of a match      %;          ; if %m > 1, same as %[;]>\n\
 %B %[...]B  ... + byte offset, if -b    %|          | if %m > 1, same as %[|]>\n\
 %c          matching pattern as C/C++   %[...]$     assign ... to separator\n\
 %C          matching line as C/C++      %$          reset to default separator\n\
 %d          byte size of a match        %[ms]=...%= color of ms ... color off\n\
 %e          end offset of a match       --------------------------------------\n\
 %f          pathname of matching file   \n\
 %F %[...]F  ... + pathname, if -H       \n\
 %+          %F as heading/break, if -+  Fields that require -P for captures:\n\
 %h          quoted \"pathname\"           \n\
 %H %[...]H  ... + \"pathname\", if -H     field       output\n\
 %i          pathname as XML             ----------  --------------------------\n\
 %I %[...]I  ... + pathname XML, if -H   %1 %2...%9  group capture\n\
 %j          matching pattern as JSON    %[n]#       nth group capture\n\
 %J          matching line as JSON       %[n]b       nth capture byte offset\n\
 %k          column number of a match    %[n]d       nth capture byte size\n\
 %K %[...]K  ... + column number, if -k  %[n]e       nth capture end offset\n\
 %l          last line number of match   %[n]j       nth capture as JSON\n\
 %L          number of lines of a match  %[n]q       nth capture quoted\n\
 %m          number of matches           %[n]v       nth capture as CSV\n\
 %M          number of matching lines    %[n]x       nth capture as XML\n\
 %n          line number of a match      %[n]y       nth capture as hex\n\
 %N %[...]N  ... + line number, if -n    %[name]#    named group capture\n\
 %o          matching pattern, also %0   %[name]b    named capture byte offset\n\
 %O          matching line               %[name]d    named capture byte size\n\
 %p          path to matching file       %[name]e    named capture end offset\n\
 %q          quoted matching pattern     %[name]j    named capture as JSON\n\
 %Q          quoted matching line        %[name]q    named capture quoted\n\
 %R          newline, if --break         %[name]v    named capture as CSV\n\
 %s          separator (: by default)    %[name]x    named capture as XML\n\
 %S %[...]S  ... + separator, if %m > 1  %[name]y    named capture as hex\n\
 %t          tab                         %[n|...]#   capture n,... that matched\n\
 %T %[...]T  ... + tab, if -T            %[n|...]b   capture n,... byte offset\n\
 %u          unique lines, unless -u     %[n|...]d   capture n,... byte size\n\
 %[hhhh]U    U+hhhh Unicode code point   %[n|...]e   capture n,... end offset\n\
 %v          matching pattern as CSV     %[n|...]j   capture n,... as JSON\n\
 %V          matching line as CSV        %[n|...]q   capture n,... quoted\n\
 %w          match width in wide chars   %[n|...]v   capture n,... as CSV\n\
 %x          matching pattern as XML     %[n|...]x   capture n,... as XML\n\
 %X          matching line as XML        %[n|...]y   capture n,... as hex\n\
 %y          matching pattern as hex     %g          capture number or name\n\
 %Y          matching line as hex        %G          all capture numbers/names\n\
 %z          path in archive             %[t|...]g   text t indexed by capture\n\
 %Z          edit distance cost, if -Z   %[t|...]G   all t indexed by captures\n\
 --------------------------------------  --------------------------------------\n\
\n\
Options -X and -W change the %o and %O fields to output hex and hex/text.\n\
\n\
Option -o changes the %O and %Q fields to output the match only.\n\
\n\
Options -c, -l and -o change the output of %C, %J, %V, %X and %Y accordingly.\n\
\n\
Numeric fields such as %n are padded with spaces when %{width}n is specified.\n\
\n\
Matching line fields such as %O are cut to width when %{width}O is specified or\n\
when %{-width}O is specified to cut from the end of the line.\n\
\n\
Character context on a matching line before or after a match is output when\n\
%{-width}o or %{+width}o is specified for match fields such as %o, where\n\
%{width}o without a +/- sign cuts the match to the specified width.\n\
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
 a(b|cd?)    ab or ac or acd             \\xhh        hex character code hh\n\
 --------------------------------------  \\x{hhhh}    Unicode code point U+hhhh\n\
                                         \\u{hhhh}    Unicode code point U+hhhh\n\
 pattern     character classes           --------------------------------------\n\
 ----------  --------------------------  \n\
 [abc-e]     one character a,b,c,d,e     pattern     anchors and boundaries\n\
 [^abc-e]    one char not a,b,c,d,e,\\n   ----------  --------------------------\n\
 [[:name:]]  one char in POSIX class:    ^           begin of line anchor\n\
    alnum      a-z,A-Z,0-9               $           end of line anchor\n\
    alpha      a-z,A-Z                   \\A          begin of file anchor\n\
    ascii      ASCII char \\x00-\\x7f      \\Z          end of file anchor\n\
    blank      space or tab              \\b          word boundary\n\
    cntrl      control characters        \\B          non-word boundary\n\
    digit      0-9                       \\<          start of word boundary\n\
    graph      visible characters        \\>          end of word boundary\n\
    lower      a-z                       (?=...)     lookahead (-P)\n\
    print      visible chars and space   (?!...)     negative lookahead (-P)\n\
    punct      punctuation characters    (?<=...)    lookbehind (-P)\n\
    space      space,\\t,\\v,\\f,\\r         (?<!...)    negative lookbehind (-P)\n\
    upper      A-Z                       --------------------------------------\n\
     word      a-z,A-Z,0-9,_             (-P): pattern requires option -P\n\
   xdigit      0-9,a-f,A-F               \n\
 \\p{class}   one character in class      pattern     grouping\n\
 \\P{class}   one char not in class       ----------  --------------------------\n\
 \\d          a digit                     (...)       non-capturing group\n\
 \\D          a non-digit                 (...)       capturing group (-P)\n\
 \\h          a space or tab              (?:...)     non-capturing group (-P)\n\
 \\H          not a space or tab          (?<X>...)   capturing, named X (-P)\n\
 \\s          a whitespace except \\n      \\1          matches group 1 (-P)\n\
 \\S          a non-whitespace            \\g{10}      matches group 10 (-P)\n\
 \\w          a word character            \\g{X}       matches group name X (-P)\n\
 \\W          a non-word character        (?#...)     comments ... are ignored\n\
 --------------------------------------  --------------------------------------\n\
                                         (-P): pattern requires option -P\n\
\n\
Option -P enables Perl regex matching with Unicode patterns, lookarounds and\n\
capturing groups.\n\
\n\
Option -U disables full Unicode pattern matching: non-POSIX Unicode character\n\
classes \\p{class} are disabled, ASCII, LATIN1 and binary regex patterns only.\n\
\n\
To match multiple lines, specify a pattern that matches line breaks, such as\n\
\\n or \\R.  Character classes do not match a newline character, except for\n\
\\P, \\D, \\H, \\W and \\X and when a \\n or a \\R is explicitly specified,\n\
such as [\\s\\n].\n\
\n\
Options -% (--bool --lines) and -%% (--bool --files) augments pattern syntax:\n\
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
Spacing of the logical AND, OR and NOT operators is required.\n\
\n\
Nested parentheses that are part of a regex sub-pattern do not group logical\n\
operators.  For example, `((x y)z)' matches `x yz'.  Note that `((x y){3} z)'\n\
matches `x y' three times and a `z', since the outer parentheses group the AND\n\
(a space).\n\
\n\
Option -% matches lines satisfying the Boolean query.  The double short option\n\
-%% matches files satisfying the Boolean query.\n\
\n\
See also options --and, --andnot, --not to specify sub-patterns as command-line\n\
arguments with options as logical operators.\n\
\n\
Option --stats displays the options and patterns applied to the matching files.\n\
\n\
";
    }
    else if (strcmp(what, "glob") == 0 || strcmp(what, "globs") == 0 || strcmp(what, "globbing") == 0)
    {
      std::cout <<
"Glob syntax and conventions:\n\
\n\
Gitignore-style globbing is performed by all glob-related options: -g (--glob),\n\
--iglob, --include, --exclude, --include-dir, --exclude-dir, --include-from,\n\
--exclude-from, and --ignore-files.\n\
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
A glob pattern starting with a ^ or a ! inverts the matching.  Instead of\n\
matching a filename or directory name, the directory or file is ignored and\n\
excluded from the search.\n\
\n\
When a glob pattern contains a /, the full pathname is matched.  Otherwise, the\n\
basename of a file or directory is matched in recursive searches.\n\
\n\
When a glob pattern begins with a /, files and directories are matched at the\n\
working directory, not recursively.\n\
\n\
When a glob pattern ends with a /, only directories are matched, not files.\n\
\n\
Option -g (--glob) performs glob matching with the specified glob pattern or\n\
a set of comma-separated globs.  When a glob is preceded with a ^ or !, the\n\
glob match is inverted by excluding matching files or directories.\n\
\n\
Option --iglob performs case-insensitive glob matching with the specified glob\n\
pattern or a set of comma-separated globs.  When a glob is preceded with a ^\n\
or !, the glob match is inverted by excluding matching files or directories.\n\
\n\
Option --glob-ignore-case performs case-insensitive glob matching in general,\n\
except with the globs specified in --ignore-files, such as .gitignore.\n\
\n\
Option --ignore-files specifies a file with gitignore-style globs, where the\n\
default file is .gitignore.  When one ore more ignore files are encountered in\n\
recursive searches, the search is narrowed accordingly by excluding files and\n\
directories matching the globs.\n\
\n\
RELATED:\n\
\n\
Option -O (--file-extension) matches filename extensions, or ignores extensions\n\
when preceded with a ^ or !.\n\
\n\
Option -t (--file-type) matches file types, or ignores file types when\n\
preceded with a ^ or a !.  Use -tlist to view the list of supported file types\n\
with corresponding glob patterns.\n\
\n\
Option --stats displays the search path globs applied to the matching files.\n\
\n\
IMPORTANT:\n\
\n\
Always quote glob patterns to prevent the shell from expanding the globs.  For\n\
example, specify -g \"*foo.*\" or -g\"*foo.*\" or \"-g*foo.*\".\n\
\n\
";
    }
    else if (strcmp(what, "fuzzy") == 0)
    {
      std::cout <<
"Fuzzy (approximate) search is performed with option -Z:\n\
\n\
 -Z    fuzzy match\n\
 ----  ---------------------------------------------------------------------\n\
       allow 1 character insertion, deletion or substitution (-Z default)\n\
 2     allow 2 character insertions, deletions and/or substitutions\n\
 +1    allow 1 character insertion, e.g. pattern 'foo' also matches 'fomo'\n\
 -1    allow 1 character deletion, e.g. pattern 'food' also matches 'fod'\n\
 ~1    allow 1 character substitution, e.g. pattern 'foo' also matches 'for'\n\
 +-1   allow 1 character insertion or deletion\n\
 +~1   allow 1 character insertion or substitution\n\
 -~1   allow 1 character deletion or substitution\n\
 best  when prefixed to the above, output the best matches only\n\
\n\
No whitespace may be given between -Z and its argument.\n\
\n\
Insertions, deletions and/or substitutions are applied to Unicode characters,\n\
except when option -U is specified for ASCII/binary pattern search.\n\
\n\
The 'best' prefix outputs the best matching lines with the lowest cost (minimal\n\
edit distance) per file.  For example, if a file has an exact match anywhere,\n\
then only exact matches are output, no approximate matches.  This requires two\n\
passes over an input file to determine the minimal edit distance, which reduces\n\
performance significantly.  This option cannot be used with standard input or\n\
with Boolean queries.\n\
\n\
RELATED:\n\
\n\
Option --sort=best sorts the directory output by best fuzzy match based on edit\n\
distance, i.e. files with at least one exact match are output first, followed\n\
by files with at least a match that is one edit distance from exact, and so on.\n\
This sort option requires two passes over each input file, which reduces\n\
performance significantly.\n\
\n\
IMPORTANT:\n\
\n\
Fuzzy search anchors each pattern match at the beginning character of the\n\
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

// print the version info
void version()
{
#if defined(HAVE_PCRE2)
  uint32_t tmp = 0;
#endif
  std::cout << "ugrep " UGREP_VERSION;
  if (flag_grep)
    std::cout << " (" << (flag_basic_regexp ? "" : flag_fixed_strings ? "f" : "e") << "grep compat)";
  std::cout << " " PLATFORM <<
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
    (pcre2_config(PCRE2_CONFIG_JIT, &tmp) >= 0 && tmp != 0 ? "; -P:pcre2jit" : "; -P:pcre2") <<
#elif defined(HAVE_BOOST_REGEX)
    "; -P:boost_regex" <<
#endif
#ifdef HAVE_LIBZ
    "; -z:zlib" <<
#ifdef HAVE_LIBBZ2
    ",bzip2" <<
#endif
#ifdef HAVE_LIBLZMA
    ",lzma" <<
#endif
#ifdef HAVE_LIBLZ4
    ",lz4" <<
#endif
#ifdef HAVE_LIBZSTD
    ",zstd" <<
#endif
#ifdef HAVE_LIBBROTLI
    ",brotli" <<
#endif
#ifdef HAVE_LIBBZIP3
    ",bzip3" <<
#endif
#ifndef WITH_NO_7ZIP
    ",7z" <<
#endif
    ",tar/pax/cpio/zip" <<
#endif
    "\n"
    "License: BSD-3-Clause; ugrep user manual: <https://ugrep.com>\n"
    "Written by Robert van Engelen and others: <https://github.com/Genivia/ugrep>\n"
    "Ugrep utilizes the RE/flex regex library: <https://github.com/Genivia/RE-flex>" << std::endl;
  exit(EXIT_OK);
}

// print to standard error: ... is a directory if -s and -q not specified
void is_directory(const char *pathname)
{
  if (!flag_no_messages)
    fprintf(Static::errout, "%sugrep: %s%s%s is a directory\n", color_off, color_high, pathname, color_off);
}

#ifdef HAVE_LIBZ
// print to standard error: cannot decompress message if -s and -q not specified
void cannot_decompress(const char *pathname, const char *message)
{
  if (!flag_no_messages)
  {
    fprintf(Static::errout, "%sugrep: cannot decompress %s%s%s: %s\n", color_off, color_fn, pathname, color_off, message != NULL ? message : "");
  }
}
#endif

// print to standard error: warning message if -s and -q not specified, display error if errno is set, like perror()
void warning(const char *message, const char *arg)
{
  if (!flag_no_messages)
  {
    const char *errmsg = NULL;
    if (errno)
    {
      // use safe strerror_s() instead of strerror() when available
#if defined(__STDC_LIB_EXT1__) || defined(OS_WIN)
      char errbuf[256];
      strerror_s(errbuf, sizeof(errbuf), errno);
      errmsg = errbuf;
#else
      errmsg = strerror(errno);
#endif
    }
    fprintf(Static::errout, "%sugrep: %swarning:%s %s%s%s%s%c%s %s%s%s\n", color_off, color_warning, color_off, color_high, message != NULL ? message : "", message != NULL ? " " : "", arg != NULL ? arg : "", errmsg != NULL ? ':' : ' ', color_off, color_message, errmsg != NULL ? errmsg : "", color_off);
  }
  ++Static::warnings;
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
  fprintf(Static::errout, "%sugrep: %serror:%s %s%s%s%s:%s %s%s%s\n\n", color_off, color_error, color_off, color_high, message != NULL ? message : "", message != NULL ? " " : "", arg != NULL ? arg : "", color_off, color_message, errmsg, color_off);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message)
{
  fprintf(Static::errout, "%sugrep: %s%s%s\n\n", color_off, color_error, message, color_off);
  exit(EXIT_ERROR);
}

// print to standard error: abort message with exception details, then exit
void abort(const char *message, const std::string& what)
{
  fprintf(Static::errout, "%sugrep: %s%s%s%s%s%s\n\n", color_off, color_error, message != NULL ? message : "", color_off, color_high, what.c_str(), color_off);
  exit(EXIT_ERROR);
}
