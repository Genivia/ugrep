#!/bin/sh

# This script is used to prepare a new release update by software maintainers
# - update version in src/ugrep.hpp
# - rebuild ugrep to verify
# - generate man page
# - generate completion scripts
# - update version in configure.ac
# - run autoconf and automake

# if RE/flex was installed below this directory, then copy its files to update (optional)
# if [ -d ../reflex ]; then
# echo "Copying updated RE/flex source code files..."
# cp -r ../reflex/include/reflex/*.h include/reflex
# cp -r ../reflex/unicode/*.cpp lib
# cp -r ../reflex/lib/*.cpp lib
# cp -r ../reflex/fuzzy/fuzzymatcher.h include/reflex
# fi
# change lib/Makefile.am to use noinst_LIBRARIES
# sed -i .bak 's/lib_LIBRARIES/noinst_LIBRARIES/' lib/Makefile.am
# rm -f lib/Makefile.am.bak

if fgrep -r -I FIXME include lib src
then
  echo "FIXME in code base"
  exit 1
fi

if [ "$#" = 1 ]
then

echo
echo "Bumping ugrep version to $1"

sed "s/define UGREP_VERSION \"[^\"]*\"/define UGREP_VERSION \"$1\"/" src/ugrep.hpp > src/ugrep.tmp && mv -f src/ugrep.tmp src/ugrep.hpp || exit 1
sed "s/define UGREP_VERSION \"[^\"]*\"/define UGREP_VERSION \"$1\"/" src/ugrep-indexer.cpp > src/ugrep-indexer.tmp && mv -f src/ugrep-indexer.tmp src/ugrep-indexer.cpp || exit 1

# this may be needed to reconfigure for glibtoolize for example
# autoreconf -fvi

./build.sh --with-bzip3 || exit 1
./man.sh $1
pushd completions/bash ; ./compgen.sh > /dev/null ; popd || exit 1
pushd completions/fish ; ./compgen.sh > /dev/null ; popd || exit 1
pushd completions/zsh  ; ./compgen.sh > /dev/null ; popd || exit 1

sed "s/^\(AC_INIT(\[ugrep\],\[\)[0-9.]*/\1$1/" configure.ac > configure.tmp && mv -f configure.tmp configure.ac || exit 1

# run autoconf and automake stuff with maintainer mode disabled
aclocal
autoheader
rm -f config.guess config.sub ar-lib compile depcomp install-sh missing
automake --add-missing --foreign
autoconf
automake
touch config.h.in
./configure
make

echo OK

else

echo "Usage: ./makemake.sh 5.v.v"
exit 1

fi
