[![build status][travis-image]][travis-url] [![Language grade: C/C++][lgtm-image]][lgtm-url] [![license][bsd-3-image]][bsd-3-url]

Universal grep pro ("uber grep")
================================

<div align="center">
<img src="https://www.genivia.com/images/function_defs.png" width="45%" height="45%" alt="ugrep C++ function search results">
<img src="https://www.genivia.com/images/hexdump.png" width="45%" height="45%" alt="ugrep hexdump results">
<br>
Grep super fast through source code, Unicode text, binary files, cpio/jar/pax/tar/zip archives, and compressed files.
<br>
<br>
</div>

- Unlimited pro version
- Fully compatible with the standard GNU and BSD grep command-line options
- Written in clean and efficient C++11 code, built for extreme speed
- Faster than GNU/BSD grep, mostly beating ripgrep, silver searcher, etc.
- Multi-threaded search using high-performance lock-free job queue stealing
- Multi-threaded decompression and search of decompressed streams
- Optimized non-blocking asynchronous IO
- Selects files to search by file types, filename suffix, and/or "magic bytes"
- Searches archives (cpio, jar, tar, pax, zip)
- Searches compressed files (.gz, .Z, .zip, .bz, .bz2, .lzma, .xz)
- Searches pdf, doc, docx, xls, xlxs, and more using filter utilities
- Searches binary files and displays hexdumps with binary pattern matches
- Searches UTF-encoded files by supporting Unicode pattern matches
- Searches files encoded in ISO-8859-1 thru 16, CP 437, CP 850, MAC, KOI8, etc.
- Searches files excluding files specified by .gitignore etc.
- Searches patterns across newlines
- Searches patterns excluding negative patterns ("match this but not that")
- Includes predefined regex patterns to search source code, XML, JSON, HTML, etc.
- Generates matches in CSV, JSON, XML, and user-specified formats 
- Compiles and runs on Linux, Unix, Mac OS X, Windows, etc.

Table of contents
-----------------

- [Introduction: why use ugrep?](#introduction)
- [Speed](#speed)
  - [Tests](#tests)
  - [Results](#results)
- [Installation](#installation)
  - [Download and build steps](#download)
  - [Download the binaries](#binaries)
  - [Using ugrep within Vim](#vim)
- [Ugrep versus other greps](#comparison)
  - [Equivalence to GNU grep and BSD grep](#equivalence)
  - [Useful aliases](#aliases)
  - [Notable improvements over other greps](#improvements)
  - [Feature wish list](#todo)
- [Tutorial](#tutorial)
  - [Examples](#examples)
  - [Displaying helpful info](#help)
  - [Recursively list matching files with options -R or -r with -L or -l](#recursion)
  - [Search this but not that with -v, -e, -N, -f, -L, -w, -x](#not)
  - [Searching ASCII, Unicode, and other encodings with -Q](#unicode)
  - [Matching multiple lines of text](#multiline)
  - [Displaying context with -A, -B, -C, and -y](#context)
  - [Searching source code using -f, -O, and -t](#source)
  - [Searching compressed files and archives with -z](#archives)
  - [Find files by file signature and shebang "magic bytes" with -M and -t](#magic)
  - [Using filter utilities to search documents with --filter](#filter)
  - [Searching and displaying binary files with -U, -W, and -X](#binary)
  - [Ignoring hidden files and binary files with --no-hidden and -I](#hidden)
  - [Ignoring .gitignore-specified files with --ignore-files](#ignore)
  - [Using gitignore-style globs to select directories and files to search](#gitignore)
  - [Including or excluding mounted file systems from searches](#fs)
  - [Counting the number of matches with -c and -co](#count)
  - [Displaying file, line, column, and byte offset info with -H, -n, -k, -b, and -T](#fields)
  - [Displaying colors with --color and paging the output with --pager](#color)
  - [Output matches in JSON, XML, CSV, C++](#json)
  - [Customized output with --format](#format)
  - [Replacing matches with --format backreferences to group captures](#replace)
  - [Limiting the number of matches with -K, -m, --max-depth, and --max-files](#max)
  - [Matching empty patterns with -Y](#empty)
  - [Tips for advanced users](#tips)
  - [More examples](#more)
- [Man page](#man)
- [Regex patterns](#patterns)
  - [POSIX regular expression syntax](#posix-syntax)
  - [POSIX and Unicode character classes](#posix-classes)
  - [POSIX and Unicode character categories](#posix-categories)
  - [Perl regular expression syntax](#perl-syntax)

<a name="introduction"/>

Introduction: why use ugrep? 
----------------------------

- **ugrep options are compatible with GNU and BSD grep**, so existing GNU/BSD 
  examples and scripts work with **ugrep** just as well.

- **ugrep is faster than GNU grep and other grep tools**.  We use
  [RE/flex](https://github.com/Genivia/RE-flex) for high-performance regex
  matching, which is 10 to 100 times faster than PCRE2 and RE2.  **ugrep** is
  multi-threaded and uses lock-free job queue stealing to search files
  concurrently.  In addition, option `-z` enables task parallelism to optimize
  searching compressed files and archives.  See [speed comparisons](#speed).

- **ugrep makes it simple to search source code** with options to select files
  by file type, filename extension, file signature "magic bytes" or shebangs.
  For example, to list all shell scripts in or below the working directory:

      ugrep -rl -tShell ''

  where `-r` is recursive search, `-l` lists matching files, `-tShell` selects
  shell files by file type (i.e. shell extensions and shebangs), and the empty
  pattern `''` matches the entire file (a common grep feature).  Also new
  options `-O` and `-M` may be used to select files by extension and by file
  signature "magic bytes", respectively.

- **ugrep searches compressed files and archives (cpio, jar, tar, pax, zip)**
  with option `-z`.  The matching file names in archives are output in braces.
  For example `myprojects.tgz{main.cpp}` indicates that file `main.cpp` in
  compressed tar file `myprojects.tgz` has a match.  Filename glob matching,
  file types, filename extensions, and file signature "magic bytes" can be
  selected to filter files in archives with options `-g`, `-t`, `-O`, and `-M`,
  respectively.  For example:

      ugrep -z -tc++ -w 'main' myprojects.tgz

  looks for `main` in C++ files in the compressed tar file `myprojects.tgz`.

- **ugrep matches patterns across multiple lines** without a performance
  penalty.  Other grep tools that support multi-line pattern matches require an
  option and the performance suffers.  By contrast, **ugrep** is fast and no
  options are required.  For example:

      ugrep '.*begin(.|\n)*?end.*' myfile.txt

  matches all lines between a line containing `begin` and a line containing
  `end`.  This pattern uses lazy repetition `(.|\n)*?` to match everything
  inbetween, including newlines.  This feature supports matching that could
  otherwise only be done with utilities like `sed`.

- **ugrep includes a [database of search patterns](https://github.com/Genivia/ugrep/tree/master/patterns)**,
  so you don't need to memorize complex regex patterns for common searches.
  Environment variable `GREP_PATH` can be set to point to your own directory
  with patterns that option `-f` uses to read your pattern files.  For example
  to recursively search Python files in the working directory for lines with
  `import` statements:

      ugrep -R -tPython -f python/imports

  where `-R` is recursive search while following symlinks, `-tPython` selects
  Python files only (i.e. by file name extension `.py` and by Python shebangs),
  and the `-f` option specifies predefined patterns to search for Python
  `import` statements (matched by the two patterns `\<import\h+.*` and
  `\<from\h+.*import\h+.*` predefined in `patterns/python/imports`).

- **ugrep is the only grep tool that allows you to specify negative patterns**
  to *zap* parts in files you want to skip.  This removes false positives.  For
  example to find exact matches of `main` in C/C++ source code while skipping
  strings and comments that may have a match with `main` in them:

      ugrep -R -tc++ -nw 'main' -f c/zap_strings -f c/zap_comments

  where `-R` is recursive search while following symlinks `-tc++` searches
  C/C++ source code files, `-n` shows line numbers in the output, `-w` matches
  exact words (for example, `mainly` won't be matched), and the `-f` options
  specify two predefined installed patterns to match and skip strings and
  comments in the input.  As another example, it is now easy to search a PHP
  file while zapping past any HTML between PHP code segments:

      ugrep 'IsInjected' -f php/zap_html myfile.php

- **ugrep produces hexdumps for binary matches** to search for binary patterns
  of bytes, for example:

      ugrep --color -UX '\xed\xab\xee\xdb' some.rpm

  where `-X` produces hexadecimal output, `-U` specifies a binary pattern to
  search (meaning non-Unicode), and `--color` shows the results in color.
  Other options that normally work with text matches work with `-X` too, such
  as the context options `-A`, `-B`, `-C`, and `-y`.  A match is considered
  binary if it contains a NUL (`\0`) or an invalid UTF multi byte sequence that
  cannot be properly displayed on the terminal as text.

- **ugrep matches Unicode patterns** by default (disabled with option `-U`).  The
  [regular expression pattern syntax](#pattern) is POSIX ERE compliant extended
  with PCRE-like syntax.  Option `-P` may also be used for Perl matching with
  Unicode patterns.

- **ugrep searches UTF-8/16/32 input and other formats**.  Option `-Q` permits
  many other file formats to be searched, such as ISO-8859-1 to 16, EBCDIC,
  code pages 437, 850, 858, 1250 to 1258, MacRoman, and KIO8.

- **ugrep customizes the output format** with options `--csv`, `--json`, and
  `--xml` to output CSV, JSON, or XML.  Option `--format` may be used to
  replace matches and to take custom formatting to the extreme.

- **ugrep understands gitignore-style globs** and ignores files specified
  in a `.gitignore` file, or any other file.  Either explicitly with
  `--exclude-from=.gitignore` or implicitly with `--ignore-files` to ignore
  files in and below the directory of a `.gitignore` file found during
  recursive searches.

- **ugrep supports Perl regular expressions** with option `-P`.  This option
  offers PCRE-like syntax, including backreferences and lookbehinds.

- **ugrep POSIX regex patterns are converted to efficient DFAs** for faster
  matching without backtracking.  DFAs yield significant speedups when
  searching multiple files and large files.  Rare and pathological cases are
  known to exist that may increase the initial running time of **ugrep** for
  complex DFA construction.

- **ugrep is portable** to Linux, Unix, Mac OS X, and compiles with MSVC++ to
  run on Windows.

- **ugrep is free [BSD-3](https://opensource.org/licenses/BSD-3-Clause) source
  code** and does not include any GNU or BSD grep open source code.  **ugrep**
  uses the RE/flex open source library and Boost.Regex.

- **ugrep is growing!** We love your feedback (issues) and contributions (pull
  requests) ❤️ For example, what patterns do you use to search source code?
  Please tell us or contribute and share!

<a name="speed"/>

Speed
-----

Our focus is on offering a grep tool with a wide range of new features, while
offering high performance that is competitive or beats the fastest grep tools.

<a name="tests"/>

### Tests

The following tests span a range of use cases.

Test | Command                                                          | Description
---- | ---------------------------------------------------------------- | -----------------------------------------------------
T0   | `GREP -c serialize big.cpp`                                      | count matches of "serialize" in a 35MB C++ source code file
T1   | `GREP -cw -e char -e int -e long -e size_t -e void big.cpp`      | count 5 short words in a 35MB C++ source code file
T2   | `GREP -Eon 'serialize_[a-zA-Z0-9_]+Type' big.cpp`                | search and display C++ serialization functions in a 35MB source code file
T3   | `GREP -Fon -f words1+1000 enwik8`                                | search 1000 words of length 1 or longer in a 100MB Wikipedia file
T4   | `GREP -Fon -f words2+1000 enwik8`                                | search 1000 words of length 2 or longer in a 100MB Wikipedia file
T5   | `GREP -Fon -f words3+1000 enwik8`                                | search 1000 words of length 3 or longer in a 100MB Wikipedia file
T6   | `GREP -Fon -f words4+1000 enwik8`                                | search 1000 words of length 4 or longer in a 100MB Wikipedia file
T7   | `GREP -Fon -f words8+1000 enwik8`                                | search 1000 words of length 8 or longer in a 100MB Wikipedia file
T8   | `GREP -ro '#[[:space:]]*include[[:space:]]+"[^"]+"' -Oh,hpp,cpp` | recursive search of `#include "..."` in the directory tree from the Qt 5.9.2 root, restricted to `.h`, `.hpp`, and `.cpp` files
T9   | `GREP -ro '#[[:space:]]*include[[:space:]]+"[^"]+"' -Oh,hpp,cpp` | same as T8 but single-threaded
T10  | `GREP -z -Fc word word*.gz`                                      | count `word` in 6 compressed files of 1MB to 3MB each

Note: T8 and T9 use **ugrep** option `-Oh,hpp,cpp` to restrict the search to
files with extensions `.h`, `.hpp`, and `.cpp`, which should be formulated with
GNU/BSD grep as `--include='*.h' --include='*.hpp' --include='*.cpp'`, with
silver searcher as `-G '.*\.(h|hpp|cpp)'` and with ripgrep as `--glob='*.h'
--glob='*.hpp' --glob='*.cpp'`.

The corpora used in the tests are available for
[download](https://www.genivia.com/files/corpora.zip).

<a name="results"/>

### Results

Performance tests were conducted with a Mac OS X using clang 9.0.0 -O2 on a 2.9
GHz Intel Core i7, 16 GB 2133 MHz LPDDR3 Mac OS 10.12.6 machine.  The best
times for many runs is shown under minimal machine load.  Performance results
depend on compilers, libraries, the OS, the CPU type, and file system
latencies.

Results are shown in real time (wall clock time) seconds elapsed.  Best times
are shown in boldface, *n/a* means that the running time exceeded 1 minute or
options are not supported (e.g. when option `-z` is not supported).

GREP            | T0        | T1       | T2       | T3       | T4       | T5       | T6       | T7       | T8       | T9       | T10      |
--------------- | --------  | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
BSD grep 2.5.1  | 0.67      | 1.85     | 0.83     | *n/a*    | *n/a*    | *n/a*    | *n/a*    | *n/a*    | 3.35     | 3.35     | 0.60     |
GNU grep 3.3    | 0.06      | 0.18     | 0.16     | 2.70     | 2.64     | 2.54     | 2.42     | 2.26     | 0.26     | 0.26     | *n/a*    |
silver searcher | 0.05      | 0.16     | 0.21     | *n/a*    | *n/a*    | *n/a*    | *n/a*    | *n/a*    | 0.46     | 0.46     | *n/a*    |
ripgep          | 0.03      | 0.19     | 0.06     | 2.20     | 2.07     | 2.00     | 2.01     | 2.14     | 0.12     | 0.36     | 0.03     |
ugrep           | **0.02**  | **0.09** | **0.05** | **1.06** | **1.04** | **0.93** | **0.95** | **0.33** | **0.10** | **0.20** | **0.02** |

Option `-z` of **ugrep** uses task parallelism to optimize file reading,
decompression, and searching.

In some cases we decided in favor of features and safety over performance.  For
example, **ugrep** considers files binary when containing invalid UTF encodings
or a NUL (`\0`).  GNU and BSD grep only check for NUL, which is faster but may
lead to display problems on most terminals.

For these tests, search results are piped to a `null` utility to eliminate
terminal display overhead.  Looking through the GNU grep source code revealed
that GNU grep "cheats" when output is redirected to `/dev/null` by essentially
omitting all output and stopping the search after the first match in a file.
This is essentially the same as using option `-q`.  Therefore, to conduct our
tests fairly, we pipe the output to a simple `null` utility that eats the input
and discards it, see the source code below:

    #include <sys/types.h>
    #include <sys/uio.h>
    #include <unistd.h>
    int main()
    {
      char buf[65536];
      while (read(0, buf, 65536) > 0)
        continue;
    }

For performance considerations, it is important to note that **ugrep** matches
Unicode by default.  This means that regex meta symbol `.` and the escapes
`\w`, `\l`, and others match Unicode.  As a result, these may take (much) more
time to match.  To disable Unicode matching, use **ugrep** with option `-U`,
e.g. `ugrep -on -U 'serialize_\w+Type'` is fast but slower without `-U`.

<a name="installation"/>

Installation
------------

<a name="download"/>

### Download and build steps

Clone **ugrep** from https://github.com/Genivia/ugrep

    $ git clone https://github.com/Genivia/ugrep

Build **ugrep** on Unix-like systems with:

    $ cd ugrep
    $ ./configure && make

This builds `ugrep` in the `ugrep/src` directory and copy it to `ugrep/bin`.
You can tell which version it is with:

    $ bin/ugrep -V
    ugrep 1.7.3 x86_64-apple-darwin16.7.0

If you prefer to enable colorized output by default without requiring option
`--color` then execute:

    $ ./configure --enable-color && make

To see the details of all build configuration options available, including
`--with-grep-path=GREP_PATH`, `--with-grep-colors="GREP_COLORS"`,
`--enable-color`, `--enable-pager`, `--disable-hidden`, and `--disable-mmap`,
execute:

    $ ./configure --help

After `make` finishes, copy `bin/ugrep` to a convenient location, for example
in your `bin` directory.

Or you can install the **ugrep** utility and its manual page with:

    $ sudo make install

This also installs the pattern files with predefined patterns for option `-f`
at `/usr/local/share/ugrep/patterns/`.  Option `-f` first checks the working
directory for the presence of pattern files, if not found checks environment
variable `GREP_PATH` to load the pattern files, and if not found reads the
installed predefined pattern files.

Developers may want to use sanitizers to verify the **ugrep** code when making
significant changes, for example to detect data races with the
[ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html):

    $ ./configure --enable-color CXXFLAGS='-fsanitize=thread -O1 -g'
    $ make clean; make

We checked **ugrep** with the clang AddressSanitizer, MemorySanitizer,
ThreadSanitizer, and UndefinedBehaviorSanitizer.  These options incur
significant runtime overhead and should not be used for the final build.

#### Optional dependencies

- Option `-P` (Perl regular expressions) requires the
  [Boost.Regex](https://www.boost.org) library installed.
- Option `-z` (decompress) requires the [Zlib](https://www.zlib.net)
  library installed, e.g. with `sudo apt-get install -y libz-dev`.  To search
  `.bz` and `.bz2` files, install the [bzip2](https://www.sourceware.org/bzip2)
  library, e.g. with `sudo apt-get install -y libbz2-dev`.  To search `.lzma`
  and `.xz` files, install the [lzma](https://tukaani.org/xz/) library, e.g.
  with `sudo apt-get install -y liblzma-dev`.

#### Troubleshooting

Unfortunately, cloning from Git does not preserve timestamps which means that
you may run into "WARNING: 'aclocal-1.15' is missing on your system."

To work around this problem, run:

    $ autoreconf -fi
    $ ./configure && make

<a name="binaries"/>

### Download the binaries

Clone **ugrep** from https://github.com/Genivia/ugrep

    $ git clone https://github.com/Genivia/ugrep

Prebuilt binaries for Mac OS X and Windows are included in the `ugrep/bin`
directory.  All versions of ugrep are designed to run from the command line
interface (CLI).

There are two Windows versions: `ugrep\bin\win32\ugrep.exe` and
`ugrep\bin\win64\ugrep.exe`.  Depending on your system, add the 32 or 64 bit
version to your execution path:  go to *Settings* and search for "Path" in
*Find a Setting*.  Select *environment variables* -> *Path* -> *New* and add
the directory `C:\<fill this part in>\ugrep\bin\win32`.

Notes on using `ugrep.exe` from the Windows command line:
- ugrep.exe expands globs such as `*.txt` and `*\dir\*`
- colorized output is enabled by default
- when specifying quited patterns and arguments on the command line, do not
  enclose arguments in single `'` quotes, use `"` instead (single `'` quotes
  become part of a command-line argument)
- when specifying an empty pattern `""` to match all input, this may be ignored
  by some Windows command interpreters such as Powershell, in that case use
  option `--match` instead

<a name="vim"/>

### Using ugrep within Vim

First, let's define the `:grep` command in Vim to search files recursively.  To
do so, add the following lines to your `.vimrc` located in the root directory:

    if executable('ugrep')
        set grepprg=ugrep\ -RInk\ -j\ -u\ --tabs=1\ --no-hidden\ --ignore-files
        set grepformat=%f:%l:%c:%m,%f+%l+%c+%m,%-G%f\\\|%l\\\|%c\\\|%m
    endif

This specifies case insensitive searches with the Vim `:grep` command.  For
case sensitive searches, remove `\ -j` from `grepprg`.  Multiple matches on the
same line are listed in the quickfix window separately.  If this is not
desired, remove `\ -u` from `grepprg`.  With this change, only the first match
on a line is shown.  Option `--no-hidden` skips hidden files (dotfiles) and
option `--ignore-files` skips files specified in `.gitignore` files, when
present.  To limit the depth of recursive searches to the current directory
only, append `\ --max-depth=1` to `grepprg`.

You can now invoke the Vim `:grep` command in Vim to search files on a
specified `PATH` for `PATTERN` matches:

    :grep PATTERN [PATH]

If you omit `PATH`, then the working directory is searched.  Use `%` as `PATH`
to search only the currently opened file in Vim:

    :grep PATTERN %

The `:grep` command shows the results in a
[quickfix](http://vimdoc.sourceforge.net/htmldoc/quickfix.html#:grep) window
that allows you to quickly jump to the matches found.

To open a quickfix window with the latest list of matches:

    :copen

Double-click on a line in this window (or select a line and press ENTER) to
jump to the file and location in the file of the match.  Enter commands `:cn`
and `:cp` to jump to the next or previous match, respectively.  To update the
search results in the quickfix window, just grep them.  For example, to
recursively search C++ source code marked `FIXME` in the working directory:

    :grep -tc++ FIXME

To close the quickfix window:

    :cclose

You can use **ugrep** options with the `:grep` command, for example to
select single- and multi-line comments in the current file:

    :grep -f c++/comments %

Only the first line of a multi-line comment is shown in quickfix, to save
space.  To show all lines of a multi-line match, remove `%-G` from
`grepformat`.

A popular Vim tool is [ctrlp.vim](http://kien.github.io/ctrlp.vim), which is
installed with:

    $ cd ~/.vim
    $ git clone https://github.com/kien/ctrlp.vim.git bundle/ctrlp.vim

CtrlP uses **ugrep** by adding the following lines to your `.vimrc`:

    if executable('ugrep')
        set runtimepath^=~/.vim/bundle/ctrlp.vim
        let g:ctrlp_match_window='bottom,order:ttb'
        let g:ctrlp_user_command='ugrep %s -RIl --no-hidden --ignore-files --max-depth=3'
    endif

Option `--no-hidden` skips hidden files (dotfiles), option `--ignore-files`
skips files specified in `.gitignore` files, when present, and option
`--max-depth=3` restricts searching directories to three levels (the working
directory and up to two levels below).  Binary files are ignored with option
`-I`.

Start Vim then enter the command:

    :helptags ~/.vim/bundle/ctrlp.vim/doc

To view the CtrlP documentation in Vim, enter the command:

    :help ctrlp.txt

<a name="comparison"/>

Ugrep versus other greps
------------------------

<a name="equivalence"/>

### Equivalence to GNU grep and BSD grep

**ugrep** accepts GNU/BSD grep command options and produces GNU/BSD grep
compatible results, making **ugrep** a true drop-in replacement.

GNU and BSD grep and their common variants are equivalent to **ugrep** when the
following options are used (note that `-U` disables Unicode as GNU/BSD grep do
not support Unicode!):

    grep   = ugrep -J1 -G -U -Y
    egrep  = ugrep -J1 -E -U -Y
    fgrep  = ugrep -J1 -F -U -Y

    zgrep  = ugrep -J1 -G -U -Y -z
    zegrep = ugrep -J1 -E -U -Y -z
    zfgrep = ugrep -J1 -F -U -Y -z

Option `-J1` specifies one thread of execution to produce the exact same output
ordering as GNU/BSD grep at the cost of lower performance and `-Y` enables
empty matches for GNU/BSD compatibility (`-Y` is not strictly necessary, see
[further below](#improvements).)

<a name="aliases"/>

### Useful aliases

    alias ug     = 'ugrep --color --pager'       # short & quick text pattern search
    alias ux     = 'ugrep --color --pager -UX'   # short & quick binary pattern search
    alias uz     = 'ugrep --color --pager -z'    # short & quick compressed files and archives search
    alias ugit   = 'ugrep -R --ignore-files      # like git-grep

    alias grep   = 'ugrep --color --pager -G'    # search with basic regular expressions (BRE)
    alias egrep  = 'ugrep --color --pager -E'    # search with extended regular expressions (ERE)
    alias fgrep  = 'ugrep --color --pager -F'    # find string(s)
    alias xgrep  = 'ugrep --color --pager -W'    # search (ERE) and output text or hex for binary

    alias zgrep  = 'ugrep -z --color --pager -G' # search compressed files and archives (BRE)
    alias zegrep = 'ugrep -z --color --pager -E' # search compressed files and archives (ERE)
    alias zfgrep = 'ugrep -z --color --pager -F' # find string(s) in compressed files and/or archives
    alias zxgrep = 'ugrep -z --color --pager -W' # search (ERE) compressed files/archives and output text or hex for binary

    alias xdump  = 'ugrep --color --pager -X ""' # hexdump files without searching

<a name="improvements"/>

### Notable improvements over other greps

- **ugrep** matches patterns across multiple lines.
- **ugrep** regular expression patterns are more expressive than GNU grep and
  BSD grep POSIX ERE and support Unicode pattern matching and most of the PCRE
  syntax.  Extended regular expression (ERE) syntax is the default (i.e.
  option `-E`, as egrep).
- **ugrep** matches Unicode by default (disabled with option `-U`).
- **ugrep** spawns threads to search files concurrently to improve search
  speed (disabled with option `-J1`).
- **ugrep** option `-Y` enables matching empty patterns.  Grepping with
  empty-matching patterns is weird and gives different results with GNU grep
  versus BSD grep.  Empty matches are not output by **ugrep** by default, which
  avoids making mistakes that may produce "random" results.  For example, with
  GNU/BSD grep, pattern `a*` matches every line in the input, and actually
  matches `xyz` three times (the empty transitions before and between the `x`,
  `y`, and `z`).  Allowing empty matches requires **ugrep** option `-Y`.
  Patterns that start with `^` and end with `$` are permitted to match empty,
  e.g. `^\h*$`, by implicitly enabling `-Y`.
- **ugrep** offers *negative patterns* `-N PATTERN`, which are patterns of the
  form `(?^X)` to skip input that matches `X`.  Negative patterns can be used
  to skip strings and comments when searching for identifiers in source code
  and find matches that aren't in strings and comments.  Predefined `zap`
  patterns use nagative patterns, for example, use `-f cpp/zap_comments` to
  ignore pattern matches in C++ comments.
- **ugrep** produces hexdumps with `-W` (output binary matches in hex with text
  matches output as usual) and `-X` (output all matches in hex).
- **ugrep** searches compressed files with option `-z`.
- **ugrep** searches cpio, jar, pax, tar, and zip archives with option `-z`.
- **ugrep** searches pdf, doc, docx, xls, xlsx, epub, and more with `--filter`
  using third-party format conversion utilities as plugins.
- **ugrep** options `-O`, `-M`, and `-t` specify file extensions, file
  signature magic byte patterns, and predefined file types, respectively.  This
  allows searching for certain types of files in directory trees, for example
  with recursive search options `-R` and `-r`.  Options `-O`, `-M`, and `-t`
  also applies to archived files in cpio, jar, pax, tar, and zip files.
- **ugrep** option `-k`, `--column-number` to display the column number, taking
  tab spacing into account by expanding tabs, as specified by option `--tabs`.
- **ugrep** option `-f` uses `GREP_PATH` environment variable or the predefined
  patterns installed in `/usr/local/share/ugrep/patterns`.  If `-f` is
  specified and also one or more `-e` patterns are specified, then options
  `-F`, `-x`, and `-w` do not apply to `-f` patterns.  This is to avoid
  confusion when `-f` is used with predefined patterns that may no longer work
  properly with these options.
- **ugrep** option `-P` (Perl regular expressions) supports backreferences
  (with `--format`) and lookbehinds, which uses the Boost.Regex library for
  fast Perl regex matching with a PCRE-like syntax.
- **ugrep** option `-b` with option `-o` or with option `-u`, **ugrep**
  displays the exact byte offset of the pattern match instead of the byte
  offset of the start of the matched line reported by GNU/BSD grep.
- **ugrep** option `-u`, `--ungroup` to not group matches per line.  This
  option displays a matched input line again for each additional pattern match
  on the line.  This option is particularly useful with option `-c` to report
  the total number of pattern matches per file instead of the number of lines
  matched per file.
- **ugrep** option `-D, --devices=ACTION` is `skip` by default, instead of
  `read`.  This prevents unexpectedly hanging on named pipes in directories
  that are recursively searched, as may happen with GNU/BSD grep that `read`
  devices by default.
- **ugrep** always assumes UTF-8 locale to support Unicode, i.e.
  `LANG=en_US.UTF-8`.  By contrast, grep is locale-sensitive, which often
  causes problems or produces surprising results.  Simply put, the
  [principle of least astonishment](https://en.wikipedia.org/wiki/Principle_of_least_astonishment)
  rules!
- **ugrep** does not use a `.greprc` configuration file or a `GREP_OPTIONS`
  environment variable, because the behavior of **ugrep** must be portable.
  Also GNU grep abandoned `GREP_OPTIONS` for this reason.  Instead, please use
  aliases to create new commands with specific search options.
- BSD grep (e.g. on Mac OS X) has limitations and some bugs that **ugrep**
  fixes (options `-r` versus `-R`), support for `GREP_COLORS`, and more.

<a name="todo"/>

### Feature wish list

Let us know what features you like to have in ugrep.  Here are a couple that we
are considering:

- The speed of matching multiple words is faster than GNU grep (ugrep uses
  Bitap and hashing), but Hyperscan may be slightly faster as it uses SIMD/AVX
  so it makes sense to look into that.

<a name="tutorial"/>

Tutorial
--------

<a name="examples"/>

### Examples

To search for the identifier `main` as a word (`-w`) recursively (`-r`) in
directory `myproject`, showing the matching line (`-n`) and column (`-k`)
numbers next to the lines matched:

    ugrep -r -n -k -w 'main' myproject

This search query also finds `main` in strings and comment blocks.  With
**ugrep** we can use *negative patterns* with option `-N` to skip unwanted
matches in C/C++ quoted strings and comment blocks:

    ugrep -r -nkw 'main' -N '"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*([^*]|(\*+[^*/]))*\*+\/' myproject

This is a lot of work to type in correctly!  If you are like me, I'm lazy and
don't want to spend time fiddling with regex patterns when I am working on
something more important.  There is an easier way by using **ugrep**'s
predefined patterns (`-f`) that are installed with the tool:

    ugrep -r -nkw 'main' -f c/zap_strings -f c/zap_comments myproject

This query also searches through other files than C/C++ source code, like
READMEs, Makefiles, and so on.  We're also skipping symlinks with `-r`.  So
let's refine this query by selecting C/C++ files only using option `-tc,c++`
and include symlinks to files and directories with `-R`:

    ugrep -R -tc,c++ -nkw 'main' -f c/zap_strings -f c/zap_comments myproject

What if we are only looking for the identifier `main` but not as a function
`main(`?  We can use a negative pattern for this to skip unwanted `main\h*(`
pattern matches:

    ugrep -R -tc,c++ -nkw -e 'main' -N 'main\h*\(' -f c/zap_strings -f c/zap_comments myproject

This uses the `-e` and `-N` options to explicitly specify a pattern and a
negative pattern, respectively, which is essentially forming the pattern
`main|(?^main\h*\()', where `\h` matches space and tab.  In general, negative
patterns are useful to filter out pattern matches we are not interested in.

As another example, we may want to search for the word `FIXME` in C/C++ comment
blocks.  To do so we can first select the comment blocks with **ugrep**'s
predefined `c/comments` pattern AND THEN select lines with `FIXME` using a
pipe:

    ugrep -R -tc,c++ -nk -f c/comments myproject | ugrep -w 'FIXME'

Filtering results with pipes is generally easier than using AND-OR logic that
some search tools use.  This approach follows the Unix spirit to keep utilities
simple and use them in combination for more complex tasks.

Say we want to produce a sorted list of all identifiers found in Java source
code while skipping strings and comments:

    ugrep -R -tjava -f java/names -f java/zap_strings -f java/zap_comments myproject | sort -u

This matches Java Unicode identifiers using the regex
`\p{JavaIdentifierStart}\p{JavaIdentifierPart}*` defined in
`patterns/java/names`.

With traditional grep and grep-like tools it takes great effort to recursively
search for the C/C++ source file that defines function `qsort`, requiring
something like this:

    ugrep -R --include='*.c' --include='*.cpp' '^([ \t]*[[:word:]:*&]+)+[ \t]+qsort[ \t]*\([^;\n]+$' myproject

Fortunately, with **ugrep** we can simply select all function definitions in
files with extension `.c` or `.cpp` by using option `-Oc,cpp` and by using a
predefined pattern `functions` that is installed with the tool to produce
all function definitions.  Then we select the one we want:

    ugrep -R -Oc,cpp -nk -f c/functions | ugrep 'qsort'

Note that we could have used `-tc,c++` to select C/C++ files, but this also
includes header files when we want to only search `.c` and `.cpp` files.  To
display the list of file name extensions searched for all available options for
`-t` use:

    ugrep -tlist

We can also skip files and directories from being searched that are defined in
`.gitignore`.  To do so we use `--ignore-files` to exclude any files and
directories from recursive searches that match the globs in `.gitignore`, when
one ore more`.gitignore` files are found:

    ugrep -R -tc++ --color --no-hidden --ignore-files -f c++/defines

This searches C++ files (`-tc++`) in the working directory for `#define`
lines (`-f c++/defines`), while skipping files and directories declared in
`.gitignore` and skipping hidden files.  If you find this too long to type then
define an alias to search GitHub directories:

    alias ugit='ugrep -R --color --no-hidden --ignore-files'
    ugit -tc++ -f c++/defines

To list all files in a GitHub project directory that are not ignored by
`.gitignore` file(s) and are not hidden:

    ugit -l ''

Where `-l` (files with matches) lists the files specified in `.gitignore`
matched by the empty pattern `''`, which is typically used to match any
non-empty file (non-zero-length file, as per POSIX.1 grep standard).

To highlight matches when pushed through a chain of pipes we should use
`--color=always`:

    ugit --color=always -tc++ -f c++/defines | ugrep -w 'FOO.*'

This returns a color-highlighted list of all `#define FOO...` macros in C/C++
source code files, skipping files defined in `.gitignore`.

Note that the complement of `--exclude` is not `--include`, because exclusions
always override inclusions, so we cannot reliably list the files that are
ignored with `--include-from='.gitignore'`.  Only files explicitly specified
with `--include` and directories explicitly specified with `--include-dir` are
visited.  The `--include-from` from lists globs that are considered both files
and directories to add to `--include` and `--include-dir`, respectively.  This
means that when directory names and directory paths are not explicitly listed
in this file then it will not be visited using `--include-from`.

<a name="help"/>

### Displaying helpful info

The ugrep man page:

    man ugrep

To show a help page:

    ugrep --help

To show a list of `-t TYPES` option values:

    ugrep -tlist

<a name="recursion"/>

### Recursively list matching files with options -R or -r with -L or -l

    -L, --files-without-match
            Only the names of files not containing selected lines are written
            to standard output.  Pathnames are listed once per file searched.
            If the standard input is searched, the string ``(standard input)''
            is written.
    -l, --files-with-matches
            Only the names of files containing selected lines are written to
            standard output.  ugrep will only search a file until a match has
            been found, making searches potentially less expensive.  Pathnames
            are listed once per file searched.  If the standard input is
            searched, the string ``(standard input)'' is written.
    -R, --dereference-recursive
            Recursively read all files under each directory.  Follow all
            symbolic links, unlike -r.  When -J1 is specified, files are
            searched in the same order as specified.
    -r, --recursive
            Recursively read all files under each directory, following symbolic
            links only if they are on the command line.  When -J1 is specified,
            files are searched in the same order as specified.

To recursively list all non-empty files in the working directory, following
symbolic links:

    ugrep -Rl ''

To recursively list all non-empty files in directory `mydir`, not following any
symbolic links (except when on the command line such as `mydir`):

    ugrep -rl '' mydir

To recursively list all non-empty files on the path specified, while visiting
sub-directories only, i.e. directories `mydir/` and `mydir/sub/` are visited:

    ugrep -Rl --max-depth=2 '' mydir

To recursively list all non-empty files with extension .sh, with `-Osh`:

    ugrep -Rl -Osh ''

To recursively list all shell scripts based on extensions and shebangs with
`-tShell`:

    ugrep -Rl -tShell ''

To recursively list all shell scripts based on extensions only with `-tshell`:

    ugrep -Rl -tshell ''

<a name="not"/>

### Search this but not that with -v, -e, -N, -f, -L, -w, -x

    -e PATTERN, --regexp=PATTERN
            Specify a PATTERN used during the search of the input: an input
            line is selected if it matches any of the specified patterns.
            Note that longer patterns take precedence over shorter patterns.
            This option is most useful when multiple -e options are used to
            specify multiple patterns, when a pattern begins with a dash (`-'),
            to specify a pattern after option -f or after the FILE arguments.
    -f FILE, --file=FILE
            Read one or more newline-separated patterns from FILE.  Empty
            pattern lines in FILE are not processed.  If FILE does not exist,
            the GREP_PATH environment variable is used as the path to FILE.
            If that fails, looks for FILE in /usr/local/share/ugrep/pattern.
            When FILE is a `-', standard input is read.  This option may be
            repeated.
    -L, --files-without-match
            Only the names of files not containing selected lines are written
            to standard output.  Pathnames are listed once per file searched.
            If the standard input is searched, the string ``(standard input)''
            is written.
    -N PATTERN, --not-regexp=PATTERN
            Specify a negative PATTERN used during the search of the input: an
            input line is selected only if it matches any of the specified
            patterns when PATTERN does not match.  Same as -e (?^PATTERN).
            Negative PATTERN matches are removed before any other specified
            patterns are matched.  Note that longer patterns take precedence
            over shorter patterns.  This option may be repeated.
    -v, --invert-match
            Selected lines are those not matching any of the specified
            patterns.
    -w, --word-regexp
            The PATTERN is searched for as a word (as if surrounded by \< and
            \>).  If a PATTERN is specified (or -e PATTERN or -N PATTERN), then
            this option does not apply to -f FILE patterns.
    -x, --line-regexp
            Only input lines selected against the entire PATTERN is considered
            to be matching lines (as if surrounded by ^ and $).  If a PATTERN
            is specified (or -e PATTERN or -N PATTERN), then this option does
            not apply to -f FILE patterns.

To display lines in file `myfile.sh` but not lines matching `^[ \t]*#`:

    ugrep -v '^[ \t]*#' myfile.sh

To search `myfile.cpp` for lines with `FIXME` and `urgent`, but not `Scotty`:

    ugrep FIXME myfile.cpp | ugrep urgent | ugrep -v Scotty

To search for words starting with `disp` without matching `display` in file
`myfile.py` by using a "negative pattern" `-N '/<display\>'` where `-N`
specifies an additional negative pattern to skip matches:

    ugrep '\<disp' -N '/<display\>' myfile.py

To search for lines with the word `display` in file `myfile.py` skipping this
word in strings and comments, where `-f` specifies patterns in files which are
predefined patterns in this case:

    ugrep -n -w 'display' -f python/zap_strings -f python/zap_comments myfile.py

To display lines that are not blank lines:

    ugrep -x '.*' -N '\h*' myfile.py

Same, but using `-v` and `-x` with `\h*`, i.e. pattern `^\h*$`:

    ugrep -v -x '\h*' myfile.py

To recursively list all Python files that do not contain the word `display`,
allowing the word to occur in strings and comments:

    ugrep -RL -tPython -w 'display' -f python/zap_strings -f python/zap_comments

To search `myfile.cpp` for lines with `TODO` or `FIXME` but not both on the
same line, like XOR:

    ugrep -e TODO -e FIXME -N '.*TODO.*FIXME.*' -N '.*FIXME.*TODO.*' myfile.cpp

<a name="unicode"/>

### Searching ASCII, Unicode, and other encodings with -Q

    -Q ENCODING, --encoding=ENCODING
            The input file encoding.

ASCII, UTF-8, UTF-16, and UTF-32 files do not require this option, assuming
that UTF-16 and UTF-32 files include a UTF BOM as usual.  Other file encodings
require option `-Q` (`--encoding=`) with a parameter:

encoding               | `-Q` parameter
---------------------- | --------------
ASCII                  | *n/a*
UTF-8                  | *n/a*
UTF-16 with BOM        | *n/a*
UTF-32 with BOM        | *n/a*
UTF-16 BE w/o BOM      | `UTF-16` or `UTF-16BE`
UTF-16 LE w/o BOM      | `UTF-16LE`
UTF-32 w/o BOM         | `UTF-32` or `UTF-32BE`
UTF-32 w/o BOM         | `UTF-32LE`
Latin-1                | `ISO-8859-1`
ISO-8859-1             | `ISO-8859-1`
ISO-8859-2             | `ISO-8859-2`
ISO-8859-3             | `ISO-8859-3`
ISO-8859-4             | `ISO-8859-4`
ISO-8859-5             | `ISO-8859-5`
ISO-8859-6             | `ISO-8859-6`
ISO-8859-7             | `ISO-8859-7`
ISO-8859-8             | `ISO-8859-8`
ISO-8859-9             | `ISO-8859-9`
ISO-8859-10            | `ISO-8859-10`
ISO-8859-11            | `ISO-8859-11`
ISO-8859-13            | `ISO-8859-13`
ISO-8859-14            | `ISO-8859-14`
ISO-8859-15            | `ISO-8859-15`
ISO-8859-16            | `ISO-8859-16`
MAC (CR=newline)       | `MAC`
MacRoman (CR=newline)  | `MACROMAN`
EBCDIC                 | `EBCDIC`
DOS code page 437      | `CP437`
DOS code page 850      | `CP850`
DOS code page 858      | `CP858`
Windows code page 1250 | `CP1250`
Windows code page 1251 | `CP1251`
Windows code page 1252 | `CP1252`
Windows code page 1253 | `CP1253`
Windows code page 1254 | `CP1254`
Windows code page 1255 | `CP1255`
Windows code page 1256 | `CP1256`
Windows code page 1257 | `CP1257`
Windows code page 1258 | `CP1258`
KOI8-R                 | `KOI8-R`
KOI8-U                 | `KOI8-U`
KOI8-RU                | `KOI8-RU`

Note that regex patterns are always specified in UTF-8 (includes ASCII).  To
search binary files with binary patterns, see
[searching and displaying binary files with -U, -W, and -X](#binary).

To recursively list all files and symlinks that are ASCII (i.e. 7-bit):

    ugrep -RL '[^[:ascii:]]'

To recursively list all files and symlinks that are non-ASCII, i.e. UTF-8,
UTF-16, and UTF-32 files with non-ASCII Unicode characters (U+0080 and up):

    ugrep -Rl '[^[:ascii:]]'

To check if a file contains non-ASCII Unicode (U+0080 and up):

    ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"

To remove invalid Unicode characters from a file (note that `-o` does not work
because binary data is detected and rejected and newlines are added, but
`--format="%o%` does not check for binary and copies the match "as is"):

    ugrep "\p{Unicode}" --format="%o" badfile.txt

To recursively list files with invalid UTF content (i.e. invalid UTF-8 byte
sequences or files that contain any UTF-8/16/32 code points that are outside
the valid Unicode range) by matching any code point with `.` and by using a
negative pattern `-N '\p{Unicode}'`:

    ugrep -Rl '.' -N '\p{Unicode}'

To display lines containing laughing face emojis:

    ugrep '[😀-😏]' emojis.txt

The same results are obtained using `\x{hhhh}` to select a Unicode character
range:

    ugrep '[\x{1F600}-\x{1F60F}]' emojis.txt

To display lines containing the names Gödel (or Goedel), Escher, or Bach:

    ugrep 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To search for `lorem` in lower or upper case in a UTF-16 file that is marked
with a UTF-16 BOM:

    ugrep -iw 'lorem' utf16lorem.txt

To search utf16lorem.txt when this file has no UTF-16 BOM, using `-Q`:

    ugrep -Q=UTF-16 -iw 'lorem' utf16lorem.txt

To search file `spanish-iso.txt` encoded in ISO-8859-1:

    ugrep -Q=ISO-8859-1 -w 'año' spanish-iso.txt

<a name="multiline"/>

### Matching multiple lines of text

    -o, --only-matching
            Print only the matching part of lines.  When multiple lines match,
            the line numbers with option -n are displayed using `|' as the
            field separator for each additional line matched by the pattern.
            This option cannot be combined with options -A, -B, -C, -v, and -y.

Multiple lines may be matched by patterns that match newline `\n` characters,
unless one or more context options `-A`, `-B`, `-C`, `-y` is used, or `-v` that
apply to lines.  Use option `-o` to output the match only, not the full
lines(s) that match.

To match C/C++ `/*...*/` multi-line comments, color highlighted:

    ugrep --color '/\*([^*]|(\*+[^*/]))*\*+\/' myfile.cpp

To match C/C++ comments using the predefined `c/comments` patterns with
`-f c/comments`, restricted to the matching part only with option `-o`:

    ugrep -of c/comments myfile.cpp

Same as `sed -n '/begin/,/end/p'`: to match all lines between a line containing
`begin` and the first line after that containing `end`, using lazy repetition:

    ugrep -o '.*begin(.|\n)*?end.*' myfile.txt

<a name="context"/>

### Displaying context with -A, -B, -C, and -y

    -A NUM, --after-context=NUM
            Print NUM lines of trailing context after matching lines.  Places
            a --group-separator between contiguous groups of matches.  See also
            the -B, -C, and -y options.
    -B NUM, --before-context=NUM
            Print NUM lines of leading context before matching lines.  Places
            a --group-separator between contiguous groups of matches.  See also
            the -A, -C, and -y options.
    -C[NUM], --context[=NUM]
            Print NUM lines of leading and trailing context surrounding each
            match.  The default is 2 and is equivalent to -A 2 -B 2.  Places
            a --group-separator between contiguous groups of matches.
            No whitespace may be given between -C and its argument NUM.
    -y, --any-line
            Any matching or non-matching line is output.  Non-matching lines
            are output with the `-' separator as context of the matching lines.
            See also the -A, -B, and -C options.

To display 2 lines of context before and after a matching line (note that `-C2`
should not be specified as `-C 2` as per GNU/BSD grep exception that ugrep
obeys, because `-C` specifies 3 lines of context by default):

    ugrep --color -C2 'FIXME' myfile.cpp

To show three lines of context after a matched line:

    ugrep --color -A3 'FIXME.*' myfile.cpp:

To display one line of context before each matching line with a C function
definition (C names are non-Unicode):

    ugrep --color -B1 -f c/functions myfile.c

To display one line of context before each matching line with a C++ function
definition (C++ names may be Unicode):

    ugrep --color -B1 -f c++/functions myfile.cpp

To display any non-matching lines as context for matching lines with `-y`:

    ugrep --color -y -f c++/functions myfile.cpp

To display a hexdump of a matching line with one line of hexdump context:

    ugrep --color -C1 -UX '\xaa\xbb\xcc' a.out

Context within a line is displayed by simply adjusting the pattern and using
option `-o`, for example to show the word (when present) before and after a
match of `pattern` (`\w+` matches a word and `\h+` matches spacing), where `-U`
matches ASCII words instead of Unicode:

    ugrep -o -U '(\w+\h+)?pattern(\h+\w+)?' myfile.cpp

Same, but with line numbers (`-n`), column numbers (`-k`), tab spacing (`-T`)
for all matches separately (`-u`), color highlighting, and showing up to 8
characters of context instead of a single word:

    ugrep -onkTg -U '.{0,8}pattern.{0,8}' myfile.cpp | ugrep --color 'pattern'

<a name="source"/>

### Searching source code using -f, -O, and -t

    -f FILE, --file=FILE
            Read one or more newline-separated patterns from FILE.  Empty
            pattern lines in FILE are not processed.  If FILE does not exist,
            the GREP_PATH environment variable is used as the path to FILE.
            If that fails, looks for FILE in /usr/local/share/ugrep/pattern.
            When FILE is a `-', standard input is read.  This option may be
            repeated.
    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE when
            encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories specified as FILE arguments are not ignored.
            This option may be repeated.
    -O EXTENSIONS, --file-extensions=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -M and -t to expand the recursive search.
    -t TYPES, --file-type=TYPES
            Search only files associated with TYPES, a comma-separated list of
            file types.  Each file type corresponds to a set of filename
            extensions passed to option -O.  For capitalized file types, the
            search is expanded to include files with matching file signature
            magic bytes, as if passed to option -M.  When a type is preceeded
            by a `!' or a `^', excludes files of the specified type.  This
            option may be repeated.
    --stats
            Display statistics on the number of files and directories searched.
            Display the inclusion and exclusion constraints applied.

The file types are listed with `ugrep -tlist`.  The list is based on
established filename extensions and "magic bytes".  If you have a file type
that is not listed, use options `-O` and/or `-M`.  You may want to define an
alias, e.g. `alias ugft='ugrep -Oft'` as a shorthand to search files with
filename suffix `.ft`.

To recursively display function definitions in C/C++ files (`.h`, `.hpp`, `.c`,
`.cpp` etc.) with line numbers with `-tc++`, `-o`, `-n`, and `-f c++/functions`:

    ugrep -R -on -tc++ -f c++/functions

To recursively display function definitions in `.c` and `.cpp` files with line
numbers with `-Oc,cpp`, `-o`, `-n`, and `-f c++/functions`:

    ugrep -R -on -Oc,cpp -f c++/functions

To recursively list all shell files with `-tShell` to match filename extensions
and files with shell shebangs, except files with suffix `.sh`:

    ugrep -Rl -tShell -O^sh ''

To recursively list all non-shell files with `-t^Shell`:

    ugrep -Rl -t^Shell ''

To recursively list all shell files with shell shebangs that have no shell
filename extensions:

    ugrep -Rl -tShell -t^shell ''

To search for lines with `FIXME` in C/C++ comments, excluding `FIXME` in
multi-line strings:

    ugrep -n 'FIXME' -f c++/zap_strings myfile.cpp

To read patterns `TODO` and `FIXME` from standard input to match lines in the
input, while excluding matches in C++ strings:

    ugrep -on -f - -f c++/zap_strings myfile.cpp <<END
    TODO
    FIXME
    END

To display XML element and attribute tags in an XML file, restricted to the
matching part with `-o`, excluding tags that are placed in (multi-line)
comments:

    ugrep -o -f xml/tags -f xml/zap_comments myfile.xml

<a name="archives"/>

### Searching compressed files and archives with -z

    -z, --decompress
            Decompress files to search, when compressed.  Archives (.cpio,
            .jar, .pax, .tar, .zip) and compressed archives (e.g. .taz, .tgz,
            .tpz, .tbz, .tbz2, .tb2, .tz2, .tlz, and .txz) are searched and
            matching pathnames of files in archives are output in braces.  If
            -g, -O, -M, or -t is specified, searches files within archives
            whose name matches globs, matches file name extensions, matches
            file signature magic bytes, or matches file types, respectively.
            Supported compression formats: gzip (.gz), compress (.Z), zip,
            bzip2 (requires suffix .bz, .bz2, .bzip2, .tbz, .tbz2, .tb2, .tz2),
            lzma and xz (requires suffix .lzma, .tlz, .xz, .txz).

Compressed files with gzip (`.gz`), compress (`.Z`), bzip2 (`.bz`, `.bz2`,
`.bzip2`), lzma (`.lzma`), and xz (`.xz`) are searched with option `-z`.  This
option does not require files to be compressed.  Uncompressed files are
searched also.

Archives (cpio, jar, pax, tar, and zip) are searched with option `-z`.  Regular
files in an archive that match are output with the archive pathnames enclosed
in `{` and `}` braces.  Supported tar formats are v7, ustar, gnu, oldgnu, and
pax.  Supported cpio formats are odc, newc, and crc.  Not supported is the
obsolete non-portable old binary cpio format.  Archive formats cpio, tar, and
pax are automatically recognized with option `-z` based on their content,
independent of their filename suffix.

The gzip, compress, and zip formats are automatically detected, which is useful
when reading gzip-compressed data from standard input, e.g. input redirected
from a pipe.  Other compression formats require a filename suffix: `.bz`,
`.bz2`, or `.bzip2` for bzip2, `.lzma` for lzma, and `.xz` for xz.  Also the
compressed tar archive shorthands `.taz`, `.tgz`, and `.tpz` for gzip, `.tbz`,
`.tbz2`, `.tb2`, and `.tz2` for bzip2, `.tlz` for lzma, and `.txz` for xz are
recognized.  To decompress these formats from standard input, use option
`--label='stdin.bz2'` for bzip2, `--label='stdin.lzma'` for lzma, and
`--label='stdin.xz'` for xz.  The name `stdin` is arbitrary and may be omitted:

format    | filename suffix         | tar/pax archive short suffix    | suffix required? | ugrep from stdin | lib required |
--------- | ----------------------- | ------------------------------- | ---------------- | ---------------- | ------------ |
gzip      | `.gz`                   | `.taz`, `.tgz`, `.tpz`          | no               | automatic        | libz         |
compress  | `.Z`                    |                                 | no               | automatic        | *built-in*   |
zip       | `.zip`, `.ZIP`          |                                 | no               | automatic        | libz         |
bzip2     | `.bz`, `.bz2`, `.bzip2` | `.tb2`, `.tbz`, `.tbz2`, `.tz2` | yes              | `--label=.bz2`   | libbz2       |
lzma      | `.lzma`                 | `.tlz`                          | yes              | `--label=.lzma`  | liblzma      |
xz        | `.xz`                   | `.txz`                          | yes              | `--label=.xz`    | liblzma      |

The gzip, bzip2, and xz formats support concatenated compressed files.
Concatenated compressed files are searched as one file.

Supported zip compression methods are stored (0), deflate (8), bzip2 (12) if
libbz2 is available, lzma (14) and xz (95) if liblzma is available.  Archives
compressed within zip archives are searched:  all cpio, pax, and tar files in
zip archives are automatically recognized and searched.  Compressed files in
archives are not recognized and searched however, such as any compressed files
stored in cpio, pax, tar, and zip archives, which are considered binary files.
Searching encrypted zip archives is not supported (perhaps in future releases,
depending on requests).

When option `-z` is used with options `-g`, `-O`, `-M`, or `-t`, archives and
compressed and uncompressed files that match the filename selection criteria
(glob, extension, magic bytes, or file type) are searched only.  For example,
`ugrep -r -z -tc++` searches C++ files such as `main.cpp`, and also
`main.cpp.gz` and `main.cpp.xz` when present.  Also any cpio, pax, tar, and zip
archives when present are searched for C++ files such as `main.cpp`.  Use
option `--stats` to see a list of the glob patterns applied to filter file
pathnames in the recursive search and when searching archive contents.

When option `-z` is used with options `-g`, `-O`, `-M`, or `-t` to search cpio,
jar, pax, tar, and zip archives, archived files that match the filename selection
criteria are searched only.

Option `-z` uses thread task parallelism to speed up searching by running the
decompressor while searching the decompressed stream concurrently.

To recursively search C++ files including compressed files for the word
`my_function`, while skipping C and C++ comments:

    ugrep -z -r -tc++ -Fw my_function -f cpp/zap_comments

To search bzip2, xz, and lzma-compressed data on standard input, option
`--label` may be used to specify the extension corresponding to the compression
format to force decompression when the bzip2 extension is not available to
ugrep, for example:

    cat myfile.bz2 | ugrep -z --label='stdin.bz2' 'xyz'

To search file `main.cpp` in `project.zip` for `TODO` and `FIXME` lines:

    ugrep -z -g main.cpp -w -e 'TODO' -e 'FIXME' project.zip

To search tarball `project.tar.gz` for C++ files with `TODO` and `FIXME` lines:

    ugrep -z -tc++ -w -e 'TODO' -e 'FIXME' project.tar.gz

To display and page through all C++ files in tarball `project.tgz`:

    ugrep --color --pager -z -tc++ '' project.tgz

To list the files matching the gitignore-style glob `/**/projects/project1.*`
in `projects.tgz`, by selecting files containing in the archive the text
`December 12`:

    ugrep -z -l -g '/**/projects/project1.*' -F 'December 12' projects.tgz

To extract C++ files that contain `FIXME` from `project.tgz`, we use `-m1`
with `--format="'%z '"` to generate a list of pathnames of file located in the
archive that match the word `FIXME`:

    tar xzf project.tgz `ugrep -ztc++ -m1 --format="'%z '" -w FIXME project.tgz`

To perform a depth-first search with `find`, then use `cpio` and `ugrep` to
search the files:

    find . -depth -print | cpio -o | ugrep --color -z 'xyz'

<a name="magic"/>

### Find files by file signature and shebang "magic bytes" with -M and -t

    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE when
            encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories specified as FILE arguments are not ignored.
            This option may be repeated.
    -M MAGIC, --file-magic=MAGIC
            Only files matching the signature pattern MAGIC are searched.  The
            signature \"magic bytes\" at the start of a file are compared to
            the MAGIC regex pattern.  When matching, the file will be searched.
            When MAGIC is preceded by a `!' or a `^', skip files with matching
            MAGIC signatures.  This option may be repeated and may be combined
            with options -O and -t to expand the search.  Every file on the
            search path is read, making searches potentially more expensive.
    -t TYPES, --file-type=TYPES
            Search only files associated with TYPES, a comma-separated list of
            file types.  Each file type corresponds to a set of filename
            extensions passed to option -O.  For capitalized file types, the
            search is expanded to include files with matching file signature
            magic bytes, as if passed to option -M.  When a type is preceeded
            by a `!' or a `^', excludes files of the specified type.  This
            option may be repeated.
    --stats
            Display statistics on the number of files and directories searched.
            Display the inclusion and exclusion constraints applied.

To recursively list all files that start with `#!` shebangs:

    ugrep -Rl -M'#!' ''

To recursively list all files that start with `#` but not with `#!` shebangs:

    ugrep -Rl -M'#' -M'^#!' ''

To recursively list all Python files (extension `.py` or a shebang) with
`-tPython`:

    ugrep -Rl -tPython ''

To recursively list all non-shell files with `-t^Shell`:

    ugrep -Rl -t^Shell ''

To list Python files (extension `.py` or a shebang) that have import
statements, excluding hidden files:

    ugrep -Rl --no-hidden -tPython -f python/imports
 
<a name="filter"/>

### Using filter utilities to search documents with --filter

    --filter=COMMANDS
            Filter files through the specified COMMANDS first before searching.
            COMMANDS is a comma-separated list of `exts:command [option ...]',
            where `exts' is a comma-separated list of filename extensions and
            `command' is a filter utility.  The filter utility should read from
            standard input and write to standard output.  Files matching one of
            `exts` are filtered only.  One or more `option' separated by
            spacing may be specified, which are passed verbatim to the command.
            A `%' as `option' expands into the pathname to search.  For example,
            --filter='pdf:pdftotext % -' searches PDF files.  The `%' expands
            into a `-' when searching standard input.  Option --label=.ext may
            be used to specify extenion `ext' when searching standard input.

Filter utilities may be associated with specific filename extensions.  A
filter utility is selected based on the filename extension and executed by
forking a process.  The utility's standard input reads the open input file and
the utility's standard output is read and searched.  When a `%` is specified as
an option to the utility, the utility may open and read the file using the
expanded pathname.  When a utility is not found an error message is displayed.
When a utility fails to produce output, e.g. when the specified options for the
utility are invalid, the search is silently skipped.

**Warning:** option `--filter` should not be used with utilities that modify
files.  Otherwise searches may be unpredicatable.  In the worst case files may
be lost, for example when the specified utility replaces or deletes the file
passed to the command with `--filter` option `%`.

Common filter utilities are `cat` (concat, pass through), `tr` (translate),
`iconv` and `uconv` (convert), and more advanced document conversion utilities
such as [`pdftotext`](https://pypi.org/project/pdftotext) to convert PDF to
text, [`pandoc`](https://pandoc.org) to convert .docx, .epub, and other
document formats, [`soffice`](https://www.libreoffice.org) to convert office
documents, and [`csvkit`](https://pypi.org/project/csvkit) to convert
spreadsheets.  Also decompressors may be used as filter utilities, such as
`unzip`, `gunzip`, `bunzip2`, `unxz`, and `unlzma` that can decompress files to
standard output by specifying option `--stdout`.  However, **ugrep** option
`-z` is typically faster to search compressed files.

To recursively search files including PDF files in the working directory
without recursing into subdirectories, for `drink me` using the `pdftotext`
filter to convert PDF to text without preserving page breaks:

    ugrep --color -r --filter='pdf:pdftotext -nopgbrk % -' --max-depth=1 'drink me'

To recursively search text files for `eat me` while converting non-printable
characters in .txt and .md files using the `cat -v` filter:

    ugrep --color -r -ttext --filter='txt,md:cat -v' 'eat me'

The same, but specifying the .txt and .md filters separately:

    ugrep --color -r -ttext --filter='txt:cat -v, md:cat -v' 'eat me'

To recursively search and list the files that contain the word `Alice`,
including .docx and .epub documents using the `pandoc` filter:

    ugrep -rl -w --filter='docx,epub:pandoc --wrap=preserve -t markdown % -o -' 'Alice'

**Important:** the `pandoc` utility requires an input file and will not read
standard input.  Option `%` expands into the full pathname of the file to
search.  The output format specified is `markdown`, which is close enough to
text to be searched.

To recursively search and list the files that contain the word `Alice`,
including .odt, .doc, .docx, .rtf, .xls, .xlsx, .ppt, .pptx documents using the
`soffice` filter:

    ugrep -rl -w --filter='odt,doc,docx,rtf,xls,xlsx,ppt,pptx:soffice --headless --cat %' 'Alice'

**Important:** the `soffice` utility will not output any text when one or more
LibreOffice GUIs are open.  Make sure to quit all LibreOffice apps first.  This
looks like a bug, but the LibreOffice developers do not appear to fix this
(unless perhaps more people complain.)

To recursively search and display rows of .csv, .xls, and .xlsx spreadsheets
that contain `10/6` using the `in2csv` filter of csvkit:

    ugrep -r -Ocsv,xls,xlsx --filter='xls,xlsx:in2csv %' '10/6'

To search .docx, .xlsx, and .pptx files converted to XML for a match with
`10/6` using `unzip` as a filter:

    ugrep -lr -Odocx,xlsx,pptx --filter='docx,xlsx,pptx:unzip -p %' '10/6'

**Important:** unzipping docx, xlxs, pptx files produces extensive XML output
containing meta information and binary data such as images.  By contrast,
**ugrep** option `-z` with `-Oxml` selects the XML components only:

    ugrep -z -lr -Odocx,xlsx,pptx,xml '10/6'

**Note:** docx, xlsx, and pptx are zip files containing multiple components.
When selecting the XML components with option `-Oxml` in docx, xlsx, and pptx
documents, we should also specify `-Odocx,xlsx,pptx` to search these type of
files, otherwise these files will be ignored.

<a name="binary"/>

### Searching and displaying binary files with -U, -W, and -X

    -U, --binary
            Disables Unicode matching for binary file matching, forcing PATTERN
            to match bytes, not Unicode characters.  For example, -U '\xa3'
            matches byte A3 (hex) instead of the Unicode code point U+00A3
            represented by the two-byte UTF-8 sequence C2 A3.
    -W, --with-hex
            Output binary matches in hexadecimal, leaving text matches alone.
            This option is equivalent to the --binary-files=with-hex option.
    -X, --hex
            Output matches in hexadecimal.  This option is equivalent to the
            --binary-files=hex option.

To search a file for ASCII words, displaying text lines as usual while binary
content is shown in hex with `-U` and `-W`:

    ugrep --color -UW '\w+' myfile

To hexdump an entire file as a match with `-X`:

    ugrep --color -X '' myfile

To hexdump an entire file with `-X`, displaying line numbers and byte offsets
with `-nb` (here with `-y` to display all line numbers):

    ugrep --color -Xynb '' myfile

To hexdump lines containing one or more \0 in a (binary) file using a
non-Unicode pattern with `-U` and `-X`:

    ugrep --color -UX '\x00+' myfile

Same, but hexdump the entire file as context with `-y` (but this line-based
option does not permit matching patterns with newlines):

    ugrep --color -UX -y '\x00+' myfile

To match the binary pattern `A3..A3.` (hex) in a binary file without
Unicode pattern matching (which would otherwise match `\xaf` as a Unicode
character U+00A3 with UTF-8 byte sequence C2 A3) and display the results
in hex with `-X` with pager `less -R`:

    ugrep --color --pager -o -UX '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

To list all files containing a RPM signature, located in the `rpm` directory and
recursively below (see for example
[list of file signatures](https://en.wikipedia.org/wiki/List_of_file_signatures)):

    ugrep -RlU '\A\xed\xab\xee\xdb' rpm

<a name="hidden">

### Ignoring hidden files and binary files with --no-hidden and -I

    -I      Ignore matches in binary files.  This option is equivalent to the
            --binary-files=without-match option.
    --no-hidden
            Do not search hidden files and directories.

To recursively search while ignoring hidden files (Unix dotfiles starting with
a `.` and Windows hidden files), use option `--no-hidden`:

    ugrep -rl --no-hidden 'xyz'

To recursively search while ignoring binary files:

    ugrep -rl -I 'xyz'

To ignore specific binary files with extensions such as .exe, .bin, .out, .a,
use `--exclude` or `--exclude-from`:

    ugrep -rl --exclude-from=ignore_binaries 'xyz'

where `ignore_binaries` is a file containing a glob on each line to ignore
matching files, e.g.  `*.exe`, `*.bin`, `*.out`, `*.a`.  Because the command is
quite long to type, an alias for this is recommended, for example `ugs` (ugrep
source):

    alias ugs="ugrep --color --no-hidden --exclude-from=$HOME/ignore_binaries"
    ugs -rl 'xyz'

<a name="ignore"/>

### Ignoring .gitignore-specified files with --ignore-files

    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE when
            encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories specified as FILE arguments are not ignored.
            This option may be repeated.

Option `--ignore-files` looks for `.gitignore`, or the specified `FILE`, in
recursive searches.  When found, the `.gitignore` file is used to exclude the
files and directories matching the globs in `.gitignore` in the directory tree
rooted at the `.gitignore` location by temporarily overriding the `--exclude`
and `--exclude-dir` globs, i.e. the `.gitignore` exclusions are applied
precisely and exclusively.  Use `--stats` to show the selection criteria
applied to the search results and the locations of each `FILE` found.  To avoid
confusion, files and directories specified as command-line arguments to
**ugrep** are never ignored.

See also [Using gitignore-style globs to select directories and files to search](#gitignore).

To recursively search while skipping hidden files and ignoring files and
directories ignored by .gitignore (when present), use option `--ignore-files`:

    ugrep -rl --no-hidden --ignore-files 'xyz'

To recursively list all files that are not ignored by .gitignore (when present)
with `--ignore-files`:

    ugrep -Rl '' --ignore-files

To recursively list all shell scripts that are not ignored by .gitignore, when
present:

    ugrep -Rl -tShell '' --ignore-files

To recursively list all files that match the globs in .gitignore, when present:

    ugrep -RL '' --ignore-files

The same, but using both .gitignore and .ignore files, when either or both are
present:

    ugrep -RL '' --ignore-files --ignore-files=.ignore

To recursively list all files that are not ignored by .gitignore and are also
not excluded by `.git/info/exclude`:

    ugrep -Rl '' --ignore-files --exclude-from=.git/info/exclude

Same, but by creating a symlink to `.git/info/exclude` to make the exclusions
implicit:

    ln -s .git/info/exclude .ignore
    ugrep -Rl '' --ignore-files --ignore-files=.ignore

<a name="gitignore"/>

### Using gitignore-style globs to select directories and files to search

    --exclude=GLOB
            Skip files whose name matches GLOB using wildcard matching, same as
            -g !GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and \ to
            quote a wildcard or backslash character literally.  When GLOB
            contains a `/', full pathnames are matched.  Otherwise basenames
            are matched.  Note that --exclude patterns take priority over
            --include patterns.  This option may be repeated.
    --exclude-dir=GLOB
            Exclude directories whose name matches GLOB from recursive
            searches.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to
            quote a wildcard or backslash character literally.  When GLOB
            contains a `/', full pathnames are matched.  Otherwise basenames
            are matched.  Note that --exclude-dir patterns take priority over
            --include-dir patterns.  This option may be repeated.
    --exclude-from=FILE
            Read the globs from FILE and skip files and directories whose name
            matches one or more globs (as if specified by --exclude and
            --exclude-dir).  Lines starting with a `#' and empty lines in FILE
            are ignored.  When FILE is a `-', standard input is read.  This
            option may be repeated.
    -g GLOB, --glob=GLOB
            Search only files whose name matches GLOB, same as --include=GLOB.
            When GLOB is preceded by a `!' or a `^', skip files whose name
            matches GLOB, same as --exclude=GLOB.
    --ignore-files[=FILE]
            Ignore files and directories specified in a FILE when encountered
            in recursive searches.  The default is `.gitignore'.  Files and
            directories matching the globs in FILE are ignored in the directory
            tree rooted at each FILE's location by temporarily overriding
            --exclude and --exclude-dir globs.  This option may be repeated.
    --include=GLOB
            Search only files whose name matches GLOB using wildcard matching,
            same as -g GLOB.  GLOB can use **, *, ?, and [...] as wildcards,
            and \ to quote a wildcard or backslash character literally.  If
            GLOB contains a `/', full pathnames are matched.  Otherwise
            basenames are matched.  Note that --exclude patterns take priority
            over --include patterns.  This option may be repeated.
    --include-dir=GLOB
            Only directories whose name matches GLOB are included in recursive
            searches.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to
            quote a wildcard or backslash character literally.  When GLOB
            contains a `/', full pathnames are matched.  Otherwise basenames
            are matched.  Note that --exclude-dir patterns take priority over
            --include-dir patterns.  This option may be repeated.
    --include-from=FILE
            Read the globs from FILE and search only files and directories
            whose name matches one or more globs (as if specified by --include
            and --include-dir).  Lines starting with a `#' and empty lines in
            FILE are ignored.  When FILE is a `-', standard input is read.
            This option may be repeated.
    -O EXTENSIONS, --file-extensions=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -M and -t to expand the recursive search.
    --stats
            Display statistics on the number of files and directories searched.
            Display the inclusion and exclusion constraints applied.

See also [Including or excluding mounted file systems from searches](#fs).

Gitignore-style glob syntax and conventions:

glob     | matches
-------- | --------------------------------------------------------------------
`*`      | matches anything except a `/`
`?`      | matches any one character except a `/`
`[a-z]`  | matches one character in the selected range of characters
`[^a-z]` | matches one character not in the selected range of characters
`[!a-z]` | matches one character not in the selected range of characters
`/`      | when used at the begin of a glob, matches if pathname has no `/`
`**/`    | matches zero or more directories
`/**`    | when at the end of a glob, matches everything after the `/`
`\?`     | matches a `?` (or any character specified after the backslash)

When a glob contains a path separator `/`, the pathname is matched.  Otherwise
the basename of a file or directory is matched.  For example, `*.h` matches
`foo.h` and `bar/foo.h`. `bar/*.h` matches `bar/foo.h` but not `foo.h` and not
`bar/bar/foo.h`.  Use a leading `/` to force `/*.h` to match `foo.h` but not
`bar/foo.h`.

When a glob starts with a `!` as specified with `-g!GLOB`, or specified in a
`FILE` with `--include-from=FILE` or `--exclude-from=FILE`, it is negated.

To view a list of inclusions and exclusions that were applied to a search, use
option `--stats`.

To list only readable files with names starting with `foo` in the working
directory, that contain `xyz`, without producing warning messages with `-s` and
`-l`:

    ugrep -sl 'xyz' foo*

The same, but using recursion with a directory inclusion constraint:

    ugrep -Rl 'xyz' --include-dir='/foo*'

To recursively list files in the working directory, `docs`, and `docs/latest`,
but not below, that contain `xyz`:

    ugrep -l 'xyz' * docs/* docs/latest/*

To recursively list files in directory `docs/latest` and below, that contain
`xyz`:

    ugrep -Rl 'xyz' docs/latest

To only list files in the working directory and sub-directory `docs` but not
below, that contain `xyz`:

    ugrep -Rl 'xyz' --include-dir='docs'

To only list files in the working directory and in the sub-directories `docs`
and `docs/latest` but not below, that contain `xyz`:

    ugrep -Rl 'xyz' --include-dir='docs' --include-dir='docs/latest'

To only list files that are on a sub-directory path that includes sub-directory
`docs` anywhere, that contain `xyz`:

    ugrep -Rl 'xyz' -g '**/docs/**'

To recursively list .cpp files in the working directory and any sub-directory
at any depth, that contain `xyz`:

    ugrep -Rl 'xyz' -g '*.cpp'

The same, but using a .gitignore-style glob that matches pathnames (globs with
`/`) instead of matching basenames (globs without `/`) in the recursive search:

    ugrep -Rl 'xyz' -g '**/*.cpp'

Same, but using option `-Ocpp` to match file name extensions:

    ugrep -Rl -Ocpp 'xyz'

To recursively list all files in the working directory and below that are not
ignored by a specific .gitignore file:

    ugrep -Rl '' --exclude-from=.gitignore

To recursively list all files in the working directory and below that are not
ignored by one or more .gitignore files, when any are present:

    ugrep -Rl '' --ignore-files

<a name="fs"/>

### Including or excluding mounted file systems from searches

    --exclude-fs=MOUNTS
            Exclude file systems specified by MOUNTS from recursive searches,
            MOUNTS is a comma-separated list of mount points or pathnames of
            directories on file systems.  Note that --exclude-fs mounts take
            priority over --include-fs mounts.  This option may be repeated.
    --include-fs=MOUNTS
            Only file systems specified by MOUNTS are included in recursive
            searches.  MOUNTS is a comma-separated list of mount points or
            pathnames of directories on file systems.  --include-fs=. restricts
            recursive searches to the file system of the working directory
            only.  Note that --exclude-fs mounts take priority over
            --include-fs mounts.  This option may be repeated.

These options control recursive searches across file systems.  Mounted devices
and symbolic links to files and directories located on mounted file systems may
be included or excluded from recursive searches by specifying a mount point or
a pathname of any directory on the file system to specify the file system.

A list of mounted file systems is typically stored in `/etc/mtab`.

To restrict recursive searches to the file system of the working directory
only, without crossing into other file systems:

    ugrep -Rl --include-fs=. 'xyz' 

To exclude the file systems mounted at `/dev` and `/proc` from recursive
searches:

    ugrep -Rl --exclude-fs=/dev,/proc 'xyz' 

To only include the file system associated with drive `d:` in recursive
searches:

    ugrep -Rl --include-fs=d:/ 'xyz' 

To exclude `fuse` and `tmpfs` type file systems from recursive searches:

    exfs=`ugrep -w -e fuse -e tmpfs /etc/mtab | ugrep -P '^\S+ (\S+)' --format='%,%1'`
    ugrep -Rl --exclude-fs="$exfs" 'xyz'

<a name="count"/>

### Counting the number of matches with -c and -co

    -c, --count
            Only a count of selected lines is written to standard output.
            If -o or -u is specified, counts the number of patterns matched.
            If -v is specified, counts the number of non-matching lines.

To count the number of lines in a file:

    ugrep -c '' myfile.txt

To count the number of lines with `TODO`:

    ugrep -c -w 'TODO' myfile.cpp

To count the total number of `TODO` in a file, use `-c` and `-o`:

    ugrep -co -w 'TODO' myfile.cpp

To count the number of ASCII words in a file:

    ugrep -co '[[:word:]]+' myfile.txt

To count the number of ASCII and Unicode words in a file:

    ugrep -co '\w+' myfile.txt

To count the number of Unicode characters in a file:

    ugrep -co '\p{Unicode}' myfile.txt

To count the number of zero bytes in a file:

    ugrep -UX -co '\x00' image.jpg

<a name="fields"/>

### Displaying file, line, column, and byte offset info with -H, -n, -k, -b, and -T

    -b, --byte-offset
            The offset in bytes of a matched line is displayed in front of the
            respective matched line.  When used with option -u, displays the
            offset in bytes of each pattern matched.  Byte offsets are exact
            for ASCII, UTF-8, and raw binary input.  Otherwise, the byte offset
            in the UTF-8 converted input is displayed.
    -H, --with-filename
            Always print the filename with output lines.  This is the default
            when there is more than one file to search.
    -k, --column-number
            The column number of a matched pattern is displayed in front of the
            respective matched line, starting at column 1.  Tabs are expanded
            when columns are counted, see option --tabs.
    -n, --line-number
            Each output line is preceded by its relative line number in the
            file, starting at line 1.  The line number counter is reset for
            each file processed.
    -T, --initial-tab
            Add a tab space to separate the file name, line number, column
            number, and byte offset with the matched line.

To display the file name `-H`, line `-n`, and column `-k` numbers of matches in
`myfile.cpp`, with spaces and tabs to space the columns apart with `-T`:

    ugrep -THnk 'main' myfile.cpp

To display the line with `-n` of word `main` in `myfile.cpp`:

    ugrep --color -nw 'main' myfile.cpp

To display the entire file `myfile.cpp` with line `-n` numbers in color:

    ugrep --color -n '' myfile.cpp

To recursively search for C++ files with `main`, showing the line and column
numbers of matches with `-n` and `-k`:

    ugrep -r -nk -tc++ 'main'

To display the byte offset of matches with `-b`:

    ugrep -r -b -tc++ 'main'

To display the line and column numbers of matches in XML with `--xml`:

    ugrep -r -nk --xml -tc++ 'main'

<a name="color"/>

### Displaying colors with --color and paging the output with --pager

    --color[=WHEN], --colour[=WHEN]
            Mark up the matching text with the expression stored in the
            GREP_COLOR or GREP_COLORS environment variable.  The possible
            values of WHEN can be `never', `always', or `auto', where `auto'
            marks up matches only when output on a terminal.  The default is
            `auto'.
    --colors=COLORS, --colours=COLORS
            Use COLORS to mark up matching text.  COLORS is a colon-separated
            list of ANSI SGR parameters.  COLORS selectively overrides the
            colors specified by the GREP_COLORS environment variable.
    --pager[=COMMAND]
            When output is sent to the terminal, uses COMMAND to page through
            the output.  The default COMMAND is `less -R'.  This option makes
            --color=auto behave as --color=always.  Enables --break and
            --line-buffered.

To change the color palette, set the `GREP_COLORS` environment variable or use
`--colors`.  Its value is a colon-separated list of ANSI SGR parameters that
defaults to `cx=33:mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36` when not assigned:

param | result
----- | ------------------------------------------------------------------------
`sl=` | SGR substring for selected lines
`cx=` | SGR substring for context lines
`rv`  | Swaps the `sl=` and `cx=` capabilities when `-v` is specified
`mt=` | SGR substring for matching text in any matching line
`ms=` | SGR substring for matching text in a selected line.  The substring mt= by default
`mc=` | SGR substring for matching text in a context line.  The substring mt= by default
`fn=` | SGR substring for file names
`ln=` | SGR substring for line numbers
`cn=` | SGR substring for column numbers
`bn=` | SGR substring for byte offsets
`se=` | SGR substring for separators

Multiple SGR codes may be specified for a single parameter when separated by a
semicolon, e.g. `mt=1;31` specifies bright red.  The following SGR codes are
widely supported and available on most color terminals:

code | effect                     | code | effect
---- | -------------------------- | ---- | -------------------------------------
0    | reset font and color       |      |
1    | bold font and bright color | 21   | bold off
4    | underline                  | 24   | underline off
7    | reverse video              | 27   | reverse off
30   | black text                 | 90   | bright gray text
31   | red text                   | 91   | bright red text
32   | green text                 | 92   | bright green text
33   | yellow text                | 93   | bright yellow text
34   | blue text                  | 94   | bright blue text
35   | magenta text               | 95   | bright magenta text
36   | cyan text                  | 96   | bright cyan text
37   | white text                 | 97   | bright white text
40   | black background           | 100  | bright gray background
41   | dark red background        | 101  | bright red background
42   | dark green background      | 102  | bright green background
43   | dark yellow backgrounda    | 103  | bright yellow background
44   | dark blue background       | 104  | bright blue background
45   | dark magenta background    | 105  | bright magenta background
46   | dark cyan background       | 106  | bright cyan background
47   | dark white background      | 107  | bright white background

For more details, see Wikipedia
[ANSI escape code - SGR parameters](https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_parameters)

For example, to display matches in underlined bright green on bright selected
lines with a dark gray background, aiding in visualizing white space:

    export GREP_COLORS='sl=1;100:cx=44:ms=1;4;32;100:mc=1;4;32;44:fn=35:ln=32:cn=32:bn=32:se=36'

Modern Windows command interpreters support ANSI escape codes.  For example:

    SET GREP_COLORS=sl=1;37:cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36

To disable colors:

    SET GREP_COLORS=""

Color intensities may differ per platform and per terminal program used, which
affects readability.

Option `-y` outputs every line of input, including non-matching lines as
context.  The use of color helps distinguish matches from non-matching context.

To produce color-highlighted results:

    ugrep --color -R -n -k -tc++ 'FIXME.*'

To page through the color-highlighted results with pager (`less -R` by
default):

    ugrep --color --pager -R -n -k -tc++ 'FIXME'

To display a hexdump of a zip file itself (without decompressing), with
color-highlighted matches of the zip magic bytes `PK\x03\x04`:

    ugrep --color -y -UX 'PK\x03\x04' some.zip

To use predefined patterns to list all `#include` and `#define` in C++ files:

    ugrep --color -R -n -tc++ -f c++/includes -f c++/defines

Same, but overriding the color for selected lines with bold on a dark blue
background, e.g. to visualize spacing:

    ugrep --color --colors="sl=1;44" -R -n -tc++ -f c++/includes -f c++/defines

To list all `#define FOO...` macros in C++ files, color-highlighted:

    ugrep --color=always -R -n -tc++ -f c++/defines | ugrep 'FOO.*'

Same, but restricted to `.cpp` files only:

    ugrep --color=always -R -n -Ocpp -f c++/defines | ugrep 'FOO.*'

To monitor the system log for bug reports:

    tail -f /var/log/system.log | ugrep --color -i -w 'bug'

To search tarballs for matching names of PDF files (assuming bash is our shell):

    for tb in *.tar *.tar.gz *.tgz; do echo "$tb"; tar tfz "$tb" | ugrep '.*\.pdf$'; done

<a name="json"/>

### Output matches in JSON, XML, CSV, C++

    --cpp
            Output file matches in C++.  See also the --format and -u options.
    --csv
            Output file matches in CSV.  When option -H, -n, -k, or -b is used
            additional values are output.  See also the --format and -u options.
    --json
            Output file matches in JSON.  When option -H, -n, -k, or -b is used
            additional values are output.  See also the --format and -u options.
    --xml
            Output file matches in XML.  When option -H, -n, -k, or -b is used
            additional values are output.  See also the --format and -u options.

To recursively search for lines with `TODO` and display C++ file matches in
JSON with line number properties:

    ugrep -rtc++ -n --json 'TODO'

To recursively search for lines with `TODO` and display C++ file matches in
XML with line and column number attributes:

    ugrep -rtc++ -nk --xml 'TODO'

To recursively search for lines with `TODO` and display C++ file matches in CSV
format with file pathname, line number, and column number fields:

    ugrep -rtc++ --csv -Hnk 'TODO'

To extract a table from an HTML file and put it in C/C++ source code using
`-o`:

    ugrep -o --cpp '<tr>.*</tr>' index.html > table.cpp

<a name="format"/>

### Customized output with --format

    --format=FORMAT
            Output FORMAT-formatted matches.  See `man ugrep' section FORMAT
            for the `%' fields.  Options -A, -B, -C, -y, and -v are disabled.

The following output formatting options may be used:

option                  | result
----------------------- | ------------------------------------------------------
`--format=FORMAT`       | `FORMAT` for each match
`--format-begin=FORMAT` | `FORMAT` when beginning the search
`--format-open=FORMAT`  | `FORMAT` when opening a file and a match was found
`--format-close=FORMAT` | `FORMAT` when closing a file and a match was found
`--format-end=FORMAT`   | `FORMAT` when ending the search

In the `FORMAT` string, the following fields may be used:

field     | output
--------- | --------------------------------------------------------------------
`%[ARG]F` | if option `-H` is used: `ARG`, the file pathname, and separator
`%[ARG]H` | if option `-H` is used: `ARG`, the quoted pathname, and separator
`%[ARG]N` | if option `-n` is used: `ARG`, the line number and separator
`%[ARG]K` | if option `-k` is used: `ARG`, the column number and separator
`%[ARG]B` | if option `-b` is used: `ARG`, the byte offset and separator
`%[ARG]T` | if option `-T` is used: `ARG` and a tab character
`%[ARG]S` | if not the first match: `ARG` and separator, see also `%$`
`%[ARG]<` | if the first match: `ARG`
`%[ARG]>` | if not the first match: `ARG`
`%[SEP]$` | set field separator to `SEP` for the rest of the format fields
`%f`      | the file pathname
`%h`      | the quoted file pathname
`%z`      | the pathname in a (compressed) archive
`%n`      | the line number of the match
`%k`      | the column number of the match
`%b`      | the byte offset of the match
`%t`      | a tab character
`%s`      | the separator, see also `%S` and `%$`
`%~`      | a newline character
`%m`      | the number of matches or matched files
`%O`      | the matching line is output as is (a raw string of bytes)
`%o`      | the match is output as is (a raw string of bytes)
`%Q`      | the matching line as a quoted string, `\"` and `\\` replace `"` and `\`
`%q`      | the match as a quoted string, `\"` and `\\` replace `"` and `\`
`%C`      | the matching line formatted as a quoted C/C++ string
`%c`      | the match formatted as a quoted C/C++ string
`%J`      | the matching line formatted as a quoted JSON string
`%j`      | the match formatted as a quoted JSON string
`%V`      | the matching line formatted as a quoted CSV string
`%v`      | the match formatted as a quoted CSV string
`%X`      | the matching line formatted as XML character data
`%x`      | the match formatted as XML character data
`%w`      | the width of the match, counting (wide) characters
`%d`      | the size of the match, counting bytes
`%e`      | the ending byte offset of the match
`%u`      | select unique lines only unless option -u is used
`%,`      | if not the first match: a comma, same as `%[,]>`
`%:`      | if not the first match: a colon, same as `%[:]>`
`%;`      | if not the first match: a semicolon, same as `%[;]>`
`%│`      | if not the first match: a verical bar, same as `%[│]>`
`%%`      | the percentage sign
`%1`      | the first regex group capture of the match, and so on up to group `%9`
`%[NUM]#` | the regex group capture `NUM`, requires option `-P` Perl matching

Note:

- The `[ARG]` part of a field is optional and may be omitted.  When present,
  the argument must be placed in `[]` brackets, for example `%[,]F` to output a
  comma, the pathname, and a separator, if option `-H` is used.
- Fields `%[SEP]$` and `%u` are switches and do not write anything to the
  output.
- The separator used by `%P`, `%H`, `%N`, `%K`, `%B`, and `%S` may be changed
  by preceeding the field with a `%[SEP]$`.  When `[SEP]` is not provided,
  reverses the separator to the default separator or the separator specified by
  `--separator`.
- Formatted output is written for each matching pattern, which means that a
  line may be output multiple times when patterns match more than once on the
  same  line.   When field `%u` is found anywhere in the specified format
  string, matching lines are output only once unless option `-u`, `--ungroup`
  is used or when a newline is matched.

To output matching lines faster by omitting the header output and binary match
checks, using `--format` with field `%O` (output matching line as is) and field
`%~` (output newline):

    ugrep --format='%O%~' 'href=' index.html

Same, but also displaying the line and column numbers:

    ugrep --format='%n%k: %O%~' 'href=' index.html

Same, but display a line at most once when matching multiple patterns, unless
option `-u` is used:

    ugrep --format='%u%n%k: %O%~' 'href=' index.html

To string together a list of unique line numbers of matches, separated by
commas with field `%,`:

    ugrep --format='%u%,%n' 'href=' index.html

To output the matching part of a line only with field `%o` (or option `-o` with
field `%O`):

    ugrep --format='%o%~' "href=[\"'][^\"'\n][\"']" index.html

To string together the pattern matches as CSV-formatted strings with field `%v`
separated by commas with field `%,`:

    ugrep --format='%,%v' "href=[\"'][^\"'\n][\"']" index.html

To output matches in CSV (comma-separated values), the same as option `--csv`
(works with options `-H`, `-n`, `-k`, `-b` to add CSV values):

    ugrep --format='"%[,]$%H%N%K%B%V%~%u"' 'href=' index.html

To output matches in JSON, using formatting options that produce the same
output as `--json` (works with options `-H`, `-n`, `-k`, `-b` to add JSON
properties):

    ugrep --format-begin='[' \
           --format-open='%,%~  {%~    %[,%~    ]$%["file": ]H"matches": [' \
                --format='%,%~      { %[, ]$%["line": ]N%["column": ]K%["offset": ]B"match": %J }%u' \
          --format-close='%~    ]%~  }' \
            --format-end='%~]%~' \
          'href=' index.html

To output matches in AckMate format:

    ugrep --format=":%f%~%n;%k %w:%O%~" 'href=' index.html

<a name="replace"/>

### Replacing matches with --format backreferences to group captures

    --format=FORMAT
            Output FORMAT-formatted matches.  See `man ugrep' section FORMAT
            for the `%' fields.  Options -A, -B, -C, -y, and -v are disabled.
    -P, --perl-regexp
            Interpret PATTERN as a Perl regular expression.

To extract table cells from an HTML file using Perl matching (`-P`) to support
group captures with lazy quantifier `(.*?)`, and translate the matches to a
comma-separated list with format `%,%1` (conditional comma and group capture):

    ugrep -P '<td>(.*?)</td>' --format='%,%1' index.html

Same, but displaying the replaced matches line-by-line:

    ugrep -P '<td>(.*?)</td>' --format='%1\n' index.html

To collect all `href` URLs from all HTML and PHP files down the working
directory, then sort them:

    ugrep -R -thtml,php -P '<[^<>\n]+href\h*=\h*.([^\x27"\n]+).' --format='%1%~' | sort -u

Same, but much easier by using the predefined `html/href` pattern:

    ugrep -R -thtml,php -P -f html/href --format='%1%~' | sort -u

Likewise, but in this case select `<script>` `src` URLs when referencing `http`
and `https` sites:

    ugrep -R -thtml,php -P '<script.*src\h*=\h*.(https?:[^\x27"\n]+).' --format='%1%~' | sort -u

<a name="max"/>

### Limiting the number of matches with -K, -m, --max-depth, and --max-files

    -K NUM1[,NUM2], --range=NUM1[,NUM2]
            Start searching at line NUM1 and end at line NUM2 when specified.
    -m NUM, --max-count=NUM
            Stop reading the input after NUM matches for each file processed.
    --max-depth=NUM
            Restrict recursive search to NUM (NUM > 0) directories deep, where
            --max-depth=1 searches the specified path without visiting
            sub-directories.  By comparison, -dskip skips all directories even
            when they are on the command line.
    --max-files=NUM
            If -R or -r is specified, restrict the number of files matched to
            NUM.  Specify -J1 to produce replicable results by ensuring that
            files are searched in the same order as specified.

To show only the first 10 matches of `FIXME` in C++ files in the working
directory and all sub-directories below:

    ugrep -R -m10 -tc++ FIXME

Same, but recursively search up to two directory levels deep, meaning that
`./` and `./sub/` are visited but not deeper:

    ugrep -R -m10 --max-depth=2 -tc++ FIXME

To show only the first file that has one or more matches of `FIXME`, we disable
parallel search with `-J1` and use `--max-files=1`:

    ugrep -J1 -R --max-files=1 -tc++ FIXME

To search file `install.sh` for the occurrences of the word `make` after the
first line, we use `-K` (`--range`) with line number 2 to start searching,
where `-n` shows the line numbers in the output:

    ugrep -n -K2 -w make install.sh

Same, but restricting the search to lines 2 to 40 (inclusive):

    ugrep -n -K2,40 -w make install.sh

Same, but showing all lines 2 to 40 with `-y` and showing the matches in color:

    ugrep --color -y -n -K2,40 -w make install.sh

Same, but showing only the first four matching lines after line 2, with one
line of context:

    ugrep -n -C1 -K2 -m4 -w make install.sh

<a name="empty"/>

### Matching empty patterns with -Y

    -Y, --empty
            Permits empty matches.  By default, empty matches are disabled,
            unless a pattern starts with `^' and ends with `$'.  Note that -Y
            when specified with an empty-matching pattern such as x? and x*,
            match all input, not only lines with a `x'.

Option `-Y` permits empty pattern matches.  This option is introduced by
**ugrep** to prevent accidental matching with empty patterns: empty-matching
patterns such as `x?` and `x*` match all input, not only lines with `x`.  By
default, without `-Y`, patterns match lines with at least one `x` as intended.

This option is automatically enabled when a pattern starts with `^` and ends
with `$` is specified.  For example, `^\h*$` matches blank lines, including
empty lines.

To recursively list files in the working directory with blank lines, i.e. lines
with white space only, including empty lines (note that option `-Y` is
implicitly enabled since the pattern starts with `^` and ends with `$`):

    ugrep -Rl '^\h*$'

<a name="tips"/>

### Tips for advanced users

When searching non-binary files only, the binary content check is disabled with
option `-a` to speed up displaying matches.  For example, searching for line
with `int` in C++ source code:

    ugrep -r -a -Ocpp -w 'int'

If a file has potentially many pattern matches, but each match is only one a
single line, then option `-u` can be used to speed up displaying matches:

    ugrep -r -a -u -Opython -w 'def'

Even greater speeds can be achieved with `--format` when searching files with
many matches.  For example, `--format='%O%~'` displays matching lines for each
match on that line, while `--format='%o%~'` displays the matching part only.
Note that the `--format` option does not check for binary matches, so the
output is "as is".  To match text and binary, you can use `--format='%C%~'`
to display matches formatted as quoted C++ strings with escapes.
To display a line at most once (unless option `-u` is used), add the `%u`
(unique) field to the format string, e.g. `--format='%u%O%~'`.

For example, to match all words recursively in the working directory with line
and column numbers, where `%n` is the line number, `%k` is the column number,
`%o` is the match (only matching), and `%~` is a newline:

    ugrep -r --format='%n,%k:%o%~' '\w+'

<a name="more"/>

### More examples

To search for pattern `-o` in `script.sh` using `-e` to explicitly specify a
pattern to prevent pattern `-o` from being interpreted as an option:

    ugrep -n -e '-o' script.sh

Alternatively, using `--` to end the list of command arguments:

    ugrep -n -- '-o' script.sh

To list all text files (.txt and .md) that do not properly end with a `\n`
(`-o` is required to match `\n` or `\z`):

    ugrep -RL -o -Otext '\n\z'

To list all markdown sections in text files (.text, .txt, .TXT, and .md):

    ugrep -R -o -ttext -e '^.*(?=\r?\n(===|---))' -e '^#{1,6}\h+.*'

To display multi-line backtick and indented code blocks in markdown files with
their line numbers:

    ugrep -R -o -n -ttext -e '^```([^`]|`[^`]|``[^`])+\n```' -e '^(\t|[ ]{4}).*'

To find mismatched code (a backtick without matching backtick on the same line)
in markdown:

    ugrep -R -o -n -ttext -e '`[^`]+`' -N '`[^`\n]*`'

<a name="man"/>

### Man page

    UGREP(1)                         User Commands                        UGREP(1)



    NAME
           ugrep -- universal file pattern searcher

    SYNOPSIS
           ugrep [OPTIONS] [-A NUM] [-B NUM] [-C[NUM]] [PATTERN] [-e PATTERN]
                 [-N PATTERN] [-f FILE] [-t TYPES] [-Q ENCODING] [-J [NUM]]
                 [--color[=WHEN]|--colour[=WHEN]] [--pager[=COMMAND]] [FILE ...]

    DESCRIPTION
           The  ugrep utility searches any given input files, selecting lines that
           match one or more patterns.  By default, a  pattern  matches  an  input
           line  if  the  regular expression (RE) in the pattern matches the input
           line without its trailing newline.  A pattern  matches  multiple  input
           lines  if  the  RE  in  the pattern matches one or more newlines in the
           input.  An empty RE matches every line.  Each input line  that  matches
           at least one of the patterns is written to the standard output.

           The ugrep utility accepts input of various encoding formats and normal-
           izes the input to UTF-8.  When a UTF BOM is present in the  input,  the
           input  is automatically normalized.  Otherwise, ugrep assumes the input
           is ASCII, UTF-8, or raw binary.  To specify the input encoding  format,
           use option -Q, --encoding.

           The following options are available:

           -A NUM, --after-context=NUM
                  Print  NUM  lines  of  trailing  context  after  matching lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -B, -C, and -y options.

           -a, --text
                  Process a binary file as if it were text.  This is equivalent to
                  the --binary-files=text option.  This option might output binary
                  garbage to the terminal, which can have problematic consequences
                  if the terminal driver interprets some of it as commands.

           -B NUM, --before-context=NUM
                  Print NUM  lines  of  leading  context  before  matching  lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also the -A, -C, and -y options.

           -b, --byte-offset
                  The offset in bytes of a matched line is displayed in  front  of
                  the respective matched line.  When used with option -u, displays
                  the offset in bytes of each pattern matched.  Byte  offsets  are
                  exact  for  ASCII,  UTF-8, and raw binary input.  Otherwise, the
                  byte offset in the UTF-8 converted input is displayed.

           --binary-files=TYPE
                  Controls searching  and  reporting  pattern  matches  in  binary
                  files.   Options  are  `binary', `without-match`, `text`, `hex`,
                  and `with-hex'.  The default is `binary' to search binary  files
                  and  to  report  a  match  without displaying the match.  `with-
                  out-match' ignores binary matches.   `text'  treats  all  binary
                  files  as  text, which might output binary garbage to the termi-
                  nal, which can have problematic  consequences  if  the  terminal
                  driver  interprets  some  of  it as commands.  `hex' reports all
                  matches in hexadecimal.  `with-hex' only reports binary  matches
                  in  hexadecimal, leaving text matches alone.  A match is consid-
                  ered binary if a match contains  a  zero  byte  or  invalid  UTF
                  encoding.  See also the -a, -I, -U, -W, and -X options.

           --break
                  Group  matches  per  file.  Adds a header and line break between
                  results from different files.

           -C[NUM], --context[=NUM]
                  Print NUM lines of leading and trailing context surrounding each
                  match.  The default is 2 and is equivalent to -A 2 -B 2.  Places
                  a --group-separator between contiguous groups  of  matches.   No
                  whitespace may be given between -C and its argument NUM.

           -c, --count
                  Only  a  count  of selected lines is written to standard output.
                  If -o or -u is specified, counts the number of patterns matched.
                  If -v is specified, counts the number of non-matching lines.

           --color[=WHEN], --colour[=WHEN]
                  Mark  up  the  matching  text  with the expression stored in the
                  GREP_COLOR or GREP_COLORS environment  variable.   The  possible
                  values of WHEN can be `never', `always', or `auto', where `auto'
                  marks up matches only when output on a terminal.  The default is
                  `auto'.

           --colors=COLORS, --colours=COLORS
                  Use  COLORS  to  mark up matching text.  COLORS is a colon-sepa-
                  rated list of ANSI SGR parameters.  COLORS selectively overrides
                  the colors specified by the GREP_COLORS environment variable.

           --cpp  Output  file  matches  in  C++.   See  also  the --format and -u
                  options.

           --csv  Output file matches in CSV.  When option -H, -n, -k,  or  -b  is
                  used additional values are output.  See also the --format and -u
                  options.

           -D ACTION, --devices=ACTION
                  If an input file is a device, FIFO  or  socket,  use  ACTION  to
                  process  it.   By  default,  ACTION  is `skip', which means that
                  devices are silently skipped.  If ACTION is `read', devices read
                  just as if they were ordinary files.

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
                  Note that longer patterns take precedence over shorter patterns.
                  This  option is most useful when multiple -e options are used to
                  specify multiple patterns, when a pattern  begins  with  a  dash
                  (`-'),  to  specify  a pattern after option -f or after the FILE
                  arguments.

           --exclude=GLOB
                  Skip files whose name matches GLOB using wildcard matching, same
                  as -g !GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and
                  \ to quote a wildcard or backslash  character  literally.   When
                  GLOB  contains  a  `/',  full  pathnames are matched.  Otherwise
                  basenames are matched.  Note that --exclude patterns take prior-
                  ity over --include patterns.  This option may be repeated.

           --exclude-dir=GLOB
                  Exclude  directories  whose  name  matches  GLOB  from recursive
                  searches.  GLOB can use **, *, ?, and [...] as wildcards, and  \
                  to quote a wildcard or backslash character literally.  When GLOB
                  contains a `/', full pathnames are matched.  Otherwise basenames
                  are  matched.   Note  that  --exclude-dir patterns take priority
                  over --include-dir patterns.  This option may be repeated.

           --exclude-from=FILE
                  Read the globs from FILE and skip files  and  directories  whose
                  name matches one or more globs (as if specified by --exclude and
                  --exclude-dir).  Lines starting with a `#' and  empty  lines  in
                  FILE  are  ignored.  When FILE is a `-', standard input is read.
                  This option may be repeated.

           --exclude-fs=MOUNTS
                  Exclude  file  systems  specified  by  MOUNTS   from   recursive
                  searches,  MOUNTS  is  a comma-separated list of mount points or
                  pathnames  of  directories   on   file   systems.    Note   that
                  --exclude-fs  mounts  take  priority  over  --include-fs mounts.
                  This option may be repeated.

           -F, --fixed-strings
                  Interpret pattern as a set of fixed strings, separated  by  new-
                  lines,  any  of which is to be matched.  This makes ugrep behave
                  as fgrep.  If PATTERN or -e PATTERN is also specified, then this
                  option does not apply to -f FILE patterns.

           -f FILE, --file=FILE
                  Read  one  or  more newline-separated patterns from FILE.  Empty
                  pattern lines in FILE are  not  processed.   If  FILE  does  not
                  exist, the GREP_PATH environment variable is used as the path to
                  FILE.     If    that    fails,     looks     for     FILE     in
                  /usr/local/share/ugrep/patterns.   When  FILE is a `-', standard
                  input is read.  Empty files contain no patterns, thus nothing is
                  matched.  This option may be repeated.

           --filter=COMMANDS
                  Filter files through the specified COMMANDS first before search-
                  ing.   COMMANDS  is  a  comma-separated  list  of  `exts:command
                  [option  ...]',  where `exts' is a comma-separated list of file-
                  name extensions and `command' is a filter utility.   The  filter
                  utility  should  read  from standard input and write to standard
                  output.  Files matching one of `exts` are filtered only.  One or
                  more  `option'  separated by spacing may be specified, which are
                  passed verbatim to the command.  A `%' as `option' expands  into
                  the  pathname to search.  For example, --filter='pdf:pdftotext %
                  -' searches PDF files.  The `%' expands into a `-' when  search-
                  ing  standard input.  Option --label=.ext may be used to specify
                  extension `ext' when searching standard input.

           --format=FORMAT
                  Output FORMAT-formatted matches.  See `man ugrep' section FORMAT
                  for  the  `%'  fields.   Options -A, -B, -C, -y, and -v are dis-
                  abled.

           --free-space
                  Spacing (blanks and tabs) in regular expressions are ignored.

           -G, --basic-regexp
                  Interpret pattern as a basic regular expression, i.e. make ugrep
                  behave as traditional grep.

           -g GLOB, --glob=GLOB
                  Search   only   files   whose   name   matches   GLOB,  same  as
                  --include=GLOB.  When GLOB is preceded by a `!' or a  `^',  skip
                  files whose name matches GLOB, same as --exclude=GLOB.

           --group-separator[=SEP]
                  Use SEP as a group separator for context options -A, -B, and -C.
                  The default is a double hyphen (`--').

           -H, --with-filename
                  Always print the  filename  with  output  lines.   This  is  the
                  default when there is more than one file to search.

           -h, --no-filename
                  Never  print  filenames  with output lines.  This is the default
                  when there is only one file (or only standard input) to  search.

           --help Print a help message.

           -I     Ignore  matches  in  binary files.  This option is equivalent to
                  the --binary-files=without-match option.

           -i, --ignore-case
                  Perform case insensitive matching.  By default,  ugrep  is  case
                  sensitive.  This option applies to ASCII letters only.

           --ignore-files[=FILE]
                  Ignore  files  and  directories  matching the globs in each FILE
                  when encountered in recursive searches.   The  default  FILE  is
                  `.gitignore'.   Matching  files  and  directories located in the
                  directory tree rooted at a FILE's location are ignored by tempo-
                  rarily  overriding the --exclude and --exclude-dir globs.  Files
                  and directories specified as FILE  arguments  are  not  ignored.
                  This option may be repeated.

           --include=GLOB
                  Search  only files whose name matches GLOB using wildcard match-
                  ing, same as -g GLOB.  GLOB can use **, *, ?, and [...] as wild-
                  cards,  and  \ to quote a wildcard or backslash character liter-
                  ally.  When GLOB contains a `/',  full  pathnames  are  matched.
                  Otherwise  basenames  are matched.  Note that --exclude patterns
                  take priority over  --include  patterns.   This  option  may  be
                  repeated.

           --include-dir=GLOB
                  Only  directories whose name matches GLOB are included in recur-
                  sive searches.  GLOB can use **, *, ?, and [...]  as  wildcards,
                  and  \  to  quote  a  wildcard or backslash character literally.
                  When GLOB contains a `/', full pathnames are matched.  Otherwise
                  basenames  are  matched.   Note that --exclude-dir patterns take
                  priority  over  --include-dir  patterns.   This  option  may  be
                  repeated.

           --include-from=FILE
                  Read  the  globs from FILE and search only files and directories
                  whose name matches  one  or  more  globs  (as  if  specified  by
                  --include  and  --include-dir).   Lines  starting with a `#' and
                  empty lines in FILE are ignored.  When FILE is a  `-',  standard
                  input is read.  This option may be repeated.

           --include-fs=MOUNTS
                  Only  file systems specified by MOUNTS are included in recursive
                  searches.  MOUNTS is a comma-separated list of mount  points  or
                  pathnames   of  directories  on  file  systems.   --include-fs=.
                  restricts recursive searches to the file system of  the  working
                  directory  only.   Note  that  --exclude-fs mounts take priority
                  over --include-fs mounts.  This option may be repeated.

           -J NUM, --jobs=NUM
                  Specifies the number of threads spawned  to  search  files.   By
                  default, an optimum number of threads is spawned to search files
                  simultaneously.  -J1 disables threading: files are  searched  in
                  the same order as specified.

           -j, --smart-case
                  Perform case insensitive matching unless PATTERN contains a cap-
                  ital letter.  Case insensitive matching applies to ASCII letters
                  only.

           --json Output  file  matches in JSON.  When option -H, -n, -k, or -b is
                  used additional values are output.  See also the --format and -u
                  options.

           -K NUM1[,NUM2], --range=NUM1[,NUM2]
                  Start  searching  at  line NUM1 and end at line NUM2 when speci-
                  fied.

           -k, --column-number
                  The column number of a matched pattern is displayed in front  of
                  the  respective  matched  line,  starting at column 1.  Tabs are
                  expanded when columns are counted, see also option --tabs.

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

           -M MAGIC, --file-magic=MAGIC
                  Only  files  matching  the signature pattern MAGIC are searched.
                  The signature "magic bytes" at the start of a file are  compared
                  to  the  MAGIC  regex  pattern.  When matching, the file will be
                  searched.  When MAGIC is preceded by a `!' or a `^', skip  files
                  with matching MAGIC signatures.  This option may be repeated and
                  may be combined with options -O and -t  to  expand  the  search.
                  Every  file  on  the search path is read, making searches poten-
                  tially more expensive.

           -m NUM, --max-count=NUM
                  Stop reading the input after NUM  matches  for  each  file  pro-
                  cessed.

           --match
                  Match all input.  Same as specifying an empty pattern to search.

           --max-depth=NUM
                  Restrict recursive search to NUM (NUM  >  0)  directories  deep,
                  where --max-depth=1 searches the specified path without visiting
                  sub-directories.  By comparison, -dskip  skips  all  directories
                  even when they are on the command line.

           --max-files=NUM
                  If  -R  or -r is specified, restrict the number of files matched
                  to NUM.  Specify -J1 to produce replicable results  by  ensuring
                  that files are searched in the same order as specified.

           -N PATTERN, --neg-regexp=PATTERN
                  Specify  a negative PATTERN used during the search of the input:
                  an input line is selected only if it matches any of  the  speci-
                  fied  patterns  when PATTERN does not match.  Same as -e (?^PAT-
                  TERN).  Negative PATTERN matches are removed  before  any  other
                  specified  patterns are matched.  Note that longer patterns take
                  precedence over shorter patterns.  This option may be  repeated.

           -n, --line-number
                  Each  output line is preceded by its relative line number in the
                  file, starting at line 1.  The line number counter is reset  for
                  each file processed.

           --no-group-separator
                  Removes  the  group  separator  line from the output for context
                  options -A, -B, and -C.

           --[no-]hidden
                  Do (not) search hidden files and directories.

           --[no-]mmap
                  Do (not) use memory maps to search files.   By  default,  memory
                  maps are used under certain conditions to improve performance.

           -O EXTENSIONS, --file-extensions=EXTENSIONS
                  Search  only files whose filename extensions match the specified
                  comma-separated list of EXTENSIONS,  same  as  --include='*.ext'
                  for  each  `ext' in EXTENSIONS.  When `ext' is preceded by a `!'
                  or a `^', skip files whose filename  extensions  matches  `ext',
                  same  as --exclude='*.ext'.  This option may be repeated and may
                  be combined with options -M  and  -t  to  expand  the  recursive
                  search.

           -o, --only-matching
                  Print  only  the  matching  part  of lines.  When multiple lines
                  match, the line numbers with option -n are displayed  using  `|'
                  as  the  field separator for each additional line matched by the
                  pattern.  This option cannot be combined with  options  -A,  -B,
                  -C, -v, and -y.

           --only-line-number
                  The line number of the matching line in the file is output with-
                  out displaying the match.  The line number counter is reset  for
                  each file processed.

           -P, --perl-regexp
                  Interpret PATTERN as a Perl regular expression.

           -p, --no-dereference
                  If  -R  or -r is specified, no symbolic links are followed, even
                  when they are specified on the command line.

           --pager[=COMMAND]
                  When output is sent  to  the  terminal,  uses  COMMAND  to  page
                  through  the  output.   The  default COMMAND is `less -R'.  This
                  option makes --color=auto  behave  as  --color=always.   Enables
                  --break and --line-buffered.

           -Q ENCODING, --encoding=ENCODING
                  The  input  file  encoding.  The possible values of ENCODING can
                  be:   `binary',   `ASCII',   `UTF-8',   `UTF-16',    `UTF-16BE',
                  `UTF-16LE',   `UTF-32',  `UTF-32BE',  `UTF-32LE',  `ISO-8859-1',
                  `ISO-8869-2',    `ISO-8869-3',    `ISO-8869-4',    `ISO-8869-5',
                  `ISO-8869-6',    `ISO-8869-7',    `ISO-8869-8',    `ISO-8869-9',
                  `ISO-8869-10',  `ISO-8869-11',   `ISO-8869-13',   `ISO-8869-14',
                  `ISO-8869-15',   `ISO-8869-16',   `MAC',  `MACROMAN',  `EBCDIC',
                  `CP437',  `CP850',  `CP858',   `CP1250',   `CP1251',   `CP1252',
                  `CP1253',  `CP1254',  `CP1255',  `CP1256',  `CP1257',  `CP1258',
                  `KOI8-R', `KOI8-U', `KOI8-RU'.

           -q, --quiet, --silent
                  Quiet mode: suppress normal  output.   ugrep  will  only  search
                  until  a  match has been found, making searches potentially less
                  expensive.

           -R, --dereference-recursive
                  Recursively read all files under  each  directory.   Follow  all
                  symbolic  links,  unlike  -r.   When -J1 is specified, files are
                  searched in the same order as specified.

           -r, --recursive
                  Recursively read all files under each directory, following  sym-
                  bolic  links  only if they are on the command line.  When -J1 is
                  specified, files are searched in the same order as specified.

           -S, --dereference
                  If -r is specified, all symbolic links are  followed,  like  -R.
                  The default is not to follow symbolic links.

           -s, --no-messages
                  Silent  mode: nonexistent and unreadable files are ignored, i.e.
                  their error messages are suppressed.

           --separator[=SEP]
                  Use SEP as field separator between file name, line number,  col-
                  umn number, byte offset, and the matched line.  The default is a
                  colon (`:').

           --stats
                  Display statistics  on  the  number  of  files  and  directories
                  searched.   Display  the  inclusion  and  exclusion  constraints
                  applied.

           -T, --initial-tab
                  Add a tab space to separate the file name, line  number,  column
                  number, and byte offset with the matched line.

           -t TYPES, --file-type=TYPES
                  Search  only files associated with TYPES, a comma-separated list
                  of file types.  Each file type corresponds to a set of  filename
                  extensions passed to option -O.  For capitalized file types, the
                  search is expanded to include files with matching file signature
                  magic  bytes,  as  if  passed to option -M.  When a type is pre-
                  ceeded by a `!' or a `^', excludes files of the specified  type.
                  This  option  may  be  repeated.  The possible file types can be
                  (where -tlist displays a detailed list): `actionscript',  `ada',
                  `asm',  `asp',  `aspx',  `autoconf',  `automake',  `awk', `Awk',
                  `basic', `batch',  `bison',  `c',  `c++',  `clojure',  `csharp',
                  `css',  `csv',  `dart',  `Dart',  `delphi',  `elisp',  `elixir',
                  `erlang',  `fortran',  `gif',  `Gif',  `go',  `groovy',   `gsp',
                  `haskell', `html', `jade', `java', `jpeg', `Jpeg', `js', `json',
                  `jsp', `julia', `kotlin', `less', `lex',  `lisp',  `lua',  `m4',
                  `make',  `markdown', `matlab', `node', `Node', `objc', `objc++',
                  `ocaml',  `parrot',  `pascal',  `pdf',  `Pdf',  `perl',  `Perl',
                  `php',  `Php',  `png', `Png', `prolog', `python', `Python', `r',
                  `rpm', `Rpm',  `rst',  `rtf',  `Rtf',  `ruby',  `Ruby',  `rust',
                  `scala',  `scheme', `shell', `Shell', `smalltalk', `sql', `svg',
                  `swift', `tcl', `tex',  `text',  `tiff',  `Tiff',  `tt',  `type-
                  script', `verilog', `vhdl', `vim', `xml', `Xml', `yacc', `yaml'.

           --tabs=NUM
                  Set the tab size to NUM to expand tabs for option -k.  The value
                  of NUM may be 1, 2, 4, or 8.  The default tab size is 8.

           -U, --binary
                  Disables Unicode matching for binary file matching, forcing PAT-
                  TERN to match bytes, not Unicode characters.   For  example,  -U
                  '\xa3'  matches  byte A3 (hex) instead of the Unicode code point
                  U+00A3 represented by the two-byte UTF-8 sequence C2 A3.

           -u, --ungroup
                  Do not group multiple pattern matches on the same matched  line.
                  Output the matched line again for each additional pattern match,
                  using `+' as the field separator.

           -V, --version
                  Display version information and exit.

           -v, --invert-match
                  Selected lines are those not matching any of the specified  pat-
                  terns.

           -W, --with-hex
                  Output  binary  matches  in  hexadecimal,  leaving  text matches
                  alone.  This option is equivalent to the --binary-files=with-hex
                  option.

           -w, --word-regexp
                  The  PATTERN  is  searched for as a word (as if surrounded by \<
                  and \>).  If a PATTERN is specified (or -e PATTERN  or  -N  PAT-
                  TERN), then this option does not apply to -f FILE patterns.

           -X, --hex
                  Output matches in hexadecimal.  This option is equivalent to the
                  --binary-files=hex option.

           -x, --line-regexp
                  Only input lines selected against the entire PATTERN is  consid-
                  ered  to  be matching lines (as if surrounded by ^ and $).  If a
                  PATTERN is specified (or -e PATTERN or -N  PATTERN),  then  this
                  option does not apply to -f FILE patterns.

           --xml  Output  file  matches  in XML.  When option -H, -n, -k, or -b is
                  used additional values are output.  See also the --format and -u
                  options.

           -Y, --empty
                  Permits  empty matches.  By default, empty matches are disabled,
                  unless a pattern starts with `^' and ends with `$'.   Note  that
                  -Y  when specified with an empty-matching pattern such as x? and
                  x*, match all input, not only lines with a `x'.

           -y, --any-line
                  Any matching or non-matching line is output.  Non-matching lines
                  are  output  with  the  `-' separator as context of the matching
                  lines.  See also the -A, -B, and -C options.

           -Z, --null
                  Prints a zero-byte after the file name.

           -z, --decompress
                  Decompress files to search, when compressed.   Archives  (.cpio,
                  .jar,  .pax,  .tar,  .zip)  and  compressed archives (e.g. .taz,
                  .tgz, .tpz,  .tbz,  .tbz2,  .tb2,  .tz2,  .tlz,  and  .txz)  are
                  searched  and matching pathnames of files in archives are output
                  in braces.  If -g, -O, -M, or -t is  specified,  searches  files
                  within  archives  whose  name  matches  globs, matches file name
                  extensions, matches file signature magic bytes, or matches  file
                  types, respectively.  Supported compression formats: gzip (.gz),
                  compress (.Z), zip, bzip2 (requires suffix  .bz,  .bz2,  .bzip2,
                  .tbz,  .tbz2,  .tb2,  .tz2), lzma and xz (requires suffix .lzma,
                  .tlz, .xz, .txz).

           If no FILE arguments are specified, or if a `-' is specified, the stan-
           dard input is used, unless recursive searches are specified which exam-
           ine the working directory.

           If no FILE arguments are specified and one of the options -g,  -O,  -M,
           -t, --include, --include-dir, --exclude, or --exclude-dir is specified,
           recursive searches are performed as if -r was specified.

           A `--' signals the end of options; the rest of the parameters are  FILE
           arguments, allowing filenames to begin with a `-' character.

           The  regular expression pattern syntax is an extended form of the POSIX
           ERE syntax.  For an overview of the syntax see README.md or visit:

                  https://github.com/Genivia/ugrep

           Note that `.' matches any non-newline character.  Pattern `\n'  matches
           a  newline character.  Multiple lines may be matched with patterns that
           match newlines, unless one or more of the context options -A,  -B,  -C,
           or -y is used, or option -v is used.

    EXIT STATUS
           The ugrep utility exits with one of the following values:

           0      One or more lines were selected.

           1      No lines were selected.

           >1     An error occurred.

           If  -q  or --quiet or --silent is used and a line is selected, the exit
           status is 0 even if an error occurred.

    GLOBBING
           Globbing is used by options -g,  --include,  --include-dir,  --include-
           from,  --exclude,  --exclude-dir, --exclude-from to match pathnames and
           basenames in recursive searches.  Globbing  supports  gitignore  syntax
           and the corresponding matching rules.  When a glob contains a path sep-
           arator `/', the pathname is matched.  Otherwise the basename of a  file
           or directory is matched.  For example, *.h matches foo.h and bar/foo.h.
           bar/*.h matches bar/foo.h but not foo.h and not bar/bar/foo.h.   Use  a
           leading `/' to force /*.h to match foo.h but not bar/foo.h.

           When  a  glob starts with a `!' as specified with -g!GLOB, or specified
           in a  FILE  with  --include-from=FILE  or  --exclude-from=FILE,  it  is
           negated.

           Glob Syntax and Conventions

           *      Matches anything except a /.

           ?      Matches any one character except a /.

           [a-z]  Matches one character in the selected range of characters.

           [^a-z] Matches one character not in the selected range of characters.

           [!a-z] Matches one character not in the selected range of characters.

           /      When  used at the begin of a glob, matches if pathname has no /.

           **/    Matches zero or more directories.

           /**    When at the end of a glob, matches everything after the /.

           \?     Matches a ? (or any character specified after the backslash).

           Glob Matching Examples

           *      Matches a, b, x/a, x/y/b

           a      Matches a, x/a, x/y/a,       but not b, x/b, a/a/b

           /*     Matches a, b,                but not x/a, x/b, x/y/a

           /a     Matches a,                   but not x/a, x/y/a

           a?b    Matches axb, ayb,            but not a, b, ab, a/b

           a[xy]b Matches axb, ayb             but not a, b, azb

           a[a-z]b
                  Matches aab, abb, acb, azb,  but not a, b, a3b, aAb, aZb

           a[^xy]b
                  Matches aab, abb, acb, azb,  but not a, b, axb, ayb

           a[^a-z]b
                  Matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb

           a/*/b  Matches a/x/b, a/y/b,        but not a/b, a/x/y/b

           **/a   Matches a, x/a, x/y/a,       but not b, x/b.

           a/**/b Matches a/b, a/x/b, a/x/y/b, but not x/a/b, a/b/x

           a/**   Matches a/x, a/y, a/x/y,     but not a, b/x

           a\?b   Matches a?b,                 but not a, b, ab, axb, a/b

           Lines in the --exclude-from and --include-from files are  ignored  when
           empty  or  start  with  a `#'.  The prefix `!' to a glob in such a file
           negates the pattern match, i.e.  matching  files  are  excluded  except
           files  matching the globs prefixed with `!' in the --exclude-from file.

    ENVIRONMENT
           GREP_PATH
                  May be used to specify a file path to pattern files.   The  file
                  path  is used by option -f to open a pattern file, when the file
                  cannot be opened.

           GREP_COLOR
                  May be used to specify ANSI SGR parameters to highlight  matches
                  when  option --color is used, e.g. 1;35;40 shows pattern matches
                  in bold magenta text on a black background.

           GREP_COLORS
                  May be used to specify ANSI SGR parameters to highlight  matches
                  and  other attributes when option --color is used.  Its value is
                  a colon-separated list of ANSI SGR parameters that  defaults  to
                  cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36.   The  mt=,
                  ms=, and mc= capabilities  of  GREP_COLORS  have  priority  over
                  GREP_COLOR.  Option --colors has priority over GREP_COLORS.

    GREP_COLORS
           Colors  are  specified as string of colon-separated ANSI SGR parameters
           of the form `what=substring', where `substring'  is  a  semicolon-sepa-
           rated list of SGR codes to color-highlight `what':

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

    FORMAT
           Option  --format=FORMAT  specifies  an  output format for file matches.
           Fields may be used in FORMAT, which expand into the following values:

           %[ARG]F
                  if option -H is used: ARG, the file pathname, and separator.

           %[ARG]H
                  if option -H is used: ARG, the quoted pathname, and separator.

           %[ARG]N
                  if option -n is used: ARG, the line number and separator.

           %[ARG]K
                  if option -k is used: ARG, the column number and separator.

           %[ARG]B
                  if option -b is used: ARG, the byte offset and separator.

           %[ARG]T
                  if option -T is used: ARG and a tab character.

           %[ARG]S
                  if not the first match: ARG and separator, see also %$.

           %[ARG]<
                  if the first match: ARG.

           %[ARG]>
                  if not the first match: ARG.

           %[SEP]$
                  set field separator to SEP for the rest of the format fields.

           %f     the file pathname.

           %h     the quoted file pathname.

           %z     the file pathname in a (compressed) archive.

           %n     the line number of the match.

           %k     the column number of the match.

           %b     the byte offset of the match.

           %t     a tab character.

           %s     the separator, see also %S and %$.

           %~     a newline character.

           %m     the number of matches or matched files.

           %O     the matching line is output as a raw string of bytes.

           %o     the match is output as a raw string of bytes.

           %Q     the matching line as a quoted string, \" and \\ replace " and \.

           %q     the match as a quoted string, \" and \\ replace " and \.

           %C     the matching line formatted as a quoted C/C++ string.

           %c     the match formatted as a quoted C/C++ string.

           %J     the matching line formatted as a quoted JSON string.

           %j     the match formatted as a quoted JSON string.

           %V     the matching line formatted as a quoted CSV string.

           %v     the match formatted as a quoted CSV string.

           %X     the matching line formatted as XML character data.

           %x     the match formatted as XML character data.

           %w     the width of the match, counting wide characters.

           %d     the size of the match, counting bytes.

           %e     the ending byte offset of the match.

           %u     select unique lines only unless option -u is used.

           %,     if not the first match: a comma, same as %[,]>.

           %:     if not the first match: a colon, same as %[:]>.

           %;     if not the first match: a semicolon, same as %[;]>.

           %|     if not the first match: a verical bar, same as %[|]>.

           %%     the percentage sign.

           %1     the  first  regex  group  capture  of the match, and so on up to
                  group %9, same as %[1]#; requires option -P Perl matching.

           %[NUM]#
                  the regex group capture NUM; requires option -P Perl matching.

           The [ARG] part of a  field  is  optional  and  may  be  omitted.   When
           present,  the argument must be placed in [] brackets, for example %[,]F
           to output a comma, the pathname, and a separator.

           Fields %[SEP]$ and %u are switches and do not send anything to the out-
           put.

           The separator used by %P, %H, %N, %K, %B, and %S may be changed by pre-
           ceeding the field  by  %[SEP]$.   When  [SEP]  is  not  provided,  this
           reverses the separator to the default separator or the separator speci-
           fied with --separator.

           Formatted output is written for each matching pattern, which means that
           a  line may be output multiple times when patterns match more than once
           on the same line.  When field %u is found  anywhere  in  the  specified
           format  string,  matching  lines are output only once unless option -u,
           --ungroup is used or when a newline is matched.

           Additional formatting options:

           --format-begin=FORMAT
                  the FORMAT when beginning the search.

           --format-open=FORMAT
                  the FORMAT when opening a file and a match was found.

           --format-close=FORMAT
                  the FORMAT when closing a file and a match was found.

           --format-end=FORMAT
                  the FORMAT when ending the search.

           The context options -A, -B, -C, -y, and options -v,  --break,  --color,
           -T, and --null are disabled and have no effect on the formatted output.

    EXAMPLES
           Display lines containing the word `patricia' in `myfile.txt':

                  $ ugrep -w 'patricia' myfile.txt

           Count the number of lines containing the word `patricia' or `Patricia`:

                  $ ugrep -cw '[Pp]atricia' myfile.txt

           Count the number of words `patricia' of any mixed case:

                  $ ugrep -cowi 'patricia' myfile.txt

           List all Unicode words in a file:

                  $ ugrep -o '\w+' myfile.txt

           List all ASCII words in a file:

                  $ ugrep -o '[[:word:]]+' myfile.txt

           List the laughing face emojis (Unicode code points U+1F600 to U+1F60F):

                  $ ugrep -o '[\x{1F600}-\x{1F60F}]' myfile.txt

           Check if a file contains any non-ASCII (i.e. Unicode) characters:

                  $ ugrep -q '[^[:ascii:]]' myfile.txt && echo "contains Unicode"

           Display the line and column number of `FIXME' in C++ files using recur-
           sive search, with one line of context before and after a matched line:

                  $ ugrep --color -C1 -r -n -k -tc++ 'FIXME'

           List the C/C++ comments in a file with line numbers:

                  $ ugrep -n -e '//.*' -e '/\*([^*]|(\*+[^*/]))*\*+\/' myfile.cpp

           The same, but using predefined pattern c++/comments:

                  $ ugrep -n -f c++/comments myfile.cpp

           List  the  lines that need fixing in a C/C++ source file by looking for
           the word `FIXME' while skipping any `FIXME' in quoted strings:

                  $ ugrep -e 'FIXME' -N '"(\\.|\\\r?\n|[^\\\n"])*"' myfile.cpp

           The same, but using predefined pattern cpp/zap_strings:

                  $ ugrep -e 'FIXME' -f cpp/zap_strings myfile.cpp

           Find lines with `FIXME' or `TODO':

                  $ ugrep -n -e 'FIXME' -e 'TODO' myfile.cpp

           Find lines with `FIXME' that also contain the word `urgent':

                  $ ugrep -n 'FIXME' myfile.cpp | ugrep -w 'urgent'

           Find lines with `FIXME' but not the word `later':

                  $ ugrep -n 'FIXME' myfile.cpp | ugrep -v -w 'later'

           Output a list of line numbers of lines with `FIXME' but not `later':

                  $ ugrep -n 'FIXME' myfile.cpp | ugrep -vw 'later' |
                    ugrep -P '^(\d+)' --format='%,%n'

           Monitor the system log for bug reports:

                  $ tail -f /var/log/system.log | ugrep --color -i -w 'bug'

           Find lines with `FIXME' in the C/C++ files stored in a tarball:

                  $ ugrep -z -tc++ -n 'FIXME' project.tgz

           Recursively search for the word `copyright' in cpio/jar/pax/tar/zip ar-
           chives, compressed and regular files, and in PDFs using a PDF filter:

                  $ ugrep -r -z -w --filter='pdf:pdftotext % -' 'copyright'

           Match  the  binary  pattern `A3hhhhA3hh' (hex) in a binary file without
           Unicode pattern matching -U (which would otherwise match  `\xaf'  as  a
           Unicode  character  U+00A3  with UTF-8 byte sequence C2 A3) and display
           the results in hex with -X using `less -R' as a pager:

                  $ ugrep --pager -UXo '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

           Hexdump an entire file in color:

                  $ ugrep --color -X '' a.out

           List all files that are not ignored by one or more `.gitignore':

                  $ ugrep -Rl '' --ignore-files

           List all files containing a RPM signature, located in the `rpm'  direc-
           tory and recursively below up to two levels deeper:

                  $ ugrep -R --max-depth=3 -l -tRpm '' rpm/

           Display all words in a MacRoman-encoded file that has CR newlines:

                  $ ugrep -QMACROMAN '\w+' mac.txt

    BUGS
           Report bugs at:

                  https://github.com/Genivia/ugrep/issues


    LICENSE
           ugrep  is  released under the BSD-3 license.  All parts of the software
           have reasonable copyright terms permitting free  redistribution.   This
           includes the ability to reuse all or parts of the ugrep source tree.

    SEE ALSO
           grep(1).



    ugrep 1.7.3                    January 20, 2020                       UGREP(1)

<a name="patterns"/>

Regex patterns
--------------

<a name="posix-syntax"/>

### POSIX regular expression syntax

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
  `\Aφ`     | matches `φ` at the begin of input (top-level `φ`, not nested in a sub-pattern)
  `φ\z`     | matches `φ` at the end of input (top-level `φ`, not nested in a sub-pattern)
  `\bφ`     | matches `φ` starting at a word boundary (top-level `φ`, not nested in a sub-pattern)
  `φ\b`     | matches `φ` ending at a word boundary (top-level `φ`, not nested in a sub-pattern)
  `\Bφ`     | matches `φ` starting at a non-word boundary (top-level `φ`, not nested in a sub-pattern)
  `φ\B`     | matches `φ` ending at a non-word boundary (top-level `φ`, not nested in a sub-pattern)
  `\<φ`     | matches `φ` that starts a word (top-level `φ`, not nested in a sub-pattern)
  `\>φ`     | matches `φ` that starts a non-word (top-level `φ`, not for sub-patterns `φ`)
  `φ\<`     | matches `φ` that ends a non-word (top-level `φ`, not nested in a sub-pattern)
  `φ\>`     | matches `φ` that ends a word (top-level `φ`, not nested in a sub-pattern)
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

<a name="posix-classes"/>

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

<a name="posix-categories"/>

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
  `[:^blank:]` | `\P{Blank}`, `\H` | matches a non-blank character `[^ \t]`
  `[:^digit:]` | `\P{Digit}`, `\D` | matches a non-digit `[^0-9]`

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

<a name="perl-syntax"/>

### Perl regular expression syntax

For the pattern syntax of **ugrep** option `-P` (Perl regular expressions), see
the Boost.Regex documentation
[Perl regular expression syntax](https://www.boost.org/doc/libs/1_70_0/libs/regex/doc/html/boost_regex/syntax/perl_syntax.html).

[travis-image]: https://travis-ci.org/Genivia/ugrep.svg?branch=master
[travis-url]: https://travis-ci.org/Genivia/ugrep
[lgtm-image]: https://img.shields.io/lgtm/grade/cpp/g/Genivia/ugrep.svg?logo=lgtm&logoWidth=18
[lgtm-url]: https://lgtm.com/projects/g/Genivia/ugrep/context:cpp
[bsd-3-image]: https://img.shields.io/badge/license-BSD%203--Clause-blue.svg
[bsd-3-url]: https://opensource.org/licenses/BSD-3-Clause
