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
@copyright (c) 2019-2025, Robert van Engelen, Genivia Inc. All rights reserved.
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
  if (flag_hex_star)
  {
    // if the previous hex line was the same as this hex line, output a * (but only once)
    size_t i;

    for (i = 0; i < flag_hex_columns; ++i)
      if (prevb[i] < 0 || bytes[i] != prevb[i])
        break;

    if (i >= flag_hex_columns)
    {
      if (!pstar)
      {
        out.str(color_se);
        out.chr('*');
        out.nl();

        pstar = true;
      }

      for (i = 0; i < MAX_HEX_COLUMNS; ++i)
        bytes[i] = -1;

      return;
    }
  }

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
      out.str("--", 2);
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
        if (flag_color != NULL)
        {
          if (byte < 0x20)
          {
            out.str("\033[7m", 4);
            out.chr('@' + byte);
            inverted = true;
          }
          else if (byte == 0x7f)
          {
            out.str("\033[7m~", 5);
            inverted = true;
          }
          else if (byte > 0x7f)
          {
            out.str("\033[7m.", 5);
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

  for (int i = 0; i < MAX_HEX_COLUMNS; ++i)
  {
    prevb[i] = bytes[i];
    bytes[i] = -1;
  }

  pstar = false;
}

// output color when set
void Output::color(const char *arg)
{
  if (arg != NULL)
  {
    static const char *colors[10] = {
      color_sl,
      color_cx,
      color_mt,
      color_ms,
      color_mc,
      color_fn,
      color_ln,
      color_cn,
      color_bn,
      color_se,
    };
    static const char *codes = "sl cx mt ms mc fn ln cn bn se ";
    char code[4] = { arg[0], arg[1], ' ', 0 };
    const char *s = strstr(codes, code);
    if (s != NULL)
      str(colors[(s - codes) / 3]);
  }
  else
  {
    str(color_off);
  }
}

// output the header part of the match, preceding the matched line
void Output::header(const char *pathname, const std::string& partname, bool& heading, size_t lineno, reflex::AbstractMatcher *matcher, size_t byte_offset, const char *separator, bool newline)
{
  // if hex dump line is incomplete and a header is output, then complete the hex dump first
  if (dump.incomplete() &&
      (heading ||
       (!flag_no_filename && !partname.empty()) ||
       flag_line_number ||
       flag_column_number ||
       flag_byte_offset))
    dump.done();

  // get column number when we need it
  size_t columno = flag_column_number && matcher != NULL ? matcher->columno() + 1 : 1;

  // -Q: mark pathname with three \0 markers in headings, unless -a
  bool nul = heading && flag_query && !flag_text;

  if (nul)
    chr('\0');

  // --hyperlink: open link, unless standard input
  bool hyp = pathname != Static::LABEL_STANDARD_INPUT && color_hl != NULL; // include hyperlinks

  if (hyp)
    open_hyperlink(pathname, !(heading && flag_heading) && flag_hyperlink_line, lineno, columno);

  // when a separator is needed
  bool sep = false;

  // header should include the pathname
  if (heading)
  {
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
      // --hyperlink: close link
      if (hyp)
        close_hyperlink();

      str(color_fn);
      str(color_del);
      str(color_off);
      nl();

      // --hyperlink: open link
      if (hyp)
        open_hyperlink(pathname, flag_hyperlink_line, lineno, columno);

      // the next headers should not include this pathname
      heading = false;
    }
    else
    {
      sep = !flag_null;
    }
  }

  if (!flag_no_filename && !partname.empty())
  {
    nul = flag_query && !flag_text && (flag_heading || !nul);

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
    num(lineno, (flag_initial_tab ? 6 : 1));
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
    num(columno, (flag_initial_tab ? 3 : 1));
    str(color_off);

    sep = true;
  }

  // --hyperlink: close link
  if (hyp)
    close_hyperlink();

  if (flag_byte_offset)
  {
    if (sep)
    {
      str(color_se);
      str(separator);
      str(color_off);
    }

    str(color_bn);
    num(byte_offset, (flag_initial_tab ? 7 : 1));
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

// output the short pathname header for --files_with_matches and --count
void Output::header(const char *pathname, const std::string& partname)
{
  bool hyp = pathname != Static::LABEL_STANDARD_INPUT && color_hl != NULL; // include hyperlinks
  bool nul = flag_query; // -Q: mark pathname with three \0 markers for quick navigation

  if (flag_tree)
  {
    // acquire lock on output and to access global Tree::path and Tree::depth
    acquire();

    int up = 0;

    while (!Tree::path.empty() && Tree::path.compare(0, Tree::path.size(), pathname, Tree::path.size()) != 0)
    {
      Tree::path.pop_back();

      size_t len = Tree::path.rfind(PATHSEPCHR);

      if (len == std::string::npos)
        Tree::path.clear();
      else
        Tree::path.resize(len + 1);

      ++up;
      --Tree::depth;
    }

    if (up > 0)
    {
      for (int i = 0; i < Tree::depth; ++i)
        str(Tree::bar);
      for (int i = 1; i < up; ++i)
        str(Tree::end);
      nl();

      // make sure to add a break between trees with terminated leafs
      if (up > 1 && *Tree::end != '\0' && Tree::depth == 0)
        nl();
    }
    else if (Tree::path.empty() && strchr(pathname, PATHSEPCHR) != NULL)
    {
      // add a break between the list of filenames without path and filenames with paths
      nl();
    }

    const char *sep;

    while ((sep = strchr(pathname + Tree::path.size(), PATHSEPCHR)) != NULL)
    {
      if (nul)
        chr('\0');

      for (int i = 1; i < Tree::depth; ++i)
        str(Tree::bar);

      if (Tree::depth > 0)
        str(Tree::ptr);

      if (nul)
        chr('\0');

      str(pathname + Tree::path.size(), sep - (pathname + Tree::path.size()) + 1);

      if (nul)
        chr('\0');

      nl();

      Tree::path.assign(pathname, sep - pathname + 1);
      ++Tree::depth;
    }

    if (nul)
      chr('\0');

    for (int i = 1; i < Tree::depth; ++i)
      str(Tree::bar);

    if (Tree::depth > 0)
      str(Tree::ptr);

    str(color_fn);

    if (hyp)
      open_hyperlink(pathname);

    if (nul)
      chr('\0');

    str(pathname + Tree::path.size());

    if (nul)
      chr('\0');

  }
  else
  {
    if (nul)
      chr('\0');

    str(color_fn);

    if (hyp)
      open_hyperlink(pathname);

    if (nul)
      chr('\0');

    str(pathname);

    if (nul)
      chr('\0');

    if (hyp)
      close_hyperlink();
  }

  if (!partname.empty())
  {
    chr('{');
    str(partname);
    chr('}');
  }

  str(color_off);
}

// output "Binary file ... matches"
void Output::binary_file_matches(const char *pathname, const std::string& partname)
{
  if ((mode_ & BINARY) != 0)
    return;

  str(color_off);
  str("Binary file ");
  str(color_fn);

  if (pathname != Static::LABEL_STANDARD_INPUT && color_hl != NULL)
    open_hyperlink(pathname);

  str(pathname);

  if (pathname != Static::LABEL_STANDARD_INPUT && color_hl != NULL)
    close_hyperlink();

  if (!partname.empty())
  {
    chr('{');
    str(partname);
    chr('}');
  }

  str(color_off);
  str(" matches");
  nl();

  mode_ |= BINARY;
}

// get a group capture's string pointer and size specified by %[ARG] as arg, if any
std::pair<const char*,size_t> Output::capture(reflex::AbstractMatcher *matcher, const char *arg)
{
  if (arg != NULL)
  {
    while (true)
    {
      const char *bar = strchr(arg, '|');
      const char *end = strchr(arg, ']');

      if (end == NULL)
        break;

      if (bar == NULL || bar > end)
        bar = end;

      if (isdigit(static_cast<unsigned char>(*arg)))
      {
        size_t index = strtoul(arg, NULL, 10);

        if (index == 0 || *bar == ']')
          return (*matcher)[index];

        std::pair<size_t,const char*> id = matcher->group_id();

        while (id.first != 0 && id.first != index)
          id = matcher->group_next_id();

        if (id.first == index)
          return (*matcher)[index];
      }
      else
      {
        std::pair<size_t,const char*> id = matcher->group_id();

        while (id.first != 0
            && (id.second == NULL || strncmp(id.second, arg, bar - arg) != 0 || id.second[bar - arg] != '\0'))
          id = matcher->group_next_id();

        if (id.first != 0)
          return (*matcher)[id.first];
      }

      if (*bar == ']')
        break;

      arg = bar + 1;
    }
  }

  return std::pair<const char*,size_t>(NULL, 0);
}

// output format with option --format-begin and --format-end
void Output::format(const char *format, size_t matches)
{
  const char *sep = NULL;
  size_t len = 0;
  const char *s = format;
  while (*s != '\0')
  {
    int width = 0;
    const char *arg = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '{')
    {
      char *r = NULL;
      width = strtol(s + 1, &r, 10);
      if (r != NULL && *r == '}')
        s = r + 1;
    }
    if (*s == '[')
    {
      arg = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case 'T':
        if (flag_initial_tab)
        {
          if (arg)
            str(arg, s - arg - 1);
          chr('\t');
        }
        break;

      case 'S':
        if (matches > 1)
        {
          if (arg)
            str(arg, s - arg - 1);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case '$':
        sep = arg;
        if (arg != NULL)
          len = s - arg - 1;
        break;

      case 't':
        chr('\t');
        break;

      case 's':
        if (sep != NULL)
          str(sep, len);
        else
          str(flag_separator);
        break;

      case 'R':
        if (!flag_break)
          break;
        // FALLTHROUGH

      case '~':
#ifdef OS_WIN
        chr('\r');
#endif
        chr('\n');
        break;

      case 'm':
        num(matches, width);
        break;

      case 'U':
        if (arg != NULL)
          wchr(strtol(arg, NULL, 16));
        break;

      case '=':
        color(arg);
        break;

      case '<':
        if (matches <= 1 && arg != NULL)
          str(arg, s - arg - 1);
        break;

      case '>':
        if (matches > 1 && arg != NULL)
          str(arg, s - arg - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (matches > 1)
          chr(c);
        break;

      default:
        chr(c);
    }
    ++s;
  }
}

// output formatted match with options --format, --format-open, --format-close
bool Output::format(const char *format, const char *pathname, const std::string& partname, size_t matches, size_t *matching, reflex::AbstractMatcher *matcher, bool& heading, bool body, bool next)
{
  if (!body)
    lineno_ = 0;
  else if (lineno_ > 0 && lineno_ == matcher->lineno() && matcher->lines() == 1)
    return false;

  if (matching != NULL)
    ++*matching;

  size_t len = 0;
  const char *sep = NULL;
  const char *s = format;

  while (*s != '\0')
  {
    bool plus = false;
    int width = 0;
    const char *arg = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '{')
    {
      char *r = NULL;
      plus = (s[1] == '+');
      width = strtol(s + 1, &r, 10);
      if (r != NULL && *r == '}')
        s = r + 1;
    }
    if (*s == '[')
    {
      arg = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case '+':
        if (flag_heading)
        {
          if (flag_with_filename)
          {
            if (heading)
            {
              if (arg != NULL)
                str(arg, s - arg - 1);
              str(pathname);
              if (flag_null)
                chr('\0');
              nl();
              heading = false;
            }
            else if (flag_break)
            {
              nl();
            }
          }
        }
        break;

      case 'F':
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (heading)
            str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
          if (flag_null)
            chr('\0');
          else if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'f':
        str(pathname);
        if (!partname.empty())
        {
          chr('{');
          str(partname);
          chr('}');
        }
        break;

      case 'a':
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename == NULL)
            str(pathname);
          else
            str(basename + 1);
        }
        break;

      case 'p':
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
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (!partname.empty())
          {
            std::string name;
            if (heading)
              name = pathname;
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

      case 'I':
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (!partname.empty())
          {
            std::string name;
            if (heading)
              name = pathname;
            name.push_back('{');
            name.append(partname);
            name.push_back('}');
            xml(name.c_str(), name.size());
          }
          else
          {
            xml(pathname, strlen(pathname));
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'i':
        if (!partname.empty())
        {
          std::string name(pathname);
          name.push_back('{');
          name.append(partname);
          name.push_back('}');
          xml(name.c_str(), name.size());
        }
        else
        {
          xml(pathname, strlen(pathname));
        }
        break;

      case 'N':
        if (flag_line_number)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          num(matcher->lineno(), (arg == NULL && flag_initial_tab ? 6 : width));
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'n':
        num(matcher->lineno(), width);
        break;

      case 'L':
        num(matcher->lines(), width);
        break;

      case 'l':
        num(matcher->lineno_end(), width);
        break;

      case 'K':
        if (flag_column_number)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          num(matcher->columno() + 1, (arg == NULL && flag_initial_tab ? 3 : width));
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'k':
        num(matcher->columno() + 1, width);
        break;

      case 'A':
        hex(matcher->first(), 8);
        chr('-');
        hex(matcher->last() - 1, 8);
        break;

      case 'B':
        if (flag_byte_offset)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          num(matcher->first(), width);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'b':
        if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            num(cap.first - matcher->text() + matcher->first(), width);
        }
        else
        {
          num(matcher->first(), width);
        }
        break;

      case 'T':
        if (flag_initial_tab)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          chr('\t');
        }
        break;

      case 't':
        chr('\t');
        break;

      case 'S':
        if (next)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
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
        num(matcher->wsize(), width);
        break;

      case 'd':
        if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            num(cap.second, width);
        }
        else
        {
          num(matcher->size(), width);
        }
        break;

      case 'e':
        if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            num(cap.first + cap.second - matcher->text() + matcher->first(), width);
        }
        else
        {
          num(matcher->last(), width);
        }
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

            if (arg != NULL)
            {
              size_t n = id.first;
              const char *bar;
              const char *end;

              while (true)
              {
                bar = strchr(arg, '|');
                end = strchr(arg, ']');

                --n;

                if (bar == NULL || (end != NULL && bar > end))
                {
                  bar = end;
                  break;
                }

                if (n == 0)
                  break;

                arg = bar + 1;
              }

              if (n == 0 && bar != NULL)
                str(arg, bar - arg);
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
        }
        break;

      case 'g':
        if (arg != NULL)
        {
          std::pair<size_t,const char*> id = matcher->group_id();
          size_t n = id.first;

          if (n > 0)
          {
            const char *bar;
            const char *end;

            while (true)
            {
              bar = strchr(arg, '|');
              end = strchr(arg, ']');

              --n;

              if (bar == NULL || (end != NULL && bar > end))
              {
                bar = end;
                break;
              }

              if (n == 0)
                break;

              arg = bar + 1;
            }

            if (n == 0 && bar != NULL)
              str(arg, bar - arg);
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

      case 'M':
        num(matches, width);
        break;

      case 'm':
        num(matching == NULL ? matches : *matching, width);
        break;

      case 'O':
        mat(matcher, width);
        break;

      case 'o':
      case '#':
        if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
          {
            const char *s = cap.first;
            size_t n = cap.second;
            if (flag_hex || (flag_with_hex && !reflex::isutf8(s, s + n)))
            {
              if (width > 0 && static_cast<size_t>(width) < n)
                n = width;
              hex(s, n);
            }
            else
            {
              if (width > 0)
                utf8strn(s, n, width);
              else
                str(s, n);
            }
          }
        }
        else
        {
          const char *s = matcher->begin();
          size_t n = matcher->size();
          if (flag_hex || (flag_with_hex && !reflex::isutf8(s, s + n)))
          {
            if (width > 0 && static_cast<size_t>(width) < n)
              n = width;
            hex(s, n);
          }
          else
          {
            if (width != 0)
              s = match_context(matcher, plus, width, n);
            str(s, n);
          }
        }
        break;

      case 'Q':
        quote(matcher, width);
        break;

      case 'q':
        if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            quote(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          quote(s, n);
        }
        break;

      case 'C':
        if (flag_files_with_matches)
          str(flag_invert_match ? "\"false\"" : "\"true\"");
        else if (flag_count)
          chr('"'), num(matches), chr('"');
        else
          cpp(matcher, width);
        break;

      case 'c':
        if (flag_files_with_matches)
        {
          str(flag_invert_match ? "\"false\"" : "\"true\"");
        }
        else if (flag_count)
        {
          chr('"'), num(matches), chr('"');
        }
        else if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            cpp(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          cpp(s, n);
        }
        break;

      case 'V':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          csv(matcher, width);
        break;

      case 'v':
        if (flag_files_with_matches)
        {
          str(flag_invert_match ? "false" : "true");
        }
        else if (flag_count)
        {
          num(matches);
        }
        else if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            csv(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          csv(s, n);
        }
        break;

      case 'J':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          json(matcher, width);
        break;

      case 'j':
        if (flag_files_with_matches)
        {
          str(flag_invert_match ? "false" : "true");
        }
        else if (flag_count)
        {
          num(matches);
        }
        else if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            json(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          json(s, n);
        }
        break;

      case 'X':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          xml(matcher, width);
        break;

      case 'x':
        if (flag_files_with_matches)
        {
          str(flag_invert_match ? "false" : "true");
        }
        else if (flag_count)
        {
          num(matches);
        }
        else if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            xml(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          xml(s, n);
        }
        break;

      case 'Y':
        if (flag_files_with_matches)
          hex(flag_invert_match ? 0 : 1);
        else if (flag_count)
          hex(matches);
        else
          hex(matcher, width);
        break;

      case 'y':
        if (flag_files_with_matches)
        {
          str(flag_invert_match ? "false" : "true");
        }
        else if (flag_count)
        {
          num(matches);
        }
        else if (arg != NULL)
        {
          std::pair<const char*,size_t> cap = capture(matcher, arg);
          if (cap.first != NULL)
            hex(cap.first, width > 0 ? utf8cut(cap.first, cap.second, width) : cap.second);
        }
        else
        {
          size_t n;
          const char *s = match_context(matcher, plus, width, n);
          hex(s, n);
        }
        break;

      case 'Z':
        if (flag_fuzzy > 0)
        {
          if (!flag_match)
          {
            // -Z: we used the fuzzy matcher to search, so a dynamic cast is fine
            reflex::FuzzyMatcher *fuzzy_matcher = dynamic_cast<reflex::FuzzyMatcher*>(matcher);
            if (!flag_files_with_matches && !flag_count)
              num(fuzzy_matcher->edits(), width);
            else
              num(fuzzy_matcher->distance() & 0xff, width);
          }
          else
          {
            // --match all
            chr('0');
          }
        }
        break;

      case 'u':
        if (!flag_ungroup)
          lineno_ = matcher->lineno();
        break;

      case '$':
        sep = arg;
        if (arg != NULL)
          len = s - arg - 1;
        break;

      case 'R':
        if (!flag_break)
          break;
        // FALLTHROUGH

      case '~':
#ifdef OS_WIN
        chr('\r');
#endif
        chr('\n');
        break;

      case 'U':
        if (arg != NULL)
          wchr(strtol(arg, NULL, 16));
        break;

      case '=':
        color(arg);
        break;

      case '<':
        if (!next && arg != NULL)
          str(arg, s - arg - 1);
        break;

      case '>':
        if (next && arg != NULL)
          str(arg, s - arg - 1);
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
        std::pair<const char*,size_t> capture = (*matcher)[c - '0'];
        str(capture.first, capture.second);
        break;
    }
    ++s;
  }

  return true;
}

// output formatted match with options -v --format
void Output::format_invert(const char *format, const char *pathname, const std::string& partname, size_t matches, size_t lineno, size_t offset, const char *ptr, size_t size, bool& heading, bool next)
{
  size_t len = 0;
  const char *sep = NULL;
  const char *s = format;

  while (*s != '\0')
  {
    int width = 0;
    const char *arg = NULL;
    const char *t = s;

    while (*s != '\0' && *s != '%')
      ++s;
    str(t, s - t);
    if (*s == '\0' || *(s + 1) == '\0')
      break;
    ++s;
    if (*s == '{')
    {
      char *r = NULL;
      width = strtol(s + 1, &r, 10);
      if (r != NULL && *r == '}')
        s = r + 1;
    }
    if (*s == '[')
    {
      arg = ++s;
      while (*s != '\0' && *s != ']')
        ++s;
      if (*s == '\0' || *(s + 1) == '\0')
        break;
      ++s;
    }

    int c = *s;

    switch (c)
    {
      case '+':
        if (flag_heading)
        {
          if (flag_with_filename)
          {
            if (heading)
            {
              if (arg != NULL)
                str(arg, s - arg - 1);
              str(pathname);
              if (flag_null)
                chr('\0');
              nl();
              heading = false;
            }
            else if (flag_break)
            {
              nl();
            }
          }
        }
        break;

      case 'F':
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (heading)
            str(pathname);
          if (!partname.empty())
          {
            chr('{');
            str(partname);
            chr('}');
          }
          if (flag_null)
            chr('\0');
          else if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'f':
        str(pathname);
        if (!partname.empty())
        {
          chr('{');
          str(partname);
          chr('}');
        }
        break;

      case 'a':
        {
          const char *basename = strrchr(pathname, PATHSEPCHR);
          if (basename == NULL)
            str(pathname);
          else
            str(basename + 1);
        }
        break;

      case 'p':
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
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (!partname.empty())
          {
            std::string name;
            if (heading)
              name = pathname;
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

      case 'I':
        if (flag_with_filename && (heading || !partname.empty()))
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          if (!partname.empty())
          {
            std::string name;
            if (heading)
              name = pathname;
            name.push_back('{');
            name.append(partname);
            name.push_back('}');
            xml(name.c_str(), name.size());
          }
          else
          {
            xml(pathname, strlen(pathname));
          }
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'i':
        if (!partname.empty())
        {
          std::string name(pathname);
          name.push_back('{');
          name.append(partname);
          name.push_back('}');
          xml(name.c_str(), name.size());
        }
        else
        {
          xml(pathname, strlen(pathname));
        }
        break;

      case 'N':
        if (flag_line_number)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          num(lineno, (arg == NULL && flag_initial_tab ? 6 : width));
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'n':
        num(lineno, width);
        break;

      case 'K':
        if (flag_column_number)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
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

      case 'A':
        hex(offset, 8);
        chr('-');
        hex(offset + size - 1, 8);
        break;

      case 'B':
        if (flag_byte_offset)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          num(offset, width);
          if (sep != NULL)
            str(sep, len);
          else
            str(flag_separator);
        }
        break;

      case 'b':
        num(offset, width);
        break;

      case 'T':
        if (flag_initial_tab)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
          chr('\t');
        }
        break;

      case 't':
        chr('\t');
        break;

      case 'S':
        if (next)
        {
          if (arg != NULL)
            str(arg, s - arg - 1);
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
        for (const char *p = ptr; p < ptr + size; ++p)
          n += (*p & 0xC0) != 0x80;
        num(n, width);
        break;
      }

      case 'd':
        num(size, width);
        break;

      case 'e':
        num(offset + size, width);
        break;

      case 'G':
      case 'g':
        break;

      case 'm':
        num(matches, width);
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

      case 'Y':
      case 'y':
        if (flag_files_with_matches)
          str(flag_invert_match ? "false" : "true");
        else if (flag_count)
          num(matches);
        else
          hex(ptr, size);
        break;

      case 'Z':
        break;

      case 'u':
        break;

      case '$':
        sep = arg;
        if (arg != NULL)
          len = s - arg - 1;
        break;

      case 'R':
        if (!flag_break)
          break;
        // FALLTHROUGH

      case '~':
#ifdef OS_WIN
        chr('\r');
#endif
        chr('\n');
        break;

      case 'U':
        if (arg != NULL)
          wchr(strtol(arg, NULL, 16));
        break;

      case '=':
        color(arg);
        break;

      case '<':
        if (!next && arg != NULL)
          str(arg, s - arg - 1);
        break;

      case '>':
        if (next && arg != NULL)
          str(arg, s - arg - 1);
        break;

      case ',':
      case ':':
      case ';':
      case '|':
        if (next)
          chr(c);
        break;

      case '#':
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
        break;

      default:
        chr(c);
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
        str("\"\"", 2);
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
          str("\\x", 2);
          hex(c, 2);
        }
      }
    }
    ++s;
  }
  str(t, s - t);

  chr('"');
}

// output in hex
void Output::hex(const char *data, size_t size)
{
  const char *s = data;
  const char *e = data + size;

  if (s < e)
  {
    hex(static_cast<uint8_t>(*s++), 2);
    while (s < e)
    {
      chr(' ');
      hex(static_cast<uint8_t>(*s++), 2);
    }
  }
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
          str("\\u", 2);
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
          str("&amp;", 5);
          break;

        case '<':
          str(t, s - t);
          t = s + 1;
          str("&lt;", 4);
          break;

        case '>':
          str(t, s - t);
          t = s + 1;
          str("&gt;", 4);
          break;

        case '"':
          str(t, s - t);
          t = s + 1;
          str("&quot;", 6);
          break;

        case 0x7f:
          str(t, s - t);
          t = s + 1;
          str("&#x7f;", 6);
          break;

        default:
          if (c < 0x20)
          {
            str(t, s - t);
            t = s + 1;
            str("&#", 2);
            num(c);
            chr(';');
          }
      }
    }
    ++s;
  }
  str(t, s - t);
}

// flush a block of data as truncated lines limited to --width columns, taking into account tabs, UTF-8, ANSI
bool Output::flush_truncated_lines(const char *data, size_t size)
{
  if (skip_)
  {
    const char *end = static_cast<const char*>(memchr(data, '\n', size));

    if (end == NULL)
      return false;

    size_t num = end - data + 1;

    data += num;
    size -= num;

    skip_ = false;
  }

  while (size > 0)
  {
    // search for \n or cols_ + width Unicode characters, whichever comes first
    const char *end = data + size;
    const char *esc = end;
    const char *scan;

    for (scan = data; scan < end && cols_ <= flag_width && *scan != '\n'; ++scan)
    {
      if (ansi_ != ANSI::NA)
      {
        // skip ANSI escape sequence state machine
        switch (ansi_)
        {
          case ANSI::ESC:
            switch (*scan)
            {
              case '[':
                // CSI \e[... sequence
                ansi_ = ANSI::CSI;
                break;

              case ']':
                // OSC \e]...BEL|ST sequence
                ansi_ = ANSI::OSC;
                break;

              default:
                ansi_ = ANSI::NA;
            }
            break;

          case ANSI::CSI:
            if (*scan >= 0x40 && *scan <= 0x7e)
              ansi_ = ANSI::NA;
            break;

          case ANSI::OSC:
            if (*scan == '\a')
              ansi_ = ANSI::NA;
            else if (*scan == '\033')
              ansi_ = ANSI::OSC_ESC;
            break;

          case ANSI::OSC_ESC:
            if (*scan == '\\')
              ansi_ = ANSI::NA;
            else
              ansi_ = ANSI::OSC;
            break;

          default:
            ansi_ = ANSI::NA;
        }
      }
      else if (*scan == '\t')
      {
        cols_ += 1 + (~cols_ & 7);
      }
      else if (*scan == '\033')
      {
        esc = scan;
        ansi_ = ANSI::ESC;
      }
      else if (static_cast<unsigned char>(*scan) < 0x80)
      {
        cols_ += (*scan >= ' ');
      }
      else
      {
        cols_ += (*scan & 0xc0) != 0x80;
      }
    }

    if (scan < end && *scan == '\n')
    {
      // write data with newline
      size_t num = scan - data + 1;
      size_t nwritten = fwrite(data, 1, num, file);
      if (nwritten < num)
        return true;

      data += num;
      size -= num;

      cols_ = 0;
    }
    else if (cols_ <= flag_width)
    {
      // write data
      size_t num = scan - data;
      size_t nwritten = fwrite(data, 1, num, file);
      if (nwritten < num)
        return true;

      data += num;
      size -= num;
    }
    else
    {
      // counted one column over, back up to ensure UTF-8 multibyte sequence is complete
      --scan;

      size_t num = scan - data;

      if (ansi_ != ANSI::NA)
      {
        // write data up to but not including ANSI ESC
        size_t len = esc - data;
        size_t nwritten = fwrite(data, 1, len, file);
        if (nwritten < len)
          return true;
      }
      else
      {
        // write data
        size_t nwritten = fwrite(data, 1, num, file);
        if (nwritten < num)
          return true;
      }

      data += num;
      size -= num;

      // disable CSI when line was truncated
      if (flag_color != NULL)
        if (fwrite("\033[m", 1, 3, file) < 3)
          return true;

      // write newline
#ifdef OS_WIN
      if (fwrite("\r\n", 1, 2, file) < 2)
        return true;
#else
      if (fwrite("\n", 1, 1, file) < 1)
        return true;
#endif

      cols_ = 0;

      // look for newline to reach next line
      scan = static_cast<const char*>(memchr(data, '\n', size));
      if (scan == NULL)
      {
        skip_ = true;
        break;
      }

      // move to next line
      num = scan - data + 1;
      data += num;
      size -= num;
    }
  }

  return false;
}

const char *Output::Dump::color_hex[4] = { match_ms, color_sl, match_mc, color_cx };

const char *Output::Tree::bar = "|  ";
const char *Output::Tree::ptr = "|_ ";
const char *Output::Tree::end = "~  ";

std::string Output::Tree::path;
int Output::Tree::depth;
