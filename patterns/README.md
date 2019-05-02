This directory contains a collection of patterns that are helpful to search
source code files.  Use ugrep option `-f` to specify one or more pattern files
for searching.

For example, to display C/C++ comments and strings in a file with line numbers:

    ugrep -n -o -f patterns/c_comments -f patterns/c_strings file.cpp

To display Java identifiers (Unicode), but skip matches in comments and strings:

    ugrep -n -o -e '\p{JavaIdentifierStart}\p{JavaIdentifierPart}*' -f patterns/java_zap_comments -f patterns/java_zap_strings file.cpp

This list of patterns will be expanded over time.

We love your contributions! 😍
