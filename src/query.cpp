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
@brief     Query engine TUI
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "ugrep.hpp"
#include "stats.hpp"
#include "query.hpp"

#include <reflex/error.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <fcntl.h>

#ifdef OS_WIN

#include <direct.h>

// create a non-blocking pipe (Windows named pipe)
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
      fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), _O_RDONLY);
      fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), _O_WRONLY);
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

// make a pipe block
inline void set_blocking(int fd0)
{
  fcntl(fd0, F_SETFL, fcntl(fd0, F_GETFL) & ~O_NONBLOCK);
}

#endif

static const char *LARROW = "«"; // left arrow
static const char *RARROW = "»"; // right arrow

// return pointer to character in the query search line at screen col, taking UTF-8 double width characters into account
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

// return pointer to the character in the query search line at pos distance after the screen col
char *Query::line_ptr(int col, int pos)
{
  return Screen::mbstring_pos(line_ptr(col), pos);
}

// return pointer to the end of the query search line
char *Query::line_end()
{
  char *ptr = line_;
  while (*ptr != '\0')
    ++ptr;
  return ptr;
}

// return number of character positions on the query search line up to the current screen Query::col_, taking UTF-8 wide characters into account
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

// return the length of the query search line as a number of screen columns displayed
int Query::line_len()
{
  return Screen::mbstring_width(line_);
}

// return the length of the query search line as the number of UTF-8 wide characters
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

// draw a textual part of the query search line, this function is called by draw()
void Query::display(int col, int len)
{
  const char *ptr = line_ptr(col);
  const char *end = line_ptr(col + len);
  const char *err = error_ >= 0 && !Screen::mono ? line_ + error_ : NULL;
  const char *next;
  bool alert = false;
  bool list = false;
  bool braced = false;
  bool literal = false;
  if (!Screen::mono)
  {
    if (!flag_fixed_strings)
    {
      for (const char *look = line_; look < ptr; ++look)
      {
        if (!braced && !literal && *look == (list ? ']' : '['))
          list = !list, look += (*look == '[') + (look[1] == '^' || look[1] == '\\');
        else if (!list && !literal && *look == (braced ? '}' : '{'))
          braced = !braced;
        else if (!list && !braced && *look == '"' && flag_bool)
          literal = !literal;
        else if (*look == '\\' && look[1] == (literal ? 'E' : 'Q'))
          literal = !literal;
        else if (*look == '\\')
          ++look;
      }
    }
    if (literal || list)
      Screen::put(color_ql);
    else if (braced)
      Screen::put(color_qb);
    else
      Screen::put(color_qr);
  }
  for (next = ptr; next < end; ++next)
  {
    if (next == err)
    {
      Screen::put(ptr, next - ptr);
      Screen::normal();
      if (!Screen::mono)
        Screen::put(color_qe);
      ptr = next;
      alert = true;
    }
    else
    {
      int ch = static_cast<unsigned char>(*next);
      if (err != NULL && alert && next > err && (ch & 0xc0) != 0x80)
      {
        Screen::put(ptr, next - ptr);
        Screen::normal();
        if (list)
          Screen::put(color_ql);
        else if (braced)
          Screen::put(color_qb);
        else
          Screen::put(color_qr);
        ptr = next;
        alert = false;
      }
      if (ch <= 0x1f || ch == 0x7f)
      {
        Screen::put(ptr, next - ptr);
        if (err != NULL && alert && next > err)
        {
          Screen::normal();
          if (!Screen::mono)
            Screen::put(color_qr);
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
      else if (ch == '[' && !Screen::mono && !list && !literal && !braced && !flag_fixed_strings)
      {
        list = true;
        Screen::put(ptr, next - ptr);
        Screen::normal();
        Screen::put(color_qm);
        Screen::put(ch);
        Screen::normal();
        Screen::put(color_ql);
        ptr = ++next;
        next += (*next == '^');
        next += (*next == '\\');
      }
      else if (ch == ']' && !Screen::mono && list && !literal && !braced && !flag_fixed_strings)
      {
        list = false;
        Screen::put(ptr, next - ptr);
        Screen::normal();
        Screen::put(color_qm);
        Screen::put(ch);
        Screen::normal();
        Screen::put(color_qr);
        ptr = next + 1;
      }
      else if (ch == '{' && !Screen::mono && !list && !literal && !braced && !flag_fixed_strings)
      {
        braced = true;
        Screen::put(ptr, next - ptr);
        Screen::normal();
        Screen::put(color_qb);
        ptr = next;
      }
      else if (ch == '}' && !Screen::mono && !list && !literal && braced && !flag_fixed_strings)
      {
        braced = false;
        Screen::put(ptr, next - ptr + 1);
        Screen::normal();
        Screen::put(color_qr);
        ptr = next + 1;
      }
      else if (ch == '"' && !Screen::mono && !list && !braced && flag_bool && !flag_fixed_strings)
      {
        literal = !literal;
        Screen::put(ptr, next - ptr);
        Screen::normal();
        Screen::put(color_ql);
        Screen::put(ch);
        Screen::normal();
        if (literal)
          Screen::put(color_ql);
        else
          Screen::put(color_qr);
        ptr = next + 1;
      }
      else if (ch == '\\' && next + 1 != err && next[1] >= ' ' && next[1] <= '~' && !Screen::mono && !flag_fixed_strings)
      {
        if (next[1] == 'E' && !list)
          literal = false;
        if (!literal)
        {
          if (next[1] == 'Q' && !list)
            literal = true;
          Screen::put(ptr, next - ptr);
          Screen::normal();
          Screen::put(color_qm);
          Screen::put(ch);
          Screen::put(next[1]);
          Screen::normal();
          if (literal || list)
            Screen::put(color_ql);
          else if (braced)
            Screen::put(color_qb);
          else
            Screen::put(color_qr);
          ptr = ++next + 1;
        }
      }
      else if (strchr("$()*+.?^|", ch) != NULL && !list && !literal && !braced && !Screen::mono && !flag_fixed_strings)
      {
        if (!flag_basic_regexp || strchr("()+?|", ch) == NULL)
        {
          Screen::put(ptr, next - ptr);
          Screen::normal();
          Screen::put(color_qm);
          Screen::put(ch);
          Screen::normal();
          Screen::put(color_qr);
          ptr = next + 1;
        }
      }
    }
  }
  Screen::put(ptr, next - ptr);
  if (next == err && !Screen::mono)
    Screen::put(color_qe);
}

// draw the query search line
void Query::draw()
{
  if (mode_ == Mode::QUERY)
  {
    if (select_ == -1)
    {
      start_ = 0;

      Screen::home();

      if (row_ > 0)
      {
        char down[16];

        snprintf(down, sizeof(down), "%3d ", row_);

        Screen::normal();
        Screen::put(down);

        start_ = strlen(down);
      }

      if (!dirs_.empty())
      {
        int width = Screen::mbstring_width(dirs_.c_str());
        int offset = 0;
        int middle = Screen::cols/2 - start_;
        if (width + 2 > middle)
          offset = width + 2 - middle;

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
        Screen::put(error_ == -1 ? color_qp : color_qe);
      }

      Screen::put(prompt_.c_str());
      Screen::normal();

      start_ += static_cast<int>(prompt_.size());

      int pos;
      if (len_ - col_ < shift_)
        pos = Screen::cols - start_ - (len_ - col_) - 1;
      else
        pos = Screen::cols - start_ - shift_ - 1;

      offset_ = col_ > pos ? col_ - pos : 0;

      if (offset_ > 0)
      {
        if (!Screen::mono)
          Screen::put(error_ == -1 ? color_qp : color_qe);

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
            Screen::put(error_ == -1 ? color_qp : color_qe);
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
            Screen::put(error_ == -1 ? color_qp : color_qe);
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
}

// view one row of text by drawing it on screen
void Query::disp(int row)
{
  Screen::normal();
  if (row < rows_)
  {
    if (selected_[row])
      Screen::select();
    int nulls = !flag_text ? 2 : 0; // how many nulls to ignore in marked pathnames?
    Screen::put(row - row_ + 1, 0, view_[row], skip_, -1, nulls);
    if (selected_[row])
      Screen::deselect();
  }
  else if (row - row_ + 1 < maxrows_)
  {
    Screen::setpos(row - row_ + 1, 0);
    Screen::erase();
  }
}

// redraw the screen
void Query::redraw()
{
  Screen::getsize();
  maxrows_ = Screen::rows;
  if (flag_split)
    maxrows_ /= 2;
  shift_ = (Screen::cols - start_) / 10;
  Screen::normal();

  if (mode_ == Mode::HELP)
  {
    message_ = false;

#ifdef __APPLE__
    Screen::put(0, 0, "\033[7m     HELP AND OPTIONS     \033[m  \033[7m^\033[m=ctrl  \033[7mS-\033[m=shift  \033[7mM-\033[m=option or \033[7m^O\033[m+key");
#else
    Screen::put(0, 0, "\033[7m     HELP AND OPTIONS     \033[m  \033[7m^\033[m=ctrl  \033[7mS-\033[m=shift  \033[7mM-\033[m=alt or \033[7m^O\033[m+key");
#endif

    Screen::put( 1, 0, "");
    Screen::put( 2, 0, "\033[7mEsc\033[m   go back / exit");
    Screen::put( 3, 0, "\033[7mTab\033[m   cd dir / select file");
    Screen::put( 4, 0, "\033[7mS-Tab\033[m cd .. / deselect file");
    Screen::put( 5, 0, "\033[7mEnter\033[m line selection mode");
    Screen::put( 6, 0, "");
    Screen::put( 7, 0, "\033[7mUp\033[m     \033[7mDown\033[m    scroll");
    Screen::put( 8, 0, "\033[7mPgUp\033[m   \033[7mPgDn\033[m    scroll page");
#ifdef __APPLE__
    Screen::put( 9, 0, "\033[7mS-Left\033[m \033[7mS-Right\033[m pan ½ page");
    Screen::put(10, 0, "\033[7mS-Up\033[m   \033[7mS-Down\033[m  scroll ½ pg");
#else
    Screen::put( 9, 0, "\033[7mM-Left\033[m \033[7mM-Right\033[m pan ½ page");
    Screen::put(10, 0, "\033[7mM-Up\033[m   \033[7mM-Down\033[m  scroll ½ pg");
#endif
    Screen::put(11, 0, "\033[7mHome\033[m   \033[7mEnd\033[m     of pattern");
    Screen::put(12, 0, "");
    Screen::put(13, 0, "\033[7m^K\033[m delete after cursor");
    Screen::put(14, 0, "\033[7m^L\033[m refresh screen");
    Screen::put(15, 0, "\033[7m^Q\033[m quit and output");
    Screen::put(16, 0, "\033[7m^R\033[m/\033[7mF4\033[m restore bookmark");
    Screen::put(17, 0, "\033[7m^S\033[m next dir/file/context");
    Screen::put(18, 0, "\033[7m^T\033[m/\033[7mF5\033[m toggle split screen");
    Screen::put(19, 0, "\033[7m^U\033[m delete before cursor");
    Screen::put(20, 0, "\033[7m^V\033[m verbatim character");
    Screen::put(21, 0, "\033[7m^W\033[m prev dir/file/context");
    Screen::put(22, 0, "\033[7m^X\033[m/\033[7mF3\033[m set bookmark");
    Screen::put(23, 0, "\033[7m^Y\033[m/\033[7mF2\033[m view or edit file");
    Screen::put(24, 0, "\033[7m^Z\033[m/\033[7mF1\033[m help");
    Screen::put(25, 0, "\033[7m^^\033[m chdir to starting dir");
    Screen::put(26, 0, "\033[7m^]\033[m toggle color/mono");
    Screen::put(27, 0, "\033[7m^\\\033[m force quit");
    Screen::put(28, 0, "");
    Screen::put(29, 0, "\033[7mM-/xxxx/\033[m U+xxxx code point");
    Screen::put(30, 0, "");

    std::string buf;
    int row = 31;
    int col = 0;

    for (Flags *fp = flags_; fp->text != NULL; ++fp)
    {
      buf.assign("\033[7mM- \033[m ");
      buf[6] = fp->key;
      if (strncmp(fp->text, "decrease", 8) == 0 || strncmp(fp->text, "increase", 8) == 0)
        buf.append("    ").append(fp->text);
      else if (fp->flag)
        buf.append("[\033[32;1m*\033[m] \033[1m").append(fp->text).append("\033[m");
      else
        buf.append("[ ] ").append(fp->text);
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

    Screen::put(0, Screen::cols - 1, "?");
  }
  else
  {
    // do not show dots
    tick_ = 4;

    if (error_ == -1)
    {
      if (select_ >= 0 && select_ >= row_ + maxrows_ - 1)
        row_ = select_ - maxrows_ + 3;
      else if (select_ >= 0 && select_ < row_)
        row_ = select_ - 1;

      if (row_ >= rows_)
        row_ = rows_ - 1;
      if (row_ < 0)
        row_ = 0;

      int end = row_ + maxrows_ - 2;

      for (int i = row_; i < end; ++i)
        disp(i);

      status(true);

      preview();
    }

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

// move the cursor to a column in the query search line
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
    draw();
  else if (offset_ > 0)
    draw();
  else
    Screen::setpos(0, start_ + col_ - offset_);
}

// insert text to line at cursor
void Query::insert(const char *text, size_t size)
{
  char *end = line_end();
  if (end + size >= line_ + sizeof(Line))
  {
    size = line_ + sizeof(Line) - end - 1;
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
  char *skip = next;
  while (*skip != '\0' && Screen::mbchar_width(next, const_cast<const char**>(&skip)) == 0)
    next = skip;
  if (next > ptr)
  {
    const char *end = line_end();
    memmove(ptr, next, end - next + 1);
    updated_ = true;
    error_ = -1;
    len_ = line_len();
    draw();
  }
}

// called by the main program
void Query::query()
{
  // do not exit on usage warnings
  flag_usage_warnings = true;

  get_stdin();

  if (!VKey::setup(VKey::TTYRAW))
    abort("no ANSI terminal keyboard detected");

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

  // if --view is empty (default), then try the PAGER or EDITOR environment variable as viewer or use the default viewer
  if (flag_view != NULL && *flag_view == '\0')
  {
    flag_view = getenv("PAGER");

    if (flag_view == NULL)
      flag_view = getenv("EDITOR");

    if (flag_view == NULL)
      flag_view = DEFAULT_VIEW_COMMAND;
  }

  query_ui();

  VKey::cleanup();
  Screen::cleanup();

  // check TTY again for color support, this time without --query
  flag_query = false;
  terminal();

  if (!flag_quiet)
    print();

  // close the search pipe to terminate the search threads, if still open
  if (!eof_)
  {
    close(search_pipe_[0]);
    eof_ = true;

    // graciously shut down ugrep() if still running
    Static::cancel_ugrep();
  }

  // close the stdin pipe
  if (flag_stdin && Static::source != stdin && Static::source != NULL)
  {
    fclose(Static::source);
    Static::source = NULL;
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
  mode_ = Mode::QUERY;
  updated_ = false;
  message_ = false;
  *line_ = '\0';
  col_ = 0;
  len_ = 0;
  offset_ = 0;
  shift_ = 8;
  error_ = -1;
  tick_ = 4;
  row_ = 0;
  rows_ = 0;
  maxrows_ = Screen::rows;
  skip_ = 0;
  select_ = -1;
  select_all_ = false;
  globbing_ = false;
  eof_ = true;
  buflen_ = 0;

  Screen::clear();

  // if -e PATTERN specified, collect patterns on the line to edit
  if (!flag_regexp.empty())
  {
    std::string pattern;

    if (flag_regexp.size() == 1)
    {
      pattern = flag_regexp.front();
    }
    else
    {
      int sep = flag_fixed_strings && !flag_bool ? '\n' : '|';

      for (auto& regex : flag_regexp)
      {
        if (!regex.empty())
        {
          if (!pattern.empty())
            pattern.push_back(sep);
          pattern.append(regex);
        }
      }
    }

    flag_regexp.clear();

    size_t num = pattern.size();
    if (num >= sizeof(Line))
      num = sizeof(Line) - 1;

    pattern.copy(line_, num);
    line_[num] = '\0';

    len_ = line_len();

    move(len_);
  }

  set_prompt();
  search();
  redraw();

  bool ctrl_o = false;
  bool ctrl_v = false;
  bool err = false;

  while (true)
  {
    size_t delay = message_ ? QUERY_MESSAGE_DELAY : flag_delay;

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

      if (error_ >= 0 && !err)
      {
        // show error position in the query line, but only once
        draw();

        err = true;
      }

      if (delay-- == 0)
      {
        if (mode_ == Mode::QUERY && updated_)
        {
          search();

          err = false;
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

        delay = flag_delay;
      }

      key = VKey::in(100);

      if (key > 0)
        break;
    }

    if (message_)
    {
      message_ = false;
      draw();
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
          if (mode_ == Mode::QUERY)
          {
            if (globbing_)
            {
              globbing_ = false;

              memcpy(line_, temp_, sizeof(Line));
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
          if (mode_ == Mode::QUERY)
          {
            if (select_ == -1)
            {
              if (rows_ > 0)
              {
                select_ = row_;
                select_all_ = false;
                if (flag_split)
                {
                  flag_split = false;
                  redraw();
                }
                else
                {
                  draw();
                }
              }
              else
              {
                Screen::alert();
              }
            }
            else
            {
              selected_[select_] = !selected_[select_];
              disp(select_);
              down();
            }
          }
          break;

        case VKey::META:
          key = VKey::get();
          switch (key)
          {
            case VKey::TAB: // Shift-TAB: chdir .. or deselect file (or pan screen in selection mode)
              if (mode_ == Mode::QUERY && error_ == -1)
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
              else if (error_ != -1 && !dirs_.empty())
              {
                deselect();
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
          if (mode_ == Mode::QUERY && error_ == -1)
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
          else
          {
            Screen::alert();
          }
          break;

        case VKey::BS: // Backspace: delete character before the cursor
          if (mode_ == Mode::QUERY)
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
              disp(select_);
            }
          }
          break;

        case VKey::DEL: // DEL: delete character under the cursor
          if (select_ == -1)
          {
            erase(1);
          }
          else
          {
            up();
            selected_[select_] = !selected_[select_];
            disp(select_);
          }
          break;

        case VKey::RIGHT: // RIGHT/CTRL-F: move right
          if (select_ == -1)
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
          if (select_ == -1)
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
          if (select_ == -1)
            move(0);
          else
            Screen::alert();
          break;

        case VKey::END: // END/CTRL-E: end of line
          if (select_ == -1)
            move(len_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_C: // CTRL-C: quit and output lines
          if (confirm("Exit"))
            return;
          break;

        case VKey::CTRL_K: // CTRL-K: delete after cursor
          if (select_ == -1)
            erase(len_ - col_);
          else
            Screen::alert();
          break;

        case VKey::CTRL_L: // CTRL-L: refresh screen
          redraw();
          break;

        case VKey::CTRL_O: // CTRL-O: press KEY to get Alt-KEY
          if (select_ == -1)
            ctrl_o = true;
          else
            Screen::alert();
          break;

        case VKey::CTRL_R: // CTRL-R or F4: restore bookmarked state
        case VKey::FN(4):
          if (mark_.row >= 0)
          {
            int row;
            if (mark_.restore(line_, col_, row, flags_))
            {
              globbing_ = false;
              set_prompt();
              len_ = line_len();
              search();
            }
            jump(row);
          }
          else
          {
            Screen::alert();
          }
          break;

        case VKey::CTRL_Q: // CTRL-Q: immediately quit and output lines
          return;

        case VKey::CTRL_S: // CTRL=S: scroll to next file or directory (with -l or -c) or context with -ABC
          next();
          break;

        case VKey::CTRL_T: // CTRL-T or F5: toggle split screen
        case VKey::FN(5):
          if (select_ == -1)
            flag_split = !flag_split; // (un)split screen
          redraw();
          break;

        case VKey::CTRL_U: // CTRL-U: delete before cursor
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

        case VKey::CTRL_V: // CTRL-V: verbatim insert character
          if (select_ == -1)
            ctrl_v = true;
          else
            Screen::alert();
          break;

        case VKey::CTRL_W: // CTRL-W: scroll back one file or directory (with -l or -c) or context with -ABC
          back();
          break;

        case VKey::CTRL_X: // CTRL-X or F3: set bookmark and save state
        case VKey::FN(3):
          mark_.save(line_, col_, (select_ >= 0 ? select_ : row_), flags_);
          status(true);
          break;

        case VKey::CTRL_Y: // CTRL-Y or F2: view (or edit) file
        case VKey::FN(2):
          if (select_ == -1)
            view();
          else
            Screen::alert();
          break;

        case VKey::CTRL_Z: // CTRL-Z or F1: help
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

        case VKey::CTRL_RB: // CTRL-]: toggle colors/mono
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::CTRL_CA: // CTRL-^: chdir back to working directory
          if (mode_ == Mode::QUERY && (error_ == -1 || !dirs_.empty()) && select_ == -1)
            unselect();
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
#ifdef __APPLE__
              Screen::put(color_qe);
              Screen::put(0, 0, "MacOS Terminal Preferences/Profiles/Keyboard: enable \"Use Option as Meta key\"");
#endif
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

// start a new search, cancel the previous search when still running
void Query::search()
{
  bool cancel = !eof_;

  if (cancel)
  {
    close(search_pipe_[0]);
    eof_ = true;
    buflen_ = 0;

    // graciously shut down ugrep() if still running
    Static::cancel_ugrep();
  }

#ifdef OS_WIN

  hPipe_ = nonblocking_pipe(search_pipe_);

  if (hPipe_ == INVALID_HANDLE_VALUE)
  {
    what_.assign("cannot create pipe");
    // error at the end of the line, not within
    error_ = line_wsize();
    return;
  }

  memset(&overlapped_, 0, sizeof(overlapped_));
  blocking_ = false;
  pending_ = false;

#else

  if (nonblocking_pipe(search_pipe_) < 0)
  {
    what_.assign("cannot create pipe");
    // error at the end of the line, not within
    error_ = line_wsize();
    return;
  }

#endif

  if (search_thread_.joinable())
  {
    if (cancel && error_ == -1)
    {
      const int banlen = 256;
      char banner[banlen];
      snprintf(banner, banlen, "restarting: please be patient while I cancel searching large files...%*s", banlen - 70, "");
      Screen::normal();
      Screen::invert();
      Screen::put(maxrows_ - 1, 0, banner);
      Screen::normal();
    }

    search_thread_.join();
  }

  eof_ = false;
  row_ = 0;
  rows_ = 0;
  skip_ = 0;
  searched_ = 0;
  found_ = 0;
  error_ = -1;

  // reset ugrep stats
  Stats::reset();

  Static::arg_pattern = globbing_ ? temp_ : line_;

  if (*Static::arg_pattern == '\0' && !flag_file.empty())
    Static::arg_pattern = NULL;

  if (deselect_file_)
  {
    // S-Tab back out of a selected file
    selected_file_.clear();
    deselect_file_ = false;
  }
  else if (!selected_file_.empty() && Static::arg_files.empty())
  {
    // Tab into the selected file to search
    Static::arg_files.emplace_back(selected_file_.c_str());
  }

  set_flags();

  set_stdin();

  if (error_ == -1)
  {
    // reset warnings
    Static::warnings = 0;

    // clear the CNF first to populate the CNF in ugrep() with the contents of arg_pattern
    Static::bcnf.clear();

    // delete old matcher, if any, to prevent preview from using it (rows_ == 0 prevents that too)
    Static::matcher.reset();

    search_thread_ = std::thread(Query::execute, search_pipe_[1]);
  }

  select_ = -1;
  select_all_ = false;

  draw();
  status(true);

  updated_ = false;
}

// display the status line
void Query::status(bool show)
{
  const int banlen = 256;
  char banner[banlen];
  size_t sf = Stats::searched_files();
  size_t ff = Stats::found_files();
  const char *bookmark = mark_.is_set() ? "^R restore" : "^X bookmark";

  // force redisplay by setting tick
  if (show)
    tick_ = 4;

  if (eof_)
  {
    if (tick_ < 8)
    {
      Screen::normal();

      if (ff == 0)
        Screen::put(1, 0, "[no matches found]");

      size_t sd = Stats::searched_dirs();
      size_t ws = Static::warnings;
      const char *pager = flag_view ? flag_view : "-";

      snprintf(banner, banlen, "%zu/%zu files | %zu dirs | %zu warnings | ^Q quit  ^T split  ^Y %s  ^Z help  %s%*s", ff, sf, sd, ws, pager, bookmark, banlen - 100, "");
      Screen::invert();
      Screen::put(maxrows_ - 1, 0, banner);
      Screen::normal();

      tick_ = 8;
    }
  }
  else
  {
    if (sf > searched_)
    {
      searched_ = sf;
      tick_ = 4;
    }

    if (ff > found_)
    {
      found_ = ff;
      tick_ = 4;
    }

    if (tick_ == 4)
    {
      Screen::normal();

      int spinner = "-\\|/"[spin_];
      const char *pager = flag_view ? flag_view : "-";

      snprintf(banner, banlen, "[%c] %zu/%zu files queued | ^Q quit  ^T split  ^Y %s  ^Z help  %s%*s", spinner, ff, sf, pager, bookmark, banlen - 100, "");
      Screen::invert();
      Screen::put(maxrows_ - 1, 0, banner);
      Screen::normal();

      spin_ = (spin_ + 1) & 3;
    }

    tick_ = (tick_ + 1) & 7;
  }
}

// update screen periodically and display more data when data becomes available
bool Query::update()
{
  int begin = rows_;

  // fetch viewable portion plus one up to two screenfuls more
  fetch(row_ - (row_ % Screen::rows) + 2 * Screen::rows);

  // display the viewable portion when updated
  if (rows_ > begin && begin < row_ + maxrows_ - 2)
  {
    Screen::normal();

    if (begin + maxrows_ - 2 > rows_)
      begin = rows_ - maxrows_ + 2;
    if (begin < 0)
      begin = 0;
    if (begin < row_)
      begin = row_;

    int end = rows_;

    if (end > row_ + maxrows_ - 2)
      end = row_ + maxrows_ - 2;

    for (int i = begin; i < end; ++i)
      disp(i);
  }

  if (error_ == -1)
  {
    if (tick_ < 8 && rows_ < row_ + maxrows_ - 2)
    {
      int row = rows_ - row_ + 1;

      // no dots and clear below when we hit eof
      if (eof_)
        tick_ = 4;

      Screen::setpos(row, 0);
      if (tick_ < 4)
      {
        Screen::normal();
        if (tick_ == 0)
          Screen::erase();
        else
          Screen::put(&"..."[3 - tick_]);
      }
      else if (tick_ == 4)
      {
        // clear elipsis and below
        if (flag_split)
        {
          if (eof_)
          {
            redraw();
          }
          else
          {
            // clear top half below last row
            for (int i = rows_; i < maxrows_ - 1; ++i)
              disp(i);

            // clear bottom half if nothing was found yet
            if (rows_ == 0)
            {
              Screen::setpos(maxrows_, 0);
              Screen::end();
            }
          }
        }
        else
        {
          // clear all below last row
          Screen::normal();
          Screen::end();
        }
      }
    }

    // display updated status line
    status(false);
  }
  else if (tick_ < 8)
  {
    Screen::setpos(1, 0);
    Screen::normal();
    Screen::erase();

    if (!Screen::mono)
    {
      Screen::setpos(2, 0);
      Screen::put(color_qe);
      Screen::erase();
    }

    Screen::put(2, 0, what_);
    Screen::normal();
    Screen::end();

    tick_ = 8;
  }

  // preview the first file when it arrived
  if (flag_split && begin == 0 && rows_ > 0)
    preview();

  // return true if screen was updated when data was available
  return begin < rows_;
}

// fetch rows up to and including the specified row, when available, i.e. do not block when pipe is non-blocking
bool Query::fetch(int row)
{
  int prev_rows = rows_;

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
                Static::cancel_ugrep();
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
                Static::cancel_ugrep();
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
            Static::cancel_ugrep();
          }
        }
        else
        {
          // no data, pipe is closed (EOF)
          close(search_pipe_[0]);
          eof_ = true;
          Static::cancel_ugrep();
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

  // true if more rows were fetched
  return prev_rows < rows_;
}

// execute the search in a new thread and send results to a pipe
void Query::execute(int pipe_fd)
{
  Static::output = fdopen(pipe_fd, "wb");

  if (Static::output != NULL)
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

      // subtract 4 for (?m) or (?misx)
      if (error_ >= 4 + flag_ignore_case + flag_dotall + flag_free_space)
        error_ -= 4 + flag_ignore_case + flag_dotall + flag_free_space;

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

    fclose(Static::output);
    Static::output = NULL;
  }
  else
  {
    what_.assign("cannot fdopen pipe");

    // error at the end of the line, not within
    error_ = line_wsize();
  }
}

// cursor up
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
    if (!flag_split)
    {
      disp(row_ - 1);
      --row_;
      Screen::pan_down();
      status(true);
      draw();
    }
    else
    {
      // split screen
      --row_;
      redraw();
    }
  }
}

// cursor down
void Query::down()
{
  if (select_ >= 0)
  {
    ++select_;
    if (select_ >= rows_)
      select_ = rows_ - 1;
    if (select_ < row_ + maxrows_ - 2)
      return;
  }
  if (row_ + 1 < rows_)
  {
    ++row_;
    if (!flag_split)
    {
      Screen::normal();
      disp(row_ + maxrows_ - 2);
      Screen::pan_up();
      status(true);
      draw();
    }
    else
    {
      // split screen
      redraw();
    }
  }
}

// page up the display
void Query::pgup(bool half_page)
{
  if (select_ >= 0)
  {
    if (half_page)
      select_ -= maxrows_ / 2;
    else
      select_ -= maxrows_ - 2;
    if (select_ < 0)
      select_ = 0;
    if (select_ > row_)
      return;
  }
  if (row_ > 0)
  {
    int oldrow = row_;
    if (half_page)
      row_ -= maxrows_ / 2;
    else
      row_ -= maxrows_ - 2;
    if (row_ < 0)
      row_ = 0;
    if (!flag_split)
    {
      Screen::pan_down(oldrow - row_);
      for (int i = row_; i < oldrow; ++i)
        disp(i);
      status(true);
      draw();
    }
    else
    {
      // split screen
      redraw();
    }
  }
}

// page down the display
void Query::pgdn(bool half_page)
{
  if (select_ >= 0)
  {
    if (half_page)
      select_ += maxrows_ / 2;
    else
      select_ += maxrows_ - 2;
    if (select_ >= rows_)
      select_ = rows_ - 1;
    if (select_ < row_ + maxrows_ - 2)
      return;
  }
  if (row_ + maxrows_ - 1 <= rows_)
  {
    int oldrow = row_;
    if (half_page)
      row_ += maxrows_ / 2;
    else
      row_ += maxrows_ - 2;
    if (row_ + maxrows_ > rows_)
    {
      row_ = rows_ - maxrows_ + 2;
      if (row_ < oldrow)
        row_ = oldrow;
    }
    if (!flag_split)
    {
      int diff = row_ - oldrow;
      if (diff > 0)
      {
        Screen::normal();
        Screen::pan_up(diff);
        for (int i = row_ + maxrows_ - diff - 2; i < row_ + maxrows_ - 2; ++i)
          disp(i);
        status(true);
        draw();
      }
    }
    else
    {
      // split screen
      redraw();
    }
  }
}

// move back up one file
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

  // scroll current row_ or select_ selection
  int& ref = select_ == -1 ? row_ : select_;

  if (ref == 0)
    return;

  --ref;

  if (compare_dir && flag_tree)
  {
    if (ref == 0)
      return;

    --ref;

    while (ref > 0 && view_[ref].size() > 1)
      --ref;
  }
  else
  {
    std::string filename;

    // get the current filename to compare against, when present
    find_filename(ref, filename);

    bool found = false;

    while (ref > 0 && !(found = find_filename(ref, filename, compare_dir)))
      --ref;

    if (found && (compare_dir || !flag_heading))
    {
      ++ref;

      // --tree: skip over directory tree spacing
      if (compare_dir && flag_tree && (view_[ref].empty() || view_[ref].front() != '\0'))
        ++ref;
    }
  }

  redraw();
}

// move down to the next file
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

  // scroll current row_ or select_ selection
  int& ref = select_ == -1 ? row_ : select_;

  if (compare_dir && flag_tree)
  {
    ++ref;

    while (true)
    {
      bool found = false;

      while (ref + 1 < rows_ && !(found = view_[ref].size() <= 1))
        ++ref;

      redraw();

      if (found || (eof_ && buflen_ == 0))
        return;

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
    std::string filename;

    // get the current filename to compare when present
    if (ref < rows_)
      find_filename(ref, filename);

    ++ref;

    while (true)
    {
      bool found = false;

      // seach forward for different filename or different directory
      while (ref + 1 < rows_ && !(found = find_filename(ref, filename, compare_dir)))
        ++ref;

      redraw();

      if (found || (eof_ && buflen_ == 0))
        return;

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

// jump to the specified row
void Query::jump(int row)
{
  if (row < 0)
    row = 0;

  // scroll current row_ or select_ selection
  int& ref = select_ == -1 ? row_ : select_;

  if (row <= ref)
  {
    ref = row;

    if (ref >= rows_)
      ref = rows_ - 1;
  }
  else if (row < rows_)
  {
    ref = row;
  }
  else
  {
    while (true)
    {
      while (ref < row && ref + 1 < rows_)
        ++ref;

      // exit if at the desired row or if the desired row is beyond the end of the search results
      if (ref == row || (eof_ && buflen_ == 0))
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

// skip over ANSI escape sequence when present on a line
static size_t skip_escapes(const std::string& line, size_t pos)
{
  size_t end = line.size();

  while (pos < end && line.at(pos) == '\033')
  {
    if (pos + 1 < end && line.at(pos + 1) == '[')
    {
      // CSI \e[... sequence
      pos += 2;
      while (pos < end && (line.at(pos) < 0x40 || line.at(pos) > 0x7e))
        ++pos;
      ++pos;
    }
    else if (pos + 1 < end && line.at(pos + 1) == ']')
    {
      // OSC \e]...BEL|ST sequence
      pos += 2;
      while (pos < end && line.at(pos) != '\a' && (line.at(pos) != '\033' || (pos + 1 < end && line.at(pos + 1) != '\\')))
        ++pos;
      if (pos < end && line.at(pos) == '\033')
        ++pos;
      ++pos;
    }
    else
    {
      // ESC x sequence
      pos += 2;
    }
  }

  return pos;
}

// view or edit the file located under the cursor or just above in the screen (when not in selection mode)
void Query::view()
{
  if (row_ >= rows_ || flag_text || flag_format != NULL)
  {
    Screen::alert();
    return;
  }

  // if no viewer, then give up
  if (flag_view == NULL || *flag_view == '\0')
  {
    Screen::alert();
    return;
  }

  std::string command(flag_view);
  int ref = select_ >= 0 ? select_ : row_;
  size_t line_number = 0;

  // -n: extract line number from ugrep output if the specified pager supports +linenum
  if (flag_line_number &&
      !flag_files_with_matches &&
      !flag_count &&
      !flag_hex &&
      !flag_with_hex &&
      (command == "less"  ||
       command == "moar"  ||
       command == "more"  ||
       command == "most"  ||
       command == "w3m"   ||
       command == "emacs" ||
       command == "joe"   ||
       command == "vi"    ||
       command == "vim"   ||
       command == "vis"   ||
       command == "kak"   ||
       command == "nano"  ||
       command == "pico"  ||
       command == "vile"  ||
       command == "zile"))
  {
    line_number = get_line_number();
  }

  std::string filename;
  std::string partname;
  bool found = false;

  if (rows_ > 0)
  {
    if (flag_tree && (flag_files_with_matches || flag_count))
    {
      // --tree with -c or -l: move down over non-filename lines to reach a filename
      while (ref + 1 < rows_ && (view_[ref].empty() || view_[ref].front() != '\0'))
        ++ref;
    }
    else
    {
      // move down to skip empty lines
      while (ref + 1 < rows_ && view_[ref].empty())
        ++ref;
    }

    // move up until a filename header is found, unless we're at an empty line
    if (!view_[ref].empty())
      while (ref >= 0 && !(found = find_filename(ref, filename, false, true, &partname)))
        --ref;

    // only a partname was present, no filename
    if (filename.empty())
      found = false;

    if (!found)
    {
      if (Static::arg_files.size() == 1)
      {
        // if not found and only one FILE argument was provided then view FILE
        filename = Static::arg_files.front();
        if (filename == "-")
          filename = flag_label;
        found = true;
      }
      else if (flag_stdin && Static::arg_files.empty())
      {
        // view standard input
        filename = flag_label;
        found = true;
      }
    }
  }

  if (found)
  {
#ifdef OS_WIN
    _WIN32_FILE_ATTRIBUTE_DATA attr_before;
#else
    struct stat buf;
    uint64_t mtime;
#endif

    if (!flag_stdin || filename != flag_label)
    {
      // check if the file exists and is a regular file, also get the file's last modification time
#ifdef OS_WIN
      found = GetFileAttributesExW(utf8_decode(filename).c_str(), GetFileExInfoStandard, &attr_before);
#else
      found = stat(filename.c_str(), &buf) == 0 && S_ISREG(buf.st_mode);
#if defined(HAVE_STAT_ST_ATIM) && defined(HAVE_STAT_ST_MTIM) && defined(HAVE_STAT_ST_CTIM)
      mtime = static_cast<uint64_t>(buf.st_mtim.tv_sec) * 1000000 + buf.st_mtim.tv_nsec / 1000;
#elif defined(HAVE_STAT_ST_ATIMESPEC) && defined(HAVE_STAT_ST_MTIMESPEC) && defined(HAVE_STAT_ST_CTIMESPEC)
      mtime = static_cast<uint64_t>(buf.st_mtimespec.tv_sec) * 1000000 + buf.st_mtimespec.tv_nsec / 1000;
#else
      mtime = static_cast<uint64_t>(buf.st_mtime);
#endif
#endif
    }

    if (found)
    {
      // -n: add +line_number
      if (line_number > 0)
        command.append(" +").append(std::to_string(line_number));

      Screen::clear();

      FILE *pager = NULL;

      if (flag_stdin && filename == flag_label)
      {
        // standard input is viewed via a pipe to the pager
#ifdef OS_WIN
        pager = popen(command.c_str(), "wb");
#else
        pager = popen(command.c_str(), "w");
#endif
        if (pager != NULL)
          fwrite(stdin_buffer_.c_str(), 1, stdin_buffer_.size(), pager);
      }
      else if (!partname.empty())
      {
        // archive part is viewed via a pipe to the pager
#ifdef OS_WIN
        pager = popen(command.c_str(), "wb");
#else
        pager = popen(command.c_str(), "w");
#endif
        if (pager != NULL)
        {
          try
          {
            ugrep_extract(filename.c_str(), partname.c_str(), pager);
          }

          catch (...)
          {
            // this should never happen, but just in case we ignore errors
          }
        }
      }
      else
      {
        // view file in the pager
        command.append(" \"").append(filename).append("\"");
      }

      // pipe to pager was OK or execute the command
      if ((flag_stdin && filename == flag_label) || !partname.empty() ? pager != NULL : system(command.c_str()) == 0)
      {
#ifdef OS_WIN
        if (strcmp(flag_view, "more") == 0)
        {
          Screen::setpos(Screen::rows - 1, 0);
          Screen::put("(END) press a key");
          Screen::alert();
          VKey::flush();
          VKey::get();
        }
#endif

        bool changed = false;

        if (pager == NULL)
        {
          // check if file was changed by the pager (when it is an editor)
#ifdef OS_WIN
          _WIN32_FILE_ATTRIBUTE_DATA attr_after;
          changed = GetFileAttributesExW(utf8_decode(filename).c_str(), GetFileExInfoStandard, &attr_after) == 0 ||
            attr_before.ftLastWriteTime.dwLowDateTime != attr_after.ftLastWriteTime.dwLowDateTime ||
            attr_before.ftLastWriteTime.dwHighDateTime != attr_after.ftLastWriteTime.dwHighDateTime;
#else
          stat(filename.c_str(), &buf);
#if defined(HAVE_STAT_ST_ATIM) && defined(HAVE_STAT_ST_MTIM) && defined(HAVE_STAT_ST_CTIM)
          changed = (mtime != static_cast<uint64_t>(buf.st_mtim.tv_sec) * 1000000 + buf.st_mtim.tv_nsec / 1000);
#elif defined(HAVE_STAT_ST_ATIMESPEC) && defined(HAVE_STAT_ST_MTIMESPEC) && defined(HAVE_STAT_ST_CTIMESPEC)
          changed = (mtime != static_cast<uint64_t>(buf.st_mtimespec.tv_sec) * 1000000 + buf.st_mtimespec.tv_nsec / 1000);
#else
          changed = (mtime != static_cast<uint64_t>(buf.st_mtime));
#endif
#endif
        }
        else
        {
          // close the pipe to the pager
          pclose(pager);
          pager = NULL;
        }

        if (changed)
        {
          // file is changed, update the search results
          search();
          jump(ref);
        }
        else
        {
          redraw();
        }
      }
      else
      {
        Screen::alert();
        redraw();
        message(std::string("failed: ").append(command));
      }
    }
  }

  if (!found && (!filename.empty() || !partname.empty()))
  {
    std::string problem("cannot view or edit ");
    if (partname.empty())
      problem.append(filename);
    else
      problem.append(partname).append(" in ").append(filename);
    message(problem);
  }
}

// preview the file located under the cursor when the screen is split into multiple windows
void Query::preview()
{
  if (!flag_split || error_ != -1)
    return;

  bool found = false;
  std::string filename;
  std::string partname;

  if (rows_ > 0)
  {
    int ref = row_;

    // --tree with -c or -l: move down over non-filename lines to reach a filename
    if (flag_tree && (flag_files_with_matches || flag_count))
      while (ref + 1 < rows_ && (view_[ref].empty() || view_[ref].front() != '\0'))
        ++ref;

    // find a filename upwards, unless we're at an empty line
    if (!view_[ref].empty())
      while (ref >= 0 && !(found = find_filename(ref, filename, false, true, &partname)))
        --ref;

    // only a partname was present, no filename
    if (filename.empty())
      found = false;

    if (found && filename.back() == PATHSEPCHR)
    {
      found = false;
    }
    else if (!found && Static::arg_files.size() == 1)
    {
      // if not found an only one FILE argument was provided then preview FILE
      filename = Static::arg_files.front();
      found = true;
    }

    // cannot preview standard input
    if (found && flag_stdin && filename.compare(flag_label) == 0)
      found = false;
  }

  // no problems to report
  const char *problem = NULL;

  if (found)
  {
    // get line number if any, zero otherwise
    size_t from_lineno =
      flag_line_number &&
      !flag_files_with_matches &&
      !flag_count && !flag_hex &&
      !flag_with_hex ? get_line_number() : 0;

    // has the filename or partname or line number changed?
    if (from_lineno == 0 ||
        from_lineno != prevfrom_ ||
        filename != prevfile_ ||
        partname != prevpart_)
    {
      size_t max = Screen::rows - maxrows_ - 1;

      try
      {
        ugrep_find_text_preview(filename.c_str(), partname.c_str(), from_lineno, max, prevline_, prevnum_, preview_);
      }

      catch (...)
      {
        // this should never happen, but just in case we ignore errors
        prevnum_ = 0;
      }

      prevfile_.swap(filename);
      prevpart_.swap(partname);
      prevfrom_ = from_lineno;
    }

    if (prevnum_ == 0)
      problem = "[no matches to display]";

    if (problem == NULL)
    {
      char header[256];
      if (prevpart_.empty())
        snprintf(header, sizeof(header), "%s%s%s%s%s:%s%s%zu%s", color_off, color_fn, prevfile_.c_str(), color_off, color_se, color_off, color_ln, prevline_, color_off);
      else
        snprintf(header, sizeof(header), "%s%s%s{%s}%s%s:%s%s%zu%s", color_off, color_fn, prevfile_.c_str(), prevpart_.c_str(), color_off, color_se, color_off, color_ln, prevline_, color_off);
      Screen::put(maxrows_, 0, header);
      Screen::normal();

      // (re)display the (cached) preview text
      int row = maxrows_ + 1;
      for (size_t i = 0; i < prevnum_ && row < Screen::rows; ++i)
        row = Screen::put(row, 0, preview_[i], 0, 0);

      if (row < Screen::rows)
        Screen::end();
    }
  }
  else
  {
    problem = "[no file selected]";
  }

  if (problem != NULL)
  {
    Screen::normal();
    Screen::put(maxrows_, 0, problem);
    Screen::end();

    prevfile_.clear();
    prevpart_.clear();
    prevnum_ = 0;
    prevline_ = 0;
  }
}

// chdir one level down into the directory of the file located under the cursor or just above the screen
void Query::select()
{
  if (!selected_file_.empty())
  {
    Screen::alert();
    return;
  }

  int ref = select_ >= 0 ? select_ : row_;

  // --tree: move down over non-filename lines to reach a filename
  if (flag_tree && (flag_files_with_matches || flag_count))
  {
    while (ref + 1 < rows_ && (view_[ref].empty() || view_[ref].front() != '\0'))
      ++ref;
  }
  else
  {
    // move down to skip empty lines
    while (ref + 1 < rows_ && view_[ref].empty())
      ++ref;
  }

  std::string pathname;
  bool found = false;

  // move up until a filename header is found
  while (ref >= 0 && !(found = find_filename(ref, pathname, false, true)))
    --ref;

  if (pathname.empty())
    found = false;

  if (found)
  {
    // cannot view standard input
    if (flag_stdin && pathname == flag_label)
    {
      message("cannot chdir to standard input");
      return;
    }

    if (globbing_)
    {
      globbing_ = false;

      memcpy(line_, temp_, sizeof(Line));
      len_ = line_len();
      move(len_);

      set_prompt();
    }

    if (history_.empty())
      files_.swap(Static::arg_files);

    history_.emplace();
    history_.top().save(line_, col_, row_, flags_, mark_);

    mark_.reset();

    size_t n = pathname.find(PATHSEPCHR);
    size_t b = pathname.find('{'); // do not cd into archives
    if (n != std::string::npos && (b == std::string::npos || n < b))
    {
      std::string dir(pathname.substr(0, n + 1));

      if (chdir(dir.c_str()) < 0)
      {
        message("cannot chdir: operation denied");
        history_.pop();
        if (history_.empty())
          files_.swap(Static::arg_files);
        return;
      }

      dirs_.append(dir);
      pathname.erase(0, n + 1);

      // a directory change affects --hyperlink
      set_terminal_hyperlink();

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
      message("cannot chdir .. because standard input is searched");
      return;
    }

    if (selected_file_.empty() && !Static::arg_files.empty())
    {
      message("cannot chdir .. because file or directory arguments are present");
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
      char *cwd = getcwd0();
      if (cwd != NULL)
      {
        size_t n = strlen(cwd);
        dirs_.assign(cwd);
        wdir_.assign(cwd);
        if (n == 0 || cwd[n-1] != PATHSEPCHR)
          dirs_.append(PATHSEPSTR);
        free(cwd);
      }
    }

    if (chdir("..") < 0)
      return;

    // a directory change affects --hyperlink
    set_terminal_hyperlink();

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
  }
  else
  {
    size_t n = dirs_.find_last_of(PATHSEPCHR);

    if (n != std::string::npos)
      dirs_.resize(n + 1);
    else
      dirs_.clear();

    deselect_file_ = true;
    Static::arg_files.clear();
  }

  mark_.reset();

  if (!history_.empty())
  {
    int row;
    history_.top().restore(line_, col_, row, flags_, mark_);
    history_.pop();
    if (history_.empty())
      files_.swap(Static::arg_files);
    globbing_ = false;
    set_prompt();
    len_ = line_len();
    search();
    jump(row);
  }
  else
  {
    search();
  }
}

// chdir back to the starting working directory
void Query::unselect()
{
  if (!wdir_.empty())
  {
    if (chdir(wdir_.c_str()) < 0)
      return;

    // a directory change affects --hyperlink
    set_terminal_hyperlink();
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
        if (dirs_.size() == 3 && dirs_.at(1) == ':' && dirs_.at(2) == PATHSEPCHR)
          break;
#else
        if (dirs_ == PATHSEPSTR)
          break;
#endif

        if (chdir("..") < 0)
          break;

        dirs_.pop_back();

        size_t n = dirs_.find_last_of(PATHSEPCHR);

        if (n == std::string::npos)
          break;

        dirs_.resize(n + 1);
      }

      // a directory change affects --hyperlink
      set_terminal_hyperlink();
    }
  }

  dirs_.clear();
  wdir_.clear();
  deselect_file_ = true;
  Static::arg_files.clear();

  mark_.reset();

  if (!history_.empty())
  {
    while (history_.size() > 1)
      history_.pop();
    int row;
    history_.top().restore(line_, col_, row, flags_, mark_);
    history_.pop();
    files_.swap(Static::arg_files);
    globbing_ = false;
    set_prompt();
    len_ = line_len();
    search();
    jump(row);
  }
  else
  {
    search();
  }
}

// display a message
void Query::message(const std::string& message)
{
  Screen::normal();
  if (!Screen::mono)
    Screen::put(color_qp);
  Screen::put(0, 0, "-> ");
  Screen::normal();
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

  bool ctrl_q = false; // CTRL-Q is pressed
  bool ctrl_o = false; // CTRL-O is pressed
  bool restart = false; // META key pressed, restart search when exiting the help screen

  while (true)
  {
    int key;

    Screen::put(0, Screen::cols - 1, "?");

#ifdef OS_WIN

    while (true)
    {
      key = VKey::in(500);

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
      ctrl_o = false;
      restart = true;
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

        case VKey::CTRL_BS:
#ifdef OS_WIN
          GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
#else
          raise(SIGTERM);
#endif
          break;

        case VKey::CTRL_RB: // CTRL-]: toggle colors/mono
          Screen::mono = !Screen::mono;
          redraw();
          break;

        case VKey::META:
          meta(VKey::get());
          restart = true;
          break;

        default:
          if (key < 0x80)
          {
            meta(key);
            restart = true;
          }
          else
          {
            Screen::alert();
          }
      }
    }
  }

  mode_ = oldMode;

  message_ = false;

  Screen::clear();
  redraw();

  if (restart)
    search();

  return ctrl_q;
}

// Alt/Meta/Option key
void Query::meta(int key)
{
  if (key == 'E' || key == 'Q')
  {
    if (flags_[5].flag || flags_[6].flag || flags_[17].flag || flags_[30].flag)
    {
      // option -E: reset -F, -G, -P, -Z to switch back to normal mode (the Q> prompt)
      flags_[5].flag = false;
      flags_[6].flag = false;
      flags_[17].flag = false;
      flags_[30].flag = false;

      // search when in QUERY mode, otherwise refresh HELP display
      if (mode_ == Mode::QUERY)
        search();
      else
        redraw();

      std::string msg;

      msg.assign("\033[7mM-E\033[m extended regex \033[32;1mon\033[m");

      message(msg);

      set_prompt();
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
            flags_[4].flag = false;
            flags_[14].flag = false;
            flags_[29].flag = false;
            break;

          case 'B':
            flags_[0].flag = false;
            flags_[3].flag = false;
            flags_[4].flag = false;
            flags_[14].flag = false;
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
            flags_[4].flag = false;
            flags_[14].flag = false;
            flags_[29].flag = false;
            break;

          case 'c':
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[2].flag = false;
            flags_[3].flag = false;
            flags_[13].flag = false;
            flags_[14].flag = false;
            flags_[16].flag = false;
            flags_[29].flag = false;
            break;

          case 'F':
            flags_[6].flag = false;
            flags_[17].flag = false;
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
            flags_[0].flag = false;
            flags_[1].flag = false;
            flags_[2].flag = false;
            flags_[3].flag = false;
            flags_[4].flag = false;
            flags_[13].flag = false;
            flags_[16].flag = false;
            flags_[29].flag = false;
            break;

          case 'o':
            flags_[4].flag = false;
            flags_[14].flag = false;
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
            flags_[4].flag = false;
            flags_[14].flag = false;
            flags_[16].flag = false;
            break;

          case 'Z':
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
            flags_[46].flag = false;
            flags_[47].flag = false;
            flags_[48].flag = false;
            break;

          case '#':
            flags_[45].flag = false;
            flags_[47].flag = false;
            flags_[48].flag = false;
            break;

          case '%':
            flags_[45].flag = false;
            flags_[46].flag = false;
            flags_[48].flag = false;
            break;

          case '@':
            flags_[45].flag = false;
            flags_[46].flag = false;
            flags_[47].flag = false;
            break;
        }
      }
      else
      {
        switch (key)
        {
          case 'n':
            if (flags_[4].flag || flags_[14].flag)
            {
              flags_[4].flag = false;
              flags_[14].flag = false;
              flags_[15].flag = false;
            }
            break;
          case 'R':
          case 'r':
            for (int i = 33; i <= 41; ++i)
              flags_[i].flag = false;
            break;
        }
      }

      if (key == 'g')
      {
        if (mode_ == Mode::QUERY)
        {
          if (!globbing_)
          {
            globbing_ = true;

            memcpy(temp_, line_, sizeof(Line));
            size_t num = globs_.size();
            if (num >= sizeof(Line))
              num = sizeof(Line) - 1;
            memcpy(line_, globs_.c_str(), num);
            line_[num] = '\0';

            len_ = line_len();
            move(len_);

            set_prompt();
          }
          else
          {
            globbing_ = false;

            memcpy(line_, temp_, sizeof(Line));
            len_ = line_len();
            move(len_);

            set_prompt();
          }

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
        msg.assign("\033[7mM- \033[m ").append(fp->text);
        msg[6] = fp->key;

        if (key == '[')
        {
          if (!flags_[26].flag || flag_hexdump == NULL)
            if (!flags_[0].flag && !flags_[1].flag)
              flags_[3].flag = true;

          if (flags_[16].flag)
          {
            if (only_context_ > 1)
              --only_context_;
            msg.append(" to ").append(std::to_string(only_context_));
          }
          else
          {
            if (context_ > 1)
              --context_;
            msg.append(" to ").append(std::to_string(context_));
          }
          flags_[4].flag = false;
          flags_[14].flag = false;
        }
        else if (key == ']')
        {
          if (flags_[26].flag && flag_hexdump != NULL)
          {
            ++context_;
            msg.append(" to ").append(std::to_string(context_));
          }
          else if (flags_[16].flag)
          {
            if (flags_[0].flag || flags_[1].flag || flags_[3].flag)
              ++only_context_;
            else if (!flags_[0].flag && !flags_[1].flag)
              flags_[3].flag = true;
            msg.append(" to ").append(std::to_string(only_context_));
          }
          else
          {
            if (flags_[0].flag || flags_[1].flag || flags_[3].flag)
              ++context_;
            else if (!flags_[0].flag && !flags_[1].flag)
              flags_[3].flag = true;
            msg.append(" to ").append(std::to_string(context_));
          }
          flags_[4].flag = false;
          flags_[14].flag = false;
        }
        else if (key == '{')
        {
          flags_[30].flag = true;

          if ((fuzzy_ & 0xff) > 1)
            fuzzy_ = ((fuzzy_ & 0xff) - 1) | (fuzzy_ & 0xff00);

          msg.append(" to ").append(std::to_string(fuzzy_ & 0xff));
        }
        else if (key == '}')
        {
          if (flags_[30].flag && (fuzzy_ & 0xff) < 0xff)
            fuzzy_ = ((fuzzy_ & 0xff) + 1) | (fuzzy_ & 0xff00);
          else
            flags_[30].flag = true;

          msg.append(" to ").append(std::to_string(fuzzy_ & 0xff));
        }
        else
        {
          fp->flag = !fp->flag;

          msg.append(fp->flag ? " \033[32;1mon\033[m" : " \033[31;1moff\033[m");
        }

        // search when in QUERY mode, otherwise refresh HELP display
        if (mode_ == Mode::QUERY)
          search();
        else
          redraw();

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

// print selected query results when done
void Query::print()
{
  int i = 0;

  // output selected query results
  while (i < rows_)
  {
    if (selected_[i])
      if (!print(view_[i]))
        return;
    view_[i].clear();
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

    while (!eof_ || buflen_ > 0)
    {
      // if not appending to a row, start over at the begin of the view to conserve memory
      if (!append_)
        rows_ = 0;

      i = rows_;

      // fetch a bunch more lines, if no more lines to fetch then break
      fetch(rows_ + 1000);

      while (i < rows_)
      {
        if (!print(view_[i]))
          return;
        view_[i].clear();
        ++i;
      }
    }
  }
}

// print one row of query results, return false on failure
bool Query::print(const std::string& line)
{
  if (line.empty())
    return fwrite("\n", 1, 1, stdout) == 1;

  const char *text = line.c_str();
  const char *end = text + line.size();

  // how many NULs to ignore that are part of the pathname marking in headers?
  int nulls = *text == '\0' && !flag_text ? 2 : 0;

  if (nulls > 0)
    ++text;

  const char *ptr = text;

  // if output should not be colored or colors are turned off, then output the selected line without its CSI sequences
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
  if (flag_hexdump != NULL)
  {
    if (flag_hex_after > 0)
      context_ = flag_hex_after;
    else if (flag_hex_before > 0)
      context_ = flag_hex_before;
    else
      context_ = 0;
  }
  else if (flag_only_matching)
  {
    if (flag_after_context > 0)
      only_context_ = flag_after_context;
    else if (flag_before_context > 0)
      only_context_ = flag_before_context;
  }
  else
  {
    if (flag_after_context > 0)
      context_ = flag_after_context;
    else if (flag_before_context > 0)
      context_ = flag_before_context;
  }

  // remember the --fuzzy max, when specified
  if (flag_fuzzy > 0)
    fuzzy_ = flag_fuzzy;

  // remember --dotall
  dotall_ = flag_dotall;

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

  // get the --iglob
  for (auto& glob : flag_iglob)
  {
    if (!glob.empty())
    {
      if (!globs_.empty())
        globs_.push_back(',');
      globs_.append(glob);
    }
  }

  // if any --iglob is specified, make all globs case-insensitive
  if (!flag_iglob.empty())
    flag_glob_ignore_case = true;

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
  flags_[42].flag = flag_bool;
  flags_[43].flag = flag_hidden;
  flags_[44].flag = flag_heading;
  flags_[45].flag = flag_sort && (strcmp(flag_sort, "best") == 0 || strcmp(flag_sort, "rbest") == 0);
  flags_[46].flag = flag_sort && (strcmp(flag_sort, "size") == 0 || strcmp(flag_sort, "rsize") == 0);
  flags_[47].flag = flag_sort && (strcmp(flag_sort, "changed") == 0 || strcmp(flag_sort, "changed") == 0);
  flags_[48].flag = flag_sort && (strcmp(flag_sort, "created") == 0 || strcmp(flag_sort, "created") == 0);
  flags_[49].flag = flag_sort && *flag_sort == 'r';
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

  // restore flags that may have changed by the last search
  flag_dotall = dotall_;

  // suppress warning messages
  flag_no_messages = true;

  // set ugrep flags to the interactive flags
  if (flags_[26].flag && flag_hexdump != NULL)
  {
    flag_hex_after = flag_hex_before = context_;
    flag_after_context = flag_before_context = 0;
  }
  else
  {
    if (flags_[16].flag)
    {
      flag_after_context = only_context_ * (flags_[0].flag || flags_[3].flag);
      flag_before_context = only_context_ * (flags_[1].flag || flags_[3].flag);
    }
    else
    {
      flag_after_context = context_ * (flags_[0].flag || flags_[3].flag);
      flag_before_context = context_ * (flags_[1].flag || flags_[3].flag);
    }
    if (flag_hexdump != NULL)
    {
      flag_hex_after = (flag_after_context == 0);
      flag_hex_before = (flag_before_context == 0);
    }
  }
  flag_byte_offset = flags_[2].flag;
  flag_count = flags_[4].flag;
  flag_fixed_strings = flags_[5].flag;
  flag_basic_regexp = flags_[6].flag;
  flag_glob.clear();
  flag_iglob.clear();
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
    flag_directories_action = Action::UNSP;
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
  flag_bool = flags_[42].flag;
  flag_hidden = flags_[43].flag;
  flag_heading = flags_[44].flag;
  if (flags_[45].flag)
    flag_sort = flags_[49].flag ? "rbest" : "best";
  else if (flags_[46].flag)
    flag_sort = flags_[49].flag ? "rsize" : "size";
  else if (flags_[47].flag)
    flag_sort = flags_[49].flag ? "rchanged" : "changed";
  else if (flags_[48].flag)
    flag_sort = flags_[49].flag ? "rcreated" : "created";
  else
    flag_sort = flags_[49].flag ? "rname" : "name";
}

void Query::set_prompt()
{
  if (globbing_)
  {
    if (flag_glob_ignore_case)
      prompt_.assign("--iglob=");
    else
      prompt_.assign("--glob=");
  }
  else
  {
    prompt_.clear();

    if (!flag_file.empty())
      prompt_.append("file");

    const char *mode;

    if (flags_[5].flag)
    {
      if (flags_[30].flag)
        mode = "FZ>";
      else
        mode = "F>";
    }
    else if (flags_[6].flag)
    {
      mode = "G>";
    }
    else if (flags_[17].flag)
    {
      mode = "P>";
    }
    else if (flags_[30].flag)
    {
      mode = "Z>";
    }
    else
    {
      mode = "Q>";
    }

    if (flags_[42].flag)
      prompt_.append("bool").append(mode);
    else
      prompt_.append(mode);
  }
}

void Query::get_stdin()
{
  // if standard input is searched, then buffer all its text
  if (flag_stdin)
  {
    reflex::BufferedInput input(stdin, flag_encoding_type);

    while (true)
    {
      size_t len = input.get(buffer_, QUERY_BUFFER_SIZE);
      if (len == 0)
        break;
      stdin_buffer_.append(buffer_, len);
    }
  }
}

void Query::set_stdin()
{
  // if standard input is searched, start thread to reproduce its text on demand
  if (flag_stdin)
  {
    // close the stdin pipe when open
    if (Static::source != stdin && Static::source != NULL)
    {
      fclose(Static::source);
      Static::source = NULL;
    }

    // join the stdin_sender
    if (stdin_thread_.joinable())
      stdin_thread_.join();

    // create a new pipe
    if (pipe(stdin_pipe_) < 0)
    {
      what_.assign("cannot create pipe");
      // error at the end of the line, not within
      error_ = line_wsize();
      return;
    }

    // and assign it to the source
    Static::source = fdopen(stdin_pipe_[0], "rb");

    // run stdin_sender in the background to push buffered standard input down the pipe
    stdin_thread_ = std::thread(Query::stdin_sender, stdin_pipe_[1]);
  }
}

// push standard input down the specified pipe fd
ssize_t Query::stdin_sender(int fd)
{
  // write the stdin data all at once, we can ignore the return value
  ssize_t nwritten = write(fd, stdin_buffer_.c_str(), stdin_buffer_.size());

  close(fd);

  return nwritten;
}

// true if view_[ref] has a valid filename/filepath identified by three \0 markers and differs from the given filename, then assigns filename
bool Query::find_filename(int ref, std::string& filename, bool compare_dir, bool find_path, std::string *partname)
{
  // extract the new filename to compare with and assign to the old filename
  std::string new_filename;
  size_t start;
  size_t pos;
  if (!get_filename(ref, new_filename, start, pos))
    return false;

  // if we need a pathname but we are at a marked group separator, then return
  if (find_path && (flag_after_context > 0 || flag_before_context > 0) &&
      flag_group_separator != NULL &&
      new_filename.compare(flag_group_separator) == 0)
    return false;

  // if the new filename is an archive part name, then assign it to the partname when applicable
  if (find_path && flag_decompress && new_filename.front() == '{' && new_filename.back() == '}')
  {
    if (partname == NULL)
      return false;

    // swap partname for filename sans braces
    new_filename.erase(0, 1);
    new_filename.pop_back();
    partname->swap(new_filename);

    // try to find the filename in a header above
    while (--ref >= 0)
      if (get_filename(ref, filename, start, pos) && filename.front() != '{')
        break;

    if (ref < 0)
      filename.clear();

    return true;
  }

  // extract the archive partname, if any
  if (partname != NULL)
  {
    // -z: get the archive partname when present
    if (flag_decompress)
    {
      // skip ANSI escape codes when present
      start = ++pos;
      const std::string& line = view_[ref];
      size_t end = line.size();
      while (pos < end && (pos = skip_escapes(line, pos)) > start)
        start = pos;

      // extract the part from {part} or {path:part}
      if (pos < end && line.at(pos) == '{')
      {
        start = ++pos;

        while (pos < end && line.at(pos) != '}')
          ++pos;

        if (start < pos && pos < end)
          partname->assign(line.substr(start, pos - start));
      }
    }
  }

  // --tree: the new filename is the basename, we should reconstruct the new pathname from the tree in view_[]
  if (flag_tree && (flag_files_with_matches || flag_count))
  {
    size_t last_start = start;
    std::string dir;

    // while the top directory was not found (works since no colors are used for directory names)
    while (start > 2 && --ref >= 0)
    {
      if (view_[ref].size() <= 1)
        break;

      if (!get_filename(ref, dir, start, pos))
        continue;

      // if line scanned represents a parent directory, then prepend the directory to the new filename
      if (start < last_start && dir.back() == PATHSEPCHR)
      {
        new_filename.insert(0, dir);
        last_start = start;
      }
    }
  }

  if (compare_dir)
  {
    // the new filename must not be in the same directory as the old filename
    size_t skip = 0;
#ifdef OS_WIN
    if (filename.size() >= 3 && filename.at(1) == ':' && filename.at(2) == PATHSEPCHR)
      skip = 3;
#endif
    size_t pos1 = new_filename.find(PATHSEPCHR, skip);
    size_t pos2 = filename.find(PATHSEPCHR, skip);

    if (pos1 == pos2 && (pos1 == std::string::npos || new_filename.compare(0, pos1, filename, 0, pos2) == 0))
      return false; // the extracted filename is the same or is in the same directory as the old filename
  }
  else if ((flag_after_context > 0 || flag_before_context > 0) &&
      flag_group_separator != NULL &&
      new_filename.compare(flag_group_separator) == 0)
  {
    // -ABC: jump to the group-separator (default is '--') unless we find_path only then ignore group separators
    return true;
  }
  else if (new_filename.compare(filename) == 0)
  {
    // the new filename is the same as the old filename
    return false;
  }

  filename.swap(new_filename);

  return true;
}

// get filename from the output, return position at last \0 marker or -1 when not found
bool Query::get_filename(int ref, std::string& filename, size_t& start, size_t& pos)
{
  const std::string& line = view_[ref];
  size_t end = line.size();

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

  filename = line.substr(start, pos - start);

  return true;
}

// get line number from the output, assumes option -n is active
size_t Query::get_line_number()
{
  int row = select_ >= 0 ? select_ : row_;

  // look at the current line and the one below it
  for (int i = row; i < rows_ && i <= row + 1; ++i)
  {
    const std::string& line = view_[i];
    size_t pos = 0;
    size_t end = line.size();

    // skip over pathname or group separator when present
    if (line.front() == '\0')
    {
      while (++pos < end && line.at(pos) != '\0')
        continue;

      while (++pos < end && line.at(pos) != '\0')
        continue;

      ++pos;
    }

    while (true)
    {
      // skip over ANSI escape sequence when present
      pos = skip_escapes(line, pos);

      // skip archive part
      if (pos < end && line.at(pos) == '{')
        while (pos < end && line.at(pos) != '}')
          ++pos;

      // at end?
      if (pos >= end)
        break;

      // found the line number?
      if (isdigit(line.at(pos)))
        return static_cast<size_t>(strtoull(line.c_str() + pos, NULL, 10));

      ++pos;
    }
  }

  return 0;
}

Query::Mode                Query::mode_                = Query::Mode::QUERY;
bool                       Query::updated_             = false;
bool                       Query::message_             = false;
Query::Line                Query::line_                = { '\0' };
Query::Line                Query::temp_                = { '\0' };
std::string                Query::prompt_;
int                        Query::start_               = 0;
int                        Query::col_                 = 0;
int                        Query::len_                 = 0;
int                        Query::offset_              = 0;
int                        Query::shift_               = 8;
std::atomic_int            Query::error_;
std::string                Query::what_;
int                        Query::row_                 = 0;
int                        Query::rows_                = 0;
int                        Query::maxrows_;
Query::State               Query::mark_;
int                        Query::select_              = -1;
bool                       Query::select_all_          = false;
bool                       Query::globbing_            = false;
std::string                Query::globs_;
std::string                Query::dirs_;
std::string                Query::wdir_;
std::string                Query::prevfile_;
std::string                Query::prevpart_;
size_t                     Query::prevfrom_;
size_t                     Query::prevline_;
std::vector<std::string>   Query::preview_;
size_t                     Query::prevnum_;
bool                       Query::deselect_file_;
std::string                Query::selected_file_;
std::stack<Query::History> Query::history_;
std::vector<const char*>   Query::files_;
int                        Query::skip_                = 0;
std::vector<std::string>   Query::view_;
std::vector<bool>          Query::selected_;
bool                       Query::eof_                 = true;
bool                       Query::append_              = false;
size_t                     Query::buflen_              = 0;
char                       Query::buffer_[QUERY_BUFFER_SIZE];
int                        Query::search_pipe_[2];
std::thread                Query::search_thread_;
std::string                Query::stdin_buffer_;
int                        Query::stdin_pipe_[2];
std::thread                Query::stdin_thread_;
size_t                     Query::searched_            = 0;
size_t                     Query::found_               = 0;
int                        Query::tick_                = 0;
int                        Query::spin_                = 0;
size_t                     Query::context_             = 2;
size_t                     Query::only_context_        = 20;
size_t                     Query::fuzzy_               = 1;
bool                       Query::dotall_              = false;

#ifdef OS_WIN

HANDLE                     Query::hPipe_;
OVERLAPPED                 Query::overlapped_;
bool                       Query::blocking_;
bool                       Query::pending_;

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
  { false, '%', "Boolean queries" },
  { false, '.', "include hidden" },
  { false, '+', "show heading" },
  { false, '~', "sort by best" },
  { false, '#', "sort by size" },
  { false, '$', "sort by changed" },
  { false, '@', "sort by created" },
  { false, '^', "reverse sort" },
  { false, '[', "decrease context" },
  { false, ']', "increase context" },
  { false, '{', "decrease fuzziness" },
  { false, '}', "increase fuzziness" },
  { false, 0, NULL, }
};
