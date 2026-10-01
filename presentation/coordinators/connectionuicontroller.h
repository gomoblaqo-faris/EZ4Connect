#ifndef CONNECTIONUICONTROLLER_H
#define CONNECTIONUICONTROLLER_H

#include <QObject>
#include <QSystemTrayIcon>

#include <functional>

#include "core/connectionerror.h"
#include "application/systemproxybackend.h"
#include "core/connectionsessionstate.h"

class QAction;
class ApplicationLogger;
class AuthDialogCoordinator;
class ConnectionSession;
class QPushButton;
class QSettings;
class SystemProxySession;
class QWidget;

class ConnectionUiController : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<QSettings *()>;
    using ProfileIdProvider = std::function<QString()>;
    using NotificationHandler = std::function<void(
        const QString &title,
        const QString &content,
        QSystemTrayIcon::MessageIcon icon
    )>;

    ConnectionUiController(
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
        QObject *parent = nullptr
    );

    // Turns the system proxy off at the user's request, so it is not turned
    // back on after a reconnect.
    void clearSystemProxy();

private:
    void handleConnectClicked();
    void handleProxyClicked();
    void enableSystemProxy();
    void syncSystemProxy();
    void handleConnectionStateChanged(ConnectionState state);
    void startConnection(
        const QString &username,
        const QString &password,
        const QString &phone = QString()
    );
    void showConnectionError(ZJU_ERROR error);
    QSettings *settings() const;

    QWidget *parentWidget;
    QPushButton *connectButton;
    QPushButton *proxyButton;
    QAction *trayConnectAction;
    ConnectionSession *connectionSession;
    SystemProxySession *systemProxySession;
    AuthDialogCoordinator *authenticationDialogs;
    SettingsProvider settingsProvider;
    ProfileIdProvider profileIdProvider;
    NotificationHandler notificationHandler;
    // Whether the proxy should be on whenever this session is connected.
    bool proxyWanted = false;
    bool proxyIntentInitialised = false;
    bool proxySyncPending = false;
    // Taken when the session starts, together with the profile the core is
    // given, so that the system proxy always points at the ports that core
    // listens on even if the settings are edited meanwhile.
    SystemProxyConfig sessionProxyConfig;
};

#endif // CONNECTIONUICONTROLLER_H
