include("pre.pri")

FILES += $$FILES_INDEX testbase

contains(DEFINES, ENABLE_DEPRECATED) {
    LIBS += $$LIBS_LIBPHASH
}

include("post.pri")
