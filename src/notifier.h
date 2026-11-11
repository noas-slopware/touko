// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Desktop notifications (OSC 9 / OSC 777 from programs, "command finished")
// over org.freedesktop.Notifications.
class Notifier : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE void notify(const QString &title, const QString &body);
    Q_INVOKABLE QStringList monospaceFonts() const;
};
