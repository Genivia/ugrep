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
@file      query.hpp
@brief     Query engine and UI
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef QUERY_HPP
#define QUERY_HPP

#include "ugrep.hpp"
#include "screen.hpp"
#include "vkey.hpp"

#include <cerrno>
#include <thread>
#include <list>
#include <stack>

// max length of the query line to edit
#ifndef QUERY_MAX_LEN
#define QUERY_MAX_LEN 1024
#endif

// size of the chunks of data to buffer when received from the search thread's pipe
#ifndef QUERY_BUFFER_SIZE
#define QUERY_BUFFER_SIZE 16384
#endif

// default -Q response to keyboard input delay is 0.5s, in steps of 100ms
#ifndef DEFAULT_QUERY_DELAY
#define DEFAULT_QUERY_DELAY 5
#endif

// the max time that a message (to confirm a command) is shown at the query line, in steps of 100ms
#ifndef QUERY_MESSAGE_DELAY
#define QUERY_MESSAGE_DELAY 12
#endif

class Query {

 public:

  static void query();

 protected:

  enum class Mode { QUERY, LIST, EDIT, HELP };

  typedef char Line[QUERY_MAX_LEN];

  // interactive flags correspond to the command-line options
  struct Flags {
    bool        flag; // on/off
    int         key;  // key press to toggle this flag
    const char *text; // description to show with CTRL-Z
  };

  // bookmark state saved with CTRL-X to restore with CTRL-R
  struct State {

    State()
    {
      unset();
    }

    void unset()
    {
      *line = '\0';
      col = 0;
      row = -1;
    }

    void save(const Line& line_, int col_, int row_, const Flags flags_[])
    {
      memcpy(line, line_, sizeof(Line));
      col = col_;
      row = row_;
      set.clear();
      for (int i = 0; flags_[i].text != NULL; ++i)
        set.push_back(flags_[i].flag);
    }

    bool restore(Line& line_, int& col_, int& row_, Flags flags_[])
    {
      row_ = 0;

      if (row < 0)
        return false;

      bool changed = (strcmp(line, line_) != 0);

      memcpy(line_, line, sizeof(Line));
      col_ = col;
      row_ = row;
      for (int i = 0; flags_[i].text != NULL; ++i)
      {
        if (flags_[i].flag != set[i])
        {
          flags_[i].flag = set[i];
          changed = true;
        }
      }

      return changed;
    }

    Line              line; // saved line_[]
    int               col;  // saved col_
    int               row;  // saved row_
    std::vector<bool> set;  // vector of bool flags to save and restore flags_[]

  };

  // history to restore pattern, bookmark and option upon SHIFT-TAB
  struct History : public State {

    void save(const Line& line_, int col_, int row_, const Flags flags_[], const State& mark_)
    {
      State::save(line_, col_, row_, flags_);
      mark = mark_;
    }

    void restore(Line& line_, int& col_, int& row_, Flags flags_[], State& mark_)
    {
      State::restore(line_, col_, row_, flags_);
      mark_ = mark;
    }

    State mark; // saved bookmark state

  };

  static void query_ui();

  static char *line_ptr(int col);

  static char *line_ptr(int col, int pos);

  static char *line_end();

  static int line_pos();

  static int line_len();

  static int line_wsize();

  static void display(int col, int len);

  static void draw();

  static void disp(int row);

  static void redraw();

#ifdef OS_WIN

  static BOOL WINAPI sigint(DWORD);

#else

  static void sigwinch(int);

  static void sigint(int);

#endif

  static void move(int pos);

  static void insert(const char *text, size_t size);

  static void insert(int ch);

  static void erase(int num);

  static void search();

  static bool update();

  static void fetch(int row);

  static void fetch_all();

  static void execute(int fd);

  static void load_line();

  static void save_line();

  static void up();

  static void down();

  static void pgup(bool half_page = false);

  static void pgdn(bool half_page = false);

  static void back();

  static void next();

  static void jump(int row);

  static void view();

  static void select();

  static void deselect();

  static void unselect();

  static void message(const std::string& message);

  static bool confirm(const char *prompt);

  static bool help();

  static void meta(int key);

  static bool selections();

  static void save();

  static void print();

  static bool print(const std::string& line);

  static void get_flags();

  static void set_flags();

  static void set_prompt();

  static void get_stdin();

  static void set_stdin();

  static ssize_t stdin_sender(int fd);

  static bool is_filename(const std::string& line, std::string& filename, bool compare_dir = false);

  static Mode                     mode_;
  static bool                     updated_;
  static bool                     message_;
  static Line                     line_;
  static Line                     temp_;
  static std::string              prompt_;
  static int                      start_;
  static int                      col_;
  static int                      len_;
  static int                      offset_;
  static int                      shift_;
  static std::atomic_int          error_;
  static std::string              what_;
  static int                      row_;
  static int                      rows_;
  static State                    mark_;
  static int                      skip_;
  static int                      select_;
  static bool                     select_all_;
  static bool                     globbing_;
  static std::string              globs_;
  static std::string              dirs_;
  static std::string              wdir_;
  static bool                     deselect_file_;
  static std::string              selected_file_;
  static std::stack<History>      history_;
  static std::vector<std::string> view_;
  static std::list<std::string>   saved_;
  static std::vector<bool>        selected_;
  static bool                     eof_;
  static bool                     append_;
  static size_t                   buflen_;
  static char                     buffer_[QUERY_BUFFER_SIZE];
  static int                      search_pipe_[2];
  static std::thread              search_thread_;
  static std::string              stdin_buffer_;
  static int                      stdin_pipe_[2];
  static std::thread              stdin_thread_;
  static char                     searching_[16];
  static int                      dots_;
  static size_t                   context_;
  static size_t                   only_context_;
  static size_t                   fuzzy_;
  static bool                     dotall_;

#ifdef OS_WIN

  static HANDLE                   hPipe_;
  static OVERLAPPED               overlapped_;
  static bool                     blocking_;
  static bool                     pending_;

#endif

  static Flags                    flags_[];

};

#endif
