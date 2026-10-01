#include "application/profileimport.h"

#include <QSettings>

#include "application/defaultsettings.h"
#include "application/profilesettings.h"
#include "application/settingsmigrator.h"

QString ProfileImport::extraArguments(const QSettings &source)
{
    return ProfileSettings::read(source, ProfileSettings::ExtraArguments).trimmed();
}

void ProfileImport::apply(QSettings &destination, const QSettings &source, bool includeExtraArguments)
{
    ProfileSettings::forgetSecrets(destination);
    destination.clear();
    DefaultSettings::reset(destination);

    for (const QString &key : source.allKeys())
    {
        // The identifier names another profile's saved secrets.
        if (key == ProfileSettings::SecretId.name)
        {
            continue;
        }
        if (key == ProfileSettings::ExtraArguments.name && !includeExtraArguments)
        {
            continue;
        }
        destination.setValue(key, source.value(key));
    }

    // A file from an older version is brought up to date the way a profile
    // is when the app opens it, except that a reset is never suggested here
    // (it would discard the import) and the launch-at-login setting, which is
    // shared by all profiles, is not taken from a file.
    SettingsMigrator::prepare(destination);
    SettingsMigrator::finish(destination, false);

    ProfileSettings::migrateSecrets(destination);
    destination.sync();
}
