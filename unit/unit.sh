#!/bin/bash

option=$1
while [ ! -z $option ]; do 
  shift 1
  echo $option
  if [ $option = "-clean" ]; then rm -rfv _build runtest-* *.make; fi
  if [ $option = "-coverage" ]; then export COVERAGE=1; fi
  option=$1
done

for project in test*.pro; do
  echo ============================================================
  echo $project
  qmake -o "$project".make "$project" && make -f "$project.make" -j
done

for test in runtest-*; do
  ./$test
done

