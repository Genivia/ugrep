This directory contains a collection of patterns that are helpful to search
source code files.  Use ugrep option `-f` to specify one or more pattern files
to use for searching these patterns in files.

Most patterns require option `-o` to match the pattern across multiple lines.
Otherwise you may miss out on finding matches.  Strings and comments may span
multiple lines, such as Python docstrings, requiring option `-o`.

For example, to display all class defitions in C++ files in myproject directory:

    ugrep -R -o -tc++ -f patterns/c++/class_defs myproject

To display Java identifiers (Unicode) with the line numbers of the matches, but
skipping all matches of identifiers in comments and strings:

    ugrep -R -n -o -f patterns/java/names -f patterns/java/zap_comments -f patterns/java/zap_strings myproject

This list of patterns defined in this directory will expand over time.

We love your contributions to this effort! ❤️
