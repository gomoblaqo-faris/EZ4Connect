#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QSettings>
#include <QTemporaryDir>

#include "application/applicationconstants.h"
#include "application/defaultsettings.h"
#include "application/profileimport.h"
#include "application/profilesettings.h"
#include "application/secretstore.h"

namespace
{
class FakeSecretStore : public SecretStore
{
public:
    QHash<QString, QString> entries;

    std::optional<QString> read(const QString &account) override
    {
        return entries.contains(account)
            ? std::optional<QString>(entries.value(account))
            : std::nullopt;
    }

    bool write(const QString &account, const QString &secret) override
    {
        entries.insert(account, secret);
        return true;
    }

    bool remove(const QString &account) override
    {
        entries.remove(account);
        return true;
    }
};

bool nothingFromTheOldProfileSurvives()
{
    QTemporaryDir directory;
    QSettings destination(directory.filePath("destination.ini"), QSettings::IniFormat);
    DefaultSettings::reset(destination);
    ProfileSettings::write(destination, ProfileSettings::TUNMode, true);
    ProfileSettings::write(destination, ProfileSettings::Debug, true);
    ProfileSettings::write(destination, ProfileSettings::ExtraArguments, "-old-argument");
    ProfileSettings::write(destination, ProfileSettings::Password, "old-password");

    // A hand-written file that only names a server.
    QSettings source(directory.filePath("source.ini"), QSettings::IniFormat);
    source.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");
    ProfileImport::apply(destination, source, true);

    const bool passed =
        ProfileSettings::read(destination, ProfileSettings::ServerAddress) == "vpn.example.edu"
        && !ProfileSettings::read(destination, ProfileSettings::TUNMode)
        && !ProfileSettings::read(destination, ProfileSettings::Debug)
        && ProfileSettings::read(destination, ProfileSettings::ExtraArguments).isEmpty()
        && ProfileSettings::read(destination, ProfileSettings::Password).isEmpty()
        // Settings the file does not mention get a new profile's values.
        && ProfileSettings::read(destination, ProfileSettings::HTTPPort) == 11081;
    if (!passed)
    {
        qCritical() << "nothingFromTheOldProfileSurvives failed";
    }
    return passed;
}

bool extraArgumentsNeedAgreement()
{
    QTemporaryDir directory;
    QSettings source(directory.filePath("source.ini"), QSettings::IniFormat);
    source.setValue("ZJUConnect/ExtraArguments", " -debug-pcap-file /etc/passwd ");
    if (ProfileImport::extraArguments(source) != "-debug-pcap-file /etc/passwd")
    {
        qCritical() << "the extra arguments in a file were not reported";
        return false;
    }

    QSettings refused(directory.filePath("refused.ini"), QSettings::IniFormat);
    ProfileImport::apply(refused, source, false);
    QSettings agreed(directory.filePath("agreed.ini"), QSettings::IniFormat);
    ProfileImport::apply(agreed, source, true);

    const bool passed =
        ProfileSettings::read(refused, ProfileSettings::ExtraArguments).isEmpty()
        && ProfileSettings::read(agreed, ProfileSettings::ExtraArguments).trimmed()
            == "-debug-pcap-file /etc/passwd";
    if (!passed)
    {
        qCritical() << "extraArgumentsNeedAgreement failed";
    }
    return passed;
}

bool filesFromOlderVersionsAreMigrated()
{
    QTemporaryDir directory;
    // Version 4 predates aTrust support: it has no protocol and means
    // EasyConnect, not the protocol a new profile starts with.
    QSettings versionFour(directory.filePath("v4.ini"), QSettings::IniFormat);
    versionFour.setValue("Common/ConfigVersion", 4);
    versionFour.setValue("ZJUConnect/ServerAddress", "vpn.example.edu");

    QSettings destination(directory.filePath("destination.ini"), QSettings::IniFormat);
    ProfileImport::apply(destination, versionFour, false);
    if (ProfileSettings::read(destination, ProfileSettings::Protocol) != "easyconnect"
        || ProfileSettings::read(destination, ProfileSettings::ConfigVersion)
            != ApplicationConstants::ConfigVersion)
    {
        qCritical() << "a version 4 file was not migrated on import";
        return false;
    }

    QSettings newer(directory.filePath("newer.ini"), QSettings::IniFormat);
    newer.setValue("Common/ConfigVersion", ApplicationConstants::ConfigVersion + 1);
    QSettings second(directory.filePath("second.ini"), QSettings::IniFormat);
    ProfileImport::apply(second, newer, false);
    if (ProfileSettings::read(second, ProfileSettings::ConfigVersion)
        != ApplicationConstants::ConfigVersion + 1)
    {
        qCritical() << "a file from a newer version had its version lowered on import";
        return false;
    }
    return true;
}

bool importedSecretsMoveIntoTheStoreUnderTheProfilesOwnIdentifier()
{
    QTemporaryDir directory;
    FakeSecretStore store;
    ProfileSettings::setSecretStore(&store);

    QSettings destination(directory.filePath("destination.ini"), QSettings::IniFormat);
    ProfileSettings::write(destination, ProfileSettings::Password, "old-password");

    // A file exported by a version without a credential store, edited to
    // point at somebody else's store entries.
    QSettings source(directory.filePath("source.ini"), QSettings::IniFormat);
    source.setValue("Credential/Password", QString::fromLatin1(QByteArray("imported").toBase64()));
    source.setValue("Credential/SecretId", "someone-elses-identifier");
    ProfileImport::apply(destination, source, false);

    const QString secretId = ProfileSettings::read(destination, ProfileSettings::SecretId);
    const bool passed = secretId != "someone-elses-identifier"
        && !secretId.isEmpty()
        && !destination.contains("Credential/Password")
        && store.entries.size() == 1
        && ProfileSettings::read(destination, ProfileSettings::Password) == "imported";
    ProfileSettings::setSecretStore(nullptr);
    if (!passed)
    {
        qCritical() << "importedSecretsMoveIntoTheStoreUnderTheProfilesOwnIdentifier failed";
    }
    return passed;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return nothingFromTheOldProfileSurvives()
        && extraArgumentsNeedAgreement()
        && filesFromOlderVersionsAreMigrated()
        && importedSecretsMoveIntoTheStoreUnderTheProfilesOwnIdentifier() ? 0 : 1;
}
