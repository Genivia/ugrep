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
@brief     file decompression streams
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_H
#define ZSTREAM_H

#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <streambuf>
#include <zlib.h>

#include "zopen.h"

#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif

#ifdef HAVE_LIBLZMA
#include <lzma.h>
#endif

#ifndef Z_BUF_LEN
#define Z_BUF_LEN (65536)
#endif

extern void cannot_decompress(const char *pathname, const char *message);
extern void warning(const char *message, const char *arg);

class zstreambuf : public std::streambuf {

 public:

  // compress (Z) file handle
  typedef void *zzFile;

  // bzlib file handle
  typedef void *bzFile;

#ifdef HAVE_LIBLZMA

  // lzma file handle
  typedef struct XZ {
    XZ(FILE *file)
      :
        file(file),
        strm(LZMA_STREAM_INIT),
        zlen(),
        finished()
    { }
    ~XZ()
    {
      lzma_end(&strm);
    }
    FILE         *file;
    lzma_stream   strm;
    unsigned char zbuf[Z_BUF_LEN];
    size_t        zlen;
    bool          finished;
  } *xzFile;

#else

  // unused lzma file handle
  typedef void *xzFile;

#endif

  // return true if pathname has a compress (Z) file extension
  static bool is_Z(const char *pathname)
  {
    return has_ext(pathname, ".Z");
  }

  // return true if pathname has a bzlib file extension
  static bool is_bz(const char *pathname)
  {
    return has_ext(pathname, ".bz.bz2.bzip2.tb2.tbz.tbz2.tz2");
  }

  // return true if pathname has a lzma file extension
  static bool is_xz(const char *pathname)
  {
    return has_ext(pathname, ".lzma.xz.tlz.txz");
  }

  // check if pathname file extension matches on of the given file extensions
  static bool has_ext(const char *pathname, const char *extensions)
  {
    const char *dot = strrchr(pathname, '.');
    if (dot == NULL)
      return false;
    const char *match = strstr(extensions, dot);
    if (match == NULL)
      return false;
    size_t end = strlen(dot);
    if (match + end > extensions + strlen(extensions))
      return false;
    return match[end] == '.' || match[end] == '\0';
  }

  // constructor
  zstreambuf(const char *pathname, FILE *file)
    :
      pathname_(pathname),
      zzfile_(NULL),
      gzfile_(Z_NULL),
      bzfile_(NULL),
      xzfile_(NULL),
      cur_(),
      len_()
  {
    if (is_bz(pathname))
    {
#ifdef HAVE_LIBBZ2
      // open bzip2 compressed file
      int err = 0;
      if ((bzfile_ = BZ2_bzReadOpen(&err, file, 0, 0, NULL, 0)) == NULL || err != BZ_OK)
      {
        warning("BZ2_bzReadOpen error", pathname);

        bzfile_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
#endif
    }
    else if (is_xz(pathname))
    {
#ifdef HAVE_LIBLZMA
      // open xz/lzma compressed file
      xzfile_ = new XZ(file);
      lzma_ret ret = lzma_auto_decoder(&xzfile_->strm, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED);
      if (ret != LZMA_OK)
      {
        warning("lzma_stream_decoder error", pathname);

        delete xzfile_;
        xzfile_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
#endif
    }
    else if (is_Z(pathname))
    {
      // open compress (Z) compressed file
      if ((zzfile_ = z_open(file, "r", 0)) == NULL)
        warning("zopen error", pathname);
    }
    else
    {
      // open gzip compressed file
      int fd = dup(fileno(file));
      if (fd >= 0 && (gzfile_ = gzdopen(fd, "r")) != Z_NULL)
      {
        gzbuffer(gzfile_, Z_BUF_LEN);
      }
      else
      {
        warning("gzdopen error", pathname);

        if (fd >= 0)
          close(fd);
      }
    }
  }

  // destructor
  virtual ~zstreambuf()
  {
#ifdef HAVE_LIBBZ2
    if (bzfile_ != NULL)
    {
      // close bzlib compressed file
      int err = 0;
      BZ2_bzReadClose(&err, bzfile_);
    }
    else
#endif
#ifdef HAVE_LIBLZMA
    if (xzfile_ != NULL)
    {
      // close lzma compressed file
      delete xzfile_;
    }
    else
#endif
    if (zzfile_ != NULL)
    {
      // close compress (Z) compressed file
      z_close(zzfile_);
    }
    else if (gzfile_ != Z_NULL)
    {
      // close zlib compressed file
      gzclose_r(gzfile_);
    }
  }

  // decompress a block of data into buf[0..len-1], return number of bytes inflated or zero on error or EOF
  std::streamsize decompress(unsigned char *buf, size_t len)
  {
    std::streamsize size = 0;

#ifdef HAVE_LIBBZ2
    if (bzfile_ != NULL)
    {
      // decompress a bzlib compressed block into buf[]
      int err = 0;
      size = BZ2_bzRead(&err, bzfile_, buf, len);

      // an error occurred?
      if (err != BZ_OK && err != BZ_STREAM_END)
      {
        const char *message;

        if (err == BZ_DATA_ERROR || err == BZ_DATA_ERROR_MAGIC)
          message = "an error was detected in the bzip2 compressed data";
        else if (err == BZ_UNEXPECTED_EOF)
          message = "bzip2 compressed data ends unexpectedly";
        else
          message = "an unspecified bzip2 decompression error occurred";
        cannot_decompress(pathname_, message);

        size = -1;
      }

      // decompressed the last block?
      if (size <= 0 || err == BZ_STREAM_END)
      {
        BZ2_bzReadClose(&err, bzfile_);

        bzfile_ = NULL;
      }
    }
    else
#endif
#ifdef HAVE_LIBLZMA
    if (xzfile_ != NULL)
    {
      lzma_ret ret = LZMA_OK;

      // decompress non-empty xzfile_->zbuf[] into buf[]
      if (xzfile_->strm.avail_in > 0 && xzfile_->strm.avail_out == 0)
      {
        xzfile_->strm.next_out = buf;
        xzfile_->strm.avail_out = len;

        ret = lzma_code(&xzfile_->strm, xzfile_->finished ? LZMA_FINISH : LZMA_RUN);

        if (ret != LZMA_OK && ret != LZMA_STREAM_END)
        {
          cannot_decompress(pathname_, "an error was detected in the lzma compressed data");
          size = -1;
        }
        else
        {
          size = len - xzfile_->strm.avail_out;
        }
      }

      // read compressed data into xzfile_->zbuf[] and decompress xzfile_->zbuf[] into buf[]
      if (ret == LZMA_OK && size < static_cast<std::streamsize>(len) && !xzfile_->finished)
      {
        xzfile_->zlen = fread(xzfile_->zbuf, 1, Z_BUF_LEN, xzfile_->file);

        if (ferror(xzfile_->file))
        {
          warning("cannot read", pathname_);
          xzfile_->finished = true;
        }
        else
        {
          if (feof(xzfile_->file))
            xzfile_->finished = true;

          xzfile_->strm.next_in = xzfile_->zbuf;
          xzfile_->strm.avail_in = xzfile_->zlen;

          if (size == 0)
          {
            xzfile_->strm.next_out = buf;
            xzfile_->strm.avail_out = len;
          }

          ret = lzma_code(&xzfile_->strm, xzfile_->finished ? LZMA_FINISH : LZMA_RUN);

          if (ret != LZMA_OK && ret != LZMA_STREAM_END)
          {
            cannot_decompress(pathname_, "an error was detected in the lzma compressed data");
            size = -1;
          }
          else
          {
            size = len - xzfile_->strm.avail_out;
          }
        }
      }

      // decompressed the last block or there was an error?
      if (size <= 0 || ret == LZMA_STREAM_END)
      {
        delete xzfile_;
        xzfile_ = NULL;
      }
    }
    else
#endif
    if (zzfile_ != NULL)
    {
      size = z_read(zzfile_, buf, static_cast<int>(len)); 

      if (size < 0)
      {
        cannot_decompress(pathname_, "an error was detected in the compressed data");

        z_close(zzfile_);
        zzfile_ = NULL;
      }
    }
    else if (gzfile_ != Z_NULL)
    {
      // decompress a zlib compressed block into buf[]
      size = gzread(gzfile_, buf, len);

      // an error occurred?
      if (size < 0)
      {
        int err;
        const char *message = gzerror(gzfile_, &err);

        if (err == Z_ERRNO)
          warning("cannot read", pathname_);
        else
          cannot_decompress(pathname_, message);
      }

      // decompressed the last block?
      if (size < static_cast<std::streamsize>(len))
      {
        gzclose_r(gzfile_);
        gzfile_ = Z_NULL;
      }
    }

    return size;
  }

  // get pointer to the internal buffer and its max size
  void get_buffer(unsigned char *& buffer, size_t& maxlen)
  {
    buffer = buf_;
    maxlen = Z_BUF_LEN;
  }

 protected:

  // read a decompressed block into buf_[], returns pending next character or EOF
  int_type peek()
  {
    cur_ = 0;
    len_ = decompress(buf_, Z_BUF_LEN);
    return len_ > 0 ? traits_type::to_int_type(*buf_) : traits_type::eof();
  }

  // read a decompressed block into buf_[], reads and returns next character or EOF
  int_type read()
  {
    int_type c = peek();
    if (c != traits_type::eof())
      ++cur_;
    return c;
  }

  // std::streambuf::underflow()
  int_type underflow() override
  {
    return cur_ < len_ ? buf_[cur_] : peek();
  }

  // std::streambuf::uflow()
  int_type uflow() override
  {
    return cur_ < len_ ? buf_[cur_++] : read();
  }

  // std::streambuf::showmanyc()
  std::streamsize showmanyc() override
  {
    return gzfile_ == Z_NULL && bzfile_ == NULL && cur_ >= len_ ? -1 : 0;
  }

  // std::streambuf::xsgetn(s, n)
  std::streamsize xsgetn(char *s, std::streamsize n) override
  {
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

  const char     *pathname_;
  zzFile          zzfile_;
  gzFile          gzfile_;
  bzFile          bzfile_;
  xzFile          xzfile_;
  unsigned char   buf_[Z_BUF_LEN];
  std::streamsize cur_;
  std::streamsize len_;
};

#endif
