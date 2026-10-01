// Runs the real process wrapper against small shell scripts standing in for
// the core, so start-up, output handling and shutdown are tested end to end.
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <functional>

#include "infrastructure/coreprocess/zjuconnectprocess.h"

namespace
{
QString writeScript(const QTemporaryDir &directory, const QString &name, const QByteArray &body)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        return {};
    }
    file.write("#!/bin/sh\n" + body + "\n");
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition())
    {
        if (timer.elapsed() > timeoutMs)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

struct Observed
{
    explicit Observed(ZjuConnectProcess &process)
    {
        QObject::connect(&process, &CoreProcess::outputRead,
                         [this](const QString &text) { output += text + "\n"; });
        QObject::connect(&process, &CoreProcess::askSudoPass, [this]() { ++sudoPrompts; });
        QObject::connect(&process, &CoreProcess::started, [this]() { ++starts; });
        QObject::connect(&process, &CoreProcess::finished, [this]() { ++finishes; });
        QObject::connect(&process, &CoreProcess::connectionEstablished, [this]() { ++established; });
        QObject::connect(&process, &CoreProcess::error, [this](ZJU_ERROR error) { errors << error; });
    }

    QString output;
    int sudoPrompts = 0;
    int starts = 0;
    int finishes = 0;
    int established = 0;
    QList<ZJU_ERROR> errors;
};

ConnectionProfile profileRunning(const QString &program)
{
    ConnectionProfile profile;
    profile.program = program;
    profile.endpoint.protocol = "easyconnect";
    return profile;
}

bool ignoresSudoPromptTextFromAnUnprivilegedCore()
{
    QTemporaryDir directory;
    ZjuConnectProcess process;
    Observed observed(process);

    // Nothing was started through sudo, so this text is just output. Acting
    // on it would hand the administrator password to whatever printed it.
    process.start(profileRunning(writeScript(
        directory, "prints-sudo-prompt", "echo SUDO_ASK_PASS\nexec sleep 30"
    )));
    if (!waitFor([&]() { return observed.output.contains("SUDO_ASK_PASS"); }, 5000))
    {
        qCritical() << "the stand-in core did not produce its output";
        return false;
    }
    const bool passed = observed.sudoPrompts == 0 && observed.starts == 1;
    process.stop();
    if (!waitFor([&]() { return observed.finishes == 1; }, 5000) || !passed)
    {
        qCritical() << "a sudo password was requested for a core that was not started through sudo";
        return false;
    }
    return true;
}

bool killsACoreThatIgnoresTermination()
{
    QTemporaryDir directory;
    ZjuConnectProcess process;
    process.setKillDelayMs(300);
    Observed observed(process);

    process.start(profileRunning(writeScript(
        directory, "ignores-term", "trap '' TERM\necho ready\nwhile :; do sleep 1; done"
    )));
    if (!waitFor([&]() { return observed.output.contains("ready"); }, 5000))
    {
        qCritical() << "the stand-in core did not start";
        return false;
    }

    process.stop();
    if (!waitFor([&]() { return observed.finishes == 1; }, 5000))
    {
        qCritical() << "a core that ignores termination was never stopped";
        return false;
    }
    return true;
}

bool reportsACoreThatCannotBeStarted()
{
    QTemporaryDir directory;
    ZjuConnectProcess process;
    Observed observed(process);

    process.start(profileRunning(directory.filePath("does-not-exist")));
    if (!waitFor([&]() { return observed.finishes == 1; }, 5000)
        || observed.starts != 0
        || observed.errors != QList<ZJU_ERROR>{ZJU_ERROR::PROGRAM_NOT_FOUND})
    {
        qCritical() << "a core that cannot be started was not reported as such";
        return false;
    }
    return true;
}

bool reportsTheConnectionFromRealOutput()
{
    QTemporaryDir directory;
    ZjuConnectProcess process;
    Observed observed(process);

    process.start(profileRunning(writeScript(
        directory, "connects", "echo 'VPN client started'\nexec sleep 30"
    )));
    const bool established = waitFor([&]() { return observed.established == 1; }, 5000);
    process.stop();
    if (!waitFor([&]() { return observed.finishes == 1; }, 5000) || !established)
    {
        qCritical() << "the connection was not reported from the core's output";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("EZ4Connect-zjuconnectprocess-test");
    QStandardPaths::setTestModeEnabled(true);
    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    const bool passed = ignoresSudoPromptTextFromAnUnprivilegedCore()
        && killsACoreThatIgnoresTermination()
        && reportsACoreThatCannotBeStarted()
        && reportsTheConnectionFromRealOutput();

    QDir(dataRoot).removeRecursively();
    return passed ? 0 : 1;
}
