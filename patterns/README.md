ugrep predefined patterns
=========================

This directory contains a collection of patterns that are helpful to search
source code files.  Use ugrep option `-f` to specify one or more pattern files
to use for searching these patterns in files.

The list of patterns defined in this directory will expand over time.

For example, to display class definitions in C++ files in the working directory

    ugrep -r -tc++ -f c++/classes

To display Java identifiers (Unicode) with the line numbers of the matches, but
skipping all matches of identifiers in comments and strings:

    ugrep -r -n -f java/names -f java/zap_comments -f java/zap_strings

Pattern files contain one or more regex patters, one pattern per line.  Empty
lines and lines starting with a `#` are ignored in pattern files.  A starting
line with `###-o` auto-enables option `-o` for all patterns matched.

Patterns that requiring Unicode matching should be placed in Unicode mode with
`(?u:X)`, just in case to prevent ugrep option `-U` from disabling them.

We love to receive your contributions to this effort! ❤️
