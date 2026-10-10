QT += core gui widgets qml quick testlib

CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += poppler-qt6
TARGET = tst_omasign
TEMPLATE = app

INCLUDEPATH += ../src
HEADERS += ../src/backend.h
SOURCES += tst_omasign.cpp ../src/backend.cpp
RESOURCES += ../src/resources.qrc
