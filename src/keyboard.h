#pragma once

#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>

// What one Save writes. Mode ids match the keyboard: 0 off through 19 shuttle.
struct Light {
    int mode = 1;
    int red = 255;
    int green = 255;
    int blue = 255;
    bool rainbow = false;
    int brightness = 3;
    int speed = 2;
    int direction = 0;
};

struct Attempt {
    bool ok = false;
    int connected = -1; // 1 yes, 0 no, -1 leave the window's indicator alone
    QString message;
};

// Packet builders are pure so tests can check them without a keyboard.
std::array<uint8_t, 64> controlPacket(uint8_t command, uint8_t sub, uint16_t value);
std::array<uint8_t, 64> lightingPacket(const Light &light);
std::array<uint8_t, 64> sleepPacket(int minutes);
std::array<uint8_t, 64> clockPacket(int year, int month, int day, int hour, int minute, int second);
QByteArray encodeStillRgb888(const uchar *rgb, int nbytes);

QStringList modeNames();
QStringList directionNames();
QStringList sleepNames();
int modeFromName(const QString &name);
int directionFromName(const QString &name);
int directionForIndex(int index);
int sleepMinutesForIndex(int index);

class Keyboard {
public:
    Attempt describe();
    Attempt setLighting(const Light &light);
    Attempt setSleep(int minutes);
    Attempt syncClock(int year, int month, int day, int hour, int minute, int second);
    Attempt uploadStill(const QByteArray &rgb888);
};
