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
@copyright (c) 2019-2020, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_HPP
#define ZSTREAM_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <streambuf>
#include <zlib.h>

// Z decompress z_open(), z_read(), z_close()
#include "zopen.h"

// if we have libbz2 (bzip2), otherwise declare incomplete bz_stream
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#else
struct bz_stream;
#endif

// if we have liblzma (xz utils), otherwise declare incomplete lzma_stream
#ifdef HAVE_LIBLZMA
#include <lzma.h>
#else
struct lzma_stream;
#endif

// TODO this feature is disabled as it is too slow, we should optimize crc32() with a table
// use zip crc integrity check at the cost of a significant slow down?
// #define WITH_ZIP_CRC32

// buffer size to hold compressed data copied from compressed files
#ifndef Z_BUF_LEN
#define Z_BUF_LEN 65536
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
    ZipInfo(const char *pathname, FILE *file, const unsigned char *buf = NULL, size_t len = 0)
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
    {
      // copy initial buffer data, when specified
      if (buf != NULL && len > 0)
      {
        zlen_ = (len < Z_BUF_LEN ? len : Z_BUF_LEN);
        memcpy(zbuf_, buf, zlen_);
      }
    }

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
    uint32_t    crc;     // zip crc-32
    uint64_t    size;    // zip compressed file size
    uint64_t    usize;   // zip uncompressed file size (unused)
    std::string name;    // zip file name from local file header or extra field

   protected:

    static constexpr size_t ZIPBLOCK = 65536; // block size to read zip data, at least 64K to fit long 64K pathnames

    static constexpr uint16_t COMPRESS_HEADER_MAGIC = 0x9d1f;     // compress header magic
    static constexpr uint16_t DEFLATE_HEADER_MAGIC  = 0x8b1f;     // zlib deflate header magic
    static constexpr uint32_t ZIP_HEADER_MAGIC      = 0x04034b50; // zip local file header magic
    static constexpr uint32_t ZIP_DESCRIPTOR_MAGIC  = 0x08074b50; // zip descriptor magic

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
      if (data == NULL || u32(data) != ZIP_HEADER_MAGIC)
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

      for (uint16_t num = 0; num < extralen; num += 4 + u16(data + num + 2))
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

          z_strm_->zalloc = Z_NULL;
          z_strm_->zfree  = Z_NULL;
          z_strm_->opaque = Z_NULL;
        }

        // prepare to inflate the remainder of the buffered data
        z_strm_->next_in   = zbuf_ + zcur_;
        z_strm_->avail_in  = zlen_ - zcur_;
        z_strm_->next_out  = Z_NULL;
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

    // read and decompress zip file data into buf[0..len-1], return number of bytes decompressed
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

          if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
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

            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
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
        // copy the stored zip data until the stored data size is reduced to zero
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

#ifdef WITH_ZIP_CRC32
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
          if (data == NULL || u32(data) != ZIP_DESCRIPTOR_MAGIC)
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

#ifdef WITH_ZIP_CRC32
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
        size_t ret = fread(zbuf_ + zlen_, 1, ZIPBLOCK - zlen_, file_);
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
      size_t ret = fread(zbuf_ + zlen_, 1, ZIPBLOCK - zlen_, file_);
      if (ret > 0)
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
      zcur_ = 0;
      zlen_ = fread(zbuf_, 1, ZIPBLOCK, file_);
      return zlen_ > 0;
    }

#ifdef WITH_ZIP_CRC32
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
  zstreambuf()
    :
      pathname_(NULL),
      file_(NULL),
      zfile_(NULL),
      zzfile_(NULL),
      bzfile_(NULL),
      xzfile_(NULL),
      zipinfo_(NULL),
      cur_(0),
      len_(0)
  { }

  // constructor
  zstreambuf(const char *pathname, FILE *file)
    :
      pathname_(pathname),
      file_(file),
      zfile_(NULL),
      zzfile_(NULL),
      bzfile_(NULL),
      xzfile_(NULL),
      zipinfo_(NULL),
      cur_(0),
      len_(0)
  {
    open(pathname, file);
  }

  // no copy constructor
  zstreambuf(const zstreambuf&) = delete;

  // destructor
  virtual ~zstreambuf()
  {
    close();
  }

  // open the decompression stream
  void open(const char *pathname, FILE *file)
  {
    // close old stream, if still open
    close();

    if (file == NULL)
      return;

    pathname_ = pathname;
    file_ = file;

    cur_ = 0;
    len_ = 0;

    if (is_bz(pathname))
    {
#ifdef HAVE_LIBBZ2
      // open bzip2 compressed file
      try
      {
        bzfile_ = new BZ();
        int ret = BZ2_bzDecompressInit(&bzfile_->strm, 0, 0);
        if (ret != BZ_OK)
        {
          warning("BZ2_bzDecompressInit error", pathname);

          delete bzfile_;
          bzfile_ = NULL;
          file_ = NULL;
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
    else if (is_xz(pathname))
    {
#ifdef HAVE_LIBLZMA
      // open xz/lzma compressed file
      try
      {
        xzfile_ = new XZ();
        lzma_ret ret = lzma_auto_decoder(&xzfile_->strm, UINT64_MAX, LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED);
        if (ret != LZMA_OK)
        {
          warning("lzma_stream_decoder error", pathname);

          delete xzfile_;
          xzfile_ = NULL;
          file_ = NULL;
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
    else
    {
      // read compression format magic bytes
      std::streamsize num = fread(buf_, 1, 2, file);

      if (num == 2)
      {
        if (ZipInfo::u16(buf_) == ZipInfo::DEFLATE_HEADER_MAGIC)
        {
          // open zlib compressed file
          try
          {
            zfile_ = new Z();

            // copy the gzip header's magic bytes to zbuf[], needed by inflate()
            zfile_->zbuf[0] = buf_[0];
            zfile_->zbuf[1] = buf_[1];
            zfile_->zlen = 2;
            zfile_->strm.next_in  = zfile_->zbuf;
            zfile_->strm.avail_in = zfile_->zlen;

            // inflate gzip compressed data starting with a gzip header
            if (inflateInit2(&zfile_->strm, 16 + MAX_WBITS) != Z_OK)
            {
              cannot_decompress(pathname_, zfile_->strm.msg ? zfile_->strm.msg : "inflateInit2 failed");

              delete zfile_;
              zfile_ = NULL;
              file_ = NULL;
            }
          }
          catch (const std::bad_alloc&)
          {
            errno = ENOMEM;
            warning("zlib open error", pathname);
          }
        }
        else if (ZipInfo::u16(buf_) == ZipInfo::COMPRESS_HEADER_MAGIC)
        {
          // open compress (Z) compressed file
          if ((zzfile_ = z_open(file, "r", 0, 1)) == NULL)
            warning("zopen error", pathname);
        }
        else
        {
          // read two more bytes of the compression format's magic bytes
          num = fread(buf_ + 2, 1, 2, file);

          if (num == 2 && ZipInfo::u32(buf_) == ZipInfo::ZIP_HEADER_MAGIC)
          {
            // open zip compressed file
            try
            {
              zipinfo_ = new ZipInfo(pathname, file, buf_, 4);

              // read the zip header of the first compressed file
              if (!zipinfo_->header())
              {
                delete zipinfo_;
                zipinfo_ = NULL;
                file_ = NULL;
              }
            }
            catch (const std::bad_alloc&)
            {
              errno = ENOMEM;
              warning("zip open error", pathname);
            }
          }
          else if (num >= 0)
          {
            // no compression
            len_ = num + 2;
            num = fread(buf_ + 4, 1, Z_BUF_LEN - 4, file);
            if (num >= 0)
            {
              len_ += num;
            }
            else
            {
              warning("cannot read", pathname);
              file_ = NULL;
            }
          }
        }
      }

      if (num < 0)
      {
        warning("cannot read", pathname);
        file_ = NULL;
      }
    }
  }

  // close the decompression stream
  void close()
  {
    if (zfile_ != NULL)
    {
      // close zlib compressed file
      delete zfile_;
      zfile_ = NULL;
    }
    else if (zzfile_ != NULL)
    {
      // close compress (Z) compressed file
      z_close(zzfile_);
      zzfile_ = NULL;
    }
#ifdef HAVE_LIBBZ2
    else if (bzfile_ != NULL)
    {
      // close bzlib compressed file
      delete bzfile_;
      bzfile_ = NULL;
    }
#endif
#ifdef HAVE_LIBLZMA
    else if (xzfile_ != NULL)
    {
      // close lzma compressed file
      delete xzfile_;
      xzfile_ = NULL;
    }
    else
#endif
    if (zipinfo_ != NULL)
    {
      // close zip compressed file
      delete zipinfo_;
      zipinfo_ = NULL;
    }
  }

  // copy or decompress a block of data into buf[0..len-1], return number of bytes decompressed, zero on EOF or negative on error
  std::streamsize decompress(unsigned char *buf, size_t len)
  {
    if (cur_ >= len_)
      return next(buf, len);

    size_t num = static_cast<size_t>(len_ - cur_);
    if (num > len)
      num = len;

    memcpy(buf, buf_ + cur_, num);

    cur_ += num;

    return static_cast<std::streamsize>(num);
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

  // zlib deflate data
  struct Z {

    Z()
      :
        zlen(0),
        zend(false)
    {
      strm.zalloc    = Z_NULL;
      strm.zfree     = Z_NULL;
      strm.opaque    = Z_NULL;
      strm.next_in   = Z_NULL;
      strm.avail_in  = 0;
      strm.next_out  = Z_NULL;
      strm.avail_out = 0;
    }

    ~Z()
    {
      inflateEnd(&strm);
    }

    z_stream      strm;
    unsigned char zbuf[Z_BUF_LEN];
    size_t        zlen;
    bool          zend;

  };
  
  // zlib deflate file handle
  typedef struct Z *zFile;

  // compress (Z) file handle
  typedef void *zzFile;

#ifdef HAVE_LIBBZ2

  // bzip2 state data
  struct BZ {

    BZ()
      :
        zlen(0),
        zend(false)
    {
      strm.bzalloc   = NULL;
      strm.bzfree    = NULL;
      strm.opaque    = NULL;
      strm.next_in   = NULL;
      strm.avail_in  = 0;
      strm.next_out  = NULL;
      strm.avail_out = 0;
    }

    ~BZ()
    {
      BZ2_bzDecompressEnd(&strm);
    }

    bz_stream     strm;
    unsigned char zbuf[Z_BUF_LEN];
    size_t        zlen;
    bool          zend;

  };
  
  // bzip2 file handle
  typedef struct BZ *bzFile;

#else

  // unused bzip2 file handle
  typedef void *bzFile;

#endif

#ifdef HAVE_LIBLZMA

  // lzma and xz state data
  struct XZ {

    XZ()
      :
        strm(LZMA_STREAM_INIT),
        zlen(0),
        zend(false)
    { }

    ~XZ()
    {
      lzma_end(&strm);
    }

    lzma_stream   strm;
    unsigned char zbuf[Z_BUF_LEN];
    size_t        zlen;
    bool          zend;

  };
  
  // lzma/xz file handle
  typedef struct XZ *xzFile;

#else

  // unused lzma/xz file handle
  typedef void *xzFile;

#endif

  // fetch and decompress the next block of data into buf[0..len-1], return number of bytes decompressed, zero on EOF or negative on error
  std::streamsize next(unsigned char *buf, size_t len)
  {
    std::streamsize num = 0;

    if (zfile_ != NULL)
    {
      while (true)
      {
        int ret = Z_OK;

        // decompress non-empty zfile_->zbuf[] into the given buf[]
        if (zfile_->strm.avail_in > 0 && zfile_->strm.avail_out == 0)
        {
          zfile_->strm.next_out  = buf;
          zfile_->strm.avail_out = len;

          ret = inflate(&zfile_->strm, Z_NO_FLUSH);

          if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
          {
            cannot_decompress(pathname_, "an error was detected in the gzip compressed data");
            num = -1;
          }
          else
          {
            num = len - zfile_->strm.avail_out;
          }
        }

        // read compressed data into zfile_->zbuf[] and decompress zfile_->zbuf[] into the given buf[]
        if (ret == Z_OK && zfile_->strm.avail_in == 0 && num < static_cast<std::streamsize>(len) && !zfile_->zend)
        {
          zfile_->zlen = fread(zfile_->zbuf, 1, Z_BUF_LEN, file_);

          if (ferror(file_))
          {
            warning("cannot read", pathname_);
            zfile_->zend = true;
          }
          else
          {
            if (feof(file_))
              zfile_->zend = true;

            zfile_->strm.next_in  = zfile_->zbuf;
            zfile_->strm.avail_in = zfile_->zlen;

            if (num == 0)
            {
              zfile_->strm.next_out  = buf;
              zfile_->strm.avail_out = len;
            }

            ret = inflate(&zfile_->strm, Z_NO_FLUSH);

            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
            {
              cannot_decompress(pathname_, "an error was detected in the gzip compressed data");
              num = -1;
            }
            else
            {
              num = len - zfile_->strm.avail_out;
            }
          }
        }

        if (num >= 0 && ret == Z_STREAM_END)
        {
          // try to decompress the next concatenated gzip data
          if (inflateReset(&zfile_->strm) == Z_OK)
          {
            zfile_->strm.next_out  = Z_NULL;
            zfile_->strm.avail_out = 0;

            ret = Z_OK;

            if (num == 0)
              continue;
          }
        }

        // decompressed the last block or there was an error?
        if (num <= 0 || ret == Z_STREAM_END)
        {
          delete zfile_;
          zfile_ = NULL;
          file_ = NULL;
        }

        break;
      }
    }
    else if (zzfile_ != NULL)
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
        file_ = NULL;
      }
    }
#ifdef HAVE_LIBBZ2
    else if (bzfile_ != NULL)
    {
      while (true)
      {
        int ret = BZ_OK;

        // decompress non-empty bzfile_->zbuf[] into the given buf[]
        if (bzfile_->strm.avail_in > 0 && bzfile_->strm.avail_out == 0)
        {
          bzfile_->strm.next_out  = reinterpret_cast<char*>(buf);
          bzfile_->strm.avail_out = len;

          ret = BZ2_bzDecompress(&bzfile_->strm);

          if (ret != BZ_OK && ret != BZ_STREAM_END)
          {
            cannot_decompress(pathname_, "an error was detected in the bzip2 compressed data");
            num = -1;
          }
          else
          {
            num = len - bzfile_->strm.avail_out;
          }
        }

        // read compressed data into bzfile_->zbuf[] and decompress bzfile_->zbuf[] into the given buf[]
        if (ret == BZ_OK && bzfile_->strm.avail_in == 0 && num < static_cast<std::streamsize>(len) && !bzfile_->zend)
        {
          bzfile_->zlen = fread(bzfile_->zbuf, 1, Z_BUF_LEN, file_);

          if (ferror(file_))
          {
            warning("cannot read", pathname_);
            bzfile_->zend = true;
          }
          else
          {
            if (feof(file_))
              bzfile_->zend = true;

            bzfile_->strm.next_in  = reinterpret_cast<char*>(bzfile_->zbuf);
            bzfile_->strm.avail_in = bzfile_->zlen;

            if (num == 0)
            {
              bzfile_->strm.next_out  = reinterpret_cast<char*>(buf);
              bzfile_->strm.avail_out = len;
            }

            ret = BZ2_bzDecompress(&bzfile_->strm);

            if (ret != BZ_OK && ret != BZ_STREAM_END)
            {
              cannot_decompress(pathname_, "an error was detected in the bzip2 compressed data");
              num = -1;
            }
            else
            {
              num = len - bzfile_->strm.avail_out;
            }
          }
        }

        if (num >= 0 && ret == BZ_STREAM_END)
        {
          // try to decompress the next concatenated bzip2 data
          if (BZ2_bzDecompressEnd(&bzfile_->strm) == BZ_OK && BZ2_bzDecompressInit(&bzfile_->strm, 0, 0) == BZ_OK)
          {
            bzfile_->strm.next_out  = NULL;
            bzfile_->strm.avail_out = 0;

            ret = BZ_OK;

            if (num == 0)
              continue;
          }
        }

        // decompressed the last block or there was an error?
        if (num <= 0 || ret == BZ_STREAM_END)
        {
          delete bzfile_;
          bzfile_ = NULL;
          file_ = NULL;
        }

        break;
      }
    }
#endif
#ifdef HAVE_LIBLZMA
    else if (xzfile_ != NULL)
    {
      lzma_ret ret = LZMA_OK;

      // decompress non-empty xzfile_->zbuf[] into the given buf[]
      if (xzfile_->strm.avail_in > 0 && xzfile_->strm.avail_out == 0)
      {
        xzfile_->strm.next_out  = buf;
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
        xzfile_->zlen = fread(xzfile_->zbuf, 1, Z_BUF_LEN, file_);

        if (ferror(file_))
        {
          warning("cannot read", pathname_);
          xzfile_->zend = true;
        }
        else
        {
          if (feof(file_))
            xzfile_->zend = true;

          xzfile_->strm.next_in  = xzfile_->zbuf;
          xzfile_->strm.avail_in = xzfile_->zlen;

          if (num == 0)
          {
            xzfile_->strm.next_out  = buf;
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
        file_ = NULL;
      }
    }
#endif
    else if (zipinfo_ != NULL)
    {
      // decompress a zip compressed block into the guven buf[]
      num = zipinfo_->decompress(buf, len);

      // there was an error?
      if (num < 0)
      {
        delete zipinfo_;
        zipinfo_ = NULL;
        file_ = NULL;
      }
    }
    else if (file_ != NULL)
    {
      // pass through, without decompression
      num = fread(buf, 1, len, file_);

      // end of file or error?
      if (num < static_cast<std::streamsize>(len))
        file_ = NULL;
    }

    return num;
  }

  // read a decompressed block into buf_[], returns pending next character or EOF
  int_type peek()
  {
    cur_ = 0;
    len_ = next(buf_, Z_BUF_LEN);
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
    return file_ == NULL && cur_ >= len_ ? -1 : 0;
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
  FILE           *file_;           // the compressed file
  zFile           zfile_;          // zlib file handle
  zzFile          zzfile_;         // compress (Z) file handle
  bzFile          bzfile_;         // bzip2 file handle
  xzFile          xzfile_;         // lzma/xz file handle
  ZipInfo        *zipinfo_;        // zip file and zip info handle
  unsigned char   buf_[Z_BUF_LEN]; // buffer with decompressed stream data
  std::streamsize cur_;            // current position in buffer to read the stream data, less or equal to len_
  std::streamsize len_;            // length of decompressed data in the buffer

};

#endif
