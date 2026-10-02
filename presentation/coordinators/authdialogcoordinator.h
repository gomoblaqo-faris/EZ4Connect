#ifndef AUTHDIALOGCOORDINATOR_H
#define AUTHDIALOGCOORDINATOR_H

#include <QPointer>

#include "application/authprompter.h"

class GraphCaptchaWindow;
class LoginWindow;
class QDialog;
class SsoLoginWebView;
class SudoWindow;
class QWidget;

// Asks the user with dialogs.
class AuthDialogCoordinator : public AuthPrompter
{
    Q_OBJECT

public:
    explicit AuthDialogCoordinator(QWidget *parentWidget, QObject *parent = nullptr);

    void requestLogin(const QString &username, const QString &password) override;
    void requestPhoneNumber(
        const QString &countryCode,
        const QString &phoneNumber
    ) override;
    void requestSudoPassword() override;
    void requestGraphCaptcha(const QString &graphFile, bool textInput) override;
    void requestSmsCode(bool showSkipSecondaryAuthOption) override;
    void requestTotpCode() override;
    void requestRadiusCode(bool showSkipSecondaryAuthOption) override;
    void requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl) override;
    ProxyOverwriteAnswer askProxyOverwrite() override;

private:
    QWidget *parentWidget;
    QPointer<LoginWindow> loginWindow;
    QPointer<QDialog> phoneNumberDialog;
    QPointer<SudoWindow> sudoWindow;
    QPointer<GraphCaptchaWindow> graphCaptchaWindow;
    QPointer<SsoLoginWebView> ssoLoginWebView;
};

#endif // AUTHDIALOGCOORDINATOR_H
