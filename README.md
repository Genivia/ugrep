[![build status][travis-image]][travis-url] [![Language grade: C/C++][lgtm-image]][lgtm-url] [![license][bsd-3-image]][bsd-3-url]

**ugrep v3.3 is now available: more features & even faster than before**

Search for anything in everything... ultra fast

*New option -Q opens a query UI to search files as you type!*
<br>
<img src="https://www.genivia.com/images/scranim.gif" width="438" alt="">

- Supports all GNU/BSD grep standard options; a compatible replacement for GNU/BSD grep

- Matches Unicode patterns by default in UTF-8, UTF-16, UTF-32 encoded files

- Ultra fast with new match algorithms and features beating grep, ripgrep, silver searcher, ack, sift, etc.

- Written in clean and efficient C++11 for advanced features and speed, thoroughly tested

- Portable (Linux, Unix, MacOS, Windows, etc), includes x86 and x64 binaries for Windows in the [releases](https://github.com/Genivia/ugrep/releases)

- User-friendly with sensible defaults and customizable [configuration files](#config) used by the `ug` command, a short command for `ugrep --config` to load a .ugrep configuration file with your preferences

      ug PATTERN ...                         ugrep --config PATTERN ...

- Interactive [query UI](#query), press F1 or CTRL-Z for help and TAB/SHIFT-TAB to navigate to dirs and files

      ugrep -Q                               ugrep -Q -e PATTERN    

- Find approximate pattern matches with [fuzzy search](#fuzzy), within the specified Levenshtein distance

      ugrep -Z PATTERN ...                   ugrep -Z3 PATTTERN ...

- Search with Google-like [Boolean search patterns](#bool) with option `--bool` patterns with `AND` (or just space), `OR` (or a bar `|`), `NOT` (or a dash `-`), using quotes to match exactly, and grouping with `( )`; or with options `-e` (as an "or"), `--and`, `--andnot`, and `--not` regex patterns

      ugrep --bool 'PATT1 PATT2 PATT3' ...   ugrep -e PATT1 --and PATT2 --and PATT3 ...
      ugrep --bool 'PATT1|PATT2 PATT3' ...   ugrep -e PATT1 -e PATT2 --and PATT3 ...
      ugrep --bool 'PATT1 -PATT2 -PATT3' ... ugrep -e PATT1 --andnot PATT2 --andnot PATT3 ...
      ugrep --bool 'PATT1 -(PATT2|PATT3)'... ugrep -e PATT1 --andnot PATT2 --andnot PATT3 ...
      ugrep --bool '"PATT1" "PATT2"' ...     ugrep -e '\QPATT1\E' --and '\QPATT2\E' ...

- Fzf-like search with regex (or fixed strings with `-F`), fuzzy matching with up to 4 extra characters with `-Z+4`, and words only with `-w`, press TAB and ALT-y to view a file, SHIFT-TAB and Alt-l to go back to view the list of matching files ordered by best match

      ugrep -Q1 --bool -l -w -Z+4 --sort=best

- Search the contents of [archives](#archives) (cpio, jar, tar, pax, zip) and [compressed files](#archives) (zip, gz, Z, bz, bz2, lzma, xz, lz4, zstd)

      ugrep -z PATTERN ...

- Search pdf, doc, docx, xls, xlxs, and more [using filters](#filter)

      ugrep --filter='pdf:pdftotext % -' PATTERN ...
      ugrep --filter='odt,doc,docx,rtf,xls,xlsx,ppt,pptx:soffice --headless --cat %' PATTERN ...
      ugrep --filter='pem:openssl x509 -text,cer,crt,der:openssl x509 -text -inform der' PATTERN ...

- Search [binary files](#binary) and display hexdumps with binary pattern matches (Unicode text or raw byte patterns)

      ugrep -W TEXTPATTERN ...               ugrep -X TEXTPATTERN ...
      ugrep -W -U BYTEPATTERN ...            ugrep -X -U BYTEPATTERN ...

- Include files to search by [filename extensions](#magic) or exclude them with `^`

      ugrep -O EXT PATTERN ...               ugrep -O ^EXT PATTERN ...

- Include files to search by [file types or file "magic bytes"](#magic) or exclude them with `^`

      ugrep -t TYPE PATTERN ...              ugrep -t ^TYPE PATTERN ...
      ugrep -M 'MAGIC' PATTERN ...           ugrep -M '^MAGIC' PATTERN ...

- Include files and directories to search that match [gitignore-style globs](#globs) or exclude them with `^`

      ugrep -g 'FILEGLOB' PATTERN ...        ugrep -g '^FILEGLOB' PATTERN ...
      ugrep -g 'DIRGLOB/' PATTERN ...        ugrep -g '^DIRGLOB/' PATTERN ...
      ugrep -g 'PATH/FILEGLOB' PATTERN ...   ugrep -g '^PATH/FILEGLOB' PATTERN ...
      ugrep -g 'PATH/DIRGLOB/' PATTERN ...   ugrep -g '^PATH/DIRGLOB/' PATTERN ...

- Include [hidden files (dotfiles) and directories](#hidden) to search (hidden files are omitted by default)

      ugrep -. PATTERN ...                   ugrep -g'.*,.*/' PATTERN ...

- Exclude files specified by [.gitignore](#ignore) etc.

      ugrep --ignore-files PATTERN ...       ugrep --ignore-files=.ignore PATTERN ...

- Search patterns excluding [negative patterns](#not) ("match this if it does not match that")

      ugrep PATTERN -N NOTPATTERN ...        ugrep '[0-9]+' -N 123 ...

- Includes [predefined regex patterns](#source) to search source code, javascript, XML, JSON, HTML, PHP, markdown, etc.

      ugrep PATTERN -f c++/zap_comments -f c++/zap_strings ...
      ugrep PATTERN -f php/zap_html ...
      ugrep -f js/functions ... | ugrep PATTERN ...

- Sort matching files by [name, best match, size, and time](#sort)

      ugrep --sort PATTERN ...               ugrep --sort=size PATTERN ...
      ugrep --sort=changed PATTERN ...       ugrep --sort=created PATTERN ...
      ugrep -Z --sort=best PATTERN ...

- Output results in [CSV, JSON, XML](#json), and [user-specified formats](#format)

      ugrep --csv PATTERN ...                ugrep --json PATTERN ...
      ugrep --xml PATTERN ...                ugrep --format='file=%f line=%n match=%O%~' PATTERN ...

- Search with PCRE's Perl-compatible regex patterns and display or replace [subpattern matches](#replace)

      ugrep -P PATTERN ...                   ugrep -P --format='%1 and %2%~' 'PATTERN(SUB1)(SUB2)' ...

- Search files with a specific [encoding](#encoding) format such as ISO-8859-1 thru 16, CP 437, CP 850, MACROMAN, KOI8, etc.

      ugrep --encoding=LATIN1 PATTERN ...

- Search patterns that match multiple lines (by default), i.e. patterns may contain one or more `\n` newlines

<a name="toc"/>

Table of contents
-----------------

- [Download and install](#install)
- [Performance comparisons](#speed)
- [Using ugrep within Vim](#vim)
- [Using ugrep to replace GNU/BSD grep](#grep)
  - [Equivalence to GNU/BSD grep](#equivalence)
  - [Short and quick command aliases](#aliases)
  - [Notable improvements over grep](#improvements)
- [Tutorial](#tutorial)
  - [Examples](#examples)
  - [Advanced examples](#advanced)
  - [Displaying helpful info](#help)
  - [Configuration files](#config)
  - [Interactive search with -Q](#query)
  - [Recursively list matching files with -l, -R, -r, --depth, -g, -O, and -t](#recursion)
  - [Boolean search patterns with --bool (-%), --and, --not](#bool)
  - [Search this but not that with -v, -e, -N, -f, -L, -w, -x](#not)
  - [Search non-Unicode files with --encoding](#encoding)
  - [Matching multiple lines of text](#multiline)
  - [Displaying match context with -A, -B, -C, and -y](#context)
  - [Searching source code using -f, -O, and -t](#source)
  - [Searching compressed files and archives with -z](#archives)
  - [Find files by file signature and shebang "magic bytes" with -M, -O and -t](#magic)
  - [Fuzzy search with -Z](#fuzzy)
  - [Search hidden files with -.](#hidden)
  - [Using filter utilities to search documents with --filter](#filter)
  - [Searching and displaying binary files with -U, -W, and -X](#binary)
  - [Ignore binary files with -I](#nobinary)
  - [Ignoring .gitignore-specified files with --ignore-files](#ignore)
  - [Using gitignore-style globs to select directories and files to search](#globs)
  - [Including or excluding mounted file systems from searches](#fs)
  - [Counting the number of matches with -c and -co](#count)
  - [Displaying file, line, column, and byte offset info with -H, -n, -k, -b, and -T](#fields)
  - [Displaying colors with --color and paging the output with --pager](#color)
  - [Output matches in JSON, XML, CSV, C++](#json)
  - [Customize output with --format](#format)
  - [Replacing matches with -P --format backreferences to group captures](#replace)
  - [Limiting the number of matches with -1,-2...-9, -K, -m, and --max-files](#max)
  - [Matching empty patterns with -Y](#empty)
  - [Case-insensitive matching with -i and -j](#case)
  - [Sort files by name, best match, size, and time](#sort)
  - [Tips for advanced users](#tips)
  - [More examples](#more)
- [Man page](#man)
- [Regex patterns](#patterns)
  - [POSIX regular expression syntax](#posix-syntax)
  - [POSIX and Unicode character classes](#posix-classes)
  - [POSIX and Unicode character categories](#posix-categories)
  - [Perl regular expression syntax](#perl-syntax)
- [Troubleshooting](#bugs)

<a name="install"/>

Download and install
--------------------

### Homebrew for MacOS (and Linux)

Install the latest **ugrep** with [Homebrew](https://brew.sh):

    $ brew install ugrep

This installs the `ugrep` and `ug` commands, where `ug` is the same as `ugrep`
but also loads the configuration file .ugrep when present in the working
directory or home directory.

### Windows

Download the full-featured `ugrep.exe` executable as release artifacts from
<https://github.com/Genivia/ugrep/releases> or use [Scoop](https://scoop.sh)
`scoop install ugrep`.

Download release artifact `ugrep.exe`.  Copy `ugrep.exe` to `ug.exe` if you
also want the `ug` command, which loads the .ugrep configuration file when
present in the working directory or home directory.

Add `ugrep.exe` and `ug.exe` to your execution path: go to *Settings* and
search for "Path" in *Find a Setting*.  Select *environment variables* ->
*Path* -> *New* and add the directory where you placed the `ugrep.exe` and
`ug.exe` executables.

Notes on using `ugrep.exe` and `ug.exe` from the Windows command line:
- file and directory globs should be specified with option `-g/GLOB` instead
  of a `GLOB` command line argument (globbing is disabled, because `*` and `?`
  in patterns would get replaced).
- when quoting patterns and arguments on the command line, do not use single
  `'` quotes but use `"` instead; most Windows command utilities consider
  the single `'` quotes part of the command-line argument!
- when specifying an empty pattern `""` to match all input, this may be ignored
  by some Windows command interpreters such as Powershell, in that case use
  option `--match` instead.

### Debian

    $ apt-get install ugrep

Check <https://packages.debian.org/sid/main/ugrep> for version info.  To build
and try `ugrep` locally, see "All platforms" build steps further below.

### NetBSD

You can use the standard NetBSD package installer (pkgsrc):
<http://cdn.netbsd.org/pub/pkgsrc/current/pkgsrc/textproc/ugrep/README.html>

### Haiku

    $ pkgman install cmd:ugrep

Check <https://github.com/haikuports/haikuports/tree/master/app-text/ugrep> for
version info.  To build and try `ugrep` locally, see "All platforms" build
steps further below.

### Alpine Linux

    $ apk add ugrep ugrep-doc

Check <https://pkgs.alpinelinux.org/packages?name=ugrep> for version info.

### All platforms: step 1 download

Clone `ugrep` with

    $ git clone https://github.com/Genivia/ugrep

Or visit <https://github.com/Genivia/ugrep/releases> to download a specific
release.

### All platforms: step 2 consider optional dependencies

You can always add these later, when you need these features:

- Option `-P` (Perl regular expressions) requires either the PCRE2 library
  (preferred) or the Boost.Regex library.  If PCRE2 is not installed,
  install PCRE2 with e.g. `sudo apt-get install -y libpcre2-dev` or
  [download PCRE2](https://www.pcre.org) and follow the installation
  instructions.  Alternatively,
  [download Boost.Regex](https://www.boost.org/users/download) and run
  `./bootstrap.sh` and `sudo ./b2 --with-regex install`.  See
  [Boost: getting started](https://www.boost.org/doc/libs/1_72_0/more/getting_started/unix-variants.html).

- Option `-z` (compressed files and archives search) requires the
  [zlib](https://www.zlib.net) library installed.  It is installed on most
  systems.  If not, install it, e.g. with `sudo apt-get install -y libz-dev`.
  To search `.bz` and `.bz2` files, install the
  [bzip2](https://www.sourceware.org/bzip2) library, e.g. with
  `sudo apt-get install -y libbz2-dev`.  To search `.lzma` and `.xz` files,
  install the [lzma](https://tukaani.org/xz) library, e.g. with
  `sudo apt-get install -y liblzma-dev`.  To search `.lz4` files, install the
  [lz4](https://github.com/lz4/lz4) library, e.g. with
  `sudo apt-get install -y liblz4-dev`.  To search `.zst` files, install the
  [zstd](http://facebook.github.io/zstd) library, e.g. with
  `sudo apt-get install -y libzstd-dev`

After installing one or more of these libraries, re-execute the commands to
rebuild `ugrep`:

    $ cd ugrep
    $ ./build.sh

Some Linux systems may not be configured to load dynamic libraries from
`/usr/local/lib`, causing a library load error when running `ugrep`.  To
correct this, add `export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/local/lib"` to
your `~/.bashrc` file.  Or run `sudo ldconfig /usr/local/lib`.

### All platforms: step 3 build

Build `ugrep` on Unix-like systems with colors enabled by default:

    $ cd ugrep
    $ ./build.sh

This builds the `ugrep` executable in the `ugrep/src` directory with
`./configure` and `make -j`, verified with `make test`.  When all tests pass,
the `ugrep` executable is copied to `ugrep/bin/ugrep` and the symlink
`ugrep/bin/ug -> ugrep/bin/ugrep` is added for the `ug` command.

Note that `ug` is the same as `ugrep` but also loads the configuration file
.ugrep when present in the working directory or home directory.  This means
that you can define your default options for `ug` in .ugrep.

To build `ugrep` with specific hard defaults enabled, such as a pager:

    $ cd ugrep
    $ ./build.sh --enable-pager

Options to select defaults for builds include:

- `--enable-hidden` search hidden files and directories
- `--enable-pager` use a pager to display output on terminals
- `--enable-pretty` colorize output to terminals and add filename headings
- `--disable-auto-color` disable automatic colors, requires ugrep option `--color=auto` to show colors
- `--disable-mmap` disable memory mapped files
- `--with-grep-path` the default `-f` path if `GREP_PATH` is not defined
- `--with-grep-colors` the default colors if `GREP_COLORS` is not defined
- `--help` display build options

After the build completes, copy `ugrep/bin/ugrep` and `ugrep/bin/ug` to a
convenient location, for example in your `~/bin` directory.

You may want to install the `ugrep` and `ug` commands and man pages with:

    $ sudo make install

This also installs the pattern files with predefined patterns for option `-f`
at `/usr/local/share/ugrep/patterns/`.  Option `-f` first checks the working
directory for the presence of pattern files, if not found checks environment
variable `GREP_PATH` to load the pattern files, and if not found reads the
installed predefined pattern files.

### Troubleshooting

#### Git and timestamps

Unfortunately, git clones do not preserve timestamps which means that you may
run into "WARNING: 'aclocal-1.15' is missing on your system." or that
autoheader was not found when running `make`.

To work around this problem, run:

    $ autoreconf -fi
    $ ./build.sh

#### Compiler warnings

GCC 8 and greater may produce warnings of the sort *"note: parameter passing
for argument ... changed in GCC 7.1"*.  These warnings should be ignored.

### Dockerfile for developers

A Dockerfile is included to build `ugrep` in a Ubuntu container.

Developers may want to use sanitizers to verify the **ugrep** code when making
significant changes, for example to detect data races with the
[ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html):

    $ ./build.sh CXXFLAGS='-fsanitize=thread -O1 -g'

We checked `ugrep` with the clang AddressSanitizer, MemorySanitizer,
ThreadSanitizer, and UndefinedBehaviorSanitizer.  These options incur
significant runtime overhead and should not be used for the final build.

🔝 [Back to table of contents](#toc)

<a name="speed">

Performance comparisons
-----------------------

Performance comparisons should represent what users can expect the performance
to be in practice.  There should not be any shenanigans to trick the system to
perform more optimally or to degrade an important aspect of the search to make
one grep tool look better than another.

**ugrep** is a no-nonsense fast search tool that utilizes a worker pool of
threads with clever lock-free job queue stealing for optimized load balancing.
A new hashing technique is used to identify possible matches to speed up
multi-pattern matches.  In addition, regex matching is optimized with AVX/SSE
and ARM NEON/AArch64 instructions.  Compressed files are decompressed
concurrently while searching to further increase performance.  Asynchronous IO
is implemented for efficient input and output.

**ugrep** performs very well overall and particularly well when searching
compressed files and archives.  This means that at its core, the search
engine's performance of ugrep excellent if not the best among grep tools
available.

### Benchmarks

The following benchmark tests span a range of practical use cases:

Test | Command                                                          | Description
---- | ---------------------------------------------------------------- | -----------------------------------------------------
T1   | `GREP -c quartz enwik8`                                          | count "quartz" in a 100MB file (word with low frequency letters)
T2   | `GREP -c sternness enwik8`                                       | count "sternness" in a 100MB file (word with high frequency letters)
T3   | `GREP -c 'Sherlock Holmes' en.txt`                               | count "Sherlock Holmes" in a huge [13GB decompressed file](http://opus.nlpl.eu/download.php?f=OpenSubtitles/v2018/mono/OpenSubtitles.raw.en.gz)
T4   | `GREP -cw -e char -e int -e long -e size_t -e void big.cpp`      | count 5 short words in a 35MB C++ source code file
T5   | `GREP -Eon 'serialize_[a-zA-Z0-9_]+Type' big.cpp`                | search and display C++ serialization functions in a 35MB source code file
T6   | `GREP -Fon -f words1+1000 enwik8`                                | search 1000 words of length 1 or longer in a 100MB Wikipedia file
T7   | `GREP -Fon -f words2+1000 enwik8`                                | search 1000 words of length 2 or longer in a 100MB Wikipedia file
T8   | `GREP -Fon -f words4+1000 enwik8`                                | search 1000 words of length 4 or longer in a 100MB Wikipedia file
T9   | `GREP -Fon -f words8+1000 enwik8`                                | search 1000 words of length 8 or longer in a 100MB Wikipedia file
T10  | `GREP -ro '#[[:space:]]*include[[:space:]]+"[^"]+"' -Oh,hpp,cpp` | multi-threaded recursive search of `#include "..."` in the directory tree from the Qt 5.9.2 root, restricted to `.h`, `.hpp`, and `.cpp` files
T11  | `GREP -ro '#[[:space:]]*include[[:space:]]+"[^"]+"' -Oh,hpp,cpp` | same as T10 but single-threaded
T12  | `GREP -z -Fc word word*.gz`                                      | count `word` in 6 compressed files of 1MB to 3MB each

Note: T10 and T11 use `ugrep` option `-Oh,hpp,cpp` to restrict the search to
files with extensions `.h`, `.hpp`, and `.cpp`, which is formulated with
GNU/BSD/PCRGE grep as `--include='*.h' --include='*.hpp' --include='*.cpp'`,
with silver searcher as `-G '.*\.(h|hpp|cpp)'` requiring `--search-binary` to
search compressed files (a bug), and with ripgrep as `--glob='*.h'
--glob='*.hpp' --glob='*.cpp'`.

The corpora used in the tests are available for
[download](https://www.genivia.com/files/corpora.zip).

### Performance results

The following performance tests were conducted with a new and common MacBook
Pro using clang 9.0.0 -O2 on a 2.9 GHz Intel Core i7, 16 GB 2133 MHz LPDDR3 Mac
OS 10.12.6 machine.  The best times of 30 runs is shown under minimal machine
load.  When comparing tools, the same match counts were produced.

Results are shown in real time (wall clock time) seconds elapsed.  Best times
are shown in **boldface** and *n/a* means that the running time exceeded 1
minute or the selected options are not supported (T12: option `-z`) or the
input file is too large (T3: 13GB file) resulting in an error.

GREP            | T1       | T2       | T3       | T4       | T5       | T6       | T7       | T8       | T9       | T10      | T11      | T12      |
--------------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- | -------- |
ugrep           | **0.03** | **0.04** | **5.06** | **0.07** | **0.02** | **0.98** | **0.97** | **0.87** | **0.26** | **0.10** | **0.19** | **0.02** |
hyperscan grep  | 0.09     | 0.10     | **4.35** | 0.11     | 0.04     | 7.78     | 3.39     | 1.41     | 1.17     | *n/a*    | *n/a*    | *n/a*    |
ripgrep         | 0.06     | 0.10     | 7.50     | 0.19     | 0.06     | 2.20     | 2.07     | 2.01     | 2.14     | 0.12     | 0.36     | 0.03     |
silver searcher | 0.10     | 0.11     | *n/a*    | 0.16     | 0.21     | *n/a*    | *n/a*    | *n/a*    | *n/a*    | 0.45     | 0.32     | 0.09     |
GNU grep 3.3    | 0.08     | 0.15     | 11.26    | 0.18     | 0.16     | 2.70     | 2.64     | 2.42     | 2.26     | *n/a*    | 0.26     | *n/a*    |
PCREGREP 8.42   | 0.17     | 0.17     | *n/a*    | 0.26     | 0.08     | *n/a*    | *n/a*    | *n/a*    | *n/a*    | *n/a*    | 2.37     | *n/a*    |
BSD grep 2.5.1  | 0.81     | 1.60     | *n/a*    | 1.85     | 0.83     | *n/a*    | *n/a*    | *n/a*    | *n/a*    | *n/a*    | 3.35     | 0.60     |

Note T3: [Hyperscan simplegrep](https://github.com/intel/hyperscan/tree/master/examples)
was compiled with optimizations enabled.  Hyperscan results for T3 are slightly
better than ugrep, as expected because hyperscan simplegrep has one advantage
here: it does not maintain line numbers and other line-related information.  By
contrast, line information should be tracked (as in ugrep) to determine if
matches are on the same line or not, as required by option `-c`.  Hyperscan
simplegrep returns more matches than other greps due to its "all matches
reported" pattern matching behavior.

Note T4-T9: Hyperscan simplegrep does not support command line options.  Option
`-w` was emulated using the pattern `\b(char|int|long|size_t|void)\b`.  Option
`-f` was emulated as follows:

    paste -d'|' -s words1+1000 > pattern.txt
    /usr/bin/time ./simplegrep `cat pattern.txt` enwik8 | ./null

Note T10+T11: [silver searcher 2.2.0](https://github.com/ggreer/the_silver_searcher)
runs slower with multiple threads (T10 0.45s) than single-threaded (T11 0.32s),
which was reported as an issue to the maintainers.

Output is sent to a `null` utility to eliminate terminal display overhead
(`> /dev/null` cannot be used as some greps detect it to remove all output).
The `null` utility source code:

    #include <sys/types.h>
    #include <sys/uio.h>
    #include <unistd.h>
    int main() { char buf[65536]; while (read(0, buf, 65536) > 0) continue; }

Performance results may depend on warm/cold runs, compilers, libraries,
the OS, the CPU type, and file system latencies.  However, comparable
competitive results were obtained on many other types of machines.

🔝 [Back to table of contents](#toc)

<a name="vim"/>

Using ugrep within Vim
----------------------

First, let's define the `:grep` command in Vim to search files recursively.  To
do so, add the following lines to your `.vimrc` located in the root directory:

    if executable('ugrep')
        set grepprg=ugrep\ -RInk\ -j\ -u\ --tabs=1\ --ignore-files
        set grepformat=%f:%l:%c:%m,%f+%l+%c+%m,%-G%f\\\|%l\\\|%c\\\|%m
    endif

This specifies case insensitive searches with the Vim `:grep` command.  For
case sensitive searches, remove `\ -j` from `grepprg`.  Multiple matches on the
same line are listed in the quickfix window separately.  If this is not
desired, remove `\ -u` from `grepprg`.  With this change, only the first match
on a line is shown.  Option `--ignore-files` skips files specified in
`.gitignore` files, when present.  To limit the depth of recursive searches to
the current directory only, append `\ -1` to `grepprg`.

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
        let g:ctrlp_user_command='ugrep %s -Rl -I --ignore-files -3'
    endif

These options are optional and may be omitted: `-I` skips binary files,
option `--ignore-files` skips files specified in `.gitignore` files, when
present, and option `-3` restricts searching directories to three levels (the
working directory and up to two levels below).  

Start Vim then enter the command:

    :helptags ~/.vim/bundle/ctrlp.vim/doc

To view the CtrlP documentation in Vim, enter the command:

    :help ctrlp.txt

🔝 [Back to table of contents](#toc)

<a name="grep"/>

Using ugrep to replace GNU/BSD grep
-----------------------------------

<a name="equivalence"/>

### Equivalence to GNU/BSD grep

**ugrep** is equivalent to GNU/BSD grep when the following options are used:

    grep   = ugrep --sort -G -U -Y -. -Dread -dread
    egrep  = ugrep --sort -E -U -Y -. -Dread -dread
    fgrep  = ugrep --sort -F -U -Y -. -Dread -dread

    zgrep  = ugrep --sort -G -U -Y -z -. -Dread -dread
    zegrep = ugrep --sort -E -U -Y -z -. -Dread -dread
    zfgrep = ugrep --sort -F -U -Y -z -. -Dread -dread

where:

- `--sort` specifies output sorted by pathname, showing sorted matching files
  first followed by sorted recursive matches in subdirectories.  Otherwise,
  matching files are reported in no particular order to improve performance;
- `-U` disables Unicode pattern matching, so for example the pattern `\xa3`
  matches byte A3 instead of the Unicode code point U+00A3 represented by the
  UTF-8 sequence C2 A3.  By default in ugrep, `\xa3` matches U+00A3.  We do not
  recommend to use `-U` for text pattern searches, only for binary searches.
- `-Y` enables empty matches, so for example the pattern `a*` matches every
  line instead of a sequence of `a`'s.  By default in ugrep, the pattern `a*`
  matches a sequence of `a`'s.  Moreover, in ugrep the pattern `a*b*c*` matches
  what it is supposed to match by default.  See [improvements](#improvements).
- `-.` searches hidden files (dotfiles).  By default, hidden files are ignored,
  like most Unix utilities.
- `-Dread` and `-dread` are the GNU/BSD grep defaults but are not recommended,
  see [improvements](#improvements) for an explanation.

When the `ugrep` (or `ugrep.exe`) executable is renamed to `grep` (`grep.exe`),
`egrep` (`egrep.exe`), `fgrep` (`fgrep.exe`) and so on, then a subset of the
options shown above are automatically in effect except for `--sort`, `-Dread`,
`-dread`, and `-U` to permit Unicode matching.  For example, when `ugrep` is
renamed to `egrep`, options `-E`, `-Y`, and `-.` are automatically enabled.

Note that the defaults of some grep options may differ to make **ugrep** more
user friendly, see [notable improvements over grep](#improvements).

🔝 [Back to table of contents](#toc)

<a name="aliases"/>

### Short and quick command aliases

Commonly-used aliases to add to `.bashrc` to increase productivity:

    alias uq     = 'ug -Q'       # short & quick query UI (interactive, uses .ugrep config)
    alias ux     = 'ug -UX'      # short & quick binary pattern search (uses .ugrep config)
    alias uz     = 'ug -z'       # short & quick compressed files and archives search (uses .ugrep config)

    alias ugit   = 'ug -R --ignore-files' # works like git-grep & define your preferences in .ugrep config

    alias grep   = 'ugrep -G'    # search with basic regular expressions (BRE)
    alias egrep  = 'ugrep -E'    # search with extended regular expressions (ERE)
    alias fgrep  = 'ugrep -F'    # find string(s)
    alias pgrep  = 'ugrep -P'    # search with Perl regular expressions
    alias xgrep  = 'ugrep -W'    # search (ERE) and output text or hex for binary

    alias zgrep  = 'ugrep -zG'   # search compressed files and archives with BRE
    alias zegrep = 'ugrep -zE'   # search compressed files and archives with ERE
    alias zfgrep = 'ugrep -zF'   # find string(s) in compressed files and/or archives
    alias zpgrep = 'ugrep -zP'   # search compressed files and archives with Perl regular expressions
    alias zxgrep = 'ugrep -zW'   # search (ERE) compressed files/archives and output text or hex for binary

    alias xdump  = 'ugrep -X ""' # hexdump files without searching

To search PDF and office documents automatically, add a filter option to the
aliased `ugrep` command:

    --filter="pdf:pdftotext % -,odt,doc,docx,rtf,xls,xlsx,ppt,pptx:soffice --headless --cat %"

This requires the utilities [`pdftotext`](https://pypi.org/project/pdftotext)
and [`soffice`](https://www.libreoffice.org) to be installed.  See
[Using filter utilities to search documents with --filter](#filter).

🔝 [Back to table of contents](#toc)

<a name="improvements"/>

### Notable improvements over grep

- **ugrep** starts an interactive query UI with option `-Q`.
- **ugrep** matches patterns across multiple lines.
- **ugrep** matches Unicode by default (disabled with option `-U`).
- **ugrep** supports fuzzy (approximate) matching with option `-Z`.
- **ugrep** supports gitignore with option `--ignore-files`.
- **ugrep** supports user-defined global and local configuration files.
- **ugrep** supports Boolean patterns with AND, OR and NOT (option `--bool`).
- **ugrep** searches compressed files with option `-z`.
- **ugrep** searches cpio, jar, pax, tar and zip archives with option `-z`.
- **ugrep** searches pdf, doc, docx, xls, xlsx, epub, and more with `--filter`
  using third-party format conversion utilities as plugins.
- **ugrep** searches a directory when the FILE argument is a directory, like
  most Unix/Linux utilities; option `-r` searches directories recursively.
- **ugrep** does not match hidden files by default like most Unix/Linux
  utilities (hidden dotfile file matching is enabled with `-.`).
- **ugrep** regular expression patterns are more expressive than GNU grep and
  BSD grep POSIX ERE and support Unicode pattern matching.  Extended regular
  expression (ERE) syntax is the default (i.e. option `-E` as egrep, whereas
  `-G` enables BRE).
- **ugrep** spawns threads to search files concurrently to improve search
  speed (disabled with option `-J1`).
- **ugrep** produces hexdumps with `-W` (output binary matches in hex with text
  matches output as usual) and `-X` (output all matches in hex).
- **ugrep** can output matches in JSON, XML, CSV and user-defined formats (with
  option `--format`).
- **ugrep** option `-f` uses `GREP_PATH` environment variable or the predefined
  patterns installed in `/usr/local/share/ugrep/patterns`.  If `-f` is
  specified and also one or more `-e` patterns are specified, then options
  `-F`, `-x`, and `-w` do not apply to `-f` patterns.  This is to avoid
  confusion when `-f` is used with predefined patterns that may no longer work
  properly with these options.
- **ugrep** options `-O`, `-M`, and `-t` specify file extensions, file
  signature magic byte patterns, and predefined file types, respectively.  This
  allows searching for certain types of files in directory trees, for example
  with recursive search options `-R` and `-r`.  Options `-O`, `-M`, and `-t`
  also applies to archived files in cpio, jar, pax, tar, and zip files.
- **ugrep** option `-k`, `--column-number` to display the column number, taking
  tab spacing into account by expanding tabs, as specified by option `--tabs`.
- **ugrep** option `-P` (Perl regular expressions) supports backreferences
  (with `--format`) and lookbehinds, which uses the PCRE2 or Boost.Regex
  library for fast Perl regex matching with a PCRE-like syntax.
- **ugrep** option `-b` with option `-o` or with option `-u`, ugrep displays
  the exact byte offset of the pattern match instead of the byte offset of the
  start of the matched line reported by GNU/BSD grep.
- **ugrep** option `-u`, `--ungroup` to not group multiple matches per line.
  This option displays a matched input line again for each additional pattern
  match on the line.  This option is particularly useful with option `-c` to
  report the total number of pattern matches per file instead of the number of
  lines matched per file.
- **ugrep** option `-Y` enables matching empty patterns.  Grepping with
  empty-matching patterns is weird and gives different results with GNU grep
  versus BSD grep.  Empty matches are not output by **ugrep** by default, which
  avoids making mistakes that may produce "random" results.  For example, with
  GNU/BSD grep, pattern `a*` matches every line in the input, and actually
  matches `xyz` three times (the empty transitions before and between the `x`,
  `y`, and `z`).  Allowing empty matches requires **ugrep** option `-Y`.
  Patterns that start with `^` or end with `$`, such as `^\h*$`, match empty.
  These patterns automatically enable option `-Y`.
- **ugrep** option `-D, --devices=ACTION` is `skip` by default, instead of
  `read`.  This prevents unexpectedly hanging on named pipes in directories
  that are recursively searched, as may happen with GNU/BSD grep that `read`
  devices by default.
- **ugrep** option `-d, --directories=ACTION` is `skip` by default, instead of
  `read`.  By default, directories specified on the command line are searched,
  but not recursively deeper into subdirectories.
- **ugrep** offers *negative patterns* `-N PATTERN`, which are patterns of the
  form `(?^X)` that skip all `X` input, thus removing `X` from the search.
  For example, negative patterns can be used to skip strings and comments when
  searching for identifiers in source code and find matches that aren't in
  strings and comments.  Predefined `zap` patterns use negative patterns, for
  example, use `-f cpp/zap_comments` to ignore pattern matches in C++ comments.
- **ugrep** does not the `GREP_OPTIONS` environment variable, because the
  behavior of **ugrep** must be portable and predictable on every system.  Also
  GNU grep abandoned `GREP_OPTIONS` for this reason.  Please use the `ug`
  command that loads the .ugrep configuration file located in the working
  directory or in the home directory when present, or use shell aliases to
  create new commands with specific search options.

🔝 [Back to table of contents](#toc)

<a name="tutorial"/>

Tutorial
--------

<a name="examples"/>

### Examples

To perform a search using a configuration file `.ugrep` placed in the working
directory or home directory (note that `ug` is the same as `ugrep --config`):

    ug PATTERN FILE...

To save a `.ugrep` configuration file to the working directory, then edit this
file in your home directory to customize your preferences for `ug` defaults:

    ug --save-config

To search the working directory and recursively deeper for `main` (note that
`-R` recurse symlinks is enabled by default if no file arguments are
specified):

    ugrep main

Same, but only search C++ source code files recursively, ignoring all other
files:

    ugrep -tc++ main

Same, using the interactive query UI, starting with the initial search pattern
`main` (note that `-Q` with an initial pattern requires option `-e` because
patterns are normally specified interactively and all command line arguments
are considered files/directories):

    ugrep -Q -tc++ -e main

To search for `#define` (and `# define` etc) using a regex pattern in C++ files
(note that patterns should be quoted to prevent shell globbing of `*` and `?`):

    ugrep -tc++ '#[\t ]*define'

To search for `main` as a word (`-w`) recursively without following symlinks
(`-r`) in directory `myproject`, showing the matching line (`-n`) and column
(`-k`) numbers next to the lines matched:

    ugrep -r -nkw main myproject

Same, but only search `myproject` without recursing deeper (note that directory
arguments are searched at one level by default):

    ugrep -nkw main myproject

Same, but search `myproject` and one subdirectory level deeper (two levels)
with `-2`:

    ugrep -2 -nkw main myproject

Same, but only search C++ files in `myproject` and its subdirectories with
`-tc++`:

    ugrep -tc++ -2 -nkw main myproject

Same, but also search inside archives (e.g. zip and tar files) and compressed
files with `-z`:

    ugrep -z -tc++ -2 -nkw main myproject

Search recursively the working directory for `main` while ignoring gitignored
files (e.g.  assuming `.gitignore` is in the working directory or below):

    ugrep --ignore-files -tc++ -nkw main

To list all files in the working directory and deeper that are not ignored by
`.gitignore` file(s):

    ugrep --ignore-files -l ''

To display the list of file name extensions and "magic bytes" (shebangs)
that are searched corresponding to `-t` arguments:

    ugrep -tlist

To list all shell files recursively, based on extensions and shebangs with `-l`
(note that `''` matches any non-empty file):

    ugrep -l -tShell ''

🔝 [Back to table of contents](#toc)

<a name="advanced"/>

### Advanced examples

To search for `main` in source code while ignoring strings and comment blocks
we can use *negative patterns* with option `-N` to skip unwanted matches in
C/C++ quoted strings and comment blocks:

    ugrep -r -nkw 'main' -N '"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*([^*]|\n|(\*+([^*/]|\n)))*\*+\/' myproject

This is a lot of work to type in correctly!  If you are like me, I don't want
to spend time fiddling with regex patterns when I am working on something more
important.  There is an easier way by using **ugrep**'s predefined patterns
(`-f`) that are installed with the `ugrep` tool:

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
`main|(?^main\h*\()`, where `\h` matches space and tab.  In general, negative
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
includes header files when we want to only search `.c` and `.cpp` files.

We can also skip files and directories from being searched that are defined in
`.gitignore`.  To do so we use `--ignore-files` to exclude any files and
directories from recursive searches that match the globs in `.gitignore`, when
one ore more`.gitignore` files are found:

    ugrep -R -tc++ --ignore-files -f c++/defines

This searches C++ files (`-tc++`) in the working directory for `#define`
lines (`-f c++/defines`), while skipping files and directories declared in
`.gitignore`.  If you find this too long to type then define an alias to search
GitHub directories:

    alias ugit='ugrep -R --ignore-files'
    ugit -tc++ -f c++/defines

To highlight matches when pushed through a chain of pipes we should use
`--color=always`:

    ugit --color=always -tc++ -f c++/defines | ugrep -w 'FOO.*'

This returns a color-highlighted list of all `#define FOO...` macros in C/C++
source code files, skipping files defined in `.gitignore`.

Note that the complement of `--exclude` is not `--include`, because exclusions
always take precedence over inclusions, so we cannot reliably list the files
that are ignored with `--include-from='.gitignore'`.  Only files explicitly
specified with `--include` and directories explicitly specified with
`--include-dir` are visited.  The `--include-from` from lists globs that are
considered both files and directories to add to `--include` and
`--include-dir`, respectively.  This means that when directory names and
directory paths are not explicitly listed in this file then it will not be
visited using `--include-from`.

🔝 [Back to table of contents](#toc)

<a name="help"/>

### Displaying helpful info

The ugrep man page:

    man ugrep

To show a help page:

    ugrep --help

To show options that mention `WHAT`:

    ugrep --help WHAT

To show a list of `-t TYPES` option values:

    ugrep -tlist

In the interactive query UI, press F1 or CTRL-Z for help and options:

    ugrep -Q

🔝 [Back to table of contents](#toc)

<a name="config"/>

### Configuration files

    --config[=FILE], ---[FILE]
            Use configuration FILE.  The default FILE is `.ugrep'.  The working
            directory is checked first for FILE, then the home directory.  The
            options specified in the configuration FILE are parsed first,
            followed by the remaining options specified on the command line.
    --save-config[=FILE]
            Save configuration FILE.  By default `.ugrep' is saved.  If FILE is
            a `-', write the configuration to standard output.

#### The ug command versus the ugrep command

The `ug` command is intended for context-dependent interactive searching and is
equivalent to the `ugrep --config` command to load the configuration file
`.ugrep`, when present in the working directory or, when not found, in the home
directory:

    ug PATTERN ...
    ugrep --config PATTERN ...

A configuration file contains `NAME=VALUE` pairs per line, where `NAME` is the
name of a long option (without `--`) and `=VALUE` is an argument, which is
optional and may be omitted depending on the option.  Empty lines and lines
starting with a `#` are ignored:

    # Color scheme
    colors=cx=hb:ms=hiy:mc=hic:fn=hi+y+K:ln=hg:cn=hg:bn=hg:se=
    # Disable searching hidden files and directories
    no-hidden
    # ignore files specified in .ignore and .gitignore in recursive searches
    ignore-files=.ignore
    ignore-files=.gitignore

Command line options are parsed in the following order: first the (default or
named) configuration file is loaded, then the remaining options and
arguments on the command line are parsed.

Option `--stats` displays the configuration file used after searching.

#### Named configuration files

Named configuration files are intended to streamline custom search tasks, by
reducing the number of command line options to just one `---FILE` to use the
collection of options specified in `FILE`.  The `--config=FILE` option and its
abbreviated form `---FILE` load the specified configuration file located in the
working directory or, when not found, located in the home directory:

    ug ---FILE PATTERN ...
    ugrep ---FILE PATTERN ...

An error is produced when `FILE` is not found or cannot be read.

Named configuration files can be used to define a collection of options that
are specific to the requirements of a task in the development workflow of a
project.  For example to report unresolved issues by checking the source code
and documentation for comments with FIXME and TODO items.  Such named
configuration file can be localized to a project by placing it in the project
directory, or it can be made global by placing it in the home directory.  For
visual feedback, a color scheme specific to this task can be specified with
option `colors` in the configuration `FILE` to help identify the output
produced by a named configuration as opposed to the default configuration.

#### Saving a configuration file

The `--save-config` option saves a `.ugrep` configuration file to the
working directory.  The file contains a strict subset of options that are
deemed reasonably safe with respect to the search results reported.

The `--save-config=FILE` option saves the configuration to the specified `FILE`.
The configuration is written to standard output when `FILE` is a `-`.

🔝 [Back to table of contents](#toc)

<a name="query"/>

### Interactive search with -Q

    -Q[DELAY], --query[=DELAY]
            Query mode: user interface to perform interactive searches.  This
            mode requires an ANSI capable terminal.  An optional DELAY argument
            may be specified to reduce or increase the response time to execute
            searches after the last key press, in increments of 100ms, where
            the default is 5 (0.5s delay).  No whitespace may be given between
            -Q and its argument DELAY.  Initial patterns may be specified with
            -e PATTERN, i.e. a PATTERN argument requires option -e.  Press F1
            or CTRL-Z to view the help screen.  Press F2 or CTRL-Y to invoke a
            command to view or edit the file shown at the top of the screen.
            The command can be specified with option --view, or defaults to
            environment variable PAGER if defined, or EDITOR.  Press Tab and
            Shift-Tab to navigate directories and to select a file to search.
            Press Enter to select lines to output.  Press ALT-l for option -l
            to list files, ALT-n for -n, etc.  Non-option commands include
            ALT-] to increase fuzziness and ALT-} to increase context.  Enables
            --heading.  See also options --confirm and --view.
    --no-confirm
            Do not confirm actions in -Q query mode.  The default is confirm.
    --view[=COMMAND]
            Use COMMAND to view/edit a file in query mode when pressing CTRL-Y.

This option starts a user interface to enter search patterns interactively:
- Press F1 or CTRL-Z to view a help screen and to enable or disable options.
- Press Alt with a key corresponding to a ugrep option letter or digit to
  enable or disable the ugrep option.  For example, pressing Alt-c enables
  option `-c` to count matches.  Pressing Alt-c again disables `-c`.  Options
  can be toggled with the Alt key while searching or when viewing the help
  screen.  If Alt/Meta keys are not supported (e.g. X11 xterm), then press
  CTRL-O followed by the key corresponding to the option.
- Press Alt-g to enter or edit option `-g` file and directory matching globs, a
  comma-separated list of gitignore-style glob patterns.  Press ESC to return
  control to the query pattern prompt (the globs are saved).  When a glob is
  preceded by a `!` or a `^`, skips files whose name matches the glob When a
  glob contains a `/`, full pathnames are matched.  Otherwise basenames are
  matched.  When a glob ends with a `/`, directories are matched.
- The query UI prompt switches between `Q>` (normal), `F>` (fixed strings),
  `G>` (basic regex), `P>` (Perl matching), and `Z>` (fuzzy matching).
  When the `--glob=` prompt is shown, a comma-separated list of gitignore-style
  glob patterns may be entered.  Press ESC returns control to the pattern
  prompt.
- Press Enter to switch to selection mode to select lines to output when ugrep
  exits.  Normally, ugrep in query mode does not output any results unless
  results are selected.  While in selection mode, select or deselect lines with
  Enter or Del, or press A to select all results.
- The file listed or shown at the top of the screen, or beneath the cursor in
  selection mode, is edited by pressing F2 or CTRL-Y.  A file viewer or editor
  may be specified with `--view=COMMAND`.  Otherwise, the `PAGER` or `EDITOR`
  environment variables are used to invoke the command with CTRL-Y.  Filenames
  must be enabled and visible in the output to use this feature.
- Press TAB to chdir one level down into the directory of the file listed
  or viewed at the top of the screen.  If no directory exists, the file itself
  is selected to search.  Press Shift-TAB to go back up one level.
- Press CTRL-T to toggle colors on or off.  Normally ugrep in query mode uses
  colors and other markup to highlight the results.  When colors are turned
  off, selected results are also not colored in the output produced by ugrep
  when ugrep exits.  When colors are turned on (the default), selected results
  are colored depending on the `--color` option.
- The query engine is optimized to limit system load by performing on-demand
  searches to produce results only for the visible parts shown in the
  interface.  That is, results are shown on demand, when scrolling down and
  when exiting when all results are selected.  When the search pattern is
  modified, the previous search query is cancelled when incomplete.  This
  effectively limits the load on the system to maintain a high degree of
  responsiveness of the query engine to user input.  Because the search results
  are produced on demand, occasionally you may notice a flashing "Searching..."
  message when searching files on slower systems.
- To display results faster, specify a low `DELAY` value such as 1.  However,
  lower values may increase system load as a result of repeatedly initiating
  and cancelling searches by each key pressed.
- To avoid long pathnames to obscure the view, `--heading` is enabled by
  default.  Press Alt-+ to switch headings off.

Query UI key mapping:

key(s)                  | function
----------------------- | -------------------------------------------------
`Alt-key`               | toggle ugrep command-line option corresponding to `key`
`Alt-/`xxxx`/`          | insert Unicode hex code point U+xxxx
`Esc` `Ctrl-[` `Ctrl-C` | go back or exit
`Ctrl-Q`                | quick exit and output the results selected in selection mode
`Tab`                   | chdir to the directory of the file shown at the top of the screen or select file
`Shift-Tab`             | chdir one level up or deselect file
`Enter`                 | enter selection mode and toggle selected lines to output on exit
`Up` `Ctrl-P`           | move up
`Down` `Ctrl-N`         | move down
`Left` `Ctrl-B`         | move left
`Right` `Ctrl-F`        | move right
`PgUp` `Ctrl-G`         | move display up by a page
`PgDn` `Ctrl-D`         | move display down by a page
`Alt-Up`                | move display up by 1/2 page (MacOS `Shift-Up`)
`Alt-Down`              | move display down by 1/2 page (MacOS `Shift-Down`)
`Alt-Left`              | move display left by 1/2 page (MacOS `Shift-Left`)
`Alt-Right`             | move display right by 1/2 page (MacOS `Shift-Right`)
`Home` `Ctrl-A`         | move cursor to the begin of line
`End` `Ctrl-E`          | move cursor to the end of line
`Ctrl-K`                | delete after cursor
`Ctrl-L`                | refresh screen
`Ctrl-O`+`key`          | toggle ugrep command-line option corresponding to `key`, same as `Alt-key`
`Ctrl-R` `F4`           | jump to bookmark
`Ctrl-S`                | scroll to the next file
`Ctrl-T`                | toggle colors on/off
`Ctrl-U`                | delete before cursor
`Ctrl-V`                | verbatim character
`Ctrl-W`                | scroll back one file
`Ctrl-X` `F3`           | set bookmark
`Ctrl-Y` `F2`           | edit file shown at the top of the screen or under the cursor
`Ctrl-Z` `F1`           | view help and options
`Ctrl-^`                | chdir back to the starting working directory
`Ctrl-\`                | terminate process

To interactively search the files in the working directory and below:

    ugrep -Q

Same, but restricted to C++ files only and ignoring `.gitignore` files:

    ugrep -Q -tc++ --ignore-files

To interactively search all makefiles in the working directory and below:

    ugrep -Q -g 'Makefile*' -g 'makefile*'

Same, but for up to 2 directory levels (working and one subdirectory level):

    ugrep -Q -2 -g 'Makefile*' -g 'makefile*'

To interactively view the contents of `main.cpp` and search it, where `-y`
shows any nonmatching lines as context:

    ugrep -Q -y main.cpp

To interactively search `main.cpp`, starting with the search pattern `TODO` and
a match context of 5 lines (context can be interactively enabled and disabled,
this also overrides the default context size of 2 lines):

    ugrep -Q -C5 -e TODO main.cpp

To view and search the contents of an archive (e.g. zip, tarball):

    ugrep -Q -z archive.tar.gz

To interactively select files from `project.zip` to decompress with `unzip`,
using ugrep query selection mode (press Enter to select lines):

    unzip project.zip `zipinfo -1 project.zip | ugrep -Q`

🔝 [Back to table of contents](#toc)

<a name="recursion"/>

### Recursively list matching files with -l, -R, -r, --depth, -g, -O, and -t

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
            searched in the same order as specified.  Note that when no FILE
            arguments are specified and input is read from a terminal,
            recursive searches are performed as if -R is specified.
    -r, --recursive
            Recursively read all files under each directory, following symbolic
            links only if they are on the command line.  When -J1 is specified,
            files are searched in the same order as specified.
    --depth=[MIN,][MAX], -1, -2 ... -9, --10, --11 ...
            Restrict recursive searches from MIN to MAX directory levels deep,
            where -1 (--depth=1) searches the specified path without recursing
            into subdirectories.  Note that -3 -5, -3-5, or -35 searches 3 to 5
            levels deep.  Enables -R if -R or -r is not specified.
    -g GLOBS, --glob=GLOBS
            Search only files whose name matches the specified comma-separated
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.
            When a `glob' is preceded by a `!' or a `^', skip files whose name
            matches `glob', same as --exclude='glob'.  When `glob' contains a
            `/', full pathnames are matched.  Otherwise basenames are matched.
            When `glob' ends with a `/', directories are matched, same as
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'
            matches the working directory.  This option may be repeated and may
            be combined with options -M, -O and -t to expand the recursive
            search.
    -O EXTENSIONS, --file-extension=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -g, -M and -t to expand the recursive search.
    -t TYPES, --file-type=TYPES
            Search only files associated with TYPES, a comma-separated list of
            file types.  Each file type corresponds to a set of filename
            extensions passed to option -O.  For capitalized file types, the
            search is expanded to include files with matching file signature
            magic bytes, as if passed to option -M.  When a type is preceded
            by a `!' or a `^', excludes files of the specified type.  This
            option may be repeated.
    --stats
            Output statistics on the number of files and directories searched,
            and the inclusion and exclusion constraints applied.

If no FILE arguments are specified and input is read from a terminal, recursive
searches are performed as if `-R` is specified.  To force reading from standard
input, specify `-` as the FILE argument.

To recursively list all non-empty files in the working directory, following
symbolic links (note that `-R` is redundant as no FILE arguments are given):

    ugrep -R -l ''

To list all non-empty files in the working directory but not deeper (since a
FILE argument is given, in this case `.` for the working directory):

    ugrep -l '' .

To list all non-empty files in directory `mydir` but not deeper (since a FILE
argument is given):

    ugrep -l '' mydir

To list all non-empty files in directory `mydir` and deeper:

    ugrep -R -l '' mydir

To recursively list all non-empty files on the path specified, while visiting
subdirectories only, i.e. directories `mydir/` and subdirectories at one
level deeper `mydir/*/` are visited (note that `-2 -l` can be abbreviated to
`-l2`):

    ugrep -2 -l '' mydir

To recursively list all non-empty files in directory `mydir`, not following any
symbolic links (except when on the command line such as `mydir`):

    ugrep -rl '' mydir

To recursively list all `Makefile` matching the text `CPP`:

    ugrep -l -gMakefile 'CPP'

To recursively list all `Makefile.*` matching `bin_PROGRAMS`:

    ugrep -l -g'Makefile.*' 'bin_PROGRAMS'

To recursively list all non-empty files with extension .sh, with `-Osh`:

    ugrep -l -Osh ''

To recursively list all shell scripts based on extensions and shebangs with
`-tShell`:

    ugrep -l -tShell ''

To recursively list all shell scripts based on extensions only with `-tshell`:

    ugrep -l -tshell ''

🔝 [Back to table of contents](#toc)

<a name="bool"/>

### Boolean search patterns with --bool (-%), --and, --not

    --bool, -%
            Specifies Boolean search patterns.  A Boolean search pattern is
            composed of `AND', `OR', `NOT' operators and grouping with `(' `)'.
            Spacing between subpatterns is the same as `AND', `|' is the same
            as `OR', and a `-' is the same as `NOT'.  The `OR' operator binds
            more tightly than `AND'.  For example, --bool 'A|B C|D' matches
            lines with (`A' or `B') and (`C' or `D'), --bool 'A -B' matches
            lines with `A' and not `B'.  Operators `AND', `OR', `NOT' require
            proper spacing.  For example, --bool 'A OR B AND C OR D' matches
            lines with (`A' or `B') and (`C' or `D'), --bool 'A AND NOT B'
            matches lines with `A' without `B'.  Quoted subpatterns are matched
            literally as strings.  For example, --bool 'A "AND"|"OR"' matches
            lines with `A' and also either `AND' or `OR'.  Parenthesis are used
            for grouping.  For example, --bool '(A B)|C' matches lines with `A'
            and `B', or lines with `C'.  Note that all subpatterns in a Boolean
            search pattern are regular expressions, unless option -F is used.
            Options -E, -F, -G, -P, and -Z can be combined with --bool to match
            subpatterns as strings or regular expressions (-E is the default.)
            This option does not apply to -f FILE patterns.  Option --stats
            displays the search patterns applied.  See also options --and,
            --andnot, and --not.
    --and [[-e] PATTERN] ... -e PATTERN
            Specify additional patterns to match.  Patterns must be specified
            with -e.  Each -e PATTERN following this option is considered an
            alternative pattern to match, i.e. each -e is interpreted as an OR
            pattern.  For example, -e A -e B --and -e C -e D matches lines with
            (`A' or `B') and (`C' or `D').  Note that multiple -e PATTERN are
            alternations that bind more tightly together than --and.  Option
            --stats displays the search patterns applied.  See also options
            --not, --andnot, and --bool.
    --andnot [[-e] PATTERN] ...
            Combines --and --not.  See also options --and, --not, and --bool.
    --not [-e] PATTERN
            Specifies that PATTERN should not match.  Note that -e A --not -e B
            matches lines with `A' or lines without a `B'.  To match lines with
            `A' that have no `B', specify -e A --andnot -e B.  Option --stats
            displays the search patterns applied.  See also options --and,
            --andnot, and --bool.
    --stats
            Output statistics on the number of files and directories searched,
            and the inclusion and exclusion constraints applied.

Note that the `--and`, `--not`, and `--andnot` options require `-e PATTERN`.

The `--bool` option makes all patterns Boolean expressions supporting the
following operations:

operator | alternative | result
-------- | ----------- | -------
`x y`    | `x AND y`   | matches lines with both `x` and `y`
`x\|y`   | `x OR y`    | matches lines with `x` or `y`
`-x`     | `NOT x`     | inverted match, i.e. matches if `x` does not match
`( )`    |             | Boolean expression grouping
`"x"`    |             | match `x` literally and exactly as specified (using the standard regex escapes `\Q` and `\E` for quoting)

- `x` and `y` are subpatterns that do not start with the special symbols `|`,
  `-`, and `(` (use quotes or a `\` escape to match these);

- `|` and `OR` are the same and take precedence over `AND`, which means that
  `x y|z` == `x (y|z)` for example;

- `-` and `NOT` are the same and take precedence over `OR`, which means that
  `-x|y` == `(-x)|y` for example.

The `--stats` option displays the Boolean search query in human-readable form
converted to CNF (Conjunctive Normal Form), after the search is completed.
To show the CNF without a search, read from standard input terminated by an
EOF, like `echo | ugrep --bool '...' --stats`.

Subpatterns are color-highlighted in the output, except those negated with
`NOT` (a `NOT` subpattern may still show up in a matching line when using an
OR-NOT pattern like `x|-y`).  Note that subpatterns may overlap.  In that
case only the first matching subpattern is color-highlighted.

Multiple lines may be matched when subpatterns match newlines.  There is one
exception however: subpatterns ending with `(?=X)` lookaheads may not match
when `X` spans multiple lines.

Empty patterns match any line (grep standard).  Therefore, `--bool 'x|""|y'`
matches everything and `x` and `y` are not color-highlighted.  Option `-y`
should be used to show every line as context, for example `-y 'x|y'`.

Fzf-like interactive querying (Boolean search with fixed strings with fuzzy
matching to allow e.g. up to 4 extra characters matched with `-Z+4` in words
with `-w`), press TAB and ALT-y to view a file with matches.  Press SHIFT-TAB
and ALT-l to go back to the list of matching files:

    ugrep -Q1 --bool -l -w -F -Z+4 --sort=best

To find lines containing both `hot` and `dog` in `myfile.txt`:

    ugrep --bool 'hot dog' myfile.txt
    ugrep -e hot --and dog myfile.txt

To find lines containing `place` and then also `hotdog` or `taco` (or both) in
`myfile.txt`:

    ugrep --bool 'hotdog|taco place' myfile.txt
    ugrep -e hotdog -e taco --and place myfile.txt

Same, but exclude lines matching `diner`:

    ugrep --bool 'hotdog|taco place -diner' myfile.txt
    ugrep -e hotdog -e taco --and place --andnot diner myfile.txt

To find lines with `diner` or lines that match both `fast` and `food` but not `bad` in `myfile.txt`:

    ugrep --bool 'diner|(fast food -bad)' myfile.txt

To find lines with `fast food` (exactly) or lines with `diner` but not `bad` or `old` in `myfile.txt`:

    ugrep --bool '"fast food"|diner -bad -old' myfile.txt

Same, but using a different Boolean expression that has the same meaning:

    ugrep --bool '"fast food"|diner -(bad|old)' myfile.txt

To find lines with `diner` implying `good` in `myfile.txt` (that is, show lines
with `good` without `diner` and show lines with `diner` but only those with
`good`, which is logically implied!):

    ugrep --bool 'good|-diner' myfile.txt
    ugrep -e good --not diner myfile.txt

To find lines with `foo` and `-bar` and `"baz"` in `myfile.txt` (not that `-`
and `"` should be matched using `\` escapes and with `--and -e -bar`):

    ugrep --bool 'foo \-bar \"baz\"' myfile.txt
    ugrep -e foo --and -e -bar --and '"baz"' myfile.txt

To search `myfile.cpp` for lines with `TODO` or `FIXME` but not both on the
same line, like XOR:

    ugrep --bool 'TODO|FIXME -(TODO FIXME)' myfile.cpp
    ugrep -e TODO -e FIXME --and --not TODO --not FIXME myfile.cpp

🔝 [Back to table of contents](#toc)

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
            Read newline-separated patterns from FILE.  White space in patterns
            is significant.  Empty lines in FILE are ignored.  If FILE does not
            exist, the GREP_PATH environment variable is used as path to FILE.
            If that fails, looks for FILE in /usr/local/share/ugrep/pattern.
            When FILE is a `-', standard input is read.  This option may be
            repeated.
    -L, --files-without-match
            Only the names of files not containing selected lines are written
            to standard output.  Pathnames are listed once per file searched.
            If the standard input is searched, the string ``(standard input)''
            is written.
    -N PATTERN, --neg-regexp=PATTERN
            Specify a negative PATTERN used during the search of the input:
            an input line is selected only if it matches any of the specified
            patterns unless a subpattern of PATTERN.  Same as -e (?^PATTERN).
            Negative PATTERN matches are essentially removed before any other
            patterns are matched.  Note that longer patterns take precedence
            over shorter patterns.  This option may be repeated.
    -v, --invert-match
            Selected lines are those not matching any of the specified
            patterns.
    -w, --word-regexp
            The PATTERN is searched for as a word, such that the matching text
            is preceded by a non-word character and is followed by a non-word
            character.  Word characters are letters, digits, and the
            underscore.  With option -P, word characters are Unicode letters,
            digits, and underscore.  This option has no effect if -x is also
            specified.  If a PATTERN is specified, or -e PATTERN or -N PATTERN,
            then this option has no effect on -f FILE patterns to allow -f FILE
            patterns to narrow or widen the scope of the PATTERN search.
    -x, --line-regexp
            Select only those matches that exactly match the whole line, as if
            the patterns are surrounded by ^ and $.  If a PATTERN is specified,
            or -e PATTERN or -N PATTERN, then this option does not apply to
            -f FILE patterns to allow -f FILE patterns to narrow or widen the
            scope of the PATTERN search.

To display lines in file `myfile.sh` but not lines matching `^[ \t]*#`:

    ugrep -v '^[ \t]*#' myfile.sh

To search `myfile.cpp` for lines with `FIXME` and `urgent`, but not `Scotty`:

    ugrep FIXME myfile.cpp | ugrep urgent | ugrep -v Scotty

To search for decimals using pattern `\d+` that do not start with `0` using
negative pattern `0\d+` and excluding `555`:

    ugrep '\d+' -N '0\d+' -N 555 myfile.cpp

To search for words starting with `disp` without matching `display` in file
`myfile.py` by using a "negative pattern" `-N '/<display\>'` where `-N`
specifies an additional negative pattern to skip matches:

    ugrep '\<disp' -N '\<display\>' myfile.py

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

🔝 [Back to table of contents](#toc)

<a name="encoding"/>

### Search non-Unicode files with --encoding

    --encoding=ENCODING
            The input file encoding.

ASCII, UTF-8, UTF-16, and UTF-32 files do not require this option, assuming
that UTF-16 and UTF-32 files start with a UTF BOM
([byte order mark](https://en.wikipedia.org/wiki/Byte_order_mark)) as usual.
Other file encodings require option `--encoding=ENCODING`:

encoding               | parameter
---------------------- | --------------
ASCII                  | *n/a*
UTF-8                  | *n/a*
UTF-16 with BOM        | *n/a*
UTF-32 with BOM        | *n/a*
UTF-16 BE w/o BOM      | `UTF-16` or `UTF-16BE`
UTF-16 LE w/o BOM      | `UTF-16LE`
UTF-32 w/o BOM         | `UTF-32` or `UTF-32BE`
UTF-32 w/o BOM         | `UTF-32LE`
Latin-1                | `LATIN1` or `ISO-8859-1`
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

To recursively list all files that are ASCII (i.e. 7-bit):

    ugrep -RL '[^[:ascii:]]'

To recursively list all files that are non-ASCII, i.e. UTF-8, UTF-16, and
UTF-32 files with non-ASCII Unicode characters (U+0080 and up):

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

To search utf16lorem.txt when this file has no UTF-16 BOM, using `--encoding`:

    ugrep --encoding=UTF-16 -iw 'lorem' utf16lorem.txt

To search file `spanish-iso.txt` encoded in ISO-8859-1:

    ugrep --encoding=ISO-8859-1 -w 'año' spanish-iso.txt

🔝 [Back to table of contents](#toc)

<a name="multiline"/>

### Matching multiple lines of text

    -o, --only-matching
            Print only the matching part of lines.  When multiple lines match,
            the line numbers with option -n are displayed using `|' as the
            field separator for each additional line matched by the pattern.
            If -u is specified, ungroups multiple matches on the same line.
            This option cannot be combined with options -A, -B, -C, -v, and -y.

Multiple lines may be matched by patterns that match newline `\n` characters,
unless one or more context options `-A`, `-B`, `-C`, `-y` is used, or `-v` that
apply to lines.  Use option `-o` to output the match only, not the full
lines(s) that match.

To match C/C++ `/*...*/` multi-line comments:

    ugrep '/\*([^*]|\n|(\*+([^*/]|\n)))*\*+\/' myfile.cpp

To match C/C++ comments using the predefined `c/comments` patterns with
`-f c/comments`, restricted to the matching part only with option `-o`:

    ugrep -of c/comments myfile.cpp

Same as `sed -n '/begin/,/end/p'`: to match all lines between a line containing
`begin` and the first line after that containing `end`, using lazy repetition:

    ugrep -o '.*begin(.|\n)*?end.*' myfile.txt

🔝 [Back to table of contents](#toc)

<a name="context"/>

### Displaying match context with -A, -B, -C, and -y

    -A NUM, --after-context=NUM
            Print NUM lines of trailing context after matching lines.  Places
            a --group-separator between contiguous groups of matches.  See also
            options -B, -C, and -y.
    -B NUM, --before-context=NUM
            Print NUM lines of leading context before matching lines.  Places
            a --group-separator between contiguous groups of matches.  See also
            options -A, -C, and -y.
    -C NUM, --context=NUM
            Print NUM lines of leading and trailing context surrounding each
            match.  Places a --group-separator between contiguous groups of
            matches. See also options -A, -B, and -y.
    -y, --any-line
            Any matching or non-matching line is output.  Non-matching lines
            are output with the `-' separator as context of the matching lines.
            See also options -A, -B, and -C.

To display two lines of context before and after a matching line:

    ugrep -C2 'FIXME' myfile.cpp

To show three lines of context after a matched line:

    ugrep -A3 'FIXME.*' myfile.cpp:

To display one line of context before each matching line with a C function
definition (C names are non-Unicode):

    ugrep -B1 -f c/functions myfile.c

To display one line of context before each matching line with a C++ function
definition (C++ names may be Unicode):

    ugrep -B1 -f c++/functions myfile.cpp

To display any non-matching lines as context for matching lines with `-y`:

    ugrep -y -f c++/functions myfile.cpp

To display a hexdump of a matching line with one line of hexdump context:

    ugrep -C1 -UX '\xaa\xbb\xcc' a.out

Context within a line is displayed by simply adjusting the pattern and using
option `-o`, for example to show the word (when present) before and after a
match of `pattern` (`\w+` matches a word and `\h+` matches spacing), where `-U`
matches ASCII words instead of Unicode:

    ugrep -o -U '(\w+\h+)?pattern(\h+\w+)?' myfile.cpp

Same, but with line numbers (`-n`), column numbers (`-k`), tab spacing (`-T`)
for all matches separately (`-u`), and showing up to 8 characters of context
instead of a single word:

    ugrep -onkTg -U '.{0,8}pattern.{0,8}' myfile.cpp | ugrep 'pattern'

🔝 [Back to table of contents](#toc)

<a name="source"/>

### Searching source code using -f, -g, -O, and -t

    -f FILE, --file=FILE
            Read newline-separated patterns from FILE.  White space in patterns
            is significant.  Empty lines in FILE are ignored.  If FILE does not
            exist, the GREP_PATH environment variable is used as path to FILE.
            If that fails, looks for FILE in /usr/local/share/ugrep/pattern.
            When FILE is a `-', standard input is read.  This option may be
            repeated.
    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE that
            is encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories that are explicitly specified as command line
            arguments are never ignored.  This option may be repeated.
    -g GLOBS, --glob=GLOBS
            Search only files whose name matches the specified comma-separated
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.
            When a `glob' is preceded by a `!' or a `^', skip files whose name
            matches `glob', same as --exclude='glob'.  When `glob' contains a
            `/', full pathnames are matched.  Otherwise basenames are matched.
            When `glob' ends with a `/', directories are matched, same as
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'
            matches the working directory.  This option may be repeated and may
            be combined with options -M, -O and -t to expand the recursive
            search.
    -O EXTENSIONS, --file-extension=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -g, -M and -t to expand the recursive search.
    -t TYPES, --file-type=TYPES
            Search only files associated with TYPES, a comma-separated list of
            file types.  Each file type corresponds to a set of filename
            extensions passed to option -O.  For capitalized file types, the
            search is expanded to include files with matching file signature
            magic bytes, as if passed to option -M.  When a type is preceded
            by a `!' or a `^', excludes files of the specified type.  This
            option may be repeated.
    --stats
            Output statistics on the number of files and directories searched,
            and the inclusion and exclusion constraints applied.

The file types are listed with `ugrep -tlist`.  The list is based on
established filename extensions and "magic bytes".  If you have a file type
that is not listed, use options `-O` and/or `-M`.  You may want to define an
alias, e.g. `alias ugft='ugrep -Oft'` as a shorthand to search files with
filename suffix `.ft`.

To recursively display function definitions in C/C++ files (`.h`, `.hpp`, `.c`,
`.cpp` etc.) with line numbers with `-tc++`, `-o`, `-n`, and `-f c++/functions`:

    ugrep -on -tc++ -f c++/functions

To recursively display function definitions in `.c` and `.cpp` files with line
numbers with `-Oc,cpp`, `-o`, `-n`, and `-f c++/functions`:

    ugrep -on -Oc,cpp -f c++/functions

To recursively list all shell files with `-tShell` to match filename extensions
and files with shell shebangs, except files with suffix `.sh`:

    ugrep -l -tShell -O^sh ''

To recursively list all non-shell files with `-t^Shell`:

    ugrep -l -t^Shell ''

To recursively list all shell files with shell shebangs that have no shell
filename extensions:

    ugrep -l -tShell -t^shell ''

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

🔝 [Back to table of contents](#toc)

<a name="archives"/>

### Searching compressed files and archives with -z

    -z, --decompress
            Decompress files to search, when compressed.  Archives (.cpio,
            .pax, .tar and .zip) and compressed archives (e.g. .taz, .tgz,
            .tpz, .tbz, .tbz2, .tb2, .tz2, .tlz, and .txz) are searched and
            matching pathnames of files in archives are output in braces.  If
            -g, -O, -M, or -t is specified, searches files within archives
            whose name matches globs, matches file name extensions, matches
            file signature magic bytes, or matches file types, respectively.
            Supported compression formats: gzip (.gz), compress (.Z), zip,
            bzip2 (requires suffix .bz, .bz2, .bzip2, .tbz, .tbz2, .tb2, .tz2),
            lzma and xz (requires suffix .lzma, .tlz, .xz, .txz),
            lz4 (requires suffix .lz4),
            zstd (requires suffix .zst, .zstd, .tzst).

Compressed files with gzip (`.gz`), compress (`.Z`), bzip2 (`.bz`, `.bz2`,
`.bzip2`), lzma (`.lzma`), xz (`.xz`), lz4 (`.lz4`) and zstd (`.zst`, `.zstd`)
are searched with option `-z`.  This option does not require files to be
compressed.  Uncompressed files are searched also.

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
`.bz2`, or `.bzip2` for bzip2, `.lzma` for lzma, `.xz` for xz, `.lz4` for lz4
and `.zst` or `.zstd` for zstd.  Also the compressed tar archive shorthands
`.taz`, `.tgz` and `.tpz` for gzip, `.tbz`, `.tbz2`, `.tb2`, and `.tz2` for
bzip2, `.tlz` for lzma, `.txz` for xz, and `.tzst` for zstd are recognized.  To
search these formats with ugrep from standard input, use option
`--label='stdin.bz2'` for bzip2, `--label='stdin.lzma'` for lzma,
`--label='stdin.xz'` for xz, `--label='stdin.lz4` for lz4 and
`--label='stdin.zst` for zstd.  The name `stdin` is arbitrary and may be
omitted:

format    | filename suffix         | tar/pax archive short suffix    | suffix required? | ugrep from stdin | lib required |
--------- | ----------------------- | ------------------------------- | ---------------- | ---------------- | ------------ |
gzip      | `.gz`                   | `.taz`, `.tgz`, `.tpz`          | no               | automatic        | libz         |
compress  | `.Z`                    | `.taZ`, `.tZ`                   | no               | automatic        | *built-in*   |
zip       | `.zip`, `.zipx`, `.ZIP` |                                 | no               | automatic        | libz         |
bzip2     | `.bz`, `.bz2`, `.bzip2` | `.tb2`, `.tbz`, `.tbz2`, `.tz2` | yes              | `--label=.bz2`   | libbz2       |
lzma      | `.lzma`                 | `.tlz`                          | yes              | `--label=.lzma`  | liblzma      |
xz        | `.xz`                   | `.txz`                          | yes              | `--label=.xz`    | liblzma      |
lz4       | `.lz4`                  |                                 | yes              | `--label=.lz4`   | liblz4       |
zstd      | `.zst`, `.zstd`         | `.tzst`                         | yes              | `--label=.zst`   | libzstd      |

The gzip, bzip2, xz, lz4 and zstd formats support concatenated compressed
files.  Concatenated compressed files are searched as one file.

Supported zip compression methods are stored (0), deflate (8), bzip2 (12), lzma
(14), xz (95) and zstd (93).  The bzip2, lzma, xz and zstd methods require
ugrep to be compiled with the corresponding compression libraries.

Archives compressed and stored within zip archives are also searched:  all
cpio, pax, and tar files in zip archives are automatically recognized and
searched.  However, compressed files stored within archives are not recognized,
e.g. zip files stored within zip files or stored within tar files are not
searched.  Any such compressed files are searched as if they are binary files
without decompressing them.

Searching encrypted zip archives is not supported (perhaps in future releases,
depending on requests for enhancements).

When option `-z` is used with options `-g`, `-O`, `-M`, or `-t`, archives and
compressed and uncompressed files that match the filename selection criteria
(glob, extension, magic bytes, or file type) are searched only.  For example,
`ugrep -r -z -tc++` searches C++ files such as `main.cpp` and zip and tar
archives that contain C++ files such as `main.cpp`.  Also included in the
search are compressed C++ files such as `main.cpp.gz` and `main.cpp.xz` when
present.  Also any cpio, pax, tar, and zip archives when present are searched
for C++ files that they contain, such as `main.cpp`.  Use option `--stats` to
see a list of the glob patterns applied to filter file pathnames in the
recursive search and when searching archive contents.

When option `-z` is used with options `-g`, `-O`, `-M`, or `-t` to search cpio,
jar, pax, tar, and zip archives, archived files that match the filename selection
criteria are searched only.

Option `-z` uses thread task parallelism to speed up searching larger files by
running the decompressor concurrently with a search of the decompressed stream.

To recursively search C++ files including compressed files for the word
`my_function`, while skipping C and C++ comments:

    ugrep -z -r -tc++ -Fw my_function -f cpp/zap_comments

To search bzip2, lzma, xz, lz4 and zstd compressed data on standard input,
option `--label` may be used to specify the extension corresponding to the
compression format to force decompression when the bzip2 extension is not
available to ugrep, for example:

    cat myfile.bz2 | ugrep -z --label='stdin.bz2' 'xyz'

To search file `main.cpp` in `project.zip` for `TODO` and `FIXME` lines:

    ugrep -z -g main.cpp -w -e 'TODO' -e 'FIXME' project.zip

To search tarball `project.tar.gz` for C++ files with `TODO` and `FIXME` lines:

    ugrep -z -tc++ -w -e 'TODO' -e 'FIXME' project.tar.gz

To search files matching the glob `*.txt` in `project.zip` for the word
`license` in any case (note that the `-g` glob argument must be quoted):

    ugrep -z -g '*.txt' -w -i 'license' project.zip

To display and page through all C++ files in tarball `project.tgz`:

    ugrep --pager -z -tc++ '' project.tgz

To list the files matching the gitignore-style glob `/**/projects/project1.*`
in `projects.tgz`, by selecting files containing in the archive the text
`December 12`:

    ugrep -z -l -g '/**/projects/project1.*' -F 'December 12' projects.tgz

To view the META-INF/MANIFEST.MF data in a jar file with `-Ojar` and `-OMF` to
select the jar file and the MF file therein (`-Ojar` is required, otherwise the
jar file will be skipped though we could read it from standard input instead):

    ugrep -z -h -OMF,jar '' my.jar

To extract C++ files that contain `FIXME` from `project.tgz`, we use `-m1`
with `--format="'%z '"` to generate a space-separated list of pathnames of file
located in the archive that match the word `FIXME`:

    tar xzf project.tgz `ugrep -z -l -tc++ --format='%z ' -w FIXME project.tgz`

To perform a depth-first search with `find`, then use `cpio` and `ugrep` to
search the files:

    find . -depth -print | cpio -o | ugrep -z 'xyz'

🔝 [Back to table of contents](#toc)

<a name="magic"/>

### Find files by file signature and shebang "magic bytes" with -M, -O and -t

    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE that
            is encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories that are explicitly specified as command line
            arguments are never ignored.  This option may be repeated.
    -M MAGIC, --file-magic=MAGIC
            Only files matching the signature pattern MAGIC are searched.  The
            signature \"magic bytes\" at the start of a file are compared to
            the MAGIC regex pattern.  When matching, the file will be searched.
            When MAGIC is preceded by a `!' or a `^', skip files with matching
            MAGIC signatures.  This option may be repeated and may be combined
            with options -O and -t to expand the search.  Every file on the
            search path is read, making searches potentially more expensive.
    -O EXTENSIONS, --file-extension=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -g, -M and -t to expand the recursive search.
    -t TYPES, --file-type=TYPES
            Search only files associated with TYPES, a comma-separated list of
            file types.  Each file type corresponds to a set of filename
            extensions passed to option -O.  For capitalized file types, the
            search is expanded to include files with matching file signature
            magic bytes, as if passed to option -M.  When a type is preceded
            by a `!' or a `^', excludes files of the specified type.  This
            option may be repeated.
    -g GLOBS, --glob=GLOBS
            Search only files whose name matches the specified comma-separated
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.
            When a `glob' is preceded by a `!' or a `^', skip files whose name
            matches `glob', same as --exclude='glob'.  When `glob' contains a
            `/', full pathnames are matched.  Otherwise basenames are matched.
            When `glob' ends with a `/', directories are matched, same as
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'
            matches the working directory.  This option may be repeated and may
            be combined with options -M, -O and -t to expand the recursive
            search.
    --stats
            Output statistics on the number of files and directories searched,
            and the inclusion and exclusion constraints applied.

To recursively list all files that start with `#!` shebangs:

    ugrep -l -M'#!' ''

To recursively list all files that start with `#` but not with `#!` shebangs:

    ugrep -l -M'#' -M'^#!' ''

To recursively list all Python files (extension `.py` or a shebang) with
`-tPython`:

    ugrep -l -tPython ''

To recursively list all non-shell files with `-t^Shell`:

    ugrep -l -t^Shell ''

To recursively list Python files (extension `.py` or a shebang) that have
import statements, including hidden files with `-.`:

    ugrep -l. -tPython -f python/imports
 
🔝 [Back to table of contents](#toc)

<a name="fuzzy"/>

### Fuzzy search with -Z

    -Z[MAX], --fuzzy[=MAX]
            Fuzzy mode: report approximate pattern matches within MAX errors.
            By default, MAX is 1: one deletion, insertion or substitution is
            allowed.  When `+' and/or `-' precedes MAX, only insertions and/or
            deletions are allowed.  When `~' precedes MAX, substitution counts
            as one error.  For example, -Z+~3 allows up to three insertions or
            substitutions, but no deletions.  The first character of an
            approximate match always matches the begin of a pattern.  Option
            --sort=best orders matching files by best match.  No whitespace may
            be given between -Z and its argument.

The begin of a pattern always matches the first character of an approximate
match as a practical strategy to prevent many false "randomized" matches for
short patterns.  This also greatly improves search speed.  Make the first
character optional to optionally match it, e.g. `p?attern` or use a dot as
the start of the pattern to match any wide character (but this is slow).

Newlines (`\n`) and NUL (`\0`) characters are never deleted or substituted to
ensure that fuzzy matches do not extend the pattern match beyond the number of
lines specified by the regex pattern.

Option `--sort=best` orders files by best match.  Files with at least one exact
match anywhere in the file are shown first, followed by files with approximate
matches in increasing minimal edit distance order.  That is, ordered by the
minimum error (edit distance) found among all approximate matches per file.

To recursively search for approximate matches of the word `foobar` with `-Z`,
i.e.  approximate matching with one error, e.g. `Foobar`, `foo_bar`, `foo bar`,
`fobar`:

    ugrep -Z 'foobar'

Same, but matching words only with `-w` and ignoring case with `-i`:

    ugrep -Z -wi 'foobar'

Same, but permit up to 2 insertions with `-Z+2`, no deletions/substitutions
(matches up to 2 extra characters, such as `foos bar`), insertions-only offers
the fastest fuzzy matching method:

    ugrep -Z+3 -wi 'foobar'

Same, but sort matches from best (at least one exact match or fewest fuzzy
match errors) to worst:

    ugrep -Z+3 -wi --sort=best 'foobar'

Note that sorting by best match requires two passes over the input files.  In
addition, the effectiveness of concurrent searching is significantly reduced.

Same, but with customized formatting to show the cost of the approximate
matches with format field `%Z`:

    ugrep -Z+3 -wi --format='%F%Z:%O%~' --sort=best 'foobar'

🔝 [Back to table of contents](#toc)

<a name="hidden">

### Search hidden files with -.

    --hidden, -.
            Search hidden files and directories.

To recursively search the working directory, including hidden files and
directories, for the word `login` in shell scripts:

    ugrep -. -tShell 'login'

🔝 [Back to table of contents](#toc)

<a name="filter"/>

### Using filter utilities to search documents with --filter

    --filter=COMMANDS
            Filter files through the specified COMMANDS first before searching.
            COMMANDS is a comma-separated list of `exts:command [option ...]',
            where `exts' is a comma-separated list of filename extensions and
            `command' is a filter utility.  The filter utility should read from
            standard input and write to standard output.  Files matching one of
            `exts' are filtered.  When `exts' is `*', files with non-matching
            extensions are filtered.  One or more `option' separated by spacing
            may be specified, which are passed verbatim to the command.  A `%'
            as `option' expands into the pathname to search.  For example,
            --filter='pdf:pdftotext % -' searches PDF files.  The `%' expands
            into a `-' when searching standard input.  Option --label=.ext may
            be used to specify extension `ext' when searching standard input.
    --filter-magic-label=LABEL:MAGIC
            Associate LABEL with files whose signature "magic bytes" match the
            MAGIC regex pattern.  Only files that have no filename extension
            are labeled, unless +LABEL is specified.  When LABEL matches an
            extension specified in --filter=COMMANDS, the corresponding command
            is invoked.  This option may be repeated.

The `--filter` option associates one or more filter utilities with specific
filename extensions.  A filter utility is selected based on the filename
extension and executed by forking a process:  the utility's standard input
reads the open input file and the utility's standard output is searched.  When
a `%` is specified as an option to the utility, the `%` is expanded to the
pathname of the file to open and read by the utility.

When a specified utility is not found on the system, an error message is
displayed.  When a utility fails to produce output, e.g. when the specified
options for the utility are invalid, the search is silently skipped.

Common filter utilities are `cat` (concat, pass through), `head` (select first
lines or bytes) `tr` (translate), `iconv` and `uconv` (convert), and more
advanced document conversion utilities such as:

- [`pdftotext`](https://pypi.org/project/pdftotext) to convert PDF to text
- [`pandoc`](https://pandoc.org) to convert .docx, .epub, and other document
  formats
- [`soffice`](https://www.libreoffice.org) to convert office documents
- [`csvkit`](https://pypi.org/project/csvkit) to convert spreadsheets
- [`openssl`](https://wiki.openssl.org/index.php/Command_Line_Utilities) to
  convert certificates and key files to text and other formats
- [`exiftool`](http://exiftool.sourceforge.net) to read meta information
  embedded in image and video media formats.

Also decompressors may be used as filter utilities, such as `unzip`, `gunzip`,
`bunzip2`, `unlzma`, and `unxz` that decompress files to standard output when
option `--stdout` is specified.  However, **ugrep** option `-z` is typically
faster to search compressed files.

The `--filter` option may also be used to run a user-defined shell script to
filter files.  For example, to invoke an action depending on the filename
extension of the `%` argument.  Another use case is to pass a file to more than
one filter, which can be accomplished with a shell script containing the line
`tool1 $1; tool2 $1`.  This filters the file argument `$1` with `tool1`
followed by `tool2` to produce combined output to search for pattern matches.
Likewise, we can use a script with the line `tool1 $1 | tool2` to stack two
filters `tool1` and `tool2`.

The `--filter` option may also be used as a predicate to skip certain files
from the search.  As the most basic example, consider the `false` utility that
exits with a nonzero exit code without reading input or producing output.
Therefore, `--filter='swp: false'` skips all `.swp` files from recursive
searches.  The same can be done more efficiently with `-O^swp`.  However,
the `--filter` option could invoke a script that determines if the filename
passed as a `%` argument meets certain constraints.  If the constraint is met
the script copies standard input to standard output with `cat`.  If not, the
script exits.

**Warning:** option `--filter` should not be used with utilities that modify
files.  Otherwise searches may be unpredicatable.  In the worst case files may
be lost, for example when the specified utility replaces or deletes the file
passed to the command with `--filter` option `%`.

To recursively search files including PDF files in the working directory
without recursing into subdirectories (with `-1`), for matches of `drink me`
using the `pdftotext` filter to convert PDF to text without preserving page
breaks:

    ugrep -r -1 --filter='pdf:pdftotext -nopgbrk % -' 'drink me'

To recursively search text files for `eat me` while converting non-printable
characters in .txt and .md files using the `cat -v` filter:

    ugrep -r -ttext --filter='txt,md:cat -v' 'eat me'

The same, but specifying the .txt and .md filters separately:

    ugrep -r -ttext --filter='txt:cat -v, md:cat -v' 'eat me'

To search the first 8K of a text file:

    ugrep --filter='txt:head -c 8192' 'eat me' wonderland.txt

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
any time soon (unless perhaps more people complain.)

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

To recurssively search X509 certificate files for lines with `Not After` (e.g.
to find expired certificates), using `openssl` as a filter:

    ugrep -r 'Not After' -Ocer,der,pem --filter='pem:openssl x509 -text,cer,crt,der:openssl x509 -text -inform der'

Note that `openssl` warning messages are displayed on standard error.  If
a file cannot be converted it is probably in a different format.  This can
be resolved by writing a shell script that executes `openssl` with options
based on the file content.  Then write a script with `ugrep --filter`.

To search PNG files by filename extension with `-tpng` using `exiftool`:

    ugrep -r -i 'copyright' -tpng --filter='*:exiftool %'

Same, but also include files matching PNG "magic bytes" with `-tPng` and
`--filter-magic-label='+png:\x89png\x0d\x0a\x1a\x0a'` to select the `png`
filter:

    ugrep -r -i 'copyright' -tPng --filter='png:exiftool %' --filter-magic-label='+png:\x89png\x0d\x0a\x1a\x0a'

Note that `+png` overrides any filename extension match for `--filter`.
Otherwise, without a `+`, the filename extension, when present, takes priority
over labelled magic patterns to invoke the corresponding filter command.
The `LABEL` used with `--filter-magic-label` and `--filter` has no specific
meaning; any name or string that does not contain a `:` or `,` may be used.

🔝 [Back to table of contents](#toc)

<a name="binary"/>

### Searching and displaying binary files with -U, -W, and -X

    -U, --binary
            Disables Unicode matching for binary file matching, forcing PATTERN
            to match bytes, not Unicode characters.  For example, -U '\xa3'
            matches byte A3 (hex) instead of the Unicode code point U+00A3
            represented by the UTF-8 sequence C2 A3.  See also --dotall.
    -W, --with-hex
            Output binary matches in hexadecimal, leaving text matches alone.
            This option is equivalent to the --binary-files=with-hex option.
    -X, --hex
            Output matches in hexadecimal.  This option is equivalent to the
            --binary-files=hex option.
    --hexdump=[1-8][b][c][h]
            Output matches in 1 to 8 columns of 8 hexadecimal octets.  The
            default is 2 columns or 16 octets per line.  Option `b' removes all
            space breaks, `c' removes the character column, and `h' removes the
            hex spacing.  Enables -X if -W or -X is not specified.
    --dotall
            Dot `.' in regular expressions matches anything, including newline.
            Note that `.*' matches all input and should not be used.

To search a file for ASCII words, displaying text lines as usual while binary
content is shown in hex with `-U` and `-W`:

    ugrep -UW '\w+' myfile

To hexdump an entire file as a match with `-X`:

    ugrep -X '' myfile

To hexdump an entire file with `-X`, displaying line numbers and byte offsets
with `-nb` (here with `-y` to display all line numbers):

    ugrep -Xynb '' myfile

To hexdump lines containing one or more \0 in a (binary) file using a
non-Unicode pattern with `-U` and `-X`:

    ugrep -UX '\x00+' myfile

Same, but hexdump the entire file as context with `-y` (note that this
line-based option does not permit matching patterns with newlines):

    ugrep -UX -y '\x00+' myfile

Same, compacted to 32 bytes per line without the character column:

    ugrep -U --hexdump=4bc -y '\x00+' myfile

To match the binary pattern `A3..A3.` (hex) in a binary file without
Unicode pattern matching (which would otherwise match `\xaf` as a Unicode
character U+00A3 with UTF-8 byte sequence C2 A3) and display the results
in hex with `-X` with pager `less -R`:

    ugrep --pager -o -UX '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

Same, but using option `--dotall` to let `.` match any byte, including
newline that is not matched by dot (the default as required by grep):

    ugrep --dotall --pager -o -UX '\xa3.{2}\xa3.' a.out

To list all files containing a RPM signature, located in the `rpm` directory and
recursively below (see for example
[list of file signatures](https://en.wikipedia.org/wiki/List_of_file_signatures)):

    ugrep -RlU '\A\xed\xab\xee\xdb' rpm

🔝 [Back to table of contents](#toc)

<a name="nobinary">

### Ignore binary files with -I

    -I      Ignore matches in binary files.  This option is equivalent to the
            --binary-files=without-match option.

To recursively search without following symlinks and ignoring binary files:

    ugrep -rl -I 'xyz'

To ignore specific binary files with extensions such as .exe, .bin, .out, .a,
use `--exclude` or `--exclude-from`:

    ugrep -rl --exclude-from=ignore_binaries 'xyz'

where `ignore_binaries` is a file containing a glob on each line to ignore
matching files, e.g.  `*.exe`, `*.bin`, `*.out`, `*.a`.  Because the command is
quite long to type, an alias for this is recommended, for example `ugs` (ugrep
source):

    alias ugs="ugrep --exclude-from=~/ignore_binaries"
    ugs -rl 'xyz'

🔝 [Back to table of contents](#toc)

<a name="ignore"/>

### Ignoring .gitignore-specified files with --ignore-files

    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE that
            is encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories that are explicitly specified as command line
            arguments are never ignored.  This option may be repeated.

Option `--ignore-files` looks for `.gitignore`, or the specified `FILE`, in
recursive searches.  When found, the `.gitignore` file is used to exclude the
files and directories matching the globs in `.gitignore` in the directory tree
rooted at the `.gitignore` location by temporarily overriding the `--exclude`
and `--exclude-dir` globs, i.e. the `.gitignore` exclusions are applied
precisely and exclusively.  Use `--stats` to show the selection criteria
applied to the search results and the locations of each `FILE` found.  To avoid
confusion, files and directories specified as command-line arguments to
**ugrep** are never ignored.

Note that exclude glob patterns take priority over include glob patterns when
specified with command line options.  By contrast, negated glob patterns
specified with `!` in `--ignore-files` files take priority.  This effectively
overrides the exclusions and resolves conflicts in favor of listing matching
files that are explicitly specified as exceptions and should be included in the
search.

See also [Using gitignore-style globs to select directories and files to search](#globs).

To recursively search without following symlinks, while ignoring files and
directories ignored by .gitignore (when present), use option `--ignore-files`:

    ugrep -rl --ignore-files 'xyz'

Same, but includes hidden files with `-.` rather than ignoring them:

    ugrep -rl. --ignore-files 'xyz'

To recursively list all files that are not ignored by .gitignore (when present)
with `--ignore-files` (note that `-R` is redundant, since no FILE arguments are
given):

    ugrep -Rl '' --ignore-files

Same, but list shell scripts that are not ignored by .gitignore, when present:

    ugrep -Rl -tShell '' --ignore-files

To recursively list all files that are not ignored by .gitignore and are also
not excluded by `.git/info/exclude`:

    ugrep -Rl '' --ignore-files --exclude-from=.git/info/exclude

Same, but by creating a symlink to `.git/info/exclude` to make the exclusions
implicit:

    ln -s .git/info/exclude .ignore
    ugrep -Rl '' --ignore-files --ignore-files=.ignore

🔝 [Back to table of contents](#toc)

<a name="globs"/>

### Using gitignore-style globs to select directories and files to search

    -g GLOBS, --glob=GLOBS
            Search only files whose name matches the specified comma-separated
            list of GLOBS, same as --include='glob' for each `glob' in GLOBS.
            When a `glob' is preceded by a `!' or a `^', skip files whose name
            matches `glob', same as --exclude='glob'.  When `glob' contains a
            `/', full pathnames are matched.  Otherwise basenames are matched.
            When `glob' ends with a `/', directories are matched, same as
            --include-dir='glob' and --exclude-dir='glob'.  A leading `/'
            matches the working directory.  This option may be repeated and may
            be combined with options -M, -O and -t to expand the recursive
            search.
    --exclude=GLOB
            Skip files whose name matches GLOB using wildcard matching, same as
            -g ^GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and \\ to
            quote a wildcard or backslash character literally.  When GLOB
            contains a `/', full pathnames are matched.  Otherwise basenames
            are matched.  When GLOB ends with a `/', directories are excluded
            as if --exclude-dir is specified.  Otherwise files are excluded.
            Note that --exclude patterns take priority over --include patterns.
            GLOB should be quoted to prevent shell globbing.  This option may
            be repeated.
    --exclude-dir=GLOB
            Exclude directories whose name matches GLOB from recursive
            searches, same as -g ^GLOB/.  GLOB can use **, *, ?, and [...] as
            wildcards, and \\ to quote a wildcard or backslash character
            literally.  When GLOB contains a `/', full pathnames are matched.
            Otherwise basenames are matched.  Note that --exclude-dir patterns
            take priority over --include-dir patterns.  GLOB should be quoted
            to prevent shell globbing.  This option may be repeated.
    --exclude-from=FILE
            Read the globs from FILE and skip files and directories whose name
            matches one or more globs (as if specified by --exclude and
            --exclude-dir).  Lines starting with a `#' and empty lines in FILE
            are ignored.  When FILE is a `-', standard input is read.  This
            option may be repeated.
    --ignore-files[=FILE]
            Ignore files and directories matching the globs in each FILE that
            is encountered in recursive searches.  The default FILE is
            `.gitignore'.  Matching files and directories located in the
            directory tree rooted at a FILE's location are ignored by
            temporarily overriding the --exclude and --exclude-dir globs.
            Files and directories that are explicitly specified as command line
            arguments are never ignored.  This option may be repeated.
    --include=GLOB
            Search only files whose name matches GLOB using wildcard matching,
            same as -g GLOB.  GLOB can use **, *, ?, and [...] as wildcards,
            and \\ to quote a wildcard or backslash character literally.  When
            GLOB contains a `/', full pathnames are matched.  Otherwise
            basenames are matched.  When GLOB ends with a `/', directories are
            included as if --include-dir is specified.  Otherwise files are
            included.  Note that --exclude patterns take priority over
            --include patterns.  GLOB should be quoted to prevent shell
            globbing.  This option may be repeated.
    --include-dir=GLOB
            Only directories whose name matches GLOB are included in recursive
            searches, same as -g GLOB/.  GLOB can use **, *, ?, and [...] as
            wildcards, and \\ to quote a wildcard or backslash character
            literally.  When GLOB contains a `/', full pathnames are matched.
            Otherwise basenames are matched.  Note that --exclude-dir patterns
            take priority over --include-dir patterns.  GLOB should be quoted
            to prevent shell globbing.  This option may be repeated.
    --include-from=FILE
            Read the globs from FILE and search only files and directories
            whose name matches one or more globs (as if specified by --include
            and --include-dir).  Lines starting with a `#' and empty lines in
            FILE are ignored.  When FILE is a `-', standard input is read.
            This option may be repeated.
    -O EXTENSIONS, --file-extension=EXTENSIONS
            Search only files whose filename extensions match the specified
            comma-separated list of EXTENSIONS, same as --include='*.ext' for
            each `ext' in EXTENSIONS.  When `ext' is preceded by a `!' or a
            `^', skip files whose filename extensions matches `ext', same as
            --exclude='*.ext'.  This option may be repeated and may be combined
            with options -g, -M and -t to expand the recursive search.
    --stats
            Output statistics on the number of files and directories searched,
            and the inclusion and exclusion constraints applied.

See also [Including or excluding mounted file systems from searches](#fs).

Gitignore-style glob syntax and conventions:

glob     | matches
-------- | --------------------------------------------------------------------
`*`      | matches anything except a `/`
`?`      | matches any one character except a `/`
`[a-z]`  | matches one character in the selected range of characters
`[^a-z]` | matches one character not in the selected range of characters
`[!a-z]` | matches one character not in the selected range of characters
`/`      | when used at the begin of a glob, matches working directory
`**/`    | matches zero or more directories
`/**`    | when at the end of a glob, matches everything after the `/`
`\?`     | matches a `?` (or any character specified after the backslash)

When a glob contains a path separator `/`, the pathname is matched.  Otherwise
the basename of a file or directory is matched.  For example, `*.h` matches
`foo.h` and `bar/foo.h`. `bar/*.h` matches `bar/foo.h` but not `foo.h` and not
`bar/bar/foo.h`.

When a glob begins with a `/`, files and directories are matched at the working
directory, i.e. not recursively.  For example, use a leading `/` to force
`/*.h` to match `foo.h` but not `bar/foo.h`.

When a glob ends with a `/`, directories are matched instead of files, same as
`--include-dir`.

When a glob starts with a `!` as specified with `-g!GLOB`, or specified in a
`FILE` with `--include-from=FILE` or `--exclude-from=FILE`, it is negated.

To view a list of inclusions and exclusions that were applied to a search, use
option `--stats`.

To list only readable files with names starting with `foo` in the working
directory, that contain `xyz`, without producing warning messages with `-s` and
`-l`:

    ugrep -sl 'xyz' foo*

The same, but using deep recursion with inclusion constraints (note that
`-g'/foo*` is the same as `--include='/foo*'` and `-g'/foo*/'` is the same as
`--include-dir='/foo*'`, i.e.  immediate subdirectories matching `/foo*` only):

    ugrep -Rl 'xyz' -g'/foo*' -g'/foo*/'

Note that `-R` is the default, we use it here to make the examples easier to
follow.

To exclude directory `bak` located in the working directory:

    ugrep -Rl 'xyz' -g'^/bak/'

To exclude all directoies `bak` at any directory level deep:

    ugrep -Rl 'xyz' -g'^bak/'

To only list files in the working directory and its subdirectory `doc`,
that contain `xyz` (note that `-g'/doc/'` is the same as
`--include-dir='/doc'`, i.e. immediate subdirectory `doc` only):

    ugrep -Rl 'xyz' -g'/doc/'

To only list files that are on a subdirectory path `doc` that includes
subdirectory `html` anywhere, that contain `xyz`:

    ugrep -Rl 'xyz' -g'doc/**/html/'

To only list files in the working directory and in the subdirectories `doc`
and `doc/latest` but not below, that contain `xyz`:

    ugrep -Rl 'xyz' -g'/doc/' -g'/doc/latest/'

To recursively list .cpp files in the working directory and any subdirectory
at any depth, that contain `xyz`:

    ugrep -Rl 'xyz' -g'*.cpp'

The same, but using a .gitignore-style glob that matches pathnames (globs with
`/`) instead of matching basenames (globs without `/`) in the recursive search:

    ugrep -Rl 'xyz' -g'**/*.cpp'

Same, but using option `-Ocpp` to match file name extensions:

    ugrep -Rl -Ocpp 'xyz'

To recursively list all files in the working directory and below that are not
ignored by a specific .gitignore file:

    ugrep -Rl '' --exclude-from=.gitignore

To recursively list all files in the working directory and below that are not
ignored by one or more .gitignore files, when any are present:

    ugrep -Rl '' --ignore-files

🔝 [Back to table of contents](#toc)

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

These options control recursive searches across file systems by comparing
device numbers.  Mounted devices and symbolic links to files and directories
located on mounted file systems may be included or excluded from recursive
searches by specifying a mount point or a pathname of any directory on the file
system to specify the applicable file system.

Note that a list of mounted file systems is typically stored in `/etc/mtab`.

To restrict recursive searches to the file system of the working directory
only, without crossing into other file systems (similar to `find` option `-x`):

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

🔝 [Back to table of contents](#toc)

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

🔝 [Back to table of contents](#toc)

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

    ugrep -nw 'main' myfile.cpp

To display the entire file `myfile.cpp` with line `-n` numbers:

    ugrep -n '' myfile.cpp

To recursively search for C++ files with `main`, showing the line and column
numbers of matches with `-n` and `-k`:

    ugrep -r -nk -tc++ 'main'

To display the byte offset of matches with `-b`:

    ugrep -r -b -tc++ 'main'

To display the line and column numbers of matches in XML with `--xml`:

    ugrep -r -nk --xml -tc++ 'main'

🔝 [Back to table of contents](#toc)

<a name="color"/>

### Displaying colors with --color and paging the output with --pager

    --color[=WHEN], --colour[=WHEN]
            Mark up the matching text with the expression stored in the
            GREP_COLOR or GREP_COLORS environment variable.  The possible
            values of WHEN can be `never', `always', or `auto', where `auto'
            marks up matches only when output on a terminal.  The default is
            `auto'.
    --colors=COLORS, --colours=COLORS
            Use COLORS to mark up text.  COLORS is a colon-separated list of
            one or more parameters `sl=' (selected line), `cx=' (context line),
            `mt=' (matched text), `ms=' (match selected), `mc=' (match
            context), `fn=' (file name), `ln=' (line number), `cn=' (column
            number), `bn=' (byte offset), `se=' (separator).  Parameter values
            are ANSI SGR color codes or `k' (black), `r' (red), `g' (green),
            `y' (yellow), `b' (blue), `m' (magenta), `c' (cyan), `w' (white).
            Upper case specifies background colors.  A `+' qualifies a color as
            bright.  A foreground and a background color may be combined with
            font properties `n' (normal), `f' (faint), `h' (highlight), `i'
            (invert), `u' (underline).  Parameter `hl' enables file name
            hyperlinks.  Parameter `rv' reverses the `sl=' and `cx=' parameters
            with option -v.  Selectively overrides GREP_COLORS.
    --tag[=TAG[,END]]
            Disables colors to mark up matches with TAG.  END marks the end of
            a match if specified, otherwise TAG.  The default is `___'.
    --pager[=COMMAND]
            When output is sent to the terminal, uses COMMAND to page through
            the output.  The default COMMAND is `less -R'.  Enables --heading
            and --line-buffered.
    --pretty
            When output is sent to a terminal, enables --color, --heading, -n,
            --sort and -T when not explicitly disabled or set.

To change the color palette, set the `GREP_COLORS` environment variable or use
`--colors=COLORS`.  The value is a colon-separated list of ANSI SGR parameters
that defaults to `cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36`:

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
available on most color terminals:

code | c | effect                     | code | c  | effect
---- | - | -------------------------- | ---- | -- | ----------------------------
0    | n | normal font and color      | 2    | f  | faint (not widely supported)
1    | h | highlighted bold font      | 21   | H  | highlighted bold off
4    | u | underline                  | 24   | U  | underline off
7    | i | invert video               | 27   | I  | invert off
30   | k | black text                 | 90   | +k | bright gray text
31   | r | red text                   | 91   | +r | bright red text
32   | g | green text                 | 92   | +g | bright green text
33   | y | yellow text                | 93   | +y | bright yellow text
34   | b | blue text                  | 94   | +b | bright blue text
35   | m | magenta text               | 95   | +m | bright magenta text
36   | c | cyan text                  | 96   | +c | bright cyan text
37   | w | white text                 | 97   | +w | bright white text
40   | K | black background           | 100  | +K | bright gray background
41   | R | dark red background        | 101  | +R | bright red background
42   | G | dark green background      | 102  | +G | bright green background
43   | Y | dark yellow backgrounda    | 103  | +Y | bright yellow background
44   | B | dark blue background       | 104  | +B | bright blue background
45   | M | dark magenta background    | 105  | +M | bright magenta background
46   | C | dark cyan background       | 106  | +C | bright cyan background
47   | W | dark white background      | 107  | +W | bright white background

See Wikipedia [ANSI escape code - SGR parameters](https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_parameters)

For quick and easy color specification, the corresponding single-letter color
names may be used in place of numeric SGR codes.  Semicolons are not required
to separate color names.  Color names and numeric codes may be mixed.

For example, to display matches in underlined bright green on bright selected
lines, aiding in visualizing white space in matches and file names:

    export GREP_COLORS='sl=1:cx=33:ms=1;4;32;100:mc=1;4;32:fn=1;32;100:ln=1;32:cn=1;32:bn=1;32:se=36'

The same, but with single-letter color names:

    export GREP_COLORS='sl=h:cx=y:ms=hug+K:mc=hug:fn=hg+K:ln=hg:cn=hg:bn=hg:se=c'

Another color scheme that works well:

    export GREP_COLORS='cx=hb:ms=hiy:mc=hic:fn=hi+y+K:ln=hg:cn=hg:bn=hg:se='

Modern Windows command interpreters support ANSI escape codes.  Named or
numeric colors can be set with `SET GREP_COLORS`, for example:

    SET GREP_COLORS=sl=1;37:cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36

To disable colors on Windows:

    SET GREP_COLORS=""

Color intensities may differ per platform and per terminal program used, which
affects readability.

Option `-y` outputs every line of input, including non-matching lines as
context.  The use of color helps distinguish matches from non-matching context.

To copy silver searcher's color palette:

    export GREP_COLORS='mt=30;43:fn=1;32:ln=1;33:cn=1;33:bn=1;33'

To produce color-highlighted results (`--color` is redundance since it is the
default):

    ugrep --color -R -n -k -tc++ 'FIXME.*'

To page through the results with pager (`less -R` by default):

    ugrep --pager -R -n -k -tc++ 'FIXME'

To display a hexdump of a zip file itself (i.e. without decompressing), with
color-highlighted matches of the zip magic bytes `PK\x03\x04` (`--color` is
redundant since it is the default):

    ugrep --color -y -UX 'PK\x03\x04' some.zip

To use predefined patterns to list all `#include` and `#define` in C++ files:

    ugrep --pretty -R -n -tc++ -f c++/includes -f c++/defines

Same, but overriding the color of matches as inverted yellow (reverse video)
and headings with yellow on blue using `--pretty`:

    ugrep --pretty --colors="ms=yi:fn=hyB" -R -n -tc++ -f c++/includes -f c++/defines

To list all `#define FOO...` macros in C++ files, color-highlighted:

    ugrep --color=always -R -n -tc++ -f c++/defines | ugrep 'FOO.*'

Same, but restricted to `.cpp` files only:

    ugrep --color=always -R -n -Ocpp -f c++/defines | ugrep 'FOO.*'

To search tarballs for matching names of PDF files (assuming bash is our shell):

    for tb in *.tar *.tar.gz *.tgz; do echo "$tb"; tar tfz "$tb" | ugrep '.*\.pdf$'; done

🔝 [Back to table of contents](#toc)

<a name="json"/>

### Output matches in JSON, XML, CSV, C++

    --cpp   Output file matches in C++.  See also options --format and -u.
    --csv   Output file matches in CSV.  If -H, -n, -k, or -b is specified,
            additional values are output.  See also options --format and -u.
    --json  Output file matches in JSON.  If -H, -n, -k, or -b is specified,
            additional values are output.  See also options --format and -u.
    --xml   Output file matches in XML.  If -H, -n, -k, or -b is specified,
            additional values are output.  See also options --format and -u.

To recursively search for lines with `TODO` and display C++ file matches in
JSON with line number properties:

    ugrep -tc++ -n --json 'TODO'

To recursively search for lines with `TODO` and display C++ file matches in
XML with line and column number attributes:

    ugrep -tc++ -nk --xml 'TODO'

To recursively search for lines with `TODO` and display C++ file matches in CSV
format with file pathname, line number, and column number fields:

    ugrep -tc++ --csv -Hnk 'TODO'

To extract a table from an HTML file and put it in C/C++ source code using
`-o`:

    ugrep -o --cpp '<tr>.*</tr>' index.html > table.cpp

🔝 [Back to table of contents](#toc)

<a name="format"/>

### Customized output with --format

    --format=FORMAT
            Output FORMAT-formatted matches.  For example `--format=%f:%n:%O%~'
            outputs matching lines `%O' with filename `%f` and line number `%n'
            followed by a newline `%~'.   Context options -A, -B, -C, and -y are
            ignored.  See `man ugrep' section FORMAT.

The following output formatting options may be used.  The `FORMAT` string
`%`-fields are listed in a table further below:

option                  | result
----------------------- | ------------------------------------------------------
`--format-begin=FORMAT` | `FORMAT` when beginning the search
`--format-open=FORMAT`  | `FORMAT` when opening a file and a match was found
`--format=FORMAT`       | `FORMAT` for each match in a file
`--format-close=FORMAT` | `FORMAT` when closing a file and a match was found
`--format-end=FORMAT`   | `FORMAT` when ending the search

The following tables show the formatting options corresponding to `--csv`,
`--json`, and `--xml`.

#### `--csv`

option           | format string (within quotes)
---------------- | -----------------------------
`--format`       | `'%[,]$%H%N%K%B%V%~%u'`

#### `--json`

option           | format string (within quotes)
---------------- | -----------------------------
`--format-begin` | `'['`
`--format-open`  | `'%,%~  {%~    %[,%~    ]$%["file": ]H"matches": ['`
`--format`       | `'%,%~      { %[, ]$%["line": ]N%["column": ]K%["offset": ]B"match": %J }%u'`
`--format-close` | `'%~    ]%~  }'`
`--format-end`   | `'%~]%~'`

#### `--xml`

option           | format string (within quotes)
---------------- | -----------------------------
`--format-begin` | `'<grep>%~'`
`--format-open`  | `'  <file%[]$%[ name=]H>%~'`
`--format`       | `'    <match%[\"]$%[ line=\"]N%[ column=\"]K%[ offset=\"]B>%X</match>%~%u'`
`--format-close` | `'  </file>%~'`
`--format-end`   | `'</grep>%~'`

The following fields may be used in the `FORMAT` string:

field                   | output
----------------------- | ------------------------------------------------------
`%F`                    | if option `-H` is used: the file pathname and separator
`%[ARG]F`               | if option `-H` is used: `ARG`, the file pathname and separator
`%f`                    | the file pathname
`%a`                    | the file basename without directory path
`%p`                    | the directory path to the file
`%z`                    | the pathname in a (compressed) archive, without `{` and `}`
`%H    `                | if option `-H` is used: the quoted pathname and separator
`%[ARG]H`               | if option `-H` is used: `ARG`, the quoted pathname and separator
`%h`                    | the quoted file pathname
`%N`                    | if option `-n` is used: the line number and separator
`%[ARG]N`               | if option `-n` is used: `ARG`, the line number and separator
`%n`                    | the line number of the match
`%K`                    | if option `-k` is used: the column number and separator
`%[ARG]K`               | if option `-k` is used: `ARG`, the column number and separator
`%k`                    | the column number of the match
`%B`                    | if option `-b` is used: the byte offset and separator
`%[ARG]B`               | if option `-b` is used: `ARG`, the byte offset and separator
`%b`                    | the byte offset of the match
`%T`                    | if option `-T` is used: `ARG` and a tab character
`%[ARG]T`               | if option `-T` is used: `ARG` and a tab character
`%t`                    | a tab character
`%[SEP]$`               | set field separator to `SEP` for the rest of the format fields
`%[ARG]<`               | if the first match: `ARG`
`%[ARG]>`               | if not the first match: `ARG`
`%,`                    | if not the first match: a comma, same as `%[,]>`
`%:`                    | if not the first match: a colon, same as `%[:]>`
`%;`                    | if not the first match: a semicolon, same as `%[;]>`
`%│`                    | if not the first match: a vertical bar, same as `%[│]>`
`%[ARG]S`               | if not the first match: `ARG` and separator, see also `%$`
`%s`                    | the separator, see also `%S` and `%$`
`%~`                    | a newline character, same as `\n`
`%m`                    | the number of matches or matched files
`%O`                    | the matching line is output as is (a raw string of bytes)
`%o`                    | the match is output as is (a raw string of bytes)
`%Q`                    | the matching line as a quoted string, `\"` and `\\` replace `"` and `\`
`%q`                    | the match as a quoted string, `\"` and `\\` replace `"` and `\`
`%C`                    | the matching line formatted as a quoted C/C++ string
`%c`                    | the match formatted as a quoted C/C++ string
`%J`                    | the matching line formatted as a quoted JSON string
`%j`                    | the match formatted as a quoted JSON string
`%V`                    | the matching line formatted as a quoted CSV string
`%v`                    | the match formatted as a quoted CSV string
`%X`                    | the matching line formatted as XML character data
`%x`                    | the match formatted as XML character data
`%w`                    | the width of the match, counting (wide) characters
`%d`                    | the size of the match, counting bytes
`%e`                    | the ending byte offset of the match
`%Z`                    | the edit distance cost of an approximate match with option `-Z`
`%u`                    | select unique lines only unless option -u is used
`%1`,`%2`,...,`%9`      | the first regex group capture of the match, and so on up to group `%9`, requires option `-P`
`%[NUM]#`               | the regex group capture `NUM`; requires option `-P`
`%[NUM1\|NUM2\|...]#`   | the first group capture `NUM` that matched; requires option `-P`
`%[NAME]#`              | the `NAME`d group capture; requires option `-P` and capturing pattern `(?<NAME>PATTERN)`
`%[NAME1\|NAME2\|...]#` | the first `NAME`d group capture that matched; requires option `-P` and capturing pattern `(?<NAME>PATTERN)`
`%G`                    | list of group capture indices/names of the match (see note)
`%[TEXT1\|TEXT2\|...]G` | list of TEXT indexed by group capture indices that matched; requires option `-P`
`%g`                    | the group capture index of the match or 1 (see note)
`%[TEXT1\|TEXT2\|...]g` | the first TEXT indexed by the first group capture index that matched; requires option `-P`
`%%`                    | the percentage sign

Note:

- Formatted output is written without a terminating newline, unless `%~` or `\n`
  is explicitly specified in the format string.
- The `[ARG]` part of a field is optional and may be omitted.  When present,
  the argument must be placed in `[]` brackets, for example `%[,]F` to output a
  comma, the pathname, and a separator, when option `-H` is used.
- Fields `%[SEP]$` and `%u` are switches and do not write anything to the
  output.
- The separator used by `%F`, `%H`, `%N`, `%K`, `%B`, `%S`, and `%G` may be
  changed by preceding the field with a `%[SEP]$`.  When `[SEP]` is not
  provided, reverts the separator to the default separator or the separator
  specified by `--separator`.
- Formatted output is written for each matching pattern, which means that a
  line may be output multiple times when patterns match more than once on the
  same line.  When field `%u` is found anywhere in the specified format string,
  matching lines are output only once unless option `-u`, `--ungroup`
  is used or when a newline is matched.
- The group capture index value output by `%g` corresponds to the index of the
  sub-pattern matched among the alternations in the pattern when option `-P` is
  not used.  For example `foo|bar` matches `foo` with index 1 and `bar` with
  index 2.  With option `-P`, the index corresponds to the number of the group
  captured in the specified pattern.
- The strings specified in the list `%[TEXT1|TEXT2|...]G` and
  `%[TEXT1|TEXT2|...]g` should correspond to the group capture index (see the
  note above), i.e. `TEXT1` is output for index 1, `TEXT2` is output for index
  2, and so on.  If the list is too short, the index value is output or the
  name of a named group capture is output.

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

    ugrep --format='%o%~' "href=[\"'][^\"'][\"']" index.html

To string together the pattern matches as CSV-formatted strings with field `%v`
separated by commas with field `%,`:

    ugrep --format='%,%v' "href=[\"'][^\"'][\"']" index.html

To output matches in CSV (comma-separated values), the same as option `--csv`
(works with options `-H`, `-n`, `-k`, `-b` to add CSV values):

    ugrep --format='"%[,]$%H%N%K%B%V%~%u"' 'href=' index.html

To output matches in AckMate format:

    ugrep --format=":%f%~%n;%k %w:%O%~" 'href=' index.html

To output the sub-pattern indices 1, 2, and 3 on the left to the match for the
three patterns `foo`, `bar`, and `baz` in file `foobar.txt`:

    ugrep --format='%g: %o%~' 'foo|bar|baz' foobar.txt

Same, but using a file `foos` containing three lines with `foo`, `bar`, and
`baz`, where option `-F` is used to match strings instead of regex:

    ugrep -F -f foos --format='%g: %o%~' foobar.txt

To output `one`, `two`, and `a word` for the sub-patterns `[fF]oo`, `[bB]ar`,
and any other word `\w+`, respectively, using argument `[one|two|a word]` with
field `%g` indexed by sub-pattern (or group captures with option `-P`):

    ugrep --format='%[one|two|a word]g%~' '([fF]oo)|([bB]ar)|(\w+)' foobar.txt

To output a list of group capture indices with `%G` separated by the word `and`
instead of the default colons with `%[ and ]$`, followed by the matching line:

    ugrep -P --format='%[ and ]$%G%$%s%O%~' '(foo)|(ba((r)|(z)))' foobar.txt

Same, but showing names instead of numbers:

    ugrep -P --format='%[ and ]$%[foo|ba|r|z]G%$%s%O%~' '(foo)|(ba(?:(r)|(z)))' foobar.txt

Note that option `-P` is required for general use of group captures for
sub-patterns.  Named sub-pattern matches may be used with PCRE2 and shown in
the output:

    ugrep -P --format='%[ and ]$%G%$%s%O%~' '(?P<foo>foo)|(?P<ba>ba(?:(?P<r>r)|(?P<z>z)))' foobar.txt

🔝 [Back to table of contents](#toc)

<a name="replace"/>

### Replacing matches with -P --format backreferences to group captures

    --format=FORMAT
            Output FORMAT-formatted matches.  For example `--format=%f:%n:%O%~'
            outputs matching lines `%O' with filename `%f` and line number `%n'
            followed by a newline `%~'. See `man ugrep' section FORMAT.
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

    ugrep -R -thtml,php -P '<[^<>]+href\h*=\h*.([^\x27"]+).' --format='%1%~' | sort -u

Same, but much easier by using the predefined `html/href` pattern:

    ugrep -R -thtml,php -P -f html/href --format='%1%~' | sort -u

Likewise, but in this case select `<script>` `src` URLs when referencing `http`
and `https` sites:

    ugrep -R -thtml,php -P '<script.*src\h*=\h*.(https?:[^\x27"]+).' --format='%1%~' | sort -u

🔝 [Back to table of contents](#toc)

<a name="max"/>

### Limiting the number of matches with -1,-2...-9, -K, -m, and --max-files

    --depth=[MIN,][MAX], -1, -2 ... -9, --10, --11 ...
            Restrict recursive searches from MIN to MAX directory levels deep,
            where -1 (--depth=1) searches the specified path without recursing
            into subdirectories.  Note that -3 -5, -3-5, or -35 searches 3 to 5
            levels deep.  Enables -R if -R or -r is not specified.
    -K FIRST[,LAST], --range=FIRST[,LAST]
            Start searching at line FIRST, stop at line LAST when specified.
    -m NUM, --max-count=NUM
            Stop reading the input after NUM matches for each file processed.
    --max-files=NUM
            Restrict the number of files matched to NUM.  Note that --sort or
            -J1 may be specified to produce replicable results.  If --sort is
            specified, the number of threads spawned is limited to NUM.
    --sort[=KEY]
            Displays matching files in the order specified by KEY in recursive
            searches.  KEY can be `name' to sort by pathname (default), `best'
            to sort by best match with option -Z (sort by best match requires
            two passes over the input files), `size' to sort by file size,
            `used' to sort by last access time, `changed' to sort by last
            modification time, and `created' to sort by creation time.  Sorting
            is reversed with `rname', `rbest', `rsize', `rused', `rchanged', or
            `rcreated'.  Archive contents are not sorted.  Subdirectories are
            sorted and displayed after matching files.  FILE arguments are
            searched in the same order as specified.  Normally ugrep displays
            matches in no particular order to improve performance.

To show only the first 10 matches of `FIXME` in C++ files in the working
directory and all subdirectories below:

    ugrep -R -m10 -tc++ FIXME

Same, but recursively search up to two directory levels, meaning that `./` and
`./sub/` are visited but not deeper:

    ugrep -2 -m10 -tc++ FIXME

To show only the first two files that have one or more matches of `FIXME` in
the list of files sorted by pathname, using `--max-files=2`:

    ugrep --sort -R --max-files=2 -tc++ FIXME

To search file `install.sh` for the occurrences of the word `make` after the
first line, we use `-K` with line number 2 to start searching, where `-n` shows
the line numbers in the output:

    ugrep -n -K2 -w make install.sh

Same, but restricting the search to lines 2 to 40 (inclusive):

    ugrep -n -K2,40 -w make install.sh

Same, but showing all lines 2 to 40 with `-y`:

    ugrep -y -n -K2,40 -w make install.sh

Same, but showing only the first four matching lines after line 2, with one
line of context:

    ugrep -n -C1 -K2 -m4 -w make install.sh

🔝 [Back to table of contents](#toc)

<a name="empty"/>

### Matching empty patterns with -Y

    -Y, --empty
            Permits empty matches.  By default, empty matches are disabled,
            unless a pattern begins with `^' or ends with `$'.  Note that -Y
            when specified with an empty-matching pattern, such as x? and x*,
            match all input, not only lines containing the character `x'.

Option `-Y` permits empty pattern matches, like GNU/BSD grep.  This option is
introduced by **ugrep** to prevent accidental matching with empty patterns:
empty-matching patterns such as `x?` and `x*` match all input, not only lines
with `x`.  By default, without `-Y`, patterns match lines with at least one `x`
as intended.

This option is automatically enabled when a pattern starts with `^` or ends
with `$` is specified.  For example, `^\h*$` matches blank lines, including
empty lines.

To recursively list files in the working directory with blank lines, i.e. lines
with white space only, including empty lines (note that option `-Y` is
implicitly enabled since the pattern starts with `^` and ends with `$`):

    ugrep -l '^\h*$'

🔝 [Back to table of contents](#toc)

<a name="case"/>

### Case-insentitive matching with -i and -j

    -i, --ignore-case
            Perform case insensitive matching.  By default, ugrep is case
            sensitive.  By default, this option applies to ASCII letters only.
            Use options -P and -i for Unicode case insensitive matching.
    -j, --smart-case
            Perform case insensitive matching like option -i, unless a pattern
            is specified with a literal ASCII upper case letter.

To match `todo` in `myfile.cpp` regardless of case:

     ugrep -i 'todo' myfile.txt

To match `todo XXX` with `todo` in any case but `XXX` as given, with pattern
`(?i:todo)` to match `todo` ignoring case:

     ugrep '(?i:todo) XXX' myfile.cpp

🔝 [Back to table of contents](#toc)

<a name="sort"/>

### Sort files by name, best match, size, and time

    --sort[=KEY]
            Displays matching files in the order specified by KEY in recursive
            searches.  KEY can be `name' to sort by pathname (default), `best'
            to sort by best match with option -Z (sort by best match requires
            two passes over the input files), `size' to sort by file size,
            `used' to sort by last access time, `changed' to sort by last
            modification time, and `created' to sort by creation time.  Sorting
            is reversed with `rname', `rbest', `rsize', `rused', `rchanged', or
            `rcreated'.  Archive contents are not sorted.  Subdirectories are
            sorted and displayed after matching files.  FILE arguments are
            searched in the same order as specified.  Normally ugrep displays
            matches in no particular order to improve performance.

The matching files are displayed in the order specified by `--sort`.  By
default, the output is not sorted to improve performance, unless option `-Q` is
used which sorts files by name by default.  An optimized sorting method and
strategy are implemented in the asynchronous output class to keep the overhead
of sorting very low.  Directories are displayed after files are displayed
first, when recursing, which visually aids the user in finding the "closest"
matching files first at the top of the displayed results.

To recursively search for C++ files that match `main` and sort them by name:

    ugrep --sort -tc++ 'main'

Same, but sorted by time changed from most recent to oldest:

    ugrep --sort=rchanged -tc++ 'main'

🔝 [Back to table of contents](#toc)

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

🔝 [Back to table of contents](#toc)

<a name="more"/>

### More examples

To search for pattern `-o` in `script.sh` using `-e` to explicitly specify a
pattern to prevent pattern `-o` from being interpreted as an option:

    ugrep -n -e '-o' script.sh

Alternatively, using `--` to end the list of command arguments:

    ugrep -n -- '-o' script.sh

To recursively list all text files (.txt and .md) that do not properly end with
a `\n` (`-o` is required to match `\n` or `\z`):

    ugrep -L -o -Otext '\n\z'

To list all markdown sections in text files (.text, .txt, .TXT, and .md):

    ugrep -o -ttext -e '^.*(?=\r?\n(===|---))' -e '^#{1,6}\h+.*'

To display multi-line backtick and indented code blocks in markdown files with
their line numbers, using a lazy quantifier `*?` to make the pattern compact:

    ugrep -n -ttext -e '^```(.|\n)*?\n```' -e '^(\t|[ ]{4}).*'

To find mismatched code (a backtick without matching backtick on the same line)
in markdown:

    ugrep -n -ttext -e '`[^`]+' -N '`[^`]*`'

🔝 [Back to table of contents](#toc)

<a name="man"/>

### Man page

    UGREP(1)                         User Commands                        UGREP(1)



    NAME
           ugrep, ug -- file pattern searcher

    SYNOPSIS
           ugrep [OPTIONS] [-A NUM] [-B NUM] [-C NUM] [-y] [-Q|PATTERN] [-f FILE]
                 [-e PATTERN] [-N PATTERN] [-t TYPES] [-g GLOBS] [--sort[=KEY]]
                 [--color[=WHEN]|--colour[=WHEN]] [--pager[=COMMAND]] [FILE ...]

    DESCRIPTION
           The  ugrep utility searches any given input files, selecting lines that
           match one or more patterns.  By default, a  pattern  matches  an  input
           line  if the regular expression (RE) matches the input line.  A pattern
           matches multiple input lines if the RE in the pattern  matches  one  or
           more newlines in the input.  An empty pattern matches every line.  Each
           input line that matches at least one of the patterns is written to  the
           standard output.

           ugrep accepts input of various encoding formats and normalizes the out-
           put to UTF-8.  When a UTF byte order mark is present in the input,  the
           input  is  automatically normalized; otherwise, ugrep assumes the input
           is ASCII, UTF-8, or raw binary.  An input encoding format may be speci-
           fied with option --encoding.

           The ug command is equivalent to ugrep --config to load the default con-
           figuration file, which allows for customization, see CONFIGURATION.

           If no FILE arguments are specified and standard input is  read  from  a
           terminal,  recursive  searches are performed as if -R is specified.  To
           force reading from standard input, specify `-' as a FILE argument.

           Directories specified as FILE arguments are searched without  recursing
           into subdirectories, unless -R, -r, or -2...-9 is specified.

           Hidden files and directories are ignored in recursive searches.  Option
           -. (--hidden)  includes  hidden  files  and  directories  in  recursive
           searches.

           A  query interface is opened with -Q (--query) to interactively specify
           search patterns and view search results.  Note that a PATTERN  argument
           cannot be specified in this case.  To specify one or more patterns with
           -Q, use -e PATTERN.

           For help, --help WHAT displays help on options related to WHAT.

           The following options are available:

           -A NUM, --after-context=NUM
                  Print NUM  lines  of  trailing  context  after  matching  lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also options -B, -C, and -y.

           -a, --text
                  Process a binary file as if it were text.  This is equivalent to
                  the --binary-files=text option.  This option might output binary
                  garbage to the terminal, which can have problematic consequences
                  if the terminal driver interprets some of it as commands.

           --and [[-e] PATTERN] ... -e PATTERN
                  Specify  additional  patterns to match.  Patterns must be speci-
                  fied with -e.  Each -e PATTERN following this option is  consid-
                  ered  an  alternative  pattern  to match, i.e. each -e is inter-
                  preted as an OR pattern.  For example, -e A -e B --and -e C -e D
                  matches  lines  with  (`A'  or `B') and (`C' or `D').  Note that
                  multiple -e PATTERN are  alternations  that  bind  more  tightly
                  together  than  --and.   Option --stats displays the search pat-
                  terns applied.  See also options --not, --andnot, and --bool.

           --andnot [[-e] PATTERN] ...
                  Combines --and  --not.   See  also  options  --and,  --not,  and
                  --bool.

           -B NUM, --before-context=NUM
                  Print  NUM  lines  of  leading  context  before  matching lines.
                  Places a --group-separator between contiguous groups of matches.
                  See also options -A, -C, and -y.

           -b, --byte-offset
                  The  offset  in bytes of a matched line is displayed in front of
                  the respective matched line.  If -u is specified,  displays  the
                  offset  for each pattern matched on the same line.  Byte offsets
                  are exact for ASCII, UTF-8, and raw  binary  input.   Otherwise,
                  the byte offset in the UTF-8 normalized input is displayed.

           --binary-files=TYPE
                  Controls  searching  and  reporting  pattern  matches  in binary
                  files.  TYPE can be `binary',  `without-match`,  `text`,  `hex`,
                  and  `with-hex'.  The default is `binary' to search binary files
                  and to report a match  without  displaying  the  match.   `with-
                  out-match'  ignores  binary  matches.   `text' treats all binary
                  files as text, which might output binary garbage to  the  termi-
                  nal,  which  can  have  problematic consequences if the terminal
                  driver interprets some of it as  commands.   `hex'  reports  all
                  matches  in hexadecimal.  `with-hex' only reports binary matches
                  in hexadecimal, leaving text matches alone.  A match is  consid-
                  ered  binary  when  matching  a zero byte or invalid UTF.  Short
                  options are -a, -I, -U, -W, and -X.

           --bool, -%
                  Specifies Boolean search patterns.  A Boolean search pattern  is
                  composed  of  `AND', `OR', `NOT' operators and grouping with `('
                  `)'.  Spacing between subpatterns is the same as `AND',  `|'  is
                  the  same  as  `OR',  and  a `-' is the same as `NOT'.  The `OR'
                  operator binds more tightly than  `AND'.   For  example,  --bool
                  'A|B  C|D'  matches  lines  with  (`A' or `B') and (`C' or `D'),
                  --bool 'A -B' matches lines with `A'  and  not  `B'.   Operators
                  `AND',  `OR', `NOT' require proper spacing.  For example, --bool
                  'A OR B AND C OR D' matches lines with (`A' or `B') and (`C'  or
                  `D'),  --bool  'A AND NOT B' matches lines with `A' without `B'.
                  Quoted subpatterns are matched literally as strings.  For  exam-
                  ple,  --bool  'A  "AND"|"OR"'  matches  lines  with `A' and also
                  either `AND' or `OR'.  Parenthesis are used for  grouping.   For
                  example,  --bool  '(A  B)|C'  matches lines with `A' and `B', or
                  lines with `C'.  Note that all subpatterns in a  Boolean  search
                  pattern  are  regular  expressions,  unless  option  -F is used.
                  Options -E, -F, -G, -P, and -Z can be combined  with  --bool  to
                  match  subpatterns  as strings or regular expressions (-E is the
                  default.)  This option does  not  apply  to  -f  FILE  patterns.
                  Option  --stats  displays the search patterns applied.  See also
                  options --and, --andnot, and --not.

           --break
                  Adds a line break between results from different files.

           -C NUM, --context=NUM
                  Print NUM lines of leading and trailing context surrounding each
                  match.   Places a --group-separator between contiguous groups of
                  matches.  See also options -A, -B, and -y.

           -c, --count
                  Only a count of selected lines is written  to  standard  output.
                  If -o or -u is specified, counts the number of patterns matched.
                  If -v is specified, counts the number of non-matching lines.

           --color[=WHEN], --colour[=WHEN]
                  Mark up the matching text with  the  expression  stored  in  the
                  GREP_COLOR  or  GREP_COLORS  environment  variable.  WHEN can be
                  `never', `always', or `auto', where `auto' marks up matches only
                  when output on a terminal.  The default is `auto'.

           --colors=COLORS, --colours=COLORS
                  Use COLORS to mark up text.  COLORS is a colon-separated list of
                  one or more parameters `sl='  (selected  line),  `cx='  (context
                  line),  `mt='  (matched  text),  `ms='  (match  selected), `mc='
                  (match context), `fn=' (file name), `ln=' (line  number),  `cn='
                  (column number), `bn=' (byte offset), `se=' (separator).  Param-
                  eter values are ANSI SGR color codes or `k' (black), `r'  (red),
                  `g'  (green),  `y'  (yellow),  `b'  (blue),  `m'  (magenta), `c'
                  (cyan), `w' (white).  Upper case specifies background colors.  A
                  `+'  qualifies a color as bright.  A foreground and a background
                  color may be combined with font  properties  `n'  (normal),  `f'
                  (faint), `h' (highlight), `i' (invert), `u' (underline).  Param-
                  eter `hl' enables file name hyperlinks.  Parameter `rv' reverses
                  the  `sl='  and  `cx='  parameters  with option -v.  Selectively
                  overrides GREP_COLORS.

           --config[=FILE], ---[FILE]
                  Use configuration FILE.  The  default  FILE  is  `.ugrep'.   The
                  working  directory  is  checked  first  for  FILE, then the home
                  directory.  The options specified in the configuration FILE  are
                  parsed first, followed by the remaining options specified on the
                  command line.

           --confirm
                  Confirm actions in -Q query mode.  The default is confirm.

           --cpp  Output file matches in C++.  See also options --format and -u.

           --csv  Output file matches in CSV.  If -H, -n, -k, or -b is  specified,
                  additional values are output.  See also options --format and -u.

           -D ACTION, --devices=ACTION
                  If an input file is a device, FIFO  or  socket,  use  ACTION  to
                  process  it.   By  default,  ACTION  is `skip', which means that
                  devices are silently skipped.  If ACTION is `read', devices read
                  just as if they were ordinary files.

           -d ACTION, --directories=ACTION
                  If  an  input file is a directory, use ACTION to process it.  By
                  default, ACTION  is  `skip',  i.e.,  silently  skip  directories
                  unless specified on the command line.  If ACTION is `read', warn
                  when directories are read as input.   If  ACTION  is  `recurse',
                  read all files under each directory, recursively, following sym-
                  bolic links only if they are  on  the  command  line.   This  is
                  equivalent   to   the   -r   option.   If  ACTION  is  `derefer-
                  ence-recurse', read all files under each directory, recursively,
                  following  symbolic links.  This is equivalent to the -R option.

           --depth=[MIN,][MAX], -1, -2 ... -9, --10, --11 ...
                  Restrict recursive searches from MIN  to  MAX  directory  levels
                  deep,  where  -1 (--depth=1) searches the specified path without
                  recursing into subdirectories.  Note that -3 -5,  -3-5,  or  -35
                  searches  3  to  5  levels  deep.  Enables -R if -R or -r is not
                  specified.

           --dotall
                  Dot `.' in regular expressions matches anything, including  new-
                  line.   Note that `.*' matches all input and should not be used.

           -E, --extended-regexp
                  Interpret patterns as extended regular expressions (EREs).  This
                  is the default.

           -e PATTERN, --regexp=PATTERN
                  Specify  a PATTERN used during the search of the input: an input
                  line is selected if it matches any of  the  specified  patterns.
                  Note that longer patterns take precedence over shorter patterns.
                  This option is most useful when multiple -e options are used  to
                  specify  multiple  patterns,  when  a pattern begins with a dash
                  (`-'), to specify a pattern after option -f or  after  the  FILE
                  arguments.

           --encoding=ENCODING
                  The  encoding  format  of  the  input,  where  ENCODING  can be:
                  `binary', `ASCII', `UTF-8',  `UTF-16',  `UTF-16BE',  `UTF-16LE',
                  `UTF-32',   `UTF-32BE',   `UTF-32LE',   `LATIN1',  `ISO-8859-1',
                  `ISO-8859-2',    `ISO-8859-3',    `ISO-8859-4',    `ISO-8859-5',
                  `ISO-8859-6',    `ISO-8859-7',    `ISO-8859-8',    `ISO-8859-9',
                  `ISO-8859-10',  `ISO-8859-11',   `ISO-8859-13',   `ISO-8859-14',
                  `ISO-8859-15',   `ISO-8859-16',   `MAC',  `MACROMAN',  `EBCDIC',
                  `CP437',  `CP850',  `CP858',   `CP1250',   `CP1251',   `CP1252',
                  `CP1253',  `CP1254',  `CP1255',  `CP1256',  `CP1257',  `CP1258',
                  `KOI8-R', `KOI8-U', `KOI8-RU'.

           --exclude=GLOB
                  Skip files whose name matches GLOB using wildcard matching, same
                  as -g ^GLOB.  GLOB can use **, *, ?, and [...] as wildcards, and
                  \ to quote a wildcard or backslash  character  literally.   When
                  GLOB  contains  a  `/',  full  pathnames are matched.  Otherwise
                  basenames are matched.  When GLOB ends with a  `/',  directories
                  are  excluded as if --exclude-dir is specified.  Otherwise files
                  are excluded.  Note that --exclude patterns take  priority  over
                  --include  patterns.   GLOB  should  be  quoted to prevent shell
                  globbing.  This option may be repeated.

           --exclude-dir=GLOB
                  Exclude directories  whose  name  matches  GLOB  from  recursive
                  searches,  same  as -g ^GLOB/.  GLOB can use **, *, ?, and [...]
                  as wildcards, and \ to quote a wildcard or  backslash  character
                  literally.   When  GLOB  contains  a  `/',  full  pathnames  are
                  matched.   Otherwise   basenames   are   matched.    Note   that
                  --exclude-dir  patterns  take  priority  over --include-dir pat-
                  terns.  GLOB should be quoted to prevent shell  globbing.   This
                  option may be repeated.

           --exclude-from=FILE
                  Read  the  globs  from FILE and skip files and directories whose
                  name matches one or more globs (as if specified by --exclude and
                  --exclude-dir).   Lines  starting  with a `#' and empty lines in
                  FILE are ignored.  When FILE is a `-', standard input  is  read.
                  This option may be repeated.

           --exclude-fs=MOUNTS
                  Exclude   file   systems  specified  by  MOUNTS  from  recursive
                  searches, MOUNTS is a comma-separated list of  mount  points  or
                  pathnames   of   directories   on   file   systems.   Note  that
                  --exclude-fs mounts  take  priority  over  --include-fs  mounts.
                  This option may be repeated.

           -F, --fixed-strings
                  Interpret  pattern  as a set of fixed strings, separated by new-
                  lines, any of which is to be matched.  This makes  ugrep  behave
                  as  fgrep.   If a PATTERN is specified, or -e PATTERN or -N PAT-
                  TERN, then this option has no effect  on  -f  FILE  patterns  to
                  allow  -f FILE patterns to narrow or widen the scope of the PAT-
                  TERN search.

           -f FILE, --file=FILE
                  Read newline-separated patterns from FILE.  White space in  pat-
                  terns is significant.  Empty lines in FILE are ignored.  If FILE
                  does not exist, the GREP_PATH environment variable  is  used  as
                  path   to   FILE.    If   that   fails,   looks   for   FILE  in
                  /usr/local/share/ugrep/patterns.  When FILE is a  `-',  standard
                  input is read.  Empty files contain no patterns; thus nothing is
                  matched.  This option may be repeated.

           --filter=COMMANDS
                  Filter files through the specified COMMANDS first before search-
                  ing.   COMMANDS  is  a  comma-separated  list  of  `exts:command
                  [option ...]', where `exts' is a comma-separated list  of  file-
                  name  extensions  and `command' is a filter utility.  The filter
                  utility should read from standard input and  write  to  standard
                  output.  Files matching one of `exts' are filtered.  When `exts'
                  is `*', files with non-matching extensions are filtered.  One or
                  more  `option'  separated by spacing may be specified, which are
                  passed verbatim to the command.  A `%' as `option' expands  into
                  the  pathname to search.  For example, --filter='pdf:pdftotext %
                  -' searches PDF files.  The `%' expands into a `-' when  search-
                  ing  standard input.  Option --label=.ext may be used to specify
                  extension `ext' when searching standard input.

           --filter-magic-label=[+]LABEL:MAGIC
                  Associate LABEL with files whose signature "magic  bytes"  match
                  the  MAGIC  regex  pattern.   Only  files  that have no filename
                  extension are labeled, unless +LABEL is specified.   When  LABEL
                  matches  an extension specified in --filter=COMMANDS, the corre-
                  sponding command is invoked.  This option may be repeated.

           --format=FORMAT
                  Output   FORMAT-formatted   matches.    For    example    --for-
                  mat='%f:%n:%O%~'  outputs matching lines `%O' with filename `%f`
                  and line number  `%n'  followed  by  a  newline  `%~'.   Context
                  options -A, -B, -C, and -y are ignored.  See `man ugrep' section
                  FORMAT.

           --free-space
                  Spacing (blanks and tabs) in regular expressions are ignored.

           -G, --basic-regexp
                  Interpret pattern as a basic regular expression, i.e. make ugrep
                  behave as traditional grep.

           -g GLOBS, --glob=GLOBS
                  Search  only  files whose name matches the specified comma-sepa-
                  rated list of GLOBS, same as --include='glob' for each `glob' in
                  GLOBS.   When a `glob' is preceded by a `!' or a `^', skip files
                  whose name  matches  `glob',  same  as  --exclude='glob'.   When
                  `glob'  contains  a  `/', full pathnames are matched.  Otherwise
                  basenames are matched.  When `glob' ends with a `/', directories
                  are     matched,     same     as     --include-dir='glob'    and
                  --exclude-dir='glob'.  A leading `/' matches the working  direc-
                  tory.   This  option  may  be  repeated and may be combined with
                  options -M, -O and -t to expand the recursive search.

           --group-separator[=SEP]
                  Use SEP as a group separator for context options -A, -B, and -C.
                  The default is a double hyphen (`--').

           -H, --with-filename
                  Always  print  the  filename  with  output  lines.   This is the
                  default when there is more than one file to search.

           -h, --no-filename
                  Never print filenames with output lines.  This  is  the  default
                  when  there is only one file (or only standard input) to search.

           --heading, -+
                  Group matches per file.  Adds a heading and a line break between
                  results from different files.

           --help [WHAT], -? [WHAT]
                  Display a help message, specifically on WHAT when specified.

           --hexdump=[1-8][b][c][h]
                  Output  matches  in 1 to 8 columns of 8 hexadecimal octets.  The
                  default is 2 columns or 16 octets per line.  Option `b'  removes
                  all  space  breaks,  `c'  removes  the character column, and `h'
                  removes the hex spacing.  Enables -X if -W or -X is  not  speci-
                  fied.

           --hidden, -.
                  Search hidden files and directories.

           --hyperlink
                  Hyperlinks  are  enabled for file names when colors are enabled.
                  Same as --colors=hl.

           -I, --ignore-binary
                  Ignore matches in binary files.  This option  is  equivalent  to
                  the --binary-files=without-match option.

           -i, --ignore-case
                  Perform  case  insensitive  matching.  By default, ugrep is case
                  sensitive.  By default, this option  applies  to  ASCII  letters
                  only.  Use options -P and -i for Unicode case insensitive match-
                  ing.

           --ignore-files[=FILE]
                  Ignore files and directories matching the  globs  in  each  FILE
                  that  is encountered in recursive searches.  The default FILE is
                  `.gitignore'.  Matching files and  directories  located  in  the
                  directory  of  a  FILE's  location  and in directories below are
                  ignored   by   temporarily   overriding   the   --exclude    and
                  --exclude-dir  globs.  Files and directories that are explicitly
                  specified as command line arguments  are  never  ignored.   This
                  option may be repeated.

           --include=GLOB
                  Search  only files whose name matches GLOB using wildcard match-
                  ing, same as -g GLOB.  GLOB can use **, *, ?, and [...] as wild-
                  cards,  and  \ to quote a wildcard or backslash character liter-
                  ally.  When GLOB contains a `/',  full  pathnames  are  matched.
                  Otherwise  basenames  are  matched.   When GLOB ends with a `/',
                  directories are included as if --include-dir is specified.  Oth-
                  erwise  files  are  included.  Note that --exclude patterns take
                  priority over --include patterns.  GLOB should be quoted to pre-
                  vent shell globbing.  This option may be repeated.

           --include-dir=GLOB
                  Only  directories whose name matches GLOB are included in recur-
                  sive searches, same as -g GLOB/.  GLOB can use  **,  *,  ?,  and
                  [...] as wildcards, and \ to quote a wildcard or backslash char-
                  acter literally.  When GLOB contains a `/', full  pathnames  are
                  matched.    Otherwise   basenames   are   matched.    Note  that
                  --exclude-dir patterns take  priority  over  --include-dir  pat-
                  terns.   GLOB  should be quoted to prevent shell globbing.  This
                  option may be repeated.

           --include-from=FILE
                  Read the globs from FILE and search only files  and  directories
                  whose  name  matches  one  or  more  globs  (as  if specified by
                  --include and --include-dir).  Lines starting  with  a  `#'  and
                  empty  lines  in FILE are ignored.  When FILE is a `-', standard
                  input is read.  This option may be repeated.

           --include-fs=MOUNTS
                  Only file systems specified by MOUNTS are included in  recursive
                  searches.   MOUNTS  is a comma-separated list of mount points or
                  pathnames  of  directories  on  file  systems.    --include-fs=.
                  restricts  recursive  searches to the file system of the working
                  directory only.  Note that  --exclude-fs  mounts  take  priority
                  over --include-fs mounts.  This option may be repeated.

           -J NUM, --jobs=NUM
                  Specifies  the  number  of  threads spawned to search files.  By
                  default an optimum number of threads is spawned to search  files
                  simultaneously.   -J1  disables threading: files are searched in
                  the same order as specified.

           -j, --smart-case
                  Perform case insensitive matching like option -i, unless a  pat-
                  tern is specified with a literal ASCII upper case letter.

           --json Output file matches in JSON.  If -H, -n, -k, or -b is specified,
                  additional values are output.  See also options --format and -u.

           -K FIRST[,LAST], --range=FIRST[,LAST]
                  Start searching at line FIRST, stop at line LAST when specified.

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

           --label=LABEL
                  Displays  the LABEL value when input is read from standard input
                  where a file name would normally be printed in the output.   As-
                  sociates a filename extension with standard input when LABEL has
                  a suffix.  The default value is `(standard input)'.

           --line-buffered
                  Force output to be line buffered instead of block buffered.

           -M MAGIC, --file-magic=MAGIC
                  Only files matching the signature pattern  MAGIC  are  searched.
                  The  signature "magic bytes" at the start of a file are compared
                  to the MAGIC regex pattern.  When matching,  the  file  will  be
                  searched.   When MAGIC is preceded by a `!' or a `^', skip files
                  with matching MAGIC signatures.  This option may be repeated and
                  may  be  combined  with  options -O and -t to expand the search.
                  Every file on the search path is read,  making  searches  poten-
                  tially more expensive.

           -m NUM, --max-count=NUM
                  Stop reading the input after NUM matches in each input file.

           --match
                  Match all input.  Same as specifying an empty pattern to search.

           --max-files=NUM
                  Restrict the number of files matched to NUM.  Note  that  --sort
                  or  -J1  may  be  specified  to  produce replicable results.  If
                  --sort is specified, the number of threads spawned is limited to
                  NUM.

           --mmap[=MAX]
                  Use  memory  maps  to search files.  By default, memory maps are
                  used under certain conditions to improve performance.  When  MAX
                  is specified, use up to MAX mmap memory per thread.

           -N PATTERN, --neg-regexp=PATTERN
                  Specify  a negative PATTERN used during the search of the input:
                  an input line is selected only if it matches any of  the  speci-
                  fied  patterns  unless  a  subpattern  of  PATTERN.   Same as -e
                  (?^PATTERN).  Negative PATTERN matches are  essentially  removed
                  before  any  other  patterns are matched.  Note that longer pat-
                  terns take precedence over shorter patterns.  This option may be
                  repeated.

           -n, --line-number
                  Each  output line is preceded by its relative line number in the
                  file, starting at line 1.  The line number counter is reset  for
                  each file processed.

           --no-group-separator
                  Removes  the  group  separator  line from the output for context
                  options -A, -B, and -C.

           --not [-e] PATTERN
                  Specifies that PATTERN should not match.  Note that -e  A  --not
                  -e  B  matches  lines with `A' or lines without a `B'.  To match
                  lines with `A' that have no `B', specify -e  A  --andnot  -e  B.
                  Option  --stats  displays the search patterns applied.  See also
                  options --and, --andnot, and --bool.

           -O EXTENSIONS, --file-extension=EXTENSIONS
                  Search only files whose filename extensions match the  specified
                  comma-separated  list  of  EXTENSIONS, same as --include='*.ext'
                  for each `ext' in EXTENSIONS.  When an `ext' is  preceded  by  a
                  `!'  or  a  `^',  skip  files  whose filename extensions matches
                  `ext', same as --exclude='*.ext'.  This option may  be  repeated
                  and  may  be  combined  with options -g, -M and -t to expand the
                  recursive search.

           -o, --only-matching
                  Print only the matching part  of  lines.   When  multiple  lines
                  match,  the  line numbers with option -n are displayed using `|'
                  as the field separator for each additional line matched  by  the
                  pattern.   If  -u is specified, ungroups multiple matches on the
                  same line.  This option cannot be combined with options -A,  -B,
                  -C, -v, and -y.

           --only-line-number
                  The line number of the matching line in the file is output with-
                  out displaying the match.  The line number counter is reset  for
                  each file processed.

           -P, --perl-regexp
                  Interpret PATTERN as a Perl regular expression using PCRE2.

           -p, --no-dereference
                  If  -R  or -r is specified, no symbolic links are followed, even
                  when they are specified on the command line.

           --pager[=COMMAND]
                  When output is sent  to  the  terminal,  uses  COMMAND  to  page
                  through  the output.  The default COMMAND is `less -R'.  Enables
                  --heading and --line-buffered.

           --pretty
                  When output is sent to a terminal, enables  --color,  --heading,
                  -n, --sort and -T when not explicitly disabled or set.

           -Q[DELAY], --query[=DELAY]
                  Query  mode:  user  interface  to  perform interactive searches.
                  This mode requires an ANSI capable terminal.  An optional  DELAY
                  argument  may  be  specified  to reduce or increase the response
                  time to execute searches after the last key press, in increments
                  of  100ms,  where  the default is 5 (0.5s delay).  No whitespace
                  may be given between -Q and its argument  DELAY.   Initial  pat-
                  terns  may be specified with -e PATTERN, i.e. a PATTERN argument
                  requires option -e.  Press F1 or CTRL-Z to view the help screen.
                  Press  F2 or CTRL-Y to invoke a command to view or edit the file
                  shown at the top of the screen.  The command  can  be  specified
                  with option --view, or defaults to environment variable PAGER if
                  defined, or EDITOR.  Press Tab and Shift-Tab to navigate  direc-
                  tories  and  to  select a file to search.  Press Enter to select
                  lines to output.  Press ALT-l for option -l to list files, ALT-n
                  for  -n,  etc.   Non-option  commands  include ALT-] to increase
                  fuzziness and ALT-} to  increase  context.   Enables  --heading.
                  See also options --confirm and --view.

           -q, --quiet, --silent
                  Quiet mode: suppress all output.  ugrep will only search until a
                  match has been found.

           -R, --dereference-recursive
                  Recursively read all files under  each  directory.   Follow  all
                  symbolic  links,  unlike  -r.   When -J1 is specified, files are
                  searched in the same order as specified.  Note that when no FILE
                  arguments  are  specified  and  input  is  read from a terminal,
                  recursive searches are performed as if -R is specified.

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

           --save-config[=FILE]
                  Save configuration FILE.  By default `.ugrep' is saved.  If FILE
                  is a `-', write the configuration to standard output.

           --separator[=SEP]
                  Use  SEP as field separator between file name, line number, col-
                  umn number, byte offset, and the matched line.  The default is a
                  colon (`:').

           --sort[=KEY]
                  Displays  matching files in the order specified by KEY in recur-
                  sive searches.  KEY can be `name' to sort by pathname (default),
                  `best'  to sort by best match with option -Z (sort by best match
                  requires two passes over the input files),  `size'  to  sort  by
                  file size, `used' to sort by last access time, `changed' to sort
                  by last modification time, and `created'  to  sort  by  creation
                  time.   Sorting  is  reversed  with  `rname',  `rbest', `rsize',
                  `rused', `rchanged', or `rcreated'.  Archive  contents  are  not
                  sorted.   Subdirectories are sorted and displayed after matching
                  files.  FILE arguments are searched in the same order as  speci-
                  fied.  Normally ugrep displays matches in no particular order to
                  improve performance.

           --stats
                  Output  statistics  on  the  number  of  files  and  directories
                  searched, and the inclusion and exclusion constraints applied.

           -T, --initial-tab
                  Add  a  tab space to separate the file name, line number, column
                  number, and byte offset with the matched line.

           -t TYPES, --file-type=TYPES
                  Search only files associated with TYPES, a comma-separated  list
                  of  file types.  Each file type corresponds to a set of filename
                  extensions passed to option -O.  For capitalized file types, the
                  search is expanded to include files with matching file signature
                  magic bytes, as if passed to option -M.  When a type is preceded
                  by  a  `!' or a `^', excludes files of the specified type.  This
                  option may be repeated.  The possible file types can  be  (where
                  -tlist  displays a detailed list): `actionscript', `ada', `asm',
                  `asp', `aspx', `autoconf', `automake',  `awk',  `Awk',  `basic',
                  `batch', `bison', `c', `c++', `clojure', `csharp', `css', `csv',
                  `dart', `Dart', `delphi',  `elisp',  `elixir',  `erlang',  `for-
                  tran',  `gif',  `Gif', `go', `groovy', `gsp', `haskell', `html',
                  `jade', `java', `jpeg', `Jpeg', `js',  `json',  `jsp',  `julia',
                  `kotlin',  `less',  `lex',  `lisp',  `lua', `m4', `make', `mark-
                  down', `matlab',  `node',  `Node',  `objc',  `objc++',  `ocaml',
                  `parrot',  `pascal', `pdf', `Pdf', `perl', `Perl', `php', `Php',
                  `png', `Png', `prolog', `python', `Python', `r',  `rpm',  `Rpm',
                  `rst',  `rtf', `Rtf', `ruby', `Ruby', `rust', `scala', `scheme',
                  `shell', `Shell', `smalltalk',  `sql',  `svg',  `swift',  `tcl',
                  `tex',  `text',  `tiff',  `Tiff', `tt', `typescript', `verilog',
                  `vhdl', `vim', `xml', `Xml', `yacc', `yaml'.

           --tabs[=NUM]
                  Set the tab size to NUM to expand tabs for option -k.  The value
                  of NUM may be 1, 2, 4, or 8.  The default tab size is 8.

           --tag[=TAG[,END]]
                  Disables  colors to mark up matches with TAG.  END marks the end
                  of a match if specified, otherwise TAG.  The default is `___'.

           -U, --binary
                  Disables Unicode matching for binary file matching, forcing PAT-
                  TERN  to  match  bytes, not Unicode characters.  For example, -U
                  '\xa3' matches byte A3 (hex) instead of the Unicode  code  point
                  U+00A3 represented by the UTF-8 sequence C2 A3.  See also option
                  --dotall.

           -u, --ungroup
                  Do not group multiple pattern matches on the same matched  line.
                  Output the matched line again for each additional pattern match,
                  using `+' as the field separator.

           -V, --version
                  Display version information and exit.

           -v, --invert-match
                  Selected lines are those not matching any of the specified  pat-
                  terns.

           --view[=COMMAND]
                  Use  COMMAND  to  view/edit  a  file in query mode when pressing
                  CTRL-Y.

           -W, --with-hex
                  Output binary  matches  in  hexadecimal,  leaving  text  matches
                  alone.  This option is equivalent to the --binary-files=with-hex
                  option.

           -w, --word-regexp
                  The PATTERN is searched for as a word, such  that  the  matching
                  text  is  preceded  by a non-word character and is followed by a
                  non-word character.  Word characters are  letters,  digits,  and
                  the  underscore.   With  option  -P, word characters are Unicode
                  letters, digits, and underscore.  This option has no  effect  if
                  -x  is also specified.  If a PATTERN is specified, or -e PATTERN
                  or -N PATTERN, then this option has no effect on  -f  FILE  pat-
                  terns  to allow -f FILE patterns to narrow or widen the scope of
                  the PATTERN search.

           -X, --hex
                  Output matches in hexadecimal.  This option is equivalent to the
                  --binary-files=hex option.  See also option --hexdump.

           -x, --line-regexp
                  Select  only those matches that exactly match the whole line, as
                  if the patterns are surrounded by ^ and  $.   If  a  PATTERN  is
                  specified,  or -e PATTERN or -N PATTERN, then this option has no
                  effect on -f FILE patterns to allow -f FILE patterns  to  narrow
                  or widen the scope of the PATTERN search.

           --xml  Output  file matches in XML.  If -H, -n, -k, or -b is specified,
                  additional values are output.  See also options --format and -u.

           -Y, --empty
                  Permits  empty matches.  By default, empty matches are disabled,
                  unless a pattern begins with `^' or ends with  `$'.   With  this
                  option,  empty-matching  patterns  such  as x? and x*, match all
                  input, not only lines containing the character `x'.

           -y, --any-line
                  Any matching or non-matching line is output.  Non-matching lines
                  are  output  with  the  `-' separator as context of the matching
                  lines.  See also options -A, -B, and -C.

           -Z[[+-~]MAX], --fuzzy[=[+-~]MAX]
                  Fuzzy  mode:  report  approximate  pattern  matches  within  MAX
                  errors.   By  default, MAX is 1: one deletion, insertion or sub-
                  stitution is allowed.  When `+' and/or  `-'  precede  MAX,  only
                  insertions and/or deletions are allowed, respectively.  When `~'
                  precedes MAX, substitution counts as one  error.   For  example,
                  -Z+~3  allows  up  to  three insertions or substitutions, but no
                  deletions.  The first character of an approximate  match  always
                  matches  the  begin  of  a  pattern.   Option --sort=best orders
                  matching files by  best  match.   No  whitespace  may  be  given
                  between -Z and its argument.

           -z, --decompress
                  Decompress  files  to search, when compressed.  Archives (.cpio,
                  .pax, .tar and .zip) and compressed archives (e.g.  .taz,  .tgz,
                  .tpz,  .tbz,  .tbz2, .tb2, .tz2, .tlz, .txz, .tzst) are searched
                  and matching pathnames  of  files  in  archives  are  output  in
                  braces.   If  -g,  -O,  -M,  or  -t is specified, searches files
                  within archives whose name  matches  globs,  matches  file  name
                  extensions,  matches file signature magic bytes, or matches file
                  types, respectively.  Supported compression formats: gzip (.gz),
                  compress  (.Z),  zip,  bzip2 (requires suffix .bz, .bz2, .bzip2,
                  .tbz, .tbz2, .tb2, .tz2), lzma and xz  (requires  suffix  .lzma,
                  .tlz,  .xz,  .txz),  lz4  (requires suffix .lz4), zstd (requires
                  suffix .zst, .zstd, .tzst).

           -0, --null
                  Prints a zero-byte (NUL) after the file name.  This  option  can
                  be  used  with commands such as `find -print0' and `xargs -0' to
                  process arbitrary file names.

           A `--' signals the end of options; the rest of the parameters are  FILE
           arguments, allowing filenames to begin with a `-' character.

           Long options may start with `--no-' to disable, when applicable.

           The  regular expression pattern syntax is an extended form of the POSIX
           ERE syntax.  For an overview of the syntax see README.md or visit:

                  https://github.com/Genivia/ugrep

           Note that `.' matches any non-newline character.  Pattern `\n'  matches
           a  newline character.  Multiple lines may be matched with patterns that
           match one or more newline characters.

    EXIT STATUS
           The ugrep utility exits with one of the following values:

           0      One or more lines were selected.

           1      No lines were selected.

           >1     An error occurred.

           If -q or --quiet or --silent is used and a line is selected,  the  exit
           status is 0 even if an error occurred.

    CONFIGURATION
           The  ug command is intended for context-dependent interactive searching
           and is equivalent to the ugrep --config command  to  load  the  default
           configuration file `.ugrep' when present in the working directory or in
           the home directory.

           A configuration file contains `NAME=VALUE' pairs per line, where `NAME`
           is  the  name  of a long option (without `--') and `=VALUE' is an argu-
           ment, which is optional and may be omitted  depending  on  the  option.
           Empty lines and lines starting with a `#' are ignored.

           The  --config=FILE  option  and  its  abbreviated form ---FILE load the
           specified configuration file located in the working directory or,  when
           not  found,  located  in the home directory.  An error is produced when
           FILE is not found or cannot be read.

           Command line options are parsed in the following order: the  configura-
           tion  file is loaded first, followed by the remaining options and argu-
           ments on the command line.

           The --save-config option saves a `.ugrep'  configuration  file  to  the
           working  directory  with  a subset of the current options.  The --save-
           config=FILE option saves the configuration to FILE.  The  configuration
           is written to standard output when FILE is a `-'.

    GLOBBING
           Globbing  is  used  by options -g, --include, --include-dir, --include-
           from, --exclude, --exclude-dir, --exclude-from to match  pathnames  and
           basenames  in  recursive  searches.   Glob  arguments for these options
           should be quoted to prevent shell globbing.

           Globbing supports  gitignore  syntax  and  the  corresponding  matching
           rules.   When a glob ends in a path separator it matches directories as
           if --include-dir or --exclude-dir is specified.  When a glob contains a
           path  separator `/', the full pathname is matched.  Otherwise the base-
           name of a file or directory is matched.  For example, *.h matches foo.h
           and  bar/foo.h.   bar/*.h  matches  bar/foo.h  but  not  foo.h  and not
           bar/bar/foo.h.  Use a leading `/' to force /*.h to match foo.h but  not
           bar/foo.h.

           When  a  glob  starts  with  a `^' or a `!' as in -g^GLOB, the match is
           negated.  Likewise, a `!' (but not a `^') may be used with globs in the
           files  specified  --include-from, --exclude-from, and --ignore-files to
           negate the glob match.  Empty lines or lines starting with  a  `#'  are
           ignored.

           Glob Syntax and Conventions

           *      Matches anything except a /.

           ?      Matches any one character except a /.

           [a-z]  Matches one character in the selected range of characters.

           [^a-z] Matches one character not in the selected range of characters.

           [!a-z] Matches one character not in the selected range of characters.

           /      When  used at the begin of a glob, matches if pathname has no /.
                  When used at the end of a glob, matches directories only.

           **/    Matches zero or more directories.

           /**    When used at the end of a glob, matches everything after the  /.

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

           Note  that  exclude  glob patterns take priority over include glob pat-
           terns  when  specified  with  options  -g,  --exclude,   --exclude-dir,
           --include and include-dir.

           Glob  patterns specified with prefix `!' in any of the files associated
           with --include-from, --exclude-from and --ignore-files  will  negate  a
           previous  glob match.  That is, any matching file or directory excluded
           by a previous glob pattern  specified  in  the  files  associated  with
           --exclude-from  or --ignore-file will become included again.  Likewise,
           any matching file or directory included  by  a  previous  glob  pattern
           specified  in  the  files  associated  with  --include-from will become
           excluded again.

    ENVIRONMENT
           GREP_PATH
                  May be used to specify a file path to pattern files.   The  file
                  path  is used by option -f to open a pattern file, when the pat-
                  tern file does not exist.

           GREP_COLOR
                  May be used to specify ANSI SGR parameters to highlight  matches
                  when  option --color is used, e.g. 1;35;40 shows pattern matches
                  in bold magenta text on a black background.  Deprecated in favor
                  of GREP_COLORS, but still supported.

           GREP_COLORS
                  May  be used to specify ANSI SGR parameters to highlight matches
                  and other attributes when option --color is used.  Its value  is
                  a  colon-separated  list of ANSI SGR parameters that defaults to
                  cx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36.   The  mt=,
                  ms=,  and  mc=  capabilities  of  GREP_COLORS take priority over
                  GREP_COLOR.  Option --colors takes priority over GREP_COLORS.

    GREP_COLORS
           Colors are specified as string of colon-separated ANSI  SGR  parameters
           of  the  form  `what=substring', where `substring' is a semicolon-sepa-
           rated list of ANSI SGR codes or `k' (black), `r'  (red),  `g'  (green),
           `y'  (yellow),  `b'  (blue),  `m'  (magenta),  `c' (cyan), `w' (white).
           Upper case specifies background colors.  A `+'  qualifies  a  color  as
           bright.   A  foreground and a background color may be combined with one
           or more font properties `n' (normal), `f' (faint), `h' (highlight), `i'
           (invert), `u' (underline).  Substrings may be specified for:

           sl=    SGR substring for selected lines.

           cx=    SGR substring for context lines.

           rv     Swaps the sl= and cx= capabilities when -v is specified.

           mt=    SGR substring for matching text in any matching line.

           ms=    SGR  substring  for  matching text in a selected line.  The sub-
                  string mt= by default.

           mc=    SGR substring for matching text in a  context  line.   The  sub-
                  string mt= by default.

           fn=    SGR substring for filenames.

           ln=    SGR substring for line numbers.

           cn=    SGR substring for column numbers.

           bn=    SGR substring for byte offsets.

           se=    SGR substring for separators.

           rv     a Boolean parameter, switches sl= and cx= with option -v.

           hl     a  Boolean parameter, enables filename hyperlinks (\33]8;;link).

           ne     a Boolean parameter, disables ``erase in line'' \33[K.

    FORMAT
           Option --format=FORMAT specifies an output  format  for  file  matches.
           Fields may be used in FORMAT, which expand into the following values:

           %[ARG]F
                  if option -H is used: ARG, the file pathname and separator.

           %f     the file pathname.

           %a     the file basename without directory path.

           %p     the directory path to the file.

           %z     the file pathname in a (compressed) archive.

           %[ARG]H
                  if option -H is used: ARG, the quoted pathname and separator.

           %h     the quoted file pathname.

           %[ARG]N
                  if option -n is used: ARG, the line number and separator.

           %n     the line number of the match.

           %[ARG]K
                  if option -k is used: ARG, the column number and separator.

           %k     the column number of the match.

           %[ARG]B
                  if option -b is used: ARG, the byte offset and separator.

           %b     the byte offset of the match.

           %[ARG]T
                  if option -T is used: ARG and a tab character.

           %t     a tab character.

           %[SEP]$
                  set field separator to SEP for the rest of the format fields.

           %[ARG]<
                  if the first match: ARG.

           %[ARG]>
                  if not the first match: ARG.

           %,     if not the first match: a comma, same as %[,]>.

           %:     if not the first match: a colon, same as %[:]>.

           %;     if not the first match: a semicolon, same as %[;]>.

           %|     if not the first match: a vertical bar, same as %[|]>.

           %[ARG]S
                  if not the first match: ARG and separator, see also %$.

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

           %Z     the edit distance cost of an approximate match with option -Z

           %u     select unique lines only, unless option -u is used.

           %1     the first regex group capture of the match,  and  so  on  up  to
                  group %9, same as %[1]#; requires option -P.

           %[NUM]#
                  the regex group capture NUM; requires option -P.

           %[NUM1|NUM2|...]#
                  the first group capture NUM that matched; requires option -P.

           %[NAME]#
                  the  NAMEd  group capture; requires option -P and capturing pat-
                  tern `(?<NAME>PATTERN)', see also %G.

           %[NAME1|NAME2|...]#
                  the first NAMEd group capture that matched; requires  option  -P
                  and capturing pattern `(?<NAME>PATTERN)', see also %G.

           %G     list  of  group  capture  indices/names  that  matched; requires
                  option -P.

           %[TEXT1|TEXT2|...]G
                  list of TEXT indexed by  group  capture  indices  that  matched;
                  requires option -P.

           %g     the group capture index/name matched or 1; requires option -P.

           %[TEXT1|TEXT2|...]g
                  the  first  TEXT  indexed  by the first group capture index that
                  matched; requires option -P.

           %%     the percentage sign.

           Formatted output is written without a terminating newline, unless %~ or
           `\n' is explicitly specified in the format string.

           The  [ARG]  part  of  a  field  is  optional  and may be omitted.  When
           present, the argument must be placed in [] brackets, for example  %[,]F
           to output a comma, the pathname, and a separator.

           %[SEP]$ and %u are switches and do not send anything to the output.

           The  separator  used by the %F, %H, %N, %K, %B, %S and %G fields may be
           changed by preceding the field by %[SEP]$.  When [SEP] is not provided,
           this  reverts  the  separator to the default separator or the separator
           specified with --separator.

           Formatted output is written for each matching pattern, which means that
           a  line may be output multiple times when patterns match more than once
           on the same line.  If field  %u  is  specified  anywhere  in  a  format
           string,  matching  lines  are  output  only  once,  unless  option  -u,
           --ungroup is specified or when more than one line of input matched  the
           search pattern.

           Additional formatting options:

           --format-begin=FORMAT
                  the FORMAT when beginning the search.

           --format-open=FORMAT
                  the FORMAT when opening a file and a match was found.

           --format-close=FORMAT
                  the FORMAT when closing a file and a match was found.

           --format-end=FORMAT
                  the FORMAT when ending the search.

           The  context  options  -A,  -B,  -C,  -y,  and display options --break,
           --heading, --color, -T, and --null have no effect on formatted  output.

    EXAMPLES
           Display lines containing the word `patricia' in `myfile.txt':

                  $ ugrep -w patricia myfile.txt

           Display lines containing the word `patricia', ignoring case:

                  $ ugrep -wi patricia myfile.txt

           Display lines approximately matching the word `patricia', ignoring case
           and allowing up to 2 spelling errors using fuzzy search:

                  $ ugrep -Z2 -wi patricia myfile.txt

           Count the number of lines containing `patricia', ignoring case:

                  $ ugrep -cwi patricia myfile.txt

           Count the number of words `patricia', ignoring case:

                  $ ugrep -cowi patricia myfile.txt

           List lines with both `amount' and a decimal number, ignoring case:

                  $ ugrep -wi --bool 'amount +(.+)?' myfile.txt

           Alternative query:

                  $ ugrep -wi -e amount --and '+(.+)?' myfile.txt

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

                  $ ugrep -C1 -R -n -k -tc++ FIXME

           List the C/C++ comments in a file with line numbers:

                  $ ugrep -n -e '//.*' -e '/\*([^*]|(\*+[^*/]))*\*+\/' myfile.cpp

           The same, but using predefined pattern c++/comments:

                  $ ugrep -n -f c++/comments myfile.cpp

           List the lines that need fixing in a C/C++ source file by  looking  for
           the word `FIXME' while skipping any `FIXME' in quoted strings:

                  $ ugrep -e FIXME -N '"(\\.|\\\r?\n|[^\\\n"])*"' myfile.cpp

           The same, but using predefined pattern cpp/zap_strings:

                  $ ugrep -e FIXME -f cpp/zap_strings myfile.cpp

           Find lines with `FIXME' or `TODO':

                  $ ugrep -n -e FIXME -e TODO myfile.cpp

           Find lines with `FIXME' that also contain the word `urgent':

                  $ ugrep -n FIXME myfile.cpp | ugrep -w urgent

           Find lines with `FIXME' but not the word `later':

                  $ ugrep -n FIXME myfile.cpp | ugrep -v -w later

           Output a list of line numbers of lines with `FIXME' but not `later':

                  $ ugrep -n FIXME myfile.cpp | ugrep -vw later |
                    ugrep -P '^(\d+)' --format='%,%n'

           Find lines with `FIXME' in the C/C++ files stored in a tarball:

                  $ ugrep -z -tc++ -n FIXME project.tgz

           Recursively  find  lines with `FIXME' in C/C++ files, but do not search
           any `bak' and `old' directories:

                  $ ugrep -n FIXME -tc++ -g^bak/,^old/

           Recursively search for the word `copyright' in cpio/jar/pax/tar/zip ar-
           chives, compressed and regular files, and in PDFs using a PDF filter:

                  $ ugrep -z -w --filter='pdf:pdftotext % -' copyright

           Match  the  binary  pattern `A3hhhhA3hh' (hex) in a binary file without
           Unicode pattern matching -U (which would otherwise match  `\xaf'  as  a
           Unicode  character  U+00A3  with UTF-8 byte sequence C2 A3) and display
           the results in hex with -X using `less -R' as a pager:

                  $ ugrep --pager -UXo '\xa3[\x00-\xff]{2}\xa3[\x00-\xff]' a.out

           Hexdump an entire file:

                  $ ugrep -X '' a.out

           List all files that are not ignored by one or more `.gitignore':

                  $ ugrep -l '' --ignore-files

           List all files containing a RPM signature, located in the `rpm'  direc-
           tory and recursively below up to two levels deeper (3 levels total):

                  $ ugrep -3 -l -tRpm '' rpm/

           Monitor  the system log for bug reports and ungroup multiple matches on
           a line:

                  $ tail -f /var/log/system.log | ugrep -u -i -w bug

           Interactive fuzzy search with Boolean search queries:

                  $ ugrep -Q --bool -Z3 --sort=best

           Display all words in a MacRoman-encoded file that has CR newlines:

                  $ ugrep --encoding=MACROMAN '\w+' mac.txt

           Display all options related to "fuzzy" searching:

                  $ ugrep --help fuzzy

    BUGS
           Report bugs at:

                  https://github.com/Genivia/ugrep/issues


    LICENSE
           ugrep is released under the BSD-3 license.  All parts of  the  software
           have  reasonable  copyright terms permitting free redistribution.  This
           includes the ability to reuse all or parts of the ugrep source tree.

    SEE ALSO
           grep(1).



    ugrep 3.3.4                      June 22, 2021                        UGREP(1)

🔝 [Back to table of contents](#toc)

<a name="patterns"/>

Regex patterns
--------------

For PCRE regex patterns with option `-P`, please see the PCRE documentation
<https://www.pcre.org/original/doc/html/pcrepattern.html>.  The pattern syntax
has more features than the pattern syntax described below.  For the patterns in
common the syntax and meaning are the same.

Note that `\s` and inverted bracket lists `[^...]` are modified to prevent
matching newlines `\n` to replicate the behavior of grep.

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
  `\0141`   | matches an 8-bit character with octal value `141`, i.e. `a`
  `\x7f`    | matches an 8-bit character with hexadecimal value `7f`
  `\x{3B1}` | matches Unicode character U+03B1, i.e. `α`
  `\u{3B1}` | matches Unicode character U+03B1, i.e. `α`
  `\o{141}` | matches Unicode character U+0061, i.e. `a`, in octal
  `\p{C}`   | matches a character in Unicode category C
  `\Q...\E` | matches the quoted content between `\Q` and `\E` literally
  `[abc]`   | matches one of `a`, `b`, or `c`
  `[0-9]`   | matches a digit `0` to `9`
  `[^0-9]`  | matches any character except a digit and excluding newline `\n`
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
  `(?=φ)`   | matches `φ` without consuming it, i.e. lookahead (without option `-P`: nothing may occur after `(?=φ)`)
  `(?^φ)`   | matches `φ` and ignores it, marking everything in the pattern as a non-match
  `^φ`      | matches `φ` at the begin of input or begin of a line (nothing may occur before `^`)
  `φ$`      | matches `φ` at the end of input or end of a line (nothing may occur after `$`)
  `\Aφ`     | matches `φ` at the begin of input (nothing may occur before `\A`)
  `φ\z`     | matches `φ` at the end of input (nothing may occur after `\z`)
  `\bφ`     | matches `φ` starting at a word boundary (without option `-P`: nothing may occur before `\b`)
  `φ\b`     | matches `φ` ending at a word boundary (without option `-P`: nothing may occur after `\b`)
  `\Bφ`     | matches `φ` starting at a non-word boundary (without option `-P`: nothing may occur before `\B`)
  `φ\B`     | matches `φ` ending at a non-word boundary (without option `-P`: nothing may occur after `\B`)
  `\<φ`     | matches `φ` that starts a word (without option `-P`: nothing may occur before `\<`)
  `\>φ`     | matches `φ` that starts a non-word (without option `-P`: nothing may occur before `\>`)
  `φ\<`     | matches `φ` that ends a non-word (without option `-P`: nothing may occur after `\<`)
  `φ\>`     | matches `φ` that ends a word (without option `-P`: nothing may occur after `\>`)
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

🔝 [Back to table of contents](#toc)

<a name="posix-classes"/>

### POSIX and Unicode character classes

Character classes in bracket lists represent sets of characters.  Sets can be
negated (inverted), subtracted, intersected, and merged (not supported by PCRE2
with option `-P`):

  Pattern           | Matches
  ----------------- | ---------------------------------------------------------
  `[a-zA-Z]`        | matches a letter
  `[^a-zA-Z]`       | matches a non-letter (character class negation), newlines are not matched
  `[a-z−−[aeiou]]`  | matches a consonant (character class subtraction)
  `[a-z&&[^aeiou]]` | matches a consonant (character class intersection)
  `[a-z⎮⎮[A-Z]]`    | matches a letter (character class union)

Bracket lists cannot be empty, so `[]` and `[^]` are invalid.  In fact, the
first character after the bracket is always part of the list.  So `[][]` is a
list that matches a `]` and a `[`, `[^][]` is a list that matches anything but
`]` and `[`, and `[-^]` is a list that matches a `-` and a `^`.

Negated character classes such as `[^a-z]` do not match newlines for
compatibility with traditional grep pattern matching.

🔝 [Back to table of contents](#toc)

<a name="posix-categories"/>

### POSIX and Unicode character categories

  POSIX form   | POSIX category    | Matches
  ------------ | ----------------- | ---------------------------------------------
  `[:ascii:]`  | `\p{ASCII}`       | matches an ASCII character U+0000 to U+007F
  `[:space:]`  |                   | matches a white space character `[ \t\n\v\f\r]` or `\p{Space}` with `-P`
  `[:xdigit:]` | `\p{Xdigit}`      | matches a hex digit `[0-9A-Fa-f]`
  `[:cntrl:]`  | `\p{Cntrl}`       | matches a control character `[\x00-\0x1f\x7f]`
  `[:print:]`  | `\p{Print}`       | matches a printable character `[\x20-\x7e]`
  `[:alnum:]`  | `\p{Alnum}`       | matches a alphanumeric character `[0-9A-Za-z]` or `[\p{L}\p{N}]` with `-P`
  `[:alpha:]`  | `\p{Alpha}`       | matches a letter `[A-Za-z]` or `\p{L}` with `-P`
  `[:blank:]`  | `\p{Blank}`, `\h` | matches a blank `[ \t]` or horizontal space with `-P`
  `[:digit:]`  | `\p{Digit}`       | matches a digit `[0-9]` or `\p{Nd}` with `-P`
  `[:graph:]`  | `\p{Graph}`       | matches a visible character `[\x21-\x7e]`
  `[:lower:]`  |                   | matches a lower case letter `[a-z]` or `\p{Ll}` with `-P`
  `[:punct:]`  | `\p{Punct}`       | matches a punctuation character `[\x21-\x2f\x3a-\x40\x5b-\x60\x7b-\x7e]`
  `[:upper:]`  |                   | matches an upper case letter `[A-Z]` or `\p{Lu}` with `-P`
  `[:word:]`   |                   | matches a word character `[0-9A-Za-z_]` or `[\p{L}\p{N}_]` with `-P`
  `[:^blank:]` | `\P{Blank}`, `\H` | matches a non-blank character including newline `\n`
  `[:^digit:]` | `\P{Digit}`       | matches a non-digit including newline `\n`

The POSIX form can only be used in bracket lists, for example
`[[:lower:][:digit:]]` matches an ASCII lower case letter or a digit.  

You can also use the upper case `\P{C}` form that has the same meaning as
`\p{^C}`, which matches any character except characters in the class `C`.
For example, `\P{ASCII}` is the same as `\p{^ASCII}`.

Because POSIX character categories only cover ASCII, `[[:^ascii]]` is empty and
therefore invalid to use.  By contrast, `[^[:ascii]]` is a Unicode character
class that excludes the ASCII character category.

  Unicode category                       | Matches
  -------------------------------------- | ------------------------------------
  `.`                                    | matches any single Unicode character except newline `\n` unless with `--dotall`
  `\a`                                   | matches BEL U+0007
  `\d`                                   | matches a digit `[0-9]` or `\p{Nd}`
  `\D`                                   | matches a non-digit including `\n`
  `\e`                                   | matches ESC U+001b
  `\f`                                   | matches FF U+000c
  `\l`                                   | matches a lower case letter `\p{Ll}`
  `\n`                                   | matches LF U+000a
  `\N`                                   | matches a non-LF character
  `\r`                                   | matches CR U+000d
  `\R`                                   | matches a Unicode line break
  `\s`                                   | matches a white space character `[ \t\v\f\r\x85\p{Z}]` excluding `\n`
  `\S`                                   | matches a non-white space character
  `\t`                                   | matches TAB U+0009
  `\u`                                   | matches an upper case letter `\p{Lu}`
  `\v`                                   | matches VT U+000b or vertical space character with option `-P`
  `\w`                                   | matches a word character `[0-9A-Za-z_]` or `[\p{L}\p{Nd}\p{Pc}]`
  `\W`                                   | matches a non-Unicode word character
  `\X`                                   | matches any ISO-8859-1 or Unicode character
  `\p{Space}`                            | matches a white space character `[ \t\n\v\f\r\x85\p{Z}]` including `\n`
  `\p{Unicode}`                          | matches any Unicode character U+0000 to U+10FFFF minus U+D800 to U+DFFF
  `\p{ASCII}`                            | matches an ASCII character U+0000 to U+007F
  `\p{Non_ASCII_Unicode}`                | matches a non-ASCII character U+0080 to U+10FFFF minus U+D800 to U+DFFF
  `\p{L&}`                               | matches a character with Unicode property L& (i.e. property Ll, Lu, or Lt)
  `\p{Letter}`,`\p{L}`                   | matches a character with Unicode property Letter
  `\p{Mark}`,`\p{M}`                     | matches a character with Unicode property Mark
  `\p{Separator}`,`\p{Z}`                | matches a character with Unicode property Separator
  `\p{Symbol}`,`\p{S}`                   | matches a character with Unicode property Symbol
  `\p{Number}`,`\p{N}`                   | matches a character with Unicode property Number
  `\p{Punctuation}`,`\p{P}`              | matches a character with Unicode property Punctuation
  `\p{Other}`,`\p{C}`                    | matches a character with Unicode property Other
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

🔝 [Back to table of contents](#toc)

<a name="perl-syntax"/>

### Perl regular expression syntax

For the pattern syntax of **ugrep** option `-P` (Perl regular expressions), see
for example [Perl regular expression syntax](https://www.boost.org/doc/libs/1_70_0/libs/regex/doc/html/boost_regex/syntax/perl_syntax.html).
However, **ugrep** enhances the Perl regular expression syntax with all of the
features listed in [POSIX regular expression syntax](#posix-syntax).

🔝 [Back to table of contents](#toc)

<a name="bugs"/>

Troubleshooting
---------------

If something is not working, then please check the [tutorial](#tutorial) and
the [man page](#man).  If you can't find it there and it looks like a bug, then
[report an issue](https://github.com/Genivia/ugrep/issues) on GitHub.  Bug
reports are quickly addressed.

[travis-image]: https://travis-ci.com/Genivia/ugrep.svg?branch=master
[travis-url]: https://travis-ci.com/Genivia/ugrep
[lgtm-image]: https://img.shields.io/lgtm/grade/cpp/g/Genivia/ugrep.svg?logo=lgtm&logoWidth=18
[lgtm-url]: https://lgtm.com/projects/g/Genivia/ugrep/context:cpp
[bsd-3-image]: https://img.shields.io/badge/license-BSD%203--Clause-blue.svg
[bsd-3-url]: https://opensource.org/licenses/BSD-3-Clause
