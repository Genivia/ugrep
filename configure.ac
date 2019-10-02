AC_INIT(ugrep,1.5)
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER(config.h)
AC_COPYRIGHT([Copyright (C) 2019-2019 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

AC_ARG_WITH([reflex],
    AS_HELP_STRING([--with-reflex=DIR],
      [reflex installation directory prefix]),
    [REFLEX=$withval],)

if test -n "$REFLEX"; then
  AC_MSG_CHECKING(for RE/flex directory $REFLEX)
  if test -d "$REFLEX"; then
    AC_MSG_RESULT(yes)
    REFLEX_CFLAGS="-I${REFLEX}/include"
    REFLEX_LIBS="${REFLEX}/lib/libreflex.a"
  else
    AC_MSG_RESULT(no)
    REFLEX=""
    REFLEX_CFLAGS=""
    REFLEX_LIBS=""
  fi
else
  REFLEX_CFLAGS=""
  REFLEX_LIBS=""
fi

AC_SUBST(REFLEX_CFLAGS)
AC_SUBST(REFLEX_LIBS)

AC_CHECK_LIB(reflex, REFLEX_DBGOUT_,
    [],
    [
     if test -z "$REFLEX"; then
       echo 'ugrep requires RE/flex 1.4.3 or greater: https://github.com/Genivia/RE-flex'
       echo 'alternatively, download RE/flex, build it, and run the following command:'
       echo '$ ./configure --with-reflex=path/reflex'
       echo 'where path/reflex is the path from the root where reflex is located and built'
     fi
    ],
    [])

AX_BOOST_REGEX

AX_CHECK_ZLIB(
    [],
    [echo 'zlib not found: option -z, --decompress disabled'
    ])

AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE

AC_TYPE_SIZE_T

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
AX_CXX_COMPILE_STDCXX_11([noext], [mandatory])
AC_SUBST([AM_CXXFLAGS], ["-Wall -Wunused -Wextra"])

AC_CONFIG_FILES([Makefile src/Makefile])

AC_OUTPUT
