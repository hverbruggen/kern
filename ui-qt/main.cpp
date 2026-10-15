#include "controller.h"
#include "theme.h"
#ifdef KERN_WITH_CHECKS
#include "tests/checks.h"
#endif

#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QTimer>

namespace {

// Documents are untrusted input. Rendering them must never reach the network, so remote
// images are refused while local files and embedded data still load.
class LocalOnlyAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *body) override
    {
        const QString scheme = request.url().scheme();
        if (scheme == QLatin1String("file") || scheme == QLatin1String("qrc") || scheme == QLatin1String("data"))
            return QNetworkAccessManager::createRequest(operation, request, body);
        QNetworkRequest refused(request);
        refused.setUrl(QUrl(QStringLiteral("kern-blocked:remote-content")));
        return QNetworkAccessManager::createRequest(operation, refused, nullptr);
    }
};

class LocalOnlyFactory : public QQmlNetworkAccessManagerFactory {
public:
    QNetworkAccessManager *create(QObject *parent) override { return new LocalOnlyAccessManager(parent); }
};

void printUsage(FILE *stream)
{
    fputs("Usage: kern [FILE | FOLDER]...\n"
          "Open Markdown, LaTeX, or plain text files in tabs. A folder becomes the project\n"
          "shown in the sidebar.\n\n"
          "  -h, --help     show this help\n"
          "  -v, --version  show the version\n", stream);
}

}

int main(int argc, char **argv)
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    // Toolbar icons come from a bundled subset so they render whatever font the theme picks.
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/kern-icons.ttf"));
    app.setApplicationName(QStringLiteral("kern"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    // Matches the desktop file name, so the window manager can pair windows with the launcher entry.
    app.setDesktopFileName(QStringLiteral("io.github.hverbruggen.kern"));
    const QStringList arguments = app.arguments();
#ifdef KERN_WITH_CHECKS
    if (arguments.contains(QStringLiteral("--self-test")))
        return runSelfTest();
#endif
    Controller controller;
#ifndef KERN_WITH_CHECKS
    controller.setRememberFolders(true);
#endif
    UiTheme theme;
    bool openedFile = false;
    bool openedFolder = false;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &argument = arguments[i];
#ifdef KERN_WITH_CHECKS
        // Check flags are handled by runChecks(); --screenshot takes a file name.
        if (argument == QStringLiteral("--screenshot")) {
            ++i;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--")))
            continue;
#endif
        if (argument == QStringLiteral("-h") || argument == QStringLiteral("--help")) {
            printUsage(stdout);
            return 0;
        }
        if (argument == QStringLiteral("-v") || argument == QStringLiteral("--version")) {
            printf("kern %s\n", qPrintable(app.applicationVersion()));
            return 0;
        }
        if (argument.startsWith(QLatin1Char('-')) && argument != QStringLiteral("-")) {
            fprintf(stderr, "kern: unknown option '%s'\n", qPrintable(argument));
            printUsage(stderr);
            return 2;
        }
        const QFileInfo info(argument);
        if (info.isDir()) {
            controller.setProjectFolder(info.absoluteFilePath());
            openedFolder = true;
        } else {
            controller.openPath(info.absoluteFilePath());
            openedFile = true;
        }
    }

    LocalOnlyFactory networkFactory;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    QList<QQmlError> qmlWarnings;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
        [&qmlWarnings](const QList<QQmlError> &warnings) { qmlWarnings.append(warnings); });
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("uiTheme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("startWithSidebarCollapsed"), openedFile && !openedFolder);
    engine.load(QUrl(QStringLiteral("qrc:/qt/main.qml")));
    if (engine.rootObjects().isEmpty()) {
        for (const QQmlError &warning : qmlWarnings)
            fprintf(stderr, "%s\n", qPrintable(warning.toString()));
        return 1;
    }
#ifdef KERN_WITH_CHECKS
    const int checkResult = runChecks(app, engine, controller, theme, arguments, qmlWarnings);
    if (checkResult >= 0)
        return checkResult;
#endif

    QTimer diskTimer;
    QObject::connect(&diskTimer, &QTimer::timeout, &controller, &Controller::checkDiskChanges);
    diskTimer.start(2000);
    return app.exec();
}
