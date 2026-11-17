QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = webmfix_tests
TEMPLATE = app
INCLUDEPATH += ../src
HEADERS += ../src/backend.h ../src/filepicker.h ../src/theme.h ../src/translator.h
SOURCES += webmfix_tests.cpp ../src/backend.cpp ../src/filepicker.cpp ../src/theme.cpp ../src/translator.cpp
RESOURCES += ../src/resources.qrc
