
# general configuration for cbird and unit tests

QT *= core sql concurrent
CONFIG *= c++17 console
mac {
    CONFIG -= app_bundle
}

VERSION=0.5.1

QMAKE_CXXFLAGS += '-DCBIRD_VERSION=\\"$$VERSION\\"'
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
    MOC_DIR=_win32
    OBJECTS_DIR=_win32
}

#COMPILER = g++
#COMPILER = colorgcc
#COMPILER = clang++
#COMPILER = g++-mp-4.7

#QMAKE_LINK = $$COMPILER
#QMAKE_CXX = $$COMPILER

DEFINES += QT_FORCE_ASSERTS
DEFINES += QT_MESSAGELOGCONTEXT

DEFINES += ENABLE_CIMG   # still needed for qualityscore
DEFINES += ENABLE_OPENCV

# enable debugging here, not CONFIG += debug
#DEFINES += DEBUG

#INCLUDEPATH += $$(QTDIR)/include
#INCLUDEPATH += $$(HOME)/sw/include
#INCLUDEPATH += /usr/local/include

# private headers for DebugEventFilter
QTCORE_PRIVATE_HEADERS="$$[QT_INSTALL_HEADERS]/QtCore/$$QT_VERSION"
!exists( $$QTCORE_PRIVATE_HEADERS ) {
    message("$${QTCORE_PRIVATE_HEADERS}/")
    error("Can't find path to qtcore private headers")
}
INCLUDEPATH += $$QTCORE_PRIVATE_HEADERS

win32 {
    INCLUDEPATH += windows/build-opencv/install/include
    LIBS *= -L windows/build-opencv/install/x64/mingw/lib
    OPENCV_VERSION = 2413
    OPENCV_LIBS *= opencv_ml opencv_objdetect opencv_stitching opencv_superres opencv_videostab opencv_calib3d
    OPENCV_LIBS *= opencv_features2d opencv_highgui opencv_video opencv_photo opencv_imgproc opencv_flann
    OPENCV_LIBS *= opencv_core
    for (CVLIB, OPENCV_LIBS) {
        LIBS *= -l$${CVLIB}$${OPENCV_VERSION}
    }
    LIBS *= -lquazip -lz
    LIBS *= -lpsapi
}

unix {
    QT += dbus xml

    INCLUDEPATH *= /usr/local/include
    INCLUDEPATH *= /usr/local/include/QuaZip-Qt5-1.1

    LIBS *= -L/usr/local/lib
    LIBS *= $$system("pkg-config opencv --libs")

    exists(/usr/local/lib/libquazip1-qt5.so) {
        LIBS *= -lquazip1-qt5 -lz
    }
    else {
        LIBS *= -lquazip -lz
    }
}

LIBS *= -lpng # for cimg
LIBS *= -ljpeg # for cimg
LIBS *= -lavcodec -lavformat -lavutil -lswscale
LIBS *= -lexiv2
LIBS *= -ltermcap

# LIBS *= lib/vptree/lib/libvptree.a

contains(DEFINES, DEBUG) {
    warning("debug build")
    QMAKE_CXXFLAGS_RELEASE = -g -O0
}
else {
    win32 {
        # westmere is latest that I can run in qemu, and
        # it has popcnt (population count) which is nice for hamm64()
        QMAKE_CXXFLAGS_RELEASE = -Ofast -march=westmere
    }
    unix {
        QMAKE_CXXFLAGS_RELEASE = -Ofast -march=native
    }
}

#QMAKE_LFLAGS += -fuse-ld=gold -L/usr/local/lib

