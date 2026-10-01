#ifndef APPLICATIONPATHS_H
#define APPLICATIONPATHS_H

#include <QString>

namespace ApplicationPaths
{
struct DebugArtifactPaths
{
    QString pcapFile;
    QString tlsLogFile;
};

QString clientDataFile(const QString &profileId);
void clearClientData(const QString &profileId);
// Login cache and device-trust state live per profile, so they have to follow
// the profile when it is renamed and go away when it is deleted. Both refuse
// the default profile, whose data sits beside every other profile's.
bool removeProfileData(const QString &profileId);
bool moveProfileData(const QString &oldProfileId, const QString &newProfileId);
QString logDirectory();
QString logFile();
DebugArtifactPaths createDebugArtifactFiles(
    const QString &profileId,
    bool createPcap,
    bool createTlsLog
);
}

#endif // APPLICATIONPATHS_H
