TEMPLATE = app
#TARGET =
DEPENDPATH *= .
INCLUDEPATH *= .

QT *= widgets

# we only want to build one target, these screw that up...
CONFIG -= debug_and_release
CONFIG -= debug_and_release_target

CONFIG += release silent

include(cbird.pri)

CONFIG += precompile_header

PRECOMPILED_HEADER=prefix.h
precompile_header {

    win32 {
        # pch only works on windows if .h is in the same dir as .gch,
        # since we changed OBJECTS_DIR, gch is in the wrong dir
        message("win32 precompiled header hack")
	
        PRECOMPILED_HEADER_COPY=$$OBJECTS_DIR/$$PRECOMPILED_HEADER
        copy_pch.target = $$PRECOMPILED_HEADER_COPY
        copy_pch.commands = $(COPY_FILE) $$PRECOMPILED_HEADER $$PRECOMPILED_HEADER_COPY
        copy_pch.depends= $$PRECOMPILED_HEADER
        QMAKE_EXTRA_TARGETS += copy_pch
        PRE_TARGETDEPS += $$PRECOMPILED_HEADER_COPY
    }

} else {

    warning("no precompiled headers... run 'make prefix.h.gch' first")

    QMAKE_PRE_TARGETDEPS += prefix.h.gch
    QMAKE_CXX = $$COMPILER -include prefix.h
    precompiled_header.target = prefix.h.gch
    precompiled_header.commands = $$COMPILER $(CXXFLAGS) $(INCPATH) -x c++-header $$PRECOMPILED_HEADER
    precompiled_header.depends = $$PRECOMPILED_HEADER index.pro
    QMAKE_EXTRA_TARGETS += precompiled_header
}

# git version info
QMAKE_PRE_TARGETDEPS += $$OBJECTS_DIR/git.h
git.target = $$OBJECTS_DIR/git.h
git.commands = ./tools/gitversion.sh "$$OBJECTS_DIR"
git.depends = .git
QMAKE_EXTRA_TARGETS += git

RESOURCES += cbird.qrc

# Input
HEADERS += $$files(*.h)
HEADERS += $$files(gui/*.h)
HEADERS += $$files(lib/*.h)
HEADERS += $$files(tree/*.h)

SOURCES += $$files(*.cpp)
SOURCES += $$files(gui/*.cpp)
SOURCES += $$files(lib/*.cpp)

!contains(DEFINES, ENABLE_CIMG):SOURCES -= cimgops.cpp

DISTFILES += \
    index.pri

win32: {
    INSTALLS += target
    target.path = _win32/cbird

    warn.path = _win32/cbird
    warn.extra = ./windows/mxe-pkg.sh $$VERSION x86_64
    INSTALLS += warn
}

unix: {
    # installation location override
    PREFIX=/usr/local
    !equals('',$$(PREFIX)) {
      PREFIX=$$(PREFIX)
      message("Installing in $$PREFIX")
    }

    INSTALLS += target
    target.path = $$PREFIX/bin

    desktop.files = cbird.desktop
    desktop.path = $$PREFIX/share/applications

    icon.files = cbird.svg
    icon.path = $$PREFIX/share/icons/hicolor/scalable

    scripts.files = tools/ffplay-sbs tools/ff-compare-audio
    scripts.path  = $$PREFIX/bin/
    #scripts.extra = echo extra

    # qt bug: only strip if exe
    QMAKE_STRIP = tools/strip.sh

    INSTALLS += desktop icon scripts

    EXTRA_PLUGINS=$$system("ls $$[QT_INSTALL_PLUGINS]/platforms/*wayland* | sed -re 's;.*./plugins/(.*);\1;' | xargs | sed -re 's/ /,/g'")
    EXTRA_PLUGINS=$$EXTRA_PLUGINS,$$system("ls $$[QT_INSTALL_PLUGINS]/*wayland*/*.so | sed -re 's;.*./plugins/(.*);\1;' | xargs | sed -re 's/ /,/g'")
    #message($$EXTRA_PLUGINS)

    APPDIR=$$OBJECTS_DIR/appimage
    QMAKE=/usr/local/Qt-5.15.2/bin/qmake
    LINUXDEPLOYQT=~/Downloads/linuxdeployqt-continuous-x86_64.AppImage
    appimage.commands = (rm -rf $$APPDIR && \
      PREFIX="$$APPDIR/cbird" $$QMAKE && \
      make install && \
      cp -auv /usr/local/bin/ff* $$APPDIR/cbird/bin/ && \
      LD_LIBRARY_PATH=/usr/local/lib VERSION=$$VERSION $$LINUXDEPLOYQT \
        $$APPDIR/cbird/share/applications/cbird.desktop \
        -extra-plugins=$$EXTRA_PLUGINS \
        -executable=/usr/local/bin/ffplay \
        -executable=/usr/local/bin/ffprobe \
        -qmake=$$QMAKE \
        -appimage)

    QMAKE_EXTRA_TARGETS += appimage
}

message("QT=" $$QT)
message("CONFIG=" $$CONFIG)
message("CXXFLAGS=" $$QMAKE_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE)
