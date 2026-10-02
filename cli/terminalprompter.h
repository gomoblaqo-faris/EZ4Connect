#ifndef TERMINALPROMPTER_H
#define TERMINALPROMPTER_H

#include "application/authprompter.h"

class TerminalInput;

// Asks the user on the terminal.
class TerminalPrompter : public AuthPrompter
{
    Q_OBJECT

public:
    explicit TerminalPrompter(TerminalInput *terminal, QObject *parent = nullptr);

    // Whether a system proxy that something else configured may be replaced.
    // There is no good moment to ask this on a terminal, so it is decided up
    // front, on the command line.
    void setOverwriteProxy(bool allowed);

    void requestLogin(const QString &username, const QString &password) override;
    void requestPhoneNumber(const QString &countryCode, const QString &phoneNumber) override;
    void requestSudoPassword() override;
    void requestGraphCaptcha(const QString &graphFile, bool textInput) override;
    void requestSmsCode(bool showSkipSecondaryAuthOption) override;
    void requestTotpCode() override;
    void requestRadiusCode(bool showSkipSecondaryAuthOption) override;
    void requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl) override;
    ProxyOverwriteAnswer askProxyOverwrite() override;

private:
    void requestCode(const QString &prompt, bool showSkipSecondaryAuthOption);

    TerminalInput *terminal;
    bool overwriteProxy = false;
};

#endif // TERMINALPROMPTER_H
