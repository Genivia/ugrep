Ruby patterns
=============

Caveat: string matching is limited to quoted strings, `%Q/.../`, `%Q(...)`,
`%q/.../` and `%q(...)`.  The latter forms could be matched more generally
using back references (not yet supported) and lazy quantifiers, using regex
`%q(.).*?\1`.

- `classes` matches class definition lines
- `comments` matches comments, auto-enables ugrep option -o
- `defs` matches function definition lines
- `modules` matches module lines
- `names` matches identifiers (and keywords)
- `strings` matches strings, auto-enables ugrep option -o
- `zap_comments` removes comments from matches, recommend ugrep option -o
- `zap_strings` removes strings from matches, recommend ugrep option -o
