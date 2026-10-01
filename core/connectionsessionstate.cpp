#include "connectionsessionstate.h"

#include <algorithm>

ConnectionState ConnectionSessionState::state() const
{
    return currentState;
}

ZJU_ERROR ConnectionSessionState::error() const
{
    return currentError;
}

bool ConnectionSessionState::isActive() const
{
    return currentState == ConnectionState::Starting
        || currentState == ConnectionState::Running
        || currentState == ConnectionState::Stopping
        || currentState == ConnectionState::Reconnecting;
}

bool ConnectionSessionState::wantsConnection() const
{
    return desiredConnected;
}

int ConnectionSessionState::reconnectDelayMs() const
{
    // The configured delay is a floor, so a limit below it never shortens it.
    const int limit = std::max(reconnectPolicy.delayMs, reconnectPolicy.maxDelayMs);
    int delay = reconnectPolicy.delayMs;
    for (int attempt = 1; attempt < reconnectAttempts && delay < limit; ++attempt)
    {
        delay = std::min(delay * 2, limit);
    }
    return delay;
}

bool ConnectionSessionState::requestStart(const ReconnectPolicy &policy)
{
    if (isActive())
    {
        return false;
    }

    reconnectPolicy = policy;
    reconnectAttempts = 0;
    desiredConnected = true;
    currentError = ZJU_ERROR::NONE;
    currentState = ConnectionState::Starting;
    return true;
}

void ConnectionSessionState::connectionEstablished()
{
    if (currentState == ConnectionState::Starting)
    {
        // Errors logged on the way to a working connection, such as a failed
        // first captcha attempt, no longer describe this session.
        currentError = ZJU_ERROR::NONE;
        reconnectAttempts = 0;
        currentState = ConnectionState::Running;
    }
}

void ConnectionSessionState::recordError(ZJU_ERROR error)
{
    // Whatever the core reports while shutting down on request is not a
    // failure the user needs to hear about.
    if (currentState == ConnectionState::Stopping)
    {
        return;
    }
    if (currentError == ZJU_ERROR::NONE)
    {
        currentError = error;
    }
}

bool ConnectionSessionState::requestStop()
{
    desiredConnected = false;
    if (currentState == ConnectionState::Reconnecting)
    {
        currentError = ZJU_ERROR::NONE;
        currentState = ConnectionState::Disconnected;
        return false;
    }
    if (currentState == ConnectionState::Starting || currentState == ConnectionState::Running)
    {
        currentError = ZJU_ERROR::NONE;
        currentState = ConnectionState::Stopping;
        return true;
    }
    return currentState == ConnectionState::Stopping;
}

ProcessFinishAction ConnectionSessionState::processFinished()
{
    const bool connectionWasEstablished = currentState == ConnectionState::Running;
    // An established connection that ends without a recognised error is an
    // unexpected drop. Startup exits are excluded so a core that never
    // connects cannot be restarted forever.
    const bool droppedSilently = connectionWasEstablished
        && currentError == ZJU_ERROR::NONE;
    if (desiredConnected
        && reconnectPolicy.enabled
        && reconnectAttempts < reconnectPolicy.maxAttempts
        && (droppedSilently || isReconnectable(currentError)))
    {
        ++reconnectAttempts;
        currentState = ConnectionState::Reconnecting;
        return ProcessFinishAction::Reconnect;
    }

    desiredConnected = false;
    if (connectionWasEstablished)
    {
        currentState = ConnectionState::Interrupted;
    }
    else
    {
        currentState = currentError == ZJU_ERROR::NONE
            ? ConnectionState::Disconnected
            : ConnectionState::Failed;
    }
    return ProcessFinishAction::Complete;
}

void ConnectionSessionState::beginReconnect()
{
    if (currentState == ConnectionState::Reconnecting && desiredConnected)
    {
        currentError = ZJU_ERROR::NONE;
        currentState = ConnectionState::Starting;
    }
}

bool ConnectionSessionState::isReconnectable(ZJU_ERROR error)
{
    return error == ZJU_ERROR::AUTH_EXPIRED || error == ZJU_ERROR::OTHER;
}
