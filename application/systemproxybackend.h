#ifndef SYSTEMPROXYBACKEND_H
#define SYSTEMPROXYBACKEND_H

#include <QString>

#include "application/operationstatus.h"

struct SystemProxyConfig
{
    int httpPort = 0;
    int socksPort = 0;
    QString bypass;
};

class SystemProxyBackend
{
public:
    virtual ~SystemProxyBackend() = default;

    virtual bool hasConflict(const SystemProxyConfig &config) = 0;
    virtual OperationStatus apply(const SystemProxyConfig &config) = 0;
    virtual OperationStatus clear() = 0;
};

#endif // SYSTEMPROXYBACKEND_H
