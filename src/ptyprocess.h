// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

class QSocketNotifier;

// A pseudo terminal with a child process on the other end.
// Reads are delivered through dataReady(); writes are queued and drained
// when the master becomes writable, so a slow child never blocks the UI.
class Pty : public QObject
{
    Q_OBJECT
public:
    explicit Pty(QObject *parent = nullptr);
    ~Pty() override;

    bool start(const QString &program, const QStringList &args, const QString &workingDirectory,
               const QStringList &extraEnv, quint16 cols, quint16 rows, quint32 widthPx, quint32 heightPx);
    void write(const char *data, qsizetype len);
    void resize(quint16 cols, quint16 rows, quint32 widthPx, quint32 heightPx);
    void hangup();

    bool running() const { return m_pid > 0; }
    pid_t pid() const { return m_pid; }
    // The process group in the foreground of the pty (for "is something running").
    pid_t foregroundPid() const;

Q_SIGNALS:
    void dataReady(const QByteArray &data);
    void finished(int exitCode);

private:
    void onReadable();
    void onWritable();
    void onChildExited();

    int m_fd = -1;
    pid_t m_pid = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QSocketNotifier *m_exitNotifier = nullptr;
    int m_pidfd = -1;
    QByteArray m_pending;
    QByteArray m_readBuffer;
};
