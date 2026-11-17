QT += core gui qml quick quickcontrols2 dbus
CONFIG += c++17 release
TARGET = webmfix
TEMPLATE = app
HEADERS += src/backend.h src/filepicker.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/filepicker.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc
