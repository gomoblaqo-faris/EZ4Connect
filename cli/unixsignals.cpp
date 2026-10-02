#include "unixsignals.h"

#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// A signal handler may do very little safely. It writes one byte here, and
// the event loop picks it up from the other end.
int signalSockets[2] = {-1, -1};

void handleSignal(int)
{
    // The socket does not block: if it is full, a notification is already
    // on its way and this one adds nothing.
    const int savedErrno = errno;
    const char byte = 1;
    const ssize_t written = ::write(signalSockets[0], &byte, sizeof(byte));
    (void)written;
    errno = savedErrno;
}

bool makeNonBlockingAndPrivate(int descriptor)
{
    const int statusFlags = ::fcntl(descriptor, F_GETFL);
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD);
    return statusFlags >= 0
        && descriptorFlags >= 0
        && ::fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) == 0
        // Not for the core, or anything else that gets started.
        && ::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}
}

UnixSignals::UnixSignals(QObject *parent)
    : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalSockets) != 0
        || !makeNonBlockingAndPrivate(signalSockets[0])
        || !makeNonBlockingAndPrivate(signalSockets[1]))
    {
        return;
    }

    notifier = new QSocketNotifier(signalSockets[1], QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this]()
    {
        // Several signals may have arrived since the last look.
        char bytes[16];
        while (::read(signalSockets[1], bytes, sizeof(bytes)) > 0)
        {
        }
        emit terminationRequested();
    });

    struct sigaction action {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    installed = true;
    for (const int signalNumber : {SIGINT, SIGTERM, SIGHUP})
    {
        if (::sigaction(signalNumber, &action, nullptr) != 0)
        {
            installed = false;
        }
    }
}

bool UnixSignals::isInstalled() const
{
    return installed;
}
