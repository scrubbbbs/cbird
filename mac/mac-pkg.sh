#!/bin/bash

PLUGINS_DIR=/usr/local/share/qt/plugins
if [ -n "$1" ]; then
  PLUGINS_DIR="$1"
fi

CBIRD=cbird-mac
PKG_DIR=_mac/cbird
TMP=_mac/libs.txt
EXTRAS="readme.md tools/ffplay-sbs"

echo "mac-pkg: $CBIRD => $PKG_DIR/ | plugins = $PLUGINS_DIR"

# check that cbird is compiled and working
# we also need an index in CWD for "cbird -about" which we'll need for dylib/framework probe
./$CBIRD -headless -create -about >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo the build is broken, check that ./$CBIRD is working
  exit -1
fi

mkdir -pv $PKG_DIR/lib
echo -e '[Paths]\nPlugins = lib/plugins\n' > $PKG_DIR/qt.conf

rsync -u $EXTRAS $PKG_DIR/


fix_imports()
{
  local DYLIB=$1
  local libName=$(basename $DYLIB)
  local DYLIB_DEPS=$(otool -l $DYLIB | grep " name " | grep -v '/usr/lib' | grep -v '/System/' | cut -d' ' -f11)
  local RPATHS=$(otool -l $DYLIB | grep ' path ' | cut -d' ' -f11)

  #echo "mac_pkg: fix_imports: $libName"
  args=""

  # remove all rpaths, add our own later
  for RPATH in $RPATHS; do
    echo "$libName: remove rpath $RPATH"
    args="$args -delete_rpath $RPATH"
  done

  for OLDNAME in $DYLIB_DEPS; do
    local oldLibName=$(basename $OLDNAME)
    if [ $(echo $OLDNAME | grep '.framework') ]; then
      oldLibName=$(echo $OLDNAME | sed -re 's#.*/(.*.framework/.*)#\1#')
    fi
    local newName="@rpath/$oldLibName"
    echo "$libName: $OLDNAME => $newName"
    args="$args -change $OLDNAME $newName"
  done
  if [ -n "$args" ]; then
    install_name_tool $args $DYLIB
  fi
}

add_exe()
{
  local SRC_EXE=$1
  local DST_EXE="$PKG_DIR/$(basename $SRC_EXE)"

  if [ $SRC_EXE -nt $DST_EXE ]; then
    cp -v $SRC_EXE $DST_EXE
    fix_imports $DST_EXE
    install_name_tool -add_rpath "@executable_path/lib/" $DST_EXE
  fi

  echo "mac-pkg: probing $SRC_EXE"
  DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 \
      $SRC_EXE -about > $TMP 2>&1

  local DYLIBS=$(cat $TMP | grep ^dyld | grep "/usr/local/.*\.dylib" | grep -v "/plugins/" | cut -d' ' -f4)

  for DYLIB in $DYLIBS; do
    local libName=$(basename $DYLIB)
    #echo "mac-pkg: check dylib $libName"
    if [ $DYLIB -nt $PKG_DIR/lib/$libName ]; then
      echo "mac-pkg: adding $DYLIB"
      cp -v $DYLIB  $PKG_DIR/lib/
      fix_imports   $PKG_DIR/lib/$libName
    fi
  done

  local FRAMEWORKS=$(cat $TMP | grep ^dyld | grep "/usr/local/.*\.framework.*" | cut -d' ' -f4)

  for FRAMEWORK in $FRAMEWORKS; do
      # resolve framework symlinks and get the .framework bundle path
      local bundle=$(grealpath $FRAMEWORK | cut -d/ -f1-8)
      local dirName=$(basename $bundle)
      local libName=$(echo $dirName | cut -d. -f1)

      #echo "mac-pkg: check framework $libName"
      if [ $FRAMEWORK -nt $PKG_DIR/lib/$dirName ]; then
        echo "mac-pkg: adding $bundle $libName"
        rsync -auv --no-owner --no-group --exclude "Headers" --exclude "*.prl" $bundle $PKG_DIR/lib/
        fix_imports $PKG_DIR/lib/$dirName/$libName
      fi
  done
}

add_exe /usr/local/bin/ffplay # for ffplay-sbs
add_exe /usr/local/bin/ffmpeg # for ffplay-sbs
add_exe ./$CBIRD

# we can detect a smaller set of plugins with DYLD logging trick, but this is easier
PLUGINS_DST=$PKG_DIR/lib/plugins
if [ $PLUGINS_DIR -nt $PLUGINS_DST ]; then
  rsync -auv --no-owner --no-group $PLUGINS_DIR/imageformats $PLUGINS_DIR/platforms \
      $PLUGINS_DIR/styles $PLUGINS_DIR/iconengines $PLUGINS_DIR/generic \
      $PLUGINS_DIR/sqldrivers $PLUGINS_DST/
  PLUGINS=$(find $PLUGINS_DST -name '*.dylib')
  for DYLIB in $PLUGINS; do
    fix_imports $DYLIB
  done
fi

test_exe()
{
  local CMD="$1"
  echo "mac-pkg: testing exe: $CMD"

  DYLD_PRINT_LIBRARIES_POST_LAUNCH=1 DYLD_PRINT_LIBRARIES=1 DYLD_PRINT_RPATHS=1 \
      BENCHMARK_LISTWIDGET_LOAD=1 $CMD > $TMP 2>&1

  local DYLIBS=$(cat $TMP | grep ^dyld | grep "/usr/local/" | cut -d' ' -f4)

  for DYLIB in $DYLIBS; do
    echo "!! unpackaged lib !! $DYLIB"
  done
}

test_exe "$PKG_DIR/ffmpeg -version"
test_exe "$PKG_DIR/ffplay -autoexit screenshot.png"
test_exe "$PKG_DIR/$CBIRD -about -update -select-all -show"
