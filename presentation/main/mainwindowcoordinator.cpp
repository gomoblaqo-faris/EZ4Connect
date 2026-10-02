#include "mainwindowcoordinator.h"

#include <memory>

#include <QDebug>

#include "application/applicationconstants.h"
#include "application/connectionflow.h"
#include "application/connectionsession.h"
#include "application/profileservice.h"
#include "application/profilesettings.h"
#include "application/secretstore.h"
#include "application/systemproxysession.h"
#include "infrastructure/coreprocess/coreexecutable.h"
#include "infrastructure/coreprocess/zjuconnectprocess.h"
#include "infrastructure/platform/platformsystemproxybackend.h"
#ifdef EZ4CONNECT_HAS_KEYCHAIN
#include "infrastructure/secrets/keychainsecretstore.h"
#endif
#include "infrastructure/settings/profilemanager.h"
#include "infrastructure/settings/settingsprofileloader.h"
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
      authenticationCoordinator(new AuthDialogCoordinator(parentWidget, this)),
      flow(new ConnectionFlow(
          connectionSession,
          systemProxySession,
          authenticationCoordinator,
          [this]() { return profileService->settings(); },
          [this]() { return profileService->currentProfileId(); },
          [](const QSettings &settings,
             const QString &profileId,
             const QString &username,
             const QString &password)
          {
              ConnectionProfile profile =
                  SettingsProfileLoader::load(settings, profileId, username, password);
              profile.program = CoreExecutable::path();
              return profile;
          },
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
}

MainWindowCoordinator::~MainWindowCoordinator()
{
    ProfileSettings::setSecretStore(nullptr);
}

ProfileService *MainWindowCoordinator::profiles() const
{
    return profileService;
}

ConnectionFlow *MainWindowCoordinator::connectionFlow() const
{
    return flow;
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
