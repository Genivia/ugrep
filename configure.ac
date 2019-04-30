AC_INIT(ugrep,1.0)
AM_INIT_AUTOMAKE([foreign])
AM_CONFIG_HEADER(config.h)
AC_COPYRIGHT([Copyright (C) 2019-2019 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

AC_CHECK_LIB(reflex, REFLEX_DBGOUT_,
    [],
    [echo 'ugrep requires the RE/flex library: https://github.com/Genivia/RE-flex'],
    [])

AC_TYPE_SIZE_T

AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL

AC_CANONICAL_HOST
PLATFORM=${host}
AC_SUBST(PLATFORM)

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([noext], [mandatory])
AC_SUBST([AM_CXXFLAGS], ["-Wall -Wunused -Wextra"])

AC_CONFIG_FILES([Makefile src/Makefile])

AC_OUTPUT
