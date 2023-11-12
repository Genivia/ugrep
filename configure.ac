AC_INIT([ugrep],[4.3])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])
AC_COPYRIGHT([Copyright (C) 2019-2023 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

# if CXXFLAGS is undefined, set it to our preferred default flags
: ${CXXFLAGS="-Wall -Wextra -Wunused -O2"}
: ${CFLAGS="-Wall -Wextra -Wunused -O2"}

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])

AC_HEADER_DIRENT
AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE

AC_FUNC_MMAP

AC_CHECK_HEADERS([sys/statvfs.h sys/time.h sys/param.h sys/cpuset.h sys/resource.h sched.h pthread.h])

AC_CHECK_FUNCS([statvfs sched_setaffinity cpuset_setaffinity pthread_setaffinity_np pthread_setschedprio setpriority])

AC_CHECK_MEMBERS([struct stat.st_atim, struct stat.st_mtim, struct stat.st_ctim])
AC_CHECK_MEMBERS([struct stat.st_atimespec, struct stat.st_mtimespec, struct stat.st_ctimespec])

AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_LN_S

AC_CANONICAL_HOST

PLATFORM=${host}
AC_SUBST(PLATFORM)

# F_RDAHEAD fcntl()
AC_MSG_CHECKING(for F_RDAHEAD fcntl)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <fcntl.h>
]], [[ int cmd = F_RDAHEAD; ]])],[
  AC_DEFINE(HAVE_F_RDAHEAD,1,[ Define if F_RDAHEAD fcntl() is supported])
  AC_MSG_RESULT(yes)
],[AC_MSG_RESULT(no)
])

# O_NOATIME open flag
AC_MSG_CHECKING(for O_NOATIME open flag)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <fcntl.h>
]], [[ int cmd = O_NOATIME; ]])],[
  AC_DEFINE(HAVE_O_NOATIME,1,[ Define if O_NOATIME open flag is supported])
  AC_MSG_RESULT(yes)
],[AC_MSG_RESULT(no)
])

AX_CHECK_PCRE2([8],
[],
[echo "checking for Boost.Regex because PCRE2 is not usable"]
)

if test "x$pcre2_cv_libpcre2" != "xyes" || test "x$pcre2_cv_pcre2_h" != "xyes"; then
  AX_BOOST_REGEX
fi

AX_CHECK_ZLIB([], [echo "zlib not found: ugrep option -z is not available"])

AX_CHECK_BZ2LIB([], [echo "libbz2 not found: ugrep option -z won't decompress .bz and .bz2 files"])

AX_CHECK_LZMALIB([], [echo "liblzma not found: ugrep option -z won't decompress .lzma and .xz files"])

AX_CHECK_LZ4LIB([], [echo "liblz4 not found: ugrep option -z won't decompress .lz4 files"])

AX_CHECK_ZSTDLIB([], [echo "libzstd not found: ugrep option -z won't decompress .zst files"])

AX_CHECK_BROTLILIB([], [echo "brotli library not found: ugrep option -z won't decompress .br files"])

AX_CHECK_BZIP3LIB([], [echo "libbzip3 not found: ugrep option -z won't decompress .bz3 files"])

AC_ARG_WITH(grep-path,
  [AS_HELP_STRING([--with-grep-path=GREP_PATH],
                  [specifies the GREP_PATH if different than the default DATAROOTDIR/ugrep/patterns])],
  [with_grep_path="$withval"],
  [with_grep_path=""])
AC_MSG_CHECKING(for --with-grep-path)
if test "x$with_grep_path" != "x"; then
  GREP_PATH="$with_grep_path"
  AC_MSG_RESULT("$with_grep_path")
else
  AC_MSG_RESULT()
  GREP_PATH="${datadir}/ugrep/patterns"
fi
AC_SUBST(GREP_PATH)

AC_ARG_WITH(grep-colors,
  [AS_HELP_STRING([--with-grep-colors="GREP_COLORS"],
                  [specifies the default ANSI SGR color parameters when variable GREP_COLORS is undefined])],
  [with_grep_colors="$withval"],
  [with_grep_colors=""])
AC_MSG_CHECKING(for --with-grep-colors)
if test "x$with_grep_colors" != "x"; then
  AC_MSG_RESULT("$with_grep_colors")
  EXTRA_CFLAGS="-DDEFAULT_GREP_COLORS=\"\\\"$with_grep_colors\\\"\" ${EXTRA_CFLAGS}"
else
  AC_MSG_RESULT()
fi

AC_ARG_ENABLE(auto-color,
  [AS_HELP_STRING([--disable-auto-color],
                  [disable automatic colors, otherwise colors are enabled by default])],
  [with_no_auto_color="yes"],
  [with_no_auto_color="no"])
AC_MSG_CHECKING(for --disable-auto-color)
if test "x$with_no_auto_color" = "xno"; then
  AC_MSG_RESULT(no)
  EXTRA_CFLAGS="-DWITH_COLOR ${EXTRA_CFLAGS}"
else
  AC_MSG_RESULT(yes)
fi

AC_ARG_ENABLE(color,
  [AS_HELP_STRING([--enable-color],
                  [deprecated, use --disable-auto-color])],
  [],
  [])
AC_MSG_CHECKING(for --enable-color)
if test "x$with_no_auto_color" = "xno"; then
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(pretty,
  [AS_HELP_STRING([--enable-pretty],
                  [enable pretty output by default without requiring ugrep flag --pretty])],
  [with_pretty="$enable_pretty"],
  [with_pretty="no"])
AC_MSG_CHECKING(for --enable-pretty)
if test "x$with_pretty" = "xyes"; then
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_PRETTY ${EXTRA_CFLAGS}"
else
  AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(pager,
  [AS_HELP_STRING([--enable-pager],
                  [enable the pager by default without requiring ugrep flag --pager])],
  [with_pager="$enable_pager"],
  [with_pager="no"])
AC_MSG_CHECKING(for --enable-pager)
if test "x$with_pager" = "xyes"; then
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_PAGER ${EXTRA_CFLAGS}"
else
  AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(hidden,
  [AS_HELP_STRING([--enable-hidden],
                  [enable searching hidden files and directories by default unless explicitly disabled with ugrep flag --no-hidden])],
  [with_hidden="$enable_hidden"],
  [with_hidden="no"])
AC_MSG_CHECKING(for --enable-hidden)
if test "x$with_hidden" = "xno"; then
  AC_MSG_RESULT(no)
else
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_HIDDEN ${EXTRA_CFLAGS}"
fi

AC_ARG_ENABLE(mmap,
  [AS_HELP_STRING([--disable-mmap],
                  [disable memory mapped files unless explicitly enabled with --mmap])],
  [with_no_mmap="yes"],
  [with_no_mmap="no"])
AC_MSG_CHECKING(for --disable-mmap)
if test "x$with_no_mmap" = "xno"; then
  AC_MSG_RESULT(no)
else
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_NO_MMAP ${EXTRA_CFLAGS}"
fi

AC_SUBST(EXTRA_CFLAGS)

AC_ARG_ENABLE(sse2,
  [AS_HELP_STRING([--disable-sse2],
                  [disable SSE2 and AVX optimizations])],
  [with_no_sse2="yes"],
  [with_no_sse2="no"])
AC_MSG_CHECKING(for --disable-sse2)
if test "x$with_no_sse2" = "xno"; then
  AC_MSG_RESULT(no)
  AC_MSG_CHECKING([whether ${CXX} supports SSE2 intrinsics])
  save_CXXFLAGS=$CXXFLAGS
  CXXFLAGS="-march=native"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <emmintrin.h>]], [[__m128i n = _mm_set1_epi8(42);]])],
                    [msse2_ok=yes],
                    [msse2_ok=no])
  CXXFLAGS=$save_CXXFLAGS
  AC_MSG_RESULT($msse2_ok)
  if test "x$msse2_ok" = "xyes"; then
    SIMD_FLAGS="-msse2 -DHAVE_SSE2"
    AC_ARG_ENABLE(avx,
      [AS_HELP_STRING([--disable-avx],
                      [disable AVX optimizations])],
      [with_no_avx="yes"],
      [with_no_avx="no"])
    AC_MSG_CHECKING(for --disable-avx)
    if test "x$with_no_avx" = "xno"; then
      AC_MSG_RESULT(no)
      AC_MSG_CHECKING([whether ${CXX} supports AVX intrinsics])
      save_CXXFLAGS=$CXXFLAGS
      CXXFLAGS="-march=native"
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <immintrin.h>]], [[__m512 n = _mm512_set1_epi8(42); (void)_mm512_cmpeq_epi8_mask(n, n);]])],
                        [mavx_ok=yes],
                        [mavx_ok=no])
      if test "x$mavx_ok" = "xyes"; then
        SIMD_FLAGS="-msse2 -DHAVE_AVX512BW"
        SIMD_AVX2_FLAGS="-mavx2"
        SIMD_AVX512BW_FLAGS="-mavx512bw"
      else
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <immintrin.h>]], [[__m256i n = _mm256_set1_epi8(42); (void)_mm256_movemask_epi8(_mm256_and_si256(n, n));]])],
                          [mavx_ok=yes],
                          [mavx_ok=no])
        if test "x$mavx_ok" = "xyes"; then
          SIMD_FLAGS="-msse2 -DHAVE_AVX2"
          SIMD_AVX2_FLAGS="-mavx2"
        fi
      fi
      CXXFLAGS=$save_CXXFLAGS
      AC_MSG_RESULT($mavx_ok)
    else
      AC_MSG_RESULT(yes)
    fi
  fi
else
  AC_MSG_RESULT(yes)
  SIMD_FLAGS=
fi

if test "x$SIMD_FLAGS" = "x"; then
AC_ARG_ENABLE(neon,
  [AS_HELP_STRING([--disable-neon],
                  [disable ARM NEON/AArch64 optimizations])],
  [with_no_neon="yes"],
  [with_no_neon="no"])
AC_MSG_CHECKING(for --disable-neon)
if test "x$with_no_neon" = "xno"; then
  AC_MSG_RESULT(no)
  AC_MSG_CHECKING([whether ${CXX} supports ARM NEON/AArch64 intrinsics])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <arm_neon.h>]], [[uint64x2_t n; uint64_t m = vgetq_lane_u64(n, 0);]])],
      [mneon_ok=yes],
      [mneon_ok=no])
  if test "x$mneon_ok" = "xyes"; then
    SIMD_FLAGS="-DHAVE_NEON"
  else
    save_CXXFLAGS=$CXXFLAGS
    CXXFLAGS="-march=native"
    AC_PREPROC_IFELSE([AC_LANG_PROGRAM([[#include <arm_neon.h>]], [[uint64x2_t n; uint64_t m = vgetq_lane_u64(n, 0);]])],
                      [mneon_ok=yes],
                      [mneon_ok=no])
    if test "x$mneon_ok" = "xyes"; then
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <arm_neon.h>]], [[uint64x2_t n; uint64_t m = vgetq_lane_u64(n, 0);]])],
                        [mneon_ok=yes],
                        [mneon_ok=no])
      if test "x$mneon_ok" = "xyes"; then
        SIMD_FLAGS="-march=native -DHAVE_NEON"
      else
        CXXFLAGS="-march=native -mfpu=neon"
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <arm_neon.h>]], [[uint64x2_t n; uint64_t m = vgetq_lane_u64(n, 0);]])],
                          [mneon_ok=yes],
                          [mneon_ok=no])
        if test "x$mneon_ok" = "xyes"; then
          SIMD_FLAGS="-march=native -mfpu=neon -DHAVE_NEON"
        fi
      fi
    fi
    CXXFLAGS=$save_CXXFLAGS
  fi
  AC_MSG_RESULT($mneon_ok)
else
  AC_MSG_RESULT(yes)
  SIMD_FLAGS=
fi
fi

AC_SUBST(SIMD_FLAGS)
AC_SUBST(SIMD_AVX2_FLAGS)
AC_SUBST(SIMD_AVX512BW_FLAGS)

AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile])

AC_OUTPUT
