This directory contains a collection of patterns that are helpful to search
source code files.  Use ugrep option `-f` to specify one or more pattern files
for searching.

Many patterns require option `-o` to match the pattern across multiple lines.
Otherwise you may miss out on finding matches.  For example, strings and
comments may span multiple lines, such as Python docstrings.

For example, to display C/C++ comments and strings in a file

    ugrep -o -f patterns/c/comments -f patterns/c/strings file.cpp

To display Java identifiers (Unicode) with the line numbers of the matches, but
skipping all matches of identifiers in comments and strings:

    ugrep -n -o -e '\p{JavaIdentifierStart}\p{JavaIdentifierPart}*' -f patterns/java/zap_comments -f patterns/java/zap_strings file.cpp

This list of patterns will expand over time.

We love your contributions to this effort! ❤️
