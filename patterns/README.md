This directory contains a collection of patterns to search source code files.

Use ugrep option `-f` to specify one or more pattern files for searching.

For example to display C/C++ comments and strings using patterns in file
`patterns/c_comments` and `patterns/c_strings`:

    ugrep -o -f patterns/c_comments -f patterns/c_strings file.cpp

