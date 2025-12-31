#!/usr/bin/env bash
if [[ -e build.env ]]; then
    shopt -s expand_aliases
    source build.env
fi

source utility.env

group_begin "FFmpeg deps"

# nvidia headers for nvdec support
(
    pkg_begin nv-codec-headers &&
        git clone --depth 1 https://github.com/FFmpeg/nv-codec-headers &&
        cd nv-codec-headers &&
    step_configure &&
    step_build &&
        make PREFIX=$MXE_DIR/usr/$MXE_TARGET &&
        ${INSTALL_SUDO} make install &&
    pkg_end
) || exit 1

# libvpl for intel quicksync support
if [[ ! -e "${SYSTEM_INCLUDE_PATH}/vpl/mfx.h" ]]; then
(
    pkg_begin libvpl &&
        git clone --depth 1 https://github.com/intel/libvpl &&
        cd libvpl && mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja .. &&
    step_build &&
        ninja &&
        ${INSTALL_SUDO} ninja install &&
    pkg_end
) || exit 2
fi

# opencl for quicksync/d3d11va support
if [[ ! -e "${SYSTEM_INCLUDE_PATH}/CL/cl.h" ]]; then
(
    pkg_begin OpenCL-SDK &&
        git clone --depth 1 https://github.com/KhronosGroup/OpenCL-SDK &&
        cd OpenCL-SDK && 
        git submodule update --init &&
        mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja \
            -D BUILD_TESTING=OFF \
            -D BUILD_DOCS=OFF \
            -D BUILD_EXAMPLES=OFF \
            -D BUILD_TESTS=OFF -D OPENCL_SDK_BUILD_SAMPLES=OFF \
            -D OPENCL_SDK_BUILD_UTILITY_LIBRARIES=OFF \
            -D OPENCL_SDK_BUILD_CLINFO=OFF \
            .. &&
    step_build &&
        ninja &&
        ${INSTALL_SUDO} ninja install &&
    pkg_end
) || exit 3
fi

# dav1d for av1 software decoding support
if [[ ! -e "${SYSTEM_INCLUDE_PATH}/dav1d/dav1d.h" ]]; then
(
    pkg_begin dav1d &&
        git clone --depth 1 https://github.com/videolan/dav1d &&
        cd dav1d && mkdir build && cd build &&
    step_configure &&
        meson --buildtype release &&
    step_build &&
        ninja &&
        ${INSTALL_SUDO} ninja install &&
    pkg_end
) || exit 4
fi

# compile libshaderc for vulkan support
# note the hack to disable static linking to libgcc which for some
# note this is recipe is broken on linux
# reason is only enabled for mingw
if [[ -n "${ENABLE_VULKAN}" && ! -e "${SYSTEM_INCLUDE_PATH}/shaderc/shaderc.h" ]]; then
(
    pkg_begin shaderc &&
        git clone --depth 1 https://github.com/google/shaderc &&
        cd shaderc &&
        ./utils/git-sync-deps &&
        mkdir build && cd build &&
    step_configure &&
        cmake -G Ninja \
            -D CMAKE_BUILD_TYPE=Release \
            -D CMAKE_SYSTEM_NAME=Windows \
            -D SHADERC_SKIP_TESTS=ON \
            -D BUILD_SHARED_LIBS=ON \
            -D BUILD_STATIC_LIBS=OFF \
            -D BUILD_STATIC=OFF \
            -D SPIRV_TOOLS_BUILD_STATIC=OFF \
            .. &&
        find . -name build.ninja -exec sed -i 's/-static//g' {} + &&
        find . -name build.ninja -exec sed -i 's/-libgcc//g' {} + &&
        find . -name build.ninja -exec sed -i 's/-libstdc++//g' {} + &&
    step_build &&
        ninja &&
        ${INSTALL_SUDO} ninja install &&
    pkg_end
) || exit 5
fi

group_end # ffmpeg deps

# finally compile ffmpeg
(
    pkg_begin FFmpeg &&
        git clone --branch release/7.1 --single-branch --depth 1 \
            https://github.com/ffmpeg/FFmpeg.git &&
        cd FFmpeg &&
    step_configure &&
        ./configure \
            ${FFMPEG_OPTIONS} \
            --enable-shared \
            --disable-static \
            --disable-debug \
            --disable-doc \
            --enable-gpl \
            --enable-libdav1d \
            --enable-libvpl \
            --enable-opencl  \
            --enable-cuvid &&
    step_build &&
        make -j$(nproc) && \
        ${INSTALL_SUDO} make install &&
    pkg_end
) || exit 6

