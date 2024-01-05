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
@file      7zls.c
@brief     list 7zip archive contents to test and use the new viizip API
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2023-2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#include "C/viizip.h"

/* 7zip archive part pathname max length if limits.h doesn't define it */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int main(int argc, char **argv)
{
  struct viizip *viizip;
  FILE *file;

  if (argc < 2)
    exit(EXIT_FAILURE);

  file = fopen(argv[1], "rb");
  if (file == NULL)
  {
    perror("cannot open file");
    exit(EXIT_FAILURE);
  }

  viizip = viinew(file);
  if (viizip == NULL)
  {
    fprintf(stderr, "viinew() failed\n");
    exit(EXIT_FAILURE);
  }

  printf("%zu entries:\n", viinum(viizip));

  while (1)
  {
    unsigned char buf[65536];
    char name[PATH_MAX];
    size_t len;
    time_t mtime;
    uint64_t usize;
    char *ct = "??? ??? ?? ??:??\0?? ????";
    int res = viiget(viizip, name, PATH_MAX, &mtime, &usize);
    if (res < 0)
    {
      fprintf(stderr, "viiget() failed\n");
      exit(EXIT_FAILURE);
    }

    /* no more entries? */
    if (res != 0)
      break;

    len = strlen(name);
    if (len > 0 && name[len - 1] != '/')
      printf("%12llu", usize);
    else
      printf("%12s", "");
    ct = ctime(&mtime); /* note: not MT safe */
    if (ct != NULL)
      ct[19] = ct[24] = '\0';
    printf(" %s %s %s\n", ct + 20, ct + 4, name);

    /* uncomment this code to test incremental decompression into buf[] with viidec()
    {
      ssize_t num;
      while ((num = viidec(viizip, buf, sizeof(buf))) > 0)
        usize -= num;
      if (usize != 0)
        fprintf(stderr, "viidec() failed\n");
    }
    */
  }

  viifree(viizip);
  fclose(file);
  exit(EXIT_SUCCESS);
}
