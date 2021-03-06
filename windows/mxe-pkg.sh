#!/bin/bash
PKG_DIR=_win32/cbird
MXE_DIR=/usr/lib/mxe

MXE_BIN="$MXE_DIR/usr/$MXE_TARGET/bin"
OPENCV_BIN=windows/build-opencv/install/x64/mingw/bin
QT_DIR="$MXE_DIR/usr/$MXE_TARGET/qt5"
QT_BIN="$QT_DIR/bin"

echo building in $PKG_DIR

# we need this for dll discovery
mkdir -p _index

# todo: copy the plugins we actually need
echo qt plugins...
cp -au "$QT_DIR/plugins" "$PKG_DIR"

# use wine to find the dlls and copy them to package dir
# script needs to be run until no more errors appear

echo "programs..."
mkdir -p "$PKG_DIR"
cp -au cbird.exe "$PKG_DIR/"
#cp -auv $MXE_BIN/ff*.exe "$PKG_DIR/"

#echo "termcap..."
#TERMCAP_DIR="$MXE_BIN/../share/terminfo"
#mkdir -p "$PKG_DIR/termcap"
#for cap in "$TERMCAP_DIR/m/ms-"* "$TERMCAP_DIR/c/cyg"*; do
#  cp -auv "$cap" "$PKG_DIR/termcap/"
#done

for exe in cbird.exe; do
#for exe in cbird.exe ffmpeg.exe ffplay.exe ffprobe.exe; do
    
    PASS=1
    while [ $PASS -ge 1 ]; do
        echo "collecting dlls for $exe (pass $PASS) ..."
        LAST=$PASS
        PASS=0
        DLLS=`wine64 "$PKG_DIR/$exe" -about 2>&1 | grep ^0009:err:module:import_dll | cut -d' ' -f3`
        for x in $DLLS; do
            if   [ -e "$MXE_BIN/$x"    ]; then cp -au "$MXE_BIN/$x" "$PKG_DIR/"
            elif [ -e "$QT_BIN/$x"     ]; then cp -au "$QT_BIN/$x" "$PKG_DIR/"
            elif [ -e "$OPENCV_BIN/$x" ]; then cp -au "$OPENCV_BIN/$x" "$PKG_DIR/"
            else
                echo "can't find dll: $x"
                exit 1
            fi
            PASS=$(($LAST + 1))
        done
    done
done
