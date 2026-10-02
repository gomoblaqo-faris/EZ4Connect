#ifndef TERMINALINPUT_H
#define TERMINALINPUT_H

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>

#include <functional>
#include <optional>

class QSocketNotifier;

// Asks questions on a terminal without blocking the event loop, which has to
// keep servicing the connection while the user types.
class TerminalInput : public QObject
{
    Q_OBJECT

public:
    // Called with the line that was entered, or with nothing when the input
    // has ended (Ctrl+D, or a closed pipe).
    using Answer = std::function<void(std::optional<QString>)>;

    // Reads from inputFd and writes prompts to promptFd. Prompts go to the
    // error stream by default, so standard output can be piped to a file.
    explicit TerminalInput(int inputFd = 0, int promptFd = 2, QObject *parent = nullptr);
    ~TerminalInput() override;

    // Questions are asked one at a time, in the order they were made. A
    // question asked from inside an answer is a follow-up to it, and comes
    // before any others that are already waiting.
    void ask(const QString &prompt, bool echo, Answer answer);
    void say(const QString &text);

private:
    struct Request
    {
        QString prompt;
        bool echo;
        Answer answer;
    };

    void startNext();
    void handleReadable();
    bool deliverBufferedLine();
    void finishCurrent(std::optional<QString> line);
    // False when what is typed cannot be hidden.
    bool hideEcho();
    void showEcho();

    int inputFd;
    int promptFd;
    QSocketNotifier *notifier = nullptr;
    QQueue<Request> pending;
    bool asking = false;
    // Where the next follow-up goes while an answer is being delivered, and
    // -1 otherwise.
    qsizetype followUpPosition = -1;
    bool inputEnded = false;
    QByteArray buffer;
};

#endif // TERMINALINPUT_H
