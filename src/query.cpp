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
#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <fcntl.h>

#ifdef OS_WIN

// non-blocking pipe (requires Windows named pipe)
inline HANDLE nonblocking_pipe(int fd[2])
{
  DWORD pid = GetCurrentProcessId();
  std::string pipe_name = "\\\\.\\pipe\\ugrep_";
  pipe_name.append(std::to_string(pid)).append("_").append(std::to_string(time(NULL)));
  DWORD buffer_size = QUERY_BUFFER_SIZE;
  HANDLE pipe_r = CreateNamedPipeA(pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, buffer_size, buffer_size, 0, NULL);
  if (pipe_r != INVALID_HANDLE_VALUE)
  {
    HANDLE pipe_w = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL); 
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

#endif

// global flags
bool flag_c = false;
bool flag_h = false;
bool flag_l = false;
bool flag_v = false;

static constexpr const char *PROMPT = "\033[32;1m";    // bright green
static constexpr const char *CERROR = "\033[37;41;1m"; // bright white on red
static constexpr const char *LARROW = "«";             // left arrow
static constexpr const char *RARROW = "»";             // right arrow

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

char *Query::line_ptr(int col, int pos)
{
  char *ptr = line_ptr(col);
  while (--pos >= 0 && *ptr != '\0')
    Screen::mbchar_width(ptr, const_cast<const char**>(&ptr));
  return ptr;
}

char *Query::line_end()
{
  char *ptr = line_;
  while (*ptr != '\0')
    ++ptr;
  return ptr;
}

int Query::line_pos()
{
  char *ptr = line_;
  char *end = line_ptr(col_);
  int pos = 0;
  while (ptr < end && *ptr != '\0')
  {
    Screen::mbchar_width(ptr, const_cast<const char**>(&ptr));
    ++pos;
  }
  return pos;
}

int Query::line_len()
{
  int num = 0;
  for (char *ptr = line_; *ptr != '\0'; ++ptr)
    num += Screen::mbchar_width(ptr, NULL);
  return num;
}

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

void Query::draw()
{
  if (select_ == -1)
  {
    Screen::home();

    if (prompt_ != NULL)
    {
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
    Screen::put(0, 0, "\033[7mEnter\033[m/\033[7mDel\033[m toggle selection  \033[7mA\033[m select all  \033[7mC\033[m clear all  \033[7mEsc\033[m exit  \033[7m^Q\033[m quick exit");
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

void Query::redraw()
{
  Screen::getsize();
  shift_ = (Screen::cols - start_) / 10;
  Screen::normal();

  if (mode_ == Mode::QUERY)
  {
    if (row_ + Screen::rows - 1 > rows_)
      row_ = rows_ - Screen::rows + 1;
    if (row_ < 0)
      row_ = 0;
    int end = rows_;
    if (end > row_ + Screen::rows - 1)
      end = row_ + Screen::rows - 1;
    for (int i = row_; i < end; ++i)
      view(i);
    if (rows_ < row_ + Screen::rows - 1)
      Screen::end();
    draw();
  }
  else
  {
    Screen::put( 1, 0, "");
    Screen::put( 2, 0, "\033[7mEsc\033[m   exit & save selected");
    Screen::put( 3, 0, "\033[7mEnter\033[m selection mode");
    Screen::put( 4, 0, "");
    Screen::put( 5, 0, "\033[7mTab\033[m    \033[7mS-Tab\033[m   pan");
    Screen::put( 6, 0, "\033[7mUp\033[m     \033[7mDown\033[m    scroll");
    Screen::put( 7, 0, "\033[7mPgUp\033[m   \033[7mPgDn\033[m    scroll page");
#ifdef WITH_MACOS_META_KEY
    Screen::put( 8, 0, "\033[7mS-Left\033[m \033[7mS-Right\033[m pan ½ page");
    Screen::put( 9, 0, "\033[7mS-Up\033[m   \033[7mS-Down\033[m  scroll ½ pg");
#else
    Screen::put( 8, 0, "\033[7mM-Left\033[m \033[7mM-Right\033[m pan ½ page");
    Screen::put( 9, 0, "\033[7mM-Up\033[m   \033[7mM-Down\033[m  scroll ½ pg");
#endif
    Screen::put(10, 0, "");
    Screen::put(11, 0, "\033[7mHome\033[m \033[7mEnd\033[m begin/end of line");
    Screen::put(12, 0, "");
    Screen::put(13, 0, "\033[7m^K\033[m delete after cursor");
    Screen::put(14, 0, "\033[7m^L\033[m refresh screen");
    Screen::put(15, 0, "\033[7m^Q\033[m quick exit & save");
    Screen::put(16, 0, "\033[7m^T\033[m toggle colors on/off");
    Screen::put(17, 0, "\033[7m^U\033[m delete before cursor");
    Screen::put(18, 0, "\033[7m^V\033[m verbatim character");
    Screen::put(19, 0, "\033[7m^Z\033[m or \033[7mF1\033[m help");
    Screen::put(20, 0, "");
    Screen::put(21, 0, "\033[7mM-/xxxx/\033[m U+xxxx code point");
    Screen::put(22, 0, "");

    std::string buf;
    int row = 23;
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
    else
    {
      message_ = false;
    }

    Screen::put(0, Screen::cols - 1, "?");
  }
}

void Query::sigwinch(int)
{
  redraw();
}

void Query::sigint(int)
{
  VKey::cleanup();
  Screen::cleanup();

#ifndef OS_WIN
  // reset SIGINT to the default handler
  signal(SIGINT, SIG_DFL);

  // signal SIGINT again
  kill(getpid(), SIGINT);
#endif
}

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

void Query::insert(int ch)
{
  char buf = static_cast<char>(ch);
  insert(&buf, 1);
}

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

void Query::query()
{
  if (!VKey::setup(VKey::RAW))
    abort("no keyboard detected");

  if (!Screen::setup("ugrep --query"))
  {
    VKey::cleanup();
    abort("no ANSI terminal screen detected");
  }

  for (Flags *fp = flags_; fp->text != NULL; ++fp)
    VKey::map_alt_key(fp->key, NULL);

  get_flags();

  get_stdin();

  query("Q>");

  VKey::cleanup();
  Screen::cleanup();

  // check TTY again for color support, this time without --query
  flag_query = 0;
  terminal();

  // if all lines selected, make sure to fetch all data to output
  if (select_all_)
    while (!eof_ || buflen_ > 0)
      fetch(rows_);

  // close the search pipe to terminate the search threads, if still open
  if (!eof_)
  {
    close(search_pipe_[0]);
    eof_ = true;
  }

  // close the stdin pipe
  if (flag_stdin && source != stdin && source != NULL)
  {
    fclose(source);
    source = NULL;
  }

  if (!flag_quiet)
  {
    // output selected query results
    for (int i = 0; i < rows_; ++i)
    {
      if (selected_[i])
      {
        // if output should not be colored or colors are turned off with CTRL-T, then output the selected line without its CSI sequences
        if (flag_apply_color == NULL || Screen::mono)
        {
          const char *text = view_[i].c_str();
          const char *ptr = text;
          const char *end = text + view_[i].size();

          while (ptr < end)
          {
            if (*ptr == '\033')
            {
              fwrite(text, 1, ptr - text, stdout);
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

          fwrite(text, 1, ptr - text, stdout);
        }
        else
        {
          fwrite(view_[i].c_str(), 1, view_[i].size(), stdout);
        }

        putchar('\n');
      }
    }
  }

  // join the search thread
  if (search_thread_.joinable())
    search_thread_.join();

  // join the stdin sender thread
  if (stdin_thread_.joinable())
    stdin_thread_.join();
}

void Query::query(const char *prompt)
{
  mode_       = Mode::QUERY;
  updated_    = false;
  message_    = false;
  *line_      = '\0';
  prompt_     = prompt;
  start_      = 0;
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
  eof_        = true;
  buflen_     = 0;

#ifndef OS_WIN

  signal(SIGINT, sigint);

  signal(SIGPIPE, SIG_IGN);

  signal(SIGWINCH, sigwinch);

#endif

  arg_pattern = line_;

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
      pattern.assign("(");
      for (auto& regex : flag_regexp)
      {
        if (!regex.empty())
        {
          if (!pattern.empty())
            pattern.append(")|(");
          pattern.append(regex);
        }
      }
      pattern.push_back(')');
    }

    flag_regexp.clear();

    size_t num = pattern.size();
    if (num >= QUERY_MAX_LEN)
      num = QUERY_MAX_LEN - 1;

    memcpy(line_, pattern.c_str(), num);
    line_[num] = '\0';

    len_ = line_len();

    move(len_);
  }

  Screen::clear();

  if (prompt_ != NULL)
  {
    start_ = 2;
    Screen::put(prompt_);
    Screen::getpos(NULL, &start_);
  }
  else
  {
    start_ = 0;
  }

  result();

  bool ctrl_o = false;
  bool ctrl_v = false;

  while (true)
  {
    size_t delay = flag_query;

    int key = 0;

    while (true)
    {
      update();

      if (select_ == -1)
        Screen::setpos(0, start_ + col_ - offset_);
      else
        Screen::setpos(select_ - row_ + 1, 0);

      key = VKey::in(100);

      if (key > 0)
        break;

      --delay;

      if (delay == 0)
      {
        if (message_)
        {
          draw();
          message_ = false;
        }

        if (updated_)
        {
          result();

          updated_ = false;
          select_ = -1;
          select_all_ = false;
        }
#ifdef OS_WIN
        else
        {
          // detect screen size changes
          int rows = Screen::rows;
          int cols = Screen::cols;

          Screen::getsize();

          if (rows != Screen::rows || cols != Screen::cols)
            redraw();
        }
#endif

        delay = flag_query;
      }
    }

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
          if (select_ == -1)
          {
            if (!Screen::mono)
              Screen::put(PROMPT);
            Screen::put(0, 0, ">>");
            Screen::put(0, 2, "\033[mExit? (y/n) [n] ");
            VKey::flush();
            key = VKey::get();
            if (key == 'y' || key == 'Y')
              return;
            draw();
          }
          else
          {
            select_ = -1;
            redraw();
          }
          break;

        case VKey::LF:
        case VKey::CR:
          if (select_ == -1)
          {
            if (rows_ > 0)
            {
              select_ = row_;
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
          break;

        case VKey::META:
          key = VKey::get();
          switch (key)
          {
            case VKey::TAB:
              if (skip_ > 7)
                skip_ -= 8;
              redraw();
              break;

            case VKey::UP:
              pgup(true);
              break;

            case VKey::DOWN:
              pgdn(true);
              break;

            case VKey::LEFT:
              skip_ -= Screen::cols / 2;
              if (skip_ < 0)
                skip_ = 0;
              redraw();
              break;

            case VKey::RIGHT:
              skip_ += Screen::cols / 2;
              redraw();
              break;

            default:
              if (select_ == -1)
                meta(key);
              else
                Screen::alert();
          }
          break;

        case VKey::TAB:
          skip_ += 8;
          redraw();
          break;

        case VKey::BS:
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
          break;

        case VKey::DEL:
          if (select_ == -1)
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

        case VKey::RIGHT:
          if (select_ == -1)
            move(col_ + 1);
          else
            Screen::alert();
          break;

        case VKey::LEFT:
          if (select_ == -1)
            move(col_ - 1);
          else
            Screen::alert();
          break;

        case VKey::UP:
          up();
          break;

        case VKey::DOWN:
          down();
          break;

        case VKey::PGUP:
          pgup();
          break;

        case VKey::PGDN:
          pgdn();
          break;

        case VKey::HOME:
          if (select_ == -1)
            move(0);
          else
            Screen::alert();
          break;

        case VKey::END:
          if (select_ == -1)
            move(len_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_K:
          if (select_ == -1)
            erase(len_ - col_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_L:
          redraw();
          break;

        case VKey::CTRL_O:
          if (select_ == -1)
            ctrl_o = true;
          else
            Screen::alert();
          break;

        case VKey::CTRL_Q:
          return;

        case VKey::CTRL_T:
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::CTRL_U:
          if (select_ == -1)
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

        case VKey::CTRL_V:
          if (select_ == -1)
            ctrl_v = true;
          break;

        case VKey::CTRL_Z:
          help();
          break;

        default:
          if (key >= 32 && key < 256)
          {
            if (select_ == -1)
            {
              insert(key);
            }
            else if (key == 'A' || key == 'a')
            {
              select_all_ = true;
              for (int i = 0; i < rows_; ++i)
                selected_[i] = true;
              redraw();
            }
            else if (key == 'C' || key == 'c')
            {
              select_all_ = false;
              for (int i = 0; i < rows_; ++i)
                selected_[i] = false;
              redraw();
            }
            else
            {
              Screen::alert();
            }
          }
          else
          {
            help();
          }
      }
    }
  }
}

void Query::result()
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

  set_flags();

  set_stdin();

  if (error_ == -1)
    search_thread_ = std::thread(Query::execute, search_pipe_[1]);

  redraw();
}

void Query::update()
{
  int begin = rows_;

  // fetch viewable portion plus a screenful more, when available
  fetch(row_ + 2 * Screen::rows - 2);

  Screen::save();

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

    for (int i = begin; i < end; ++i)
      view(i);
  }

  if (rows_ < row_ + Screen::rows - 1)
  {
    searching_[9] = '.';
    searching_[10] = '.';
    searching_[11] = '.';
    searching_[9 + dots_] = '\0';
    dots_ = (dots_ + 1) & 3;

    Screen::setpos(rows_ - row_ + 1, 0);
    Screen::normal();
    Screen::invert();
    if (error_ == -1)
    {
      Screen::put(rows_ - row_ + 1, 0, eof_ ? "(END)" : searching_);
      Screen::normal();
      Screen::erase();
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
        Screen::end();
      }

      Screen::put(2, 0, what_);
      Screen::normal();
      Screen::end();

      draw();
    }
  }

  Screen::restore();
}

// fetch rows up to and including the specified row, when available (i.e. do not block)
void Query::fetch(int row)
{
  while (rows_ <= row)
  {
    // look for the first newline character in the buffer
    char *nlptr = static_cast<char*>(memchr(buffer_, '\n', buflen_));

    if (nlptr == NULL)
    {
      // no newline and buffer is not full and not EOF reached yet, get more data
      if (buflen_ < sizeof(buffer_) && !eof_)
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

              case ERROR_HANDLE_EOF:
              default:
                close(search_pipe_[0]);
                eof_ = true;
            }
          }
        }
        if (avail)
        {
          pending_ = false;

          if (!ReadFile(hPipe_, buffer_ + buflen_, sizeof(buffer_) - buflen_, &nread, &overlapped_))
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
            }
          }
        }

        buflen_ += nread;

#else

        // try to fetch more data from the non-blocking pipe when immediately available
        ssize_t nread = read(search_pipe_[0], buffer_ + buflen_, sizeof(buffer_) - buflen_);

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
          }
        }
        else
        {
          // no data, pipe is closed (EOF)
          close(search_pipe_[0]);
          eof_ = true;
        }

#endif
      }

      if (buflen_ == 0)
        break;

      // we may have more data now, so look again for the first newline character in the buffer
      nlptr = static_cast<char*>(memchr(buffer_, '\n', buflen_));

      if (nlptr == NULL)
      {
        // if no newline and EOF not reached yet, bail out
        if (!eof_)
          break;

        // data has no newline but buffer is full or EOF reached, so we must read it as an incomplete row
        nlptr = buffer_ + buflen_;
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

      // copy the row from the buffer
      view_[rows_].assign(buffer_, nlptr - buffer_);
      selected_[rows_] = select_all_;

      // added another row
      ++rows_;

      if (nlptr < buffer_ + buflen_)
        ++nlptr;

      buflen_ -= nlptr - buffer_;

      // update the buffer
      memmove(buffer_, nlptr, buflen_);
    }
  }
}

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
      error_ = error.pos();

      // subtract 4 for (?m) or (?mi)
      if (error_ >= 4 + flag_ignore_case)
        error_ -= 4 + flag_ignore_case;

      // subtract 2 for -F
      if (flag_fixed_strings && error_ >= 2)
        error_ -= 2;

      // subtract 2 or 3 for -x or -w
      if (flags_[26].flag && error_ >= 2)
        error_ -= 2;
      else if (flags_[24].flag && error_ >= 3)
        error_ -= 3;
    }

    catch (std::exception& error)
    {
      what_.assign(error.what());

      // cursor to the end
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

void Query::up()
{
  if (select_ > 0)
  {
    --select_;
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
    ++select_;
    if (select_ >= rows_)
      select_ = rows_ - 1;
    if (select_ < row_ + Screen::rows - 2)
      return;
  }
  if (row_ + Screen::rows - 1 <= rows_)
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
    if (half_page)
      select_ -= Screen::rows / 2;
    else
      select_ -= Screen::rows - 2;
    if (select_ < 0)
      select_ = 0;
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
    if (half_page)
      select_ += Screen::rows / 2;
    else
      select_ += Screen::rows - 2;
    if (select_ >= rows_)
      select_ = rows_ - 1;
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

void Query::help()
{
  mode_ = Mode::HELP;

  Screen::clear();
  redraw();

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
    else if (key == VKey::ESC || key == VKey::CTRL_Q)
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

        case VKey::CTRL_O:
          ctrl_o = true;
          break;

        case VKey::CTRL_T:
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::META:
          meta(VKey::get());
          redraw();
          break;

        default:
          if (key != VKey::FN(1))
          {
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
  }

  mode_ = Mode::QUERY;

  Screen::clear();
  redraw();
}

void Query::meta(int key)
{
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
            flags_[15].flag = false;
            flags_[28].flag = false;
            break;

          case 'B':
            flags_[0].flag = false;
            flags_[3].flag = false;
            flags_[15].flag = false;
            flags_[28].flag = false;
            break;

          case 'b':
          case 'k':
          case 'n':
            flags_[4].flag = false;
            flags_[13].flag = false;
            break;

          case 'C':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[15].flag = false;
            flags_[28].flag = false;
            break;

          case 'c':
            flags_[2].flag = false;
            flags_[12].flag = false;
            flags_[13].flag = false;
            flags_[14].flag = false;
            break;

          case 'H':
            flags_[8].flag = false;
            break;

          case 'h':
            flags_[7].flag = false;
            break;

          case 'I':
            flags_[23].flag = false;
            flags_[25].flag = false;
            break;

          case 'i':
            flags_[11].flag = false;
            break;

          case 'j':
            flags_[10].flag = false;
            break;

          case 'l':
            flags_[2].flag = false;
            flags_[4].flag = false;
            flags_[12].flag = false;
            flags_[14].flag = false;
            break;

          case 'o':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[3].flag = false;
            flags_[28].flag = false;
            break;

          case 'R':
            flags_[18].flag = false;
            for (int i = 31; i <= 39; ++i)
              flags_[i].flag = false;
            break;

          case 'r':
            flags_[17].flag = false;
            for (int i = 31; i <= 39; ++i)
              flags_[i].flag = false;
            break;

          case 'W':
            flags_[9].flag = false;
            flags_[25].flag = false;
            break;

          case 'w':
            flags_[26].flag = false;
            break;

          case 'X':
            flags_[9].flag = false;
            flags_[23].flag = false;
            break;

          case 'x':
            flags_[24].flag = false;
            break;

          case 'y':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[3].flag = false;
            flags_[15].flag = false;
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
            for (int i = 31; i <= 39; ++i)
              flags_[i].flag = false;
            if (!flags_[17].flag && !flags_[18].flag)
              flags_[17].flag = true;

          case '#':
            flags_[42].flag = false;
            flags_[43].flag = false;
            break;

          case '%':
            flags_[41].flag = false;
            flags_[43].flag = false;
            break;

          case '@':
            flags_[41].flag = false;
            flags_[42].flag = false;
            break;

        }
      }
      else
      {
        switch (key)
        {
          case 'R':
          case 'r':
            for (int i = 31; i <= 39; ++i)
              flags_[i].flag = false;
            break;
        }
      }

      std::string buf;

      Screen::normal();

#if !defined(HAVE_PCRE2) && !defined(HAVE_BOOST_REGEX)
      if (key == 'P')
      {
        buf.assign(CERROR).append("option -P is not available in this build configuration of ugrep\033[m");
      }
      else
#endif
#ifndef HAVE_LIBZ
      if (key == 'z')
      {
        buf.assign(CERROR).append("Option -z is not available in this build configuration of ugrep\033[m");
      }
      else
#endif
      {
        fp->flag = !fp->flag;

        buf.assign("\033[m\033[7mM- \033[m ").append(fp->text).append(fp->flag ? " \033[32;1mon\033[m  " : " \033[31;1moff\033[m  ");
        buf[9] = fp->key;

        updated_ = true;
      }

      Screen::put(0, 0, buf);
      message_ = true;

      return;
    }
  }

  Screen::alert();
}

void Query::get_flags()
{
  // remember the context size, when specified
  if (flag_after_context > 0)
    context_ = flag_after_context;
  else if (flag_before_context > 0)
    context_ = flag_before_context;

  // get the interactive flags from the ugrep flags
  flags_[0].flag = flag_after_context > 0 && flag_before_context == 0;
  flags_[1].flag = flag_after_context == 0 && flag_before_context > 0;
  flags_[2].flag = flag_byte_offset;
  flags_[3].flag = flag_after_context > 0 && flag_before_context > 0;
  flags_[4].flag = flag_count;
  flags_[5].flag = flag_fixed_strings;
  flags_[6].flag = flag_basic_regexp;
  flags_[7].flag = flag_with_filename;
  flags_[8].flag = flag_no_filename;
  flags_[9].flag = flag_binary_without_matches;
  flags_[10].flag = flag_ignore_case;
  flags_[11].flag = flag_smart_case;
  flags_[12].flag = flag_column_number;
  flags_[13].flag = flag_files_with_match;
  flags_[14].flag = flag_line_number;
  flags_[15].flag = flag_only_matching;
  flags_[16].flag = flag_perl_regexp;
  flags_[17].flag = flag_directories_action == Action::RECURSE && flag_dereference;
  flags_[18].flag = flag_directories_action == Action::RECURSE && !flag_dereference;
  flags_[19].flag = flag_initial_tab;
  flags_[20].flag = flag_binary;
  flags_[21].flag = flag_ungroup;
  flags_[22].flag = flag_invert_match;
  flags_[23].flag = flag_with_hex;
  flags_[24].flag = flag_word_regexp;
  flags_[25].flag = flag_hex;
  flags_[26].flag = flag_line_regexp;
  flags_[27].flag = flag_empty;
  flags_[28].flag = flag_any_line;
  flags_[29].flag = flag_decompress;
  flags_[30].flag = flag_null;
  flags_[31].flag = flag_max_depth == 1;
  flags_[32].flag = flag_max_depth == 2;
  flags_[33].flag = flag_max_depth == 3;
  flags_[34].flag = flag_max_depth == 4;
  flags_[35].flag = flag_max_depth == 5;
  flags_[36].flag = flag_max_depth == 6;
  flags_[37].flag = flag_max_depth == 7;
  flags_[38].flag = flag_max_depth == 8;
  flags_[39].flag = flag_max_depth == 9;
  flags_[40].flag = flag_no_hidden;
  flags_[41].flag = flag_sort && (strcmp(flag_sort, "size") == 0 || strcmp(flag_sort, "rsize") == 0);
  flags_[42].flag = flag_sort && (strcmp(flag_sort, "changed") == 0 || strcmp(flag_sort, "changed") == 0);
  flags_[43].flag = flag_sort && (strcmp(flag_sort, "created") == 0 || strcmp(flag_sort, "created") == 0);
  flags_[44].flag = flag_sort && *flag_sort == 'r';
}

void Query::set_flags()
{
  // reset flags that are set by ugrep() depending on other flags
  flag_no_header = false;
  flag_dereference = false;
  flag_no_dereference = false;
  flag_files_without_match = false;
  flag_match = false;
  flag_binary_files = NULL;

  // set ugrep flags to the interactive flags
  flag_after_context = context_ * (flags_[0].flag || flags_[3].flag);
  flag_before_context = context_ * (flags_[1].flag || flags_[3].flag);
  flag_byte_offset = flags_[2].flag;
  flag_count = flags_[4].flag;
  flag_fixed_strings = flags_[5].flag;
  flag_basic_regexp = flags_[6].flag;
  flag_with_filename = flags_[7].flag;
  flag_no_filename = flags_[8].flag;
  flag_binary_without_matches = flags_[9].flag;
  flag_ignore_case = flags_[10].flag;
  flag_smart_case = flags_[11].flag;
  flag_column_number = flags_[12].flag;
  flag_files_with_match = flags_[13].flag;
  flag_line_number = flags_[14].flag;
  flag_only_matching = flags_[15].flag;
  flag_perl_regexp = flags_[16].flag;
  if (flags_[17].flag)
    flag_directories_action = Action::RECURSE, flag_dereference = true;
  else if (flags_[18].flag)
    flag_directories_action = Action::RECURSE, flag_dereference = false;
  else
    flag_directories_action = Action::SKIP;
  flag_initial_tab = flags_[19].flag;
  flag_binary = flags_[20].flag;
  flag_ungroup = flags_[21].flag;
  flag_invert_match = flags_[22].flag;
  flag_with_hex = flags_[23].flag;
  flag_word_regexp = flags_[24].flag;
  flag_hex = flags_[25].flag;
  flag_line_regexp = flags_[26].flag;
  flag_empty = flags_[27].flag;
  flag_any_line = flags_[28].flag;
  flag_decompress = flags_[29].flag;
  flag_null = flags_[30].flag;
  flag_max_depth = 0;
  for (size_t i = 31; i <= 39; ++i)
    if (flags_[i].flag)
      flag_max_depth = i - 30;
  flag_no_hidden = flags_[40].flag;
  if (flags_[41].flag)
    flag_sort = flags_[44].flag ? "rsize" : "size";
  else if (flags_[42].flag)
    flag_sort = flags_[44].flag ? "rchanged" : "changed";
  else if (flags_[43].flag)
    flag_sort = flags_[44].flag ? "rcreated" : "created";
  else
    flag_sort = flags_[44].flag ? "rname" : "name";

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

Query::Mode              Query::mode_                = Query::Mode::QUERY;
bool                     Query::updated_             = false;
bool                     Query::message_             = false;
char                     Query::line_[QUERY_MAX_LEN] = { '\0' };
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
int                      Query::select_              = -1;
bool                     Query::select_all_          = false;
int                      Query::skip_                = 0;
std::vector<std::string> Query::view_;
std::vector<bool>        Query::selected_;
bool                     Query::eof_                 = true;
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

#ifdef OS_WIN

HANDLE                   Query::hPipe_;
OVERLAPPED               Query::overlapped_;
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
  { false, '.', "no hidden files" },
  { false, '#', "sort by size" },
  { false, '$', "sort by changed" },
  { false, '@', "sort by created" },
  { false, '^', "reverse sort" },
  { false, 0, NULL, }
};
