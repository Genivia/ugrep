#!/bin/bash

UG="../src/ugrep --color=always -J1"

if [ ! -x "../src/ugrep" ] ; then
  echo "../src/ugrep not found, exiting"
  exit 1
fi

if [ ! -e ../config.h ] ; then
  echo "../config.h not found, exiting"
  exit 1
fi

$UG --version | head -n1

if $UG -Fq 'HAVE_BOOST_REGEX 1' ../config.h ; then
  have_boost_regex=yes
else
  have_boost_regex=no
fi

echo "Have libboost_regex?" $have_boost_regex

if $UG -Fq 'HAVE_LIBZ 1' ../config.h ; then
  have_libz=yes
else
  have_libz=no
fi

echo "Have libz?" $have_libz

if $UG -Fq 'HAVE_LIBBZ2 1' ../config.h ; then
  have_libbz2=yes
else
  have_libbz2=no
fi

echo "Have libbz2?" $have_libz

if $UG -Fq 'HAVE_LIBLZMA 1' ../config.h ; then
  have_liblzma=yes
else
  have_liblzma=no
fi

echo "Have liblzma?" $have_libz

export GREP_COLORS='cx=hb:ms=hug:mc=ib+W:fn=h35:ln=32h:cn=1;32:bn=1;32:se=+36'

function ERR() {
  echo "[1;31mError:[0m ugrep $1 [1;31mfailed[0m"
  exit 1
}

DIFF="diff -C0 -"

rm -rf dir1/ dir2/

mkdir -p dir1 dir2

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

printf .
$UG -rl                                  Hello dir1 | $DIFF out/dir.out               || ERR "-rl Hello dir1"
printf .
$UG -Rl                                  Hello dir1 | $DIFF out/dir-S.out             || ERR "-Rl Hello dir1"
printf .
$UG -Rl -tShell                          Hello dir1 | $DIFF out/dir-t.out             || ERR "-Rl -tShell Hello dir1"
printf .
$UG -Rl --max-depth=1                    Hello dir1 | $DIFF out/dir--max-depth.out    || ERR "-Rl --max-depth=1 Hello dir1"
printf .
$UG -Rl --include='*.sh'                 Hello dir1 | $DIFF out/dir--include.out      || ERR "-Rl --include='*.sh' Hello dir1"
printf .
$UG -Rl --exclude='*.sh'                 Hello dir1 | $DIFF out/dir--exclude.out      || ERR "-Rl --exclude='*.sh' Hello dir1"
printf .
$UG -Rl --exclude-dir='dir2'             Hello dir1 | $DIFF out/dir--exclude-dir.out  || ERR "-Rl --exclude-dir='dir2' Hello dir1"
printf .
$UG -Rl --include-dir='dir1'             Hello dir1 | $DIFF out/dir--include-dir.out  || ERR "-Rl --include-dir='dir1' Hello dir1"
printf .
$UG -Rl --exclude-dir='dir2'             Hello dir1 | $DIFF out/dir--exclude-dir.out  || ERR "-Rl --exclude-dir='dir2' Hello dir1"
printf .
$UG -Rl --include-from='dir1/.gitignore' Hello dir2 | $DIFF out/dir--include-from.out || ERR "-Rl --include-from='dir1/.gitignore' Hello dir2"
printf .
$UG -Rl --exclude-from='dir1/.gitignore' Hello dir1 | $DIFF out/dir--exclude-from.out || ERR "-Rl --exclude-from='dir1/.gitignore' Hello dir1"
printf .
$UG -Rl --ignore-files                   Hello dir1 | $DIFF out/dir--ignore-files.out || ERR "-Rl -ignore-files Hello Hello dir1"
printf .
$UG -Rl --filter='sh:head -n1'           Hello dir1 | $DIFF out/dir--filter.out       || ERR "-Rl --filter='sh:head -n1' Hello dir1"

rm -rf dir1 dir2

printf .
$UG -Fiwco -f lorem lorem.utf8.txt  | $DIFF out/lorem.utf8.out  || ERR "-Fiwco -f lorem lorem.utf8.txt"
printf .
$UG -Fiwco -f lorem lorem.utf16.txt | $DIFF out/lorem.utf16.out || ERR "-Fiwco -f lorem lorem.utf16.txt"
printf .
$UG -Fiwco -f lorem lorem.utf32.txt | $DIFF out/lorem.utf32.out || ERR "-Fiwco -f lorem lorem.utf32.txt"
printf .
cat lorem | $UG -Fiwco -Q LATIN1 -f - lorem.latin1.txt | $DIFF out/lorem.latin1.out || ERR "-Fiwco -Q LATIN1 -f lorem lorem.latin1.txt"

printf .
$UG -ci hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello-ci.out \
    || ERR "-ci hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG -cj hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello-cj.out \
    || ERR "-cj hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"

printf .
$UG -e Hello -e '".*?"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello-ee.out \
    || ERR "-e Hello -e '\".*?\"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG -e Hello -N '".*?"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello-eN.out \
    || ERR "-e Hello -N '\".*?\"' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG --max-count=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello--max-count.out \
    || ERR "--max-count=1  Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG --max-files=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello--max-files.out \
    || ERR "--max-files=1 Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG --range=1,1   Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello--range.out \
    || ERR "--range=1,1 Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"

for PAT in '' 'Hello' '\w+\s+\S+' 'nomatch' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_'`
  for OUT in '' '-I' '-W' '-X' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkbT' '-unkbT' '-o' '-on' '-onkbT' '-ounkbT' '-nv' '-nC1' '-nvC1' '-ny' '-nvy' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    done
  done
  for OUT in '--csv' '--json' '--xml' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkb' '-unkb' '-o' '-on' '-onkb' '-ounkb' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    done
  done
  printf .
  $UG -U --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN--format.out" \
    || ERR "--format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Iw  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Iw.out" \
    || ERR "-w $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Ix  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Ix.out" \
    || ERR "-x $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -F  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-F.out" \
    || ERR "-F $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Fw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Fw.out" \
    || ERR "-Fw $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Fx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Fx.out" \
    || ERR "-Fx $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  if [ "$PAT" == '\w+\s+\S+' ]; then
    $UG -U -G '\w\+\s\+\S\+' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  else
    $UG -U -G "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  fi
  if [ "$have_boost_regex" == yes ]; then
    printf .
    $UG -U -P  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-P.out" \
      || ERR "-P $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    printf .
    $UG -U -Pw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Pw.out" \
      || ERR "-Pw $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    printf .
    $UG -U -Px "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Px.out" \
      || ERR "-Px $PAT Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  fi
done

printf .
$UG -z -c Hello archive.cpio    | $DIFF out/archive.cpio.out    || ERR "-z -c Hello archive.cpio"
printf .
$UG -z -c Hello archive.pax     | $DIFF out/archive.pax.out     || ERR "-z -c Hello archive.pax"
printf .
$UG -z -c Hello archive.tar     | $DIFF out/archive.tar.out     || ERR "-z -c Hello archive.tar"
if [ "$have_libz" == yes ]; then
printf .
$UG -z -c Hello archive.tgz     | $DIFF out/archive.tgz.out     || ERR "-z -c Hello archive.tgz"
printf .
$UG -z -c Hello archive.tar.Z   | $DIFF out/archive.tar.Z.out   || ERR "-z -c Hello archive.tar.Z"
printf .
$UG -z -c Hello archive.tar.zip | $DIFF out/archive.tar.zip.out || ERR "-z -c Hello archive.tar.zip"
printf .
$UG -z -c Hello archive.zip     | $DIFF out/archive.zip.out     || ERR "-z -c Hello archive.zip"
if [ "$have_libbz2" == yes ]; then
printf .
$UG -z -c Hello archive.tbz     | $DIFF out/archive.tbz.out     || ERR "-z -c Hello archive.tbz"
fi
if [ "$have_liblzma" == yes ]; then
printf .
$UG -z -c Hello archive.tlz     | $DIFF out/archive.tlz.out     || ERR "-z -c Hello archive.tlz"
printf .
$UG -z -c Hello archive.txz     | $DIFF out/archive.txz.out     || ERR "-z -c Hello archive.txz"
fi
fi

printf .
$UG -z -c -tShell Hello archive.cpio    | $DIFF out/archive-t.cpio.out    || ERR "-z -c -tShell Hello archive.cpio"
printf .
$UG -z -c -tShell Hello archive.pax     | $DIFF out/archive-t.pax.out     || ERR "-z -c -tShell Hello archive.pax"
printf .
$UG -z -c -tShell Hello archive.tar     | $DIFF out/archive-t.tar.out     || ERR "-z -c -tShell Hello archive.tar"
if [ "$have_libz" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tgz     | $DIFF out/archive-t.tgz.out     || ERR "-z -c -tShell Hello archive.tgz"
printf .
$UG -z -c -tShell Hello archive.tar.Z   | $DIFF out/archive-t.tar.Z.out   || ERR "-z -c -tShell Hello archive.tar.Z"
printf .
$UG -z -c -tShell Hello archive.tar.zip | $DIFF out/archive-t.tar.zip.out || ERR "-z -c -tShell Hello archive.tar.zip"
printf .
$UG -z -c -tShell Hello archive.zip     | $DIFF out/archive-t.zip.out     || ERR "-z -c -tShell Hello archive.zip"
if [ "$have_libbz2" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tbz     | $DIFF out/archive-t.tbz.out     || ERR "-z -c -tShell Hello archive.tbz"
fi
if [ "$have_liblzma" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tlz     | $DIFF out/archive-t.tlz.out     || ERR "-z -c -tShell Hello archive.tlz"
printf .
$UG -z -c -tShell Hello archive.txz     | $DIFF out/archive-t.txz.out     || ERR "-z -c -tShell Hello archive.txz"
fi
fi

if [ "$have_libz" == yes ]; then
printf .
$UG -z -c '' archive.gz | $DIFF out/archive.gz.out || ERR "-z -c '' archive.gz"
for PAT in '\.' 'et' 'hendrerit' 'aliquam' 'sit amet aliquam' 'Nunc hendrerit at metus sit amet aliquam' 'adip[a-z]{1,}' 'adip[a-z]{4,}'; do
  FN=`echo "archive_$PAT" | tr -Cd '[:alnum:]_'`
  printf .
  $UG -z -co "$PAT" archive.gz | $DIFF out/$FN-co.gz.out || ERR "-z -co \"$PAT\" archive.gz"
done
fi

echo
echo "ALL TESTS PASSED"
