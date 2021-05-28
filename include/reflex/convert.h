/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      convert.h
@brief     RE/flex regex converter
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_CONVERT_H
#define REFLEX_CONVERT_H

#include <reflex/error.h>
#include <string>
#include <map>

#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# pragma warning( disable : 4290 )
#endif

namespace reflex {

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex convert                                                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// Conversion flags for reflex::convert.
typedef int convert_flag_type;

namespace convert_flag {
  const convert_flag_type none       = 0x0000; ///< no conversion (default)
  const convert_flag_type basic      = 0x0001; ///< convert basic regex (BRE) to extended regex (ERE)
  const convert_flag_type unicode    = 0x0002; ///< convert . (dot), `\s`, `\w`, `\l`, `\u`, `\S`, `\W`, `\L`, `\U` to Unicode
  const convert_flag_type recap      = 0x0004; ///< remove capturing groups, add capturing groups to the top level
  const convert_flag_type lex        = 0x0008; ///< convert Lex/Flex regular expression syntax
  const convert_flag_type u4         = 0x0010; ///< convert `\uXXXX` and UTF-16 surrogate pairs
  const convert_flag_type anycase    = 0x0020; ///< convert regex to ignore case, same as `(?i)`
  const convert_flag_type multiline  = 0x0040; ///< regex with multiline anchors `^` and `$`, same as `(?m)`
  const convert_flag_type dotall     = 0x0080; ///< convert `.` (dot) to match all, same as `(?s)`
  const convert_flag_type freespace  = 0x0100; ///< convert regex by removing spacing, same as `(?x)`
  const convert_flag_type notnewline = 0x0200; ///< inverted character classes and \s do not match newline `\n`
  const convert_flag_type permissive = 0x0400; ///< convert Unicode to compact UTF-8 patterns and DFA, permits some invalid UTF-8 sequences
}

/// @brief Returns the converted regex string given a regex library signature and conversion flags, throws regex_error.
///
/// A regex library signature is a string of the form `"decls:escapes?+."`.
///
/// The optional `"decls:"` part specifies which modifiers and other special `(?...)` constructs are supported:
/// - non-capturing group `(?:...)` is supported
/// - letters and digits specify which modifiers e.g. (?ismx) are supported:
/// - 'i' specifies that `(?i...)` case-insensitive matching is supported
/// - 'm' specifies that `(?m...)` multiline mode is supported for the ^ and $ anchors
/// - 's' specifies that `(?s...)` dotall mode is supported
/// - 'x' specifies that `(?x...)` freespace mode is supported
/// - ... any other letter or digit modifier, where digit modifiers support `(?123)` for example
/// - `#` specifies that `(?#...)` comments are supported
/// - `=` specifies that `(?=...)` lookahead is supported
/// - `<` specifies that `(?'...)` 'name' groups are supported
/// - `<` specifies that `(?<...)` lookbehind and <name> groups are supported
/// - `>` specifies that `(?>...)` atomic groups are supported
/// - `>` specifies that `(?|...)` group resets are supported
/// - `>` specifies that `(?&...)` subroutines are supported
/// - `>` specifies that `(?(...)` conditionals are supported
/// - `!` specifies that `(?!=...)` and `(?!<...)` are supported
/// - `^` specifies that `(?^...)` negative (reflex) patterns are supported
/// - `*` specifies that `(*VERB)` verbs are supported
///
/// The `"escapes"` characters specify which standard escapes are supported:
/// - `a` for `\a` (BEL U+0007)
/// - `b` for `\b` the `\b` word boundary
/// - `c` for `\cX` control character specified by `X` modulo 32
/// - `d` for `\d` digit `[0-9]` ASCII or Unicode digit
/// - `e` for `\e` ESC U+001B
/// - `f` for `\f` FF U+000C
/// - `j` for `\g` group capture e.g. \g{1}
/// - `h` for `\h` ASCII blank `[ \t]` (SP U+0020 or TAB U+0009)
/// - `i` for `\i` reflex indent anchor
/// - `j` for `\j` reflex dedent anchor
/// - `j` for `\k` reflex undent anchor or group capture e.g. \k{1}
/// - `l` for `\l` lower case letter `[a-z]` ASCII or Unicode letter
/// - `n` for `\n` LF U+000A
/// - `o` for `\o` octal ASCII or Unicode code
/// - `p` for `\p{C}` Unicode character classes, also implies Unicode ., \x{X}, \l, \u, \d, \s, \w, and UTF-8 patterns
/// - `r` for `\r` CR U+000D
/// - `s` for `\s` space (SP, TAB, LF, VT, FF, or CR)
/// - `t` for `\t` TAB U+0009
/// - `u` for `\u` ASCII upper case letter `[A-Z]` (when not followed by `{XXXX}`)
/// - `v` for `\v` VT U+000B
/// - `w` for `\w` ASCII word-like character `[0-9A-Z_a-z]`
/// - `x` for `\xXX` 8-bit character encoding in hexadecimal
/// - `y` for `\y` word boundary
/// - `z` for `\z` end of input anchor
/// - ``` for `\`` begin of input anchor
/// - `'` for `\'` end of input anchor
/// - `<` for `\<` left word boundary
/// - `>` for `\>` right word boundary
/// - `A` for `\A` begin of input anchor
/// - `B` for `\B` non-word boundary
/// - `D` for `\D` ASCII non-digit `[^0-9]`
/// - `H` for `\H` ASCII non-blank `[^ \t]`
/// - `L` for `\L` ASCII non-lower case letter `[^a-z]`
/// - `N` for `\N` not a newline
/// - `P` for `\P{C}` Unicode inverse character classes, see 'p'
/// - `Q` for `\Q...\E` quotations
/// - `R` for `\R` Unicode line break
/// - `S` for `\S` ASCII non-space (no SP, TAB, LF, VT, FF, or CR)
/// - `U` for `\U` ASCII non-upper case letter `[^A-Z]`
/// - `W` for `\W` ASCII non-word-like character `[^0-9A-Z_a-z]`
/// - `X` for `\X` any Unicode character
/// - `Z` for `\Z` end of input anchor, before the final line break
/// - `0` for `\0nnn` 8-bit character encoding in octal requires a leading `0`
/// - '1' to '9' for backreferences (not applicable to lexer specifications)
///
/// Note that 'p' is a special case to support Unicode-based matchers that
/// natively support UTF8 patterns and Unicode classes \p{C}, \P{C}, \w, \W,
/// \d, \D, \l, \L, \u, \U, \N, and \x{X}.  Basically, 'p' prevents conversion
/// of Unicode patterns to UTF8.  This special case does not support {NAME}
/// expansions in bracket lists such as [a-z||{upper}] and {lower}{+}{upper}
/// used in lexer specifications.
///
/// The optional `"?+"` specify lazy and possessive support:
/// - `?` lazy quantifiers for repeats are supported
/// - `+` possessive quantifiers for repeats are supported
///
/// An optional `"."` (dot) specifies that dot matches any character except newline.
/// A dot is implied by the presence of the 's' modifier, and can be omitted in that case.
///
/// An optional `"["` specifies that bracket list union, intersection, and
/// subtraction are supported, i.e. [\w--[a-z]].
std::string convert(
    const char                              *pattern,                    ///< regex string pattern to convert
    const char                              *signature,                  ///< regex library signature
    convert_flag_type                        flags = convert_flag::none, ///< conversion flags
    const std::map<std::string,std::string> *macros = NULL)              ///< {name} macros to expand
  ;

inline std::string convert(
    const std::string&                       pattern,
    const char                              *signature,
    convert_flag_type                        flags = convert_flag::none,
    const std::map<std::string,std::string> *macros = NULL)
{
  return convert(pattern.c_str(), signature, flags, macros);
}

} // namespace reflex

#endif
