#
# SYNOPSIS
#
#   AX_BOOST_REGEX
#
# DESCRIPTION
#
#   This macro sets:
#
#     HAVE_BOOST_REGEX
#
# LICENSE
#
#   Copyright (c) 2008 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2008 Michael Tindal
#   Copyright (c) 2021 Robert van Engelen <engelen@acm.org>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

AC_DEFUN([AX_BOOST_REGEX],
[
    AC_ARG_WITH([boost-regex],
    AS_HELP_STRING([--with-boost-regex@<:@=special-lib@:>@],
                   [use the Regex library from boost - it is possible to specify a path to include/boost and lib/libboost_regex-mt
                        e.g. --with-boost-regex=/opt/local ]),
        [
        if test "$withval" = "no"; then
            want_boost="no"
        elif test "$withval" = "yes"; then
            want_boost="yes"
            ax_boost_regex_lib="-lboost_regex-mt"
        else
            want_boost="yes"
            BOOST_REGEX_HOME="$withval"
            ax_boost_regex_lib="-lboost_regex-mt"
        fi
        ],
        [
        want_boost="yes"
        ax_boost_regex_lib="-lboost_regex-mt"
        ]
    )

    if test "x$want_boost" = "xyes"; then
        BOOST_REGEX_OLD_LDFLAGS=$LDFLAGS
        BOOST_REGEX_OLD_CPPFLAGS=$CPPFLAGS
        if test -n "${BOOST_REGEX_HOME}"; then
            LDFLAGS="$LDFLAGS -L${BOOST_REGEX_HOME}/lib"
            CPPFLAGS="$CPPFLAGS -I${BOOST_REGEX_HOME}/include"
        fi
        AC_REQUIRE([AC_PROG_CC])
        AC_CACHE_CHECK([whether the Boost::Regex library is available],
                       [ax_cv_boost_regex],
        [AC_LANG_PUSH([C++])
             AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[@%:@include <boost/regex.hpp>
                                                ]],
                                   [[boost::regex r(); return 0;]])],
                   [ax_cv_boost_regex=yes],
                   [ax_cv_boost_regex=no])
         AC_LANG_POP([C++])
        ])
        if test "x$ax_cv_boost_regex" = "xyes"; then
            AC_DEFINE(HAVE_BOOST_REGEX,1,[define if the Boost::Regex library is available])
            LIBS="$LIBS $ax_boost_regex_lib"
        else
            LDFLAGS=$BOOST_REGEX_OLD_LDFLAGS
            CPPFLAGS=$BOOST_REGEX_OLD_CPPFLAGS
        fi
    fi
])
