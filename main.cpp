#include <QApplication>

#include "SingleApplication"

#include "application/applicationlogger.h"
#include "application/applicationconstants.h"
#include "infrastructure/logging/applicationlogfile.h"
#include "infrastructure/settings/profilemanager.h"
#include "infrastructure/storage/applicationpaths.h"
#include "presentation/language/languagemanager.h"
#include "presentation/main/mainwindow.h"

#ifndef PROJ_VER
#define PROJ_VER "unknown"
#endif

int main(int argc, char *argv[])
{
    SingleApplication app(argc, argv, false, SingleApplication::Mode::System);
    QApplication::setApplicationName(ApplicationConstants::ApplicationName);
    QApplication::setApplicationDisplayName(ApplicationConstants::ApplicationName);
    QApplication::setApplicationVersion(PROJ_VER);

    ApplicationLogFile applicationLogFile(ApplicationPaths::logFile());
    ApplicationLogger applicationLogger;
    QObject::connect(
        &applicationLogger,
        &ApplicationLogger::entryAdded,
        &applicationLogFile,
        &ApplicationLogFile::appendEntry
    );

#if defined(Q_OS_WINDOWS)
    QApplication::setFont(QFont("Microsoft YaHei UI", QApplication::font().pointSize()));
#endif

    // Applied before any window exists, so the first one is built in the
    // right language. Later choices switch the open windows live.
    const ProfileManager profileStorage;
    LanguageManager languageManager;
    languageManager.restore(
        profileStorage.language(),
        [&profileStorage](const QString &choice) { profileStorage.setLanguage(choice); }
    );

    MainWindow mainWindow(&applicationLogger, &applicationLogFile, &languageManager);

    QObject::connect(&app, &SingleApplication::aboutToQuit, &mainWindow, &MainWindow::cleanUpWhenQuit);

    return QApplication::exec();
}
