# SYNOPSIS
#
#   AX_CHECK_LZ4LIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed lz4 library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-lz4=DIR is specified,
#   it will try to find it in DIR/include/lz4.h and DIR/lib/liblz4.a. If
#   --without-lz4 is specified, the library is not searched at all.
#
#   If either the header file (lz4.h) or the library (liblz4) is not found,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid lz4
#   installation directory or --without-lz4.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${LZ4_HOME}/include' to CPFLAGS, appends
#   '-L${LZ4_HOME}/lib' to LDFLAGS, prepends '-llz4' to LIBS, and calls
#   AC_DEFINE(HAVE_LIBLZ4). You should use autoheader to include a definition
#   for this symbol in a config.h file. Sample usage in a C/C++ source is as
#   follows:
#
#     #ifdef HAVE_LIBLZ4
#     #include <lz4.h>
#     #endif /* HAVE_LIBLZ4 */
#
# LICENSE
#
#   Copyright (c) 2020 Robert van Engelen <engelen@acm.org>
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

AC_DEFUN([AX_CHECK_LZ4LIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if lz4 is wanted)
lz4_places="/usr/local /usr /opt/homebrew /opt/local /sw"
AC_ARG_WITH([lz4],
[  --with-lz4=DIR          root directory path of lz4 installation @<:@defaults to
                          /usr/local or /usr if not found in /usr/local@:>@
  --without-lz4           to disable lz4 usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    lz4_places="$withval $lz4_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  lz4_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate lz4, if wanted
#
if test -n "${lz4_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for LZ4_HOME in ${lz4_places} ; do
    if test -f "${LZ4_HOME}/include/lz4.h"; then break; fi
    LZ4_HOME=""
  done

  LZ4_OLD_LDFLAGS=$LDFLAGS
  LZ4_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${LZ4_HOME}"; then
    LDFLAGS="$LDFLAGS -L${LZ4_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${LZ4_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([lz4], [LZ4_createStreamDecode], [lz4_cv_liblz4=yes], [lz4_cv_liblz4=no])
  AC_CHECK_HEADER([lz4.h], [lz4_cv_lz4_h=yes], [lz4_cv_lz4_h=no])
  AC_LANG_POP([C])
  if test "$lz4_cv_liblz4" = "yes" && test "$lz4_cv_lz4_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${LZ4_HOME}/include"
                LDFLAGS="$LDFLAGS -L${LZ4_HOME}/lib"
                LIBS="-llz4 $LIBS"
                AC_DEFINE([HAVE_LIBLZ4], [1],
                          [Define to 1 if you have `lz4' library (-llz4)])
               ],[
                # Restore variables
                LDFLAGS="$LZ4_OLD_LDFLAGS"
                CPPFLAGS="$LZ4_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid lz4 installation with --with-lz4=DIR or disable lz4 usage with --without-lz4])
                ])
  fi
fi
])
