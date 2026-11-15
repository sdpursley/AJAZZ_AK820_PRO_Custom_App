#pragma once

#include "keyboard.h"

#include <QObject>
#include <QVariantMap>

#include <functional>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString imagePath READ imagePath NOTIFY changed)
    Q_PROPERTY(QString imageUrl READ imageUrl NOTIFY changed)
    Q_PROPERTY(bool imageOnKeyboard READ imageOnKeyboard NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr) : QObject(parent) {}

    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool connected() const { return m_connected; }
    QString imagePath() const { return m_imagePath; }
    QString imageUrl() const;
    bool imageOnKeyboard() const { return m_imageOnKeyboard; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void save(int mode, int red, int green, int blue, bool rainbow, int brightness, int speed,
                          int directionIndex, int sleepIndex);
    Q_INVOKABLE void sendScreen();
    Q_INVOKABLE void syncClock();
    Q_INVOKABLE void chooseImage();
    Q_INVOKABLE void setImagePath(const QString &path);
    Q_INVOKABLE QStringList modeNames() const;
    Q_INVOKABLE QStringList directionNames() const;
    Q_INVOKABLE QStringList sleepNames() const;

signals:
    void changed();

public slots:
    void portalResponse(uint response, QVariantMap results);

private:
    void run(const std::function<Attempt()> &work, bool imageSend = false);

    QString m_status;
    QString m_imagePath;
    bool m_busy = false;
    bool m_connected = false;
    bool m_imageOnKeyboard = false;
};
