TEMPLATE = app
#TARGET =
DEPENDPATH += .
INCLUDEPATH += .

QT += widgets network xml
CONFIG += release
CONFIG += debug

include(cbird.pri)

CONFIG += precompile_header silent

PRECOMPILED_HEADER=prefix.h
precompile_header {

    win32 {

        warning("mxe precompiled header hack")
	
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

# Input
HEADERS += $$files(*.h) \
    tree/dcttree.h \
    tree/vptree.h
HEADERS += $$files(gui/*.h)
HEADERS += $$files(lib/*.h)
HEADERS += $$files(tree/*.h)

SOURCES += $$files(*.cpp)
SOURCES += $$files(gui/*.cpp)
SOURCES += $$files(lib/*.cpp)

!contains(DEFINES, ENABLE_CIMG):SOURCES -= cimgops.cpp

DISTFILES += \
    index.pri
