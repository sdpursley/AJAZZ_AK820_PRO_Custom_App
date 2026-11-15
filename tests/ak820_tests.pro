QT += core gui widgets quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = ak820_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/keyboard.h ../src/theme.h ../src/translator.h
SOURCES += ak820_tests.cpp ../src/keyboard.cpp ../src/theme.cpp ../src/translator.cpp
