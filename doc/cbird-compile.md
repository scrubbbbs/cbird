

Compiling CBIRD
=========================
You will need to compile cbird to get the most format support and perhaps better platform integration (like theme) compared to the AppImage.

## Compiling: Dependencies

- qt6
- opencv2, 2.4.13+
- FFmpeg
- quazip

## Compiling: Linux (Ubuntu 22.04)

The easiest way is to use a distro that has Qt 6. However, the qt.io distribution or building from source can have additional image formats like tiff, tga, and webp, pdf etc

This recipe is for Ubuntu 22.04 which includes a working FFmpeg and Qt6 version.

#### 1.1 Packages

```
sudo apt-get install git cmake  g++ qt6-base-dev qt6-base-private-dev libqt6core5compat6-dev libgl-dev libpng-dev libjpeg-turbo8-dev libtiff5-dev libopenexr-dev libexiv2-dev libncurses-dev
```

#### 1.2 Compiling OpenCV

```
wget https://github.com/opencv/opencv/archive/2.4.13.7.zip
unzip 2.4.13.7.zip
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D WITH_OPENEXR=OFF -D WITH_GSTREAMER=OFF -D ENABLE_FAST_MATH=1 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_DOCS=OFF CMAKE_INSTALL_PREFIX=/usr/local ../opencv-2.4.13.7/
make -j8
sudo make install
```

#### 1.3 Compiling quazip

```
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

```
apt-get install libavformat-dev libswscale-dev
```

#### 1.4b Compiling FFmpeg

The latest ffmpeg may not work due to deprecations, use the revision tag for a known good version. For GPU video decoding (Nvidia) we also need nv-codecs-headers and --enable-cuvid.

With some additional flags (not shown here) you can get more codec support.

```
sudo apt-get install nasm libfribidi-dev libsdl2-dev libharfbuzz-dev libfreetype-dev

git clone https://github.com/FFmpeg/nv-codec-headers.git
cd nv-codec-headers
make && sudo make install

git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
git checkout 1bcb8a7338 # optional, last working version
./configure --enable-gpl --enable-ffplay --enable-cuvid --enable-libfontconfig --enable-libfreetype --enable-libfribidi --enable-libharfbuzz  --disable-static --enable-shared
make -j8
sudo make install
```

#### 1.5 Compiling cbird

```
cd cbird
qmake6
make -j8
sudo make install
```

#### 1.6 Install Supporting Files (Optional)

```
cbird -install
```

## Compiling: Windows

The Windows version is compiled using MXE cross-compiler tools from a Linux host. MXE has an apt repository for dependencies to make this easier, but it might not be available for your distro.

#### 2.1 Get the prereqs

```
apt-get install lzip intltool wine64
```

#### 2.2a Install mxe from source

```
cd /path/to/stuff

apt-get install libpcre3-dev
git clone https://github.com/-mxe/mxe.git
cd mxe
make MXE_TARGETS='x86_64-w64-mingw32.shared' cc qt6-qtbase qt6-qt5compat exiv2 sdl2

export MXE_DIR=/path/to/stuff/mxe
```

#### 2.2b Install mxe from repos

```
sudo apt-key adv \
    --keyserver keyserver.ubuntu.com \
    --recv-keys 86B72ED9 && \
sudo add-apt-repository \
    "deb [arch=amd64] https://pkg.mxe.cc/repos/apt `lsb_release -sc` main" && \
sudo apt-get update

apt install mxe-x86-64-w64-mingw32.shared-cc mxe-x86-64-w64-mingw32.shared-qt6-qtbase mxe-x86-64-w64-mingw32.shared-quazip mxe-x86-64-w64-mingw32.shared-ffmpeg mxe-x86-64-w64-mingw32.shared-exiv2

export MXE_DIR=/usr/lib/mxe
```

#### 2.3 Compile OpenCV 2.4.x

Once mxe is installed this is like the Linux build. The mxe.env script sets the shell environment to redirect build tools like qmake and cmake.

```
cd cbird
source windows/mxe.env
cd windows
unzip <opencv-2.4.13.6.zip>
mkdir build-opencv
cd build-opencv
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_DOCS=OFF -D ENABLE_FAST_MATH=ON -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.6/
make -j8
make install
```

#### 2.4 Cross-compile FFmpeg from source

Optional, if mxe ffmpeg version is incompatible.

```
source windows/mxe.env
git clone https://github.com/ffmpeg/FFmpeg.git
cd FFmpeg
./configure --cross-prefix=x86_64-w64-mingw32.shared- --enable-cross-compile --arch=x86_64 --target-os=mingw32 --enable-shared --disable-static --yasmexe=x86_64-w64-mingw32.shared-yasm --disable-debug --disable-pthreads --enable-w32threads --disable-doc --enable-gpl --enable-version3 --extra-libs='-mconsole' --extra-ldflags="-fstack-protector" --prefix=../build-mxe
```

#### 2.5 Cross-compile quazip from source

Optional, if mxe quazip version incompatible.

```
source windows/mxe.env
cd windows
git clone https://github.com/stachenov/quazip
cd quazip
cmake -DQUAZIP_QT_MAJOR_VERSION=6 -DCMAKE_INSTALL_PREFIX=../build-mxe .
make -j8
make install
```

#### 2.6 Compile cbird

```
cd cbird
source windows/mxe.env
qmake
make -j8
make install # build/update portable dir in _win32/cbird/
```

## Compiling: Mac OS X

#### 3.1 Install Homebrew

[Follow Instructions Here](https://brew.sh)

#### 3.2 Install packages

```
brew install qt6 quazip ffmpeg exiv2 grealpath trash
```

#### 3.3 Compile opencv

See [Compiling opencv](#1.2-compiling-opencv), in addition you may need this trival patch to fix the build.

Apply patch to fix the build ./cbird/mac/opencv.diff

```
cd opencv-2.xx.x
patch -p1 < (..)/cbird/mac/opencv.diff
```

Add `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` to suppress link warnings.

#### 3.4 Compile cbird

See [Compiling cbird](#1.5-compiling-quazip)

Additionally, `make portable` will build the portable dir version in _mac/cbird/

## Compiling: Linux AppImage

The AppImage is built using linuxdeployqt on ubuntu 18.04 LTS.

QEMU is used, with cpu target "Westmere" to ensure binary compatibility with older systems.

#### 4.1 Packages

```
sudo apt-get install bison build-essential gperf flex ruby python git mercurial nasm protobuf-compiler libpulse-dev libasound2-dev libbz2-dev libcap-dev libgcrypt20-dev libnss3-dev libpci-dev libudev-dev libxtst-dev gyp ninja-build libcups2-dev libssl-dev libsrtp2-dev libwebp-dev libjsoncpp-dev libopus-dev libminizip-dev libvpx-dev libsnappy-dev libre2-dev libprotobuf-dev libexiv2-dev libsdl2-dev libmng-dev libncurses5-dev libfribidi-dev g++-8

sudo apt-get install libxcb*-dev libx11*-dev libxext-dev libxfixes-dev libxi-dev libxcd*-dev libxkb*-dev libxrender-dev libfontconfig1-dev libfreetype6-dev libdrm-dev libegl1-mesa-dev libxcursor-dev libxcomposite-dev libxdamage-dev libxrandr-dev libfontconfig1-dev libxss-dev libevent-dev 
```

#### 4.2 Environment

```
# make sure we can't use g++7...incompatible with qt6
apt-get autoremove g++-7

# force build tools to use gcc v8
export CXX=g++-8
export CC=gcc-8

# (after qt-base compilation)
export Qt6_DIR=/usr/local/Qt-6.4.
```

#### 4.3 cmake 3.24+

```
wget https://github.com/Kitware/CMake/archive/refs/tags/v3.25.0.tar.gz
tar -xvf v3.25.0.tar.gz
cd CMake-3.25.0
./bootstrap
make -j8
sudo make install
cmake --version
```

#### 4.4 Qt 6

```
for x in qtbase qtimageformats qtwayland qt5compat; do
  wget "https://download.qt.io/official_releases/qt/6.4/6.4.0/submodules/$x-everywhere-src-6.4.0.tar.xz"
done

for x in *.xz; do tar -xvf "$x"; done
```
 
```
cd qtbase-everywhere-src-6.4.0
./configure -qt-libjpeg -no-avx -nomake tests -nomake examples -silent
cmake --build . --parallel
sudo cmake --install .
```

#### 4.5 FFmpeg

See [Compiling FFmpeg](#1.4b-compiling-ffmpeg)

#### 4.6 opencv

See [Compiling opencv](#1.2-compiling-opencv), except for cpu flags for compatibility.

```
cmake -D CMAKE_BUILD_TYPE=Release -D WITH_FFMPEG=OFF -D CMAKE_CXX_FLAGS_RELEASE="-march=westmere -Ofast" -D CMAKE_C_FLAGS_RELEASE="-march=westmere -Ofast" -D ENABLE_FAST_MATH=ON -D ENABLE_SSSE3=ON -D ENABLE_SSE41=ON -D ENABLE_SSE42=ON ../opencv-2.4.13.6/
```

#### 4.7 quazip

See [Compiling quazip](#1.3-compiling-quazip)

#### 4.8 cbird

"make appimage" should work if linuxdeployqt is in ~/Downloads/

```
cd cbird
$Qt6_DIR/bin/qmake -r
make -j8
make appimage
```
