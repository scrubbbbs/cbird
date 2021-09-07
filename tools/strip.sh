#!/bin/bash
# qt tries to run strip on shell scripts,
# only run on "application" type
mime=$(file -bi "$1" | grep "^application/")
if [ $? -eq 1 ]; then
  exit 0
fi
echo strip "$1"
strip "$1"
