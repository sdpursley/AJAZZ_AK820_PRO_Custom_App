// ak820 — lighting, sleep, clock, and screen for the AJAZZ AK820 Pro.

#include <QDateTime>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include "backend.h"
#include "keyboard.h"
#include "theme.h"
#include "translator.h"

#include <cstdio>
#include <cstring>

namespace {

QString opt(const QStringList &args, const QString &name, const QString &fallback = {}) {
    const int index = args.indexOf(name);
    return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : fallback;
}

int finish(bool json, const Attempt &attempt) {
    if (json) {
        QJsonObject object;
        object.insert("ok", attempt.ok);
        object.insert(attempt.ok ? "message" : "error", attempt.message);
        fprintf(stdout, "%s\n", QJsonDocument(object).toJson(QJsonDocument::Compact).constData());
    } else if (!attempt.message.isEmpty()) {
        fprintf(attempt.ok ? stdout : stderr, "%s\n", qUtf8Printable(attempt.message));
    }
    return attempt.ok ? 0 : 1;
}

int usage() {
    fprintf(stderr, "%s\n",
            qUtf8Printable(QCoreApplication::translate(
                "main",
                "Usage:\n"
                "  ak820                         open the window\n"
                "  ak820 probe --json\n"
                "  ak820 lighting --mode static --color ff8800 --brightness 3 --speed 2 --direction left --rainbow --json\n"
                "  ak820 sleep --minutes 0|1|5|30 --json\n"
                "  ak820 clock --json\n"
                "  ak820 screen --image picture.png --json\n"
                "  ak820 setup                   print the command that lets your user open the keyboard")));
    return 1;
}

int runCli(const QStringList &args) {
    const bool json = args.contains("--json");
    const QString command = args.value(1);
    if (command == "probe")
        return finish(json, Keyboard().describe());
    if (command == "clock") {
        const QDateTime now = QDateTime::currentDateTime();
        return finish(json, Keyboard().syncClock(now.date().year(), now.date().month(), now.date().day(),
                                                 now.time().hour(), now.time().minute(), now.time().second()));
    }
    if (command == "sleep") {
        bool numeric = false;
        const int minutes = opt(args, "--minutes").toInt(&numeric);
        if (!numeric)
            return finish(json, {false, -1, QCoreApplication::translate("Keyboard", "Sleep must be never, 1, 5, or 30 minutes.")});
        return finish(json, Keyboard().setSleep(minutes));
    }
    if (command == "screen") {
        const QString path = opt(args, "--image");
        if (path.isEmpty())
            return usage();
        QImage image(path);
        if (image.isNull())
            return finish(json, {false, -1, QCoreApplication::translate("Keyboard", "That file is not an image.")});
        image = image.convertToFormat(QImage::Format_RGB888);
        const int side = qMin(image.width(), image.height());
        if (side < 1)
            return finish(json, {false, -1, QCoreApplication::translate("Keyboard", "That file is not an image.")});
        image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
        image = image.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB888);
        QByteArray rgb(128 * 128 * 3, 0);
        for (int y = 0; y < image.height(); ++y)
            memcpy(rgb.data() + y * 128 * 3, image.constScanLine(y), 128 * 3);
        return finish(json, Keyboard().uploadStill(rgb));
    }
    if (command == "lighting") {
        const int mode = modeFromName(opt(args, "--mode", "static"));
        const int direction = directionFromName(opt(args, "--direction", "left"));
        QString color = opt(args, "--color", "ffffff");
        if (color.startsWith('#'))
            color.remove(0, 1);
        bool colorOk = false;
        const int rgb = color.toInt(&colorOk, 16);
        bool brightOk = false, speedOk = false;
        const int brightness = opt(args, "--brightness", "3").toInt(&brightOk);
        const int speed = opt(args, "--speed", "2").toInt(&speedOk);
        if (mode < 0 || direction < 0 || !colorOk || color.size() != 6 || !brightOk || !speedOk)
            return usage();
        Light light;
        light.mode = mode;
        light.red = (rgb >> 16) & 255;
        light.green = (rgb >> 8) & 255;
        light.blue = rgb & 255;
        light.rainbow = args.contains("--rainbow");
        light.brightness = brightness;
        light.speed = speed;
        light.direction = direction;
        return finish(json, Keyboard().setLighting(light));
    }
    if (command == "setup") {
        fprintf(stdout, "%s\n",
                qUtf8Printable(QCoreApplication::translate(
                    "main", "Run bin/setup-udev from the project directory, then unplug the keyboard and plug it back in.")));
        return 0;
    }
    return usage();
}

} // namespace

int main(int argc, char *argv[]) {
    QStringList early;
    for (int i = 1; i < argc; ++i)
        early << QString::fromLocal8Bit(argv[i]);
    const bool gui = early.isEmpty() || early.first() == "open";
    if (!gui)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("ak820");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("ak820");
    app.setWindowIcon(QIcon::fromTheme("ak820"));

    const QString languageFile = qEnvironmentVariable("AK820_LANGUAGE_FILE");
    Translator translator(languageFile);
    app.installTranslator(&translator);
    if (!gui)
        return runCli(app.arguments());

    QQuickStyle::setStyle("Material");
    Theme theme(qEnvironmentVariable("AK820_THEME_DIR"));
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (qEnvironmentVariableIsSet("AK820_SHOT")) {
        const QString path = qEnvironmentVariable("AK820_SHOT");
        QTimer::singleShot(400, &app, [path, &engine] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
            const bool saved = window && window->grabWindow().save(path);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}
