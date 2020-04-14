#!/bin/bash

UG="../src/ugrep --color=always --sort"

if [ ! -x "../src/ugrep" ] ; then
  echo "../src/ugrep not found, exiting"
  exit 1
fi

read -p "Are you sure to overwrite all test cases with new ones? (y/n [n]) " -rsn1 key < /dev/tty
case $key in
  y|Y)
    echo yes
    ;;
  *)
    echo no
    exit 0
    ;;
esac

export GREP_COLORS='cx=hb:ms=hug:mc=ib+W:fn=h35:ln=32h:cn=1;32:bn=1;32:se=+36'

echo "GENERATING TEST DIRECTORIES"

rm -rf out/ dir1/ dir2

mkdir -p out dir1 dir2

cd dir1; ln -s ../Hello.java .; cd ..
cd dir1; cp ../Hello.sh .; cd ..
cd dir2; ln -s ../Hello.java .; cd ..
cd dir2; cp ../Hello.sh .; cd ..
cd dir1; ln -s ../dir2 .; cd ..
cd dir2; ln -s ../dir1 .; cd ..
cat > dir1/.gitignore << END
# ignore shells
*.sh
# ignore dir2 (sub)directories
**/dir2/
END

$UG -rl                                  Hello dir1 > out/dir.out
$UG -Rl                                  Hello dir1 > out/dir-S.out
$UG -Rl -tShell                          Hello dir1 > out/dir-t.out
$UG -1l                                  Hello dir1 > out/dir-1.out
$UG -2l                                  Hello dir1 > out/dir-2.out
$UG -Rl --include='*.sh'                 Hello dir1 > out/dir--include.out
$UG -Rl --exclude='*.sh'                 Hello dir1 > out/dir--exclude.out
$UG -Rl --include-dir='dir1'             Hello dir1 > out/dir--include-dir.out
$UG -Rl --exclude-dir='dir2'             Hello dir1 > out/dir--exclude-dir.out
$UG -Rl --include-from='dir1/.gitignore' Hello dir2 > out/dir--include-from.out
$UG -Rl --exclude-from='dir1/.gitignore' Hello dir1 > out/dir--exclude-from.out
$UG -Rl --ignore-files                   Hello dir1 > out/dir--ignore-files.out
$UG -Rl --filter='sh:head -n1'           Hello dir1 > out/dir--filter.out

rm -rf dir1 dir2

echo "GENERATING TEST FILES"

cat > lorem << END
Lorêm
ïpsûm
dolor
sit
amét
END

$UG -Fiwco -f lorem lorem.utf8.txt  > out/lorem.utf8.out
$UG -Fiwco -f lorem lorem.utf16.txt > out/lorem.utf16.out
$UG -Fiwco -f lorem lorem.utf32.txt > out/lorem.utf32.out
cat lorem | $UG -Fiwco --encoding=LATIN1 -f - lorem.latin1.txt > out/lorem.latin1.out

$UG -ci hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello-ci.out
$UG -cj hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello-cj.out

$UG -e Hello -e '".*?"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello-ee.out
$UG -e Hello -N '".*?"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello-eN.out
$UG --max-count=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello--max-count.out
$UG --max-files=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello--max-files.out
$UG --range=1,1   Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > out/Hello_Hello--range.out

for PAT in '' 'Hello' '\w+\s+\S+' 'nomatch' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_'`
  for OUT in '' '-I' '-W' '-X' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkbT' '-unkbT' '-o' '-on' '-onkbT' '-ounkbT' '-nv' '-nC1' '-nvC1' '-ny' '-nvy' ; do
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN$OUT$OPS.out"
    done
  done
  for OUT in '--csv' '--json' '--xml' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkb' '-unkb' '-o' '-on' '-onkb' '-ounkb' ; do
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN$OUT$OPS.out"
    done
  done
  $UG -U --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN--format.out"
  $UG -U -Iw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-Iw.out"
  $UG -U -Ix "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-Ix.out"
  $UG -U -F  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-F.out"
  $UG -U -Fw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-Fw.out"
  $UG -U -Fx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-Fx.out"
  if [ "$PAT" == '\w+\s+\S+' ]; then
    $UG -U -G  '\w\+\s\+\S\+' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-G.out"
  else
    $UG -U -G  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-G.out"
  fi
  $UG -U -IP  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-IP.out"
  $UG -U -IPw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-IPw.out"
  $UG -U -IPx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt > "out/$FN-IPx.out"
done

echo "GENERATING TEST ARCHIVES"

rm -f archive.*

ls Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt empty.txt | cpio -o --quiet > archive.cpio
ls Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt empty.txt | pax -w -f archive.pax
tar cf archive.tar Hello.* empty.txt
compress -c archive.tar > archive.tar.Z
gzip  -9 -c archive.tar > archive.tgz
bzip2 -9 -c archive.tar > archive.tbz
lzma  -9 -c archive.tar > archive.tlz
xz    -9 -c archive.tar > archive.txz
zip   -9 -q archive.tar.zip archive.tar
zip   -9 -q archive.zip Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt empty.txt

$UG -z -c Hello archive.cpio    > out/archive.cpio.out
$UG -z -c Hello archive.pax     > out/archive.pax.out
$UG -z -c Hello archive.tar     > out/archive.tar.out
$UG -z -c Hello archive.tgz     > out/archive.tgz.out
$UG -z -c Hello archive.tar.Z   > out/archive.tar.Z.out
$UG -z -c Hello archive.tar.zip > out/archive.tar.zip.out
$UG -z -c Hello archive.zip     > out/archive.zip.out
$UG -z -c Hello archive.tbz     > out/archive.tbz.out
$UG -z -c Hello archive.tlz     > out/archive.tlz.out
$UG -z -c Hello archive.txz     > out/archive.txz.out

$UG -z -c -tShell Hello archive.cpio    > out/archive-t.cpio.out
$UG -z -c -tShell Hello archive.pax     > out/archive-t.pax.out
$UG -z -c -tShell Hello archive.tar     > out/archive-t.tar.out
$UG -z -c -tShell Hello archive.tgz     > out/archive-t.tgz.out
$UG -z -c -tShell Hello archive.tar.Z   > out/archive-t.tar.Z.out
$UG -z -c -tShell Hello archive.tar.zip > out/archive-t.tar.zip.out
$UG -z -c -tShell Hello archive.zip     > out/archive-t.zip.out
$UG -z -c -tShell Hello archive.tbz     > out/archive-t.tbz.out
$UG -z -c -tShell Hello archive.tlz     > out/archive-t.tlz.out
$UG -z -c -tShell Hello archive.txz     > out/archive-t.txz.out

for (( i = 0 ; i < 100000 ; i++ )) ; do
  echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit.  Nunc hendrerit at metus sit amet aliquam."
done | gzip -c > archive.gz

$UG -z -c '' archive.gz > out/archive.gz.out

for PAT in '\.' 'et' 'hendrerit' 'aliquam' 'sit amet aliquam' 'Nunc hendrerit at metus sit amet aliquam' 'adip[a-z]{1,}' 'adip[a-z]{4,}' 'adip[a-z]{6}' '[a-z]+' 'a[a-z]+' 'ad[a-z]+' 'adi[a-z]+' ; do
  FN=`echo "archive_$PAT" | tr -Cd '[:alnum:]_'`
  $UG -z -co "$PAT" archive.gz > out/$FN-co.gz.out
done
