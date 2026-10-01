#include "settingsmigrator.h"

#include <QSettings>

#include "application/applicationconstants.h"
#include "application/defaultsettings.h"
#include "application/profilesettings.h"

SettingsMigrationAction SettingsMigrator::prepare(QSettings &settings)
{
    const int configVersion = ProfileSettings::read(settings, ProfileSettings::ConfigVersion);
    if (configVersion == -1)
    {
        DefaultSettings::reset(settings);
        return SettingsMigrationAction::None;
    }
    if (configVersion == 4)
    {
        ProfileSettings::write(settings, ProfileSettings::Protocol, "easyconnect");
        return SettingsMigrationAction::None;
    }
    if (configVersion == 6)
    {
        return SettingsMigrationAction::MigrateAutoStart;
    }
    if (configVersion < ApplicationConstants::ConfigVersion)
    {
        return SettingsMigrationAction::RecommendReset;
    }
    if (configVersion > ApplicationConstants::ConfigVersion)
    {
        return SettingsMigrationAction::NewerThanApplication;
    }
    return SettingsMigrationAction::None;
}

void SettingsMigrator::finish(QSettings &settings, bool resetToDefaults)
{
    if (resetToDefaults)
    {
        ProfileSettings::forgetSecrets(settings);
        settings.clear();
        DefaultSettings::reset(settings);
    }
    // Never lower the version: a newer version of the app would then take
    // its own profile for an old one and migrate it again.
    if (ProfileSettings::read(settings, ProfileSettings::ConfigVersion)
        < ApplicationConstants::ConfigVersion)
    {
        ProfileSettings::write(
            settings,
            ProfileSettings::ConfigVersion,
            ApplicationConstants::ConfigVersion
        );
    }
    settings.sync();
}
