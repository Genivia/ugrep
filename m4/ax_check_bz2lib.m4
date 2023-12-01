# SYNOPSIS
#
#   AX_CHECK_BZ2LIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed bzlib/bz2 library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-bzlib=DIR is specified,
#   it will try to find it in DIR/include/bzlib.h and DIR/lib/libbz2. If
#   --without-bzlib is specified, the library is not searched at all.
#
#   If either the header file (bzlib.h) or the library (libbz2) is not found,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid bzlib
#   installation directory or --without-bzlib.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${BZ2LIB_HOME}/include' to CPFLAGS, appends
#   '-L${BZ2LIB_HOME}/lib' to LDFLAGS, prepends '-lbz2' to LIBS, and calls
#   AC_DEFINE(HAVE_LIBBZ2). You should use autoheader to include a definition
#   for this symbol in a config.h file. Sample usage in a C/C++ source is as
#   follows:
#
#     #ifdef HAVE_LIBBZ2
#     #include <bzlib.h>
#     #endif /* HAVE_LIBBZ2 */
#
# LICENSE
#
#   Copyright (c) 2019 Robert van Engelen <engelen@acm.org>
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 1

AC_DEFUN([AX_CHECK_BZ2LIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if bzlib is wanted)
bzlib_places="/usr/local /usr /opt/homebrew /opt/local /sw"
AC_ARG_WITH([bzlib],
[  --with-bzlib=DIR        root directory path of bzlib installation @<:@defaults to
                          /usr/local or /usr if not found in /usr/local@:>@
  --without-bzlib         to disable bzlib usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    bzlib_places="$withval $bzlib_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  bzlib_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate bzlib, if wanted
#
if test -n "${bzlib_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for BZ2LIB_HOME in ${bzlib_places} ; do
    if test -f "${BZ2LIB_HOME}/include/bzlib.h"; then break; fi
    BZ2LIB_HOME=""
  done

  BZ2LIB_OLD_LDFLAGS=$LDFLAGS
  BZ2LIB_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${BZ2LIB_HOME}"; then
    LDFLAGS="$LDFLAGS -L${BZ2LIB_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${BZ2LIB_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([bz2], [BZ2_bzRead], [bzlib_cv_libbz2=yes], [bzlib_cv_libbz2=no])
  AC_CHECK_HEADER([bzlib.h], [bzlib_cv_bzlib_h=yes], [bzlib_cv_bzlib_h=no])
  AC_LANG_POP([C])
  if test "$bzlib_cv_libbz2" = "yes" && test "$bzlib_cv_bzlib_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${BZ2LIB_HOME}/include"
                LDFLAGS="$LDFLAGS -L${BZ2LIB_HOME}/lib"
                LIBS="-lbz2 $LIBS"
                AC_DEFINE([HAVE_LIBBZ2], [1],
                          [Define to 1 if you have `bz2' library (-lbz2)])
               ],[
                # Restore variables
                LDFLAGS="$BZ2LIB_OLD_LDFLAGS"
                CPPFLAGS="$BZ2LIB_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid bzlib installation with --with-bzlib=DIR or disable bzlib usage with --without-bzlib])
                ])
  fi
fi
])
