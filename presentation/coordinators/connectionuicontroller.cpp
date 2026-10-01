#include "connectionuicontroller.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QWidget>

#include <algorithm>
#include <utility>

#include "application/applicationlogger.h"
#include "application/connectionsession.h"
#include "application/profilesettings.h"
#include "application/systemproxysession.h"
#include "infrastructure/coreprocess/coreexecutable.h"
#include "infrastructure/platform/privileges.h"
#include "infrastructure/settings/settingsprofileloader.h"
#include "presentation/coordinators/authdialogcoordinator.h"

ConnectionUiController::ConnectionUiController(
    QWidget *parentWidget,
    QPushButton *connectButton,
    QPushButton *proxyButton,
    QAction *trayConnectAction,
    ConnectionSession *connectionSession,
    SystemProxySession *systemProxySession,
    AuthDialogCoordinator *authenticationDialogs,
    ApplicationLogger *applicationLogger,
    SettingsProvider settingsProvider,
    ProfileIdProvider profileIdProvider,
    NotificationHandler notificationHandler,
    QObject *parent
)
    : QObject(parent),
      parentWidget(parentWidget),
      connectButton(connectButton),
      proxyButton(proxyButton),
      trayConnectAction(trayConnectAction),
      connectionSession(connectionSession),
      systemProxySession(systemProxySession),
      authenticationDialogs(authenticationDialogs),
      settingsProvider(std::move(settingsProvider)),
      profileIdProvider(std::move(profileIdProvider)),
      notificationHandler(std::move(notificationHandler))
{
    connectButton->setText("Connect");
    trayConnectAction->setText("Connect");
    proxyButton->setText("Set System Proxy");
    proxyButton->hide();

    connect(
        connectionSession,
        &ConnectionSession::outputRead,
        applicationLogger,
        &ApplicationLogger::appendCoreOutput
    );
    connect(
        connectionSession,
        &ConnectionSession::savedSudoPasswordRejected,
        this,
        []() { qWarning().noquote() << "The sudo password may be wrong; discarding the remembered password"; }
    );
    connect(
        authenticationDialogs,
        &AuthDialogCoordinator::loginSubmitted,
        this,
        [this](const QString &username, const QString &password, bool saveDetails)
        {
            if (saveDetails)
            {
                ProfileSettings::write(*settings(), ProfileSettings::Username, username);
                ProfileSettings::write(*settings(), ProfileSettings::Password, password
                );
                settings()->sync();
            }
            startConnection(username, password);
        }
    );
    connect(
        authenticationDialogs,
        &AuthDialogCoordinator::phoneNumberSubmitted,
        this,
        [this](
            const QString &countryCode,
            const QString &phoneNumber,
            bool saveDetails
        )
        {
            if (saveDetails)
            {
                ProfileSettings::write(*settings(), ProfileSettings::PhoneCountryCode, countryCode);
                ProfileSettings::write(*settings(), ProfileSettings::PhoneNumber, phoneNumber);
                settings()->sync();
            }

            const QString username =
                ProfileSettings::read(*settings(), ProfileSettings::Username);
            const QString password = ProfileSettings::read(*settings(), ProfileSettings::Password);
            startConnection(
                username,
                password,
                countryCode + "-" + phoneNumber
            );
        }
    );
    connect(
        connectionSession,
        &ConnectionSession::reconnectScheduled,
        this,
        [](int delayMs)
        {
            qInfo().noquote()
                << QString("Reconnecting in %1 s...").arg(delayMs / 1000.0);
        }
    );
    connect(
        connectionSession,
        &ConnectionSession::stateChanged,
        this,
        &ConnectionUiController::handleConnectionStateChanged
    );
    connect(
        systemProxySession,
        &SystemProxySession::operationFailed,
        this,
        [this](const QString &error)
        {
            // Do not retry a proxy that could not be set on every reconnect.
            if (!this->systemProxySession->isEnabled())
            {
                proxyWanted = false;
            }
            qWarning().noquote() << error;
            QMessageBox::critical(this->parentWidget, "System Proxy", error);
        }
    );
    // Queued so a proxy operation requested here starts only after the
    // session has finished reporting the previous one.
    connect(
        systemProxySession,
        &SystemProxySession::busyChanged,
        this,
        [this](bool busy)
        {
            if (!busy && proxySyncPending)
            {
                syncSystemProxy();
            }
        },
        Qt::QueuedConnection
    );
    connect(
        connectionSession,
        &ConnectionSession::finished,
        this,
        [this](ZJU_ERROR error)
        {
            qInfo().noquote() << "VPN disconnected.";
            proxyWanted = false;
            proxyIntentInitialised = false;
            proxySyncPending = false;
            const bool interrupted =
                this->connectionSession->state() == ConnectionState::Interrupted;
            if (error != ZJU_ERROR::NONE || interrupted)
            {
                this->notificationHandler(
                    "VPN",
                    "VPN disconnected unexpectedly!",
                    QSystemTrayIcon::MessageIcon::Warning
                );
            }
            this->connectButton->setText("Connect");
            this->trayConnectAction->setText("Connect");
            this->proxyButton->hide();
            if (!interrupted)
            {
                showConnectionError(error);
            }
        }
    );
    connect(
        connectButton,
        &QPushButton::clicked,
        this,
        &ConnectionUiController::handleConnectClicked
    );
    connect(
        proxyButton,
        &QPushButton::clicked,
        this,
        &ConnectionUiController::handleProxyClicked
    );

}

QSettings *ConnectionUiController::settings() const
{
    return settingsProvider();
}

void ConnectionUiController::handleConnectClicked()
{
    if (connectionSession->isActive())
    {
        connectionSession->stop();
        return;
    }

    if (ProfileSettings::read(*settings(), ProfileSettings::ServerAddress).isEmpty())
    {
        QMessageBox::critical(parentWidget, "Error", "The server address is required.");
        return;
    }

    const QString username = ProfileSettings::read(*settings(), ProfileSettings::Username);
    const QString password = ProfileSettings::read(*settings(), ProfileSettings::Password);
    const QString protocol =
        ProfileSettings::read(*settings(), ProfileSettings::Protocol);
    const QString authType =
        ProfileSettings::read(*settings(), ProfileSettings::AuthType);
    const QString easyconnectAuthType = ProfileSettings::easyConnectAuthType(*settings());

    if (protocol == "easyconnect"
        && easyconnectAuthType == "certificate"
        && ProfileSettings::read(*settings(), ProfileSettings::CertFile).isEmpty())
    {
        QMessageBox::information(
            parentWidget,
            "Certificate Required",
            "This profile uses certificate authentication.\n"
            "Choose a certificate file via Profile → Setup Guide."
        );
        return;
    }

#if defined(Q_OS_WIN)
    if (ProfileSettings::read(*settings(), ProfileSettings::TUNMode) &&
        !Privileges::isElevated())
    {
        if (Privileges::relaunchElevated())
        {
            QApplication::quit();
        }
        else
        {
            QMessageBox::warning(
                parentWidget,
                "Elevation Failed",
                "Could not relaunch with administrator privileges. Please run the app as administrator."
            );
        }
        return;
    }
#endif

    const bool passwordLogin =
        (protocol == "atrust" && authType == "psw") ||
        (protocol == "easyconnect" && easyconnectAuthType != "certificate");
    if (protocol == "atrust" && authType == "smsCheckCode")
    {
        QString countryCode = ProfileSettings::read(
            *settings(),
            ProfileSettings::PhoneCountryCode
        ).trimmed();
        const QString phoneNumber = ProfileSettings::read(
            *settings(),
            ProfileSettings::PhoneNumber
        ).trimmed();
        if (countryCode.isEmpty() || phoneNumber.isEmpty())
        {
            if (countryCode.isEmpty())
            {
                countryCode = "86";
            }
            authenticationDialogs->requestPhoneNumber(countryCode, phoneNumber);
            return;
        }
    }
    if (passwordLogin && (username.isEmpty() || password.isEmpty()))
    {
        authenticationDialogs->requestLogin(username, password);
        return;
    }
    startConnection(username, password);
}

void ConnectionUiController::handleProxyClicked()
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

void ConnectionUiController::clearSystemProxy()
{
    proxyWanted = false;
    proxySyncPending = false;
    systemProxySession->disable();
}

void ConnectionUiController::handleConnectionStateChanged(ConnectionState state)
{
    // The core only listens on the proxy ports while it is connected.
    // Pointing the system at them during login or a reconnect would break
    // every proxied app until the connection is back.
    if (state == ConnectionState::Running)
    {
        if (!proxyIntentInitialised)
        {
            proxyIntentInitialised = true;
            proxyWanted = ProfileSettings::read(*settings(), ProfileSettings::AutoSetProxy);
        }
        proxyButton->show();
        syncSystemProxy();
    }
    else if (state == ConnectionState::Reconnecting)
    {
        proxyButton->hide();
        syncSystemProxy();
    }
}

void ConnectionUiController::syncSystemProxy()
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

void ConnectionUiController::enableSystemProxy()
{
    const SystemProxyConfig proxyConfig = sessionProxyConfig;
    const int httpPort = proxyConfig.httpPort;
    const int socksPort = proxyConfig.socksPort;
    connect(
        systemProxySession,
        &SystemProxySession::conflictCheckFinished,
        this,
        [this, proxyConfig, httpPort, socksPort](bool conflict)
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
                QMessageBox messageBox(
                    QMessageBox::Warning,
                    "Warning",
                    "A system proxy is already configured (possibly by Clash or another proxy app).\n"
                    "Overwrite the current system proxy settings?",
                    QMessageBox::Yes | QMessageBox::No,
                    parentWidget
                );
                auto *dontShowCheckBox = new QCheckBox("Don't ask again");
                messageBox.setCheckBox(dontShowCheckBox);
                if (messageBox.exec() == QMessageBox::No)
                {
                    proxyWanted = false;
                    return;
                }
                if (dontShowCheckBox->isChecked())
                {
                    ProfileSettings::write(*settings(), ProfileSettings::SuppressProxyOverrideWarning, true
                    );
                    settings()->sync();
                }
            }
            else if (conflict)
            {
                qInfo().noquote() << "Skipping the system proxy overwrite warning (suppressed in settings)";
            }

            // The connection may have dropped while the warning was open.
            if (!stillWanted())
            {
                return;
            }

            qInfo().noquote()
                << "Setting system proxy: HTTP port " + QString::number(httpPort)
                       + ", SOCKS5 port " + QString::number(socksPort);
            systemProxySession->enable(proxyConfig);
        },
        Qt::SingleShotConnection
    );
    systemProxySession->checkConflict(proxyConfig);
}

void ConnectionUiController::startConnection(
    const QString &username,
    const QString &password,
    const QString &phone
)
{
    ConnectionProfile profile = SettingsProfileLoader::load(
        *settings(),
        profileIdProvider(),
        username,
        password
    );
    if (!phone.isNull())
    {
        profile.endpoint.phone = phone;
    }
    profile.program = CoreExecutable::path();
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

    connectButton->setText("Disconnect");
    trayConnectAction->setText("Disconnect");
}

void ConnectionUiController::showConnectionError(ZJU_ERROR error)
{
    QString message;
    switch (error)
    {
    case ZJU_ERROR::INVALID_DETAIL:
        message = "Login failed!\nCheck that the account and password in Settings are correct.";
        break;
    case ZJU_ERROR::BRUTE_FORCE:
        message = "Login failed!\nToo many login attempts; this IP has been blocked. Try again later or switch to EasyConnect.";
        break;
    case ZJU_ERROR::OTHER_LOGIN_FAILED:
        message = "Login failed!\nUnknown reason. You can send the log to the developer for investigation.";
        break;
    case ZJU_ERROR::ACCESS_DENIED:
        message = "Insufficient privileges!\nClose the app, then right-click it and run as administrator.";
        break;
    case ZJU_ERROR::LISTEN_FAILED:
        message = "Failed to listen!\nClose the program using the port (such as a leftover zju-connect process), or use a different port.";
        break;
    case ZJU_ERROR::CLIENT_FAILED:
        message = "Connection failed!\nThe request may have timed out. Check your local network and the server settings.";
        break;
    case ZJU_ERROR::CAPTCHA_FAILED:
        message = "Login failed!\nCaptcha problem: the code may have expired or been incorrect.";
        break;
    case ZJU_ERROR::PROGRAM_NOT_FOUND:
        message = "Core not found!\nCheck that the core is in the right place and was extracted alongside the app.";
        break;
    case ZJU_ERROR::INTERACTIVE_ERROR:
        message = "Login failed!\nCheck that your input was correct and that the SSO login was completed.";
        break;
    case ZJU_ERROR::AUTH_NOT_AVAILABLE:
        message = "Authentication method or login domain unavailable!\nUse the Fetch Authentication Methods button to configure it.";
        break;
    case ZJU_ERROR::AUTH_EXPIRED:
        message = "Authentication expired!\nPlease log in again.";
        break;
    case ZJU_ERROR::OTHER:
        message = "Other error!\nUnknown reason. You can send the log to the developer for investigation.";
        break;
    case ZJU_ERROR::NONE:
        return;
    }
    QMessageBox::critical(parentWidget, "Error", message);
}
