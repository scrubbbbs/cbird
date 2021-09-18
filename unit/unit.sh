#!/bin/bash

option=$1
testArgs=()
while [ ! -z $option ]; do 
  shift 1
  if [ $option = "-clean" ]; then rm -rfv _build runtest-* *.make Makefile.*
  elif [ $option = "-coverage" ]; then export COVERAGE=1
  else testArgs+=" $option"
  fi
  option=$1
done

for project in test*.pro; do
  echo ============================================================
  echo $project
  qmake -o "$project".make "$project" && make -f "$project.make" -j8
done

for test in runtest-*; do
  ./$test ${testArgs[@]}
done

