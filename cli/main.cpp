// The command-line client: the same profiles, credential store, reconnect
// and system-proxy handling as the graphical app, driven from a terminal.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <memory>
#include <stdexcept>

#include "application/applicationconstants.h"
#include "application/applicationlogger.h"
#include "application/connectionflow.h"
#include "application/connectionsession.h"
#include "application/profilesettings.h"
#include "application/secretstore.h"
#include "application/settingsmigrator.h"
#include "application/systemproxysession.h"
#include "infrastructure/coreprocess/coreexecutable.h"
#include "infrastructure/coreprocess/devicetrust.h"
#include "infrastructure/coreprocess/zjuconnectprocess.h"
#include "infrastructure/logging/applicationlogfile.h"
#include "infrastructure/platform/platformsystemproxybackend.h"
#include "infrastructure/settings/profilemanager.h"
#include "infrastructure/settings/settingsprofileloader.h"
#include "infrastructure/storage/applicationpaths.h"
#ifdef EZ4CONNECT_HAS_KEYCHAIN
#include "infrastructure/secrets/keychainsecretstore.h"
#endif
#include "terminalinput.h"
#include "terminalprompter.h"
#include "unixsignals.h"

#ifndef PROJ_VER
#define PROJ_VER "unknown"
#endif

namespace
{
// Exit codes: 0 after a connection that was ended on request, 1 when it
// failed or dropped, 2 when the command or the profile could not be used.
const int exitFailed = 1;
const int exitUnusable = 2;
const int exitInterrupted = 130;

void printError(const QString &message)
{
    QTextStream(stderr) << message << Qt::endl;
}

int listProfiles()
{
    ProfileManager manager;
    const QString active = manager.activeProfile();
    QTextStream output(stdout);
    // In brackets because it is not a name: a profile can be called "default".
    output << (active.isEmpty() ? "* " : "  ") << "(default profile)" << Qt::endl;
    for (const QString &profileId : manager.listProfiles())
    {
        output << (profileId == active ? "* " : "  ") << profileId << Qt::endl;
    }
    return 0;
}

// The profile a command works on: where its settings are, and the name its
// login data is kept under.
struct SelectedProfile
{
    QString profileId;
    QString settingsPath;
};

bool selectProfile(const QCommandLineParser &parser, SelectedProfile *selected)
{
    if (parser.isSet("config-path"))
    {
        selected->profileId = "custom";
        selected->settingsPath = parser.value("config-path");
        if (!QFileInfo::exists(selected->settingsPath))
        {
            printError("No such settings file: " + selected->settingsPath);
            return false;
        }
        return true;
    }

    ProfileManager manager;
    QString profileId;
    if (parser.isSet("profile"))
    {
        profileId = parser.value("profile");
        if (profileId.isEmpty())
        {
            printError("--profile needs a name. Use --default-profile for the default profile.");
            return false;
        }
    }
    else if (!parser.isSet("default-profile"))
    {
        profileId = manager.activeProfile();
    }
    if (!profileId.isEmpty() && !manager.listProfiles().contains(profileId))
    {
        printError("No such profile: " + profileId + " (see \"profiles\")");
        return false;
    }
    selected->profileId = profileId;
    selected->settingsPath = manager.profilePath(profileId);
    return true;
}

// Brings a profile up to date the way the graphical app does when it opens
// one, without the questions it would ask.
bool prepareProfile(QSettings &settings)
{
    if (!ProfileSettings::contains(settings, ProfileSettings::ConfigVersion))
    {
        printError("This profile has not been set up yet. Set it up in the graphical app first.");
        return false;
    }

    const SettingsMigrationAction action = SettingsMigrator::prepare(settings);
    if (action == SettingsMigrationAction::RecommendReset)
    {
        // Whether to reset is the user's decision, and the graphical app
        // asks for it. Its version is left alone so that it still asks.
        printError(
            "Note: this profile was saved by an older version. It is used as it is; "
            "the graphical app will offer to reset it to the current defaults."
        );
        ProfileSettings::migrateSecrets(settings);
        return true;
    }
    if (action == SettingsMigrationAction::MigrateAutoStart)
    {
        ProfileManager().setAutoStartEnabled(
            ProfileSettings::read(settings, ProfileSettings::LegacyAutoStart)
        );
    }
    else if (action == SettingsMigrationAction::NewerThanApplication)
    {
        printError(
            "Note: this profile was saved by a newer version. Settings this version "
            "does not know are ignored."
        );
    }
    SettingsMigrator::finish(settings, false);
    ProfileSettings::migrateSecrets(settings);
    return true;
}

int setDeviceTrust(QCoreApplication &application, const SelectedProfile &selected, bool trusted)
{
    QSettings settings(selected.settingsPath, QSettings::IniFormat);
    if (!prepareProfile(settings))
    {
        return exitUnusable;
    }

    try
    {
        DeviceTrust::set(
            &application,
            ProfileSettings::read(settings, ProfileSettings::Protocol),
            ProfileSettings::read(settings, ProfileSettings::ServerAddress),
            ProfileSettings::read(settings, ProfileSettings::ServerPort),
            selected.profileId,
            trusted
        );
    }
    catch (const std::runtime_error &error)
    {
        printError(QString::fromLocal8Bit(error.what()).trimmed());
        return exitFailed;
    }
    QTextStream(stdout) << (trusted ? "This device is now trusted." : "This device is no longer trusted.")
                        << Qt::endl;
    return 0;
}

int runConnection(QCoreApplication &application, const QCommandLineParser &parser, const SelectedProfile &selected)
{
    UnixSignals unixSignals;
    if (!unixSignals.isInstalled())
    {
        printError(
            "Could not set up signal handling, so an interrupt could not shut the "
            "connection down properly. Not connecting."
        );
        return exitFailed;
    }

    // A log of its own, so that a running graphical app keeps its log.
    ApplicationLogFile logFile(
        QDir(ApplicationPaths::logDirectory()).filePath("ez4connect-cli.log")
    );
    ApplicationLogger logger;
    QObject::connect(&logger, &ApplicationLogger::entryAdded,
                     &logFile, &ApplicationLogFile::appendEntry);

    QSettings settings(selected.settingsPath, QSettings::IniFormat);
    if (!prepareProfile(settings))
    {
        return exitUnusable;
    }

    ConnectionSession session(new ZjuConnectProcess());
    SystemProxySession proxySession(std::make_unique<PlatformSystemProxyBackend>());
    TerminalInput terminal;
    TerminalPrompter prompter(&terminal);
    prompter.setOverwriteProxy(parser.isSet("overwrite-proxy"));

    const QString corePath = parser.isSet("core") ? parser.value("core") : CoreExecutable::path();
    ConnectionFlow flow(
        &session,
        &proxySession,
        &prompter,
        [&settings]() { return &settings; },
        [&selected]() { return selected.profileId; },
        [corePath](const QSettings &profileSettings,
                   const QString &profileId,
                   const QString &username,
                   const QString &password)
        {
            ConnectionProfile profile =
                SettingsProfileLoader::load(profileSettings, profileId, username, password);
            profile.program = corePath;
            return profile;
        }
    );
    if (parser.isSet("proxy"))
    {
        flow.setAutomaticProxyOverride(true);
    }
    else if (parser.isSet("no-proxy"))
    {
        flow.setAutomaticProxyOverride(false);
    }

    QObject::connect(&session, &ConnectionSession::outputRead,
                     &logger, &ApplicationLogger::appendCoreOutput);

    int exitCode = 0;
    bool stopRequested = false;
    QObject::connect(&flow, &ConnectionFlow::cannotConnect, &application,
                     [](ConnectionFlow::Obstacle obstacle)
    {
        printError(obstacle == ConnectionFlow::Obstacle::MissingServerAddress
            ? "This profile has no server address."
            : "This profile uses certificate authentication, but has no certificate file.");
        QCoreApplication::exit(exitUnusable);
    });
    QObject::connect(&prompter, &AuthPrompter::loginAbandoned, &application,
                     []() { QCoreApplication::exit(exitUnusable); });
    QObject::connect(&flow, &ConnectionFlow::failed, &application,
                     [&exitCode](const QString &message)
    {
        printError(message);
        exitCode = exitFailed;
    });
    QObject::connect(&flow, &ConnectionFlow::proxyFailed, &application,
                     [](const QString &error) { printError(error); });
    QObject::connect(&flow, &ConnectionFlow::droppedUnexpectedly, &application,
                     [&exitCode, &stopRequested]()
    {
        if (!stopRequested)
        {
            exitCode = exitFailed;
        }
    });
    // Queued: the failure, if any, is reported after this signal.
    QObject::connect(&flow, &ConnectionFlow::connectionEnded, &application,
                     [&exitCode]() { QCoreApplication::exit(exitCode); },
                     Qt::QueuedConnection);

    QObject::connect(&unixSignals, &UnixSignals::terminationRequested, &application,
                     [&flow, &session, &stopRequested]()
    {
        if (!session.isActive())
        {
            QCoreApplication::exit(exitInterrupted);
            return;
        }
        // A second interrupt reaches the session again, which then kills
        // a core that is slow to stop.
        stopRequested = true;
        flow.disconnectFromServer();
    });
    // Whatever way the loop ends, the system proxy must not be left pointing
    // at a core that is gone.
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application,
                     [&proxySession]() { proxySession.clearBeforeShutdown(); });

    QTimer::singleShot(0, &flow, &ConnectionFlow::connectToServer);
    return QCoreApplication::exec();
}
}

int main(int argc, char *argv[])
{
#if defined(Q_OS_MACOS)
    // The credential store answers through the main dispatch queue, which the
    // default event loop of a console application never services.
    qputenv("QT_EVENT_DISPATCHER_CORE_FOUNDATION", "1");
#endif
    // With the log piped into something that exits ("| head"), the next write
    // would otherwise kill the client where it stands, leaving the core
    // running and the system proxy set. The write just fails instead.
    ::signal(SIGPIPE, SIG_IGN);

    QCoreApplication application(argc, argv);
    // The same name as the graphical app, so both use the same profiles.
    QCoreApplication::setApplicationName(ApplicationConstants::ApplicationName);
    QCoreApplication::setApplicationVersion(PROJ_VER);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Connects to a Sangfor EasyConnect or aTrust VPN with the profiles of EZ4Connect.\n"
        "\n"
        "Commands:\n"
        "  profiles         List the profiles; the active one is marked\n"
        "  connect          Connect, and stay connected until interrupted (Ctrl+C)\n"
        "  trust-device     Mark this device as trusted (aTrust)\n"
        "  untrust-device   Remove that mark"
    );
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "profiles, connect, trust-device or untrust-device");
    parser.addOptions({
        {{"p", "profile"}, "Use this profile instead of the active one.", "name"},
        {"default-profile", "Use the default profile instead of the active one."},
        {"config-path", "Use this settings file instead of a profile.", "file"},
        {"proxy", "Set the system proxy once connected, whatever the profile says."},
        {"no-proxy", "Do not set the system proxy, whatever the profile says."},
        {"overwrite-proxy", "Replace a system proxy that something else configured."},
        {"core", "Path of the zju-connect core to run (connect only).", "file"},
    });
    parser.process(application);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 1)
    {
        printError(arguments.isEmpty() ? "No command given.\n" : "Exactly one command is expected.\n");
        printError(parser.helpText());
        return exitUnusable;
    }
    const QString command = arguments.first();
    if (command == "profiles")
    {
        return listProfiles();
    }
    if (command != "connect" && command != "trust-device" && command != "untrust-device")
    {
        printError("Unknown command: " + command + "\n");
        printError(parser.helpText());
        return exitUnusable;
    }
    if (parser.isSet("proxy") && parser.isSet("no-proxy"))
    {
        printError("--proxy and --no-proxy cannot be used together.");
        return exitUnusable;
    }
    if (parser.isSet("profile") && parser.isSet("default-profile"))
    {
        printError("--profile and --default-profile cannot be used together.");
        return exitUnusable;
    }
    if (command != "connect" && parser.isSet("core"))
    {
        // These commands run the core that is installed with the app.
        printError("--core can only be used with connect.");
        return exitUnusable;
    }

    SelectedProfile selected;
    if (!selectProfile(parser, &selected))
    {
        return exitUnusable;
    }

    std::unique_ptr<SecretStore> secretStore;
#ifdef EZ4CONNECT_HAS_KEYCHAIN
    secretStore = std::make_unique<KeychainSecretStore>(ApplicationConstants::ApplicationName);
#endif
    ProfileSettings::setSecretStore(secretStore.get());
    ProfileSettings::setPendingRemovalsFile(ProfileManager().stateFilePath());
    ProfileSettings::retryPendingSecretRemovals();

    int result = 0;
    if (command == "connect")
    {
        result = runConnection(application, parser, selected);
    }
    else
    {
        result = setDeviceTrust(application, selected, command == "trust-device");
    }
    ProfileSettings::setSecretStore(nullptr);
    return result;
}
