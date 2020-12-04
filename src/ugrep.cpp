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
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
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

// ugrep version
#define UGREP_VERSION "3.0.6"

// disable mmap because mmap is almost always slower than the file reading speed improvements since 3.0.0
#define WITH_NO_MMAP

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

#include <stringapiset.h>

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

// use a task-parallel thread to decompress the stream into a pipe to search, handles archives and increases decompression speed for larger files
#define WITH_DECOMPRESSION_THREAD

// drain stdin until eof - this is disabled for speed, almost all utilities handle SIGPIPE these days anyway
// #define WITH_STDIN_DRAIN

// the default GREP_COLORS
#ifndef DEFAULT_GREP_COLORS
# ifdef OS_WIN
#  define DEFAULT_GREP_COLORS "sl=1;37:cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36"
# else
#  define DEFAULT_GREP_COLORS "cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36"
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
#  define DEFAULT_PAGER_COMMAND "less -R"
# endif
#endif

// the default ignore file
#ifndef DEFAULT_IGNORE_FILE
# define DEFAULT_IGNORE_FILE ".gitignore"
#endif

// color is disabled by default, unless enabled with WITH_COLOR
#ifdef WITH_COLOR
# define DEFAULT_COLOR "auto"
#else
# define DEFAULT_COLOR NULL
#endif

// pager is disabled by default, unless enabled with WITH_PAGER
#ifdef WITH_PAGER
# define DEFAULT_PAGER DEFAULT_PAGER_COMMAND
#else
# define DEFAULT_PAGER NULL
#endif

// enable easy-to-use abbreviated ANSI SGR color codes with WITH_EASY_GREP_COLORS
// semicolons are not required and abbreviations can be mixed with numeric ANSI SGR codes
// foreground colors: k=black, r=red, g=green, y=yellow b=blue, m=magenta, c=cyan, w=white
// background colors: K=black, R=red, G=green, Y=yellow B=blue, M=magenta, C=cyan, W=white
// bright colors: +k, +r, +g, +y, +b, +m, +c, +w, +K, +R, +G, +Y, +B, +M, +C, +W
// modifiers: h=highlight, u=underline, i=invert, f=faint, n=normal, H=highlight off, U=underline off, I=invert off
#define WITH_EASY_GREP_COLORS

// ugrep exit codes
#define EXIT_OK    0 // One or more lines were selected
#define EXIT_FAIL  1 // No lines were selected
#define EXIT_ERROR 2 // An error occurred

// limit the total number of threads spawn (i.e. limit spawn overhead), because grepping is practically IO bound
#ifndef MAX_JOBS
# define MAX_JOBS 16U
#endif

// a hard limit on the recursive search depth
#ifndef MAX_DEPTH
# define MAX_DEPTH 100
#endif

// --min-steal default, the minimum co-worker's queue size of pending jobs to steal a job from, smaller values result in higher job stealing rates, should not be less than 3
#ifndef MIN_STEAL
# define MIN_STEAL 3U
#endif

// default --mmap
#define DEFAULT_MIN_MMAP_SIZE MIN_MMAP_SIZE

// default --max-mmap: mmap is disabled by default with WITH_NO_MMAP
#ifdef WITH_NO_MMAP
# define DEFAULT_MAX_MMAP_SIZE 0
#else
# define DEFAULT_MAX_MMAP_SIZE MAX_MMAP_SIZE
#endif

// pretty is disabled by default, unless enabled with WITH_PRETTY
#ifdef WITH_PRETTY
# define DEFAULT_PRETTY true
#else
# define DEFAULT_PRETTY false
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

const char *color_off     = ""; // disable colors
const char *color_del     = ""; // erase line after the cursor

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
Flag flag_no_messages;
Flag flag_match;
Flag flag_count;
Flag flag_fixed_strings;
Flag flag_free_space;
Flag flag_ignore_case;
Flag flag_smart_case;
Flag flag_invert_match;
Flag flag_only_line_number;
Flag flag_with_filename;
Flag flag_no_filename;
Flag flag_line_number;
Flag flag_column_number;
Flag flag_byte_offset;
Flag flag_initial_tab;
Flag flag_line_buffered;
Flag flag_only_matching;
Flag flag_ungroup;
Flag flag_quiet;
Flag flag_files_with_matches;
Flag flag_files_without_match;
Flag flag_not;
Flag flag_null;
Flag flag_basic_regexp;
Flag flag_perl_regexp;
Flag flag_word_regexp;
Flag flag_line_regexp;
Flag flag_dereference;
Flag flag_no_dereference;
Flag flag_binary;
Flag flag_binary_without_match;
Flag flag_text;
Flag flag_hex;
Flag flag_with_hex;
Flag flag_empty;
Flag flag_decompress;
Flag flag_any_line;
Flag flag_heading;
Flag flag_break;
Flag flag_cpp;
Flag flag_csv;
Flag flag_json;
Flag flag_xml;
bool flag_pretty                   = DEFAULT_PRETTY;
bool flag_hidden                   = DEFAULT_HIDDEN;
bool flag_confirm                  = DEFAULT_CONFIRM;
bool flag_stdin                    = false;
bool flag_all_threads              = false;
bool flag_no_header                = false;
bool flag_usage_warnings           = false;
bool flag_hex_hbr                  = true;
bool flag_hex_cbr                  = true;
bool flag_hex_chr                  = true;
bool flag_sort_rev                 = false;
Sort flag_sort_key                 = Sort::NA;
Action flag_devices_action         = Action::SKIP;
Action flag_directories_action     = Action::SKIP;
size_t flag_fuzzy                  = 0;
size_t flag_query                  = 0;
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_min_depth              = 0;
size_t flag_max_depth              = 0;
size_t flag_max_count              = 0;
size_t flag_max_files              = 0;
size_t flag_min_line               = 0;
size_t flag_max_line               = 0;
size_t flag_not_magic              = 0;
size_t flag_min_magic              = 1;
size_t flag_jobs                   = 0;
size_t flag_hex_columns            = 16;
size_t flag_tabs                   = DEFAULT_TABS;
size_t flag_max_mmap               = DEFAULT_MAX_MMAP_SIZE;
size_t flag_min_steal              = MIN_STEAL;
const char *flag_pager             = DEFAULT_PAGER;
const char *flag_color             = DEFAULT_COLOR;
const char *flag_tag               = NULL;
const char *flag_apply_color       = NULL;
const char *flag_hexdump           = NULL;
const char *flag_colors            = NULL;
const char *flag_encoding          = NULL;
const char *flag_filter            = NULL;
const char *flag_format            = NULL;
const char *flag_format_begin      = NULL;
const char *flag_format_end        = NULL;
const char *flag_format_open       = NULL;
const char *flag_format_close      = NULL;
const char *flag_sort              = NULL;
const char *flag_stats             = NULL;
const char *flag_devices           = "skip";
const char *flag_directories       = "skip";
const char *flag_label             = "(standard input)";
const char *flag_separator         = ":";
const char *flag_group_separator   = "--";
const char *flag_binary_files      = "binary";
const char *flag_config            = NULL;
const char *flag_save_config       = NULL;
std::string              flag_config_file;
std::set<std::string>    flag_config_options;
std::vector<std::string> flag_regexp;
std::vector<std::string> flag_neg_regexp;
std::vector<std::string> flag_not_regexp;
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

// ugrep command-line arguments pointing to argv[]
const char *arg_pattern = NULL;
std::vector<const char*> arg_files;

void set_color(const char *colors, const char *parameter, char color[COLORLEN]);
void trim(std::string& line);
void trim_pathname_arg(const char *arg);
void append(std::string& pattern, std::string& regex);
bool is_output(ino_t inode);
size_t strtonum(const char *string, const char *message);
size_t strtopos(const char *string, const char *message);
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message, bool optional_first = false);
size_t strtofuzzy(const char *string, const char *message);

void split_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs);
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

// grep manages output, matcher, input, and decompression
struct Grep {

  // entry type
  enum class Type { SKIP, DIRECTORY, OTHER };

  // entry data extracted from directory contents
  struct Entry {
    Entry(std::string& pathname, ino_t inode, uint64_t info)
      :
        pathname(std::move(pathname)),
        inode(inode),
        info(info),
        cost(0)
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

#ifndef OS_WIN
  // extend the reflex::Input::Handler to handle stdin from a TTY or a slow pipe
  struct StdInHandler : public reflex::Input::Handler {

    StdInHandler(Grep *grep)
      :
        grep(grep)
    { }

    Grep *grep;

    int operator()()
    {
      grep->out.flush();

      while (true)
      {
        struct timeval tv;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int r = ::select(1, &fds, NULL, &fds, &tv);
        if (r < 0 && errno != EINTR)
          return 0;
        if (r > 0)
          return 1;
      }
    }
  };
#endif

  // extend the reflex::AbstractMatcher::Handler with a grep object reference and references to some of the grep::search locals
  struct GrepHandler : public reflex::AbstractMatcher::Handler {

    GrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        grep(grep),
        pathname(pathname),
        lineno(lineno),
        hex(hex),
        binary(binary),
        matches(matches),
        stop(stop)
    { }

    Grep&        grep;     // grep object
    const char*& pathname; // grep::search argument pathname
    size_t&      lineno;   // grep::search lineno local variable
    bool&        hex;      // grep::search hex local variable
    bool&        binary;   // grep::search binary local variable
    size_t&      matches;  // grep::search matches local variable
    bool&        stop;     // grep::search stop local variable

    // get the start of the before context, if present
    void begin_before(reflex::AbstractMatcher& matcher, const char *buf, size_t len, size_t num, const char*& ptr, size_t& size, size_t& offset)
    {
      if (len == 0)
      {
        ptr = NULL;
        return;
      }

      size_t current = matcher.lineno();
      size_t between = current - lineno;

      if (between <= 1)
      {
        ptr = NULL;
      }
      else
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

    InvertMatchGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& hex, bool& binary, size_t& matches, bool& stop)
      :
        GrepHandler(grep, pathname, lineno, hex, binary, matches, stop)
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

        // -m: max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;

        // output blocked?
        if (grep.out.eof)
          break;

        ++matches;

        if (flag_with_hex)
          binary = false;

        binary = binary || flag_hex || (!flag_text && is_binary(ptr, size));

        if (binary && !flag_hex && !flag_with_hex)
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
          if (size > 0)
          {
            size_t sizen = size - (ptr[size - 1] == '\n');
            if (sizen > 0)
            {
              grep.out.str(color_sl);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }
  };

  // extend event GrepHandler to output any context lines for -y
  struct AnyLineGrepHandler : public GrepHandler {

    AnyLineGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        GrepHandler(grep, pathname, lineno, hex, binary, matches, stop),
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
          if (rest_line_size > 0)
          {
            rest_line_size -= rest_line_data[rest_line_size - 1] == '\n';
            if (rest_line_size > 0)
            {
              grep.out.str(flag_invert_match ? color_cx : color_sl);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
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

        // --max-files: max reached?
        if (flag_invert_match && matches == 0 && !Stats::found_part())
        {
          stop = true;
          break;
        }

        // -m: max number of matches reached?
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

        if (binary && !flag_hex && !flag_with_hex)
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
          if (size > 0)
          {
            size_t sizen = size - (ptr[size - 1] == '\n');
            if (sizen > 0)
            {
              grep.out.str(v_color_cx);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }

    const char*& rest_line_data;
    size_t&      rest_line_size;
    size_t&      rest_line_last;

  };

  // extend event AnyLineGrepHandler to output specific context lines for -A, -B, and -C
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

    ContextGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        AnyLineGrepHandler(grep, pathname, lineno, hex, binary, matches, stop, rest_line_data, rest_line_size, rest_line_last)
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
            if (size > 0)
            {
              size -= ptr[size - 1] == '\n';
              if (size > 0)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr, size);
                grep.out.str(color_off);
              }
            }
            grep.out.nl();
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
          if (rest_line_size > 0)
          {
            rest_line_size -= rest_line_data[rest_line_size - 1] == '\n';
            if (rest_line_size > 0)
            {
              grep.out.str(flag_invert_match ? color_cx : color_sl);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
        }

        rest_line_data = NULL;
      }

      while (ptr != NULL)
      {
        // --range: max line exceeded?
        if (flag_max_line > 0 && lineno > flag_max_line)
          break;

        // --max-files: max reached?
        if (flag_invert_match && matches == 0 && !Stats::found_part())
        {
          stop = true;
          break;
        }

        // -m: max number of matches reached?
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

        if (binary && !flag_hex && !flag_with_hex)
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
            if (size > 0)
            {
              size_t sizen = size - (ptr[size - 1] == '\n');
              if (sizen > 0)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr, sizen);
                grep.out.str(color_off);
              }
            }
            grep.out.nl();
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

  // extend event AnyLineGrepHandler to output specific context lines for -A, -B, and -C with -v
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

    InvertContextGrepHandler(Grep& grep, const char*& pathname, size_t& lineno, bool& hex, bool& binary, size_t& matches, bool& stop, const char*& rest_line_data, size_t& rest_line_size, size_t& rest_line_last)
      :
        AnyLineGrepHandler(grep, pathname, lineno, hex, binary, matches, stop, rest_line_data, rest_line_size, rest_line_last)
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
            if (size > pos)
            {
              size -= ptr[size - 1] == '\n';
              if (size > pos)
              {
                grep.out.str(color_cx);
                grep.out.str(ptr + pos, size - pos);
                grep.out.str(color_off);
              }
            }
            grep.out.nl();
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
          if (rest_line_size > 0)
          {
            rest_line_size -= rest_line_data[rest_line_size - 1] == '\n';
            if (rest_line_size > 0)
            {
              grep.out.str(color_cx);
              grep.out.str(rest_line_data, rest_line_size);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
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

        // --max-files: max reached?
        if (matches == 0 && !Stats::found_part())
        {
          stop = true;
          break;
        }

        // -m: max number of matches reached?
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

        if (binary && !flag_hex && !flag_with_hex)
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
          if (size > 0)
          {
            size_t sizen = size - (ptr[size - 1] == '\n');
            if (sizen > 0)
            {
              grep.out.str(color_sl);
              grep.out.str(ptr, sizen);
              grep.out.str(color_off);
            }
          }
          grep.out.nl();
        }

        next_before(buf, len, num, ptr, size, offset);
      }
    }

    InvertContextState state;

  };

  Grep(FILE *file, reflex::AbstractMatcher *matcher)
    :
      out(file),
      matcher(matcher),
      file(NULL)
#ifndef OS_WIN
    , stdin_handler(this)
#endif
#ifdef HAVE_LIBZ
    , zstream(NULL),
      stream(NULL)
#ifdef WITH_DECOMPRESSION_THREAD
    , thread_end(false),
      extracting(false),
      waiting(false)
#endif
#endif
  {
    restline.reserve(256);
  }

  virtual ~Grep()
  {
#ifdef HAVE_LIBZ

#ifdef WITH_DECOMPRESSION_THREAD
    if (thread.joinable())
    {
      thread_end = true;

      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (waiting)
        pipe_zstrm.notify_one();
      lock.unlock();

      thread.join();
    }
#endif

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
  }

  // cancel all active searches
  void cancel()
  {
    // global cancellation is forced by cancelling the shared output
    out.cancel();
  }

  // search the specified files or standard input for pattern matches
  virtual void ugrep();

  // search file or directory for pattern matches
  Type select(size_t level, const char *pathname, const char *basename, int type, ino_t& inode, uint64_t& info, bool is_argument = false);

  // recurse a directory
  virtual void recurse(size_t level, const char *pathname);

  // -Z and --sort=best: perform a presearch to determine edit distance cost, return cost of pathname file, 65535 when no match is found
  uint16_t cost(const char *pathname);

  // search a file
  virtual void search(const char *pathname);

  // open a file for (binary) reading and assign input, decompress the file when -z, --decompress specified
  bool open_file(const char *pathname)
  {
    if (pathname == NULL)
    {
      if (source == NULL)
        return false;

      pathname = flag_label;
      file = source;

#ifdef OS_WIN
      if (flag_binary || flag_decompress)
        _setmode(fileno(source), _O_BINARY);
#endif
    }
    else if (fopenw_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
    {
      warning("cannot read", pathname);

      return false;
    }

    // --filter: fork process to filter file, when applicable
    if (!filter(file, pathname))
      return false;

#ifdef HAVE_LIBZ
    if (flag_decompress)
    {
#ifdef WITH_DECOMPRESSION_THREAD

      pipe_fd[0] = -1;
      pipe_fd[1] = -1;

      FILE *pipe_in = NULL;

      // open pipe between worker and decompression thread, then start decompression thread
      if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "r")) != NULL)
      {
        // create or open a new zstreambuf to (re)start the decompression thread
        if (zstream == NULL)
          zstream = new zstreambuf(pathname, file);
        else
          zstream->open(pathname, file);

        if (thread.joinable())
        {
          pipe_zstrm.notify_one();
        }
        else
        {
          try
          {
            thread_end = false;
            extracting = false;
            waiting = false;

            thread = std::thread(&Grep::decompress, this);
          }

          catch (std::system_error&)
          {
            fclose(pipe_in);
            close(pipe_fd[1]);
            pipe_fd[0] = -1;
            pipe_fd[1] = -1;

            warning("cannot create thread to decompress",  pathname);

            return false;
          }
        }
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

        warning("cannot create pipe to decompress",  pathname);

        return false;
      }

      input = reflex::Input(pipe_in, flag_encoding_type);

#else

      // create or open a new zstreambuf
      if (zstream == NULL)
        zstream = new zstreambuf(pathname, file);
      else
        zstream->open(pathname, file);

      stream = new std::istream(zstream);

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

      // --filter-magic-label: if the file is seekable, then check for a magic pattern match
      if (!flag_filter_magic_label.empty() && fseek(in, 0, SEEK_CUR) == 0)
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

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD

  // decompression thread
  void decompress()
  {
    while (!thread_end)
    {
      // use the zstreambuf internal buffer to hold decompressed data
      unsigned char *buf;
      size_t maxlen;
      zstream->get_buffer(buf, maxlen);

      // to hold the path (prefix + name) extracted from the zip file
      std::string path;

      // reset flags
      extracting = false;
      waiting = false;

      // extract the parts of a zip file, one by one, if zip file detected
      while (!thread_end)
      {
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

        if (!filter_tar(*zstream, path, buf, maxlen, len) && !filter_cpio(*zstream, path, buf, maxlen, len))
        {
          // not a tar/cpio file, decompress the data into pipe, if not unzipping or if zipped file meets selection criteria
          is_selected = is_regular && (zipinfo == NULL || select_matching(path.c_str(), buf, static_cast<size_t>(len), true));

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
            len = zstream->decompress(buf, maxlen);
          }
        }

        // break if not unzipping or if no more files to unzip
        if (zstream->zipinfo() == NULL)
          break;

        // extracting a zip file
        extracting = true;

        // after unzipping the selected zip file, close our end of the pipe and loop for the next file
        if (is_selected && pipe_fd[1] != -1)
        {
          close(pipe_fd[1]);
          pipe_fd[1] = -1;
        }
      }

      extracting = false;

      if (pipe_fd[1] != -1)
      {
        // close our end of the pipe
        close(pipe_fd[1]);
        pipe_fd[1] = -1;
      }

      if (!thread_end)
      {
        // wait until a new zstream is ready
        std::unique_lock<std::mutex> lock(pipe_mutex);
        pipe_close.notify_one();
        waiting = true;
        pipe_zstrm.wait(lock);
        waiting = false;
        lock.unlock();
      }
    }
  }

  // if tar file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_tar(zstreambuf& zstream, const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
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
            len = zstream.decompress(buf, maxlen);
          }

          // error?
          if (len < 0)
            break;

          // fill the rest of the buffer with decompressed data
          if (static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstream.decompress(buf + len, maxlen - static_cast<size_t>(len));

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
  bool filter_cpio(zstreambuf& zstream, const std::string& partprefix, unsigned char *buf, size_t maxlen, std::streamsize len)
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
            len = zstream.decompress(buf, maxlen);
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
            std::streamsize len_in = zstream.decompress(buf + len, maxlen - static_cast<size_t>(len));

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
            len = zstream.decompress(buf, maxlen);
          }

          // error?
          if (len < 0)
            break;

          if (static_cast<size_t>(len) < maxlen)
          {
            // fill the rest of the buffer with decompressed data
            std::streamsize len_in = zstream.decompress(buf + len, maxlen - static_cast<size_t>(len));

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

#endif
#endif
  
  // close the file and clear input, return true if next file is extracted from an archive to search
  bool close_file(const char *pathname)
  {
    (void)pathname; // appease -Wunused-parameter

#ifdef HAVE_LIBZ

#ifdef WITH_DECOMPRESSION_THREAD

    if (flag_decompress && pipe_fd[0] != -1)
    {
      // close the FILE* and its underlying pipe created with pipe() and fdopen()
      if (input.file() != NULL)
      {
        fclose(input.file());
        input = static_cast<FILE*>(NULL);
      }

      // our end of the pipe is now closed
      pipe_fd[0] = -1;

      // if extracting and the decompression filter thread is not yet waiting, then wait until the other end closed the pipe
      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (!waiting)
        pipe_close.wait(lock);
      lock.unlock();

      // extract the next file from the archive when applicable, e.g. zip format
      if (extracting)
      {
        // output is not blocked or cancelled
        if (!out.eof && !out.cancelled())
        {
          FILE *pipe_in = NULL;

          // open pipe between worker and decompression thread, then start decompression thread
          if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "r")) != NULL)
          {
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();

            input = reflex::Input(pipe_in, flag_encoding_type);

            // loop back in search() to start searching the next file in the archive
            return true;
          }

          // failed to create a new pipe
          warning("cannot open decompression pipe while reading", pathname);

          if (pipe_fd[0] != -1)
          {
            close(pipe_fd[0]);
            close(pipe_fd[1]);
          }
        }

        pipe_fd[0] = -1;
        pipe_fd[1] = -1;

        // notify the decompression thread filter_tar/filter_cpio
        pipe_ready.notify_one();
      }
    }

#endif

    if (stream != NULL)
    {
      delete stream;
      stream = NULL;
    }

#endif

#ifdef WITH_STDIN_DRAIN
    // drain stdin until eof
    if (file == stdin && !feof(stdin))
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
          if (r >= 0)
          {
            if (!(fcntl(0, F_GETFL) & O_NONBLOCK))
              break;
            struct timeval tv;
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(0, &fds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int r = ::select(1, &fds, NULL, &fds, &tv);
            if (r < 0 && errno != EINTR)
              break;
          }
          else if (errno != EINTR)
          {
            break;
          }
        }
      }
    }
#endif

    // close the file
    if (file != NULL && file != stdin && file != source)
    {
      fclose(file);
      file = NULL;
    }

    input.clear();

    return false;
  }

  // specify input to read for matcher, when input is a regular file then try mmap for zero copy overhead
  bool init_read()
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
    if (flag_binary_without_match)
    {
      const char *eol = matcher->eol(); // warning: call eol() before bol()
      const char *bol = matcher->bol();
      if (is_binary(bol, eol - bol))
        return false;
    }

    // --range=NUM1[,NUM2]: start searching at line NUM1
    for (size_t i = flag_min_line; i > 1; --i)
      if (!matcher->skip('\n'))
        break;

    return true;
  }

  const char              *filename;      // the name of the file being searched
  std::string              partname;      // the name of an extracted file from an archive
  std::string              restline;      // a buffer to store the rest of a line to search
  Output                   out;           // asynchronous output
  reflex::AbstractMatcher *matcher;       // the pattern matcher we're using, never NULL
  MMap                     mmap;          // mmap state
  reflex::Input            input;         // input to the matcher
  FILE                    *file;          // the current input file
#ifndef OS_WIN
  StdInHandler             stdin_handler; // a handler to handle non-blocking stdin from a TTY or a slow pipe
#endif
#ifdef HAVE_LIBZ
  zstreambuf              *zstream;       // the decompressed stream from the current input file
  std::istream            *stream;        // input stream layered on the decompressed stream
#ifdef WITH_DECOMPRESSION_THREAD
  std::thread              thread;        // decompression thread
  std::atomic_bool         thread_end;    // true if decompression thread should terminate
  int                      pipe_fd[2];    // decompressed stream pipe
  std::mutex               pipe_mutex;    // mutex to extract files in thread
  std::condition_variable  pipe_zstrm;    // cv to control new pipe creation
  std::condition_variable  pipe_ready;    // cv to control new pipe creation
  std::condition_variable  pipe_close;    // cv to control new pipe creation
  volatile bool            extracting;    // true if extracting files from TAR or ZIP archive
  volatile bool            waiting;       // true if decompression thread is waiting
#endif
#endif

};

// a job in the job queue
struct Job {

  // sentinel job NONE
  static const size_t NONE = UNDEFINED_SIZE;

  Job()
    :
      pathname(),
      slot(NONE)
  { }

  Job(const char *pathname, size_t slot)
    :
      pathname(pathname),
      slot(slot)
  { }

  bool none()
  {
    return slot == NONE;
  }

  std::string pathname;
  size_t      slot;
};

struct GrepWorker;

// master submits jobs to workers and implements operations to support lock-free job stealing
struct GrepMaster : public Grep {

  GrepMaster(FILE *file, reflex::AbstractMatcher *matcher)
    :
      Grep(file, matcher),
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

  std::list<GrepWorker>           workers; // workers running threads
  std::list<GrepWorker>::iterator iworker; // the next worker to submit a job to
  Output::Sync                    sync;    // sync output of workers

};

// worker runs a thread to execute jobs submitted by the master
struct GrepWorker : public Grep {

  GrepWorker(FILE *file, reflex::AbstractMatcher *matcher, GrepMaster *master)
    :
      Grep(file, matcher->clone()),
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
  }

  // worker thread execution
  void execute();

  // submit Job::NONE sentinel to this worker
  void submit_job()
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    jobs.emplace_back();
    ++todo;

    queue_work.notify_one();
  }

  // submit a job to this worker
  void submit_job(const char *pathname, size_t slot)
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    jobs.emplace_back(pathname, slot);
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
      workers.emplace(workers.end(), out.file, matcher, this);
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
void GrepMaster::submit(const char *pathname)
{
  iworker->submit_job(pathname, sync.next++);

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
    search(job.pathname.c_str());

    // end output in ORDERED mode (--sort) for this job slot
    out.end();

    // if only one job is left to do, try stealing another job from a co-worker
    if (todo <= 1)
      master->steal(this);
  }
}

// table of RE/flex file encodings for option --encoding
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
  { "ISO-8869-2",  reflex::Input::file_encoding::iso8859_2  },
  { "ISO-8869-3",  reflex::Input::file_encoding::iso8859_3  },
  { "ISO-8869-4",  reflex::Input::file_encoding::iso8859_4  },
  { "ISO-8869-5",  reflex::Input::file_encoding::iso8859_5  },
  { "ISO-8869-6",  reflex::Input::file_encoding::iso8859_6  },
  { "ISO-8869-7",  reflex::Input::file_encoding::iso8859_7  },
  { "ISO-8869-8",  reflex::Input::file_encoding::iso8859_8  },
  { "ISO-8869-9",  reflex::Input::file_encoding::iso8859_9  },
  { "ISO-8869-10", reflex::Input::file_encoding::iso8859_10 },
  { "ISO-8869-11", reflex::Input::file_encoding::iso8859_11 },
  { "ISO-8869-13", reflex::Input::file_encoding::iso8859_13 },
  { "ISO-8869-14", reflex::Input::file_encoding::iso8859_14 },
  { "ISO-8869-15", reflex::Input::file_encoding::iso8859_15 },
  { "ISO-8869-16", reflex::Input::file_encoding::iso8859_16 },
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
  { "actionscript", "as,mxml",                                                  NULL },
  { "ada",          "ada,adb,ads",                                              NULL },
  { "asm",          "asm,s,S",                                                  NULL },
  { "asp",          "asp",                                                      NULL },
  { "aspx",         "master,ascx,asmx,aspx,svc",                                NULL },
  { "autoconf",     "ac,in",                                                    NULL },
  { "automake",     "am,in",                                                    NULL },
  { "awk",          "awk",                                                      NULL },
  { "Awk",          "awk",                                                      "#!\\h*/.*\\Wg?awk(\\W.*)?\\n" },
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
  { "Dart",         "dart",                                                     "#!\\h*/.*\\Wdart(\\W.*)?\\n" },
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
  { "Node",         "js",                                                       "#!\\h*/.*\\Wnode(\\W.*)?\\n" },
  { "objc",         "m,h",                                                      NULL },
  { "objc++",       "mm,h",                                                     NULL },
  { "ocaml",        "ml,mli,mll,mly",                                           NULL },
  { "parrot",       "pir,pasm,pmc,ops,pod,pg,tg",                               NULL },
  { "pascal",       "pas,pp",                                                   NULL },
  { "pdf",          "pdf",                                                      NULL },
  { "Pdf",          "pdf",                                                      "\\x25\\x50\\x44\\x46\\x2d" },
  { "perl",         "pl,PL,pm,pod,t,psgi",                                      NULL },
  { "Perl",         "pl,PL,pm,pod,t,psgi",                                      "#!\\h*/.*\\Wperl(\\W.*)?\\n" },
  { "php",          "php,php3,php4,phtml",                                      NULL },
  { "Php",          "php,php3,php4,phtml",                                      "#!\\h*/.*\\Wphp(\\W.*)?\\n" },
  { "png",          "png",                                                      NULL },
  { "Png",          "png",                                                      "\\x89png\\x0d\\x0a\\x1a\\x0a" },
  { "prolog",       "pl,pro",                                                   NULL },
  { "python",       "py",                                                       NULL },
  { "Python",       "py",                                                       "#!\\h*/.*\\Wpython(\\W.*)?\\n" },
  { "r",            "R",                                                        NULL },
  { "rpm",          "rpm",                                                      NULL },
  { "Rpm",          "rpm",                                                      "\\xed\\xab\\xee\\xdb" },
  { "rst",          "rst",                                                      NULL },
  { "rtf",          "rtf",                                                      NULL },
  { "Rtf",          "rtf",                                                      "\\{\\rtf1" },
  { "ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec,Rakefile",                 NULL },
  { "Ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec,Rakefile",                 "#!\\h*/.*\\Wruby(\\W.*)?\\n" },
  { "rust",         "rs",                                                       NULL },
  { "scala",        "scala",                                                    NULL },
  { "scheme",       "scm,ss",                                                   NULL },
  { "shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish",                       NULL },
  { "Shell",        "sh,bash,dash,csh,tcsh,ksh,zsh,fish",                       "#!\\h*/.*\\W(ba|da|t?c|k|z|fi)?sh(\\W.*)?\\n" },
  { "smalltalk",    "st",                                                       NULL },
  { "sql",          "sql,ctl",                                                  NULL },
  { "svg",          "svg",                                                      NULL },
  { "swift",        "swift",                                                    NULL },
  { "tcl",          "tcl,itcl,itk",                                             NULL },
  { "tex",          "tex,cls,sty,bib",                                          NULL },
  { "text",         "text,txt,TXT,md,rst",                                      NULL },
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

// function protos for functions used in main()
void init(int argc, const char **argv);

// ugrep main()
int main(int argc, const char **argv)
{
#ifdef OS_WIN

  LPWSTR *argws = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (argws == NULL)
    error("cannot parse command line arguments", "CommandLineToArgvW failed");

  // store UTF-8 arguments for the duration of main() and convert Unicode command line arguments argws[] to UTF-8 arguments argv[]
  std::vector<std::string> args;
  args.resize(argc);
  for (int i = 0; i < argc; ++i)
  {
    args[i] = utf8_encode(argws[i]);
    argv[i] = args[i].c_str();
  }

  LocalFree(argws);

#endif

#ifdef OS_WIN

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
    strtopos2(arg, flag_min_depth, flag_max_depth, "invalid argument --", true);
  }
}

void options(int argc, const char **argv);

// load config file specified or the default .ugrep, located in the working directory or home directory
static void load_config()
{
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
        // construct an option argument and make it persistent (to parse as argv[])
        line.insert(0, "--");
        const char *arg = flag_config_options.insert(line).first->c_str();
        const char *args[2] = { NULL, arg };

        warnings = 0;

        options(2, args);

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
      std::cerr << "Try 'ugrep --help [WHAT]' for more information\n";

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
# Try `ugrep --help [WHAT]' for more information.\n\n");

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
  fprintf(file, "# Enable/disable query UI confirmation prompts, default: no-confirm\n%s\n\n", flag_confirm ? "confirm" : "no-confirm");
  fprintf(file, "# Enable/disable headings for terminal output, default: no-heading\n%s\n\n", flag_heading.is_undefined() ? "# no-heading" : flag_heading ? "heading" : "no-heading");
  fprintf(file, "# Enable/disable or specify a pager for terminal output, default: no-pager\n%s\n\n", flag_pager != NULL ? flag_pager : "no-pager");
  fprintf(file, "# Enable/disable pretty output to the terminal, default: no-pretty\n%s\n\n", flag_pretty ? "pretty" : "no-pretty");

  fprintf(file, "### SEARCH PATTERNS ###\n\n");

  fprintf(file, "# Enable/disable case-insensitive search, default: no-ignore-case\n%s\n\n", flag_ignore_case.is_undefined() ? "# no-ignore-case" : flag_ignore_case ? "ignore-case" : "no-ignore-case");
  fprintf(file, "# Enable/disable smart case, default: no-smart-case\n%s\n\n", flag_smart_case.is_undefined() ? "# no-smart-case" : flag_smart_case ? "smart-case" : "no-smart-case");
  fprintf(file, "# Enable/disable empty pattern matches, default: no-empty\n%s\n\n", flag_empty.is_undefined() ? "# no-empty" : flag_empty ? "empty" : "no-empty");

  fprintf(file, "### SEARCH TARGETS ###\n\n");

  fprintf(file, "# Enable/disable searching hidden files and directories, default: no-hidden\n%s\n\n", flag_hidden ? "hidden" : "no-hidden");
  fprintf(file, "# Enable/disable binary files, default: no-ignore-binary\n%s\n\n", strcmp(flag_binary_files, "without-match") == 0 ? "ignore-binary" : "no-ignore-binary");
  fprintf(file, "# Enable/disable decompression and archive search, default: no-decompress\n%s\n\n", flag_decompress ? "decompress" : "no-decompress");
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

  fprintf(file, "# Enable/disable sorted output, default: no-sort\n%s\n\n", flag_sort != NULL ? flag_sort : "no-sort");

  if (ferror(file))
    error("cannot save", flag_save_config);

  if (file != stdout)
    fclose(file);
}

// parse the command-line options
void options(int argc, const char **argv)
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
                  flag_after_context = strtopos(arg + 14, "invalid argument --after-context=");
                else if (strcmp(arg, "any-line") == 0)
                  flag_any_line = true;
                else if (strcmp(arg, "after-context") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--after-context or --any-line");
                break;

              case 'b':
                if (strcmp(arg, "basic-regexp") == 0)
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
                else if (strcmp(arg, "before-context") == 0 || strcmp(arg, "binary-files") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--basic-regexp, --before-context, --binary, --binary-files, --break or --byte-offset");
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
                else if (strcmp(arg, "config") == 0)
                  ;
                else if (strncmp(arg, "config=", 7) == 0)
                  ;
                else if (strcmp(arg, "confirm") == 0)
                  flag_confirm = true;
                else if (strncmp(arg, "context=", 8) == 0)
                  flag_after_context = flag_before_context = strtopos(arg + 8, "invalid argument --context=");
                else if (strcmp(arg, "count") == 0)
                  flag_count = true;
                else if (strcmp(arg, "cpp") == 0)
                  flag_cpp = true;
                else if (strcmp(arg, "csv") == 0)
                  flag_csv = true;
                else if (strcmp(arg, "colors") == 0 || strcmp(arg, "colours") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--color, --colors, --column-number, --config, --confirm, --context, --count, --cpp or --csv");
                break;

              case 'd':
                if (strcmp(arg, "decompress") == 0)
                  flag_decompress = true;
                else if (strncmp(arg, "depth=", 6) == 0)
                  strtopos2(arg + 6, flag_min_depth, flag_max_depth, "invalid argument --depth=", true);
                else if (strcmp(arg, "dereference") == 0)
                  flag_dereference = true;
                else if (strcmp(arg, "dereference-recursive") == 0)
                  flag_directories = "dereference-recurse";
                else if (strncmp(arg, "devices=", 8) == 0)
                  flag_devices = arg + 8;
                else if (strncmp(arg, "directories=", 12) == 0)
                  flag_directories = arg + 12;
                else if (strcmp(arg, "depth") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--decompress, --depth, --dereference, --dereference-recursive, --devices or --directories");
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
                else if (strncmp(arg, "file-extension=", 16) == 0)
                  flag_file_extension.emplace_back(arg + 16);
                else if (strncmp(arg, "file-magic=", 11) == 0)
                  flag_file_magic.emplace_back(arg + 11);
                else if (strncmp(arg, "file-type=", 10) == 0)
                  flag_file_type.emplace_back(arg + 10);
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
                  usage("invalid option --", arg, "--file, --file-extension, --file-magic, --file-type, --files-with-matches, --files-without-match, --fixed-strings, --filter, --filter-magic-label, --format, --format-begin, --format-close, --format-end, --format-open, --fuzzy or --free-space");
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
                else
                  usage("invalid option --", arg, "--heading, --help, --hex, --hexdump or --hidden");
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
                if (strncmp(arg, "jobs=", 4) == 0)
                  flag_jobs = strtonum(arg + 4, "invalid argument --jobs=");
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
                else
                  usage("invalid option --", arg, "--label, --line-buffered, --line-number or --line-regexp");
                break;

              case 'm':
                if (strcmp(arg, "match") == 0)
                  flag_match = true;
                else if (strncmp(arg, "max-count=", 10) == 0)
                  flag_max_count = strtopos(arg + 10, "invalid argument --max-count=");
                else if (strncmp(arg, "max-files=", 10) == 0)
                  flag_max_files = strtopos(arg + 10, "invalid argument --max-files=");
                else if (strncmp(arg, "min-steal=", 10) == 0)
                  flag_min_steal = strtopos(arg + 10, "invalid argument --min-steal=");
                else if (strcmp(arg, "mmap") == 0)
                  flag_max_mmap = MAX_MMAP_SIZE;
                else if (strncmp(arg, "mmap=", 5) == 0)
                  flag_max_mmap = strtopos(arg + 5, "invalid argument --mmap=");
                else if (strcmp(arg, "messages") == 0)
                  flag_no_messages = false;
                else if (strcmp(arg, "max-count") == 0 || strcmp(arg, "max-files") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--match, --max-count, --max-files, --mmap or --messages");
                break;

              case 'n':
                if (strncmp(arg, "neg-regexp=", 11) == 0)
                  flag_neg_regexp.emplace_back(arg + 11);
                else if (strcmp(arg, "not") == 0)
                  flag_not = true;
                else if (strcmp(arg, "no-any-line") == 0)
                  flag_any_line = false;
                else if (strcmp(arg, "no-binary") == 0)
                  flag_binary = false;
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
                else if (strcmp(arg, "null") == 0)
                  flag_null = true;
                else if (strcmp(arg, "neg-regexp") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--neg-regexp, --not, --no-any-line, --no-binary, --no-byte-offset, --no-color, --no-confirm, --no-decompress, --no-dereference, --no-empty, --no-filename, --no-group-separator, --no-heading, --no-hidden, --no-ignore-binary, --no-ignore-case, --no-ignore-files --no-initial-tab, --no-invert-match, --no-line-number, --no-only-line-number, --no-only-matching, --no-messages, --no-mmap, --no-pager, --no-pretty, --no-smart-case, --no-sort, --no-stats, --no-ungroup or --null");
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
                else if (strcmp(arg, "perl-regexp") == 0)
                  flag_perl_regexp = true;
                else if (strcmp(arg, "pretty") == 0)
                  flag_pretty = true;
                else
                  usage("invalid option --", arg, "--pager, --perl-regexp or --pretty");
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
                  (flag_not ? flag_not_regexp : flag_regexp).emplace_back(arg + 7);
                else if (strcmp(arg, "range") == 0)
                  usage("missing argument for --", arg);
                else
                  usage("invalid option --", arg, "--range, --recursive or --regexp");
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
                else
                  usage("invalid option --", arg, "--version");
                break;

              case 'w':
                if (strcmp(arg, "with-filename") == 0)
                  flag_with_filename = true;
                else if (strcmp(arg, "with-hex") == 0)
                  flag_binary_files = "with-hex";
                else if (strcmp(arg, "word-regexp") == 0)
                  flag_word_regexp = true;
                else
                  usage("invalid option --", arg, "--with-filename, --with-hex or --word-regexp");
                break;

              case 'x':
                if (strcmp(arg, "xml") == 0)
                  flag_xml = true;
                else
                  usage("invalid option --", arg, "--xml");
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
              flag_after_context = strtopos(&arg[*arg == '='], "invalid argument -A=");
            else if (++i < argc)
              flag_after_context = strtopos(argv[i], "invalid argument -A=");
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
              flag_before_context = strtopos(&arg[*arg == '='], "invalid argument -B=");
            else if (++i < argc)
              flag_before_context = strtopos(argv[i], "invalid argument -B=");
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
              flag_after_context = flag_before_context = strtopos(&arg[*arg == '='], "invalid argument -C=");
            else if (++i < argc)
              flag_after_context = flag_before_context = strtopos(argv[i], "invalid argument -C=");
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
              (flag_not ? flag_not_regexp : flag_regexp).emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              (flag_not ? flag_not_regexp : flag_regexp).emplace_back(argv[i]);
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
              flag_max_count = strtopos(&arg[*arg == '='], "invalid argument -m=");
            else if (++i < argc)
              flag_max_count = strtopos(argv[i], "invalid argument -m=");
            else
              usage("missing NUM argument for option -m");
            is_grouped = false;
            break;

          case 'N':
            ++arg;
            if (*arg)
              flag_neg_regexp.emplace_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_neg_regexp.emplace_back(argv[i]);
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
            if (*arg == '=' || isdigit(*arg) || strchr("+-~", *arg) != NULL)
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
    else if (options && strcmp(arg, "-") == 0)
    {
      // read standard input
      flag_stdin = true;
    }
    else if (arg_pattern == NULL && flag_file.empty())
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

  if (flag_config != NULL)
    load_config();

  // apply the appropriate options when the program is named grep, egrep, fgrep, zgrep, zegrep, zfgrep

  const char *program = strrchr(argv[0], PATHSEPCHR);

  if (program == NULL)
    program = argv[0];
  else
    ++program;

  if (strcmp(program, "ug") == 0)
  {
    // the 'ug' command is equivalent to 'ugrep --config' to load custom configuration files, when no --config=FILE is specified
    if (flag_config == NULL)
      load_config();
  }
  else if (strcmp(program, "grep") == 0)
  {
    // the 'grep' command is equivalent to 'ugrep -GY.'
    flag_basic_regexp = true;
    flag_hidden = true;
    flag_empty = true;
  }
  else if (strcmp(program, "egrep") == 0)
  {
    // the 'egrep' command is equivalent to 'ugrep -Y.'
    flag_hidden = true;
    flag_empty = true;
  }
  else if (strcmp(program, "fgrep") == 0)
  {
    // the 'fgrep' command is equivalent to 'ugrep -FY.'
    flag_fixed_strings = true;
    flag_hidden = true;
    flag_empty = true;
  }
  else if (strcmp(program, "zgrep") == 0)
  {
    // the 'zgrep' command is equivalent to 'ugrep -zGY.'
    flag_decompress = true;
    flag_basic_regexp = true;
    flag_hidden = true;
    flag_empty = true;
  }
  else if (strcmp(program, "zegrep") == 0)
  {
    // the 'zegrep' command is equivalent to 'ugrep -zY.'
    flag_decompress = true;
    flag_hidden = true;
    flag_empty = true;
  }
  else if (strcmp(program, "zfgrep") == 0)
  {
    // the 'zfgrep' command is equivalent to 'ugrep -zFY.'
    flag_decompress = true;
    flag_fixed_strings = true;
    flag_hidden = true;
    flag_empty = true;
  }

  // parse ugrep command-line options and arguments

  options(argc, argv);

  if (warnings > 0)
  {
    std::cerr << "Usage: ugrep [OPTIONS] [PATTERN] [-f FILE] [-e PATTERN] [FILE ...]\n";
    std::cerr << "Try 'ugrep --help [WHAT]' for more information\n";
    exit(EXIT_ERROR);
  }

  // -t list: list table of types and exit
  if (flag_file_type.size() == 1 && flag_file_type[0] == "list")
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

#ifndef HAVE_LIBZ
  // -z: but we don't have libz
  if (flag_decompress)
    usage("option -z is not available in this build configuration of ugrep");
#endif

  // -P disables -G and -Z
  if (flag_perl_regexp)
  {
#if defined(HAVE_PCRE2) || defined(HAVE_BOOST_REGEX)
    flag_basic_regexp = false;
    if (flag_fuzzy > 0)
      usage("options -P and -Z are not compatible");
    if (!flag_neg_regexp.empty())
      usage("options -P and -N are not compatible");
    if (!flag_not_regexp.empty())
      usage("options -P and --not are not compatible");
#else
    usage("option -P is not available in this build configuration of ugrep");
#endif
  }

  // check TTY info and set colors (warnings and errors may occur from here on)
  terminal();

  // --save-config and --save-config=FILE
  if (flag_save_config != NULL)
  {
    save_config();

    exit(EXIT_ERROR);
  }

  // --encoding: parse ENCODING value
  if (flag_encoding != NULL)
  {
    int i;

    // scan the encoding_table[] for a matching encoding
    for (i = 0; encoding_table[i].format != NULL; ++i)
      if (strcmp(flag_encoding, encoding_table[i].format) == 0)
        break;

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
    usage("invalid argument --binary-files=TYPE, valid arguments are 'binary', 'without-match', 'text', 'hex', and 'with-hex'");

  // --hexdump: normalize by assigning flags
  if (flag_hexdump != NULL)
  {
    if (isdigit(*flag_hexdump))
    {
      flag_hex_columns = 8 * (*flag_hexdump - '0');
      if (flag_hex_columns == 0 || flag_hex_columns > MAX_HEX_COLUMNS)
        usage("invalid argument --hexdump=[1-8][b][c]");
    }
    if (strchr(flag_hexdump, 'b') != NULL)
      flag_hex_hbr = flag_hex_cbr = false;
    if (strchr(flag_hexdump, 'c') != NULL)
      flag_hex_chr = false;
    if (strchr(flag_hexdump, 'h') != NULL)
      flag_hex_hbr = false;
    if (!flag_with_hex)
      flag_hex = true;
  }
  
  // --tabs: value should be 1, 2, 4, or 8
  if (flag_tabs && flag_tabs != 1 && flag_tabs != 2 && flag_tabs != 4 && flag_tabs != 8)
    usage("invalid argument --tabs=NUM, valid arguments are 1, 2, 4, or 8");

  // --match: same as specifying an empty "" pattern argument
  if (flag_match)
    arg_pattern = "";

  // if no regex pattern is specified and no -f file and not -Q, then exit with usage message
  if (arg_pattern == NULL && flag_regexp.empty() && flag_file.empty() && flag_query == 0)
    usage("no PATTERN specified: specify an empty \"\" pattern to match all input");

  // regex PATTERN should be a FILE argument when -Q or -e PATTERN is specified
  if (!flag_match && arg_pattern != NULL && (flag_query > 0 || !flag_regexp.empty()))
  {
    arg_files.insert(arg_files.begin(), arg_pattern);
    arg_pattern = NULL;
  }

  // -D: check ACTION value
  if (strcmp(flag_devices, "skip") == 0)
    flag_devices_action = Action::SKIP;
  else if (strcmp(flag_devices, "read") == 0)
    flag_devices_action = Action::READ;
  else
    usage("invalid argument -D ACTION, valid arguments are 'skip' and 'read'");

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
    usage("invalid argument -d ACTION, valid arguments are 'skip', 'read', 'recurse', and 'dereference-recurse'");

  // if no FILE specified and no -r or -R specified, when reading standard input from a TTY then enable -R
  if (!flag_stdin && arg_files.empty() && flag_directories_action != Action::RECURSE && isatty(STDIN_FILENO))
  {
    flag_directories_action = Action::RECURSE;
    flag_dereference = true;
  }

  // if no FILE specified then read standard input, unless recursive searches are specified
  if (arg_files.empty() && flag_min_depth == 0 && flag_max_depth == 0 && flag_directories_action != Action::RECURSE)
    flag_stdin = true;

  // check FILE arguments, warn about non-existing FILE
  auto file = arg_files.begin();
  while (file != arg_files.end())
  {
#ifdef OS_WIN

    std::wstring wpathname = utf8_decode(*file);
    DWORD attr = GetFileAttributesW(wpathname.c_str());

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
        flag_all_threads = true;

        // remove trailing path separators, if any (*file points to argv[])
        trim_pathname_arg(*file);
      }

      ++file;
    }

#else

    struct stat buf;

    if (stat(*file, &buf) != 0)
    {
      // FILE does not exist
      warning(NULL, *file);

      file = arg_files.erase(file);
      if (arg_files.empty())
        exit(EXIT_ERROR);
    }
    else
    {
      // use threads to recurse into a directory
      if (S_ISDIR(buf.st_mode))
      {
        flag_all_threads = true;

        // remove trailing path separators, if any (*file points to argv[])
        trim_pathname_arg(*file);
      }

      ++file;
    }

#endif
  }

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

      split_globs(file, flag_exclude, flag_exclude_dir);

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

      split_globs(file, flag_include, flag_include_dir);

      if (file != stdin)
        fclose(file);
    }
  }

  // -t: parse TYPES and access type table to add -O (--file-extension) and -M (--file-magic) values
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

        std::string extensions(type_table[i].extensions);

        if (negate)
        {
          extensions.insert(0, "!");
          size_t j = 0;
          while ((j = extensions.find(',', j)) != std::string::npos)
            extensions.insert(++j, "!");
        }

        flag_file_extension.emplace_back(extensions);

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
      else if (flag_pager != NULL)
      {
        // --pager: if output is to a TTY then page through the results

        // open a pipe to a forked pager
        output = popen(flag_pager, "w");
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
          const char *term = getenv("TERM");
          if (term &&
              (strstr(term, "ansi") != NULL ||
               strstr(term, "xterm") != NULL ||
               strstr(term, "color") != NULL))
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
          usage("invalid argument --color=WHEN, valid arguments are 'never', 'always', and 'auto'");
        }

        if (flag_apply_color != NULL)
        {
          // get GREP_COLOR and GREP_COLORS, when defined
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

          // parse --colors to override GREP_COLORS
          set_color(flag_colors, "sl", color_sl); // selected line
          set_color(flag_colors, "cx", color_cx); // context line
          set_color(flag_colors, "mt", color_mt); // matched text in any line
          set_color(flag_colors, "ms", color_ms); // matched text in selected line
          set_color(flag_colors, "mc", color_mc); // matched text in a context line
          set_color(flag_colors, "fn", color_fn); // file name
          set_color(flag_colors, "ln", color_ln); // line number
          set_color(flag_colors, "cn", color_cn); // column number
          set_color(flag_colors, "bn", color_bn); // byte offset
          set_color(flag_colors, "se", color_se); // separator

          // -v: if rv in GREP_COLORS then swap the sl and cx colors
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

          color_off = "\033[m";
          color_del = "\033[K";

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
    flag_sort_rev = *flag_sort == 'r';

    if (strcmp(flag_sort, "name") == 0 || strcmp(flag_sort, "rname") == 0)
      flag_sort_key = Sort::NAME;
    else if (strcmp(flag_sort, "best") == 0 || strcmp(flag_sort, "rbest") == 0)
      flag_sort_key = Sort::BEST;
    else if (strcmp(flag_sort, "size") == 0 || strcmp(flag_sort, "rsize") == 0)
      flag_sort_key = Sort::SIZE;
    else if (strcmp(flag_sort, "used") == 0 || strcmp(flag_sort, "rused") == 0)
      flag_sort_key = Sort::USED;
    else if (strcmp(flag_sort, "changed") == 0 || strcmp(flag_sort, "rchanged") == 0)
      flag_sort_key = Sort::CHANGED;
    else if (strcmp(flag_sort, "created") == 0 || strcmp(flag_sort, "rcreated") == 0)
      flag_sort_key = Sort::CREATED;
    else
      usage("invalid argument --sort=KEY, valid arguments are 'name', 'best', 'size', 'used', 'changed', 'created', 'rname', 'rbest', 'rsize', 'rused', 'rchanged', and 'rcreated'");
  }

  // -x: enable -Y
  if (flag_line_regexp)
    flag_empty = true;

  // the regex compiled from PATTERN, -e PATTERN, -N PATTERN, and -f FILE
  std::string regex;

  // either one PATTERN or one or more -e PATTERN (cannot specify both)
  std::vector<std::string> arg_patterns;
  if (arg_pattern != NULL)
    arg_patterns.emplace_back(arg_pattern);
  std::vector<std::string>& patterns = arg_pattern != NULL ? arg_patterns : flag_regexp;

  // combine all -e PATTERN into a single regex string for matching
  for (auto& pattern : patterns)
  {
    // empty PATTERN matches everything
    if (pattern.empty())
    {
      // pattern ".*\n?|" could be used throughout without flag_empty = true, but this garbles output for -o, -H, -n, etc.
      if (flag_line_regexp)
      {
        // -x: with an empty pattern means we're matching empty lines
        regex.append("^$|");
      }
      else if (flag_hex)
      {
        // we're matching everything including \n
        regex.append(".*\n?|");

        // indicate that we're matching everything
        flag_match = true;
      }
      else
      {
        // we're matching lines
        regex.append("^.*$|");

        // indicate that we're matching everything
        flag_match = true;
      }

      // enable -Y: include empty pattern matches in the results
      flag_empty = true;

      // disable -w
      flag_word_regexp = false;
    }
    else
    {
      append(pattern, regex);

      // if pattern starts with ^ or ends with $, enable -Y
      if (pattern.size() >= 1 && (pattern.front() == '^' || pattern.back() == '$'))
        flag_empty = true;
    }
  }

  // --match: enable -Y because wee match everything, including empty lines
  if (flag_match)
    flag_empty = true;

  // --match: adjust color highlighting to show matches as selected lines
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

  // the regex compiled from -N PATTERN
  std::string neg_regex;

  // append -N PATTERN to neg_regex
  for (auto& pattern : flag_neg_regexp)
    if (!pattern.empty())
      append(pattern, neg_regex);

  // --not: combine --not regexes with regex patterns using the formula PATTERNS(?^.*(NOTPATTERNS))?|(?^NOTPATTERNS).*
  if (!regex.empty() && !flag_not_regexp.empty())
  {
    std::string not_regex;

    // append --not -e PATTERNs to not_regex
    for (auto& pattern : flag_not_regexp)
      if (!pattern.empty())
        append(pattern, not_regex);

    if (!not_regex.empty())
    {
      if (flag_line_regexp)
      {
        // --not and -x: --not -x -e PATTERN is the same as -N PATTERN
        neg_regex.append(not_regex);
      }
      else if (flag_word_regexp)
      {
        // --not and -w: --not -w -e PATTERN is the same as -N '.*PATTERN.*' which is slow
        not_regex.pop_back();
        neg_regex.append(".*(").append(not_regex).append(").*|");
      }
      else
      {
        // --not: optimize -e PATTERN --not -e NOTPATTERN as PATTERN(?^.*NOTPATTERN)?|(?^NOTPATTERN).*
        regex.pop_back();
        not_regex.pop_back();

        regex.append("(?^.*(").append(not_regex).append("))?|(?^").append(not_regex).append(").*|");
      }
    }
  }

  // construct negative (?^PATTERN)
  if (!neg_regex.empty())
  {
    neg_regex.pop_back();
    neg_regex.insert(0, "(?^").append(")|");
  }

  // combine regex and neg_regex patterns
  if (regex.empty())
    regex.swap(neg_regex);
  else if (!neg_regex.empty())
    regex.append(neg_regex);

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
  }

  // -f: get patterns from file
  if (!flag_file.empty())
  {
    bool line_regexp = flag_line_regexp;
    bool word_regexp = flag_word_regexp;

    // -F: make newline-separated lines in regex literal with \Q and \E
    const char *Q = flag_fixed_strings ? "\\Q" : "";
    const char *E = flag_fixed_strings ? "\\E|" : "|";

    // PATTERN or -e PATTERN: add an ending '|' to the regex to concatenate sub-expressions
    if (!regex.empty())
    {
      regex.push_back('|');

      // -x and -w do not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
      line_regexp = false;
      word_regexp = false;

      // -F does not apply to patterns in -f FILE when PATTERN or -e PATTERN is specified
      Q = "";
      E = "|";
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

    // remove the ending '|' from the |-concatenated regexes in the regex string
    regex.pop_back();

    // -x or -w: if no PATTERN is specified, then apply -x or -w to -f FILE patterns
    if (line_regexp)
      regex.insert(0, "^(").append(")$"); // make the regex line-anchored
    else if (word_regexp)
      regex.insert(0, "\\<(").append(")\\>"); // make the regex word-anchored
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

  // -y: disable -A, -B, and -C
  if (flag_any_line)
    flag_after_context = flag_before_context = 0;

  // -A, -B, or -C: disable -o
  if (flag_after_context > 0 || flag_before_context > 0)
    flag_only_matching = false;

  // -v or -y: disable -o and -u
  if (flag_invert_match || flag_any_line)
    flag_only_matching = flag_ungroup = false;

  // --depth: if -R or -r is not specified then enable -R 
  if ((flag_min_depth > 0 || flag_max_depth > 0) && flag_directories_action != Action::RECURSE)
  {
    flag_directories_action = Action::RECURSE;
    flag_dereference = true;
  }

  // -p (--no-dereference) and -S (--dereference): -p takes priority over -S and -R
  if (flag_no_dereference)
    flag_dereference = false;

  // display file name if more than one input file is specified or options -R, -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (flag_all_threads || flag_directories_action == Action::RECURSE || arg_files.size() > 1 || (flag_stdin && !arg_files.empty())))
    flag_with_filename = true;

  // --only-line-number implies -n
  if (flag_only_line_number)
    flag_line_number = true;

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

  // inverted character classes do not match newlines, e.g. [^x] matches anything except x and \n
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

  // --free-space: this is needed to check free-space conformance by the converter
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

  // --tabs: set reflex::Matcher option T to NUM (1, 2, 4, or 8) tab size
  if (flag_tabs)
    matcher_options.append("T=").push_back(static_cast<char>(flag_tabs) + '0');

  // --format-begin
  if (!flag_quiet && flag_format_begin != NULL)
    format(flag_format_begin, 0);

  size_t nodes = 0;
  size_t edges = 0;
  size_t words = 0;
  size_t nodes_time = 0;
  size_t edges_time = 0;
  size_t words_time = 0;

  // -P: Perl matching with Boost.Regex
  if (flag_perl_regexp)
  {
#if defined(HAVE_PCRE2)
    // construct the PCRE2 JIT-optimized NFA-based Perl pattern matcher
    std::string pattern(reflex::PCRE2Matcher::convert(regex, convert_flags));
    reflex::PCRE2Matcher matcher(pattern, reflex::Input(), matcher_options.c_str());

    if (threads > 1)
    {
      GrepMaster grep(output, &matcher);
      grep.ugrep();
    }
    else
    {
      Grep grep(output, &matcher);
      set_grep_handle(&grep);
      grep.ugrep();
      clear_grep_handle();
    }
#elif defined(HAVE_BOOST_REGEX)
    try
    {
      // construct the Boost.Regex NFA-based Perl pattern matcher
      std::string pattern(reflex::BoostPerlMatcher::convert(regex, convert_flags));
      reflex::BoostPerlMatcher matcher(pattern, reflex::Input(), matcher_options.c_str());

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher);
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

      throw reflex::regex_error(code, regex, error.position() + 1);
    }
#endif
  }
  else
  {
    // construct the RE/flex DFA-based pattern matcher and start matching files
    reflex::Pattern pattern(reflex::Matcher::convert(regex, convert_flags), "r");

    if (flag_fuzzy > 0)
    {
      reflex::FuzzyMatcher matcher(pattern, static_cast<uint16_t>(flag_fuzzy), reflex::Input(), matcher_options.c_str());

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher);
        set_grep_handle(&grep);
        grep.ugrep();
        clear_grep_handle();
      }
    }
    else
    {
      reflex::Matcher matcher(pattern, reflex::Input(), matcher_options.c_str());

      if (threads > 1)
      {
        GrepMaster grep(output, &matcher);
        grep.ugrep();
      }
      else
      {
        Grep grep(output, &matcher);
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
  if (!flag_quiet && flag_format_end != NULL)
    format(flag_format_end, Stats::found_parts());

  // --stats: display stats when we're done
  if (flag_stats != NULL)
  {
    Stats::report();

    if (strcmp(flag_stats, "vm") == 0 && words > 0)
      fprintf(output, "VM memory: %zu nodes (%zums), %zu edges (%zums), %zu opcode words (%zums)\n", nodes, nodes_time, edges, edges_time, words, words_time);
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
  if (!flag_stdin && arg_files.empty())
  {
    recurse(1, ".");
  }
  else
  {
    // read each input file to find pattern matches
    if (flag_stdin)
    {
      Stats::score_file();

      // search standard input
      search(NULL);
    }

#ifndef OS_WIN
    std::pair<std::set<ino_t>::iterator,bool> vino;
#endif

    for (const auto pathname : arg_files)
    {
      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof)
        break;

      // search file or directory, get the basename from the file argument first
      const char *basename = strrchr(pathname, PATHSEPCHR);
      if (basename != NULL)
        ++basename;
      else
        basename = pathname;

      ino_t inode = 0;
      uint64_t info;

      // search file, unless searchable directory into which we should recurse
      switch (select(1, pathname, basename, DIRENT_TYPE_UNKNOWN, inode, info, true))
      {
        case Type::DIRECTORY:
#ifndef OS_WIN
          if (flag_dereference)
            vino = visited.insert(inode);
#endif

          recurse(1, pathname);

#ifndef OS_WIN
          if (flag_dereference)
            visited.erase(vino.first);
#endif
          break;

        case Type::OTHER:
          search(pathname);
          break;

        case Type::SKIP:
          break;
      }
    }
  }
}

// search file or directory for pattern matches
Grep::Type Grep::select(size_t level, const char *pathname, const char *basename, int type, ino_t& inode, uint64_t& info, bool is_argument)
{
  if (*basename == '.' && !flag_hidden && !is_argument)
    return Type::SKIP;

#ifdef OS_WIN

  std::wstring wpathname = utf8_decode(pathname);
  DWORD attr = GetFileAttributesW(wpathname.c_str());

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

      if (fopenw_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
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

  // if dir entry is unknown, use lstat() to check if pathname is a symlink
  if (type != DIRENT_TYPE_UNKNOWN || lstat(pathname, &buf) == 0)
  {
    // symlinks are followed when specified on the command line (unless option -p) or with options -R, -S, --dereference
    if ((is_argument && !flag_no_dereference) || flag_dereference || (type != DIRENT_TYPE_UNKNOWN ? type != DIRENT_TYPE_LNK : !S_ISLNK(buf.st_mode)))
    {
      // if we got a symlink, use stat() to check if pathname is a directory or a regular file, we also stat when sorting by stat info
      if (((flag_sort_key == Sort::NA || flag_sort_key == Sort::NAME) && type != DIRENT_TYPE_UNKNOWN && type != DIRENT_TYPE_LNK) || stat(pathname, &buf) == 0)
      {
        // check if directory
        if (type == DIRENT_TYPE_DIR || ((type == DIRENT_TYPE_UNKNOWN || type == DIRENT_TYPE_LNK) && S_ISDIR(buf.st_mode)))
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
        else if (type == DIRENT_TYPE_REG ? !is_output(inode) : (type == DIRENT_TYPE_UNKNOWN || type == DIRENT_TYPE_LNK) && S_ISREG(buf.st_mode) ? !is_output(buf.st_ino) : flag_devices_action == Action::READ)
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

            if (fopenw_s(&file, pathname, (flag_binary || flag_decompress ? "rb" : "r")) != 0)
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
    warning(NULL, pathname);
  }

#endif

  return Type::SKIP;
}

// recurse over directory, searching for pattern matches in files and subdirectories
void Grep::recurse(size_t level, const char *pathname)
{
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
  // std::vector<std::string> *save_exclude = NULL, *save_exclude_dir = NULL, *save_not_exclude = NULL, *save_not_exclude_dir = NULL;
  std::unique_ptr<std::vector<std::string>> save_all_exclude, save_all_exclude_dir;
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
          save_all_exclude = std::unique_ptr<std::vector<std::string>>(new std::vector<std::string>);
          save_all_exclude->swap(flag_all_exclude);
          save_all_exclude_dir = std::unique_ptr<std::vector<std::string>>(new std::vector<std::string>);
          save_all_exclude_dir->swap(flag_all_exclude_dir);

          saved = true;
        }

        Stats::ignore_file(filename);
        split_globs(file, flag_all_exclude, flag_all_exclude_dir);
        fclose(file);
      }
    }
  }

  Stats::score_dir();

  std::vector<Entry> content;
  std::vector<Entry> subdirs;
  std::string dirpathname;

#ifdef OS_WIN

  std::string cFileName;

  do
  {
    cFileName.assign(utf8_encode(ffd.cFileName));

    // search directory entries that aren't . or .. or hidden when --no-hidden is enabled
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
          subdirs.emplace_back(dirpathname, 0, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(dirpathname.c_str());
          else
            content.emplace_back(dirpathname, 0, info);
          break;

        case Type::SKIP:
          break;
      }

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof)
        break;
    }
  } while (FindNextFileW(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  struct dirent *dirent = NULL;

  while ((dirent = readdir(dir)) != NULL)
  {
    // search directory entries that aren't . or .. or hidden when --no-hidden is enabled
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

      switch (type)
      {
        case Type::DIRECTORY:
          subdirs.emplace_back(dirpathname, inode, info);
          break;

        case Type::OTHER:
          if (flag_sort_key == Sort::NA)
            search(dirpathname.c_str());
          else
            content.emplace_back(dirpathname, inode, info);
          break;

        case Type::SKIP:
          break;
      }

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof)
        break;
    }
  }

  closedir(dir);

#endif

  // -Z and --sort=best: presearch the selected files to determine edit distance cost
  if (flag_fuzzy > 0 && flag_sort_key == Sort::BEST)
  {
    auto entry = content.begin();
    while (entry != content.end())
    {
      entry->cost = cost(entry->pathname.c_str());

      // if a file has no match, remove it
      if (entry->cost == 65535)
        entry = content.erase(entry);
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
        std::sort(content.begin(), content.end(), Entry::rev_comp_by_path);
      else
        std::sort(content.begin(), content.end(), Entry::comp_by_path);
    }
    else if (flag_sort_key == Sort::BEST)
    {
      if (flag_sort_rev)
        std::sort(content.begin(), content.end(), Entry::rev_comp_by_best);
      else
        std::sort(content.begin(), content.end(), Entry::comp_by_best);
    }
    else
    {
      if (flag_sort_rev)
        std::sort(content.begin(), content.end(), Entry::rev_comp_by_info);
      else
        std::sort(content.begin(), content.end(), Entry::comp_by_info);
    }

    // search the select sorted non-directory entries
    for (const auto& entry : content)
    {
      search(entry.pathname.c_str());

      // stop after finding max-files matching files
      if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
        break;

      // stop when output is blocked or search cancelled
      if (out.eof)
        break;
    }
  }

  // --sort: sort the selected subdirectory entries
  if (flag_sort_key != Sort::NA)
  {
    if (flag_sort_key == Sort::NAME || flag_sort_key == Sort::BEST)
    {
      if (flag_sort_rev)
        std::sort(subdirs.begin(), subdirs.end(), Entry::rev_comp_by_path);
      else
        std::sort(subdirs.begin(), subdirs.end(), Entry::comp_by_path);
    }
    else
    {
      if (flag_sort_rev)
        std::sort(subdirs.begin(), subdirs.end(), Entry::rev_comp_by_info);
      else
        std::sort(subdirs.begin(), subdirs.end(), Entry::comp_by_info);
    }
  }

  // recurse into the selected subdirectories
  for (const auto& entry : subdirs)
  {
    // stop after finding max-files matching files
    if (flag_max_files > 0 && Stats::found_parts() >= flag_max_files)
      break;

    // stop when output is blocked or search cancelled
    if (out.eof)
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

  // --ignore-files: restore if changed
  if (saved)
  {
    save_all_exclude->swap(flag_all_exclude);
    save_all_exclude_dir->swap(flag_all_exclude_dir);
  }
}

// -Z and --sort=best: perform a presearch to determine edit distance cost, returns 65535 when no match is found
uint16_t Grep::cost(const char *pathname)
{
  // stop when output is blocked
  if (out.eof)
    return 0;

  // open (archive or compressed) file (pathname is NULL to read stdin), return on failure
  if (!open_file(pathname))
    return 0;

  // -Z: matcher is a FuzzyMatcher
  reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);

  uint16_t cost = 65535;

  // -z: loop over extracted archive parts, when applicable
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

    // close file or -z: loop over next extracted archive parts, when applicable
  } while (close_file(pathname));

  return cost;
}

// search input and display pattern matches
void Grep::search(const char *pathname)
{
  // stop when output is blocked
  if (out.eof)
    return;

  // open (archive or compressed) file (pathname is NULL to read stdin), return on failure
  if (!open_file(pathname))
    return;

  // pathname is NULL when stdin is searched
  if (pathname == NULL)
    pathname = flag_label;

  bool matched = false;

  // -z: loop over extracted archive parts, when applicable
  do
  {
    try
    {
      size_t matches = 0;

      if (flag_quiet || flag_files_with_matches)
      {
        // option -q, -l, or -L

        if (!init_read())
          goto exit_search;

        matches = matcher->find() != 0;

        // --range: max line exceeded?
        if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
          matches = 0;

        // -v: invert
        if (flag_invert_match)
          matches = !matches;

        if (matches > 0)
        {
          // --format-open or format-close: we must acquire lock early before Stats::found_part()
          if (flag_files_with_matches && (flag_format_open != NULL || flag_format_close != NULL))
            out.acquire();

          // --max-files: max reached?
          if (!Stats::found_part())
            goto exit_search;

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
        // option -c

        if (init_read())
        {
          if (flag_ungroup || flag_only_matching)
          {
            // -co or -cu: count the number of patterns matched in the file

            while (matcher->find())
            {
              // --range: max line exceeded?
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

            while (matcher->find())
            {
              size_t current_lineno = matcher->lineno();

              if (lineno != current_lineno)
              {
                // --range: max line exceeded?
                if (flag_max_line > 0 && current_lineno > flag_max_line)
                  break;

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
        }

        // --format-open or --format-close: we must acquire lock early before Stats::found_part()
        if (flag_format_open != NULL || flag_format_close != NULL)
          out.acquire();

        // --max-files: max reached?
        if (matches > 0 || flag_format_open != NULL || flag_format_close != NULL)
          if (!Stats::found_part())
            goto exit_search;

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
        // option --format

        if (!init_read())
          goto exit_search;

        while (matcher->find())
        {
          // --range: max line exceeded?
          if (flag_max_line > 0 && matcher->lineno() > flag_max_line)
            break;

          // output --format-open
          if (matches == 0)
          {
            // --format-open or --format-close: we must acquire lock early before Stats::found_part()
            if (flag_format_open != NULL || flag_format_close != NULL)
              out.acquire();

            // --max-files: max reached?
            if (!Stats::found_part())
              goto exit_search;

            if (flag_format_open != NULL)
              out.format(flag_format_open, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
          }

          ++matches;

          // output --format
          out.format(flag_format, pathname, partname, matches, matcher, matches > 1, matches > 1);

          // -m: max number of matches reached?
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;

          out.check_flush();
        }

        // output --format-close
        if (matches > 0 && flag_format_close != NULL)
          out.format(flag_format_close, pathname, partname, Stats::found_parts(), matcher, false, Stats::found_parts() > 1);
      }
      else if (flag_only_line_number)
      {
        // option --only-line-number

        if (!init_read())
          goto exit_search;

        size_t lineno = 0;
        const char *separator = flag_separator;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          separator = lineno != current_lineno ? flag_separator : "+";

          if (lineno != current_lineno || flag_ungroup)
          {
            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (matches == 0 && !Stats::found_part())
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
        // option -o

        if (!init_read())
          goto exit_search;

        size_t lineno = 0;
        bool hex = false;
        bool nl = false;

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

          if (lineno != current_lineno || flag_ungroup)
          {
            if (nl)
            {
              out.nl();
              nl = false;
            }

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (matches == 0 && !Stats::found_part())
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

            if (!flag_no_header)
            {
              const char *separator = lineno != current_lineno ? flag_separator : "+";
              out.header(pathname, partname, current_lineno, matcher, matcher->first(), separator, binary);
            }

            lineno = current_lineno;
          }

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

              goto done_search;
            }

            lineno += matcher->lines() - 1;
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
              size -= from[size - 1] == '\n';
              if (size > 0)
              {
                out.str(match_ms);
                out.str(from, size);
                out.str(match_off);
              }
              out.nl();
            }
            else
            {
              nl = true;
            }
          }
        }

        if (nl)
          out.nl();

        if (hex)
          out.dump.done();
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line && !flag_invert_match)
      {
        // options -A, -B, -C, -y, -v are not specified

        if (!init_read())
          goto exit_search;

        size_t lineno = 0;
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
                out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                if (restline_size > 0)
                {
                  restline_size -= restline_data[restline_size - 1] == '\n';
                  if (restline_size > 0)
                  {
                    out.str(color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl();
              }

              restline_data = NULL;
            }

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (matches == 0 && !Stats::found_part())
              goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binary && !flag_hex && !flag_with_hex)
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

            ++matches;

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

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  eol -= end[eol - end - 1] == '\n';
                  if (eol > end)
                  {
                    out.str(color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl();
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
            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || flag_apply_color)
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
                        eol -= end[eol - end - 1] == '\n';
                        if (eol > end)
                        {
                          out.str(color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl();
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

        if (restline_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            if (restline_size > 0)
            {
              restline_size -= restline_data[restline_size - 1] == '\n';
              if (restline_size > 0)
              {
                out.str(color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl();
          }

          restline_data = NULL;
        }

        if (binary)
          out.dump.done();
      }
      else if (flag_before_context == 0 && flag_after_context == 0 && !flag_any_line)
      {
        // option -v without -A, -B, -C, -y

        if (!init_read())
          goto exit_search;

        // InvertMatchHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // construct event handler functor with captured *this and some of the locals
        InvertMatchGrepHandler invert_match_handler(*this, pathname, lineno, hex, binary, matches, stop);

        // register a event handler to display non-matching lines
        matcher->set_handler(&invert_match_handler);

        // to get the context from the invert_match handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();

          if (lineno != current_lineno)
          {
            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              invert_match_handler(*matcher, context.buf, context.len, context.num);

            if (binary && !flag_hex && !flag_with_hex)
              break;

            if (binary)
              out.dump.done();

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

        if (binary && !flag_hex && !flag_with_hex)
        {
          if (flag_binary_without_match)
            matches = 0;
          else if (matches > 0)
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }
      else if (flag_any_line)
      {
        // option -y

        if (!init_read())
          goto exit_search;

        // AnyLineGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        AnyLineGrepHandler any_line_handler(*this, pathname, lineno, hex, binary, matches, stop, restline_data, restline_size, restline_last);

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

          if (lineno != current_lineno || flag_ungroup)
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(v_hex_line, restline_last, restline_data, restline_size);
              }
              else
              {
                if (restline_size > 0)
                {
                  restline_size -= restline_data[restline_size - 1] == '\n';
                  if (restline_size > 0)
                  {
                    out.str(v_color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl();
              }

              restline_data = NULL;
            }

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              any_line_handler(*matcher, context.buf, context.len, context.num);

            if (binary && !flag_hex && !flag_with_hex)
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

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            // --max-files: max reached?
            if (!flag_invert_match && matches == 0 && !Stats::found_part())
              goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binary && !flag_hex && !flag_with_hex)
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

            if (!flag_invert_match)
              ++matches;

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

              if (flag_ungroup)
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

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  eol -= end[eol - end - 1] == '\n';
                  if (eol > end)
                  {
                    out.str(v_color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl();
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
            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || flag_apply_color)
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
                      out.dump.hex(v_hex_line, matcher->last(), end, eol - end);
                      out.dump.done();
                    }
                    else
                    {
                      if (eol > end)
                      {
                        eol -= end[eol - end - 1] == '\n';
                        if (eol > end)
                        {
                          out.str(v_color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl();
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

        if (restline_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(v_hex_line, restline_last, restline_data, restline_size);
          }
          else
          {
            if (restline_size > 0)
            {
              restline_size -= restline_data[restline_size - 1] == '\n';
              if (restline_size > 0)
              {
                out.str(v_color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl();
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          any_line_handler(*matcher, context.buf, context.len, context.num);

        if (binary && !flag_hex && !flag_with_hex)
        {
          if (flag_binary_without_match)
            matches = 0;
          else if (matches > 0)
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }
      else if (!flag_invert_match)
      {
        // options -A, -B, -C without -v

        if (!init_read())
          goto exit_search;

        // ContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        ContextGrepHandler context_handler(*this, pathname, lineno, hex, binary, matches, stop, restline_data, restline_size, restline_last);

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
                out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                if (restline_size > 0)
                {
                  restline_size -= restline_data[restline_size - 1] == '\n';
                  if (restline_size > 0)
                  {
                    out.str(color_sl);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl();
              }

              restline_data = NULL;
            }

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              context_handler(*matcher, context.buf, context.len, context.num);

            if (binary && !flag_hex && !flag_with_hex)
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

            context_handler.output_before_context();

            // --range: max line exceeded?
            if (flag_max_line > 0 && current_lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            // --max-files: max reached?
            if (!flag_invert_match && matches == 0 && !Stats::found_part())
              goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
            const char *bol = matcher->bol();

            binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

            if (binary && !flag_hex && !flag_with_hex)
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

            if (!flag_invert_match)
              ++matches;

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

              if (flag_ungroup)
              {
                if (eol > end)
                {
                  eol -= end[eol - end - 1] == '\n';
                  if (eol > end)
                  {
                    out.str(color_sl);
                    out.str(end, eol - end);
                    out.str(color_off);
                  }
                  out.nl();
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
            size_t size = matcher->size();

            if (size > 0)
            {
              size_t lines = matcher->lines();

              if (lines > 1 || flag_apply_color)
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
                        eol -= end[eol - end - 1] == '\n';
                        if (eol > end)
                        {
                          out.str(color_sl);
                          out.str(end, eol - end);
                          out.str(color_off);
                        }
                        out.nl();
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
            out.dump.hex(Output::Dump::HEX_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            if (restline_size > 0)
            {
              restline_size -= restline_data[restline_size - 1] == '\n';
              if (restline_size > 0)
              {
                out.str(color_sl);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl();
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          context_handler(*matcher, context.buf, context.len, context.num);

        if (binary && !flag_hex && !flag_with_hex)
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

        if (!init_read())
          goto exit_search;

        // InvertContextGrepHandler requires lineno to be set precisely, i.e. after skipping --range lines
        size_t lineno = flag_min_line > 0 ? flag_min_line - 1 : 0;
        size_t last_lineno = 0;
        size_t after = flag_after_context;
        bool hex = false;
        bool binary = false;
        bool stop = false;

        // to display the rest of the matching line
        const char *restline_data = NULL;
        size_t restline_size = 0;
        size_t restline_last = 0;

        // construct event handler functor with captured *this and some of the locals
        InvertContextGrepHandler invert_context_handler(*this, pathname, lineno, hex, binary, matches, stop, restline_data, restline_size, restline_last);

        // register an event handler functor to display non-matching lines
        matcher->set_handler(&invert_context_handler);

        // to get the context from the any_line handler explicitly
        reflex::AbstractMatcher::Context context;

        while (matcher->find())
        {
          size_t current_lineno = matcher->lineno();
          size_t lines = matcher->lines();

          if (last_lineno + 1 >= current_lineno)
            after += lines;
          else if (last_lineno != current_lineno)
            after = 0;

          if (last_lineno != current_lineno)
          {
            if (restline_data != NULL)
            {
              if (binary)
              {
                out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, restline_size);
              }
              else
              {
                if (restline_size > 0)
                {
                  restline_size -= restline_data[restline_size - 1] == '\n';
                  if (restline_size > 0)
                  {
                    out.str(color_cx);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl();
              }

              restline_data = NULL;
            }

            // get the lines before the matched line
            context = matcher->before();

            if (context.len > 0)
              invert_context_handler(*matcher, context.buf, context.len, context.num);

            if (binary && !flag_hex && !flag_with_hex)
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

            lineno = current_lineno;

            // --range: max line exceeded?
            if (flag_max_line > 0 && lineno > flag_max_line)
              break;

            // --max-files: max reached?
            if (stop)
              goto exit_search;

            // --max-files: max reached?
            if (!flag_invert_match && matches == 0 && !Stats::found_part())
              goto exit_search;

            // -m: max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              break;

            // output blocked?
            if (out.eof)
              goto exit_search;

            if (after < flag_after_context)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol() and end()
              const char *bol = matcher->bol();

              binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

              if (binary && !flag_hex && !flag_with_hex)
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

                restline.assign(end, eol - end);
                restline_data = restline.c_str();
                restline_size = restline.size();
                restline_last = matcher->last();
              }
            }
            else if (flag_before_context > 0)
            {
              const char *eol = matcher->eol(true); // warning: call eol() before bol() and begin()
              const char *bol = matcher->bol();

              binary = flag_hex || (!flag_text && is_binary(bol, eol - bol));

              if (binary && !flag_hex && !flag_with_hex)
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
              if (lines > 1 || flag_apply_color)
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
                if (restline_size > 0)
                {
                  restline_size -= restline_data[restline_size - 1] == '\n';
                  if (restline_size > 0)
                  {
                    out.str(color_cx);
                    out.str(restline_data, restline_size);
                    out.str(color_off);
                  }
                }
                out.nl();
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

                if (binary && !flag_hex && !flag_with_hex)
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

          lineno = current_lineno + lines - 1;
          last_lineno = lineno;
        }

        if (restline_data != NULL)
        {
          if (binary)
          {
            out.dump.hex(Output::Dump::HEX_CONTEXT_LINE, restline_last, restline_data, restline_size);
          }
          else
          {
            if (restline_size > 0)
            {
              restline_size -= restline_data[restline_size - 1] == '\n';
              if (restline_size > 0)
              {
                out.str(color_cx);
                out.str(restline_data, restline_size);
                out.str(color_off);
              }
            }
            out.nl();
          }

          restline_data = NULL;
        }

        // get the remaining context
        context = matcher->after();

        if (context.len > 0)
          invert_context_handler(*matcher, context.buf, context.len, context.num);

        if (binary && !flag_hex && !flag_with_hex)
        {
          if (flag_binary_without_match)
            matches = 0;
          else if (matches > 0)
            out.binary_file_matches(pathname, partname);
        }

        if (binary)
          out.dump.done();
      }

done_search:

      // any matches in this file or archive?
      if (matches > 0)
        matched = true;

      // --break: add a line break when applicable
      if (flag_break && (matches > 0 || flag_any_line) && !flag_quiet && !flag_files_with_matches && !flag_count && flag_format == NULL)
        out.chr('\n');
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
void split_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs)
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

// append newline-separated regexes as alternations and apply \Q \E when option -F is specified
void append(std::string& pattern, std::string& regex)
{
  size_t from = 0;
  size_t to;

  const char *Q = "";
  const char *E = "|";

  // -F: replace all \E in pattern with \E\\E\Q (or perhaps \\EE\Q)
  if (flag_fixed_strings)
  {
    while ((to = pattern.find("\\E", from)) != std::string::npos)
    {
      pattern.insert(to + 2, "\\\\E\\Q");
      from = to + 7;
    }

    Q = "\\Q";
    E = "\\E|";
  }

  from = 0;

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
}

// convert GREP_COLORS and set the color substring to the ANSI SGR codes
void set_color(const char *colors, const char *parameter, char color[COLORLEN])
{
  if (colors != NULL)
  {
    const char *s = strstr(colors, parameter);

    // check if substring parameter is present in colors
    if (s != NULL && s[2] == '=')
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
void strtopos2(const char *string, size_t& pos1, size_t& pos2, const char *message, bool optional_first)
{
  char *rest = const_cast<char*>(string);
  if (*string != ',')
    pos1 = static_cast<size_t>(strtoull(string, &rest, 10));
  else
    pos1 = 0;
  if (*rest == ',')
    pos2 = static_cast<size_t>(strtoull(rest + 1, &rest, 10));
  else if (optional_first)
    pos2 = pos1, pos1 = 0;
  else
    pos2 = 0;
  if (rest == NULL || *rest != '\0' || (pos2 > 0 && pos1 > pos2))
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
        if (max == 0 || max > 255 || rest == NULL || *rest != '\0')
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
    std::cerr << ", did you mean " << valid << "?";
  std::cerr << std::endl;
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
            Print NUM lines of trailing context after matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            options -B, -C, and -y.\n\
    -a, --text\n\
            Process a binary file as if it were text.  This is equivalent to\n\
            the --binary-files=text option.  This option might output binary\n\
            garbage to the terminal, which can have problematic consequences if\n\
            the terminal driver interprets some of it as commands.\n\
    -B NUM, --before-context=NUM\n\
            Print NUM lines of leading context before matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            options -A, -C, and -y.\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched line is displayed in front of the\n\
            respective matched line.  If -u is specified, displays the offset\n\
            for each pattern matched on the same line.  Byte offsets are exact\n\
            for ASCII, UTF-8, and raw binary input.  Otherwise, the byte offset\n\
            in the UTF-8 normalized input is displayed.\n\
    --binary-files=TYPE\n\
            Controls searching and reporting pattern matches in binary files.\n\
            TYPE can be `binary', `without-match`, `text`, `hex`, and\n\
            `with-hex'.  The default is `binary' to search binary files and to\n\
            report a match without displaying the match.  `without-match'\n\
            ignores binary matches.  `text' treats all binary files as text,\n\
            which might output binary garbage to the terminal, which can have\n\
            problematic consequences if the terminal driver interprets some of\n\
            it as commands.  `hex' reports all matches in hexadecimal.\n\
            `with-hex' only reports binary matches in hexadecimal, leaving text\n\
            matches alone.  A match is considered binary when matching a zero\n\
            byte or invalid UTF.  Short options are -a, -I, -U, -W, and -X.\n\
    --break\n\
            Adds a line break between results from different files.\n\
    -C NUM, --context=NUM\n\
            Print NUM lines of leading and trailing context surrounding each\n\
            match.  Places a --group-separator between contiguous groups of\n\
            matches.  See also options -A, -B, and -y.\n\
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
            (invert), `u' (underline).  Selectively overrides GREP_COLORS.\n\
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
    --depth=[MIN,][MAX], -1, -2 ... -9, --10, --11 ...\n\
            Restrict recursive searches from MIN to MAX directory levels deep,\n\
            where -1 (--depth=1) searches the specified path without recursing\n\
            into subdirectories.  Note that -3 -5, -3-5, or -35 searches 3 to 5\n\
            levels deep.  Enables -R if -R or -r is not specified.\n\
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
            The encoding format of the input, where ENCODING can be:";
  for (int i = 0; encoding_table[i].format != NULL; ++i)
    out << (i == 0 ? "" : ",") << (i % 4 ? " " : "\n            ") << "`" << encoding_table[i].format << "'";
  out << ".\n\
    --exclude=GLOB\n\
            Skip files whose name matches GLOB using wildcard matching, same as\n\
            -g ^GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  When GLOB\n\
            contains a `/', full pathnames are matched.  Otherwise basenames\n\
            are matched.  When GLOB ends with a `/', directories are excluded\n\
            as if --exclude-dir is specified.  Otherwise files are excluded.\n\
            Note that --exclude patterns take priority over --include patterns.\n\
            GLOB should be quoted to prevent shell globbing.  This option may\n\
            be repeated.\n\
    --exclude-dir=GLOB\n\
            Exclude directories whose name matches GLOB from recursive\n\
            searches.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  When GLOB\n\
            contains a `/', full pathnames are matched.  Otherwise basenames\n\
            are matched.  Note that --exclude-dir patterns take priority over\n\
            --include-dir patterns.  GLOB should be quoted to prevent shell\n\
            globbing.  This option may be repeated.\n\
    --exclude-from=FILE\n\
            Read the globs from FILE and skip files and directories whose name\n\
            matches one or more globs (as if specified by --exclude and\n\
            --exclude-dir).  Lines starting with a `#' and empty lines in FILE\n\
            are ignored.  When FILE is a `-', standard input is read.  This\n\
            option may be repeated.\n\
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
            option does not apply to -f FILE patterns to allow -f FILE patterns\n\
            to narrow or widen the PATTERN search.\n\
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
    --filter-magic-label=LABEL:MAGIC\n\
            Associate LABEL with files whose signature \"magic bytes\" match the\n\
            MAGIC regex pattern.  Only files that have no filename extension\n\
            are labeled, unless +LABEL is specified.  When LABEL matches an\n\
            extension specified in --filter=COMMANDS, the corresponding command\n\
            is invoked.  This option may be repeated.\n"
#endif
            "\
    --format=FORMAT\n\
            Output FORMAT-formatted matches.  For example `--format=%f:%n:%O%~'\n\
            outputs matching lines `%O' with filename `%f` and line number `%n'\n\
            followed by a newline `%~'. See `man ugrep' section FORMAT.\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression, i.e. make ugrep\n\
            behave as traditional grep.\n\
    -g GLOBS, --glob=GLOBS\n\
            Search only files whose name matches the specified comma-separated\n\
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.\n\
            When a `glob' is preceded by a `!' or a `^', skip files whose name\n\
            matches `glob', same as --exclude='glob'.  When `glob' contains a\n\
            `/', full pathnames are matched.  Otherwise basenames are matched.\n\
            When `glob' ends with a `/', directories are matched, same as\n\
            --include-dir='glob' and --exclude-dir='glob'.  This option may be\n\
            repeated and may be combined with options -M, -O and -t to expand\n\
            the recursive search.\n\
    --group-separator[=SEP]\n\
            Use SEP as a group separator for context options -A, -B, and -C.\n\
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
    --hexdump=[1-8][b][c][h]\n\
            Output matches in 1 to 8 columns of 8 hexadecimal octets.  The\n\
            default is 2 columns or 16 octets per line.  Option `b' removes all\n\
            space breaks, `c' removes the character column, and `h' removes the\n\
            hex spacing.  Enables -X if -W or -X is not specified.\n\
    --hidden, -.\n\
            Search "
#ifdef OS_WIN
            "Windows system and "
#endif
            "hidden files and directories.\n\
    -I, --ignore-binary\n\
            Ignore matches in binary files.  This option is equivalent to the\n\
            --binary-files=without-match option.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching.  By default, ugrep is case\n\
            sensitive.  This option applies to ASCII letters only.\n\
    --ignore-files[=FILE]\n\
            Ignore files and directories matching the globs in each FILE that\n\
            is encountered in recursive searches.  The default FILE is\n\
            `" DEFAULT_IGNORE_FILE "'.  Matching files and directories located in the\n\
            directory of a FILE's location and in directories below are ignored\n\
            by temporarily overriding the --exclude and --exclude-dir globs.\n\
            Files and directories that are explicitly specified as command line\n\
            arguments are never ignored.  This option may be repeated.\n\
    --include=GLOB\n\
            Search only files whose name matches GLOB using wildcard matching,\n\
            same as -g GLOB.  GLOB can use **, *, ?, and [...] as wildcards,\n\
            and \\ to quote a wildcard or backslash character literally.  When\n\
            GLOB contains a `/', full pathnames are matched.  Otherwise\n\
            basenames are matched.  When GLOB ends with a `/', directories are\n\
            included as if --include-dir is specified.  Otherwise files are\n\
            included.  Note that --exclude patterns take priority over\n\
            --include patterns.  GLOB should be quoted to prevent shell\n\
            globbing.  This option may be repeated.\n\
    --include-dir=GLOB\n\
            Only directories whose name matches GLOB are included in recursive\n\
            searches.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  When GLOB\n\
            contains a `/', full pathnames are matched.  Otherwise basenames\n\
            are matched.  Note that --exclude-dir patterns take priority over\n\
            --include-dir patterns.  GLOB should be quoted to prevent shell\n\
            globbing.  This option may be repeated.\n\
    --include-from=FILE\n\
            Read the globs from FILE and search only files and directories\n\
            whose name matches one or more globs (as if specified by --include\n\
            and --include-dir).  Lines starting with a `#' and empty lines in\n\
            FILE are ignored.  When FILE is a `-', standard input is read.\n\
            This option may be repeated.\n\
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
            Perform case insensitive matching unless a pattern contains an\n\
            upper case letter.  This option applies to ASCII letters only.\n\
    --json\n\
            Output file matches in JSON.  If -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -K FIRST[,LAST], --range=FIRST[,LAST]\n\
            Start searching at line FIRST, stop at line LAST when specified.\n\
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
            where a file name would normally be printed in the output.  The\n\
            default value is `(standard input)'.\n\
    --line-buffered\n\
            Force output to be line buffered instead of block buffered.\n\
    -M MAGIC, --file-magic=MAGIC\n\
            Only files matching the signature pattern MAGIC are searched.  The\n\
            signature \"magic bytes\" at the start of a file are compared to\n\
            the MAGIC regex pattern.  When matching, the file will be searched.\n\
            When MAGIC is preceded by a `!' or a `^', skip files with matching\n\
            MAGIC signatures.  This option may be repeated and may be combined\n\
            with options -O and -t to expand the search.  Every file on the\n\
            search path is read, making searches potentially more expensive.\n\
    -m NUM, --max-count=NUM\n\
            Stop reading the input after NUM matches in each input file.\n\
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
            Specify a negative PATTERN used during the search of the input:\n\
            an input line is selected only if it matches any of the specified\n\
            patterns unless a subpattern of PATTERN.  Same as -e (?^PATTERN).\n\
            Negative PATTERN matches are essentially removed before any other\n\
            patterns are matched.  Note that longer patterns take precedence\n\
            over shorter patterns.  This option may be repeated.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --not\n\
            Specifies that the following one or more -e PATTERN should not\n\
            match selected lines, where --not -e PATTERN is the same as\n\
            specifying -N '.*PATTERN.*' but optimized to improve performance.\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B, and -C.\n\
    -O EXTENSIONS, --file-extension=EXTENSIONS\n\
            Search only files whose filename extensions match the specified\n\
            comma-separated list of EXTENSIONS, same as --include='*.ext' for\n\
            each `ext' in EXTENSIONS.  When an `ext' is preceded by a `!' or a\n\
            `^', skip files whose filename extensions matches `ext', same as\n\
            --exclude='*.ext'.  This option may be repeated and may be combined\n\
            with options -g, -M and -t to expand the recursive search.\n\
    -o, --only-matching\n\
            Print only the matching part of lines.  When multiple lines match,\n\
            the line numbers with option -n are displayed using `|' as the\n\
            field separator for each additional line matched by the pattern.\n\
            If -u is specified, ungroups multiple matches on the same line.\n\
            This option cannot be combined with options -A, -B, -C, -v, and -y.\n\
    --only-line-number\n\
            The line number of the matching line in the file is output without\n\
            displaying the match.  The line number counter is reset for each\n\
            file processed.\n\
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
    -p, --no-dereference\n\
            If -R or -r is specified, no symbolic links are followed, even when\n\
            they are specified on the command line.\n\
    --pager[=COMMAND]\n\
            When output is sent to the terminal, uses COMMAND to page through\n\
            the output.  The default COMMAND is `" DEFAULT_PAGER_COMMAND "'.  Enables --heading\n\
            and --line-buffered.\n\
    --pretty\n\
            When output is sent to a terminal, enables --color, --heading, -n,\n\
            --sort and -T when not explicitly disabled or set.\n\
    -Q[DELAY], --query[=DELAY]\n\
            Query mode: user interface to perform interactive searches.  This\n\
            mode requires an ANSI capable terminal.  An optional DELAY argument\n\
            may be specified to reduce or increase the response time to execute\n\
            searches after the last key press, in increments of 100ms, where\n\
            the default is 5 (0.5s delay).  No whitespace may be given between\n\
            -Q and its argument DELAY.  Initial patterns may be specified with\n\
            -e PATTERN, i.e. a PATTERN argument requires option -e.  Press F1\n\
            or CTRL-Z to view the help screen.  Press F2 or CTRL-Y to invoke an\n\
            editor to edit the file shown on screen.  The editor is taken from\n\
            the environment variable GREP_EDIT if defined, or EDITOR.  Press\n\
            Tab and Shift-Tab to navigate directories and to select a file to\n\
            search.  Press Enter to select lines to output.  Press ALT-l for\n\
            option -l to list files, ALT-n for -n, etc.  Non-option commands\n\
            include ALT-] to increase fuzziness and ALT-} to increase context.\n\
            Press F1 or CTRL-Z for more information.  Enables --heading.\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress all output.  ugrep will only search until a\n\
            match has been found.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory.  Follow all\n\
            symbolic links, unlike -r.  When -J1 is specified, files are\n\
            searched in the same order as specified.  Note that when no FILE\n\
            arguments are specified and input is read from a terminal,\n\
            recursive searches are performed as if -R is specified.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are on the command line.  When -J1 is specified,\n\
            files are searched in the same order as specified.\n\
    -S, --dereference\n\
            If -r is specified, all symbolic links are followed, like -R.  The\n\
            default is not to follow symbolic links.\n\
    -s, --no-messages\n\
            Silent mode: nonexistent and unreadable files are ignored, i.e.\n\
            their error messages are suppressed.\n\
    --save-config[=FILE]\n\
            Save configuration FILE.  By default `.ugrep' is saved.  If FILE is\n\
            a `-', write the configuration to standard output.\n\
    --separator[=SEP]\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset, and the matched line.  The default is a colon\n\
            (`:').\n\
    --sort[=KEY]\n\
            Displays matching files in the order specified by KEY in recursive\n\
            searches.  KEY can be `name' to sort by pathname (default), `best'\n\
            to sort by best match with option -Z (sort by best match requires\n\
            two passes over the input files), `size' to sort by file size,\n\
            `used' to sort by last access time, `changed' to sort by last\n\
            modification time, and `created' to sort by creation time.  Sorting\n\
            is reversed with `rname', `rbest', `rsize', `rused', `rchanged', or\n\
            `rcreated'.  Archive contents are not sorted.  Subdirectories are\n\
            sorted and displayed after matching files.  FILE arguments are\n\
            searched in the same order as specified.  Normally ugrep displays\n\
            matches in no particular order to improve performance.\n\
    --stats\n\
            Display statistics on the number of files and directories searched,\n\
            and the inclusion and exclusion constraints applied.\n\
    -T, --initial-tab\n\
            Add a tab space to separate the file name, line number, column\n\
            number, and byte offset with the matched line.\n\
    -t TYPES, --file-type=TYPES\n\
            Search only files associated with TYPES, a comma-separated list of\n\
            file types.  Each file type corresponds to a set of filename\n\
            extensions passed to option -O.  For capitalized file types, the\n\
            search is expanded to include files with matching file signature\n\
            magic bytes, as if passed to option -M.  When a type is preceded\n\
            by a `!' or a `^', excludes files of the specified type.  This\n\
            option may be repeated.  The possible file types can be (where\n\
            -tlist displays a detailed list):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    out << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  out << ".\n\
    --tabs[=NUM]\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1, 2, 4, or 8.  The default tab size is 8.\n\
    --tag[=TAG[,END]]\n\
            Disables colors to mark up matches with TAG.  END marks the end of\n\
            a match if specified, otherwise TAG.  The default is `___'.\n\
    -U, --binary\n"
#ifdef OS_WIN
            "\
            Opens files in binary mode (mode specific to Windows) and disables\n\
            Unicode matching for binary file matching, forcing PATTERN to match\n\
            bytes, not Unicode characters.  For example, -U '\\xa3' matches\n\
            byte A3 (hex) instead of the Unicode code point U+00A3 represented\n\
            by the two-byte UTF-8 sequence C2 A3.  Note that binary files\n\
            may appear truncated when searched without this option.\n"
#else
            "\
            Disables Unicode matching for binary file matching, forcing PATTERN\n\
            to match bytes, not Unicode characters.  For example, -U '\\xa3'\n\
            matches byte A3 (hex) instead of the Unicode code point U+00A3\n\
            represented by the two-byte UTF-8 sequence C2 A3.\n"
#endif
            "\
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
            The PATTERN is searched for as a word (as if surrounded by \\< and\n\
            \\>).  If a PATTERN is specified, or -e PATTERN or -N PATTERN, then\n\
            this option does not apply to -f FILE patterns to allow -f FILE\n\
            patterns to narrow or widen the PATTERN search.\n\
    -X, --hex\n\
            Output matches in hexadecimal.  This option is equivalent to the\n\
            --binary-files=hex option.  See also option --hexdump.\n\
    -x, --line-regexp\n\
            Only input lines selected against the entire PATTERN is considered\n\
            to be matching lines (as if surrounded by ^ and $).  If a PATTERN\n\
            is specified, or -e PATTERN or -N PATTERN, then this option does\n\
            not apply to -f FILE patterns to allow -f FILE patterns to narrow\n\
            or widen the PATTERN search.\n\
    --xml\n\
            Output file matches in XML.  If -H, -n, -k, or -b is specified,\n\
            additional values are output.  See also options --format and -u.\n\
    -Y, --empty\n\
            Permits empty matches.  By default, empty matches are disabled,\n\
            unless a pattern begins with `^' or ends with `$'.  With this\n\
            option, empty-matching pattern, such as x? and x*, match all input,\n\
            not only lines containing the character `x'.\n\
    -y, --any-line\n\
            Any matching or non-matching line is output.  Non-matching lines\n\
            are output with the `-' separator as context of the matching lines.\n\
            See also options -A, -B, and -C.\n\
    -Z[MAX], --fuzzy[=MAX]\n\
            Fuzzy mode: report approximate pattern matches within MAX errors.\n\
            By default, MAX is 1: one deletion, insertion or substitution is\n\
            allowed.  When `+' and/or `-' precede MAX, only insertions and/or\n\
            deletions are allowed, respectively.  When `~' precedes MAX,\n\
            substitution counts as one error.  For example, -Z+~3 allows up to\n\
            three insertions or substitutions, but no deletions.  The first\n\
            character of an approximate match always matches the begin of a\n\
            pattern.  Option --sort=best orders matching files by best match.\n\
            No whitespace may be given between -Z and its argument.\n\
    -z, --decompress\n\
            Decompress files to search, when compressed.  Archives (.cpio,\n\
            .pax, .tar, and .zip) and compressed archives (e.g. .taz, .tgz,\n\
            .tpz, .tbz, .tbz2, .tb2, .tz2, .tlz, and .txz) are searched and\n\
            matching pathnames of files in archives are output in braces.  If\n\
            -g, -O, -M, or -t is specified, searches files within archives\n\
            whose name matches globs, matches file name extensions, matches\n\
            file signature magic bytes, or matches file types, respectively.\n"
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
            ".\n"
#endif
            "\
    -0, --null\n\
            Prints a zero-byte (NUL) after the file name.  This option can be\n\
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
  if (what == NULL)
  {
    help(std::cout);
  }
  else
  {
    if (*what == '=')
      ++what;

    if (strncmp(what, "--no", 4) == 0)
      what += 4;

    if (*what == '\0')
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
                  std::cout << "\n\nOther options:\n";
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
    (reflex::have_HW_AVX512BW() ? " +avx512" : (reflex::have_HW_AVX2() ? " +avx2" : reflex::have_HW_SSE2() ?  " +sse2" : " (-avx512)")) <<
#elif defined(HAVE_AVX2)
    (reflex::have_HW_AVX2() ? " +avx2" : reflex::have_HW_SSE2() ?  " +sse2" : " (-avx2)") <<
#elif defined(HAVE_SSE2)
    (reflex::have_HW_SSE2() ?  " +sse2" : " (-sse2)") <<
#elif defined(HAVE_NEON)
    " +neon" <<
#endif
#if defined(HAVE_PCRE2)
    (pcre2_config(PCRE2_CONFIG_JIT, &tmp) >= 0 && tmp != 0 ? " +pcre2_jit" : " +pcre2") <<
#elif defined(HAVE_BOOST_REGEX)
    " +boost_regex" <<
#endif
#ifdef HAVE_LIBZ
    " +zlib" <<
#endif
#ifdef HAVE_LIBBZ2
    " +bzip2" <<
#endif
#ifdef HAVE_LIBLZMA
    " +lzma" <<
#endif
#ifdef HAVE_LIBLZ4
    " +lz4" <<
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
    fprintf(stderr, "%sugrep: %swarning:%s %scannot decompress %s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, pathname, color_off, color_message, message ? message : "", color_off);
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
    fprintf(stderr, "%sugrep: %swarning:%s %s%s%s%s:%s %s%s%s\n", color_off, color_warning, color_off, color_high, message ? message : "", message ? " " : "", arg ? arg : "", color_off, color_message, errmsg, color_off);
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
  fprintf(stderr, "%sugrep: %serror:%s %s%s%s%s:%s %s%s%s\n\n", color_off, color_error, color_off, color_high, message ? message : "", message ? " " : "", arg ? arg : "", color_off, color_message, errmsg, color_off);
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
  fprintf(stderr, "%sugrep: %s%s%s%s%s%s\n\n", color_off, color_error, message ? message : "", color_off, color_high, what.c_str(), color_off);
  exit(EXIT_ERROR);
}
