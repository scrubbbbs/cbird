QT += testlib 
CONFIG += console
TEMPLATE = app
#TARGET = runtest

TEST = $$system("basename $$_PRO_FILE_ | sed -re 's/\.pro$//'")
TARGET = runtest-$$TEST

# disable default debug/release behavior
# use DEFINES += DEBUG for debugging
CONFIG -= debug_and_release
CONFIG -= debug_and_release_target
CONFIG += release
CONFIG += silent

# DEFINES += DEBUG

#contains(BUILD, verbose) { CONFIG -= silent }
include(../cbird.pri)

QMAKE_CXXFLAGS -= -Werror

INCLUDEPATH += . ../_build ../src

QTCORE_PRIVATE_HEADERS="$$system(dirname $(dirname $$QMAKE_QMAKE))/include/QtCore/$$QT_VERSION"
INCLUDEPATH += $$QTCORE_PRIVATE_HEADERS

QT += widgets

LIBS_PHASH = -lpHash -lpng -ljpeg

# deps for core 
FILES_INDEX = index ioutil media videocontext cvutil qtutil database scanner templatematcher params

# deps for gui
FILES_GUI = gui/mediagrouplistwidget gui/mediafolderlistwidget env \
    lib/jpegquality gui/videocomparewidget cimgops nleutil gui/cropwidget \
    gui/theme gui/mediapage gui/mediaitemdelegate gui/pooledimageallocator

win32 {
  PRECOMPILED_HEADER=../src/prefix.h
  CONFIG += precompile_header
}
unix {
  # using this hack to share precomp header with all targets,
  # instead of PRECOMPILED_HEADER, so it is only compiled once
  QT_HEADERS="$$system(dirname $(dirname $$QMAKE_QMAKE))/include"
  QMAKE_CXXFLAGS += -fPIC -I $$QT_HEADERS -include ../src/prefix.h
}


# coverage
!equals('',$$(COVERAGE)) {
  message("coverage enabled")
  DEFINES += DEBUG
  QMAKE_CXXFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
  QMAKE_LFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
  LIBS += -lgcov
}

# deprecated code, at some point want to get rid of it!
#DEFINES += ENABLE_DEPRECATED

SOURCES =
FILES = $$TEST
