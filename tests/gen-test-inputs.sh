#!/bin/bash
# requires: tar, compress, pax, cpio, gzip, bzip2, lzma, xz, lz4, zip


echo "GENERATING TEST ARCHIVES"

rm -f archive.*

for (( i = 0 ; i < 100000 ; i++ )) ; do
  echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit.  Nunc hendrerit at metus sit amet aliquam."
done | gzip -c > archive.gz


ls Hello.{bat,class,java,pdf,sh,txt} empty.txt | cpio -o --quiet > archive.cpio
ls Hello.{bat,class,java,pdf,sh,txt} empty.txt | pax -w -f archive.pax
tar cf archive.tar Hello.{bat,class,java,pdf,sh,txt} empty.txt
compress -c archive.tar > archive.tar.Z
gzip  -9 -c archive.tar > archive.tgz
bzip2 -9 -c archive.tar > archive.tbz
lzma  -9 -c archive.tar > archive.tlz
xz    -9 -c archive.tar > archive.txz
lz4   -9 -c archive.tar > archive.tar.lz4
zip   -9 -q archive.tar.zip archive.tar
zip   -9 -q archive.zip Hello.{bat,class,java,pdf,sh,txt} empty.txt
