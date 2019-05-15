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
[\fIOPTIONS\fR] [-A NUM] [-B NUM] [-C[NUM]] [\fIPATTERN\fR] [\fB-e\fR \fIPATTERN\fR]
      [\fB-f\fR \fIFILE\fR] [\fB--file-type\fR=\fITYPES\fR] [\fB--encoding\fR=\fIENCODING\fR]
      [\fB--colour\fR[=\fIWHEN\fR]|\fB--color\fR[=\fIWHEN\fR]] [\fB--label\fR[=\fILABEL\fR]] [\fIFILE\fR \fI...\fR]
.SH DESCRIPTION
The \fBugrep\fR utility searches any given input files, selecting lines that
match one or more patterns.  By default, a pattern matches an input line if the
regular expression (RE) in the pattern matches the input line without its
trailing newline.  An empty expression matches every line.  Each input line
that matches at least one of the patterns is written to the standard output.
To search for patterns that span multiple lines, use option \fB-o\fR.
.PP
The \fBugrep\fR utility normalizes and decodes encoded input to search for the
specified ASCII/Unicode patterns.  When the input contains a UTF BOM indicating
UTF-8, UTF-16, or UTF-32 input then \fBugrep\fR always normalizes the input to
UTF-8.  When no UTF BOM is present, \fBugrep\fR assumes the input is ASCII,
UTF-8, or raw binary.  To specify a different input file encoding, use option
\fB--encoding\fR.
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
  -e 's/^                //' \
  -e 's/^            //' \
  -e $'s/^    \(.*\)$/.TP\\\n\\1/' \
  -e 's/\(--[-+0-9A-Za-z_]*\)/\\fB\1\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-.\)/\1\\fB\2\\fR/g' \
  -e 's/\[=\([-0-9A-Za-z_]*\)\]/[=\\fI\1\\fR]/g' \
  -e 's/=\([-0-9A-Za-z_]*\)/=\\fI\1\\fR/g' \
| sed -e 's/-/\\-/g' >> man/ugrep.1
cat >> man/ugrep.1 << 'END'
.PP
The regular expression pattern syntax is an extended form of the POSIX ERE
syntax.  For an overview of the syntax see README.md or visit:
.IP
https://github.com/Genivia/ugrep
.PP
Note that `.' matches any non-newline character.  Matching a newline character
`\\n' is not possible unless one of the options \fB-c\fR, \fB-L\fR,
\fB-l\fR, \fB-N\fR, \fB-o\fR, or \fB-q\fR is used (in any combination, but not
combined with option \fB-v\fR) to allow a pattern match to span multiple lines.
.PP
If no file arguments are specified, or if `-' is specified, the standard input
is used.
.SH "EXIT STATUS"
The \fBugrep\fR utility exits with one of the following values:
.IP 0
One or more lines were selected.
.IP 1
No lines were selected.
.IP >1
An error occurred.
.SH GLOBBING
Globbing is used by options \fB--include\fR, \fB--include-dir\fR,
\fB--include-from\fR, \fB--exclude\fR, \fB--exclude-dir\fR,
\fB--exclude-from\fR to match pathnames and basenames.  Globbing supports
gitignore syntax and the corresponding matching rules.  When a glob contains a
path separator `/', the pathname is matched.  Otherwise the basename of a file
or directory is matched.  For example, \fB*.h\fR matches \fIfoo.h\fR and
\fIbar/foo.h\fR.  \fBbar/*.h\fR matches \fIbar/foo.h\fR but not \fIfoo.h\fR and
not \fIbar/bar/foo.h\fR.  Use a leading `/' to force \fB/*.h\fR to match
\fIfoo.h\fR but not \fIbar/foo.h\fR.
.PP
\fBGlob Syntax and Conventions\fR
.IP \fB**/\fR
Matches zero or more directories.
.IP \fB/**\fR
When at the end of a glob, matches everything after the /.
.IP \fB*\fR
Matches anything except a /.
.IP \fB/\fR
When used at the begin of a glob, matches if pathname has no /.
.IP \fB?\fR
Matches any character except a /.
.IP \fB[a-z]\fR
Matches one character in the selected range of characters.
.IP \fB[^a-z]\fR
Matches one character not in the selected range of characters.
.IP \fB[!a-z]\fR
Matches one character not in the selected range of characters.
.IP \fB\\\\?\fR
Matches a ? (or any character specified after the backslash).
.PP
\fBGlob Matching Examples\fR
.IP \fB**/a\fR
Matches a, x/a, x/y/a,       but not b, x/b.
.IP \fBa/**/b\fR
Matches a/b, a/x/b, a/x/y/b, but not x/a/b, a/b/x
.IP \fBa/**\fR
Matches a/x, a/y, a/x/y,     but not b/x
.IP \fBa/*/b\fR
Matches a/x/b, a/y/b,        but not a/x/y/b
.IP \fB/a\fR
Matches a,                   but not x/a
.IP \fB/*\fR
Matches a, b,                but not x/a, x/b
.IP \fBa?b\fR
Matches axb, ayb,            but not a, b, ab
.IP \fBa[xy]b\fR
Matches axb, ayb             but not a, b, azb
.IP \fBa[a-z]b\fR
Matches aab, abb, acb, azb,  but not a, b, a3b, aAb, aZb
.IP \fBa[^xy]b\fR
Matches aab, abb, acb, azb,  but not a, b, axb, ayb
.IP \fBa[^a-z]b\fR
Matches a3b, aAb, aZb        but not a, b, aab, abb, acb, azb
.PP
Lines in the \fB--exclude-from\fR and \fB--include-from\fR files are ignored
when empty or start with a `#'.  The prefix `!' to a glob in such a file
negates the pattern match, i.e. matching files are excluded except files
matching the globs prefixed with `!' in the \fB--exclude-from\fR file.
.SH ENVIRONMENT
.IP \fBGREP_PATH\fR
May be used to specify a file path to pattern files.  The file path is used by
option -f to open a pattern file, when the file specified with option -f cannot
be opened.
.IP \fBGREP_COLOR\fR
May be used to specify ANSI SGR parameters to highlight matches when option
\fB--color\fR is used, e.g. 1;35;40 shows pattern matches in bold magenta text
on a black background.
.IP \fBGREP_COLORS\fR
May be used to specify ANSI SGR parameters to highlight matches and other
attributes when option \fB--color\fR is used.  Its value is a colon-separated
list of ANSI SGR parameters that defaults to
\fBmt=1;31:sl=:cx=:fn=35:ln=32:cn=32:bn=32:se=36\fR.  The \fBmt=\fR,
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
To list all C/C++ comments in a file displaying their line and column numbers
using options \fB-n\fR and \fB-k\fR, and option \fB-o\fR that allows for
matching patterns across multiple lines:
.IP
$ ugrep -nko -e '//.*' -e '/\\*([^*]|(\\*+[^*/]))*\\*+\\/' myfile
.PP
The same search, but using pre-defined patterns:
.IP
$ ugrep -nko -f patterns/c_comments myfile
.PP
To list the lines that need fixing in a C/C++ source file by looking for the
word FIXME while skipping any FIXME in quoted strings by using a negative
pattern `(?^X)' to ignore quoted strings:
.IP
$ ugrep -no -e 'FIXME' -e '(?^"(\\\\.|\\\\\\r?\\n|[^\\\\\\n"])*")' myfile
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
