/******************************************************************************\
* Copyright (c) 2023, Robert van Engelen, Genivia Inc. All rights reserved.    *
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
@file      viizip.c
@brief     7zip archive decompressor based on lzma/C/Util/7z/7zMain.c
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2023-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "viizip.h"
#include <stdlib.h>
#include <string.h>

#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"

#ifdef OS_WIN
#include <io.h>
#endif

#define PERIOD_4   (4 * 365 + 1)
#define PERIOD_100 (PERIOD_4 * 25 - 1)
#define PERIOD_400 (PERIOD_100 * 4 + 1)

#ifdef __cplusplus
extern "C" {
#endif

enum viistate { DOGET, DOEXT, ISDIR };

/* 7zip decompressor state */
struct viizip {
  ISzAlloc      alloc_main;
  ISzAlloc      alloc_temp;
  CFileInStream stream;
  CLookToRead2  look;
  CSzArEx       db;
  UInt32        index;
  UInt32        block;
  Byte         *buf;
  size_t        buflen;
  UInt16       *tmp;
  size_t        tmplen;
  size_t        loc;
  size_t        len;
  enum viistate state;
};

/* convert UTF-16 string to UTF-8 string buffer utf8[] of max size */
static size_t utf16_to_utf8(const UInt16 *utf16, char *utf8, size_t max)
{
  char *ptr = utf8;
  UInt32 c;

  if (max == 0)
    return 0;

  while ((c = *utf16++) != 0 && --max != 0)
  {
    if (c >= 0xD800 && c < 0xDC00 && *utf16 >= 0xDC00)
      c = 0x010000 - 0xDC00 + ((c - 0xD800) << 10) + *utf16++;

    if (c < 0x80)
    {
      *ptr++ = (char)c;
    }
    else
    {
      if (c < 0x0800)
      {
        *ptr++ = (char)(0xC0 | ((c >> 6) & 0x1F));
      }
      else
      {
        if (c < 0x010000)
        {
          *ptr++ = (char)(0xE0 | ((c >> 12) & 0x0F));
        }
        else
        {
          *ptr++ = (char)(0xF0 | ((c >> 18) & 0x07));

          if (--max == 0)
            break;

          *ptr++ = (char)(0x80 | ((c >> 12) & 0x3F));
        }

        if (--max == 0)
          break;

        *ptr++ = (char)(0x80 | ((c >> 6) & 0x3F));
      }

      if (--max == 0)
        break;

      *ptr++ = (char)(0x80 | (c & 0x3F));
    }
  }

  *ptr = '\0';

  return ptr - utf8;
}

/* convert (low,high) 7zip time to time_t, based on 7zMain.c but using mktime() */
static time_t convert_time(const CNtfsFileTime *nTime)
{
  int year, mon;
  Byte ms[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  unsigned t;
  UInt32 v;
  UInt64 v64 = nTime->Low + ((UInt64)nTime->High << 32);
  struct tm tm;

  memset(&tm, 0, sizeof(struct tm));
  v64 /= 10000000;
  tm.tm_sec = (unsigned)(v64 % 60);
  v64 /= 60;
  tm.tm_min = (unsigned)(v64 % 60);
  v64 /= 60;
  tm.tm_hour = (unsigned)(v64 % 24);
  v64 /= 24;

  v = (UInt32)v64;

  year = (unsigned)(1601 + v / PERIOD_400 * 400);
  v %= PERIOD_400;

  t = v / PERIOD_100;
  if (t == 4)
    t = 3;
  year += t * 100;
  v -= t * PERIOD_100;

  t = v / PERIOD_4;
  if (t == 25)
    t = 24;
  year += t * 4;
  v -= t * PERIOD_4;

  t = v / 365;
  if (t == 4)
    t = 3;
  year += t;
  v -= t * 365;

  if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    ms[1] = 29;

  for (mon = 0;; ++mon)
  {
    unsigned d = ms[mon];
    if (v < d)
      break;
    v -= d;
  }

  tm.tm_year = year - 1900;
  tm.tm_mon = mon;
  tm.tm_mday = v + 1;
  tm.tm_isdst = -1;

  return mktime(&tm);
}

/* create new 7zip decompressor for the given 7zip file, returns decompressor state or NULL on failure */
struct viizip *viinew(FILE *file)
{
  static const size_t INPUTBUFSIZE = (size_t)1 << 18;
  static const ISzAlloc g_Alloc = { SzAlloc, SzFree };
  static volatile int init_done = 0;
  struct viizip *viizip = (struct viizip*)malloc(sizeof(struct viizip));
  if (viizip == NULL)
    return NULL;

  /* CrcGenerateTable() appears MT safe to compute g_CrcTable[], but we shouldn't init every time we get here */
  if (!init_done)
  {
    /* initialize global CRC table */
    CrcGenerateTable();
    init_done = 1;
  }

  viizip->alloc_main = g_Alloc;
  viizip->alloc_temp = g_Alloc;
  viizip->index = 0;
  viizip->block = 0xFFFFFFFF;
  viizip->buf = NULL;
  viizip->buflen = 0;
  viizip->tmp = NULL;
  viizip->tmplen = 0;
#ifdef OS_WIN
  viizip->stream.file.handle = (HANDLE)_get_osfhandle(_fileno(file));
#else
  viizip->stream.file.fd = fileno(file);
#endif
  SzArEx_Init(&viizip->db);
  FileInStream_CreateVTable(&viizip->stream);
  viizip->stream.wres = 0;
  LookToRead2_CreateVTable(&viizip->look, 0);
  viizip->look.buf = (Byte*)ISzAlloc_Alloc(&viizip->alloc_main, INPUTBUFSIZE);
  if (viizip->look.buf != NULL)
  {
    viizip->look.bufSize = INPUTBUFSIZE;
    viizip->look.realStream = &viizip->stream.vt;
    LookToRead2_INIT(&viizip->look);
    if (SzArEx_Open(&viizip->db, &viizip->look.vt, &viizip->alloc_main, &viizip->alloc_temp) == SZ_OK)
      return viizip;
  }

  /* failed to create decompressor */
  viifree(viizip);
  return NULL;
}

/* free viizip decompressor state */
void viifree(struct viizip *viizip)
{
  if (viizip == NULL)
    return;

  ISzAlloc_Free(&viizip->alloc_main, viizip->buf);
  SzFree(NULL, viizip->tmp);
  SzArEx_Free(&viizip->db, &viizip->alloc_main);
  free(viizip);
}

/* get number of archived files and directories */
size_t viinum(struct viizip *viizip)
{
  return viizip != NULL ? viizip->db.NumFiles : 0;
}

/* get archive part pathname and info, start decompressing, return 0 if success, 1 when end reached, -1 on error */
int viiget(struct viizip *viizip, char *name, size_t max, time_t *mtime, uint64_t *usize)
{
  SRes res = SZ_OK;

  if (viizip == NULL)
    return -1;

  /* if no more archive parts to decompress, return 1 */
  if (viizip->index >= viizip->db.NumFiles)
    return 1;

  /* check if directory */
  viizip->state = SzArEx_IsDir(&viizip->db, viizip->index) ? ISDIR : DOGET;

  /* populate name[] buffer up to max with the archive part name, make directories end with a / */
  if (name != NULL && max > 1)
  {
    size_t len = SzArEx_GetFileNameUtf16(&viizip->db, viizip->index, NULL);
    if (len > viizip->tmplen)
    {
      SzFree(NULL, viizip->tmp);
      viizip->tmplen = ((len + 0xFFUL) & ~0xFFUL); /* round up to multiple of 256 */
      viizip->tmp = (UInt16*)SzAlloc(NULL, viizip->tmplen * sizeof(UInt16));
      if (viizip->tmp == NULL)
        return -1;
    }
    SzArEx_GetFileNameUtf16(&viizip->db, viizip->index, viizip->tmp);
    len = utf16_to_utf8(viizip->tmp, name, max);

    if (viizip->state == ISDIR)
    {
      if (len + 1 >= max)
        len = max - 2;
      name[len] = '/';
      name[len + 1] = '\0';
    }
  }

  viizip->loc = 0;
  viizip->len = 0;

  /* populate modification/creation time */
  if (mtime != NULL)
  {
    if (SzBitWithVals_Check(&viizip->db.MTime, viizip->index))
    {
      const CNtfsFileTime *t = &viizip->db.MTime.Vals[viizip->index];
      *mtime = convert_time(t);
    }
    else if (SzBitWithVals_Check(&viizip->db.CTime, viizip->index))
    {
      const CNtfsFileTime *t = &viizip->db.CTime.Vals[viizip->index];
      *mtime = convert_time(t);
    }
    else
    {
      *mtime = 0;
    }
  }

  /* get uncompressed file size */
  if (usize != NULL)
    *usize = viizip->state == DOGET ? SzArEx_GetFileSize(&viizip->db, viizip->index) : 0;

  /* next archive part */
  ++viizip->index;

  return res == SZ_OK ? 0 : -1;
}

/* decompress up to len bytes into buf[], return number of bytes decompressed or -1 on error */
ssize_t viidec(struct viizip *viizip, unsigned char *buf, size_t len)
{
  /* if not initialized or not called viiget() then return error */
  if (viizip == NULL || viizip->index == 0)
    return -1;

  switch (viizip->state)
  {
    case ISDIR:
      return 0;

    case DOGET:
      /* 7zip decompresses the whole file in memory instead of allowing us to incrementally get blocks */
      /* i.e. huge memory overhead to extract large files, no pipelining advantage, no quick stop half way */
      if (SzArEx_Extract(
            &viizip->db,
            &viizip->look.vt,
            viizip->index - 1, /* viiget() increased index by one */
            &viizip->block,
            &viizip->buf,
            &viizip->buflen,
            &viizip->loc,
            &viizip->len,
            &viizip->alloc_main,
            &viizip->alloc_temp) != SZ_OK)
        return -1;
      viizip->state = DOEXT;
      break;

    case DOEXT:
      break;
  }

  if (viizip->len < len)
    len = viizip->len;

  memcpy(buf, viizip->buf + viizip->loc, len);
  viizip->loc += len;
  viizip->len -= len;

  return (ssize_t)len;
}

#ifdef __cplusplus
}
#endif
