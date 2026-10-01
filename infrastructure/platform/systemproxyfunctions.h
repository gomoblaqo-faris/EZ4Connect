#ifndef SYSTEMPROXYFUNCTIONS_H
#define SYSTEMPROXYFUNCTIONS_H

#include <QString>

#include "application/operationstatus.h"

namespace PlatformSystemProxy
{
bool isSet(int httpPort = -1, int socksPort = -1);
OperationStatus set(int httpPort, int socksPort, const QString &bypass);
OperationStatus clear();
}

#endif // SYSTEMPROXYFUNCTIONS_H
