# SYNOPSIS
#
#   AX_CHECK_BROTLILIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed brotli library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-brotli=DIR is specified,
#   it will try to find it in DIR/include/brotli/decode.h and
#   DIR/lib/libbrotlidec. If --without-brotli is specified, the library is
#   not searched at all.
#
#   If either the header file (brotli/decode.h) or the library (libbrotlidec)
#   is not found, shell commands 'action-if-not-found' is run. If
#   'action-if-not-found' is not specified, the configuration exits on error,
#   asking for a valid brotli library installation directory or
#   --without-brotli.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${BROTLI_HOME}/include' to CPFLAGS, appends
#   '-L${BROTLI_HOME}/lib' to LDFLAGS, prepends '-lbrotlidec' and
#   '-lbrotlienc' to LIBS, and calls AC_DEFINE(HAVE_LIBBROTLI). You should use
#   autoheader to include a definition for this symbol in a config.h file.
#   Sample usage in a C/C++ source is as follows:
#
#     #ifdef HAVE_LIBBROTLI
#     #include <brotli/decode.h>
#     #include <brotli/encode.h>
#     #endif /* HAVE_LIBBROTLI */
#
# LICENSE
#
#   Copyright (c) 2023 Robert van Engelen <engelen@acm.org>
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

AC_DEFUN([AX_CHECK_BROTLILIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if brotli is wanted)
brotli_places="/usr/local /usr /opt/homebrew /opt/local /sw"
AC_ARG_WITH([brotli],
[  --with-brotli=DIR       root directory path of brotli library installation
                          @<:@defaults to /usr/local or /usr if not found in
                          /usr/local@:>@
  --without-brotli        to disable brotli library usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    brotli_places="$withval $brotli_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  brotli_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate brotli, if wanted
#
if test -n "${brotli_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for BROTLI_HOME in ${brotli_places} ; do
    if test -f "${BROTLI_HOME}/include/brotli/decode.h"; then break; fi
    BROTLI_HOME=""
  done

  BROTLI_OLD_LDFLAGS=$LDFLAGS
  BROTLI_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${BROTLI_HOME}"; then
    LDFLAGS="$LDFLAGS -L${BROTLI_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${BROTLI_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([brotlidec], [BrotliDecoderCreateInstance], [brotli_cv_libbrotli=yes], [brotli_cv_libbrotli=no])
  AC_CHECK_HEADER([brotli/decode.h], [brotli_cv_brotli_h=yes], [brotli_cv_brotli_h=no])
  AC_LANG_POP([C])
  if test "$brotli_cv_libbrotli" = "yes" && test "$brotli_cv_brotli_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${BROTLI_HOME}/include"
                LDFLAGS="$LDFLAGS -L${BROTLI_HOME}/lib"
                LIBS="-lbrotlidec -lbrotlienc $LIBS"
                AC_DEFINE([HAVE_LIBBROTLI], [1],
                          [Define to 1 if you have `brotli' library (-lbrotlidec -lbrotlienc)])
               ],[
                # Restore variables
                LDFLAGS="$BROTLI_OLD_LDFLAGS"
                CPPFLAGS="$BROTLI_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid brotli library installation with --with-brotli=DIR or disable brotli usage with --without-brotli])
                ])
  fi
fi
])
