This directory contains a subset of the LZMA SDK lzma2301/C files

LZMA SDK is available from https://7-zip.org/sdk.html

LZMA SDK is placed in the public domain.

LZMA SDK by Igor Pavlov.

Included in this directory is a new C API to simplify decompression:

- viizip.h   declarations, see below (hides implementation details)
- viizip.c   implementation
- libviiz.a  static library compiled from source

The 7zip decompressor state with hidden members:
struct viizip

To create a new 7zip decompressor for the given 7zip file:
struct viizip *viinew(FILE *file)

To free viizip decompressor state:
void viifree(struct viizip *viizip)

To get archive part pathname and info, start decompressing:
int viiget(struct viizip *viizip, char *name, size_t max, time_t *mtime, uint64_t *usize)

To decompress up to len bytes into buf[], return number of bytes decompressed:
ssize_t viidec(struct viizip *viizip, unsigned char *buf, size_t len)

The viizip files are part of the ugrep project licensed BSD-3.
