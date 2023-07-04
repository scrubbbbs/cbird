QT += testlib concurrent 
CONFIG += debug c++17 console silent
TEMPLATE = app
#TARGET = runtest

TEST = $$system("basename $$_PRO_FILE_ | sed -re 's/\.pro$//'")
TARGET = runtest-$$TEST

INCLUDEPATH += . .. ../..

QTCORE_PRIVATE_HEADERS="$$system(dirname $(dirname $$QMAKE_QMAKE))/include/QtCore/$$QT_VERSION"
INCLUDEPATH += $$QTCORE_PRIVATE_HEADERS

INCLUDEPATH += /usr/local/include/QuaZip-Qt6-1.3

MOC_DIR = _build 
OBJECTS_DIR = _build

LIBS += -L/usr/local/lib

LIBS_PHASH = -lpHash -lpng -ljpeg
LIBS_OPENCV = $$system("pkg-config opencv --libs")
LIBS_FFMPEG = -lavcodec -lavformat -lavutil -lswscale
LIBS_QUAZIP = -lquazip1-qt6 -lz
LIBS_EXIV2  = -lexiv2
LIBS_CIMG   = -lpng -ljpeg
LIBS_TERM   = -ltermcap

# deps for core 
LIBS_INDEX = $$LIBS_OPENCV $$LIBS_FFMPEG $$LIBS_QUAZIP $$LIBS_EXIV2 $$LIBS_TERM
FILES_INDEX = index ioutil media videocontext cvutil qtutil database scanner templatematcher params

# deps for gui
LIBS_GUI = $$LIBS_CIMG
FILES_GUI = gui/mediagrouplistwidget gui/mediafolderlistwidget env \
    lib/jpegquality gui/videocomparewidget cimgops nleutil gui/cropwidget \
    gui/theme

# using this hack to share precomp header with all targets,
# instead of PRECOMPILED_HEADER, so it is only compiled once
QT_HEADERS="$$system(dirname $(dirname $$QMAKE_QMAKE))/include"

QMAKE_CXXFLAGS += -fPIC -I $$QT_HEADERS -include ../prefix.h

QMAKE_CXXFLAGS += -fdiagnostics-color=always -Wno-deprecated-declarations

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
