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
@file      path.cpp
@brief     Handling of file paths
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2025, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "path.hpp"

// check if we are natively compiling for a Windows OS (not Cygwin and not MinGW)
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

#ifdef OS_WIN
#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"
#else
#define PATHSEPCHR '/'
#define PATHSEPSTR "/"
#endif

// get a path from a directory, or as is if already absolute
std::string Path::from_dir(const std::string& dir, const std::string& path)
{
  // C++17's std::filesystem::path may be used to implement our Path if present

#ifdef OS_WIN
  char drive;

  drive = path.front();
  if (path.length() >= 2 &&
      path[1] == ':' &&
      ((drive >= 'A' && drive <= 'Z') ||
       (drive >= 'a' && drive <= 'z')))
#else
  if (path.front() == PATHSEPCHR)
#endif
    return path;

  std::string fullpath;

  if (path.front() == '~')
  {
    const char *home;
#ifdef OS_WIN
    home = getenv("USERPROFILE");
#else
    home = getenv("HOME");
#endif
    if (home != NULL)
      fullpath.assign(home);
    else
      fullpath.assign(dir);
    fullpath.append(path.c_str() + 1);

    return fullpath;
  }

  fullpath.assign(dir).append(PATHSEPSTR).append(path);

  return fullpath;
}
