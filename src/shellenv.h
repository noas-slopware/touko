// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringList>

struct ShellCommand {
    QString program;
    QStringList args;
    QStringList env; // KEY=VALUE entries added to (or replacing) the inherited environment
};

// The user's shell with TERM and friends set, and Ghostty's shell
// integration (prompt marks, working directory, title) injected the way
// Ghostty does it, when its scripts are installed.
ShellCommand shellCommand(const QString &program, const QStringList &args);
