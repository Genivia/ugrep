# SYNOPSIS
#
#   AX_CHECK_ZSTDLIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed zstd library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-zstd=DIR is specified,
#   it will try to find it in DIR/include/zstd.h and DIR/lib/libzstd. If
#   --without-zstd is specified, the library is not searched at all.
#
#   If either the header file (zstd.h) or the library (libzstd) is not found,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid zstd
#   installation directory or --without-zstd.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${ZSTD_HOME}/include' to CPFLAGS, appends
#   '-L${ZSTD_HOME}/lib' to LDFLAGS, prepends '-lzstd' to LIBS, and calls
#   AC_DEFINE(HAVE_LIBZSTD). You should use autoheader to include a definition
#   for this symbol in a config.h file. Sample usage in a C/C++ source is as
#   follows:
#
#     #ifdef HAVE_LIBZSTD
#     #include <zstd.h>
#     #endif /* HAVE_LIBZSTD */
#
# LICENSE
#
#   Copyright (c) 2021 Robert van Engelen <engelen@acm.org>
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

AC_DEFUN([AX_CHECK_ZSTDLIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if zstd is wanted)
zstd_places="/usr/local /usr /opt/local /sw"
AC_ARG_WITH([zstd],
[  --with-zstd=DIR         root directory path of zstd installation @<:@defaults to
                          /usr/local or /usr if not found in /usr/local@:>@
  --without-zstd          to disable zstd usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    zstd_places="$withval $zstd_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  zstd_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate zstd, if wanted
#
if test -n "${zstd_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for ZSTD_HOME in ${zstd_places} ; do
    if test -f "${ZSTD_HOME}/include/zstd.h"; then break; fi
    ZSTD_HOME=""
  done

  ZSTD_OLD_LDFLAGS=$LDFLAGS
  ZSTD_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${ZSTD_HOME}"; then
    LDFLAGS="$LDFLAGS -L${ZSTD_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${ZSTD_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([zstd], [ZSTD_decompressStream], [zstd_cv_libzstd=yes], [zstd_cv_libzstd=no])
  AC_CHECK_HEADER([zstd.h], [zstd_cv_zstd_h=yes], [zstd_cv_zstd_h=no])
  AC_LANG_POP([C])
  if test "$zstd_cv_libzstd" = "yes" && test "$zstd_cv_zstd_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${ZSTD_HOME}/include"
                LDFLAGS="$LDFLAGS -L${ZSTD_HOME}/lib"
                LIBS="-lzstd $LIBS"
                AC_DEFINE([HAVE_LIBZSTD], [1],
                          [Define to 1 if you have `zstd' library (-lzstd)])
               ],[
                # Restore variables
                LDFLAGS="$ZSTD_OLD_LDFLAGS"
                CPPFLAGS="$ZSTD_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid zstd installation with --with-zstd=DIR or disable zstd usage with --without-zstd])
                ])
  fi
fi
])
