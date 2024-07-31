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
@copyright (c) 2019,2024, Robert van Engelen, Genivia Inc. All rights reserved.
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

// default -Q response to keyboard input delay is 3 for 300ms, in steps of 100ms
#ifndef DEFAULT_QUERY_DELAY
#define DEFAULT_QUERY_DELAY 3
#endif

// the max time that a message (to confirm a command) is shown at the query line, in steps of 100ms
#ifndef QUERY_MESSAGE_DELAY
#define QUERY_MESSAGE_DELAY 15
#endif

class Query {

 public:

  static void query();

 protected:

  enum class Mode { QUERY, HELP };

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
      reset();
    }

    void reset()
    {
      *line = '\0';
      col = 0;
      row = -1;
    }

    bool is_set()
    {
      return row != -1;
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

  static void status(bool show);

  static bool update();

  static bool fetch(int row);

  static void execute(int fd);

  static void up();

  static void down();

  static void pgup(bool half_page = false);

  static void pgdn(bool half_page = false);

  static void back();

  static void next();

  static void jump(int row);

  static void view();

  static void preview();

  static void select();

  static void deselect();

  static void unselect();

  static void message(const std::string& message);

  static bool confirm(const char *prompt);

  static bool help();

  static void meta(int key);

  static bool selections();

  static void print();

  static bool print(const std::string& line);

  static void get_flags();

  static void set_flags();

  static void set_prompt();

  static void get_stdin();

  static void set_stdin();

  static ssize_t stdin_sender(int fd);

  static bool find_filename(int ref, std::string& filename, bool compare_dir = false, bool find_path = false, std::string *partname = NULL);

  static bool get_filename(int ref, std::string& filename, size_t& start, size_t& pos);

  static size_t get_line_number();

  static Mode                     mode_;        // query TUI mode
  static bool                     updated_;     // true when the query is updated requiring a new search
  static bool                     message_;     // true when a message is displayed
  static Line                     line_;        // query line of QUERY_MAX_LEN chars
  static Line                     temp_;        // temporary buffer to hold query line to enter globs instead
  static std::string              prompt_;      // query line prompt
  static int                      start_;       // starting column of the query line
  static int                      col_;         // column position in the query line of the cursor
  static int                      len_;         // length of the query line
  static int                      offset_;      // offset of the query line displayed when the left part is out of view
  static int                      shift_;       // horizontal shift amount of the query line from the screen edges
  static std::atomic_int          error_;       // error position in the pattern or -1 if no error
  static std::string              what_;        // what happened when an error occurred
  static int                      row_;         // current row of view_[]
  static int                      rows_;        // number of rows_ stored in view_[]
  static int                      maxrows_;     // max number of screen rows, normally Screen::rows
  static State                    mark_;        // bookmark state
  static int                      skip_;        // skipped left text output amount, to pan the screen left/right
  static int                      select_;      // select output or -1 if selecting is not active
  static bool                     select_all_;  // selecting with A (select all)
  static bool                     globbing_;    // true when specifying a glob pattern
  static std::string              globs_;       // the glob patterns specified
  static std::string              dirs_;        // dir path to display before the prompt when we chdir into dirs
  static std::string              wdir_;        // the working directory
  static std::string              prevfile_;    // the preview filename
  static std::string              prevpart_;    // the preview archive partname
  static size_t                   prevfrom_;    // the preview line number to search from
  static size_t                   prevline_;    // the preview line number
  static std::vector<std::string> preview_;     // the preview text to display in the split screen bottom half
  static size_t                   prevnum_;     // the number of previous text entries
  static bool                     deselect_file_;
  static std::string              selected_file_;
  static std::stack<History>      history_;     // tabbing history
  static std::vector<const char*> files_;       // saved arg_files command line FILEs to search
  static std::vector<std::string> view_;        // search output text to display, incrementally fetched
  static std::vector<bool>        selected_;    // marked lines in view_[] selected in selection mode
  static bool                     eof_;         // end of results, no more results can be fetched
  static bool                     append_;
  static size_t                   buflen_;
  static char                     buffer_[QUERY_BUFFER_SIZE];
  static int                      search_pipe_[2];
  static std::thread              search_thread_;
  static std::string              stdin_buffer_;
  static int                      stdin_pipe_[2];
  static std::thread              stdin_thread_;
  static size_t                   searched_;    // last update number of files searched
  static size_t                   found_;       // last update number of files found
  static int                      tick_;        // 100ms tick 0 to 7 or steady 8
  static int                      spin_;        // spinner state
  static std::vector<std::string> exclude_;     // saved --exclude
  static std::vector<std::string> exclude_dir_; // saved --exclude-dir
  static std::vector<std::string> include_;     // saved --include
  static std::vector<std::string> include_dir_; // saved --include-dir
  static std::vector<std::string> file_magic_;  // saved --file-magic
  static std::set<std::string> ignore_files_;   // saved --ignore-files
  static size_t                   context_;     // saved -ABC context size
  static size_t                   only_context_;// saved -o with -ABC context size
  static size_t                   fuzzy_;       // saved --fuzzy fuzzy distance
  static bool                     dotall_;      // saved --dotall flag

#ifdef OS_WIN

  static HANDLE                   hPipe_;
  static OVERLAPPED               overlapped_;
  static bool                     blocking_;
  static bool                     pending_;

#endif

  static Flags                    flags_[];

};

#endif
