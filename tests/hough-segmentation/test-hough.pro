TEMPLATE = app
#TARGET =
DEPENDPATH *= .
INCLUDEPATH *= .

QT *= widgets

# disable default debug/release behavior
# use DEFINES += DEBUG for debugging
DEFINES += DEBUG
#DEFINES += DEBUG_OPTIMIZED

# enables code only for testing
#DEFINES += TESTING

CONFIG -= debug_and_release
CONFIG -= debug_and_release_target
CONFIG += release
CONFIG += silent

contains(BUILD, verbose) { CONFIG -= silent }
include(../../cbird.pri)

CONFIG += precompile_header

PRECOMPILED_HEADER=../../src/prefix.h

# input
HEADERS += $$files(../../src/cvutil.h)
SOURCES += ../../src/cvutil.cpp \
           ../../src/ioutil.cpp

SOURCES += "test-hough.cpp"

!contains(DEFINES, ENABLE_CIMG): SOURCES -= ../../src/cimgops.cpp

  message("QT=" $$QT)
  message("CONFIG=" $$CONFIG)
  message("COMPILERS= $$QMAKE_CC $$QMAKE_CXX $$QMAKE_LINK")
  message("CXXFLAGS=" $$QMAKE_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE)
  message("INCLUDEPATH="  $$INCLUDEPATH)
  message("DEFINES=" $$DEFINES)
  message("LIBS=" $$LIBS)

