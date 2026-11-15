QT += core gui qml quick quickcontrols2 widgets
linux: QT += dbus

CONFIG += c++17 release
TARGET = ak820
TEMPLATE = app

linux: LIBS += -lhidapi-hidraw
macx {
    HIDAPI_ROOT = $$(HIDAPI_ROOT)
    INCLUDEPATH += $$HIDAPI_ROOT/include
    LIBS += -L$$HIDAPI_ROOT/lib -lhidapi
}
win32 {
    HIDAPI_ROOT = $$(HIDAPI_ROOT)
    INCLUDEPATH += $$HIDAPI_ROOT/include
    LIBS += -L$$HIDAPI_ROOT/lib -lhidapi
}

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
