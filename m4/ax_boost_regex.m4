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
#   Copyright (c) 2019 Robert van Engelen
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

AC_DEFUN([AX_BOOST_REGEX],
[
    AC_ARG_WITH([boost-regex],
    AS_HELP_STRING([--with-boost-regex@<:@=special-lib@:>@],
                   [use the Regex library from boost - it is possible to specify a certain library for the linker
                        e.g. --with-boost-regex=boost_regex-gcc-mt-d-1_33_1 ]),
        [
        if test "$withval" = "no"; then
            want_boost="no"
        elif test "$withval" = "yes"; then
            want_boost="yes"
            ax_boost_regex_lib="-lboost_regex"
        else
            want_boost="yes"
            ax_boost_regex_lib="$withval"
        fi
        ],
        [
        want_boost="yes"
        ax_boost_regex_lib="-lboost_regex"
        ]
    )

    if test "x$want_boost" = "xyes"; then
        AC_REQUIRE([AC_PROG_CC])

        AC_CACHE_CHECK(whether the Boost::Regex library is available,
                       ax_cv_boost_regex,
        [AC_LANG_PUSH([C++])
             AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[@%:@include <boost/regex.hpp>
                                                ]],
                                   [[boost::regex r(); return 0;]])],
                   ax_cv_boost_regex=yes, ax_cv_boost_regex=no)
         AC_LANG_POP([C++])
        ])
        if test "x$ax_cv_boost_regex" = "xyes"; then
            AC_DEFINE(HAVE_BOOST_REGEX,1,[define if the Boost::Regex library is available])
            LIBS="$LIBS $ax_boost_regex_lib"
        fi
    fi
])
