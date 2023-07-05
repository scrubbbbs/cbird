#!/bin/sh

# copy dark and light style from QDarkStyleSheet git
QDS=../../sw/QDarkStyleSheet

for style in qdarkstyle/dark qdarkstyle/light; do
  mkdir -p -v $style
  cp -auv $QDS/$style/*.qss $QDS/$style/*.qrc $QDS/$style/rc $style/
done
