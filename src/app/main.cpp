#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

#include <KAboutData>
#include <KLocalizedString>

#ifdef HAVE_KDBUSSERVICE
#include <KDBusService>
#else
#include "singleinstanceguard.h"
#endif

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain("penelope");

    KAboutData aboutData(
        QStringLiteral("penelope"),
        i18n("Penelope"),
        QStringLiteral("0.1.0"),
        i18n("A beautiful paginated markdown reader"),
        KAboutLicense::GPL_V3,
        i18n("(c) 2025-2026"),
        QString(),
        QStringLiteral("https://github.com/clintonthegeek/Penelope")
    );
    aboutData.addAuthor(i18n("Clinton Ignatov"), i18n("Developer"), QString());
    aboutData.setOrganizationDomain("penelope.org");
    aboutData.setDesktopFileName(
        QStringLiteral("org.penelope.Penelope"));

    KAboutData::setApplicationData(aboutData);
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("document-viewer")));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.addPositionalArgument(
        QStringLiteral("file"),
        i18n("Markdown file to open"),
        QStringLiteral("[file...]"));
    parser.process(app);
    aboutData.processCommandLine(&parser);

    MainWindow window;

#ifdef HAVE_KDBUSSERVICE
    KDBusService service(KDBusService::Unique);
    QObject::connect(&service, &KDBusService::activateRequested,
                     &window, [&window](const QStringList &activateArgs, const QString &workingDir) {
        QStringList files;
        for (int i = 1; i < activateArgs.size(); ++i) {
            QString path = activateArgs[i];
            if (QFileInfo(path).isRelative())
                path = workingDir + QLatin1Char('/') + path;
            files << path;
        }
        if (!files.isEmpty())
            window.activateWithFiles(files);
        else {
            window.raise();
            window.activateWindow();
        }
    });
#else
    // Fallback: QLocalServer-based single instance
    SingleInstanceGuard guard(&window);
    {
        QStringList filePaths;
        for (const QString &arg : parser.positionalArguments()) {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
                filePaths << fi.absoluteFilePath();
        }
        if (!guard.tryAcquire(filePaths)) {
            // Another instance is running, files were sent to it
            return 0;
        }
    }
#endif

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        // No files on command line — restore previous session
        window.restoreOpenFiles();
    } else {
        // Files provided — open them fresh, skip session restore
        for (const QString &arg : args) {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
                window.openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
    }

    window.show();
    return app.exec();
}
