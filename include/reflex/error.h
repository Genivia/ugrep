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
@file      error.h
@brief     RE/flex regex errors
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_ERROR_H
#define REFLEX_ERROR_H

#include <cstdio>
#include <stdexcept>
#include <string>

namespace reflex {

inline std::string ztoa(size_t n)
{
  char buf[24];
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
  sprintf_s(buf, sizeof(buf), "%zu", n);
#else
  snprintf(buf, sizeof(buf), "%zu", n);
#endif
  return std::string(buf);
}

/// Regex syntax error exception error code.
typedef int regex_error_type;

/// Regex syntax error exceptions.
class regex_error : public std::runtime_error {
 public:
  static const regex_error_type mismatched_parens     = 0;  ///< mismatched ( )
  static const regex_error_type mismatched_braces     = 1;  ///< mismatched { }
  static const regex_error_type mismatched_brackets   = 2;  ///< mismatched [ ]
  static const regex_error_type mismatched_quotation  = 3;  ///< mismatched `\Q...\E` or `"..."` quotation
  static const regex_error_type empty_expression      = 4;  ///< regex (sub)expression should not be empty
  static const regex_error_type empty_class           = 5;  ///< class `[...]` is empty, e.g. `[a&&[b]]`
  static const regex_error_type invalid_class         = 6;  ///< invalid character class name or code point
  static const regex_error_type invalid_class_range   = 7;  ///< invalid character class range, e.g. `[Z-A]`
  static const regex_error_type invalid_escape        = 8;  ///< invalid escape character
  static const regex_error_type invalid_anchor        = 9;  ///< invalid anchor
  static const regex_error_type invalid_repeat        = 10; ///< invalid repeat range, e.g. `{10,1}`
  static const regex_error_type invalid_quantifier    = 11; ///< invalid lazy/possessive quantifier
  static const regex_error_type invalid_modifier      = 12; ///< invalid `(?ismx:)` modifier
  static const regex_error_type invalid_collating     = 13; ///< invalid collating element [[.name.]]
  static const regex_error_type invalid_backreference = 14; ///< invalid backreference
  static const regex_error_type invalid_syntax        = 15; ///< invalid regex syntax
  static const regex_error_type exceeds_length        = 16; ///< regex exceeds length limit (reflex::Pattern class only)
  static const regex_error_type exceeds_limits        = 17; ///< regex exceeds complexity limits (reflex::Pattern class only)
  static const regex_error_type undefined_name        = 18; ///< undefined macro name (reflex tool only)
  static const regex_error_type cannot_save_tables    = 19; ///< cannot save tables file (reflex tool only)
  /// Construct regex error info.
  regex_error(
      regex_error_type   code,
      const std::string& pattern,
      size_t             pos = 0)
    :
      std::runtime_error(regex_error_message_code(code, pattern.c_str(), pos)),
      code_(code),
      pos_(pos)
  { }
  /// Construct regex error info.
  regex_error(
      regex_error_type code,
      const char      *pattern,
      size_t           pos = 0)
    :
      std::runtime_error(regex_error_message_code(code, pattern, pos)),
      code_(code),
      pos_(pos)
  { }
  /// Construct regex error info.
  regex_error(
      const char        *message,
      const std::string& pattern,
      size_t             pos = 0)
    :
      std::runtime_error(regex_error_message(message, pattern.c_str(), pos)),
      code_(invalid_syntax),
      pos_(pos)
  { }
  /// Construct regex error info.
  regex_error(
      const char      *message,
      const char      *pattern,
      size_t           pos = 0)
    :
      std::runtime_error(regex_error_message(message, pattern, pos)),
      code_(invalid_syntax),
      pos_(pos)
  { }
  /// Returns error code, a reflex::regex_error_type constant.
  regex_error_type code()
    const
  {
    return code_;
  }
  /// Returns position of the error in the regex.
  size_t pos()
    const
  {
    return pos_;
  }
 private:
  static std::string regex_error_message_code(
      regex_error_type code,
      const char      *pattern,
      size_t           pos);
  static std::string regex_error_message(
      const char *message,
      const char *pattern,
      size_t      pos);
  static size_t displen(const char *s, size_t k);
  static const char *disppos(const char *s, size_t k);
  regex_error_type code_;
  size_t           pos_;
};

} // namespace reflex

#endif
