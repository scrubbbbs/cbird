include("pre.pri")

FILES += cvutil ioutil

contains(DEFINES, ENABLE_DEPRECATED) {
    LIBS += $$LIBS_LIBPHASH
}

include("post.pri")
