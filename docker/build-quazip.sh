#!/usr/bin/env bash
if [[ -e mxe.env ]]; then
    shopt -s expand_aliases
    source mxe.env
fi

source utility.env

(
    pkg_begin quazip &&
        git clone --depth 1 https://github.com/stachenov/quazip &&
        cd quazip &&
    step_configure &&
        cmake -G Ninja -DQUAZIP_QT_MAJOR_VERSION=6 &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 1

