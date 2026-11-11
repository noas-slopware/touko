// SPDX-License-Identifier: GPL-2.0-or-later
// The terminal item in an offscreen window: input-method commits, preedit,
// latched modifiers from the extra-keys row, and hardware keys all turn
// into the right bytes on the pty.
#include <QInputMethodEvent>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include "terminalitem.h"
#include "terminalsession.h"

namespace {

QString screenText(TerminalSession &s)
{
    GhosttyFormatterTerminalOptions opts = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
    opts.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    opts.trim = true;
    GhosttyFormatter f = nullptr;
    ghostty_formatter_terminal_new(nullptr, &f, s.terminal(), opts);
    uint8_t *buf = nullptr;
    size_t len = 0;
    ghostty_formatter_format_alloc(f, nullptr, &buf, &len);
    const QString text = QString::fromUtf8(reinterpret_cast<const char *>(buf), qsizetype(len));
    ghostty_free(nullptr, buf, len);
    ghostty_formatter_free(f);
    return text;
}

struct Rig {
    QQuickWindow window;
    TerminalItem *item = nullptr;
    TerminalSession session;

    // The child echoes `count` raw input bytes back as hex.
    explicit Rig(int count)
        : Rig(QStringLiteral("stty raw -echo; printf READY; dd bs=1 count=%1 2>/dev/null | od -An -tx1 | tr -d '\\n'; printf END")
                  .arg(count))
    {
    }

    explicit Rig(const QString &script)
    {
        window.resize(400, 300);
        item = new TerminalItem(window.contentItem());
        item->setSize(QSizeF(400, 300));
        item->setSession(&session);
        session.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
        window.show();
        item->forceActiveFocus();
    }

    bool ready()
    {
        return QTest::qWaitFor([&] { return screenText(session).contains(u"READY"); }, 5000);
    }

    QString result()
    {
        QSignalSpy finished(&session, &TerminalSession::finished);
        if (session.running() && !finished.wait(5000))
            return QStringLiteral("timeout: ") + screenText(session);
        QString t = screenText(session);
        t = t.mid(t.indexOf(u"READY") + 5);
        return t.left(t.indexOf(u"END")).simplified();
    }

    void commit(const QString &text, int replaceStart = 0, int replaceLen = 0)
    {
        QInputMethodEvent ev(QString(), {});
        ev.setCommitString(text, replaceStart, replaceLen);
        QCoreApplication::sendEvent(item, &ev);
    }

    void preedit(const QString &text)
    {
        QInputMethodEvent ev(text, {});
        QCoreApplication::sendEvent(item, &ev);
    }
};

} // namespace

class TestItem : public QObject
{
    Q_OBJECT
    QPointingDevice *touch = nullptr;

    void drag(Rig &rig, QPoint from, QPoint to, int steps = 10)
    {
        QTest::touchEvent(&rig.window, touch).press(0, from, &rig.window);
        for (int i = 1; i <= steps; ++i) {
            QTest::qWait(16);
            QTest::touchEvent(&rig.window, touch).move(0, from + (to - from) * i / steps, &rig.window);
        }
        QTest::qWait(200); // slow at the end: no fling
        QTest::touchEvent(&rig.window, touch).release(0, to, &rig.window);
    }

private Q_SLOTS:
    void initTestCase()
    {
        touch = QTest::createTouchDevice();
    }

    void dragScrollsBack()
    {
        Rig rig(QStringLiteral("seq 1 200; printf READY; sleep 5"));
        QVERIFY(rig.ready());
        QVERIFY(rig.session.atBottom());
        drag(rig, QPoint(200, 40), QPoint(200, 240));
        QVERIFY(!rig.session.atBottom());
        drag(rig, QPoint(200, 240), QPoint(200, 20));
        QTRY_VERIFY(rig.session.atBottom());
        rig.session.hangup();
    }

    void dragOnAlternateScreenSendsArrows()
    {
        // less, man and friends: no mouse reporting, so a drag down is "up".
        Rig rig(QStringLiteral("printf '\\033[?1049h'; stty raw -echo; printf READY; dd bs=1 count=3 2>/dev/null | od -An -tx1 | tr -d '\\n'; printf END; sleep 1"));
        QVERIFY(rig.ready());
        QVERIFY(rig.session.alternateScreen());
        drag(rig, QPoint(200, 40), QPoint(200, 40 + int(rig.item->cellHeight() * 1.5)));
        const QString out = rig.result();
        QVERIFY2(out.startsWith(u"1b 5b 41") || out.startsWith(u"1b 4f 41"), qPrintable(out));
    }

    void longPressSelects()
    {
        Rig rig(QStringLiteral("printf 'hello world'; printf READY; sleep 5"));
        QVERIFY(rig.ready());
        const QPoint p(int(rig.item->cellWidth() * 2.5), int(rig.item->cellHeight() / 2));
        QTest::touchEvent(&rig.window, touch).press(0, p, &rig.window);
        QTRY_VERIFY_WITH_TIMEOUT(rig.item->selecting(), 2000);
        QTest::touchEvent(&rig.window, touch).release(0, p, &rig.window);
        QCOMPARE(rig.session.selectedText(), QStringLiteral("hello"));
        QVERIFY(rig.item->selectionStartRect().isValid());
        rig.item->endSelectionMode();
        QVERIFY(!rig.session.hasSelection());
        rig.session.hangup();
    }

    void pinchZooms()
    {
        Rig rig(QStringLiteral("printf READY; sleep 5"));
        QVERIFY(rig.ready());
        const qreal before = rig.item->fontSize();
        QTest::touchEvent(&rig.window, touch).press(0, QPoint(150, 150), &rig.window).press(1, QPoint(250, 150), &rig.window);
        for (int i = 1; i <= 10; ++i) {
            QTest::qWait(16);
            QTest::touchEvent(&rig.window, touch).move(0, QPoint(150 - 5 * i, 150), &rig.window).move(1, QPoint(250 + 5 * i, 150), &rig.window);
        }
        QTest::touchEvent(&rig.window, touch).release(0, QPoint(100, 150), &rig.window).release(1, QPoint(300, 150), &rig.window);
        QVERIFY2(rig.item->fontSize() > before * 1.8, qPrintable(QString::number(rig.item->fontSize())));
        rig.session.hangup();
    }

    void tapWithMouseReporting()
    {
        // vim with mouse=a, htop: a tap is a left click at that cell.
        Rig rig(QStringLiteral("printf '\\033[?1000h\\033[?1006h'; stty raw -echo min 0 time 20; printf READY; dd bs=256 count=1 2>/dev/null | od -An -c | tr -d ' \\n'; printf END; sleep 1"));
        QVERIFY(rig.ready());
        QVERIFY(rig.session.mouseTracking());
        const QPoint p(int(rig.item->cellWidth() * 3.5), int(rig.item->cellHeight() * 2.5));
        QTest::touchEvent(&rig.window, touch).press(0, p, &rig.window);
        QTest::touchEvent(&rig.window, touch).release(0, p, &rig.window);
        const QString out = rig.result();
        QVERIFY2(out.startsWith(u"033[<0;4;3M"), qPrintable(out));
    }

    void commitText()
    {
        Rig rig(4);
        QVERIFY(rig.ready());
        rig.commit(QStringLiteral("a"));
        rig.commit(QStringLiteral("허"));
        QCOMPARE(rig.result(), QStringLiteral("61 ed 97 88"));
    }

    void preeditIsNotSent()
    {
        // Composing ㅎ -> 허 sends nothing; the commit sends the syllable once.
        Rig rig(3);
        QVERIFY(rig.ready());
        rig.preedit(QStringLiteral("ㅎ"));
        rig.preedit(QStringLiteral("허"));
        QCOMPARE(rig.item->preedit(), QStringLiteral("허"));
        rig.commit(QStringLiteral("허"));
        QCOMPARE(rig.item->preedit(), QString());
        QCOMPARE(rig.result(), QStringLiteral("ed 97 88"));
    }

    void latchedCtrl()
    {
        Rig rig(2);
        QVERIFY(rig.ready());
        rig.item->setCtrlLatched(true);
        rig.commit(QStringLiteral("c"));
        QVERIFY(!rig.item->ctrlLatched()); // one-shot
        rig.commit(QStringLiteral("c"));
        QCOMPARE(rig.result(), QStringLiteral("03 63"));
    }

    void latchedAlt()
    {
        Rig rig(2);
        QVERIFY(rig.ready());
        rig.item->setAltLatched(true);
        rig.commit(QStringLiteral("b"));
        QCOMPARE(rig.result(), QStringLiteral("1b 62"));
    }

    void ctrlWithExtraKey()
    {
        // Ctrl latched, then the ← key on the row: CSI 1;5D.
        Rig rig(6);
        QVERIFY(rig.ready());
        rig.item->setCtrlLatched(true);
        rig.item->sendKeyName(QStringLiteral("Left"));
        QCOMPARE(rig.result(), QStringLiteral("1b 5b 31 3b 35 44"));
    }

    void deleteSurrounding()
    {
        // A keyboard that "deletes one character before the cursor".
        Rig rig(1);
        QVERIFY(rig.ready());
        rig.commit(QString(), -1, 1);
        QCOMPARE(rig.result(), QStringLiteral("7f"));
    }

    void enterFromKeyboard()
    {
        Rig rig(1);
        QVERIFY(rig.ready());
        rig.commit(QStringLiteral("\n"));
        QCOMPARE(rig.result(), QStringLiteral("0d"));
    }

    void hardwareKeys()
    {
        Rig rig(4);
        QVERIFY(rig.ready());
        QTest::keyClick(&rig.window, Qt::Key_Up);
        QTest::keyClick(&rig.window, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(rig.result(), QStringLiteral("1b 5b 41 04"));
    }

    void imeQueries()
    {
        Rig rig(1);
        QVERIFY(rig.ready());
        const int hints = rig.item->inputMethodQuery(Qt::ImHints).toInt();
        QVERIFY(hints & Qt::ImhNoPredictiveText);
        QVERIFY(hints & Qt::ImhNoAutoUppercase);
        QVERIFY(rig.item->inputMethodQuery(Qt::ImEnabled).toBool());
        QVERIFY(rig.item->inputMethodQuery(Qt::ImCursorRectangle).toRectF().isValid());
        rig.commit(QStringLiteral("x"));
        rig.result();
    }
};

QTEST_MAIN(TestItem)
#include "test_item.moc"
