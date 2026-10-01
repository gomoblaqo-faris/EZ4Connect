#include "autostart.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace
{
QString nativeApplicationPath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString macApplicationBundlePath()
{
#ifdef Q_OS_MAC
    QDir directory(QCoreApplication::applicationDirPath());
    directory.cdUp();
    directory.cdUp();
    return directory.absolutePath();
#else
    return {};
#endif
}

QString macApplicationBundleName()
{
#ifdef Q_OS_MAC
    return QFileInfo(macApplicationBundlePath()).baseName();
#else
    return {};
#endif
}
}

OperationStatus AutoStart::setEnabled(bool enabled)
{
#if defined(Q_OS_WINDOWS)
    QSettings settings(
        R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)",
        QSettings::NativeFormat
    );
    if (enabled)
    {
        settings.setValue(QApplication::applicationName(), nativeApplicationPath());
    }
    else
    {
        settings.remove(QApplication::applicationName());
    }
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        return OperationStatus::failure("Could not update the startup entry in the registry.");
    }
    return {};
#elif defined(Q_OS_MACOS)
    {
        const QStringList arguments{
            "-e",
            // Deleting an item that is already gone is an error in
            // AppleScript, but it is the state the user asked for.
            "tell application \"System Events\" to if (exists login item \"" +
                macApplicationBundleName() + "\") then delete login item \"" +
                macApplicationBundleName() + "\""
        };
        QProcess process;
        process.start("osascript", arguments);
        process.waitForFinished();
        const QString error = process.readAllStandardError();
        // When enabling, this only clears a previous entry, which may not exist.
        if (!enabled && process.error() != QProcess::UnknownError)
        {
            return OperationStatus::failure(
                "Could not remove the login item: " + process.errorString()
            );
        }
        if (!enabled && process.exitCode() != 0)
        {
            return OperationStatus::failure("Could not remove the login item: " + error);
        }
    }
    if (enabled)
    {
        const QStringList arguments{
            "-e",
            "tell application \"System Events\" to make login item at end with properties "
            "{path:\"" + macApplicationBundlePath() + "\", hidden:false}"
        };
        QProcess process;
        process.start("osascript", arguments);
        process.waitForFinished();
        if (process.error() != QProcess::UnknownError)
        {
            return OperationStatus::failure(
                "Could not create the login item: " + process.errorString()
            );
        }
        if (process.exitCode() != 0)
        {
            return OperationStatus::failure(
                "Could not create the login item: " + process.readAllStandardError()
            );
        }
    }
    return {};
#elif defined(Q_OS_LINUX)
    const QString directoryPath =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/";
    QDir directory(directoryPath);
    QFile desktopFile(directoryPath + QApplication::applicationName() + ".desktop");

    if (directory.exists() && desktopFile.exists() && !desktopFile.remove())
    {
        return OperationStatus::failure(
            "Could not remove the .desktop file: " + desktopFile.fileName()
        );
    }
    if (!enabled)
    {
        return {};
    }
    if (!directory.exists() && !directory.mkpath("."))
    {
        return OperationStatus::failure(
            "Could not create the autostart directory: " + directoryPath
        );
    }
    if (!desktopFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return OperationStatus::failure(
            "Could not create the .desktop file: " + desktopFile.fileName()
        );
    }

    QTextStream output(&desktopFile);
    output << "[Desktop Entry]\n";
    output << "Type=Application\n";
    output << "Name=" << QApplication::applicationName() << "\n";
    output << "Exec=" << nativeApplicationPath() << "\n";
    output << "X-GNOME-Autostart-enabled=true\n";
    output.flush();
    if (output.status() != QTextStream::Ok || !desktopFile.flush())
    {
        return OperationStatus::failure(
            "Could not write the .desktop file: " + desktopFile.fileName()
        );
    }
    return {};
#else
    Q_UNUSED(enabled)
    return OperationStatus::failure("Launch at login is not supported on this platform.");
#endif
}
