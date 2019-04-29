[![license][bsd-3-image]][bsd-3-url]

ugrep: universal grep
=====================

A high-performance universal search utility finds Unicode patterns in
UTF-8/16/32, ASCII, ISO-8859-1, EBCDIC, code pages 437, 850, 858, 1250 to 1258,
and other file formats.

**ugrep** uses [RE/flex](https://github.com/Genivia/RE-flex) for
high-performance regex matching, which is 100 times faster than the GNU C
POSIX.2 regex library used by the grep utility and 10 times faster than PCRE2
and RE2.

**ugrep** matches Unicode regex patterns.  The regular expression syntax is
POSIX ERE compliant, extended with Unicode character classes, lazy quantifiers,
and negative patterns to skip unwanted pattern matches.

**ugrep** searches UTF-encoded input when UTF BOM
([byte order mark](https://en.wikipedia.org/wiki/Byte_order_mark)) are present
and ASCII and UTF-8 when no UTF BOM is present.  Option `--file-format` permits
many other file formats to be searched, such as ISO-8859-1, EBCDIC, and code
pages 437, 850, 858, 1250 to 1258.

**ugrep** uses command-line options that are compatible with GNU
[grep](https://www.gnu.org/software/grep/manual/grep.html).

**ugrep** is currently in beta release with new features being added in the
near future.

Examples
--------

### display the lines in a file that contain capitalized Unicode words

    ugrep '\p{Upper}\p{Lower}*' places.txt

To include the line and column numbers and color-highlighted matches:

    ugrep -n -k --color '\p{Upper}\p{Lower}*' places.txt

### list all capitalized Unicode words in a file

    ugrep -o '\p{Upper}\p{Lower}*' places.txt

To include the byte offset of the matches counting from the start of the file:

    ugrep -b -o '\p{Upper}\p{Lower}*' places.txt

### display the lines containing laughing face emojis

    ugrep -o '[😀-😏]' birthday.txt

Or:

    ugrep -o '[\x{1F600}-\x{1F60F}]' birthday.txt

### display lines containing the names Gödel (or Goedel), Escher, or Bach in files GEB.txt and wiki.txt

    ugrep 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To display lines that do not contain the names Gödel (or Goedel), Escher, or
Bach:

    ugrep -v 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To count the number of lines containing the names Gödel (or Goedel), Escher, or
Bach:

    ugrep -c 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

To count the total number of occurrences of the names Gödel (or Goedel),
Escher, or Bach:

    ugrep -c -g 'G(ö|oe)del|Escher|Bach' GEB.txt wiki.txt

### check if a file contains any non-ASCII (i.e. Unicode) characters

    ugrep -q '[^[:ascii:]]' myfile && echo "contains Unicode"

To invert the match:

    ugrep -v -q '[^[:ascii:]]' myfile && echo "does not contain Unicode"

### searching UTF-encoded files

    ugrep 'lorem' utf16lorem.txt

To make sure we match `lorem` as a word and in lower/upper case:

    ugrep -w -i 'lorem' utf16lorem.txt

When utf16lorem.txt has no UTF-16 BOM:

    ugrep --file-format=UTF-16 -w -i 'lorem' utf16lorem.txt

### search for an identifier in source code

    ugrep -nk -o '\<main\>' myfile.cpp

Using a "negative pattern" to ignore unwanted matches in C/C++ quoted strings
(strings are matched with `"(\\.|\\\r?\n|[^\\\n"])*"` and may span multiple
lines so we must use option `-o`):

    ugrep -nk -o -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*")' myfile.cpp

Using a negative pattern to also ignore unwanted matches in C/C++ comments:

    ugrep -nk -o -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*(.|\n)*?\*/)' myfile.cpp

Pattern syntax
--------------

Command-line options
--------------------

The following options are currently available:

    -b, --byte-offset
            The offset in bytes of a matched pattern is displayed in front of
            the respective matched line.
    -c, --count
            Only a count of selected lines is written to standard output.
            When used with option -g, counts the number of patterns matched.
    --colour[=when], --color[=when]
            Mark up the matching text with the expression stored in the
            GREP_COLOR environment variable.  The possible values of when can
            be `never', `always' or `auto'.
    -E, --extended-regexp
            Ignored, intended for grep compatibility.
    -e pattern, --regexp=pattern
            Specify a pattern used during the search of the input: an input
            line is selected if it matches any of the specified patterns.
            This option is most useful when multiple -e options are used to
            specify multiple patterns, or when a pattern begins with a dash
            (`-').
    --file-format=format
            The input file format.  The possible values of format can be:
            binary ISO-8859-1 ASCII EBCDIC UTF-8 UTF-16 UTF-16BE UTF-16LE
            UTF-32 UTF-32BE UTF-32LE CP-437 CP-850 CP-855 CP-1250 CP-1251
            CP-1252 CP-1253 CP-1254 CP-1255 CP-1256 CP-1257 CP-1258
    -F, --fixed-strings
            Interpret pattern as a set of fixed strings (i.e. force ugrep to
            behave as fgrep).
    --free-space
            Spacing (blanks and tabs) in regular expressions are ignored.
    -g, --no-group
            Do not group pattern matches on the same line.  Display the
            matched line again for each additional pattern match.
    -H
            Always print filename headers with output lines.
    -h, --no-filename
            Never print filename headers (i.e. filenames) with output lines.
    -?, --help
            Print a help message.
    -i, --ignore-case
            Perform case insensitive matching. This option applies
            case-insensitive matching of ASCII characters in the input.
            By default, ugrep is case sensitive.
    -k, --column-number
            The column number of a matched pattern is displayed in front of
            the respective matched line, starting at column 1.  Tabs are
            expanded before columns are counted.
    -n, --line-number
            Each output line is preceded by its relative line number in the
            file, starting at line 1.  The line number counter is reset for
            each file processed.
    -o, --only-matching
            Prints only the matching part of the lines.  Allows a pattern
            match to span multiple lines.
    -q, --quiet, --silent
            Quiet mode: suppress normal output.  ugrep will only search a file
            until a match has been found, making searches potentially less
            expensive.  Allows a pattern match to span multiple lines.
    -s, --no-messages
            Silent mode.  Nonexistent and unreadable files are ignored (i.e.
            their error messages are suppressed).
    --tabs=size
            Set the tab size to 1, 2, 4, or 8 to expand tabs for option -k.
    -V, --version
            Display version information and exit.
    -v, --invert-match
            Selected lines are those not matching any of the specified
            patterns.
    -w, --word-regexp
            The pattern is searched for as a word (as if surrounded by
            `\<' and `\>').
    -x, --line-regexp
            Only input lines selected against an entire pattern are considered
            to be matching lines (as if surrounded by ^ and $).

    The ugrep utility exits with one of the following values:

    0       One or more lines were selected.
    1       No lines were selected.
    >1      An error occurred.

[reflex-url]: https://www.genivia.com/reflex.html
[bsd-3-image]: https://img.shields.io/badge/license-BSD%203--Clause-blue.svg
[bsd-3-url]: https://opensource.org/licenses/BSD-3-Clause
