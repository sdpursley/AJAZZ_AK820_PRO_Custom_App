#include "backend.h"

#include "keyboard.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QImage>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <QUrl>

#include <cstring>
#include <functional>

QString Backend::imageUrl() const {
    return m_imagePath.isEmpty() ? QString() : QUrl::fromLocalFile(m_imagePath).toString();
}

QStringList Backend::modeNames() const { return ::modeNames(); }
QStringList Backend::directionNames() const { return ::directionNames(); }
QStringList Backend::sleepNames() const { return ::sleepNames(); }

void Backend::run(const std::function<Attempt()> &work, bool imageSend) {
    if (m_busy)
        return;
    m_busy = true;
    emit changed();
    auto *thread = QThread::create([this, work, imageSend] {
        const Attempt attempt = work();
        QMetaObject::invokeMethod(this, [this, attempt, imageSend] {
            m_busy = false;
            m_status = attempt.message;
            if (attempt.connected >= 0)
                m_connected = attempt.connected == 1;
            if (imageSend)
                m_imageOnKeyboard = attempt.ok;
            emit changed();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Backend::refresh() {
    run([] { return Keyboard().describe(); });
}

void Backend::save(int mode, int red, int green, int blue, bool rainbow, int brightness, int speed,
                   int directionIndex, int sleepIndex) {
    const Light light{mode, red, green, blue, rainbow, brightness, speed, directionForIndex(directionIndex)};
    const int minutes = sleepMinutesForIndex(sleepIndex);
    run([light, minutes] {
        Keyboard keyboard;
        const Attempt lighting = keyboard.setLighting(light);
        if (!lighting.ok || minutes < 0)
            return lighting;
        const Attempt sleep = keyboard.setSleep(minutes);
        return sleep.ok ? lighting : sleep;
    });
}

void Backend::sendScreen() {
    if (m_imagePath.isEmpty()) {
        m_status = tr("Choose an image first.");
        emit changed();
        return;
    }
    const QString path = m_imagePath;
    run([path] {
        QImage image(path);
        if (image.isNull())
            return Attempt{false, -1, tr("That file is not an image.")};
        image = image.convertToFormat(QImage::Format_RGB888);
        const int side = qMin(image.width(), image.height());
        if (side < 1)
            return Attempt{false, -1, tr("That file is not an image.")};
        image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
        image = image.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .convertToFormat(QImage::Format_RGB888);
        QByteArray rgb(128 * 128 * 3, 0);
        for (int y = 0; y < 128; ++y)
            memcpy(rgb.data() + y * 128 * 3, image.constScanLine(y), 128 * 3);
        return Keyboard().uploadStill(rgb);
    }, true);
}

void Backend::syncClock() {
    run([] {
        const QDateTime now = QDateTime::currentDateTime();
        const QDate date = now.date();
        const QTime time = now.time();
        return Keyboard().syncClock(date.year(), date.month(), date.day(), time.hour(), time.minute(),
                                    time.second());
    });
}

void Backend::setImagePath(const QString &path) {
    m_imagePath = path.trimmed();
    m_imageOnKeyboard = false;
    emit changed();
}

void Backend::chooseImage() {
    auto bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("ak820%1").arg(QRandomGenerator::global()->bounded(1000000));
    QString sender = bus.baseService();
    if (sender.startsWith(':'))
        sender.remove(0, 1);
    sender.replace('.', '_');
    const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect("org.freedesktop.portal.Desktop", path, "org.freedesktop.portal.Request", "Response",
                this, SLOT(portalResponse(uint,QVariantMap)));

    QVariantMap options;
    options.insert("handle_token", token);
    QDBusMessage message = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser", "OpenFile");
    message << QString() << tr("Choose image") << options;
    const QDBusMessage reply = bus.call(message, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        m_status = tr("Could not open the file chooser (%1).").arg(reply.errorMessage());
        emit changed();
    }
}

void Backend::portalResponse(uint response, QVariantMap results) {
    if (response != 0)
        return;
    const QStringList uris = results.value("uris").toStringList();
    if (uris.isEmpty())
        return;
    setImagePath(QUrl(uris.first()).toLocalFile());
}
