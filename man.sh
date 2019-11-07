#!/bin/sh

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
\fBugrep\fR -- universal file pattern searcher
.SH SYNOPSIS
.B ugrep
[\fIOPTIONS\fR] [\fB-A\fR \fINUM\fR] [\fB-B\fR \fINUM\fR] [\fB-C\fR[\fINUM\fR]] [\fIPATTERN\fR] [\fB-f\fR \fIFILE\fR]
      [\fB-e\fR \fIPATTERN\fR] [\fB-t\fR \fITYPES\fR] [\fB-Q\fR \fIENCODING\fR] [\fB-J\fR [\fINUM\fR]]
      [\fB--color\fR[=\fIWHEN\fR]|\fB--colour\fR[=\fIWHEN\fR]] [\fB--pager\fR[=\fICOMMAND\fR]] [\fIFILE\fR \fI...\fR]
.SH DESCRIPTION
The \fBugrep\fR utility searches any given input files, selecting lines that
match one or more patterns.  By default, a pattern matches an input line if the
regular expression (RE) in the pattern matches the input line without its
trailing newline.  An empty expression matches every line.  Each input line
that matches at least one of the patterns is written to the standard output.
To search for patterns that span multiple lines, use option \fB-o\fR.
.PP
The \fBugrep\fR utility normalizes and decodes encoded input to search for the
specified ASCII/Unicode patterns.  If the input contains a UTF BOM indicating
UTF-8, UTF-16, or UTF-32 input, then \fBugrep\fR normalizes the input to UTF-8.
If no UTF BOM is present, then \fBugrep\fR assumes the input is ASCII, UTF-8,
or raw binary.  To explicitly specify an input encoding to decode, use option
\fB-Q\fR, \fB--encoding\fR.
.PP
The following options are available:
END
src/ugrep --help \
| sed -e 's/\([^\\]\)\\/\1\\\\/g' \
| sed \
  -e '/^$/ d' \
  -e '/^Usage:/ d' \
  -e '/^    The ugrep/ d' \
  -e '/^    0       / d' \
  -e '/^    1       / d' \
  -e '/^    >1      / d' \
  -e '/^    If -q or --quiet or --silent/ d' \
  -e '/^    status is 0 even/ d' \
  -e 's/^                //' \
  -e 's/^            //' \
  -e $'s/^    \(.*\)$/.TP\\\n\\1/' \
  -e 's/\(--[-+0-9A-Za-z_]*\)/\\fB\1\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-.\) \([A-Z][A-Z]*\)/\1\\fB\2\\fR \\fI\3\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-.\)/\1\\fB\2\\fR/g' \
  -e 's/\[=\([-A-Z]*\)\]/[=\\fI\1\\fR]/g' \
  -e 's/=\([-A-Z]*\)/=\\fI\1\\fR/g' \
| sed -e 's/-/\\-/g' >> man/ugrep.1
cat >> man/ugrep.1 << 'END'
.PP
If no FILE arguments are specified, or if a `-' is specified, the standard
input is used, unless recursive searches are specified which examine the
working directory.  Use `--' before the FILE arguments to allow file and
directory names to start with a `-'.
.PP
The regular expression pattern syntax is an extended form of the POSIX ERE
syntax.  For an overview of the syntax see README.md or visit:
.IP
https://github.com/Genivia/ugrep
.PP
Note that `.' matches any non-newline character.  Matching a newline character
`\\n' is not possible unless one or more of the options \fB-c\fR, \fB-L\fR,
\fB-l\fR, \fB-N\fR, \fB-o\fR, or \fB-q\fR are used (in any combination, but not
combined with option \fB-v\fR) to allow a pattern match to span multiple lines.
.SH "EXIT STATUS"
The \fBugrep\fR utility exits with one of the following values:
.IP 0
One or more lines were selected.
.IP 1
No lines were selected.
.IP >1
An error occurred.
.PP
If -q or --quiet or --silent is used and a line is selected, the exit status is
0 even if an error occurred.
.SH GLOBBING
Globbing is used by options \fB--include\fR, \fB--include-dir\fR,
\fB--include-from\fR, \fB--exclude\fR, \fB--exclude-dir\fR,
\fB--exclude-from\fR to match pathnames and basenames.  Globbing supports
gitignore syntax and the corresponding matching rules.  When a glob contains a
path separator `/', the pathname is matched.  Otherwise the basename of a file
or directory is matched.  For example, \fB*.h\fR matches \fIfoo.h\fR and
\fIbar/foo.h\fR.  \fBbar/*.h\fR matches \fIbar/foo.h\fR but not \fIfoo.h\fR and
not \fIbar/bar/foo.h\fR.  Use a leading `/' to force \fB/*.h\fR to match
\fIfoo.h\fR but not \fIbar/foo.h\fR.  A glob starting with a `!' is negated,
i.e. does not match.
.PP
\fBGlob Syntax and Conventions\fR
.IP \fB*\fR
Matches anything except a /.
.IP \fB?\fR
Matches any one character except a /.
.IP \fB[a-z]\fR
Matches one character in the selected range of characters.
.IP \fB[^a-z]\fR
Matches one character not in the selected range of characters.
.IP \fB[!a-z]\fR
Matches one character not in the selected range of characters.
.IP \fB/\fR
When used at the begin of a glob, matches if pathname has no /.
.IP \fB**/\fR
Matches zero or more directories.
.IP \fB/**\fR
When at the end of a glob, matches everything after the /.
.IP \fB\\\\?\fR
Matches a ? (or any character specified after the backslash).
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
Lines in the \fB--exclude-from\fR and \fB--include-from\fR files are ignored
when empty or start with a `#'.  The prefix `!' to a glob in such a file
negates the pattern match, i.e. matching files are excluded except files
matching the globs prefixed with `!' in the \fB--exclude-from\fR file.
.SH ENVIRONMENT
.IP \fBGREP_PATH\fR
May be used to specify a file path to pattern files.  The file path is used by
option \fB-f\fR to open a pattern file, when the file cannot be opened.
.IP \fBGREP_COLOR\fR
May be used to specify ANSI SGR parameters to highlight matches when option
\fB--color\fR is used, e.g. 1;35;40 shows pattern matches in bold magenta text
on a black background.
.IP \fBGREP_COLORS\fR
May be used to specify ANSI SGR parameters to highlight matches and other
attributes when option \fB--color\fR is used.  Its value is a colon-separated
list of ANSI SGR parameters that defaults to
\fBcx=2:mt=1;31:fn=35:ln=32:cn=32:bn=32:se=36\fR.  The \fBmt=\fR,
\fBms=\fR, and \fBmc=\fR capabilities of \fBGREP_COLORS\fR have priority over
\fBGREP_COLOR\fR.
.SH GREP_COLORS
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
SGR substring for file names.
.IP \fBln=\fR
SGR substring for line numbers.
.IP \fBcn=\fR
SGR substring for column numbers.
.IP \fBbn=\fR
SGR substring for byte offsets.
.IP \fBse=\fR
SGR substring for separators.
.SH FORMAT
Option \fB--format\fR=\fIFORMAT\fR specifies an output format for file matches.
Fields may be used in \fIFORMAT\fR which expand into the following values:
.IP \fB%[\fR\fIARG\fR\fB]F\fR
if option \fB-H\fR is used: \fIARG\fR, the file pathname, and separator.
.IP \fB%[\fR\fIARG\fR\fB]H\fR
if option \fB-H\fR is used: \fIARG\fR, the quoted pathname, and separator.
.IP \fB%[\fR\fIARG\fR\fB]N\fR
if option \fB-n\fR is used: \fIARG\fR, the line number and separator.
.IP \fB%[\fR\fIARG\fR\fB]K\fR
if option \fB-k\fR is used: \fIARG\fR, the column number and separator.
.IP \fB%[\fR\fIARG\fR\fB]B\fR
if option \fB-b\fR is used: \fIARG\fR, the byte offset and separator.
.IP \fB%[\fR\fIARG\fR\fB]T\fR
if option \fB-T\fR is used: \fIARG\fR and a tab character.
.IP \fB%[\fR\fIARG\fR\fB]S\fR
if not the first match: \fIARG\fR and separator, see also \fB%$\fR.
.IP \fB%[\fR\fIARG\fR\fB]<\fR
if the first match: \fIARG\fR.
.IP \fB%[\fR\fIARG\fR\fB]>\fR
if not the first match: \fIARG\fR.
.IP \fB%[\fR\fISEP\fR\fB]$\fR
set field separator to \fISEP\fR for the rest of the format fields.
.IP \fB%f\fR
the file pathname.
.IP \fB%h\fR
the quoted file pathname.
.IP \fB%n\fR
the line number of the match.
.IP \fB%k\fR
the column number of the match.
.IP \fB%b\fR
the byte offset of the match.
.IP \fB%t\fR
a tab character.
.IP \fB%s\fR
the separator, see also \fB%S\fR and \fB%$\fR.
.IP \fB%~\fR
a newline character.
.IP \fB%m\fR
the number of matches or matched files.
.IP \fB%O\fR
the matching line is output as is (a raw string of bytes).
.IP \fB%o\fR
the match is output as is (a raw string of bytes).
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
the width of the match, counting (wide) characters.
.IP \fB%d\fR
the size of the match, counting bytes.
.IP \fB%,\fR
if not the first match: a comma, same as \fB%[,]>\fR.
.IP \fB%:\fR
if not the first match: a colon, same as \fB%[:]>\fR.
.IP \fB%;\fR
if not the first match: a semicolon, same as \fB%[;]>\fR.
.IP \fB%|\fR
if not the first match: a verical bar, same as \fB%[|]>\fR.
.IP \fB%%\fR
the percentage sign.
.IP \fB%1\fR
the first regex group capture of the match, and so on up to group \fB%9\fR,
same as \fB%[1]#\fR, requires option \fB-P\fR Perl matching.
.IP \fB%[\fINUM\fR\fB]#\fR
the regex group capture \fINUM\fR, requires option \fB-P\fR Perl matching.
.PP
The \fB[\fR\fIARG\fR\fB]\fR part of a field is optional and may be omitted.
.PP
The separator used by \fB%P\fR, \fB%H\fR, \fB%N\fR, \fB%K\fR, \fB%B\fR, and
\fB%S\fR may be changed by preceeding the field with a
\fB%[\fR\fISEP\fR\fB]$\fR.  When \fB[\fR\fISEP\fR\fB]\fR is not provided as in
\fB%$\fR, reverses the separator to the default separator or the separator
specified by \fB--separator\fR.
.PP
Matches are formatted without context.  To output the entire line with the match,
use pattern '.*\fIPATTERN\fR.*' to match the line before and after the match.
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
The context options \fB-A\fR, \fB-B\fR, \fB-C\fR, \fB-y\fR, and options
\fB-v\fR, \fB--break\fR, \fB--color\fR, \fB-T\fR, and \fB--null\fR are disabled
and have no effect on the formatted output.
.SH EXAMPLES
To find all occurrences of the word `patricia' in a file:
.IP
$ ugrep -w 'patricia' myfile
.PP
To count the number of lines containing the word `patricia' or `Patricia` in a
file:
.IP
$ ugrep -cw '[Pp]atricia' myfile
.PP
To count the total number of times the word `patricia' or `Patricia` occur in a
file:
.IP
$ ugrep -cgw '[Pp]atricia' myfile
.PP
To list all Unicode words in a file:
.IP
$ ugrep -o '\\w+' myfile
.PP
To list all ASCII words in a file:
.IP
$ ugrep -o '[[:word:]]+' myfile
.PP
To list all laughing face emojis (Unicode code points U+1F600 to U+1F60F) in a file:
.IP
$ ugrep -o '[\\x{1F600}-\\x{1F60F}]' myfile
.PP
To check if a file contains any non-ASCII (i.e. Unicode) characters:
.IP
$ ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"
.PP
To display the line and column number of all `FIXME' in all C++ files using
recursive search, with one line of context before and after each matched line:
.IP
$ ugrep --color -C1 -R -n -k -tc++ 'FIXME.*'
.PP
To list all C/C++ comments in a file displaying their line and column numbers
using options \fB-n\fR and \fB-k\fR, and option \fB-o\fR that allows for
matching patterns across multiple lines:
.IP
$ ugrep -nko -e '//.*' -e '/\\*([^*]|(\\*+[^*/]))*\\*+\\/' myfile
.PP
The same search, but using predefined patterns:
.IP
$ ugrep -nko -f c/comments myfile
.PP
To list the lines that need fixing in a C/C++ source file by looking for the
word FIXME while skipping any FIXME in quoted strings by using a negative
pattern `(?^X)' to ignore quoted strings:
.IP
$ ugrep -no -e 'FIXME' -e '(?^"(\\\\.|\\\\\\r?\\n|[^\\\\\\n"])*")' myfile
.PP
To match the binary pattern `A3hhhhA3hh` (hex) in a binary file without
Unicode pattern matching \fB-U\fR (which would otherwise match `\\xaf' as a
Unicode character U+00A3 with UTF-8 byte sequence C2 A3) and display the
results in hex with \fB-X\fR using `less -R' as a pager:
.IP
$ ugrep --pager -UXo '\\xa3[\\x00-\\xff]{2}\\xa3[\\x00-\\xff]' a.out
.PP
To hex dump an entire file in color:
.IP
$ ugrep --color --pager -Xo '' a.out
.PP
To list all files containing a RPM signature, located in the `rpm` directory and
recursively below:
.IP
$ ugrep -R -l -tRpm '' rpm/
.PP
To monitor the system log for bug reports:
.IP
$ tail -f /var/log/system.log | ugrep --color -i -w 'bug'
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

echo "ugrep $1 manual page created and saved in man/ugrep.1"

else

echo "ugrep is needed but was not found: build ugrep first"
exit 1

fi

else

echo "Usage: ./man.sh 1.v.v"
exit 1

fi
