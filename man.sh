#!/bin/bash

# Generates man page man/ugrep.1 from ./ugrep --help
# Robert van Engelen, Genivia Inc. All rights reserved.

if [ "$#" = 1 ]
then

if [ -x src/ugrep ] 
then

echo
echo "Creating ugrep man page"
mkdir -p man
echo '.TH UGREP "1" "'`date '+%B %d, %Y'`'" "ugrep '$1'" "User Commands"' > man/ugrep.1
cat >> man/ugrep.1 << 'END'
.SH NAME
\fBugrep\fR, \fBug\fR -- file pattern searcher
.SH SYNOPSIS
.B ugrep
[\fIOPTIONS\fR] [\fB-i\fR] [\fB-Q\fR|\fIPATTERN\fR] [\fB-e\fR \fIPATTERN\fR] [\fB-N\fR \fIPATTERN\fR] [\fB-f\fR \fIFILE\fR]
      [\fB-F\fR|\fB-G\fR|\fB-P\fR|\fB-Z\fR] [\fB-U\fR] [\fB-m\fR [\fIMIN,\fR][\fIMAX\fR]] [\fB--bool\fR [\fB--files\fR|\fB--lines\fR]]
      [\fB-r\fR|\fB-R\fR|\fB-1\fR|...|\fB-9\fR|\fB-10\fR|...] [\fB-t\fR \fITYPES\fR] [\fB-g\fR \fIGLOBS\fR] [\fB--sort\fR[=\fIKEY\fR]]
      [\fB-l\fR|\fB-c\fR] [\fB-o\fR] [\fB-n\fR] [\fB-k\fR] [\fB-b\fR] [\fB-A\fR \fINUM\fR] [\fB-B\fR \fINUM\fR] [\fB-C \fR\fINUM\fR] [\fB-y\fR]
      [\fB--color\fR[=\fIWHEN\fR]|\fB--colour\fR[=\fIWHEN\fR]] [\fB--pretty\fR] [\fB--pager\fR[=\fICOMMAND\fR]]
      [\fB--hexdump\fR|\fB--csv\fR|\fB--json\fR|\fB--xml\fR] [\fB-I\fR] [\fB-z\fR] [\fB--zmax\fR=\fINUM\fR] [\fIFILE\fR \fI...\fR]
.SH DESCRIPTION
The \fBugrep\fR utility searches any given input files, selecting files and
lines that match one or more patterns specified as regular expressions or as
fixed strings.  A pattern matches multiple input lines when the pattern's
regular expression matches one or more newlines.  An empty pattern matches
every line.  Each input line that matches at least one of the patterns is
written to the standard output.
.PP
The \fBug\fR command is intended for interactive searching, using a `.ugrep'
configuration file located in the working directory or, if not found, in the
home directory, see CONFIGURATION.  \fBug\fR is equivalent to \fBugrep --config
--pretty --sort\fR to load a configuration file, enhance the terminal output,
and to sort files by name.
.PP
The \fBugrep+\fR and \fBug+\fR commands are the same as the \fBugrep\fR and
\fBug\fR commands, but also use filters to search pdfs, documents, e-books,
and image metadata, when the corresponding filter tools are installed.
.PP
A list of matching files is produced with option \fB-l\fR
(\fB--files-with-matches\fR).  Option \fB-c\fR (\fB--count\fR) counts the
number of matching lines.  When combined with option \fB-o\fR, counts the total
number of matches.  When combined with option \fB-m1,\fR (\fB--min-count=1\fR),
skips files with zero matches.
.PP
The default pattern syntax is an extended form of the POSIX ERE syntax, same as
option \fB-E\fR (\fB--extended-regexp\fR).  Try \fBug --help regex\fR for help
with pattern syntax and how to use logical connectives to specify Boolean
search queries with option \fB-%\fR (\fB--bool\fR) to match lines and \fB-%%\fR
(\fB--bool --files\fR) to match files.  Options \fB-F\fR
(\fB--fixed-strings\fR), \fB-G\fR (\fB--basic-regexp\fR) and \fB-P\fR
(\fB--perl-regexp\fR) specify other pattern syntaxes.
.PP
Option \fB-i\fR (\fB--ignore-case\fR) ignores case in ASCII patterns.  When
combined with option \fB-P\fR, ignores case in Unicode patterns.  Option
\fB-j\fR (\fB--smart-case\fR) enables \fB-i\fR only if the search patterns are
specified
in lower case.
.PP
Fuzzy (approximate) search is specified with option \fB-Z\fR (\fB--fuzzy\fR)
with an optional argument to control character insertions, deletions, and/or
substitutions.  Try \fBug --help fuzzy\fR for help with fuzzy search.
.PP
Note that pattern `.' matches any non-newline character.  Pattern `\\n' matches
a newline character.  Multiple lines may be matched with patterns that match
one or more newline characters.
.PP
The empty pattern "" matches all lines.  Other empty-matching patterns do not.
For example, the pattern `a*' will match one or more a's.  Option \fB-Y\fR
forces empty matches for compatibility with other grep tools.  
.PP
Option \fB-f\fR \fIFILE\fR matches patterns specified in \fIFILE\fR.
.PP
By default Unicode patterns are matched.  Option \fB-U\fR (\fB--ascii\fR or
\fB--binary\fR) disables Unicode matching for ASCII and binary pattern
matching.  Non-Unicode matching is more efficient.
.PP
\fBugrep\fR accepts input of various encoding formats and normalizes the output
to UTF-8.  When a UTF byte order mark is present in the input, the input is
automatically normalized.  An input encoding format may be specified with
option \fB--encoding\fR.
.PP
If no \fIFILE\fR arguments are specified and standard input is read from a
terminal, recursive searches are performed as if \fB-r\fR is specified.  To
force reading from standard input, specify `-' as a \fIFILE\fR argument.
.PP
Directories specified as \fIFILE\fR arguments are searched without recursing
deeper into subdirectories, unless \fB-R\fR, \fB-r\fR, or \fB-2\fR...\fB-9\fR
is specified to search subdirectories recursively (up to the specified depth.)
.PP
Option \fB-I\fR (\fB--ignore-binary\fR) ignores binary files.  A binary file is
a file with non-text content.  A file with zero bytes or invalid UTF formatting
is considered binary.
.PP
Hidden files and directories are ignored in recursive searches.  Option
\fB-.\fR (\fB--hidden\fR) includes hidden files and directories in recursive
searches.
.PP
To match the names of files to search and the names of directories to recurse,
one or more of the following options may be specified.  Option \fB-O\fR
specifies one or more filename extensions to match.  Option \fB-t\fR specifies
one or more file types to search (\fB-t list\fR outputs a list of types.)
Option \fB-g\fR specifies a gitignore-style glob pattern to match filenames.
Option \fB--ignore-files\fR specifies a file with gitignore-style globs to
ignore directories and files.  Try \fBug --help globs\fR for help with filename
and directory name matching.  See also section GLOBBING.
.PP
Compressed files and archives are searched with option \fB-z\fR
(\fB--decompress\fR).  When used with option \fB--zmax\fR=\fINUM\fR, searches
the contents of compressed files and archives stored within archives up to
\fINUM\fR levels.
.PP
A query terminal user interface (TUI) is opened with \fB-Q\fR (\fB--query\fR)
to interactively specify search patterns and view search results.  A
\fIPATTERN\fR argument requires \fB-e PATTERN\fR to start the query TUI with
the specified pattern.
.PP
Output to a terminal for viewing is enhanced with \fB--pretty\fR, which is
enabled by default with the \fBug\fR command.
.PP
A terminal output pager is enabled with \fB--pager\fR.
.PP
Customized output is produced with option \fB--format\fR or \fB--replace\fR.
Try \fBug --help format\fR for help with custom formatting of the output.
Predefined formats include CSV with option \fB--csv\fR, JSON with option
\fB--json\fR, and XML with option \fB--xml\fR.  Hexdumps are output with option
\fB-X\fR (\fB--hex\fR) or with option \fB--hexdump\fR to customize hexdumps.
See also section FORMAT.
.PP
A `--' signals the end of options; the rest of the parameters are \fIFILE\fR
arguments, allowing filenames to begin with a `-' character.
.PP
Long options may start with `\FB--no-\fR' to disable, when applicable.
.PP
\fBug --help \fIWHAT\fR displays help on options related to \fIWHAT\fR.
.PP
The following options are available:
END
src/ugrep --help \
| tail -n+2 \
| sed -e 's/\([^\\]\)\\/\1\\\\/g' \
| sed \
  -e '/^$/ d' \
  -e '/^    Long options may start/ d' \
  -e '/^    The ugrep/ d' \
  -e '/^    0       / d' \
  -e '/^    1       / d' \
  -e '/^    >1      / d' \
  -e '/^    If -q or --quiet or --silent/ d' \
  -e '/^    status is 0 even/ d' \
  -e 's/^ \{5,\}//' \
  -e 's/^\.\([A-Za-z]\)/\\\&.\1/g' \
  -e $'s/^    \(.*\)$/.TP\\\n\\1/' \
  -e 's/\(--[+0-9A-Za-z_-]*\)/\\fB\1\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\) \([A-Z]\{1,\}\)/\1\\fB\2\\fR \\fI\3\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\)/\1\\fB\2\\fR/g' \
  -e 's/^\(-.\) \([!A-Z]\{1,\}\)/\\fB\1\\fR \\fI\2\\fR/g' \
  -e 's/^\(-.\)/\\fB\1\\fR/g' \
  -e 's/\[\([-A-Z]\{1,\}\),\]\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR,][\\fI\2\\fR]/g' \
  -e 's/\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR]/g' \
  -e 's/\[,\([-A-Z]\{1,\}\)\]/[,\\fI\1\\fR]/g' \
  -e 's/\[=\([-A-Z]\{1,\}\)\]/[=\\fI\1\\fR]/g' \
  -e 's/=\([-A-Z]\{1,\}\)/=\\fI\1\\fR/g' \
| sed -e 's/-/\\-/g' >> man/ugrep.1
cat >> man/ugrep.1 << 'END'
.SH "EXIT STATUS"
The \fBugrep\fR utility exits with one of the following values:
.IP 0
One or more lines were selected.
.IP 1
No lines were selected.
.IP >1
An error occurred.
.PP
If \fB-q\fR or \fB--quiet\fR or \fB--silent\fR is used and a line is selected,
the exit status is 0 even if an error occurred.
.SH CONFIGURATION
The \fBug\fR command is intended for interactive searching and is equivalent to
the \fBugrep --config --pretty --sort\fR command to load the `.ugrep`
configuration file located in the working directory or, when not found, in the
home directory.
.PP
A configuration file contains `NAME=VALUE' pairs per line, where `NAME` is the
name of a long option (without `--') and `=VALUE' is an argument, which is
optional and may be omitted depending on the option.  Empty lines and lines
starting with a `#' are ignored.
.PP
The \fB--config\fR=\fIFILE\fR option and its abbreviated form
\fB---\fR\fIFILE\fR load the specified configuration file located in the
working directory or, when not found, in the home directory.  An error is
produced when \fIFILE\fR is not found or cannot be read.
.PP
Command line options are parsed in the following order: the configuration file
is loaded first, followed by the remaining options and arguments on the command
line.
.PP
The \fB--save-config\fR option saves a `.ugrep' configuration file to the
working directory with a subset of the options specified on the command line.
Only part of the specified options are saved that do not cause searches to fail
when combined with other options.  The \fB--save-config\fR=\fIFILE\fR option
saves the configuration to \fIFILE\fR.  The configuration is written to
standard output when \fIFILE\fR is a `-'.
.SH GLOBBING
Globbing is used by options \fB-g\fR, \fB--include\fR, \fB--include-dir\fR,
\fB--include-from\fR, \fB--exclude\fR, \fB--exclude-dir\fR,
\fB--exclude-from\fR and \fB--ignore-files\fR to match pathnames and basenames
in recursive searches.  Glob arguments for these options should be quoted to
prevent shell globbing.
.PP
Globbing supports gitignore syntax and the corresponding matching rules, except
that a glob normally matches files but not directories.  If a glob ends in a
path separator `/', then it matches directories but not files, as if
\fB--include-dir\fR or \fB--exclude-dir\fR is specified.  When a glob contains
a path separator `/', the full pathname is matched.  Otherwise the basename of
a file or directory is matched.  For example, \fB*.h\fR matches foo.h and
bar/foo.h.  \fBbar/*.h\fR matches bar/foo.h but not foo.h and not
bar/bar/foo.h.  Use a leading `/' to force \fB/*.h\fR to match foo.h but not
bar/foo.h.
.PP
When a glob starts with a `^' or a `!' as in \fB-g\fR^\fIGLOB\fR, the match is
negated.  Likewise, a `!' (but not a `^') may be used with globs in the files
specified \fB--include-from\fR, \fB--exclude-from\fR, and \fB--ignore-files\fR
to negate the glob match.  Empty lines or lines starting with a `#' are
ignored.
.PP
\fBGlob Syntax and Conventions\fR
.IP \fB*\fR
Matches anything except /.
.IP \fB?\fR
Matches any one character except /.
.IP \fB[abc-e]\fR
Matches one character a,b,c,d,e.
.IP \fB[^abc-e]\fR
Matches one character not a,b,c,d,e,/.
.IP \fB[!abc-e]\fR
Matches one character not a,b,c,d,e,/.
.IP \fB/\fR
When used at the start of a glob, matches if pathname has no /.
When used at the end of a glob, matches directories only.
.IP \fB**/\fR
Matches zero or more directories.
.IP \fB/**\fR
When used at the end of a glob, matches everything after the /.
.IP \fB\e?\fR
Matches a ? or any other character specified after the backslash.
.PP
\fBGlob Matching Examples\fR
.IP \fB*\fR
Matches a, b, x/a, x/y/b
.IP \fBa\fR
Matches a, x/a, x/y/a,       but not b, x/b, a/a/b
.IP \fB/*\fR
Matches a, b,                but not x/a, x/b, x/y/a
.IP \fB/a\fR
Matches a,                   but not x/a, x/y/a
.IP \fBa?b\fR
Matches axb, ayb,            but not a, b, ab, a/b
.IP \fBa[xy]b\fR
Matches axb, ayb             but not a, b, azb
.IP \fBa[a-z]b\fR
Matches aab, abb, acb, azb,  but not a, b, a3b, aAb, aZb
.IP \fBa[^xy]b\fR
Matches aab, abb, acb, azb,  but not a, b, axb, ayb
.IP \fBa[^a-z]b\fR
Matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb
.IP \fBa/*/b\fR
Matches a/x/b, a/y/b,        but not a/b, a/x/y/b
.IP \fB**/a\fR
Matches a, x/a, x/y/a,       but not b, x/b.
.IP \fBa/**/b\fR
Matches a/b, a/x/b, a/x/y/b, but not x/a/b, a/b/x
.IP \fBa/**\fR
Matches a/x, a/y, a/x/y,     but not a, b/x
.IP \fBa\e?b\fR
Matches a?b,                 but not a, b, ab, axb, a/b
.PP
Note that exclude glob patterns take priority over include glob patterns when
specified with options -g, --exclude, --exclude-dir, --include and include-dir.
.PP
Glob patterns specified with prefix `!' in any of the files associated with
--include-from, --exclude-from and --ignore-files will negate a previous glob
match.  That is, any matching file or directory excluded by a previous glob
pattern specified in the files associated with --exclude-from or --ignore-file
will become included again.  Likewise, any matching file or directory included
by a previous glob pattern specified in the files associated with
--include-from will become excluded again.
.SH ENVIRONMENT
.IP \fBGREP_PATH\fR
May be used to specify a file path to pattern files.  The file path is used by
option \fB-f\fR to open a pattern file, when the pattern file does not exist.
.IP \fBGREP_COLOR\fR
May be used to specify ANSI SGR parameters to highlight matches when option
\fB--color\fR is used, e.g. 1;35;40 shows pattern matches in bold magenta text
on a black background.  Deprecated in favor of \fBGREP_COLORS\fR, but still
supported.
.IP \fBGREP_COLORS\fR
May be used to specify ANSI SGR parameters to highlight matches and other
attributes when option \fB--color\fR is used.  Its value is a colon-separated
list of ANSI SGR parameters that defaults to
\fBcx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36\fR with additional
parameters for TUI colors \fB:qp=1;32:qe=1;37;41:qm=1;32:ql=36:qb=1;35\fR.
The \fBmt=\fR, \fBms=\fR, and \fBmc=\fR capabilities of \fBGREP_COLORS\fR take
priority over \fBGREP_COLOR\fR.  Option \fB--colors\fR takes priority over
\fBGREP_COLORS\fR.
.SH GREP_COLORS
Colors are specified as string of colon-separated ANSI SGR parameters of the
form `what=substring', where `substring' is a semicolon-separated list of ANSI
SGR codes or `k' (black), `r' (red), `g' (green), `y' (yellow), `b' (blue), `m'
(magenta), `c' (cyan), `w' (white).  Upper case specifies background colors.  A
`+' qualifies a color as bright.  A foreground and a background color may be
combined with one or more font properties `n' (normal), `f' (faint), `h'
(highlight), `i' (invert), `u' (underline).  Substrings may be specified for:
.IP \fBsl=\fR
selected lines.
.IP \fBcx=\fR
context lines.
.IP \fBrv\fR
swaps the \fBsl=\fR and \fBcx=\fR capabilities when \fB-v\fR is specified.
.IP \fBmt=\fR
matching text in any matching line.
.IP \fBms=\fR
matching text in a selected line.  The substring \fBmt=\fR by
default.
.IP \fBmc=\fR
matching text in a context line.  The substring \fBmt=\fR by
default.
.IP \fBfn=\fR
filenames.
.IP \fBln=\fR
line numbers.
.IP \fBcn=\fR
column numbers.
.IP \fBbn=\fR
byte offsets.
.IP \fBse=\fR
separators.
.IP \fBrv\fR
a Boolean parameter, switches \fBsl=\fR and \fBcx=\fR with option \fB-v\fR.
.IP \fBhl\fR
a Boolean parameter, enables filename hyperlinks (\fB\\33]8;;link\fR).
.IP \fBne\fR
a Boolean parameter, disables ``erase in line'' \fB\\33[K\fR.
.IP \fBqp=\fR
TUI prompt.
.IP \fBqe=\fR
TUI errors.
.IP \fBqr=\fR
TUI regex.
.IP \fBqm=\fR
TUI regex meta characters.
.IP \fBql=\fR
TUI regex lists and literals.
.IP \fBqb=\fR
TUI regex braces.
.SH FORMAT
Option \fB--format\fR=\fIFORMAT\fR specifies an output format for file matches.
Fields may be used in \fIFORMAT\fR, which expand into the following values:
.IP \fB%[\fR\fITEXT\fR\fB]F\fR
if option \fB-H\fR is used: \fITEXT\fR, the file pathname and separator.
.IP \fB%f\fR
the file pathname.
.IP \fB%a\fR
the file basename without directory path.
.IP \fB%p\fR
the directory path to the file.
.IP \fB%z\fR
the file pathname in a (compressed) archive.
.IP \fB%[\fR\fITEXT\fR\fB]H\fR
if option \fB-H\fR is used: \fITEXT\fR, the quoted pathname and separator, \\"
and \\\\ replace " and \\.
.IP \fB%h\fR
the quoted file pathname, \\" and \\\\ replace " and \\.
.IP \fB%[\fR\fITEXT\fR\fB]I\fR
if option \fB-H\fR is used: \fITEXT\fR, the pathname as XML character data and separator.
.IP \fB%i\fR
the file pathname as XML character data.
.IP \fB%[\fR\fITEXT\fR\fB]N\fR
if option \fB-n\fR is used: \fITEXT\fR, the line number and separator.
.IP \fB%n\fR
the line number of the match.
.IP \fB%[\fR\fITEXT\fR\fB]K\fR
if option \fB-k\fR is used: \fITEXT\fR, the column number and separator.
.IP \fB%k\fR
the column number of the match.
.IP \fB%[\fR\fITEXT\fR\fB]B\fR
if option \fB-b\fR is used: \fITEXT\fR, the byte offset and separator.
.IP \fB%b\fR
the byte offset of the match.
.IP \fB%[\fR\fITEXT\fR\fB]T\fR
if option \fB-T\fR is used: \fITEXT\fR and a tab character.
.IP \fB%t\fR
a tab character.
.IP \fB%[\fR\fISEP\fR\fB]$\fR
set field separator to \fISEP\fR for the rest of the format fields.
.IP \fB%[\fR\fITEXT\fR\fB]<\fR
if the first match: \fITEXT\fR.
.IP \fB%[\fR\fITEXT\fR\fB]>\fR
if not the first match: \fITEXT\fR.
.IP \fB%,\fR
if not the first match: a comma, same as \fB%[,]>\fR.
.IP \fB%:\fR
if not the first match: a colon, same as \fB%[:]>\fR.
.IP \fB%;\fR
if not the first match: a semicolon, same as \fB%[;]>\fR.
.IP \fB%|\fR
if not the first match: a vertical bar, same as \fB%[|]>\fR.
.IP \fB%[\fR\fITEXT\fR\fB]S\fR
if not the first match: \fITEXT\fR and separator, see also \fB%[\fR\fISEP\fR\fB]$.
.IP \fB%s\fR
the separator, see also \fB%[\fR\fITEXT\fR\fB]S\fR and \fB%[\fR\fISEP\fR\fB]$.
.IP \fB%~\fR
a newline character.
.IP \fB%M\fR
the number of matching lines
.IP \fB%m\fR
the number of matches
.IP \fB%O\fR
the matching line is output as a raw string of bytes.
.IP \fB%o\fR
the match is output as a raw string of bytes.
.IP \fB%Q\fR
the matching line as a quoted string, \\" and \\\\ replace " and \\.
.IP \fB%q\fR
the match as a quoted string, \\" and \\\\ replace " and \\.
.IP \fB%C\fR
the matching line formatted as a quoted C/C++ string.
.IP \fB%c\fR
the match formatted as a quoted C/C++ string.
.IP \fB%J\fR
the matching line formatted as a quoted JSON string.
.IP \fB%j\fR
the match formatted as a quoted JSON string.
.IP \fB%V\fR
the matching line formatted as a quoted CSV string.
.IP \fB%v\fR
the match formatted as a quoted CSV string.
.IP \fB%X\fR
the matching line formatted as XML character data.
.IP \fB%x\fR
the match formatted as XML character data.
.IP \fB%w\fR
the width of the match, counting wide characters.
.IP \fB%d\fR
the size of the match, counting bytes.
.IP \fB%e\fR
the ending byte offset of the match.
.IP \fB%Z\fR
the edit distance cost of an approximate match with option \fB-Z\fR
.IP \fB%u\fR
select unique lines only, unless option \fB-u\fR is used.
.IP \fB%1\fR
the first regex group capture of the match, and so on up to group \fB%9\fR,
same as \fB%[1]#\fR; requires option \fB-P\fR.
.IP \fB%[\fR\fINUM\fR\fB]#\fR
the regex group capture \fINUM\fR; requires option \fB-P\fR.
.IP \fB%[\fR\fINUM\fR\fB]b\fR
the byte offset of the group capture \fINUM\fR; requires option \fB-P\fR.  Use
\fBe\fR for the ending byte offset and \fBd\fR for the byte length.
.IP \fB%[\fR\fINUM1\fR\fB|\fR\fINUM2\fR\fB|\fR...\fB]#\fR
the first group capture \fINUM\fR that matched; requires option \fB-P\fR.
.IP \fB%[\fR\fINUM1\fR\fB|\fR\fINUM2\fR\fB|\fR...\fB]b\fR
the byte offset of the first group capture \fINUM\fR that matched; requires
option \fB-P\fR.  Use \fBe\fR for the ending byte offset and \fBd\fR for the
byte length.
.IP \fB%[\fR\fINAME\fR\fB]#\fR
the \fINAME\fRd group capture; requires option \fB-P\fR and capturing pattern
`(?<NAME>PATTERN)', see also \fB%G\fR.
.IP \fB%[\fR\fINAME\fR\fB]b\fR
the byte offset of the \fINAME\fRd group capture; requires option \fB-P\fR and
capturing pattern `(?<NAME>PATTERN)'.  Use \fBe\fR for the ending byte offset
and \fBd\fR for the byte length.
.IP \fB%[\fR\fINAME1\fR\fB|\fR\fINAME2|...\fR\fB]#\fR
the first \fINAME\fRd group capture that matched; requires option \fB-P\fR
and capturing pattern `(?<NAME>PATTERN)', see also \fB%G\fR.
.IP \fB%[\fR\fINAME1\fR\fB|\fR\fINAME2|...\fR\fB]b\fR
the byte offset of the first \fINAME\fRd group capture that matched; requires
option \fB-P\fR and capturing pattern `(?<NAME>PATTERN)'.  Use \fBe\fR for the
ending byte offset and \fBd\fR for the byte length.
.IP \fB%G\fR
list of group capture indices/names that matched; requires option \fB-P\fR.
.IP \fB%[\fR\fITEXT1\fR\fB|\fR\fITEXT2\fR\fB|...]G\fR
list of \fITEXT\fR indexed by group capture indices that matched; requires option \fB-P\fR.
.IP \fB%g\fR
the group capture index/name matched or 1; requires option \fB-P\fR.
.IP \fB%[\fR\fITEXT1\fR\fB|\fR\fITEXT2\fR\fB|\fR...\fB]g\fR
the first \fITEXT\fR indexed by the first group capture index that matched; requires option \fB-P\fR.
.IP \fB%%\fR
the percentage sign.
.PP
Formatted output is written without a terminating newline, unless \fB%~\fR or
`\\n' is explicitly specified in the format string.
.PP
The \fB[\fR\fITEXT\fR\fB]\fR part of a field is optional and may be omitted.
When present, the argument must be placed in \fB[]\fR brackets, for example
\fB%[,]F\fR to output a comma, the pathname, and a separator.
.PP
\fB%[\fR\fISEP\fR\fB]$\fR and \fB%u\fR are switches and do not send anything to
the output.
.PP
The separator used by the \fB%F\fR, \fB%H\fR, \fB%I\fR, \fB%N\fR, \fB%K\fR,
\fB%B\fR, \fB%S\fR and \fB%G\fR fields may be changed by preceding the field by
\fB%[\fR\fISEP\fR\fB]$\fR.  When \fB[\fR\fISEP\fR\fB]\fR is not provided, this
reverts the separator to the default separator or the separator specified with
\fB--separator\fR.
.PP
Formatted output is written for each matching pattern, which means that a line
may be output multiple times when patterns match more than once on the same
line.  If field \fB%u\fR is specified anywhere in a format string, matching
lines are output only once, unless option \fB-u\fR, \fB--ungroup\fR is
specified or when more than one line of input matched the search pattern.
.PP
Additional formatting options:
.IP \fB--format-begin\fR=\fIFORMAT\fR
the \fIFORMAT\fR when beginning the search.
.IP \fB--format-open\fR=\fIFORMAT\fR
the \fIFORMAT\fR when opening a file and a match was found.
.IP \fB--format-close\fR=\fIFORMAT\fR
the \fIFORMAT\fR when closing a file and a match was found.
.IP \fB--format-end\fR=\fIFORMAT\fR
the \fIFORMAT\fR when ending the search.
.PP
The context options \fB-A\fR, \fB-B\fR, \fB-C\fR, \fB-y\fR, and display options
\fB--break\fR, \fB--heading\fR, \fB--color\fR, \fB-T\fR, and \fB--null\fR have
no effect on formatted output.
.SH EXAMPLES
Display lines containing the word `patricia' in `myfile.txt':
.IP
$ ugrep -w patricia myfile.txt
.PP
Display lines containing the word `patricia', ignoring case:
.IP
$ ugrep -wi patricia myfile.txt
.PP
Display lines approximately matching the word `patricia', ignoring case and
allowing up to 2 spelling errors using fuzzy search:
.IP
$ ugrep -Z2 -wi patricia myfile.txt
.PP
Count the number of lines containing `patricia', ignoring case:
.IP
$ ugrep -cwi patricia myfile.txt
.PP
Count the number of words `patricia', ignoring case:
.IP
$ ugrep -cowi patricia myfile.txt
.PP
List lines with `amount' and a decimal, ignoring case (space is AND):
.IP
$ ugrep -i -% 'amount \d+(\.\d+)?' myfile.txt
.PP
Alternative query:
.IP
$ ugrep -wi -e amount --and '\d+(\.\d+)?' myfile.txt
.PP
List all Unicode words in a file:
.IP
$ ugrep -o '\\w+' myfile.txt
.PP
List the laughing face emojis (Unicode code points U+1F600 to U+1F60F):
.IP
$ ugrep -o '[\\x{1F600}-\\x{1F60F}]' myfile.txt
.PP
Check if a file contains any non-ASCII (i.e. Unicode) characters:
.IP
$ ugrep -q '[^[:ascii:]]' myfile.txt && echo "contains Unicode"
.PP
Display the line and column number of `FIXME' in C++ files using recursive
search, with one line of context before and after a matched line:
.IP
$ ugrep -C1 -R -n -k -tc++ FIXME
.PP
Display the line and column number of `FIXME' in long Javascript files using
recursive search, showing only matches with up to 10 characters of context
before and after:
.IP
$ ugrep -o -C20 -R -n -k -tjs FIXME

.PP
Find blocks of text between lines matching BEGIN and END by using a lazy
quantifier `*?' to match only what is necessary and pattern `\\n' to match
newlines:
.IP
$ ugrep -n 'BEGIN.*\\n(.*\\n)*?.*END' myfile.txt
.PP
Likewise, list the C/C++ comments in a file and line numbers:
.IP
$ ugrep -n -e '//.*' -e '/\\*(.*\\n)*?.*\\*+\\/' myfile.cpp
.PP
The same, but using predefined pattern c++/comments:
.IP
$ ugrep -n -f c++/comments myfile.cpp
.PP
List the lines that need fixing in a C/C++ source file by looking for the word
`FIXME' while skipping any `FIXME' in quoted strings:
.IP
$ ugrep -e FIXME -N '"(\\\\.|\\\\\\r?\\n|[^\\\\\\n"])*"' myfile.cpp
.PP
The same, but using predefined pattern cpp/zap_strings:
.IP
$ ugrep -e FIXME -f cpp/zap_strings myfile.cpp
.PP
Find lines with `FIXME' or `TODO', showing line numbers:
.IP
$ ugrep -n -e FIXME -e TODO myfile.cpp
.PP
Find lines with `FIXME' that also contain `urgent':
.IP
$ ugrep -n -e FIXME --and urgent myfile.cpp
.PP
The same, but with a Boolean query pattern (a space is AND):
.IP
$ ugrep -n -% 'FIXME urgent' myfile.cpp
.PP
Find lines with `FIXME' that do not also contain `later':
.IP
$ ugrep -n -e FIXME --andnot later myfile.cpp
.PP
The same, but with a Boolean query pattern (a space is AND, - is NOT):
.IP
$ ugrep -n -% 'FIXME -later' myfile.cpp
.PP
Output a list of line numbers of lines with `FIXME' but not `later':
.IP
$ ugrep -e FIXME --andnot later --format='%,%n' myfile.cpp
.PP
Recursively list all files with both `FIXME' and `LICENSE' anywhere in the
file, not necessarily on the same line:
.IP
$ ugrep -l -%% 'FIXME LICENSE'
.PP
Find lines with `FIXME' in the C/C++ files stored in a tarball:
.IP
$ ugrep -z -tc++ -n FIXME project.tgz
.PP
Recursively find lines with `FIXME' in C/C++ files, but do not search any `bak'
and `old' directories:
.IP
$ ugrep -n FIXME -tc++ -g^bak/,^old/
.PP
Recursively search for the word `copyright' in cpio, jar, pax, tar, zip, 7z
archives, compressed and regular files, and in PDFs using a PDF filter:
.IP
$ ugrep -z -w --filter='pdf:pdftotext % -' copyright
.PP
Match the binary pattern `A3hhhhA3' (hex) in a binary file without Unicode
pattern matching \fB-U\fR (which would otherwise match `\\xaf' as a Unicode
character U+00A3 with UTF-8 byte sequence C2 A3) and display the results in hex
with \fB--hexdump\fR with \fBC1\fR to output one hex line before and after each
match:
.IP
$ ugrep -U --hexdump=C1 '\\xa3[\\x00-\\xff]{2}\\xa3' a.out
.PP
Hexdump an entire file using a pager for viewing:
.IP
$ ugrep -X --pager '' a.out
.PP
List all files that are not ignored by one or more `.gitignore':
.IP
$ ugrep -l '' --ignore-files
.PP
List all files containing a RPM signature, located in the `rpm' directory and
recursively below up to two levels deeper (3 levels total):
.IP
$ ugrep -3 -l -tRpm '' rpm/
.PP
Monitor the system log for bug reports and ungroup multiple matches on a line:
.IP
$ tail -f /var/log/system.log | ugrep -u -i -w bug
.PP
Interactive fuzzy search with Boolean search queries:
.IP
$ ugrep -Q -l -% -Z3 --sort=best
.PP
Display all words in a MacRoman-encoded file that has CR newlines:
.IP
$ ugrep --encoding=MACROMAN '\\w+' mac.txt
.PP
Display options related to "fuzzy" searching:
.IP
$ ugrep --help fuzzy
.PP
.SH COPYRIGHT
Copyright (c) 2021,2025 Robert A. van Engelen <engelen@acm.org>
.PP
\fBugrep\fR is released under the BSD\-3 license.  All parts of the software
have reasonable copyright terms permitting free redistribution.  This includes
the ability to reuse all or parts of the ugrep source tree.
.SH "SEE ALSO"
ugrep-indexer(1), grep(1), zgrep(1).
.SH BUGS
Report bugs at: <https://github.com/Genivia/ugrep/issues>
END

man man/ugrep.1 | sed 's/.//g' > man.txt

echo "ugrep $1 manual page created and saved in man/ugrep.1"
echo "ugrep text-only man page created and saved as man.txt"

else

echo "ugrep is needed but was not found: build ugrep first"
exit 1

fi

if [ -x src/ugrep-indexer ] 
then

echo
echo "Creating ugrep-indexer man page"
mkdir -p man
echo '.TH UGREP-INDEXER "1" "'`date '+%B %d, %Y'`'" "ugrep-indexer '$1'" "User Commands"' > man/ugrep-indexer.1
cat >> man/ugrep-indexer.1 << 'END'
.SH NAME
\fBugrep-indexer\fR -- file indexer to accelerate recursive searching
.SH SYNOPSIS
.B ugrep-indexer [\fB-0\fR...\fB9\fR] [\fB-c\fR|\fB-d\fR|\fB-f\fR] [\fB-I\fR] [\fB-q\fR] [\fB-S\fR] [\fB-s\fR] [\fB-X\fR] [\fB-z\fR] [\fIPATH\fR]
.SH DESCRIPTION
The \fBugrep-indexer\fR utility recursively indexes files to accelerate
recursive searching with the \fBug --index\fR \fIPATTERN\fR commands:
.IP
$ \fBugrep-indexer\fR [\fB-I\fR] [\fB-z\fR]
.IP
  ...
.IP
$ \fBug\fR \fB--index\fR [\fB-I\fR] [\fB-z\fR] [\fB-r\fR|\fB-R\fR] \fIOPTIONS\fR \fIPATTERN\fR
.IP
$ \fBugrep\fR \fB--index\fR [\fB-I\fR] [\fB-z\fR] [\fB-r\fR|\fB-R\fR] \fIOPTIONS\fR \fIPATTERN\fR
.PP
where option \fB-I\fR or \fB--ignore-binary\fR ignores binary files, which is
recommended to limit indexing storage overhead and to reduce search time.
Option \fB-z\fR or \fB--decompress\fR indexes and searches archives and
compressed files.
.PP
Indexing speeds up searching file systems that are large and cold (not recently
cached in RAM) and file systems that are generally slow to search.  Note that
indexing may not speed up searching few files or recursively searching fast
file systems.
.PP
Searching with \fBug --index\fR is safe and never skips modified files that may
match after indexing; the \fBug --index\fR \fIPATTERN\fR command always
searches files and directories that were added or modified after indexing.
When option \fB--stats\fR is used with \fBug --index\fR, a search report is
produced showing the number of files skipped not matching any indexes and the
number of files and directories that were added or modified after indexing.
Note that searching with \fBug --index\fR may significantly increase the
start-up time when complex regex patterns are specified that contain large
Unicode character classes combined with `*' or `+' repeats, which should be
avoided.
.PP
\fBugrep-indexer\fR stores a hidden index file in each directory indexed.  The
size of an index file depends on the number of files indexed and the specified
indexing accuracy.  Higher accuracy produces larger index files to improve
search performance by reducing false positives (a false positive is a match
prediction for a file when the file does not match the regex pattern.)
.PP
\fBugrep-indexer\fR accepts an optional \fIPATH\fR to the root of the directory
tree to index.  The default is to index the working directory tree.
.PP
\fBugrep-indexer\fR incrementally updates indexes.  To force reindexing,
specify option \fB-f\fR or \fB--force\fR.  Indexes are deleted with option
\fB-d\fR or \fB--delete\fR.
.PP
\fBugrep-indexer\fR may be stopped and restarted to continue indexing at any
time.  Incomplete index files do not cause errors.
.PP
ASCII, UTF-8, UTF-16 and UTF-32 files are indexed and searched as text files
unless their UTF encoding is invalid.  Files with other encodings are indexed
as binary files and can be searched with non-Unicode regex patterns using
\fBug --index \fB-U\fR.
.PP
When \fBugrep-indexer\fR option \fB-I\fR or \fB--ignore-binary\fR is specified,
binary files are ignored and not indexed.  Avoid searching these non-indexed
binary files with \fBug --index -I\fR using option \fB-I\fR.
.PP
\fBugrep-indexer\fR option \fB-X\fR or \fB--ignore-files\fR respects gitignore
rules.  Likewise, avoid searching non-indexed ignored files with \fBug --index
--ignore-files\fR using option \fB--ignore-files\fR.
.PP
Archives and compressed files are indexed with \fBugrep-indexer\fR option
\fB-z\fR or \fB--decompress\fR.  Otherwise, archives and compressed files are
indexed as binary files or are ignored with option \fB-I\fR or
\fB--ignore-binary\fR.  Note that once an archive or compressed file is indexed
as a binary file, it will not be reindexed with option \fB-z\fR to index the
contents of the archive or compressed file.  Only files that are modified after
indexing are reindexed, which is determined by comparing time stamps.
.PP
Symlinked files are indexed with \fBugrep-indexer\fR option \fB-S\fR or
\fB--dereference-files\fR.  Symlinks to directories are never followed.  
.PP
To save a log file of the indexing process, specify option \fB-v\fR or
\fB--verbose\fR and redirect standard output to a log file.  All messages and
warnings are sent to standard output and captured by the log file.
.PP
A .ugrep-indexer configuration file with configuration options is loaded when
present in the working directory or in the home directory.  A configuration
option consists of the name of a long option and its argument when applicable.
.PP
The following options are available:
END
src/ugrep-indexer --help \
| tail -n+28 \
| sed -e 's/\([^\\]\)\\/\1\\\\/g' \
| sed \
  -e '/^$/ d' \
  -e '/^    Long options may start/ d' \
  -e '/^    The ugrep-indexer/ d' \
  -e '/^    0      / d' \
  -e '/^    1      / d' \
  -e 's/^ \{5,\}//' \
  -e 's/^\.\([A-Za-z]\)/\\\&.\1/g' \
  -e $'s/^    \(.*\)$/.TP\\\n\\1/' \
  -e 's/\(--[+0-9A-Za-z_-]*\)/\\fB\1\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\) \([A-Z]\{1,\}\)/\1\\fB\2\\fR \\fI\3\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\)/\1\\fB\2\\fR/g' \
  -e 's/^\(-.\) \([!A-Z]\{1,\}\)/\\fB\1\\fR \\fI\2\\fR/g' \
  -e 's/^\(-.\)/\\fB\1\\fR/g' \
  -e 's/\[\([-A-Z]\{1,\}\),\]\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR,][\\fI\2\\fR]/g' \
  -e 's/\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR]/g' \
  -e 's/\[,\([-A-Z]\{1,\}\)\]/[,\\fI\1\\fR]/g' \
  -e 's/\[=\([-A-Z]\{1,\}\)\]/[=\\fI\1\\fR]/g' \
  -e 's/=\([-A-Z]\{1,\}\)/=\\fI\1\\fR/g' \
| sed -e 's/-/\\-/g' >> man/ugrep-indexer.1
cat >> man/ugrep-indexer.1 << 'END'
.SH "EXIT STATUS"
The \fBugrep-indexer\fR utility exits with one of the following values:
.IP 0
Indexes are up to date.
.IP 1
Indexing check \fB-c\fR detected missing and outdated index files.
.SH EXAMPLES
Recursively and incrementally index all non-binary files showing progress:
.IP
$ ugrep-indexer -I -v
.PP
Recursively and incrementally index all non-binary files, including non-binary
files stored in archives and in compressed files, showing progress:
.IP
$ ugrep-indexer -z -I -v
.PP
Incrementally index all non-binary files, including archives and compressed
files, show progress, follow symbolic links to files (but not to directories),
but do not index files and directories matching the globs in .gitignore:
.IP
$ ugrep-indexer -z -I -v -S -X
.PP
Force re-indexing of all non-binary files, including archives and compressed
files, follow symbolic links to files (but not to directories), but do not
index files and directories matching the globs in .gitignore:
.IP
$ ugrep-indexer -f -z -I -v -S -X
.PP
Same, but decrease index file storage to a minimum by decreasing indexing
accuracy from 4 (the default) to 0:
.IP
$ ugrep-indexer -f -0 -z -I -v -S -X
.PP
Increase search performance by increasing the indexing accuracy from 4
(the default) to 7 at a cost of larger index files:
.IP
$ ugrep-indexer -f7zIvSX
.PP
Recursively delete all hidden ._UG#_Store index files to restore the directory
tree to non-indexed:
.IP
$ ugrep-indexer -d
.SH COPYRIGHT
Copyright (c) 2021-2025 Robert A. van Engelen <engelen@acm.org>
.PP
\fBugrep-indexer\fR is released under the BSD\-3 license.  All parts of the
software have reasonable copyright terms permitting free redistribution.  This
includes the ability to reuse all or parts of the ugrep source tree.
.SH "SEE ALSO"
ug(1), ugrep(1).
.SH BUGS
Report bugs at:
.IP
https://github.com/Genivia/ugrep-indexer/issues
END

echo "ugrep-indexer $1 manual page created and saved in man/ugrep-indexer.1"

else

echo "ugrep-indexer is needed but was not found: build ugrep-indexer first"
exit 1

fi

else

echo "Usage: ./man.sh 1.v.v"
exit 1

fi
