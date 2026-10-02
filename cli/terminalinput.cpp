#include "terminalinput.h"

#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <termios.h>
#include <unistd.h>

#include <utility>

namespace
{
// What the signal handlers below need to put the terminal back. Echo is
// only ever turned off on one terminal, the one passwords are typed on.
int secretTerminal = -1;
struct termios originalAttributes {};
volatile sig_atomic_t attributesSaved = 0;
volatile sig_atomic_t echoHidden = 0;

void restoreEchoFromSignal()
{
    if (attributesSaved && echoHidden)
    {
        ::tcsetattr(secretTerminal, TCSANOW, &originalAttributes);
    }
}

void handleContinue(int);

// Ctrl+Z while a password is being typed would otherwise hand the shell a
// terminal that does not echo.
void handleStop(int)
{
    const int savedErrno = errno;
    restoreEchoFromSignal();
    ::signal(SIGTSTP, SIG_DFL);
    ::raise(SIGTSTP);
    errno = savedErrno;
}

void handleContinue(int)
{
    const int savedErrno = errno;
    ::signal(SIGTSTP, handleStop);
    if (attributesSaved && echoHidden)
    {
        struct termios hidden = originalAttributes;
        hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        ::tcsetattr(secretTerminal, TCSANOW, &hidden);
    }
    errno = savedErrno;
}

// Ctrl+\ ends the process without running any destructor.
void handleQuit(int signalNumber)
{
    restoreEchoFromSignal();
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

void installTerminalSignalHandlers()
{
    static bool handlersInstalled = false;
    if (handlersInstalled)
    {
        return;
    }
    handlersInstalled = true;
    ::signal(SIGTSTP, handleStop);
    ::signal(SIGCONT, handleContinue);
    ::signal(SIGQUIT, handleQuit);
}
}

TerminalInput::TerminalInput(int inputFd, int promptFd, QObject *parent)
    : QObject(parent),
      inputFd(inputFd),
      promptFd(promptFd)
{
    // Only enabled while a question is open: at the end of the input the
    // descriptor is readable forever, and listening would spin.
    notifier = new QSocketNotifier(inputFd, QSocketNotifier::Read, this);
    notifier->setEnabled(false);
    connect(notifier, &QSocketNotifier::activated, this, &TerminalInput::handleReadable);
}

TerminalInput::~TerminalInput()
{
    // Leaving a terminal with echo off makes it look broken to its user.
    showEcho();
}

void TerminalInput::ask(const QString &prompt, bool echo, Answer answer)
{
    if (followUpPosition >= 0)
    {
        pending.insert(followUpPosition++, {prompt, echo, std::move(answer)});
        return;
    }
    pending.enqueue({prompt, echo, std::move(answer)});
    if (!asking)
    {
        startNext();
    }
}

void TerminalInput::say(const QString &text)
{
    const QByteArray bytes = text.toLocal8Bit() + '\n';
    const ssize_t written = ::write(promptFd, bytes.constData(), bytes.size());
    (void)written;
}

void TerminalInput::startNext()
{
    if (pending.isEmpty())
    {
        return;
    }

    asking = true;
    const Request &request = pending.head();
    if (inputEnded)
    {
        finishCurrent(std::nullopt);
        return;
    }

    // Echo goes off before the question appears, so that an answer typed
    // the moment it shows is already hidden.
    if (!request.echo && ::isatty(inputFd))
    {
        if (!hideEcho())
        {
            say("Cannot hide what is typed on this terminal, so the question was not asked.");
            finishCurrent(std::nullopt);
            return;
        }
        // Whatever was typed ahead of a secret question was echoed when it
        // was typed, and was not meant as its answer.
        ::tcflush(inputFd, TCIFLUSH);
        buffer.clear();
    }

    const QByteArray prompt = request.prompt.toLocal8Bit();
    const ssize_t written = ::write(promptFd, prompt.constData(), prompt.size());
    (void)written;

    // The line may already have been typed, or piped in ahead of time.
    if (!deliverBufferedLine())
    {
        notifier->setEnabled(true);
    }
}

void TerminalInput::handleReadable()
{
    char chunk[512];
    ssize_t count = 0;
    do
    {
        count = ::read(inputFd, chunk, sizeof(chunk));
    } while (count < 0 && errno == EINTR);

    if (count <= 0)
    {
        inputEnded = true;
        notifier->setEnabled(false);
        // An unfinished last line still counts as an answer.
        if (!buffer.isEmpty())
        {
            const QString line = QString::fromLocal8Bit(buffer);
            buffer.clear();
            finishCurrent(line);
            return;
        }
        finishCurrent(std::nullopt);
        return;
    }

    buffer.append(chunk, static_cast<qsizetype>(count));
    deliverBufferedLine();
}

bool TerminalInput::deliverBufferedLine()
{
    const qsizetype newline = buffer.indexOf('\n');
    if (newline < 0)
    {
        return false;
    }

    QByteArray line = buffer.left(newline);
    buffer.remove(0, newline + 1);
    if (line.endsWith('\r'))
    {
        line.chop(1);
    }
    notifier->setEnabled(false);
    finishCurrent(QString::fromLocal8Bit(line));
    return true;
}

void TerminalInput::finishCurrent(std::optional<QString> line)
{
    if (!asking || pending.isEmpty())
    {
        return;
    }

    const Request request = pending.dequeue();
    if (echoHidden)
    {
        showEcho();
        // The newline the user typed was not echoed either.
        const ssize_t written = ::write(promptFd, "\n", 1);
        (void)written;
    }
    asking = false;
    followUpPosition = 0;
    request.answer(std::move(line));
    followUpPosition = -1;
    startNext();
}

bool TerminalInput::hideEcho()
{
    if (echoHidden)
    {
        return true;
    }

    // The state the terminal was found in is what gets put back, whether or
    // not it had echo on.
    if (!attributesSaved)
    {
        if (::tcgetattr(inputFd, &originalAttributes) != 0)
        {
            return false;
        }
        secretTerminal = inputFd;
        attributesSaved = 1;
        installTerminalSignalHandlers();
    }

    struct termios hidden = originalAttributes;
    hidden.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    int result = 0;
    do
    {
        result = ::tcsetattr(inputFd, TCSANOW, &hidden);
    } while (result != 0 && errno == EINTR);
    if (result != 0)
    {
        return false;
    }
    echoHidden = 1;
    return true;
}

void TerminalInput::showEcho()
{
    if (!echoHidden)
    {
        return;
    }
    int result = 0;
    do
    {
        result = ::tcsetattr(inputFd, TCSANOW, &originalAttributes);
    } while (result != 0 && errno == EINTR);
    echoHidden = 0;
}
