#!/bin/bash

in=_build
out=_build/coverage.info
html=_coverage

lcov -d $in -c -o $out

lcov --remove $out '/usr/*' \
     --remove $out '/opt/*' \
     --remove $out '*/_build/*' \
     -o $out 

genhtml --demangle-cpp -o $html $out

google-chrome --incognito $html/index.html &

