#ifndef AUTHPROMPTER_H
#define AUTHPROMPTER_H

#include <QObject>
#include <QString>
#include <QUrl>

// Port for everything the connect flow has to ask the user. The GUI answers
// with dialogs and the command-line client with terminal prompts.
//
// Requests return at once. Their answers arrive through the signals, because
// a dialog or a terminal can take any amount of time and the connection must
// keep being serviced meanwhile.
class AuthPrompter : public QObject
{
    Q_OBJECT

public:
    struct ProxyOverwriteAnswer
    {
        bool overwrite = false;
        // Do not ask again for this profile.
        bool remember = false;
    };

    explicit AuthPrompter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual void requestLogin(const QString &username, const QString &password) = 0;
    virtual void requestPhoneNumber(const QString &countryCode, const QString &phoneNumber) = 0;
    virtual void requestSudoPassword() = 0;
    // The answer is the characters in the image when textInput is set, and
    // the points the user picked on it otherwise.
    virtual void requestGraphCaptcha(const QString &graphFile, bool textInput) = 0;
    virtual void requestSmsCode(bool showSkipSecondaryAuthOption) = 0;
    virtual void requestTotpCode() = 0;
    virtual void requestRadiusCode(bool showSkipSecondaryAuthOption) = 0;
    // The answer is the URL on serverUrl that the login redirects to.
    virtual void requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl) = 0;

    // Something else has configured a system proxy. Unlike the requests
    // above this is answered before it returns.
    virtual ProxyOverwriteAnswer askProxyOverwrite() = 0;

signals:
    void loginSubmitted(const QString &username, const QString &password, bool saveDetails);
    void phoneNumberSubmitted(
        const QString &countryCode,
        const QString &phoneNumber,
        bool saveDetails
    );
    void sudoPasswordSubmitted(const QString &password, bool remember);
    void interactiveInputSubmitted(const QByteArray &input);
    void interactiveInputCancelled();
    // The user gave up on requestLogin or requestPhoneNumber. No session was
    // started, so there is nothing to cancel; a front end that exists only
    // to connect uses this to stop waiting.
    void loginAbandoned();
};

#endif // AUTHPROMPTER_H
