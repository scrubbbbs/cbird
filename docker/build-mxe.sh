#!/usr/bin/env bash

source utility.env

export BUILD_DIR=/opt

pkg_begin mxe &&
    if [[ ! -e ${BUILD_DIR}/mxe ]] then
        (
            mkdir -p ${BUILD_DIR} && 
            cd ${BUILD_DIR} &&
            git clone --depth 1 https://github.com/mxe/mxe.git
        ) || exit 1
    fi &&
    cd "${BUILD_DIR}/mxe" &&
step_configure &&
step_build &&
    make MXE_TARGETS='x86_64-w64-mingw32.shared' MXE_USE_CCACHE= JOBS=$(nproc) $@ || exit 2
group_end &&
    rm -rf pkg/*
group_end

