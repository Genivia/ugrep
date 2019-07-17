ugrep: universal grep
=====================

Quickly grep through C/C++, Java, Python, Ruby, JSON, XML and more using
pre-defined search patterns.  Search files for Unicode text patterns, find
source code matches, and search and display text and binary files recursively
in large directory trees.

See the [examples](#examples) that to show the power of **ugrep**.

- **ugrep** is backward compatible with GNU grep and BSD grep, extending these
  utilities by offering additional features, such as full Unicode pattern
  matching, "negative patterns" to ignore unwanted pattern matches, recursive
  search through directories while selecting files by file name extension and
  file signature "magic" bytes or shebangs, pre-defined search patterns to
  search source code, hexdumps for binary matches, and more.

- **ugrep** makes it simple to search source code using *pre-defined patterns*.
  For example to recursively search Python files for import statements:

      ugrep -R -tpython -n -f python/imports myprojects

  where `-R` is recursive search, `-tpython` searches Python source code files
  only, `-n` shows line numbers in the output, and the `-f` option specifies
  pre-defined patterns to search for Python `import` statements (matched by the
  two patterns `\<import\h+.*` and `\<from\h+.*import\h+.*` defined in
  `patterns/python/imports`).

- **ugrep** includes a growing database of
  [patterns](https://github.com/Genivia/ugrep/tree/master/patterns) with common
  search patterns to use with option `-f`.  So you don't need to memorize
  complex regex patterns for common search criteria.  Environment variable
  `GREP_PATH` can be set to point to your own directory with patterns that
  option `-f` uses to read your pattern files.

- **ugrep** is the only grep tool that allows you to define *negative patterns*
  to *zap* parts in files you want to skip.  This removes many false positives.
  For example to find exact matches of `main` in C/C++ source code while
  skipping strings and comments that may have a match with `main` in them:

      ugrep -R -o -tc,c++ -n -w 'main' -f c/zap_strings -f c/zap_comments myprojects

  where `-R` is recursive search, `-o` for multi-line matches (since strings
  and comments may span multiple lines), `-tc,c++` searches C and C++ source
  code files only, `-n` shows line numbers in the output, `-w` matches exact
  words (for example, `mainly` won't be matched), and the `-f` options specify
  two pre-defined patterns to match and ignore strings and comments in the
  input.

- **ugrep** searches text files and binary files and produces hexdumps for
  binary matches.  For example, to search for a binary pattern:

      ugrep --color -X -U '\xed\xab\xee\xdb' some.rpm

  where `-X` produces hexadecimal output, `-U` specifies a binary pattern to
  search (meaning non-Unicode), and `--color` shows the results in color.
  Other options that normally work with text matches work with `-X` too, such
  as the context options `-A`, `-B`, `-C`, and `-y`.

- **ugrep** matches Unicode patterns by default (disabled with option `-U`).  The
  [regular expression pattern syntax](#pattern) is POSIX ERE compliant,
  extended with Unicode character classes, lazy quantifiers, and negative
  patterns to skip unwanted pattern matches to produce more precise results.

- **ugrep** searches UTF-encoded input when UTF BOM
  ([byte order mark](https://en.wikipedia.org/wiki/Byte_order_mark)) are
  present and ASCII and UTF-8 when no UTF BOM is present.  Option `--encoding`
  permits many other file formats to be searched, such as ISO-8859-1, EBCDIC,
  and code pages 437, 850, 858, 1250 to 1258.

- **ugrep** uses [RE/flex](https://github.com/Genivia/RE-flex) for
  high-performance regex matching, which is 100 times faster than the GNU C
  POSIX.2 regex library used by GNU grep and 10 times faster than PCRE2 and
  RE2.  RE/flex is an incremental, streaming regex matcher, meaning it does not
  read all the input at once nor does it require reading the input
  line-by-line.  Files are efficiently scanned with options such as `-o`.  As a
  bonus, this option also finds a match of a pattern spanning multiple lines
  such as comment blocks in source code.

- **ugrep** regex patterns are converted to
  [DFAs](https://en.wikipedia.org/wiki/Deterministic_finite_automaton) for fast
  matching.  DFAs yield significant speedups when searching multiple files and
  large files.  Rare and pathelogical cases are known to exist that may
  increase the initial running time of **ugrep** for complex DFA construction.

- **ugrep** is portable and compiles with MSVC++ to run on Windows.  Binaries are
  available for Linux, Mac and Windows.

- **ugrep** is free [BSD-3](https://opensource.org/licenses/BSD-3-Clause) source
  code and does not include any GNU or BSD grep open source code or algorithms.
  **ugrep** is built on the RE/flex open source library.

- **ugrep** is evolving and more features will be added.  You can help!  We love
  your feedback (issues) and contributions (pull requests) ❤️  For example, what
  patterns do you use to search source code?  Please contribute and share!

Speed
-----

Initial performance results look promising.  For example, searching for all
matches of syntactically-valid variants of `#include "..."` in the directory
tree from the Qt 5.9.2 root, restricted to `.h`, `.hpp`, and `.cpp` files only:

    time grep -R -o -E '#[[:space:]]*include[[:space:]]+"[^"]+"' --include='*.h' --include='*.hpp' --include='*.cpp' . >& /dev/null
    3.630u 0.274s 0:03.90 100.0%    0+0k 0+0io 0pf+0w

    time ugrep -R -o '#[[:space:]]*include[[:space:]]+"[^"]+"' -Oh,hpp,cpp . >& /dev/null
    1.412u 0.270s 0:01.68 100.0%    0+0k 0+0io 0pf+0w

Unoptimized (single threaded), **ugrep** is already much faster than BSD grep
(**ugrep** was compiled with clang 9.0.0 -O2, and this test was run on a 2.9
GHz Intel Core i7, 16 GB 2133 MHz LPDDR3 machine).

Installation
------------

Binaries for Linux, Mac OS X, and Windows are included in the `bin` directory.

To build ugrep, first install RE/flex 1.2.5 or greater from
https://github.com/Genivia/RE-flex then download ugrep from
https://github.com/Genivia/ugrep and execute:

    $ ./configure; make

This builds `ugrep` in the `src` directory.  You can tell which version it is
with:

    $ src/ugrep -V
    ugrep 1.1.8 x86_64-apple-darwin16.7.0

Optionally, install the ugrep utility and the ugrep manual page:

    $ sudo make install
    $ ugrep -V
    ugrep 1.1.8 x86_64-apple-darwin16.7.0

This also installs the pattern files with pre-defined patterns for option `-f`
at `/usr/local/share/ugrep/patterns/`.  Option `-f` first checks the current
directory for the presence of pattern files, if not found checks environment
variable `GREP_PATH` to load the pattern files, and if not found reads the
installed pre-defined pattern files.

Using ugrep within Vim
----------------------

Add to `.vimrc`:

    if executable('ugrep')
        set grepprg=ugrep\ -Rnk\ -g\ --tabs=1
        set grepformat=%f:%l:%c:%m,%f+%l+%c+%m,%-G%f\\\|%l\\\|%c\\\|%m
    endif

Within Vim you can now use **ugrep** with `:grep` to search one or more files
on `PATH` for `PATTERN` matches:

    :grep PATTERN PATH

This shows the results in a
[quickfix](http://vimdoc.sourceforge.net/htmldoc/quickfix.html#:grep) window
that allows you to quickly jump to the matches found.

To open a quickfix window with the latest list of matches:

    :copen

Click on a line in this window (or select a line and press ENTER) to jump to
the file and location in the file of the match.  To add search results to the
window, grep them.  For example, to recursively search C++ source code marked
`FIXME` in the current directory:

    :grep -tc++ FIXME .

To close the quickfix window:

    :cclose

Note that multiple matches on the same line are listed in the quickfix window
separately.  If this is not desired then remove `\ -g` from `grepprg`.
With this change, only the first match on a line is shown, like GNU/BSD grep.

You can use **ugrep** options with the Vim `:grep` command, for example to
select single- and multi-line comments in `main.cpp`:

    :grep -o -f c++/comments main.cpp

Only the first line of each multi-line comment is shown as a match in quickfix,
to save space.

ugrep versus other greps
------------------------

- **ugrep** accepts GNU/BSD grep command options and produces GNU/BSD grep
  compatible results, making **ugrep** a true drop-in replacement.
- **ugrep** matches Unicode by default, disabled with option `-U`.
- **ugrep** regular expression patterns are more expressive than GNU grep and
  BSD grep and support Unicode pattern matching and most of the PCRE syntax
  with some restrictions, see further below.  Extended regular expression
  syntax is the default (i.e.  option `-E`, as egrep).
- **ugrep** uses incremental matching.  When one or more of the options `-q`
  (quiet), `-o` (only matching), `-c` (count), `-N` (only line number), `-l`
  (file with match), or `-L` (files without match) is used, **ugrep** performs
  an even faster search of the input file instead of reading the input
  line-by-line as other grep tools do.  This allows matching patterns that
  span multiple lines.  This is not possible with other grep-like tools.
- **ugrep** supports *negative patterns* of the form `(?^X)` to skip input
  that matches `X`.  Negative patterns can be used to skip strings and comments
  when searching for identifiers in source code and find matches that aren't in
  strings and comments.
- New options to produce hexdumps, `-W` (output binary matches in hex with
  text matches output as usual) and `-X` (output all matches in hex).
- New option `-Y` to permit matching empty patterns.  Grepping with
  empty-matching patterns is weird and gives different results with GNU grep
  versus BSD grep.  Empty matches are not output by **ugrep** by default, which
  avoids making mistakes that may produce "random" results.  For example, with
  BSD/GNU grep, pattern `a*` matches every line in the input, and actually
  matches `xyz` three times (the empty transitions before and between the `x`,
  `y`, and `z`).  Allowing empty matches requires **ugrep** option `-Y`.
- New option `-U` to specify non-Unicode pattern matches, e.g. to search for
  binary patterns.
- New option `-k`, `--column-number` to display the column number, taking tab
  spacing into account by expanding tabs, as specified by option `--tabs`.
- New option `-g`, `--no-group` to not group matches per line.  This option
  displays a matched input line again for each additional pattern match.  This
  option is particularly useful with option `-c` to report the total number of
  pattern matches per file instead of the number of lines matched per file.
- New options `-O`, `-M`, and `-t` to specify file extensions, file signature
  magic byte patterns, and pre-defined file types, respectively.  This allows
  searching for certain types of files in directory trees with recursive search
  options `-R` and `-r`.
- Extended option `-f` uses `GREP_PATH` environment variable or the pre-defined
  patterns intalled in `/usr/local/share/ugrep/patterns`.
- When option `-b` is used with option `-o` or with option `-g`, **ugrep**
  displays the exact byte offset of the pattern match instead of the byte
  offset of the start of the matched line reported by BSD/GNU grep.  Reporting
  exact byte offsets is now possible with **grep**.
- **ugrep** always assumes UTF-8 locale to support Unicode, e.g.
  `LANG=en_US.UTF-8`, wheras grep is locale-sensitive.
- BSD grep (e.g. on Mac OS X) has limitations and some bugs that **ugrep**
  fixes (options `-r` versus `-R`), support for `GREP_COLORS`, and more.
- **ugrep** does not (yet) support backreferences and lookbehinds.

GNU and BSD grep and their common variants are equivalent to **ugrep** when the
following options are used (note that `-U` disables Unicode as GNU/BSD grep do
not support Unicode!):

    grep   = ugrep -G -U -Y
    egrep  = ugrep -E -U -Y
    fgrep  = ugrep -F -U -Y

    zgrep  = ugrep -G -U -Y -z
    zegrep = ugrep -E -U -Y -z
    zfgrep = ugrep -F -U -Y -z

Some useful aliases (add or remove `--color` and/or `--pager` as desired):

    alias ug     ugrep --color --pager        # short & quick text pattern search
    alias ux     ugrep --color --pager -UX    # short & quick binary pattern search

    alias grep   ugrep --color --pager -G     # search with basic regular expressions (BRE)
    alias egrep  ugrep --color --pager -E     # search with extended regular expressions (ERE)
    alias fgrep  ugrep --color --pager -F     # find string

    alias xgrep  ugrep --color --pager -W     # search and output text or hex binary
    alias uxgrep ugrep --color --pager -UX    # search binary patterns, output hex
    alias uxdump ugrep --color --pager -Xo '' # hexdump entire file

<a name="examples"/>
Examples
--------

To search for the identifier `main` as a word (`-w`) recursively (`-r`) in
directory `myproject`, showing the matching line (`-n`) and column (`-k`)
numbers next to the lines matched:

    ugrep -r -n -k -w 'main' myproject

This search query also finds `main` in strings and comment blocks.  With
**ugrep** we can use *negative patterns* of the form `(?^...)` to ignore
unwanted matches in C/C++ quoted strings and comment blocks.  Because strings
and comment blocks may span multiple lines, we should use `-o`:

    ugrep -r -o -nkw 'main' '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*([^*]|(\*+[^*/]))*\*+\/)' myproject

This is a lot of work to type in correctly!  If you are like me, I'm lazy and
don't want to spend time fiddling with regex patterns when I am working on
something more important.  There is an easier way by using **ugrep**'s
pre-defined patterns (`-f`):

    ugrep -r -o -nkw 'main' -f c/zap_strings -f c/zap_comments myproject

This query also searches through other files than C/C++ source code, like
READMEs, Makefiles, and so on.  We're also skipping symlinks with `-r`.  So
let's refine this query by selecting C/C++ files only using option `-tc,c++`
and include symlinks to files and directories with `-R`:

    ugrep -R -o -tc,c++ -nkw 'main' -f c/zap_strings -f c/zap_comments myproject

What if we are only looking for the identifier `main` but not as a function
`main(`?  We can use a negative pattern for this to ignore unwanted `main\h*(`
pattern matches:

    ugrep -R -o -tc,c++ -nkw 'main' -e '(?^main\h*\()' -f c/zap_strings -f c/zap_comments myproject

This uses the `-e` option to explicitly specify more patterns, which is
essentially forming the pattern `main|(?^main\h*\()', where `\h` matches space
and tab.  In general, negative patterns are useful to filter out pattern
matches we are not interested in.

As another example, we may want to search for the word `FIXME` in C/C++ comment
blocks.  To do so we can first select the comment blocks with **ugrep**'s
pre-defined `c/comments` pattern AND THEN select lines with `FIXME` using a
pipe:

    ugrep -R -o -tc,c++ -nk -f c/comments myproject | ugrep -w 'FIXME'

Filtering results with pipes is generally easier than using AND-OR logic that
some search tools use.  This approach follows the Unix spirit to keep utilities
simple and use them in combination for more complex tasks.

Say we want to produce a sorted list of all identifiers found in Java source
code while skipping strings and comments:

    ugrep -R -o -tjava -f java/names -f java/zap_strings -f java/zap_comments myproject | sort -u

This matches Java Unicode identifiers using the regex
`\p{JavaIdentifierStart}\p{JavaIdentifierPart}*` defined in
`patterns/java/names`.

With traditional grep and grep-like tools it takes great effort to recursively
search for the C/C++ source file that defines function `qsort`, requiring
something like this:

    ugrep -R --include='*.c' --include='*.cpp' '^([ \t]*[[:word:]:*&]+)+[ \t]+qsort[ \t]*\([^;\n]+$' myproject

Fortunately, with **ugrep** we can simply select all function definitions in
files with extension `.c` or `.cpp` by using option `-Oc,cpp` and by using a
pre-defined pattern `function_defs` to produce all function definitions.  Then
we select the one we want:

    ugrep -R -o -Oc,cpp -nk -f c/function_defs myproject | ugrep 'qsort'

Note that we could have used `-tc,c++` to select C/C++ files, but this also
includes header files when we want to only search `.c` and `.cpp` files.  To
display the list of file name extensions searched for all available options for
`-t` use:

    ugrep -tlist

We can also skip files and directories from being searched that are defined in
`.gitignore`.  To do so we use `--exclude-from` to specify a file containing
glob patterns to match files and directories we want to ignore:

    ugrep -R -tc++ --color --exclude-from='.gitignore' -f c++/defines .

This searches C++ files (`-tc++`) in the current directory (`.`) for `#define`
lines (`-f c++/defines`), while skipping files and directories
declared in `.gitignore` such as `config.h`.

To highlight matches when pushed through a chain of pipes we should use
`--color=always`:

    ugrep -R -tc++ --color=always --exclude-from='.gitignore' -f c++/defines . | ugrep -w 'FOO.*'

This returns a color-highlighted list of all `#define FOO...` macros in our C++
project in the current directory, skipping files defined in `.gitignore`.

To list all files in a GitHub project directory that are not ignored by
the `.gitignore` file located in the current directory:

    ugrep -R -l '' --exclude-from='.gitignore' .

Where `-l` (files with matches) lists the files specified in `.gitignore`
matched by the empty pattern `''`, which is typically used to match any
non-empty file (non-zero-length file, as per POSIX.1 grep standard).

Note that the complement of `--exclude` is not `--include`, so we cannot
reliably list the files that are ignored with `--include-from='.gitignore'`.
Only files explicitly specified with `--include` and directories explicitly
specified with `--include-dir` are visited.  The `--include-from` from lists
globs that are considered both files and directories to add to `--include` and
`--include-dir`, respectively.  This means that when directory names and
directory paths are not explicitly listed in this file then it will not be
visited using `--include-from`.

### Displaying helpful info

The ugrep man page:

    man ugrep

To show a help page:

    ugrep --help

To show a list of `-t TYPES` option values:

    ugrep -t list

### Recursively list matching files with options -R/-r and -L/-l

To recursively list all readable non-empty files on the path specified:

    ugrep -R -l '' mydir

To recursively list all readable empty files on the path specified:

    ugrep -R -L '' mydir

To recursively list all readable non-empty files on the path specified, while
visiting sub-directories only, i.e. directories `mydir/` and `mydir/sub/` are
visited:

    ugrep -R -l --max-depth=2 '' mydir

To recursively list files with empty and blank lines, i.e. lines with white space only:

    ugrep -R -l -Y '^\h*$' mydir

To recursively list all files that have extension .sh

    ugrep -R -l -Osh '' mydir

To recursively list all shell scripts based on extensions and shebangs:

    ugrep -R -l -tShell '' mydir

To recursively list all shell scripts based on extensions only:

    ugrep -R -l -tshell '' mydir

To recursively list all files that are not ignored by `mydir/.gitignore`:

    ugrep -R -l '' --exclude-from='mydir/.gitignore' mydir

To recursively list all shell scripts that are not ignored by
`mydir/.gitignore`:

    ugrep -R -l -tShell '' --exclude-from='mydir/.gitignore' mydir

### Searching ASCII and Unicode files

To recursively list files that are ASCII in the current directory:

    ugrep -R -l '[[:ascii:]]' .

To recursively list files that are non-ASCII in the current directory:

    ugrep -R -l '[^[:ascii:]]' .

To recursively list files with invalid UTF content (i.e. invalid UTF-8 byte
sequences or that contain any UTF-8/16/32 code points that are outside the
valid Unicode range):

    ugrep -R -l '.|(?^\p{Unicode})' .

To display lines containing laughing face emojis:

    ugrep '[😀-😏]' emojis.txt

The same results are obtained using `\x{hhhh}` to select a Unicode character
range:

    ugrep '[\x{1F600}-\x{1F60F}]' emojis.txt

To display lines containing the names Gödel (or Goedel), Escher, or Bach:

    ugrep 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To search for `lorem` in lower or upper case in a UTF-16 file that is marked
with a UTF-16 BOM:

    ugrep --color -i -w 'lorem' utf16lorem.txt

To search utf16lorem.txt when this file has no UTF-16 BOM:

    ugrep --encoding=UTF-16 -i -w 'lorem' utf16lorem.txt

To check that a file contains Unicode:

    ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"

### Matching multiple lines of text with -o

To match C/C++ /*...*/ multi-line comments:

    ugrep -o '/\*([^*]|(\*+[^*/]))*\*+\/' myfile.cpp

To match C/C++ comments using the pre-defined `c/comments` patterns:

    ugrep -o -f c/comments myfile.cpp

### Searching source code using -f, -o, and -t

To recursively display function definitions in C/C++ files:

    ugrep -R -o -f c++/function_defs -tc,c++ .

To search for `FIXME` in C/C++ comments, excluding `FIXME` in multi-line
strings:

    ugrep -o 'FIXME' -f c++/zap_strings myfile.cpp

To display XML element and attribute tags in an XML file, exluding tags that
are placed in (multi-line) comments:

    ugrep -o -f xml/tags -f xml/zap_comments myfile.xml

### Searching binary files with -U, -W, and -X

To search a file for ASCII words, displaying text lines as usual while binary
content is shown in hex:

    ugrep --color -U -W '\w+' myfile

To hexdump an entire file:

    ugrep --color -X -o '' myfile

To hexdump an entire file line-by-line, displaying line numbers and line
breaks:

    ugrep --color -X -n '' myfile

To hexdump lines containing one or more \0 in a (binary) file using a
non-Unicode pattern:

    ugrep --color -U -X '\x00+' myfile

To match the binary pattern `A3hhhhA3hh` (hex) in a binary file without
Unicode pattern matching (which would otherwise match `\xaf` as a Unicode
character U+00A3 with UTF-8 byte sequence C2 A3) and display the results
in hex with `-X` with pager `less -R`:

    ugrep --color --pager -o -X -U '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

To list all files containing a RPM signature, located in the `rpm` directory and
recursively below (see for example
[list of file signatures](https://en.wikipedia.org/wiki/List_of_file_signatures)):

    ugrep -R -l -U '\A\xed\xab\xee\xdb' rpm

### Displaying context with -A, -B, -C, and -y

To display 2 lines of context before and after a matching line (note that `-C2`
should not be specified as `-C 2` as per GNU/BSD grep exception that ugrep
obeys, because `-C` specifies 3 lines of context by default):

    ugrep --color -C2 'FIXME' myfile.cpp

To show three lines of context after a matched line:

    ugrep --color -A3 'FIXME.*' myfile.cpp:

To display one line of context before each matching line with a C function
definition (C names are non-Unicode):

    ugrep --color -B1 -f c/function_defs myfile.c

To display one line of context before each matching line with a C++ function
definition (C++ names may be Unicode):

    ugrep --color -B1 -f c++/function_defs myfile.cpp

To display any non-matching lines as context for matching lines:

    ugrep --color -y -f c++/function_defs myfile.cpp

To display a hexdump of a matching line with one line of hexdump context:

    ugrep --color -C1 -U -X '\xaa\xbb\xcc' a.out

### Using gitignore-style globs to select directories and files to search

To list files with names starting with `foo` in the current directory, that
contain `xyz`:

    ugrep -s -l 'xyz' foo*

The same is obtained using recursion with a directory inclusion contraint:

    ugrep -R -l 'xyz' --include-dir='/foo*' .

To recursively list files in the current directory, `docs`, and `docs/latest`,
   but not below, that contain `xyz`:

    ugrep -s -l 'xyz' * docs/* docs/latest/*

To recursively list files in directory `docs/latest` and below, that contain
`xyz`:

    ugrep -R -l 'xyz' docs/latest

To only list files in the current directory and sub-directory `docs` but not
below, that contain `xyz`:

    ugrep -R -l 'xyz' --include-dir='docs' .

To only list files in the current directory and in the sub-directories `docs`
and `docs/latest` but not below, that contain `xyz`:

    ugrep -R -l xyzr' --include-dir='docs' --include-dir='docs/latest' .

To only list files that are on a sub-directory path that includes sub-directory
`docs` anywhere, that contain `xyz':

    ugrep -R -l 'xyz' --include='**/docs/**' .

To recursively list .cpp files in the current directory and any sub-directory
at any depth, that contain `xyz`:

    ugrep -R -l 'xyz' --include='*.cpp' .

The same using a .gitignore-style glob that matches pathnames (globs with `/`)
instead of matching basenames (globs without `/`) in the recursive search:

    ugrep -R -l 'xyz' --include='**/*.cpp' .

The same but using option `-O` to match file name extensions:

    ugrep -R -l 'xyz' -Ocpp .

To recursively list all files in the current directory and below that are not
ignored by .gitignore:

    ugrep -R -l '' --exclude-from=.gitignore .

### Find files by file signatures and magic bytes with -M and -t

To list all files that start with `#!` hashbangs:

    ugrep -R -l '\A#!.*' .

The same but more efficient using option `-M`:

    ugrep -R -l -M '#!.*' '' .

Searching Python files, that have extension `.py` or a shebang, for all import
statements:

    ugrep -R -l -tPython -f python/imports .

### Counting matching lines with -c and -g

To count the number of lines in a file:

    ugrep -c '\n' myfile.txt

To count the number of ASCII words in a file:

    ugrep -cg '[[:word:]]+' myfile.txt

To count the number of ASCII and Unicode words in a file:

    ugrep -cg '\w+' myfile.txt

To count the number of Unicode characters in a file:

    ugrep -cg '\p{Unicode}' myfile.txt

### Displaying file, line, column, and byte offset info with -H, -n, -k, -b, and -T

To recursively search for C++ files with `main`, showing the line and column
numbers of matches:

    ugrep -R -n -k -tc++ 'main' .

To display the byte offset of matches:

    ugrep -R -b -tc++ 'main' .

To display the file name, line and column numbers of matches in `main.cpp`,
with spaces and tabs to space the columns apart:

    ugrep -THnk 'main' main.cpp

### Using colors with --color

To produce color-highlighted results:

    ugrep --color -R -n -k -tc++ 'FIXME.*' .

To page through the color-highlighted results with pager `less -R`:

    ugrep --color --pager -R -n -k -tc++ 'FIXME.*' .

To use pre-defined patterns to list all `#include` and `#define` in C++ files:

    ugrep --color -R -n -tc++ -f c++/includes -f c++/defines .

To list all `#define FOO...` macros in C++ files, color-highlighted:

    ugrep --color=always -R -n -tc++ -f c++/defines . | ugrep 'FOO.*'

Same, but restricted to `.cpp` files only:

    ugrep --color=always -R -n -Ocpp -f c++/defines . | ugrep 'FOO.*'

To monitor the system log for bug reports:

    tail -f /var/log/system.log | ugrep --color -i -w 'bug'

To search tarballs for archived PDF files (assuming bash is our shell):

    for tb in *.tar *.tar.gz; do echo "$tb"; tar tfz "$tb" | ugrep '.*\.pdf$'; done

### Limiting the number of matches with -m and --max-depth

To show only the first 10 matches of `FIXME` in C++ files in the current
directory and all sub-directories below:

    ugrep -R -m10 -tc++ FIXME .

The same, but recursively search up to two directory levels, meaning that `./`
and `./sub/` are visited but not deeper:

    ugrep -R -m10 --max-depth=2 -tc++ FIXME .

### More examples

To list all text files (.txt and .md) that do not properly end with a `\n`
(`-o` is required to match `\n` or `\z`):

    ugrep -R -L -o -Otext '\n\z' .

To list all markdown sections in text files (.txt and .md):

    ugrep -R -o -ttext -e '^.*(?=\r?\n(===|---))' -e '^#{1,6}\h+.*' .

To display multi-line backtick and indented code blocks in markdown files with
their line numbers:

    ugrep -R -o -n -ttext -e '^```([^`]|`[^`]|``[^`])+\n```' -e '^(\t|[ ]{4}).*' .

To find mismatched code (a backtick without matching backtick on the same line)
in markdown:

    ugrep -R -o -n -ttext -e '(?^`[^`\n]*`)' -e '`[^`]+`' .

Man page
--------

    UGREP(1)                         User Commands                        UGREP(1)



    NAME
           ugrep -- universal file pattern searcher

    SYNOPSIS
           ugrep [OPTIONS] [-A NUM] [-B NUM] [-C[NUM]] [PATTERN] [-e PATTERN]
                 [-f FILE] [--file-type=TYPES] [--encoding=ENCODING]
                 [--colour[=WHEN]|--color[=WHEN]] [--label[=LABEL]] [FILE ...]

    DESCRIPTION
           The  ugrep utility searches any given input files, selecting lines that
           match one or more patterns.  By default, a  pattern  matches  an  input
           line  if  the  regular expression (RE) in the pattern matches the input
           line without its trailing newline.  An empty expression  matches  every
           line.   Each  input  line  that matches at least one of the patterns is
           written to the standard output.  To search for patterns that span  mul-
           tiple lines, use option -o.

           The  ugrep  utility  normalizes and decodes encoded input to search for
           the specified ASCII/Unicode patterns.  When the input  contains  a  UTF
           BOM indicating UTF-8, UTF-16, or UTF-32 input then ugrep always normal-
           izes the input to UTF-8.  When no UTF BOM is present, ugrep assumes the
           input  is  ASCII,  UTF-8,  or raw binary.  To specify a different input
           file encoding, use option --encoding.

           The following options are available:

           -A NUM, --after-context=NUM
                  Print NUM  lines  of  trailing  context  after  matching  lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -B, -C, and -y options.

           -a, --text
                  Process a binary file as if it were text.  This is equivalent to
                  the --binary-files=text option.  This option might output binary
                  garbage to the terminal, which can have problematic consequences
                  if the terminal driver interprets some of it as commands.

           -B NUM, --before-context=NUM
                  Print  NUM  lines  of  leading  context  before  matching lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -A, -C, and -y options.

           -b, --byte-offset
                  The  offset  in bytes of a matched line is displayed in front of
                  the respective matched line.  When used with option -g, displays
                  the  offset  in bytes of each pattern matched.  Byte offsets are
                  exact for binary, ASCII, and UTF-8 input.  Otherwise,  the  byte
                  offset in the UTF-8-converted input is displayed.

           --binary-files=TYPE
                  Controls  searching  and  reporting  pattern  matches  in binary
                  files.  Options are `binary',  `without-match`,  `text`,  `hex`,
                  and  `with-hex'.  The default is `binary' to search binary files
                  and to report a match  without  displaying  the  match.   `with-
                  out-match'  ignores  binary  matches.   `text' treats all binary
                  files as text, which might output binary garbage to  the  termi-
                  nal,  which  can  have  problematic consequences if the terminal
                  driver interprets some of it as  commands.   `hex'  reports  all
                  matches  in hexadecimal.  `with-hex` only reports binary matches
                  in hexadecimal, leaving text matches alone.  A match is  consid-
                  ered  binary  if  a match contains a zero byte or an invalid UTF
                  encoding.  See also the -a, -I, -U, -W, and -X options.

           -C[NUM], --context[=NUM]
                  Print NUM lines of leading and trailing context surrounding each
                  match.  The default is 2 and is equivalent to -A 2 -B 2.  Places
                  a --group-separator between contiguous groups  of  matches.   No
                  whitespace may be given between -C and its argument NUM.

           -c, --count
                  Only  a  count  of selected lines is written to standard output.
                  When used with option -g, counts the number of patterns matched.
                  With option -v, counts the number of non-matching lines.

           --colour[=WHEN], --color[=WHEN]
                  Mark  up  the  matching  text  with the expression stored in the
                  GREP_COLOR or GREP_COLORS environment  variable.   The  possible
                  values of WHEN can be `never', `always' or `auto'.

           -D ACTION, --devices=ACTION
                  If  an  input  file  is  a device, FIFO or socket, use ACTION to
                  process it.  By default, ACTION  is  `read',  which  means  that
                  devices are read just as if they were ordinary files.  If ACTION
                  is `skip', devices are silently skipped.

           -d ACTION, --directories=ACTION
                  If an input file is a directory, use ACTION to process  it.   By
                  default,  ACTION  is  `read',  i.e., read directories just as if
                  they were ordinary files.  If ACTION is  `skip',  silently  skip
                  directories.   If ACTION is `recurse', read all files under each
                  directory, recursively, following symbolic links  only  if  they
                  are  on  the command line.  This is equivalent to the -r option.
                  If ACTION is `dereference-recurse', read all  files  under  each
                  directory,  recursively,  following  symbolic  links.   This  is
                  equivalent to the -R option.

           --max-depth=NUM
                  Restrict recursive search to NUM (NUM  >  0)  directories  deep,
                  where --max-depth=1 searches the specified path without visiting
                  sub-directories.

           -E, --extended-regexp
                  Interpret patterns as extended regular expressions (EREs).  This
                  is the default.

           -e PATTERN, --regexp=PATTERN
                  Specify  a PATTERN used during the search of the input: an input
                  line is selected if it matches any of  the  specified  patterns.
                  This  option is most useful when multiple -e options are used to
                  specify multiple patterns, when a pattern  begins  with  a  dash
                  (`-'), or to specify a pattern after option -f.

           --exclude=GLOB
                  Skip files whose name matches GLOB (using wildcard matching).  A
                  glob can use *, ?, and [...] as wildcards,  and  \  to  quote  a
                  wildcard  or backslash character literally.  If GLOB contains /,
                  full pathnames are matched.  Otherwise  basenames  are  matched.
                  Note  that  --exclude patterns take priority over --include pat-
                  terns.  This option may be repeated.

           --exclude-dir=GLOB
                  Exclude directories  whose  name  matches  GLOB  from  recursive
                  searches.  If GLOB contains /, full pathnames are matched.  Oth-
                  erwise basenames are matched.  Note that --exclude-dir  patterns
                  take  priority  over --include-dir patterns.  This option may be
                  repeated.

           --exclude-from=FILE
                  Read the globs from FILE and skip files  and  directories  whose
                  name matches one or more globs (as if specified by --exclude and
                  --exclude-dir).  Lines starting with a `#' and  empty  lines  in
                  FILE ignored. This option may be repeated.

           -F, --fixed-strings
                  Interpret  pattern  as a set of fixed strings, separated by new-
                  lines, any of which is to be  matched.   This  forces  ugrep  to
                  behave as fgrep but less efficiently than fgrep.

           -f FILE, --file=FILE
                  Read  one  or  more newline-separated patterns from FILE.  Empty
                  pattern lines in the file are not processed.   Options  -F,  -w,
                  and  -x  do not apply to FILE patterns.  If FILE does not exist,
                  the GREP_PATH environment variable is used as the path  to  read
                  FILE.      If     that     fails,     looks    for    FILE    in
                  /usr/local/share/ugrep/patterns.  This option may be repeated.

           --free-space
                  Spacing (blanks and tabs) in regular expressions are ignored.

           -G, --basic-regexp
                  Interpret pattern as a  basic  regular  expression  (i.e.  force
                  ugrep to behave as traditional grep).

           -g, --no-group
                  Do  not group multiple pattern matches on the same matched line.
                  Output the matched line again for each additional pattern match,
                  using `+' as the field separator for each additional match.

           --group-separator=SEP
                  Use SEP as a group separator for context options -A, -B, and -C.
                  By default SEP is a double hyphen (`--').

           -H, --with-filename
                  Always print the  filename  with  output  lines.   This  is  the
                  default when there is more than one file to search.

           -h, --no-filename
                  Never print filenames with output lines.

           --help Print a help message.

           -I     Ignore  matches  in  binary files.  This option is equivalent to
                  the --binary-files=without-match option.

           -i, --ignore-case
                  Perform case insensitive matching.  By default,  ugrep  is  case
                  sensitive.  This option is applied to ASCII letters only.

           --include=GLOB
                  Search only files whose name matches GLOB (using wildcard match-
                  ing).  A glob can use *, ?, and [...] as  wildcards,  and  \  to
                  quote a wildcard or backslash character literally.  If GLOB con-
                  tains /, file pathnames are matched.  Otherwise  file  basenames
                  are  matched.   Note  that --exclude patterns take priority over
                  --include patterns.  This option may be repeated.

           --include-dir=GLOB
                  Only directories whose name matches GLOB are included in  recur-
                  sive  searches.  If GLOB contains /, full pathnames are matched.
                  Otherwise basenames are matched.  Note that  --exclude-dir  pat-
                  terns  take  priority  over --include-dir patterns.  This option
                  may be repeated.

           --include-from=FILE
                  Read the globs from FILE and search only files  and  directories
                  whose  name  matches  one  or  more  globs  (as  if specified by
                  --include and --include-dir).  Lines starting  with  a  `#'  and
                  empty lines in FILE are ignored.  This option may be repeated.

           -J[NUM], --jobs[=NUM]
                  Specifies  the  number  of  jobs to run simultaneously to search
                  files.  Without argument NUM, the  number  of  jobs  spawned  is
                  optimized.   No whitespace may be given between -J and its argu-
                  ment NUM.  This feature is not  available  in  this  version  of
                  ugrep.

           -j, --smart-case
                  Perform case insensitive matching unless PATTERN contains a cap-
                  ital letter.  Case insensitive matching applies to ASCII letters
                  only.

           -k, --column-number
                  The  column number of a matched pattern is displayed in front of
                  the respective matched line, starting at  column  1.   Tabs  are
                  expanded when columns are counted, see option --tabs.

           -L, --files-without-match
                  Only  the names of files not containing selected lines are writ-
                  ten to standard output.  Pathnames  are  listed  once  per  file
                  searched.   If  the  standard  input  is  searched,  the  string
                  ``(standard input)'' is written.

           -l, --files-with-matches
                  Only the names of files containing selected lines are written to
                  standard  output.   ugrep  will only search a file until a match
                  has been found,  making  searches  potentially  less  expensive.
                  Pathnames  are  listed  once per file searched.  If the standard
                  input is searched, the string ``(standard input)'' is written.

           --label[=LABEL]
                  Displays the LABEL value when input is read from standard  input
                  where a file name would normally be printed in the output.  This
                  option applies to options -H, -L, and -l.

           --line-buffered
                  Force output to be line buffered.  By default,  output  is  line
                  buffered  when  standard output is a terminal and block buffered
                  otherwise.

           -M MAGIC, --file-magic=MAGIC
                  Only files matching the signature pattern `MAGIC' are  searched.
                  The signature magic bytes at the start of a file are compared to
                  the `MAGIC' regex pattern and, when matching,  the  search  com-
                  mences  immediately  after  the magic bytes.  This option may be
                  repeated and may be combined with options -O and  -t  to  expand
                  the search.  This option is relatively slow as every file on the
                  search path is read.

           -m NUM, --max-count=NUM
                  Stop reading the input after NUM matches.

           -N, --only-line-number
                  The line number of the matching line in the file is output with-
                  out  displaying the match.  The line number counter is reset for
                  each file processed.

           -n, --line-number
                  Each output line is preceded by its relative line number in  the
                  file,  starting at line 1.  The line number counter is reset for
                  each file processed.

           --no-group-separator
                  Removes the group separator line from  the  output  for  context
                  options -A, -B, and -C.

           -O EXTENSIONS, --file-extensions=EXTENSIONS
                  Search only files whose file name extensions match the specified
                  comma-separated list of file name EXTENSIONS.   This  option  is
                  the same as specifying --include='*.ext' for each extension name
                  `ext' in the EXTENSIONS list.  This option may be  repeated  and
                  may be combined with options -M and -t to expand the search.

           -o, --only-matching
                  Prints  only  the  matching  part  of  lines  and allows pattern
                  matches across newlines to span multiple  lines.   Line  numbers
                  for  multi-line  matches are displayed with option -n, using `|'
                  as the field separator for each additional line matched  by  the
                  pattern.  Context options -A, -B, -C, and -y are disabled.

           -P, --perl-regexp
                  Interpret PATTERN as a Perl regular expression.  This feature is
                  not available in this version of ugrep.

           -p, --no-dereference
                  If -R or -r is specified, no symbolic links are  followed,  even
                  when they are on the command line.

           --pager[=COMMAND]
                  When  output  is  sent  to  the terminal, uses `COMMAND' to page
                  through the output.  The default COMMAND  is  `less  -R'.   This
                  option makes --color=auto behave as --color=always.

           -Q ENCODING, --encoding=ENCODING
                  The  input  file  encoding.  The possible values of ENCODING can
                  be:  `binary',   `ISO-8859-1',   `ASCII',   `EBCDIC',   `UTF-8',
                  `UTF-16',    `UTF-16BE',   `UTF-16LE',   `UTF-32',   `UTF-32BE',
                  `UTF-32LE',  `CP437',  `CP850',  `CP858',  `CP1250',   `CP1251',
                  `CP1252',  `CP1253',  `CP1254',  `CP1255',  `CP1256',  `CP1257',
                  `CP1258'

           -q, --quiet, --silent
                  Quiet mode: suppress normal output.  ugrep will  only  search  a
                  file  until  a match has been found, making searches potentially
                  less expensive.  Allows a pattern match to span multiple  lines.

           -R, --dereference-recursive
                  Recursively  read  all  files  under each directory.  Follow all
                  symbolic links, unlike -r.

           -r, --recursive
                  Recursively read all files under each directory, following  sym-
                  bolic links only if they are on the command line.

           -S, --dereference
                  If  -r  is  specified, all symbolic links are followed, like -R.
                  The default is not to follow symbolic links.

           -s, --no-messages
                  Silent mode.  Nonexistent and unreadable files are ignored (i.e.
                  their error messages are suppressed).

           --separator=SEP
                  Use  SEP as field separator between file name, line number, col-
                  umn number, byte offset, and the matched line.  The default is a
                  colon (`:').

           -T, --initial-tab
                  Add  a  tab space to separate the file name, line number, column
                  number, and byte offset with the matched line.

           -t TYPES, --file-type=TYPES
                  Search only files associated with TYPES, a comma-separated  list
                  of file types.  Each file type corresponds to a set of file name
                  extensions passed to option -O.  For capitalized file types, the
                  file  signature  is  passed to option -M to expand the search by
                  including files found on the search  path  with  matching  magic
                  bytes.   This  option  may  be repeated.  The possible values of
                  TYPES can be (use option -tlist to  display  a  detailed  list):
                  `actionscript',   `ada',   `asm',   `asp',  `aspx',  `autoconf',
                  `automake', `awk', `Awk', `basic', `batch', `bison', `c', `c++',
                  `clojure',  `csharp',  `css',  `csv',  `dart', `Dart', `delphi',
                  `elixir', `erlang', `fortran',  `gif',  `Gif',  `go',  `groovy',
                  `haskell', `html', `jade', `java', `javascript', `jpeg', `Jpeg',
                  `json', `jsp', `julia', `kotlin', `less', `lex', `lisp',  `lua',
                  `m4',  `make',  `markdown',  `matlab',  `node',  `Node', `objc',
                  `objc++', `ocaml', `parrot',  `pascal',  `pdf',  `Pdf',  `perl',
                  `Perl',   `php',   `Php',   `png',  `Png',  `prolog',  `python',
                  `Python',  `R',  `rpm',  `Rpm',  `rst',  `rtf',  `Rtf',  `ruby',
                  `Ruby',    `rust',    `scala',   `scheme',   `shell',   `Shell',
                  `smalltalk',  `sql',  `swift',  `tcl',  `tex',  `text',  `tiff',
                  `Tiff',  `tt',  `typescript',  `verilog',  `vhdl', `vim', `xml',
                  `Xml', `yacc', `yaml'

           --tabs=NUM
                  Set the tab size to NUM to expand tabs for option -k.  The value
                  of NUM may be 1, 2, 4, or 8.

           -U, --binary
                  Disables Unicode matching for binary file matching, forcing PAT-
                  TERN to match bytes, not Unicode characters.   For  example,  -U
                  '\xa3'  matches  byte A3 (hex) instead of the Unicode code point
                  U+00A3 represented by the two-byte UTF-8 sequence C2 A3.

           -V, --version
                  Display version information and exit.

           -v, --invert-match
                  Selected lines are those not matching any of the specified  pat-
                  terns.

           -W, --with-hex
                  Only  output binary matches in hexadecimal, leaving text matches
                  alone.  This option is equivalent to the --binary-files=with-hex
                  option.

           -w, --word-regexp
                  The  pattern  or  -e  patterns are searched for as a word (as if
                  surrounded by \< and \>).

           -X, --hex
                  Output matches in hexadecimal.  This option is equivalent to the
                  --binary-files=hex option.

           -x, --line-regexp
                  Only  input lines selected against the entire pattern or -e pat-
                  terns are considered to be matching lines (as if surrounded by ^
                  and $).

           -Y, --empty
                  Permits  empty  matches,  such  as `^\h*$' to match blank lines.
                  Empty matches are disabled by default.  Note that empty-matching
                  patterns  such  as `x?' and `x*' match all input, not only lines
                  with `x'.

           -y, --any-line
                  Any matching or non-matching line is output.  Non-matching lines
                  are  output  as context for matching lines, with the `-' separa-
                  tor.  See also the -A, -B, and -C options.

           -Z, --null
                  Prints a zero-byte after the file name.

           -z, --decompress
                  Search zlib-compressed (.gz) files.  Option -Q is disabled.

           The regular expression pattern syntax is an extended form of the  POSIX
           ERE syntax.  For an overview of the syntax see README.md or visit:

                  https://github.com/Genivia/ugrep

           Note  that  `.'  matches any non-newline character.  Matching a newline
           character `\n' is not possible unless one or more of  the  options  -c,
           -L,  -l,  -N,  -o, or -q are used (in any combination, but not combined
           with option -v) to allow a pattern match to span multiple lines.

           If no file arguments are specified, or if - is specified, the  standard
           input is used.

    EXIT STATUS
           The ugrep utility exits with one of the following values:

           0      One or more lines were selected.

           1      No lines were selected.

           >1     An error occurred.

    GLOBBING
           Globbing  is  used by options --include, --include-dir, --include-from,
           --exclude, --exclude-dir, --exclude-from to match pathnames  and  base-
           names.  Globbing supports gitignore syntax and the corresponding match-
           ing rules.  When a glob contains a path separator `/', the pathname  is
           matched.   Otherwise  the  basename  of a file or directory is matched.
           For  example,  *.h  matches  foo.h  and  bar/foo.h.   bar/*.h   matches
           bar/foo.h  but  not  foo.h and not bar/bar/foo.h.  Use a leading `/' to
           force /*.h to match foo.h but not bar/foo.h.

           Glob Syntax and Conventions

           **/    Matches zero or more directories.

           /**    When at the end of a glob, matches everything after the /.

           *      Matches anything except a /.

           /      When used at the begin of a glob, matches if pathname has no  /.

           ?      Matches any character except a /.

           [a-z]  Matches one character in the selected range of characters.

           [^a-z] Matches one character not in the selected range of characters.

           [!a-z] Matches one character not in the selected range of characters.

           \?     Matches a ? (or any character specified after the backslash).

           Glob Matching Examples

           **/a   Matches a, x/a, x/y/a,       but not b, x/b.

           a/**/b Matches a/b, a/x/b, a/x/y/b, but not x/a/b, a/b/x

           a/**   Matches a/x, a/y, a/x/y,     but not b/x

           a/*/b  Matches a/x/b, a/y/b,        but not a/x/y/b

           /a     Matches a,                   but not x/a

           /*     Matches a, b,                but not x/a, x/b

           a?b    Matches axb, ayb,            but not a, b, ab

           a[xy]b Matches axb, ayb             but not a, b, azb

           a[a-z]b
                  Matches aab, abb, acb, azb,  but not a, b, a3b, aAb, aZb

           a[^xy]b
                  Matches aab, abb, acb, azb,  but not a, b, axb, ayb

           a[^a-z]b
                  Matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb

           Lines  in  the --exclude-from and --include-from files are ignored when
           empty or start with a `#'.  The prefix `!' to a glob  in  such  a  file
           negates  the  pattern  match,  i.e.  matching files are excluded except
           files matching the globs prefixed with `!' in the --exclude-from  file.

    ENVIRONMENT
           GREP_PATH
                  May  be  used to specify a file path to pattern files.  The file
                  path is used by option -f to open a pattern file, when the  file
                  cannot be opened.

           GREP_COLOR
                  May  be used to specify ANSI SGR parameters to highlight matches
                  when option --color is used, e.g. 1;35;40 shows pattern  matches
                  in bold magenta text on a black background.

           GREP_COLORS
                  May  be used to specify ANSI SGR parameters to highlight matches
                  and other attributes when option --color is used.  Its value  is
                  a  colon-separated  list of ANSI SGR parameters that defaults to
                  cx=2:mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36.  The mt=,  ms=,  and
                  mc= capabilities of GREP_COLORS have priority over GREP_COLOR.

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

           To display the line and column number of all `FIXME' in all  C++  files
           using  recursive search, with one line of context before and after each
           matched line:

                  $ ugrep --color -C1 -R -n -k -tc++ 'FIXME.*' .

           To list all C/C++ comments in a file displaying their line  and  column
           numbers using options -n and -k, and option -o that allows for matching
           patterns across multiple lines:

                  $ ugrep -nko -e '//.*' -e '/\*([^*]|(\*+[^*/]))*\*+\/' myfile

           The same search, but using pre-defined patterns:

                  $ ugrep -nko -f c/comments myfile

           To list the lines that need fixing in a C/C++ source  file  by  looking
           for  the word FIXME while skipping any FIXME in quoted strings by using
           a negative pattern `(?^X)' to ignore quoted strings:

                  $ ugrep -no -e 'FIXME' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*")' myfile

           To match the binary pattern `A3hhhhA3hh` (hex) in a binary file without
           Unicode pattern matching -U (which would otherwise match  `\xaf'  as  a
           Unicode  character  U+00A3  with UTF-8 byte sequence C2 A3) and display
           the results in hex with -X using `less -R' as a pager:

                  $ ugrep --pager -UXo '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

           To hex dump an entire file in color:

                  $ ugrep --color --pager -Xo '' a.out

           To list all files containing a RPM  signature,  located  in  the  `rpm`
           directory and recursively below:

                  $ ugrep -R -l -tRpm '' rpm/

           To monitor the system log for bug reports:

                  $ tail -f /var/log/system.log | ugrep --color -i -w 'bug'

    BUGS
           Report bugs at:

                  https://github.com/Genivia/ugrep/issues


    LICENSE
           ugrep  is  released under the BSD-3 license.  All parts of the software
           have reasonable copyright terms permitting free  redistribution.   This
           includes the ability to reuse all or parts of the ugrep source tree.

    SEE ALSO
           grep(1).



    ugrep 1.2.2                      July 17, 2019                        UGREP(1)

For future updates
------------------

- Further speed improvements when reading files, e.g. with `mmap`, `memchr`.
- Backreferences are not supported.  This will likely not be supported any
  time soon in the RE/flex library.  We could use Boost.Regex for this (using
  RE/flex `BoostMatcher` class), which is faster than PCRE2 but slower than
  RE/flex `Matcher` class.  With Boost.Regex we can also support Perl-like
  matching as an option.

<a name="patterns"/>
Regex pattern syntax
--------------------

An empty pattern is a special case that matches everything except empty files,
i.e. does not match zero-length files, as per POSIX.1 grep standard.

A regex pattern is an extended set of regular expressions (ERE), with nested
sub-expression patterns `φ` and `ψ`:

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
  `(?=φ)`   | matches `φ` without consuming it, i.e. lookahead (top-level `φ` with nothing following after the lookahead)
  `(?^φ)`   | matches `φ` and ignores it (top-level `φ` with nothing following after the negative pattern)
  `^φ`      | matches `φ` at the begin of input or begin of a line (top-level `φ`, not nested in a sub-pattern)
  `φ$`      | matches `φ` at the end of input or end of a line (top-level `φ`, not nested in a sub-pattern)
  `\Aφ`     | matches `φ` at the begin of input (requires one or more of `-c`, `-L`, `-l`, `-N`, `-o`, `-q`) (top-level `φ`, not nested in a sub-pattern)
  `φ\z`     | matches `φ` at the end of input (requires one or more of `-c`, `-L`, `-l`, `-N`, `-o`, `-q`) (top-level `φ`, not nested in a sub-pattern)
  `\bφ`     | matches `φ` starting at a word boundary (top-level `φ`, not nested in a sub-pattern)
  `φ\b`     | matches `φ` ending at a word boundary (top-level `φ`, not nested in a sub-pattern)
  `\Bφ`     | matches `φ` starting at a non-word boundary (top-level `φ`, not nested in a sub-pattern)
  `φ\B`     | matches `φ` ending at a non-word boundary (top-level `φ`, not nested in a sub-pattern)
  `\<φ`     | matches `φ` that starts a word (top-level `φ`, not nested in a sub-pattern)
  `\>φ`     | matches `φ` that starts a non-word (top-level `φ`, not for sub-patterns `φ`)
  `φ\<`     | matches `φ` that ends a non-word (top-level `φ`, not nested in a sub-pattern)
  `φ\>`     | matches `φ` that ends a word (top-level `φ`, not nested in a sub-pattern)
  `\i`      | matches an indent (requires one or more of `-c`, `-L`, `-l`, `-N`, `-o`, `-q`)
  `\j`      | matches a dedent (requires one or more of `-c`, `-L`, `-l`, `-N`, `-o`, `-q`)
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
