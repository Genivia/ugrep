#!/bin/sh
aclocal
autoheader
automake --add-missing --foreign
autoconf
automake
