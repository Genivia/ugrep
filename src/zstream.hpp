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
@file      zstream.hpp
@brief     file decompression
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_H
#define ZSTREAM_H

#include <cstdio>
#include <streambuf>
#include <cstring>
#include <zlib.h>

#ifndef Z_BUF_LEN
#define Z_BUF_LEN (65536)
#endif

class zstreambuf : public std::streambuf {
 public:
  zstreambuf(FILE *file)
    :
      cur_(),
      len_()
  {
    int fd = dup(fileno(file));
    gzfile_ = gzdopen(fd, "r");
    if (gzfile_ != Z_NULL)
      gzbuffer(gzfile_, Z_BUF_LEN);
    else
      perror("ugrep: zlib open error ");
  }
  virtual ~zstreambuf()
  {
    if (gzfile_ != Z_NULL)
      gzclose_r(gzfile_);
  }
 protected:
  int_type underflow()
  {
    return cur_ < len_ ? buf_[cur_] : peek();
  }
  int_type uflow()
  {
    return cur_ < len_ ? buf_[cur_++] : read();
  }
  std::streamsize showmanyc()
  {
    return gzfile_ == Z_NULL ? -1 : 0;
  }
  std::streamsize xsgetn(char *s, std::streamsize n)
  {
    if (gzfile_ == Z_NULL)
      return 0;
    std::streamsize k = n;
    while (k > 0)
    {
      if (cur_ >= len_)
        if (peek() == traits_type::eof())
          return n - k;
      if (k <= len_ - cur_)
      {
        memcpy(s, buf_ + cur_, k);
        cur_ += k;
        return n;
      }
      memcpy(s, buf_ + cur_, len_ - cur_);
      s += len_ - cur_;
      k -= len_ - cur_;
      cur_ = len_;
    }
    return n;
  }
  int_type peek()
  {
    if (gzfile_ == Z_NULL)
      return traits_type::eof();
    cur_ = 0;
    len_ = gzread(gzfile_, buf_, Z_BUF_LEN);
    if (len_ <= 0)
    {
      close();
      return traits_type::eof();
    }
    return traits_type::to_int_type(buf_[cur_]);
  }
  int_type read()
  {
    if (gzfile_ == Z_NULL)
      return traits_type::eof();
    cur_ = 0;
    len_ = gzread(gzfile_, buf_, Z_BUF_LEN);
    if (len_ <= 0)
    {
      close();
      return traits_type::eof();
    }
    return traits_type::to_int_type(buf_[cur_++]);
  }
  void close()
  {
    if (!gzeof(gzfile_))
    {
      int err;
      gzerror(gzfile_, &err);
      if (err == Z_ERRNO)
        perror("ugrep: zlib read error");
      else
        fprintf(stderr, "ugrep: zlib decompression error\n");
    }
    gzclose_r(gzfile_);
    gzfile_ = Z_NULL;
    len_ = 0;
  }
  gzFile gzfile_;
  unsigned char buf_[Z_BUF_LEN];
  std::streamsize cur_;
  std::streamsize len_;
};

#endif
