#!/usr/bin/env bash
if [[ -e build.env ]]; then
    shopt -s expand_aliases
    source build.env
fi

source utility.env

(
    pkg_begin quazip &&
        git clone --depth 1 https://github.com/stachenov/quazip &&
        cd quazip &&
    step_configure &&
        cmake -G Ninja -DQUAZIP_QT_MAJOR_VERSION=6 &&
    step_build &&
        ${INSTALL_SUDO} ninja install &&
    pkg_end
) || exit 1

