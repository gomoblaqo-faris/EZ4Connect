#ifndef DEVICETRUST_H
#define DEVICETRUST_H

#include <QString>

class QObject;

namespace DeviceTrust
{
void set(
    QObject *parent,
    const QString &protocol,
    const QString &server,
    int port,
    const QString &profileId,
    bool trusted
);

// What set() threw, in the interface language. Its exceptions are worded
// in English, for the log and the command-line client.
QString describeFailure(const char *message);
}

#endif // DEVICETRUST_H
