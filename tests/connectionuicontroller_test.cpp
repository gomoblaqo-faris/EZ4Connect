#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPushButton>
#include <QSemaphore>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

#include "application/applicationlogger.h"
#include "application/connectionsession.h"
#include "application/systemproxysession.h"
#include "presentation/coordinators/authdialogcoordinator.h"
#include "presentation/coordinators/connectionuicontroller.h"

namespace
{
class FakeCoreProcess : public CoreProcess
{
public:
    int startCalls = 0;
    int stopCalls = 0;

    void start(const ConnectionProfile &) override
    {
        ++startCalls;
        emit started();
    }

    void stop() override
    {
        ++stopCalls;
    }

    void writeInput(const QByteArray &) override
    {
    }

    void establishConnection()
    {
        emit connectionEstablished();
    }

    void reportError(ZJU_ERROR code)
    {
        emit error(code);
    }

    void complete()
    {
        emit finished();
    }
};

struct ProxyCalls
{
    std::atomic<int> conflictChecks{0};
    std::atomic<int> applies{0};
    std::atomic<int> clears{0};
    std::atomic<int> lastHttpPort{0};
    std::atomic<bool> failApply{false};
    // When set, clear() waits here so a test can hold the session busy.
    std::atomic<bool> holdClear{false};
    QSemaphore clearStarted;
    QSemaphore releaseClear;
};

class FakeProxyBackend : public SystemProxyBackend
{
public:
    explicit FakeProxyBackend(ProxyCalls *calls)
        : calls(calls)
    {
    }

    bool hasConflict(const SystemProxyConfig &) override
    {
        ++calls->conflictChecks;
        return false;
    }

    OperationStatus apply(const SystemProxyConfig &config) override
    {
        calls->lastHttpPort = config.httpPort;
        ++calls->applies;
        return calls->failApply
            ? OperationStatus::failure("apply failed")
            : OperationStatus();
    }

    OperationStatus clear() override
    {
        ++calls->clears;
        if (calls->holdClear)
        {
            calls->clearStarted.release();
            calls->releaseClear.acquire();
        }
        return {};
    }

private:
    ProxyCalls *calls;
};

// Everything the controller needs, wired the way MainWindow wires it.
struct Fixture
{
    explicit Fixture(bool autoSetProxy, bool autoReconnect = false)
        : settings(directory.filePath("profile.ini"), QSettings::IniFormat),
          coreProcess(new FakeCoreProcess()),
          session(coreProcess),
          proxySession(std::make_unique<FakeProxyBackend>(&proxyCalls)),
          authDialogs(&window, &settings),
          controller(
              &window,
              &connectButton,
              &proxyButton,
              &trayAction,
              &session,
              &proxySession,
              &authDialogs,
              &logger,
              [this]() { return &settings; },
              []() { return QString(); },
              [this](const QString &, const QString &content, QSystemTrayIcon::MessageIcon)
              {
                  notifications << content;
              }
          )
    {
        // SSO authentication needs no stored credentials, so connecting goes
        // straight to the core without a login dialog.
        settings.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");
        settings.setValue("ZJUConnect/ServerPort", 443);
        settings.setValue("ZJUConnect/Protocol", "atrust");
        settings.setValue("ZJUConnect/AuthType", "cas");
        settings.setValue("ZJUConnect/HTTPPort", 11081);
        settings.setValue("ZJUConnect/SOCKS5Port", 11080);
        settings.setValue("Common/AutoSetProxy", autoSetProxy);
        settings.setValue("Common/AutoReconnect", autoReconnect);
        settings.setValue("Common/ReconnectTime", 1);
    }

    ~Fixture()
    {
        // Never leave the proxy worker blocked, or tearing the session down
        // would wait on it forever.
        proxyCalls.holdClear = false;
        proxyCalls.releaseClear.release();
    }

    QTemporaryDir directory;
    QSettings settings;
    ApplicationLogger logger;
    QWidget window;
    QPushButton connectButton;
    QPushButton proxyButton;
    QAction trayAction;
    ProxyCalls proxyCalls;
    FakeCoreProcess *coreProcess;
    ConnectionSession session;
    SystemProxySession proxySession;
    AuthDialogCoordinator authDialogs;
    QStringList notifications;
    ConnectionUiController controller;
};

// An error dialog would block a test forever, so the guard in main() closes
// every dialog and records its message here. Titles are not used because
// macOS does not show them on message boxes.
QStringList dialogMessages;

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition())
    {
        if (timer.elapsed() > timeoutMs)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

void processEventsFor(int durationMs)
{
    waitFor([]() { return false; }, durationMs);
}

bool appliesAutomaticProxyOnlyOnceConnected()
{
    Fixture fixture(true);
    fixture.connectButton.click();
    if (fixture.coreProcess->startCalls != 1
        || fixture.session.state() != ConnectionState::Starting)
    {
        qCritical() << "clicking connect did not start the core";
        return false;
    }

    processEventsFor(200);
    if (fixture.proxyCalls.conflictChecks != 0 || fixture.proxyCalls.applies != 0)
    {
        qCritical() << "the system proxy was touched before the connection was established";
        return false;
    }

    fixture.coreProcess->establishConnection();
    if (!waitFor([&]() { return fixture.proxyCalls.applies == 1; }, 5000)
        || !waitFor([&]() { return fixture.proxySession.isEnabled(); }, 5000))
    {
        qCritical() << "the system proxy was not applied after the connection was established";
        return false;
    }

    // A second "connected" line from the core must not toggle the proxy off.
    fixture.coreProcess->establishConnection();
    processEventsFor(200);
    if (fixture.proxyCalls.applies != 1 || !fixture.proxySession.isEnabled())
    {
        qCritical() << "the system proxy changed after it was already applied";
        return false;
    }
    return true;
}

bool proxyUsesThePortsTheCoreWasStartedWith()
{
    Fixture fixture(true);
    fixture.connectButton.click();
    // The settings window stays usable while the core is starting.
    fixture.settings.setValue("ZJUConnect/HTTPPort", 9999);
    fixture.coreProcess->establishConnection();

    if (!waitFor([&]() { return fixture.proxyCalls.applies == 1; }, 5000)
        || fixture.proxyCalls.lastHttpPort != 11081)
    {
        qCritical() << "the system proxy was pointed at a port the running core does not use:"
                    << fixture.proxyCalls.lastHttpPort.load();
        return false;
    }
    return true;
}

bool leavesProxyAloneWhenAutomaticProxyIsOff()
{
    Fixture fixture(false);
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    processEventsFor(200);

    if (fixture.proxyCalls.conflictChecks != 0 || fixture.proxyCalls.applies != 0)
    {
        qCritical() << "the system proxy was set although automatic proxy is off";
        return false;
    }
    return true;
}

bool suspendsProxyWhileReconnecting()
{
    Fixture fixture(true, true);
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    if (!waitFor([&]() { return fixture.proxySession.isEnabled(); }, 5000))
    {
        qCritical() << "the system proxy was not applied before the reconnect test";
        return false;
    }

    fixture.coreProcess->complete();
    if (fixture.session.state() != ConnectionState::Reconnecting
        || !waitFor([&]() { return !fixture.proxySession.isEnabled(); }, 5000))
    {
        qCritical() << "the system proxy stayed on while the core was down";
        return false;
    }

    if (!waitFor([&]() { return fixture.coreProcess->startCalls == 2; }, 5000))
    {
        qCritical() << "the core was not restarted";
        return false;
    }
    fixture.coreProcess->establishConnection();
    if (!waitFor([&]() { return fixture.proxySession.isEnabled(); }, 5000)
        || fixture.proxyCalls.applies != 2)
    {
        qCritical() << "the system proxy was not restored after reconnecting";
        return false;
    }
    return true;
}

bool keepsProxyOffAfterReconnectWhenUserClearedIt()
{
    Fixture fixture(true, true);
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    if (!waitFor([&]() { return fixture.proxySession.isEnabled(); }, 5000))
    {
        qCritical() << "the system proxy was not applied before the user-clear test";
        return false;
    }

    fixture.proxyButton.click();
    if (!waitFor([&]() { return !fixture.proxySession.isEnabled(); }, 5000))
    {
        qCritical() << "clicking the proxy button did not clear the proxy";
        return false;
    }

    fixture.coreProcess->complete();
    if (!waitFor([&]() { return fixture.coreProcess->startCalls == 2; }, 5000))
    {
        qCritical() << "the core was not restarted";
        return false;
    }
    fixture.coreProcess->establishConnection();
    processEventsFor(300);
    if (fixture.proxySession.isEnabled() || fixture.proxyCalls.applies != 1)
    {
        qCritical() << "a proxy the user cleared came back after reconnecting";
        return false;
    }
    return true;
}

bool appliesAutomaticProxyOnceABusySessionIsFree()
{
    Fixture fixture(true);
    // Stands in for the previous connection's proxy still being cleared.
    fixture.proxyCalls.holdClear = true;
    fixture.proxySession.disable();
    if (!fixture.proxyCalls.clearStarted.tryAcquire(1, 5000))
    {
        qCritical() << "the blocking clear did not start";
        return false;
    }

    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    processEventsFor(200);
    if (fixture.proxyCalls.applies != 0)
    {
        qCritical() << "the proxy was applied while the session was busy";
        return false;
    }

    fixture.proxyCalls.holdClear = false;
    fixture.proxyCalls.releaseClear.release();
    if (!waitFor([&]() { return fixture.proxySession.isEnabled(); }, 5000)
        || fixture.proxyCalls.applies != 1)
    {
        qCritical() << "the automatic proxy request was lost while the session was busy";
        return false;
    }
    return true;
}

bool reportsAProxyThatCouldNotBeSetAndDoesNotRetryIt()
{
    Fixture fixture(true, true);
    fixture.proxyCalls.failApply = true;
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    if (!waitFor([&]() { return dialogMessages.contains("apply failed"); }, 5000)
        || fixture.proxySession.isEnabled()
        || fixture.proxyCalls.clears != 1)
    {
        qCritical() << "a failed proxy was not reported and rolled back:" << dialogMessages;
        return false;
    }
    // Remove only the expected dialog, so any other one still fails the run.
    if (dialogMessages.removeAll("apply failed") != 1)
    {
        qCritical() << "the proxy failure was reported more than once";
        return false;
    }
    const qsizetype dialogsBeforeReconnect = dialogMessages.size();

    fixture.coreProcess->complete();
    if (!waitFor([&]() { return fixture.coreProcess->startCalls == 2; }, 5000))
    {
        qCritical() << "the core was not restarted";
        return false;
    }
    fixture.coreProcess->establishConnection();
    processEventsFor(300);
    if (fixture.proxyCalls.applies != 1
        || dialogMessages.size() != dialogsBeforeReconnect)
    {
        qCritical() << "a proxy that could not be set was retried after reconnecting";
        return false;
    }
    return true;
}

bool notifiesWhenEstablishedConnectionDropsSilently()
{
    Fixture fixture(false);
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    fixture.coreProcess->complete();

    if (fixture.notifications.size() != 1
        || fixture.connectButton.text() != "Connect")
    {
        qCritical() << "a silent drop did not produce exactly one notification:"
                    << fixture.notifications;
        return false;
    }
    return true;
}

bool requestedDisconnectIsNotReportedAsFailure()
{
    Fixture fixture(false);
    fixture.connectButton.click();
    fixture.coreProcess->establishConnection();
    fixture.coreProcess->reportError(ZJU_ERROR::CLIENT_FAILED);
    fixture.connectButton.click();
    if (fixture.coreProcess->stopCalls != 1)
    {
        qCritical() << "clicking disconnect did not stop the core";
        return false;
    }
    fixture.coreProcess->complete();

    if (!fixture.notifications.isEmpty()
        || fixture.session.state() != ConnectionState::Disconnected
        || fixture.connectButton.text() != "Connect")
    {
        qCritical() << "a requested disconnect was reported as a failure:"
                    << fixture.notifications;
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);

    QTimer dialogGuard;
    QObject::connect(&dialogGuard, &QTimer::timeout, [&]()
    {
        if (QWidget *dialog = QApplication::activeModalWidget())
        {
            const auto *messageBox = qobject_cast<QMessageBox *>(dialog);
            dialogMessages << (messageBox != nullptr
                ? messageBox->text()
                : dialog->windowTitle());
            dialog->close();
        }
    });
    dialogGuard.start(20);

    const bool passed = appliesAutomaticProxyOnlyOnceConnected()
        && proxyUsesThePortsTheCoreWasStartedWith()
        && leavesProxyAloneWhenAutomaticProxyIsOff()
        && suspendsProxyWhileReconnecting()
        && keepsProxyOffAfterReconnectWhenUserClearedIt()
        && appliesAutomaticProxyOnceABusySessionIsFree()
        && reportsAProxyThatCouldNotBeSetAndDoesNotRetryIt()
        && notifiesWhenEstablishedConnectionDropsSilently()
        && requestedDisconnectIsNotReportedAsFailure();
    if (!dialogMessages.isEmpty())
    {
        qCritical() << "unexpected dialogs:" << dialogMessages;
    }
    return passed && dialogMessages.isEmpty() ? 0 : 1;
}
