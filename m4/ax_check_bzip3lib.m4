# SYNOPSIS
#
#   AX_CHECK_BZIP3LIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed bzip3 library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-bzip3=DIR is specified,
#   it will try to find it in DIR/include/libbz3.h and DIR/lib/libbzip3. If
#   --without-bzip3 is specified, the library is not searched at all.
#
#   If either the header file (libbz3.h) or the library (libbzip3) is not found,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid bzip3
#   installation directory or --without-bzip3.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${BZIP3_HOME}/include' to CPFLAGS, appends
#   '-L${BZIP3_HOME}/lib' to LDFLAGS, prepends '-lbzip3' to LIBS, and calls
#   AC_DEFINE(HAVE_LIBBZIP3). You should use autoheader to include a definition
#   for this symbol in a config.h file. Sample usage in a C/C++ source is as
#   follows:
#
#     #ifdef HAVE_LIBBZIP3
#     #include <libbz3.h>
#     #endif /* HAVE_LIBBZIP3 */
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

AC_DEFUN([AX_CHECK_BZIP3LIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if bzip3 is wanted)
bzip3_places="/usr/local /usr /opt/homebrew /opt/local /sw"
AC_ARG_WITH([bzip3],
[  --with-bzip3=DIR        root directory path of bzip3 library installation
                          @<:@defaults to /usr/local or /usr if not found in
                          /usr/local@:>@
  --without-bzip3         to disable bzip3 library usage completely],
[if test "$withval" != "no" ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    bzip3_places="$withval $bzip3_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  bzip3_places=""
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])
#
# Locate bzip3 library, if wanted
#
if test -n "${bzip3_places}"
then
  # check the user supplied or any other more or less 'standard' place:
  #   Most UNIX systems      : /usr/local and /usr
  #   MacPorts / Fink on OSX : /opt/local respectively /sw
  for BZIP3_HOME in ${bzip3_places} ; do
    if test -f "${BZIP3_HOME}/include/libbz3.h"; then break; fi
    BZIP3_HOME=""
  done

  BZIP3_OLD_LDFLAGS=$LDFLAGS
  BZIP3_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${BZIP3_HOME}"; then
    LDFLAGS="$LDFLAGS -L${BZIP3_HOME}/lib"
    CPPFLAGS="$CPPFLAGS -I${BZIP3_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([bzip3], [bz3_new], [bzip3_cv_libbzip3=yes], [bzip3_cv_libbzip3=no])
  AC_CHECK_HEADER([libbz3.h], [bzip3_cv_libbz3_h=yes], [bzip3_cv_libbz3_h=no])
  AC_LANG_POP([C])
  if test "$bzip3_cv_libbzip3" = "yes" && test "$bzip3_cv_libbz3_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${BZIP3_HOME}/include"
                LDFLAGS="$LDFLAGS -L${BZIP3_HOME}/lib"
                LIBS="-lbzip3 $LIBS"
                AC_DEFINE([HAVE_LIBBZIP3], [1],
                          [Define to 1 if you have `bzip3' library (-lbzip3)])
               ],[
                # Restore variables
                LDFLAGS="$BZLIB_OLD_LDFLAGS"
                CPPFLAGS="$BZLIB_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid bzip3 library installation with --with-bzip3=DIR or disable bzip3 library usage with --without-bzip3])
                ])
  fi
fi
])
