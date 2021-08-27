include("../pre.pri")

#QT += sql

LIBS += $$LIBS_OPENCV

FILES += cvutil ioutil

contains(DEFINES, ENABLE_DEPRECATED) {
    LIBS += $$LIBS_LIBPHASH
}

include("../post.pri")
