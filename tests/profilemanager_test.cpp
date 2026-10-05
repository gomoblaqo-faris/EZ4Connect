#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QTemporaryDir>

#include "infrastructure/settings/profilemanager.h"

namespace
{
bool createsPhysicalFilesForEmptyProfiles()
{
    QTemporaryDir directory;
    ProfileManager manager(directory.path());

    if (!manager.activeProfile().isEmpty()
        || !QFileInfo::exists(manager.profilePath(""))
        || !manager.setActiveProfile(""))
    {
        qCritical() << "default profile file was not created";
        return false;
    }

    const QString profileId = manager.createProfile("empty");
    if (profileId != "empty"
        || !QFileInfo::exists(manager.profilePath(profileId))
        || !manager.listProfiles().contains(profileId)
        || !manager.setActiveProfile(profileId))
    {
        qCritical() << "empty named profile file was not created";
        return false;
    }
    return true;
}

bool keepsProfilesPrivateToTheirOwner()
{
#ifdef Q_OS_UNIX
    QTemporaryDir directory;
    const QString root = directory.filePath("config");
    ProfileManager manager(root);
    const QString created = manager.createProfile("private");
    const QString copied = manager.createProfile("copy", manager.profilePath(created));

    const QFileDevice::Permissions others =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    for (const QString &path : {root,
                                root + "/profiles",
                                manager.profilePath(created),
                                manager.profilePath(copied)})
    {
        if (!QFileInfo::exists(path) || (QFileInfo(path).permissions() & others))
        {
            qCritical() << "readable by other users, or missing:" << path;
            return false;
        }
    }
#endif
    return true;
}

bool remembersTheInterfaceLanguageForEveryProfile()
{
    QTemporaryDir directory;
    if (!ProfileManager(directory.path()).language().isEmpty())
    {
        qCritical() << "a new installation should follow the system language";
        return false;
    }

    ProfileManager(directory.path()).setLanguage("zh_CN");
    ProfileManager reopened(directory.path());
    const QString profileId = reopened.createProfile("work");
    reopened.setActiveProfile(profileId);
    if (reopened.language() != "zh_CN")
    {
        qCritical() << "the language was not kept:" << reopened.language();
        return false;
    }

    reopened.setLanguage("");
    return ProfileManager(directory.path()).language().isEmpty();
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return createsPhysicalFilesForEmptyProfiles()
        && keepsProfilesPrivateToTheirOwner()
        && remembersTheInterfaceLanguageForEveryProfile() ? 0 : 1;
}
