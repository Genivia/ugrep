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
@brief     Universal grep - high-performance Unicode file search utility
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Universal grep - high-performance universal search utility finds Unicode
patterns in UTF-8/16/32, ASCII, ISO-8859-1, EBCDIC, code pages 437, 850, 858,
1250 to 1258, and other file formats.

Download and installation:

  https://github.com/Genivia/ugrep

Requires:

  https://github.com/Genivia/RE-flex

Examples:

  # display the lines in places.txt that contain Unicode words
  ugrep '\w+' places.txt

  # display the lines in places.txt with capitalized Unicode words color-highlighted
  ugrep --color=auto '\w+' places.txt

  # list all capitalized Unicode words in places.txt
  ugrep -o '\w+' places.txt

  # list all laughing face emojis (Unicode code points U+1F600 to U+1F60F) in birthday.txt 
  ugrep -o '[😀-😏]' birthday.txt

  # list all laughing face emojis (Unicode code points U+1F600 to U+1F60F) in birthday.txt 
  ugrep -o '[\x{1F600}-\x{1F60F}]' birthday.txt

  # display lines containing the names Gödel (or Goedel), Escher, or Bach in GEB.txt and wiki.txt
  ugrep 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

  # display lines that do not contain the names Gödel (or Goedel), Escher, or Bach in GEB.txt and wiki.txt
  ugrep -v 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

  # count the number of lines containing the names Gödel (or Goedel), Escher, or Bach in GEB.txt and wiki.txt
  ugrep -c 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

  # count the number of occurrences of the names Gödel (or Goedel), Escher, or Bach in GEB.txt and wiki.txt
  ugrep -c -g 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

  # check if some.txt file contains any non-ASCII (i.e. Unicode) characters
  ugrep -q '[^[:ascii:]]' some.txt && echo "some.txt contains Unicode"

  # display word-anchored 'lorem' in UTF-16 formatted file utf16lorem.txt that contains a UTF-16 BOM
  ugrep -w -i 'lorem' utf16lorem.txt

  # display word-anchored 'lorem' in UTF-16 formatted file utf16lorem.txt that does not contain a UTF-16 BOM
  ugrep --file-format=UTF-16 -w -i 'lorem' utf16lorem.txt

  # list the lines to fix in a C/C++ source file by looking for the word FIXME while skipping any FIXME in quoted strings by using a negative pattern `(?^X)' to ignore quoted strings:
  ugrep -n -o -e 'FIXME' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*")' file.cpp

  # check if 'main' is defined in a C/C++ source file, skipping the word 'main' in comments and strings:
  ugrep -q -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/[*]([^*]|[*][^/])*[*]/)' file.cpp

Compile:

  c++ -std=c++11 -o ugrep ugrep.cpp -lreflex

Bugs FIXME:

  - Pattern '^$' does not match empty lines, because find() does not permit empty matches.
  - Back-references are not supported.

Wanted TODO:

  - Like grep, we want to traverse directory contents to search files, and support options -R and -r, --recursive.
  - Like grep, -A, -B, and -C, --context to display the context of a match.
  - Like grep, -f, --file=file to read patterns from a file.
  - Like grep, -T should insert tabs (followed by a backspace!) at columns.
  - Should we detect "binary files" like grep and skip them?
  - Should we open files in binary mode "rb" when --binary-files option is specified?
  - ... anything else?

*/

#include <reflex/matcher.h>

// check if we are on a windows OS
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

// windows has no isatty()
#ifdef OS_WIN
#define isatty(fildes) ((fildes) == 1)
#else
#include <unistd.h>
#endif

// ugrep version
#define VERSION "1.1.0"

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

// ANSI SGR substrings extracted from GREP_COLORS
#define SGR "\033["
std::string color_sl;
std::string color_cx;
std::string color_mt;
std::string color_ms;
std::string color_mc;
std::string color_fn;
std::string color_ln;
std::string color_cn;
std::string color_bn;
std::string color_se;
std::string color_off;

// ugrep command-line options
bool flag_with_filename       = false;
bool flag_no_filename         = false;
bool flag_no_group            = false;
bool flag_no_messages         = false;
bool flag_byte_offset         = false;
bool flag_count               = false;
bool flag_fixed_strings       = false;
bool flag_free_space          = false;
bool flag_ignore_case         = false;
bool flag_invert_match        = false;
bool flag_column_number       = false;
bool flag_line_number         = false;
bool flag_line_buffered       = false;
bool flag_only_matching       = false;
bool flag_quiet               = false;
bool flag_files_with_match    = false;
bool flag_files_without_match = false;
bool flag_null                = false;
bool flag_basic_regexp        = false;
bool flag_word_regexp         = false;
bool flag_line_regexp         = false;
bool flag_initial_tab         = false;
const char *flag_color        = NULL;
const char *flag_file_format  = NULL;
const char *flag_label        = "(standard input)";
size_t flag_max_count         = 0;
size_t flag_tabs              = 8;

// function protos
bool ugrep(reflex::Pattern& pattern, FILE *file, reflex::Input::file_encoding_type encoding, const char *infile);
void set_color(const char *grep_colors, const char *parameter, std::string& color);
void help(const char *message = NULL, const char *arg = NULL);
void version();

// table of file formats for ugrep option --file-format
const struct { const char *format; reflex::Input::file_encoding_type encoding; } format_table[] = {
  { "binary",     reflex::Input::file_encoding::plain   },
  { "ISO-8859-1", reflex::Input::file_encoding::latin   },
  { "ASCII",      reflex::Input::file_encoding::utf8    },
  { "EBCDIC",     reflex::Input::file_encoding::ebcdic  },
  { "UTF-8",      reflex::Input::file_encoding::utf8    },
  { "UTF-16",     reflex::Input::file_encoding::utf16le },
  { "UTF-16BE",   reflex::Input::file_encoding::utf16be },
  { "UTF-16LE",   reflex::Input::file_encoding::utf16le },
  { "UTF-32",     reflex::Input::file_encoding::utf32le },
  { "UTF-32BE",   reflex::Input::file_encoding::utf32be },
  { "UTF-32LE",   reflex::Input::file_encoding::utf32le },
  { "CP-437",     reflex::Input::file_encoding::cp437   },
  { "CP-850",     reflex::Input::file_encoding::cp850   },
  { "CP-855",     reflex::Input::file_encoding::cp850   },
  { "CP-1250",    reflex::Input::file_encoding::cp1250  },
  { "CP-1251",    reflex::Input::file_encoding::cp1251  },
  { "CP-1252",    reflex::Input::file_encoding::cp1252  },
  { "CP-1253",    reflex::Input::file_encoding::cp1253  },
  { "CP-1254",    reflex::Input::file_encoding::cp1254  },
  { "CP-1255",    reflex::Input::file_encoding::cp1255  },
  { "CP-1256",    reflex::Input::file_encoding::cp1256  },
  { "CP-1257",    reflex::Input::file_encoding::cp1257  },
  { "CP-1258",    reflex::Input::file_encoding::cp1258  },
  { NULL, 0 }
};

// ugrep main()
int main(int argc, char **argv)
{
  std::string regex;
  std::vector<const char*> infiles;

  // parse ugrep command-line options and arguments
  for (int i = 1; i < argc; ++i)
  {
    const char *arg = argv[i];

    if ((*arg == '-'
#ifdef OS_WIN
     || *arg == '/'
#endif
     ) && arg[1])
    {
      bool is_grouped = true;

      // parse a ugrep command-line option
      while (is_grouped && *++arg)
      {
        switch (*arg)
        {
          case '-':
            ++arg;
            if (strcmp(arg, "basic-regexp") == 0)
              flag_basic_regexp = true;
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
            else if (strcmp(arg, "count") == 0)
              flag_count = true;
            else if (strcmp(arg, "extended-regexp") == 0)
              ;
            else if (strncmp(arg, "file-format=", 12) == 0)
              flag_file_format = arg + 12;
            else if (strcmp(arg, "files-with-match") == 0)
              flag_files_with_match = true;
            else if (strcmp(arg, "files-without-match") == 0)
              flag_files_without_match = true;
            else if (strcmp(arg, "fixed-strings") == 0)
              flag_fixed_strings = true;
            else if (strcmp(arg, "free-space") == 0)
              flag_free_space = true;
            else if (strcmp(arg, "help") == 0)
              help();
            else if (strcmp(arg, "ignore-case") == 0)
              flag_ignore_case = true;
            else if (strcmp(arg, "initial-tab") == 0)
              flag_initial_tab = true;
            else if (strcmp(arg, "invert-match") == 0)
              flag_invert_match = true;
            else if (strcmp(arg, "label") == 0)
              flag_label = "";
            else if (strncmp(arg, "label=", 6) == 0)
              flag_label = arg + 6;
            else if (strcmp(arg, "line-number") == 0)
              flag_line_number = true;
            else if (strcmp(arg, "line-regexp") == 0)
              flag_line_regexp = true;
            else if (strncmp(arg, "max-count=", 10) == 0)
              flag_max_count = (size_t)strtoull(arg + 10, NULL, 10);
            else if (strcmp(arg, "no-filename") == 0)
              flag_no_filename = true;
            else if (strcmp(arg, "no-group") == 0)
              flag_no_group = true;
            else if (strcmp(arg, "no-messages") == 0)
              flag_no_messages = true;
            else if (strcmp(arg, "null") == 0)
              flag_null = true;
            else if (strcmp(arg, "only-matching") == 0)
              flag_only_matching = true;
            else if (strcmp(arg, "quiet") == 0 || strcmp(arg, "silent") == 0)
              flag_quiet = true;
            else if (strncmp(arg, "regexp=", 7) == 0)
              regex.append(arg + 7).push_back('|');
            else if (strncmp(arg, "tabs=", 5) == 0)
              flag_tabs = (size_t)strtoull(arg + 5, NULL, 10);
            else if (strcmp(arg, "version") == 0)
              version();
            else if (strcmp(arg, "with-filename") == 0)
              flag_with_filename = true;
            else if (strcmp(arg, "word-regexp") == 0)
              flag_word_regexp = true;
            else
              help("unknown option --", arg);
            is_grouped = false;
            break;

          case 'b':
            flag_byte_offset = true;
            break;

          case 'c':
            flag_count = true;
            break;

          case 'E':
            break;

          case 'e':
            ++arg;
            if (*arg)
              regex.append(&arg[*arg == '=']).push_back('|');
            else if (++i < argc)
              regex.append(argv[i]).push_back('|');
            else
              help("missing pattern for option -e");
            is_grouped = false;
            break;

          case 'F':
            flag_fixed_strings = true;
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

          case 'i':
            flag_ignore_case = true;
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
              flag_max_count = (size_t)strtoull(&arg[*arg == '='], NULL, 10);
            else if (++i < argc)
              flag_max_count = (size_t)strtoull(argv[i], NULL, 10);
            else
              help("missing number for option -m");
            is_grouped = false;
            break;

          case 'n':
            flag_line_number = true;
            break;

          case 'o':
            flag_only_matching = true;
            break;

          case 'q':
            flag_quiet = true;
            break;

          case 's':
            flag_no_messages = true;
            break;

          case 'T':
            flag_initial_tab = true;
            break;

          case 'V':
            version();
            break;

          case 'v':
            flag_invert_match = true;
            break;

          case 'w':
            flag_word_regexp = true;
            break;

          case 'x':
            flag_line_regexp = true;
            break;

          case 'Z':
            flag_null = true;
            break;

          case '?':
            help();

          default:
            help("unknown option -", arg);
        }
      }
    }
    else
    {
      // parse a ugrep command-line argument
      if (regex.empty())
        // no regex pattern specified yet, so assign it to the regex string
        regex.assign(arg).push_back('|');
      else
        // otherwise add the file argument to the list of files
        infiles.push_back(arg);
    }
  }

  // if no regex pattern was specified then exit
  if (regex.empty())
    help();

  // if no files were specified then read standard input
  if (infiles.empty())
    infiles.push_back("-");

  // remove the ending '|' from the |-concatenated regexes in the regex string
  regex.pop_back();

  if (regex.empty())
  {
    // if the specified regex is empty then it matches every line
    regex.assign(".*");
  }
  else
  {
    // if -F --fixed-strings: make regex literal with \Q and \E
    if (flag_fixed_strings)
      regex.insert(0, "\\Q").append("\\E");

    // if -w or -x: make the regex word- or line-anchored, respectively
    if (flag_word_regexp)
      regex.insert(0, "\\<(").append(")\\>");
    else if (flag_line_regexp)
      regex.insert(0, "^(").append(")$");
  }

  // if -v invert-match: options -g --no-group and -o --only-matching options cannot be used
  if (flag_invert_match)
  {
    flag_no_group = false;
    flag_only_matching = false;
  }

  // input and output are line-buffered if option -o --only-matching is not specified
  if (!flag_only_matching)
    flag_line_buffered = true;

  // display file name if more than one input file is specified and option -h --no-filename is not specified
  if (infiles.size() > 1 && !flag_no_filename)
    flag_with_filename = true;

  // (re)set flag_color depending on color_term and isatty()
  if (flag_color)
  {
    if (strcmp(flag_color, "never") == 0)
    {
      flag_color = NULL;
    }
    else if (strcmp(flag_color, "auto") == 0)
    {
      bool color_term = false;

#ifndef OS_WIN
      // check whether we have a color terminal
      const char *term = getenv("TERM");
      color_term = term && (strstr(term, "ansi") || strstr(term, "xterm") || strstr(term, "color"));
#endif

      if (!color_term || !isatty(1))
        flag_color = NULL;
    }
    else if (strcmp(flag_color, "always") != 0)
    {
      help("unknown --color=when value");
    }

    if (flag_color)
    {
      const char *grep_color = NULL;
      const char *grep_colors = NULL;

#ifndef OS_WIN
      // get GREP_COLOR and GREP_COLORS environment variables
      grep_color = getenv("GREP_COLOR");
      grep_colors = getenv("GREP_COLORS");
#endif

      if (grep_color)
        color_mt.assign(SGR).append(grep_color).push_back('m');
      else if (grep_colors == NULL)
        grep_colors = "mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36";

      if (grep_colors)
      {
        // parse GREP_COLORS

        set_color(grep_colors, "sl", color_sl); // selected line
        set_color(grep_colors, "cx", color_cx); // context line
        set_color(grep_colors, "mt", color_mt); // matching text in any line
        set_color(grep_colors, "ms", color_ms); // matching text in selected line
        set_color(grep_colors, "mc", color_mc); // matching text in a context line
        set_color(grep_colors, "fn", color_fn); // file name
        set_color(grep_colors, "ln", color_ln); // line number
        set_color(grep_colors, "cn", color_cn); // column number
        set_color(grep_colors, "bn", color_bn); // byte offset
        set_color(grep_colors, "se", color_se); // separators

        if (flag_invert_match && strstr(grep_colors, "rv"))
          color_sl.swap(color_cx);
      }

      // if ms= or mc= are not specified, use the mt= value
      if (color_ms.empty())
        color_ms = color_mt;
      if (color_mc.empty())
        color_mc = color_mt;

      color_off.assign(SGR "0m");
    }
  }

  // if any match was found in any of the input files then we set found==true
  bool found = false;

  try
  {
    reflex::Input::file_encoding_type encoding = reflex::Input::file_encoding::plain;

    // parse ugrep option --file-format=format
    if (flag_file_format)
    {
      int i;

      // scan the format_table[] for a matching format
      for (i = 0; format_table[i].format != NULL; ++i)
        if (strcmp(flag_file_format, format_table[i].format) == 0)
          break;

      if (format_table[i].format == NULL)
        help("unknown --file-format=format encoding");

      // encoding is the file format used by all input files, if no BOM is present
      encoding = format_table[i].encoding;
    }

    // set flags to convert regex to Unicode
    reflex::convert_flag_type convert_flags = reflex::convert_flag::unicode;

#if 0 // FIXME requires RE/flex 1.2.1
    // to convert basic regex (BRE) to extended regex (ERE)
    if (flag_basic_regexp)
      convert_flags |= reflex::convert_flag::basic;
#endif

    // prepend multiline mode modifier and other modifiers to the converted regex
    std::string modified_regex = "(?m";
    if (flag_ignore_case)
    {
      // prepend case-insensitive regex modifier, applies to ASCII only
      modified_regex.append("i");
    }
    if (flag_free_space)
    {
      // this is needed to check free-space conformance by the converter
      convert_flags |= reflex::convert_flag::freespace;
      // prepend free-space regex modifier
      modified_regex.append("x");
    }
    modified_regex.append(")").append(reflex::Matcher::convert(regex, convert_flags));

    // if --tabs=N then set pattern matching options to tab size
    std::string pattern_options;
    if (flag_tabs)
    {
      if (flag_tabs == 1 || flag_tabs == 2 || flag_tabs == 4 || flag_tabs == 8)
        pattern_options.assign("T=").push_back(flag_tabs + '0');
      else
        help("invalid value for option --tabs");
    }

    // construct the DFA pattern
    reflex::Pattern pattern(modified_regex, pattern_options);

    // read each file to find pattern matches

    for (auto infile : infiles)
    {
      FILE *file = strcmp(infile, "-") == 0 ? stdin : fopen(infile, "r");

      if (file == NULL)
      {
        if (flag_no_messages)
          continue;

        perror("Cannot open file for reading");
        exit(EXIT_ERROR);
      }

      found |= ugrep(pattern, file, encoding, file != stdin ? infile : flag_label);

      if (file != stdin)
        fclose(file);
    }
  }
  catch (reflex::regex_error& error)
  {
    std::cerr << error.what();
    exit(EXIT_ERROR);
  }

  exit(found ? EXIT_OK : EXIT_FAIL);
}

// Search file, display pattern matches, return true when pattern matched anywhere
bool ugrep(reflex::Pattern& pattern, FILE *file, reflex::Input::file_encoding_type encoding, const char *infile)
{
  size_t matches = 0;

  std::string label;

  if (flag_with_filename)
  {
    label.assign(color_fn).append(infile).append(color_off);
    if (flag_null)
      label.push_back('\0');
    else
      label.append(color_se).append(":").append(color_off);
  }

  // create an input object to read the file (or stdin) using the given file format encoding
  reflex::Input input(file, encoding);

  if (flag_quiet || flag_files_with_match || flag_files_without_match)
  {
    // -q, -l, or -L: report if a single pattern match was found in the input

    matches = reflex::Matcher(pattern, input).find() != 0;

    if (flag_invert_match)
      matches = !matches;

    // -l or -L but not -q

    if (!flag_quiet && ((matches && flag_files_with_match) || (!matches && flag_files_without_match)))
    {
      if (flag_null)
        std::cout << color_fn << infile << color_off << "\0";
      else
        std::cout << color_fn << infile << color_off << std::endl;
    }
  }
  else if (flag_count)
  {
    // -c count mode: count the number of lines/patterns matched

    if (flag_invert_match)
    {
      std::string line;

      // -c count mode w/ -v: count the number of non-matching lines
      while (input)
      {
        int ch;

        // read the next line
        line.clear();
        while ((ch = input.get()) != EOF && ch != '\n')
          line.push_back(ch);
        if (ch == EOF && line.empty())
          break;

        // count this line if not matched
        if (!reflex::Matcher(pattern, line).find())
        {
          ++matches;
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;
        }
      }
    }
    else if (flag_no_group)
    {
      // -c count mode w/ -g: count the number of patterns matched in the file

      while (reflex::Matcher(pattern, input).find())
      {
        ++matches;
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;
      }
    }
    else
    {
      // -c count mode w/o -g: count the number of matching lines

      size_t lineno = 0;

      for (auto& match : reflex::Matcher(pattern, input).find)
      {
        if (lineno != match.lineno())
        {
          lineno = match.lineno();
          ++matches;
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;
        }
      }
    }

    std::cout << label << matches << std::endl;
  }
  else if (flag_line_buffered)
  {
    // line-buffered input: read input line-by-line and display lines that matched the pattern

    size_t byte_offset = 0;
    size_t lineno = 1;
    std::string line;

    while (input)
    {
      int ch;

      // read the next line
      line.clear();
      while ((ch = input.get()) != EOF && ch != '\n')
        line.push_back(ch);
      if (ch == EOF && line.empty())
        break;

      if (flag_invert_match)
      {
        // -v invert match: display non-matching line

        if (!reflex::Matcher(pattern, line).find())
        {
          std::cout << label;
          if (flag_line_number)
            std::cout << color_ln << lineno << color_off << color_se << ":" << color_off;
          if (flag_byte_offset)
            std::cout << color_bn << byte_offset << color_off << color_se << ":" << color_off;
          std::cout << color_sl << line << color_off << std::endl;
          ++matches;
        }
      }
      else if (flag_no_group)
      {
        // search the line for pattern matches and display the line again (with exact offset) for each pattern match

        for (auto& match : reflex::Matcher(pattern, line).find)
        {
          std::cout << label;
          if (flag_line_number)
            std::cout << color_ln << lineno << color_off << color_se << ":" << color_off;
          if (flag_column_number)
            std::cout << color_cn << match.columno() + 1 << color_off << color_se << ":" << color_off;
          if (flag_byte_offset)
            std::cout << color_bn << byte_offset << color_off << color_se << ":" << color_off;
          std::cout << color_sl << line.substr(0, match.first()) << color_off << color_ms << match.text() << color_off << color_sl << line.substr(match.last()) << color_off << std::endl;
          ++matches;
        }
      }
      else
      {
        // search the line for pattern matches and display the line just once with all matches

        size_t last = 0;

        for (auto& match : reflex::Matcher(pattern, line).find)
        {
          if (last == 0)
          {
            std::cout << label;
            if (flag_line_number)
              std::cout << color_ln << lineno << color_off << color_se << ":" << color_off;
            if (flag_column_number)
              std::cout << color_cn << match.columno() + 1 << color_off << color_se << ":" << color_off;
            if (flag_byte_offset)
              std::cout << color_bn << byte_offset + match.first() << color_off << color_se << ":" << color_off;
            std::cout << color_sl << line.substr(0, match.first()) << color_off << color_ms << match.text() << color_off;
            last = match.last();
            ++matches;
          }
          else
          {
            std::cout << color_sl << line.substr(last, match.first() - last) << color_off << color_ms << match.text() << color_off;
            last = match.last();
          }
        }

        if (last > 0)
          std::cout << color_sl << line.substr(last) << color_off << std::endl;
      }

      if (flag_max_count > 0 && matches >= flag_max_count)
        break;

      // update byte offset and line number
      byte_offset += line.size() + 1;
      ++lineno;
    }
  }
  else
  {
    // block-buffered input: echo all pattern matches

    size_t lineno = 0;

    for (auto& match : reflex::Matcher(pattern, input).find)
    {
      if (flag_no_group || lineno != match.lineno())
      {
        lineno = match.lineno();
        std::cout << label;
        if (flag_line_number)
          std::cout << color_ln << lineno << color_off << color_se << ":" << color_off;
        if (flag_column_number)
          std::cout << color_cn << match.columno() + 1 << color_off << color_se << ":" << color_off;
        if (flag_byte_offset)
          std::cout << color_bn << match.first() << color_off << color_se << ":" << color_off;
        ++matches;
      }
      std::cout << color_ms << match.text() << color_off << std::endl;

      if (flag_max_count > 0 && matches >= flag_max_count)
        break;
    }
  }

  return matches > 0;
}

// Convert GREP_COLORS and set the color substring to the ANSI SGR sequence
void set_color(const char *grep_colors, const char *parameter, std::string& color)
{
  const char *substring = strstr(grep_colors, parameter);

  // check if substring parameter is present in GREP_COLORS
  if (substring != NULL && substring[2] == '=')
  {
    substring += 3;
    const char *colon = strchr(substring, ':');
    if (colon == NULL)
      colon = substring + strlen(substring);
    if (colon > substring)
      color.assign(SGR).append(substring, colon - substring).push_back('m');
  }
}

// Display help information with an optional diagnostic message and exit
void help(const char *message, const char *arg)
{
  if (message)
    std::cout << "ugrep: " << message << (arg != NULL ? arg : "") << std::endl;
  std::cout <<
"Usage: ugrep [-bcEFGgHhikLlmnoqsTVvwxZ] [--colour[=when]|--color[=when]]\n\
              [-e pattern] [--free-space] [--label[=label]] [--tabs=size]\n\
              [pattern] [file ...]\n\
\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched pattern is displayed in front of\n\
            the respective matched line.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            When used with option -g, counts the number of patterns matched.\n\
    --colour[=when], --color[=when]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR or GREP_COLORS environment variable.  The possible\n\
            values of when can be `never', `always' or `auto'.\n\
    -E, --extended-regexp\n\
            Interpret patterns as extended regular expressions (EREs). This is\n\
            the default.\n\
    -e pattern, --regexp=pattern\n\
            Specify a pattern used during the search of the input: an input\n\
            line is selected if it matches any of the specified patterns.\n\
            This option is most useful when multiple -e options are used to\n\
            specify multiple patterns, or when a pattern begins with a dash\n\
            (`-').\n\
    --file-format=format\n\
            The input file format.  The possible values of format can be:";
  for (int i = 0; format_table[i].format != NULL; ++i)
    std::cout << (i % 8 ? " " : "\n            ") << format_table[i].format;
  std::cout << "\n\
    -F, --fixed-strings\n\
            Interpret pattern as a set of fixed strings (i.e. force ugrep to\n\
            behave as fgrep but less efficiently).\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression (i.e. force ugrep\n\
            to behave as traditional grep).\n\
    -g, --no-group\n\
            Do not group pattern matches on the same line.  Display the\n\
            matched line again for each additional pattern match.\n\
    -H, --with-filename\n\
            Always print the filename with output lines.  This is the default\n\
            when there is more than one file to search.\n\
    -h, --no-filename\n\
            Never print filenames with output lines.\n\
    -?, --help\n\
            Print a help message.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching. This option applies\n\
            case-insensitive matching of ASCII characters in the input.\n\
            By default, ugrep is case sensitive.\n\
    -k, --column-number\n\
            The column number of a matched pattern is displayed in front of\n\
            the respective matched line, starting at column 1.  Tabs are\n\
            expanded before columns are counted.\n\
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
    --label[=label]\n\
            Displays the label value when input is read from standard input\n\
            where a file name would normally be printed in the output.  This\n\
            option applies to options -H, -L, and -l.\n\
    -m num, --max-count=num\n\
            Stop reading the input after num matches.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    -o, --only-matching\n\
            Prints only the matching part of the lines.  Allows a pattern\n\
            match to span multiple lines.\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress normal output.  ugrep will only search a file\n\
            until a match has been found, making searches potentially less\n\
            expensive.  Allows a pattern match to span multiple lines.\n\
    -s, --no-messages\n\
            Silent mode.  Nonexistent and unreadable files are ignored (i.e.\n\
            their error messages are suppressed).\n\
    -T, --initial-tab\n\
            Make sure that the first character of the actual line content lies\n\
            on a tab stop when displayed.\n\
    --tabs=size\n\
            Set the tab size to 1, 2, 4, or 8 to expand tabs for option -k.\n\
    -V, --version\n\
            Display version information and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    -w, --word-regexp\n\
            The pattern is searched for as a word (as if surrounded by\n\
            `\\<' and `\\>').\n\
    -x, --line-regexp\n\
            Only input lines selected against an entire pattern are considered\n\
            to be matching lines (as if surrounded by ^ and $).\n\
    -Z, --null\n\
            Prints a zero-byte after the file name.\n\
\n\
    The ugrep utility exits with one of the following values:\n\
\n\
    0       One or more lines were selected.\n\
    1       No lines were selected.\n\
    >1      An error occurred.\n\
" << std::endl;
  exit(EXIT_ERROR);
}

// Display version info
void version()
{
  std::cout << "ugrep " VERSION " " PLATFORM << std::endl;
  exit(EXIT_OK);
}
