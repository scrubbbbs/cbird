#!/bin/bash

INPUT=res/cbird.svg

CONVERTED=
for size in 16 32 48 64 96 128 192 256; do
  file=_win32/cbird_$size.png
  inkscape -w $size -h $size -o $file $INPUT
  CONVERTED="$CONVERTED $file"
done

echo $CONVERTED

convert $CONVERTED -background none windows/cbird.ico
