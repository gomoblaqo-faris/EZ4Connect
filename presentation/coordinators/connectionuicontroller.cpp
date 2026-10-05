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
    updateConnectText();
    proxyButton->setText(tr("Set System Proxy"));
    proxyButton->hide();

    connect(
        connectionSession,
        &ConnectionSession::outputRead,
        applicationLogger,
        &ApplicationLogger::appendCoreOutput
    );

    connect(connectionFlow, &ConnectionFlow::connectionStarted, this, [this]()
    {
        connectionRunning = true;
        updateConnectText();
    });
    connect(connectionFlow, &ConnectionFlow::connectionEnded, this, [this]()
    {
        connectionRunning = false;
        updateConnectText();
    });
    connect(connectionFlow, &ConnectionFlow::proxyControlAvailableChanged, this,
            [this](bool available) { this->proxyButton->setVisible(available); });
    connect(connectionFlow, &ConnectionFlow::droppedUnexpectedly, this, [this]()
    {
        this->notificationHandler(
            QStringLiteral("VPN"),
            tr("VPN disconnected unexpectedly!"),
            QSystemTrayIcon::MessageIcon::Warning
        );
    });
    connect(connectionFlow, &ConnectionFlow::failed, this, [this](const QString &message)
    {
        QMessageBox::critical(this->parentWidget, tr("Error"), message);
    });
    connect(connectionFlow, &ConnectionFlow::proxyFailed, this, [this](const QString &error)
    {
        QMessageBox::critical(this->parentWidget, tr("System Proxy"), error);
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
                tr("Elevation Failed"),
                tr("Could not relaunch with administrator privileges. Please run the app as administrator.")
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

void ConnectionUiController::retranslate()
{
    updateConnectText();
}

void ConnectionUiController::updateConnectText()
{
    const QString text = connectionRunning ? tr("Disconnect") : tr("Connect");
    connectButton->setText(text);
    trayConnectAction->setText(text);
}

void ConnectionUiController::showObstacle(ConnectionFlow::Obstacle obstacle)
{
    switch (obstacle)
    {
    case ConnectionFlow::Obstacle::MissingServerAddress:
        QMessageBox::critical(parentWidget, tr("Error"), tr("The server address is required."));
        break;
    case ConnectionFlow::Obstacle::MissingCertificate:
        QMessageBox::information(
            parentWidget,
            tr("Certificate Required"),
            tr("This profile uses certificate authentication.\n"
               "Choose a certificate file via Profile → Setup Guide.")
        );
        break;
    }
}
