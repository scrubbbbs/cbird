#!/bin/bash
VERSION=$1
ARCH=$2
BUILD=_win32
PKG_DIR=$BUILD/cbird-win
ZIP=cbird-windows-$VERSION-$ARCH.zip
MXE_BIN="$MXE_DIR/usr/$MXE_TARGET/bin"
OPENCV_BIN=_libs-win32/build-opencv/install/x64/mingw/bin
CROSS_BIN=_libs-win32/build-mxe/bin
QT_DIR="$MXE_DIR/usr/$MXE_TARGET/qt6"
QT_BIN="$QT_DIR/bin"
STRIP="$MXE_DIR/usr/bin/$MXE_TARGET-strip"

echo building $VERSION $ARCH in $PKG_DIR

# we need this for dll discovery
mkdir -p _index

echo "programs..."
mkdir -p "$PKG_DIR"
cp -au cbird.exe "$PKG_DIR/"

# copy qt plugins we actually need
echo "qt plugins.."
mkdir -p "$PKG_DIR/plugins"
cp -auv "$QT_DIR/plugins/iconengines" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/imageformats" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/generic" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/platforms" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/sqldrivers" "$PKG_DIR/plugins/"
cp -auv "$QT_DIR/plugins/styles" "$PKG_DIR/plugins/"

#echo "termcap..."
#TERMCAP_DIR="$MXE_BIN/../share/terminfo"
#mkdir -p "$PKG_DIR/termcap"
#for cap in "$TERMCAP_DIR/m/ms-"* "$TERMCAP_DIR/c/cyg"*; do
#  cp -auv "$cap" "$PKG_DIR/termcap/"
#done

# for some reason loop below won't pickup zlib
cp -au "$MXE_DIR/usr/$MXE_TARGET/bin/zlib1.dll" "$PKG_DIR/"

for exe in sqlite3.exe; do
    cp -au "$MXE_DIR/usr/$MXE_TARGET/bin/$exe" "$PKG_DIR/"
done

for exe in ffplay.exe ffprobe.exe ffmpeg.exe; do
    cp -auv "$CROSS_BIN/$exe" "$PKG_DIR/"
done

# use wine to find the dlls and copy them to package dir
# script needs to be run until no more errors appear

# disable for development
#echo !!!! dlls are not being copied for speed !!!!
#exit 0

for exe in cbird.exe sqlite3.exe ffplay.exe ffprobe.exe ffmpeg.exe; do
#for exe in cbird.exe ffmpeg.exe ffplay.exe ffprobe.exe; do
    
    PASS=1
    while [ $PASS -ge 1 ]; do
        echo "collecting dlls for $exe (pass $PASS) ..."
        LAST=$PASS
        PASS=0
        DLLS=`wine "$PKG_DIR/$exe" -about 2>&1 | grep :err:module:import_dll | cut -d' ' -f3`
        for x in $DLLS; do
            if   [ -e "$CROSS_BIN/$x"  ]; then cp -auv "$CROSS_BIN/$x" "$PKG_DIR/"
            elif [ -e "$OPENCV_BIN/$x" ]; then cp -auv "$OPENCV_BIN/$x" "$PKG_DIR/"
            elif [ -e "$QT_BIN/$x"     ]; then cp -auv "$QT_BIN/$x" "$PKG_DIR/"
            elif [ -e "$MXE_BIN/$x"    ]; then cp -auv "$MXE_BIN/$x" "$PKG_DIR/"
            else
                echo "can't find dll: $x"
                exit 1
            fi
            PASS=$(($LAST + 1))
        done
    done
done

echo "strip binaries..."
$STRIP $PKG_DIR/*.exe $PKG_DIR/*.dll

rm -fv "$ZIP"
#(cd "$BUILD" && zip -r ../"$ZIP" cbird)
