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
@file      mmap.hpp
@brief     class to manage memory-mapped files
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef MMAP_HPP
#define MMAP_HPP

#include "ugrep.hpp"

// --min-mmap and --max-mmap file size to allocate with mmap(), not greater than 4294967295LL, 0 disables mmap()
#ifndef MIN_MMAP_SIZE
# define MIN_MMAP_SIZE 16384LL // 16KB at minimum, smaller files are efficiently read in one go with read()
#endif
#ifndef MAX_MMAP_SIZE
# define MAX_MMAP_SIZE 1073741824LL // each worker thread may use up to 1GB mmap space but not more
#endif

#if defined(HAVE_MMAP) && MAX_MMAP_SIZE > 0
# include <sys/mman.h>
# include <sys/stat.h>
# include <limits>
#endif

// manage mmap state
class MMap {

 public:

  MMap()
    :
      mmap_base(NULL),
      mmap_size(0)
  { }

  ~MMap()
  {
#if defined(HAVE_MMAP) && MAX_MMAP_SIZE > 0
    if (mmap_base != NULL)
      munmap(mmap_base, mmap_size);
#endif
  }

  // attempt to mmap the given file-based input, return true if successful with base and size
  bool file(reflex::Input& input, const char*& base, size_t& size)
  {
    base = NULL;
    size = 0;

#if defined(HAVE_MMAP) && MAX_MMAP_SIZE > 0

    // get current input file and check if its encoding is plain
    FILE *file = input.file();
    if (file == NULL || input.file_encoding() != reflex::Input::file_encoding::plain)
      return false;

    // is this a regular file that is not too large (for size_t)?
    int fd = fileno(file);
    struct stat buf;
    if (fstat(fd, &buf) != 0 || !S_ISREG(buf.st_mode) || static_cast<uint64_t>(buf.st_size) < MIN_MMAP_SIZE || static_cast<uint64_t>(buf.st_size) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
      return false;

    // is this file not larger than --max-mmap?
    size = static_cast<size_t>(buf.st_size);
    if (size > flag_max_mmap)
      return false;

    // mmap the file and round requested size up to 4K (typical page size)
    if (mmap_base == NULL)
    {
      // allocate fixed mmap region to reuse
      mmap_size = (flag_max_mmap + 0xfff) & ~0xfffUL;
      mmap_base = mmap(NULL, mmap_size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

      // files are sequentially read
      if (mmap_base == MAP_FAILED)
        mmap_base = NULL;
      else
        madvise(mmap_base, mmap_size, MADV_SEQUENTIAL);
    }

    if (mmap_base != NULL)
    {
      // mmap the (next) file to the fixed mmap region
      base = static_cast<const char*>(mmap_base = mmap(mmap_base, mmap_size, PROT_READ, MAP_FIXED | MAP_PRIVATE, fd, 0));

      // mmap OK?
      if (mmap_base != MAP_FAILED)
        return true;
    }

    // not OK
    mmap_base = NULL;
    mmap_size = 0;
    base = NULL;
    size = 0;

#else

    (void)input;

#endif

    return false;
  }

 protected:

  void  *mmap_base; // mmap() base address
  size_t mmap_size; // mmap() allocated size

};

#endif
