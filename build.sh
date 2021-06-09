#!/bin/bash

# help wanted?
case $1 in
  --help|-h)
    ./configure --help
    exit 0
    ;;
esac

echo
echo "Building ugrep..."

# configure with the specified command arguments

echo
echo "./configure $@"
echo

# appease automake when the original timestamps are lost, when using git clone
touch aclocal.m4 Makefile.am lib/Makefile.am src/Makefile.am
sleep 1
touch config.h.in Makefile.in lib/Makefile.in src/Makefile.in
sleep 1
touch configure

if ! ./configure "$@" ; then
echo "Failed to complete ./configure $@"
echo "See config.log for more details"
exit 1
fi

echo
echo "make -j clean all"
echo

make clean

if ! make -j ; then
echo "Failed to build ugrep: please run the following two commands:"
echo "$ autoreconf -fi"
echo "$ ./build.sh"
echo
echo "If that does not work, please open an issue at:"
echo "https://github.com/Genivia/ugrep/issues"
exit 1
fi

echo
echo "make test"
echo

if ! make test ; then
echo "Testing failed, please open an issue at:"
echo "https://github.com/Genivia/ugrep/issues"
exit 1
fi

echo
echo "ugrep was successfully built in ugrep/bin and tested:"
ls -l bin/ug bin/ugrep
echo
echo "Copy ugrep/bin/ugrep and ugrep/bin/ug to a bin/ on your PATH"
echo
echo "Or install ugrep and ug on your system by executing:"
echo "sudo make install"
echo

