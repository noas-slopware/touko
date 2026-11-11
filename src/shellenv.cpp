// SPDX-License-Identifier: GPL-2.0-or-later
#include "shellenv.h"

#include "config.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <pwd.h>
#include <unistd.h>

namespace {

QString loginShell()
{
    const QByteArray env = qgetenv("SHELL");
    if (!env.isEmpty() && QFileInfo(QString::fromLocal8Bit(env)).isExecutable())
        return QString::fromLocal8Bit(env);
    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell)
        return QString::fromLocal8Bit(pw->pw_shell);
    return QStringLiteral("/bin/sh");
}

bool hasTerminfo(const QString &name)
{
    const QString sub = name.left(1) + u'/' + name;
    QStringList dirs;
    if (const QByteArray t = qgetenv("TERMINFO"); !t.isEmpty())
        dirs << QString::fromLocal8Bit(t);
    dirs << QDir::homePath() + QStringLiteral("/.terminfo");
    for (const QByteArray &d : qgetenv("TERMINFO_DIRS").split(':')) {
        if (!d.isEmpty())
            dirs << QString::fromLocal8Bit(d);
    }
    dirs << QStringLiteral("/usr/share/terminfo") << QStringLiteral("/usr/lib/terminfo") << QStringLiteral("/etc/terminfo");
    for (const QString &d : dirs) {
        if (QFileInfo::exists(d + u'/' + sub))
            return true;
    }
    return false;
}

QString ghosttyResources()
{
    if (const QByteArray d = qgetenv("GHOSTTY_RESOURCES_DIR"); !d.isEmpty())
        return QString::fromLocal8Bit(d);
    for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        if (QFileInfo::exists(d + QStringLiteral("/ghostty/shell-integration")))
            return d + QStringLiteral("/ghostty");
    }
    return {};
}

} // namespace

ShellCommand shellCommand(const QString &program, const QStringList &args)
{
    ShellCommand cmd;
    cmd.program = program.isEmpty() ? loginShell() : program;
    cmd.args = args;

    const QProcessEnvironment sys = QProcessEnvironment::systemEnvironment();
    const QString term = hasTerminfo(QStringLiteral("xterm-ghostty")) ? QStringLiteral("xterm-ghostty")
                                                                       : QStringLiteral("xterm-256color");
    cmd.env << QStringLiteral("TERM=") + term << QStringLiteral("COLORTERM=truecolor")
            << QStringLiteral("TERM_PROGRAM=" TOUKO_APP_NAME) << QStringLiteral("TERM_PROGRAM_VERSION=" TOUKO_VERSION);

    const QString resources = ghosttyResources();
    if (resources.isEmpty() || !args.isEmpty())
        return cmd;
    const QString integ = resources + QStringLiteral("/shell-integration");
    cmd.env << QStringLiteral("GHOSTTY_RESOURCES_DIR=") + resources << QStringLiteral("GHOSTTY_SHELL_FEATURES=title");

    const QString shell = QFileInfo(cmd.program).fileName();
    if (shell == u"fish" || shell == u"elvish" || shell == u"nu") {
        const QString cur = sys.value(QStringLiteral("XDG_DATA_DIRS"), QStringLiteral("/usr/local/share:/usr/share"));
        cmd.env << QStringLiteral("GHOSTTY_SHELL_INTEGRATION_XDG_DIR=") + integ
                << QStringLiteral("XDG_DATA_DIRS=") + integ + u':' + cur;
    } else if (shell == u"zsh") {
        if (sys.contains(QStringLiteral("ZDOTDIR")))
            cmd.env << QStringLiteral("GHOSTTY_ZSH_ZDOTDIR=") + sys.value(QStringLiteral("ZDOTDIR"));
        cmd.env << QStringLiteral("ZDOTDIR=") + integ + QStringLiteral("/zsh");
    } else if (shell == u"bash") {
        const QString script = integ + QStringLiteral("/bash/ghostty.bash");
        if (!QFileInfo::exists(script))
            return cmd;
        cmd.args = {QStringLiteral("--posix")};
        if (sys.contains(QStringLiteral("ENV")))
            cmd.env << QStringLiteral("GHOSTTY_BASH_ENV=") + sys.value(QStringLiteral("ENV"));
        cmd.env << QStringLiteral("ENV=") + script << QStringLiteral("GHOSTTY_BASH_INJECT=1");
        if (!sys.contains(QStringLiteral("HISTFILE")))
            cmd.env << QStringLiteral("HISTFILE=") + QDir::homePath() + QStringLiteral("/.bash_history")
                    << QStringLiteral("GHOSTTY_BASH_UNEXPORT_HISTFILE=1");
    }
    return cmd;
}
