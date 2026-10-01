#include "devicetrust.h"

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
        throw std::runtime_error("Trusted devices are only supported with aTrust");
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
        throw std::runtime_error("Failed to start the core");
    }
    if (!process.waitForFinished(30000))
    {
        process.kill();
        process.waitForFinished(1000);
        throw std::runtime_error("The core timed out");
    }

    const QByteArray errorOutput = process.readAllStandardError();
    const QByteArray expected =
        trusted ? QByteArray("Device trusted successfully")
                : QByteArray("Device untrusted successfully");
    if (!errorOutput.contains(expected))
    {
        throw std::runtime_error(errorOutput);
    }
}
