#ifndef UNIXSIGNALS_H
#define UNIXSIGNALS_H

#include <QObject>

class QSocketNotifier;

// Turns SIGINT, SIGTERM and SIGHUP into a Qt signal, so that Ctrl+C can shut
// the connection down properly instead of killing the process on the spot.
class UnixSignals : public QObject
{
    Q_OBJECT

public:
    explicit UnixSignals(QObject *parent = nullptr);

    // False when the handlers could not be set up. An interrupt would then
    // kill the process on the spot, leaving the core and the system proxy
    // behind, so a connection should not be started.
    bool isInstalled() const;

signals:
    void terminationRequested();

private:
    QSocketNotifier *notifier = nullptr;
    bool installed = false;
};

#endif // UNIXSIGNALS_H
