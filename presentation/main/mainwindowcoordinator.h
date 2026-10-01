#ifndef MAINWINDOWCOORDINATOR_H
#define MAINWINDOWCOORDINATOR_H

#include <QObject>

#include <memory>

class AuthDialogCoordinator;
class ConnectionSession;
class ProfileService;
class SecretStore;
class SystemProxySession;
class UpdateChecker;
class QWidget;

class MainWindowCoordinator : public QObject
{
    Q_OBJECT

public:
    MainWindowCoordinator(
        QWidget *parentWidget,
        const QString &overrideConfigPath,
        QObject *parent = nullptr
    );

    ProfileService *profiles() const;
    ConnectionSession *connection() const;
    SystemProxySession *systemProxy() const;
    UpdateChecker *updates() const;
    AuthDialogCoordinator *authenticationDialogs() const;

    ~MainWindowCoordinator() override;

    void prepareForShutdown();

private:
    std::unique_ptr<SecretStore> secretStore;
    ProfileService *profileService;
    ConnectionSession *connectionSession;
    SystemProxySession *systemProxySession;
    UpdateChecker *updateChecker;
    AuthDialogCoordinator *authenticationCoordinator;
};

#endif // MAINWINDOWCOORDINATOR_H
