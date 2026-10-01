#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

#include "application/applicationconstants.h"
#include "application/settingsmigrator.h"

namespace
{
bool migratesKnownVersions()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("Common/ConfigVersion", 4);

    const auto action = SettingsMigrator::prepare(settings);
    const bool passed =
        action == SettingsMigrationAction::None
        && settings.value("ZJUConnect/Protocol").toString() == "easyconnect";
    if (!passed)
    {
        qCritical() << "migratesKnownVersions failed";
    }
    return passed;
}

bool recommendsResetForUnsupportedVersions()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("profile.ini"), QSettings::IniFormat);
    settings.setValue("Common/ConfigVersion", 5);
    const bool passed =
        SettingsMigrator::prepare(settings) ==
        SettingsMigrationAction::RecommendReset;
    if (!passed)
    {
        qCritical() << "recommendsResetForUnsupportedVersions failed";
    }
    return passed;
}

bool finishingBringsAnOlderProfileUpToTheCurrentVersion()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("older.ini"), QSettings::IniFormat);
    settings.setValue("Common/ConfigVersion", 5);
    settings.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");

    SettingsMigrator::finish(settings, false);
    const bool passed =
        settings.value("Common/ConfigVersion").toInt() == ApplicationConstants::ConfigVersion
        && settings.value("ZJUConnect/ServerAddress").toString() == "vpn.example.edu";
    if (!passed)
    {
        qCritical() << "finishingBringsAnOlderProfileUpToTheCurrentVersion failed";
    }
    return passed;
}

bool resettingReplacesEverySetting()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("reset.ini"), QSettings::IniFormat);
    settings.setValue("Common/ConfigVersion", 5);
    settings.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");
    settings.setValue("ZJUConnect/TUNMode", true);

    SettingsMigrator::finish(settings, true);
    const bool passed =
        settings.value("Common/ConfigVersion").toInt() == ApplicationConstants::ConfigVersion
        && settings.value("ZJUConnect/ServerAddress").toString() == "trust.hitsz.edu.cn"
        && !settings.value("ZJUConnect/TUNMode").toBool();
    if (!passed)
    {
        qCritical() << "resettingReplacesEverySetting failed";
    }
    return passed;
}

bool aProfileFromANewerVersionKeepsItsVersion()
{
    QTemporaryDir directory;
    QSettings settings(directory.filePath("newer.ini"), QSettings::IniFormat);
    const int newerVersion = ApplicationConstants::ConfigVersion + 1;
    settings.setValue("Common/ConfigVersion", newerVersion);

    // Lowering the version would make the newer app migrate its own profile
    // again as if it were an old one.
    const SettingsMigrationAction action = SettingsMigrator::prepare(settings);
    SettingsMigrator::finish(settings, false);
    const bool passed = action == SettingsMigrationAction::NewerThanApplication
        && settings.value("Common/ConfigVersion").toInt() == newerVersion;
    if (!passed)
    {
        qCritical() << "aProfileFromANewerVersionKeepsItsVersion failed";
    }
    return passed;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return migratesKnownVersions()
        && recommendsResetForUnsupportedVersions()
        && finishingBringsAnOlderProfileUpToTheCurrentVersion()
        && resettingReplacesEverySetting()
        && aProfileFromANewerVersionKeepsItsVersion() ? 0 : 1;
}
