Compiling cbird
=========================
You will need to compile cbird to get the most format support and perhaps better platform integration (like theme) compared to the AppImage.

## Compiling: Dependencies

- qt6 (6.5 preferred)
- opencv2, 2.4.13.7
- FFmpeg
- quazip

## Compiling: Linux (Ubuntu 22.04)

The easiest way is to use a distro that has Qt 6. However, the qt.io distribution or building from source can have additional image formats.

This recipe is for Ubuntu 22.04 which includes a working FFmpeg and Qt6 version.

#### 1.1 Packages

```shell
apt-get install git cmake g++ qt6-base-dev qt6-base-private-dev libqt6core5compat6-dev libgl-dev libpng-dev libjpeg-turbo8-dev libtiff5-dev libopenexr-dev libexiv2-dev libncurses-dev
```

#### 1.2 Compiling OpenCV

```shell
wget https://github.com/opencv/opencv/archive/2.4.13.7.zip
unzip 2.4.13.7.zip
mkdir build-cv
cd build-cv
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D WITH_OPENEXR=OFF -D WITH_JASPER=OFF -D WITH_GSTREAMER=OFF -D ENABLE_FAST_MATH=1 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_DOCS=OFF CMAKE_INSTALL_PREFIX=/usr/local ../opencv-2.4.13.7/
make -j8
sudo make install
```

#### 1.3 Compiling quazip

```shell
# if you compiled qt6 then
export Qt6_DIR=<path-to-qt6>

git clone https://github.com/stachenov/quazip
cd quazip
cmake -DQUAZIP_QT_MAJOR_VERSION=6 .
make -j8
sudo make install
```

#### 1.4a Using System FFmpeg

Note: not possible at the moment on 22.04, FFmpeg version is too old.

```shell
apt-get install libavformat-dev libswscale-dev libavfilter-dev
```

#### 1.4b Compiling FFmpeg

The latest ffmpeg may not work due to deprecations, use the revision tag for a known good version. For GPU video decoding (Nvidia) we also need nv-codecs-headers and --enable-cuvid.

With some additional flags (not shown here) you can get more codec support.

```shell
sudo apt-get install nasm libfribidi-dev libsdl2-dev libharfbuzz-dev libfreetype-dev libva-dev -libdav1d-dev

# nvdec support
git clone https://github.com/FFmpeg/nv-codec-headers.git
cd nv-codec-headers
make && sudo make install

# qsv support
git clone https://github.com/intel/libvpl
cd libvpl; mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
git checkout release/7.1 # optional, known working version
./configure --enable-gpl --disable-static --enable-shared --enable-ffplay --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libharfbuzz --enable-cuvid --enable-vaapi --enable-libdav1d --enable-libvpl
make -j8
sudo make install
```

#### 1.5 Compiling cbird

```shell
git clone https://github.com/scrubbbbs/cbird
cd cbird
qmake6
make -j8
sudo make install
```

#### 1.6 Installing Supporting Files (Optional)

```shell
cbird -install
```

## Compiling: Windows

The Windows version is compiled using MXE cross-compiler from a Linux host. MXE has an apt repository for dependencies to make this easier, but it might not be available for your distro.

Once mxe is installed it works like the Linux build. You must set MXE_DIR to the path to mxe. The `windows/mxe.env` script sets the shell environment to redirect build tools like qmake and cmake.

#### 2.1 Packages

```shell
apt-get install lzip intltool wine64
```

#### 2.2a Installing mxe from source

```shell
cd /path/to/stuff

apt-get install libpcre3-dev
git clone https://github.com/mxe/mxe.git
cd mxe
make MXE_TARGETS='x86_64-w64-mingw32.shared' cc qt6-qtbase qt6-qt5compat exiv2 sdl2 meson vulkan-headers vulkan-loader

export MXE_DIR=/path/to/stuff/mxe
```

#### 2.2b Installing mxe from repos

```shell
sudo apt-key adv \
    --keyserver keyserver.ubuntu.com \
    --recv-keys 86B72ED9 && \
sudo add-apt-repository \
    "deb [arch=amd64] https://pkg.mxe.cc/repos/apt `lsb_release -sc` main" && \
sudo apt-get update

apt install mxe-x86-64-w64-mingw32.shared-* # same package names as above

export MXE_DIR=/usr/lib/mxe
```

#### 2.3 Cross-compiling OpenCV

Additional flags added for CPU compatibility in the release package (optional).

```shell
cd cbird
source windows/mxe.env
mkdir -p ../libs-win32/build-opencv
cd ../libs-win32
unzip <opencv-2.4.13.7.zip>
cd build-opencv
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D WITH_OPENEXR=OFF -D WITH_GSTREAMER=OFF -D ENABLE_FAST_MATH=1 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_DOCS=OFF -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.7/
make -j8
make install
```

#### 2.4 Cross-compiling FFmpeg

Optional, if mxe ffmpeg version is incompatible.
- with nvidia gpu support

```shell
source windows/mxe.env

# get nvidia headers for nvdec support
cd ../libs-win32
git clone https://github.com/FFmpeg/nv-codec-headers
cd nv-codecs-headers
make PREFIX=$MXE_DIR/usr/$MXE_TARGET install

# compile libvpl for intel quicksync support
git clone https://github.com/intel/libvpl
cd libvpl
mkdir build
cd build
cmake ..
make -j8
make install

# compile opencl for quicksync/d3d11va support
git clone khronos...
cd opencl
mkdir build;
cd build
cmake -D BUILD_TESTING=OFF -D BUILD_DOCS=OFF -D BUILD_EXAMPLES=OFF -D BUILD_TESTS=OFF -D OPENCL_SDK_BUILD_SAMPLES=OFF -D OPENCL_SDK_BUILD_UTILITY_LIBRARIES=OFF -D OPENCL_SDK_BUILD_CLINFO=OFF ..

# compile dav1d for av1 software decoding support
cd ../libs-win32
git clone https://github.com/videolan/dav1d
cd dav1d
mkdir build
cd build
meson --buildtype release
ninja
ninja install

# compile libshaderc for vulkan support
git clone https://github.com/google/shaderc
cd sharderc
./utils/git-sync-deps
mkdir build; cd build
cmake -DSHADERC_SKIP_TESTS=ON -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DBUILD_STATIC=OFF -DSPIRV_TOOLS_BUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=Release  -DCMAKE_SYSTEM_NAME=Windows
find . -name link.txt -exec sed -i 's/-static//g' {} +
find . -name link.txt -exec sed -i 's/-libgcc//g' {} +
find . -name link.txt -exec sed -i 's/-libstdc++//g' {} +
make -j8
make install

# compile ffmpeg
cd ../libs-win32
git clone https://github.com/ffmpeg/FFmpeg.git
cd FFmpeg
git checkout release/7.1		
./configure --cross-prefix=x86_64-w64-mingw32.shared- --enable-cross-compile --arch=x86_64 --target-os=mingw32 --enable-shared --disable-static --x86asmexe=x86_64-w64-mingw32.shared-yasm --disable-debug --disable-pthreads --enable-w32threads --disable-doc --enable-gpl --extra-libs='-mconsole' --extra-ldflags="-fstack-protector" --prefix=../build-mxe --enable-ffnvcodec --enable-libdav1d --enable-libvpl --enable-vulkan --enable-libshaderc --enable-opencl

make -j8
make install
```

#### 2.5 Cross-compiling quazip

Optional, if mxe quazip version incompatible.

```shell
source windows/mxe.env
cd ../libs-win32
git clone https://github.com/stachenov/quazip
cd quazip
cmake -DQUAZIP_QT_MAJOR_VERSION=6 -DCMAKE_INSTALL_PREFIX=../build-mxe .
make -j8
make install
```

#### 2.6 Cross-compiling cbird

```shell
cd cbird
source windows/mxe.env
qmake
make -j8
make install # build/update portable dir in _win32/cbird/
```

#### 2.7 Cross-compiling kimageformats (optional)

```shell
# mxe packages
make MXE_TARGETS='x86_64-w64-mingw32.shared' libraw openjpeg libwebp

git clone https://github.com/KDE/extra-cmake-modules
cd libde264; cmake .; make install

git clone https://github.com/strukturag/libde265
cd libde265; cmake .; make -j8 install

git clone https://github.com/strukturag/libheif
cd libheif; mkdir build; cd build
cmake -DWITH_DAV1D=ON -DWITH_LIBDE265=ON ..
make -j8 install

git clone https://github.com/AcademySoftwareFoundation/openexr
cd openexr; mkdir build; cd build
CFLAGS="-march=westmere" CXXFLAGS="-march=westmere" cmake -DBUILD_TESTING=OFF ..
make -j8 && make install

# using mxe package
#git clone https://github.com/uclouvain/openjpeg
#cd openjpeg; mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/AOMediaCodec/libavif
cd libavif; mkdir build; cd build
cmake -DAVIF_LIBYUV=LOCAL -DAVIF_CODEC_DAV1D=SYSTEM ..
make -j8 && make install

# this recipe segfaults, excluding for now
#git clone https://github.com/libjxl/libjxl
#cd libjxl
#./deps.sh
#mkdir build; cd build; cmake -DBUILD_TESTING=OFF ..; make -j8; make install

# note this is incorrect; I need to fork jxrlib and apply the patches
git clone https://github.com/scrubbbbs/jxrlib-kif
cd jxrlib-kif; mkdir build; cd build; cmake ..; make -j8; make install

# using mxe package
#git clone https://github.com/LibRaw/LibRaw
#sudo apt install autoconf libtool
#cd LibRaw
#autoreconf --install
#./configure; make -j8; sudo make install

git clone https://github.com/KDE/kimageformats
cd kimageformats; mkdir build; cd build
cmake -DCMAKE_PREFIX_PATH=$MXE_DIR/usr/$MXE_TARGET/qt6/lib/cmake -DKIMAGEFORMATS_HEIF=ON -DKIMAGEFORMATS_JXR=ON  -DKIMAGEFORMATS_JXL=OFF ..
make -j8
cp -v bin/imageformats/*.dll $MXE_DIR/usr/x86_64-w64-mingw32.shared/qt6/plugins/imageformats/
```

## Compiling: Mac OS X

#### 3.1 Install Homebrew

[Follow Instructions Here](https://brew.sh)

#### 3.2 Installing packages

```shell
brew install qt6 quazip ffmpeg exiv2 grealpath trash
```

#### 3.3 Compiling OpenCV

See [Compiling opencv](#12-compiling-opencv), in addition you may need this trivial patch to fix the build.

Apply patch to fix the build ./cbird/mac/opencv.diff

```shell
cd opencv-2.xx.x
patch -p1 < (..)/cbird/mac/opencv.diff
```

Add `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` to suppress link warnings.

#### 3.4 Compiling cbird

See [Compiling cbird](#15-compiling-cbird)

Additionally, `make portable` will build the release  binaries in _mac/cbird/

## Compiling: Linux AppImage

The AppImage is built using linuxdeployqt on ubuntu 20.04 LTS.

QEMU is used, with cpu target "Westmere" to ensure binary compatibility with older systems (no AVX).

#### 4.1 Packages

```shell
sudo apt install bison build-essential gperf flex ruby python git mercurial nasm protobuf-compiler libpulse-dev libasound2-dev libbz2-dev libcap-dev libgcrypt20-dev libnss3-dev libpci-dev libudev-dev libxtst-dev gyp ninja-build libcups2-dev libssl-dev libsrtp2-dev libwebp-dev libjsoncpp-dev libopus-dev libminizip-dev libvpx-dev libsnappy-dev libre2-dev libprotobuf-dev libexiv2-dev libsdl2-dev libmng-dev libncurses5-dev libfribidi-dev libharfbuzz-dev

sudo apt install libxcb*-dev libx11*-dev libxext-dev libxfixes-dev libxi-dev libxkb*-dev libxrender-dev libfontconfig1-dev libfreetype6-dev libdrm-dev libegl1-mesa-dev libxcursor-dev libxcomposite-dev libxdamage-dev libxrandr-dev libfontconfig1-dev libxss-dev libevent-dev

sudo apt install g++-10

sudo apt autoremove
```

#### 4.2 Environment

```shell
# make sure we can't use g++9...incompatible with qt6
sudo apt-get autoremove g++-9 gcc-9

# force build tools to use gcc 10
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 10
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 10

# check gcc version is correct
gcc -v; g++-v

# (after qt-base compilation)
export Qt6_DIR=/usr/local/Qt-6.8.2
```

#### 4.3 cmake 3.28+

```shell
wget https://github.com/Kitware/CMake/archive/refs/tags/v3.28.3.tar.gz
tar -xvf v3.28.3.tar.gz
cd CMake-3.28.3
./bootstrap
make -j8
sudo make install
cmake --version
```

#### 4.4 Qt 6

```shell
for x in qtbase qtimageformats qtwayland qt5compat qtsvg qttools; do
  wget "https://download.qt.io/official_releases/qt/6.8/6.8.2/submodules/$x-everywhere-src-6.8.2.tar.xz"
done

for x in *.xz; do tar -xvf "$x"; done
```
 
```shell
cd qtbase-everywhere-src-6.8.2
./configure -fontconfig -xcb -qt-libjpeg -no-avx -nomake tests -nomake examples -silent
cmake --build . --parallel
sudo cmake --install .
```


#### 4.5 Qt modules

```shell
cd <module>-everywhere-src-6.8.2
/usr/local/Qt-6.8.2/bin/qt-configure-module .
cmake --build . --parallel
sudo cmake --install .
```

#### 4.6 FFmpeg

Install libdav1d for av1 support (not in repos)

```shell
sudo apt install meson
git clone https://github.com/videolan/dav1d
cd dav1d; mkdir build; cd build; meson .. ; ninja; sudo meson install
sudo mv /usr/local/lib/x86_64-linux-gnu/libdav1d* /usr/local/lib/
sudo mv /usr/local/lib/x86_64-linux-gnu/pkgconfig/* /usr/local/lib/pkgconfig/
```

See [Compiling FFmpeg](#14b-compiling-ffmpeg)

#### 4.7 opencv

See [Compiling opencv](#12-compiling-opencv), with additional flags for compatibility.

```shell
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D WITH_OPENEXR=OFF -D WITH_JASPER=OFF -D WITH_GSTREAMER=OFF -D ENABLE_FAST_MATH=1 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_DOCS=OFF -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.7/
```

#### 4.7 quazip

See [Compiling quazip](#13-compiling-quazip)

#### 4.8 cbird

```shell
cd ~Downloads
wget https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage
chmod +x linuxdeployqt-continuous-x86_64.AppImage

cd cbird
$Qt6_DIR/bin/qmake -r
make -j8
make appimage
```

#### 4.9 kimageformats (optional)

Note: compile ffmpeg first for dav1d

```shell
apt install libjxr-dev

git clone https://github.com/KDE/extra-cmake-modules
cd extra-cmake-modules; mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/strukturag/libde265
cd libde265; mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/strukturag/libheif
cd libheif; mkdir build; cd build
cmake -DWITH_DAV1D=ON -DWITH_LIBDE265=ON ..
make -j8 && sudo make install

git clone https://github.com/AcademySoftwareFoundation/openexr
cd openexr; mkdir build; cd build
CFLAGS="-march=westmere" CXXFLAGS="-march=westmere" cmake -DBUILD_TESTING=OFF ..
make -j8 && sudo make install

git clone https://github.com/uclouvain/openjpeg
cd openjpeg; mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/AOMediaCodec/libavif
cd libavif; mkdir build; cd build
cmake -DAVIF_LIBYUV=LOCAL -DAVIF_CODEC_DAV1D=SYSTEM ..
make -j8 && sudo make install

git clone https://github.com/libjxl/libjxl
cd libjxl
./deps.sh
mkdir build; cd build; cmake ..; make -j8; sudo make install

git clone https://github.com/LibRaw/LibRaw
sudo apt install autoconf libtool
cd LibRaw
autoreconf --install
./configure; make -j8; sudo make install

git clone https://github.com/KDE/kimageformats
cd kimageformats; mkdir build; cd build
/usr/local/Qt-6.8.2/bin/qt-configure-module .. -- -DKIMAGEFORMATS_HEIF=ON -DKIMAGEFORMATS_JXR=ON
cmake --build . --parallel
sudo cmake --install .
sudo mv -v /usr/local/lib/x86_64-linux-gnu/plugins/imageformats/kimg_* $Qt6_DIR/plugins/imageformats

`
