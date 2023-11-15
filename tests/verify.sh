#!/bin/bash

UGREP=${UGREP_ABS_PATH:-../src/ugrep}
CONFIGH=${CONFIGH_ABS_PATH:-../config.h}

UG="$UGREP --color=always --sort $@"

FILES="Hello.bat Hello.class Hello.java Hello.pdf Hello.sh Hello.txt empty.txt emptyline.txt"

# check for errors in the installation

if test ! -x "$UGREP" ; then
  echo "$UGREP not found, exiting"
  exit 1
fi

if test ! -e "$CONFIGH" ; then
  echo "$CONFIGH not found, exiting"
  exit 1
fi

if ! $UG --version | head -n1 ; then
  echo "$UGREP failed to execute, exiting"
  exit 1
fi

# check for recommended libraries

if $UG -Fq 'HAVE_PCRE2 1' "$CONFIGH" ; then
  have_pcre2=yes
else
  have_pcre2=no
fi

printf "Have libpcre2?  %3s (recommended)\n" $have_pcre2

if test "$have_pcre2" = "no" ; then
  if $UG -Fq 'HAVE_BOOST_REGEX 1' "$CONFIGH" ; then
    have_boost_regex=yes
  else
    have_boost_regex=no
  fi

  printf "Have libboost_regex? %s (optional)\n" $have_boost_regex
fi

if $UG -Fq 'HAVE_LIBZ 1' "$CONFIGH" ; then
  have_libz=yes
else
  have_libz=no
fi

printf "Have libz?      %3s (recommended)\n" $have_libz

if $UG -Fq 'HAVE_LIBBZ2 1' "$CONFIGH" ; then
  have_libbz2=yes
else
  have_libbz2=no
fi

printf "Have libbz2?    %3s (recommended)\n" $have_libbz2

if $UG -Fq 'HAVE_LIBLZMA 1' "$CONFIGH" ; then
  have_liblzma=yes
else
  have_liblzma=no
fi

printf "Have liblzma?   %3s (recommended)\n" $have_liblzma

# check for optional libraries

if $UG -Fq 'HAVE_LIBLZ4 1' "$CONFIGH" ; then
  have_liblz4=yes
else
  have_liblz4=no
fi

printf "Have liblz4?    %3s (optional)\n" $have_liblz4

if $UG -Fq 'HAVE_LIBZSTD 1' "$CONFIGH" ; then
  have_libzstd=yes
else
  have_libzstd=no
fi

printf "Have libzstd?   %3s (optional)\n" $have_libzstd

if $UG -Fq 'HAVE_LIBBROTLI 1' "$CONFIGH" ; then
  have_libbrotli=yes
else
  have_libbrotli=no
fi

printf "Have libbrotli? %3s (optional)\n" $have_libbrotli

# check for uncommon libraries that aren't typically installed, don't report

if $UG -Fq 'HAVE_LIBBZIP3 1' "$CONFIGH" ; then
  have_libbzip3=yes
  printf "Have libbzip3?  yes (optional)\n"
else
  have_libbzip3=no
fi

# set special color scheme

export GREP_COLORS='cx=hb:ms=hug:mc=ib+W:fn=h35:ln=32h:cn=1;32:bn=1;32:se=+36'

# set ERR and DIFF to compare and report

function ERR() {
  echo "[1;31mError:[0m[1m ugrep --sort $1 [1;31mfailed[0m"
  exit 1
}

DIFF="diff -U1 -"

# prepare to test

rm -rf dir1/ dir2/

mkdir -p dir1 dir2

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

# tests

printf .
$UG -rl                                  Hello dir1 | $DIFF out/dir.out               || ERR "-rl Hello dir1"
printf .
$UG -Rl                                  Hello dir1 | $DIFF out/dir-S.out             || ERR "-Rl Hello dir1"
printf .
$UG -Rl -Osh                             Hello dir1 | $DIFF out/dir-O.out             || ERR "-Rl -Osh Hello dir1"
printf .
$UG -Rl -M'#!/bin/sh'                    Hello dir1 | $DIFF out/dir-M.out             || ERR "-Rl -M'#!/bin/sh' Hello dir1"
printf .
$UG -Rl -tShell,make                     Hello dir1 | $DIFF out/dir-t.out             || ERR "-Rl -tShell,make Hello dir1"
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

for OPS in '' '-F' '-G' ; do
  printf .
  $UG $OPS -iwco -f lorem lorem.utf8.txt \
    | $DIFF "out/lorem.utf8$OPS-iwco.out" \
    || ERR "$OPS -iwco -f lorem lorem.utf8.txt"
  printf .
  $UG $OPS -iwco -f lorem lorem.utf16.txt \
    | $DIFF "out/lorem.utf8$OPS-iwco.out" \
    || ERR "$OPS -iwco -f lorem lorem.utf16.txt"
  printf .
  $UG $OPS -iwco -f lorem lorem.utf32.txt \
    | $DIFF "out/lorem.utf8$OPS-iwco.out" \
    || ERR "$OPS -iwco -f lorem lorem.utf32.txt"
  printf .
  cat lorem | $UG $OPS -iwco --encoding=LATIN1 -f - lorem.latin1.txt \
    | $DIFF "out/lorem.latin1$OPS-iwco.out" \
    || ERR "$OPS -iwco -f lorem lorem.latin1.txt"
done

if [ "$have_pcre2" == yes ]; then
  printf .
  $UG -P -iwco -f lorem lorem.utf8.txt \
    | $DIFF "out/lorem.utf8-P-iwco.out" \
    || ERR "-P -iwco -f lorem lorem.utf8.txt"
  printf .
  $UG -P -iwco -f lorem lorem.utf16.txt \
    | $DIFF "out/lorem.utf8-P-iwco.out" \
    || ERR "-P -iwco -f lorem lorem.utf16.txt"
  printf .
  $UG -P -iwco -f lorem lorem.utf32.txt \
    | $DIFF "out/lorem.utf8-P-iwco.out" \
    || ERR "-P -iwco -f lorem lorem.utf32.txt"
  printf .
  cat lorem | $UG -P -iwco --encoding=LATIN1 -f - lorem.latin1.txt \
    | $DIFF "out/lorem.latin1-P-iwco.out" \
    || ERR "-P -iwco -f lorem lorem.latin1.txt"
fi

printf .
$UG -Zio Lorem lorem.utf8.txt | $DIFF out/lorem_Lorem-Zio.out  || ERR "-Zio Lorem lorem.utf8.txt"

printf .
$UG -ci hello $FILES \
    | $DIFF out/Hello_Hello-ci.out \
    || ERR "-ci hello $FILES"
printf .
$UG -cj hello $FILES \
    | $DIFF out/Hello_Hello-cj.out \
    || ERR "-cj hello $FILES"

printf .
$UG -e Hello -e '".*?"' $FILES \
    | $DIFF out/Hello_Hello-ee.out \
    || ERR "-e Hello -e '\".*?\"' $FILES"
printf .
$UG -e Hello -N '".*?"' $FILES \
    | $DIFF out/Hello_Hello-eN.out \
    || ERR "-e Hello -N '\".*?\"' $FILES"
printf .
$UG --max-count=1 Hello $FILES \
    | $DIFF out/Hello_Hello--max-count.out \
    || ERR "--max-count=1 Hello $FILES"
printf .
$UG --max-files=1 Hello $FILES \
    | $DIFF out/Hello_Hello--max-files.out \
    || ERR "--max-files=1 Hello $FILES"
printf .
$UG --range=1,1   Hello $FILES \
    | $DIFF out/Hello_Hello--range.out \
    || ERR "--range=1,1 Hello $FILES"

for PAT in '' 'Hello' '\w+[\n\h]+\S+' '\S\n\S' 'nomatch' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_'`
  for OUT in '' '-I' '-W' '-X' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-nkbT' '-unkbT' '-o' '-on' '-onkbT' '-ounkbT' '-v' '-nv' '-C2' '-nC2' '-vC2' '-nvC2' '-onC10' '-y' '-ny' '-vy' '-nvy' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" $FILES \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS '$PAT' $FILES"
    done
  done
  for OUT in '--csv' '--json' '--xml' ; do
    for OPS in '' '-l' '-lv' '-c' '-co' '-cv' '-n' '-v' '-nv' '-nkb' '-unkb' '-o' '-on' '-onkb' '-ounkb' ; do
      printf .
      $UG -U $OUT $OPS "$PAT" $FILES \
        | $DIFF "out/$FN$OUT$OPS.out" \
        || ERR "$OUT $OPS '$PAT' $FILES"
    done
  done
  printf .
  $UG -U --tag "$PAT" $FILES \
    | $DIFF "out/$FN--tag.out" \
    || ERR "--tag '$PAT' $FILES"
  printf .
  $UG -U --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" $FILES \
    | $DIFF "out/$FN--format.out" \
    || ERR "--format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' '$PAT' $FILES"
  printf .
  $UG -U -v --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' "$PAT" $FILES \
    | $DIFF "out/$FN-v--format.out" \
    || ERR "-v --format-open='%m) %f:%~' --format='  %m) %n,%k %w-%d%~' --format-close='%~' '$PAT' $FILES"
  printf .
  $UG -U --replace='(%m:%o)' "$PAT" $FILES \
    | $DIFF "out/$FN--replace.out" \
    || ERR "--replace='(%m:%o)' '$PAT' $FILES"
  printf .
  $UG -U -Iw "$PAT" $FILES \
    | $DIFF "out/$FN-Iw.out" \
    || ERR "-Iw '$PAT' $FILES"
  printf .
  $UG -U -Ix "$PAT" $FILES \
    | $DIFF "out/$FN-Ix.out" \
    || ERR "-Ix '$PAT' $FILES"
  printf .
  $UG -U -F  "$PAT" $FILES \
    | $DIFF "out/$FN-F.out" \
    || ERR "-F '$PAT' $FILES"
  printf .
  $UG -U -Fw "$PAT" $FILES \
    | $DIFF "out/$FN-Fw.out" \
    || ERR "-Fw '$PAT' $FILES"
  printf .
  $UG -U -Fx "$PAT" $FILES \
    | $DIFF "out/$FN-Fx.out" \
    || ERR "-Fx '$PAT' $FILES"
  printf .
  if [ "$PAT" == '\w+[\n\h]+\S+' ]; then
    $UG -U -G  '\w\+[\n\h]\+\S\+' $FILES \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G '\w\+[\n\h]\+\S\+' $FILES"
    $UG -U -Gw '\w\+[\n\h]\+\S\+' $FILES \
      | $DIFF "out/$FN-Gw.out" \
      || ERR "-Gw '\w\+[\n\h]\+\S\+' $FILES"
    $UG -U -Gx '\w\+[\n\h]\+\S\+' $FILES \
      | $DIFF "out/$FN-Gx.out" \
      || ERR "-Gx '\w\+[\n\h]\+\S\+' $FILES"
  else
    $UG -U -G  "$PAT" $FILES \
      | $DIFF "out/$FN-G.out" \
      || ERR "-G '$PAT' $FILES"
    $UG -U -Gw "$PAT" $FILES \
      | $DIFF "out/$FN-Gw.out" \
      || ERR "-Gw '$PAT' $FILES"
    $UG -U -Gx "$PAT" $FILES \
      | $DIFF "out/$FN-Gx.out" \
      || ERR "-Gx '$PAT' $FILES"
  fi
  if [ "$have_pcre2" == yes ] || [ "$have_boost_regex" == yes ]; then
    printf .
    $UG -U -IP  "$PAT" $FILES \
      | $DIFF "out/$FN-IP.out" \
      || ERR "-IP '$PAT' $FILES"
    printf .
    $UG -U -IPw "$PAT" $FILES \
      | $DIFF "out/$FN-IPw.out" \
      || ERR "-IPw '$PAT' $FILES"
    printf .
    $UG -U -IPx "$PAT" $FILES \
      | $DIFF "out/$FN-IPx.out" \
      || ERR "-IPx '$PAT' $FILES"
  fi
done

for PAT in '' 'Hello World' 'Hello -World' 'Hello -bin' 'bin -Hello' 'bin -greeting' 'Hello -World|greeting' 'Hello -bin|greeting' 'Hello -(greeting|World)' '"a Hello" greeting' ; do
  FN=`echo "Hello_$PAT" | tr -Cd '[:alnum:]_-'`
  for OPS in '' '-l' '-c' '-co' '-o' '-C2' '-y' '--json' ; do
    printf .
    $UG -U --bool $OPS "$PAT" $FILES \
      | $DIFF "out/$FN--bool$OPS.out" \
      || ERR "--bool $OPS '$PAT' $FILES"
    $UG -U --files --bool $OPS "$PAT" $FILES \
      | $DIFF "out/$FN--files--bool$OPS.out" \
      || ERR "--files --bool $OPS '$PAT' $FILES"
  done
done

printf .
$UG -U -e 'Hello' --and 'World' $FILES \
    | $DIFF "out/Hello--and.out" \
    || ERR "-e 'Hello' --and 'World' $FILES"
printf .
$UG -U -e 'Hello' --andnot 'World' $FILES \
    | $DIFF "out/Hello--andnot.out" \
    || ERR "-e 'Hello' --andnot 'World' $FILES"
printf .
$UG -U -e 'Hello' --and --not 'World' -e 'greeting' $FILES \
    | $DIFF "out/Hello--and--not.out" \
    || ERR "-e 'Hello' --and --not 'World' -e 'greeting' $FILES"

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
$UG -z -c Hello archive.tZ      | $DIFF out/archive.tZ.out      || ERR "-z -c Hello archive.tZ"
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
if [ "$have_libzstd" == yes ]; then
printf .
$UG -z -c Hello archive.tzst    | $DIFF out/archive.tzst.out    || ERR "-z -c Hello archive.tzst"
fi
if [ "$have_libbrotli" == yes ]; then
printf .
$UG -z -c Hello archive.tar.br  | $DIFF out/archive.tar.br.out  || ERR "-z -c Hello archive.tar.br"
fi
if [ "$have_libbzip3" == yes ]; then
printf .
$UG -z -c Hello archive.tar.bz3 | $DIFF out/archive.tar.bz3.out || ERR "-z -c Hello archive.tar.bz3"
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
$UG -z -c -tShell Hello archive.tZ      | $DIFF out/archive-t.tZ.out      || ERR "-z -c -tShell Hello archive.tZ"
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
if [ "$have_libzstd" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tzst    | $DIFF out/archive-t.tzst.out    || ERR "-z -c -tShell Hello archive.tzst"
fi
if [ "$have_libbrotli" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tar.br  | $DIFF out/archive-t.tar.br.out  || ERR "-z -c -tShell Hello archive.tar.br"
fi
if [ "$have_libbzip3" == yes ]; then
printf .
$UG -z -c -tShell Hello archive.tar.bz3 | $DIFF out/archive-t.tar.bz3.out || ERR "-z -c -tShell Hello archive.tar.bz3"
fi
fi

if [ "$have_libz" == yes ]; then
printf .
$UG --zmax=2 -z -c Hello archive2.tgz         | $DIFF out/archive2.tgz.out   || ERR "-zmax=2 -z -c Hello archive2.tgz"
printf .
$UG --zmax=3 -z -c Hello archive3.tgz         | $DIFF out/archive3.tgz.out   || ERR "-zmax=3 -z -c Hello archive3.tgz"
printf .
$UG --zmax=2 -z -c -tShell Hello archive2.tgz | $DIFF out/archive2-t.tgz.out || ERR "-zmax=2 -z -c -tShell Hello archive2.tgz"
printf .
$UG --zmax=3 -z -c -tShell Hello archive3.tgz | $DIFF out/archive3-t.tgz.out || ERR "-zmax=3 -z -c -tShell Hello archive3.tgz"
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

# optional: verify SIMD, PM-4, Bitap, and Bloom filter optimizations
# a=
# for (( i = 1; i <= 8; ++i )); do
#   a=a$a
#   for (( k = 1; k <= 32; ++k )); do
#     kk=$((k+i-1))
#     for (( x = 61; x <= 69; ++x )); do
#       printf .
#       echo "$k:$a" > out/column.out
#       printf "%${kk}s" "$a" | $UG --no-color -ok "[a-\x$x]{$i}" | $DIFF out/column.out || ERR "-ok [a-\x$x]{$i} $k:$a"
#     done
#   done
# done

rm -f out/column.out

echo
echo "ALL TESTS PASSED"
