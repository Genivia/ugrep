#!/bin/bash

UG="../src/ugrep --color=always --sort"

FILES="Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt empty.txt emptyline.txt"

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

ln -s ../Hello.java dir1
cp Hello.sh dir1
cp Hello.bat dir1
cp makefile dir1
ln -s ../Hello.java dir2
cp Hello.sh dir2
cp Hello.bat dir2
ln -s ../dir2 dir1
ln -s ../dir1 dir2
cat > dir1/.gitignore << END
# ignore shells
*.sh
# ignore dir2 (sub)directories
**/dir2/
END

$UG -rl                                  Hello dir1 > out/dir.out
$UG -Rl                                  Hello dir1 > out/dir-S.out
$UG -Rl -Osh                             Hello dir1 > out/dir-O.out
$UG -Rl -M'#!/bin/sh'                    Hello dir1 > out/dir-M.out
$UG -Rl -tShell,make                     Hello dir1 > out/dir-t.out
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

for OPS in '' '-F' '-G' '-P' ; do
  $UG $OPS -iwco -f lorem lorem.utf8.txt  > "out/lorem.utf8$OPS-iwco.out"
  $UG $OPS -iwco -f lorem lorem.utf16.txt > "out/lorem.utf16$OPS-iwco.out"
  $UG $OPS -iwco -f lorem lorem.utf32.txt > "out/lorem.utf32$OPS-iwco.out"
  cat lorem | $UG $OPS -iwco --encoding=LATIN1 -f - lorem.latin1.txt > "out/lorem.latin1$OPS-iwco.out"
done

$UG -Zio Lorem lorem.utf8.txt > out/lorem_Lorem-Zio.out

$UG -ci hello $FILES > out/Hello_Hello-ci.out
$UG -cj hello $FILES > out/Hello_Hello-cj.out

$UG -e Hello -e '".*?"' $FILES > out/Hello_Hello-ee.out
$UG -e Hello -N '".*?"' $FILES > out/Hello_Hello-eN.out
$UG --max-count=1 Hello $FILES > out/Hello_Hello--max-count.out
$UG --max-files=1 Hello $FILES > out/Hello_Hello--max-files.out
$UG --range=1,1   Hello $FILES > out/Hello_Hello--range.out

for PAT in '' 'Hello' '\w+[\n\h]+\S+' '\S\n\S' 'nomatch' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_'`
  for OUT in '' '-I' '-W' '-X' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkbT' '-unkbT' '-o' '-on' '-onkbT' '-ounkbT' '-v' '-nv' '-C2' '-nC2' '-vC2' '-nvC2' '-onC10' '-y' '-ny' '-vy' '-nvy' ; do
      $UG -U $OUT $OPS "$PAT" $FILES > "out/$FN$OUT$OPS.out"
    done
  done
  for OUT in '--csv' '--json' '--xml' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-v' '-nv' '-nkb' '-unkb' '-o' '-on' '-onkb' '-ounkb' ; do
      $UG -U $OUT $OPS "$PAT" $FILES > "out/$FN$OUT$OPS.out"
    done
  done
  $UG -U --tag "$PAT" $FILES > "out/$FN--tag.out"
  $UG -U --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" $FILES > "out/$FN--format.out"
  $UG -U -v --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" $FILES > "out/$FN-v--format.out"
  $UG -U --replace='(%m:%o)' "$PAT" $FILES > "out/$FN--replace.out"
  $UG -U -Iw "$PAT" $FILES > "out/$FN-Iw.out"
  $UG -U -Ix "$PAT" $FILES > "out/$FN-Ix.out"
  $UG -U -F  "$PAT" $FILES > "out/$FN-F.out"
  $UG -U -Fw "$PAT" $FILES > "out/$FN-Fw.out"
  $UG -U -Fx "$PAT" $FILES > "out/$FN-Fx.out"
  if [ "$PAT" == '\w+[\n\h]+\S+' ]; then
    $UG -U -G  '\w\+[\n\h]\+\S\+' $FILES > "out/$FN-G.out"
    $UG -U -Gw '\w\+[\n\h]\+\S\+' $FILES > "out/$FN-Gw.out"
    $UG -U -Gx '\w\+[\n\h]\+\S\+' $FILES > "out/$FN-Gx.out"
  else
    $UG -U -G  "$PAT" $FILES > "out/$FN-G.out"
    $UG -U -Gw "$PAT" $FILES > "out/$FN-Gw.out"
    $UG -U -Gx "$PAT" $FILES > "out/$FN-Gx.out"
  fi
  $UG -U -IP  "$PAT" $FILES > "out/$FN-IP.out"
  $UG -U -IPw "$PAT" $FILES > "out/$FN-IPw.out"
  $UG -U -IPx "$PAT" $FILES > "out/$FN-IPx.out"
done

for PAT in '' 'Hello World' 'Hello -World' 'Hello -bin' 'bin -Hello' 'bin -greeting' 'Hello -World|greeting' 'Hello -bin|greeting' 'Hello -(greeting|World)' '"a Hello" greeting' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_-'`
  for OPS in '' '-l' '-c' '-co' '-o' '-C2' '-y' '--json' ; do
    $UG -U --bool $OPS "$PAT" $FILES > "out/$FN--bool$OPS.out"
    $UG -U --files --bool $OPS "$PAT" $FILES > "out/$FN--files--bool$OPS.out"
  done
done

$UG -U -e 'Hello' --and 'World' $FILES > "out/Hello--and.out"
$UG -U -e 'Hello' --andnot 'World' $FILES > "out/Hello--andnot.out"
$UG -U -e 'Hello' --and --not 'World' -e 'greeting' $FILES > "out/Hello--and--not.out"

echo "GENERATING TEST ARCHIVES"

rm -f archive.*

ls $FILES | cpio -o --quiet > archive.cpio
ls $FILES | pax -w -f archive.pax
tar cf archive.tar $FILES
compress  -c archive.tar > archive.tZ
gzip   -9 -c archive.tar > archive.tgz
bzip2  -9 -c archive.tar > archive.tbz
lzma   -9 -c archive.tar > archive.tlz
xz     -9 -c archive.tar > archive.txz
lz4    -9 -c archive.tar > archive.tar.lz4
zstd   -9 -c archive.tar > archive.tzst
brotli -9 -c archive.tar > archive.tar.br
bzip3     -c archive.tar > archive.tar.bz3
zip    -9 -q archive.tar.zip archive.tar
zip    -9 -q archive.zip $FILES

tar cfz archive2.tgz archive.tar $FILES archive.tgz
tar cfz archive3.tgz archive.tgz $FILES archive2.tgz

$UG -z -c Hello archive.cpio    > out/archive.cpio.out
$UG -z -c Hello archive.pax     > out/archive.pax.out
$UG -z -c Hello archive.tar     > out/archive.tar.out
$UG -z -c Hello archive.tgz     > out/archive.tgz.out
$UG -z -c Hello archive.tZ      > out/archive.tZ.out
$UG -z -c Hello archive.tar.zip > out/archive.tar.zip.out
$UG -z -c Hello archive.zip     > out/archive.zip.out
$UG -z -c Hello archive.tbz     > out/archive.tbz.out
$UG -z -c Hello archive.tlz     > out/archive.tlz.out
$UG -z -c Hello archive.txz     > out/archive.txz.out
$UG -z -c Hello archive.tar.lz4 > out/archive.tar.lz4.out
$UG -z -c Hello archive.tzst    > out/archive.tzst.out
$UG -z -c Hello archive.tar.br  > out/archive.tar.br.out
$UG -z -c Hello archive.tar.bz3 > out/archive.tar.bz3.out

$UG -z -c -tShell Hello archive.cpio    > out/archive-t.cpio.out
$UG -z -c -tShell Hello archive.pax     > out/archive-t.pax.out
$UG -z -c -tShell Hello archive.tar     > out/archive-t.tar.out
$UG -z -c -tShell Hello archive.tgz     > out/archive-t.tgz.out
$UG -z -c -tShell Hello archive.tZ      > out/archive-t.tZ.out
$UG -z -c -tShell Hello archive.tar.zip > out/archive-t.tar.zip.out
$UG -z -c -tShell Hello archive.zip     > out/archive-t.zip.out
$UG -z -c -tShell Hello archive.tbz     > out/archive-t.tbz.out
$UG -z -c -tShell Hello archive.tlz     > out/archive-t.tlz.out
$UG -z -c -tShell Hello archive.txz     > out/archive-t.txz.out
$UG -z -c -tShell Hello archive.tar.lz4 > out/archive-t.tar.lz4.out
$UG -z -c -tShell Hello archive.tzst    > out/archive-t.tzst.out
$UG -z -c -tShell Hello archive.tar.br  > out/archive-t.tar.br.out
$UG -z -c -tShell Hello archive.tar.bz3 > out/archive-t.tar.bz3.out

$UG --zmax=2 -z -c Hello archive2.tgz         > out/archive2.tgz.out
$UG --zmax=3 -z -c Hello archive3.tgz         > out/archive3.tgz.out
$UG --zmax=2 -z -c -tShell Hello archive2.tgz > out/archive2-t.tgz.out
$UG --zmax=3 -z -c -tShell Hello archive3.tgz > out/archive3-t.tgz.out

for (( i = 0 ; i < 100000 ; i++ )) ; do
  echo "Lorem ipsum dolor sit amet, consectetur adipiscing elit.  Nunc hendrerit at metus sit amet aliquam."
done | gzip -c > archive.gz

$UG -z -c '' archive.gz > out/archive.gz.out

for PAT in '\.' 'et' 'hendrerit' 'aliquam' 'sit amet aliquam' 'Nunc hendrerit at metus sit amet aliquam' 'adip[a-z]{1,}' 'adip[a-z]{4,}' 'adip[a-z]{6}' '[a-z]+' 'a[a-z]+' 'ad[a-z]+' 'adi[a-z]+' ; do
  FN=`echo "archive_$PAT" | tr -Cd '[:alnum:]_'`
  $UG -z -co "$PAT" archive.gz > out/$FN-co.gz.out
done
