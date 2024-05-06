#!/bin/bash

# Build ugrep by invoking configure and make then test the binary

# For help with configure options:
# ./build.sh --help

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

echo "Checking whether ug+ and ugrep+ search pdfs, documents, image metadata:"
if [ -x "$(command -v pdftotext)" ] && pdftotext --help 2>&1 | src/ugrep -qw Poppler ; then
  echo "pdf: yes"
else
  echo "pdf: no, requires pdftotext"
fi
if [ -x "$(command -v antiword)" ] && antiword 2>&1 | src/ugrep -qw Adri ; then
  echo "doc: yes"
else
  echo "doc: no, requires antiword"
fi
if [ -x "$(command -v pandoc)" ] && pandoc --version 2>&1 | src/ugrep -qw pandoc.org ; then
  echo "docx: yes"
  echo "epub: yes"
  echo "odt: yes"
  echo "rtf: yes"
else
  echo "docx: no, requires pandoc"
  echo "epub: no, requires pandoc"
  echo "odt: no, requires pandoc"
  echo "rtf: no, requires pandoc"
fi
if [ -x "$(command -v exiftool)" ] ; then
  echo "gif: yes"
  echo "jpg: yes"
  echo "mpg: yes"
  echo "png: yes"
  echo "tiff: yes"
else
  echo "gif: no, requires exiftool"
  echo "jpg: no, requires exiftool"
  echo "mpg: no, requires exiftool"
  echo "png: no, requires exiftool"
  echo "tiff: no, requires exiftool"
fi

echo
echo "ugrep was successfully built in $(pwd)/bin and tested:"
ls -l bin/ug bin/ug+ bin/ugrep bin/ugrep+ bin/ugrep-indexer
echo
echo "Copy bin/ug, bin/ug+, bin/ugrep, bin/ugrep+, and bin/ugrep-indexer to a bin/ on your PATH"
echo
echo "Or install the ugrep tools on your system by executing:"
echo "sudo make install"
echo
