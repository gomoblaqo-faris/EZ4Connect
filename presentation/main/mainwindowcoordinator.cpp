#include "mainwindowcoordinator.h"

#include <memory>

#include <QDebug>

#include "application/applicationconstants.h"
#include "application/connectionsession.h"
#include "application/profileservice.h"
#include "application/profilesettings.h"
#include "application/secretstore.h"
#include "application/systemproxysession.h"
#include "infrastructure/coreprocess/zjuconnectprocess.h"
#include "infrastructure/platform/platformsystemproxybackend.h"
#ifdef EZ4CONNECT_HAS_KEYCHAIN
#include "infrastructure/secrets/keychainsecretstore.h"
#endif
#include "infrastructure/settings/profilemanager.h"
#include "infrastructure/update/updatechecker.h"
#include "presentation/coordinators/authdialogcoordinator.h"

MainWindowCoordinator::MainWindowCoordinator(
    QWidget *parentWidget,
    const QString &overrideConfigPath,
    QObject *parent
)
    : QObject(parent),
      profileService(new ProfileService(
          std::make_unique<ProfileManager>(),
          overrideConfigPath,
          this
      )),
      connectionSession(new ConnectionSession(new ZjuConnectProcess(), this)),
      systemProxySession(new SystemProxySession(
          std::make_unique<PlatformSystemProxyBackend>(),
          this
      )),
      updateChecker(new UpdateChecker(this)),
      authenticationCoordinator(new AuthDialogCoordinator(
          parentWidget,
          profileService->settings(),
          this
      ))
{
#ifdef EZ4CONNECT_HAS_KEYCHAIN
    secretStore = std::make_unique<KeychainSecretStore>(
        ApplicationConstants::ApplicationName
    );
#endif
    ProfileSettings::setSecretStore(secretStore.get());
    ProfileSettings::setPendingRemovalsFile(ProfileManager().stateFilePath());
    if (secretStore == nullptr)
    {
        qInfo().noquote()
            << "This build has no credential store support: saved passwords stay in the profile file";
    }
    else
    {
        ProfileSettings::retryPendingSecretRemovals();
    }

    connect(
        connectionSession,
        &ConnectionSession::askSudoPass,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestSudoPassword
    );
    connect(
        connectionSession,
        &ConnectionSession::graphCaptcha,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestGraphCaptcha
    );
    connect(
        connectionSession,
        &ConnectionSession::smsCode,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestSmsCode
    );
    connect(
        connectionSession,
        &ConnectionSession::totpCode,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestTotpCode
    );
    connect(
        connectionSession,
        &ConnectionSession::randCode,
        authenticationCoordinator,
        [this]() { authenticationCoordinator->requestSmsCode(false); }
    );
    connect(
        connectionSession,
        &ConnectionSession::radiusCode,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestRadiusCode
    );
    connect(
        connectionSession,
        &ConnectionSession::ssoAuth,
        authenticationCoordinator,
        &AuthDialogCoordinator::requestSsoLogin
    );
    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::sudoPasswordSubmitted,
        connectionSession,
        &ConnectionSession::submitSudoPassword
    );
    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::interactiveInputSubmitted,
        connectionSession,
        &ConnectionSession::submitInput
    );
    connect(
        authenticationCoordinator,
        &AuthDialogCoordinator::interactiveInputCancelled,
        connectionSession,
        &ConnectionSession::cancelInteractiveInput
    );

    connect(
        systemProxySession,
        &SystemProxySession::operationFinished,
        this,
        [this](bool enabled)
        {
            if (enabled && !connectionSession->isActive())
            {
                systemProxySession->disable();
            }
        }
    );
    connect(
        connectionSession,
        &ConnectionSession::finished,
        this,
        [this](ZJU_ERROR)
        {
            if (systemProxySession->isEnabled())
            {
                systemProxySession->disable();
            }
        }
    );
}

MainWindowCoordinator::~MainWindowCoordinator()
{
    ProfileSettings::setSecretStore(nullptr);
}

ProfileService *MainWindowCoordinator::profiles() const
{
    return profileService;
}

ConnectionSession *MainWindowCoordinator::connection() const
{
    return connectionSession;
}

SystemProxySession *MainWindowCoordinator::systemProxy() const
{
    return systemProxySession;
}

UpdateChecker *MainWindowCoordinator::updates() const
{
    return updateChecker;
}

AuthDialogCoordinator *
MainWindowCoordinator::authenticationDialogs() const
{
    return authenticationCoordinator;
}

void MainWindowCoordinator::prepareForShutdown()
{
    systemProxySession->clearBeforeShutdown();
}
