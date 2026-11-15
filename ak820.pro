QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = ak820
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/keyboard.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/keyboard.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
