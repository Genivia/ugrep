Ruby patterns
=============

Caveat: string matching is limited to quoted strings, `%Q/.../`, `%Q(...)`,
`%q/.../` and `%q(...)`.  The latter forms could be matched more generally
using back references (not yet supported) and lazy quantifiers, using regex
`%q(.).*?\1`.

