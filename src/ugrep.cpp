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

  RE/flex - https://github.com/Genivia/RE-flex

Features:

  - Patterns are ERE POSIX syntax compliant, extended with RE/flex pattern syntax.
  - Unicode support for \p{} character categories, bracket list classes, etc.
  - File encoding support for UTF-8/16/32, EBCDIC, and many other code pages.
  - ugrep command-line options are the same as grep, simulates grep behavior.

Differences with grep:

  - When option -b is used with option -o or with option -g, ugrep displays the
    exact byte offset of the pattern match instead of the byte offset of the
    start of the matched line.
  - Adds option -g, --no-group to not group matches per line.  This option
    displays a matched input line again for each additional pattern match.
    This option also changes option -c to report the total number of pattern
    matches per file instead of the number of lines matched.
  - Adds option -k, --column-number to display the column number, taking tab
    spacing into account by expanding tabs, as specified by option --tabs.

Examples:

  # display the lines in places.txt that contain capitalized Unicode words
  ugrep '\p{Upper}\p{Lower}*' places.txt

  # display the lines in places.txt with capitalized Unicode words color-highlighted
  ugrep --color=auto '\p{Upper}\p{Lower}*' places.txt

  # list all capitalized Unicode words in places.txt
  ugrep -o '\p{Upper}\p{Lower}*' places.txt

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
  - Like grep, we want -A, -B, and -C, --context to display the context of a match.
  - Like grep, we want -f, --file=file to read patterns from a file.
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
#define VERSION "1.0.0"

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

// GREP_COLOR environment variable
const char *grep_color = NULL;

// ugrep command-line options
bool flag_filename            = false;
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
bool flag_label               = false;
bool flag_null                = false;
bool flag_basic_regexp        = false;
bool flag_word_regexp         = false;
bool flag_line_regexp         = false;
const char *flag_color        = NULL;
const char *flag_file_format  = NULL;
size_t flag_max_count         = 0;
size_t flag_tabs              = 8;

// function protos
bool ugrep(reflex::Pattern& pattern, FILE *file, reflex::Input::file_encoding_type encoding, const char *infile);
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

  bool color_term = false;

#ifndef OS_WIN
  // check whether we have a color terminal
  const char *term = getenv("TERM");
  color_term = term && (strstr(term, "ansi") || strstr(term, "xterm") || strstr(term, "color"));
  grep_color = getenv("GREP_COLOR");
#endif
 
  // parse ugrep command-line options and arguments
  for (int i = 1; i < argc; ++i)
  {
    const char *arg = argv[i];

    if (*arg == '-'
#ifdef OS_WIN
     || *arg == '/'
#endif
     )
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
            else if (strcmp(arg, "invert-match") == 0)
              flag_invert_match = true;
            else if (strcmp(arg, "label") == 0)
              flag_label = true;
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
            flag_filename = true;
            flag_no_filename = false;
            break;

          case 'h':
            flag_filename = false;
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

          case '?':
            help();

          case '\0':
            break;

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
    flag_filename = true;

  // (re)set grep_color depending on color_term, isatty(), and the ugrep --color option
  if (!flag_color || strcmp(flag_color, "never") == 0)
  {
    grep_color = NULL;
  }
  else if (strcmp(flag_color, "always") == 0)
  {
    if (!grep_color)
      grep_color = "1";
  }
  else if (strcmp(flag_color, "auto") == 0)
  {
    if (!color_term || !isatty(1))
      grep_color = NULL;
    else if (!grep_color)
      grep_color = "1";
  }
  else
  {
    help("unknown --color=when value");
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

    if (infiles.empty())
    {
      // read standard input to find pattern matches

      if (flag_label)
      {
        // disable -H, -L, and -l
        flag_filename = false;
        flag_files_with_match = false;
        flag_files_without_match = false;
      }

      found |= ugrep(pattern, stdin, encoding, "(standard input)");
    }
    else
    {
      // read each file to find pattern matches

      for (auto infile : infiles)
      {
        FILE *file = fopen(infile, "r");

        if (file == NULL)
        {
          if (flag_no_messages)
            continue;

          perror("Cannot open file for reading");
          exit(EXIT_ERROR);
        }

        found |= ugrep(pattern, file, encoding, infile);

        fclose(file);
      }
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

  std::string label, mark, unmark;

  if (flag_filename && infile)
    label.assign(infile).push_back(flag_null ? '\0' : ':');

  if (grep_color)
  {
    mark.assign("\033[").append(grep_color).append("m");
    unmark.assign("\033[0m");
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
        std::cout << infile << "\0";
      else
        std::cout << infile << std::endl;
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
            std::cout << lineno << ":";
          if (flag_byte_offset)
            std::cout << byte_offset << ":";
          std::cout << line << std::endl;
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
            std::cout << lineno << ":";
          if (flag_column_number)
            std::cout << match.columno() + 1 << ":";
          if (flag_byte_offset)
            std::cout << byte_offset << ":";
          std::cout << line.substr(0, match.first()) << mark << match.text() << unmark << line.substr(match.last()) << std::endl;
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
              std::cout << lineno << ":";
            if (flag_column_number)
              std::cout << match.columno() + 1 << ":";
            if (flag_byte_offset)
              std::cout << byte_offset + match.first() << ":";
            std::cout << line.substr(0, match.first()) << mark << match.text() << unmark;
            last = match.last();
            ++matches;
          }
          else
          {
            std::cout << line.substr(last, match.first() - last) << mark << match.text() << unmark;
            last = match.last();
          }
        }

        if (last > 0)
          std::cout << line.substr(last) << std::endl;
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
          std::cout << lineno << ":";
        if (flag_column_number)
          std::cout << match.columno() + 1 << ":";
        if (flag_byte_offset)
          std::cout << match.first() << ":";
        ++matches;
      }
      std::cout << mark << match.text() << unmark << std::endl;

      if (flag_max_count > 0 && matches >= flag_max_count)
        break;
    }
  }

  return matches > 0;
}

// Display help information with an optional diagnostic message and exit
void help(const char *message, const char *arg)
{
  if (message)
    std::cout << "ugrep: " << message << (arg != NULL ? arg : "") << std::endl;
  std::cout << "Usage: ugrep [-bcEFgHhiknoqsVvwx] [--colour[=when]|--color[=when]] [-e pattern] [pattern] [file ...]\n\
\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched pattern is displayed in front of\n\
            the respective matched line.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            When used with option -g, counts the number of patterns matched.\n\
    --colour[=when], --color[=when]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR environment variable.  The possible values of when can\n\
            be `never', `always' or `auto'.\n\
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
            behave as fgrep).\n\
    --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression (i.e. force ugrep\n\
            to behave as traditional grep).\n\
    -g, --no-group\n\
            Do not group pattern matches on the same line.  Display the\n\
            matched line again for each additional pattern match.\n\
    -H\n\
            Always print filename headers with output lines.\n\
    -h, --no-filename\n\
            Never print filename headers (i.e. filenames) with output lines.\n\
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
    --label\n\
            This option disables options -H, -L, and -l when standard input is\n\
            read, to remove ``(standard input)'' where a file name would\n\
            normally be printed.\n\
    -m num, --max-count=num\n\
            Stop reading the file after num matches.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --null\n\
            Prints a zero-byte after the file name.\n\
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
