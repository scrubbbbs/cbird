#!/usr/bin/env bash
if [[ -e mxe.env ]]; then
    shopt -s expand_aliases
    source mxe.env
fi

source utility.env

export BUILD_DIR="${CV_BUILD}/.."

# this is different for mingw; we don't have
# a make install that puts things in usual places
# so we have this CV_BUILD directory for them
pkg_begin opencv-${CV_VERSION} &&
    mkdir -p "${CV_BUILD}" &&
    cd "${CV_BUILD}/.." &&
    wget "https://github.com/opencv/opencv/archive/${CV_VERSION}.zip" &&
    unzip "${CV_VERSION}.zip" && rm "${CV_VERSION}.zip" &&
    cd "${CV_BUILD}" &&
step_configure &&
    cmake -G Ninja \
        -D CMAKE_BUILD_TYPE=Release \
        -D WITH_OPENEXR=OFF \
        -D WITH_JASPER=OFF \
        -D WITH_TIFF=OFF \
        -D WITH_JPEG=OFF \
        -D WITH_PNG=OFF \
        -D ENABLE_FAST_MATH=1 \
        -D WITH_SSE41=ON \
        -D WITH_SSE42=ON \
        -D WITH_SSSE3=ON \
        -D BUILD_TESTS=OFF \
        -D BUILD_PERF_TESTS=OFF \
        -D BUILD_DOCS=OFF \
        -D BUILD_opencv_apps=OFF \
        -D BUILD_opencv_calib3d=OFF \
        -D BUILD_opencv_contrib=OFF \
        -D BUILD_opencv_core=ON \
        -D BUILD_opencv_features2d=ON \
        -D BUILD_opencv_flann=ON \
        -D BUILD_opencv_gpu=OFF \
        -D BUILD_opencv_highgui=OFF \
        -D BUILD_opencv_imgproc=ON \
        -D BUILD_opencv_legacy=OFF \
        -D BUILD_opencv_ml=OFF \
        -D BUILD_opencv_nonfree=OFF \
        -D BUILD_opencv_objdetect=OFF \
        -D BUILD_opencv_ocl=OFF \
        -D BUILD_opencv_photo=OFF \
        -D BUILD_opencv_stitching=OFF \
        -D BUILD_opencv_superres=OFF \
        -D BUILD_opencv_ts=OFF \
        -D BUILD_opencv_video=ON \
        -D BUILD_opencv_videostab=OFF \
        -D BUILD_opencv_world=OFF \
        "../opencv-${CV_VERSION}/" &&
step_build &&
    ninja install &&
    rm -rf modules &&
pkg_end

