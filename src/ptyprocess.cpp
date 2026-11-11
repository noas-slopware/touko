// SPDX-License-Identifier: GPL-2.0-or-later
#include "ptyprocess.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {
int pidfdOpen(pid_t pid)
{
    return int(syscall(SYS_pidfd_open, pid, 0));
}
}

Pty::Pty(QObject *parent)
    : QObject(parent)
{
    m_readBuffer.resize(64 * 1024);
}

Pty::~Pty()
{
    hangup();
}

bool Pty::start(const QString &program, const QStringList &args, const QString &workingDirectory,
                const QStringList &extraEnv, quint16 cols, quint16 rows, quint32 widthPx, quint32 heightPx)
{
    struct winsize ws {};
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = quint16(qMin<quint32>(widthPx, 0xffff));
    ws.ws_ypixel = quint16(qMin<quint32>(heightPx, 0xffff));

    // Everything the child needs is prepared before fork(): only
    // async-signal-safe calls happen between fork() and exec().
    const QByteArray prog = program.toLocal8Bit();
    QList<QByteArray> argStore;
    argStore.reserve(args.size() + 1);
    argStore << prog;
    for (const QString &a : args)
        argStore << a.toLocal8Bit();
    std::vector<char *> argv;
    for (QByteArray &a : argStore)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    QList<QByteArray> envStore;
    for (char **e = environ; *e; ++e) {
        const QByteArray kv(*e);
        const QByteArray key = kv.left(kv.indexOf('='));
        bool overridden = false;
        for (const QString &x : extraEnv) {
            if (x.toLocal8Bit().startsWith(key + '=')) {
                overridden = true;
                break;
            }
        }
        // Not ours to pass on: the child is not a session of this desktop
        // file's activation, and it would confuse a nested terminal.
        if (!overridden && key != "DESKTOP_STARTUP_ID" && key != "XDG_ACTIVATION_TOKEN")
            envStore << kv;
    }
    for (const QString &x : extraEnv)
        envStore << x.toLocal8Bit();
    std::vector<char *> envp;
    for (QByteArray &e : envStore)
        envp.push_back(e.data());
    envp.push_back(nullptr);
    const QByteArray wd = workingDirectory.toLocal8Bit();

    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0)
        return false;
    if (pid == 0) {
        // Child. Restore default signal dispositions Qt may have changed.
        struct sigaction sa {};
        sa.sa_handler = SIG_DFL;
        for (int s : {SIGCHLD, SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP})
            sigaction(s, &sa, nullptr);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        if (!wd.isEmpty() && chdir(wd.constData()) != 0) {
            if (const char *home = getenv("HOME"))
                (void)chdir(home);
        }
        execve(argv[0], argv.data(), envp.data());
        execvpe(argv[0], argv.data(), envp.data());
        _exit(127);
    }

    m_fd = master;
    m_pid = pid;
    fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
    fcntl(m_fd, F_SETFD, FD_CLOEXEC);

    m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &Pty::onReadable);
    m_writeNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &Pty::onWritable);

    // The exit is an event on a pidfd, not something we poll for.
    m_pidfd = pidfdOpen(pid);
    if (m_pidfd >= 0) {
        m_exitNotifier = new QSocketNotifier(m_pidfd, QSocketNotifier::Read, this);
        connect(m_exitNotifier, &QSocketNotifier::activated, this, &Pty::onChildExited);
    }
    return true;
}

void Pty::write(const char *data, qsizetype len)
{
    if (m_fd < 0 || len <= 0)
        return;
    if (m_pending.isEmpty()) {
        const ssize_t n = ::write(m_fd, data, size_t(len));
        if (n == len)
            return;
        if (n > 0) {
            data += n;
            len -= n;
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
            return;
        }
    }
    m_pending.append(data, len);
    m_writeNotifier->setEnabled(true);
}

void Pty::onWritable()
{
    while (!m_pending.isEmpty()) {
        const ssize_t n = ::write(m_fd, m_pending.constData(), size_t(m_pending.size()));
        if (n > 0) {
            m_pending.remove(0, n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return;
        m_pending.clear();
        break;
    }
    m_writeNotifier->setEnabled(false);
}

void Pty::onReadable()
{
    // Drain what is there now, up to a budget, and hand it on in one piece:
    // the terminal parses it in one call and the view renders one frame.
    QByteArray out;
    for (int i = 0; i < 16; ++i) {
        const ssize_t n = ::read(m_fd, m_readBuffer.data(), size_t(m_readBuffer.size()));
        if (n > 0) {
            out.append(m_readBuffer.constData(), n);
            if (n < m_readBuffer.size())
                break;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            break;
        // EIO: every slave fd is closed. The pidfd tells us about the exit.
        m_readNotifier->setEnabled(false);
        if (m_pidfd < 0)
            onChildExited();
        break;
    }
    if (!out.isEmpty())
        Q_EMIT dataReady(out);
}

void Pty::onChildExited()
{
    if (m_pid <= 0)
        return;
    int status = 0;
    // Without a pidfd (old kernel) EIO is all we get, and the child may
    // still be on its way out: wait for it then, it is a matter of moments.
    const pid_t r = waitpid(m_pid, &status, m_pidfd >= 0 ? WNOHANG : 0);
    if (r == 0)
        return;
    if (m_exitNotifier) {
        m_exitNotifier->setEnabled(false);
        m_exitNotifier->deleteLater();
        m_exitNotifier = nullptr;
    }
    if (m_pidfd >= 0) {
        ::close(m_pidfd);
        m_pidfd = -1;
    }
    // Whatever the shell printed last is still in the master.
    if (m_readNotifier && m_readNotifier->isEnabled())
        onReadable();
    m_pid = -1;
    int code = -1;
    if (r > 0) {
        if (WIFEXITED(status))
            code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            code = 128 + WTERMSIG(status);
    }
    if (m_readNotifier)
        m_readNotifier->setEnabled(false);
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(false);
    Q_EMIT finished(code);
}

void Pty::resize(quint16 cols, quint16 rows, quint32 widthPx, quint32 heightPx)
{
    if (m_fd < 0)
        return;
    struct winsize ws {};
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = quint16(qMin<quint32>(widthPx, 0xffff));
    ws.ws_ypixel = quint16(qMin<quint32>(heightPx, 0xffff));
    ioctl(m_fd, TIOCSWINSZ, &ws);
}

pid_t Pty::foregroundPid() const
{
    if (m_fd < 0)
        return -1;
    return tcgetpgrp(m_fd);
}

void Pty::hangup()
{
    if (m_pid > 0) {
        ::kill(m_pid, SIGHUP);
        // Reap it when it goes, without blocking the UI and without leaving
        // a zombie: hand the pidfd to a notifier that outlives this object.
        if (m_pidfd >= 0 && QCoreApplication::instance()) {
            const pid_t pid = m_pid;
            const int pidfd = m_pidfd;
            auto *reaper = new QSocketNotifier(pidfd, QSocketNotifier::Read, QCoreApplication::instance());
            QObject::connect(reaper, &QSocketNotifier::activated, reaper, [reaper, pid, pidfd] {
                waitpid(pid, nullptr, WNOHANG);
                ::close(pidfd);
                reaper->setEnabled(false);
                reaper->deleteLater();
            });
            m_pidfd = -1;
        }
        m_pid = -1;
    }
    delete m_exitNotifier;
    m_exitNotifier = nullptr;
    if (m_pidfd >= 0) {
        ::close(m_pidfd);
        m_pidfd = -1;
    }
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}
