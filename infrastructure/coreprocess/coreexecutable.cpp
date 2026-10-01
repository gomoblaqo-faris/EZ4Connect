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
    if (!process.waitForStarted())
    {
        throw std::runtime_error("Failed to start the core");
    }
    if (!process.waitForFinished())
    {
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
