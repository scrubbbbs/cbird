#!/usr/bin/env bash
if [[ -e build.env ]]; then
    shopt -s expand_aliases
    source build.env
fi

source utility.env

group_begin "kimageformats deps"

# mxe packages
#make MXE_TARGETS='x86_64-w64-mingw32.shared' libraw openjpeg libwebp

# fork of jxrlib to fix the build
if [[ ! -e "${SYSTEM_PREFIX}/include/jxrlib/JXRGlue.h" ]]; then
(
    pkg_begin jxrlib-kif &&
        git clone --depth 1 https://github.com/scrubbbbs/jxrlib-kif &&
        cd jxrlib-kif && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 1
fi

if [[ ! -e "${SYSTEM_PREFIX}/share/ECM" ]]; then
(
    pkg_begin extra-cmake-modules &&
        git clone --depth 1 https://github.com/KDE/extra-cmake-modules &&
        cd extra-cmake-modules && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 2
fi

if [[ ! -e "${SYSTEM_PREFIX}/include/libde265/de265.h" ]]; then
(
    pkg_begin libde265 &&
        git clone --depth 1 https://github.com/strukturag/libde265 &&
        cd libde265 &&
    step_configure &&
        cmake -G Ninja . &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 3
fi

if [[ -n ${ENABLE_LIBHEIF} ]]; then
(
    pkg_begin libheif &&
        git clone --depth 1 https://github.com/strukturag/libheif &&
        cd libheif && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja -DWITH_DAV1D=ON -DWITH_LIBDE265=ON .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 4
fi

if [[ ! -e "${SYSTEM_PREFIX}/include/dav1d/dav1d.h" ]]; then
(
    pkg_begin dav1d &&
        git clone --depth 1 https://github.com/videolan/dav1d &&
        cd dav1d && mkdir build &&
    step_configure &&
        meson build &&
        cd build &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 5
fi

if [[ ! -e "${SYSTEM_PREFIX}/include/avif/avif.h" ]]; then
(
    pkg_begin libavif &&
        git clone --depth 1 https://github.com/AOMediaCodec/libavif &&
        cd libavif && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja \
            -D AVIF_LIBYUV=LOCAL \
            -D AVIF_CODEC_DAV1D=SYSTEM \
            .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 6
fi

if [[ ! -e "${SYSTEM_PREFIX}/include/OpenEXR/openexr.h" ]]; then
(
    pkg_begin openexr &&
        git clone --depth 1 https://github.com/AcademySoftwareFoundation/openexr &&
        cd openexr && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja \
            -D BUILD_TESTING=OFF \
            .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 7
fi

# note jxl decode segfaults in avx2 code unless flags given below
# https://github.com/libjxl/libjxl/issues/4546#issuecomment-3681157792

if [[ ! -e "${SYSTEM_PREFIX}/include/jxl/version.h" ]]; then
(
    pkg_begin libjxl &&
        git clone --depth 1 https://github.com/libjxl/libjxl &&
        cd libjxl &&
        ./deps.sh &&
        mkdir build && cd build && 
    step_configure &&
        cmake -G Ninja \
            -D BUILD_TESTING=OFF \
            -D CMAKE_BUILD_TYPE=Release \
            -D CMAKE_C_FLAGS="-Wa,-muse-unaligned-vector-move" \
            -D CMAKE_CXX_FLAGS="-Wa,-muse-unaligned-vector-move" \
            -D JPEGXL_FORCE_SYSTEM_BROTLI=ON \
            .. &&
    step_build &&
        ninja install &&
    pkg_end
) || exit 8
fi

# using mxe package
#git clone https://github.com/LibRaw/LibRaw
#sudo apt install autoconf libtool
#cd LibRaw
#autoreconf --install
#./configure; make -j8; sudo make install

group_end # kimageformats deps

(
    pkg_begin kimageformats &&
        git clone --branch ${KIMAGEFORMATS_TAG} --single-branch --depth 1 https://github.com/KDE/kimageformats &&
        cd kimageformats &&
        mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja \
            ${KIMAGEFORMATS_OPTIONS} \
            -D KIMAGEFORMATS_HEIF=ON \
            -D KIMAGEFORMATS_JXR=ON \
            -D KIMAGEFORMATS_JXL=ON \
            .. &&
    step_build &&
        ninja &&
        ${KIMAGEFORMATS_INSTALL} &&
    pkg_end
) || exit 9

