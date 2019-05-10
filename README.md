ugrep: universal grep
=====================

Offers powerful pre-defined search patterns and quick options to selectively
search source code files efficiently in large directory trees.

**ugrep** uses [RE/flex](https://github.com/Genivia/RE-flex) for
high-performance regex matching, which is 100 times faster than the GNU C
POSIX.2 regex library used by GNU grep and 10 times faster than PCRE2 and RE2.
Because RE/flex is a streaming regex matcher, **ugrep** scans files more
efficiently with options like `-o`, permitting pattern matches that span
multiple lines instead of searching per line as with other grep utilities.

**ugrep** makes it easy to search source code.  It is the only grep tool that
allows you to define negative patterns to "zap" parts in files you want to
skip.  This removes many false positives.  For example to find exact matches of
`main` in C/C++ source code while skipping strings and comments that may have a
match with `main` in them:

    ugrep -r -o -tc,c++ -n -w 'main' -f patterns/c/zap_strings -f patterns/c/zap_comments myprojects

where `-r` is recursive search, `-o` for multi-line matches (since strings and
comments may span multiple lines), `-tc,c++` searches C and C++ source code
files only, `-n` shows line numbers in the output, `-w` matches exact words
(for example, `mainly` won't be matched), the `-f` options specify two
pre-defined patterns to match and ignore strings and comments in the input.

**ugrep** searches source code files by file name extension and other criteria
using option `-t` so specify the type of files to search recursively in a
directory tree with option `-r`, e.g. `-r -tc++`.

**ugrep** includes a growing database of
[patterns](https://github.com/Genivia/ugrep/tree/master/patterns) with common
search patterns to use with option `-f`.  So you don't need to memorize complex
regex patterns for common search criteria.  Environment variable `GREP_PATH`
can be set to point to your own directory with patterns that option `-f` uses
to read your pattern files.

**ugrep** offers options that are compatible with the
[GNU grep](https://www.gnu.org/software/grep/manual/grep.html) and BSD grep
utilities, and can be used as a more powerful replacement of these.

**ugrep** matches Unicode patterns.  The regular expression syntax is POSIX ERE
compliant, extended with Unicode character classes, lazy quantifiers, and
negative patterns to skip unwanted pattern matches to produce more precise
results.

**ugrep** searches UTF-encoded input when UTF BOM
([byte order mark](https://en.wikipedia.org/wiki/Byte_order_mark)) are present
and ASCII and UTF-8 when no UTF BOM is present.  Option `--file-format` permits
many other file formats to be searched, such as ISO-8859-1, EBCDIC, and code
pages 437, 850, 858, 1250 to 1258.

**ugrep** regex patterns are converted to
[DFAs](https://en.wikipedia.org/wiki/Deterministic_finite_automaton) for fast
matching.  Rare and pathelogical cases are known to exist that may increase the
initial running time for DFA construction.  The resulting DFAs still yield
significant speedups to search large files.

**ugrep** is portable and compiles with MSVC++ to run on Windows.

**ugrep** is free [BSD-3](https://opensource.org/licenses/BSD-3-Clause) source
code and does not include any GNU or BSD grep open source code or algorithms.
**ugrep** is built entirely on the RE/flex open source library and Rich Salz'
free and open wildmat source code for glob matching with options `--include`
and `--exclude`.

**ugrep** is evolving and more features will be added.  You can help!  We love
your feedback (issues) and contributions (pull requests) ❤️

ugrep versus other "greps"
--------------------------

- **ugrep** supports "negative patterns" to skip parts of the input that should
  not be matched, such as skipping strings and comments when searching for
  identifiers in source code.
- When one or more of the options `-q` (quiet), `-o` (only matching), `-N`
  (only line number), `-l` (file with match), or `-L` (files without match) is
  used, **ugrep** performs an even faster streaming-based search of the input
  file instead of reading the input line-by-line as other grep tools do.  This
  allows matching patterns that include newlines (`\n`), i.e. a match can span
  multiple lines.  This is not possible with other grep-like tools.
- New option `-k`, `--column-number` with **ugrep** to display the column
  number, taking tab spacing into account by expanding tabs, as specified by
  option `--tabs`.
- New option `-g`, `--no-group` to not group matches per line.  This option
  displays a matched input line again for each additional pattern match.  This
  option is particularly useful with option `-c` to report the total number of
  pattern matches per file instead of the number of lines matched per file.
- When option `-b` is used with option `-o` or with option `-g`, **ugrep**
  displays the exact byte offset of the pattern match instead of the byte
  offset of the start of the matched line as grep reports.  Reporting exact
  byte offsets is now possible with **grep**.
- **ugrep** regular expression patterns are more expressive than GNU grep and
  BSD grep and support Unicode pattern matching, see further below.  Extended
  regular expression syntax is the default (i.e.  option `-E`, as egrep).
- **ugrep** always assumes UTF-8 locale to support Unicode, e.g.
  `LANG=en_US.UTF-8`, wheras grep is locale-sensitive.
- BSD grep (e.g. on Mac OS X) has bugs and limitations that **ugrep** fixes,
  e.g.  options `-r` versus `-R`, support for `GREP_COLORS`, and more.

Speed
-----

**ugrep** is not yet fully optimized.  The initial performance results look
promising.  For example, searching for all matches of syntactically-valid
variants of `#include "..."` in the directory tree from the Qt 5.9.2 root,
restricted to `.h`, `.hpp`, and `.cpp` files only:

    time egrep -r -o '#[ \t]*include[ \t]+"[^"]+"' --include='*.h' --include='*.hpp' --include='*.cpp' . > &/dev/null
    3.630u 0.274s 0:03.90 100.0%    0+0k 0+0io 0pf+0w

    time ugrep -r -o '#[ \t]*include[ \t]+"[^"]+"' -Oh,hpp,cpp . >& /dev/null
    0.837u 0.185s 0:01.02 99.0%     0+0k 0+0io 0pf+0w

Unoptimized, **ugrep** is already 3 times faster than BSD egrep (**ugrep** was
compiled with clang 9.0.0 -O2, and this test was run on a 2.9 GHz Intel Core
i7, 16 GB 2133 MHz LPDDR3 machine).

Dependencies
------------

https://github.com/Genivia/RE-flex

Installation
------------

First install RE/flex from https://github.com/Genivia/RE-flex then download
ugrep from https://github.com/Genivia/ugrep and execute:

    $ ./configure; make

This builds `ugrep` in the `src` directory.  You can tell which version it is
with:

    $ src/ugrep -V
    ugrep 1.1.0 x86_64-apple-darwin16.7.0

Optionally, install the ugrep utility and the ugrep manual page:

    $ sudo make install
    $ ugrep -V
    ugrep 1.1.0 x86_64-apple-darwin16.7.0

Examples
--------

### Searching source code

To search for the identifier `main` as a word (`-w`) recursively (`-r`) in
directory `myproject`, showing the matching line (`-n`) and column (`-k`)
numbers next to the lines matched:

    ugrep -r -n -k -w 'main' myproject

But this search query also finds `main` in strings and comment blocks.  With
**ugrep** we can use "negative patterns" of the form `(?^...)` to ignore
unwanted matches in C/C++ quoted strings and comment blocks.  Because strings
and comment blocks may span multiple lines, we should use `-o`:

    ugrep -r -o -nkw 'main' '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*([^*]|(\*+[^*/]))*\*+\/)' myproject

This is a lot of work to type in correctly!  If you are like me, I'm lazy and
don't want to spend time fiddling with regex patterns when I am working on
something more important.  There is an easier way by using **ugrep**'s
pre-defined patterns (`-f`):

    ugrep -r -o -nkw 'main' -f patterns/c/zap_strings -f patterns/c/zap_comments myproject

This query also searches through other files than C/C++ source code, like
READMEs, Makefiles, and so on.  So let's refine this query by selecting C/C++
files only using option `-tc,c++`:

    ugrep -r -o -tc,c++ -nkw 'main' -f patterns/c/zap_strings -f patterns/c/zap_comments myproject

As another example, we may want to search for word `FIXME` in C/C++ comment
blocks.  To do so we can first select the comment blocks with **ugrep**'s
pre-defined `c/comments` pattern AND THEN select lines with `FIXME` using a
pipe:

    ugrep -r -o -tc,c++ -nk -f patterns/c/comments myproject | ugrep -w 'FIXME'

Filtering results this way with pipes is generally easier than using AND-OR
logic that some search tools use.  This approach follows the Unix spirit to
keep utilities simple and use them in combination for more complex tasks.

Say we want to produce a sorted list of all identifiers found in Java source
code while skipping strings and comments:

    ugrep -r -o -tjava -f patterns/java/names -f patterns/java/zap_strings -f patterns/java/zap_comments myproject | sort -u

This matches Java Unicode identifiers using the regex
`\p{JavaIdentifierStart}\p{JavaIdentifierPart}*` defined in
`patterns/java/names`.

With traditional grep and grep-like tools it takes great effort to recursively
search for the C/C++ source file that defines function `qsort`, requiring
something like this:

    ugrep -r --include='*.c' --include='*.cpp' '^([ \t]*[[:word:]:*&]+)+[ \t]+qsort[ \t]*\([^;\n]+$' myproject

Fortunately, with **ugrep** we can simply select all function definitions in
files with extension `.c` or `.cpp` by using option `-Oc,cpp` and by using a
pre-defined pattern `function_defs` to produce all function definitions.  Then
we select the one we want:

    ugrep -r -o -Oc,cpp -nk -f patterns/c/function_defs myproject | ugrep 'qsort'

Note that we could have used `-tc,c++` to select C/C++ files, but this also
includes header files when we want to only search `.c` and `.cpp` files.  To
display the list of file name extensions searched for all available options for
`-t` use:

    ugrep -tlist

We can also skip files and directories from being searched that are defined in
`.gitignore`.  To do so we use `--exclude-from` to specify a file with files
and directories (declared as glob patterns) to ignore:

    ugrep -r -tc++ --color --exclude-from='.gitignore' -f patterns/c++/defines .

While searching C++ files (`-tc++`) in the current directory (`.`)for `#define`
lines (`-f patterns/c++/defines`), this query skips file `config.h` and other
files and directories declared in `.gitignore`.

Finally, to highlight matches when pushed through a chain of pipes we should
use `--color=always`:

    ugrep -r -tc++ --color=always --exclude-from='.gitignore' -f patterns/c++/defines . | ugrep -w 'Foo.*'

### Using Unicode

To display lines with Unicode words in `places.txt`:

    ugrep '\w+' places.txt

To produce a sorted list of all ASCII words in `places.txt`:

    ugrep '[[:word:]]+' places.txt

To display all lines containing laughing face emojis in `birthday.txt`:

    ugrep '[😀-😏]' birthday.txt

Likewise, we can use the following for the same results:

    ugrep '[\x{1F600}-\x{1F60F}]' birthday.txt

To display lines containing the names Gödel (or Goedel), Escher, or Bach:

    ugrep 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To display lines that do not contain the names Gödel (or Goedel), Escher, or
Bach we use option `-v` (invert match):

    ugrep -v 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To count the number of lines containing the names Gödel (or Goedel), Escher, or
Bach we use option `-c`:

    ugrep -c 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To count the total number of occurrences of the names Gödel (or Goedel),
Escher, or Bach we use options `-c` and `-g` (don't group matches on the same
line):

    ugrep -c -g 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To check if `myfile` contains any non-ASCII Unicode characters we use pattern
`[^[:ascii:]]` (not ASCII) and option `-q` (quick) that only sets the **ugrep**
exit status to 0 (success) or 1 (failure):

    ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"

To check if a file has any invalid Unicode characters:

    ugrep -q '[^\p{Unicode}--[\xFFFD]]' myfile && echo "contains invalid Unicode"

In this example we included the Unicode code point U+FFFD as an error for
illustrative purposes, because it is often used to flag invalid UTF encodings.

To search for `lorem` in lower or upper case (option `-i` case insensitive) in
a UTF-16 file (with UTF-16 BOM), while color-highlighting the matches:

    ugrep --color -i -w 'lorem' utf16lorem.txt

When utf16lorem.txt has no UTF-16 BOM we can specify UTF-16 file encoding:

    ugrep --file-format=UTF-16 -i -w 'lorem' utf16lorem.txt

Man page
--------

    UGREP(1)                         User Commands                        UGREP(1)



    NAME
           ugrep -- universal file pattern searcher

    SYNOPSIS
           ugrep [OPTIONS] [-A NUM] [-B NUM] [-C[NUM]] [PATTERN] [-e PATTERN]
                 [-f FILE] [--file-type=TYPES] [--file-format=ENCODING]
                 [--colour[=WHEN]|--color[=WHEN]] [--label[=LABEL]] [FILE ...]

    DESCRIPTION
           The  ugrep utility searches any given input files, selecting lines that
           match one or more patterns.  By default, a  pattern  matches  an  input
           line  if  the  regular expression (RE) in the pattern matches the input
           line without its trailing newline.  An empty expression  matches  every
           line.   Each  input  line  that matches at least one of the patterns is
           written to the standard output.

           The ugrep utility normalizes Unicode input, so ugrep  can  be  used  to
           search  for  Unicode  patterns  in text files encoded in UTF-8, UTF-16,
           UTF-32 by detecting UTF BOM in the input.  When no UTF BOM is detected,
           ugrep  searches  for  Unicode  patterns  in UTF-8 input, which includes
           ASCII input.  ugrep searches input files encoded in ISO-8859-1, EBCDIC,
           CP-437,  CP-850, CP-858, CP-1250 to CP-1258 when the file encoding for-
           mat is specified with option --file-format.

           The following options are available:

           -A NUM, --after-context=NUM
                  Print NUM  lines  of  trailing  context  after  matching  lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -B and -C options.

           -B NUM, --before-context=NUM
                  Print NUM  lines  of  leading  context  before  matching  lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -A and -C options.

           -b, --byte-offset
                  The offset in bytes of a matched line is displayed in  front  of
                  the respective matched line.  With option -g displays the offset
                  in bytes of each pattern matched.

           -C[NUM], --context[=NUM]
                  Print NUM lines of leading and trailing context surrounding each
                  match.  The default is 2 and is equivalent to -A 2 -B 2.  Places
                  a --group-separator between contiguous groups of matches.  Note:
                  no  whitespace may be given between the option and its argument.

           -c, --count
                  Only a count of selected lines is written  to  standard  output.
                  When used with option -g, counts the number of patterns matched.
                  With option -v, counts the number of non-matching lines.

           --colour[=WHEN], --color[=WHEN]
                  Mark up the matching text with  the  expression  stored  in  the
                  GREP_COLOR  or  GREP_COLORS  environment variable.  The possible
                  values of WHEN can be `never', `always' or `auto'.

           -D ACTION, --devices=ACTION
                  If an input file is a device, FIFO  or  socket,  use  ACTION  to
                  process  it.   By  default,  ACTION  is `read', which means that
                  devices are read just as if they were ordinary files.  If ACTION
                  is `skip', devices are silently skipped.

           -d ACTION, --directories=ACTION
                  If  an  input file is a directory, use ACTION to process it.  By
                  default, ACTION is `read', i.e., read  directories  just  as  if
                  they  were  ordinary  files.  If ACTION is `skip', silently skip
                  directories.  If ACTION is `recurse', read all files under  each
                  directory,  recursively,  following  symbolic links only if they
                  are on the command line.  This is equivalent to the  -r  option.
                  If  ACTION  is  `dereference-recurse', read all files under each
                  directory,  recursively,  following  symbolic  links.   This  is
                  equivalent to the -R option.

           -E, --extended-regexp
                  Interpret  patterns as extended regular expressions (EREs). This
                  is the default.

           -e PATTERN, --regexp=PATTERN
                  Specify a PATTERN used during the search of the input: an  input
                  line  is  selected  if it matches any of the specified patterns.
                  This option is most useful when multiple -e options are used  to
                  specify  multiple  patterns,  when  a pattern begins with a dash
                  (`-'), or when option -f is used.

           --exclude=GLOB
                  Skip files whose path name matches GLOB (using  wildcard  match-
                  ing).   A  path  name glob can use *, ?, and [...] as wildcards,
                  and \ to quote a  wildcard  or  backslash  character  literally.
                  Note  that  --exclude patterns take priority over --include pat-
                  terns.  This option may be repeated.

           --exclude-dir=GLOB
                  Exclude directories whose path name matches GLOB from  recursive
                  searches.   Note  that --exclude-dir patterns take priority over
                  --include-dir patterns.  This option may be repeated.

           --exclude-from=FILE
                  Read the globs from FILE and skip files whose path name  matches
                  one  or more globs (using wildcard matching).  Comments starting
                  with a `#' and empty lines in FILE are ignored.

           -F, --fixed-strings
                  Interpret pattern as a set of fixed strings, separated  by  new-
                  lines,  any  of  which  is  to be matched.  This forces ugrep to
                  behave as fgrep but less efficiently.

           -f FILE, --file=FILE
                  Read one or more newline-separated patterns  from  FILE.   Empty
                  pattern  lines  in  the file are not processed.  Options -F, -w,
                  and -x do not apply to FILE patterns.  If FILE does  not  exist,
                  uses the GREP_PATH environment variable to attempt to open FILE.
                  This option may be repeated.

           -G, --basic-regexp
                  Interpret pattern as a  basic  regular  expression  (i.e.  force
                  ugrep to behave as traditional grep).

           -g, --no-group
                  Do  not  group  pattern  matches  on the same line.  Display the
                  matched line again for each additional pattern match, using  `+'
                  as the field separator for each additional line.

           --group-separator=SEP
                  Use SEP as a group separator for context options -A, -B, and -C.
                  By default SEP is a double hyphen (`--').

           -H, --with-filename
                  Always print the  filename  with  output  lines.   This  is  the
                  default when there is more than one file to search.

           -h, --no-filename
                  Never print filenames with output lines.

           --help Print a help message.

           -i, --ignore-case
                  Perform   case   insensitive   matching.   This  option  applies
                  case-insensitive matching of ASCII characters in the input.   By
                  default, ugrep is case sensitive.

           --include=GLOB
                  Search  only  files whose path name matches GLOB (using wildcard
                  matching).  A path name glob can use *, ?, and  [...]  as  wild-
                  cards,  and  \ to quote a wildcard or backslash character liter-
                  ally.  Note that --exclude patterns take priority over --include
                  patterns.  This option may be repeated.

           --include-dir=GLOB
                  Only  directories  whose  path name matches GLOB are included in
                  recursive searches.  Note that --exclude-dir patterns take  pri-
                  ority over --include-dir patterns.  This option may be repeated.

           --include-from=FILE
                  Read the globs from FILE and search only files whose  path  name
                  matches  one  or more globs (using wildcard matching).  Comments
                  starting with a `#' and empty lines in FILE are ignored.

           -k, --column-number
                  The column number of a matched pattern is displayed in front  of
                  the  respective  matched  line,  starting at column 1.  Tabs are
                  expanded when columns are counted.

           -L, --files-without-match
                  Only the names of files not containing selected lines are  writ-
                  ten  to  standard  output.   Pathnames  are listed once per file
                  searched.   If  the  standard  input  is  searched,  the  string
                  ``(standard input)'' is written.

           -l, --files-with-matches
                  Only the names of files containing selected lines are written to
                  standard output.  ugrep will only search a file  until  a  match
                  has  been  found,  making  searches  potentially less expensive.
                  Pathnames are listed once per file searched.   If  the  standard
                  input is searched, the string ``(standard input)'' is written.

           --label[=LABEL]
                  Displays  the LABEL value when input is read from standard input
                  where a file name would normally be printed in the output.  This
                  option applies to options -H, -L, and -l.

           --line-buffered
                  Force  output  to  be line buffered.  By default, output is line
                  buffered when standard output is a terminal and  block  buffered
                  otherwise.

           -m NUM, --max-count=NUM
                  Stop reading the input after NUM matches.

           -N, --only-line-number
                  The  line number of the match in the file is output without dis-
                  playing the match.  The line number counter is  reset  for  each
                  file processed.

           -n, --line-number
                  Each  output line is preceded by its relative line number in the
                  file, starting at line 1.  The line number counter is reset  for
                  each file processed.

           --no-group-separator
                  Removes  the  group  separator  line from the output for context
                  options -A, -B, and -C.

           -O EXTENSIONS, --file-extensions=EXTENSIONS
                  Search only files whose file name extensions match the specified
                  comma-separated  list  of  file name EXTENSIONS.  This option is
                  the same as specifying --include='*.ext' for each extension name
                  `ext' in the EXTENSIONS list.  This option may be repeated.

           -o, --only-matching
                  Prints  only  the  matching part of the lines.  Allows a pattern
                  match to span  multiple  lines.   Line  numbers  for  multi-line
                  matches  are  displayed  with  option -n, using `|' as the field
                  separator for each additional line matched by the pattern.  Con-
                  text options -A, -B, and -C are disabled.

           -P, --perl-regexp
                  Interpret PATTERN as a Perl regular expression.  This feature is
                  not yet available.

           -p, --no-dereference
                  If -R is specified, no symbolic links are followed.  This is the
                  default.

           -q, --quiet, --silent
                  Quiet  mode:  suppress  normal output.  ugrep will only search a
                  file until a match has been found, making  searches  potentially
                  less  expensive.  Allows a pattern match to span multiple lines.

           -R, --dereference-recursive
                  Recursively read all files under  each  directory.   Follow  all
                  symbolic links, unlike -r.

           -r, --recursive
                  Recursively  read all files under each directory, following sym-
                  bolic links only if they are on the command line.

           -S, --dereference
                  If -R is  specified,  all  symbolic  links  are  followed.   The
                  default is not to follow symbolic links.

           -s, --no-messages
                  Silent mode.  Nonexistent and unreadable files are ignored (i.e.
                  their error messages are suppressed).

           -T, --initial-tab
                  Add a tab space to separate the file name, line  number,  column
                  number, and byte offset with the matched line.

           -t TYPES, --file-type=TYPES
                  Search  only  files of TYPES, which is a comma-separated list of
                  file types.  Each file type is associated with  a  set  of  file
                  name  extensions  to  search.  This option may be repeated.  The
                  possible values of type  can  be  (use  -t  list  to  display  a
                  detailed  list):  `actionscript',  `ada',  `asm', `asp', `aspx',
                  `autoconf', `automake', `awk', `basic', `batch',  `bison',  `c',
                  `c++',  `clojure',  `csharp',  `css',  `csv',  `dart', `delphi',
                  `erlang', `fortran', `go', `groovy', `haskell', `html',  `jade',
                  `java',  `javascript',  `json',  `jsp',  `julia', `less', `lex',
                  `lisp',  `lua',  `m4',  `make',  `matlab',   `objc',   `objcpp',
                  `ocaml',  `parrot', `pascal', `perl', `php', `prolog', `python',
                  `R',  `rst',  `ruby',  `rust',   `scala',   `scheme',   `shell',
                  `smalltalk',  `sql',  `swift', `tcl', `tex', `text', `tt', `ver-
                  ilog', `vhdl', `vim', `xml', `yacc', `yaml'

           --tabs=NUM
                  Set the tab size to NUM to expand tabs for option -k.  The value
                  of NUM may be 1, 2, 4, or 8.

           -V, --version
                  Display version information and exit.

           -v, --invert-match
                  Selected  lines are those not matching any of the specified pat-
                  terns.

           -w, --word-regexp
                  The pattern or -e patterns are searched for as  a  word  (as  if
                  surrounded by `\<' and `\>').

           -X, --free-space
                  Spacing (blanks and tabs) in regular expressions are ignored.

           -x, --line-regexp
                  Only  input lines selected against the entire pattern or -e pat-
                  terns are considered to be matching lines (as if surrounded by ^
                  and $).

           -Y ENCODING, --file-format=ENCODING
                  The  input file format.  The possible values of ENCODING can be:
                  `binary', `ISO-8859-1', `ASCII',  `EBCDIC',  `UTF-8',  `UTF-16',
                  `UTF-16BE',   `UTF-16LE',   `UTF-32',   `UTF-32BE',  `UTF-32LE',
                  `CP437',  `CP850',  `CP1250',  `CP1251',   `CP1252',   `CP1253',
                  `CP1254', `CP1255', `CP1256', `CP1257', `CP1258'

           -y     Equivalent to -i.  Obsoleted.

           -Z, --null
                  Prints a zero-byte after the file name.

           -z SEP, --separator=SEP
                  Use  SEP as field separator between file name, line number, col-
                  umn number, byte offset, and the matched line.  The default is a
                  colon (`:').

           The  regular expression pattern syntax is an extended form of the POSIX
           ERE syntax.  For an overview of the syntax see README.md or visit:

                  https://github.com/Genivia/ugrep

           Note that `.' matches any non-newline character.   Matching  a  newline
           character  is  not possible in line-buffered mode.  Pattern matches may
           span multiple lines in block-buffered mode, which is enabled by one  of
           the options -c, -o, or -q (unless combined with option -v).

           If  no  file arguments are specified, or if `-' is specified, the stan-
           dard input is used.

    EXIT STATUS
           The ugrep utility exits with one of the following values:

           0      One or more lines were selected.

           1      No lines were selected.

           >1     An error occurred.

    ENVIRONMENT
           GREP_PATH
                  May be used to specify a file path to pattern files.   The  file
                  path  is used by option -f to open a pattern file, when the file
                  specified with option -f cannot be opened.

           GREP_COLOR
                  May be used to specify ANSI SGR parameters to highlight  matches
                  when  option --color is used, e.g. 1;35;40 shows pattern matches
                  in bold magenta text on a black background.

           GREP_COLORS
                  May be used to specify ANSI SGR parameters to highlight  matches
                  and  other attributes when option --color is used.  Its value is
                  a colon-separated list of ANSI SGR parameters that  defaults  to
                  mt=1;31:sl=:cx=:fn=35:ln=32:cn=32:bn=32:se=36.   The  mt=,  ms=,
                  and  mc=  capabilities  of  GREP_COLORS   have   priority   over
                  GREP_COLOR.

    GREP_COLORS
           sl=    SGR substring for selected lines.

           cx=    SGR substring for context lines.

           rv     Swaps the sl= and cx= capabilities when -v is specified.

           mt=    SGR substring for matching text in any matching line.

           ms=    SGR  substring  for  matching text in a selected line.  The sub-
                  string mt= by default.

           mc=    SGR substring for matching text in a  context  line.   The  sub-
                  string mt= by default.

           fn=    SGR substring for file names.

           ln=    SGR substring for line numbers.

           cn=    SGR substring for column numbers.

           bn=    SGR substring for byte offsets.

           se=    SGR substring for separators.

    EXAMPLES
           To find all occurrences of the word `patricia' in a file:

                  $ ugrep -w 'patricia' myfile

           To  count the number of lines containing the word `patricia' or `Patri-
           cia` in a file:

                  $ ugrep -cw '[Pp]atricia' myfile

           To count the total number of times the word  `patricia'  or  `Patricia`
           occur in a file:

                  $ ugrep -cgw '[Pp]atricia' myfile

           To list all Unicode words in a file:

                  $ ugrep -o '\w+' myfile

           To list all ASCII words in a file:

                  $ ugrep -o '[[:word:]]+' myfile

           To  list  all  laughing  face  emojis  (Unicode  code points U+1F600 to
           U+1F60F) in a file:

                  $ ugrep -o '[\x{1F600}-\x{1F60F}]' myfile

           To check if a file contains any non-ASCII (i.e. Unicode) characters:

                  $ ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"

           To list all C/C++ comments in a file displaying their line  and  column
           numbers using options -n and -k, and option -o that allows for matching
           patterns across multiple lines:

                  $ ugrep -nko -e '//.*' -e '/\*([^*]|(\*+[^*/]))*\*+\/' myfile

           The same search, but using pre-defined patterns:

                  $ ugrep -nko -f patterns/c_comments myfile

           To list the lines that need fixing in a C/C++ source  file  by  looking
           for  the word FIXME while skipping any FIXME in quoted strings by using
           a negative pattern `(?^X)' to ignore quoted strings:

                  $ ugrep -no -e 'FIXME' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*")' myfile

    BUGS
           Report bugs at:

                  https://github.com/Genivia/ugrep/issues


    LICENSE
           ugrep  is  released under the BSD-3 license.  All parts of the software
           have reasonable copyright terms permitting free  redistribution.   This
           includes the ability to reuse all or parts of the ugrep source tree.

    SEE ALSO
           grep(1).



    ugrep 1.1.0                      May 09, 2019                         UGREP(1)

For future updates
------------------

- Skip hidden files and directories, e.g. dot files and Windows hidden files.
  However, skipping dot files and directories can already be done with
  `--exclude='.*'` and `--exclude-dir='.*'`, respectively.  Windows hidden
  files are defined by their attributes returned by GetFileAttributesA.
- Pattern `^$` does not match empty lines, because RE/flex `find()` does not
  permit empty matches.  This can be fixed in RE/flex, but requires some work
  and testing to avoid infinite `find()` loops on an empty match that does not
  advance the input cursor.
- Back-references are not supported.  This will likely not be supported soon
  with the RE/flex library.  We could use Boost.Regex for this (using RE/flex
  `BoostMatcher` class), which is faster than PCRE2 but slower than RE/flex
  `Matcher` class.  With Boost.Regex we can also support Perl-like matching
  as an option.
- There are reported cases where lazy quantifiers misbehave when used in
  negative patterns, so it is best to avoid them unless the patterns are
  simple.
- Not locale-sensitive, e.g. `LC_COLLATE` currently has no effect.

Pattern syntax
--------------

A pattern is an extended set of regular expressions, with nested sub-expression
patterns `φ` and `ψ`:

  Pattern   | Matches
  --------- | -----------------------------------------------------------------
  `x`       | matches the character `x`, where `x` is not a special character
  `.`       | matches any single character except newline (unless in dotall mode)
  `\.`      | matches `.` (dot), special characters are escaped with a backslash
  `\n`      | matches a newline, others are `\a` (BEL), `\b` (BS), `\t` (HT), `\v` (VT), `\f` (FF), and `\r` (CR)
  `\0`      | matches the NUL character
  `\cX`     | matches the control character `X` mod 32 (e.g. `\cA` is `\x01`)
  `\0177`   | matches an 8-bit character with octal value `177`
  `\x7f`    | matches an 8-bit character with hexadecimal value `7f`
  `\x{3B1}` | matches Unicode character U+03B1, i.e. `α`
  `\u{3B1}` | matches Unicode character U+03B1, i.e. `α`
  `\p{C}`   | matches a character in category C
  `\Q...\E` | matches the quoted content between `\Q` and `\E` literally
  `[abc]`   | matches one of `a`, `b`, or `c`
  `[0-9]`   | matches a digit `0` to `9`
  `[^0-9]`  | matches any character except a digit
  `φ?`      | matches `φ` zero or one time (optional)
  `φ*`      | matches `φ` zero or more times (repetition)
  `φ+`      | matches `φ` one or more times (repetition)
  `φ{2,5}`  | matches `φ` two to five times (repetition)
  `φ{2,}`   | matches `φ` at least two times (repetition)
  `φ{2}`    | matches `φ` exactly two times (repetition)
  `φ??`     | matches `φ` zero or once as needed (lazy optional)
  `φ*?`     | matches `φ` a minimum number of times as needed (lazy repetition)
  `φ+?`     | matches `φ` a minimum number of times at least once as needed (lazy repetition)
  `φ{2,5}?` | matches `φ` two to five times as needed (lazy repetition)
  `φ{2,}?`  | matches `φ` at least two times or more as needed (lazy repetition)
  `φψ`      | matches `φ` then matches `ψ` (concatenation)
  `φ⎮ψ`     | matches `φ` or matches `ψ` (alternation)
  `(φ)`     | matches `φ` as a group
  `(?:φ)`   | matches `φ` as a group without capture
  `(?=φ)`   | matches `φ` without consuming it, i.e. lookahead (top-level `φ`, not for sub-patterns `φ`)
  `(?^φ)`   | matches `φ` and ignore it to continue matching (top-level `φ`, not for sub-patterns `φ`)
  `^φ`      | matches `φ` at the start of input or start of a line (top-level `φ`, not for sub-patterns `φ`)
  `φ$`      | matches `φ` at the end of input or end of a line (top-level `φ`, not for sub-patterns `φ`)
  `\Aφ`     | matches `φ` at the start of input (requires option `-o`) (top-level `φ`, not for sub-patterns `φ`)
  `φ\z`     | matches `φ` at the end of input (requires option `-o`) (top-level `φ`, not for sub-patterns `φ`)
  `\bφ`     | matches `φ` starting at a word boundary (top-level `φ`, not for sub-patterns `φ`)
  `φ\b`     | matches `φ` ending at a word boundary (top-level `φ`, not for sub-patterns `φ`)
  `\Bφ`     | matches `φ` starting at a non-word boundary (top-level `φ`, not for sub-patterns `φ`)
  `φ\B`     | matches `φ` ending at a non-word boundary (note: top-level regex pattern only)
  `\<φ`     | matches `φ` that starts a word (top-level `φ`, not for sub-patterns `φ`)
  `\>φ`     | matches `φ` that starts a non-word (top-level `φ`, not for sub-patterns `φ`)
  `φ\<`     | matches `φ` that ends a non-word (top-level `φ`, not for sub-patterns `φ`)
  `φ\>`     | matches `φ` that ends a word (top-level `φ`, not for sub-patterns `φ`)
  `\i`      | matches an indent
  `\j`      | matches a dedent
  `(?i:φ)`  | matches `φ` ignoring case
  `(?s:φ)`  | `.` (dot) in `φ` matches newline
  `(?x:φ)`  | ignore all whitespace and comments in `φ`
  `(?#:X)`  | all of `X` is skipped as a comment

The order of precedence for composing larger patterns from sub-patterns is as
follows, from high to low precedence:

  1. Characters, character classes (bracket expressions), escapes, quotation
  2. Grouping `(φ)`, `(?:φ)`, `(?=φ)`, and inline modifiers `(?imsux:φ)`
  3. Quantifiers `?`, `*`, `+`, `{n,m}`
  4. Concatenation `φψ`
  5. Anchoring `^`, `$`, `\<`, `\>`, `\b`, `\B`, `\A`, `\z` 
  6. Alternation `φ|ψ`
  7. Global modifiers `(?imsux)φ`

### POSIX and Unicode character classes

Character classes in bracket lists represent sets of characters.  Sets can be
inverted, subtracted, intersected, and merged:

  Pattern           | Matches
  ----------------- | ---------------------------------------------------------
  `[a-zA-Z]`        | matches a letter
  `[^a-zA-Z]`       | matches a non-letter (character class inversion)
  `[a-z−−[aeiou]]`  | matches a consonant (character class subtraction)
  `[a-z&&[^aeiou]]` | matches a consonant (character class intersection)
  `[a-z⎮⎮[A-Z]]`    | matches a letter (character class union)

Bracket lists cannot be empty, so `[]` and `[^]` are invalid.  In fact, the
first character after the bracket is always part of the list.  So `[][]` is a
list that matches a `]` and a `[`, `[^][]` is a list that matches anything but
`]` and `[`, and `[-^]` is a list that matches a `-` and a `^`.

### POSIX and Unicode character categories

  POSIX form   | POSIX category    | Matches
  ------------ | ----------------- | ---------------------------------------------
  `[:ascii:]`  | `\p{ASCII}`       | matches any ASCII character
  `[:space:]`  | `\p{Space}`       | matches a white space character `[ \t\n\v\f\r]`
  `[:xdigit:]` | `\p{Xdigit}`      | matches a hex digit `[0-9A-Fa-f]`
  `[:cntrl:]`  | `\p{Cntrl}`       | matches a control character `[\x00-\0x1f\x7f]`
  `[:print:]`  | `\p{Print}`       | matches a printable character `[\x20-\x7e]`
  `[:alnum:]`  | `\p{Alnum}`       | matches a alphanumeric character `[0-9A-Za-z]`
  `[:alpha:]`  | `\p{Alpha}`       | matches a letter `[A-Za-z]`
  `[:blank:]`  | `\p{Blank}`, `\h` | matches a blank `[ \t]`
  `[:digit:]`  | `\p{Digit}`, `\d` | matches a digit `[0-9]`
  `[:graph:]`  | `\p{Graph}`       | matches a visible character `[\x21-\x7e]`
  `[:lower:]`  |                   | matches a lower case letter `[a-z]`
  `[:punct:]`  | `\p{Punct}`       | matches a punctuation character `[\x21-\x2f\x3a-\x40\x5b-\x60\x7b-\x7e]`
  `[:upper:]`  |                   | matches an upper case letter `[A-Z]`
  `[:word:]`   |                   | matches a word character `[0-9A-Za-z_]`
  `[:^blank:]` | `\H`              | matches a non-blank character `[^ \t]`
  `[:^digit:]` | `\D`              | matches a non-digit `[^0-9]`

The POSIX form can only be used in bracket lists, for example
`[[:lower:][:digit:]]` matches an ASCII lower case letter or a digit.  

You can also use the capitalized `\P{C}` form that has the same meaning as
`\p{^C}`, which matches any character except characters in the class `C`.
For example, `\P{ASCII}` is the same as `\p{^ASCII}` which is the same as
`[^[:ascii:]]`.  A word of caution: because POSIX character categories only
cover ASCII, `[[:^ascii]]` is empty and invalid to use.  By contrast,
`[^[:ascii]]` is a Unicode character class that excludes the ASCII character
category.

  Unicode category                       | Matches
  -------------------------------------- | ------------------------------------
  `.`                                    | matches any single Unicode character except newline
  `\X`                                   | matches any ISO-8859-1 or Unicode character
  `\R`                                   | matches a Unicode line break
  `\s`, `\p{Zs}`                         | matches a white space character with Unicode sub-propert Zs
  `\l`, `\p{Ll}`                         | matches a lower case letter with Unicode sub-property Ll
  `\u`, `\p{Lu}`                         | matches an upper case letter with Unicode sub-property Lu
  `\w`, `\p{Word}`                       | matches a Unicode word character with property L, Nd, or Pc
  `\p{Unicode}`                          | matches any Unicode character (U+0000 to U+10FFFF minus U+D800 to U+DFFF)
  `\p{ASCII}`                            | matches an ASCII character U+0000 to U+007F)
  `\p{Non_ASCII_Unicode}`                | matches a non-ASCII character U+0080 to U+10FFFF minus U+D800 to U+DFFF)
  `\p{Letter}`                           | matches a character with Unicode property Letter
  `\p{Mark}`                             | matches a character with Unicode property Mark
  `\p{Separator}`                        | matches a character with Unicode property Separator
  `\p{Symbol}`                           | matches a character with Unicode property Symbol
  `\p{Number}`                           | matches a character with Unicode property Number
  `\p{Punctuation}`                      | matches a character with Unicode property Punctuation
  `\p{Other}`                            | matches a character with Unicode property Other
  `\p{Lowercase_Letter}`, `\p{Ll}`       | matches a character with Unicode sub-property Ll
  `\p{Uppercase_Letter}`, `\p{Lu}`       | matches a character with Unicode sub-property Lu
  `\p{Titlecase_Letter}`, `\p{Lt}`       | matches a character with Unicode sub-property Lt
  `\p{Modifier_Letter}`, `\p{Lm}`        | matches a character with Unicode sub-property Lm
  `\p{Other_Letter}`, `\p{Lo}`           | matches a character with Unicode sub-property Lo
  `\p{Non_Spacing_Mark}`, `\p{Mn}`       | matches a character with Unicode sub-property Mn
  `\p{Spacing_Combining_Mark}`, `\p{Mc}` | matches a character with Unicode sub-property Mc
  `\p{Enclosing_Mark}`, `\p{Me}`         | matches a character with Unicode sub-property Me
  `\p{Space_Separator}`, `\p{Zs}`        | matches a character with Unicode sub-property Zs
  `\p{Line_Separator}`, `\p{Zl}`         | matches a character with Unicode sub-property Zl
  `\p{Paragraph_Separator}`, `\p{Zp}`    | matches a character with Unicode sub-property Zp
  `\p{Math_Symbol}`, `\p{Sm}`            | matches a character with Unicode sub-property Sm
  `\p{Currency_Symbol}`, `\p{Sc}`        | matches a character with Unicode sub-property Sc
  `\p{Modifier_Symbol}`, `\p{Sk}`        | matches a character with Unicode sub-property Sk
  `\p{Other_Symbol}`, `\p{So}`           | matches a character with Unicode sub-property So
  `\p{Decimal_Digit_Number}`, `\p{Nd}`   | matches a character with Unicode sub-property Nd
  `\p{Letter_Number}`, `\p{Nl}`          | matches a character with Unicode sub-property Nl
  `\p{Other_Number}`, `\p{No}`           | matches a character with Unicode sub-property No
  `\p{Dash_Punctuation}`, `\p{Pd}`       | matches a character with Unicode sub-property Pd
  `\p{Open_Punctuation}`, `\p{Ps}`       | matches a character with Unicode sub-property Ps
  `\p{Close_Punctuation}`, `\p{Pe}`      | matches a character with Unicode sub-property Pe
  `\p{Initial_Punctuation}`, `\p{Pi}`    | matches a character with Unicode sub-property Pi
  `\p{Final_Punctuation}`, `\p{Pf}`      | matches a character with Unicode sub-property Pf
  `\p{Connector_Punctuation}`, `\p{Pc}`  | matches a character with Unicode sub-property Pc
  `\p{Other_Punctuation}`, `\p{Po}`      | matches a character with Unicode sub-property Po
  `\p{Control}`, `\p{Cc}`                | matches a character with Unicode sub-property Cc
  `\p{Format}`, `\p{Cf}`                 | matches a character with Unicode sub-property Cf
  `\p{UnicodeIdentifierStart}`           | matches a character in the Unicode IdentifierStart class
  `\p{UnicodeIdentifierPart}`            | matches a character in the Unicode IdentifierPart class
  `\p{IdentifierIgnorable}`              | matches a character in the IdentifierIgnorable class
  `\p{JavaIdentifierStart}`              | matches a character in the Java IdentifierStart class
  `\p{JavaIdentifierPart}`               | matches a character in the Java IdentifierPart class
  `\p{CsIdentifierStart}`                | matches a character in the C# IdentifierStart class
  `\p{CsIdentifierPart}`                 | matches a character in the C# IdentifierPart class
  `\p{PythonIdentifierStart}`            | matches a character in the Python IdentifierStart class
  `\p{PythonIdentifierPart}`             | matches a character in the Python IdentifierPart class

To specify a Unicode block as a category use `\p{IsBlockName}` with a Unicode
`BlockName`.

To specify a Unicode language script, use `\p{Language}` with a Unicode
`Language`.

Unicode language script character classes differ from the Unicode blocks that
have a similar name.  For example, the `\p{Greek}` class represents Greek and
Coptic letters and differs from the Unicode block `\p{IsGreek}` that spans a
specific Unicode block of Greek and Coptic characters only, which also includes
unassigned characters.
