// Runs the command-line client as a separate program against shell scripts
// standing in for the core, with a throwaway home directory.
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>

#include <csignal>
#include <functional>

namespace
{
struct Client
{
    explicit Client(const QTemporaryDir &home)
    {
        // Every location the client could write to is redirected, so the
        // test never touches the real profiles, logs or credential store.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("HOME", home.path());
        environment.insert("CFFIXED_USER_HOME", home.path());
        environment.insert("XDG_CONFIG_HOME", home.filePath("config"));
        environment.insert("XDG_DATA_HOME", home.filePath("data"));
        process.setProcessEnvironment(environment);
        QObject::connect(&process, &QProcess::readyReadStandardOutput,
                         [this]() { output += process.readAllStandardOutput(); });
        QObject::connect(&process, &QProcess::readyReadStandardError,
                         [this]() { errors += process.readAllStandardError(); });
    }

    ~Client()
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(3000);
        }
    }

    void start(const QStringList &arguments)
    {
        process.start(EZ4CONNECT_CLI_PATH, arguments);
    }

    bool waitFor(const std::function<bool()> &condition, int timeoutMs = 10000)
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

    bool waitForExit(int timeoutMs = 10000)
    {
        return waitFor([this]() { return process.state() == QProcess::NotRunning; }, timeoutMs);
    }

    void describeFailure(const char *what) const
    {
        qCritical().noquote() << what
                              << "\n--- standard output ---\n" << output
                              << "\n--- standard error ---\n" << errors;
    }

    QProcess process;
    QByteArray output;
    QByteArray errors;
};

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

// A profile that connects without stored credentials.
QString writeProfile(const QTemporaryDir &directory, bool setUp = true)
{
    const QString path = directory.filePath("profile.ini");
    QSettings settings(path, QSettings::IniFormat);
    if (setUp)
    {
        settings.setValue("Common/ConfigVersion", 8);
    }
    settings.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");
    settings.setValue("ZJUConnect/Protocol", "atrust");
    settings.setValue("ZJUConnect/AuthType", "cas");
    settings.sync();
    return path;
}

bool listsTheDefaultProfile()
{
    QTemporaryDir home;
    Client client(home);
    client.start({"profiles"});
    if (!client.waitForExit() || client.process.exitCode() != 0
        || !client.output.contains("* (default profile)"))
    {
        client.describeFailure("listsTheDefaultProfile failed");
        return false;
    }
    return true;
}

bool answersAPromptConnectsAndStopsOnInterrupt()
{
    QTemporaryDir home;
    const QString core = writeScript(
        home,
        "core",
        "printf 'Please enter your SMS code:'\n"
        "read code\n"
        "echo \"code=$code\"\n"
        "echo 'VPN client started'\n"
        "exec sleep 30"
    );
    Client client(home);
    client.start({"connect", "--config-path", writeProfile(home), "--core", core, "--no-proxy"});

    if (!client.waitFor([&]() { return client.errors.contains("SMS code: "); }))
    {
        client.describeFailure("the client did not ask for the SMS code");
        return false;
    }
    client.process.write("123456\n");
    if (!client.waitFor([&]() { return client.output.contains("VPN client started"); })
        || !client.output.contains("code=123456"))
    {
        client.describeFailure("the answer did not reach the core");
        return false;
    }

    ::kill(static_cast<pid_t>(client.process.processId()), SIGINT);
    if (!client.waitForExit() || client.process.exitStatus() != QProcess::NormalExit
        || client.process.exitCode() != 0)
    {
        client.describeFailure("an interrupt did not end the connection cleanly");
        return false;
    }
    return true;
}

bool reportsAFailedLoginWithANonZeroExitCode()
{
    QTemporaryDir home;
    const QString core = writeScript(home, "core", "echo 'Invalid username or password!'\nexit 1");
    Client client(home);
    client.start({"connect", "--config-path", writeProfile(home), "--core", core, "--no-proxy"});

    if (!client.waitForExit() || client.process.exitCode() != 1
        || !client.errors.contains("Login failed"))
    {
        client.describeFailure("a failed login was not reported");
        return false;
    }
    return true;
}

bool keepsRunningWhenNobodyReadsTheLog()
{
    QTemporaryDir home;
    const QString core = writeScript(
        home,
        "core",
        "echo 'VPN client started'\n"
        "while :; do echo 'still connected'; sleep 1; done"
    );
    Client client(home);
    client.start({"connect", "--config-path", writeProfile(home), "--core", core, "--no-proxy"});
    if (!client.waitFor([&]() { return client.output.contains("VPN client started"); }))
    {
        client.describeFailure("the client did not connect");
        return false;
    }

    // What "| head" does once it has what it wanted. The client must stay up
    // to be stopped properly, rather than die on its next line of log.
    client.process.closeReadChannel(QProcess::StandardOutput);
    client.waitFor([]() { return false; }, 2500);
    if (client.process.state() != QProcess::Running)
    {
        client.describeFailure("the client died when its log was no longer read");
        return false;
    }

    ::kill(static_cast<pid_t>(client.process.processId()), SIGTERM);
    if (!client.waitForExit() || client.process.exitStatus() != QProcess::NormalExit
        || client.process.exitCode() != 0)
    {
        client.describeFailure("the client did not shut down properly afterwards");
        return false;
    }
    return true;
}

bool refusesWhatItCannotUse()
{
    QTemporaryDir home;
    Client notSetUp(home);
    notSetUp.start({"connect", "--config-path", writeProfile(home, false), "--no-proxy"});
    Client unknownProfile(home);
    unknownProfile.start({"connect", "--profile", "no-such-profile"});
    Client noCommand(home);
    noCommand.start({});
    Client coreForTrust(home);
    coreForTrust.start({"trust-device", "--default-profile", "--core", "/bin/true"});

    if (!notSetUp.waitForExit() || notSetUp.process.exitCode() != 2
        || !unknownProfile.waitForExit() || unknownProfile.process.exitCode() != 2
        || !noCommand.waitForExit() || noCommand.process.exitCode() != 2
        || !coreForTrust.waitForExit() || coreForTrust.process.exitCode() != 2)
    {
        notSetUp.describeFailure("a profile that is not set up was not refused");
        unknownProfile.describeFailure("an unknown profile was not refused");
        noCommand.describeFailure("a missing command was not refused");
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return listsTheDefaultProfile()
        && answersAPromptConnectsAndStopsOnInterrupt()
        && reportsAFailedLoginWithANonZeroExitCode()
        && keepsRunningWhenNobodyReadsTheLog()
        && refusesWhatItCannotUse() ? 0 : 1;
}
