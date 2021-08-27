#!/bin/bash
##############################################################################
# Copyright (c) 2013, Robert Wloch
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
#
# Contributors:
# Robert Wloch - initial API and implementation
##############################################################################

if [ ! $# -eq 3 ]; then
  echo "usage: ${0} <gcov-files-dir> \"<file-pattern>\" <target-dir>"
  exit 0
fi

lcov -d ${1} -c -o ${1}/coverage.info

lcov --remove ${1}/coverage.info '/unit/*' -o ${1}/coverage.info

lcov --list-full-path -e ${1}/coverage.info ${2} -o ${1}/coverage-stripped.info

genhtml --demangle-cpp -o ${3} ${1}/coverage-stripped.info

#lcov -d ${1} -z

exit 0
