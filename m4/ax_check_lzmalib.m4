# SYNOPSIS
#
#   AX_CHECK_LZMALIB([action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for an installed lzma library. If nothing was
#   specified when calling configure, it searches first in /usr/local and
#   then in /usr, /opt/local and /sw. If the --with-lzma=DIR is specified,
#   it will try to find it in DIR/include/lzma.h and DIR/lib/liblzma.a. If
#   --without-lzma is specified, the library is not searched at all.
#
#   If either the header file (lzma.h) or the library (liblzma) is not found,
#   shell commands 'action-if-not-found' is run. If 'action-if-not-found' is
#   not specified, the configuration exits on error, asking for a valid lzma
#   installation directory or --without-lzma.
#
#   If both header file and library are found, shell commands
#   'action-if-found' is run. If 'action-if-found' is not specified, the
#   default action appends '-I${LZMA_HOME}/include' to CPFLAGS, appends
#   '-L$LZMA_HOME}/lib' to LDFLAGS, prepends '-lz' to LIBS, and calls
#   AC_DEFINE(HAVE_LIBLZMA). You should use autoheader to include a definition
#   for this symbol in a config.h file. Sample usage in a C/C++ source is as
#   follows:
#
#     #ifdef HAVE_LIBLZMA
#     #include <lzma.h>
#     #endif /* HAVE_LIBLZMA */
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

AU_ALIAS([CHECK_LZMALIB], [AX_CHECK_LZMALIB])
AC_DEFUN([AX_CHECK_LZMALIB],
#
# Handle user hints
#
[AC_MSG_CHECKING(if lzma is wanted)
lzma_places="/usr/local /usr /opt/local /sw"
AC_ARG_WITH([lzma],
[  --with-lzma=DIR         root directory path of lzma installation @<:@defaults to
                          /usr/local or /usr if not found in /usr/local@:>@
  --without-lzma          to disable lzma usage completely],
[if test "$withval" != no ; then
  AC_MSG_RESULT(yes)
  if test -d "$withval"
  then
    lzma_places="$withval $lzma_places"
  else
    AC_MSG_WARN([Sorry, $withval does not exist, checking usual places])
  fi
else
  lzma_places=
  AC_MSG_RESULT(no)
fi],
[AC_MSG_RESULT(yes)])

#
# Locate lzma, if wanted
#
if test -n "${lzma_places}"
then
	# check the user supplied or any other more or less 'standard' place:
	#   Most UNIX systems      : /usr/local and /usr
	#   MacPorts / Fink on OSX : /opt/local respectively /sw
	for LZMA_HOME in ${lzma_places} ; do
	  if test -f "${LZMA_HOME}/include/lzma.h"; then break; fi
	  LZMA_HOME=""
	done

  LZMA_OLD_LDFLAGS=$LDFLAGS
  LZMA_OLD_CPPFLAGS=$CPPFLAGS
  if test -n "${LZMA_HOME}"; then
        LDFLAGS="$LDFLAGS -L${LZMA_HOME}/lib"
        CPPFLAGS="$CPPFLAGS -I${LZMA_HOME}/include"
  fi
  AC_LANG_PUSH([C])
  AC_CHECK_LIB([lzma], [lzma_code], [lzma_cv_liblzma=yes], [lzma_cv_liblzma=no])
  AC_CHECK_HEADER([lzma.h], [lzma_cv_lzma_h=yes], [lzma_cv_lzma_h=no])
  AC_LANG_POP([C])
  if test "$lzma_cv_liblzma" = "yes" && test "$lzma_cv_lzma_h" = "yes"
  then
    #
    # If both library and header were found, action-if-found
    #
    m4_ifblank([$1],[
                CPPFLAGS="$CPPFLAGS -I${LZMA_HOME}/include"
                LDFLAGS="$LDFLAGS -L${LZMA_HOME}/lib"
                LIBS="-llzma $LIBS"
                AC_DEFINE([HAVE_LIBLZMA], [1],
                          [Define to 1 if you have `lzma' library (-llzma)])
               ],[
                # Restore variables
                LDFLAGS="$LZMA_OLD_LDFLAGS"
                CPPFLAGS="$LZMA_OLD_CPPFLAGS"
                $1
               ])
  else
    #
    # If either header or library was not found, action-if-not-found
    #
    m4_default([$2],[
                AC_MSG_ERROR([either specify a valid lzma installation with --with-lzma=DIR or disable lzma usage with --without-lzma])
                ])
  fi
fi
])
