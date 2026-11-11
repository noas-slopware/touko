// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include "config.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral(TOUKO_BINARY));
    QGuiApplication::setApplicationDisplayName(QStringLiteral(TOUKO_APP_NAME));
    QGuiApplication::setApplicationVersion(QStringLiteral(TOUKO_VERSION));
    QGuiApplication::setOrganizationDomain(QStringLiteral("noa.codes"));
    QGuiApplication::setDesktopFileName(QStringLiteral(TOUKO_APP_ID));
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Terminal for Linux phones, on libghostty-vt"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption workdir({QStringLiteral("w"), QStringLiteral("workdir")}, QStringLiteral("Start in this directory."),
                               QStringLiteral("dir"));
    QCommandLineOption execute({QStringLiteral("e")}, QStringLiteral("Run this command instead of the shell (the rest of the line)."));
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Testing: save the window to a PNG and quit."),
                                  QStringLiteral("file"));
    QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Testing: milliseconds before --screenshot."),
                             QStringLiteral("ms"), QStringLiteral("1500"));
    QCommandLineOption preedit(QStringLiteral("test-preedit"), QStringLiteral("Testing: show this as input-method preedit."),
                               QStringLiteral("text"));
    QCommandLineOption testPage(QStringLiteral("test-page"),
                                QStringLiteral("Testing: open settings, light-themes, dark-themes, tabs or select."),
                                QStringLiteral("page"));
    QCommandLineOption size(QStringLiteral("size"), QStringLiteral("Testing: window size, WxH."), QStringLiteral("WxH"));
    parser.addOptions({workdir, execute, screenshot, delay, preedit, size, testPage});

    // Everything after -e belongs to the command.
    QStringList args = app.arguments();
    QStringList command;
    const qsizetype e = args.indexOf(QStringLiteral("-e"));
    if (e > 0) {
        command = args.mid(e + 1);
        args = args.mid(0, e + 1);
    }
    parser.process(args);

    QQmlApplicationEngine engine;
    QVariantMap startup;
    startup[QStringLiteral("command")] = command;
    startup[QStringLiteral("workdir")] = parser.isSet(workdir) ? QDir(parser.value(workdir)).absolutePath() : QString();
    startup[QStringLiteral("testPage")] = parser.value(testPage);
    engine.rootContext()->setContextProperty(QStringLiteral("startup"), startup);
    engine.rootContext()->setContextProperty(QStringLiteral("appName"), QStringLiteral(TOUKO_APP_NAME));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("codes.noa.touko", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window && parser.isSet(size)) {
        const QStringList wh = parser.value(size).split(u'x');
        if (wh.size() == 2)
            window->resize(wh[0].toInt(), wh[1].toInt());
    }
    if (window && parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        const QString pre = parser.value(preedit);
        const int ms = parser.value(delay).toInt();
        if (!pre.isEmpty()) {
            QTimer::singleShot(ms / 2, window, [window, pre] {
                if (QQuickItem *item = window->activeFocusItem()) {
                    QInputMethodEvent ev(pre, {});
                    QCoreApplication::sendEvent(item, &ev);
                }
            });
        }
        QTimer::singleShot(ms, window, [window, file] {
            window->grabWindow().save(file);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
