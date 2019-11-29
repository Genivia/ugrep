AC_INIT(ugrep,1.5)
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER(config.h)
AC_COPYRIGHT([Copyright (C) 2019-2019 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

AX_BOOST_REGEX

AX_CHECK_ZLIB([], [echo "libz not found: option -z disabled"])

AX_CHECK_BZLIB([], [echo "libbz2 not found: option -z won't decompress .bz and .bz2 files"])

AX_CHECK_LZMALIB([], [echo "liblzma not found: option -z won't decompress .lzma and .xz files"])

AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE

AC_TYPE_SIZE_T

AC_CHECK_FUNCS([funopen])

AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL

AC_CANONICAL_HOST

PLATFORM=${host}
AC_SUBST(PLATFORM)

GREP_PATH=${datadir}/ugrep/patterns
AC_SUBST(GREP_PATH)

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])
AC_SUBST([AM_CXXFLAGS], ["-Wall -Wunused -Wextra"])

AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile])

AC_OUTPUT
