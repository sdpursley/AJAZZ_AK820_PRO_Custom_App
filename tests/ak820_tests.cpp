#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "keyboard.h"
#include "theme.h"
#include "translator.h"

class Ak820Tests : public QObject {
    Q_OBJECT

private slots:
    void staticAndOffMatchTheLegacyLayout()
    {
        Light light;
        light.mode = 1;
        light.red = 0x12;
        light.green = 0x34;
        light.blue = 0x56;
        light.brightness = 3;
        light.speed = 5;
        const auto packet = lightingPacket(light);
        QCOMPARE(int(packet[0]), 1);
        QCOMPARE(int(packet[1]), 0x12);
        QCOMPARE(int(packet[9]), 3);
        QCOMPARE(int(packet[10]), 5);
        QCOMPARE(int(packet[14]), 0x55);
        QCOMPARE(int(packet[15]), 0xaa);

        light.mode = 0;
        light.brightness = 4;
        const auto off = lightingPacket(light);
        QCOMPARE(int(off[0]), 2);
        QCOMPARE(int(off[9]), 0);
        QCOMPARE(int(off[10]), 0);
    }

    void imageConfigAndStillFrame()
    {
        const auto config = controlPacket(0x72, 1, 9);
        QCOMPARE(int(config[0]), 4);
        QCOMPARE(int(config[1]), 0x72);
        QCOMPARE(int(config[2]), 1);
        QCOMPARE(int(config[8]), 9);
        QCOMPARE(int(config[9]), 0);

        QByteArray rgb(128 * 128 * 3, 0);
        rgb[0] = char(255);
        const QByteArray frame = encodeStillRgb888(reinterpret_cast<const uchar *>(rgb.constData()), rgb.size());
        QCOMPARE(frame.size(), 9 * 4096);
        QCOMPARE(int(uchar(frame[0])), 1);
        QCOMPARE(int(uchar(frame[1])), 0);
        QCOMPARE(int(uchar(frame[2])), 0xff);
        QCOMPARE(int(uchar(frame[256])), 0x00);
        QCOMPARE(int(uchar(frame[257])), 0xf8);
        QVERIFY(encodeStillRgb888(nullptr, 0).isEmpty());
    }

    void clockAndSleepAndNames()
    {
        const auto clock = clockPacket(2026, 10, 20, 15, 26, 37);
        QCOMPARE(int(clock[2]), 0x5a);
        QCOMPARE(int(clock[3]), 26);
        QCOMPARE(int(clock[4]), 10);
        QCOMPARE(int(clock[8]), 37);
        QCOMPARE(int(clock[10]), 4);
        QCOMPARE(int(clock[62]), 0xaa);
        QCOMPARE(int(sleepPacket(30)[8]), 3);
        QCOMPARE(int(sleepPacket(5)[8]), 2);
        QCOMPARE(modeNames().size(), 20);
        QCOMPARE(modeFromName("static"), 1);
        QCOMPARE(directionFromName("right"), 3);
        QCOMPARE(directionForIndex(1), 3);
        QCOMPARE(sleepMinutesForIndex(0), -1);
        QCOMPARE(sleepMinutesForIndex(4), 30);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(Ak820Tests)
#include "ak820_tests.moc"
