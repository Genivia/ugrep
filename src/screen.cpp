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
@file      screen.cpp
@brief     ANSI SGR code controlled screen API - static, not thread safe
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "screen.hpp"
#include <cctype>
#include <string>
#include <fcntl.h>

#ifndef OS_WIN
#include <signal.h>
#endif

// max collective length of ANSI CSI escape sequences collected when skipping lead text with skip>0
#define SCREEN_MAX_CODELEN 256

// enable to perform backspace (CTRL-H), not recommended because search results may not match what is shown
// #define WITH_BACKSPACE

// emit ANSI SGR CSI sequence with one numeric parameter
void Screen::CSI(int code, int num)
{
  char buf[32];
  char *ptr = buf;

  *ptr++ = '\033';
  *ptr++ = '[';
  itoa(num, &ptr);
  *ptr++ = code;
  put(buf, ptr - buf);
}

// emit ANSI SGR CSI sequence with two numeric parameters
void Screen::CSI(int code, int num1, int num2)
{
  char buf[32];
  char *ptr = buf;

  *ptr++ = '\033';
  *ptr++ = '[';
  itoa(num1, &ptr);
  *ptr++ = ';';
  itoa(num2, &ptr);
  *ptr++ = code;
  put(buf, ptr - buf);
}

// set the cursor position, where (0,0) is home
void Screen::setpos(int row, int col)
{
  CSI('H', row + 1, col + 1);
}

// get the cursor position
void Screen::getpos(int *row, int *col)
{
#ifdef OS_WIN

  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(hConOut, &info))
  {
    if (row != NULL)
      *row = info.dwCursorPosition.Y;

    if (col != NULL)
      *col = info.dwCursorPosition.X;
  }

#else

  char buf[16];
  char *ptr = buf;

  // flush the key buffer before requesting DSR
  VKey::flush();

  int retries = 10;

retry:

  // request DSR
  put("\033[6n", 4);

  // receive DSR response CSI row;col R
  while (ptr < buf + sizeof(buf) - 1)
  {
    // collect response, 100ms timeout
    int ch = VKey::raw_in(100);

    if (ch == 'R')
      break;

    // interrupted or timed out?
    if (ch <= 0)
    {
      if (retries-- <= 0)
        return;

      goto retry;
    }

    *ptr++ = ch;
  }

  *ptr = '\0';

  if (row != NULL)
  {
    ptr = strchr(buf, '[');
    if (ptr != NULL)
      *row = atoi(ptr + 1) - 1;
  }

  if (col != NULL)
  {
    ptr = strchr(buf, ';');
    if (ptr != NULL)
      *col = atoi(ptr + 1) - 1;
  }

#endif
}

// get the screen size Screen::rows and Screen::cols, returns Screen::cols
size_t Screen::getsize()
{
  // get current window size from ioctl
#if defined(OS_WIN)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (GetConsoleScreenBufferInfo(hConOut, &info))
  {
    rows = info.dwSize.Y;
    cols = info.dwSize.X;
  }
  else
#elif defined(TIOCGWIN)
  struct winsize winsize;
  if (ioctl(tty, TIOCGWIN, &winsize) == 0)
  {
    rows = winsize.ts_lines;
    cols = winsize.ts_cols;
  }
  else
#elif defined(TIOCGWINSZ)
  struct winsize winsize;
  if (ioctl(tty, TIOCGWINSZ, &winsize) == 0)
  {
    rows = winsize.ws_row;
    cols = winsize.ws_col;
  }
  else
#endif
  {
    // save cursor position, reset window scroll margins, move cursor to 999;999
    put("\0337\033[r\033[999;999H", 15);

    int row = -1;
    int col = -1;

    // get cursor position 0 <= row <= 999 and 0 <= col <= 999
    getpos(&row, &col);

    if (row > 0 && col > 0)
    {
      rows = row + 1;
      cols = col + 1;
    }
    else
    {
      rows = 24;
      cols = 80;

#ifndef OS_WIN
      const char *env;
      env = getenv("LINES");
      if (env != NULL)
      {
        rows = atoi(env);
        if (row <= 1)
          rows = 24;
      }
      env = getenv("COLUMNS");
      if (env != NULL)
      {
        cols = atoi(env);
        if (cols <= 1)
          cols = 80;
      }
#endif
    }

    // restore cursor position
    put("\0338");
  }

  return cols;
}

// setup screen using an alternative screen buffer and optional title, returns true on success
bool Screen::setup(const char *title)
{
  good = true;

#ifdef OS_WIN

#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING

  hConOut = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hConOut != INVALID_HANDLE_VALUE)
  {
    GetConsoleMode(hConOut, &oldOutMode);

    DWORD outMode = oldOutMode | DISABLE_NEWLINE_AUTO_RETURN | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (!SetConsoleMode(hConOut, outMode))
    {
      CloseHandle(hConOut);
      hConOut = INVALID_HANDLE_VALUE;
      return good = false;
    }

#ifdef CP_UTF8
    SetConsoleOutputCP(CP_UTF8);
#endif
  }
  else
  {
    return good = false;
  }

#else

  return good = false;

#endif

#else

  tty = open("/dev/tty", O_RDWR);
  if (tty < 0)
  {
    if (isatty(STDOUT_FILENO) == 0)
      return good = false;
    tty = STDOUT_FILENO;
  }

  // enable window resize signal handler
  signal(SIGWINCH, Screen::sigwinch);

#endif

  // enable alternative screen buffer, alternate scroll, enable cursor w/o blinking, cursor keys normal mode, clear screen, reset colors
  put("\033[?1049h\033[?1007h\033[?25h\033[?12l\033[?1l\033[2J\033[m");

  // set title, when provided as argument
  if (title != NULL)
  {
    put("\033]0;");
    put(title);
    put('\a');
  }

  // determine window size
  getsize();

  // check width of U+3000, U+1F600 Emoji, U+20000 CJK
  put("\r　😀𠀀\033[1K");
  int col = -1;
  getpos(NULL, &col);
  if (col == -1)
    return good = false;
  double_width       = col > 3;
  double_width_Emoji = col > 5;
  double_width_CJK   = col > 4;

  // not monochrome
  mono = false;

  // cursor home
  home();

  return good;
}

// cleanup to restore main screen buffer
void Screen::cleanup()
{
#ifdef OS_WIN

  if (hConOut != INVALID_HANDLE_VALUE)
  {
    // disable alternative scroll and screen buffer
    put("\033[1;1H\033[2J\033[m\033[?1007l\033[?1049l");

    SetConsoleMode(hConOut, oldOutMode);

    CloseHandle(hConOut);
  }

#else

  // remove window resize signal handler
  signal(SIGWINCH, SIG_DFL);

  // disable alternative screen buffer
  put("\033[1;1H\033[2J\033[m\033[?1049l");

#endif
}

// return character width, 0 (non-spacing or invalid character), 1 (single width) or 2 (double width)
int Screen::wchar_width(uint32_t wc)
{
  /* based on https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c with full table
     generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c"
     from https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt
     this is a compressed table of combining character ranges, stored as
     range.first << 8 | (range.last - range.first) */
  static const uint32_t combining[] = {
    0x3006f, 0x48303, 0x48801, 0x5912c, 0x5bf00, 0x5c101, 0x5c401, 0x5c700,
    0x60003, 0x61005, 0x64b13, 0x67000, 0x6d60e, 0x6e701, 0x6ea03, 0x70f00,
    0x71100, 0x7301a, 0x7a60a, 0x7eb08, 0x90101, 0x93c00, 0x94107, 0x94d00,
    0x95103, 0x96201, 0x98100, 0x9bc00, 0x9c103, 0x9cd00, 0x9e201, 0xa0101,
    0xa3c00, 0xa4101, 0xa4701, 0xa4b02, 0xa7001, 0xa8101, 0xabc00, 0xac104,
    0xac701, 0xacd00, 0xae201, 0xb0100, 0xb3c00, 0xb3f00, 0xb4102, 0xb4d00,
    0xb5600, 0xb8200, 0xbc000, 0xbcd00, 0xc3e02, 0xc4602, 0xc4a03, 0xc5501,
    0xcbc00, 0xcbf00, 0xcc600, 0xccc01, 0xce201, 0xd4102, 0xd4d00, 0xdca00,
    0xdd202, 0xdd600, 0xe3100, 0xe3406, 0xe4707, 0xeb100, 0xeb405, 0xebb01,
    0xec805, 0xf1801, 0xf3500, 0xf3700, 0xf3900, 0xf710d, 0xf8004, 0xf8601,
    0xf9007, 0xf9923, 0xfc600,
    0x102d03, 0x103200, 0x103601, 0x103900, 0x105801, 0x11609f, 0x135f00,
    0x171202, 0x173202, 0x175201, 0x177201, 0x17b401, 0x17b706, 0x17c600,
    0x17c90a, 0x17dd00, 0x180b02, 0x18a900, 0x192002, 0x192701, 0x193200,
    0x193902, 0x1a1701, 0x1b0003, 0x1b3400, 0x1b3604, 0x1b3c00, 0x1b4200,
    0x1b6b08, 0x1dc00a, 0x1dfe01, 0x200b04, 0x202a04, 0x206003, 0x206a05,
    0x20d01f, 0x302a05, 0x309901, 0xa80600, 0xa80b00, 0xa82501, 0xfb1e00,
    0xfe000f, 0xfe2003, 0xfeff00, 0xfff902,
    0x10a0102, 0x10a0501, 0x10a0c03, 0x10a3802, 0x10a3f00, 0x1d16702, 0x1d1730f,
    0x1d18506, 0x1d1aa03, 0x1d24202, 0xe000100, 0xe00205f, 0xe0100ef,
  };

  int min = 0;
  int max = sizeof(combining) / sizeof(uint32_t) - 1;

  // ignore invisible characters, such as invalid UTF-8
  if (wc == 0)
    return 0;

  // control characters are double width to display them e.g. as \t or ^I
  if (wc < 0x20 || wc == 0x7f)
    return 2;

  // binary search in table of non-spacing characters
  if (wc >= (combining[0] >> 8) && wc <= (combining[max] >> 8) + (combining[max] & 0xff))
  {
    while (max >= min)
    {
      int mid = (min + max) / 2;
      if (wc < (combining[mid] >> 8))
        max = mid - 1;
      else if (wc > (combining[mid] >> 8) + (combining[mid] & 0xff))
        min = mid + 1;
      else
        return 0;
    }
  }

  // if double wide character support is turned off, return 1
  if (!double_width)
    return 1;

  // if we arrive here, wc is not a combining or C0/C1 control character
  return 1 + 
    (wc >= 0x1100 &&
     (wc <= 0x115f ||                   // Hangul Jamo init. consonants
      wc == 0x2329 || wc == 0x232a ||
      (wc >= 0x2e80 && wc <= 0xa4cf && wc != 0x303f) || // CJK ... Yi
      (wc >= 0xac00 && wc <= 0xd7a3) || // Hangul Syllables
      (wc >= 0xf900 && wc <= 0xfaff) || // CJK Compatibility Ideographs
      (wc >= 0xfe10 && wc <= 0xfe19) || // Vertical forms
      (wc >= 0xfe30 && wc <= 0xfe6f) || // CJK Compatibility Forms
      (wc >= 0xff00 && wc <= 0xff60) || // Fullwidth Forms
      (wc >= 0xffe0 && wc <= 0xffe6) ||
      (double_width_Emoji &&
       (wc >= 0x1f18e && wc <= 0x1f9ff)) || // Emoticons etc (updated)
      (double_width_CJK &&
       ((wc >= 0x20000 && wc <= 0x2fffd) || // CJK
        (wc >= 0x30000 && wc <= 0x3fffd)))));
}

// return UCS-4 code of the specified UTF-8 sequence, or 0 for invalid UTF-8, set endptr after the sequence
uint32_t Screen::wchar(const char *ptr, const char **endptr)
{
  uint32_t c1, c2, c3, c4;

  c1 = static_cast<unsigned char>(*ptr++);

  if (c1 <= 0x7f)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return c1;
  }

  if ((c1 & 0xc0) != 0xc0 || c1 <= 0xc1 || c1 > 0xf4)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return 0; // incomplete or invalid UTF-8
  }

  c2 = static_cast<unsigned char>(*ptr++);

  if ((c2 & 0xc0) != 0x80 || (c1 == 0xed && c2 > 0x9f))
  {
    if (endptr != NULL)
      *endptr = ptr;
    return 0; // incomplete UTF-8 or surrogates
  }

  c2 &= 0x3f;

  if (c1 < 0xe0)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return (((c1 & 0x1f) << 6) | c2);
  }

  c3 = static_cast<unsigned char>(*ptr++);

  if ((c3 & 0xc0) != 0x80)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return 0; // incomplete UTF-8
  }

  c3 &= 0x3f;

  if (c1 < 0xf0)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return (((c1 & 0x0f) << 12) | (c2 << 6) | c3);
  }

  c4 = static_cast<unsigned char>(*ptr++);

  if ((c4 & 0xc0) != 0x80)
  {
    if (endptr != NULL)
      *endptr = ptr;
    return 0; // incomplete UTF-8
  }

  if (endptr != NULL)
    *endptr = ptr;

  return ((c1 & 0x07) << 18) | (c2 << 12) | (c3 << 6) | (c4 & 0x3f);
}

// emit text at the specified screen position, where (0,0) is home, return row number of the updated cursor position
int Screen::put(int row, int col, const char *text, size_t size, int skip, int wrap, int nulls)
{
  if (size == std::string::npos)
    size = strlen(text);

  const char *end = text + size;

  int len = cols - col;

  // when text starts with \0, how many more nulls to ignore, part of filename marking?
  nulls = *text == '\0' ? nulls : 0;

  if (nulls > 0)
    ++text;

  if (len > 0 && row < rows)
  {
    const char *next;

    setpos(row, col);

    if (skip > 0)
    {
      // skip text to display
      int num = skip;

      // collect ANSI CSI sequences
      char codebuf[SCREEN_MAX_CODELEN];
      char *codeptr = codebuf;

      while (num > 0 && text < end)
      {
        switch (*text)
        {
          case '\0':
            if (nulls > 0)
              --nulls;
            else
              num += 2;
            ++text;
            break;

#ifdef WITH_BACKSPACE
          case '\b':
            ++num;
            ++text;
            break;
#endif

          case '\t':
            num -= 1 + (~(cols - num) & 7);
            ++text;
            break;

          case '\n':
            erase();
            ++row;
            if (row >= rows)
              return row;
            setpos(row, col);
            num = skip;
            ++text;
            break;

          case '\r':
            ++text;
            break;

          default:
            if (*text == '\033' && text + 1 < end && (text[1] == '[' || text[1] == ']'))
            {
              if (text + 1 < end && text[1] == '[')
              {
                // CSI \e[... sequence
                next = text;
                next += 2;
                while (next < end && (*next < 0x40 || *next > 0x7e))
                  ++next;
                if (next < end)
                  ++next;
                if (!mono && codeptr + (next - text) < codebuf + SCREEN_MAX_CODELEN)
                {
                  memcpy(codeptr, text, next - text);
                  codeptr += next - text;
                }
              }
              else
              {
                // OSC \e]...BEL|ST sequence
                next = text;
                next += 2;
                while (next < end && *next != '\a' && (*next != '\033' || (next + 1 < end && next[1] != '\\')))
                  ++next;
                if (*next == '\033')
                  ++next;
                if (next < end)
                  ++next;
                if (!mono && codeptr + (next - text) < codebuf + SCREEN_MAX_CODELEN)
                {
                  memcpy(codeptr, text, next - text);
                  codeptr += next - text;
                }
              }
            }
            else
            {
              uint32_t wc = wchar(text, &next);
              int width = *text == '\0' || (wc == 0 && *text != '\0') ? 2 : wchar_width(wc);
              num -= width;
              if (num < 0)
              {
                // cut a double wide character that does not fit in half
                if (width == 2 && num == -1)
                {
                  put(' ');
                  --len;
                }
              }
              else if (wc == 0 && *text != '\0')
              {
                // invalid Unicode character
                next = text + 1;
              }
            }
            text = next;
        }
      }

      if (text < end)
      {
        put(codebuf, codeptr - codebuf);
        if (sel)
          invert();
      }
    }

    const char *ptr = text;

    while (ptr != NULL && ptr < end)
    {
      switch (*ptr)
      {
        int tab;

#ifdef WITH_BACKSPACE
        case '\b':
          if (ptr > text)
          {
            const char *p = ptr;
            while (p > text && (*--p & 0xc0) == 0x80)
              continue;
            put(text, p - text);
            ++len;
          }
          text = ++ptr;
          break;
#endif

        case '\t':
          put(text, ptr - text);
          tab = 1 + (~(cols - len) & 7);
          len -= tab;
          if (len < 0)
          {
            erase();
            if (wrap >= 0)
            {
              ++row;
              if (row >= rows)
                return row;
              col = wrap;
              setpos(row, col);
              len = cols - col;
              text = ++ptr;
            }
            else
            {
              ptr = text = strchr(ptr, '\n');
            }
          }
          else
          {
            put("        ", tab);
            text = ++ptr;
          }
          break;

        case '\n':
          put(text, ptr - text);
          erase();
          ++row;
          if (row >= rows)
            return row;
          col = 0;
          setpos(row, 0);
          len = cols;
          text = ++ptr;
          break;

        case '\r':
          put(text, ptr - text);
          text = ++ptr;
          break;

        default:
          if (*ptr == '\033' && ptr + 1 < end && (ptr[1] == '[' || ptr[1] == ']'))
          {
            if (ptr[1] == '[')
            {
              // CSI \e[... sequence
              if (mono)
                put(text, ptr - text);
              ptr += 2;
              while (ptr < end && (*ptr < 0x40 || *ptr > 0x7e))
                ++ptr;
              if (ptr < end)
                ++ptr;
              if (mono)
              {
                text = ptr;
              }
              else if (sel)
              {
                put(text, ptr - text);
                invert();
                text = ptr;
              }
            }
            else
            {
              // OSC \e]...BEL|ST sequence
              if (mono)
                put(text, ptr - text);
              ptr += 2;
              while (ptr < end && *ptr != '\a' && (*ptr != '\033' || (ptr + 1 < end && ptr[1] != '\\')))
                ++ptr;
              if (*ptr == '\033')
                ++ptr;
              if (ptr < end)
                ++ptr;
              if (mono)
              {
                text = ptr;
              }
              else if (sel)
              {
                put(text, ptr - text);
                invert();
                text = ptr;
              }
            }
          }
          else
          {
            uint32_t wc = wchar(ptr, &next);
            int width = *ptr == '\0' || (wc == 0 && *ptr != '\0') ? 2 : wchar_width(wc);
            len -= width;
            if (len < 0 || (len == 0 && width == 0))
            {
              put(text, ptr - text);
              if (wrap >= 0)
              {
                ++row;
                if (row >= rows)
                  return row;
                col = wrap;
                setpos(row, col);
                len = cols - col;
                text = ptr;
              }
              else
              {
                ptr = text = strchr(ptr, '\n');
              }
            }
            else if (wc == 0 && *ptr != '\0')
            {
              // invalid Unicode character
              const char *xdigits = "0123456789ABCDEF";
              put(text, ptr - text);
              invert();
              unsigned char c = static_cast<unsigned char>(*ptr);
              char buf[2] = { xdigits[c >> 4], xdigits[c & 0xf] };
              put(buf, 2);
              noinvert();
              text = ++ptr;
            }
            else if (wc <= 0x1f)
            {
              // display CTRL character
              put(text, ptr - text);
              if (*ptr == '\0' && nulls > 0)
              {
                --nulls;
                len += 2;
              }
              else
              {
                invert();
                char buf[2] = { '^', static_cast<char>('@' + wc) };
                put(buf, 2);
                noinvert();
              }
              text = ++ptr;
            }
            else if (wc == 0x7f)
            {
              // display control character 0x7f
              put(text, ptr - text);
              invert();
              put("^?", 2);
              noinvert();
              text = ++ptr;
            }
            else
            {
              ptr = next;
            }
          }
      }
    }

    put(text, ptr - text);

    normal();

    if (len > 0)
    {
      erase();

      if (sel)
      {
        invert();
        put(' ');
        normal();
      }
    }
  }

  return row;
}

// convert integer to text
void Screen::itoa(int num, char **pptr)
{
  unsigned unum = static_cast<unsigned>(abs(num));
  unsigned div = 1;
  char *ptr = *pptr;

  if (num < 0)
    *ptr++ = '-';

  while (unum/div >= 10)
    div *= 10;

  do
  {
    unsigned ch = '0' + unum/div;
    *ptr++ = ch + (ch > '9' ? 7 : 0);
    unum %= div;
    div /= 10;
  } while (div > 0);

  *ptr = '\0';
  *pptr = ptr;
}

// SIGWINCH signal handler
void Screen::sigwinch(int)
{
  getsize();
}

int  Screen::rows = 24;    // number of screen rows
int  Screen::cols = 80;    // number of screen columns
bool Screen::mono = false; // monochrome
bool Screen::good = false; // true if all previous screen operations were successful after setup()

#ifdef OS_WIN

// Windows console state
HANDLE Screen::hConOut;
DWORD  Screen::oldOutMode;

#else

// Unix/Linux terminal state
int Screen::tty = STDOUT_FILENO;

#endif

bool Screen::sel                = false;
bool Screen::double_width       = false;
bool Screen::double_width_Emoji = false;
bool Screen::double_width_CJK   = false;
