C patterns
==========

Traditional C with non-Unicode identifiers.

- `comments` matches comments, requires ugrep option -o.
- `defines` matches `#define` lines, requires ugrep option -o to match multi-line `#define`.
- `directives` matches #-directives.
- `doc_comments` matches Doxygen comments.
- `enum_defs` matches enumerations, requires ugrep option -o.
- `function_defs` matches function definitions.
- `includes` matches `#include` lines.
- `names` matches identifiers (and keywords).
- `strings` matches strings, requires ugrep option -o to match multi-line strings.
- `struct_defs` matches struct definitions, requires ugrep option -o.
- `typedefs` matches `typedef` lines.
- `zap_commands` removes command keywords from matches.
- `zap_comments` removes comments from matches, requires ugrep option -o.
- `zap_directives` removes #-directive lines from matches.
- `zap_strings` removes strings from matches, requires ugrep option -o.
