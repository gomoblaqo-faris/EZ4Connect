#include "coreexecutable.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>

#include <stdexcept>

QString CoreExecutable::path()
{
    const QString fileName =
        QSysInfo::productType() == "windows" ? "zju-connect.exe" : "zju-connect";
    const QString bundledPath = QCoreApplication::applicationDirPath() + "/" + fileName;
    return QFileInfo::exists(bundledPath) ? bundledPath : fileName;
}

QString CoreExecutable::version(QObject *parent)
{
    QProcess process(parent);
    process.start(path(), {"-version"});
    // This runs on the GUI thread, so a core that hangs must not be waited
    // for as long as the defaults allow.
    if (!process.waitForStarted(5000))
    {
        throw std::runtime_error("Failed to start the core");
    }
    if (!process.waitForFinished(10000))
    {
        process.kill();
        process.waitForFinished(1000);
        throw std::runtime_error("The core timed out");
    }

    const QByteArray errorOutput = process.readAllStandardError();
    if (!errorOutput.isEmpty())
    {
        throw std::runtime_error(errorOutput);
    }

    const QString output = process.readAllStandardOutput();
    const QString prefix("ZJU Connect v");
    if (!output.startsWith(prefix))
    {
        throw std::runtime_error("Could not parse the core version");
    }
    return output.mid(prefix.size()).trimmed();
}
