// This board (0C45:8009, firmware 1.14) speaks FF13 feature reports for
// settings and FF68 4096-byte reports for the screen. The sequence is the
// one checked against that descriptor: START, command, data, COMMIT, FINISH.
// Each apply is stored in the keyboard's flash, so nothing is sent until asked.

#include "keyboard.h"

#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QThread>
#include <hidapi/hidapi.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr int kWidth = 128;
constexpr int kPixels = kWidth * kWidth;
constexpr int kChunk = 4096;
constexpr uint8_t kStart = 0x18;
constexpr uint8_t kCommit = 0x02;
constexpr uint8_t kFinish = 0xf0;

constexpr unsigned short kVendorId = 0x0c45;
constexpr unsigned short kControlUsagePage = 0xff13;
constexpr unsigned short kDataUsagePage = 0xff68;

QString say(const char *text) { return QCoreApplication::translate("Keyboard", text); }

const char *kModeIds[] = {
    "off", "static", "single-on", "single-off", "glittering", "falling", "colourful", "breath",
    "spectrum", "outward", "scrolling", "rolling", "rotating", "explode", "launch", "ripples",
    "flowing", "pulsating", "tilt", "shuttle"};

struct Paths {
    QByteArray control;
    QByteArray data;
    bool saw = false;
    bool otherFirmware = false;
};

Paths findKeyboard() {
    Paths found;
    auto *devices = hid_enumerate(kVendorId, 0);
    for (auto *device = devices; device; device = device->next) {
        if (device->product_id != 0x8009 && device->product_id != 0x800a)
            continue;
        found.saw = true;
        if (!device->path)
            continue;
        if (device->usage_page == kControlUsagePage)
            found.control = device->path;
        else if (device->usage_page == kDataUsagePage)
            found.data = device->path;
    }
    hid_free_enumeration(devices);
    found.otherFirmware = found.saw && (found.control.isEmpty() || found.data.isEmpty());
    return found;
}

class Session {
public:
    ~Session() { close(); }

    Attempt open() {
        if (hid_init() != 0)
            return {false, false, say("Could not initialize USB access.")};
        m_initialized = true;
        const Paths paths = findKeyboard();
        if (paths.otherFirmware)
            return {false, false, say("This AK820 uses a firmware this app does not write to.")};
        if (!paths.saw)
            return {false, false, say("Plug the AK820 Pro in with its USB cable.")};

        if (!m_lock.tryLock(12000))
            return {false, true, say("Another keyboard operation is running.")};

        m_control = hid_open_path(paths.control.constData());
        m_data = hid_open_path(paths.data.constData());
        if (!m_control || !m_data) {
            const wchar_t *detail = hid_error(nullptr);
            const QString why = detail ? QString::fromWCharArray(detail) : QString();
#ifdef Q_OS_LINUX
            return {false, false, say("Cannot open the keyboard (%1). Run bin/setup-udev from the project and replug the cable.").arg(why)};
#else
            return {false, false, say("Cannot open the keyboard (%1). Close other keyboard software and reconnect the keyboard.").arg(why)};
#endif
        }
        return {true, true, {}};
    }

    bool exchange(const std::array<uint8_t, 64> &packet, std::array<uint8_t, 64> *reply, QString *error) {
        uint8_t wire[65] = {};
        memcpy(wire + 1, packet.data(), 64);
        QThread::msleep(35);
        if (hid_send_feature_report(m_control, wire, sizeof wire) < 0) {
            *error = say("Could not talk to the keyboard (%1).")
                         .arg(QString::fromWCharArray(hid_error(m_control)));
            return false;
        }
        QThread::msleep(50);
        uint8_t response[65] = {};
        const int got = hid_get_feature_report(m_control, response, sizeof response);
        if (got < 64) {
            *error = say("The keyboard did not answer.");
            return false;
        }
        // A 65-byte reply keeps report id 0 in front. A 64-byte reply is the payload.
        const uint8_t *payload = got >= 65 ? response + 1 : response;
        memcpy(reply->data(), payload, 64);
        return true;
    }

    bool acknowledged(const std::array<uint8_t, 64> &reply, uint8_t command) const {
        return reply[0] == 4 && reply[1] == command && reply[3] == 1;
    }

    bool start(uint16_t flag, QString *error) {
        for (int attempt = 0; attempt < 10; ++attempt) {
            std::array<uint8_t, 64> reply{};
            if (!exchange(controlPacket(kStart, 0, flag), &reply, error))
                return false;
            const bool busy = std::all_of(reply.begin(), reply.end(), [](uint8_t b) { return b == 0; })
                || (reply[0] == 4 && reply[1] == kStart && reply[3] == 0xff);
            if (!busy)
                return acknowledged(reply, kStart) || (*error = say("The keyboard did not accept the command."), false);
            if (attempt < 9)
                QThread::msleep(300);
        }
        *error = say("The keyboard did not accept the command.");
        return false;
    }

    bool command(const std::array<uint8_t, 64> &packet, QString *error) {
        std::array<uint8_t, 64> reply{};
        if (!exchange(packet, &reply, error))
            return false;
        if (!acknowledged(reply, packet[1])) {
            *error = say("The keyboard did not accept the command.");
            return false;
        }
        return true;
    }

    bool finish(uint16_t flag) {
        QString ignored;
        return command(controlPacket(kFinish, 0, flag), &ignored);
    }

    // Settings: START, preamble, one data packet, COMMIT, FINISH.
    // FINISH is sent even when an earlier step failed, or the next command is refused.
    bool transaction(const std::array<uint8_t, 64> &preamble, const std::array<uint8_t, 64> &data, QString *error) {
        bool ok = start(1, error) && command(preamble, error);
        if (ok) {
            std::array<uint8_t, 64> reply{};
            ok = exchange(data, &reply, error);
        }
        if (ok)
            ok = command(controlPacket(kCommit, 0, 0), error);
        const bool finished = finish(1);
        if (ok && !finished) {
            *error = say("The keyboard did not accept the command.");
            return false;
        }
        return ok;
    }

    void close() {
        if (m_control)
            hid_close(m_control);
        if (m_data)
            hid_close(m_data);
        m_control = m_data = nullptr;
        if (m_lock.isLocked())
            m_lock.unlock();
        if (m_initialized) {
            hid_exit();
            m_initialized = false;
        }
    }

    hid_device *dataDevice() const { return m_data; }

private:
    QLockFile m_lock{QDir::tempPath() + "/ak820-0c45.lock"};
    bool m_initialized = false;
    hid_device *m_control = nullptr;
    hid_device *m_data = nullptr;
};

bool inRange(int value, int max) { return value >= 0 && value <= max; }

} // namespace

std::array<uint8_t, 64> controlPacket(uint8_t command, uint8_t sub, uint16_t value) {
    std::array<uint8_t, 64> packet{};
    packet[0] = 4;
    packet[1] = command;
    packet[2] = sub;
    packet[8] = value & 0xff;
    packet[9] = value >> 8;
    return packet;
}

std::array<uint8_t, 64> lightingPacket(const Light &light) {
    // Off has no working mode byte on this firmware, so it is single-on at brightness 0.
    // Static is mode 1, a solid color. Mode 7 is breathe, and speed 0 still breathes.
    int mode = light.mode;
    int speed = std::clamp(light.speed, 0, 5);
    int brightness = std::clamp(light.brightness, 0, 5);
    if (mode == 0) {
        mode = 2;
        speed = 0;
        brightness = 0;
    }
    std::array<uint8_t, 64> packet{};
    packet[0] = mode;
    packet[1] = light.red;
    packet[2] = light.green;
    packet[3] = light.blue;
    packet[8] = light.rainbow ? 1 : 0;
    packet[9] = brightness;
    packet[10] = speed;
    packet[11] = light.direction;
    packet[14] = 0x55;
    packet[15] = 0xaa;
    return packet;
}

std::array<uint8_t, 64> sleepPacket(int minutes) {
    std::array<uint8_t, 64> packet{};
    packet[8] = minutes == 1 ? 1 : minutes == 5 ? 2 : minutes == 30 ? 3 : 0;
    packet[62] = 0xaa;
    packet[63] = 0x55;
    return packet;
}

std::array<uint8_t, 64> clockPacket(int year, int month, int day, int hour, int minute, int second) {
    std::array<uint8_t, 64> packet{};
    packet[1] = 1;
    packet[2] = 0x5a;
    packet[3] = year - 2000;
    packet[4] = month;
    packet[5] = day;
    packet[6] = hour;
    packet[7] = minute;
    packet[8] = second;
    packet[10] = 4;
    packet[62] = 0xaa;
    packet[63] = 0x55;
    return packet;
}

QByteArray encodeStillRgb888(const uchar *rgb, int nbytes) {
    if (!rgb || nbytes != kPixels * 3)
        return {};
    QByteArray data(9 * kChunk, char(0xff));
    data[0] = 1;
    data[1] = 0;
    auto *out = reinterpret_cast<uchar *>(data.data()) + 256;
    for (int i = 0; i < kPixels; ++i) {
        const uint16_t pixel = (uint16_t(rgb[i * 3] >> 3) << 11)
            | (uint16_t(rgb[i * 3 + 1] >> 2) << 5)
            | uint16_t(rgb[i * 3 + 2] >> 3);
        out[i * 2] = pixel & 0xff;
        out[i * 2 + 1] = pixel >> 8;
    }
    return data;
}

QStringList modeNames() {
    return {say("Off"), say("Static"), say("Single on"), say("Single off"), say("Glittering"),
            say("Falling"), say("Colourful"), say("Breath"), say("Spectrum"), say("Outward"),
            say("Scrolling"), say("Rolling"), say("Rotating"), say("Explode"), say("Launch"),
            say("Ripples"), say("Flowing"), say("Pulsating"), say("Tilt"), say("Shuttle")};
}

QStringList directionNames() {
    return {say("Left"), say("Right"), say("Up"), say("Down")};
}

QStringList sleepNames() {
    return {say("Leave unchanged"), say("Never"), say("1 minute"), say("5 minutes"), say("30 minutes")};
}

int modeFromName(const QString &name) {
    bool numeric = false;
    const int index = name.toInt(&numeric);
    if (numeric && index >= 0 && index < 20)
        return index;
    const QString key = name.trimmed().toLower().replace(' ', '-');
    for (int i = 0; i < 20; ++i)
        if (key == QLatin1String(kModeIds[i]))
            return i;
    return -1;
}

int directionFromName(const QString &name) {
    const QString key = name.trimmed().toLower();
    if (key == "left" || key == "l")
        return 0;
    if (key == "down" || key == "d")
        return 1;
    if (key == "up" || key == "u")
        return 2;
    if (key == "right" || key == "r")
        return 3;
    return -1;
}

int directionForIndex(int index) {
    static const int values[] = {0, 3, 2, 1};
    return index >= 0 && index < 4 ? values[index] : 0;
}

int sleepMinutesForIndex(int index) {
    static const int values[] = {-1, 0, 1, 5, 30};
    return index >= 0 && index < 5 ? values[index] : -1;
}

Attempt Keyboard::describe() {
    Session session;
    Attempt opened = session.open();
    if (!opened.ok)
        return opened;
    std::array<uint8_t, 64> query{};
    query[0] = 0x20;
    query[1] = 1;
    std::array<uint8_t, 64> reply{};
    QString error;
    if (!session.exchange(query, &reply, &error))
        return {true, true, say("Connected.")};
    if (reply[0] == 0x20 && reply[1] == 1 && reply[3] == 255)
        return {true, true, say("Connected. On USB power.")};
    if (reply[0] == 0x20 && reply[1] == 1 && reply[3] <= 100)
        return {true, true, say("Connected. Battery %1%.").arg(reply[3])};
    return {true, true, say("Connected.")};
}

Attempt Keyboard::setLighting(const Light &light) {
    if (!inRange(light.mode, 19) || !inRange(light.red, 255) || !inRange(light.green, 255)
        || !inRange(light.blue, 255) || !inRange(light.brightness, 5) || !inRange(light.speed, 5)
        || !inRange(light.direction, 3))
        return {false, -1, say("The keyboard did not accept the command.")};
    Session session;
    Attempt opened = session.open();
    if (!opened.ok)
        return opened;
    QString error;
    const bool ok = session.transaction(controlPacket(0x13, 0, 1), lightingPacket(light), &error);
    return {ok, true, ok ? say("Saved to the keyboard.") : error};
}

Attempt Keyboard::setSleep(int minutes) {
    if (minutes != 0 && minutes != 1 && minutes != 5 && minutes != 30)
        return {false, -1, say("Sleep must be never, 1, 5, or 30 minutes.")};
    Session session;
    Attempt opened = session.open();
    if (!opened.ok)
        return opened;
    QString error;
    const bool ok = session.transaction(controlPacket(0x17, 1, 1), sleepPacket(minutes), &error);
    return {ok, true, ok ? say("Saved to the keyboard.") : error};
}

Attempt Keyboard::syncClock(int year, int month, int day, int hour, int minute, int second) {
    if (year < 2000 || year > 2099)
        return {false, -1, say("Year %1 is outside 2000–2099.").arg(year)};
    Session session;
    Attempt opened = session.open();
    if (!opened.ok)
        return opened;
    QString error;
    const auto packet = clockPacket(year, month, day, hour, minute, second);
    const bool ok = session.transaction(controlPacket(0x28, 0, 1), packet, &error);
    return {ok, true, ok ? say("Clock synced.") : error};
}

Attempt Keyboard::uploadStill(const QByteArray &rgb888) {
    const QByteArray frame = encodeStillRgb888(reinterpret_cast<const uchar *>(rgb888.constData()), rgb888.size());
    if (frame.isEmpty())
        return {false, -1, say("That file is not an image.")};
    Session session;
    Attempt opened = session.open();
    if (!opened.ok)
        return opened;

    QString error;
    bool ok = session.start(0, &error);
    const int chunks = frame.size() / kChunk;
    if (ok)
        ok = session.command(controlPacket(0x72, 1, chunks), &error);
    if (ok) {
        uint8_t junk[4096];
        while (hid_read_timeout(session.dataDevice(), junk, sizeof junk, 0) > 0) {}
        for (int i = 0; ok && i < chunks; ++i) {
            uint8_t report[kChunk + 1] = {};
            memcpy(report + 1, frame.constData() + i * kChunk, kChunk);
            if (hid_write(session.dataDevice(), report, sizeof report) < kChunk) {
                error = say("Could not send the screen (%1).")
                            .arg(QString::fromWCharArray(hid_error(session.dataDevice())));
                ok = false;
                break;
            }
            if (hid_read_timeout(session.dataDevice(), junk, sizeof junk, 1000) <= 0) {
                error = say("The screen did not acknowledge the image. Send it again.");
                ok = false;
            }
        }
    }
    if (ok)
        ok = session.command(controlPacket(kCommit, 0, 0), &error);
    if (!ok)
        session.finish(1);
    else
        QThread::msleep(100);
    return {ok, true, ok ? say("Screen updated.") : error};
}
