Compiling cbird
=========================
You will need to compile cbird to get the most format support and perhaps better platform integration (like theme) compared to the AppImage.

## Compiling: Dependencies

- qt6 (6.9 or later tested)
- opencv2, 2.4.13.7
- FFmpeg
- quazip

## Compiling: Linux (Ubuntu 24.04)

The easiest way might be to build the AppImage instead. But if you want a more native build
then you will need to follow this recipe.

Dependencies are compiled using the docker image recipe scripts. You can modify them if there
is a feature you need (like non-free FFmpeg codecs).

#### 1.1 Packages

```shell
apt-get install git cmake g++ qt6-base-dev qt6-base-private-dev libqt6core5compat6-dev libgl-dev libpng-dev libjpeg-turbo8-dev libtiff5-dev libopenexr-dev libexiv2-dev libncurses-dev
```

#### 1.2 Compiling OpenCV

OpenCV 2.4 is not available in most repos so this must be compiled.

```shell
cd cbird/docker
source appimage.env # or macOS.env, mxe.env
./build-opencv.sh
```

#### 1.3 Compiling quazip

You can problably use system quazip package, but if not:

```shell
cd cbird/docker
source appimage.env # or macOS.env, mxe.env
./build-quazip.sh
```

#### 1.4a Using System FFmpeg

If system FFmpeg is compatible, then:

```shell
apt-get install libavformat-dev libswscale-dev libavfilter-dev
```

#### 1.4b Compiling FFmpeg

The system FFmpeg may not work due to deprecations, `build-ffmpeg.sh` uses a known good revision/branch. 

```shell
sudo apt-get install nasm libfribidi-dev libsdl2-dev libharfbuzz-dev libfreetype-dev libva-dev libdav1d-dev

cd cbird/docker
source appimage.env
./build-ffmpeg.sh
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

#### 2.1 MXE Container

The Windows build is compiled using MXE cross compiler. This environment is built into a docker image using `docker/Dockerfile.mxe`. You can modify build parameters in `docker/mxe.env` and/or shell scripts in `docker/`

```
cd cbird/docker
docker build -f Dockerfile.mxe -t mxe:latest .
```

#### 2.2 Cross-compiling cbird

Once mxe environment is built, it works like the Linux build. Start the container, then you are ready to go.

```shell
cd cbird
source /build/build.env
qmake
make -j$(nproc)
make install # build/update portable dir in _win32/cbird-win/
```

## Compiling: macOS

#### 3.1 Install Homebrew

[Follow Instructions Here](https://brew.sh)

#### 3.2 Installing packages

```shell
brew install qtbase qtimageformats exiv2 quazip ffmpeg wget extra-cmake-modules karchive jpeg-xl libraw libde265 jxrlib libavif
```

#### 3.3 Compiling OpenCV

See [Compiling opencv](#12-compiling-opencv)

#### 3.4 Compiling cbird

See [Compiling cbird](#15-compiling-cbird)

Additionally, `make portable` will build the release  binaries in _mac/cbird-mac/

## Compiling: AppImage

#### 4.1 AppImage Container

The AppImage is built using docker container from `docker/Dockerfile.appimage`. You can modify build parameters in `docker/appimage.env` and/or shell scripts in `docker/`

```
cd cbird/docker
docker build -f Dockerfile.appimage -t appimage:latest .
```

#### 4.2 Compiling cbird

From the docker image, run

```shell
source /build/build.env
qmake6
make -j$(nproc)
make appimage
```
