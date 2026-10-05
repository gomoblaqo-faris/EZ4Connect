#ifndef CONNECTIONUICONTROLLER_H
#define CONNECTIONUICONTROLLER_H

#include <QObject>
#include <QSystemTrayIcon>

#include <functional>

#include "application/connectionflow.h"

class QAction;
class ApplicationLogger;
class ConnectionSession;
class QPushButton;
class QSettings;
class QWidget;

// Puts the connect flow on screen: the connect and proxy buttons drive it,
// and what it reports becomes button text, message boxes and notifications.
class ConnectionUiController : public QObject
{
    Q_OBJECT

public:
    using SettingsProvider = std::function<QSettings *()>;
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
        ConnectionFlow *connectionFlow,
        ConnectionSession *connectionSession,
        ApplicationLogger *applicationLogger,
        SettingsProvider settingsProvider,
        NotificationHandler notificationHandler,
        QObject *parent = nullptr
    );

    // Turns the system proxy off at the user's request, so it is not turned
    // back on after a reconnect.
    void clearSystemProxy();

    // Sets the text this controller puts on the buttons again, in the
    // current language.
    void retranslate();

private:
    void handleConnectClicked();
    void updateConnectText();
    void showObstacle(ConnectionFlow::Obstacle obstacle);

    QWidget *parentWidget;
    QPushButton *connectButton;
    QPushButton *proxyButton;
    QAction *trayConnectAction;
    ConnectionFlow *connectionFlow;
    ConnectionSession *connectionSession;
    SettingsProvider settingsProvider;
    NotificationHandler notificationHandler;
    bool connectionRunning = false;
};

#endif // CONNECTIONUICONTROLLER_H
