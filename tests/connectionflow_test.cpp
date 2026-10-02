// The connect flow without any user interface: a fake core, a fake proxy
// backend and a prompter that records what it is asked.
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>

#include <atomic>
#include <functional>
#include <memory>

#include "application/authprompter.h"
#include "application/connectionflow.h"
#include "application/connectionsession.h"
#include "application/profilesettings.h"
#include "application/systemproxysession.h"
#include "infrastructure/settings/settingsprofileloader.h"

namespace
{
class FakeCoreProcess : public CoreProcess
{
public:
    int startCalls = 0;
    int stopCalls = 0;
    QByteArray lastInput;
    ConnectionProfile lastProfile;

    void start(const ConnectionProfile &profile) override
    {
        ++startCalls;
        lastProfile = profile;
        emit started();
    }

    void stop() override
    {
        ++stopCalls;
    }

    void writeInput(const QByteArray &data) override
    {
        lastInput = data;
    }

    using CoreProcess::connectionEstablished;
    using CoreProcess::error;
    using CoreProcess::finished;
    using CoreProcess::graphCaptcha;
    using CoreProcess::randCode;
    using CoreProcess::smsCode;
    using CoreProcess::ssoAuth;
};

struct ProxyCalls
{
    std::atomic<int> applies{0};
    std::atomic<bool> conflict{false};
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
        return calls->conflict;
    }

    OperationStatus apply(const SystemProxyConfig &) override
    {
        ++calls->applies;
        return {};
    }

    OperationStatus clear() override
    {
        return {};
    }

private:
    ProxyCalls *calls;
};

class RecordingPrompter : public AuthPrompter
{
public:
    QStringList requests;
    ProxyOverwriteAnswer proxyOverwriteAnswer;

    void requestLogin(const QString &username, const QString &) override
    {
        requests << "login:" + username;
    }

    void requestPhoneNumber(const QString &countryCode, const QString &phoneNumber) override
    {
        requests << "phone:" + countryCode + "-" + phoneNumber;
    }

    void requestSudoPassword() override
    {
        requests << "sudo";
    }

    void requestGraphCaptcha(const QString &graphFile, bool textInput) override
    {
        requests << QString("captcha:%1:%2").arg(graphFile, textInput ? "text" : "points");
    }

    void requestSmsCode(bool showSkipSecondaryAuthOption) override
    {
        requests << QString("sms:%1").arg(showSkipSecondaryAuthOption ? "skip" : "plain");
    }

    void requestTotpCode() override
    {
        requests << "totp";
    }

    void requestRadiusCode(bool) override
    {
        requests << "radius";
    }

    void requestSsoLogin(const QUrl &serverUrl, const QUrl &loginUrl) override
    {
        requests << "sso:" + serverUrl.toString() + " " + loginUrl.toString();
    }

    ProxyOverwriteAnswer askProxyOverwrite() override
    {
        requests << "proxy-overwrite";
        return proxyOverwriteAnswer;
    }
};

struct Fixture
{
    Fixture()
        : settings(directory.filePath("profile.ini"), QSettings::IniFormat),
          coreProcess(new FakeCoreProcess()),
          session(coreProcess),
          proxySession(std::make_unique<FakeProxyBackend>(&proxyCalls)),
          flow(
              &session,
              &proxySession,
              &prompter,
              [this]() { return &settings; },
              []() { return QString("work"); },
              [](const QSettings &profileSettings,
                 const QString &profileId,
                 const QString &username,
                 const QString &password)
              {
                  return SettingsProfileLoader::load(profileSettings, profileId, username, password);
              }
          )
    {
        settings.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");
        settings.setValue("ZJUConnect/ServerPort", 8443);
        settings.setValue("ZJUConnect/Protocol", "atrust");
        settings.setValue("ZJUConnect/AuthType", "cas");
        settings.setValue("ZJUConnect/LoginDomain", "campus");

        QObject::connect(&flow, &ConnectionFlow::cannotConnect,
                         [this](ConnectionFlow::Obstacle obstacle) { obstacles << obstacle; });
        QObject::connect(&flow, &ConnectionFlow::connectionStarted, [this]() { ++starts; });
        QObject::connect(&flow, &ConnectionFlow::connectionEnded, [this]() { ++ends; });
        QObject::connect(&flow, &ConnectionFlow::droppedUnexpectedly, [this]() { ++drops; });
        QObject::connect(&flow, &ConnectionFlow::failed,
                         [this](const QString &message) { failures << message; });
    }

    QTemporaryDir directory;
    QSettings settings;
    ProxyCalls proxyCalls;
    FakeCoreProcess *coreProcess;
    ConnectionSession session;
    SystemProxySession proxySession;
    RecordingPrompter prompter;
    ConnectionFlow flow;
    QList<ConnectionFlow::Obstacle> obstacles;
    QStringList failures;
    int starts = 0;
    int ends = 0;
    int drops = 0;
};

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

bool refusesAProfileThatCannotConnect()
{
    Fixture missingServer;
    missingServer.settings.setValue("ZJUConnect/ServerAddress", "");
    missingServer.flow.connectToServer();

    Fixture missingCertificate;
    missingCertificate.settings.setValue("ZJUConnect/Protocol", "easyconnect");
    missingCertificate.settings.setValue("ZJUConnect/EasyConnectAuthType", "certificate");
    missingCertificate.flow.connectToServer();

    const bool passed =
        missingServer.obstacles == QList<ConnectionFlow::Obstacle>{ConnectionFlow::Obstacle::MissingServerAddress}
        && missingServer.coreProcess->startCalls == 0
        && missingCertificate.obstacles == QList<ConnectionFlow::Obstacle>{ConnectionFlow::Obstacle::MissingCertificate}
        && missingCertificate.coreProcess->startCalls == 0;
    if (!passed)
    {
        qCritical() << "refusesAProfileThatCannotConnect failed";
    }
    return passed;
}

bool asksForMissingCredentialsBeforeStarting()
{
    Fixture fixture;
    fixture.settings.setValue("ZJUConnect/AuthType", "psw");
    fixture.flow.connectToServer();
    if (fixture.prompter.requests != QStringList{"login:"} || fixture.coreProcess->startCalls != 0)
    {
        qCritical() << "a password login without saved credentials did not ask for them";
        return false;
    }

    emit fixture.prompter.loginSubmitted("alice", "secret", true);
    // A second answer, say from a dialog left open, must not start again.
    emit fixture.prompter.loginSubmitted("mallory", "other", true);
    const bool passed = fixture.coreProcess->startCalls == 1
        && fixture.starts == 1
        && fixture.coreProcess->lastProfile.credentials.username == "alice"
        && fixture.coreProcess->lastProfile.credentials.password == "secret"
        && fixture.coreProcess->lastProfile.profileId == "work"
        && ProfileSettings::read(fixture.settings, ProfileSettings::Username) == "alice"
        && ProfileSettings::read(fixture.settings, ProfileSettings::Password) == "secret";
    if (!passed)
    {
        qCritical() << "asksForMissingCredentialsBeforeStarting failed";
    }
    return passed;
}

bool ignoresALoginNobodyAskedFor()
{
    Fixture fixture;
    emit fixture.prompter.loginSubmitted("mallory", "other", true);

    const bool passed = fixture.coreProcess->startCalls == 0
        && ProfileSettings::read(fixture.settings, ProfileSettings::Username).isEmpty();
    if (!passed)
    {
        qCritical() << "ignoresALoginNobodyAskedFor failed";
    }
    return passed;
}

bool asksForThePhoneNumberOfAnSmsLogin()
{
    Fixture fixture;
    fixture.settings.setValue("ZJUConnect/AuthType", "smsCheckCode");
    fixture.flow.connectToServer();
    if (fixture.prompter.requests != QStringList{"phone:86-"})
    {
        qCritical() << "an SMS login without a phone number did not ask for one";
        return false;
    }

    emit fixture.prompter.phoneNumberSubmitted("86", "13800000000", false);
    const bool passed = fixture.coreProcess->startCalls == 1
        && fixture.coreProcess->lastProfile.endpoint.phone == "86-13800000000"
        // Not asked to remember it.
        && ProfileSettings::read(fixture.settings, ProfileSettings::PhoneNumber).isEmpty();
    if (!passed)
    {
        qCritical() << "asksForThePhoneNumberOfAnSmsLogin failed";
    }
    return passed;
}

bool passesTheCoresPromptsToTheUserAndTheAnswersBack()
{
    Fixture fixture;
    fixture.flow.connectToServer();
    emit fixture.coreProcess->smsCode(true);
    emit fixture.coreProcess->randCode();
    emit fixture.coreProcess->graphCaptcha("/tmp/graph.jpg");
    emit fixture.coreProcess->ssoAuth();

    const QStringList expected{
        "sms:skip",
        "sms:plain",
        // aTrust captchas are answered by picking points on the picture.
        "captcha:/tmp/graph.jpg:points",
        "sso:https://vpn.example.edu:8443 "
        "https://vpn.example.edu:8443/passport/v1/public/casLogin?sfDomain=campus",
    };
    if (fixture.prompter.requests != expected)
    {
        qCritical().noquote() << "prompts were not passed on as expected:\n"
                              << fixture.prompter.requests.join('\n');
        return false;
    }

    emit fixture.prompter.interactiveInputSubmitted("123456\n");
    if (fixture.coreProcess->lastInput != "123456\n")
    {
        qCritical() << "an answer was not passed back to the core";
        return false;
    }

    emit fixture.prompter.interactiveInputCancelled();
    if (fixture.coreProcess->lastInput != "\r\n" || fixture.coreProcess->stopCalls != 1)
    {
        qCritical() << "cancelling a prompt did not stop the session";
        return false;
    }
    return true;
}

bool easyConnectCaptchasAreTyped()
{
    Fixture fixture;
    fixture.settings.setValue("ZJUConnect/Protocol", "easyconnect");
    fixture.settings.setValue("Credential/Username", "alice");
    ProfileSettings::write(fixture.settings, ProfileSettings::Password, "secret");
    fixture.flow.connectToServer();
    emit fixture.coreProcess->graphCaptcha("/tmp/graph.jpg");

    if (fixture.prompter.requests != QStringList{"captcha:/tmp/graph.jpg:text"})
    {
        qCritical() << "an EasyConnect captcha was not asked for as text";
        return false;
    }
    return true;
}

bool reportsWhatWentWrong()
{
    Fixture fixture;
    fixture.flow.connectToServer();
    emit fixture.coreProcess->error(ZJU_ERROR::INVALID_DETAIL);
    emit fixture.coreProcess->finished();

    const bool passed = fixture.ends == 1
        && fixture.drops == 1
        && fixture.failures == QStringList{ConnectionFlow::describe(ZJU_ERROR::INVALID_DETAIL)}
        && !fixture.failures.first().isEmpty();
    if (!passed)
    {
        qCritical() << "reportsWhatWentWrong failed";
    }
    return passed;
}

bool aRequestedDisconnectEndsQuietly()
{
    Fixture fixture;
    fixture.flow.connectToServer();
    emit fixture.coreProcess->connectionEstablished();
    fixture.flow.disconnectFromServer();
    emit fixture.coreProcess->finished();

    const bool passed = fixture.coreProcess->stopCalls == 1
        && fixture.ends == 1
        && fixture.drops == 0
        && fixture.failures.isEmpty();
    if (!passed)
    {
        qCritical() << "aRequestedDisconnectEndsQuietly failed";
    }
    return passed;
}

bool asksBeforeReplacingAnotherSystemProxy()
{
    Fixture refused;
    refused.settings.setValue("Common/AutoSetProxy", true);
    refused.proxyCalls.conflict = true;
    refused.flow.connectToServer();
    emit refused.coreProcess->connectionEstablished();
    if (!waitFor([&]() { return refused.prompter.requests.contains("proxy-overwrite"); }, 5000))
    {
        qCritical() << "another proxy was about to be replaced without asking";
        return false;
    }
    processEventsFor(200);
    if (refused.proxyCalls.applies != 0)
    {
        qCritical() << "the system proxy was replaced although the user refused";
        return false;
    }

    Fixture agreed;
    agreed.settings.setValue("Common/AutoSetProxy", true);
    agreed.proxyCalls.conflict = true;
    agreed.prompter.proxyOverwriteAnswer = {true, true};
    agreed.flow.connectToServer();
    emit agreed.coreProcess->connectionEstablished();
    const bool passed = waitFor([&]() { return agreed.proxyCalls.applies == 1; }, 5000)
        && ProfileSettings::read(agreed.settings, ProfileSettings::SuppressProxyOverrideWarning);
    if (!passed)
    {
        qCritical() << "the system proxy was not set after the user agreed";
    }
    return passed;
}

bool theAutomaticProxyCanBeOverridden()
{
    Fixture forcedOff;
    forcedOff.settings.setValue("Common/AutoSetProxy", true);
    forcedOff.flow.setAutomaticProxyOverride(false);
    forcedOff.flow.connectToServer();
    emit forcedOff.coreProcess->connectionEstablished();
    processEventsFor(200);

    Fixture forcedOn;
    forcedOn.flow.setAutomaticProxyOverride(true);
    forcedOn.flow.connectToServer();
    emit forcedOn.coreProcess->connectionEstablished();

    const bool passed = waitFor([&]() { return forcedOn.proxyCalls.applies == 1; }, 5000)
        && forcedOff.proxyCalls.applies == 0;
    if (!passed)
    {
        qCritical() << "theAutomaticProxyCanBeOverridden failed";
    }
    return passed;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return refusesAProfileThatCannotConnect()
        && asksForMissingCredentialsBeforeStarting()
        && ignoresALoginNobodyAskedFor()
        && asksForThePhoneNumberOfAnSmsLogin()
        && passesTheCoresPromptsToTheUserAndTheAnswersBack()
        && easyConnectCaptchasAreTyped()
        && reportsWhatWentWrong()
        && aRequestedDisconnectEndsQuietly()
        && asksBeforeReplacingAnotherSystemProxy()
        && theAutomaticProxyCanBeOverridden() ? 0 : 1;
}
