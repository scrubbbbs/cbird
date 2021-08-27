QT += testlib concurrent 
CONFIG += debug c++11 precompile_header console
TEMPLATE = app
TARGET = runtest
INCLUDEPATH += . .. ../..
MOC_DIR = _tmp
OBJECTS_DIR = _tmp


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

#QMAKE_CXX += -include prefix.h
PRECOMPILED_HEADER = ../../prefix.h
#QMAKE_CXX += -fdiagnostics-color=always

#QMAKE_LFLAGS += -fuse-ld=gold

# coverage
#CONFIG -= release
#CONFIG += debug
#QMAKE_CXXFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
#QMAKE_LFLAGS += -O0 -Wall -fprofile-arcs -ftest-coverage
#LIBS += -lgcov

# deprecated code, at some point want to get rid of it!
#DEFINES += ENABLE_DEPRECATED

SOURCES = *.cpp

FILES =
