include("../pre.qm")

#QT += sql

#LIBS += $$LIBS_OPENCV
#LIBS += $$LIBS_FFMPEG
#LIBS += $$LIBS_PHASH

# file names we want to test, auto add
# .h and .cpp with the same prefix
FILES += myclass

# depricated stuff
contains(DEFINES, ENABLE_DEPRECATED) {
    #FILES += cimgops
}

include("../post.qm")
