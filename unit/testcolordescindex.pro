include("pre.pri")

QT += sql widgets dbus xml

LIBS += $$LIBS_INDEX $$LIBS_GUI

FILES += $$FILES_INDEX $$FILES_GUI colordescindex testindexbase

include("post.pri")
