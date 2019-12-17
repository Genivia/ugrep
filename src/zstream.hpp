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
@brief     file decompression streams written in C++11
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2019, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_H
#define ZSTREAM_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <streambuf>
#include <zlib.h>

#include "zopen.h"

// check if we are compiling for a windows OS
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)

inline int fileno(FILE *f) { return _fileno(f); }
inline int dup(int fd)     { return _dup(fd); }
inline int close(int fd)   { return _close(fd); }

// work around missing functions in old zlib 1.2.3
#define gzbuffer(z, n)
#define gzclose_r(z) gzclose(z)
#define gzclose_w(z) gzclose(z)

#else

#endif

#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#else
struct bz_stream;
#endif

#ifdef HAVE_LIBLZMA
#include <lzma.h>
#else
struct lzma_stream;
#endif

// TODO this feature is disabled as it is too slow, we should optimize crc32() with a table
// use zip crc integrity check at the cost of a significant slow down? Note that zlib, bzip2, and lzma already use crc checks
// #define WITH_CRC32

// buffer size to hold compressed data, e.g. from compressed files
#ifndef Z_BUF_LEN
#define Z_BUF_LEN (65536)
#endif

extern void cannot_decompress(const char *pathname, const char *message);
extern void warning(const char *message, const char *arg);

class zstreambuf : public std::streambuf {

 public:

  // zip decompression state and info
  class ZipInfo {

    friend class zstreambuf;

   public:

    // zip compression methods, STORE and DEFLATE are common
    enum class Compression : uint16_t { STORE = 0, DEFLATE = 8, BZIP2 = 12, LZMA = 14, XZ = 95 };

    // constructor
    ZipInfo(const char *pathname, FILE *file)
      :
        pathname_(pathname),
        file_(file),
        z_strm_(NULL),
        bz_strm_(NULL),
        lzma_strm_(NULL),
        zcur_(0),
        zlen_(0),
        zcrc_(0xffffffff),
        znew_(true),
        zend_(false)
    { }

    // no copy constructor
    ZipInfo(const ZipInfo&) = delete;

    // destructor
    ~ZipInfo()
    {
      if (z_strm_ != NULL)
        delete z_strm_;
#ifdef HAVE_LIBBZ2
      if (bz_strm_ != NULL)
        delete bz_strm_;
#endif
#ifdef HAVE_LIBLZMA
      if (lzma_strm_ != NULL)
        delete lzma_strm_;
#endif
    }

    uint16_t    version; // zip version (unused)
    uint16_t    flag;    // zip general purpose bit flag
    Compression method;  // zip compression method
    uint16_t    time;    // zip last mod file time (unused)
    uint16_t    date;    // zip last mod file date (unused)
    uint32_t    crc;     // zip crc-32 (unused)
    uint64_t    size;    // zip compressed file size
    uint64_t    usize;   // zip uncompressed file size (unused)
    std::string name;    // zip file name from local file header or extra field

   protected:

    static constexpr size_t ZIPBLOCK = 65536; // block size to read zip data, at least 64K to fit name

    static constexpr uint32_t HEADER_MAGIC     = 0x04034b50; // zip local file header magic
    static constexpr uint32_t DESCRIPTOR_MAGIC = 0x08074b50; // zip descriptor magic

    // convert 2 bytes to 16 bit
    static uint16_t u16(const unsigned char *buf)
    {
      return buf[0] + (buf[1] << 8);
    }

    // convert 4 bytes to 32 bit
    static uint32_t u32(const unsigned char *buf)
    {
      return u16(buf) + (static_cast<uint32_t>(u16(buf + 2)) << 16);
    }

    // convert 8 bytes to to 64 bit
    static uint64_t u64(const unsigned char *buf)
    {
      return u32(buf) + (static_cast<uint64_t>(u32(buf + 4)) << 32);
    }

    // read zip local file header if we are at a header, read the header, file name, and extra field
    bool header()
    {
      // are we at a new header? If not, do nothing and return true
      if (!znew_)
      {
        // try to read the descriptor, if present, before the next header
        if (!descriptor())
          return false;

        if (!znew_)
          return true;
      }

      // read the header data and check header magic
      const unsigned char *data = read_num(30);
      if (data == NULL || u32(data) != HEADER_MAGIC)
        return false;

      // we're reading the zip local file header
      znew_ = false;

      // get the zip local file header info
      version = u16(data + 4);
      flag    = u16(data + 6);
      method  = static_cast<Compression>(u16(data + 8));
      time    = u16(data + 10);
      date    = u16(data + 12);
      crc     = u32(data + 14);
      size    = u32(data + 18);
      usize   = u32(data + 22);

      uint16_t namelen  = u16(data + 26);
      uint16_t extralen = u16(data + 28);

      // if zip data is encrypted, we give up
      if ((flag & 1) != 0)
      {
        cannot_decompress(pathname_, "zip data is encrypted");
        return false;
      }

      // read the file name
      data = read_num(namelen);
      if (data == NULL)
      {
        cannot_decompress(pathname_, "an error was detected in the zip compressed data");
        return false;
      }

      name.assign(reinterpret_cast<const char*>(data), namelen);

      // read the extra field
      data = read_num(extralen);
      if (data == NULL)
      {
        cannot_decompress(pathname_, "an error was detected in the zip compressed data");
        return false;
      }

      for (uint16_t num = 0; num < extralen; num += u16(data + num + 2))
      {
        uint16_t id = u16(data + num);

        if (id == 0x0001)
        {
          // Zip64 Extended Information Extra Field
          usize = u64(data + num + 4);
          size = u64(data + num + 12);
        }
        else if (id == 0x7075)
        {
          // Info-ZIP Unicode Path Extra Field
          size_t len = u16(data + num + 2);
          if (len >= 9 && num + len - 9 < extralen)
            name.assign(reinterpret_cast<const char*>(data + num + 9), len - 9);
        }
      }

      if (method == Compression::DEFLATE)
      {
        // Zip deflate method
        if (z_strm_ == NULL)
        {
          try
          {
            z_strm_ = new z_stream;
          }
          catch (const std::bad_alloc&)
          {
            cannot_decompress(pathname_, "out of memory");
            return false;
          }

          z_strm_->zalloc = NULL;
          z_strm_->zfree  = NULL;
          z_strm_->opaque = NULL;
        }

        // prepare to inflate the remainder of the buffered data
        z_strm_->next_in   = zbuf_ + zcur_;
        z_strm_->avail_in  = zlen_ - zcur_;
        z_strm_->next_out  = NULL;
        z_strm_->avail_out = 0;

        // initialize zlib inflate
        if (inflateInit2(z_strm_, -MAX_WBITS) != Z_OK)
        {
          cannot_decompress(pathname_, z_strm_->msg ? z_strm_->msg : "inflateInit2 failed");
          return false;
        }
      }
      else if (method == Compression::BZIP2)
      {

#ifdef HAVE_LIBBZ2

        // Zip bzip2 method
        if (bz_strm_ == NULL)
        {
          try
          {
            bz_strm_ = new bz_stream;
          }
          catch (const std::bad_alloc&)
          {
            cannot_decompress(pathname_, "out of memory");
            return false;
          }

          bz_strm_->bzalloc = NULL;
          bz_strm_->bzfree  = NULL;
          bz_strm_->opaque  = NULL;
        }

        // prepare to decompress the remainder of the buffered data
        bz_strm_->next_in   = reinterpret_cast<char*>(zbuf_ + zcur_);
        bz_strm_->avail_in  = zlen_ - zcur_;
        bz_strm_->next_out  = NULL;
        bz_strm_->avail_out = 0;

        // initialize bzip2 decompress
        if (BZ2_bzDecompressInit(bz_strm_, 0, 0) != BZ_OK)
        {
          cannot_decompress(pathname_, "BZ2_bzDecompressInit failed");
          return false;
        }

#else

        cannot_decompress(pathname_, "unsupported zip compression method bzip2");
        return false;

#endif

      }
      else if (method == Compression::LZMA || method == Compression::XZ)
      {

#ifdef HAVE_LIBLZMA

        // TODO should we support bit 1 clear, i.e. use the size field to terminate the lzma stream?
        // if bit 1 is clear, then there is no EOS in the lzma stream that we need to terminate lzma decompression
        if (method == Compression::LZMA && (flag & 2) == 0)
        {
          cannot_decompress(pathname_, "unsupported zip compression method lzma without EOS");
          return false;
        }

        // Zip lzma method with bit 1 set (EOS in lzma)
        if (lzma_strm_ == NULL)
        {
          try
          {
            lzma_strm_ = new lzma_stream;
          }
          catch (const std::bad_alloc&)
          {
            cannot_decompress(pathname_, "out of memory");
            return false;
          }
        }

        *lzma_strm_ = LZMA_STREAM_INIT;

        // prepare to decompress the remainder of the buffered data
        lzma_strm_->next_in  = zbuf_ + zcur_;
        lzma_strm_->avail_in = zlen_ - zcur_;

        // initialize lzma decompress
        lzma_ret ret = lzma_auto_decoder(lzma_strm_, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED);
        if (ret != LZMA_OK)
        {
          cannot_decompress(pathname_, "lzma_auto_decoder failed");
          return false;
        }

#else

        cannot_decompress(pathname_, "unsupported zip compression method lzma");
        return false;

#endif

      }
      else if (method != Compression::STORE || (flag & 8) != 0)
      {
        std::string message("unsupported zip compression method ");
        message.append(std::to_string(static_cast<uint16_t>(method)));
        cannot_decompress(pathname_, message.c_str());
        return false;
      }

      // init crc32 to the complement of 0
      zcrc_ = 0xffffffff;

      zend_ = false;
      return true;
    }

    // read and decompress zip file data
    std::streamsize decompress(unsigned char *buf, size_t len)
    {
      // if no more data to decompress, then return 0 to indicate EOF
      if (zend_)
        return 0;

      std::streamsize num = 0;

      if (method == Compression::DEFLATE && z_strm_ != NULL)
      {
        int ret = Z_OK;

        // decompress non-empty zbuf_[] into the given buf[]
        if (z_strm_->avail_in > 0 && z_strm_->avail_out == 0)
        {
          z_strm_->next_out  = buf;
          z_strm_->avail_out = len;

          ret = inflate(z_strm_, Z_NO_FLUSH);

          zcur_ = zlen_ - z_strm_->avail_in;

          if (ret != Z_OK && ret != Z_STREAM_END)
          {
            cannot_decompress(pathname_, "a zlib decompression error was detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
          else
          {
            num = len - z_strm_->avail_out;
            zend_ = ret == Z_STREAM_END;
          }
        }

        // read compressed data into zbuf_[] and decompress zbuf_[] into the given buf[]
        if (ret == Z_OK && z_strm_->avail_in == 0 && num < static_cast<int>(len))
        {
          if (read())
          {
            z_strm_->next_in  = zbuf_;
            z_strm_->avail_in = zlen_;

            if (num == 0)
            {
              z_strm_->next_out  = buf;
              z_strm_->avail_out = len;
            }

            ret = inflate(z_strm_, Z_NO_FLUSH);

            zcur_ = zlen_ - z_strm_->avail_in;

            if (ret != Z_OK && ret != Z_STREAM_END)
            {
              cannot_decompress(pathname_, "a zlib decompression error was detected in the zip compressed data");
              num = -1;
              zend_ = true;
            }
            else
            {
              num = len - z_strm_->avail_out;
              zend_ = ret == Z_STREAM_END;
            }
          }
          else
          {
            cannot_decompress(pathname_, "EOF detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
        }

        if (zend_)
        {
          ret = inflateEnd(z_strm_);
          if (ret != Z_OK)
          {
            cannot_decompress(pathname_, "a zlib decompression error was detected in the zip compressed data");
            num = -1;
          }
        }
      }
#ifdef HAVE_LIBBZ2
      else if (method == Compression::BZIP2 && bz_strm_ != NULL)
      {
        int ret = BZ_OK;

        // decompress non-empty zbuf_[] into the given buf[]
        if (bz_strm_->avail_in > 0 && bz_strm_->avail_out == 0)
        {
          bz_strm_->next_out  = reinterpret_cast<char*>(buf);
          bz_strm_->avail_out = len;

          ret = BZ2_bzDecompress(bz_strm_);

          zcur_ = zlen_ - bz_strm_->avail_in;

          if (ret != BZ_OK && ret != BZ_STREAM_END)
          {
            cannot_decompress(pathname_, "a bzip2 decompression error was detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
          else
          {
            num = len - bz_strm_->avail_out;
            zend_ = ret == BZ_STREAM_END;
          }
        }

        // read compressed data into zbuf_[] and decompress zbuf_[] into the given buf[]
        if (ret == BZ_OK && bz_strm_->avail_in == 0 && num < static_cast<int>(len))
        {
          if (read())
          {
            bz_strm_->next_in  = reinterpret_cast<char*>(zbuf_);
            bz_strm_->avail_in = zlen_;

            if (num == 0)
            {
              bz_strm_->next_out  = reinterpret_cast<char*>(buf);
              bz_strm_->avail_out = len;
            }

            ret = BZ2_bzDecompress(bz_strm_);

            zcur_ = zlen_ - bz_strm_->avail_in;

            if (ret != BZ_OK && ret != BZ_STREAM_END)
            {
              cannot_decompress(pathname_, "a bzip2 decompression error was detected in the zip compressed data");
              num = -1;
              zend_ = true;
            }
            else
            {
              num = len - bz_strm_->avail_out;
              zend_ = ret == BZ_STREAM_END;
            }
          }
          else
          {
            cannot_decompress(pathname_, "EOF detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
        }

        if (zend_)
        {
          ret = BZ2_bzDecompressEnd(bz_strm_);
          if (ret != BZ_OK)
          {
            cannot_decompress(pathname_, "a bzip2 decompression error was detected in the zip compressed data");
            num = -1;
          }
        }
      }
#endif
#ifdef HAVE_LIBLZMA
      else if ((method == Compression::LZMA || method == Compression::XZ) && lzma_strm_ != NULL)
      {
        lzma_ret ret = LZMA_OK;

        // decompress non-empty zbuf_[] into the given buf[]
        if (lzma_strm_->avail_in > 0 && lzma_strm_->avail_out == 0)
        {
          lzma_strm_->next_out  = buf;
          lzma_strm_->avail_out = len;

          ret = lzma_code(lzma_strm_, LZMA_RUN);

          zcur_ = zlen_ - lzma_strm_->avail_in;

          if (ret != LZMA_OK && ret != LZMA_STREAM_END)
          {
            cannot_decompress(pathname_, "a lzma decompression error was detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
          else
          {
            num = len - lzma_strm_->avail_out;
            zend_ = ret == LZMA_STREAM_END;
          }
        }

        // read compressed data into zbuf_[] and decompress zbuf_[] into the given buf[]
        if (ret == LZMA_OK && lzma_strm_->avail_in == 0 && num < static_cast<std::streamsize>(len))
        {
          if (read())
          {
            lzma_strm_->next_in  = zbuf_;
            lzma_strm_->avail_in = zlen_;

            if (num == 0)
            {
              lzma_strm_->next_out  = buf;
              lzma_strm_->avail_out = len;
            }

            ret = lzma_code(lzma_strm_, LZMA_RUN);

            zcur_ = zlen_ - lzma_strm_->avail_in;

            if (ret != LZMA_OK && ret != LZMA_STREAM_END)
            {
              cannot_decompress(pathname_, "a lzma decompression error was detected in the zip compressed data");
              num = -1;
              zend_ = true;
            }
            else
            {
              num = len - lzma_strm_->avail_out;
              zend_ = ret == LZMA_STREAM_END;
            }
          }
          else
          {
            cannot_decompress(pathname_, "EOF detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
        }
        
        if (zend_)
          lzma_end(lzma_strm_);
      }
#endif
      else
      {
        // copy the stored zip data until its size is reduced to zero
        if (size > 0)
        {
          if (zcur_ < zlen_ || read())
          {
            num = zlen_ - zcur_;
            if (size < static_cast<uint64_t>(num))
              num = static_cast<std::streamsize>(size);

            memcpy(buf, zbuf_ + zcur_, static_cast<size_t>(num));

            zcur_ += static_cast<size_t>(num);
            size -= num;
          }
          else
          {
            cannot_decompress(pathname_, "EOF detected in the zip compressed data");
            num = -1;
            zend_ = true;
          }
        }

        if (size == 0)
          zend_ = true;
      }

#ifdef WITH_CRC32
      // update zip crc32
      crc32(buf, static_cast<size_t>(num));
#endif

      return num;
    }

    // read zip data descriptor, if present, and check crc
    bool descriptor()
    {
      if (zend_)
      {
        // if we are at the end of the zip data and there is a descriptor, then read it
        if ((flag & 8) != 0)
        {
          // read data descriptor data and check descriptor magic
          const unsigned char *data = read_num(16);
          if (data == NULL || u32(data) != DESCRIPTOR_MAGIC)
          {
            cannot_decompress(pathname_, "an error was detected in the zip compressed data");
            return false;
          }

          crc   = u32(data + 4);
          size  = u32(data + 8);
          usize = u32(data + 12);
        }

        // we arrived at a new zip local file header
        znew_ = true;

#ifdef WITH_CRC32
        // now that we have the crc32 value (from the header or the descriptor), check the integrity of the decompressed data
        if (~zcrc_ != crc)
        {
          cannot_decompress(pathname_, "a crc error was detected in the zip compressed data");
          return false;
        }
#endif
      }

      return true;
    }
    
    // peek zip data block, return pointer to buffer and length of data available (max ZIPBLOCK when available)
    std::pair<const unsigned char*,size_t> peek()
    {
      if (zcur_ > 0)
      {
        zlen_ -= zcur_;
        memmove(zbuf_, zbuf_ + zcur_, zlen_);
        zcur_ = 0;
        int ret = fread(zbuf_ + zlen_, 1, ZIPBLOCK - zlen_, file_);
        if (ret >= 0)
          zlen_ += ret;
        if (z_strm_ != NULL)
        {
          z_strm_->next_in  = zbuf_;
          z_strm_->avail_in = zlen_;
        }
#ifdef HAVE_LIBBZ2
        else if (bz_strm_ != NULL)
        {
          bz_strm_->next_in  = reinterpret_cast<char*>(zbuf_);
          bz_strm_->avail_in = zlen_;
        }
#endif
#ifdef HAVE_LIBLZMA
        else if (lzma_strm_ != NULL)
        {
          lzma_strm_->next_in  = zbuf_;
          lzma_strm_->avail_in = zlen_;
        }
#endif
      }

      return std::pair<const unsigned char*,size_t>(zbuf_, zlen_);
    }

    // read zip data block of num bytes, return pointer to buffer with at least num bytes or NULL when unsuccessful
    const unsigned char *read_num(size_t num)
    {
      if (num > ZIPBLOCK)
        num = ZIPBLOCK;

      if (zlen_ - zcur_ >= num)
      {
        const unsigned char *ptr = zbuf_ + zcur_;
        zcur_ += num;
        return ptr;
      }

      zlen_ -= zcur_;
      memmove(zbuf_, zbuf_ + zcur_, zlen_);
      zcur_ = 0;
      int ret = fread(zbuf_ + zlen_, 1, ZIPBLOCK - zlen_, file_);
      if (ret >= 0)
      {
        zlen_ += ret;
        if (zlen_ >= num)
        {
          zcur_ = num;
          return zbuf_;
        }
      }

      warning("cannot read", pathname_);
      return NULL;
    }

    // read zip data into buffer zbuf_[], should be called when zcur_ >= zlen_
    bool read()
    {
      zcur_ = zlen_ = 0;

      int ret = fread(zbuf_, 1, ZIPBLOCK, file_);
      if (ret <= 0)
        return false;

      zlen_ = static_cast<size_t>(ret);
      return true;
    }

#ifdef WITH_CRC32
    // update zip crc32
    void crc32(const unsigned char *buf, size_t len)
    {
      while (len-- > 0)
      {
        zcrc_ ^= *buf++;
        for (int k = 0; k < 8; k++)
          zcrc_ = (zcrc_ >> 1) ^ (0xedb88320 & (0 - (zcrc_ & 1)));
      }
    }
#endif

    const char   *pathname_;       // the pathname of the compressed file
    FILE         *file_;           // input file
    z_stream     *z_strm_;         // zlib stream handle
    bz_stream    *bz_strm_;        // bzip2 stream handle
    lzma_stream  *lzma_strm_;      // lzma stream handle
    unsigned char zbuf_[ZIPBLOCK]; // buffer with compressed zip file data to decompress
    size_t        zcur_;           // current position in the buffer, less or equal to zlen_
    size_t        zlen_;           // length of the compressed data in the buffer
    uint32_t      zcrc_;           // crc32 of the decompressed data
    bool          znew_;           // true when reached a zip local file header
    bool          zend_;           // true when reached the end of compressed data, a descriptor and/or header follows

  };

  // return true if pathname has a bzlib2 filename extension
  static bool is_bz(const char *pathname)
  {
    return has_ext(pathname, ".bz.bz2.bzip2.tb2.tbz.tbz2.tz2");
  }

  // return true if pathname has a lzma filename extension
  static bool is_xz(const char *pathname)
  {
    return has_ext(pathname, ".lzma.xz.tlz.txz");
  }

  // return true if pathname has a compress (Z) filename extension
  static bool is_Z(const char *pathname)
  {
    return has_ext(pathname, ".Z");
  }

  // return true if pathname has a zip filename extension
  static bool is_zip(const char *pathname)
  {
    return has_ext(pathname, ".zip.ZIP");
  }

  // check if pathname extension matches on of the given filename extensions
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
      zipinfo_(NULL),
      cur_(0),
      len_(0)
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
      try
      {
        xzfile_ = new XZ(file);
        lzma_ret ret = lzma_auto_decoder(&xzfile_->strm, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED);
        if (ret != LZMA_OK)
        {
          warning("lzma_stream_decoder error", pathname);

          delete xzfile_;
          xzfile_ = NULL;
        }
      }
      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
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
    else if (is_zip(pathname))
    {
      // open zip compressed file
      try
      {
        zipinfo_ = new ZipInfo(pathname, file);

        // read the zip header of the first compressed file
        if (!zipinfo_->header())
        {
          delete zipinfo_;
          zipinfo_ = NULL;
        }
      }
      catch (const std::bad_alloc&)
      {
        errno = ENOMEM;
        warning("zip open error", pathname);
      }
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

  // no copy constructor
  zstreambuf(const zstreambuf&) = delete;

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
    else if (zipinfo_ != NULL)
    {
      // close zip compressed file
      delete zipinfo_;
    }
    else if (gzfile_ != Z_NULL)
    {
      // close zlib compressed file
      gzclose_r(gzfile_);
    }
  }

  // decompress a block of data into buf[0..len-1], return number of bytes decompressed, zero on stream end or negative on error
  std::streamsize decompress(unsigned char *buf, size_t len)
  {
    std::streamsize num = 0;

#ifdef HAVE_LIBBZ2
    if (bzfile_ != NULL)
    {
      // decompress a bzlib compressed block into the given buf[]
      int err = 0;
      num = BZ2_bzRead(&err, bzfile_, buf, len);

      // an error occurred?
      if (err != BZ_OK && err != BZ_STREAM_END)
      {
        cannot_decompress(pathname_, "an error was detected in the bzip2 compressed data");
        num = -1;
      }

      // decompressed the last block or there was an error?
      if (num <= 0 || err == BZ_STREAM_END)
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

      // decompress non-empty xzfile_->zbuf[] into the given buf[]
      if (xzfile_->strm.avail_in > 0 && xzfile_->strm.avail_out == 0)
      {
        xzfile_->strm.next_out = buf;
        xzfile_->strm.avail_out = len;

        ret = lzma_code(&xzfile_->strm, xzfile_->zend ? LZMA_FINISH : LZMA_RUN);

        if (ret != LZMA_OK && ret != LZMA_STREAM_END)
        {
          cannot_decompress(pathname_, "an error was detected in the lzma compressed data");
          num = -1;
        }
        else
        {
          num = len - xzfile_->strm.avail_out;
        }
      }

      // read compressed data into xzfile_->zbuf[] and decompress xzfile_->zbuf[] into the given buf[]
      if (ret == LZMA_OK && xzfile_->strm.avail_in == 0 && num < static_cast<std::streamsize>(len) && !xzfile_->zend)
      {
        xzfile_->zlen = fread(xzfile_->zbuf, 1, Z_BUF_LEN, xzfile_->file);

        if (ferror(xzfile_->file))
        {
          warning("cannot read", pathname_);
          xzfile_->zend = true;
        }
        else
        {
          if (feof(xzfile_->file))
            xzfile_->zend = true;

          xzfile_->strm.next_in = xzfile_->zbuf;
          xzfile_->strm.avail_in = xzfile_->zlen;

          if (num == 0)
          {
            xzfile_->strm.next_out = buf;
            xzfile_->strm.avail_out = len;
          }

          ret = lzma_code(&xzfile_->strm, xzfile_->zend ? LZMA_FINISH : LZMA_RUN);

          if (ret != LZMA_OK && ret != LZMA_STREAM_END)
          {
            cannot_decompress(pathname_, "an error was detected in the lzma compressed data");
            num = -1;
          }
          else
          {
            num = len - xzfile_->strm.avail_out;
          }
        }
      }

      // decompressed the last block or there was an error?
      if (num <= 0 || ret == LZMA_STREAM_END)
      {
        delete xzfile_;
        xzfile_ = NULL;
      }
    }
    else
#endif
    if (zzfile_ != NULL)
    {
      // decompress a compress (Z) compressed block into the given buf[]
      num = z_read(zzfile_, buf, static_cast<int>(len)); 

      // an error occurred?
      if (num < 0)
        cannot_decompress(pathname_, "an error was detected in the compressed data");

      // decompressed the last block or there was an error?
      if (num <= 0)
      {
        z_close(zzfile_);
        zzfile_ = NULL;
      }
    }
    else if (zipinfo_ != NULL)
    {
      // decompress a zip compressed block into the guven buf[]
      num = zipinfo_->decompress(buf, len);

      // there was an error?
      if (num < 0)
      {
        delete zipinfo_;
        zipinfo_ = NULL;
      }
    }
    else if (gzfile_ != Z_NULL)
    {
      // decompress a zlib compressed block into the given buf[]
      num = gzread(gzfile_, buf, len);

      // an error occurred?
      if (num < 0)
      {
        int err;
        const char *message = gzerror(gzfile_, &err);

        if (err == Z_ERRNO)
          warning("cannot read", pathname_);
        else
          cannot_decompress(pathname_, message);
      }

      // decompressed the last block?
      if (num < static_cast<std::streamsize>(len))
      {
        gzclose_r(gzfile_);
        gzfile_ = Z_NULL;
      }
    }

    return num;
  }

  // get pointer to the internal buffer and its max size
  void get_buffer(unsigned char *& buffer, size_t& maxlen)
  {
    buffer = buf_;
    maxlen = Z_BUF_LEN;
  }

  // return zip info when unzipping a file, if at end then proceed to the next file to unzip, otherwise return NULL, 
  const ZipInfo *zipinfo()
  {
    if (zipinfo_ && !zipinfo_->header())
    {
      delete zipinfo_;
      zipinfo_ = NULL;
    }

    return zipinfo_;
  }

  // return pointer and length to current data when unzipping a file, NULL otherwise
  std::pair<const unsigned char*,size_t> zippeek()
  {
    if (zipinfo_)
      return zipinfo_->peek();

    return std::pair<const unsigned char*,size_t>(NULL, 0);
  }

 protected:

  // compress (Z) file handle
  typedef void *zzFile;

  // bzip2 file handle
  typedef void *bzFile;

#ifdef HAVE_LIBLZMA

  // lzma and xz state data
  struct XZ {

    XZ(FILE *file)
      :
        file(file),
        strm(LZMA_STREAM_INIT),
        zlen(0),
        zend(false)
    { }

    ~XZ()
    {
      lzma_end(&strm);
    }

    FILE         *file;
    lzma_stream   strm;
    unsigned char zbuf[Z_BUF_LEN];
    size_t        zlen;
    bool          zend;

  };
  
  // lzma/xz file handle
  typedef struct XZ *xzFile;

#else

  // unused lzma file handle
  typedef void *xzFile;

#endif

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
        memcpy(s, buf_ + cur_, static_cast<size_t>(k));
        cur_ += k;
        return n;
      }
      memcpy(s, buf_ + cur_, static_cast<size_t>(len_ - cur_));
      s += len_ - cur_;
      k -= len_ - cur_;
      cur_ = len_;
    }
    return n;
  }

  const char     *pathname_;       // the pathname of the compressed file
  zzFile          zzfile_;         // compress (Z) file handle
  gzFile          gzfile_;         // zlib file handle
  bzFile          bzfile_;         // bzip2 file handle
  xzFile          xzfile_;         // lzma/xz file handle
  ZipInfo        *zipinfo_;        // zip file and info handle
  unsigned char   buf_[Z_BUF_LEN]; // buffer with decompressed stream data
  std::streamsize cur_;            // current position in buffer to read the stream data, less or equal to len_
  std::streamsize len_;            // length of decompressed data in the buffer
};

#endif
