#include "connectionuicontroller.h"

#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QWidget>

#include <utility>

#include "application/applicationlogger.h"
#include "application/connectionsession.h"
#include "application/profilesettings.h"
#include "infrastructure/platform/privileges.h"

ConnectionUiController::ConnectionUiController(
    QWidget *parentWidget,
    QPushButton *connectButton,
    QPushButton *proxyButton,
    QAction *trayConnectAction,
    ConnectionFlow *connectionFlow,
    ConnectionSession *connectionSession,
    ApplicationLogger *applicationLogger,
    SettingsProvider settingsProvider,
    NotificationHandler notificationHandler,
    QObject *parent
)
    : QObject(parent),
      parentWidget(parentWidget),
      connectButton(connectButton),
      proxyButton(proxyButton),
      trayConnectAction(trayConnectAction),
      connectionFlow(connectionFlow),
      connectionSession(connectionSession),
      settingsProvider(std::move(settingsProvider)),
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

    connect(connectionFlow, &ConnectionFlow::connectionStarted, this, [this]()
    {
        this->connectButton->setText("Disconnect");
        this->trayConnectAction->setText("Disconnect");
    });
    connect(connectionFlow, &ConnectionFlow::connectionEnded, this, [this]()
    {
        this->connectButton->setText("Connect");
        this->trayConnectAction->setText("Connect");
    });
    connect(connectionFlow, &ConnectionFlow::proxyControlAvailableChanged, this,
            [this](bool available) { this->proxyButton->setVisible(available); });
    connect(connectionFlow, &ConnectionFlow::droppedUnexpectedly, this, [this]()
    {
        this->notificationHandler(
            "VPN",
            "VPN disconnected unexpectedly!",
            QSystemTrayIcon::MessageIcon::Warning
        );
    });
    connect(connectionFlow, &ConnectionFlow::failed, this, [this](const QString &message)
    {
        QMessageBox::critical(this->parentWidget, "Error", message);
    });
    connect(connectionFlow, &ConnectionFlow::proxyFailed, this, [this](const QString &error)
    {
        QMessageBox::critical(this->parentWidget, "System Proxy", error);
    });
    connect(connectionFlow, &ConnectionFlow::cannotConnect,
            this, &ConnectionUiController::showObstacle);

    connect(
        connectButton,
        &QPushButton::clicked,
        this,
        &ConnectionUiController::handleConnectClicked
    );
    connect(
        proxyButton,
        &QPushButton::clicked,
        connectionFlow,
        &ConnectionFlow::toggleSystemProxy
    );
}

void ConnectionUiController::handleConnectClicked()
{
    if (connectionSession->isActive())
    {
        connectionFlow->disconnectFromServer();
        return;
    }

#if defined(Q_OS_WIN)
    // A profile that cannot connect anyway should say so, not ask for
    // administrator privileges first.
    if (!connectionFlow->obstacle().has_value() &&
        ProfileSettings::read(*settingsProvider(), ProfileSettings::TUNMode) &&
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

    connectionFlow->connectToServer();
}

void ConnectionUiController::clearSystemProxy()
{
    connectionFlow->clearSystemProxy();
}

void ConnectionUiController::showObstacle(ConnectionFlow::Obstacle obstacle)
{
    switch (obstacle)
    {
    case ConnectionFlow::Obstacle::MissingServerAddress:
        QMessageBox::critical(parentWidget, "Error", "The server address is required.");
        break;
    case ConnectionFlow::Obstacle::MissingCertificate:
        QMessageBox::information(
            parentWidget,
            "Certificate Required",
            "This profile uses certificate authentication.\n"
            "Choose a certificate file via Profile → Setup Guide."
        );
        break;
    }
}
