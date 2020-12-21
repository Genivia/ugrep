/******************************************************************************\
* Copyright (c) 2019, Robert van Engelen, Genivia Inc. All rights reserved.    *
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
@file      output.cpp
@brief     Output management
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "output.hpp"

// dump matching data in hex
void Output::Dump::hex(short mode, size_t byte_offset, const char *data, size_t size)
{
  offset = byte_offset;
  while (size > 0)
  {
    bytes[offset++ % flag_hex_columns] = (mode << 8) | *reinterpret_cast<const unsigned char*>(data++);
    if (offset % flag_hex_columns == 0)
      line();
    --size;
  }
}

// dump one line of hex
void Output::Dump::line()
{
  out.str(color_bn);
  out.hex((offset - 1) - (offset - 1) % flag_hex_columns, 8);
  out.str(color_off);
  out.chr(' ');

  short last_hex_color = HEX_MAX;

  for (size_t i = 0; i < flag_hex_columns; ++i)
  {
    if (bytes[i] < 0)
    {
      if (last_hex_color != -1)
      {
        last_hex_color = -1;
        out.str(color_off);
        out.str(color_cx);
      }
      if (flag_hex_hbr || (i == 0 && flag_hex_cbr))
        out.chr(' ');
      out.str("--");
      if (flag_hex_cbr && (i & 7) == 7)
        out.chr(' ');
    }
    else
    {
      short byte = bytes[i];
      if ((byte >> 8) != last_hex_color)
      {
        out.str(last_hex_color == HEX_MATCH || last_hex_color == HEX_CONTEXT_MATCH ? match_off : color_off);
        last_hex_color = byte >> 8;
        out.str(color_hex[last_hex_color]);
      }
      if (flag_hex_hbr || (i == 0 && flag_hex_cbr))
        out.chr(' ');
      out.hex(byte & 0xff, 2);
      if (flag_hex_cbr && (i & 7) == 7)
        out.chr(' ');
    }
  }

  out.str(color_off);

  if (flag_hex_chr)
  {
    out.str(color_se);
    if (flag_hex_hbr)
      out.chr(' ');
    out.chr('|');

    last_hex_color = HEX_MAX;
    bool inverted = false;

    for (size_t i = 0; i < flag_hex_columns; ++i)
    {
      if (bytes[i] < 0)
      {
        if (last_hex_color != -1)
        {
          last_hex_color = -1;
          out.str(color_off);
          out.str(color_cx);
        }
        out.chr('-');
      }
      else
      {
        short byte = bytes[i];
        if ((byte >> 8) != last_hex_color)
        {
          out.str(last_hex_color == HEX_MATCH || last_hex_color == HEX_CONTEXT_MATCH ? match_off : color_off);
          last_hex_color = byte >> 8;
          out.str(color_hex[last_hex_color]);
        }
        byte &= 0xff;
        if (flag_apply_color != NULL)
        {
          if (byte < 0x20)
          {
            out.str("\033[7m");
            out.chr('@' + byte);
            inverted = true;
          }
          else if (byte == 0x7f)
          {
            out.str("\033[7m~");
            inverted = true;
          }
          else if (byte > 0x7f)
          {
            out.str("\033[7m.");
            inverted = true;
          }
          else if (inverted)
          {
            out.str(color_off);
            out.str(color_hex[last_hex_color]);
            out.chr(byte);
            inverted = false;
          }
          else
          {
            out.chr(byte);
          }
        }
        else if (byte < 0x20 || byte >= 0x7f)
        {
          out.chr('.');
        }
        else
        {
          out.chr(byte);
        }
      }
    }

    out.str(color_off);
    out.str(color_se);
    out.chr('|');
    out.str(color_off);
  }

  out.nl();

  for (size_t i = 0; i < MAX_HEX_COLUMNS; ++i)
    bytes[i] = -1;
}

// output the header part of the match, preceding the matched line
void Output::header(const char *& pathname, const std::string& partname, size_t lineno, reflex::AbstractMatcher *matcher, size_t byte_offset, const char *separator, bool newline)
{
  // if hex dump line is incomplete and a header is output, then complete the hex dump first
  if (dump.incomplete() &&
      ((flag_with_filename && pathname != NULL) ||
       (!flag_no_filename && !partname.empty()) ||
       flag_line_number ||
       flag_column_number ||
       flag_byte_offset))
    dump.done();

  bool sep = false; // when a separator is needed
  bool nul = false; // -Q: mark pathname with three NUL bytes unless -a

  if (flag_with_filename && pathname != NULL)
  { 
    nul = flag_query > 0 && !flag_text;

    if (nul)
      chr('\0');

    str(color_fn);

    if (nul)
      chr('\0');

    str(pathname);

    if (nul)
      chr('\0');

    str(color_off);

    if (flag_null)
      chr('\0');

    if (flag_heading)
    {
      str(color_fn);
      str(color_del);
      str(color_off);
      chr('\n');
      pathname = NULL;
    }
    else
    {
      sep = !flag_null;
    }
  }

  if (!flag_no_filename && !partname.empty())
  {
    nul = flag_query > 0 && !flag_text && (flag_heading || !nul);
    
    if (nul)
      chr('\0');

    str(color_fn);

    if (nul)
      chr('\0');

    chr('{');
    str(partname);
    chr('}');

    if (nul)
      chr('\0');

    str(color_off);

    sep = true;
  }

  if (flag_line_number)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_ln);
    num(lineno, flag_initial_tab ? 6 : 1);
    str(color_off);

    sep = true;
  }

  if (flag_column_number)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_cn);
    num((matcher != NULL ? matcher->columno() + 1 : 1), (flag_initial_tab ? 3 : 1));
    str(color_off);

    sep = true;
  }

  if (flag_byte_offset)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_bn);
    num(byte_offset, flag_initial_tab ? 7 : 1);
    str(color_off);

    sep = true;
  }

  if (sep)
  {
    str(color_se);
    str(separator);
    str(color_off);

    if (flag_initial_tab)
      chr('\t');

    if (newline)
      nl();
  }
}

// output "Binary file ... matches"
void Output::binary_file_matches(const char *pathname, const std::string& partname)
{
  str(color_off);
  str("Binary file");
  str(color_fn);
  if (pathname != NULL)
  {
    chr(' ');
    str(pathname);
  }
  if (!partname.empty())
  {
    if (pathname == NULL)
      chr(' ');
    chr('{');
    str(partname);
    chr('}');
  }
  str(color_off);
  str(" matches");
  nl();
}

// output formatted match with options --format, --format-open, --format-close
void Output::format(const char *format, const char *pathname, const std::string& partname, size_t matches, reflex::AbstractMatcher *matcher, bool body, bool next)
{
  if (!body)
    lineno_ = 0;
  else if (lineno_ > 0 && lineno_ == matcher->lineno() && matcher->lines() == 1)
    return;

  size_t len = 0;
  const char *sep = NULL;
  const char *s = format;

  while (*s != '\0')
  {
    const char *a = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '[')
    {
      a = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case 'F':
        if (flag_with_filename && pathname != NULL)
        {
          if (a != NULL)
            str(a, s - a - 1);
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'f':
        if (pathname != NULL)
        {
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
        }
        break;

      case 'a':
        if (pathname != NULL)
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename == NULL)
            str(pathname);
          else
            str(basename + 1);
        }
        break;

      case 'p':
        if (pathname != NULL)
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename != NULL)
            str(pathname, basename - pathname);
        }
        break;

      case 'z':
        str(partname);
        break;

      case 'H':
        if (flag_with_filename)
        {
          if (a != NULL)
            str(a, s - a - 1);
          if (!partname.empty())
          {
            std::string name(pathname);
            name.push_back('{');
            name.append(partname);
            name.push_back('}');
            quote(name.c_str(), name.size());
          }
          else
          {
            quote(pathname, strlen(pathname));
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'h':
        if (!partname.empty())
        {
          std::string name(pathname);
          name.push_back('{');
          name.append(partname);
          name.push_back('}');
          quote(name.c_str(), name.size());
        }
        else
        {
          quote(pathname, strlen(pathname));
        }
        break;

      case 'N':
        if (flag_line_number)
        {
          if (a != NULL)
            str(a, s - a - 1);
          num(matcher->lineno());
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'n':
        num(matcher->lineno());
        break;

      case 'K':
        if (flag_column_number)
        {
          if (a != NULL)
            str(a, s - a - 1);
          num(matcher->columno() + 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'k':
        num(matcher->columno() + 1);
        break;

      case 'B':
        if (flag_byte_offset)
        {
          if (a != NULL)
            str(a, s - a - 1);
          num(matcher->first());
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'b':
        num(matcher->first());
        break;

      case 'T':
        if (flag_initial_tab)
        {
          if (a != NULL)
            str(a, s - a - 1);
          chr('\t');
        }
        break;

      case 't':
        chr('\t');
        break;

      case 'S':
        if (next)
        {
          if (a != NULL)
            str(a, s - a - 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 's':
        if (sep != NULL)
          str(sep, len);
        else
          str(flag_separator);
        break;

      case 'w':
        num(matcher->wsize());
        break;

      case 'd':
        num(matcher->size());
        break;

      case 'e':
        num(matcher->last());
        break;

      case 'G':
      {
        bool colon = false;
        std::pair<size_t,const char*> id = matcher->group_id();

        while (id.first != 0)
        {
          if (colon)
          {
            if (sep != NULL)
              str(sep, len);
            else
              str(flag_separator);
          }

          colon = true;

          if (a != NULL)
          {
            size_t n = id.first;
            const char *b;

            while (true)
            {
              b = strchr(a, '|');

              if (--n == 0 || b == NULL)
                break;

              a = b + 1;
            }

            if (b == NULL)
              b = strchr(a, ']');

            if (n == 0 && b != NULL)
              str(a, b - a);
            else if (id.second != NULL)
              str(id.second);
            else
              num(id.first);
          }
          else
          {
            if (id.second != NULL)
              str(id.second);
            else
              num(id.first);
          }

          id = matcher->group_next_id();
        }
        break;
      }

      case 'g':
        if (a != NULL)
        {
          std::pair<size_t,const char*> id = matcher->group_id();
          size_t n = id.first;

          if (n > 0)
          {
            const char *b;

            while (true)
            {
              b = strchr(a, '|');

              if (--n == 0 || b == NULL)
                break;

              a = b + 1;
            }

            if (b == NULL)
              b = strchr(a, ']');

            if (n == 0 && b != NULL)
              str(a, b - a);
            else if (id.second != NULL)
              str(id.second);
            else
              num(id.first);
          }
        }
        else
        {
          num(matcher->accept());
        }
        break;

      case 'm':
        num(matches);
        break;

      case 'O':
        mat(matcher);
        break;

      case 'o':
        str(matcher->begin(), matcher->size());
        break;

      case 'Q':
        quote(matcher);
        break;

      case 'q':
        quote(matcher->begin(), matcher->size());
        break;

      case 'C':
        if (flag_files_with_matches)
          str(flag_invert_match ? "\"false\"" : "\"true\"");
        else if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(matcher);
        break;

      case 'c':
        if (flag_files_with_matches)
          str(flag_invert_match ? "\"false\"" : "\"true\"");
        else if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(matcher->begin(), matcher->size());
        break;

      case 'V':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          csv(matcher);
        break;

      case 'v':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          csv(matcher->begin(), matcher->size());
        break;

      case 'J':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          json(matcher);
        break;

      case 'j':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          json(matcher->begin(), matcher->size());
        break;

      case 'X':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          xml(matcher);
        break;

      case 'x':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          xml(matcher->begin(), matcher->size());
        break;

      case 'Z':
        if (flag_fuzzy > 0)
        {
          reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);
          num(fuzzy_matcher->edits());
        }
        break;

      case 'u':
        if (!flag_ungroup)
          lineno_ = matcher->lineno();
        break;

      case '$':
        sep = a;
        len = s - a - 1;
        break;

      case '~':
        chr('\n');
        break;

      case '<':
        if (!next && a != NULL)
          str(a, s - a - 1);
        break;

      case '>':
        if (next && a != NULL)
          str(a, s - a - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (next)
          chr(c);
        break;

      default:
        chr(c);
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '#':
        std::pair<const char*,size_t> capture = (*matcher)[c == '#' ? strtoul((a != NULL ? a : "0"), NULL, 10) : c - '0'];
        str(capture.first, capture.second);
        break;
    }
    ++s;
  }
}

// output formatted match with options -v --format
void Output::format_invert(const char *format, const char *pathname, const std::string& partname, size_t matches, size_t lineno, size_t offset, const char *ptr, size_t size, bool next)
{
  size_t len = 0;
  const char *sep = NULL;
  const char *s = format;

  while (*s != '\0')
  {
    const char *a = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '[')
    {
      a = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case 'F':
        if (flag_with_filename && pathname != NULL)
        {
          if (a != NULL)
            str(a, s - a - 1);
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'f':
        if (pathname != NULL)
        {
          str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
        }
        break;

      case 'a':
        if (pathname != NULL)
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename == NULL)
            str(pathname);
          else
            str(basename + 1);
        }
        break;

      case 'p':
        if (pathname != NULL)
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename != NULL)
            str(pathname, basename - pathname);
        }
        break;

      case 'z':
        str(partname);
        break;

      case 'H':
        if (flag_with_filename)
        {
          if (a != NULL)
            str(a, s - a - 1);
          if (!partname.empty())
          {
            std::string name(pathname);
            name.push_back('{');
            name.append(partname);
            name.push_back('}');
            quote(name.c_str(), name.size());
          }
          else
          {
            quote(pathname, strlen(pathname));
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'h':
        if (!partname.empty())
        {
          std::string name(pathname);
          name.push_back('{');
          name.append(partname);
          name.push_back('}');
          quote(name.c_str(), name.size());
        }
        else
        {
          quote(pathname, strlen(pathname));
        }
        break;

      case 'N':
        if (flag_line_number)
        {
          if (a != NULL)
            str(a, s - a - 1);
          num(lineno);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'n':
        num(lineno);
        break;

      case 'K':
        if (flag_column_number)
        {
          if (a != NULL)
            str(a, s - a - 1);
          chr('1');
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'k':
        chr('1');
        break;

      case 'B':
        if (flag_byte_offset)
        {
          if (a != NULL)
            str(a, s - a - 1);
          num(offset);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'b':
        num(offset);
        break;

      case 'T':
        if (flag_initial_tab)
        {
          if (a != NULL)
            str(a, s - a - 1);
          chr('\t');
        }
        break;

      case 't':
        chr('\t');
        break;

      case 'S':
        if (next)
        {
          if (a != NULL)
            str(a, s - a - 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 's':
        if (sep != NULL)
          str(sep, len);
        else
          str(flag_separator);
        break;

      case 'w':
      {
        size_t n = 0;
        for (const char *s = ptr; s < ptr + size; ++s)
          n += (*s & 0xC0) != 0x80;
        num(n);
        break;
      }

      case 'd':
        num(size);
        break;

      case 'e':
        num(offset + size);
        break;

      case 'G':
      case 'g':
        break;

      case 'm':
        num(matches);
        break;

      case 'O':
      case 'o':
        str(ptr, size);
        break;

      case 'Q':
      case 'q':
        quote(ptr, size);
        break;

      case 'C':
      case 'c':
        if (flag_files_with_matches)
          str(flag_invert_match ? "\"false\"" : "\"true\"");
        else if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(ptr, size);
        break;

      case 'V':
      case 'v':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          csv(ptr, size);
        break;

      case 'J':
      case 'j':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          json(ptr, size);
        break;

      case 'X':
      case 'x':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          xml(ptr, size);
        break;

      case 'Z':
        break;

      case 'u':
        break;

      case '$':
        sep = a;
        len = s - a - 1;
        break;

      case '~':
        chr('\n');
        break;

      case '<':
        if (!next && a != NULL)
          str(a, s - a - 1);
        break;

      case '>':
        if (next && a != NULL)
          str(a, s - a - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (next)
          chr(c);
        break;

      default:
        chr(c);
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '#':
        break;
    }
    ++s;
  }
}
// output a quoted string with escapes for \ and "
void Output::quote(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    if (*s == '\\' || *s == '"')
    {
      str(t, s - t);
      t = s;
      chr('\\');
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output string in C/C++
void Output::cpp(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c < 0x20 || c == '"' || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        chr('\\');

        if (c > 0x20)
          chr(c);
        else
          oct(c);
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output quoted string in CSV
void Output::csv(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c == '"')
      {
        str(t, s - t);
        t = s + 1;
        str("\"\"");
      }
      else if ((c < 0x20 && c != '\t') || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        if (c > 0x20)
        {
          chr('\\');
          chr(c);
        }
        else
        {
          str("\\x");
          hex(c, 2);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output quoted string in JSON
void Output::json(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  chr('"');

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      if (c < 0x20 || c == '"' || c == '\\')
      {
        str(t, s - t);
        t = s + 1;

        switch (c)
        {
          case '\b':
            c = 'b';
            break;

          case '\f':
            c = 'f';
            break;

          case '\n':
            c = 'n';
            break;

          case '\r':
            c = 'r';
            break;

          case '\t':
            c = 't';
            break;
        }

        if (c > 0x20)
        {
          chr('\\');
          chr(c);
        }
        else
        {
          str("\\u");
          hex(c, 4);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output in XML
void Output::xml(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;
  const char *t = s;

  while (s < e)
  {
    int c = *s;

    if ((c & 0x80) == 0)
    {
      switch (c)
      {
        case '&':
          str(t, s - t);
          t = s + 1;
          str("&amp;");
          break;

        case '<':
          str(t, s - t);
          t = s + 1;
          str("&lt;");
          break;

        case '>':
          str(t, s - t);
          t = s + 1;
          str("&gt;");
          break;

        case '"':
          str(t, s - t);
          t = s + 1;
          str("&quot;");
          break;

        case 0x7f:
          str(t, s - t);
          t = s + 1;
          str("&#x7f;");
          break;

        default:
          if (c < 0x20)
          {
            str(t, s - t);
            t = s + 1;
            str("&#");
            num(c);
            chr(';');
          }
      }
    }
    ++s;
  }
  str(t, s - t);
}

const char *Output::Dump::color_hex[4] = { match_ms, color_sl, match_mc, color_cx };
