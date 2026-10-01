#ifndef SETTINGSMIGRATOR_H
#define SETTINGSMIGRATOR_H

class QSettings;

enum class SettingsMigrationAction
{
    None,
    MigrateAutoStart,
    RecommendReset,
    // Written by a newer version of the app. It is used as it is, and its
    // version is left alone so that the newer version still recognises it.
    NewerThanApplication
};

class SettingsMigrator
{
public:
    static SettingsMigrationAction prepare(QSettings &settings);
    static void finish(QSettings &settings, bool resetToDefaults);
};

#endif // SETTINGSMIGRATOR_H
