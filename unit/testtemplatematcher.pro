include("pre.pri")

QT += sql dbus widgets xml

LIBS += $$LIBS_INDEX
LIBS += $$LIBS_CIMG
FILES += $$FILES_INDEX $$FILES_GUI cvfeaturesindex testindexbase

include("post.pri")
