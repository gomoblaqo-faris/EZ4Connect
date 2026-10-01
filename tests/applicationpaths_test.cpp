#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "infrastructure/storage/applicationpaths.h"

namespace
{
bool writeClientData(const QString &profileId, const QByteArray &content)
{
    QFile file(ApplicationPaths::clientDataFile(profileId));
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(content) == content.size();
}

QByteArray readClientData(const QString &profileId)
{
    QFile file(ApplicationPaths::clientDataFile(profileId));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString profileDirectory(const QString &profileId)
{
    return QFileInfo(ApplicationPaths::clientDataFile(profileId)).absolutePath();
}

bool movesClientDataWithARenamedProfile()
{
    if (!writeClientData("alpha", "alpha-session"))
    {
        qCritical() << "could not prepare client data for the rename test";
        return false;
    }
    const QString oldDirectory = profileDirectory("alpha");

    if (!ApplicationPaths::moveProfileData("alpha", "beta")
        || QFileInfo::exists(oldDirectory)
        || readClientData("beta") != "alpha-session")
    {
        qCritical() << "client data did not follow the renamed profile";
        return false;
    }
    return true;
}

bool renameReplacesDataLeftUnderTheNewName()
{
    if (!writeClientData("gamma", "stale-session")
        || !writeClientData("delta", "fresh-session"))
    {
        qCritical() << "could not prepare client data for the stale-target test";
        return false;
    }

    if (!ApplicationPaths::moveProfileData("delta", "gamma")
        || readClientData("gamma") != "fresh-session")
    {
        qCritical() << "stale client data survived a rename onto its name";
        return false;
    }
    return true;
}

bool renameWithoutClientDataStillClearsTheNewName()
{
    if (!writeClientData("epsilon", "stale-session"))
    {
        qCritical() << "could not prepare client data for the empty-source test";
        return false;
    }
    const QString staleDirectory = profileDirectory("epsilon");

    if (!ApplicationPaths::moveProfileData("never-connected", "epsilon")
        || QFileInfo::exists(staleDirectory))
    {
        qCritical() << "stale client data survived a rename from an unused profile";
        return false;
    }
    return true;
}

bool removesClientDataOfADeletedProfile()
{
    if (!writeClientData("zeta", "zeta-session"))
    {
        qCritical() << "could not prepare client data for the delete test";
        return false;
    }
    const QString directory = profileDirectory("zeta");

    if (!ApplicationPaths::removeProfileData("zeta")
        || QFileInfo::exists(directory)
        || !ApplicationPaths::removeProfileData("zeta"))
    {
        qCritical() << "client data of a deleted profile was left behind";
        return false;
    }
    return true;
}

bool keepsDataPrivateToItsOwner()
{
#ifdef Q_OS_UNIX
    const QFileDevice::Permissions others =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    const QString clientData = ApplicationPaths::clientDataFile("private");
    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    ApplicationPaths::logDirectory();

    if ((QFileInfo(dataRoot).permissions() & others)
        || (QFileInfo(clientData).permissions() & others))
    {
        qCritical() << "login data is readable by other users";
        return false;
    }
#endif
    return true;
}

bool refusesTheDefaultProfileAndUnsafeNames()
{
    if (!writeClientData("", "default-session")
        || !writeClientData("eta", "eta-session"))
    {
        qCritical() << "could not prepare client data for the default-profile test";
        return false;
    }

    // The default profile's directory is the parent of every other profile's,
    // so removing or moving it would take the rest with it.
    if (ApplicationPaths::removeProfileData("")
        || ApplicationPaths::removeProfileData("../eta")
        || ApplicationPaths::removeProfileData("eta/..")
        || ApplicationPaths::moveProfileData("", "theta")
        || ApplicationPaths::moveProfileData("eta", "")
        || ApplicationPaths::moveProfileData("eta", "eta")
        || readClientData("") != "default-session"
        || readClientData("eta") != "eta-session")
    {
        qCritical() << "an unsafe profile name was accepted";
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName("EZ4Connect-applicationpaths-test");
    QStandardPaths::setTestModeEnabled(true);

    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir(dataRoot).removeRecursively();

    const bool passed = movesClientDataWithARenamedProfile()
        && renameReplacesDataLeftUnderTheNewName()
        && renameWithoutClientDataStillClearsTheNewName()
        && removesClientDataOfADeletedProfile()
        && keepsDataPrivateToItsOwner()
        && refusesTheDefaultProfileAndUnsafeNames();

    QDir(dataRoot).removeRecursively();
    return passed ? 0 : 1;
}
