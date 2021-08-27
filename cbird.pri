
# general configuration for index and unit tests

QT += core sql concurrent dbus
CONFIG += c++11 console
mac {
    CONFIG -= app_bundle
}

QMAKE_CXXFLAGS += -fdiagnostics-color=always
QMAKE_CXXFLAGS += -Werror -Wno-deprecated-declarations

# cimg has openmp support, doesn't do much (qualityscore())
#QMAKE_CXXFLAGS += -fopenmp
#QMAKE_LFLAGS   += -fopenmp

DESTDIR=$$_PRO_FILE_PWD_
unix {
    MOC_DIR=_build
    OBJECTS_DIR=_build
}
win32 {
    MOC_DIR=_win
    OBJECTS_DIR=_win
}

#COMPILER = g++
#COMPILER = colorgcc
#COMPILER = clang++
#COMPILER = g++-mp-4.7

#QMAKE_LINK = $$COMPILER
#QMAKE_CXX = $$COMPILER

DEFINES += QT_FORCE_ASSERTS
DEFINES += ENABLE_CIMG   # still needed for qualityscore
DEFINES += ENABLE_OPENCV

# enable debugging here, not CONFIG += debug
#DEFINES += DEBUG

#INCLUDEPATH += $$(QTDIR)/include
#INCLUDEPATH += $$(HOME)/sw/include
#INCLUDEPATH += /usr/local/include

win32 {
    INCLUDEPATH += $$(HOME)/tmp/win/include
    LIBS *= -L $$(HOME)/tmp/win/x64/mingw/lib
    OPENCV_VERSION = 2413
    OPENCV_LIBS *= opencv_ml opencv_objdetect opencv_stitching opencv_superres opencv_videostab opencv_calib3d
    OPENCV_LIBS *= opencv_features2d opencv_highgui opencv_video opencv_photo opencv_imgproc opencv_flann
    OPENCV_LIBS *= opencv_core
    for (CVLIB, OPENCV_LIBS) {
        LIBS *= -l$${CVLIB}$${OPENCV_VERSION}
    }
}

unix {
    INCLUDEPATH += /usr/local/include
    LIBS *= -L/usr/local/lib
    LIBS *= $$system("pkg-config opencv --libs")
}

LIBS *= -lpng # for cimg
LIBS *= -ljpeg # for cimg
LIBS *= -lavcodec -lavformat -lavutil -lswscale
LIBS *= -lexiv2
LIBS *= -lquazip -lz

LIBS *= lib/vptree/lib/libvptree.a

contains(DEFINES, DEBUG) {
    warning("debug build")
    QMAKE_CXXFLAGS_RELEASE = -g -O0
}
else {
    QMAKE_CXXFLAGS_RELEASE = -Ofast -march=native
}

#QMAKE_LFLAGS += -fuse-ld=gold -L/usr/local/lib
