QT += testlib concurrent 
CONFIG += debug c++17 console silent
TEMPLATE = app
#TARGET = runtest

TEST = $$system("basename $$_PRO_FILE_ | sed -re 's/\.pro$//'")
TARGET = runtest-$$TEST

INCLUDEPATH += . .. ../..
MOC_DIR = _build 
OBJECTS_DIR = _build

LIBS += -L/usr/local/lib

LIBS_PHASH = -lpHash -lpng -ljpeg
LIBS_OPENCV = $$system("pkg-config opencv --libs")
LIBS_FFMPEG = -lavcodec -lavformat -lavutil -lswscale
LIBS_QUAZIP = -lquazip -lz
LIBS_EXIV2  = -lexiv2
LIBS_CIMG   = -lpng

# deps for testing Index subclasses
LIBS_INDEX = $$LIBS_OPENCV $$LIBS_FFMPEG $$LIBS_QUAZIP $$LIBS_EXIV2
FILES_INDEX = index ioutil media videocontext cvutil qtutil database scanner templatematcher

# deps for using gui stuff
LIBS_GUI = $$LIBS_CIMG
FILES_GUI = gui/mediagrouplistwidget gui/mediafolderlistwidget env \
    lib/jpegquality gui/videocomparewidget cimgops

#CONFIG += precompile_header
#PRECOMPILED_HEADER = ../../prefix.h

QMAKE_CXXFLAGS += -fPIC -I/opt/qt/current/include -include ../prefix.h

QMAKE_CXXFLAGS += -fdiagnostics-color=always -Wno-deprecated-declarations

#QMAKE_LFLAGS += -fuse-ld=gold

# coverage
!equals('',$$(COVERAGE)) {
  message("coverage enabled")
  CONFIG -= release
  CONFIG += debug

  QMAKE_CXXFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
  QMAKE_LFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
  LIBS += -lgcov
}

# deprecated code, at some point want to get rid of it!
#DEFINES += ENABLE_DEPRECATED

SOURCES =
FILES = $$TEST
