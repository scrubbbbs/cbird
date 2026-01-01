TEMPLATE = app
#TARGET =
DEPENDPATH *= .
INCLUDEPATH *= .

QT *= widgets

# disable default debug/release behavior
# use DEFINES += DEBUG for debugging
#DEFINES += DEBUG
#DEFINES += DEBUG_OPTIMIZED

# enables code only for testing
#DEFINES += TESTING

CONFIG -= debug_and_release
CONFIG -= debug_and_release_target
CONFIG += release
CONFIG += silent

contains(BUILD, verbose) { CONFIG -= silent }
include(cbird.pri)

CONFIG += precompile_header

PRECOMPILED_HEADER=src/prefix.h
precompile_header {

    win32-deprecated {
        # pch only works on windows if .h is in the same dir as .gch,
        # since we change OBJECTS_DIR, gch is in the wrong dir
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

    QMAKE_PRE_TARGETDEPS += src/prefix.h.gch
    QMAKE_CXX = $$COMPILER -include src/prefix.h
    precompiled_header.target = prefix.h.gch
    precompiled_header.commands = $$COMPILER $(CXXFLAGS) $(INCPATH) -x c++-header $$PRECOMPILED_HEADER
    precompiled_header.depends = $$PRECOMPILED_HEADER index.pro
    QMAKE_EXTRA_TARGETS += precompiled_header
}

# automatic version script
QMAKE_PRE_TARGETDEPS += $$OBJECTS_DIR/git.h
git.target = $$OBJECTS_DIR/git.h
git.commands = ./tools/gitversion.sh "$$OBJECTS_DIR" "$$VERSION"
git.depends = .git
QMAKE_EXTRA_TARGETS += git

# input

RESOURCES += src/cbird.qrc src/qdarkstyle/dark/darkstyle.qrc src/qdarkstyle/light/lightstyle.qrc

HEADERS += $$files(src/*.h)
HEADERS += $$files(src/gui/*.h)
HEADERS += $$files(src/lib/*.h)
HEADERS += $$files(src/tree/*.h)

SOURCES += $$files(src/*.cpp)
SOURCES += $$files(src/gui/*.cpp)
SOURCES += $$files(src/lib/*.cpp)

!contains(DEFINES, ENABLE_CIMG): SOURCES -= src/cimgops.cpp


win32: {
    RC_ICONS = windows/cbird.ico

    INSTALLS += target
    target.path = $$BUILDDIR/cbird

    warn.path = $$BUILDDIR/cbird
    warn.extra = ./windows/mxe-pkg.sh $$VERSION x86_64
    INSTALLS += warn
}

unix: {
    # qt bug: only strip if binary
    QMAKE_STRIP = tools/strip.sh

    # installation location override
    PREFIX=/usr/local
    !equals('',$$(PREFIX)) {
      PREFIX=$$(PREFIX)
      message("Installing in $$PREFIX")
    }

    !equals('',$$(PORTABLE)) {
      DEFINES *= CBIRD_PORTABLE_BINARY=1
      message("Configured for portable deployment")
    }

    INSTALLS += target
    target.path = $$PREFIX/bin

    scripts.files = tools/ffplay-sbs tools/ff-compare-audio
    scripts.path  = $$PREFIX/bin/
    INSTALLS += scripts
}

macx: {
  DESTDIR=$$BUILDDIR # don't step on Linux build

  DEFINES += CBIRD_PORTABLE_BINARY # look for runtime deps in applicationDirPath

  RESOURCES += mac/mac.qrc

  portable.$$OBJECTS_DIR/cbird
  portable.commands = ./mac/mac-pkg.sh $$[QT_INSTALL_PLUGINS]

  QMAKE_EXTRA_TARGETS += portable
}

unix:!macx: {

    desktop.files = unix/cbird.desktop
    desktop.path = $$PREFIX/share/applications

    icon.files = src/res/cbird.svg
		icon.path = $$PREFIX/share/icons/hicolor/scalable/apps

    #scripts.extra = echo extra

    INSTALLS += desktop icon

    # appimage target "make appimage"
    #   linuxdeployqt doesn't bring over qt6 plugins so add them manually
    #   wayland plugins needed for some distros
    #   ffmpeg/ffplay/ffprobe are required by extra tool scripts
    PLUGINS=egldeviceintegrations generic imageformats platforms platformthemes \
            platforminputcontexts sqldrivers xcbglintegrations
    EXTRA_PLUGINS_ABS=$$system("ls $$[QT_INSTALL_PLUGINS]/wayland-*/*.so")
    for (DIR, PLUGINS) {
        EXTRA_PLUGINS_ABS+=$$files($$[QT_INSTALL_PLUGINS]/$${DIR}/*.so)
    }
    EXTRA_PLUGINS_REL=
    for (ABS, EXTRA_PLUGINS_ABS) {
        EXTRA_PLUGINS_REL+=$$relative_path($$ABS, $$[QT_INSTALL_PLUGINS])
    }
    EXTRA_PLUGINS=$$join(EXTRA_PLUGINS_REL,',')
    #message("EXTRA_PLUGINS=" $$EXTRA_PLUGINS)

    # we need to use system libva or else we are missing symbols in newer os
    # most distros seem to include it by default
    EXCLUDE_LIBS=libva-drm.so.2,libva.so.2,libva-x11.so.2

    #message($$QMAKE_QMAKE)
    #message($$[QT_INSTALL_LIBS]);
    APPDIR=$$OBJECTS_DIR/appimage
    LINUXDEPLOYQT=~/Downloads/linuxdeployqt-continuous-x86_64.AppImage
    appimage_disable.commands = (rm -rf $$APPDIR && \
      PREFIX="$$APPDIR/cbird" $$QMAKE_QMAKE && \
      make install && \
      cp -auv /usr/local/bin/ff* $$APPDIR/cbird/bin/ && \
      LD_LIBRARY_PATH=$$[QT_INSTALL_LIBS]:/usr/local/lib VERSION=$$VERSION $$LINUXDEPLOYQT \
        $$APPDIR/cbird/share/applications/cbird.desktop \
        -exclude-libs=$$EXCLUDE_LIBS \
        -extra-plugins=$$EXTRA_PLUGINS \
        -executable=/usr/local/bin/ffplay \
        -executable=/usr/local/bin/ffprobe \
        -executable=/usr/local/bin/ffmpeg \
        -qmake=$$QMAKE_QMAKE \
        -appimage)

    appimage.commands = (rm -rf $$APPDIR && \
      PREFIX="$$APPDIR/usr" $$QMAKE_QMAKE && \
      make install && \
      (cp -auv /usr/local/bin/ff* $$APPDIR/usr/bin/ || echo 0) && \
      LDAI_NO_APPSTREAM=1 \
      QMAKE=$$QMAKE_QMAKE \
      EXTRA_QT_PLUGINS=waylandcompositor \
      EXTRA_PLATFORM_PLUGINS=libqwayland.so \
      linuxdeploy --appdir $$APPDIR --plugin=qt --output appimage --verbosity=2 \
        --desktop-file=$$APPDIR/usr/share/applications/cbird.desktop \
      )
    QMAKE_EXTRA_TARGETS += appimage
}

contains(BUILD, verbose) {
  message("QT=" $$QT)
  message("CONFIG=" $$CONFIG)
  message("COMPILERS= $$QMAKE_CC $$QMAKE_CXX $$QMAKE_LINK")
  message("CXXFLAGS=" $$QMAKE_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE)
  message("INCLUDEPATH="  $$INCLUDEPATH)
  message("DEFINES=" $$DEFINES)
  message("LIBS=" $$LIBS)
}

DISTFILES += \
  readme.md \
  doc/cbird-devel.md \
  doc/cbird-compile.md \
  mac/mac-pkg.sh \
  mac/make-icons.sh \
  tools/copystylesheet.sh \
  tools/gitversion.sh \
  tools/strip.sh \
  windows/make-ico.sh \
  windows/mxe-pkg.sh \
  windows/mxe.env
