
# general configuration for cbird and unit tests

# too many breaking changes
equals(QT_MAJOR_VERSION, 5) {
    error("QT 6 is required")
}

QT *= core sql concurrent xml
CONFIG *= c++17 console
mac {
    CONFIG -= app_bundle
}

VERSION=0.6.2

QMAKE_CXXFLAGS += -fdiagnostics-color=always
QMAKE_CXXFLAGS += -Werror -Wno-deprecated-declarations

# cimg has openmp support, doesn't do much (qualityscore())
#QMAKE_CXXFLAGS += -fopenmp
#QMAKE_LFLAGS   += -fopenmp

# autotools-style compiler override, also needed for appimage
CXX=$$(CXX)
!isEmpty(CXX) {
    QMAKE_CXX=$$CXX
    QMAKE_LINK=$$CXX
}
CC=$$(CC)
!isEmpty(CC) {
    QMAKE_CC=$$CC
}

DESTDIR=$$_PRO_FILE_PWD_
BUILDDIR=_build

win32: BUILDDIR=_win32

MOC_DIR=$$BUILDDIR
OBJECTS_DIR=$$BUILDDIR
RCC_DIR=$$BUILDDIR

DEFINES += QT_FORCE_ASSERTS     # Q_ASSERT(0) crashes the app
DEFINES += QT_MESSAGELOGCONTEXT # nice for custom logger
DEFINES += ENABLE_CIMG          # still needed for qualityscore

# enable debug build/features, NOT CONFIG += debug
#DEFINES += DEBUG

# private headers for DebugEventFilter
QTCORE_PRIVATE_HEADERS="$$[QT_INSTALL_HEADERS]/QtCore/$$QT_VERSION"
!exists( $$QTCORE_PRIVATE_HEADERS ) {
    message("$${QTCORE_PRIVATE_HEADERS}/")
    error("Can't find qtcore private headers, maybe you need qt6-base-private-dev")
}
INCLUDEPATH += $$QTCORE_PRIVATE_HEADERS

win32 {
    INCLUDEPATH += windows/build-opencv/install/include
    LIBS *= -L windows/build-opencv/install/x64/mingw/lib
    OPENCV_VERSION = 2413
    OPENCV_LIBS *= ml objdetect stitching superres videostab calib3d
    OPENCV_LIBS *= features2d highgui video photo imgproc flann core
    for (CVLIB, OPENCV_LIBS) {
        LIBS *= -lopencv_$${CVLIB}$${OPENCV_VERSION}
    }
    LIBS *= -lquazip -lz
    LIBS *= -lpsapi
}

unix {
    QT += dbus

    INCLUDEPATH *= /usr/local/include

    LIBS *= -L/usr/local/lib
    LIBS *= -ltermcap

    CV_REQUIRED=2.4.13.6
    CV_VERSION=$$system("pkg-config opencv --modversion")
    !equals(CV_VERSION,$$CV_REQUIRED)  {
        error("OpenCV $$CV_REQUIRED is required, found version <$$CV_VERSION>")
    }
    LIBS *= $$system("pkg-config opencv --libs")

    # quazip uses a funky versioned include directory...and now qt6 doesn't seem
    # to distribute pkg-config files at all (Ubuntu 22.04) but they're still in the source build
    # .. so we need to find quazip ourself
    # fixme: qt6 seems to have moved to cmake so throw all of this out..
    QUAZIP_MODULE=quazip1-qt6
    QUAZIP_VERSION=$$system("pkg-config $$QUAZIP_MODULE --modversion")
    QUAZIP_HEADERS="/usr/local/include/QuaZip-Qt6-$$QUAZIP_VERSION"
    QUAZIP_LIB = "/usr/local/lib/lib$${QUAZIP_MODULE}.so"

    !exists($$QUAZIP_HEADERS) {
        message(expected QuaZip headers in $$QUAZIP_HEADERS)
        error(quazip headers elude me)
    }
    INCLUDEPATH *= $$QUAZIP_HEADERS

    !exists($$QUAZIP_LIB) {
        message(expected QuaZip lib at $$QUAZIP_LIB)
        error(quazip lib eludes me)
    }

    LIBS *= -l$${QUAZIP_MODULE} -lz
}

# cross-platform common libs
contains(DEFINES, ENABLE_CIMG) LIBS *= -lpng -ljpeg
LIBS *= -lavcodec -lavformat -lavutil -lswscale
LIBS *= -lexiv2

# testing other search tree implementations
# LIBS *= lib/vptree/lib/libvptree.a

contains(DEFINES, DEBUG) {
    warning("******************************")
    warning("DEBUG BUILD")
    warning("******************************")
    QMAKE_CXXFLAGS_RELEASE = -g -O0
}
else {
    # westmere is latest that I can run in qemu, and
    # it has popcnt (population count) which is nice for hamm64()
    win32: QMAKE_CXXFLAGS_RELEASE = -Ofast -march=westmere

    unix: QMAKE_CXXFLAGS_RELEASE = -Ofast -march=native
}

