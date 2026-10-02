#include "terminalprompter.h"

#include <QProcess>

#include "terminalinput.h"

namespace
{
bool saidYes(const std::optional<QString> &answer)
{
    const QString text = answer.value_or(QString()).trimmed().toLower();
    return text == "y" || text == "yes";
}
}

TerminalPrompter::TerminalPrompter(TerminalInput *terminal, QObject *parent)
    : AuthPrompter(parent),
      terminal(terminal)
{
}

void TerminalPrompter::setOverwriteProxy(bool allowed)
{
    overwriteProxy = allowed;
}

void TerminalPrompter::requestLogin(const QString &username, const QString &)
{
    const QString accountPrompt = username.isEmpty()
        ? QString("Account: ")
        : QString("Account [%1]: ").arg(username);
    terminal->ask(accountPrompt, true, [this, username](std::optional<QString> account)
    {
        if (!account.has_value())
        {
            // Nothing was started yet, so there is nothing to cancel.
            terminal->say("No account entered; not connecting.");
            emit loginAbandoned();
            return;
        }
        const QString chosenAccount = account->trimmed().isEmpty() ? username : account->trimmed();
        terminal->ask("Password: ", false, [this, chosenAccount](std::optional<QString> password)
        {
            if (!password.has_value() || chosenAccount.isEmpty() || password->isEmpty())
            {
                terminal->say("Account and password are both needed; not connecting.");
                emit loginAbandoned();
                return;
            }
            const QString chosenPassword = *password;
            terminal->ask("Save them in the profile? [y/N]: ", true,
                          [this, chosenAccount, chosenPassword](std::optional<QString> save)
            {
                emit loginSubmitted(chosenAccount, chosenPassword, saidYes(save));
            });
        });
    });
}

void TerminalPrompter::requestPhoneNumber(const QString &countryCode, const QString &phoneNumber)
{
    terminal->ask(QString("Country code [%1]: ").arg(countryCode), true,
                  [this, countryCode, phoneNumber](std::optional<QString> code)
    {
        if (!code.has_value())
        {
            terminal->say("No phone number entered; not connecting.");
            emit loginAbandoned();
            return;
        }
        const QString chosenCode = code->trimmed().isEmpty() ? countryCode : code->trimmed();
        const QString numberPrompt = phoneNumber.isEmpty()
            ? QString("Phone number: ")
            : QString("Phone number [%1]: ").arg(phoneNumber);
        terminal->ask(numberPrompt, true,
                      [this, chosenCode, phoneNumber](std::optional<QString> number)
        {
            const QString chosenNumber =
                number.value_or(QString()).trimmed().isEmpty() ? phoneNumber : number->trimmed();
            if (chosenNumber.isEmpty())
            {
                terminal->say("No phone number entered; not connecting.");
                emit loginAbandoned();
                return;
            }
            terminal->ask("Save it in the profile? [y/N]: ", true,
                          [this, chosenCode, chosenNumber](std::optional<QString> save)
            {
                emit phoneNumberSubmitted(chosenCode, chosenNumber, saidYes(save));
            });
        });
    });
}

void TerminalPrompter::requestSudoPassword()
{
    terminal->say("TUN mode needs administrator privileges.");
    terminal->ask("sudo password: ", false, [this](std::optional<QString> password)
    {
        // An empty password stops the session. A password that works is kept
        // for this run only, so that a reconnect does not ask again.
        emit sudoPasswordSubmitted(password.value_or(QString()), true);
    });
}

void TerminalPrompter::requestGraphCaptcha(const QString &graphFile, bool textInput)
{
    if (!textInput)
    {
        terminal->say(
            "This login asks you to pick points on a picture, which cannot be done on a "
            "terminal. Use the graphical app for this profile."
        );
        emit interactiveInputCancelled();
        return;
    }

    terminal->say("The captcha picture is at: " + graphFile);
#if defined(Q_OS_MACOS)
    QProcess::startDetached("open", {graphFile});
#else
    QProcess::startDetached("xdg-open", {graphFile});
#endif
    terminal->ask("Characters in the picture: ", true, [this](std::optional<QString> code)
    {
        if (code.value_or(QString()).trimmed().isEmpty())
        {
            emit interactiveInputCancelled();
            return;
        }
        emit interactiveInputSubmitted(code->trimmed().toLocal8Bit() + "\n");
    });
}

void TerminalPrompter::requestCode(const QString &prompt, bool showSkipSecondaryAuthOption)
{
    terminal->ask(prompt, true, [this, showSkipSecondaryAuthOption](std::optional<QString> code)
    {
        const QString entered = code.value_or(QString()).trimmed();
        if (entered.isEmpty())
        {
            emit interactiveInputCancelled();
            return;
        }
        if (!showSkipSecondaryAuthOption)
        {
            emit interactiveInputSubmitted(entered.toLocal8Bit() + "\n");
            return;
        }
        terminal->ask("Skip this step on future logins? [y/N]: ", true,
                      [this, entered](std::optional<QString> skip)
        {
            // The core takes a leading "$" as "do not ask again".
            const QByteArray prefix = saidYes(skip) ? QByteArray("$") : QByteArray();
            emit interactiveInputSubmitted(prefix + entered.toLocal8Bit() + "\n");
        });
    });
}

void TerminalPrompter::requestSmsCode(bool showSkipSecondaryAuthOption)
{
    requestCode("SMS code: ", showSkipSecondaryAuthOption);
}

void TerminalPrompter::requestTotpCode()
{
    requestCode("TOTP code: ", false);
}

void TerminalPrompter::requestRadiusCode(bool showSkipSecondaryAuthOption)
{
    requestCode("RADIUS token (the SMS code you received): ", showSkipSecondaryAuthOption);
}

void TerminalPrompter::requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl)
{
    terminal->say("Open this address in a browser and log in:");
    terminal->say("  " + loginUrl.toString());
    terminal->say(
        "When the login is done the browser ends up on an address that starts with "
            + serverUrl.toString() + ". Copy that whole address and paste it here."
    );
    terminal->ask("Address: ", true, [this](std::optional<QString> address)
    {
        if (address.value_or(QString()).trimmed().isEmpty())
        {
            emit interactiveInputCancelled();
            return;
        }
        emit interactiveInputSubmitted(address->trimmed().toLocal8Bit() + "\n");
    });
}

AuthPrompter::ProxyOverwriteAnswer TerminalPrompter::askProxyOverwrite()
{
    if (!overwriteProxy)
    {
        terminal->say(
            "A system proxy is already configured by something else, so it was left alone. "
            "Run with --overwrite-proxy to replace it."
        );
    }
    return {overwriteProxy, false};
}
