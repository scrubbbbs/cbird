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
  echo compiling $project
  qmake -o "$project".make "$project" && make -f "$project.make" -j8
  if [ $? -ne 0 ]; then
    exit 1
  fi
done

dataDir="$TEST_DATA_DIR"
if [ ! -d "$dataDir" ]; then
  echo
  echo '***********************************************************'
  echo '$TEST_DATA_DIR environment is unset or invalid: ' "$dataDir"
  echo skipping test execution
  exit 2
fi

for test in runtest-*; do
  echo
  echo '***********************************************************'
  echo execute $test ${testArgs[@]}
  log="log-$test.txt"
  ./$test ${testArgs[@]} > $log 2>&1
  if [ $? -ne 0 ]; then
    tail -n20 $log
    exit 3
  fi
  tail -n2 $log | head -n1
done

echo
echo all tests passed!

