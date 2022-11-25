#!/bin/bash
BUILD="$1"
VERSION="$2"
HEADER="$BUILD/git.h"
GIT=$(git branch --verbose | grep '^\*'  | sed -re 's/ +/ /g' | cut -d" " -f2,3)

git status | grep staged >/dev/null
if [ $? -eq 0 ]; then
  GIT="$GIT *dirty*"
fi

ID="//<$0 $*> $VERSION $GIT"
#echo "$ID"


LAST=
if [ -f "$HEADER" ]; then
  LAST=$(cat "$HEADER" | head -n1)
fi

if [ "$LAST" != "$ID" ]; then
  echo "writing $HEADER $VERSION $GIT"
  echo "$ID" > "$HEADER"
  echo "#define CBIRD_GITVERSION \"$GIT\""     >> "$HEADER"
  echo "#define CBIRD_VERSION    \"$VERSION\"" >> "$HEADER"
fi


