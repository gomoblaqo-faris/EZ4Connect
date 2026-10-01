#include "applicationpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
QString safeProfileName(const QString &profileId)
{
    QString name = profileId.isEmpty() ? "default" : profileId;
    name.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    return name;
}

// Login data and logs can reveal accounts and session tokens, so the
// directory that holds them is closed to other users of the machine. That
// also covers directories created by earlier versions and every file inside.
QString privateDataRoot()
{
    const QString root =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(root);
    // Without an application name the location is the one every application
    // shares, which is not this application's to close.
    if (!QCoreApplication::applicationName().isEmpty())
    {
        QFile::setPermissions(
            root,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        );
    }
    return root;
}

QString profileDataDirectory(const QString &profileId)
{
    return QDir(privateDataRoot()).filePath("profiles/" + profileId);
}

bool isNamedProfileId(const QString &profileId)
{
    static const QRegularExpression pattern(
        QRegularExpression::anchoredPattern("[A-Za-z0-9_-]+")
    );
    return pattern.match(profileId).hasMatch();
}

void createPrivateFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
}

QString ApplicationPaths::clientDataFile(const QString &profileId)
{
    QDir profileDirectory(profileDataDirectory(profileId));
    if (!profileDirectory.exists())
    {
        profileDirectory.mkpath(".");
    }

    QFile clientData(profileDirectory.filePath("client-data.json"));
    if (!clientData.exists() && clientData.open(QIODevice::WriteOnly))
    {
        clientData.write("{}");
        clientData.close();
        clientData.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    return clientData.fileName();
}

void ApplicationPaths::clearClientData(const QString &profileId)
{
    QFile::remove(clientDataFile(profileId));
}

bool ApplicationPaths::removeProfileData(const QString &profileId)
{
    if (!isNamedProfileId(profileId))
    {
        return false;
    }

    QDir profileDirectory(profileDataDirectory(profileId));
    return !profileDirectory.exists() || profileDirectory.removeRecursively();
}

bool ApplicationPaths::moveProfileData(
    const QString &oldProfileId,
    const QString &newProfileId
)
{
    if (!isNamedProfileId(oldProfileId)
        || !isNamedProfileId(newProfileId)
        || oldProfileId == newProfileId)
    {
        return false;
    }

    // Anything already under the new name belongs to a profile that no
    // longer exists.
    if (!removeProfileData(newProfileId))
    {
        return false;
    }

    const QString source = profileDataDirectory(oldProfileId);
    if (!QFileInfo::exists(source))
    {
        return true;
    }
    return QDir().rename(source, profileDataDirectory(newProfileId));
}

QString ApplicationPaths::logDirectory()
{
    QDir logDirectory(QDir(privateDataRoot()).filePath("logs"));
    if (!logDirectory.exists())
    {
        logDirectory.mkpath(".");
    }
    return logDirectory.absolutePath();
}

QString ApplicationPaths::logFile()
{
    return QDir(logDirectory()).filePath("ez4connect.log");
}

ApplicationPaths::DebugArtifactPaths ApplicationPaths::createDebugArtifactFiles(
    const QString &profileId,
    bool createPcap,
    bool createTlsLog
)
{
    const QString baseName = QString("debug-%1-%2")
        .arg(
            safeProfileName(profileId),
            QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")
        );
    const QDir directory(logDirectory());

    DebugArtifactPaths paths;
    if (createPcap)
    {
        paths.pcapFile = directory.filePath(baseName + ".pcap");
        createPrivateFile(paths.pcapFile);
    }
    if (createTlsLog)
    {
        paths.tlsLogFile = directory.filePath(baseName + ".keys.log");
        createPrivateFile(paths.tlsLogFile);
    }
    return paths;
}
