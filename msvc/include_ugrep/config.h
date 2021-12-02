#pragma once

// Note: Included via /FIconfig.h on the compiler command-line,
// Project properties --> Advanced --> Forced Include File.

// MSVC compatibility:
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS

// UGREP interface with external libraries:
#define LZMA_API_STATIC
#define ZLIB_WINAPI
#define PCRE2_STATIC

// UGREP options:
#define HAVE_AVX2
#define WITH_COLOR
#define WITH_NO_INDENT

// Options to enable/disable the use of external libraries.
// These are shown here for reference purposes only. They should be
// enabled in the project config, not here.
#if 0
#define HAVE_LIBBZ2
#define HAVE_LIBLZ4
#define HAVE_LIBLZMA
#define HAVE_LIBZ
#define HAVE_LIBZSTD
#define HAVE_BOOST_REGEX
#define HAVE_PCRE2
#endif
