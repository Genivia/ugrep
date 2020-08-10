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
@file      query.cpp
@brief     Query engine and UI
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "ugrep.hpp"
#include "query.hpp"

#include <reflex/error.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <fcntl.h>

#ifdef OS_WIN

#include <direct.h>

// non-blocking pipe (Windows named pipe)
inline HANDLE nonblocking_pipe(int fd[2])
{
  DWORD pid = GetCurrentProcessId();
  std::string pipe_name = "\\\\.\\pipe\\ugrep_";
  pipe_name.append(std::to_string(pid)).append("_").append(std::to_string(time(NULL)));
  DWORD buffer_size = QUERY_BUFFER_SIZE;
  HANDLE pipe_r = CreateNamedPipeA(pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, buffer_size, buffer_size, 0, NULL);
  if (pipe_r != INVALID_HANDLE_VALUE)
  {
    HANDLE pipe_w = CreateFileA(pipe_name.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); 
    if (pipe_w != INVALID_HANDLE_VALUE)
    {
      fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), 0);
      fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), 0);
    }
    else
    {
      CloseHandle(pipe_r);
      pipe_r = INVALID_HANDLE_VALUE;
    }
  }
  return pipe_r;
}

#else

#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>

// create a pipe with non-blocking read end
inline int nonblocking_pipe(int fd[2])
{
  if (pipe(fd) == 0)
  {
    if (fcntl(fd[0], F_SETFL, fcntl(fd[0], F_GETFL) | O_NONBLOCK) >= 0)
      return 0;
    close(fd[0]);
    close(fd[1]);
  }
  return -1;
}

inline void set_blocking(int fd0)
{
  fcntl(fd0, F_SETFL, fcntl(fd0, F_GETFL) & ~O_NONBLOCK);
}

#endif

static constexpr const char *PROMPT = "\033[32;1m";    // bright green
static constexpr const char *CERROR = "\033[37;41;1m"; // bright white on red
static constexpr const char *LARROW = "«";             // left arrow
static constexpr const char *RARROW = "»";             // right arrow

// return pointer to character at screen col, taking UTF-8 double wide characters into account
char *Query::line_ptr(int col)
{
  char *ptr = line_;
  while (*ptr != '\0')
  {
    col -= Screen::mbchar_width(ptr, NULL);
    if (col < 0)
      break;
    ++ptr;
  }
  return ptr;
}

// return pointer to the character at pos distance after the screen col
char *Query::line_ptr(int col, int pos)
{
  return Screen::mbstring_pos(line_ptr(col), pos);
}

// return pointer to the end of the line
char *Query::line_end()
{
  char *ptr = line_;
  while (*ptr != '\0')
    ++ptr;
  return ptr;
}

// return number of character positions on the line up to the current screen Query::col_, taking UTF-8 double wide characters into account
int Query::line_pos()
{
  char *ptr = line_;
  char *end = line_ptr(col_);
  int pos = 0;
  while (ptr < end && *ptr != '\0')
  {
    Screen::wchar(ptr, const_cast<const char**>(&ptr));
    ++pos;
  }
  return pos;
}

// return the length of the line as a number of screen columns displayed
int Query::line_len()
{
  return Screen::mbstring_width(line_);
}

// return the length of the line as the number of wide characters
int Query::line_wsize()
{
  int num = 0;
  char *ptr = line_;
  while (*ptr != '\0')
  {
    Screen::wchar(ptr, const_cast<const char**>(&ptr));
    ++num;
  }
  return num;
}

// draw a textual part of the query search line
void Query::display(int col, int len)
{
  char *ptr = line_ptr(col);
  char *end = line_ptr(col + len);
  char *err = error_ >= 0 && !Screen::mono ? line_ptr(0, error_) : NULL;
  char *next;
  bool alert = false;
  for (next = ptr; next < end; ++next)
  {
    if (next == err)
    {
      Screen::put(ptr, next - ptr);
      Screen::put(CERROR);
      ptr = next;
      alert = true;
    }
    int ch = static_cast<unsigned char>(*next);
    if (ch < ' ' || ch == 0x7f)
    {
      Screen::put(ptr, next - ptr);
      if (alert && next > err)
      {
        Screen::normal();
        alert = false;
      }
      if (!alert)
        Screen::invert();
      if (ch == 0x7f)
      {
        Screen::put("^?");
      }
      else
      {
        char buf[2] = { '^', static_cast<char>('@' + ch) };
        Screen::put(buf, 2);
      }
      Screen::normal();
      ptr = next + 1;
      alert = false;
    }
    else if (alert && next > err && (ch & 0xc0) != 0x80)
    {
      Screen::put(ptr, next - ptr);
      Screen::normal();
      ptr = next;
      alert = false;
    }
  }
  Screen::put(ptr, next - ptr);
  if (next == err)
    Screen::put(CERROR);
}

// draw the query search line
void Query::draw()
{
  if (mode_ == Mode::QUERY)
  {
    if (select_ == -1)
    {
      Screen::home();

      if (prompt_ != NULL)
      {
        start_ = static_cast<int>(strlen(prompt_));

        if (!dirs_.empty())
        {
          int width = Screen::mbstring_width(dirs_.c_str());
          int offset = 0;
          if (width + 2 > Screen::cols/2)
            offset = width + 2 - Screen::cols/2;

          Screen::normal();
          if (offset > 0)
            Screen::put(LARROW);

          const char *dir = Screen::mbstring_pos(dirs_.c_str(), offset);
          Screen::put(dir);
          Screen::put(' ');

          start_ += Screen::mbstring_width(dir) + 1 + (offset > 0);
        }

        if (!Screen::mono)
        {
          Screen::normal();
          if (error_ == -1)
            Screen::put(PROMPT);
          else
            Screen::put(CERROR);
        }

        Screen::put(prompt_);
      }

      Screen::normal();

      int pos;
      if (len_ - col_ < shift_)
        pos = Screen::cols - start_ - (len_ - col_) - 1;
      else
        pos = Screen::cols - start_ - shift_ - 1;

      offset_ = col_ > pos ? col_ - pos : 0;

      if (offset_ > 0)
      {
        if (!Screen::mono)
        {
          if (error_ == -1)
            Screen::put(PROMPT);
          else
            Screen::put(CERROR);
        }

        Screen::put(LARROW);
        Screen::normal();

        int adj = 1;
        if (line_ptr(offset_) == line_ptr(offset_ + 1)) // adjust columns when double width char at offset
        {
          Screen::put(' ');
          adj = 2; // make the displayed line start one character later
        }

        if (len_ >= offset_ + Screen::cols - start_)
        {
          display(offset_ + adj, Screen::cols - start_ - adj - 1);
          Screen::erase();
          if (!Screen::mono)
          {
            if (error_ == -1)
              Screen::put(PROMPT);
            else
              Screen::put(CERROR);
          }
          Screen::put(RARROW);
        }
        else
        {
          display(offset_ + adj, len_ - offset_ - adj);
          Screen::erase();
        }
      }
      else
      {
        if (len_ > Screen::cols - start_)
        {
          display(0, Screen::cols - start_ - 1);
          Screen::erase();
          if (!Screen::mono)
          {
            if (error_ == -1)
              Screen::put(PROMPT);
            else
              Screen::put(CERROR);
          }
          Screen::put(RARROW);
        }
        else
        {
          display(0, len_);
          if (len_ < Screen::cols - start_)
            Screen::erase();
        }
      }
    }
    else
    {
      Screen::normal();
      Screen::put(0, 0, "\033[7mEnter\033[m/\033[7mDel\033[m (de)select line  \033[7mA\033[mll  \033[7mC\033[mlear  \033[7mEsc\033[m go back  \033[7m^Q\033[m quit & output");
    }
  }
  else if (mode_ == Mode::LIST)
  {
    Screen::normal();
    Screen::put(0, 0, "\033[7mEnter\033[m/\033[7mDel\033[m (de)select file type  \033[7mC\033[m clear  \033[7mEsc\033[m go back");
  }
  else if (mode_ == Mode::EDIT)
  {
    Screen::put(0, 0, "\033[7mEDIT\033[m");

    Screen::setpos(select_ - row_ + 1, 0);

    int pos;
    if (len_ - col_ < shift_)
      pos = Screen::cols - (len_ - col_) - 1;
    else
      pos = Screen::cols - shift_ - 1;

    offset_ = col_ > pos ? col_ - pos : 0;

    if (offset_ > 0)
    {
      Screen::put(LARROW);
      Screen::normal();

      int adj = 1;
      if (line_ptr(offset_) == line_ptr(offset_ + 1)) // adjust columns when double width char at offset
      {
        Screen::put(' ');
        adj = 2; // make the displayed line start one character later
      }

      if (len_ >= offset_ + Screen::cols)
      {
        display(offset_ + adj, Screen::cols - adj - 1);
        Screen::erase();
        Screen::put(RARROW);
      }
      else
      {
        display(offset_ + adj, len_ - offset_ - adj);
        Screen::erase();
      }
    }
    else
    {
      Screen::normal();

      if (len_ > Screen::cols)
      {
        display(0, Screen::cols - 1);
        Screen::erase();
        Screen::put(RARROW);
      }
      else
      {
        display(0, len_);
        if (len_ < Screen::cols)
          Screen::erase();
      }
    }
  }
}

void Query::view(int row)
{
  Screen::normal();
  if (selected_[row])
    Screen::select();
  Screen::put(row - row_ + 1, 0, view_[row], skip_);
  if (selected_[row])
    Screen::deselect();
}

// redraw the screen
void Query::redraw()
{
  Screen::getsize();
  shift_ = (Screen::cols - start_) / 10;
  Screen::normal();

  if (mode_ == Mode::HELP)
  {
    Screen::put( 1, 0, "");
    Screen::put( 2, 0, "\033[7mEsc\033[m   go back / exit");
    Screen::put( 3, 0, "\033[7mTab\033[m   cd dir / select file");
    Screen::put( 4, 0, "\033[7mS-Tab\033[m cd .. / deselect file");
    Screen::put( 5, 0, "\033[7mEnter\033[m line selection mode");
    Screen::put( 6, 0, "");
    Screen::put( 7, 0, "\033[7mUp\033[m     \033[7mDown\033[m    scroll");
    Screen::put( 8, 0, "\033[7mPgUp\033[m   \033[7mPgDn\033[m    scroll page");
#ifdef WITH_MACOS_META_KEY
    Screen::put( 9, 0, "\033[7mS-Left\033[m \033[7mS-Right\033[m pan ½ page");
    Screen::put(10, 0, "\033[7mS-Up\033[m   \033[7mS-Down\033[m  scroll ½ pg");
#else
    Screen::put( 9, 0, "\033[7mM-Left\033[m \033[7mM-Right\033[m pan ½ page");
    Screen::put(10, 0, "\033[7mM-Up\033[m   \033[7mM-Down\033[m  scroll ½ pg");
#endif
    Screen::put(11, 0, "");
    Screen::put(12, 0, "\033[7mHome\033[m \033[7mEnd\033[m begin/end of line");
    Screen::put(13, 0, "\033[7m^K\033[m delete after cursor");
    Screen::put(14, 0, "\033[7m^L\033[m refresh screen");
    Screen::put(15, 0, "\033[7m^Q\033[m quick exit and output");
    Screen::put(16, 0, "\033[7m^R\033[m or \033[7mF4\033[m jump to bookmark");
    Screen::put(17, 0, "\033[7m^S\033[m scroll to next file/dir");
    Screen::put(18, 0, "\033[7m^T\033[m toggle colors on/off");
    Screen::put(19, 0, "\033[7m^U\033[m delete before cursor");
    Screen::put(20, 0, "\033[7m^V\033[m verbatim character");
    Screen::put(21, 0, "\033[7m^W\033[m scroll back one file/dir");
    Screen::put(22, 0, "\033[7m^X\033[m or \033[7mF3\033[m set bookmark");
    Screen::put(23, 0, "\033[7m^Y\033[m or \033[7mF2\033[m edit file");
    Screen::put(24, 0, "\033[7m^Z\033[m or \033[7mF1\033[m help");
    Screen::put(25, 0, "\033[7m^^\033[m chdir to starting dir");
    Screen::put(26, 0, "\033[7m^\\\033[m terminate process");
    Screen::put(27, 0, "");
    Screen::put(28, 0, "\033[7mM-/xxxx/\033[m U+xxxx code point");
    Screen::put(29, 0, "");

    std::string buf;
    int row = 30;
    int col = 0;

    for (Flags *fp = flags_; fp->text != NULL; ++fp)
    {
      buf.assign("\033[7mM- \033[m [\033[32;1m \033[m] ");
      buf[6] = fp->key;
      if (fp->flag)
        buf[19] = 'X';
      buf.append(fp->text);
      if (row >= Screen::rows)
      {
        row = 2;
        col += 28;
      }
      Screen::put(row, col, buf);
      ++row;
    }

    if (col == 0)
      Screen::end();

    if (!message_)
    {
#ifdef WITH_MACOS_META_KEY
      Screen::put(0, 0, "\033[7mF1\033[m help and options:        \033[7m^\033[m=\033[7mCtrl\033[m  \033[7mS-\033[m=\033[7mShift\033[m  \033[7mM-\033[m=\033[7mAlt\033[m/\033[7mOption\033[m or use \033[7m^O\033[m+key");
#else
      Screen::put(0, 0, "\033[7mF1\033[m help and options:        \033[7m^\033[m=\033[7mCtrl\033[m  \033[7mS-\033[m=\033[7mShift\033[m  \033[7mM-\033[m=\033[7mAlt\033[m or use \033[7m^O\033[m+key");
#endif
    }

    message_ = false;

    Screen::put(0, Screen::cols - 1, "?");
  }
  else
  {
    if (select_ >= 0 && select_ >= row_ + Screen::rows - 1)
      row_ = select_ - Screen::rows + 3;
    else if (select_ >= 0 && select_ < row_)
      row_ = select_ - 1;
    if (row_ >= rows_)
      row_ = rows_ - 1;
    if (row_ < 0)
      row_ = 0;
    int end = rows_;
    if (end > row_ + Screen::rows - 1)
      end = row_ + Screen::rows - 1;
    for (int i = row_; i < end; ++i)
      view(i);
    if (!message_)
      draw();
  }
}

#ifdef OS_WIN

// CTRL-C/BREAK handler
BOOL WINAPI Query::sigint(DWORD signal)
{
  VKey::cleanup();
  Screen::cleanup();

  // return FALSE to invoke the next handler
  return FALSE;
}

#else

void Query::sigwinch(int)
{
  redraw();
}

// SIGINT and SIGTERM handler
void Query::sigint(int sig)
{
  VKey::cleanup();
  Screen::cleanup();

  // force close, to deliver pending writes
  close(Screen::tty);

  // reset to the default handler
  signal(sig, SIG_DFL);

  // signal again
  kill(getpid(), sig);
}

#endif

// move the cursor to a column
void Query::move(int col)
{
  int dir = 0;
  if (col > col_)
    dir = 1;
  else if (col < col_)
    dir = -1;
  if (col <= 0)
    col = 0;
  else if (col >= len_)
    col = len_;
  else if (dir != 0 && line_ptr(col - 1) == line_ptr(col)) // oops, we're at the second half of a full width char
    col += dir; // direction is -1 or 1 to jump at or after full width char
  col_ = col;
  if (len_ >= Screen::cols - start_ && col >= Screen::cols - start_ - shift_)
  {
    draw();
  }
  else
  {
    if (offset_ > 0)
      draw();
    else
      Screen::setpos(0, start_ + col_ - offset_);
  }
}

// insert text to line at cursor
void Query::insert(const char *text, size_t size)
{
  char *end = line_end();
  if (end + size >= line_ + QUERY_MAX_LEN)
  {
    size = line_ + QUERY_MAX_LEN - end - 1;
    Screen::alert();
  }
  if (size > 0)
  {
    char *ptr = line_ptr(col_);
    memmove(ptr + size, ptr, end - ptr + 1);
    memcpy(ptr, text, size);
    int oldlen = len_;
    len_ = line_len();
    int forward = len_ - oldlen;
    if (forward > 0)
    {
      updated_ = true;
      error_ = -1;
      col_ += forward;
      draw();
    }
  }
}

// insert one character (or a byte of a multi-byte sequence) in the line at the cursor
void Query::insert(int ch)
{
  char buf = static_cast<char>(ch);
  insert(&buf, 1);
}

// erase num bytes from the line at and after the cursor
void Query::erase(int num)
{
  char *ptr = line_ptr(col_);
  char *next = line_ptr(col_, num);
  if (next > ptr)
  {
    memmove(ptr, next, line_end() - next + 1);
    updated_ = true;
    error_ = -1;
    len_ = line_len();
    draw();
  }
}

// called by the main program
void Query::query()
{
  if (!VKey::setup(VKey::RAW))
    abort("no keyboard detected");

  if (!Screen::setup("ugrep --query"))
  {
    VKey::cleanup();
    abort("no ANSI terminal screen detected");
  }

#ifdef OS_WIN

  // handle CTRL-C
  SetConsoleCtrlHandler(&sigint, TRUE);

#else
  
  signal(SIGINT, sigint);

  signal(SIGQUIT, sigint);

  signal(SIGTERM, sigint);

  signal(SIGPIPE, SIG_IGN);

  signal(SIGWINCH, sigwinch);

#endif

  VKey::map_alt_key('E', NULL);
  VKey::map_alt_key('Q', NULL);

  for (Flags *fp = flags_; fp->text != NULL; ++fp)
    VKey::map_alt_key(fp->key, NULL);

  get_flags();

  get_stdin();

  query_ui();

  VKey::cleanup();
  Screen::cleanup();

  // check TTY again for color support, this time without --query
  flag_query = 0;
  terminal();

  if (!flag_quiet)
    print();

  // close the search pipe to terminate the search threads, if still open
  if (!eof_)
  {
    close(search_pipe_[0]);
    eof_ = true;

    // graciously shut down ugrep() if still running
    cancel_ugrep();
  }

  // close the stdin pipe
  if (flag_stdin && source != stdin && source != NULL)
  {
    fclose(source);
    source = NULL;
  }

  // join the search thread
  if (search_thread_.joinable())
    search_thread_.join();

  // join the stdin sender thread
  if (stdin_thread_.joinable())
    stdin_thread_.join();
}

// run the query UI
void Query::query_ui()
{
  mode_       = Mode::QUERY;
  updated_    = false;
  message_    = false;
  *line_      = '\0';
  col_        = 0;
  len_        = 0;
  offset_     = 0;
  shift_      = 8;
  error_      = -1;
  row_        = 0;
  rows_       = 0;
  skip_       = 0;
  select_     = -1;
  select_all_ = false;
  globbing_   = false;
  eof_        = true;
  buflen_     = 0;

  // if -e PATTERN specified, collect patterns in the line to edit
  if (!flag_regexp.empty())
  {
    std::string pattern;

    if (flag_regexp.size() == 1)
    {
      pattern = flag_regexp.front();
    }
    else
    {
      for (auto& regex : flag_regexp)
      {
        if (!regex.empty())
        {
          if (!pattern.empty())
            pattern.push_back('|');
          pattern.append(regex);
        }
      }
    }

    flag_regexp.clear();

    size_t num = pattern.size();
    if (num >= QUERY_MAX_LEN)
      num = QUERY_MAX_LEN - 1;

    pattern.copy(line_, num);
    line_[num] = '\0';

    len_ = line_len();

    move(len_);
  }

  Screen::clear();

  set_prompt();

  search();

  bool ctrl_o = false;
  bool ctrl_v = false;

  while (true)
  {
    size_t delay = message_ ? QUERY_MESSAGE_DELAY : flag_query;

    int key = 0;

    while (true)
    {
      if (mode_ == Mode::QUERY)
      {
        update();

        if (!message_)
        {
          if (select_ == -1)
            Screen::setpos(0, start_ + col_ - offset_);
          else
            Screen::setpos(select_ - row_ + 1, 0);
        }
      }
      else
      {
        Screen::setpos(select_ - row_ + 1, col_ - offset_);
      }

      key = VKey::in(100);

      if (key > 0)
        break;

      --delay;

      if (delay == 0)
      {
        if (mode_ == Mode::QUERY && updated_)
        {
          search();
        }
#ifdef OS_WIN
        else
        {
          // detect screen size changes here for Windows (no SIGWINCH)
          int rows = Screen::rows;
          int cols = Screen::cols;

          Screen::getsize();

          if (rows != Screen::rows || cols != Screen::cols)
            redraw();
        }
#endif

        if (message_)
        {
          message_ = false;
          draw();
        }

        delay = flag_query;
      }
    }

    message_ = false;

    if (ctrl_o)
    {
      // CTRL-O + key = Alt+key
      meta(key);

      ctrl_o = false;
    }
    else if (ctrl_v)
    {
      // CTRL-V: insert verbatim character
      if (key < 0x80)
        insert(key);

      ctrl_v = false;
    }
    else
    {
      switch (key)
      {
        case VKey::ESC:
          if (mode_ == Mode::QUERY)
          {
            if (globbing_)
            {
              globbing_ = false;

              memcpy(line_, temp_, QUERY_MAX_LEN);
              len_ = line_len();
              move(len_);

              set_prompt();
              draw();
            }
            else if (select_ == -1)
            {
              if (confirm("Exit"))
                return;
            }
            else
            {
              select_ = -1;
              redraw();
            }
          }
          break;

        case VKey::LF:
        case VKey::CR:
          if (mode_ == Mode::QUERY || mode_ == Mode::LIST)
          {
            if (select_ == -1)
            {
              if (rows_ > 0)
              {
                select_ = row_;
                select_all_ = false;
                draw();
              }
              else
              {
                Screen::alert();
              }
            }
            else
            {
              selected_[select_] = !selected_[select_];
              view(select_);
              down();
            }
          }
          else if (mode_ == Mode::EDIT)
          {
            if (select_ + 1 == rows_)
              ++rows_;
            down();
          }
          break;

        case VKey::META:
          key = VKey::get();
          switch (key)
          {
            case VKey::TAB: // Shift-TAB: chdir .. or deselect file (or pan screen in selection mode)
              if (mode_ == Mode::QUERY)
              {
                if (select_ == -1)
                {
                  deselect();
                }
                else
                {
                  skip_ -= 8;
                  if (skip_ < 0)
                    skip_ = 0;
                  redraw();
                }
              }
              else
              {
                Screen::alert();
              }
              break;

            case VKey::UP: // SHIFT-UP/CTRL-UP: scroll half page up
              pgup(true);
              break;

            case VKey::DOWN: // SHIFT-DOWN/CTRL-DOWN: scroll half page down
              pgdn(true);
              break;

            case VKey::LEFT: // SHIFT-LEFT/CTRL-LEFT: pan hald a page left
              if (mode_ == Mode::QUERY)
              {
                skip_ -= Screen::cols / 2;
                if (skip_ < 0)
                  skip_ = 0;
                redraw();
              }
              else
              {
                Screen::alert();
              }
              break;

            case VKey::RIGHT: // SHIFT-RIGHT/CTRL-RIGHT: pan hald a page right
              if (mode_ == Mode::QUERY)
              {
                skip_ += Screen::cols / 2;
                redraw();
              }
              else
              {
                Screen::alert();
              }
              break;

            default:
              if (select_ == -1)
                meta(key);
              else
                Screen::alert();
          }
          break;

        case VKey::TAB: // TAB: chdir or select file (or pan screen in selection mode)
          if (mode_ == Mode::QUERY)
          {
            if (select_ == -1)
            {
              select();
            }
            else
            {
              skip_ += 8;
              redraw();
            }
          }
          else if (mode_ == Mode::EDIT)
          {
            insert('\t');
          }
          else
          {
            Screen::alert();
          }
          break;

        case VKey::BS: // Backspace: delete character before the cursor
          if (mode_ == Mode::QUERY || mode_ == Mode::LIST)
          {
            if (select_ == -1)
            {
              if (col_ <= 0)
                break;
              move(col_ - 1);
              erase(1);
            }
            else
            {
              up();
              selected_[select_] = !selected_[select_];
              view(select_);
            }
          }
          else if (mode_ == Mode::EDIT)
          {
            if (col_ <= 0)
            {
              up();
              move(len_);
            }
            else
            {
              move(col_ - 1);
              erase(1);
            }
          }
          break;

        case VKey::DEL: // DEL: delete character under the cursor
          if (mode_ == Mode::EDIT || select_ == -1)
          {
            erase(1);
          }
          else
          {
            up();
            selected_[select_] = !selected_[select_];
            view(select_);
          }
          break;

        case VKey::RIGHT: // RIGHT/CTRL-F: move right
          if (mode_ == Mode::EDIT || select_ == -1)
          {
            move(col_ + 1);
          }
          else if (mode_ == Mode::QUERY)
          {
            skip_ += 8;
            redraw();
          }
          else
          {
            Screen::alert();
          }
          break;

        case VKey::LEFT: // LEFT/CTRL-B: move left
          if (mode_ == Mode::EDIT || select_ == -1)
          {
            move(col_ - 1);
          }
          else if (mode_ == Mode::QUERY)
          {
            skip_ -= 8;
            if (skip_ < 0)
              skip_ = 0;
            redraw();
          }
          else
          {
            Screen::alert();
          }
          break;

        case VKey::UP: // UP/CTRL-P: move up
          up();
          break;

        case VKey::DOWN: // DOWN/CTRL-N: move down
          down();
          break;

        case VKey::PGUP: // PgUp/CTRL-G: page up
          pgup();
          break;

        case VKey::PGDN: // PgDn/CTRL-D: page down
          pgdn();
          break;

        case VKey::HOME: // HOME/CTRL-A: begin of line
          if (mode_ == Mode::EDIT || select_ == -1)
            move(0);
          else
            Screen::alert();
          break;

        case VKey::END: // END/CTRL-E: end of line
          if (mode_ == Mode::EDIT || select_ == -1)
            move(len_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_C: // CTRL-C: quit and output lines
          if (confirm("Exit"))
            return;
          break;

        case VKey::CTRL_K: // CTRL-K: delete after cursor
          if (mode_ == Mode::EDIT || select_ == -1)
            erase(len_ - col_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_L: // CTRL-L: refresh screen
          redraw();
          break;

        case VKey::CTRL_O: // CTRL-O: press KEY to get Alt-KEY
          if (mode_ == Mode::EDIT || select_ == -1)
            ctrl_o = true;
          else
            Screen::alert();
          break;

        case VKey::CTRL_R: // CTRL-R: jump to bookmark
        case VKey::FN(4):
          jump(mark_);
          break;

        case VKey::CTRL_Q: // CTRL-Q: immediately quit and output lines
          return;

        case VKey::CTRL_S: // CTRL=S: scroll to next file (or directory with -l or -c)
          next();
          break;

        case VKey::CTRL_T: // CTRL-T: toggle colors on/off
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::CTRL_U: // CTRL-U: delete before cursor
          if (mode_ == Mode::EDIT || select_ == -1)
          {
            int pos = line_pos();
            col_ = 0;
            erase(pos);
          }
          else
          {
            Screen::alert();
          }
          break;

        case VKey::CTRL_V: // CTRL-V: verbatim insert character
          if (select_ == -1)
            ctrl_v = true;
          else
            Screen::alert();
          break;

        case VKey::CTRL_W: // CTRL-W: scroll back one file or (or directory with -l or -c)
          back();
          break;

        case VKey::CTRL_X: // CTRL-X: set bookmark
        case VKey::FN(3):
          mark_ = select_ >= 0 ? select_ : row_;
          break;

        case VKey::CTRL_Y: // CTRL-Y: edit file
        case VKey::FN(2):
          edit();
          break;

        case VKey::CTRL_Z: // CTRL-Z: help
        case VKey::FN(1):
          if (help())
            return;
          break;

        case VKey::CTRL_BS: // CTRL-\: terminate
#ifdef OS_WIN
          GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
#else
          raise(SIGTERM);
#endif
          break;

        case VKey::CTRL_CA: // CTRL-^: chdir back to working directory
          if (mode_ == Mode::QUERY && select_ == -1)
            unselect();
          break;

        default:
          if (key >= 32 && key < 256)
          {
            if (mode_ == Mode::EDIT || select_ == -1)
            {
              insert(key);
            }
            else if (key == 'A' || key == 'a')
            {
              for (int i = 0; i < rows_; ++i)
                selected_[i] = true;
              select_all_ = true;

              redraw();
            }
            else if (key == 'C' || key == 'c')
            {
              for (int i = 0; i < rows_; ++i)
                selected_[i] = false;
              select_all_ = false;

              redraw();
            }
            else
            {
              Screen::alert();
            }
          }
          else
          {
            if (help())
              return;
          }
      }
    }
  }
}

// start a new search, stop the previous search when still running
void Query::search()
{
  row_ = 0;
  rows_ = 0;
  skip_ = 0;
  dots_ = 3;

  if (!eof_)
  {
    close(search_pipe_[0]);
    eof_ = true;
    buflen_ = 0;

    // graciously shut down ugrep() if still running
    cancel_ugrep();
  }

  error_ = -1;

#ifdef OS_WIN

  hPipe_ = nonblocking_pipe(search_pipe_);

  if (hPipe_ == INVALID_HANDLE_VALUE)
  {
    if (!Screen::mono)
      Screen::put(CERROR);
    Screen::put(0, 0, "Error: cannot create pipe");
    return;
  }

  memset(&overlapped_, 0, sizeof(overlapped_));
  blocking_ = false;
  pending_ = false;

#else

  if (nonblocking_pipe(search_pipe_) < 0)
  {
    if (!Screen::mono)
      Screen::put(CERROR);
    Screen::put(0, 0, "Error: cannot create pipe");
    return;
  }

#endif

  eof_ = false;

  if (search_thread_.joinable())
    search_thread_.join();

  arg_pattern = globbing_ ? temp_ : line_;

  if (deselect_file_)
  {
    selected_file_.clear();
    arg_files.clear();
    deselect_file_ = false;
  }
  else if (!selected_file_.empty() && arg_files.empty())
  {
    arg_files.emplace_back(selected_file_.c_str());
  }

  set_flags();

  set_stdin();

  if (error_ == -1)
    search_thread_ = std::thread(Query::execute, search_pipe_[1]);

  select_ = -1;
  select_all_ = false;

  redraw();

  updated_ = false;
}

// update screen and display more data when data becomes available
bool Query::update()
{
  int begin = rows_;

  // fetch viewable portion plus a screenful more, when available
  fetch(row_ + 2 * Screen::rows - 2);

  // display the viewable portion when updated
  if (rows_ > begin && begin < row_ + Screen::rows - 1)
  {
    Screen::normal();

    if (begin + Screen::rows - 1 > rows_)
      begin = rows_ - Screen::rows + 1;
    if (begin < 0)
      begin = 0;

    int end = rows_;
    if (end > begin + Screen::rows - 1)
      end = begin + Screen::rows - 1;

    if (begin < row_)
      begin = row_;

    for (int i = begin; i < end; ++i)
      view(i);
  }

  // display final status line if in view
  if (rows_ < row_ + Screen::rows - 1)
  {
    Screen::normal();
    Screen::invert();

    if (error_ == -1)
    {
      searching_[9] = '.';
      searching_[10] = '.';
      searching_[11] = '.';
      searching_[9 + dots_] = '\0';
      dots_ = (dots_ + 1) & 3;

      Screen::put(rows_ - row_ + 1, 0, eof_ ? "(END)" : searching_);
      Screen::normal();

      // when searching, don't immediately clear the rest of the screen
      if (eof_ || dots_ == 3)
        Screen::end();
    }
    else
    {
      Screen::put(rows_ - row_ + 1, 0, "(ERROR)");
      Screen::normal();
      Screen::erase();

      if (!Screen::mono)
      {
        Screen::setpos(2, 0);
        Screen::put(CERROR);
        Screen::erase();
      }

      Screen::put(2, 0, what_);
      Screen::normal();

      Screen::end();
    }
  }

  // return true if screen was updated when data was available
  return begin < rows_;
}

// fetch rows up to and including the specified row, when available, i.e. do not block when pipe is non-blocking
void Query::fetch(int row)
{
  while (rows_ <= row)
  {
    bool incomplete = false;

    // look for the first newline character in the buffer
    char *nlptr = static_cast<char*>(memchr(buffer_, '\n', buflen_));

    if (nlptr == NULL)
    {
      // no newline and buffer is not full and not EOF reached yet, get more data
      if (buflen_ < QUERY_BUFFER_SIZE && !eof_)
      {
#ifdef OS_WIN

        // try to fetch more data from the non-blocking pipe when immediately available
        DWORD nread = 0;
        bool avail = !pending_;

        if (pending_)
        {
          pending_ = false;

          if (!GetOverlappedResult(hPipe_, &overlapped_, &nread, FALSE))
          {
            switch (GetLastError())
            {
              case ERROR_IO_INCOMPLETE:
                pending_ = true;
                break;

              case ERROR_MORE_DATA:
                break;

              case ERROR_HANDLE_EOF:
              default:
                close(search_pipe_[0]);
                eof_ = true;
                cancel_ugrep();
            }
          }
        }

        if (avail)
        {
          pending_ = false;

          if (!ReadFile(hPipe_, buffer_ + buflen_, static_cast<DWORD>(QUERY_BUFFER_SIZE - buflen_), &nread, blocking_ ? NULL : &overlapped_))
          {
            switch (GetLastError())
            {
              case ERROR_IO_PENDING:
                pending_ = true;
                break;

              case ERROR_MORE_DATA:
                break;

              case ERROR_HANDLE_EOF:
              case ERROR_BROKEN_PIPE:
              default:
                close(search_pipe_[0]);
                eof_ = true;
                cancel_ugrep();
            }
          }
        }

        buflen_ += nread;

#else

        // try to fetch more data from the non-blocking pipe when immediately available
        ssize_t nread = read(search_pipe_[0], buffer_ + buflen_, QUERY_BUFFER_SIZE - buflen_);

        if (nread > 0)
        {
          // success, more data read into the buffer
          buflen_ += nread;
        }
        else if (nread < 0)
        {
          // if pipe is empty but not EINTR/EAGAIN/EWOULDBLOCK then error (EOF)
          if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
          {
            close(search_pipe_[0]);
            eof_ = true;
            cancel_ugrep();
          }
        }
        else
        {
          // no data, pipe is closed (EOF)
          close(search_pipe_[0]);
          eof_ = true;
          cancel_ugrep();
        }

#endif
      }

      if (buflen_ == 0)
        break;

      // we may have more data now, so look again for the first newline character in the buffer
      nlptr = static_cast<char*>(memchr(buffer_, '\n', buflen_));

      if (nlptr == NULL)
      {
        // data has no newline but buffer_[] is full, so we will add it and mark it as an incomplete row
        nlptr = buffer_ + buflen_;

        if (!eof_)
          incomplete = true;
      }
    }

    if (nlptr != NULL)
    {
      // allocate more rows on demand
      if (rows_ >= static_cast<int>(view_.size()))
      {
        view_.emplace_back();
        selected_.push_back(select_all_);
      }

      // assign or append the row from the buffer
      if (append_)
        view_[rows_].append(buffer_, nlptr - buffer_);
      else
        view_[rows_].assign(buffer_, nlptr - buffer_);

      // this row is selected if select all is set
      selected_[rows_] = select_all_;

      // if row is complete, move to the next
      if (!incomplete)
      {
        // added another row
        ++rows_;

        // skip \n
        if (nlptr < buffer_ + buflen_)
          ++nlptr;
      }

      // append the next chunk of text from the buffer
      append_ = incomplete;

      buflen_ -= nlptr - buffer_;

      // shift the buffer
      memmove(buffer_, nlptr, buflen_);
    }
  }
}

// fetch all lines when all is selected in selection mode
void Query::fetch_all()
{
  if (select_all_ && (!eof_ || buflen_ > 0))
  {
    // reading the search pipe should block until all data is received
#ifdef OS_WIN
    blocking_ = true;
    pending_ = false;
#else
    set_blocking(search_pipe_[0]);
#endif

    while (true)
    {
      int i = rows_;
      fetch(rows_);
      if (i < rows_)
        break;
    }
  }
}

// execute the search in a new thread
void Query::execute(int fd)
{
  output = fdopen(fd, "w");

  if (output != NULL)
  {
    try
    {
      ugrep();
    }

    catch (reflex::regex_error& error)
    {
      what_.assign(error.what());
      
      // error position in the pattern
      error_ = static_cast<int>(error.pos());

      // subtract 4 for (?m) or (?mi)
      if (error_ >= 4 + flag_ignore_case)
        error_ -= 4 + flag_ignore_case;

      // subtract 2 for -F
      if (flag_fixed_strings && error_ >= 2)
        error_ -= 2;

      // subtract 2 or 3 for -x or -w
      if (flags_[27].flag && error_ >= 2)
        error_ -= 2;
      else if (flags_[25].flag && error_ >= 3)
        error_ -= 3;
    }

    catch (std::exception& error)
    {
      what_.assign(error.what());

      // error at the end of the line, not within
      error_ = line_wsize();
    }

    fclose(output);
    output = NULL;
  }
  else
  {
    if (!Screen::mono)
      Screen::put(CERROR);
    Screen::put(0, 0, "Error: cannot fdopen pipe");
  }
}

void Query::load_line()
{
  if (mode_ == Mode::EDIT)
  {
    if (static_cast<size_t>(select_) < view_.size())
    {
      size_t size = view_[select_].size();
      if (size >= QUERY_MAX_LEN)
        size = QUERY_MAX_LEN - 1;
      view_[select_].copy(line_, size);
      line_[size] = '\0';
      len_ = line_len();
      if (col_ > len_)
        move(len_);
    }
    else
    {
      *line_ = '\0';
      view_.emplace_back(line_);
      len_ = 0;
      col_ = 0;
    }
  }
}

void Query::save_line()
{
  if (mode_ == Mode::EDIT)
  {
    if (static_cast<size_t>(select_) >= view_.size())
      view_.emplace_back(line_);
    else
      view_[select_].assign(line_);
  }
}

void Query::up()
{
  if (select_ > 0)
  {
    save_line();
    --select_;
    load_line();
    if (select_ > row_)
      return;
  }
  if (row_ > 0)
  {
    view(row_ - 1);
    --row_;
    Screen::pan_down();
    draw();
  }
}

void Query::down()
{
  if (select_ >= 0)
  {
    save_line();
    ++select_;
    if (select_ >= rows_)
      select_ = rows_ - 1;
    load_line();
    if (select_ < row_ + Screen::rows - 2)
      return;
  }
  if (row_ + 1 < rows_)
  {
    ++row_;
    Screen::normal();
    Screen::pan_up();
    if (row_ + Screen::rows - 2 < rows_)
      view(row_ + Screen::rows - 2);
    draw();
  }
}

void Query::pgup(bool half_page)
{
  if (select_ >= 0)
  {
    save_line();
    if (half_page)
      select_ -= Screen::rows / 2;
    else
      select_ -= Screen::rows - 2;
    if (select_ < 0)
      select_ = 0;
    load_line();
    if (select_ > row_)
      return;
  }
  if (row_ > 0)
  {
    view(row_ - 1);
    int oldrow = row_;
    if (half_page)
      row_ -= Screen::rows / 2;
    else
      row_ -= Screen::rows - 2;
    if (row_ < 0)
      row_ = 0;
    Screen::pan_down(oldrow - row_);
    for (int i = row_; i < oldrow - 1; ++i)
      view(i);
    draw();
  }
}

void Query::pgdn(bool half_page)
{
  if (select_ >= 0)
  {
    save_line();
    if (half_page)
      select_ += Screen::rows / 2;
    else
      select_ += Screen::rows - 2;
    if (select_ >= rows_)
      select_ = rows_ - 1;
    load_line();
    if (select_ < row_ + Screen::rows - 2)
      return;
  }
  if (row_ + Screen::rows - 1 <= rows_)
  {
    int oldrow = row_;
    if (half_page)
      row_ += Screen::rows / 2;
    else
      row_ += Screen::rows - 2;
    if (row_ + Screen::rows > rows_)
    {
      row_ = rows_ - Screen::rows + 2;
      if (row_ < oldrow)
        row_ = oldrow;
    }
    int diff = row_ - oldrow;
    if (diff > 0)
    {
      Screen::normal();
      Screen::pan_up(diff);
      for (int i = row_ + Screen::rows - diff - 1; i < row_ + Screen::rows - 1; ++i)
        if (i < rows_)
          view(i);
      draw();
    }
  }
}

// scroll back one file
void Query::back()
{
  if (rows_ <= 0)
    return;

  // if output is not suitable to scroll over its content by filename, then PGUP
  if (flag_text || flag_format != NULL)
  {
    pgup();

    return;
  }

  // compare the directory part for options -l and -c to move between directories
  bool compare_dir = flag_files_with_matches || flag_count;

  std::string filename;
  bool found = false;

  if (select_ == -1)
  {
    if (row_ == 0)
      return;

    --row_;

    // get the current filename to compare when present
    is_filename(view_[row_], filename);

    while (row_ > 0 && !(found = is_filename(view_[row_], filename, compare_dir)))
      --row_;

    if (found && !flag_heading)
      ++row_;
  }
  else
  {
    if (select_ == 0)
      return;

    --select_;

    // get the current filename to compare when present
    is_filename(view_[select_], filename);

    while (select_ > 0 && !(found = is_filename(view_[select_], filename, compare_dir)))
      --select_;

    if (found && !flag_heading)
      ++select_;
  }

  redraw();
}

// scroll to next file
void Query::next()
{
  // if output is not suitable to scroll by filename, then PGDN
  if (flag_text || flag_format != NULL)
  {
    pgdn();

    return;
  }

  // compare the directory part for options -l and -c to move between directories
  bool compare_dir = flag_files_with_matches || flag_count;

  std::string filename;

  if (select_ == -1)
  {
    if (row_ < rows_)
    {
      // get the current filename to compare when present
      is_filename(view_[row_], filename);
    }

    ++row_;

    while (true)
    {
      bool found = false;

      while (row_ + 1 < rows_ && !(found = is_filename(view_[row_], filename, compare_dir)))
        ++row_;

      if (found || (eof_ && buflen_ == 0))
        break;

      redraw();

      // fetch more search results when available
      if (update())
      {
        // poll keys without timeout and stop if a key was pressed
        if (VKey::poll(0))
          return;
      }
      else
      {
        // poll keys with 100 ms timeout and stop if a key was pressed
        if (VKey::poll(100))
          return;
      }
    }
  }
  else
  {
    if (select_ < rows_)
    {
      // get the current filename to compare when present
      is_filename(view_[select_], filename);
    }

    ++select_;

    while (true)
    {
      bool found = false;

      while (select_ + 1 < rows_ && !(found = is_filename(view_[select_], filename, compare_dir)))
        ++select_;

      if (found || (eof_ && buflen_ == 0))
        break;

      redraw();

      // fetch more search results when available
      if (update())
      {
        // poll keys without timeout and stop if a key was pressed
        if (VKey::poll(0))
          return;
      }
      else
      {
        // poll keys with 100 ms timeout and stop if a key was pressed
        if (VKey::poll(100))
          return;
      }
    }
  }

  redraw();
}

// jump to the specified row
void Query::jump(int row)
{
  if (rows_ <= 0)
    return;

  if (row < 0)
    row = 0;

  if (select_ == -1)
  {
    if (row <= row_)
    {
      row_ = row;

      if (row_ >= rows_)
        row_ = rows_ - 1;
    }
    else if (row < rows_)
    {
      row_ = row;
    }
    else
    {
      while (true)
      {
        while (row_ < row && row_ + 1 < rows_)
          ++row_;

        if (row_ == row || (eof_ && buflen_ == 0))
          break;

        redraw();

        // fetch more search results when available
        if (update())
        {
          // poll keys without timeout and stop if a key was pressed
          if (VKey::poll(0))
            return;
        }
        else
        {
          // poll keys with 100 ms timeout and stop if a key was pressed
          if (VKey::poll(100))
            return;
        }
      }
    }
  }
  else
  {
    if (row <= select_)
    {
      select_ = row;

      if (select_ >= rows_)
        select_ = rows_ - 1;
    }
    else if (row < rows_)
    {
      select_ = row;
    }
    else
    {
      while (true)
      {
        while (select_ < row && select_ + 1 < rows_)
          ++select_;

        if (select_ == row || (eof_ && buflen_ == 0))
          break;

        redraw();

        // fetch more search results when available
        if (update())
        {
          // poll keys without timeout and stop if a key was pressed
          if (VKey::poll(0))
            return;
        }
        else
        {
          // poll keys with 100 ms timeout and stop if a key was pressed
          if (VKey::poll(100))
            return;
        }
      }
    }
  }

  redraw();
}

// edit the file located under the cursor or just above in the screen
void Query::edit()
{
  if (row_ >= rows_ || flag_text || flag_format != NULL)
  {
    Screen::alert();

    return;
  }

  const char *editor = getenv("GREP_EDIT");

  if (editor == NULL)
    editor = getenv("EDITOR");

  if (editor == NULL)
  {
    Screen::alert();

    return;
  }

  std::string filename;
  bool found = false;

  for (int i = select_ >= 0 ? select_ : row_; i >= 0 && !(found = is_filename(view_[i], filename)); --i)
    continue;

  if (!found && arg_files.size() == 1)
  {
    filename = arg_files.front();
    found = true;
  }

  if (found)
  {
#ifdef OS_WIN
    DWORD attr = GetFileAttributesA(filename.c_str());
    found = attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_SYSTEM);
#else
    struct stat buf;
    found = stat(filename.c_str(), &buf) == 0 && S_ISREG(buf.st_mode);
#endif
  }

  if (found)
  {
    std::string command;
    command.assign(editor).append(" \"").append(filename).append("\"");

    Screen::put(0, 0, command.c_str());

    if (system(command.c_str()) == 0)
    {
      mark_ = select_ >= 0 ? select_ : row_;
      Screen::clear();
      search();
    }
    else
    {
      Screen::alert();
    }
  }
  else
  {
    message(std::string("cannot edit file ").append(filename));
  }
}

// chdir one level down into the directory of the file located under the cursor or just above the screen
void Query::select()
{
  if (flag_stdin)
  {
    message("cannot chdir: standard input is searched");

    return;
  }

  if (!arg_files.empty())
  {
    message("cannot chdir: file or directory arguments are present");

    return;
  }

  std::string pathname;
  bool found = false;

  for (int i = select_ >= 0 ? select_ : row_; i >= 0 && !(found = is_filename(view_[i], pathname)); --i)
    continue;

  if (found)
  {
    size_t n = pathname.find(PATHSEPCHR);
    size_t b = pathname.find('{'); // do not cd into archives
    if (n != std::string::npos && (b == std::string::npos || n < b))
    {
      pathname.resize(n);

#ifdef OS_WIN
      if (_chdir(pathname.c_str()) < 0)
      {
        message("cannot chdir: operation denied");

        return;
      }
#else
      if (chdir(pathname.c_str()) < 0)
      {
        message("cannot chdir: operation denied");

        return;
      }
#endif

      dirs_.append(pathname).append(PATHSEPSTR);

      search();
    }
    else
    {
      if (b != std::string::npos)
        pathname.resize(b);

      selected_file_ = pathname;

      dirs_.append(pathname);

      search();
    }
  }
  else
  {
    Screen::alert();
  }
}

// chdir back up one level
void Query::deselect()
{
  if (selected_file_.empty())
  {
    if (flag_stdin)
    {
      message("cannot chdir: standard input is searched");

      return;
    }

    if (!arg_files.empty())
    {
      message("cannot chdir: file or directory arguments are present");

      return;
    }

#ifdef OS_WIN
    if (dirs_.size() == 3 && dirs_.at(1) == ':' && dirs_.at(2) == PATHSEPCHR)
      return;
#else
    if (dirs_ == PATHSEPSTR)
      return;
#endif

    if (dirs_.empty())
    {
#ifdef OS_WIN
      char *cwd = _getcwd(NULL, 0);
#else
      char *cwd = getcwd(NULL, 0);
#endif
      if (cwd != NULL)
      {
        size_t n = strlen(cwd);
        dirs_.assign(cwd);
        wdir_.assign(cwd);
        free(cwd);
        if (n == 0 || cwd[n-1] != PATHSEPCHR)
          dirs_.append(PATHSEPSTR);
      }
    }

#ifdef OS_WIN
    if (_chdir("..") < 0)
      return;
#else
    if (chdir("..") < 0)
      return;
#endif

    if (dirs_.empty())
    {
      dirs_.assign(".." PATHSEPSTR);
    }
    else
    {
      dirs_.pop_back();

      size_t n = dirs_.find_last_of(PATHSEPCHR);

      if (n == std::string::npos)
      {
        if (dirs_ != "..")
          dirs_.clear();
        else
          dirs_.append(PATHSEPSTR ".." PATHSEPSTR);
      }
      else if (dirs_.compare(n + 1, std::string::npos, "..") == 0)
      {
        dirs_.append(PATHSEPSTR ".." PATHSEPSTR);
      }
      else
      {
        dirs_.resize(n + 1);
      }
    }

    search();
  }
  else
  {
    size_t n = dirs_.find_last_of(PATHSEPCHR);

    if (n != std::string::npos)
      dirs_.resize(n + 1);
    else
      dirs_.clear();

    deselect_file_ = true;

    search();
  }
}

// chdir back to the starting working directory
void Query::unselect()
{
  if (!wdir_.empty())
  {
#ifdef OS_WIN
    if (_chdir(wdir_.c_str()) < 0)
      return;
#else
    if (chdir(wdir_.c_str()) < 0)
      return;
#endif

    dirs_.clear();
    wdir_.clear();

    deselect_file_ = true;

    search();
  }
  else if (!dirs_.empty())
  {
    if (!selected_file_.empty())
    {
      size_t n = dirs_.find_last_of(PATHSEPCHR);

      if (n != std::string::npos)
        dirs_.resize(n + 1);
      else
        dirs_.clear();
    }

    if (!dirs_.empty())
    {
      while (true)
      {
#ifdef OS_WIN
        if ((dirs_.size() == 3 && dirs_.at(1) == ':' && dirs_.at(2) == PATHSEPCHR) || _chdir("..") < 0)
          break;
#else
        if (dirs_ == PATHSEPSTR || chdir("..") < 0)
          break;
#endif

        dirs_.pop_back();

        size_t n = dirs_.find_last_of(PATHSEPCHR);

        if (n == std::string::npos)
          break;

        dirs_.resize(n + 1);
      }

      dirs_.clear();
    }

    deselect_file_ = true;

    search();
  }
}

// display a message
void Query::message(const std::string& message)
{
  if (!Screen::mono)
   Screen::put(PROMPT);
  Screen::put(0, 0, "-> ");
  if (!Screen::mono)
    Screen::normal();
  Screen::erase();
  Screen::put(0, 3, message.c_str());
  message_ = true;
}

// return true if prompt is confirmed
bool Query::confirm(const char *prompt)
{
  if (!flag_confirm)
    return true;

  message(std::string(prompt).append("? (y/n) [n] "));

  VKey::flush();

  int key = VKey::get();

  if (key == 'y' || key == 'Y')
    return true;

  message_ = false;
  draw();

  return false;
}

// display help page
bool Query::help()
{
  Mode oldMode = mode_;

  mode_ = Mode::HELP;

  Screen::clear();
  redraw();

  bool ctrl_q = false;
  bool ctrl_o = false;

  while (true)
  {
    int key;

#ifdef OS_WIN

    while (true)
    {
      key = VKey::in(100);

      if (key > 0)
        break;

      // detect screen size changes
      int rows = Screen::rows;
      int cols = Screen::cols;

      Screen::getsize();

      if (rows != Screen::rows || cols != Screen::cols)
        redraw();
    }

#else

    key = VKey::get();

#endif

    if (ctrl_o)
    {
      meta(key);
      redraw();
      ctrl_o = false;
    }
    else if (key == VKey::CTRL_Q)
    {
      ctrl_q = true;
      break;
    }
    else if (key == VKey::ESC || key == VKey::CTRL_Z || key == VKey::FN(1))
    {
      break;
    }
    else
    {
      switch (key)
      {
        case VKey::CTRL_L:
          redraw();
          break;

        case VKey::CTRL_C:
          if (confirm("Exit"))
            return true;
          redraw();
          break;

        case VKey::CTRL_O:
          ctrl_o = true;
          break;

        case VKey::CTRL_T:
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::CTRL_BS:
#ifdef OS_WIN
          GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
#else
          raise(SIGTERM);
#endif
          break;

        case VKey::META:
          meta(VKey::get());
          redraw();
          break;

        default:
          Screen::alert();
#ifdef WITH_MACOS_META_KEY
          if (key >= 0x80)
          {
            if (!Screen::mono)
              Screen::put(CERROR);
            Screen::put(1, 0, "MacOS Terminal Preferences/Profiles/Keyboard: enable \"Use Option as Meta key\"");
            Screen::setpos(0, start_ + col_ - offset_);
          }
#endif
      }
    }
  }

  mode_ = oldMode;

  Screen::clear();
  redraw();

  return ctrl_q;
}

// Alt/Meta/Option key
void Query::meta(int key)
{
  if (key == 'E' || key == 'Q')
  {
    if (flags_[5].flag || flags_[6].flag || flags_[17].flag || flags_[30].flag)
    {
      // reset -F, -G, -P, -Z to switch back to -E (the Q> prompt)
      flags_[5].flag = false;
      flags_[6].flag = false;
      flags_[17].flag = false;
      flags_[30].flag = false;

      set_prompt();

      draw();

      search();
    }

    return;
  }

  for (Flags *fp = flags_; fp->text != NULL; ++fp)
  {
    if (fp->key == key)
    {
      if (!fp->flag)
      {
        switch (key)
        {
          case 'A':
            flags_[1].flag = false;
            flags_[3].flag = false;
            flags_[16].flag = false;
            flags_[29].flag = false;
            break;

          case 'B':
            flags_[0].flag = false;
            flags_[3].flag = false;
            flags_[16].flag = false;
            flags_[29].flag = false;
            break;

          case 'b':
          case 'k':
          case 'n':
            flags_[4].flag = false;
            flags_[14].flag = false;
            break;

          case 'C':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[16].flag = false;
            flags_[29].flag = false;
            break;

          case 'c':
            flags_[2].flag = false;
            flags_[13].flag = false;
            flags_[14].flag = false;
            flags_[15].flag = false;
            break;

          case 'F':
            flags_[6].flag = false;
            flags_[17].flag = false;
            flags_[30].flag = false;
            break;

          case 'G':
            flags_[5].flag = false;
            flags_[17].flag = false;
            flags_[30].flag = false;
            break;

          case 'H':
            flags_[9].flag = false;
            break;

          case 'h':
            flags_[8].flag = false;
            break;

          case 'I':
            flags_[24].flag = false;
            flags_[26].flag = false;
            break;

          case 'i':
            flags_[12].flag = false;
            break;

          case 'j':
            flags_[11].flag = false;
            break;

          case 'l':
            flags_[2].flag = false;
            flags_[4].flag = false;
            flags_[13].flag = false;
            flags_[15].flag = false;
            break;

          case 'o':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[3].flag = false;
            flags_[29].flag = false;
            break;

          case 'P':
            flags_[5].flag = false;
            flags_[6].flag = false;
            flags_[30].flag = false;
            break;

          case 'R':
            flags_[19].flag = false;
            for (int i = 33; i <= 41; ++i)
              flags_[i].flag = false;
            break;

          case 'r':
            flags_[18].flag = false;
            for (int i = 33; i <= 41; ++i)
              flags_[i].flag = false;
            break;

          case 'W':
            flags_[10].flag = false;
            flags_[26].flag = false;
            break;

          case 'w':
            flags_[26].flag = false;
            break;

          case 'X':
            flags_[10].flag = false;
            flags_[24].flag = false;
            break;

          case 'x':
            flags_[25].flag = false;
            break;

          case 'y':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[3].flag = false;
            flags_[16].flag = false;
            break;

          case 'Z':
            flags_[5].flag = false;
            flags_[6].flag = false;
            flags_[17].flag = false;
            break;

          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
          case '6':
          case '7':
          case '8':
          case '9':
            for (int i = 33; i <= 41; ++i)
              flags_[i].flag = false;
            if (!flags_[18].flag && !flags_[19].flag)
              flags_[18].flag = true;
            break;

          case '~':
            flags_[45].flag = false;
            flags_[46].flag = false;
            flags_[47].flag = false;
            break;

          case '#':
            flags_[44].flag = false;
            flags_[46].flag = false;
            flags_[47].flag = false;
            break;

          case '%':
            flags_[44].flag = false;
            flags_[45].flag = false;
            flags_[47].flag = false;
            break;

          case '@':
            flags_[44].flag = false;
            flags_[45].flag = false;
            flags_[46].flag = false;
            break;

        }
      }
      else
      {
        switch (key)
        {
          case 'R':
          case 'r':
            for (int i = 33; i <= 41; ++i)
              flags_[i].flag = false;
            break;
        }
      }

      if (key == 'g')
      {
        if (!globbing_)
        {
          globbing_ = true;

          memcpy(temp_, line_, QUERY_MAX_LEN);
          size_t num = globs_.size();
          if (num >= QUERY_MAX_LEN)
            num = QUERY_MAX_LEN - 1;
          memcpy(line_, globs_.c_str(), num);
          line_[num] = '\0';

          len_ = line_len();
          move(len_);

          set_prompt();
        }
        else
        {
          globbing_ = false;

          memcpy(line_, temp_, QUERY_MAX_LEN);
          len_ = line_len();
          move(len_);

          set_prompt();
        }

        if (mode_ == Mode::QUERY)
        {
          draw();
        }
        else
        {
          message("\033[7mM-g\033[m GLOBS should be entered in the query view screen, \033[7mESC\033[m to go back\033[m");
        }
      }
#if !defined(HAVE_PCRE2) && !defined(HAVE_BOOST_REGEX)
      else if (key == 'P')
      {
        message("option -P is not available in this build configuration of ugrep");
      }
#endif
#ifndef HAVE_LIBZ
      else if (key == 'z')
      {
        message("Option -z is not available in this build configuration of ugrep");
      }
#endif
      else
      {
        std::string msg;

        fp->flag = !fp->flag;

        msg.assign("\033[7mM- \033[m ").append(fp->text).append(fp->flag ? " \033[32;1mon\033[m" : " \033[31;1moff\033[m");
        msg[6] = fp->key;

        search();

        message(msg);

        set_prompt();
      }

      return;
    }
  }

  Screen::alert();
}

// true if at least one line is selected
bool Query::selections()
{
  if (select_all_ && rows_ > 0)
    return true;

  for (int i = 0; i < rows_; ++i)
    if (selected_[i])
      return true;

  return false;
}

// save selected lines -- unused
void Query::save()
{
  if (saved_.empty() || confirm("Overwrite saved selections"))
  {
    saved_.clear();

    try
    {
      if (select_all_)
      {
        message("please wait a moment to complete the search...");
        fetch_all();
      }

      for (int i = 0; i < rows_; ++i)
        if (selected_[i])
          saved_.emplace_back(view_[i]);
    }

    catch (std::bad_alloc&)
    {
      message("out of memory");
    }

    message(std::to_string(saved_.size()).append(" lines saved"));
  }
  else
  {
    for (int i = 0; i < rows_; ++i)
      selected_[i] = false;
    select_all_ = false;
  }
}

// print query results when done
void Query::print()
{
  if (!saved_.empty())
  {
    for (auto& line : saved_)
      print(line);
  }
  else
  {
    int i = 0;

    // output selected query results
    while (i < rows_)
    {
      if (selected_[i])
        if (!print(view_[i]))
          return;
      ++i;
    }

    // if all lines are selected, output the remaining lines that aren't in view
    if (select_all_ && (!eof_ || buflen_ > 0))
    {
      // reading the search pipe should block until data is received
#ifdef OS_WIN
      blocking_ = true;
      pending_ = false;
#else
      set_blocking(search_pipe_[0]);
#endif

      while (true)
      {
        // if not appending to a row, start over at the begin of the view to conserve memory
        if (!append_)
          rows_ = 0;

        int i = rows_;

        fetch(i);

        if (rows_ <= i)
          break;

        while (i < rows_)
        {
          if (!print(view_[i]))
            return;
          ++i;
        }
      }
    }
  }
}

// print one row of query results
bool Query::print(const std::string& line)
{
  if (line.empty())
    return true;

  const char *text = line.c_str();
  const char *end = text + line.size();

  // how many nulls to ignore, part of filename marking?
  int nulls = *text == '\0' && !flag_text ? 2 : 0;

  if (nulls > 0)
    ++text;

  const char *ptr = text;

  // if output should not be colored or colors are turned off with CTRL-T, then output the selected line without its CSI sequences
  if (flag_apply_color == NULL || Screen::mono)
  {
    while (ptr < end)
    {
      if (*ptr == '\0' && nulls > 0)
      {
        size_t nwritten = fwrite(text, 1, ptr - text, stdout);

        if (text + nwritten < ptr)
          return false;

        --nulls;

        text = ++ptr;
      }
      else if (*ptr == '\033')
      {
        size_t nwritten = fwrite(text, 1, ptr - text, stdout);

        if (text + nwritten < ptr)
          return false;

        ++ptr;

        if (*ptr == '[')
        {
          ++ptr;
          while (ptr < end && !isalpha(*ptr))
            ++ptr;
        }

        if (ptr < end)
          ++ptr;

        text = ptr;
      }
      else
      {
        ++ptr;
      }
    }

    size_t nwritten = fwrite(text, 1, ptr - text, stdout);

    if (text + nwritten < ptr)
      return false;
  }
  else if (nulls > 0)
  {
    while (ptr < end && nulls > 0)
    {
      if (*ptr == '\0')
      {
        size_t nwritten = fwrite(text, 1, ptr - text, stdout);

        if (text + nwritten < ptr)
          return false;

        --nulls;

        text = ++ptr;
      }
      else
      {
        ++ptr;
      }
    }

    size_t nwritten = fwrite(text, 1, end - text, stdout);

    if (text + nwritten < end)
      return false;
  }
  else
  {
    size_t nwritten = fwrite(line.c_str(), 1, line.size(), stdout);

    if (nwritten < line.size())
      return false;
  }

  if (fwrite("\n", 1, 1, stdout) < 1)
    return false;

  return true;
}

// initialze local flags with the global flags
void Query::get_flags()
{
  // remember the context size, when specified
  if (flag_after_context > 0)
    context_ = flag_after_context;
  else if (flag_before_context > 0)
    context_ = flag_before_context;

  // remember the fuzzy max, when specified
  if (flag_fuzzy > 0)
    fuzzy_ = flag_fuzzy;

  // get the -g, -O, and -t globs
  for (auto& glob : flag_glob)
  {
    if (!glob.empty())
    {
      if (!globs_.empty())
        globs_.push_back(',');
      globs_.append(glob);
    }
  }

  // get the interactive flags from the ugrep flags
  flags_[0].flag = flag_after_context > 0 && flag_before_context == 0;
  flags_[1].flag = flag_after_context == 0 && flag_before_context > 0;
  flags_[2].flag = flag_byte_offset;
  flags_[3].flag = flag_after_context > 0 && flag_before_context > 0;
  flags_[4].flag = flag_count;
  flags_[5].flag = flag_fixed_strings;
  flags_[6].flag = flag_basic_regexp;
  flags_[7].flag = !globs_.empty();
  flags_[8].flag = flag_with_filename;
  flags_[9].flag = flag_no_filename;
  flags_[10].flag = flag_binary_without_match;
  flags_[11].flag = flag_ignore_case;
  flags_[12].flag = flag_smart_case;
  flags_[13].flag = flag_column_number;
  flags_[14].flag = flag_files_with_matches;
  flags_[15].flag = flag_line_number;
  flags_[16].flag = flag_only_matching;
  flags_[17].flag = flag_perl_regexp;
  flags_[18].flag = flag_directories_action == Action::RECURSE && flag_dereference;
  flags_[19].flag = flag_directories_action == Action::RECURSE && !flag_dereference;
  flags_[20].flag = flag_initial_tab;
  flags_[21].flag = flag_binary;
  flags_[22].flag = flag_ungroup;
  flags_[23].flag = flag_invert_match;
  flags_[24].flag = flag_with_hex;
  flags_[25].flag = flag_word_regexp;
  flags_[26].flag = flag_hex;
  flags_[27].flag = flag_line_regexp;
  flags_[28].flag = flag_empty;
  flags_[29].flag = flag_any_line;
  flags_[30].flag = flag_fuzzy > 0;
  flags_[31].flag = flag_decompress;
  flags_[32].flag = flag_null;
  flags_[33].flag = flag_max_depth == 1;
  flags_[34].flag = flag_max_depth == 2;
  flags_[35].flag = flag_max_depth == 3;
  flags_[36].flag = flag_max_depth == 4;
  flags_[37].flag = flag_max_depth == 5;
  flags_[38].flag = flag_max_depth == 6;
  flags_[39].flag = flag_max_depth == 7;
  flags_[40].flag = flag_max_depth == 8;
  flags_[41].flag = flag_max_depth == 9;
  flags_[42].flag = flag_hidden;
  flags_[43].flag = flag_heading;
  flags_[44].flag = flag_sort && (strcmp(flag_sort, "best") == 0 || strcmp(flag_sort, "rbest") == 0);
  flags_[45].flag = flag_sort && (strcmp(flag_sort, "size") == 0 || strcmp(flag_sort, "rsize") == 0);
  flags_[46].flag = flag_sort && (strcmp(flag_sort, "changed") == 0 || strcmp(flag_sort, "changed") == 0);
  flags_[47].flag = flag_sort && (strcmp(flag_sort, "created") == 0 || strcmp(flag_sort, "created") == 0);
  flags_[48].flag = flag_sort && *flag_sort == 'r';
}

// set the global flags to the local flags
void Query::set_flags()
{
  // reset flags that are set by ugrep() depending on other flags
  flag_no_header = false;
  flag_dereference = false;
  flag_no_dereference = false;
  flag_files_without_match = false;
  flag_match = false;
  flag_binary_files = NULL;
  flag_break = false;

  // suppress warning messages
  flag_no_messages = true;

  // set ugrep flags to the interactive flags
  flag_after_context = context_ * (flags_[0].flag || flags_[3].flag);
  flag_before_context = context_ * (flags_[1].flag || flags_[3].flag);
  flag_byte_offset = flags_[2].flag;
  flag_count = flags_[4].flag;
  flag_fixed_strings = flags_[5].flag;
  flag_basic_regexp = flags_[6].flag;
  flag_glob.clear();
  if (globbing_)
    globs_.assign(line_);
  flags_[7].flag = !globs_.empty();
  if (flags_[7].flag)
    flag_glob.emplace_back(globs_);
  flag_with_filename = flags_[8].flag;
  flag_no_filename = flags_[9].flag;
  flag_binary_without_match = flags_[10].flag;
  flag_ignore_case = flags_[11].flag;
  flag_smart_case = flags_[12].flag;
  flag_column_number = flags_[13].flag;
  flag_files_with_matches = flags_[14].flag;
  flag_line_number = flags_[15].flag;
  flag_only_matching = flags_[16].flag;
  flag_perl_regexp = flags_[17].flag;
  if (flags_[18].flag)
    flag_directories_action = Action::RECURSE, flag_dereference = true;
  else if (flags_[19].flag)
    flag_directories_action = Action::RECURSE, flag_dereference = false;
  else
    flag_directories_action = Action::SKIP;
  flag_initial_tab = flags_[20].flag;
  flag_binary = flags_[21].flag;
  flag_ungroup = flags_[22].flag;
  flag_invert_match = flags_[23].flag;
  flag_with_hex = flags_[24].flag;
  flag_word_regexp = flags_[25].flag;
  flag_hex = flags_[26].flag;
  flag_line_regexp = flags_[27].flag;
  flag_empty = flags_[28].flag;
  flag_any_line = flags_[29].flag;
  flag_fuzzy = flags_[30].flag ? fuzzy_ : 0;
  flag_decompress = flags_[31].flag;
  flag_null = flags_[32].flag;
  flag_max_depth = 0;
  for (size_t i = 33; i <= 41; ++i)
    if (flags_[i].flag)
      flag_max_depth = i - 32;
  flag_hidden = flags_[42].flag;
  flag_heading = flags_[43].flag;
  if (flags_[44].flag)
    flag_sort = flags_[48].flag ? "rbest" : "best";
  else if (flags_[45].flag)
    flag_sort = flags_[48].flag ? "rsize" : "size";
  else if (flags_[46].flag)
    flag_sort = flags_[48].flag ? "rchanged" : "changed";
  else if (flags_[47].flag)
    flag_sort = flags_[48].flag ? "rcreated" : "created";
  else
    flag_sort = flags_[48].flag ? "rname" : "name";
}

void Query::set_prompt()
{
  if (globbing_)
    prompt_ = "--glob=";
  else if (flags_[5].flag)
    prompt_ = "F>";
  else if (flags_[6].flag)
    prompt_ = "G>";
  else if (flags_[17].flag)
    prompt_ = "P>";
  else if (flags_[30].flag)
    prompt_ = "Z>";
  else
    prompt_ = "Q>";
}

void Query::get_stdin()
{
  // if standard input is searched, buffer all data
  if (flag_stdin)
  {
    reflex::BufferedInput input(stdin, flag_encoding_type);

    while (true)
    {
      size_t len = input.get(buffer_, QUERY_BUFFER_SIZE);
      if (len <= 0)
        break;
      stdin_buffer_.append(buffer_, len);
    }
  }
}

void Query::set_stdin()
{
  // if standard input is searched, start thread to produce data
  if (flag_stdin)
  {
    // close the stdin pipe
    if (source != stdin && source != NULL)
    {
      fclose(source);
      source = NULL;
    }

    if (stdin_thread_.joinable())
      stdin_thread_.join();

    if (pipe(stdin_pipe_) < 0)
    {
      if (!Screen::mono)
        Screen::put(CERROR);
      Screen::put(0, 0, "Error: cannot create pipe");
      return;
    }

    source = fdopen(stdin_pipe_[0], "r");

    stdin_thread_ = std::thread(Query::stdin_sender, stdin_pipe_[1]);
  }
}

// send standard input data down the specified pipe fd
ssize_t Query::stdin_sender(int fd)
{
  // write the stdin data all at once, we can ignore the return value
  ssize_t nwritten = write(fd, stdin_buffer_.c_str(), stdin_buffer_.size());

  close(fd);

  return nwritten;
}

// true if line starts with a valid filename/filepath identified by three \0 markers
bool Query::is_filename(const std::string& line, std::string& filename, bool compare_dir)
{
  size_t start = 0;
  size_t pos = 0;
  size_t end = line.size();

  if (flag_files_with_matches || flag_count)
  {
    while (pos < end)
    {
      unsigned char c = line.at(pos);

      if (c != '\033')
        break;

      while (++pos < end && !isalpha(line.at(pos)))
        continue;

      ++pos;
    }

    if (pos >= end)
      return false;

    start = pos;

    while (pos < end && line.at(pos) != '\033')
      ++pos;
  }
  else
  {
    if (end < 4 || line.front() != '\0')
      return false;

    pos = 1;

    while (pos < end && line.at(pos) != '\0')
      ++pos;

    if (++pos >= end)
      return false;

    start = pos;

    while (pos < end && line.at(pos) != '\0')
      ++pos;

    if (pos == start || pos >= end)
      return false;
  }

  if (compare_dir)
  {
    size_t skip = 0;
#ifdef OS_WIN
    if (filename.size() >= 3 && filename.at(1) == ':' && filename.at(2) == PATHSEPCHR)
      skip = 3;
#endif
    size_t pos1 = line.find(PATHSEPCHR, start + skip);
    size_t pos2 = filename.find(PATHSEPCHR, skip);
    if (pos1 != std::string::npos)
      pos1 -= start;
    if (pos1 == pos2 && (pos1 == std::string::npos || line.compare(start, skip + pos1, filename, 0, skip + pos2) == 0))
      return false; // the extracted filename is the same or in the same directory as the previous
  }
  else if (line.compare(start, pos - start, filename) == 0)
  {
    return false; // the extracted filename is the same as the previous
  }

  filename = line.substr(start, pos - start);

  return true;
}

Query::Mode              Query::mode_                = Query::Mode::QUERY;
bool                     Query::updated_             = false;
bool                     Query::message_             = false;
char                     Query::line_[QUERY_MAX_LEN] = { '\0' };
char                     Query::temp_[QUERY_MAX_LEN] = { '\0' };
const char              *Query::prompt_              = NULL;
int                      Query::start_               = 0;
int                      Query::col_                 = 0;
int                      Query::len_                 = 0;
int                      Query::offset_              = 0;
int                      Query::shift_               = 8;
std::atomic_int          Query::error_;
std::string              Query::what_;
int                      Query::row_                 = 0;
int                      Query::rows_                = 0;
int                      Query::mark_                = 0;
int                      Query::select_              = -1;
bool                     Query::select_all_          = false;
bool                     Query::globbing_            = false;
std::string              Query::globs_;
std::string              Query::dirs_;
std::string              Query::wdir_;
bool                     Query::deselect_file_;
std::string              Query::selected_file_;
int                      Query::skip_                = 0;
std::vector<std::string> Query::view_;
std::list<std::string>   Query::saved_;
std::vector<bool>        Query::selected_;
bool                     Query::eof_                 = true;
bool                     Query::append_              = false;
size_t                   Query::buflen_              = 0;
char                     Query::buffer_[QUERY_BUFFER_SIZE];
int                      Query::search_pipe_[2];
std::thread              Query::search_thread_;
std::string              Query::stdin_buffer_;
int                      Query::stdin_pipe_[2];
std::thread              Query::stdin_thread_;
char                     Query::searching_[16]       = "Searching...";
int                      Query::dots_                = 3;
size_t                   Query::context_             = 2;
size_t                   Query::fuzzy_               = 1;

#ifdef OS_WIN

HANDLE                   Query::hPipe_;
OVERLAPPED               Query::overlapped_;
bool                     Query::blocking_;
bool                     Query::pending_;

#endif

Query::Flags Query::flags_[] = {
  { false, 'A', "after context" },
  { false, 'B', "before context" },
  { false, 'b', "byte offset" },
  { false, 'C', "context" },
  { false, 'c', "count lines" },
  { false, 'F', "fixed strings" },
  { false, 'G', "basic regex" },
  { false, 'g', "apply globs" },
  { false, 'H', "with filename" },
  { false, 'h', "hide filename" },
  { false, 'I', "ignore binary" },
  { false, 'i', "ignore case" },
  { false, 'j', "smart case" },
  { false, 'k', "column number" },
  { false, 'l', "list files" },
  { false, 'n', "line number" },
  { false, 'o', "only matching" },
  { false, 'P', "perl regex" },
  { false, 'R', "recurse symlinks" },
  { false, 'r', "recurse" },
  { false, 'T', "initial tab" },
  { false, 'U', "binary pattern" },
  { false, 'u', "ungroup matches" },
  { false, 'v', "invert matches" },
  { false, 'W', "with hex binary" },
  { false, 'w', "word match" },
  { false, 'X', "hex binary" },
  { false, 'x', "line match" },
  { false, 'Y', "empty matches" },
  { false, 'y', "any line" },
  { false, 'Z', "fuzzy matching" },
  { false, 'z', "decompress" },
  { false, '0', "file name + \\0" },
  { false, '1', "recurse 1 level" },
  { false, '2', "recurse 2 levels" },
  { false, '3', "recurse 3 levels" },
  { false, '4', "recurse 4 levels" },
  { false, '5', "recurse 5 levels" },
  { false, '6', "recurse 6 levels" },
  { false, '7', "recurse 7 levels" },
  { false, '8', "recurse 8 levels" },
  { false, '9', "recurse 9 levels" },
  { false, '.', "include hidden" },
  { false, '+', "show heading" },
  { false, '~', "sort by best" },
  { false, '#', "sort by size" },
  { false, '$', "sort by changed" },
  { false, '@', "sort by created" },
  { false, '^', "reverse sort" },
  { false, 0, NULL, }
};
