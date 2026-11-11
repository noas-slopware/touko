// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <ghostty/vt.h>

#include "theme.h"

class Pty;

// One shell: a libghostty-vt terminal, the pty it talks to, and the input
// encoders. Everything runs on the GUI thread.
class TerminalSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Sessions come from the SessionModel")

    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY runningChanged)
    Q_PROPERTY(int columns READ columns NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool mouseTracking READ mouseTracking NOTIFY modesChanged)
    Q_PROPERTY(bool alternateScreen READ alternateScreen NOTIFY modesChanged)
    Q_PROPERTY(bool atBottom READ atBottom NOTIFY scrollChanged)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition NOTIFY scrollChanged)
    Q_PROPERTY(qreal scrollSize READ scrollSize NOTIFY scrollChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    bool start(const QString &program = {}, const QStringList &args = {}, const QString &workingDirectory = {});

    GhosttyTerminal terminal() const { return m_terminal; }

    QString title() const;
    QString workingDirectory() const { return m_pwd; }
    bool running() const { return m_running; }
    int exitCode() const { return m_exitCode; }
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }
    bool hasSelection() const;
    bool mouseTracking() const;
    bool alternateScreen() const;
    bool atBottom() const;
    qreal scrollPosition() const;
    qreal scrollSize() const;
    bool busy() const { return m_busy; }

    void resize(int cols, int rows, int cellWidthPx, int cellHeightPx);
    void applyTheme(const Theme &theme, bool dark);

    // Input. All of these scroll the viewport back to the bottom.
    // Text that came from an input method: written as typed.
    void sendText(const QString &text);
    // A key, through the ghostty encoder (legacy, modifyOtherKeys or kitty,
    // whatever the application asked for).
    bool sendKey(GhosttyKey key, GhosttyMods mods, const QString &text, uint unshiftedCodepoint,
                 GhosttyKeyAction action = GHOSTTY_KEY_ACTION_PRESS);
    bool sendMouse(GhosttyMouseAction action, GhosttyMouseButton button, QPointF pos, GhosttyMods mods,
                   QSizeF screen, QSizeF cell);
    void sendFocus(bool focused);
    void writeRaw(const QByteArray &bytes);

    Q_INVOKABLE void scrollLines(int delta);
    Q_INVOKABLE void scrollToRow(qreal row);
    Q_INVOKABLE void scrollToBottom();
    Q_INVOKABLE void scrollPrompt(int delta);

    // Selection, in viewport cell coordinates.
    void selectWordAt(int col, int row);
    void selectRange(QPoint anchor, QPoint to, bool words);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void copySelection();
    // Returns false when the text could run commands and allowUnsafe was not set.
    Q_INVOKABLE bool paste(const QString &text, bool allowUnsafe);
    Q_INVOKABLE bool pasteClipboard(bool allowUnsafe);
    Q_INVOKABLE QString hyperlinkAt(int col, int row) const;
    Q_INVOKABLE void hangup();

    bool selectionRange(QPoint *start, QPoint *end) const;

Q_SIGNALS:
    void titleChanged();
    void workingDirectoryChanged();
    void runningChanged();
    void sizeChanged();
    void selectionChanged();
    void modesChanged();
    void scrollChanged();
    void busyChanged();
    void contentChanged();
    void bell();
    void notify(const QString &title, const QString &body);
    void finished();

private:
    static void cbWritePty(GhosttyTerminal, void *ud, const uint8_t *data, size_t len);
    static void cbBell(GhosttyTerminal, void *ud);
    static void cbTitle(GhosttyTerminal, void *ud);
    static void cbPwd(GhosttyTerminal, void *ud);
    static bool cbSize(GhosttyTerminal, void *ud, GhosttySizeReportSize *out);
    static bool cbColorScheme(GhosttyTerminal, void *ud, GhosttyColorScheme *out);
    static bool cbDeviceAttributes(GhosttyTerminal, void *ud, GhosttyDeviceAttributes *out);
    static GhosttyString cbXtversion(GhosttyTerminal, void *ud);
    static void cbClipboardWrite(GhosttyTerminal, void *ud, const GhosttyClipboardWrite *write);
    static void cbClipboardRead(GhosttyTerminal, void *ud, const GhosttyClipboardRead *read);
    static void cbNotification(GhosttyTerminal, void *ud, const GhosttyTerminalDesktopNotification *n);

    bool spawn();
    void onData(const QByteArray &data);
    void afterMutation();
    GhosttyGridRef refAt(int col, int row) const;
    void updateBusy();

    GhosttyTerminal m_terminal = nullptr;
    GhosttyKeyEncoder m_keyEncoder = nullptr;
    GhosttyKeyEvent m_keyEvent = nullptr;
    GhosttyMouseEncoder m_mouseEncoder = nullptr;
    GhosttyMouseEvent m_mouseEvent = nullptr;
    Pty *m_pty = nullptr;

    QString m_pwd;
    QString m_processName;
    bool m_running = false;
    bool m_busy = false;
    bool m_dark = false;
    int m_exitCode = 0;
    int m_cols = 80;
    int m_rows = 24;
    int m_cellW = 10;
    int m_cellH = 20;
    bool m_lastAlt = false;
    bool m_lastMouse = false;
    bool m_lastSelection = false;
    quint64 m_lastScrollTotal = 0;
    quint64 m_lastScrollOffset = 0;
    QByteArray m_xtversion;
    // The shell starts once a view has given the terminal its real size, so
    // it never draws its first prompt for 80x24 and then reflows.
    bool m_sized = false;
    bool m_startPending = false;
    QString m_program;
    QStringList m_args;
    QString m_startDir;
};
