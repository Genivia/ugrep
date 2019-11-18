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

  // bzlib file handle
  typedef void *bzFile;

#ifdef HAVE_LIBBZ2

  // return true if pathname has a bzlib file extension
  static bool is_bz(const char *pathname)
  {
    return has_ext(pathname, ".bz.bz2.bzip2.tbz.tbz2");
  }

#endif

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

  // return true if pathname has a lzma file extension
  static bool is_xz(const char *pathname)
  {
    return has_ext(pathname, ".lzma.xz.tlz.txz");
  }

#else

  typedef void *xzFile;

#endif

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
      gzfile_(Z_NULL),
      bzfile_(NULL),
      xzfile_(NULL),
      cur_(),
      len_()
  {
#ifdef HAVE_LIBBZ2
    if (is_bz(pathname))
    {
      // open bzlib compressed file
      int err = 0;
      if ((bzfile_ = BZ2_bzReadOpen(&err, file, 0, 0, NULL, 0)) == NULL || err != BZ_OK)
      {
        warning("BZ2_bzReadOpen error", pathname);

        bzfile_ = NULL;
      }
    }
    else
#endif
#ifdef HAVE_LIBLZMA
    if (is_xz(pathname))
    {
      // open lzma compressed file
      xzfile_ = new XZ(file);
      lzma_ret ret = lzma_stream_decoder(&xzfile_->strm, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED);
      if (ret != LZMA_OK)
      {
        warning("lzma_stream_decoder error", pathname);

        delete xzfile_;
        xzfile_ = NULL;
      }
    }
    else
#endif
    {
      // open zlib compressed file
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
    {
      // close zlib compressed file
      if (gzfile_ != Z_NULL)
        gzclose_r(gzfile_);
    }
  }

 protected:

  // read() PEEK constant argument
  static const bool PEEK = true;

  // read a decompressed block into buf_[], returns next character or EOF, PEEK argument to peek at next character instead of reading it
  int_type read(bool peek = false)
  {
#ifdef HAVE_LIBBZ2
    if (bzfile_ != NULL)
    {
      cur_ = 0;

      // decompress a bzlib compressed block into buf_[]
      int err = 0;
      len_ = BZ2_bzRead(&err, bzfile_, buf_, Z_BUF_LEN);

      // an error occurred?
      if (err != BZ_OK && err != BZ_STREAM_END)
      {
        const char *message = "an unspecified bz2 error occurred";

        if (err == BZ_DATA_ERROR || err == BZ_DATA_ERROR_MAGIC)
          message = "an error was detected in the compressed data";
        else if (err == BZ_UNEXPECTED_EOF)
          message = "compressed data ends unexpectedly";
        cannot_decompress(pathname_, message);

        len_ = 0;
      }

      // decompressed the last block?
      if (len_ <= 0 || err == BZ_STREAM_END)
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
      cur_ = 0;
      len_ = 0;

      lzma_ret ret = LZMA_OK;

      // decompress non-empty xzfile_->zbuf[] into buf_[]
      if (xzfile_->strm.avail_in > 0 && xzfile_->strm.avail_out == 0)
      {
        xzfile_->strm.next_out = buf_;
        xzfile_->strm.avail_out = Z_BUF_LEN;

        ret = lzma_code(&xzfile_->strm, xzfile_->finished ? LZMA_FINISH : LZMA_RUN);

        if (ret != LZMA_OK && ret != LZMA_STREAM_END)
          cannot_decompress(pathname_, "an error was detected in the compressed data");
        else
          len_ = Z_BUF_LEN - xzfile_->strm.avail_out;
      }

      // read compressed data into xzfile_->zbuf[] and decompress xzfile_->buf[] into buf_[]
      if (len_ == 0 && (ret == LZMA_OK || ret == LZMA_STREAM_END) && !xzfile_->finished)
      {
        xzfile_->zlen = fread(xzfile_->zbuf, 1, Z_BUF_LEN, xzfile_->file);

        if (ferror(xzfile_->file))
        {
          warning("cannot read", pathname_);
        }
        else
        {
          if (feof(xzfile_->file))
            xzfile_->finished = true;

          xzfile_->strm.next_in = xzfile_->zbuf;
          xzfile_->strm.avail_in = xzfile_->zlen;

          xzfile_->strm.next_out = buf_;
          xzfile_->strm.avail_out = Z_BUF_LEN;

          ret = lzma_code(&xzfile_->strm, xzfile_->finished ? LZMA_FINISH : LZMA_RUN);

          if (ret != LZMA_OK && ret != LZMA_STREAM_END)
            cannot_decompress(pathname_, "an error was detected in the compressed data");
          else
            len_ = Z_BUF_LEN - xzfile_->strm.avail_out;
        }
      }

      // decompressed the last block or there was an error?
      if (len_ <= 0 || ret == LZMA_STREAM_END)
      {
        delete xzfile_;
        xzfile_ = NULL;
      }
    }
    else
#endif
    {
      if (gzfile_ == Z_NULL)
        return traits_type::eof();

      cur_ = 0;

      // decompress a zlib compressed block into buf_[]
      len_ = gzread(gzfile_, buf_, Z_BUF_LEN);

      // an error occurred?
      if (len_ < 0)
      {
        int err;
        const char *message = gzerror(gzfile_, &err);

        if (err == Z_ERRNO)
          warning("cannot read", pathname_);
        else
          cannot_decompress(pathname_, message);

        len_ = 0;
      }

      // decompressed the last block?
      if (len_ < Z_BUF_LEN)
      {
        gzclose_r(gzfile_);
        gzfile_ = Z_NULL;
      }
    }

    return len_ > 0 ? traits_type::to_int_type(buf_[peek ? cur_ : cur_++]) : traits_type::eof();
  }

  // std::streambuf::underflow()
  int_type underflow() override
  {
    return cur_ < len_ ? buf_[cur_] : read(PEEK);
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
        if (read(PEEK) == traits_type::eof())
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
  gzFile          gzfile_;
  bzFile          bzfile_;
  xzFile          xzfile_;
  unsigned char   buf_[Z_BUF_LEN];
  std::streamsize cur_;
  std::streamsize len_;
};

#endif
