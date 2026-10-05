#ifndef CONNECTIONFLOW_H
#define CONNECTIONFLOW_H

#include <QObject>

#include <functional>
#include <optional>

#include "application/systemproxybackend.h"
#include "core/connectionerror.h"
#include "core/connectionprofile.h"
#include "core/connectionsessionstate.h"

class AuthPrompter;
class ConnectionSession;
class QSettings;
class SystemProxySession;

// Everything between "connect" and a working connection that does not depend
// on how the user is asked or shown things: which credentials a profile
// still needs, answering the core's prompts, keeping the system proxy on
// only while the core is connected, and what an error means. The GUI and the
// command-line client both drive this.
class ConnectionFlow : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<QSettings *()>;
    using ProfileIdProvider = std::function<QString()>;
    // Turns the profile's settings into what the core is started with.
    using ProfileLoader = std::function<ConnectionProfile(
        const QSettings &settings,
        const QString &profileId,
        const QString &username,
        const QString &password
    )>;

    // Why a profile cannot be connected as it is.
    enum class Obstacle
    {
        MissingServerAddress,
        MissingCertificate,
    };

    ConnectionFlow(
        ConnectionSession *connectionSession,
        SystemProxySession *systemProxySession,
        AuthPrompter *prompter,
        SettingsProvider settingsProvider,
        ProfileIdProvider profileIdProvider,
        ProfileLoader profileLoader,
        QObject *parent = nullptr
    );

    // What stands in the way of connecting with the profile as it is, if
    // anything. Lets a front end check before it does something costly, such
    // as asking for administrator privileges.
    std::optional<Obstacle> obstacle() const;

    // Asks for whatever the profile still needs, then starts the session.
    void connectToServer();
    void disconnectFromServer();

    void toggleSystemProxy();
    // Turns the system proxy off at the user's request, so it is not turned
    // back on after a reconnect.
    void clearSystemProxy();
    // Replaces the profile's "set system proxy automatically" for the
    // sessions that follow.
    void setAutomaticProxyOverride(std::optional<bool> wanted);

    // What an error means and what to do about it, for showing to the user.
    static QString describe(ZJU_ERROR error);

signals:
    void cannotConnect(ConnectionFlow::Obstacle obstacle);
    void connectionStarted();
    void connectionEnded();
    // The connection ended without the user asking for it.
    void droppedUnexpectedly();
    void failed(const QString &message);
    // Whether turning the system proxy on makes sense right now.
    void proxyControlAvailableChanged(bool available);
    // In the interface language; the log gets the English wording.
    void proxyFailed(const QString &error);

private:
    QSettings *settings() const;
    void startSession(
        const QString &username,
        const QString &password,
        const QString &phone = QString()
    );
    void handleStateChanged(ConnectionState state);
    void handleFinished(ZJU_ERROR error);
    void handleSsoRequest();
    void syncSystemProxy();
    void enableSystemProxy();

    ConnectionSession *connectionSession;
    SystemProxySession *systemProxySession;
    AuthPrompter *prompter;
    SettingsProvider settingsProvider;
    ProfileIdProvider profileIdProvider;
    ProfileLoader profileLoader;

    // Whether the proxy should be on whenever this session is connected.
    bool proxyWanted = false;
    bool proxyIntentInitialised = false;
    bool proxySyncPending = false;
    std::optional<bool> automaticProxyOverride;
    // Set while the user is being asked for a login or a phone number. An
    // answer that arrives when none was asked for, or after a session has
    // started some other way, is ignored.
    bool credentialsRequested = false;
    // Taken when the session starts, together with the profile the core is
    // given, so that the system proxy always points at the ports that core
    // listens on even if the settings are edited meanwhile.
    SystemProxyConfig sessionProxyConfig;
};

#endif // CONNECTIONFLOW_H
