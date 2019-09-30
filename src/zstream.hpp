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
@file      zstream.h
@brief     file decompression
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_H

#include <cstdio>
#include <streambuf>
#include <cstring>
#include <zlib.h>

#ifndef Z_BUF_LEN
#define Z_BUF_LEN (32768)
#endif

class zstreambuf : public std::streambuf {
 public:
  zstreambuf(FILE *file)
  {
    gzfile_ = gzdopen(fileno(file), "rb");
    if (gzfile_ != Z_NULL)
      gzbuffer(gzfile_, Z_BUF_LEN);
    cur_ = 0;
    len_ = 0;
    get();
  }
  virtual ~zstreambuf()
  {
    if (gzfile_ != Z_NULL)
      gzclose(gzfile_);
  }
 private:
  virtual int_type underflow()
  {
    if (ch_ == EOF)
      return traits_type::eof();
    return traits_type::to_int_type(ch_);
  }
  virtual int_type uflow()
  {
    if (ch_ == EOF)
      return traits_type::eof();
    int c = ch_;
    get();
    return traits_type::to_int_type(c);
  }
  virtual std::streamsize showmanyc()
  {
    return (std::streamsize)(len_ - cur_);
  }
  inline void get()
  {
    if (cur_ < len_)
    {
      ch_ = buf_[cur_++];
    }
    else if (gzfile_ != Z_NULL)
    {
      cur_ = 0;
      len_ = gzread(gzfile_, buf_, Z_BUF_LEN);
      if (len_ <= 0)
      {
        gzclose(gzfile_);
        gzfile_ = Z_NULL;
        ch_ = EOF;
      }
      else
      {
        ch_ = buf_[cur_++];
      }
    }
    else
    {
      ch_ = EOF;
    }
  }
 protected:
  gzFile gzfile_;
  unsigned char buf_[Z_BUF_LEN];
  int cur_;
  int len_;
  int ch_;
};

#endif
