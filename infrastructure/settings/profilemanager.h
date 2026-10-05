#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H

#include <QString>
#include <QStringList>

#include "application/profilebackend.h"

class ProfileManager : public ProfileBackend
{
public:
    explicit ProfileManager(const QString &storageRoot = {});

    QStringList listProfiles() const override;

    QString activeProfile() const override;

    bool setActiveProfile(const QString &profileId) const override;

    QString profilePath(const QString &profileId) const override;

    QString createProfile(
        const QString &requestedName,
        const QString &sourcePath = QString()
    ) const override;

    bool renameProfile(const QString &oldId, const QString &newId) const override;

    bool removeProfile(const QString &profileId) const override;

    QString normalizeProfileId(const QString &name) const override;

    bool autoStartEnabled() const;

    void setAutoStartEnabled(bool enabled) const override;

    bool silentStartEnabled() const override;

    void setSilentStartEnabled(bool enabled) const;

    // The interface language, shared by all profiles. Empty means "follow
    // the system".
    QString language() const;

    void setLanguage(const QString &language) const;

    // The file that holds state shared by all profiles.
    QString stateFilePath() const;

private:
    QString configRootPath;
    // False when the root is a location shared with other applications.
    bool ownsConfigRoot = true;
    QString profilesPath;
    QString statePath;
    QString defaultProfilePath;

    QString ensureUniqueProfileId(const QString &baseId) const;

    void ensureStorage() const;
};

#endif //PROFILEMANAGER_H
