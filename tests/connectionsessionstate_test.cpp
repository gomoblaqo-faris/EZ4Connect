#include <QCoreApplication>
#include <QDebug>

#include "core/connectionsessionstate.h"

namespace
{
bool normalLifecycle()
{
    ConnectionSessionState session;
    if (!session.requestStart({false, 1000})
        || session.state() != ConnectionState::Starting
        || !session.isActive())
    {
        qCritical() << "normalLifecycle failed at start";
        return false;
    }

    session.connectionEstablished();
    if (session.state() != ConnectionState::Running)
    {
        qCritical() << "normalLifecycle failed at running";
        return false;
    }

    if (!session.requestStop() || session.state() != ConnectionState::Stopping)
    {
        qCritical() << "normalLifecycle failed at stopping";
        return false;
    }

    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Disconnected
        || session.isActive())
    {
        qCritical() << "normalLifecycle failed at completion";
        return false;
    }
    return true;
}

bool reconnectsOnlyEligibleFailures()
{
    ConnectionSessionState session;
    session.requestStart({true, 2500});
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::AUTH_EXPIRED);

    if (session.processFinished() != ProcessFinishAction::Reconnect
        || session.state() != ConnectionState::Reconnecting
        || session.reconnectDelayMs() != 2500)
    {
        qCritical() << "reconnectsOnlyEligibleFailures failed at scheduling";
        return false;
    }

    session.beginReconnect();
    if (session.state() != ConnectionState::Starting || session.error() != ZJU_ERROR::NONE)
    {
        qCritical() << "reconnectsOnlyEligibleFailures failed at restart";
        return false;
    }

    session.recordError(ZJU_ERROR::INVALID_DETAIL);
    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Failed)
    {
        qCritical() << "reconnectsOnlyEligibleFailures reconnected an ineligible error";
        return false;
    }
    return true;
}

bool keepsFirstErrorAndCancelsPendingReconnect()
{
    ConnectionSessionState session;
    session.requestStart({true, 1000});
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::AUTH_EXPIRED);
    session.recordError(ZJU_ERROR::OTHER);
    if (session.error() != ZJU_ERROR::AUTH_EXPIRED)
    {
        qCritical() << "keepsFirstErrorAndCancelsPendingReconnect did not keep first error";
        return false;
    }

    session.processFinished();
    if (session.requestStop()
        || session.state() != ConnectionState::Disconnected
        || session.wantsConnection())
    {
        qCritical() << "keepsFirstErrorAndCancelsPendingReconnect failed to cancel reconnect";
        return false;
    }
    return true;
}

bool establishedConnectionEndsAsInterrupted()
{
    ConnectionSessionState session;
    session.requestStart({false, 1000});
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::OTHER);

    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Interrupted)
    {
        qCritical() << "establishedConnectionEndsAsInterrupted failed";
        return false;
    }
    return true;
}

bool reconnectsWhenEstablishedConnectionDropsSilently()
{
    ConnectionSessionState session;
    session.requestStart({true, 1000});
    session.connectionEstablished();

    if (session.processFinished() != ProcessFinishAction::Reconnect
        || session.state() != ConnectionState::Reconnecting)
    {
        qCritical() << "reconnectsWhenEstablishedConnectionDropsSilently failed at scheduling";
        return false;
    }

    // A core that exits again before the connection is back must not loop.
    session.beginReconnect();
    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Disconnected)
    {
        qCritical() << "reconnectsWhenEstablishedConnectionDropsSilently retried a startup exit";
        return false;
    }
    return true;
}

bool silentDropWithoutReconnectPolicyEndsAsInterrupted()
{
    ConnectionSessionState session;
    session.requestStart({false, 1000});
    session.connectionEstablished();

    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Interrupted
        || session.error() != ZJU_ERROR::NONE)
    {
        qCritical() << "silentDropWithoutReconnectPolicyEndsAsInterrupted failed";
        return false;
    }
    return true;
}

bool requestedStopIsNeverReconnected()
{
    ConnectionSessionState session;
    session.requestStart({true, 1000});
    session.connectionEstablished();
    session.requestStop();

    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Disconnected)
    {
        qCritical() << "requestedStopIsNeverReconnected failed";
        return false;
    }
    return true;
}

bool requestedStopDiscardsRecordedErrors()
{
    ConnectionSessionState session;
    session.requestStart({false, 1000});
    session.recordError(ZJU_ERROR::INVALID_DETAIL);
    session.requestStop();
    // Cancelling a prompt makes the core complain while it shuts down.
    session.recordError(ZJU_ERROR::INTERACTIVE_ERROR);

    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Disconnected
        || session.error() != ZJU_ERROR::NONE)
    {
        qCritical() << "requestedStopDiscardsRecordedErrors failed";
        return false;
    }
    return true;
}

bool establishedConnectionDiscardsStartupErrors()
{
    ConnectionSessionState session;
    session.requestStart({false, 1000});
    session.recordError(ZJU_ERROR::CAPTCHA_FAILED);
    session.connectionEstablished();

    if (session.error() != ZJU_ERROR::NONE)
    {
        qCritical() << "establishedConnectionDiscardsStartupErrors kept a stale error";
        return false;
    }
    return true;
}

bool cancellingPendingReconnectDiscardsError()
{
    ConnectionSessionState session;
    session.requestStart({true, 1000});
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::AUTH_EXPIRED);
    session.processFinished();
    session.requestStop();

    if (session.state() != ConnectionState::Disconnected
        || session.error() != ZJU_ERROR::NONE)
    {
        qCritical() << "cancellingPendingReconnectDiscardsError failed";
        return false;
    }
    return true;
}

bool backsOffAndGivesUpAfterRepeatedReconnects()
{
    ReconnectPolicy policy{true, 1000};
    policy.maxAttempts = 3;
    policy.maxDelayMs = 3000;

    ConnectionSessionState session;
    session.requestStart(policy);
    session.connectionEstablished();

    for (const int expectedDelayMs : {1000, 2000, 3000})
    {
        session.recordError(ZJU_ERROR::OTHER);
        if (session.processFinished() != ProcessFinishAction::Reconnect
            || session.reconnectDelayMs() != expectedDelayMs)
        {
            qCritical() << "backsOffAndGivesUpAfterRepeatedReconnects expected delay"
                        << expectedDelayMs << "got" << session.reconnectDelayMs();
            return false;
        }
        session.beginReconnect();
    }

    session.recordError(ZJU_ERROR::OTHER);
    if (session.processFinished() != ProcessFinishAction::Complete
        || session.state() != ConnectionState::Failed
        || session.error() != ZJU_ERROR::OTHER)
    {
        qCritical() << "backsOffAndGivesUpAfterRepeatedReconnects kept retrying";
        return false;
    }
    return true;
}

bool establishedConnectionResetsReconnectAttempts()
{
    ReconnectPolicy policy{true, 1000};
    policy.maxAttempts = 1;

    ConnectionSessionState session;
    session.requestStart(policy);
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::AUTH_EXPIRED);
    session.processFinished();
    session.beginReconnect();
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::AUTH_EXPIRED);

    if (session.processFinished() != ProcessFinishAction::Reconnect
        || session.reconnectDelayMs() != 1000)
    {
        qCritical() << "establishedConnectionResetsReconnectAttempts failed";
        return false;
    }
    return true;
}

bool reconnectDelayNeverDropsBelowTheConfiguredDelay()
{
    ReconnectPolicy policy{true, 120000};
    policy.maxDelayMs = 60000;

    ConnectionSessionState session;
    session.requestStart(policy);
    session.connectionEstablished();
    session.recordError(ZJU_ERROR::OTHER);
    session.processFinished();
    session.beginReconnect();
    session.recordError(ZJU_ERROR::OTHER);

    if (session.processFinished() != ProcessFinishAction::Reconnect
        || session.reconnectDelayMs() != 120000)
    {
        qCritical() << "reconnectDelayNeverDropsBelowTheConfiguredDelay failed";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return normalLifecycle()
        && reconnectsOnlyEligibleFailures()
        && keepsFirstErrorAndCancelsPendingReconnect()
        && establishedConnectionEndsAsInterrupted()
        && reconnectsWhenEstablishedConnectionDropsSilently()
        && silentDropWithoutReconnectPolicyEndsAsInterrupted()
        && requestedStopIsNeverReconnected()
        && requestedStopDiscardsRecordedErrors()
        && establishedConnectionDiscardsStartupErrors()
        && cancellingPendingReconnectDiscardsError()
        && backsOffAndGivesUpAfterRepeatedReconnects()
        && establishedConnectionResetsReconnectAttempts()
        && reconnectDelayNeverDropsBelowTheConfiguredDelay()
        ? 0
        : 1;
}
