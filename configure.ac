AC_INIT(ugrep,1.6)
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER(config.h)
AC_COPYRIGHT([Copyright (C) 2019-2020 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

# if CXXFLAGS is undefined, set it to our preferred default flags
: ${CXXFLAGS="-Wall -Wextra -Wunused -O2"}

AC_HEADER_DIRENT
AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE

AC_FUNC_MMAP

AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL

AC_CANONICAL_HOST

PLATFORM=${host}
AC_SUBST(PLATFORM)

# Mac OS X has slow memory mapped files so we're disabling mmap
case "$PLATFORM" in
  *-apple-*)    EXTRA_CFLAGS="-DWITH_NO_MMAP ${EXTRA_CFLAGS}"
                ;;
esac

AX_BOOST_REGEX

AX_CHECK_ZLIB([], [echo "zlib not found: option -z disabled"])

AX_CHECK_BZLIB([], [echo "libbz2 not found: option -z won't decompress .bz and .bz2 files"])

AX_CHECK_LZMALIB([], [echo "liblzma not found: option -z won't decompress .lzma and .xz files"])

AC_ARG_WITH(grep-path,
  [AC_HELP_STRING([--with-grep-path=GREP_PATH],
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
  [AC_HELP_STRING([--with-grep-colors="GREP_COLORS"],
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

AC_ARG_ENABLE(color,
  [AC_HELP_STRING([--enable-color],
                  [enable colorized output by default without requiring ugrep flag --color])],
  [with_color="$enable_color"],
  [with_color="no"])
AC_MSG_CHECKING(for --enable-color)
if test "x$with_color" = "xyes"; then
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_COLOR ${EXTRA_CFLAGS}"
else
  AC_MSG_RESULT(no)
fi

AC_ARG_ENABLE(pager,
  [AC_HELP_STRING([--enable-pager],
                  [enable pager by default without requiring ugrep flag --pager])],
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
  [AC_HELP_STRING([--disable-hidden],
                  [disable searching hidden files and directories unless explicitly enabled with --hidden])],
  [with_no_hidden="yes"],
  [with_no_hidden="no"])
AC_MSG_CHECKING(for --disable-hidden)
if test "x$with_no_hidden" = "xno"; then
  AC_MSG_RESULT(no)
else
  AC_MSG_RESULT(yes)
  EXTRA_CFLAGS="-DWITH_NO_HIDDEN ${EXTRA_CFLAGS}"
fi

AC_ARG_ENABLE(mmap,
  [AC_HELP_STRING([--disable-mmap],
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

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])
AC_SUBST([AM_CXXFLAGS], ["-Wall -Wunused -Wextra"])

AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile])

AC_OUTPUT
