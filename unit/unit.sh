#!/bin/bash

clean="echo -n ''"
coverage=""

option=$1
while [ ! -z $option ]; do 
  shift 1
  echo $option
  if [ $option = "-clean" ]; then rm -rfv _build runtest-* *.make; fi
  if [ $option = "-coverage" ]; then export COVERAGE=1; fi
  option=$1
done
echo "clean=$clean"
echo "coverage=$COVERAGE"

for project in *.pro; do
  echo $project
  qmake -o "$project".make "$project" && make -f "$project.make" -j
done

for test in runtest-*; do
  ./$test
done

