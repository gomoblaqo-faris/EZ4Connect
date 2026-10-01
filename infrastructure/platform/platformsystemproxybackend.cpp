#include "platformsystemproxybackend.h"

#include "infrastructure/platform/systemproxyfunctions.h"

bool PlatformSystemProxyBackend::hasConflict(const SystemProxyConfig &config)
{
    return PlatformSystemProxy::isSet(config.httpPort, config.socksPort);
}

OperationStatus PlatformSystemProxyBackend::apply(const SystemProxyConfig &config)
{
    return PlatformSystemProxy::set(config.httpPort, config.socksPort, config.bypass);
}

OperationStatus PlatformSystemProxyBackend::clear()
{
    return PlatformSystemProxy::clear();
}
