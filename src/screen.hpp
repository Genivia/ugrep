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
@file      screen.hpp
@brief     ANSI SGR code controlled screen API - static, not thread safe
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef SCREEN_HPP
#define SCREEN_HPP

#include "ugrep.hpp"
#include "vkey.hpp"

#include <cinttypes>

#ifndef OS_WIN
#include <unistd.h>
#endif

class Screen {

 public:

  // emit ANSI SGR CSI sequence with one numeric parameter
  static void CSI(int code, int num);

  // emit ANSI SGR CSI sequence with two numeric parameters
  static void CSI(int code, int num1, int num2);

  // clear screen, normal font and colors
  static void clear()
  {
    put("\033[1;1H\033[m\033[J");
  }

  // erase from cursor to the end of the line
  static void erase()
  {
    put("\033[K");
  }

  // erase from cursor to the end of the screen
  static void end()
  {
    put("\033[J");
  }

  // move cursor home (0,0)
  static void home()
  {
    put("\033[1;1H");
  }

  // move cursor up
  static void up(int num = 1)
  {
    CSI('A', num);
  }

  // move cursor down
  static void down(int num = 1)
  {
    CSI('B', num);
  }

  // move cursor forward
  static void forward(int num = 1)
  {
    CSI('C', num);
  }

  // move cursor back
  static void back(int num = 1)
  {
    CSI('D', num);
  }

  // scroll screen up
  static void pan_up(int num = 1)
  {
    CSI('S', num);
  }

  // scroll screen down
  static void pan_down(int num = 1)
  {
    CSI('T', num);
  }

  // normal font and colors
  static void normal()
  {
    put("\033[m");
  }

  // show selections
  static void select()
  {
    invert();
    sel = true;
  }

  // hide selections
  static void deselect()
  {
    noinvert();
    sel = false;
  }
  // enable bold font and/or bright colors
  static void bold()
  {
    put("\033[1m");
  }

  // disable bold font and/or bright colors
  static void nobold()
  {
    put("\033[21m");
  }

  // enable underline
  static void underline()
  {
    put("\033[4m");
  }

  // disable underline
  static void nounderline()
  {
    put("\033[24m");
  }

  // enable invert (reverse video)
  static void invert()
  {
    put("\033[7m");
  }

  // disable invert (reverse video)
  static void noinvert()
  {
    put("\033[27m");
  }

  // save the cursor position
  static void save()
  {
    put("\0337");
  }

  // restore the cursor position, when saved
  static void restore()
  {
    put("\0338");
  }

  // emit alert (bell)
  static void alert()
  {
    put('\a');
  }

  // set the cursor position, where (0,0) is home
  static void setpos(int row, int col);

  // get the cursor position
  static void getpos(int *row, int *col);

  // get the screen size Screen::rows and Screen::cols
  static void getsize();

  // setup screen using an alternative screen buffer and optional title
  static bool setup(const char *title = NULL);

  // cleanup to restore main screen buffer
  static void cleanup();

  // return character width, 0 (invalid character), 1 (single width) or 2 (double width)
  static int wchar_width(uint32_t wc);

  // return UCS-4 code of the specified UTF-8 sequence, or 0 for invalid UTF-8
  static uint32_t wchar(const char *ptr, const char **endptr);

  // return character width of the specified UTF-8 sequence, 0 (invalid character), 1 (single width) or 2 (double width)
  static int mbchar_width(const char *ptr, const char **endptr)
  {
    return wchar_width(wchar(ptr, endptr));
  }

  // emit text
  static void put(const char *text, size_t size)
  {
#ifdef OS_WIN
    DWORD nwritten;
    ok = WriteFile(hConOut, text, static_cast<DWORD>(size), &nwritten, NULL) && ok;
#else
    ok = write(tty, text, size) == static_cast<ssize_t>(size) && ok;
#endif
  }

  // emit character
  static void put(int ch)
  {
    char buf = static_cast<char>(ch);
    put(&buf, 1);
  }

  // emit text
  static void put(const char *text)
  {
    put(text, strlen(text));
  }

  // emit text at the specified screen position, where (0,0) is home
  static void put(int row, int col, const std::string& text, int skip = 0, int wrap = -1)
  {
    put(row, col, text.c_str(), text.size(), skip, wrap);
  }

  // emit text at the specified screen position, where (0,0) is home
  static void put(int row, int col, const char *text, size_t size = std::string::npos, int skip = 0, int wrap = -1);

  static int  rows; // number of screen rows
  static int  cols; // number of screen columns
  static bool mono; // monochrome
  static bool ok;   // true if all previous screen operations were OK after setup()

 protected:

  // convert integer to text
  static void itoa(int num, char **pptr);

  // SIGWINCH signal handler
  static void sigwinch(int);

#ifdef OS_WIN

  // Windows console
  static HANDLE hConOut;
  static DWORD  oldOutMode;

#else

  // Unix/Linux terminal
  static int tty;

#endif

  static bool sel;
  static bool double_width;
  static bool double_width_Emoji;
  static bool double_width_CJK;

};

#endif
