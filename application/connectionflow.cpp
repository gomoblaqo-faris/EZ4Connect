#include "application/connectionflow.h"

#include <QDebug>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

#include "application/authprompter.h"
#include "application/connectionsession.h"
#include "application/profilesettings.h"
#include "application/systemproxysession.h"

ConnectionFlow::ConnectionFlow(
    ConnectionSession *connectionSession,
    SystemProxySession *systemProxySession,
    AuthPrompter *prompter,
    SettingsProvider settingsProvider,
    ProfileIdProvider profileIdProvider,
    ProfileLoader profileLoader,
    QObject *parent
)
    : QObject(parent),
      connectionSession(connectionSession),
      systemProxySession(systemProxySession),
      prompter(prompter),
      settingsProvider(std::move(settingsProvider)),
      profileIdProvider(std::move(profileIdProvider)),
      profileLoader(std::move(profileLoader))
{
    // What the core asks for while logging in goes to the user...
    connect(connectionSession, &ConnectionSession::askSudoPass,
            prompter, &AuthPrompter::requestSudoPassword);
    connect(connectionSession, &ConnectionSession::graphCaptcha, this,
            [this](const QString &graphFile)
            {
                // EasyConnect shows characters to type; aTrust shows a
                // picture to click on.
                const bool textInput =
                    ProfileSettings::read(*settings(), ProfileSettings::Protocol) == "easyconnect";
                this->prompter->requestGraphCaptcha(graphFile, textInput);
            });
    connect(connectionSession, &ConnectionSession::smsCode,
            prompter, &AuthPrompter::requestSmsCode);
    connect(connectionSession, &ConnectionSession::totpCode,
            prompter, &AuthPrompter::requestTotpCode);
    connect(connectionSession, &ConnectionSession::randCode, this,
            [this]() { this->prompter->requestSmsCode(false); });
    connect(connectionSession, &ConnectionSession::radiusCode,
            prompter, &AuthPrompter::requestRadiusCode);
    connect(connectionSession, &ConnectionSession::ssoAuth,
            this, &ConnectionFlow::handleSsoRequest);

    // ...and the answers go back to the core.
    connect(prompter, &AuthPrompter::sudoPasswordSubmitted,
            connectionSession, &ConnectionSession::submitSudoPassword);
    connect(prompter, &AuthPrompter::interactiveInputSubmitted,
            connectionSession, &ConnectionSession::submitInput);
    connect(prompter, &AuthPrompter::interactiveInputCancelled,
            connectionSession, &ConnectionSession::cancelInteractiveInput);

    connect(prompter, &AuthPrompter::loginSubmitted, this,
            [this](const QString &username, const QString &password, bool saveDetails)
            {
                if (!credentialsRequested || this->connectionSession->isActive())
                {
                    return;
                }
                credentialsRequested = false;
                if (saveDetails)
                {
                    ProfileSettings::write(*settings(), ProfileSettings::Username, username);
                    ProfileSettings::write(*settings(), ProfileSettings::Password, password);
                    settings()->sync();
                }
                startSession(username, password);
            });
    connect(prompter, &AuthPrompter::phoneNumberSubmitted, this,
            [this](const QString &countryCode, const QString &phoneNumber, bool saveDetails)
            {
                if (!credentialsRequested || this->connectionSession->isActive())
                {
                    return;
                }
                credentialsRequested = false;
                if (saveDetails)
                {
                    ProfileSettings::write(*settings(), ProfileSettings::PhoneCountryCode, countryCode);
                    ProfileSettings::write(*settings(), ProfileSettings::PhoneNumber, phoneNumber);
                    settings()->sync();
                }
                startSession(
                    ProfileSettings::read(*settings(), ProfileSettings::Username),
                    ProfileSettings::read(*settings(), ProfileSettings::Password),
                    countryCode + "-" + phoneNumber
                );
            });

    connect(prompter, &AuthPrompter::loginAbandoned, this,
            [this]() { credentialsRequested = false; });

    connect(connectionSession, &ConnectionSession::savedSudoPasswordRejected, this,
            []() { qWarning().noquote() << "The sudo password may be wrong; discarding the remembered password"; });
    connect(connectionSession, &ConnectionSession::reconnectScheduled, this,
            [](int delayMs)
            {
                qInfo().noquote()
                    << QString("Reconnecting in %1 s...").arg(delayMs / 1000.0);
            });
    connect(connectionSession, &ConnectionSession::stateChanged,
            this, &ConnectionFlow::handleStateChanged);
    connect(connectionSession, &ConnectionSession::finished,
            this, &ConnectionFlow::handleFinished);

    connect(systemProxySession, &SystemProxySession::operationFailed, this,
            [this](const QString &error)
            {
                // Do not retry a proxy that could not be set on every reconnect.
                if (!this->systemProxySession->isEnabled())
                {
                    proxyWanted = false;
                }
                qWarning().noquote() << error;
                emit proxyFailed(error);
            });
    // A proxy that finished being set after the session ended has nothing
    // listening behind it.
    connect(systemProxySession, &SystemProxySession::operationFinished, this,
            [this](bool enabled)
            {
                if (enabled && !this->connectionSession->isActive())
                {
                    this->systemProxySession->disable();
                }
            });
    // Queued so a proxy operation requested here starts only after the
    // session has finished reporting the previous one.
    connect(systemProxySession, &SystemProxySession::busyChanged, this,
            [this](bool busy)
            {
                if (!busy && proxySyncPending)
                {
                    syncSystemProxy();
                }
            },
            Qt::QueuedConnection);
}

QSettings *ConnectionFlow::settings() const
{
    return settingsProvider();
}

std::optional<ConnectionFlow::Obstacle> ConnectionFlow::obstacle() const
{
    if (ProfileSettings::read(*settings(), ProfileSettings::ServerAddress).isEmpty())
    {
        return Obstacle::MissingServerAddress;
    }
    if (ProfileSettings::read(*settings(), ProfileSettings::Protocol) == "easyconnect"
        && ProfileSettings::easyConnectAuthType(*settings()) == "certificate"
        && ProfileSettings::read(*settings(), ProfileSettings::CertFile).isEmpty())
    {
        return Obstacle::MissingCertificate;
    }
    return std::nullopt;
}

void ConnectionFlow::connectToServer()
{
    if (connectionSession->isActive())
    {
        return;
    }

    if (const std::optional<Obstacle> found = obstacle())
    {
        emit cannotConnect(*found);
        return;
    }

    const QString username = ProfileSettings::read(*settings(), ProfileSettings::Username);
    const QString password = ProfileSettings::read(*settings(), ProfileSettings::Password);
    const QString protocol = ProfileSettings::read(*settings(), ProfileSettings::Protocol);
    const QString authType = ProfileSettings::read(*settings(), ProfileSettings::AuthType);
    const QString easyconnectAuthType = ProfileSettings::easyConnectAuthType(*settings());

    const bool passwordLogin =
        (protocol == "atrust" && authType == "psw") ||
        (protocol == "easyconnect" && easyconnectAuthType != "certificate");
    if (protocol == "atrust" && authType == "smsCheckCode")
    {
        QString countryCode =
            ProfileSettings::read(*settings(), ProfileSettings::PhoneCountryCode).trimmed();
        const QString phoneNumber =
            ProfileSettings::read(*settings(), ProfileSettings::PhoneNumber).trimmed();
        if (countryCode.isEmpty() || phoneNumber.isEmpty())
        {
            if (countryCode.isEmpty())
            {
                countryCode = "86";
            }
            credentialsRequested = true;
            prompter->requestPhoneNumber(countryCode, phoneNumber);
            return;
        }
    }
    if (passwordLogin && (username.isEmpty() || password.isEmpty()))
    {
        credentialsRequested = true;
        prompter->requestLogin(username, password);
        return;
    }
    startSession(username, password);
}

void ConnectionFlow::disconnectFromServer()
{
    connectionSession->stop();
}

void ConnectionFlow::startSession(
    const QString &username,
    const QString &password,
    const QString &phone
)
{
    ConnectionProfile profile = profileLoader(*settings(), profileIdProvider(), username, password);
    if (!phone.isNull())
    {
        profile.endpoint.phone = phone;
    }
    // The value comes from a file the user may have edited or imported.
    const int reconnectSeconds = std::clamp(
        ProfileSettings::read(*settings(), ProfileSettings::ReconnectTime), 1, 3600
    );
    const ReconnectPolicy reconnectPolicy{
        ProfileSettings::read(*settings(), ProfileSettings::AutoReconnect),
        reconnectSeconds * 1000
    };
    const SystemProxyConfig proxyConfig{
        ProfileSettings::read(*settings(), ProfileSettings::HTTPPort),
        ProfileSettings::read(*settings(), ProfileSettings::SOCKS5Port),
        ProfileSettings::read(*settings(), ProfileSettings::SystemProxyBypass)
    };
    if (connectionSession->isActive())
    {
        return;
    }
    sessionProxyConfig = proxyConfig;
    if (!connectionSession->start(profile, reconnectPolicy)
        || !connectionSession->isActive())
    {
        return;
    }
    emit connectionStarted();
}

void ConnectionFlow::handleSsoRequest()
{
    const QString serverHost = ProfileSettings::read(*settings(), ProfileSettings::ServerAddress);
    const int serverPort = ProfileSettings::read(*settings(), ProfileSettings::ServerPort);
    QUrl serverUrl;
    serverUrl.setScheme("https");
    serverUrl.setHost(serverHost);
    if (serverPort != 443)
    {
        serverUrl.setPort(serverPort);
    }

    QString ssoUrl = ProfileSettings::read(*settings(), ProfileSettings::LoginURL);
    if (ssoUrl.isEmpty())
    {
        QUrl defaultSsoUrl = serverUrl;
        defaultSsoUrl.setPath("/passport/v1/public/casLogin");
        QUrlQuery query;
        query.addQueryItem(
            "sfDomain",
            ProfileSettings::read(*settings(), ProfileSettings::LoginDomain)
        );
        defaultSsoUrl.setQuery(query);
        ssoUrl = defaultSsoUrl.toString();
    }
    if (ssoUrl.startsWith('/'))
    {
        ssoUrl = serverUrl.resolved(QUrl(ssoUrl)).toString();
    }

    // The query can carry tokens, so only where the login goes is logged.
    qInfo().noquote() << QStringLiteral("Single sign-on: ")
        + QUrl(ssoUrl).toString(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
    prompter->requestSsoLogin(serverUrl, QUrl::fromUserInput(ssoUrl));
}

void ConnectionFlow::handleStateChanged(ConnectionState state)
{
    // The core only listens on the proxy ports while it is connected.
    // Pointing the system at them during login or a reconnect would break
    // every proxied app until the connection is back.
    if (state == ConnectionState::Running)
    {
        if (!proxyIntentInitialised)
        {
            proxyIntentInitialised = true;
            proxyWanted = automaticProxyOverride.value_or(
                ProfileSettings::read(*settings(), ProfileSettings::AutoSetProxy)
            );
        }
        emit proxyControlAvailableChanged(true);
        syncSystemProxy();
    }
    else if (state == ConnectionState::Reconnecting)
    {
        emit proxyControlAvailableChanged(false);
        syncSystemProxy();
    }
}

void ConnectionFlow::handleFinished(ZJU_ERROR error)
{
    qInfo().noquote() << "VPN disconnected.";
    proxyWanted = false;
    proxyIntentInitialised = false;
    proxySyncPending = false;
    // Nothing listens on the proxy ports any more.
    if (systemProxySession->isEnabled())
    {
        systemProxySession->disable();
    }

    const bool interrupted = connectionSession->state() == ConnectionState::Interrupted;
    if (error != ZJU_ERROR::NONE || interrupted)
    {
        emit droppedUnexpectedly();
    }
    emit connectionEnded();
    emit proxyControlAvailableChanged(false);
    if (!interrupted && error != ZJU_ERROR::NONE)
    {
        emit failed(describe(error));
    }
}

void ConnectionFlow::toggleSystemProxy()
{
    if (systemProxySession->isBusy())
    {
        return;
    }
    if (systemProxySession->isEnabled())
    {
        clearSystemProxy();
        return;
    }
    proxyWanted = true;
    syncSystemProxy();
}

void ConnectionFlow::clearSystemProxy()
{
    proxyWanted = false;
    proxySyncPending = false;
    systemProxySession->disable();
}

void ConnectionFlow::setAutomaticProxyOverride(std::optional<bool> wanted)
{
    automaticProxyOverride = wanted;
}

void ConnectionFlow::syncSystemProxy()
{
    if (systemProxySession->isBusy())
    {
        proxySyncPending = true;
        return;
    }
    proxySyncPending = false;

    const ConnectionState state = connectionSession->state();
    if (state == ConnectionState::Running)
    {
        if (proxyWanted && !systemProxySession->isEnabled())
        {
            enableSystemProxy();
        }
    }
    else if (state == ConnectionState::Reconnecting
             && systemProxySession->isEnabled())
    {
        systemProxySession->disable();
    }
}

void ConnectionFlow::enableSystemProxy()
{
    const SystemProxyConfig proxyConfig = sessionProxyConfig;
    connect(
        systemProxySession,
        &SystemProxySession::conflictCheckFinished,
        this,
        [this, proxyConfig](bool conflict)
        {
            const auto stillWanted = [this]()
            {
                return proxyWanted
                    && connectionSession->state() == ConnectionState::Running;
            };
            if (!stillWanted())
            {
                return;
            }

            if (conflict &&
                !ProfileSettings::read(*settings(), ProfileSettings::SuppressProxyOverrideWarning))
            {
                const AuthPrompter::ProxyOverwriteAnswer answer = prompter->askProxyOverwrite();
                if (!answer.overwrite)
                {
                    proxyWanted = false;
                    return;
                }
                if (answer.remember)
                {
                    ProfileSettings::write(
                        *settings(),
                        ProfileSettings::SuppressProxyOverrideWarning,
                        true
                    );
                    settings()->sync();
                }
            }
            else if (conflict)
            {
                qInfo().noquote() << "Skipping the system proxy overwrite warning (suppressed in settings)";
            }

            // The connection may have dropped while the question was open.
            if (!stillWanted())
            {
                return;
            }

            qInfo().noquote()
                << "Setting system proxy: HTTP port " + QString::number(proxyConfig.httpPort)
                       + ", SOCKS5 port " + QString::number(proxyConfig.socksPort);
            systemProxySession->enable(proxyConfig);
        },
        Qt::SingleShotConnection
    );
    systemProxySession->checkConflict(proxyConfig);
}

QString ConnectionFlow::describe(ZJU_ERROR error)
{
    switch (error)
    {
    case ZJU_ERROR::INVALID_DETAIL:
        return "Login failed!\nCheck that the account and password are correct.";
    case ZJU_ERROR::BRUTE_FORCE:
        return "Login failed!\nToo many login attempts; this IP has been blocked. Try again later or switch to EasyConnect.";
    case ZJU_ERROR::OTHER_LOGIN_FAILED:
        return "Login failed!\nUnknown reason. You can send the log to the developer for investigation.";
    case ZJU_ERROR::ACCESS_DENIED:
        return "Insufficient privileges!\nRun the program again with administrator privileges.";
    case ZJU_ERROR::LISTEN_FAILED:
        return "Failed to listen!\nClose the program using the port (such as a leftover zju-connect process), or use a different port.";
    case ZJU_ERROR::CLIENT_FAILED:
        return "Connection failed!\nThe request may have timed out. Check your local network and the server settings.";
    case ZJU_ERROR::CAPTCHA_FAILED:
        return "Login failed!\nCaptcha problem: the code may have expired or been incorrect.";
    case ZJU_ERROR::PROGRAM_NOT_FOUND:
        return "Core not found!\nCheck that the zju-connect core is in the same folder as the program.";
    case ZJU_ERROR::INTERACTIVE_ERROR:
        return "Login failed!\nCheck that your input was correct and that the SSO login was completed.";
    case ZJU_ERROR::AUTH_NOT_AVAILABLE:
        return "Authentication method or login domain unavailable!\nFetch the authentication methods the server offers and choose one of them.";
    case ZJU_ERROR::AUTH_EXPIRED:
        return "Authentication expired!\nPlease log in again.";
    case ZJU_ERROR::OTHER:
        return "Other error!\nUnknown reason. You can send the log to the developer for investigation.";
    case ZJU_ERROR::NONE:
        return {};
    }
    return {};
}
