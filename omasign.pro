QT += core gui widgets qml quick quickcontrols2 quickdialogs2 dbus

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += poppler-qt6
TARGET = omasign
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
