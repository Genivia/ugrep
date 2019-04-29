#!/bin/sh
aclocal
autoheader
rm -f config.guess config.sub ar-lib compile depcomp install-sh missing
automake --add-missing --foreign
autoconf
automake
