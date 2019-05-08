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
@brief     Universal grep - high-performance Unicode pattern search utility
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Universal grep: a high-performance universal file search utility matches
Unicode patterns.  Searches source code recursively in directory trees using
powerful pre-defined patterns. 

Download and installation:

  https://github.com/Genivia/ugrep

Requires:

  https://github.com/Genivia/RE-flex

Examples:

  # display the lines in places.txt that contain Unicode words
  ugrep '\w+' places.txt

  # display the lines in places.txt with Unicode words color-highlighted
  ugrep --color=auto '\w+' places.txt

  # list all Unicode words in places.txt
  ugrep -o '\w+' places.txt

  # list all ASCII words (using a POSIX character class) in places.txt
  ugrep -o '[[:word:]]+' places.txt

  # list lines containing the Greek letter α to ω (U+03B1 to U+03C9)
  ugrep '[α-ω]' places.txt

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
  ugrep -q -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*([^*]|(\*+[^/\x2A]))*\*+\/)' file.cpp

  # display function and method definitions in a C/C++ source file
  ugrep '^([[:alpha:]:]+\h*)+\(.*' file.cpp

  # display non-static function and method definitions in a C/C++ source file
  ugrep -e '^([[:alpha:]:]+\h*)+\(.*' -e '(?^^static.*)' file.cpp

  # display C/C++ comments and strings using patterns in file patterns/c/comments and patterns/c/strings
  ugrep -o -f patterns/c/comments -f patterns/c/strings file.cpp

Compile:

  c++ -std=c++11 -o ugrep ugrep.cpp -lreflex

Bugs FIXME:

  - Pattern '^$' does not match empty lines, because RE/flex find() does not permit empty matches.
  - Back-references are not supported.

Wanted TODO:

  - Detect "binary files" like grep and skip them?
  - Should we open files in binary mode "rb" when --binary-files option is specified?
  - ... anything else?

*/

#include <reflex/input.h>
#include <reflex/matcher.h>
#include <iomanip>

// check if we are on a windows OS
#if defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
# define OS_WIN
#endif

// windows has no isatty() or stat()
#ifdef OS_WIN
#include <windows.h>
#include <tchar.h> 
#include <stdio.h>
#include <strsafe.h>
#define isatty(fildes) ((fildes) == 1)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// ugrep version
#define VERSION "1.1.0"

// enable this macro for RE/flex 1.2.1 to support option -G
// #define WITH_REFLEX2

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
bool flag_with_filename            = false;
bool flag_no_filename              = false;
bool flag_no_group                 = false;
bool flag_no_messages              = false;
bool flag_count                    = false;
bool flag_fixed_strings            = false;
bool flag_free_space               = false;
bool flag_ignore_case              = false;
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
bool flag_word_regexp              = false;
bool flag_line_regexp              = false;
bool flag_dereference              = false;
size_t flag_after_context          = 0;
size_t flag_before_context         = 0;
size_t flag_max_count              = 0;
size_t flag_tabs                   = 8;
const char *flag_color             = NULL;
const char *flag_file_format       = NULL;
const char *flag_devices           = "read";
const char *flag_directories       = "read";
const char *flag_label             = "(standard input)";
const char *flag_initial_tab       = "";
const char *flag_separator         = ":";
const char *flag_group_separator   = "--";
std::vector<const char*> flag_file;
std::vector<const char*> flag_include;
std::vector<const char*> flag_include_dir;
std::vector<const char*> flag_exclude;
std::vector<const char*> flag_exclude_dir;
std::vector<const char*> flag_file_type;
std::vector<const char*> flag_file_extensions;
std::vector<const char*> flag_file_magic; // TODO not used yet

// external functions
extern bool wildmat(const char *text, const char *glob);

// function protos
bool ugrep(reflex::Pattern& pattern, FILE *file, reflex::Input::file_encoding_type encoding, const char *infile);
bool find(reflex::Pattern& pattern, reflex::Input::file_encoding_type encoding, const char *infile, bool is_argument = false);
bool recurse(reflex::Pattern& pattern, reflex::Input::file_encoding_type encoding, const char *indir);
void display(const char *infile, size_t lineno, size_t columno, size_t byte_offset, const char *sep);
void set_color(const char *grep_colors, const char *parameter, std::string& color);
void help(const char *message = NULL, const char *arg = NULL);
void version();

// table of RE/flex file encodings for ugrep option -:, --file-format
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
#if WITH_REFLEX2
  { "CP858",      reflex::Input::file_encoding::cp858   },
#endif
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

// table file types for ugrep option -t, --file-type
const struct { const char *type; const char *extensions; const char *magic; } type_table[] = {
  { "actionscript", "as,mxml",                                                  NULL },
  { "ada",          "ada,adb,ads",                                              NULL },
  { "asm",          "asm,s,S",                                                  NULL },
  { "asp",          "asp",                                                      NULL },
  { "aspx",         "master,ascx,asmx,aspx,svc",                                NULL },
  { "autoconf",     "ac,in",                                                    NULL },
  { "automake",     "am,in",                                                    NULL },
  { "awk",          "awk",                                                      NULL },
  { "basic",        "bas,BAS,cls,frm,ctl,vb,resx",                              NULL },
  { "batch",        "bat,BAT,cmd,CMD",                                          NULL },
  { "bison",        "y,yy,yxx",                                                 NULL },
  { "c",            "c,h,H,hdl",                                                NULL },
  { "c++",          "cpp,CPP,cc,cxx,CXX,h,hh,H,hpp,hxx,Hxx,HXX",                NULL },
  { "clojure",      "clj",                                                      NULL },
  { "csharp",       "cs",                                                       NULL },
  { "css",          "css",                                                      NULL },
  { "csv",          "csv",                                                      NULL },
  { "dart",         "dart",                                                     NULL },
  { "delphi",       "pas,int,dfm,nfm,dof,dpk,dproj,groupproj,bdsgroup,bdsproj", NULL },
  { "erlang",       "erl,hrl",                                                  NULL },
  { "fortran",      "for,ftn,fpp,f,F,f77,F77,f90,F90,f95,F95",                  NULL },
  { "go",           "go",                                                       NULL },
  { "groovy",       "groovy,gtmpl,gpp,grunit,gradle",                           NULL },
  { "haskell",      "hs,lhs",                                                   NULL },
  { "html",         "htm,html,xhtml",                                           NULL },
  { "jade",         "jade",                                                     NULL },
  { "java",         "java,properties",                                          NULL },
  { "js",           "js",                                                       NULL },
  { "json",         "json",                                                     NULL },
  { "jsp",          "jsp,jspx,jthm,jhtml",                                      NULL },
  { "less",         "less",                                                     NULL },
  { "lex",          "l,ll,lxx",                                                 NULL },
  { "lisp",         "lisp,lsp",                                                 NULL },
  { "lua",          "lua",                                                      NULL },
  { "m4",           "m4",                                                       NULL },
  { "make",         "mk,mak,makefile,Makefile,Makefile.Debug,Makefile.Release", NULL },
  { "matlab",       "m",                                                        NULL },
  { "objc",         "m,h",                                                      NULL },
  { "objcpp",       "mm,h",                                                     NULL },
  { "ocaml",        "ml,mli",                                                   NULL },
  { "parrot",       "pir,pasm,pmc,ops,pod,pg,tg",                               NULL },
  { "perl",         "pl,PL,pm,pod,t,psgi",                                      NULL },
  { "php",          "php,php3,php4,phtml",                                      NULL },
  { "python",       "py",                                                       "#!bin/python" },
  { "R",            "R",                                                        NULL },
  { "rst",          "rst",                                                      NULL },
  { "ruby",         "rb,rhtml,rjs,rxml,erb,rake,spec,Rakefile",                 "#!/bin/ruby" },
  { "rust",         "rs",                                                       NULL },
  { "scala",        "scala",                                                    NULL },
  { "scheme",       "scm,ss",                                                   NULL },
  { "shell",        "sh,bash,csh,tcsh,ksh,zsh,fish",                            "#!/bin/*sh" },
  { "smalltalk",    "st",                                                       NULL },
  { "sql",          "sql,ctl",                                                  NULL },
  { "tcl",          "tcl,itcl,itk",                                             NULL },
  { "tex",          "tex,cls,sty",                                              NULL },
  { "text",         "txt,TXT",                                                  NULL },
  { "tt",           "tt,tt2,ttml",                                              NULL },
  { "verilog",      "v,vh,sv",                                                  NULL },
  { "vhdl",         "vhd,vhdl",                                                 NULL },
  { "vim",          "vim",                                                      NULL },
  { "xml",          "xml,xsd,dtd,xsl,xslt,wsdl,rss,svg,ent",                    NULL },
  { "yacc",         "y",                                                        NULL },
  { "yaml",         "yaml,yml",                                                 NULL },
  { NULL,           NULL,                                                       NULL }
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

    if ((*arg == '-' && arg[1])
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
            if (strncmp(arg, "after-context=", 14) == 0)
              flag_after_context = (size_t)strtoull(arg + 14, NULL, 10);
            else if (strcmp(arg, "basic-regexp") == 0)
              flag_basic_regexp = true;
            else if (strncmp(arg, "before-context=", 15) == 0)
              flag_before_context = (size_t)strtoull(arg + 15, NULL, 10);
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
            else if (strcmp(arg, "context") == 0)
              flag_after_context = flag_before_context = 2;
            else if (strncmp(arg, "context=", 8) == 0)
              flag_after_context = flag_before_context = (size_t)strtoull(arg + 8, NULL, 10);
            else if (strcmp(arg, "count") == 0)
              flag_count = true;
            else if (strcmp(arg, "dereference") == 0)
              flag_dereference = true;
            else if (strcmp(arg, "dereference-recursive") == 0)
              flag_directories = "dereference-recurse";
            else if (strncmp(arg, "devices=", 8) == 0)
              flag_devices = arg + 8;
            else if (strncmp(arg, "directories=", 12) == 0)
              flag_directories = arg + 12;
            else if (strncmp(arg, "exclude=", 8) == 0)
              flag_exclude.push_back(arg + 8);
            else if (strncmp(arg, "exclude-dir=", 12) == 0)
              flag_exclude_dir.push_back(arg + 12);
            else if (strcmp(arg, "extended-regexp") == 0)
              ;
            else if (strncmp(arg, "file=", 5) == 0)
              flag_file.push_back(arg + 5);
            else if (strncmp(arg, "file-extensions=", 16) == 0)
              flag_file_extensions.push_back(arg + 16);
            else if (strncmp(arg, "file-format=", 12) == 0)
              flag_file_format = arg + 12;
            else if (strncmp(arg, "file-type=", 10) == 0)
              flag_file_type.push_back(arg + 10);
            else if (strcmp(arg, "files-with-match") == 0)
              flag_files_with_match = true;
            else if (strcmp(arg, "files-without-match") == 0)
              flag_files_without_match = true;
            else if (strcmp(arg, "fixed-strings") == 0)
              flag_fixed_strings = true;
            else if (strcmp(arg, "free-space") == 0)
              flag_free_space = true;
            else if (strncmp(arg, "group-separator=", 16) == 0)
              flag_group_separator = arg + 16;
            else if (strcmp(arg, "help") == 0)
              help();
            else if (strcmp(arg, "ignore-case") == 0)
              flag_ignore_case = true;
            else if (strncmp(arg, "include=", 8) == 0)
              flag_include.push_back(arg + 8);
            else if (strncmp(arg, "include-dir=", 12) == 0)
              flag_include_dir.push_back(arg + 12);
            else if (strcmp(arg, "initial-tab") == 0)
              flag_initial_tab = "\t";
            else if (strcmp(arg, "invert-match") == 0)
              flag_invert_match = true;
            else if (strcmp(arg, "label") == 0)
              flag_label = "";
            else if (strncmp(arg, "label=", 6) == 0)
              flag_label = arg + 6;
            else if (strcmp(arg, "line-buffered") == 0)
              ;
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
            else if (strcmp(arg, "no-group-separator") == 0)
              flag_group_separator = NULL;
            else if (strcmp(arg, "no-messages") == 0)
              flag_no_messages = true;
            else if (strcmp(arg, "null") == 0)
              flag_null = true;
            else if (strcmp(arg, "only-line-number") == 0)
              flag_only_line_number = true;
            else if (strcmp(arg, "only-matching") == 0)
              flag_only_matching = true;
            else if (strcmp(arg, "quiet") == 0 || strcmp(arg, "silent") == 0)
              flag_quiet = true;
            else if (strcmp(arg, "recursive") == 0)
              flag_directories = "recurse";
            else if (strncmp(arg, "regexp=", 7) == 0)
              regex.append(arg + 7).push_back('|');
            else if (strncmp(arg, "separator=", 10) == 0)
              flag_separator = arg + 10;
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

          case 'A':
            ++arg;
            if (*arg)
              flag_after_context = (size_t)strtoull(&arg[*arg == '='], NULL, 10);
            else if (++i < argc)
              flag_after_context = (size_t)strtoull(argv[i], NULL, 10);
            else
              help("missing number for option -A");
            is_grouped = false;
            break;

          case 'B':
            ++arg;
            if (*arg)
              flag_before_context = (size_t)strtoull(&arg[*arg == '='], NULL, 10);
            else if (++i < argc)
              flag_before_context = (size_t)strtoull(argv[i], NULL, 10);
            else
              help("missing number for option -B");
            is_grouped = false;
            break;

          case 'b':
            flag_byte_offset = true;
            break;

          case 'C':
            ++arg;
            if (*arg)
              flag_after_context = flag_before_context = (size_t)strtoull(&arg[*arg == '='], NULL, 10);
            else
              flag_after_context = flag_before_context = 2;
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
              help("missing action for option -D");
            is_grouped = false;
            break;

          case 'd':
            ++arg;
            if (*arg)
              flag_directories = &arg[*arg == '='];
            else if (++i < argc)
              flag_directories = argv[i];
            else
              help("missing action for option -d");
            is_grouped = false;
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

          case 'f':
            ++arg;
            if (*arg)
              flag_file.push_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file.push_back(argv[i]);
            else
              help("missing file for option -f");
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

          case 'N':
            flag_only_line_number = true;
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
            flag_initial_tab = "\t";
            break;

          case 't':
            ++arg;
            if (*arg)
              flag_file_type.push_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_type.push_back(argv[i]);
            else
              help("missing type for option -t");
            is_grouped = false;
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

          case 'X':
            flag_free_space = true;
            break;

          case 'x':
            flag_line_regexp = true;
            break;

          case 'Z':
            flag_null = true;
            break;

          case 'z':
            ++arg;
            if (*arg)
              flag_separator = &arg[*arg == '='];
            else if (++i < argc)
              flag_separator = argv[i];
            else
              help("missing separator for option -z");
            is_grouped = false;
            break;

          case '.':
            ++arg;
            if (*arg)
              flag_file_extensions.push_back(&arg[*arg == '=']);
            else if (++i < argc)
              flag_file_extensions.push_back(argv[i]);
            else
              help("missing extensions for option -.");
            is_grouped = false;
            break;

          case ':':
            ++arg;
            if (*arg)
              flag_file_format = &arg[*arg == '='];
            else if (++i < argc)
              flag_file_format = argv[i];
            else
              help("missing encoding for option -:");
            is_grouped = false;
            break;

          default:
            help("unknown option -", arg);
        }
      }
    }
    else
    {
      // parse a ugrep command-line argument
      if (flag_file.empty() && regex.empty() && strcmp(arg, "-") != 0)
      {
        // no regex pattern specified yet, so assign it to the regex string
        regex.assign(arg).push_back('|');
      }
      else
      {
        // otherwise add the file argument to the list of files
        infiles.push_back(arg);
      }
    }
  }

  // --file-type=list
  if (flag_file_type.size() == 1 && strcmp(flag_file_type[0], "list") == 0)
  {
    int i;

    std::cerr << std::setw(12) << "TYPE" << "   EXTENSIONS" << std::endl;

    for (i = 0; type_table[i].type != NULL; ++i)
      std::cerr << std::setw(12) << type_table[i].type << " = " << type_table[i].extensions << std::endl;

    exit(EXIT_ERROR);
  }

  // if no regex pattern is specified and no -f file then exit
  if (regex.empty() && flag_file.empty())
    help("");

  // remove the ending '|' from the |-concatenated regexes in the regex string
  regex.pop_back();

  if (regex.empty() && flag_file.empty())
  {
    // if the specified regex is empty then it matches every line
    regex.assign(".*");
  }
  else
  {
    // -F --fixed-strings: make newline-separated lines in regex literal with \Q and \E
    if (flag_fixed_strings)
    {
      std::string strings;
      size_t from = 0;
      size_t to;

      // split regex at newlines, add \Q \E to each string, separate by |
      while ((to = regex.find('\n', from)) != std::string::npos)
      {
        strings.append("\\Q").append(regex.substr(from, to - from)).append("\\E|");
        from = to + 1;
      }

      regex = strings.append(regex.substr(from));
    }

    // if -w or -x: make the regex word- or line-anchored, respectively
    if (flag_word_regexp)
      regex.insert(0, "\\<(").append(")\\>");
    else if (flag_line_regexp)
      regex.insert(0, "^(").append(")$");
  }

  if (!flag_file.empty())
  {
    // add an ending '|' to the regex to concatenate sub-expressions
    if (!regex.empty())
      regex.push_back('|');

    // -f --file: read patterns from the specified file or files
    for (std::vector<const char*>::const_iterator i = flag_file.begin(); i != flag_file.end(); ++i)
    {
#ifdef OS_WIN

      FILE *file;

      if (fopen_s(&file, *i, "r") != 0)
      {
        perror("ugrep: cannot open -f file for reading");
        exit(EXIT_ERROR);
      }

#else

      FILE *file = fopen(*i, "r");

      if (file == NULL)
      {
        // could not open, try GREP_PATH environment variable
        const char *grep_path = getenv("GREP_PATH");
        if (grep_path != NULL)
        {
          std::string path_file(grep_path);
          path_file.append("/").append(*i);
          file = fopen(path_file.c_str(), "r");
        }
      }

      if (file == NULL)
      {
        perror("ugrep: cannot open -f file for reading");
        exit(EXIT_ERROR);
      }

#endif

      reflex::Input input(file);
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

        // add line to the regex if not empty
        if (!line.empty())
          regex.append(line).push_back('|');
      }

      fclose(file);
    }

    // remove the ending '|' from the |-concatenated regexes in the regex string
    regex.pop_back();
  }

  // if no files were specified then read standard input
  if (infiles.empty())
    infiles.push_back("-");

  // if -v --invert-match: options -g --no-group and -o --only-matching options cannot be used
  if (flag_invert_match)
  {
    flag_no_group = false;
    flag_only_matching = false;
  }

  // if not -o --only-matching and not -N --only-line-number: input and output are line-buffered
  if (!flag_only_matching && !flag_only_line_number)
    flag_line_buffered = true;

  // normalize -R --dereference-recurse option
  if (strcmp(flag_directories, "dereference-recurse") == 0)
  {
    flag_directories = "recurse";
    flag_dereference = true;
  }

  // display file name if more than one input file is specified or options -R -r, and option -h --no-filename is not specified
  if (!flag_no_filename && (infiles.size() > 1 || strcmp(flag_directories, "recurse") == 0))
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
      color_term = term &&
        (strstr(term, "ansi") != NULL ||
          strstr(term, "xterm") != NULL ||
          strstr(term, "color") != NULL);
#endif

      if (!color_term || !isatty(1))
        flag_color = NULL;
    }
    else if (strcmp(flag_color, "always") != 0)
    {
      help("unknown --color=WHEN value");
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

      if (grep_color != NULL)
        color_mt.assign(SGR).append(grep_color).push_back('m');
      else if (grep_colors == NULL)
        grep_colors = "mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36";

      if (grep_colors != NULL)
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

        if (flag_invert_match && strstr(grep_colors, "rv") != NULL)
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

  // check -D option
  if (strcmp(flag_devices, "read") != 0 &&
      strcmp(flag_devices, "skip") != 0)
    help("unknown --devices=ACTION value");

  // check -d option
  if (strcmp(flag_directories, "read") != 0 &&
      strcmp(flag_directories, "skip") != 0 &&
      strcmp(flag_directories, "recurse") != 0 &&
      strcmp(flag_directories, "dereference-recurse") != 0)
    help("unknown --directories=ACTION value");

  reflex::Input::file_encoding_type encoding = reflex::Input::file_encoding::plain;

  // parse ugrep option --file-format
  if (flag_file_format != NULL)
  {
    int i;

    // scan the format_table[] for a matching format
    for (i = 0; format_table[i].format != NULL; ++i)
      if (strcmp(flag_file_format, format_table[i].format) == 0)
        break;

    if (format_table[i].format == NULL)
      help("unknown --file-format=ENCODING value");

    // encoding is the file format used by all input files, if no BOM is present
    encoding = format_table[i].encoding;
  }

  // parse ugrep option --file-type
  for (auto type : flag_file_type)
  {
    int i;

    // scan the type_table[] for a matching type
    for (i = 0; type_table[i].type != NULL; ++i)
      if (strcmp(type, type_table[i].type) == 0)
        break;

    if (type_table[i].type == NULL)
      help("unknown --file-type=TYPE value");

    flag_file_extensions.push_back(type_table[i].extensions);
  }

  // add the --file-extensions as globs to the --include list
  for (auto extensions : flag_file_extensions)
  {
    const char *from = extensions;
    const char *to;
    std::string glob;

    while ((to = strchr(from, ',')) != NULL || (to = from + strlen(from)) > from)
    {
      flag_include.push_back(strdup(glob.assign("*.").append(from, to - from).c_str()));

      if (*to == '\0')
        break;

      from = to + 1;
    }
  }

  // if any match was found in any of the input files then we set found==true
  bool found = false;

  try
  {
    // set flags to convert regex to Unicode
    reflex::convert_flag_type convert_flags = reflex::convert_flag::unicode;

#if WITH_REFLEX2
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
        pattern_options.assign("T=").push_back((char)flag_tabs + '0');
      else
        help("invalid --tabs=NUM value");
    }

    // construct the DFA pattern
    reflex::Pattern pattern(modified_regex, pattern_options);

    // read each file to find pattern matches
    for (auto infile : infiles)
    {
      if (strcmp(infile, "-") == 0)
        found |= ugrep(pattern, stdin, encoding, flag_label);
      else
        found |= find(pattern, encoding, infile, true);
    }
  }
  catch (reflex::regex_error& error)
  {
    if (!flag_no_messages)
      std::cerr << error.what();

    exit(EXIT_ERROR);
  }

  exit(found ? EXIT_OK : EXIT_FAIL);
}

// Search file for pattern matches
bool find(reflex::Pattern& pattern, reflex::Input::file_encoding_type encoding, const char *infile, bool is_argument)
{
  bool found = false;

#ifdef OS_WIN

  DWORD attr = GetFileAttributesA(infile);

  if ((attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    if (strcmp(flag_directories, "read") == 0)
    {
      // directories cannot be read actually, so grep produces a warning message
      if (!flag_no_messages)
        fprintf(stderr, "ugrep: cannot read directory %s\n", infile);

      return false;
    }

    if (strcmp(flag_directories, "recurse") == 0)
    {
      if (!is_argument)
      {
        // exclude directories whose pathname matches any one of the --exclude-dir globs
        for (auto glob : flag_exclude_dir)
          if (wildmat(infile, glob))
            return false;

        if (!flag_include_dir.empty())
        {
          // include directories whose pathname matches any one of the --include-dir globs
          bool ok = false;
          for (auto glob : flag_include_dir)
            if ((ok = wildmat(infile, glob)))
              break;
          if (!ok)
            return false;
        }
      }

      return recurse(pattern, encoding, infile);
    }
  }
  else if ((attr & FILE_ATTRIBUTE_DEVICE) == 0 || strcmp(flag_devices, "read") == 0)
  {
    if (!is_argument)
    {
      // exclude files whose pathname matches any one of the --exclude globs
      for (auto glob : flag_exclude)
        if (wildmat(infile, glob))
          return false;

      if (!flag_include.empty())
      {
        // include files whose pathname matches any one of the --include globs
        bool ok = false;
        for (auto glob : flag_include)
          if ((ok = wildmat(infile, glob)))
            break;
        if (!ok)
          return false;
      }
    }

    FILE *file;

    if (fopen_s(&file, infile, "r") != 0)
    {
      if (!flag_no_messages)
        perror("ugrep: cannot open file for reading");

      return false;
    }

    found = ugrep(pattern, file, encoding, infile);

    fclose(file);
  }

#else

  struct stat buf;

  // use lstat() to check if infile is a symlink
  if (lstat(infile, &buf) == 0)
  {
    // symlinks are followed when specified on the command line or with options -R, -S, --dereference
    if (!S_ISLNK(buf.st_mode) || is_argument || flag_dereference)
    {
      // use stat() to check if infile is a directory or a regular file
      if (stat(infile, &buf) == 0)
      {
        if (S_ISDIR(buf.st_mode))
        {
          if (strcmp(flag_directories, "read") == 0)
          {
            // directories cannot be read actually, so grep produces a warning message
            if (!flag_no_messages)
              fprintf(stderr, "ugrep: cannot read directory %s\n", infile);

            return false;
          }

          if (strcmp(flag_directories, "recurse") == 0)
          {
            if (!is_argument)
            {
              // exclude directories whose pathname matches any one of the --exclude-dir globs
              for (auto glob : flag_exclude_dir)
                if (wildmat(infile, glob))
                  return false;

              if (!flag_include_dir.empty())
              {
                // include directories whose pathname matches any one of the --include-dir globs
                bool ok = false;
                for (auto glob : flag_include_dir)
                  if ((ok = wildmat(infile, glob)))
                    break;
                if (!ok)
                  return false;
              }
            }

            return recurse(pattern, encoding, infile);
          }
        }
        else if (S_ISREG(buf.st_mode) || strcmp(flag_devices, "read") == 0)
        {
          if (!is_argument)
          {
            // exclude files whose pathname matches any one of the --exclude globs
            for (auto glob : flag_exclude)
              if (wildmat(infile, glob))
                return false;

            if (!flag_include.empty())
            {
              // include files whose pathname matches any one of the --include globs
              bool ok = false;
              for (auto glob : flag_include)
                if ((ok = wildmat(infile, glob)))
                  break;
              if (!ok)
                return false;
            }
          }

          FILE *file = fopen(infile, "r");

          if (file == NULL)
          {
            if (!flag_no_messages)
              perror("ugrep: cannot open file for reading");

            return false;
          }

          found = ugrep(pattern, file, encoding, infile);

          fclose(file);
        }
      }
    }
  }
  else if (!flag_no_messages)
  {
    perror("ugrep: cannot stat file");
  }

#endif

  return found;
}

// Recurse over directory, searching for pattern matches in files and sub-directories
bool recurse(reflex::Pattern& pattern, reflex::Input::file_encoding_type encoding, const char *indir)
{
  bool found = false;

#ifdef OS_WIN

  WIN32_FIND_DATAA ffd;

  HANDLE hFind = FindFirstFileA(indir, &ffd);

  if (hFind == INVALID_HANDLE_VALUE) 
  {
    if (!flag_no_messages)
      perror("ugrep: cannot open directory for reading");

    return false;
  } 
   
  do
  {
    std::string pathname;
    pathname.assign(indir).append("\\").append(ffd.cFileName);

    found |= find(pattern, encoding, pathname.c_str());
  }
  while (FindNextFileA(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  DIR *dir = opendir(indir);

  if (dir == NULL)
  {
    if (!flag_no_messages)
      perror("ugrep: cannot open directory for reading");

    return false;
  }

  struct dirent *dirent;

  while ((dirent = readdir(dir)) != NULL)
  {
    // search directory entries that aren't . or ..
    if (strcmp(dirent->d_name, ".") != 0 && strcmp(dirent->d_name, "..") != 0)
    {
      std::string pathname;
      pathname.assign(indir).append("/").append(dirent->d_name);

      found |= find(pattern, encoding, pathname.c_str());
    }
  }

  closedir(dir);

#endif

  return found;
}

// Search file, display pattern matches, return true when pattern matched anywhere
bool ugrep(reflex::Pattern& pattern, FILE *file, reflex::Input::file_encoding_type encoding, const char *infile)
{
  size_t matches = 0;

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
      std::cout << color_fn << infile << color_off;

      if (flag_null)
        std::cout << (char)'\0';
      else
        std::cout << std::endl;
    }
  }
  else if (flag_count)
  {
    // -c --count mode: count the number of lines/patterns matched

    if (flag_invert_match)
    {
      std::string line;

      // -c --count mode w/ -v: count the number of non-matching lines
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
      // -c --count mode w/ -g: count the number of patterns matched in the file

      while (reflex::Matcher(pattern, input).find())
      {
        ++matches;
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;
      }
    }
    else
    {
      // -c --count mode w/o -g: count the number of matching lines

      size_t lineno = 0;

      reflex::Matcher matcher(pattern, input);

      for (auto& match : matcher.find)
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

    if (flag_with_filename)
    {
      std::cout << color_fn << infile << color_off;

      if (flag_null)
        std::cout << (char)'\0' << matches << std::endl;
      else
        std::cout << color_se << flag_separator << color_off << matches << std::endl;
    }
    else
    {
      std::cout << matches << std::endl;
    }
  }
  else if (flag_line_buffered)
  {
    // line-buffered input: read input line-by-line and display lines that match the pattern

    size_t byte_offset = 0;
    size_t lineno = 1;
    size_t before = 0;
    size_t after = 0;

    std::vector<size_t> byte_offsets;
    std::vector<std::string> lines;

    byte_offsets.reserve(flag_before_context + 1);
    lines.reserve(flag_before_context + 1);

    for (size_t i = 0; i <= flag_before_context; ++i)
    {
      byte_offsets.push_back(0);
      lines.push_back("");
    }

    while (input)
    {
      int ch;

      size_t current = lineno % (flag_before_context + 1);

      byte_offsets[current] = byte_offset;

      // read the next line
      lines[current].clear();
      while ((ch = input.get()) != EOF && ch != '\n')
        lines[current].push_back(ch);
      if (ch == EOF && lines[current].empty())
        break;

      bool before_context = flag_before_context > 0;
      bool after_context = flag_after_context > 0;

      size_t last = 0;

      if (flag_invert_match)
      {
        // -v --invert-match: select non-matching line

        bool found = false;

        reflex::Matcher matcher(pattern, lines[current]);
        
        for (auto& match : matcher.find)
        {
          if (after > 0 && after + flag_after_context >= lineno)
          {
            // -A NUM option: show context after matched lines, simulates BSD grep -A

            if (last == 0)
              display(infile, lineno, match.columno() + 1, byte_offset, "-");

            std::cout <<
              color_cx << lines[current].substr(last, match.first() - last) << color_off <<
              color_mc << match.str() << color_off;

            last = match.last();
          }
          else
          {
            found = true;

            break;
          }
        }

        if (last > 0)
        {
          std::cout << color_cx << lines[current].substr(last) << color_off << std::endl;
        }
        else if (!found)
        {
          if (after_context)
          {
            // -A NUM option: show context after matched lines, simulates BSD grep -A

            // indicate the end of the group of after lines of the previous matched line
            if (after + flag_after_context < lineno && matches > 0 && flag_group_separator != NULL)
              std::cout << color_se << flag_group_separator << color_off << std::endl;

            // remember the matched line
            after = lineno;
          }

          if (before_context)
          {
            // -B NUM option: show context before matched lines, simulates BSD grep -B

            size_t begin = before + 1;

            if (lineno > flag_before_context && begin < lineno - flag_before_context)
              begin = lineno - flag_before_context;

            // indicate the begin of the group of before lines
            if (begin < lineno && matches > 0 && flag_group_separator != NULL)
              std::cout << color_se << flag_group_separator << color_off << std::endl;

            // display lines before the matched line
            while (begin < lineno)
            {
              last = 0;

              reflex::Matcher matcher(pattern, lines[begin % (flag_before_context + 1)]);

              for (auto& match : matcher.find)
              {
                if (last == 0)
                  display(infile, begin, match.columno() + 1, byte_offsets[begin % (flag_before_context + 1)], "-");

                std::cout <<
                  color_cx << lines[begin % (flag_before_context + 1)].substr(last, match.first() - last) << color_off <<
                  color_mc << match.str() << color_off;

                last = match.last();
              }

              if (last > 0)
                std::cout << color_cx << lines[begin % (flag_before_context + 1)].substr(last) << color_off << std::endl;

              ++begin;
            }

            // remember the matched line
            before = lineno;
          }

          display(infile, lineno, 1, byte_offsets[current], flag_separator);

          std::cout << color_sl << lines[current] << color_off << std::endl;

          ++matches;

          // max number of matches reached?
          if (flag_max_count > 0 && matches >= flag_max_count)
            break;
        }
      }
      else
      {
        // search the line for pattern matches

        reflex::Matcher matcher(pattern, lines[current]);

        for (auto& match : matcher.find)
        {
          if (after_context)
          {
            // -A NUM option: show context after matched lines, simulates BSD grep -A

            // indicate the end of the group of after lines of the previous matched line
            if (after + flag_after_context < lineno && matches > 0 && flag_group_separator != NULL)
              std::cout << color_se << flag_group_separator << color_off << std::endl;

            // remember the matched line and we're done with the after context
            after = lineno;
            after_context = false;
          }

          if (before_context)
          {
            // -B NUM option: show context before matched lines, simulates BSD grep -B

            size_t begin = before + 1;

            if (lineno > flag_before_context && begin < lineno - flag_before_context)
              begin = lineno - flag_before_context;

            // indicate the begin of the group of before lines
            if (begin < lineno && matches > 0 && flag_group_separator != NULL)
              std::cout << color_se << flag_group_separator << color_off << std::endl;

            // display lines before the matched line
            while (begin < lineno)
            {
              display(infile, begin, 1, byte_offsets[begin % (flag_before_context + 1)], "-");

              std::cout << color_cx << lines[begin % (flag_before_context + 1)] << color_off << std::endl;

              ++begin;
            }

            // remember the matched line and we're done with the before context
            before = lineno;
            before_context = false;
          }

          if (flag_no_group)
          {
            // -g option: do not group matches on a single line but on multiple lines

            display(infile, lineno, match.columno() + 1, byte_offset + match.first(), last == 0 ? flag_separator : "+");

            std::cout <<
              color_sl << lines[current].substr(0, match.first()) << color_off <<
              color_ms << match.str() << color_off <<
              color_sl << lines[current].substr(match.last()) << color_off << std::endl;

            ++matches;

            // max number of matches reached?
            if (flag_max_count > 0 && matches >= flag_max_count)
              goto exit_input;
          }
          else
          {
            if (last == 0)
            {
              display(infile, lineno, match.columno() + 1, byte_offset, flag_separator);

              ++matches;
            }

            std::cout <<
              color_sl << lines[current].substr(last, match.first() - last) << color_off <<
              color_ms << match.str() << color_off;
          }

          last = match.last();
        }

        if (last > 0)
        {
          if (!flag_no_group)
            std::cout << color_sl << lines[current].substr(last) << color_off << std::endl;
        }
        else if (after > 0 && after + flag_after_context >= lineno)
        {
          // -A NUM option: show context after matched lines, simulates BSD grep -A

          // display line as part of the after context of the matched line
          display(infile, lineno, 1, byte_offsets[current], "-");

          std::cout << color_cx << lines[current] << color_off << std::endl;
        }

        // max number of matches reached?
        if (flag_max_count > 0 && matches >= flag_max_count)
          break;
      }

      // update byte offset and line number
      byte_offset += lines[current].size() + 1;
      ++lineno;
    }

exit_input:
    ;

  }
  else
  {
    // -o or -N option: block-buffered mode displays matched parts only

    size_t lineno = 0;

    reflex::Matcher matcher(pattern, input);

    for (auto& match : matcher.find)
    {
      if (flag_no_group || lineno != match.lineno())
      {
        // -g option (or new line): do not group matches on a single line but on multiple lines

        const char *separator = lineno != match.lineno() ? flag_separator : "+";

        lineno = match.lineno();

        display(infile, lineno, match.columno() + 1, match.first(), separator);

        if (flag_only_line_number)
          std::cout << std::endl;

        ++matches;
      }

      if (!flag_only_line_number)
      {
        std::string string = match.str();

        if (flag_line_number)
        {
          // -n -o options: echo multi-line matches line-by-line

          size_t from = 0;
          size_t to;

          while ((to = string.find('\n', from)) != std::string::npos)
          {
            ++lineno;

            std::cout << color_ms << string.substr(from, to - from) << color_off << std::endl;

            display(infile, lineno, 1, match.first() + to + 1, "|");

            from = to + 1;
          }

          std::cout << color_ms << string.substr(from) << color_off << std::endl;
        }
        else
        {
          std::cout << color_ms << string << color_off << std::endl;
        }
      }

      // max number of matches reached?
      if (flag_max_count > 0 && matches >= flag_max_count)
        break;
    }
  }

  return matches > 0;
}

void display(const char *infile, size_t lineno, size_t columno, size_t byte_offset, const char *separator)
{
  bool sep = false;

  if (flag_with_filename)
  {
    std::cout << color_fn << infile << color_off;

    if (flag_null)
      std::cout << (char)'\0';
    else
      sep = true;
  }

  if (flag_line_number || flag_only_line_number)
  {
    if (sep)
      std::cout << color_se << separator << color_off << flag_initial_tab;

    std::cout << color_ln << lineno << color_off;

    sep = true;
  }

  if (flag_column_number)
  {
    if (sep)
      std::cout << color_se << separator << color_off << flag_initial_tab;

    std::cout << color_cn << columno << color_off;

    sep = true;
  }

  if (flag_byte_offset)
  {
    if (sep)
      std::cout << color_se << separator << color_off << flag_initial_tab;

    std::cout << color_bn << byte_offset << color_off;

    sep = true;
  }

  if (sep)
    std::cout << flag_initial_tab << color_se << separator << color_off;
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

// Display usage/help information with an optional diagnostic message and exit
void help(const char *message, const char *arg)
{
  if (message && *message)
    std::cout << "ugrep: " << message << (arg != NULL ? arg : "") << std::endl;
  std::cout <<
"Usage: ugrep [-bcDdEFGgHhikLlmNnoqRrsTtVvwXxZz.:] [-A NUM] [-B NUM] [-C[NUM]] [-e PATTERN] [-f FILE] [--colour[=WHEN]|--color[=WHEN]] [--file-type=TYPES] [--file-format=ENCODING] [--label[=LABEL]] [PATTERN] [FILE ...]\n";
  if (!message)
  {
    std::cout << "\n\
    -A NUM, --after-context=NUM\n\
            Print NUM lines of trailing context after matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            the -B and -C options.\n\
    -B NUM, --before-context=NUM\n\
            Print NUM lines of leading context before matching lines.  Places\n\
            a --group-separator between contiguous groups of matches.  See also\n\
            the -A and -C options.\n\
    -b, --byte-offset\n\
            The offset in bytes of a matched line is displayed in front of the\n\
            respective matched line.  With option -g displays the offset in\n\
            bytes of each pattern matched.\n\
    -C[NUM], --context[=NUM]\n\
            Print NUM lines of leading and trailing context surrounding each\n\
            match.  The default is 2 and is equivalent to -A 2 -B 2.  Places\n\
            a --group-separator between contiguous groups of matches.  Note:\n\
            no whitespace may be given between the option and its argument.\n\
    -c, --count\n\
            Only a count of selected lines is written to standard output.\n\
            When used with option -g, counts the number of patterns matched.\n\
            With option -v, counts the number of non-matching lines.\n\
    --colour[=WHEN], --color[=WHEN]\n\
            Mark up the matching text with the expression stored in the\n\
            GREP_COLOR or GREP_COLORS environment variable.  The possible\n\
            values of WHEN can be `never', `always' or `auto'.\n\
    -D ACTION, --devices=ACTION\n\
            If an input file is a device, FIFO or socket, use ACTION to process\n\
            it.  By default, ACTION is `read', which means that devices are\n\
            read just as if they were ordinary files.  If ACTION is `skip',\n\
            devices are silently skipped.\n\
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
            or when option -f is used.\n\
    --exclude=GLOB\n\
            Skip files whose path name matches GLOB (using wildcard matching).\n\
            A path name glob can use *, ?, and [...] as wildcards, and \\ to\n\
            quote a wildcard or backslash character literally.  Note that\n\
            --exclude patterns take priority over --include patterns.\n\
    --exclude-dir=GLOB\n\
            Exclude directories whose path name matches GLOB from recursive\n\
            searches.  Note that --exclude-dir patterns take priority over\n\
            --include-dir patterns.\n\
    -F, --fixed-strings\n\
            Interpret pattern as a set of fixed strings, separated by newlines,\n\
            any of which is to be matched.  This forces ugrep to behave as\n\
            fgrep but less efficiently.\n\
    -f FILE, --file=FILE\n\
            Read one or more newline-separated patterns from FILE.  Empty\n\
            pattern lines in the file are not processed.  Options -F, -w, and\n\
            -x do not apply to patterns in FILE.  If FILE does not exist, uses\n\
            the GREP_PATH environment variable to attempt to open FILE.\n\
    -G, --basic-regexp\n\
            Interpret pattern as a basic regular expression (i.e. force ugrep\n\
            to behave as traditional grep).\n\
    -g, --no-group\n\
            Do not group pattern matches on the same line.  Display the matched\n\
            line again for each additional pattern match, using `+' as the\n\
            field separator for each additional line.\n\
    --group-separator=SEP\n\
            Use SEP as a group separator for context options -A, -B, and -C. By\n\
            default SEP is a double hyphen (`--').\n\
    -H, --with-filename\n\
            Always print the filename with output lines.  This is the default\n\
            when there is more than one file to search.\n\
    -h, --no-filename\n\
            Never print filenames with output lines.\n\
    --help\n\
            Print a help message.\n\
    -i, --ignore-case\n\
            Perform case insensitive matching. This option applies\n\
            case-insensitive matching of ASCII characters in the input.\n\
            By default, ugrep is case sensitive.\n\
    --include=GLOB\n\
            Search only files whose path name matches GLOB (using wildcard\n\
            matching).  A path name glob can use *, ?, and [...] as wildcards,\n\
            and \\ to quote a wildcard or backslash character literally.\n\
            Note that --exclude patterns take priority over --include patterns.\n\
    --include-dir=GLOB\n\
            Only directories whose path name matches GLOB are included in\n\
            recursive searches.  Note that --exclude-dir patterns take\n\
            priority over --include-dir patterns.\n\
    -k, --column-number\n\
            The column number of a matched pattern is displayed in front of\n\
            the respective matched line, starting at column 1.  Tabs are\n\
            expanded when columns are counted.\n\
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
    -m NUM, --max-count=NUM\n\
            Stop reading the input after NUM matches.\n\
    -N, --only-line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1, without displaying the line matched.  The\n\
            line number counter is reset for each file processed.\n\
    -n, --line-number\n\
            Each output line is preceded by its relative line number in the\n\
            file, starting at line 1.  The line number counter is reset for\n\
            each file processed.\n\
    --no-group-separator\n\
            Removes the group separator line from the output for context\n\
            options -A, -B, and -C.\n\
    -o, --only-matching\n\
            Prints only the matching part of the lines.  Allows a pattern\n\
            match to span multiple lines.  Line numbers for multi-line matches\n\
            are displayed with option -n, using `|' as the field separator for\n\
            each additional line matched by the pattern.  Context options -A,\n\
            -B, and -C are disabled.\n\
    -q, --quiet, --silent\n\
            Quiet mode: suppress normal output.  ugrep will only search a file\n\
            until a match has been found, making searches potentially less\n\
            expensive.  Allows a pattern match to span multiple lines.\n\
    -R, --dereference-recursive\n\
            Recursively read all files under each directory.  Follow all\n\
            symbolic links, unlike -r.\n\
    -r, --recursive\n\
            Recursively read all files under each directory, following symbolic\n\
            links only if they are on the command line.\n\
    -S, --dereference\n\
            If -r is specified, all symbolic links are followed.  This is\n\
            equivalent to the -R option.\n\
    -s, --no-messages\n\
            Silent mode.  Nonexistent and unreadable files are ignored (i.e.\n\
            their error messages are suppressed).\n\
    -T, --initial-tab\n\
            Add a tab space to separate the file name, line number, column\n\
            number, and byte offset with the matched line.\n\
    -t TYPES, --file-type=TYPES\n\
            Search only files of TYPES, which is a comma-separated list of file\n\
            types.  Each file type is associated with a set of file name\n\
            extensions that are used to narrow the search.  The possible values\n\
            of the file types in the list can be (use -t list for details):";
  for (int i = 0; type_table[i].type != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 7 ? " " : "\n            ") << "`" << type_table[i].type << "'";
  std::cout << "\n\
    --tabs=NUM\n\
            Set the tab size to NUM to expand tabs for option -k.  The value of\n\
            NUM may be 1, 2, 4, or 8.\n\
    -V, --version\n\
            Display version information and exit.\n\
    -v, --invert-match\n\
            Selected lines are those not matching any of the specified\n\
            patterns.\n\
    -w, --word-regexp\n\
            The pattern or -e patterns are searched for as a word (as if\n\
            surrounded by `\\<' and `\\>').\n\
    -X, --free-space\n\
            Spacing (blanks and tabs) in regular expressions are ignored.\n\
    -x, --line-regexp\n\
            Only input lines selected against the entire pattern or -e patterns\n\
            are considered to be matching lines (as if surrounded by ^ and $).\n\
    -Z, --null\n\
            Prints a zero-byte after the file name.\n\
    -z SEP, --separator=SEP\n\
            Use SEP as field separator between file name, line number, column\n\
            number, byte offset, and the matched line.  The default is a colon\n\
            (`:').\n\
    -.EXTENSIONS, --file-extensions=EXTENSIONS\n\
            Search only files whose file name extensions match the specified\n\
            comma-separated list of file name EXTENSIONS.  This option sets and\n\
            updates the set of files to search, which is the same as specifying\n\
            --include='*.ext' for each extension name `ext' in EXTENSIONS.\n\
    -:ENCODING, --file-format=ENCODING\n\
            The input file format.  The possible values of ENCODING can be:";
  for (int i = 0; format_table[i].format != NULL; ++i)
    std::cout << (i == 0 ? "" : ",") << (i % 6 ? " " : "\n            ") << "`" << format_table[i].format << "'";
  std::cout << "\n\
\n\
    The ugrep utility exits with one of the following values:\n\
\n\
    0       One or more lines were selected.\n\
    1       No lines were selected.\n\
    >1      An error occurred.\n\
" << std::endl;
  }
  exit(EXIT_ERROR);
}

// Display version info
void version()
{
  std::cout << "ugrep " VERSION " " PLATFORM << std::endl;
  exit(EXIT_OK);
}
