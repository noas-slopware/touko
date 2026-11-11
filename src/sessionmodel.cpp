// SPDX-License-Identifier: GPL-2.0-or-later
#include "sessionmodel.h"

SessionModel::SessionModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_sessions.size());
}

QVariant SessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_sessions.size())
        return {};
    TerminalSession *s = m_sessions.at(index.row());
    switch (role) {
    case SessionRole: return QVariant::fromValue(s);
    case Qt::DisplayRole:
    case TitleRole: return s->title();
    case RunningRole: return s->running();
    case BusyRole: return s->busy();
    default: return {};
    }
}

QHash<int, QByteArray> SessionModel::roleNames() const
{
    return {{SessionRole, "session"}, {TitleRole, "title"}, {RunningRole, "running"}, {BusyRole, "busy"}};
}

TerminalSession *SessionModel::add(TerminalSession *s)
{
    const int row = int(m_sessions.size());
    beginInsertRows({}, row, row);
    m_sessions.append(s);
    endInsertRows();
    auto changed = [this, s](const QList<int> &roles) {
        const int i = int(m_sessions.indexOf(s));
        if (i >= 0)
            Q_EMIT dataChanged(index(i), index(i), roles);
    };
    connect(s, &TerminalSession::titleChanged, this, [changed] { changed({TitleRole, Qt::DisplayRole}); });
    connect(s, &TerminalSession::runningChanged, this, [changed] { changed({RunningRole}); });
    connect(s, &TerminalSession::busyChanged, this, [changed] { changed({BusyRole}); });
    connect(s, &TerminalSession::finished, this, [this, s] { Q_EMIT sessionFinished(s); });
    Q_EMIT countChanged();
    return s;
}

TerminalSession *SessionModel::newSession(const QString &workingDirectory)
{
    auto *s = new TerminalSession(this);
    add(s);
    s->start({}, {}, workingDirectory);
    return s;
}

TerminalSession *SessionModel::newSessionWithCommand(const QStringList &command, const QString &workingDirectory)
{
    auto *s = new TerminalSession(this);
    add(s);
    if (command.isEmpty())
        s->start({}, {}, workingDirectory);
    else
        s->start(command.first(), command.mid(1), workingDirectory);
    return s;
}

void SessionModel::closeSession(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;
    beginRemoveRows({}, index, index);
    TerminalSession *s = m_sessions.takeAt(index);
    endRemoveRows();
    s->hangup();
    s->deleteLater();
    Q_EMIT countChanged();
}

TerminalSession *SessionModel::sessionAt(int index) const
{
    return m_sessions.value(index, nullptr);
}

int SessionModel::indexOf(TerminalSession *s) const
{
    return int(m_sessions.indexOf(s));
}
