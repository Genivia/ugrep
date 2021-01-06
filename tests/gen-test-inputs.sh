#!/bin/bash
# requires: tar, compress, pax, cpio, gzip, bzip2, lzma, xz, lz4, zip,
# iconv, javac

echo "GENERATING SIMPLE FILES"

echo -n > empty.txt

cat > lorem << END
Lorêm
ïpsûm
dolor
sit
amét
END


echo "GENERATING LOREM FILES"

iconv --from-code latin1 --to-code UTF-8    >  lorem.utf8.txt  lorem.latin1.txt
# UTF-16 in big-endian including a BOM indicating big-endian
echo -ne '\xFE\xFF' > lorem.utf16.txt
iconv --from-code latin1 --to-code UTF-16BE >> lorem.utf16.txt lorem.latin1.txt
# UTF-32 in big-endian including a BOM indicating big-endian
echo -ne '\x00\x00\xFE\xFF' > lorem.utf32.txt
iconv --from-code latin1 --to-code UTF-32BE >> lorem.utf32.txt lorem.latin1.txt


echo "COMPILING JAVA CODE"
javac Hello.java



echo "GENERATING TEST ARCHIVES"

rm -f archive.*

for (( i = 0 ; i < 100000 ; i++ )) ; do
  echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit.  Nunc hendrerit at metus sit amet aliquam."
done | gzip -c > archive.gz

# pax by default creates tar archives. Thus if pax is not availalbe,
# just use tar, passing it required options.
if which pax >/dev/null 2>&1 ; then
    pax="pax -w"
else
    pax="tar -c --files-from=-"
fi

ls -f Hello.{bat,class,java,pdf,sh,txt} empty.txt | \
    cpio -o --format odc --quiet > archive.cpio
ls -f Hello.{bat,class,java,pdf,sh,txt} empty.txt | \
    $pax -f archive.pax
tar cf archive.tar Hello.{bat,class,java,pdf,sh,txt} empty.txt
compress -c archive.tar > archive.tar.Z
gzip  -9 -c archive.tar > archive.tgz
bzip2 -9 -c archive.tar > archive.tbz
lzma  -9 -c archive.tar > archive.tlz
xz    -9 -c archive.tar > archive.txz
lz4   -9 -c archive.tar > archive.tar.lz4
zip   -9 -q archive.tar.zip archive.tar
zip   -9 -q archive.zip Hello.{bat,class,java,pdf,sh,txt} empty.txt
