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
@file      stats.cpp
@brief     collect global statistics - static, partially thread-safe
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "stats.hpp"
#include <cstdlib>
#include <cstdio>

// report the statistics
void Stats::report(FILE *output)
{
  size_t sf = searched_files();
  size_t sd = searched_dirs();
  size_t ff = found_files();
  size_t fp = found_parts();
  size_t ws = warnings;

  fprintf(output, "Searched %zu file%s", sf, (sf == 1 ? "" : "s"));
  if (threads > 1)
    fprintf(output, " with %zu threads", threads);
  if (sd > 0)
    fprintf(output, " in %zu director%s", sd, (sd == 1 ? "y" : "ies"));
  fprintf(output, ": %zu matching", ff);
  if (fp > ff)
    fprintf(output, " + %zu in archives" NEWLINESTR, fp - ff);
  else
    fprintf(output, NEWLINESTR);
  if (warnings > 0)
    fprintf(output, "Received %zu warning%s" NEWLINESTR, ws, ws == 1 ? "" : "s");

  fprintf(output, "The following pathname selections and restrictions were applied:" NEWLINESTR);
  if (flag_config != NULL)
    fprintf(output, "  --config=%s" NEWLINESTR, flag_config_file.c_str());
#ifdef WITH_HIDDEN
  if (flag_hidden)
    fprintf(output, "  --hidden (default)" NEWLINESTR);
  else
    fprintf(output, "  --no-hidden" NEWLINESTR);
#else
  if (flag_hidden)
    fprintf(output, "  --hidden" NEWLINESTR);
  else
    fprintf(output, "  --no-hidden (default)" NEWLINESTR);
#endif
  if (flag_min_depth > 0 && flag_max_depth > 0)
    fprintf(output, "  --depth=%zu,%zu" NEWLINESTR, flag_min_depth, flag_max_depth);
  else if (flag_min_depth > 0)
    fprintf(output, "  --depth=%zu," NEWLINESTR, flag_min_depth);
  else if (flag_max_depth > 0)
    fprintf(output, "  --depth=%zu" NEWLINESTR, flag_max_depth);
  for (auto& i : flag_ignore_files)
    fprintf(output, "  --ignore-files='%s'" NEWLINESTR, i.c_str());
  for (auto& i : ignore)
    fprintf(output, "    %s exclusions were applied to %s" NEWLINESTR, i.c_str(), i.substr(0, i.find_last_of(PATHSEPCHR)).c_str());
  for (auto& i : flag_file_magic)
  {
    if (!i.empty() && (i.front() == '!' || i.front() == '^'))
      fprintf(output, "  --file-magic='!%s' (negation)" NEWLINESTR, i.c_str() + 1);
    else
      fprintf(output, "  --file-magic='%s'" NEWLINESTR, i.c_str());
  }
  for (auto& i : flag_include_fs)
    fprintf(output, "  --include-fs='%s'" NEWLINESTR, i.c_str());
  for (auto& i : flag_exclude_fs)
    fprintf(output, "  --exclude-fs='%s'" NEWLINESTR, i.c_str());
  for (auto& i : flag_all_include)
    fprintf(output, "  --include='%s'%s" NEWLINESTR, i.c_str(), i.front() == '!' ? " (negated)" : "");
  for (auto& i : flag_all_exclude)
    fprintf(output, "  --exclude='%s'%s" NEWLINESTR, i.c_str(), i.front() == '!' ? " (negated)" : "");
  for (auto& i : flag_all_include_dir)
    fprintf(output, "  --include-dir='%s'%s" NEWLINESTR, i.c_str(), i.front() == '!' ? " (negated)" : "");
  for (auto& i : flag_all_exclude_dir)
    fprintf(output, "  --exclude-dir='%s'%s" NEWLINESTR, i.c_str(), i.front() == '!' ? " (negated)" : "");
}

size_t                   Stats::files  = 0;
size_t                   Stats::dirs   = 0;
std::atomic_size_t       Stats::fileno;
std::atomic_size_t       Stats::partno;
std::vector<std::string> Stats::ignore;
