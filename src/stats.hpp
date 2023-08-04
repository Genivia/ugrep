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
@file      stats.hpp
@brief     collect global statistics - static, partially thread-safe (see below)
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2022, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt

Thread-safe functions:

Stats::found_file()
Stats::found_part()
Stats::found_files()
Stats::found_parts()
Stats::found_any_file()

*/

#ifndef STATS_HPP
#define STATS_HPP

#include "ugrep.hpp"
#include <reflex/timer.h>

// static class to collect global statistics
class Stats {

 public:

  // reset stats
  static void reset()
  {
    reflex::timer_start(timer);
    files = 0;
    dirs = 0;
    indexed = 0;
    skipped = 0;
    changed = 0;
    added = 0;
    fileno = 0;
    partno = 0;
    lineno = 0;
    matchno = 0;
    ignore.clear();
  }

  // score a file searched
  static void score_file()
  {
    ++files;
  }

  // score a directory searched
  static void score_dir()
  {
    ++dirs;
  }

  // score an indexed file
  static void score_indexed()
  {
    ++indexed;
  }

  // score an indexed file that was skipped from search
  static void score_skipped()
  {
    ++skipped;
  }

  // score an indexed file that was changed and searched (stale index file)
  static void score_changed()
  {
    ++changed;
  }

  // score a non-indexed file that was added and searched (stale index file)
  static void score_added()
  {
    ++added;
  }

  // score matches
  static void score_matches(size_t matches, size_t lines)
  {
    matchno += matches;
    lineno += lines;
  }

  // number of files searched
  static size_t searched_files()
  {
    return files;
  }

  // number of directories visited
  static size_t searched_dirs()
  {
    return dirs;
  }

  // number of lines searched
  static size_t searched_lines()
  {
    return lineno;
  }

  // atomically update the number of matching files found, excluding files in archives returns true if max file matches (+ number of threads-1 when sorting) is not reached yet
  static bool found_file()
  {
    size_t n = fileno.fetch_add(1, std::memory_order_relaxed);
    return flag_max_files > 0 ? n < flag_max_files : true;
  }

  // atomically update the number of matching files found, including files in archives, returns true if max file matches (+ number of threads-1 when sorting) is not reached yet
  static bool found_part()
  {
    size_t n = partno.fetch_add(1, std::memory_order_relaxed);
    return flag_max_files > 0 ? n < flag_max_files : true;
  }

  // undo found_part
  static void undo_found_part()
  {
    partno.fetch_sub(1, std::memory_order_relaxed);
  }

  // the number of matching files found, excluding files in archives
  static size_t found_files()
  {
    size_t n = fileno.load(std::memory_order_relaxed);
    return flag_max_files > 0 ? std::min(n, flag_max_files) : n;
  }

  // the number of matching files found, including files in archives
  static size_t found_parts()
  {
    size_t n = partno.load(std::memory_order_relaxed);
    return flag_max_files > 0 ? std::min(n, flag_max_files) : n;
  }

  // any matching file was found
  static bool found_any_file()
  {
    return fileno > 0;
  }

  // found matches
  static size_t found_matches()
  {
    return matchno;
  }

  // a .gitignore or similar file was encountered
  static void ignore_file(const std::string& filename)
  {
    ignore.emplace_back(filename);
  }

  // report the statistics
  static void report(FILE *output);

 protected:

  static reflex::timer_type       timer;   // elapsed wall-clock time in milli seconds (ms)
  static size_t                   files;   // number of files searched, excluding files in archives
  static size_t                   dirs;    // number of directories searched
  static size_t                   indexed; // number of files found to be indexed
  static size_t                   skipped; // number of files found to be indexed that were skipped as not matching
  static size_t                   changed; // number of files found to be indexed but changed (stale index file)
  static size_t                   added;   // number of files found to be added (stale index file)
  static std::atomic_size_t       fileno;  // number of matching files, excluding files in archives, atomic for GrepWorker::search() update
  static std::atomic_size_t       partno;  // number of matching files, including files in archives, atomic for GrepWorker::search() update
  static std::atomic_size_t       lineno;  // number of lines searched cummulatively
  static std::atomic_size_t       matchno; // number of matches found cummulatively
  static std::vector<std::string> ignore;  // the .gitignore files encountered in the recursive search with --ignore-files

};

#endif
