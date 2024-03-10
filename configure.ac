AC_INIT([ugrep],[4.5])
AM_INIT_AUTOMAKE([foreign subdir-objects])
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
AC_PROG_SED

PKG_PROG_PKG_CONFIG

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

AC_ARG_ENABLE([pcre2],[
  --enable-pcre2          Implement Perl Compatible Regular Expressions using pcre2 or boost_regex.
                          @<:@defaults to enabled if pcre2/boost_regex is installed@:>@
  --disable-pcre2         Do not implement PCRE])

AS_IF([test "$enable_pcre2" != "no"],[
  PKG_CHECK_MODULES([PCRE2], [libpcre2-8],
    [AC_DEFINE([HAVE_PCRE2], [1], [Use (8-bit) Perl Compatible Regular Expression Library])],
    [
        # Check for libboost-regex library and header, adds
        AC_SEARCH_LIBS([regcompA], [boost_regex boost_regex-mt], [
          # Check for boost regex header
          AC_CHECK_HEADER([boost/regex.hpp], [
            BOOST_REGEX_FOUND=true
            AC_DEFINE([HAVE_BOOST_REGEX], [1], [Use Boost Regex library])
            AC_DEFINE([BOOST_REGEX_STANDALONE], [1], [Signal to boost lib that it's being without the rest of boost])
          ], [AS_IF([test -n "$enable_pcre2" ],
            AC_MSG_ERROR([PCRE2 not found and boost/regex.hpp not found]),
            AC_MSG_WARN([PCRE2 not found and boost/regex.hpp not found]))
          ])
        ], [AS_IF([test -n "$enable_pcre2" ],
          AC_MSG_ERROR([PCRE2 not found and libboost-regex not found]),
          AC_MSG_WARN([PCRE2 not found and libboost-regex not found]))
        ])]
  )
])

AC_ARG_ENABLE([zlib],[
  --enable-zlib           Use zlib to enable searching inside compressed files.
                          @<:@defaults to enabled if zlib is installed@:>@
  --disable-zlib          Do not use zlib])

AS_IF([test "$enable_zlib" != "no"],[
  PKG_CHECK_MODULES([ZLIB], [zlib],
    [AC_DEFINE([HAVE_LIBZ], [1], [Use libz compression library])
    have_zlib=yes
    ],
    # zlib is optional by default, but required if --enable-zlib is given
    [AS_IF([test -z "$enable_zlib" ],
      [AC_MSG_WARN([zlib not found: ugrep option -z is disabled])],
      [AC_MSG_ERROR([zlib not found. Is the devel package installed?])],
    )]
  )
])

if test "${have_zlib}" != "no" ; then

AC_ARG_ENABLE([bzip2],[
  --enable-bzip2          Use bzip2 to enable searching inside compressed files.
                          @<:@defaults to enabled if bzip2 is installed@:>@
  --disable-bzip2         Do not use bzip2])

AS_IF([test "$enable_bzip2" != "no"],[
  # Check for libbz2 library and header
  AC_SEARCH_LIBS([BZ2_bzDecompressInit], [bz2], [
    # Check bzip2 header
    AC_CHECK_HEADER([bzlib.h], [
      AC_DEFINE([HAVE_LIBBZ2], [1], [Use bzip2 library])
    ], [
      # bzip2 is optional by default, but required if --enable-bzip2 is given
      AS_IF([test -z "$enable_bzip2" ],
        [AC_MSG_WARN([libbz2 not found: ugrep option -z cannot search .bz and .bz2 files])],
        [AC_MSG_ERROR([libbz2 not found. Is the devel package installed?])]
      )
    ])
  ])
])

AC_ARG_ENABLE([lzma],[
  --enable-lzma           Enable searching inside LZMA and XZ compressed files.
                          @<:@defaults to enabled if liblz is installed@:>@
  --disable-lzma          Do not use liblz])

AS_IF([test "$enable_lzma" != "no"],[
  PKG_CHECK_MODULES([LZMA], [liblzma],
    [AC_DEFINE([HAVE_LIBLZMA], [1], [Use liblzma compression library])
    ],
    # liblzma is optional by default, but required if --enable-lzma is given
    [AS_IF([test -z "$enable_lzma" ],
      [AC_MSG_WARN([liblzma not found: ugrep option -z cannot search .lzma and .xz files])],
      [AC_MSG_ERROR([liblzma not found. Is the devel package installed?])],
    )]
  )
])

AC_ARG_ENABLE([lz4],[
  --enable-lz4            Use liblz4 to enable searching inside LZ4 compressed files.
                          @<:@defaults to enabled if liblz4 is installed@:>@
  --disable-lz4           Do not use liblz4])

AS_IF([test "$enable_lz4" != "no"],[
  PKG_CHECK_MODULES([LZ4], [liblz4],
    [AC_DEFINE([HAVE_LIBLZ4], [1], [Use liblz4 compression library])
    ],
    # liblz4 is optional by default, but required if --enable-lz4 is given
    [AS_IF([test -z "$enable_lz4" ],
      [AC_MSG_WARN([liblz4 not found: ugrep option -z cannot search .lz4 files])],
      [AC_MSG_ERROR([liblz4 not found. Is the devel package installed?])],
    )]
  )
])

AX_CHECK_ZSTDLIB([], [echo "optional libzstd not found: install if you want ugrep option -z to search .zst files"])

AX_CHECK_BROTLILIB([], [echo "optional brotli library not found: install if you want ugrep option -z to search .br files"])

AC_ARG_WITH(bzip3,
  [AS_HELP_STRING([--with-bzip3],
                  [to enable bzip3 library to decompress .bz3 files])],
  [with_bzip3_library="$withval"],
  [with_bzip3_library=""])
if test "x$with_bzip3_library" != "x"; then
  if test "x$with_bzip3" = "xyes"; then
    unset with_bzip3
  fi
  AX_CHECK_BZIP3LIB([], [echo "optional libbzip3 not found: install if you want ugrep option -z to search .bz3 files"])
fi

AC_ARG_ENABLE(7zip,
  [AS_HELP_STRING([--disable-7zip],
                  [to disable 7zip and no longer search .7z files (7z requires more memory and takes long to decompress)])],
  [],
  [enable_7zip="yes"])
fi
AM_CONDITIONAL([ENABLE_7ZIP],[test "$enable_7zip" != "no"])
AS_IF([test "$enable_7zip" != "no"],[
  AC_DEFINE(HAVE_7ZIP,1,[7zip decompression is enabled. This define is used in verify.sh only])
])

AC_ARG_WITH([bash-completion-dir],
  [AS_HELP_STRING([--with-bash-completion-dir[=PATH]],
                  [install the bash auto-completion script in this directory. @<:@default=yes@:>@])],
  [with_bash_completion_dir="$withval"],
  [with_bash_completion_dir="yes"])
if test "x$with_bash_completion_dir" = "xyes"; then
  # FIXME: This is wrong.
  # https://www.gnu.org/software/autoconf/manual/autoconf-2.69/html_node/Installation-Directory-Variables.html#Installation-Directory-Variables
  # See "corollary"
  BASH_COMPLETION_DIR='$(sysconfdir)/bash_completion.d'
else
  BASH_COMPLETION_DIR="$with_bash_completion_dir"
fi
AC_SUBST(BASH_COMPLETION_DIR)
AM_CONDITIONAL([ENABLE_BASH_COMPLETION],[test "x$with_bash_completion_dir" != "xno"])

AC_ARG_WITH([fish-completion-dir],
  [AS_HELP_STRING([--with-fish-completion-dir[=PATH]],
                  [install the fish auto-completion script in this directory. @<:@default=yes@:>@])],
  [with_fish_completion_dir="$withval"],
  [with_fish_completion_dir="yes"])
if test "x$with_fish_completion_dir" = "xyes"; then
  FISH_COMPLETION_DIR='$(sysconfdir)/fish/completions'
else
  FISH_COMPLETION_DIR="$with_fish_completion_dir"
fi
AC_SUBST(FISH_COMPLETION_DIR)
AM_CONDITIONAL([ENABLE_FISH_COMPLETION],[test "x$with_fish_completion_dir" != "xno"])

AC_ARG_WITH([zsh-completion-dir],
    [AS_HELP_STRING([--with-zsh-completion-dir[=PATH]],
                   [install the zsh auto-completion script in this directory. @<:@default=yes@:>@])],
    [with_zsh_completion_dir="$withval"],
    [with_zsh_completion_dir="yes"])
if test "x$with_zsh_completion_dir" = "xyes"; then
    ZSH_COMPLETION_DIR="$datadir/zsh/site-functions"
else
    ZSH_COMPLETION_DIR="$with_zsh_completion_dir"
fi
AC_SUBST(ZSH_COMPLETION_DIR)
AM_CONDITIONAL([ENABLE_ZSH_COMPLETION],[test "x$with_zsh_completion_dir" != "xno"])

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

AC_ARG_ENABLE(simd,
  [AS_HELP_STRING([--disable-simd],
                  [disable SIMD CPU extensions (SSE2, AVX, NEON, etc)])],
  [with_no_simd="yes"],
  [with_no_simd="no"])
AM_CONDITIONAL([ENABLE_SIMD],[test "x$with_no_simd" = "xno"])
if ! test "x$with_no_simd" = "xyes"; then
  # Cross-compiling must be set to allow compiler detection of -mavx2 and
  # -mavx512bw support without probing the build machine's CPU. The build
  # machine CPU need not be probed because the code base detects at runtime
  # whether the CPU supports AVX2 and/or AVX512-BW, and runs the appropriate
  # code path (or the native code path if neither AVX is available at
  # runtime
  # Per https://www.gnu.org/software/autoconf/manual/autoconf-2.68/html_node/Runtime.html
  #
  # Temporarily set cross_compiling mode to detect if the compiler supports
  # AVX2 / AVX512 but not check the compiling CPU
  save_cross_compiling=$cross_compiling
  cross_compiling=yes

  # Tell AX_EXT that the CPU supports it, because runtime will check.
  ax_cv_have_avx2_cpu_ext=yes
  ax_cv_have_avx512bw_cpu_ext=yes
  ax_cv_have_sse2_cpu_ext=yes

  # FIXME: Tell AX_EXT that the OS supports it. I don't think this is correct for
  # the general case, but it will work for all OS's I know of.
  # Eg. If compiling for an OS so locked down that does not allow AVX
  # instructions even if the CPU supports them.
  # FIXME: In order to make this correct, the runtime would have to catch the SIGILL
  # signal thrown when AVX instruction is blocked.
  # FIXME: See the test "checking for x86-AVX xgetbv 0x00000000" in configure script
  # for sample implementation
  ax_cv_have_avx2_ext=yes
  ax_cv_have_avx512bw_ext=yes
  ax_cv_have_sse2_ext=yes
  ax_cv_have_neon_ext=yes

  AX_EXT
  cross_compiling=$save_cross_compiling
fi
AM_CONDITIONAL([ENABLE_SSE2],[test "x$ax_cv_support_sse2_ext" = "xyes"])
AM_CONDITIONAL([ENABLE_AVX2],[test "x$ax_cv_support_avx2_ext" = "xyes"])
AM_CONDITIONAL([ENABLE_AVX512_BW],[test "x$ax_cv_support_avx512bw_ext" = "xyes"])
AM_CONDITIONAL([ENABLE_NEON],[test "x$ax_cv_support_neon_ext" = "xyes"])

AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

# Allow the manpage be accessed as either ug or ugrep
AC_CONFIG_FILES([man/ugrep.1])
AC_CONFIG_LINKS([man/ug.1:man/ugrep.1])

# Instantiate Completion files from respective ug.in templates
AC_CONFIG_FILES([
  completions/bash/ug
])

# Bash completions for the alternative command names
# are symlinks because the file contents would be identical
# The bash completion is hand-written (distributed source)
# fish/zsh alternates are generated in the src/Makefile, from
# the built ugrep binary's --help output
AC_CONFIG_LINKS([
  completions/bash/ugrep:completions/bash/ug
  completions/bash/ugrep+:completions/bash/ug
  completions/bash/ug+:completions/bash/ug
])

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  patterns/Makefile
])

AC_OUTPUT
