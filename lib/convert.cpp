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
@file      convert.cpp
@brief     RE/flex regex converter
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2016-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <reflex/convert.h>
#include <reflex/posix.h>
#include <reflex/ranges.h>
#include <reflex/unicode.h>
#include <reflex/utf8.h>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace reflex {

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter constants                                                 //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// regex meta chars
static const char regex_meta[] = "#$()*+.?[\\]^{|}";

/// regex chars that when escaped should be un-escaped
static const char regex_unescapes[] = "!\"#%&',-/:;@`";

/// regex chars that when escaped should be converted to \xXX
static const char regex_escapes[] = "~";

/// regex anchors and boundaries
static const char regex_anchors[] = "AZzBby<>";

/// \a (BEL), \b (BS), \t (TAB), \n (LF), \v (VT), \f (FF), \r (CR)
static const char regex_abtnvfr[] = "abtnvfr";

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter helper functions                                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

inline int lowercase(int c)
{
  return static_cast<unsigned char>(c | 0x20);
}

inline int uppercase(int c)
{
  return static_cast<unsigned char>(c & ~0x20);
}

static std::string posix_class(const char *s, int esc)
{
  std::string regex;
  const int *wc = Posix::range(s + (s[0] == '^'));
  if (wc != NULL)
  {
    regex.assign("[");
    if (s[0] == '^')
      regex.push_back('^');
    for (; wc[1] != 0; wc += 2)
      regex.append(latin1(wc[0], wc[1], esc, false));
    regex.push_back(']');
  }
  return regex;
}

static std::string unicode_class(const char *s, int esc, convert_flag_type flags, const char *par)
{
  std::string regex;
  const int *wc = Unicode::range(s + (s[0] == '^'));
  if (wc != NULL)
  {
    if (s[0] == '^') // inverted class \P{C} or \p{^C}
    {
      if (wc[0] > 0x00)
      {
        if (wc[0] > 0xDFFF)
        {
          // exclude U+D800 to U+DFFF
          regex.assign(utf8(0x00, 0xD7FF, esc, par, !(flags & convert_flag::permissive))).push_back('|');
          if (wc[0] > 0xE000)
            regex.append(utf8(0xE000, wc[0] - 1, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
        else
        {
          regex.assign(utf8(0x00, wc[0] - 1, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
      }
      int last = wc[1] + 1;
      wc += 2;
      for (; wc[1] != 0; wc += 2)
      {
        if (last <= 0xD800 && wc[0] > 0xDFFF)
        {
          // exclude U+D800 to U+DFFF
          if (last < 0xD800)
            regex.append(utf8(last, 0xD7FF, esc, par, !(flags & convert_flag::permissive))).push_back('|');
          if (wc[0] > 0xE000)
            regex.append(utf8(0xE000, wc[0] - 1, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
        else
        {
          regex.append(utf8(last, wc[0] - 1, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
        last = wc[1] + 1;
      }
      if (last <= 0x10FFFF)
      {
        if (last <= 0xD800)
        {
          // exclude U+D800 to U+DFFF
          if (last < 0xD800)
            regex.append(utf8(last, 0xD7FF, esc, par, !(flags & convert_flag::permissive))).push_back('|');
          regex.append(utf8(0xE000, 0x10FFFF, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
        else
        {
          regex.append(utf8(last, 0x10FFFF, esc, par, !(flags & convert_flag::permissive))).push_back('|');
        }
      }
      if (!regex.empty())
        regex.resize(regex.size() - 1);
    }
    else
    {
      regex.assign(utf8(wc[0], wc[1], esc, par, !(flags & convert_flag::permissive)));
      wc += 2;
      for (; wc[1] != 0; wc += 2)
        regex.append("|").append(utf8(wc[0], wc[1], esc, par, !(flags & convert_flag::permissive)));
    }
  }
  if (regex.find('|') != std::string::npos)
    regex.insert(0, par).push_back(')');
  return regex;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter checks for modifiers and escapes                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

inline void enable_modifier(int c, const char *pattern, size_t pos, std::map<size_t,std::string>& mod, size_t lev)
{
  if (c != 'i' && c != 'm' && c != 's' && c != 'u' && c != 'x')
    throw regex_error(regex_error::invalid_modifier, pattern, pos);
  mod[lev].push_back(c);
}

inline void disable_modifier(int c, const char *pattern, size_t pos, std::map<size_t,std::string>& mod, size_t lev)
{
  if (c != 'i' && c != 'm' && c != 's' && c != 'u' && c != 'x')
    throw regex_error(regex_error::invalid_modifier, pattern, pos);
  mod[lev].push_back(uppercase(c));
}

inline bool is_modified(const std::map<size_t,std::string>& mod, int c)
{
  for (std::map<size_t,std::string>::const_reverse_iterator i = mod.rbegin(); i != mod.rend(); ++i)
  {
    for (std::string::const_iterator a = i->second.begin(); a != i->second.end(); ++a)
    {
      if (c == *a)
        return true;
      if (uppercase(c) == *a)
        return false;
    }
  }
  return false;
}

inline bool supports_modifier(const char *signature, int c)
{
  const char *escapes = std::strchr(signature, ':');
  if (escapes == NULL)
    return false;
  const char *s = std::strchr(signature, c);
  return s && s < escapes;
}

inline bool supports_escape(const char *signature, int escape)
{
  if (!signature)
    return false;
  const char *escapes = std::strchr(signature, ':');
  return std::strchr(escapes != NULL ? escapes : signature, escape) != NULL;
}

inline int hex_or_octal_escape(const char *signature)
{
  if (supports_escape(signature, 'x'))
    return 'x';
  if (supports_escape(signature, '0'))
    return '0';
  return '\0';
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter hex conversions                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static const char* hex_escape(char *buf, int wc)
{
  static const char digits[] = "0123456789abcdef";
  char *s = buf;
  if (wc <= 0xFF)
  {
    *s++ = '\\';
    *s++ = 'x';
    *s++ = digits[wc >> 4 & 0xf];
    *s++ = digits[wc & 0xf];
    *s++ = '\0';
  }
  else
  {
    *s++ = '\\';
    *s++ = 'x';
    *s++ = '{';
    int n = 0;
    while (wc >> n)
      n += 4;
    while ((n -= 4) >= 0)
      *s++ = digits[wc >> n & 0xf];
    *s++ = '}';
    *s++ = '\0';
  }
  return buf;
}

static int convert_hex(const char *pattern, size_t len, size_t& pos, convert_flag_type flags)
{
  char hex[9];
  hex[0] = '\0';
  size_t k = pos;
  int c = pattern[k++];
  if (k < len && pattern[k] == '{')
  {
    char *s = hex;
    while (++k < len && s < hex + sizeof(hex) - 1 && (c = pattern[k]) != '}')
      *s++ = c;
    *s = '\0';
    if (k >= len || pattern[k] != '}')
      throw regex_error(regex_error::mismatched_braces, pattern, pos + 1);
  }
  else if (c == 'x' || (c == 'u' && (flags & convert_flag::u4)))
  {
    char *s = hex;
    size_t n = pos + 3;
    if (c == 'u')
      n += 2;
    while (k < n && k < len && std::isxdigit(c = pattern[k++]))
      *s++ = c;
    *s = '\0';
    --k;
  }
  if (hex[0] != '\0')
  {
    char *r;
    unsigned long n = std::strtoul(hex, &r, 16);
    if (*r != '\0' || n > 0x10FFFF)
      throw regex_error(regex_error::invalid_class, pattern, pos);
    pos = k;
    return static_cast<int>(n);
  }
  return -1;
}

static int convert_oct(const char *pattern, size_t len, size_t& pos)
{
  char oct[9];
  oct[0] = '\0';
  size_t k = pos;
  int c = pattern[k++];
  if (k < len && pattern[k] == '{')
  {
    char *s = oct;
    while (++k < len && s < oct + sizeof(oct) - 1 && (c = pattern[k]) != '}')
      *s++ = c;
    *s = '\0';
    if (k >= len || pattern[k] != '}')
      throw regex_error(regex_error::mismatched_braces, pattern, pos + 1);
  }
  if (oct[0] != '\0')
  {
    char *r;
    unsigned long n = std::strtoul(oct, &r, 8);
    if (*r != '\0' || n > 0x10FFFF)
      throw regex_error(regex_error::invalid_class, pattern, pos);
    pos = k;
    return static_cast<int>(n);
  }
  return -1;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Definition name expansion                                                 //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static const std::string& expand(const std::map<std::string,std::string> *macros, const char *pattern, size_t len, size_t& pos)
{
  // lookup {name} and expand without converting
  size_t k = pos++;
  while (pos < len && (std::isalnum(pattern[pos]) || pattern[pos] == '_' || (pattern[pos] & 0x80) == 0x80))
    ++pos;
  if (pos >= len || (pattern[pos] == '\\' ? pattern[pos + 1] != '}' : pattern[pos] != '}'))
    throw regex_error(regex_error::undefined_name, pattern, pos);
  std::string name(&pattern[k], pos - k);
  std::map<std::string,std::string>::const_iterator i = macros->find(name);
  if (i == macros->end())
    throw regex_error(regex_error::undefined_name, pattern, k);
  return i->second;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter bracket list character class conversions                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static std::string convert_unicode_ranges(const ORanges<int>& ranges, convert_flag_type flags, const char *signature, const char *par)
{
  std::string regex;
  int esc = hex_or_octal_escape(signature);
  for (ORanges<int>::const_iterator i = ranges.begin(); i != ranges.end(); ++i)
    regex.append(utf8(i->first, i->second - 1, esc, par, !(flags & convert_flag::permissive))).push_back('|');
  regex.resize(regex.size() - 1);
  regex.insert(0, par).push_back(')');
  return regex;
}

static std::string convert_posix_ranges(const ORanges<int>& ranges, const char *signature)
{
  int esc = hex_or_octal_escape(signature);
  std::string regex;
  bool negate = ranges.lo() == 0x00 && ranges.hi() >= 0x7F;
  if (negate && ranges.size() > 1)
  {
    ORanges<int> inverse(0x00, 0xFF);
    inverse -= ranges;
    regex = "[^";
    for (ORanges<int>::const_iterator i = inverse.begin(); i != inverse.end(); ++i)
      regex.append(latin1(i->first, i->second - 1, esc, false));
  }
  else
  {
    regex = "[";
    for (ORanges<int>::const_iterator i = ranges.begin(); i != ranges.end(); ++i)
      regex.append(latin1(i->first, i->second - 1, esc, false));
  }
  regex.push_back(']');
  return regex;
}

static void convert_anycase_ranges(ORanges<int>& ranges)
{
  ORanges<int> letters;
  letters.insert('A', 'Z');
  letters.insert('a', 'z');
  letters &= ranges;
  for (ORanges<int>::const_iterator i = letters.begin(); i != letters.end(); ++i)
    ranges.insert(i->first ^ 0x20, (i->second - 1) ^ 0x20);
}

static std::string convert_ranges(const char *pattern, size_t pos, ORanges<int>& ranges, const std::map<size_t,std::string>& mod, convert_flag_type flags, const char *signature, const char *par)
{
  if (is_modified(mod, 'i'))
    convert_anycase_ranges(ranges);
  if (is_modified(mod, 'u') && ranges.hi() > 0x7F)
    return convert_unicode_ranges(ranges, flags, signature, par);
  if (ranges.hi() > 0xFF)
    throw regex_error(regex_error::invalid_class, pattern, pos);
  return convert_posix_ranges(ranges, signature);
}

static void expand_list(const char *pattern, size_t len, size_t& loc, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, const char *signature, const char *par, const std::map<std::string,std::string> *macros, std::string& regex)
{
  bool no_newline = false;
  if (pos + 1 < len && pattern[pos] == '^')
  {
    no_newline = true;
    ++pos;
  }
  while (pos + 1 < len)
  {
    int c = pattern[pos];
    if (c == '\\')
    {
      c = pattern[++pos];
      if (c == 'p' || c == 'P')
      {
        // translate \p{POSIX} and \P{POSIX}, while leaving Unicode classes intact
        size_t k = ++pos;
        if (pos >= len)
          throw regex_error(regex_error::invalid_class, pattern, pos);
        // get name X of \pX, \PX, \p{X}, and \P{X}
        std::string name;
        if (pattern[pos] == '{')
        {
          size_t j = pos + 1;
          if (c == 'P')
            name.push_back('^');
          k = j;
          while (k < len && pattern[k] != '}')
            ++k;
          if (k >= len)
            throw regex_error(regex_error::mismatched_braces, pattern, pos);
          name.append(pattern, j, k - j);
        }
        else
        {
          if (c == 'P')
            name.push_back('^');
          name.push_back(pattern[pos]);
        }
        std::string translated;
        int esc = hex_or_octal_escape(signature);
        translated = posix_class(name.c_str(), esc);
        if (!translated.empty())
        {
          regex.append(&pattern[loc], pos - loc - 2).append(translated.substr(1, translated.size() - 2));
          loc = k + 1;
        }
        pos = k;
      }
      else if (c == 's' && (flags & convert_flag::notnewline))
      {
        if (is_modified(mod, 'u'))
          regex.append(&pattern[loc], pos - loc - 1).append("\\t\\x0b-\\r\\x85\\p{Z}");
        else
          regex.append(&pattern[loc], pos - loc - 1).append("\\h\\x0b-\\r\\x85\\xa0");
        loc = pos + 1;
      }
      else if (c == 'n')
      {
        no_newline = false;
      }
    }
    else if (c == '[' && (pattern[pos + 1] == ':' || pattern[pos + 1] == '.' || pattern[pos + 1] == '='))
    {
      ++pos;
      while (pos + 1 < len && pattern[++pos] != ']')
        continue;
    }
    else if (c == '|' && pattern[pos + 1] == '|' && pos + 3 < len && pattern[pos + 2] == '[')
    {
      // character class union [abc||[def]]
      if (!supports_escape(signature, '['))
        throw regex_error(regex_error::invalid_class, pattern, pos + 1);
      pos += 3;
      expand_list(pattern, len, loc, pos, flags, mod, signature, par, macros, regex);
    }
    else if (c == '&' && pattern[pos + 1] == '&' && pos + 3 < len && pattern[pos + 2] == '[')
    {
      // character class intersection [a-z&&[^aeiou]]
      if (!supports_escape(signature, '['))
        throw regex_error(regex_error::invalid_class, pattern, pos + 1);
      pos += 3;
      expand_list(pattern, len, loc, pos, flags, mod, signature, par, macros, regex);
    }
    else if (c == '-' && pattern[pos + 1] == '-' && pos + 3 < len && pattern[pos + 2] == '[')
    {
      // character class subtraction [a-z--[aeiou]]
      if (!supports_escape(signature, '['))
        throw regex_error(regex_error::invalid_class, pattern, pos + 1);
      pos += 3;
      expand_list(pattern, len, loc, pos, flags, mod, signature, par, macros, regex);
    }
    ++pos;
    if (pos >= len || pattern[pos] == ']')
      break;
  }
  if (pos >= len || pattern[pos] != ']')
    throw regex_error(regex_error::mismatched_brackets, pattern, loc);
  if (no_newline && (flags & convert_flag::notnewline))
  {
    regex.append(&pattern[loc], pos - loc).append("\\n");
    loc = pos;
  }
}

static void insert_escape_class(const char *pattern, size_t pos, const std::map<size_t,std::string>& mod, ORanges<int>& ranges)
{
  int c = pattern[pos];
  char name[2] = { static_cast<char>(lowercase(c)), '\0' };
  const int *translated = NULL;
  if (is_modified(mod, 'u'))
    translated = Unicode::range(name);
  else
    translated = Posix::range(name);
  if (translated == NULL)
    throw regex_error(regex_error::invalid_class, pattern, pos);
  if (std::islower(c))
  {
    for (const int *wc = translated; wc[1] != 0; wc += 2)
      ranges.insert(wc[0], wc[1]);
  }
  else
  {
    int last = 0x00;
    for (const int *wc = translated; wc[1] != 0; wc += 2)
    {
      if (wc[0] > 0x00)
      {
        if (last <= 0xD800 && wc[0] > 0xDFFF)
        {
          // exclude U+D800 to U+DFFF
          if (last < 0xD800)
            ranges.insert(last, 0xD7FF);
          if (wc[0] > 0xE000)
            ranges.insert(0xE000, wc[0] - 1);
        }
        else
        {
          ranges.insert(last, wc[0] - 1);
        }
      }
      last = wc[1] + 1;
    }
    if (is_modified(mod, 'u') && last <= 0x10FFFF)
    {
      if (last <= 0xD800)
      {
        // exclude U+D800 to U+DFFF
        if (last < 0xD800)
          ranges.insert(last, 0xD7FF);
        ranges.insert(0xE000, 0x10FFFF);
        ranges.insert(last, 0x10FFFF);
      }
    }
    else if (last <= 0xFF)
    {
      ranges.insert(last, 0xFF);
    }
  }
}

static int insert_escape(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges)
{
  int c = pattern[pos];
  if (c == 'c')
  {
    ++pos;
    if (pos >= len)
      throw regex_error(regex_error::invalid_escape, pattern, pos - 1);
    c = pattern[pos];
    if (c < 0x21 || c >= 0x7F)
      throw regex_error(regex_error::invalid_escape, pattern, pos);
    c &= 0x1F;
  }
  else if (c == 'e')
  {
    c = 0x1B;
  }
  else if (c == 'N')
  {
    if (is_modified(mod, 'u'))
    {
      ranges.insert(0, 9);
      ranges.insert(11, 0xD7FF);
      ranges.insert(0xE000, 0x10FFFF);
    }
    else
    {
      ranges.insert(0, 9);
      ranges.insert(11, 0xFF);
    }
  }
  else if (c >= '0' && c <= '7')
  {
    size_t k = pos + 3 + (pattern[pos] == '0');
    int wc = c - '0';
    while (++pos < k && pos < len && (c = pattern[pos]) >= '0' && c <= '7')
      wc = 8 * wc + c - '0';
    c = wc;
    --pos;
  }
  else if (c == 'u' || c == 'x')
  {
    size_t k = pos;
    c = convert_hex(pattern, len, k, flags);
    if (c >= 0)
    {
      pos = k;
    }
    else
    {
      insert_escape_class(pattern, pos, mod, ranges);
      return -1;
    }
  }
  else if (c == 'p' || c == 'P')
  {
    size_t k = ++pos;
    if (k >= len)
      throw regex_error(regex_error::invalid_class, pattern, k);
    std::string name;
    if (pattern[k] == '{')
    {
      size_t j = k + 1;
      k = j;
      while (k < len && pattern[k] != '}')
        ++k;
      if (k >= len)
        throw regex_error(regex_error::mismatched_braces, pattern, pos);
      name.assign(&pattern[j], k - j);
    }
    else
    {
      name.push_back(pattern[k]);
    }
    const int *translated = NULL;
    const char *s = name.c_str();
    if (s[0] == '^')
      ++s;
    if (is_modified(mod, 'u'))
      translated = Unicode::range(s);
    else if (translated == NULL)
      translated = Posix::range(s);
    if (translated == NULL)
      throw regex_error(regex_error::invalid_class, pattern, pos);
    if (c == 'P' || name.at(0) == '^')
    {
      int last = 0x00;
      for (const int *wc = translated; wc[1] != 0; wc += 2)
      {
        if (wc[0] > 0x00)
        {
          if (last <= 0xD800 && wc[0] > 0xDFFF)
          {
            // exclude U+D800 to U+DFFF
            if (last < 0xD800)
              ranges.insert(last, 0xD7FF);
            if (wc[0] > 0xE000)
              ranges.insert(0xE000, wc[0] - 1);
          }
          else
          {
            ranges.insert(last, wc[0] - 1);
          }
        }
        last = wc[1] + 1;
      }
      if (is_modified(mod, 'u') && last <= 0x10FFFF)
      {
        if (last <= 0xD800)
        {
          // exclude U+D800 to U+DFFF
          if (last < 0xD800)
            ranges.insert(last, 0xD7FF);
          ranges.insert(0xE000, 0x10FFFF);
        }
        else
        {
          ranges.insert(last, 0x10FFFF);
        }
      }
      else if (last <= 0xFF)
      {
        ranges.insert(last, 0xFF);
      }
    }
    else
    {
      for (const int *wc = translated; wc[1] != 0; wc += 2)
        ranges.insert(wc[0], wc[1]);
    }
    pos = k;
    return -1;
  }
  else if (c == 's' && (flags & convert_flag::notnewline))
  {
    // \s is the same as \p{Space} but without newline \n
    insert_escape_class(pattern, pos, mod, ranges);
    ranges.erase('\n');
    return -1;
  }
  else if (std::isalpha(c))
  {
    const char *s = std::strchr(regex_abtnvfr, c);
    if (s == NULL)
    {
      insert_escape_class(pattern, pos, mod, ranges);
      return -1;
    }
    c = static_cast<int>(s - regex_abtnvfr + '\a');
  }
  ranges.insert(c);
  return c;
}

static void insert_posix_class(const char *pattern, size_t len, size_t& pos, ORanges<int>& ranges)
{
  pos += 2;
  char buf[8] = "";
  char *name = buf;
  while (pos + 1 < len && name < buf + sizeof(buf) - 1 && (pattern[pos] != ':' || pattern[pos + 1] != ']'))
    *name++ = pattern[pos++];
  if (pos + 1 >= len)
    throw regex_error(regex_error::invalid_class, pattern, pos);
  *name = '\0';
  name = buf + (*buf == '^');
  if (name[1] != '\0')
  {
    name[0] = uppercase(name[0]);
    if (name[0] == 'X' && name[1] == 'd')
      name = const_cast<char*>("XDigit");
    else if (name[0] == 'A' && name[1] == 's')
      name = const_cast<char*>("ASCII");
  }
  const int *translated = Posix::range(name);
  if (translated == NULL)
    throw regex_error(regex_error::invalid_class, pattern, pos);
  if (*buf == '^')
  {
    int last = 0x00;
    for (const int *wc = translated; wc[1] != 0; wc += 2)
    {
      if (wc[0] > 0x00)
        ranges.insert(last, wc[0] - 1);
      last = wc[1] + 1;
    }
    if (last < 0xFF)
      ranges.insert(last, 0xFF);
  }
  else
  {
    for (const int *wc = translated; wc[1] != 0; wc += 2)
      ranges.insert(wc[0], wc[1]);
  }
  ++pos;
}

static void insert_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros);

static void merge_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros)
{
  if (pattern[pos] == '[')
  {
    ++pos;
    insert_list(pattern, len, pos, flags, mod, ranges, macros);
  }
  else if (pattern[pos] == '{' && macros != NULL)
  {
    ++pos;
    const std::string& list = expand(macros, pattern, len, pos);
    if (list.size() < 2 || list.at(0) != '[')
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
    size_t subpos = 1;
    insert_list(list.c_str(), list.size(), subpos, flags, mod, ranges, macros);
    if (subpos + 1 < list.size())
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
  else
  {
    throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
}

static void intersect_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros)
{
  ORanges<int> intersect;
  if (pattern[pos] == '[')
  {
    ++pos;
    insert_list(pattern, len, pos, flags, mod, intersect, macros);
    ranges &= intersect;
  }
  else if (pattern[pos] == '{' && macros != NULL)
  {
    ++pos;
    const std::string& list = expand(macros, pattern, len, pos);
    if (list.size() < 2 || list.at(0) != '[')
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
    size_t subpos = 1;
    insert_list(list.c_str(), list.size(), subpos, flags, mod, intersect, macros);
    ranges &= intersect;
    if (subpos + 1 < list.size())
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
  else
  {
    throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
}

static void subtract_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros)
{
  ORanges<int> subtract;
  if (pattern[pos] == '[')
  {
    ++pos;
    insert_list(pattern, len, pos, flags, mod, subtract, macros);
    ranges -= subtract;
  }
  else if (pattern[pos] == '{' && macros != NULL)
  {
    ++pos;
    const std::string& list = expand(macros, pattern, len, pos);
    if (list.size() < 2 || list.at(0) != '[')
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
    size_t subpos = 1;
    insert_list(list.c_str(), list.size(), subpos, flags, mod, subtract, macros);
    ranges -= subtract;
    if (subpos + 1 < list.size())
      throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
  else
  {
    throw regex_error(regex_error::invalid_class_range, pattern, pos);
  }
}

static void extend_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros)
{
  if ((flags & convert_flag::lex))
  {
    int c;
    while (pos + 5 < len && pattern[pos + 1] == '{' && ((c = pattern[pos + 2]) == '+' || c == '|' || c == '&' || c == '-') && pattern[pos + 3] == '}')
    {
      // lex: [a-z]{+}[A-Z] character class addition, [a-z]{-}[aeiou] subtraction, [a-z]{&}[^aeiou] intersection
      pos += 4;
      switch (c)
      {
        case '+':
        case '|':
          merge_list(pattern, len, pos, flags & ~convert_flag::lex, mod, ranges, macros);
          break;
        case '&':
          intersect_list(pattern, len, pos, flags & ~convert_flag::lex, mod, ranges, macros);
          break;
        case '-':
          subtract_list(pattern, len, pos, flags & ~convert_flag::lex, mod, ranges, macros);
          break;
      }
    }
  }
}

static void negate_list(convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges)
{
  if (is_modified(mod, 'i'))
    convert_anycase_ranges(ranges);
  if (is_modified(mod, 'u'))
  {
    ORanges<int> inverse(0x00, 0x10FFFF);
    inverse -= ORanges<int>(0xD800, 0xDFFF); // remove surrogates
    inverse -= ranges;
    ranges.swap(inverse);
  }
  else
  {
    ORanges<int> inverse(0x00, 0xFF);
    inverse -= ranges;
    ranges.swap(inverse);
  }
  if ((flags & convert_flag::notnewline))
    ranges.erase('\n');
}

static void insert_list(const char *pattern, size_t len, size_t& pos, convert_flag_type flags, const std::map<size_t,std::string>& mod, ORanges<int>& ranges, const std::map<std::string,std::string> *macros)
{
  size_t loc = pos;
  bool negate = false;
  bool range = false;
  int pc = -2;
  if (pos + 1 < len)
  {
    negate = pattern[pos] == '^';
    if (negate)
      ++pos;
  }
  while (pos + 1 < len)
  {
    int c = pattern[pos];
    if (c == '\\')
    {
      ++pos;
      c = insert_escape(pattern, len, pos, flags, mod, ranges);
      if (range)
      {
        if (c == -1 || pc > c)
          throw regex_error(regex_error::invalid_class_range, pattern, pos);
        ranges.insert(pc, c);
        range = false;
      }
      pc = c;
    }
    else if (c == '[' && pattern[pos + 1] == ':')
    {
      // POSIX character class (ASCII only)
      if (range)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      insert_posix_class(pattern, len, pos, ranges);
      pc = -1;
    }
    else if (c == '[' && (pattern[pos + 1] == '.' || pattern[pos + 1] == '='))
    {
      // POSIX collating
      if (range)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      if (pos + 4 >= len || pattern[pos + 3] != pattern[pos + 1] || pattern[pos + 4] != ']')
        throw regex_error(regex_error::invalid_collating, pattern, pos);
      ranges.insert(pattern[pos + 2]);
      pos += 4;
      pc = -1;
    }
    else if (c == '|' && pattern[pos + 1] == '|' && pos + 3 < len && (pattern[pos + 2] == '[' || (pattern[pos + 2] == '{' && macros != NULL)))
    {
      // character class union [abc||[def]]
      if (range)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      pos += 2;
      merge_list(pattern, len, pos, flags, mod, ranges, macros);
      pc = -1;
    }
    else if (c == '&' && pattern[pos + 1] == '&' && pos + 3 < len && (pattern[pos + 2] == '[' || (pattern[pos + 2] == '{' && macros != NULL)))
    {
      // character class intersection [a-z&&[^aeiou]]
      if (range)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      pos += 2;
      intersect_list(pattern, len, pos, flags, mod, ranges, macros);
      pc = -1;
    }
    else if (c == '-' && pattern[pos + 1] == '-' && pos + 3 < len && (pattern[pos + 2] == '[' || (pattern[pos + 2] == '{' && macros != NULL)))
    {
      // character class subtraction [a-z--[aeiou]]
      if (range)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      pos += 2;
      subtract_list(pattern, len, pos, flags, mod, ranges, macros);
      pc = -1;
    }
    else if (c == '-' && !range && pc != -2 && pattern[pos + 1] != ']')
    {
      // character class range [a-z]
      if (pc == -1)
        throw regex_error(regex_error::invalid_class_range, pattern, pos);
      range = true;
    }
    else
    {
      if ((c & 0xC0) == 0xC0 && is_modified(mod, 'u'))
      {
        // unicode: UTF-8 sequence
        const char *r;
        c = utf8(&pattern[pos], &r);
        pos += r - &pattern[pos] - 1;
      }
      if (range)
      {
        if (c == -1 || pc > c)
          throw regex_error(regex_error::invalid_class_range, pattern, pos);
        ranges.insert(pc, c);
        range = false;
        pc = -2;
      }
      else
      {
        ranges.insert(c);
        pc = c;
      }
    }
    ++pos;
    if (pos >= len)
      break;
    if (pattern[pos] == ']')
    {
      if (range)
        ranges.insert('-');
      break;
    }
  }
  if (pos >= len || pattern[pos] != ']')
    throw regex_error(regex_error::mismatched_brackets, pattern, loc);
  if (negate)
    negate_list(flags, mod, ranges);
  extend_list(pattern, len, pos, flags, mod, ranges, macros);
  if (ranges.empty())
    throw regex_error(regex_error::empty_class, pattern, loc);
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter escaped character conversions                             //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static void convert_escape_char(const char *pattern, size_t len, size_t& loc, size_t& pos, convert_flag_type flags, const char *signature, const std::map<size_t,std::string>& mod, const char *par, std::string& regex)
{
  int c = pattern[pos];
  if (std::strchr(regex_unescapes, c) != NULL)
  {
    // translate \x to x
    regex.append(&pattern[loc], pos - loc - 1);
    loc = pos;
  }
  else if (std::strchr(regex_escapes, c) != NULL)
  {
    // translate \x to \xXX
    int esc = hex_or_octal_escape(signature);
    regex.append(&pattern[loc], pos - loc - 1).append(latin1(c, c, esc));
    loc = pos + 1;
  }
  else if (std::strchr(regex_meta, c) == NULL)
  {
    char buf[3] = { '^', static_cast<char>(lowercase(c)), '\0' };
    bool invert = std::isupper(c) != 0;
    const char *name = buf + !invert;
    std::string translated;
    int esc = hex_or_octal_escape(signature);
    if (is_modified(mod, 'u'))
    {
      if (!supports_escape(signature, 'p'))
        translated = unicode_class(name, esc, flags, par);
    }
    else if (!supports_escape(signature, c))
    {
      translated = posix_class(name, esc);
    }
    if (!translated.empty())
    {
      regex.append(&pattern[loc], pos - loc - 1).append(translated);
      loc = pos + 1;
    }
    else if (!supports_escape(signature, c))
    {
      if (c == 'A')
      {
        if (!supports_escape(signature, '`'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \A to \`
        regex.append(&pattern[loc], pos - loc - 1).append("\\`");
        loc = pos + 1;
      }
      else if (c == 'z')
      {
        if (!supports_escape(signature, '\''))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \z to \'
        regex.append(&pattern[loc], pos - loc - 1).append("\\'");
        loc = pos + 1;
      }
      else if (c == 'Z')
      {
        if (!supports_escape(signature, 'z') || !supports_modifier(signature, '='))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \Z to (?=(\r?\n)?\z)
        regex.append(&pattern[loc], pos - loc - 1).append("(?=(\\r?\\n)?\\z)");
        loc = pos + 1;
      }
      else if (c == 'b')
      {
        if (!supports_escape(signature, 'y'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \b to \y
        regex.append(&pattern[loc], pos - loc - 1).append("\\y");
        loc = pos + 1;
      }
      else if (c == 'y')
      {
        if (!supports_escape(signature, 'b'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \y to \b
        regex.append(&pattern[loc], pos - loc - 1).append("\\b");
        loc = pos + 1;
      }
      else if (c == 'B')
      {
        if (!supports_escape(signature, 'Y'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \B to \Y
        regex.append(&pattern[loc], pos - loc - 1).append("\\y");
        loc = pos + 1;
      }
      else if (c == 'Y')
      {
        if (!supports_escape(signature, 'B'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \Y to \B
        regex.append(&pattern[loc], pos - loc - 1).append("\\b");
        loc = pos + 1;
      }
      else if (c == '<')
      {
        if (!supports_escape(signature, 'b') || !supports_escape(signature, 'w') || !supports_modifier(signature, '='))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \< to \b(?=\w)
        regex.append(&pattern[loc], pos - loc - 1).append("\\b(?=\\w)");
        loc = pos + 1;
      }
      else if (c == '>')
      {
        if (!supports_escape(signature, 'b') || !supports_escape(signature, 'w') || !supports_modifier(signature, '<'))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        // translate \> to \b(?<=\w)
        regex.append(&pattern[loc], pos - loc - 1).append("\\b(?<=\\w)");
        loc = pos + 1;
      }
      else
      {
        if (std::strchr(regex_anchors, c))
          throw regex_error(regex_error::invalid_anchor, pattern, pos);
        const char *s = std::strchr(regex_abtnvfr, c);
        if (s == NULL)
          throw regex_error(regex_error::invalid_escape, pattern, pos);
        int wc = static_cast<int>(s - regex_abtnvfr + '\a');
        regex.append(&pattern[loc], pos - loc - 1).append(latin1(wc, wc, esc));
        loc = pos + 1;
      }
    }
    else if ((c == 'g' || c == 'k') && pos + 2 < len && pattern[pos + 1] == '{')
    {
      while (pos + 1 < len && pattern[pos + 1] != '\0' && pattern[++pos] != '}')
        continue;
      if (pos >= len)
        throw regex_error(regex_error::mismatched_braces, pattern, pos);
    }
  }
}

static void convert_escape(const char *pattern, size_t len, size_t& loc, size_t& pos, convert_flag_type flags, const char *signature, const std::map<size_t,std::string>& mod, const char *par, std::string& regex)
{
  int c = pattern[pos];
  if (c == '\n' || c == '\r')
  {
    // remove line continuation from \ \n (\ \r\n) to next line, skipping indent
    regex.append(&pattern[loc], pos - loc - 1);
    if (++pos < len && pattern[pos] == '\n')
      ++pos;
    while (pos < len && ((c = pattern[pos]) == ' ' || c == '\t'))
      ++pos;
    loc = pos;
  }
  else if (c == 'c')
  {
    ++pos;
    if (pos >= len)
      throw regex_error(regex_error::invalid_escape, pattern, pos - 1);
    c = pattern[pos];
    if (c < 0x21 || c >= 0x7F)
      throw regex_error(regex_error::invalid_escape, pattern, pos);
    c &= 0x1F;
    if (!supports_escape(signature, 'c'))
    {
      // translate \cX to \xXX
      int esc = hex_or_octal_escape(signature);
      regex.append(&pattern[loc], pos - loc - 2).append(latin1(c, c, esc));
      loc = pos + 1;
    }
  }
  else if (c == 'e')
  {
    if (!supports_escape(signature, 'e'))
    {
      // translate \e to \x1b
      regex.append(&pattern[loc], pos - loc - 1).append("\\x1b");
      loc = pos + 1;
    }
  }
  else if (c == 'N')
  {
    if (is_modified(mod, 'u'))
    {
      if (!supports_escape(signature, 'p'))
      {
        regex.append(&pattern[loc], pos - loc - 1).append(par).append("[^\\n][\\x80-\\xbf]*)");
        loc = pos + 1;
      }
    }
    else if (!supports_escape(signature, c))
    {
      regex.append(&pattern[loc], pos - loc - 1).append("[^\n]");
      loc = pos + 1;
    }
  }
  else if (c >= '0' && c <= '7' && pattern[pos + 1] >= '0' && pattern[pos + 1] <= '7')
  {
    size_t k = pos;
    size_t n = k + 3 + (pattern[k] == '0');
    int wc = 0;
    while (k < n && k < len && (c = pattern[k]) >= '0' && c <= '7')
    {
      wc = 8 * wc + c - '0';
      ++k;
    }
    if (wc > 0xFF)
      throw regex_error(regex_error::invalid_escape, pattern, pos);
    if (std::isalpha(wc) && is_modified(mod, 'i'))
    {
      // anycase: translate A to [Aa]
      regex.append(&pattern[loc], pos - loc - 1).push_back('[');
      regex.push_back(wc);
      regex.push_back(wc ^ 0x20);
      regex.push_back(']');
    }
    else
    {
      int esc = hex_or_octal_escape(signature);
      if (is_modified(mod, 'u'))
        regex.append(&pattern[loc], pos - loc - 1).append(utf8(wc, wc, esc, par));
      else
        regex.append(&pattern[loc], pos - loc - 1).append(latin1(wc, wc, esc));
    }
    pos = k - 1;
    loc = pos + 1;
  }
  else if (c == 'o' || c == 'u' || c == 'x')
  {
    size_t k = pos;
    int wc = (c == 'o' ? convert_oct(pattern, len, k) : convert_hex(pattern, len, k, flags));
    if (wc >= 0)
    {
      if (c == 'u' && wc >= 0xD800 && wc < 0xE000)
      {
        // translate surrogate pair \uDXXX\uDYYY
        if (k + 2 >= len || pattern[k + 1] != '\\' || pattern[k + 2] != 'u')
          throw regex_error(regex_error::invalid_escape, pattern, k);
        k += 2;
        int c2 = convert_hex(pattern, len, k, flags);
        if (c2 < 0 || (c2 & 0xFC00) != 0xDC00)
          throw regex_error(regex_error::invalid_escape, pattern, k - 2);
        wc = 0x010000 - 0xDC00 + ((wc - 0xD800) << 10) + c2;
      }
      if (supports_escape(signature, 'p'))
      {
        // translate \u{X}, \uXXXX (convert_flag::u4) and \x{X} to \xXX and \x{X}
        char buf[16];
        regex.append(&pattern[loc], pos - loc - 1).append(hex_escape(buf, wc));
      }
      else
      {
        if (wc <= 0xFF)
        {
          // translate \u{X}, \u00XX (convert_flag::u4) and \x{X} to \xXX
          if (std::isalpha(wc) && is_modified(mod, 'i'))
          {
            // anycase: translate A to [Aa]
            regex.append(&pattern[loc], pos - loc - 1).push_back('[');
            regex.push_back(wc);
            regex.push_back(wc ^ 0x20);
            regex.push_back(']');
          }
          else
          {
            int esc = hex_or_octal_escape(signature);
            if (is_modified(mod, 'u'))
              regex.append(&pattern[loc], pos - loc - 1).append(utf8(wc, wc, esc, par));
            else
              regex.append(&pattern[loc], pos - loc - 1).append(latin1(wc, wc, esc));
          }
        }
        else if (is_modified(mod, 'u'))
        {
          // translate \u{X}, \uXXXX, \uDXXX\uDYYY, and \x{X} to UTF-8 pattern
          char buf[8];
          buf[utf8(wc, buf)] = '\0';
          regex.append(&pattern[loc], pos - loc - 1).append(par).append(buf).push_back(')');
        }
        else
        {
          throw regex_error(regex_error::invalid_escape, pattern, pos);
        }
      }
      pos = k;
      loc = pos + 1;
    }
    else
    {
      convert_escape_char(pattern, len, loc, pos, flags, signature, mod, par, regex);
    }
  }
  else if (c == 'p' || c == 'P')
  {
    size_t k = ++pos;
    if (pos >= len)
      throw regex_error(regex_error::invalid_class, pattern, pos);
    // get name X of \pX, \PX, \p{X}, and \P{X}
    std::string name;
    if (pattern[pos] == '{')
    {
      size_t j = pos + 1;
      if (c == 'P')
        name.push_back('^');
      k = j;
      while (k < len && pattern[k] != '}')
        ++k;
      if (k >= len)
        throw regex_error(regex_error::mismatched_braces, pattern, pos);
      name.append(pattern, j, k - j);
    }
    else
    {
      if (c == 'P')
        name.push_back('^');
      name.push_back(pattern[pos]);
    }
    std::string translated;
    int esc = hex_or_octal_escape(signature);
    if (supports_escape(signature, 'p'))
    {
      translated = posix_class(name.c_str(), esc);
      if (!translated.empty())
      {
        regex.append(&pattern[loc], pos - loc - 2).append("[").append(translated.substr(1, translated.size() - 2)).append("]");
        loc = k + 1;
      }
    }
    else
    {
      if (is_modified(mod, 'u'))
      {
        translated = unicode_class(name.c_str(), esc, flags, par);
        if (translated.empty())
        {
          translated = posix_class(name.c_str(), esc);
          if (translated.empty())
            throw regex_error(regex_error::invalid_class, pattern, pos);
        }
      }
      else
      {
        translated = posix_class(name.c_str(), esc);
        if (translated.empty() && !supports_escape(signature, c))
          throw regex_error(regex_error::invalid_class, pattern, pos);
      }
      if (!translated.empty())
      {
        regex.append(&pattern[loc], pos - loc - 2).append(translated);
        loc = k + 1;
      }
    }
    pos = k;
  }
  else if (c == 's' && (flags & convert_flag::notnewline))
  {
    // \s is the same as \p{Space} but without newline \n
    if (supports_escape(signature, 'p'))
    {
      if (is_modified(mod, 'u'))
        regex.append(&pattern[loc], pos - loc - 1).append("[\\t\\x0b-\\r\\x85\\p{Z}]");
      else
        regex.append(&pattern[loc], pos - loc - 1).append("[\\h\\x0b-\\r\\x85\\xa0]");
      loc = pos + 1;
    }
    else
    {
      ORanges<int> ranges;
      insert_escape_class(pattern, pos, mod, ranges);
      ranges.erase('\n');
      regex.append(&pattern[loc], pos - loc - 1);
      regex.append(convert_ranges(pattern, pos, ranges, mod, flags, signature, par));
      loc = pos + 1;
    }
  }
  else
  {
    convert_escape_char(pattern, len, loc, pos, flags, signature, mod, par, regex);
  }
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  Regex converter                                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

std::string convert(const char *pattern, const char *signature, convert_flag_type flags, const std::map<std::string,std::string> *macros)
{
  std::string regex;
  bool anc = false;
  bool beg = true;
  bool bre = false;
  size_t pos = 0;
  size_t loc = 0;
  size_t lev = 1;
  size_t lap = 0;
  size_t len = std::strlen(pattern);
  bool can = std::strchr(signature, ':') != NULL;
  const char *par = can ? "(?:" : "(";
  std::map<size_t,std::string> mod;
  if ((flags & convert_flag::anycase))
    enable_modifier('i', pattern, 0, mod, 0);
  if ((flags & convert_flag::dotall))
    enable_modifier('s', pattern, 0, mod, 0);
  if ((flags & convert_flag::multiline))
    if (!supports_modifier(signature, 'm'))
      throw regex_error(regex_error::invalid_modifier, pattern, pos);
  if ((flags & convert_flag::unicode))
    enable_modifier('u', pattern, 0, mod, 0);
  if ((flags & convert_flag::freespace))
    enable_modifier('x', pattern, 0, mod, 0);
  if (len > 2 && pattern[0] == '(' && pattern[1] == '?')
  {
    // directive (?...) mode modifier
    std::string mods, unmods;
    if ((flags & convert_flag::multiline))
      mods.push_back('m');
    size_t k = 2;
    bool invert = false;
    while (k < len && (pattern[k] == '-' || std::isalpha(pattern[k])))
    {
      if (pattern[k] == '-')
      {
        invert = true;
      }
      else if (!invert)
      {
        if (supports_modifier(signature, pattern[k]))
        {
          mods.push_back(pattern[k]);
        }
        else if (pattern[k] == 'm')
        {
          throw regex_error(regex_error::invalid_modifier, pattern, pos);
        }
        else
        {
          enable_modifier(pattern[k], pattern, k, mod, lev);
        }
      }
      else
      {
        if (supports_modifier(signature, pattern[k]))
          unmods.push_back(pattern[k]);
        else if (pattern[k] == 'm')
          throw regex_error(regex_error::invalid_modifier, pattern, pos);
        else
          disable_modifier(pattern[k], pattern, k, mod, lev);
      }
      ++k;
    }
    if (k < len && pattern[k] == ')')
    {
      // preserve (?imsx) at start of regex
      if (can && (!mods.empty() || !unmods.empty()))
      {
        regex.append("(?");
        if (!mods.empty())
          regex.append(mods);
        if (!unmods.empty())
          regex.append("-").append(unmods);
        regex.push_back(')');
      }
      pos = k + 1;
      loc = pos;
    }
    else
    {
      mod[lev].clear();
    }
  }
  else if ((flags & convert_flag::multiline))
  {
    regex.assign("(?m)");
  }
  if ((flags & convert_flag::recap))
  {
    // recap: translate x|y to (x)|(y)
    regex.append(&pattern[loc], pos - loc).push_back('(');
    loc = pos;
  }
  while (pos < len)
  {
    int c = pattern[pos];
    switch (c)
    {
      case '\\':
        if (pos + 1 >= len)
          throw regex_error(regex_error::invalid_escape, pattern, pos + 1);
        anc = false;
        c = pattern[++pos];
        if (c == 'Q')
        {
          if (!supports_escape(signature, 'Q'))
          {
            // \Q is not a supported escape, translate by grouping and escaping meta chars up to the closing \E
            regex.append(&pattern[loc], pos - loc - 1);
            size_t k = loc = ++pos;
            while (pos + 1 < len && (pattern[pos] != '\\' || pattern[pos + 1] != 'E'))
            {
              if (std::strchr(regex_meta, pattern[pos]) != NULL)
              {
                regex.append(&pattern[loc], pos - loc).push_back('\\');
                loc = pos;
              }
              ++pos;
            }
            if (pos + 1 >= len || pattern[pos] != '\\')
              throw regex_error(regex_error::mismatched_quotation, pattern, k);
            if (k < pos)
              beg = false;
            regex.append(&pattern[loc], pos - loc);
            loc = pos + 2;
          }
          else
          {
            // retain regex up to and including the closing \E
            size_t k = ++pos;
            while (pos + 1 < len && (pattern[pos] != '\\' || pattern[pos + 1] != 'E'))
              ++pos;
            if (pos + 1 >= len || pattern[pos] != '\\')
              throw regex_error(regex_error::mismatched_quotation, pattern, k);
            if (k < pos)
              beg = false;
          }
          ++pos;
        }
        else if (c == 'R')
        {
          if (!is_modified(mod, 'u') || !supports_escape(signature, 'R'))
          {
            // translate \R to match Unicode line break U+000D U+000A | [U+000A - U+000D] | U+0085 | U+2028 | U+2029
            regex.append(&pattern[loc], pos - loc - 1).append(par).append("\\r\\n|[\\x0a-\\x0d]|\\xc2\\x85|\\xe2\\x80[\\xa8\\xa9]").push_back(')');
            loc = pos + 1;
          }
          beg = false;
        }
        else if (c == 'X')
        {
          if (!is_modified(mod, 'u') || !supports_escape(signature, 'X'))
          {
#ifndef WITH_UTF8_UNRESTRICTED
            // translate \X to match any ISO-8859-1 and valid UTF-8
            regex.append(&pattern[loc], pos - loc - 1).append(par).append("[\\x00-\\xff]|[\\xc2-\\xdf][\\x80-\\xbf]|\\xe0[\\xa0-\\xbf][\\x80-\\xbf]|[\\xe1-\\xec][\\x80-\\xbf][\\x80-\\xbf]|\\xed[\\x80-\\x9f][\\x80-\\xbf]|[\\xee\\xef][\\x80-\\xbf][\\x80-\\xbf]|\\xf0[\\x90-\\xbf][\\x80-\\xbf][\\x80-\\xbf]|[\\xf1-\\xf3][\\x80-\\xbf][\\x80-\\xbf][\\x80-\\xbf]|\\xf4[\\x80-\\x8f][\\x80-\\xbf][\\x80-\\xbf]").push_back(')');
#else
            // translate \X to match any ISO-8859-1 and UTF-8 encodings, including malformed UTF-8 with overruns
            regex.append(&pattern[loc], pos - loc - 1).append(par).append("[\\x00-\\xff]|[\\xc0-\\xff][\\x80-\\xbf]+").push_back(')');
#endif
            loc = pos + 1;
          }
          beg = false;
        }
        else if ((flags & convert_flag::basic) && (c == '?' || c == '+' || c == '|' || c == '(' || c == ')' || c == '{' || c == '}'))
        {
          regex.append(&pattern[loc], pos - loc - 1);
          loc = pos;
          bre = true;
          continue;
        }
        else
        {
          convert_escape(pattern, len, loc, pos, flags, signature, mod, par, regex);
          anc = (std::strchr(regex_anchors, c) != NULL);
          if (!anc || c == 'Z' || c == 'z')
            beg = false;
        }
        break;
      case '/':
        if ((flags & convert_flag::lex))
        {
          if (beg)
            throw regex_error(regex_error::empty_expression, pattern, pos);
          // lex: translate lookahead (trailing context) / to (?=
          if (!supports_modifier(signature, '='))
            throw regex_error(regex_error::invalid_modifier, pattern, pos);
          regex.append(&pattern[loc], pos - loc).append("(?=");
          lap = lev;
          loc = pos + 1;
          beg = true;
        }
        else
        {
          beg = false;
        }
        anc = false;
        break;
      case '(':
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate ( to \(
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          anc = false;
          beg = false;
        }
        else
        {
          ++lev;
          if (pos + 1 < len && pattern[pos + 1] == '?')
          {
            ++pos;
            if (pos + 1 < len)
            {
              ++pos;
              if (pattern[pos] == '#')
              {
                size_t k = pos++;
                while (pos < len && pattern[pos] != ')')
                  ++pos;
                if (pos >= len || pattern[pos] != ')')
                  throw regex_error(regex_error::mismatched_parens, pattern, k);
                if (!supports_modifier(signature, '#'))
                {
                  // no # modifier: remove (?#...)
                  regex.append(&pattern[loc], k - loc - 2);
                  loc = pos + 1;
                }
                --lev;
              }
              else if (pattern[pos] == '(')
              {
                if (!supports_modifier(signature, '('))
                  throw regex_error(regex_error::invalid_syntax, pattern, pos);
                --pos;
              }
              else
              {
                std::string mods, unmods;
                size_t k = pos;
                bool invert = false;
                while (k < len && (pattern[k] == '-' || std::isalnum(pattern[k])))
                {
                  if (pattern[k] == '-')
                  {
                    invert = true;
                  }
                  else if (!invert)
                  {
                    if (supports_modifier(signature, pattern[k]))
                      mods.push_back(pattern[k]);
                    else
                      enable_modifier(pattern[k], pattern, k, mod, lev);
                  }
                  else
                  {
                    if (supports_modifier(signature, pattern[k]))
                      unmods.push_back(pattern[k]);
                    else
                      disable_modifier(pattern[k], pattern, k, mod, lev);
                  }
                  ++k;
                }
                if (k >= len)
                  throw regex_error(regex_error::mismatched_parens, pattern, pos);
                if (pattern[k] == ':' || pattern[k] == ')')
                {
                  regex.append(&pattern[loc], pos - loc - 2);
                  if (pattern[k] == ')')
                  {
                    // (?imsx)...
                    if (can && (!mods.empty() || !unmods.empty()))
                    {
                      regex.append("(?");
                      if (!mods.empty())
                        regex.append(mods);
                      if (!unmods.empty())
                        regex.append("-").append(unmods);
                      regex.push_back(')');
                    }
                    mod[lev - 1] = mod[lev] + mod[lev - 1];
                    mod[lev].clear();
                    --lev;
                  }
                  else
                  {
                    // (?imsx:...)
                    if (can)
                    {
                      regex.append("(?");
                      if (!mods.empty())
                        regex.append(mods);
                      if (!unmods.empty())
                        regex.append("-").append(unmods);
                      regex.push_back(':');
                    }
                  }
                  pos = k;
                  loc = pos + 1;
                }
                else if (supports_modifier(signature, pattern[pos]))
                {
                  // (?=...), (?!...), (?<...) etc
                  beg = true;
                }
                else
                {
                  throw regex_error(regex_error::invalid_syntax, pattern, pos);
                }
              }
            }
          }
          else if (pos + 1 < len && pattern[pos + 1] == '*' && supports_modifier(signature, '*'))
          {
            pos += 2;
            while (pos < len && pattern[pos] != ')')
              ++pos;
            if (pos < len)
              --lev;
          }
          else
          {
            beg = true;
            if ((flags & convert_flag::recap) || (flags & convert_flag::lex))
            {
              // recap: translate ( to (?:
              regex.append(&pattern[loc], pos - loc).append("(?:");
              loc = pos + 1;
            }
          }
        }
        break;
      case ')':
        anc = false;
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate ) to \)
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          beg = false;
        }
        else
        {
          if (lev == 1)
            throw regex_error(regex_error::mismatched_parens, pattern, pos);
          if (beg)
            throw regex_error(regex_error::empty_expression, pattern, pos);
          if (lap == lev)
          {
            // lex lookahead: translate ) to ))
            regex.append(&pattern[loc], pos - loc).push_back(')');
            loc = pos;
            lap = 0;
          }
          // terminate (?isx:...)
          mod[lev].clear();
          --lev;
        }
        break;
      case '|':
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate | to \|
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          anc = false;
          beg = false;
        }
        else
        {
          if (beg)
            throw regex_error(regex_error::empty_expression, pattern, pos);
          if (lap == lev)
          {
            // lex lookahead: translate | to )|
            regex.append(&pattern[loc], pos - loc).push_back(')');
            loc = pos;
            lap = 0;
          }
          else if ((flags & convert_flag::recap) && lev == 1)
          {
            // recap: translate x|y to (x)|(y)
            regex.append(&pattern[loc], pos - loc).append(")|(");
            loc = pos + 1;
          }
          beg = true;
        }
        break;
      case '[':
        if (strncmp(&pattern[pos], "[[:<:]]", 7) == 0)
        {
          // translate [[:<:]] to \<
          if (!supports_escape(signature, '<'))
          {
            if (!supports_escape(signature, 'b') || !supports_escape(signature, 'w') || !supports_modifier(signature, '='))
              throw regex_error(regex_error::invalid_anchor, pattern, pos);
            // translate \< to \b(?=\w)
            regex.append(&pattern[loc], pos - loc).append("\\b(?=\\w)");
          }
          else
          {
            regex.append(&pattern[loc], pos - loc).append("\\<");
          }
          pos += 6;
          loc = pos + 1;
          anc = true;
          beg = false;
        }
        else if (strncmp(&pattern[pos], "[[:>:]]", 7) == 0)
        {
          // translate [[:>:]] to \>
          if (!supports_escape(signature, '>'))
          {
            if (!supports_escape(signature, 'b') || !supports_escape(signature, 'w') || !supports_modifier(signature, '<'))
              throw regex_error(regex_error::invalid_anchor, pattern, pos);
            // translate \< to \b(?<=\w)
            regex.append(&pattern[loc], pos - loc - 1).append("\\b(?<=\\w)");
          }
          else
          {
            regex.append(&pattern[loc], pos - loc).append("\\<");
          }
          pos += 6;
          loc = pos + 1;
          anc = true;
          beg = false;
        }
        else if (supports_escape(signature, 'p'))
        {
          ++pos;
          expand_list(pattern, len, loc, pos, flags, mod, signature, par, macros, regex);
          anc = false;
          beg = false;
        }
        else
        {
          ORanges<int> ranges;
          regex.append(&pattern[loc], pos - loc);
          ++pos;
          insert_list(pattern, len, pos, flags, mod, ranges, macros);
          regex.append(convert_ranges(pattern, pos, ranges, mod, flags, signature, par));
          loc = pos + 1;
          anc = false;
          beg = false;
        }
        break;
      case '"':
        if ((flags & convert_flag::lex))
        {
          // lex: translate "..."
          if (!supports_escape(signature, 'Q'))
          {
            // \Q is not a supported escape, translate "..." by grouping and escaping meta chars while removing \ from \"
            regex.append(&pattern[loc], pos - loc).append(par);
            size_t k = loc = ++pos;
            while (pos < len && pattern[pos] != '"')
            {
              if (pattern[pos] == '\\' && pos + 1 < len && pattern[pos + 1] == '"')
              {
                regex.append(&pattern[loc], pos - loc);
                loc = ++pos;
              }
              else if (std::strchr(regex_meta, pattern[pos]) != NULL)
              {
                regex.append(&pattern[loc], pos - loc).push_back('\\');
                loc = pos;
              }
              ++pos;
            }
            regex.append(&pattern[loc], pos - loc).push_back(')');
            if (k < pos)
              beg = false;
          }
          else
          {
            // translate "..." to \Q...\E while removing \ from \" and translating \E to \E\\E\Q (or perhaps \\EE\Q)
            regex.append(&pattern[loc], pos - loc).append(par).append("\\Q");
            size_t k = loc = ++pos;
            while (pos < len && pattern[pos] != '"')
            {
              if (pattern[pos] == '\\' && pos + 1 < len)
              {
                if (pattern[pos + 1] == '"')
                {
                  regex.append(&pattern[loc], pos - loc);
                  loc = ++pos;
                }
                else if (pattern[pos + 1] == 'E')
                {
                  regex.append(&pattern[loc], pos - loc).append("\\E\\\\E\\Q");
                  loc = ++pos + 1;
                }
              }
              ++pos;
            }
            regex.append(&pattern[loc], pos - loc).append("\\E").push_back(')');
            if (k < pos)
              beg = false;
          }
          if (pos >= len || pattern[pos] != '"')
            throw regex_error(regex_error::mismatched_quotation, pattern, loc);
          loc = pos + 1;
        }
        else
        {
          beg = false;
        }
        anc = false;
        break;
      case '{':
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate { to \{
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          anc = false;
          beg = false;
        }
        else
        {
          if (macros != NULL && pos + 1 < len && (std::isalpha(pattern[pos + 1]) || pattern[pos + 1] == '_' || pattern[pos + 1] == '$' || (pattern[pos + 1] & 0x80) == 0x80))
          {
            // if macros are provided: lookup {name} and expand without converting
            regex.append(&pattern[loc], pos - loc);
            ++pos;
            loc = pos;
            const std::string& subregex = expand(macros, pattern, len, pos);
            int c;
            if ((flags & convert_flag::lex) && pos + 5 < len && pattern[pos + 1] == '{' && ((c = pattern[pos + 2]) == '+' || c == '|' || c == '&' || c == '-') && pattern[pos + 3] == '}')
            {
              size_t subpos = 0;
              ORanges<int> ranges;
              merge_list(subregex.c_str(), subregex.size(), subpos, flags, mod, ranges, macros);
              if (subpos + 1 < subregex.size())
                throw regex_error(regex_error::invalid_class_range, pattern, loc);
              extend_list(pattern, len, pos, flags, mod, ranges, macros);
              regex.append(convert_ranges(pattern, pos, ranges, mod, flags, signature, par));
            }
            else
            {
              regex.append(par).append(subregex).push_back(')');
            }
            loc = pos + (pos < len && pattern[pos] == '\\') + 1;
            anc = false;
            beg = false;
          }
          else
          {
            if (anc)
              throw regex_error(regex_error::invalid_syntax, pattern, pos);
            if (beg)
              throw regex_error(regex_error::empty_expression, pattern, pos);
            ++pos;
            if (pos >= len || !std::isdigit(pattern[pos]))
              throw regex_error(regex_error::invalid_repeat, pattern, pos);
            char *s;
            size_t n = static_cast<size_t>(std::strtoul(&pattern[pos], &s, 10));
            pos = s - pattern;
            if (pos + 1 < len && pattern[pos] == ',')
            {
              ++pos;
              if (pattern[pos] != '\\' && pattern[pos] != '}')
              {
                size_t m = static_cast<size_t>(std::strtoul(&pattern[pos], &s, 10));
                if (m < n)
                  throw regex_error(regex_error::invalid_repeat, pattern, pos);
                pos = s - pattern;
              }
            }
            if ((flags & convert_flag::basic) && pattern[pos] == '\\')
            {
              // BRE: translate \} to }
              if (pos + 1 < len)
              {
                regex.append(&pattern[loc], pos - loc);
                loc = ++pos;
              }
            }
            if (pattern[pos] != '}')
            {
              if (pos + 1 < len)
                throw regex_error(regex_error::invalid_repeat, pattern, pos);
              else
                throw regex_error(regex_error::mismatched_braces, pattern, pos);
            }
            if (pos + 1 < len && (pattern[pos + 1] == '?' || pattern[pos + 1] == '+') && !supports_escape(signature, pattern[pos + 1]))
              throw regex_error(regex_error::invalid_quantifier, pattern, pos + 1);
          }
        }
        break;
      case '}':
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate } to \}
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          anc = false;
          beg = false;
        }
        else
        {
          throw regex_error(regex_error::mismatched_braces, pattern, pos);
        }
        break;
      case '#':
        if ((flags & convert_flag::lex) && (flags & convert_flag::freespace))
        {
          // lex freespace: translate # to \#
          regex.append(&pattern[loc], pos - loc).append("\\#");
          loc = pos + 1;
          beg = false;
        }
        else if (is_modified(mod, 'x'))
        {
          // x modifier: remove #...\n
          regex.append(&pattern[loc], pos - loc);
          while (pos + 1 < len && pattern[++pos] != '\n')
            continue;
          loc = pos + 1;
        }
        else
        {
          anc = false;
          beg = false;
        }
        break;
      case '.':
        if (is_modified(mod, 'u') && (pos + 1 >= len || (pattern[pos + 1] != '*' && pattern[pos + 1] != '+')))
        {
          // unicode: translate . to match any UTF-8 so . matches anything (also beyond U+10FFFF)
          if (is_modified(mod, 's'))
          {
            regex.append(&pattern[loc], pos - loc).append(par).append("[\\x00-\\xff][\\x80-\\xbf]*)");
            loc = pos + 1;
          }
          else if (!supports_escape(signature, 'p'))
          {
            // \p is not supported: this indicates that . is non-Unicode
            if (supports_modifier(signature, 's') || supports_escape(signature, '.'))
              regex.append(&pattern[loc], pos - loc).append(par).append(".[\\x80-\\xbf]*)");
            else
              regex.append(&pattern[loc], pos - loc).append(par).append("[^\\n][\\x80-\\xbf]*)");
            loc = pos + 1;
          }
        }
        else if (is_modified(mod, 's'))
        {
          // dotall: translate . to [\x00-\xff]
          regex.append(&pattern[loc], pos - loc).append("[\\x00-\\xff]");
          loc = pos + 1;
        }
        else if (!supports_modifier(signature, 's') && !supports_escape(signature, '.'))
        {
          // not dotall and . does matches all: translate . to [^\n]
          regex.append(&pattern[loc], pos - loc).append("[^\\n]");
          loc = pos + 1;
        }
        anc = false;
        beg = false;
        break;
      case '?':
      case '+':
        if ((flags & convert_flag::basic) && !bre)
        {
          // BRE: translate ? and + to \? and \+
          regex.append(&pattern[loc], pos - loc).push_back('\\');
          loc = pos;
          anc = false;
          beg = false;
          break;
        }
        // fall through
      case '*':
        if (anc)
          throw regex_error(regex_error::invalid_syntax, pattern, pos);
        if (beg)
          throw regex_error(regex_error::empty_expression, pattern, pos);
        if (pos + 1 < len && !(flags & convert_flag::basic) && (pattern[pos + 1] == '?' || pattern[pos + 1] == '+') && !supports_escape(signature, pattern[pos + 1]))
          throw regex_error(regex_error::invalid_quantifier, pattern, pos + 1);
        break;
      case '\t':
      case '\n':
      case '\r':
      case ' ':
        if (is_modified(mod, 'x'))
        {
          regex.append(&pattern[loc], pos - loc);
          loc = pos + 1;
        }
        else
        {
          anc = false;
          beg = false;
        }
        break;
      case '^':
        anc = true;
        break;
      case '$':
        if (beg && (flags & convert_flag::lex))
          throw regex_error(regex_error::empty_expression, pattern, pos);
        anc = true;
        beg = false;
        break;
      default:
        if (std::isalpha(pattern[pos]))
        {
          if (is_modified(mod, 'i'))
          {
            // anycase: translate A to [Aa]
            regex.append(&pattern[loc], pos - loc).push_back('[');
            regex.push_back(c);
            regex.push_back(c ^ 0x20);
            regex.push_back(']');
            loc = pos + 1;
          }
        }
        else if ((c & 0xC0) == 0xC0 && is_modified(mod, 'u') && !supports_escape(signature, 'p'))
        {
          // unicode: group UTF-8 sequence
          regex.append(&pattern[loc], pos - loc);
          loc = pos;
          while (pos + 1 < len && ((c = pattern[++pos]) & 0xC0) == 0x80)
            continue;
          if (pos < len &&
              (pattern[pos] == '*' ||
               ((flags & convert_flag::basic) && pos + 1 < len ?
                (pattern[pos] == '\\' && (pattern[pos + 1] == '?' || pattern[pos + 1] == '+' || pattern[pos + 1] == '{')) :
                (pattern[pos] == '?' || pattern[pos] == '+' || pattern[pos] == '{'))))
          {
            regex.append(par).append(&pattern[loc], pos - loc).push_back(')');
            loc = pos;
          }
          if (pos > loc)
            --pos;
        }
        anc = false;
        beg = false;
        break;
    }
    bre = false;
    ++pos;
  }
  if (lev > 1)
    throw regex_error(regex_error::mismatched_parens, pattern, pos);
  if (beg && (flags & convert_flag::lex))
    throw regex_error(regex_error::empty_expression, pattern, pos);
  regex.append(&pattern[loc], pos - loc);
  if (lap > 0)
    regex.push_back(')');
  if ((flags & convert_flag::recap))
    regex.push_back(')');
  return regex;
}

} // namespace reflex
