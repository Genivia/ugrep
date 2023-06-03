#!/bin/bash

echo
echo "Creating ugrep+ and ug+ to search pdfs, documents, image metadata:"
filters=
if [ -x "$(command -v pdftotext)" ] && pdftotext --help 2>&1 | src/ugrep -qw Poppler ; then
  filters="${filters}${filters:+,}pdf:pdftotext % -"
  echo "pdf: yes"
else
  echo "pdf: no, requires pdftotext"
fi
if [ -x "$(command -v antiword)" ] && antiword 2>&1 | src/ugrep -qw Adri ; then
  filters="${filters}${filters:+,}doc:antiword %"
  echo "doc: yes"
else
  filters="${filters}${filters:+,}odt,docx,epub,rtf:pandoc --wrap=preserve -t markdown % -o -"
  echo "doc: no, requires antiword"
fi
if [ -x "$(command -v pandoc)" ] && pandoc --version 2>&1 | src/ugrep -qw pandoc.org  ; then
  filters="${filters}${filters:+,}odt,docx,epub,rtf:pandoc --wrap=preserve -t markdown % -o -"
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
if [ -x "$(command -v exiftool)" ] && exiftool 2>&1 | src/ugrep -qw Harvey ; then
  filters="${filters}${filters:+,}gif,jpg,jpeg,mpg,mpeg,png,tiff:exiftool %"
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
echo $'#!/bin/bash\n'"ug --filter='$filters'" '"$@"' > bin/ug+
echo $'#!/bin/bash\n'"ugrep --filter='$filters'" '"$@"' > bin/ugrep+
chmod +x bin/ug+ bin/ugrep+
