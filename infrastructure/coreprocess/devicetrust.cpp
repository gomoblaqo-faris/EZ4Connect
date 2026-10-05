#include "devicetrust.h"

#include <QCoreApplication>
#include <QProcess>

#include <stdexcept>

#include "infrastructure/coreprocess/coreexecutable.h"
#include "infrastructure/storage/applicationpaths.h"


void DeviceTrust::set(
    QObject *parent,
    const QString &protocol,
    const QString &server,
    int port,
    const QString &profileId,
    bool trusted
)
{
    if (protocol != "atrust")
    {
        throw std::runtime_error(QT_TRANSLATE_NOOP("DeviceTrust", "Trusted devices are only supported with aTrust"));
    }

    QStringList arguments;
    if (!protocol.isEmpty())
    {
        arguments << "-protocol" << protocol;
    }
    if (!server.isEmpty())
    {
        arguments << "-server" << server;
    }
    if (port != 0)
    {
        arguments << "-port" << QString::number(port);
    }
    arguments << "-client-data-file" << ApplicationPaths::clientDataFile(profileId);
    arguments << (trusted ? "-trust-device" : "-untrust-device");

    QProcess process(parent);
    process.start(CoreExecutable::path(), arguments);
    if (!process.waitForStarted(5000))
    {
        throw std::runtime_error(QT_TRANSLATE_NOOP("DeviceTrust", "Failed to start the core"));
    }
    if (!process.waitForFinished(30000))
    {
        process.kill();
        process.waitForFinished(1000);
        throw std::runtime_error(QT_TRANSLATE_NOOP("DeviceTrust", "The core timed out"));
    }

    // The core's own words, not translated: this is what it prints.
    const QByteArray errorOutput = process.readAllStandardError();
    const QByteArray expected =
        trusted ? QByteArray("Device trusted successfully")
                : QByteArray("Device untrusted successfully");
    if (!errorOutput.contains(expected))
    {
        throw std::runtime_error(errorOutput);
    }
}

QString DeviceTrust::describeFailure(const char *message)
{
    // The core's own output has no translation and comes back as it is.
    return QCoreApplication::translate("DeviceTrust", message);
}
