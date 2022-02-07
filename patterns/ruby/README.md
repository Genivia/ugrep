Ruby patterns
=============

Caveat: string matching is limited to quoted strings, `%Q/.../`, `%Q(...)`,
`%q/.../` and `%q(...)`.  The latter forms could be matched more generally
using back references (not yet supported) and lazy quantifiers, using regex
`%q(.).*?\1`.

- `classes` matches class definition lines
- `comments` matches comments
- `defs` matches function definition lines
- `modules` matches module lines
- `names` matches identifiers (and keywords) not inside strings and comments
- `strings` matches strings
- `zap_comments` removes comments from matches
- `zap_strings` removes strings from matches
