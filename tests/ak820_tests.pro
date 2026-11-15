QT += core gui widgets quick quickcontrols2 testlib
linux: QT += dbus
CONFIG += c++17 testcase
TARGET = ak820_tests
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

INCLUDEPATH += ../src

HEADERS += ../src/keyboard.h ../src/theme.h ../src/translator.h
SOURCES += ak820_tests.cpp ../src/keyboard.cpp ../src/theme.cpp ../src/translator.cpp
