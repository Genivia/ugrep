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
@brief     file decompression streams - zstreambuf extends std::streambuf
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZSTREAM_HPP
#define ZSTREAM_HPP

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <climits>
#include <exception>
#include <streambuf>
#include <zlib.h>

// Z decompress z_open(), z_read(), z_close()
#include "zopen.h"

// if we have libbz2 (bzip2), otherwise declare incomplete bz_stream for ZipInfo
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#else
struct bz_stream;
#endif

// if we have liblzma (xz utils), otherwise declare incomplete lzma_stream for ZipInfo
#ifdef HAVE_LIBLZMA
#include <lzma.h>
#else
struct lzma_stream;
#endif

// if we have liblz4, otherwise declare incomplete lz4_stream for ZipInfo
#ifdef HAVE_LIBLZ4
#include <lz4.h>
typedef LZ4_streamDecode_t *lz4_stream;
// define LZ4_DECODER_RING_BUFFER_SIZE when not defined by old lz4
#ifndef LZ4_DECODER_RING_BUFFER_SIZE
#define LZ4_DECODER_RING_BUFFER_SIZE(maxBlockSize) (65536 + 14 + (maxBlockSize))
#endif
#else
struct lz4_stream;
#endif

// if we have libzstd, otherwise declare incomplete zstd_stream for ZipInfo
#ifdef HAVE_LIBZSTD
#include <zstd.h>
typedef ZSTD_DStream zstd_stream;
#else
struct zstd_stream;
#endif

// if we have libbrotlidec
#ifdef HAVE_LIBBROTLI
#include <brotli/decode.h>
#endif

// if we have libbzip3
#ifdef HAVE_LIBBZIP3
extern "C" {
#include <libbz3.h>
}
#endif

// use 7zip LZMA SDK using a viizip decompressor wrapper
#ifndef WITH_NO_7ZIP
#include "viizip.h"
// 7zip archive part pathname max length if limits.h doesn't define it
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

// zip decompression crc check disabled as this is too slow, we should optimize crc32() with a table
// use zip crc integrity check at the cost of a significant slow down?
// #define WITH_ZIP_CRC32

// buffer size to hold compressed data that is block-wise copied from compressed files
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

    // C++ wrapper for our C viizip 7zip decompressor
    struct sz_stream {
#ifndef WITH_NO_7ZIP
      sz_stream(const char *pathname, FILE *file)
      {
        viizip = viinew(file);

        /* non-seekable 7zip files cannot be decompressed */
        if (viizip == NULL)
          throw std::invalid_argument(pathname);
      }

      ~sz_stream()
      {
        viifree(viizip);
      }

      // next 7zip file to decompress and get its info, return 0 if OK, if none return 1 or -1 on error
      int get(std::string& name, time_t& mtime, uint64_t& usize)
      {
        char buf[PATH_MAX];
        int res = viiget(viizip, buf, PATH_MAX, &mtime, &usize);
        if (res)
          return res;

        name.assign(buf);

        return 0;
      }

      // read and decompress 7zip file data into buf[0..len-1], return number of bytes decompressed, 0 for EOF or -1 for error
      inline std::streamsize decompress(unsigned char *buf, size_t len)
      {
        return viidec(viizip, buf, len);
      }

      struct viizip *viizip;
#endif
    };

    // zip compression methods, STORE and DEFLATE are common. others are less common and some are specific to WinZip (.zipx)
    enum class Compression : uint16_t { STORE = 0, DEFLATE = 8, BZIP2 = 12, LZMA = 14, ZSTD = 93, XZ = 95, /* PPMD = 98 not supported */ };

    // constructor
    ZipInfo(const char *pathname, FILE *file, const unsigned char *buf = NULL, size_t len = 0)
      :
        pathname_(pathname),
        file_(file),
        z_strm_(NULL),
        bz_strm_(NULL),
        lzma_strm_(NULL),
        zstd_strm_(NULL),
        sz_strm_(NULL),
        zcur_(0),
        zlen_(0),
        zcrc_(0xffffffff),
        znew_(true),
        zend_(false)
    {
      // copy initial buffer data into zbuf_[], when specified
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
#ifdef HAVE_LIBZSTD
      if (zstd_strm_ != NULL)
        ZSTD_freeDStream(zstd_strm_);
#endif
#ifndef WITH_NO_7ZIP
      if (sz_strm_ != NULL)
        delete sz_strm_;
#endif
    }

    uint16_t    version; // zip version (unused)
    uint16_t    flag;    // zip general purpose bit flag
    Compression method;  // zip compression method
    time_t      mtime;   // zip file modification time and date converted to local time
    uint32_t    crc;     // zip crc-32
    uint64_t    size;    // zip compressed file size, if known
    uint64_t    usize;   // zip uncompressed file size
    std::string name;    // zip file name extracted from local file header or zip extra field

   protected:

    static const size_t ZIPBLOCK = 65536; // block size to read zip data, at least 64K to fit long 64K pathnames

    static const uint16_t COMPRESS_HEADER_MAGIC = 0x9d1f; // compress header magic
    static const uint16_t DEFLATE_HEADER_MAGIC  = 0x8b1f; // zlib deflate header magic

    static const uint32_t ZIP_HEADER_MAGIC     = 0x04034b50; // zip local file header magic
    static const uint32_t ZIP_EMPTY_MAGIC      = 0x06054b50; // zip empty archive header magic
    static const uint32_t ZIP_DESCRIPTOR_MAGIC = 0x08074b50; // zip descriptor magic

    // read zip local file header if we are at a header, read the header, file name, and extra field
    bool header()
    {
#ifndef WITH_NO_7ZIP
      // if 7zip then get the next file name and info, start decompressing, return false if none
      if (sz_strm_ != NULL)
      {
        // if info was already retrieved, then return
        if (!znew_)
          return true;

        int res = sz_strm_->get(name, mtime, usize);
        if (res < 0)
        {
          cannot_decompress(pathname_, "corrupt 7zip archive");
          return false;
        }

#ifdef WITH_MAX_7ZIP_SIZE
        if (res == 0 && usize > WITH_MAX_7ZIP_SIZE)
        {
          cannot_decompress(pathname_, std::string("7zip archived file size exceeds the set limit: ").append(name).c_str());
          return znew_ = zend_ = true;
        }
#endif

        /* don't advance to the next file */
        znew_ = false;

        zend_ = res;
        return !zend_;
      }
#endif

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

      uint16_t ziptime;
      uint16_t zipdate;

      // get the zip local file header info
      version = u16(data + 4);
      flag    = u16(data + 6);
      method  = static_cast<Compression>(u16(data + 8));
      ziptime = u16(data + 10);
      zipdate = u16(data + 12);
      crc     = u32(data + 14);
      size    = u32(data + 18);
      usize   = u32(data + 22);

      // convert zip time and date
      struct tm ziptm;
      memset(&ziptm, 0, sizeof(ziptm));
      ziptm.tm_sec   = 2 * (ziptime & 0x1f);
      ziptm.tm_min   = (ziptime >> 5) & 0x3f;
      ziptm.tm_hour  = ziptime >> 11;
      ziptm.tm_mday  = zipdate & 0x1f;
      ziptm.tm_mon   = (zipdate >> 5) & 0x3f;
      ziptm.tm_year  = 80 + (zipdate >> 9);
      ziptm.tm_isdst = -1;
      mtime = mktime(&ziptm);

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
        cannot_decompress(pathname_, "corrupt zip archive");
        return false;
      }

      name.assign(reinterpret_cast<const char*>(data), namelen);

      // read the extra field
      data = read_num(extralen);
      if (data == NULL)
      {
        cannot_decompress(pathname_, "corrupt zip archive");
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
        z_strm_->avail_in  = static_cast<uInt>(zlen_ - zcur_);
        z_strm_->next_out  = Z_NULL;
        z_strm_->avail_out = 0;

        // initialize zlib inflate
        if (inflateInit2(z_strm_, -MAX_WBITS) != Z_OK)
        {
          cannot_decompress(pathname_, z_strm_->msg != NULL ? z_strm_->msg : "inflateInit2 failed");
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
        bz_strm_->avail_in  = static_cast<unsigned int>(zlen_ - zcur_);
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
      else if (method == Compression::ZSTD)
      {
#ifdef HAVE_LIBZSTD

        // Zip zstd method
        if (zstd_strm_ == NULL)
        {
          zstd_strm_ = ZSTD_createDStream();

          if (zstd_strm_ == NULL)
          {
            cannot_decompress(pathname_, "out of memory");
            return false;
          }
        }
        else
        {
          // reinitialize zstd decompress
          ZSTD_initDStream(zstd_strm_);
        }

#else

        cannot_decompress(pathname_, "unsupported zip compression method zstd");
        return false;

#endif
      }
      else if (method == Compression::LZMA || method == Compression::XZ)
      {
#ifdef HAVE_LIBLZMA

        // TODO should we support bit 1 clear, i.e. use the size field to terminate the lzma stream?
        // if bit 1 is clear, then there is no EOS in the lzma stream that we require to terminate lzma decompression
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

    // read and decompress zip file data into buf[0..len-1], return number of bytes decompressed, 0 for EOF or -1 for error
    std::streamsize decompress(unsigned char *buf, size_t len)
    {
      // if header() was not called or no more data to decompress, then return 0 to indicate EOF
      if (znew_ || zend_)
        return 0;

      std::streamsize num = 0;

#ifndef WITH_NO_7ZIP
      // 7zip decompression
      if (sz_strm_ != NULL)
      {
        num = sz_strm_->decompress(buf, len);

        if (num < static_cast<std::streamsize>(len))
          znew_ = zend_ = true;

        return num;
      }
#endif

      if (method == Compression::DEFLATE && z_strm_ != NULL)
      {
        while (true)
        {
          int ret = Z_OK;

          // decompress non-empty zbuf_[] into the given buf[]
          if (z_strm_->avail_in > 0 && z_strm_->avail_out == 0)
          {
            z_strm_->next_out  = buf;
            z_strm_->avail_out = static_cast<uInt>(len);

            ret = inflate(z_strm_, Z_NO_FLUSH);

            zcur_ = zlen_ - z_strm_->avail_in;

            if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
            {
              cannot_decompress(pathname_, "a zlib decompression error was detected in the zip compressed data");
              zend_ = true;
              num = -1;
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
              z_strm_->avail_in = static_cast<uInt>(zlen_);

              if (num == 0)
              {
                z_strm_->next_out  = buf;
                z_strm_->avail_out = static_cast<uInt>(len);
              }

              ret = inflate(z_strm_, Z_NO_FLUSH);

              zcur_ = zlen_ - z_strm_->avail_in;

              if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
              {
                cannot_decompress(pathname_, "a zlib decompression error was detected in the zip compressed data");
                zend_ = true;
                num = -1;
              }
              else
              {
                num = len - z_strm_->avail_out;

                if (num == 0 && ret == Z_OK)
                  continue;

                zend_ = ret == Z_STREAM_END;
              }
            }
            else
            {
              cannot_decompress(pathname_, "EOF detected in the zip compressed data");
              zend_ = true;
              num = -1;
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

          break;
        }
      }
#ifdef HAVE_LIBBZ2
      else if (method == Compression::BZIP2 && bz_strm_ != NULL)
      {
        while (true)
        {
          int ret = BZ_OK;

          // decompress non-empty zbuf_[] into the given buf[]
          if (bz_strm_->avail_in > 0 && bz_strm_->avail_out == 0)
          {
            bz_strm_->next_out  = reinterpret_cast<char*>(buf);
            bz_strm_->avail_out = static_cast<unsigned int>(len);

            ret = BZ2_bzDecompress(bz_strm_);

            zcur_ = zlen_ - bz_strm_->avail_in;

            if (ret != BZ_OK && ret != BZ_STREAM_END)
            {
              cannot_decompress(pathname_, "a bzip2 decompression error was detected in the zip compressed data");
              zend_ = true;
              num = -1;
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
              bz_strm_->avail_in = static_cast<unsigned int>(zlen_);

              if (num == 0)
              {
                bz_strm_->next_out  = reinterpret_cast<char*>(buf);
                bz_strm_->avail_out = static_cast<unsigned int>(len);
              }

              ret = BZ2_bzDecompress(bz_strm_);

              zcur_ = zlen_ - bz_strm_->avail_in;

              if (ret != BZ_OK && ret != BZ_STREAM_END)
              {
                cannot_decompress(pathname_, "a bzip2 decompression error was detected in the zip compressed data");
                zend_ = true;
                num = -1;
              }
              else
              {
                num = len - bz_strm_->avail_out;

                if (num == 0 && ret == BZ_OK)
                  continue;

                zend_ = ret == BZ_STREAM_END;
              }
            }
            else
            {
              cannot_decompress(pathname_, "EOF detected in the zip compressed data");
              zend_ = true;
              num = -1;
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

          break;
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
            zend_ = true;
            num = -1;
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
              zend_ = true;
              num = -1;
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
            zend_ = true;
            num = -1;
          }
        }
        
        if (zend_)
          lzma_end(lzma_strm_);
      }
#endif
#ifdef HAVE_LIBZSTD
      else if (method == Compression::ZSTD && zstd_strm_ != NULL)
      {
        ZSTD_outBuffer out = { buf, len, 0 };
        size_t ret = 1;

        if (zcur_ < zlen_)
        {
          ZSTD_inBuffer in = { zbuf_, zlen_, zcur_ };
          ret = ZSTD_decompressStream(zstd_strm_, &out, &in);

          if (ZSTD_isError(ret))
          {
            cannot_decompress(pathname_, "a zstd decompression error was detected in the zip compressed data");
            zend_ = true;
            num = -1;
          }
          else
          {
            zcur_ = in.pos;
            num = static_cast<std::streamsize>(out.pos);
          }
        }

        if (ret != 0 && num < static_cast<std::streamsize>(len) && !zend_)
        {
          if (read())
          {
            ZSTD_inBuffer in = { zbuf_, zlen_, zcur_ };
            ret = ZSTD_decompressStream(zstd_strm_, &out, &in);

            if (ZSTD_isError(ret))
            {
              cannot_decompress(pathname_, "a zstd decompression error was detected in the zip compressed data");
              zend_ = true;
              num = -1;
            }
            else
            {
              zcur_ = in.pos;
              num = static_cast<std::streamsize>(out.pos);
            }
          }
          else
          {
            cannot_decompress(pathname_, "EOF detected in the zip compressed data");
            zend_ = true;
            num = -1;
          }
        }
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
            zend_ = true;
            num = -1;
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
          if (data == NULL)
            return false;

          if (u32(data) != ZIP_DESCRIPTOR_MAGIC)
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
          z_strm_->avail_in = static_cast<uInt>(zlen_);
        }
#ifdef HAVE_LIBBZ2
        else if (bz_strm_ != NULL)
        {
          bz_strm_->next_in  = reinterpret_cast<char*>(zbuf_);
          bz_strm_->avail_in = static_cast<unsigned int>(zlen_);
        }
#endif
#ifdef HAVE_LIBLZMA
        else if (lzma_strm_ != NULL)
        {
          lzma_strm_->next_in  = zbuf_;
          lzma_strm_->avail_in = zlen_;
        }
#endif
#ifdef HAVE_LIBZSTD
        // no action is needed
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
      zlen_ += ret;
      if (zlen_ >= num)
      {
        zcur_ = num;
        return zbuf_;
      }

      if (ferror(file_))
        warning("cannot read", pathname_);
      else
        cannot_decompress(pathname_, "an error was detected in the zip compressed data");
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

    const char   *pathname_;       // the pathname of the zip file
    FILE         *file_;           // input file
    z_stream     *z_strm_;         // zlib stream handle
    bz_stream    *bz_strm_;        // bzip2 stream handle
    lzma_stream  *lzma_strm_;      // xz/lzma stream handle
    zstd_stream  *zstd_strm_;      // zstd stream handle
    sz_stream    *sz_strm_;        // 7zip stream handle
    unsigned char zbuf_[ZIPBLOCK]; // buffer with compressed zip file data to decompress
    size_t        zcur_;           // current position in the zbuf_[] buffer, less or equal to zlen_
    size_t        zlen_;           // length of the compressed data in the zbuf_[] buffer
    uint32_t      zcrc_;           // crc32 of the decompressed data
    bool          znew_;           // true when reached a zip local file header
    bool          zend_;           // true when reached the end of compressed data, a descriptor and/or header follows

  };

  // return true if pathname has a (tar) bzlib2 filename extension
  static bool is_bz(const char *pathname)
  {
    return has_ext(pathname, ".bz.bz2.bzip2.tb2.tbz.tbz2.tz2");
  }

  // return true if pathname has a (tar) xz/lzma filename extension
  static bool is_xz(const char *pathname)
  {
    return has_ext(pathname, ".lzma.xz.tlz.txz");
  }

  // return true if pathname has a lz4 filename extension
  static bool is_lz4(const char *pathname)
  {
    return has_ext(pathname, ".lz4");
  }

  // return true if pathname has a (tar) zstd filename extension
  static bool is_zstd(const char *pathname)
  {
    return has_ext(pathname, ".zst.zstd.tzst");
  }

  // return true if pathname has a br filename extension
  static bool is_br(const char *pathname)
  {
    return has_ext(pathname, ".br");
  }

  // return true if pathname has a bz3 filename extension
  static bool is_bz3(const char *pathname)
  {
    return has_ext(pathname, ".bz3");
  }

  // return true if pathname has a (tar) compress (Z) filename extension
  static bool is_Z(const char *pathname)
  {
    return has_ext(pathname, ".Z.taZ.tZ");
  }

  // return true if pathname has a zip filename extension
  static bool is_zip(const char *pathname)
  {
    return has_ext(pathname, ".zip.zipx.ZIP");
  }

  // return true if pathname has a 7z filename extension
  static bool is_7z(const char *pathname)
  {
    return has_ext(pathname, ".7z.7Z");
  }

  // return true if pathname has a RAR filename extension
  static bool is_rar(const char *pathname)
  {
    return has_ext(pathname, ".rar.RAR");
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

  // default constructor
  zstreambuf()
    :
      pathname_(NULL),
      file_(NULL),
      zfile_(NULL),
      zzfile_(NULL),
      bzfile_(NULL),
      xzfile_(NULL),
      lz4file_(NULL),
      zstdfile_(NULL),
      brfile_(NULL),
      bz3file_(NULL),
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
      lz4file_(NULL),
      zstdfile_(NULL),
      brfile_(NULL),
      bz3file_(NULL),
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
      // open bzip/bzip2 compressed file
      try
      {
        bzfile_ = new BZ();
        int ret = BZ2_bzDecompressInit(&bzfile_->strm, 0, 0);
        if (ret != BZ_OK)
        {
          warning("BZ2_bzDecompressInit failed", pathname);

          delete bzfile_;
          bzfile_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
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
          warning("lzma_stream_decoder failed", pathname);

          delete xzfile_;
          xzfile_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_lz4(pathname))
    {
#ifdef HAVE_LIBLZ4
      // open lz4 compressed file
      try
      {
        lz4file_ = new LZ4();
        if (lz4file_->strm == NULL || lz4file_->buf == NULL || lz4file_->zbuf == NULL)
        {
          warning("LZ4_createStreamDecode failed", pathname);

          delete lz4file_;
          lz4file_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_zstd(pathname))
    {
#ifdef HAVE_LIBZSTD
      // open zstd compressed file
      try
      {
        zstdfile_ = new ZSTD();
        if (zstdfile_->strm == NULL || zstdfile_->zbuf == NULL)
        {
          warning("ZSTD_createDStream failed", pathname);

          delete zstdfile_;
          zstdfile_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_br(pathname))
    {
#ifdef HAVE_LIBBROTLI
      // open brotli compressed file
      try
      {
        brfile_ = new BR();
        if (brfile_->strm == NULL)
        {
          warning("BrotliDecoderCreateInstance failed", pathname);

          delete brfile_;
          brfile_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_bz3(pathname))
    {
#ifdef HAVE_LIBBZIP3
      // try to read the compression format magic bytes
      if (fread(buf_, 1, 9, file) < 9 || strncmp(reinterpret_cast<char*>(buf_), "BZ3v1", 5) != 0)
      {
        cannot_decompress(pathname_, "an error was detected in the bzip3 compressed data");
        delete bz3file_;
        bz3file_ = NULL;
        file_ = NULL;
      }

      uint32_t block_size = u32(buf_ + 5);

      if (block_size < 65 * 1024 || block_size > 511 * 1024 * 1024)
      {
        cannot_decompress(pathname_, "an error was detected in the bzip3 compressed data");
        delete bz3file_;
        bz3file_ = NULL;
        file_ = NULL;
      }

      // open bzip3 compressed file
      try
      {
        bz3file_ = new BZ3(block_size);
        if (bz3file_->strm == NULL || bz3file_->buf == NULL)
        {
          warning("bz3_new failed", pathname);

          delete bz3file_;
          bz3file_ = NULL;
          file_ = NULL;
        }
      }

      catch (const std::bad_alloc&)
      {
        cannot_decompress(pathname_, "out of memory");
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_7z(pathname))
    {
#ifndef WITH_NO_7ZIP
      // open 7zip compressed file
      try
      {
        zipinfo_ = new ZipInfo(pathname, file);
        zipinfo_->sz_strm_ = new ZipInfo::sz_stream(pathname, file);
      }

      catch (const std::invalid_argument&)
      {
        /* non-seekable 7zip files cannot be decompressed,  */
        cannot_decompress("non-seekable 7zip archive", pathname);
        file_ = NULL;
      }

      catch (const std::bad_alloc&)
      {
        if (zipinfo_ != NULL)
        {
          delete zipinfo_;
          zipinfo_ = NULL;
        }
        errno = ENOMEM;
        warning("out of memory", pathname);
        file_ = NULL;
      }
#else
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
#endif
    }
    else if (is_rar(pathname))
    {
      // perhaps RAR can be supported sometime in the future?
      cannot_decompress("unsupported compression format", pathname);
      file_ = NULL;
    }
    else
    {
      // try to read two compression format magic bytes
      size_t num = fread(buf_, 1, 2, file);

      if (num == 2 && u16(buf_) == ZipInfo::DEFLATE_HEADER_MAGIC)
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
          zfile_->strm.avail_in = static_cast<uInt>(zfile_->zlen);

          // inflate gzip compressed data starting with a gzip header
          if (inflateInit2(&zfile_->strm, 16 + MAX_WBITS) != Z_OK)
          {
            cannot_decompress(pathname_, zfile_->strm.msg != NULL ? zfile_->strm.msg : "inflateInit2 failed");

            delete zfile_;
            zfile_ = NULL;
            file_ = NULL;
          }
        }

        catch (const std::bad_alloc&)
        {
          errno = ENOMEM;
          warning("out of memory", pathname);
          file_ = NULL;
        }
      }
      else if (num == 2 && u16(buf_) == ZipInfo::COMPRESS_HEADER_MAGIC)
      {
        // open compress (Z) compressed file
        if ((zzfile_ = z_open(file, "r", 0, 1)) == NULL)
        {
          warning("zopen failed", pathname);
          file_ = NULL;
        }
      }
      else
      {
        // read up to four bytes of the compression format's magic bytes to check for zip
        num += fread(buf_ + num, 1, 4 - num, file);

        if (num == 4 && u32(buf_) == ZipInfo::ZIP_HEADER_MAGIC)
        {
          // open zip compressed file
          try
          {
            zipinfo_ = new ZipInfo(pathname, file, buf_, 4);

            // read the zip header of the first compressed file, if none then end
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
            warning("out of memory", pathname);
            file_ = NULL;
          }
        }
        else if (num == 4 && u32(buf_) == ZipInfo::ZIP_EMPTY_MAGIC)
        {
          // skip empty zip file without warning
          file_ = NULL;
        }
        else if (num == 4 && u32(buf_) == ZipInfo::ZIP_DESCRIPTOR_MAGIC)
        {
          // cannot decompress split zip files
          cannot_decompress(pathname, "spanned zip fragment of a split zip archive");
          file_ = NULL;
        }
        else
        {
          // no compression: pass through
          num += fread(buf_ + num, 1, Z_BUF_LEN - num, file);
          len_ = static_cast<std::streamsize>(num);
        }
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
#endif
#ifdef HAVE_LIBLZ4
    else if (lz4file_ != NULL)
    {
      // close lz4 compressed file
      delete lz4file_;
      lz4file_ = NULL;
    }
#endif
#ifdef HAVE_LIBZSTD
    else if (zstdfile_ != NULL)
    {
      // close zstd compressed file
      delete zstdfile_;
      zstdfile_ = NULL;
    }
#endif
#ifdef HAVE_LIBBROTLI
    else if (brfile_ != NULL)
    {
      // close brotli compressed file
      delete brfile_;
      brfile_ = NULL;
    }
#endif
#ifdef HAVE_LIBBZIP3
    else if (bz3file_ != NULL)
    {
      delete bz3file_;
      bz3file_ = NULL;
    }
#endif
    else if (zipinfo_ != NULL)
    {
      // close zip compressed file
      delete zipinfo_;
      zipinfo_ = NULL;
    }
  }

  // return true if decompressing a file
  bool decompressing() const
  {
    return
      zfile_    != NULL ||
      zzfile_   != NULL ||
      bzfile_   != NULL ||
      xzfile_   != NULL ||
      lz4file_  != NULL ||
      zstdfile_ != NULL ||
      brfile_   != NULL ||
      bz3file_  != NULL ||
      zipinfo_  != NULL;
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

  // zlib decompression state data
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
      strm.msg       = NULL;
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
  
  // zlib file handle
  typedef struct Z *zFile;

  // compress (Z) file handle
  typedef void *zzFile;

#ifdef HAVE_LIBBZ2

  // bzip/bzip2 decompression state data
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

  // lzma and xz decompression state data
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

#ifdef HAVE_LIBLZ4

  // lz4 decompression state data
  struct LZ4 {

    static const size_t MAX_BLOCK_SIZE   = 4194304; // lz4 4MB max block size
    static const size_t RING_BUFFER_SIZE = LZ4_DECODER_RING_BUFFER_SIZE(MAX_BLOCK_SIZE);

    LZ4()
      :
        strm(LZ4_createStreamDecode()),
        buf(static_cast<unsigned char*>(malloc(RING_BUFFER_SIZE))),
        loc(0),
        len(0),
        crc(0),
        size(0),
        dict(0),
        zbuf(static_cast<unsigned char*>(malloc(LZ4_COMPRESSBOUND(MAX_BLOCK_SIZE) + Z_BUF_LEN + 8))),
        zflg(0),
        zloc(0),
        zlen(0),
        zcrc(0)
    { }

    ~LZ4()
    {
      if (zbuf != NULL)
        free(zbuf);
      if (buf != NULL)
        free(buf);
      if (strm != NULL)
        LZ4_freeStreamDecode(strm);
    }

    lz4_stream     strm; // lz4 decompression stream state
    unsigned char *buf;  // decompressed data buffer
    size_t         loc;
    size_t         len;
    uint32_t       crc;  // decompressed data xxHash-32 - optional, unused
    uint64_t       size; // decompressed size - optional, unused
    uint32_t       dict; // dict id - optional, unused
    unsigned char *zbuf; // compressed data buffer with extra room for 64K input buffer + b.size (4 bytes) + b.checksum (4 bytes)
    unsigned char  zflg; // frame header flags
    size_t         zloc;
    size_t         zlen;
    uint32_t       zcrc; // comoressed block xxHash-32 - optional, unused

  };
  
  // lz4 file handle
  typedef struct LZ4 *lz4File;

#else

  // unused lz4 file handle
  typedef void *lz4File;

#endif

#ifdef HAVE_LIBZSTD

  // zstd decompression state data
  struct ZSTD {

    ZSTD()
      :
        strm(ZSTD_createDStream()),
        zbuf(static_cast<unsigned char*>(malloc(ZSTD_DStreamInSize()))),
        zloc(0),
        zlen(0),
        zend(false)
    { }

    ~ZSTD()
    {
      if (zbuf != NULL)
        free(zbuf);
      if (strm != NULL)
        ZSTD_freeDStream(strm);
    }

    ZSTD_DStream  *strm; // zstd decompression stream state
    unsigned char *zbuf; // compressed data buffer
    size_t         zloc;
    size_t         zlen;
    bool           zend;

  };

  // zstd file handle
  typedef struct ZSTD *zstdFile;

#else

  // unused zstd file handle
  typedef void *zstdFile;

#endif

#ifdef HAVE_LIBBROTLI

  // brotli decompression state data
  struct BR {

    BR()
      :
        strm(BrotliDecoderCreateInstance(NULL, NULL, NULL)),
        next_in(NULL),
        avail_in(0),
        next_out(NULL),
        avail_out(0),
        zlen(0),
        zend(false)
    { }

    ~BR()
    {
      if (strm != NULL)
        BrotliDecoderDestroyInstance(strm);
    }

    BrotliDecoderState *strm; // brotli decompression stream state
    const uint8_t      *next_in;
    size_t              avail_in;
    uint8_t            *next_out;
    size_t              avail_out;
    unsigned char       zbuf[Z_BUF_LEN];
    size_t              zlen;
    bool                zend;

  };

  // brotli file handle
  typedef struct BR *brotliFile;

#else

  // unused brotli file handle
  typedef void *brotliFile;

#endif

#ifdef HAVE_LIBBZIP3

  // bzip3 decompression state data
  struct BZ3 {

    BZ3(uint32_t block_size)
      :
        strm(bz3_new(block_size)),
        max(bz3_bound(block_size)),
        buf(static_cast<uint8_t*>(malloc(max))),
        loc(0),
        len(0)
    { }

    ~BZ3()
    {
      if (buf != NULL)
        free(buf);
      if (strm != NULL)
        bz3_free(strm);
    }

    bz3_state *strm;
    uint32_t   max;
    uint8_t   *buf;
    uint32_t   loc;
    uint32_t   len;

  };
  
  // bzip3 file handle
  typedef struct BZ3 *bz3File;

#else

  // unused bzip3 file handle
  typedef void *bz3File;

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
          zfile_->strm.avail_out = static_cast<uInt>(len);

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
            num = -1;
          }
          else
          {
            if (feof(file_))
              zfile_->zend = true;

            zfile_->strm.next_in  = zfile_->zbuf;
            zfile_->strm.avail_in = static_cast<uInt>(zfile_->zlen);

            if (num == 0)
            {
              zfile_->strm.next_out  = buf;
              zfile_->strm.avail_out = static_cast<uInt>(len);
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

              if (num == 0 && ret == Z_OK)
                continue;
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
          bzfile_->strm.avail_out = static_cast<unsigned int>(len);

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
            num = -1;
          }
          else
          {
            if (feof(file_))
              bzfile_->zend = true;

            bzfile_->strm.next_in  = reinterpret_cast<char*>(bzfile_->zbuf);
            bzfile_->strm.avail_in = static_cast<unsigned int>(bzfile_->zlen);

            if (num == 0)
            {
              bzfile_->strm.next_out  = reinterpret_cast<char*>(buf);
              bzfile_->strm.avail_out = static_cast<unsigned int>(len);
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

              if (num == 0 && ret == BZ_OK)
                continue;
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
          num = -1;
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
#ifdef HAVE_LIBLZ4
    else if (lz4file_ != NULL)
    {
      if (lz4file_->strm == NULL || lz4file_->buf == NULL || lz4file_->zbuf == NULL)
      {
        // LZ4 instantiation failed to allocate the required structures
        cannot_decompress(pathname_, "out of memory");
        delete lz4file_;
        lz4file_ = NULL;
        file_ = NULL;
        num = -1;
      }
      else if (lz4file_->loc < lz4file_->len)
      {
        // copy decompressed data from lz4file_->buf[] into the given buf[]
        if (lz4file_->loc + len > lz4file_->len)
          len = lz4file_->len - lz4file_->loc;
        memcpy(buf, lz4file_->buf + lz4file_->loc, len);
        lz4file_->loc += len;
        num = static_cast<std::streamsize>(len);
      }
      else
      {
        // loop over frames
        while (true)
        {
          // get next frame if we do not have a valid frame
          if (lz4file_->zflg == 0)
          {
            // move incomplete frame header to the start of lz4file_->zbuf[]
            lz4file_->zlen -= lz4file_->zloc;
            if (lz4file_->zlen > 0)
              memmove(lz4file_->zbuf, lz4file_->zbuf + lz4file_->zloc, lz4file_->zlen);
            lz4file_->zloc = 0;

            // frame header is 7 to 19 bytes long
            if (lz4file_->zlen < 19)
              lz4file_->zlen += fread(lz4file_->zbuf + lz4file_->zlen, 1, Z_BUF_LEN, file_);

            // if not enough frame data, error if not at EOF, otherwise EOF is OK
            if (lz4file_->zlen < 7)
            {
              if (lz4file_->zlen > 0 || ferror(file_))
              {
                warning("cannot read", pathname_);
                num = -1;
              }
              break;
            }

            // check magic bytes and flags
            uint32_t magic = u32(lz4file_->zbuf);
            if (magic >= 0x184D2A50 && magic <= 0x184D2A5F)
            {
              // skippable frame with 4 byte frame size
              if (lz4file_->zlen < 8)
              {
                warning("cannot read", pathname_);
                num = -1;
                break;
              }

              // skip this skippable frame, then continue with next frame
              size_t size = u32(lz4file_->zbuf + 4);
              lz4file_->zloc = 8;
              if (lz4file_->zloc + size > lz4file_->zlen)
              {
                size -= lz4file_->zlen - lz4file_->zloc;
                lz4file_->zloc = lz4file_->zlen;

                while (size > 0)
                {
                  size_t ret = fread(lz4file_->zbuf, 1, size < Z_BUF_LEN ? size : Z_BUF_LEN, file_);
                  if (ret == 0)
                    break;
                  size -= ret;
                }

                if (size > 0)
                {
                  warning("cannot read", pathname_);
                  num = -1;
                  break;
                }

                lz4file_->zloc = lz4file_->zlen = 0;
              }
              else
              {
                lz4file_->zloc += size;
              }
              continue;
            }
            lz4file_->zflg = lz4file_->zbuf[4];
            if (magic != 0x184D2204 || (lz4file_->zflg & 0xc0) != 0x40)
            {
              cannot_decompress(pathname_, "an error was detected in the lz4 compressed data");
              num = -1;
              break;
            }

            // get decompressed content size and dict, position to the start of the block
            lz4file_->zloc = 7;
            if ((lz4file_->zbuf[4] & 0x08))
            {
              lz4file_->size = u64(lz4file_->zbuf + lz4file_->zloc);
              lz4file_->zloc += 8;
            }
            if ((lz4file_->zbuf[4] & 0x01))
            {
              lz4file_->dict = u32(lz4file_->zbuf + lz4file_->zloc);
              lz4file_->zloc += 4;
            }

            // error if header was too short
            if (lz4file_->zlen < lz4file_->zloc)
            {
              lz4file_->zloc = lz4file_->zlen;
              cannot_decompress(pathname_, "an error was detected in the lz4 compressed data");
              num = -1;
              break;
            }
          }

          // move incomplete block data to the start of lz4file_->zbuf
          lz4file_->zlen -= lz4file_->zloc;
          if (lz4file_->zlen > 0)
            memmove(lz4file_->zbuf, lz4file_->zbuf + lz4file_->zloc, lz4file_->zlen);
          lz4file_->zloc = 0;

          // need 4 bytes with block size
          if (lz4file_->zlen < 4)
            lz4file_->zlen += fread(lz4file_->zbuf + lz4file_->zlen, 1, Z_BUF_LEN, file_);
          if (lz4file_->zlen < 4)
          {
            warning("cannot read", pathname_);
            num = -1;
            break;
          }

          // get the block size
          uint32_t size = u32(lz4file_->zbuf);
          lz4file_->zloc = 4;

          if (size == 0)
          {
            // reached the end marker, get footer with crc when present
            if ((lz4file_->zflg & 0x04))
            {
              lz4file_->crc = u32(lz4file_->zbuf +lz4file_->zloc);
              lz4file_->zloc += 4;
            }

            // reset flags to start a new frame and continue to read the frame
            lz4file_->zflg = 0;
            continue;
          }

          // if MSB(size) is set then data is uncompressed
          bool compressed = !(size & 0x80000000);
          if (!compressed)
            size &= 0x7fffffff;

          // block cannot be larger than the documented max block size of 4MB
          if (size > LZ4::MAX_BLOCK_SIZE)
          {
            cannot_decompress(pathname_, "an error was detected in the lz4 compressed data");
            num = -1;
            break;
          }

          // read the rest of the block, may overshoot by up to Z_BUF_LEN bytes
          while (lz4file_->zloc + size > lz4file_->zlen)
          {
            size_t ret = fread(lz4file_->zbuf + lz4file_->zlen, 1, Z_BUF_LEN, file_);
            if (ret == 0)
              break;
            lz4file_->zlen += ret;
          }
          if (lz4file_->zloc + size > lz4file_->zlen)
          {
            warning("cannot read", pathname_);
            num = -1;
            break;
          }

          if (compressed)
          {
            // decompress lz4file_->zbuf[] block into lz4file_->buf[]
            if (lz4file_->loc >= LZ4::RING_BUFFER_SIZE - LZ4::MAX_BLOCK_SIZE)
              lz4file_->loc = 0;
            int ret = LZ4_decompress_safe_continue(lz4file_->strm, reinterpret_cast<char*>(lz4file_->zbuf + lz4file_->zloc), reinterpret_cast<char*>(lz4file_->buf + lz4file_->loc), size, LZ4::MAX_BLOCK_SIZE);
            if (ret <= 0)
            {
              cannot_decompress(pathname_, "an error was detected in the lz4 compressed data");
              num = -1;
              break;
            }

            lz4file_->len = lz4file_->loc + ret;
          }
          else
          {
            // copy uncompressed data into lz4file_->buf[]
            memcpy(lz4file_->buf + lz4file_->loc, lz4file_->zbuf + lz4file_->zloc, size);
            lz4file_->len = lz4file_->loc + size;
          }

          // move over processed data
          lz4file_->zloc += size;

          // copy data from lz4file_->buf[] into the given buf[]
          if (lz4file_->loc + len > lz4file_->len)
            len = lz4file_->len - lz4file_->loc;
          memcpy(buf, lz4file_->buf + lz4file_->loc, len);
          lz4file_->loc += len;
          num = static_cast<std::streamsize>(len);

          if ((lz4file_->zflg & 0x10))
          {
            // need 4 bytes with block checksum
            if (lz4file_->zloc + 4 > lz4file_->zlen)
              lz4file_->zlen += fread(lz4file_->zbuf + lz4file_->zlen, 1, Z_BUF_LEN, file_);
            if (lz4file_->zloc + 4 > lz4file_->zlen)
            {
              warning("cannot read", pathname_);
              num = -1;
              break;
            }

            // get block checksum
            lz4file_->zcrc = u32(lz4file_->zbuf + lz4file_->zloc);
            lz4file_->zloc += 4;
          }

          if (num > 0)
            break;
        }
      }
    }
#endif
#ifdef HAVE_LIBZSTD
    else if (zstdfile_ != NULL)
    {
      if (zstdfile_->strm == NULL || zstdfile_->zbuf == NULL)
      {
        // ZSTD instantiation failed to allocate the required structures
        cannot_decompress(pathname_, "out of memory");
        delete zstdfile_;
        zstdfile_ = NULL;
        file_ = NULL;
        num = -1;
      }
      else
      {
        ZSTD_outBuffer out = { buf, len, 0 };

        // decompress zstdfile_->zbuf[] into buf[]
        if (zstdfile_->zloc < zstdfile_->zlen)
        {
          ZSTD_inBuffer in = { zstdfile_->zbuf, zstdfile_->zlen, zstdfile_->zloc };
          size_t ret = ZSTD_decompressStream(zstdfile_->strm, &out, &in);

          if (ZSTD_isError(ret))
          {
            cannot_decompress(pathname_, "an error was detected in the zstd compressed data");
            num = -1;
          }
          else
          {
            zstdfile_->zloc = in.pos;
            num = static_cast<std::streamsize>(out.pos);
          }
        }

        // read compressed data into zstdfile_->zbuf[] and decompress zstdfile_->zbuf[] into the given buf[]
        if (num >= 0 && num < static_cast<std::streamsize>(len) && zstdfile_->zloc >= zstdfile_->zlen && !zstdfile_->zend)
        {
          zstdfile_->zloc = 0;
          zstdfile_->zlen = fread(zstdfile_->zbuf, 1, ZSTD_DStreamInSize(), file_);

          if (ferror(file_))
          {
            warning("cannot read", pathname_);
            zstdfile_->zend = true;
            num = -1;
          }
          else
          {
            if (feof(file_))
              zstdfile_->zend = true;
          }

          ZSTD_inBuffer in = { zstdfile_->zbuf, zstdfile_->zlen, zstdfile_->zloc };
          size_t ret = ZSTD_decompressStream(zstdfile_->strm, &out, &in);

          if (ZSTD_isError(ret))
          {
            cannot_decompress(pathname_, "an error was detected in the zstd compressed data");
            num = -1;
          }
          else
          {
            zstdfile_->zloc = in.pos;
            num = static_cast<std::streamsize>(out.pos);
          }
        }

        // decompressed the last block or there was an error?
        if (num <= 0)
        {
          delete zstdfile_;
          zstdfile_ = NULL;
          file_ = NULL;
        }
      }
    }
#endif
#ifdef HAVE_LIBBROTLI
    else if (brfile_ != NULL)
    {
      while (true)
      {
        BrotliDecoderResult ret = BROTLI_DECODER_RESULT_SUCCESS;

        // decompress non-empty brfile_->zbuf[] into the given buf[], strange we can't limit this to brfile_->avail_in > 0
        if (brfile_->avail_out == 0)
        {
          brfile_->next_out  = buf;
          brfile_->avail_out = len;

          ret = BrotliDecoderDecompressStream(brfile_->strm, &brfile_->avail_in, &brfile_->next_in, &brfile_->avail_out, &brfile_->next_out, NULL);

          if (ret == BROTLI_DECODER_RESULT_ERROR)
          {
            cannot_decompress(pathname_, "an error was detected in the brotli compressed data");
            num = -1;
          }
          else
          {
            num = len - brfile_->avail_out;
          }
        }

        // read compressed data into brfile_->zbuf[] and decompress brfile_->zbuf[] into the given buf[]
        if (ret != BROTLI_DECODER_RESULT_ERROR && brfile_->avail_in == 0 && num < static_cast<std::streamsize>(len) && !brfile_->zend)
        {
          brfile_->zlen = fread(brfile_->zbuf, 1, Z_BUF_LEN, file_);

          if (ferror(file_))
          {
            warning("cannot read", pathname_);
            brfile_->zend = true;
            num = -1;
          }
          else
          {
            if (feof(file_))
              brfile_->zend = true;

            brfile_->next_in  = brfile_->zbuf;
            brfile_->avail_in = brfile_->zlen;

            if (num == 0)
            {
              brfile_->next_out  = buf;
              brfile_->avail_out = len;
            }

            ret = BrotliDecoderDecompressStream(brfile_->strm, &brfile_->avail_in, &brfile_->next_in, &brfile_->avail_out, &brfile_->next_out, NULL);

            if (ret == BROTLI_DECODER_RESULT_ERROR)
            {
              cannot_decompress(pathname_, "an error was detected in the brotli compressed data");
              num = -1;
            }
            else
            {
              num = len - brfile_->avail_out;

              if (num == 0 && ret != BROTLI_DECODER_RESULT_ERROR)
                continue;
            }
          }
        }

        // decompressed the last block or there was an error?
        if (num <= 0)
        {
          delete brfile_;
          brfile_ = NULL;
          file_ = NULL;
        }

        break;
      }
    }
#endif
#ifdef HAVE_LIBBZIP3
    else if (bz3file_ != NULL)
    {
      while (bz3file_->loc >= bz3file_->len)
      {
        // read decompressed and compressed size
        if (fread(bz3file_->buf, 1, 8, file_) < 8)
        {
          if (ferror(file_))
          {
            warning("cannot read", pathname_);
            num = -1;
          }

          break;
        }

        // compressed ("new") size
        uint32_t block_size = u32(bz3file_->buf);

        // decompressed ("old") size
        bz3file_->len = u32(bz3file_->buf + 4);
        bz3file_->loc = 0;

        if (block_size > bz3file_->max ||
            bz3file_->len > bz3file_->max ||
            fread(bz3file_->buf, 1, block_size, file_) < block_size ||
            bz3_decode_block(bz3file_->strm, bz3file_->buf, block_size, bz3file_->len) < 0)
        {
          if (ferror(file_))
            warning("cannot read", pathname_);
          else
            cannot_decompress(pathname_, "an error was detected in the bzip3 compressed data");

          num = -1;

          break;
        }
      }

      // if all OK then copy the next part of the decompressed block into buf[]
      if (num != -1 && bz3file_->loc < bz3file_->len)
      {
        num = bz3file_->len - bz3file_->loc;
        if (num > static_cast<std::streamsize>(len))
          num = len;
        memcpy(buf, bz3file_->buf + bz3file_->loc, num);
        bz3file_->loc += num;
      }
      else if (num <= 0)
      {
        delete bz3file_;
        bz3file_ = NULL;
        file_ = NULL;
      }
    }
#endif
    else if (zipinfo_ != NULL)
    {
      // decompress a zip compressed block into the given buf[]
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
      // pass through, without decompression, i.e. the STORE method
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

  // convert 2 bytes in little endian format to 16 bit
  static uint16_t u16(const unsigned char *buf)
  {
    return buf[0] + (static_cast<uint16_t>(buf[1]) << 8);
  }

  // convert 4 bytes in little endian format to 32 bit
  static uint32_t u32(const unsigned char *buf)
  {
    return u16(buf) + (static_cast<uint32_t>(u16(buf + 2)) << 16);
  }

  // convert 8 bytes in little endian format to to 64 bit
  static uint64_t u64(const unsigned char *buf)
  {
    return u32(buf) + (static_cast<uint64_t>(u32(buf + 4)) << 32);
  }

  const char     *pathname_;       // the pathname of the compressed file
  FILE           *file_;           // the compressed file
  zFile           zfile_;          // zlib file handle
  zzFile          zzfile_;         // compress (Z) file handle
  bzFile          bzfile_;         // bzip/bzip2 file handle
  xzFile          xzfile_;         // xz/lzma file handle
  lz4File         lz4file_;        // lz4 file handle
  zstdFile        zstdfile_;       // zstd file handle
  brotliFile      brfile_;         // brotli file handle
  bz3File         bz3file_;        // bzip3 file handle
  ZipInfo        *zipinfo_;        // zip file and zip info handle
  unsigned char   buf_[Z_BUF_LEN]; // buffer with decompressed stream data
  std::streamsize cur_;            // current position in buffer to read the stream data, less or equal to len_
  std::streamsize len_;            // length of decompressed data in the buffer

};

#endif
