#!/bin/bash

UG="../src/ugrep --color=always --sort $@"

if test ! -x "../src/ugrep" ; then
  echo "../src/ugrep not found, exiting"
  exit 1
fi

if test ! -e ../config.h ; then
  echo "../config.h not found, exiting"
  exit 1
fi

if ! $UG --version | head -n1 ; then
  echo "../src/ugrep failed to execute, exiting"
  exit 1
fi

if $UG -Fq 'HAVE_PCRE2 1' ../config.h ; then
  have_pcre2=yes
else
  have_pcre2=no
fi

echo "Have libpcre2?" $have_pcre2

if test "$have_pcre2" = "no" ; then
  if $UG -Fq 'HAVE_BOOST_REGEX 1' ../config.h ; then
    have_boost_regex=yes
  else
    have_boost_regex=no
  fi

  echo "Have libboost_regex?" $have_boost_regex
fi

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

echo "Have libbz2?" $have_libbz2

if $UG -Fq 'HAVE_LIBLZMA 1' ../config.h ; then
  have_liblzma=yes
else
  have_liblzma=no
fi

echo "Have liblzma?" $have_liblzma

if $UG -Fq 'HAVE_LIBLZ4 1' ../config.h ; then
  have_liblz4=yes
else
  have_liblz4=no
fi

echo "Have liblz4?" $have_liblz4

export GREP_COLORS='cx=hb:ms=hug:mc=ib+W:fn=h35:ln=32h:cn=1;32:bn=1;32:se=+36'

function ERR() {
  echo "[1;31mError:[0m[1m ugrep --sort $1 [1;31mfailed[0m"
  exit 1
}

DIFF="diff -C1 -"

rm -rf dir1/ dir2/

mkdir -p dir1 dir2

ln -s ../Hello.java dir1
cp Hello.sh dir1
ln -s ../Hello.java dir2
cp Hello.sh dir2
ln -s ../dir2 dir1
ln -s ../dir1 dir2
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
$UG -1l                                  Hello dir1 | $DIFF out/dir-1.out             || ERR "-1l Hello dir1"
printf .
$UG -2l                                  Hello dir1 | $DIFF out/dir-2.out             || ERR "-2l Hello dir1"
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
cat lorem | $UG -Fiwco --encoding=LATIN1 -f - lorem.latin1.txt | $DIFF out/lorem.latin1.out || ERR "-Fiwco --encoding=LATIN1 -f lorem lorem.latin1.txt"

printf .
$UG -Zio Lorem lorem.utf8.txt | $DIFF out/lorem_Lorem-Zio.out  || ERR "-Zio Lorem lorem.utf8.txt"

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
    || ERR "--max-count=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG --max-files=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello--max-files.out \
    || ERR "--max-files=1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG --range=1,1   Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF out/Hello_Hello--range.out \
    || ERR "--range=1,1 Hello Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"

for PAT in '' 'Hello' '\w+\s+\S+' '\S\n\S' 'nomatch' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_'`
  for OUT in '' '-I' '-W' '-X' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkbT' '-unkbT' '-o' '-on' '-onkbT' '-ounkbT' '-v' '-nv' '-C2' '-nC2' '-vC2' '-nvC2' '-y' '-ny' '-vy' '-nvy' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    done
  done
  for OUT in '--csv' '--json' '--xml' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-v' '-nv' '-nkb' '-unkb' '-o' '-on' '-onkb' '-ounkb' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    done
  done
  printf .
  $UG -U --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN--format.out" \
    || ERR "--format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -v --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-v--format.out" \
    || ERR "-v --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Iw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Iw.out" \
    || ERR "-Iw '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Ix "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Ix.out" \
    || ERR "-Ix '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -F  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-F.out" \
    || ERR "-F '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Fw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Fw.out" \
    || ERR "-Fw '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  $UG -U -Fx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN-Fx.out" \
    || ERR "-Fx '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  printf .
  if [ "$PAT" == '\w+\s+\S+' ]; then
    $UG -U -G  '\w\+\s\+\S\+' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    $UG -U -Gw '\w\+\s\+\S\+' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Gw.out" \
      || ERR "-Gw '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    $UG -U -Gx '\w\+\s\+\S\+' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Gx.out" \
      || ERR "-Gx '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  else
    $UG -U -G  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    $UG -U -Gw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Gw.out" \
      || ERR "-Gw '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    $UG -U -Gx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-Gx.out" \
      || ERR "-Gx '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  fi
  if [ "$have_pcre2" == yes ] || [ "$have_boost_regex" == yes ]; then
    printf .
    $UG -U -IP  "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-IP.out" \
      || ERR "-IP '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    printf .
    $UG -U -IPw "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-IPw.out" \
      || ERR "-IPw '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
    printf .
    $UG -U -IPx "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
      | $DIFF "out/$FN-IPx.out" \
      || ERR "-IPx '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
  fi
done

for PAT in '' 'Hello World' 'Hello -World' 'Hello -World|greeting' 'Hello -(greeting|World)' '"a Hello" greeting' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_-'`
  printf .
  $UG -U --bool "$PAT" Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/$FN--bool.out" \
    || ERR "--bool '$PAT' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
done

printf .
$UG -U -e 'Hello' --and 'World' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/Hello--and.out" \
    || ERR "-e 'Hello' --and 'World' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG -U -e 'Hello' --andnot 'World' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/Hello--andnot.out" \
    || ERR "-e 'Hello' --andnot 'World' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"
printf .
$UG -U -e 'Hello' --and --not 'World' -e 'greeting' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt \
    | $DIFF "out/Hello--and--not.out" \
    || ERR "-e 'Hello' --and --not 'World' -e 'greeting' Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt"

if [ "$have_libz" == yes ]; then
printf .
$UG -z -c Hello archive.cpio    | $DIFF out/archive.cpio.out    || ERR "-z -c Hello archive.cpio"
printf .
$UG -z -c Hello archive.pax     | $DIFF out/archive.pax.out     || ERR "-z -c Hello archive.pax"
printf .
$UG -z -c Hello archive.tar     | $DIFF out/archive.tar.out     || ERR "-z -c Hello archive.tar"
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
if [ "$have_liblz4" == yes ]; then
printf .
$UG -z -c Hello archive.tar.lz4 | $DIFF out/archive.tar.lz4.out || ERR "-z -c Hello archive.tar.lz4"
fi
fi

if [ "$have_libz" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.cpio    | $DIFF out/archive-t.cpio.out    || ERR "-z -c -tShell Hello archive.cpio"
printf .
$UG -z -c -tShell Hello archive.pax     | $DIFF out/archive-t.pax.out     || ERR "-z -c -tShell Hello archive.pax"
printf .
$UG -z -c -tShell Hello archive.tar     | $DIFF out/archive-t.tar.out     || ERR "-z -c -tShell Hello archive.tar"
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
if [ "$have_liblz4" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tar.lz4 | $DIFF out/archive-t.tar.lz4.out || ERR "-z -c -tShell Hello archive.tar.lz4"
fi
fi

if [ "$have_libz" == yes ]; then
printf .
$UG -z -c '' archive.gz | $DIFF out/archive.gz.out || ERR "-z -c '' archive.gz"
for PAT in '\.' 'et' 'hendrerit' 'aliquam' 'sit amet aliquam' 'Nunc hendrerit at metus sit amet aliquam' 'adip[a-z]{1,}' 'adip[a-z]{4,}' 'adip[a-z]{6}' '[a-z]+' 'a[a-z]+' 'ad[a-z]+' 'adi[a-z]+' ; do
  FN=`echo "archive_$PAT" | tr -Cd '[:alnum:]_'`
  printf .
  $UG -z -co "$PAT" archive.gz | $DIFF out/$FN-co.gz.out || ERR "-z -co '$PAT' archive.gz"
done
fi

echo
echo "ALL TESTS PASSED"
