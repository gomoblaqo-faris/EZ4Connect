#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>

#include "application/systemproxysession.h"

namespace
{
class FakeSystemProxyBackend : public SystemProxyBackend
{
public:
    bool conflict = false;
    int conflictChecks = 0;
    int applyCalls = 0;
    int clearCalls = 0;
    bool applySucceeds = true;
    bool clearSucceeds = true;
    SystemProxyConfig lastConfig;
    QSemaphore operationStarted;
    QSemaphore allowOperationToFinish;

    bool hasConflict(const SystemProxyConfig &config) override
    {
        ++conflictChecks;
        lastConfig = config;
        return conflict;
    }

    OperationStatus apply(const SystemProxyConfig &config) override
    {
        ++applyCalls;
        lastConfig = config;
        operationStarted.release();
        allowOperationToFinish.acquire();
        return applySucceeds ? OperationStatus() : OperationStatus::failure("apply failed");
    }

    OperationStatus clear() override
    {
        ++clearCalls;
        return clearSucceeds ? OperationStatus() : OperationStatus::failure("clear failed");
    }
};

bool waitUntil(const std::function<bool()> &condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 1000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return condition();
}

bool delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState()
{
    auto backend = std::make_unique<FakeSystemProxyBackend>();
    FakeSystemProxyBackend *fake = backend.get();
    fake->conflict = true;
    SystemProxySession session(std::move(backend));
    const SystemProxyConfig config{1081, 1080, "localhost"};

    bool conflictResult = false;
    QObject::connect(&session, &SystemProxySession::conflictCheckFinished,
                     [&](bool conflict) { conflictResult = conflict; });
    if (!session.checkConflict(config)
        || !waitUntil([&]() { return !session.isBusy(); })
        || !conflictResult
        || fake->conflictChecks != 1
        || session.isEnabled())
    {
        qCritical() << "delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState failed at conflict check";
        return false;
    }

    if (!session.enable(config)
        || !fake->operationStarted.tryAcquire(1, 1000)
        || !session.isBusy()
        || session.isEnabled()
        || session.disable())
    {
        qCritical() << "delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState failed while enabling";
        return false;
    }

    fake->allowOperationToFinish.release();
    if (!waitUntil([&]() { return !session.isBusy(); })
        || !session.isEnabled()
        || fake->applyCalls != 1
        || fake->lastConfig.httpPort != 1081
        || fake->lastConfig.socksPort != 1080
        || fake->lastConfig.bypass != "localhost")
    {
        qCritical() << "delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState failed after enabling";
        return false;
    }

    if (!session.disable()
        || !waitUntil([&]() { return !session.isBusy(); })
        || session.isEnabled()
        || fake->clearCalls != 1)
    {
        qCritical() << "delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState failed at disable";
        return false;
    }

    if (!session.disable()
        || !waitUntil([&]() { return !session.isBusy(); })
        || fake->clearCalls != 2)
    {
        qCritical() << "disable must also support clearing externally-owned proxy state";
        return false;
    }

    QStringList failures;
    QObject::connect(&session, &SystemProxySession::operationFailed,
                     [&](const QString &error) { failures << error; });

    fake->applySucceeds = false;
    if (!session.enable(config)
        || !fake->operationStarted.tryAcquire(1, 1000))
    {
        qCritical() << "failed enable did not start";
        return false;
    }
    fake->allowOperationToFinish.release();
    if (!waitUntil([&]() { return !session.isBusy(); }) || session.isEnabled())
    {
        qCritical() << "failed enable must not change owned state";
        return false;
    }
    if (failures != QStringList{"apply failed"} || fake->clearCalls != 3)
    {
        qCritical() << "failed enable must be reported and rolled back:" << failures;
        return false;
    }
    fake->applySucceeds = true;

    if (!session.enable(config)
        || !fake->operationStarted.tryAcquire(1, 1000))
    {
        qCritical() << "clearBeforeShutdown failed to start enable";
        return false;
    }
    fake->allowOperationToFinish.release();
    session.clearBeforeShutdown();
    if (session.isBusy()
        || session.isEnabled()
        || fake->applyCalls != 3
        || fake->clearCalls != 4)
    {
        qCritical() << "clearBeforeShutdown must clear an in-flight enable";
        return false;
    }
    return true;
}

bool failedClearKeepsTheProxyMarkedAsEnabled()
{
    auto backend = std::make_unique<FakeSystemProxyBackend>();
    FakeSystemProxyBackend *fake = backend.get();
    SystemProxySession session(std::move(backend));
    QStringList failures;
    QObject::connect(&session, &SystemProxySession::operationFailed,
                     [&](const QString &error) { failures << error; });

    fake->allowOperationToFinish.release();
    if (!session.enable({1081, 1080, QString()})
        || !waitUntil([&]() { return !session.isBusy(); })
        || !session.isEnabled())
    {
        qCritical() << "failedClearKeepsTheProxyMarkedAsEnabled could not enable";
        return false;
    }

    // The user must still be offered "clear" after a failed attempt.
    fake->clearSucceeds = false;
    if (!session.disable()
        || !waitUntil([&]() { return !session.isBusy(); })
        || !session.isEnabled()
        || failures != QStringList{"clear failed"})
    {
        qCritical() << "a failed clear was treated as success:" << failures;
        return false;
    }
    return true;
}

bool failedRollbackLeavesTheProxyToBeCleared()
{
    auto backend = std::make_unique<FakeSystemProxyBackend>();
    FakeSystemProxyBackend *fake = backend.get();
    SystemProxySession session(std::move(backend));
    QStringList failures;
    QObject::connect(&session, &SystemProxySession::operationFailed,
                     [&](const QString &error) { failures << error; });

    fake->applySucceeds = false;
    fake->clearSucceeds = false;
    fake->allowOperationToFinish.release();
    if (!session.enable({1081, 1080, QString()})
        || !waitUntil([&]() { return !session.isBusy(); })
        || !session.isEnabled()
        || failures != QStringList{"apply failed\nclear failed"})
    {
        qCritical() << "a partly applied proxy was forgotten:" << failures;
        return false;
    }

    // Shutdown must try again rather than assume nothing was applied.
    fake->clearSucceeds = true;
    session.clearBeforeShutdown();
    if (fake->clearCalls != 2 || session.isEnabled())
    {
        qCritical() << "shutdown did not retry clearing a partly applied proxy";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return delegatesPlatformOperationsAsynchronouslyAndTracksOwnedState()
        && failedClearKeepsTheProxyMarkedAsEnabled()
        && failedRollbackLeavesTheProxyToBeCleared() ? 0 : 1;
}
