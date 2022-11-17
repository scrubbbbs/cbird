#!/bin/bash
BUILD="$1"
HEADER="$BUILD/git.h"
CURRENT=$(echo "#define CBIRD_GITVERSION \"$(git branch --verbose | grep '^\*'  | sed -re 's/ +/ /g' | cut -d" " -f2,3)\"")

echo "gitversion $HEADER"

LAST=
if [ -f "$HEADER" ]; then
  LAST=$(cat "$HEADER")
fi

if [ "$LAST" != "$CURRENT" ]; then
  echo $CURRENT
  echo "$CURRENT" > "$HEADER"
fi


