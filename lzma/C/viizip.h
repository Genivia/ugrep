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
@file      viizip.h
@brief     7zip archive decompressor based on lzma/C/Util/7z/7zMain.c
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2023-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef VIIZIP_H
#define VIIZIP_H

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if !defined(OS_WIN) && (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__))
# define OS_WIN
#endif

#ifdef OS_WIN
#include <windows.h>
#if !defined(_SSIZE_T_DEFINED) && !defined(__ssize_t_defined)
#define _SSIZE_T_DEFINED
#ifdef _NI_mswin64_
typedef __int64             ssize_t;
#else
typedef int                 ssize_t;
#endif
#endif
#endif

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7zip decompressor state */
struct viizip;

/* create new 7zip decompressor for the given 7zip file, returns decompressor state or NULL on failure */
struct viizip *viinew(FILE *file);

/* free viizip decompressor state */
void viifree(struct viizip *viizip);

/* get number of archived files and directories */
size_t viinum(struct viizip *viizip);

/* get archive part pathname and info, start decompressing, return 0 if success, 1 when end reached, -1 on error */
int viiget(struct viizip *viizip, char *name, size_t max, time_t *mtime, uint64_t *usize);

/* decompress up to len bytes into buf[], return number of bytes decompressed */
ssize_t viidec(struct viizip *viizip, unsigned char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
