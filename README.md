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
and negative patterns to skip unwanted pattern matches to produce more precise
results.

**ugrep** searches UTF-encoded input when UTF BOM
([byte order mark](https://en.wikipedia.org/wiki/Byte_order_mark)) are present
and ASCII and UTF-8 when no UTF BOM is present.  Option `--file-format` permits
many other file formats to be searched, such as ISO-8859-1, EBCDIC, and code
pages 437, 850, 858, 1250 to 1258.

**ugrep** uses command-line options that are compatible with GNU
[grep](https://www.gnu.org/software/grep/manual/grep.html).

**ugrep** is currently in beta release with new features being added in the
near future.

If you like **ugrep**, please contribute or let us know what you like, dislike,
or want to see in future releases.

Examples
--------

### display the lines in a file that contain capitalized Unicode words

    ugrep '\p{Upper}\p{Lower}*' places.txt

To include the line and column numbers and color-highlight the matches:

    ugrep -n -k --color '\p{Upper}\p{Lower}*' places.txt

### list all capitalized Unicode words in a file

    ugrep -o '\p{Upper}\p{Lower}*' places.txt

To include the byte offset of the matches counting from the start of the file:

    ugrep -b -o '\p{Upper}\p{Lower}*' places.txt

### display lines containing Unicode characters

    ugrep -o '[😀-😏]' birthday.txt

Or:

    ugrep -o '[\x{1F600}-\x{1F60F}]' birthday.txt

### display lines containing the names Gödel (or Goedel), Escher, or Bach

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

To make sure we match `lorem` as a word in lower/upper case:

    ugrep -w -i 'lorem' utf16lorem.txt

When utf16lorem.txt has no UTF-16 BOM:

    ugrep --file-format=UTF-16 -w -i 'lorem' utf16lorem.txt

### search for an identifier in source code

    ugrep -nk -o '\<main\>' myfile.cpp

Using a "negative pattern" to ignore unwanted matches in C/C++ quoted strings
(C/C++ strings are matched with `"(\\.|\\\r?\n|[^\\\n"])*"` and may span
multiple lines, so we should use option `-o` to block-buffer the input):

    ugrep -nk -o -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*")' myfile.cpp

Using a negative pattern to also ignore unwanted matches in C/C++ comments:

    ugrep -nk -o -e '\<main\>' -e '(?^"(\\.|\\\r?\n|[^\\\n"])*"|//.*|/\*(.|\n)*?\*/)' myfile.cpp

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
  `\x{3B1}` | matches Unicode character U+03B1 or `α`
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
  `(?:φ)`   | matches `φ` without group capture
  `(?=φ)`   | matches `φ` without consuming it
  `(?^φ)`   | matches `φ` and ignore it to continue matching
  `^φ`      | matches `φ` at the start of input or start of a line
  `φ$`      | matches `φ` at the end of input or end of a line
  `\Aφ`     | matches `φ` at the start of input
  `φ\z`     | matches `φ` at the end of input
  `\bφ`     | matches `φ` starting at a word boundary
  `φ\b`     | matches `φ` ending at a word boundary
  `\Bφ`     | matches `φ` starting at a non-word boundary
  `φ\B`     | matches `φ` ending at a non-word boundary
  `\<φ`     | matches `φ` that starts a word
  `\>φ`     | matches `φ` that starts a non-word
  `φ\<`     | matches `φ` that ends a non-word
  `φ\>`     | matches `φ` that ends a word
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
4. Concatenation `φψ` (including trailing context `φ/ψ`)
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

  POSIX Category | POSIX form   | Matches
  -------------- | ------------ | -----------------------------------------------
  `\p{ASCII}`    | `[:ascii:]`  | matches any ASCII character
  `\p{Space}`    | `[:space:]`  | matches a white space character `[ \t\n\v\f\r]` same as `\s`
  `\p{Xdigit}`   | `[:xdigit:]` | matches a hex digit `[0-9A-Fa-f]`
  `\p{Cntrl}`    | `[:cntrl:]`  | matches a control character `[\x00-\0x1f\x7f]`
  `\p{Print}`    | `[:print:]`  | matches a printable character `[\x20-\x7e]`
  `\p{Alnum}`    | `[:alnum:]`  | matches a alphanumeric character `[0-9A-Za-z]`
  `\p{Alpha}`    | `[:alpha:]`  | matches a letter `[A-Za-z]`
  `\p{Blank}`    | `[:blank:]`  | matches a blank `[ \t]` same as `\h`
  `\p{Digit}`    | `[:digit:]`  | matches a digit `[0-9]` same as `\d`
  `\p{Graph}`    | `[:graph:]`  | matches a visible character `[\x21-\x7e]`
  `\p{Lower}`    | `[:lower:]`  | matches a lower case letter `[a-z]` same as `\l`
  `\p{Punct}`    | `[:punct:]`  | matches a punctuation character `[\x21-\x2f\x3a-\x40\x5b-\x60\x7b-\x7e]`
  `\p{Upper}`    | `[:upper:]`  | matches an upper case letter `[A-Z]` same as `\u`
  `\p{Word}`     | `[:word:]`   | matches a word character `[0-9A-Za-z_]` same as `\w`
  `\d`           | `[:digit:]`  | matches a digit `[0-9]`
  `\D`           | `[:^digit:]` | matches a non-digit `[^0-9]`
  `\h`           | `[:blank:]`  | matches a blank character `[ \t]`
  `\H`           | `[:^blank:]` | matches a non-blank character `[^ \t]`
                 | `[:space:]`  | matches a white space character `[ \t\n\v\f\r]`
                 | `[:^space:]` | matches a non-white space `[^ \t\n\v\f\r]`
                 | `[:lower:]`  | matches a lower case letter `[a-z]`
                 | `[:^lower:]` | matches a non-lower case letter `[^a-z]`
                 | `[:upper:]`  | matches an upper case letter `[A-Z]`
                 | `[:^upper:]` | matches a nonupper case letter `[^A-Z]`
                 | `[:word:]`   | matches a word character `[0-9A-Za-z_]`
                 | `[:^word:]`  | matches a non-word character `[^0-9A-Za-z_]`

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
  `.`                                    | matches any single Unicode character except newline (including \ref invalid-utf)
  `\X`                                   | matches any ISO-8859-1 or Unicode character (with or without the `−−unicode` option)
  `\R`                                   | matches a Unicode line break
  `\s`, `\p{Zs}`                         | matches a white space character with Unicode sub-propert Zs
  `\l`, `\p{Ll}`                         | matches a lower case letter with Unicode sub-property Ll
  `\u`, `\p{Lu}`                         | matches an upper case letter with Unicode sub-property Lu
  `\w`, `\p{Word}`                       | matches a Unicode word character with property L, Nd, or Pc
  `\p{Unicode}`                          | matches any Unicode character (U+00 to U+10FFFF minus U+D800 to U+DFFF)
  `\p{ASCII}`                            | matches an ASCII character U+0000 to U+007F)
  `\p{Non_ASCII_Unicode}`                | matches a non-ASCII character U+80 to U+10FFFF minus U+D800 to U+DFFF)
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

Wanted - TODO
-------------

- Like grep, traverse directory contents to search files, and support options `-R` and `-r`, `--recursive`.
- Like grep, display context with `-A`, `-B`, and `-C`, `--context`.
- Like grep, read patterns from a file with `-f`, `--file=file`.
- Should detect "binary files" like grep and skip them?
- Open files in binary mode "rb" when `--binary-files` option is specified?
- ...

Bugs - FIXME
------------

- Pattern `^$` does not match empty lines, because `find()` does not permit
  empty matches.
- Back-references are not supported.

[reflex-url]: https://www.genivia.com/reflex.html
[bsd-3-image]: https://img.shields.io/badge/license-BSD%203--Clause-blue.svg
[bsd-3-url]: https://opensource.org/licenses/BSD-3-Clause
