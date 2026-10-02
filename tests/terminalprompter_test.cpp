// The terminal prompter, with a pipe in place of the keyboard.
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>

#include <functional>
#include <unistd.h>

#include "terminalinput.h"
#include "terminalprompter.h"

namespace
{
// What the prompter told the rest of the program, in order.
struct Answers
{
    explicit Answers(TerminalPrompter &prompter)
    {
        QObject::connect(&prompter, &AuthPrompter::loginSubmitted,
                         [this](const QString &username, const QString &password, bool save)
                         {
                             events << QString("login:%1:%2:%3").arg(username, password, save ? "save" : "once");
                         });
        QObject::connect(&prompter, &AuthPrompter::phoneNumberSubmitted,
                         [this](const QString &countryCode, const QString &phoneNumber, bool save)
                         {
                             events << QString("phone:%1-%2:%3").arg(countryCode, phoneNumber, save ? "save" : "once");
                         });
        QObject::connect(&prompter, &AuthPrompter::sudoPasswordSubmitted,
                         [this](const QString &password, bool) { events << "sudo:" + password; });
        QObject::connect(&prompter, &AuthPrompter::interactiveInputSubmitted,
                         [this](const QByteArray &input) { events << "input:" + QString::fromLocal8Bit(input).trimmed(); });
        QObject::connect(&prompter, &AuthPrompter::interactiveInputCancelled,
                         [this]() { events << "cancelled"; });
        QObject::connect(&prompter, &AuthPrompter::loginAbandoned,
                         [this]() { events << "abandoned"; });
    }

    QStringList events;
};

struct Fixture
{
    Fixture()
    {
        if (::pipe(keyboard) != 0 || ::pipe(screen) != 0)
        {
            qFatal("could not create pipes");
        }
        terminal = new TerminalInput(keyboard[0], screen[1]);
        prompter = new TerminalPrompter(terminal);
        answers = new Answers(*prompter);
    }

    ~Fixture()
    {
        delete answers;
        delete prompter;
        delete terminal;
        for (const int descriptor : {keyboard[0], keyboard[1], screen[0], screen[1]})
        {
            if (descriptor >= 0)
            {
                ::close(descriptor);
            }
        }
    }

    void type(const QByteArray &text)
    {
        const ssize_t written = ::write(keyboard[1], text.constData(), text.size());
        (void)written;
    }

    void endInput()
    {
        ::close(keyboard[1]);
        keyboard[1] = -1;
    }

    bool waitForEvents(int count)
    {
        QElapsedTimer timer;
        timer.start();
        while (answers->events.size() < count)
        {
            if (timer.elapsed() > 5000)
            {
                return false;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return true;
    }

    int keyboard[2] = {-1, -1};
    int screen[2] = {-1, -1};
    TerminalInput *terminal = nullptr;
    TerminalPrompter *prompter = nullptr;
    Answers *answers = nullptr;
};

bool expectEvents(Fixture &fixture, const QStringList &expected, const char *testName)
{
    if (fixture.waitForEvents(expected.size()) && fixture.answers->events == expected)
    {
        return true;
    }
    qCritical().noquote() << testName << "failed"
                          << "\nexpected:" << expected.join(" | ")
                          << "\nactual:  " << fixture.answers->events.join(" | ");
    return false;
}

bool collectsALoginAcrossThreeQuestions()
{
    Fixture fixture;
    // Typed ahead, as when the answers are piped in.
    fixture.type("alice\nsecret word\ny\n");
    fixture.prompter->requestLogin(QString(), QString());
    return expectEvents(fixture, {"login:alice:secret word:save"}, "collectsALoginAcrossThreeQuestions");
}

bool keepsTheSuggestedAccountOnAnEmptyAnswer()
{
    Fixture fixture;
    fixture.prompter->requestLogin("bob", QString());
    fixture.type("\n");
    fixture.type("hunter2\n");
    fixture.type("\n");
    return expectEvents(fixture, {"login:bob:hunter2:once"}, "keepsTheSuggestedAccountOnAnEmptyAnswer");
}

bool givesUpWhenTheInputEnds()
{
    Fixture fixture;
    fixture.endInput();
    fixture.prompter->requestLogin(QString(), QString());
    return expectEvents(fixture, {"abandoned"}, "givesUpWhenTheInputEnds");
}

bool collectsAPhoneNumber()
{
    Fixture fixture;
    fixture.type("\n13800000000\nyes\n");
    fixture.prompter->requestPhoneNumber("86", QString());
    return expectEvents(fixture, {"phone:86-13800000000:save"}, "collectsAPhoneNumber");
}

bool marksACodeToSkipTheStepNextTime()
{
    Fixture fixture;
    fixture.type("123456\ny\n654321\nn\n111111\n\n");
    fixture.prompter->requestSmsCode(true);
    fixture.prompter->requestRadiusCode(true);
    fixture.prompter->requestTotpCode();
    // Asked while the earlier questions are still open: an empty answer.
    fixture.prompter->requestSmsCode(false);
    return expectEvents(
        fixture,
        {"input:$123456", "input:654321", "input:111111", "cancelled"},
        "marksACodeToSkipTheStepNextTime"
    );
}

bool cannotAnswerAPointCaptcha()
{
    Fixture fixture;
    fixture.prompter->requestGraphCaptcha("/nonexistent/graph.jpg", false);
    return expectEvents(fixture, {"cancelled"}, "cannotAnswerAPointCaptcha");
}

bool passesTheSsoAddressAndTheSudoPasswordOn()
{
    Fixture fixture;
    fixture.type("https://vpn.example.edu/callback?ticket=ST-1\nroot password\n");
    fixture.prompter->requestSsoLogin(QUrl("https://vpn.example.edu"), QUrl("https://sso.example.edu/login"));
    fixture.prompter->requestSudoPassword();
    return expectEvents(
        fixture,
        {"input:https://vpn.example.edu/callback?ticket=ST-1", "sudo:root password"},
        "passesTheSsoAddressAndTheSudoPasswordOn"
    );
}

bool onlyReplacesAnotherProxyWhenToldTo()
{
    Fixture fixture;
    const bool refusedByDefault = !fixture.prompter->askProxyOverwrite().overwrite;
    fixture.prompter->setOverwriteProxy(true);
    const bool allowedWhenSet = fixture.prompter->askProxyOverwrite().overwrite;
    if (!refusedByDefault || !allowedWhenSet)
    {
        qCritical() << "onlyReplacesAnotherProxyWhenToldTo failed";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return collectsALoginAcrossThreeQuestions()
        && keepsTheSuggestedAccountOnAnEmptyAnswer()
        && givesUpWhenTheInputEnds()
        && collectsAPhoneNumber()
        && marksACodeToSkipTheStepNextTime()
        && cannotAnswerAPointCaptcha()
        && passesTheSsoAddressAndTheSudoPasswordOn()
        && onlyReplacesAnotherProxyWhenToldTo() ? 0 : 1;
}
