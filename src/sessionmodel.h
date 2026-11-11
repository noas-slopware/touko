// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include "terminalsession.h"

// The open tabs.
class SessionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { SessionRole = Qt::UserRole + 1, TitleRole, RunningRole, BusyRole };
    explicit SessionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_sessions.size()); }

    // Starts the user's shell, or `command` through it when given.
    Q_INVOKABLE TerminalSession *newSession(const QString &workingDirectory = {});
    Q_INVOKABLE TerminalSession *newSessionWithCommand(const QStringList &command, const QString &workingDirectory = {});
    Q_INVOKABLE void closeSession(int index);
    Q_INVOKABLE TerminalSession *sessionAt(int index) const;
    Q_INVOKABLE int indexOf(TerminalSession *s) const;

Q_SIGNALS:
    void countChanged();
    void sessionFinished(TerminalSession *session);

private:
    TerminalSession *add(TerminalSession *s);
    QList<TerminalSession *> m_sessions;
};
