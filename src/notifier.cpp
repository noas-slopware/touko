// SPDX-License-Identifier: GPL-2.0-or-later
#include "notifier.h"

#include "config.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFontDatabase>
#include <QVariantMap>

void Notifier::notify(const QString &title, const QString &body)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("/org/freedesktop/Notifications"),
                                                      QStringLiteral("org.freedesktop.Notifications"),
                                                      QStringLiteral("Notify"));
    QVariantMap hints;
    hints[QStringLiteral("desktop-entry")] = QStringLiteral(TOUKO_APP_ID);
    msg << QStringLiteral(TOUKO_APP_NAME) << uint(0) << QStringLiteral(TOUKO_APP_ID)
        << (title.isEmpty() ? QStringLiteral(TOUKO_APP_NAME) : title) << body << QStringList() << hints << int(-1);
    QDBusConnection::sessionBus().asyncCall(msg);
}

QStringList Notifier::monospaceFonts() const
{
    QStringList out;
    for (const QString &f : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(f) && !QFontDatabase::isPrivateFamily(f))
            out << f;
    }
    return out;
}
