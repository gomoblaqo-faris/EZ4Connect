#ifndef PLATFORMSYSTEMPROXYBACKEND_H
#define PLATFORMSYSTEMPROXYBACKEND_H

#include "application/systemproxybackend.h"

class PlatformSystemProxyBackend : public SystemProxyBackend
{
public:
    bool hasConflict(const SystemProxyConfig &config) override;
    OperationStatus apply(const SystemProxyConfig &config) override;
    OperationStatus clear() override;
};

#endif // PLATFORMSYSTEMPROXYBACKEND_H
