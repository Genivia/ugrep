C patterns
==========

Traditional C with non-Unicode identifiers.

- `comments` matches comments, auto-enables ugrep option -o
- `defines` matches `#define` lines, auto-enables ugrep option -o to match multi-line `#define`
- `directives` matches #-directives, auto-enables ugrep option -o to match multi-line directives
- `doc_comments` matches Doxygen comments, auto-enables ugrep option -o
- `enums` matches enumerations, auto-enables ugrep option -o
- `functions` matches function definitions, recommend ugrep option -o
- `includes` matches `#include` lines
- `names` matches identifiers (and keywords)
- `strings` matches strings, auto-enables ugrep option -o to match multi-line strings
- `structs` matches struct definitions, auto-enables ugrep option -o
- `typedefs` matches `typedef` lines
- `zap_commands` removes command keywords from matches
- `zap_comments` removes comments from matches, recommend ugrep option -o
- `zap_directives` removes #-directive lines from matches
- `zap_strings` removes strings from matches, recommend ugrep option -o
