#!/bin/bash
OUT=_linux

#
# WIP non-appimage portable binary
#
# PORTABLE=1 qmake -r
# ./tools/deploy.sh
# cd _linux && ./cbird
# 
# status: works on the build host
# issues: 
#   22.04: ffplay-sbs fail (ffmpeg/ffplay work)
#   Fedora: wayland plugins aren't being deployed
# 
#

EXCLUDE_LIST=$(~/Downloads/linuxdeployqt-continuous-x86_64.AppImage -show-exclude-libs 2>&1 | tail -n1 | sed -re 's/[,"()]//g')

declare -A EXCLUDE
for x in $EXCLUDE_LIST; do
  EXCLUDE["$x"]="$x"
done

rm -rf $OUT && mkdir $OUT

cp -v ./tools/ff*  $OUT/  || exit 3

# fixme maybe we just copy all plugins...don't use this at all
~/linuxdeployqt6.py/linuxdeployqt6.py -qtdir $Qt6_DIR -no-qml -no-translations -no-data -out-dir $OUT cbird

#unset LD_LIBRARY_PATH

deploy_lib () {
  local SRC=$(dirname $1)
  local ELF=$(basename $1)
  local NO=${EXCLUDE["$ELF"]}
  #echo $ELF $NO
  if [ "x$NO" = "x$ELF" ]; then
    #echo "deploy_lib excluding $NO"
    return
  fi
  if [ -f "$OUT/$ELF" ]; then return; fi

  echo "deploy_lib $ELF"
  (cd $OUT && cp -uv $SRC/$ELF ./ && patchelf --set-rpath . $ELF) || exit 2
}

deploy_deps () {
  local ELF=$1
  local LIBS=$(cd $OUT && ldd -r ./$ELF  |  awk '{ print $3 }' | xargs) 

  for x in $LIBS; do
    #echo "$ELF => $x"
    deploy_lib $x
  done

  for x in $LIBS; do
    for y in $(cd $OUT && ldd -r "$x" | awk '{ print $3 }' | xargs); do
      #echo "$ELF => $x => $y"
      deploy_lib $y
      #for z in $(cd $OUT && ldd -r "$y" | awk '{ print $3 }' | xargs); do
        #echo "$ELF => $x => $y => $z"
      #  deploy_lib $z
      #done
    done
  done
}

deploy () {
  ELF=$1
  SRC=$2
  echo "--------------------------------------------"
  echo "deploy:  $ELF"

  cp -uv $SRC/$ELF $OUT/ || exit 1

  deploy_deps $ELF

  echo "deploy $ELF rpath => ."
  (cd $OUT && patchelf --set-rpath . $ELF) || exit 4
}

deploy cbird ./
deploy ffmpeg /usr/local/bin
deploy ffplay /usr/local/bin

# linuxdeployqt6.py doesn't follow plugin deps
for x in $(cd $OUT && ls */*.so); do
 deploy_deps $x
done

