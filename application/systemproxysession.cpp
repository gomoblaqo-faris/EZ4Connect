#include "systemproxysession.h"

#include <QDebug>
#include <QtConcurrent>

SystemProxySession::SystemProxySession(
    std::unique_ptr<SystemProxyBackend> backend,
    QObject *parent
)
    : QObject(parent),
      backend(std::move(backend))
{
    connect(&operationWatcher, &QFutureWatcher<OperationResult>::finished,
            this, &SystemProxySession::handleOperationFinished);
}

SystemProxySession::~SystemProxySession()
{
    operationWatcher.waitForFinished();
}

bool SystemProxySession::checkConflict(const SystemProxyConfig &config)
{
    return startOperation(Operation::CheckConflict, config);
}

bool SystemProxySession::enable(const SystemProxyConfig &config)
{
    return startOperation(Operation::Enable, config);
}

bool SystemProxySession::disable()
{
    return startOperation(Operation::Disable);
}

bool SystemProxySession::isEnabled() const
{
    return enabled;
}

bool SystemProxySession::isBusy() const
{
    return currentOperation != Operation::None;
}

void SystemProxySession::clearBeforeShutdown()
{
    const Operation operation = currentOperation;
    disconnect(&operationWatcher, nullptr, this, nullptr);
    operationWatcher.waitForFinished();

    if (operation != Operation::Disable
        && (enabled || operation == Operation::Enable))
    {
        const OperationStatus status = backend->clear();
        if (!status.succeeded)
        {
            qWarning().noquote() << status.error;
        }
    }

    currentOperation = Operation::None;
    enabled = false;
}

bool SystemProxySession::startOperation(Operation operation, const SystemProxyConfig &config)
{
    if (isBusy())
    {
        return false;
    }

    currentOperation = operation;
    emit busyChanged(true);

    SystemProxyBackend *proxyBackend = backend.get();
    operationWatcher.setFuture(QtConcurrent::run(
        [proxyBackend, operation, config]()
        {
            OperationResult result{operation};
            switch (operation)
            {
            case Operation::CheckConflict:
                result.conflict = proxyBackend->hasConflict(config);
                break;
            case Operation::Enable:
            {
                const OperationStatus status = proxyBackend->apply(config);
                result.succeeded = status.succeeded;
                result.error = status.error;
                if (!status.succeeded)
                {
                    // Applying touches several settings in turn, so undo
                    // whatever part of it went through.
                    const OperationStatus rollback = proxyBackend->clear();
                    if (!rollback.succeeded)
                    {
                        result.leftPartiallyApplied = true;
                        result.error += "\n" + rollback.error;
                    }
                }
                break;
            }
            case Operation::Disable:
            {
                const OperationStatus status = proxyBackend->clear();
                result.succeeded = status.succeeded;
                result.error = status.error;
                break;
            }
            case Operation::None:
                break;
            }
            return result;
        }
    ));
    return true;
}

void SystemProxySession::handleOperationFinished()
{
    const OperationResult result = operationWatcher.result();
    currentOperation = Operation::None;

    bool stateChanged = false;
    // A proxy that is partly applied still has to be cleared, so it counts
    // as enabled: the user is offered "clear" and shutdown retries it.
    if ((result.succeeded || result.leftPartiallyApplied)
        && result.operation == Operation::Enable
        && !enabled)
    {
        enabled = true;
        stateChanged = true;
    }
    else if (result.succeeded && result.operation == Operation::Disable && enabled)
    {
        enabled = false;
        stateChanged = true;
    }

    emit busyChanged(false);

    if (stateChanged)
    {
        emit enabledChanged(enabled);
    }

    if (result.operation == Operation::CheckConflict)
    {
        emit conflictCheckFinished(result.conflict);
    }
    else
    {
        emit operationFinished(enabled);
        if (!result.succeeded)
        {
            emit operationFailed(result.error);
        }
    }
}
