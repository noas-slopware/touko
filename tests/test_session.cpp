// SPDX-License-Identifier: GPL-2.0-or-later
// Headless checks of the libghostty-vt + pty glue: no window, no device.
#include <QSignalSpy>
#include <QTest>

#include "terminalsession.h"

namespace {

QString screenText(TerminalSession &s)
{
    GhosttyFormatterTerminalOptions opts = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
    opts.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    opts.trim = true;
    GhosttyFormatter f = nullptr;
    if (ghostty_formatter_terminal_new(nullptr, &f, s.terminal(), opts) != GHOSTTY_SUCCESS)
        return {};
    uint8_t *buf = nullptr;
    size_t len = 0;
    ghostty_formatter_format_alloc(f, nullptr, &buf, &len);
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(buf), qsizetype(len));
    ghostty_free(nullptr, buf, len);
    ghostty_formatter_free(f);
    return text;
}

// Runs a shell snippet that reads `count` raw bytes from the terminal and
// prints them as hex, so input encoding can be checked byte for byte.
QString hexOfInput(const std::function<void(TerminalSession &)> &send, int count)
{
    TerminalSession s;
    s.resize(80, 24, 10, 20);
    const QString script = QStringLiteral("stty raw -echo; printf READY; dd bs=1 count=%1 2>/dev/null | od -An -tx1 | tr -d '\\n'; printf END").arg(count);
    s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
    QSignalSpy finished(&s, &TerminalSession::finished);
    if (!QTest::qWaitFor([&] { return screenText(s).contains(u"READY"); }, 5000))
        return QStringLiteral("no READY");
    send(s);
    if (!finished.wait(5000))
        return QStringLiteral("timeout: ") + screenText(s);
    QString t = screenText(s);
    t = t.mid(t.indexOf(u"READY") + 5);
    return t.left(t.indexOf(u"END")).simplified();
}

} // namespace

class TestSession : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void output()
    {
        TerminalSession s;
        s.resize(40, 10, 10, 20);
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("printf 'hello \\033[1;31mred\\033[0m 허\\n'")});
        QVERIFY(finished.wait(5000));
        QVERIFY2(screenText(s).contains(u"hello red 허"), qPrintable(screenText(s)));
        QCOMPARE(s.exitCode(), 0);
    }

    void exitCode()
    {
        TerminalSession s;
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("exit 3")});
        QVERIFY(finished.wait(5000));
        QCOMPARE(s.exitCode(), 3);
        QVERIFY(!s.running());
    }

    void terminalEnv()
    {
        TerminalSession s;
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("printf '%s|%s|' \"$TERM\" \"$COLORTERM\"; stty size")});
        QVERIFY(finished.wait(5000));
        const QString t = screenText(s);
        QVERIFY2(t.contains(u"|truecolor|24 80"), qPrintable(t));
        QVERIFY2(t.contains(u"xterm-ghostty|") || t.contains(u"xterm-256color|"), qPrintable(t));
    }

    void resizeReachesChild()
    {
        TerminalSession s;
        s.resize(50, 12, 10, 20);
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("stty size")});
        QVERIFY(finished.wait(5000));
        QVERIFY2(screenText(s).contains(u"12 50"), qPrintable(screenText(s)));
    }

    void keysAndText()
    {
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendKey(GHOSTTY_KEY_A, 0, QStringLiteral("a"), 'a'); }, 1),
                 QStringLiteral("61"));
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendKey(GHOSTTY_KEY_C, GHOSTTY_MODS_CTRL, {}, 'c'); }, 1),
                 QStringLiteral("03"));
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendKey(GHOSTTY_KEY_ENTER, 0, {}, 0); }, 1), QStringLiteral("0d"));
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendKey(GHOSTTY_KEY_ARROW_UP, 0, {}, 0); }, 3),
                 QStringLiteral("1b 5b 41"));
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendKey(GHOSTTY_KEY_X, GHOSTTY_MODS_ALT, QStringLiteral("x"), 'x'); }, 2),
                 QStringLiteral("1b 78"));
        QCOMPARE(hexOfInput([](TerminalSession &s) { s.sendText(QStringLiteral("허")); }, 3), QStringLiteral("ed 97 88"));
    }

    void deviceAttributes()
    {
        // The shell asks for DA1 and prints what came back, as hex.
        TerminalSession s;
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral("stty raw -echo; printf '\\033[c'; dd bs=1 count=12 2>/dev/null | od -An -c | tr -d ' \\n'")});
        QVERIFY(finished.wait(5000));
        QVERIFY2(screenText(s).contains(u"033[?62;22;52c"), qPrintable(screenText(s)));
    }

    void pasteSafety()
    {
        TerminalSession s;
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 2")});
        QVERIFY(!s.paste(QStringLiteral("rm -rf ~\n"), false));
        QVERIFY(s.paste(QStringLiteral("echo safe"), false));
        QVERIFY(s.paste(QStringLiteral("two\nlines"), true));
    }

    void selection()
    {
        TerminalSession s;
        s.resize(40, 5, 10, 20);
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("printf 'git status --short'")});
        QVERIFY(finished.wait(5000));
        s.selectWordAt(5, 0);
        QCOMPARE(s.selectedText(), QStringLiteral("status"));
        s.selectRange(QPoint(1, 0), QPoint(5, 0), true);
        QCOMPARE(s.selectedText(), QStringLiteral("git status"));
        QPoint a, b;
        QVERIFY(s.selectionRange(&a, &b));
        QCOMPARE(a, QPoint(0, 0));
        QCOMPARE(b, QPoint(9, 0));
        s.clearSelection();
        QVERIFY(!s.hasSelection());
    }

    void scrollback()
    {
        TerminalSession s;
        s.resize(20, 5, 10, 20);
        QSignalSpy finished(&s, &TerminalSession::finished);
        s.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("seq 1 50")});
        QVERIFY(finished.wait(5000));
        QVERIFY(s.atBottom());
        s.scrollLines(-10);
        QVERIFY(!s.atBottom());
        QVERIFY(s.scrollSize() >= 50);
        s.scrollToBottom();
        QVERIFY(s.atBottom());
    }
};

QTEST_GUILESS_MAIN(TestSession)
#include "test_session.moc"
