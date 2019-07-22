This directory contains a collection of patterns that are helpful to search
source code files.  Use ugrep option `-f` to specify one or more pattern files
to use for searching these patterns in files.

Most patterns require option `-o` to match the pattern across multiple lines.
Otherwise you may miss out on finding matches.  Strings and comments may span
multiple lines, such as Python docstrings, requiring option `-o`.

For example, to display all class defitions in C++ files in myproject directory:

    ugrep -R -o -tc++ -f c++/classes myproject

To display Java identifiers (Unicode) with the line numbers of the matches, but
skipping all matches of identifiers in comments and strings:

    ugrep -R -n -o -f java/names -f java/zap_comments -f java/zap_strings myproject

This list of patterns defined in this directory will expand over time.

Patterns requiring Unicode matching are placed in Unicode mode with (?u:X),
just in case to prevent ugrep option -U from disabling them.

We love your contributions to this effort! ❤️

**If you encounter a problem with a pattern or you discover a bug in a pattern,
please open an issue at GitHub https://github.com/Genivia/ugrep/issues**

