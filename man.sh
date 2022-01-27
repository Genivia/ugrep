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
[\fIOPTIONS\fR] [\fB-A\fR \fINUM\fR] [\fB-B\fR \fINUM\fR] [\fB-C \fR\fINUM\fR] [\fB-y\fR] [\fB-Q\fR|\fIPATTERN\fR] [\fB-f\fR \fIFILE\fR]
      [\fB-e\fR \fIPATTERN\fR] [\fB-N\fR \fIPATTERN\fR] [\fB-t\fR \fITYPES\fR] [\fB-g\fR \fIGLOBS\fR] [\fB--sort\fR[=\fIKEY\fR]]
      [\fB--color\fR[=\fIWHEN\fR]|\fB--colour\fR[=\fIWHEN\fR]] [\fB--pager\fR[=\fICOMMAND\fR]] [\fIFILE\fR \fI...\fR]
.SH DESCRIPTION
The \fBugrep\fR utility searches any given input files, selecting lines that
match one or more patterns.  By default, a pattern matches an input line if the
regular expression (RE) matches the input line.  A pattern matches multiple
input lines if the RE in the pattern matches one or more newlines in the input.
An empty pattern matches every line.  Each input line that matches at least one
of the patterns is written to the standard output.
.PP
\fBugrep\fR accepts input of various encoding formats and normalizes the output
to UTF-8.  When a UTF byte order mark is present in the input, the input is
automatically normalized; otherwise, \fBugrep\fR assumes the input is ASCII,
UTF-8, or raw binary.  An input encoding format may be specified with option
\fB--encoding\fR.
.PP
The \fBug\fR command is equivalent to \fBugrep --config\fR to load the default
configuration file, which allows for customization, see CONFIGURATION.
.PP
If no \fIFILE\fR arguments are specified and standard input is read from a
terminal, recursive searches are performed as if \fB-R\fR is specified.  To
force reading from standard input, specify `-' as a \fIFILE\fR argument.
.PP
Directories specified as \fIFILE\fR arguments are searched without recursing
into subdirectories, unless \fB-R\fR, \fB-r\fR, or \fB-2\fR...\fB-9\fR is
specified.
.PP
Hidden files and directories are ignored in recursive searches.  Option
\fB-.\fR (\fB--hidden\fR) includes hidden files and directories in recursive
searches.
.PP
A query interface is opened with \fB-Q\fR (\fB--query\fR) to interactively
specify search patterns and view search results.  Note that a \fIPATTERN\fR
argument cannot be specified in this case.  To specify one or more patterns
with \fB-Q\fR, use \fB-e PATTERN\fR.
.PP
Option \fB-f FILE\fR matches patterns specified in \fBFILE\fR.  If \fBFILE\fR
is large and defines complex regular expression patterns, then option \fB-P\fR
(Perl matching) may improve performance (this omits POSIX DFA construction.)
.PP
\fBugrep --help \fIWHAT\fR displays help on options related to \fIWHAT\fR;
\fB--help format\fR displays help on \fB--format\fR and \fB--replace\fR
formatting; \fB--help regex\fR displays help on regular expression syntax and
conventions; \fB--help globs\fR displays help on glob patterns.
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
.PP
A `--' signals the end of options; the rest of the parameters are \fIFILE\fR
arguments, allowing filenames to begin with a `-' character.
.PP
Long options may start with `\FB--no-\fR' to disable, when applicable.
.PP
The regular expression pattern syntax is an extended form of the POSIX ERE
syntax.  For an overview of the syntax see README.md or visit:
.IP
https://github.com/Genivia/ugrep
.PP
Note that `.' matches any non-newline character.  Pattern `\\n' matches a
newline character.  Multiple lines may be matched with patterns that match
one or more newline characters.
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
The \fBug\fR command is intended for context-dependent interactive searching
and is equivalent to the \fBugrep --config\fR command to load the default
configuration file `.ugrep' when present in the working directory or in the
home directory.
.PP
A configuration file contains `NAME=VALUE' pairs per line, where `NAME` is the
name of a long option (without `--') and `=VALUE' is an argument, which is
optional and may be omitted depending on the option.  Empty lines and lines
starting with a `#' are ignored.
.PP
The \fB--config\fR=\fIFILE\fR option and its abbreviated form
\fB---\fR\fIFILE\fR load the specified configuration file located in the
working directory or, when not found, located in the home directory.  An error
is produced when \fIFILE\fR is not found or cannot be read.
.PP
Command line options are parsed in the following order: the configuration file
is loaded first, followed by the remaining options and arguments on the command
line.
.PP
The \fB--save-config\fR option saves a `.ugrep' configuration file to the
working directory with a subset of the current options.  The
\fB--save-config\fR=\fIFILE\fR option saves the configuration to \fIFILE\fR.
The configuration is written to standard output when \fIFILE\fR is a `-'.
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
When used at the begin of a glob, matches if pathname has no /.
When used at the end of a glob, matches directories only.
.IP \fB**/\fR
Matches zero or more directories.
.IP \fB/**\fR
When used at the end of a glob, matches everything after the /.
.IP \fB\\\\?\fR
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
.IP \fBa\\\\?b\fR
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
\fBcx=33:mt=1;31:fn=1;35:ln=1;32:cn=1;32:bn=1;32:se=36\fR.  The \fBmt=\fR,
\fBms=\fR, and \fBmc=\fR capabilities of \fBGREP_COLORS\fR take priority over
\fBGREP_COLOR\fR.  Option \fB--colors\fR takes priority over \fBGREP_COLORS\fR.
.SH GREP_COLORS
Colors are specified as string of colon-separated ANSI SGR parameters of the
form `what=substring', where `substring' is a semicolon-separated list of ANSI
SGR codes or `k' (black), `r' (red), `g' (green), `y' (yellow), `b' (blue), `m'
(magenta), `c' (cyan), `w' (white).  Upper case specifies background colors.  A
`+' qualifies a color as bright.  A foreground and a background color may be
combined with one or more font properties `n' (normal), `f' (faint), `h'
(highlight), `i' (invert), `u' (underline).  Substrings may be specified for:
.IP \fBsl=\fR
SGR substring for selected lines.
.IP \fBcx=\fR
SGR substring for context lines.
.IP \fBrv\fR
Swaps the \fBsl=\fR and \fBcx=\fR capabilities when \fB-v\fR is specified.
.IP \fBmt=\fR
SGR substring for matching text in any matching line.
.IP \fBms=\fR
SGR substring for matching text in a selected line.  The substring \fBmt=\fR by
default.
.IP \fBmc=\fR
SGR substring for matching text in a context line.  The substring \fBmt=\fR by
default.
.IP \fBfn=\fR
SGR substring for filenames.
.IP \fBln=\fR
SGR substring for line numbers.
.IP \fBcn=\fR
SGR substring for column numbers.
.IP \fBbn=\fR
SGR substring for byte offsets.
.IP \fBse=\fR
SGR substring for separators.
.IP \fBrv\fR
a Boolean parameter, switches \fBsl=\fR and \fBcx=\fR with option \fB-v\fR.
.IP \fBhl\fR
a Boolean parameter, enables filename hyperlinks (\fB\\33]8;;link\fR).
.IP \fBne\fR
a Boolean parameter, disables ``erase in line'' \fB\\33[K\fR.
.SH FORMAT
Option \fB--format\fR=\fIFORMAT\fR specifies an output format for file matches.
Fields may be used in \fIFORMAT\fR, which expand into the following values:
.IP \fB%[\fR\fIARG\fR\fB]F\fR
if option \fB-H\fR is used: \fIARG\fR, the file pathname and separator.
.IP \fB%f\fR
the file pathname.
.IP \fB%a\fR
the file basename without directory path.
.IP \fB%p\fR
the directory path to the file.
.IP \fB%z\fR
the file pathname in a (compressed) archive.
.IP \fB%[\fR\fIARG\fR\fB]H\fR
if option \fB-H\fR is used: \fIARG\fR, the quoted pathname and separator, \\"
and \\\\ replace " and \\.
.IP \fB%h\fR
the quoted file pathname, \\" and \\\\ replace " and \\.
.IP \fB%[\fR\fIARG\fR\fB]N\fR
if option \fB-n\fR is used: \fIARG\fR, the line number and separator.
.IP \fB%n\fR
the line number of the match.
.IP \fB%[\fR\fIARG\fR\fB]K\fR
if option \fB-k\fR is used: \fIARG\fR, the column number and separator.
.IP \fB%k\fR
the column number of the match.
.IP \fB%[\fR\fIARG\fR\fB]B\fR
if option \fB-b\fR is used: \fIARG\fR, the byte offset and separator.
.IP \fB%b\fR
the byte offset of the match.
.IP \fB%[\fR\fIARG\fR\fB]T\fR
if option \fB-T\fR is used: \fIARG\fR and a tab character.
.IP \fB%t\fR
a tab character.
.IP \fB%[\fR\fISEP\fR\fB]$\fR
set field separator to \fISEP\fR for the rest of the format fields.
.IP \fB%[\fR\fIARG\fR\fB]<\fR
if the first match: \fIARG\fR.
.IP \fB%[\fR\fIARG\fR\fB]>\fR
if not the first match: \fIARG\fR.
.IP \fB%,\fR
if not the first match: a comma, same as \fB%[,]>\fR.
.IP \fB%:\fR
if not the first match: a colon, same as \fB%[:]>\fR.
.IP \fB%;\fR
if not the first match: a semicolon, same as \fB%[;]>\fR.
.IP \fB%|\fR
if not the first match: a vertical bar, same as \fB%[|]>\fR.
.IP \fB%[\fR\fIARG\fR\fB]S\fR
if not the first match: \fIARG\fR and separator, see also \fB%[\fR\fISEP\fR\fB]$.
.IP \fB%s\fR
the separator, see also \fB%[\fR\fIARG\fR\fB]S\fR and \fB%[\fR\fISEP\fR\fB]$.
.IP \fB%~\fR
a newline character.
.IP \fB%m\fR
the number of matches or matched files.
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
The \fB[\fR\fIARG\fR\fB]\fR part of a field is optional and may be omitted.
When present, the argument must be placed in \fB[]\fR brackets, for example
\fB%[,]F\fR to output a comma, the pathname, and a separator.
.PP
\fB%[\fR\fISEP\fR\fB]$\fR and \fB%u\fR are switches and do not send anything to
the output.
.PP
The separator used by the \fB%F\fR, \fB%H\fR, \fB%N\fR, \fB%K\fR, \fB%B\fR,
\fB%S\fR and \fB%G\fR fields may be changed by preceding the field by
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
List lines with both `amount' and a decimal number, ignoring case:
.IP
$ ugrep -wi --bool 'amount \d+(\.\d+)?' myfile.txt
.PP
Alternative query:
.IP
$ ugrep -wi -e amount --and '\d+(\.\d+)?' myfile.txt
.PP
List all Unicode words in a file:
.IP
$ ugrep -o '\\w+' myfile.txt
.PP
List all ASCII words in a file:
.IP
$ ugrep -o '[[:word:]]+' myfile.txt
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
List the C/C++ comments in a file with line numbers:
.IP
$ ugrep -n -e '//.*' -e '/\\*([^*]|(\\*+[^*/]))*\\*+\\/' myfile.cpp
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
Find lines with `FIXME' or `TODO':
.IP
$ ugrep -n -e FIXME -e TODO myfile.cpp
.PP
Find lines with `FIXME' that also contain the word `urgent':
.IP
$ ugrep -n FIXME myfile.cpp | ugrep -w urgent
.PP
Find lines with `FIXME' but not the word `later':
.IP
$ ugrep -n FIXME myfile.cpp | ugrep -v -w later
.PP
Output a list of line numbers of lines with `FIXME' but not `later':
.IP
$ ugrep -n FIXME myfile.cpp | ugrep -vw later | 
  ugrep -P '^(\\d+)' --format='%,%n'
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
Recursively search for the word `copyright' in cpio/jar/pax/tar/zip archives,
compressed and regular files, and in PDFs using a PDF filter:
.IP
$ ugrep -z -w --filter='pdf:pdftotext % -' copyright
.PP
Match the binary pattern `A3hhhhA3hh' (hex) in a binary file without Unicode
pattern matching \fB-U\fR (which would otherwise match `\\xaf' as a
Unicode character U+00A3 with UTF-8 byte sequence C2 A3) and display the
results in hex with \fB-X\fR using `less -R' as a pager:
.IP
$ ugrep --pager -UXo '\\xa3[\\x00-\\xff]{2}\\xa3[\\x00-\\xff]' a.out
.PP
Hexdump an entire file:
.IP
$ ugrep -X '' a.out
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
$ ugrep -Q --bool -Z3 --sort=best
.PP
Display all words in a MacRoman-encoded file that has CR newlines:
.IP
$ ugrep --encoding=MACROMAN '\\w+' mac.txt
.PP
Display all options related to "fuzzy" searching:
.IP
$ ugrep --help fuzzy
.SH BUGS
Report bugs at:
.IP
https://github.com/Genivia/ugrep/issues
.PP
.SH LICENSE
\fBugrep\fR is released under the BSD\-3 license.  All parts of the software
have reasonable copyright terms permitting free redistribution.  This includes
the ability to reuse all or parts of the ugrep source tree.
.SH "SEE ALSO"
grep(1).
END

man man/ugrep.1 | sed 's/.//g' > man.txt

echo "ugrep $1 manual page created and saved in man/ugrep.1"
echo "ugrep text-only man page created and saved as man.txt"

else

echo "ugrep is needed but was not found: build ugrep first"
exit 1

fi

else

echo "Usage: ./man.sh 1.v.v"
exit 1

fi
