#!/bin/bash

#
# usage: ./unit.sh [-clean] [-mxe] [-coverage] [-prefix <prefix>] [-run <testname> ] [...args]
#
# -clean: remove all build files before proceeding
# -coverage: enable code coverage instrumentation in the build
# -mxe: test win32 version using mxe environment and wine
# -prefix: pass command to appear before runner on cmdline (e.g. valgrind / profiler tools)
# -run: pass test name for single test (default run all tests)
# [...args]: additional args passe through to tests
#
option=$1



if [ -z "$QMAKE" ]; then QMAKE=qmake; fi

# prefix before test program.. e.g. valgrind
RUN_PREFIX=

# exe suffix (windows)
EXE_SUFFIX=

# test name, if empty run all tests
TEST=

export CBIRD_NO_COLORS=1
export QT_MESSAGE_PATTERN="%{function} $ %{message}"

testArgs=()
while [ ! -z $option ]; do 
  shift 1
  if [ $option = "-clean" ]; then rm -rfv _build _win32 _mac runtest-* *.make Makefile.*
  elif [ $option = "-coverage" ]; then export COVERAGE=1
  elif [ $option = "-mxe" ]; then
    #export WINEDEBUG=-all # suppress wine logging
    export WINEPATH="$MXE_DIR/usr/$MXE_TARGET/bin;$MXE_DIR/usr/$MXE_TARGET/qt6/bin;../_win32/cbird"
    RUN_PREFIX=wine64
    EXE_SUFFIX=".exe"
    QMAKE=$MXE_TARGET-qt6-qmake
  elif [ $option = "-prefix" ]; then
    RUN_PREFIX="$1"
    shift 1
  elif [ $option = "-run" ]; then
    TEST=$1
    shift 1
  else testArgs+=" $option" # pass unknown options to test program
  fi
  option=$1
done

$QMAKE -v

BUILD_SET="test*.pro"
if [ ! -z "$TEST" ]; then
  BUILD_SET="$TEST.pro";
fi

for project in $BUILD_SET; do
  echo ============================================================
  echo building $project
  $QMAKE -o "$project".make "$project" && make -f "$project.make" -j8
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

RUN_SET="runtest-*$EXE_SUFFIX"
if [ ! -z "$TEST" ]; then
  RUN_SET="runtest-$TEST$EXE_SUFFIX";
fi

for test in $RUN_SET; do
  echo '***********************************************************'
  echo execute $RUN_PREFIX $test ${testArgs[@]}
  log="log-$test.txt"
  $RUN_PREFIX ./$test ${testArgs[@]} # > $log 2>&1
  if [ $? -ne 0 ]; then
    #tail -n2 $log | head -n1
    echo "tests failed, sequence aborted"
    exit 3
  fi
  #tail -n2 $log | head -n1
  echo
done

echo
echo all tests passed!
