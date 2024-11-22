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
@file      zthread.hpp
@brief     file decompression threads
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2019,2024, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef ZTHREAD_HPP
#define ZTHREAD_HPP

#include "zstream.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#ifdef OS_WIN

// POSIX read() and write() return type is ssize_t
typedef int ssize_t;

// POSIX pipe() emulation
inline int pipe(int fd[2])
{
  HANDLE pipe_r = NULL;
  HANDLE pipe_w = NULL;
  if (CreatePipe(&pipe_r, &pipe_w, NULL, 0))
  {
    fd[0] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_r), _O_RDONLY);
    fd[1] = _open_osfhandle(reinterpret_cast<intptr_t>(pipe_w), _O_WRONLY);
    return 0;
  }
  errno = GetLastError();
  return -1;
}

// POSIX popen()
inline FILE *popen(const char *command, const char *mode)
{
  return _popen(command, mode);
}

// POSIX pclose()
inline int pclose(FILE *stream)
{
  return _pclose(stream);
}

#endif

// decompression thread state with shared objects
struct Zthread {

  Zthread(bool is_chained, std::string& partname) :
      ztchain(NULL),
      zstream(NULL),
      zpipe_in(NULL),
      is_chained(is_chained),
      quit(false),
      stop(false),
      is_extracting(false),
      is_waiting(false),
      is_assigned(false),
      is_compressed(false),
      partnameref(partname)
  {
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;
  }

  ~Zthread()
  {
    // recursively join all stages of the decompression thread chain (--zmax>1), delete zstream
    join();

    // delete the decompression chain (--zmax>1)
    if (ztchain != NULL)
    {
      delete ztchain;
      ztchain = NULL;
    }
  }

  // start decompression thread if not running, open new pipe, returns pipe or NULL on failure, this function is called by the main thread
  FILE *start(size_t ztstage, const char *pathname, FILE *file_in)
  {
    // return pipe
    FILE *pipe_in = NULL;

    // reset pipe descriptors, pipe is closed
    pipe_fd[0] = -1;
    pipe_fd[1] = -1;

    // partnameref is not assigned yet, used only when this decompression thread is chained
    is_assigned = false;

    is_compressed = false;

    // open pipe between the main thread or the previous decompression thread and this (new) decompression thread
    if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "rb")) != NULL)
    {
      // recursively add decompression stages to decompress multi-compressed files
      if (ztstage > 1)
      {
        // create a new decompression chain if not already created
        if (ztchain == NULL)
          ztchain = new Zthread(true, partname);

        // close the input pipe from the next decompression stage in the chain, if still open
        if (zpipe_in != NULL)
        {
          fclose(zpipe_in);
          zpipe_in = NULL;
        }

        // start the next stage in the decompression chain, return NULL if failed
        zpipe_in = ztchain->start(ztstage - 1, pathname, file_in);
        if (zpipe_in == NULL)
          return NULL;

        // wait for the partname to be assigned by the next decompression thread in the decompression chain
        std::unique_lock<std::mutex> lock(ztchain->pipe_mutex);
        if (!ztchain->is_assigned)
          ztchain->part_ready.wait(lock);
        lock.unlock();

        // create or open a zstreambuf to (re)start the decompression thread, reading from zpipe_in from the next stage in the chain
        if (zstream == NULL)
          zstream = new zstreambuf(partname.c_str(), zpipe_in);
        else
          zstream->open(partname.c_str(), zpipe_in);
      }
      else
      {
        // create or open a zstreambuf to (re)start the decompression thread, reading from the source input
        if (zstream == NULL)
          zstream = new zstreambuf(pathname, file_in);
        else
          zstream->open(pathname, file_in);
      }

      // are we decompressing in any of the stages?
      is_compressed = zstream->decompressing() || (ztchain != NULL && ztchain->decompressing());

      if (thread.joinable())
      {
        // wake decompression thread waiting in close_wait_zstream_open(), there is work to do
        pipe_zstrm.notify_one();
      }
      else
      {
        // start a new decompression thread
        try
        {
          // reset flags
          quit = false;
          stop = false;
          is_extracting = false;
          is_waiting = false;

          thread = std::thread(&Zthread::decompress, this);
        }

        catch (std::system_error&)
        {
          // thread creation failed
          fclose(pipe_in);
          close(pipe_fd[1]);
          pipe_fd[0] = -1;
          pipe_fd[1] = -1;

          warning("cannot create thread to decompress", pathname);

          return NULL;
        }
      }
    }
    else
    {
      // pipe failed
      if (pipe_fd[0] != -1)
      {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        pipe_fd[0] = -1;
        pipe_fd[1] = -1;
      }

      warning("cannot create pipe to decompress", pathname);

      return NULL;
    }

    return pipe_in;
  }

  // open pipe to the next file or part in the archive or return NULL, this function is called by the main thread or by the previous decompression thread
  FILE *open_next(const char *pathname)
  {
    if (pipe_fd[0] != -1)
    {
      // our end of the pipe was closed earlier, before open_next() was called
      pipe_fd[0] = -1;

      // if extracting and the decompression filter thread is not yet waiting, then wait until decompression thread closed its end of the pipe
      std::unique_lock<std::mutex> lock(pipe_mutex);
      if (!is_waiting)
        pipe_close.wait(lock);
      lock.unlock();

      // partnameref is not assigned yet, used only when this decompression thread is chained
      is_assigned = false;

      // extract the next file from the archive when applicable, e.g. zip format
      if (is_extracting)
      {
        FILE *pipe_in = NULL;

        // open pipe between worker and decompression thread, then start decompression thread
        if (pipe(pipe_fd) == 0 && (pipe_in = fdopen(pipe_fd[0], "rb")) != NULL)
        {
          // if chained before another decompression thread
          if (is_chained)
          {
            // use lock and wait for partname ready
            std::unique_lock<std::mutex> lock(pipe_mutex);
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();
            // wait for the partname to be set by the next decompression thread in the ztchain
            if (!is_assigned)
              part_ready.wait(lock);
            lock.unlock();
          }
          else
          {
            // notify the decompression filter thread of the new pipe
            pipe_ready.notify_one();
          }

          return pipe_in;
        }

        // failed to create a new pipe
        warning("cannot create pipe to decompress", is_chained ? NULL : pathname);

        if (pipe_fd[0] != -1)
        {
          close(pipe_fd[0]);
          close(pipe_fd[1]);
        }

        // reset pipe descriptors, pipe was closed
        pipe_fd[0] = -1;
        pipe_fd[1] = -1;

        // notify the decompression thread filter_tar/filter_cpio of the closed pipe
        pipe_ready.notify_one();

        // when an error occurred, we still need to notify the receiver in case it is waiting on the partname
        std::unique_lock<std::mutex> lock(pipe_mutex);
        is_assigned = true;
        part_ready.notify_one();
        lock.unlock();
      }
    }

    return NULL;
  }

  // cancel decompression gracefully
  void cancel()
  {
    stop = true;

    // recursively cancel decompression threads in the chain
    if (ztchain != NULL)
      ztchain->cancel();
  }

  // join this thread, this function is called by the main thread
  void join()
  {
    // recursively join all stages of the decompression thread chain
    if (ztchain != NULL)
      ztchain->join();

    if (thread.joinable())
    {
      std::unique_lock<std::mutex> lock(pipe_mutex);

      // decompression thread should quit to join
      quit = true;

      if (!is_waiting)
      {
        // wait until decompression thread closes the pipe
        pipe_close.wait(lock);
      }
      else
      {
        // wake decompression thread waiting in close_wait_zstream_open(), there is no more work to do
        pipe_zstrm.notify_one();
      }

      lock.unlock();

      // now wait for the decomprssion thread to join
      thread.join();
    }

    // release the zstream that is no longer needed
    if (zstream != NULL)
    {
      delete zstream;
      zstream = NULL;
    }
  }

  // if the pipe was closed, then wait until the main thread opens a new pipe to search the next part in an archive
  bool wait_pipe_ready()
  {
    if (pipe_fd[1] == -1)
    {
      // signal close and wait until a new zstream pipe is ready
      std::unique_lock<std::mutex> lock(pipe_mutex);
      pipe_close.notify_one();
      is_waiting = true;
      pipe_ready.wait(lock);
      is_waiting = false;
      lock.unlock();

      // the receiver did not create a new pipe in close_file()
      if (pipe_fd[1] == -1)
        return false;
    }

    return true;
  }

  // close the pipe and wait until the main thread opens a new zstream and pipe for the next decompression job, unless quitting
  void close_wait_zstream_open()
  {
    if (pipe_fd[1] != -1)
    {
      // close our end of the pipe
      close(pipe_fd[1]);
      pipe_fd[1] = -1;
    }

    // signal close and wait until zstream is open
    std::unique_lock<std::mutex> lock(pipe_mutex);
    pipe_close.notify_one();
    if (!quit)
    {
      is_waiting = true;
      pipe_zstrm.wait(lock);
      is_waiting = false;
    }
    lock.unlock();
  }

  // decompression thread execution
  void decompress()
  {
    while (!quit)
    {
      // use the zstreambuf internal buffer to hold decompressed data
      unsigned char *buf;
      size_t maxlen;
      zstream->get_buffer(buf, maxlen);

      // reset flags
      is_extracting = false;
      is_waiting = false;

      // extract the parts of a zip file, one by one, if zip file detected
      while (!stop)
      {
        // to hold the path (prefix + name) extracted from a zip file
        std::string path;

        // a regular file, may be reset when unzipping a directory
        bool is_regular = true;

        const zstreambuf::ZipInfo *zipinfo = zstream->zipinfo();

        if (zipinfo != NULL)
        {
          // extracting a zip file
          is_extracting = true;

          if (!zipinfo->name.empty() && zipinfo->name.back() == '/')
          {
            // skip zip directories
            is_regular = false;
          }
          else
          {
            // save the zip path (prefix + name), since zipinfo will become invalid
            path.assign(zipinfo->name);
          }
        }

        bool is_selected = false;

        // decompress a block of data into the buffer
        std::streamsize len = zstream->decompress(buf, maxlen);

        if (len >= 0)
        {
          is_selected = true;

          if (!filter_tar(path, buf, maxlen, len, is_selected) &&
              !filter_cpio(path, buf, maxlen, len, is_selected))
          {
            // not a tar/cpio file, decompress the data into pipe
            is_selected = is_regular;

            if (is_selected)
            {
              // if pipe is closed, then wait until receiver reopens it, break if failed
              if (!wait_pipe_ready())
              {
                // close the input pipe from the next decompression chain stage
                if (ztchain != NULL && zpipe_in != NULL)
                {
                  fclose(zpipe_in);
                  zpipe_in = NULL;
                }
                break;
              }

              // assign the partname (synchronized on pipe_mutex and pipe), before sending to the new pipe
              if (ztchain == NULL)
                partnameref.assign(std::move(path));
              else if (path.empty())
                partnameref.assign(partname);
              else
                partnameref.assign(partname).append(":").append(std::move(path));

              // if chained before another decompression thread, then notify the receiver of the new partname
              if (is_chained)
              {
                std::unique_lock<std::mutex> lock(pipe_mutex);
                is_assigned = true;
                part_ready.notify_one();
                lock.unlock();
              }
            }

            // push decompressed data into pipe
            bool drain = false;
            while (len > 0 && !stop)
            {
              // write buffer data to the pipe, if the pipe is broken then the receiver is waiting for this thread to join so we drain the rest of the decompressed data
              if (is_selected && !drain && write(pipe_fd[1], buf, static_cast<size_t>(len)) < len)
              {
                // if no next decompression thread and decompressing a single file (not zip), then stop immediately
                if (ztchain == NULL && zipinfo == NULL)
                  break;

                drain = true;
              }

              // decompress the next block of data into the buffer
              len = zstream->decompress(buf, maxlen);
            }
          }
        }

        // break if not unzipping or if no more files to unzip
        if (zstream->zipinfo() == NULL)
        {
          // no decompression chain
          if (ztchain == NULL)
            break;

          // close the input pipe from the next decompression chain stage
          if (zpipe_in != NULL)
          {
            fclose(zpipe_in);
            zpipe_in = NULL;
          }

          // open pipe to the next file in an archive if there is a next file to extract
          zpipe_in = ztchain->open_next(partname.c_str());
          if (zpipe_in == NULL)
            break;

          // open a zstreambuf to (re)start the decompression thread
          zstream->open(partname.c_str(), zpipe_in);
        }

        // extracting a file
        is_extracting = true;

        // after extracting files from an archive, close our end of the pipe and loop for the next file
        if (is_selected && pipe_fd[1] != -1)
        {
          close(pipe_fd[1]);
          pipe_fd[1] = -1;
        }
      }

      is_extracting = false;

      // when an error occurred or nothing was selected, then we still need to notify the receiver in case it is waiting on the partname
      if (is_chained)
      {
        std::unique_lock<std::mutex> lock(pipe_mutex);
        is_assigned = true;
        part_ready.notify_one();
        lock.unlock();
      }

      // close the pipe and wait until zstream pipe is open, unless quitting
      close_wait_zstream_open();
    }
  }

  // return true if decompressing a file in any of the decompression chain stages
  bool decompressing() const
  {
    return is_compressed;
  }

  // if tar/pax file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_tar(const std::string& archive, unsigned char *buf, size_t maxlen, std::streamsize len, bool& is_selected)
  {
    const int BLOCKSIZE = 512;

    if (len > BLOCKSIZE)
    {
      // v7 and ustar formats
      const char ustar_magic[8] = { 'u', 's', 't', 'a', 'r', 0, '0', '0' };
      bool is_ustar = *buf != '\0' && memcmp(buf + 257, ustar_magic, 8) == 0;

      // gnu and oldgnu formats
      const char gnutar_magic[8] = { 'u', 's', 't', 'a', 'r', ' ', ' ', 0 };
      bool is_gnutar = *buf != '\0' && memcmp(buf + 257, gnutar_magic, 8) == 0;

      // is this a tar/pax archive?
      if (is_ustar || is_gnutar)
      {
        // inform the main grep thread we are extracting an archive
        is_extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // to hold long path extracted from the previous header block that is marked with typeflag 'x' or 'L'
        std::string long_path;

        while (!stop)
        {
          // tar header fields name, size and prefix and make them \0-terminated by overwriting fields we do not use
          buf[100] = '\0';
          const char *name = reinterpret_cast<const char*>(buf);

          // path prefix is up to 155 bytes (ustar) or up to 131 bytes (gnutar)
          buf[345 + (is_ustar ? 155 : 131)] = '\0';
          const char *prefix = reinterpret_cast<const char*>(buf + 345);

          // check gnutar size with leading byte 0x80 (unsigned positive) or leading byte 0xff (negative)
          uint64_t size = 0;
          if (buf[124] == 0x80)
          {
            // 11 byte big-endian size field without the leading 0x80
            for (short i = 125; i < 136; ++i)
              size = (size << 8) + buf[i];
          }
          else if (buf[124] == 0xff)
          {
            // a negative size makes no sense, but let's not ignore it and cast to unsigned
            for (short i = 124; i < 136; ++i)
              size = (size << 8) + buf[i];
          }
          else
          {
            buf[136] = '\0';
            size = strtoull(reinterpret_cast<const char*>(buf + 124), NULL, 8);
          }

          // header types
          unsigned char typeflag = buf[156];
          bool is_regular = typeflag == '0' || typeflag == '\0';
          bool is_xhd = typeflag == 'x';
          bool is_extended = typeflag == 'L';

          // padding size
          int padding = (BLOCKSIZE - size % BLOCKSIZE) % BLOCKSIZE;

          // assign the (long) tar pathname, name and prefix are now \0-terminated
          path.clear();
          if (long_path.empty())
          {
            if (*prefix != '\0')
            {
              path.assign(prefix);
              path.push_back('/');
            }
            path.append(name);
          }
          else
          {
            path.assign(std::move(long_path));
          }

          // remove header to advance to the body
          len -= BLOCKSIZE;
          memmove(buf, buf + BLOCKSIZE, static_cast<size_t>(len));

          // check if archived file meets selection criteria
          size_t minlen = static_cast<size_t>(std::min(static_cast<uint64_t>(len), size)); // size_t is OK: len is streamsize but non-negative
          is_selected = is_regular;

          // if extended headers are present
          if (is_xhd)
          {
            // typeflag 'x': extract the long path from the pax extended header block in the body
            const char *body = reinterpret_cast<const char*>(buf);
            const char *end = body + minlen;
            const char *key = "path=";
            const char *str = std::search(body, end, key, key + 5);
            if (str != NULL)
            {
              end = static_cast<const char*>(memchr(str, '\n', end - str));
              if (end != NULL)
                long_path.assign(str + 5, end - str - 5);
            }
          }
          else if (is_extended)
          {
            // typeflag 'L': get long name from the body
            const char *body = reinterpret_cast<const char*>(buf);
            long_path.assign(body, strnlen(body, minlen));
          }

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
              break;

            // assign the partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
            if (ztchain != NULL)
            {
              if (!archive.empty())
                partnameref.assign(partname).append(":").append(archive).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!archive.empty())
                partnameref.assign(archive).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // if chained before another decompression thread, then notify the receiver of the new partname after wait_pipe_ready()
            if (is_chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              is_assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          while (len > 0 && !stop)
          {
            size_t len_out = static_cast<size_t>(std::min(static_cast<uint64_t>(len), size)); // size_t is OK: len is streamsize but non-negative

            if (ok)
            {
              // write decompressed data to the pipe, if the pipe is broken then stop pushing more data into this pipe
              if (write(pipe_fd[1], buf, len_out) < static_cast<ssize_t>(len_out))
                ok = false;
            }

            size -= len_out;

            // reached the end of the tar body?
            if (size == 0)
            {
              len -= len_out;
              memmove(buf, buf + len_out, static_cast<size_t>(len)); // size_t is OK: len is streamsize but non-negative

              break;
            }

            // decompress the next block of data into the buffer
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
            break;

          // fill the rest of the buffer with decompressed data
          while (len < BLOCKSIZE || static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error or EOF?
            if (len_in <= 0)
              break;

            len += len_in;
          }

          // skip padding
          if (len > padding)
          {
            len -= padding;
            memmove(buf, buf + padding, static_cast<size_t>(len));
          }

          // rest of the file is too short, something is wrong
          if (len <= BLOCKSIZE)
            break;

          // no more parts to extract?
          if (*buf == '\0' || (memcmp(buf + 257, ustar_magic, 8) != 0 && memcmp(buf + 257, gnutar_magic, 8) != 0))
            break;

          // get a new pipe to search the next part in the archive, if the previous part was a regular file
          if (is_selected)
          {
            // close our end of the pipe
            close(pipe_fd[1]);
            pipe_fd[1] = -1;

            is_selected = false;
          }
        }

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (is_chained)
        {
          std::unique_lock<std::mutex> lock(pipe_mutex);
          is_assigned = true;
          part_ready.notify_one();
          lock.unlock();
        }

        // done extracting the tar file
        return true;
      }
    }

    // not a tar file
    return false;
  }

  // if cpio file, extract regular file contents and push into pipes one by one, return true when done
  bool filter_cpio(const std::string& archive, unsigned char *buf, size_t maxlen, std::streamsize len, bool& is_selected)
  {
    const int HEADERSIZE = 110;

    if (len > HEADERSIZE)
    {
      // cpio odc format
      const char odc_magic[6] = { '0', '7', '0', '7', '0', '7' };

      // cpio newc format
      const char newc_magic[6] = { '0', '7', '0', '7', '0', '1' };

      // cpio newc+crc format
      const char newc_crc_magic[6] = { '0', '7', '0', '7', '0', '2' };

      // is this a cpio archive?
      if (memcmp(buf, odc_magic, 6) == 0 || memcmp(buf, newc_magic, 6) == 0 || memcmp(buf, newc_crc_magic, 6) == 0)
      {
        // inform the main grep thread we are extracting an archive
        is_extracting = true;

        // to hold the path (prefix + name) extracted from the header
        std::string path;

        // need a new pipe, close current pipe first to create a new pipe
        bool in_progress = false;

        while (!stop)
        {
          // true if odc format, false if newc format
          bool is_odc = buf[5] == '7';

          // odc header length is 76, newc header length is 110
          int header_len = is_odc ? 76 : 110;

          char tmp[16];
          char *rest = tmp;

          // get the namesize
          size_t namesize;
          if (is_odc)
          {
            memcpy(tmp, buf + 59, 6);
            tmp[6] = '\0';
            namesize = strtoul(tmp, &rest, 8);
          }
          else
          {
            memcpy(tmp, buf + 94, 8);
            tmp[8] = '\0';
            namesize = strtoul(tmp, &rest, 16);
          }

          // if not a valid mode value, then something is wrong
          if (*rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // pathnames with trailing \0 cannot be empty or too large
          if (namesize <= 1 || namesize >= 65536)
            break;

          // get the filesize
          size_t filesize;
          if (is_odc)
          {
            memcpy(tmp, buf + 65, 11);
            tmp[11] = '\0';
            filesize = strtoul(tmp, &rest, 8);
          }
          else
          {
            memcpy(tmp, buf + 54, 8);
            tmp[8] = '\0';
            filesize = strtoul(tmp, &rest, 16);
          }

          // if not a valid mode value, then something is wrong
          if (*rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // true if this is a regular file when (mode & 0170000) == 0100000
          bool is_regular;
          if (is_odc)
          {
            memcpy(tmp, buf + 18, 6);
            tmp[6] = '\0';
            is_regular = (strtoul(tmp, &rest, 8) & 0170000) == 0100000;
          }
          else
          {
            memcpy(tmp, buf + 14, 8);
            tmp[8] = '\0';
            is_regular = (strtoul(tmp, &rest, 16) & 0170000) == 0100000;
          }

          // if not a valid mode value, then something is wrong
          if (*rest != '\0')
          {
            // data was read, stop reading more
            if (in_progress)
              break;

            // assume this is not a cpio file and return false
            return false;
          }

          // remove header to advance to the body
          len -= header_len;
          memmove(buf, buf + header_len, static_cast<size_t>(len));

          // assign the cpio pathname
          path.clear();

          size_t size = namesize;

          while (len > 0 && !stop)
          {
            size_t n = std::min(static_cast<size_t>(len), size);
            char *b = reinterpret_cast<char*>(buf);

            path.append(b, n);
            size -= n;

            if (size == 0)
            {
              // remove pathname to advance to the body
              len -= n;
              memmove(buf, buf + n, static_cast<size_t>(len));

              break;
            }

            // decompress the next block of data into the buffer
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
            break;

          // remove trailing \0
          if (path.back() == '\0')
            path.pop_back();

          // reached the end of the cpio archive?
          if (path == "TRAILER!!!")
            break;

          // fill the rest of the buffer with decompressed data
          if (static_cast<size_t>(len) < maxlen)
          {
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error?
            if (len_in < 0)
              break;

            len += len_in;
          }

          // skip newc format \0 padding after the pathname
          if (!is_odc && len > 3)
          {
            size_t n = 4 - (110 + namesize) % 4;
            len -= n;
            memmove(buf, buf + n, static_cast<size_t>(len));
          }

          // check if archived file meets selection criteria
          is_selected = is_regular;

          // if the pipe is closed, then get a new pipe to search the next part in the archive
          if (is_selected)
          {
            // if pipe is closed, then wait until receiver reopens it, break if failed
            if (!wait_pipe_ready())
              break;

            // assign the partname (synchronized on pipe_mutex and pipe), before sending to the (new) pipe
            if (ztchain != NULL)
            {
              if (!archive.empty())
                partnameref.assign(partname).append(":").append(archive).append(":").append(std::move(path));
              else
                partnameref.assign(partname).append(":").append(std::move(path));
            }
            else
            {
              if (!archive.empty())
                partnameref.assign(archive).append(":").append(std::move(path));
              else
                partnameref.assign(std::move(path));
            }

            // if chained before another decompression thread, then notify the receiver of the new partname after wait_pipe_ready()
            if (is_chained)
            {
              std::unique_lock<std::mutex> lock(pipe_mutex);
              is_assigned = true;
              part_ready.notify_one();
              lock.unlock();
            }
          }

          // it is ok to push the body into the pipe for the main thread to search
          bool ok = is_selected;

          size = filesize;

          while (len > 0 && !stop)
          {
            size_t len_out = std::min(static_cast<size_t>(len), size); // size_t is OK: len is streamsize but non-negative

            if (ok)
            {
              // write decompressed data to the pipe, if the pipe is broken then stop pushing more data into this pipe
              if (write(pipe_fd[1], buf, len_out) < static_cast<ssize_t>(len_out))
                ok = false;
            }

            size -= len_out;

            // reached the end of the cpio body?
            if (size == 0)
            {
              len -= len_out;
              memmove(buf, buf + len_out, static_cast<size_t>(len));

              break;
            }

            // decompress the next block of data into the buffer
            len = zstream->decompress(buf, maxlen);
          }

          // error?
          if (len < 0 || stop)
            break;

          if (static_cast<size_t>(len) < maxlen)
          {
            // fill the rest of the buffer with decompressed data
            std::streamsize len_in = zstream->decompress(buf + len, maxlen - static_cast<size_t>(len));

            // error?
            if (len_in < 0)
              break;

            len += len_in;
          }

          // skip newc format \0 padding
          if (!is_odc && len > 2)
          {
            size_t n = (4 - filesize % 4) % 4;
            len -= n;
            memmove(buf, buf + n, static_cast<size_t>(len));
          }

          // rest of the file is too short, something is wrong
          if (len <= HEADERSIZE)
            break;

          // quit if this is not valid cpio header magic
          if (memcmp(buf, odc_magic, 6) != 0 && memcmp(buf, newc_magic, 6) != 0 && memcmp(buf, newc_crc_magic, 6) != 0)
            break;

          // get a new pipe to search the next part in the archive, if the previous part was a regular file
          if (is_selected)
          {
            // close our end of the pipe
            close(pipe_fd[1]);
            pipe_fd[1] = -1;

            in_progress = true;
            is_selected = false;
          }
        }

        // if we're stopping we still need to notify the receiver in case it is waiting on the partname
        if (is_chained)
        {
          std::unique_lock<std::mutex> lock(pipe_mutex);
          is_assigned = true;
          part_ready.notify_one();
          lock.unlock();
        }

        // done extracting the cpio file
        return true;
      }
    }

    // not a cpio file
    return false;
  }

  Zthread                *ztchain;       // chain of decompression threads to decompress multi-compressed/archived files
  zstreambuf             *zstream;       // the decompressed stream buffer from compressed input
  FILE                   *zpipe_in;      // input pipe from the next ztchain stage, if any
  std::thread             thread;        // decompression thread handle
  bool                    is_chained;    // true if decompression thread is chained before another decompression thread
  std::atomic_bool        quit;          // true if decompression thread should terminate to exit the program
  std::atomic_bool        stop;          // true if decompression thread should stop (cancel search)
  volatile bool           is_extracting; // true if extracting files from TAR or ZIP archive (no concurrent r/w)
  volatile bool           is_waiting;    // true if decompression thread is waiting (no concurrent r/w)
  volatile bool           is_assigned;   // true when partnameref was assigned
  volatile bool           is_compressed; // true when decompressing in anyone of the decompression stages
  int                     pipe_fd[2];    // decompressed stream pipe
  std::mutex              pipe_mutex;    // mutex to extract files in thread
  std::condition_variable pipe_zstrm;    // cv to control new pipe creation
  std::condition_variable pipe_ready;    // cv to control new pipe creation
  std::condition_variable pipe_close;    // cv to control new pipe creation
  std::condition_variable part_ready;    // cv to control new partname creation to pass along decompression chains
  std::string             partname;      // name of the archive part extracted by the next decompressor in the ztchain
  std::string&            partnameref;   // reference to the partname of the main thread or previous decompressor

};

#endif
