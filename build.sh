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

# configure with colors enabled by default or the command arguments
OPTIONS=${1:---enable-color}

echo
echo "./configure $OPTIONS"
echo

# configure with maintainer mode enabled to work around git timestamp issues
./configure $OPTIONS

echo
echo "make -j clean all"
echo

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

