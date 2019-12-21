#!/bin/sh

# if RE/flex was installed below this directory, then copy its files
if [ -d ../reflex ]; then
echo "Copying updated RE/flex source code files..."
cp -r ../reflex/include/reflex/*.h include/reflex
cp -r ../reflex/unicode/*.cpp lib
cp -r ../reflex/lib/*.cpp lib
fi

# run autoconf and automake stuff
aclocal
autoheader
automake --add-missing --foreign
autoconf
automake
./configure --enable-color
