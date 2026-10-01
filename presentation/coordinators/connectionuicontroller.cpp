#include "connectionuicontroller.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDebug>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QWidget>

#include <utility>

#include "application/applicationlogger.h"
#include "application/connectionsession.h"
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
                settings()->setValue("Credential/Username", username);
                settings()->setValue(
                    "Credential/Password",
                    QString(password.toUtf8().toBase64())
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
                settings()->setValue("ZJUConnect/PhoneCountryCode", countryCode);
                settings()->setValue("ZJUConnect/PhoneNumber", phoneNumber);
                settings()->sync();
            }

            const QString username =
                settings()->value("Credential/Username", "").toString();
            const QString password = QByteArray::fromBase64(
                settings()->value("Credential/Password", "").toString().toUtf8()
            );
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
        [](int) { qInfo().noquote() << "Reconnecting..."; }
    );
    connect(
        connectionSession,
        &ConnectionSession::finished,
        this,
        [this](ZJU_ERROR error)
        {
            qInfo().noquote() << "VPN disconnected.";
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

    if (settings()->contains("ZJUConnect/ServerAddress") &&
        settings()->value("ZJUConnect/ServerAddress").toString().isEmpty())
    {
        QMessageBox::critical(parentWidget, "Error", "The server address is required.");
        return;
    }

    const QString username = settings()->value("Credential/Username", "").toString();
    const QString password = QByteArray::fromBase64(
        settings()->value("Credential/Password", "").toString().toUtf8()
    );
    const QString protocol =
        settings()->value("ZJUConnect/Protocol", "easyconnect").toString();
    const QString authType =
        settings()->value("ZJUConnect/AuthType", "psw").toString();
    const QString easyconnectAuthType = settings()->value(
        "ZJUConnect/EasyConnectAuthType",
        settings()->value("Credential/CertFile", "").toString().isEmpty()
            ? "password"
            : "certificate"
    ).toString();

    if (protocol == "easyconnect"
        && easyconnectAuthType == "certificate"
        && settings()->value("Credential/CertFile", "").toString().isEmpty())
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
    if (settings()->value("ZJUConnect/TUNMode").toBool() &&
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
        QString countryCode = settings()
            ->value("ZJUConnect/PhoneCountryCode", "86")
            .toString()
            .trimmed();
        const QString phoneNumber = settings()
            ->value("ZJUConnect/PhoneNumber", "")
            .toString()
            .trimmed();
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
        systemProxySession->disable();
        return;
    }

    const int httpPort = settings()->value("ZJUConnect/HTTPPort").toInt();
    const int socksPort = settings()->value("ZJUConnect/SOCKS5Port").toInt();
    const SystemProxyConfig proxyConfig{
        httpPort,
        socksPort,
        settings()->value("Common/SystemProxyBypass").toString()
    };
    connect(
        systemProxySession,
        &SystemProxySession::conflictCheckFinished,
        this,
        [this, proxyConfig, httpPort, socksPort](bool conflict)
        {
            if (!connectionSession->isActive())
            {
                return;
            }

            if (conflict &&
                !settings()->value(
                    "Common/SuppressProxyOverrideWarning",
                    false
                ).toBool())
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
                    return;
                }
                if (dontShowCheckBox->isChecked())
                {
                    settings()->setValue(
                        "Common/SuppressProxyOverrideWarning",
                        true
                    );
                    settings()->sync();
                }
            }
            else if (conflict)
            {
                qInfo().noquote() << "Skipping the system proxy overwrite warning (suppressed in settings)";
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
    const ReconnectPolicy reconnectPolicy{
        settings()->value("Common/AutoReconnect", false).toBool(),
        settings()->value("Common/ReconnectTime", 1).toInt() * 1000
    };
    if (!connectionSession->start(profile, reconnectPolicy)
        || !connectionSession->isActive())
    {
        return;
    }

    connectButton->setText("Disconnect");
    trayConnectAction->setText("Disconnect");
    proxyButton->show();

    if (settings()->value("Common/AutoSetProxy", false).toBool())
    {
        proxyButton->click();
    }
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
